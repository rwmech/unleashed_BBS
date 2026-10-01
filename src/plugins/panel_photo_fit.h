/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/panel_photo_fit.h
 * Module:       Plugins / panel: a new photo on the glass, the pure half (1.2.1)
 *
 * Purpose:      What panel_photo.h draws and decodes with, kept free of the
 *               platform, the runner and the card so host/test_panel_photo.cpp
 *               can check it at every board's size: the frame (where the
 *               picture, its keyline and the caption go on a glass of w x h,
 *               never cropping or enlarging), the scale the decoder runs at,
 *               the caption's words and how they are cut to fit, and the
 *               scaler that area-averages TJpgDec's blocks into the picture.
 *               The swipe gallery reuses all of it.
 *
 * Libraries:    none (libc)
 * Targets:      ESP32-S3 boards with BBS_HAS_LCD (through panel_photo.h), and
 *               the Linux host tests
 * See also:     src/plugins/panel_photo.h, host/test_panel_photo.cpp,
 *               internal/tty-ux-panel-gallery-2026-09-28.md
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
#include "../config.h"          // BBS_USER_MAX
#include "panel_gfx.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace photoshow {

using panelgfx::Rect;
using panelgfx::R;

constexpr uint16_t kAccRows  = 24;       // the scaler's ring: an MCU row's 16 and the one it straddles

// ===========================================================================
// The frame: where everything goes on a glass of w x h, for a picture of
// iw x ih. Pure, so the host can test it at every board's size.
// ===========================================================================
enum : uint8_t { SHAPE_LAND, SHAPE_PORTRAIT, SHAPE_SQUARE };

struct Frame {
    Rect     title, strip;        // the mat's two rows at the top
    Rect     key, pic;            // the 1 px keyline and the picture inside it
    Rect     cap1, cap2;          // the caption; cap2 only in portrait (the time)
    uint16_t pw = 0, ph = 0;      // the picture as drawn
    uint8_t  scale = 0;           // decoded at 1 / (1 << scale)
    uint8_t  shape = SHAPE_LAND;
};

inline uint8_t shapeOf(uint16_t w, uint16_t h) {
    return w == h ? SHAPE_SQUARE : w < h ? SHAPE_PORTRAIT : SHAPE_LAND;
}

// box: the largest picture the frame holds on this glass.
//   portrait   the width less 4 px of mat and the keyline each side; under
//              it a 4 px gap and two 20 px caption rows
//   square     12 px of mat each side; the caption (16) 8 px under the
//              keyline, the block centred in y 28 to h - 12
//   landscape  16 px of mat each side; the caption the glass's foot row
//              (20), the picture in y 28 to h - 24
inline void box(uint16_t w, uint16_t h, int& bw, int& bh) {
    switch (shapeOf(w, h)) {
        case SHAPE_PORTRAIT: bw = w - 10; bh = h - 78; break;
        case SHAPE_SQUARE:   bw = w - 24; bh = h - 66; break;
        default:             bw = w - 34; bh = h - 54; break;
    }
}

// fit: the frame for a picture of iw x ih, or false when the glass is too
// small to frame anything. Never cropped, never enlarged: the scale is
// min(bw / iw, bh / ih, 1), the size its floor.
inline bool fit(uint16_t w, uint16_t h, uint16_t iw, uint16_t ih, Frame& f) {
    f = Frame();
    int bw = 0, bh = 0;
    box(w, h, bw, bh);
    if (!iw || !ih || bw < 16 || bh < 16) return false;
    uint32_t pw = iw, ph = ih;
    if (pw > static_cast<uint32_t>(bw) || ph > static_cast<uint32_t>(bh)) {
        if (static_cast<uint32_t>(iw) * static_cast<uint32_t>(bh) >= static_cast<uint32_t>(ih) * static_cast<uint32_t>(bw)) {
            pw = static_cast<uint32_t>(bw);
            ph = static_cast<uint32_t>(ih) * static_cast<uint32_t>(bw) / iw;
        } else {
            ph = static_cast<uint32_t>(bh);
            pw = static_cast<uint32_t>(iw) * static_cast<uint32_t>(bh) / ih;
        }
    }
    if (!pw) pw = 1;
    if (!ph) ph = 1;
    f.pw = static_cast<uint16_t>(pw);
    f.ph = static_cast<uint16_t>(ph);
    // The decode: the largest of 1/1 to 1/8 whose output is still at least
    // the drawn size (TJpgDec's output at 1/2^s is exactly iw >> s).
    uint8_t s = 3;
    while (s && ((static_cast<uint32_t>(iw) >> s) < pw || (static_cast<uint32_t>(ih) >> s) < ph)) --s;
    f.scale = s;
    f.shape = shapeOf(w, h);
    f.title = R(0, 0, w, 20);
    f.strip = R(0, 20, w, 4);
    const int kw = static_cast<int>(pw) + 2, kh = static_cast<int>(ph) + 2;
    const int kx = (static_cast<int>(w) - kw) / 2;
    int ky;
    if (f.shape == SHAPE_PORTRAIT) {
        ky = 28 + (static_cast<int>(h) - 28 - (kh + 4 + 40)) / 2;
        f.cap1 = R(4, ky + kh + 4, w - 8, 20);
        f.cap2 = R(4, ky + kh + 24, w - 8, 20);
    } else if (f.shape == SHAPE_SQUARE) {
        ky = 28 + (static_cast<int>(h) - 40 - (kh + 24)) / 2;
        f.cap1 = R(12, ky + kh + 8, w - 24, 16);
    } else {
        ky = 28 + (static_cast<int>(h) - 52 - kh) / 2;
        f.cap1 = R(4, h - 20, w - 8, 20);
    }
    f.key = R(kx, ky, kw, kh);
    f.pic = R(kx + 1, ky + 1, static_cast<int>(pw), static_cast<int>(ph));
    return true;
}

