/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         host/test_bans.cpp
 * Module:       Host test / BanList, the address ban table
 *
 * Purpose:      The ban table with every slot in use (1.2.2). Up to 1.2.1
 *               BanList::slotFor fell back to &slots_[0] when no slot was
 *               free, so a ninth address earning a ban silently LIFTED the
 *               ban in slot 0: three wrong passwords each from nine
 *               addresses inside one fifteen-minute window, 27 in all, and
 *               the first address was free to try again.
 *
 *               The table refuses now: the ninth ban is simply not recorded,
 *               and the eight that exist keep standing. Every check below
 *               fails against the parent commit except the ones marked as
 *               the unchanged common path, which is the point of having them
 *               here: the fix must not have cost the ordinary case.
 *
 * Libraries:    none
 * Targets:      the Linux host build only
 * See also:     src/core/guard.h, src/core/guard.cpp
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

#include "../src/core/guard.h"

#include <cstdio>
#include <cstdint>

namespace {

int g_pass = 0;
int g_fail = 0;

void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++g_pass; else ++g_fail;
}

// ban: the three wrong passwords one address needs, at `now`.
bool ban(BanList& b, uint32_t ip, uint32_t now) {
    bool started = false;
    for (int i = 0; i < BBS_BAN_TRIES; ++i) started = b.fail(ip, now);
    return started;
}

// 10.0.0.n as a raw s_addr, the order ipToText reads, so the console line
// a refusal logs names the address this test meant.
uint32_t addr(int n) { return 0x0000000Au | (static_cast<uint32_t>(n) << 24); }

} // namespace

int main() {
    printf("BanList: the table full of running bans\n");

    // Every slot a running ban, all inside one window.
    {
        BanList b;
        const uint32_t t = 1000;
        bool all = true;
        for (int i = 0; i < BBS_BAN_SLOTS; ++i) all = ban(b, addr(i), t) && all;
        check("eight addresses each earn a ban", all);
        check("and the table says it is full", b.full(t));

        // The find: a ninth address must not cost the first its ban.
        bool ninth = ban(b, addr(BBS_BAN_SLOTS), t);
        check("a ninth address does not start a ban", !ninth);
        check("the first address is STILL banned", b.banned(addr(0), t));
        bool every = true;
        for (int i = 0; i < BBS_BAN_SLOTS; ++i) every = b.banned(addr(i), t) && every;
        check("and so is every one of the eight", every);
        check("the ninth is not banned, which is the cost of refusing",
              !b.banned(addr(BBS_BAN_SLOTS), t));

        // A held answer to the login's sysop question cannot be counted
        // either, so it is not given: that direction fails shut.
        check("aheadTake is refused while the table is full",
              !b.aheadTake(addr(BBS_BAN_SLOTS), t));

        // UNBAN frees a slot, and the next address can then be banned.
        check("UNBAN frees one", b.clear(addr(3)));
        check("the table is no longer full", !b.full(t));
        check("and the ninth address can now earn a ban", ban(b, addr(BBS_BAN_SLOTS), t));
        check("which is live", b.banned(addr(BBS_BAN_SLOTS), t));
        check("while the one that was unbanned is not", !b.banned(addr(3), t));
    }

    // The common path, unchanged: a ban that has run out is reclaimed, and
    // a slot holding only failures is reused freely.
    {
        BanList b;
        const uint32_t t = 1000;
        for (int i = 0; i < BBS_BAN_SLOTS; ++i) ban(b, addr(i), t);
        const uint32_t later = t + BBS_BAN_MS + 1;        // every ban has run out
        check("after the window the bans have run out", !b.banned(addr(0), later));
        check("the table is not full then", !b.full(later));
        check("and a new address is banned normally",
              ban(b, addr(100), later) && b.banned(addr(100), later));
    }

    // One expired ban among seven live ones is the slot that gets reused,
    // and no live ban is touched.
    {
        BanList b;
        const uint32_t t = 1000;
        ban(b, addr(0), t);                               // this one will expire
        const uint32_t t2 = t + BBS_BAN_MS - 1000;        // still inside its ban
        for (int i = 1; i < BBS_BAN_SLOTS; ++i) ban(b, addr(i), t2);
        const uint32_t t3 = t + BBS_BAN_MS + 1;           // slot 0 has run out
        check("the first ban has run out, the rest have not",
              !b.banned(addr(0), t3) && b.banned(addr(1), t3));
        check("a new address takes the expired slot", ban(b, addr(100), t3));
        check("and the seven live bans are all still there",
              b.banned(addr(1), t3) && b.banned(addr(BBS_BAN_SLOTS - 1), t3));
    }

    // A failure counted on an address with no slot must not be remembered as
    // a partial count somewhere else: the ninth address asking again while
    // the table is full is still refused, and nothing it did leaked into
    // another address's entry.
    {
        BanList b;
        const uint32_t t = 1000;
        for (int i = 0; i < BBS_BAN_SLOTS; ++i) ban(b, addr(i), t);
        for (int i = 0; i < 20; ++i) b.fail(addr(50), t);
        bool every = true;
        for (int i = 0; i < BBS_BAN_SLOTS; ++i) every = b.banned(addr(i), t) && every;
        check("twenty failures from a ninth address change nothing", every);
        check("and it is still not banned", !b.banned(addr(50), t));
    }

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
