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
 * Protocol:     YMODEM by default both ways, XMODEM on request. YMODEM's
 *                  block 0 carries the name and the exact size, so a file
 *                  arrives byte-exact; XMODEM has no length field and pads
 *                  the last block with 0x1A, which the engine refuses to
 *                  strip because 0x1A is legal inside a .PRG.
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
#include "../core/claims.h"
#include "../core/bbs_util.h"
#include "../core/xmodem.h"
#include "../platform/platform.h"
#include "../config.h"
#include "panel_feed.h"       // pendingCount, on a board with a display

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

// How many rows the section menu and its prompt took last time, so a cursor
// move can draw over exactly those rows instead of clearing the screen.
//
// Clearing on every keypress is what made the door screen flash away the
// moment anybody moved: the banner is drawn once on the way in and then
// wiped by the first cursor key. Redrawing in place leaves everything above
// the menu alone, which is the whole point of having put a banner there.
uint8_t g_menuRows[BBS_MAX_NODES + 2] = {};

// What the caller is being asked for, while they are being asked. 0 is
// nothing, and the two transfer prompts are the only users.
//
// This exists because DOWNLOAD and UPLOAD were shell commands while the
// plugin owns the caller's keys, so from inside a file area there was no way
// to reach either of them: the one place the files are was the one place you
// could not ask for one. Found on the board, not by a test, because every
// test drove the commands from the shell where they worked fine.
enum : uint8_t {
    AskNone = 0,
    AskNum,          // which numbered file (for a download)
    AskDelNum,       // which numbered file (to erase)
    AskUp,           // a name for XMODEM, or nothing at all for YMODEM
    AskApprove,      // which waiting upload to make live
    AskReject,       // which waiting upload to throw away
    AskYesDown,      // "Download GAME.PRG?" one key
    AskYesDel,       // "Erase GAME.PRG?" one key
    AskDescNum,      // which numbered file to describe
    AskDescText,     // the description itself
    AskUpDesc,       // describe the file that has just been uploaded
};

// The file a yes/no question is about. A number chooses it, the question
// names it, and the answer acts on it, so the caller confirms a filename
// rather than a number they might have miscounted.
char g_pend[BBS_MAX_NODES + 2][kDescMax + 1] = {};

// Which section the just-finished upload went into. g_at is where the
// caller is standing, which is the same thing today and will not be the
// moment anything can move them while a transfer finishes.
uint8_t g_pendArea[BBS_MAX_NODES + 2] = {};
uint8_t g_ask[BBS_MAX_NODES + 2] = {};
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
// nthFile: the Nth file of an area, counting the way the listing numbers
// them, from 1.
//
// Walked rather than cached. A cached table would have to be per session and
// sized for the largest folder anybody might make, which on a board whose
// heap is measured in tens of KB is the wrong trade for something a caller
// does once per download. The folder is reopened, which is the same thing
// the listing itself does for every row.
// ---------------------------------------------------------------------------
bool areaPath(uint8_t i, char* out, size_t n);       // just below

bool pendPath(uint8_t i, char* out, size_t n);       // just below

// nthPending: the Nth upload waiting in a section, numbered the way the
// pending list shows them. Same walk as nthFile, different folder.
bool nthPending(uint8_t area, uint16_t want, char* out, size_t n) {
    char pd[160];
    if (!want || !pendPath(area, pd, sizeof(pd))) return false;
    DIR* d = opendir(pd);
    if (!d) return false;
    uint16_t seen = 0;
    bool found = false;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        if (ieq(e->d_name, kPendList)) continue;
        if (++seen != want) continue;
        snprintf(out, n, "%.48s", e->d_name);
        found = true;
        break;
    }
    closedir(d);
    return found;
}

bool nthFile(uint8_t area, uint16_t want, char* out, size_t n) {
    char dir[128];
    if (!want || !areaPath(area, dir, sizeof(dir))) return false;
    DIR* d = opendir(dir);
    if (!d) return false;
    uint16_t seen = 0;
    bool found = false;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        if (ieq(e->d_name, BBS_FILES_DESC)) continue;
        if (++seen != want) continue;
        snprintf(out, n, "%.48s", e->d_name);
        found = true;
        break;
    }
    closedir(d);
    return found;
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
void startSend(Bbs& b, Session& s, const char* arg, uint32_t now);
void startRecv(Bbs& b, Session& s, const char* arg, uint32_t now);
void doApprove(Bbs& b, Session& s, const char* a);
void doReject(Bbs& b, Session& s, const char* a);
void doErase(Bbs& b, Session& s, const char* a);
void askFor(Session& s, uint8_t what, bool keep = false);
void askYes(Session& s, uint8_t what, const char* name);
void listPending(Bbs& b, Session& s, uint8_t area);
void filesHelp(Bbs& b, Session& s, uint8_t area);
bool doApproveQuiet(Bbs& b, Session& s, uint8_t area, const char* name);
bool doRejectQuiet(Bbs& b, Session& s, uint8_t area, const char* name);
void backToArea(Bbs& b, Session& s);
void filesPrompt(Session& s);          // defined below, used by listDone

// listDone: a listing has finished, one way or the other.
//
// The prompt is emitted as the last row of a listing that runs to the end,
// which works right up until somebody presses Q at [More]. Then the row is
// never reached, the core hands the session back here without drawing a
// shell prompt, and the caller is left staring at "Stopped." with no way to
// know the file areas still have them.
//
// Only on an abort: a listing that finished has already drawn its prompt,
// and drawing a second one would just be a blank line and a repeat.
void listDone(Session& s, bool aborted) {
    if (aborted) filesPrompt(s);
}

