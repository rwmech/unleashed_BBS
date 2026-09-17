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
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <zlib.h>

namespace {
std::string g_fsBase   = "../data";
std::string g_logsBase = "../data/logs";
}

// host-only: set by main_host.cpp; logs live in <data>/logs
void hostSetFsBase(const char* path) {
    g_fsBase   = path;
    g_logsBase = std::string(path) + "/logs";
    mkdir(g_logsBase.c_str(), 0755);
}

namespace plat {

uint32_t millis() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
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

HeapStats heap() {
    return HeapStats{ 0, 0, 0, false };
}

int8_t wifiRssi() {
    return 0;
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

bool fsInfo(uint32_t& total, uint32_t& used) {
    total = 768u * 1024u;                            // the board's storage partition
    used  = dirBytes(g_fsBase);
    return true;
}

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
