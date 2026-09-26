/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/bbs_screens.cpp
 * Module:       Core / SCREENS (1.1.0)
 *
 * Purpose:      The board's screens, seen from the board. Rob: "I think we
 *                  need a screen pager where we can see the screens online,
 *                  this should be super low overhead."
 *
 *                    SCREENS                          every screen by name
 *                    SCREENS VIEW name[.ext] [FLASH]  play one
 *                    SCREENS INSTALL [STOCK]          the card's own into
 *                                                     flash, and back (1.1.1,
 *                                                     sysop; see below)
 *
 *                  The list is one row a name: which of .ans, .asc and .seq
 *                  there are, how big the copy callers get is, and where it
 *                  comes from: flash, or the card, and there whether it is
 *                  still the stock copy the board seeded (the sd plugin's
 *                  manifest) or the sysop's own.
 *
 *                  VIEW plays one through the ordinary screen player. With no
 *                  extension it plays what this terminal would get; with one
 *                  it plays exactly that file, if this terminal can show it.
 *                  FLASH plays the stock copy even where the card overrides
 *                  it.
 *
 *                  Low overhead, as asked: nothing is cached and nothing is
 *                  kept on a Session. Each row reads the folders again, the
 *                  way the file areas do, and no directory is held open from
 *                  one row to the next. Any staff level (CF_STAFF), as SYS.
 *
 * Libraries:    none (libc stdio, dirent, sys/stat)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     SCREENS.md, COMMANDS.md
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

#include "bbs.h"
#include "bbs_util.h"
#include "screens.h"
#include "disk.h"             // opendir that tells the drive light (1.1.1)
#include "ziparc.h"          // validScreenName: one rule for a screen's name
#include "fx.h"              // spinFrame: SCREENS INSTALL's spinner
#include "../platform/platform.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>          // rmdir

using namespace bbsu;

namespace {

// The list's columns, in this order. .p40 and .p80 play too, but no stock
// screen has one and three columns are what 40 columns hold.
constexpr const char* kCols[3] = { ".ans", ".asc", ".seq" };

// Names one row's walk can tell apart. A card with more screens than this
// lists the first of them; the board's own backup holds no more than 64.
constexpr uint8_t kMaxNames = 64;

// word: the next space-separated word of *p into out, and *p past it
void word(const char*& p, char* out, size_t n) {
    while (*p == ' ') ++p;
    size_t w = 0;
    while (*p && *p != ' ') {
        if (w + 1 < n) out[w++] = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
        ++p;
    }
    out[w] = '\0';
    while (*p == ' ') ++p;
}

void flashDir(char* out, size_t n) {
    snprintf(out, n, "%s/%s", plat::fsBase(), BBS_SCREEN_DIR);
}

// baseOf: "welcome" from a directory entry that is a screen, lower cased
// (a card written on a laptop may say WELCOME.ANS, and FAT will play it),
// or false for anything that is not one.
bool baseOf(const char* entry, char* base) {
    char low[16];
    size_t n = strlen(entry);
    if (n >= sizeof(low)) return false;
    for (size_t i = 0; i <= n; ++i) low[i] = static_cast<char>(tolower(static_cast<unsigned char>(entry[i])));
    if (!ziparc::validScreenName(low)) return false;
    size_t b = static_cast<size_t>(strchr(low, '.') - low);
    memcpy(base, low, b);
    base[b] = '\0';
    return true;
}

// nthScreen: the j-th screen name: the flash folder's in its own order,
// then the ones only the card has. False past the last, with total set to
// how many there are. Walked afresh every row, nothing held between rows.
bool nthScreen(uint8_t j, char* out, uint8_t& total) {
    char seen[kMaxNames][BBS_SCREEN_NAME_MAX + 1];
    uint8_t n = 0;
    char flash[96];
    flashDir(flash, sizeof(flash));
    const char* dirs[2] = { flash, sdScreensDir() };
    for (const char* dir : dirs) {
        if (!dir || !dir[0]) continue;
        DIR* d = disk::dir(dir);
        if (!d) continue;
        for (struct dirent* e = readdir(d); e && n < kMaxNames; e = readdir(d)) {
            char base[BBS_SCREEN_NAME_MAX + 1];
            if (!baseOf(e->d_name, base)) continue;
            bool dup = false;
            for (uint8_t k = 0; k < n && !dup; ++k) dup = !strcmp(seen[k], base);
            if (dup) continue;
            memcpy(seen[n], base, sizeof(base));
            if (n == j) {
                closedir(d);
                memcpy(out, base, sizeof(base));
                return true;
            }
            ++n;
        }
        closedir(d);
    }
    total = n;
    return false;
}

// Where callers get one flavour of a screen from.
enum class From : uint8_t { None, Flash, Seeded, Own };

From whence(const char* base, const char* ext, uint32_t& size) {
    char path[112], file[16];
    struct stat st;
    snprintf(file, sizeof(file), "%s%s", base, ext);
    if (const char* card = sdScreensDir()) {
        snprintf(path, sizeof(path), "%.90s/%s", card, file);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            size = static_cast<uint32_t>(st.st_size);
            return sdSeededStock(file) ? From::Seeded : From::Own;
        }
    }
    char flash[96];
    flashDir(flash, sizeof(flash));
    snprintf(path, sizeof(path), "%.90s/%s", flash, file);
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        size = static_cast<uint32_t>(st.st_size);
        return From::Flash;
    }
    return From::None;
}

