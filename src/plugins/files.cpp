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
 *               Transfer is XMODEM and XMODEM-1K, one at a time board-wide.
 *                  The engine in core/xmodem.h does the protocol and knows
 *                  nothing about sockets or files; this plugin feeds it from
 *                  onBytes, drains it into the caller's timeline, and nudges
 *                  it from tick() for timeouts. A transfer advances one block
 *                  per round trip, which is XMODEM being stop-and-wait rather
 *                  than the integration being slow.
 *
 *               No YMODEM yet, so there is no filename on the wire: a
 *                  download is named by the command and an upload is named
 *                  at a prompt before the line goes binary. That is how
 *                  boards did XMODEM uploads and it is not a workaround.
 *
 * Config:       [plugin:files]
 *                  enabled  yes
 *                  read     all        who may browse
 *                  write    staff      the fallback upload level
 *                  admin    sysop      the fallback delete level
 *                  area1..8 <path> | <name> | read | up | down | del
 *
 *               The four area levels are optional and each falls back on
 *                  its own: read to the plugin's read, up to the plugin's
 *                  write, down to THIS AREA's read, del to the plugin's
 *                  admin. Upload sits before download on the wire because
 *                  that is where the old single "write" level was, and
 *                  moving it would silently reinterpret every area already
 *                  configured.
 *
 * Commands:     FILES, FILES n, DOWNLOAD, UPLOAD, UPLOADS, APPROVE,
 *               REJECT, ERASE, DESC
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
#include "../core/xmodem.h"
#include "../platform/platform.h"
#include "../config.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <ctime>

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
// path | name | read | up | down | del, with " | " between each pair.
constexpr uint8_t kAreaValMax = kPathMax + kNameMax + 4 * 6 + 5 * 3;
constexpr uint8_t kDescMax  = 48;

// An upload lands here and is not in the area until staff move it.
//
// A staging folder rather than a flag on the file, and that is the whole
// safety argument: with a marker, every listing and download path has to
// remember to check it, and forgetting one check serves an unapproved file.
// Fail-open. With a folder, an unapproved file simply is not in the area, so
// the code that lists and sends needs no changes at all to keep it out of
// sight. Approval is then a rename on the same filesystem, which is cheap
// and near atomic on FAT, and a power cut mid-upload leaves junk in staging
// rather than in the public area.
//
// The leading dot is not decoration: the area listing already skips entries
// starting with one, so this folder is invisible to callers for free.
const char* const kPendDir  = ".pending";
const char* const kPendList = "UPLOADS.BBS";

// Caps, because an upload is the first thing on this board a caller can use
// to consume somebody else's resources. Without these the first person to
// find it fills the card.
constexpr uint8_t  kMaxPendPerArea = 20;
constexpr uint32_t kMaxUploadBytes = 4u * 1024u * 1024u;

