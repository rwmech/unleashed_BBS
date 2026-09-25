/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/info.h
 * Module:       Plugins / information pages
 *
 * Purpose:      The two doors into the information pages that are not the
 *               INFO command itself: the chat room's /i, /i3 and /i3-.
 *               One store, two doors, the same rules behind both.
 *
 * Interfaces:   info::roomIndex, info::roomShow, info::roomClear
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     info.cpp
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
#include <cstdint>

struct Session;

namespace info {

// roomIndex: the list, printed straight into the room with no paging. The
// room has no pager, and ten short titles do not need one.
void roomIndex(Session& s);

// roomShow: one page, printed straight. A long page scrolls in the room;
// INFO n at the main prompt pages it with [More].
void roomShow(Session& s, uint8_t n);

// roomClear: /i3-, for somebody with the plugin's write level.
void roomClear(Session& s, uint8_t n);

} // namespace info
