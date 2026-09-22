/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/screens.h
 * Module:       Core / screen player
 *
 * Purpose:      Streams display files from the filesystem into a session
 *                  Timeline in small chunks (never loads a whole file).
 *
 *                  Lookup by terminal, first match wins:
 *                  PETSCII-40: name.p40, name.seq, name.asc
 *                  PETSCII-80: name.p80, name.seq, name.asc
 *                  ANSI:       name.ans, name.asc
 *                  ASCII:      name.asc
 *
 *                  .seq/.p40/.p80 are raw PETSCII, .ans is raw ANSI in CP437 (re-encoded
 *                  for UTF-8 callers, SAUCE records skipped), .asc is plain ASCII and is
 *                  translated for every terminal.
 *
 *                  Paging: .asc files pause every N lines when setPaging(N) is on; the
 *                  caller shows a More prompt and calls resume(). Art formats (.ans,
 *                  .seq) are drawn with cursor movement and are never paged.
 *
 *                  @-codes, case-insensitive, work in all formats:
 *                  @BBS@ @BOARD@ @VER@ @NODE@ @NODES@ @USER@ @TERM@ @COLS@
 *                  @DATE@ @TIME@ @CLS@ @BELL@ @DELAY:ms@ @SPIN:ms@ @BAUD:n@
 *
 * Libraries:    none (libc stdio)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     SCREENS.md
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
#include "../config.h"
#include "term.h"
#include "timeline.h"

// ---------------------------------------------------------------------------
// sdScreensDir: the directory on the SD card that overrides the stock
// screens, or null when there is no card or the override is switched off.
//
// Defined by the sd plugin and declared here so the screen player can look
// on the card first without the core knowing that plugins exist. The core
// reaching into a plugin is the wrong direction for a dependency, so it is
// one function returning a path: the core asks "is there another place to
// look" and does not care who answers.
// ---------------------------------------------------------------------------
const char* sdScreensDir();

class ScreenPlayer {
public:
    struct Vars {
        const char* user;
        uint8_t     node;
        uint8_t     nodes;
    };

    // open: find the best file for this terminal. False if none exists.
    bool open(const char* name, const Term& t);
    bool active() const { return f_ != nullptr; }
    // onCard: this screen is being read from the SD card. The card can be
    // unmounted under a caller who is paused at a page break, and a stale
    // descriptor into a torn-down filesystem is worse than a screen that
    // stops early, so the unmount path closes these first.
    bool onCard() const { return f_ != nullptr && fromCard_; }
    void close();

    // detach: forget the file without closing it (session moved elsewhere)
    void detach() { f_ = nullptr; paused_ = false; pageBreak_ = false; }

    // pump: stream as much as fits. Returns true while more remains.
    bool pump(Term& t, Timeline& tl, const Vars& v);

    // paging for .asc files: pause after rows lines (0 = off)
    void setPaging(uint8_t rows) { pageRows_ = rows; }
    bool paused() const { return paused_; }
    void resume()       { paused_ = false; pageBreak_ = false; lines_ = 0; }

    // pageBreak: the screen asked to stop here with a form feed (0x0C),
    // rather than simply having filled the screen. The difference matters:
    // a deliberate break is a page in a document and gets a clear screen
    // and a "press a key", where a full screen gets [More].
    bool pageBreak() const { return pageBreak_; }

    // fullSpeed: drop any @BAUD@ pacing for the rest of this screen, and
    // ignore any later @BAUD@ in it. Called when a caller presses a key
    // (skipDelays alone only hurries the frames already queued, and the
    // player would pace everything after them), and by showScreen, which
    // plays a screen in one go with nothing draining the line in between,
    // so a paced screen there stopped at the frame guard and lost the rest.
    void fullSpeed() { typeMs_ = 0; pace_ = false; }

private:
    enum class Mode : uint8_t { Text, Pet, Ansi };

    void emitByte(Term& t, Timeline& tl, uint8_t b);
    void flushToken(Term& t, Timeline& tl);
    void runToken(Term& t, Timeline& tl, const Vars& v);
    void byteIn(Term& t, Timeline& tl, const Vars& v, uint8_t b);
    void say(Term& t, Timeline& tl, const char* s);

    FILE*   f_       = nullptr;
    bool    fromCard_ = false;
    Mode    mode_    = Mode::Text;
    bool    inTok_   = false;
    uint8_t tokLen_  = 0;
    char    tok_[16] = {};    // normalized uppercase ASCII
    uint8_t raw_[16] = {};    // original bytes, for literal fallback
    uint8_t prev_    = 0;
    bool    sauce_   = false; // hit ^Z in an .ans file

    uint8_t buf_[BBS_SCREEN_CHUNK] = {};
    uint8_t bufLen_   = 0;
    uint8_t bufPos_   = 0;
    uint8_t pageRows_ = 0;
    uint8_t lines_    = 0;
    bool    paused_   = false;
    bool    pageBreak_ = false;
    // @BAUD:n@: milliseconds held after each byte, 0 = full speed. Paced
    // here, one frame per byte, and not with Timeline::setCps, because that
    // is live rather than queued (it would slow output already waiting) and
    // it is the caller's own BAUD setting, which a screen must not leave
    // changed behind it. Cleared by open() and close(), so a screen that
    // never says @BAUD:0@ still ends at full speed.
    uint8_t typeMs_   = 0;
    bool    pace_     = true;     // false after fullSpeed(), until the next open()
};