// An area's own read and write levels, which are the plugin's unless the
// area says otherwise. Deferring these to the upload phase was a mistake:
// the first thing anybody wants is a staff-only area, and that has nothing
// to do with uploading. Same words and same ladder as everywhere else, so a
// sysop learns the vocabulary once.
// Four levels, because "write" was doing two jobs and they are not the same
// trust. Pulling a file out of an area and pushing one into it are different
// permissions on every BBS that ever ran, and lumping them together would
// have meant a board could not offer downloads to callers without also
// letting them upload.
//
// The wire order is path | name | read | up | down | del, and `up` sits
// where the old `write` was **on purpose**. Inserting `down` in its place
// would have silently turned every existing area's upload level into its
// download level, which is the exact class of change this project refuses
// to make quietly. Ugly order, no ambiguity, nothing reinterpreted.
struct Area {
    char      path[kPathMax + 1] = {};   // relative to the card root
    char      name[kNameMax + 1] = {};   // what a caller sees
    PlugLevel read  = PlugLevel::Nobody; // Nobody here means "use the fallback"
    PlugLevel up    = PlugLevel::Nobody; // was `write`, same position on the wire
    PlugLevel down  = PlugLevel::Nobody;
    PlugLevel del   = PlugLevel::Nobody;
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

// This caller against this area. An area that set no level of its own falls
// back, and what it falls back to is chosen per permission rather than all
// landing on the plugin's read level:
//
//   read  -> the plugin's read.   Seeing an area is the plugin's own gate.
//   up    -> the plugin's write.  Where the old `write` went, unchanged.
//   down  -> this area's READ.    If you can see what is in an area, taking
//            it is the least surprising default, and it is what the board
//            did before download existed. A sysop who wants "everyone can
//            look, members can fetch" sets it explicitly.
//   del   -> the plugin's ADMIN.  Never the area's write. Removing files is
//            the destructive one, so it fails shut: an area that says
//            nothing about deletion does not quietly inherit permission to
//            delete from permission to upload.
bool mayRead(const Session& s, uint8_t i) {
    if (i >= g_areas || !g_area[i].path[0]) return false;
    PlugLevel lv = g_area[i].read;
    if (lv == PlugLevel::Nobody) lv = plugins::levelFor(g_index, 0);
    return plugins::mayUse(s, lv);
}

bool mayUp(const Session& s, uint8_t i) {
    if (i >= g_areas || !g_area[i].path[0]) return false;
    PlugLevel lv = g_area[i].up;
    if (lv == PlugLevel::Nobody) lv = plugins::levelFor(g_index, 1);
    return plugins::mayUse(s, lv);
}

bool mayDown(const Session& s, uint8_t i) {
    if (i >= g_areas || !g_area[i].path[0]) return false;
    PlugLevel lv = g_area[i].down;
    if (lv == PlugLevel::Nobody) lv = g_area[i].read;              // the area's
    if (lv == PlugLevel::Nobody) lv = plugins::levelFor(g_index, 0);
    return plugins::mayUse(s, lv);
}

bool mayDel(const Session& s, uint8_t i) {
    if (i >= g_areas || !g_area[i].path[0]) return false;
    PlugLevel lv = g_area[i].del;
    if (lv == PlugLevel::Nobody) lv = plugins::levelFor(g_index, 2);   // admin
    return plugins::mayUse(s, lv);
}

// Kept as the name DESC uses. Describing a file is part of putting one
// there, so it follows upload rather than being a fifth level.
bool mayWrite(const Session& s, uint8_t i) { return mayUp(s, i); }

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
    char parts[6][kPathMax + kNameMax + 4] = {};
    uint8_t np = 0;
    const char* p = value;
    while (np < 6) {
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
        plat::log("files: area%ld read level '%s' is not a level, using the fallback", n, parts[2]);
    if (np > 3 && parts[3][0] && !plugins::levelFromText(parts[3], a.up))
        plat::log("files: area%ld upload level '%s' is not a level, using the fallback", n, parts[3]);
    if (np > 4 && parts[4][0] && !plugins::levelFromText(parts[4], a.down))
        plat::log("files: area%ld download level '%s' is not a level, using the fallback", n, parts[4]);
    if (np > 5 && parts[5][0] && !plugins::levelFromText(parts[5], a.del))
        plat::log("files: area%ld delete level '%s' is not a level, using the fallback", n, parts[5]);
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

// Defined with the transfer code below, but start() is what establishes the
// count in the first place.
void recountPending();

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
    scr.down  = PlugLevel::Staff;
    scr.up    = PlugLevel::Sysop;
    scr.del   = PlugLevel::Sysop;

    Area& lg = g_area[kAreaLogs];
    snprintf(lg.path, sizeof(lg.path), "%s", BBS_SD_LOG_DIR);
    snprintf(lg.name, sizeof(lg.name), "%s", "Logs");
    lg.read  = PlugLevel::Staff;
    lg.down  = PlugLevel::Staff;
    lg.up    = PlugLevel::Sysop;
    lg.del   = PlugLevel::Sysop;

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
        if (!had) {
            if (makeAreaDir(g_area[i].path)) {
                ++made;
            } else {
                // Say so. This failed silently once and cost real time: the
                // card was 8.3 only, a folder called "textfiles" is nine
                // characters and therefore not a valid name at all, so the
                // mkdir was refused, the area listed as configured, and the
                // only symptom a sysop ever saw was "that folder is not on
                // the card" with nothing anywhere explaining it.
                plat::log("files: area%u could not make %s, so it will not list",
                          static_cast<unsigned>(i + 1), full);
            }
        }
    }
    recountPending();
    plat::log("files: %u area%s, %u folder%s created",
              live, live == 1 ? "" : "s", made, made == 1 ? "" : "s");
    return true;
}

