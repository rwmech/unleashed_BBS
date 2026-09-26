// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/platform/linkradio.h
// Module:       Platform / the µnleashed link's radio (1.2.0)
//
// Purpose:      What the link plugin needs from the radio, and nothing more.
//               On the board it is ESP-NOW (linkradio_esp32.cpp); on the
//               host it is UDP on 127.0.0.1 (host/linkradio_host.cpp), so a
//               test program (host/linkpeer.cpp) can be a satellite or a door
//               box beside a host board.
//
//               Kept out of platform.h on purpose: it is one feature's seam,
//               and a file of its own is one that nobody else's change has
//               to merge through.
//
//               The receive side runs in the Wi-Fi task on the board: its
//               callback does nothing but copy the frame into a ring that the
//               loop empties with linkRadioRecv. Nothing is allocated per
//               frame; the ring is allocated by linkRadioStart and freed by
//               linkRadioStop.
//
//               Peers are added UNENCRYPTED at the ESP-NOW layer: the link
//               seals every frame itself (LINK.md, "Why the link encrypts for
//               itself").
//
// Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1) and the Linux host build
// See also:     LINK.md, src/core/link.h, src/plugins/link.cpp
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
#include <cstddef>
#include <cstdint>

namespace plat {

// linkRadioStart: ESP-NOW up (Wi-Fi must already be started), callbacks in,
// the broadcast address in the peer table, and a receive ring of `slots`
// frames. False, and the radio left as it was, when any of that fails.
bool     linkRadioStart(uint8_t slots);
void     linkRadioStop();
bool     linkRadioUp();

// linkRadioSend: one frame, to mac (null: broadcast). A unicast to a MAC not
// in the ESP-NOW table adds it first. False when ESP-NOW refused it.
bool     linkRadioSend(const uint8_t* mac, const uint8_t* f, size_t n);
// linkRadioIdle: the last frame's send callback has come back.
bool     linkRadioIdle();
// linkRadioRecv: the oldest received frame, or 0.
size_t   linkRadioRecv(uint8_t* out, size_t cap, uint8_t mac[6], int8_t& rssi);

bool     linkRadioAddPeer(const uint8_t mac[6]);
void     linkRadioDelPeer(const uint8_t mac[6]);

// linkRadioChannel: the channel the radio is on (the router's, on a board).
uint8_t  linkRadioChannel();
// linkRadioMac: this board's station MAC, which is what a peer sees.
void     linkRadioMac(uint8_t mac[6]);

// Measures for LINK and SYS.
uint32_t linkRadioRingDrops();     // frames lost because the ring was full
uint8_t  linkRadioRingHigh();      // the most frames the ring has held
uint32_t linkRadioSendFails();     // send callbacks that said FAIL
uint32_t linkRadioChannelMoves();  // home channel changes seen (the router hopped)

// linkAlloc / linkFree: the link's buffers, from PSRAM on a board that has
// it and the internal heap otherwise. Only at start and stop, never per frame.
void*    linkAlloc(size_t n);
void     linkFree(void* p);

}  // namespace plat
