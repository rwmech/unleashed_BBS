/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/cardnames.h
 * Module:       Core / backups on the SD card (1.1.0)
 *
 * Purpose:      The names of the zips BACKUP SD and the nightly backup write
 *                  into the card's backup folder, and the rule that decides
 *                  which nightly one to remove.
 *
 *                    unleashed-YYYYMMDD-HHMM.zip   BACKUP SD            (27)
 *                    screens-YYYYMMDD-HHMM.zip     BACKUP SD SCREENS    (25)
 *                    nightly-YYYYMMDD.zip          the nightly backup   (20)
 *
 *                  The nightly name is its own on purpose. "Keep the last 7"
 *                  over one shared name would count the sysop's own backups
 *                  too, and a zip made by hand before a risky change would
 *                  be deleted a week later without anybody being told. So
 *                  pruning only ever looks at nightly-YYYYMMDD.zip, and a
 *                  name that merely resembles one is left alone.
 *
 *                  Any other .zip in the folder is listed too (a screens zip
 *                  built on a laptop and copied over is the mass-update case
 *                  RESTORE SD SCREENS exists for), but never pruned.
 *
 *                  Pure and header-only: host/test_cardnames.cpp drives it.
 *
 * Interfaces:   Kind, kindOf, listable, partial, makeName, NightlyScan
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     BACKUP.md "Backups on the SD card"
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
#include <cstdio>
#include <cstring>
#include <strings.h>

namespace cardbak {

constexpr char    kDir[]       = "backup";   // on the card: <sd>/backup
constexpr uint8_t kNameMax     = 27;         // the list's name column, and the longest own name
constexpr uint8_t kNightlyKeep = 7;          // nightly zips kept on the card

enum class Kind : uint8_t { None, Full, Screens, Nightly, Other };

namespace detail {
inline bool digits(const char* p, size_t n) {
    for (size_t i = 0; i < n; ++i) if (p[i] < '0' || p[i] > '9') return false;
    return true;
}
} // namespace detail

// listable: a name the list may show and a restore may be given. A .zip
// (any case), short enough for the list's column, not hidden and with no
// path in it. The list is the only way to a file on the card, so this is
// also what keeps "RESTORE SD ../users.txt" from meaning anything. A space
// is allowed, since a laptop names things "C64 screens.zip": such a zip is
// restored by its number, a name with a space in it being two words to the
// command line.
inline bool listable(const char* name) {
    if (!name) return false;
    size_t n = strlen(name);
    if (n < 5 || n > kNameMax || name[0] == '.' || name[0] == ' ') return false;
    if (strcasecmp(name + n - 4, ".zip")) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 0x20 || c > 0x7E || c == '/' || c == '\\' || c == ':') return false;
    }
    return true;
}

// kindOf: which of the board's own names this is, Other for any other zip
// the list would show, None for anything else.
inline Kind kindOf(const char* name) {
    if (!listable(name)) return Kind::None;
    size_t n = strlen(name);
    // unleashed-20260923-2210.zip
    if (n == 27 && !strncmp(name, "unleashed-", 10) && detail::digits(name + 10, 8) &&
        name[18] == '-' && detail::digits(name + 19, 4) && !strcmp(name + 23, ".zip"))
        return Kind::Full;
    // screens-20260923-2210.zip
    if (n == 25 && !strncmp(name, "screens-", 8) && detail::digits(name + 8, 8) &&
        name[16] == '-' && detail::digits(name + 17, 4) && !strcmp(name + 21, ".zip"))
        return Kind::Screens;
    // nightly-20260923.zip
    if (n == 20 && !strncmp(name, "nightly-", 8) && detail::digits(name + 8, 8) &&
        !strcmp(name + 16, ".zip"))
        return Kind::Nightly;
    return Kind::Other;
}

// partial: a zip BACKUP SD or the nightly one was part way through writing
// when the power went or the card was pulled: "<zip>.tmp", where <zip> is a
// name the list would show. The zip is written under that name and renamed
// only once it is whole, so the list never offers half of one; a .tmp that
// outlives the write is taking card space for nothing, and is tidied when
// the card is next mounted. Nothing else ending in .tmp matches, so a file
// the sysop keeps in the folder under such a name is never touched.
inline bool partial(const char* name) {
    if (!name) return false;
    size_t n = strlen(name);
    if (n < 9 || n > kNameMax + 4u || strcmp(name + n - 4, ".tmp")) return false;
    char zip[kNameMax + 1];
    memcpy(zip, name, n - 4);
    zip[n - 4] = '\0';
    return listable(zip);
}

// makeName: the board's own name for a zip made now. stamp is
// "YYYYMMDD-HHMM" (strftime "%Y%m%d-%H%M"); a nightly one uses only the
// date. Writes "" and returns false for Other or None, or a stamp of the
// wrong shape, so a name is never made from a clock that is not there.
inline bool makeName(Kind k, const char* stamp, char* out, size_t n) {
    if (n) out[0] = '\0';
    if (!stamp || strlen(stamp) != 13 || stamp[8] != '-' ||
        !detail::digits(stamp, 8) || !detail::digits(stamp + 9, 4)) return false;
    int w = -1;
    if      (k == Kind::Full)    w = snprintf(out, n, "unleashed-%s.zip", stamp);
    else if (k == Kind::Screens) w = snprintf(out, n, "screens-%s.zip", stamp);
    else if (k == Kind::Nightly) w = snprintf(out, n, "nightly-%.8s.zip", stamp);
    if (w < 0 || static_cast<size_t>(w) >= n) { if (n) out[0] = '\0'; return false; }
    return true;
}

// NightlyScan: fed every name in the folder, it counts the nightly zips and
// remembers the oldest, which is the smallest name because the date is in
// it. The caller removes that one while count is over kNightlyKeep, then
// scans again: one name held, whatever the folder holds.
struct NightlyScan {
    uint16_t count  = 0;
    char     oldest[kNameMax + 1] = {};

    void feed(const char* name) {
        if (kindOf(name) != Kind::Nightly) return;
        ++count;
        if (!oldest[0] || strcmp(name, oldest) < 0) {
            strncpy(oldest, name, kNameMax);
            oldest[kNameMax] = '\0';
        }
    }

    // prune: the name to remove, or nullptr when no more than keep are there
    const char* prune(uint16_t keep = kNightlyKeep) const {
        return count > keep && oldest[0] ? oldest : nullptr;
    }
};

} // namespace cardbak
