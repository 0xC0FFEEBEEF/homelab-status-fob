// The pages. Each draws inside x=[ox, ox+128), y=[BODY_Y0, 63].
#include "ui.h"
#include "common/anim.h"

Page    gPages[20];
uint8_t gPageCount = 0;
float   gAge = 0.f;

// Per-VM CPU history, so a VM page has a shape and not just a number.
static uint8_t cpuHist[MAX_GUESTS][32];
static uint8_t histHead = 0;
static bool    histFull = false;
static uint32_t lastHist = 0;

static Tween tA, tB, tC;      // reused per page; snapped on page change
static PageKind lastKind = PG_SEC;
static uint8_t  lastIdx = 255;

void pagesRebuild() {
  gPageCount = 0;
  for (uint8_t i = 0; i < fd.vmCount && gPageCount < 18; i++)
    gPages[gPageCount++] = { PG_VM, i };
  if (fd.ctCount) gPages[gPageCount++] = { PG_CTS, 0 };
  gPages[gPageCount++] = { PG_UPS, 0 };
  gPages[gPageCount++] = { PG_SLERM, 0 };
  gPages[gPageCount++] = { PG_SEC, 0 };
  gPages[gPageCount++] = { PG_WX_NOW, 0 };
  gPages[gPageCount++] = { PG_WX_WIND, 0 };
  gPages[gPageCount++] = { PG_WX_STORM, 0 };
  // Deliberately NOT in the rotation: the outage sequence is shown only when a
  // run is live, or a replay is triggered from the dashboard. It kept cycling
  // round and replaying a stale dry run otherwise.
}

const char *pageTitle(const Page &p) {
  static char buf[18];
  switch (p.kind) {
    case PG_VM:      snprintf(buf, sizeof buf, "%u %s", fd.vms[p.idx].id, fd.vms[p.idx].name); return buf;
    case PG_CTS:     return "containers";
    case PG_UPS:     return "ups";
    case PG_SLERM:   return "slerm";
    case PG_SEC:     return "security";
    case PG_REPLAY:  return "last run";
    case PG_WX_NOW:  return "weather";
    case PG_WX_WIND: return "wind";
    default:         return "storm";
  }
}

void pagesSampleHistory() {
  const uint32_t now = millis();
  if (now - lastHist < 2000) return;
  lastHist = now;
  for (uint8_t i = 0; i < MAX_GUESTS; i++) {
    const float c = i < fd.vmCount ? fd.vms[i].cpu : 0.f;
    cpuHist[i][histHead] = (uint8_t)(clamp01(c / 25.f) * 255.f);   // 25% full-scale
  }
  histHead = (uint8_t)((histHead + 1) % 32);
  if (!histHead) histFull = true;
}

static void fmtUp(char *o, size_t n, uint32_t secs) {
  const uint32_t d = secs / 86400, h = (secs % 86400) / 3600;
  if (d) snprintf(o, n, "up %lud %luh", (unsigned long)d, (unsigned long)h);
  else   snprintf(o, n, "up %luh %lum", (unsigned long)h, (unsigned long)((secs % 3600) / 60));
}

// A bar with a travelling highlight, so load reads as motion not just length.
static void meter(int16_t x, int16_t y, uint8_t w, uint8_t h, float pct, float t, bool live) {
  dsp->drawFrame(x, y, w, h);
  const uint8_t inner = (uint8_t)(w - 2);
  const uint8_t fw = (uint8_t)(clamp01(pct / 100.f) * inner);
  if (fw) dsp->drawBox((int16_t)(x + 1), (int16_t)(y + 1), fw, (uint8_t)(h - 2));
  if (live && fw > 3) {
    const int16_t hx = (int16_t)(x + 1 + (int16_t)(fmodf(t * 18.f, (float)fw)));
    dsp->setDrawColor(0);
    dsp->drawVLine(hx, (int16_t)(y + 1), (uint8_t)(h - 2));
    dsp->setDrawColor(1);
  }
}

