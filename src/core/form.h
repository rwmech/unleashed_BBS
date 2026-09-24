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
 *                  their box; a textarea field gets four 37-column rows
 *                  (two of 74 at 80 columns).
 *
 *                  Plain ASCII terminals cannot move the cursor, so they get
 *                  the same fields as one prompt per line, then Save (Y/n).
 *
 *                  The form edits the caller's buffers in place. It only reports Save or
 *                  Cancel; the owner validates and calls fail() to point at a field.
 *
 *                  Two layouts, chosen by the terminal's width (1.1.0,
 *                  internal/tty-ux-forms-80-2026-09-24.md). Under 80
 *                  columns, and when the width is unknown, the 40 x 24 card
 *                  that fits a C64: labels 9 wide, a 27 column box. At 80
 *                  and wider (132 included), labels 20 wide and a 56 column
 *                  box, so a long value shows whole rather than its tail.
 *                  The owner picks each field's label and note for the
 *                  width (Form::pick) before begin(); the form itself
 *                  stores nothing about it and asks Term::cols() each time.
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
    FF_TEXTAREA = 8,    // four 37-column rows under the label (two of 74 at 80)
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
    const char* label   = "";        // 9 characters at 40 columns, 20 at 80 (Form::pick)
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

    // wide: does this terminal get the 80 column layout? 80 and up; anything
    // narrower, a NAWS of 41 to 79 included, and an unknown width (0) get
    // the 40 column one, because a row that wraps is worse everywhere than
    // one that is short somewhere.
    static bool wide(const Term& t) { return t.cols() >= 80; }

    // pick: the words for this terminal. The long label (20 columns) or the
    // long note (78) at 80, when the table has one, and the short one
    // otherwise. Owners call it when they fill a FormField, so a field with
    // no long version reads its short one, padded, which is the right
    // failure: a C64 never gets a label cut to a stub.
    static const char* pick(const Term& t, const char* narrow, const char* wideText) {
        return (wideText && wide(t)) ? wideText : narrow;
    }

    // labelWidth / lineWidth: the label column and the width of a line the
    // form draws (the rule under the title), for a screen laid out beside
    // one (WHOIS), so the two agree at both widths.
    static uint8_t labelWidth(const Term& t) { return wide(t) ? 20 : 9; }
    static uint8_t lineWidth(const Term& t)  { return wide(t) ? 79 : 39; }

    // ask: a yes-or-no question before a save, on the status line (or its
    // own line in plain ASCII), with the focus on the field it is about
    // (1.1.0). For an owner whose Save found a value it will take but wants
    // said twice, such as a brightness past what a strip should draw. Y
    // returns Res::Save again, and the owner then finds takeConfirmed()
    // true and goes ahead. Anything else leaves the form open on that field,
    // nothing saved, and "Not saved." where the question was.
    void ask(uint8_t field, const char* question, Term& t, Timeline& tl);

    // takeConfirmed: was this Save the Y to ask()? True once, then false.
    bool takeConfirmed() {
        bool yes = ask_ == AskYes;
        if (yes) ask_ = AskNone;
        return yes;
    }

private:
    void changed(uint8_t i, Term& t, Timeline& tl) { if (onChange) onChange(*this, i, t, tl); }
    void listChoices(const FormField& f, Term& t, Timeline& tl);

    // The geometry, as functions of the width rather than constants (1.1.0).
    // The 40 column values are the ones this form has always used, so a C64
    // draws exactly what it did.
    static constexpr uint8_t kLabelCol = 2;
    static uint8_t boxCol(const Term& t)    { return wide(t) ? 23 : 12; }
    static uint8_t boxW(const Term& t)      { return wide(t) ? 56 : 27; }
    // A textarea keeps its size, 148 characters, which is BBS_PROFILE_MAX:
    // four rows of 37 on a C64, two of 74 at 80.
    static uint8_t areaW(const Term& t)     { return wide(t) ? 74 : 37; }
    static uint8_t areaRows(const Term& t)  { return wide(t) ? 2 : 4; }
    static uint8_t statusW(const Term& t)   { return wide(t) ? 78 : 38; }
    // Line mode: how much of a current value "[...]" shows before "...".
    static uint8_t shownCut(const Term& t)  { return wide(t) ? 60 : 20; }

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
    // What a key answers, besides a field. One byte for the line-mode
    // "Save (Y/n)?" and the owner's ask() together, where a bool was the
    // first alone: a Form lives in every Session, and a byte more would have
    // been four with the padding, twelve times over.
    enum : uint8_t { AskNone, AskSave, AskOwner, AskYes };
    uint8_t     ask_        = AskNone;
};
