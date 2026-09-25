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
    // A button rather than a value: focusable, never typed into, and Enter
    // (or space) on it ends the key with Res::Open and the field's index.
    // buf is the summary the button shows, not a value the form edits, so
    // the owner keeps the real thing somewhere of its own. This is what
    // lets one row of a form stand for a page of its own.
    FF_ACTION   = 64,
    // The first key replaces the whole value rather than adding to it. For a
    // password that is already set, which the owner shows as its mask: the
    // stars sit in the buffer, and typing after them used to save
    // "********newpass" (found by the screen artist, 0.23.0). Backspace
    // clears the lot in one press, which is what deleting a mask means.
    FF_REPLACE  = 128,
};

struct FormField {
    const char* label   = "";        // 9 characters max
    char*       buf     = nullptr;   // value, edited in place
    uint8_t     cap     = 0;         // max characters, excluding the terminator
    uint8_t     flags   = FF_NONE;
    const char* choices = nullptr;   // FF_CYCLE: "User|Co2|Co1|Sysop"

    // note: shown on the status line while this field has the focus.
    //
    // For telling somebody who can read what they are about to type, at the
    // moment they type it. The posture was already right, email, address and
    // phone are UF_PRIVATE and INFO shows them only to the owner and to
    // staff holding USERS, but a caller entering a phone number was told
    // none of that, and a policy screen somewhere else is not an answer.
    // Appended, so every existing field initialiser is untouched.
    const char* note    = nullptr;
};

class Form {
public:
    // Open: an FF_ACTION field was pressed. opened() says which one, and the
    // owner takes the session somewhere else and comes back with begin().
    enum class Res : uint8_t { Editing, Save, Cancel, Open };
    // 16, not 10, because a plugin's own settings sit on the CONFIG form
    // underneath the four core keys, and announce alone declares nine. A
    // form this tall still fits a 25 row C64 screen with its title bar,
    // buttons and the prompt underneath.
    static constexpr uint8_t kMaxFields = 16;

    // begin: draw the form (with a little flourish) and focus a field. focus
    // defaults to the first one; an owner returning from a sub-page passes
    // the button that opened it, so the caller lands where they left.
    void begin(const char* title, FormField* fields, uint8_t count, Term& t, Timeline& tl,
               uint8_t focus = 0);

    // key: one key event
    Res key(int k, Term& t, Timeline& tl);

    // opened: which field returned Res::Open
    uint8_t opened() const { return opened_; }

    // fail: flash an error, focus the field that caused it
    void fail(uint8_t field, const char* msg, Term& t, Timeline& tl);

    // status: one line under the buttons (hint, progress, result)
    void status(const char* msg, Color c, Term& t, Timeline& tl);

    // statusForFocus: the focused field's note, or the movement hint
    void statusForFocus(Term& t, Timeline& tl);


    // after: cursor below the form, ready for normal output
    void after(Term& t, Timeline& tl);

    // wipe: forget the line-mode input buffer (it can hold a password)
    void wipe();

    // redraw: draw field i again from its buffer, for an owner that has just
    // changed it because the caller changed another one (CONFIG's Timezone
    // writes its TZ string, 1.1.0). The cursor goes back where it was. Plain
    // ASCII has nothing drawn to redraw: it shows the value when it asks.
    void redraw(uint8_t i, Term& t, Timeline& tl);

    // onChange: called whenever a field's value changes, with the field, on
    // every form, and before the next field is asked for in line mode, so an
    // owner that ties two fields together can change the second while the
    // caller is still looking at the first. One pointer for the whole board
    // rather than one per form, because a form lives in every Session and a
    // byte there costs twelve: the one owner that sets it (CONFIG, one sysop
    // at a time) checks that the form is its own.
    using ChangeFn = void (*)(Form& f, uint8_t field, Term& t, Timeline& tl);
    static ChangeFn onChange;

private:
    void changed(uint8_t i, Term& t, Timeline& tl) { if (onChange) onChange(*this, i, t, tl); }
    void listChoices(const FormField& f, Term& t, Timeline& tl);
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
    uint8_t     opened_     = 0;      // field that returned Res::Open
    uint8_t     row_[kMaxFields] = {};
    uint8_t     buttonRow_  = 0;
    uint8_t     statusRow_  = 0;

    char        in_[BBS_PROFILE_MAX + 1] = {};   // line mode input
    uint8_t     inLen_      = 0;
    bool        confirming_ = false;
};
