// Boot, network bring-up, and the graphical outage sequence.
//
// The outage is DRAWN, not listed: mains feeding a UPS feeding the host, with a
// grid of guests below. When the feed breaks the guests collapse one by one at
// the exact offsets the run log recorded, so a dry run and a real recovery test
// animate identically and truthfully.
#include "ui.h"
#include "common/anim.h"

// ---------- boot ----------------------------------------------------------
void bootDraw(float p, float t) {
  // A horizon splits outward, the wordmark types in, then a rack of blocks
  // lights up - the same block motif the outage scene uses.
  const float e = easeOutCubic(clamp01(p / 0.35f));
  const int16_t half = (int16_t)(e * 64.f);
  dsp->drawHLine((int16_t)(64 - half), 31, (uint8_t)(half * 2));

  if (p > 0.22f) {
    static const char *word = "nbdevlab";
    dsp->setFont(u8g2_font_6x10_tf);
    const uint8_t full = dsp->getStrWidth(word);
    const uint8_t n = (uint8_t)(clamp01((p - 0.22f) / 0.30f) * strlen(word) + 0.5f);
    char buf[16];
    memset(buf, 0, sizeof buf);
    strncpy(buf, word, n);
    dsp->drawStr((int16_t)(64 - full / 2), 27, buf);
    if (n < strlen(word) && pulse(t * 1000.f, 240.f) > 0.5f)
      dsp->drawBox((int16_t)(64 - full / 2 + n * 6), 19, 5, 9);
  }

  if (p > 0.48f) {
    for (uint8_t i = 0; i < 12; i++) {
      const float a = clamp01((p - 0.48f - i * 0.026f) / 0.14f);
      if (a <= 0.f) continue;
      const int16_t x = (int16_t)(4 + i * 10);
      const uint8_t h = (uint8_t)(easeOutBack(a) * 9.f);
      if (h) dsp->drawBox(x, (int16_t)(46 - h), 7, h);
    }
  }
}

// ---------- network / first data -----------------------------------------
// state: 0 joining wifi, 1 wifi up but no data yet, 2 lost after being up
void netDraw(uint8_t state, float t, const char *ssid, const char *ip) {
  const int16_t cx = 22, cy = 34;

  // Antenna, with sonar arcs pulsing outward while it searches.
  dsp->drawVLine(cx, cy - 6, 12);
  dsp->drawBox((int16_t)(cx - 2), (int16_t)(cy + 6), 5, 2);
  for (uint8_t k = 0; k < 3; k++) {
    const float ph = fmodf(t * 0.9f + k * 0.333f, 1.f);
    const uint8_t r = (uint8_t)(4 + ph * 16.f);
    if (ph > 0.85f) continue;                 // fade out at the edge
    for (int16_t a = -50; a <= 50; a += 10) {
      const float rad = a * 0.01745f;
      dsp->drawPixel((int16_t)(cx + cosf(rad) * r), (int16_t)(cy - 6 + sinf(rad) * r));
    }
  }

  dsp->setFont(u8g2_font_6x10_tf);
  dsp->drawStr(46, 30, state == 0 ? "joining" : (state == 1 ? "linked" : "lost"));
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr(46, 40, state == 0 ? ssid : ip);

  // Dot cycle, so a long wait still looks alive.
  const uint8_t dots = (uint8_t)(fmodf(t * 2.f, 4.f));
  for (uint8_t i = 0; i < dots; i++) dsp->drawBox((int16_t)(46 + i * 5), 46, 3, 3);

  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr(46, 58, state == 1 ? "waiting for byteme2" : "");
  // Progress comb along the bottom.
  for (int16_t x = 0; x < 128; x += 4)
    if (fmodf(t * 26.f + x, 128.f) < 30.f) dsp->drawPixel(x, 62);
}

// ---------- outage sequence ----------------------------------------------
// The stage rail mirrors the dashboard's own treatment: a vertical column of
// NUMBERED circles joined by connectors, where the live stage carries a glow
// ring and completed ones are filled with a knocked-out tick. The right-hand
// side then shows whatever that stage is actually doing.
static uint32_t seqStart = 0;
static float    seqScale = 1.f;
static float    playDur  = 20.f;
static bool     seqReady = false;
// Set while the dashboard is driving us, so progress/done follow the browser.
static bool     following = false;
static float    followF = 0.f;
static float    syncNow = 0.f;
static bool     syncArmed = false;