// flavourOf: what a caller would call a screen file's kind
const char* flavourOf(const char* ext) {
    if (!strcmp(ext, ".asc")) return "plain ASCII";
    if (!strcmp(ext, ".ans")) return "ANSI";
    return "PETSCII";
}

// shows: can this terminal show a file of that kind? .asc is translated for
// every terminal; the art formats are raw bytes for one kind of terminal.
bool shows(const Term& t, const char* ext) {
    if (!strcmp(ext, ".asc")) return true;
    if (!strcmp(ext, ".ans")) return t.isAnsi();
    return t.isPet();
}

} // namespace

// ---------------------------------------------------------------------------
// cmdScreens: SCREENS and SCREENS VIEW
// ---------------------------------------------------------------------------
void Bbs::cmdScreens(Session& s, const char* arg) {
    const char* p = arg;
    char first[8], name[24], flag[8];
    word(p, first, sizeof(first));
    if (!first[0]) {
        startList(s, ListKind::Screens);
        return;
    }
    word(p, name, sizeof(name));
    word(p, flag, sizeof(flag));
    bool flash = !strcmp(flag, "flash");
    Term& t = s.term;
    auto say = [&](Color c, const char* text) {
        char row[136];
        t.color(s.tl, c);
        for (const char* q = wrap(text, row, sizeof(row), rowWidth(s)); ; q = wrap(q, row, sizeof(row), rowWidth(s))) {
            t.text(s.tl, row);
            t.nl(s.tl);
            if (!q || !*q) break;
        }
    };
    if (!strcmp(first, "install") && !flag[0] && !*p && (!name[0] || !strcmp(name, "stock"))) {
        screensInstall(s, name[0] != '\0');
        return;
    }
    if (strcmp(first, "view") || !name[0] || (flag[0] && !flash) || *p) {
        say(Color::Grey, "SCREENS lists them.");                          // SC-usage
        say(Color::Grey, "SCREENS VIEW name[.ext] [FLASH] plays one.");  // SC-usage-2
        if (s.level == Access::Sysop)
            say(Color::Grey, "SCREENS INSTALL [STOCK] copies the card's to flash.");   // SC-usage-3
        prompt(s);
        return;
    }

    // A name is a screen's name and nothing else: no path, no "..", only
    // the letters a screen may have (ziparc::validScreenName), so nothing
    // typed here can reach a file that is not a screen.
    char base[BBS_SCREEN_NAME_MAX + 1] = "";
    const char* ext = nullptr;
    char probe[24];
    const char* dot = strchr(name, '.');
    if (dot) {
        snprintf(probe, sizeof(probe), "%s", name);
        ext = dot;
    } else {
        snprintf(probe, sizeof(probe), "%.12s.asc", name);
    }
    if (!ziparc::validScreenName(probe)) {
        say(Color::LightRed, "Not a screen name.");                       // SC-bad
        prompt(s);
        return;
    }
    size_t bl = static_cast<size_t>((dot ? dot : name + strlen(name)) - name);
    memcpy(base, name, bl);
    base[bl] = '\0';

    char msg[96];
    if (ext && !shows(t, ext)) {
        snprintf(msg, sizeof(msg), "%s is %s; this terminal is %s.", name, flavourOf(ext), t.name());   // SC-flavour
        say(Color::LightRed, msg);
        prompt(s);
        return;
    }
    ScreenPlayer::Found f;
    if (!ScreenPlayer::find(base, t, ext, flash, f)) {
        say(Color::LightRed, flash ? "No screen by that name in flash."   // SC-none-flash
                                   : "No screen by that name.");          // SC-none
        prompt(s);
        return;
    }
    snprintf(msg, sizeof(msg), "Playing %s%s, from %s.", base, f.ext,       // SC-playing
             f.card ? "the card" : "flash");
    say(Color::Grey, msg);
    if (!s.scr.openFound(f)) {
        say(Color::LightRed, "It would not open.");                       // SC-open
        prompt(s);
        return;
    }
    // As playScreen does: paged, and the prompt when it ends.
    s.ed = LineEditor();
    s.scr.setPaging(pageRows(s));
    s.pendingPrompt = true;
    s.pageLines     = 0;
    s.st            = SState::Shell;
}

