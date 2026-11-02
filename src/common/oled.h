// HW-364A/B display bring-up. The OLED is not on the default I2C pins and the
// A/B revisions swap SDA/SCL, so both orders are probed rather than assumed.
//
// The panel is TWO-COLOUR: rows 0..15 yellow, rows 16..63 blue. That is also
// the byte-page boundary (pages 0..1 vs 2..7), which designs exploit.
#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

extern U8G2   *dsp;
extern uint8_t oledSda, oledScl, oledAddr;
extern bool    oledFound;

void oledBegin(uint32_t busHz);
