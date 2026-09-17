/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         host/main_host.cpp
 * Module:       Host test build entry point
 *
 * Purpose:      Linux host entry point. Runs the same BBS core as the
 *                  ESP32 so screens, detection and effects can be tried with
 *                  SyncTERM, a terminal emulator, or VICE + a TCP modem bridge.
 *
 * Usage:        ./bbs_host [data_dir] [port]
 *
 * Libraries:    libc
 * Targets:      Linux host test build
 * See also:     README.md
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

#include "core/bbs.h"
#include "core/sysconfig.h"
#include "platform/platform.h"
#include <csignal>
#include <cstdlib>
#include <ctime>

void hostSetFsBase(const char* path);

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    srand(static_cast<unsigned>(time(nullptr)));
    hostSetFsBase(argc > 1 ? argv[1] : "../data");
    uint16_t port = static_cast<uint16_t>(argc > 2 ? atoi(argv[2]) : BBS_PORT);
    syscfg::load();          // the host clock is already set, no NTP here

    Bbs& bbs = Bbs::instance();
    if (!bbs.begin(port)) return 1;
    for (;;) bbs.tick();
}
