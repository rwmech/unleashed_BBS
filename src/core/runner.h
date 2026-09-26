// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/runner.h
// Module:       Core / the background runner (1.1.2)
//
// Purpose:      One task below the BBS loop that does the slow work, so the
//               loop never has to (Rob's rule no. 1: the online experience
//               without lag is paramount).
//
//               The loop is cooperative: anything it does, every caller
//               waits for. A directory walk on the card, a partition's
//               free-space walk, a DNS lookup, a zip's CRC scan: each of
//               those held one pass for 100 ms to seconds (internal/audit-
//               1.1.2-2026-09-26.md). They run here instead, on the BBS
//               task's core three priorities below it, so whenever the loop
//               has anything to do it runs and the runner waits. The loop
//               shows a spinner and reads the answer when it is there.
//
//               Rob's shape, generalised from the camera's worker, which
//               was the first thing on the board built this way:
//
//                 - one job at a time, in the order they were posted, from
//                   a queue of kQueue that is static (no heap for the
//                   queue; the task's stack comes from the heap while it
//                   runs, as the camera's always did)
//                 - a job is a struct its owner keeps (static, never on a
//                   Session): the inputs are written before post(), the
//                   results by the runner, and the runner sets the state to
//                   DONE LAST. The loop reads the results only after it
//                   sees DONE, then collect()s the job back to IDLE.
//                 - a job that serves a caller carries the node and the
//                   call (Session::call), never a Session*: the caller may
//                   hang up, and the slot may hold somebody else by the
//                   time the answer comes back.
//                 - the task exists while there is work and a few seconds
//                   after, then ends, so a board with nothing to do keeps
//                   its heap.
//
//               What a job must not do: touch a Session, the timeline, the
//               bus, or anything else the loop owns; write a card file the
//               loop may be writing (CONFIG_FATFS_FS_LOCK=0, so two tasks
//               on one FAT file corrupt it: FILES.BBS has one writer, the
//               files plugin, and its writes come through here); or run for
//               long without breathe(), which lets the idle task on core 1
//               feed the watchdog.
//
//               A flash erase stops both cores whichever task asks for it
//               (SPI flash concurrency, esp-idf v5.3.1), so the runner does
//               not make a LittleFS write free for the loop. What it does
//               is spread one: between two of the runner's writes the loop
//               runs, where the same writes on the loop are one long pass.
//
// Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build,
//               where the runner is a thread, so the tests exercise the real
//               concurrency.
// See also:     src/platform/platform.h (taskStart, runLock), CLAUDE.md
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
// ===========================================================================
#pragma once

#include <atomic>
#include <cstdint>

namespace runner {

// A job's state. Only the loop moves IDLE/DONE to QUEUED (post) and DONE to
// IDLE (collect); only the runner moves QUEUED to RUNNING and RUNNING to DONE.
enum : uint8_t { IDLE = 0, QUEUED, RUNNING, DONE };

// The jobs waiting at once. Every kind of job there is has one static Job,
// so this only has to hold one of each that could be waiting together.
constexpr uint8_t kQueue = 8;

struct Job {
    std::atomic<uint8_t> st{ IDLE };
    void (*work)(Job& self) = nullptr;   // runs on the runner
    const char* name = "";               // for the console and SYS
    uint32_t postedAt = 0;               // millis, set by post()
    uint32_t tookMs   = 0;               // how long work() ran, set before DONE
};

// post: queue j (its work and its inputs set first). Loop only. False when
// it is already queued or running, or the queue is full, or the task would
// not start: the caller says so rather than waiting on nothing.
bool post(Job& j);

inline bool done(const Job& j)    { return j.st.load() == DONE; }
inline bool pending(const Job& j) { const uint8_t s = j.st.load(); return s == QUEUED || s == RUNNING; }
inline bool idle(const Job& j)    { return j.st.load() == IDLE; }

// collect: the loop has read a DONE job's results; it may be posted again.
inline void collect(Job& j) { uint8_t want = DONE; j.st.compare_exchange_strong(want, IDLE); }

// busy: anything queued or running. What SD UNMOUNT asks before taking the
// card away from under a job.
bool busy();

// current: the running job's name, "" when none. For SYS.
const char* current();

// breathe: inside a long job, give core 1 to anything waiting (the idle
// task, which feeds the watchdog). A tick; the loop never waits on it.
void breathe();

// stackLow: the least free stack the runner has had, in bytes, 0 before it
// has run (and on the host, where it is not measured). SYS shows it, so the
// stack's size is settled on the bench rather than guessed.
uint32_t stackLow();

// Measures for SYS: jobs finished since boot, and the longest one's time.
uint32_t jobsDone();
uint32_t longestMs();
const char* longestName();

}  // namespace runner
