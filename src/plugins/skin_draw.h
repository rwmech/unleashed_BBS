/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/skin_draw.h
 * Module:       Plugins / panel skins (BBS_HAS_LCD boards, and the host tests)
 *
 * Purpose:      What a loaded skin does to pixels: the LEDs lit over the
 *               art, the status lines in their rectangle, the clock, and
 *               which rectangles to send. Pure: a framebuffer, the decoded
 *               background beside it and a manifest, so host/test_skin.cpp
 *               can draw a whole skin with no panel and read it back.
 *
 * Design:       The background is kept whole (the loader's copy, RGB565 as
 *               the framebuffer is), so anything drawn over it is undone by
 *               copying its box back: an LED going dark, a line of text
 *               getting shorter. Nothing is ever drawn over something else
 *               drawn: the manifest refuses overlapping boxes.
 *
 *               An LED is light, not paint. Each pixel of its box has a
 *               weight, worked out once at load (ledWeights): how much of
 *               the lens covers it (a 4 x 4 supersample, so its edge is
 *               smooth), brighter to the middle, and past the lens a halo
 *               falling away as the square of the distance. The colour at
 *               that weight is screen-blended over the art, 255 - (255 -
 *               art)(255 - light) / 255, which only ever brightens: a dark
 *               colour is a faint glow and black leaves the art as painted,
 *               so an LED that is off is the art's own unlit lens. A bright
 *               colour also runs towards white in the middle of the lens,
 *               the hot spot a real LED has.
 *
 *               The live widgets (1.2.0), fields, seven-segment displays,
 *               node and event lists, meters, graphs and lamps, are units
 *               of their own: each keeps a hash of the words or the figure
 *               it last drew, and a pass redraws only those whose hash
 *               moved, up to a pixel budget, carrying the rest to the next.
 *               The figures are the panel's, from RAM; the traffic and the
 *               keystrokes are counters skin.cpp samples.
 *
 *               Colours come in as the lights plugin sends a real pixel,
 *               shaded to its brightness setting, and go through the
 *               panel's glassLevel, so a skin's LEDs are the strip a wired
 *               board shows, as the status skin draws it.
 *
 * Libraries:    none
 * Targets:      ESP32-S3 boards with BBS_HAS_LCD, and the Linux host tests
 * See also:     src/plugins/skin_manifest.h, src/plugins/skin.cpp,
 *               src/plugins/panel_gfx.h
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
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include "panel_gfx.h"
#include "skin_manifest.h"

