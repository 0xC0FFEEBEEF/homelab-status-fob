// Design 3 - "Sky over Rack".
//
// This panel is physically two-colour: rows 0..15 are YELLOW, 16..63 BLUE.
// Rather than work around that, it IS the design: the yellow band is the sky,
// the blue band is the rack. The sky is driven by real Tempest data - the sun
// sits at a height set by measured solar radiation, clouds drift at the real
// wind speed and direction, rain falls out of the yellow band and down across
// the colour boundary, and a lightning strike inverts the whole display.
//
// The rack half stays information-complete at all times; weather is additive in
// a band that used to be chrome. A takeover mode borrows the full screen for
// ~15 s for genuinely dramatic events - a near strike OR a critical rack event,
// because the drama should not belong to the weather alone.
#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "common/status.h"
#include "common/weather.h"
#include "common/oled.h"

static const uint8_t SCR_W = 128, SCR_H = 64;
static const int16_t SKY_Y1  = 15;    // yellow band is rows 0..15
static const int16_t RACK_Y0 = 16;    // blue band
static const int16_t RACK_Y1 = 55;
static const int16_t FOOT_B  = 62;    // page 7: clock + rollup

void skyDraw(float t, bool tall);
void rackDraw(float t);

// Which event, if any, deserves the whole screen.
enum EventKind { EV_NONE, EV_STRIKE, EV_RAIN, EV_POWER, EV_NODE, EV_SECURITY };
EventKind eventPick();
void      eventDraw(EventKind k, float t, float age);
