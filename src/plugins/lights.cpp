/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/lights.cpp
 * Module:       Plugins / lights
 *
 * Purpose:      Two outputs of WS2812B pixels for a board in a case: a drive
 *               light that shows storage at work, and a strip of ten that
 *               shows the lines, a Hayes front panel, or something to look
 *               at. Off until a sysop switches it on and gives it a pin.
 *
 * Design:       The pixels are the platform's (plat::pixels*, the RMT
 *               peripheral on the board, a record on the host). This file
 *               only decides colours, fifty times a second, from things the
 *               board already knows: plat::diskSeen for storage, the Bbs's
 *               traffic bits and byte totals for the lines, and the sessions
 *               for who is on.
 *
 *               Every effect's state is a few static bytes: one shared
 *               per-pixel cell, a position, a direction, a step clock. The
 *               effect only changes on a restart, which resets the lot, so
 *               two effects never have to share them at once.
 *
 *               Brightness is a percentage per output, 1 to 100 (1.1.0, Rob:
 *               "remove the limit over 30% ... but allow it"; it was capped
 *               at 30 in the code). Above 30 is the sysop's call and CONFIG
 *               asks first: ten pixels at full white draw about 600 mA, more
 *               than a USB port gives, so a strip that bright wants its own
 *               5 V supply (COMMANDS.md).
 *
 *                 [plugin:lights]
 *                 enabled      = yes
 *                 drive_pin    = 13         -1 is off, as shipped
 *                 drive_fx     = pc         pc | 1541 | disk2 | breathe | off
 *                 drive_bright = 10         percent, 1 to 100
 *                 strip_pin    = 14         -1 is off, as shipped
 *                 strip_fx     = nodes      nodes | hayes | blinken | scanner |
 *                                           c64 | boing | vu | rainbow |
 *                                           manual | off
 *                 strip_bright = 10         percent, 1 to 100
 *                 led1         = sparkle | random    manual mode, per pixel
 *                 strip_count  = 10         pixels on the strip, 1 to 16
 *                 drive_order  = GRB        the bytes' order on the wire:
 *                 strip_order  = GRB        GRB | RGB | BRG | RBG | GBR | BGR
 *
 *               A board profile may ship other defaults (board.h): the
 *               Waveshare S3 has the plugin on, its drive light on the
 *               onboard pixel (GPIO38) in RGB order.
 *
 * Commands:     LIGHTS, LIGHTS TEST
 *
 * Libraries:    none (libc)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     COMMANDS.md, PLUGINS.md, src/plugins/lights.h
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
#include "lights.h"
#include "../core/silent.h"
#include "../core/bbs.h"
#include "../core/bbs_util.h"
#include "../core/form.h"
#include "../core/plugin.h"
#include "../core/sysconfig.h"
#include "../platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using bbsu::ieq;

#ifdef BBS_HOST
// host/platform_host.cpp: the signal plat::wifiRssi reports on the host.
void hostSetRssi(int8_t dbm);
#endif

