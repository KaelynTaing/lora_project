// secure.h - ATECC608A signing/verifying + AES-GCM encryption + packet build/parse.
//
// Wire format (USE_ENCRYPTION=1):  [node_id 1][nonce 12][ AES-GCM( body 22 + signature 64 ) ][tag 16]  = 115 bytes
//   - node_id is sent in the clear but is authenticated (AES-GCM "additional data").
//   - the signature (ECDSA P-256, private key never leaves the ATECC608A) is made over SHA-256(body).
// Wire format (USE_ENCRYPTION=0):  [node_id 1][ body 22 + signature 64 ]                      = 87 bytes
#pragma once
#include <Wire.h>
#include <SparkFun_ATECCX08a_Arduino_Library.h>
#include "mbedtls/sha256.h"
#include "mbedtls/gcm.h"
#include "config.h"
#include "packet.h"
#include "secrets.h"      // AES_KEY (and Wi-Fi / Adafruit IO keys)
#include "peer_keys.h"    // PUBKEY_NODE_A, PUBKEY_NODE_B

static ATECCX08A atecc;

enum SecResult { SEC_OK = 0, SEC_BAD_LEN, SEC_BAD_DECRYPT, SEC_BAD_VERSION, SEC_UNKNOWN_NODE, SEC_BAD_SIG };
static inline const char* secResultName(SecResult r) {
  switch (r) { case SEC_OK: return "ok"; case SEC_BAD_LEN: return "bad_len"; case SEC_BAD_DECRYPT: return "bad_decrypt";
    case SEC_BAD_VERSION: return "bad_version"; case SEC_UNKNOWN_NODE: return "unknown_node"; default: return "bad_sig"; }
}

// Starts I2C + the chip. Returns false if the chip is missing or NOT fully locked (run atecc_setup first).
static inline bool secInit(bool requireLocked = true) {
  Wire.begin(PIN_SDA, PIN_SCL);
  if (!atecc.begin(0x60, Wire, Serial)) return false;
  atecc.readConfigZone(false);
  if (requireLocked && !(atecc.configLockStatus && atecc.dataOTPLockStatus && atecc.slot0LockStatus)) return false;
  return true;
}

static inline const uint8_t* pubkeyFor(uint8_t node) {
  if (node == NODE_ID_A) return PUBKEY_NODE_A;
  if (node == NODE_ID_B) return PUBKEY_NODE_B;
  return nullptr;
}

static inline bool secSign(const uint8_t* body, uint8_t* sig) {
  uint8_t h[32]; mbedtls_sha256(body, BODY_LEN, h, 0);
  if (!atecc.createSignature(h)) return false;       // signs with the private key in slot 0
  memcpy(sig, atecc.signature, SIG_LEN);
  return true;
}
static inline bool secVerify(const uint8_t* body, const uint8_t* sig, const uint8_t* pub) {
  uint8_t h[32]; mbedtls_sha256(body, BODY_LEN, h, 0);
  return atecc.verifySignature(h, (uint8_t*)sig, (uint8_t*)pub);
}

// Build a signed (and optionally encrypted) packet. Returns length, 0 on failure.
static inline size_t buildWire(const Body& b, uint8_t* wire) {
  uint8_t plain[PLAIN_LEN];
  packBody(b, plain);
  if (!secSign(plain, plain + BODY_LEN)) return 0;
  wire[0] = b.node;
#if USE_ENCRYPTION
  if (!atecc.updateRandom32Bytes(false)) return 0;          // fresh random nonce from the chip's hardware RNG
  uint8_t* nonce = wire + 1;
  uint8_t* ct    = nonce + GCM_NONCE;
  uint8_t* tag   = ct + PLAIN_LEN;
  memcpy(nonce, atecc.random32Bytes, GCM_NONCE);
  mbedtls_gcm_context g; mbedtls_gcm_init(&g);
  int rc = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, AES_KEY, 128);
  if (!rc) rc = mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, PLAIN_LEN, nonce, GCM_NONCE, wire, 1, plain, ct, GCM_TAG, tag);
  mbedtls_gcm_free(&g);
  return rc ? 0 : (1 + GCM_NONCE + PLAIN_LEN + GCM_TAG);
#else
  memcpy(wire + 1, plain, PLAIN_LEN);
  return 1 + PLAIN_LEN;
#endif
}

// Decrypt + check version + check the sender's signature. Does NOT do replay checks (use ReplayGuard after this).
static inline SecResult parseWire(const uint8_t* wire, size_t len, uint8_t myId, Body& out) {
  uint8_t plain[PLAIN_LEN];
#if USE_ENCRYPTION
  if (len != (size_t)(1 + GCM_NONCE + PLAIN_LEN + GCM_TAG)) return SEC_BAD_LEN;
  const uint8_t* nonce = wire + 1; const uint8_t* ct = nonce + GCM_NONCE; const uint8_t* tag = ct + PLAIN_LEN;
  mbedtls_gcm_context g; mbedtls_gcm_init(&g);
  int rc = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, AES_KEY, 128);
  if (!rc) rc = mbedtls_gcm_auth_decrypt(&g, PLAIN_LEN, nonce, GCM_NONCE, wire, 1, tag, GCM_TAG, ct, plain);
  mbedtls_gcm_free(&g);
  if (rc) return SEC_BAD_DECRYPT;
#else
  if (len != (size_t)(1 + PLAIN_LEN)) return SEC_BAD_LEN;
  memcpy(plain, wire + 1, PLAIN_LEN);
#endif
  if (!unpackBody(plain, out)) return SEC_BAD_VERSION;
  const uint8_t* pub = pubkeyFor(out.node);
  if (!pub || out.node == myId || out.node != wire[0]) return SEC_UNKNOWN_NODE;
  if (!secVerify(plain, plain + BODY_LEN, pub)) return SEC_BAD_SIG;
  return SEC_OK;
}
