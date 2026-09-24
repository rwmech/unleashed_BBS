/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/calllog.cpp
 * Module:       Core / caller log
 *
 * Purpose:      Caller log ring file (see calllog.h).
 *
 * Libraries:    none (libc stdio)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
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

#include "calllog.h"
#include "../platform/platform.h"
#include "clock.h"
#include "../config.h"
#include <sys/stat.h>
#include <cstdio>
#include <cstring>

namespace {

const char kMagic[4] = { 'C', 'L', 'G', '1' };

struct Header {
    char     magic[4];
    uint16_t next;
    uint16_t count;
};

Header g_hdr;
bool   g_loaded = false;

void path(char* out, size_t n) {
    snprintf(out, n, "%s/%s", plat::logsBase(), BBS_CALLLOG_FILE);
}

// ---------------------------------------------------------------------------
// loadHeader: read (or reset) the cached header once
// ---------------------------------------------------------------------------
void loadHeader() {
    if (g_loaded) return;
    g_loaded = true;
    memcpy(g_hdr.magic, kMagic, 4);
    g_hdr.next = g_hdr.count = 0;

    char p[96];
    path(p, sizeof(p));
    FILE* f = fopen(p, "rb");
    if (!f) return;
    Header h;
    if (fread(&h, sizeof(h), 1, f) == 1 && !memcmp(h.magic, kMagic, 4) &&
        h.next < BBS_CALLLOG_SIZE && h.count <= BBS_CALLLOG_SIZE) {
        g_hdr = h;
    }
    fclose(f);
}

long slotOffset(uint16_t slot) {
    return static_cast<long>(sizeof(Header) + slot * sizeof(CallRec));
}

} // namespace

namespace calllog {

// ---------------------------------------------------------------------------
// mirror: append one line to a plain text copy on the SD card.
//
// The ring above is the record. This is a second copy, and the difference
// matters: the ring is fifty calls on a partition that is always there, and
// this is months of them on a card that can be pulled at any moment. LAST
// reads the ring, so nothing a sysop relies on stops working when the card
// goes, and that was the point of mirroring rather than moving.
//
// Plain text, one file per month, because the reason the card is FAT32 is
// that you can take it out and read it. A fixed-size binary ring would be
// the wrong shape for a file somebody opens in a text editor.
//
// Failure here is deliberately quiet past the first complaint: a caller
// hanging up must not be held up, and a card that has been pulled should not
// fill the console with one message per call.
// ---------------------------------------------------------------------------
static bool g_mirrorWarned = false;

static void mirror(const CallRec& r) {
    const char* base = plat::sdBase();
    if (!base[0]) return;                       // no card, nothing to mirror to

    char dir[96];
    snprintf(dir, sizeof(dir), "%s/%s", base, BBS_SD_LOG_DIR);
    mkdir(dir, 0755);                           // harmless when it exists

    char when[24] = "unknown";
    char month[8] = "000000";
    if (r.start) {
        clk::fmtEpoch(when, sizeof(when), "%Y-%m-%d %H:%M", r.start);
        clk::fmtEpoch(month, sizeof(month), "%Y-%m", r.start);
    }

    char p[128];
    snprintf(p, sizeof(p), "%s/calls-%.7s.log", dir, month);
    FILE* f = fopen(p, "a");
    if (!f) {
        if (!g_mirrorWarned) {
            plat::log("calllog: cannot mirror to the card (%s)", p);
            g_mirrorWarned = true;
        }
        return;
    }
    g_mirrorWarned = false;

    // Tab separated so a spreadsheet opens it and a human can still read it.
    fprintf(f, "%s\t%-20s\t%-15s\tnode %u\t%u min%s\n",
            when, r.user[0] ? r.user : "(none)", r.ip,
            static_cast<unsigned>(r.node),
            static_cast<unsigned>((r.secs + 59u) / 60u),
            (r.flags & CallRec::F_GUEST) ? "\tguest" :
            (r.flags & CallRec::F_SYSOP) ? "\tsysop" : "");
    fflush(f);
    fclose(f);
}

// ---------------------------------------------------------------------------
// append: record into slot "next", then rewrite the header
// ---------------------------------------------------------------------------
bool append(const CallRec& r) {
    loadHeader();
    char p[96];
    path(p, sizeof(p));
    FILE* f = fopen(p, "r+b");
    if (!f) {
        f = fopen(p, "w+b");                      // first call ever
        if (!f) { plat::log("calllog: cannot create %s", p); return false; }
        g_hdr.next = g_hdr.count = 0;
    }

    bool ok = fseek(f, slotOffset(g_hdr.next), SEEK_SET) == 0 &&
              fwrite(&r, sizeof(r), 1, f) == 1;
    if (ok) {
        g_hdr.next = static_cast<uint16_t>((g_hdr.next + 1) % BBS_CALLLOG_SIZE);
        if (g_hdr.count < BBS_CALLLOG_SIZE) ++g_hdr.count;
        ok = fseek(f, 0, SEEK_SET) == 0 && fwrite(&g_hdr, sizeof(g_hdr), 1, f) == 1;
    }
    fclose(f);
    if (!ok) plat::log("calllog: write failed");
    mirror(r);
    return ok;
}

uint8_t count() {
    loadHeader();
    return static_cast<uint8_t>(g_hdr.count);
}

uint8_t countSince(uint32_t epoch) {
    loadHeader();
    if (!g_hdr.count) return 0;
    char p[96];
    path(p, sizeof(p));
    FILE* f = fopen(p, "rb");
    if (!f) return 0;
    uint8_t n = 0;
    CallRec r;
    fseek(f, slotOffset(0), SEEK_SET);
    for (uint16_t i = 0; i < g_hdr.count; ++i) {
        if (fread(&r, sizeof(r), 1, f) != 1) break;
        if (r.start && r.start >= epoch) ++n;
    }
    fclose(f);
    return n;
}

bool get(uint8_t back, CallRec& out) {
    loadHeader();
    if (back >= g_hdr.count) return false;
    uint16_t slot = static_cast<uint16_t>((g_hdr.next + BBS_CALLLOG_SIZE - 1 - back) % BBS_CALLLOG_SIZE);
    char p[96];
    path(p, sizeof(p));
    FILE* f = fopen(p, "rb");
    if (!f) return false;
    bool ok = fseek(f, slotOffset(slot), SEEK_SET) == 0 && fread(&out, sizeof(out), 1, f) == 1;
    fclose(f);
    out.user[BBS_USER_MAX] = '\0';
    out.ip[sizeof(out.ip) - 1] = '\0';
    return ok;
}

} // namespace calllog
