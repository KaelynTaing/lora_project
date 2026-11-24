// Host test (runs on your PC, no ESP32): g++ -std=c++11 -I shared tests/test_logic.cpp -o t && ./t
#include <stdio.h>
#include <math.h>
#include "packet.h"
#include "nav.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main() {
  // pack / unpack round trip
  Body a = {1,1,T_REPORT,3, 513, 1795000000u, 374220000, -1220840000, 7, 5870, F_GPS_FIX|F_LOW_BATT};
  uint8_t buf[BODY_LEN]; packBody(a, buf);
  Body b = {}; CHECK(unpackBody(buf, b));
  CHECK(!memcmp(&a, &b, sizeof(Body)) || (a.seq==b.seq && a.time==b.time && a.lat==b.lat && a.lon==b.lon && a.vbat_mV==b.vbat_mV && a.flags==b.flags && a.msg==b.msg && a.sats==b.sats));
  CHECK(b.lon == -1220840000 && b.lat == 374220000);
  buf[0] = 9; CHECK(!unpackBody(buf, b));                         // wrong version rejected
  CHECK(sizeof(buf) == 22 && PLAIN_LEN == 86 && WIRE_MAX == 115);
  // unix time: 2026-11-23 12:00:00 UTC = 1795435200
  CHECK(unixFromCivil(2026,11,23,12,0,0) == 1795435200u);
  CHECK(unixFromCivil(1970,1,1,0,0,0) == 0u);
  CHECK(unixFromCivil(2000,3,1,0,0,0) == 951868800u);
  // replay guard
  ReplayGuard g; CHECK(g.accept(1,5)); CHECK(!g.accept(1,5)); CHECK(!g.accept(1,4)); CHECK(g.accept(1,6)); CHECK(g.accept(2,1)); CHECK(!g.accept(9,1));
  // navigation
  CHECK(fabsf(bearingDeg(0,0,0,1) - 90) < 0.1f);
  CHECK(fabsf(bearingDeg(0,0,1,0) - 0) < 0.1f || fabsf(bearingDeg(0,0,1,0) - 360) < 0.1f);
  CHECK(fabsf(bearingDeg(0,0,-1,0) - 180) < 0.1f);
  CHECK(fabsf(distanceM(0,0,0,1) - 111195) < 300);
  bool behind;
  CHECK(arrowServoDeg(90, 90, &behind) == 90 && !behind);          // target straight ahead
  CHECK(arrowServoDeg(135, 90, &behind) == 135 && !behind);        // 45 deg to the right
  CHECK(arrowServoDeg(45, 90, &behind) == 45 && !behind);          // 45 deg to the left
  { int d = arrowServoDeg(270, 90, &behind); CHECK((d == 0 || d == 180) && behind); }         // directly behind -> pinned to edge + flag
  CHECK(arrowServoDeg(10, 350, &behind) == 110 && !behind);        // wrap-around across north
  printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
  return fails != 0;
}
