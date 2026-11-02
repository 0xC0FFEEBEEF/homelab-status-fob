// Six pages, restrained: a letter-spaced label in the amber band, one figure
// that carries the screen, hairlines, and small right-aligned secondaries.
#include "ui.h"
#include "common/anim.h"

float gAge = 0.f;

static inline float cF(float c) { return c * 1.8f + 32.f; }
static inline float mph(float ms) { return ms * 2.23694f; }
static inline float inHg(float mb) { return mb * 0.0295300f; }
static inline float mi(float km) { return km * 0.621371f; }
static inline float in_(float mm) { return mm * 0.0393701f; }

void eyebrow(int16_t x, int16_t y, const char *s) {
  dsp->setFont(u8g2_font_4x6_tr);
  char c[2] = { 0, 0 };
  for (uint8_t i = 0; s[i] && i < 22; i++) { c[0] = s[i]; dsp->drawStr((int16_t)(x + i * 5), y, c); }
}
void rightStr(int16_t rx, int16_t y, const char *s) {
  dsp->drawStr((int16_t)(rx - dsp->getStrWidth(s)), y, s);
}

// ---- an animated weather glyph, driven by the API's own icon string --------
// clear-day, clear-night, cloudy, foggy, partly-cloudy-day/night, rainy, sleet,
// snow, thunderstorm, possibly-*, windy.
static inline bool has(const char *h, const char *n) { return strstr(h, n) != nullptr; }
static inline float hsh(uint8_t i) {
  const float v = sinf(i * 12.9898f) * 43758.5453f;
  return v - floorf(v);
}

static void gSun(int16_t cx, int16_t cy, uint8_t r, float t) {
  dsp->drawDisc(cx, cy, r);
  for (uint8_t a = 0; a < 8; a++) {
    const float an = a * 0.7854f + t * 0.35f;      // rays turn slowly
    const uint8_t len = (uint8_t)(r + 3 + (a & 1));
    dsp->drawPixel((int16_t)(cx + cosf(an) * len), (int16_t)(cy + sinf(an) * len));
    dsp->drawPixel((int16_t)(cx + cosf(an) * (len + 2)), (int16_t)(cy + sinf(an) * (len + 2)));
  }
}
static void gMoon(int16_t cx, int16_t cy, uint8_t r, float t) {
  dsp->drawDisc(cx, cy, r);
  dsp->setDrawColor(0);
  dsp->drawDisc((int16_t)(cx + r - 1), (int16_t)(cy - 2), r);
  dsp->setDrawColor(1);
  for (uint8_t i = 0; i < 4; i++) {                 // stars twinkle
    const int16_t sx = (int16_t)(cx - 14 + hsh(i) * 28);
    const int16_t sy = (int16_t)(cy - 12 + hsh((uint8_t)(i + 9)) * 24);
    if (((uint16_t)(t * 2.f) + i * 3) % 5) dsp->drawPixel(sx, sy);
  }
}
static void gCloud(int16_t cx, int16_t cy, float t, float driftPxS) {
  const int16_t d = (int16_t)(sinf(t * driftPxS * 0.1f) * 2.f);
  dsp->drawDisc((int16_t)(cx - 5 + d), cy, 3);
  dsp->drawDisc((int16_t)(cx + d), (int16_t)(cy - 2), 4);
  dsp->drawDisc((int16_t)(cx + 6 + d), cy, 3);
  dsp->drawHLine((int16_t)(cx - 8 + d), (int16_t)(cy + 3), 16);
}
static void gRain(int16_t cx, int16_t cy, float t, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    const float sp = 22.f + hsh(i) * 16.f;
    const float y = fmodf(t * sp + hsh((uint8_t)(i + 5)) * 18.f, 18.f);
    const int16_t x = (int16_t)(cx - 7 + hsh((uint8_t)(i + 11)) * 15.f);
    dsp->drawPixel(x, (int16_t)(cy + 5 + y));
    dsp->drawPixel(x, (int16_t)(cy + 6 + y));
  }
}
static void gSnow(int16_t cx, int16_t cy, float t) {
  for (uint8_t i = 0; i < 5; i++) {
    const float y = fmodf(t * 9.f + hsh(i) * 18.f, 18.f);
    const int16_t x = (int16_t)(cx - 7 + hsh((uint8_t)(i + 3)) * 15.f + sinf(t * 2.f + i) * 2.f);
    dsp->drawPixel(x, (int16_t)(cy + 5 + y));
  }
}
// A bolt that only strikes now and then, and briefly lights the whole glyph.
static bool gBolt(int16_t cx, int16_t cy, float t) {
  const float ph = fmodf(t, 3.4f);
  if (ph > 0.26f) return false;
  const bool on = ph < 0.08f || (ph > 0.14f && ph < 0.20f);
  if (!on) return false;
  int16_t x = (int16_t)(cx - 1), y = (int16_t)(cy + 4);
  for (uint8_t k = 0; k < 4; k++) {
    dsp->drawLine((uint16_t)x, (uint16_t)y, (uint16_t)(x + (k & 1 ? 3 : -3)), (uint16_t)(y + 3));
    x = (int16_t)(x + (k & 1 ? 3 : -3));
    y = (int16_t)(y + 3);
  }
  return true;
}
static void gFog(int16_t cx, int16_t cy, float t) {
  for (uint8_t i = 0; i < 4; i++) {
    const int16_t y = (int16_t)(cy - 4 + i * 4);
    const int16_t off = (int16_t)(sinf(t * 0.7f + i) * 4.f);
    dsp->drawHLine((int16_t)(cx - 9 + off), y, (uint8_t)(12 + (i & 1) * 5));
  }
}

