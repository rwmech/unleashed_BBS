// Checks for the caller log's copy of itself in RAM (1.1.0): the newest
// calls and today's count, which the dashboard and the login line read
// instead of the file.
//
// The copy is only worth having if it is the file's copy and never its own
// story, so every check here compares what comes out of RAM with what a
// fresh read of the file would say, then takes the file away to show the
// answers really are not coming from it. The platform and the clock are
// stubbed with the few calls calllog.cpp makes: a directory to keep the log
// in, no card, and a midnight the test chooses.
//
// The rule it serves, that a dashboard frame opens no file, is checked end
// to end on a running board by tools/testclient.py (test_dash_opens_nothing).
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3 of the License, or (at your
// option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>. The full
// text is in the LICENSE file at the top of this repository.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include "../src/core/calllog.h"
#include "../src/core/clock.h"
#include "../src/platform/platform.h"
#include "../src/core/runner.h"

// ---- stubs: what calllog.cpp asks of the platform and the clock -----------
static std::string g_dir;
static uint32_t    g_midnight = 0;

namespace plat {
const char* logsBase() { return g_dir.c_str(); }
const char* sdBase()   { return ""; }                  // no card: no mirror
void log(const char* fmt, ...) { (void)fmt; }
void diskPulse(DiskKind) {}                            // the drive light (core/disk.h, 1.1.1)
void hostDiskOpen(const char*, const char*) {}         // the host's cost per open (1.1.2)
bool onLoop() { return false; }                        // disk.h's tally of the loop's opens (1.1.2)
uint32_t micros() { return 0; }
void runLock() {}                                      // the card mirror's queue (1.1.2)
void runUnlock() {}
}
namespace runner {
bool post(Job&) { return false; }                      // no card here, so nothing is mirrored
void breathe() {}
}
namespace clk {
uint32_t todayStart() { return g_midnight; }
size_t fmtEpoch(char* buf, size_t n, const char* f, uint32_t e) {
    (void)f;
    return static_cast<size_t>(snprintf(buf, n, "%u", static_cast<unsigned>(e)));
}
}

static int fails = 0, passes = 0;

static void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passes; else ++fails;
}

static CallRec call(const char* who, uint32_t start) {
    CallRec r;
    snprintf(r.user, sizeof(r.user), "%s", who);
    snprintf(r.ip, sizeof(r.ip), "10.0.0.%u", static_cast<unsigned>(start % 250u));
    r.node  = 1;
    r.start = start;
    r.secs  = 60;
    return r;
}

static std::string logPath() { return g_dir + "/" + BBS_CALLLOG_FILE; }

