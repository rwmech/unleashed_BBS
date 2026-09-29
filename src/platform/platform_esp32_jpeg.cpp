/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/platform/platform_esp32_jpeg.cpp
 * Module:       Platform / the panel's JPEG decoder
 *
 * Purpose:      jpegDecode for the panel's skins: the TJpgDec in the
 *               ESP32-S3's ROM, which costs no flash. Called only from a
 *               skin's load, a job on the background runner (core/runner.h),
 *               never from the loop.
 *
 * Design:       The ROM's R0.01 (esp32s3/rom/tjpgd.h, and jd_prepare and
 *               jd_decomp PROVIDEd at 0x40000858 and 0x40000864 in
 *               esp32s3.rom.ld), fixed at RGB888 out and a 512-byte input
 *               buffer. What it cannot take is refused before it is called
 *               (plugins/skin_jpeg.h).
 *
 * Libraries:    ESP-IDF 5.3.1 (heap_caps, the ROM's TJpgDec)
 * Targets:      ESP32-S3 boards with a panel (BBS_HAS_LCD)
 * See also:     src/platform/platform.h, src/plugins/skin.cpp,
 *               host/jpeg_host.cpp (the host's)
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
#include "platform.h"
#include "../config.h"

#ifdef BBS_HAS_LCD

#include "esp_heap_caps.h"
#if CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/tjpgd.h"             // the ROM's JPEG decoder: no flash
#else
#include "esp32/rom/tjpgd.h"
#endif

namespace plat {

// ---------------------------------------------------------------------------
// jpegDecode: the ROM's TJpgDec, its work area and object on the worker's
// heap for the length of the call.
// ---------------------------------------------------------------------------
namespace {

// TJpgDec's work area: its tables, the MCU and the output block. ChaN's
// own figure is 3,100 bytes for most files; four full quantisation tables
// and two optimised AC Huffman tables at 4:2:0 come to about 4,200, so the
// area is 5 KB and a JDR_MEM1 is never the file's fault.
constexpr size_t kPool = 5120;

struct Dec {
    JpegRead rd;
    JpegPut  put;
    void*    ctx;
};

UINT decIn(JDEC* jd, BYTE* buf, UINT n) {
    Dec* d = static_cast<Dec*>(jd->device);
    return static_cast<UINT>(d->rd(d->ctx, buf, n));
}

UINT decOut(JDEC* jd, void* bitmap, JRECT* r) {
    Dec* d = static_cast<Dec*>(jd->device);
    return d->put(d->ctx, r->left, r->top, static_cast<uint16_t>(r->right - r->left + 1),
                  static_cast<uint16_t>(r->bottom - r->top + 1), static_cast<const uint8_t*>(bitmap)) ? 1 : 0;
}

}   // namespace

int jpegDecode(JpegRead rd, JpegPut put, void* ctx, uint16_t& width, uint16_t& height) {
    width = height = 0;
    void* pool = heap_caps_malloc(kPool, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    JDEC* jd   = static_cast<JDEC*>(heap_caps_malloc(sizeof(JDEC), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    int rc = JDR_MEM1;
    if (pool && jd) {
        Dec d = { rd, put, ctx };
        rc = jd_prepare(jd, decIn, pool, kPool, &d);
        if (rc == JDR_OK) {
            width  = static_cast<uint16_t>(jd->width);
            height = static_cast<uint16_t>(jd->height);
            rc = jd_decomp(jd, decOut, 0);
        }
    }
    heap_caps_free(jd);
    heap_caps_free(pool);
    return rc;
}

}   // namespace plat

#endif  // BBS_HAS_LCD
