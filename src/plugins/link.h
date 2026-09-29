// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/plugins/link.h
// Module:       Plugins / the µnleashed link, for other plugins (1.2.0)
//
// Purpose:      How a plugin speaks over the link: it registers a message
//               family (LINK.md, "What it carries"), and the link plugin
//               hands it every message of that family and lends it the
//               engine to answer with. The doors plugin is family 2; the
//               camera satellite plugin (unleashed_camsat) is family 1.
//
//               Registering does not depend on the link running or on which
//               plugin starts first: the table is the link plugin's, and a
//               family registered while the link is off simply hears nothing
//               until it is switched on.
//
//               Every handler is called on the BBS loop, except bulkData,
//               which the background runner calls (a picture being written
//               to the card). A handler must never block.
//
// Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1) and the Linux host build
// See also:     LINK.md, src/core/link.h, PLUGINS.md
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

#include "../core/link.h"

class Bbs;
struct Session;

namespace linkp {

// A message family's handlers. Any may be null.
struct Family {
    uint8_t     id = 0xFF;                // LINK.md's table: 1 CAMERA, 2 DOOR, 128-239 plugins
    const char* name = "";
    // A single-frame message. False: no room now (the sender is told to wait).
    bool (*message)(uint8_t peer, uint16_t sess, uint8_t type, const uint8_t* p, size_t n) = nullptr;
    // A bulk message begins; total is its size. False refuses it.
    bool (*bulkBegin)(uint8_t peer, uint16_t sess, uint8_t type, uint32_t total) = nullptr;
    // Its bytes, in order. ON THE RUNNER, not the loop. False aborts it.
    bool (*bulkData)(uint8_t peer, uint16_t sess, const uint8_t* p, size_t n) = nullptr;
    // It is complete (ok) or abandoned. On the loop.
    void (*bulkEnd)(uint8_t peer, uint16_t sess, bool ok) = nullptr;
    // A bulk message this end sent was taken (ok) or given up.
    void (*bulkSent)(uint8_t peer, uint16_t sess, bool ok) = nullptr;
    // A session of this family was ended by the far end or by retries.
    void (*reset)(uint8_t peer, uint16_t sess, uint8_t reason) = nullptr;
    // A peer came up or went down (every family hears every peer).
    void (*peerState)(uint8_t peer, bool up) = nullptr;
    // The bulk message's last bytes are in: ON THE RUNNER, just before
    // bulkEnd, for the sink to finish its card work (photos::file) off the
    // loop. ok is false when the message CRC-32 did not check. Appended.
    void (*bulkFinish)(uint8_t peer, uint16_t sess, bool ok) = nullptr;
    // What this board wants from satellite peer changed (CONFIG sats: what
    // it takes, its camera number). On the loop. Appended (1.2.0).
    void (*settingsChanged)(uint8_t peer) = nullptr;
};

// registerFamily: false when the id is taken by another family (logged, by
// name) or the table is full. unregisterFamily in the plugin's stop().
bool registerFamily(const Family& f);
void unregisterFamily(uint8_t id);

// engine: the running link, or null when it is off. Borrowed for the length
// of a call: never keep the pointer across a tick, since a CONFIG save can
// stop and start the link.
ulink::Engine* engine();

// peerName: what the sysop calls peer i ("garden"), "" when unused.
const char* peerName(uint8_t peer);

// peerOfKind: the n-th live pairing of this kind, -1 when there is none.
int peerOfKind(uint8_t kind, uint8_t n = 0);

// --- One satellite, several boards; SATS; CONFIG sats (1.2.0) -------------
// peerRecv: what this board takes from satellite peer (linkfam RECV_*).
uint8_t peerRecv(uint8_t peer);
// peerCamNo: the camera number set for it here, 0 auto.
uint8_t peerCamNo(uint8_t peer);
// channel: the radio's channel, 0 with the link off.
uint8_t channel();
// onWire: the board is on Ethernet (BBS_HAS_ETH, an address on the wire), so
// Wi-Fi stands by unjoined and ESP-NOW cannot reach a sat (1.2.1 fixes it).
// Always false on a board with no wired port.
bool onWire();

// SatInfo: everything the board knows of one pairing, for SATS and CONFIG
// sats. The key never leaves the plugin: fp is its fingerprint, the first 8
// bytes of SHA-256 of this board's k_link with it, as "3F9A 12C0 77E1 0B42".
struct SatInfo {
    bool     up = false;
    char     name[17] = {};
    uint8_t  kind = 0;
    ulink::Mac mac;
    int      rssi = 0, farRssi = 0;
    uint32_t lastHeard = 0;          // millis, 0 never
    uint32_t rx = 0, tx = 0, retries = 0, drops = 0;
    bool     slow = false;           // at 1 Mbps after failed sends
    uint8_t  chan = 0;               // the channel it was last up on
    uint8_t  recv = 0, camno = 0;
    bool     checked = false;
    uint8_t  boards = 0;             // boards sharing it, from its PEERS; 0 unknown
    int8_t   owned = -1;             // 1 this board owns it, 0 another does, -1 unknown
    char     owner[17] = {};         // the owner's name, when another
    ulink::SharedBoard others[ulink::Engine::kHosts];
    uint8_t  nothers = 0;            // the other boards, not this one
    char     fp[20] = {};
};
// satInfo: false when the link is off or the pairing is unused.
bool satInfo(uint8_t peer, SatInfo& out);
// ask: CONFIG sats' Share and Unpair, asked on the sysop's own screen
// ("Unpair garden? (y/N)"); Y runs LINK SHARE or LINK FORGET. The session is
// the link plugin's until the key. False when it cannot ask.
enum : uint8_t { ASK_SHARE = 1, ASK_UNPAIR = 2 };
bool ask(Bbs& b, Session& s, uint8_t peer, uint8_t what);
// setSat: CONFIG sats' save for one pairing: its name (one word, unique), its
// camera number (0 auto, 2 to 9) and what this board takes. The satellite is
// told what this board takes at once. False with why (a sentence for the
// form's status line).
bool setSat(uint8_t peer, const char* name, uint8_t camno, uint8_t recv, char* why, size_t n);

}  // namespace linkp

// For SYS and HARDWARE (core/bbs_hardware.cpp): the link's one row, false
// when the link is off. val is its state and channel, note the peers.
bool linkHwRow(char* val, size_t valN, char* note, size_t noteN, bool& warn);
