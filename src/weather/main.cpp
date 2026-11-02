// nbdevlab weather fob - reads the Tempest cloud API directly so it works on any
// WiFi, anywhere. If it cannot join a network it hosts an OPEN access point with
// a setup portal; a wrong password sends it straight back to hosting.
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include "ui.h"
#include "common/anim.h"
#include "common/secrets.h"

static const char *AP_NAME = "nbdevlab-weather";
static const uint32_t FRAME_MS  = 20;
static const uint32_t BOOT_MS   = 1700;
static const uint32_t DWELL_MS  = 8000;    // slow: this is meant to be pleasant
static const uint32_t SLIDE_MS  = 480;

static WiFiManager wm;
static bool     portalUp = false;
static uint32_t joinStart = 0;
// "The portal has been used" marker. Needed because the ESP SDK retains the last
// SSID it ever saw - this board still had a stale "Bratnet2" from a previous
// life, and any heuristic based on "something is stored" believes it.
static const uint8_t  CFG_ADDR  = 0;
static const uint8_t  CFG_MAGIC = 0xA5;
static bool           userConfigured = false;
static const uint32_t JOIN_GRACE = 22000;   // then host a portal instead

enum Phase { P_BOOT, P_NET, P_PAGES };
static Phase phase = P_BOOT;
static uint32_t phaseStart = 0;
static uint8_t  cur = 0, nxt = 0;
static bool     sliding = false;
static uint32_t slideStart = 0, lastAdvance = 0;
static uint8_t  contrast = 255;

// The page band is byte-pages 2..7, so a slide is a memmove per page and every
// coordinate stays non-negative (u8g2 takes unsigned coords).
static void shiftBodyLeft(uint8_t px) {
  if (!px) return;
  uint8_t *buf = dsp->getBufferPtr();
  for (uint8_t pg = 2; pg <= 7; pg++) {
    uint8_t *row = buf + (uint16_t)pg * SCR_W;
    if (px >= SCR_W) { memset(row, 0, SCR_W); continue; }
    memmove(row, row + px, (size_t)(SCR_W - px));
    memset(row + (SCR_W - px), 0, px);
  }
}

static void drawBoot(float p, float t) {
  static const char *word = "nbdevlab";
  const float e = easeOutCubic(clamp01(p / 0.4f));
  const int16_t half = (int16_t)(e * 60.f);
  dsp->drawHLine((int16_t)(64 - half), 30, (uint8_t)(half * 2));
  if (p > 0.25f) {
    dsp->setFont(u8g2_font_6x10_tf);
    const uint8_t full = dsp->getStrWidth(word);
    const uint8_t n = (uint8_t)(clamp01((p - 0.25f) / 0.35f) * strlen(word) + 0.5f);
    char buf[12];
    memset(buf, 0, sizeof buf);
    strncpy(buf, word, n);
    dsp->drawStr((int16_t)(64 - full / 2), 27, buf);
    if (n < strlen(word) && pulse(t * 1000.f, 240.f) > 0.5f)
      dsp->drawBox((int16_t)(64 - full / 2 + n * 6), 19, 5, 9);
  }
  if (p > 0.62f) {
    dsp->setFont(u8g2_font_4x6_tr);
    const char *s = "weather";
    eyebrow((int16_t)(64 - (strlen(s) * 5) / 2), 42, s);
  }
}

// Joining, or hosting a portal for someone to set it up.
static void drawNet(float t) {
  const int16_t cx = 22, cy = 34;
  dsp->drawVLine(cx, cy - 6, 12);
  dsp->drawBox((int16_t)(cx - 2), (int16_t)(cy + 6), 5, 2);
  for (uint8_t k = 0; k < 3; k++) {
    const float ph = fmodf(t * 0.9f + k * 0.333f, 1.f);
    if (ph > 0.85f) continue;
    const uint8_t r = (uint8_t)(4 + ph * 16.f);
    for (int16_t a = -50; a <= 50; a += 10) {
      const float rad = a * 0.01745f;
      dsp->drawPixel((int16_t)(cx + cosf(rad) * r), (int16_t)(cy - 6 + sinf(rad) * r));
    }
  }
  dsp->setFont(u8g2_font_4x6_tr);
  if (portalUp) {
    eyebrow(3, 8, "SETUP NEEDED");
    dsp->setFont(u8g2_font_6x10_tf);
    dsp->drawStr(46, 30, "join wifi");
    dsp->setFont(u8g2_font_4x6_tr);
    dsp->drawStr(46, 40, AP_NAME);
    dsp->drawStr(46, 48, "no password");
    dsp->drawStr(46, 58, "then 192.168.4.1");
  } else {
    eyebrow(3, 8, "JOINING");
    dsp->setFont(u8g2_font_6x10_tf);
    dsp->drawStr(46, 32, WiFi.SSID().length() ? WiFi.SSID().c_str() : "wifi");
    dsp->setFont(u8g2_font_4x6_tr);
    const uint8_t dots = (uint8_t)(fmodf(t * 2.f, 4.f));
    for (uint8_t i = 0; i < dots; i++) dsp->drawBox((int16_t)(46 + i * 5), 42, 3, 3);
  }
}

