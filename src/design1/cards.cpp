// The six status cards. Each draws inside x=[ox, ox+128), y=[CARD_Y0, CARD_Y1].
#include "ui.h"
#include "common/anim.h"

// Tweens live here so values glide whenever the card is on screen.
static Tween tTok, tBmCpu, tBmRam, tB2Cpu, tB2Ram, tBatt, tLoad, tAlerts;
static Spark<64> sparkTok;
static uint32_t lastSpark = 0;

void cardsUpdate(float dt) {
  tTok.set(g.tokS);         tTok.rate   = 6.f;
  tBmCpu.set(g.bmCpu);      tBmCpu.rate = 5.f;
  tBmRam.set(g.bmRam);      tBmRam.rate = 5.f;
  tB2Cpu.set(g.bm2Cpu);     tB2Cpu.rate = 5.f;
  tB2Ram.set(g.bm2Ram);     tB2Ram.rate = 5.f;
  tBatt.set(g.battPct);     tBatt.rate  = 3.f;
  tLoad.set(g.loadW);       tLoad.rate  = 4.f;
  tAlerts.set(g.alerts);    tAlerts.rate = 7.f;

  tTok.update(dt); tBmCpu.update(dt); tBmRam.update(dt);
  tB2Cpu.update(dt); tB2Ram.update(dt); tBatt.update(dt);
  tLoad.update(dt); tAlerts.update(dt);

  const uint32_t now = millis();
  if (now - lastSpark > 130) {
    lastSpark = now;
    const float capped = g.tokS > 120.f ? 120.f : g.tokS;
    sparkTok.push((uint8_t)(capped / 120.f * 255.f));
  }
}

static void rightStr(int16_t ox, int16_t y, const char *s) {
  dsp->drawStr((int16_t)(ox + 126 - dsp->getStrWidth(s)), y, s);
}

static void fmtUptime(char *out, size_t n, uint32_t mins) {
  const uint32_t d = mins / 1440, h = (mins % 1440) / 60;
  if (d) snprintf(out, n, "up %ud%uh", (unsigned)d, (unsigned)h);
  else   snprintf(out, n, "up %uh%um", (unsigned)h, (unsigned)(mins % 60));
}

// ---- 0: OVERVIEW ---------------------------------------------------------
// Worst-of rollup: the card you want when you pull the fob out of a pocket.
static void cardOverview(int16_t ox, float t) {
  const bool sysOk[6] = {
    g.nodes[0].up, g.bmCpu < 90 && g.bm2Cpu < 90, !g.onBattery,
    g.alerts == 0, g.svcs[0].up && g.svcs[1].up && g.svcs[2].up, g.wanUp
  };
  uint8_t bad = 0;
  for (uint8_t i = 0; i < 6; i++) if (!sysOk[i]) bad++;

  dsp->setFont(u8g2_font_helvB10_tr);
  const char *word = bad == 0 ? "NOMINAL" : (bad == 1 ? "DEGRADED" : "ALERT");
  dsp->drawStr((int16_t)(ox + 2), 28, word);
  drawSweep((int16_t)(ox + 2), 31, dsp->getStrWidth(word), t);

  uint8_t svcUp = 0;
  for (uint8_t i = 0; i < g.svcCount; i++) if (g.svcs[i].up) svcUp++;
  char buf[32], up[16];
  fmtUptime(up, sizeof up, g.uptimeMin);
  snprintf(buf, sizeof buf, "svc %u/%u   %s", svcUp, g.svcCount, up);
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 2), 40, buf);

  static const char *lbl[6] = { "clst", "pmox", "pwr", "sec", "svc", "wan" };
  for (uint8_t i = 0; i < 6; i++) {
    // Stagger the entrance left to right.
    if (clamp01((gCardAge - i * 0.05f) * 6.f) <= 0.f) continue;
    const int16_t x = (int16_t)(ox + 3 + i * 21);
    drawPip(x, 45, sysOk[i], t);
    dsp->drawStr(x - 1, 55, lbl[i]);
  }
}

