// compass.h - minimal driver for the cheap GY-271 style compass. Auto-detects:
//   QMC5883L (I2C 0x0D)  or  HMC5883L (I2C 0x1E).   UNTESTED on your board - run compass_test first.
// Heading = direction the sensor's +X arrow points, 0 = magnetic north, clockwise, degrees.
#pragma once
#include <Wire.h>
#include <math.h>
#include "config.h"

#define COMPASS_OFFSET_DEG 0.0f     // add this if the arrow on the compass board isn't aligned with the servo's "front"
#define DECLINATION_DEG    0.0f     // magnetic -> true north correction for your area (optional)

static uint8_t compassAddr = 0;     // 0x0D (QMC) or 0x1E (HMC); 0 = not found
static float cxMin = 1e9, cxMax = -1e9, cyMin = 1e9, cyMax = -1e9;   // running min/max for hard-iron calibration

static inline bool i2cWrite(uint8_t a, uint8_t reg, uint8_t v) { Wire.beginTransmission(a); Wire.write(reg); Wire.write(v); return Wire.endTransmission() == 0; }
static inline bool i2cPing(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }

static inline bool compassBegin() {
  if (i2cPing(0x0D)) { compassAddr = 0x0D; i2cWrite(0x0D, 0x0B, 0x01); i2cWrite(0x0D, 0x09, 0x1D); return true; }   // QMC: reset, 200 Hz continuous
  if (i2cPing(0x1E)) { compassAddr = 0x1E; i2cWrite(0x1E, 0x00, 0x70); i2cWrite(0x1E, 0x01, 0x20); i2cWrite(0x1E, 0x02, 0x00); return true; } // HMC: 15 Hz continuous
  return false;
}

static inline bool compassReadRaw(int16_t& x, int16_t& y, int16_t& z) {
  if (!compassAddr) return false;
  uint8_t start = (compassAddr == 0x0D) ? 0x00 : 0x03;
  Wire.beginTransmission(compassAddr); Wire.write(start);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)compassAddr, 6) != 6) return false;
  uint8_t d[6]; for (int i = 0; i < 6; i++) d[i] = Wire.read();
  if (compassAddr == 0x0D) { x = (int16_t)(d[0] | d[1] << 8); y = (int16_t)(d[2] | d[3] << 8); z = (int16_t)(d[4] | d[5] << 8); }  // X,Y,Z little-endian
  else                     { x = (int16_t)(d[0] << 8 | d[1]); z = (int16_t)(d[2] << 8 | d[3]); y = (int16_t)(d[4] << 8 | d[5]); }  // X,Z,Y big-endian
  return true;
}

// Call repeatedly while slowly rotating the board through a full circle (flat) to learn the offsets.
static inline void compassCalibrateSample(int16_t x, int16_t y) {
  if (x < cxMin) cxMin = x; if (x > cxMax) cxMax = x; if (y < cyMin) cyMin = y; if (y > cyMax) cyMax = y;
}
// Returns heading in degrees, or -1 on error. Uses calibration offsets if a full circle has been sampled.
static inline float compassHeading() {
  int16_t rx, ry, rz;
  if (!compassReadRaw(rx, ry, rz)) return -1;
  float x = rx, y = ry;
  if (cxMax > cxMin && cyMax > cyMin) { x -= (cxMax + cxMin) / 2; y -= (cyMax + cyMin) / 2; }
  float h = atan2f(y, x) * 57.2957795f + COMPASS_OFFSET_DEG + DECLINATION_DEG;
  while (h < 0) h += 360; while (h >= 360) h -= 360;
  return h;
}
