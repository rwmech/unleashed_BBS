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
 *               panel: RGB565 colours, text in the Spleen faces, a disc for
 *               each pixel of the strip, where each figure goes on the glass
 *               at a given size, and which rectangles still need sending.
 *               No platform and no BBS in it, so host/test_panel.cpp can
 *               check every edge of it with no display attached.
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
 * Libraries:    none
 * Targets:      ESP32-S3 boards with BBS_HAS_LCD, and the Linux host tests
 * See also:     src/plugins/panel.cpp, src/plugins/panel_font.h
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
#pragma once
#include <cstdint>
#include <cstddef>
#include "panel_font.h"

namespace panelgfx {

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
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

// ledLevel: one channel of a strip pixel, as the lights plugin sent it, as
// it should look on glass. A WS2812B's channel is capped at 30% (76 of 255)
// and 10% is the shipped brightness, which drawn as it stands is a near
// black a person reads as "off". The cap becomes full scale and the rest
// follows a square root, which is roughly how a dimmed LED reads to the
// eye: 76 is 255, 25 (10%) is 146, 2 (1%) is 41, and dark stays dark.
inline uint8_t ledLevel(uint8_t v) {
    // 856 is the least k with isqrt(76 k) = 255.
    uint32_t o = isqrt(static_cast<uint32_t>(v) * 856u);
    return static_cast<uint8_t>(o > 255u ? 255u : o);
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
// that fits (a portrait panel's last event: "15:18 logoff", then the handle).
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

// ---------------------------------------------------------------------------
// The strip's pixels: a lamp each, a disc with a rim, so a dark pixel still
// shows where it is.
// ---------------------------------------------------------------------------
inline void disc(Canvas& c, int cx, int cy, int r, uint16_t col) {
    for (int dy = -r; dy <= r; ++dy) {
        int dx = static_cast<int>(isqrt(static_cast<uint32_t>(r * r - dy * dy)));
        fill(c, { static_cast<int16_t>(cx - dx), static_cast<int16_t>(cy + dy),
                  static_cast<int16_t>(2 * dx + 1), 1 }, col);
    }
}

// stripGrid: how n lamps sit in the strip's box, as rows of cols square
// cells of size pixels, for the biggest lamps. One row on a wide landscape
// panel; on a portrait one the strip wraps, ten pixels as two rows of five
// rather than ten slivers. A grid is scored by its lamps' size times the
// square of how full it is, so empty places in a short last row count
// against it: ten as 4, 4 and 2 lamps of 43 pixels loses to 5 and 5 of 34,
// and seven comes out 4 over 3 rather than 3, 3 and a lone 1.
inline void stripGrid(const Rect& s, uint8_t n, uint8_t& cols, uint8_t& rows, int& size) {
    cols = rows = 0;
    size = 0;
    int best = -1;
    for (int r = 1; r <= n; ++r) {
        int k = (n + r - 1) / r;
        if ((r - 1) * k >= n) continue;                     // an empty last row
        int cw = s.w / k, ch = s.h / r;
        int sz = cw < ch ? cw : ch;
        const int places = r * k;
        int score = sz * n * n * 64 / (places * places);
        if (score > best) {
            best = score;
            cols = static_cast<uint8_t>(k);
            rows = static_cast<uint8_t>(r);
            size = sz;
        }
    }
}

// stripCell: lamp i of n's square cell: the grid centred in the strip's box
// both ways, and a short last row centred under the others.
inline Rect stripCell(const Rect& s, uint8_t i, uint8_t n) {
    if (!n || i >= n) return {};
    uint8_t cols, rows;
    int sz;
    stripGrid(s, n, cols, rows, sz);
    if (sz <= 0) return {};
    const int row = i / cols, col = i % cols;
    const int inRow = row + 1 < rows ? cols : n - row * cols;
    const int x0 = s.x + (s.w - sz * inRow) / 2;
    const int y0 = s.y + (s.h - sz * rows) / 2;
    return { static_cast<int16_t>(x0 + col * sz), static_cast<int16_t>(y0 + row * sz),
             static_cast<int16_t>(sz), static_cast<int16_t>(sz) };
}

// lamp: one strip pixel in its cell, lit in col, in a rim two pixels wide
// (one pixel of rim on a disc reads as a dotted line where the circle's
// steps fall). A lamp too small for a rim is all light.
inline void lamp(Canvas& c, const Rect& cell, uint16_t col, uint16_t rim, uint16_t bg) {
    fill(c, cell, bg);
    int d = cell.w < cell.h ? cell.w : cell.h;
    int r = d / 2 - 2;
    if (r < 1) r = 1;
    int cx = cell.x + cell.w / 2, cy = cell.y + cell.h / 2;
    if (r > 4) {
        disc(c, cx, cy, r, rim);
        disc(c, cx, cy, r - 2, col);
    } else {
        disc(c, cx, cy, r, col);
    }
}

// ---------------------------------------------------------------------------
// Layout: where each figure goes for a panel of w x h, portrait (the
// Waveshare stick's 172 x 320, hanging from a port) or landscape.
// ---------------------------------------------------------------------------
enum Field : uint8_t { F_NAME, F_CLOCK, F_LABEL, F_CALLERS, F_ADDR, F_UPTIME, F_CARD, F_EVENT,
                       F_COUNT };

struct Layout {
    Rect    bar;                  // the title bar, behind name and clock
    Rect    field[F_COUNT];
    bool    big[F_COUNT] = {};
    uint8_t align[F_COUNT] = {};
    Rect    strip;
};

inline Rect R(int x, int y, int w, int h) {
    return { static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w < 0 ? 0 : w),
             static_cast<int16_t>(h < 0 ? 0 : h) };
}

inline Layout layout(uint16_t w, uint16_t h) {
    Layout L;
    const int clockW = 5 * kSmallW;                       // "12:34"
    L.bar = R(0, 0, w, 20);
    L.field[F_NAME]  = R(4, 2, w - 8 - clockW - 8, kSmallH);
    L.field[F_CLOCK] = R(w - 4 - clockW, 2, clockW, kSmallH);
    L.align[F_CLOCK] = RIGHT;
    L.big[F_CALLERS] = true;
    L.align[F_CALLERS] = CENTRE;
    L.align[F_LABEL] = CENTRE;
    if (w >= h) {
        // Landscape: "callers on 3 of 10" in one large line, the address
        // and uptime sharing a line, then the card, then the last event,
        // and the strip along the bottom.
        L.field[F_LABEL]   = R(0, 0, 0, 0);
        L.field[F_CALLERS] = R(0, 24, w, kBigH);
        int half = (w - 8) * 3 / 5;
        L.field[F_ADDR]    = R(4, 62, half, kSmallH);
        L.field[F_UPTIME]  = R(4 + half, 62, w - 8 - half, kSmallH);
        L.align[F_UPTIME]  = RIGHT;
        L.field[F_CARD]    = R(4, 82, w - 8, kSmallH);
        L.field[F_EVENT]   = R(4, 102, w - 8, kSmallH);
        L.strip            = R(0, 122, w, h - 122);
    } else {
        // Portrait (the Waveshare stick hanging from a port): the label on
        // a line of its own above the figure, then a line each, the last
        // event on two, and the strip filling the rest as a grid of lamps.
        L.field[F_LABEL]   = R(0, 28, w, kSmallH);
        L.field[F_CALLERS] = R(0, 46, w, kBigH);
        L.field[F_ADDR]    = R(4, 88, w - 8, kSmallH);
        L.field[F_UPTIME]  = R(4, 106, w - 8, kSmallH);
        L.field[F_CARD]    = R(4, 124, w - 8, kSmallH);
        L.field[F_EVENT]   = R(4, 142, w - 8, 2 * kSmallH);
        L.strip            = R(0, 180, w, h - 184);
    }
    for (Rect& r : L.field) r = clip(r, w, h);
    L.bar   = clip(L.bar, w, h);
    L.strip = clip(L.strip, w, h);
    return L;
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

} // namespace panelgfx