int main() {
    char tmpl[] = "/tmp/calllog-XXXXXX";
    const char* d = mkdtemp(tmpl);
    if (!d) { perror("mkdtemp"); return 2; }
    g_dir = d;

    printf("Caller log: the newest calls in RAM\n");
    {
        CallRec r;
        check("an empty log has nothing", calllog::count() == 0 && !calllog::get(0, r));
        const char* who[] = { "ann", "bob", "cat", "dan", "eve", "fay", "gus" };
        for (unsigned i = 0; i < 7; ++i) calllog::append(call(who[i], 1000u + i));
        check("seven calls are counted", calllog::count() == 7);
        bool order = true;
        for (unsigned back = 0; back < 7; ++back) {
            order &= calllog::get(static_cast<uint8_t>(back), r) &&
                     !strcmp(r.user, who[6 - back]) && r.start == 1000u + 6 - back;
        }
        check("newest first, all seven, the newest five from RAM and the rest from the file", order);

        // Take the file away. The newest five must still come back, because
        // they never came from it; the sixth must not, because it did. At a
        // pass's end, where the loop closes the read handle (1.1.2): one
        // still open would read the file it had.
        calllog::passEnd();
        unlink(logPath().c_str());
        bool ram = true;
        for (unsigned back = 0; back < calllog::kRecent; ++back)
            ram &= calllog::get(static_cast<uint8_t>(back), r) && !strcmp(r.user, who[6 - back]);
        check("with the file gone the newest five are still there: no file was opened", ram);
        check("and the sixth is not: the copy holds five and says so", !calllog::get(5, r));
    }

    printf("Caller log: today's count\n");
    {
        // A fresh log for this part. The count starts from a file pass the
        // first time it is asked for a midnight, and is only added to after.
        calllog::passEnd();
        unlink(logPath().c_str());
        g_midnight = 0;
        check("no clock, no count", calllog::today() == 0);
        g_midnight = 5000;
        uint16_t before = calllog::today();
        check("a midnight after every call so far counts none of them", before == 0);
        calllog::append(call("today1", 5000));
        calllog::append(call("today2", 6000));
        calllog::append(call("yesterday", 4999));
        check("calls from midnight on are counted as they are logged, one before is not",
              calllog::today() == 2);
        calllog::passEnd();
        unlink(logPath().c_str());
        check("and the count comes from RAM, not the file", calllog::today() == 2);
        g_midnight = 6000;                       // the day turned (or the timezone moved)
        check("a new midnight counts the file again, and a missing file is none",
              calllog::today() == 0);
    }

    printf("Caller log: the copy follows only what the file took\n");
    {
        CallRec r;
        // A log that cannot be written: its folder is gone. append() fails,
        // and neither the newest calls nor today's count may show a call the
        // file never took.
        g_midnight = 7000;
        calllog::append(call("kept", 7001));
        uint16_t today = calllog::today();
        std::string saved = g_dir;
        g_dir = saved + "/missing/deeper";
        bool wrote = calllog::append(call("lost", 7002));
        check("a write that fails says so", !wrote);
        check("and the newest call is still the one that was kept",
              calllog::get(0, r) && !strcmp(r.user, "kept"));
        check("and today's count did not move", calllog::today() == today);
        g_dir = saved;
    }

    printf("Caller log: written front to back, around the ring (1.1.2)\n");
    {
        // append() writes the header first and carries the records ahead of
        // the slot across from a second handle, so the writing handle never
        // seeks (a seek on LittleFS copies the block again). What it leaves
        // must be exactly the ring the old seek-and-write left: every slot,
        // the header, and the order LAST reads, across the wrap and past it.
        calllog::passEnd();
        unlink(logPath().c_str());
        g_midnight = 0;
        char who[24];
        for (unsigned i = 0; i < 123; ++i) {
            snprintf(who, sizeof(who), "ring%03u", i);
            calllog::append(call(who, 20000u + i));
            calllog::passEnd();
        }
        check("fifty kept after 123 calls", calllog::count() == BBS_CALLLOG_SIZE);
        CallRec r;
        bool order = true;
        for (unsigned back = 0; back < BBS_CALLLOG_SIZE; ++back) {
            snprintf(who, sizeof(who), "ring%03u", 122u - back);
            order &= calllog::get(static_cast<uint8_t>(back), r) && !strcmp(r.user, who) &&
                     r.start == 20000u + 122u - back;
        }
        calllog::passEnd();
        check("newest first, every one of them, through the one read handle", order);

        // The file itself, as the next boot will read it.
        FILE* f = fopen(logPath().c_str(), "rb");
        unsigned char hdr[8] = {};
        bool raw = f && fread(hdr, 1, 8, f) == 8 && !memcmp(hdr, "CLG1", 4);
        const unsigned next  = hdr[4] | (hdr[5] << 8);
        const unsigned count = hdr[6] | (hdr[7] << 8);
        check("the header says next 23 and count 50", raw && next == 123u % BBS_CALLLOG_SIZE &&
                                                       count == BBS_CALLLOG_SIZE);
        bool slots = f != nullptr;
        for (unsigned slot = 0; f && slot < BBS_CALLLOG_SIZE; ++slot) {
            // slot k holds the newest call that went to it: 100 + k before
            // the head, 50 + k from the head on
            const unsigned want = slot < next ? 100u + slot : 50u + slot;
            snprintf(who, sizeof(who), "ring%03u", want);
            slots &= fread(&r, sizeof(r), 1, f) == 1 && !strcmp(r.user, who);
        }
        long size = -1;
        if (f) { fseek(f, 0, SEEK_END); size = ftell(f); fclose(f); }
        check("every slot holds the call that belongs in it", slots);
        check("and the file is the ring's size, no more",
              size == static_cast<long>(8 + BBS_CALLLOG_SIZE * sizeof(CallRec)));
    }

    printf("Caller log: a file shorter than its header\n");
    {
        // Not a file this code writes, but a partition can hold anything: a
        // header that claims more than the file has is written the old way,
        // with the seek, and the call is still logged and read back.
        calllog::passEnd();
        FILE* f = fopen(logPath().c_str(), "r+b");
        bool cut = f && ftruncate(fileno(f), 8 + 3 * static_cast<long>(sizeof(CallRec))) == 0;
        if (f) fclose(f);
        check("cut to three records under a header of fifty", cut);
        bool wrote = calllog::append(call("aftercut", 30000));
        calllog::passEnd();
        CallRec r;
        check("the call is logged anyway", wrote && calllog::get(0, r) && !strcmp(r.user, "aftercut"));
    }

    calllog::passEnd();
    unlink(logPath().c_str());
    rmdir(g_dir.c_str());
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
