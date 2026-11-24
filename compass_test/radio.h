// radio.h - thin wrapper around the Sandeep Mistry "LoRa" library.
#pragma once
#include <SPI.h>
#include <LoRa.h>
#include "config.h"

static inline bool radioBegin() {
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI);
  LoRa.setPins(PIN_LORA_CS, PIN_LORA_RST, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) return false;
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setSyncWord(LORA_SYNC);
  LoRa.enableCrc();
  LoRa.setTxPower(LORA_TX_DBM);
  return true;
}
static inline bool radioSend(const uint8_t* buf, size_t n) {
  LoRa.beginPacket();
  LoRa.write(buf, n);
  return LoRa.endPacket() == 1;          // blocks until sent
}
// Listen up to timeoutMs. Returns bytes received, 0 = nothing, -1 = packet too big.
static inline int radioRecv(uint8_t* buf, size_t maxLen, uint32_t timeoutMs, int* rssi) {
  LoRa.receive();                        // continuous receive mode
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    int p = LoRa.parsePacket();
    if (p > 0) {
      size_t n = 0;
      while (LoRa.available() && n < maxLen) buf[n++] = LoRa.read();
      while (LoRa.available()) LoRa.read();
      if (rssi) *rssi = LoRa.packetRssi();
      return p > (int)maxLen ? -1 : (int)n;
    }
    delay(2);
  }
  return 0;
}

// Re-arm continuous receive (call once after every radioSend when you poll with radioPoll).
static inline void radioListen() { LoRa.receive(); }
// Non-blocking check. Returns bytes received, 0 = nothing, -1 = too big. Call radioListen() first.
static inline int radioPoll(uint8_t* buf, size_t maxLen, int* rssi) {
  int p = LoRa.parsePacket();
  if (p <= 0) return 0;
  size_t n = 0;
  while (LoRa.available() && n < maxLen) buf[n++] = LoRa.read();
  while (LoRa.available()) LoRa.read();
  if (rssi) *rssi = LoRa.packetRssi();
  return p > (int)maxLen ? -1 : (int)n;
}