// A 7px version for forecast rows.
static void miniGlyph(int16_t cx, int16_t cy, const char *icon, float t) {
  const bool night = has(icon, "night");
  if (has(icon, "thunder")) {
    dsp->drawDisc((int16_t)(cx - 2), cy, 2); dsp->drawDisc((int16_t)(cx + 2), cy, 2);
    if (fmodf(t, 2.2f) < 0.16f) {
      dsp->drawLine((uint16_t)cx, (uint16_t)(cy + 3), (uint16_t)(cx - 2), (uint16_t)(cy + 5));
      dsp->drawLine((uint16_t)(cx - 2), (uint16_t)(cy + 5), (uint16_t)(cx + 1), (uint16_t)(cy + 7));
    }
  } else if (has(icon, "rain") || has(icon, "sleet")) {
    dsp->drawDisc((int16_t)(cx - 2), cy, 2); dsp->drawDisc((int16_t)(cx + 2), cy, 2);
    for (uint8_t i = 0; i < 3; i++) {
      const float y = fmodf(t * 14.f + i * 3.f, 6.f);
      dsp->drawPixel((int16_t)(cx - 3 + i * 3), (int16_t)(cy + 3 + y));
    }
  } else if (has(icon, "snow")) {
    dsp->drawDisc((int16_t)(cx - 2), cy, 2); dsp->drawDisc((int16_t)(cx + 2), cy, 2);
    for (uint8_t i = 0; i < 3; i++)
      dsp->drawPixel((int16_t)(cx - 3 + i * 3), (int16_t)(cy + 4 + (int16_t)fmodf(t * 6.f + i * 2.f, 4.f)));
  } else if (has(icon, "cloud")) {
    dsp->drawDisc((int16_t)(cx - 2), cy, 2); dsp->drawDisc((int16_t)(cx + 2), (int16_t)(cy - 1), 2);
    if (has(icon, "partly")) {
      if (night) { dsp->drawCircle((int16_t)(cx + 5), (int16_t)(cy - 4), 2); }
      else for (uint8_t a = 0; a < 4; a++) {
        const float an = a * 1.5708f + t * 0.5f;
        dsp->drawPixel((int16_t)(cx + 5 + cosf(an) * 4), (int16_t)(cy - 4 + sinf(an) * 4));
      }
    }
  } else if (night) {
    dsp->drawDisc(cx, cy, 3); dsp->setDrawColor(0);
    dsp->drawDisc((int16_t)(cx + 2), (int16_t)(cy - 1), 3); dsp->setDrawColor(1);
  } else {
    dsp->drawDisc(cx, cy, 2);
    for (uint8_t a = 0; a < 4; a++) {
      const float an = a * 1.5708f + t * 0.5f;
      dsp->drawPixel((int16_t)(cx + cosf(an) * 5), (int16_t)(cy + sinf(an) * 5));
    }
  }
}

