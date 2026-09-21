/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/forums_ptr.h
 * Module:       Plugins / message boards
 *
 * Purpose:      The read pointer: a high-water mark plus a window of what
 *                  has been read above it, and the arithmetic over both.
 *
 * Design:       Pure functions over a plain struct, in a header of its own so
 *               they can be tested without a board, a card or a session. This
 *               is the piece of the forums most likely to be subtly wrong and
 *               least likely to look wrong, which is exactly the combination
 *               that earns a unit test.
 *
 * Notes:        The window width is part of the on-disk format and cannot be
 *               changed without converting every PTRS.TXT on every card.
 *
 * Libraries:    none (libc)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     PLAN-FORUMS.md, host/test_forums_ptr.cpp
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

#pragma once
#include <cstdint>
#include <cstring>

namespace forumptr {

// ===========================================================================
// Why this is not simply a number.
//
// A single forum-wide high-water mark cannot serve a subject list, and that
// is the whole reason this file exists. Rob's own example settled it: a HAM
// RADIO forum carrying "20m tips" and "20m antennas". Read the tips subject
// to its newest message and a bare mark declares the antennas subject read
// too, because it sits below the mark. Refuse to move the mark and the tips
// subject still says "3 new" immediately after somebody read all three.
// Either way the number on the screen is a lie, and the unread count is the
// first thing a caller checks.
//
// So: a mark, plus a window recording which messages ABOVE the mark have
// already been shown.
//
// **It self-drains, and that property is what makes 16 bytes enough rather
// than merely small.** Reading in order sets the bit immediately above the
// mark, the mark absorbs it at once, and the window shifts down. Ordinary
// sequential reading therefore never accumulates anything at all; the window
// only holds anything while somebody is reading out of order, and it empties
// itself as the gap behind them fills in.
//
// **The failure direction is the other half of the design.** A message read
// more than 128 above the mark cannot be recorded, so it stays unread and is
// shown again later. It never marks an unread message read; it only forgets
// that one was read. Of the two ways to be wrong, showing something twice is
// the one a caller forgives.
//
// 16 bytes is fixed on disk and cannot be widened later without converting
// every PTRS.TXT on every card, which is the one migration this whole format
// exists to avoid.
// ===========================================================================

constexpr uint8_t  kWindowBytes = 16;
constexpr uint32_t kWindowBits  = kWindowBytes * 8u;      // 128 messages

struct Ptr {
    uint32_t mark = 0;                      // everything at or below is read
    uint8_t  win[kWindowBytes] = {};        // bit i = message mark+1+i read
};

// seen: has this caller been shown message n?
//
// A message beyond the window is reported unread rather than unknown. That
// is the safe direction and it is deliberate: the alternative would hide a
// message somebody has not read.
inline bool seen(const Ptr& p, uint32_t n) {
    if (!n) return true;                    // record 0 is the header
    if (n <= p.mark) return true;
    uint32_t off = n - p.mark - 1;
    if (off >= kWindowBits) return false;
    return (p.win[off / 8] & (1u << (off % 8))) != 0;
}

// markSeen: record that message n was shown, then drain what that unlocked.
inline void markSeen(Ptr& p, uint32_t n) {
    if (!n || n <= p.mark) return;
    uint32_t off = n - p.mark - 1;
    if (off < kWindowBits)
        p.win[off / 8] |= static_cast<uint8_t>(1u << (off % 8));

    // The drain. While the message directly above the mark is already set,
    // the mark swallows it and the whole window shifts down one bit. This is
    // what stops the window filling over a long session: catching up gives
    // the space back.
    while (p.win[0] & 1u) {
        ++p.mark;
        for (uint8_t k = 0; k < kWindowBytes; ++k) {
            p.win[k] = static_cast<uint8_t>(p.win[k] >> 1);
            if (k + 1 < kWindowBytes && (p.win[k + 1] & 1u))
                p.win[k] |= 0x80;
        }
    }
}

// unreadUpTo: how many of messages 1..newest this caller has not seen.
//
// Counting rather than deriving from newest - mark, because the window means
// those two are not the same number, and the forum list and the subject list
// must agree or a caller sees a forum claiming 12 whose subjects sum to 9.
inline uint32_t unreadUpTo(const Ptr& p, uint32_t newest) {
    if (newest <= p.mark) return 0;
    uint32_t n = 0;
    for (uint32_t m = p.mark + 1; m <= newest; ++m)
        if (!seen(p, m)) ++n;
    return n;
}

}  // namespace forumptr
