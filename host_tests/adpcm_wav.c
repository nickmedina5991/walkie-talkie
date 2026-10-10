// Listen to the codec on your laptop:
//   ./adpcm_wav input.wav output.wav [loss_percent]
// Input must be 16 kHz, 16 bit, mono PCM WAV. The output is what the other
// walkie talkie would play, including concealment of randomly dropped frames.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adpcm.h"

static int read_wav(const char *path, int16_t **samples, uint32_t *count)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        fclose(f);
        return -2;
    }
    uint16_t fmt = 0, ch = 0, bits = 0;
    uint32_t rate = 0;
    for (;;) {
        uint8_t ck[8];
        if (fread(ck, 1, 8, f) != 8) break;
        uint32_t sz = ck[4] | (ck[5] << 8) | (ck[6] << 16) | ((uint32_t)ck[7] << 24);
        if (!memcmp(ck, "fmt ", 4)) {
            uint8_t b[16];
            if (sz < 16 || fread(b, 1, 16, f) != 16) break;
            fmt = b[0] | (b[1] << 8);
            ch = b[2] | (b[3] << 8);
            rate = b[4] | (b[5] << 8) | (b[6] << 16) | ((uint32_t)b[7] << 24);
            bits = b[14] | (b[15] << 8);
            fseek(f, sz - 16 + (sz & 1), SEEK_CUR);
        } else if (!memcmp(ck, "data", 4)) {
            if (fmt != 1 || ch != 1 || bits != 16 || rate != 16000) {
                fprintf(stderr, "need 16 kHz 16-bit mono PCM (got fmt %u, %u ch, %u bit, %u Hz)\n", fmt, ch, bits,
                        rate);
                fclose(f);
                return -3;
            }
            *count = sz / 2;
            *samples = malloc(sz);
            size_t got = fread(*samples, 2, *count, f);
            *count = (uint32_t)got;
            fclose(f);
            return 0;
        } else {
            fseek(f, sz + (sz & 1), SEEK_CUR);
        }
    }
    fclose(f);
    return -4;
}

static void put32(FILE *f, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, f);
}

static void put16(FILE *f, uint16_t v)
{
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    fwrite(b, 1, 2, f);
}

static int write_wav(const char *path, const int16_t *s, uint32_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + n * 2);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 1);
    put32(f, 16000);
    put32(f, 32000);
    put16(f, 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, n * 2);
    fwrite(s, 2, n, f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s in.wav out.wav [loss_percent]\n", argv[0]);
        return 1;
    }
    int loss = argc > 3 ? atoi(argv[3]) : 0;
    int16_t *in = NULL;
    uint32_t n = 0;
    int err = read_wav(argv[1], &in, &n);
    if (err) {
        fprintf(stderr, "could not read %s (%d)\n", argv[1], err);
        return 1;
    }
    uint32_t frames = n / ADPCM_FRAME_SAMPLES;
    int16_t *out = calloc(frames * ADPCM_FRAME_SAMPLES, 2);
    adpcm_state_t st;
    adpcm_init(&st);
    int16_t last[ADPCM_FRAME_SAMPLES] = {0};
    int conceal = 0, lost = 0;
    srand(1234);
    for (uint32_t i = 0; i < frames; i++) {
        uint8_t pkt[ADPCM_FRAME_BYTES];
        adpcm_encode_frame(&st, in + i * ADPCM_FRAME_SAMPLES, ADPCM_FRAME_SAMPLES, pkt);
        int16_t *o = out + i * ADPCM_FRAME_SAMPLES;
        if (rand() % 100 < loss) {
            lost++;
            // Same concealment as the firmware: repeat last frame at half volume, 3 times max.
            if (conceal < 3) {
                for (int k = 0; k < ADPCM_FRAME_SAMPLES; k++) last[k] /= 2;
                memcpy(o, last, sizeof(last));
                conceal++;
            }
        } else {
            adpcm_decode_frame(pkt, sizeof(pkt), o, ADPCM_FRAME_SAMPLES);
            memcpy(last, o, sizeof(last));
            conceal = 0;
        }
    }
    write_wav(argv[2], out, frames * ADPCM_FRAME_SAMPLES);
    printf("%u frames, %d dropped (%d%%), wrote %s\n", frames, lost, loss, argv[2]);
    free(in);
    free(out);
    return 0;
}
