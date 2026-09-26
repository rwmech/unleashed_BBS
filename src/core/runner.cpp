// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/runner.cpp
// Module:       Core / the background runner (1.1.2)
//
// Purpose:      The queue and the task behind runner.h. See there for what
//               a job may and may not do.
//
//               The task is started when a job is posted and nothing is
//               running, and it ends once the queue has been empty for
//               kLingerMs. Both decisions are taken under the runner's lock,
//               so a post that lands while the task is deciding to end is
//               either seen by it (it carries on) or starts a new one: never
//               a job left in the queue with nobody to run it.
//
//               The one global trampoline this replaces (plat::taskStart's
//               g_worker) could run the second of two jobs twice when two
//               were started in one pass. Nothing here is handed over
//               through a global a task reads later: the queue is read under
//               the lock, one Job* at a time.
//
// Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
// See also:     src/core/runner.h
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
// ===========================================================================

#include "runner.h"
#include "../config.h"
#include "../platform/platform.h"

namespace runner {
namespace {

// A few seconds of nothing to do before the task ends: a caller paging a
// file area posts a job a page, and a task started and ended for each page
// would take and give its stack from the heap over and over.
constexpr uint32_t kLingerMs = 3000;

Job*     g_q[kQueue] = {};
uint8_t  g_head  = 0;                    // under the lock
uint8_t  g_count = 0;                    // under the lock
bool     g_alive = false;                // a task is running main(); under the lock
bool     g_inHand = false;               // a job taken and not yet DONE; under the lock

// Written by the runner, read by the loop for SYS. Single words: a torn
// read cannot happen, and a stale one is a figure a moment old.
std::atomic<const char*> g_current{ "" };
std::atomic<uint32_t>    g_stackLow{ 0 };
std::atomic<uint32_t>    g_done{ 0 };
std::atomic<uint32_t>    g_longest{ 0 };
std::atomic<const char*> g_longestName{ "" };

Job* take() {
    plat::runLock();
    Job* j = nullptr;
    if (g_count) {
        j = g_q[g_head];
        g_q[g_head] = nullptr;
        g_head = static_cast<uint8_t>((g_head + 1) % kQueue);
        --g_count;
    }
    g_inHand = j != nullptr;
    plat::runUnlock();
    return j;
}

// main: the task's body. Runs jobs until the queue has stayed empty for
// kLingerMs, then says so under the lock and returns (the platform ends the
// task).
void main() {
    for (;;) {
        Job* j = take();
        if (!j) {
            if (plat::runWait(kLingerMs)) continue;       // woken: look again
            plat::runLock();
            if (!g_count) {
                g_alive = false;
                plat::runUnlock();
                plat::log("DBG runner: exit");
                return;
            }
            plat::runUnlock();
            continue;
        }
        j->st.store(RUNNING);
        plat::log("DBG runner: run %s", j->name);
        g_current.store(j->name);
        const uint32_t t0 = plat::millis();
        if (j->work) j->work(*j);
        j->tookMs = plat::millis() - t0;
        const uint32_t sf = plat::taskStackFree();
        if (sf) {
            uint32_t low = g_stackLow.load();
            if (!low || sf < low) g_stackLow.store(sf);
        }
        if (j->tookMs > g_longest.load()) {
            g_longest.store(j->tookMs);
            g_longestName.store(j->name);
        }
        g_done.fetch_add(1);
        g_current.store("");
        plat::runLock();
        g_inHand = false;
        plat::runUnlock();
        // Last: once the loop sees DONE the job is the loop's again, and it
        // may post it anew the same pass. Nothing of it is touched after.
        j->st.store(DONE);
    }
}

}  // namespace

bool post(Job& j) {
    uint8_t s = j.st.load();
    if (s == QUEUED || s == RUNNING) return false;
    j.postedAt = plat::millis();
    j.tookMs   = 0;
    plat::runLock();
    if (g_count >= kQueue) {
        plat::runUnlock();
        plat::log("runner: queue full, %s refused", j.name);
        return false;
    }
    j.st.store(QUEUED);
    g_q[(g_head + g_count) % kQueue] = &j;
    ++g_count;
    const bool start = !g_alive;
    plat::log("DBG runner: post %s start %d count %u", j.name, start ? 1 : 0, g_count);
    if (start) g_alive = true;
    plat::runUnlock();

    if (!start) {
        plat::runWake();
        return true;
    }
    if (plat::taskStart(main, BBS_RUNNER_STACK, "runner")) return true;

    // No task: take back what is queued, this job with the rest, and say
    // so. A job left QUEUED with nothing to run it would hang its caller.
    plat::log("runner: the task would not start (heap %u free)", static_cast<unsigned>(plat::heapFree()));
    plat::runLock();
    g_alive = false;
    while (g_count) {
        Job* q = g_q[g_head];
        g_q[g_head] = nullptr;
        g_head = static_cast<uint8_t>((g_head + 1) % kQueue);
        --g_count;
        if (q && q != &j) q->st.store(IDLE);
    }
    plat::runUnlock();
    j.st.store(IDLE);
    return false;
}

bool busy() {
    plat::runLock();
    const bool b = g_count != 0 || g_inHand;
    plat::runUnlock();
    return b;
}

const char* current()       { return g_current.load(); }
void        breathe()       { plat::taskSleep(0); }
uint32_t    stackLow()      { return g_stackLow.load(); }
uint32_t    jobsDone()      { return g_done.load(); }
uint32_t    longestMs()     { return g_longest.load(); }
const char* longestName()   { return g_longestName.load(); }

}  // namespace runner
