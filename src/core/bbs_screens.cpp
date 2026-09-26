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
 *                  Low overhead, as asked, and since 1.1.2 without lag: the
 *                  list is one table built on the background runner (the
 *                  folders read once, the card's manifest read once, each card
 *                  copy hashed once) and drawn from it by the ordinary paced
 *                  list. Worked out a row at a time on the loop, as it was,
 *                  it held a pass for 1.2 s. Any staff level (CF_STAFF), as
 *                  SYS.
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
#include "runner.h"          // the table and the plan, built off the loop (1.1.2)
#include "space.h"           // the screens partition's kept figure, marked stale
#include "../platform/platform.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>          // the table: heap while it is looked at
#include <new>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>          // rmdir

using namespace bbsu;

namespace {

// The flavours a screen comes in, in this order. The list shows the first
// three: no stock screen has a .p40 or a .p80, and three columns are what 40
// columns hold. SCREENS INSTALL takes all five.
constexpr const char* kExt[5] = { ".ans", ".asc", ".seq", ".p40", ".p80" };
constexpr uint8_t     kExts   = 5;
constexpr uint8_t     kCols   = 3;

// Names the table can hold. A card with more screens than this lists the
// first of them; the board's own backup holds no more than 64.
constexpr uint8_t kMaxNames = 64;

// Where callers get one flavour of a screen from.
enum From : uint8_t { F_NONE = 0, F_FLASH, F_SEEDED, F_OWN };

// ===========================================================================
// The table (1.1.2): every screen, built on the background runner.
//
// The list used to work a row out when the row was drawn: the flash folder
// and the card's walked from the top for every row, and for every flavour a
// stat of the card copy, the card's whole manifest read, and the card copy
// hashed end to end. About twenty rows a pass: SCREENS held the loop 1.22 to
// 1.28 s on the bench, the worst pass on the board (Rule no. 1 fails).
//
// Now one runner job builds it: each folder read once, the manifest read
// once, each card copy hashed once. The loop shows a spinner meanwhile, then
// draws rows out of the table through the ordinary paced list. SCREENS
// INSTALL plans from the same table, on the same runner.
//
// One table, on the heap while somebody is looking at it: it is 3 KB that
// every board would otherwise keep in static DRAM for a staff screen, and
// the camera boards have 6 to 8 KB of that left. It is taken when SCREENS or
// SCREENS INSTALL starts and given back when nobody is in the list, the
// install is over and no build is out.
// ===========================================================================
struct Cell {
    uint32_t size  : 24;     // bytes, of the copy callers get (the card's when there is one)
    uint32_t from  : 3;      // From
    uint32_t card  : 1;      // a copy on the card
    uint32_t flash : 1;      // a copy in flash
    uint32_t done  : 1;      // SCREENS INSTALL: put in, for the card's manifest
};

struct Row {
    char name[BBS_SCREEN_NAME_MAX + 1];
    Cell cell[kExts];
};

struct Table {
    uint8_t  rows  = 0;
    bool     full  = false;          // more names than kMaxNames
    bool     card  = false;          // the card's screens were there to read
    uint8_t  picks = 0;              // SCREENS INSTALL: screens to put in, in order
    char     pickName[BBS_ZIP_MAX_FILES][16] = {};   // the card's own spelling
    uint8_t  pickRow[BBS_ZIP_MAX_FILES] = {};
    uint8_t  pickCol[BBS_ZIP_MAX_FILES] = {};
    Row      row[kMaxNames];
};

// The build: its inputs set by the loop before post, its results written by
// the runner, read by the loop only once the job says DONE.
struct Build {
    runner::Job job;
    Table*   t       = nullptr;
    bool     install = false;        // in: plan SCREENS INSTALL as well
    // out: the plan
    uint8_t  same = 0, seeded = 0;
    uint32_t bytes = 0;
    int32_t  want = 0, freeB = 0;
    bool     ready = false;          // planned, room checked, .stock made: copy
    char     why[64] = {};           // refused whole, and why
};
Build    g_b;
Table*   g_tab   = nullptr;          // the loop's; the runner writes it only inside a build
uint32_t g_tabAt = 0;                // millis the table was built, 0 never

void flashDir(char* out, size_t n) {
    snprintf(out, n, "%s/%s", plat::fsBase(), BBS_SCREEN_DIR);
}

// lowerName: a directory entry lower cased (a card written on a laptop may
// say WELCOME.ANS, and FAT will play it), false when it is not a screen.
bool lowerName(const char* entry, char* low, size_t n) {
    size_t len = strlen(entry);
    if (len >= n) return false;
    for (size_t i = 0; i <= len; ++i) low[i] = static_cast<char>(tolower(static_cast<unsigned char>(entry[i])));
    return ziparc::validScreenName(low);
}

// place: the row and flavour of a screen file, adding the row when it is new.
bool place(Table& t, const char* low, uint8_t& r, uint8_t& c) {
    const char* dot = strchr(low, '.');
    for (c = 0; c < kExts && strcmp(dot, kExt[c]); ++c) {}
    if (c == kExts) return false;
    const size_t bl = static_cast<size_t>(dot - low);
    for (r = 0; r < t.rows; ++r)
        if (!strncmp(t.row[r].name, low, bl) && t.row[r].name[bl] == '\0') return true;
    if (t.rows >= kMaxNames) { t.full = true; return false; }
    r = t.rows++;
    t.row[r] = Row();
    memcpy(t.row[r].name, low, bl);
    t.row[r].name[bl] = '\0';
    return true;
}

void cardPath(char* out, size_t n, const char* file) {
    const char* d = sdScreensDir();
    snprintf(out, n, "%.90s/%s", d ? d : "", file);
}

void flashPath(char* out, size_t n, const char* file, const char* suffix = "") {
    snprintf(out, n, "%s/%s/%s%s", plat::fsBase(), BBS_SCREEN_DIR, file, suffix);
}

constexpr char kStockDir[] = ".stock";           // under the flash screens folder

void stockPath(char* out, size_t n, const char* file) {
    snprintf(out, n, "%s/%s/%s/%s", plat::fsBase(), BBS_SCREEN_DIR, kStockDir, file);
}

uint32_t blocksOf(uint32_t bytes) {
    uint32_t b = (bytes + BBS_FS_BLOCK - 1) / BBS_FS_BLOCK;
    return b ? b : 1;
}

// sameFile: byte for byte, read both. Only asked when the sizes agree. The
// runner's (the plan).
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

// --- the runner's side ------------------------------------------------------

struct CardWalk { Table* t; };

bool cardEntry(void* ctx, const char* name, bool dir, uint32_t size) {
    Table& t = *static_cast<CardWalk*>(ctx)->t;
    char low[16];
    uint8_t r, c;
    if (dir || !lowerName(name, low, sizeof(low)) || !place(t, low, r, c)) return true;
    Cell& k = t.row[r].cell[c];
    k.card = 1;
    k.from = F_OWN;                                  // until the manifest says it is the board's
    k.size = size > 0xFFFFFFu ? 0xFFFFFFu : size;
    return true;
}

bool manifestLine(void* ctx, const char* file, uint32_t hash) {
    Table& t = *static_cast<CardWalk*>(ctx)->t;
    char low[16];
    uint8_t r, c;
    if (!lowerName(file, low, sizeof(low))) return true;
    const char* dot = strchr(low, '.');
    for (c = 0; c < kExts && strcmp(dot, kExt[c]); ++c) {}
    if (c == kExts) return true;
    const size_t bl = static_cast<size_t>(dot - low);
    for (r = 0; r < t.rows; ++r)
        if (!strncmp(t.row[r].name, low, bl) && t.row[r].name[bl] == '\0') break;
    if (r == t.rows) return true;
    Cell& k = t.row[r].cell[c];
    if (!k.card || hash == sdSeededMine) return true;
    char path[112];
    cardPath(path, sizeof(path), low);
    if (sdFileHash(path) == hash) k.from = F_SEEDED;
    runner::breathe();
    return true;
}

// fill: the whole table. Flash first, in its own order, then the names only
// the card has; each folder read once.
void fill(Table& t) {
    // Reset field by field: a Table() temporary would be 3 KB on the
    // runner's stack.
    t.rows = 0;
    t.full = false;
    t.card = false;
    t.picks = 0;
    char dir[96];
    flashDir(dir, sizeof(dir));
    if (DIR* d = disk::dir(dir)) {
        for (struct dirent* e = readdir(d); e; e = readdir(d)) {
            char low[16], path[128];
            uint8_t r, c;
            if (!lowerName(e->d_name, low, sizeof(low)) || !place(t, low, r, c)) continue;
            snprintf(path, sizeof(path), "%.96s/%s", dir, low);          // low: a screen name, 15 at most
            struct stat st;
            if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
            Cell& k = t.row[r].cell[c];
            k.flash = 1;
            k.from  = F_FLASH;
            k.size  = static_cast<uint32_t>(st.st_size) > 0xFFFFFFu ? 0xFFFFFFu : static_cast<uint32_t>(st.st_size);
        }
        closedir(d);
    }
    if (!sdScreensDir()) return;
    CardWalk w{ &t };
    t.card = plat::sdList(BBS_SD_SCREEN_DIR, cardEntry, &w);
    sdSeededEach(manifestLine, &w);
    // The card copy callers get says the card's size; a flavour the card
    // does not have keeps flash's.
}

// --- SCREENS INSTALL's plan, on the runner ------------------------------------
//
// Every screen on the card that is not the board's own seeded copy and is not
// already in flash byte for byte is picked, in the card's own spelling; it
// is checked like a restore (a name, a size, no more than a restore takes)
// and against the room: the partition's free blocks, measured here and now,
// against what the set needs at its peak, counted in LittleFS blocks. Refused
// whole if it would not fit, never half installed.

struct PlanWalk { Table* t; int32_t need, peak; };

bool planEntry(void* ctx, const char* name, bool dir, uint32_t size) {
    PlanWalk& p = *static_cast<PlanWalk*>(ctx);
    Table& t = *p.t;
    char low[16];
    if (dir || !lowerName(name, low, sizeof(low))) return true;
    const char* dot = strchr(low, '.');
    uint8_t c = 0, r = 0;
    for (; c < kExts && strcmp(dot, kExt[c]); ++c) {}
    const size_t bl = static_cast<size_t>(dot - low);
    for (; r < t.rows; ++r)
        if (!strncmp(t.row[r].name, low, bl) && t.row[r].name[bl] == '\0') break;
    if (c == kExts || r == t.rows) return true;
    const Cell& k = t.row[r].cell[c];
    if (k.from == F_SEEDED) { ++g_b.seeded; return true; }      // the board's own copy
    char live[112], card[112], stockp[112];
    flashPath(live, sizeof(live), low);
    cardPath(card, sizeof(card), name);
    struct stat fs, ss;
    const bool inFlash = stat(live, &fs) == 0 && S_ISREG(fs.st_mode);
    if (inFlash && static_cast<uint32_t>(fs.st_size) == size && sameFile(card, live)) {
        ++g_b.same;
        return true;
    }
    if (!size && !g_b.why[0])
        snprintf(g_b.why, sizeof(g_b.why), "%s is empty.", low);                   // SI-empty
    if (size > BBS_ZIP_FILE_MAX && !g_b.why[0])
        snprintf(g_b.why, sizeof(g_b.why), "%s is too big: %u KB, a screen may be %u.",  // SI-big
                 low, static_cast<unsigned>((size + 1023) / 1024),
                 static_cast<unsigned>(BBS_ZIP_FILE_MAX / 1024));
    if (t.picks >= BBS_ZIP_MAX_FILES) {
        if (!g_b.why[0]) snprintf(g_b.why, sizeof(g_b.why), "More than %u screens to install.",  // SI-many
                                  static_cast<unsigned>(BBS_ZIP_MAX_FILES));
        return true;
    }
    snprintf(t.pickName[t.picks], sizeof(t.pickName[0]), "%s", name);
    t.pickRow[t.picks] = r;
    t.pickCol[t.picks] = c;
    ++t.picks;
    g_b.bytes += size;
    // What the set adds once it is in. The stock copy it replaces moves
    // aside and keeps its blocks; a screen installed before is replaced, and
    // its blocks come back. The peak is while this one sits beside the one
    // it replaces: everything in so far plus its new blocks, before the old
    // ones go. The largest of those over the set, in the order it goes in.
    stockPath(stockp, sizeof(stockp), low);
    const bool hadStock = stat(stockp, &ss) == 0;
    const int32_t newB = static_cast<int32_t>(blocksOf(size));
    const int32_t oldB = (hadStock && inFlash) ? static_cast<int32_t>(blocksOf(static_cast<uint32_t>(fs.st_size))) : 0;
    if (p.need + newB > p.peak) p.peak = p.need + newB;
    p.need += newB - oldB;
    runner::breathe();
    return true;
}

void plan(Table& t) {
    if (t.full && !g_b.why[0])
        snprintf(g_b.why, sizeof(g_b.why), "More than %u screens in the card's folder.",   // SI-folder
                 static_cast<unsigned>(kMaxNames));
    if (!t.card) {
        if (!g_b.why[0]) snprintf(g_b.why, sizeof(g_b.why), "The card went. Nothing was installed.");
        return;
    }
    PlanWalk p{ &t, 0, 0 };
    plat::sdList(BBS_SD_SCREEN_DIR, planEntry, &p);
    if (g_b.why[0] || !t.picks) return;
    if (g_b.bytes > BBS_ZIP_TOTAL_MAX) {
        snprintf(g_b.why, sizeof(g_b.why), "%u KB of screens, more than the %u KB a restore takes.",   // SI-total
                 static_cast<unsigned>((g_b.bytes + 1023) / 1024),
                 static_cast<unsigned>(BBS_ZIP_TOTAL_MAX / 1024));
        return;
    }
    // The room, measured now: this is the one figure that must not be a
    // kept one, and on the runner a walk of the partition is nobody's wait.
    uint64_t total = 0, used = 0;
    if (!plat::measure(plat::PART_SCREENS, total, used)) {
        snprintf(g_b.why, sizeof(g_b.why), "Could not read the flash's free space.");         // SI-noinfo
        return;
    }
    // The peak of the running total, plus the .stock folder and a spare block.
    g_b.freeB = static_cast<int32_t>((total > used ? total - used : 0) / BBS_FS_BLOCK);
    g_b.want  = (p.peak > 0 ? p.peak : 0) + 2;
    if (g_b.want > g_b.freeB) {
        snprintf(g_b.why, sizeof(g_b.why), "Not enough room in flash: %u KB needed, %u KB free.",   // SI-room
                 static_cast<unsigned>(g_b.want * (BBS_FS_BLOCK / 1024)),
                 static_cast<unsigned>(g_b.freeB * (BBS_FS_BLOCK / 1024)));
        return;
    }
    char dir[112];
    snprintf(dir, sizeof(dir), "%s/%s/%s", plat::fsBase(), BBS_SCREEN_DIR, kStockDir);
    mkdir(dir, 0755);                                  // there already is fine
    g_b.ready = true;
}

void buildWork(runner::Job&) {
    Table& t = *g_b.t;
    fill(t);
    if (g_b.install) plan(t);
}

// --- the loop's side ----------------------------------------------------------

// post: a build (and a plan) of g_tab. False when it is already out, or there
// is no memory for the table, or the runner would not take it.
bool postBuild(bool install) {
    if (!runner::idle(g_b.job)) return false;
    if (!g_tab) {
        g_tab = static_cast<Table*>(malloc(sizeof(Table)));
        if (!g_tab) return false;
        new (g_tab) Table();
        g_tabAt = 0;
    }
    g_b.t       = g_tab;
    g_b.install = install;
    g_b.same = g_b.seeded = 0;
    g_b.bytes = 0;
    g_b.want = g_b.freeB = 0;
    g_b.ready = false;
    g_b.why[0] = '\0';
    g_b.job.work = buildWork;
    g_b.job.name = install ? "screens install plan" : "screens list";
    return runner::post(g_b.job);
}

// collect: a finished build becomes the table. Once a pass, from serviceScreens.
void collect(uint32_t now) {
    if (!runner::done(g_b.job)) return;
    g_tabAt = now ? now : 1;
    runner::collect(g_b.job);
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

} // namespace

// ---------------------------------------------------------------------------
// The table's readers (1.1.2): who is drawing it or waiting on it. The table
// is not rebuilt under one, and it goes back to the heap once there is none.
// ---------------------------------------------------------------------------
uint8_t Bbs::screensReaders() const {
    uint8_t n = 0;
    for (const Session* o : all_) {
        if (o->st == SState::Free) continue;
        if ((o->st == SState::List || o->st == SState::More) && o->list == ListKind::Screens) ++n;
        else if (o->st == SState::Waiting && o->waitFor == WaitFor::Screens) ++n;
    }
    return n;
}

void Bbs::screensTableRelease() {
    if (!g_tab || !runner::idle(g_b.job) || screensBusy() || screensReaders()) return;
    g_tab->~Table();
    free(g_tab);
    g_tab   = nullptr;
    g_tabAt = 0;
}

bool Bbs::screensTableReady(const Session&) const {
    return g_tab && g_tabAt && runner::idle(g_b.job);
}

void Bbs::screensTableGone(Session& s) {
    s.term.color(s.tl, Color::Yellow);
    s.term.text(s.tl, "The list could not be made. Try SCREENS again.");      // SC-gone
    s.term.nl(s.tl);
}

// ---------------------------------------------------------------------------
// cmdScreens: SCREENS and SCREENS VIEW
// ---------------------------------------------------------------------------
void Bbs::cmdScreens(Session& s, const char* arg) {
    const char* p = arg;
    char first[8], name[24], flag[8];
    word(p, first, sizeof(first));
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
    if (!first[0]) {
        // The table, built on the runner: a spinner while it is, then the
        // rows. A table another staff member is reading, or one a moment
        // old, is used as it stands rather than built under them.
        if (screensBusy()) {
            say(Color::Yellow, "Screens are being installed. SCREENS after.");   // SC-installing
            prompt(s);
            return;
        }
        const bool fresh = g_tab && g_tabAt && plat::millis() - g_tabAt < 5000u;
        if (runner::pending(g_b.job) || (g_tab && g_tabAt && (fresh || screensReaders()))) {
            startWait(s, WaitFor::Screens, 0, "Reading the screens");
            return;
        }
        if (!postBuild(false)) {
            say(Color::Yellow, "The board could not start the list. Try again in a moment.");   // SC-nostart
            screensTableRelease();
            prompt(s);
            return;
        }
        startWait(s, WaitFor::Screens, 0, "Reading the screens");
        return;
    }
    word(p, name, sizeof(name));
    word(p, flag, sizeof(flag));
    bool flash = !strcmp(flag, "flash");
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
// rowScreens: one row of SCREENS, out of the table. 40 columns and 80 each
// get a layout:
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
    if (!g_tab) return false;                     // given back under us: end it
    const Table& tb = *g_tab;
    // The wide row is 76 columns (a name of 10 and three cells of 22), so a
    // terminal narrower than that gets the 39 column one, not a wrapped wide
    // one: 61 to 76 columns is a real NAWS size.
    bool    wide = rowWidth(s) >= 76;
    if (row == 0) {
        rowTitle(s, "Screens", tb.card ? "card and flash" : "flash only");     // SC-title
        return true;
    }
    if (row == 1) {
        // Each flavour over its sizes, which are right-aligned in the cell.
        char head[80];
        if (wide) snprintf(head, sizeof(head), "%-10s%6s%16s%6s%16s%6s", "name", kExt[0], "", kExt[1], "", kExt[2]);
        else      snprintf(head, sizeof(head), "%-9s%5s%5s%5s%5s%5s", "name", kExt[0], "", kExt[1], "", kExt[2]);
        rowText(s, Color::Grey, head);
        return true;
    }
    // A name with nothing in the three columns shown (a .p40 only) is a
    // row of dashes, as it always was.
    const uint8_t total = tb.rows;
    uint8_t k = static_cast<uint8_t>(row - 2);
    if (k < total) {
        const Row& r = tb.row[k];
        char cell[32];
        uint8_t col = 0;
        snprintf(cell, sizeof(cell), wide ? "%-10s" : "%-9s", r.name);
        rowSeg(s, Color::White, cell, col);
        for (uint8_t i = 0; i < kCols; ++i) {
            const Cell& c = r.cell[i];
            const From w = static_cast<From>(c.from);
            bool last = i == kCols - 1;
            static const char* const kWide[]  = { "", "flash", "card, seeded", "card, own" };
            static const char  kNarrow[]      = { ' ', 'F', 'C', 'O' };
            Color cc = w == F_NONE   ? Color::DarkGrey
                     : w == F_FLASH  ? Color::Grey
                     : w == F_SEEDED ? Color::Cyan
                                     : Color::Yellow;
            if (w == F_NONE) snprintf(cell, sizeof(cell), wide ? "%6s%16s" : "%5s%5s", "-", "");
            else if (wide)   snprintf(cell, sizeof(cell), "%6u %-15s", static_cast<unsigned>(c.size), kWide[w]);
            else             snprintf(cell, sizeof(cell), "%5u %c   ", static_cast<unsigned>(c.size), kNarrow[w]);
            if (last) {                                  // nothing past the last column
                size_t e = strlen(cell);
                while (e && cell[e - 1] == ' ') cell[--e] = '\0';
            }
            rowSeg(s, cc, cell, col);
        }
        rowEnd(s, col);
        return true;
    }
    switch (k - total) {
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
//   - which, and whether it fits: the plan, on the background runner since
//     1.1.2 (see plan() above). It took one card entry a pass on the loop,
//     walking the card's folder from the top for each, and ended with a walk
//     of the partition for its free space and a mkdir: 491 ms in one pass on
//     the bench.
//   - each screen written beside the live one under "<name>.new", then put
//     live by a rename within the partition. The stock copy it replaces is
//     moved aside, not deleted, to screens/.stock/<name>; a screen flash did
//     not have gets an empty marker there instead. Nothing live is removed
//     to make room for a rename: a rename that fails puts the stock copy
//     back where it was.
//   - callers reading one of flash's screens are let go of it first, as a
//     restore does (esp_littlefs will not rename over an open file). The
//     renames stay on the loop, in the same pass as endScreens, so no caller
//     can open the screen between the two.
//   - the card's manifest marks the installed ones as the sysop's, so the
//     next firmware's stock screens never overwrite them on the card: once,
//     for all of them, on the runner, at the end (it was a manifest rewrite
//     on the loop for each screen).
//
// SCREENS INSTALL STOCK walks screens/.stock and puts it back: a stock copy
// over the installed one, an empty marker's screen removed. A firmware update
// (the installer, uploadfs) rewrites the partition whole and does the same.
//
// The copy is one step a pass, never a burst (Rule no. 1): 512 bytes, or one
// rename. A flash erase stops both cores whoever asks for it, so the most a
// pass can hold the board is one block's erase. The job is board-wide state,
// not a Session's: a sysop who hangs up half way leaves it to finish, because
// each screen goes live whole or not at all.
// ===========================================================================
namespace {

constexpr uint16_t kChunk  = 512;                // bytes a pass

struct Install {
    enum Ph : uint8_t { Idle, Plan, Copy, Mark, Stock } ph = Idle;
    uint8_t  who    = 0xFF;      // the sysop's session id, while they watch
    uint16_t call   = 0;         // and their call, so a new caller on that node is not them
    uint8_t  idx    = 0;         // Copy: the pick being put in
    uint8_t  done = 0, failed = 0, back = 0, gone = 0;
    uint32_t copied = 0;         // Copy: bytes of this screen so far
    FILE*    in     = nullptr;
    FILE*    out    = nullptr;
    char     lower[16] = {};     // this screen's flash name
    char     why[64]   = {};
    uint32_t spinAt = 0;
    uint8_t  spin   = 0;
};
Install g_in;

// The card's manifest, marked for every screen that went in: one rewrite, on
// the runner, once the copy is over (sdSeededMark). Its names are the
// table's picks that are done.
struct MarkJob {
    runner::Job job;
    Table*      t = nullptr;
};
MarkJob g_mark;

const char* markNth(void* ctx, uint8_t i) {
    Table& t = *static_cast<Table*>(ctx);
    static char low[16];
    for (uint8_t p = 0, n = 0; p < t.picks; ++p) {
        const Cell& c = t.row[t.pickRow[p]].cell[t.pickCol[p]];
        if (!c.done) continue;
        if (n++ == i) {
            snprintf(low, sizeof(low), "%s%s", t.row[t.pickRow[p]].name, kExt[t.pickCol[p]]);
            return low;
        }
    }
    return nullptr;
}

void markWork(runner::Job&) {
    sdSeededMark(markNth, g_mark.t);
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
// touched, and the ones already in stay in. (The plan and the mark run on
// the runner, and SD UNMOUNT waits for the runner.)
void Bbs::screensDrop() {
    if (g_in.ph != Install::Copy) return;
    closeFiles();
    if (g_in.lower[0]) {
        char neu[112];
        flashPath(neu, sizeof(neu), g_in.lower, ".new");
        remove(neu);
    }
    plat::log("screens: install stopped, the card was unmounted (%u in)", static_cast<unsigned>(g_in.done));
    snprintf(g_in.why, sizeof(g_in.why), "The card went: %u installed, the rest not.",
             static_cast<unsigned>(g_in.done));
    g_in.ph = Install::Idle;
    for (Session* o : all_) {
        if (o->id != g_in.who || o->call != g_in.call || o->st != SState::CardJob || o->fd < 0) continue;
        o->term.nl(o->tl);
        o->term.color(o->tl, Color::Yellow);
        o->term.text(o->tl, g_in.why);
        o->term.nl(o->tl);
        prompt(*o);
    }
    space::stale(plat::PART_SCREENS);
    screensTableRelease();
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
    if (!stock && (screensReaders() || !runner::idle(g_b.job))) {
        say(Color::Yellow, "The screens are being listed. Try again in a moment.");   // SI-listing
        prompt(s);
        return;
    }
    g_in = Install();
    g_in.who    = s.id;
    g_in.call   = s.call;
    g_in.spinAt = plat::millis();
    if (!stock) {
        if (!postBuild(true)) {
            say(Color::Yellow, "The board could not start it. Try again in a moment.");   // SI-nostart
            screensTableRelease();
            prompt(s);
            return;
        }
        g_in.ph = Install::Plan;
    } else {
        g_in.ph = Install::Stock;
    }
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
    collect(now);                                        // a table built for SCREENS
    if (g_in.ph == Install::Idle) return;
    Session* s = nullptr;
    for (Session* o : all_)
        if (o->id == g_in.who && o->call == g_in.call && o->st == SState::CardJob && o->fd >= 0 && o->loggedIn) s = o;

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
        // Only the screens partition changed (1.1.2): its figure is measured
        // again at the next staff login or FORCE, never here.
        space::stale(plat::PART_SCREENS);
        screensTableRelease();
    };
    char msg[112], more[112];

    // --- Plan: the runner's; the answer is read once it is DONE ---------------
    if (g_in.ph == Install::Plan) {
        if (!g_tabAt || !runner::idle(g_b.job)) return;
        if (g_b.why[0]) {
            finish(Color::LightRed, g_b.why, strstr(g_b.why, "Nothing was") ? nullptr
                                                                             : "Nothing was installed.");   // SI-refused
            return;
        }
        if (!g_tab->picks) {
            finish(Color::LightGreen, "Nothing to install: flash has every screen of yours already.",   // SI-none
                   nullptr);
            return;
        }
        if (!g_b.ready) {
            finish(Color::LightRed, "The install could not be planned.", "Nothing was installed.");
            return;
        }
        g_in.idx = 0;
        g_in.ph  = Install::Copy;
        plat::log("screens: installing %u, %u bytes, %d blocks of %d free",
                  static_cast<unsigned>(g_tab->picks), static_cast<unsigned>(g_b.bytes),
                  static_cast<int>(g_b.want), static_cast<int>(g_b.freeB));
        return;
    }

    // --- Copy: 512 bytes a pass, then the rename ----------------------------
    if (g_in.ph == Install::Copy) {
        Table& tb = *g_tab;
        char cardp[112], live[112], neu[112], stockp[112];
        if (!g_in.in) {
            if (g_in.idx >= tb.picks) {
                // The manifest, once, on the runner, for every one that went in.
                g_mark.t        = g_tab;
                g_mark.job.work = markWork;
                g_mark.job.name = "screens mark";
                if (g_in.done && runner::post(g_mark.job)) { g_in.ph = Install::Mark; return; }
                g_in.ph = Install::Mark;                   // nothing to mark, or it would not post
                return;
            }
            const uint8_t p = g_in.idx;
            if (!sdScreensDir()) { ++g_in.failed; ++g_in.idx; return; }
            snprintf(g_in.lower, sizeof(g_in.lower), "%s%s", tb.row[tb.pickRow[p]].name, kExt[tb.pickCol[p]]);
            cardPath(cardp, sizeof(cardp), tb.pickName[p]);
            flashPath(neu, sizeof(neu), g_in.lower, ".new");
            // Checked again as it goes in: the card is the sysop's to change
            // while this runs. A size, which on FAT is one look in a folder
            // already read; the hash was the plan's.
            struct stat cs;
            if (stat(cardp, &cs) != 0 || !S_ISREG(cs.st_mode) || cs.st_size <= 0 || cs.st_size > BBS_ZIP_FILE_MAX) {
                ++g_in.failed;
                ++g_in.idx;
                return;
            }
            g_in.copied = 0;
            g_in.in  = disk::open(cardp, "rb");
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
        // has grown past what the check allowed since it was checked is not
        // put in.
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
        const uint8_t p = g_in.idx++;
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
            tb.row[tb.pickRow[p]].cell[tb.pickCol[p]].done = 1;
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

    // --- Mark: the manifest on the runner, then the answer ------------------
    if (g_in.ph == Install::Mark) {
        if (runner::pending(g_mark.job)) return;
        if (runner::done(g_mark.job)) runner::collect(g_mark.job);
        snprintf(msg, sizeof(msg), "Installed %u screen%s in flash.",                    // SI-done
                 static_cast<unsigned>(g_in.done), g_in.done == 1 ? "" : "s");
        if (g_in.failed)
            snprintf(more, sizeof(more), "%u could not be put in. SCREENS INSTALL STOCK undoes it.",  // SI-partial
                     static_cast<unsigned>(g_in.failed));
        else
            snprintf(more, sizeof(more), "They play from flash with the card out. SCREENS INSTALL STOCK undoes it.");  // SI-done-2
        plat::log("screens: installed %u, %u failed, %u already in flash, %u seeded",
                  static_cast<unsigned>(g_in.done), static_cast<unsigned>(g_in.failed),
                  static_cast<unsigned>(g_b.same), static_cast<unsigned>(g_b.seeded));
        finish(g_in.failed ? Color::Yellow : Color::LightGreen, msg, more);
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
