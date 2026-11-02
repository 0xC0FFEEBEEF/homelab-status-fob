// Weather straight from the WeatherFlow Tempest cloud API.
//
// Deliberately NOT via the homelab aggregator: this board roams, so it must work
// on any WiFi with no dependency on the home LAN. That means TLS on the ESP8266,
// which is the expensive part - BearSSL wants a 16 KB receive buffer unless the
// server negotiates a smaller max fragment length, so we probe for that first.
//
// better_forecast returns ~240 hourly entries and will never fit in RAM, so the
// response is stream-parsed through an ArduinoJson filter that keeps current
// conditions and three daily entries and lets everything else fly past.
#pragma once
#include <Arduino.h>

static const uint8_t WX_DAYS  = 3;
static const uint8_t WX_HIST  = 40;     // ~3.3 h at one sample per 5 min

struct WxDay { float hi, lo; uint8_t pp; char cond[18], icon[22]; };

struct Wx {
  bool     ok;
  uint32_t lastOkMs, fetches, failures;
  char     err[28];

  char     loc[20], cond[24], icon[28], wc[5];
  float    tc, fl, dc, mb, wm, gm, uv, sr, rnd, skm, lat, lon;
  uint16_t rh, wd, bri, s1, s3, pp, rmin;
  int8_t   tr;
  uint32_t age;

  WxDay    day[WX_DAYS];
  uint8_t  nDay;

  int8_t   histT[WX_HIST];   // whole degrees C
  uint8_t  nHist;
};

extern Wx wx;

void wxBegin();
void wxUpdate();            // call every frame; self-paces
uint32_t wxHeapLow();       // lowest heap seen during a fetch
