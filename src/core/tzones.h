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

} // namespace tzones
