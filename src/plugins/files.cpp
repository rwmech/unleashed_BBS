/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/files.cpp
 * Module:       Plugin / file areas
 *
 * Purpose:      Publishes folders on the SD card as file areas a caller can
 *                  browse. An area is a folder the sysop mounts under a
 *                  human name: the config maps a path on the card to a name,
 *                  so /bbs/c64stuff becomes "C64 Downloads". A folder with
 *                  no config entry is not an area, which is what lets a
 *                  sysop keep their own files on the same card.
 *
 *               Descriptions live in FILES.BBS inside each folder, one line
 *                  per file, the way every BBS did it. A sysop can edit that
 *                  on a laptop with the card in hand, which is the whole
 *                  reason the card is FAT32 rather than LittleFS.
 *
 *               No transfer here. Browsing is useful on its own and XMODEM
 *                  is its own piece of work; uploads and the move interface
 *                  wait for it, because an upload area on a board with no
 *                  transfer protocol cannot be used and so cannot be tested.
 *
 * Config:       [plugin:files]
 *                  enabled  yes
 *                  read     all        who may browse
 *                  write    staff      who may write descriptions
 *                  area1..8 <path> | <name>
 *
 * Commands:     FILES, FILES n, DESC
 *
 * Depends on:   the sd plugin for a mounted card (PF_SD)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     CLAUDE.md, NEXT.md
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

#include "../core/plugin.h"
#include "../core/bbs.h"
#include "../core/bbs_util.h"
#include "../platform/platform.h"
#include "../config.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>

using bbsu::ieq;

namespace {

const char* const kName = "files";

// Eight areas. The limit is the CONFIG form, which holds 16 fields and
// spends four on the core keys; eight areas plus those is twelve and leaves
// room. A board wanting more than eight has outgrown a form anyway.
// Eight the sysop configures, then two the board provides. The built-ins sit
// at fixed numbers ABOVE the configured ones so a number always means the
// same area: giving them the first free slot would move them every time
// somebody added a folder.
constexpr uint8_t kCfgAreas    = 8;
constexpr uint8_t kMaxAreas    = kCfgAreas + 2;
constexpr uint8_t kAreaScreens = kCfgAreas;       // shows as 9
constexpr uint8_t kAreaLogs    = kCfgAreas + 1;   // shows as 10
constexpr uint8_t kPathMax  = 48;
constexpr uint8_t kNameMax  = 24;
constexpr uint8_t kDescMax  = 48;

// An area's own read and write levels, which are the plugin's unless the
// area says otherwise. Deferring these to the upload phase was a mistake:
// the first thing anybody wants is a staff-only area, and that has nothing
// to do with uploading. Same words and same ladder as everywhere else, so a
// sysop learns the vocabulary once.
struct Area {
    char      path[kPathMax + 1] = {};   // relative to the card root
    char      name[kNameMax + 1] = {};   // what a caller sees
    PlugLevel read  = PlugLevel::Nobody; // Nobody here means "use the plugin's"
    PlugLevel write = PlugLevel::Nobody;
};

Area    g_area[kMaxAreas];
uint8_t g_areas  = 0;

// Where the highlight is sitting, as an index into the list of areas THIS
// caller can see, not an area number. Two callers with different levels see
// different menus, so an area number would put the bar on the wrong row.
uint8_t g_sel[BBS_MAX_NODES + 2] = {};
uint8_t g_index  = 0;

// Which area a caller is in, 0xFF for the area menu. Per session, because
// two callers may be in two areas at once, and it is cleared on logoff so
// the next caller on that node does not inherit it: sessions come from a
// static pool.
uint8_t g_at[BBS_MAX_NODES + 2] = {};

// FILES is a place, not a command. The plugin owns the session while a
// caller is in it, so keys come here and the shell prompt is not drawn
// underneath. Q or ESC leaves. Modelled on the chat room, which works the
// same way and for the same reason.
enum class Where : uint8_t { Out, Menu, Area };
Where   g_where[BBS_MAX_NODES + 2] = {};

uint8_t slotOf(const Session& s) { return s.id <= BBS_MAX_NODES + 1 ? s.id : 0; }

// mayRead / mayWrite: this caller against this area. An area that set no
// level of its own falls back to the plugin's, which is what the levels on
// the [plugin:files] section have always meant.
bool mayRead(const Session& s, uint8_t i) {
    if (i >= g_areas || !g_area[i].path[0]) return false;
    PlugLevel lv = g_area[i].read;
    if (lv == PlugLevel::Nobody) lv = plugins::levelFor(g_index, 0);
    return plugins::mayUse(s, lv);
}

bool mayWrite(const Session& s, uint8_t i) {
    if (i >= g_areas || !g_area[i].path[0]) return false;
    PlugLevel lv = g_area[i].write;
    if (lv == PlugLevel::Nobody) lv = plugins::levelFor(g_index, 1);
    return plugins::mayUse(s, lv);
}

// ---------------------------------------------------------------------------
// areaPath: the full path of an area, or false when the card is not there.
// ---------------------------------------------------------------------------
bool areaPath(uint8_t i, char* out, size_t n) {
    if (i >= g_areas || !plat::sdBase()[0]) return false;
    snprintf(out, n, "%s/%s", plat::sdBase(), g_area[i].path);
    return true;
}

// ---------------------------------------------------------------------------
// makeAreaDir: create an area's folder on the card, parents and all.
//
// So a sysop can set an area up from the board itself: type a path into
// CONFIG, save, and the folder is there. Without this, configuring an area
// meant pulling the card, finding a PC, making the folder and putting the
// card back, which is a strange thing to require of somebody who is already
// logged in as the sysop of the machine that owns the card.
//
// Only ever under the card's own mount point, and readKey has already
// refused any path with ".." in it, so this cannot reach the rest of the
// filesystem. A typo makes a junk folder rather than an area that silently
// does not work, and of those two a junk folder is much easier to notice.
// ---------------------------------------------------------------------------
bool makeAreaDir(const char* rel) {
    const char* base = plat::sdBase();
    if (!base[0] || !rel || !*rel) return false;

    char full[160];
    int n = snprintf(full, sizeof(full), "%s/%s", base, rel);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(full)) return false;

