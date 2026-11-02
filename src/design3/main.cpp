// Design 3 - "Sky over Rack". See ui.h for the concept.
#include <Arduino.h>
#include "ui.h"
#include "common/anim.h"

static const uint32_t FRAME_MS    = 20;
static const uint32_t BOOT_MS     = 1900;
static const uint32_t TAKEOVER_MS = 15000;
static const uint32_t TAKE_COOLDOWN = 45000;   // don't nag about the same thing

enum Phase { P_BOOT, P_LIVE, P_TAKEOVER };
static Phase     phase = P_BOOT;
static uint32_t  phaseStart = 0;
static EventKind current = EV_NONE, lastShown = EV_NONE;
static uint32_t  lastTakeoverEnd = 0;
static uint32_t  flashUntil = 0;
static float     lastStrikeSeen = 0;

// Horizon drops in from the top on boot: the sky arrives, then the rack.
static void drawBoot(float p, float t) {
  const float e = easeOutCubic(clamp01(p / 0.55f));
  dsp->setClipWindow(0, 0, SCR_W, (int16_t)(1 + e * 16.f));
  skyDraw(t, false);
  dsp->setMaxClipWindow();

  if (p > 0.40f) {
    const float q = easeOutCubic(clamp01((p - 0.40f) / 0.45f));
    dsp->setClipWindow(0, RACK_Y0, SCR_W, (int16_t)(RACK_Y0 + q * (64 - RACK_Y0)));
    rackDraw(t);
    dsp->setMaxClipWindow();
  }
  // The boundary draws itself in as a sweep.
  dsp->drawHLine(0, SKY_Y1, (uint8_t)(clamp01(p / 0.5f) * 128.f));
}

void setup() {
  Serial.begin(115200);
  delay(120);
  Serial.println("\n[fob] design3 sky-over-rack booting");
  oledBegin(600000);
  dataInit();
  weatherBegin();
  phaseStart = millis();
  Serial.printf("[fob] up, free heap %u\n", ESP.getFreeHeap());
}

void loop() {
  static uint32_t lastFrame = 0, beat = 0, frames = 0;
  const uint32_t now = millis();
  if (now - lastFrame < FRAME_MS) { delay(1); return; }
  float dt = (now - lastFrame) / 1000.f;
  if (dt > 0.08f) dt = 0.08f;
  lastFrame = now;
  const float t = now / 1000.f;

  dataUpdate(now, dt);
  weatherUpdate(dt);

  // A strike inverts the whole display for a couple of frames, the moment the
  // packet lands. This is the one thing on the fob that is genuinely startling,
  // and it should be, because it means lightning just hit nearby.
  if (wx.strikeLastMs && (float)wx.strikeLastMs != lastStrikeSeen) {
    lastStrikeSeen = (float)wx.strikeLastMs;
    flashUntil = now + 90;
  }

  frames++;
  if (now - beat > 5000) {
    beat = now;
    Serial.printf("[fob] alive fps=%u heap=%u wx=%s wind=%.1fmph %dF rain=%d ev=%d\n",
                  (unsigned)(frames / 5), ESP.getFreeHeap(), wx.live ? "LIVE" : "mock",
                  msToMph(wx.windMs), (int)cToF(wx.tempC), wx.raining ? 1 : 0, (int)current);
    frames = 0;
  }

  if (phase == P_BOOT) {
    if (now - phaseStart >= BOOT_MS) { phase = P_LIVE; phaseStart = now; }
  } else if (phase == P_LIVE) {
    const EventKind k = eventPick();
    // Re-show only after a cooldown, or immediately if it is a different event.
    if (k != EV_NONE && (k != lastShown || now - lastTakeoverEnd > TAKE_COOLDOWN)) {
      current = k; lastShown = k;
      phase = P_TAKEOVER; phaseStart = now;
    }
  } else {
    if (now - phaseStart >= TAKEOVER_MS) {
      phase = P_LIVE; phaseStart = now; lastTakeoverEnd = now;
    }
  }

  dsp->clearBuffer();
  if (phase == P_BOOT) {
    drawBoot((now - phaseStart) / (float)BOOT_MS, t);
  } else if (phase == P_TAKEOVER) {
    eventDraw(current, t, (now - phaseStart) / 1000.f);
  } else {
    skyDraw(t, false);
    dsp->drawHLine(0, SKY_Y1, SCR_W);  // horizon
    rackDraw(t);
  }

  if (now < flashUntil) {
    dsp->setDrawColor(2);
    dsp->drawBox(0, 0, SCR_W, SCR_H);
    dsp->setDrawColor(1);
  }
  dsp->sendBuffer();
}