// ---- VM ------------------------------------------------------------------
static void pgVm(int16_t ox, uint8_t i, float t) {
  if (i >= fd.vmCount) return;
  const Guest &v = fd.vms[i];

  // Entrance: the figure rises into place with a slight overshoot.
  const float ent = easeOutBack(clamp01(gAge * 3.2f));
  const int16_t rise = (int16_t)((1.f - ent) * 16.f);
  dsp->setClipWindow(0, BODY_Y0, SCR_W, 64);
  dsp->setFont(u8g2_font_logisoso16_tn);
  char n[8];
  snprintf(n, sizeof n, "%d", (int)(tA.v + 0.5f));
  dsp->drawStr((int16_t)(ox + 2), (int16_t)(34 + rise), n);
  dsp->setMaxClipWindow();
  const uint8_t nw = dsp->getStrWidth(n);
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 3 + nw), 28, "%");
  dsp->drawStr((int16_t)(ox + 3 + nw), 34, "cpu");

  // CPU history sparkline
  const uint8_t cnt = histFull ? 32 : histHead;
  const int16_t sx = (int16_t)(ox + 40), sy = 20;
  // A highlight sweeps the history box, so an idle VM still has a pulse.
  dsp->setDrawColor(2);
  dsp->drawVLine((int16_t)(sx + (int16_t)fmodf(t * 11.f, 32.f)), sy, 15);
  dsp->setDrawColor(1);
  int16_t prev = 0;
  for (uint8_t k = 0; k < cnt; k++) {
    const uint8_t s = cpuHist[i][(uint8_t)(((histFull ? histHead : 0) + k) % 32)];
    const int16_t px = (int16_t)(sx + 32 - cnt + k);
    const int16_t py = (int16_t)(sy + 14 - (s * 14) / 255);
    if (k && px >= 0) dsp->drawLine((uint16_t)(px - 1), (uint16_t)prev, (uint16_t)px, (uint16_t)py);
    prev = py;
  }

  dsp->setFont(u8g2_font_6x10_tf);
  char b[24];
  snprintf(b, sizeof b, "%.1fG", v.mgb);
  dsp->drawStr((int16_t)(ox + 80), 28, b);
  snprintf(b, sizeof b, "%uc", v.nc);
  dsp->drawStr((int16_t)(ox + 80), 40, b);

  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 2), 46, "mem");
  const uint8_t mw = (uint8_t)(54 * easeOutCubic(clamp01(gAge * 3.f)));
  if (mw > 4) meter((int16_t)(ox + 20), 40, mw, 7, tB.v, t, v.cpu > 1.f);
  snprintf(b, sizeof b, "%d%%", (int)(tB.v + 0.5f));
  dsp->drawStr((int16_t)(ox + 20 + 56), 46, b);

  fmtUp(b, sizeof b, v.up);
  dsp->setFont(u8g2_font_6x10_tf);
  dsp->drawStr((int16_t)(ox + 2), 62, b);
}

// ---- containers ---------------------------------------------------------
static void pgCts(int16_t ox, float t) {
  dsp->setFont(u8g2_font_6x10_tf);
  for (uint8_t i = 0; i < fd.ctCount && i < 5; i++) {
    const float a = clamp01((gAge - i * 0.07f) * 6.f);
    if (a <= 0.f) continue;
    const int16_t y = (int16_t)(26 + i * 9);
    const int16_t slide = (int16_t)((1.f - easeOutCubic(a)) * 22.f);
    dsp->drawStr((int16_t)(ox + 2 - slide), y, fd.cts[i].name);
    const uint8_t w = (uint8_t)(34 * easeOutCubic(a));
    if (w > 3) meter((int16_t)(ox + 74), (int16_t)(y - 7), w, 7, fd.cts[i].mem, t, false);
    char b[8];
    snprintf(b, sizeof b, "%u", fd.cts[i].id);
    dsp->setFont(u8g2_font_4x6_tr);
    dsp->drawStr((int16_t)(ox + 112), y, b);
    dsp->setFont(u8g2_font_6x10_tf);
  }
}

