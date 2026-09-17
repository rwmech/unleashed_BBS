/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/bus.cpp
 * Module:       Core / message bus
 *
 * Purpose:      Per-session message ring (see bus.h).
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

#include "bus.h"

// ---------------------------------------------------------------------------
// push: append at the tail, overwrite the oldest when full
// ---------------------------------------------------------------------------
bool Mailbox::push(const BusMsg& m) {
    bool kept = true;
    if (count_ == BBS_BUS_DEPTH) {
        head_ = static_cast<uint8_t>((head_ + 1) % BBS_BUS_DEPTH);
        --count_;
        kept = false;
    }
    q_[(head_ + count_) % BBS_BUS_DEPTH] = m;
    ++count_;
    return kept;
}

// ---------------------------------------------------------------------------
// pop: remove from the head
// ---------------------------------------------------------------------------
bool Mailbox::pop(BusMsg& out) {
    if (!count_) return false;
    out   = q_[head_];
    head_ = static_cast<uint8_t>((head_ + 1) % BBS_BUS_DEPTH);
    --count_;
    return true;
}
