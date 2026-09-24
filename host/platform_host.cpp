/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         host/platform_host.cpp
 * Module:       Platform layer (Linux host)
 *
 * Purpose:      Linux host implementation of the platform layer, so the
 *                  BBS core can be run and tested on a PC before flashing.
 *                  The backup button is "pressed" when BBS_BACKUP_TEST_OPEN=1
 *                  is set in the environment.
 *
 * Libraries:    libc, zlib
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

#include "platform/platform.h"
#include "config.h"              // BBS_STACK_BAND
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>

namespace {
std::string g_fsBase   = "../data";
std::string g_userBase = "../data/user";
std::string g_logsBase = "../data/logs";
const uint8_t* g_stackLo  = nullptr;   // the BBS thread's painted stack
size_t         g_stackLen = 0;
char* const*   g_argv     = nullptr;   // how this program was started, for restart()
}

// host-only: set by main_host.cpp before the BBS thread starts
void hostSetStack(const uint8_t* lo, size_t len) {
    g_stackLo  = lo;
    g_stackLen = len;
}

// host-only: set by main_host.cpp, so plat::restart can start it again
void hostSetArgv(char* const* argv) {
    g_argv = argv;
}

// host-only: set by main_host.cpp; logs live in <data>/logs
void hostSetFsBase(const char* path) {
    g_fsBase   = path;
    g_userBase = std::string(path) + "/user";
    g_logsBase = std::string(path) + "/logs";
    mkdir(g_userBase.c_str(), 0755);
    mkdir(g_logsBase.c_str(), 0755);
}

namespace plat {

uint32_t millis() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

uint32_t micros() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL);
}

uint32_t random32() {
    return static_cast<uint32_t>(rand());
}

const char* fsBase() {
    return g_fsBase.c_str();
}

const char* logsBase() {
    return g_logsBase.c_str();
}

const char* userBase() {
    return g_userBase.c_str();
}

HeapStats heap() {
    return HeapStats{ 0, 0, 0, 0, false };
}

// The host has no heap worth reporting, and 0 is what heapWatch reads as
// "say nothing", which is the same answer heap() already gives it.
// The host has no radio, and an empty string is what the system screen
// shows as a dash rather than as a fault.
const char* powerSave() {
    return "";
}

uint32_t heapFree() {
    return 0;
}

// The BBS runs on a thread whose stack main_host.cpp painted with 0xA5 before
// starting it, the byte FreeRTOS paints a task's stack with, so the high
// water mark is read here exactly the way the kernel reads it on the board:
// count the untouched fill from the bottom up. Before hostSetStack (a tool
// that never starts the thread) there is nothing to measure and it says 0,
// which SYS shows as "n/a" rather than a confident zero.
//
// The figure is real and it is not the board's: x86-64 frames under glibc,
// not Xtensa frames under newlib. It is for comparing host runs with each
// other, before and after a change.
//
// Not instrumented under SAN=1: this reads stack below the live frames on
// purpose, which is exactly what AddressSanitizer exists to object to.
__attribute__((no_sanitize_address))
uint32_t stackFree() {
    if (!g_stackLo) return 0;
    size_t n = 0;
    while (n < g_stackLen && g_stackLo[n] == 0xA5) ++n;
    return static_cast<uint32_t>(n);
}

uint32_t stackSize() {
    return static_cast<uint32_t>(g_stackLen);
}

// stackDeeper: the band of fill just under the old mark, as on the board
__attribute__((no_sanitize_address))
uint32_t stackDeeper(uint32_t knownFree) {
    constexpr uint32_t kBand = BBS_STACK_BAND;
    if (!g_stackLo || knownFree < 4 || knownFree > g_stackLen) return 0;
    uint32_t top  = knownFree & ~3u;
    uint32_t base = top > kBand ? top - kBand : 0;
    for (uint32_t at = base; at < top; ++at) {
        if (g_stackLo[at] != 0xA5) return stackFree();
    }
    return 0;
}