void onLogoff(Session& s) {
    xferDropped(s);
    g_at[slotOf(s)]    = 0xFF;
    g_where[slotOf(s)]   = Where::Out;
    g_sel[slotOf(s)]     = 0;
    g_ask[slotOf(s)]     = AskNone;
    g_menuRows[slotOf(s)] = 0;
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

    uint8_t rows = 1;                     // the title bar
    b.rowTitle(s, "File areas");
    if (!n) {
        t.color(tl, Color::Grey);
        t.text(tl, "Nothing here yet. The sysop sets areas up in CONFIG.");
        t.nl(tl);
        g_menuRows[slot] = 2;
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
        // The last cell on a row is padded to the name width rather than the
        // full cell, so the bar still looks deliberate without ever reaching
        // the final column. Filling the final column makes the terminal wrap,
        // and the newline that follows then lands as a blank row.
        //
        // It is padded whether or not it is highlighted, and that matters:
        // the width has to be the same either way. It used to be widest + 8
        // when highlighted and nothing at all when not, which was invisible
        // while every redraw cleared the screen first. Once the menu started
        // drawing over itself in place, moving the bar OFF the last cell
        // printed a shorter line than the one underneath and left the old
        // reverse-video block sitting on the screen to the right of it.
        int pad = last ? static_cast<int>(widest + 8)
                       : static_cast<int>(widest + 10);
        snprintf(buf, sizeof(buf), "%2u  %-*.*s", static_cast<unsigned>(a + 1),
                 pad, static_cast<int>(widest + 8), nm);
        if (here) t.reverse(tl, true);
        t.text(tl, buf);
        if (here) t.reverse(tl, false);
        if (last) { t.nl(tl); ++rows; }
    }
    g_menuRows[slot] = rows;
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
    (void)wide;
    if (g_where[slotOf(s)] == Where::Area) {
        // What you can do here, then the prompt itself. Only what this
        // caller may actually do: a section they cannot upload to should
        // not advertise U at them and then refuse it.
        uint8_t at = g_at[slotOf(s)];
        bool up = at != 0xFF && mayUp(s, at);
        bool dl = at != 0xFF && mayDel(s, at);
        char line[96];
        if (t.cols() >= 60)
            snprintf(line, sizeof(line),
                     "L lists, a number downloads%s%s, ? help, Q/ESC back",
                     up ? ", U uploads, D describes" : "",
                     dl ? ", P pending" : "");
        else
            snprintf(line, sizeof(line), "L list, number gets%s%s, ? help, Q/ESC",
                     up ? ", U send" : "", dl ? ", P pend" : "");
        t.text(tl, line);
        t.nl(tl);
        // [S1] Files> : the section you are in, the way a node number is
        // shown everywhere else on the board.
        t.color(tl, Color::LightGreen);
        snprintf(line, sizeof(line), "[S%u] ", static_cast<unsigned>(at + 1));
        t.text(tl, line);
        t.color(tl, Color::White);
        t.text(tl, "Files> ");
        return;                      // no trailing newline: this is a prompt
    }
    else if (canPoint(s) && wide)
        t.text(tl, "Files: cursor keys and Enter, or a number. Q quits");
    else if (canPoint(s) && wide)
        t.text(tl, "Files: cursor keys and Enter, or a number. Q/ESC quits");
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
    unsigned dw   = cols > 25 ? cols - 24 : 1;   // 3 more for the number
    if (dw > kDescMax) dw = kDescMax;
    // Numbered, because the number is how a caller picks a file. An
    // unnumbered listing followed by "type the name" is asking somebody to
    // retype what is already on their screen.
    snprintf(buf, sizeof(buf), "%2u %-12.12s %5luK %-*.*s",
             static_cast<unsigned>(want + 1), fname, kb,
             static_cast<int>(dw), static_cast<int>(dw), desc);
    b.rowText(s, Color::LightGrey, buf);
    return true;
}

// ---------------------------------------------------------------------------
// enter / leave / listArea: the subsystem.
// ---------------------------------------------------------------------------
// How the menu gets onto the screen.
//
//   Fresh   clear first: coming in from somewhere else
//   Keep    no clear: a door screen has just played and wiping it would be
//           the same as not having one
//   Again   draw over the rows it drew last time, touching nothing above
//
// Again is what a cursor key uses. Clearing on every keypress is what made
// the door banner flash away the moment anybody moved the highlight, and
// Rob's instinct was right: only the part below the header should refresh.
// The rows are identical in length between redraws, only the highlight
// moves, so drawing over them leaves no tails behind.
enum class Draw : uint8_t { Fresh, Keep, Again };

void showMenu(Bbs& b, Session& s, Draw how = Draw::Fresh) {
    // g_at is deliberately left alone: it is the area this caller last
    // opened, not where they are standing, and DESC at the command prompt
    // reads it. g_where is the one that says where they are.
    uint8_t slot = slotOf(s);
    g_where[slot] = Where::Menu;

    // +2 for the blank line and the hint line filesPrompt adds under the
    // menu. Only a terminal that can move its cursor can do this; anything
    // else falls back to a clear, which is what it always did.
    if (how == Draw::Again && canPoint(s) && g_menuRows[slot])
        s.term.up(s.tl, static_cast<uint8_t>(g_menuRows[slot] + 2));
    else if (how == Draw::Fresh)
        s.term.cls(s.tl);

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

// leave: hand the caller back to the shell, and say where they are going
// rather than what they have stopped doing.
//
// This said "Out of files.", which reads as the board having run out of
// them rather than as the caller stepping out of a room. A line a caller
// sees on the way out of every subsystem is worth getting right, and the
// forums will say the same thing in their own words.
void leave(Bbs& b, Session& s) {
    g_where[slotOf(s)] = Where::Out;
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, "Leaving the file areas. Returning to the BBS...");
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
    // No question carried in from last time. A caller can leave the file
    // areas without answering one: a config save hands them home, and a
    // sysop answering a ring is taken straight to the room (1.1.0). Without
    // this their next visit's keys went to a question nobody could see.
    g_ask[slotOf(s)] = AskNone;
    s.term.reset(s.tl);
    // A board with screens/files gets a way in. Without one the caller lands
    // on the menu, the same deal the chat room has with chatin.
    s.term.cls(s.tl);
    if (b.showScreen(s, "files")) s.term.nl(s.tl);
    showMenu(b, s, Draw::Keep);     // the screen just played: do not wipe it
}

