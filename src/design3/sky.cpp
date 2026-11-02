// The yellow band: a living sky driven by the Tempest.
#include "ui.h"
#include "common/anim.h"

static Tween tSunX, tSunY, tCloud;
static bool  ready = false;

// Deterministic per-index jitter, so clouds and stars keep their shapes.
static inline uint8_t hash8(uint8_t i) { return (uint8_t)((i * 73u + 41u) * 17u); }

void skyDraw(float t, bool tall) {
  const int16_t horizon = tall ? 46 : SKY_Y1;     // takeover borrows more sky

  if (!ready) { ready = true; tSunX.snap(64); tSunY.snap(8); tCloud.snap(0); }

  // --- sun or moon ---------------------------------------------------------
  // Height comes from MEASURED solar radiation rather than an almanac, so the
  // sun genuinely tracks the day the sensor is seeing. Horizontal position is
  // time of day across the band.
  const float hr = g.hh + g.mm / 60.f;
  const float dayFrac = clamp01((hr - 6.f) / 14.f);       // 06:00 -> 20:00
  const bool  daylight = wx.solarWm2 > 12.f;
  const float solarN = clamp01(wx.solarWm2 / 850.f);

  tSunX.rate = 1.2f; tSunY.rate = 1.2f;
  tSunX.set(6.f + dayFrac * 112.f);
  tSunY.set(daylight ? (horizon - 2.f - solarN * (horizon - 6.f)) : 5.f);
  tSunX.update(0.02f); tSunY.update(0.02f);

  const int16_t sx = (int16_t)tSunX.v, sy = (int16_t)tSunY.v;
  if (daylight) {
    dsp->drawDisc(sx, sy, 3);
    // Rays lengthen with irradiance.
    if (solarN > 0.35f) {
      const uint8_t r = (uint8_t)(5 + solarN * 2.f);
      for (uint8_t a = 0; a < 8; a++) {
        const float ang = a * PI / 4.f + t * 0.25f;
        dsp->drawPixel((int16_t)(sx + cosf(ang) * r), (int16_t)(sy + sinf(ang) * r));
      }
    }
  } else {
    // Crescent: a disc with a second disc knocked out of it.
    dsp->drawDisc(sx, sy, 3);
    dsp->setDrawColor(0);
    dsp->drawDisc((int16_t)(sx + 2), (int16_t)(sy - 1), 3);
    dsp->setDrawColor(1);
    for (uint8_t i = 0; i < 7; i++) {          // stars, twinkling
      const int16_t stx = (int16_t)(8 + (hash8(i) % 112));
      const int16_t sty = (int16_t)(1 + (hash8((uint8_t)(i + 40)) % (horizon - 2)));
      if (((uint16_t)(t * 2.f) + i * 3) % 7) dsp->drawPixel(stx, sty);
    }
  }

  // --- clouds -------------------------------------------------------------
  // Drift speed is the real wind speed; direction follows the real bearing, so
  // an easterly genuinely pushes them the other way.
  const bool westerly = wx.windDirDeg > 180;
  const float driftPxS = 1.2f + msToMph(wx.windMs) * 0.9f;
  tCloud.v += driftPxS * 0.02f * (westerly ? 1.f : -1.f);
  if (tCloud.v > 512.f)  tCloud.v -= 512.f;
  if (tCloud.v < -512.f) tCloud.v += 512.f;

  // Cloudiness: humid air and suppressed irradiance both mean more cover.
  uint8_t puffs = 0;
  if (wx.rh > 55) puffs++;
  if (wx.rh > 72) puffs++;
  if (wx.raining) puffs += 2;
  if (daylight && solarN < 0.35f) puffs++;
  if (puffs > 4) puffs = 4;

  for (uint8_t c = 0; c < puffs; c++) {
    const int16_t base = (int16_t)(hash8((uint8_t)(c + 7)) % 128);
    int16_t cx = (int16_t)(base + (int16_t)tCloud.v % 128);
    while (cx > 140) cx -= 168;
    while (cx < -40) cx += 168;
    const int16_t cy = (int16_t)(2 + (hash8((uint8_t)(c + 19)) % (uint8_t)max(3, horizon - 8)));
    // Three overlapping discs read as a cloud even at this size.
    dsp->drawDisc(cx, cy, 2);
    dsp->drawDisc((int16_t)(cx + 3), (int16_t)(cy + 1), 3);
    dsp->drawDisc((int16_t)(cx + 7), cy, 2);
  }

  // --- rain ---------------------------------------------------------------
  // Streaks fall out of the yellow band and down across the colour boundary,
  // which is the whole point of putting the sky up there.
  if (wx.raining || wx.rainMmPerMin > 0.f) {
    const uint8_t drops = (uint8_t)(10 + clamp01(wx.rainMmPerMin / 2.f) * 22.f);
    const float fall = t * (34.f + clamp01(wx.rainMmPerMin) * 40.f);
    const int16_t lean = (int16_t)(msToMph(wx.windMs) > 8.f ? 1 : 0);
    for (uint8_t d = 0; d < drops; d++) {
      const int16_t dx = (int16_t)(hash8((uint8_t)(d + 3)) % 128);
      const uint8_t span = (uint8_t)(tall ? 58 : 34);
      const int16_t dy = (int16_t)((int32_t)(fall + hash8(d) % span) % span);
      dsp->drawPixel((int16_t)(dx + (lean ? dy / 8 : 0)), dy);
      dsp->drawPixel((int16_t)(dx + (lean ? dy / 8 : 0)), (int16_t)(dy + 1));
    }
  }

  // --- readouts, knocked out of the art so they stay legible --------------
  // 6x10, not 4x6: on a 22mm-wide panel a 4x6 glyph is under a millimetre
  // tall and simply cannot be read at a glance, which is the whole job here.
  if (!tall) {
    dsp->setFont(u8g2_font_6x10_tf);

    char clk[8];
    snprintf(clk, sizeof clk, "%02u:%02u", g.hh, g.mm);
    const uint8_t cw = dsp->getStrWidth(clk);
    dsp->setDrawColor(0);
    dsp->drawBox(0, 4, (uint8_t)(cw + 3), 11);
    dsp->setDrawColor(1);
    dsp->drawStr(1, 14, clk);

    char tmp[10];
    snprintf(tmp, sizeof tmp, "%dF", (int)(cToF(wx.tempC) + 0.5f));
    const uint8_t w = dsp->getStrWidth(tmp);
    dsp->setDrawColor(0);
    dsp->drawBox((int16_t)(125 - w), 4, (uint8_t)(w + 3), 11);
    dsp->setDrawColor(1);
    dsp->drawStr((int16_t)(126 - w), 14, tmp);
  }
}
