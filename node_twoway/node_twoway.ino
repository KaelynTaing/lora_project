// node_twoway.ino - the SAME sketch runs on BOTH boards (set MY_NODE_ID below: 1 on one board, 2 on the other).
// Needs the secure setup finished (atecc_setup on both boards, keys pasted into shared/peer_keys.h).
//
//  * POT = menu dial.  MODE button toggles SEND <-> POINT.  SEND button sends / (hold in POINT mode = save rally point).
//  * SEND mode : pot highlights a preset message, SEND button transmits it (signed + encrypted, with your GPS position).
//  * POINT mode: pot picks the target (other node / rally point); the servo arrow points at it.
//  * Incoming message -> beep + shown on the OLED; the sender's coordinates move the servo arrow.
//  * Every message is acked (signed). Both boards can upload what they receive to Adafruit IO (WIFI_UPLOAD 1).
//  * Also sends a position beacon every REPORT_INTERVAL_S (default 3 h).
// Powered from USB / always listening, so this is the BENCH + DEMO version, not the 72 h battery version.
#define MY_NODE_ID   1        // <<< CHANGE TO 2 ON THE OTHER BOARD
#define WIFI_UPLOAD  1        // 1 = upload received messages to Adafruit IO
#define HAVE_COMPASS 0        // set 1 once the compass is wired (run compass_test first)

#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "packet.h"
#include "nav.h"
#include "secure.h"
#include "radio.h"
#include "gps.h"
#include "compass.h"
#if WIFI_UPLOAD
#include <time.h>
#include "aio.h"
#endif

#define PEER_ID (MY_NODE_ID == NODE_ID_A ? NODE_ID_B : NODE_ID_A)

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  #define SERVO_ATTACH()  ledcAttach(PIN_SERVO, 50, 16)
  #define SERVO_WRITE(d)  ledcWrite(PIN_SERVO, d)
#else
  #define SERVO_ATTACH()  do { ledcSetup(2, 50, 16); ledcAttachPin(PIN_SERVO, 2); } while (0)
  #define SERVO_WRITE(d)  ledcWrite(2, d)
#endif

Adafruit_SSD1306 display(OLED_W, OLED_H, &SPI, PIN_OLED_DC, PIN_OLED_RES, PIN_OLED_CS);
Preferences prefs;
ReplayGuard guard;

enum Mode { MODE_SEND, MODE_POINT };
Mode mode = MODE_SEND;
float potSm = 0; int idxSend = 0, idxTarget = 0;         // idxTarget: 0 = other node, 1 = rally point
uint16_t seq = 0;
GpsFix myFix = {};
bool peerValid = false; int32_t peerLat = 0, peerLon = 0; uint32_t peerMs = 0;
bool rallyValid = false; int32_t rallyLat = 0, rallyLon = 0;
String alertText = ""; uint32_t alertUntil = 0;
String statusLine = "";
uint16_t pendingSeq = 0; uint32_t pendingMs = 0; bool delivered = false;
uint32_t lastBeacon = 0, lastDraw = 0, lastServo = 0;
float heading = 0; bool compassOk = false;
int servoDegNow = 90; bool behindNow = false;

struct Btn { uint8_t pin; bool down; uint32_t t; bool longFired; };
Btn btnMode = { PIN_BTN_MODE, false, 0, false }, btnSend = { PIN_BTN_SEND, false, 0, false };
int btnEvent(Btn& b) {                                   // 0 none, 1 short press, 2 long press (fires once while held)
  bool d = !digitalRead(b.pin); uint32_t now = millis(); int ev = 0;
  if (d && !b.down && now - b.t > 30) { b.down = true; b.t = now; b.longFired = false; }
  else if (d && b.down && !b.longFired && now - b.t > 800) { b.longFired = true; ev = 2; }
  else if (!d && b.down && now - b.t > 30) { b.down = false; if (!b.longFired) ev = 1; b.t = now; }
  return ev;
}

