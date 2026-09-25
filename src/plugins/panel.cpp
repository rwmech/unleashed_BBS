/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/panel.cpp
 * Module:       Plugins / panel (BBS_HAS_LCD boards only)
 *
 * Purpose:      The board's own display as a status panel, laid out like a
 *               phone's status bar over two lists:
 *
 *                 the bar     the board's name, its address and its uptime
 *                             with the card's free space, in turn, fading
 *                             from one to the next; who is ringing, while
 *                             somebody rings the sysop
 *                 the band    glyphs that show only while true (the card,
 *                             a ring, the sysop's mail, uploads to approve,
 *                             the backup window, the listing, staff on, an
 *                             unclean restart, a slow pass), the Wi-Fi
 *                             antenna and the clock
 *                 the track   a dot travelling along it: the board is alive
 *                 the lists   "Callers 4/11", who is on and for how long,
 *                             then the recent logins, logoffs and pages
 *                 the foot    free heap and calls today, and the lights
 *                             plugin's strip as a row of square LEDs,
 *                             whether or not a strip is wired
 *
 *               The spec is internal/tty-ux-panel-2026-09-24.md, revision 2.
 *               Compiled only for a board profile with BBS_HAS_LCD
 *               (board.h): on any other board this file is empty, the
 *               plugin is not in the registry and CONFIG has no page for it.
 *
 *                 [plugin:panel]
 *                 enabled   = yes        on as shipped on a board with one
 *                 pin1_mosi = 45         the Pins page: SDA
 *                 pin2_sclk = 40         SCL
 *                 pin3_cs   = 42         -1 for a panel with CS tied low
 *                 pin4_dc   = 41
 *                 pin5_rst  = 39         -1 to reset by command
 *                 pin6_bl   = 48         -1 for a backlight that is always on
 *                 width     = 172        the glass with the USB plug up
 *                 height    = 320
 *                 xoff      = 34         where that sits in the controller's RAM,
 *                 yoff      = 0          with the plug up
 *                 orientation = up       up | left | right | down: where the
 *                                        USB plug is as you face the screen;
 *                                        left and right draw it landscape
 *                 invert    = yes        an IPS panel is normally black
 *                 mirror    = yes        glass wired mirrored, as this one is
 *                 colours   = BGR        RGB | BGR
 *                 spi_mhz   = 10         10 | 20 | 40
 *                 backlight = 60         percent, 0 dark
 *
 *               Every default is the board profile's; each is a setting so
 *               a panel on another board, or a spec that changes, is a
 *               CONFIG page away rather than a rebuild.
 *
 * Design:       The framebuffer is in PSRAM, allocated at start (110 KB at
 *               320 x 172), never in the loop. The figures are recomposed
 *               twice a second and each is redrawn into the framebuffer only
 *               when what decides its pixels changed; the header's fade and
 *               the bell's blink step on their own clocks, the antenna four
 *               times a second, the LEDs and the dot every 40 ms. What
 *               changed is queued (panel_gfx.h, Dirty) and sent one band a
 *               tick by DMA (plat::lcdDraw), which returns at once: the BBS
 *               loop never waits on the glass.
 *
 *               Everything the panel shows is a figure already in RAM: the
 *               session pool, the core's members (Bbs::ringing and the rest)
 *               and the other plugins' own counts (panel_feed.h). It opens
 *               no file and walks no heap in the loop. The one exception is
 *               the card's free space, which the sd plugin's cache renews
 *               from the card's FAT; the panel asks for that once a minute.
 *
 * Commands:     PANEL (sysop)
 *
 * Libraries:    none (libc)
 * Targets:      ESP32-S3 boards with BBS_HAS_LCD, and the Linux host build of
 *               one (host/Makefile, bbs_host_s3)
 * See also:     src/plugins/panel_gfx.h, src/plugins/panel_feed.h,
 *               src/board.h, COMMANDS.md
 *
 * Copyright 2026 - Robert Mech
 * License:      GNU General Public License v3 or later
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>. The full
 * text is in the LICENSE file at the top of this repository.
 * ===========================================================================
 */
#include "../config.h"

#ifdef BBS_HAS_LCD

#include "panel_gfx.h"
#include "panel_feed.h"
#include "lights.h"
#include "../core/bbs.h"
#include "../core/bbs_util.h"
#include "../core/bus.h"
#include "../core/clock.h"
#include "../core/form.h"
#include "../core/plugin.h"
#include "../core/silent.h"
#include "../core/sysconfig.h"
#include "../platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using bbsu::ieq;
using bbsu::markFor;
using bbsu::nodeLabel;
using namespace panelgfx;
using namespace panelgfx::tok;

#ifdef BBS_HOST
// Host only: the glass the platform keeps, written to a PPM file, so a
// person can look at the layout without a panel (PANEL SHOT).
bool hostPanelShot(const char* path);
// And the backlight as the platform was last told it, percent.
int hostLcdBacklight();
#endif

