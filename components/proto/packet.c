#include "packet.h"

#include <string.h>

static inline void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void pkt_write_hdr(const pkt_hdr_t *h, uint8_t *buf)
{
    buf[0] = (uint8_t)(((h->version & 0x07) << 5) | ((h->type & 0x03) << 3) | (h->flags & 0x07));
    buf[1] = h->channel;
    buf[2] = h->ttl;
    put32(buf + 3, h->sender);
    put32(buf + 7, h->seq);
    put32(buf + 11, h->timestamp_us);
}

int pkt_read_hdr(const uint8_t *buf, size_t len, pkt_hdr_t *h)
{
    if (len < PKT_HDR_BYTES) return -1;
    h->version = buf[0] >> 5;
    h->type = (buf[0] >> 3) & 0x03;
    h->flags = buf[0] & 0x07;
    if (h->version != PKT_VERSION) return -1;
    h->channel = buf[1];
    h->ttl = buf[2];
    h->sender = get32(buf + 3);
    h->seq = get32(buf + 7);
    h->timestamp_us = get32(buf + 11);
    return 0;
}

void pkt_make_aad(const uint8_t *hdr_bytes, uint8_t aad[PKT_HDR_BYTES])
{
    memcpy(aad, hdr_bytes, PKT_HDR_BYTES);
    aad[2] = 0;  // TTL is not authenticated
}

void pkt_make_nonce(const pkt_hdr_t *h, uint8_t nonce[PKT_NONCE_BYTES])
{
    put32(nonce, h->sender);
    put32(nonce + 4, h->seq);
    nonce[8] = h->channel;
    nonce[9] = nonce[10] = nonce[11] = 0;
}

void beacon_write(const beacon_t *b, uint8_t out[BEACON_BYTES])
{
    memset(out, 0, BEACON_BYTES);
    for (int i = 0; i < BEACON_NAME_LEN && b->name[i]; i++) out[i] = (uint8_t)b->name[i];
    out[6] = b->battery_pct;
    out[7] = b->flags;
}

int beacon_read(const uint8_t *in, size_t len, beacon_t *b)
{
    if (len < BEACON_BYTES) return -1;
    for (int i = 0; i < BEACON_NAME_LEN; i++) {
        char c = (char)in[i];
        b->name[i] = (c >= 32 && c < 127) ? c : 0;
    }
    b->name[BEACON_NAME_LEN] = 0;
    b->battery_pct = in[6] > 100 ? 100 : in[6];
    b->flags = in[7];
    return 0;
}

void replay_init(replay_t *r)
{
    memset(r, 0, sizeof(*r));
}

static const replay_peer_t *find_peer(const replay_t *r, uint32_t sender)
{
    for (int i = 0; i < REPLAY_MAX_PEERS; i++) {
        if (r->peer[i].used && r->peer[i].sender == sender) return &r->peer[i];
    }
    return NULL;
}

int replay_check(const replay_t *r, uint32_t sender, uint32_t seq)
{
    const replay_peer_t *p = find_peer(r, sender);
    if (!p) return 0;  // first packet from this sender
    int32_t d = (int32_t)(seq - p->top);
    if (d > 0) return 0;
    uint32_t back = (uint32_t)(-d);
    if (back >= REPLAY_WINDOW) return -1;
    return (p->bitmap & (1u << back)) ? -1 : 0;
}

void replay_accept(replay_t *r, uint32_t sender, uint32_t seq, uint32_t now_ms)
{
    replay_peer_t *p = (replay_peer_t *)find_peer(r, sender);
    if (!p) {
        // Take a free slot, or evict the peer heard from least recently.
        p = &r->peer[0];
        for (int i = 0; i < REPLAY_MAX_PEERS; i++) {
            if (!r->peer[i].used) {
                p = &r->peer[i];
                break;
            }
            if ((int32_t)(r->peer[i].last_seen_ms - p->last_seen_ms) < 0) p = &r->peer[i];
        }
        p->used = true;
        p->sender = sender;
        p->top = seq;
        p->bitmap = 1;
        p->last_seen_ms = now_ms;
        return;
    }
    int32_t d = (int32_t)(seq - p->top);
    if (d > 0) {
        p->bitmap = (d >= REPLAY_WINDOW) ? 0 : (p->bitmap << d);
        p->bitmap |= 1;
        p->top = seq;
    } else {
        uint32_t back = (uint32_t)(-d);
        if (back < REPLAY_WINDOW) p->bitmap |= (1u << back);
    }
    p->last_seen_ms = now_ms;
}
