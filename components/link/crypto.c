#include "crypto.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/ccm.h"
#include "mbedtls/sha256.h"
#include "packet.h"

static const char *TAG = "crypto";

static mbedtls_ccm_context s_ccm;
static SemaphoreHandle_t s_lock;
static char s_pass[64];
static uint8_t s_channel;
static uint8_t s_work[PKT_MAX_PAYLOAD];  // shared scratch buffer, only used while holding s_lock

static esp_err_t load_key(uint8_t channel)
{
    char material[80];
    int n = snprintf(material, sizeof(material), "%s:%u", s_pass, channel);
    uint8_t digest[32];
    if (mbedtls_sha256((const unsigned char *)material, (size_t)n, digest, 0) != 0) return ESP_FAIL;
    int err = mbedtls_ccm_setkey(&s_ccm, MBEDTLS_CIPHER_ID_AES, digest, 128);
    memset(digest, 0, sizeof(digest));
    memset(material, 0, sizeof(material));
    if (err != 0) return ESP_FAIL;
    s_channel = channel;
    return ESP_OK;
}

esp_err_t crypto_init(const char *passphrase, uint8_t channel)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    strlcpy(s_pass, passphrase, sizeof(s_pass));
    mbedtls_ccm_init(&s_ccm);
    esp_err_t err = load_key(channel);
    ESP_LOGI(TAG, "AES-128-CCM ready, channel %u", channel);
    return err;
}

esp_err_t crypto_set_channel(uint8_t channel)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = (channel == s_channel) ? ESP_OK : load_key(channel);
    xSemaphoreGive(s_lock);
    return err;
}

int crypto_seal(uint8_t *pkt, size_t payload_len)
{
    if (payload_len > PKT_MAX_PAYLOAD) return -1;
    pkt_hdr_t h;
    if (pkt_read_hdr(pkt, PKT_HDR_BYTES, &h) != 0) return -1;
    uint8_t aad[PKT_HDR_BYTES], nonce[PKT_NONCE_BYTES];
    pkt_make_aad(pkt, aad);
    pkt_make_nonce(&h, nonce);
    uint8_t *payload = pkt + PKT_HDR_BYTES;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    int err = mbedtls_ccm_encrypt_and_tag(&s_ccm, payload_len, nonce, sizeof(nonce), aad, sizeof(aad), payload,
                                          s_work, payload + payload_len, PKT_TAG_BYTES);
    if (err == 0) memcpy(payload, s_work, payload_len);
    xSemaphoreGive(s_lock);
    if (err != 0) return -1;
    return (int)(PKT_HDR_BYTES + payload_len + PKT_TAG_BYTES);
}

int crypto_open(uint8_t *pkt, size_t len, size_t *payload_len)
{
    if (len < PKT_HDR_BYTES + PKT_TAG_BYTES) return -1;
    pkt_hdr_t h;
    if (pkt_read_hdr(pkt, len, &h) != 0) return -1;
    size_t plen = len - PKT_HDR_BYTES - PKT_TAG_BYTES;
    if (plen > PKT_MAX_PAYLOAD) return -1;
    uint8_t aad[PKT_HDR_BYTES], nonce[PKT_NONCE_BYTES];
    pkt_make_aad(pkt, aad);
    pkt_make_nonce(&h, nonce);
    uint8_t *payload = pkt + PKT_HDR_BYTES;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    int err = mbedtls_ccm_auth_decrypt(&s_ccm, plen, nonce, sizeof(nonce), aad, sizeof(aad), payload, s_work,
                                       payload + plen, PKT_TAG_BYTES);
    if (err == 0) memcpy(payload, s_work, plen);
    xSemaphoreGive(s_lock);
    if (err != 0) return -1;
    *payload_len = plen;
    return 0;
}
