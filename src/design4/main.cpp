// design4 - a page per thing, live from byteme2, with a run takeover.
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "ui.h"
#include "common/anim.h"
#include "common/secrets.h"

static const uint32_t FRAME_MS = 20;
static const uint32_t BOOT_MS  = 1900;
static const uint32_t DWELL_MS = 5200;
static const uint32_t SLIDE_MS = 460;

enum Phase { P_BOOT, P_NET, P_PAGES };
static Phase    phase = P_BOOT;
static uint32_t phaseStart = 0;
static bool     everOk = false;

static uint8_t  cur = 0, nxt = 0;
static bool     sliding = false;
static uint32_t slideStart = 0, lastAdvance = 0;
static uint8_t  lastVmCount = 255;
static bool     wasRun = false;
static uint32_t runSince = 0;

// Body occupies byte-pages 2..7 exactly, so a slide is a memmove per page and
// every coordinate stays non-negative.
static void shiftBodyLeft(uint8_t px) {
  if (!px) return;
  uint8_t *buf = dsp->getBufferPtr();
  for (uint8_t pg = BODY_PAGE0; pg <= BODY_PAGE1; pg++) {
    uint8_t *row = buf + (uint16_t)pg * SCR_W;
    if (px >= SCR_W) { memset(row, 0, SCR_W); continue; }
    memmove(row, row + px, (size_t)(SCR_W - px));
    memset(row + (SCR_W - px), 0, px);
  }
}

static bool replaying = false;

static void advance() {
  if (sliding || gPageCount < 2) return;
  nxt = (uint8_t)((cur + 1) % gPageCount);
  sliding = true;
  slideStart = millis();
  lastAdvance = slideStart;
}

static void header(float t, float dwellFrac) {
  dsp->setFont(u8g2_font_6x10_tf);
  dsp->drawStr(2, 12, pageTitle(gPages[sliding ? nxt : cur]));

  // Page position as ticks, so you know where you are in the rotation.
  const uint8_t shown = gPageCount > 16 ? 16 : gPageCount;
  for (uint8_t i = 0; i < shown; i++) {
    const int16_t x = (int16_t)(128 - shown * 3 + i * 3);
    if (i == (sliding ? nxt : cur)) dsp->drawBox(x, 3, 2, 5);
    else                           dsp->drawPixel(x, 5);
  }
  // Stale data warning: the fob should never quietly show old numbers.
  if (!fd.ok || millis() - fd.lastOkMs > 15000) {
    if (pulse(t * 1000.f, 900.f) > 0.5f) dsp->drawStr(108, 12, "!");
  }
  dsp->drawHLine(0, 15, (uint8_t)(dwellFrac * 128.f));
}

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println("\n[fob] design4 booting");
  oledBegin(600000);
  fobDataBegin();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  pagesRebuild();
  phaseStart = lastAdvance = millis();
}