// onKey: the subsystem's keys. Digits pick an area, and they accumulate so
// area 12 is reachable on a board with twelve of them: a single keypress
// would cap the whole design at nine.
void onKey(Session& s, int k, uint32_t now) {
    Bbs& b = Bbs::instance();
    uint8_t slot = slotOf(s);

    // A question is open: the line editor owns the keys until it says
    // otherwise. Same editor the shell uses, so backspace and the abort
    // keys do what a caller already expects.
    // A yes/no dialog is open: one key answers it and nothing else does.
    if (g_ask[slot] == AskYesDown || g_ask[slot] == AskYesDel) {
        uint8_t what = g_ask[slot];
        g_ask[slot] = AskNone;
        bool xmo = (what == AskYesDown && (k == 'x' || k == 'X'));
        // Enter means yes for a download and no for an erase. The default
        // on a destructive question should never be the destructive one.
        bool yes = xmo || k == 'y' || k == 'Y' ||
                   (k == KEY_ENTER && what == AskYesDown);
        s.term.text(s.tl, xmo ? "X" : (yes ? "Y" : "N"));
        s.term.nl(s.tl);
        if (!yes) { backToArea(b, s); return; }
        if (what == AskYesDown) {
            char arg[kDescMax + 8];
            snprintf(arg, sizeof(arg), "%.48s%s", g_pend[slot], xmo ? " X" : "");
            startSend(b, s, arg, now);
        } else {
            doErase(b, s, g_pend[slot]);
        }
        return;
    }

    // A typed question is open: the line editor owns the keys until it says
    // otherwise. The same editor the shell uses, so backspace and the abort
    // keys do what a caller already expects.
    if (g_ask[slot] != AskNone) {
        LineEditor::Res r = s.ed.key(k, s.term, s.tl);
        if (r == LineEditor::Res::Editing) return;
        uint8_t what = g_ask[slot];
        g_ask[slot] = AskNone;
        if (r == LineEditor::Res::Abort) { backToArea(b, s); return; }

        char answer[kDescMax + 1];
        snprintf(answer, sizeof(answer), "%s", s.ed.text());
        s.term.nl(s.tl);

        // A lone Q backs out, because the prompt line still on the screen
        // above says "Q/ESC back" and a caller has no way to know that
        // stopped being true once a question opened. ESC always worked; Q
        // was being typed into the answer, so the board looked deaf to the
        // key it had just advertised. No answer here is ever a bare Q.
        if ((answer[0] == 'q' || answer[0] == 'Q') && !answer[1]) {
            backToArea(b, s);
            return;
        }

        uint8_t at = g_at[slot];
        char name[kDescMax + 1];
        switch (what) {
        case AskDescNum: {
            long want = strtol(answer, nullptr, 10);
            if (want < 1 || !nthFile(at, static_cast<uint16_t>(want),
                                     name, sizeof(name))) {
                s.term.color(s.tl, Color::LightRed);
                s.term.text(s.tl, "No file with that number. L lists them.");
                backToArea(b, s);
                return;
            }
            snprintf(g_pend[slot], sizeof(g_pend[0]), "%s", name);
            askFor(s, AskDescText);
            return;
        }
        case AskUpDesc: {
            uint8_t area = g_pendArea[slot];
            char pd[160], dir[128];
            if (!pendPath(area, pd, sizeof(pd)) ||
                !areaPath(area, dir, sizeof(dir))) { backToArea(b, s); return; }

            // The description lives beside the file while it waits, in the
            // staging folder's own FILES.BBS. Same format, same code, and a
            // sysop with the card in a laptop can read both.
            if (answer[0]) setDesc(pd, g_pend[slot], answer);

            // Somebody who can approve does not queue behind themselves.
            // Making a sysop approve their own upload is ceremony, not
            // review, and ceremony is what stops people using a thing.
            if (mayDel(s, area)) {
                if (doApproveQuiet(b, s, area, g_pend[slot])) {
                    if (answer[0]) setDesc(dir, g_pend[slot], answer);
                    setDesc(pd, g_pend[slot], "");       // no longer waiting
                    s.term.color(s.tl, Color::LightGreen);
                    char msg[80];
                    snprintf(msg, sizeof(msg), "%.40s is live.", g_pend[slot]);
                    s.term.text(s.tl, msg);
                } else {
                    s.term.color(s.tl, Color::LightRed);
                    s.term.text(s.tl, "Could not make it live. It is still waiting.");
                }
            } else {
                s.term.color(s.tl, Color::Yellow);
                s.term.text(s.tl,
                    "Thanks. Staff will look at it before anyone else sees it.");
            }
            backToArea(b, s);
            return;
        }
        case AskDescText: {
            char dir[128];
            if (!areaPath(at, dir, sizeof(dir))) { backToArea(b, s); return; }
            bool ok = setDesc(dir, g_pend[slot], answer);
            s.term.color(s.tl, ok ? Color::LightGreen : Color::LightRed);
            s.term.text(s.tl, ok ? (answer[0] ? "Described." : "Description cleared.")
                                 : "Could not write the description file.");
            backToArea(b, s);
            return;
        }
        case AskNum:
        case AskDelNum: {
            long want = strtol(answer, nullptr, 10);
            if (want < 1 || !nthFile(at, static_cast<uint16_t>(want),
                                     name, sizeof(name))) {
                s.term.color(s.tl, Color::LightRed);
                s.term.text(s.tl, "No file with that number. L lists them.");
                backToArea(b, s);
                return;
            }
            askYes(s, what == AskNum ? AskYesDown : AskYesDel, name);
            return;
        }
        // An empty answer is the YMODEM case, which is the whole reason the
        // question says "Enter alone": the sending terminal already knows
        // the filename and there is no reason to make somebody type it.
        case AskUp:      startRecv(b, s, answer, now); break;

        // Numbers, not filenames. A sysop clearing a queue should not be
        // retyping names off their own screen, and "A" doing the lot is the
        // whole reason the queue is worth having.
        case AskApprove:
        case AskReject: {
            bool all = answer[0] == 'a' || answer[0] == 'A';
            bool ok  = false;
            uint16_t done = 0;
            if (all) {
                // Walked from the top each time, because each one removes
                // itself from the folder as it goes.
                while (nthPending(at, 1, name, sizeof(name))) {
                    if (what == AskApprove) doApproveQuiet(b, s, at, name);
                    else                    doRejectQuiet(b, s, at, name);
                    if (++done >= kMaxPendPerArea) break;
                }
                char msg[80];
                snprintf(msg, sizeof(msg), "%u upload%s %s.",
                         static_cast<unsigned>(done), done == 1 ? "" : "s",
                         what == AskApprove ? "approved" : "rejected");
                s.term.color(s.tl, done ? Color::LightGreen : Color::Grey);
                s.term.text(s.tl, done ? msg : "Nothing was waiting.");
                backToArea(b, s);
                return;
            }
            long want = strtol(answer, nullptr, 10);
            ok = want >= 1 && nthPending(at, static_cast<uint16_t>(want),
                                         name, sizeof(name));
            if (!ok) {
                s.term.color(s.tl, Color::LightRed);
                s.term.text(s.tl, "No upload with that number. P lists them.");
                backToArea(b, s);
                return;
            }
            if (what == AskApprove) doApprove(b, s, name);
            else                    doReject(b, s, name);
            return;
        }
        default:         backToArea(b, s); break;
        }
        return;
    }

    // The section prompt. Only inside a section: at the section menu a
    // letter is not a file operation, it is a mistyped number.
    if (g_where[slot] == Where::Area) {
        uint8_t at = g_at[slot];
        if (k >= '1' && k <= '9' && mayDown(s, at)) {
            // The digit they pressed starts the number, so one keypress is
            // enough for the first nine files and a second digit still
            // reaches the rest.
            askFor(s, AskNum);
            s.ed.key(k, s.term, s.tl);
            return;
        }
        if ((k == 'u' || k == 'U') && mayUp(s, at))  { askFor(s, AskUp); return; }
        if ((k == 'e' || k == 'E') && mayDel(s, at)) { askFor(s, AskDelNum); return; }
        if ((k == 'a' || k == 'A') && mayDel(s, at)) { askFor(s, AskApprove); return; }
        if ((k == 'r' || k == 'R') && mayDel(s, at)) { askFor(s, AskReject); return; }
        if ((k == 'd' || k == 'D') && mayWrite(s, at)) {
            askFor(s, AskDescNum); return;
        }
        if ((k == 'p' || k == 'P') && mayDel(s, at)) {
            s.term.cls(s.tl);
            listPending(b, s, at);
            filesPrompt(s);
            return;
        }
        if (k == '?') {
            s.term.cls(s.tl);
            filesHelp(b, s, at);
            filesPrompt(s);
            return;
        }
    }

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
    if (k == '?' && g_where[slot] == Where::Menu) {
        s.term.cls(s.tl);
        b.rowTitle(s, "File sections");
        b.rowText(s, Color::White, "1 2 3    a number opens that section");
        if (canPoint(s))
            b.rowText(s, Color::White, "cursors  move the bar, Enter opens");
        b.rowText(s, Color::White, "?        this");
        b.rowText(s, Color::White, "Q  ESC   leave the file areas");
        b.rowRule(s);
        showMenu(b, s, Draw::Keep);
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
        showMenu(b, s, Draw::Again);      // over the menu, not over the banner
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
    // Who holds it is claims::Res::Transfer, keyed by node. This pointer is
    // the data path only: the engine has to push bytes into a specific
    // session's timeline. It is never the test for "is a transfer running",
    // because a Session* from a static pool is exactly the thing that goes
    // stale, and a stale one here had the board pushing a file at whoever
    // dialled in next.
    Session* s      = nullptr;
    FILE*    fp     = nullptr;
    bool     sending = false;
    uint8_t  area   = 0xFF;
    uint32_t started = 0;
    bool     ymodem = false;       // block 0 carried the name and the size
    uint32_t written = 0;          // receiving: bytes accepted, for the cap
    char     name[kDescMax + 1] = {};
};

Xfer            g_x;
xmodem::Engine  g_eng;

// Why the board said no, in words, for after the line comes out of binary.
//
// A YMODEM receiver that refuses a file can only say so with a CAN, and the
// far end renders that as "Canceled remotely" with no reason at all. That is
// exactly what a sysop retrying an upload sees when an earlier attempt left
// a file of the same name waiting: the board knows precisely what is wrong
// and had no way to say it. Kept here and printed once the terminal can read
// text again.
char g_why[96] = {};

// How many uploads are waiting on staff, board-wide. Counted at start and on
// mount, then kept by hand as uploads arrive and are dealt with, so telling a
// staff member at login costs no reads at all. Same reasoning as chat's "you
// have mail". A card edited on a laptop goes stale until the next start,
// which is the accepted trade for a login that does not touch the card.
uint16_t g_pending = 0;

#ifdef BBS_HAS_LCD
}   // namespace

