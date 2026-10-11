// Mode 4: the walkie talkie. Task layout follows the RTOS design in the design doc.
//
//  core 1 (audio)                          core 0 (network, UI)
//  audio_capture  prio 19  I2S DMA paced   radio_rx  prio 17  message buffer from ESP-NOW callback
//  audio_encode   prio 18  frame queue     radio_tx  prio 16  packet queue
//  audio_playback prio 20  I2S DMA paced   link_mgr  prio 10  beacons, floor timeout, roster aging
//                                          ui        prio 8   input events, menus (walkie_ui.c)
//                                          display   prio 5   OLED at 10 Hz (walkie_ui.c)
//                                          monitor   prio 2   stats, run-time stats, stack marks
//
// TX path: mic -> capture -> [frame queue] -> encode (ADPCM + AES-CCM) -> [packet queue] -> radio_tx
// RX path: ESP-NOW callback -> [message buffer] -> radio_rx (auth, replay, floor) -> [jitter buffer]
//          -> playback (decode or conceal) -> speaker
#include <stdio.h>
#include <string.h>

#include "adpcm.h"
#include "app_common.h"
#include "audio_io.h"
#include "battery.h"
#include "crypto.h"
#include "dbg_pins.h"
#include "espnow_link.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "jbuf.h"
#include "nvs.h"
#include "packet.h"
#include "sdkconfig.h"
#include "walkie.h"

static const char *TAG = "walkie";

EventGroupHandle_t g_ev;
wt_stats_t g_stats;
wt_settings_t g_settings;

#define FLOOR_TIMEOUT_MS  300    // channel counts as free this long after the last audio packet
#define PEER_TIMEOUT_MS   7000   // drop a unit from the roster after this much silence
#define SEQ_BLOCK         10000  // sequence numbers reserved in NVS at a time (see seq_next)
#define POOL_FRAMES       6
#define TX_QUEUE_LEN      6
#define JB_MISS_LIMIT     10     // 200 ms of missing frames ends a stream that lost its EOT packet

typedef struct {
    int16_t pcm[AUDIO_FRAME_SAMPLES];
    bool eot;
} frame_t;

typedef struct {
    uint16_t len;
    uint8_t data[PKT_MAX_BYTES];
} tx_pkt_t;

// Static pool of PCM frames, passed by pointer: no malloc in the audio path.
static frame_t s_pool[POOL_FRAMES];
static QueueHandle_t s_free_q;  // frame_t* ready to fill
static QueueHandle_t s_cap_q;   // frame_t* waiting to be encoded
static QueueHandle_t s_tx_q;    // tx_pkt_t by value
static MessageBufferHandle_t s_rx_mb;

// Jitter buffer: written by radio_rx on core 0, read by playback on core 1.
// A spinlock critical section works across both cores and is held for microseconds.
static jbuf_t s_jb;
static portMUX_TYPE s_jb_mux = portMUX_INITIALIZER_UNLOCKED;

// Floor control: who is talking on our channel right now.
static portMUX_TYPE s_floor_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_talker, s_talker_last_ms, s_talker_last_seq;

static SemaphoreHandle_t s_roster_mtx;
static wt_peer_t s_peers[WT_MAX_PEERS];

static SemaphoreHandle_t s_settings_mtx;
static replay_t s_replay;  // only touched by radio_rx
static uint32_t s_my_id;

static volatile int s_tone_hz, s_tone_frames;

static uint32_t s_seq;
static volatile uint32_t s_seq_limit;
static volatile bool s_seq_commit_needed;

static TaskHandle_t s_link_task;
static TaskHandle_t s_tasks[8];
static const char *s_task_names[8];
static int s_task_count;

// ---------------------------------------------------------------------------
// Settings and sequence numbers in NVS

static void settings_load(void)
{
    g_settings.channel = CONFIG_WT_DEFAULT_CHANNEL;
    g_settings.volume = 5;
    g_settings.long_range = WT_START_LONG_RANGE;
    snprintf(g_settings.name, sizeof(g_settings.name), "U%04X", (unsigned)(s_my_id & 0xFFFF));

    nvs_handle_t h;
    if (nvs_open("wt", NVS_READONLY, &h) == ESP_OK) {
        wt_settings_t saved;
        size_t len = sizeof(saved);
        if (nvs_get_blob(h, "settings", &saved, &len) == ESP_OK && len == sizeof(saved) && saved.channel >= 1 &&
            saved.channel <= 8 && saved.volume <= WT_VOLUME_MAX) {
            saved.name[WT_NAME_LEN] = 0;
            g_settings = saved;
        }
        nvs_close(h);
    }
}