namespace skin {

static_assert(kGlyphW[FACE_SMALL] == panelgfx::kSmallW && kGlyphH[FACE_SMALL] == panelgfx::kSmallH &&
              kGlyphW[FACE_BIG] == panelgfx::kBigW && kGlyphH[FACE_BIG] == panelgfx::kBigH &&
              sizeof(panelfont::kTiny[0]) == static_cast<size_t>(kGlyphH[FACE_TINY]),
              "skin_manifest.h's glyph sizes are the panel's faces");

// ---------------------------------------------------------------------------
// The figures the status lines and the widgets show, filled by the panel
// twice a second from what it already keeps (panel.cpp, skinFigures): the
// session pool, the core's counters and the other plugins' own counts in
// RAM. Nothing here is read from the card. Text as it is to appear; empty
// is a blank line.
// ---------------------------------------------------------------------------
constexpr uint8_t kWhoMax    = 12;
constexpr uint8_t kRowsMax   = kNodeLines + 1;   // the sysop's line and sixteen
constexpr uint8_t kEventsMax = 10;               // the panel's own ring

struct NodeRow {
    uint8_t line = 0;          // 0 the sysop's
    bool    on   = false;      // somebody the panel may name is on it
    char    mark = ')';        // the rank mark, as WHO shows it
    char    handle[17] = "";
    char    doing[9]   = "";   // CHAT, FILES, MAIL...
    char    onFor[4]   = "";   // 4m, 2h, 1d
};

struct Figures {
    char    name[40]    = "";      // the board's name
    char    address[24] = "";      // 192.168.0.40:6400, "no network"
    char    uptime[24]  = "";      // "up 3h 14m"
    char    callers[24] = "";      // "Callers 4/11"
    char    today[24]   = "";      // "12 calls today"
    char    heap[24]    = "";      // "84K free"
    char    card[24]    = "";      // "7.4 GB free", "no card"
    char    clock[8]    = "";      // "14:05", "--:--"
    char    date[16]    = "";      // "Sat 26 Sep"
    char    last[40]    = "";      // "14:02 login bob"
    char    ring[40]    = "";      // "bob is ringing", "" when nobody is
    uint8_t who         = 0;       // callers on, named below
    char    whoLine[kWhoMax][32] = {};   // " 3) bob 4m"
    // The fields' own words (1.2.0).
    char    online[8]     = "";    // "2"
    char    lines[8]      = "";    // "11"
    char    lastcaller[17] = "";   // "alice", "" before anybody
    char    rssi[12]      = "";    // "-58 dBm", "no Wi-Fi"
    char    peak[8]       = "";    // "4"
    char    version[24]   = "";    // "1.2.0 (MF35 1.1.0)"
    // The numbers the meters and the seven-segment displays read.
    int16_t nOnline = 0, nLines = 0, nToday = 0, nPeak = 0, nRssi = 0, nHeapK = 0;
    int16_t nClock = -1;           // HHMM, -1 with no clock
    uint16_t heapTopK = 0;         // the most heap seen free, K: a meter's full scale
    uint32_t cardFreeMB = 0, cardTotalMB = 0;
    // The states the lamps show (the slow ones: twice a second is enough).
    bool    fRing = false, fMail = false, fStaff = false, fListed = false, fClosed = false, fCard = false;
    // Who is on each line, the sysop's first, then 1 to the board's lines.
    uint8_t rows = 0;
    NodeRow row[kRowsMax];
    // The recent events, newest first: "21:40 login alice".
    uint8_t events = 0;
    char    event[kEventsMax][40] = {};
};

// lineText: what word shows, the k-th row of who for LW_WHO.
inline const char* lineText(const Figures& f, uint8_t word, uint8_t k) {
    switch (word) {
        case LW_NAME:       return f.name;
        case LW_ADDRESS:    return f.address;
        case LW_UPTIME:     return f.uptime;
        case LW_CALLERS:    return f.callers;
        case LW_TODAY:      return f.today;
        case LW_HEAP:       return f.heap;
        case LW_CARD:       return f.card;
        case LW_CLOCK:      return f.clock;
        case LW_DATE:       return f.date;
        case LW_LAST:       return f.last;
        case LW_RING:       return f.ring;
        case LW_WHO:        return k < f.who && k < kWhoMax ? f.whoLine[k] : "";
        case LW_ONLINE:     return f.online;
        case LW_LINES:      return f.lines;
        case LW_LASTCALLER: return f.lastcaller;
        case LW_RSSI:       return f.rssi;
        case LW_PEAK:       return f.peak;
        case LW_VERSION:    return f.version;
        default:            return "";
    }
}

// numberOf: a numeric word's value for a seven-segment display; false when
// there is nothing to show (no clock yet, no Wi-Fi).
inline bool numberOf(const Figures& f, uint8_t word, int& v) {
    switch (word) {
        case LW_ONLINE: v = f.nOnline; return true;
        case LW_LINES:  v = f.nLines;  return true;
        case LW_TODAY:  v = f.nToday;  return true;
        case LW_PEAK:   v = f.nPeak;   return true;
        case LW_HEAP:   v = f.nHeapK;  return true;
        case LW_RSSI:   v = f.nRssi < 0 ? -f.nRssi : f.nRssi; return f.nRssi != 0;
        case LW_CLOCK:  v = f.nClock;  return f.nClock >= 0;
        default:        return false;
    }
}

// Live: what the skin samples itself, from counters, twice a second (the
// meters and the graphs), and every LED frame (the lamps). skin.cpp.
constexpr uint16_t kHistory = 240;               // two minutes at two samples a second
struct Live {
    uint32_t        rate = 0;                    // traffic, bytes a second, the last sample
    uint32_t        samples = 0;                 // taken so far: a graph's key
    const uint16_t* traffic = nullptr;           // kHistory, bytes a second (to 65,535)
    const uint8_t*  callers = nullptr;           // kHistory, callers on
    uint16_t        head = 0;                    // where the newest sample is
};

// The lights at one frame, as a real pixel would get them.
struct LedState {
    uint8_t drive[3]  = {};        // RGB as the lights plugin shades it
    uint8_t drivePct  = 0;         // the drive light's brightness, percent
    bool    activity  = false;     // traffic within the pulse
    uint8_t n         = 0;         // the strip's length as the plugin has it
    uint8_t strip[kStripMax * 3] = {};
    uint8_t stripPct  = 0;
    // The lamps (1.2.0): the fast states, from counters every frame.
    uint32_t nodeOn  = 0;          // bit k: line k + 1 has somebody on it; bit 16 the sysop's line
    uint32_t nodeHot = 0;          // bit k: and they pressed a key this frame
    bool     rx = false, tx = false, disk = false, error = false;
    bool     blink = true;         // the 2 Hz phase a blinking lamp is lit in
    // and the slow ones, from the figures.
    bool     ring = false, mail = false, staff = false, listed = false, closed = false, card = false;
};

// ---------------------------------------------------------------------------
// Pixels
// ---------------------------------------------------------------------------
inline uint16_t pack(const Rgb& c) { return panelgfx::rgb(c.r, c.g, c.b); }

// div255: x / 255 rounded, for x up to 255 x 255.
inline uint32_t div255(uint32_t x) { return (x + 128u + ((x + 128u) >> 8)) >> 8; }

// screen: light l over art a, one channel.
inline uint8_t screen(uint8_t a, uint8_t l) {
    return static_cast<uint8_t>(255u - div255((255u - a) * (255u - l)));
}

// restore: a box of the background back into the framebuffer.
inline void restore(panelgfx::Canvas& c, const uint16_t* bg, const Box& b) {
    panelgfx::Rect r = panelgfx::clip(panelgfx::R(b.x, b.y, b.w, b.h), c.w, c.h);
    for (int y = r.y; y < r.y + r.h; ++y)
        memcpy(c.px + static_cast<size_t>(y) * c.w + r.x, bg + static_cast<size_t>(y) * c.w + r.x,
               static_cast<size_t>(r.w) * 2u);
}

// ground: a widget's box put back to the art, or filled with its colour.
inline void ground(panelgfx::Canvas& c, const uint16_t* bg, const Box& b, bool hasBg, const Rgb& col) {
    if (hasBg) panelgfx::fill(c, panelgfx::R(b.x, b.y, b.w, b.h), pack(col));
    else       restore(c, bg, b);
}

inline Rgb scaled(const Rgb& c, uint8_t level) {
    Rgb o;
    o.r = static_cast<uint8_t>(div255(static_cast<uint32_t>(c.r) * level));
    o.g = static_cast<uint8_t>(div255(static_cast<uint32_t>(c.g) * level));
    o.b = static_cast<uint8_t>(div255(static_cast<uint32_t>(c.b) * level));
    return o;
}

// ---------------------------------------------------------------------------
// LEDs
// ---------------------------------------------------------------------------
// ledWeights: side x side pairs of bytes for l's box: [0] how much of the
// colour, [1] how much of the hot spot. Worked once at load.
inline void ledWeights(const Led& l, uint8_t* map) {
    const int   side = l.d + 2 * l.halo;
    const float c    = side / 2.0f;             // the middle of the box
    const float r    = l.d / 2.0f;
    const float halo = static_cast<float>(l.halo);
    const float hotR = r * 0.5f;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            float e = 0, k = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    const float dx = x + (sx + 0.5f) / 4.0f - c, dy = y + (sy + 0.5f) / 4.0f - c;
                    const float t  = sqrtf(dx * dx + dy * dy);
                    if (t <= r) {
                        const float u = t / r;
                        e += 0.72f + 0.28f * (1.0f - u * u);          // the lens, brighter inwards
                    } else if (halo > 0 && t < r + halo) {
                        const float u = 1.0f - (t - r) / halo;
                        e += 0.5f * u * u;                             // the glow round it
                    }
                    if (t < hotR) {
                        const float u = 1.0f - t / hotR;
                        k += u * sqrtf(u);
                    }
                }
            uint8_t* p = map + (static_cast<size_t>(y) * side + x) * 2u;
            const float ev = e / 16.0f * 255.0f + 0.5f, kv = k / 16.0f * 255.0f + 0.5f;
            p[0] = static_cast<uint8_t>(ev > 255.0f ? 255.0f : ev);
            p[1] = static_cast<uint8_t>(kv > 255.0f ? 255.0f : kv);
        }
}

