// aio.h - Wi-Fi + Adafruit IO HTTPS helper (REST API).
// NOTE: client.setInsecure() skips certificate checking (fine for the course demo; listed as a known gap in STRIDE).
#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "secrets.h"

static inline bool wifiConnect(uint32_t timeoutMs = 20000) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(250);
  return WiFi.status() == WL_CONNECTED;
}

// POST one data point. lat/lon (optional) make the point show up on an Adafruit IO map block.
static inline bool aioPost(const char* feed, const String& value, bool hasLoc = false, float lat = 0, float lon = 0) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.setTimeout(8000);
  String url = String("https://io.adafruit.com/api/v2/") + AIO_USER + "/feeds/" + feed + "/data";
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-AIO-Key", AIO_KEY);
  String body = String("{\"value\":\"") + value + "\"";
  if (hasLoc) body += String(",\"lat\":") + String(lat, 6) + ",\"lon\":" + String(lon, 6);
  body += "}";
  int code = http.POST(body);
  http.end();
  if (code != 200) { Serial.printf("[aio] %s -> HTTP %d\n", feed, code); return false; }
  return true;
}
