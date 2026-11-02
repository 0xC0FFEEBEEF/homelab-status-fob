// Design 2 rendering: yellow-band chrome, the hero, and the spectrum strip.
#include "ui.h"
#include "common/anim.h"

// Same hazard as design 1: every u8g2 primitive takes an unsigned coordinate
// and drawLine has no bounds check, so clamp in signed space first.
static void safeLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  if ((x0 < 0 && x1 < 0) || (x0 >= SCR_W && x1 >= SCR_W)) return;
  if ((y0 < 0 && y1 < 0) || (y0 >= SCR_H && y1 >= SCR_H)) return;
  x0 = (int16_t)constrain(x0, 0, SCR_W - 1);
  x1 = (int16_t)constrain(x1, 0, SCR_W - 1);
  y0 = (int16_t)constrain(y0, 0, SCR_H - 1);
  y1 = (int16_t)constrain(y1, 0, SCR_H - 1);
  dsp->drawLine((uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1);
}

// Nothing here was tweened before, so every value snapped to its new sample
// every 450 ms. That is what made the motion read as dead, and it defeated the
// odometer entirely - rolling digits need a value that actually moves.
static Tween tHero;
static bool  animReady = false;

void d2Animate(float dt, uint8_t drawnHero) {
  if (!animReady) {
    animReady = true;
    tHero.snap(gM[drawnHero].value);
    for (uint8_t m = 0; m < METRIC_COUNT; m++) gM[m].disp = gM[m].norm;
  }
  tHero.rate = 4.5f;
  tHero.set(gM[drawnHero].value);
  tHero.update(dt);
  // Strip bars breathe toward their targets instead of stepping.
  float k = 6.f * dt;
  if (k > 1.f) k = 1.f;
  for (uint8_t m = 0; m < METRIC_COUNT; m++)
    gM[m].disp += (gM[m].norm - gM[m].disp) * k;
}
float d2HeroValue() { return tHero.v; }

int16_t stripTickX(uint8_t metric) {
  return (int16_t)(STRIP_X0 + metric * STRIP_STEP + gJx);
}

// ---- yellow band: time, which subsystem has focus, and why -----------------
void d2TopBar(uint8_t hero, float t) {
  char clk[8];
  snprintf(clk, sizeof clk, "%02u:%02u", g.hh, g.mm);
  dsp->setFont(u8g2_font_logisoso16_tr);
  dsp->drawStr(2, CLOCK_BASE, clk);

  // The reason the hero was promoted. This is the narrative of the design, so
  // it gets said in words rather than implied.
  const Metric &m = gM[hero];
  const char *why;
  if (m.alarm)            why = "ALARM";
  else if (m.rate >  .10f) why = "climbing";
  else if (m.rate < -.10f) why = "falling";
  else if (m.sev  >  .70f) why = "running hot";
  else                     why = "in focus";

  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr(54, 7, groupName(m.group));
  dsp->drawStr(54, 15, why);

  // A trend arrow that bobs in the direction the value is going.
  if (fabsf(m.rate) > 0.10f) {
    const int16_t ax = (int16_t)(56 + dsp->getStrWidth(why));
    const bool upward = m.rate > 0.f;
    const int16_t bob = (int16_t)(pulse(t * 1000.f, 700.f) * 2.f);
    const int16_t ay = (int16_t)(12 + (upward ? -bob : bob));
    for (uint8_t i = 0; i < 3; i++) {
      const int16_t dy = upward ? (int16_t)i : (int16_t)(2 - i);
      dsp->drawPixel((int16_t)(ax + 2 - i), (int16_t)(ay + dy));
      dsp->drawPixel((int16_t)(ax + 2 + i), (int16_t)(ay + dy));
    }
  }

  // wifi, bottom right
  uint8_t level = 0;
  if      (g.rssi > -55) level = 4;
  else if (g.rssi > -65) level = 3;
  else if (g.rssi > -75) level = 2;
  else if (g.rssi > -85) level = 1;
  for (uint8_t i = 0; i < 4; i++) {
    const uint8_t h = (uint8_t)(2 + i * 2);
    if (i < level) dsp->drawVLine(106 + i * 2, 15 - h, h);
    else           dsp->drawPixel(106 + i * 2, 14);
  }

  if (g.alerts) {
    const uint8_t r = (uint8_t)(4 + pulse(t * 1000.f, 900.f) * 1.5f);
    dsp->drawDisc(121, 5, r);
    dsp->setDrawColor(0);
    char n[4];
    snprintf(n, sizeof n, "%u", g.alerts > 9 ? 9 : g.alerts);
    dsp->drawStr(119, 8, n);
    dsp->setDrawColor(1);
  } else {
    dsp->drawCircle(121, 5, 2);
  }
}

