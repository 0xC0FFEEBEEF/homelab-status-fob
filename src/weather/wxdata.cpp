#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "wxdata.h"
#include "common/secrets.h"

Wx wx;

static const char *HOST = "swd.weatherflow.com";
static uint32_t lastTry = 0, heapLow = 0xFFFFFFFF;
static bool     mflnOk = false, mflnProbed = false;
static const uint32_t POLL_MS = 120000;     // the station reports each minute
static uint32_t lastHist = 0;

uint32_t wxHeapLow() { return heapLow; }

static void cp(char *d, size_t n, const char *s) {
  if (!s) { d[0] = 0; return; }
  strncpy(d, s, n - 1);
  d[n - 1] = 0;
}

void wxBegin() {
  memset(&wx, 0, sizeof wx);
  cp(wx.cond, sizeof wx.cond, "");
}

void wxUpdate() {
  const uint32_t now = millis();
  if (wx.ok && now - lastTry < POLL_MS) return;
  if (!wx.ok && now - lastTry < 15000)  return;
  if (WiFi.status() != WL_CONNECTED) return;
  lastTry = now;

  // Ask once whether the server will accept small TLS fragments. If it will, the
  // receive buffer drops from 16 KB to 1 KB, which is the difference between
  // this fitting and not.
  if (!mflnProbed) {
    mflnProbed = true;
    mflnOk = WiFiClientSecure::probeMaxFragmentLength(HOST, 443, 1024);
    Serial.printf("[wx] MFLN 1024 %s\n", mflnOk ? "supported" : "not supported");
  }

  // Keep only what is drawn. `hourly` is absent from the filter, so its ~240
  // entries are skipped while streaming and never allocated.
  //
  // Built with direct assignments rather than by holding a JsonObject while
  // inserting into it: the document grows as members are added, and a held
  // reference can be left dangling, which silently produced an empty filter and
  // a parse that returned nothing.
  JsonDocument filter;
  filter["current_conditions"]["air_temperature"] = true;
  filter["current_conditions"]["feels_like"] = true;
  filter["current_conditions"]["dew_point"] = true;
  filter["current_conditions"]["relative_humidity"] = true;
  filter["current_conditions"]["sea_level_pressure"] = true;
  filter["current_conditions"]["pressure_trend"] = true;
  filter["current_conditions"]["wind_avg"] = true;
  filter["current_conditions"]["wind_gust"] = true;
  filter["current_conditions"]["wind_direction"] = true;
  filter["current_conditions"]["wind_direction_cardinal"] = true;
  filter["current_conditions"]["uv"] = true;
  filter["current_conditions"]["solar_radiation"] = true;
  filter["current_conditions"]["brightness"] = true;
  filter["current_conditions"]["precip_accum_local_day"] = true;
  filter["current_conditions"]["precip_minutes_local_day"] = true;
  filter["current_conditions"]["precip_probability"] = true;
  filter["current_conditions"]["lightning_strike_count_last_1hr"] = true;
  filter["current_conditions"]["lightning_strike_count_last_3hr"] = true;
  filter["current_conditions"]["lightning_strike_last_distance"] = true;
  filter["current_conditions"]["conditions"] = true;
  filter["current_conditions"]["icon"] = true;
  filter["location_name"] = true;
  filter["latitude"] = true;
  filter["longitude"] = true;
  filter["forecast"]["daily"][0]["air_temp_high"] = true;
  filter["forecast"]["daily"][0]["air_temp_low"] = true;
  filter["forecast"]["daily"][0]["conditions"] = true;
  filter["forecast"]["daily"][0]["precip_probability"] = true;
  filter["forecast"]["daily"][0]["icon"] = true;

  if (filter.isNull() || filter["current_conditions"].isNull()) {
    wx.failures++;
    cp(wx.err, sizeof wx.err, "filter alloc failed");
    return;
  }

  WiFiClientSecure cli;
  cli.setInsecure();                       // no CA store; saves ~4KB
  if (mflnOk) cli.setBufferSizes(1024, 512);
  cli.setTimeout(9000);

  String url = String("https://") + HOST + "/swd/rest/better_forecast?station_id="
             + TEMPEST_STATION
             + "&units_temp=c&units_wind=mps&units_pressure=mb"
               "&units_precip=mm&units_distance=km";

  HTTPClient http;
  http.setTimeout(9000);
  // HTTP/1.0 so the server does not use chunked encoding. getStream() hands back
  // the raw socket, chunk-size headers and all, which ArduinoJson cannot parse -
  // it silently produced an empty document rather than an error.
  http.useHTTP10(true);
  if (!http.begin(cli, url)) { wx.failures++; cp(wx.err, sizeof wx.err, "begin failed"); return; }
  http.addHeader("Authorization", String("Bearer ") + TEMPEST_TOKEN);
  const int code = http.GET();
  Serial.printf("[wx] http %d, len %d, heap %u\n", code, http.getSize(), ESP.getFreeHeap());
  if (code != 200) {
    wx.failures++;
    snprintf(wx.err, sizeof wx.err, "http %d", code);
    http.end();
    return;
  }

  JsonDocument doc;
  const DeserializationError err =
    deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  const uint32_t h = ESP.getFreeHeap();
  if (h < heapLow) heapLow = h;
  http.end();

  if (err) { wx.failures++; cp(wx.err, sizeof wx.err, err.c_str()); return; }

  JsonObject cc = doc["current_conditions"];
  if (cc.isNull()) {
    wx.failures++;
    cp(wx.err, sizeof wx.err, "no conditions");
    // Say what did arrive, once, so this is diagnosable from serial.
    static bool told = false;
    if (!told) {
      told = true;
      Serial.print("[wx] parsed keys:");
      for (JsonPair kv : doc.as<JsonObject>()) Serial.printf(" %s", kv.key().c_str());
      Serial.println(doc.isNull() ? "  <null doc>" : "");
    }
    return;
  }

  cp(wx.loc,  sizeof wx.loc,  doc["location_name"] | "");
  cp(wx.cond, sizeof wx.cond, cc["conditions"] | "");
  cp(wx.icon, sizeof wx.icon, cc["icon"] | "");
  cp(wx.wc,   sizeof wx.wc,   cc["wind_direction_cardinal"] | "");
  wx.lat = doc["latitude"]  | 0.0f;
  wx.lon = doc["longitude"] | 0.0f;
  wx.tc  = cc["air_temperature"] | 0.0f;
  wx.fl  = cc["feels_like"]      | 0.0f;
  wx.dc  = cc["dew_point"]       | 0.0f;
  wx.mb  = cc["sea_level_pressure"] | 0.0f;
  wx.wm  = cc["wind_avg"]  | 0.0f;
  wx.gm  = cc["wind_gust"] | 0.0f;
  wx.uv  = cc["uv"] | 0.0f;
  wx.sr  = cc["solar_radiation"] | 0.0f;
  wx.rnd = cc["precip_accum_local_day"] | 0.0f;
  wx.skm = cc["lightning_strike_last_distance"] | 0.0f;
  wx.rh  = cc["relative_humidity"] | 0;
  wx.wd  = cc["wind_direction"] | 0;
  wx.bri = cc["brightness"] | 0;
  wx.s1  = cc["lightning_strike_count_last_1hr"] | 0;
  wx.s3  = cc["lightning_strike_count_last_3hr"] | 0;
  wx.pp  = cc["precip_probability"] | 0;
  wx.rmin = cc["precip_minutes_local_day"] | 0;
  const char *tr = cc["pressure_trend"] | "steady";
  wx.tr = !strcmp(tr, "falling") ? -1 : (!strcmp(tr, "rising") ? 1 : 0);

  wx.nDay = 0;
  for (JsonObject dd : doc["forecast"]["daily"].as<JsonArray>()) {
    if (wx.nDay >= WX_DAYS) break;
    wx.day[wx.nDay].hi = dd["air_temp_high"] | 0.0f;
    wx.day[wx.nDay].lo = dd["air_temp_low"]  | 0.0f;
    wx.day[wx.nDay].pp = dd["precip_probability"] | 0;
    cp(wx.day[wx.nDay].cond, sizeof wx.day[wx.nDay].cond, dd["conditions"] | "");
    cp(wx.day[wx.nDay].icon, sizeof wx.day[wx.nDay].icon, dd["icon"] | "");
    wx.nDay++;
  }

  if (!lastHist || now - lastHist > 300000) {        // one sample per 5 min
    lastHist = now;
    if (wx.nHist < WX_HIST) wx.histT[wx.nHist++] = (int8_t)(wx.tc + 0.5f);
    else { memmove(wx.histT, wx.histT + 1, WX_HIST - 1); wx.histT[WX_HIST - 1] = (int8_t)(wx.tc + 0.5f); }
  }

  wx.ok = true;
  wx.lastOkMs = now;
  wx.fetches++;
  wx.err[0] = 0;
}
