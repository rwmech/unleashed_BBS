/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/telnet.h
 * Module:       Core / telnet filter
 *
 * Purpose:      Minimal telnet (RFC 854) filter. Strips and answers option
 *                  negotiation, reads window size (NAWS), collapses CR LF and
 *                  CR NUL into a single CR. Disabled for PETSCII callers who
 *                  never spoke telnet, so the C64 pi key ($FF) passes through
 *                  instead of being eaten as IAC.
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

#pragma once
#include <cstdint>
#include <cstddef>
#include "term.h"

class Telnet {
public:
    void reset();

    void setEnabled(bool on) { enabled_ = on; }
    bool enabled()     const { return enabled_; }

    // Binary mode: telnet commands are still understood, CR is not touched.
    //
    // The input filter normally drops a 0x0A or a 0x00 that follows a 0x0D,
    // because telnet says a bare CR travels as CR NUL and a line ending as
    // CR LF, and a terminal wants one newline rather than two. In a file
    // transfer those are data, and deleting them corrupts the file silently:
    // it is the same class of bug as not escaping IAC, with a different byte
    // pair, and a test that only checks 0xFF will not catch it.
    //
    // Not the same as setEnabled(false), which also stops IAC being
    // unescaped, so every 0xFF in the file would arrive doubled instead.
    // setBinary: RFC 856 TRANSMIT-BINARY, in both directions, for the
    // duration of a file transfer.
    //
    // This used to set a local flag and nothing else, and that was a real
    // bug rather than an omission. Without the negotiation the link is
    // still NVT ASCII, where a sender transmitting a bare CR must follow it
    // with LF or NUL. A terminal sending XMODEM obeys that, so every block
    // containing a 0x0D arrived with an extra byte in it, the check failed,
    // and the board NAKed every block of every upload. SyncTERM showed it
    // as "Received NAK Expected ACK" until it gave up.
    //
    // The local flag alone made it worse, not better: it stopped the board
    // deleting the byte after a CR, which is right for real binary and
    // exactly wrong while the far end is still inserting one.
    //
    // reply carries the option bytes, so this needs the session's sink.
    void setBinary(ByteSink& reply, bool on);
    bool binary()      const { return binary_; }

    bool clientSpoke() const { return seenIac_; }   // client sent IAC first

    // filter: raw socket bytes in, clean data bytes out (out >= n bytes).
    // Negotiation replies are written to reply. Returns data byte count.
    size_t filter(const uint8_t* in, size_t n, uint8_t* out, ByteSink& reply);

    // negotiate: server-side opening for character-at-a-time ANSI sessions
    void negotiate(ByteSink& reply);

    bool    hasSize() const { return cols_ != 0 && rows_ != 0; }
    uint8_t cols()    const { return cols_; }
    uint8_t rows()    const { return rows_; }

private:
    enum : uint8_t { S_DATA, S_IAC, S_OPT, S_SB, S_SB_IAC };

    void send3(ByteSink& o, uint8_t cmd, uint8_t opt);
    void onOption(uint8_t cmd, uint8_t opt, ByteSink& reply);
    void onSubneg();

    bool     enabled_ = true;
    bool     binary_  = false;   // a transfer is running: leave CR alone
    bool     seenIac_ = false;
    bool     lastCR_  = false;
    uint8_t  st_      = S_DATA;
    uint8_t  cmd_     = 0;
    uint8_t  sb_[8]   = {};
    uint8_t  sbLen_   = 0;
    bool     wantBin_ = false;  // a transfer asked for binary
    uint32_t us_      = 0;   // options we have agreed to (WILL)
    uint32_t him_     = 0;   // options we asked the client for (DO)
    uint8_t  cols_    = 0;
    uint8_t  rows_    = 0;
};
