/*
 * File:        src/core/form.cpp
 * Description: Fill-in forms (see form.h).
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc)
 */
#include "form.h"
#include "fx.h"
#include <cstring>
#include <cstdio>

namespace {

constexpr uint8_t kSaveW   = 8;    // "[ Save ]"
constexpr uint8_t kCancelW = 10;   // "[ Cancel ]"

bool editable(const FormField& f) { return !(f.flags & FF_READONLY); }

} // namespace

// ===========================================================================
// Setup
// ===========================================================================

// ---------------------------------------------------------------------------
// begin: layout rows, draw title, fields and buttons, focus the first field
// ---------------------------------------------------------------------------
void Form::begin(const char* title, FormField* fields, uint8_t count, Term& t, Timeline& tl) {
    f_          = fields;
    n_          = count > kMaxFields ? kMaxFields : count;
    confirming_ = false;
    inLen_      = 0;
    in_[0]      = '\0';

    uint8_t row = 4;
    for (uint8_t i = 0; i < n_; ++i) {
        row_[i] = row;
        row = static_cast<uint8_t>(row + ((f_[i].flags & FF_TEXTAREA) ? 1 + kAreaRows : 1));
    }
    buttonRow_ = static_cast<uint8_t>(row + 1);
    statusRow_ = static_cast<uint8_t>(row + 3);

    focus_ = 0;
    while (focus_ < n_ && !editable(f_[focus_])) ++focus_;

    t.reset(tl);
    t.cls(tl);
    t.color(tl, Color::Yellow);
    fx::scramble(t, tl, title, 8, 45);
    t.nl(tl);
    t.color(tl, Color::Cyan);
    fx::rule(t, tl, 39);
    t.nl(tl);

    if (!positional(t)) {                        // plain ASCII: one prompt per line
        linePrompt(t, tl);
        return;
    }

    t.cursor(tl, false);
    for (uint8_t i = 0; i < n_; ++i) {
        drawField(i, t, tl);
        tl.delay(35);                            // fields cascade in
    }
    drawButtons(t, tl);
    status(t.isPet() ? "CRSR moves, F1 saves, <- cancels" : "Up/Down move, F1 saves, ESC cancels",
           Color::DarkGrey, t, tl);
    t.cursor(tl, true);
    placeCursor(t, tl);
}

// ===========================================================================
// Positional drawing
// ===========================================================================

void Form::drawField(uint8_t i, Term& t, Timeline& tl) {
    const FormField& f = f_[i];
    bool focused = focus_ == i;
    bool area    = f.flags & FF_TEXTAREA;
    bool mask    = f.flags & FF_MASK;

    char label[16];
    snprintf(label, sizeof(label), "%-9.9s", f.label);
    t.reverse(tl, false);
    t.gotoXY(tl, kLabelCol, row_[i]);
    t.color(tl, focused ? Color::Yellow : (editable(f) ? Color::LightBlue : Color::DarkGrey));
    t.text(tl, label);

    size_t len = strlen(f.buf);
    if (!area) {
        const char* shown = f.buf;
        size_t showLen = len;
        if (showLen > kBoxW - 1u) { shown = f.buf + (len - (kBoxW - 1u)); showLen = kBoxW - 1u; }
        t.gotoXY(tl, kBoxCol, row_[i]);
        if (focused) { t.reverse(tl, true); t.color(tl, Color::White); }
        else         t.color(tl, editable(f) ? Color::Grey : Color::DarkGrey);
        for (size_t k = 0; k < showLen; ++k) t.ch(tl, mask ? '*' : shown[k]);
        char fill = focused ? ' ' : (editable(f) ? '.' : ' ');
        if (!focused) t.color(tl, Color::DarkGrey);
        for (size_t k = showLen; k < kBoxW; ++k) t.ch(tl, fill);
        if (focused) t.reverse(tl, false);
        return;
    }

    for (uint8_t r = 0; r < kAreaRows; ++r) {
        t.gotoXY(tl, kLabelCol, static_cast<uint8_t>(row_[i] + 1 + r));
        if (focused) { t.reverse(tl, true); t.color(tl, Color::White); }
        else         t.color(tl, Color::Grey);
        size_t from = static_cast<size_t>(r) * kAreaW;
        size_t k = 0;
        for (; k < kAreaW && from + k < len; ++k) t.ch(tl, f.buf[from + k]);
        if (!focused) t.color(tl, Color::DarkGrey);
        for (; k < kAreaW; ++k) t.ch(tl, focused ? ' ' : '.');
        if (focused) t.reverse(tl, false);
    }
}

void Form::drawButtons(Term& t, Timeline& tl) {
    t.gotoXY(tl, kBoxCol, buttonRow_);
    bool save = focus_ == n_;
    bool cancel = focus_ == n_ + 1;
    if (save) t.reverse(tl, true);
    t.color(tl, save ? Color::White : Color::LightGreen);
    t.text(tl, "[ Save ]");
    if (save) t.reverse(tl, false);
    t.text(tl, "  ");
    if (cancel) t.reverse(tl, true);
    t.color(tl, cancel ? Color::White : Color::LightRed);
    t.text(tl, "[ Cancel ]");
    if (cancel) t.reverse(tl, false);
}

