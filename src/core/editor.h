/*
 * File:        src/core/editor.h
 * Description: Single-line input editor with echo, destructive backspace
 *              per terminal type, optional password masking, uppercase
 *              and digits-only filters, plus a small command history for
 *              up/down-arrow recall.
 * Listing:     COMPLETE FILE
 * Libraries:   none
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
