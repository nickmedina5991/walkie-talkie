#include "jbuf.h"

#include <string.h>

// Sequence numbers are compared with signed differences so wraparound at 2^32 works.
static inline int32_t seq_diff(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b);
}

void jbuf_init(jbuf_t *jb, uint8_t prefill, uint8_t miss_limit)
{
    memset(jb, 0, sizeof(*jb));
    if (prefill < 1) prefill = 1;
    if (prefill > JB_SLOTS - 1) prefill = JB_SLOTS - 1;
    jb->prefill = prefill;
    jb->miss_limit = miss_limit ? miss_limit : 1;
}

void jbuf_reset(jbuf_t *jb)
{
    for (int i = 0; i < JB_SLOTS; i++) jb->slot[i].valid = 0;
    jb->playing = false;
    jb->end_known = false;
    jb->miss_run = 0;
}

int jbuf_count(const jbuf_t *jb)
{
    int n = 0;
    for (int i = 0; i < JB_SLOTS; i++) n += jb->slot[i].valid;
    return n;
}

static bool lowest_seq(const jbuf_t *jb, uint32_t *out)
{
    bool found = false;
    uint32_t lo = 0;
    for (int i = 0; i < JB_SLOTS; i++) {
        if (!jb->slot[i].valid) continue;
        if (!found || seq_diff(jb->slot[i].seq, lo) < 0) lo = jb->slot[i].seq;
        found = true;
    }
    *out = lo;
    return found;
}

int jbuf_put(jbuf_t *jb, uint32_t seq, const uint8_t *data, uint16_t len, bool end_of_tx)
{
    if (len == 0 || len > JB_MAX_PAYLOAD) return -1;

    if (jb->playing) {
        int32_t ahead = seq_diff(seq, jb->next_seq);
        if (ahead < 0) {
            jb->stats.late++;
            return -2;
        }
        if (ahead >= JB_SLOTS) {
            // Far ahead of the play point (long gap or a new burst we missed the end of).
            jb->stats.resyncs++;
            jbuf_reset(jb);
        }
    } else {
        // Prefilling: if this frame is too far from what is buffered, start over with it.
        uint32_t lo;
        if (lowest_seq(jb, &lo)) {
            int32_t d = seq_diff(seq, lo);
            if (d >= JB_SLOTS || d <= -JB_SLOTS) {
                jb->stats.resyncs++;
                jbuf_reset(jb);
            }
        }
    }

    jb_slot_t *s = &jb->slot[seq % JB_SLOTS];
    if (s->valid && s->seq == seq) {
        jb->stats.duplicate++;
        return -3;
    }
    s->seq = seq;
    s->len = len;
    memcpy(s->data, data, len);
    s->valid = 1;

    if (end_of_tx) {
        jb->end_known = true;
        jb->end_seq = seq;
    }
    return 0;
}

jb_result_t jbuf_get(jbuf_t *jb, uint8_t *out, uint16_t *len)
{
    if (!jb->playing) {
        int n = jbuf_count(jb);
        if (n == 0) return JB_IDLE;
        if (n < jb->prefill && !jb->end_known) return JB_IDLE;
        lowest_seq(jb, &jb->next_seq);
        jb->playing = true;
        jb->miss_run = 0;
        jb->stats.streams++;
    }

    jb_result_t r;
    jb_slot_t *s = &jb->slot[jb->next_seq % JB_SLOTS];
    if (s->valid && s->seq == jb->next_seq) {
        memcpy(out, s->data, s->len);
        *len = s->len;
        s->valid = 0;
        jb->miss_run = 0;
        jb->stats.played++;
        r = JB_OK;
    } else {
        *len = 0;
        jb->miss_run++;
        jb->stats.missing++;
        r = JB_MISSING;
    }

    bool finished = jb->end_known && jb->next_seq == jb->end_seq;
    jb->next_seq++;
    if (finished || jb->miss_run >= jb->miss_limit) {
        jbuf_reset(jb);
    }
    return r;
}
