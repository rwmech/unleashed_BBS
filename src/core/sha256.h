/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/sha256.h
 * Module:       Core / hashing
 *
 * Purpose:      SHA-256 (FIPS 180-4), portable, no heap. Used for the user
 *                  password hash (salted, repeated): simple on purpose, it
 *                  keeps plaintext out of users.txt without stalling the loop.
 *
 * Interfaces:   Sha256, Sha256::digest
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

class Sha256 {
public:
    Sha256() { reset(); }
    void reset();
    void update(const uint8_t* data, size_t len);
    void finish(uint8_t out[32]);

    static void digest(const uint8_t* data, size_t len, uint8_t out[32]);

private:
    void block(const uint8_t* p);

    uint32_t h_[8];
    uint8_t  buf_[64];
    uint64_t total_;
    uint8_t  fill_;
};
