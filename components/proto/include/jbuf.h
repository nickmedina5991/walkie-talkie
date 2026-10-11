// Jitter buffer for one talker: reorders frames by sequence number, waits for a
// small prefill before playing, drops late frames, and reports missing frames so
// the caller can conceal them.
//
// Portable C with no locking. The firmware wraps every call in a critical section
// because radio_rx (core 0) writes it and audio_playback (core 1) reads it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JB_SLOTS       8
#define JB_MAX_PAYLOAD 640  // a raw PCM frame: 320 samples x 2 bytes (ADPCM frames are 164)

typedef enum {
    JB_IDLE = 0,  // nothing playing (prefilling or no talker)
    JB_OK,        // frame copied out
    JB_MISSING,   // stream is playing but this frame never arrived: conceal it
} jb_result_t;

typedef struct {
    uint32_t seq;
    uint16_t len;
    uint8_t valid;
    uint8_t data[JB_MAX_PAYLOAD];
} jb_slot_t;

typedef struct {
    uint32_t late;         // arrived after its play time
    uint32_t duplicate;    // same sequence number twice
    uint32_t missing;      // play time reached with no frame
    uint32_t played;       // frames played
    uint32_t resyncs;      // big sequence jumps that forced a reset
    uint32_t streams;      // talk bursts started
} jb_stats_t;

typedef struct {
    jb_slot_t slot[JB_SLOTS];
    uint8_t prefill;     // frames buffered before playback starts
    uint8_t miss_limit;  // consecutive misses before giving up on a stream
    bool playing;
    uint32_t next_seq;   // next sequence number to play
    bool end_known;      // end of transmission frame seen
    uint32_t end_seq;
    uint8_t miss_run;
    jb_stats_t stats;
} jbuf_t;

void jbuf_init(jbuf_t *jb, uint8_t prefill, uint8_t miss_limit);

// Drop all buffered frames and stop playing. Stats are kept.
void jbuf_reset(jbuf_t *jb);

// Add a frame. end_of_tx marks the last frame of a talk burst.
// Returns 0 if stored, -1 bad length, -2 late, -3 duplicate.
int jbuf_put(jbuf_t *jb, uint32_t seq, const uint8_t *data, uint16_t len, bool end_of_tx);

// Called once per 20 ms frame by the playback task.
jb_result_t jbuf_get(jbuf_t *jb, uint8_t *out, uint16_t *len);

int jbuf_count(const jbuf_t *jb);

#ifdef __cplusplus
}
#endif
