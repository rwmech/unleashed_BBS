/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/detect.cpp
 * Module:       Core / terminal detection
 *
 * Purpose:      Terminal detection state machine (see detect.h).
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

#include "detect.h"
#include "../config.h"

namespace {
// Probe: text, CPR request, UTF-8 box-drawing char (U+2500), CPR request
const uint8_t kProbe[] = {
    '\r', '\n', '\r', '\n',
    'D','E','T','E','C','T','I','N','G',' ','T','E','R','M','I','N','A','L',
    0x1B, '[', '6', 'n',
    0xE2, 0x94, 0x80,
    0x1B, '[', '6', 'n',
};
} // namespace

// ---------------------------------------------------------------------------
// start: reset, then settle before probing
// ---------------------------------------------------------------------------
void Detector::start(uint32_t now) {
    st_       = St::Settle;
    cpr_      = 0;
    idx_      = 0;
    replies_  = 0;
    dots_     = 0;
    tries_    = 0;
    type_     = TermType::Unknown;
    deadline_ = now + BBS_SETTLE_MS;
}

// ---------------------------------------------------------------------------
// probeNow: send the probe and arm the reply timers
// ---------------------------------------------------------------------------
void Detector::probeNow(uint32_t now, ByteSink& out) {
    if (st_ != St::Settle) return;
    st_       = St::Probe;
    out.put(kProbe, sizeof(kProbe));
    deadline_ = now + BBS_DETECT_TIMEOUT_MS;
    nextDot_  = now + BBS_DETECT_DOT_MS;
}

// ---------------------------------------------------------------------------
// cprByte: parse ESC [ row ; col R. Returns true if b was consumed.
// ---------------------------------------------------------------------------
bool Detector::cprByte(uint8_t b, bool& complete) {
    complete = false;
    switch (cpr_) {
        case 0:
            if (b == 0x1B) { cpr_ = 1; return true; }
            return false;
        case 1:
            if (b == '[') { cpr_ = 2; idx_ = 0; num_[0] = num_[1] = 0; return true; }
            cpr_ = 0;
            return false;
        case 2:
            if (b >= '0' && b <= '9') {
                if (num_[idx_] < 1000) num_[idx_] = static_cast<uint16_t>(num_[idx_] * 10 + (b - '0'));
                return true;
            }
            if (b == ';') { if (idx_ < 1) ++idx_; return true; }
            cpr_ = 0;
            if (b == 'R') {
                if (replies_ < 2) col_[replies_] = num_[1];
                ++replies_;
                complete = true;
                return true;
            }
            return true;   // some other CSI reply: swallow it
        default:
            cpr_ = 0;
            return false;
    }
}

// ---------------------------------------------------------------------------
// finish: lock in a terminal type
// ---------------------------------------------------------------------------
Detector::Result Detector::finish(TermType t, Charset cs, uint8_t cols, uint8_t rows, ByteSink& out) {
    (void)out;
    type_ = t;
    cs_   = cs;
    cols_ = cols;
    rows_ = rows;
    st_   = St::Done;
    return Result::Done;
}

// ---------------------------------------------------------------------------
// finishAnsi: UTF-8 if the test glyph advanced the cursor one column
// ---------------------------------------------------------------------------
Detector::Result Detector::finishAnsi(ByteSink& out) {
    Charset cs = Charset::Cp437;
    if (replies_ >= 2 && col_[1] == col_[0] + 1) cs = Charset::Utf8;
    out.puts("\r\x1b[K");   // wipe the probe line
    return finish(TermType::Ansi, cs, 80, 24, out);
}

// ---------------------------------------------------------------------------
// askKey: the classic "press a key" prompt
// ---------------------------------------------------------------------------
void Detector::askKey(uint32_t now, ByteSink& out, bool clear) {
    if (clear) out.putc(0x93);   // PETSCII CLR/HOME, wipes probe residue
    out.puts("\r\nHIT DEL OR BACKSPACE: ");
    st_       = St::AskKey;
    deadline_ = now + BBS_KEY_PROMPT_MS;
}

void Detector::askCols(uint32_t now, ByteSink& out) {
    out.puts("\r40 OR 80 COLUMNS (4/8)? ");
    st_       = St::AskCols;
    deadline_ = now + BBS_KEY_PROMPT_MS;
}

// ---------------------------------------------------------------------------
// feed: one inbound data byte
// ---------------------------------------------------------------------------
Detector::Result Detector::feed(uint8_t b, uint32_t now, ByteSink& out) {
    bool complete = false;

    switch (st_) {
        case St::Settle:
            return Result::Pending;   // keys before the probe are ignored

        case St::Probe:
            if (cprByte(b, complete) && complete) {
                if (replies_ >= 2) return finishAnsi(out);
                deadline_ = now + BBS_DETECT_CPR2_MS;
            }
            return Result::Pending;   // early keypresses are ignored

        case St::AskKey:
            if (cprByte(b, complete)) {
                if (complete) return finishAnsi(out);   // late ANSI reply
                return Result::Pending;
            }
            if (b == 0x14) { askCols(now, out); return Result::Pending; }
            if (b == 0x08 || b == 0x7F) {
                out.puts("\r\n");
                return finish(TermType::Ascii, Charset::Ascii, 80, 24, out);
            }
            if (b == 0x0A || b == 0x00) return Result::Pending;
            if (++tries_ >= BBS_KEY_PROMPT_TRIES) {
                out.puts("\r\n");
                return finish(TermType::Ascii, Charset::Ascii, 80, 24, out);
            }
            askKey(now, out, false);
            return Result::Pending;

        case St::AskCols:
            if (b == '4' || b == 0x0D) {
                out.putc(0x0D);
                return finish(TermType::Pet40, Charset::Petscii, 40, 25, out);
            }
            if (b == '8') {
                out.putc(0x0D);
                return finish(TermType::Pet80, Charset::Petscii, 80, 25, out);
            }
            askCols(now, out);
            return Result::Pending;

        case St::Done:
        default:
            return Result::Done;
    }
}

// ---------------------------------------------------------------------------
// tick: dots and timeouts
// ---------------------------------------------------------------------------
Detector::Result Detector::tick(uint32_t now, ByteSink& out) {
    switch (st_) {
        case St::Settle:
            if (static_cast<int32_t>(now - deadline_) >= 0) probeNow(now, out);
            return Result::Pending;

        case St::Probe:
            if (replies_ == 0 && dots_ < 8 && static_cast<int32_t>(now - nextDot_) >= 0) {
                out.putc('.');
                ++dots_;
                nextDot_ += BBS_DETECT_DOT_MS;
            }
            if (static_cast<int32_t>(now - deadline_) >= 0) {
                if (replies_ > 0) return finishAnsi(out);
                askKey(now, out, true);
            }
            return Result::Pending;

        case St::AskKey:
        case St::AskCols:
            if (static_cast<int32_t>(now - deadline_) >= 0) return Result::Timeout;
            return Result::Pending;

        case St::Done:
        default:
            return Result::Done;
    }
}
