/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         host/test_since.cpp
 * Module:       Host tests / elapsed time (1.2.1)
 *
 * Purpose:      plat::since with no board in it. A stamp a little ahead of
 *               now (taken later in the pass, or on another task) is 0, and
 *               a real gap keeps the clock's whole 49.7 days, past the 24.8
 *               a signed difference turns negative. Across the wrap too.
 *
 * Libraries:    none
 * Targets:      Linux host build
 * See also:     src/platform/platform.h
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
#include <cstdio>
#include <cstdint>
#include "platform/platform.h"

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* name, bool ok) {
    std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", name);
    ok ? ++g_pass : ++g_fail;
}

constexpr uint32_t kDay = 86400000u;

}  // namespace

int main() {
    std::printf("plat::since\n");
    const uint32_t now = 1000000u;
    check("the same moment is 0", plat::since(now, now) == 0);
    check("1 ms ago is 1", plat::since(now, now - 1) == 1);
    check("a stamp 1 ms ahead is 0, not 49 days", plat::since(now, now + 1) == 0);
    check("a stamp 60 s ahead is 0", plat::since(now, now + 60000u) == 0);
    check("25 days ago is 25 days (signed it would be negative)",
          plat::since(now + 25u * kDay, now) == 25u * kDay);
    check("49 days ago is 49 days", plat::since(now + 49u * kDay, now) == 49u * kDay);

    // Across the wrap: a stamp just before it and a now just after.
    const uint32_t before = 0xFFFFFF00u, after = 0x00000100u;
    check("across the wrap, 512 ms", plat::since(after, before) == 512u);
    check("ahead across the wrap is 0", plat::since(before, after) == 0);
    check("a day across the wrap", plat::since(before + kDay, before) == kDay);

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