// ---- UPS (mirrors the byteme2 hero card) --------------------------------
static void pgUps(int16_t ox, float t) {
  const bool onUtil = fd.ups.online;

  // A live AC waveform whose amplitude tracks the measured input voltage: the
  // one thing on the fob that shows the mains is actually there.
  const float amp = clamp01(fd.ups.iv / 130.f) * 7.f;
  for (int16_t x = 0; x < 120; x++) {
    const float ph = (x / 120.f) * 6.2832f * 2.f + t * 6.f;
    const int16_t y = (int16_t)(27 - sinf(ph) * amp);
    dsp->drawPixel((int16_t)(ox + 4 + x), y);
  }
  // A charge rides the waveform: the mains, visibly arriving.
  {
    const float px = fmodf(t * 46.f, 120.f);
    const float ph = (px / 120.f) * 6.2832f * 2.f + t * 6.f;
    const int16_t py = (int16_t)(27 - sinf(ph) * amp);
    dsp->drawDisc((int16_t)(ox + 4 + px), py, 1);
  }
  dsp->setFont(u8g2_font_4x6_tr);
  char b[26];
  snprintf(b, sizeof b, "%.0fV in  %.0fV out", fd.ups.iv, fd.ups.ov);
  dsp->drawStr((int16_t)(ox + 2), 41, b);

  // battery
  dsp->drawFrame((int16_t)(ox + 2), 44, 40, 12);
  dsp->drawBox((int16_t)(ox + 42), 47, 2, 6);
  const uint8_t fw = (uint8_t)(clamp01(tA.v / 100.f) * 36.f * easeOutCubic(clamp01(gAge * 2.6f)));
  if (fw) dsp->drawBox((int16_t)(ox + 4), 46, fw, 8);
  if (fw > 6) {                       // shimmer travelling through the charge
    dsp->setDrawColor(2);
    dsp->drawVLine((int16_t)(ox + 4 + (int16_t)fmodf(t * 13.f, (float)fw)), 46, 8);
    dsp->setDrawColor(1);
  }
  dsp->setFont(u8g2_font_4x6_tr);
  snprintf(b, sizeof b, "%.0f%%", fd.ups.batt);
  dsp->setDrawColor(2);
  dsp->drawStr((int16_t)(ox + 14), 53, b);
  dsp->setDrawColor(1);

  dsp->setFont(u8g2_font_6x10_tf);
  snprintf(b, sizeof b, "%.0fW", fd.ups.load);
  dsp->drawStr((int16_t)(ox + 48), 54, b);
  dsp->setFont(u8g2_font_4x6_tr);
  snprintf(b, sizeof b, "%.0fmin", fd.ups.rt);
  dsp->drawStr((int16_t)(ox + 82), 54, b);
  snprintf(b, sizeof b, "%s", onUtil ? fd.ups.src : "ON BATTERY");
  dsp->drawStr((int16_t)(ox + 48), 62, b);
  if (!onUtil && pulse(t * 1000.f, 700.f) > 0.4f) dsp->drawFrame((int16_t)(ox + 46), 56, 60, 7);
  snprintf(b, sizeof b, "%.0f%% of %.0fVA", fd.ups.pct, fd.ups.va);
  dsp->drawStr((int16_t)(ox + 2), 62, b);
}

// ---- slerm -------------------------------------------------------------
static void pgSlerm(int16_t ox, float t) {
  dsp->setFont(u8g2_font_4x6_tr);
  uint8_t shown = 0;
  for (uint8_t i = 0; i < fd.vmCount; i++) {
    if (strncmp(fd.vms[i].name, "slerm", 5) != 0) continue;
    const int16_t y = (int16_t)(26 + shown * 14);
    dsp->setFont(u8g2_font_6x10_tf);
    dsp->drawStr((int16_t)(ox + 2), y, fd.vms[i].name + 6);
    dsp->setFont(u8g2_font_4x6_tr);
    char b[16];
    snprintf(b, sizeof b, "%.0f%%cpu", fd.vms[i].cpu);
    dsp->drawStr((int16_t)(ox + 44), y, b);
    meter((int16_t)(ox + 78), (int16_t)(y - 7), 48, 7, fd.vms[i].mem, t, true);
    shown++;
  }
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 2), 62, "router unreachable from byteme2");
}

// ---- security (Wazuh deferred) -----------------------------------------
static void pgSec(int16_t ox, float t) {
  (void)t;
  dsp->setFont(u8g2_font_6x10_tf);
  dsp->drawStr((int16_t)(ox + 2), 30, "wazuh not wired");
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 2), 42, "deferred by request");
  char b[26];
  snprintf(b, sizeof b, "host %.0fC  load %.2f", fd.host.tmp, fd.host.l1);
  dsp->drawStr((int16_t)(ox + 2), 54, b);
  snprintf(b, sizeof b, "guardian %s", fd.gdMode);
  dsp->drawStr((int16_t)(ox + 2), 62, b);
}