// Draws whatever the sky is doing, animated. Returns true if it flashed.
static bool weatherGlyph(int16_t cx, int16_t cy, const char *icon, float t) {
  const bool night = has(icon, "night");
  bool flashed = false;
  if (has(icon, "thunder")) {
    gCloud(cx, (int16_t)(cy - 3), t, 3.f);
    flashed = gBolt(cx, cy, t);
    gRain(cx, (int16_t)(cy - 1), t, 4);
  } else if (has(icon, "rain") || has(icon, "sleet")) {
    gCloud(cx, (int16_t)(cy - 3), t, 2.f);
    gRain(cx, (int16_t)(cy - 1), t, 6);
  } else if (has(icon, "snow")) {
    gCloud(cx, (int16_t)(cy - 3), t, 1.5f);
    gSnow(cx, (int16_t)(cy - 1), t);
  } else if (has(icon, "fog")) {
    gFog(cx, cy, t);
  } else if (has(icon, "partly")) {
    if (night) gMoon((int16_t)(cx - 4), (int16_t)(cy - 5), 5, t);
    else       gSun((int16_t)(cx - 4), (int16_t)(cy - 5), 4, t);
    gCloud((int16_t)(cx + 3), (int16_t)(cy + 3), t, 2.f);
  } else if (has(icon, "cloudy")) {
    gCloud(cx, (int16_t)(cy - 2), t, 2.f);
    gCloud((int16_t)(cx + 2), (int16_t)(cy + 4), t, 1.2f);
  } else if (has(icon, "wind")) {
    for (uint8_t i = 0; i < 4; i++) {
      const float x = fmodf(t * 26.f + i * 9.f, 34.f);
      dsp->drawHLine((int16_t)(cx - 15 + x), (int16_t)(cy - 6 + i * 4), (uint8_t)(5 + (i & 1) * 4));
    }
  } else {
    if (night) gMoon(cx, cy, 6, t);
    else       gSun(cx, cy, 5, t);
  }
  return flashed;
}

// The one big figure, with its unit tucked beside it.
static void hero(int16_t ox, const char *v, const char *unit, int16_t baseline) {
  dsp->setFont(u8g2_font_logisoso24_tr);
  dsp->drawStr((int16_t)(ox + 4), baseline, v);
  const uint8_t w = dsp->getStrWidth(v);
  dsp->setFont(u8g2_font_4x6_tr);
  if (unit && *unit) dsp->drawStr((int16_t)(ox + 7 + w), baseline, unit);
}
static void row(int16_t ox, int16_t y, const char *k, const char *v) {
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 74), y, k);
  rightStr((int16_t)(ox + 124), y, v);
}

static Tween tA;
static uint8_t lastPage = 255;

// ---- 0 now ---------------------------------------------------------------
static void pgNow(int16_t ox, float t) {
  char b[26];
  snprintf(b, sizeof b, "%d", (int)(tA.v + 0.5f));
  hero(ox, b, "F", 40);
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 4), 50, wx.cond);
  snprintf(b, sizeof b, "%d.%02d\" %s", (int)inHg(wx.mb), (int)(inHg(wx.mb) * 100) % 100,
           wx.tr < 0 ? "falling" : (wx.tr > 0 ? "rising" : "steady"));
  dsp->drawStr((int16_t)(ox + 4), 58, b);

  // The sky, animated, top right.
  weatherGlyph((int16_t)(ox + 100), 30, wx.icon, t);

  snprintf(b, sizeof b, "%dF", (int)(cF(wx.fl) + 0.5f));
  row(ox, 46, "feels", b);
  snprintf(b, sizeof b, "%dF", (int)(cF(wx.dc) + 0.5f));
  row(ox, 54, "dew", b);
  snprintf(b, sizeof b, "%u%%", wx.rh);
  row(ox, 62, "hum", b);
}

// ---- 1 wind --------------------------------------------------------------
static void pgWind(int16_t ox, float t) {
  char b[22];
  snprintf(b, sizeof b, "%d", (int)(tA.v + 0.5f));
  hero(ox, b, "mph", 44);
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 4), 54, wx.wm < 0.3f ? "calm" : wx.wc);
  // Streaks crossing the panel at the measured speed. Calm draws nothing.
  if (wx.wm >= 0.3f) {
    const float sp = 12.f + mph(wx.wm) * 5.f;
    for (uint8_t i = 0; i < 5; i++) {
      const float x = fmodf(t * sp + i * 27.f, 150.f) - 20.f;
      const int16_t y = (int16_t)(20 + i * 9);
      const uint8_t len = (uint8_t)(6 + (i & 1) * 6);
      dsp->drawHLine((int16_t)(ox + x), y, len);
      if (mph(wx.gm) > mph(wx.wm) + 4.f)
        dsp->drawPixel((int16_t)(ox + x + len + 2), y);
    }
  }
  // Bearing as a single line from a point - no dial, no ticks.
  const int16_t cx = (int16_t)(ox + 100), cy = 38;
  const float a = (wx.wd - 90) * 0.01745f;
  dsp->drawCircle(cx, cy, 2);
  if (wx.wm >= 0.3f) {
    dsp->drawLine((uint16_t)cx, (uint16_t)cy,
                  (uint16_t)(cx + cosf(a) * 13), (uint16_t)(cy + sinf(a) * 13));
    const float p = fmodf(t * 0.5f, 1.f);
    dsp->drawPixel((int16_t)(cx + cosf(a) * (4 + p * 9)), (int16_t)(cy + sinf(a) * (4 + p * 9)));
  }
  snprintf(b, sizeof b, "%d", (int)(mph(wx.gm) + 0.5f));
  row(ox, 26, "gust", b);
  snprintf(b, sizeof b, "%u", wx.wd);
  row(ox, 57, "bearing", b);
}

