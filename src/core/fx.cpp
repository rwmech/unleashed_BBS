/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/fx.cpp
 * Module:       Core / TTY effects
 *
 * Purpose:      TTY effects (see fx.h).
 *
 * Libraries:    none (libc only)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     README.md
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

#include "fx.h"
#include "../platform/platform.h"
#include <cstring>
#include <cstdio>

namespace fx {

namespace {

const char kNoise[]  = "!@#$%^&*()+=[];:,.<>/?~{}|ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const char kScramb[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#$%&*+=?";   // ASCII only: effects step per byte

// PETSCII quadrant blocks, rotating clockwise (verified on a C64)
const uint8_t kPetQuad[4] = { 0xBE, 0xBC, 0xAC, 0xBB };

char pick(const char* set, size_t n) {
    return set[plat::random32() % n];
}

} // namespace

// ---------------------------------------------------------------------------
// spinFrame: emit frame i of a spinner style. Public since 1.1.0, for a
// spinner that has to be driven one frame at a time from a clock rather than
// queued whole: a ring for the sysop spins for up to 45 seconds and stops on
// a key, and 45 seconds of frames does not fit in a timeline.
// ---------------------------------------------------------------------------
void spinFrame(Term& t, Timeline& tl, Spin style, uint8_t i) {
    static const char kLine[]  = "|/-\\";
    static const char kDots[]  = ".oOo";
    static const char kArrow[] = "^>v<";
    i &= 3;
    switch (style) {
        case Spin::Line:
            if (t.isPet()) tl.putc(kPetQuad[i]);
            else           t.ch(tl, kLine[i]);
            break;
        case Spin::Dots:  t.ch(tl, kDots[i]);  break;
        case Spin::Arrow: t.ch(tl, kArrow[i]); break;
    }
}

// ---------------------------------------------------------------------------
// fitSteps: clamp animation length to remaining Timeline capacity,
// keeping a small reserve for the effect's closing bytes
// ---------------------------------------------------------------------------
uint16_t fitSteps(const Timeline& tl, uint16_t wanted, uint16_t bytesPerStep) {
    size_t frames = tl.freeFrames();
    size_t bytes  = tl.freeBytes();
    if (frames <= 4 || bytes <= 96) return 0;
    frames -= 4;
    bytes  -= 96;
    size_t byBytes = bytesPerStep ? bytes / bytesPerStep : wanted;
    size_t n = wanted;
    if (n > frames)  n = frames;
    if (n > byBytes) n = byBytes;
    return static_cast<uint16_t>(n);
}

// ===========================================================================
// Timing
// ===========================================================================

void pause(Timeline& tl, uint16_t ms) {
    tl.delay(ms);
}

// ---------------------------------------------------------------------------
// baud: 10 bits per character on the wire (8N1)
// ---------------------------------------------------------------------------
void baud(Timeline& tl, uint32_t bps) {
    tl.setCps(static_cast<uint16_t>(bps / 10));
}

// ===========================================================================
// Text reveal
// ===========================================================================

// ---------------------------------------------------------------------------
// typewriter: one character per frame
// ---------------------------------------------------------------------------
void typewriter(Term& t, Timeline& tl, const char* s, uint16_t msPerChar) {
    size_t len   = strlen(s);
    uint16_t fit = fitSteps(tl, static_cast<uint16_t>(len), 4);
    for (size_t i = 0; i < len; ++i) {
        t.ch(tl, s[i]);
        if (i < fit) tl.delay(msPerChar);
    }
}

// ---------------------------------------------------------------------------
// scramble: random characters resolve left to right into the real text
// ---------------------------------------------------------------------------
void scramble(Term& t, Timeline& tl, const char* s, uint8_t rounds, uint16_t ms) {
    uint8_t len = static_cast<uint8_t>(strnlen(s, 60));
    if (!len) return;
    uint16_t per = static_cast<uint16_t>(len * 2 + 8);
    uint16_t r   = fitSteps(tl, rounds, per);
    for (uint16_t k = 0; k < r; ++k) {
        uint16_t revealed = static_cast<uint16_t>((len * k) / (r ? r : 1));
        for (uint8_t j = 0; j < len; ++j) {
            if (j < revealed || s[j] == ' ') t.ch(tl, s[j]);
            else                             t.ch(tl, pick(kScramb, sizeof(kScramb) - 1));
        }
        tl.delay(ms);
        t.left(tl, len);
    }
    t.textN(tl, s, len);
}

// ---------------------------------------------------------------------------
// blink: flash text in reverse video (PETSCII has no blink attribute)
//
// Up to 78 characters, the status line of a form at 80 columns (1.1.0): a
// form's refusal blinks there, and at 60 the end of a long one was cut off.
// fitSteps still shortens the flashing to what the timeline has room for.
// ---------------------------------------------------------------------------
void blink(Term& t, Timeline& tl, const char* s, uint8_t times, uint16_t ms) {
    uint8_t len = static_cast<uint8_t>(strnlen(s, 78));
    uint16_t n  = fitSteps(tl, static_cast<uint16_t>(times) * 2, len * 2 + 12);
    for (uint16_t i = 0; i + 1 < n; i += 2) {
        t.reverse(tl, true);
        t.textN(tl, s, len);
        t.reverse(tl, false);
        tl.delay(ms);
        t.left(tl, len);
        t.textN(tl, s, len);
        tl.delay(ms);
        t.left(tl, len);
    }
    t.textN(tl, s, len);
}

// ---------------------------------------------------------------------------
// marquee: scroll text through a fixed window, then clear the window
// ---------------------------------------------------------------------------
void marquee(Term& t, Timeline& tl, const char* s, uint8_t width, uint16_t ms, uint8_t loops) {
    // One cell a column. The micro sign is two bytes of UTF-8 and one
    // column, so it becomes a marker cell here and goes out as the Term's
    // Glyph::Micro (1.1.1): a real µ on UTF-8, CP437's 0xE6 on ANSI, a u
    // where there is none. Sent byte by byte it was two wrong characters.
    constexpr char kMicro = '\x01';
    char cell[61];
    uint8_t len = 0;
    for (const char* q = s; *q && len < sizeof(cell) - 1; ++q) {
        if (static_cast<uint8_t>(q[0]) == 0xC2 && static_cast<uint8_t>(q[1]) == 0xB5) {
            cell[len++] = kMicro;
            ++q;
        } else {
            cell[len++] = *q;
        }
    }
    if (!len || !width) return;
    uint16_t total = static_cast<uint16_t>(len + width);
    uint16_t steps = fitSteps(tl, static_cast<uint16_t>(total * loops), width * 2 + 8);
    // virtual strip = [width blanks][text]; text enters at the right edge
    for (uint16_t k = 0; k < steps; ++k) {
        for (uint8_t w = 0; w < width; ++w) {
            uint16_t p = static_cast<uint16_t>((k + 1 + w) % total);
            char c = p < width ? ' ' : cell[p - width];
            if (c == kMicro) t.glyph(tl, Glyph::Micro);
            else             t.ch(tl, c);
        }
        tl.delay(ms);
        t.left(tl, width);
    }
    for (uint8_t w = 0; w < width; ++w) t.ch(tl, ' ');
    t.left(tl, width);
}

// ===========================================================================
// Erase / rewrite
// ===========================================================================

// ---------------------------------------------------------------------------
// rubout: delete n characters one at a time, back toward line start
// ---------------------------------------------------------------------------
void rubout(Term& t, Timeline& tl, uint8_t n, uint16_t msEach) {
    uint16_t fit = fitSteps(tl, n, 4);
    for (uint8_t i = 0; i < n; ++i) {
        t.eraseBack(tl, 1);
        if (i < fit) tl.delay(msEach);
    }
}

void typeRubout(Term& t, Timeline& tl, const char* s,
                uint16_t msType, uint16_t hold, uint16_t msErase) {
    typewriter(t, tl, s, msType);
    tl.delay(hold);
    rubout(t, tl, static_cast<uint8_t>(strnlen(s, 255)), msErase);
}

// ---------------------------------------------------------------------------
// rewrite: replace the last oldLen characters with new text
// ---------------------------------------------------------------------------
void rewrite(Term& t, Timeline& tl, uint8_t oldLen, const char* newText) {
    t.eraseBack(tl, oldLen);
    t.text(tl, newText);
}

// ===========================================================================
// Activity indicators
// ===========================================================================

void dots(Term& t, Timeline& tl, uint8_t count, uint16_t msEach) {
    uint16_t fit = fitSteps(tl, count, 2);
    for (uint8_t i = 0; i < count; ++i) {
        t.ch(tl, '.');
        if (i < fit) tl.delay(msEach);
    }
}

// ---------------------------------------------------------------------------
// spinner: rotating character in place, erased when done
// ---------------------------------------------------------------------------
void spinner(Term& t, Timeline& tl, Spin style, uint16_t totalMs, uint16_t msEach) {
    if (!msEach) msEach = 100;
    uint16_t steps = fitSteps(tl, totalMs / msEach, 8);
    if (!steps) return;
    t.cursor(tl, false);
    spinFrame(t, tl, style, 0);
    tl.delay(msEach);
    for (uint16_t i = 1; i < steps; ++i) {
        t.eraseBack(tl, 1);
        spinFrame(t, tl, style, static_cast<uint8_t>(i));
        tl.delay(msEach);
    }
    t.eraseBack(tl, 1);
    t.cursor(tl, true);
}

// ---------------------------------------------------------------------------
// cursorBlink: block cursor blinking in place ("waiting for you")
// ---------------------------------------------------------------------------
void cursorBlink(Term& t, Timeline& tl, uint8_t times, uint16_t ms) {
    uint16_t n = fitSteps(tl, static_cast<uint16_t>(times) * 2, 8);
    for (uint16_t i = 0; i + 1 < n; i += 2) {
        t.glyph(tl, Glyph::Block);
        tl.delay(ms);
        t.eraseBack(tl, 1);
        tl.delay(ms);
    }
}

// ---------------------------------------------------------------------------
// working: "Label... [spin] result" on one line
// ---------------------------------------------------------------------------
void working(Term& t, Timeline& tl, const char* label, uint16_t ms, const char* result) {
    t.text(tl, label);
    spinner(t, tl, Spin::Line, ms, 90);
    t.text(tl, result);
    t.nl(tl);
}

// ---------------------------------------------------------------------------
// progressBar: [#####     ] filling left to right
// ---------------------------------------------------------------------------
void progressBar(Term& t, Timeline& tl, uint8_t width, uint16_t totalMs) {
    if (!width) return;
    t.ch(tl, '[');
    for (uint8_t i = 0; i < width; ++i) t.ch(tl, ' ');
    t.ch(tl, ']');
    t.left(tl, static_cast<uint8_t>(width + 1));
    uint16_t fit = fitSteps(tl, width, 6);
    uint16_t per = static_cast<uint16_t>(totalMs / width);
    for (uint8_t i = 0; i < width; ++i) {
        t.glyph(tl, Glyph::Block);
        if (i < fit) tl.delay(per);
    }
    t.ch(tl, ']');   // re-print over the bracket (ASCII cannot move right)
}

// ---------------------------------------------------------------------------
// countdown: 5..4..3..2..1 in place, then final text
// ---------------------------------------------------------------------------
void countdown(Term& t, Timeline& tl, uint8_t from, uint16_t msEach, const char* finalText) {
    uint16_t fit = fitSteps(tl, from, 12);
    char buf[4];
    for (uint8_t i = from; i > 0; --i) {
        snprintf(buf, sizeof(buf), "%u", i);
        t.text(tl, buf);
        if ((from - i) < fit) tl.delay(msEach);
        t.eraseBack(tl, static_cast<uint8_t>(strlen(buf)));
    }
    if (finalText) t.text(tl, finalText);
}

// ===========================================================================
// Line noise / modem
// ===========================================================================

// ---------------------------------------------------------------------------
// lineNoise: burst of garbage like a noisy phone line, then cleaned up
// ---------------------------------------------------------------------------
void lineNoise(Term& t, Timeline& tl, uint8_t n, uint16_t hold) {
    if (fitSteps(tl, 4, n + 16) < 4) return;
    uint8_t burst1 = n / 3;
    for (uint8_t i = 0; i < n; ++i) {
        t.ch(tl, pick(kNoise, sizeof(kNoise) - 1));
        if (i == burst1) tl.delay(60);
    }
    tl.delay(hold);
    t.eraseBack(tl, n);
}

void hangup(Term& t, Timeline& tl) {
    t.reset(tl);
    t.nl(tl);
    t.text(tl, "NO CARRIER");
    t.nl(tl);
}

void bell(Term& t, Timeline& tl) {
    t.bell(tl);
}

// ===========================================================================
// Decoration
// ===========================================================================

void rule(Term& t, Timeline& tl, uint8_t width) {
    t.glyphs(tl, Glyph::HLine, width);
}

} // namespace fx