void loop() {
  static uint32_t lastFrame = 0, beat = 0, frames = 0;
  const uint32_t now = millis();
  if (now - lastFrame < FRAME_MS) { delay(1); return; }
  float dt = (now - lastFrame) / 1000.f;
  if (dt > 0.08f) dt = 0.08f;
  lastFrame = now;
  const float t = now / 1000.f;

  fobDataUpdate();
  pagesSampleHistory();

  // GET http://192.0.2.2:8788/replay bumps rep.req: jump straight to the replay.
  static uint32_t seenReq = 0;
  static bool     reqInit = false;
  if (fd.ok && !reqInit) { reqInit = true; seenReq = fd.rep.req; }
  const bool posFresh = fd.pos.n > 1 && fd.pos.age <= 4;
  static bool wasPosFresh = false;
  const bool posEdge = posFresh && !wasPosFresh;
  wasPosFresh = posFresh;
  if (fd.ok && (fd.rep.req != seenReq || posEdge)) {
    seenReq = fd.rep.req;
    if (outageAvailable() && gPageCount < 19) {
      gPages[gPageCount] = { PG_REPLAY, 0 };
      cur = gPageCount++; sliding = false; gAge = 0.f;
      lastAdvance = now; outageBegin(); replaying = true;
      Serial.println("[fob] replay requested");
    }
  }
  if (fd.vmCount != lastVmCount) { lastVmCount = fd.vmCount; pagesRebuild(); }

  frames++;
  if (now - beat > 5000) {
    beat = now;
    Serial.printf("[fob] fps=%u heap=%u pages=%u ok=%u fail=%u run=%u/%s\n",
                  (unsigned)(frames / 5), ESP.getFreeHeap(), gPageCount,
                  fd.fetches, fd.failures, fd.run.active, fd.run.phase);
    frames = 0;
  }

  // A live run takes the screen and plays the outage animation from the top.
  const bool runNow = fd.seq.live != 0;
  if (runNow && !wasRun) { runSince = now; outageBegin(); }
  wasRun = runNow;

  dsp->clearBuffer();

  if (phase == P_BOOT) {
    bootDraw((now - phaseStart) / (float)BOOT_MS, t);
    if (now - phaseStart >= BOOT_MS) { phase = P_NET; phaseStart = now; }
  } else if (phase == P_NET) {
    static char ipbuf[18];
    strncpy(ipbuf, WiFi.localIP().toString().c_str(), sizeof ipbuf - 1);
    ipbuf[sizeof ipbuf - 1] = 0;
    const uint8_t st = (WiFi.status() != WL_CONNECTED) ? (everOk ? 2 : 0) : 1;
    netDraw(st, t, WIFI_SSID, ipbuf);
    if (fd.ok) { everOk = true; phase = P_PAGES; phaseStart = now; lastAdvance = now; }
  } else if (runNow) {
    outageDraw(t);
  } else if (everOk && WiFi.status() != WL_CONNECTED) {
    netDraw(2, t, WIFI_SSID, "reconnecting");
  } else {
    gAge += dt;
    const bool onReplay = gPages[cur].kind == PG_REPLAY && !sliding;
    if (onReplay && !replaying) { outageBegin(); replaying = true; }
    if (!onReplay) replaying = false;
    // The replay holds the rotation until it has played out.
    const bool hold = onReplay && !outageDone();
    if (onReplay && !hold) {
      // Played out: remove the page again so the rotation cannot come back to
      // a stale run.
      if (gPageCount && gPages[gPageCount - 1].kind == PG_REPLAY) gPageCount--;
      cur = 0; replaying = false; gAge = 0.f; lastAdvance = now;
    } else if (!sliding && !hold && now - lastAdvance > DWELL_MS) advance();

    if (sliding) {
      const float p = clamp01((now - slideStart) / (float)SLIDE_MS);
      const uint16_t dx = (uint16_t)(easeInOutCubic(p) * (float)SCR_W);
      pageDraw(gPages[cur], 0, t);
      shiftBodyLeft((uint8_t)dx);
      pageDraw(gPages[nxt], (int16_t)(SCR_W - dx), t);
      // A bright seam leads the incoming page, with a short trailing comb.
      const int16_t seam = (int16_t)(SCR_W - dx);
      if (seam > 0 && seam < SCR_W) {
        dsp->drawVLine(seam, BODY_Y0, (uint8_t)(64 - BODY_Y0));
        for (int16_t k = 2; k < 8; k += 2)
          if (seam + k < SCR_W)
            for (int16_t yy = BODY_Y0; yy < 64; yy += 3) dsp->drawPixel((int16_t)(seam + k), yy);
      }
      if (p >= 1.f) { cur = nxt; sliding = false; gAge = 0.f; }
    } else if (gPages[cur].kind == PG_REPLAY) {
      outageDraw(t);                       // draws its own header
    } else {
      pageDraw(gPages[cur], 0, t);
    }
    if (gPages[cur].kind != PG_REPLAY || sliding)
      header(t, gPages[cur].kind == PG_REPLAY ? outageProgress()
                                             : clamp01((now - lastAdvance) / (float)DWELL_MS));
  }

  dsp->sendBuffer();
}
