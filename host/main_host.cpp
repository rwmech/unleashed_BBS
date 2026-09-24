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
#include "core/plugin.h"
#include "platform/platform.h"
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

void hostSetFsBase(const char* path);
void hostSetStack(const uint8_t* lo, size_t len);

namespace {

// The BBS thread's stack. Far bigger than the board's, because x86-64
// frames under glibc are not Xtensa frames under newlib and the point here
// is to measure, not to reproduce an overflow. What matters is that it is a
// known block painted before the thread starts, so plat::stackFree can count
// the untouched fill the way the kernel does on the board.
constexpr size_t kHostStack = 256 * 1024;

uint16_t g_port = BBS_PORT;

// bbsThread: what the board's bbsTask does once the network is up. begin()
// and the plugins' start() run here, as they run on the BBS task there, so
// their depth is counted too.
void* bbsThread(void*) {
    Bbs& bbs = Bbs::instance();
    if (!bbs.begin(g_port)) exit(1);
    plugins::begin(bbs);
    for (;;) bbs.tick();
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    srand(static_cast<unsigned>(time(nullptr)));
    hostSetFsBase(argc > 1 ? argv[1] : "../data");
    g_port = static_cast<uint16_t>(argc > 2 ? atoi(argv[2]) : BBS_PORT);
    syscfg::load();          // the host clock is already set, no NTP here

    // A guard page under the stack, so running off the bottom faults at once
    // rather than scribbling on whatever the allocator put there.
    size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    void* map = mmap(nullptr, kHostStack + page, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map == MAP_FAILED) { perror("mmap"); return 1; }
    uint8_t* lo = static_cast<uint8_t*>(map) + page;
    mprotect(map, page, PROT_NONE);
    memset(lo, 0xA5, kHostStack);                      // tskSTACK_FILL_BYTE
    hostSetStack(lo, kHostStack);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, lo, kHostStack);
    pthread_t t;
    if (pthread_create(&t, &attr, bbsThread, nullptr) != 0) { perror("pthread_create"); return 1; }
    pthread_join(t, nullptr);
    return 0;
}
