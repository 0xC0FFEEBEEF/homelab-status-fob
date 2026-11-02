// Shared drawing primitives: top bar, page dots, bars, pips, glyphs.
#include "ui.h"
#include "common/anim.h"

static int16_t cx0 = 0, cy0 = 0, cx1 = SCR_W, cy1 = SCR_H;

void clipSet(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  cx0 = x0; cy0 = y0; cx1 = x1; cy1 = y1;
  dsp->setClipWindow(x0, y0, x1, y1);
}
void clipRestore() { dsp->setClipWindow(cx0, cy0, cx1, cy1); }
void clipFull() {
  cx0 = 0; cy0 = 0; cx1 = SCR_W; cy1 = SCR_H;
  dsp->setMaxClipWindow();
}

// Cohen-Sutherland-lite: reject or clamp in signed space, because every u8g2
// primitive takes u8g2_uint_t and drawLine has no intersection check.
void safeLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  if ((x0 < 0 && x1 < 0) || (x0 >= SCR_W && x1 >= SCR_W)) return;
  if ((y0 < 0 && y1 < 0) || (y0 >= SCR_H && y1 >= SCR_H)) return;
  // Short segments only (sparklines, tick marks), so clamping the endpoints
  // costs at most a pixel of slope and can never run away.
  x0 = (int16_t)constrain(x0, 0, SCR_W - 1);
  x1 = (int16_t)constrain(x1, 0, SCR_W - 1);
  y0 = (int16_t)constrain(y0, 0, SCR_H - 1);
  y1 = (int16_t)constrain(y1, 0, SCR_H - 1);
  dsp->drawLine((uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1);
}

// 4 ascending bars; unfilled levels stay as a 1px floor so the icon keeps
// its shape at low signal.
void drawWifi(int16_t x, int16_t y, int8_t rssi, float t) {
  (void)t;
  uint8_t level = 0;
  if      (rssi > -55) level = 4;
  else if (rssi > -65) level = 3;
  else if (rssi > -75) level = 2;
  else if (rssi > -85) level = 1;
  for (uint8_t i = 0; i < 4; i++) {
    const uint8_t h = (uint8_t)(2 + i * 2);
    const int16_t bx = x + i * 2;
    if (i < level) dsp->drawVLine(bx, y - h, h);
    else           dsp->drawPixel(bx, y - 1);
  }
}

void drawTopBar(float t) {
  drawWifi(1, 15, g.rssi, t);

  // The yellow band is 16 rows, so the clock gets a real size here.
  char buf[8];
  snprintf(buf, sizeof buf, "%02u:%02u", g.hh, g.mm);
  dsp->setFont(u8g2_font_logisoso16_tr);
  const uint8_t w = dsp->getStrWidth(buf);
  dsp->drawStr((SCR_W - w) / 2, 15, buf);

  if (g.alerts) {
    // Badge breathes so a glance catches it without reading anything.
    const float p = pulse(t * 1000.f, 900.f);
    const uint8_t r = (uint8_t)(5 + p * 1.6f);
    dsp->drawDisc(SCR_W - 7, 8, r);
    dsp->setDrawColor(0);
    dsp->setFont(u8g2_font_5x7_tr);
    char n[4];
    snprintf(n, sizeof n, "%u", g.alerts > 9 ? 9 : g.alerts);
    dsp->drawStr(SCR_W - 9, 11, n);
    dsp->setDrawColor(1);
  } else {
    dsp->drawCircle(SCR_W - 7, 8, 2);
  }
  // No rule: the panel's own yellow/blue boundary at row 15 is the divider.
}

// Page dots with a sliding indicator; slide is a fractional card index.
void drawDots(uint8_t active, float slide) {
  (void)active;
  const uint8_t sp = 8;
  const int16_t x0 = (SCR_W - CARD_COUNT * sp) / 2 + 2;
  for (uint8_t i = 0; i < CARD_COUNT; i++) dsp->drawPixel(x0 + i * sp + 1, DOTS_Y + 1);
  const int16_t ix = x0 + (int16_t)(slide * sp);
  dsp->drawRBox(ix - 1, DOTS_Y, 5, 3, 1);
}