void servoDeg(int d) { d = constrain(d, 0, 180); int us = map(d, 0, 180, 600, 2400); SERVO_WRITE((uint32_t)us * 65536UL / 20000UL); }
void beep(int ms, int hz = 2000) {
  uint32_t half = 500000UL / hz, t0 = millis();
  while (millis() - t0 < (uint32_t)ms) { digitalWrite(PIN_BUZZER, HIGH); delayMicroseconds(half); digitalWrite(PIN_BUZZER, LOW); delayMicroseconds(half); }
}
uint16_t readBatteryMv() { uint32_t s = 0; for (int i = 0; i < 8; i++) s += analogReadMilliVolts(PIN_VBAT); return (uint16_t)((s / 8) * 3); }

// pot -> menu index with hysteresis so it doesn't flicker between two items
int potIndex(int count, int current) {
  int bin = 4096 / count;
  int cand = constrain((int)potSm / bin, 0, count - 1);
  if (cand > current && potSm > cand * bin + 80) return cand;
  if (cand < current && potSm < (cand + 1) * bin - 80) return cand;
  return current;
}

// ---------- cloud queue ----------
#if WIFI_UPLOAD
struct Up { Body b; int rssi; uint32_t t; bool own; };
Up upq[8]; int upHead = 0, upCount = 0;
void upEnqueue(const Up& u) { if (upCount == 8) { upHead = (upHead + 1) % 8; upCount--; } upq[(upHead + upCount) % 8] = u; upCount++; }
bool upSend(const Up& u) {
  const Body& b = u.b;
  const char* typ = b.type == T_SOS ? "SOS" : b.type == T_MSG ? "MSG" : b.type == T_ACK ? "ACK" : "REPORT";
  String txt = (b.type == T_MSG && b.msg < PRESET_N) ? String(" \"") + PRESETS[b.msg] + "\"" : String("");
  String log = String("n") + b.node + " seq=" + b.seq + " " + typ + txt + (u.own ? String(" SENT") : String(" rssi=") + u.rssi + " verified=1") + " t=" + u.t;
  bool ok = aioPost("lora-log", log);
  if (!u.own) {
    ok &= aioPost((String("n") + b.node + "-batt").c_str(), String(b.vbat_mV / 1000.0, 2));
    ok &= aioPost((String("n") + b.node + "-rssi").c_str(), String(u.rssi));
    if (b.lat || b.lon) ok &= aioPost((String("n") + b.node + "-loc").c_str(), String(b.seq), true, b.lat / 1e7f, b.lon / 1e7f);
  }
  return ok;
}
void upService() {
  if (!upCount) return;
  if (WiFi.status() != WL_CONNECTED) { static uint32_t t = 0; if (millis() - t > 30000) { t = millis(); WiFi.reconnect(); } return; }
  if (upSend(upq[upHead])) { upHead = (upHead + 1) % 8; upCount--; }      // one item per call so the UI stays responsive
}
#endif

// ---------- sending ----------
void sendPacket(uint8_t type, uint8_t msg) {
  Body b = {};
  b.ver = PKT_VERSION; b.node = MY_NODE_ID; b.type = type; b.msg = msg;
  b.seq = ++seq; prefs.putUShort("seq", seq);
  if (myFix.valid) { b.lat = myFix.lat; b.lon = myFix.lon; b.sats = myFix.sats; b.time = myFix.unixTime; b.flags |= F_GPS_FIX; }
  b.vbat_mV = readBatteryMv();
  uint8_t wire[WIRE_MAX]; size_t n = buildWire(b, wire);
  if (!n) { statusLine = "sign failed"; return; }
  radioSend(wire, n); radioListen();
  pendingSeq = b.seq; pendingMs = millis(); delivered = false;
  statusLine = String("sent #") + b.seq + " ...";
  Serial.printf("[%d] TX type=%u msg=%u seq=%u\n", MY_NODE_ID, type, msg, b.seq);
#if WIFI_UPLOAD
  Up u = { b, 0, (uint32_t)time(nullptr), true }; upEnqueue(u);
#endif
}
void sendAck(const Body& rx) {
  Body a = {}; a.ver = PKT_VERSION; a.node = MY_NODE_ID; a.type = T_ACK; a.seq = rx.seq;
  uint8_t wire[WIRE_MAX]; size_t n = buildWire(a, wire);
  delay(150);
  if (n) radioSend(wire, n);
  radioListen();
}

