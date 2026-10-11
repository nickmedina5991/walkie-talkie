#include <string.h>

#include "packet.h"
#include "test_main.h"

static void test_header_round_trip(void)
{
    pkt_hdr_t h = {.version = PKT_VERSION, .type = PKT_TYPE_AUDIO, .flags = PKT_FLAG_EOT | PKT_FLAG_LR,
                   .channel = 5, .ttl = 2, .sender = 0xA1B2C3D4, .seq = 0x01020304, .timestamp_us = 0xDEADBEEF};
    uint8_t buf[PKT_HDR_BYTES];
    pkt_write_hdr(&h, buf);
    pkt_hdr_t g;
    CHECK_EQ(pkt_read_hdr(buf, sizeof(buf), &g), 0);
    CHECK_EQ(g.type, PKT_TYPE_AUDIO);
    CHECK_EQ(g.flags, PKT_FLAG_EOT | PKT_FLAG_LR);
    CHECK_EQ(g.channel, 5);
    CHECK_EQ(g.ttl, 2);
    CHECK_EQ(g.sender, 0xA1B2C3D4);
    CHECK_EQ(g.seq, 0x01020304);
    CHECK_EQ(g.timestamp_us, 0xDEADBEEF);
    CHECK_EQ(buf[7], 0x04);  // little endian
}

static void test_rejects_short_or_wrong_version(void)
{
    uint8_t buf[PKT_HDR_BYTES] = {0};
    pkt_hdr_t g;
    CHECK_EQ(pkt_read_hdr(buf, PKT_HDR_BYTES - 1, &g), -1);
    buf[0] = (2 << 5);
    CHECK_EQ(pkt_read_hdr(buf, sizeof(buf), &g), -1);
}

static void test_audio_packet_fits_espnow(void)
{
    CHECK_EQ(PKT_HDR_BYTES + 164 + PKT_TAG_BYTES, 187);        // ADPCM (Long Range)
    CHECK(PKT_HDR_BYTES + 164 + PKT_TAG_BYTES <= PKT_MAX_BYTES_V1);
    CHECK_EQ(PKT_HDR_BYTES + 640 + PKT_TAG_BYTES, 663);        // raw PCM (normal)
    CHECK(PKT_HDR_BYTES + 640 + PKT_TAG_BYTES <= PKT_MAX_BYTES);
    CHECK(640 <= PKT_MAX_PAYLOAD);
}

static void test_aad_ignores_ttl(void)
{
    pkt_hdr_t h = {.version = PKT_VERSION, .channel = 1, .ttl = 3, .sender = 7, .seq = 9};
    uint8_t a[PKT_HDR_BYTES], b[PKT_HDR_BYTES], aad_a[PKT_HDR_BYTES], aad_b[PKT_HDR_BYTES];
    pkt_write_hdr(&h, a);
    h.ttl = 0;
    pkt_write_hdr(&h, b);
    pkt_make_aad(a, aad_a);
    pkt_make_aad(b, aad_b);
    CHECK(memcmp(aad_a, aad_b, PKT_HDR_BYTES) == 0);
}

static void test_nonce_changes_with_seq(void)
{
    pkt_hdr_t h = {.sender = 1, .seq = 1, .channel = 1};
    uint8_t n1[PKT_NONCE_BYTES], n2[PKT_NONCE_BYTES];
    pkt_make_nonce(&h, n1);
    h.seq = 2;
    pkt_make_nonce(&h, n2);
    CHECK(memcmp(n1, n2, PKT_NONCE_BYTES) != 0);
}

static void test_beacon_round_trip(void)
{
    beacon_t b = {.name = "ALPHA", .battery_pct = 87, .flags = BEACON_F_LR};
    uint8_t buf[BEACON_BYTES];
    beacon_write(&b, buf);
    beacon_t g;
    CHECK_EQ(beacon_read(buf, sizeof(buf), &g), 0);
    CHECK(strcmp(g.name, "ALPHA") == 0);
    CHECK_EQ(g.battery_pct, 87);
    CHECK_EQ(g.flags, BEACON_F_LR);
}

static void test_replay_window(void)
{
    replay_t r;
    replay_init(&r);
    CHECK_EQ(replay_check(&r, 42, 100), 0);
    replay_accept(&r, 42, 100, 0);
    CHECK_EQ(replay_check(&r, 42, 100), -1);  // exact replay
    CHECK_EQ(replay_check(&r, 42, 101), 0);
    replay_accept(&r, 42, 105, 0);            // 101-104 skipped
    CHECK_EQ(replay_check(&r, 42, 103), 0);   // late but inside window, first time
    replay_accept(&r, 42, 103, 0);
    CHECK_EQ(replay_check(&r, 42, 103), -1);  // second time rejected
    CHECK_EQ(replay_check(&r, 42, 105 - 32), -1);  // older than the window
    CHECK_EQ(replay_check(&r, 99, 5), 0);     // other senders are independent
    replay_accept(&r, 42, 1000, 0);           // big jump clears the bitmap
    CHECK_EQ(replay_check(&r, 42, 999), 0);
    CHECK_EQ(replay_check(&r, 42, 1000), -1);
}

static void test_replay_evicts_oldest_peer(void)
{
    replay_t r;
    replay_init(&r);
    for (uint32_t i = 0; i < REPLAY_MAX_PEERS; i++) replay_accept(&r, 1000 + i, 10, i * 100);
    replay_accept(&r, 1000, 11, 5000);  // refresh peer 1000
    replay_accept(&r, 2000, 1, 6000);   // forces eviction of peer 1001 (least recent)
    CHECK_EQ(replay_check(&r, 1000, 11), -1);  // still tracked
    CHECK_EQ(replay_check(&r, 1001, 10), 0);   // forgotten
}

void packet_tests(void)
{
    RUN(test_header_round_trip);
    RUN(test_rejects_short_or_wrong_version);
    RUN(test_audio_packet_fits_espnow);
    RUN(test_aad_ignores_ttl);
    RUN(test_nonce_changes_with_seq);
    RUN(test_beacon_round_trip);
    RUN(test_replay_window);
    RUN(test_replay_evicts_oldest_peer);
}
