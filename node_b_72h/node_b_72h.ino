// node_b_72h.ino - NODE B: USB/wall-powered gateway for the 72-hour test.
// Listens on LoRa -> decrypts + verifies the signature (ATECC608A) + replay check -> sends a signed ACK back
// -> uploads the report to Adafruit IO over Wi-Fi (queued and retried if Wi-Fi is down).
// Board: "ESP32 Dev Module". Libraries: LoRa, SparkFun ATECCX08a, Adafruit SSD1306 + GFX (TinyGPSPlus is not needed here).
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include "config.h"
#include "packet.h"
#include "secure.h"
#include "radio.h"
#include "aio.h"

#define MY_ID NODE_ID_B
#define HEARTBEAT_S 600          // "gateway alive" post every 10 minutes
#define QUEUE_N 24

Adafruit_SSD1306 display(OLED_W, OLED_H, &SPI, PIN_OLED_DC, PIN_OLED_RES, PIN_OLED_CS);
ReplayGuard guard;

struct Report { Body b; int rssi; uint32_t rxTime; };
Report queue_[QUEUE_N]; int qHead = 0, qCount = 0;

uint32_t rxOk = 0, rxRejected = 0, uploadsOk = 0, lastBeat = 0;
Body lastBody = {}; int lastRssi = 0; uint32_t lastRxMs = 0; bool haveLast = false;
uint32_t lastRejectPost = 0;

static void enqueue(const Report& r) {
  if (qCount == QUEUE_N) { qHead = (qHead + 1) % QUEUE_N; qCount--; }      // drop oldest if full
  queue_[(qHead + qCount) % QUEUE_N] = r; qCount++;
}

static String feedName(uint8_t node, const char* what) { return String("n") + node + "-" + what; }

static bool uploadReport(const Report& r) {
  const Body& b = r.b;
  const char* typ = b.type == T_SOS ? "SOS" : b.type == T_MSG ? "MSG" : "REPORT";
  float lat = b.lat / 1e7f, lon = b.lon / 1e7f;
  String log = String("n") + b.node + " seq=" + b.seq + " " + typ + " bat=" + String(b.vbat_mV / 1000.0, 2) + "V rssi=" + r.rssi +
               " sats=" + b.sats + " fix=" + ((b.flags & F_GPS_FIX) ? 1 : 0) + " missed=" + b.msg + " verified=1 t=" + r.rxTime;
  bool ok = aioPost("lora-log", log);
  ok &= aioPost(feedName(b.node, "batt").c_str(), String(b.vbat_mV / 1000.0, 2));
  ok &= aioPost(feedName(b.node, "rssi").c_str(), String(r.rssi));
  if (b.lat != 0 || b.lon != 0) ok &= aioPost(feedName(b.node, "loc").c_str(), String(b.seq), true, lat, lon);
  return ok;
}

static void service_uploads() {
  if (!qCount) return;
  if (!wifiConnect(8000)) return;
  while (qCount) {
    if (!uploadReport(queue_[qHead])) { Serial.println("[B] upload failed, will retry"); return; }
    qHead = (qHead + 1) % QUEUE_N; qCount--; uploadsOk++;
  }
}

static void drawScreen() {
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  display.setCursor(0, 0);  display.printf("NODE B gateway  %s", WiFi.status() == WL_CONNECTED ? "WiFi" : "no-WiFi");
  display.setCursor(0, 12); display.printf("rx ok %lu  rej %lu", (unsigned long)rxOk, (unsigned long)rxRejected);
  display.setCursor(0, 24); display.printf("uploaded %lu  queued %d", (unsigned long)uploadsOk, qCount);
  if (haveLast) {
    display.setCursor(0, 38); display.printf("last: n%u seq %u", lastBody.node, lastBody.seq);
    display.setCursor(0, 50); display.printf("%.2fV rssi %d %lum", lastBody.vbat_mV / 1000.0, lastRssi, (unsigned long)((millis() - lastRxMs) / 60000));
  } else { display.setCursor(0, 44); display.print("waiting for Node A..."); }
  display.display();
}

static void sendAck(const Body& rx) {
  Body a = {};
  a.ver = PKT_VERSION; a.node = MY_ID; a.type = T_ACK; a.seq = rx.seq;    // echoes the sequence number being acknowledged
  a.time = (uint32_t)time(nullptr);
  uint8_t wire[WIRE_MAX];
  size_t n = buildWire(a, wire);
  delay(150);                                                              // give Node A time to switch to receive
  if (n) radioSend(wire, n);
}

void setup() {
  Serial.begin(115200); delay(1000);
  pinMode(PIN_LED, OUTPUT);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI);
  display.begin(SSD1306_SWITCHCAPVCC);
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0); display.println("NODE B starting"); display.display();
  if (!secInit()) { Serial.println("[B] ATECC not ready - run atecc_setup"); display.println("ATECC ERROR"); display.display(); while (1) delay(1000); }
  if (!radioBegin()) { Serial.println("[B] LoRa init failed"); display.println("LORA ERROR"); display.display(); while (1) delay(1000); }
  Serial.print("[B] Wi-Fi... ");
  bool w = wifiConnect(20000);
  Serial.println(w ? "connected" : "FAILED (will keep retrying)");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  drawScreen();
  Serial.println("[B] listening");
}

void loop() {
  uint8_t rx[WIRE_MAX + 8]; int rssi = 0;
  int len = radioRecv(rx, sizeof(rx), 300, &rssi);
  if (len != 0) {
    Body b;
    SecResult r = (len < 0) ? SEC_BAD_LEN : parseWire(rx, len, MY_ID, b);
    if (r == SEC_OK && !guard.accept(b.node, b.seq)) { r = SEC_BAD_SIG; Serial.println("[B] REPLAY: old sequence number rejected"); }
    if (r == SEC_OK) {
      rxOk++; lastBody = b; lastRssi = rssi; lastRxMs = millis(); haveLast = true;
      digitalWrite(PIN_LED, HIGH);
      Serial.printf("[B] OK  n%u seq=%u type=%u bat=%umV lat=%.5f lon=%.5f rssi=%d\n", b.node, b.seq, b.type, b.vbat_mV, b.lat / 1e7, b.lon / 1e7, rssi);
      if (b.type != T_ACK) { sendAck(b); Report rep = { b, rssi, (uint32_t)time(nullptr) }; enqueue(rep); }
      digitalWrite(PIN_LED, LOW);
    } else {
      rxRejected++;
      Serial.printf("[B] REJECTED (%s) len=%d rssi=%d\n", secResultName(r), len, rssi);
      if (wifiConnect(3000) && millis() - lastRejectPost > 60000) {     // log at most one rejection per minute
        lastRejectPost = millis();
        aioPost("lora-log", String("REJECTED ") + secResultName(r) + " len=" + len + " rssi=" + rssi);
      }
    }
    drawScreen();
  }
  service_uploads();
  if (millis() - lastBeat > HEARTBEAT_S * 1000UL) {
    lastBeat = millis();
    if (wifiConnect(5000)) aioPost("gateway-hb", String("up=") + (millis() / 60000) + "min rx=" + rxOk + " rej=" + rxRejected + " up_ok=" + uploadsOk);
    drawScreen();
  }
  if (WiFi.status() != WL_CONNECTED) { static uint32_t t = 0; if (millis() - t > 30000) { t = millis(); WiFi.reconnect(); } }
}
