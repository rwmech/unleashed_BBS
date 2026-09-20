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
constexpr uint8_t kMaxAreas = 8;
constexpr uint8_t kPathMax  = 48;
constexpr uint8_t kNameMax  = 24;
constexpr uint8_t kDescMax  = 48;

struct Area {
    char path[kPathMax + 1] = {};   // relative to the card root
    char name[kNameMax + 1] = {};   // what a caller sees
};

Area    g_area[kMaxAreas];
uint8_t g_areas  = 0;
uint8_t g_index  = 0;

// Which area a caller is listing. Per session, because two callers may be in
// two areas at once, and it is cleared on logoff so the next caller on that
// node does not inherit it: sessions come from a static pool.
uint8_t g_at[BBS_MAX_NODES + 2] = {};

uint8_t slotOf(const Session& s) { return s.id <= BBS_MAX_NODES + 1 ? s.id : 0; }

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
    if (n < 1 || n > kMaxAreas) return;

    const char* bar = strchr(value, '|');
    Area a;
    if (bar) {
        size_t plen = static_cast<size_t>(bar - value);
        while (plen && (value[plen - 1] == ' ' || value[plen - 1] == '\t')) --plen;
        snprintf(a.path, sizeof(a.path), "%.*s", static_cast<int>(plen), value);
        const char* nm = bar + 1;
        while (*nm == ' ' || *nm == '\t') ++nm;
        snprintf(a.name, sizeof(a.name), "%.*s", kNameMax, nm);
    } else {
        snprintf(a.path, sizeof(a.path), "%.*s", kPathMax, value);
        snprintf(a.name, sizeof(a.name), "%.*s", kNameMax, value);
    }
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
    plat::log("files: %u area%s configured, %u folder%s created",
              live, live == 1 ? "" : "s", made, made == 1 ? "" : "s");
    return true;
}

void stop() {}

// A caller leaving takes their place in an area with them. Sessions are a
// static pool, so anything not cleared here is inherited by the next caller
// on that node.
void onLogoff(Session& s) { g_at[slotOf(s)] = 0xFF; }

// ---------------------------------------------------------------------------
// rows: the paged list, both kinds. listIdx is the row, as in the core's own
// list builders, and returning false ends the list.
// ---------------------------------------------------------------------------
bool rows(Session& s) {
    Bbs& b = Bbs::instance();
    char buf[96];
    uint8_t at = g_at[slotOf(s)];

    // ---- the areas -------------------------------------------------------
    if (at == 0xFF) {
        uint8_t i = s.listIdx++;
        if (i == 0) { b.rowTitle(s, "File areas"); return true; }
        uint8_t n = static_cast<uint8_t>(i - 1);
        if (n < g_areas) {
            if (!g_area[n].path[0]) { b.rowText(s, Color::DarkGrey, ""); return true; }
            snprintf(buf, sizeof(buf), "%2u  %-24.24s", static_cast<unsigned>(n + 1),
                     g_area[n].name);
            b.rowText(s, Color::LightGreen, buf);
            return true;
        }
        if (n == g_areas) { b.rowRule(s); return true; }
        if (n == g_areas + 1) {
            b.rowText(s, Color::Grey, g_areas ? "FILES n opens an area."
                                              : "The sysop has not set any up yet.");
            return true;
        }
        return false;
    }

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
        if (want == seen + 1) {
            b.rowText(s, Color::Grey, "FILES on its own goes back to the areas.");
            return true;
        }
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
// The commands.
// ---------------------------------------------------------------------------
const Command kCommands[] = {
    { "FILES", "F", 0, CF_READ, "[F]ILES [n]", "file areas on the card",
      [](Bbs& b, Session& s, const char* a, uint32_t) {
          uint8_t slot = slotOf(s);
          if (!plat::sdBase()[0]) {
              s.term.color(s.tl, Color::Grey);
              s.term.text(s.tl, "No card in the board, so no file areas.");
              b.prompt(s);
              return;
          }
          if (*a) {
              long n = strtol(a, nullptr, 10);
              if (n < 1 || n > g_areas || !g_area[n - 1].path[0]) {
                  s.term.color(s.tl, Color::LightRed);
                  s.term.text(s.tl, "No area by that number.");
                  b.prompt(s);
                  return;
              }
              g_at[slot] = static_cast<uint8_t>(n - 1);
          } else {
              g_at[slot] = 0xFF;                       // back to the area list
          }
          b.setDoing(s, "FILES");
          b.startPluginList(s, g_index);
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
    snprintf(out, n, "%s | %s", g_area[i].path, g_area[i].name);
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
    nullptr,                 // onKey
    status,
    kCommands,
    sizeof(kCommands) / sizeof(kCommands[0]),
    kSettings,
    sizeof(kSettings) / sizeof(kSettings[0]),
    setting,
    rows,
};
