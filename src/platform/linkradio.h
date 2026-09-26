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
//               Receiving: the Wi-Fi task's callback copies each frame into
//               one of two rings and returns. Fragments of a bulk message
//               (nfrag > 1, read from the header, unauthenticated, so only a
//               routing decision) go to the bulk ring, which the background
//               runner empties; everything else to the control ring, which
//               the loop empties. On the bench a loop taking eight frames a
//               pass lost half a picture at 24 Mbps (2026-09-26).
//
//               Sending: up to four frames outstanding at the MAC (+53% at
//               24 Mbps on the bench). Each peer is sent at 802.11g 24 Mbps,
//               down to 1 Mbps after three MAC failures in a row to it, and
//               back to 24 Mbps after 30 s without one (bench: 6-10x the
//               rate of 1 Mbps, and less airtime taken from the callers'
//               Wi-Fi, gateway pings 1-4 ms against 7-21 ms).
//
//               Peers are added UNENCRYPTED at the ESP-NOW layer: the link
//               seals every frame itself (LINK.md).
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
// the broadcast address in the peer table, a control ring of ctrlSlots and a
// bulk ring of bulkSlots frames (each rounded up to a power of two). False,
// and the radio left as it was, when any of that fails.
bool     linkRadioStart(uint8_t ctrlSlots, uint8_t bulkSlots);
void     linkRadioStop();
bool     linkRadioUp();

// linkRadioSend: one frame, to mac (null: broadcast). A unicast to a MAC not
// in the ESP-NOW table adds it first, at 24 Mbps. False when ESP-NOW refused it.
bool     linkRadioSend(const uint8_t* mac, const uint8_t* f, size_t n);
// linkRadioIdle: another frame may go (fewer than four outstanding).
bool     linkRadioIdle();
// linkRadioWait: sleep in 1 ms steps, at least one, until nothing is
// outstanding or ms have passed. For stopping only (the last CLOSE frames),
// never on the loop's ordinary path.
void     linkRadioWait(uint32_t ms);
// linkRadioRecv: the oldest control frame, or 0. The loop's.
size_t   linkRadioRecv(uint8_t* out, size_t cap, uint8_t mac[6], int8_t& rssi);
// linkRadioRecvBulk: the oldest bulk fragment, or 0. The runner's.
size_t   linkRadioRecvBulk(uint8_t* out, size_t cap, uint8_t mac[6], int8_t& rssi);
// linkRadioBulkWaiting: the bulk ring holds something (for posting the job).
bool     linkRadioBulkWaiting();

bool     linkRadioAddPeer(const uint8_t mac[6]);
void     linkRadioDelPeer(const uint8_t mac[6]);

// linkRadioChannel: the channel the radio is on (the router's, on a board).
uint8_t  linkRadioChannel();
// linkRadioAssociated: the station is joined to its router.
bool     linkRadioAssociated();
// linkRadioMac: this board's station MAC, which is what a peer sees.
void     linkRadioMac(uint8_t mac[6]);

// linkLock / linkUnlock: a recursive lock for the engine, which the loop and
// the runner both call into.
void     linkLock();
void     linkUnlock();

// Measures for LINK and SYS.
uint32_t linkRadioRingDrops();     // frames lost because a ring was full
uint8_t  linkRadioRingHigh();      // the most frames the control ring has held
uint8_t  linkRadioBulkHigh();      // the most the bulk ring has held
uint32_t linkRadioSendFails();     // send callbacks that said FAIL
uint32_t linkRadioChannelMoves();  // home channel changes seen (the router hopped)
uint8_t  linkRadioSlowPeers();     // peers sent at 1 Mbps just now (fallen back)

// linkAlloc / linkFree: the link's buffers, from PSRAM on a board that has
// it and the internal heap otherwise. Only at start and stop, never per frame.
void*    linkAlloc(size_t n);
void     linkFree(void* p);

}  // namespace plat