// hardware: no chip to describe. The directory shows it as the badge text,
// which is the honest answer for a board running on a PC.
void hardware(char* out, size_t n) {
    snprintf(out, n, "host");
}

int8_t wifiRssi() {
    return 0;
}

// netInfo: the host has no radio, so the system screen shows the dashes.
// BBS_HOST_SSID plays a board joined to that network, which is what a board
// on the compiled-in secrets.h fallback looks like to CONFIG wifi: on a
// network, with none named in system.cfg.
NetInfo netInfo() {
    NetInfo n;
    const char* ssid = getenv("BBS_HOST_SSID");
    if (ssid && *ssid) {
        snprintf(n.ssid, sizeof(n.ssid), "%.32s", ssid);
        snprintf(n.ip, sizeof(n.ip), "127.0.0.1");
        n.channel = 1;
        n.valid   = true;
    }
    return n;
}

// ---------------------------------------------------------------------------
// fsInfo: the host has a whole disk, so pretend it is the board's storage
// partition and add up what the data directory holds. That keeps the free
// space rules testable off the board.
// ---------------------------------------------------------------------------
namespace {
uint32_t dirBytes(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return 0;
    uint32_t total = 0;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        std::string path = dir + "/" + e->d_name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) total += dirBytes(path);
        else                     total += static_cast<uint32_t>(st.st_size);
    }
    closedir(d);
    return total;
}
}   // namespace

// ---------------------------------------------------------------------------
// The SD card on the host: a directory, mounted on demand.
//
// BBS_SD_DIR names it. Without that variable the host build has no card,
// which is the case worth having as the default, because "runs without a
// card" is the claim the whole design rests on and it should be what the
// tests exercise unless they say otherwise.
//
// Mounting here is creating a directory and is not slow. On the board it is
// a few hundred milliseconds of SPI, so anything written against this must
// still treat a mount as something that happens at start or on a sysop's
// say-so, never from the loop. A host stub that is faster than the real
// thing is how blocking calls end up somewhere they cannot be.
// ---------------------------------------------------------------------------
namespace {
std::string g_sdBase;
bool        g_sdMount = false;
}   // namespace

const char* sdBase() {
    return g_sdMount ? g_sdBase.c_str() : "";
}

bool sdMount(const SdPins& pins, char* err, size_t errLen) {
    (void)pins;                       // the host has no bus to wire wrongly
    if (err && errLen) err[0] = '\0';
    if (g_sdMount) return true;
    const char* dir = getenv("BBS_SD_DIR");
    if (!dir || !*dir) {
        if (err && errLen) snprintf(err, errLen, "no card found: set BBS_SD_DIR to fake one");
        return false;
    }
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        if (err && errLen) snprintf(err, errLen, "cannot use %s: %s", dir, strerror(errno));
        return false;
    }
    g_sdBase  = dir;
    g_sdMount = true;
    plat::log("sd: mounted %s (host)", dir);
    return true;
}

void sdUnmount() {
    if (!g_sdMount) return;
    g_sdMount = false;
    plat::log("sd: unmounted");
}

SdInfo sdInfo() {
    SdInfo i;
    if (!g_sdMount) return i;
    i.mounted  = true;
    i.speedKHz = 20000;
    snprintf(i.type, sizeof(i.type), "%s", "host dir");
    // A notional 2 GB, so the free-space arithmetic a plugin does is
    // exercised rather than skipped.
    i.totalKB = 2u * 1024u * 1024u;
    uint32_t used = dirBytes(g_sdBase) / 1024u;
    i.freeKB  = used < i.totalKB ? i.totalKB - used : 0;
    return i;
}

// ---------------------------------------------------------------------------
// Device serial port on the host. BBS_SERIAL_DEV points at a real port or
// a pty for live testing; without it the port is a loopback, so what the
// operator types comes back as if a device echoed it. That is enough to
// drive the bridge, its watchers and the tests.
// ---------------------------------------------------------------------------
namespace {
bool     g_serOpen = false;
int      g_serFd   = -1;
uint8_t  g_loop[512];
size_t   g_loopLen = 0;
}   // namespace

