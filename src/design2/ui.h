// Design 2 - "auto-focus hero".
//
// The screen picks its own subject. Every metric the fob knows is scored each
// frame on alarm state, severity and rate of change; the winner is promoted to
// the hero slot. Everything else stays live as a spectrum strip along the
// bottom, and when the hero changes it visibly unfolds out of its own tick in
// that strip, so you can see which of the 23 just got promoted.
//
// No buttons on this board, so nothing here waits for input.
#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "common/status.h"
#include "common/oled.h"

static const uint8_t SCR_W = 128, SCR_H = 64;

// Yellow band, rows 0..15.
static const int16_t CLOCK_BASE = 15;

// Blue band, rows 16..55.
static const int16_t HERO_LABEL_B = 22;   // hero name
static const int16_t HERO_VAL_B   = 46;   // odometer baseline
static const int16_t HERO_VAL_R   = 50;   // odometer right edge (right-aligned)
static const int16_t SPARK_X      = 54;
static const int16_t SPARK_Y      = 24;
static const uint8_t SPARK_W      = 72;
static const uint8_t SPARK_H      = 21;

// Spectrum strip.
static const int16_t STRIP_X0   = 6;
static const uint8_t STRIP_STEP = 5;      // 4px bar + 1px gap
static const int16_t STRIP_BASE = 54;     // bars grow up from here
static const uint8_t STRIP_H    = 7;
static const int16_t GROUP_B    = 62;     // group labels, page 7

static const uint8_t METRIC_COUNT = 23;
static const uint8_t GROUP_COUNT  = 5;
static const uint8_t MHIST        = 40;   // ~20 s of history per metric

struct Metric {
  const char *label;     // hero caption, e.g. "node01 gpu"
  const char *unit;      // "C", "%", "W", "" ...
  const char *state;     // when set, the hero shows this word instead of a
                         // number - a down node has no meaningful value, and
                         // rendering it as a big "0" is just wrong
  uint8_t     group;     // 0 slerm, 1 pmox, 2 pwr, 3 sec, 4 svc
  float       value;     // shown in the hero
  float       norm;      // 0..1, strip height
  float       disp;      // tweened norm, what the strip actually draws
  float       sev;       // 0..1, how bad
  bool        alarm;
  float       rate;      // signed change over the history window
  float       score;
};

extern Metric  gM[METRIC_COUNT];
extern int16_t gJx, gJy;               // slow burn-in drift

void metricsInit();
void metricsUpdate(float dt);
uint8_t metricsPick(uint8_t current, bool allowChange);
const char *groupName(uint8_t grp);
uint8_t histAt(uint8_t metric, uint8_t i);   // i=0 oldest
uint8_t histCount();

// render.cpp
void d2Animate(float dt, uint8_t drawnHero);
float d2HeroValue();
void d2TopBar(uint8_t hero, float t);
void d2Hero(uint8_t hero, float t);
void d2Strip(uint8_t hero, float t, float reveal);
void d2GroupLabels();
int16_t stripTickX(uint8_t metric);
