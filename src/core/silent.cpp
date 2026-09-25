/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/silent.cpp
 * Module:       Core / silent mode (1.1.0)
 *
 * Purpose:      The board's silent state: the switch and the silent hours
 *               from the live settings, looked at no more than once a second,
 *               and the answer kept in one byte for everything that lights
 *               (see silent.h).
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E, ESP32-S3 (ESP-IDF 5.3.1) and the Linux host
 * See also:     src/core/silent.h
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

#include "silent.h"
#include "clock.h"
#include "sysconfig.h"
#include "../platform/platform.h"

namespace {

constexpr uint32_t kEveryMs = 1000;     // the hours are minutes: once a second is plenty

board::Quiet g_why    = board::Quiet::Off;
bool         g_looked = false;          // silentTick has run at least once
uint32_t     g_at     = 0;              // when it last looked

} // namespace

namespace board {

bool silent() {
    return g_why != Quiet::Off;
}

Quiet silentWhy() {
    return g_why;
}

void silentTick(uint32_t now, bool force) {
    if (!force && g_looked && now - g_at < kEveryMs) return;
    const bool first = !g_looked;
    g_looked = true;
    g_at     = now;

    const SysConfig& c = syscfg::get();
    // The clock only when there are hours to judge: a localtime a second on a
    // board with none set is work for nothing.
    const int16_t minute = (c.silentFrom >= 0 && c.silentUntil >= 0) ? clk::minuteOfDay() : kNoTime;
    const Quiet why = rule(c.silent, c.silentFrom, c.silentUntil, minute);
    if (why == g_why && !first) return;

    const bool was = g_why != Quiet::Off;
    g_why = why;
    const bool on = why != Quiet::Off;
    if (on != was || first) plat::ledSilent(on);
    if (first && !on) return;                    // a board that was never silent says nothing
    if (!on) {
        plat::log("silent: off, the lights are back");
    } else if (why == Quiet::Switch) {
        plat::log("silent: on, by the switch in CONFIG board");
    } else {
        char until[6];
        fmtTime(c.silentUntil, until, sizeof(until));
        plat::log("silent: on, silent hours until %s", until);
    }
}

} // namespace board
