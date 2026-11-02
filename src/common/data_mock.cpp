// Mock data provider.
//
// THE SEAM: replace dataUpdate() with a real poll and the UI needs no
// changes. Suggested shape, one GET against an aggregator on the LAN:
//
//   {"tokS":70,"queue":2,"nodes":[{"n":"n01","up":1,"t":68}],
//    "bm":{"cpu":31,"ram":58},"bm2":{"cpu":12,"ram":44},"vms":9,"cts":4,
//    "batt":100,"onBatt":0,"loadW":214,"runMin":38,
//    "alerts":2,"sev":[0,1,1,6],"agents":[12,13],"last":"...",
//    "svcs":[{"n":"jelly","up":1,"ms":41}],"wan":{"up":1,"ms":18}}
//
#include "common/status.h"

Status g;

static uint32_t bootMs = 0;
static float evtAcc = 0.f, walkAcc = 0.f;

static float rnd(float a, float b) {
  return a + (float)random(0, 10001) / 10000.f * (b - a);
}
static uint8_t clampU8(float v, uint8_t lo, uint8_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return (uint8_t)v;
}

void dataInit() {
  randomSeed(ESP.getCycleCount());
  bootMs = millis();

  g.tokS = 68.f; g.queue = 1; g.model = "qwen3-coder-next";
  g.nodes[0] = { "head", 6,  41, true  };
  g.nodes[1] = { "n01",  71, 68, true  };
  g.nodes[2] = { "n02",  64, 71, true  };
  g.nodes[3] = { "n03",  12, 44, true  };
  g.nodes[4] = { "n05",  31, 39, true  };
  g.nodeCount = 5;

  g.bmCpu = 28; g.bmRam = 61; g.bm2Cpu = 14; g.bm2Ram = 47;
  g.vms = 9; g.cts = 4;

  g.battPct = 100; g.onBattery = false; g.loadW = 214; g.runtimeMin = 38;

  g.alerts = 0;
  g.sev[0] = 0; g.sev[1] = 1; g.sev[2] = 2; g.sev[3] = 7;
  g.agentsUp = 12; g.agentsTotal = 13;
  strncpy(g.lastAlert, "no active alerts", sizeof g.lastAlert - 1);

  g.svcs[0] = { "jelly",  true, 41 };
  g.svcs[1] = { "ncloud", true, 18 };
  g.svcs[2] = { "hass",   true, 9  };
  g.svcs[3] = { "dns",    true, 3  };
  g.svcs[4] = { "owui",   true, 26 };
  g.svcs[5] = { "wazuh",  true, 12 };
  g.svcCount = 6;

  g.rssi = -58; g.wanUp = true; g.wanMs = 17;
  g.uptimeMin = 6 * 1440 + 4 * 60;
  g.hh = 20; g.mm = 14;
}

void dataUpdate(uint32_t nowMs, float dt) {
  // Clock ticks from boot; NTP replaces this once wifi is wired up.
  const uint32_t mins = (nowMs - bootMs) / 60000UL;
  const uint16_t base = 20 * 60 + 14;
  g.hh = (uint8_t)(((base + mins) / 60) % 24);
  g.mm = (uint8_t)((base + mins) % 60);
  g.uptimeMin = 6 * 1440 + 4 * 60 + mins;

  // Continuous drift, so every gauge is always in motion.
  walkAcc += dt;
  if (walkAcc > 0.45f) {
    walkAcc = 0.f;
    // constrain() is a macro and evaluates its argument repeatedly, so a
    // random() call inside it gets drawn more than once and the clamp compares
    // a different number than it returns. Always step into a local first.
    const float tok = g.tokS + rnd(-7.f, 7.f);
    g.tokS = constrain(tok, 8.f, 104.f);
    g.bmCpu  = clampU8(g.bmCpu  + rnd(-6.f, 6.f), 3, 99);
    g.bmRam  = clampU8(g.bmRam  + rnd(-2.f, 2.f), 20, 96);
    g.bm2Cpu = clampU8(g.bm2Cpu + rnd(-5.f, 5.f), 2, 99);
    g.bm2Ram = clampU8(g.bm2Ram + rnd(-2.f, 2.f), 15, 94);
    const float w = (float)g.loadW + rnd(-18.f, 18.f);
    g.loadW = (uint16_t)constrain(w, 120.f, 480.f);
    const float rs = (float)g.rssi + rnd(-3.f, 3.f);
    g.rssi = (int8_t)constrain(rs, -88.f, -42.f);
    for (uint8_t i = 0; i < g.nodeCount; i++)
      if (g.nodes[i].up) {
        const float tp = (float)g.nodes[i].temp + rnd(-2.f, 2.f);
        g.nodes[i].temp = (int8_t)constrain(tp, 33.f, 84.f);
      }
    for (uint8_t i = 0; i < g.svcCount; i++)
      if (g.svcs[i].up) {
        const float ms = (float)g.svcs[i].ms + rnd(-6.f, 6.f);
        g.svcs[i].ms = (uint16_t)constrain(ms, 2.f, 320.f);
      }
    g.queue = (uint8_t)random(0, 4);
  }

  // Occasional events, so the alert states and DOWN glyphs actually show up.
  evtAcc += dt;
  if (evtAcc > 9.f) {
    evtAcc = 0.f;
    switch (random(0, 6)) {
      case 0: {
        const uint8_t i = (uint8_t)random(0, g.svcCount);
        g.svcs[i].up = !g.svcs[i].up;
        if (!g.svcs[i].up) {
          g.alerts++;
          g.sev[1]++;
          snprintf(g.lastAlert, sizeof g.lastAlert, "%s unreachable from wazuh probe", g.svcs[i].name);
        }
        break;
      }
      case 1: {
        const uint8_t i = (uint8_t)random(1, g.nodeCount);
        g.nodes[i].up = !g.nodes[i].up;
        if (!g.nodes[i].up) {
          g.alerts++;
          g.sev[0]++;
          snprintf(g.lastAlert, sizeof g.lastAlert, "slerm-%s rpc worker stopped responding", g.nodes[i].name);
        }
        break;
      }
      case 2:
        g.alerts++;
        g.sev[3]++;
        strncpy(g.lastAlert, "sshd: 4 failed logins from 192.0.2.54", sizeof g.lastAlert - 1);
        break;
      case 3:
        g.onBattery = !g.onBattery;
        if (g.onBattery) {
          g.alerts++;
          g.sev[0]++;
          strncpy(g.lastAlert, "byteme2 UPS on battery - mains lost", sizeof g.lastAlert - 1);
        }
        break;
      case 4:
        if (g.alerts) { g.alerts--; if (g.sev[3]) g.sev[3]--; }
        if (!g.alerts) strncpy(g.lastAlert, "no active alerts", sizeof g.lastAlert - 1);
        break;
      default:
        g.agentsUp = (g.agentsUp == g.agentsTotal) ? (uint8_t)(g.agentsTotal - 1) : g.agentsTotal;
        break;
    }
  }

  // Battery follows the mains state.
  if (g.onBattery) {
    if (g.battPct > 1 && random(0, 30) == 0) g.battPct--;
    g.runtimeMin = (uint16_t)(g.battPct * 0.42f);
  } else {
    if (g.battPct < 100 && random(0, 40) == 0) g.battPct++;
    g.runtimeMin = (uint16_t)(g.battPct * 0.42f);
  }
}