void wt_settings_save(void)
{
    xSemaphoreTake(s_settings_mtx, portMAX_DELAY);
    wt_settings_t copy = g_settings;
    xSemaphoreGive(s_settings_mtx);
    nvs_handle_t h;
    if (nvs_open("wt", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "settings", &copy, sizeof(copy));
        nvs_commit(h);
        nvs_close(h);
    }
}

// AES-CCM must never reuse a nonce (sender + seq + channel) with the same key. A
// reboot would restart the counter, so we reserve blocks of sequence numbers in
// NVS: on boot, start at the last reserved limit and reserve the next block.
static void seq_load(void)
{
    nvs_handle_t h;
    uint32_t hi = 0;
    if (nvs_open("wt", NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, "seq_hi", &hi);
        s_seq = hi;
        s_seq_limit = hi + SEQ_BLOCK;
        nvs_set_u32(h, "seq_hi", s_seq_limit);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "sequence numbers start at %lu", (unsigned long)s_seq);
}

static void seq_commit(void)  // monitor task, so the slow flash write stays out of the audio path
{
    uint32_t next = s_seq_limit + SEQ_BLOCK;
    nvs_handle_t h;
    if (nvs_open("wt", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "seq_hi", next);
        nvs_commit(h);
        nvs_close(h);
        s_seq_limit = next;
    }
    s_seq_commit_needed = false;
}

static uint32_t seq_next(void)
{
    uint32_t s = __atomic_fetch_add(&s_seq, 1, __ATOMIC_RELAXED);
    if ((int32_t)(s - (s_seq_limit - SEQ_BLOCK / 2)) >= 0) s_seq_commit_needed = true;
    return s;
}

// ---------------------------------------------------------------------------
// Roster of units heard on our channel

static wt_peer_t *peer_slot(uint32_t id, uint32_t now)
{
    wt_peer_t *free_slot = NULL, *oldest = &s_peers[0];
    for (int i = 0; i < WT_MAX_PEERS; i++) {
        if (s_peers[i].id == id) return &s_peers[i];
        if (!s_peers[i].id && !free_slot) free_slot = &s_peers[i];
        if ((int32_t)(s_peers[i].last_ms - oldest->last_ms) < 0) oldest = &s_peers[i];
    }
    wt_peer_t *p = free_slot ? free_slot : oldest;
    memset(p, 0, sizeof(*p));
    p->id = id;
    snprintf(p->name, sizeof(p->name), "U%04X", (unsigned)(id & 0xFFFF));
    p->last_ms = now;
    return p;
}

static void roster_heard(uint32_t id, int8_t rssi, bool lr, const beacon_t *b, uint32_t now)
{
    xSemaphoreTake(s_roster_mtx, portMAX_DELAY);
    wt_peer_t *p = peer_slot(id, now);
    p->rssi = rssi;
    p->long_range = lr;
    p->last_ms = now;
    if (b) {
        if (b->name[0]) strlcpy(p->name, b->name, sizeof(p->name));
        p->battery_pct = b->battery_pct;
        p->talking = (b->flags & BEACON_F_TALKING) != 0;
    }
    xSemaphoreGive(s_roster_mtx);
}

int wt_roster(wt_peer_t *out, int max)
{
    int n = 0;
    xSemaphoreTake(s_roster_mtx, portMAX_DELAY);
    for (int i = 0; i < WT_MAX_PEERS && n < max; i++) {
        if (s_peers[i].id) out[n++] = s_peers[i];
    }
    xSemaphoreGive(s_roster_mtx);
    return n;
}

bool wt_peer_name(uint32_t id, char *out)
{
    bool found = false;
    xSemaphoreTake(s_roster_mtx, portMAX_DELAY);
    for (int i = 0; i < WT_MAX_PEERS; i++) {
        if (s_peers[i].id == id) {
            strlcpy(out, s_peers[i].name, WT_NAME_LEN + 1);
            found = true;
        }
    }
    xSemaphoreGive(s_roster_mtx);
    if (!found) snprintf(out, WT_NAME_LEN + 1, "U%04X", (unsigned)(id & 0xFFFF));
    return found;
}

// ---------------------------------------------------------------------------
// Audio tasks (core 1)

