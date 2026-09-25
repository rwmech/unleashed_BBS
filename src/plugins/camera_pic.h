/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/camera_pic.h
 * Module:       Plugins / camera (BBS_HAS_CAMERA boards), the picture
 *
 * Purpose:      What the camera does to a picture besides taking it (1.1.1,
 *               after the first outdoor photo came out washed out): the sizes
 *               a sensor can give, the GC0308 settings its driver leaves out,
 *               when the sensor's exposure has settled, and the correction
 *               applied on the worker before the encode ("Auto levels" and a
 *               gamma). Pure and header-only, so host/test_camera.cpp drives
 *               every rule with no sensor and no board.
 *
 *               The correction is three 256-entry tables built from a
 *               histogram of the frame: no second frame is ever held. A raw
 *               frame (the GC0308's RGB565) is read once for the histogram,
 *               then converted a strip at a time with the tables applied, as
 *               it always was. A sensor's own JPEG (the OV2640) is decoded
 *               once at an eighth of its size for the histogram, then once
 *               in full and re-encoded, as the watermark already did.
 *
 * Interfaces:   kSizes, sizeCountFor, sizeList, clampSize,
 *               gc0308Regs, Settle, Steady, cut, buildTables, histRgb565,
 *               reencodeQuality
 *
 * Libraries:    none
 * Targets:      ESP32 and ESP32-S3 camera boards and the Linux host build
 * See also:     src/plugins/camera.cpp, src/platform/platform.h
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
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace campic {

// ---------------------------------------------------------------------------
// Sizes. Every size CONFIG camera may offer, smallest first, with the words
// it uses and the frame each one is (esp32-camera 2.1.7's resolution table).
// A sensor offers those that fit inside its largest frame: a GC0308 the
// first two, an OV2640 the first seven. The saved size is kept by its word,
// so a board that has its OV2640 swapped for a GC0308 and back again keeps
// what the sysop chose; only the size a snap uses is brought down.
// ---------------------------------------------------------------------------
struct Size { const char* word; uint16_t w, h; };
constexpr Size kSizes[] = {
    { "qvga", 320, 240 },   { "vga", 640, 480 },    { "svga", 800, 600 },
    { "xga", 1024, 768 },   { "hd", 1280, 720 },    { "sxga", 1280, 1024 },
    { "uxga", 1600, 1200 }, { "qxga", 2048, 1536 },
};
constexpr uint8_t kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]);
constexpr char kAllSizes[] = "qvga|vga|svga|xga|hd|sxga|uxga|qxga";

// sizeCountFor: how many of kSizes a sensor whose largest frame is maxW x
// maxH can give. 0 x 0 (not known yet) gives fallback.
inline uint8_t sizeCountFor(uint16_t maxW, uint16_t maxH, uint8_t fallback) {
    if (!maxW || !maxH) return fallback;
    uint8_t n = 0;
    for (const Size& z : kSizes)
        if (z.w <= maxW && z.h <= maxH) ++n;
    return n ? n : 1;
}

// sizeList: the first count words, bar-separated, as CONFIG's choices.
inline void sizeList(uint8_t count, char* out, size_t cap) {
    if (!out || !cap) return;
    out[0] = '\0';
    size_t at = 0;
    for (uint8_t i = 0; i < count && i < kSizeCount; ++i) {
        int w = snprintf(out + at, cap - at, "%s%s", i ? "|" : "", kSizes[i].word);
        if (w < 0 || static_cast<size_t>(w) >= cap - at) { out[at] = '\0'; break; }   // whole words only
        at += static_cast<size_t>(w);
    }
}

// clampSize: the size a snap uses: the saved one, or the largest the
// sensor gives when the saved one is bigger.
inline uint8_t clampSize(uint8_t saved, uint8_t count) {
    if (!count) return 0;
    return saved < count ? saved : static_cast<uint8_t>(count - 1);
}

