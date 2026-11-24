// atecc_setup.ino - ONE-TIME setup of the ATECC608A. Run on each board (Node A, then Node B).
//
// What it does:  writes the config -> LOCKS the config (permanent) -> makes the private key in slot 0
//                (it can never be read out) -> locks data + slot 0 -> prints the PUBLIC key to paste
//                into shared/peer_keys.h.
// WARNING: locking is permanent. Do the first run on a chip you're happy to commit to this project.
// Serial monitor: 115200 baud, line ending "Newline".
#include "config.h"
#include "packet.h"
#include "secure.h"

static String readLine() {
  while (!Serial.available()) delay(10);
  String s = Serial.readStringUntil('\n'); s.trim(); return s;
}

static void printStatus() {
  atecc.readConfigZone(false);
  Serial.printf("Config locked: %s | Data/OTP locked: %s | Slot0 locked: %s\n",
    atecc.configLockStatus ? "YES" : "no", atecc.dataOTPLockStatus ? "YES" : "no", atecc.slot0LockStatus ? "YES" : "no");
}

static void printPublicKey() {
  if (!atecc.generatePublicKey(0, false)) { Serial.println("Could not read public key."); return; }
  Serial.println("\n// ---- copy this into shared/peer_keys.h (rename to PUBKEY_NODE_A or PUBKEY_NODE_B) ----");
  Serial.println("static const uint8_t PUBKEY_THIS_NODE[64] = {");
  for (int i = 0; i < 64; i++) {
    Serial.printf("0x%02X%s", atecc.publicKey64Bytes[i], i == 63 ? "" : ",");
    if (i % 16 == 15) Serial.println();
  }
  Serial.println("};\n// ----------------------------------------------------------------------------\n");
}

static void selfTest() {
  // Sign a test message with the private key, then verify it with the public key. Proves the chip works.
  uint8_t body[PLAIN_LEN] = {0}; body[0] = 0xAB;
  if (!secSign(body, body + BODY_LEN)) { Serial.println("SIGN FAILED"); return; }
  atecc.generatePublicKey(0, false);
  uint8_t pub[64]; memcpy(pub, atecc.publicKey64Bytes, 64);
  bool good = secVerify(body, body + BODY_LEN, pub);
  body[3] ^= 1;                                               // tamper with one bit
  bool bad  = secVerify(body, body + BODY_LEN, pub);
  Serial.printf("Self-test: valid signature verifies = %s, tampered message verifies = %s\n", good ? "YES" : "NO", bad ? "YES (BAD!)" : "no (good)");
  Serial.println(good && !bad ? "SELF-TEST PASSED" : "SELF-TEST FAILED");
}

void setup() {
  Serial.begin(115200); delay(1500);
  Wire.begin(PIN_SDA, PIN_SCL);
  if (!atecc.begin(0x60, Wire, Serial)) { Serial.println("ATECC608A not found. Check SDA=21 SCL=22 3V3 GND."); while (1) delay(1000); }
  Serial.println("ATECC608A found.");
  printStatus();

  if (!atecc.configLockStatus) {
    Serial.println("\nThis chip is NOT configured. Setup will PERMANENTLY lock it.");
    Serial.println("Type  LOCK  and press enter to continue (anything else cancels):");
    if (readLine() != "LOCK") { Serial.println("Cancelled. Nothing was changed."); while (1) delay(1000); }
    Serial.print("Write config: ");   Serial.println(atecc.writeConfigSparkFun() ? "ok" : "FAILED");
    Serial.print("Lock config: ");    Serial.println(atecc.lockConfig()          ? "ok" : "FAILED");
    Serial.print("Create key pair: "); Serial.println(atecc.createNewKeyPair()   ? "ok" : "FAILED");
    Serial.print("Lock data/OTP: ");  Serial.println(atecc.lockDataAndOTP()      ? "ok" : "FAILED");
    Serial.print("Lock slot 0: ");    Serial.println(atecc.lockDataSlot0()       ? "ok" : "FAILED");
    printStatus();
  } else if (!atecc.dataOTPLockStatus || !atecc.slot0LockStatus) {
    Serial.println("Config is locked but key/data are not. Finishing...");
    Serial.print("Create key pair: "); Serial.println(atecc.createNewKeyPair() ? "ok" : "FAILED");
    Serial.print("Lock data/OTP: ");  Serial.println(atecc.lockDataAndOTP()    ? "ok" : "FAILED");
    Serial.print("Lock slot 0: ");    Serial.println(atecc.lockDataSlot0()     ? "ok" : "FAILED");
    printStatus();
  } else {
    Serial.println("Already fully configured - nothing to lock.");
  }
  printPublicKey();
  selfTest();
  Serial.println("Commands: p = print public key again, t = self-test");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'p') printPublicKey();
    if (c == 't') selfTest();
  }
}
