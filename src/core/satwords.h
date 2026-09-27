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
//               and nowhere else, so a rename is one edit (Rob, 2026-09-27:
//               the names are under review, internal/naming-satellites-
//               2026-09-27.md proposes camera/door for the kinds and "door
//               sat"/"feeder" for the classes).
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

// The kinds as shown (ulink::kindName returns these). Proposed: "camera"
// and "door"; kept as shipped until Rob picks.
constexpr const char kKindCamera[] = "camsat";
constexpr const char kKindDoor[]   = "doorbox";
constexpr const char kKindOther[]  = "device";

// A door box, in sentences (doors.cpp). Proposed: "door sat".
constexpr const char kDoorBox[]    = "door box";

// The verbs.
constexpr const char kVerbSats[]   = "SATS";        // the list (callers and staff)
constexpr const char kVerbLink[]   = "LINK";        // the radio and pairing (staff)

}  // namespace satwords
