// Boot animation and the at-rest idle face.
#include "ui.h"
#include "common/anim.h"

// Rings out of the centre, then the wordmark types itself in.
void drawBoot(float t) {
  const int16_t cx = 64, cy = 30;

  if (t < 0.55f) {
    const float p = t / 0.55f;
    for (uint8_t i = 0; i < 3; i++) {
      const float pp = p - i * 0.17f;
      if (pp <= 0.f) continue;
      const int16_t r = (int16_t)(easeOutCubic(pp) * 78.f);
      if (r > 2 && r < 80) dsp->drawCircle(cx, cy, r);
    }
    dsp->drawDisc(cx, cy, 2);
  }

  if (t > 0.28f) {
    static const char *word = "nbdevlab";
    dsp->setFont(u8g2_font_helvB10_tr);
    const uint8_t full = dsp->getStrWidth(word);
    const float p = clamp01((t - 0.28f) / 0.42f);
    const uint8_t n = (uint8_t)(p * strlen(word) + 0.5f);
    char buf[12];
    memset(buf, 0, sizeof buf);
    strncpy(buf, word, n);
    dsp->drawStr((int16_t)(cx - full / 2), 34, buf);
    // Cursor block while typing.
    if (p < 1.f && pulse(t * 1000.f, 260.f) > 0.5f) {
      dsp->setFont(u8g2_font_4x6_tr);
      dsp->drawBox((int16_t)(cx - full / 2 + dsp->getStrWidth("") + n * 8), 27, 5, 8);
    }
  }

  if (t > 0.66f) {
    dsp->setFont(u8g2_font_4x6_tr);
    const char *sub = "";
    const float a = clamp01((t - 0.66f) / 0.2f);
    const uint8_t w = dsp->getStrWidth(sub);
    dsp->drawStr((int16_t)(cx - w / 2), 46, sub);
    drawSweep((int16_t)(cx - 24), 50, (uint8_t)(48 * easeOutCubic(a)), t);
  }
}

// The resting face: big clock that drifts slowly so a static image never
// sits in one place on a mono OLED, plus the one word that matters.
void drawIdle(float t) {
  // Drift keeps a static clock off one set of pixels, but it has to stay
  // inside the blue band (rows 16..63) or it straddles the panel's colour
  // boundary as it moves.
  const float dx = sinf(t * 0.11f) * 17.f;
  const float dy = sinf(t * 0.074f) * 4.f;

  char clock[8];
  snprintf(clock, sizeof clock, "%02u:%02u", g.hh, g.mm);
  dsp->setFont(u8g2_font_logisoso24_tr);
  const uint8_t w = dsp->getStrWidth(clock);
  const int16_t x = (int16_t)(64 - w / 2 + dx);
  const int16_t y = (int16_t)(48 + dy);          // glyph top lands at ~20
  dsp->drawStr(x, y, clock);

  dsp->setFont(u8g2_font_4x6_tr);
  char line[28];
  if (g.alerts) snprintf(line, sizeof line, "%u ALERT%s", g.alerts, g.alerts > 1 ? "S" : "");
  else          snprintf(line, sizeof line, "nominal");
  const uint8_t lw = dsp->getStrWidth(line);
  dsp->drawStr((int16_t)(64 - lw / 2 + dx), (int16_t)(y + 10), line);

  if (g.alerts && pulse(t * 1000.f, 1100.f) > 0.6f) {
    dsp->drawFrame((int16_t)(64 - w / 2 + dx - 4), (int16_t)(y - 28), (uint8_t)(w + 8), 40);
  }
}
