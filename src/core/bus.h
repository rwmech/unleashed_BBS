/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/bus.h
 * Module:       Core / message bus
 *
 * Purpose:      Message bus between sessions. Each session owns a fixed
 *                  ring of BBS_BUS_DEPTH messages; posting never allocates.
 *                  Messages are addressed by session, and the sender's handle
 *                  travels as a display string only, so C2 user records can
 *                  change what a handle is without changing this format.
 *                  Delivery happens when the receiver is back at a prompt.
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
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

#pragma once
#include <cstdint>
#include "../config.h"

enum class BusKind : uint8_t {
    Page,        // PAGE n msg from another node
    Notice,      // arrivals and departures
    Broadcast,   // sysop to everyone
    Mail,        // "You have mail", from the chat plugin's mail
    // Appended, like Mail. Somebody logging on, which rings the bell for
    // callers who have it on (Rob: "A bell when somebody logs in"). A
    // departure is still a Notice: nobody needs a bell to learn that
    // somebody left.
    Arrival,
    // Appended (1.1.0). A caller ringing for the sysop (OPERATOR). Only the
    // sysop session being rung ever gets one, and the text is the ring's
    // sequence number rather than anything to print: the ring itself lives
    // on the board, one at a time, and a notice that arrives after its ring
    // has ended is dropped rather than asked about.
    Ring,
};

struct BusMsg {
    BusKind kind     = BusKind::Notice;
    uint8_t fromNode = 0;                        // 0 = sysop
    char    from[BBS_USER_MAX + 1] = {};
    char    text[BBS_LINE_MAX + 1] = {};
};

class Mailbox {
public:
    void clear() { head_ = count_ = 0; }
    bool empty() const { return count_ == 0; }

    // push: queue a message; when full the oldest is dropped (returns false).
    // Never a Ring while anything else can go instead: there is at most one,
    // it is a question somebody is waiting on, and a burst of arrivals in
    // front of a sysop who is busy must not quietly cost the caller their
    // ring (1.1.0).
    bool push(const BusMsg& m);

    // pop: oldest message first
    bool pop(BusMsg& out);

    // take: the oldest message whose kind is in mask (a bit per BusKind),
    // leaving the rest where they were, in order. For a caller who can be
    // told some things now and not others: somebody in a form gets a
    // broadcast on its status line, while a page waits for the prompt,
    // where there is room to read it.
    bool take(uint32_t mask, BusMsg& out);

    // has: is a message of a kind in mask waiting?
    bool has(uint32_t mask) const;

    static constexpr uint32_t bit(BusKind k) { return 1u << static_cast<uint8_t>(k); }

private:
    BusMsg  q_[BBS_BUS_DEPTH];
    uint8_t head_  = 0;
    uint8_t count_ = 0;
};

#ifdef BBS_HAS_LCD
// ---------------------------------------------------------------------------
// The last page on the board, for the panel's "last event" (BBS_HAS_LCD
// boards only). Every page and every ring for the sysop goes through
// Mailbox::push, so it is noted there: no hook in the paging code, and
// nothing at all on a board without a panel. count wraps; only a change
// means anything, and the panel keeps its own clock of when it saw one.
// ---------------------------------------------------------------------------
namespace bus {
struct PageSeen {
    uint16_t count = 0;
    bool     ring  = false;                      // a ring for the sysop, not a caller's PAGE
    char     from[BBS_USER_MAX + 1] = {};
};
const PageSeen& lastPage();
} // namespace bus
#endif
