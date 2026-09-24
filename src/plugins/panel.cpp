/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/panel.cpp
 * Module:       Plugins / panel (BBS_HAS_LCD boards only)
 *
 * Purpose:      The board's own display as a status panel: its name and the
 *               time, how many callers are on out of how many lines, where
 *               to dial, how long it has been up, the card's free space,
 *               the last thing that happened (a login, a logoff, a page),
 *               and along the bottom the lights plugin's strip, drawn as
 *               lamps whether or not a strip is wired.
 *
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
 *                 width     = 172        as drawn, after the rotation: portrait,
 *                 height    = 320        the stick hanging with its plug up
 *                 xoff      = 34         where that sits in the controller's RAM
 *                 yoff      = 0
 *                 rotation  = 0          0 | 90 | 180 | 270 (landscape: 90,
 *                                        320 x 172 at yoff 34)
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
 *               320 x 172), never in the loop. A figure is recomposed twice
 *               a second and redrawn into the framebuffer only when its text
 *               changed; the strip every 40 ms, a lamp at a time or the row
 *               at once. What changed is queued (panel_gfx.h, Dirty) and sent
 *               one band a tick by DMA (plat::lcdDraw), which returns at
 *               once: the BBS loop never waits on the glass.
 *
 * Commands:     PANEL (sysop)
 *
 * Libraries:    none (libc)
 * Targets:      ESP32-S3 boards with BBS_HAS_LCD, and the Linux host build of
 *               one (host/Makefile, bbs_host_s3)
 * See also:     src/plugins/panel_gfx.h, src/board.h, COMMANDS.md
 *
 * Copyright 2026 - Robert Mech
 * License:      GNU General Public License v2 or later
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <https://www.gnu.org/licenses/>. The full
 * text is in the LICENSE file at the top of this repository.
 * ===========================================================================
 */
#include "../config.h"

#ifdef BBS_HAS_LCD

#include "panel_gfx.h"
#include "lights.h"
#include "../core/bbs.h"
#include "../core/bbs_util.h"
#include "../core/bus.h"
#include "../core/clock.h"
#include "../core/form.h"
#include "../core/plugin.h"
#include "../core/sysconfig.h"
#include "../platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using bbsu::ieq;
using namespace panelgfx;

#ifdef BBS_HOST
// Host only: the glass the platform keeps, written to a PPM file, so a
// person can look at the layout without a panel (PANEL SHOT).
bool hostPanelShot(const char* path);
#endif

