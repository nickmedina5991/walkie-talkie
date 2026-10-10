// Adafruit ESP32 Feather V2 pin map for the walkie talkie.
// Checked against Adafruit's Feather V2 pinout page.
//
// Avoid: GPIO16/17 (PSRAM), GPIO12 (strapping, must be low at reset),
// GPIO0 (NeoPixel data), GPIO2 (NeoPixel + STEMMA QT power), and the
// input-only pins 34-39 for anything that needs a pull-up or an output.
#pragma once

#include "driver/gpio.h"
#include "hal/adc_types.h"

// INMP441 microphone, I2S0 (RX). L/R pin tied to GND = left slot.
#define PIN_MIC_SCK   GPIO_NUM_26  // Feather A0
#define PIN_MIC_WS    GPIO_NUM_25  // Feather A1
#define PIN_MIC_SD    GPIO_NUM_34  // Feather A2 (input only, fine for data in)

// MAX98357A amplifier, I2S1 (TX).
#define PIN_AMP_BCLK  GPIO_NUM_27  // Feather 27
#define PIN_AMP_LRC   GPIO_NUM_33  // Feather 33
#define PIN_AMP_DIN   GPIO_NUM_32  // Feather 32
#define PIN_AMP_SD    GPIO_NUM_8   // Feather TX. High = on (left channel), low = shutdown.
                                   // Not GPIO14/15: those can output PWM during boot.

// SSD1306 OLED, I2C (shared with the STEMMA QT connector).
#define PIN_I2C_SDA   GPIO_NUM_22  // Feather SDA
#define PIN_I2C_SCL   GPIO_NUM_20  // Feather SCL

// EC11 rotary encoder and push to talk. All switch to GND, internal pull-ups.
#define PIN_ENC_A     GPIO_NUM_5   // Feather SCK
#define PIN_ENC_B     GPIO_NUM_19  // Feather MO
#define PIN_ENC_SW    GPIO_NUM_21  // Feather MI
#define PIN_PTT       GPIO_NUM_4   // Feather A5

// Battery voltage through the Feather's on-board 200k/200k divider (GPIO35).
#define VBAT_ADC_UNIT    ADC_UNIT_1
#define VBAT_ADC_CHANNEL ADC_CHANNEL_7
#define VBAT_DIVIDER     2

// On-board parts.
#define PIN_I2C_POWER GPIO_NUM_2   // Powers NeoPixel and STEMMA QT, drive high
#define PIN_LED       GPIO_NUM_13  // Red LED, used as the "transmitting" light

// Timing markers for the logic analyzer (enable with menuconfig > Walkie Talkie).
#define PIN_DBG_CAPTURE  GPIO_NUM_7   // Feather RX: high while audio_capture works on a frame
#define PIN_DBG_PLAYBACK GPIO_NUM_14  // high while audio_playback works on a frame
#define PIN_DBG_ENCODE   GPIO_NUM_15  // high while audio_encode works on a frame