struct StageDef { const char *key, *label, *sub; };

// Act 1 is the shutdown, act 2 the recovery. Split so the circles stay big
// enough to hold a legible digit.
static const StageDef ACT1[5] = {
  { "detect",  "Detect outage", "pwrstatd" },
  { "remote",  "Shed remote",   "iDRAC 1.7" },
  { "guests",  "Stop guests",   "deadline-aware" },
  { "cap",     "Cap host",      "120W via BMC" },
  { "done",    "On battery",    "budget held" },
};
static const StageDef ACT2[3] = {
  { "restore",  "Restore",      "automatic" },
  { "up",       "Start guests", "saved state" },
  { "restored", "Restored",     "verified" },
};

static float stageOff(const char *key) {
  for (uint8_t i = 0; i < fd.seq.nS; i++)
    if (!strcmp(fd.seq.s[i].k, key)) return (float)fd.seq.s[i].off;
  return -1.f;
}

// One line orbiting the border, continuously. Not a progress meter - just
// steady motion so the panel is never entirely still. A solid head with a tail
// that thins behind it, one lap every ~5.5s. The perimeter crosses both colour
// bands, so the line goes amber along the top and blue round the rest, which is
// free on this panel.
static void borderOrbit(float t) {
  const int16_t TOP = SCR_W - 1, RIGHT = TOP + (SCR_H - 1);
  const int16_t BOT = RIGHT + (SCR_W - 1), PER = BOT + (SCR_H - 1);
  const float head = fmodf(t * (PER / 5.5f), (float)PER);
  const uint8_t LEN = 26;
  for (uint8_t d = 0; d < LEN; d++) {
    // Thin out toward the tail: solid, then every other, then every third.
    if (d >= 9  && (d & 1))       continue;
    if (d >= 17 && (d % 3) != 0)  continue;
    int16_t k = (int16_t)(head - d);
    if (k < 0) k += PER;
    int16_t x, y;
    if      (k < TOP)   { x = k;                            y = 0; }
    else if (k < RIGHT) { x = SCR_W - 1;                    y = (int16_t)(k - TOP); }
    else if (k < BOT)   { x = (int16_t)(SCR_W - 1 - (k - RIGHT)); y = SCR_H - 1; }
    else                { x = 0;                            y = (int16_t)(SCR_H - 1 - (k - BOT)); }
    dsp->drawPixel(x, y);
  }
}

// Letter-spaced small caps: the dashboard's eyebrow style, which is most of
// why its labels read as deliberate rather than cramped.
static void eyebrow(int16_t x, int16_t y, const char *s) {
  dsp->setFont(u8g2_font_4x6_tr);
  char c[2] = { 0, 0 };
  for (uint8_t i = 0; s[i]; i++) {
    c[0] = s[i];
    dsp->drawStr((int16_t)(x + i * 5), y, c);
  }
}

bool outageAvailable() { return fd.seq.nG > 0 || fd.seq.nS > 0; }

void outageBegin() {
  seqReady = fd.seq.nG || fd.seq.nS;
  if (!seqReady) return;
  const float dur = fd.seq.dur ? (float)fd.seq.dur : 8.f;
  // A 7s dry run is too fast to watch and a 212s real run too slow, so both are
  // retimed onto a similar screen duration and the factor is shown.
  playDur  = 20.f + (dur / 20.f > 12.f ? 12.f : dur / 20.f);
  seqScale = dur / playDur;
  seqStart = millis();
}

float outageProgress() {
  if (following) return clamp01(followF);
  if (!seqReady) return 1.f;
  return clamp01(((millis() - seqStart) / 1000.f) / (playDur + 3.f));
}
// While the browser is replaying we never finish on our own - it decides.
bool outageDone() { return following ? false : outageProgress() >= 1.f; }

