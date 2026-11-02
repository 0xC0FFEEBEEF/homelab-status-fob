// Live weather from a WeatherFlow Tempest.
//
// Uses the hub's LOCAL UDP broadcast on port 50222, which needs no token, no
// cloud round-trip, and delivers lightning strikes the moment they happen.
// The fob must sit on the same L2 segment as the hub - it joins the 2.4 GHz
// SSID the hub is on, which an ESP8266 is limited to anyway.
//
// Everything here is metric, as the hub sends it. Convert at the point of
// display; helpers at the bottom.
#pragma once
#include <Arduino.h>

struct WeatherNow {
  bool     live;             // a UDP packet has been parsed
  uint32_t lastPacketMs;

  float    tempC, dewC;
  uint8_t  rh;
  float    slpMb;            // sea-level-ish pressure
  int8_t   trend;            // -1 falling, 0 steady, +1 rising

  float    windMs;           // rapid_wind, updated every ~3 s
  float    gustMs;
  uint16_t windDirDeg;

  float    rainMmPerMin;
  uint8_t  precipType;       // 0 none, 1 rain, 2 hail, 3 both
  bool     raining;

  float    solarWm2;         // drives how high the sun sits
  float    uv;

  uint16_t strikes30m;
  float    strikeLastKm;
  uint32_t strikeLastMs;     // millis() when it landed, 0 = never
};

extern WeatherNow wx;

void weatherBegin();
void weatherUpdate(float dt);
bool wifiUp();
const char *windCardinal(uint16_t deg);

inline float cToF(float c)     { return c * 1.8f + 32.f; }
inline float msToMph(float ms) { return ms * 2.23694f; }
inline float mbToInHg(float mb){ return mb * 0.0295300f; }
inline float kmToMi(float km)  { return km * 0.621371f; }
inline float mmToIn(float mm)  { return mm * 0.0393701f; }
