// Over-the-air packet format and replay protection. Portable C.
//
// Layout (all multi-byte fields little endian):
//   byte 0      version (bits 7..5) | type (bits 4..3) | flags (bits 2..0)
//   byte 1      channel (1..8)
//   byte 2      TTL (hop count for the relay stretch goal, NOT authenticated)
//   bytes 3-6   sender ID (low 4 bytes of the Wi-Fi MAC)
//   bytes 7-10  sequence number
//   bytes 11-14 send timestamp (microseconds, sender's clock)
//   bytes 15..  payload, AES-CCM encrypted
//   last 8      AES-CCM authentication tag
// Audio comes in two forms, chosen by radio mode:
//   normal mode:     raw 16 bit PCM, 15 + 640 + 8 = 663 bytes (needs ESP-NOW v2, ESP-IDF 5.4+)
//   Long Range mode: IMA ADPCM,      15 + 164 + 8 = 187 bytes (raw audio needs 256 kbps,
//                    more than LR's 250 kbps, so LR must compress)
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PKT_VERSION      1
#define PKT_HDR_BYTES    15
#define PKT_TAG_BYTES    8
#define PKT_MAX_BYTES    1470  // ESP-NOW v2 limit
#define PKT_MAX_BYTES_V1 250   // ESP-NOW v1 limit, still what LR mode packets stay under
#define PKT_MAX_PAYLOAD  (PKT_MAX_BYTES - PKT_HDR_BYTES - PKT_TAG_BYTES)
#define PKT_NONCE_BYTES  12

typedef enum {
    PKT_TYPE_AUDIO = 0,      // IMA ADPCM audio (Long Range mode)
    PKT_TYPE_BEACON = 1,
    PKT_TYPE_TEST = 2,       // radio range test, not encrypted
    PKT_TYPE_AUDIO_PCM = 3,  // raw 16 bit PCM audio (normal mode)
} pkt_type_t;

#define PKT_FLAG_EOT     0x01  // last audio frame of a talk burst
#define PKT_FLAG_LR      0x02  // sender is in Long Range mode
#define PKT_FLAG_RELAYED 0x04  // forwarded by a relay unit

typedef struct {
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint8_t channel;
    uint8_t ttl;
    uint32_t sender;
    uint32_t seq;
    uint32_t timestamp_us;
} pkt_hdr_t;

void pkt_write_hdr(const pkt_hdr_t *h, uint8_t *buf);

// Returns 0 on success, -1 if too short or wrong version.
int pkt_read_hdr(const uint8_t *buf, size_t len, pkt_hdr_t *h);

// Additional authenticated data: the header with the TTL byte zeroed, so a relay
// can change TTL without breaking authentication.
void pkt_make_aad(const uint8_t *hdr_bytes, uint8_t aad[PKT_HDR_BYTES]);

// Nonce = sender (4) | seq (4) | channel (1) | 0 0 0. Unique per key as long as a
// sender never reuses a sequence number on a channel.
void pkt_make_nonce(const pkt_hdr_t *h, uint8_t nonce[PKT_NONCE_BYTES]);

// Beacon payload (8 bytes before encryption).
#define BEACON_NAME_LEN 6
#define BEACON_BYTES    8
#define BEACON_F_TALKING 0x01
#define BEACON_F_LR      0x02

typedef struct {
    char name[BEACON_NAME_LEN + 1];
    uint8_t battery_pct;
    uint8_t flags;
} beacon_t;

void beacon_write(const beacon_t *b, uint8_t out[BEACON_BYTES]);
int beacon_read(const uint8_t *in, size_t len, beacon_t *b);

// Replay protection: per sender, the highest sequence number seen plus a 32 bit
// bitmap of the 32 sequence numbers below it.
#define REPLAY_MAX_PEERS 8
#define REPLAY_WINDOW    32

typedef struct {
    bool used;
    uint32_t sender;
    uint32_t top;
    uint32_t bitmap;  // bit 0 = top, bit k = top - k
    uint32_t last_seen_ms;
} replay_peer_t;

typedef struct {
    replay_peer_t peer[REPLAY_MAX_PEERS];
} replay_t;

void replay_init(replay_t *r);

// Check without changing state (call before decrypting). 0 = fresh, -1 = replay/too old.
int replay_check(const replay_t *r, uint32_t sender, uint32_t seq);

// Record a sequence number (call only after the packet authenticated).
void replay_accept(replay_t *r, uint32_t sender, uint32_t seq, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