void stop() {}

// A caller leaving takes their place in an area with them. Sessions are a
// static pool, so anything not cleared here is inherited by the next caller
// on that node.
// Defined with the rest of the transfer code further down; onLogoff is the
// one thing above it that has to know a caller can vanish mid-transfer.
void xferDropped(Session& s);

void onLogoff(Session& s) {
    xferDropped(s);
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
            // Not "any more": it may never have been there. The old wording
            // sent a sysop looking for a folder that had gone missing when
            // the truth was that it was never created.
            b.rowText(s, Color::LightRed, "That folder is not on the card.");
            if (plugins::mayUse(s, PlugLevel::Staff))
                b.rowText(s, Color::Grey, "Check the area's path in CONFIG files.");
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


// ===========================================================================
// Transfers.
//
// One at a time, board-wide. That is the engine's own documented design and
// it is the right one here: XMODEM is stop-and-wait, so a transfer spends
// almost all of its time waiting for the far end, and a second engine would
// cost 1.1 KB of static RAM to overlap two things that are each mostly idle.
// A caller asking while somebody else is transferring is told to wait rather
// than queued, because a queue on a board with ten lines is a way of making
// somebody sit and watch nothing happen.
//
// Nothing here blocks. The engine is fed from onBytes as the far end
// answers, drained into the caller's timeline, and nudged from tick() for
// timeouts and the receiver's start polling. A transfer therefore advances
// one block per round trip, which is XMODEM behaving correctly rather than
// the integration being slow.
// ===========================================================================
struct Xfer {
    Session* s      = nullptr;     // who holds it, null when free
    FILE*    fp     = nullptr;
    bool     sending = false;
    uint8_t  area   = 0xFF;
    uint32_t started = 0;
    uint32_t written = 0;          // receiving: bytes accepted, for the cap
    char     name[kDescMax + 1] = {};
};

Xfer            g_x;
xmodem::Engine  g_eng;

// How many uploads are waiting on staff, board-wide. Counted at start and on
// mount, then kept by hand as uploads arrive and are dealt with, so telling a
// staff member at login costs no reads at all. Same reasoning as chat's "you
// have mail". A card edited on a laptop goes stale until the next start,
// which is the accepted trade for a login that does not touch the card.
uint16_t g_pending = 0;

// pendPath: an area's staging folder, or false when there is no card.
bool pendPath(uint8_t i, char* out, size_t n) {
    char dir[128];
    if (!areaPath(i, dir, sizeof(dir))) return false;
    snprintf(out, n, "%s/%s", dir, kPendDir);
    return true;
}

// countPending: how many real files are staged in one area. The list file is
// not one of them.
uint16_t countPending(uint8_t i) {
    char pd[160];
    if (!pendPath(i, pd, sizeof(pd))) return 0;
    DIR* d = opendir(pd);
    if (!d) return 0;
    uint16_t n = 0;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        if (ieq(e->d_name, kPendList)) continue;
        ++n;
    }
    closedir(d);
    return n;
}

void recountPending() {
    g_pending = 0;
    for (uint8_t i = 0; i < g_areas; ++i)
        if (g_area[i].path[0]) g_pending = static_cast<uint16_t>(g_pending + countPending(i));
}

// The engine asks for file data through this and never learns what a file
// is. A short read is taken as end of file, so this must return max until
// the file really ends.
uint16_t xferRead(void* ctx, uint8_t* dst, uint16_t max) {
    Xfer* x = static_cast<Xfer*>(ctx);
    if (!x->fp) return 0;
    size_t got = fread(dst, 1, max, x->fp);
    return static_cast<uint16_t>(got);
}