// ===========================================================================
// The caption: the kind's icon, the camera's number (what SNAPSHOT takes)
// and name, a caller's handle on a snap, and when, from the name's stamp.
// ===========================================================================
enum : uint8_t { KIND_SNAP = 0, KIND_TIMELAPSE, KIND_MOTION };

struct Caption {
    uint8_t kind = KIND_SNAP;
    char    num[4]  = "";                 // "2", "" when the camera is not known
    char    name[17] = "";                // "garden"; the kind's word when unknown
    char    who[BBS_USER_MAX + 1] = "";   // snaps only, and only of a caller not hiding
    char    when[16] = "";                // "today 14:32", "27 Sep 14:32"
};

// stampIn: the -YYYYMMDD-HHMMSS in the last part of a photo's name, as
// every camera writes it (camera_rules.h). False when there is none.
inline bool stampIn(const char* rel, int& y, int& mo, int& d, int& h, int& mi) {
    const char* base = strrchr(rel, '/');
    base = base ? base + 1 : rel;
    for (const char* p = strchr(base, '-'); p; p = strchr(p + 1, '-')) {
        bool ok = true;
        for (int i = 1; i <= 15 && ok; ++i) ok = (i == 9) ? p[i] == '-' : (p[i] >= '0' && p[i] <= '9');
        if (!ok) continue;
        auto num = [&](int at, int len) {
            int v = 0;
            for (int i = 0; i < len; ++i) v = v * 10 + (p[1 + at + i] - '0');
            return v;
        };
        y = num(0, 4); mo = num(4, 2); d = num(6, 2); h = num(9, 2); mi = num(11, 2);
        return mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h <= 23 && mi <= 59;
    }
    return false;
}

