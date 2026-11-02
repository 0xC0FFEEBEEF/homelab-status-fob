// design4 - a page per thing, live from fob-api on byteme2.
//
// Yellow band (rows 0..15) is the persistent header; the blue band (16..63) is
// the page, and that region is byte-pages 2..7 exactly, so a slide is a memmove
// per page rather than drawing at a negative x (u8g2 coords are unsigned and
// drawLine has no bounds check - see design1).
#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "common/oled.h"
#include "common/fobdata.h"

static const uint8_t SCR_W = 128, SCR_H = 64;
static const int16_t HDR_Y1     = 15;
static const int16_t BODY_Y0    = 16;
static const uint8_t BODY_PAGE0 = 2;
static const uint8_t BODY_PAGE1 = 7;

enum PageKind { PG_VM, PG_CTS, PG_UPS, PG_SLERM, PG_SEC, PG_WX_NOW, PG_WX_WIND, PG_WX_STORM, PG_REPLAY };

struct Page { PageKind kind; uint8_t idx; };   // idx = which VM, for PG_VM

extern Page    gPages[20];
extern uint8_t gPageCount;
extern float   gAge;          // seconds since this page arrived

void pagesRebuild();
void pageDraw(const Page &p, int16_t ox, float t);
const char *pageTitle(const Page &p);

// scenes.cpp
void bootDraw(float p, float t);
void netDraw(uint8_t state, float t, const char *ssid, const char *ip);

// The outage sequence, drawn rather than listed. Used for a live run, a dry run
// and the replay alike - same data shape, same animation.
bool  outageAvailable();
void  outageBegin();
void  outageDraw(float t);
bool  outageDone();
float outageProgress();

void pagesSampleHistory();