static int volume_gain_q8(int v)
{
    return v * v * 256 / 25;  // 5 = unity, 10 = 4x
}

static void capture_task(void *arg)
{
    esp_task_wdt_add(NULL);
    int16_t pcm[AUDIO_FRAME_SAMPLES];
    bool was_tx = false;
    for (;;) {
        audio_read_frame(pcm);  // blocks until the I2S DMA has 20 ms of audio
        DBG_HI(PIN_DBG_CAPTURE);
        esp_task_wdt_reset();
        int p = audio_peak(pcm, AUDIO_FRAME_SAMPLES);
        g_stats.mic_peak = p > g_stats.mic_peak ? p : (g_stats.mic_peak * 7) / 8;

        bool tx = (xEventGroupGetBits(g_ev) & EV_TX_ACTIVE) != 0;
        if (tx || was_tx) {  // one extra frame after release carries the EOT flag
            frame_t *f;
            if (xQueueReceive(s_free_q, &f, 0) == pdTRUE) {
                memcpy(f->pcm, pcm, sizeof(pcm));
                f->eot = !tx;
                xQueueSend(s_cap_q, &f, 0);  // cannot fail: queue is as long as the pool
            } else {
                g_stats.capture_overruns++;  // encode fell behind
            }
        }
        was_tx = tx;
        DBG_LO(PIN_DBG_CAPTURE);
    }
}

static void encode_task(void *arg)
{
    adpcm_state_t st;
    adpcm_init(&st);
    for (;;) {
        frame_t *f;
        xQueueReceive(s_cap_q, &f, portMAX_DELAY);
        DBG_HI(PIN_DBG_ENCODE);
        int64_t t0 = esp_timer_get_time();
        // Normal mode sends raw PCM (best quality). Long Range mode is limited to 250 kbps,
        // less than raw audio needs, so it sends 4:1 compressed ADPCM instead.
        bool raw = !g_settings.long_range;
        tx_pkt_t p;
        pkt_hdr_t h = {
            .version = PKT_VERSION,
            .type = raw ? PKT_TYPE_AUDIO_PCM : PKT_TYPE_AUDIO,
            .flags = (uint8_t)((f->eot ? PKT_FLAG_EOT : 0) | (g_settings.long_range ? PKT_FLAG_LR : 0)),
            .channel = g_settings.channel,
            .ttl = 0,
            .sender = s_my_id,
            .seq = seq_next(),
            .timestamp_us = (uint32_t)t0,
        };
        pkt_write_hdr(&h, p.data);
        size_t n;
        if (raw) {
            memcpy(p.data + PKT_HDR_BYTES, f->pcm, sizeof(f->pcm));  // ESP32 is little endian, same as the wire
            n = sizeof(f->pcm);
        } else {
            n = adpcm_encode_frame(&st, f->pcm, AUDIO_FRAME_SAMPLES, p.data + PKT_HDR_BYTES);
        }
        xQueueSend(s_free_q, &f, 0);  // give the PCM buffer back as soon as possible
        int len = crypto_seal(p.data, n);
        uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
        if (us > g_stats.encode_us_max) g_stats.encode_us_max = us;
        if (len > 0) {
            p.len = (uint16_t)len;
            g_stats.tx_frames++;
            if (xQueueSend(s_tx_q, &p, 0) != pdTRUE) g_stats.tx_queue_full++;
        }
        DBG_LO(PIN_DBG_ENCODE);
    }
}

static inline int16_t sat16(int32_t v)
{
    return v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v);
}

