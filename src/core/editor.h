/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/editor.h
 * Module:       Core / line editor
 *
 * Purpose:      Single-line input editor with echo, destructive backspace
 *                  per terminal type, optional password masking (capped at
 *                  BBS_MASK_SHOW stars), stay-on-line Enter, uppercase
 *                  and digits-only filters, plus a small command history for
 *                  up/down-arrow recall.
 *
 * Interfaces:   LineEditor (begin, key, redraw, replace, shown), LineHistory
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
#include "../config.h"
#include "term.h"

class LineEditor {
public:
    enum Flags : uint8_t {
        F_NONE    = 0,
        F_MASK    = 1,   // echo '*' (passwords)
        F_UPPER   = 2,   // force uppercase
        F_DIGITS  = 4,   // digits only
        F_BYEMASK = 8,   // echo '*' for everything after "BYE "
        F_STAY    = 16,  // Enter does not move to a new line (caller rubs out or moves on)
    };
    enum class Res : uint8_t { Editing, Done, Abort };

    void begin(uint8_t maxLen, uint8_t flags = F_NONE);
    Res  key(int k, Term& t, ByteSink& o);

    // redraw: echo the current buffer again (after a notice interrupted it)
    void redraw(Term& t, ByteSink& o) const;

    // replace: swap the visible line for new text (history recall)
    void replace(const char* s, Term& t, ByteSink& o);

    bool        active() const { return active_; }
    const char* text()   const { return buf_; }
    uint8_t     len()    const { return len_; }

    // shown: characters on screen for this line (a masked line shows at
    // most BBS_MASK_SHOW stars so it never wraps)
    uint8_t     shown()  const;

private:
    char echoFor(uint8_t pos) const;

    char    buf_[BBS_LINE_MAX + 1] = {};
    uint8_t len_    = 0;
    uint8_t max_    = 0;
    uint8_t flags_  = 0;
    bool    active_ = false;
};

// ---------------------------------------------------------------------------
// LineHistory: last BBS_HISTORY command lines, newest first on recall
// ---------------------------------------------------------------------------
class LineHistory {
public:
    // add: keep a line (skips blanks, repeats, and anything starting "BYE ")
    void add(const char* line);

    // get: back = 0 is the newest line, nullptr past the end
    const char* get(uint8_t back) const;

    uint8_t count() const { return count_; }

private:
    char    lines_[BBS_HISTORY][BBS_LINE_MAX + 1] = {};
    uint8_t head_  = 0;    // next write slot
    uint8_t count_ = 0;
};
