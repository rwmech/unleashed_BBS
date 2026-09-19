/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/clock.h
 * Module:       Core / wall clock
 *
 * Purpose:      Wall clock helpers. Time comes from NTP on the ESP32 and
 *                  from the OS on the host; until the clock is set every
 *                  formatter prints "--" so nothing shows 1970.
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

#pragma once
#include <cstdint>
#include <cstddef>

namespace clk {

// valid: true once the wall clock has been set
bool valid();

// epoch: seconds since 1970, 0 when not valid
uint32_t epoch();

// fmt: strftime of the current local time; "--" when not valid
size_t fmt(char* buf, size_t n, const char* strftimeFmt);

// fmtEpoch: strftime of a stored epoch; "--" when e is 0
size_t fmtEpoch(char* buf, size_t n, const char* strftimeFmt, uint32_t e);

// todayStart: epoch of local midnight today, 0 when the clock is not set
// utcOffset: minutes east of UTC right now, daylight saving included,
// or 0 when the clock has never been set. Sent to a directory so it can
// describe a board's busy hours in local time instead of UTC.
int16_t utcOffset();

uint32_t todayStart();

// dayKey: local calendar day (YYYYDDD). Before NTP sync, a day number
// derived from uptime so daily limits still reset.
uint32_t dayKey(uint32_t millisNow);

} // namespace clk
