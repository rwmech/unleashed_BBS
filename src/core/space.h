// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/space.h
// Module:       Core / how full the storage is (1.1.2)
//
// Purpose:      The flash partitions' and the card's size and use, measured
//               on the background runner and kept here for everybody else:
//               MEM, SYS, DASH, HARDWARE, PLUGINS, the plugins' write guard
//               and the restore's room checks.
//
//               Measuring is the expensive part. LittleFS has no free-block
//               counter: esp_littlefs_info walks every block of every file,
//               about 85 ms a partition on the board, and a card with a
//               stale free-cluster hint makes FAT scan its whole table. On
//               the loop those froze every caller (SYS asked for two, DASH
//               once a minute, every plugin write once a minute).
//
//               Rob's rule (2026-09-26): these are informational, so they
//               are cached. They are measured on the runner at boot, at
//               each staff login, after an SD MOUNT, and when a sysop asks
//               (MEM FORCE, SYS FORCE). A write, a restore or an install
//               marks only the partition it changed stale, which means
//               "measure it at the next of those", never on the loop. Every
//               screen marks a kept figure with a trailing "." and says
//               when it was taken, so a sysop can tell a kept figure from a
//               live one. Memory figures are counters and stay live.
//
//               The plugins' write guard reads the kept figure. Its 32 KB
//               margin is what makes that safe: nothing written between two
//               staff logins comes near it on a board a sysop looks after,
//               and a restore, which could, measures on the runner itself.
//
// Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
// See also:     src/core/runner.h, src/platform/platform.h (measure)
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
// ===========================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include "../platform/platform.h"

namespace space {

struct Fig {
    uint64_t total = 0;
    uint64_t used  = 0;
    uint32_t atMs  = 0;       // millis when measured; 0 never
    bool     valid = false;   // measured, and the platform could tell
};

// get: the kept figure for a partition or the card (a copy). Loop only.
Fig get(plat::Part p);

// freeBytes: total minus used, 0 when not known.
uint64_t freeBytes(plat::Part p);

// stale: p changed (a write, a restore, an install); measure it at the next
// refresh. Never measures by itself.
void stale(plat::Part p);

// forget: p is gone (the card came out), so its figure means nothing now.
void forget(plat::Part p);

// refresh: post the runner job for every stale figure, or for all of them
// with all set (MEM FORCE, a staff login). False when one is already
// running or it would not post; true when posted or nothing needed it.
bool refresh(bool all);

// busy: a refresh is queued or running. MEM FORCE's spinner waits on it.
bool busy();

// tick: publish a finished refresh. Once a pass from Bbs::tick; a load and
// a compare when there is nothing to do.
void tick();

// asOf: when the oldest kept figure that is known was measured, as local
// time "14:02" when the clock is set, else "3 min ago". Empty when nothing
// has been measured yet.
void asOf(char* out, size_t n);

}  // namespace space
