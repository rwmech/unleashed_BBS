/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/camera_mark.h
 * Module:       Plugins / camera (BBS_HAS_CAMERA boards), the watermark
 *
 * Purpose:      The visible watermark (Rob: "doesnt have to be annoying but
 *               its helpful when looking at an image"): one small line in
 *               the bottom right corner, "board · date time · handle", light
 *               text with a thin dark outline so it reads on any picture
 *               without covering much of it, about 2 to 3% of the picture's
 *               height. Drawn a strip of rows at a time into RGB888, because
 *               the photo is re-encoded a strip at a time (platform.h,
 *               jpegMark) and never exists whole as pixels.
 *
 *               The glyphs are the panel's Spleen 8x16 (panel_font.h,
 *               BSD-2-Clause, credited there), scaled by whole pixels. The
 *               font has no middle dot, so one is drawn here.
 *
 *               Pure and header-only: host/test_camera.cpp draws into a
 *               buffer and reads it back.
 *
 * Interfaces:   Box, layout, fitText, drawRows
 *
 * Libraries:    none
 * Targets:      ESP32 and ESP32-S3 camera boards and the Linux host build
 * See also:     src/plugins/camera.cpp, src/plugins/panel_font.h
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
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "panel_font.h"

namespace cammark {

constexpr int kGlyphW = 8, kGlyphH = 16;
constexpr char kDot   = '\xB7';          // the separator, drawn here
constexpr char kMicro = '\xB5';          // the font's micro sign (panelfont::kMicro)

// The colours: near white on near black, never pure either, which JPEG
// rings around.
constexpr uint8_t kLight[3] = { 236, 236, 228 };
constexpr uint8_t kDark[3]  = { 16, 16, 20 };

struct Box {
    int scale = 1;           // each font pixel is scale x scale
    int x = 0, y = 0;        // the text's top left, in the picture
    int w = 0, h = 0;        // the text's size, outline not included
    int glyphs = 0;
};

// scaleFor: the whole-pixel scale that puts the line nearest 2.5% of the
// picture's height, never below one.
inline int scaleFor(int height) {
    int s = (height * 25 + 8000) / (1000 * kGlyphH);      // round(h * 0.025 / 16)
    return s < 1 ? 1 : s;
}

// margin: the gap to the picture's edges, 1% of the width, at least 2.
inline int margin(int width) { int m = width / 100; return m < 2 ? 2 : m; }

// maxGlyphs: how many characters fit in the width the corner may take, most
// of the picture's width less both margins.
inline int maxGlyphs(int width, int height) {
    int s = scaleFor(height);
    int room = width - 2 * margin(width) - 2 * s;
    int n = room / (kGlyphW * s);
    return n < 0 ? 0 : n;
}

// fitText: "board · when · who" in at most max characters. The board's name
// gives way first, then the handle; the date and time stay whole, and
// without room for them the watermark is just what fits of them.
inline void fitText(const char* board, const char* when, const char* who, int max,
                    char* out, size_t n) {
    if (!out || !n) return;
    out[0] = '\0';
    if (max <= 0) return;
    const char sep[] = { ' ', kDot, ' ', '\0' };
    int bl = static_cast<int>(strlen(board ? board : ""));
    int wl = static_cast<int>(strlen(when ? when : ""));
    int hl = static_cast<int>(strlen(who ? who : ""));
    int fixed = wl + (hl ? 3 + hl : 0);                      // "when · who"
    if (wl > max) { snprintf(out, n, "%.*s", max, when); return; }
    if (hl && fixed > max) {                                 // the handle cut to fit
        int room = max - wl - 3;
        if (room > 0) snprintf(out, n, "%s%s%.*s", when, sep, room, who);
        else          snprintf(out, n, "%s", when);
        return;
    }
    int room = max - fixed - 3;                              // for the board and its separator
    if (bl && room > 0) {
        snprintf(out, n, "%.*s%s%s%s%s", room < bl ? room : bl, board, sep, when, hl ? sep : "", hl ? who : "");
    } else {
        snprintf(out, n, "%s%s%s", when, hl ? sep : "", hl ? who : "");
    }
}

// toFont: UTF-8 text as the watermark's one byte a character: ASCII as it
// is, the micro sign as kMicro (a board called µnleashed something), and any
// other character as one '?', so a name is never two garbage glyphs wide.
inline void toFont(const char* in, char* out, size_t n) {
    size_t k = 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(in ? in : "");
    while (*p && k + 1 < n) {
        if (*p < 0x80) { out[k++] = static_cast<char>(*p++); continue; }
        if (p[0] == 0xC2 && p[1] == 0xB5) { out[k++] = kMicro; p += 2; continue; }
        size_t len = (*p >= 0xF0) ? 4 : (*p >= 0xE0) ? 3 : (*p >= 0xC0) ? 2 : 1;
        for (size_t i = 0; i < len && *p; ++i) ++p;
        out[k++] = '?';
    }
    out[k] = '\0';
}

// layout: where text goes in a width x height picture.
inline Box layout(int width, int height, const char* text) {
    Box b;
    b.scale  = scaleFor(height);
    b.glyphs = static_cast<int>(strlen(text));
    b.w = b.glyphs * kGlyphW * b.scale;
    b.h = kGlyphH * b.scale;
    int m = margin(width);
    b.x = width - m - b.scale - b.w;
    b.y = height - m - b.scale - b.h;
    if (b.x < 0) b.x = 0;
    if (b.y < 0) b.y = 0;
    return b;
}

// lit: the font pixel (fx, fy) of text, in font pixels from the box's top
// left. Outside the text is dark space.
inline bool lit(const char* text, int glyphs, int fx, int fy) {
    if (fx < 0 || fy < 0 || fy >= kGlyphH || fx >= glyphs * kGlyphW) return false;
    unsigned char c = static_cast<unsigned char>(text[fx / kGlyphW]);
    int col = fx % kGlyphW;
    if (c == static_cast<unsigned char>(kDot))              // a 2 x 2 dot, centred
        return (col == 3 || col == 4) && (fy == 8 || fy == 9);
    int g = (c >= 0x20 && c <= 0x7E) ? c - 0x20
          : c == static_cast<unsigned char>(kMicro) ? panelfont::kMicro : '?' - 0x20;
    return (panelfont::kSmall[g][fy] >> (7 - col)) & 1;
}

// drawRows: draw the part of the watermark that falls on picture rows y0 to
// y0 + rows - 1, held in rgb (RGB888, width pixels a row). Only the text and
// its outline change; every other pixel is left as the camera saw it.
inline void drawRows(uint8_t* rgb, int width, int y0, int rows, const Box& b, const char* text) {
    const int s = b.scale;
    const int top = b.y - s, bottom = b.y + b.h + s;         // the outline's reach
    for (int y = y0 < top ? top : y0; y < y0 + rows && y < bottom; ++y) {
        int fy = (y - b.y) >= 0 ? (y - b.y) / s : -1;
        uint8_t* row = rgb + static_cast<size_t>(y - y0) * static_cast<size_t>(width) * 3u;
        for (int x = b.x - s; x < b.x + b.w + s && x < width; ++x) {
            if (x < 0) continue;
            int fx = (x - b.x) >= 0 ? (x - b.x) / s : -1;
            const uint8_t* c = nullptr;
            if (lit(text, b.glyphs, fx, fy)) {
                c = kLight;
            } else {
                // Outline: a font pixel next to a lit one, in any of the
                // eight directions, measured in the picture's pixels so the
                // outline stays one scale wide.
                int ax = x - b.x, ay = y - b.y;
                for (int dy = -s; dy <= s && !c; dy += s)
                    for (int dx = -s; dx <= s && !c; dx += s) {
                        int nx = ax + dx, ny = ay + dy;
                        if (nx < 0 || ny < 0) continue;
                        if (lit(text, b.glyphs, nx / s, ny / s)) c = kDark;
                    }
            }
            if (c) memcpy(row + static_cast<size_t>(x) * 3u, c, 3);
        }
    }
}

} // namespace cammark