namespace {

constexpr const char kName[] = "panel";

// The ST7789's RAM: 240 x 320, or 320 x 240 once the axes are swapped. A
// window past its edge would wrap into the far side of the glass.
constexpr uint16_t kRamShort = 240, kRamLong = 320;

constexpr char kColours[]   = "RGB|BGR";
constexpr char kClocks[]    = "10|20|40";

constexpr uint32_t kTextMs    = 500;      // figures recomposed twice a second
constexpr uint32_t kStripMs   = 40;       // the LEDs and the dot, 25 frames a second
constexpr uint32_t kNetMs     = 5000;     // the address, when it might have moved
constexpr uint32_t kCardMs    = 60000;    // the card's free space, for page C
constexpr uint32_t kHeapMs    = 5000;     // the heap figure
constexpr uint32_t kRssiMs    = 250;      // the antenna, four times a second
constexpr uint32_t kBlinkMs   = 250;      // the bell: a quarter lit, a quarter dark, 2 Hz
constexpr uint32_t kHoldMs    = 3000;     // a page of the header's slot
constexpr uint32_t kStepMs    = 60;       // a step of its fade, three ticks
constexpr int      kSteps     = 8;        // steps out, and as many back in
constexpr uint32_t kSlowMs    = 60000;    // the hourglass: a slow pass this last minute
constexpr uint32_t kCardErrMs = 10000;    // a failed card read: red as long as the drive light blinks
constexpr uint8_t  kEvents    = 10;       // the recent list's ring

// ---------------------------------------------------------------------------
// Settings, and what the panel is running on
// ---------------------------------------------------------------------------
uint8_t        g_index = 0xFF;
plat::LcdCfg   g_cfg;                       // the settings: the glass with the plug up
uint8_t        g_orient  = ORIENT_UP;       // where the USB plug is (panel_gfx.h, Orient)
plat::LcdCfg   g_run;                       // what the glass is driven with: g_cfg turned
char           g_why[64] = "not started";   // why the panel is dark, for PANEL and PLUGINS
bool           g_up      = false;           // the glass is ours and being drawn
bool           g_lightOwed = false;         // backlight on once the first full frame is out
uint32_t       g_owedAt  = 0;               // g_bands when it was owed: only a band after counts
// Silent mode (1.1.0, core/silent): the backlight off and nothing drawn, the
// picture and every setting kept. When it ends the whole glass is drawn
// again before it is lit, so nothing stale is ever shown.
bool           g_dark    = false;
// The glass holds a picture from before silent began: kept across a stop and
// a start (a CONFIG save ending silent restarts every plugin), and cleared
// only once a whole new frame has been sent and lit.
bool           g_stale   = false;
uint8_t        g_blNow   = 0;               // the backlight as this plugin last set it, for PANEL

// A system.cfg from before orientation (1.1.0-dev.8) said rotation, with
// width, height and offsets typed for the turned glass. Read once and put
// into the new terms, so a board that was turned stays turned.
bool           g_orientSet = false;
int16_t        g_legacyRot = -1;

// The framebuffer, in PSRAM, kept across restarts of the plugin while its
// size does not change: a CONFIG save restarts every plugin.
uint16_t*      g_fb      = nullptr;
size_t         g_fbSize  = 0;
Canvas         g_canvas;
Layout         g_layout;
Dirty          g_dirty;
uint32_t       g_bands   = 0;               // bands sent since start, for PANEL

// What each field last drew: its words (what PANEL prints), then \x1F and
// whatever else decides a pixel of it (a colour, a shade, the rule above a
// list slot). Only a change is redrawn. A leading 0x01 can never match a
// real key, which is how "draw it again" is said.
constexpr size_t kKey = 80;
char           g_shown[F_COUNT][kKey];

// The strip as it was last drawn.
uint16_t       g_ledCol[plat::kPixelMax];
uint8_t        g_ledN    = 0xFF;

uint32_t       g_textAt = 0, g_stripAt = 0, g_netAt = 0, g_cardAt = 0, g_heapAt = 0, g_rssiAt = 0;

// The header's slot: three pages in turn, and the fade between them.
enum Page : uint8_t { PAGE_NAME, PAGE_ADDR, PAGE_UP, PAGES };
uint8_t        g_page      = PAGE_NAME;
int8_t         g_fade      = 0;             // 0 the text, kSteps gone into the bar
int8_t         g_dir       = 0;             // 0 holding, 1 fading out, -1 fading in
uint32_t       g_stepAt    = 0;             // millis the hold or the last step began
bool           g_ringShown = false;         // the slot says who is ringing
char           g_addr[24]  = "";            // page B, "" with no network
char           g_free[16]  = "";            // page C's card figure, "" with no card

// The band: the status glyphs as last drawn, for the bell's blink.
Packed         g_packed;
bool           g_bellLit   = true;
uint16_t       g_errSeen   = 0;             // plat::diskSeen's error count, last looked at
uint32_t       g_errAt     = 0;             // millis of the last failed card read, 0 none
uint32_t       g_slowSeen  = 0;             // Bbs::slowPasses, last looked at
uint32_t       g_slowAt    = 0;             // millis a new slow pass was seen, 0 none
bool           g_slowInit  = false;
bool           g_bootSeen  = false;         // staff have been on since an unclean start
uint8_t        g_filesIdx = 0xFF, g_annIdx = 0xFF;

uint16_t       g_heapK     = 0;

// Calls today, as the system row says it. The core's figure is the caller
// log's count of calls that have ended, plus the one arriving, so callers on
// together are each told "1st"; the panel takes it at every login and keeps
// one more than the last on the same day, and 0 from midnight until the
// first login of the new day.
uint16_t       g_today    = 0;
uint32_t       g_todayDay = 0;

// The recent events, newest first. The kind, not the verb, so the icon says
// it and the line has its width for the time and the handle.
enum Ev : uint8_t { EV_LOGIN, EV_GUEST, EV_LOGOFF, EV_PAGE, EV_RING };
struct Event {
    uint8_t kind;
    char    text[6 + BBS_USER_MAX + 1];     // "HH:MM " and a handle
};
Event          g_events[kEvents];
uint8_t        g_eventN   = 0;
uint16_t       g_pageSeen = 0;

// The dot on the track.
int16_t        g_dotX = 0;

// defaults: every setting as the board profile ships it (board.h). From
// start(), before the file is read, and from setting() on a board that has
// never started the plugin, whose CONFIG page would otherwise show zeros.
bool           g_defaulted = false;

void defaults() {
    g_defaulted = true;
    g_cfg = plat::LcdCfg();
    g_cfg.mosi = BBS_LCD_MOSI;  g_cfg.sclk = BBS_LCD_SCLK;  g_cfg.cs  = BBS_LCD_CS;
    g_cfg.dc   = BBS_LCD_DC;    g_cfg.rst  = BBS_LCD_RST;   g_cfg.bl  = BBS_LCD_BL;
    g_cfg.width  = BBS_LCD_WIDTH;  g_cfg.height = BBS_LCD_HEIGHT;
    g_cfg.xoff   = BBS_LCD_XOFF;   g_cfg.yoff   = BBS_LCD_YOFF;
    g_cfg.rotation = 0;                        // the settings are the glass unturned
    g_orient     = BBS_LCD_ORIENT < ORIENTS ? BBS_LCD_ORIENT : ORIENT_UP;
    g_orientSet  = false;
    g_legacyRot  = -1;
    g_cfg.invert = BBS_LCD_INVERT;
    g_cfg.bgr    = BBS_LCD_BGR;
    g_cfg.mirror = BBS_LCD_MIRROR;
    g_cfg.mhz    = BBS_LCD_MHZ;
    g_cfg.backlight = BBS_LCD_BACKLIGHT;
}

// ---------------------------------------------------------------------------
// Reading system.cfg. A value the plugin cannot use is logged and the
// default kept, the way the lights plugin reads its own.
// ---------------------------------------------------------------------------
bool number(const char* v, long& out) {
    char* end = nullptr;
    out = strtol(v, &end, 10);
    return end && end != v && !*end;
}

void pinKey(int8_t& out, const char* key, const char* v, bool optional) {
    long p = 0;
    if (!number(v, p) || p < (optional ? -1 : 0) || p > BBS_GPIO_OUT_MAX || syscfg::pinProblem(p)) {
        plat::log("panel: %s = %s is not a pin it can use, keeping %d", key, v, out);
        return;
    }
    out = static_cast<int8_t>(p);
}

void numKey(uint16_t& out, const char* key, const char* v, long lo, long hi) {
    long n = 0;
    if (!number(v, n) || n < lo || n > hi) {
        plat::log("panel: %s = %s is outside %ld to %ld, keeping %u", key, v, lo, hi,
                  static_cast<unsigned>(out));
        return;
    }
    out = static_cast<uint16_t>(n);
}

bool yes(const char* v) {
    return ieq(v, "yes") || ieq(v, "on") || ieq(v, "true") || !strcmp(v, "1");
}

void readKey(void* ctx, const char* key, const char* v) {
    (void)ctx;
    if      (!strcmp(key, "pin1_mosi")) pinKey(g_cfg.mosi, key, v, false);
    else if (!strcmp(key, "pin2_sclk")) pinKey(g_cfg.sclk, key, v, false);
    else if (!strcmp(key, "pin3_cs"))   pinKey(g_cfg.cs,   key, v, true);
    else if (!strcmp(key, "pin4_dc"))   pinKey(g_cfg.dc,   key, v, false);
    else if (!strcmp(key, "pin5_rst"))  pinKey(g_cfg.rst,  key, v, true);
    else if (!strcmp(key, "pin6_bl"))   pinKey(g_cfg.bl,   key, v, true);
    else if (!strcmp(key, "width"))     numKey(g_cfg.width,  key, v, 1, kRamLong);
    else if (!strcmp(key, "height"))    numKey(g_cfg.height, key, v, 1, kRamLong);
    else if (!strcmp(key, "xoff"))      numKey(g_cfg.xoff,   key, v, 0, kRamLong - 1);
    else if (!strcmp(key, "yoff"))      numKey(g_cfg.yoff,   key, v, 0, kRamLong - 1);
    else if (!strcmp(key, "orientation")) {
        if (orientFromWord(v, g_orient)) g_orientSet = true;
        else plat::log("panel: orientation = %s is not up, left, right or down, keeping %s", v,
                       orientWord(g_orient));
    }
    else if (!strcmp(key, "rotation")) {       // up to 1.1.0-dev.8: see legacy()
        long r = 0;
        if (number(v, r) && (r == 0 || r == 90 || r == 180 || r == 270)) g_legacyRot = static_cast<int16_t>(r);
        else plat::log("panel: rotation = %s is not 0, 90, 180 or 270", v);
    }
    else if (!strcmp(key, "invert"))    g_cfg.invert = yes(v);
    else if (!strcmp(key, "mirror"))    g_cfg.mirror = yes(v);
    else if (!strcmp(key, "colours")) {
        if      (ieq(v, "BGR")) g_cfg.bgr = true;
        else if (ieq(v, "RGB")) g_cfg.bgr = false;
        else plat::log("panel: colours = %s is not RGB or BGR", v);
    }
    else if (!strcmp(key, "spi_mhz")) {
        uint16_t m = g_cfg.mhz;
        numKey(m, key, v, 10, 40);
        if (m == 10 || m == 20 || m == 40) g_cfg.mhz = static_cast<uint8_t>(m);
        else plat::log("panel: spi_mhz = %s is not 10, 20 or 40", v);
    }
    else if (!strcmp(key, "backlight")) {
        uint16_t b = g_cfg.backlight;
        numKey(b, key, v, 0, 100);
        g_cfg.backlight = static_cast<uint8_t>(b);
    }
}

// legacy: a file with rotation and no orientation, from before the USB plug
// setting. Its width, height and offsets were typed for the turned glass
// (the old advice: 90 or 270 "with the width and height swapped and the
// offset moved to Y"), so a sideways one is turned back to the plug-up terms
// the settings mean now. On this mirrored glass rotation 90 is the plug on
// the right and 270 on the left (panel_gfx.h, scanFor).
void legacy() {
    if (g_orientSet || g_legacyRot < 0) return;
    const bool side = g_legacyRot == 90 || g_legacyRot == 270;
    if (g_legacyRot == 180) g_orient = ORIENT_DOWN;
    else if (side) g_orient = (g_legacyRot == 90) == g_cfg.mirror ? ORIENT_RIGHT : ORIENT_LEFT;
    else g_orient = ORIENT_UP;
    if (side && g_cfg.width > g_cfg.height) {
        const uint16_t w = g_cfg.width, x = g_cfg.xoff;
        g_cfg.width = g_cfg.height;  g_cfg.height = w;
        g_cfg.xoff  = g_cfg.yoff;    g_cfg.yoff   = x;
    }
    plat::log("panel: rotation = %d read as orientation = %s", g_legacyRot, orientWord(g_orient));
}

// fits: the window lies inside the controller's RAM, the right way round
// for the rotation. Said as a reason when it does not.
const char* fits(const plat::LcdCfg& c) {
    const bool swapped = c.rotation == 90 || c.rotation == 270;
    const uint16_t ramW = swapped ? kRamLong : kRamShort, ramH = swapped ? kRamShort : kRamLong;
    if (c.xoff + c.width > ramW)  return "width and x offset run past the controller's RAM";
    if (c.yoff + c.height > ramH) return "height and y offset run past the controller's RAM";
    return nullptr;
}

// turned: the settings as the glass is driven in this orientation: the
// picture's size, the gaps and the rotation the platform sets MADCTL from.
plat::LcdCfg turned() {
    plat::LcdCfg r = g_cfg;
    const Scan s = scanFor(g_orient, g_cfg.mirror, g_cfg.width, g_cfg.height, g_cfg.xoff, g_cfg.yoff);
    r.rotation = s.rotation;
    r.width    = s.w;
    r.height   = s.h;
    r.xoff     = s.xgap;
    r.yoff     = s.ygap;
    return r;
}

// ---------------------------------------------------------------------------
// Drawing: the fields and their keys
// ---------------------------------------------------------------------------
void invalidate() {
    for (auto& s : g_shown) { s[0] = '\x01'; s[1] = '\0'; }
    g_ledN = 0xFF;
}

// changed: the field's key is new. Stores it, so the caller draws once.
// A field with no box on this glass is never drawn.
bool changed(uint8_t f, const char* key) {
    if (!strcmp(g_shown[f], key)) return false;
    snprintf(g_shown[f], sizeof(g_shown[f]), "%s", key);
    return !empty(fieldBox(g_layout, f));
}

// begin: clear a field's box to its background and queue it.
void begin(uint8_t f, uint16_t bg) {
    const Rect r = fieldBox(g_layout, f);
    fill(g_canvas, r, bg);
    g_dirty.add(r);
}

// cutGlyphs: the first k glyphs of s.
void cutGlyphs(const char* s, int k, char* out, size_t n) {
    const char* p = s;
    for (int g = 0; *p && g < k; ++g) glyph(p);
    size_t len = static_cast<size_t>(p - s);
    if (len > n - 1) len = n - 1;
    memcpy(out, s, len);
    out[len] = '\0';
}

// The dot's footprint at x: its tail two pixels behind it on the rail, the
// dot itself 3 x 3 across the band's last row, the rail and the first row
// of the body.
Rect dotBox(int x) { return R(x - 3, g_layout.track.y - 1, 5, 3); }

// dotErase: the rows the dot crosses put back as they are without it.
void dotErase(const Rect& r) {
    const int y = g_layout.track.y;
    fill(g_canvas, R(r.x, y - 1, r.w, 1), kBand);
    fill(g_canvas, R(r.x, y, r.w, 1), kTrack);
    fill(g_canvas, R(r.x, y + 1, r.w, 1), kBg);
}

void dotDraw(int x) {
    const int y = g_layout.track.y;
    dot(g_canvas, x - 3, y, scale(kDial, 1, 6));
    dot(g_canvas, x - 2, y, scale(kDial, 2, 6));
    fill(g_canvas, R(x - 1, y - 1, 3, 3), kDial);
    dot(g_canvas, x, y, kWhite);
}

// redrawAll: the whole glass from nothing: the header's two rows, the rail,
// the rules, and every field and LED redrawn on the next pass.
void redrawAll() {
    const Layout& L = g_layout;
    fill(g_canvas, R(0, 0, g_canvas.w, g_canvas.h), kBg);
    fill(g_canvas, L.bar, kBar);
    fill(g_canvas, L.band, kBand);
    fill(g_canvas, L.track, kTrack);
    fill(g_canvas, L.colRule, kRule);
    fill(g_canvas, L.rule1, kRule);
    fill(g_canvas, L.rule2, kRule);
    if (g_dotX >= g_canvas.w) g_dotX = 0;
    dotDraw(g_dotX);
    invalidate();
    g_dirty.clear();
    g_dirty.add(R(0, 0, g_canvas.w, g_canvas.h));
}

// ---------------------------------------------------------------------------
// The header's slot
// ---------------------------------------------------------------------------
void drawSlot(const char* s, uint16_t col) {
    char key[kKey];
    snprintf(key, sizeof(key), "%.60s\x1F%04X", s, static_cast<unsigned>(col));
    if (!changed(F_SLOT, key)) return;
    begin(F_SLOT, kBar);
    const Rect& r = g_layout.slot;
    text(g_canvas, r.x, r.y, s, col, kBar, false, r.w);
}

// pageText: what a page of the slot says now, and in what colour.
void pageText(uint8_t page, uint32_t now, char* out, size_t n, uint16_t& col) {
    const int fit = g_layout.slot.w / kSmallW;
    col = kInk;
    if (page == PAGE_NAME) {
        const SysConfig& c = syscfg::get();
        cutWords(c.boardName[0] ? c.boardName : BBS_NAME, fit, out, n);
    } else if (page == PAGE_ADDR) {
        if (g_addr[0]) { snprintf(out, n, "%s", g_addr); col = kDial; }
        else           { snprintf(out, n, "no network"); col = kRisk; }
    } else {
        char up[16];
        fmtUptime(now / 1000u, up, sizeof(up));
        if (g_free[0]) snprintf(out, n, "up %s  %s", up, g_free);
        else           snprintf(out, n, "up %s", up);
    }
}

// slotTick: the rotation, from every tick. A page holds for kHoldMs, fades
// into the bar in kSteps steps of kStepMs, and the next fades up out of
// it: 16 redraws a turn and nothing between turns but a change of text. A
// live ring holds the slot on who is ringing, and when it ends the
// rotation starts again from the name.
void slotTick(uint32_t now) {
    char buf[64];
    if (const char* who = Bbs::instance().ringing()) {
        char h[BBS_USER_MAX * 2 + 1];
        const int fit = g_layout.slot.w / kSmallW - 11;          // " is ringing" stays whole
        cutGlyphs(who, fit > 1 ? fit : 1, h, sizeof(h));
        snprintf(buf, sizeof(buf), "%s is ringing", h);
        drawSlot(buf, kBusy);
        g_ringShown = true;
        return;
    }
    if (g_ringShown) {
        g_ringShown = false;
        g_page = PAGE_NAME;
        g_fade = 0;
        g_dir  = 0;
        g_stepAt = now;
    }
    if (g_dir == 0) {
        if (now - g_stepAt >= kHoldMs) { g_dir = 1; g_fade = 1; g_stepAt = now; }
    } else if (now - g_stepAt >= kStepMs) {
        g_stepAt = now;
        if (g_dir > 0) {
            if (g_fade < kSteps) ++g_fade;
            else { g_page = static_cast<uint8_t>((g_page + 1) % PAGES); g_dir = -1; g_fade = kSteps - 1; }
        } else {
            if (g_fade > 0) --g_fade;
            if (g_fade == 0) g_dir = 0;                          // the hold starts now
        }
    }
    uint16_t col;
    pageText(g_page, now, buf, sizeof(buf), col);
    drawSlot(buf, mix(col, kBar, g_fade, kSteps));
}

// ---------------------------------------------------------------------------
// The band: the status glyphs, the antenna, the clock
// ---------------------------------------------------------------------------
// statusWords: the row in words, for PANEL and as its key: one word a glyph,
// so two rows with the same words are the same pixels.
void statusWords(const Status& s, char* out, size_t n) {
    static const char* const kCard[]    = { "no card", "card", "card error" };
    static const char* const kListing[] = { "", " listed", " waiting", " unlisted" };
    static const char* const kStaff[]   = { "", " co-sysop", " sysop" };
    snprintf(out, n, "%s%s%s%s%s%s%s%s%s", kCard[s.card], s.ring ? " ring" : "", s.mail ? " mail" : "",
             s.upload ? " uploads" : "", s.backup ? " backup" : "", kListing[s.listing],
             kStaff[s.staff], s.warn ? " restart" : "", s.slow ? " slow" : "");
}

uint8_t bellAt(const Packed& p) {
    for (uint8_t i = 0; i < p.n; ++i) if (p.which[i] == G_BELL) return i;
    return 0xFF;
}

void drawGlyphs(const Status& s) {
    char key[kKey];
    statusWords(s, key, sizeof(key));
    if (!changed(F_GLYPHS, key)) return;
    begin(F_GLYPHS, kBand);
    g_packed = pack(s, g_layout.glyphs.x);
    const uint8_t bell = bellAt(g_packed);
    for (uint8_t i = 0; i < g_packed.n; ++i)
        if (i != bell || g_bellLit) drawGlyphAt(g_canvas, g_packed, i, g_layout.glyphs.y);
}

// bellTick: while the band shows the bell, it blinks at 2 Hz by redrawing
// its own 9 x 11 cell, not the row.
void bellTick(uint32_t now) {
    const uint8_t bell = bellAt(g_packed);
    const bool lit = bell == 0xFF || ((now / kBlinkMs) & 1u) == 0;
    if (lit == g_bellLit) return;
    g_bellLit = lit;
    if (bell == 0xFF || empty(g_layout.glyphs)) return;
    const Rect b = glyphBox(g_packed, bell, g_layout.glyphs.y);
    fill(g_canvas, b, kBand);
    if (lit) drawGlyphAt(g_canvas, g_packed, bell, g_layout.glyphs.y);
    g_dirty.add(b);
}

// antTick: the antenna, sampled four times a second and redrawn when its
// height, its colour or whether the board is joined changes.
void antTick(uint32_t now) {
    if (g_rssiAt && now - g_rssiAt < kRssiMs) return;
    g_rssiAt = now ? now : 1;
    const int rssi = plat::wifiRssi();
    const bool joined = rssi != 0;
    const int h = signalFill(rssi);
    const uint16_t col = signalColour(rssi);
    char key[kKey];
    if (joined) snprintf(key, sizeof(key), "wifi %d/16 %s", h,
                         col == kLive ? "live" : col == kWarm ? "warm" : "risk");
    else        snprintf(key, sizeof(key), "wifi none");
    if (!changed(F_ANT, key)) return;
    begin(F_ANT, kBand);
    drawSignal(g_canvas, g_layout.ant.x, g_layout.ant.y, h, col, joined, kBand);
}

void drawClock() {
    char buf[8];
    const bool ok = clk::valid();
    if (ok) clk::fmt(buf, sizeof(buf), "%H:%M");
    else    snprintf(buf, sizeof(buf), "--:--");
    if (!changed(F_CLOCK, buf)) return;
    begin(F_CLOCK, kBand);
    line(g_canvas, g_layout.clock, g_layout.clock.y, buf, ok ? kYellow : kDim, kBand, false, RIGHT);
}

// ---------------------------------------------------------------------------
// The lists
// ---------------------------------------------------------------------------
// shown: a caller the panel may name: logged in with a handle, and not a
// member of staff who has chosen to be hidden or to lurk, whose presence
// the desk must not give away any more than WHO does.
bool shown(const Session& s) {
    return s.st != SState::Free && s.loggedIn && s.visible && !s.lurk && s.user[0];
}

uint16_t rankColour(char mark) {
    switch (mark) {
        case ']': return kRisk;
        case '>': return kYellow;
        case '*': return kDim;
        default:  return kInk;
    }
}

// The rule under the callers, drawn in the air above slot k when it is the
// first slot after them.
void slotRule(uint8_t k, bool rule) {
    if (!rule || !g_layout.gaps) return;
    const Rect& r = g_layout.list[k];
    fill(g_canvas, R(r.x, r.y - 2, r.w, 1), kRule);
}

void callerRow(uint8_t k, const Session& s, uint32_t now, bool rule) {
    const char mark = markFor(s) == ' ' ? ')' : markFor(s);
    char on[8], key[kKey];
    fmtOnFor(now - s.loginAt, on, sizeof(on));
    snprintf(key, sizeof(key), "%s%c %s %s\x1F%d", nodeLabel(s).t, mark, s.user, on, rule ? 1 : 0);
    if (!changed(static_cast<uint8_t>(F_LIST + k), key)) return;
    begin(static_cast<uint8_t>(F_LIST + k), kBg);
    slotRule(k, rule);
    const Rect& r = g_layout.list[k];
    const uint16_t rc = rankColour(mark);
    const char markS[2] = { mark, '\0' };
    text(g_canvas, r.x, r.y, nodeLabel(s).t, rc, kBg, false, 16);
    text(g_canvas, r.x + 16, r.y, markS, rc, kBg, false, 8);
    const int tw = textWidth(on, false);
    text(g_canvas, r.x + 32, r.y, s.user, kInk, kBg, false, r.w - 32 - tw - 8);
    text(g_canvas, r.x + r.w - tw, r.y, on, kDim, kBg, false, tw);
}

void moreRow(uint8_t k, uint8_t more, bool rule) {
    char key[kKey];
    snprintf(key, sizeof(key), "+%u more\x1F%d", static_cast<unsigned>(more), rule ? 1 : 0);
    if (!changed(static_cast<uint8_t>(F_LIST + k), key)) return;
    begin(static_cast<uint8_t>(F_LIST + k), kBg);
    slotRule(k, rule);
    const Rect& r = g_layout.list[k];
    char s[16];
    snprintf(s, sizeof(s), "+%u more", static_cast<unsigned>(more));
    text(g_canvas, r.x + 32, r.y, s, kDim, kBg, false, r.w - 32);
}

const char* evWord(uint8_t kind) {
    switch (kind) {
        case EV_LOGIN:  return "login";
        case EV_GUEST:  return "guest";
        case EV_LOGOFF: return "logoff";
        case EV_PAGE:   return "page";
        default:        return "ring";
    }
}

// recentRow: event j of the ring in slot k. The newest in ink, the next in
// dim, the rest faint; the icon in its kind's colour on the newest, and a
// bell keeps its colour however old, because somebody wanted somebody.
void recentRow(uint8_t k, uint8_t j, bool rule) {
    const Event& e = g_events[j];
    const uint8_t age = j < 2 ? j : 2;
    char key[kKey];
    snprintf(key, sizeof(key), "%s %s\x1F%u%d", evWord(e.kind), e.text, static_cast<unsigned>(age), rule ? 1 : 0);
    if (!changed(static_cast<uint8_t>(F_LIST + k), key)) return;
    begin(static_cast<uint8_t>(F_LIST + k), kBg);
    slotRule(k, rule);
    const Rect& r = g_layout.list[k];
    const uint16_t shade = age == 0 ? kInk : age == 1 ? kDim : kFaint;
    const bool bell = e.kind == EV_PAGE || e.kind == EV_RING;
    const uint16_t kindCol = e.kind == EV_LOGIN ? kLive : e.kind == EV_GUEST ? kWarm
                           : e.kind == EV_LOGOFF ? kDim : kBusy;
    icon(g_canvas, r.x, r.y, bell ? kIconBell : e.kind == EV_LOGOFF ? kIconLogoff : kIconLogin,
         (age == 0 || bell) ? kindCol : shade);
    text(g_canvas, r.x + 20, r.y, e.text, shade, kBg, false, r.w - 20);
}

void quietRow(uint8_t k, bool rule) {
    char key[kKey];
    snprintf(key, sizeof(key), "nothing yet\x1F%d", rule ? 1 : 0);
    if (!changed(static_cast<uint8_t>(F_LIST + k), key)) return;
    begin(static_cast<uint8_t>(F_LIST + k), kBg);
    slotRule(k, rule);
    const Rect& r = g_layout.list[k];
    icon(g_canvas, r.x, r.y, kIconQuiet, kFaint);
    text(g_canvas, r.x + 20, r.y, "nothing yet", kDim, kBg, false, r.w - 20);
}

void blankRow(uint8_t k, bool rule) {
    char key[8];
    snprintf(key, sizeof(key), "\x1F%d", rule ? 1 : 0);
    if (!changed(static_cast<uint8_t>(F_LIST + k), key)) return;
    begin(static_cast<uint8_t>(F_LIST + k), kBg);
    slotRule(k, rule);
}

// onNow: the callers the lists may name, the sysop's line first and then by
// node, which is the order a sysop reads a board in (the mock-ups').
struct OnNow {
    const Session* s[BBS_MAX_NODES + 2];
    uint8_t        n = 0;
};

void collect(void* ctx, Session& s) {
    OnNow& o = *static_cast<OnNow*>(ctx);
    if (shown(s) && o.n < BBS_MAX_NODES + 2) o.s[o.n++] = &s;
}

uint16_t order(const Session& s) {
    return s.role == Role::Sysop ? 0 : static_cast<uint16_t>(1 + s.id);
}

void drawLists(uint32_t now, const OnNow& o) {
    const Alloc a = allocate(g_layout, o.n);
    for (uint8_t k = 0; k < g_layout.slots; ++k) {
        const bool rule = a.rule && k == a.callers;
        if (k < a.names)                         callerRow(k, *o.s[k], now, rule);
        else if (k == a.names && a.more)         moreRow(k, a.more, rule);
        else if (k >= a.recent0 && k < a.recent0 + a.recentN) {
            const uint8_t j = static_cast<uint8_t>(k - a.recent0);
            if (!g_eventN && !j)                 quietRow(k, rule);
            else if (j < g_eventN)               recentRow(k, j, rule);
            else                                 blankRow(k, rule);
        } else                                   blankRow(k, rule);
    }
}

void drawHead(unsigned on, unsigned of) {
    char key[kKey];
    snprintf(key, sizeof(key), "Callers %u/%u", on, of);
    if (!changed(F_HEAD, key)) return;
    begin(F_HEAD, kBg);
    const uint16_t col = on ? kStruct : kDim;
    icon(g_canvas, g_layout.headIcon.x, g_layout.headIcon.y, kIconCallers, col);
    text(g_canvas, g_layout.head.x, g_layout.head.y, key, col, kBg, false, g_layout.head.w);
}

// drawSys: free heap and calls today, and in landscape the most lines busy
// at once since boot. The heap's colour is its warning: ink at 40 K and up,
// warm to 20 K, risk below.
void drawSys(uint16_t heapK, uint16_t today, uint8_t peak) {
    const Layout& L = g_layout;
    char key[kKey];
    if (L.sysFigs > 2) snprintf(key, sizeof(key), "%uK  %u today  peak %u", heapK, today, peak);
    else               snprintf(key, sizeof(key), "%uK  %u today", heapK, today);
    if (!changed(F_SYS, key)) return;
    begin(F_SYS, kBg);
    const Rect& r = L.sys;
    char s[24];
    const uint16_t hc = heapK >= 40 ? kInk : heapK >= 20 ? kWarm : kRisk;
    icon(g_canvas, L.sysAt[0], r.y, kIconChip, kDial);
    snprintf(s, sizeof(s), "%uK", heapK);
    text(g_canvas, L.sysAt[0] + 20, r.y, s, hc, kBg, false, L.sysAt[1] - L.sysAt[0] - 24);
    const int end2 = L.sysFigs > 2 ? L.sysAt[2] - 4 : r.x + r.w;
    icon(g_canvas, L.sysAt[1], r.y, kIconHandset, kDial);
    snprintf(s, sizeof(s), "%u today", today);
    text(g_canvas, L.sysAt[1] + 20, r.y, s, kInk, kBg, false, end2 - L.sysAt[1] - 20);
    if (L.sysFigs > 2) {
        icon(g_canvas, L.sysAt[2], r.y, kIconCallers, kDial);
        snprintf(s, sizeof(s), "peak %u", peak);
        text(g_canvas, L.sysAt[2] + 20, r.y, s, kInk, kBg, false, r.x + r.w - L.sysAt[2] - 20);
    }
}

// ---------------------------------------------------------------------------
// The events
// ---------------------------------------------------------------------------
void event(uint8_t kind, const char* who) {
    if (g_eventN < kEvents) ++g_eventN;
    for (uint8_t i = static_cast<uint8_t>(g_eventN - 1); i > 0; --i) g_events[i] = g_events[i - 1];
    char t[6];                                             // "HH:MM"
    if (clk::valid()) clk::fmt(t, sizeof(t), "%H:%M");
    else              snprintf(t, sizeof(t), "--:--");
    g_events[0].kind = kind;
    snprintf(g_events[0].text, sizeof(g_events[0].text), "%s %.*s", t, BBS_USER_MAX, who && *who ? who : "?");
}

// ---------------------------------------------------------------------------
// The flags, from RAM
// ---------------------------------------------------------------------------
// sysopMail: the sysop has mail not yet read: the sysop's account as the
// board routes missed rings to it (Bbs::sysopAccount, CONFIG board's Sysop
// or the last to elevate), from a flag chat keeps in RAM. Known from boot,
// with no wait for the sysop to be seen on first.
bool sysopMail() {
    return Bbs::instance().sysopMail();
}

// scan: one pass over the sessions for the lists, the person glyph, the
// sysop's handle, and whether staff have seen an unclean restart.
uint8_t scan(OnNow& on) {
    Bbs& b = Bbs::instance();
    b.eachSession(collect, &on);
    // Sysop's line first, then by node: twelve at most, so a plain sort.
    for (uint8_t i = 1; i < on.n; ++i)
        for (uint8_t k = i; k > 0 && order(*on.s[k]) < order(*on.s[k - 1]); --k) {
            const Session* t = on.s[k]; on.s[k] = on.s[k - 1]; on.s[k - 1] = t;
        }
    struct Ctx { uint8_t staff; } ctx{ Status::STAFF_NONE };
    b.eachSession([](void* c, Session& s) {
        if (s.st == SState::Free || !s.loggedIn || s.guest) return;
        if (s.level != Access::None) g_bootSeen = true;   // told at elevation, as reboots.log's notice is
        // The person follows the same rank as the mark on the caller's row
        // (markFor): the account's rank, or this call's elevation if higher.
        const uint8_t r = s.rank > static_cast<uint8_t>(s.level) ? s.rank : static_cast<uint8_t>(s.level);
        if (!r || !shown(s)) return;
        uint8_t& st = static_cast<Ctx*>(c)->staff;
        const uint8_t mine = r >= static_cast<uint8_t>(Access::Sysop) ? Status::STAFF_SYSOP : Status::STAFF_CO;
        if (mine > st) st = mine;
    }, &ctx);
    return ctx.staff;
}

// hiding: the handle is a caller on the board hidden or lurking. A page they
// send is not the desk's to show, any more than they are: the bus notes the
// sender's handle whoever it is.
bool hiding(const char* who) {
    struct Ctx { const char* who; bool hid; } c{ who, false };
    if (!who || !*who) return false;
    Bbs::instance().eachSession([](void* p, Session& s) {
        Ctx& x = *static_cast<Ctx*>(p);
        if (s.st != SState::Free && s.loggedIn && ieq(s.user, x.who) && (!s.visible || s.lurk)) x.hid = true;
    }, &c);
    return c.hid;
}

// cardState: the card for the SD glyph and page C. Red for a card that would
// not mount, and for a failed read on a mounted one while the drive light
// would still be blinking for it.
uint8_t cardState(uint32_t now) {
    uint32_t freeKB = 0;
    const bool renew = !g_cardAt || now - g_cardAt >= kCardMs;
    if (renew) g_cardAt = now ? now : 1;
    uint8_t c = sdcard::panel(freeKB, renew);
    const plat::DiskSeen d = plat::diskSeen();
    // DISK_ERROR is a failed read or write on either store. It is the card's
    // when the card was the one touched last, which is how the drive light's
    // pulse before an error names the store it failed on.
    if (d.count[plat::DISK_ERROR] != g_errSeen) {
        g_errSeen = d.count[plat::DISK_ERROR];
        if (static_cast<int32_t>(d.at[plat::DISK_CARD] - d.at[plat::DISK_FLASH]) >= 0) g_errAt = now ? now : 1;
    }
    if (c == Status::CARD_IN) {
        if (renew || !g_free[0]) {
            if (freeKB) fmtFree(freeKB, g_free, sizeof(g_free));
            else if (renew) g_free[0] = '\0';
        }
        if (g_errAt && now - g_errAt < kCardErrMs) c = Status::CARD_ERROR;
    } else {
        g_free[0] = '\0';
    }
    return c;
}

// refreshText: every figure but the fade, the bell, the antenna and the
// strip, twice a second. Each is redrawn only when it changed.
void refreshText(uint32_t now) {
    Bbs& b = Bbs::instance();

    OnNow on;
    const uint8_t staff = scan(on);

    const bus::PageSeen& pg = bus::lastPage();
    if (pg.count != g_pageSeen) {
        g_pageSeen = pg.count;
        if (!hiding(pg.from)) event(pg.ring ? EV_RING : EV_PAGE, pg.from);
    }

    if (!g_netAt || now - g_netAt >= kNetMs) {
        g_netAt = now ? now : 1;
        plat::NetInfo ni = plat::netInfo();
        if (ni.ip[0]) snprintf(g_addr, sizeof(g_addr), "%s:%u", ni.ip, static_cast<unsigned>(b.port()));
        else          g_addr[0] = '\0';
    }
    if (!g_heapAt || now - g_heapAt >= kHeapMs) {
        g_heapAt = now ? now : 1;
        g_heapK = static_cast<uint16_t>(plat::heapFree() / 1024u);
    }

    const uint32_t slow = b.slowPasses();
    if (!g_slowInit) { g_slowInit = true; g_slowSeen = slow; }
    if (slow != g_slowSeen) { g_slowSeen = slow; g_slowAt = now ? now : 1; }

    Status s;
    s.card    = cardState(now);
    s.ring    = b.ringing() != nullptr;
    s.mail    = sysopMail();
    s.upload  = g_filesIdx != 0xFF && plugins::running(g_filesIdx) && files::pendingCount() > 0;
    s.backup  = b.backupOpen();
    s.listing = g_annIdx != 0xFF && plugins::running(g_annIdx) ? announce::listing()
                                                               : static_cast<uint8_t>(Status::LIST_OFF);
    s.staff   = staff;
    s.warn    = b.crashBoot() && !g_bootSeen;
    s.slow    = g_slowAt && now - g_slowAt < kSlowMs;
    drawGlyphs(s);

    drawClock();
    drawHead(b.publicBusy(), b.publicNodes());
    drawLists(now, on);
    if (g_todayDay && clk::dayKey(now) != g_todayDay) { g_today = 0; g_todayDay = 0; }
    drawSys(g_heapK, g_today, b.peakNodes());
}

// refreshLeds: the lights' frame as square LEDs. How many it queued, which
// the dot reads to stand aside for a busy strip.
uint8_t refreshLeds() {
    const Rect& box = g_layout.leds;
    if (empty(box)) return 0;
    uint8_t rgbs[plat::kPixelMax * 3];
    uint8_t pct = 0;
    const uint8_t n = lights::panelFrame(rgbs, plat::kPixelMax, pct);
    const bool all = n != g_ledN;
    if (all) {                                             // a new length, or none
        fill(g_canvas, box, kBg);
        g_ledN = n;
        g_dirty.add(box);
    }
    uint8_t moved = 0;
    Rect cells[plat::kPixelMax];
    for (uint8_t i = 0; i < n; ++i) {
        const uint16_t col = rgb(glassLevel(rgbs[i * 3], pct), glassLevel(rgbs[i * 3 + 1], pct),
                                 glassLevel(rgbs[i * 3 + 2], pct));
        if (!all && col == g_ledCol[i]) continue;
        g_ledCol[i] = col;
        const Led l = ledAt(box, i, n);
        drawLed(g_canvas, l, col);
        cells[moved++] = l.cell;
    }
    // A few LEDs one at a time; most of the row as the whole row, which is
    // one queued rectangle rather than a queue full of small ones.
    if (all) return moved;
    if (moved > 3) g_dirty.add(box);
    else for (uint8_t i = 0; i < moved; ++i) g_dirty.add(cells[i]);
    return moved;
}

// dotStep: the dot one pixel along the rail, wrapping at the right edge.
// It stands aside on a pass where the strip queued more than one LED, so a
// busy strip has the glass to itself.
void dotStep(uint8_t leds) {
    if (leds > 1 || empty(g_layout.track)) return;
    const Rect was = dotBox(g_dotX);
    g_dotX = static_cast<int16_t>(g_dotX + 1 >= g_canvas.w ? 0 : g_dotX + 1);
    const Rect now = dotBox(g_dotX);
    dotErase(was);
    dotDraw(g_dotX);
    const Rect a = clip(was, g_canvas.w, g_canvas.h), b = clip(now, g_canvas.w, g_canvas.h);
    if (b.x >= a.x && b.x <= a.x + a.w) g_dirty.add(unite(a, b));   // one step along
    else { g_dirty.add(a); g_dirty.add(b); }                         // wrapped round
}

// light: the backlight, through one place, so PANEL can say what it was
// last told.
void light(uint8_t pct) {
    g_blNow = pct;
    plat::lcdBacklight(pct);
}

// oweLight: the backlight to come on once the whole glass queued by
// redrawAll has been sent, and not a band before.
void oweLight() {
    g_lightOwed = true;
    g_owedAt    = g_bands;
}

// flush: one band, if the last one has gone. After a reset the backlight
// waits for the whole first frame: redrawAll queues the glass as one
// rectangle ahead of anything else, and a rectangle is sent to its end
// before the next is started, so the first time nothing is part-sent after a
// band has gone since it was owed, the glass holds a whole new picture.
void flush() {
    if (!plat::lcdReady()) return;
    if (g_lightOwed && g_bands != g_owedAt && empty(g_dirty.cur)) {
        g_lightOwed = false;
        g_stale     = false;
        light(g_run.backlight);
    }
    Rect r;
    if (!g_dirty.next(plat::lcdBandPixels(), r)) return;
    if (plat::lcdDraw(g_fb, g_canvas.w, static_cast<uint16_t>(r.x), static_cast<uint16_t>(r.y),
                      static_cast<uint16_t>(r.w), static_cast<uint16_t>(r.h)))
        ++g_bands;
    else
        g_dirty.add(r);                                    // try it again next tick
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------
bool start(Bbs& bbs) {
    (void)bbs;
    g_index = plugins::indexOf(kName);
    g_filesIdx = plugins::indexOf("files");
    g_annIdx   = plugins::indexOf("announce");
    defaults();
    plugins::forEachKey(g_index, readKey, nullptr);
    legacy();
    g_up = false;
    g_lightOwed = false;

    // The settings are checked as the glass sits with the plug up, and then
    // turned: the window is then inside the RAM whichever way it is turned.
    const char* why = fits(g_cfg);
    g_run = turned();
    if (!why) why = fits(g_run);
    if (why) {
        snprintf(g_why, sizeof(g_why), "%s", why);
        plat::log("panel: %s; the panel stays dark", why);
        return true;                  // on, and PANEL says why it is dark
    }
    const size_t need = static_cast<size_t>(g_run.width) * g_run.height * 2u;
    if (g_fb && g_fbSize != need) {
        plat::psramFree(g_fb);
        g_fb = nullptr;
        g_fbSize = 0;
    }
    if (!g_fb) {
        g_fb = static_cast<uint16_t*>(plat::psramAlloc(need));
        if (!g_fb) {
            snprintf(g_why, sizeof(g_why), "no PSRAM for a %u x %u framebuffer",
                     static_cast<unsigned>(g_run.width), static_cast<unsigned>(g_run.height));
            plat::log("panel: %s", g_why);
            return true;
        }
        g_fbSize = need;
    }
    // The layout follows the turn: landscape when the picture is wider than
    // tall (MV set), the same portrait layout for the plug up or down.
    g_canvas = { g_fb, g_run.width, g_run.height };
    g_layout = layout(g_run.width, g_run.height);

    // A CONFIG save restarts every plugin. Settings that did not change keep
    // the panel as it is, lit, rather than taking it through its reset. New
    // ones (a turn, above all) reset it, and its memory then holds whatever
    // it held, so the backlight waits until the first whole frame is out:
    // nothing of the old picture, or of noise, is ever lit. A board that is
    // silent (1.1.0) starts dark and is lit by tick() when silent ends.
    const bool quiet = board::silent();
    bool owe = false;
    if (!plat::lcdSame(g_run)) {
        char err[64] = "";
        plat::LcdCfg dark = g_run;
        dark.backlight = 0;                                // lit by flush(), once drawn
        if (!plat::lcdBegin(dark, err, sizeof(err))) {
            snprintf(g_why, sizeof(g_why), "%s", err);
            return true;
        }
        g_blNow = 0;
        owe = !quiet;
    } else if (quiet || g_stale) {
        light(0);                                          // dark, or old glass: lit once drawn
        owe = !quiet;
    } else {
        light(g_run.backlight);
    }
    if (quiet) g_stale = true;
    redrawAll();
    g_textAt = g_stripAt = g_netAt = g_cardAt = g_heapAt = g_rssiAt = 0;
    g_page = PAGE_NAME;
    g_fade = 0;
    g_dir  = 0;
    g_stepAt = plat::millis();
    g_ringShown = false;
    g_packed = Packed();
    g_bellLit = true;
    g_errSeen = plat::diskSeen().count[plat::DISK_ERROR];   // only errors from here on
    g_bands = 0;
    g_dark = quiet;
    if (owe) oweLight();
    g_up = true;
    g_why[0] = '\0';
    lights::wantPanel(true);
    return true;
}

// stop: the backlight off and nothing more. A CONFIG save is a stop and a
// start a moment later, and the panel keeps its framebuffer and its bus
// across it; a panel switched off stays dark.
void stop() {
    lights::wantPanel(false);
    if (g_up) light(0);
    g_up = false;
    g_dark = false;
    g_dirty.clear();
}

void tick(uint32_t now) {
    if (!g_up) return;
    // Silent mode (1.1.0): dark, and nothing drawn or sent while it lasts.
    // What happens meanwhile (a login, a ring) is still kept by the hooks,
    // so the picture that comes back is the board as it is then.
    if (board::silent()) {
        if (!g_dark) {
            g_dark = true;
            g_stale = true;
            g_lightOwed = false;
            light(0);
        }
        return;
    }
    if (g_dark) {
        // The whole glass again, every field recomposed on this pass, and the
        // light owed until all of it has been sent.
        g_dark = false;
        redrawAll();
        g_textAt = g_stripAt = 0;
        oweLight();
    }
    if (!g_textAt || now - g_textAt >= kTextMs) {
        g_textAt = now ? now : 1;
        refreshText(now);
    }
    slotTick(now);
    bellTick(now);
    antTick(now);
    if (!g_stripAt || now - g_stripAt >= kStripMs) {
        g_stripAt = now ? now : 1;
        dotStep(refreshLeds());
    }
    flush();
}

void onLogin(Session& s) {
    // Every login is a call today, a hidden one too; only the list is shy.
    const uint32_t day  = clk::dayKey(plat::millis());
    const uint16_t core = Bbs::instance().callsToday();
    g_today = (day == g_todayDay && g_today >= core) ? static_cast<uint16_t>(g_today + 1u) : core;
    g_todayDay = day;
    if (shown(s)) event(s.guest ? EV_GUEST : EV_LOGIN, s.user);
}

void onLogoff(Session& s) {
    if (shown(s)) event(EV_LOGOFF, s.user);
}

const char* status() {
    static char out[48];
    if (g_up) snprintf(out, sizeof(out), "Panel: %ux%u ST7789, %u bands sent",
                       static_cast<unsigned>(g_run.width), static_cast<unsigned>(g_run.height),
                       static_cast<unsigned>(g_bands));
    else      snprintf(out, sizeof(out), "Panel: dark, %.34s", g_why);
    return out;
}

// ---------------------------------------------------------------------------
// PANEL: what the panel is running on, and what it shows, as text: every
// field's words, top to bottom. What a sysop at the bench compares with the
// glass, and what the tests read.
// ---------------------------------------------------------------------------
void line(Session& s, Color c, const char* text) {
    s.term.color(s.tl, c);
    s.term.text(s.tl, text);
    s.term.nl(s.tl);
}

void cmdPanel(Bbs& b, Session& s, const char* a, uint32_t now) {
    (void)now;
#ifdef BBS_HOST
    // Host only: the glass as a picture, for a person checking the layout.
    if (!strncmp(a, "shot ", 5) || ieq(a, "shot")) {
        const char* path = a[4] ? a + 5 : "panel.ppm";
        line(s, Color::Grey, hostPanelShot(path) ? "Written." : "No glass to write.");
        b.prompt(s);
        return;
    }
#endif
    if (*a) {
        line(s, Color::LightRed, "PANEL, on its own.");
        b.prompt(s);
        return;
    }
    char buf[48];
    b.rowTitle(s, "Panel", !g_up ? "dark" : g_dark ? "silent" : "lit");
    if (!g_up) {
        snprintf(buf, sizeof(buf), "Dark: %.40s", g_why);
        line(s, Color::LightRed, buf);
    }
    // As driven: the picture's size and where it sits, turned.
    snprintf(buf, sizeof(buf), "ST7789 %ux%u at %u,%u, USB %s",
             static_cast<unsigned>(g_run.width), static_cast<unsigned>(g_run.height),
             static_cast<unsigned>(g_run.xoff), static_cast<unsigned>(g_run.yoff), orientWord(g_orient));
    line(s, Color::Yellow, buf);
    snprintf(buf, sizeof(buf), "Pins %d %d %d %d %d %d, %u MHz",
             g_cfg.mosi, g_cfg.sclk, g_cfg.cs, g_cfg.dc, g_cfg.rst, g_cfg.bl,
             static_cast<unsigned>(g_cfg.mhz));
    line(s, Color::Grey, buf);
    // What the backlight was last told (silent mode, 1.1.0). On the host,
    // what the platform holds rather than what this plugin meant, so a test
    // reads the glass. Above "bands sent": the fields' words follow that
    // line, and a reader takes them from there.
#ifdef BBS_HOST
    const int bl = hostLcdBacklight();
#else
    const int bl = g_blNow;
#endif
    if (g_up && g_dark) snprintf(buf, sizeof(buf), "Backlight %d%%, silent mode", bl);
    else                snprintf(buf, sizeof(buf), "Backlight %d%%", bl);
    line(s, Color::Grey, buf);
    snprintf(buf, sizeof(buf), "%u bands sent", static_cast<unsigned>(g_bands));
    line(s, Color::Grey, buf);
    if (g_up) {
        static const uint8_t kOrder[] = { F_SLOT, F_GLYPHS, F_ANT, F_CLOCK, F_HEAD };
        auto say = [&](uint8_t f) {
            const char* k = g_shown[f];
            if (empty(fieldBox(g_layout, f)) || k[0] == '\x01') return;
            const size_t n = strcspn(k, "\x1F");
            if (!n) return;
            snprintf(buf, sizeof(buf), "  %.*s", static_cast<int>(n < 44 ? n : 44), k);
            line(s, Color::White, buf);
        };
        for (uint8_t f : kOrder) say(f);
        for (uint8_t k = 0; k < g_layout.slots; ++k) say(static_cast<uint8_t>(F_LIST + k));
        say(F_SYS);
        const uint8_t n = g_ledN == 0xFF ? 0 : g_ledN;
        snprintf(buf, sizeof(buf), "  strip: %u LEDs", static_cast<unsigned>(n));
        line(s, Color::White, buf);
    }
    b.prompt(s);
}

const Command kCommands[] = {
    { "PANEL", "", 0, CF_ADMIN, "PANEL", "what the display shows", cmdPanel, Menu::Sysop, 8 },
};

// ---------------------------------------------------------------------------
// CONFIG: twelve rows, which is a full page (Form::kMaxFields less the
// core's four), and a Pins page for the six pins, which are the settings a
// board with another panel changes first and are checked together there
// for two rows on one pin. Labels are nine columns and notes 38; at 80
// columns each row has a twenty column label too (1.1.0, the forms at 80).
// ---------------------------------------------------------------------------
constexpr PluginSetting kSettings[] = {
    { "driver",    "Driver",    PS_INFO,  0, 0,   8, "The panel's controller chip.", nullptr,
      "Controller chip" },
    { "pin",       "Pins",      PS_PAGE,  0, 0,   0, "The panel's SPI and control pins.", nullptr,
      "Panel pins" },
    { "width",     "Width",     PS_NUM,   1, kRamLong,     3, "Pixels across, with the USB plug up.",
      nullptr, "Width, pixels" },
    { "height",    "Height",    PS_NUM,   1, kRamLong,     3, "Pixels down, with the USB plug up.",
      nullptr, "Height, pixels" },
    { "xoff",      "X offset",  PS_NUM,   0, kRamLong - 1, 3, "Glass's first column in the chip's RAM",
      nullptr, "X offset in RAM" },
    { "yoff",      "Y offset",  PS_NUM,   0, kRamLong - 1, 3, "Glass's first row in the chip's RAM.",
      nullptr, "Y offset in RAM" },
    // In the row Rotation had (1.1.0-dev.8), which this replaces: the page is
    // full, and two ways to turn one picture would be one too many.
    { "orientation", "USB plug", PS_CYCLE, 0, 0, 5, "Where the plug is, as you face it.",
      kOrientWords, "USB plug points" },
    { "invert",    "Invert",    PS_YESNO, 0, 0,   3, "Yes for an IPS panel (normally black).", nullptr,
      "Invert colours" },
    { "mirror",    "Mirror",    PS_YESNO, 0, 0,   3, "Yes if text shows back to front.", nullptr,
      "Mirror the picture" },
    { "colours",   "Colours",   PS_CYCLE, 0, 0,   3, "BGR if red and blue are swapped.", kColours,
      "Colour order" },
    { "spi_mhz",   "SPI MHz",   PS_CYCLE, 0, 0,   2, "10 is safe; the panel's limit is 62.5.", kClocks,
      "SPI clock, MHz" },
    { "backlight", "Bright %",  PS_NUM,   0, 100, 3, "0 is dark; 60 as shipped.", nullptr,
      "Backlight %" },
    { "pin1_mosi", "MOSI pin",  PS_PIN,   0, BBS_GPIO_OUT_MAX, 2, "Data to the panel: SDA.", nullptr,
      "MOSI GPIO (SDA)" },
    { "pin2_sclk", "SCLK pin",  PS_PIN,   0, BBS_GPIO_OUT_MAX, 2, "The clock to the panel: SCL.", nullptr,
      "Clock GPIO (SCL)" },
    { "pin3_cs",   "CS pin",    PS_PIN,  -1, BBS_GPIO_OUT_MAX, 2, "Chip select. -1 if it is tied low.", nullptr,
      "Chip select GPIO" },
    { "pin4_dc",   "D/C pin",   PS_PIN,   0, BBS_GPIO_OUT_MAX, 2, "Data or command select.", nullptr,
      "Data/command GPIO" },
    { "pin5_rst",  "RST pin",   PS_PIN,  -1, BBS_GPIO_OUT_MAX, 2, "Reset. -1 resets it by command.", nullptr,
      "Reset GPIO" },
    { "pin6_bl",   "Light pin", PS_PIN,  -1, BBS_GPIO_OUT_MAX, 2, "Backlight. -1 if it is always on.", nullptr,
      "Backlight GPIO" },
};
constexpr size_t kSettingCount = sizeof(kSettings) / sizeof(kSettings[0]);

constexpr bool pinRow(const char* k) {
    return k[0] == 'p' && k[1] == 'i' && k[2] == 'n' && k[3] >= '1' && k[3] <= '9';
}
constexpr size_t countPins(size_t i = 0) {
    return i == kSettingCount ? 0 : (pinRow(kSettings[i].key) ? 1 : 0) + countPins(i + 1);
}
static_assert(countPins() == 6, "six pins on the Pins page");
static_assert(kCoreRows + (kSettingCount - countPins()) <= Form::kMaxFields, "the panel page is full");

void setting(const char* key, char* out, size_t n) {
    if (!g_defaulted) defaults();
    if      (!strcmp(key, "driver"))    snprintf(out, n, "ST7789");
    else if (!strcmp(key, "pin"))       snprintf(out, n, "%d %d %d %d %d %d", g_cfg.mosi, g_cfg.sclk,
                                                 g_cfg.cs, g_cfg.dc, g_cfg.rst, g_cfg.bl);
    else if (!strcmp(key, "pin1_mosi")) snprintf(out, n, "%d", g_cfg.mosi);
    else if (!strcmp(key, "pin2_sclk")) snprintf(out, n, "%d", g_cfg.sclk);
    else if (!strcmp(key, "pin3_cs"))   snprintf(out, n, "%d", g_cfg.cs);
    else if (!strcmp(key, "pin4_dc"))   snprintf(out, n, "%d", g_cfg.dc);
    else if (!strcmp(key, "pin5_rst"))  snprintf(out, n, "%d", g_cfg.rst);
    else if (!strcmp(key, "pin6_bl"))   snprintf(out, n, "%d", g_cfg.bl);
    else if (!strcmp(key, "width"))     snprintf(out, n, "%u", static_cast<unsigned>(g_cfg.width));
    else if (!strcmp(key, "height"))    snprintf(out, n, "%u", static_cast<unsigned>(g_cfg.height));
    else if (!strcmp(key, "xoff"))      snprintf(out, n, "%u", static_cast<unsigned>(g_cfg.xoff));
    else if (!strcmp(key, "yoff"))      snprintf(out, n, "%u", static_cast<unsigned>(g_cfg.yoff));
    else if (!strcmp(key, "orientation")) snprintf(out, n, "%s", orientWord(g_orient));
    else if (!strcmp(key, "invert"))    snprintf(out, n, "%s", g_cfg.invert ? "yes" : "no");
    else if (!strcmp(key, "mirror"))    snprintf(out, n, "%s", g_cfg.mirror ? "yes" : "no");
    else if (!strcmp(key, "colours"))   snprintf(out, n, "%s", g_cfg.bgr ? "BGR" : "RGB");
    else if (!strcmp(key, "spi_mhz"))   snprintf(out, n, "%u", static_cast<unsigned>(g_cfg.mhz));
    else if (!strcmp(key, "backlight")) snprintf(out, n, "%u", static_cast<unsigned>(g_cfg.backlight));
    else out[0] = '\0';
}

} // namespace

extern const Plugin kPanelPlugin = {
    // On as shipped: the panel is part of the board. PF_FAST, because the
    // header fades, the strip moves and a band goes out each tick.
    { kName, "Status display", "1.1", 12288, 0, PF_CORE | PF_FAST | PF_ON,
      PlugLevel::Sysop, PlugLevel::Sysop, PlugLevel::Sysop },
    start,
    stop,
    tick,
    nullptr,                 // onConnect
    onLogin,
    onLogoff,
    nullptr,                 // onKey
    status,
    kCommands,
    sizeof(kCommands) / sizeof(kCommands[0]),
    kSettings,
    static_cast<uint8_t>(kSettingCount),
    setting,
    nullptr,                 // rows
    nullptr,                 // onPresence: publicBusy is read twice a second
    nullptr,                 // onBytes
    nullptr,                 // onRename
    nullptr,                 // listDone
};

#endif  // BBS_HAS_LCD
