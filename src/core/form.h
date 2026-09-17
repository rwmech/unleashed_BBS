/*
 * File:        src/core/form.h
 * Description: Fill-in forms for ANSI and PETSCII terminals (curses style):
 *              labels on the left, input boxes on the right, [ Save ] and
 *              [ Cancel ] underneath, a status line for hints and errors.
 *              Cursor keys (C64: CRSR) or Enter move between fields, the
 *              focused box is in reverse video, F1 saves from anywhere,
 *              ESC (C64: left-arrow) cancels. Long values scroll inside
 *              their box; a textarea field gets four 37-column rows.
 *
 *              Plain ASCII terminals cannot move the cursor, so they get
 *              the same fields as one prompt per line, then Save (Y/n).
 *
 *   The form edits the caller's buffers in place. It only reports Save or
 *   Cancel; the owner validates and calls fail() to point at a field.
 *   Laid out for 40 x 24: fits a C64 and every PC terminal.
 * Listing:     COMPLETE FILE
 * Libraries:   none
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
};

struct FormField {
    const char* label   = "";        // 9 characters max
    char*       buf     = nullptr;   // value, edited in place
    uint8_t     cap     = 0;         // max characters, excluding the terminator
    uint8_t     flags   = FF_NONE;
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
