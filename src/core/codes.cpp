/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/codes.cpp
 * Module:       Core / inline codes in messages
 *
 * Purpose:      Inline @-codes in callers' messages (see codes.h).
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     codes.h
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

#include "codes.h"
#include "fx.h"
#include "clock.h"
#include "sysconfig.h"
#include "../config.h"
#include <cstring>

namespace codes {
namespace {

// The longest code there can be: "@SCRAMBLE:" plus kTextMax plus the "@".
// Looking further for a closing @ than that only finds the start of
// somebody's next code and misreads everything between.
constexpr uint8_t kScan = 10 + kTextMax + 1;

// How long the effects take. Fixed here rather than chosen by the writer,
// so nobody can hold every reader's screen for a minute.
constexpr uint16_t kTypeMs     = 40;    // @TYPE@, per character
constexpr uint8_t  kScrambles  = 8;     // @SCRAMBLE@ rounds
constexpr uint16_t kScrambleMs = 60;
constexpr uint8_t  kBlinks     = 3;     // @BLINK@ flashes
constexpr uint16_t kBlinkMs    = 250;
constexpr uint16_t kOopsHoldMs = 600;   // @OOPS@ stays this long before it goes
constexpr uint16_t kOopsEraseMs = 30;
constexpr uint16_t kSpinMs     = 800;   // @SPIN@
constexpr uint8_t  kDots       = 3;     // @DOTS@
constexpr uint16_t kDotMs      = 250;
constexpr uint8_t  kNoise      = 6;     // @NOISE@ characters
constexpr uint16_t kNoiseMs    = 300;

enum class Kind : uint8_t {
    None,       // this @ is just a character
    At,         // @@, a literal @
    Unknown,    // shaped like a code and not one of ours: printed whole, as typed
    Colour, Normal,
    Blink, Scramble, Type, Oops,
    Spin, Dots, Noise, Rule, Bell,
    Board, Date, Time,
};

struct Code {
    Kind        kind   = Kind::None;
    uint8_t     bytes  = 0;             // in the source, both @ included
    Color       colour = Color::Grey;
    const char* arg    = nullptr;       // an effect's text
    uint8_t     argLen = 0;
};

bool takesText(Kind k) {
    return k == Kind::Blink || k == Kind::Scramble || k == Kind::Type || k == Kind::Oops;
}

// ---------------------------------------------------------------------------
// parse: the code starting at src, whose first byte is '@'.
//
// Strict on purpose. A name is letters only, an argument is only for the
// four effects, and anything that does not fit is not a code and prints as
// typed. Being generous here is how "email me@home or @work@" turns into a
// colour change nobody asked for.
//
// Something SHAPED like a code (letters between two @) that is not one of
// ours is Unknown and prints whole, both @ included. Without that, the
// closing @ of "@CLS@" and the opening @ of a code straight after it read
// as "@@", the escape for a literal @, and "@CLS@@RED@" came out as
// "@CLS@RED@" with the second code eaten.
// ---------------------------------------------------------------------------
Code parse(const char* src) {
    Code c;
    if (src[0] != '@') return c;
    if (src[1] == '@') { c.kind = Kind::At; c.bytes = 2; return c; }

    const char* end = nullptr;
    for (uint8_t i = 1; i < kScan && src[i] && src[i] != '\n'; ++i)
        if (src[i] == '@') { end = src + i; break; }
    if (!end) return c;

    char name[12];
    uint8_t n = 0;
    const char* q = src + 1;
    while (q < end && *q != ':') {
        char ch = *q;
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 32);
        if (ch < 'A' || ch > 'Z' || n >= sizeof(name) - 1) return Code{};
        name[n++] = ch;
        ++q;
    }
    if (!n) return Code{};
    name[n] = '\0';
    Code unknown;                          // what anything not ours becomes
    unknown.kind  = Kind::Unknown;
    unknown.bytes = static_cast<uint8_t>(end - src + 1);
    const char* arg = (q < end) ? q + 1 : nullptr;              // after the ':'
    uint8_t argLen = arg ? static_cast<uint8_t>(end - arg) : 0;

