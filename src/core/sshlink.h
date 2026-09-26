/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/sshlink.h
 * Module:       Core / the SSH link between the BBS loop and the SSH task
 *
 * Purpose:      What a caller's session holds instead of a socket when the
 *               caller came in over SSH (1.1.2, S3 only, BBS_HAS_SSH).
 *
 *               The loop never runs any SSH: key exchange is up to half a
 *               second of CPU, and in the cooperative loop that would be a
 *               slow pass for every caller on the board (Rule no. 1). So the
 *               SSH task (sshd.cpp, core 0, just above idle) owns the TCP
 *               socket and the crypto, and the two meet here: a byte ring
 *               each way, in PSRAM, and an eventfd the loop's select()
 *               already waits on, so SSH input wakes the loop exactly as a
 *               socket does. An eventfd is a VFS descriptor, not an lwIP
 *               socket, so it does not count against the sixteen.
 *
 *               Ring: one producer and one consumer, free-running 32-bit
 *               counters, a power-of-two size. The producer writes the
 *               bytes, then publishes the head (release); the consumer reads
 *               the head (acquire), then the bytes. Nothing else is shared
 *               without an atomic.
 *
 *               Who owns what: the loop claims a link (sshd::claim), after
 *               which the task allocates the rings and runs the handshake.
 *               Either side may end it. The loop sets loopDone and never
 *               touches the link again; the task sets taskDone once the
 *               socket is closed. The task frees the rings and returns the
 *               slot only when both are set, because until then the loop
 *               may still be reading the last keys out of the inbound ring.
 *
 * Libraries:    none (std::atomic)
 * Targets:      ESP32-S3 (ESP-IDF 5.3.1) and the Linux host build
 * See also:     src/core/sshd.h, internal/ssh-research-2026-09-26.md
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

#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ssh {

// ---------------------------------------------------------------------------
// Ring: single producer, single consumer. buf is size bytes, size a power
// of two, or nullptr while the link has no rings (everything then reads as
// empty and full, so a stray call does nothing).
// ---------------------------------------------------------------------------
struct Ring {
    uint8_t*              buf  = nullptr;
    uint32_t              size = 0;
    std::atomic<uint32_t> head{0};     // written by the producer
    std::atomic<uint32_t> tail{0};     // written by the consumer

    void reset(uint8_t* b, uint32_t n) {
        buf  = b;
        size = n;
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
    }

    // used / room: as the caller's side sees it; the other side can only
    // make used() grow (for the consumer) or room() grow (for the producer).
    uint32_t used() const {
        return head.load(std::memory_order_acquire) - tail.load(std::memory_order_acquire);
    }
    uint32_t room() const { return buf ? size - used() : 0; }

    // push: the producer's; as much of d as fits, returns how much.
    size_t push(const uint8_t* d, size_t n) {
        if (!buf) return 0;
        const uint32_t h = head.load(std::memory_order_relaxed);
        const uint32_t t = tail.load(std::memory_order_acquire);
        size_t free = size - (h - t);
        if (n > free) n = free;
        for (size_t i = 0; i < n; ++i) buf[(h + i) & (size - 1)] = d[i];
        head.store(h + static_cast<uint32_t>(n), std::memory_order_release);
        return n;
    }

    // peek: the consumer's; a pointer to the next contiguous run and its
    // length (up to the wrap), without taking it. drop takes n of it.
    size_t peek(const uint8_t*& p) const {
        if (!buf) return 0;
        const uint32_t t = tail.load(std::memory_order_relaxed);
        const uint32_t h = head.load(std::memory_order_acquire);
        size_t n = h - t;
        const uint32_t at = t & (size - 1);
        if (n > size - at) n = size - at;
        p = buf + at;
        return n;
    }
    void drop(size_t n) {
        tail.store(tail.load(std::memory_order_relaxed) + static_cast<uint32_t>(n),
                   std::memory_order_release);
    }

    // pop: the consumer's; up to n bytes into d, returns how many.
    size_t pop(uint8_t* d, size_t n) {
        size_t got = 0;
        while (got < n) {
            const uint8_t* p = nullptr;
            size_t run = peek(p);
            if (!run) break;
            if (run > n - got) run = n - got;
            memcpy(d + got, p, run);
            drop(run);
            got += run;
        }
        return got;
    }
};

// ---------------------------------------------------------------------------
// Link states, as the task moves them. The loop only ever reads state.
// ---------------------------------------------------------------------------
enum class LState : uint8_t {
    Free,        // in the pool
    Start,       // claimed by the loop, the task has not seen it yet
    Handshake,   // key exchange and authentication, the task's
    Open,        // a shell channel: bytes flow
    Done,        // the task has closed the socket; freed once loopDone
};

// The authentication question the task hands the loop (the account system
// is the loop's: users.txt, the lockout, the bans). Asked once at a time.
enum : uint8_t { AUTH_IDLE = 0, AUTH_ASKED, AUTH_YES, AUTH_NO };
enum : uint8_t { AUTH_NONE = 0, AUTH_PASSWORD };

constexpr size_t kPreMax = 80;         // "SSH-2.0-" and one BBS_RX_CHUNK read after it

struct Link {
    std::atomic<uint8_t>  state{static_cast<uint8_t>(LState::Free)};
    std::atomic<bool>     loopDone{false};   // the session is gone: send what is left, close
    std::atomic<bool>     taskDone{false};   // the socket is closed
    int      sock  = -1;                     // the TCP socket, the task's from claim on
    int      efd   = -1;                     // task -> loop wake; made once, kept
    uint8_t  node  = 0;                      // the session it was claimed for (the log)
    uint32_t peer  = 0;                      // the caller's IPv4 address, network order
    uint32_t local = 0;                      // the board's own, for the backup window
    uint8_t  pre[kPreMax] = {};              // bytes the loop read before it knew
    uint8_t  preLen = 0;
    uint8_t  preAt  = 0;                     // the task's: how many it has handed on
    uint8_t  asks   = 0;                     // the task's: login questions this connection

    Ring     in;                             // caller -> board: task pushes, loop pops
    Ring     out;                            // board -> caller: loop pushes, task pops

    std::atomic<uint16_t> cols{80}, rows{24};
    std::atomic<bool>     resized{false};    // a window-change the loop has not seen

    // Authentication, one question at a time. The task fills kind, user
    // and pass, then sets AUTH_ASKED (release); the loop answers AUTH_YES
    // or AUTH_NO (release) and wipes pass. Neither side touches the text
    // while the other one holds the question.
    std::atomic<uint8_t>  auth{AUTH_IDLE};
    uint8_t  authKind = AUTH_NONE;
    char     user[33] = {};
    char     pass[65] = {};
    std::atomic<uint8_t> pwFails{0};         // the task's: wrong passwords this connection
    std::atomic<bool>    authed{false};      // the task's: authentication succeeded
    char     why[48]  = {};                  // the task's: why it ended (read once taskDone)
};

} // namespace ssh
