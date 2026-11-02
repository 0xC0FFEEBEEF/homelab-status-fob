#include <Wire.h>
#include "common/oled.h"

U8G2   *dsp = nullptr;
uint8_t oledSda = 14, oledScl = 12, oledAddr = 0x3C;
bool    oledFound = false;

// Autoprobing this board turned out to be a dead end: a bare-address write
// NAKs (rc=2) on every pin pair and at every address, while U8g2's own
// transmissions to the very same pins drive the panel perfectly. The probe was
// therefore measuring something other than "is the display there", and its
// false negative printed an alarming and wrong diagnosis at every boot.
//
// So the pin order is explicit instead. The A revision is the default; if your
// board is a B (SDA and SCL swapped) the screen stays blank, and you flip the
// one define below. `pio run -e diag` still exists to hunt an intermittent
// solder joint.
void oledBegin(uint32_t busHz) {
#ifdef FOB_BOARD_REV_B
  oledSda = 12; oledScl = 14;
#else
  oledSda = 14; oledScl = 12;
#endif
  delay(120);                     // the panel's controller lags the ESP's boot

  dsp = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE, oledScl, oledSda);
  dsp->setI2CAddress(oledAddr << 1);
  dsp->setBusClock(busHz);
  dsp->begin();
  Wire.setClock(busHz);
  dsp->setContrast(255);
  oledFound = true;

  Serial.printf("[fob] OLED 0x%02X SDA=GPIO%u SCL=GPIO%u @%lukHz%s\n",
                oledAddr, oledSda, oledScl, (unsigned long)(busHz / 1000),
#ifdef FOB_BOARD_REV_B
                "  (rev B pinout)"
#else
                "  (rev A pinout; blank screen => -D FOB_BOARD_REV_B)"
#endif
  );
}