inline size_t ledMapBytes(const Led& l) {
    const size_t side = static_cast<size_t>(l.d) + 2u * l.halo;
    return side * side * 2u;
}

// drawLed: l's box as the background with col's light over it. col is the
// colour as it should look on glass; black leaves the art as it is.
inline void drawLed(panelgfx::Canvas& c, const uint16_t* bg, const Led& l, const uint8_t* map, const Rgb& col) {
    const Box b = ledBox(l);
    const int side = b.w;
    const uint8_t peak = col.r > col.g ? (col.r > col.b ? col.r : col.b) : (col.g > col.b ? col.g : col.b);
    const uint32_t hot = static_cast<uint32_t>(peak) * peak / 255u;   // a hot spot for bright LEDs only
    for (int y = 0; y < side; ++y) {
        const int py = b.y + y;
        if (py < 0 || py >= c.h) continue;
        const uint16_t* src = bg + static_cast<size_t>(py) * c.w;
        uint16_t*       dst = c.px + static_cast<size_t>(py) * c.w;
        for (int x = 0; x < side; ++x) {
            const int px = b.x + x;
            if (px < 0 || px >= c.w) continue;
            const uint8_t* w = map + (static_cast<size_t>(y) * side + x) * 2u;
            if (!peak || (!w[0] && !w[1])) { dst[px] = src[px]; continue; }
            const panelgfx::Rgb8 a = panelgfx::unpack(src[px]);
            const uint32_t k = div255(static_cast<uint32_t>(w[1]) * hot);
            auto ch = [&](uint8_t art, uint8_t v) {
                uint32_t e = div255(static_cast<uint32_t>(v) * w[0]);
                e += div255((255u - e) * k);
                return screen(art, static_cast<uint8_t>(e));
            };
            dst[px] = panelgfx::rgb(ch(a.r, col.r), ch(a.g, col.g), ch(a.b, col.b));
        }
    }
}

// onGlass: a pixel as the lights plugin sent it, at pct, as the glass shows
// it (panel_gfx.h glassLevel: the status skin's own rule).
inline Rgb onGlass(const uint8_t* rgb, uint8_t pct) {
    Rgb o;
    o.r = panelgfx::glassLevel(rgb[0], pct);
    o.g = panelgfx::glassLevel(rgb[1], pct);
    o.b = panelgfx::glassLevel(rgb[2], pct);
    return o;
}

// lampLevel: how brightly a lamp in state s is lit, 0 to 255. A node's lamp
// glows while somebody is on the line and flares on their keystrokes, the
// way a front panel's lamps flicker with the bus.
constexpr uint32_t kSysopBit = 1u << kNodeLines;
inline uint8_t lampLevel(uint8_t s, const LedState& st) {
    if (s >= ST_NODE || s == ST_NODE_S) {
        const uint32_t bit = s == ST_NODE_S ? kSysopBit : 1u << (s - ST_NODE);
        return (st.nodeHot & bit) ? 255 : (st.nodeOn & bit) ? 90 : 0;
    }
    switch (s) {
        case ST_RX:     return st.rx ? 255 : 0;
        case ST_TX:     return st.tx ? 255 : 0;
        case ST_DISK:   return st.disk ? 255 : 0;
        case ST_RUN:    return 255;
        case ST_CLOSED: return st.closed ? 255 : 0;
        case ST_RING:   return st.ring ? 255 : 0;
        case ST_MAIL:   return st.mail ? 255 : 0;
        case ST_STAFF:  return st.staff ? 255 : 0;
        case ST_LISTED: return st.listed ? 255 : 0;
        case ST_CARD:   return st.card ? 255 : 0;
        case ST_ERROR:  return st.error ? 255 : 0;
        case ST_ONLINE: return (st.nodeOn & ~kSysopBit) ? 255 : 0;
        case ST_OPEN:   return st.closed ? 0 : 255;
        default:        return 0;
    }
}

// ---------------------------------------------------------------------------
// Text over the art, in any of the three faces
// ---------------------------------------------------------------------------
inline const uint8_t* glyphBits(uint8_t face, uint8_t g) {
    return face == FACE_BIG ? panelfont::kBig[g] : face == FACE_TINY ? panelfont::kTiny[g] : panelfont::kSmall[g];
}

// glyphOver: one glyph's lit pixels only, clipped to the box.
inline void glyphOver(panelgfx::Canvas& c, int x, int y, uint8_t g, uint16_t fg, uint8_t face, const Box& clipTo) {
    const int gw = kGlyphW[face], gh = kGlyphH[face];
    const uint8_t* bits = glyphBits(face, g);
    const int bpr = (gw + 7) / 8;
    for (int row = 0; row < gh; ++row) {
        const int py = y + row;
        if (py < clipTo.y || py >= clipTo.y + clipTo.h || py < 0 || py >= c.h) continue;
        uint16_t* line = c.px + static_cast<size_t>(py) * c.w;
        for (int col = 0; col < gw; ++col) {
            const int px = x + col;
            if (px < clipTo.x || px >= clipTo.x + clipTo.w || px < 0 || px >= c.w) continue;
            if ((bits[row * bpr + col / 8] >> (7 - col % 8)) & 1u) line[px] = fg;
        }
    }
}

