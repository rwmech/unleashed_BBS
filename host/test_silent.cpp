/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         host/test_silent.cpp
 * Module:       Host tests / silent mode (1.1.0)
 *
 * Purpose:      The silent mode rules with no board in them: the switch, the
 *               hours at every edge (inside a day and across midnight), no
 *               clock, and the times as the parser and CONFIG read and write
 *               them. Every rule here is a boundary, and one that is a
 *               minute out is a strip lighting up at ten past midnight in a
 *               bedroom.
 *
 * Libraries:    none
 * Targets:      Linux host build
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
#include <cstdio>
#include <cstring>
#include "../src/core/silent.h"

using namespace board;

static int fails = 0, passes = 0;

static void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passes; else ++fails;
}

static int16_t hm(int h, int m) { return static_cast<int16_t>(h * 60 + m); }

int main() {
    printf("Silent mode\n");

    // The switch: silent whatever the time, with or without hours or a clock.
    check("the switch alone is silent", rule(true, kNoTime, kNoTime, hm(12, 0)) == Quiet::Switch);
    check("with no clock too", rule(true, kNoTime, kNoTime, kNoTime) == Quiet::Switch);
    check("and outranks the hours as the reason",
          rule(true, hm(22, 0), hm(7, 0), hm(23, 0)) == Quiet::Switch);
    check("and holds outside them", rule(true, hm(22, 0), hm(7, 0), hm(12, 0)) == Quiet::Switch);
    check("off with nothing set", rule(false, kNoTime, kNoTime, hm(3, 0)) == Quiet::Off);

    // Hours inside one day, 13:00 to 14:30: the start in, the end out.
    const int16_t a = hm(13, 0), b = hm(14, 30);
    check("13:00-14:30 silent at 13:00", rule(false, a, b, hm(13, 0)) == Quiet::Hours);
    check("and at 14:29", rule(false, a, b, hm(14, 29)) == Quiet::Hours);
    check("lit again at 14:30", rule(false, a, b, hm(14, 30)) == Quiet::Off);
    check("lit at 12:59", rule(false, a, b, hm(12, 59)) == Quiet::Off);
    check("lit at midnight", rule(false, a, b, 0) == Quiet::Off);

    // Across midnight, 22:00 to 07:00.
    const int16_t f = hm(22, 0), u = hm(7, 0);
    check("22:00-07:00 lit at 21:59", rule(false, f, u, hm(21, 59)) == Quiet::Off);
    check("silent at 22:00", rule(false, f, u, hm(22, 0)) == Quiet::Hours);
    check("at 23:59", rule(false, f, u, hm(23, 59)) == Quiet::Hours);
    check("at 00:00", rule(false, f, u, 0) == Quiet::Hours);
    check("at 06:59", rule(false, f, u, hm(6, 59)) == Quiet::Hours);
    check("lit again at 07:00", rule(false, f, u, hm(7, 0)) == Quiet::Off);
    check("lit at noon", rule(false, f, u, hm(12, 0)) == Quiet::Off);
    {
        int silentMinutes = 0;
        for (int16_t m = 0; m < 1440; ++m) if (inHours(f, u, m)) ++silentMinutes;
        check("nine hours a day, exactly", silentMinutes == 9 * 60);
    }
    check("from midnight: 00:00-06:00 silent at 00:00", rule(false, 0, hm(6, 0), 0) == Quiet::Hours);
    check("to midnight: 20:00-00:00 silent at 23:59, lit at 00:00",
          rule(false, hm(20, 0), 0, hm(23, 59)) == Quiet::Hours && rule(false, hm(20, 0), 0, 0) == Quiet::Off);

    // No clock: the hours do nothing, and only the switch applies.
    check("no clock: hours do nothing", rule(false, f, u, kNoTime) == Quiet::Off);
    check("no clock: the switch still works", rule(true, f, u, kNoTime) == Quiet::Switch);

    // Hours that are not a range.
    check("only a start: never", rule(false, f, kNoTime, hm(23, 0)) == Quiet::Off);
    check("only an end: never", rule(false, kNoTime, u, hm(3, 0)) == Quiet::Off);
    check("the same time twice: never", rule(false, f, f, f) == Quiet::Off);

    // The times, as the file and the form have them.
    check("\"22:00\" is 1320", parseTime("22:00") == hm(22, 0));
    check("\"7:05\" is 425", parseTime("7:05") == hm(7, 5));
    check("\"00:00\" is 0", parseTime("00:00") == 0);
    check("\"23:59\" is the last", parseTime("23:59") == hm(23, 59));
    check("blank is none", parseTime("") == kNoTime && parseTime("   ") == kNoTime && parseTime(nullptr) == kNoTime);
    check("spaces round it are fine", parseTime(" 06:30 ") == hm(6, 30));
    const char* bad[] = { "24:00", "12:60", "7", "7:5", "0700", "7:00pm", "123:00", ":30", "ab:cd", "12:3x",
                          "-1:00", "12:00:00" };
    bool allBad = true;
    for (const char* v : bad) if (parseTime(v) != kBadTime) { allBad = false; printf("    took %s\n", v); }
    check("and nothing else is a time", allBad);

    char out[8];
    fmtTime(hm(7, 0), out, sizeof(out));
    check("07:00 written as 07:00", !strcmp(out, "07:00"));
    fmtTime(hm(23, 59), out, sizeof(out));
    check("23:59 as 23:59", !strcmp(out, "23:59"));
    fmtTime(kNoTime, out, sizeof(out));
    check("none written blank", out[0] == '\0');
    bool round = true;
    for (int16_t m = 0; m < 1440; ++m) {
        fmtTime(m, out, sizeof(out));
        if (parseTime(out) != m) round = false;
    }
    check("every minute of the day round-trips", round);

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
