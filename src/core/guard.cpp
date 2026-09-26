/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/guard.cpp
 * Module:       Core / abuse tracking
 *
 * Purpose:      Ban list and daily time bank (see guard.h).
 *
 * Libraries:    none (libc)
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

#include "guard.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>

namespace {

bool sameUser(const char* a, const char* b) {
    while (*a && *b) {
        if (toupper(static_cast<unsigned char>(*a)) != toupper(static_cast<unsigned char>(*b))) return false;
        ++a; ++b;
    }
    return *a == *b;
}

} // namespace

// ===========================================================================
// BanList
// ===========================================================================

bool BanList::banned(uint32_t ip, uint32_t now) {
    for (auto& e : slots_) {
        if (e.ip != ip || !e.until) continue;
        if (static_cast<int32_t>(now - e.until) < 0) return true;
        e = Entry();                              // ban expired
    }
    return false;
}

// ---------------------------------------------------------------------------
// fail: count inside the window; reuse the stalest slot when full
// ---------------------------------------------------------------------------
bool BanList::fail(uint32_t ip, uint32_t now) {
    Entry* slot   = nullptr;
    Entry* empty  = nullptr;
    Entry* oldest = nullptr;      // oldest non-banned entry, evicted when full
    for (auto& e : slots_) {
        if (e.ip == ip) { slot = &e; break; }
        if (!e.ip) { if (!empty) empty = &e; continue; }
        if (!e.until && (!oldest || e.firstFail < oldest->firstFail)) oldest = &e;
    }
    if (!slot) {
        slot = empty ? empty : (oldest ? oldest : &slots_[0]);
        *slot = Entry();
        slot->ip = ip;
    }
    if (slot->until) return false;                // already banned

    if (!slot->fails || now - slot->firstFail > BBS_BAN_WINDOW_MS) {
        slot->fails     = 0;
        slot->firstFail = now;
    }
    if (++slot->fails >= BBS_BAN_TRIES) {
        slot->until = now + BBS_BAN_MS;
        if (!slot->until) slot->until = 1;       // 0 means "not banned"
        return true;
    }
    return false;
}

bool BanList::clear(uint32_t ip) {
    for (auto& e : slots_) {
        if (e.ip == ip) { e = Entry(); return true; }
    }
    return false;
}

bool BanList::at(uint8_t i, uint32_t now, Entry& out) const {
    if (i >= BBS_BAN_SLOTS) return false;
    const Entry& e = slots_[i];
    if (!e.ip || !e.until || static_cast<int32_t>(now - e.until) >= 0) return false;
    out = e;
    return true;
}

// ===========================================================================
// LoginGuard
// ===========================================================================

bool LoginGuard::locked(const char* handle, uint32_t now) {
    for (auto& e : slots_) {
        if (!e.until || !sameUser(e.handle, handle)) continue;
        if (static_cast<int32_t>(now - e.until) < 0) return true;
        e = Entry();                              // lock expired
    }
    return false;
}

// ---------------------------------------------------------------------------
// fail: count inside the window; reuse an empty or the oldest slot
// ---------------------------------------------------------------------------
bool LoginGuard::fail(const char* handle, uint32_t now) {
    Entry* slot  = nullptr;
    Entry* empty = nullptr;
    Entry* stale = nullptr;                          // window or lock has run out
    for (auto& e : slots_) {
        if (e.handle[0] && sameUser(e.handle, handle)) { slot = &e; break; }
        if (!e.handle[0]) { if (!empty) empty = &e; continue; }
        bool done = e.until ? static_cast<int32_t>(now - e.until) >= 0
                            : now - e.firstFail > BBS_LOCK_WINDOW_MS;
        if (done && (!stale || e.firstFail < stale->firstFail)) stale = &e;
    }
    if (!slot) {
        slot = empty ? empty : stale;
        // every slot holds a live counter or lock: never evict one, since
        // that is how an attacker would clear a victim's failures
        if (!slot) return true;                      // treat as locked: hang this call up
        *slot = Entry();
        strncpy(slot->handle, handle, BBS_USER_MAX);
    }
    if (slot->until) return false;
    if (!slot->fails || now - slot->firstFail > BBS_LOCK_WINDOW_MS) {
        slot->fails     = 0;
        slot->firstFail = now;
    }
    if (++slot->fails >= BBS_LOCK_FAILS) {
        slot->until = now + BBS_LOCK_MS;
        if (!slot->until) slot->until = 1;
        return true;
    }
    return false;
}

void LoginGuard::clear(const char* handle) {
    for (auto& e : slots_) {
        if (e.handle[0] && sameUser(e.handle, handle)) e = Entry();
    }
}

// ===========================================================================
// IP text helpers
// ===========================================================================

void ipToText(uint32_t ip, char* out, size_t n) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&ip);
    snprintf(out, n, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}

bool ipFromText(const char* s, uint32_t& out) {
    uint8_t b[4];
    for (int i = 0; i < 4; ++i) {
        if (!isdigit(static_cast<unsigned char>(*s))) return false;
        char* end = nullptr;
        long v = strtol(s, &end, 10);
        if (v < 0 || v > 255) return false;
        b[i] = static_cast<uint8_t>(v);
        s = end;
        if (i < 3) {
            if (*s != '.') return false;
            ++s;
        }
    }
    if (*s && *s != ' ') return false;
    memcpy(&out, b, 4);
    return true;
}

// ---------------------------------------------------------------------------
// localNet: the one rule for "on the board's own network" (1.1.1).
//
// There were two: the shell's (Bbs::localAddr, on the dotted quad, which
// took 127.0.0.1 exactly) and the backup port's (on the raw address, which
// took all of 127/8), and both counted 100.64/10 unconditionally. That range
// is the carrier's shared space as much as Tailscale's, so since 1.1.1 it is
// local only when CONFIG network says so (SysConfig::cgnatLocal).
//
// Loopback is 127.0.0.1 alone. A board is never called over loopback; the
// host harness is, and it calls from 127.0.0.2 to stand for "from outside".
// ---------------------------------------------------------------------------
bool localNet(uint32_t netOrder, bool cgnat) {
    const uint8_t* o = reinterpret_cast<const uint8_t*>(&netOrder);
    if (o[0] == 127) return o[1] == 0 && o[2] == 0 && o[3] == 1;
    return o[0] == 10 ||
           (o[0] == 172 && (o[1] & 0xF0) == 16) ||
           (o[0] == 192 && o[1] == 168) ||
           (o[0] == 169 && o[1] == 254) ||                        // link local
           (cgnat && o[0] == 100 && (o[1] & 0xC0) == 64);         // 100.64/10
}

bool cgnatAddr(uint32_t netOrder) {
    const uint8_t* o = reinterpret_cast<const uint8_t*>(&netOrder);
    return o[0] == 100 && (o[1] & 0xC0) == 64;
}

// peerAddr: the address a caller is taken to have come from. The board's is
// the socket's. The host build has no carrier NAT to call from, so there a
// caller from 127.0.0.3 stands for one at 100.64.0.3 (test_cgnat_local),
// and nothing else changes.
uint32_t peerAddr(uint32_t netOrder) {
#ifdef BBS_HOST
    const uint8_t* o = reinterpret_cast<const uint8_t*>(&netOrder);
    if (o[0] == 127 && o[1] == 0 && o[2] == 0 && o[3] == 3) {
        const uint8_t cg[4] = { 100, 64, 0, 3 };
        uint32_t v;
        memcpy(&v, cg, 4);
        return v;
    }
#endif
    return netOrder;
}
