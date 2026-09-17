/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/detect.h
 * Module:       Core / terminal detection
 *
 * Purpose:      Connect-time terminal detection on the single dial-in port.
 *
 *                  0. Settle: wait up to BBS_SETTLE_MS. A telnet client (PuTTY, SyncTERM)
 *                  speaks IAC first; the session negotiates character mode and calls
 *                  probeNow() at once. Probing before that lets a line-mode client
 *                  hold its cursor reports until Enter, which reads as a slow detect
 *                  and leaks "[3;20R" into the handle prompt.
 *                  1. "DETECTING TERMINAL" + ANSI cursor-position request (ESC[6n), a
 *                  UTF-8 test glyph, and a second request. ANSI terminals answer
 *                  automatically; the column delta tells UTF-8 (1) from CP437 (3).
 *                  Dots tick while waiting.
 *                  2. No answer: PETSCII clear-screen (wipes probe residue on a C64),
 *                  then "HIT DEL OR BACKSPACE". INST/DEL ($14) = PETSCII,
 *                  BS ($08) / DEL ($7F) = ASCII. A late cursor report = ANSI.
 *                  3. PETSCII: "40 OR 80 COLUMNS (4/8)?" for C64 vs C128.
 *
 *                  All prompt text is uppercase ASCII, which reads correctly on both a
 *                  C64 and a PC terminal before the terminal type is known.
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
#include "term.h"

class Detector {
public:
    enum class Result : uint8_t { Pending, Done, Timeout };

    // start: arm the settle timer (nothing is sent yet)
    void   start(uint32_t now);

    // probeNow: send the probe immediately (telnet client spoke first)
    void   probeNow(uint32_t now, ByteSink& out);

    Result feed(uint8_t b, uint32_t now, ByteSink& out);
    Result tick(uint32_t now, ByteSink& out);

    TermType type()    const { return type_; }
    Charset  charset() const { return cs_; }
    uint8_t  cols()    const { return cols_; }
    uint8_t  rows()    const { return rows_; }

private:
    enum class St : uint8_t { Settle, Probe, AskKey, AskCols, Done };

    bool   cprByte(uint8_t b, bool& complete);
    Result finishAnsi(ByteSink& out);
    Result finish(TermType t, Charset cs, uint8_t cols, uint8_t rows, ByteSink& out);
    void   askKey(uint32_t now, ByteSink& out, bool clear);
    void   askCols(uint32_t now, ByteSink& out);

    St       st_       = St::Settle;
    uint8_t  cpr_      = 0;       // 0 idle, 1 ESC, 2 CSI body
    uint16_t num_[2]   = {0, 0};
    uint8_t  idx_      = 0;
    uint8_t  replies_  = 0;
    uint16_t col_[2]   = {0, 0};
    uint32_t deadline_ = 0;
    uint32_t nextDot_  = 0;
    uint8_t  dots_     = 0;
    uint8_t  tries_    = 0;

    TermType type_ = TermType::Unknown;
    Charset  cs_   = Charset::Ascii;
    uint8_t  cols_ = 80;
    uint8_t  rows_ = 24;
};
