/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/skin_jpeg.h
 * Module:       Plugins / panel skins (BBS_HAS_LCD boards, and the host tests)
 *
 * Purpose:      A skin's background.jpg read as far as its first scan, and
 *               refused, with the reason in words, when the decoder the
 *               board has cannot draw it. No platform and no heap.
 *
 * Design:       The ESP32-S3 decodes with the TJpgDec in its ROM
 *               (esp32s3/rom/tjpgd.h, jd_prepare at 0x40000858 and
 *               jd_decomp at 0x40000864 in esp32s3.rom.ld): ChaN's R0.01 of
 *               2012, fixed at a 512-byte input buffer and RGB888 output.
 *               R0.01 takes:
 *                 - baseline sequential Huffman only (SOF0). Progressive,
 *                   extended, lossless and arithmetic coded files are
 *                   refused (JDR_FMT3);
 *                 - three components, Y Cb Cr, and nothing else: greyscale
 *                   came in R0.02 (JDR_FMT3);
 *                 - luma sampled 1x1, 2x1 or 2x2 (4:4:4, 4:2:2, 4:2:0) and
 *                   both chroma components 1x1 (JDR_FMT3);
 *                 - quantisation table ids 0 to 3, Huffman table ids 0 and 1;
 *                 - every table and frame segment it loads no longer than
 *                   its 512-byte buffer (JDR_MEM2).
 *               The host's decoder is R0.03, which accepts more (greyscale
 *               above all), so this check runs on both before either
 *               decodes: a skin the host draws is a skin the board draws.
 *               8-bit samples too, which R0.01 assumes without checking.
 *
 *               Everything else in the file is skipped: APPn (EXIF, JFIF,
 *               ICC, a thumbnail), COM. Only the frame's size is kept, for
 *               the loader to hold against the panel.
 *
 * Libraries:    none
 * Targets:      ESP32-S3 boards with BBS_HAS_LCD, and the Linux host tests
 * See also:     SKINS.md, src/plugins/skin.cpp, tools/mkskin.py (the same
 *               rules, for a sysop at a PC)
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