// The engine hands a received block here. Returning false aborts the
// transfer with SinkFailed, which is how the size cap and a full card are
// both reported: the caller is told the upload failed rather than being left
// to discover a truncated file later.
bool xferWrite(void* ctx, const uint8_t* src, uint16_t len) {
    Xfer* x = static_cast<Xfer*>(ctx);
    if (!x->fp) return false;
    if (x->written + len > kMaxUploadBytes) {
        plat::log("files: upload %s refused, over the size cap", x->name);
        return false;
    }
    if (fwrite(src, 1, len, x->fp) != len) return false;
    x->written += len;
    return true;
}

// xferPump: move whatever the engine has ready into the caller's timeline.
//
// Sized against the room actually left, and halved, because Term::raw
// doubles every 0xFF for telnet: 1 KB of 0xFF becomes 2 KB on the wire, and
// a timeline put() is all-or-nothing, so asking for more than will fit
// silently drops the block and stalls the transfer with no error anywhere.
void xferPump(Session& s, uint32_t now) {
    uint8_t buf[256];
    for (;;) {
        size_t room = s.tl.freeBytes();
        if (room < 64) return;                       // let the socket drain
        size_t want = (room - 32) / 2;               // worst case IAC doubling
        if (want > sizeof(buf)) want = sizeof(buf);
        size_t n = g_eng.pull(buf, want, now);
        if (!n) return;
        s.term.raw(s.tl, buf, n);
    }
}

// xferEnd: close the file, put the terminal back, tell the caller, and hand
// the session to whoever should have it next.
void xferEnd(Bbs& b, Session& s) {
    char buf[160];
    if (g_x.fp) { fclose(g_x.fp); g_x.fp = nullptr; }

    b.setRawInput(s, false);
    s.tn.setBinary(false);
    s.term.reset(s.tl);
    s.term.nl(s.tl);

    if (g_eng.done()) {
        unsigned kb = static_cast<unsigned>((g_eng.bytes() + 512) / 1024);
        snprintf(buf, sizeof(buf), "%s complete: %s, %u KB in %u blocks%s",
                 g_x.sending ? "Download" : "Upload", g_x.name, kb,
                 static_cast<unsigned>(g_eng.blocks()),
                 g_eng.crcMode() ? ", CRC" : ", checksum");
        s.term.color(s.tl, Color::LightGreen);
    } else {
        snprintf(buf, sizeof(buf), "%s failed: %s",
                 g_x.sending ? "Download" : "Upload", g_eng.errorText());
        s.term.color(s.tl, Color::LightRed);
    }
    s.term.text(s.tl, buf);
    s.term.nl(s.tl);

    plat::log("files: %s %s %s, %lu bytes, %u errors",
              g_x.sending ? "sent" : "received", g_x.name,
              g_eng.done() ? "ok" : "failed",
              static_cast<unsigned long>(g_eng.bytes()),
              static_cast<unsigned>(g_eng.errors()));

    // A finished upload is recorded next to the file it describes, so a
    // sysop with the card in a laptop can see who sent what without the
    // board running. Appended rather than rewritten: an append cannot lose
    // the lines already there if the power goes.
    if (!g_x.sending && g_eng.done() && g_x.area != 0xFF) {
        char pd[160], lf[192];
        if (pendPath(g_x.area, pd, sizeof(pd))) {
            snprintf(lf, sizeof(lf), "%s/%s", pd, kPendList);
            FILE* f = fopen(lf, "a");
            if (f) {
                fprintf(f, "%s\t%s\t%lu\t%lu\n", g_x.name, s.user,
                        static_cast<unsigned long>(time(nullptr)),
                        static_cast<unsigned long>(g_x.written));
                fclose(f);
            }
        }
        ++g_pending;
    }

    g_eng.reset();
    g_x = Xfer();

    // Back where they came from. A caller who was in the file area when they
    // started a download belongs in the file area afterwards, not dropped at
    // the shell without being told why the room changed.
    if (g_where[slotOf(s)] != Where::Out) showMenu(b, s);
    else                                  b.prompt(s);
}

// xferDrive: one pass of feed, tick and pull. Both entry points share it so
// there is one order of operations rather than two that can drift.
void xferDrive(Session& s, const uint8_t* in, size_t n, uint32_t now) {
    if (in && n) g_eng.feed(in, n, now);
    g_eng.tick(now);
    xferPump(s, now);
    if (!g_eng.running() && !g_eng.pending())
        xferEnd(Bbs::instance(), s);
}