    auto is = [&](const char* w) { return strcmp(name, w) == 0; };
    Kind k = Kind::None;
    if      (is("BLINK"))    k = Kind::Blink;
    else if (is("SCRAMBLE")) k = Kind::Scramble;
    else if (is("TYPE"))     k = Kind::Type;
    else if (is("OOPS"))     k = Kind::Oops;
    else if (is("N") || is("NORMAL")) k = Kind::Normal;
    else if (is("SPIN"))     k = Kind::Spin;
    else if (is("DOTS"))     k = Kind::Dots;
    else if (is("NOISE"))    k = Kind::Noise;
    else if (is("RULE"))     k = Kind::Rule;
    else if (is("BELL"))     k = Kind::Bell;
    else if (is("BOARD"))    k = Kind::Board;
    else if (is("DATE"))     k = Kind::Date;
    else if (is("TIME"))     k = Kind::Time;
    else {
        // Black is not offered: black text on a black screen is a way to
        // hide words from the reader, not a colour.
        Color col = colorByName(name, Color::Black);
        if (col == Color::Black) return unknown;
        k = Kind::Colour;
        c.colour = col;
    }

    if (takesText(k)) {
        if (!arg || !argLen || argLen > kTextMax) return unknown;
        c.arg    = arg;
        c.argLen = argLen;
    } else if (arg) {
        return unknown;                   // nothing else takes an argument
    }
    c.kind  = k;
    c.bytes = static_cast<uint8_t>(end - src + 1);
    return c;
}

// visible: columns a run of text takes. A UTF-8 continuation byte takes
// none, so the micro sign counts once.
uint8_t visible(const char* s, size_t n) {
    uint8_t w = 0;
    for (size_t i = 0; i < n && s[i]; ++i)
        if ((static_cast<uint8_t>(s[i]) & 0xC0) != 0x80) ++w;
    return w;
}

// fill: what a fill-in prints. The same text for measuring and drawing, so
// the wrap and the row can never disagree about how wide it is.
const char* fill(Kind k, char* buf, size_t n) {
    switch (k) {
        case Kind::Board: {
            // The same fallback as the screens' @BOARD@ (1.1.2): a board with
            // no name set is called by its hostname, not by the software.
            const SysConfig& c = syscfg::get();
            return c.boardName[0] ? c.boardName : (c.hostname[0] ? c.hostname : BBS_HOSTNAME);
        }
        case Kind::Date: clk::fmt(buf, n, "%d %b %Y"); return buf;
        case Kind::Time: clk::fmt(buf, n, "%H:%M");    return buf;
        default:         buf[0] = '\0';                return buf;
    }
}

// width: columns a code takes on the row, when it acts.
//
// The transient ones (@SPIN@, @NOISE@, @OOPS@) are counted at the width
// they take while they run, not the nothing they leave behind: a spinner in
// the last column pushes the terminal onto the next row, and an erase cannot
// follow it back up. @RULE@ is handled by the caller, since it takes
// whatever is left of the row.
uint8_t width(const Code& c) {
    char buf[48];
    switch (c.kind) {
        case Kind::At:       return 1;
        case Kind::Blink:
        case Kind::Scramble:
        case Kind::Type:
        case Kind::Oops:     return visible(c.arg, c.argLen);
        case Kind::Spin:     return 1;
        case Kind::Dots:     return kDots;
        case Kind::Noise:    return kNoise;
        case Kind::Board:
        case Kind::Date:
        case Kind::Time: {
            const char* f = fill(c.kind, buf, sizeof(buf));
            return visible(f, strlen(f));
        }
        default:             return 0;
    }
}

// afford: may an effect take its frames and bytes and still leave the rest
// of the message room to print? Timeline::put is all or nothing and its
// callers do not check, so an effect that ate the buffer would cost later
// words silently. This is the whole of "a long message never loses words".
bool afford(const Timeline& tl, const Painter& p, size_t cost) {
    return tl.freeFrames() > 24 && tl.freeBytes() > p.reserve + cost + 160;
}

// act: one code that is within the limit.
void act(Term& t, Timeline& tl, const Code& c, uint8_t cols, uint8_t& col, Painter& p) {
    char text[kTextMax + 1];
    uint8_t room = col < cols ? static_cast<uint8_t>(cols - col) : 0;
    if (takesText(c.kind)) {
        // Cut to what is left of the row rather than let the terminal wrap
        // it: an effect that moves the cursor back cannot follow a wrap.
        uint8_t n = c.argLen;
        while (n && visible(c.arg, n) > room) --n;
        memcpy(text, c.arg, n);
        text[n] = '\0';
    }
    uint8_t len = takesText(c.kind) ? visible(text, strlen(text)) : 0;

    switch (c.kind) {
        case Kind::Colour:
            p.cur = c.colour;
            t.color(tl, p.cur);
            break;
        case Kind::Normal:
            p.cur = p.base;
            t.color(tl, p.cur);
            break;
        case Kind::Blink:
            if (afford(tl, p, 6u * (strlen(text) * 2 + 12))) fx::blink(t, tl, text, kBlinks, kBlinkMs);
            else                                           t.text(tl, text);
            col = static_cast<uint8_t>(col + len);
            break;
        case Kind::Scramble:
            if (afford(tl, p, kScrambles * (strlen(text) * 2 + 8))) fx::scramble(t, tl, text, kScrambles, kScrambleMs);
            else                                                  t.text(tl, text);
            col = static_cast<uint8_t>(col + len);
            break;
        case Kind::Type:
            // typewriter degrades on its own: it always prints the text and
            // only drops the pauses it cannot fit.
            if (afford(tl, p, strlen(text) * 4)) fx::typewriter(t, tl, text, kTypeMs);
            else                                t.text(tl, text);
            col = static_cast<uint8_t>(col + len);
            break;
        case Kind::Oops:
            // Said and taken back, so with no room for the show there is
            // nothing to print: the words were meant to go anyway.
            if (afford(tl, p, strlen(text) * 8 + 16))
                fx::typeRubout(t, tl, text, kTypeMs, kOopsHoldMs, kOopsEraseMs);
            break;
        case Kind::Spin:
            if (room && afford(tl, p, 96)) fx::spinner(t, tl, fx::Spin::Line, kSpinMs, 100);
            break;
        case Kind::Dots:
            fx::dots(t, tl, kDots, kDotMs);
            col = static_cast<uint8_t>(col + kDots);
            break;
        case Kind::Noise:
            if (room >= kNoise && afford(tl, p, kNoise + 32)) fx::lineNoise(t, tl, kNoise, kNoiseMs);
            break;
        case Kind::Rule:
            if (room) t.glyphs(tl, Glyph::HLine, room);
            col = cols;
            break;
        case Kind::Bell:
            if (p.bells && !p.rang) t.bell(tl);
            p.rang = true;
            break;
        case Kind::Board:
        case Kind::Date:
        case Kind::Time: {
            char buf[48];
            const char* f = fill(c.kind, buf, sizeof(buf));
            t.text(tl, f);
            col = static_cast<uint8_t>(col + visible(f, strlen(f)));
            break;
        }
        default:
            break;
    }
    // An effect may leave the terminal in reverse or in another colour.
    t.color(tl, p.cur);
}

} // namespace