    // Walk the path making each component, because "pub/c64" needs "pub"
    // first and mkdir does not do that for you.
    size_t baseLen = strlen(base);
    for (char* q = full + baseLen + 1; *q; ++q) {
        if (*q != '/') continue;
        *q = '\0';
        mkdir(full, 0755);
        *q = '/';
    }
    if (mkdir(full, 0755) == 0) {
        plat::log("files: created %s", full);
        return true;
    }
    struct stat st;                       // already there is success
    return stat(full, &st) == 0 && S_ISDIR(st.st_mode);
}

// ---------------------------------------------------------------------------
// safeName: a filename a caller typed, with anything that could walk out of
// the area removed. A path separator or a pair of dots is refused outright
// rather than stripped: a caller who typed "../users.txt" gets told no, and
// quietly turning it into something else would hide what they tried.
// ---------------------------------------------------------------------------
bool safeName(const char* f) {
    if (!f || !*f || strlen(f) > 32) return false;
    if (strchr(f, '/') || strchr(f, '\\') || strstr(f, "..")) return false;
    for (const char* p = f; *p; ++p)
        if (static_cast<unsigned char>(*p) < 0x20) return false;
    return true;
}

// ---------------------------------------------------------------------------
// findDesc: the description for one file, from FILES.BBS in its folder.
//
// Read on demand rather than held in RAM. A folder of two hundred files
// would be a 12 KB table on a board whose heap is measured in tens of KB,
// and the list is drawn one row at a time anyway, so the file is walked once
// per row. That is more reads than a cache would need and it is the right
// trade here: no allocation, no staleness, and a sysop who edits FILES.BBS
// on a laptop sees the change on the next listing rather than after a
// reboot.
// ---------------------------------------------------------------------------
void findDesc(const char* dir, const char* file, char* out, size_t n) {
    out[0] = '\0';
    char p[160];
    snprintf(p, sizeof(p), "%s/%s", dir, BBS_FILES_DESC);
    FILE* f = fopen(p, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char* nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        char* sp = line;
        while (*sp && *sp != ' ' && *sp != '\t') ++sp;
        if (!*sp) continue;
        *sp++ = '\0';
        while (*sp == ' ' || *sp == '\t') ++sp;
        if (ieq(line, file)) { snprintf(out, n, "%.*s", static_cast<int>(n) - 1, sp); break; }
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// setDesc: write one description back, through a temp file and a rename.
//
// FAT is not power-fail safe and this is the only thing the board writes to
// the card, so it is worth doing properly: build the whole new file beside
// the old one, flush it to the card, then rename over the top. A board that
// loses power mid-write loses the temp file and keeps the descriptions.
// ---------------------------------------------------------------------------
bool setDesc(const char* dir, const char* file, const char* text) {
    char cur[160], tmp[176];
    snprintf(cur, sizeof(cur), "%s/%s", dir, BBS_FILES_DESC);
    snprintf(tmp, sizeof(tmp), "%s/%s.tmp", dir, BBS_FILES_DESC);

    FILE* out = fopen(tmp, "w");
    if (!out) return false;
    bool wrote = false;

    FILE* in = fopen(cur, "r");
    if (in) {
        char line[128];
        while (fgets(line, sizeof(line), in)) {
            char keep[128];
            snprintf(keep, sizeof(keep), "%s", line);
            char* nl = strpbrk(line, "\r\n");
            if (nl) *nl = '\0';
            char* sp = line;
            while (*sp && *sp != ' ' && *sp != '\t') ++sp;
            char had = *sp;
            *sp = '\0';
            if (ieq(line, file)) {                   // replaced below
                if (*text) { fprintf(out, "%s %s\n", file, text); wrote = true; }
                else       { wrote = true; }          // empty text deletes the row
                continue;
            }
            *sp = had;
            fputs(keep, out);
        }
        fclose(in);
    }
    if (!wrote && *text) fprintf(out, "%s %s\n", file, text);

    fflush(out);
    fclose(out);
    remove(cur);                                     // FAT rename will not replace
    return rename(tmp, cur) == 0;
}

// ---------------------------------------------------------------------------
// readKey: areaN = <path> | <name>
//
// One key per area rather than a section each, because a plugin gets one
// config section and the core has no notion of a section a plugin owns
// several of. The pipe is what a sysop will actually type; a path with a
// pipe in it is not a path anybody has.
// ---------------------------------------------------------------------------
void readKey(void* ctx, const char* key, const char* value) {
    (void)ctx;
    if (strncmp(key, "area", 4) != 0) return;
    long n = strtol(key + 4, nullptr, 10);
    if (n < 1 || n > kCfgAreas) return;

    // <path> | <name> | <read> | <write>, the last two optional. Split on
    // every bar rather than the first, so a sysop can say
    //   area1 = admin/screens | Screens | staff | sysop
    char parts[4][kPathMax + kNameMax + 4] = {};
    uint8_t np = 0;
    const char* p = value;
    while (np < 4) {
        const char* bar = strchr(p, '|');
        size_t len = bar ? static_cast<size_t>(bar - p) : strlen(p);
        while (len && (p[len - 1] == ' ' || p[len - 1] == '\t')) --len;
        while (len && (*p == ' ' || *p == '\t')) { ++p; --len; }
        snprintf(parts[np], sizeof(parts[0]), "%.*s", static_cast<int>(len), p);
        ++np;
        if (!bar) break;
        p = bar + 1;
    }

    // Accept the path however a sysop naturally writes it. SD prints the
    // screens folder as /sd/screens, so that is what somebody types, and
    // treating it as relative to the card turned it into /sd//sd/screens
    // and silently pointed the area at nothing. All four of these mean the
    // same folder:  /sd/screens   sd/screens   /screens   screens
    const char* rel = parts[0];
    while (*rel == '/') ++rel;
    const char* mount = plat::sdBase()[0] ? plat::sdBase() + 1 : "sd";   // past its slash
    size_t mlen = strlen(mount);
    if (!strncmp(rel, mount, mlen) && (rel[mlen] == '/' || !rel[mlen])) {
        rel += mlen;
        while (*rel == '/') ++rel;
    }

    Area a;
    snprintf(a.path, sizeof(a.path), "%.*s", kPathMax, rel);
    snprintf(a.name, sizeof(a.name), "%.*s", kNameMax, np > 1 && parts[1][0] ? parts[1] : parts[0]);
    // A level the ladder does not recognise is refused rather than guessed
    // at, and the area keeps the plugin's level. Guessing here would mean a
    // typo quietly opening a staff area to everybody.
    if (np > 2 && parts[2][0] && !plugins::levelFromText(parts[2], a.read))
        plat::log("files: area%ld read level '%s' is not a level, using the plugin's", n, parts[2]);
    if (np > 3 && parts[3][0] && !plugins::levelFromText(parts[3], a.write))
        plat::log("files: area%ld write level '%s' is not a level, using the plugin's", n, parts[3]);
    if (!a.path[0]) return;
    if (strstr(a.path, "..")) {                      // a sysop typo, not an attack
        plat::log("files: area%ld path has .. in it, ignored", n);
        return;
    }
    if (!a.name[0]) snprintf(a.name, sizeof(a.name), "%.*s", kNameMax, a.path);

    uint8_t i = static_cast<uint8_t>(n - 1);
    g_area[i] = a;
    if (i + 1 > g_areas) g_areas = static_cast<uint8_t>(i + 1);
}

bool start(Bbs& bbs) {
    (void)bbs;
    g_index = plugins::indexOf(kName);
    g_areas = 0;
    for (uint8_t i = 0; i < kMaxAreas; ++i) g_area[i] = Area();
    for (uint8_t i = 0; i < BBS_MAX_NODES + 2; ++i) g_at[i] = 0xFF;
    plugins::forEachKey(g_index, readKey, nullptr);

    // The two the board provides, so a fresh card is not an empty room.
    // Screens is the folder the screen player already reads its overrides
    // from and Logs is where the caller log is mirrored, so both are things
    // a sysop wants at hand and neither is anybody else's business: staff
    // may look, the sysop may change.
    //
    // Logs only works because the log is mirrored to the card. The ring on
    // the internal partition is not reachable from a file area and should
    // not be.
    Area& scr = g_area[kAreaScreens];
    snprintf(scr.path, sizeof(scr.path), "%s", BBS_SD_SCREEN_DIR);
    snprintf(scr.name, sizeof(scr.name), "%s", "Screens");
    scr.read  = PlugLevel::Staff;
    scr.write = PlugLevel::Sysop;

    Area& lg = g_area[kAreaLogs];
    snprintf(lg.path, sizeof(lg.path), "%s", BBS_SD_LOG_DIR);
    snprintf(lg.name, sizeof(lg.name), "%s", "Logs");
    lg.read  = PlugLevel::Staff;
    lg.write = PlugLevel::Sysop;

    g_areas = kMaxAreas;

    // Make each area's folder if it is not there yet. This runs at start,
    // and a CONFIG save restarts the plugins, so adding an area in CONFIG
    // creates its folder on the way back up with nothing else to do.
    uint8_t live = 0, made = 0;
    for (uint8_t i = 0; i < g_areas; ++i) {
        if (!g_area[i].path[0]) continue;
        ++live;
        char full[160];
        snprintf(full, sizeof(full), "%s/%s", plat::sdBase(), g_area[i].path);
        struct stat st;
        bool had = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        if (!had && makeAreaDir(g_area[i].path)) ++made;
    }
    plat::log("files: %u area%s, %u folder%s created",
              live, live == 1 ? "" : "s", made, made == 1 ? "" : "s");
    return true;
}

void stop() {}

// A caller leaving takes their place in an area with them. Sessions are a
// static pool, so anything not cleared here is inherited by the next caller
// on that node.
void onLogoff(Session& s) {
    g_at[slotOf(s)]    = 0xFF;
    g_where[slotOf(s)] = Where::Out;
    g_sel[slotOf(s)]   = 0;
}

// ---------------------------------------------------------------------------
// areaMenu: the areas, numbered, in as many columns as the terminal has room
// for.
//
// One column at 40, two at 80, and the width of a column comes from the
// longest name rather than from a guess, so a board with short names gets
// more of them on a line. A C64 caller and a PuTTY caller should both get
// something that fits; giving both the narrow case is the mistake already
// made once with the description column.
//
// Areas the caller may not read are not here, and the numbers are still the
// config slots, so FILES 4 means area 4 to everybody.
// ---------------------------------------------------------------------------
// canPoint: this terminal can draw a highlight bar and send cursor keys.
// Plain ASCII can do neither, so it gets the numbers and nothing else,
// which is the same split the forms already make.
bool canPoint(Session& s) { return s.term.isAnsi() || s.term.isPet(); }

// visibleAreas: the areas this caller may see, in menu order, and the number
// of columns the menu will lay them out in.
//
// Drawing and the cursor keys both go through here on purpose. When they
// each worked it out for themselves the highlight could sit on a different
// area than the one that opened, and that is a bug a caller reads as the
// board being broken rather than as an off-by-one.
uint8_t visibleAreas(Session& s, uint8_t* out, uint8_t* perRow, uint8_t* widestOut) {
    uint8_t n = 0, widest = 0;
    for (uint8_t i = 0; i < g_areas; ++i) {
        if (!g_area[i].path[0] || !mayRead(s, i)) continue;
        out[n++] = i;
        uint8_t w = static_cast<uint8_t>(strlen(g_area[i].name));
        if (w > widest) widest = w;
    }
    if (widestOut) *widestOut = widest;
    if (perRow) {
        // A cell is "nn  " plus the name field, and the name field has to
        // hold the widest name plus the " (staff)" marker plus two spaces
        // between columns. The old arithmetic divided by widest + 6 while
        // printing widest + 14, so a wide board packed four columns into a
        // line that fitted three and wrapped every row.
        uint8_t cols  = s.term.cols() ? s.term.cols() : 40;
        uint8_t cellW = static_cast<uint8_t>(widest + 14);
        uint8_t pr    = static_cast<uint8_t>(cols / cellW);
        if (!pr) pr = 1;
        if (pr > 4) pr = 4;
        *perRow = pr;
    }
    return n;
}

void areaMenu(Bbs& b, Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    char buf[96];

    uint8_t visible[kMaxAreas];
    uint8_t widest = 0;
    uint8_t perRow = 1;
    uint8_t n = visibleAreas(s, visible, &perRow, &widest);

    // A config reload can take areas away underneath a caller who is sitting
    // on the menu, so the bar is clamped here rather than trusted.
    uint8_t slot = slotOf(s);
    if (n && g_sel[slot] >= n) g_sel[slot] = static_cast<uint8_t>(n - 1);

    b.rowTitle(s, "File areas");
    if (!n) {
        t.color(tl, Color::Grey);
        t.text(tl, "Nothing here yet. The sysop sets areas up in CONFIG.");
        t.nl(tl);
        return;
    }

    bool point = canPoint(s);
    t.color(tl, Color::LightGreen);
    for (uint8_t i = 0; i < n; ++i) {
        uint8_t a = visible[i];
        bool last = (i % perRow) == static_cast<uint8_t>(perRow - 1) || i + 1 == n;
        bool here = point && i == g_sel[slot];
        // An area with a read level of its own is marked, so a sysop can see
        // at a glance which ones a caller will not be shown. Lost when the
        // menu moved out of rows() into here, and only a test noticed.
        bool shut = g_area[a].read != PlugLevel::Nobody &&
                    g_area[a].read != PlugLevel::All;
        char nm[kNameMax + 10];
        snprintf(nm, sizeof(nm), "%s%s", g_area[a].name, shut ? " (staff)" : "");
        // The highlight covers the number and the name together, because a
        // bar on the name alone reads as the name being special rather than
        // as this being the row you are standing on.
        // The last cell on a row is padded only when it is highlighted, and
        // then only to the name width rather than the full cell, so the bar
        // still looks deliberate without ever reaching the final column.
        // Filling the last column makes the terminal wrap, and the newline
        // that follows then lands as a blank row.
        int pad = last ? (here ? static_cast<int>(widest + 8) : 0)
                       : static_cast<int>(widest + 10);
        snprintf(buf, sizeof(buf), "%2u  %-*.*s", static_cast<unsigned>(a + 1),
                 pad, static_cast<int>(widest + 8), nm);
        if (here) t.reverse(tl, true);
        t.text(tl, buf);
        if (here) t.reverse(tl, false);
        if (last) t.nl(tl);
    }
}

// filesPrompt: the subsystem's own prompt line, drawn wherever a caller
// lands. The core's shell prompt is not running while the plugin owns the
// session, so this is the only thing telling them what to type.
void filesPrompt(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    t.nl(tl);
    t.color(tl, Color::Cyan);
    // Two tests, not one. A PETSCII C64 can point, so canPoint() alone put a
    // 49 character line on a 40 column screen, where it wrapped and cost a
    // row off a screen that only has 25. Pointing is about the terminal's
    // attributes; fitting is about its width, and they are not the same
    // question.
    bool wide = t.cols() >= 60;
    if (g_where[slotOf(s)] == Where::Area)
        t.text(tl, "Area: number, L lists, Q back");
    else if (canPoint(s) && wide)
        t.text(tl, "Files: cursor keys and Enter, or a number. Q quits");
    else if (canPoint(s))
        t.text(tl, "Files: cursors, Enter, number. Q quits");   // 38, fits 40
    else
        t.text(tl, "Files: number opens an area, Q quits");
    t.nl(tl);
    t.color(tl, Color::White);
}

// ---------------------------------------------------------------------------
// rows: the paged list, both kinds. listIdx is the row, as in the core's own
// list builders, and returning false ends the list.
// ---------------------------------------------------------------------------
bool rows(Session& s) {
    Bbs& b = Bbs::instance();
    char buf[96];
    uint8_t at = g_at[slotOf(s)];
    if (at == 0xFF) return false;

    // ---- one area's files ------------------------------------------------
    char dir[128];
    if (!areaPath(at, dir, sizeof(dir))) return false;

    uint8_t i = s.listIdx++;
    if (i == 0) { b.rowTitle(s, g_area[at].name); return true; }

    // The directory is reopened and walked to row i every time. A board with
    // sixteen callers cannot hold sixteen open DIR handles against a FATFS
    // budget of twenty, and a held handle across a page break is a handle
    // held until somebody presses a key, which may be never.
    DIR* d = opendir(dir);
    if (!d) {
        // The folder is made at start, so reaching this means it went away
        // afterwards: the card was pulled, or somebody deleted it on a PC.
        if (i == 1) {
            b.rowText(s, Color::LightRed, "That folder is not on the card any more.");
            return true;
        }
        return false;
    }
    uint8_t want = static_cast<uint8_t>(i - 1);
    uint8_t seen = 0;
    bool    found = false;
    char    fname[40] = {};
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        if (ieq(e->d_name, BBS_FILES_DESC)) continue;   // the descriptions are not a file area
        if (seen++ != want) continue;
        snprintf(fname, sizeof(fname), "%.39s", e->d_name);
        found = true;
        break;
    }
    closedir(d);

    if (!found) {
        if (want == 0) {
            b.rowText(s, Color::Grey, "Nothing in here yet.");
            return true;
        }
        if (want == seen) { b.rowRule(s); return true; }
        // The subsystem's own prompt is the last thing the list emits. The
        // core hands the session back to this plugin when rows() returns
        // false, and draws no shell prompt, so if this is not here the
        // caller is left with no idea what to type.
        if (want == seen + 1) { filesPrompt(s); return true; }
        return false;
    }

    char full[192];
    snprintf(full, sizeof(full), "%s/%s", dir, fname);
    struct stat st;
    unsigned long kb = 0;
    if (stat(full, &st) == 0) kb = (static_cast<unsigned long>(st.st_size) + 1023u) / 1024u;

    char desc[kDescMax + 1];
    findDesc(dir, fname, desc, sizeof(desc));

    // Name, size, then whatever width is left for the description. A 40
    // column terminal gets 18 characters of it and an 80 column one gets 58,
    // rather than both being cut to the narrow case: 18 characters is meaner
    // than any real BBS managed and there is no reason to spend the width
    // that is sitting there. 12 + 1 + 6 + 1 = 20 goes to the name and size.
    unsigned cols = s.term.cols() ? s.term.cols() : 40;
    unsigned dw   = cols > 22 ? cols - 21 : 1;
    if (dw > kDescMax) dw = kDescMax;
    snprintf(buf, sizeof(buf), "%-12.12s %5luK %-*.*s", fname, kb,
             static_cast<int>(dw), static_cast<int>(dw), desc);
    b.rowText(s, Color::LightGrey, buf);
    return true;
}

// ---------------------------------------------------------------------------
// enter / leave / listArea: the subsystem.
// ---------------------------------------------------------------------------
// clear: true everywhere except the way in, where an intro screen has just
// played and wiping it would be the same as not having one.
void showMenu(Bbs& b, Session& s, bool clear = true) {
    // g_at is deliberately left alone: it is the area this caller last
    // opened, not where they are standing, and DESC at the command prompt
    // reads it. g_where is the one that says where they are.
    g_where[slotOf(s)] = Where::Menu;
    if (clear) s.term.cls(s.tl);
    areaMenu(b, s);
    filesPrompt(s);
}

// listArea: hand the session to the core's list machinery for this area's
// files. The plugin still owns the session, so when the list finishes the
// core gives it back rather than dropping the caller at the shell prompt.
void listArea(Bbs& b, Session& s, uint8_t area) {
    g_where[slotOf(s)] = Where::Area;
    g_at[slotOf(s)]    = area;
    s.term.cls(s.tl);
    b.startPluginList(s, g_index);
}

void leave(Bbs& b, Session& s) {
    g_where[slotOf(s)] = Where::Out;
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, "Out of files.");
    b.release(s);
}