void outageDraw(float t) {
  if (!seqReady) return;
  const float el = (millis() - seqStart) / 1000.f;

  // Follow the dashboard's replay position when it is reporting one. Its frames
  // reveal log lines, and those lines are not evenly spread through the run, so
  // the reported frame is mapped through each line's real offset rather than
  // linearly onto the duration.
  following = (fd.pos.n > 1 && fd.pos.age <= 4);
  float now;
  if (following) {
    followF = (float)fd.pos.i / (float)(fd.pos.n - 1);
    float target;
    if (fd.seq.nl > 1) {
      uint16_t idx = (uint16_t)(followF * (fd.seq.nl - 1) + 0.5f);
      if (idx >= fd.seq.nl) idx = (uint16_t)(fd.seq.nl - 1);
      target = (float)fd.seq.lo[idx];
    } else {
      target = followF * (float)fd.seq.dur;
    }
    if (!syncArmed) { syncArmed = true; syncNow = target; }
    syncNow += (target - syncNow) * 0.12f;     // glides across the 800ms steps
    now = syncNow;
  } else {
    syncArmed = false;
    now = el * seqScale;
  }

  // ---- phase ------------------------------------------------------------
  // The amber rail changes what it measures as the run moves through its
  // phases, because a deadline is meaningless once it has been met and a
  // battery level is meaningless while mains is still up.
  const float tDone     = stageOff("done");
  const float tRestore  = stageOff("restore");
  const float tRestored = stageOff("restored");
  enum Ph { PH_SHED, PH_BATT, PH_BACK, PH_DONE };
  Ph ph = PH_SHED;
  if      (tRestored >= 0.f && now >= tRestored) ph = PH_DONE;
  else if (tRestore  >= 0.f && now >= tRestore)  ph = PH_BACK;
  else if (tDone     >= 0.f && now >= tDone)     ph = PH_BATT;

  // ---- how many guests are up / down at this instant ---------------------
  uint8_t up = 0, total = fd.seq.nG;
  for (uint8_t i = 0; i < fd.seq.nG; i++) {
    const SeqEvent &g = fd.seq.g[i];
    const bool down = g.off >= 0 && now >= (float)g.off;
    const bool back = g.up  >= 0 && now >= (float)g.up;
    if (!down || back) up++;
  }
  const uint8_t downN = (uint8_t)(total - up);

  // ---- power: real telemetry when a run is live, modelled on replay ------
  const bool livePower = fd.seq.live != 0;
  float battPct, drawW, runMin;
  if (livePower) {
    battPct = fd.ups.batt; drawW = fd.ups.load; runMin = fd.ups.rt;
  } else {
    // ~135 Wh usable; the host draws ~60 W on its own, each guest ~6.8 W.
    const float onBatt = (tDone >= 0.f && now > tDone)
                           ? (now - tDone) : 0.f;
    battPct = 100.f - onBatt * 0.09f;
    if (battPct < 40.f) battPct = 40.f;
    drawW   = 60.f + 6.8f * up;
    runMin  = (135.f * battPct / 100.f) / drawW * 60.f;
    if (runMin > 180.f) runMin = 180.f;
  }

  // ---- amber band: label, rail, and the phase's own figure ---------------
  const char *phLabel = ph == PH_SHED ? "MAKING SAFE"
                      : ph == PH_BATT ? "ON BATTERY"
                      : ph == PH_BACK ? "COMING BACK" : "RESTORED";
  eyebrow(3, 8, phLabel);

  float railF = 0.f;
  char railV[10];
  switch (ph) {
    case PH_SHED: railF = total ? (float)downN / total : 0.f;
                  snprintf(railV, sizeof railV, "%u/%u", downN, total); break;
    case PH_BATT: railF = clamp01(battPct / 100.f);
                  snprintf(railV, sizeof railV, "%d%%", (int)(battPct + 0.5f)); break;
    case PH_BACK: railF = total ? (float)up / total : 0.f;
                  snprintf(railV, sizeof railV, "%u/%u", up, total); break;
    default:      railF = 1.f; snprintf(railV, sizeof railV, "OK"); break;
  }
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(124 - dsp->getStrWidth(railV)), 8, railV);
  // Dotted track with a solid fill. No bloom on the rail.
  for (int16_t x = 4; x < 124; x += 3) dsp->drawPixel(x, 13);
  const uint8_t fw = (uint8_t)(clamp01(railF) * 120.f);
  if (fw) dsp->drawHLine(4, 13, fw);

  // ---- the count that matters, large -----------------------------------
  char big[8];
  const char *bigSub;
  if (ph == PH_BATT) { snprintf(big, sizeof big, "%d", (int)(battPct + 0.5f)); bigSub = "percent"; }
  else if (ph == PH_BACK || ph == PH_DONE) { snprintf(big, sizeof big, "%u", up); bigSub = "restarted"; }
  else { snprintf(big, sizeof big, "%u", downN); bigSub = "stopped"; }
  dsp->setFont(u8g2_font_logisoso24_tr);
  dsp->drawStr(4, 44, big);
  const uint8_t bw = dsp->getStrWidth(big);
  dsp->setFont(u8g2_font_4x6_tr);
  if (ph != PH_BATT) {
    char ofn[10];
    snprintf(ofn, sizeof ofn, "of %u", total);
    dsp->drawStr((int16_t)(7 + bw), 44, ofn);
  } else {
    dsp->drawStr((int16_t)(7 + bw), 44, "%");
  }
  dsp->drawStr(4, 52, bigSub);

  // ---- secondary figures, right column ---------------------------------
  const int16_t RX = 124;
  dsp->drawStr(72, 26, "deadline");
  char v[14];
  if (ph == PH_SHED) snprintf(v, sizeof v, "%ds", (int)(fd.seq.budget - now > 0 ? fd.seq.budget - now : 0));
  else               snprintf(v, sizeof v, "met");
  dsp->drawStr((int16_t)(RX - dsp->getStrWidth(v)), 26, v);

  dsp->drawStr(72, 36, livePower ? "runtime" : "runtime est");
  snprintf(v, sizeof v, "%dm", (int)(runMin + 0.5f));
  dsp->drawStr((int16_t)(RX - dsp->getStrWidth(v)), 36, v);

  dsp->drawStr(72, 46, "draw");
  snprintf(v, sizeof v, "%dW", (int)(drawW + 0.5f));
  dsp->drawStr((int16_t)(RX - dsp->getStrWidth(v)), 46, v);

  // ---- the event that just landed --------------------------------------
  int8_t lastEv = -1;
  float  lastAt = -1.f;
  bool   lastUp = false;
  for (uint8_t i = 0; i < fd.seq.nG; i++) {
    const SeqEvent &g = fd.seq.g[i];
    if (g.off >= 0 && now >= (float)g.off && (float)g.off > lastAt) {
      lastAt = (float)g.off; lastEv = (int8_t)i; lastUp = false;
    }
    if (g.up >= 0 && now >= (float)g.up && (float)g.up > lastAt) {
      lastAt = (float)g.up; lastEv = (int8_t)i; lastUp = true;
    }
  }
  borderOrbit(t);

  // Which run, and whether the browser is driving us. Bottom right, quiet.
  {
    char tag[18];
    const char *kind = fd.seq.live ? "live" : (fd.seq.kind[0] == 'd' ? "dry" : "real");
    if (following)          snprintf(tag, sizeof tag, "%s sync", kind);
    else if (seqScale >= 1.f) snprintf(tag, sizeof tag, "%s %.0fx", kind, seqScale);
    else                      snprintf(tag, sizeof tag, "%s %.1fx", kind, 1.f / seqScale);
    dsp->setFont(u8g2_font_4x6_tr);
    dsp->drawStr((int16_t)(123 - dsp->getStrWidth(tag)), 61, tag);
  }

  if (lastEv >= 0 && now - lastAt < 6.f) {
    const SeqEvent &g = fd.seq.g[lastEv];
    char line[34];
    if (lastUp) snprintf(line, sizeof line, "up %s %u", g.kind ? "ct" : "vm", g.id);
    else        snprintf(line, sizeof line, "shed %s %u  %us/%us%s", g.kind ? "ct" : "vm",
                         g.id, g.used, g.grace, g.forced ? "  FORCED" : "");
    dsp->setClipWindow(0, 56, 76, 64);
    dsp->drawStr(4, 61, line);
    dsp->setMaxClipWindow();
  } else if (fd.seq.modeled && ph >= PH_BACK) {
    dsp->drawStr(4, 61, "recovery modeled");
  }
}
