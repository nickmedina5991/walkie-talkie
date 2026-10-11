// ESP-NOW broadcast link. No router, no pairing: every unit on the same Wi-Fi
// channel hears every packet, and the packet header plus the channel key decide
// who keeps it.
//
// The receive callback runs inside the Wi-Fi task, so it only copies the packet
// (with its RSSI in front) into a FreeRTOS message buffer and returns. One writer
// (the callback) and one reader (radio_rx), as message buffers require.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"

// Each message in the buffer is: [int8 rssi][packet bytes...]
#define LINK_RX_MSG_MAX (1 + 1470)  // ESP-NOW v2 packets can be up to 1470 bytes

esp_err_t link_init(uint8_t wifi_channel, bool long_range, MessageBufferHandle_t rx_buffer);
esp_err_t link_send(const uint8_t *data, size_t len);
// Normal: 1 Mbps 802.11b rate. Long Range: Espressif LR mode at 250 kbps.
// Every unit listens for both, so only the sender's rate changes.
esp_err_t link_set_long_range(bool on);
bool link_long_range(void);
uint32_t link_my_id(void);   // low 4 bytes of the station MAC
uint32_t link_rx_dropped(void);  // packets lost because the message buffer was full
