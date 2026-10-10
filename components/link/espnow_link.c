#include "espnow_link.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"

static const char *TAG = "link";
static const uint8_t BROADCAST[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static MessageBufferHandle_t s_rx;
static uint32_t s_my_id;
static bool s_lr;
static volatile uint32_t s_rx_dropped;

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len <= 0 || len > 250 || !s_rx) return;
    uint8_t msg[LINK_RX_MSG_MAX];
    msg[0] = (uint8_t)(int8_t)info->rx_ctrl->rssi;
    memcpy(msg + 1, data, len);
    if (xMessageBufferSend(s_rx, msg, len + 1, 0) == 0) s_rx_dropped++;
}

esp_err_t link_set_long_range(bool on)
{
    esp_now_rate_config_t rate = {
        .phymode = on ? WIFI_PHY_MODE_LR : WIFI_PHY_MODE_11B,
        .rate = on ? WIFI_PHY_RATE_LORA_250K : WIFI_PHY_RATE_1M_L,
    };
    esp_err_t err = esp_now_set_peer_rate_config(BROADCAST, &rate);
    if (err == ESP_OK) {
        s_lr = on;
        ESP_LOGI(TAG, "TX rate: %s", on ? "Long Range 250 kbps" : "normal 1 Mbps");
    } else {
        ESP_LOGE(TAG, "rate change failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t link_init(uint8_t wifi_channel, bool long_range, MessageBufferHandle_t rx_buffer)
{
    s_rx = rx_buffer;

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    // Listen for normal and Long Range frames at the same time.
    ESP_RETURN_ON_ERROR(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
                                                               WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR),
                        TAG, "protocol");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(wifi_channel, WIFI_SECOND_CHAN_NONE), TAG, "channel");
    // Power save adds tens of ms of receive latency. Audio needs the radio always on.
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "power save");

    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp_now init");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(on_recv), TAG, "recv cb");
    esp_now_peer_info_t peer = {
        .channel = 0,  // current channel
        .ifidx = WIFI_IF_STA,
        .encrypt = false,  // we do our own AES-CCM (ESP-NOW encryption does not work for broadcast)
    };
    memcpy(peer.peer_addr, BROADCAST, ESP_NOW_ETH_ALEN);
    ESP_RETURN_ON_ERROR(esp_now_add_peer(&peer), TAG, "add peer");

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    s_my_id = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
    ESP_LOGI(TAG, "ESP-NOW up on Wi-Fi channel %u, my id %08" PRIX32 " (MAC " MACSTR ")", wifi_channel, s_my_id,
             MAC2STR(mac));
    return link_set_long_range(long_range);
}

esp_err_t link_send(const uint8_t *data, size_t len)
{
    return esp_now_send(BROADCAST, data, len);
}

bool link_long_range(void)
{
    return s_lr;
}

uint32_t link_my_id(void)
{
    return s_my_id;
}

uint32_t link_rx_dropped(void)
{
    return s_rx_dropped;
}
