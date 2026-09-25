/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/screens.cpp
 * Module:       Core / screen player
 *
 * Purpose:      Screen file player (see screens.h).
 *
 * Libraries:    none (libc stdio)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     SCREENS.md
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

#include "screens.h"
#include "fx.h"
#include "clock.h"
#include "sysconfig.h"
#include "../config.h"
#include "../platform/platform.h"
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>

// pump() stops pacing @BAUD@ output at 48 free frames, so there has to be
// more than that to begin with or a paced screen would never start.
static_assert(BBS_TL_FRAMES > 64, "@BAUD@ pacing needs frames to spare");

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
bool ScreenPlayer::open(const char* name, const Term& t, bool flashOnly) {
    close();
    const Ext* list = kAscii;
    size_t     cnt  = 1;
    switch (t.type()) {
        case TermType::Pet40: list = kPet40; cnt = 3; break;
        case TermType::Pet80: list = kPet80; cnt = 3; break;
        case TermType::Ansi:  list = kAnsi;  cnt = 2; break;
        default: break;
    }
    // The SD card first, when there is one, then the stock set.
    //
    // Override rather than replace: a sysop who wants one custom screen puts
    // one file on the card and keeps the rest. Pull the card and the board
    // falls back to the screens it shipped with rather than losing them,
    // which is the difference between the card being optional and the card
    // being required. Asked through a function so the core does not have to
    // know a plugin exists; it returns null when there is no card.
    // Both entries are complete screen directories, not roots to build one
    // out of. sdScreensDir() already names the directory, and the first cut
    // of this appended the screens folder to it a second time and looked in
    // <sd>/screens/screens. It failed silently, because a screen that is not
    // found is an ordinary thing here: the flash copy was found on the next
    // pass and every screen still played.
    char flashDir[96];
    snprintf(flashDir, sizeof(flashDir), "%s/%s", plat::fsBase(), BBS_SCREEN_DIR);
    const char* dirs[2] = { flashOnly ? nullptr : sdScreensDir(), flashDir };

    // Extension first, then directory. The other way round let a lower
    // preference format on the card beat the right format in flash: a sysop
    // dropping a quick welcome.asc on a card to try the override would have
    // silently taken every C64 caller off welcome.p40 and every ANSI caller
    // off welcome.ans, and it would have looked like it worked. The override
    // is per file, not per format, so the card wins only for the same
    // extension the flash copy would have used.
    char path[96];
    for (size_t i = 0; i < cnt; ++i) {
    for (size_t r = 0; r < 2; ++r) {
        if (!dirs[r] || !dirs[r][0]) continue;
        snprintf(path, sizeof(path), "%s/%s%s", dirs[r], name, list[i].ext);
        FILE* f = fopen(path, "rb");
        if (f) {
            fromCard_ = (r == 0);
            plat::diskPulse(fromCard_ ? plat::DISK_CARD : plat::DISK_FLASH);   // the drive light
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
            typeMs_   = 0;
            pace_     = true;
            return true;
        }
    }
    }
    return false;
}