// ---------------------------------------------------------------------------
// The GC0308's own settings for CONFIG camera's -2..2 (GalaxyCore, GC0308
// DataSheet, 2010-01-28: page 0, selected by writing 0 to 0xFE). esp32-camera
// 2.1.7's driver has contrast (0xB3), saturation (0xB1, 0xB2) and the AEC
// target (0xD3), but its brightness is set_dummy: nothing is written. So
// all five are written here, after the driver's own, the same way at every
// bring-up, and brightness exists at all.
//   0xB5 Luma_offset   "Add offset on luma value. S7": a signed offset,
//                      default 0x00. 16 a step: -32..+32.
//   0xB3 luma_contrast 2.6 bits, 0x40 = 1.0. 16 a step: 0.5..1.5.
//   0xB1 saturation_Cb, 0xB2 saturation_Cr: the driver's defaults are 0x40;
//                      16 a step, as the driver.
//   0xD3 AEC_target_Y  "expected luminance value", default 0x48 (72). 12 a
//                      step: 48..96, wider than the driver's 8, so -2 is a
//                      visibly darker picture.
// ---------------------------------------------------------------------------
struct Reg { uint8_t reg, val; };
constexpr uint8_t kGcPage      = 0xFE;
constexpr uint8_t kGcLumaOff   = 0xB5;
constexpr uint8_t kGcContrast  = 0xB3;
constexpr uint8_t kGcSatCb     = 0xB1;
constexpr uint8_t kGcSatCr     = 0xB2;
constexpr uint8_t kGcAecTarget = 0xD3;
constexpr uint8_t kGcYAverage  = 0xD4;   // RO: the current frame's luma average
constexpr uint8_t kGcTarget0   = 0x48;

inline int8_t clamp2(int v) { return static_cast<int8_t>(v < -2 ? -2 : v > 2 ? 2 : v); }

// gc0308Regs: the page select and the five writes, in order. Returns 6.
inline uint8_t gc0308Regs(int bright, int contrast, int sat, int exposure, Reg out[6]) {
    const int b = clamp2(bright), c = clamp2(contrast), s = clamp2(sat), e = clamp2(exposure);
    out[0] = { kGcPage, 0x00 };
    out[1] = { kGcLumaOff, static_cast<uint8_t>(static_cast<int8_t>(b * 16)) };
    out[2] = { kGcContrast, static_cast<uint8_t>(0x40 + c * 16) };
    out[3] = { kGcSatCb, static_cast<uint8_t>(0x40 + s * 16) };
    out[4] = { kGcSatCr, static_cast<uint8_t>(0x40 + s * 16) };
    out[5] = { kGcAecTarget, static_cast<uint8_t>(kGcTarget0 + e * 12) };
    return 6;
}

// ---------------------------------------------------------------------------
// Settle: whether the sensor's auto exposure has finished, from its own
// frame average (the GC0308's Y_average, 0xD4) against its target (0xD3),
// one reading a frame. The camera comes up from cold for every snap, and a
// GC0308 starts on its default exposure, which outdoors is many times too
// long: the three frames the camera used to wait were taken before the AEC
// had moved, which is why the first outdoor photo was white and why the
// Exposure setting "did nothing". Settled when the average is near the
// target and has stopped moving, or when it has stopped moving for long
// enough that the AEC is at a limit (a dark room, a lamp in the frame).
// The caller stops at kSettleMaxMs whatever this says.
// ---------------------------------------------------------------------------
constexpr uint32_t kSettleMaxMs  = 2500;
constexpr uint32_t kSettleLimitMs = 1200;   // still this long: at a limit
constexpr uint8_t  kSettleNear   = 16;      // of the target
constexpr uint8_t  kSettleStill  = 3;       // frame to frame

struct Settle {
    uint8_t  y[3] = {};
    uint8_t  n = 0;                         // readings so far
};

// Steady: the same for a sensor that reports no target, only what its
// auto exposure is doing (the OV2640: its exposure lines and its gain).
// Steady once neither has moved by more than kSteadyPct percent (or two
// counts) over two frames running, after kSteadyMin frames, which is also
// what its white balance needs to catch up: the green cast of a frame
// taken straight after power-up is AWB that has not run yet. The caller
// stops at kSteadyMaxMs whatever this says.
constexpr uint32_t kSteadyMaxMs = 1500;
constexpr uint8_t  kSteadyMin   = 4;
constexpr uint8_t  kSteadyPct   = 3;

struct Steady {
    uint16_t e[3] = {}, g[3] = {};
    uint8_t  n = 0;
};