namespace {

constexpr const char kName[] = "lights";

constexpr uint8_t kOutDrive = 0;
constexpr uint8_t kOutStrip = 1;
// The longest the strip may be. Every per-pixel table below is this long;
// g_count says how much of it is in use.
constexpr uint8_t kStrip    = lights::kPixelsMax;
static_assert(kStrip <= plat::kPixelMax, "the strip is longer than an output may be");
static_assert(lights::kPixelsDefault >= 1 && lights::kPixelsDefault <= kStrip, "a default the strip can have");

// Brightness, percent of full drive. kPctClamp is what the plugin will do,
// whatever system.cfg says: 100 since 1.1.0 (Rob), when it had been 30. A
// hand-edited 150 is read as 100. kPctMax is the CONFIG rows' range, the
// same 100, and kPctWarn is where the form starts to ask first
// (PluginSetting::warnAbove): past 30, ten pixels can brown out a board on USB.
constexpr uint8_t kPctMax   = 100;
constexpr uint8_t kPctWarn  = 30;
constexpr uint8_t kPctClamp = 100;
constexpr uint8_t kPctDef   = 10;

// ---------------------------------------------------------------------------
// The lists, as numbers. The words are lights.h's, in the same order.
// ---------------------------------------------------------------------------
enum : uint8_t { DF_PC, DF_1541, DF_DISK2, DF_BREATHE, DF_OFF };
enum : uint8_t { SF_NODES, SF_HAYES, SF_BLINKEN, SF_SCANNER, SF_C64, SF_BOING, SF_VU,
                 SF_RAINBOW, SF_MANUAL, SF_OFF, SF_WIFI };
enum : uint8_t { LF_SOLID, LF_BLINK, LF_BREATHE, LF_FLICKER, LF_SPARKLE, LF_TRAFFIC,
                 LF_NODE, LF_OFF };
enum : uint8_t { LC_RANDOM = 10, LC_CYCLE = 11 };

// wordIndex: where word sits in a bar-separated list, -1 when it does not.
// constexpr so the enums above are checked against lights.h at build time:
// a word inserted into a list there would otherwise renumber these silently.
constexpr bool wordIs(const char* p, const char* w) {
    return (*p == '|' || *p == '\0') ? *w == '\0' : (*p == *w && wordIs(p + 1, w + 1));
}
constexpr const char* nextWord(const char* p) {
    return *p == '\0' ? nullptr : *p == '|' ? p + 1 : nextWord(p + 1);
}
constexpr int wordIndex(const char* list, const char* w, int at = 0) {
    return !list ? -1 : wordIs(list, w) ? at : wordIndex(nextWord(list), w, at + 1);
}
static_assert(wordIndex(lights::kDriveFx, "off") == DF_OFF, "kDriveFx and DF_ disagree");
static_assert(wordIndex(lights::kStripFx, "manual") == SF_MANUAL &&
              wordIndex(lights::kStripFx, "off") == SF_OFF &&
              wordIndex(lights::kStripFx, "wifi") == SF_WIFI, "kStripFx and SF_ disagree");
static_assert(wordIndex(lights::kLedFx, "node") == LF_NODE &&
              wordIndex(lights::kLedFx, "off") == LF_OFF, "kLedFx and LF_ disagree");
static_assert(wordIndex(lights::kColours, "random") == LC_RANDOM &&
              wordIndex(lights::kColours, "cycle") == LC_CYCLE &&
              wordIndex(lights::kColours, "cycle") == lights::kColourDefault,
              "kColours and LC_ disagree");
// The order words are plat::PixOrder's numbers, and the platform's wire
// table is indexed by them.
static_assert(wordIndex(lights::kOrders, "GRB") == plat::PIX_GRB &&
              wordIndex(lights::kOrders, "RGB") == plat::PIX_RGB &&
              wordIndex(lights::kOrders, "BGR") == plat::PIX_BGR &&
              wordIndex(lights::kOrders, "BGR") + 1 == plat::PIX_ORDERS, "kOrders and PixOrder disagree");
static_assert(BBS_LIGHTS_DRIVE_ORDER < plat::PIX_ORDERS, "a board's drive order is one of kOrders");

// ---------------------------------------------------------------------------
// Colours, at full scale. Every one goes through shade() on its way out.
// ---------------------------------------------------------------------------
struct Rgb { uint8_t r, g, b; };

constexpr Rgb kBlack     = {   0,   0,   0 };
constexpr Rgb kAmber     = { 255, 130,   0 };   // the card
constexpr Rgb kCoolWhite = { 170, 200, 255 };   // the board's own flash
constexpr Rgb kRed       = { 255,   0,   0 };   // an error, and every lamp on a panel
constexpr Rgb kWhite     = { 255, 255, 255 };

// Manual mode's palette, in kColours order up to white.
constexpr Rgb kPalette[] = {
    { 255,   0,   0 }, { 255,  72,   0 }, { 255, 130,   0 }, { 255, 210,   0 },
    {   0, 255,   0 }, {   0, 210, 255 }, {   0,  32, 255 }, { 140,   0, 255 },
    { 255,  30, 110 }, { 255, 255, 255 },
};
static_assert(sizeof(kPalette) / sizeof(kPalette[0]) == LC_RANDOM, "one colour per palette word");

// The terminal palette as light, so a caller's pixel is the colour of their
// rank marker in WHO (bbsu::markColor): Grey for a caller, DarkGrey for a
// guest, Yellow for a co-sysop, LightRed for the sysop. In Color's order.
constexpr Rgb kTermRgb[] = {
    {   0,   0,   0 }, { 255, 255, 255 }, { 200,   0,   0 }, {   0, 200, 200 },
    { 160,   0, 200 }, {   0, 200,   0 }, {   0,   0, 220 }, { 255, 190,   0 },
    { 255, 100,   0 }, { 140,  70,   0 }, { 255,  50,  50 }, {  50,  50,  50 },
    { 130, 130, 130 }, {  80, 255,  80 }, {  80, 120, 255 }, { 190, 190, 190 },
};
static_assert(sizeof(kTermRgb) / sizeof(kTermRgb[0]) == static_cast<size_t>(Color::LightGrey) + 1,
              "one light per terminal colour");

// The Commodore breadbin's badge stripes, for c64.
constexpr Rgb kStripe[] = {
    { 255, 0, 0 }, { 255, 90, 0 }, { 255, 210, 0 }, { 0, 200, 0 }, { 0, 60, 255 },
};

// The Hayes Smartmodem's front panel, left to right. MR is power, TR the
// terminal (our listener), then the line.
const char* const kHayes[] = { "HS", "AA", "CD", "OH", "RD", "SD", "TR", "MR" };

// ---------------------------------------------------------------------------
// Timing, in milliseconds
// ---------------------------------------------------------------------------
constexpr uint32_t kPcHold      = 60;      // the least a pc flash lasts: a 2 ms read shows
constexpr uint32_t k1541Hold    = 150;     // bridges the gaps inside one access
constexpr uint32_t kDisk2Hold   = 1000;    // the Disk II's motor, still turning
constexpr uint32_t kBreatheHold = 80;
constexpr uint32_t kErrorShow   = 10000;   // how long an error blinks
constexpr uint32_t kErrorHalf   = 500;     // on half a second, off half a second
constexpr uint8_t  kIdleGlow    = 24;      // of 255: the drive light at rest
constexpr uint32_t kTestStep    = 1000;    // LIGHTS TEST: a second a colour

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
uint8_t g_index    = 0xFF;
int8_t  g_drivePin = -1;
#ifdef BBS_HAS_CAMERA
bool    g_flash    = false;     // the camera is taking a picture (lights::flash)
#endif
int8_t  g_stripPin = -1;
uint8_t g_driveFx  = DF_PC;
uint8_t g_stripFx  = SF_NODES;
uint8_t g_drivePct = kPctDef;
uint8_t g_stripPct = kPctDef;
uint8_t g_count    = lights::kPixelsDefault;   // pixels on the strip, 1 to kStrip
uint8_t g_driveOrd = BBS_LIGHTS_DRIVE_ORDER;   // plat::PixOrder
uint8_t g_stripOrd = plat::PIX_GRB;
uint8_t g_ledFx[kStrip];
uint8_t g_ledCol[kStrip];
bool    g_driveOn  = false;
bool    g_stripOn  = false;
// Silent mode (1.1.0, core/silent): each output's dark frame, once the wire
// has taken it. Nothing else is sent while silent lasts, and every setting
// and effect is left exactly as it was for when it ends.
bool    g_darkDrive = false;
bool    g_darkStrip = false;
constexpr uint8_t kDark[kStrip * 3] = {};
#ifdef BBS_HAS_LCD
bool    g_panel    = false;                    // the panel shows the strip
uint8_t g_shown[kStrip * 3];                   // the strip's last frame, for it
bool    g_running  = false;                    // between start and stop
#endif

// ---------------------------------------------------------------------------
// What the effects remember. All of it reset by start().
// ---------------------------------------------------------------------------
plat::DiskSeen g_seen;              // the counts at the last frame
uint32_t g_accessAt = 0;            // the latest card or flash pulse
uint32_t g_burstAt  = 0;            // when the run of pulses it belongs to began
uint8_t  g_accessKind = plat::DISK_FLASH;
bool     g_accessed = false;        // any access since start
uint32_t g_errorAt  = 0;
bool     g_errored  = false;
uint32_t g_testAt   = 0;            // LIGHTS TEST began, 0 when not running
uint32_t g_rng      = 1;
uint32_t g_inAt     = 0;            // the Bbs's byte totals at the last frame
uint32_t g_outAt    = 0;
uint8_t  g_act      = 0;            // board traffic, smoothed, 0 to 255
uint8_t  g_cell[kStrip];            // per pixel: a blip, a level, a sparkle
uint8_t  g_mark[kStrip];            // manual: the cycle a random colour was drawn in
uint8_t  g_hue[kStrip];             // manual: that random colour
uint8_t  g_pos   = 0;
int8_t   g_dir   = 1;
uint8_t  g_phase = 0;
uint8_t  g_vu    = 0;               // tenths of a pixel
uint16_t g_bits  = 0;
uint32_t g_stepAt = 0;
uint8_t  g_rd = 0, g_sd = 0;        // hayes: the RD and SD blips
int8_t   g_rssi   = 0;              // wifi: the signal in dBm, 0 when not joined
uint32_t g_rssiAt = 0;              // when it was last asked for, 0 never

// wifi: the meter's scale and colours. -90 dBm or weaker lights one pixel
// and -50 or stronger all of them; the colours are SYS's words for the
// signal: green from -67 (good), amber from -75 (fair), red below (weak).
constexpr int      kRssiLow   = -90;
constexpr int      kRssiHigh  = -50;
constexpr int      kRssiGood  = -67;
constexpr int      kRssiFair  = -75;
constexpr uint32_t kRssiEvery = 1000;   // plat::wifiRssi asks the Wi-Fi driver: once a second

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
uint32_t rnd() {                    // xorshift32, seeded from the hardware at start
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

// shade: one channel at an effect's level (0 to 255) and an output's cap (1
// to 100 percent), in one rounding. Down, so the cap is a ceiling nothing
// crosses: 76 of 255 at 30%, 127 at 50%, 255 at 100%; then 1 for any
// channel that was lit, so a dim colour at 1% stays a colour rather than
// going out.
uint8_t shade(uint8_t v, uint8_t level, uint8_t pct) {
    if (!v || !level) return 0;
    uint32_t o = static_cast<uint32_t>(v) * level * pct / (255u * 100u);
    return static_cast<uint8_t>(o ? o : 1u);
}

void put(uint8_t* f, uint8_t i, Rgb c, uint8_t level, uint8_t pct) {
    f[i * 3]     = shade(c.r, level, pct);
    f[i * 3 + 1] = shade(c.g, level, pct);
    f[i * 3 + 2] = shade(c.b, level, pct);
}

// wheel: a hue, 0 to 255, at full saturation.
Rgb wheel(uint8_t h) {
    uint8_t region = static_cast<uint8_t>(h / 43u);
    uint8_t rise   = static_cast<uint8_t>((h - region * 43u) * 6u);
    uint8_t fall   = static_cast<uint8_t>(255u - rise);
    switch (region) {
        case 0:  return { 255, rise, 0 };
        case 1:  return { fall, 255, 0 };
        case 2:  return { 0, 255, rise };
        case 3:  return { 0, fall, 255 };
        case 4:  return { rise, 0, 255 };
        default: return { 255, 0, fall };
    }
}

// tri: a triangle wave over period, 0 to 255 and back.
uint8_t tri(uint32_t t, uint32_t period) {
    uint32_t half  = period / 2u;
    uint32_t phase = t % period;
    uint32_t v = phase < half ? phase * 255u / half : (period - phase) * 255u / half;
    return static_cast<uint8_t>(v > 255u ? 255u : v);
}

// blip: a lamp that flickers while something keeps happening. A hit lights
// it for two frames, then it is dark for at least one before another hit can
// light it again, so a steady stream reads as a flicker rather than a glow.
bool blip(uint8_t& st, bool hit) {
    if (!st && hit) st = 3;
    if (!st) return false;
    bool lit = st > 1;
    --st;
    return lit;
}

// wordOf: the idx-th word of a bar list, into out.
const char* wordOf(const char* list, uint8_t idx, char* out, size_t n) {
    out[0] = '\0';
    for (uint8_t i = 0; list; ++i) {
        const char* bar = strchr(list, '|');
        size_t len = bar ? static_cast<size_t>(bar - list) : strlen(list);
        if (i == idx) {
            snprintf(out, n, "%.*s", static_cast<int>(len), list);
            return out;
        }
        list = bar ? bar + 1 : nullptr;
    }
    return out;
}

// pick: a word's place in a bar list, ignoring case, -1 when it is not there.
int pick(const char* list, const char* w) {
    char word[12];
    for (uint8_t i = 0; ; ++i) {
        wordOf(list, i, word, sizeof(word));
        if (!word[0]) return -1;
        if (ieq(word, w)) return i;
    }
}

// ---------------------------------------------------------------------------
// Reading system.cfg. A value the plugin cannot use is logged and the
// default kept, which for a pin is -1: an output nobody set up correctly is
// an output that stays dark, never one driving a pin somebody else is using.
// ---------------------------------------------------------------------------
void pinKey(int8_t& out, const char* key, const char* v) {
    char* end = nullptr;
    long  p   = strtol(v, &end, 10);
    bool  num = end && end != v && !*end;
    // The chip's highest output pin (board.h): 33 on the ESP32, whose 34 to
    // 39 are inputs only; 48 on the S3, whose drive light is on 38.
    if (!num || p < -1 || p > BBS_GPIO_OUT_MAX || syscfg::pinProblem(p)) {
        plat::log("lights: %s = %s is not a pin it can use, that output stays off", key, v);
        out = -1;
        return;
    }
    out = static_cast<int8_t>(p);
}

void pctKey(uint8_t& out, const char* key, const char* v) {
    char* end = nullptr;
    long  p   = strtol(v, &end, 10);
    if (!end || end == v || *end) {
        plat::log("lights: %s = %s is not a number, keeping %u%%", key, v, static_cast<unsigned>(out));
        return;
    }
    if (p > kPctClamp || p < 1) {
        long got = p > kPctClamp ? kPctClamp : 1;
        plat::log("lights: %s = %ld is outside 1 to %u, using %ld", key, p,
                  static_cast<unsigned>(kPctClamp), got);
        p = got;
    }
    out = static_cast<uint8_t>(p);
}

// countKey: the strip's length. Past the most an output can carry is read
// as that most, the way a brightness past the ceiling is, and said so.
void countKey(const char* key, const char* v) {
    char* end = nullptr;
    long  n   = strtol(v, &end, 10);
    if (!end || end == v || *end) {
        plat::log("lights: %s = %s is not a number, keeping %u", key, v, static_cast<unsigned>(g_count));
        return;
    }
    if (n < 1 || n > kStrip) {
        long got = n > kStrip ? kStrip : 1;
        plat::log("lights: %s = %ld is outside 1 to %u, using %ld", key, n,
                  static_cast<unsigned>(kStrip), got);
        n = got;
    }
    g_count = static_cast<uint8_t>(n);
}

void wordKey(uint8_t& out, const char* list, const char* key, const char* v) {
    int i = pick(list, v);
    if (i < 0) {
        plat::log("lights: %s = %s is not one of %s", key, v, list);
        return;
    }
    out = static_cast<uint8_t>(i);
}

// ledKey: "led3 = sparkle | random". Either half may be left off, and keeps
// its default: a pixel that says only "blink" blinks in the default colour.
void ledKey(const char* key, const char* v) {
    long n = strtol(key + 3, nullptr, 10);
    if (n < 1 || n > kStrip) return;
    uint8_t at = static_cast<uint8_t>(n - 1);
    char part[16];
    const char* bar = strchr(v, '|');
    size_t len = bar ? static_cast<size_t>(bar - v) : strlen(v);
    for (uint8_t half = 0; half < 2; ++half) {
        while (len && (*v == ' ' || *v == '\t')) { ++v; --len; }
        while (len && (v[len - 1] == ' ' || v[len - 1] == '\t')) --len;
        snprintf(part, sizeof(part), "%.*s", static_cast<int>(len), v);
        if (part[0]) wordKey(half ? g_ledCol[at] : g_ledFx[at], half ? lights::kColours : lights::kLedFx,
                             key, part);
        if (!bar) break;
        v   = bar + 1;
        bar = nullptr;
        len = strlen(v);
    }
}

void readKey(void* ctx, const char* key, const char* value) {
    (void)ctx;
    if      (!strcmp(key, "drive_pin"))    pinKey(g_drivePin, key, value);
    else if (!strcmp(key, "strip_pin"))    pinKey(g_stripPin, key, value);
    else if (!strcmp(key, "drive_fx"))     wordKey(g_driveFx, lights::kDriveFx, key, value);
    else if (!strcmp(key, "strip_fx"))     wordKey(g_stripFx, lights::kStripFx, key, value);
    else if (!strcmp(key, "drive_bright")) pctKey(g_drivePct, key, value);
    else if (!strcmp(key, "strip_bright")) pctKey(g_stripPct, key, value);
    else if (!strcmp(key, "strip_count"))  countKey(key, value);
    else if (!strcmp(key, "drive_order"))  wordKey(g_driveOrd, lights::kOrders, key, value);
    else if (!strcmp(key, "strip_order"))  wordKey(g_stripOrd, lights::kOrders, key, value);
    else if (!strncmp(key, "led", 3) && key[3] >= '1' && key[3] <= '9') ledKey(key, value);
}

// ---------------------------------------------------------------------------
// Who is on the lines, gathered once a frame for the effects that care.
// ---------------------------------------------------------------------------
struct Lines {
    char mark[kStrip];     // 0 for a free line, else its WHO marker: ' ' * > ]
    bool oh;               // a line is off hook: somebody is on one, the busy line too
    bool cd;               // a caller has carrier: connected and past detection
    bool slow;             // every such caller has BAUD at 2400 or under
};

void gather(Lines& l) {
    memset(&l, 0, sizeof(l));
    l.slow = true;
    bool anyone = false;
    struct Ctx { Lines* l; bool* anyone; } ctx{ &l, &anyone };
    Bbs::instance().eachSession([](void* p, Session& s) {
        Ctx& c = *static_cast<Ctx*>(p);
        if (s.st == SState::Free) return;
        if (s.role == Role::Busy) { c.l->oh = true; return; }
        if (s.role != Role::Caller || !s.id) return;    // the sysop's line is not one
        c.l->oh = true;
        if (s.st != SState::Detect) {
            c.l->cd = true;
            *c.anyone = true;
            uint16_t cps = s.tl.cps();
            if (!cps || cps * 10u > 2400u) c.l->slow = false;
        }
        // Hidden or lurking staff look like a free line in WHO, and so here:
        // the case is on a desk somebody else can see.
        if (!s.visible || s.lurk) return;
        // A strip shorter than the board has lines shows the first of them
        // (1.1.0): pixel i is node i + 1, and a node past the end has none.
        if (s.id > g_count) return;
        c.l->mark[s.id - 1] = bbsu::markFor(s);
    }, &ctx);
    if (!anyone) l.slow = false;           // HS: on, with nobody to be slow for
}

// ---------------------------------------------------------------------------
// The drive light
// ---------------------------------------------------------------------------
void noteDisk() {
    plat::DiskSeen d = plat::diskSeen();
    bool card  = d.count[plat::DISK_CARD]  != g_seen.count[plat::DISK_CARD];
    bool flash = d.count[plat::DISK_FLASH] != g_seen.count[plat::DISK_FLASH];
    if (card || flash) {
        bool useCard = card && (!flash ||
                       static_cast<int32_t>(d.at[plat::DISK_CARD] - d.at[plat::DISK_FLASH]) >= 0);
        uint32_t at = useCard ? d.at[plat::DISK_CARD] : d.at[plat::DISK_FLASH];
        if (!g_accessed || at - g_accessAt > kPcHold) g_burstAt = at;     // a new run of pulses
        g_accessAt   = at;
        g_accessKind = useCard ? plat::DISK_CARD : plat::DISK_FLASH;
        g_accessed   = true;
    }
    if (d.count[plat::DISK_ERROR] != g_seen.count[plat::DISK_ERROR]) {
        g_errorAt = d.at[plat::DISK_ERROR];
        g_errored = true;
    }
    g_seen = d;
}

void drawDrive(uint32_t now, uint8_t* f) {
    put(f, 0, kBlack, 0, g_drivePct);
    if (g_driveFx == DF_OFF) return;
    uint32_t since = now - g_accessAt;
    bool lit = false;
    if (g_accessed) {
        switch (g_driveFx) {
            case DF_PC:
                lit = since < kPcHold;
                // A run of reads longer than one flash flickers, the way a
                // PC/XT's light did through a long load: mostly on, now and
                // then a frame off.
                if (lit && now - g_burstAt > kPcHold && (rnd() & 3u) == 0) lit = false;
                break;
            case DF_1541:    lit = since < k1541Hold;    break;   // solid for the whole access
            case DF_DISK2:   lit = since < kDisk2Hold;   break;   // and the motor runs on
            case DF_BREATHE: lit = since < kBreatheHold; break;
            default:         break;
        }
    }
    if (lit) {
        put(f, 0, g_accessKind == plat::DISK_CARD ? kAmber : kCoolWhite, 255, g_drivePct);
        return;
    }
    // A storage error blinks red, slowly, in every style: on half a second,
    // dark half a second, so it reads as a warning rather than as a dim.
    if (g_errored && now - g_errorAt < kErrorShow) {
        if (((now - g_errorAt) / kErrorHalf) % 2u == 0) put(f, 0, kRed, 255, g_drivePct);
        return;
    }
    if (g_driveFx == DF_BREATHE) {
        put(f, 0, kAmber, static_cast<uint8_t>(8u + tri(now, 4000u) * 5u / 8u), g_drivePct);
        return;
    }
    put(f, 0, kAmber, kIdleGlow, g_drivePct);              // the glow at rest
}

// ---------------------------------------------------------------------------
// The strip, g_count pixels of it (1.1.0: ten until the length became a
// setting). Every effect is drawn for the length it has: nodes shows the
// first g_count lines, hayes cuts or pads its eight lamps, the moving ones
// sweep, bounce and spread over whatever is there, and nothing past the end
// is drawn or sent. At ten, every frame is what it was before.
// ---------------------------------------------------------------------------
void drawStrip(uint32_t now, uint8_t* f, uint16_t rx, uint16_t tx, uint32_t bytes) {
    const uint8_t pct = g_stripPct;
    const uint8_t n   = g_count;
    memset(f, 0, kStrip * 3u);
    Lines lines{};
    const bool needLines = g_stripFx == SF_NODES || g_stripFx == SF_HAYES || g_stripFx == SF_MANUAL;
    if (needLines) gather(lines);
    const uint16_t moved = static_cast<uint16_t>(rx | tx);
    // A hue step that goes round the wheel once along the strip, near
    // enough: 25 at ten pixels, as it always was.
    const uint8_t hueStep = static_cast<uint8_t>(250u / n);

    switch (g_stripFx) {
        case SF_NODES:
            // One pixel a caller line, the colour of the caller's rank in
            // WHO, dipping for a moment whenever their line carries bytes.
            for (uint8_t i = 0; i < n; ++i) {
                bool dip = blip(g_cell[i], (moved >> (i + 1)) & 1u);
                if (!lines.mark[i]) continue;
                Rgb c = kTermRgb[static_cast<uint8_t>(bbsu::markColor(lines.mark[i]))];
                put(f, i, c, dip ? 60 : 255, pct);
            }
            break;

        case SF_HAYES: {
            // A Smartmodem's panel on pixels 0 to 7, from real state. RD and
            // SD are every line's bytes in and out, as the modem's were the
            // one line's.
            bool lamp[8] = {
                !lines.slow,                               // HS: a caller above 2400
                Bbs::instance().answering(),               // AA: taking calls
                lines.cd,                                  // CD: carrier
                lines.oh,                                  // OH: off hook
                blip(g_rd, rx != 0),                       // RD
                blip(g_sd, tx != 0),                       // SD
                Bbs::instance().listening(),               // TR: the listener is up
                true,                                      // MR: powered
            };
            // In their order, cut short on a shorter strip and dark past MR
            // on a longer one.
            for (uint8_t i = 0; i < 8 && i < n; ++i) if (lamp[i]) put(f, i, kRed, 255, pct);
            break;
        }

        case SF_BLINKEN: {
            // An IMSAI's data lamps: a new pattern every 400 ms on a quiet
            // board, every 40 ms on a busy one.
            uint32_t every = 400u - static_cast<uint32_t>(g_act) * 360u / 255u;
            if (now - g_stepAt >= every) {
                g_stepAt = now;
                g_bits   = static_cast<uint16_t>(rnd());
            }
            for (uint8_t i = 0; i < n; ++i)
                if ((g_bits >> i) & 1u) put(f, i, kRed, 255, pct);
            break;
        }

        case SF_SCANNER:
            // Larson: a head sweeping end to end with a tail that fades. One
            // pixel has nowhere to sweep, so its head simply stays lit.
            for (uint8_t i = 0; i < n; ++i)
                g_cell[i] = static_cast<uint8_t>(g_cell[i] * 5u / 8u);
            if (now - g_stepAt >= 70u && n > 1) {
                g_stepAt = now;
                if ((g_dir > 0 && g_pos >= n - 1) || (g_dir < 0 && g_pos == 0)) g_dir = static_cast<int8_t>(-g_dir);
                g_pos = static_cast<uint8_t>(g_pos + g_dir);
            }
            if (g_pos >= n) g_pos = 0;
            g_cell[g_pos] = 255;
            for (uint8_t i = 0; i < n; ++i) put(f, i, kRed, g_cell[i], pct);
            break;

        case SF_C64:
            // The breadbin's stripes, one step along every 400 ms.
            if (now - g_stepAt >= 400u) {
                g_stepAt = now;
                g_phase  = static_cast<uint8_t>((g_phase + 1u) % 5u);
            }
            for (uint8_t i = 0; i < n; ++i) put(f, i, kStripe[(i + g_phase) % 5u], 255, pct);
            break;

        case SF_BOING: {
            // The Amiga ball: three pixels of red and white, bouncing end to
            // end, the checks swapping as it goes so it looks like it spins.
            // On a strip of three or fewer it fills the strip and spins in
            // place.
            const uint8_t ball = n < 3 ? n : 3;
            if (now - g_stepAt >= 90u) {
                g_stepAt = now;
                if (n > ball) {
                    if ((g_dir > 0 && g_pos >= n - ball) || (g_dir < 0 && g_pos == 0)) g_dir = static_cast<int8_t>(-g_dir);
                    g_pos = static_cast<uint8_t>(g_pos + g_dir);
                }
                g_phase = static_cast<uint8_t>(g_phase ^ 1u);
            }
            if (g_pos + ball > n) g_pos = 0;
            for (uint8_t j = 0; j < ball; ++j)
                put(f, static_cast<uint8_t>(g_pos + j), ((j + g_phase) & 1u) ? kWhite : kRed, 255, pct);
            break;
        }

        case SF_VU: {
            // A bar as long as the log of this frame's bytes, 1 to 512 and up,
            // as tenths of the strip, falling back a fiftieth every frame.
            // Green for the first six tenths, then yellow, then red.
            uint8_t want = 0;
            for (uint32_t d = bytes; d && want < 10; d >>= 1) ++want;
            uint8_t fell = g_vu > 2 ? static_cast<uint8_t>(g_vu - 2) : 0;
            g_vu = static_cast<uint8_t>(want * 10u > fell ? want * 10u : fell);   // hundredths
            uint8_t lit = static_cast<uint8_t>((g_vu * n + 99u) / 100u);
            for (uint8_t i = 0; i < lit && i < n; ++i)
                put(f, i, i * 10u < n * 6u ? kTermRgb[static_cast<uint8_t>(Color::Green)]
                        : i * 10u < n * 8u ? kTermRgb[static_cast<uint8_t>(Color::Yellow)] : kRed, 255, pct);
            break;
        }

        case SF_RAINBOW:
            for (uint8_t i = 0; i < n; ++i)
                put(f, i, wheel(static_cast<uint8_t>(now / 20u + i * hueStep)), 255, pct);
            break;

        case SF_WIFI: {
            // The station's signal as a meter (1.1.0, Rob: "add a wifi
            // signal to the light strip effects"). The reading is taken once
            // a second, never a frame: it is a call into the Wi-Fi driver.
            if (!g_rssiAt || now - g_rssiAt >= kRssiEvery) {
                g_rssiAt = now ? now : 1;
                g_rssi   = plat::wifiRssi();
            }
            if (!g_rssi) {
                // Not joined: one red pixel, breathing slowly.
                put(f, 0, kRed, static_cast<uint8_t>(40u + tri(now, 2000u) * 215u / 255u), pct);
                break;
            }
            const int r = g_rssi < kRssiLow ? kRssiLow : g_rssi > kRssiHigh ? kRssiHigh : g_rssi;
            const uint8_t lit = static_cast<uint8_t>(1 + (n - 1) * (r - kRssiLow) / (kRssiHigh - kRssiLow));
            const Rgb c = g_rssi >= kRssiGood ? kTermRgb[static_cast<uint8_t>(Color::Green)]
                        : g_rssi >= kRssiFair ? kAmber : kRed;
            for (uint8_t i = 0; i + 1 < lit; ++i) put(f, i, c, 255, pct);
            // The meter's tip breathes a little, so it reads as live.
            put(f, static_cast<uint8_t>(lit - 1), c, static_cast<uint8_t>(190u + tri(now, 1500u) * 65u / 255u), pct);
            break;
        }

        case SF_MANUAL:
            // led1 to ledN; a pixel past the strip's end is not drawn, even
            // with a setting of its own in the file.
            for (uint8_t i = 0; i < n; ++i) {
                uint8_t  level = 0;
                uint32_t cyc   = now / 3000u;          // when a random colour moves on
                bool     fresh = false;                // or now, for a twinkle
                bool     dip   = false;
                switch (g_ledFx[i]) {
                    case LF_SOLID:   level = 255; break;
                    case LF_BLINK:   level = (now % 1000u) < 500u ? 255 : 0; cyc = now / 1000u; break;
                    case LF_BREATHE: level = tri(now, 3000u); break;
                    case LF_FLICKER: level = static_cast<uint8_t>(120u + rnd() % 136u); break;
                    case LF_SPARKLE:
                        g_cell[i] = static_cast<uint8_t>(g_cell[i] * 3u / 4u);
                        if (rnd() % 50u == 0) { g_cell[i] = 255; fresh = true; }
                        level = g_cell[i];
                        break;
                    case LF_TRAFFIC: level = (moved && (rnd() & 1u)) ? 255 : 0; break;
                    case LF_NODE:            // g_cell is this pixel's blip, as in nodes
                        dip   = blip(g_cell[i], ((moved >> (i + 1)) & 1u) != 0);
                        level = lines.mark[i] ? (dip ? 60 : 255) : 0;
                        break;
                    default:         level = 0; break;
                }
                Rgb c;
                if (g_ledCol[i] == LC_RANDOM) {
                    uint8_t m = static_cast<uint8_t>(cyc);
                    if (g_ledFx[i] == LF_SPARKLE ? fresh : m != g_mark[i]) {
                        g_mark[i] = m;
                        g_hue[i]  = static_cast<uint8_t>(rnd());
                    }
                    c = wheel(g_hue[i]);
                } else if (g_ledCol[i] == LC_CYCLE) {
                    // A slow turn of the wheel, each pixel a tenth of it
                    // ahead of the last, so the strip never changes in step.
                    c = wheel(static_cast<uint8_t>(now / 80u + i * hueStep));
                } else {
                    c = kPalette[g_ledCol[i] < LC_RANDOM ? g_ledCol[i] : 0];
                }
                put(f, i, c, level, pct);
            }
            break;

        default:
            break;                                 // off
    }
}

// drawTest: LIGHTS TEST. Red, green, blue, white, a second each, on every
// pixel of both outputs. If red shows green, the strip is not GRB.
bool drawTest(uint32_t now, uint8_t* drive, uint8_t* strip) {
    uint32_t step = (now - g_testAt) / kTestStep;
    if (step >= 4) { g_testAt = 0; return false; }
    const Rgb seq[4] = { kRed, { 0, 255, 0 }, { 0, 0, 255 }, kWhite };
    put(drive, 0, seq[step], 255, g_drivePct);
    memset(strip, 0, kStrip * 3u);
    for (uint8_t i = 0; i < g_count; ++i) put(strip, i, seq[step], 255, g_stripPct);
    return true;
}

// ---------------------------------------------------------------------------
// tick: one frame, every BBS_PLUGIN_FAST_MS (PF_FAST). Draw, hand it over,
// never wait: a frame the wire is not ready for is simply not sent, and the
// next one is drawn fresh.
// ---------------------------------------------------------------------------
void tick(uint32_t now) {
    Bbs& bbs = Bbs::instance();
    uint16_t rx = 0, tx = 0;
    bbs.takeTraffic(rx, tx);
    uint32_t in = bbs.bytesIn(), out = bbs.bytesOut();
    uint32_t bytes = (in - g_inAt) + (out - g_outAt);
    g_inAt  = in;
    g_outAt = out;
    uint8_t target = bytes >= 64u ? 255 : static_cast<uint8_t>(bytes * 4u);
    g_act = target > g_act ? target : (g_act > 4 ? static_cast<uint8_t>(g_act - 4) : 0);
    noteDisk();
    // Silent: every pixel dark, and kept dark. A dark frame is offered until
    // the wire takes it (a frame offered mid-send is skipped), then nothing
    // is drawn or sent. A LIGHTS TEST under way stops with it.
    if (board::silent()) {
        g_testAt = 0;
        if (g_driveOn && !g_darkDrive) g_darkDrive = plat::pixelsShow(kOutDrive, kDark, 1);
        if (g_stripOn && !g_darkStrip) g_darkStrip = plat::pixelsShow(kOutStrip, kDark, g_count);
        return;
    }
    g_darkDrive = g_darkStrip = false;
#ifdef BBS_HAS_LCD
    // The panel's strip is drawn whether or not a strip is wired.
    if (!g_driveOn && !g_stripOn && !g_panel) return;
#else
    if (!g_driveOn && !g_stripOn) return;
#endif

    uint8_t drive[3];
    uint8_t strip[kStrip * 3];
    if (!g_testAt || !drawTest(now, drive, strip)) {
        drawDrive(now, drive);
        drawStrip(now, strip, rx, tx, bytes);
    }
#ifdef BBS_HAS_CAMERA
    if (g_flash) memset(drive, 255, sizeof(drive));   // the camera's flash: full white
#endif
    if (g_driveOn) plat::pixelsShow(kOutDrive, drive, 1);
    if (g_stripOn) plat::pixelsShow(kOutStrip, strip, g_count);
#ifdef BBS_HAS_LCD
    memcpy(g_shown, strip, sizeof(g_shown));
#endif
}

#ifdef BBS_HAS_CAMERA
}   // namespace

// The camera's flash (lights.h). A plain store: the camera sets it from its
// tick, on the same task as this plugin's.
bool lights::flash(bool on) {
    g_flash = on && g_driveOn;
    return g_driveOn;
}

namespace {
#endif

#ifdef BBS_HAS_LCD
}   // namespace

// The panel's side of the strip (lights.h). Plain stores and a copy: the
// panel reads it from its own tick, on the same task.
void lights::wantPanel(bool on) {
    g_panel = on;
    if (!on) memset(g_shown, 0, sizeof(g_shown));
}

uint8_t lights::panelFrame(uint8_t* rgb, uint8_t cap, uint8_t& pct) {
    pct = g_stripPct;
    if (!g_running || !rgb) return 0;
    uint8_t n = g_count < cap ? g_count : cap;
    memcpy(rgb, g_shown, static_cast<size_t>(n) * 3u);
    return n;
}

namespace {
#endif

// defaults: every setting as shipped. From start(), before the file is read,
// and from setting() on a board that has never started the plugin, whose
// CONFIG page would otherwise show each pixel as zero, which is red.
bool g_defaulted = false;

void defaults() {
    // The drive pin and its order are the board profile's (board.h): -1 and
    // GRB on the WROOM, the Waveshare S3's onboard pixel on 38 in RGB.
    g_drivePin = BBS_LIGHTS_DRIVE_PIN;
    g_stripPin = -1;
    g_driveFx  = DF_PC;
    g_stripFx  = SF_NODES;
    g_drivePct = g_stripPct = kPctDef;
    g_count    = lights::kPixelsDefault;
    g_driveOrd = BBS_LIGHTS_DRIVE_ORDER;
    g_stripOrd = plat::PIX_GRB;
    for (uint8_t i = 0; i < kStrip; ++i) {
        g_ledFx[i]  = lights::kLedFxDefault;
        g_ledCol[i] = lights::kColourDefault;
    }
    g_defaulted = true;
}

bool start(Bbs& bbs) {
    g_index = plugins::indexOf(kName);
    defaults();
    plugins::forEachKey(g_index, readKey, nullptr);

    // CONFIG refuses this; a hand-edited file can still say it. The drive
    // light keeps the pin, being the one pixel, and the strip goes dark.
    if (g_stripPin >= 0 && g_stripPin == g_drivePin) {
        plat::log("lights: strip_pin %d is the drive pin, the strip stays off", g_stripPin);
        g_stripPin = -1;
    }
    g_driveOn = g_drivePin >= 0 && plat::pixelsBegin(kOutDrive, g_drivePin, 1, g_driveOrd);
    g_stripOn = g_stripPin >= 0 && plat::pixelsBegin(kOutStrip, g_stripPin, g_count, g_stripOrd);
    if (g_drivePin >= 0 && !g_driveOn) plat::log("lights: drive light on gpio %d would not start", g_drivePin);
    if (g_stripPin >= 0 && !g_stripOn) plat::log("lights: strip on gpio %d would not start", g_stripPin);

    // Everything an effect remembers starts again, including what counts as
    // new: pulses and bytes from before this start are not this start's.
    g_seen     = plat::diskSeen();
    g_accessed = false;
    g_accessAt = g_burstAt = 0;
    // Except an error: one from just before this start still blinks for the
    // rest of its time. The sd plugin starts first, so a card that would not
    // mount at boot is only ever seen from here, and a CONFIG save restarts
    // this plugin part way through a blink.
    g_errorAt  = g_seen.at[plat::DISK_ERROR];
    g_errored  = g_seen.count[plat::DISK_ERROR] != 0 && plat::millis() - g_errorAt < kErrorShow;
    g_testAt   = 0;
    g_rng      = plat::random32() | 1u;
    uint16_t rx, tx;
    bbs.takeTraffic(rx, tx);
    g_inAt  = bbs.bytesIn();
    g_outAt = bbs.bytesOut();
    g_act   = 0;
    memset(g_cell, 0, sizeof(g_cell));
    memset(g_mark, 0xFF, sizeof(g_mark));
    for (uint8_t i = 0; i < kStrip; ++i) g_hue[i] = static_cast<uint8_t>(rnd());
    g_pos = 0; g_dir = 1; g_phase = 0; g_vu = 0; g_bits = 0; g_stepAt = 0; g_rd = g_sd = 0;
    g_rssi = 0; g_rssiAt = 0;
    g_darkDrive = g_darkStrip = false;

    char a[10], b[10];
    plat::log("lights: drive %s on gpio %d at %u%%, strip %s on gpio %d at %u%%, %u pixels",
              wordOf(lights::kDriveFx, g_driveFx, a, sizeof(a)), g_drivePin,
              static_cast<unsigned>(g_drivePct),
              wordOf(lights::kStripFx, g_stripFx, b, sizeof(b)), g_stripPin,
              static_cast<unsigned>(g_stripPct), static_cast<unsigned>(g_count));
#ifdef BBS_HAS_LCD
    memset(g_shown, 0, sizeof(g_shown));
    g_running = true;
#endif
    return true;       // on with nothing wired is still on: LIGHTS says so
}

void stop() {
    plat::pixelsEnd(kOutDrive);                // dark first, then the pin goes
    plat::pixelsEnd(kOutStrip);
    g_driveOn = g_stripOn = false;
    g_testAt  = 0;
#ifdef BBS_HAS_CAMERA
    g_flash   = false;
#endif
#ifdef BBS_HAS_LCD
    g_running = false;
    memset(g_shown, 0, sizeof(g_shown));
#endif
}

// ---------------------------------------------------------------------------
// status: the DASH and PLUGINS line
// ---------------------------------------------------------------------------
const char* status() {
    static char line[48];
    char a[10], b[10];
    snprintf(line, sizeof(line), "Lights: drive %s, strip %s",
             g_driveOn ? wordOf(lights::kDriveFx, g_driveFx, a, sizeof(a)) : "off",
             g_stripOn ? wordOf(lights::kStripFx, g_stripFx, b, sizeof(b)) : "off");
    return line;
}

// ---------------------------------------------------------------------------
// LIGHTS: each output, its setting, and the colours it was last given, as
// hex. What a sysop at the bench compares with what the pixels are doing,
// and what the tests read, since on the host these are the only pixels.
// ---------------------------------------------------------------------------
// The order the bytes go out in follows the percentage (1.1.0), so a bench
// check of LIGHTS TEST can be read against it: red showing green on a GRB
// output means the strip is RGB.
void showOutput(Session& s, const char* name, int8_t pin, bool on, const char* fx, uint8_t pct,
                uint8_t out, bool hayes, uint8_t order) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    char buf[48], ord[6];
    if (pin < 0)  snprintf(buf, sizeof(buf), "%-6s no pin, off", name);
    else if (!on) snprintf(buf, sizeof(buf), "%-6s pin %d would not start", name, pin);
    else          snprintf(buf, sizeof(buf), "%-6s pin %d  %s  %u%%  %s", name, pin, fx,
                           static_cast<unsigned>(pct), wordOf(lights::kOrders, order, ord, sizeof(ord)));
    t.color(tl, on ? Color::Yellow : Color::Grey);
    t.text(tl, buf);
    t.nl(tl);
    if (!on) return;
    uint8_t px[plat::kPixelMax * 3];
    uint8_t got = plat::pixelsFrame(out, px, plat::kPixelMax);
    for (uint8_t row = 0; row * 5 < got; ++row) {
        size_t at = 0;
        if (hayes) {
            at = static_cast<size_t>(snprintf(buf, sizeof(buf), "  "));
            for (uint8_t k = static_cast<uint8_t>(row * 5); k < got && k < row * 5 + 5 && at < sizeof(buf); ++k)
                at += static_cast<size_t>(snprintf(buf + at, sizeof(buf) - at, "%-7s",
                                                   k < 8 ? kHayes[k] : ""));
            t.color(tl, Color::DarkGrey);
            t.text(tl, buf);
            t.nl(tl);
        }
        at = static_cast<size_t>(snprintf(buf, sizeof(buf), " "));
        for (uint8_t k = static_cast<uint8_t>(row * 5); k < got && k < row * 5 + 5 && at < sizeof(buf); ++k)
            at += static_cast<size_t>(snprintf(buf + at, sizeof(buf) - at, " %02X%02X%02X",
                                               px[k * 3], px[k * 3 + 1], px[k * 3 + 2]));
        t.color(tl, Color::Grey);
        t.text(tl, buf);
        t.nl(tl);
    }
}

void cmdLights(Bbs& b, Session& s, const char* a, uint32_t now) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    if (ieq(a, "test")) {
        if (board::silent()) {
            t.color(tl, Color::LightRed);
            t.text(tl, "Silent mode is on. CONFIG board.");
        } else if (!g_driveOn && !g_stripOn) {
            t.color(tl, Color::LightRed);
            t.text(tl, "No pixels to test. CONFIG lights sets a pin.");
        } else {
            g_testAt = now ? now : 1;
            t.color(tl, Color::Grey);
            t.text(tl, "Red, green, blue, then white, a second each.");
        }
        b.prompt(s);
        return;
    }
#ifdef BBS_HOST
    // Host only, never on a board: a storage pulse on demand, so a test can
    // put an error in front of the drive light without breaking a card.
    if (ieq(a, "pulse card") || ieq(a, "pulse flash") || ieq(a, "pulse error")) {
        plat::diskPulse(ieq(a, "pulse card")  ? plat::DISK_CARD
                      : ieq(a, "pulse flash") ? plat::DISK_FLASH : plat::DISK_ERROR);
        t.color(tl, Color::Grey);
        t.text(tl, "Pulsed.");
        b.prompt(s);
        return;
    }
    // Host only: the signal the host's radio reports (it has none), so a
    // test can put the wifi meter through strong, weak and not joined.
    // "rssi -70" sets it, "rssi off" is not joined; the meter reads it
    // straight away rather than at its next second.
    if (!strncmp(a, "rssi ", 5) || !strncmp(a, "RSSI ", 5)) {
        const char* v = a + 5;
        hostSetRssi(ieq(v, "off") ? 0 : static_cast<int8_t>(strtol(v, nullptr, 10)));
        g_rssiAt = 0;
        t.color(tl, Color::Grey);
        t.text(tl, "Signal set.");
        b.prompt(s);
        return;
    }
#endif
    if (*a) {
        t.color(tl, Color::LightRed);
        t.text(tl, "LIGHTS, or LIGHTS TEST.");
        b.prompt(s);
        return;
    }
    char fa[10], fb[10];
    b.rowTitle(s, "Lights", board::silent() ? "silent" : g_testAt ? "testing" : nullptr);
    showOutput(s, "Drive", g_drivePin, g_driveOn, wordOf(lights::kDriveFx, g_driveFx, fa, sizeof(fa)),
               g_drivePct, kOutDrive, false, g_driveOrd);
    showOutput(s, "Strip", g_stripPin, g_stripOn, wordOf(lights::kStripFx, g_stripFx, fb, sizeof(fb)),
               g_stripPct, kOutStrip, g_stripFx == SF_HAYES, g_stripOrd);
    b.prompt(s);
}