// ---------------------------------------------------------------------------
// find: where open() would get this screen from, or exactly name + want
// (".seq") when want is given, without opening anything (1.1.0, SCREENS
// VIEW). The same order open() uses: for each extension the terminal takes,
// the card and then flash.
// ---------------------------------------------------------------------------
bool ScreenPlayer::find(const char* name, const Term& t, const char* want, bool flashOnly, Found& out) {
    static const Ext kAll[] = { {".asc", 0}, {".ans", 2}, {".seq", 1}, {".p40", 1}, {".p80", 1} };
    const Ext* list = kAscii;
    size_t     cnt  = 1;
    if (want) {
        list = nullptr;
        for (const Ext& e : kAll) if (!strcmp(e.ext, want)) { list = &e; break; }
        if (!list) return false;
    } else {
        switch (t.type()) {
            case TermType::Pet40: list = kPet40; cnt = 3; break;
            case TermType::Pet80: list = kPet80; cnt = 3; break;
            case TermType::Ansi:  list = kAnsi;  cnt = 2; break;
            default: break;
        }
    }
    char flashDir[96];
    snprintf(flashDir, sizeof(flashDir), "%s/%s", plat::fsBase(), BBS_SCREEN_DIR);
    const char* dirs[2] = { flashOnly ? nullptr : sdScreensDir(), flashDir };
    for (size_t i = 0; i < cnt; ++i) {
        for (size_t r = 0; r < 2; ++r) {
            if (!dirs[r] || !dirs[r][0]) continue;
            snprintf(out.path, sizeof(out.path), "%.90s/%.12s%s", dirs[r], name, list[i].ext);
            struct stat st;
            if (stat(out.path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
            out.ext  = list[i].ext;
            out.mode = list[i].mode;
            out.card = r == 0;
            return true;
        }
    }
    out.path[0] = '\0';
    return false;
}

bool ScreenPlayer::openFound(const Found& f) {
    close();
    FILE* file = fopen(f.path, "rb");
    if (!file) return false;
    fromCard_ = f.card;
    plat::diskPulse(fromCard_ ? plat::DISK_CARD : plat::DISK_FLASH);   // the drive light
    f_        = file;
    mode_     = static_cast<Mode>(f.mode);
    inTok_    = false;
    tokLen_   = 0;
    prev_     = 0;
    sauce_    = false;
    bufLen_   = 0;
    bufPos_   = 0;
    pageRows_ = 0;
    lines_    = 0;
    paused_   = false;
    typeMs_   = 0;
    pace_     = true;
    return true;
}

void ScreenPlayer::close() {
    if (f_) {
        fclose(f_);
        f_ = nullptr;
    }
    fromCard_  = false;
    paused_    = false;
    pageBreak_ = false;
    typeMs_    = 0;
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
    if (typeMs_) tl.delay(typeMs_);
}

// ---------------------------------------------------------------------------
// say: a token's value, paced like the file's own bytes under @BAUD@
//
// One character at a time, not one byte, so the micro sign's two bytes stay
// together and Term::text still turns them into the terminal's glyph.
// ---------------------------------------------------------------------------
void ScreenPlayer::say(Term& t, Timeline& tl, const char* s) {
    if (!typeMs_) { t.text(tl, s); return; }
    char one[5];
    while (*s) {
        uint8_t n = 1;
        while (n < 4 && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80) ++n;
        memcpy(one, s, n);
        one[n] = '\0';
        t.text(tl, one);
        tl.delay(typeMs_);
        s += n;
    }
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

    if (!strcmp(tok_, "BBS"))        { say(t, tl, BBS_NAME); }
    else if (!strcmp(tok_, "BOARD")) {          // this board, not the software
        const char* n = syscfg::get().boardName;
        say(t, tl, n[0] ? n : BBS_NAME);
    }
    else if (!strcmp(tok_, "VER"))   { say(t, tl, BBS_VERSION_SHOWN); }
    else if (!strcmp(tok_, "NODE"))  { snprintf(num, sizeof(num), "%u", v.node);  say(t, tl, num); }
    else if (!strcmp(tok_, "NODES")) { snprintf(num, sizeof(num), "%u", v.nodes); say(t, tl, num); }
    else if (!strcmp(tok_, "USER"))  { say(t, tl, (v.user && *v.user) ? v.user : "caller"); }
    else if (!strcmp(tok_, "TERM"))  { say(t, tl, t.name()); }
    else if (!strcmp(tok_, "COLS"))  { snprintf(num, sizeof(num), "%u", t.cols()); say(t, tl, num); }
    else if (!strcmp(tok_, "DATE"))  { char d[24]; clk::fmt(d, sizeof(d), "%a %d %b %Y"); say(t, tl, d); }
    else if (!strcmp(tok_, "TIME"))  { char d[16]; clk::fmt(d, sizeof(d), "%H:%M"); say(t, tl, d); }
    else if (!strcmp(tok_, "CLS"))   { t.cls(tl); lines_ = 0; }
    else if (!strcmp(tok_, "BELL"))  { t.bell(tl); }
    else if (!strncmp(tok_, "DELAY:", 6)) {
        tl.delay(static_cast<uint16_t>(atoi(tok_ + 6)));
    }
    else if (!strncmp(tok_, "BAUD:", 5)) {
        // Ten bits a character on the wire, so 300 baud is 33 ms each.
        // 0 or anything unparsable is full speed again.
        long bps = atol(tok_ + 5);
        long ms  = bps > 0 ? 10000 / bps : 0;
        if (bps > 0 && ms < 1) ms = 1;
        typeMs_ = pace_ ? static_cast<uint8_t>(ms > 255 ? 255 : ms) : 0;
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
    if (b == '@') {
        // "@@" is a literal @, the same escape a caller's message uses, so
        // a screen can show a code instead of running it: the CODES screen
        // writes @@BELL@@ to print @BELL@ rather than ringing every
        // reader's bell. Only when the @ has just opened a token, so two
        // tokens side by side ("@TIME@@DELAY:400@") still read as a close
        // and an open, as they always have.
        if (tokLen_ == 0) { emitByte(t, tl, '@'); inTok_ = false; return; }
        runToken(t, tl, v);
        return;
    }

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
            // The drive light: every chunk is an access, and a read that
            // failed rather than ran out is a storage error.
            if (n) plat::diskPulse(fromCard_ ? plat::DISK_CARD : plat::DISK_FLASH);
            else if (!sauce_ && ferror(f_)) plat::diskPulse(plat::DISK_ERROR);
            if (n == 0) {
                if (inTok_) flushToken(t, tl);
                close();
                return false;
            }
            bufLen_ = static_cast<uint8_t>(n);
            bufPos_ = 0;
        }
        while (bufPos_ < bufLen_ && !sauce_) {
            // Paced output takes a frame per character. Stop while there is
            // still room for a whole token's worth (a 40 character board
            // name), because a put() with no frame to open is dropped rather
            // than refused, and the rest follows on the next pass.
            if (typeMs_ && tl.freeFrames() <= 48) return true;
            if (buf_[bufPos_] == 0x0C) {         // form feed: the screen's own page break
                ++bufPos_;
                paused_ = pageBreak_ = true;
                return true;
            }
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
