/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/improv.h
 * Module:       Core / Improv Wi-Fi Serial, the packets
 *
 * Purpose:      Improv Wi-Fi Serial (https://www.improv-wifi.com/serial/):
 *               how a browser page that has just flashed a board over USB
 *               tells it which network to join, over the same cable. This
 *               file is only the packets, with no UART, radio or board in
 *               it, so it is tested on its own like codes and compose.
 *
 *                 IMPROV  ver  type  len  data...  checksum
 *                 6 bytes  1    1     1    len      1
 *
 *               The checksum is the sum of every byte before it, kept to 8
 *               bits (confirmed against the reference implementation, since
 *               the spec page does not say). An RPC's data is its command,
 *               a length, and the command's own bytes; a result's data is
 *               the command it answers, a length, and length-prefixed
 *               strings.
 *
 * Design:       Written here rather than taken from the official C++ SDK.
 *               That SDK is Apache-2.0, which the FSF lists as incompatible
 *               with GPLv2, and this firmware is GPLv2 or later; bringing it
 *               in would make the combined work GPLv3 in practice. The
 *               protocol is small enough that our own copy costs less than
 *               that decision.
 *
 * Interfaces:   improv::Parser, frame, stateFrame, errorFrame, resultFrame,
 *               parseRpc, parseWifi
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     src/main.cpp (the UART and the radio)
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
#include <cstddef>
#include <cstdint>

namespace improv {

constexpr uint8_t kVersion = 1;

// Packet types
enum : uint8_t { T_STATE = 0x01, T_ERROR = 0x02, T_RPC = 0x03, T_RESULT = 0x04 };

// States, sent as a T_STATE packet
enum : uint8_t { S_STOPPED = 0x00, S_AUTHORIZED = 0x02, S_PROVISIONING = 0x03,
                 S_PROVISIONED = 0x04 };

// Errors, sent as a T_ERROR packet
enum : uint8_t { E_NONE = 0x00, E_INVALID = 0x01, E_UNKNOWN_RPC = 0x02,
                 E_CONNECT = 0x03, E_UNKNOWN = 0xFF };

// RPC commands this board answers. 0x05 to 0x07 (hostname, device name,
// network state) are optional in the spec and answered E_UNKNOWN_RPC.
enum : uint8_t { C_WIFI = 0x01, C_STATE = 0x02, C_INFO = 0x03, C_SCAN = 0x04 };

// The longest data a packet may carry here. Wi-Fi settings are the biggest
// thing a client sends: 32 bytes of SSID and 64 of password, two lengths
// and the RPC's own two bytes. Anything longer is not a packet this board
// has a use for and is dropped rather than buffered.
constexpr uint8_t kDataMax = 128;

// ---------------------------------------------------------------------------
// Parser: bytes in, whole packets out. The serial line also carries the
// board's log, so anything that is not a packet is skipped over: a parser
// that gave up on the first stray byte would never see one.
// ---------------------------------------------------------------------------
class Parser {
public:
    enum class Res : uint8_t { None, Packet, BadChecksum };

    Res feed(uint8_t b);
    uint8_t        type() const { return buf_[7]; }
    uint8_t        len()  const { return buf_[8]; }
    const uint8_t* data() const { return buf_ + 9; }
    void           reset() { pos_ = 0; }

private:
    uint8_t  buf_[9 + kDataMax + 1] = {};
    uint16_t pos_ = 0;
};

// frame: a whole packet into out, with its checksum and a newline after it
// (the log shares this line, and a packet left without one runs into the
// next log line on the screen of anybody watching). 0 if it does not fit.
size_t frame(uint8_t* out, size_t cap, uint8_t type, const uint8_t* data, uint8_t len);
size_t stateFrame(uint8_t* out, size_t cap, uint8_t state);
size_t errorFrame(uint8_t* out, size_t cap, uint8_t err);
// resultFrame: the answer to an RPC, as length-prefixed strings. n may be 0,
// which is how the end of a network scan is said.
size_t resultFrame(uint8_t* out, size_t cap, uint8_t cmd, const char* const* strs, uint8_t n);

// parseRpc: an RPC packet's command and its own bytes. False when the length
// the RPC claims runs past the packet.
bool parseRpc(const uint8_t* data, uint8_t len, uint8_t& cmd,
              const uint8_t*& payload, uint8_t& plen);

// parseWifi: the settings command's bytes, as two terminated strings. False
// when the lengths do not fit the payload or the buffers, or the SSID is
// empty.
bool parseWifi(const uint8_t* payload, uint8_t plen, char* ssid, size_t ssidCap,
               char* pass, size_t passCap);

} // namespace improv
