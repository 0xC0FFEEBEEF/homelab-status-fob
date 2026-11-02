// The blue band: your rack drawn as terrain under the sky.
//
// Text at 4x6 on a 22mm panel is unreadable, and four rows of 6x10 is still a
// wall of text. So the rack is a LANDSCAPE instead: seven subsystems, seven
// peaks, each height its health. A low or jagged skyline reads as trouble from
// across the room without reading a single character, and an alarming peak
// grows a blinking beacon. One big auto-selected number floats over the
// terrain to name whatever actually matters right now.
#include "ui.h"
#include "common/anim.h"

static const int16_t GROUND   = 63;   // terrain baseline
static const uint8_t PEAK_MIN = 3;
static const uint8_t PEAK_MAX = 20;
static const int16_t HERO_B   = 40;   // big number baseline

static const uint8_t SUBSYS = 7;

struct Sub {
  const char *label;     // named in the hero line
  const char *unit;
  float value;           // what the hero shows
  float health;          // 0..1 -> peak height (1 = tall = good)
  float sev;             // 0..1 -> how bad
  bool  alarm;
  const char *state;     // shown instead of a number when there isn't one
};
static Sub sb[SUBSYS];
static Tween tPeak[SUBSYS];
static Tween tHero;
static bool  ready = false;

static uint8_t hero = 0, heroPrev = 0;
static uint32_t heroSince = 0, rollStart = 0;
static bool rolling = false;

static void buildSubs() {
  uint8_t nUp = 0;
  for (uint8_t i = 0; i < g.nodeCount; i++) if (g.nodes[i].up) nUp++;
  uint8_t sUp = 0;
  for (uint8_t i = 0; i < g.svcCount; i++) if (g.svcs[i].up) sUp++;
  const char *downSvc = nullptr;
  for (uint8_t i = 0; i < g.svcCount; i++) if (!g.svcs[i].up) { downSvc = g.svcs[i].name; break; }
  const char *downNode = nullptr;
  for (uint8_t i = 0; i < g.nodeCount; i++) if (!g.nodes[i].up) { downNode = g.nodes[i].name; break; }

  sb[0] = { downNode ? downNode : "slerm", downNode ? "" : "t/s", g.tokS,
            (float)nUp / g.nodeCount, 1.f - (float)nUp / g.nodeCount,
            nUp < g.nodeCount, downNode ? "DOWN" : nullptr };
  sb[1] = { "byteme", "%", (float)g.bmCpu, 1.f - g.bmCpu / 100.f,
            g.bmCpu / 100.f, g.bmCpu > 90, nullptr };
  sb[2] = { "byteme2", "%", (float)g.bm2Cpu, 1.f - g.bm2Cpu / 100.f,
            g.bm2Cpu / 100.f, g.bm2Cpu > 90, nullptr };
  sb[3] = { g.onBattery ? "ups batt" : "ups", g.onBattery ? "%" : "W",
            g.onBattery ? (float)g.battPct : (float)g.loadW,
            g.battPct / 100.f, g.onBattery ? 1.f : g.loadW / 500.f,
            g.onBattery, nullptr };
  sb[4] = { "wazuh", "", (float)g.alerts,
            1.f - clamp01(g.alerts / 8.f), clamp01(g.alerts / 6.f),
            g.sev[0] > 0, nullptr };
  sb[5] = { downSvc ? downSvc : "services", downSvc ? "" : "up",
            downSvc ? 0.f : (float)sUp, (float)sUp / g.svcCount,
            1.f - (float)sUp / g.svcCount, sUp < g.svcCount,
            downSvc ? "DOWN" : nullptr };
  sb[6] = { "wan", "ms", (float)g.wanMs, 1.f - clamp01(g.wanMs / 120.f),
            clamp01(g.wanMs / 120.f), !g.wanUp, g.wanUp ? nullptr : "OFFLINE" };
}

// Attention: broken beats bad beats busy. Hysteresis and a dwell floor keep the
// hero readable; a ceiling stops it parking on one thing forever.
static void pickHero(uint32_t now) {
  if (rolling) return;
  const uint32_t held = now - heroSince;
  if (held < 4000) return;

  uint8_t best = hero;
  float bs = (sb[hero].alarm ? 3.f : 0.f) + sb[hero].sev * 1.5f + 0.3f;
  for (uint8_t i = 0; i < SUBSYS; i++) {
    const float s = (sb[i].alarm ? 3.f : 0.f) + sb[i].sev * 1.5f;
    if (s > bs) { bs = s; best = i; }
  }
  if (held > 10000 && best == hero) {           // force some variety
    best = (uint8_t)((hero + 1) % SUBSYS);
  }
  if (best != hero) {
    heroPrev = hero; hero = best;
    rolling = true; rollStart = now; heroSince = now;
  }
}

