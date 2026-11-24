# LoRa secure nodes - code pack (NOT tested on real hardware yet)

## Install once (Arduino IDE > Library Manager)
LoRa (Sandeep Mistry) | SparkFun Cryptographic Co-processor ATECCX08a | TinyGPSPlus | Adafruit SSD1306 | Adafruit GFX
Board: "ESP32 Dev Module" (Espressif esp32 core).

## How the folders work
Edit ONLY files in `shared/` (config.h pins, secrets.h, peer_keys.h), then run `python3 tools/sync.py`
(Arduino sketches can't share files, so this copies shared/*.h into every sketch folder).

## Order to run things
1. `cloud_test`      - fill shared/secrets.h, make feeds `lora-log`, `n1-batt` in Adafruit IO. Posts fake data. (no LoRa needed)
2. `atecc_setup`     - once per board. Type LOCK when asked. Paste each printed public key into shared/peer_keys.h
                       (first board -> PUBKEY_NODE_A, second -> PUBKEY_NODE_B), run tools/sync.py.
3. `python3 tools/make_aes_key.py` -> paste into shared/secrets.h (same key on both nodes), sync.
4. Bench test: set FAST_BENCH_MODE 1 and LORA_TX_DBM 10 in config.h, sync, flash `node_b_72h` on board B and `node_a_72h` on board A.
   Node A sends a signed packet, B acks + uploads. Watch the Serial Monitors + the dashboard.
5. Real test: FAST_BENCH_MODE 0 (every 3 h), run on batteries.
6. `compass_test`    - when the compass arrives. Then set HAVE_COMPASS 1 in node_twoway.
7. `node_twoway`     - same sketch on both boards; change MY_NODE_ID (1 / 2).

## Adafruit IO feeds to create
lora-log, gateway-hb, n1-batt, n1-rssi, n1-loc (type: map), n2-batt, n2-rssi, n2-loc

## Hardware change needed for the 72 h test: switch the GPS off during sleep
NEO-6M draws ~45 mA all the time. Left on 3V3 it would drain 4xAA in ~1.5 days.
GPS GND -> collector of an NPN transistor (2N2222/2N3904), emitter -> GND, base -> 1 kOhm -> GPIO 12 (PIN_GPS_PWR).
(GPS VCC stays on 3V3.) If you skip this, set PIN_GPS_PWR to -1 and expect to fail 72 h.

## Tests that run on a PC
g++ -std=c++11 -I shared tests/test_logic.cpp -o t && ./t
