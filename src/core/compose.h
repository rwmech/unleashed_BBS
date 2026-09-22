/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/compose.h
 * Module:       Core / multi-line text entry
 *
 * Purpose:      One way to write something longer than a line, used by every
 *                  subsystem that takes a body: forum posts, mail, and the
 *                  feedback system when it is built.
 *
 * Design:       Rob's requirement and the right shape anyway: "whatever we
 *               choose as these interface commands must be consistent across
 *               all entries... I dont see why mail, the system feedback
 *               systems, forums all dont use a unified message entry system."
 *               A second copy of this would drift from the first, and this
 *               project has paid for that pattern more than once.
 *
 * Notes:        LineEditor cannot do this alone. Its buffer is
 *               char buf_[BBS_LINE_MAX + 1], 73 bytes, and begin() takes a
 *               uint8_t: asking it for a 1,728 byte body does not fail or
 *               warn, it silently returns 72 characters and the caller finds
 *               out by running out of room mid-sentence. That shipped once.
 *
 *               **Only the current line is held in RAM.** Ten callers each
 *               able to write 1,728 bytes would be about 20 KB of static
 *               DRAM against roughly 34 KB of headroom, spent on buffers
 *               that are empty almost all the time. Finished lines go to a
 *               per-node draft file instead, so the cost is one open file
 *               and no static growth. That also makes the card the thing
 *               that decides whether a subsystem can offer this at all,
 *               which is why chat without a card keeps its one-line entry.
 *
 * Libraries:    none (libc)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     host/test_compose.cpp, src/plugins/forums.cpp
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
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace compose {

// ---------------------------------------------------------------------------
// How a caller finishes, and why there is more than one way.
//
// `/s` on a line of its own, and nothing else.
//
// Ctrl-D was offered as a shortcut and **it did not reach the board**:
// tested on SyncTERM and the key does nothing at all, because the client
// takes it. A documented key that silently does nothing is worse than one
// that was never mentioned, because somebody trusts it and loses what they
// wrote. Removed rather than explained.
//
// `/s` works on every keyboard ever built, which is why it was always the
// one the screen named.
//
// `/a` throws it away. Both are recognised only on a line of their own, so
// somebody writing *about* a command still can.
// ---------------------------------------------------------------------------

inline bool isSave(const char* line) {
    return line && (!strcasecmp(line, "/s") || !strcasecmp(line, "/save"));
}
inline bool isAbort(const char* line) {
    return line && (!strcasecmp(line, "/a") || !strcasecmp(line, "/abort"));
}

// The words the screen uses, in one place so every subsystem says the same
// thing. A caller who learns how to finish a forum post has learned how to
// finish a mail message.
constexpr const char kHowToEnd[]  = " on a line by itself sends it.  ";
constexpr const char kHowToDrop[] = " throws it away.";
constexpr const char kSaveWord[]  = "/s";
constexpr const char kDropWord[]  = "/a";

// ---------------------------------------------------------------------------
// lineWidth: how wide one composed line may be on THIS terminal.
//
// Both subsystems used to open the editor at BBS_LINE_MAX (72) whatever the
// caller was sitting at, and trigger the wrap at the same constant. On a C64
// that is a four character line number plus 72 characters against a 40 column
// screen, so every line wrapped in the terminal, the board's own wrap never
// fired, and the numbers down the left went out of step with what was on
// screen. The message stored was right and the screen was wrong, which is the
// worse way round.
//
// `prompt` is the columns the line number occupies ("16: " is four). One more
// column is held back for the cursor, because a cursor sitting in the last
// column is what makes a terminal scroll a line it has not finished.
//
// An unknown width counts as 40, the same rule the rest of the board follows:
// a terminal that never said how wide it is has not promised anything, and a
// line that wraps is worse everywhere than a line that is short somewhere.
// ---------------------------------------------------------------------------
inline uint8_t lineWidth(uint8_t cols, uint8_t prompt, uint8_t hardMax) {
    if (cols < 40) cols = 40;
    uint16_t room = cols > static_cast<uint16_t>(prompt + 1)
                  ? static_cast<uint16_t>(cols - prompt - 1)
                  : 1;
    return room > hardMax ? hardMax : static_cast<uint8_t>(room);
}

// ---------------------------------------------------------------------------
// wrapPoint: where a full line breaks, and what carries to the next one.
//
// Called when the line editor is full and the caller is still typing, so the
// board wraps for them instead of silently refusing keystrokes. A caller who
// stops being echoed mid-sentence reads that as the board having frozen,
// which is exactly how this was reported.
//
// The break goes at the last space so a word is never cut in half. A single
// word wider than the whole line has nowhere to break and is let through
// whole, because hunting for a space that is not there would loop forever.
//
// Returns how many characters stay on this line; `carry` receives the
// unfinished word, empty when there is none. The space at the break is
// dropped rather than carried, or every wrapped line would start with one.
// ---------------------------------------------------------------------------
inline uint8_t wrapPoint(const char* line, uint8_t len, char* carry, size_t carryN) {
    if (carry && carryN) carry[0] = '\0';
    if (!line || !len) return 0;

    int cut = -1;
    for (int i = static_cast<int>(len) - 1; i > 0; --i)
        if (line[i] == ' ') { cut = i; break; }

    if (cut <= 0) return len;                 // one long word: let it stand

    if (carry && carryN) {
        size_t n = static_cast<size_t>(len - cut - 1);
        if (n >= carryN) n = carryN - 1;
        memcpy(carry, line + cut + 1, n);
        carry[n] = '\0';
    }
    return static_cast<uint8_t>(cut);
}

// ---------------------------------------------------------------------------
// Body: lines accumulated into storage the caller owns.
//
// Kept as plain data over a caller-supplied buffer so the arithmetic can be
// tested without a board, a session or a card. A subsystem that has somewhere
// bigger to put the text points this at its own buffer; one that does not
// keeps a small one.
// ---------------------------------------------------------------------------
struct Body {
    char*    buf     = nullptr;
    uint16_t cap     = 0;          // bytes, excluding the terminator
    uint16_t len     = 0;
    uint8_t  rows    = 0;
    uint8_t  maxRows = 0;
};

inline void begin(Body& b, char* storage, uint16_t cap, uint8_t maxRows) {
    b.buf = storage;
    b.cap = cap;
    b.len = 0;
    b.rows = 0;
    b.maxRows = maxRows;
    if (b.buf && b.cap) b.buf[0] = '\0';
}

inline bool full(const Body& b) {
    return b.rows >= b.maxRows || b.len >= b.cap;
}

// addLine: one finished line. False when there is no room left.
//
// Lines are joined with a newline, never a space, so a blank line stays a
// blank line. That is how somebody separates paragraphs, and swallowing it
// would make paragraphs impossible to write.
inline bool addLine(Body& b, const char* line) {
    if (!b.buf || b.rows >= b.maxRows) return false;
    size_t n = line ? strlen(line) : 0;
    size_t need = n + (b.len ? 1u : 0u);
    if (b.len + need > b.cap) return false;
    if (b.len) b.buf[b.len++] = '\n';
    if (n) memcpy(b.buf + b.len, line, n);
    b.len = static_cast<uint16_t>(b.len + n);
    b.buf[b.len] = '\0';
    ++b.rows;
    return true;
}

// popLine: take the last line back out, for editing.
//
// Line-at-a-time entry commits a line the moment Enter is pressed, which
// leaves a typo three lines back unreachable: the caller can only abandon
// the whole message and start again. That is worse than the fixed line
// length it replaced, because the wall was at least visible.
//
// So backspace on an empty line calls this, and the line it returns goes
// back into the editor with the cursor at its end. Repeating it walks a
// caller all the way back to an empty message.
//
// False when there is nothing left to take, which is how the caller knows
// to stop rather than beeping at an empty body.
inline bool popLine(Body& b, char* out, size_t outN) {
    if (out && outN) out[0] = '\0';
    if (!b.buf || !b.rows) return false;

    // Find the start of the last line. The separator is a newline, so the
    // last one is the boundary; with no newline at all the whole body is
    // the line.
    uint16_t start = 0;
    for (uint16_t i = b.len; i > 0; --i) {
        if (b.buf[i - 1] == '\n') { start = i; break; }
    }

    if (out && outN) {
        size_t n = static_cast<size_t>(b.len - start);
        if (n >= outN) n = outN - 1;
        memcpy(out, b.buf + start, n);
        out[n] = '\0';
    }

    // Drop the line and the newline that preceded it, so the body does not
    // end on a separator waiting for a line that is no longer coming.
    b.len = start ? static_cast<uint16_t>(start - 1) : 0;
    b.buf[b.len] = '\0';
    --b.rows;
    return true;
}

}  // namespace compose
