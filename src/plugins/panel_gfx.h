/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/panel_gfx.h
 * Module:       Plugins / panel (BBS_HAS_LCD boards only)
 *
 * Purpose:      Everything the panel plugin does to pixels that is not the
 *               panel: the site's palette in RGB565, text in the Spleen
 *               faces, the icons and the status row's glyphs, the Wi-Fi
 *               antenna, the square LEDs, where each figure goes on the
 *               glass at a given size, how the two lists share their slots,
 *               and which rectangles still need sending. No platform and no
 *               BBS in it, so host/test_panel.cpp can check every edge of it
 *               with no display attached.
 *
 * Design:       The framebuffer is native RGB565, one uint16_t a pixel, row
 *               after row. The panel is told its data is little-endian, so
 *               a row goes to the glass exactly as it sits in memory.
 *
 *               Only what changed is sent. A figure that changes has its box
 *               redrawn in the framebuffer and the box queued (Dirty); the
 *               plugin sends the queue one band at a time, a band being as
 *               many whole rows as the platform's DMA staging buffer holds.
 *
 *               The layout is revision 2 of internal/tty-ux-panel-2026-09-24.md:
 *               a phone-style header (a rotating slot on the blue bar, a band
 *               of status glyphs, the antenna and the clock), the track with
 *               its dot, the callers on now and the recent events sharing ten
 *               slots, a system row, and the strip as a row of square LEDs.
 *
 * Libraries:    none
 * Targets:      ESP32-S3 boards with BBS_HAS_LCD, and the Linux host tests
 * See also:     src/plugins/panel.cpp, src/plugins/panel_font.h
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
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include "panel_font.h"

