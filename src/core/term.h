/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/term.h
 * Module:       Core / terminal layer
 *
 * Purpose:      Terminal layer. One interface, four drivers: PETSCII-40,
 *                  PETSCII-80, ANSI (CP437 or UTF-8) and plain ASCII.
 *                  Everything above this layer writes ASCII strings plus
 *                  abstract operations (color, cursor, glyph). Plugins must
 *                  never emit raw control codes.
 *
 * Design:       Nothing above this layer emits raw control codes: callers use text,
 *               color, glyph and cursor operations, and each driver renders them.
 *
 * Interfaces:   Term (setType, text, color, glyph, cursor, feed, idle), Glyph, Color, Key
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     README.md
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
#include <cstddef>

// ---------------------------------------------------------------------------
// ByteSink: anything that accepts outgoing bytes (Timeline, raw socket)
// ---------------------------------------------------------------------------
class ByteSink {
public:
    virtual ~ByteSink() = default;
    virtual bool put(const uint8_t* data, size_t len) = 0;
    bool putc(uint8_t b) { return put(&b, 1); }
    bool puts(const char* s);
};

enum class TermType : uint8_t { Unknown, Ascii, Ansi, Pet40, Pet80 };
enum class Charset  : uint8_t { Ascii, Cp437, Utf8, Petscii };

// C64 palette order, mapped to the nearest ANSI color on PC terminals
enum class Color : uint8_t {
    Black, White, Red, Cyan, Purple, Green, Blue, Yellow,
    Orange, Brown, LightRed, DarkGrey, Grey, LightGreen, LightBlue, LightGrey
};

// colorByName: a colour written in a config file ("cyan", "ltgreen") to a
// palette entry. Returns fallback when the name is not one of ours, so a
// typo in system.cfg leaves the screen readable instead of blank.
Color colorByName(const char* name, Color fallback);

// colorName: the name colorByName accepts, for writing settings back out
const char* colorName(Color c);

// Drawing glyphs with a per-terminal best match
// HLine2 is the double horizontal rule, APPENDED because this enum is
// indexed by the translation tables in term.cpp and inserting into it
// would silently renumber every glyph after the insertion point.
// The forums use it for the row a caller acts on, where plain ASCII has
// no colour and no reverse video to make that row different: '=' against
// '-' is the distinction that 1987 used and it still reads.
enum class Glyph : uint8_t { Block, Shade, HLine, VLine, Bullet, Micro, HLine2 };

// Canonical key codes returned by Term::feed (printables are plain ASCII)
enum Key : int {
    KEY_NONE      = -1,
    KEY_ENTER     = 0x100,
    KEY_BACKSPACE,
    KEY_ESC,        // ESC on PC, left-arrow key on C64 (classic abort key)
    KEY_BREAK,      // Ctrl-C on PC, RUN/STOP on C64
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME,
    KEY_CLEAR,      // Ctrl-L on PC, SHIFT+CLR/HOME on C64
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8,
};

using KeyFn = void (*)(void* ctx, int key);

class Term {
public:
    // -- setup ---------------------------------------------------------
    void setType(TermType t, Charset cs, uint8_t cols, uint8_t rows);
    void setGeometry(uint8_t cols, uint8_t rows) { if (cols) cols_ = cols; if (rows) rows_ = rows; }
    // telnet sessions must double any 0xFF data byte (IAC IAC)
    void setIacEscape(bool on) { iacEsc_ = on; }
    bool iacEscape()   const { return iacEsc_; }
    TermType type()    const { return type_; }
    Charset  charset() const { return cs_; }
    uint8_t  cols()    const { return cols_; }
    uint8_t  rows()    const { return rows_; }
    bool     isPet()   const { return type_ == TermType::Pet40 || type_ == TermType::Pet80; }
    bool     isAnsi()  const { return type_ == TermType::Ansi; }
    const char* name() const { return nameOf(type_, cs_); }
    static const char* nameOf(TermType t, Charset cs);

    // init: put the remote terminal in a known state (charset, colors, cls)
    void init(ByteSink& o);

    // -- output ----------------------------------------------------------
    void ch(ByteSink& o, char c);                  // one ASCII char, translated
    void text(ByteSink& o, const char* s);

    // raw: bytes straight out, with only telnet's IAC doubled.
    //
    // For a file transfer. Nothing else on the way out may touch the bytes:
    // no charset mapping, no CR rewriting, no colour. Term::text and
    // Term::ch both translate, so neither can carry a binary file, and
    // ByteSink::put does no escaping at all, so it cannot carry one once
    // telnet has been negotiated. Hence this.
    void raw(ByteSink& o, const uint8_t* b, size_t n);         // ASCII string, '\n' = newline, UTF-8 µ allowed
    void textN(ByteSink& o, const char* s, size_t n);

    // textCols: at most maxCols columns of s, returning how many were
    // written. Multi-byte characters count as one column and are never
    // split, which is why a caller must not do this arithmetic itself.
    uint8_t textCols(ByteSink& o, const char* s, uint8_t maxCols);
    void nl(ByteSink& o);
    void cls(ByteSink& o);
    void color(ByteSink& o, Color c);
    void reset(ByteSink& o);                       // default color, no reverse
    void reverse(ByteSink& o, bool on);

    // eolClear: erase from the cursor to the end of the line, and say
    // whether it happened. False means the caller has to pad with spaces
    // itself, which is the only option on PETSCII.
    //
    // Refused while reverse video is on. The erase paints with the current
    // attributes, so under a reverse bar it would extend the bar to the
    // edge of the screen rather than clear the row, and terminals do not
    // agree with each other about it. Spaces are unambiguous there.
    bool eolClear(ByteSink& o);
    void left(ByteSink& o, uint8_t n);             // non-destructive moves
    void right(ByteSink& o, uint8_t n);
    void up(ByteSink& o, uint8_t n);
    void down(ByteSink& o, uint8_t n);
    void home(ByteSink& o);
    void gotoXY(ByteSink& o, uint8_t x, uint8_t y); // 1-based
    void eraseBack(ByteSink& o, uint8_t n);        // destructive backspace x n
    void eraseEol(ByteSink& o, uint8_t hint);      // ANSI ESC[K, else hint spaces
    void glyph(ByteSink& o, Glyph g);
    void glyphs(ByteSink& o, Glyph g, uint8_t n);
    void bell(ByteSink& o);
    void cursor(ByteSink& o, bool visible);        // ANSI only
    void cp437(ByteSink& o, uint8_t b);            // one CP437 byte, re-encoded

    // -- input -----------------------------------------------------------
    // feed: raw byte (after telnet filtering) -> zero or more key events
    void feed(uint8_t b, KeyFn fn, void* ctx);

    // idle: call when input has been quiet; resolves a lone ESC keypress
    void idle(KeyFn fn, void* ctx);

private:
    TermType type_ = TermType::Unknown;
    Charset  cs_   = Charset::Ascii;
    uint8_t  cols_ = 80;
    uint8_t  rows_ = 24;
    bool     rev_  = false;
    // The last colour set. reverse(o,false) re-asserts it instead of sending
    // ESC[27m, so turning reverse off does not depend on the terminal
    // implementing the least widely supported code in the SGR set.
    Color    cur_  = Color::LightGrey;
    bool     iacEsc_ = false;

    // input escape parser (ANSI arrow keys, stray CPR replies)
    uint8_t  esc_      = 0;   // 0 idle, 1 got ESC, 2 in CSI, 3 in SS3
    uint8_t  escParam_ = 0;   // first numeric CSI parameter
};