void onBytes(Session& s, const uint8_t* in, size_t n, uint32_t now) {
    if (g_x.s != &s) return;            // not this caller's transfer
    xferDrive(s, in, n, now);
}

// tick: timeouts and retries only. The transfer is driven by the far end
// answering, so this is what notices when it stops answering.
void tick(uint32_t now) {
    if (!g_x.s) return;
    xferDrive(*g_x.s, nullptr, 0, now);
}

// A caller dropping mid-transfer takes the engine with them. Sessions come
// from a static pool, so a stale pointer here would be handed to whoever
// dialled in next and the board would push a file at them.
// Staff are told once, at login, and only if there is something to tell.
// Costs no reads: g_pending is kept as uploads come and go.
void onLogin(Session& s) {
    if (!g_pending) return;
    if (!plugins::mayUse(s, PlugLevel::Staff)) return;
    char buf[80];
    snprintf(buf, sizeof(buf), "%u upload%s awaiting approval. UPLOADS lists them.",
             static_cast<unsigned>(g_pending), g_pending == 1 ? "" : "s");
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::Yellow);
    s.term.text(s.tl, buf);
    s.term.nl(s.tl);
    s.term.reset(s.tl);
}

void xferDropped(Session& s) {
    if (g_x.s != &s) return;
    if (g_x.fp) { fclose(g_x.fp); g_x.fp = nullptr; }
    g_eng.reset();
    g_x = Xfer();
    plat::log("files: transfer dropped, node %u left", static_cast<unsigned>(s.id));
}

// startSend: the download. Everything that can be refused is refused before
// the terminal is put into binary mode, because a refusal that arrives after
// the switch is a refusal the caller's terminal cannot read.
void startSend(Bbs& b, Session& s, const char* arg, uint32_t now) {
    char buf[192];
    uint8_t at = g_at[slotOf(s)];
    char dir[128];

    if (at == 0xFF || !areaPath(at, dir, sizeof(dir))) {
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, "Open an area first with FILES n.");
        b.prompt(s);
        return;
    }
    if (!mayDown(s, at)) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "This area is not yours to download from.");
        b.prompt(s);
        return;
    }
    if (g_x.s) {
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, "Somebody is transferring right now. Try in a moment.");
        b.prompt(s);
        return;
    }

    char name[kDescMax + 1] = {};
    const char* r = arg;
    while (*r == ' ') ++r;
    size_t k = 0;
    while (*r && *r != ' ' && k + 1 < sizeof(name)) name[k++] = *r++;
    name[k] = '\0';
    if (!safeName(name)) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "DOWNLOAD <file>");
        b.prompt(s);
        return;
    }

    snprintf(buf, sizeof(buf), "%s/%s", dir, name);
    FILE* fp = fopen(buf, "rb");
    if (!fp) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "No such file in this area.");
        b.prompt(s);
        return;
    }

    if (!b.own(s, g_index)) { fclose(fp); b.prompt(s); return; }
    b.setDoing(s, "DOWNLOAD");

    g_x.s       = &s;
    g_x.fp      = fp;
    g_x.sending = true;
    g_x.area    = at;
    g_x.started = now;
    snprintf(g_x.name, sizeof(g_x.name), "%s", name);

    // Told before the line goes binary, because after this the caller's
    // terminal is a transfer and nothing readable reaches it again until
    // the transfer ends.
    s.term.color(s.tl, Color::Cyan);
    snprintf(buf, sizeof(buf), "Sending %s. Start your XMODEM receive now.", name);
    s.term.text(s.tl, buf);
    s.term.nl(s.tl);
    s.term.reset(s.tl);

    s.tn.setBinary(true);        // CR is data now, not a line ending
    b.setRawInput(s, true);          // and 0x1B is data, not an escape
    g_eng.beginSend(xferRead, &g_x, now, true);
}

// argName: the first word of an argument as a filename, checked. Shared by
// every command that takes one, so they cannot drift apart on what they
// accept.
bool argName(const char* a, char* out, size_t n) {
    const char* r = a;
    while (*r == ' ') ++r;
    size_t k = 0;
    while (*r && *r != ' ' && k + 1 < n) out[k++] = *r++;
    out[k] = 0;
    return safeName(out);
}

