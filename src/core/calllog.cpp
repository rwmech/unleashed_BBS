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
#include "runner.h"             // the card copy, off the loop (1.1.2)
#include "disk.h"              // fopen and opendir that tell the drive light (1.1.1)
#include "../platform/platform.h"
#include "clock.h"
#include "../config.h"
#include <sys/stat.h>
#include <cerrno>
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

// The newest calls, newest first, and today's count (1.1.0). 5 x 52 bytes
// and a few more: what the dashboard shows every frame, held so that showing
// it opens nothing. The file is still the record; these are only ever a
// copy of what a successful append() put in it.
CallRec  g_recent[calllog::kRecent];
uint8_t  g_recentN    = 0;
uint16_t g_today      = 0;
uint32_t g_todayStart = 0;         // the midnight g_today counts from, 0 none yet

void path(char* out, size_t n) {
    snprintf(out, n, "%s/%s", plat::logsBase(), BBS_CALLLOG_FILE);
}

long slotOffset(uint16_t slot) {
    return static_cast<long>(sizeof(Header) + slot * sizeof(CallRec));
}

// ---------------------------------------------------------------------------
// The pass's read handle (1.1.2). LAST drew a row a record and opened the
// file for each one past the five kept in RAM: a page was fifteen opens in
// one pass, 70 to 88 ms on the bench. Now the first read in a pass opens
// it, every read after that in the same pass uses it, and passEnd closes it
// from the loop's tail, so a handle is never held across a pass (or across
// a [More], which can be for ever). A write closes it first: it must not
// read the file from before.
// ---------------------------------------------------------------------------
FILE* g_rd = nullptr;

FILE* reader() {
    if (!g_rd) {
        char p[96];
        path(p, sizeof(p));
        g_rd = disk::open(p, "rb");
    }
    return g_rd;
}

void readerClose() {
    if (g_rd) { fclose(g_rd); g_rd = nullptr; }
}

// endStrings: a record's strings ended, whatever the bytes said
void endStrings(CallRec& r) {
    r.user[BBS_USER_MAX] = '\0';
    r.ip[sizeof(r.ip) - 1] = '\0';
}

