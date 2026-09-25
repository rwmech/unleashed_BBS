/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/tzones.h
 * Module:       Core / the timezone picker (1.1.0)
 *
 * Purpose:      The zones CONFIG board offers by name, each with the POSIX
 *                  TZ string the board actually keeps. Nobody types
 *                  "CST6CDT,M3.2.0,M11.1.0" from memory, and "US Central
 *                  (Chicago)" is what a sysop is looking for.
 *
 *                  One list, two views. TZ_ZONES below is the only place a
 *                  zone is written down: kZones is it as a table and
 *                  kZoneChoices is it as the form's cycle ("UTC|...|Custom"),
 *                  so the names the form steps through and the strings behind
 *                  them cannot drift apart.
 *
 *                  The names and strings are the copywriter's table
 *                  (internal/copy-1.1.0-2026-09-23.md section 3), checked
 *                  there against the tz database 2026d with zic's POSIX
 *                  footers. No two entries share a string, so "the file's
 *                  value matches the table" always has exactly one answer.
 *
 *                  Header-only and pure, so host/test_tzones.cpp checks the
 *                  table's own rules (lengths, uniqueness, the round trip)
 *                  with no board in it.
 *
 * Interfaces:   kZones, kZoneCount, kZoneChoices, kCustom, nameFor, posixFor
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     COMMANDS.md, system.cfg "tz"
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
#include <cstring>