namespace {

constexpr const char kName[] = "panel";

// The ST7789's RAM: 240 x 320, or 320 x 240 once the axes are swapped. A
// window past its edge would wrap into the far side of the glass.
constexpr uint16_t kRamShort = 240, kRamLong = 320;

constexpr char kRotations[] = "0|90|180|270";
constexpr char kColours[]   = "RGB|BGR";
constexpr char kClocks[]    = "10|20|40";

// Colours on the glass.
constexpr uint16_t kBg      = rgb(0, 0, 0);
constexpr uint16_t kBar     = rgb(0x18, 0x2C, 0x78);
constexpr uint16_t kWhite   = rgb(0xF0, 0xF0, 0xF0);
constexpr uint16_t kGrey    = rgb(0x90, 0x90, 0x90);
constexpr uint16_t kYellow  = rgb(0xFF, 0xD8, 0x40);
constexpr uint16_t kGreen   = rgb(0x50, 0xF0, 0x50);
constexpr uint16_t kCyan    = rgb(0x60, 0xD8, 0xFF);
constexpr uint16_t kAmber   = rgb(0xFF, 0xA0, 0x20);
constexpr uint16_t kRim     = rgb(0x48, 0x48, 0x48);
constexpr uint16_t kUnlit   = rgb(0x14, 0x14, 0x14);   // a lamp that is off, still a lamp

constexpr uint32_t kTextMs  = 500;      // figures recomposed twice a second
constexpr uint32_t kStripMs = 40;       // the strip at 25 frames a second
constexpr uint32_t kNetMs   = 5000;     // the address, when it might have moved
constexpr uint32_t kCardMs  = 10000;    // the card's free space

// ---------------------------------------------------------------------------
// Settings, and what the panel is running on
// ---------------------------------------------------------------------------
uint8_t        g_index = 0xFF;
plat::LcdCfg   g_cfg;
char           g_why[64] = "not started";   // why the panel is dark, for PANEL and PLUGINS
bool           g_up      = false;           // the glass is ours and being drawn

// The framebuffer, in PSRAM, kept across restarts of the plugin while its
// size does not change: a CONFIG save restarts every plugin.
uint16_t*      g_fb      = nullptr;
size_t         g_fbSize  = 0;
Canvas         g_canvas;
Layout         g_layout;
Dirty          g_dirty;

// What each figure last drew, so only a change is redrawn. A leading 0x01
// can never match a real figure, which is how "draw it again" is said.
char           g_shown[F_COUNT][48];
uint16_t       g_lampCol[plat::kPixelMax];
uint8_t        g_lampN   = 0xFF;
uint32_t       g_textAt  = 0, g_stripAt = 0, g_netAt = 0, g_cardAt = 0;
char           g_addr[24]  = "";
char           g_card[32]  = "";
uint32_t       g_bands     = 0;             // bands sent since start, for PANEL

// The last event: a login, a logoff or a page.
char           g_event[48] = "";
uint16_t       g_pageSeen  = 0;

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
    g_cfg.rotation = BBS_LCD_ROTATION;
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
    else if (!strcmp(key, "rotation")) {
        uint16_t r = g_cfg.rotation;
        numKey(r, key, v, 0, 270);
        if (r % 90) plat::log("panel: rotation = %s is not 0, 90, 180 or 270", v);
        else g_cfg.rotation = r;
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

// fits: the window lies inside the controller's RAM, the right way round
// for the rotation. Said as a reason when it does not.
const char* fits(const plat::LcdCfg& c) {
    const bool swapped = c.rotation == 90 || c.rotation == 270;
    const uint16_t ramW = swapped ? kRamLong : kRamShort, ramH = swapped ? kRamShort : kRamLong;
    if (c.xoff + c.width > ramW)  return "width and x offset run past the controller's RAM";
    if (c.yoff + c.height > ramH) return "height and y offset run past the controller's RAM";
    return nullptr;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void invalidate() {
    for (auto& s : g_shown) { s[0] = '\x01'; s[1] = '\0'; }
    g_lampN = 0xFF;
}

// redrawAll: the whole glass from nothing: the background, the bar, and
// every figure and lamp redrawn on the next pass.
void redrawAll() {
    fill(g_canvas, R(0, 0, g_canvas.w, g_canvas.h), kBg);
    fill(g_canvas, g_layout.bar, kBar);
    invalidate();
    g_dirty.clear();
    g_dirty.add(R(0, 0, g_canvas.w, g_canvas.h));
}

// put: one figure, redrawn and queued if its text changed.
void put(uint8_t f, const char* s, uint16_t fg) {
    if (!strcmp(g_shown[f], s)) return;
    snprintf(g_shown[f], sizeof(g_shown[f]), "%s", s);
    const Rect& r = g_layout.field[f];
    if (empty(r)) return;
    uint16_t bg = (f == F_NAME || f == F_CLOCK) ? kBar : kBg;
    field(g_canvas, r, s, fg, bg, g_layout.big[f], g_layout.align[f]);
    g_dirty.add(r);
}

void uptime(char* out, size_t n, uint32_t now) {
    uint32_t s = now / 1000u;
    uint32_t d = s / 86400u, h = (s / 3600u) % 24u, m = (s / 60u) % 60u;
    if (d) snprintf(out, n, "up %ud %02u:%02u", static_cast<unsigned>(d), static_cast<unsigned>(h),
                    static_cast<unsigned>(m));
    else   snprintf(out, n, "up %02u:%02u", static_cast<unsigned>(h), static_cast<unsigned>(m));
}

// stamp: the event's time, when the clock is set.
void stamp(char* out, size_t n) {
    if (!clk::valid()) { out[0] = '\0'; return; }
    clk::fmt(out, n, "%H:%M ");
}

void event(const char* what, const char* who) {
    char t[8];
    stamp(t, sizeof(t));
    snprintf(g_event, sizeof(g_event), "%s%s %s", t, what, who);
}

void refreshText(uint32_t now) {
    Bbs& b = Bbs::instance();
    const SysConfig& c = syscfg::get();
    char buf[48];

    put(F_NAME, c.boardName[0] ? c.boardName : BBS_NAME, kWhite);
    if (clk::valid()) clk::fmt(buf, sizeof(buf), "%H:%M");
    else snprintf(buf, sizeof(buf), "--:--");
    put(F_CLOCK, buf, kYellow);

    // The directory's own figures: a sysop who is hidden is not on, as far
    // as a desk somebody else can see is concerned (the lights plugin's rule).
    const unsigned on = b.publicBusy(), of = b.publicNodes();
    snprintf(buf, sizeof(buf), "callers on %u of %u", on, of);
    const bool one = !empty(g_layout.field[F_CALLERS]) &&
                     textWidth(buf, true) <= g_layout.field[F_CALLERS].w && empty(g_layout.field[F_LABEL]);
    if (!one) snprintf(buf, sizeof(buf), "%u of %u", on, of);
    put(F_CALLERS, buf, on ? kGreen : kGrey);
    put(F_LABEL, empty(g_layout.field[F_LABEL]) ? "" : "callers on", kGrey);

    if (!g_netAt || now - g_netAt >= kNetMs) {
        g_netAt = now ? now : 1;
        plat::NetInfo ni = plat::netInfo();
        if (ni.ip[0]) snprintf(g_addr, sizeof(g_addr), "%s:%u", ni.ip, static_cast<unsigned>(b.port()));
        else          snprintf(g_addr, sizeof(g_addr), "no network");
    }
    put(F_ADDR, g_addr, kCyan);
    uptime(buf, sizeof(buf), now);
    put(F_UPTIME, buf, kGrey);

    if (!g_cardAt || now - g_cardAt >= kCardMs) {
        g_cardAt = now ? now : 1;
        plat::SdInfo si = plat::sdInfo();
        if (si.mounted) {
            uint32_t mb = si.freeKB / 1024u;
            if (mb >= 1024u)
                snprintf(g_card, sizeof(g_card), "card %u.%u GB free", static_cast<unsigned>(mb / 1024u),
                         static_cast<unsigned>((mb % 1024u) * 10u / 1024u));
            else
                snprintf(g_card, sizeof(g_card), "card %u MB free", static_cast<unsigned>(mb));
        } else {
            snprintf(g_card, sizeof(g_card), "no card");
        }
    }
    put(F_CARD, g_card, strcmp(g_card, "no card") ? kAmber : kGrey);

    const bus::PageSeen& pg = bus::lastPage();
    if (pg.count != g_pageSeen) {
        g_pageSeen = pg.count;
        event(pg.ring ? "sysop rung by" : "page from", pg.from[0] ? pg.from : "?");
    }
    put(F_EVENT, g_event[0] ? g_event : "nothing yet", g_event[0] ? kWhite : kGrey);
}

void refreshStrip() {
    const Rect& s = g_layout.strip;
    if (empty(s)) return;
    uint8_t rgbs[plat::kPixelMax * 3];
    uint8_t pct = 0;
    uint8_t n = lights::panelFrame(rgbs, plat::kPixelMax, pct);
    if (n != g_lampN) {                                    // a new length, or none
        fill(g_canvas, s, kBg);
        for (uint8_t i = 0; i < plat::kPixelMax; ++i) g_lampCol[i] = 0xFFFF;
        g_lampN = n;
        g_dirty.add(s);
    }
    uint8_t changed = 0;
    Rect cells[plat::kPixelMax];
    for (uint8_t i = 0; i < n; ++i) {
        uint16_t col = rgb(glassLevel(rgbs[i * 3], pct), glassLevel(rgbs[i * 3 + 1], pct),
                           glassLevel(rgbs[i * 3 + 2], pct));
        if (!col) col = kUnlit;
        if (col == g_lampCol[i]) continue;
        g_lampCol[i] = col;
        Rect cell = stripCell(s, i, n);
        lamp(g_canvas, cell, col, kRim, kBg);
        cells[changed++] = cell;
    }
    // A few lamps a lamp at a time; most of the strip as the whole row,
    // which is one queued rectangle rather than a queue full of small ones.
    if (changed > 3) g_dirty.add(s);
    else for (uint8_t i = 0; i < changed; ++i) g_dirty.add(cells[i]);
}

// flush: one band, if the last one has gone.
void flush() {
    if (!plat::lcdReady()) return;
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
    defaults();
    plugins::forEachKey(g_index, readKey, nullptr);
    g_up = false;

    if (const char* why = fits(g_cfg)) {
        snprintf(g_why, sizeof(g_why), "%s", why);
        plat::log("panel: %s; the panel stays dark", why);
        return true;                  // on, and PANEL says why it is dark
    }
    const size_t need = static_cast<size_t>(g_cfg.width) * g_cfg.height * 2u;
    if (g_fb && g_fbSize != need) {
        plat::psramFree(g_fb);
        g_fb = nullptr;
        g_fbSize = 0;
    }
    if (!g_fb) {
        g_fb = static_cast<uint16_t*>(plat::psramAlloc(need));
        if (!g_fb) {
            snprintf(g_why, sizeof(g_why), "no PSRAM for a %u x %u framebuffer",
                     static_cast<unsigned>(g_cfg.width), static_cast<unsigned>(g_cfg.height));
            plat::log("panel: %s", g_why);
            return true;
        }
        g_fbSize = need;
    }
    g_canvas = { g_fb, g_cfg.width, g_cfg.height };
    g_layout = layout(g_cfg.width, g_cfg.height);

    // A CONFIG save restarts every plugin. Settings that did not change keep
    // the panel as it is, lit, rather than taking it through its reset.
    if (!plat::lcdSame(g_cfg)) {
        char err[64] = "";
        if (!plat::lcdBegin(g_cfg, err, sizeof(err))) {
            snprintf(g_why, sizeof(g_why), "%s", err);
            return true;
        }
    } else {
        plat::lcdBacklight(g_cfg.backlight);
    }
    redrawAll();
    g_textAt = g_stripAt = g_netAt = g_cardAt = 0;
    g_bands = 0;
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
    if (g_up) plat::lcdBacklight(0);
    g_up = false;
    g_dirty.clear();
}

void tick(uint32_t now) {
    if (!g_up) return;
    if (!g_textAt || now - g_textAt >= kTextMs) {
        g_textAt = now ? now : 1;
        refreshText(now);
    }
    if (!g_stripAt || now - g_stripAt >= kStripMs) {
        g_stripAt = now ? now : 1;
        refreshStrip();
    }
    flush();
}

// A caller the panel may name: not a hidden or lurking member of staff,
// whose arrival would show on a desk the board's owner does not control.
bool shown(const Session& s) {
    return s.visible && !s.lurk && s.user[0];
}

void onLogin(Session& s) {
    if (shown(s)) event(s.guest ? "guest in" : "login", s.user);
}

void onLogoff(Session& s) {
    if (shown(s)) event("logoff", s.user);
}

const char* status() {
    static char out[48];
    if (g_up) snprintf(out, sizeof(out), "Panel: %ux%u ST7789, %u bands sent",
                       static_cast<unsigned>(g_cfg.width), static_cast<unsigned>(g_cfg.height),
                       static_cast<unsigned>(g_bands));
    else      snprintf(out, sizeof(out), "Panel: dark, %.34s", g_why);
    return out;
}

// ---------------------------------------------------------------------------
// PANEL: what the panel is running on, and what it shows, as text. What a
// sysop at the bench compares with the glass, and what the tests read.
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
    b.rowTitle(s, "Panel", g_up ? "lit" : "dark");
    if (!g_up) {
        snprintf(buf, sizeof(buf), "Dark: %.40s", g_why);
        line(s, Color::LightRed, buf);
    }
    snprintf(buf, sizeof(buf), "ST7789 %ux%u at %u,%u, turned %u",
             static_cast<unsigned>(g_cfg.width), static_cast<unsigned>(g_cfg.height),
             static_cast<unsigned>(g_cfg.xoff), static_cast<unsigned>(g_cfg.yoff),
             static_cast<unsigned>(g_cfg.rotation));
    line(s, Color::Yellow, buf);
    snprintf(buf, sizeof(buf), "Pins %d %d %d %d %d %d, %u MHz",
             g_cfg.mosi, g_cfg.sclk, g_cfg.cs, g_cfg.dc, g_cfg.rst, g_cfg.bl,
             static_cast<unsigned>(g_cfg.mhz));
    line(s, Color::Grey, buf);
    snprintf(buf, sizeof(buf), "%u bands sent", static_cast<unsigned>(g_bands));
    line(s, Color::Grey, buf);
    if (g_up) {
        for (uint8_t f = 0; f < F_COUNT; ++f) {
            if (empty(g_layout.field[f]) || g_shown[f][0] == '\x01' || !g_shown[f][0]) continue;
            snprintf(buf, sizeof(buf), "  %.44s", g_shown[f]);
            line(s, Color::White, buf);
        }
        uint8_t n = g_lampN == 0xFF ? 0 : g_lampN;
        snprintf(buf, sizeof(buf), "  strip: %u lamps", static_cast<unsigned>(n));
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
// for two rows on one pin. Labels are nine columns and notes 38.
// ---------------------------------------------------------------------------
constexpr PluginSetting kSettings[] = {
    { "driver",    "Driver",    PS_INFO,  0, 0,   8, "The panel's controller chip." },
    { "pin",       "Pins",      PS_PAGE,  0, 0,   0, "The panel's SPI and control pins." },
    { "width",     "Width",     PS_NUM,   1, kRamLong,     3, "Pixels across, as the panel is turned." },
    { "height",    "Height",    PS_NUM,   1, kRamLong,     3, "Pixels down, as the panel is turned." },
    { "xoff",      "X offset",  PS_NUM,   0, kRamLong - 1, 3, "Glass's first column in the chip's RAM" },
    { "yoff",      "Y offset",  PS_NUM,   0, kRamLong - 1, 3, "Glass's first row in the chip's RAM." },
    { "rotation",  "Rotation",  PS_CYCLE, 0, 0,   3, "Turns the picture a quarter at a time.", kRotations },
    { "invert",    "Invert",    PS_YESNO, 0, 0,   3, "Yes for an IPS panel (normally black)." },
    { "mirror",    "Mirror",    PS_YESNO, 0, 0,   3, "Yes if text shows back to front." },
    { "colours",   "Colours",   PS_CYCLE, 0, 0,   3, "BGR if red and blue are swapped.", kColours },
    { "spi_mhz",   "SPI MHz",   PS_CYCLE, 0, 0,   2, "10 is safe; the panel's limit is 62.5.", kClocks },
    { "backlight", "Bright %",  PS_NUM,   0, 100, 3, "0 is dark; 60 as shipped." },
    { "pin1_mosi", "MOSI pin",  PS_PIN,   0, BBS_GPIO_OUT_MAX, 2, "Data to the panel: SDA." },
    { "pin2_sclk", "SCLK pin",  PS_PIN,   0, BBS_GPIO_OUT_MAX, 2, "The clock to the panel: SCL." },
    { "pin3_cs",   "CS pin",    PS_PIN,  -1, BBS_GPIO_OUT_MAX, 2, "Chip select. -1 if it is tied low." },
    { "pin4_dc",   "D/C pin",   PS_PIN,   0, BBS_GPIO_OUT_MAX, 2, "Data or command select." },
    { "pin5_rst",  "RST pin",   PS_PIN,  -1, BBS_GPIO_OUT_MAX, 2, "Reset. -1 resets it by command." },
    { "pin6_bl",   "Light pin", PS_PIN,  -1, BBS_GPIO_OUT_MAX, 2, "Backlight. -1 if it is always on." },
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
    else if (!strcmp(key, "rotation"))  snprintf(out, n, "%u", static_cast<unsigned>(g_cfg.rotation));
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
    // strip moves and a band goes out each tick.
    { kName, "Status display", "1.0", 12288, 0, PF_CORE | PF_FAST | PF_ON,
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