void enter(Bbs& b, Session& s) {
    if (!plat::sdBase()[0]) {
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, "No card in the board, so no file areas.");
        b.prompt(s);
        return;
    }
    if (!b.own(s, g_index)) { b.prompt(s); return; }
    b.setDoing(s, "FILES");
    s.term.reset(s.tl);
    // A board with screens/files gets a way in. Without one the caller lands
    // on the menu, the same deal the chat room has with chatin.
    s.term.cls(s.tl);
    if (b.showScreen(s, "files")) s.term.nl(s.tl);
    showMenu(b, s, false);          // the screen just played: do not wipe it
}

// onKey: the subsystem's keys. Digits pick an area, and they accumulate so
// area 12 is reachable on a board with twelve of them: a single keypress
// would cap the whole design at nine.
void onKey(Session& s, int k, uint32_t now) {
    (void)now;
    Bbs& b = Bbs::instance();
    uint8_t slot = slotOf(s);

    if (k == 'q' || k == 'Q' || k == KEY_ESC || k == KEY_BREAK) {
        if (g_where[slot] == Where::Area) { showMenu(b, s); return; }   // back one level
        leave(b, s);
        return;
    }
    if (k == 'l' || k == 'L') {
        if (g_where[slot] == Where::Area) { listArea(b, s, g_at[slot]); return; }
        showMenu(b, s);
        return;
    }
    // Cursor keys move the bar. Only on the menu: inside an area the list
    // pager owns the screen and moving a highlight that is not drawn would
    // just eat the key.
    if (g_where[slot] == Where::Menu && canPoint(s) &&
        (k == KEY_UP || k == KEY_DOWN || k == KEY_LEFT || k == KEY_RIGHT)) {
        uint8_t visible[kMaxAreas];
        uint8_t perRow = 1;
        uint8_t n = visibleAreas(s, visible, &perRow, nullptr);
        if (!n) return;
        int sel = g_sel[slot];
        // Left and right walk the whole menu rather than stopping at the end
        // of a row, so a caller who never works out that it is a grid can
        // still reach every area with one key.
        if (k == KEY_LEFT)  sel -= 1;
        if (k == KEY_RIGHT) sel += 1;
        if (k == KEY_UP)    sel -= perRow;
        if (k == KEY_DOWN)  sel += perRow;
        if (sel < 0)   sel = 0;
        if (sel >= n)  sel = n - 1;
        g_sel[slot] = static_cast<uint8_t>(sel);
        showMenu(b, s);
        return;
    }
    if (k >= '0' && k <= '9') {
        // One keypress per area, and '0' means ten: the two built-in areas
        // sit at 9 and 10, so without that the Logs area would be reachable
        // by cursor but not by number, and plain ASCII has no cursor.
        uint8_t want = k == '0' ? 10 : static_cast<uint8_t>(k - '0');
        if (want >= 1 && want <= g_areas && g_area[want - 1].path[0] &&
            mayRead(s, static_cast<uint8_t>(want - 1))) {
            s.term.ch(s.tl, static_cast<char>(k));
            listArea(b, s, static_cast<uint8_t>(want - 1));
            return;
        }
        // Same answer for a number that is not an area and for one the
        // caller may not read, so pressing digits cannot be used to find out
        // which numbers are hiding something. Saying nothing at all would
        // leak just as little and leave the caller wondering if the board
        // heard them.
        s.term.nl(s.tl);
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "No area by that number.");
        filesPrompt(s);
        return;
    }
    if (k == KEY_ENTER) {
        if (g_where[slot] == Where::Menu && canPoint(s)) {
            uint8_t visible[kMaxAreas];
            uint8_t n = visibleAreas(s, visible, nullptr, nullptr);
            if (n) { listArea(b, s, visible[g_sel[slot]]); return; }
        }
        filesPrompt(s);
        return;
    }
}

