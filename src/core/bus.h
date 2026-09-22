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

    // push: queue a message; when full the oldest is dropped (returns false)
    bool push(const BusMsg& m);

    // pop: oldest message first
    bool pop(BusMsg& out);

private:
    BusMsg  q_[BBS_BUS_DEPTH];
    uint8_t head_  = 0;
    uint8_t count_ = 0;
};
