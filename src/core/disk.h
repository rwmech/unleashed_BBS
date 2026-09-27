// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/disk.h
// Module:       Core / storage, as the drive light sees it
//
// Purpose:      fopen and opendir that tell the drive light (1.1.1).
//
//               The lights plugin's drive light follows plat::diskPulse, and
//               1.1.0 only pulsed it from the screens and the sd plugin, so
//               the board's busiest storage (users.txt, the caller log,
//               system.cfg, chat's mail, the forums, the file areas, the
//               backups, the information pages, reboots.log) never lit it.
//               The board calls stdio directly and there is no one place a
//               file is opened, so these stand in for fopen and opendir on
//               those paths instead of a pulse added beside each call.
//
//               Which lamp is the path's: under the card's mount (plat::
//               sdBase) it is the card, anything else is the board's own
//               flash. A file that will not open to be written or appended
//               is an error; one that will not open for reading (or r+,
//               tried first where a file may not exist yet) usually just is
//               not there, and says nothing.
//
//               A pulse is two stores (platform.h), so this costs nothing
//               worth measuring on any path, and nothing at all on a board
//               with no lights.
//
// Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
// See also:     src/platform/platform.h (diskPulse), src/plugins/lights.cpp
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
// ===========================================================================
#pragma once

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include "../platform/platform.h"

namespace disk {

// kindOf: the card when the path is under its mount, else the board's flash.
inline plat::DiskKind kindOf(const char* path) {
    const char* sd = plat::sdBase();
    const size_t n = sd ? strlen(sd) : 0;
    return (n && path && !strncmp(path, sd, n)) ? plat::DISK_CARD : plat::DISK_FLASH;
}

// Tally: what the loop opened in this pass (1.1.2), for the slow-pass line.
// A pass's time says a pass was slow; this says whether files were why, and
// how many: the bench's login and logoff passes were one "session" figure
// each until this, and the cause had to be reasoned about from outside.
// writes counts the opens that will write, and on the board's own flash
// each of those is a block copied at its close with both cores stopped,
// which is why deferrable work (the call figures, users::statsTick) waits
// for a pass that has none. Only the loop's opens count (plat::onLoop): the
// runner's are its own business. Bbs::tick zeroes it at the top of a pass.
struct Tally {
    uint16_t opens  = 0;       // files and folders opened
    uint16_t writes = 0;       // of those, opened to write, on flash
    uint32_t us     = 0;       // time spent in the opens themselves
};
inline Tally& tally() { static Tally t; return t; }

// open: fopen, and the drive light told. A write or an append that cannot
// open is an error. "r+" is not: it is how a file that may not be there yet
// is tried first (the caller log's first call, a new forum), and its miss is
// the expected path to "w+".
inline FILE* open(const char* path, const char* mode) {
    const bool loop = plat::onLoop();
    const uint32_t t0 = loop ? plat::micros() : 0;
#ifdef BBS_HOST
    plat::hostDiskOpen(path, mode);          // a test's cost per open, and its count (1.1.2)
#endif
    FILE* f = fopen(path, mode);
    if (f)                                   plat::diskPulse(kindOf(path));
    else if (mode[0] == 'w' || mode[0] == 'a') plat::diskPulse(plat::DISK_ERROR);
    if (loop) {
        Tally& t = tally();
        ++t.opens;
        t.us += plat::micros() - t0;
        const bool writes = mode[0] == 'w' || mode[0] == 'a' || strchr(mode, '+');
        if (f && writes && kindOf(path) == plat::DISK_FLASH) ++t.writes;
    }
    return f;
}

// dir: opendir, and the drive light told when it opens.
inline DIR* dir(const char* path) {
    const bool loop = plat::onLoop();
    const uint32_t t0 = loop ? plat::micros() : 0;
#ifdef BBS_HOST
    plat::hostDiskOpen(path, "dir");
#endif
    DIR* d = opendir(path);
    if (d) plat::diskPulse(kindOf(path));
    if (loop) {
        ++tally().opens;
        tally().us += plat::micros() - t0;
    }
    return d;
}

} // namespace disk