// ---------------------------------------------------------------------------
// The commands.
// ---------------------------------------------------------------------------
const Command kCommands[] = {
    { "FILES", "F", 0, CF_READ, "[F]ILES [n]", "the file areas",
      [](Bbs& b, Session& s, const char* a, uint32_t) {
          enter(b, s);
          if (!b.owns(s, g_index) || !*a) return;      // entry refused, or no area asked for
          long n = strtol(a, nullptr, 10);
          // An area they may not read is refused in the same words as one
          // that does not exist, so the command cannot be used to find out
          // which numbers are hiding something.
          if (n < 1 || n > g_areas || !g_area[n - 1].path[0] ||
              !mayRead(s, static_cast<uint8_t>(n - 1))) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "No area by that number.");
              filesPrompt(s);
              return;
          }
          listArea(b, s, static_cast<uint8_t>(n - 1));
      },
      Menu::Main, 8 },
    { "DESC", "", 0, CF_WRITE, "DESC f text", "describe a file in this area",
      [](Bbs& b, Session& s, const char* a, uint32_t) {
          char buf[96];
          uint8_t at = g_at[slotOf(s)];
          char dir[128];
          if (at == 0xFF || !areaPath(at, dir, sizeof(dir))) {
              s.term.color(s.tl, Color::Grey);
              s.term.text(s.tl, "Open an area first with FILES n.");
              b.prompt(s);
              return;
          }
          // The command's CF_WRITE got them this far against the plugin's
          // level; this is the area's own, which may be higher.
          if (!mayWrite(s, at)) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "This area is not yours to describe.");
              b.prompt(s);
              return;
          }
          char name[40] = {};
          const char* rest = a;
          while (*rest == ' ') ++rest;
          size_t k = 0;
          while (*rest && *rest != ' ' && k + 1 < sizeof(name)) name[k++] = *rest++;
          name[k] = '\0';
          while (*rest == ' ') ++rest;
          if (!safeName(name)) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "DESC <file> <description>");
              b.prompt(s);
              return;
          }
          char full[192];
          snprintf(full, sizeof(full), "%s/%s", dir, name);
          struct stat st;
          if (stat(full, &st) != 0) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "No file by that name in this area.");
              b.prompt(s);
              return;
          }
          char text[kDescMax + 1];
          snprintf(text, sizeof(text), "%.*s", kDescMax, rest);
          if (setDesc(dir, name, text)) {
              snprintf(buf, sizeof(buf), *text ? "Described %.20s." : "Description cleared.", name);
              s.term.color(s.tl, Color::LightGreen);
              s.term.text(s.tl, buf);
          } else {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "Could not write the description file.");
          }
          b.prompt(s);
      },
      Menu::Main, 9 },
};