// ---- 1: SLERM (llama.cpp cluster) ---------------------------------------
static void cardSlerm(int16_t ox, float t) {
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 2), 21, "SLERM");
  uint8_t nUp = 0;
  for (uint8_t i = 0; i < g.nodeCount; i++) if (g.nodes[i].up) nUp++;
  char hdr[24];
  snprintf(hdr, sizeof hdr, "q:%u  %u/%u up", g.queue, nUp, g.nodeCount);
  rightStr(ox, 21, hdr);

  char n[8];
  snprintf(n, sizeof n, "%d", (int)(tTok.v + 0.5f));
  dsp->setFont(u8g2_font_logisoso16_tn);
  dsp->drawStr((int16_t)(ox + 2), 40, n);
  const uint8_t nw = dsp->getStrWidth(n);
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 4 + nw), 40, "tok/s");

  // Sparkline, newest on the right.
  const int16_t sx = (int16_t)(ox + 62), sy = 24;
  const uint8_t sw = 64, sh = 16;
  const uint8_t cnt = sparkTok.count();
  int16_t prevY = 0;
  for (uint8_t i = 0; i < cnt; i++) {
    const int16_t px = (int16_t)(sx + sw - cnt + i);
    const int16_t py = (int16_t)(sy + sh - 1 - (sparkTok.at(i) * (sh - 1)) / 255);
    if (i) safeLine((int16_t)(px - 1), prevY, px, py);
    prevY = py;
  }
  for (int16_t i = 0; i < sw; i += 3) dsp->drawPixel((int16_t)(sx + i), (int16_t)(sy + sh));

  for (uint8_t i = 0; i < g.nodeCount; i++) {
    if (clamp01((gCardAge - i * 0.06f) * 6.f) <= 0.f) continue;
    const int16_t x = (int16_t)(ox + 3 + i * 25);
    drawPip(x, 45, g.nodes[i].up, t);
    dsp->drawStr((int16_t)(x + 6), 49, g.nodes[i].name);
    if (g.nodes[i].up) {
      char tp[8];
      snprintf(tp, sizeof tp, "%dC", g.nodes[i].temp);
      dsp->drawStr(x, 55, tp);
    } else {
      dsp->drawStr(x, 55, "--");
    }
  }
}

// ---- 2: PROXMOX ----------------------------------------------------------
static void cardProxmox(int16_t ox, float t) {
  (void)t;
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 2), 21, "PROXMOX");
  char hdr[20];
  snprintf(hdr, sizeof hdr, "%uvm %uct", g.vms, g.cts);
  rightStr(ox, 21, hdr);

  static const char *lbl[4] = { "bm  cpu", "bm  mem", "bm2 cpu", "bm2 mem" };
  const Tween *tw[4] = { &tBmCpu, &tBmRam, &tB2Cpu, &tB2Ram };
  for (uint8_t i = 0; i < 4; i++) {
    // Rows wipe in from the top.
    const float a = clamp01((gCardAge - i * 0.06f) * 7.f);
    if (a <= 0.f) continue;
    const int16_t y = (int16_t)(24 + i * 8);
    dsp->drawStr((int16_t)(ox + 2), (int16_t)(y + 5), lbl[i]);
    const uint8_t w = (uint8_t)(66 * easeOutCubic(a));
    if (w > 3) drawBarH((int16_t)(ox + 32), y, w, 6, tw[i]->v / 100.f);
    char pc[8];
    snprintf(pc, sizeof pc, "%d%%", (int)(tw[i]->v + 0.5f));
    rightStr(ox, (int16_t)(y + 5), pc);
  }
}