// The board's display shows an upload glyph while this is above 0
// (panel_feed.h). The same figure staff are told at login, so no read.
uint16_t files::pendingCount() { return g_pending; }

namespace {
#endif

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
#ifdef BBS_XFER_DIAG
    plat::log("xfer: block accepted, %u bytes", static_cast<unsigned>(len));
#endif
    if (!x->fp) return false;
    if (x->written + len > kMaxUploadBytes) {
        plat::log("files: upload %s refused, over the size cap", x->name);
        return false;
    }
    if (fwrite(src, 1, len, x->fp) != len) return false;
    x->written += len;
    return true;
}

// xferOpen: YMODEM block 0 has named a file. This is the first moment the
// board learns what is being sent, and the name came off the wire from a
// caller, so it is checked exactly as hard as one typed at a prompt.
//
// Returning false aborts the transfer with SinkFailed, which is the right
// answer for every refusal here: the caller is told the upload failed rather
// than discovering later that the board quietly renamed or dropped it.
bool xferOpen(void* ctx, const char* name, uint32_t size) {
    Xfer* x = static_cast<Xfer*>(ctx);
#ifdef BBS_XFER_DIAG
    plat::log("xfer: block 0 named [%s] size %lu", name,
              static_cast<unsigned long>(size));
#endif
    char dir[128], pd[160], full[224];
    struct stat st;

    if (!safeName(name)) {
        snprintf(g_why, sizeof(g_why), "that name is not one the board will store");
        plat::log("files: upload refused, unsafe name from block 0");
        return false;
    }
    // The catalogue is the area's, not a caller's to overwrite.
    if (ieq(name, BBS_FILES_DESC) || ieq(name, kPendList)) {
        snprintf(g_why, sizeof(g_why), "that name belongs to the section itself");
        return false;
    }
    if (size > kMaxUploadBytes) {
        snprintf(g_why, sizeof(g_why), "%lu bytes is over the %lu byte limit",
                 static_cast<unsigned long>(size),
                 static_cast<unsigned long>(kMaxUploadBytes));
        plat::log("files: upload %s refused, block 0 says %lu bytes",
                  name, static_cast<unsigned long>(size));
        return false;
    }
    if (!areaPath(x->area, dir, sizeof(dir))) return false;
    snprintf(full, sizeof(full), "%s/%.48s", dir, name);
    if (stat(full, &st) == 0) {
        snprintf(g_why, sizeof(g_why), "%.40s is already in this section", name);
        return false;
    }

    if (!pendPath(x->area, pd, sizeof(pd))) return false;
    mkdir(pd, 0755);
    snprintf(full, sizeof(full), "%s/%.48s", pd, name);
    if (stat(full, &st) == 0) {
        // The one a sysop hits constantly while testing: an earlier attempt
        // that failed still left its name in the staging folder.
        snprintf(g_why, sizeof(g_why),
                 "%.36s is already waiting for approval. E erases it.", name);
        return false;
    }

    x->fp = fopen(full, "wb");
    if (!x->fp) return false;
    snprintf(x->name, sizeof(x->name), "%.48s", name);
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
    s.tn.setBinary(s.tl, false);
    s.term.reset(s.tl);
    s.term.nl(s.tl);

    if (g_eng.done()) {
        unsigned kb = static_cast<unsigned>((g_eng.bytes() + 512) / 1024);
        snprintf(buf, sizeof(buf), "%s complete: %s, %u KB in %u blocks, %s%s",
                 g_x.sending ? "Download" : "Upload", g_x.name, kb,
                 static_cast<unsigned>(g_eng.blocks()),
                 g_x.ymodem ? "YMODEM" : "XMODEM",
                 g_eng.crcMode() ? ", CRC" : ", checksum");
        s.term.color(s.tl, Color::LightGreen);
    } else {
        snprintf(buf, sizeof(buf), "%s failed: %s",
                 g_x.sending ? "Download" : "Upload", g_eng.errorText());
        s.term.color(s.tl, Color::LightRed);
        if (g_why[0]) {
            s.term.text(s.tl, buf);
            s.term.nl(s.tl);
            snprintf(buf, sizeof(buf), "%s", g_why);
        }
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

    bool    upOk   = !g_x.sending && g_eng.done() && g_x.area != 0xFF;
    uint8_t upArea = g_x.area;
    char    upName[kDescMax + 1];
    snprintf(upName, sizeof(upName), "%s", g_x.name);

    g_eng.reset();
    if (g_x.s) claims::release(claims::Res::Transfer, g_x.s->id);
    g_x = Xfer();
    g_why[0] = 0;

    // A file with no description is a filename in a list, which tells the
    // next caller nothing. Ask while the person who chose it is still here:
    // asking later means asking somebody who has moved on, and a sysop
    // writing descriptions for other people's uploads is how a file area
    // stops being maintained.
    if (upOk) {
        uint8_t slot = slotOf(s);
        snprintf(g_pend[slot], sizeof(g_pend[0]), "%s", upName);
        g_pendArea[slot] = upArea;
        askFor(s, AskUpDesc);
        return;
    }

    // Back exactly where they were, which for a transfer means the area
    // they were standing in rather than the menu above it. Somebody who
    // just fetched one file usually wants the next one, and sending them up
    // a level to walk back down is the kind of small rudeness that makes a
    // board feel unfinished.
    switch (g_where[slotOf(s)]) {
    case Where::Area: filesPrompt(s); break;
    case Where::Menu: showMenu(b, s); break;
    default:          b.prompt(s);    break;
    }
}

// xferDrive: one pass of feed, tick and pull. Both entry points share it so
// there is one order of operations rather than two that can drift.
//
// With BBS_XFER_DIAG defined it also narrates, because two rounds of
// reasoning from screenshots got the wrong answer and each one cost a flash.
// A terminal window shows what the far end thinks happened; this shows what
// the board actually received and what the engine made of it.
void xferDrive(Session& s, const uint8_t* in, size_t n, uint32_t now) {
#ifdef BBS_XFER_DIAG
    static uint32_t fed = 0;
    if (in && n) {
        fed += n;
        char hex[64] = {};
        size_t show = n < 12 ? n : 12;
        for (size_t i = 0; i < show; ++i)
            snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02X ", in[i]);
        plat::log("xfer: in %u (total %lu) [%s] st=%d err=%d blk=%lu",
                  static_cast<unsigned>(n), static_cast<unsigned long>(fed), hex,
                  static_cast<int>(g_eng.status()), static_cast<int>(g_eng.error()),
                  static_cast<unsigned long>(g_eng.blocks()));
    }
#endif
    if (in && n) g_eng.feed(in, n, now);
    g_eng.tick(now);
    size_t before = s.tl.freeBytes();
    xferPump(s, now);
#ifdef BBS_XFER_DIAG
    if (before != s.tl.freeBytes())
        plat::log("xfer: out %u bytes, st=%d err=%d errs=%u",
                  static_cast<unsigned>(before - s.tl.freeBytes()),
                  static_cast<int>(g_eng.status()), static_cast<int>(g_eng.error()),
                  static_cast<unsigned>(g_eng.errors()));
#else
    (void)before;
#endif
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
    if (g_x.s) claims::release(claims::Res::Transfer, g_x.s->id);
    g_x = Xfer();
    g_why[0] = 0;
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
        backToArea(b, s);
        return;
    }
    if (!mayDown(s, at)) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "This area is not yours to download from.");
        backToArea(b, s);
        return;
    }
    if (claims::held(claims::Res::Transfer)) {
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, "Somebody is transferring right now. Try in a moment.");
        backToArea(b, s);
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
        backToArea(b, s);
        return;
    }

    // A second word picks the protocol. YMODEM is the default because it is
    // the one that does not damage the file: XMODEM has no length field, so
    // it pads the last block with 0x1A and the caller receives a file up to
    // 1023 bytes longer than the one on the card. Anyone whose terminal only
    // speaks XMODEM asks for it by name.
    bool useY = true;
    while (*r == ' ') ++r;
    if (*r == 'x' || *r == 'X') useY = false;

    snprintf(buf, sizeof(buf), "%s/%.48s", dir, name);
    FILE* fp = fopen(buf, "rb");
    if (!fp) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "No such file in this area.");
        backToArea(b, s);
        return;
    }
    struct stat sst;
    uint32_t fsize = 0;
    if (stat(buf, &sst) == 0) fsize = static_cast<uint32_t>(sst.st_size);

    if (!b.own(s, g_index)) { fclose(fp); backToArea(b, s); return; }
    b.setDoing(s, "DOWNLOAD");

    g_x.s       = &s;
    claims::take(claims::Res::Transfer, s.id);
    g_x.fp      = fp;
    g_x.sending = true;
    g_x.ymodem  = useY;
    g_x.area    = at;
    g_x.started = now;
    snprintf(g_x.name, sizeof(g_x.name), "%.48s", name);

    // Told before the line goes binary, because after this the caller's
    // terminal is a transfer and nothing readable reaches it again until
    // the transfer ends.
    s.term.color(s.tl, Color::Cyan);
    snprintf(buf, sizeof(buf), "Sending %.48s. Start your %s receive now.",
             name, useY ? "YMODEM" : "XMODEM");
    s.term.text(s.tl, buf);
    s.term.nl(s.tl);
    if (useY) {
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, "DOWNLOAD <file> X if your terminal only does XMODEM.");
        s.term.nl(s.tl);
    }
    s.term.reset(s.tl);

    s.tn.setBinary(s.tl, true);  // and the far end stops padding CRs
    b.setRawInput(s, true);      // and 0x1B is data, not an escape
    if (useY) g_eng.beginSendY(xferRead, &g_x, name, fsize, now, true);
    else      g_eng.beginSend(xferRead, &g_x, now, true);
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
        backToArea(b, s);
        return;
    }
    if (!mayUp(s, at)) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "This area is not yours to upload to.");
        backToArea(b, s);
        return;
    }
    if (claims::held(claims::Res::Transfer)) {
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, "Somebody is transferring right now. Try in a moment.");
        backToArea(b, s);
        return;
    }
    if (countPending(at) >= kMaxPendPerArea) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "This area has all the uploads it can hold until staff clear some.");
        backToArea(b, s);
        return;
    }

    // No name means YMODEM: block 0 carries it, so the caller does not have
    // to type what their own terminal already knows. A name means the
    // caller's terminal only speaks XMODEM, which has no filename on the
    // wire and therefore needs one here.
    char name[kDescMax + 1] = {};
    bool useY = !argName(arg, name, sizeof(name));

    if (useY) {
        if (!b.own(s, g_index)) { backToArea(b, s); return; }
        b.setDoing(s, "UPLOAD");
        g_x.s       = &s;
        claims::take(claims::Res::Transfer, s.id);
        g_x.fp      = nullptr;            // xferOpen opens it when block 0 lands
        g_x.sending = false;
        g_x.ymodem  = true;
        g_x.area    = at;
        g_x.started = now;
        g_x.written = 0;
        snprintf(g_x.name, sizeof(g_x.name), "%s", "(unnamed)");

        s.term.color(s.tl, Color::Cyan);
        s.term.text(s.tl, "Ready. Start your YMODEM send now.");
        s.term.nl(s.tl);
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, "It waits for staff approval before anyone else sees it.");
        s.term.nl(s.tl);
        s.term.reset(s.tl);

        s.tn.setBinary(s.tl, true);
        b.setRawInput(s, true);
        g_eng.beginRecvY(xferOpen, xferWrite, &g_x, now);
        return;
    }
    // Never let an upload be named as the catalogue: approving it would
    // overwrite every description in the area.
    if (ieq(name, BBS_FILES_DESC) || ieq(name, kPendList)) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "That name belongs to the area. Pick another.");
        backToArea(b, s);
        return;
    }

    struct stat st;
    snprintf(buf, sizeof(buf), "%s/%s", dir, name);
    if (stat(buf, &st) == 0) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "There is already a file by that name here.");
        backToArea(b, s);
        return;
    }

    char pd[160];
    if (!pendPath(at, pd, sizeof(pd))) { backToArea(b, s); return; }
    mkdir(pd, 0755);                                   // first upload makes it

    snprintf(buf, sizeof(buf), "%s/%.48s", pd, name);
    if (stat(buf, &st) == 0) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl,
            "One by that name is already waiting for approval. E erases it.");
        backToArea(b, s);
        return;
    }
    FILE* fp = fopen(buf, "wb");
    if (!fp) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "Could not open that name on the card.");
        backToArea(b, s);
        return;
    }

    if (!b.own(s, g_index)) { fclose(fp); remove(buf); backToArea(b, s); return; }
    b.setDoing(s, "UPLOAD");

    g_x.s       = &s;
    claims::take(claims::Res::Transfer, s.id);
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

    s.tn.setBinary(s.tl, true);
    b.setRawInput(s, true);
    g_eng.beginRecv(xferWrite, &g_x, now, true);
}

