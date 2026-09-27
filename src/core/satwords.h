// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/satwords.h
// Module:       Core / the words for satellites, in one place (1.2.0)
//
// Purpose:      Every word a sysop or a caller reads for a box on the
//               µnleashed link: the umbrella ("satellite", "sat"), the kinds
//               and the commands that list them. Internally the kinds are
//               "camera" and "door" (link.h's KIND_*); what is shown is here
//               and nowhere else, so a rename is one edit.
//
//               Settled by Rob, 2026-09-27: "sat" and "sats"; an **orbiter**
//               is the data kind (camera, GPIO, sensors, Home Assistant); a
//               **door sat** is the kind a caller goes into (the program on
//               it stays a "door"). Going in is **UPLINK** (BEAM was
//               rejected). The kinds' short names in LINK (camsat, doorbox)
//               are not settled (internal/naming-satellites-2026-09-27.md).
//
//               Wire ids and the pairings file keep numbers, never these
//               words, so changing one needs no migration.
//
// Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1) and the Linux host build
// See also:     src/core/link.h (kindName), src/plugins/link.cpp, LINK.md
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3 of the License, or (at your
// option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>. The full
// text is in the LICENSE file at the top of this repository.
// ===========================================================================
#pragma once

namespace satwords {

// The umbrella, for every kind.
constexpr const char kSat[]       = "satellite";
constexpr const char kSats[]      = "satellites";
constexpr const char kSatTitle[]  = "Satellites";   // list titles
constexpr const char kSatShort[]  = "sat";

// The kinds as shown (ulink::kindName returns these), in LINK's kind
// column. Proposed: "camera" and "door"; kept as shipped until Rob picks.
constexpr const char kKindCamera[] = "camsat";
constexpr const char kKindDoor[]   = "doorbox";
constexpr const char kKindOther[]  = "device";

// The two classes, in sentences. Settled.
constexpr const char kDoorSat[]    = "door sat";     // a caller goes in (doors.cpp)
constexpr const char kDoorSats[]   = "door sats";
constexpr const char kOrbiter[]    = "orbiter";      // feeds the board data
constexpr const char kOrbiters[]   = "orbiters";
constexpr const char kDoorsHelp[]  = "games up on the sats";   // the DOORS | UPLINK row

// The verbs.
constexpr const char kVerbSats[]   = "SATS";        // the list (callers and staff)
constexpr const char kVerbLink[]   = "LINK";        // the radio and pairing (staff)
// Going into a door sat (Rob, 2026-09-27): UPLINK n, UPLINK name. No
// shortcut: U is FILES' upload key, and a verb that sends a caller
// somewhere is worth six letters. Checked against every verb and shortcut
// in the core and the plugins, and the room's commands: no clash.
constexpr const char kVerbEnter[]  = "UPLINK";
constexpr const char kVerbDoors[]  = "DOORS";
constexpr const char kDoorsUsage[] = "DOORS | UPLINK";   // one HELP row, like G | BYE

// The board's lines on the way in and back, in its voice ("--> " is added
// by Bbs::markedLine). At 40 columns each is at most 39 with the longest
// sat name (16) and the longest key name (RUN/STOP), except kNoSuch and
// kNoDoors, which carry what the caller typed and wrap at a word
// (markedLine). test_doors_petscii measures them from this file.
constexpr const char kUplinking[]  = "Uplinking to %s...";        // the sat's name
constexpr const char kHomeIs[]     = "Home is %s three times.";   // the home key
constexpr const char kBackHome[]   = "Back home.";
constexpr const char kGoesIn[]     = "UPLINK n or a name goes in.";
constexpr const char kNoSuch[]     = "No door or sat called %.20s.";
constexpr const char kNoDoorN[]    = "No such door. DOORS lists them.";
constexpr const char kNoDoors[]    = "%s has no doors on the air.";   // a sat, no doors
// Why a caller came back, before kBackHome. A door's own words (FINISHED,
// REFUSED) are the door's voice and are printed as they came.
constexpr const char kWhyTime[]    = "Time's up.";
constexpr const char kWhySignal[]  = "Lost the signal.";
constexpr const char kWhyNoAnswer[] = "The door didn't answer.";
constexpr const char kWhyFull[]    = "That door is full. Try again later.";
constexpr const char kWhyNo[]      = "The door said no.";
constexpr const char kWhyClosing[] = "The doors are closing.";
constexpr const char kWhyStopped[] = "Stopped.";
constexpr const char kBusy[]       = "The link is busy. Try again soon.";

}  // namespace satwords
