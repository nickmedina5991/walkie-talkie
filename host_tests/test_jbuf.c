#include <string.h>

#include "jbuf.h"
#include "test_main.h"

static uint8_t s_frame[JB_MAX_PAYLOAD];
static uint8_t s_out[JB_MAX_PAYLOAD];

static int put(jbuf_t *jb, uint32_t seq, bool eot)
{
    memset(s_frame, (int)(seq & 0xFF), sizeof(s_frame));
    return jbuf_put(jb, seq, s_frame, sizeof(s_frame), eot);
}

// Returns the frame's tag byte for OK, -1 for MISSING, -2 for IDLE.
static int get(jbuf_t *jb)
{
    uint16_t len = 0;
    jb_result_t r = jbuf_get(jb, s_out, &len);
    if (r == JB_OK) return s_out[0];
    return r == JB_MISSING ? -1 : -2;
}

static void test_waits_for_prefill_then_plays_in_order(void)
{
    jbuf_t jb;
    jbuf_init(&jb, 3, 10);
    CHECK_EQ(get(&jb), -2);
    put(&jb, 100, false);
    put(&jb, 101, false);
    CHECK_EQ(get(&jb), -2);  // only 2 of 3 buffered
    put(&jb, 102, false);
    CHECK_EQ(get(&jb), 100);
    CHECK_EQ(get(&jb), 101);
    CHECK_EQ(get(&jb), 102);
    CHECK_EQ(jb.stats.played, 3);
}

static void test_reorders(void)
{
    jbuf_t jb;
    jbuf_init(&jb, 3, 10);
    put(&jb, 12, false);
    put(&jb, 10, false);
    put(&jb, 11, false);
    CHECK_EQ(get(&jb), 10);
    CHECK_EQ(get(&jb), 11);
    CHECK_EQ(get(&jb), 12);
}

static void test_duplicate_and_late_are_dropped(void)
{
    jbuf_t jb;
    jbuf_init(&jb, 2, 10);
    put(&jb, 5, false);
    CHECK_EQ(put(&jb, 5, false), -3);
    put(&jb, 6, false);
    CHECK_EQ(get(&jb), 5);
    CHECK_EQ(put(&jb, 5, false), -2);  // already played
    CHECK_EQ(jb.stats.duplicate, 1);
    CHECK_EQ(jb.stats.late, 1);
}

static void test_loss_reports_missing_then_recovers(void)
{
    jbuf_t jb;
    jbuf_init(&jb, 2, 10);
    put(&jb, 1, false);
    put(&jb, 2, false);
    put(&jb, 4, false);  // 3 lost
    CHECK_EQ(get(&jb), 1);
    CHECK_EQ(get(&jb), 2);
    CHECK_EQ(get(&jb), -1);
    CHECK_EQ(get(&jb), 4);
    CHECK_EQ(jb.stats.missing, 1);
}

static void test_end_of_tx_stops_cleanly(void)
{
    jbuf_t jb;
    jbuf_init(&jb, 3, 10);
    put(&jb, 7, false);
    put(&jb, 8, true);  // short burst: EOT before prefill is reached
    CHECK_EQ(get(&jb), 7);
    CHECK_EQ(get(&jb), 8);
    CHECK_EQ(get(&jb), -2);  // stopped, no missing frames counted
    CHECK_EQ(jb.stats.missing, 0);
    CHECK(!jb.playing);
}

static void test_gives_up_after_miss_limit(void)
{
    jbuf_t jb;
    jbuf_init(&jb, 1, 3);
    put(&jb, 50, false);
    CHECK_EQ(get(&jb), 50);
    CHECK_EQ(get(&jb), -1);
    CHECK_EQ(get(&jb), -1);
    CHECK_EQ(get(&jb), -1);
    CHECK(!jb.playing);
    CHECK_EQ(get(&jb), -2);
}

static void test_resync_on_big_jump(void)
{
    jbuf_t jb;
    jbuf_init(&jb, 1, 10);
    put(&jb, 10, false);
    CHECK_EQ(get(&jb), 10);
    put(&jb, 1000, false);  // new burst far ahead
    CHECK_EQ(jb.stats.resyncs, 1);
    CHECK_EQ(get(&jb), 1000 & 0xFF);
}

static void test_sequence_wraparound(void)
{
    jbuf_t jb;
    jbuf_init(&jb, 3, 10);
    put(&jb, 0xFFFFFFFEu, false);
    put(&jb, 0xFFFFFFFFu, false);
    put(&jb, 0x00000000u, false);
    CHECK_EQ(get(&jb), 0xFE);
    CHECK_EQ(get(&jb), 0xFF);
    CHECK_EQ(get(&jb), 0x00);
}

static void test_rejects_bad_length(void)
{
    jbuf_t jb;
    jbuf_init(&jb, 3, 10);
    CHECK_EQ(jbuf_put(&jb, 1, s_frame, 0, false), -1);
    CHECK_EQ(jbuf_put(&jb, 1, s_frame, JB_MAX_PAYLOAD + 1, false), -1);
}

void jbuf_tests(void)
{
    RUN(test_waits_for_prefill_then_plays_in_order);
    RUN(test_reorders);
    RUN(test_duplicate_and_late_are_dropped);
    RUN(test_loss_reports_missing_then_recovers);
    RUN(test_end_of_tx_stops_cleanly);
    RUN(test_gives_up_after_miss_limit);
    RUN(test_resync_on_big_jump);
    RUN(test_sequence_wraparound);
    RUN(test_rejects_bad_length);
}