// ---------------------------------------------------------------------------
// wrap
// ---------------------------------------------------------------------------
const char* wrap(const char* src, char* out, size_t outN, uint8_t cols, const Painter& p) {
    if (!src || !*src || !outN) return nullptr;
    if (cols == 0) cols = 1;
    while (*src == ' ') ++src;                    // no row starts on a space
    if (!*src) return nullptr;

    uint8_t used = p.used;
    size_t  take = 0;                             // bytes that certainly fit
    size_t  col  = 0;                             // columns they take
    size_t  lastSp = 0;                           // byte index of the last space
    bool    ruled = false;

    while (src[take]) {
        char ch = src[take];
        if (ch == '\n') {                         // an explicit break wins
            size_t n = take < outN - 1 ? take : outN - 1;
            memcpy(out, src, n);
            out[n] = '\0';
            return src + take + 1;
        }
        size_t  b = 1;
        size_t  w = (static_cast<uint8_t>(ch) & 0xC0) == 0x80 ? 0 : 1;
        bool    counts = false;
        bool    rule   = false;
        if (ch == '@') {
            Code c = parse(src + take);
            if (c.kind == Kind::At) {
                b = 2;
            } else if (c.kind == Kind::Unknown) {
                b = c.bytes;
                w = visible(src + take, b);       // printed as typed
            } else if (c.kind != Kind::None) {
                b = c.bytes;
                if (used < kPerMessage) {
                    counts = true;
                    rule   = c.kind == Kind::Rule;
                    w      = rule ? 0 : width(c);
                } else {
                    w = visible(src + take, b);   // over the limit: printed as typed
                }
            }
        }
        // A unit wider than a whole row goes on a row of its own rather than
        // never fitting anywhere; row() cuts its text to the width.
        bool fits = col + w <= cols || col == 0;
        if (!fits || take + b > outN - 1) break;
        if (ch == ' ') lastSp = take;
        take += b;
        col  += w;
        if (counts) ++used;
        if (rule) { ruled = true; break; }        // a rule ends its row
    }

    size_t brk;
    if (!src[take] || ruled)   brk = take;        // the rest fits, or a rule ended it
    else if (src[take] == ' ') brk = take;        // a word ends exactly on the margin
    else if (lastSp)           brk = lastSp;      // step back to the last space
    else                       brk = take;        // one word wider than the row

    size_t outLen = brk;
    while (outLen && src[outLen - 1] == ' ') --outLen;
    memcpy(out, src, outLen);
    out[outLen] = '\0';
    return src + brk;
}