// textRow: s in the row box r (one glyph tall), in style st: the box put
// back to the art or filled, the shadow a pixel down and right, the text.
// Cut at the box, never wrapped.
// A run of a row's characters in a colour of its own (tint=yes): the rank
// mark, an event's kind. len 0 is none.
struct Span { uint8_t at = 0, len = 0; Rgb colour; };

inline void textRow(panelgfx::Canvas& c, const uint16_t* bg, const Box& r, const char* s, const TextStyle& st,
                    const Span& span = Span()) {
    ground(c, bg, r, st.hasBg, st.bg);
    const int gw = kGlyphW[st.face];
    const int fit = r.w / gw;
    int n = panelgfx::glyphs(s);
    if (n > fit) n = fit;
    const int w = n * gw;
    const int x0 = st.align == AL_RIGHT ? r.x + r.w - w : st.align == AL_CENTRE ? r.x + (r.w - w) / 2 : r.x;
    for (int pass = st.hasShadow ? 0 : 1; pass < 2; ++pass) {
        const char* p = s;
        const uint16_t col = pass ? pack(st.fg) : pack(st.shadow), tint = pack(span.colour);
        for (int i = 0; i < n; ++i) {
            const uint8_t g = panelgfx::glyph(p);
            const bool inSpan = pass && i >= span.at && i < span.at + span.len;
            glyphOver(c, x0 + i * gw + (pass ? 0 : 1), r.y + (pass ? 0 : 1), g, inSpan ? tint : col, st.face, r);
        }
    }
}

// ---------------------------------------------------------------------------
// The widgets' words and pixels (1.2.0)
// ---------------------------------------------------------------------------
// nodeRow: one line of a node list, fitted to cols characters:
// " 3) alice      CHAT  4m", or " 5  waiting" for a free line.
inline void nodeRow(const NodeRow& r, int cols, char* out, size_t n) {
    char label[4];
    if (r.line) snprintf(label, sizeof(label), "%2u", static_cast<unsigned>(r.line));
    else        snprintf(label, sizeof(label), " S");
    if (!r.on) {
        snprintf(out, n, "%s  %s", label, cols >= 12 ? "waiting" : "-");
        return;
    }
    const int rest = cols - 8;                  // label, mark, two spaces, the time
    if (rest < 4) {
        snprintf(out, n, "%s%c %s", label, r.mark, r.handle);
        return;
    }
    const int dw = rest >= 14 ? (rest - 10 < 8 ? rest - 10 : 8) : 0;
    const int hw = rest - dw - (dw ? 1 : 0);
    if (dw) snprintf(out, n, "%s%c %-*.*s %-*.*s%4s", label, r.mark, hw, hw, r.handle, dw, dw, r.doing, r.onFor);
    else    snprintf(out, n, "%s%c %-*.*s%4s", label, r.mark, hw, hw, r.handle, r.onFor);
}

// listRow: row k of a node list as it shows now, or "" past its end.
inline const NodeRow* listRow(const Figures& f, const List& l, int k) {
    int seen = 0;
    for (uint8_t i = 0; i < f.rows && i < kRowsMax; ++i) {
        const NodeRow& r = f.row[i];
        if (!r.on && (!l.free || r.line == 0)) continue;   // the sysop's line only while on it
        if (seen++ == k) return &r;
    }
    return nullptr;
}

// listCount: how many rows a node list would list, all told.
inline int listCount(const Figures& f, const List& l) {
    int n = 0;
    for (uint8_t i = 0; i < f.rows && i < kRowsMax; ++i)
        if (f.row[i].on || (l.free && f.row[i].line != 0)) ++n;
    return n;
}

// The tint (tint=yes): the rank marks' and the events' kinds' colours, the
// status layout's hues lifted for a dark ground (the tty-ux spec's set).
inline Rgb rankTint(char mark, const Rgb& fg) {
    switch (mark) {
        case ']': return { 0xFF, 0x90, 0x90 };
        case '>': return { 0xFF, 0xFF, 0x55 };
        case '*': return { 0xB0, 0xB0, 0xB0 };
        default:  return fg;
    }
}
inline Rgb kindTint(const char* e, const Rgb& fg) {
    const char* k = strlen(e) > 6 ? e + 6 : "";
    if (!strncmp(k, "login", 5))  return { 0x5D, 0xDC, 0x7A };
    if (!strncmp(k, "guest", 5))  return { 0xF0, 0xB8, 0x60 };
    if (!strncmp(k, "logoff", 6)) return { 0xA8, 0xA8, 0xA8 };
    if (!strncmp(k, "page", 4) || !strncmp(k, "ring", 4)) return { 0xFF, 0x9A, 0x60 };
    return fg;
}

// eventRow: row k of an event list, of rows in all.
inline const char* eventRow(const Figures& f, const List& l, int k, int rows) {
    const int n = f.events < kEventsMax ? f.events : kEventsMax;
    if (!l.oldest) return k < n ? f.event[k] : "";
    // Oldest at the top, newest at the foot, like a printout: the rows fill
    // from the bottom.
    const int shown = n < rows ? n : rows;
    const int from = rows - shown;
    if (k < from) return "";
    return f.event[shown - 1 - (k - from)];
}