// ---- weather -----------------------------------------------------------
static inline float cF(float c) { return c * 1.8f + 32.f; }

static void pgWxNow(int16_t ox, float t) {
  char b[26];
  const float ent = easeOutBack(clamp01(gAge * 3.f));
  dsp->setClipWindow(0, BODY_Y0, SCR_W, 64);
  dsp->setFont(u8g2_font_logisoso24_tn);
  snprintf(b, sizeof b, "%d", (int)(tA.v + 0.5f));
  dsp->drawStr((int16_t)(ox + 2), (int16_t)(44 + (1.f - ent) * 18.f), b);
  dsp->setMaxClipWindow();
  const uint8_t w = dsp->getStrWidth(b);
  dsp->setFont(u8g2_font_6x10_tf);
  dsp->drawStr((int16_t)(ox + 4 + w), 28, "F");

  dsp->setFont(u8g2_font_4x6_tr);
  snprintf(b, sizeof b, "feels %.0f", cF(fd.wx.fl));
  dsp->drawStr((int16_t)(ox + 60), 26, b);
  snprintf(b, sizeof b, "dew   %.0f", cF(fd.wx.dc));
  dsp->drawStr((int16_t)(ox + 60), 34, b);
  snprintf(b, sizeof b, "hum   %u%%", fd.wx.rh);
  dsp->drawStr((int16_t)(ox + 60), 42, b);

  // Sun height from measured solar radiation; moon and stars after dark.
  const bool day = fd.wx.sr > 12.f;
  const int16_t cx = (int16_t)(ox + 100);
  if (day) {
    const uint8_t r = (uint8_t)(3 + clamp01(fd.wx.sr / 800.f) * 3.f);
    dsp->drawDisc(cx, 54, r);
    for (uint8_t a = 0; a < 8; a++) {
      const float an = a * 0.785f + t * 0.3f;
      dsp->drawPixel((int16_t)(cx + cosf(an) * (r + 3)), (int16_t)(54 + sinf(an) * (r + 3)));
    }
  } else {
    dsp->drawDisc(cx, 54, 4);
    dsp->setDrawColor(0);
    dsp->drawDisc((int16_t)(cx + 3), 52, 4);
    dsp->setDrawColor(1);
  }
  snprintf(b, sizeof b, "%lus old", (unsigned long)fd.wx.age);
  dsp->drawStr((int16_t)(ox + 2), 62, b);
}

static void pgWxWind(int16_t ox, float t) {
  // Compass with an eased needle: bearing is the one weather value that is
  // genuinely angular, so draw it as an angle.
  const int16_t cx = (int16_t)(ox + 30), cy = 40;
  dsp->drawCircle(cx, cy, 17);
  for (uint8_t a = 0; a < 4; a++) {
    const float an = a * 1.5708f;
    dsp->drawPixel((int16_t)(cx + cosf(an) * 20), (int16_t)(cy + sinf(an) * 20));
  }
  // Radar sweep, dotted so it reads as a trace rather than a second needle.
  const float sweep = fmodf(t * 1.4f, 6.2832f);
  for (uint8_t r = 4; r < 17; r += 3)
    dsp->drawPixel((int16_t)(cx + cosf(sweep) * r), (int16_t)(cy + sinf(sweep) * r));
  const float rad = (tC.v - 90.f) * 0.01745f;
  dsp->drawLine((uint16_t)cx, (uint16_t)cy,
                (uint16_t)(cx + cosf(rad) * 15), (uint16_t)(cy + sinf(rad) * 15));
  dsp->drawDisc(cx, cy, 2);

  char b[22];
  dsp->setFont(u8g2_font_logisoso16_tn);
  snprintf(b, sizeof b, "%d", (int)(tA.v + 0.5f));
  dsp->drawStr((int16_t)(ox + 56), 36, b);
  const uint8_t w = dsp->getStrWidth(b);
  dsp->setFont(u8g2_font_4x6_tr);
  dsp->drawStr((int16_t)(ox + 58 + w), 36, "mph");
  snprintf(b, sizeof b, "gust %.0f", fd.wx.gm * 2.23694f);
  dsp->drawStr((int16_t)(ox + 56), 46, b);
  snprintf(b, sizeof b, "%.2f\" %s", fd.wx.mb * 0.02953f,
           fd.wx.tr < 0 ? "fall" : (fd.wx.tr > 0 ? "rise" : "stdy"));
  dsp->drawStr((int16_t)(ox + 56), 54, b);
  snprintf(b, sizeof b, "%u deg", fd.wx.wd);
  dsp->drawStr((int16_t)(ox + 56), 62, b);
}