// startRecv: the upload. It lands in the area's staging folder and is not in
// the area at all until staff move it, so nothing else on the board has to
// know it exists.
void startRecv(Bbs& b, Session& s, const char* arg, uint32_t now) {
    char buf[224];        // holds "<pending dir 160>/<name 48>" with room over
    uint8_t at = g_at[slotOf(s)];
    char dir[128];

    if (at == 0xFF || !areaPath(at, dir, sizeof(dir))) {
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, "Open an area first with FILES n.");
        b.prompt(s);
        return;
    }
    if (!mayUp(s, at)) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "This area is not yours to upload to.");
        b.prompt(s);
        return;
    }
    if (g_x.s) {
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, "Somebody is transferring right now. Try in a moment.");
        b.prompt(s);
        return;
    }
    if (countPending(at) >= kMaxPendPerArea) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "This area has all the uploads it can hold until staff clear some.");
        b.prompt(s);
        return;
    }

    char name[kDescMax + 1] = {};
    if (!argName(arg, name, sizeof(name))) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "UPLOAD <file>");
        b.prompt(s);
        return;
    }
    // Never let an upload be named as the catalogue: approving it would
    // overwrite every description in the area.
    if (ieq(name, BBS_FILES_DESC) || ieq(name, kPendList)) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "That name belongs to the area. Pick another.");
        b.prompt(s);
        return;
    }

    struct stat st;
    snprintf(buf, sizeof(buf), "%s/%s", dir, name);
    if (stat(buf, &st) == 0) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "There is already a file by that name here.");
        b.prompt(s);
        return;
    }

    char pd[160];
    if (!pendPath(at, pd, sizeof(pd))) { b.prompt(s); return; }
    mkdir(pd, 0755);                                   // first upload makes it

    snprintf(buf, sizeof(buf), "%s/%.48s", pd, name);
    if (stat(buf, &st) == 0) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "Somebody has already sent one by that name, still waiting.");
        b.prompt(s);
        return;
    }
    FILE* fp = fopen(buf, "wb");
    if (!fp) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "Could not open that name on the card.");
        b.prompt(s);
        return;
    }

    if (!b.own(s, g_index)) { fclose(fp); remove(buf); b.prompt(s); return; }
    b.setDoing(s, "UPLOAD");

    g_x.s       = &s;
    g_x.fp      = fp;
    g_x.sending = false;
    g_x.area    = at;
    g_x.started = now;
    g_x.written = 0;
    snprintf(g_x.name, sizeof(g_x.name), "%s", name);

    s.term.color(s.tl, Color::Cyan);
    s.term.text(s.tl, "Ready. Start your XMODEM send now.");
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, "It waits for staff approval before anyone else sees it.");
    s.term.nl(s.tl);
    s.term.reset(s.tl);

    s.tn.setBinary(true);
    b.setRawInput(s, true);
    g_eng.beginRecv(xferWrite, &g_x, now, true);
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
    { "UPLOAD", "U", 0, CF_WRITE, "[U]PLOAD f", "send a file to this area",
      [](Bbs& b, Session& s, const char* a, uint32_t now) { startRecv(b, s, a, now); },
      Menu::Main, 10 },
    { "UPLOADS", "", 0, CF_ADMIN, "UPLOADS", "uploads waiting for approval",
      [](Bbs& b, Session& s, const char*, uint32_t) {
          char buf[192];
          uint8_t shown = 0;
          b.rowTitle(s, "Uploads awaiting approval");
          for (uint8_t i = 0; i < g_areas && shown < 40; ++i) {
              if (!g_area[i].path[0] || !mayDel(s, i)) continue;
              char pd[160];
              if (!pendPath(i, pd, sizeof(pd))) continue;
              DIR* d = opendir(pd);
              if (!d) continue;
              struct dirent* e;
              while ((e = readdir(d)) != nullptr && shown < 40) {
                  if (e->d_name[0] == '.') continue;
                  if (ieq(e->d_name, kPendList)) continue;
                  char full[224];
                  struct stat st;
                  unsigned long kb = 0;
                  snprintf(full, sizeof(full), "%s/%.48s", pd, e->d_name);
                  if (stat(full, &st) == 0)
                      kb = (static_cast<unsigned long>(st.st_size) + 1023u) / 1024u;
                  snprintf(buf, sizeof(buf), "%2u  %-20.20s %4lu KB  %s",
                           static_cast<unsigned>(i + 1), e->d_name, kb, g_area[i].name);
                  b.rowText(s, Color::White, buf);
                  ++shown;
              }
              closedir(d);
          }
          if (!shown) b.rowText(s, Color::Grey, "Nothing waiting.");
          else        b.rowText(s, Color::Grey,
                                "FILES n, then APPROVE <file> or REJECT <file>.");
          b.rowRule(s);
          b.prompt(s);
      },
      Menu::Sysop, 21 },
    { "APPROVE", "", 0, CF_ADMIN, "APPROVE f", "make an upload live in this area",
      [](Bbs& b, Session& s, const char* a, uint32_t) {
          char buf[192], pd[160], from[224], to[224];
          char name[kDescMax + 1] = {};
          uint8_t at = g_at[slotOf(s)];
          char dir[128];
          if (at == 0xFF || !areaPath(at, dir, sizeof(dir))) {
              s.term.color(s.tl, Color::Grey);
              s.term.text(s.tl, "Open the area first with FILES n.");
              b.prompt(s);
              return;
          }
          if (!mayDel(s, at)) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "Approving in this area is not yours to do.");
              b.prompt(s);
              return;
          }
          if (!argName(a, name, sizeof(name)) || !pendPath(at, pd, sizeof(pd))) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "APPROVE <file>");
              b.prompt(s);
              return;
          }
          snprintf(from, sizeof(from), "%s/%s", pd, name);
          snprintf(to,   sizeof(to),   "%s/%s", dir, name);
          // A rename on the same filesystem, which is what makes approval
          // cheap and near atomic. The file is in the area the instant this
          // returns, and was in nobody's way before it.
          if (rename(from, to) != 0) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "No upload by that name is waiting here.");
              b.prompt(s);
              return;
          }
          if (g_pending) --g_pending;
          plat::log("files: %s approved %s into area %u",
                    s.user, name, static_cast<unsigned>(at + 1));
          s.term.color(s.tl, Color::LightGreen);
          snprintf(buf, sizeof(buf), "%.48s is live. DESC it to say what it is.", name);
          s.term.text(s.tl, buf);
          b.prompt(s);
      },
      Menu::Sysop, 22 },
    { "REJECT", "", 0, CF_ADMIN, "REJECT f", "throw an upload away",
      [](Bbs& b, Session& s, const char* a, uint32_t) {
          char buf[224], pd[160];
          char name[kDescMax + 1] = {};
          uint8_t at = g_at[slotOf(s)];
          char dir[128];
          if (at == 0xFF || !areaPath(at, dir, sizeof(dir))) {
              s.term.color(s.tl, Color::Grey);
              s.term.text(s.tl, "Open the area first with FILES n.");
              b.prompt(s);
              return;
          }
          if (!mayDel(s, at)) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "Rejecting in this area is not yours to do.");
              b.prompt(s);
              return;
          }
          if (!argName(a, name, sizeof(name)) || !pendPath(at, pd, sizeof(pd))) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "REJECT <file>");
              b.prompt(s);
              return;
          }
          snprintf(buf, sizeof(buf), "%s/%s", pd, name);
          if (remove(buf) != 0) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "No upload by that name is waiting here.");
              b.prompt(s);
              return;
          }
          if (g_pending) --g_pending;
          plat::log("files: %s rejected %s in area %u",
                    s.user, name, static_cast<unsigned>(at + 1));
          s.term.color(s.tl, Color::Yellow);
          snprintf(buf, sizeof(buf), "%s thrown away.", name);
          s.term.text(s.tl, buf);
          b.prompt(s);
      },
      Menu::Sysop, 23 },
    { "ERASE", "", 0, CF_ADMIN, "ERASE f", "remove a file from this area",
      [](Bbs& b, Session& s, const char* a, uint32_t) {
          char buf[192];
          uint8_t at = g_at[slotOf(s)];
          char dir[128];
          if (at == 0xFF || !areaPath(at, dir, sizeof(dir))) {
              s.term.color(s.tl, Color::Grey);
              s.term.text(s.tl, "Open an area first with FILES n.");
              b.prompt(s);
              return;
          }
          // The command's CF_ADMIN got them past the plugin's gate; this is
          // the area's own delete level, which may be higher and which
          // defaults to the plugin's admin rather than to its upload level.
          // Being allowed to put files somewhere is not the same as being
          // allowed to remove somebody else's.
          if (!mayDel(s, at)) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "This area is not yours to erase from.");
              b.prompt(s);
              return;
          }
          char name[kDescMax + 1] = {};
          const char* r = a;
          while (*r == ' ') ++r;
          size_t k = 0;
          while (*r && *r != ' ' && k + 1 < sizeof(name)) name[k++] = *r++;
          name[k] = 0;
          if (!safeName(name)) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "ERASE <file>");
              b.prompt(s);
              return;
          }
          // Never the description file. FILES.BBS is the area's catalogue,
          // not one of its files, and losing it would silently take every
          // description with it.
          if (ieq(name, BBS_FILES_DESC)) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "That one belongs to the area, not to you.");
              b.prompt(s);
              return;
          }
          snprintf(buf, sizeof(buf), "%s/%s", dir, name);
          if (remove(buf) != 0) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "No such file in this area.");
              b.prompt(s);
              return;
          }
          plat::log("files: %s erased %s from area %u",
                    s.user, name, static_cast<unsigned>(at + 1));
          s.term.color(s.tl, Color::LightGreen);
          snprintf(buf, sizeof(buf), "%s erased.", name);
          s.term.text(s.tl, buf);
          b.prompt(s);
      },
      Menu::Sysop, 20 },
    { "DOWNLOAD", "D", 0, CF_READ, "[D]OWNLOAD f", "send a file by XMODEM",
      [](Bbs& b, Session& s, const char* a, uint32_t now) { startSend(b, s, a, now); },
      Menu::Main, 9 },
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
    // path + name + four level words, each at most 6, with " | " between.
    { "area1", "Area 1", PS_TEXT, 0, 0, kAreaValMax },
    { "area2", "Area 2", PS_TEXT, 0, 0, kAreaValMax },
    { "area3", "Area 3", PS_TEXT, 0, 0, kAreaValMax },
    { "area4", "Area 4", PS_TEXT, 0, 0, kAreaValMax },
    { "area5", "Area 5", PS_TEXT, 0, 0, kAreaValMax },
    { "area6", "Area 6", PS_TEXT, 0, 0, kAreaValMax },
    { "area7", "Area 7", PS_TEXT, 0, 0, kAreaValMax },
    { "area8", "Area 8", PS_TEXT, 0, 0, kAreaValMax },
};

