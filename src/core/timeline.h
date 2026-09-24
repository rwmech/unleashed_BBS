/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/timeline.h
 * Module:       Core / output scheduling
 *
 * Purpose:      Per-session timed output. Bytes are grouped into frames;
 *                  each frame can carry a delay that holds the next frame
 *                  back. This is what makes spinners, typewriter text and
 *                  baud emulation non-blocking: effects queue frames, the
 *                  scheduler drains them as time passes.
 *
 * Interfaces:   Timeline (put, delay, pump, freeBytes, freeFrames, skipDelays)
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     README.md
 *
 * Copyright 2026 - Robert Mech
 * License:      GNU General Public License v3 or later
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>. The full
 * text is in the LICENSE file at the top of this repository.
 * ===========================================================================
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include "../config.h"
#include "term.h"

// send callback: returns bytes accepted, 0 if the socket would block,
// negative on a dead connection
using SendFn = int (*)(void* ctx, const uint8_t* data, size_t len);

class Timeline : public ByteSink {
public:
    // put: append bytes to the open frame. All-or-nothing, false if full.
    bool put(const uint8_t* data, size_t len) override;

    // delay: close the open frame with a hold of ms before the next one
    void delay(uint16_t ms);

    // clear: drop everything pending
    void clear();

    // skipDelays: fast-forward (a keypress during an effect)
    void skipDelays();

    // setCps: emulate a line speed in characters per second (0 = off)
    void setCps(uint16_t cps) { cps_ = cps; budget_ = 0; }
    uint16_t cps() const { return cps_; }

    size_t freeBytes()  const { return BBS_TL_BYTES - bCount_; }
    size_t freeFrames() const { return BBS_TL_FRAMES - fCount_; }
    bool   empty()      const { return fCount_ == 0; }

    // pump: drain what is due. Returns bytes sent, or -1 on dead socket.
    int pump(uint32_t now, SendFn send, void* ctx);

private:
    struct Frame { uint16_t len; uint16_t delay; };

    bool openFrame();

    uint8_t  buf_[BBS_TL_BYTES];
    uint16_t bHead_  = 0;
    uint16_t bCount_ = 0;

    Frame    fr_[BBS_TL_FRAMES];
    uint8_t  fHead_  = 0;
    uint8_t  fCount_ = 0;

    bool     waiting_   = false;
    uint32_t waitUntil_ = 0;

    uint16_t cps_      = 0;
    uint32_t budget_   = 0;   // milli-characters
    uint32_t lastPump_ = 0;
};
