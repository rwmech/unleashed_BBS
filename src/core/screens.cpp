/*
 * File:        src/core/screens.cpp
 * Description: Screen file player (see screens.h).
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc stdio)
 */
#include "screens.h"
#include "fx.h"
#include "clock.h"
#include "../config.h"
#include "../platform/platform.h"
#include <cstring>
#include <cstdlib>

namespace {

struct Ext { const char* ext; uint8_t mode; };   // mode: 0 text, 1 pet, 2 ansi

const Ext kPet40[] = { {".p40", 1}, {".seq", 1}, {".asc", 0} };
const Ext kPet80[] = { {".p80", 1}, {".seq", 1}, {".asc", 0} };
const Ext kAnsi[]  = { {".ans", 2}, {".asc", 0} };
const Ext kAscii[] = { {".asc", 0} };

// ---------------------------------------------------------------------------
// tokChar: normalize a token byte to uppercase ASCII, 0 if not allowed
// ---------------------------------------------------------------------------
char tokChar(uint8_t b, bool pet) {
    if (pet) {
        if (b >= 0x41 && b <= 0x5A) return static_cast<char>(b);          // lower -> upper
        if (b >= 0xC1 && b <= 0xDA) return static_cast<char>(b - 0x80);   // upper
    } else {
        if (b >= 'a' && b <= 'z') return static_cast<char>(b - 0x20);
        if (b >= 'A' && b <= 'Z') return static_cast<char>(b);
    }
    if (b >= '0' && b <= '9') return static_cast<char>(b);
    if (b == ':') return ':';
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// open: resolve the file for this terminal type
// ---------------------------------------------------------------------------
bool ScreenPlayer::open(const char* name, const Term& t) {
    close();
    const Ext* list = kAscii;
    size_t     cnt  = 1;
    switch (t.type()) {
        case TermType::Pet40: list = kPet40; cnt = 3; break;
        case TermType::Pet80: list = kPet80; cnt = 3; break;
        case TermType::Ansi:  list = kAnsi;  cnt = 2; break;
        default: break;
    }
    char path[96];
    for (size_t i = 0; i < cnt; ++i) {
        snprintf(path, sizeof(path), "%s/%s/%s%s",
                 plat::fsBase(), BBS_SCREEN_DIR, name, list[i].ext);
        FILE* f = fopen(path, "rb");
        if (f) {
            f_        = f;
            mode_     = static_cast<Mode>(list[i].mode);
            inTok_    = false;
            tokLen_   = 0;
            prev_     = 0;
            sauce_    = false;
            bufLen_   = 0;
            bufPos_   = 0;
            pageRows_ = 0;
            lines_    = 0;
            paused_   = false;
            return true;
        }
    }
    return false;
}

void ScreenPlayer::close() {
    if (f_) {
        fclose(f_);
        f_ = nullptr;
    }
    paused_ = false;
}

// ---------------------------------------------------------------------------
// emitByte: one file byte to the terminal in the file's native format
// ---------------------------------------------------------------------------
void ScreenPlayer::emitByte(Term& t, Timeline& tl, uint8_t b) {
    switch (mode_) {
        case Mode::Pet:
            tl.putc(b);
            if (b == 0xFF && t.iacEscape()) tl.putc(0xFF);   // telnet IAC escape
            break;
        case Mode::Ansi:
            if (b == '\n' && prev_ != '\r') tl.putc('\r');
            if (b >= 0x80) t.cp437(tl, b);
            else           tl.putc(b);
            break;
        case Mode::Text:
        default:
            t.ch(tl, static_cast<char>(b));
            if (b == '\n' && lines_ < 255) ++lines_;
            break;
    }
    prev_ = b;
}

// ---------------------------------------------------------------------------
// flushToken: not a known @-code, print it literally
// ---------------------------------------------------------------------------
void ScreenPlayer::flushToken(Term& t, Timeline& tl) {
    emitByte(t, tl, '@');
    for (uint8_t i = 0; i < tokLen_; ++i) emitByte(t, tl, raw_[i]);
    inTok_  = false;
    tokLen_ = 0;
}

// ---------------------------------------------------------------------------
// runToken: execute a complete @-code
// ---------------------------------------------------------------------------
void ScreenPlayer::runToken(Term& t, Timeline& tl, const Vars& v) {
    tok_[tokLen_] = '\0';
    char num[8];

    if (!strcmp(tok_, "BBS"))        { t.text(tl, BBS_NAME); }
    else if (!strcmp(tok_, "VER"))   { t.text(tl, BBS_VERSION); }
    else if (!strcmp(tok_, "NODE"))  { snprintf(num, sizeof(num), "%u", v.node);  t.text(tl, num); }
    else if (!strcmp(tok_, "NODES")) { snprintf(num, sizeof(num), "%u", v.nodes); t.text(tl, num); }
    else if (!strcmp(tok_, "USER"))  { t.text(tl, (v.user && *v.user) ? v.user : "caller"); }
    else if (!strcmp(tok_, "TERM"))  { t.text(tl, t.name()); }
    else if (!strcmp(tok_, "COLS"))  { snprintf(num, sizeof(num), "%u", t.cols()); t.text(tl, num); }
    else if (!strcmp(tok_, "DATE"))  { char d[24]; clk::fmt(d, sizeof(d), "%a %d %b %Y"); t.text(tl, d); }
    else if (!strcmp(tok_, "TIME"))  { char d[16]; clk::fmt(d, sizeof(d), "%H:%M"); t.text(tl, d); }
    else if (!strcmp(tok_, "CLS"))   { t.cls(tl); lines_ = 0; }
    else if (!strcmp(tok_, "BELL"))  { t.bell(tl); }
    else if (!strncmp(tok_, "DELAY:", 6)) {
        tl.delay(static_cast<uint16_t>(atoi(tok_ + 6)));
    }
    else if (!strncmp(tok_, "SPIN:", 5)) {
        fx::spinner(t, tl, fx::Spin::Line, static_cast<uint16_t>(atoi(tok_ + 5)), 100);
    }
    else {
        flushToken(t, tl);
        // the closing '@' may open a new token (e.g. "a@b@BBS@")
        inTok_  = true;
        tokLen_ = 0;
        return;
    }
    inTok_  = false;
    tokLen_ = 0;
}

// ---------------------------------------------------------------------------
// byteIn: token scanner in front of emitByte
// ---------------------------------------------------------------------------
void ScreenPlayer::byteIn(Term& t, Timeline& tl, const Vars& v, uint8_t b) {
    if (mode_ == Mode::Ansi && b == 0x1A) { sauce_ = true; return; }

    if (!inTok_) {
        if (b == '@') { inTok_ = true; tokLen_ = 0; return; }
        emitByte(t, tl, b);
        return;
    }
    if (b == '@') { runToken(t, tl, v); return; }

    char c = tokChar(b, mode_ == Mode::Pet);
    if (!c || tokLen_ >= sizeof(tok_) - 1) {
        flushToken(t, tl);
        emitByte(t, tl, b);
        return;
    }
    raw_[tokLen_] = b;
    tok_[tokLen_] = c;
    ++tokLen_;
}

// ---------------------------------------------------------------------------
// pump: read while the Timeline has room for a chunk plus token effects
// ---------------------------------------------------------------------------
bool ScreenPlayer::pump(Term& t, Timeline& tl, const Vars& v) {
    while (f_ && !paused_ && tl.freeBytes() > (BBS_SCREEN_CHUNK * 4u + 256u) && tl.freeFrames() > 32) {
        if (bufPos_ >= bufLen_) {
            size_t n = sauce_ ? 0 : fread(buf_, 1, sizeof(buf_), f_);
            if (n == 0) {
                if (inTok_) flushToken(t, tl);
                close();
                return false;
            }
            bufLen_ = static_cast<uint8_t>(n);
            bufPos_ = 0;
        }
        while (bufPos_ < bufLen_ && !sauce_) {
            if (pageRows_ && mode_ == Mode::Text && lines_ >= pageRows_) {
                paused_ = true;                  // caller shows More, then resume()
                return true;
            }
            byteIn(t, tl, v, buf_[bufPos_++]);
        }
        if (sauce_) bufPos_ = bufLen_;
    }
    return f_ != nullptr;
}
