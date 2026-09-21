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
#include <cstring>
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

// ---------------------------------------------------------------------------
// How a node is written down.
//
// This used to be one function returning one character: 'S' for the sysop,
// 'B' for the busy line, otherwise '0' + id. That worked while there were
// six nodes and silently produced ':' for node 10 and ';' for node 11 the
// moment there were sixteen, so it is gone rather than fixed in place.
//
// Two forms, because the two uses want different things:
//
//   nodeName   "1", "16", "S", "B"     for prose: "Node 16 is not taking pages"
//   nodeLabel  " 1", "16", " S", " B"  for lists, where a fixed column has to
//                                      line up under its heading
//
// Digits, not letters, for 10 and up: nodeByArg parses a node argument with
// strtol, so the number in a list is the number you type at the prompt. A
// list showing 'c' for the node you have to call 12 is a bug wearing a
// disguise.
//
// Both return by value. NodeStr is three bytes, so this costs nothing and
// needs no caller-supplied buffer; pass `.t` to printf-family functions,
// since varargs will not apply the conversion operator for you.
// ---------------------------------------------------------------------------
struct NodeStr {
    char t[3];
    operator const char*() const { return t; }
};

// nodeNum: a bare node number, for a stored record that has the number but
// not the Session it came from (the caller log keeps CallRec::node).
inline NodeStr nodeNum(uint8_t id) {
    NodeStr r{};
    if (id >= 10) { r.t[0] = static_cast<char>('0' + id / 10);
                    r.t[1] = static_cast<char>('0' + id % 10); }
    else          { r.t[0] = static_cast<char>('0' + id); }
    return r;
}

inline NodeStr nodeName(const Session& s) {
    if (s.role == Role::Sysop) { NodeStr r{}; r.t[0] = 'S'; return r; }
    if (s.role == Role::Busy)  { NodeStr r{}; r.t[0] = 'B'; return r; }
    return nodeNum(s.id);
}

inline NodeStr nodeLabel(const Session& s) {
    NodeStr r = nodeName(s);
    if (r.t[1] == '\0') { r.t[1] = r.t[0]; r.t[0] = ' '; }   // right align in two columns
    return r;
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

// fmtCommas: 136424 -> "136,424". Long numbers are read at a glance on a
// 40 column screen only when they are grouped.
inline void fmtCommas(uint32_t v, char* out, size_t n) {
    char digits[12];
    int len = snprintf(digits, sizeof(digits), "%u", static_cast<unsigned>(v));
    size_t w = 0;
    for (int i = 0; i < len && w + 1 < n; ++i) {
        if (i && (len - i) % 3 == 0 && w + 2 < n) out[w++] = ',';
        out[w++] = digits[i];
    }
    out[w < n ? w : n - 1] = '\0';
}

// markColor: the colour of a rank marker, used wherever one is printed so
// the same character always means the same thing
inline Color markColor(char mark) {
    switch (mark) {
        case '*': return Color::DarkGrey;      // guest
        case '>': return Color::Yellow;        // co-sysop
        case ']': return Color::LightRed;      // sysop
        default:  return Color::Grey;
    }
}

// listHandle: a handle cut to a list column
inline void listHandle(char* out, size_t n, const char* user, int width) {
    snprintf(out, n, "%.*s", width, user);
}

// wrap: one line of word-wrapped text, returning where the next line starts.
//
// Wrapping happens on OUTPUT, at the reader's width, not on input at the
// writer's. A message typed at 72 columns on SyncTERM has to be readable on
// a C64, and one typed at 35 on a C64 should not sit in a narrow stripe down
// the left of an 80 column screen. The reader's width is not knowable when
// the text is written, so it cannot be baked in then.
//
// Returns nullptr when there is nothing left. `out` gets one line, never
// wider than `cols` columns and never with the trailing space.
//
// An over-long word is broken rather than allowed to overflow: a URL or a
// row of dashes that is wider than the terminal has to go somewhere, and a
// row that runs past the margin wraps in the terminal, which on a refresh
// screen leaves a tail behind on every redraw.
//
// The forums need this for message bodies and the queued "profile text
// should word wrap" item needs exactly the same function, which is why it
// lives here rather than in either of them.
inline const char* wrap(const char* src, char* out, size_t outN, uint8_t cols) {
    if (!src || !*src || !outN) return nullptr;
    if (cols == 0) cols = 1;
    while (*src == ' ') ++src;                    // no line starts on a space
    if (!*src) return nullptr;

    size_t limit = (cols < outN - 1) ? cols : outN - 1;
    size_t take  = 0;                             // bytes that certainly fit
    size_t lastSp = 0;                            // byte after the last space

    while (src[take] && take < limit) {
        if (src[take] == '\n') {                  // an explicit break wins
            memcpy(out, src, take);
            out[take] = '\0';
            return src + take + 1;
        }
        if (src[take] == ' ') lastSp = take;
        ++take;
    }

    // Where this line ends, and where the next one starts, are the same
    // index; how much of it is PRINTED is not, because trailing spaces are
    // rubbed off. Keeping the two separate is what stops a line ending in a
    // space, which on a reverse-video row is a visible notch.
    size_t brk;
    if (!src[take])          brk = take;   // the rest fits
    else if (src[take] == ' ') brk = take; // a word ends exactly on the margin
    else if (lastSp)         brk = lastSp; // step back to the last space
    else                     brk = take;   // one word wider than the row

    // The `src[take] == ' '` case is the one worth naming. The scan stops
    // before the column at the margin, so a word whose last character lands
    // on it is never seen to have ended, and stepping back to the previous
    // space threw that whole word onto the next line. "abcd efgh ijkl" at
    // nine columns came out as "abcd" and then "efgh ijkl", which wastes
    // half the row and looks like the wrap is broken rather than tight.
    // lastSp cannot be 0 for a real space, because leading spaces are
    // skipped above, so 0 reliably means "no space in this line".

    size_t outLen = brk;
    while (outLen && src[outLen - 1] == ' ') --outLen;

    memcpy(out, src, outLen);
    out[outLen] = '\0';
    return src + brk;
}

constexpr const char kMarkKey[]     = "*GUEST  >CO-SYSOP  ]SYSOP";
constexpr const char kMoreText[]    = "[More] Y/n/c ";
constexpr uint8_t    kMoreLen       = sizeof(kMoreText) - 1;
constexpr const char kConfirmText[] = "Log off (Y/N)? ";

} // namespace bbsu