// askFor: put a one-line question under the listing and take an answer.
//
// The same LineEditor the shell uses, so backspace, the abort keys and the
// 40 column behaviour are the ones a caller already knows. Nothing here
// leaves the file area: the answer comes back to onKey and the caller is
// still standing in the same place afterwards, which is the whole point of
// FILES being somewhere you are rather than a command you ran.
void askFor(Session& s, uint8_t what, bool keep) {
    const char* q = "";
    switch (what) {
    case AskNum:     q = "File number: ";                  break;
    case AskDelNum:  q = "Erase which number? ";           break;
    case AskUp:      q = s.term.cols() >= 60
                         ? "Upload (Enter alone = YMODEM, or type a name): "
                         : "Upload (Enter = YMODEM): ";    break;
    case AskApprove: q = "Approve which number? (A = all) ";  break;
    case AskReject:  q = "Reject which number? (A = all) ";   break;
    case AskDescNum: q = "Describe which number? ";           break;
    case AskDescText: q = "Description (empty clears it): ";  break;
    case AskUpDesc:  q = "Describe it for the file list: ";   break;
    default:         q = "? ";                             break;
    }

    g_ask[slotOf(s)] = what;
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::Cyan);
    s.term.text(s.tl, q);
    s.term.color(s.tl, Color::White);
    // keep: asked again after a notice (1.1.0), over what was being typed.
    if (keep && s.ed.active()) { s.ed.redraw(s.term, s.tl); return; }
    s.ed.begin(kDescMax, 0);
}

