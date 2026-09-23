/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/chat.h
 * Module:       Plugins / chat
 *
 * Purpose:      What another plugin may ask the chat room. Today that is
 *               one question, from announce: is the room's mail switched
 *               on, so the directory's "mail" badge is only ever sent by a
 *               board where mail actually works.
 *
 * Interfaces:   chat::mailOn
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     chat.cpp, announce.cpp, CHAT.md
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

namespace chat {

// mailOn: mail is switched on (mail_slots above 0). Only an answer while the
// chat plugin is running, because mail lives inside it; the caller checks
// plugins::running first.
bool mailOn();

} // namespace chat
