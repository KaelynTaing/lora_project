// compass_test.ino - run this FIRST when the compass arrives (no LoRa needed).
// 1) finds the chip (0x0D or 0x1E)  2) prints raw values  3) after ~15 s of slowly spinning the board flat
// through a full circle, prints the heading. Point the board at a phone compass app's north to check it.
#include "config.h"
#include "compass.h"

void setup() {
  Serial.begin(115200); delay(1000);
  Wire.begin(PIN_SDA, PIN_SCL);
  Serial.println("I2C scan:");
  for (uint8_t a = 1; a < 127; a++) if (i2cPing(a)) Serial.printf("  found 0x%02X\n", a);
  if (!compassBegin()) { Serial.println("No compass at 0x0D/0x1E. Check SDA/SCL/3V3/GND and solder joints."); while (1) delay(1000); }
  Serial.printf("Compass chip at 0x%02X (%s)\n", compassAddr, compassAddr == 0x0D ? "QMC5883L" : "HMC5883L");
  Serial.println("Slowly rotate the board flat through 360 degrees for 15 s...");
}

void loop() {
  static uint32_t t0 = millis();
  int16_t x, y, z;
  if (compassReadRaw(x, y, z)) {
    if (millis() - t0 < 15000) compassCalibrateSample(x, y);
    Serial.printf("raw x=%d y=%d z=%d  heading=%.0f  (x range %.0f..%.0f, y range %.0f..%.0f)\n", x, y, z, compassHeading(), cxMin, cxMax, cyMin, cyMax);
  } else Serial.println("read failed");
  delay(200);
}