// ---- 2 sky ---------------------------------------------------------------
static void pgSky(int16_t ox, float t) {
  char b[22];
  snprintf(b, sizeof b, "%d.%d", (int)wx.uv, (int)(wx.uv * 10) % 10);
  hero(ox, b, "uv", 44);
  dsp->setFont(u8g2_font_4x6_tr);
  const bool day = wx.sr > 12.f;
  dsp->drawStr((int16_t)(ox + 4), 54, day ? "daylight" : "after dark");
  // A thin arc with the sun's height set by measured irradiance.
  const int16_t cx = (int16_t)(ox + 98), cy = 46;
  for (float a = 3.1416f; a <= 6.2832f; a += 0.16f)
    dsp->drawPixel((int16_t)(cx + cosf(a) * 20), (int16_t)(cy + sinf(a) * 18));
  if (day) {
    const float h = clamp01(wx.sr / 850.f);
    const float a = 3.1416f + (0.15f + 0.7f * h) * 3.1416f;
    dsp->drawDisc((int16_t)(cx + cosf(a) * 20), (int16_t)(cy + sinf(a) * 18), 2);
  } else if (pulse(t * 1000.f, 2600.f) > 0.5f) {
    dsp->drawPixel((int16_t)(cx - 20), cy);
  }
  snprintf(b, sizeof b, "%dW", (int)wx.sr);
  row(ox, 26, "solar", b);
  snprintf(b, sizeof b, "%uk", (unsigned)(wx.bri / 1000));
  row(ox, 35, "lux", b);
}

// ---- 3 storm -------------------------------------------------------------
static void pgStorm(int16_t ox, float t) {
  char b[22];
  snprintf(b, sizeof b, "%u", wx.s1);
  hero(ox, b, "1hr", 44);
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 4), 54, wx.s1 ? "strikes" : "no strikes");
  snprintf(b, sizeof b, "%u", wx.s3);
  row(ox, 26, "3hr", b);
  if (wx.skm > 0.f) { snprintf(b, sizeof b, "%dmi", (int)(mi(wx.skm) + 0.5f)); row(ox, 35, "nearest", b); }
  snprintf(b, sizeof b, "%u%%", wx.pp);
  row(ox, 44, "rain odds", b);
  snprintf(b, sizeof b, "%d.%02d\"", (int)in_(wx.rnd), (int)(in_(wx.rnd) * 100) % 100);
  row(ox, 53, "today", b);
  if (wx.s1) {
    // A bolt in the corner, striking now and then, with a brief flash behind it.
    const int16_t bx = (int16_t)(ox + 102), by = 24;
    const float ph = fmodf(t, 2.8f);
    if (ph < 0.2f) {
      int16_t x = bx, y = by;
      for (uint8_t k = 0; k < 4; k++) {
        dsp->drawLine((uint16_t)x, (uint16_t)y,
                      (uint16_t)(x + (k & 1 ? 4 : -4)), (uint16_t)(y + 4));
        x = (int16_t)(x + (k & 1 ? 4 : -4));
        y = (int16_t)(y + 4);
      }
      if (ph < 0.05f && mi(wx.skm) < 12.f) {
        dsp->setDrawColor(2);
        dsp->drawBox((int16_t)(ox + 88), 18, 34, 26);
        dsp->setDrawColor(1);
      }
    }
  }
}

