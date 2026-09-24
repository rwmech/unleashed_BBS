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
#include <vector>
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
int16_t        g_hostRssi = -1000;     // plat::wifiRssi's answer; -1000 not read yet
}

// host-only: the signal plat::wifiRssi reports, set by the lights plugin's
// host-only LIGHTS RSSI (1.1.0). 0 is "not joined".
void hostSetRssi(int8_t dbm) {
    g_hostRssi = dbm;
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

// The host has no radio: 0, "not joined", unless a test says otherwise, by
// BBS_HOST_RSSI in the environment at start or LIGHTS RSSI (hostSetRssi)
// while it runs, for the lights plugin's wifi meter (1.1.0).
int8_t wifiRssi() {
    if (g_hostRssi == -1000) {
        const char* v = getenv("BBS_HOST_RSSI");
        g_hostRssi = static_cast<int16_t>(v && *v ? atoi(v) : 0);
    }
    return static_cast<int8_t>(g_hostRssi);
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

// The partitions' sizes are the board's (partitions.csv), so a restore's room
// check does the same arithmetic here as there. They were 768 KB and 128 KB,
// the sizes before 0.17.0 moved the accounts to a partition of their own.
bool fsInfo(uint32_t& total, uint32_t& used) {
    total = 256u * 1024u;                            // the board's storage partition
    // The host keeps the user and logs "partitions" inside the data folder;
    // on the board they are partitions of their own and not in this figure.
    uint32_t all = dirBytes(g_fsBase), other = dirBytes(g_userBase) + dirBytes(g_logsBase);
    used = all > other ? all - other : 0;
    return true;
}
// userInfo: the host has no partitions, so report the same notional size the
// board gives its user partition, with what the directory actually holds.
bool userInfo(uint32_t& total, uint32_t& used) {
    total = 608u * 1024u;
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

// ---------------------------------------------------------------------------
// diskPulse: a time and a count, exactly as on the board.
// ---------------------------------------------------------------------------
namespace {
uint32_t g_diskAt[DISK_KINDS]    = {};
uint16_t g_diskCount[DISK_KINDS] = {};
}   // namespace

void diskPulse(DiskKind kind) {
    if (kind >= DISK_KINDS) return;
    g_diskAt[kind] = millis();
    ++g_diskCount[kind];
}

DiskSeen diskSeen() {
    DiskSeen d;
    for (uint8_t i = 0; i < DISK_KINDS; ++i) {
        d.at[i]    = g_diskAt[i];
        d.count[i] = g_diskCount[i];
    }
    return d;
}

// ---------------------------------------------------------------------------
// Pixels on the host: no strip, a record. Each output keeps its pin and the
// last frame it was given, and pixelsFrame hands that back, so LIGHTS, and
// the tests through it, read the colours a board would have sent.
//
// The pins are the chip's output pins, so a pin the board's RMT driver
// would refuse is refused here too. For the reference WROOM: 0 to 33, less
// 20, 24 and 28 to 31, which the chip does not have. For an S3 board
// profile: 0 to 48, less 22 to 25. A frame is never still going out on the
// host, so pixelsShow always takes it; the board's refusal while the wire
// is busy has no host equivalent, and the plugin treats one as nothing more
// than "draw again next frame".
//
// The frame is kept as RGB, which is what pixelsFrame returns on the board
// too, whatever order the bytes went out in. The order goes in the log line,
// which is what a person reading the host's console checks it against.
// ---------------------------------------------------------------------------
namespace {
struct HostPix {
    int     pin   = -1;
    uint8_t count = 0;
    uint8_t order = PIX_GRB;
    uint8_t rgb[kPixelMax * 3] = {};
};
HostPix g_hostPix[kPixelOuts];

bool outputPin(int p) {
#ifdef BBS_CHIP_S3
    return p >= 0 && p <= 48 && !(p >= 22 && p <= 25);
#else
    return p >= 0 && p <= 33 && p != 20 && p != 24 && !(p >= 28 && p <= 31);
#endif
}
}   // namespace

bool pixelsBegin(uint8_t out, int pin, uint8_t count, uint8_t order) {
    if (out >= kPixelOuts || !count || count > kPixelMax || order >= PIX_ORDERS) return false;
    g_hostPix[out] = HostPix();
    if (!outputPin(pin)) return false;
    g_hostPix[out].pin   = pin;
    g_hostPix[out].count = count;
    g_hostPix[out].order = order;
    static const char* const kOrderName[PIX_ORDERS] = { "GRB", "RGB", "BRG", "RBG", "GBR", "BGR" };
    log("pixels: output %u on gpio %d, %u pixel%s, %s (host record)", static_cast<unsigned>(out), pin,
        static_cast<unsigned>(count), count == 1 ? "" : "s", kOrderName[order]);
    return true;
}

void pixelsEnd(uint8_t out) {
    if (out < kPixelOuts) g_hostPix[out] = HostPix();
}

bool pixelsShow(uint8_t out, const uint8_t* rgb, uint8_t count) {
    if (out >= kPixelOuts || !rgb || g_hostPix[out].pin < 0) return false;
    HostPix& p = g_hostPix[out];
    if (count > p.count) count = p.count;
    memcpy(p.rgb, rgb, static_cast<size_t>(count) * 3u);
    return true;
}

uint8_t pixelsFrame(uint8_t out, uint8_t* rgb, uint8_t cap) {
    if (out >= kPixelOuts || !rgb || g_hostPix[out].pin < 0) return 0;
    const HostPix& p = g_hostPix[out];
    uint8_t n = p.count < cap ? p.count : cap;
    memcpy(rgb, p.rgb, static_cast<size_t>(n) * 3u);
    return n;
}

#ifdef BBS_HAS_LCD
// ---------------------------------------------------------------------------
// The panel on the host (a build for a board profile with BBS_HAS_LCD):
// glass in memory. lcdDraw copies each band onto it exactly as the board
// would send it, and PANEL SHOT (hostPanelShot, below the namespace)
// writes it out as a picture. The pins must be ones the panel needs, the
// rest is taken on trust: there is nothing here to refuse it.
// ---------------------------------------------------------------------------
namespace {
LcdCfg              g_lcdCfg;
bool                g_lcdUp = false;
std::vector<uint16_t> g_glass;
}

bool lcdBegin(const LcdCfg& c, char* err, size_t errLen) {
    lcdEnd();
    if (c.mosi < 0 || c.sclk < 0 || c.dc < 0 || !c.width || !c.height) {
        snprintf(err, errLen, "the panel needs MOSI, SCLK and D/C");
        return false;
    }
    g_lcdCfg = c;
    g_lcdUp  = true;
    g_glass.assign(static_cast<size_t>(c.width) * c.height, 0);
    log("panel: ST7789 %ux%u, rotation %u, %u MHz (host glass)", static_cast<unsigned>(c.width),
        static_cast<unsigned>(c.height), static_cast<unsigned>(c.rotation), static_cast<unsigned>(c.mhz));
    return true;
}

bool lcdSame(const LcdCfg& c) {
    const LcdCfg& o = g_lcdCfg;
    return g_lcdUp && o.mosi == c.mosi && o.sclk == c.sclk && o.cs == c.cs && o.dc == c.dc &&
           o.rst == c.rst && o.bl == c.bl && o.width == c.width && o.height == c.height &&
           o.xoff == c.xoff && o.yoff == c.yoff && o.rotation == c.rotation &&
           o.invert == c.invert && o.bgr == c.bgr && o.mirror == c.mirror && o.mhz == c.mhz;
}

void lcdEnd() {
    g_lcdUp = false;
    g_lcdCfg = LcdCfg();
    g_glass.clear();
}

bool lcdReady() { return g_lcdUp; }

uint32_t lcdBandPixels() { return 320u * 16u; }

bool lcdDraw(const uint16_t* fb, uint16_t stride, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!g_lcdUp || !fb || static_cast<uint32_t>(w) * h > lcdBandPixels()) return false;
    if (x + w > g_lcdCfg.width || y + h > g_lcdCfg.height) return false;
    for (uint16_t r = 0; r < h; ++r)
        memcpy(&g_glass[static_cast<size_t>(y + r) * g_lcdCfg.width + x],
               fb + static_cast<size_t>(y + r) * stride + x, static_cast<size_t>(w) * 2u);
    return true;
}

void lcdBacklight(uint8_t) {}

void* psramAlloc(size_t n) { return malloc(n); }
void  psramFree(void* p)  { free(p); }
#endif  // BBS_HAS_LCD

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

#ifdef BBS_HAS_LCD
// hostPanelShot: the host's glass as a binary PPM, 8 bits a channel, for a
// person to look at (PANEL SHOT). Relative paths land in the data folder.
bool hostPanelShot(const char* path) {
    if (!plat::g_lcdUp || plat::g_glass.empty()) return false;
    std::string full = path[0] == '/' ? std::string(path) : g_fsBase + "/" + path;
    FILE* f = fopen(full.c_str(), "wb");
    if (!f) return false;
    const unsigned w = plat::g_lcdCfg.width, h = plat::g_lcdCfg.height;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint16_t v : plat::g_glass) {
        uint8_t px[3] = { static_cast<uint8_t>((v >> 8) & 0xF8), static_cast<uint8_t>((v >> 3) & 0xFC),
                          static_cast<uint8_t>((v << 3) & 0xF8) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    return true;
}
#endif
