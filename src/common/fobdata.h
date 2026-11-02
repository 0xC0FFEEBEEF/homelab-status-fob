// Live homelab + weather data, fetched as one compact JSON from fob-api on
// byteme2 (http://192.0.2.2:8788/fob.json, plain HTTP - no TLS on an ESP8266).
//
// That service aggregates Proxmox, the UPS dashboard API and the WeatherFlow
// cloud, so the fob makes exactly one request and parses ~2.7 KB.
#pragma once
#include <Arduino.h>

#define FOB_API_HOST "192.0.2.2"
#define FOB_API_PORT 8788
#define FOB_API_PATH "/fob.json"

static const uint8_t MAX_GUESTS = 8;
static const uint8_t MAX_TL     = 12;
static const uint8_t MAX_LINES  = 5;

struct Guest {
  uint16_t id;
  char     name[15];
  float    cpu;       // percent
  uint8_t  nc;        // cores
  float    mem;       // percent of its max
  float    mgb;       // max mem, GB
  uint32_t up;        // seconds
};

struct UpsData {
  char    state[22], src[18], model[18];
  float   iv, ov, batt, rt, load, pct, va;
  uint8_t online, conn;
  char    lastEvent[46];
};

struct HostData { float l1, mem, tmp; uint32_t up; uint8_t cpu; };

struct WxData {
  float    tc, fl, dc, mb, wm, gm, uv, sr, rn, rnd, skm;
  uint16_t rh, wd, bri, s1, s3;
  int8_t   tr;
  uint32_t age;
};

struct RunData {
  uint8_t active;
  char    kind[6], phase[16];
  int8_t  ec;
  uint32_t started;
  uint8_t tlCount;
  uint16_t tlOff[MAX_TL];        // seconds from run start, for honest replay
  char    tlKey[MAX_TL][14];
  char    tlLabel[MAX_TL][34];
  uint8_t lineCount;
  char    lines[MAX_LINES][52];
};

// The last real recovery test, kept apart from `run` so a 7-second dry run with
// no timeline can never displace the only thing worth replaying.
struct ReplayData {
  uint32_t req;                  // bumped by GET /replay -> replay immediately
  uint32_t started, finished;
  int8_t   ec;
  uint8_t  n;
  uint16_t off[MAX_TL];          // seconds from run start
  char     label[MAX_TL][34];
};

// A graphical outage sequence: what stopped when. The aggregator parses the run
// log so the fob can animate the rack draining rather than listing stage labels.
struct SeqEvent {
  uint8_t  kind;          // 0 vm, 1 ct
  uint16_t id;
  int16_t  off, up;       // stopped / restarted, seconds from run start
  uint8_t  grace, used;   // grace window and how much of it was consumed
  uint8_t  agent, forced; // guest agent answered / it blew through its grace
};
struct SeqStage { char k[10]; uint16_t off; };

struct SeqData {
  char     kind[6];        // "dry" | "real"
  uint16_t dur, budget;
  uint8_t  live, modeled;  // modeled = the recovery half is synthesised
  uint8_t  nG, nS;
  SeqEvent g[12];
  SeqStage s[9];
  uint8_t  nl;             // log-line count
  uint16_t lo[80];         // each line's offset from run start, seconds
};

// Where the dashboard's own replay currently is. The fob follows this rather
// than running a second clock, or the two screens drift apart.
struct PosData { uint16_t i, n, age; };

struct FobData {
  bool     ok;              // a fetch has succeeded
  uint32_t lastOkMs;
  uint16_t fetches, failures;
  UpsData  ups;
  HostData host;
  char     gdMode[14];
  uint8_t  gdPowerfail;
  Guest    vms[MAX_GUESTS]; uint8_t vmCount;
  Guest    cts[MAX_GUESTS]; uint8_t ctCount;
  WxData     wx;
  RunData    run;
  ReplayData rep;
  SeqData    seq;
  PosData    pos;
};

extern FobData fd;

void fobDataBegin();
void fobDataUpdate();      // non-blocking-ish; call every frame