void setting(const char* key, char* out, size_t n) {
    if (strncmp(key, "area", 4) != 0) return;
    long i = strtol(key + 4, nullptr, 10) - 1;
    if (i < 0 || i >= kMaxAreas || !g_area[i].path[0]) return;
    const Area& a = g_area[i];
    if (a.read == PlugLevel::Nobody && a.up == PlugLevel::Nobody &&
        a.down == PlugLevel::Nobody && a.del == PlugLevel::Nobody) {
        snprintf(out, n, "%s | %s", a.path, a.name);
        return;
    }
    // Show what each one is actually running under, never a blank, because a
    // blank reads as "unset by accident" rather than as "inheriting".
    PlugLevel rd = a.read == PlugLevel::Nobody ? plugins::levelFor(g_index, 0) : a.read;
    PlugLevel up = a.up   == PlugLevel::Nobody ? plugins::levelFor(g_index, 1) : a.up;
    PlugLevel dn = a.down == PlugLevel::Nobody ? rd : a.down;
    PlugLevel dl = a.del  == PlugLevel::Nobody ? plugins::levelFor(g_index, 2) : a.del;
    snprintf(out, n, "%s | %s | %s | %s | %s | %s", a.path, a.name,
             plugins::levelName(rd), plugins::levelName(up),
             plugins::levelName(dn), plugins::levelName(dl));
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
    tick,
    nullptr,                 // onConnect
    onLogin,
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
    onBytes,
};
