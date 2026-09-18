/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/term.cpp
 * Module:       Core / terminal layer
 *
 * Purpose:      Terminal drivers: PETSCII-40/80 (C64/C128, mixed-case
 *                  charset), ANSI (CP437 or UTF-8) and plain ASCII.
 *                  PETSCII glyph codes are from the standard C64 charset
 *                  tables and were verified on a C64 via TeensyROM.
 *
 * Libraries:    none (libc only)
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

#include "term.h"
#include <cstring>
#include <cstdio>

// ===========================================================================
// ByteSink helper
// ===========================================================================
bool ByteSink::puts(const char* s) {
    return put(reinterpret_cast<const uint8_t*>(s), strlen(s));
}

// ===========================================================================
// Tables
// ===========================================================================
namespace {

// PETSCII color control codes in Color enum order
const uint8_t kPetColor[16] = {
    0x90, 0x05, 0x1C, 0x9F, 0x9C, 0x1E, 0x1F, 0x9E,
    0x81, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B,
};

// ANSI SGR for each C64 color (bold flag, 30-37 base)
struct AnsiColor { uint8_t bold; uint8_t fg; };
const AnsiColor kAnsiColor[16] = {
    {0, 30}, {1, 37}, {0, 31}, {1, 36}, {0, 35}, {0, 32}, {0, 34}, {1, 33},
    {0, 33}, {0, 33}, {1, 31}, {1, 30}, {0, 37}, {1, 32}, {1, 34}, {0, 37},
};

// CP437 0x80-0xFF as Unicode code points (for UTF-8 terminals)
const uint16_t kCp437Hi[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

// ---------------------------------------------------------------------------
// putUtf8: encode one BMP code point
// ---------------------------------------------------------------------------
void putUtf8(ByteSink& o, uint16_t cp) {
    uint8_t b[3];
    if (cp < 0x80) {
        b[0] = static_cast<uint8_t>(cp);
        o.put(b, 1);
    } else if (cp < 0x800) {
        b[0] = static_cast<uint8_t>(0xC0 | (cp >> 6));
        b[1] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
        o.put(b, 2);
    } else {
        b[0] = static_cast<uint8_t>(0xE0 | (cp >> 12));
        b[1] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
        b[2] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
        o.put(b, 3);
    }
}

// ---------------------------------------------------------------------------
// putRepeat: n copies of a byte sequence
// ---------------------------------------------------------------------------
void putRepeat(ByteSink& o, const char* seq, uint8_t n) {
    for (uint8_t i = 0; i < n; ++i) o.puts(seq);
}

// ---------------------------------------------------------------------------
// asciiToPet: ASCII to PETSCII for the mixed-case (lowercase) charset
// ---------------------------------------------------------------------------
uint8_t asciiToPet(char c) {
    uint8_t u = static_cast<uint8_t>(c);
    if (u >= 'a' && u <= 'z') return static_cast<uint8_t>(u - 0x20); // $41-$5A
    if (u >= 'A' && u <= 'Z') return static_cast<uint8_t>(u + 0x80); // $C1-$DA
    switch (u) {
        case '\\': return '/';   // PETSCII has no backslash ($5C is pound)
        case '_':  return 0xA4;  // low bar graphic
        case '|':  return 0xDD;  // vertical line graphic
        case '{':  return '(';
        case '}':  return ')';
        case '~':  return '-';
        case '`':  return '\'';
        case '\t': return ' ';
        default:   break;
    }
    if (u >= 0x20 && u <= 0x5F) return u; // digits, punctuation, [ ] ^ @
    return '?';
}

} // namespace

// ===========================================================================
// Setup
// ===========================================================================

// ---------------------------------------------------------------------------
// setType: select driver and screen geometry
// ---------------------------------------------------------------------------
void Term::setType(TermType t, Charset cs, uint8_t cols, uint8_t rows) {
    type_ = t;
    cs_   = cs;
    cols_ = cols;
    rows_ = rows;
    rev_  = false;
    esc_  = 0;
}

// ---------------------------------------------------------------------------
// nameOf: human-readable terminal name for TERM / WHO / LAST
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Colour names. The palette is the C64 one, so the names are the C64 names
// with the obvious aliases: "grey" and "gray", "ltblue" and "lightblue".
// ---------------------------------------------------------------------------
namespace {
struct ColorName { const char* name; Color color; };
const ColorName kColorNames[] = {
    { "black",     Color::Black },      { "white",     Color::White },
    { "red",       Color::Red },        { "cyan",      Color::Cyan },
    { "purple",    Color::Purple },     { "magenta",   Color::Purple },
    { "green",     Color::Green },      { "blue",      Color::Blue },
    { "yellow",    Color::Yellow },     { "orange",    Color::Orange },
    { "brown",     Color::Brown },      { "ltred",     Color::LightRed },
    { "lightred",  Color::LightRed },   { "darkgrey",  Color::DarkGrey },
    { "darkgray",  Color::DarkGrey },   { "dgrey",     Color::DarkGrey },
    { "grey",      Color::Grey },       { "gray",      Color::Grey },
    { "ltgreen",   Color::LightGreen }, { "lightgreen", Color::LightGreen },
    { "ltblue",    Color::LightBlue },  { "lightblue", Color::LightBlue },
    { "ltgrey",    Color::LightGrey },  { "lightgrey", Color::LightGrey },
    { "ltgray",    Color::LightGrey },
};
} // namespace

Color colorByName(const char* name, Color fallback) {
    if (!name || !*name) return fallback;
    for (const ColorName& c : kColorNames) {
        const char* a = c.name;
        const char* b = name;
        while (*a && *b && *a == (*b >= 'A' && *b <= 'Z' ? *b + 32 : *b)) { ++a; ++b; }
        if (!*a && !*b) return c.color;
    }
    return fallback;
}

const char* colorName(Color c) {
    for (const ColorName& n : kColorNames) if (n.color == c) return n.name;
    return "white";
}

const char* Term::nameOf(TermType t, Charset cs) {
    switch (t) {
        case TermType::Pet40: return "PETSCII-40";
        case TermType::Pet80: return "PETSCII-80";
        case TermType::Ansi:  return cs == Charset::Utf8 ? "ANSI-UTF8" : "ANSI-CP437";
        case TermType::Ascii: return "ASCII";
        default:              return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// init: known state on the remote side
// ---------------------------------------------------------------------------
void Term::init(ByteSink& o) {
    if (isPet()) {
        const uint8_t seq[] = { 0x0E,   // mixed-case charset
                                0x08,   // lock Shift+C= charset toggle
                                0x92,   // reverse off
                                0x93 }; // clear screen
        o.put(seq, sizeof(seq));
        color(o, Color::LightGrey);
    } else if (isAnsi()) {
        o.puts("\x1b[0m\x1b[2J\x1b[H");
    } else {
        o.puts("\r\n");
    }
    rev_ = false;
}

// ===========================================================================
// Output
// ===========================================================================

// ---------------------------------------------------------------------------
// ch: one ASCII character through the active driver
// ---------------------------------------------------------------------------
void Term::ch(ByteSink& o, char c) {
    if (c == '\n') { nl(o); return; }
    if (c == '\r') return;
    if (isPet()) {
        o.putc(asciiToPet(c));
        return;
    }
    uint8_t u = static_cast<uint8_t>(c);
    if (u == '\t') u = ' ';
    if (u < 0x20 || u > 0x7E) u = '?';
    o.putc(u);
}

// ---------------------------------------------------------------------------
// text / textN: ASCII strings
// ---------------------------------------------------------------------------
// The only non-ASCII character allowed above this layer is UTF-8 µ
// (C2 B5), used by the BBS name; it becomes Glyph::Micro per terminal.
void Term::text(ByteSink& o, const char* s) {
    while (*s) {
        if (static_cast<uint8_t>(s[0]) == 0xC2 && static_cast<uint8_t>(s[1]) == 0xB5) {
            glyph(o, Glyph::Micro);
            s += 2;
            continue;
        }
        ch(o, *s++);
    }
}

void Term::textN(ByteSink& o, const char* s, size_t n) {
    for (size_t i = 0; i < n && s[i]; ++i) {
        if (i + 1 < n && static_cast<uint8_t>(s[i]) == 0xC2 && static_cast<uint8_t>(s[i + 1]) == 0xB5) {
            glyph(o, Glyph::Micro);
            ++i;
            continue;
        }
        ch(o, s[i]);
    }
}

// ---------------------------------------------------------------------------
// nl: newline. PETSCII has no CR-without-LF, so line rewrites must use
// cursor-left or DEL, never a bare carriage return.
// ---------------------------------------------------------------------------
void Term::nl(ByteSink& o) {
    if (isPet()) o.putc(0x0D);
    else         o.puts("\r\n");
}

// ---------------------------------------------------------------------------
// cls: clear screen and home
// ---------------------------------------------------------------------------
void Term::cls(ByteSink& o) {
    if (isPet())       o.putc(0x93);
    else if (isAnsi()) o.puts("\x1b[2J\x1b[H");
    else               o.puts("\r\n\r\n");
}

// ---------------------------------------------------------------------------
// color: foreground color (backgrounds are left to the terminal)
// ---------------------------------------------------------------------------
void Term::color(ByteSink& o, Color c) {
    uint8_t i = static_cast<uint8_t>(c) & 0x0F;
    if (isPet()) {
        o.putc(kPetColor[i]);
    } else if (isAnsi()) {
        char buf[16];
        snprintf(buf, sizeof(buf), "\x1b[%u;%u%sm",
                 kAnsiColor[i].bold, kAnsiColor[i].fg, rev_ ? ";7" : "");
        o.puts(buf);
    }
}

// ---------------------------------------------------------------------------
// reset: default attributes
// ---------------------------------------------------------------------------
void Term::reset(ByteSink& o) {
    rev_ = false;
    if (isPet()) {
        o.putc(0x92);
        color(o, Color::LightGrey);
    } else if (isAnsi()) {
        o.puts("\x1b[0m");
    }
}

// ---------------------------------------------------------------------------
// reverse: reverse video on/off
// ---------------------------------------------------------------------------
void Term::reverse(ByteSink& o, bool on) {
    rev_ = on;
    if (isPet())       o.putc(on ? 0x12 : 0x92);
    else if (isAnsi()) o.puts(on ? "\x1b[7m" : "\x1b[27m");
}

// ---------------------------------------------------------------------------
// Cursor movement (ASCII can only move left)
// ---------------------------------------------------------------------------
void Term::left(ByteSink& o, uint8_t n) {
    if (!n) return;
    if (isPet()) { for (uint8_t i = 0; i < n; ++i) o.putc(0x9D); return; }
    if (isAnsi() && n > 3) {
        char buf[12];
        snprintf(buf, sizeof(buf), "\x1b[%uD", n);
        o.puts(buf);
        return;
    }
    putRepeat(o, "\b", n);
}

void Term::right(ByteSink& o, uint8_t n) {
    if (!n) return;
    if (isPet()) { for (uint8_t i = 0; i < n; ++i) o.putc(0x1D); return; }
    if (isAnsi()) {
        char buf[12];
        snprintf(buf, sizeof(buf), "\x1b[%uC", n);
        o.puts(buf);
    }
}

void Term::up(ByteSink& o, uint8_t n) {
    if (!n) return;
    if (isPet()) { for (uint8_t i = 0; i < n; ++i) o.putc(0x91); return; }
    if (isAnsi()) {
        char buf[12];
        snprintf(buf, sizeof(buf), "\x1b[%uA", n);
        o.puts(buf);
    }
}

void Term::down(ByteSink& o, uint8_t n) {
    if (!n) return;
    if (isPet()) { for (uint8_t i = 0; i < n; ++i) o.putc(0x11); return; }
    if (isAnsi()) {
        char buf[12];
        snprintf(buf, sizeof(buf), "\x1b[%uB", n);
        o.puts(buf);
    }
}

void Term::home(ByteSink& o) {
    if (isPet())       o.putc(0x13);
    else if (isAnsi()) o.puts("\x1b[H");
}

void Term::gotoXY(ByteSink& o, uint8_t x, uint8_t y) {
    if (isPet()) {
        home(o);
        down(o, y > 0 ? y - 1 : 0);
        right(o, x > 0 ? x - 1 : 0);
    } else if (isAnsi()) {
        char buf[16];
        snprintf(buf, sizeof(buf), "\x1b[%u;%uH", y, x);
        o.puts(buf);
    }
}

// ---------------------------------------------------------------------------
// eraseBack: destructive backspace. PETSCII DEL removes the char to the
// left of the cursor; PC terminals need BS-space-BS.
// ---------------------------------------------------------------------------
void Term::eraseBack(ByteSink& o, uint8_t n) {
    if (!n) return;
    if (isPet()) { for (uint8_t i = 0; i < n; ++i) o.putc(0x14); return; }
    if (isAnsi() && n > 1) {
        char buf[16];
        snprintf(buf, sizeof(buf), "\x1b[%uD\x1b[K", n);
        o.puts(buf);
        return;
    }
    putRepeat(o, "\b \b", n);
}

// ---------------------------------------------------------------------------
// eraseEol: clear to end of line (hint = columns to blank when the
// terminal has no native erase)
// ---------------------------------------------------------------------------
void Term::eraseEol(ByteSink& o, uint8_t hint) {
    if (isAnsi()) { o.puts("\x1b[K"); return; }
    for (uint8_t i = 0; i < hint; ++i) o.putc(' ');
    left(o, hint);
}

// ---------------------------------------------------------------------------
// glyph: best drawing character for the active terminal
// ---------------------------------------------------------------------------
void Term::glyph(ByteSink& o, Glyph g) {
    if (isPet()) {
        switch (g) {
            case Glyph::Block: {
                const uint8_t s[] = {0x12, 0x20, 0x92};   // reverse space
                o.put(s, 3);
                if (rev_) o.putc(0x12);                   // restore reverse
                break;
            }
            case Glyph::Shade:  o.putc(0xA6); break; // checker
            case Glyph::HLine:  o.putc(0xC0); break; // horizontal line
            case Glyph::VLine:  o.putc(0xDD); break; // vertical line
            case Glyph::Bullet: o.putc('*');  break;
            case Glyph::Micro:  o.putc(0x55); break; // no micro sign: lowercase u
        }
        return;
    }
    if (isAnsi()) {
        uint8_t b = 0;
        switch (g) {
            case Glyph::Block:  b = 0xDB; break;
            case Glyph::Shade:  b = 0xB1; break;
            case Glyph::HLine:  b = 0xC4; break;
            case Glyph::VLine:  b = 0xB3; break;
            case Glyph::Bullet: b = 0xF9; break;
            case Glyph::Micro:  b = 0xE6; break;     // CP437 micro sign
        }
        cp437(o, b);
        return;
    }
    switch (g) {
        case Glyph::Block:  o.putc('#'); break;
        case Glyph::Shade:  o.putc(':'); break;
        case Glyph::HLine:  o.putc('-'); break;
        case Glyph::VLine:  o.putc('|'); break;
        case Glyph::Bullet: o.putc('*'); break;
        case Glyph::Micro:  o.putc('u'); break;
    }
}

void Term::glyphs(ByteSink& o, Glyph g, uint8_t n) {
    for (uint8_t i = 0; i < n; ++i) glyph(o, g);
}

void Term::bell(ByteSink& o) {
    o.putc(0x07);
}

void Term::cursor(ByteSink& o, bool visible) {
    if (isAnsi()) o.puts(visible ? "\x1b[?25h" : "\x1b[?25l");
}

// ---------------------------------------------------------------------------
// cp437: one byte from an ANSI art file, re-encoded for the terminal
// ---------------------------------------------------------------------------
void Term::cp437(ByteSink& o, uint8_t b) {
    if (b < 0x80) { o.putc(b); return; }
    if (cs_ == Charset::Utf8) { putUtf8(o, kCp437Hi[b - 0x80]); return; }
    if (cs_ == Charset::Cp437) {
        o.putc(b);
        if (b == 0xFF && iacEsc_) o.putc(0xFF);   // telnet IAC escape
        return;
    }
    o.putc('?');
}

// ===========================================================================
// Input
// ===========================================================================

// ---------------------------------------------------------------------------
// feed: translate one inbound byte into key events
// ---------------------------------------------------------------------------
void Term::feed(uint8_t b, KeyFn fn, void* ctx) {
    if (isPet()) {
        int k = KEY_NONE;
        switch (b) {
            case 0x0D: k = KEY_ENTER; break;
            case 0x14: k = KEY_BACKSPACE; break;
            case 0x5F: k = KEY_ESC; break;      // left-arrow key
            case 0x03: k = KEY_BREAK; break;    // RUN/STOP
            case 0x91: k = KEY_UP; break;
            case 0x11: k = KEY_DOWN; break;
            case 0x9D: k = KEY_LEFT; break;
            case 0x1D: k = KEY_RIGHT; break;
            case 0x13: k = KEY_HOME; break;
            case 0x85: k = KEY_F1; break;
            case 0x89: k = KEY_F2; break;
            case 0x86: k = KEY_F3; break;
            case 0x8A: k = KEY_F4; break;
            case 0x87: k = KEY_F5; break;
            case 0x8B: k = KEY_F6; break;
            case 0x88: k = KEY_F7; break;
            case 0x8C: k = KEY_F8; break;
            case 0x5C: k = '#'; break;          // pound sign
            case 0x5E: k = '^'; break;          // up-arrow
            case 0xDD: k = '|'; break;
            case 0xA4: k = '_'; break;
            default:
                if (b >= 0x41 && b <= 0x5A)      k = b + 0x20;  // lower
                else if (b >= 0xC1 && b <= 0xDA) k = b - 0x80;  // upper
                else if (b >= 0x61 && b <= 0x7A) k = b - 0x20;  // shifted alt
                else if (b >= 0x20 && b <= 0x40) k = b;
                else if (b == 0x5B || b == 0x5D) k = b;
                break;
        }
        if (k != KEY_NONE) fn(ctx, k);
        return;
    }

    // ANSI / ASCII
    switch (esc_) {
        case 0:
            if (b == 0x1B)                    { esc_ = 1; escParam_ = 0; return; }
            if (b == 0x0D || b == 0x0A)       { fn(ctx, KEY_ENTER); return; }
            if (b == 0x08 || b == 0x7F)       { fn(ctx, KEY_BACKSPACE); return; }
            if (b == 0x03)                    { fn(ctx, KEY_BREAK); return; }
            if (b >= 0x20 && b <= 0x7E)       { fn(ctx, b); return; }
            return;
        case 1:
            if (b == '[') { esc_ = 2; return; }
            if (b == 'O') { esc_ = 3; return; }
            esc_ = 0;
            fn(ctx, KEY_ESC);
            feed(b, fn, ctx);
            return;
        case 2: // CSI: params, then final byte
            if (b >= '0' && b <= '9') {
                if (escParam_ < 100) escParam_ = static_cast<uint8_t>(escParam_ * 10 + (b - '0'));
                return;
            }
            if (b < 0x40 || b > 0x7E) return;   // other params/intermediates
            esc_ = 0;
            switch (b) {
                case 'A': fn(ctx, KEY_UP); break;
                case 'B': fn(ctx, KEY_DOWN); break;
                case 'C': fn(ctx, KEY_RIGHT); break;
                case 'D': fn(ctx, KEY_LEFT); break;
                case 'H': fn(ctx, KEY_HOME); break;
                case '~':
                    if (escParam_ == 1 || escParam_ == 7)        fn(ctx, KEY_HOME);
                    else if (escParam_ >= 11 && escParam_ <= 15) fn(ctx, KEY_F1 + (escParam_ - 11));
                    else if (escParam_ >= 17 && escParam_ <= 19) fn(ctx, KEY_F6 + (escParam_ - 17));
                    break;
                default: break;                  // stray CPR ('R') etc.
            }
            return;
        case 3: // SS3
            esc_ = 0;
            switch (b) {
                case 'P': fn(ctx, KEY_F1); break;
                case 'Z': fn(ctx, KEY_UP); break;   // shift-tab: back a field
                case 'Q': fn(ctx, KEY_F2); break;
                case 'R': fn(ctx, KEY_F3); break;
                case 'S': fn(ctx, KEY_F4); break;
                case 'A': fn(ctx, KEY_UP); break;
                case 'B': fn(ctx, KEY_DOWN); break;
                case 'C': fn(ctx, KEY_RIGHT); break;
                case 'D': fn(ctx, KEY_LEFT); break;
                case 'H': fn(ctx, KEY_HOME); break;
                default: break;
            }
            return;
        default:
            esc_ = 0;
            return;
    }
}

// ---------------------------------------------------------------------------
// idle: a lone ESC with nothing after it is the ESC key itself
// ---------------------------------------------------------------------------
void Term::idle(KeyFn fn, void* ctx) {
    if (esc_ == 1) {
        esc_ = 0;
        fn(ctx, KEY_ESC);
    }
}
