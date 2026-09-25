/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/calllog.h
 * Module:       Core / caller log
 *
 * Purpose:      Caller log: a fixed ring of BBS_CALLLOG_SIZE records in one
 *                  file on the logs partition (<logs>/calls.log). One write per
 *                  logoff, reads one record at a time for LAST. It never grows,
 *                  is not part of the backup zip, and survives uploadfs. The
 *                  newest five and today's count are kept in RAM (1.1.0), so
 *                  the dashboard and the login line open no file.
 *
 *                  File layout: 8-byte header (magic "CLG1", next u16, count u16),
 *                  then BBS_CALLLOG_SIZE fixed-size CallRec slots.
 *
 * Libraries:    none (libc stdio)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     README.md
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
#include "../config.h"

struct CallRec {
    char     user[BBS_USER_MAX + 1] = {};
    char     ip[16]  = {};
    uint8_t  node    = 0;      // 0 = sysop node, BBS_MAX_NODES + 1 = busy line
    uint8_t  term    = 0;      // TermType
    uint8_t  charset = 0;      // Charset
    uint8_t  flags   = 0;      // F_SYSOP, F_GUEST
    uint32_t start   = 0;      // epoch of login, 0 if the clock was not set
    uint32_t secs    = 0;      // call length

    enum : uint8_t { F_SYSOP = 1,        // call was on the sysop node
                     F_GUEST = 2,        // guest call, no account
                     F_RANK_CO = 4,      // account holds a co-sysop rank
                     F_RANK_SYSOP = 8 }; // account holds the sysop rank
};

namespace calllog {

// append: write one record at the ring head
bool append(const CallRec& r);

// count: records stored (up to BBS_CALLLOG_SIZE)
uint8_t count();

// get: back = 0 is the newest record. The newest kRecent come from RAM.
bool get(uint8_t back, CallRec& out);

// countSince: records whose login time is at or after epoch (one file pass)
uint8_t countSince(uint32_t epoch);

// today: calls logged since local midnight, 0 while the clock is not set.
//
// Kept, not counted (1.1.0). One pass over the file the first time it is
// asked on a given day, and append() adds to it after that, so the login
// line and the dashboard read a number instead of opening the log. The
// dashboard used to count the whole file on every frame, which put a file
// open and fifty reads into the loop once a second for a sysop with DASH 1
// up. A day that rolls over, or a timezone that moves midnight, is seen as
// a new start and counted afresh.
uint16_t today();

// kRecent: how many of the newest calls are kept in RAM. get() answers these
// without the file, which is what the dashboard's last calls read.
constexpr uint8_t kRecent = 5;

} // namespace calllog