// ---------------------------------------------------------------------------
// row
// ---------------------------------------------------------------------------
void row(Term& t, Timeline& tl, const char* text, uint8_t cols, Painter& p) {
    t.color(tl, p.cur);
    uint8_t col = 0;
    const char* s = text;
    while (*s) {
        const char* r = s;
        while (*r && *r != '@') ++r;
        if (r > s) {                              // plain text up to the next @
            t.textN(tl, s, static_cast<size_t>(r - s));
            col = static_cast<uint8_t>(col + visible(s, static_cast<size_t>(r - s)));
            s = r;
            continue;
        }
        Code c = parse(s);
        if (c.kind == Kind::None) { t.ch(tl, '@'); ++col; ++s; continue; }
        if (c.kind == Kind::At)   { t.ch(tl, '@'); ++col; s += 2; continue; }
        if (c.kind == Kind::Unknown || p.used >= kPerMessage) {   // as typed
            t.textN(tl, s, c.bytes);
            col = static_cast<uint8_t>(col + visible(s, c.bytes));
            s += c.bytes;
            continue;
        }
        ++p.used;
        act(t, tl, c, cols, col, p);
        s += c.bytes;
    }
}

// ---------------------------------------------------------------------------
// plain
// ---------------------------------------------------------------------------
void plain(const char* src, char* out, size_t outN) {
    if (!outN) return;
    size_t o = 0;
    const char* s = src ? src : "";
    while (*s && o + 1 < outN) {
        if (*s != '@') { out[o++] = *s++; continue; }
        Code c = parse(s);
        if (c.kind == Kind::None) { out[o++] = *s++; continue; }
        if (c.kind == Kind::At)   { out[o++] = '@'; s += 2; continue; }
        if (c.kind == Kind::Unknown) {
            for (uint8_t i = 0; i < c.bytes && o + 1 < outN; ++i) out[o++] = s[i];
            s += c.bytes;
            continue;
        }
        // An effect's words are part of what was said; @OOPS@'s were taken
        // back. Everything else is decoration and leaves nothing.
        if (takesText(c.kind) && c.kind != Kind::Oops)
            for (uint8_t i = 0; i < c.argLen && o + 1 < outN; ++i) out[o++] = c.arg[i];
        s += c.bytes;
    }
    out[o] = '\0';
}

} // namespace codes
