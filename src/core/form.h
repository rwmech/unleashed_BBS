/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/form.h
 * Module:       Core / fill-in forms
 *
 * Purpose:      Fill-in forms for ANSI and PETSCII terminals (curses style):
 *                  labels on the left, input boxes on the right, [ Save ] and
 *                  [ Cancel ] underneath, a status line for hints and errors.
 *                  Cursor keys (C64: CRSR) or Enter move between fields, the
 *                  focused box is in reverse video, F1 saves from anywhere,
 *                  ESC (C64: left-arrow) cancels. Long values scroll inside
 *                  their box; a textarea field gets four 37-column rows.
 *
 *                  Plain ASCII terminals cannot move the cursor, so they get
 *                  the same fields as one prompt per line, then Save (Y/n).
 *
 *                  The form edits the caller's buffers in place. It only reports Save or
 *                  Cancel; the owner validates and calls fail() to point at a field.
 *                  Laid out for 40 x 24: fits a C64 and every PC terminal.
 *
 * Interfaces:   Form (begin, key, fail, status, after), FormField, FormFlag
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     USERS.md
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
#include "timeline.h"

enum FormFlag : uint8_t {
    FF_NONE     = 0,
    FF_MASK     = 1,    // echo '*' (passwords)
    FF_REQUIRED = 2,    // label marked, owner still validates
    FF_READONLY = 4,    // shown, never focused
    FF_TEXTAREA = 8,    // four 37-column rows under the label
    FF_YESNO    = 16,   // one letter, Y or N, space toggles
    FF_CYCLE    = 32,   // one of choices, space steps to the next
};

struct FormField {
    const char* label   = "";        // 9 characters max
    char*       buf     = nullptr;   // value, edited in place
    uint8_t     cap     = 0;         // max characters, excluding the terminator
    uint8_t     flags   = FF_NONE;
    const char* choices = nullptr;   // FF_CYCLE: "User|Co2|Co1|Sysop"
};

class Form {
public:
    enum class Res : uint8_t { Editing, Save, Cancel };
    static constexpr uint8_t kMaxFields = 10;

    // begin: draw the form (with a little flourish) and focus the first field
    void begin(const char* title, FormField* fields, uint8_t count, Term& t, Timeline& tl);

    // key: one key event
    Res key(int k, Term& t, Timeline& tl);

    // fail: flash an error, focus the field that caused it
    void fail(uint8_t field, const char* msg, Term& t, Timeline& tl);

    // status: one line under the buttons (hint, progress, result)
    void status(const char* msg, Color c, Term& t, Timeline& tl);

    // after: cursor below the form, ready for normal output
    void after(Term& t, Timeline& tl);

    // wipe: forget the line-mode input buffer (it can hold a password)
    void wipe();

private:
    static constexpr uint8_t kLabelCol = 2;
    static constexpr uint8_t kBoxCol   = 12;
    static constexpr uint8_t kBoxW     = 27;
    static constexpr uint8_t kAreaW    = 37;
    static constexpr uint8_t kAreaRows = 4;

    bool positional(const Term& t) const { return t.isAnsi() || t.isPet(); }

    // positional
    void drawField(uint8_t i, Term& t, Timeline& tl);
    void drawButtons(Term& t, Timeline& tl);
    void placeCursor(Term& t, Timeline& tl);
    void setFocus(uint8_t next, Term& t, Timeline& tl);
    uint8_t nextFocus(int dir) const;
    static bool cycle(FormField& f, int k);
    Res  keyPositional(int k, Term& t, Timeline& tl);

    // line mode
    void linePrompt(Term& t, Timeline& tl);
    Res  keyLine(int k, Term& t, Timeline& tl);

    FormField*  f_          = nullptr;
    uint8_t     n_          = 0;
    uint8_t     focus_      = 0;      // 0..n-1 fields, n = Save, n+1 = Cancel
    uint8_t     row_[kMaxFields] = {};
    uint8_t     buttonRow_  = 0;
    uint8_t     statusRow_  = 0;

    char        in_[BBS_PROFILE_MAX + 1] = {};   // line mode input
    uint8_t     inLen_      = 0;
    bool        confirming_ = false;
};