void Form::status(const char* msg, Color c, Term& t, Timeline& tl) {
    if (!positional(t)) {
        t.nl(tl);
        t.color(tl, c);
        t.text(tl, msg);
        return;
    }
    t.gotoXY(tl, kLabelCol, statusRow_);
    t.color(tl, c);
    size_t n = 0;
    for (; msg[n] && n < 38; ++n) t.ch(tl, msg[n]);
    for (; n < 38; ++n) t.ch(tl, ' ');
    placeCursor(t, tl);
}

// placeCursor: end of the focused value, or on the focused button
void Form::placeCursor(Term& t, Timeline& tl) {
    if (focus_ >= n_) {
        uint8_t col = static_cast<uint8_t>(focus_ == n_ ? kBoxCol : kBoxCol + kSaveW + 2);
        t.gotoXY(tl, col, buttonRow_);
        return;
    }
    const FormField& f = f_[focus_];
    size_t len = strlen(f.buf);
    if (f.flags & FF_TEXTAREA) {
        size_t pos = len < static_cast<size_t>(kAreaW) * kAreaRows ? len : static_cast<size_t>(kAreaW) * kAreaRows - 1;
        t.gotoXY(tl, static_cast<uint8_t>(kLabelCol + pos % kAreaW),
                 static_cast<uint8_t>(row_[focus_] + 1 + pos / kAreaW));
        return;
    }
    size_t shown = len < kBoxW - 1u ? len : kBoxW - 1u;
    t.gotoXY(tl, static_cast<uint8_t>(kBoxCol + shown), row_[focus_]);
}

uint8_t Form::nextFocus(int dir) const {
    int i = focus_;
    for (int guard = 0; guard < n_ + 2; ++guard) {
        i += dir;
        if (i < 0) return focus_;
        if (i > n_ + 1) return focus_;
        if (i >= n_ || editable(f_[i])) return static_cast<uint8_t>(i);
    }
    return focus_;
}

void Form::setFocus(uint8_t next, Term& t, Timeline& tl) {
    if (next == focus_) return;
    uint8_t prev = focus_;
    focus_ = next;
    t.cursor(tl, false);
    if (prev < n_) drawField(prev, t, tl);
    if (next < n_) drawField(next, t, tl);
    if (prev >= n_ || next >= n_) drawButtons(t, tl);
    t.cursor(tl, true);
    placeCursor(t, tl);
}

// ===========================================================================
// Keys
// ===========================================================================

Form::Res Form::key(int k, Term& t, Timeline& tl) {
    if (k == KEY_ESC || k == KEY_BREAK) return Res::Cancel;
    if (k == KEY_F1) return Res::Save;
    return positional(t) ? keyPositional(k, t, tl) : keyLine(k, t, tl);
}

Form::Res Form::keyPositional(int k, Term& t, Timeline& tl) {
    switch (k) {
        case KEY_UP:
            setFocus(nextFocus(-1), t, tl);
            return Res::Editing;
        case KEY_DOWN:
            setFocus(nextFocus(+1), t, tl);
            return Res::Editing;
        case KEY_LEFT:
            if (focus_ == n_ + 1) setFocus(static_cast<uint8_t>(n_), t, tl);
            return Res::Editing;
        case KEY_RIGHT:
            if (focus_ == n_) setFocus(static_cast<uint8_t>(n_ + 1), t, tl);
            return Res::Editing;
        case KEY_ENTER:
            if (focus_ == n_)     return Res::Save;
            if (focus_ == n_ + 1) return Res::Cancel;
            setFocus(nextFocus(+1), t, tl);
            return Res::Editing;
        default:
            break;
    }
    if (focus_ >= n_) return Res::Editing;

    FormField& f = f_[focus_];
    size_t len = strlen(f.buf);

    if (f.flags & FF_YESNO) {
        if (k == 'y' || k == 'Y')      strcpy(f.buf, "Y");
        else if (k == 'n' || k == 'N') strcpy(f.buf, "N");
        else if (k == ' ')             strcpy(f.buf, f.buf[0] == 'Y' ? "N" : "Y");
        else return Res::Editing;
        drawField(focus_, t, tl);
        placeCursor(t, tl);
        return Res::Editing;
    }

    bool area = f.flags & FF_TEXTAREA;
    uint16_t visible = area ? static_cast<uint16_t>(kAreaW) * kAreaRows : static_cast<uint16_t>(kBoxW - 1u);

    if (k == KEY_BACKSPACE) {
        if (!len) return Res::Editing;
        f.buf[len - 1] = '\0';
        // the cursor sits at position len; only step back in place when that
        // does not cross a row start or the scrolled/clamped end of the box
        bool fast = area ? (len < visible && len % kAreaW != 0) : (len <= visible);
        if (fast) {                                      // fast path: wipe one cell in place
            t.left(tl, 1);
            t.reverse(tl, true);
            t.color(tl, Color::White);
            t.ch(tl, ' ');
            t.reverse(tl, false);
            t.left(tl, 1);
        } else {
            drawField(focus_, t, tl);
            placeCursor(t, tl);
        }
        return Res::Editing;
    }

    if (k < 0x20 || k > 0x7E || len >= f.cap) return Res::Editing;
    f.buf[len]     = static_cast<char>(k);
    f.buf[len + 1] = '\0';
    bool endsRow = area && ((len + 1) % kAreaW == 0);
    if (len + 1 < visible && !endsRow) {                 // fast path: echo in place
        t.reverse(tl, true);
        t.color(tl, Color::White);
        t.ch(tl, (f.flags & FF_MASK) ? '*' : static_cast<char>(k));
        t.reverse(tl, false);
    } else {
        drawField(focus_, t, tl);
        placeCursor(t, tl);
    }
    return Res::Editing;
}