// The seven segments of a digit, a to g, as the digits 0 to 9 light them.
constexpr uint8_t kSegs[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

// drawDigits: the display with value v (or all unlit when !has).
inline void drawDigits(panelgfx::Canvas& c, const uint16_t* bg, const Digits& d, bool has, int v) {
    restore(c, bg, d.box);
    const int h = d.box.h, w = digitW(h), gap = digitGap(h);
    const int t = h / 7 < 2 ? 2 : h / 7;
    char text[kDigitsNMax + 1];
    for (int i = 0; i < d.n; ++i) text[i] = ' ';
    text[d.n] = '\0';
    if (has) {
        if (d.value == LW_CLOCK) {
            snprintf(text, sizeof(text), "%02d%02d", (v / 100) % 100, v % 100);
        } else {
            long top = 1;
            for (int i = 0; i < d.n; ++i) top *= 10;
            if (v >= top) v = static_cast<int>(top - 1);
            if (v < 0) v = 0;
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%d", v);
            const int len = static_cast<int>(strlen(tmp));
            memcpy(text + d.n - len, tmp, static_cast<size_t>(len));
        }
    }
    const uint16_t on = pack(d.fg), off = pack(d.dim);
    for (int i = 0; i < d.n; ++i) {
        const int x = d.box.x + i * (w + gap), y = d.box.y;
        const uint8_t lit = text[i] >= '0' && text[i] <= '9' ? kSegs[text[i] - '0'] : 0;
        const panelgfx::Rect seg[7] = {
            panelgfx::R(x + t, y, w - 2 * t, t),                        // a
            panelgfx::R(x + w - t, y + t, t, h / 2 - t),                // b
            panelgfx::R(x + w - t, y + h / 2, t, h - h / 2 - t),        // c
            panelgfx::R(x + t, y + h - t, w - 2 * t, t),                // d
            panelgfx::R(x, y + h / 2, t, h - h / 2 - t),                // e
            panelgfx::R(x, y + t, t, h / 2 - t),                        // f
            panelgfx::R(x + t, y + h / 2 - t / 2, w - 2 * t, t),        // g
        };
        for (int s = 0; s < 7; ++s) {
            if ((lit >> s) & 1u) panelgfx::fill(c, seg[s], on);
            else if (d.hasDim)   panelgfx::fill(c, seg[s], off);
        }
    }
    if (d.value == LW_CLOCK && d.n == 4) {                         // the colon, between 2 and 3
        const int cx = d.box.x + 2 * w + gap + gap / 2 - t / 2;
        const uint16_t col = has ? on : off;
        if (has || d.hasDim) {
            panelgfx::fill(c, panelgfx::R(cx, d.box.y + h / 3 - t / 2, t, t), col);
            panelgfx::fill(c, panelgfx::R(cx, d.box.y + 2 * h / 3 - t / 2, t, t), col);
        }
    }
}

// clampK: a fraction held to 0..1000, whatever the figures behind it did.
inline int clampK(long v) { return v < 0 ? 0 : v > 1000 ? 1000 : static_cast<int>(v); }

// fraction: a meter's source now, 0 to 1000.
inline int fraction(uint8_t src, const Figures& f, const Live& lv) {
    switch (src) {
        case SRC_TRAFFIC: {                        // log scale: 1 B/s to 64 KB/s
            if (!lv.rate) return 0;
            const float v = log2f(static_cast<float>(lv.rate) + 1.0f) / 16.0f;
            return v >= 1.0f ? 1000 : static_cast<int>(v * 1000.0f);
        }
        case SRC_HEAP:
            return f.heapTopK ? clampK(static_cast<long>(f.nHeapK) * 1000 / f.heapTopK) : 0;
        case SRC_CARD:
            return f.cardTotalMB ? clampK(static_cast<long>(static_cast<unsigned long long>(f.cardFreeMB) * 1000u /
                                                            f.cardTotalMB)) : 0;
        case SRC_CALLERS:
            return f.nLines > 0 ? clampK(static_cast<long>(f.nOnline) * 1000 / f.nLines) : 0;
        case SRC_RSSI: {
            if (!f.nRssi) return 0;
            int r = f.nRssi < -90 ? -90 : f.nRssi > -50 ? -50 : f.nRssi;
            return (r + 90) * 1000 / 40;
        }
        default: return 0;
    }
}

// drawMeter: the bar filled to px pixels along its length.
inline void drawMeter(panelgfx::Canvas& c, const uint16_t* bg, const Meter& m, int px) {
    ground(c, bg, m.box, m.hasBg, m.bg);
    if (px <= 0) return;
    const Box& b = m.box;
    const Rgb hi = { static_cast<uint8_t>(m.fg.r + (255 - m.fg.r) / 3), static_cast<uint8_t>(m.fg.g + (255 - m.fg.g) / 3),
                     static_cast<uint8_t>(m.fg.b + (255 - m.fg.b) / 3) };
    if (m.up) {
        panelgfx::fill(c, panelgfx::R(b.x, b.y + b.h - px, b.w, px), pack(m.fg));
        panelgfx::fill(c, panelgfx::R(b.x, b.y + b.h - px, b.w, 1), pack(hi));   // its lit edge
    } else {
        panelgfx::fill(c, panelgfx::R(b.x, b.y, px, b.h), pack(m.fg));
        panelgfx::fill(c, panelgfx::R(b.x, b.y, px, 1), pack(hi));
    }
}

// sampleOf: sample i back from the newest (0 the newest), 0 to 1000.
inline int sampleOf(uint8_t src, const Live& lv, int i, int lines) {
    if (static_cast<uint32_t>(i) >= lv.samples || i >= kHistory) return -1;
    const int at = (lv.head + kHistory - i) % kHistory;
    // Against the lines now: a visible sysop leaving takes one from them, so
    // an old sample can be more than the lines are; held to the box.
    if (src == SRC_CALLERS) return lines > 0 && lv.callers ? clampK(static_cast<long>(lv.callers[at]) * 1000 / lines) : 0;
    if (!lv.traffic || !lv.traffic[at]) return 0;
    const float v = log2f(static_cast<float>(lv.traffic[at]) + 1.0f) / 16.0f;
    return v >= 1.0f ? 1000 : static_cast<int>(v * 1000.0f);
}

// graphHash: the sweep's column heights as one hash, so a graph whose
// picture did not move is not drawn again when a sample lands.
inline uint32_t graphHash(const Meter& g, const Live& lv, int lines) {
    uint32_t h = 2166136261u ^ (0x6AAFu + g.src);
    for (int i = 0; i < g.box.w; ++i) {
        const int v = sampleOf(g.src, lv, i, lines);
        const int hgt = v <= 0 ? 0 : (v * g.box.h + 999) / 1000;
        h ^= static_cast<uint32_t>(hgt & 0xFF);
        h *= 16777619u;
    }
    return h ? h : 1u;
}

// drawGraph: the sweep, newest at the right, one column a sample.
inline void drawGraph(panelgfx::Canvas& c, const uint16_t* bg, const Meter& g, const Live& lv, int lines) {
    ground(c, bg, g.box, g.hasBg, g.bg);
    const Box& b = g.box;
    const uint16_t col = pack(g.fg);
    const Rgb hiRgb = { static_cast<uint8_t>(g.fg.r + (255 - g.fg.r) / 2), static_cast<uint8_t>(g.fg.g + (255 - g.fg.g) / 2),
                        static_cast<uint8_t>(g.fg.b + (255 - g.fg.b) / 2) };
    const uint16_t hi = pack(hiRgb);
    for (int i = 0; i < b.w; ++i) {
        const int v = sampleOf(g.src, lv, i, lines);
        if (v <= 0) continue;
        int hgt = (v * b.h + 999) / 1000;
        if (hgt > b.h) hgt = b.h;
        const int x = b.x + b.w - 1 - i;
        panelgfx::fill(c, panelgfx::R(x, b.y + b.h - hgt, 1, hgt), col);
        panelgfx::dot(c, x, b.y + b.h - hgt, hi);
    }
}

inline uint32_t hashText(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= static_cast<uint8_t>(*s++); h *= 16777619u; }
    return h ? h : 1u;
}

