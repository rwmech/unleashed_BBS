/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/ring.h
 * Module:       Core / the sysop page, its rules (1.1.0)
 *
 * Purpose:      The parts of OPERATOR with no board in them: how long a
 *                  ring rings, how often a caller may ring, and how a ring
 *                  nobody answered is written down for the sysop.
 *
 *                  Rate. One ring per caller per kGapMs, kPerCall in one call,
 *                  and one at a time on the whole board. The first two are
 *                  here; the third is "is a ring running", which only the
 *                  board knows, so it is passed in. A caller refused by one
 *                  rule is told which, so the order of the checks is part of
 *                  what they read: the call's allowance first, because it
 *                  does not change by waiting, then the wait, then the board.
 *
 *                  Notes. A ring that was not answered leaves a note, shown
 *                  at the sysop's next login or elevation and then cleared.
 *                  They are kept in a small file on userdata rather than in
 *                  RAM: the sysop may be away for days, a note that a reboot
 *                  threw away is a note that was never left, and a file costs
 *                  no static DRAM at all. One line per note, tab separated,
 *                  after a header line carrying how many rang in all, so the
 *                  sysop can be told when more rang than were kept.
 *
 *                  Pure and header-only, so host/test_ring.cpp drives every
 *                  boundary on a simulated clock. The file itself is written
 *                  by bbs_ring.cpp.
 *
 * Interfaces:   ringMs, Limiter, Verdict, Note, formatNote, parseNote,
 *               formatHeader, parseHeader
 *
 * Libraries:    none (libc)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     COMMANDS.md (OPERATOR), CHAT.md (/o)
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
#include <cstdlib>
#include <cstring>
#include "../config.h"

namespace ring {

constexpr uint32_t kRingMs    = 45000;    // how long a ring rings before "No answer"
constexpr uint32_t kGapMs     = 180000;   // one ring per caller per three minutes
constexpr uint8_t  kPerCall   = 3;        // and three in one call
constexpr uint8_t  kReasonMax = 60;       // what the caller wrote (UX spec)
constexpr uint8_t  kNotesMax  = 8;        // notes kept for the sysop, the newest
constexpr uint32_t kSpinMs    = 200;      // one spinner frame while it rings

// ringMs: how long a ring rings. kRingMs on a board. The host build takes
// BBS_RING_MS from the environment so the suite can watch a ring run out in
// seconds rather than sitting through 45 of them; a hook that must never
// reach a board, which is what BBS_HOST is for.
inline uint32_t ringMs() {
#ifdef BBS_HOST
    static uint32_t v = 0;
    if (!v) {
        const char* e = getenv("BBS_RING_MS");
        long n = e ? strtol(e, nullptr, 10) : 0;
        v = (n >= 1000 && n <= 600000) ? static_cast<uint32_t>(n) : kRingMs;
    }
    return v;
#else
    return kRingMs;
#endif
}

// Verdict: may this caller ring now, and if not, which rule said no.
enum class Verdict : uint8_t { Ok, Call, Wait, Board };

// Limiter: per session slot, when it last rang and how many times this call.
// N is the number of slots (every Session::id). A slot is forgotten when a
// new call opens on it: sessions come from a static pool, and a caller must
// never inherit the last caller's allowance, spent or not.
template <uint8_t N>
class Limiter {
public:
    void forget(uint8_t slot) {
        if (slot >= N) return;
        at_[slot]   = 0;
        used_[slot] = 0;
    }

    // move: the same caller on a different slot (Bbs::moveSession). Their
    // allowance goes with them rather than resetting on the way.
    void move(uint8_t from, uint8_t to) {
        if (from >= N || to >= N || from == to) return;
        at_[to]   = at_[from];
        used_[to] = used_[from];
        forget(from);
    }

    // check: may slot ring at now? boardBusy is "a ring is running", which
    // only the board knows. minutes is set for Wait: how long is left,
    // rounded UP, so 30 seconds left is "1 minute" and never "0 minutes".
    Verdict check(uint8_t slot, uint32_t now, bool boardBusy, uint8_t& minutes) const {
        minutes = 0;
        if (slot >= N) return Verdict::Call;
        if (used_[slot] >= kPerCall) return Verdict::Call;
        if (used_[slot]) {
            uint32_t since = now - at_[slot];           // unsigned: wraps cleanly
            if (since < kGapMs) {
                uint32_t left = kGapMs - since;
                minutes = static_cast<uint8_t>((left + 59999u) / 60000u);
                return Verdict::Wait;
            }
        }
        return boardBusy ? Verdict::Board : Verdict::Ok;
    }