// ---------- receiving ----------
void handleRx() {
  uint8_t rx[WIRE_MAX + 8]; int rssi = 0;
  int len = radioPoll(rx, sizeof(rx), &rssi);
  if (len == 0) return;
  Body b; SecResult r = (len < 0) ? SEC_BAD_LEN : parseWire(rx, len, MY_NODE_ID, b);
  if (r == SEC_OK && !guard.accept(b.node, b.seq)) { Serial.println("replay rejected"); r = SEC_BAD_SIG; }
  if (r != SEC_OK) { Serial.printf("rejected: %s\n", secResultName(r)); statusLine = String("rejected ") + secResultName(r); return; }

  if (b.type == T_ACK) { if (b.seq == pendingSeq) { delivered = true; statusLine = String("delivered #") + b.seq; } return; }

  sendAck(b);
  if (b.lat || b.lon) { peerLat = b.lat; peerLon = b.lon; peerValid = true; peerMs = millis(); }
  if (b.type == T_SOS)      { alertText = String("SOS from n") + b.node; beep(150); delay(80); beep(150); delay(80); beep(400, 1500); }
  else if (b.type == T_MSG) { alertText = String("n") + b.node + ": " + (b.msg < PRESET_N ? PRESETS[b.msg] : "?"); beep(120); }
  else                      { alertText = String("n") + b.node + " position update"; beep(40); }
  alertUntil = millis() + 8000;
  Serial.printf("[%d] RX from n%u type=%u msg=%u seq=%u lat=%.5f lon=%.5f rssi=%d\n", MY_NODE_ID, b.node, b.type, b.msg, b.seq, b.lat / 1e7, b.lon / 1e7, rssi);
#if WIFI_UPLOAD
  Up u = { b, rssi, (uint32_t)time(nullptr), false }; upEnqueue(u);
#endif
}

// ---------- display ----------
void draw() {
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("N%d %s GPS:%s %s", MY_NODE_ID, mode == MODE_SEND ? "SEND " : "POINT", myFix.valid ? "ok" : "--", compassOk ? "" : "nocmp");
  if (millis() < alertUntil) {                                   // incoming message takes over the screen
    display.setTextSize(2); display.setCursor(0, 16); display.println(alertText);
    display.setTextSize(1); display.setCursor(0, 56); display.print(peerValid ? "arrow -> sender" : "no position sent");
  } else if (mode == MODE_SEND) {
    for (int k = -1; k <= 1; k++) {
      int i = idxSend + k; if (i < 0 || i >= PRESET_N) continue;
      display.setCursor(0, 16 + (k + 1) * 11); display.print(k == 0 ? "> " : "  "); display.print(PRESETS[i]);
    }
    display.setCursor(0, 54); display.print(statusLine);
  } else {
    bool rally = idxTarget == 1; bool ok = rally ? rallyValid : peerValid;
    display.setCursor(0, 12); display.print(rally ? "> rally point" : "> other node"); display.print(rally ? "" : "   (pot: rally)");
    if (ok && myFix.valid) {
      int32_t tl = rally ? rallyLat : peerLat, tn = rally ? rallyLon : peerLon;
      float d = distanceM(myFix.lat / 1e7f, myFix.lon / 1e7f, tl / 1e7f, tn / 1e7f);
      float br = bearingDeg(myFix.lat / 1e7f, myFix.lon / 1e7f, tl / 1e7f, tn / 1e7f);
      display.setCursor(0, 26); display.printf("dist %.0f m", d);
      display.setCursor(0, 36); display.printf("brg %.0f  hdg %.0f", br, heading);
      display.setCursor(0, 46); display.print(behindNow ? "BEHIND YOU - turn around" : "follow the arrow");
    } else {
      display.setCursor(0, 30); display.print(ok ? "waiting for my GPS fix" : (rally ? "hold SEND to set rally" : "no position from peer yet"));
    }
    display.setCursor(0, 56); display.print("hold SEND = save rally");
  }
  display.display();
}

