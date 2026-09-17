/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/clock.cpp
 * Module:       Core / wall clock
 *
 * Purpose:      Wall clock helpers (see clock.h).
 *
 * Libraries:    none (libc time)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     README.md
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

#include "clock.h"
#include <ctime>
#include <cstdio>

namespace {
constexpr time_t kValidAfter = 1700000000;   // Nov 2023: anything earlier is unset
}

namespace clk {

bool valid() {
    return time(nullptr) > kValidAfter;
}

uint32_t epoch() {
    time_t t = time(nullptr);
    return t > kValidAfter ? static_cast<uint32_t>(t) : 0;
}

size_t fmtEpoch(char* buf, size_t n, const char* strftimeFmt, uint32_t e) {
    if (!n) return 0;
    if (!e) return static_cast<size_t>(snprintf(buf, n, "--"));
    time_t t = static_cast<time_t>(e);
    struct tm lt;
    localtime_r(&t, &lt);
    size_t w = strftime(buf, n, strftimeFmt, &lt);
    if (!w) buf[0] = '\0';
    return w;
}

size_t fmt(char* buf, size_t n, const char* strftimeFmt) {
    return fmtEpoch(buf, n, strftimeFmt, epoch());
}

uint32_t todayStart() {
    uint32_t e = epoch();
    if (!e) return 0;
    time_t t = static_cast<time_t>(e);
    struct tm lt;
    localtime_r(&t, &lt);
    lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
    lt.tm_isdst = -1;
    time_t mid = mktime(&lt);
    return mid > 0 ? static_cast<uint32_t>(mid) : 0;
}

uint32_t dayKey(uint32_t millisNow) {
    uint32_t e = epoch();
    if (!e) return millisNow / 86400000u + 1u;
    time_t t = static_cast<time_t>(e);
    struct tm lt;
    localtime_r(&t, &lt);
    return static_cast<uint32_t>((lt.tm_year + 1900) * 1000 + lt.tm_yday);
}

} // namespace clk
