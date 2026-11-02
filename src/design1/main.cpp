// Homelab status fob - HW-364A (ESP8266 + soldered 0.96" SSD1306 128x64).
//
// The OLED on this board is NOT on the default I2C pins, and the HW-364A and
// HW-364B revisions swap SDA/SCL, so the pin order is probed at boot instead
// of hardcoded. Serial (115200) reports what was found.
#include <Arduino.h>
#include "ui.h"
#include "common/anim.h"

float gCardAge = 0.f;

static const uint32_t BOOT_MS   = 2100;
static const uint32_t REVEAL_MS = 300;
static const uint32_t DWELL_MS  = 6200;    // auto-advance
static const uint32_t SLIDE_MS  = 520;     // card transition
// There is no button on this board, so the rest face cannot be input-driven or
// it would park there forever: cards run for IDLE_AFTER, rest for IDLE_FOR.
static const uint32_t IDLE_AFTER = 300000; // 5 min of cards
static const uint32_t IDLE_FOR   = 45000;  // then 45 s of rest
static const uint8_t  BTN_PIN   = 0;       // FLASH button if one is ever added
static const uint32_t FRAME_MS  = 20;

enum Phase { P_BOOT, P_REVEAL, P_CARDS, P_IDLE };
static Phase    phase = P_BOOT;
static uint32_t phaseStart = 0;

static uint8_t  cur = 0, nxt = 0;
static bool     sliding = false;
static int8_t   slideDir = 1;
static uint32_t slideStart = 0, lastAdvance = 0, lastInput = 0;
static bool     autoRotate = true;

// ---- input ---------------------------------------------------------------
static void advance(int8_t dir) {
  if (sliding) return;
  slideDir = dir;
  nxt = (uint8_t)((cur + CARD_COUNT + dir) % CARD_COUNT);
  sliding = true;
  slideStart = millis();
  lastAdvance = slideStart;
}

static void handleButton(uint32_t now) {
  static bool     down = false;
  static uint32_t downAt = 0;
  const bool pressed = digitalRead(BTN_PIN) == LOW;

  if (pressed && !down) {
    down = true;
    downAt = now;
  } else if (!pressed && down) {
    down = false;
    lastInput = now;
    const uint32_t held = now - downAt;
    if (held < 40) return;                       // debounce
    if (phase == P_IDLE) {                       // first press just wakes it
      phase = P_CARDS;
      phaseStart = now;
      gCardAge = 0.f;
      dsp->setContrast(255);
      return;
    }
    if (held > 600) {
      autoRotate = !autoRotate;
      Serial.printf("[fob] auto-rotate %s\n", autoRotate ? "on" : "off");
    } else {
      advance(1);
    }
  }
}

// ---- compose -------------------------------------------------------------
// Slide by memmoving the card band's pages instead of drawing cards at a
// negative x. u8g2's primitives all take u8g2_uint_t: a negative coordinate
// wraps to ~65500, which makes text and boxes silently vanish and makes
// drawLine's unbounded Bresenham loop run for seconds (it hung mid-slide on
// the SLERM sparkline). The band is pages 2..6 exactly, so this is one
// memmove per page and is pixel-exact.
static void shiftCardBandLeft(uint8_t n) {
  if (!n) return;
  uint8_t *buf = dsp->getBufferPtr();
  for (uint8_t page = CARD_PAGE0; page <= CARD_PAGE1; page++) {
    uint8_t *row = buf + (uint16_t)page * SCR_W;
    if (n >= SCR_W) { memset(row, 0, SCR_W); continue; }
    memmove(row, row + n, (size_t)(SCR_W - n));
    memset(row + (SCR_W - n), 0, n);
  }
}

