// cloud_test.ino - no LoRa, no ATECC. Just proves ESP32 -> Wi-Fi -> Adafruit IO works.
// Before running: make feeds named  lora-log  and  n1-batt  in Adafruit IO, and fill in shared/secrets.h.
#include "config.h"
#include "aio.h"

void setup() {
  Serial.begin(115200); delay(1000);
  Serial.print("Wi-Fi... ");
  if (!wifiConnect(20000)) { Serial.println("FAILED. Use a 2.4 GHz network or a phone hotspot."); return; }
  Serial.println("connected");
  Serial.println(aioPost("lora-log", "cloud_test hello from ESP32") ? "lora-log: OK" : "lora-log: FAILED (check AIO_USER / AIO_KEY / feed name)");
}

void loop() {
  static float fake = 5.9;
  static uint32_t t = 0;
  if (millis() - t > 20000) {                                    // one point every 20 s (free limit is 30/min)
    t = millis(); fake -= 0.01;
    Serial.println(aioPost("n1-batt", String(fake, 2)) ? "posted fake battery" : "post FAILED");
  }
}