inline bool steady(Steady& s, uint16_t exposure, uint16_t gain) {
    for (int i = 2; i > 0; --i) { s.e[i] = s.e[i - 1]; s.g[i] = s.g[i - 1]; }
    s.e[0] = exposure;
    s.g[0] = gain;
    if (s.n < 255) ++s.n;
    if (s.n < kSteadyMin) return false;
    auto near = [](uint16_t a, uint16_t b) {
        const uint32_t d = a > b ? a - b : b - a, m = a > b ? a : b;
        return d <= 2 || d * 100u <= m * kSteadyPct;
    };
    return near(s.e[0], s.e[1]) && near(s.e[1], s.e[2]) && near(s.g[0], s.g[1]) && near(s.g[1], s.g[2]);
}

inline bool settled(Settle& s, uint8_t y, uint8_t target, uint32_t elapsedMs) {
    s.y[2] = s.y[1];
    s.y[1] = s.y[0];
    s.y[0] = y;
    if (s.n < 255) ++s.n;
    if (s.n < 3) return false;
    auto diff = [](uint8_t a, uint8_t b) { return a > b ? a - b : b - a; };
    const bool still = diff(s.y[0], s.y[1]) <= kSettleStill && diff(s.y[1], s.y[2]) <= kSettleStill;
    if (still && diff(y, target) <= kSettleNear) return true;
    return still && elapsedMs >= kSettleLimitMs;
}

// ---------------------------------------------------------------------------
// The correction. A histogram a channel, 256 bins (a raw frame's 5 and 6
// bit values land on every 8th and 4th bin, which is fine for this), then a
// table a channel: "Auto levels" stretches each channel so that clip per
// mille of its pixels at each end go to black and to white, and the gamma
// bends the middle ("1.0" none; under 1 darker middle tones, which is what
// a washed-out picture wants; over 1 lighter). Each channel's points are
// kept within kCastSpan of the three channels' common points: enough to
// take a colour cast out, not enough to turn a green lawn grey.
// ---------------------------------------------------------------------------
struct Hist {
    uint32_t c[3][256];
    uint32_t all[256];     // buildTables' scratch: here, not on the worker's stack
    uint32_t n;
};

constexpr uint16_t kClipPermille = 5;       // 0.5% at each end
constexpr uint8_t  kMinSpan      = 48;      // no more than about 5x on a flat frame
constexpr uint8_t  kCastSpan     = 32;
constexpr char     kGammas[]     = "0.6|0.7|0.8|0.9|1.0|1.1|1.2|1.4|1.6";
constexpr uint8_t  kGammaNone    = 4;       // "1.0"

// gammaTenths: a kGammas word's value in tenths.
inline uint8_t gammaTenths(uint8_t index) {
    static const uint8_t k[] = { 6, 7, 8, 9, 10, 11, 12, 14, 16 };
    return index < sizeof(k) ? k[index] : 10;
}

// cut: the black and white points of one channel's histogram of n values.
inline void cut(const uint32_t* h, uint32_t n, uint16_t clipPermille, uint8_t& lo, uint8_t& hi) {
    const uint64_t k = static_cast<uint64_t>(n) * clipPermille / 1000u;
    uint64_t acc = 0;
    lo = 0;
    for (int v = 0; v < 256; ++v) {
        acc += h[v];
        if (acc > k) { lo = static_cast<uint8_t>(v); break; }
    }
    acc = 0;
    hi = 255;
    for (int v = 255; v >= 0; --v) {
        acc += h[v];
        if (acc > k) { hi = static_cast<uint8_t>(v); break; }
    }
    if (hi < lo) hi = lo;
}

// widen: a span under kMinSpan grown about its middle.
inline void widen(uint8_t& lo, uint8_t& hi) {
    if (hi - lo >= kMinSpan) return;
    int mid = (lo + hi) / 2;
    int a = mid - kMinSpan / 2, b = mid + kMinSpan / 2;
    if (a < 0) { b -= a; a = 0; }
    if (b > 255) { a -= b - 255; b = 255; }
    lo = static_cast<uint8_t>(a);
    hi = static_cast<uint8_t>(b);
}

