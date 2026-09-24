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

#include "core/bbs.h"
#include "core/recovery.h"
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
void hostSetArgv(char* const* argv);

namespace {

// ---------------------------------------------------------------------------
// bootWatch: the BOOT-hold watch the board runs in its first seconds, here on
// a simulated clock ten milliseconds a step, so a 21 s hold costs a test no
// time at all. The watch itself, the files it changes and the restart that
// follows are the board's own code; only the clock and the button are not.
// With no BBS_BOOT_HOLD_MS the button is never down and the window simply
// closes. The LED's pattern is traced, since a PC has no LED to look at.
// ---------------------------------------------------------------------------
const char* ledName(recovery::Led l) {
    switch (l) {
        case recovery::Led::Slow:  return "slow";
        case recovery::Led::Fast:  return "fast";
        case recovery::Led::Solid: return "solid";
        case recovery::Led::Off:   return "off";
        default:                   return "free";
    }
}

void bootWatch() {
    recovery::Led was = recovery::Led::Free;
    for (uint32_t t = 0; recovery::bootPoll(t); t += 10) {
        recovery::Led now = recovery::bootLed();
        if (now != was) {
            plat::log("led: %s at %u ms (host)", ledName(now), static_cast<unsigned>(t));
            was = now;
        }
    }
    if (was != recovery::Led::Free) plat::log("led: free (host)");
}

// ---------------------------------------------------------------------------
// wifiSim: the Wi-Fi side of a boot, for the last-good fallback, on a
// simulated clock. BBS_HOST_WIFI=up=<ms> joins the network system.cfg names
// at that time; BBS_HOST_WIFI=never never does. A network gone back to joins
// a second after the switch, as a working one would. The decisions and the
// file are the board's code (core/recovery); the radio is this loop.
// ---------------------------------------------------------------------------
void wifiSim(const char* spec) {
    const SysConfig& c = syscfg::get();
    char ssid[33], pass[65];
    snprintf(ssid, sizeof(ssid), "%s", c.wifiSsid);
    snprintf(pass, sizeof(pass), "%s", c.wifiPass);
    const long upAt = !strncmp(spec, "up=", 3) ? atol(spec + 3) : -1;
    long switchedAt = -1;
    recovery::wifiBegin(0, ssid, pass);
    for (uint32_t t = 0; t <= 120000; t += 100) {
        const bool up = switchedAt >= 0 ? t >= static_cast<uint32_t>(switchedAt) + 1000
                                        : upAt >= 0 && t >= static_cast<uint32_t>(upAt);
        if (up) {
            plat::log("wifi (host): joined \"%s\" at %u ms", ssid, static_cast<unsigned>(t));
            recovery::wifiJoined(ssid, pass);
            return;
        }
        char ts[33], tp[65];
        if (recovery::wifiDue(t, false, false, ssid, ts, tp)) {
            plat::log("wifi (host): went back at %u ms", static_cast<unsigned>(t));
            snprintf(ssid, sizeof(ssid), "%s", ts);
            snprintf(pass, sizeof(pass), "%s", tp);
            switchedAt = static_cast<long>(t);
        }
    }
    plat::log("wifi (host): still dialling \"%s\" at 120000 ms", ssid);
}

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
    hostSetArgv(argv);
    hostSetFsBase(argc > 1 ? argv[1] : "../data");
    syscfg::load();          // the host clock is already set, no NTP here
    bootWatch();             // may restart this program, as the board would restart
    if (const char* w = getenv("BBS_HOST_WIFI")) wifiSim(w);
    // A port on the command line wins, because the harness gives every run
    // its own. Without one, the port in system.cfg, as the board reads it at
    // boot: which is how a test restarts a board on its own files and finds
    // it listening where CONFIG network said.
    g_port = static_cast<uint16_t>(argc > 2 ? atoi(argv[2]) : syscfg::get().port);

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