// whenText: "today 14:32" when the stamp is today's (today as YYYYMMDD, 0
// when the clock is not set), else "27 Sep 14:32". "" with no stamp.
inline void whenText(const char* rel, uint32_t today, char* out, size_t n) {
    static const char kMon[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    if (!n) return;
    out[0] = '\0';
    if (!stampIn(rel, y, mo, d, h, mi)) return;
    const uint32_t key = static_cast<uint32_t>(y) * 10000u + static_cast<uint32_t>(mo) * 100u + static_cast<uint32_t>(d);
    // Two digits each, as stampIn checked them; said again in the type so
    // the compiler's range for the format is the real one.
    const unsigned hh = static_cast<unsigned>(h) % 24u, mm = static_cast<unsigned>(mi) % 60u;
    const unsigned dd = static_cast<unsigned>(d) % 32u;
    if (today && key == today) snprintf(out, n, "today %02u:%02u", hh, mm);
    else                       snprintf(out, n, "%u %s %02u:%02u", dd, kMon[(mo - 1) % 12], hh, mm);
}

// shrink: the caption's words cut to a row of fit glyphs after the icon,
// the gallery spec's order: the handle cut to 6, the handle dropped, the
// name cut. The number and the time are never cut. two: the time has a row
// of its own (portrait), so only the first row is measured.
inline void shrink(Caption& c, int fit, bool two) {
    auto len = [](const char* s) { return static_cast<int>(strlen(s)); };
    auto cost = [&]() {
        int n = 0;
        if (c.num[0]) n += len(c.num) + 1;
        n += len(c.name);
        if (c.who[0]) n += 2 + len(c.who);
        if (!two && c.when[0]) n += 2 + len(c.when);
        return n;
    };
    if (cost() > fit && len(c.who) > 6) c.who[6] = '\0';
    if (cost() > fit) c.who[0] = '\0';
    const int over = cost() - fit;
    if (over > 0) {
        const int keep = len(c.name) - over;
        c.name[keep > 0 ? keep : 0] = '\0';
    }
}

// The scaler: TJpgDec's blocks (RGB888, a row of blocks left to right, top
// to bottom) area-averaged into the picture. Source pixel (sx, sy) of the
// decoded sw x sh lands on picture pixel (sx * pw / sw, sy * ph / sh); a
// picture row is finished, and written, once the last source row that
// lands on it has come. The ring holds the rows not yet finished: an MCU
// row is at most 16 source rows, which land on at most 16 picture rows,
// with one more left from the row before.
struct Scaler {
    void*     f;                          // first: the decoder's reader takes the context as the file
    uint16_t* pic;
    uint16_t* acc;                        // kAccRows x pw x { r, g, b, n }
    uint16_t  pw, ph, sw, sh;
    uint16_t  next = 0;                   // the first picture row not yet written
    std::atomic<bool>* cancel;
    bool      yield;                      // stop at a row of blocks when other work is queued
    bool      yielded = false;
    bool    (*waiting)() = nullptr;       // other work is queued (runner::waiting on the board)
    void    (*rest)()    = nullptr;       // a row of blocks done: give the processor away
};

inline uint32_t lastSrc(const Scaler& s, uint32_t dy) {   // the last source row on picture row dy
    return ((dy + 1) * s.sh + s.ph - 1) / s.ph - 1;
}

inline void finishRow(Scaler& s) {
    uint16_t* a   = s.acc + static_cast<size_t>(s.next % kAccRows) * s.pw * 4u;
    uint16_t* out = s.pic + static_cast<size_t>(s.next) * s.pw;
    uint16_t last = 0;
    for (uint16_t x = 0; x < s.pw; ++x, a += 4) {
        const uint16_t n = a[3];
        if (n) last = panelgfx::rgb(static_cast<uint8_t>(a[0] / n), static_cast<uint8_t>(a[1] / n),
                                    static_cast<uint8_t>(a[2] / n));
        out[x] = last;                    // a pixel no sample reached takes its left neighbour's
        a[0] = a[1] = a[2] = a[3] = 0;
    }
    ++s.next;
}

inline bool putBlock(void* ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* rgb) {
    Scaler& s = *static_cast<Scaler*>(ctx);
    if (s.cancel->load(std::memory_order_relaxed)) return false;
    for (uint16_t r = 0; r < h; ++r) {
        const uint32_t sy = static_cast<uint32_t>(y) + r;
        if (sy >= s.sh) { rgb += static_cast<size_t>(w) * 3u; continue; }
        const uint32_t dy = sy * s.ph / s.sh;
        while (dy >= static_cast<uint32_t>(s.next) + kAccRows) finishRow(s);   // never, by the arithmetic
        uint16_t* row = s.acc + static_cast<size_t>(dy % kAccRows) * s.pw * 4u;
        for (uint16_t c = 0; c < w; ++c, rgb += 3) {
            const uint32_t sx = static_cast<uint32_t>(x) + c;
            if (sx >= s.sw) continue;
            uint16_t* e = row + static_cast<size_t>(sx * s.pw / s.sw) * 4u;
            e[0] = static_cast<uint16_t>(e[0] + rgb[0]);
            e[1] = static_cast<uint16_t>(e[1] + rgb[1]);
            e[2] = static_cast<uint16_t>(e[2] + rgb[2]);
            e[3] = static_cast<uint16_t>(e[3] + 1);
        }
    }
    // A row of blocks done: the picture rows it finished, and the idle
    // task's turn (skin.cpp's putRows).
    if (static_cast<uint32_t>(x) + w >= s.sw) {
        const uint32_t end = static_cast<uint32_t>(y) + h;
        while (s.next < s.ph && lastSrc(s, s.next) < end) finishRow(s);
        // Another job queued behind this one (a FILES page, a forum walk,
        // the link): this one steps aside and is posted again, so a second
        // of decoding is never somebody else's wait (Rule no. 1, for the
        // callers who never asked for a photo).
        if (s.yield && s.waiting && s.waiting()) {
            s.yielded = true;
            return false;
        }
        if (s.rest) s.rest();
    }
    return true;
}
// finish: the rows the last blocks left open, once the decode is whole.
inline void finish(Scaler& s) {
    while (s.next < s.ph) finishRow(s);
}

}  // namespace photoshow
