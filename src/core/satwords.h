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
//               rejected). A sat's type is camera, door, gpio or sensor;
//               "camsat" is only the camera satellite's firmware and
//               repository name, never a type on screen.
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

// The types as shown (ulink::kindName returns these), in LINK's kind
// column and the pairing question. Settled by Rob, 2026-09-27. gpio and
// sensor have no wire kind yet (internal/sat-types-2026-09-27.md).
constexpr const char kKindCamera[] = "camera";
constexpr const char kKindDoor[]   = "door";
constexpr const char kKindGpio[]   = "gpio";
constexpr const char kKindSensor[] = "sensor";
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

// A camera sat taking a caller's photo (1.2.0-link.18, Rob). "--> " is added
// by the camera sat plugin; %s is kCamSat, or kCamSatShort when the line
// would not fit the caller's row with its arrow (and the spinner, on the two
// that end in "..."); %u is the camera number SNAPSHOT takes (2 to 9).
// test_sats measures them at 40: every line a caller is shown fits 39.
constexpr const char kCamSat[]      = "Camera sat";
constexpr const char kCamSatShort[] = "Sat";
constexpr const char kSnapContact[] = "Contacting camera sat #%u...";   // fits at 40 as it is
constexpr const char kSnapWait[]    = "%s #%u is busy, %u ahead of you...";
constexpr const char kSnapNoAnswer[] = "%s #%u didn't answer.";
constexpr const char kSnapSlow[]    = "%s #%u took too long.";
constexpr const char kSnapDamaged[] = "%s #%u's picture came damaged.";
constexpr const char kSnapNoPic[]   = "%s #%u's camera gave no picture.";
constexpr const char kSnapFull[]    = "%s #%u is full, try again soon.";
constexpr const char kSnapBusy[]    = "%s #%u is busy, try again soon.";
constexpr const char kSnapOther[]   = "%s #%u: %s.";                   // any other reason, cut to fit
// This board's one picture is another sat's: the wait is the board's, not
// the sat's. %s is kCamSat or kCamSatShort (so "Camera sats", "Sats").
constexpr const char kSnapWaitHere[] = "%ss busy here, %u ahead of you...";
constexpr const char kSnapHereFull[] = "%ss busy here, try again soon.";

// The link on a board that is on its wire (BBS_HAS_ETH, Ethernet up): Wi-Fi
// stands by unjoined, and ESP-NOW needs it joined (1.2.1 fixes it).
constexpr const char kOnWire[]      = "The link needs Wi-Fi. This board is on Ethernet, so sats can't pair.";
constexpr const char kOnWireShort[] = "On Ethernet: the link needs Wi-Fi.";
constexpr const char kOnWireRow[]   = "needs Wi-Fi, on Ethernet";       // CONFIG sats' row value at 40

}  // namespace satwords