// askYes: the one-key confirmation, which is the "dialog" a caller sees
// before a transfer starts. Named rather than numbered on purpose: a number
// is easy to miscount off a listing, and a filename is not.
void askYes(Session& s, uint8_t what, const char* name) {
    char q[96];
    uint8_t slot = slotOf(s);
    snprintf(g_pend[slot], sizeof(g_pend[0]), "%s", name);
    g_ask[slot] = what;
    // The download dialog is also where the protocol is chosen, now that
    // there is no typed filename to hang a flag off. Y is YMODEM because
    // YMODEM is the one that does not pad the file; X is there for
    // terminals that only speak XMODEM.
    if (what == AskYesDown)
        snprintf(q, sizeof(q), "Download %.48s?  [Y]es  [X]modem  [N]o ", name);
    else
        snprintf(q, sizeof(q), "Erase %.48s? [y/N] ", name);
    s.term.nl(s.tl);
    s.term.color(s.tl, what == AskYesDown ? Color::Cyan : Color::LightRed);
    s.term.text(s.tl, q);
    s.term.color(s.tl, Color::White);
}

// backToArea: say something and leave the caller where they were.
//
// Every refusal in here used to call Bbs::prompt, which drops the caller at
// the shell. That is right for a command and wrong for a door: being told
// "no such file" should not also throw you out of the room.
void backToArea(Bbs& b, Session& s) {
    if (g_where[slotOf(s)] == Where::Out) { b.prompt(s); return; }
    s.term.nl(s.tl);
    filesPrompt(s);
}