// Smoothstep between peak centres, so the ridge is a landscape and not a bar
// chart with gaps.
static uint8_t ridgeAt(int16_t x) {
  const float step = (float)SCR_W / SUBSYS;
  const float fx = (x + 0.5f) / step - 0.5f;
  int   i0 = (int)floorf(fx);
  float f  = fx - i0;
  int   i1 = i0 + 1;
  if (i0 < 0) { i0 = 0; i1 = 0; f = 0.f; }
  if (i1 > SUBSYS - 1) { i0 = SUBSYS - 1; i1 = SUBSYS - 1; f = 0.f; }
  const float s = f * f * (3.f - 2.f * f);
  const float h = tPeak[i0].v + (tPeak[i1].v - tPeak[i0].v) * s;
  return (uint8_t)(PEAK_MIN + clamp01(h) * (PEAK_MAX - PEAK_MIN));
}

static void drawHeroNumber(uint8_t which, int16_t yOff) {
  const Sub &s = sb[which];
  if (s.state) {
    dsp->setFont(u8g2_font_logisoso16_tr);
    dsp->drawStr(2, (int16_t)(HERO_B - 3 + yOff), s.state);
  } else {
    char n[10];
    snprintf(n, sizeof n, "%d", (int)(tHero.v + 0.5f));
    dsp->setFont(u8g2_font_logisoso24_tn);
    dsp->drawStr(2, (int16_t)(HERO_B + yOff), n);
    const uint8_t w = dsp->getStrWidth(n);
    dsp->setFont(u8g2_font_6x10_tf);
    dsp->drawStr((int16_t)(4 + w), (int16_t)(HERO_B + yOff), s.unit);
  }
}

void rackDraw(float t) {
  const uint32_t now = millis();
  buildSubs();

  if (!ready) {
    ready = true;
    for (uint8_t i = 0; i < SUBSYS; i++) tPeak[i].snap(sb[i].health);
    tHero.snap(sb[hero].value);
    heroSince = now;
  }
  for (uint8_t i = 0; i < SUBSYS; i++) {
    tPeak[i].rate = 2.2f;                 // slow, so the land breathes
    tPeak[i].set(sb[i].health);
    tPeak[i].update(0.02f);
  }
  pickHero(now);
  tHero.rate = 4.5f;
  tHero.set(sb[hero].value);
  tHero.update(0.02f);

  // --- hero, rolling vertically on a change (no folding, no capture) -------
  float roll = 0.f;
  if (rolling) {
    const float p = clamp01((now - rollStart) / 320.f);
    roll = easeInOutCubic(p);
    if (p >= 1.f) rolling = false;
  }
  dsp->setClipWindow(0, 17, SCR_W, 43);
  if (rolling) {
    drawHeroNumber(heroPrev, (int16_t)(-roll * 26.f));
    drawHeroNumber(hero,     (int16_t)((1.f - roll) * 26.f));
  } else {
    drawHeroNumber(hero, 0);
  }
  dsp->setMaxClipWindow();

  // Label sits top-right of the hero zone, away from the digits.
  dsp->setFont(u8g2_font_6x10_tf);
  {
    const Sub &s = sb[hero];
    const uint8_t w = dsp->getStrWidth(s.label);
    dsp->drawStr((int16_t)(126 - w), 25, s.label);
    if (s.alarm && pulse(t * 1000.f, 900.f) > 0.4f) {
      dsp->drawFrame((int16_t)(124 - w), 16, (uint8_t)(w + 4), 11);
    }
  }
  dsp->drawStr(118, 40, wx.live ? "L" : "M");

  // --- terrain ------------------------------------------------------------
  for (int16_t x = 0; x < SCR_W; x++) {
    const uint8_t h = ridgeAt(x);
    const int16_t top = (int16_t)(GROUND - h + 1);
    dsp->drawPixel(x, top);                        // ridge line
    // 50% dither for the body: reads as solid ground, half the lit pixels.
    for (int16_t y = (int16_t)(top + 2); y <= GROUND; y++)
      if (((x + y) & 1) == 0) dsp->drawPixel(x, y);
  }

  // --- beacons on troubled peaks -----------------------------------------
  for (uint8_t i = 0; i < SUBSYS; i++) {
    if (!sb[i].alarm) continue;
    const int16_t cx = (int16_t)((i + 0.5f) * (SCR_W / (float)SUBSYS));
    const int16_t top = (int16_t)(GROUND - ridgeAt(cx) + 1);
    dsp->drawVLine(cx, (int16_t)(top - 5), 4);     // mast
    if (pulse(t * 1000.f, 700.f) > 0.35f) {
      dsp->drawDisc(cx, (int16_t)(top - 7), 1);    // blinking light
    }
  }
}