// ---- 4 forecast ----------------------------------------------------------
// Three rows on a 16px pitch. The conditions text was the longest thing here and
// made it cramped, so a glyph carries it instead.
static void pgFcst(int16_t ox, float t) {
  static const char *when[WX_DAYS] = { "today", "tomorrow", "then" };
  for (uint8_t i = 0; i < wx.nDay && i < WX_DAYS; i++) {
    const float a = clamp01((gAge - i * 0.10f) * 5.f);
    if (a <= 0.f) continue;
    const int16_t y = (int16_t)(26 + i * 16);
    dsp->setFont(u8g2_font_4x6_tr);
    dsp->drawStr((int16_t)(ox + 4), y, when[i]);
    char b[14];
    snprintf(b, sizeof b, "%d/%d", (int)(cF(wx.day[i].hi) + 0.5f), (int)(cF(wx.day[i].lo) + 0.5f));
    dsp->setFont(u8g2_font_6x10_tf);
    dsp->drawStr((int16_t)(ox + 40), y, b);
    miniGlyph((int16_t)(ox + 92), (int16_t)(y - 4), wx.day[i].icon, t);
    dsp->setFont(u8g2_font_4x6_tr);
    snprintf(b, sizeof b, "%u%%", wx.day[i].pp);
    rightStr((int16_t)(ox + 124), y, b);
    if (i < 2) {
      const uint8_t w = (uint8_t)(116 * easeOutCubic(a));
      for (int16_t x = 0; x < w; x += 3) dsp->drawPixel((int16_t)(ox + 4 + x), (int16_t)(y + 5));
    }
  }
}

// ---- 5 trend -------------------------------------------------------------
static void pgTrend(int16_t ox, float t) {
  char b[22];
  snprintf(b, sizeof b, "%d", (int)(cF(wx.tc) + 0.5f));
  dsp->setFont(u8g2_font_logisoso16_tn);
  dsp->drawStr((int16_t)(ox + 4), 34, b);
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 30), 34, "F now");
  if (wx.nHist < 2) { dsp->drawStr((int16_t)(ox + 4), 50, "building history"); return; }
  int8_t lo = 127, hi = -128;
  for (uint8_t i = 0; i < wx.nHist; i++) { if (wx.histT[i] < lo) lo = wx.histT[i]; if (wx.histT[i] > hi) hi = wx.histT[i]; }
  if (hi - lo < 2) { hi = (int8_t)(lo + 2); }
  int16_t prev = 0;
  for (uint8_t i = 0; i < wx.nHist; i++) {
    const int16_t x = (int16_t)(ox + 4 + (i * 116) / (wx.nHist > 1 ? wx.nHist - 1 : 1));
    const int16_t y = (int16_t)(58 - ((wx.histT[i] - lo) * 16) / (hi - lo));
    if (i) dsp->drawLine((uint16_t)prev, (uint16_t)(58 - ((wx.histT[i - 1] - lo) * 16) / (hi - lo)),
                         (uint16_t)x, (uint16_t)y);
    prev = x;
  }
  dsp->drawDisc((int16_t)prev, (int16_t)(58 - ((wx.histT[wx.nHist - 1] - lo) * 16) / (hi - lo)),
                (uint8_t)(1 + (pulse(t * 1000.f, 1500.f) > 0.5f ? 1 : 0)));
  snprintf(b, sizeof b, "%dF", (int)(cF(hi) + 0.5f));
  rightStr((int16_t)(ox + 124), 44, b);
  snprintf(b, sizeof b, "%dF", (int)(cF(lo) + 0.5f));
  rightStr((int16_t)(ox + 124), 60, b);
  for (int16_t x = 0; x < 116; x += 3) dsp->drawPixel((int16_t)(ox + 4 + x), 60);
}

const char *wxPageName(uint8_t i) {
  static const char *n[WX_PAGES] = { "NOW", "WIND", "SKY", "STORM", "FORECAST", "TREND" };
  return n[i % WX_PAGES];
}

void wxPage(uint8_t idx, int16_t ox, float t) {
  if (idx != lastPage) {
    lastPage = idx;
    if (idx == 0) tA.snap(cF(wx.tc));
    else if (idx == 1) tA.snap(mph(wx.wm));
  }
  tA.rate = 4.f;
  if (idx == 0) tA.set(cF(wx.tc));
  else if (idx == 1) tA.set(mph(wx.wm));
  tA.update(0.02f);

  switch (idx) {
    case 0: pgNow(ox, t);   break;
    case 1: pgWind(ox, t);  break;
    case 2: pgSky(ox, t);   break;
    case 3: pgStorm(ox, t); break;
    case 4: pgFcst(ox, t);  break;
    default: pgTrend(ox, t); break;
  }
}
