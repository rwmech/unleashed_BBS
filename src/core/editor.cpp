/*
 * File:        src/core/editor.cpp
 * Description: Line editor and command history (see editor.h).
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc)
 */
#include "editor.h"
#include <cstring>
#include <cctype>

namespace {

// ---------------------------------------------------------------------------
// byeArgStart: index where the argument of "BYE <arg>" begins, or -1.
// Leading spaces are allowed; the verb is case-insensitive.
// ---------------------------------------------------------------------------
int byeArgStart(const char* s, uint8_t len) {
    uint8_t i = 0;
    while (i < len && s[i] == ' ') ++i;
    if (len < i + 4) return -1;
    if (toupper(static_cast<unsigned char>(s[i]))     != 'B' ||
        toupper(static_cast<unsigned char>(s[i + 1])) != 'Y' ||
        toupper(static_cast<unsigned char>(s[i + 2])) != 'E' ||
        s[i + 3] != ' ') return -1;
    return i + 4;
}

} // namespace

// ---------------------------------------------------------------------------
// begin: arm the editor for a new line
// ---------------------------------------------------------------------------
void LineEditor::begin(uint8_t maxLen, uint8_t flags) {
    max_    = maxLen > BBS_LINE_MAX ? BBS_LINE_MAX : maxLen;
    flags_  = flags;
    len_    = 0;
    buf_[0] = '\0';
    active_ = true;
}

// ---------------------------------------------------------------------------
// echoFor: what the caller sees for the character at pos
// ---------------------------------------------------------------------------
char LineEditor::echoFor(uint8_t pos) const {
    if (flags_ & F_MASK) return '*';
    if (flags_ & F_BYEMASK) {
        int a = byeArgStart(buf_, len_);
        if (a >= 0 && pos >= a) return '*';
    }
    return buf_[pos];
}

// ---------------------------------------------------------------------------
// key: apply one key event, echoing through the terminal driver
// ---------------------------------------------------------------------------
LineEditor::Res LineEditor::key(int k, Term& t, ByteSink& o) {
    if (!active_) return Res::Done;

    if (k == KEY_ENTER) {
        buf_[len_] = '\0';
        active_ = false;
        if (!(flags_ & F_STAY)) t.nl(o);
        return Res::Done;
    }
    if (k == KEY_BACKSPACE) {
        if (len_) {
            bool visible = !(flags_ & F_MASK) || len_ <= BBS_MASK_SHOW;
            --len_;
            buf_[len_] = '\0';
            if (visible) t.eraseBack(o, 1);
        }
        return Res::Editing;
    }
    if (k == KEY_ESC || k == KEY_BREAK) {
        t.eraseBack(o, shown());
        len_    = 0;
        buf_[0] = '\0';
        active_ = false;
        return Res::Abort;
    }
    if (k < 0x20 || k > 0x7E) return Res::Editing;   // arrows, F-keys
    if (len_ >= max_) return Res::Editing;

    char c = static_cast<char>(k);
    if ((flags_ & F_DIGITS) && (c < '0' || c > '9')) return Res::Editing;
    if ((flags_ & F_UPPER) && c >= 'a' && c <= 'z') c = static_cast<char>(c - 0x20);

    buf_[len_++] = c;
    buf_[len_]   = '\0';
    if (len_ <= shown()) t.ch(o, echoFor(static_cast<uint8_t>(len_ - 1)));
    return Res::Editing;
}

uint8_t LineEditor::shown() const {
    return ((flags_ & F_MASK) && len_ > BBS_MASK_SHOW) ? BBS_MASK_SHOW : len_;
}

// ---------------------------------------------------------------------------
// redraw: re-echo the whole buffer with masking applied
// ---------------------------------------------------------------------------
void LineEditor::redraw(Term& t, ByteSink& o) const {
    for (uint8_t i = 0; i < shown(); ++i) t.ch(o, echoFor(i));
}

// ---------------------------------------------------------------------------
// replace: erase what is on screen, load s, echo it
// ---------------------------------------------------------------------------
void LineEditor::replace(const char* s, Term& t, ByteSink& o) {
    if (!active_) return;
    t.eraseBack(o, shown());
    size_t n = s ? strlen(s) : 0;
    if (n > max_) n = max_;
    if (n) memcpy(buf_, s, n);
    len_       = static_cast<uint8_t>(n);
    buf_[len_] = '\0';
    redraw(t, o);
}

// ===========================================================================
// LineHistory
// ===========================================================================

void LineHistory::add(const char* line) {
    while (*line == ' ') ++line;
    if (!*line) return;
    if (byeArgStart(line, static_cast<uint8_t>(strnlen(line, BBS_LINE_MAX))) >= 0) return;   // may hold a password
    const char* last = get(0);
    if (last && !strcmp(last, line)) return;

    strncpy(lines_[head_], line, BBS_LINE_MAX);
    lines_[head_][BBS_LINE_MAX] = '\0';
    head_ = static_cast<uint8_t>((head_ + 1) % BBS_HISTORY);
    if (count_ < BBS_HISTORY) ++count_;
}

const char* LineHistory::get(uint8_t back) const {
    if (back >= count_) return nullptr;
    return lines_[(head_ + BBS_HISTORY - 1 - back) % BBS_HISTORY];
}