    // spend: a ring went out, answered or not. Counted when it starts, so a
    // ring that reaches nobody costs the same as one that rings: otherwise
    // the notes file is a way to write eight lines on a board whose sysop
    // is away, as often as a caller likes.
    void spend(uint8_t slot, uint32_t now) {
        if (slot >= N) return;
        at_[slot] = now;
        if (used_[slot] < 255) ++used_[slot];
    }

    uint8_t used(uint8_t slot) const { return slot < N ? used_[slot] : 0; }

private:
    uint32_t at_[N]   = {};     // millis of the last ring; meaningful only while used_ > 0
    uint8_t  used_[N] = {};
};

// Note: one unanswered ring, as the sysop will read it.
struct Note {
    uint32_t epoch = 0;                          // 0 when the board had no clock
    uint8_t  node  = 0;
    bool     guest = false;
    char     handle[BBS_USER_MAX + 1] = {};
    char     reason[kReasonMax + 1]   = {};
};

// cleanCopy: n bytes of src into out, with every control character (a tab,
// a line end) turned into a space. A reason typed at the prompt cannot hold
// either today, but the file format is one note per line and one field per
// tab, and a format should not depend on what the editor happens to allow.
inline void cleanCopy(char* out, size_t cap, const char* src) {
    if (!cap) return;
    size_t i = 0;
    for (; src && src[i] && i + 1 < cap; ++i) {
        unsigned char c = static_cast<unsigned char>(src[i]);
        out[i] = c < 0x20 || c == 0x7F ? ' ' : static_cast<char>(c);
    }
    out[i] = '\0';
}

// formatHeader / parseHeader: "rings <total>", the first line of the file.
// total is how many rang since the notes were last shown, which can be more
// than the kNotesMax the file keeps.
inline size_t formatHeader(uint16_t total, char* out, size_t cap) {
    int n = snprintf(out, cap, "rings %u\n", static_cast<unsigned>(total));
    return n < 0 ? 0 : (static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : cap - 1);
}

inline bool parseHeader(const char* line, uint16_t& total) {
    if (!line || strncmp(line, "rings ", 6) != 0) return false;
    char* end = nullptr;
    unsigned long v = strtoul(line + 6, &end, 10);
    if (end == line + 6) return false;
    total = static_cast<uint16_t>(v > 65535ul ? 65535ul : v);
    return true;
}

// formatNote: "epoch<TAB>node<TAB>guest<TAB>handle<TAB>reason\n". The reason
// is last because it is the only field that may hold spaces, and nothing
// after it has to be found.
inline size_t formatNote(const Note& n, char* out, size_t cap) {
    char h[BBS_USER_MAX + 1], r[kReasonMax + 1];
    cleanCopy(h, sizeof(h), n.handle);
    cleanCopy(r, sizeof(r), n.reason);
    int w = snprintf(out, cap, "%lu\t%u\t%u\t%s\t%s\n",
                     static_cast<unsigned long>(n.epoch), static_cast<unsigned>(n.node),
                     n.guest ? 1u : 0u, h, r);
    return w < 0 ? 0 : (static_cast<size_t>(w) < cap ? static_cast<size_t>(w) : cap - 1);
}

// parseNote: the reverse. False for anything that is not a whole note, so a
// line damaged by a power cut is skipped rather than shown half read.
inline bool parseNote(const char* line, Note& n) {
    n = Note();
    if (!line) return false;
    const char* p = line;
    char* end = nullptr;
    unsigned long e = strtoul(p, &end, 10);
    if (end == p || *end != '\t') return false;
    n.epoch = static_cast<uint32_t>(e);
    p = end + 1;
    unsigned long node = strtoul(p, &end, 10);
    if (end == p || *end != '\t' || node > 255) return false;
    n.node = static_cast<uint8_t>(node);
    p = end + 1;
    if ((*p != '0' && *p != '1') || p[1] != '\t') return false;
    n.guest = *p == '1';
    p += 2;
    const char* tab = strchr(p, '\t');
    if (!tab || tab == p) return false;                     // a note has a handle
    size_t hl = static_cast<size_t>(tab - p);
    if (hl > BBS_USER_MAX) return false;
    memcpy(n.handle, p, hl);
    n.handle[hl] = '\0';
    p = tab + 1;
    size_t rl = strcspn(p, "\r\n");
    if (rl > kReasonMax) rl = kReasonMax;
    memcpy(n.reason, p, rl);
    n.reason[rl] = '\0';
    // A line without its newline is one a power cut stopped part way
    // through writing, unless it is the last thing in the file and the
    // writer never adds one, which formatNote always does.
    return p[rl] == '\n' || p[rl] == '\r';
}

} // namespace ring