static void drawUI(float t) {
  float pos = cur;

  // Cards first and alone in the buffer, so the page shift only moves them.
  clipSet(0, CARD_Y0, SCR_W, CARD_Y1 + 1);
  if (sliding) {
    const float p = clamp01((millis() - slideStart) / (float)SLIDE_MS);
    const float e = easeInOutCubic(p);
    const uint16_t dx = (uint16_t)(e * (float)SCR_W);
    // Whichever card needs to sit at a negative x is drawn at 0 and shifted;
    // the other one is always at a non-negative offset.
    if (slideDir > 0) {
      drawCard(cur, 0, t);
      shiftCardBandLeft((uint8_t)dx);
      drawCard(nxt, (int16_t)(SCR_W - dx), t);
    } else {
      drawCard(nxt, 0, t);
      shiftCardBandLeft((uint8_t)(SCR_W - dx));
      drawCard(cur, (int16_t)dx, t);
    }
    pos = fmodf(cur + slideDir * e + CARD_COUNT, (float)CARD_COUNT);
    if (p >= 1.f) { cur = nxt; sliding = false; gCardAge = 0.f; }
  } else {
    drawCard(cur, 0, t);
  }
  clipFull();

  // Chrome after the shift so it is never dragged sideways.
  drawTopBar(t);
  drawDots(cur, pos);
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr(2, 63, cardName(sliding ? nxt : cur));
  if (!autoRotate) dsp->drawStr(SCR_W - 14, 63, "||");
}

void setup() {
  Serial.begin(115200);
  delay(120);
  Serial.println("\n[fob] homelab status fob booting");

  pinMode(BTN_PIN, INPUT_PULLUP);
  oledBegin(600000);                   // 400000 if the panel ever glitches

  dataInit();
  phaseStart = millis();
  lastAdvance = lastInput = phaseStart;
  Serial.printf("[fob] up, free heap %u\n", ESP.getFreeHeap());
}

void loop() {
  static uint32_t lastFrame = 0;
  const uint32_t now = millis();
  if (now - lastFrame < FRAME_MS) { delay(1); return; }

  float dt = (now - lastFrame) / 1000.f;
  if (dt > 0.08f) dt = 0.08f;          // first frame / any hiccup
  lastFrame = now;
  const float t = now / 1000.f;

  handleButton(now);
  dataUpdate(now, dt);

  // Heartbeat: if the loop ever stalls again this stops, which is visible.
  static uint32_t beat = 0, frames = 0;
  frames++;
  if (now - beat > 5000) {
    beat = now;
    Serial.printf("[fob] alive fps=%u heap=%u card=%s\n",
                  (unsigned)(frames / 5), ESP.getFreeHeap(), cardName(cur));
    frames = 0;
  }
  cardsUpdate(dt);

  const uint32_t inPhase = now - phaseStart;

  switch (phase) {
    case P_BOOT:
      if (inPhase >= BOOT_MS) { phase = P_REVEAL; phaseStart = now; }
      break;
    case P_REVEAL:
      if (inPhase >= REVEAL_MS) {
        phase = P_CARDS; phaseStart = now;
        gCardAge = 0.f; lastAdvance = lastInput = now;
      }
      break;
    case P_CARDS:
      gCardAge += dt;
      if (autoRotate && !sliding && now - lastAdvance > DWELL_MS) advance(1);
      if (now - lastInput > IDLE_AFTER) {
        phase = P_IDLE; phaseStart = now;
        dsp->setContrast(90);          // easier on the panel at rest
      }
      break;
    case P_IDLE:
      // Times out back to the cards by itself, and a new alert cuts it short.
      if (inPhase > IDLE_FOR) {
        phase = P_CARDS; phaseStart = now;
        sliding = false; gCardAge = 0.f;
        lastInput = lastAdvance = now;
        dsp->setContrast(255);
        break;
      }
      if (g.alerts && inPhase > 1500) {
        phase = P_CARDS; phaseStart = now;
        cur = 4; sliding = false; gCardAge = 0.f;
        lastInput = lastAdvance = now;
        dsp->setContrast(255);
      }
      break;
  }

  dsp->clearBuffer();
  switch (phase) {
    case P_BOOT:
      drawBoot(inPhase / (float)BOOT_MS);
      break;
    case P_REVEAL: {
      // The UI opens out of a hairline, like a CRT coming on.
      const float e = easeOutCubic(inPhase / (float)REVEAL_MS);
      const int16_t half = (int16_t)(e * 32.f) + 1;
      clipSet(0, (int16_t)(32 - half), SCR_W, (int16_t)(32 + half));
      drawUI(t);
      clipFull();
      break;
    }
    case P_CARDS:
      drawUI(t);
      break;
    case P_IDLE:
      drawIdle(t);
      break;
  }
  dsp->sendBuffer();
}
