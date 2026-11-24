// node_a_72h.ino - NODE A: battery-powered field node for the 72-hour test.
// Every REPORT_INTERVAL_S (default 3 h): wake -> read battery -> GPS fix -> sign+encrypt -> LoRa send -> wait for
// Node B's signed ack -> deep sleep.  Pressing the SEND button also wakes it and sends an SOS right away.
// Board: "ESP32 Dev Module". Libraries: LoRa (Sandeep Mistry), SparkFun ATECCX08a, TinyGPSPlus, Adafruit SSD1306 + GFX.
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/rtc_io.h>
#include "config.h"
#include "packet.h"
#include "secure.h"
#include "radio.h"
#include "gps.h"

#define NODE_SHOWS_OLED 1                  // short status on the OLED after each report (screen is turned OFF before sleep)
#define MY_ID NODE_ID_A
#define PEER_ID NODE_ID_B

Adafruit_SSD1306 display(OLED_W, OLED_H, &SPI, PIN_OLED_DC, PIN_OLED_RES, PIN_OLED_CS);
Preferences prefs;
RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR uint32_t sentCount = 0, ackedCount = 0;
RTC_DATA_ATTR uint8_t  missedInRow = 0;
RTC_DATA_ATTR int32_t  lastLat = 0, lastLon = 0;     // last good GPS fix, reused if there is no fix this time

static uint16_t readBatteryMv() {                    // 200k/100k divider -> pin sees vbat/3. Reads ~0 if no divider fitted.
  uint32_t s = 0; for (int i = 0; i < 16; i++) { s += analogReadMilliVolts(PIN_VBAT); delay(2); }
  return (uint16_t)((s / 16) * 3);
}

static void show(const char* l1, const char* l2 = "", const char* l3 = "") {
#if NODE_SHOWS_OLED
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  display.setCursor(0, 0);  display.println("NODE A");
  display.setCursor(0, 16); display.println(l1);
  display.setCursor(0, 28); display.println(l2);
  display.setCursor(0, 40); display.println(l3);
  display.display();
#endif
}

static void goToSleep(uint64_t sleepUs) {
  Serial.printf("[A] sleeping %llu s (awake %lu ms)\n", sleepUs / 1000000ULL, (unsigned long)millis());
  Serial.flush();
#if NODE_SHOWS_OLED
  display.ssd1306_command(SSD1306_DISPLAYOFF);       // OLED off: otherwise it keeps drawing ~20 mA in sleep
#endif
  LoRa.sleep();
  gpsPower(false); gpsEnd();
  // Hold the chip-select / switch pins in a safe state during deep sleep so nothing glitches.
  digitalWrite(PIN_LORA_CS, HIGH); digitalWrite(PIN_OLED_CS, HIGH); digitalWrite(PIN_LORA_RST, HIGH);
  gpio_hold_en((gpio_num_t)PIN_LORA_CS); gpio_hold_en((gpio_num_t)PIN_OLED_CS); gpio_hold_en((gpio_num_t)PIN_LORA_RST);
  if (PIN_GPS_PWR >= 0) gpio_hold_en((gpio_num_t)PIN_GPS_PWR);
  gpio_deep_sleep_hold_en();
  rtc_gpio_pullup_en((gpio_num_t)PIN_BTN_SEND); rtc_gpio_pulldown_dis((gpio_num_t)PIN_BTN_SEND);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_SEND, 0);   // SEND button (to GND) wakes -> SOS
  esp_sleep_enable_timer_wakeup(sleepUs);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)PIN_LORA_CS); gpio_hold_dis((gpio_num_t)PIN_OLED_CS); gpio_hold_dis((gpio_num_t)PIN_LORA_RST);
  if (PIN_GPS_PWR >= 0) gpio_hold_dis((gpio_num_t)PIN_GPS_PWR);
  pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, LOW);
  pinMode(PIN_BTN_SEND, INPUT_PULLUP);
  bootCount++;
  bool sosWake = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0);
  Serial.printf("\n[A] boot #%lu (%s)\n", (unsigned long)bootCount, sosWake ? "button -> SOS" : "timer / power-on");

  uint16_t vbat = readBatteryMv();
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI);
#if NODE_SHOWS_OLED
  display.begin(SSD1306_SWITCHCAPVCC);