namespace panelgfx {

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

// The site's tokens (the directory's server.py), packed. What each is for
// is the site's meaning of it: ink for text, dim and faint for age, live
// for "up", warm for "waiting", risk for "look at this", dial for things
// you can act on, busy for somebody wanting you, struct for headings.
namespace tok {
constexpr uint16_t kBg      = rgb(0x00, 0x00, 0x00);
constexpr uint16_t kSurface = rgb(0x14, 0x14, 0x1B);   // an LED that is off
constexpr uint16_t kRule    = rgb(0x2C, 0x2C, 0x38);   // rules, the LED's off ring
constexpr uint16_t kInk     = rgb(0xC8, 0xC8, 0xC8);
constexpr uint16_t kDim     = rgb(0x8A, 0x8A, 0x8A);
constexpr uint16_t kFaint   = rgb(0x6A, 0x6A, 0x72);
constexpr uint16_t kLive    = rgb(0x5D, 0xDC, 0x7A);
constexpr uint16_t kWarm    = rgb(0xE0, 0xA9, 0x4E);
constexpr uint16_t kDial    = rgb(0x7F, 0xD4, 0xFF);
constexpr uint16_t kBusy    = rgb(0xEF, 0x8B, 0x5A);
constexpr uint16_t kRisk    = rgb(0xE0, 0x6C, 0x6C);
constexpr uint16_t kYellow  = rgb(0xFF, 0xD3, 0x5C);
constexpr uint16_t kStruct  = rgb(0x4C, 0xE0, 0xE0);
constexpr uint16_t kBar     = rgb(0x18, 0x2C, 0x78);   // row 1 of the header
constexpr uint16_t kBand    = rgb(0x0E, 0x1A, 0x48);   // row 2, the status glyphs
constexpr uint16_t kTrack   = rgb(0x4C, 0x7F, 0x99);   // dial at 60%, the dot's rail
constexpr uint16_t kWhite   = rgb(0xFF, 0xFF, 0xFF);
} // namespace tok

// unpack: a packed colour back to 8 bits a channel, the low bits filled from
// the high ones, so white comes back as 255 and not 248.
struct Rgb8 { uint8_t r, g, b; };
inline Rgb8 unpack(uint16_t c) {
    const uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return { static_cast<uint8_t>((r << 3) | (r >> 2)), static_cast<uint8_t>((g << 2) | (g >> 4)),
             static_cast<uint8_t>((b << 3) | (b >> 2)) };
}

// mix: from a toward b, k of n of the way: a at k = 0, b at k = n. The
// header's fade is mix(text, bar, k, 8).
inline uint16_t mix(uint16_t a, uint16_t b, int k, int n) {
    if (k <= 0) return a;
    if (k >= n) return b;
    const Rgb8 x = unpack(a), y = unpack(b);
    auto ch = [&](int p, int q) { return static_cast<uint8_t>((p * (n - k) + q * k) / n); };
    return rgb(ch(x.r, y.r), ch(x.g, y.g), ch(x.b, y.b));
}

// scale: a colour at num/den of its brightness (an LED's glow edge, the
// dot's tail).
inline uint16_t scale(uint16_t c, int num, int den) {
    const Rgb8 x = unpack(c);
    return rgb(static_cast<uint8_t>(x.r * num / den), static_cast<uint8_t>(x.g * num / den),
               static_cast<uint8_t>(x.b * num / den));
}

// isqrt: the floor of the square root of v.
inline uint32_t isqrt(uint32_t v) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

// glassLevel: one channel of a strip pixel, as the lights plugin sent it at
// pct percent, as it should look on glass. Drawn as it stands, the shipped
// 10% is a near black a person reads as "off". So the channel is first
// taken back to the effect's own level (v x 100 / pct, which keeps a
// colour's hue at any brightness: orange at 100% stays orange rather than
// clipping to yellow), then drawn at a brightness that follows the square
// root of pct up to 30% and is full from there, roughly how a dimmed LED
// reads to the eye: 147 of 255 at 10%, 46 at 1%. Dark stays dark.
inline uint8_t glassLevel(uint8_t v, uint8_t pct) {
    if (!v || !pct) return 0;
    uint32_t level = static_cast<uint32_t>(v) * 100u / pct;           // the effect's 0-255
    if (level > 255u) level = 255u;                                   // shade()'s rounding up
    // 2168 is the least k with isqrt(30 k) = 255.
    uint32_t bright = pct >= 30 ? 255u : isqrt(static_cast<uint32_t>(pct) * 2168u);
    return static_cast<uint8_t>(level * bright / 255u);
}

// ---------------------------------------------------------------------------
// Rectangles and the canvas
// ---------------------------------------------------------------------------
struct Rect {
    int16_t x = 0, y = 0, w = 0, h = 0;
};

inline bool empty(const Rect& r) { return r.w <= 0 || r.h <= 0; }

inline bool same(const Rect& a, const Rect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

// contains: b lies wholly inside a.
inline bool contains(const Rect& a, const Rect& b) {
    return b.x >= a.x && b.y >= a.y && b.x + b.w <= a.x + a.w && b.y + b.h <= a.y + a.h;
}

inline Rect unite(const Rect& a, const Rect& b) {
    if (empty(a)) return b;
    if (empty(b)) return a;
    int16_t x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
    int16_t x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int16_t y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return { x0, y0, static_cast<int16_t>(x1 - x0), static_cast<int16_t>(y1 - y0) };
}

// clip: r cut to the w x h canvas, or empty.
inline Rect clip(Rect r, uint16_t w, uint16_t h) {
    if (r.x < 0) { r.w = static_cast<int16_t>(r.w + r.x); r.x = 0; }
    if (r.y < 0) { r.h = static_cast<int16_t>(r.h + r.y); r.y = 0; }
    if (r.x + r.w > static_cast<int>(w)) r.w = static_cast<int16_t>(w - r.x);
    if (r.y + r.h > static_cast<int>(h)) r.h = static_cast<int16_t>(h - r.y);
    if (r.w < 0 || r.h < 0) r.w = r.h = 0;
    return r;
}

inline Rect R(int x, int y, int w, int h) {
    return { static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w < 0 ? 0 : w),
             static_cast<int16_t>(h < 0 ? 0 : h) };
}

struct Canvas {
    uint16_t* px = nullptr;
    uint16_t  w  = 0;
    uint16_t  h  = 0;
};

inline void fill(Canvas& c, Rect r, uint16_t col) {
    r = clip(r, c.w, c.h);
    for (int y = r.y; y < r.y + r.h; ++y) {
        uint16_t* row = c.px + static_cast<size_t>(y) * c.w;
        for (int x = r.x; x < r.x + r.w; ++x) row[x] = col;
    }
}

inline void dot(Canvas& c, int x, int y, uint16_t col) {
    if (x >= 0 && y >= 0 && x < c.w && y < c.h) c.px[static_cast<size_t>(y) * c.w + x] = col;
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------
constexpr int kSmallW = 8,  kSmallH = 16;
constexpr int kBigW   = 16, kBigH   = 32;

// glyph: the next glyph's index in panel_font.h, advancing s past it.
// Printable ASCII is itself; the micro sign in UTF-8 (C2 B5) is its own
// glyph, because the board's own name starts with one; anything else is a
// '?', and a multi-byte UTF-8 character is one '?' rather than several.
inline uint8_t glyph(const char*& s) {
    uint8_t b = static_cast<uint8_t>(*s++);
    if (b >= 0x20 && b < 0x7F) return static_cast<uint8_t>(b - 0x20);
    if (b == 0xC2 && static_cast<uint8_t>(*s) == 0xB5) { ++s; return panelfont::kMicro; }
    if (b >= 0xC0)
        while ((static_cast<uint8_t>(*s) & 0xC0) == 0x80) ++s;
    return static_cast<uint8_t>('?' - 0x20);
}

// glyphs: how many glyphs text is, as glyph() reads it.
inline int glyphs(const char* s) {
    int n = 0;
    while (*s) { glyph(s); ++n; }
    return n;
}

inline int textWidth(const char* s, bool big) { return glyphs(s) * (big ? kBigW : kSmallW); }

// drawGlyph: one cell at x, y, fg on bg, clipped to the canvas and to the
// columns before xmax.
inline void drawGlyph(Canvas& c, int x, int y, uint8_t g, uint16_t fg, uint16_t bg, bool big, int xmax) {
    const int gw = big ? kBigW : kSmallW, gh = big ? kBigH : kSmallH;
    const uint8_t* bits = big ? panelfont::kBig[g] : panelfont::kSmall[g];
    const int bpr = gw / 8;
    for (int row = 0; row < gh; ++row) {
        int py = y + row;
        if (py < 0 || py >= c.h) continue;
        uint16_t* line = c.px + static_cast<size_t>(py) * c.w;
        for (int col = 0; col < gw; ++col) {
            int px = x + col;
            if (px < 0 || px >= c.w || px >= xmax) continue;
            bool on = (bits[row * bpr + col / 8] >> (7 - col % 8)) & 1u;
            line[px] = on ? fg : bg;
        }
    }
}

// text: s at x, y, cut (never wrapped) at maxW pixels. The width drawn.
inline int text(Canvas& c, int x, int y, const char* s, uint16_t fg, uint16_t bg, bool big, int maxW) {
    const int gw = big ? kBigW : kSmallW;
    const int xmax = x + maxW;
    int at = x;
    while (*s && at + gw <= xmax) {
        drawGlyph(c, at, y, glyph(s), fg, bg, big, xmax);
        at += gw;
    }
    return at - x;
}

enum Align : uint8_t { LEFT, RIGHT, CENTRE };

// line: one line of text in a box, aligned, cut at the box's width.
inline void line(Canvas& c, const Rect& r, int y, const char* s, uint16_t fg, uint16_t bg, bool big,
                 uint8_t align) {
    int w = textWidth(s, big);
    if (w > r.w) w = r.w - r.w % (big ? kBigW : kSmallW);
    int x = align == RIGHT ? r.x + r.w - w : align == CENTRE ? r.x + (r.w - w) / 2 : r.x;
    text(c, x, y, s, fg, bg, big, w);
}

// field: a figure in its box. The box is cleared to bg first, so a shorter
// string leaves nothing of a longer one behind. A box tall enough for two
// lines takes a string too wide for one on two, broken at the last space
// that fits.
inline void field(Canvas& c, const Rect& r, const char* s, uint16_t fg, uint16_t bg, bool big, uint8_t align) {
    fill(c, r, bg);
    if (empty(r)) return;
    const int gw = big ? kBigW : kSmallW, gh = big ? kBigH : kSmallH;
    const int fit = r.w / gw;
    if (fit > 0 && r.h >= 2 * gh && glyphs(s) > fit) {
        // The last space whose line before it still fits the box.
        const char* p = s;
        const char* cut = nullptr;
        for (int g = 0; *p && g <= fit; ++g) {
            if (*p == ' ') cut = p;
            glyph(p);
        }
        if (cut) {
            char first[64];
            size_t n = static_cast<size_t>(cut - s);
            if (n > sizeof(first) - 1) n = sizeof(first) - 1;
            for (size_t i = 0; i < n; ++i) first[i] = s[i];
            first[n] = '\0';
            int y = r.y + (r.h - 2 * gh) / 2;
            line(c, r, y, first, fg, bg, big, align);
            line(c, r, y + gh, cut + 1, fg, bg, big, align);
            return;
        }
    }
    line(c, r, r.y + (r.h - gh) / 2, s, fg, bg, big, align);
}

// cutWords: s cut to at most fit glyphs for a line that must not end
// mid-word: at the last space at or past half of fit when there is one
// ("The Rusty Antenna" in 11 is "The Rusty"), otherwise at fit glyphs.
// A space right after the fit glyphs counts, since that cut is between two
// words already. Trailing spaces go. out holds the result.
inline void cutWords(const char* s, int fit, char* out, size_t n) {
    if (!n) return;
    out[0] = '\0';
    if (fit <= 0) return;
    const char* p = s;
    const char* end = s;              // where fit glyphs end
    const char* cut = nullptr;        // the last acceptable space
    int g = 0;
    while (*p && g < fit) {
        if (*p == ' ' && g >= fit / 2) cut = p;
        glyph(p);
        ++g;
        end = p;
    }
    if (*p) {                                          // it did not fit
        if (*p == ' ') cut = p;
        if (cut) end = cut;
    }
    size_t len = static_cast<size_t>(end - s);
    while (len && s[len - 1] == ' ') --len;
    if (len > n - 1) len = n - 1;
    for (size_t i = 0; i < len; ++i) out[i] = s[i];
    out[len] = '\0';
}

// ---------------------------------------------------------------------------
// Figures as the panel writes them
// ---------------------------------------------------------------------------
// fmtUptime: at most 7 glyphs. 14m under an hour, 3h 14m under a day,
// 12d 3h under a hundred days, 123d after.
inline void fmtUptime(uint32_t secs, char* out, size_t n) {
    const unsigned m = secs / 60u, h = secs / 3600u, d = secs / 86400u;
    if (secs < 3600u)       snprintf(out, n, "%um", m);
    else if (secs < 86400u) snprintf(out, n, "%uh %um", h, m % 60u);
    else if (d < 100u)      snprintf(out, n, "%ud %uh", d, h % 24u);
    else                    snprintf(out, n, "%ud", d);
}

// fmtFree: a card's free space in at most 6 glyphs: 512 MB, 7.4 GB, 29 GB.
inline void fmtFree(uint32_t freeKB, char* out, size_t n) {
    const uint32_t mb = freeKB / 1024u;
    if (mb >= 10u * 1024u) snprintf(out, n, "%u GB", static_cast<unsigned>(mb / 1024u));
    else if (mb >= 1024u)  snprintf(out, n, "%u.%u GB", static_cast<unsigned>(mb / 1024u),
                                    static_cast<unsigned>((mb % 1024u) * 10u / 1024u));
    else                   snprintf(out, n, "%u MB", static_cast<unsigned>(mb));
}

// fmtOnFor: how long a caller has been on, at most 3 glyphs: 4m, 51m, 2h, 1d.
inline void fmtOnFor(uint32_t ms, char* out, size_t n) {
    const uint32_t m = ms / 60000u;
    if (m < 60u) { snprintf(out, n, "%um", static_cast<unsigned>(m)); return; }
    const uint32_t h = m / 60u;
    if (h < 24u) { snprintf(out, n, "%uh", static_cast<unsigned>(h)); return; }
    uint32_t d = h / 24u;
    if (d > 99u) d = 99u;
    snprintf(out, n, "%ud", static_cast<unsigned>(d));
}

// ---------------------------------------------------------------------------
// Icons: 16 x 16, one bit a pixel, a 16-bit word a row with the leftmost
// pixel in the top bit. Rasterised from geometry with a 2 px pen (the
// report's icons.py), so they match the mock-ups pixel for pixel.
// ---------------------------------------------------------------------------
using Icon = uint16_t[16];
constexpr Icon kIconCallers = { 0x0000, 0x0000, 0x03C0, 0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x03C0,
                                0x0180, 0x0FF0, 0x1E78, 0x381C, 0x700E, 0x6006, 0x6006, 0xC003 };
constexpr Icon kIconLogin   = { 0x0038, 0x007C, 0x003C, 0x000C, 0x020C, 0x030C, 0x018C, 0x7FCC,
                                0x7FCC, 0x018C, 0x030C, 0x020C, 0x000C, 0x003C, 0x007C, 0x0038 };
constexpr Icon kIconLogoff  = { 0x1C00, 0x3E00, 0x3C00, 0x3000, 0x3010, 0x3018, 0x300C, 0x33FE,
                                0x33FE, 0x300C, 0x3018, 0x3010, 0x3000, 0x3C00, 0x3E00, 0x1C00 };
constexpr Icon kIconBell    = { 0x0000, 0x0000, 0x03C0, 0x0FF0, 0x1C38, 0x1818, 0x1818, 0x381C,
                                0x381C, 0x381C, 0x300C, 0x3FFC, 0x7FFE, 0x0180, 0x0180, 0x0180 };
constexpr Icon kIconQuiet   = { 0x0000, 0x0000, 0x07E0, 0x1FF8, 0x1818, 0x300C, 0x300C, 0x300C,
                                0x300C, 0x300C, 0x300C, 0x1818, 0x1FF8, 0x07E0, 0x0000, 0x0000 };
constexpr Icon kIconChip    = { 0x0000, 0x0660, 0x0660, 0x07E0, 0x0FF0, 0x7FFE, 0x7C3E, 0x1C38,
                                0x1C38, 0x7C3E, 0x7FFE, 0x0FF0, 0x07E0, 0x0660, 0x0660, 0x0000 };
constexpr Icon kIconHandset = { 0x0000, 0x0600, 0x0E00, 0x1C00, 0x3C00, 0x7E00, 0x6700, 0x0380,
                                0x01C0, 0x00E6, 0x007E, 0x003C, 0x0038, 0x0070, 0x0060, 0x0000 };
// The big glass's (480 x 320, internal/tty-ux-panel-mf35-2026-09-26.md): the
// traffic arrows, a camera, an hourglass, and the card and the clock the
// Waveshare's revision 0 named and never drew.
constexpr Icon kIconTraffic = { 0x0000, 0x0C00, 0x1E00, 0x3F00, 0x6D80, 0x0C00, 0x0C0C, 0x0C0C,
                                0x0C0C, 0x0C0C, 0x000C, 0x00DB, 0x007E, 0x003C, 0x0018, 0x0000 };
constexpr Icon kIconCamera  = { 0x0000, 0x0000, 0x07E0, 0x7FFE, 0x6006, 0x63C6, 0x6666, 0x6C36,
                                0x6C36, 0x6666, 0x63C6, 0x6006, 0x7FFE, 0x0000, 0x0000, 0x0000 };
constexpr Icon kIconHourglass = { 0x0000, 0x3FFC, 0x1818, 0x1818, 0x0C30, 0x0660, 0x03C0, 0x0180,
                                  0x0180, 0x03C0, 0x0660, 0x0DB0, 0x1BD8, 0x1FF8, 0x3FFC, 0x0000 };
constexpr Icon kIconCard    = { 0x0000, 0x3FE0, 0x3030, 0x3018, 0x300C, 0x354C, 0x354C, 0x300C,
                                0x300C, 0x300C, 0x300C, 0x300C, 0x300C, 0x3FFC, 0x3FFC, 0x0000 };
constexpr Icon kIconClock   = { 0x0000, 0x07E0, 0x1C38, 0x300C, 0x318C, 0x6186, 0x6186, 0x61F6,
                                0x61F6, 0x6006, 0x300C, 0x300C, 0x1C38, 0x07E0, 0x0000, 0x0000 };

// bits: paint the set bits of w x h rows (top bit leftmost) in fg, leaving
// the rest as the caller cleared it.
inline void bits(Canvas& c, int x, int y, const uint16_t* rows, int w, int h, uint16_t fg) {
    for (int r = 0; r < h; ++r)
        for (int b = 0; b < w; ++b)
            if ((rows[r] >> (15 - b)) & 1u) dot(c, x + b, y + r, fg);
}

inline void icon(Canvas& c, int x, int y, const Icon& ic, uint16_t fg) { bits(c, x, y, ic, 16, 16, fg); }

// ---------------------------------------------------------------------------
// The status row's glyphs: silhouettes, packed left on the band in this
// order, each only while its condition holds (the SD card always).
// ---------------------------------------------------------------------------
struct Glyph {
    uint8_t  w, h;
    uint16_t rows[12];
};
constexpr int kGlyphGap = 3;

constexpr Glyph kGlyphSd     = { 9, 12, { 0xFE00, 0xFF00, 0xD580, 0xD580, 0xD580, 0xFF80, 0xFF80, 0xFF80,
                                          0xFF80, 0xFF80, 0xFF80, 0xFF80 } };   // filled, contacts cut
constexpr Glyph kGlyphSdNone = { 9, 12, { 0xFE00, 0x8100, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080,
                                          0x8080, 0x8080, 0x8080, 0xFF80 } };   // hollow: no card
constexpr Glyph kGlyphBell   = { 9, 11, { 0x0800, 0x1C00, 0x3E00, 0x3E00, 0x3E00, 0x7F00, 0x7F00, 0xFF80,
                                          0xFF80, 0x0000, 0x1C00 } };
constexpr Glyph kGlyphMail   = { 11, 8, { 0xFFE0, 0xBFA0, 0xDF60, 0xEEE0, 0xF5E0, 0xFFE0, 0xFFE0, 0xFFE0 } };
constexpr Glyph kGlyphUpload = { 9, 10, { 0x0800, 0x1C00, 0x3E00, 0x7F00, 0x1C00, 0x1C00, 0x1C00, 0x8080,
                                          0x8080, 0xFF80 } };
constexpr Glyph kGlyphLock   = { 9, 11, { 0x1C00, 0x2200, 0x2200, 0x2000, 0x2000, 0xFF80, 0xFF80, 0xF780,
                                          0xF780, 0xFF80, 0xFF80 } };           // open padlock
constexpr Glyph kGlyphTower  = { 9, 10, { 0x0800, 0x2A00, 0x4900, 0x4900, 0x2A00, 0x0800, 0x0800, 0x1C00,
                                          0x3E00, 0x7F00 } };
constexpr Glyph kGlyphStaff  = { 8, 10, { 0x1800, 0x3C00, 0x3C00, 0x1800, 0x0000, 0x7E00, 0xFF00, 0xFF00,
                                          0xFF00, 0xFF00 } };
constexpr Glyph kGlyphWarn   = { 9, 9,  { 0x0800, 0x1C00, 0x1400, 0x3600, 0x3600, 0x7700, 0x7F00, 0xF780,
                                          0xFF80 } };
constexpr Glyph kGlyphSlow   = { 7, 9,  { 0xFE00, 0x4400, 0x6C00, 0x3800, 0x1000, 0x2800, 0x4400, 0x7C00,
                                          0xFE00 } };

// The status row's state: what the panel read this pass, in packing order.
// Everything that decides a pixel of the row is in here, so two equal
// states draw the same row.
struct Status {
    enum Card : uint8_t { CARD_NONE, CARD_IN, CARD_ERROR };
    enum Listing : uint8_t { LIST_OFF, LIST_ONLINE, LIST_WAITING, LIST_TROUBLE };
    enum Staff : uint8_t { STAFF_NONE, STAFF_CO, STAFF_SYSOP };
    uint8_t card    = CARD_NONE;
    bool    ring    = false;   // a caller is ringing the sysop
    bool    mail    = false;   // the sysop has unread mail
    bool    upload  = false;   // an upload waits for approval
    bool    backup  = false;   // the backup window is open
    uint8_t listing = LIST_OFF;
    uint8_t staff   = STAFF_NONE;
    bool    warn    = false;   // the last restart was not a clean one
    bool    slow    = false;   // a slow pass in the last minute
};

enum Slot : uint8_t { G_SD, G_BELL, G_MAIL, G_UPLOAD, G_LOCK, G_TOWER, G_STAFF, G_WARN, G_SLOW, G_COUNT };

struct Packed {
    uint8_t         n = 0;
    uint8_t         which[G_COUNT] = {};
    const Glyph*    glyph[G_COUNT] = {};
    uint16_t        colour[G_COUNT] = {};
    int16_t         x[G_COUNT] = {};        // left edge, absolute
    int16_t         end = 0;                // one past the last glyph's right edge
};

// pack: the glyphs a status shows, left to right from x0, with their
// colours. The bell's slot is always one of them while ringing, whatever its
// blink phase, so the row does not shuffle at 2 Hz: the blink is drawn over
// it on its own.
inline Packed pack(const Status& s, int x0) {
    Packed p;
    auto add = [&](uint8_t which, const Glyph& g, uint16_t col) {
        p.which[p.n] = which;
        p.glyph[p.n] = &g;
        p.colour[p.n] = col;
        p.x[p.n] = static_cast<int16_t>(x0);
        x0 += g.w;
        p.end = static_cast<int16_t>(x0);
        x0 += kGlyphGap;
        ++p.n;
    };
    add(G_SD, s.card == Status::CARD_NONE ? kGlyphSdNone : kGlyphSd,
        s.card == Status::CARD_IN ? tok::kDial : s.card == Status::CARD_ERROR ? tok::kRisk : tok::kDim);
    if (s.ring)   add(G_BELL, kGlyphBell, tok::kBusy);
    if (s.mail)   add(G_MAIL, kGlyphMail, tok::kBusy);
    if (s.upload) add(G_UPLOAD, kGlyphUpload, tok::kWarm);
    if (s.backup) add(G_LOCK, kGlyphLock, tok::kWarm);
    if (s.listing != Status::LIST_OFF)
        add(G_TOWER, kGlyphTower, s.listing == Status::LIST_ONLINE ? tok::kLive
                                : s.listing == Status::LIST_WAITING ? tok::kWarm : tok::kRisk);
    if (s.staff != Status::STAFF_NONE)
        add(G_STAFF, kGlyphStaff, s.staff == Status::STAFF_SYSOP ? tok::kRisk : tok::kYellow);
    if (s.warn)   add(G_WARN, kGlyphWarn, tok::kRisk);
    if (s.slow)   add(G_SLOW, kGlyphSlow, tok::kWarm);
    return p;
}

// glyphBox: one packed glyph's box, vertically centred in a 16 px row at y.
inline Rect glyphBox(const Packed& p, uint8_t i, int y) {
    const Glyph& g = *p.glyph[i];
    return R(p.x[i], y + (16 - g.h) / 2, g.w, g.h);
}

inline void drawGlyphAt(Canvas& c, const Packed& p, uint8_t i, int y) {
    const Rect b = glyphBox(p, i, y);
    bits(c, b.x, b.y, p.glyph[i]->rows, p.glyph[i]->w, p.glyph[i]->h, p.colour[i]);
}

// ---------------------------------------------------------------------------
// The Wi-Fi antenna (revision 2): a ball 6 x 5 on a mast 2 x 11, 6 x 16 in
// all, filling from the bottom. Six bits a row, bit 5 the leftmost pixel.
// ---------------------------------------------------------------------------
constexpr uint8_t kAntenna[16] = { 0x1E, 0x3F, 0x3F, 0x3F, 0x1E,                 // the ball
                                   0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,           // the mast
                                   0x0C, 0x0C, 0x0C, 0x0C, 0x0C };
constexpr int kAntennaW = 6, kAntennaH = 16;

// signalFill: how many of the 16 rows are lit: 2.5 dB a row over -90 to
// -50, 0 at -90 and below, 16 at -50 and above. 0 when not joined.
inline int signalFill(int rssi) {
    if (!rssi) return 0;
    if (rssi < -90) rssi = -90;
    if (rssi > -50) rssi = -50;
    return (rssi + 90) * 16 / 40;
}

// signalColour: the fill's colour, SYS's and the strip's own thresholds:
// live at -67 and up, warm to -75, risk below.
inline uint16_t signalColour(int rssi) {
    return rssi >= -67 ? tok::kLive : rssi >= -75 ? tok::kWarm : tok::kRisk;
}

// drawSignal: the antenna at x, y on bg. Joined: rows 16 - h to 15 of the
// silhouette in colour and the rest in faint, the ghost of an empty gauge.
// Not joined: the whole silhouette in risk.
inline void drawSignal(Canvas& c, int x, int y, int h, uint16_t colour, bool joined, uint16_t bg) {
    fill(c, R(x, y, kAntennaW, kAntennaH), bg);
    for (int r = 0; r < kAntennaH; ++r) {
        const uint16_t col = !joined ? tok::kRisk : r >= kAntennaH - h ? colour : tok::kFaint;
        for (int b = 0; b < kAntennaW; ++b)
            if ((kAntenna[r] >> (kAntennaW - 1 - b)) & 1u) dot(c, x + b, y + r, col);
    }
}

// ---------------------------------------------------------------------------
// The strip as square LEDs in one row: a cell of min(16, w / n), the LED
// max(6, cell - 4) square in it (never as big as its cell, so two never
// touch), the row centred. Lit: the square in the colour at a third, the
// glow edge, round a core one pixel in. Off: a ring in the rule's grey round
// a near-black fill, one pixel further in, so a lit LED swells.
// ---------------------------------------------------------------------------
struct Led {
    Rect cell;         // what is cleared and sent
    Rect led;          // the LED itself, its glow edge included
};

inline Led ledAt(const Rect& box, uint8_t i, uint8_t n) {
    Led out;
    if (!n || i >= n || empty(box)) return out;
    int cell = box.w / n;
    if (cell > 16) cell = 16;
    if (cell < 2) return out;
    int side = cell - 4;
    if (side < 6) side = 6;
    if (side > cell - 1) side = cell - 1;
    const int x0 = box.x + (box.w - cell * n) / 2;
    const int cx = x0 + i * cell;
    out.cell = R(cx, box.y, cell, box.h);
    out.led  = R(cx + (cell - side) / 2, box.y + (box.h - side) / 2, side, side);
    return out;
}

// drawLed: one LED in its cell, lit in col, or off when col is 0.
inline void drawLed(Canvas& c, const Led& l, uint16_t col) {
    fill(c, l.cell, tok::kBg);
    const Rect& s = l.led;
    if (col) {
        fill(c, s, scale(col, 1, 3));
        fill(c, R(s.x + 1, s.y + 1, s.w - 2, s.h - 2), col);
    } else {
        fill(c, R(s.x + 1, s.y + 1, s.w - 2, s.h - 2), tok::kRule);
        fill(c, R(s.x + 2, s.y + 2, s.w - 4, s.h - 4), tok::kSurface);
    }
}

// ---------------------------------------------------------------------------
// Orientation: which way the glass is turned, named by where the USB plug is
// as a person faces the screen, because that is what somebody holding the
// board can see. Up is portrait as Waveshare's demo draws it; left and right
// are landscape; down is portrait upside down.
//
// The ST7789 turns the picture with MADCTL: MV exchanges rows and columns,
// MX and MY run the columns and rows backwards. The platform takes a
// rotation (0, 90, 180, 270) and the glass's mirror and sets the three bits
// from them (platform_esp32.cpp, lcdBegin): rotation 0 is MV 0 MX 0 MY 0,
// 90 is MV MX, 180 is MX MY, 270 is MV MY, and the mirror then turns over MX
// without MV or MY with it. On this glass (mirror yes) that gives exactly
// the four Waveshare's demo sets for LVGL's four rotations (its
// LVGL_Driver.c, example_lvgl_port_update_callback: swap_xy false with
// mirror (true, false); true with (true, true); false with (false, true);
// true with (false, false)).
//
// Which rotation is which plug position follows from the demo's portrait
// being upright with the plug at the top (MV 0 MX 1 MY 0): there a column
// further into the controller's memory is further to the viewer's left and
// a row further in is further from the plug. MV 1 with no mirror puts the
// picture's x along the rows, away from the plug, and its y along the
// columns, to the viewer's left of the plug-up view: upright with the plug
// on the left. That is rotation 270 on a mirrored glass and 90 on one that
// is not; the two landscape modes use no mirror or both, so the result does
// not hang on which memory axis a mirror bit acts on once MV is set.
//
// The offsets. The 172-pixel glass sits in the middle of the controller's
// 240 columns and fills its 320 rows: Waveshare's demo draws portrait at
// Offset_X 34, Offset_Y 0 (its LCD_Driver/ST7789.h), and espp's board
// support for this module draws landscape 320 x 172 at lcd_offset_x 0,
// lcd_offset_y 34 (components/ws-s3-lcd-1-47, ws-s3-lcd-1-47.hpp). So the
// offset follows the 172-pixel axis onto whichever axis the picture's x or
// y is, and a mirror moves it to the other end of that axis, which for a
// centred glass is the same 34 (240 - 172 - 34). The settings give the
// offsets as they apply with the plug up, the demo's portrait, and every
// other orientation is worked from those.
// ---------------------------------------------------------------------------
enum Orient : uint8_t { ORIENT_UP, ORIENT_LEFT, ORIENT_RIGHT, ORIENT_DOWN, ORIENTS };

// The words, in Orient's order: CONFIG's choices and what system.cfg holds.
constexpr char kOrientWords[] = "up|left|right|down";

inline const char* orientWord(uint8_t o) {
    static const char* const kWords[] = { "up", "left", "right", "down" };
    return o < ORIENTS ? kWords[o] : "up";
}

// orientFromWord: one of the four words, in any case. False for anything
// else, and out is left as it was.
inline bool orientFromWord(const char* v, uint8_t& out) {
    for (uint8_t o = 0; o < ORIENTS; ++o) {
        const char* w = orientWord(o);
        const char* p = v;
        while (*w && *p && ((*p | 0x20) == *w)) { ++w; ++p; }
        if (!*w && !*p) { out = o; return true; }
    }
    return false;
}

constexpr int kRamCols = 240, kRamRows = 320;   // the ST7789's memory, unturned (the default)

// Scan: how the platform draws one orientation: the rotation it is given,
// the MADCTL bits that makes, the picture's size and the gaps the window is
// moved by (esp_lcd_panel_set_gap: the x gap is added to every column
// address the picture's x makes, the y gap to every row address).
struct Scan {
    uint16_t rotation = 0;
    bool     mv = false, mx = false, my = false;
    uint16_t w = 0, h = 0;
    uint16_t xgap = 0, ygap = 0;
};

// scanFor: one orientation of a glass w x h whose gaps with the plug up are
// xoff, yoff, wired mirrored or not, on a controller whose memory is ramCols
// x ramRows unturned (the ST7789's 240 x 320 unless a profile says otherwise:
// the ILI9488's is 320 x 480, board.h BBS_LCD_RAM_SHORT/LONG).
inline Scan scanFor(uint8_t orient, bool mirror, uint16_t w, uint16_t h, uint16_t xoff, uint16_t yoff,
                    int ramCols = kRamCols, int ramRows = kRamRows) {
    Scan s;
    switch (orient) {
        case ORIENT_LEFT:  s.rotation = mirror ? 270 : 90;  break;
        case ORIENT_RIGHT: s.rotation = mirror ? 90 : 270;  break;
        case ORIENT_DOWN:  s.rotation = 180;                break;
        default:           s.rotation = 0;                  break;
    }
    // The platform's table, then the mirror.
    switch (s.rotation) {
        case 90:  s.mv = true;  s.mx = true;  break;
        case 180: s.mx = true;  s.my = true;  break;
        case 270: s.mv = true;  s.my = true;  break;
        default:  break;
    }
    if (mirror) {
        if (s.mv) s.my = !s.my;
        else      s.mx = !s.mx;
    }
    // Where the glass is in memory, from its gaps with the plug up (MV 0,
    // MX the mirror, MY 0).
    auto clamp0 = [](int v) { return static_cast<uint16_t>(v < 0 ? 0 : v); };
    const int col = mirror ? ramCols - w - xoff : xoff;
    const int row = yoff;
    // A gap along an axis run backwards is measured from its other end.
    const uint16_t colGap = clamp0(s.mx ? ramCols - w - col : col);
    const uint16_t rowGap = clamp0(s.my ? ramRows - h - row : row);
    if (s.mv) { s.w = h; s.h = w; s.xgap = rowGap; s.ygap = colGap; }
    else      { s.w = w; s.h = h; s.xgap = colGap; s.ygap = rowGap; }
    return s;
}

// ---------------------------------------------------------------------------
// Layout: where each figure goes for a panel of w x h, portrait (the
// Waveshare stick's 172 x 320, hanging from a port) or landscape.
// ---------------------------------------------------------------------------
constexpr uint8_t kListMax   = 10;    // list slots on a portrait panel
constexpr int     kPitch     = 20;    // a list row: 16 of glyph, 4 of air

enum Field : uint8_t { F_SLOT, F_CLOCK, F_GLYPHS, F_ANT, F_HEAD, F_SYS, F_LIST,
                       F_COUNT = F_LIST + kListMax };

struct Layout {
    bool    land = false;
    Rect    bar, band, track;         // the header's two rows and the rail
    Rect    slot, glyphs, ant, clock; // what sits on them
    Rect    headIcon, head;           // "Callers 4/11" and its icon
    Rect    list[kListMax];           // each slot's 16 px row
    uint8_t slots       = 0;          // how many there are
    uint8_t callerSlots = 0;          // how many can hold callers: all in portrait,
                                      // the left column's in landscape
    bool    gaps = false;             // portrait: each slot owns the 4 px above it,
                                      // where the rule under the callers goes
    Rect    colRule;                  // landscape: between the two columns
    Rect    rule1, rule2;             // above and below the system row
    Rect    sys;                      // the system row
    int16_t sysAt[3] = {};            // where its figures start: heap, calls, peak
    uint8_t sysFigs = 0;              // 2 portrait, 3 landscape
    Rect    leds;                     // the strip
};

inline Layout layout(uint16_t w, uint16_t h) {
    Layout L;
    L.land  = w >= h;
    L.bar   = R(0, 0, w, 22);
    L.band  = R(0, 22, w, 20);
    L.track = R(0, 42, w, 1);
    L.slot  = R(2, 3, (w - 4) / kSmallW * kSmallW, kSmallH);
    L.clock = R(w - 44, 24, 5 * kSmallW, kSmallH);
    L.ant   = R(L.clock.x - 10, 24, kAntennaW, kAntennaH);
    int gw  = L.ant.x - 4 - 4;                                  // the glyphs end short of the antenna
    L.glyphs = R(4, 24, gw < 110 ? gw : 110, 16);
    L.headIcon = R(4, 48, 16, 16);
    if (!L.land) {
        // Portrait: the ten slots from 68, then the rules, the system row
        // and the LEDs anchored to the foot of the glass.
        L.rule2 = R(4, h - 28, w - 8, 1);
        L.leds  = R(4, h - 22, w - 8, 16);
        L.sys   = R(4, h - 52, w - 8, 16);
        L.rule1 = R(4, h - 54, w - 8, 1);
        L.head  = R(24, 48, w - 28, 16);
        int n = 0;
        while (n < kListMax && 68 + kPitch * n + 16 <= L.rule1.y) {
            L.list[n] = R(4, 68 + kPitch * n, w - 8, 16);
            ++n;
        }
        L.slots = L.callerSlots = static_cast<uint8_t>(n);
        L.gaps  = true;
        L.sysAt[0] = 4;
        L.sysAt[1] = 76;
        L.sysFigs  = 2;
    } else {
        // Landscape: two columns under the heading, callers on the left
        // and the recent events on the right, then a full-width system row
        // and the LEDs.
        L.rule1 = R(4, h - 46, w - 8, 1);
        L.sys   = R(4, h - 42, w - 8, 16);
        L.rule2 = R(4, h - 22, w - 8, 1);
        L.leds  = R(10, h - 18, w - 20, 16);
        const int mid = w / 2;
        L.head    = R(24, 48, mid - 4 - 24, 16);
        L.colRule = R(mid, 48, 1, L.rule1.y - 2 - 48);
        int n = 0;
        while (n < 3 && 68 + kPitch * n + 16 <= L.rule1.y - 2) {
            L.list[n] = R(4, 68 + kPitch * n, mid - 8, 16);
            ++n;
        }
        L.callerSlots = static_cast<uint8_t>(n);
        for (int j = 0; j < 4 && n < kListMax && 48 + kPitch * j + 16 <= L.rule1.y - 2; ++j, ++n)
            L.list[n] = R(mid + 6, 48 + kPitch * j, w - 4 - (mid + 6), 16);
        L.slots = static_cast<uint8_t>(n);
        L.sysAt[0] = 4;
        L.sysAt[1] = 96;
        L.sysAt[2] = 208;
        L.sysFigs  = 3;
    }
    Rect* all[] = { &L.bar, &L.band, &L.track, &L.slot, &L.glyphs, &L.ant, &L.clock, &L.headIcon, &L.head,
                    &L.colRule, &L.rule1, &L.rule2, &L.sys, &L.leds };
    for (Rect* r : all) *r = clip(*r, w, h);
    for (Rect& r : L.list) r = clip(r, w, h);
    return L;
}

// listBox: slot k as it is cleared and sent: its row, and in portrait the
// four rows of air above it, where the rule under the callers is drawn.
inline Rect listBox(const Layout& L, uint8_t k) {
    const Rect& r = L.list[k];
    if (empty(r) || !L.gaps) return r;
    return R(r.x, r.y - 4, r.w, r.h + 4);
}

// fieldBox: what a field clears and sends.
inline Rect fieldBox(const Layout& L, uint8_t f) {
    switch (f) {
        case F_SLOT:   return L.slot;
        case F_CLOCK:  return L.clock;
        case F_GLYPHS: return L.glyphs;
        case F_ANT:    return L.ant;
        case F_HEAD:   return unite(L.headIcon, L.head);
        case F_SYS:    return L.sys;
        default:       return f >= F_LIST && f < F_COUNT ? listBox(L, static_cast<uint8_t>(f - F_LIST)) : Rect();
    }
}

// ---------------------------------------------------------------------------
// The two lists and their slots (revision 2). The callers take min(on, the
// slots that can hold them); past that the first all-but-one are named and
// the last slot says "+N more". In portrait the recent events take the
// slots the callers left, and a rule divides the two when both are there;
// in landscape they have the right column to themselves.
// ---------------------------------------------------------------------------
struct Alloc {
    uint8_t callers = 0;    // slots the callers take, the "+N more" one included
    uint8_t names   = 0;    // callers named
    uint8_t more    = 0;    // callers past the names, in slot `names`; 0 none
    uint8_t recent0 = 0;    // the recent list's first slot
    uint8_t recentN = 0;    // and how many it has
    bool    rule    = false;// the rule above slot `callers` (portrait)
};

inline Alloc allocate(const Layout& L, uint8_t on) {
    Alloc a;
    const uint8_t cap = L.callerSlots;
    a.callers = on < cap ? on : cap;
    a.names   = a.callers;
    if (on > cap && cap) {
        a.names = static_cast<uint8_t>(cap - 1);
        a.more  = static_cast<uint8_t>(on - a.names);
    }
    if (L.land) {
        a.recent0 = cap;
        a.recentN = static_cast<uint8_t>(L.slots - cap);
    } else {
        a.recent0 = a.callers;
        a.recentN = static_cast<uint8_t>(L.slots - a.callers);
        a.rule    = a.callers > 0 && a.callers < L.slots;
    }
    return a;
}

// ---------------------------------------------------------------------------
// Dirty: the rectangles still to be sent, and the one being sent a band at a
// time. Fixed size: a queue that fills becomes one rectangle around all of
// it, which sends more than it needs to and loses nothing.
// ---------------------------------------------------------------------------
struct Dirty {
    static constexpr uint8_t kMax = 24;
    Rect    q[kMax];
    uint8_t n = 0;
    Rect    cur;                   // the part of the current rectangle not yet sent

    void clear() { n = 0; cur = Rect(); }
    bool idle() const { return !n && empty(cur); }

    void add(const Rect& r) {
        if (empty(r)) return;
        for (uint8_t i = 0; i < n; ++i)
            if (contains(q[i], r)) return;                 // already going
        uint8_t k = 0;                                     // drop what r covers
        for (uint8_t i = 0; i < n; ++i)
            if (!contains(r, q[i])) q[k++] = q[i];
        n = k;
        if (n == kMax) {
            Rect all = r;
            for (uint8_t i = 0; i < n; ++i) all = unite(all, q[i]);
            q[0] = all;
            n = 1;
            return;
        }
        q[n++] = r;
    }

    // next: the next band to send, as many whole rows of the current
    // rectangle as fit bandPixels (at least one), or false when there is
    // nothing left. A rectangle wider than a band is sent a row at a time
    // in pieces of bandPixels.
    bool next(uint32_t bandPixels, Rect& out) {
        if (!bandPixels) return false;
        if (empty(cur)) {
            if (!n) return false;
            cur = q[0];
            for (uint8_t i = 1; i < n; ++i) q[i - 1] = q[i];
            --n;
        }
        if (static_cast<uint32_t>(cur.w) > bandPixels) {
            out = { cur.x, cur.y, static_cast<int16_t>(bandPixels), 1 };
            cur.x = static_cast<int16_t>(cur.x + bandPixels);
            cur.w = static_cast<int16_t>(cur.w - bandPixels);
            // the rest of this row, then the rows below it at full width,
            // go back on the queue: first the remainder, then the rows
            Rect rest = cur;
            Rect below = { static_cast<int16_t>(out.x), static_cast<int16_t>(cur.y + 1),
                           static_cast<int16_t>(out.w + rest.w), static_cast<int16_t>(cur.h - 1) };
            cur = { rest.x, rest.y, rest.w, 1 };
            if (!empty(below)) add(below);
            return true;
        }
        uint32_t rows = bandPixels / static_cast<uint32_t>(cur.w);
        if (rows > static_cast<uint32_t>(cur.h)) rows = static_cast<uint32_t>(cur.h);
        out = { cur.x, cur.y, cur.w, static_cast<int16_t>(rows) };
        cur.y = static_cast<int16_t>(cur.y + rows);
        cur.h = static_cast<int16_t>(cur.h - rows);
        if (cur.h <= 0) cur = Rect();
        return true;
    }
};

// area: w x h as a count of pixels.
inline int32_t area(const Rect& r) { return empty(r) ? 0 : static_cast<int32_t>(r.w) * r.h; }

// DirtyCheap: the same queue, but a full queue merges only the pair whose
// union grows least, rather than uniting all of it. On a 480 x 320 glass the
// union of the dot, a caller row and the LED row is most of the glass: 0.8 s
// of bands during which nothing else moves (internal/tty-ux-panel-mf35-
// 2026-09-26.md, "Motion"). Kept apart from Dirty so the Waveshare's queue
// is what shipped.
struct DirtyCheap : Dirty {
    void add(const Rect& r) {
        if (empty(r)) return;
        for (uint8_t i = 0; i < n; ++i)
            if (contains(q[i], r)) return;
        uint8_t k = 0;
        for (uint8_t i = 0; i < n; ++i)
            if (!contains(r, q[i])) q[k++] = q[i];
        n = k;
        if (n == kMax) {
            // The pair (i, j), r among them as index n, whose union adds the
            // fewest pixels over the two it replaces.
            Rect all[kMax + 1];
            for (uint8_t i = 0; i < n; ++i) all[i] = q[i];
            all[n] = r;
            const uint8_t m = static_cast<uint8_t>(n + 1);
            uint8_t bi = 0, bj = 1;
            int32_t best = INT32_MAX;
            for (uint8_t i = 0; i < m; ++i)
                for (uint8_t j = static_cast<uint8_t>(i + 1); j < m; ++j) {
                    const int32_t grow = area(unite(all[i], all[j])) - area(all[i]) - area(all[j]);
                    if (grow < best) { best = grow; bi = i; bj = j; }
                }
            all[bi] = unite(all[bi], all[bj]);
            n = 0;
            for (uint8_t i = 0; i < m; ++i)
                if (i != bj) q[n++] = all[i];
            return;
        }
        q[n++] = r;
    }
};

// ---------------------------------------------------------------------------
// The big glass: 480 x 320 landscape and 320 x 480 portrait, the Makerfabs
// 3.5" board's (internal/tty-ux-panel-mf35-2026-09-26.md, part 1, the status
// skin). A fixed node board of eleven rows, the calls and the recent list,
// the traffic sweep and six system cells, under the Waveshare's header.
// Chosen by the glass's size; the Waveshare's two layouts never reach it.
// ---------------------------------------------------------------------------
constexpr uint8_t kBigRows   = 11;    // the sysop line, then nodes 1 to 10
constexpr uint8_t kRecent    = 4;
constexpr uint8_t kCells     = 6;
constexpr int     kGraphUp   = 28;    // the sweep's heights above and below its axis
constexpr int     kGraphDown = 27;
constexpr int     kGraphCols = 160;   // the longest sweep, landscape

// The big glass's own fields, numbered on from the Waveshare's.
enum BigField : uint8_t { F_NAME = F_COUNT, F_WORD, F_DBM, F_PIPS, F_CALLS, F_TRAF,
                          F_CELL, F_ROW = F_CELL + kCells, F_RECENT = F_ROW + kBigRows,
                          F_BIG_COUNT = F_RECENT + kRecent };

// The system cells, in reading order: A then B, three rows.
enum Cell : uint8_t { C_HEAP, C_SLOW, C_CARD, C_PEAK, C_UP, C_CAMERA };

struct BigLayout {
    bool    on = false;               // the glass is big enough
    bool    land = false;
    Rect    name, slot, word, dbm;    // the header's additions (bar, band, glyphs, antenna,
                                      // clock and track are the Layout's)
    Rect    head;                     // "Callers 4/11" with its icon and column labels
    int16_t colDoing = 0, colOnEnd = 0, colTerm = 0;   // where the labels and columns are
    Rect    row[kBigRows];            // each row's text, x 14 on
    Rect    pips;                     // the pip column, one field
    Rect    colRule, midRule;         // landscape's vertical rule, portrait's middle one
    Rect    calls;                    // "Calls 23 today" with its icon
    Rect    recent[kRecent];
    Rect    traf, graph;              // the rate label and the sweep
    int16_t axis = 0;                 // the sweep's axis row
    Rect    cell[kCells];
    Rect    foot;                     // the rule over the LEDs
    Rect    leds;
};

inline bool bigGlass(uint16_t w, uint16_t h) {
    return (w >= 400 && h >= 300) || (w >= 300 && h >= 400);
}

// bigLayout: the big layouts, and a Layout with the parts the Waveshare's
// drawing shares (the header's rows, the glyph strip, antenna, clock, track,
// heading and LEDs) filled in, so that code draws them unchanged.
inline BigLayout bigLayout(uint16_t w, uint16_t h, Layout& L) {
    BigLayout B;
    L = Layout();
    if (!bigGlass(w, h)) return B;
    B.on   = true;
    B.land = w >= h;
    L.land = B.land;
    L.bar    = R(0, 0, w, 22);
    L.band   = R(0, 22, w, 20);
    L.track  = R(0, 42, w, 1);
    L.glyphs = R(4, 24, 120, 16);
    L.clock  = R(w - 44, 24, 5 * kSmallW, kSmallH);
    B.dbm    = R(L.clock.x - 6 - 32, 24, 32, 16);       // "-100" at worst: four glyphs
    L.ant    = R(B.dbm.x - 4 - kAntennaW, 24, kAntennaW, kAntennaH);
    if (B.land) {
        B.name = R(4, 3, 26 * kSmallW, 16);
        B.slot = R(w - 4 - 24 * kSmallW, 3, 24 * kSmallW, 16);
        B.word = R(134, 24, L.ant.x - 8 - 134, 16);
    } else {
        B.name = R(4, 3, 16 * kSmallW, 16);
        B.slot = R(w - 4 - 21 * kSmallW, 3, 21 * kSmallW, 16);
        B.word = Rect();                                   // no room beside the glyphs
    }
    L.slot = B.slot;
    // The node board, the same in both: heading at 48, rows at 68 + 20k.
    L.headIcon = R(6, 48, 16, 16);
    L.head     = R(26, 48, 116, 16);
    B.head     = R(6, 48, 296, 16);
    B.colDoing = 150;
    B.colOnEnd = 254;                                      // one past "ON"'s right edge
    B.colTerm  = 262;
    for (uint8_t k = 0; k < kBigRows; ++k) B.row[k] = R(14, 68 + kPitch * k, 288, 16);
    B.pips = R(6, 68, 6, kPitch * (kBigRows - 1) + 16);
    if (B.land) {
        B.colRule = R(308, 48, 1, 236);
        B.calls   = R(316, 48, 160, 16);
        for (uint8_t j = 0; j < kRecent; ++j) B.recent[j] = R(316, 68 + kPitch * j, 160, 16);
        B.traf  = R(316, 148, 160, 16);
        B.graph = R(316, 168, 160, 56);
        for (uint8_t c = 0; c < kCells; ++c)
            B.cell[c] = R(c % 2 ? 398 : 316, 228 + kPitch * (c / 2), 78, 16);
        B.foot = R(4, 290, 472, 1);
        L.leds = R(0, 296, w, 16);
    } else {
        B.midRule = R(4, 290, w - 8, 1);
        B.calls   = R(6, 296, 148, 16);
        for (uint8_t j = 0; j < kRecent; ++j) B.recent[j] = R(6, 316 + kPitch * j, 148, 16);
        B.traf  = R(166, 296, 150, 16);
        B.graph = R(166, 316, 148, 56);
        for (uint8_t c = 0; c < kCells; ++c)
            B.cell[c] = R(c % 2 ? 166 : 6, 396 + kPitch * (c / 2), 148, 16);
        B.foot = R(4, 456, w - 8, 1);
        L.leds = R(0, 462, w, 16);
    }
    B.axis = static_cast<int16_t>(B.graph.y + kGraphUp);
    B.leds = L.leds;
    Rect* all[] = { &B.name, &B.slot, &B.word, &B.dbm, &B.head, &B.pips, &B.colRule, &B.midRule, &B.calls,
                    &B.traf, &B.graph, &B.foot, &B.leds, &L.bar, &L.band, &L.track, &L.glyphs, &L.clock,
                    &L.ant, &L.slot, &L.headIcon, &L.head, &L.leds };
    for (Rect* r : all) *r = clip(*r, w, h);
    for (Rect& r : B.row) r = clip(r, w, h);
    for (Rect& r : B.recent) r = clip(r, w, h);
    for (Rect& r : B.cell) r = clip(r, w, h);
    return B;
}

// bigFieldBox: what a big-glass field clears and sends. The Waveshare's
// fields that the big glass shares (slot, clock, glyphs, antenna, heading)
// are in fieldBox, from the Layout bigLayout filled in; the heading here is
// the whole heading row with its column labels.
inline Rect bigFieldBox(const BigLayout& B, const Layout& L, uint8_t f) {
    if (f == F_HEAD) return B.head;
    if (f < F_COUNT) return f == F_SYS || f >= F_LIST ? Rect() : fieldBox(L, f);
    switch (f) {
        case F_NAME:  return B.name;
        case F_WORD:  return B.word;
        case F_DBM:   return B.dbm;
        case F_PIPS:  return B.pips;
        case F_CALLS: return B.calls;
        case F_TRAF:  return B.traf;
        default: break;
    }
    if (f >= F_CELL && f < F_CELL + kCells)       return B.cell[f - F_CELL];
    if (f >= F_ROW && f < F_ROW + kBigRows)       return B.row[f - F_ROW];
    if (f >= F_RECENT && f < F_RECENT + kRecent)  return B.recent[f - F_RECENT];
    return Rect();
}

// bigLedAt: LED i of n in the big glass's row: a cell of min(22, 240 / n), the
// LED clamp(cell - 6, 8, 14) square, the row centred, so the whole row is at
// most 240 x 16 = 3,840 px, one band.
inline Led bigLedAt(const Rect& box, uint8_t i, uint8_t n) {
    Led out;
    if (!n || i >= n || empty(box)) return out;
    int cell = 240 / n;
    if (cell > 22) cell = 22;
    int side = cell - 6;
    if (side < 8) side = 8;
    if (side > 14) side = 14;
    if (side > cell - 1) side = cell - 1;
    const int x0 = box.x + (box.w - cell * n) / 2;
    const int cx = x0 + i * cell;
    out.cell = R(cx, box.y, cell, box.h);
    out.led  = R(cx + (cell - side) / 2, box.y + (box.h - side) / 2, side, side);
    return out;
}

// bitLen: how many bits v needs; 0 for 0.
inline int bitLen(uint32_t v) {
    int n = 0;
    while (v) { ++n; v >>= 1; }
    return n;
}

// graphHeight: a rate's column in the sweep, H at most: a fixed log scale,
// 17 doublings to full at 128 KB/s, so the graph never rescales.
inline int graphHeight(uint32_t rate, int H) {
    const int h = (bitLen(rate) * H + 16) / 17;
    return h < H ? h : H;
}

// fmtRate: bytes a second in at most four glyphs: 0, 340, 1.2K, 12K, 1.2M.
inline void fmtRate(uint32_t v, char* out, size_t n) {
    if (v < 1000u)            snprintf(out, n, "%u", static_cast<unsigned>(v));
    else if (v < 10000u)      snprintf(out, n, "%u.%uK", static_cast<unsigned>(v / 1000u),
                                       static_cast<unsigned>(v % 1000u / 100u));
    else if (v < 1000000u)    snprintf(out, n, "%uK", static_cast<unsigned>(v / 1000u));
    else if (v < 10000000u)   snprintf(out, n, "%u.%uM", static_cast<unsigned>(v / 1000000u),
                                       static_cast<unsigned>(v % 1000000u / 100000u));
    else                      snprintf(out, n, "%uM", static_cast<unsigned>(v / 1000000u));
}

// sweepBox: the rectangle one sample of the sweep sends: the column at head
// and the two cleared ahead of it, cut at the graph's right edge (the part
// past it wraps to the left and is the second rectangle, wrap).
inline Rect sweepBox(const Rect& g, int head, Rect& wrap) {
    wrap = Rect();
    const int cols = g.w;
    if (cols <= 0) return Rect();
    const int n = 3 < cols ? 3 : cols;
    const int first = cols - head < n ? cols - head : n;
    if (first < n) wrap = R(g.x, g.y, n - first, g.h);
    return R(g.x + head, g.y, first, g.h);
}

} // namespace panelgfx
