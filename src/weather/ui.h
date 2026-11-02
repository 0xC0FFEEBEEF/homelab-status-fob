// Minimal weather fob. Rows 0..15 are amber on this panel, 16..63 blue, so the
// band carries the label and the blue holds the figure.
#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "common/oled.h"
#include "wxdata.h"

static const uint8_t SCR_W = 128, SCR_H = 64;
static const uint8_t WX_PAGES = 6;

void eyebrow(int16_t x, int16_t y, const char *s);
void rightStr(int16_t rx, int16_t y, const char *s);
void wxPage(uint8_t idx, int16_t ox, float t);
const char *wxPageName(uint8_t idx);
extern float gAge;