const PluginSetting kSettings[] = {
    { "area1", "Area 1", PS_TEXT, 0, 0, kPathMax + kNameMax + 3 },
    { "area2", "Area 2", PS_TEXT, 0, 0, kPathMax + kNameMax + 3 },
    { "area3", "Area 3", PS_TEXT, 0, 0, kPathMax + kNameMax + 3 },
    { "area4", "Area 4", PS_TEXT, 0, 0, kPathMax + kNameMax + 3 },
    { "area5", "Area 5", PS_TEXT, 0, 0, kPathMax + kNameMax + 3 },
    { "area6", "Area 6", PS_TEXT, 0, 0, kPathMax + kNameMax + 3 },
    { "area7", "Area 7", PS_TEXT, 0, 0, kPathMax + kNameMax + 3 },
    { "area8", "Area 8", PS_TEXT, 0, 0, kPathMax + kNameMax + 3 },
};

void setting(const char* key, char* out, size_t n) {
    if (strncmp(key, "area", 4) != 0) return;
    long i = strtol(key + 4, nullptr, 10) - 1;
    if (i < 0 || i >= kMaxAreas || !g_area[i].path[0]) return;
    if (g_area[i].read == PlugLevel::Nobody && g_area[i].write == PlugLevel::Nobody) {
        snprintf(out, n, "%s | %s", g_area[i].path, g_area[i].name);
    } else {
        snprintf(out, n, "%s | %s | %s | %s", g_area[i].path, g_area[i].name,
                 plugins::levelName(g_area[i].read == PlugLevel::Nobody
                                    ? plugins::levelFor(g_index, 0) : g_area[i].read),
                 plugins::levelName(g_area[i].write == PlugLevel::Nobody
                                    ? plugins::levelFor(g_index, 1) : g_area[i].write));
    }
}

const char* status() {
    static char line[64];
    if (!plat::sdBase()[0]) return nullptr;      // the sd plugin already says so
    uint8_t live = 0;
    for (uint8_t i = 0; i < g_areas; ++i) if (g_area[i].path[0]) ++live;
    snprintf(line, sizeof(line), "Files: %u area%s", live, live == 1 ? "" : "s");
    return line;
}

} // namespace

extern const Plugin kFilesPlugin = {
    // PF_SD: the files are on the card, so this does not start without one.
    // PF_ON so a sysop who adds an area to system.cfg gets it on the next
    // reload rather than having to switch the plugin on as well.
    { kName, "File areas", "1.0", 0, 0, PF_CORE | PF_SD | PF_ON,
      PlugLevel::All, PlugLevel::Staff, PlugLevel::Sysop },
    start,
    stop,
    nullptr,                 // tick
    nullptr,                 // onConnect
    nullptr,                 // onLogin
    onLogoff,
    onKey,
    status,
    kCommands,
    sizeof(kCommands) / sizeof(kCommands[0]),
    kSettings,
    sizeof(kSettings) / sizeof(kSettings[0]),
    setting,
    rows,
    nullptr,                 // onPresence
    nullptr,                 // onBytes
};
