// packet.h - packet layout, packing, replay guard. Pure C++ (no Arduino) so it can be tested on a PC.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stddef.h>

enum PktType : uint8_t { T_REPORT = 1, T_SOS = 2, T_ACK = 3, T_MSG = 4 };
enum PktFlags : uint8_t { F_GPS_FIX = 1, F_LOW_BATT = 2 };

#define PKT_VERSION 1
#define BODY_LEN    22     // bytes that get signed
#define SIG_LEN     64     // ECDSA P-256 r||s
#define PLAIN_LEN   (BODY_LEN + SIG_LEN)   // 86
#define GCM_NONCE   12
#define GCM_TAG     16
#define WIRE_MAX    (1 + GCM_NONCE + PLAIN_LEN + GCM_TAG)   // 115

struct Body {
  uint8_t  ver;      // PKT_VERSION
  uint8_t  node;     // sender id
  uint8_t  type;     // PktType
  uint8_t  msg;      // preset message index (T_MSG) / consecutive missed acks (T_REPORT)
  uint16_t seq;      // per-sender counter (replay protection)
  uint32_t time;     // unix seconds from GPS (0 = unknown)
  int32_t  lat;      // degrees * 1e7
  int32_t  lon;
  uint8_t  sats;
  uint16_t vbat_mV;
  uint8_t  flags;    // PktFlags
};

static inline void put16(uint8_t* p, uint16_t v){ p[0]=v; p[1]=v>>8; }
static inline void put32(uint8_t* p, uint32_t v){ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static inline uint16_t get16(const uint8_t* p){ return p[0] | (p[1]<<8); }
static inline uint32_t get32(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

static inline void packBody(const Body& b, uint8_t* o) {
  o[0]=b.ver; o[1]=b.node; o[2]=b.type; o[3]=b.msg;
  put16(o+4, b.seq); put32(o+6, b.time);
  put32(o+10, (uint32_t)b.lat); put32(o+14, (uint32_t)b.lon);
  o[18]=b.sats; put16(o+19, b.vbat_mV); o[21]=b.flags;
}
static inline bool unpackBody(const uint8_t* i, Body& b) {
  b.ver=i[0]; b.node=i[1]; b.type=i[2]; b.msg=i[3];
  b.seq=get16(i+4); b.time=get32(i+6);
  b.lat=(int32_t)get32(i+10); b.lon=(int32_t)get32(i+14);
  b.sats=i[18]; b.vbat_mV=get16(i+19); b.flags=i[21];
  return b.ver == PKT_VERSION;
}

// days-from-civil (Howard Hinnant) -> unix seconds
static inline uint32_t unixFromCivil(int y, int m, int d, int hh, int mm, int ss) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = (long)era * 146097 + (long)doe - 719468;
  return (uint32_t)(days * 86400L + hh * 3600L + mm * 60L + ss);
}

// Replay guard: accept a packet only if its seq is higher than the last one seen from that node.
struct ReplayGuard {
  uint16_t last[8]; bool seen[8];
  ReplayGuard() { for (int i=0;i<8;i++){ last[i]=0; seen[i]=false; } }
  bool accept(uint8_t node, uint16_t seq) {
    if (node >= 8) return false;
    if (seen[node] && !(seq > last[node])) return false;
    seen[node]=true; last[node]=seq; return true;
  }
};

static const char* const PRESETS[] = { "OK - I'm safe", "On my way", "NEED HELP", "Send position", "Meet at rally", "SOS" };
#define PRESET_N 6
