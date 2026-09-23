/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/codes.h
 * Module:       Core / inline codes in messages
 *
 * Purpose:      The @-codes a caller may put in a forum post, a mail or a
 *               chat line: colour, a few effects, and a few fill-ins. One
 *               renderer shared by all three, the way compose.h is one
 *               editor, so a code cannot mean one thing in the forums and
 *               another in the room.
 *
 *                 colour     @RED@ @LTGREEN@ ... (the palette, no black),
 *                            @N@ back to normal
 *                 effects    @BLINK:text@ @SCRAMBLE:text@ @TYPE:text@
 *                            @OOPS:text@ (types it, then rubs it out)
 *                 drop-ins   @SPIN@ @DOTS@ @NOISE@ @RULE@ @BELL@
 *                 fill-ins   @BOARD@ @DATE@ @TIME@
 *                 @@         a literal @
 *
 * Design:       The screen syntax, not {{FX1}}: a C64 has no braces, and
 *               '@' has a key on every machine that calls here. Anything
 *               that is not on the list prints exactly as typed, which is
 *               what keeps me@example.com safe.
 *
 *               Deliberately absent: @CLS@, @DELAY@ and @BAUD@, which reach
 *               into somebody else's screen or time, and @USER@, which
 *               shows the reader their own handle and makes "@USER@, sysop
 *               here, your password expired" easy to write. Screens keep
 *               all of those; they are the sysop's words, not a caller's.
 *
 *               The board sets the limits, not the writer: kPerMessage
 *               codes act in one message and the rest print as typed,
 *               @BELL@ rings once, durations are fixed here, and an effect
 *               that would eat the room the rest of the message needs is
 *               printed plain instead. A long message never loses words to
 *               a flourish.
 *
 * Interfaces:   Painter, wrap, row, endParagraph, plain
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     CLAUDE.md "Inline codes in messages", screens/codes.*
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
#include <cstddef>
#include <cstdint>
#include "term.h"
#include "timeline.h"

namespace codes {

constexpr uint8_t kPerMessage = 8;    // codes that act in one message or line
constexpr uint8_t kTextMax    = 40;   // characters of text inside an effect

// ---------------------------------------------------------------------------
// Painter: what one message, or one chat line, has spent so far.
//
// Carried from row to row, so the limits and a colour span the whole of a
// paragraph rather than stopping where the reader's terminal wrapped it.
// Lives on the caller's stack for the length of one message: nothing here
// outlives the drawing of it.
// ---------------------------------------------------------------------------
struct Painter {
    Color   base    = Color::Grey;    // the colour text starts in and returns to
    Color   cur     = Color::Grey;    // the colour now
    uint8_t used    = 0;              // codes that have acted
    bool    rang    = false;          // @BELL@ is once a message
    bool    bells   = true;           // the reader has the bell on
    size_t  reserve = 0;              // bytes still to come, which an effect must leave

    void begin(Color c, bool bellOn) {
        base = cur = c;
        used = 0;
        rang = false;
        bells = bellOn;
        reserve = 0;
    }
};

// wrap: bbsu::wrap for text that may carry codes.
//
// A code is as wide as what it prints, not as wide as it was typed, and a
// code is never split across two rows. Without this a coloured sentence
// wraps early by the width of its codes, and "@BLINK:hello world@" breaks
// at its own space and prints as two halves of a code. p is read, never
// changed: the codes it counts are the ones row() will then act on, in the
// same order, so the two agree about which codes are over the limit.
const char* wrap(const char* src, char* out, size_t outN, uint8_t cols, const Painter& p);

// row: one row with its codes acted on, starting in the painter's current
// colour. cols is the reader's width, for @RULE@ and to keep an effect's
// text inside the row. Prints no newline.
void row(Term& t, Timeline& tl, const char* text, uint8_t cols, Painter& p);

// endParagraph: the message has an explicit line break here. Colour goes
// back to the base, the way it does at the end of a chat line.
inline void endParagraph(Painter& p) { p.cur = p.base; }

// plain: the text with the codes taken out and an effect's own text kept.
// For previews and lists, where an effect would be noise and a colour code
// would read as garbage.
void plain(const char* src, char* out, size_t outN);

} // namespace codes