void updateServo() {
  bool rally = idxTarget == 1; bool ok = (rally ? rallyValid : peerValid) && myFix.valid;
  if (!ok) { servoDeg(90); return; }
  int32_t tl = rally ? rallyLat : peerLat, tn = rally ? rallyLon : peerLon;
  float br = bearingDeg(myFix.lat / 1e7f, myFix.lon / 1e7f, tl / 1e7f, tn / 1e7f);
  servoDegNow = arrowServoDeg(br, heading, &behindNow);
  servoDeg(servoDegNow);
}

void setup() {
  Serial.begin(115200); delay(800);
  pinMode(PIN_BTN_MODE, INPUT_PULLUP); pinMode(PIN_BTN_SEND, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT); pinMode(PIN_LED, OUTPUT);
  SERVO_ATTACH(); servoDeg(90);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI);
  display.begin(SSD1306_SWITCHCAPVCC);
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE); display.setCursor(0, 0); display.println("starting..."); display.display();
  if (!secInit()) { display.println("ATECC not ready"); display.display(); Serial.println("run atecc_setup + paste keys"); while (1) delay(1000); }
  if (!radioBegin()) { display.println("LoRa error"); display.display(); while (1) delay(1000); }
  radioListen();
  gpsPower(true); gpsBegin();
#if HAVE_COMPASS
  compassOk = compassBegin();
#endif
  prefs.begin("tw", false);
  seq = prefs.getUShort("seq", 0);
  rallyValid = prefs.getBool("rset", false); rallyLat = prefs.getInt("rlat", 0); rallyLon = prefs.getInt("rlon", 0);
#if WIFI_UPLOAD
  display.println("Wi-Fi..."); display.display();
  wifiConnect(15000); configTime(0, 0, "pool.ntp.org");
#endif
  lastBeacon = millis();
  Serial.printf("[%d] ready\n", MY_NODE_ID);
}

void loop() {
  gpsFeed();
  if (gpsHasFix()) gpsSnapshot(myFix);
  potSm += ((float)analogRead(PIN_POT) - potSm) * 0.2f;
#if HAVE_COMPASS
  if (compassOk) { float h = compassHeading(); if (h >= 0) heading = h; }
#endif
  handleRx();

  int em = btnEvent(btnMode), es = btnEvent(btnSend);
  if (em == 1) mode = (mode == MODE_SEND) ? MODE_POINT : MODE_SEND;
  if (mode == MODE_SEND) {
    idxSend = potIndex(PRESET_N, idxSend);
    if (es == 1) {
      if (idxSend == 5) sendPacket(T_SOS, 5);
      else if (idxSend == 3) sendPacket(T_REPORT, 0);
      else sendPacket(T_MSG, idxSend);
    }
  } else {
    idxTarget = potIndex(2, idxTarget);
    if (es == 2 && myFix.valid) {                                   // long press: remember where we are as the rally point
      rallyLat = myFix.lat; rallyLon = myFix.lon; rallyValid = true;
      prefs.putBool("rset", true); prefs.putInt("rlat", rallyLat); prefs.putInt("rlon", rallyLon);
      beep(80); statusLine = "rally saved";
    }
  }
  if (millis() - lastServo > 100) { lastServo = millis(); updateServo(); }
  if (pendingSeq && !delivered && millis() - pendingMs > 8000) { statusLine = String("no ack #") + pendingSeq; pendingSeq = 0; }
  if (millis() - lastBeacon > REPORT_INTERVAL_S * 1000UL) { lastBeacon = millis(); sendPacket(T_REPORT, 0); }
#if WIFI_UPLOAD
  upService();
#endif
  if (millis() - lastDraw > 150) { lastDraw = millis(); draw(); }
}
