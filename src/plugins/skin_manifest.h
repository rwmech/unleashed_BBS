/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/skin_manifest.h
 * Module:       Plugins / panel skins (BBS_HAS_LCD boards, and the host tests)
 *
 * Purpose:      skin.txt, the manifest of a panel skin: its grammar, read
 *               into a Manifest or refused with the line and the reason.
 *               No platform, no heap and no BBS in it, so host/test_skin.cpp
 *               can throw anything at it. SKINS.md is the grammar written
 *               out for a person; tools/mkskin.py checks the same rules and
 *               both are run against host/skins/cases.txt, so the two
 *               readers cannot drift apart unnoticed.
 *
 * Design:       Line by line, every rule checked, nothing guessed. A skin
 *               a sysop made with a typo must say which line and what is
 *               wrong with it, not fall back in silence, so the first fault
 *               found is reported and the rest of the file is not read.
 *
 *               Geometry is checked once the whole file is read, because a
 *               bound depends on the panel line and an overlap on two lines;
 *               each element remembers the line it came from, so the error
 *               still names a line.
 *
 * Grammar (skin format 1):
 *
 *   A line is a directive, a blank line or a comment. A '#' as the first
 *   thing on a line makes it a comment; a ';' anywhere starts a comment that
 *   runs to the end of the line (a '#' elsewhere is a colour). Words are
 *   separated by spaces or tabs, directive and option names are read in any
 *   case, and numbers are plain decimal with no sign. 4096 bytes and 120
 *   characters a line at most.
 *
 *     skin 1                       the format; the first directive, always
 *     panel W H                    the panel it is drawn for, in pixels
 *     name TEXT                    what CONFIG calls it (optional, 24 chars)
 *     drive X Y D STYLE [halo=N]   the drive light: centre, lens diameter,
 *                                  pc | 1541 | disk2 | breathe
 *     activity X Y D [colour=#RRGGBB] [halo=N]
 *                                  the network activity LED
 *     strip N                      N strip LEDs follow, 1 to 16
 *     led I X Y D [halo=N]         strip LED I (1 to N), each exactly once
 *     text X Y W H [size=small|big] [colour=#RRGGBB] [shadow=#RRGGBB|none]
 *                  [background=#RRGGBB|none] [align=left|centre|right]
 *                                  the status rectangle
 *     lines WORD ...               what the rectangle shows, top to bottom
 *     clock X Y [size=..] [colour=..] [shadow=..] [background=..]
 *                                  HH:MM, top left at X Y
 *
 *   The live widgets (1.2.0), each placed as often as its limit allows, so
 *   the drawn machine's own parts carry the board's figures:
 *
 *     field X Y W VALUE [label=TEXT] [size] [colour] [shadow] [background]
 *                  [align]         one line bound to a value, one glyph tall;
 *                                  label a fixed prefix, _ for a space
 *     digits X Y H N VALUE [colour=#] [dim=#|none]
 *                                  an N-digit seven-segment display, H tall
 *     nodes X Y W H [size] [colour] [shadow] [background] [free=yes|no]
 *                  [dim=#RRGGBB|none] [tint=yes|no]
 *                                  who is on each line, one row a line
 *     events X Y W H [size] [colour] [shadow] [background]
 *                  [order=newest|oldest] [tint=yes|no]
 *                                  the last logins, logoffs, pages and rings
 *     meter X Y W H SOURCE [colour] [background] [dir=right|up]
 *     graph X Y W H SOURCE [colour] [background]
 *                                  a bar, or a sweep of the last samples:
 *                                  traffic heap card callers rssi (a graph
 *                                  takes traffic or callers)
 *     lamp X Y D STATE [colour=#] [halo=N] [blink=yes|no]
 *                                  a lens lit by a state: rx tx disk run
 *                                  closed open online ring mail staff listed
 *                                  card error, node1..node16 or sysop (the
 *                                  sysop's line); blink for a state that
 *                                  calls somebody
 *
 *   nodes also take dim=#RRGGBB (waiting rows) and tint=yes (the mark by
 *   rank), events tint=yes (the kind by kind). A node list with more on
 *   than rows ends in "+n more".
 *
 *   Sizes are tiny (6 x 12), small (8 x 16) and big (16 x 32). Values are
 *   the lines' words less who and blank, and online, lines, lastcaller,
 *   rssi, peak, version; a seven-segment display takes the numeric ones.
 *   Every widget's box joins the bounds and overlap rules, and lamps count
 *   towards the LEDs' pixels.
 *
 *   Everything but skin and panel is optional, and each directive appears
 *   once (led once per LED). D is 2 to 64 pixels, a halo 0 to 32 (half of
 *   D when not given, 32 at most). An LED's drawn box is D + 2 x halo
 *   square, centred on X Y. Every box lies inside the panel and no two
 *   boxes (LEDs, text, clock) overlap, so redrawing one never paints over
 *   another. The LEDs' boxes cover kLedPixelsMax pixels between them at
 *   most. lines needs text, text needs lines, and the words must fit the
 *   rectangle's height.
 *
 * Libraries:    none
 * Targets:      ESP32-S3 boards with BBS_HAS_LCD, and the Linux host tests
 * See also:     SKINS.md, src/plugins/skin.cpp, tools/mkskin.py
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
#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>

namespace skin {

// ---------------------------------------------------------------------------
// Limits. Each is a rule of the format, the same in mkskin.py.
// ---------------------------------------------------------------------------
constexpr uint16_t kFormat       = 1;      // the skin line this board reads
constexpr size_t   kFileMax      = 4096;   // bytes of skin.txt
constexpr size_t   kLineMax      = 120;    // characters a line, CR not counted
constexpr uint16_t kPanelMax     = 1024;   // a panel side, pixels
constexpr uint8_t  kStripMax     = 16;     // plat::kPixelMax, the most a strip may be
constexpr uint8_t  kLedMin       = 2;      // a lens diameter, pixels
constexpr uint8_t  kLedMax       = 64;
constexpr uint8_t  kHaloMax      = 32;
constexpr uint8_t  kNameMax      = 24;     // characters of name
constexpr uint8_t  kLinesMax     = 16;     // words on the lines line
constexpr uint8_t  kTokensMax    = 20;     // words a line may have (lines and its 16, and room)
constexpr uint8_t  kLabelMax     = 12;     // a field's label, characters
// The live widgets (1.2.0): how many of each a skin may place.
constexpr uint8_t  kFieldsMax    = 24;
constexpr uint8_t  kDigitsMax    = 4;
constexpr uint8_t  kListsMax     = 2;      // node lists, and event lists, each
constexpr uint8_t  kMetersMax    = 8;
constexpr uint8_t  kGraphsMax    = 4;
constexpr uint8_t  kLampsMax     = 32;
constexpr uint8_t  kDigitsNMax   = 6;      // digits a seven-segment display may have
constexpr uint8_t  kDigitsHMin   = 10, kDigitsHMax = 64;
constexpr uint8_t  kNodeLines    = 16;     // lamps bound to node1..node16
// The LEDs' and lamps' boxes together: each has a weight map two bytes a
// pixel, so this is 128 KB of PSRAM at most, beside the panel's framebuffer
// and the background's copy.
constexpr uint32_t kLedPixelsMax = 65536;
// The most one widget unit may draw at once (a text row, a field, the clock,
// a display, a list's row, a meter, a graph): the widgets' budget a pass
// (skin_draw.h kWidgetBudget), so no single unit can take a pass past it.
constexpr uint32_t kUnitPxMax    = 16384;

// The panel's three faces: small 8 x 16 and big 16 x 32 (panel_gfx.h
// kSmallW..kBigH) and tiny 6 x 12 (1.2.0, panelfont::kTiny), repeated so
// this header stands alone; test_skin.cpp checks they agree.
enum Face : uint8_t { FACE_SMALL, FACE_BIG, FACE_TINY, FACE_COUNT };
constexpr int kGlyphW[FACE_COUNT] = { 8, 16, 6 };
constexpr int kGlyphH[FACE_COUNT] = { 16, 32, 12 };

// The drive light's styles, in lights::kDriveFx's order ("pc|1541|disk2|
// breathe|off"), so a style is the number the lights plugin already uses.
enum DriveStyle : uint8_t { DS_PC, DS_1541, DS_DISK2, DS_BREATHE, DS_COUNT };
constexpr const char* kDriveWords[DS_COUNT] = { "pc", "1541", "disk2", "breathe" };

// What a line of the status rectangle, or a field, can show. The first
// thirteen are the lines' own (1.2.0); the rest came with the fields
// (1.2.0). who and blank belong to lines only.
enum LineWord : uint8_t {
    LW_BLANK, LW_NAME, LW_ADDRESS, LW_UPTIME, LW_CALLERS, LW_TODAY, LW_HEAP,
    LW_CARD, LW_CLOCK, LW_DATE, LW_LAST, LW_RING, LW_WHO,
    LW_ONLINE, LW_LINES, LW_LASTCALLER, LW_RSSI, LW_PEAK, LW_VERSION, LW_COUNT
};
constexpr const char* kLineWords[LW_COUNT] = {
    "blank", "name", "address", "uptime", "callers", "today", "heap",
    "card", "clock", "date", "last", "ring", "who",
    "online", "lines", "lastcaller", "rssi", "peak", "version"
};
// The values a seven-segment display takes: numbers only.
constexpr bool numericWord(uint8_t w) {
    return w == LW_ONLINE || w == LW_LINES || w == LW_TODAY || w == LW_PEAK || w == LW_RSSI ||
           w == LW_HEAP || w == LW_CLOCK;
}

// A meter's or a graph's source.
enum Source : uint8_t { SRC_TRAFFIC, SRC_HEAP, SRC_CARD, SRC_CALLERS, SRC_RSSI, SRC_COUNT };
constexpr const char* kSourceWords[SRC_COUNT] = { "traffic", "heap", "card", "callers", "rssi" };
constexpr bool graphSource(uint8_t s) { return s == SRC_TRAFFIC || s == SRC_CALLERS; }

// A lamp's state: node1..node16 are ST_NODE + line - 1.
enum LampState : uint8_t {
    ST_RX, ST_TX, ST_DISK, ST_RUN, ST_CLOSED, ST_RING, ST_MAIL, ST_STAFF, ST_LISTED, ST_CARD, ST_ERROR,
    ST_ONLINE, ST_OPEN, ST_NODE_S,
    ST_NODE, ST_COUNT = ST_NODE + kNodeLines
};
constexpr const char* kStateWords[ST_NODE] = {
    "rx", "tx", "disk", "run", "closed", "ring", "mail", "staff", "listed", "card", "error",
    "online", "open", "sysop"
};

enum Align : uint8_t { AL_LEFT, AL_CENTRE, AL_RIGHT };

struct Rgb { uint8_t r = 0, g = 0, b = 0; };

inline bool sameRgb(const Rgb& a, const Rgb& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

struct Box {
    int16_t x = 0, y = 0, w = 0, h = 0;
};

inline bool overlaps(const Box& a, const Box& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

struct Led {
    bool     on   = false;     // defined in the file
    uint16_t line = 0;         // where
    int16_t  x = 0, y = 0;     // the centre
    uint8_t  d = 0;            // lens diameter
    uint8_t  halo = 0;
    Rgb      colour;           // the activity LED's and a lamp's; the others are the lights'
};

// box: what an LED draws and is redrawn by: D + 2 x halo square on its centre.
inline Box ledBox(const Led& l) {
    const int side = l.d + 2 * l.halo;
    Box b;
    b.x = static_cast<int16_t>(l.x - side / 2);
    b.y = static_cast<int16_t>(l.y - side / 2);
    b.w = b.h = static_cast<int16_t>(side);
    return b;
}

struct TextStyle {
    uint8_t face     = FACE_SMALL;
    Rgb     fg       = { 0xC8, 0xC8, 0xC8 };   // the panel's ink
    bool    hasShadow = false;
    Rgb     shadow;
    bool    hasBg    = false;                   // none: drawn over the art
    Rgb     bg;
    uint8_t align    = AL_LEFT;
};

// The live widgets (1.2.0).
struct Field {                 // one line bound to a value
    uint16_t  line = 0;
    Box       box;
    uint8_t   value = LW_NAME;
    TextStyle style;
    char      label[kLabelMax + 1] = "";
};
struct Digits {                // a seven-segment display
    uint16_t line = 0;
    Box      box;
    uint8_t  n = 0;
    uint8_t  value = LW_ONLINE;
    Rgb      fg = { 0xFF, 0x30, 0x20 };
    bool     hasDim = false;   // unlit segments drawn faint, or left to the art
    Rgb      dim;
};
struct List {                  // a node list or an event list
    uint16_t  line = 0;
    Box       box;
    TextStyle style;
    bool      free = false;    // nodes: every line, free ones as waiting
    bool      oldest = false;  // events: the newest at the bottom, like a printout
    bool      hasDim = false;  // nodes: waiting rows in this colour
    Rgb       dim;
    bool      tint = false;    // the mark by rank, an event's kind by kind
};
struct Meter {                 // a meter, or a graph
    uint16_t line = 0;
    Box      box;
    uint8_t  src = SRC_TRAFFIC;
    Rgb      fg = { 0x5D, 0xDC, 0x7A };
    bool     hasBg = false;
    Rgb      bg;
    bool     up = false;       // meters: fills upwards rather than to the right
};
struct Lamp {
    Led     led;
    uint8_t state = ST_RUN;
    bool    blink = false;     // lit, it blinks at 2 Hz: a state that calls somebody
};

struct Manifest {
    uint16_t  w = 0, h = 0;
    char      name[kNameMax + 1] = "";
    Led       drive;
    uint8_t   driveStyle = DS_PC;
    Led       activity;
    uint8_t   strip = 0;
    uint16_t  stripLine = 0;
    Led       led[kStripMax];
    bool      hasText = false;
    uint16_t  textLine = 0;
    Box       text;
    TextStyle textStyle;
    uint8_t   nLines = 0;
    uint16_t  linesLine = 0;
    uint8_t   lines[kLinesMax] = {};
    bool      hasClock = false;
    uint16_t  clockLine = 0;
    Box       clock;
    TextStyle clockStyle;
    // The live widgets (1.2.0).
    uint8_t   nFields = 0, nDigits = 0, nNodes = 0, nEvents = 0, nMeters = 0, nGraphs = 0, nLamps = 0;
    Field     field[kFieldsMax];
    Digits    digits[kDigitsMax];
    List      nodes[kListsMax];
    List      events[kListsMax];
    Meter     meter[kMetersMax];
    Meter     graph[kGraphsMax];
    Lamp      lamp[kLampsMax];
};

// clockBox: "HH:MM" in the clock's face at x, y.
inline Box clockBox(int16_t x, int16_t y, uint8_t face) {
    Box b;
    b.x = x;
    b.y = y;
    b.w = static_cast<int16_t>(5 * kGlyphW[face]);
    b.h = static_cast<int16_t>(kGlyphH[face]);
    return b;
}

// A seven-segment display's geometry: a digit is 11/20 of the height wide,
// the digits a sixth of the height apart (two pixels at the least).
inline int digitW(int h) { return h * 11 / 20; }
inline int digitGap(int h) { return h / 6 < 2 ? 2 : h / 6; }
inline Box digitsBox(int16_t x, int16_t y, int h, int n) {
    Box b;
    b.x = x;
    b.y = y;
    b.w = static_cast<int16_t>(n * digitW(h) + (n - 1) * digitGap(h));
    b.h = static_cast<int16_t>(h);
    return b;
}

namespace detail {

inline char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

inline bool ieq(const char* a, const char* b) {
    while (*a && *b && lower(*a) == lower(*b)) { ++a; ++b; }
    return !*a && !*b;
}

inline bool blank(char c) { return c == ' ' || c == '\t'; }

// number: all digits, at most five of them, into v. No sign, no hex, no
// spaces: "12a" and "+3" are not numbers.
inline bool number(const char* s, long& v) {
    if (!*s) return false;
    long n = 0;
    int  k = 0;
    for (; *s; ++s, ++k) {
        if (*s < '0' || *s > '9' || k >= 5) return false;
        n = n * 10 + (*s - '0');
    }
    v = n;
    return true;
}

inline int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// colour: #RRGGBB exactly.
inline bool colour(const char* s, Rgb& out) {
    if (s[0] != '#' || strlen(s) != 7) return false;
    int v[6];
    for (int i = 0; i < 6; ++i)
        if ((v[i] = hexv(s[1 + i])) < 0) return false;
    out.r = static_cast<uint8_t>(v[0] * 16 + v[1]);
    out.g = static_cast<uint8_t>(v[2] * 16 + v[3]);
    out.b = static_cast<uint8_t>(v[4] * 16 + v[5]);
    return true;
}

// wordIn: w's place in a list of n words, any case; -1 when it is not there.
inline int wordIn(const char* w, const char* const* list, int n) {
    for (int k = 0; k < n; ++k)
        if (ieq(w, list[k])) return k;
    return -1;
}

// The reader's state: where it is and how to say what is wrong.
struct Ctx {
    Manifest& m;
    char*     err;
    size_t    errLen;
    uint16_t  line = 0;
    bool      seenSkin = false, seenPanel = false, seenName = false;
    Ctx(Manifest& mm, char* e, size_t n) : m(mm), err(e), errLen(n) {}

    bool fail(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
};

inline bool Ctx::fail(const char* fmt, ...) {
    if (!err || !errLen) return false;
    int at = line ? snprintf(err, errLen, "line %u: ", static_cast<unsigned>(line)) : 0;
    if (at < 0) at = 0;
    if (static_cast<size_t>(at) < errLen) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err + at, errLen - static_cast<size_t>(at), fmt, ap);
        va_end(ap);
    }
    return false;
}

// num: token t as a number in lo..hi, named what for the message.
inline bool num(Ctx& c, const char* t, const char* what, long lo, long hi, long& v) {
    if (!number(t, v)) return c.fail("%s '%.16s' is not a number", what, t);
    if (v < lo || v > hi) return c.fail("%s %ld is outside %ld to %ld", what, v, lo, hi);
    return true;
}

// The options after a directive's fixed words: key=value, each once.
struct Opts {
    const char* halo = nullptr;
    const char* colour = nullptr;
    const char* size = nullptr;
    const char* shadow = nullptr;
    const char* background = nullptr;
    const char* align = nullptr;
    const char* label = nullptr;
    const char* dim = nullptr;
    const char* free = nullptr;
    const char* order = nullptr;
    const char* dir = nullptr;
    const char* tint = nullptr;
    const char* blink = nullptr;
};

// opts: tokens from `from` on are options, and only those named in `allowed`
// (a space separated list) may appear.
inline bool opts(Ctx& c, const char* dir, char** tok, int n, int from, const char* allowed, Opts& o) {
    for (int i = from; i < n; ++i) {
        char* eq = strchr(tok[i], '=');
        if (!eq || eq == tok[i])
            return c.fail("%s: '%.16s' is not an option (name=value)", dir, tok[i]);
        *eq = '\0';
        const char* key = tok[i];
        const char* val = eq + 1;
        if (!*val) return c.fail("%s: %.16s= has no value", dir, key);
        // allowed?
        bool ok = false;
        const char* p = allowed;
        while (*p && !ok) {
            const char* e = p;
            while (*e && *e != ' ') ++e;
            size_t len = static_cast<size_t>(e - p), kl = strlen(key);
            if (len == kl) {
                ok = true;
                for (size_t k = 0; k < len; ++k)
                    if (lower(p[k]) != lower(key[k])) { ok = false; break; }
            }
            p = *e ? e + 1 : e;
        }
        if (!ok) return c.fail("%s takes no %.16s= option", dir, key);
        const char** slot = ieq(key, "halo") ? &o.halo : ieq(key, "colour") || ieq(key, "color") ? &o.colour
                          : ieq(key, "size") ? &o.size : ieq(key, "shadow") ? &o.shadow
                          : ieq(key, "background") ? &o.background : ieq(key, "label") ? &o.label
                          : ieq(key, "dim") ? &o.dim : ieq(key, "free") ? &o.free
                          : ieq(key, "order") ? &o.order : ieq(key, "dir") ? &o.dir
                          : ieq(key, "tint") ? &o.tint : ieq(key, "blink") ? &o.blink : &o.align;
        if (*slot) return c.fail("%s: %.16s= is given twice", dir, key);
        *slot = val;
    }
    return true;
}

// led: X Y D at tok[at..at+2], and a halo option.
inline bool led(Ctx& c, const char* dir, char** tok, int at, const Opts& o, Led& l) {
    long x = 0, y = 0, d = 0, halo = 0;
    if (!num(c, tok[at], "x", 0, kPanelMax, x) || !num(c, tok[at + 1], "y", 0, kPanelMax, y) ||
        !num(c, tok[at + 2], "diameter", kLedMin, kLedMax, d))
        return false;
    halo = d / 2 > kHaloMax ? kHaloMax : d / 2;
    if (o.halo && !num(c, o.halo, "halo", 0, kHaloMax, halo)) return false;
    l.on = true;
    l.line = c.line;
    l.x = static_cast<int16_t>(x);
    l.y = static_cast<int16_t>(y);
    l.d = static_cast<uint8_t>(d);
    l.halo = static_cast<uint8_t>(halo);
    (void)dir;
    return true;
}

// box: X Y W H at tok[at..at+3].
inline bool box(Ctx& c, char** tok, int at, Box& b) {
    long x = 0, y = 0, w = 0, h = 0;
    if (!num(c, tok[at], "x", 0, kPanelMax, x) || !num(c, tok[at + 1], "y", 0, kPanelMax, y) ||
        !num(c, tok[at + 2], "width", 1, kPanelMax, w) || !num(c, tok[at + 3], "height", 1, kPanelMax, h))
        return false;
    b = { static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h) };
    return true;
}

// style: the text options shared by text, clock, fields and the lists.
inline bool style(Ctx& c, const char* dir, const Opts& o, TextStyle& s) {
    if (o.size) {
        if (ieq(o.size, "small")) s.face = FACE_SMALL;
        else if (ieq(o.size, "big")) s.face = FACE_BIG;
        else if (ieq(o.size, "tiny")) s.face = FACE_TINY;
        else return c.fail("%s: size=%.16s is not tiny, small or big", dir, o.size);
    }
    if (o.colour && !colour(o.colour, s.fg))
        return c.fail("%s: colour=%.16s is not #RRGGBB", dir, o.colour);
    if (o.shadow) {
        if (ieq(o.shadow, "none")) s.hasShadow = false;
        else if (colour(o.shadow, s.shadow)) s.hasShadow = true;
        else return c.fail("%s: shadow=%.16s is not #RRGGBB or none", dir, o.shadow);
    }
    if (o.background) {
        if (ieq(o.background, "none")) s.hasBg = false;
        else if (colour(o.background, s.bg)) s.hasBg = true;
        else return c.fail("%s: background=%.16s is not #RRGGBB or none", dir, o.background);
    }
    if (o.align) {
        if (ieq(o.align, "left")) s.align = AL_LEFT;
        else if (ieq(o.align, "centre") || ieq(o.align, "center")) s.align = AL_CENTRE;
        else if (ieq(o.align, "right")) s.align = AL_RIGHT;
        else return c.fail("%s: align=%.16s is not left, centre or right", dir, o.align);
    }
    return true;
}

// fill: a meter's or a graph's colour and background.
inline bool fill(Ctx& c, const char* dir, const Opts& o, Meter& g) {
    if (o.colour && !colour(o.colour, g.fg)) return c.fail("%s: colour=%.16s is not #RRGGBB", dir, o.colour);
    if (o.background) {
        if (ieq(o.background, "none")) g.hasBg = false;
        else if (colour(o.background, g.bg)) g.hasBg = true;
        else return c.fail("%s: background=%.16s is not #RRGGBB or none", dir, o.background);
    }
    return true;
}

inline bool yesNo(Ctx& c, const char* dir, const char* key, const char* v, bool& out) {
    if (ieq(v, "yes")) out = true;
    else if (ieq(v, "no")) out = false;
    else return c.fail("%s: %s=%.16s is not yes or no", dir, key, v);
    return true;
}

inline bool once(Ctx& c, const char* dir, bool seen, uint16_t first) {
    if (seen) return c.fail("a second %s line (the first is line %u)", dir, static_cast<unsigned>(first));
    return true;
}

inline bool room(Ctx& c, const char* dir, uint8_t have, uint8_t most) {
    if (have >= most) return c.fail("more than %u %s lines", static_cast<unsigned>(most), dir);
    return true;
}

// directive: one line's words.
inline bool directive(Ctx& c, char** tok, int n, const char* rest) {
    Manifest& m = c.m;
    const char* dir = tok[0];
    auto want = [&](int lo, int hi) {
        if (n - 1 < lo) return c.fail("%s needs %d value%s", dir, lo, lo == 1 ? "" : "s");
        if (hi >= 0 && n - 1 > hi) return c.fail("%s has too many values", dir);
        return true;
    };

    if (!c.seenSkin) {
        if (!ieq(dir, "skin")) return c.fail("the first line must be 'skin %u'", static_cast<unsigned>(kFormat));
        if (!want(1, 1)) return false;
        long v = 0;
        if (!number(tok[1], v)) return c.fail("skin '%.16s' is not a number", tok[1]);
        if (v != kFormat)
            return c.fail("skin format %ld; this board reads format %u", v, static_cast<unsigned>(kFormat));
        c.seenSkin = true;
        return true;
    }
    if (ieq(dir, "skin")) return c.fail("a second skin line");

    if (ieq(dir, "panel")) {
        if (c.seenPanel) return c.fail("a second panel line");
        if (!want(2, 2)) return false;
        long w = 0, h = 0;
        if (!num(c, tok[1], "width", 1, kPanelMax, w) || !num(c, tok[2], "height", 1, kPanelMax, h)) return false;
        m.w = static_cast<uint16_t>(w);
        m.h = static_cast<uint16_t>(h);
        c.seenPanel = true;
        return true;
    }
    if (ieq(dir, "name")) {
        if (c.seenName) return c.fail("a second name line");
        size_t len = strlen(rest);
        if (!len) return c.fail("name is empty");
        if (len > kNameMax) return c.fail("name is %u characters; %u at most", static_cast<unsigned>(len),
                                          static_cast<unsigned>(kNameMax));
        for (size_t i = 0; i < len; ++i)
            if (rest[i] < 0x20 || rest[i] > 0x7E) return c.fail("name has a character that is not plain ASCII");
        memcpy(m.name, rest, len);
        m.name[len] = '\0';
        c.seenName = true;
        return true;
    }
    if (ieq(dir, "drive")) {
        if (!once(c, dir, m.drive.on, m.drive.line)) return false;
        if (n < 5) return c.fail("drive needs X Y D STYLE");
        Opts o;
        if (!opts(c, dir, tok, n, 5, "halo", o)) return false;
        const int s = wordIn(tok[4], kDriveWords, DS_COUNT);
        if (s < 0) return c.fail("drive style '%.16s' is not pc, 1541, disk2 or breathe", tok[4]);
        if (!led(c, dir, tok, 1, o, m.drive)) return false;
        m.driveStyle = static_cast<uint8_t>(s);
        return true;
    }
    if (ieq(dir, "activity")) {
        if (!once(c, dir, m.activity.on, m.activity.line)) return false;
        if (n < 4) return c.fail("activity needs X Y D");
        Opts o;
        if (!opts(c, dir, tok, n, 4, "halo colour color", o)) return false;
        Rgb col = { 0x5D, 0xDC, 0x7A };               // the panel's live green
        if (o.colour && !colour(o.colour, col))
            return c.fail("activity: colour=%.16s is not #RRGGBB", o.colour);
        if (!led(c, dir, tok, 1, o, m.activity)) return false;
        m.activity.colour = col;
        return true;
    }
    if (ieq(dir, "strip")) {
        if (m.strip) return c.fail("a second strip line (the first is line %u)", static_cast<unsigned>(m.stripLine));
        if (!want(1, 1)) return false;
        long v = 0;
        if (!num(c, tok[1], "strip", 1, kStripMax, v)) return false;
        m.strip = static_cast<uint8_t>(v);
        m.stripLine = c.line;
        return true;
    }
    if (ieq(dir, "led")) {
        if (!m.strip) return c.fail("led before the strip line");
        if (n < 5) return c.fail("led needs I X Y D");
        long i = 0;
        if (!num(c, tok[1], "led", 1, m.strip, i)) return false;
        Led& l = m.led[i - 1];
        if (l.on) return c.fail("led %ld again (the first is line %u)", i, static_cast<unsigned>(l.line));
        Opts o;
        if (!opts(c, dir, tok, n, 5, "halo", o)) return false;
        return led(c, dir, tok, 2, o, l);
    }
    if (ieq(dir, "text")) {
        if (!once(c, dir, m.hasText, m.textLine)) return false;
        if (n < 5) return c.fail("text needs X Y W H");
        Opts o;
        if (!opts(c, dir, tok, n, 5, "size colour color shadow background align", o)) return false;
        if (!box(c, tok, 1, m.text)) return false;
        if (!style(c, dir, o, m.textStyle)) return false;
        m.hasText = true;
        m.textLine = c.line;
        return true;
    }
    if (ieq(dir, "lines")) {
        if (m.nLines) return c.fail("a second lines line (the first is line %u)", static_cast<unsigned>(m.linesLine));
        if (n < 2) return c.fail("lines needs at least one word");
        if (n - 1 > kLinesMax) return c.fail("lines has %d words; %u at most", n - 1, static_cast<unsigned>(kLinesMax));
        for (int i = 1; i < n; ++i) {
            const int w = wordIn(tok[i], kLineWords, LW_COUNT);
            if (w < 0) return c.fail("lines: '%.16s' is not a word the panel knows", tok[i]);
            if (w == LW_WHO && i != n - 1) return c.fail("lines: who takes the lines left, so it comes last");
            m.lines[i - 1] = static_cast<uint8_t>(w);
        }
        m.nLines = static_cast<uint8_t>(n - 1);
        m.linesLine = c.line;
        return true;
    }
    if (ieq(dir, "clock")) {
        if (!once(c, dir, m.hasClock, m.clockLine)) return false;
        if (n < 3) return c.fail("clock needs X Y");
        Opts o;
        if (!opts(c, dir, tok, n, 3, "size colour color shadow background", o)) return false;
        long x = 0, y = 0;
        if (!num(c, tok[1], "x", 0, kPanelMax, x) || !num(c, tok[2], "y", 0, kPanelMax, y)) return false;
        m.clockStyle.fg = { 0xFF, 0xD3, 0x5C };        // the panel's clock yellow
        if (!style(c, dir, o, m.clockStyle)) return false;
        m.clock = clockBox(static_cast<int16_t>(x), static_cast<int16_t>(y), m.clockStyle.face);
        m.hasClock = true;
        m.clockLine = c.line;
        return true;
    }

    // ---- the live widgets (1.2.0) ----------------------------------------
    if (ieq(dir, "field")) {
        if (!room(c, dir, m.nFields, kFieldsMax)) return false;
        if (n < 5) return c.fail("field needs X Y W VALUE");
        Opts o;
        if (!opts(c, dir, tok, n, 5, "label size colour color shadow background align", o)) return false;
        Field& f = m.field[m.nFields];
        long x = 0, y = 0, w = 0;
        if (!num(c, tok[1], "x", 0, kPanelMax, x) || !num(c, tok[2], "y", 0, kPanelMax, y) ||
            !num(c, tok[3], "width", 1, kPanelMax, w))
            return false;
        const int v = wordIn(tok[4], kLineWords, LW_COUNT);
        if (v < 0 || v == LW_WHO || v == LW_BLANK)
            return c.fail("field: '%.16s' is not a value a field shows", tok[4]);
        if (!style(c, dir, o, f.style)) return false;
        if (o.label) {
            const size_t len = strlen(o.label);
            if (len > kLabelMax)
                return c.fail("field: label is %u characters; %u at most", static_cast<unsigned>(len),
                              static_cast<unsigned>(kLabelMax));
            for (size_t i = 0; i < len; ++i) f.label[i] = o.label[i] == '_' ? ' ' : o.label[i];
            f.label[len] = '\0';
        }
        f.value = static_cast<uint8_t>(v);
        f.box = { static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w),
                  static_cast<int16_t>(kGlyphH[f.style.face]) };
        if (f.box.w < kGlyphW[f.style.face])
            return c.fail("field is %d px wide; one character is %d", f.box.w, kGlyphW[f.style.face]);
        f.line = c.line;
        ++m.nFields;
        return true;
    }
    if (ieq(dir, "digits")) {
        if (!room(c, dir, m.nDigits, kDigitsMax)) return false;
        if (n < 6) return c.fail("digits needs X Y H N VALUE");
        Opts o;
        if (!opts(c, dir, tok, n, 6, "colour color dim", o)) return false;
        Digits& d = m.digits[m.nDigits];
        long x = 0, y = 0, h = 0, k = 0;
        if (!num(c, tok[1], "x", 0, kPanelMax, x) || !num(c, tok[2], "y", 0, kPanelMax, y) ||
            !num(c, tok[3], "height", kDigitsHMin, kDigitsHMax, h) || !num(c, tok[4], "digits", 1, kDigitsNMax, k))
            return false;
        const int v = wordIn(tok[5], kLineWords, LW_COUNT);
        if (v < 0 || !numericWord(static_cast<uint8_t>(v)))
            return c.fail("digits: '%.16s' is not a number the panel shows", tok[5]);
        if (v == LW_CLOCK && k != 4) return c.fail("digits: the clock takes 4 digits");
        if (o.colour && !colour(o.colour, d.fg)) return c.fail("digits: colour=%.16s is not #RRGGBB", o.colour);
        if (o.dim) {
            if (ieq(o.dim, "none")) d.hasDim = false;
            else if (colour(o.dim, d.dim)) d.hasDim = true;
            else return c.fail("digits: dim=%.16s is not #RRGGBB or none", o.dim);
        }
        d.value = static_cast<uint8_t>(v);
        d.n = static_cast<uint8_t>(k);
        d.box = digitsBox(static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int>(h), static_cast<int>(k));
        d.line = c.line;
        ++m.nDigits;
        return true;
    }
    if (ieq(dir, "nodes") || ieq(dir, "events")) {
        const bool nodes = ieq(dir, "nodes");
        uint8_t& count = nodes ? m.nNodes : m.nEvents;
        if (!room(c, dir, count, kListsMax)) return false;
        if (n < 5) return c.fail("%s needs X Y W H", dir);
        Opts o;
        if (!opts(c, dir, tok, n, 5,
                  nodes ? "size colour color shadow background free dim tint"
                        : "size colour color shadow background order tint", o))
            return false;
        List& l = nodes ? m.nodes[count] : m.events[count];
        if (!box(c, tok, 1, l.box)) return false;
        if (!style(c, dir, o, l.style)) return false;
        if (o.free && !yesNo(c, dir, "free", o.free, l.free)) return false;
        if (o.tint && !yesNo(c, dir, "tint", o.tint, l.tint)) return false;
        if (o.dim) {
            if (ieq(o.dim, "none")) l.hasDim = false;
            else if (colour(o.dim, l.dim)) l.hasDim = true;
            else return c.fail("nodes: dim=%.16s is not #RRGGBB or none", o.dim);
        }
        if (o.order) {
            if (ieq(o.order, "newest")) l.oldest = false;
            else if (ieq(o.order, "oldest")) l.oldest = true;
            else return c.fail("events: order=%.16s is not newest or oldest", o.order);
        }
        const int gw = kGlyphW[l.style.face], gh = kGlyphH[l.style.face];
        if (l.box.w < 8 * gw) return c.fail("%s is %d px wide; 8 characters are %d", dir, l.box.w, 8 * gw);
        if (l.box.h < gh) return c.fail("%s is %d px tall; one row is %d", dir, l.box.h, gh);
        l.line = c.line;
        ++count;
        return true;
    }
    if (ieq(dir, "meter") || ieq(dir, "graph")) {
        const bool graph = ieq(dir, "graph");
        uint8_t& count = graph ? m.nGraphs : m.nMeters;
        if (!room(c, dir, count, graph ? kGraphsMax : kMetersMax)) return false;
        if (n < 6) return c.fail("%s needs X Y W H SOURCE", dir);
        Opts o;
        if (!opts(c, dir, tok, n, 6, graph ? "colour color background" : "colour color background dir", o))
            return false;
        Meter& g = graph ? m.graph[count] : m.meter[count];
        if (!box(c, tok, 1, g.box)) return false;
        const int s = wordIn(tok[5], kSourceWords, SRC_COUNT);
        if (s < 0 || (graph && !graphSource(static_cast<uint8_t>(s))))
            return c.fail(graph ? "graph: '%.16s' is not traffic or callers"
                                : "meter: '%.16s' is not traffic, heap, card, callers or rssi", tok[5]);
        if (!fill(c, dir, o, g)) return false;
        if (o.dir) {
            if (ieq(o.dir, "right")) g.up = false;
            else if (ieq(o.dir, "up")) g.up = true;
            else return c.fail("meter: dir=%.16s is not right or up", o.dir);
        }
        if (g.box.w < 2 || g.box.h < 2) return c.fail("%s is smaller than 2 x 2", dir);
        g.src = static_cast<uint8_t>(s);
        g.line = c.line;
        ++count;
        return true;
    }
    if (ieq(dir, "lamp")) {
        if (!room(c, dir, m.nLamps, kLampsMax)) return false;
        if (n < 5) return c.fail("lamp needs X Y D STATE");
        Opts o;
        if (!opts(c, dir, tok, n, 5, "halo colour color blink", o)) return false;
        Lamp& l = m.lamp[m.nLamps];
        if (o.blink && !yesNo(c, dir, "blink", o.blink, l.blink)) return false;
        int s = wordIn(tok[4], kStateWords, ST_NODE);
        if (s < 0 && (tok[4][0] | 0x20) == 'n' && (tok[4][1] | 0x20) == 'o' && (tok[4][2] | 0x20) == 'd' &&
            (tok[4][3] | 0x20) == 'e' && tok[4][4]) {
            long k = 0;
            if (number(tok[4] + 4, k) && k >= 1 && k <= kNodeLines) s = ST_NODE + static_cast<int>(k) - 1;
        }
        if (s < 0) return c.fail("lamp: '%.16s' is not a state a lamp shows", tok[4]);
        l.led.colour = { 0xFF, 0x2A, 0x10 };           // a panel lamp's red
        if (o.colour && !colour(o.colour, l.led.colour))
            return c.fail("lamp: colour=%.16s is not #RRGGBB", o.colour);
        if (!led(c, dir, tok, 1, o, l.led)) return false;
        l.state = static_cast<uint8_t>(s);
        ++m.nLamps;
        return true;
    }
    return c.fail("'%.16s' is not a skin directive", dir);
}

// inside: a box wholly on the panel.
inline bool inside(const Box& b, uint16_t w, uint16_t h) {
    return b.x >= 0 && b.y >= 0 && b.x + b.w <= static_cast<int>(w) && b.y + b.h <= static_cast<int>(h);
}

// What an item is, for a message: "led 3", "field 2", "text".
enum ItemKind : uint8_t { IK_DRIVE, IK_ACTIVITY, IK_LED, IK_TEXT, IK_CLOCK, IK_FIELD, IK_DIGITS, IK_NODES,
                          IK_EVENTS, IK_METER, IK_GRAPH, IK_LAMP };
inline void itemName(uint8_t kind, uint8_t index, char* out, size_t n) {
    static const char* const kNames[] = { "drive", "activity", "led", "text", "clock", "field", "digits",
                                          "nodes", "events", "meter", "graph", "lamp" };
    if (kind == IK_DRIVE || kind == IK_ACTIVITY || kind == IK_TEXT || kind == IK_CLOCK)
        snprintf(out, n, "%s", kNames[kind]);
    else
        snprintf(out, n, "%s %u", kNames[kind], static_cast<unsigned>(index + 1));
}

// geometry: every rule that needs the whole file.
inline bool geometry(Ctx& c) {
    Manifest& m = c.m;
    c.line = 0;
    if (!c.seenSkin) return c.fail("empty: the first line must be 'skin %u'", static_cast<unsigned>(kFormat));
    if (!c.seenPanel) return c.fail("no panel line");

    // Every drawn thing, for bounds and overlaps: twelve bytes an item, so
    // the whole file's worth is about 1.2 KB of the loading task's stack.
    struct Item { Box b; uint16_t line; uint8_t kind, index; };
    Item items[2 + kStripMax + kLampsMax + 2 + kFieldsMax + kDigitsMax + 2 * kListsMax + kMetersMax + kGraphsMax];
    int  n = 0;
    uint32_t ledPixels = 0;
    auto add = [&](const Box& b, uint8_t kind, uint8_t index, uint16_t line) {
        items[n].b = b;
        items[n].kind = kind;
        items[n].index = index;
        items[n].line = line;
        ++n;
    };
    if (m.drive.on)    { add(ledBox(m.drive), IK_DRIVE, 0, m.drive.line); }
    if (m.activity.on) { add(ledBox(m.activity), IK_ACTIVITY, 0, m.activity.line); }
    for (uint8_t i = 0; i < m.strip; ++i) {
        if (!m.led[i].on) {
            c.line = m.stripLine;
            return c.fail("strip %u but led %u is missing", static_cast<unsigned>(m.strip),
                          static_cast<unsigned>(i + 1));
        }
        add(ledBox(m.led[i]), IK_LED, i, m.led[i].line);
    }
    for (uint8_t i = 0; i < m.nLamps; ++i) add(ledBox(m.lamp[i].led), IK_LAMP, i, m.lamp[i].led.line);
    for (int i = 0; i < n; ++i) ledPixels += static_cast<uint32_t>(items[i].b.w) * items[i].b.h;
    if (m.hasText)  add(m.text, IK_TEXT, 0, m.textLine);
    if (m.hasClock) add(m.clock, IK_CLOCK, 0, m.clockLine);
    for (uint8_t i = 0; i < m.nFields; ++i) add(m.field[i].box, IK_FIELD, i, m.field[i].line);
    for (uint8_t i = 0; i < m.nDigits; ++i) add(m.digits[i].box, IK_DIGITS, i, m.digits[i].line);
    for (uint8_t i = 0; i < m.nNodes; ++i)  add(m.nodes[i].box, IK_NODES, i, m.nodes[i].line);
    for (uint8_t i = 0; i < m.nEvents; ++i) add(m.events[i].box, IK_EVENTS, i, m.events[i].line);
    for (uint8_t i = 0; i < m.nMeters; ++i) add(m.meter[i].box, IK_METER, i, m.meter[i].line);
    for (uint8_t i = 0; i < m.nGraphs; ++i) add(m.graph[i].box, IK_GRAPH, i, m.graph[i].line);

    char a[16], b[16];
    for (int i = 0; i < n; ++i) {
        const Box& x = items[i].b;
        if (!inside(x, m.w, m.h)) {
            c.line = items[i].line;
            itemName(items[i].kind, items[i].index, a, sizeof(a));
            return c.fail("%s's box %d,%d %dx%d runs off the %ux%u panel", a, x.x, x.y, x.w, x.h,
                          static_cast<unsigned>(m.w), static_cast<unsigned>(m.h));
        }
    }
    for (int i = 0; i < n; ++i)
        for (int k = i + 1; k < n; ++k)
            if (overlaps(items[i].b, items[k].b)) {
                c.line = items[k].line;
                itemName(items[k].kind, items[k].index, a, sizeof(a));
                itemName(items[i].kind, items[i].index, b, sizeof(b));
                return c.fail("%s's box overlaps %s's (line %u)", a, b, static_cast<unsigned>(items[i].line));
            }
    if (ledPixels > kLedPixelsMax) {
        c.line = 0;
        return c.fail("the LEDs' boxes cover %lu pixels between them; %lu at most",
                      static_cast<unsigned long>(ledPixels), static_cast<unsigned long>(kLedPixelsMax));
    }
    // What one unit of each widget draws at once: a row of a list or of the
    // text rectangle is one glyph tall; everything else is its whole box.
    for (int i = 0; i < n; ++i) {
        const uint8_t kd = items[i].kind;
        if (kd == IK_DRIVE || kd == IK_ACTIVITY || kd == IK_LED || kd == IK_LAMP) continue;
        uint32_t px = static_cast<uint32_t>(items[i].b.w) * items[i].b.h;
        const uint8_t face = kd == IK_TEXT  ? m.textStyle.face
                           : kd == IK_NODES ? m.nodes[items[i].index].style.face
                           : kd == IK_EVENTS ? m.events[items[i].index].style.face : static_cast<uint8_t>(FACE_COUNT);
        if (face < FACE_COUNT) px = static_cast<uint32_t>(items[i].b.w) * kGlyphH[face];
        if (px > kUnitPxMax) {
            c.line = items[i].line;
            itemName(kd, items[i].index, a, sizeof(a));
            return c.fail("%s draws %lu pixels at once; %lu at most", a, static_cast<unsigned long>(px),
                          static_cast<unsigned long>(kUnitPxMax));
        }
    }

    if (m.nLines && !m.hasText) { c.line = m.linesLine; return c.fail("lines without a text line"); }
    if (m.hasText) {
        c.line = m.textLine;
        if (!m.nLines) return c.fail("text without a lines line");
        const int gw = kGlyphW[m.textStyle.face], gh = kGlyphH[m.textStyle.face];
        if (m.text.w < gw) return c.fail("text is %d px wide; one character is %d", m.text.w, gw);
        const int need = m.nLines * gh;
        if (need > m.text.h)
            return c.fail("%u lines of %d px need %d px; the rectangle is %d", static_cast<unsigned>(m.nLines), gh,
                          need, m.text.h);
    }
    return true;
}

} // namespace detail

// ---------------------------------------------------------------------------
// parse: skin.txt's bytes into m. False with "line N: what is wrong" (or the
// file-wide reason with no line) in err. m is written as the file is read and
// means nothing on a false.
// ---------------------------------------------------------------------------
inline bool parse(const char* buf, size_t len, Manifest& m, char* err, size_t errLen) {
    new (&m) Manifest();                   // in place: a Manifest is some 3 KB, too much for a stack copy
    if (err && errLen) err[0] = '\0';
    detail::Ctx c(m, err, errLen);
    if (len > kFileMax)
        return c.fail("skin.txt is %lu bytes; %lu at most", static_cast<unsigned long>(len),
                      static_cast<unsigned long>(kFileMax));
    size_t at = 0;
    while (at < len) {
        ++c.line;
        size_t end = at;
        while (end < len && buf[end] != '\n') ++end;
        size_t n = end - at;
        if (n && buf[at + n - 1] == '\r') --n;
        if (n > kLineMax) return c.fail("%lu characters; %lu at most", static_cast<unsigned long>(n),
                                        static_cast<unsigned long>(kLineMax));
        char line[kLineMax + 1];
        for (size_t i = 0; i < n; ++i) {
            const char ch = buf[at + i];
            if (ch == '\0' || ch == '\r' || (static_cast<unsigned char>(ch) < 0x20 && ch != '\t') ||
                static_cast<unsigned char>(ch) > 0x7E)
                return c.fail("a character that is not plain ASCII (byte %u)", static_cast<unsigned>(
                                                                                 static_cast<unsigned char>(ch)));
            line[i] = ch;
        }
        line[n] = '\0';
        at = end < len ? end + 1 : end;

        // Comments: a '#' first, or ';' onwards.
        char* p = line;
        while (detail::blank(*p)) ++p;
        if (*p == '#' || !*p) continue;
        if (char* semi = strchr(p, ';')) *semi = '\0';

        // The words, and for name the text after the first word.
        char  rest[kLineMax + 1];
        char* tok[kTokensMax];
        int   ntok = 0;
        {
            char* q = p;
            while (*q && !detail::blank(*q)) ++q;
            const char* r = q;
            while (detail::blank(*r)) ++r;
            snprintf(rest, sizeof(rest), "%s", r);
            size_t rl = strlen(rest);
            while (rl && detail::blank(rest[rl - 1])) rest[--rl] = '\0';
        }
        for (char* q = p; *q;) {
            while (detail::blank(*q)) *q++ = '\0';
            if (!*q) break;
            if (ntok == kTokensMax) return c.fail("more than %u words", static_cast<unsigned>(kTokensMax));
            tok[ntok++] = q;
            while (*q && !detail::blank(*q)) ++q;
        }
        if (!ntok) continue;
        if (!detail::directive(c, tok, ntok, rest)) return false;
    }
    return detail::geometry(c);
}

} // namespace skin