namespace skin {

// A stream of the file: fill buf with up to n bytes and say how many; buf
// null means skip n. Short only at the end of the file.
using ReadFn = size_t (*)(void* ctx, uint8_t* buf, size_t n);

constexpr size_t kJpegSegMax = 512;      // R0.01's JD_SZBUF

struct JpegInfo {
    uint16_t w = 0, h = 0;
    uint8_t  sampling = 0;               // luma's, 0x11, 0x21 or 0x22
};

namespace jdetail {

inline bool fail(char* err, size_t n, const char* why) {
    if (err && n) snprintf(err, n, "%s", why);
    return false;
}

inline bool byte(ReadFn rd, void* ctx, uint8_t& b) { return rd(ctx, &b, 1) == 1; }

inline bool word(ReadFn rd, void* ctx, uint16_t& w) {
    uint8_t b[2];
    if (rd(ctx, b, 2) != 2) return false;
    w = static_cast<uint16_t>((b[0] << 8) | b[1]);
    return true;
}

} // namespace jdetail

// ---------------------------------------------------------------------------
// checkJpeg: the file from its start to its first scan. True with the frame
// in info when the ROM decoder takes it; false with the reason in err.
// ---------------------------------------------------------------------------
inline bool checkJpeg(ReadFn rd, void* ctx, JpegInfo& info, char* err, size_t errLen) {
    using namespace jdetail;
    info = JpegInfo();
    uint8_t a, b;
    if (!byte(rd, ctx, a) || !byte(rd, ctx, b) || a != 0xFF || b != 0xD8)
        return fail(err, errLen, "not a JPEG (no FF D8 at the start)");
    bool frame = false, dqt = false, dht = false;
    for (int segs = 0; segs < 256; ++segs) {
        // A marker: FF and its code. R0.01 reads a marker and its length as
        // four bytes with no fill FFs between segments, and refuses a length
        // of 2 or less, so both are refused here too, or the check would
        // pass a file the ROM then will not draw.
        if (!byte(rd, ctx, a)) return fail(err, errLen, "the file ends before its picture");
        if (a != 0xFF) return fail(err, errLen, "damaged: a segment does not start with FF");
        if (!byte(rd, ctx, b)) return fail(err, errLen, "the file ends before its picture");
        if (b == 0xFF) return fail(err, errLen, "fill bytes between segments: save it again as baseline");
        if (b == 0xD8 || b == 0x01 || (b >= 0xD0 && b <= 0xD7))
            return fail(err, errLen, "damaged: a marker out of place before the picture");
        if (b == 0xD9) return fail(err, errLen, "the file ends before its picture");
        uint16_t len;
        if (!word(rd, ctx, len) || len < 2) return fail(err, errLen, "damaged: a segment's length is wrong");
        if (len == 2) return fail(err, errLen, "an empty segment the decoder refuses: save it again");
        const uint16_t body = static_cast<uint16_t>(len - 2);

        switch (b) {
            case 0xC0: {                                   // SOF0, baseline
                if (frame) return fail(err, errLen, "damaged: two frames");
                if (body > kJpegSegMax) return fail(err, errLen, "the frame header is too long for the decoder");
                uint8_t seg[kJpegSegMax];
                if (rd(ctx, seg, body) != body) return fail(err, errLen, "the file ends in its frame header");
                if (body < 6) return fail(err, errLen, "damaged: a frame header too short");
                if (seg[0] != 8) return fail(err, errLen, "12-bit samples: save it as an ordinary 8-bit JPEG");
                info.h = static_cast<uint16_t>((seg[1] << 8) | seg[2]);
                info.w = static_cast<uint16_t>((seg[3] << 8) | seg[4]);
                if (!info.w || !info.h) return fail(err, errLen, "the picture has no size in its header");
                if (seg[5] == 1) return fail(err, errLen, "greyscale: save it in colour (YCbCr)");
                if (seg[5] != 3) return fail(err, errLen, "not three colour components (CMYK?): save it as RGB");
                if (body < 6 + 9) return fail(err, errLen, "damaged: a frame header too short");
                for (int i = 0; i < 3; ++i) {
                    const uint8_t s = seg[7 + 3 * i], q = seg[8 + 3 * i];
                    if (!i) {
                        if (s != 0x11 && s != 0x21 && s != 0x22)
                            return fail(err, errLen, "chroma sampling the decoder cannot do: use 4:4:4, 4:2:2 or 4:2:0");
                        info.sampling = s;
                    } else if (s != 0x11) {
                        return fail(err, errLen, "chroma sampling the decoder cannot do: use 4:4:4, 4:2:2 or 4:2:0");
                    }
                    if (q > 3) return fail(err, errLen, "damaged: a quantisation table id past 3");
                }
                frame = true;
                break;
            }
            case 0xC2: return fail(err, errLen, "progressive JPEG: save it as baseline (not progressive)");
            case 0xC1: return fail(err, errLen, "extended JPEG: save it as baseline");
            case 0xC3: case 0xC5: case 0xC6: case 0xC7:
            case 0xC9: case 0xCA: case 0xCB: case 0xCD: case 0xCE: case 0xCF:
                return fail(err, errLen, "a kind of JPEG the decoder cannot do: save it as baseline");
            case 0xC4:                                     // DHT
            case 0xDB: {                                   // DQT
                if (body > kJpegSegMax)
                    return fail(err, errLen, b == 0xC4 ? "a Huffman table segment longer than the decoder's 512 bytes"
                                                       : "a quantisation segment longer than the decoder's 512 bytes");
                uint8_t seg[kJpegSegMax];
                if (rd(ctx, seg, body) != body) return fail(err, errLen, "the file ends in a table");
                // Walk the tables in the segment for their ids.
                size_t p = 0;
                while (p < body) {
                    const uint8_t id = seg[p];
                    if (b == 0xDB) {
                        if ((id >> 4) != 0) return fail(err, errLen, "16-bit quantisation tables: save it as baseline");
                        if ((id & 15) > 3) return fail(err, errLen, "damaged: a quantisation table id past 3");
                        p += 1 + 64;
                        dqt = true;
                    } else {
                        if ((id >> 4) > 1 || (id & 15) > 1)
                            return fail(err, errLen, "a Huffman table id the decoder cannot do");
                        if (p + 17 > body) return fail(err, errLen, "damaged: a Huffman table cut short");
                        size_t codes = 0;
                        for (int k = 1; k <= 16; ++k) codes += seg[p + k];
                        p += 17 + codes;
                        dht = true;
                    }
                }
                if (p != body) return fail(err, errLen, "damaged: a table segment's length is wrong");
                break;
            }
            case 0xDD:                                     // DRI
                if (body != 2) return fail(err, errLen, "damaged: a restart interval segment is wrong");
                if (rd(ctx, nullptr, body) != body) return fail(err, errLen, "the file ends early");
                break;
            case 0xDA: {                                   // SOS: the picture starts
                if (!frame) return fail(err, errLen, "damaged: the picture starts before its frame header");
                if (!dqt || !dht) return fail(err, errLen, "no Huffman or quantisation tables before the picture");
                uint8_t n;
                if (body < 1 || !byte(rd, ctx, n)) return fail(err, errLen, "the file ends at its picture");
                if (n != 3) return fail(err, errLen, "the picture is not in one scan of three components");
                return true;
            }
            default:                                       // APPn, COM and the rest: not ours
                if (rd(ctx, nullptr, body) != body) return fail(err, errLen, "the file ends early");
                break;
        }
    }
    return fail(err, errLen, "damaged: too many segments before the picture");
}

} // namespace skin
