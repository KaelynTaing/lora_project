// gps.h - NEO-6M via TinyGPSPlus on UART2. GPS power can be switched with PIN_GPS_PWR (NPN transistor on GPS GND).
#pragma once
#include <TinyGPSPlus.h>
#include "config.h"
#include "packet.h"

static TinyGPSPlus gps;
static HardwareSerial gpsSerial(2);

struct GpsFix { bool valid; int32_t lat, lon; uint8_t sats; uint32_t unixTime; };

static inline void gpsPower(bool on) {
  if (PIN_GPS_PWR >= 0) { pinMode(PIN_GPS_PWR, OUTPUT); digitalWrite(PIN_GPS_PWR, on ? HIGH : LOW); }
}
static inline void gpsBegin() { gpsSerial.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX); }
static inline void gpsEnd()   { gpsSerial.end(); }
static inline void gpsFeed()  { while (gpsSerial.available()) gps.encode(gpsSerial.read()); }

static inline bool gpsHasFix() {
  return gps.location.isValid() && gps.location.age() < 3000 && gps.satellites.isValid() &&
         gps.satellites.value() >= 4 && gps.date.isValid() && gps.time.isValid();
}
static inline void gpsSnapshot(GpsFix& f) {
  f.valid = true;
  f.lat = (int32_t)lround(gps.location.lat() * 1e7);
  f.lon = (int32_t)lround(gps.location.lng() * 1e7);
  f.sats = gps.satellites.value();
  f.unixTime = unixFromCivil(gps.date.year(), gps.date.month(), gps.date.day(),
                             gps.time.hour(), gps.time.minute(), gps.time.second());
}
// Blocks until a fix or timeout. f.valid tells you which.
static inline bool gpsWaitFix(uint32_t timeoutMs, GpsFix& f) {
  uint32_t t0 = millis();
  f.valid = false;
  while (millis() - t0 < timeoutMs) {
    gpsFeed();
    if (gpsHasFix()) { gpsSnapshot(f); return true; }
    delay(20);
  }
  return false;
}
