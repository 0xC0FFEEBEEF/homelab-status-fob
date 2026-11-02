// Builds the flat metric table from Status and scores it for attention.
#include "ui.h"
#include "common/anim.h"

Metric  gM[METRIC_COUNT];
int16_t gJx = 0, gJy = 0;

static uint8_t  hist[METRIC_COUNT][MHIST];
static uint8_t  hHead = 0;
static bool     hFull = false;
static uint32_t lastSample = 0;

// Group ranges must stay in step with the fill order below.
static const uint8_t GRP_START[GROUP_COUNT + 1] = { 0, 7, 11, 14, 17, 23 };

const char *groupName(uint8_t grp) {
  static const char *n[GROUP_COUNT] = { "slerm", "pmox", "pwr", "sec", "svc" };
  return n[grp < GROUP_COUNT ? grp : 0];
}
uint8_t histCount() { return hFull ? MHIST : hHead; }
uint8_t histAt(uint8_t m, uint8_t i) {
  const uint8_t start = hFull ? hHead : 0;
  return hist[m][(uint8_t)((start + i) % MHIST)];
}

void metricsInit() {
  memset(hist, 0, sizeof hist);
  for (uint8_t i = 0; i < METRIC_COUNT; i++) gM[i].score = 0.f;
}

static void put(uint8_t i, const char *label, const char *unit, uint8_t grp,
                float value, float norm, float sev, bool alarm,
                const char *state = nullptr) {
  gM[i].label = label; gM[i].unit = unit; gM[i].group = grp;
  gM[i].value = value; gM[i].norm = clamp01(norm);
  gM[i].sev = clamp01(sev); gM[i].alarm = alarm; gM[i].state = state;
}

static void build() {
  uint8_t i = 0;
  // --- slerm ---
  put(i++, "cluster tok/s", "",  0, g.tokS, g.tokS / 120.f, 0.f, false);
  put(i++, "slurm queue",   "",  0, g.queue, g.queue / 8.f, g.queue / 8.f, g.queue >= 4);
  for (uint8_t n = 0; n < 5; n++) {
    static const char *nl[5] = { "head node", "node01 gpu", "node02 gpu", "node03", "node05 ram" };
    const Node &nd = g.nodes[n];
    put(i++, nl[n], nd.up ? "C" : "", 0, nd.up ? nd.temp : 0,
        nd.up ? (nd.temp - 30) / 55.f : 0.f,
        nd.up ? (nd.temp - 55) / 30.f : 1.f, !nd.up || nd.temp > 80,
        nd.up ? nullptr : "DOWN");
  }
  // --- proxmox ---
  put(i++, "byteme cpu",  "%", 1, g.bmCpu,  g.bmCpu  / 100.f, g.bmCpu  / 100.f, g.bmCpu  > 90);
  put(i++, "byteme mem",  "%", 1, g.bmRam,  g.bmRam  / 100.f, g.bmRam  / 100.f, g.bmRam  > 92);
  put(i++, "byteme2 cpu", "%", 1, g.bm2Cpu, g.bm2Cpu / 100.f, g.bm2Cpu / 100.f, g.bm2Cpu > 90);
  put(i++, "byteme2 mem", "%", 1, g.bm2Ram, g.bm2Ram / 100.f, g.bm2Ram / 100.f, g.bm2Ram > 92);
  // --- power ---
  put(i++, "ups load",   "W", 2, g.loadW, g.loadW / 500.f, g.loadW / 500.f, false);
  put(i++, "ups charge", "%", 2, g.battPct, g.battPct / 100.f,
      (100 - g.battPct) / 100.f, g.onBattery);
  put(i++, "ups runtime", "m", 2, g.runtimeMin, g.runtimeMin / 60.f,
      (60 - g.runtimeMin) / 60.f, g.onBattery && g.runtimeMin < 15);
  // --- wazuh ---
  put(i++, "open alerts", "", 3, g.alerts, g.alerts / 10.f, g.alerts / 6.f, g.alerts > 0);
  put(i++, "critical",    "", 3, g.sev[0], g.sev[0] / 6.f, g.sev[0] / 3.f, g.sev[0] > 0);
  put(i++, "agents up",   "", 3, g.agentsUp, (float)g.agentsUp / g.agentsTotal,
      1.f - (float)g.agentsUp / g.agentsTotal, g.agentsUp < g.agentsTotal);
  gM[i - 1].unit = g.agentsUp < g.agentsTotal ? "!" : "ok";
  // --- services ---
  for (uint8_t s = 0; s < 6; s++) {
    static const char *sl[6] = { "jellyfin", "nextcloud", "home asst", "technitium", "open webui", "wazuh api" };
    const Svc &sv = g.svcs[s];
    put(i++, sl[s], sv.up ? "ms" : "", 4, sv.up ? sv.ms : 0,
        sv.up ? sv.ms / 300.f : 1.f, sv.up ? sv.ms / 300.f : 1.f, !sv.up,
        sv.up ? nullptr : "DOWN");
  }
}

void metricsUpdate(float dt) {
  (void)dt;
  build();

  const uint32_t now = millis();
  if (now - lastSample > 500) {
    lastSample = now;
    for (uint8_t m = 0; m < METRIC_COUNT; m++)
      hist[m][hHead] = (uint8_t)(gM[m].norm * 255.f);
    hHead = (uint8_t)((hHead + 1) % MHIST);
    if (hHead == 0) hFull = true;
  }

  const uint8_t cnt = histCount();
  for (uint8_t m = 0; m < METRIC_COUNT; m++) {
    gM[m].rate = cnt > 4 ? (gM[m].norm - histAt(m, 0) / 255.f) : 0.f;
    // Attention: being broken beats being bad, which beats merely moving.
    gM[m].score = (gM[m].alarm ? 2.4f : 0.f)
                + gM[m].sev * 1.4f
                + fabsf(gM[m].rate) * 1.8f;
  }
}

// Hysteresis plus a dwell floor, or the hero would flicker between two
// near-equal candidates and be unreadable.
uint8_t metricsPick(uint8_t current, bool allowChange) {
  if (!allowChange) return current;
  uint8_t best = current;
  float   bestScore = gM[current].score + 0.35f;
  for (uint8_t m = 0; m < METRIC_COUNT; m++)
    if (gM[m].score > bestScore) { bestScore = gM[m].score; best = m; }
  return best;
}