// ---- 3: POWER / UPS ------------------------------------------------------
static void cardPower(int16_t ox, float t) {
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 2), 21, "POWER");
  rightStr(ox, 21, g.onBattery ? "ON BATTERY" : "MAINS");

  drawBattery((int16_t)(ox + 2), 26, 44, 24, tBatt.v / 100.f, !g.onBattery && g.battPct < 100, t);
  char pc[8];
  snprintf(pc, sizeof pc, "%u%%", g.battPct);
  dsp->setFont(u8g2_font_5x7_tr);
  dsp->setDrawColor(2);                        // XOR: legible over the fill
  dsp->drawStr((int16_t)(ox + 24 - dsp->getStrWidth(pc) / 2), 41, pc);
  dsp->setDrawColor(1);

  char w[12];
  snprintf(w, sizeof w, "%dW", (int)(tLoad.v + 0.5f));
  dsp->setFont(u8g2_font_6x10_tf);
  dsp->drawStr((int16_t)(ox + 54), 36, w);

  char rt[20];
  snprintf(rt, sizeof rt, "%um hold", g.runtimeMin);
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 54), 46, rt);
  dsp->drawStr((int16_t)(ox + 54), 55, g.onBattery ? "!! outage" : "byteme2 ups");
}

// ---- 4: WAZUH ------------------------------------------------------------
static void cardWazuh(int16_t ox, float t) {
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 2), 21, "WAZUH");
  char hdr[20];
  snprintf(hdr, sizeof hdr, "%u/%u agents", g.agentsUp, g.agentsTotal);
  rightStr(ox, 21, hdr);

  char n[8];
  snprintf(n, sizeof n, "%d", (int)(tAlerts.v + 0.5f));
  dsp->setFont(u8g2_font_logisoso24_tr);
  dsp->drawStr((int16_t)(ox + 2), 47, n);

  static const char *sv[4] = { "C", "H", "M", "L" };
  uint8_t peak = 1;
  for (uint8_t i = 0; i < 4; i++) if (g.sev[i] > peak) peak = g.sev[i];
  dsp->setFont(u8g2_font_4x6_tr);
  for (uint8_t i = 0; i < 4; i++) {
    const float a = clamp01((gCardAge - i * 0.07f) * 7.f);
    if (a <= 0.f) continue;
    const int16_t y = (int16_t)(24 + i * 6);
    dsp->drawStr((int16_t)(ox + 48), (int16_t)(y + 5), sv[i]);
    drawBarH((int16_t)(ox + 54), y, 48, 5, (float)g.sev[i] / peak * easeOutCubic(a));
    char c[6];
    snprintf(c, sizeof c, "%u", g.sev[i]);
    rightStr(ox, (int16_t)(y + 5), c);
  }
  drawMarquee((int16_t)(ox + 2), 54, 124, g.lastAlert, t);
}

// ---- 5: SERVICES ---------------------------------------------------------
static void cardServices(int16_t ox, float t) {
  (void)t;
  dsp->setFont(u8g2_font_4x6_tr);
  for (uint8_t i = 0; i < g.svcCount; i++) {
    const float a = clamp01((gCardAge - i * 0.07f) * 6.f);
    if (a <= 0.f) continue;
    const uint8_t col = i % 3, row = i / 3;
    const int16_t x = (int16_t)(ox + 3 + col * 42);
    const int16_t nameB = (int16_t)(23 + row * 17);
    dsp->drawStr(x, nameB, g.svcs[i].name);
    if (g.svcs[i].up) drawCheck(x, (int16_t)(nameB + 3), a);
    else              drawCross(x, (int16_t)(nameB + 3), a);
    char ms[10];
    if (g.svcs[i].up) snprintf(ms, sizeof ms, "%ums", g.svcs[i].ms);
    else              snprintf(ms, sizeof ms, "DOWN");
    dsp->drawStr((int16_t)(x + 8), (int16_t)(nameB + 11), ms);
  }
}

void drawCard(uint8_t idx, int16_t ox, float t) {
  switch (idx) {
    case 0: cardOverview(ox, t); break;
    case 1: cardSlerm(ox, t);    break;
    case 2: cardProxmox(ox, t);  break;
    case 3: cardPower(ox, t);    break;
    case 4: cardWazuh(ox, t);    break;
    default: cardServices(ox, t); break;
  }
}

const char *cardName(uint8_t idx) {
  static const char *n[CARD_COUNT] = { "overview", "slerm", "proxmox", "power", "wazuh", "services" };
  return n[idx < CARD_COUNT ? idx : 0];
}