static void playback_task(void *arg)
{
    esp_task_wdt_add(NULL);
    int16_t out[AUDIO_FRAME_SAMPLES];
    int16_t last[AUDIO_FRAME_SAMPLES] = {0};
    int16_t tone[AUDIO_FRAME_SAMPLES];
    uint8_t frame[JB_MAX_PAYLOAD];
    uint32_t phase = 0;
    int conceal = 0, quiet = 0;
    bool amp_on = false;
    for (;;) {
        DBG_HI(PIN_DBG_PLAYBACK);
        esp_task_wdt_reset();
        uint16_t len = 0;
        taskENTER_CRITICAL(&s_jb_mux);
        jb_result_t r = jbuf_get(&s_jb, frame, &len);
        taskEXIT_CRITICAL(&s_jb_mux);

        if (r == JB_OK) {
            int64_t t0 = esp_timer_get_time();
            if (len == sizeof(out)) {
                memcpy(out, frame, sizeof(out));  // raw PCM frame
            } else if (adpcm_decode_frame(frame, len, out, AUDIO_FRAME_SAMPLES) != AUDIO_FRAME_SAMPLES) {
                memset(out, 0, sizeof(out));
            }
            uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
            if (us > g_stats.decode_us_max) g_stats.decode_us_max = us;
            memcpy(last, out, sizeof(out));
            conceal = 0;
            g_stats.played++;
        } else if (r == JB_MISSING) {
            // Deadline miss: the frame was not here when it had to play.
            g_stats.underruns++;
            if (conceal < 3) {  // repeat the last frame at half volume, then go silent
                for (int i = 0; i < AUDIO_FRAME_SAMPLES; i++) last[i] /= 2;
                memcpy(out, last, sizeof(out));
                conceal++;
                g_stats.concealed++;
            } else {
                memset(out, 0, sizeof(out));
            }
        } else {
            memset(out, 0, sizeof(out));
        }
        audio_scale(out, AUDIO_FRAME_SAMPLES, volume_gain_q8(g_settings.volume));

        bool tone_on = s_tone_frames > 0;
        if (tone_on) {
            audio_tone(tone, AUDIO_FRAME_SAMPLES, s_tone_hz, 2500, &phase);
            for (int i = 0; i < AUDIO_FRAME_SAMPLES; i++) out[i] = sat16(out[i] + tone[i]);
            s_tone_frames--;
        }

        if (r != JB_IDLE)
            xEventGroupSetBits(g_ev, EV_RX_ACTIVE);
        else
            xEventGroupClearBits(g_ev, EV_RX_ACTIVE);

        if (r != JB_IDLE || tone_on) {
            quiet = 0;
            if (!amp_on) {
                audio_amp_enable(true);
                amp_on = true;
            }
        } else if (amp_on && ++quiet >= 15) {  // 300 ms of silence: shut the amp down
            audio_amp_enable(false);
            amp_on = false;
        }
        DBG_LO(PIN_DBG_PLAYBACK);
        audio_write_frame(out);  // blocks until DMA has room: this paces the task at 20 ms
    }
}

// ---------------------------------------------------------------------------
// Radio tasks (core 0)

static void radio_tx_task(void *arg)
{
    tx_pkt_t p;
    for (;;) {
        xQueueReceive(s_tx_q, &p, portMAX_DELAY);
        esp_err_t err = link_send(p.data, p.len);
        if (err == ESP_ERR_ESPNOW_NO_MEM) {  // Wi-Fi driver queue full: one quick retry
            vTaskDelay(1);
            err = link_send(p.data, p.len);
        }
        if (err == ESP_OK)
            g_stats.tx_pkts++;
        else
            g_stats.tx_errors++;
    }
}

static void handle_audio(const pkt_hdr_t *h, const uint8_t *payload, size_t plen, uint32_t now)
{
    if (xEventGroupGetBits(g_ev) & EV_TX_ACTIVE) {
        // Both pressed PTT at once. The unit with the higher ID backs off.
        if (h->sender < s_my_id) {
            xEventGroupClearBits(g_ev, EV_TX_ACTIVE);
            xEventGroupSetBits(g_ev, EV_TX_PREEMPTED);
        } else {
            g_stats.rx_floor_ignored++;
            return;
        }
    }

    bool new_talker = false, someone_else_has_floor = false;
    taskENTER_CRITICAL(&s_floor_mux);
    if (s_talker && s_talker != h->sender && (now - s_talker_last_ms) < FLOOR_TIMEOUT_MS) {
        someone_else_has_floor = true;
    } else {
        if (s_talker != h->sender) {
            new_talker = true;
            s_talker = h->sender;
            s_talker_last_seq = h->seq - 1;
        }
        int32_t gap = (int32_t)(h->seq - s_talker_last_seq);
        if (gap > 1 && gap < 50) g_stats.rx_lost += (uint32_t)(gap - 1);
        if (gap > 0) s_talker_last_seq = h->seq;
        // On end of transmission, free the channel 100 ms from now instead of 300.
        s_talker_last_ms = (h->flags & PKT_FLAG_EOT) ? now - (FLOOR_TIMEOUT_MS - 100) : now;
    }
    taskEXIT_CRITICAL(&s_floor_mux);
    if (someone_else_has_floor) {
        g_stats.rx_floor_ignored++;
        return;
    }
    xEventGroupSetBits(g_ev, EV_CH_BUSY);

    taskENTER_CRITICAL(&s_jb_mux);
    if (new_talker) jbuf_reset(&s_jb);
    int r = jbuf_put(&s_jb, h->seq, payload, (uint16_t)plen, (h->flags & PKT_FLAG_EOT) != 0);
    taskEXIT_CRITICAL(&s_jb_mux);
    if (r == -2) g_stats.late++;
}