// ---------------------------------------------------------------------------
// rowScreens: one row of SCREENS. 40 columns and 80 each get a layout:
//
//   Screens                card and flash
//   name      .ans      .asc      .seq
//   welcome   1219 C     590 F     735 O
//
//   Screens                                         card and flash
//   name        .ans                  .asc                  .seq
//   welcome     1219 card, seeded      590 flash             735 card, own
//
// Plain ASCII has no colour or bar and reads the same words.
// ---------------------------------------------------------------------------
bool Bbs::rowScreens(Session& s) {
    uint8_t row  = s.listIdx++;
    // The wide row is 76 columns (a name of 10 and three cells of 22), so a
    // terminal narrower than that gets the 39 column one, not a wrapped wide
    // one: 61 to 76 columns is a real NAWS size.
    bool    wide = rowWidth(s) >= 76;
    const char* card = sdScreensDir();
    if (row == 0) {
        rowTitle(s, "Screens", card ? "card and flash" : "flash only");     // SC-title
        return true;
    }
    if (row == 1) {
        // Each flavour over its sizes, which are right-aligned in the cell.
        char head[80];
        if (wide) snprintf(head, sizeof(head), "%-10s%6s%16s%6s%16s%6s", "name", kCols[0], "", kCols[1], "", kCols[2]);
        else      snprintf(head, sizeof(head), "%-9s%5s%5s%5s%5s%5s", "name", kCols[0], "", kCols[1], "", kCols[2]);
        rowText(s, Color::Grey, head);
        return true;
    }
    char name[BBS_SCREEN_NAME_MAX + 1];
    uint8_t total = 0;
    if (nthScreen(static_cast<uint8_t>(row - 2), name, total)) {
        char cell[32];
        uint8_t col = 0;
        snprintf(cell, sizeof(cell), wide ? "%-10s" : "%-9s", name);
        rowSeg(s, Color::White, cell, col);
        for (uint8_t i = 0; i < 3; ++i) {
            uint32_t size = 0;
            From w = whence(name, kCols[i], size);
            bool last = i == 2;
            static const char* const kWide[]  = { "", "flash", "card, seeded", "card, own" };
            static const char  kNarrow[]      = { ' ', 'F', 'C', 'O' };
            Color c = w == From::None   ? Color::DarkGrey
                    : w == From::Flash  ? Color::Grey
                    : w == From::Seeded ? Color::Cyan
                                        : Color::Yellow;
            uint8_t k = static_cast<uint8_t>(w);
            if (w == From::None) snprintf(cell, sizeof(cell), wide ? "%6s%16s" : "%5s%5s", "-", "");
            else if (wide)       snprintf(cell, sizeof(cell), "%6u %-15s", static_cast<unsigned>(size), kWide[k]);
            else                 snprintf(cell, sizeof(cell), "%5u %c   ", static_cast<unsigned>(size), kNarrow[k]);
            if (last) {                                  // nothing past the last column
                size_t e = strlen(cell);
                while (e && cell[e - 1] == ' ') cell[--e] = '\0';
            }
            rowSeg(s, c, cell, col);
        }
        rowEnd(s, col);
        return true;
    }
    switch (row - 2 - total) {
        case 0:
            if (!total) rowText(s, Color::Grey, "No screens found.");       // SC-empty
            else        rowRule(s);
            return true;
        case 1:
            rowText(s, Color::Grey, wide ? "The card's copy wins. Seeded: the board put it there. Own: yours."
                                         : "F flash  C card, seeded  O card, own");   // SC-key
            return true;
        case 2:
            rowText(s, Color::Grey, wide ? "SCREENS VIEW name[.ext] [FLASH] plays one."
                                         : "SCREENS VIEW name plays one.");           // SC-foot
            return true;
        default:
            return false;
    }
}