inline uint32_t hashNum(uint32_t tag, uint32_t v) {
    uint32_t h = 2166136261u ^ tag;
    for (int i = 0; i < 4; ++i) { h ^= (v >> (8 * i)) & 0xFFu; h *= 16777619u; }
    return h ? h : 1u;
}

// mergeBoxes: the boxes as few rectangles to send, a box joining another when
// the two together waste no more than half again what they cover (a row of
// LEDs goes as one rectangle; two in far corners do not become the glass).
inline int mergeBoxes(Box* b, int n) {
    bool again = true;
    while (again) {
        again = false;
        for (int i = 0; i < n && !again; ++i)
            for (int k = i + 1; k < n && !again; ++k) {
                const int x0 = b[i].x < b[k].x ? b[i].x : b[k].x, y0 = b[i].y < b[k].y ? b[i].y : b[k].y;
                const int x1 = b[i].x + b[i].w > b[k].x + b[k].w ? b[i].x + b[i].w : b[k].x + b[k].w;
                const int y1 = b[i].y + b[i].h > b[k].y + b[k].h ? b[i].y + b[i].h : b[k].y + b[k].h;
                const long u = static_cast<long>(x1 - x0) * (y1 - y0);
                const long s = static_cast<long>(b[i].w) * b[i].h + static_cast<long>(b[k].w) * b[k].h;
                if (u * 2 <= s * 3) {
                    b[i] = { static_cast<int16_t>(x0), static_cast<int16_t>(y0), static_cast<int16_t>(x1 - x0),
                             static_cast<int16_t>(y1 - y0) };
                    b[k] = b[--n];
                    again = true;
                }
            }
    }
    return n;
}

inline void queue(panelgfx::Dirty& d, const Box& b) { d.add(panelgfx::R(b.x, b.y, b.w, b.h)); }

// The most the LEDs may leave queued before a frame of them waits for the
// glass: the panel sends a band a tick, and a strip effect that changes
// every pixel every frame would otherwise queue faster than it drains. The
// state is read afresh each frame, so a skipped frame is lost, not late.
constexpr uint8_t kLedBacklog = 4;

// The most LED pixels one frame recomposes (Rule no. 1). Each is a few
// PSRAM reads and ten multiplies, so a frame's worth of loop time is bounded
// by pixels, not by how many LEDs changed: a skin at the 65,536-pixel cap
// with a strip effect changing every pixel would otherwise take tens of
// milliseconds a frame. Past the budget the rest wait for the next frame,
// taken in turn from where this one stopped, so none starves.
constexpr uint32_t kLedBudget = 8192;

// The same for the widgets (1.2.0), a pass: a text row is a restore and a
// row of glyphs, a meter or a graph a fill, so a pixel costs less than an
// LED's; a pass past this carries on from where it stopped on the next. The
// reader holds every unit to it (kUnitPxMax), so no one unit is bigger.
constexpr uint32_t kWidgetBudget = kUnitPxMax;

// LED slots: 0 the drive light, 1 the activity lamp, 2 to 17 the strip,
// 18 on the lamps.
constexpr uint8_t kLampBase   = kStripMax + 2;
constexpr uint8_t kSceneLeds  = kLampBase + kLampsMax;
// The widgets' units: a text row, the clock, a field, a display, a list's
// row, a meter, a graph. Enough for every limit at once at the tiny face.
constexpr uint16_t kUnitsMax  = 224;

struct Scene {
    const Manifest* m   = nullptr;
    const uint16_t* bg  = nullptr;          // the background, the panel's size
    const uint8_t*  map[kSceneLeds] = {};   // weights, by LED slot
    // What is on the glass now.
    Rgb      shown[kSceneLeds];
    bool     valid[kSceneLeds] = {};        // shown[i] is what the glass has
    uint8_t  next = 0;                      // the LED a frame starts at
    uint32_t hash[kUnitsMax] = {};          // each unit's words or figure as drawn
    uint16_t cursor = 0;                    // the unit a widget pass starts at
    bool     pending = false;               // the last pass stopped at its budget

