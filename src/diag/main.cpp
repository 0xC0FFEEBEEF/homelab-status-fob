// Flaky-joint finder.
//
// Re-probes the OLED continuously and prints only the transitions, so you can
// press and flex around the panel's four pins and watch for it appearing or
// dropping out. If pressing makes it FOUND, it is a solder joint, not the chip.
//
//   pio run -e diag -t upload && pio device monitor
#include <Arduino.h>
#include <Wire.h>

static bool seen = false;
static uint32_t changes = 0;

static bool probeOnce(uint8_t sda, uint8_t scl, uint8_t addr) {
  Wire.begin(sda, scl);
  Wire.setClock(100000);
  Wire.beginTransmission(addr);
  Wire.write(0xE3);                    // real byte; zero-length writes NAK
  return Wire.endTransmission() == 0;
}

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println("\n[diag] OLED joint finder. Press and flex around the panel's");
  Serial.println("[diag] VCC / GND / SDA / SCL pins. Transitions print below.");
  Serial.println("[diag] Silence = no change. Nothing at all = never detected.");
}

void loop() {
  // Either pin order, either address - whatever answers counts as found.
  const bool now = probeOnce(14, 12, 0x3C) || probeOnce(14, 12, 0x3D)
                || probeOnce(12, 14, 0x3C) || probeOnce(12, 14, 0x3D);
  if (now != seen) {
    seen = now;
    changes++;
    Serial.printf("[diag] %-5s  (%lu transitions, %lus in)\n",
                  seen ? "FOUND" : "LOST", (unsigned long)changes, millis() / 1000);
  }
  static uint32_t last = 0;
  if (millis() - last > 10000) {
    last = millis();
    Serial.printf("[diag] still %s\n", seen ? "FOUND" : "absent");
  }
  delay(120);
}