const Command kCommands[] = {
    { "LIGHTS", "", 0, CF_ADMIN, "LIGHTS [TEST]", "what the pixels show", cmdLights,
      Menu::Sysop, 7 },
};

// ---------------------------------------------------------------------------
// CONFIG. Labels are the form's nine columns and the notes its 38; the words
// are the copy's (internal/copy-1.1.0-2026-09-23.md, section 4) where it had
// them. At 80 columns the labels are twenty and some notes 78, from
// internal/tty-ux-forms-80-2026-09-24.md (1.1.0). "Pixels" is a button to a
// page of its own, which lists led1 to led16 as buttons in turn, each opening
// its Effect and Colour: sixteen more rows than this page could hold. The
// rows are appended, never inserted: the table is filled positionally and
// read in order (1.1.0 added the strip's length, the two outputs' colour
// order, and led11 to led16).
// ---------------------------------------------------------------------------
constexpr PluginSetting kSettings[] = {
    { "drive_pin",    "Drive pin", PS_PIN,  -1, BBS_GPIO_OUT_MAX, 2, "The disk light: one pixel. -1 is off.", nullptr,
      "Drive light GPIO",
      "The disk light, one pixel on this GPIO. -1 is off. 330 ohm in series helps." },
    { "drive_fx",     "Drive fx",  PS_CYCLE, 0,  0, 7, "pc: a flicker on every disk access.",
      lights::kDriveFx, "Drive light effect",
      "pc flickers per access, 1541 holds solid, disk2 lingers, breathe glows, off." },
    // 1 to 100 (1.1.0; Rob: "remove the limit over 30% on light brightness
    // and warn the user are you really sure before applying over 30% but
    // allow it"). CONFIG asks before it saves anything past 30.
    { "drive_bright", "Drive %",   PS_NUM,   1, kPctMax, 3, "1 to 100 percent; over 30 asks first.", nullptr,
      "Drive brightness %",
      "1 to 100 percent, 10 as shipped. Over 30 asks first: a bright pixel runs hot.",
      kPctWarn, "runs the pixel hot.",
      "hot on USB. Keep?" },
    { "strip_pin",    "Strip pin", PS_PIN,  -1, BBS_GPIO_OUT_MAX, 2, "10 pixels need their own 5 V supply.", nullptr,
      "Strip GPIO",
      "Ten pixels on this GPIO. Give them their own 5 V feed: 600 mA at full white." },
    { "strip_fx",     "Strip",     PS_CYCLE, 0,  0, 7, "nodes: one pixel for each caller line.",
      lights::kStripFx, "Strip effect",
      "nodes: one pixel per caller line, in the caller's rank colour. manual: Pixels." },
    { "strip_bright", "Strip %",   PS_NUM,   1, kPctMax, 3, "White at 10 is 60 mA; at 30, 180 mA.", nullptr,
      "Strip brightness %",
      "Ten pixels in white: 60 mA at 10, 180 mA at 30, 600 mA at 100. Over 30 asks.",
      kPctWarn, "may brown out the board without a 5 V supply.",
      "brownout risk. Keep?" },
    { "led",          "Pixels",    PS_PAGE,  0,  0, 0, "Manual: each pixel its own effect.", nullptr,
      "Pixels, by hand" },
    { "led1",  "Pixel 1",  PS_TEXT, 0, 0, 20 },
    { "led2",  "Pixel 2",  PS_TEXT, 0, 0, 20 },
    { "led3",  "Pixel 3",  PS_TEXT, 0, 0, 20 },
    { "led4",  "Pixel 4",  PS_TEXT, 0, 0, 20 },
    { "led5",  "Pixel 5",  PS_TEXT, 0, 0, 20 },
    { "led6",  "Pixel 6",  PS_TEXT, 0, 0, 20 },
    { "led7",  "Pixel 7",  PS_TEXT, 0, 0, 20 },
    { "led8",  "Pixel 8",  PS_TEXT, 0, 0, 20 },
    { "led9",  "Pixel 9",  PS_TEXT, 0, 0, 20 },
    { "led10", "Pixel 10", PS_TEXT, 0, 0, 20 },
    // Appended in 1.1.0. The strip's length: sixteen at most, because the
    // Pixels page holds sixteen rows and every pixel needs one (and both
    // chips can send that many; platform.h, kPixelMax).
    { "strip_count", "Strip len", PS_NUM,   1, kStrip, 2, "1 to 16, one Pixels row for each.", nullptr,
      "Strip length, pixels" },
    { "drive_order", "Drive ord", PS_CYCLE, 0, 0, 3, "If TEST shows red as green, try RGB.",
      lights::kOrders, "Drive colour order",
      "The drive pixel's byte order. If LIGHTS TEST shows red as green, try RGB." },
    { "strip_order", "Strip ord", PS_CYCLE, 0, 0, 3, "If TEST shows red as green, try RGB.",
      lights::kOrders, "Strip colour order",
      "The strip's byte order. If LIGHTS TEST shows red as green, try RGB." },
    { "led11", "Pixel 11", PS_TEXT, 0, 0, 20 },
    { "led12", "Pixel 12", PS_TEXT, 0, 0, 20 },
    { "led13", "Pixel 13", PS_TEXT, 0, 0, 20 },
    { "led14", "Pixel 14", PS_TEXT, 0, 0, 20 },
    { "led15", "Pixel 15", PS_TEXT, 0, 0, 20 },
    { "led16", "Pixel 16", PS_TEXT, 0, 0, 20 },
};
constexpr size_t kSettingCount = sizeof(kSettings) / sizeof(kSettings[0]);

