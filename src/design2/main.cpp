// Design 2 - "auto-focus hero". See ui.h for the concept.
#include <Arduino.h>
#include "ui.h"
#include "common/anim.h"

static const uint32_t FRAME_MS   = 20;
static const uint32_t BOOT_MS    = 2000;
static const uint32_t MORPH_MS   = 620;    // out, then in
static const uint32_t MIN_DWELL  = 3500;   // long enough to read
static const uint32_t MAX_DWELL  = 11000;  // force a change so it never stagnates
static const uint32_t DRIFT_MS   = 37000;  // burn-in nudge

enum Phase { P_BOOT, P_LIVE };
static Phase    phase = P_BOOT;
static uint32_t phaseStart = 0;

static uint8_t  hero = 0, heroNext = 0;
static uint32_t heroSince = 0;
static bool     morphing = false;
static uint32_t morphStart = 0;

// The hero region and a metric's slot in the strip: the morph interpolates
// between them, which is what makes the promotion legible.
struct Rect { int16_t x0, y0, x1, y1; };
static Rect heroRect() { return { (int16_t)gJx, (int16_t)(17 + gJy),
                                  (int16_t)(127 + gJx), (int16_t)(47 + gJy) }; }
static Rect tickRect(uint8_t m) {
  const int16_t x = stripTickX(m);
  return { (int16_t)(x - 1), (int16_t)(STRIP_BASE - STRIP_H + gJy),
           (int16_t)(x + 4), (int16_t)(STRIP_BASE + 1 + gJy) };
}
static Rect lerpRect(const Rect &a, const Rect &b, float k) {
  return { (int16_t)(a.x0 + (b.x0 - a.x0) * k), (int16_t)(a.y0 + (b.y0 - a.y0) * k),
           (int16_t)(a.x1 + (b.x1 - a.x1) * k), (int16_t)(a.y1 + (b.y1 - a.y1) * k) };
}

// k=0 fully collapsed into the tick, k=1 fully open.
static void drawHeroMorph(uint8_t which, float k, float t) {
  const Rect r = lerpRect(tickRect(which), heroRect(), k);
  if (r.x1 <= r.x0 || r.y1 <= r.y0) return;
  dsp->setClipWindow(r.x0, r.y0, (int16_t)(r.x1 + 1), (int16_t)(r.y1 + 1));
  d2Hero(which, t);
  dsp->setMaxClipWindow();
  if (k < 0.98f)
    dsp->drawFrame(r.x0, r.y0, (uint8_t)(r.x1 - r.x0 + 1), (uint8_t)(r.y1 - r.y0 + 1));
}

static void drawBoot(float p, float t) {
  d2Strip(hero, t, clamp01(p / 0.45f));           // spectrum waves in
  if (p > 0.30f) {                                 // scanline reveals the chrome
    const float q = clamp01((p - 0.30f) / 0.42f);
    const int16_t sx = (int16_t)(q * 128.f);
    if (sx > 0) {
      dsp->setClipWindow(0, 0, sx, 16);
      d2TopBar(hero, t);
      dsp->setMaxClipWindow();
      dsp->setClipWindow(0, 56, sx, 64);
      d2GroupLabels();
      dsp->setMaxClipWindow();
    }
    if (q < 1.f) dsp->drawVLine(sx, 0, 64);
  }
  if (p > 0.70f)                                   // hero unfolds from its tick
    drawHeroMorph(hero, easeOutCubic(clamp01((p - 0.70f) / 0.30f)), t);
}

// Forced rotation picks the best metric that is NOT the current hero.
static uint8_t bestOther(uint8_t cur) {
  uint8_t best = (uint8_t)((cur + 1) % METRIC_COUNT);
  float   bs = -1.f;
  for (uint8_t m = 0; m < METRIC_COUNT; m++)
    if (m != cur && gM[m].score > bs) { bs = gM[m].score; best = m; }
  return best;
}

void setup() {
  Serial.begin(115200);
  delay(120);
  Serial.println("\n[fob] design2 auto-focus hero booting");
  oledBegin(600000);                 // drop to 400000 if the panel glitches
  dataInit();
  metricsInit();
  metricsUpdate(0.f);
  hero = heroNext = metricsPick(0, true);
  phaseStart = heroSince = millis();
  Serial.printf("[fob] up, free heap %u\n", ESP.getFreeHeap());
}

void loop() {
  static uint32_t lastFrame = 0, lastDrift = 0, beat = 0, frames = 0;
  const uint32_t now = millis();
  if (now - lastFrame < FRAME_MS) { delay(1); return; }
  float dt = (now - lastFrame) / 1000.f;
  if (dt > 0.08f) dt = 0.08f;
  lastFrame = now;
  const float t = now / 1000.f;

  dataUpdate(now, dt);
  metricsUpdate(dt);

  // Whichever hero is actually on screen this frame is the tween target, so
  // the odometer rolls across a hero change instead of jumping.
  const float morphP = morphing ? clamp01((now - morphStart) / (float)MORPH_MS) : 0.f;
  const uint8_t drawnHero = (morphing && morphP >= 0.5f) ? heroNext : hero;
  d2Animate(dt, drawnHero);

  // Slow 1px nudge: this panel is always showing the same composition, and a
  // mono OLED will burn it in. Invisible in use, and the strip hides it.
  if (now - lastDrift > DRIFT_MS) {
    lastDrift = now;
    static uint8_t step = 0;
    step = (uint8_t)((step + 1) & 3);
    gJx = (step == 1 || step == 2) ? 1 : 0;
    gJy = (step == 2 || step == 3) ? 1 : 0;
  }

  frames++;
  if (now - beat > 5000) {
    beat = now;
    Serial.printf("[fob] alive fps=%u heap=%u hero=%s score=%d.%02d\n",
                  (unsigned)(frames / 5), ESP.getFreeHeap(), gM[hero].label,
                  (int)gM[hero].score, (int)(gM[hero].score * 100) % 100);
    frames = 0;
  }

  if (phase == P_BOOT) {
    if (now - phaseStart >= BOOT_MS) { phase = P_LIVE; phaseStart = now; heroSince = now; }
  } else if (!morphing) {
    const uint32_t held = now - heroSince;
    uint8_t want = hero;
    if (held > MAX_DWELL)      want = bestOther(hero);
    else if (held > MIN_DWELL) want = metricsPick(hero, true);
    if (want != hero) { heroNext = want; morphing = true; morphStart = now; }
  }

  dsp->clearBuffer();

  if (phase == P_BOOT) {
    drawBoot((now - phaseStart) / (float)BOOT_MS, t);
  } else {
    d2TopBar(morphing ? hero : hero, t);
    d2Strip(morphing ? heroNext : hero, t, 1.f);
    d2GroupLabels();

    if (morphing) {
      const float p = morphP;
      if (p < 0.5f) {
        // Old hero folds back down into its own tick...
        drawHeroMorph(hero, 1.f - easeInOutCubic(p * 2.f), t);
      } else {
        // ...and the new one unfolds out of its tick.
        drawHeroMorph(heroNext, easeOutCubic((p - 0.5f) * 2.f), t);
      }
      // One frame of invert at the handover, so a promotion is punctuated.
      if (fabsf(p - 0.5f) < 0.045f) {
        dsp->setDrawColor(2);
        dsp->drawBox(0, (int16_t)(17 + gJy), 128, 31);
        dsp->setDrawColor(1);
      }
      if (p >= 1.f) { hero = heroNext; morphing = false; heroSince = now; }
    } else {
      d2Hero(hero, t);
    }
  }

  dsp->sendBuffer();
}
