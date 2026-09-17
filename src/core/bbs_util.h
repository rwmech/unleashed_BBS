/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/bbs_util.h
 * Module:       Core / shared helpers
 *
 * Purpose:      Small helpers shared by bbs.cpp, bbs_shell.cpp and
 *                  bbs_sysop.cpp. Internal to the core, not a plugin API.
 *
 * Libraries:    none (libc)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     COMMANDS.md
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
#include <cctype>
#include <cstdint>
#include <cstdio>
#include "bbs.h"
#include "calllog.h"

namespace bbsu {

// ieq: case-insensitive string equality
inline bool ieq(const char* a, const char* b) {
    while (*a && *b) {
        if (toupper(static_cast<unsigned char>(*a)) != toupper(static_cast<unsigned char>(*b))) return false;
        ++a; ++b;
    }
    return *a == *b;
}

// nodeChar: '1'..'6' for callers, 'S' sysop node, 'B' busy line
inline char nodeChar(const Session& s) {
    if (s.role == Role::Sysop) return 'S';
    if (s.role == Role::Busy)  return 'B';
    return static_cast<char>('0' + s.id);
}

// fmtIdle: mm:ss, minutes capped at 99
inline void fmtIdle(char* out, size_t n, uint32_t ms) {
    uint32_t secs = ms / 1000u;
    uint32_t m    = secs / 60u;
    if (m > 99) { m = 99; secs = 99u * 60u + 59u; }
    snprintf(out, n, "%02u:%02u", static_cast<unsigned>(m), static_cast<unsigned>(secs % 60u));
}

// can: session holds a staff permission (Perm bit)
inline bool can(const Session& s, uint16_t bit) {
    return (s.perms & bit) != 0;
}

// outranks: a may act on b (KICK, SNOOP) only from a higher staff level
inline bool outranks(const Session& a, const Session& b) {
    return static_cast<uint8_t>(a.level) > static_cast<uint8_t>(b.level);
}

// isAbortKey: stops a listing, a screen or the FX demo
inline bool isAbortKey(int k) {
    return k == ' ' || k == KEY_ESC || k == KEY_BREAK;
}

// markFor: the DDial-style marker printed between the node number and the
// handle. Guests are '*', co-sysops '>', the sysop ']', callers a space.
// It follows the account's staff rank, so it shows before they elevate too.
inline char markFor(const Session& s) {
    if (s.guest) return '*';
    uint8_t r = s.rank > static_cast<uint8_t>(s.level) ? s.rank : static_cast<uint8_t>(s.level);
    if (r >= static_cast<uint8_t>(Access::Sysop)) return ']';
    return r ? '>' : ' ';
}

// markForFlags: the same marker for a caller-log record
inline char markForFlags(uint8_t flags) {
    if (flags & CallRec::F_GUEST)      return '*';
    if (flags & CallRec::F_RANK_SYSOP) return ']';
    if (flags & CallRec::F_RANK_CO)    return '>';
    return ' ';
}

// listHandle: a handle cut to a list column
inline void listHandle(char* out, size_t n, const char* user, int width) {
    snprintf(out, n, "%.*s", width, user);
}

constexpr const char kMarkKey[]     = "*GUEST  >CO-SYSOP  ]SYSOP";
constexpr const char kMoreText[]    = "[More] Y/n/c ";
constexpr uint8_t    kMoreLen       = sizeof(kMoreText) - 1;
constexpr const char kConfirmText[] = "Log off (Y/N)? ";

} // namespace bbsu