static void header(float t, float dwell) {
  eyebrow(3, 8, wxPageName(sliding ? nxt : cur));
  dsp->setFont(u8g2_font_4x6_tr);
  char b[30];
  if (!wx.ok)                          snprintf(b, sizeof b, "%s", wx.err[0] ? wx.err : "no data");
  else if (millis() - wx.lastOkMs > 420000) snprintf(b, sizeof b, "stale");
  else                                 snprintf(b, sizeof b, "%s", wx.loc);
  rightStr(124, 8, b);
  dsp->drawHLine(0, 13, (uint8_t)(clamp01(dwell) * 128.f));
}

void setup() {
  Serial.begin(115200);
  delay(120);
  Serial.println("\n[wx] nbdevlab weather booting");
  oledBegin(600000);
  wxBegin();

  // Credential choice. Use the compiled-in network unless someone has actually
  // saved credentials through the portal, which sets a magic byte in EEPROM.
  EEPROM.begin(4);
  userConfigured = EEPROM.read(CFG_ADDR) == CFG_MAGIC;
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  if (userConfigured) {
    Serial.printf("[wx] portal-configured, joining %s\n", WiFi.SSID().c_str());
    WiFi.begin();
  } else {
    Serial.printf("[wx] joining %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  joinStart = millis();

  wm.setConfigPortalBlocking(false);      // keep drawing while the portal runs
  wm.setConfigPortalTimeout(0);           // never give up and leave it dark
  wm.setSaveConfigCallback([]() {
    userConfigured = true;
    EEPROM.write(CFG_ADDR, CFG_MAGIC);
    EEPROM.commit();
    Serial.println("[wx] credentials saved via portal, will prefer them from now on");
  });

  phaseStart = lastAdvance = millis();
  Serial.printf("[wx] up, free heap %u\n", ESP.getFreeHeap());
}

void loop() {
  static uint32_t lastFrame = 0, beat = 0, frames = 0;
  const uint32_t now = millis();
  if (now - lastFrame < FRAME_MS) { wm.process(); delay(1); return; }
  float dt = (now - lastFrame) / 1000.f;
  if (dt > 0.08f) dt = 0.08f;
  lastFrame = now;
  const float t = now / 1000.f;

  if (portalUp) wm.process();
  wxUpdate();
  const bool linked = WiFi.status() == WL_CONNECTED;

  // Couldn't join in time: host an OPEN access point with the setup portal. A
  // wrong password lands back here, because the join simply fails again.
  if (!linked && !portalUp && now - joinStart > JOIN_GRACE) {
    portalUp = true;
    Serial.printf("[wx] no join in %lus, hosting open AP %s -> 192.168.4.1\n",
                  (unsigned long)(JOIN_GRACE / 1000), AP_NAME);
    wm.startConfigPortal(AP_NAME);        // no password, as asked
  }
  // Keep trying to join while the portal is up, so walking back into range of a
  // known network recovers on its own. This is meant to travel.
  static uint32_t lastRetry = 0;
  if (!linked && portalUp && now - lastRetry > 45000) {
    lastRetry = now;
    if (userConfigured) WiFi.begin();
    else                WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  if (linked && portalUp) {
    portalUp = false;
    wm.stopConfigPortal();
    Serial.printf("[wx] joined %s as %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
  }

  // Dim after dark so it is not glaring next to a bed.
  const uint8_t want = (wx.ok && wx.sr < 5.f && wx.bri < 200) ? 70 : 255;
  if (want != contrast) { contrast = want; dsp->setContrast(contrast); }

  frames++;
  if (now - beat > 10000) {
    beat = now;
    Serial.printf("[wx] fps=%u heap=%u low=%u ok=%lu fail=%lu %s %.0fF %s\n",
                  (unsigned)(frames / 10), ESP.getFreeHeap(), wxHeapLow(),
                  (unsigned long)wx.fetches, (unsigned long)wx.failures,
                  linked ? WiFi.localIP().toString().c_str() : (portalUp ? "PORTAL" : "joining"),
                  wx.tc * 1.8f + 32.f, wx.err);
    frames = 0;
  }

  dsp->clearBuffer();
  if (phase == P_BOOT) {
    drawBoot((now - phaseStart) / (float)BOOT_MS, t);
    if (now - phaseStart >= BOOT_MS) { phase = P_NET; phaseStart = now; }
  } else if (phase == P_NET) {
    drawNet(t);
    if (wx.ok) { phase = P_PAGES; phaseStart = lastAdvance = now; gAge = 0.f; }
  } else if (!linked && !wx.ok) {
    drawNet(t);
  } else {
    gAge += dt;
    if (!sliding && now - lastAdvance > DWELL_MS) {
      nxt = (uint8_t)((cur + 1) % WX_PAGES);
      sliding = true; slideStart = now; lastAdvance = now;
    }
    if (sliding) {
      const float p = clamp01((now - slideStart) / (float)SLIDE_MS);
      const uint16_t dx = (uint16_t)(easeInOutCubic(p) * (float)SCR_W);
      wxPage(cur, 0, t);
      shiftBodyLeft((uint8_t)dx);
      wxPage(nxt, (int16_t)(SCR_W - dx), t);
      if (p >= 1.f) { cur = nxt; sliding = false; gAge = 0.f; }
    } else {
      wxPage(cur, 0, t);
    }
    header(t, (now - lastAdvance) / (float)DWELL_MS);
    }
  dsp->sendBuffer();
}
