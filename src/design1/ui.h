// Homelab status fob - shared types and layout.
#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "common/status.h"
#include "common/oled.h"

// 128x64 layout. Top bar, card area, page dots.
static const uint8_t SCR_W   = 128;
static const uint8_t SCR_H   = 64;
// This panel is a two-colour OLED: rows 0..15 are YELLOW, 16..63 are BLUE.
// That split is pages 0..1 vs 2..7, so the layout is aligned to it: chrome in
// the yellow band, data in the blue. The card band is also page-aligned
// (pages 2..6) so a slide can be done by memmoving those pages -- see
// shiftCardBandLeft() in main.cpp.
static const uint8_t TOPBAR_H  = 16;   // rows 0..15  = pages 0..1, yellow
static const uint8_t CARD_Y0   = 16;   // rows 16..55 = pages 2..6, blue
static const uint8_t CARD_Y1   = 55;
static const uint8_t CARD_PAGE0 = 2;
static const uint8_t CARD_PAGE1 = 6;
static const uint8_t DOTS_Y    = 58;   // page 7

static const uint8_t CARD_COUNT = 6;

// Shared drawing helpers (gfx.cpp)
void drawTopBar(float t);
void drawDots(uint8_t active, float slide);
void drawBarH(int16_t x, int16_t y, uint8_t w, uint8_t h, float pct);
void drawPip(int16_t x, int16_t y, bool up, float t);
void drawWifi(int16_t x, int16_t y, int8_t rssi, float t);
void drawBattery(int16_t x, int16_t y, uint8_t w, uint8_t h, float pct, bool charging, float t);
void drawCheck(int16_t x, int16_t y, float t);
void drawCross(int16_t x, int16_t y, float t);
void drawMarquee(int16_t x, int16_t y, uint8_t w, const char *s, float t);
// u8g2's drawLine takes UNSIGNED coords and does no bounds check, so an
// off-screen endpoint wraps to ~65500 and its loop runs for seconds. Always
// go through this.
void safeLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void drawSweep(int16_t x, int16_t y, uint8_t w, float t);

// Cards (cards.cpp). ox shifts the whole card for slide transitions.
void drawCard(uint8_t idx, int16_t ox, float t);
const char *cardName(uint8_t idx);

// Boot animation + idle face (screens.cpp)
void drawBoot(float t);
void drawIdle(float t);

// Clip window bookkeeping: cards are clipped to the card area during a
// slide, and helpers that clip (marquee) must restore that, not the full
// screen.
void clipSet(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void clipRestore();
void clipFull();

// Seconds since the current card slid into place, for staggered entrances.
extern float gCardAge;
void cardsUpdate(float dt);
