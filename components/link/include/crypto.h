// AES-128-CCM packet encryption with one key per channel, derived from a shared
// passphrase: key = first 16 bytes of SHA-256(passphrase ":" channel).
// mbedTLS on the ESP32 uses the hardware AES block.
//
// A FreeRTOS mutex (with priority inheritance) guards the cipher contexts, because
// audio_encode and link_mgr both seal packets and the UI task changes channels.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t crypto_init(const char *passphrase, uint8_t channel);
esp_err_t crypto_set_channel(uint8_t channel);

// pkt holds a written header followed by payload_len plaintext bytes.
// Encrypts the payload in place and appends the tag. Returns total length, or -1.
int crypto_seal(uint8_t *pkt, size_t payload_len);

// Verifies and decrypts in place. Returns 0 and the plaintext length, or -1 if the
// tag does not match (wrong channel key, corruption or tampering).
int crypto_open(uint8_t *pkt, size_t len, size_t *payload_len);