// ---------------------------------------------------------------------------
// loadHeader: read (or reset) the cached header once, and with it the newest
// kRecent records. Once per boot, so these reads are on nobody's call.
// ---------------------------------------------------------------------------
void loadHeader() {
    if (g_loaded) return;
    g_loaded = true;
    memcpy(g_hdr.magic, kMagic, 4);
    g_hdr.next = g_hdr.count = 0;
    g_recentN  = 0;

    char p[96];
    path(p, sizeof(p));
    FILE* f = disk::open(p, "rb");
    if (!f) return;
    Header h;
    if (fread(&h, sizeof(h), 1, f) == 1 && !memcmp(h.magic, kMagic, 4) &&
        h.next < BBS_CALLLOG_SIZE && h.count <= BBS_CALLLOG_SIZE) {
        g_hdr = h;
        uint8_t want = g_hdr.count < calllog::kRecent ? static_cast<uint8_t>(g_hdr.count)
                                                      : calllog::kRecent;
        for (uint8_t back = 0; back < want; ++back) {
            uint16_t slot = static_cast<uint16_t>((g_hdr.next + BBS_CALLLOG_SIZE - 1 - back) %
                                                  BBS_CALLLOG_SIZE);
            if (fseek(f, slotOffset(slot), SEEK_SET) != 0 ||
                fread(&g_recent[back], sizeof(CallRec), 1, f) != 1) break;
            endStrings(g_recent[back]);
            g_recentN = static_cast<uint8_t>(back + 1);
        }
    }
    fclose(f);
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
    FILE* f = disk::open(p, "a");
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
// The mirror, on the background runner (1.1.2). A hang-up is on the caller's
// own path, and the card copy was a mkdir, an open, an append and a flush
// there, with an SD card's write-busy at up to 250 ms (a sysop's hang-up
// measured 60 to 220 ms on the bench). Now the record goes into a small
// queue and the runner writes it; the loop only hands it over. A queue that
// is full (four hang-ups before the runner got to any of them) loses the
// card copy of the one that did not fit, and says so: the ring above, which
// is the record, has it.
// ---------------------------------------------------------------------------
constexpr uint8_t kMirrorQ = 4;
CallRec     g_mq[kMirrorQ];
uint8_t     g_mqHead  = 0;           // under the runner's lock
uint8_t     g_mqCount = 0;
runner::Job g_mirrorJob;

void mirrorWork(runner::Job&) {
    for (;;) {
        CallRec r;
        plat::runLock();
        if (!g_mqCount) { plat::runUnlock(); break; }
        r = g_mq[g_mqHead];
        g_mqHead = static_cast<uint8_t>((g_mqHead + 1) % kMirrorQ);
        --g_mqCount;
        plat::runUnlock();
        mirror(r);
        runner::breathe();
    }
}

void mirrorTick();

void mirrorQueue(const CallRec& r) {
    if (!plat::sdBase()[0]) return;             // no card, nothing to mirror to
    plat::runLock();
    const bool room = g_mqCount < kMirrorQ;
    if (room) {
        g_mq[(g_mqHead + g_mqCount) % kMirrorQ] = r;
        ++g_mqCount;
    }
    plat::runUnlock();
    if (!room) plat::log("calllog: the card copy of a call was dropped: the queue was full");
    mirrorTick();
}

// ---------------------------------------------------------------------------
// append: the record into slot "next", and the header that says so.
//
// Written front to back in one run since 1.1.2. The record went in at its
// slot and then a seek back wrote the header at the front, and on LittleFS a
// seek in the middle of writing ends that write: the file's block was copied
// to a new one for the record, and copied again to another for the header.
// Two 4 KB erases on every hang-up, both cores stopped for each. Now the
// header goes first and the records ahead of the slot are carried across
// from a second, read-only handle on the same file, so the writing handle
// never seeks: one block copied, and LittleFS brings the records after the
// slot across itself at the close, which commits the whole file at once (a
// power cut leaves the old log or the new one, never half). The reader costs
// an open; the erase it saves costs far more.
//
// A file shorter than its header says (it cannot be, written this way, but
// a partition can hold anything) is written the old way, with the seek,
// because the run would have nothing to carry across.
// ---------------------------------------------------------------------------
static bool writeRec(const CallRec& r) {
    loadHeader();
    readerClose();                               // not the file from before this write
    char p[96];
    path(p, sizeof(p));

    Header h = g_hdr;
    const uint16_t slot = h.next;
    h.next = static_cast<uint16_t>((h.next + 1) % BBS_CALLLOG_SIZE);
    if (h.count < BBS_CALLLOG_SIZE) ++h.count;

    bool ok = false;
    FILE* in = disk::open(p, "rb");
    long have = -1;
    if (in && fseek(in, 0, SEEK_END) == 0) have = ftell(in);
    if (in && have >= slotOffset(slot) && fseek(in, slotOffset(0), SEEK_SET) == 0) {
        FILE* f = disk::open(p, "r+b");
        if (f) {
            ok = fwrite(&h, sizeof(h), 1, f) == 1;
            // The records ahead of the slot, as they lie, a record at a time
            // through the stack.
            CallRec carry;
            for (uint16_t i = 0; ok && i < slot; ++i) {
                if (fread(&carry, sizeof(carry), 1, in) != 1) {
                    // A read that fails half way cannot be undone: the header
                    // is written. What it carries is a blank record, which
                    // LAST shows as nobody, rather than a shifted log.
                    carry = CallRec();
                    plat::log("calllog: a record could not be read while writing; left blank");
                }
                ok = fwrite(&carry, sizeof(carry), 1, f) == 1;
            }
            // The reader is done before the writer commits. LittleFS keeps an
            // open reader on the old blocks (they stay reserved while it is
            // open, lfs.c's DUSTY handling), so it never reads the new file,
            // and closing it first frees them the moment the writer commits.
            fclose(in);
            in = nullptr;
            ok = ok && fwrite(&r, sizeof(r), 1, f) == 1;
            ok = (fclose(f) == 0) && ok;
        }
        if (in) fclose(in);
    } else {
        if (in) fclose(in);
        FILE* f = disk::open(p, "r+b");
        if (!f) {
            // Only a log that is not there is made afresh: "w+b" over one
            // that could not be opened for another reason (no memory, no
            // handle free) would empty the sysop's security record.
            if (errno != ENOENT) { plat::log("calllog: cannot open %s", p); return false; }
            f = disk::open(p, "w+b");                  // first call ever
            if (!f) { plat::log("calllog: cannot create %s", p); return false; }
            h.next    = 1;
            h.count   = 1;
            g_recentN = 0;                         // nothing on file, nothing to copy
        }
        const uint16_t at = static_cast<uint16_t>((h.next + BBS_CALLLOG_SIZE - 1) % BBS_CALLLOG_SIZE);
        ok = fseek(f, 0, SEEK_SET) == 0 && fwrite(&h, sizeof(h), 1, f) == 1 &&
             fseek(f, slotOffset(at), SEEK_SET) == 0 && fwrite(&r, sizeof(r), 1, f) == 1;
        ok = (fclose(f) == 0) && ok;
    }
    if (ok) g_hdr = h;
    if (ok) {
        // The copy follows the file, and only a record the file took: a
        // failed write must not show on the dashboard as a call LAST has
        // never heard of.
        memmove(&g_recent[1], &g_recent[0], sizeof(CallRec) * (kRecent - 1));
        g_recent[0] = r;
        endStrings(g_recent[0]);
        if (g_recentN < kRecent) ++g_recentN;
        if (g_recentN > g_hdr.count) g_recentN = static_cast<uint8_t>(g_hdr.count);
        // Counted from the midnight today() last worked out. If the day has
        // turned since, today() sees a new midnight and counts the file
        // again, this record included, so a stale addition is never read.
        if (g_todayStart && r.start >= g_todayStart && g_today < 0xFFFF) ++g_today;
    } else {
        plat::log("calllog: write failed");
    }
    mirrorQueue(r);                              // the card's copy, on the runner (1.1.2)
    return ok;
}

// ---------------------------------------------------------------------------
// append: one flash write a pass (1.1.2). Each write above is a block copied
// with both cores stopped, and two callers hanging up in the same pass (a
// SHUTDOWN, a Wi-Fi drop, two BYEs typed together) paid two in one pass. A
// record that arrives in a pass that has already written flash, or behind
// one still waiting, waits in a small queue, oldest first, and writeOne
// writes one from the loop's tail in a pass that has written nothing else.
// A fifth before the first is written sends the oldest to the file at once:
// the log is the sysop's security record, and nothing is dropped from it.
// LAST and DASH show a waiting call a pass or two later, when it is written.
// ---------------------------------------------------------------------------
namespace {
constexpr uint8_t kPendQ = 4;
CallRec g_pend[kPendQ];
uint8_t g_pendHead = 0;
uint8_t g_pendN    = 0;
} // namespace

bool append(const CallRec& r) {
    if (!g_pendN && !disk::tally().writes) return writeRec(r);
    if (g_pendN == kPendQ) {                         // full: the oldest now, in order
        writeRec(g_pend[g_pendHead]);
        g_pendHead = static_cast<uint8_t>((g_pendHead + 1) % kPendQ);
        --g_pendN;
    }
    g_pend[(g_pendHead + g_pendN) % kPendQ] = r;
    ++g_pendN;
    return true;
}

bool writeOne() {
    if (!g_pendN) return false;
    const CallRec r = g_pend[g_pendHead];
    g_pendHead = static_cast<uint8_t>((g_pendHead + 1) % kPendQ);
    --g_pendN;
    writeRec(r);
    return true;
}

uint8_t count() {
    loadHeader();
    return static_cast<uint8_t>(g_hdr.count);
}

uint8_t countSince(uint32_t epoch) {
    loadHeader();
    if (!g_hdr.count) return 0;
    FILE* f = reader();                            // the pass's handle (1.1.2)
    if (!f) return 0;
    uint8_t n = 0;
    CallRec r;
    if (fseek(f, slotOffset(0), SEEK_SET) != 0) return 0;
    for (uint16_t i = 0; i < g_hdr.count; ++i) {
        if (fread(&r, sizeof(r), 1, f) != 1) break;
        if (r.start && r.start >= epoch) ++n;
    }
    return n;
}

bool get(uint8_t back, CallRec& out) {
    loadHeader();
    if (back >= g_hdr.count) return false;
    if (back < g_recentN) {                        // the newest: no file
        out = g_recent[back];
        return true;
    }
    uint16_t slot = static_cast<uint16_t>((g_hdr.next + BBS_CALLLOG_SIZE - 1 - back) % BBS_CALLLOG_SIZE);
    FILE* f = reader();                            // one open a pass, not one a row (1.1.2)
    if (!f) return false;
    bool ok = fseek(f, slotOffset(slot), SEEK_SET) == 0 && fread(&out, sizeof(out), 1, f) == 1;
    endStrings(out);
    return ok;
}

void passEnd() {
    readerClose();
}

uint16_t today() {
    uint32_t start = clk::todayStart();
    if (!start) return 0;
    if (start != g_todayStart) {                   // a new day, or a new timezone
        g_todayStart = start;
        g_today      = countSince(start);
    }
    return g_today;
}


// mirrorTick: post the mirror job while records wait and it is not out. The
// loop's, from Bbs::tick: a record queued while the job was finishing is
// picked up here rather than left until the next hang-up.
void mirrorTick() {
    if (runner::done(g_mirrorJob)) runner::collect(g_mirrorJob);
    if (!runner::idle(g_mirrorJob)) return;
    plat::runLock();
    const bool waiting = g_mqCount != 0;
    plat::runUnlock();
    if (!waiting) return;
    g_mirrorJob.work = mirrorWork;
    g_mirrorJob.name = "call log mirror";
    runner::post(g_mirrorJob);
}
} // namespace calllog
