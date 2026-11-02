// Full-screen takeover. Deliberately not weather-only: a UPS on battery or a
// downed node earns the screen exactly as much as a nearby strike.
#include "ui.h"
#include "common/anim.h"

EventKind eventPick() {
  if (wx.strikeLastMs && millis() - wx.strikeLastMs < 15000 && kmToMi(wx.strikeLastKm) < 12.f)
    return EV_STRIKE;
  if (g.onBattery)                                              return EV_POWER;
  if (g.sev[0] > 0)                                             return EV_SECURITY;
  for (uint8_t i = 0; i < g.nodeCount; i++) if (!g.nodes[i].up) return EV_NODE;
  if (wx.rainMmPerMin > 1.2f)                                   return EV_RAIN;
  return EV_NONE;
}

void eventDraw(EventKind k, float t, float age) {
  char big[16], sub[32];
  const char *title;

  switch (k) {
    case EV_STRIKE:
      title = "LIGHTNING";
      snprintf(big, sizeof big, "%d mi", (int)(kmToMi(wx.strikeLastKm) + 0.5f));
      snprintf(sub, sizeof sub, "%u strikes 30min", wx.strikes30m);
      break;
    case EV_RAIN:
      title = "HEAVY RAIN";
      snprintf(big, sizeof big, "%d.%02d\"", (int)(mmToIn(wx.rainMmPerMin) * 60.f),
               (int)(mmToIn(wx.rainMmPerMin) * 6000.f) % 100);
      snprintf(sub, sizeof sub, "per hour  %u%%rh", wx.rh);
      break;
    case EV_POWER:
      title = "ON BATTERY";
      snprintf(big, sizeof big, "%um", g.runtimeMin);
      snprintf(sub, sizeof sub, "%u%% left  %uW draw", g.battPct, g.loadW);
      break;
    case EV_NODE: {
      title = "NODE DOWN";
      const char *who = "?";
      for (uint8_t i = 0; i < g.nodeCount; i++) if (!g.nodes[i].up) { who = g.nodes[i].name; break; }
      snprintf(big, sizeof big, "%s", who);
      snprintf(sub, sizeof sub, "slerm rpc worker gone");
      break;
    }
    default:
      title = "CRITICAL";
      snprintf(big, sizeof big, "%u", g.sev[0]);
      snprintf(sub, sizeof sub, "%u open  %u/%u agents", g.alerts, g.agentsUp, g.agentsTotal);
      break;
  }

  if (k == EV_STRIKE || k == EV_RAIN) skyDraw(t, true);

  dsp->setDrawColor(0);
  dsp->drawBox(0, 17, SCR_W, 40);
  dsp->setDrawColor(1);
  dsp->drawFrame(0, 17, SCR_W, 40);

  dsp->setFont(u8g2_font_6x10_tf);
  dsp->drawStr((int16_t)(64 - dsp->getStrWidth(title) / 2), 28, title);
  dsp->setFont(u8g2_font_logisoso16_tr);
  dsp->drawStr((int16_t)(64 - dsp->getStrWidth(big) / 2), 47, big);
  dsp->setFont(u8g2_font_6x10_tf);
  dsp->drawStr((int16_t)(64 - dsp->getStrWidth(sub) / 2), 62, sub);

  const float intro = clamp01(age * 4.f);
  if (intro < 1.f) {
    dsp->setDrawColor(0);
    dsp->drawBox((int16_t)(intro * 128.f), 17, (uint8_t)(128 - intro * 128.f), 40);
    dsp->setDrawColor(1);
  }
  dsp->drawHLine(0, 16, (uint8_t)(128.f * clamp01(1.f - age / 15.f)));
}
