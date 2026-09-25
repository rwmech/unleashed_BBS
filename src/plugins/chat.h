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
#include "../core/term.h"

struct Session;
struct UserRec;

namespace chat {

// mailOn: mail is switched on (mail_slots above 0). Only an answer while the
// chat plugin is running, because mail lives inside it; the caller checks
// plugins::running first.
bool mailOn();

// leaveMail (1.1.0): a message from `from` to the account `to`, stored by
// the same writer MAIL uses, for a sender with no line of their own: the
// sysop page leaving a ring nobody answered, whose caller may have hung up.
// `to` is the account as users.txt has it, which the caller has in hand
// from its own pass over the file, so nothing is looked up again. The same
// rules as MAIL: nothing replaced, a full box or a full board refused, the
// recipient told "You have mail" if they are on. False, with nothing
// stored, for any of those or when chat is not running.
bool leaveMail(const char* from, const UserRec& to, const char* text);

// sysopUnread (1.1.0): a message not yet read is waiting for the sysop's
// account (Bbs::sysopAccount). From a flag kept in RAM as mail comes and
// goes, never a file read, because the display panel's letter icon asks for
// it on every frame. False when chat is not running. Bbs::sysopMail is the
// way in for anything outside the core.
bool sysopUnread();

// sysopChanged (1.1.0): the sysop's account may have moved (the setup named
// one, or a new account elevated), so the marks sysopUnread reads are taken
// again. One pass over users.txt; nothing when chat is not running.
void sysopChanged();

// The sysop page (1.1.0) reaches into the room through these three, the
// same way the lights reach Bbs::takeTraffic: a named call, not a hook every
// plugin would carry. Each is safe to call when chat is not running.

// inRoom: this caller is standing in the room with nothing in front of them
// (not reading or writing mail). A ring for the sysop is two lines of the
// room's voice there, not a one-key question, because /o answers in the room.
bool inRoom(const Session& s);

// roomSay: a line in the room's own voice, the --> marker and the text,
// word wrapped at the reader's width. For a caller whose input line has
// already been lifted.
void roomSay(Session& s, Color c, const char* text);

// converse: put a caller in the room talking to one node only: the sticky
// private (/p n*) already aimed, [>n] on their input line, and `say` as the
// last line before it. Walks them in if they are not there; a room ban does
// not stop somebody the sysop answered. False when the room could not take
// them, and then nothing has been drawn for them.
bool converse(Session& s, uint8_t withNode, const char* say);

} // namespace chat