    // reset: a new skin, in place (a Scene is about 1.3 KB, too much for a
    // temporary on the BBS task's stack).
    void reset(const Manifest* nm, const uint16_t* nbg) {
        m = nm;
        bg = nbg;
        memset(map, 0, sizeof(map));
        memset(shown, 0, sizeof(shown));
        forget();
        pending = false;
    }

    // forget: nothing is on the glass but the art; every LED and widget is
    // drawn on the next leds() and widgets().
    void forget() {
        memset(valid, 0, sizeof(valid));
        memset(hash, 0, sizeof(hash));
        next = 0;
        cursor = 0;
        pending = true;
    }

    // full: the whole skin from nothing: the art, the rectangles' own
    // backgrounds, and every LED and widget owed to the next leds() and
    // widgets().
    void full(panelgfx::Canvas& c, panelgfx::Dirty& d) {
        memcpy(c.px, bg, static_cast<size_t>(c.w) * c.h * 2u);
        fills(c);
        forget();
        d.clear();
        d.add(panelgfx::R(0, 0, c.w, c.h));
    }

    // fills: the text rectangle and the clock filled with their background
    // colours, where they have one.
    void fills(panelgfx::Canvas& c) const {
        if (m->hasText && m->textStyle.hasBg)
            panelgfx::fill(c, panelgfx::R(m->text.x, m->text.y, m->text.w, m->text.h), pack(m->textStyle.bg));
        if (m->hasClock && m->clockStyle.hasBg)
            panelgfx::fill(c, panelgfx::R(m->clock.x, m->clock.y, m->clock.w, m->clock.h), pack(m->clockStyle.bg));
        for (uint8_t i = 0; i < m->nNodes; ++i)
            if (m->nodes[i].style.hasBg) ground(c, bg, m->nodes[i].box, true, m->nodes[i].style.bg);
        for (uint8_t i = 0; i < m->nEvents; ++i)
            if (m->events[i].style.hasBg) ground(c, bg, m->events[i].box, true, m->events[i].style.bg);
    }

    const Led* ledAt(uint8_t i) const {
        if (i == 0) return m->drive.on ? &m->drive : nullptr;
        if (i == 1) return m->activity.on ? &m->activity : nullptr;
        if (i < kLampBase) return i - 2 < m->strip ? &m->led[i - 2] : nullptr;
        return i - kLampBase < m->nLamps ? &m->lamp[i - kLampBase].led : nullptr;
    }

    // leds: every LED and lamp at this frame's state, redrawn where it
    // changed, up to kLedBudget pixels. How many it redrew; 0 also when it
    // held back for the glass.
    int leds(panelgfx::Canvas& c, panelgfx::Dirty& d, const LedState& st) {
        const uint8_t backlog = static_cast<uint8_t>(d.n + (panelgfx::empty(d.cur) ? 0 : 1));
        if (backlog > kLedBacklog) return 0;
        Box boxes[kSceneLeds];
        int nb = 0;
        uint32_t spent = 0;
        uint8_t i = next;
        for (uint8_t k = 0; k < kSceneLeds; ++k, i = static_cast<uint8_t>((i + 1) % kSceneLeds)) {
            const Led* l = ledAt(i);
            if (!l) continue;
            Rgb col;
            if (i == 0)                col = onGlass(st.drive, st.drivePct);
            else if (i == 1)           col = st.activity ? l->colour : Rgb();
            else if (i < kLampBase)    col = i - 2 < st.n ? onGlass(st.strip + (i - 2) * 3, st.stripPct) : Rgb();
            else {
                const Lamp& lp = m->lamp[i - kLampBase];
                const uint8_t lv = lampLevel(lp.state, st);
                col = scaled(l->colour, lp.blink && !st.blink ? 0 : lv);
            }
            if (valid[i] && sameRgb(col, shown[i])) continue;
            const Box b = ledBox(*l);
            const uint32_t cost = static_cast<uint32_t>(b.w) * b.h;
            if (spent && spent + cost > kLedBudget) break;   // the rest next frame, from here
            spent += cost;
            shown[i] = col;
            valid[i] = true;
            drawLed(c, bg, *l, map[i], col);
            boxes[nb++] = b;
        }
        next = i;
        const int q = mergeBoxes(boxes, nb);
        for (int k = 0; k < q; ++k) queue(d, boxes[k]);
        return nb;
    }

    // rowsOf: how many glyph rows a box holds in a face, at most 32.
    static int rowsOf(const Box& b, uint8_t face) {
        const int r = b.h / kGlyphH[face];
        return r > 32 ? 32 : r;
    }
    int textRows() const {
        if (!m->hasText) return 0;
        const int r = rowsOf(m->text, m->textStyle.face);
        return r > kLinesMax + kWhoMax ? kLinesMax + kWhoMax : r;
    }
    uint16_t unitCount() const {
        int n = textRows() + (m->hasClock ? 1 : 0) + m->nFields + m->nDigits + m->nMeters + m->nGraphs;
        for (uint8_t i = 0; i < m->nNodes; ++i)  n += rowsOf(m->nodes[i].box, m->nodes[i].style.face);
        for (uint8_t i = 0; i < m->nEvents; ++i) n += rowsOf(m->events[i].box, m->events[i].style.face);
        return static_cast<uint16_t>(n > kUnitsMax ? kUnitsMax : n);
    }

