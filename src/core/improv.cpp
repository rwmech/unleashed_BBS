/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/improv.cpp
 * Module:       Core / Improv Wi-Fi Serial, the packets
 *
 * Purpose:      Improv packets (see improv.h).
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     improv.h
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

#include "improv.h"
#include <cstring>

namespace improv {
namespace {
const char kHeader[] = "IMPROV";
}

// ---------------------------------------------------------------------------
// feed: one byte. A byte that cannot continue the packet so far starts the
// search again, and gets a second look as a possible first byte, so
// "IMPIMPROV..." still finds the packet.
// ---------------------------------------------------------------------------
Parser::Res Parser::feed(uint8_t b) {
    for (int pass = 0; pass < 2; ++pass) {
        if (pos_ < 6) {
            if (b == static_cast<uint8_t>(kHeader[pos_])) { buf_[pos_++] = b; return Res::None; }
            if (pos_ == 0) return Res::None;
            pos_ = 0;
            continue;                                   // look at b again as a start
        }
        if (pos_ == 6) {                                // version
            if (b != kVersion) { pos_ = 0; continue; }
            buf_[pos_++] = b;
            return Res::None;
        }
        if (pos_ == 7) { buf_[pos_++] = b; return Res::None; }   // type
        if (pos_ == 8) {                                // length
            if (b > kDataMax) { pos_ = 0; return Res::None; }
            buf_[pos_++] = b;
            return Res::None;
        }
        uint16_t end = static_cast<uint16_t>(9 + buf_[8]);
        if (pos_ < end) { buf_[pos_++] = b; return Res::None; }
        // b is the checksum
        uint8_t sum = 0;
        for (uint16_t i = 0; i < end; ++i) sum = static_cast<uint8_t>(sum + buf_[i]);
        pos_ = 0;
        return sum == b ? Res::Packet : Res::BadChecksum;
    }
    return Res::None;
}

size_t frame(uint8_t* out, size_t cap, uint8_t type, const uint8_t* data, uint8_t len) {
    size_t need = 6 + 1 + 1 + 1 + static_cast<size_t>(len) + 1 + 1;
    if (!out || cap < need) return 0;
    memcpy(out, kHeader, 6);
    out[6] = kVersion;
    out[7] = type;
    out[8] = len;
    if (len) memcpy(out + 9, data, len);
    uint8_t sum = 0;
    for (size_t i = 0; i < 9u + len; ++i) sum = static_cast<uint8_t>(sum + out[i]);
    out[9 + len]  = sum;
    out[10 + len] = '\n';
    return need;
}

size_t stateFrame(uint8_t* out, size_t cap, uint8_t state) {
    return frame(out, cap, T_STATE, &state, 1);
}

size_t errorFrame(uint8_t* out, size_t cap, uint8_t err) {
    return frame(out, cap, T_ERROR, &err, 1);
}

size_t resultFrame(uint8_t* out, size_t cap, uint8_t cmd, const char* const* strs, uint8_t n) {
    uint8_t data[kDataMax + 2];
    size_t  pos = 2;                                    // command and length first
    for (uint8_t i = 0; i < n; ++i) {
        size_t l = strs[i] ? strlen(strs[i]) : 0;
        if (l > 255 || pos + 1 + l > sizeof(data)) return 0;   // will not fit a packet
        data[pos++] = static_cast<uint8_t>(l);
        if (l) memcpy(data + pos, strs[i], l);
        pos += l;
    }
    data[0] = cmd;
    data[1] = static_cast<uint8_t>(pos - 2);
    if (pos > kDataMax) return 0;
    return frame(out, cap, T_RESULT, data, static_cast<uint8_t>(pos));
}

bool parseRpc(const uint8_t* data, uint8_t len, uint8_t& cmd,
              const uint8_t*& payload, uint8_t& plen) {
    if (len < 2) return false;
    cmd  = data[0];
    plen = data[1];
    if (static_cast<uint16_t>(2 + plen) > len) return false;
    payload = data + 2;
    return true;
}

bool parseWifi(const uint8_t* payload, uint8_t plen, char* ssid, size_t ssidCap,
               char* pass, size_t passCap) {
    if (plen < 1) return false;
    uint8_t sl = payload[0];
    if (!sl || 1u + sl + 1u > plen || sl + 1u > ssidCap) return false;
    uint8_t pl = payload[1 + sl];
    if (2u + sl + pl > plen || pl + 1u > passCap) return false;
    memcpy(ssid, payload + 1, sl);
    ssid[sl] = '\0';
    memcpy(pass, payload + 2 + sl, pl);
    pass[pl] = '\0';
    return true;
}

} // namespace improv