// Which rows are the Pixels page's: the key "led" and a number. Counted
// from the table, so the page limits below cannot drift from it.
constexpr bool ledRow(const char* k) {
    return k[0] == 'l' && k[1] == 'e' && k[2] == 'd' && k[3] >= '1' && k[3] <= '9';
}
constexpr size_t countLed(size_t i = 0) {
    return i == kSettingCount ? 0 : (ledRow(kSettings[i].key) ? 1 : 0) + countLed(i + 1);
}
static_assert(countLed() == kStrip, "one led row per pixel");
static_assert(kStrip == 16, "strip_count's note says 1 to 16");
static_assert(kCoreRows + (kSettingCount - countLed()) <= Form::kMaxFields, "the lights page is full");
static_assert(countLed() <= Form::kMaxFields, "the Pixels page is full");

// setting: the running value of each key, for a board whose system.cfg has
// not been given it yet. A pixel's is its packed "effect | colour", which is
// also what its button shows and its page opens on.
void setting(const char* key, char* out, size_t n) {
    char a[10], b[10];
    if (!g_defaulted) defaults();
    if      (!strcmp(key, "drive_pin"))    snprintf(out, n, "%d", g_drivePin);
    else if (!strcmp(key, "strip_pin"))    snprintf(out, n, "%d", g_stripPin);
    else if (!strcmp(key, "drive_fx"))     snprintf(out, n, "%s", wordOf(lights::kDriveFx, g_driveFx, a, sizeof(a)));
    else if (!strcmp(key, "strip_fx"))     snprintf(out, n, "%s", wordOf(lights::kStripFx, g_stripFx, a, sizeof(a)));
    else if (!strcmp(key, "drive_bright")) snprintf(out, n, "%u", static_cast<unsigned>(g_drivePct));
    else if (!strcmp(key, "strip_bright")) snprintf(out, n, "%u", static_cast<unsigned>(g_stripPct));
    else if (!strcmp(key, "led"))          snprintf(out, n, "%u pixels", static_cast<unsigned>(g_count));
    else if (!strcmp(key, "strip_count"))  snprintf(out, n, "%u", static_cast<unsigned>(g_count));
    else if (!strcmp(key, "drive_order"))  snprintf(out, n, "%s", wordOf(lights::kOrders, g_driveOrd, a, sizeof(a)));
    else if (!strcmp(key, "strip_order"))  snprintf(out, n, "%s", wordOf(lights::kOrders, g_stripOrd, a, sizeof(a)));
    else if (ledRow(key)) {
        long i = strtol(key + 3, nullptr, 10);
        if (i < 1 || i > kStrip) { out[0] = '\0'; return; }
        snprintf(out, n, "%s | %s", wordOf(lights::kLedFx, g_ledFx[i - 1], a, sizeof(a)),
                 wordOf(lights::kColours, g_ledCol[i - 1], b, sizeof(b)));
    }
    else out[0] = '\0';
}

} // namespace