bool serialOpen(int rxPin, int txPin, uint32_t baud, uint8_t bits, char parity, uint8_t stop) {
    (void)rxPin; (void)txPin; (void)baud; (void)bits; (void)parity; (void)stop;
    if (g_serOpen) serialClose();
    const char* dev = getenv("BBS_SERIAL_DEV");
    if (dev && *dev) {
        g_serFd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (g_serFd < 0) return false;
    }
    g_loopLen = 0;
    g_serOpen = true;
    return true;
}

void serialClose() {
    if (g_serFd >= 0) { close(g_serFd); g_serFd = -1; }
    g_serOpen = false;
    g_loopLen = 0;
}

bool serialIsOpen() { return g_serOpen; }

bool serialSetLine(uint32_t baud, uint8_t bits, char parity, uint8_t stop) {
    (void)baud; (void)bits; (void)parity; (void)stop;
    return g_serOpen;
}

size_t serialRead(uint8_t* buf, size_t cap) {
    if (!g_serOpen) return 0;
    if (g_serFd >= 0) {
        ssize_t n = read(g_serFd, buf, cap);
        return n > 0 ? static_cast<size_t>(n) : 0;
    }
    size_t n = g_loopLen < cap ? g_loopLen : cap;
    memcpy(buf, g_loop, n);
    memmove(g_loop, g_loop + n, g_loopLen - n);
    g_loopLen -= n;
    return n;
}

size_t serialWrite(const uint8_t* data, size_t n) {
    if (!g_serOpen) return 0;
    if (g_serFd >= 0) {
        ssize_t w = write(g_serFd, data, n);
        return w > 0 ? static_cast<size_t>(w) : 0;
    }
    size_t room = sizeof(g_loop) - g_loopLen;                // loopback: it comes back
    if (n > room) n = room;
    memcpy(g_loop + g_loopLen, data, n);
    g_loopLen += n;
    return n;
}

uint32_t serialFramingErrors() { return 0; }

bool fsInfo(uint32_t& total, uint32_t& used) {
    total = 768u * 1024u;                            // the board's storage partition
    used  = dirBytes(g_fsBase);
    return true;
}
// userInfo: the host has no partitions, so report the same notional size the
// board gives its user partition, with what the directory actually holds.
bool userInfo(uint32_t& total, uint32_t& used) {
    total = 128u * 1024u;
    used  = dirBytes(g_userBase);
    return true;
}

// Nothing kept on the host: summing a directory costs nothing here, which is
// exactly why the board's cost never showed up in a host test.
void fsInfoStale() {}

void log(const char* fmt, ...) {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(stdout, "[%6lu.%03lu] ", static_cast<unsigned long>(ts.tv_sec),
            static_cast<unsigned long>(ts.tv_nsec / 1000000));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

void backupButtonBegin(int gpio) {
    log("backup: host button %s (gpio %d ignored)",
        getenv("BBS_BACKUP_TEST_OPEN") ? "held by BBS_BACKUP_TEST_OPEN" : "not available", gpio);
}

bool backupButtonPressed(uint32_t) {
    const char* v = getenv("BBS_BACKUP_TEST_OPEN");
    return v && *v == '1';
}

void activityLedBegin(int) {}
void activityPulse(uint32_t) {}
void activityTick(uint32_t) {}
void ledSignal(uint32_t, uint32_t) {}   // no LED on a PC
// No LED either. main_host traces the watch's pattern (recovery::bootLed)
// instead, which is the part worth checking: what the LED was told.
void ledOverride(int8_t) {}

// The host build is started by a person, so it never crashed its way here.
const char* resetReason()  { return "host start"; }
bool        resetWasCrash() { return false; }

// ---------------------------------------------------------------------------
// Recovery on the host (1.1.0). A test plays a BOOT hold of BBS_BOOT_HOLD_MS
// against main_host's simulated clock, and the restart that follows an
// action is this program starting itself again on the same files, with the
// note in the environment where the board keeps it in RTC memory. That is
// what lets a test see the boot after a reset, not just the reset.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kHostPressAt = 500;      // the simulated press, after reset
}

