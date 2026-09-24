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

#include "bbs.h"
#include "bbs_util.h"
#include "screens.h"
#include "ziparc.h"          // validScreenName: one rule for a screen's name
#include "../platform/platform.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

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
        DIR* d = opendir(dir);
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
    if (strcmp(first, "view") || !name[0] || (flag[0] && !flash) || *p) {
        say(Color::Grey, "SCREENS lists them.");                          // SC-usage
        say(Color::Grey, "SCREENS VIEW name[.ext] [FLASH] plays one.");  // SC-usage-2
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