// ---- odometer -------------------------------------------------------------
// Digits roll continuously with the value instead of snapping, and a digit
// only rolls once the one below it is about to carry - a real odometer.
static void drawOdometer(int16_t rightX, int16_t baseline, float v) {
  if (v < 0.f) v = 0.f;
  dsp->setFont(u8g2_font_logisoso24_tn);
  const uint8_t dw = dsp->getStrWidth("0");
  const int16_t h  = 24;

  uint8_t digits = 1;
  for (float lim = 10.f; v >= lim && digits < 4; lim *= 10.f) digits++;

  for (uint8_t k = 0; k < digits; k++) {
    const float pw   = powf(10.f, (float)k);
    const float u    = v / pw;
    const int   d    = ((int)floorf(u)) % 10;
    const float fr   = u - floorf(u);
    const float roll = (k == 0) ? fr : clamp01((fr - 0.9f) * 10.f);
    const int16_t cx  = (int16_t)(rightX - (k + 1) * dw);
    const int16_t off = (int16_t)(roll * h);

    dsp->setClipWindow(cx, (int16_t)(baseline - h + 1), (int16_t)(cx + dw), (int16_t)(baseline + 1));
    char c[2] = { 0, 0 };
    c[0] = (char)('0' + d);
    dsp->drawStr(cx, (int16_t)(baseline + off), c);
    c[0] = (char)('0' + ((d + 1) % 10));
    dsp->drawStr(cx, (int16_t)(baseline + off - h), c);
    dsp->setMaxClipWindow();
  }
}

// ---- hero ----------------------------------------------------------------
void d2Hero(uint8_t hero, float t) {
  const Metric &m = gM[hero];

  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(2 + gJx), (int16_t)(HERO_LABEL_B + gJy), m.label);

  // An alarming hero gets bracketed, and the brackets breathe.
  if (m.alarm && pulse(t * 1000.f, 1000.f) > 0.35f) {
    const int16_t x0 = (int16_t)(gJx), y0 = (int16_t)(17 + gJy);
    const int16_t x1 = (int16_t)(127 + gJx), y1 = (int16_t)(47 + gJy);
    for (uint8_t i = 0; i < 5; i++) {
      dsp->drawPixel((int16_t)(x0 + i), y0); dsp->drawPixel(x0, (int16_t)(y0 + i));
      dsp->drawPixel((int16_t)(x1 - i), y0); dsp->drawPixel(x1, (int16_t)(y0 + i));
      dsp->drawPixel((int16_t)(x0 + i), y1); dsp->drawPixel(x0, (int16_t)(y1 - i));
      dsp->drawPixel((int16_t)(x1 - i), y1); dsp->drawPixel(x1, (int16_t)(y1 - i));
    }
  }

  if (m.state) {
    // A down node or an unreachable service has no number worth showing.
    dsp->setFont(u8g2_font_logisoso16_tr);
    if (!m.alarm || pulse(t * 1000.f, 1200.f) > 0.18f)
      dsp->drawStr((int16_t)(2 + gJx), (int16_t)(HERO_VAL_B - 2 + gJy), m.state);
  } else {
    drawOdometer((int16_t)(HERO_VAL_R + gJx), (int16_t)(HERO_VAL_B + gJy), d2HeroValue());
    dsp->setFont(u8g2_font_6x10_tf);
    dsp->drawStr((int16_t)(HERO_VAL_R + 2 + gJx), (int16_t)(HERO_VAL_B + gJy), m.unit);
  }

  // The hero's own history, with its current point emphasised.
  const int16_t sx = (int16_t)(SPARK_X + gJx), sy = (int16_t)(SPARK_Y + gJy);
  const uint8_t cnt = histCount();
  int16_t prevY = 0, lastX = sx, lastY = sy;
  for (uint8_t i = 0; i < cnt; i++) {
    const int16_t px = (int16_t)(sx + SPARK_W - cnt + i);
    const int16_t py = (int16_t)(sy + SPARK_H - 1 - (histAt(hero, i) * (SPARK_H - 1)) / 255);
    if (i) safeLine((int16_t)(px - 1), prevY, px, py);
    prevY = py; lastX = px; lastY = py;
  }
  if (cnt) {
    dsp->drawDisc(lastX, lastY, (uint8_t)(1 + (pulse(t * 1000.f, 1400.f) > 0.5f ? 1 : 0)));
    for (int16_t x = sx; x < sx + SPARK_W; x += 4) dsp->drawPixel(x, (int16_t)(sy + SPARK_H));
  }
}