static void pgWxStorm(int16_t ox, float t) {
  // Range rings, with the last strike placed on them.
  const int16_t cx = (int16_t)(ox + 28), cy = 40;
  for (uint8_t r = 6; r <= 18; r += 6) dsp->drawCircle(cx, cy, r);
  dsp->drawPixel(cx, cy);
  // A ping expands outward every couple of seconds.
  {
    const float p = fmodf(t, 2.4f) / 2.4f;
    const uint8_t pr = (uint8_t)(easeOutCubic(p) * 19.f);
    if (pr > 2 && p < 0.85f) dsp->drawCircle(cx, cy, pr);
  }
  const float mi = fd.wx.skm * 0.621371f;
  if (mi > 0.f && mi < 40.f) {
    const float rr = clamp01(mi / 30.f) * 18.f;
    const float an = t * 0.6f;
    dsp->drawDisc((int16_t)(cx + cosf(an) * rr), (int16_t)(cy + sinf(an) * rr), 2);
  }
  char b[24];
  dsp->setFont(u8g2_font_4x6_tr);
  snprintf(b, sizeof b, "last %.0f mi", mi);
  dsp->drawStr((int16_t)(ox + 54), 26, b);
  snprintf(b, sizeof b, "1hr  %u", fd.wx.s1);
  dsp->drawStr((int16_t)(ox + 54), 36, b);
  snprintf(b, sizeof b, "3hr  %u", fd.wx.s3);
  dsp->drawStr((int16_t)(ox + 54), 44, b);
  snprintf(b, sizeof b, "rain %.2f\"", fd.wx.rnd * 0.0393701f);
  dsp->drawStr((int16_t)(ox + 54), 54, b);
  snprintf(b, sizeof b, "uv %.1f  %uklx", fd.wx.uv, (unsigned)(fd.wx.bri / 1000));
  dsp->drawStr((int16_t)(ox + 2), 62, b);
}

void pageDraw(const Page &p, int16_t ox, float t) {
  // Snap tweens when the page identity changes, so a new page does not animate
  // up from the previous page's value.
  if (p.kind != lastKind || p.idx != lastIdx) {
    lastKind = p.kind; lastIdx = p.idx;
    switch (p.kind) {
      case PG_VM:      tA.snap(fd.vms[p.idx].cpu); tB.snap(fd.vms[p.idx].mem); break;
      case PG_UPS:     tA.snap(fd.ups.batt); break;
      case PG_WX_NOW:  tA.snap(cF(fd.wx.tc)); break;
      case PG_WX_WIND: tA.snap(fd.wx.wm * 2.23694f); tC.snap(fd.wx.wd); break;
      default: break;
    }
  }
  tA.rate = tB.rate = 4.f; tC.rate = 2.f;
  switch (p.kind) {
    case PG_VM:      tA.set(fd.vms[p.idx].cpu); tB.set(fd.vms[p.idx].mem); break;
    case PG_UPS:     tA.set(fd.ups.batt); break;
    case PG_WX_NOW:  tA.set(cF(fd.wx.tc)); break;
    case PG_WX_WIND: tA.set(fd.wx.wm * 2.23694f); tC.set(fd.wx.wd); break;
    default: break;
  }
  tA.update(0.02f); tB.update(0.02f); tC.update(0.02f);

  if (p.kind == PG_REPLAY) return;      // drawn full-bleed by main
  switch (p.kind) {
    case PG_VM:      pgVm(ox, p.idx, t); break;
    case PG_CTS:     pgCts(ox, t);       break;
    case PG_UPS:     pgUps(ox, t);       break;
    case PG_SLERM:   pgSlerm(ox, t);     break;
    case PG_SEC:     pgSec(ox, t);       break;
    case PG_WX_NOW:  pgWxNow(ox, t);     break;
    case PG_WX_WIND: pgWxWind(ox, t);    break;
    default:         pgWxStorm(ox, t);   break;
  }
}
