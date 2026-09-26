/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         host/jpeg_host.cpp
 * Module:       Host platform / the panel's JPEG decoder
 *
 * Purpose:      plat::jpegDecode on the host, for a board profile with a
 *               panel (BBS_HAS_LCD): ChaN's TJpgDec R0.03 in host/tjpgd,
 *               configured as the ESP32-S3's ROM copy is fixed, so a panel
 *               skin is decoded on the host by the same code, give or take
 *               a revision, that draws it on the board. What R0.03 takes
 *               and the ROM's R0.01 does not (greyscale above all) is
 *               refused first by skin::checkJpeg on both.
 *
 * Libraries:    TJpgDec R0.03 (C)ChaN, host/tjpgd (THIRD_PARTY_NOTICES.md)
 * Targets:      the Linux host build
 * See also:     src/platform/platform_esp32_task.cpp (the board's)
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
#include "platform/platform.h"
#include "config.h"

#ifdef BBS_HAS_LCD

#include "tjpgd/tjpgd.h"
#include <cstdlib>

namespace {

// As the board's (platform_esp32_task.cpp), with room for R0.03's larger
// tables.
constexpr size_t kPool = 8192;

struct Dec {
    plat::JpegRead rd;
    plat::JpegPut  put;
    void*          ctx;
};

size_t decIn(JDEC* jd, uint8_t* buf, size_t n) {
    Dec* d = static_cast<Dec*>(jd->device);
    return d->rd(d->ctx, buf, n);
}

int decOut(JDEC* jd, void* bitmap, JRECT* r) {
    Dec* d = static_cast<Dec*>(jd->device);
    return d->put(d->ctx, r->left, r->top, static_cast<uint16_t>(r->right - r->left + 1),
                  static_cast<uint16_t>(r->bottom - r->top + 1), static_cast<const uint8_t*>(bitmap)) ? 1 : 0;
}

} // namespace

namespace plat {

int jpegDecode(JpegRead rd, JpegPut put, void* ctx, uint16_t& width, uint16_t& height) {
    width = height = 0;
    void* pool = malloc(kPool);
    JDEC  jd;
    int   rc = JDR_MEM1;
    if (pool) {
        Dec d = { rd, put, ctx };
        rc = jd_prepare(&jd, decIn, pool, kPool, &d);
        if (rc == JDR_OK) {
            width  = static_cast<uint16_t>(jd.width);
            height = static_cast<uint16_t>(jd.height);
            rc = jd_decomp(&jd, decOut, 0);
        }
    }
    free(pool);
    return rc;
}

} // namespace plat

#endif  // BBS_HAS_LCD
