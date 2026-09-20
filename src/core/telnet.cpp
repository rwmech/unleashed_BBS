/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/telnet.cpp
 * Module:       Core / telnet filter
 *
 * Purpose:      Telnet filter (see telnet.h).
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     README.md
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

#include "telnet.h"

namespace {
constexpr uint8_t T_SE   = 240;
constexpr uint8_t T_SB   = 250;
constexpr uint8_t T_WILL = 251;
constexpr uint8_t T_WONT = 252;
constexpr uint8_t T_DO   = 253;
constexpr uint8_t T_DONT = 254;
constexpr uint8_t T_IAC  = 255;

constexpr uint8_t O_ECHO = 1;
constexpr uint8_t O_SGA  = 3;
constexpr uint8_t O_NAWS = 31;

inline uint32_t bit(uint8_t opt) { return opt < 32 ? (1UL << opt) : 0; }
} // namespace

// ---------------------------------------------------------------------------
// reset: fresh state for a new connection
// ---------------------------------------------------------------------------
void Telnet::reset() {
    enabled_ = true;
    seenIac_ = false;
    lastCR_  = false;
    st_      = S_DATA;
    cmd_     = 0;
    sbLen_   = 0;
    us_      = 0;
    him_     = 0;
    cols_    = 0;
    rows_    = 0;
}

void Telnet::send3(ByteSink& o, uint8_t cmd, uint8_t opt) {
    const uint8_t seq[3] = { T_IAC, cmd, opt };
    o.put(seq, 3);
}

// ---------------------------------------------------------------------------
// negotiate: echo and SGA from us (character mode), window size from them
// ---------------------------------------------------------------------------
void Telnet::negotiate(ByteSink& reply) {
    enabled_ = true;
    if (!(us_ & bit(O_ECHO))) { us_ |= bit(O_ECHO); send3(reply, T_WILL, O_ECHO); }
    if (!(us_ & bit(O_SGA)))  { us_ |= bit(O_SGA);  send3(reply, T_WILL, O_SGA);  }
    if (!(him_ & bit(O_NAWS))) { him_ |= bit(O_NAWS); send3(reply, T_DO, O_NAWS); }
}

// ---------------------------------------------------------------------------
// onOption: answer one WILL/WONT/DO/DONT without creating loops
// ---------------------------------------------------------------------------
void Telnet::onOption(uint8_t cmd, uint8_t opt, ByteSink& reply) {
    bool weDo    = (opt == O_ECHO || opt == O_SGA);   // options we perform
    bool heMayDo = (opt == O_NAWS || opt == O_SGA);   // options we accept

    switch (cmd) {
        case T_WILL:
            if (heMayDo) {
                if (!(him_ & bit(opt))) { him_ |= bit(opt); send3(reply, T_DO, opt); }
            } else {
                send3(reply, T_DONT, opt);
            }
            break;
        case T_WONT:
            if (him_ & bit(opt)) { him_ &= ~bit(opt); send3(reply, T_DONT, opt); }
            break;
        case T_DO:
            if (weDo) {
                if (!(us_ & bit(opt))) { us_ |= bit(opt); send3(reply, T_WILL, opt); }
            } else {
                send3(reply, T_WONT, opt);
            }
            break;
        case T_DONT:
            if (us_ & bit(opt)) { us_ &= ~bit(opt); send3(reply, T_WONT, opt); }
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// onSubneg: IAC SB <opt> ... IAC SE
// ---------------------------------------------------------------------------
void Telnet::onSubneg() {
    if (sbLen_ >= 5 && sb_[0] == O_NAWS) {
        uint16_t w = static_cast<uint16_t>((sb_[1] << 8) | sb_[2]);
        uint16_t h = static_cast<uint16_t>((sb_[3] << 8) | sb_[4]);
        cols_ = static_cast<uint8_t>(w > 255 ? 255 : w);
        rows_ = static_cast<uint8_t>(h > 255 ? 255 : h);
    }
}

// ---------------------------------------------------------------------------
// filter: strip protocol, keep data
// ---------------------------------------------------------------------------
size_t Telnet::filter(const uint8_t* in, size_t n, uint8_t* out, ByteSink& reply) {
    size_t o = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t b = in[i];

        if (!enabled_ && st_ == S_DATA) {
            if (!binary_ && lastCR_ && (b == 0x0A || b == 0x00)) { lastCR_ = false; continue; }
            lastCR_ = (b == 0x0D);
            out[o++] = b;
            continue;
        }

        switch (st_) {
            case S_DATA:
                if (b == T_IAC) { st_ = S_IAC; seenIac_ = true; break; }
                // In binary mode this pair is data, not a line ending.
                if (!binary_ && lastCR_ && (b == 0x0A || b == 0x00)) { lastCR_ = false; break; }
                lastCR_ = (b == 0x0D);
                out[o++] = b;
                break;
            case S_IAC:
                if (b == T_IAC) { out[o++] = 0xFF; st_ = S_DATA; lastCR_ = false; break; }
                if (b >= T_WILL && b <= T_DONT) { cmd_ = b; st_ = S_OPT; break; }
                if (b == T_SB) { sbLen_ = 0; st_ = S_SB; break; }
                st_ = S_DATA;               // NOP, GA, AYT, etc.
                break;
            case S_OPT:
                onOption(cmd_, b, reply);
                st_ = S_DATA;
                break;
            case S_SB:
                if (b == T_IAC) { st_ = S_SB_IAC; break; }
                if (sbLen_ < sizeof(sb_)) sb_[sbLen_++] = b;
                break;
            case S_SB_IAC:
                if (b == T_SE) { onSubneg(); st_ = S_DATA; break; }
                if (b == T_IAC && sbLen_ < sizeof(sb_)) sb_[sbLen_++] = b;
                st_ = S_SB;
                break;
            default:
                st_ = S_DATA;
                break;
        }
    }
    return o;
}
