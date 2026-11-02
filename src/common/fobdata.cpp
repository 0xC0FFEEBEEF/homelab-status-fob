#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "common/fobdata.h"

FobData fd;

static uint32_t lastTry = 0;
static const uint32_t POLL_MS      = 3000;
static const uint32_t POLL_RUN_MS  = 1000;   // faster while a run is live

static void copyStr(char *dst, size_t n, const char *src) {
  if (!src) { dst[0] = 0; return; }
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

static void readGuests(JsonArray arr, Guest *out, uint8_t &count) {
  count = 0;
  for (JsonObject o : arr) {
    if (count >= MAX_GUESTS) break;
    Guest &gu = out[count];
    gu.id  = o["id"] | 0;
    copyStr(gu.name, sizeof gu.name, o["n"] | "?");
    gu.cpu = o["cpu"] | 0.0f;
    gu.nc  = o["nc"]  | 0;
    gu.mem = o["mem"] | 0.0f;
    gu.mgb = o["mgb"] | 0.0f;
    gu.up  = o["up"]  | 0UL;
    count++;
  }
}

void fobDataBegin() {
  memset(&fd, 0, sizeof fd);
}

void fobDataUpdate() {
  const uint32_t now = millis();
  const uint32_t every = fd.run.active ? POLL_RUN_MS : POLL_MS;
  if (now - lastTry < every) return;
  lastTry = now;
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient net;
  HTTPClient http;
  net.setTimeout(4000);
  if (!http.begin(net, FOB_API_HOST, FOB_API_PORT, FOB_API_PATH)) return;
  http.setTimeout(4000);
  const int code = http.GET();
  if (code != 200) { http.end(); fd.failures++; return; }

  // Filter the parse: only the keys the fob actually renders, which keeps the
  // document small enough to sit comfortably in heap.
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) { fd.failures++; return; }

  JsonObject u = doc["ups"];
  copyStr(fd.ups.state,     sizeof fd.ups.state,     u["st"]  | "");
  copyStr(fd.ups.src,       sizeof fd.ups.src,       u["src"] | "");
  copyStr(fd.ups.model,     sizeof fd.ups.model,     u["mdl"] | "");
  copyStr(fd.ups.lastEvent, sizeof fd.ups.lastEvent, u["le"]  | "");
  fd.ups.iv = u["iv"] | 0.0f;  fd.ups.ov   = u["ov"]  | 0.0f;
  fd.ups.batt = u["b"] | 0.0f; fd.ups.rt   = u["rt"]  | 0.0f;
  fd.ups.load = u["ld"] | 0.0f; fd.ups.pct = u["pct"] | 0.0f;
  fd.ups.va = u["va"] | 0.0f;
  fd.ups.online = u["ol"] | 0;  fd.ups.conn = u["cn"] | 0;

  JsonObject h = doc["host"];
  fd.host.l1  = h["l1"]  | 0.0f;
  fd.host.mem = h["mem"] | 0.0f;
  fd.host.tmp = h["tmp"] | 0.0f;
  fd.host.up  = h["up"]  | 0UL;
  fd.host.cpu = h["cpu"] | 0;

  copyStr(fd.gdMode, sizeof fd.gdMode, doc["gd"]["mode"] | "");
  fd.gdPowerfail = doc["gd"]["pf"] | 0;

  readGuests(doc["vms"].as<JsonArray>(), fd.vms, fd.vmCount);
  readGuests(doc["cts"].as<JsonArray>(), fd.cts, fd.ctCount);

  JsonObject w = doc["wx"];
  fd.wx.tc = w["tc"] | 0.0f;  fd.wx.fl = w["fl"] | 0.0f;
  fd.wx.dc = w["dc"] | 0.0f;  fd.wx.mb = w["mb"] | 0.0f;
  fd.wx.wm = w["wm"] | 0.0f;  fd.wx.gm = w["gm"] | 0.0f;
  fd.wx.uv = w["uv"] | 0.0f;  fd.wx.sr = w["sr"] | 0.0f;
  fd.wx.rn = w["rn"] | 0.0f;  fd.wx.rnd = w["rnd"] | 0.0f;
  fd.wx.skm = w["skm"] | 0.0f;
  fd.wx.rh = w["rh"] | 0;     fd.wx.wd = w["wd"] | 0;
  fd.wx.bri = w["bri"] | 0;   fd.wx.s1 = w["s1"] | 0;
  fd.wx.s3 = w["s3"] | 0;     fd.wx.tr = w["tr"] | 0;
  fd.wx.age = w["age"] | 0UL;

  JsonObject r = doc["run"];
  fd.run.active = r["active"] | 0;
  copyStr(fd.run.kind,  sizeof fd.run.kind,  r["kind"]  | "");
  copyStr(fd.run.phase, sizeof fd.run.phase, r["phase"] | "");
  fd.run.ec = r["ec"] | -1;
  fd.run.started = r["st"] | 0UL;
  fd.run.tlCount = 0;
  for (JsonObject e : r["tl"].as<JsonArray>()) {
    if (fd.run.tlCount >= MAX_TL) break;
    copyStr(fd.run.tlKey[fd.run.tlCount],   14, e["k"] | "");
    copyStr(fd.run.tlLabel[fd.run.tlCount], 34, e["l"] | "");
    const uint32_t ts = e["ts"] | 0UL;
    fd.run.tlOff[fd.run.tlCount] =
      (ts && fd.run.started && ts >= fd.run.started)
        ? (uint16_t)min<uint32_t>(ts - fd.run.started, 65000) : 0;
    fd.run.tlCount++;
  }
  fd.run.lineCount = 0;
  for (JsonVariant v : r["lines"].as<JsonArray>()) {
    if (fd.run.lineCount >= MAX_LINES) break;
    copyStr(fd.run.lines[fd.run.lineCount], 52, v.as<const char *>());
    fd.run.lineCount++;
  }

  JsonObject rp = doc["rep"];
  fd.rep.req      = rp["req"] | 0UL;
  fd.rep.started  = rp["st"]  | 0UL;
  fd.rep.finished = rp["fin"] | 0UL;
  fd.rep.ec       = rp["ec"]  | 0;
  fd.rep.n = 0;
  for (JsonObject e : rp["tl"].as<JsonArray>()) {
    if (fd.rep.n >= MAX_TL) break;
    copyStr(fd.rep.label[fd.rep.n], 34, e["l"] | "");
    fd.rep.off[fd.rep.n] = e["o"] | 0;
    fd.rep.n++;
  }

  JsonObject sq = doc["seq"];
  copyStr(fd.seq.kind, sizeof fd.seq.kind, sq["kind"] | "none");
  fd.seq.dur    = sq["dur"]    | 0;
  fd.seq.budget = sq["budget"] | 240;
  fd.seq.live    = sq["live"] | 0;
  fd.seq.modeled = sq["mod"]  | 0;
  fd.seq.nG = 0;
  for (JsonObject e : sq["guests"].as<JsonArray>()) {
    if (fd.seq.nG >= 12) break;
    const char *ty = e["t"] | "vm";
    fd.seq.g[fd.seq.nG].kind = (ty[0] == 'c') ? 1 : 0;
    fd.seq.g[fd.seq.nG].id   = e["id"] | 0;
    fd.seq.g[fd.seq.nG].off  = e["o"]  | -1;
    fd.seq.g[fd.seq.nG].up     = e["u"]  | -1;
    fd.seq.g[fd.seq.nG].grace  = e["gr"] | 45;
    fd.seq.g[fd.seq.nG].used   = e["us"] | 0;
    fd.seq.g[fd.seq.nG].agent  = e["ag"] | 1;
    fd.seq.g[fd.seq.nG].forced = e["fx"] | 0;
    fd.seq.nG++;
  }
  fd.seq.nS = 0;
  for (JsonObject e : sq["stages"].as<JsonArray>()) {
    if (fd.seq.nS >= 9) break;
    copyStr(fd.seq.s[fd.seq.nS].k, 10, e["k"] | "");
    fd.seq.s[fd.seq.nS].off = e["o"] | 0;
    fd.seq.nS++;
  }

  fd.seq.nl = 0;
  for (JsonVariant v : sq["lo"].as<JsonArray>()) {
    if (fd.seq.nl >= 80) break;
    fd.seq.lo[fd.seq.nl++] = v.as<uint16_t>();
  }
  fd.pos.i   = doc["pos"]["i"]   | 0;
  fd.pos.n   = doc["pos"]["n"]   | 0;
  fd.pos.age = doc["pos"]["age"] | 9999;

  fd.ok = true;
  fd.lastOkMs = now;
  fd.fetches++;
}