static void radio_rx_task(void *arg)
{
    esp_task_wdt_add(NULL);
    static uint8_t msg[LINK_RX_MSG_MAX];
    for (;;) {
        size_t n = xMessageBufferReceive(s_rx_mb, msg, sizeof(msg), pdMS_TO_TICKS(1000));
        esp_task_wdt_reset();
        if (n < 1) continue;
        int8_t rssi = (int8_t)msg[0];
        uint8_t *pkt = msg + 1;
        size_t len = n - 1;

        pkt_hdr_t h;
        if (pkt_read_hdr(pkt, len, &h) != 0 || h.type == PKT_TYPE_TEST) {
            g_stats.rx_malformed++;
            continue;
        }
        if (h.sender == s_my_id) continue;  // a relay echoing our own packet
        g_stats.rx_pkts++;
        if (h.channel != g_settings.channel) {
            g_stats.rx_other_channel++;
            continue;
        }
        if (replay_check(&s_replay, h.sender, h.seq) != 0) {
            g_stats.rx_replay++;
            continue;
        }
        size_t plen = 0;
        if (crypto_open(pkt, len, &plen) != 0) {
            g_stats.rx_auth_fail++;  // wrong passphrase, corruption or tampering
            continue;
        }
        uint32_t now = now_ms();
        replay_accept(&s_replay, h.sender, h.seq, now);  // only after it authenticated
        g_stats.last_rssi = rssi;
        bool lr = (h.flags & PKT_FLAG_LR) != 0;

        if (h.type == PKT_TYPE_BEACON) {
            beacon_t b;
            if (beacon_read(pkt + PKT_HDR_BYTES, plen, &b) == 0) roster_heard(h.sender, rssi, lr, &b, now);
            g_stats.rx_beacons++;
        } else if (h.type == PKT_TYPE_AUDIO || h.type == PKT_TYPE_AUDIO_PCM) {
            roster_heard(h.sender, rssi, lr, NULL, now);
            g_stats.rx_audio++;
            handle_audio(&h, pkt + PKT_HDR_BYTES, plen, now);
        }
    }
}

static void send_beacon(void)
{
    EventBits_t bits = xEventGroupGetBits(g_ev);
    if (bits & EV_TX_ACTIVE) return;  // audio packets already tell everyone we are here
    tx_pkt_t p;
    pkt_hdr_t h = {
        .version = PKT_VERSION,
        .type = PKT_TYPE_BEACON,
        .flags = g_settings.long_range ? PKT_FLAG_LR : 0,
        .channel = g_settings.channel,
        .sender = s_my_id,
        .seq = seq_next(),
        .timestamp_us = (uint32_t)esp_timer_get_time(),
    };
    pkt_write_hdr(&h, p.data);
    beacon_t b = {.battery_pct = (uint8_t)battery_percent(g_stats.battery_mv),
                  .flags = (uint8_t)((bits & EV_RX_ACTIVE ? BEACON_F_TALKING : 0) |
                                     (g_settings.long_range ? BEACON_F_LR : 0))};
    strlcpy(b.name, g_settings.name, sizeof(b.name));
    beacon_write(&b, p.data + PKT_HDR_BYTES);
    int len = crypto_seal(p.data, BEACON_BYTES);
    if (len > 0) {
        p.len = (uint16_t)len;
        if (xQueueSend(s_tx_q, &p, 0) == pdTRUE) g_stats.beacons_sent++;
    }
}

static void beacon_timer_cb(TimerHandle_t t)
{
    xTaskNotifyGive(s_link_task);
    // 1.8 to 2.2 s so units that booted together do not keep colliding.
    xTimerChangePeriod(t, pdMS_TO_TICKS(1800 + esp_random() % 400), 0);
}