// ---- spectrum strip ------------------------------------------------------
// All 23 metrics, always live. reveal 0..1 staggers them in at boot.
void d2Strip(uint8_t hero, float t, float reveal) {
  const int16_t base = (int16_t)(STRIP_BASE + gJy);
  for (uint8_t m = 0; m < METRIC_COUNT; m++) {
    const float a = clamp01(reveal * METRIC_COUNT - m);
    if (a <= 0.f) continue;
    const int16_t x = stripTickX(m);
    const uint8_t hgt = (uint8_t)(gM[m].disp * STRIP_H * easeOutCubic(a));

    if (hgt) dsp->drawBox(x, (int16_t)(base - hgt + 1), 4, hgt);
    dsp->drawPixel((int16_t)(x + 1), (int16_t)(base + 1));   // baseline tick

    // Alarms blink a cap above the bar so they read even at 4px wide.
    if (gM[m].alarm && pulse(t * 1000.f, 600.f) > 0.4f)
      dsp->drawBox(x, (int16_t)(base - STRIP_H - 1), 4, 2);

    // The hero's own slot is marked and the marker breathes, so the link
    // between the spectrum and what is on top of the screen stays visible.
    if (m == hero) {
      dsp->drawBox((int16_t)(x - 1), (int16_t)(base + 2), 6, 1);
      const uint8_t g2 = (uint8_t)(pulse(t * 1000.f, 1500.f) * 2.f);
      dsp->drawVLine((int16_t)(x - 2 - g2), (int16_t)(base - 2), 4);
      dsp->drawVLine((int16_t)(x + 5 + g2), (int16_t)(base - 2), 4);
    }
  }

  // A highlight sweeps the spectrum end to end: constant, quiet motion that
  // ties the 23 bars together even when nothing is changing.
  const float sp = fmodf(t * 1000.f, 3400.f) / 3400.f;
  const int16_t scanX = (int16_t)(STRIP_X0 + gJx + sp * (METRIC_COUNT * STRIP_STEP));
  dsp->setDrawColor(2);
  dsp->drawVLine(scanX, (int16_t)(base - STRIP_H), (uint8_t)(STRIP_H + 2));
  dsp->setDrawColor(1);
}

void d2GroupLabels() {
  static const uint8_t start[GROUP_COUNT] = { 0, 7, 11, 14, 17 };
  static const uint8_t end_[GROUP_COUNT]  = { 6, 10, 13, 16, 22 };
  dsp->setFont(u8g2_font_4x6_tr);
  for (uint8_t grp = 0; grp < GROUP_COUNT; grp++) {
    const char *n = groupName(grp);
    const int16_t mid = (int16_t)((stripTickX(start[grp]) + stripTickX(end_[grp]) + 4) / 2);
    dsp->drawStr((int16_t)(mid - dsp->getStrWidth(n) / 2), (int16_t)(GROUP_B + gJy), n);
  }
}
