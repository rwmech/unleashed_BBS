// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         host/test_photos_cfg.cpp
// Module:       Host unit test / CONFIG photos' keys against the camera's old
//               lines (1.2.0-link.15)
//
// Purpose:      syscfg::parseFile, alone: a photos_ line in system.cfg wins
//               over the [plugin:camera] line that used to hold the same
//               setting (keep, max, floor, tl_keep, tl_max), in either order
//               in the file; an old line stands in for a photos_ key the
//               file lacks, and photosOld says which it did, so CONFIG
//               photos writes the new key and drops the old line only for
//               those. The CONFIG photos test on the board cannot show the
//               first rule: the page reads the file first.
//
// Targets:      Linux host build
// See also:     src/core/sysconfig.cpp (parseFile, oldPhotoKey), CLAUDE.md
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3 of the License, or (at your
// option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>. The full
// text is in the LICENSE file at the top of this repository.
// ===========================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include "../src/core/silent.h"
#include "../src/core/sysconfig.h"
#include "../src/core/users.h"
#include "../src/platform/platform.h"

static std::string g_dir;

// What parseFile reaches beyond itself, stood in for: no board, no card.
namespace plat {
const char* fsBase()   { return g_dir.c_str(); }
const char* userBase() { return g_dir.c_str(); }
const char* sdBase()   { return ""; }
void log(const char*, ...) {}
void diskPulse(DiskKind) {}
void hostDiskOpen(const char*, const char*) {}
bool onLoop() { return false; }
uint32_t micros() { return 0; }
uint32_t millis() { return 0; }
}
namespace board {
void silentTick(uint32_t, bool) {}
}
namespace users {
uint8_t landFromKey(const char*) { return 0; }
bool validHandle(const char* h) { return h && *h; }
}

static int fails = 0, passes = 0;

static void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passes; else ++fails;
}

// parse: text as a system.cfg, into out. The number of problems.
static int parse(const char* text, SysConfig& out) {
    const std::string p = g_dir + "/system.cfg";
    FILE* f = fopen(p.c_str(), "w");
    if (!f) return -1;
    fputs(text, f);
    fclose(f);
    out = SysConfig();
    char err[160] = "";
    return syscfg::parseFile(p.c_str(), out, err, sizeof(err));
}

int main() {
    char tmpl[] = "/tmp/photoscfg-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 1; }
    g_dir = tmpl;
    printf("CONFIG photos: the photos_ keys and the camera's old lines\n");

    {
        SysConfig c;
        const int n = parse("sysop_password = x1y2z3\n", c);
        check("no photo lines: no problem", n == 0);
        check("and the defaults: keep 30, max 200, floor a tenth, TL 7 and 200",
              c.photosKeep == 30 && c.photosMax == 200 && c.photosFloor == -1 &&
              c.photosTlKeep == 7 && c.photosTlMax == 200 && c.photosOld == 0);
    }
    {
        SysConfig c;
        const int n = parse("sysop_password = x1y2z3\n"
                            "[plugin:camera]\nkeep = 9\nmax = 55\nfloor = 300\ntl_keep = 3\ntl_max = 77\n", c);
        check("the old camera lines alone: no problem", n == 0);
        check("and each stands for its photos_ key",
              c.photosKeep == 9 && c.photosMax == 55 && c.photosFloor == 300 &&
              c.photosTlKeep == 3 && c.photosTlMax == 77);
        check("photosOld names all five", c.photosOld == (4u | 8u | 16u | 32u | 64u));
    }
    {
        SysConfig c;
        // The photos_ lines first, the old ones after: the new key still wins.
        const int n = parse("sysop_password = x1y2z3\n"
                            "photos_keep = 12\nphotos_max = 34\nphotos_floor = 56\n"
                            "photos_tl_keep = 5\nphotos_tl_max = 78\n"
                            "[plugin:camera]\nkeep = 9\nmax = 55\nfloor = 300\ntl_keep = 3\ntl_max = 77\n", c);
        check("both: no problem", n == 0);
        check("a photos_ line wins over the camera's old line, every one of the five",
              c.photosKeep == 12 && c.photosMax == 34 && c.photosFloor == 56 &&
              c.photosTlKeep == 5 && c.photosTlMax == 78);
        check("and photosOld names none, so a CONFIG photos save drops nothing it needs",
              c.photosOld == 0);
    }
    {
        SysConfig c;
        // The old section first: a plugin section ends the core keys, so a
        // photos_ line cannot follow it; the old lines are parsed first and
        // the rule is applied after the whole file.
        const int n = parse("sysop_password = x1y2z3\nphotos_max = 34\n"
                            "[plugin:camera]\nkeep = 9\nmax = 55\n", c);
        check("some of each: no problem", n == 0);
        check("the photos_ line wins where there is one (max 34)", c.photosMax == 34);
        check("and the old line stands in where there is not (keep 9)", c.photosKeep == 9);
        check("photosOld names only the stand-in", c.photosOld == 4u);
    }
    {
        SysConfig c;
        // An empty photos_floor is "a tenth of the card", and still a line:
        // it wins over the old floor's 300.
        parse("sysop_password = x1y2z3\nphotos_floor =\n[plugin:camera]\nfloor = 300\n", c);
        check("an empty photos_floor still wins: a tenth of the card, not 300",
              c.photosFloor == -1 && !(c.photosOld & 16u));
    }
    {
        SysConfig c;
        // An old line the camera would not take is not a stand-in.
        parse("sysop_password = x1y2z3\n[plugin:camera]\nkeep = lots\nmax = 99999\n", c);
        check("an old line out of range stands for nothing: the defaults",
              c.photosKeep == 30 && c.photosMax == 200 && c.photosOld == 0);
    }

    unlink((g_dir + "/system.cfg").c_str());
    rmdir(g_dir.c_str());
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
