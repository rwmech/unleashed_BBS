/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         host/test_sshlink.cpp
 * Module:       Tests / the SSH link's ring (1.1.2)
 *
 * Purpose:      The BBS loop and the SSH task meet in a pair of these rings
 *               (src/core/sshlink.h), on two cores. A byte lost, doubled or
 *               reordered there is a corrupted XMODEM block or a keystroke
 *               that never arrives, and nothing downstream would say why.
 *               So: the edges on one thread (empty, full, the wrap, a ring
 *               with no buffer), then two threads, a producer and a
 *               consumer each taking odd-sized bites, 4 MB through a 4 KB
 *               ring, every byte checked in order.
 *
 * Libraries:    none (std::thread)
 * Targets:      the Linux host build (make test)
 * See also:     src/core/sshlink.h
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

#include "core/sshlink.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

int g_fail = 0, g_pass = 0;

void check(const char* name, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", name);
    ok ? ++g_pass : ++g_fail;
}

// The byte at position i of the stream: not a repeating pattern short
// enough to hide a slip of a whole ring's length.
uint8_t at(uint32_t i) {
    uint32_t x = i * 2654435761u;
    return static_cast<uint8_t>((x >> 13) ^ (x >> 24) ^ i);
}

} // namespace

int main() {
    printf("SSH link ring\n");
    {
        ssh::Ring r;
        uint8_t b[4];
        const uint8_t* p = nullptr;
        check("no buffer: nothing goes in", r.push(reinterpret_cast<const uint8_t*>("ab"), 2) == 0);
        check("no buffer: nothing comes out", r.pop(b, 4) == 0 && r.peek(p) == 0);
        check("no buffer: no room", r.room() == 0);
    }
    {
        static uint8_t mem[16];
        ssh::Ring r;
        r.reset(mem, sizeof(mem));
        uint8_t in[20], out[20];
        for (int i = 0; i < 20; ++i) in[i] = static_cast<uint8_t>(i + 1);
        check("empty: 16 of room", r.room() == 16 && r.used() == 0);
        check("full: 16 go in of 20", r.push(in, 20) == 16);
        check("full: no room, nothing more goes in", r.room() == 0 && r.push(in, 1) == 0);
        check("pop 10 in order", r.pop(out, 10) == 10 && !memcmp(out, in, 10));
        check("6 left, 10 of room", r.used() == 6 && r.room() == 10);
        check("8 more go in, across the wrap", r.push(in + 16, 4) == 4 && r.push(in, 4) == 4);
        const uint8_t* p = nullptr;
        size_t run = r.peek(p);
        check("peek stops at the wrap", run == 6 && p == mem + 10 && !memcmp(p, in + 10, 6));
        r.drop(run);
        size_t n = r.pop(out, 20);
        uint8_t want[8] = { 17, 18, 19, 20, 1, 2, 3, 4 };
        check("the rest, in order, after the wrap", n == 8 && !memcmp(out, want, 8));
        check("empty again", r.used() == 0 && r.pop(out, 1) == 0);
    }
    {
        // Two threads, as on the board: the loop on one core, the SSH task
        // on the other.
        constexpr uint32_t kTotal = 4u * 1024u * 1024u;
        static uint8_t mem[4096];
        ssh::Ring r;
        r.reset(mem, sizeof(mem));
        uint32_t bad = 0, got = 0;
        std::thread consumer([&]() {
            uint8_t buf[700];
            uint32_t i = 0, bite = 1;
            while (i < kTotal) {
                size_t n = r.pop(buf, bite);
                for (size_t k = 0; k < n; ++k)
                    if (buf[k] != at(i + static_cast<uint32_t>(k))) ++bad;
                i += static_cast<uint32_t>(n);
                bite = bite % 699 + 1;
            }
            got = i;
        });
        std::thread producer([&]() {
            uint8_t buf[1100];
            uint32_t i = 0, bite = 3;
            while (i < kTotal) {
                uint32_t n = bite;
                if (n > kTotal - i) n = kTotal - i;
                for (uint32_t k = 0; k < n; ++k) buf[k] = at(i + k);
                size_t put = r.push(buf, n);          // part of it when the ring is full
                i += static_cast<uint32_t>(put);
                bite = bite % 1097 + 3;
            }
        });
        producer.join();
        consumer.join();
        check("4 MB across two threads: every byte, in order", got == kTotal && bad == 0);
        check("and the ring ends empty", r.used() == 0);
    }
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