namespace tzones {

// The zones, UTC first because it is what every board ships with, then west
// to east. Names are 24 characters at most (the form's box shows 26) and
// strings 40 at most (the tz key's cap). Canada's names say so on purpose:
// British Columbia and Alberta stopped changing their clocks in 2026, so a
// sysop in Vancouver who picked "US Pacific" would get a clock that goes
// wrong on 1 November 2026.
#define TZ_ZONES(X)                                                      \
    X("UTC",                      "UTC0")                                \
    X("US Hawaii (Honolulu)",     "HST10")                               \
    X("US Alaska (Anchorage)",    "AKST9AKDT,M3.2.0,M11.1.0")            \
    X("US Pacific (Los Angeles)", "PST8PDT,M3.2.0,M11.1.0")              \
    X("Arizona, BC (Phoenix)",    "MST7")                                \
    X("US Mountain (Denver)",     "MST7MDT,M3.2.0,M11.1.0")              \
    X("Mexico, Alberta, Sask.",   "CST6")                                \
    X("US Central (Chicago)",     "CST6CDT,M3.2.0,M11.1.0")              \
    X("US Eastern (New York)",    "EST5EDT,M3.2.0,M11.1.0")              \
    X("Atlantic (Halifax)",       "AST4ADT,M3.2.0,M11.1.0")              \
    X("Chile (Santiago)",         "<-04>4<-03>,M9.1.6/24,M4.1.6/24")     \
    X("Newfoundland (St John's)", "NST3:30NDT,M3.2.0,M11.1.0")           \
    X("Sao Paulo, Buenos Aires",  "<-03>3")                              \
    X("UK (London)",              "GMT0BST,M3.5.0/1,M10.5.0")            \
    X("Ireland (Dublin)",         "GMT0IST,M3.5.0/1,M10.5.0")            \
    X("Portugal (Lisbon)",        "WET0WEST,M3.5.0/1,M10.5.0")           \
    X("West Africa (Lagos)",      "WAT-1")                               \
    X("Central Europe (Berlin)",  "CET-1CEST,M3.5.0,M10.5.0/3")          \
    X("Eastern Europe (Athens)",  "EET-2EEST,M3.5.0/3,M10.5.0/4")        \
    X("Egypt (Cairo)",            "EET-2EEST,M4.5.5/0,M10.5.4/24")       \
    X("South Africa (Pretoria)",  "SAST-2")                              \
    X("East Africa (Nairobi)",    "EAT-3")                               \
    X("Turkey (Istanbul)",        "<+03>-3")                             \
    X("Russia (Moscow)",          "MSK-3")                               \
    X("Gulf (Dubai)",             "<+04>-4")                             \
    X("India (New Delhi)",        "IST-5:30")                            \
    X("China (Beijing)",          "CST-8")                               \
    X("Singapore, Malaysia",      "<+08>-8")                             \
    X("Australia WA (Perth)",     "AWST-8")                              \
    X("Japan (Tokyo)",            "JST-9")                               \
    X("Australia SA (Adelaide)",  "ACST-9:30ACDT,M10.1.0,M4.1.0/3")      \
    X("Australia QLD (Brisbane)", "AEST-10")                             \
    X("Australia East (Sydney)",  "AEST-10AEDT,M10.1.0,M4.1.0/3")        \
    X("New Zealand (Auckland)",   "NZST-12NZDT,M9.5.0,M4.1.0/3")

struct Zone { const char* name; const char* posix; };

#define TZ_ZONE_ROW(n, p) { n, p },
constexpr Zone kZones[] = { TZ_ZONES(TZ_ZONE_ROW) };
#undef TZ_ZONE_ROW

constexpr uint8_t kZoneCount = static_cast<uint8_t>(sizeof(kZones) / sizeof(kZones[0]));

// The last choice, which is not a zone: whatever TZ string is typed below it.
constexpr char kCustom[] = "Custom";

// The form's cycle: every name, then Custom. FF_CYCLE splits on '|', which
// no name contains (the unit test holds that).
#define TZ_ZONE_CHOICE(n, p) n "|"
constexpr char kZoneChoices[] = TZ_ZONES(TZ_ZONE_CHOICE) "Custom";
#undef TZ_ZONE_CHOICE

constexpr uint8_t kNameMax  = 24;     // the copy's cap, and the field's
constexpr uint8_t kPosixMax = 40;     // the tz key's cap in CONFIG

// nameFor: the zone a TZ string is, or nullptr when it is none of them. An
// exact match: a string that differs by a space is a string the sysop typed
// on purpose, and it opens as Custom with that string rather than being
// quietly tidied into an entry it only resembles.
inline const char* nameFor(const char* posix) {
    if (!posix) return nullptr;
    for (const Zone& z : kZones)
        if (!strcmp(z.posix, posix)) return z.name;
    return nullptr;
}

// posixFor: the string behind a zone's name, or nullptr for Custom or for a
// name the table does not have.
inline const char* posixFor(const char* name) {
    if (!name) return nullptr;
    for (const Zone& z : kZones)
        if (!strcmp(z.name, name)) return z.posix;
    return nullptr;
}

// ---------------------------------------------------------------------------
// valid: a TZ string the board's C library can read (1.1.1, TZ-bad).
//
// newlib's tzset (tzset_r.c, the IDF's newlib 4.x) gives up part way
// through a string it cannot read and leaves the board on "unnamed UTC",
// with nothing said: a sysop who typed a Custom string one character wrong
// ran their clock in UTC and found out from the log times. So CONFIG and
// the parser ask this first. It follows newlib's own reading, and is a
// little stricter where newlib is merely lenient, never looser:
//
//   std offset [dst [offset] [,rule[/time],rule[/time]]]
//
//   std, dst   1 to 10 letters, or <1 to 10 of A-Z a-z 0-9 + -> quoted
//   offset     [+|-]hh[:mm[:ss]], hh 0 to 24, mm and ss 0 to 59
//   rule       Mm.w.d (m 1-12, w 1-5, d 0-6), Jn (1-365) or n (0-365);
//              left out, newlib uses the US rules, as here
//   time       [+|-]hh[:mm[:ss]], hh 0 to 167
//
// Stricter: the whole string has to be read (newlib ignores what is left
// over, which is where a typo usually is), a quoted name has to close its
// bracket, and an offset's hours stop at 24.
// ---------------------------------------------------------------------------
namespace detail {

inline bool tzAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
inline bool tzDigit(char c) { return c >= '0' && c <= '9'; }

// tzNum: 1 to 3 digits as a number, no more than max.
inline bool tzNum(const char*& p, unsigned max, unsigned& out) {
    if (!tzDigit(*p)) return false;
    unsigned v = 0;
    uint8_t n = 0;
    while (tzDigit(*p) && n < 3) { v = v * 10u + static_cast<unsigned>(*p - '0'); ++p; ++n; }
    if (tzDigit(*p) || v > max) return false;
    out = v;
    return true;
}

inline bool tzName(const char*& p) {
    uint8_t n = 0;
    if (*p == '<') {
        ++p;
        while ((tzAlpha(*p) || tzDigit(*p) || *p == '+' || *p == '-') && n < 10) { ++p; ++n; }
        if (!n || *p != '>') return false;
        ++p;
        return true;
    }
    while (tzAlpha(*p) && n < 10) { ++p; ++n; }
    return n && !tzAlpha(*p);
}

// tzClock: [+|-]hh[:mm[:ss]], the hours no more than maxH.
inline bool tzClock(const char*& p, unsigned maxH) {
    if (*p == '+' || *p == '-') ++p;
    unsigned v;
    if (!tzNum(p, maxH, v)) return false;
    for (uint8_t i = 0; i < 2 && *p == ':'; ++i) {
        ++p;
        if (!tzNum(p, 59, v)) return false;
    }
    return true;
}

inline bool tzRule(const char*& p) {
    unsigned v;
    if (*p == 'M') {
        ++p;
        if (!tzNum(p, 12, v) || v < 1 || *p++ != '.') return false;
        if (!tzNum(p, 5, v) || v < 1 || *p++ != '.') return false;
        if (!tzNum(p, 6, v)) return false;
    } else if (*p == 'J') {
        ++p;
        if (!tzNum(p, 365, v) || v < 1) return false;
    } else if (tzDigit(*p)) {
        if (!tzNum(p, 365, v)) return false;
    }
    // else: no rule at all, and newlib puts the US one in its place
    if (*p == '/') {
        ++p;
        if (!tzClock(p, 167)) return false;
    }
    return true;
}

} // namespace detail

inline bool valid(const char* tz) {
    using namespace detail;
    if (!tz) return false;
    const char* p = tz;
    if (*p == ':') ++p;                               // newlib skips one
    if (!tzName(p) || !tzClock(p, 24)) return false;
    if (!*p) return true;                             // standard time only
    if (!tzName(p)) return false;
    if (*p && *p != ',') {                            // the summer offset
        if (!tzClock(p, 24)) return false;
    }
    for (uint8_t i = 0; i < 2 && *p; ++i) {
        if (*p == ',') ++p;
        if (!tzRule(p)) return false;
    }
    return *p == '\0';
}

} // namespace tzones