// ===========================================================================
// SCREENS INSTALL [STOCK] (1.1.1, Rob)
//
// A card's screens are played from the card and never copied into flash, so
// pulling the card took a sysop's own screens with it until it came back.
// SCREENS INSTALL copies them into the screens partition, where the board
// plays them from when the card is out:
//
//   - which: every screen on the card that is not the board's own seeded
//     copy (the sd plugin's manifest) and is not already in flash byte for
//     byte. The card's own name may be upper case; flash gets it in lower.
//   - checked first, all of it, like a restore: a screen's name, its size
//     (none empty, none past a restore's per-file cap), no more than a
//     restore takes, and the room: the partition's free blocks against what
//     the set needs, counted in LittleFS blocks. Refused whole if it would
//     not fit, never half installed.
//   - each screen written beside the live one under "<name>.new", then put
//     live by a rename within the partition. The stock copy it replaces is
//     moved aside, not deleted, to screens/.stock/<name>; a screen flash did
//     not have gets an empty marker there instead. Nothing live is removed
//     to make room for a rename: a rename that fails puts the stock copy
//     back where it was.
//   - callers reading one of flash's screens are let go of it first, as a
//     restore does (esp_littlefs will not rename over an open file).
//   - the card's manifest marks the installed ones as the sysop's, so the
//     next firmware's stock screens never overwrite them on the card.
//
// SCREENS INSTALL STOCK walks screens/.stock and puts it back: a stock copy
// over the installed one, an empty marker's screen removed. A firmware update
// (the installer, uploadfs) rewrites the partition whole and does the same.
//
// One step a pass, never a burst (Rule no. 1): a card entry looked at, or
// 512 bytes copied, or one rename. A flash erase stops both cores whoever
// asks for it, so the most a pass can hold the board is one block's erase.
// The job is board-wide state here, not a Session's: a sysop who hangs up
// half way leaves it to finish, because each screen goes live whole or not
// at all and stopping between two leaves nothing half done either way.
// ===========================================================================
namespace {

constexpr char kStockDir[] = ".stock";           // under the flash screens folder
constexpr uint16_t kChunk  = 512;                // bytes a pass
// Screen files the card's folder may hold for an install to look at them
// all. A position in the folder, not a count of what goes in (that is
// BBS_ZIP_MAX_FILES): the stock set is seeded there first, so the sysop's
// own sit from position 33 on.
constexpr uint16_t kMaxEntries = 256;

struct Install {
    enum Ph : uint8_t { Idle, Plan, Copy, Stock } ph = Idle;
    uint8_t  who    = 0xFF;      // the sysop's session id, while they watch
    uint16_t idx    = 0;         // Plan and Copy: the card entry looked at
    uint8_t  pick[kMaxEntries / 8] = {};   // card entries to install, by position
    uint8_t  picked = 0, same = 0, seeded = 0, done = 0, failed = 0, back = 0, gone = 0;
    int32_t  need   = 0;         // blocks the set adds so far, in the order it goes in
    int32_t  peak   = 0;         // the most it adds at any moment of that
    uint32_t bytes  = 0;
    uint32_t copied = 0;         // Copy: bytes of this screen so far
    FILE*    in     = nullptr;
    FILE*    out    = nullptr;
    char     lower[16] = {};     // this screen's flash name
    char     why[64]   = {};     // Plan: the first reason the set is refused
    uint32_t spinAt = 0;
    uint8_t  spin   = 0;
};
Install g_in;

uint32_t blocksOf(uint32_t bytes) {
    uint32_t b = (bytes + BBS_FS_BLOCK - 1) / BBS_FS_BLOCK;
    return b ? b : 1;
}

void flashPath(char* out, size_t n, const char* file, const char* suffix = "") {
    snprintf(out, n, "%s/%s/%s%s", plat::fsBase(), BBS_SCREEN_DIR, file, suffix);
}

void stockPath(char* out, size_t n, const char* file) {
    snprintf(out, n, "%s/%s/%s/%s", plat::fsBase(), BBS_SCREEN_DIR, kStockDir, file);
}

// cardEntry: the i-th screen in the card's folder, its name there and in
// lower case. Walked afresh each time, as the SCREENS list is, and nothing
// held open between passes: the card may be pulled between two of them.
bool isPicked(uint16_t i) { return i < kMaxEntries && (g_in.pick[i / 8] >> (i % 8)) & 1u; }

bool cardEntry(uint16_t i, char* name, size_t nn, char* lower, size_t ln) {
    const char* dir = sdScreensDir();
    if (!dir || !dir[0]) return false;
    DIR* d = disk::dir(dir);
    if (!d) return false;
    uint16_t k = 0;
    bool found = false;
    for (struct dirent* e = readdir(d); e; e = readdir(d)) {
        size_t n = strlen(e->d_name);
        if (e->d_name[0] == '.' || n >= ln || n >= nn) continue;
        for (size_t c = 0; c <= n; ++c) lower[c] = static_cast<char>(tolower(static_cast<unsigned char>(e->d_name[c])));
        if (!ziparc::validScreenName(lower)) continue;
        if (k++ == i) {
            memcpy(name, e->d_name, n + 1);
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

// sameFile: byte for byte, read both. Only asked when the sizes agree.
bool sameFile(const char* a, const char* b) {
    FILE* fa = disk::open(a, "rb");
    if (!fa) return false;
    FILE* fb = disk::open(b, "rb");
    if (!fb) { fclose(fa); return false; }
    char ba[128], bb[128];
    bool same = true;
    for (;;) {
        size_t na = fread(ba, 1, sizeof(ba), fa), nb = fread(bb, 1, sizeof(bb), fb);
        if (na != nb || memcmp(ba, bb, na)) { same = false; break; }
        if (!na) break;
    }
    fclose(fa);
    fclose(fb);
    return same;
}

// markOne: one installed screen, for the card's manifest (sdSeededMark),
// marked as it goes in: one rewrite a screen, one screen a pass, and only
// the ones that did go in.
const char* markOne(void* ctx, uint8_t i) {
    return i == 0 ? static_cast<const char*>(ctx) : nullptr;
}

void closeFiles() {
    if (g_in.in)  { fclose(g_in.in);  g_in.in  = nullptr; }
    if (g_in.out) { fclose(g_in.out); g_in.out = nullptr; }
}

} // namespace

bool Bbs::screensBusy() const {
    return g_in.ph != Install::Idle;
}

// screensDrop: the card is going (SD UNMOUNT, new pins). A screen half
// copied off it is abandoned whole: its "<name>.new" goes, nothing live was
// touched, and the ones already in stay in.
void Bbs::screensDrop() {
    if (g_in.ph != Install::Plan && g_in.ph != Install::Copy) return;
    closeFiles();
    if (g_in.lower[0]) {
        char neu[112];
        flashPath(neu, sizeof(neu), g_in.lower, ".new");
        remove(neu);
    }
    plat::log("screens: install stopped, the card was unmounted (%u in)", static_cast<unsigned>(g_in.done));
    g_in.why[0] = '\0';
    snprintf(g_in.why, sizeof(g_in.why), "The card went: %u installed, the rest not.",
             static_cast<unsigned>(g_in.done));
    g_in.ph = Install::Idle;
    for (Session* o : all_) {
        if (o->id != g_in.who || o->st != SState::CardJob || o->fd < 0) continue;
        o->term.nl(o->tl);
        o->term.color(o->tl, Color::Yellow);
        o->term.text(o->tl, g_in.why);
        o->term.nl(o->tl);
        prompt(*o);
    }
    plat::fsInfoStale();
}

// ---------------------------------------------------------------------------
// screensInstall: SCREENS INSTALL and SCREENS INSTALL STOCK. Sysop only (it
// rewrites the board's own screens), one at a time, and never beside a
// backup or restore, which read and write the same folder.
// ---------------------------------------------------------------------------
void Bbs::screensInstall(Session& s, bool stock) {
    auto say = [&](Color c, const char* text) {
        s.term.color(s.tl, c);
        s.term.text(s.tl, text);
        s.term.nl(s.tl);
    };
    if (s.level != Access::Sysop) {
        say(Color::LightRed, "SCREENS INSTALL is the sysop's.");                // SI-sysop
        prompt(s);
        return;
    }
    if (g_in.ph != Install::Idle) {
        say(Color::Yellow, "Screens are being installed already.");             // SI-busy
        prompt(s);
        return;
    }
    if (backup_.job() != BackupService::Job::None || backup_.holding() || backup_.busy()) {
        say(Color::Yellow, "A backup or restore is running. Try again after.");  // SI-backup
        prompt(s);
        return;
    }
    if (!stock && !sdScreensDir()) {
        say(Color::LightRed, "No card, or CONFIG sd has Screens off.");         // SI-nocard
        prompt(s);
        return;
    }
    g_in = Install();
    g_in.who    = s.id;
    g_in.ph     = stock ? Install::Stock : Install::Plan;
    g_in.spinAt = plat::millis();
    s.ed = LineEditor();
    s.st = SState::CardJob;                               // keys wait until it is done
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, stock ? "Putting the stock screens back  "              // SI-stock-go
                            : "Checking the card's screens  ");               // SI-check
    plat::log("screens: %s %s", s.user, stock ? "putting the stock screens back" : "installing the card's screens");
}

// ---------------------------------------------------------------------------
// serviceScreens: one step of the job a pass, from tick.
// ---------------------------------------------------------------------------
void Bbs::serviceScreens(uint32_t now) {
    if (g_in.ph == Install::Idle) return;
    Session* s = nullptr;
    for (Session* o : all_)
        if (o->id == g_in.who && o->st == SState::CardJob && o->fd >= 0 && o->loggedIn) s = o;

    // The spinner, only onto an empty line out, so a slow terminal spins
    // slower rather than queueing frames.
    if (s && static_cast<int32_t>(now - g_in.spinAt) >= 0 && s->tl.empty()) {
        s->term.left(s->tl, 1);
        s->term.color(s->tl, Color::Yellow);
        fx::spinFrame(s->term, s->tl, fx::Spin::Line, ++g_in.spin);
        g_in.spinAt = now + 150;
    }
    auto finish = [&](Color c, const char* text, const char* more) {
        if (s) {
            s->term.left(s->tl, 1);
            s->term.ch(s->tl, ' ');
            s->term.nl(s->tl);
            char row[136];
            const char* const parts[2] = { text, more };
            for (const char* t : parts) {
                if (!t || !*t) continue;
                s->term.color(s->tl, c);
                for (const char* q = wrap(t, row, sizeof(row), rowWidth(*s)); ; q = wrap(q, row, sizeof(row), rowWidth(*s))) {
                    s->term.text(s->tl, row);
                    s->term.nl(s->tl);
                    if (!q || !*q) break;
                }
                c = Color::Grey;
            }
            prompt(*s);
        }
        g_in.ph = Install::Idle;
        plat::fsInfoStale();
    };
    char msg[112], more[112];

    // --- Plan: one card entry a pass --------------------------------------
    if (g_in.ph == Install::Plan) {
        char name[16], cardPath[112], live[112], stockp[112];
        if (!sdScreensDir()) { finish(Color::LightRed, "The card went. Nothing was installed.", nullptr); return; }
        if (g_in.idx >= kMaxEntries) {
            // A folder this full is not looked at further, and not taken
            // in part: the walk has to end, and a set it did not see whole
            // is not a set it checked.
            if (!g_in.why[0])
                snprintf(g_in.why, sizeof(g_in.why), "More than %u files in the card's screens folder.",  // SI-folder
                         static_cast<unsigned>(kMaxEntries));
        } else if (cardEntry(g_in.idx, name, sizeof(name), g_in.lower, sizeof(g_in.lower))) {
            const uint16_t i = g_in.idx++;
            snprintf(cardPath, sizeof(cardPath), "%.90s/%s", sdScreensDir(), name);
            struct stat cs, fs, ss;
            if (stat(cardPath, &cs) != 0 || !S_ISREG(cs.st_mode)) return;          // not a file: pass it by
            const uint32_t size = static_cast<uint32_t>(cs.st_size);
            if (sdSeededStock(g_in.lower)) { ++g_in.seeded; return; }              // the board's own copy
            flashPath(live, sizeof(live), g_in.lower);
            const bool inFlash = stat(live, &fs) == 0 && S_ISREG(fs.st_mode);
            if (inFlash && static_cast<uint32_t>(fs.st_size) == size && sameFile(cardPath, live)) {
                ++g_in.same;
                return;
            }
            if (!size && !g_in.why[0])
                snprintf(g_in.why, sizeof(g_in.why), "%s is empty.", g_in.lower);                  // SI-empty
            if (size > BBS_ZIP_FILE_MAX && !g_in.why[0])
                snprintf(g_in.why, sizeof(g_in.why), "%s is too big: %u KB, a screen may be %u.",  // SI-big
                         g_in.lower, static_cast<unsigned>((size + 1023) / 1024),
                         static_cast<unsigned>(BBS_ZIP_FILE_MAX / 1024));
            if (g_in.picked >= BBS_ZIP_MAX_FILES) {
                if (!g_in.why[0]) snprintf(g_in.why, sizeof(g_in.why), "More than %u screens to install.",   // SI-many
                                           static_cast<unsigned>(BBS_ZIP_MAX_FILES));
                return;
            }
            g_in.pick[i / 8] = static_cast<uint8_t>(g_in.pick[i / 8] | (1u << (i % 8)));
            ++g_in.picked;
            g_in.bytes += size;
            // What the set adds once it is in. The stock copy it replaces
            // moves aside and keeps its blocks; a screen installed before is
            // replaced, and its blocks come back.
            stockPath(stockp, sizeof(stockp), g_in.lower);
            const bool hadStock = stat(stockp, &ss) == 0;
            const int32_t newB = static_cast<int32_t>(blocksOf(size));
            const int32_t oldB = (hadStock && inFlash) ? static_cast<int32_t>(blocksOf(static_cast<uint32_t>(fs.st_size))) : 0;
            // The peak is while this one sits beside the one it replaces:
            // everything in so far plus its new blocks, before the old ones
            // go. The largest of those over the set, in the order it goes in.
            if (g_in.need + newB > g_in.peak) g_in.peak = g_in.need + newB;
            g_in.need += newB - oldB;
            return;
        }
        // The whole set looked at: refuse it whole, or go.
        g_in.lower[0] = '\0';
        if (g_in.why[0]) {
            finish(Color::LightRed, g_in.why, "Nothing was installed.");                          // SI-refused
            return;
        }
        if (!g_in.picked) {
            finish(Color::LightGreen, "Nothing to install: flash has every screen of yours already.",   // SI-none
                   nullptr);
            return;
        }
        if (g_in.bytes > BBS_ZIP_TOTAL_MAX) {
            snprintf(msg, sizeof(msg), "%u KB of screens, more than the %u KB a restore takes.",   // SI-total
                     static_cast<unsigned>((g_in.bytes + 1023) / 1024),
                     static_cast<unsigned>(BBS_ZIP_TOTAL_MAX / 1024));
            finish(Color::LightRed, msg, "Nothing was installed.");
            return;
        }
        uint32_t total = 0, used = 0;
        if (!plat::fsInfo(total, used)) {
            finish(Color::LightRed, "Could not read the flash's free space.", "Nothing was installed.");  // SI-noinfo
            return;
        }
        // The peak of the running total (Plan), plus the .stock folder and
        // a spare block.
        const int32_t freeB = static_cast<int32_t>((total > used ? total - used : 0) / BBS_FS_BLOCK);
        const int32_t want  = (g_in.peak > 0 ? g_in.peak : 0) + 2;
        if (want > freeB) {
            snprintf(msg, sizeof(msg), "Not enough room in flash: %u KB needed, %u KB free.",       // SI-room
                     static_cast<unsigned>(want * (BBS_FS_BLOCK / 1024)),
                     static_cast<unsigned>(freeB * (BBS_FS_BLOCK / 1024)));
            finish(Color::LightRed, msg, "Nothing was installed.");
            return;
        }
        char dir[112];
        snprintf(dir, sizeof(dir), "%s/%s/%s", plat::fsBase(), BBS_SCREEN_DIR, kStockDir);
        mkdir(dir, 0755);                                  // there already is fine
        g_in.idx = 0;
        g_in.ph  = Install::Copy;
        plat::log("screens: installing %u, %u bytes, %d blocks of %d free",
                  static_cast<unsigned>(g_in.picked), static_cast<unsigned>(g_in.bytes),
                  static_cast<int>(want), static_cast<int>(freeB));
        return;
    }

    // --- Copy: 512 bytes a pass, then the rename ----------------------------
    if (g_in.ph == Install::Copy) {
        char name[16], cardPath[112], live[112], neu[112], stockp[112];
        if (!g_in.in) {
            while (g_in.idx < kMaxEntries && !isPicked(g_in.idx)) ++g_in.idx;
            if (g_in.idx >= kMaxEntries) {
                snprintf(msg, sizeof(msg), "Installed %u screen%s in flash.",                    // SI-done
                         static_cast<unsigned>(g_in.done), g_in.done == 1 ? "" : "s");
                if (g_in.failed)
                    snprintf(more, sizeof(more), "%u could not be put in. SCREENS INSTALL STOCK undoes it.",  // SI-partial
                             static_cast<unsigned>(g_in.failed));
                else
                    snprintf(more, sizeof(more), "They play from flash with the card out. SCREENS INSTALL STOCK undoes it.");  // SI-done-2
                plat::log("screens: installed %u, %u failed, %u already in flash, %u seeded",
                          static_cast<unsigned>(g_in.done), static_cast<unsigned>(g_in.failed),
                          static_cast<unsigned>(g_in.same), static_cast<unsigned>(g_in.seeded));
                finish(g_in.failed ? Color::Yellow : Color::LightGreen, msg, more);
                return;
            }
            if (!cardEntry(g_in.idx, name, sizeof(name), g_in.lower, sizeof(g_in.lower))) {
                ++g_in.failed;                              // gone from the card since the check
                ++g_in.idx;
                return;
            }
            snprintf(cardPath, sizeof(cardPath), "%.90s/%s", sdScreensDir(), name);
            flashPath(neu, sizeof(neu), g_in.lower, ".new");
            // Checked again as it goes in: the card is the sysop's to change
            // while this runs, and what sits at this place in the folder now
            // may not be what was checked there.
            struct stat cs;
            if (stat(cardPath, &cs) != 0 || !S_ISREG(cs.st_mode) || cs.st_size <= 0 ||
                cs.st_size > BBS_ZIP_FILE_MAX || sdSeededStock(g_in.lower)) {
                ++g_in.failed;
                ++g_in.idx;
                return;
            }
            g_in.copied = 0;
            g_in.in  = disk::open(cardPath, "rb");
            g_in.out = g_in.in ? disk::open(neu, "wb") : nullptr;
            if (!g_in.in || !g_in.out) {
                closeFiles();
                remove(neu);
                ++g_in.failed;
                ++g_in.idx;
            }
            return;
        }
        char buf[kChunk];
        size_t n = fread(buf, 1, sizeof(buf), g_in.in);
        g_in.copied += static_cast<uint32_t>(n);
        // The card is the sysop's to change while this runs: a file that
        // has grown past what the check allowed since it was checked (or a
        // different one now at that place in the folder) is not put in.
        if ((n && fwrite(buf, 1, n, g_in.out) != n) || g_in.copied > BBS_ZIP_FILE_MAX) {
            closeFiles();
            flashPath(neu, sizeof(neu), g_in.lower, ".new");
            remove(neu);
            plat::diskPulse(plat::DISK_ERROR);
            ++g_in.failed;
            ++g_in.idx;
            return;
        }
        if (n == sizeof(buf)) return;                       // more next pass
        const bool readOk = !ferror(g_in.in);
        fclose(g_in.in);
        g_in.in = nullptr;
        const bool wroteOk = fclose(g_in.out) == 0;
        g_in.out = nullptr;
        flashPath(neu, sizeof(neu), g_in.lower, ".new");
        flashPath(live, sizeof(live), g_in.lower);
        stockPath(stockp, sizeof(stockp), g_in.lower);
        ++g_in.idx;
        if (!readOk || !wroteOk) {
            remove(neu);
            plat::diskPulse(plat::DISK_ERROR);
            ++g_in.failed;
            return;
        }
        // Nobody may be reading flash's copy while it is replaced.
        endScreens(false, "Screen ended: the sysop is changing screens.");
        struct stat st;
        bool moved = false, marker = false;
        if (stat(stockp, &st) != 0) {                        // the stock copy, kept once
            if (stat(live, &st) == 0) {
                moved = rename(live, stockp) == 0;
                if (!moved) { remove(neu); ++g_in.failed; return; }
            } else if (FILE* m = disk::open(stockp, "wb")) {  // flash had none: an empty marker
                fclose(m);
                marker = true;
            } else {
                remove(neu);
                ++g_in.failed;
                return;
            }
        }
        if (rename(neu, live) == 0) {
            ++g_in.done;
            // The card's manifest marks it the sysop's, so a later firmware's
            // stock screens never replace it there.
            sdSeededMark(markOne, g_in.lower);
            return;
        }
        // Put back what was moved: the board is left as it was for this one.
        if (moved)  rename(stockp, live);
        if (marker) remove(stockp);
        remove(neu);
        plat::diskPulse(plat::DISK_ERROR);
        ++g_in.failed;
        return;
    }

    // --- Stock: one kept copy a pass --------------------------------------
    if (g_in.ph == Install::Stock) {
        char dir[112];
        snprintf(dir, sizeof(dir), "%s/%s/%s", plat::fsBase(), BBS_SCREEN_DIR, kStockDir);
        char file[16] = "";
        if (DIR* d = disk::dir(dir)) {
            for (struct dirent* e = readdir(d); e; e = readdir(d)) {
                if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof(file)) continue;
                if (!ziparc::validScreenName(e->d_name)) continue;
                memcpy(file, e->d_name, strlen(e->d_name) + 1);
                break;
            }
            closedir(d);
        }
        if (!file[0]) {
            const bool any = g_in.back || g_in.gone;
            rmdir(dir);
            if (!any) {
                finish(Color::LightGreen, "Flash has the stock screens already.", nullptr);      // SI-stock-none
                return;
            }
            snprintf(msg, sizeof(msg), "Stock screens back: %u put back, %u removed.",           // SI-stock-done
                     static_cast<unsigned>(g_in.back), static_cast<unsigned>(g_in.gone));
            snprintf(more, sizeof(more), "The card's own screens still play while it is in.");  // SI-stock-2
            plat::log("screens: stock back, %u put back, %u removed, %u failed",
                      static_cast<unsigned>(g_in.back), static_cast<unsigned>(g_in.gone),
                      static_cast<unsigned>(g_in.failed));
            finish(Color::LightGreen, msg, more);
            return;
        }
        char live[112], stockp[112];
        flashPath(live, sizeof(live), file);
        stockPath(stockp, sizeof(stockp), file);
        endScreens(false, "Screen ended: the sysop is changing screens.");
        struct stat st;
        bool ok;
        if (stat(stockp, &st) == 0 && st.st_size > 0) {
            ok = rename(stockp, live) == 0;                  // over the installed one
            if (ok) ++g_in.back;
        } else {
            // Flash had no such screen: the installed one goes, then its marker.
            ok = remove(live) == 0 || stat(live, &st) != 0;
            if (ok) { remove(stockp); ++g_in.gone; }
        }
        if (!ok) {
            // Stopped here rather than stepped past: the stock copy is the
            // only one, so it is never removed to get on, and trying the
            // same file every pass would never end. Another STOCK resumes.
            ++g_in.failed;
            plat::diskPulse(plat::DISK_ERROR);
            snprintf(msg, sizeof(msg), "Could not put %s back: %u put back, %u removed so far.",   // SI-stock-stuck
                     file, static_cast<unsigned>(g_in.back), static_cast<unsigned>(g_in.gone));
            finish(Color::Yellow, msg, "SCREENS INSTALL STOCK again carries on.");
        }
        return;
    }
}
