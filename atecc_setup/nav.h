// nav.h - bearing / distance / servo-arrow math. Pure C++.
#pragma once
#include <math.h>

static inline float bearingDeg(float lat1, float lon1, float lat2, float lon2) {
  const float R = 0.017453292f;
  float p1 = lat1*R, p2 = lat2*R, dl = (lon2-lon1)*R;
  float y = sinf(dl)*cosf(p2);
  float x = cosf(p1)*sinf(p2) - sinf(p1)*cosf(p2)*cosf(dl);
  float b = atan2f(y, x) / R;
  return fmodf(b + 360.0f, 360.0f);              // 0 = north, clockwise
}
static inline float distanceM(float lat1, float lon1, float lat2, float lon2) {
  const float R = 0.017453292f, E = 6371000.0f;
  float dp = (lat2-lat1)*R, dl = (lon2-lon1)*R;
  float a = sinf(dp/2)*sinf(dp/2) + cosf(lat1*R)*cosf(lat2*R)*sinf(dl/2)*sinf(dl/2);
  return 2*E*atan2f(sqrtf(a), sqrtf(1-a));
}
// rel = how far the target is to the RIGHT of where you face (-180..180).
// Servo: 90 = straight ahead, 0 = hard left, 180 = hard right. "behind" = target is outside the servo's 180 degrees.
static inline int arrowServoDeg(float bearing, float heading, bool* behind) {
  float rel = fmodf(bearing - heading + 540.0f, 360.0f) - 180.0f;
  if (behind) *behind = fabsf(rel) > 90.0f;
  if (rel > 90) rel = 90;
  if (rel < -90) rel = -90;
  return 90 + (int)lroundf(rel);
}