bool bootButtonDown(uint32_t now) {
    static long hold = -1;
    if (hold < 0) {
        const char* v = getenv("BBS_BOOT_HOLD_MS");
        hold = v && *v ? atol(v) : 0;
    }
    return hold > 0 && now >= kHostPressAt && now - kHostPressAt < static_cast<uint32_t>(hold);
}

namespace {
// emptyDir: everything under dir, leaving dir itself, as an erased and
// freshly formatted partition would be.
bool emptyDir(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return errno == ENOENT;
    bool ok = true;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        std::string path = dir + "/" + e->d_name;
        struct stat st;
        if (lstat(path.c_str(), &st) != 0) { ok = false; continue; }
        if (S_ISDIR(st.st_mode)) ok = emptyDir(path) && rmdir(path.c_str()) == 0 && ok;
        else                     ok = unlink(path.c_str()) == 0 && ok;
    }
    closedir(d);
    return ok;
}
}   // namespace

bool factoryErase(char* err, size_t errLen) {
    if (!emptyDir(g_userBase)) { snprintf(err, errLen, "userdata"); return false; }
    if (!emptyDir(g_logsBase)) { snprintf(err, errLen, "logs"); return false; }
    return true;
}

void restart(uint8_t note) {
    char n[8];
    snprintf(n, sizeof(n), "%u", static_cast<unsigned>(note));
    setenv("BBS_HOST_RESTART_NOTE", n, 1);
    // One hold, one reset: the board's button is not still down after it
    // restarts, and a simulated one must not be either.
    unsetenv("BBS_BOOT_HOLD_MS");
    unsetenv("BBS_HOST_WIFI");
    log("host: restarting (note %u)", static_cast<unsigned>(note));
    fflush(stdout);
    if (g_argv) execv("/proc/self/exe", g_argv);
    perror("execv");
    _exit(3);
}

uint8_t restartNote() {
    static int note = -1;
    if (note < 0) {
        const char* v = getenv("BBS_HOST_RESTART_NOTE");
        note = v && *v ? atoi(v) & 0xFF : 0;
        unsetenv("BBS_HOST_RESTART_NOTE");
    }
    return static_cast<uint8_t>(note);
}

// ---------------------------------------------------------------------------
// inflateRaw: zlib in raw mode (windowBits -15)
// ---------------------------------------------------------------------------
bool inflateRaw(InflateIn in, InflateOut out, void* ctx) {
    z_stream z;
    memset(&z, 0, sizeof(z));
    if (inflateInit2(&z, -15) != Z_OK) return false;
    uint8_t ibuf[1024];
    uint8_t obuf[4096];
    bool ok = false;
    bool eof = false;
    for (;;) {
        if (z.avail_in == 0 && !eof) {
            size_t n = in(ctx, ibuf, sizeof(ibuf));
            if (n == 0) eof = true;
            z.next_in  = ibuf;
            z.avail_in = static_cast<uInt>(n);
        }
        z.next_out  = obuf;
        z.avail_out = sizeof(obuf);
        int r = inflate(&z, eof ? Z_FINISH : Z_NO_FLUSH);
        size_t produced = sizeof(obuf) - z.avail_out;
        if (produced && !out(ctx, obuf, produced)) break;
        if (r == Z_STREAM_END) { ok = true; break; }
        if (r != Z_OK && r != Z_BUF_ERROR) break;
        if (eof && produced == 0 && z.avail_in == 0) break;   // truncated stream
    }
    inflateEnd(&z);
    return ok;
}

} // namespace plat
