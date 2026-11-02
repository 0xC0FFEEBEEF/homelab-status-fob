#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <time.h>
#include "common/weather.h"
#include "common/status.h"
#include "common/secrets.h"

WeatherNow wx;

static WiFiUDP   udp;
static bool      udpOpen = false;
static float     mockAcc = 0.f;
static uint32_t  lastWifiTry = 0;

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }

const char *windCardinal(uint16_t deg) {
  static const char *c[16] = { "N","NNE","NE","ENE","E","ESE","SE","SSE",
                               "S","SSW","SW","WSW","W","WNW","NW","NNW" };
  return c[(uint8_t)(((deg + 11) % 360) / 22.5f) & 15];
}

void weatherBegin() {
  // Seeded from the station's actual readings so the panel looks right from
  // the first frame, before any UDP packet has arrived.
  wx.live = false; wx.lastPacketMs = 0;
  wx.tempC = 31.1f; wx.dewC = 23.0f; wx.rh = 62;
  wx.slpMb = 1011.7f; wx.trend = 0;
  wx.windMs = 0.3f; wx.gustMs = 0.4f; wx.windDirDeg = 317;
  wx.rainMmPerMin = 0.f; wx.precipType = 0; wx.raining = false;
  wx.solarWm2 = 34.f; wx.uv = 0.34f;
  wx.strikes30m = 0; wx.strikeLastKm = 40.f; wx.strikeLastMs = 0;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);   // non-blocking; the UI runs regardless
  lastWifiTry = millis();
  Serial.printf("[wx] joining %s\n", WIFI_SSID);
}

static void parsePacket(const char *buf, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, buf, len)) return;
  const char *type = doc["type"];
  if (!type) return;

  if (!strcmp(type, "rapid_wind")) {
    JsonArray ob = doc["ob"];
    if (ob.size() >= 3) {
      wx.windMs     = ob[1].as<float>();
      wx.windDirDeg = ob[2].as<uint16_t>();
    }
  } else if (!strcmp(type, "obs_st")) {
    JsonArray o = doc["obs"][0];
    if (o.size() >= 18) {
      wx.gustMs      = o[3].as<float>();
      wx.windDirDeg  = o[4].as<uint16_t>();
      wx.slpMb       = o[6].as<float>();
      wx.tempC       = o[7].as<float>();
      wx.rh          = o[8].as<uint8_t>();
      wx.uv          = o[10].as<float>();
      wx.solarWm2    = o[11].as<float>();
      const float mm = o[12].as<float>();
      const float iv = o[17].as<float>();          // report interval, minutes
      wx.rainMmPerMin = iv > 0.f ? mm / iv : 0.f;
      wx.precipType  = o[13].as<uint8_t>();
      wx.raining     = wx.precipType != 0 || wx.rainMmPerMin > 0.f;
      wx.strikes30m  = o[15].as<uint16_t>();
      // Magnus approximation, so dew point needs no extra packet.
      const float a = 17.62f, b = 243.12f;
      const float g = logf(wx.rh / 100.f) + (a * wx.tempC) / (b + wx.tempC);
      wx.dewC = (b * g) / (a - g);
    }
  } else if (!strcmp(type, "evt_strike")) {
    JsonArray e = doc["evt"];
    if (e.size() >= 2) {
      wx.strikeLastKm = e[1].as<float>();
      wx.strikeLastMs = millis();
      wx.strikes30m++;
      Serial.printf("[wx] STRIKE %.1f km\n", wx.strikeLastKm);
    }
  } else if (!strcmp(type, "evt_precip")) {
    wx.raining = true;
  } else {
    return;
  }
  wx.live = true;
  wx.lastPacketMs = millis();
}

void weatherUpdate(float dt) {
  const uint32_t now = millis();

  if (!wifiUp()) {
    if (now - lastWifiTry > 20000) { lastWifiTry = now; WiFi.reconnect(); }
  } else if (!udpOpen) {
    udpOpen = udp.begin(50222);
    configTime(FOB_TZ, "pool.ntp.org", "time.nist.gov");
    Serial.printf("[wx] wifi ok %s, udp:50222 %s\n",
                  WiFi.localIP().toString().c_str(), udpOpen ? "listening" : "FAILED");
  }

  if (udpOpen) {
    // Drain whatever arrived; rapid_wind lands every ~3 s.
    for (uint8_t i = 0; i < 4; i++) {
      const int sz = udp.parsePacket();
      if (sz <= 0) break;
      static char buf[900];
      const int n = udp.read(buf, sizeof buf - 1);
      if (n > 0) { buf[n] = 0; parsePacket(buf, (size_t)n); }
    }
  }

  // No hub in earshot: keep the sky alive so the design still reads, and say
  // so on screen rather than passing invented numbers off as real.
  if (!wx.live || now - wx.lastPacketMs > 120000) {
    wx.live = false;
    mockAcc += dt;
    if (mockAcc > 1.0f) {
      mockAcc = 0.f;
      const float w = wx.windMs + random(-30, 31) / 100.f;
      const float c = wx.tempC  + random(-10, 11) / 100.f;
      wx.windMs = constrain(w, 0.f, 9.f);
      wx.tempC  = constrain(c, -20.f, 45.f);
      wx.windDirDeg = (uint16_t)((wx.windDirDeg + random(-6, 7) + 360) % 360);
      // Track a plausible diurnal curve so the sun actually moves.
      time_t tt = time(nullptr);
      struct tm lt;
      localtime_r(&tt, &lt);
      const float hr = lt.tm_hour + lt.tm_min / 60.f;
      const float day = cosf((hr - 13.f) / 12.f * PI);
      wx.solarWm2 = day > 0.f ? day * 900.f : 0.f;
      wx.uv = day > 0.f ? day * 7.f : 0.f;
    }
  }

  // Clock: NTP when we have it, otherwise keep the mock ticking.
  time_t tt = time(nullptr);
  if (tt > 1700000000) {
    struct tm lt;
    localtime_r(&tt, &lt);
    g.hh = (uint8_t)lt.tm_hour;
    g.mm = (uint8_t)lt.tm_min;
  }
}