// doApproveQuiet / doRejectQuiet: the file operation with nothing said.
//
// Approving twenty uploads should report "20 approved", not twenty lines of
// "X is live". The single-file forms keep their own messages because there
// the message is the whole feedback.
bool doApproveQuiet(Bbs& b, Session& s, uint8_t area, const char* name) {
    (void)b;
    char dir[128], pd[160], from[224], to[224];
    if (!areaPath(area, dir, sizeof(dir))) return false;
    if (!pendPath(area, pd, sizeof(pd))) return false;
    snprintf(from, sizeof(from), "%s/%.48s", pd, name);
    snprintf(to,   sizeof(to),   "%s/%.48s", dir, name);
    if (rename(from, to) != 0) return false;
    if (g_pending) --g_pending;
    plat::log("files: %s approved %s into area %u",
              s.user, name, static_cast<unsigned>(area + 1));
    return true;
}

bool doRejectQuiet(Bbs& b, Session& s, uint8_t area, const char* name) {
    (void)b;
    char pd[160], full[224];
    if (!pendPath(area, pd, sizeof(pd))) return false;
    snprintf(full, sizeof(full), "%s/%.48s", pd, name);
    if (remove(full) != 0) return false;
    if (g_pending) --g_pending;
    plat::log("files: %s rejected %s in area %u",
              s.user, name, static_cast<unsigned>(area + 1));
    return true;
}

// listPending: what is waiting in this section, numbered so staff never
// type a filename. Capped by kMaxPendPerArea, so it always fits a screen
// and never needs the pager.
void listPending(Bbs& b, Session& s, uint8_t area) {
    char pd[160], full[224], buf[120];
    struct stat st;
    uint16_t n = 0;

    b.rowTitle(s, "Waiting for approval");
    if (pendPath(area, pd, sizeof(pd))) {
        DIR* d = opendir(pd);
        if (d) {
            struct dirent* e;
            while ((e = readdir(d)) != nullptr) {
                if (e->d_name[0] == '.') continue;
                if (ieq(e->d_name, kPendList)) continue;
                unsigned long kb = 0;
                snprintf(full, sizeof(full), "%s/%.48s", pd, e->d_name);
                if (stat(full, &st) == 0)
                    kb = (static_cast<unsigned long>(st.st_size) + 1023u) / 1024u;
                snprintf(buf, sizeof(buf), "%2u %-24.24s %5luK",
                         static_cast<unsigned>(++n), e->d_name, kb);
                b.rowText(s, Color::White, buf);
            }
            closedir(d);
        }
    }
    if (!n) b.rowText(s, Color::Grey, "Nothing waiting in this section.");
    else    b.rowText(s, Color::Grey, "A approves, R rejects. A number or A for all.");
    b.rowRule(s);
}

// filesHelp: what the keys do, on the one screen they apply to. A caller
// who has to leave a place to find out how to use it has been failed by it.
void filesHelp(Bbs& b, Session& s, uint8_t area) {
    bool up = area != 0xFF && mayUp(s, area);
    bool dl = area != 0xFF && mayDel(s, area);
    b.rowTitle(s, "Files: what the keys do");
    b.rowText(s, Color::White, "L        list this section's files, numbered");
    b.rowText(s, Color::White, "1 2 3    a number picks that file to download");
    if (up) {
        b.rowText(s, Color::White, "U        send a file to this section");
        b.rowText(s, Color::White, "D        describe a file by number");
    }
    if (dl) {
        b.rowText(s, Color::White, "P        list uploads waiting for approval");
        b.rowText(s, Color::White, "A        approve one by number, or A for all");
        b.rowText(s, Color::White, "R        reject one by number, or A for all");
        b.rowText(s, Color::White, "E        erase a file by number");
    }
    b.rowText(s, Color::White, "?        this");
    b.rowText(s, Color::White, "Q  ESC   back one level, again to leave");
    b.rowRule(s);
    b.rowText(s, Color::Grey, "Downloads offer YMODEM, or X for plain XMODEM.");
    if (up)
        b.rowText(s, Color::Grey,
                  "Uploads wait for staff before anyone else can see them.");
}

