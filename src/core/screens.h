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
 *                  @BBS@ @VER@ @NODE@ @NODES@ @USER@ @TERM@ @COLS@ @DATE@ @TIME@
 *                  @CLS@ @BELL@ @DELAY:ms@ @SPIN:ms@
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
    void close();

    // detach: forget the file without closing it (session moved elsewhere)
    void detach() { f_ = nullptr; paused_ = false; }

    // pump: stream as much as fits. Returns true while more remains.
    bool pump(Term& t, Timeline& tl, const Vars& v);

    // paging for .asc files: pause after rows lines (0 = off)
    void setPaging(uint8_t rows) { pageRows_ = rows; }
    bool paused() const { return paused_; }
    void resume()       { paused_ = false; lines_ = 0; }

private:
    enum class Mode : uint8_t { Text, Pet, Ansi };

    void emitByte(Term& t, Timeline& tl, uint8_t b);
    void flushToken(Term& t, Timeline& tl);
    void runToken(Term& t, Timeline& tl, const Vars& v);
    void byteIn(Term& t, Timeline& tl, const Vars& v, uint8_t b);

    FILE*   f_       = nullptr;
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
};