    // unit: unit u's words or figure as a hash, and its box; with c, drawn.
    void unit(uint16_t u, panelgfx::Canvas* c, const Figures& f, const Live& lv, uint32_t& h, Box& box) {
        char s[96];
        int k = u;
        // the text rectangle's rows
        const int tr = textRows();
        if (k < tr) {
            const int gh = kGlyphH[m->textStyle.face];
            int row = 0;
            const char* word = "";
            for (uint8_t i = 0; i < m->nLines; ++i) {
                const int span = m->lines[i] == LW_WHO ? tr - row : 1;
                if (k < row + span) { word = lineText(f, m->lines[i], static_cast<uint8_t>(k - row)); break; }
                row += span;
            }
            box = { m->text.x, static_cast<int16_t>(m->text.y + k * gh), m->text.w, static_cast<int16_t>(gh) };
            h = hashText(word);
            if (c) textRow(*c, bg, box, word, m->textStyle);
            return;
        }
        k -= tr;
        if (m->hasClock) {
            if (k == 0) {
                box = m->clock;
                h = hashText(f.clock);
                if (c) textRow(*c, bg, box, f.clock, m->clockStyle);
                return;
            }
            --k;
        }
        if (k < m->nFields) {
            const Field& fl = m->field[k];
            snprintf(s, sizeof(s), "%s%s", fl.label, lineText(f, fl.value, 0));
            box = fl.box;
            h = hashText(s);
            if (c) textRow(*c, bg, box, s, fl.style);
            return;
        }
        k -= m->nFields;
        if (k < m->nDigits) {
            const Digits& d = m->digits[k];
            int v = 0;
            const bool has = numberOf(f, d.value, v);
            box = d.box;
            h = hashNum(0xD161u, has ? static_cast<uint32_t>(v) : 0xFFFFFFFFu);
            if (c) drawDigits(*c, bg, d, has, v);
            return;
        }
        k -= m->nDigits;
        for (uint8_t i = 0; i < m->nNodes; ++i) {
            const List& l = m->nodes[i];
            const int rows = rowsOf(l.box, l.style.face);
            if (k < rows) {
                const int gh = kGlyphH[l.style.face];
                const int all = listCount(f, l);
                const NodeRow* r = nullptr;
                TextStyle st = l.style;
                Span span;
                if (all > rows && k == rows - 1) {
                    // More on than rows: the last says how many it could not
                    // name, so the list never reads as whole when it is not.
                    snprintf(s, sizeof(s), " +%d more", all - (rows - 1));
                } else if ((r = listRow(f, l, k)) != nullptr) {
                    nodeRow(*r, l.box.w / kGlyphW[l.style.face], s, sizeof(s));
                    if (!r->on && l.hasDim) st.fg = l.dim;
                    if (r->on && l.tint) { span.at = 0; span.len = 3; span.colour = rankTint(r->mark, st.fg); }
                } else {
                    s[0] = '\0';
                }
                box = { l.box.x, static_cast<int16_t>(l.box.y + k * gh), l.box.w, static_cast<int16_t>(gh) };
                h = hashText(s) ^ (r && !r->on && l.hasDim ? 0x5A5A5A5Au : 0u);
                if (c) textRow(*c, bg, box, s, st, span);
                return;
            }
            k -= rows;
        }
        for (uint8_t i = 0; i < m->nEvents; ++i) {
            const List& l = m->events[i];
            const int rows = rowsOf(l.box, l.style.face);
            if (k < rows) {
                const int gh = kGlyphH[l.style.face];
                const char* e = eventRow(f, l, k, rows);
                box = { l.box.x, static_cast<int16_t>(l.box.y + k * gh), l.box.w, static_cast<int16_t>(gh) };
                h = hashText(e);
                Span span;
                if (l.tint && strlen(e) > 6) {
                    const char* sp = strchr(e + 6, ' ');
                    span.at = 6;
                    span.len = static_cast<uint8_t>(sp ? sp - (e + 6) : static_cast<long>(strlen(e + 6)));
                    span.colour = kindTint(e, l.style.fg);
                }
                if (c) textRow(*c, bg, box, e, l.style, span);
                return;
            }
            k -= rows;
        }
        if (k < m->nMeters) {
            const Meter& g = m->meter[k];
            const int len = g.up ? g.box.h : g.box.w;
            const int px = fraction(g.src, f, lv) * len / 1000;
            box = g.box;
            h = hashNum(0x3E7Eu, static_cast<uint32_t>(px));
            if (c) drawMeter(*c, bg, g, px);
            return;
        }
        k -= m->nMeters;
        if (k < m->nGraphs) {
            const Meter& g = m->graph[k];
            box = g.box;
            h = graphHash(g, lv, f.nLines);
            if (c) drawGraph(*c, bg, g, lv, f.nLines);
            return;
        }
        box = Box();
        h = 0;
    }

    // widgets: every widget whose words or figure changed, redrawn and
    // queued, up to kWidgetBudget pixels; the rest on the next pass, from
    // where this one stopped (pending says so). How many it redrew.
    int widgets(panelgfx::Canvas& c, panelgfx::Dirty& d, const Figures& f, const Live& lv,
                uint32_t budget = kWidgetBudget) {
        const uint16_t n = unitCount();
        pending = false;
        if (!n) return 0;
        // Held back while the glass is behind, as the LEDs are: the widgets'
        // rectangles would queue ahead of the lamps' and make them late.
        const uint8_t backlog = static_cast<uint8_t>(d.n + (panelgfx::empty(d.cur) ? 0 : 1));
        if (backlog > kLedBacklog) { pending = true; return 0; }
        if (cursor >= n) cursor = 0;
        int drew = 0;
        uint32_t spent = 0;
        for (uint16_t k = 0; k < n; ++k) {
            const uint16_t u = static_cast<uint16_t>((cursor + k) % n);
            uint32_t h = 0;
            Box box;
            unit(u, nullptr, f, lv, h, box);
            if (hash[u] == h) continue;
            const uint32_t cost = static_cast<uint32_t>(box.w) * box.h;
            if (spent && spent + cost > budget) {                // the rest next pass, from here
                cursor = u;
                pending = true;
                return drew;
            }
            spent += cost;
            unit(u, &c, f, lv, h, box);
            hash[u] = h;
            queue(d, box);
            ++drew;
        }
        return drew;
    }

    // text: every widget now, whatever the budget: the tests' way in, and
    // what 1.1.2's status lines were.
    void text(panelgfx::Canvas& c, panelgfx::Dirty& d, const Figures& f) {
        Live none;
        widgets(c, d, f, none, 0xFFFFFFFFu);
    }
};

} // namespace skin