// ---------------------------------------------------------------------------
// fail: point at the problem
// ---------------------------------------------------------------------------
void Form::fail(uint8_t field, const char* msg, Term& t, Timeline& tl) {
    t.bell(tl);
    if (!positional(t)) {
        t.nl(tl);
        t.color(tl, Color::LightRed);
        t.text(tl, msg);
        confirming_ = false;
        inLen_      = 0;
        focus_      = field < n_ ? field : 0;
        linePrompt(t, tl);
        return;
    }
    t.cursor(tl, false);
    t.gotoXY(tl, kLabelCol, statusRow_);
    t.color(tl, Color::LightRed);
    for (uint8_t k = 0; k < 38; ++k) t.ch(tl, ' ');
    t.gotoXY(tl, kLabelCol, statusRow_);
    fx::blink(t, tl, msg, 3, 110);
    uint8_t prev = focus_;
    focus_ = field < n_ ? field : prev;
    if (prev != focus_) {
        if (prev < n_) drawField(prev, t, tl);
        else           drawButtons(t, tl);
    }
    drawField(focus_, t, tl);
    drawButtons(t, tl);
    t.cursor(tl, true);
    placeCursor(t, tl);
}

void Form::after(Term& t, Timeline& tl) {
    t.reset(tl);
    t.cursor(tl, true);
    if (positional(t)) t.gotoXY(tl, 1, static_cast<uint8_t>(statusRow_ + 1));
}

// ===========================================================================
// Line mode (plain ASCII)
// ===========================================================================

void Form::linePrompt(Term& t, Timeline& tl) {
    while (focus_ < n_ && !editable(f_[focus_])) {       // show read-only values
        t.nl(tl);
        t.color(tl, Color::DarkGrey);
        t.text(tl, f_[focus_].label);
        t.text(tl, ": ");
        t.text(tl, f_[focus_].buf);
        ++focus_;
    }
    t.nl(tl);
    if (focus_ >= n_) {
        confirming_ = true;
        t.color(tl, Color::Yellow);
        t.text(tl, "Save (Y/n)? ");
        return;
    }
    const FormField& f = f_[focus_];
    t.color(tl, Color::LightBlue);
    t.text(tl, f.label);
    if (f.buf[0]) {
        t.color(tl, Color::DarkGrey);
        t.text(tl, " [");
        if (f.flags & FF_MASK) t.text(tl, "set");
        else if (strlen(f.buf) > 20) { t.textN(tl, f.buf, 17); t.text(tl, "..."); }
        else t.text(tl, f.buf);
        t.text(tl, "]");
    }
    t.color(tl, Color::Grey);
    t.text(tl, ": ");
    t.color(tl, Color::White);
    inLen_ = 0;
    in_[0] = '\0';
}

Form::Res Form::keyLine(int k, Term& t, Timeline& tl) {
    if (confirming_) {
        if (k == 'y' || k == 'Y' || k == KEY_ENTER) { t.ch(tl, 'Y'); return Res::Save; }
        if (k == 'n' || k == 'N')                   { t.ch(tl, 'N'); return Res::Cancel; }
        return Res::Editing;
    }
    FormField& f = f_[focus_];
    if (k == KEY_ENTER) {
        if (inLen_) {                                    // Enter on an empty line keeps the value
            in_[inLen_] = '\0';
            if (f.flags & FF_YESNO) strcpy(f.buf, (in_[0] == 'y' || in_[0] == 'Y') ? "Y" : "N");
            else { strncpy(f.buf, in_, f.cap); f.buf[f.cap] = '\0'; }
        }
        ++focus_;
        linePrompt(t, tl);
        return Res::Editing;
    }
    if (k == KEY_BACKSPACE) {
        if (inLen_) { --inLen_; t.eraseBack(tl, 1); }
        return Res::Editing;
    }
    if (k < 0x20 || k > 0x7E || inLen_ >= f.cap || inLen_ >= BBS_PROFILE_MAX) return Res::Editing;
    in_[inLen_++] = static_cast<char>(k);
    t.ch(tl, (f.flags & FF_MASK) ? '*' : static_cast<char>(k));
    return Res::Editing;
}