#endif
  show(sosWake ? "SOS!" : "Waking...", "Starting security");

  if (!secInit()) {                                   // ATECC missing or not locked
    show("ATECC ERROR", "run atecc_setup");
    Serial.println("[A] ATECC not ready - run atecc_setup. Sleeping and retrying.");
    goToSleep(REPORT_INTERVAL_S * 1000000ULL);
  }
  if (!radioBegin()) { show("LORA ERROR", "check wiring"); Serial.println("[A] LoRa init failed"); goToSleep(600ULL * 1000000ULL); }

  // ---- GPS ----
  gpsPower(true); gpsBegin();
  show("Getting GPS fix", "(<=60 s)");
  GpsFix fix; bool haveFix = gpsWaitFix(GPS_FIX_TIMEOUT_MS, fix);
  gpsPower(false);
  if (haveFix) { lastLat = fix.lat; lastLon = fix.lon; }
  Serial.printf("[A] GPS: %s sats=%u\n", haveFix ? "fix" : "NO FIX (using last known)", haveFix ? fix.sats : 0);

  // ---- build packet ----
  prefs.begin("nodeA", false);
  uint16_t seq = prefs.getUShort("seq", 0) + 1;       // survives power loss, so the replay check on B keeps working
  prefs.putUShort("seq", seq);
  Body b = {};
  b.ver = PKT_VERSION; b.node = MY_ID; b.type = sosWake ? T_SOS : T_REPORT; b.msg = missedInRow;
  b.seq = seq; b.time = haveFix ? fix.unixTime : 0;
  b.lat = haveFix ? fix.lat : lastLat; b.lon = haveFix ? fix.lon : lastLon;
  b.sats = haveFix ? fix.sats : 0; b.vbat_mV = vbat;
  b.flags = (haveFix ? F_GPS_FIX : 0) | (vbat > 0 && vbat < LOW_BATT_MV ? F_LOW_BATT : 0);
  uint8_t wire[WIRE_MAX];
  size_t n = buildWire(b, wire);
  if (!n) { show("SIGN ERROR"); Serial.println("[A] build/sign failed"); goToSleep(REPORT_INTERVAL_S * 1000000ULL); }

  // ---- send + wait for ack ----
  bool acked = false; int rssi = 0;
  for (int attempt = 1; attempt <= MAX_TRIES && !acked; attempt++) {
    digitalWrite(PIN_LED, HIGH);
    show("Sending...", ("seq " + String(seq) + " try " + String(attempt)).c_str());
    radioSend(wire, n); sentCount++;
    digitalWrite(PIN_LED, LOW);
    uint8_t rx[WIRE_MAX + 8];
    uint32_t t0 = millis();
    while (!acked && millis() - t0 < ACK_WAIT_MS) {
      int len = radioRecv(rx, sizeof(rx), ACK_WAIT_MS - (millis() - t0), &rssi);
      if (len <= 0) break;
      Body ab;
      SecResult r = parseWire(rx, len, MY_ID, ab);
      if (r == SEC_OK && ab.type == T_ACK && ab.node == PEER_ID && ab.seq == seq) acked = true;
      else Serial.printf("[A] ignored packet: %s\n", secResultName(r));
    }
    if (!acked && attempt < MAX_TRIES) delay(random(200, 1200));
  }
  if (acked) { ackedCount++; missedInRow = 0; } else if (missedInRow < 255) missedInRow++;
  Serial.printf("[A] seq=%u %s | bat=%u mV | sent=%lu acked=%lu\n", seq, acked ? "ACKED" : "NO ACK", vbat, (unsigned long)sentCount, (unsigned long)ackedCount);
  show(acked ? "Delivered (ack ok)" : "NO ACK from B", ("bat " + String(vbat / 1000.0, 2) + " V").c_str(), ("rssi " + String(rssi)).c_str());
  delay(2500);                                        // leave the result on screen briefly

  // ---- sleep until the next slot (interval minus time spent awake) ----
  uint64_t awakeUs = (uint64_t)millis() * 1000ULL, fullUs = (uint64_t)REPORT_INTERVAL_S * 1000000ULL;
  goToSleep(awakeUs < fullUs ? fullUs - awakeUs : 1000000ULL);
}

void loop() {}   // never reached: setup() ends in deep sleep