static void link_mgr_task(void *arg)
{
    for (;;) {
        uint32_t beacon_due = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        uint32_t now = now_ms();
        if (beacon_due) send_beacon();

        taskENTER_CRITICAL(&s_floor_mux);
        if (s_talker && (now - s_talker_last_ms) >= FLOOR_TIMEOUT_MS) s_talker = 0;
        bool idle = s_talker == 0;
        taskEXIT_CRITICAL(&s_floor_mux);
        if (idle) xEventGroupClearBits(g_ev, EV_CH_BUSY);

        xSemaphoreTake(s_roster_mtx, portMAX_DELAY);
        for (int i = 0; i < WT_MAX_PEERS; i++) {
            if (s_peers[i].id && now - s_peers[i].last_ms > PEER_TIMEOUT_MS) s_peers[i].id = 0;
        }
        xSemaphoreGive(s_roster_mtx);
    }
}

// ---------------------------------------------------------------------------
// Monitor task: the numbers you put in the report

static void monitor_task(void *arg)
{
    static char runtime[1536];
    uint32_t tick = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        tick++;
        if (tick % 5 == 1) {
            int mv = battery_mv();
            g_stats.battery_mv = mv;
            if (mv > 2500 && mv < 3500)
                xEventGroupSetBits(g_ev, EV_LOW_BATT);
            else
                xEventGroupClearBits(g_ev, EV_LOW_BATT);
        }
        if (s_seq_commit_needed) seq_commit();

        EventBits_t b = xEventGroupGetBits(g_ev);
        printf("[%s%s%s] ch%u %s | tx %lu pkts %lu err %lu qfull %lu ovr %lu | rx %lu audio %lu lost %lu "
               "late %lu | under %lu conc %lu | auth %lu replay %lu | enc %luus dec %luus | rssi %d | bat %dmV\n",
               b & EV_TX_ACTIVE ? "TX" : "", b & EV_RX_ACTIVE ? "RX" : "", b & EV_CH_BUSY ? " busy" : "",
               g_settings.channel, g_settings.long_range ? "LR" : "NRM", (unsigned long)g_stats.tx_frames,
               (unsigned long)g_stats.tx_pkts, (unsigned long)g_stats.tx_errors,
               (unsigned long)g_stats.tx_queue_full, (unsigned long)g_stats.capture_overruns,
               (unsigned long)g_stats.rx_pkts, (unsigned long)g_stats.rx_audio, (unsigned long)g_stats.rx_lost,
               (unsigned long)g_stats.late, (unsigned long)g_stats.underruns, (unsigned long)g_stats.concealed,
               (unsigned long)g_stats.rx_auth_fail, (unsigned long)g_stats.rx_replay,
               (unsigned long)g_stats.encode_us_max, (unsigned long)g_stats.decode_us_max, g_stats.last_rssi,
               g_stats.battery_mv);

        if (tick % 10 == 0) {
            vTaskGetRunTimeStats(runtime);
            printf("--- CPU time per task ---\n%s", runtime);
            printf("--- free stack (bytes) ---\n");
            for (int i = 0; i < s_task_count; i++) {
                printf("%-14s %u\n", s_task_names[i], (unsigned)uxTaskGetStackHighWaterMark(s_tasks[i]));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// API used by the UI task

uint32_t wt_my_id(void)
{
    return s_my_id;
}

uint32_t wt_talker_id(void)
{
    taskENTER_CRITICAL(&s_floor_mux);
    uint32_t t = s_talker;
    taskEXIT_CRITICAL(&s_floor_mux);
    return t;
}

void wt_set_channel(uint8_t ch)
{
    if (ch < 1 || ch > 8) return;
    xSemaphoreTake(s_settings_mtx, portMAX_DELAY);
    g_settings.channel = ch;
    xSemaphoreGive(s_settings_mtx);
    crypto_set_channel(ch);
    taskENTER_CRITICAL(&s_jb_mux);
    jbuf_reset(&s_jb);
    taskEXIT_CRITICAL(&s_jb_mux);
    taskENTER_CRITICAL(&s_floor_mux);
    s_talker = 0;
    taskEXIT_CRITICAL(&s_floor_mux);
    xSemaphoreTake(s_roster_mtx, portMAX_DELAY);
    memset(s_peers, 0, sizeof(s_peers));  // the roster is per channel
    xSemaphoreGive(s_roster_mtx);
    ESP_LOGI(TAG, "channel %u", ch);
}

void wt_set_long_range(bool lr)
{
    xSemaphoreTake(s_settings_mtx, portMAX_DELAY);
    g_settings.long_range = lr;
    xSemaphoreGive(s_settings_mtx);
    link_set_long_range(lr);
    if (lr)
        xEventGroupSetBits(g_ev, EV_MODE_LR);
    else
        xEventGroupClearBits(g_ev, EV_MODE_LR);
}

void wt_set_volume(uint8_t v)
{
    if (v > WT_VOLUME_MAX) v = WT_VOLUME_MAX;
    xSemaphoreTake(s_settings_mtx, portMAX_DELAY);
    g_settings.volume = v;
    xSemaphoreGive(s_settings_mtx);
}

void wt_tone(int freq_hz, int duration_ms)
{
    s_tone_hz = freq_hz;
    s_tone_frames = duration_ms / 20;
}

void wt_busy_tone(void)
{
    wt_tone(400, 400);
}

bool wt_start_tx(void)
{
    if (xEventGroupGetBits(g_ev) & EV_CH_BUSY) {
        wt_busy_tone();
        return false;
    }
    xEventGroupClearBits(g_ev, EV_TX_PREEMPTED);
    xEventGroupSetBits(g_ev, EV_TX_ACTIVE);
    led_set(true);
    wt_tone(1200, 40);  // short chirp: you are live
    return true;
}

void wt_stop_tx(void)
{
    xEventGroupClearBits(g_ev, EV_TX_ACTIVE);
    led_set(false);
}

// ---------------------------------------------------------------------------

static void start_task(TaskFunction_t fn, const char *name, uint32_t stack, UBaseType_t prio, int core)
{
    TaskHandle_t h = NULL;
    xTaskCreatePinnedToCore(fn, name, stack, NULL, prio, &h, core);
    configASSERT(h);
    if (s_task_count < 8) {
        s_tasks[s_task_count] = h;
        s_task_names[s_task_count++] = name;
    }
    if (fn == link_mgr_task) s_link_task = h;
}

void app_walkie(void)
{
    g_ev = xEventGroupCreate();
    s_settings_mtx = xSemaphoreCreateMutex();
    s_roster_mtx = xSemaphoreCreateMutex();

    ESP_ERROR_CHECK(battery_init());
    g_stats.battery_mv = battery_mv();
    ESP_ERROR_CHECK(audio_init());

    s_rx_mb = xMessageBufferCreate(12 * LINK_RX_MSG_MAX);
    ESP_ERROR_CHECK(link_init(CONFIG_WT_WIFI_CHANNEL, false, s_rx_mb));
    s_my_id = link_my_id();
    settings_load();
    wt_set_long_range(g_settings.long_range);
    seq_load();
    ESP_ERROR_CHECK(crypto_init(CONFIG_WT_PASSPHRASE, g_settings.channel));
    jbuf_init(&s_jb, CONFIG_WT_JITTER_PREFILL, JB_MISS_LIMIT);
    replay_init(&s_replay);

    s_free_q = xQueueCreate(POOL_FRAMES, sizeof(frame_t *));
    s_cap_q = xQueueCreate(POOL_FRAMES, sizeof(frame_t *));
    s_tx_q = xQueueCreate(TX_QUEUE_LEN, sizeof(tx_pkt_t));
    for (int i = 0; i < POOL_FRAMES; i++) {
        frame_t *f = &s_pool[i];
        xQueueSend(s_free_q, &f, 0);
    }

    start_task(playback_task, "audio_playback", 6144, PRIO_PLAYBACK, CORE_AUDIO);
    start_task(capture_task, "audio_capture", 4096, PRIO_CAPTURE, CORE_AUDIO);
    start_task(encode_task, "audio_encode", 6144, PRIO_ENCODE, CORE_AUDIO);
    start_task(radio_rx_task, "radio_rx", 6144, PRIO_RADIO_RX, CORE_NET);
    start_task(radio_tx_task, "radio_tx", 6144, PRIO_RADIO_TX, CORE_NET);
    start_task(link_mgr_task, "link_mgr", 6144, PRIO_LINK_MGR, CORE_NET);
    start_task(monitor_task, "monitor", 4096, PRIO_MONITOR, CORE_NET);

    TimerHandle_t beacon = xTimerCreate("beacon", pdMS_TO_TICKS(500), pdTRUE, NULL, beacon_timer_cb);
    xTimerStart(beacon, 0);

    wt_ui_start();
    ESP_LOGI(TAG, "ready: %s on channel %u (%s)", g_settings.name, g_settings.channel,
             g_settings.long_range ? "Long Range" : "normal");
}
