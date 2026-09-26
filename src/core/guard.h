/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/guard.h
 * Module:       Core / abuse tracking
 *
 * Purpose:      Abuse and usage tracking, all in fixed static tables.
 *
 *                  BanList   wrong sysop passwords per IP. BBS_BAN_TRIES failures inside
 *                  BBS_BAN_WINDOW_MS ban that IP for BBS_BAN_MS. RAM only, a
 *                  reboot clears it.
 *                  LoginGuard wrong account passwords per handle. BBS_LOCK_FAILS inside
 *                  BBS_LOCK_WINDOW_MS lock that handle for BBS_LOCK_MS. RAM
 *                  only: failures never write to flash.
 *
 *                  IPs are the raw 4-byte s_addr value, same byte order as the socket.
 *
 * Libraries:    none
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
#include <cstddef>
#include "../config.h"

class BanList {
public:
    struct Entry {
        uint32_t ip        = 0;
        uint8_t  fails     = 0;
        uint32_t firstFail = 0;
        uint32_t until     = 0;     // 0 = not banned
    };

    // banned: true while an active ban covers ip
    bool banned(uint32_t ip, uint32_t now);

    // fail: count a wrong password. True if this failure started a ban.
    bool fail(uint32_t ip, uint32_t now);

    // clear: forget ip (correct password or UNBAN). False if not listed.
    bool clear(uint32_t ip);

    // at: entry i if it is an active ban (for BANS)
    bool at(uint8_t i, uint32_t now, Entry& out) const;

private:
    Entry slots_[BBS_BAN_SLOTS];
};

class LoginGuard {
public:
    // locked: true while the handle is locked out
    bool locked(const char* handle, uint32_t now);

    // fail: count a wrong password. True if this failure locked the handle.
    bool fail(const char* handle, uint32_t now);

    // clear: a correct password forgets earlier failures
    void clear(const char* handle);

private:
    struct Entry {
        char     handle[BBS_USER_MAX + 1] = {};
        uint8_t  fails     = 0;
        uint32_t firstFail = 0;
        uint32_t until     = 0;     // 0 = not locked
    };
    Entry slots_[BBS_LOCK_SLOTS];
};

// ip helpers shared by the shell (dotted quad <-> raw s_addr bytes)
void ipToText(uint32_t ip, char* out, size_t n);
bool ipFromText(const char* s, uint32_t& out);

// localNet: is this address (network order) on the board's own network?
// RFC 1918, link local, 127.0.0.1, and 100.64/10 when cgnat is set (CONFIG
// network, SysConfig::cgnatLocal). The one rule the shell and the backup
// port both ask (1.1.1).
bool localNet(uint32_t netOrder, bool cgnat);

// cgnatAddr: in 100.64.0.0/10, so that a refusal can say which setting
// would let it in (a sysop on Tailscale after 1.1.1 turned it off).
bool cgnatAddr(uint32_t netOrder);

// peerAddr: a socket's peer as the board takes it. The same address on a
// board; the host build maps 127.0.0.3 to 100.64.0.3 for its tests.
uint32_t peerAddr(uint32_t netOrder);