// ---------------------------------------------------------------------------
// The three staff actions, as functions rather than command bodies.
//
// A key pressed in the area and a command typed at the shell both land
// here, so there is one implementation of each and they cannot drift.
// ---------------------------------------------------------------------------
void doApprove(Bbs& b, Session& s, const char* a) {
          char buf[192], pd[160], from[224], to[224];
          char name[kDescMax + 1] = {};
          uint8_t at = g_at[slotOf(s)];
          char dir[128];
          if (at == 0xFF || !areaPath(at, dir, sizeof(dir))) {
              s.term.color(s.tl, Color::Grey);
              s.term.text(s.tl, "Open the area first with FILES n.");
              backToArea(b, s);
              return;
          }
          if (!mayDel(s, at)) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "Approving in this area is not yours to do.");
              backToArea(b, s);
              return;
          }
          if (!argName(a, name, sizeof(name)) || !pendPath(at, pd, sizeof(pd))) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "APPROVE <file>");
              backToArea(b, s);
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
              backToArea(b, s);
              return;
          }
          if (g_pending) --g_pending;
          // The uploader's own words go with the file. Losing them at the
          // approval step would mean the description was only ever worth
          // asking for from staff.
          char desc[kDescMax + 1];
          findDesc(pd, name, desc, sizeof(desc));
          if (desc[0]) {
              setDesc(dir, name, desc);
              setDesc(pd, name, "");
          }
          plat::log("files: %s approved %s into area %u",
                    s.user, name, static_cast<unsigned>(at + 1));
          s.term.color(s.tl, Color::LightGreen);
          snprintf(buf, sizeof(buf), "%.48s is live. DESC it to say what it is.", name);
          s.term.text(s.tl, buf);
          backToArea(b, s);
}

void doReject(Bbs& b, Session& s, const char* a) {
          char buf[224], pd[160];
          char name[kDescMax + 1] = {};
          uint8_t at = g_at[slotOf(s)];
          char dir[128];
          if (at == 0xFF || !areaPath(at, dir, sizeof(dir))) {
              s.term.color(s.tl, Color::Grey);
              s.term.text(s.tl, "Open the area first with FILES n.");
              backToArea(b, s);
              return;
          }
          if (!mayDel(s, at)) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "Rejecting in this area is not yours to do.");
              backToArea(b, s);
              return;
          }
          if (!argName(a, name, sizeof(name)) || !pendPath(at, pd, sizeof(pd))) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "REJECT <file>");
              backToArea(b, s);
              return;
          }
          snprintf(buf, sizeof(buf), "%s/%s", pd, name);
          if (remove(buf) != 0) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "No upload by that name is waiting here.");
              backToArea(b, s);
              return;
          }
          if (g_pending) --g_pending;
          plat::log("files: %s rejected %s in area %u",
                    s.user, name, static_cast<unsigned>(at + 1));
          s.term.color(s.tl, Color::Yellow);
          snprintf(buf, sizeof(buf), "%s thrown away.", name);
          s.term.text(s.tl, buf);
          backToArea(b, s);
}

void doErase(Bbs& b, Session& s, const char* a) {
          char buf[192];
          uint8_t at = g_at[slotOf(s)];
          char dir[128];
          if (at == 0xFF || !areaPath(at, dir, sizeof(dir))) {
              s.term.color(s.tl, Color::Grey);
              s.term.text(s.tl, "Open an area first with FILES n.");
              backToArea(b, s);
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
              backToArea(b, s);
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
              backToArea(b, s);
              return;
          }
          // Never the description file. FILES.BBS is the area's catalogue,
          // not one of its files, and losing it would silently take every
          // description with it.
          if (ieq(name, BBS_FILES_DESC)) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "That one belongs to the area, not to you.");
              backToArea(b, s);
              return;
          }
          snprintf(buf, sizeof(buf), "%s/%s", dir, name);
          if (remove(buf) != 0) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "No such file in this area.");
              backToArea(b, s);
              return;
          }
          plat::log("files: %s erased %s from area %u",
                    s.user, name, static_cast<unsigned>(at + 1));
          s.term.color(s.tl, Color::LightGreen);
          snprintf(buf, sizeof(buf), "%s erased.", name);
          s.term.text(s.tl, buf);
          backToArea(b, s);
}

// ---------------------------------------------------------------------------
// The commands.
// ---------------------------------------------------------------------------
const Command kCommands[] = {
    { "FILES", "F", 0, CF_READ, "[F]ILES [n]", "the file areas",
      [](Bbs& b, Session& s, const char* a, uint32_t) {
          // A number that names nothing is refused at the shell, before the
          // door opens. Somebody who typed FILES 99 asked for one area, not
          // for the file areas in general, and dropping them inside a
          // subsystem they did not ask for means everything they type next
          // is swallowed by it: "term 80" came back as "File number: 80".
          //
          // An area they may not read is refused in the same words as one
          // that does not exist, so the command cannot be used to find out
          // which numbers are hiding something.
          long n = *a ? strtol(a, nullptr, 10) : 0;
          if (*a && (n < 1 || n > g_areas || !g_area[n - 1].path[0] ||
                     !mayRead(s, static_cast<uint8_t>(n - 1)))) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "No area by that number.");
              b.prompt(s);
              return;
          }
          enter(b, s);
          if (!b.owns(s, g_index) || !*a) return;      // entry refused, or no area asked for
          listArea(b, s, static_cast<uint8_t>(n - 1));
      },
      Menu::Main, 8 },
};

// ---------------------------------------------------------------------------
// liftInput / restoreInput: a page, a broadcast or a ring printed into the
// file areas (1.1.0). Never during a transfer: the line is binary then, and
// a notice would land in the middle of somebody's file. Otherwise the line is
// ended and whatever the caller was at is drawn again underneath: the menu
// with its bar (redrawn whole, because the bar moves by counting rows up and
// the notice has just added some), the section prompt, or the question they
// were answering, with what they had typed.
// ---------------------------------------------------------------------------
bool hookLift(Session& s) {
    if (g_x.s == &s || s.rawInput) return false;
    s.term.reset(s.tl);
    s.term.nl(s.tl);
    return true;
}

void hookRestore(Session& s) {
    Bbs& b = Bbs::instance();
    uint8_t slot = slotOf(s);
    uint8_t a = g_ask[slot];
    if (a == AskYesDown || a == AskYesDel) {
        char name[kDescMax + 1];
        snprintf(name, sizeof(name), "%s", g_pend[slot]);
        askYes(s, a, name);
        return;
    }
    if (a != AskNone) { askFor(s, a, true); return; }
    if (g_where[slot] == Where::Menu) { showMenu(b, s, Draw::Keep); return; }
    filesPrompt(s);
}

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
    nullptr,                 // onRename
    listDone,
    hookLift,                // liftInput: notices reach the file areas (1.1.0)
    hookRestore,             // restoreInput
};
