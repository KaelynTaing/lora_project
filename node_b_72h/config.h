// config.h - pins + settings shared by every sketch.
// If your course helper (iot_common.h / IoTPins::) uses different pins, change them HERE.
#pragma once
#include <Arduino.h>

#define NODE_ID_A 1
#define NODE_ID_B 2

// ---------- test timing ----------
#define FAST_BENCH_MODE 0            // 1 = report every 60 s (for desk testing), 0 = every 3 h
#if FAST_BENCH_MODE
  #define REPORT_INTERVAL_S 60UL
#else
  #define REPORT_INTERVAL_S (3UL * 3600UL)   // DEFAULT: one report every 3 hours
#endif
#define ACK_WAIT_MS       3000       // how long Node A listens for the ack
#define MAX_TRIES         3          // send attempts per report
#define GPS_FIX_TIMEOUT_MS 60000UL   // give up on GPS after 60 s
#define LOW_BATT_MV       4600       // flag "low battery" below this (4xAA near empty)

// ---------- security switches ----------
#define USE_ENCRYPTION 1             // AES-GCM on top of the ATECC signature

// ---------- pins ----------
#define PIN_POT        34
#define PIN_BTN_MODE    4
#define PIN_BTN_SEND   32
#define PIN_SCK        18
#define PIN_MISO       19
#define PIN_MOSI       23
#define PIN_LORA_CS     5
#define PIN_LORA_RST   14
#define PIN_LORA_DIO0  26            // wired, but we poll instead of using interrupts
#define PIN_OLED_CS    27
#define PIN_OLED_DC    25
#define PIN_OLED_RES   33
#define PIN_SDA        21
#define PIN_SCL        22
#define PIN_SERVO      13
#define PIN_BUZZER     15
#define PIN_LED         2
#define PIN_GPS_RX     16            // ESP32 receives here  <- GPS TX
#define PIN_GPS_TX     17            // ESP32 sends here     -> GPS RX
#define PIN_GPS_PWR    12            // drives an NPN transistor that switches GPS GND (see notes). -1 = GPS always on
#define PIN_VBAT       35            // optional 200k/100k divider; reads vbat/3

// ---------- OLED ----------
#define OLED_W 128
#define OLED_H 64                    // change to 32 if your screen is 128x32

// ---------- LoRa radio ----------
#define LORA_FREQ   915E6
#define LORA_SF     9
#define LORA_BW     125E3
#define LORA_CR     5                // 4/5
#define LORA_SYNC   0x42             // private network (not the public 0x34)
#define LORA_TX_DBM 17               // use 10 on a desk test so nodes don't deafen each other