// On as shipped only on a board whose pixel is part of the board (board.h,
// BBS_LIGHTS_ON: the Waveshare S3's onboard WS2812B).
#if BBS_LIGHTS_ON
constexpr uint8_t kLightsFlags = PF_CORE | PF_FAST | PF_ON;
#else
constexpr uint8_t kLightsFlags = PF_CORE | PF_FAST;
#endif

extern const Plugin kLightsPlugin = {
    // PF_FAST: fifty frames a second. Not PF_ON on the reference board:
    // nothing is wired to a board that has just been flashed, and a pin
    // driven blind is a pin somebody else may be using.
    { kName, "Disk light and strip", "1.0", 2048, 0, kLightsFlags,
      PlugLevel::Sysop, PlugLevel::Sysop, PlugLevel::Sysop },
    start,
    stop,
    tick,
    nullptr,                 // onConnect
    nullptr,                 // onLogin
    nullptr,                 // onLogoff
    nullptr,                 // onKey
    status,
    kCommands,
    sizeof(kCommands) / sizeof(kCommands[0]),
    kSettings,
    static_cast<uint8_t>(kSettingCount),
    setting,
    nullptr,                 // rows: no paged list of its own
    nullptr,                 // onPresence: the strip reads the sessions each frame
    nullptr,                 // onBytes
    nullptr,                 // onRename
    nullptr,                 // listDone
};