// buildTables: the three tables. With levels off (or no histogram) only the
// gamma; with both off, the identity, which the caller need not apply.
inline void buildTables(Hist* h, bool levels, uint8_t gammaT, uint8_t lut[3][256],
                        uint8_t lo[3], uint8_t hi[3]) {
    uint8_t cl = 0, ch = 255;
    if (levels && h && h->n) {
        for (int v = 0; v < 256; ++v) h->all[v] = h->c[0][v] + h->c[1][v] + h->c[2][v];
        cut(h->all, h->n * 3u, kClipPermille, cl, ch);
    }
    const float inv = 10.0f / static_cast<float>(gammaT ? gammaT : 10);
    for (int c = 0; c < 3; ++c) {
        uint8_t l = 0, u = 255;
        if (levels && h && h->n) {
            cut(h->c[c], h->n, kClipPermille, l, u);
            if (l + kCastSpan < cl) l = static_cast<uint8_t>(cl - kCastSpan);
            if (l > cl + kCastSpan) l = static_cast<uint8_t>(cl + kCastSpan);
            if (u + kCastSpan < ch) u = static_cast<uint8_t>(ch - kCastSpan);
            if (u > ch + kCastSpan && ch + kCastSpan <= 255) u = static_cast<uint8_t>(ch + kCastSpan);
            widen(l, u);
        }
        lo[c] = l;
        hi[c] = u;
        for (int v = 0; v < 256; ++v) {
            float x = v <= l ? 0.0f : v >= u ? 1.0f : static_cast<float>(v - l) / static_cast<float>(u - l);
            if (gammaT != 10 && x > 0.0f && x < 1.0f) x = std::pow(x, inv);
            int o = static_cast<int>(x * 255.0f + 0.5f);
            lut[c][v] = static_cast<uint8_t>(o < 0 ? 0 : o > 255 ? 255 : o);
        }
    }
}

// reencodeQuality: the JPEG quality (1-100) a photo is encoded again at,
// for the watermark or the correction, from CONFIG's sensor quality (4-40,
// lower better): high enough that the re-encode never costs the sensor's
// quality back, 90 at the shipped 10.
inline uint8_t reencodeQuality(uint8_t sensorQ) {
    const int q = 100 - sensorQ;
    return static_cast<uint8_t>(q < 60 ? 60 : q > 95 ? 95 : q);
}

// identity: whether a set of tables changes nothing.
inline bool identity(const uint8_t lut[3][256]) {
    for (int c = 0; c < 3; ++c)
        for (int v = 0; v < 256; ++v)
            if (lut[c][v] != v) return false;
    return true;
}

// histRgb565: a raw frame's histogram, high byte first as the driver gives
// it, every step-th pixel of every step-th row (2: a quarter of the frame,
// plenty for percentiles and a quarter of the reading).
inline void histRgb565(const uint8_t* px, uint16_t w, uint16_t h, uint8_t step, Hist& out) {
    memset(&out, 0, sizeof(out));
    if (!px || !step) return;
    for (uint32_t y = 0; y < h; y += step) {
        const uint8_t* row = px + static_cast<size_t>(y) * w * 2u;
        for (uint32_t x = 0; x < w; x += step) {
            const uint8_t hi = row[x * 2u], lo = row[x * 2u + 1u];
            ++out.c[0][hi & 0xF8];
            ++out.c[1][((hi & 0x07) << 5) | ((lo & 0xE0) >> 3)];
            ++out.c[2][(lo & 0x1F) << 3];
            ++out.n;
        }
    }
}

// histRgb888: add a strip of RGB888 pixels to a histogram (the JPEG path's
// eighth-size decode).
inline void histRgb888(const uint8_t* px, size_t count, Hist& out) {
    for (size_t i = 0; i < count; ++i, px += 3) {
        ++out.c[0][px[0]];
        ++out.c[1][px[1]];
        ++out.c[2][px[2]];
        ++out.n;
    }
}

// apply: the tables over rows of RGB888, in place.
inline void apply(const uint8_t lut[3][256], uint8_t* rgb, size_t pixels) {
    for (size_t i = 0; i < pixels; ++i, rgb += 3) {
        rgb[0] = lut[0][rgb[0]];
        rgb[1] = lut[1][rgb[1]];
        rgb[2] = lut[2][rgb[2]];
    }
}

} // namespace campic