// Fill plus a detached tip, which reads as motion while the value tweens.
void drawBarH(int16_t x, int16_t y, uint8_t w, uint8_t h, float pct) {
  dsp->drawFrame(x, y, w, h);
  const uint8_t inner = (uint8_t)(w - 2);
  uint8_t fw = (uint8_t)(clamp01(pct) * inner);
  if (fw > 2 && fw < inner) {
    dsp->drawBox(x + 1, y + 1, (uint8_t)(fw - 2), (uint8_t)(h - 2));
    dsp->drawVLine(x + 1 + fw, y + 1, (uint8_t)(h - 2));
  } else if (fw) {
    dsp->drawBox(x + 1, y + 1, fw, (uint8_t)(h - 2));
  }
}

void drawPip(int16_t x, int16_t y, bool up, float t) {
  if (up) {
    dsp->drawBox(x, y, 4, 4);
  } else {
    dsp->drawFrame(x, y, 4, 4);
    if (pulse(t * 1000.f, 700.f) > 0.5f) dsp->drawBox(x + 1, y + 1, 2, 2);
  }
}

void drawBattery(int16_t x, int16_t y, uint8_t w, uint8_t h, float pct, bool charging, float t) {
  dsp->drawFrame(x, y, w, h);
  dsp->drawBox(x + w, (int16_t)(y + h / 3), 2, (uint8_t)(h / 3));   // terminal nub
  const uint8_t inner = (uint8_t)(w - 4);
  const uint8_t fw = (uint8_t)(clamp01(pct) * inner);
  if (fw) dsp->drawBox(x + 2, y + 2, fw, (uint8_t)(h - 4));
  if (charging) {
    // A chevron climbing the cell, inverted over the fill so it stays visible.
    const float p = fmodf(t * 1.1f, 1.f);
    const int16_t cyy = (int16_t)(y + h - 3 - p * (h - 6));
    dsp->setDrawColor(2);
    for (uint8_t i = 0; i < 4; i++) {
      dsp->drawPixel((int16_t)(x + w / 2 - 3 + i), (int16_t)(cyy + (i < 2 ? i : 3 - i)));
    }
    dsp->setDrawColor(1);
  }
}

// Drawn progressively so tiles can pop in.
void drawCheck(int16_t x, int16_t y, float t) {
  const float e = easeOutCubic(t);
  if (e > 0.f) safeLine(x, (int16_t)(y + 2), (int16_t)(x + 2), (int16_t)(y + 4));
  if (e > 0.45f) {
    const int16_t len = (int16_t)(1 + (e - 0.45f) / 0.55f * 3.f);
    safeLine((int16_t)(x + 2), (int16_t)(y + 4), (int16_t)(x + 2 + len), (int16_t)(y + 4 - len));
  }
}

void drawCross(int16_t x, int16_t y, float t) {
  const float e = easeOutCubic(t);
  const int16_t n = (int16_t)(e * 4.f);
  for (int16_t i = 0; i <= n && i < 5; i++) {
    dsp->drawPixel((int16_t)(x + i), (int16_t)(y + i));
    dsp->drawPixel((int16_t)(x + 4 - i), (int16_t)(y + i));
  }
}

// Scrolls only when the text does not fit, with a gap between repeats.
void drawMarquee(int16_t x, int16_t y, uint8_t w, const char *s, float t) {
  const uint16_t sw = dsp->getStrWidth(s);
  if (sw <= w) { dsp->drawStr(x, y, s); return; }
  const float span = sw + 14.f;
  const float ph = fmodf(t * 16.f, span);
  int16_t cy1b = (int16_t)(y + 2);
  if (cy1b > CARD_Y1 + 1) cy1b = CARD_Y1 + 1;   // page 7 must not be touched
  dsp->setClipWindow(x, (int16_t)(y - 7), (int16_t)(x + w), cy1b);
  dsp->drawStr((int16_t)(x - ph), y, s);
  dsp->drawStr((int16_t)(x - ph + span), y, s);
  clipRestore();
}

// A short segment tracking back and forth: the "alive" cue on idle cards.
void drawSweep(int16_t x, int16_t y, uint8_t w, float t) {
  const uint8_t seg = (uint8_t)(w * 0.32f);
  const float e = easeInOutCubic(pulse(t * 1000.f, 2600.f));
  const int16_t sx = (int16_t)(x + e * (w - seg));
  dsp->drawHLine(sx, y, seg);
}
