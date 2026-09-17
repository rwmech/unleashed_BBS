/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/platform/platform.h
 * Module:       Platform layer
 *
 * Purpose:      Thin platform layer. The core only touches the hardware
 *                  through these calls, so the same core compiles for the
 *                  ESP32 (platform_esp32.cpp) and for a Linux host test build
 *                  (host/platform_host.cpp). Sockets use the BSD API, which
 *                  lwIP provides on the ESP32.
 *
 * Interfaces:   millis, random32, fsBase, logsBase, heap, wifiRssi, log,
 *               backupButton*, activityLed*, inflateRaw
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

namespace plat {

struct HeapStats {
    uint32_t freeBytes;     // current free 8-bit heap
    uint32_t minFree;       // lowest free heap since boot
    uint32_t largestBlock;  // largest contiguous free block
    bool     valid;         // false on hosts that cannot report
};

// ---------------------------------------------------------------------------
// millis: monotonic milliseconds since boot (wraps ~49 days, callers use
// unsigned subtraction so wrap is harmless)
// ---------------------------------------------------------------------------
uint32_t millis();

// ---------------------------------------------------------------------------
// random32: hardware RNG on ESP32, libc rand on host
// ---------------------------------------------------------------------------
uint32_t random32();

// ---------------------------------------------------------------------------
// fsBase: mount point of the data filesystem (screens, system.cfg), no
// trailing slash. logsBase: mount point of the separate logs filesystem.
// ---------------------------------------------------------------------------
const char* fsBase();
const char* logsBase();

// ---------------------------------------------------------------------------
// heap: heap statistics for MEM command and per-session measurement
// ---------------------------------------------------------------------------
HeapStats heap();

// ---------------------------------------------------------------------------
// wifiRssi: signal strength of the joined access point in dBm, 0 when not
// connected or not available (host)
// ---------------------------------------------------------------------------
int8_t wifiRssi();

// ---------------------------------------------------------------------------
// fsInfo: size and used bytes of the data filesystem. False when the
// platform cannot tell.
// ---------------------------------------------------------------------------
bool fsInfo(uint32_t& total, uint32_t& used);

// ---------------------------------------------------------------------------
// Device serial port (the serial bridge plugin). This is the second UART,
// never the console: flashing and the monitor keep working while a caller
// is driving the device. On the host build it is a loopback (or a real
// device when BBS_SERIAL_DEV names one), so the bridge can be tested off
// the board.
//
// serialOpen returns false when the pins are unusable or the port is busy.
// serialRead never blocks and returns the bytes it had.
// ---------------------------------------------------------------------------
bool   serialOpen(int rxPin, int txPin, uint32_t baud, uint8_t bits, char parity, uint8_t stop);
void   serialClose();
bool   serialIsOpen();
bool   serialSetLine(uint32_t baud, uint8_t bits, char parity, uint8_t stop);
size_t serialRead(uint8_t* buf, size_t cap);
size_t serialWrite(const uint8_t* data, size_t n);
uint32_t serialFramingErrors();      // wrong speed shows up here (autoprobe)

// ---------------------------------------------------------------------------
// log: printf-style line to the console (UART on ESP32, stdout on host)
// ---------------------------------------------------------------------------
void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// ---------------------------------------------------------------------------
// backupButtonBegin / backupButtonPressed: the physical button that opens
// the backup window. Pressed() returns true once per debounced press.
// A test build (BBS_BACKUP_TEST_OPEN) reports a press on every call.
// ---------------------------------------------------------------------------
void backupButtonBegin(int gpio);
bool backupButtonPressed(uint32_t now);

// ---------------------------------------------------------------------------
// activityLedBegin / activityPulse / activityTick: blink an LED on network
// traffic. Pulse turns it on, tick turns it off BBS_LED_PULSE_MS later.
// gpio -1 disables it.
// ---------------------------------------------------------------------------
void activityLedBegin(int gpio);
void activityPulse(uint32_t now);
void activityTick(uint32_t now);

// ---------------------------------------------------------------------------
// inflateRaw: decode a raw DEFLATE stream (ZIP method 8). in() fills a
// buffer and returns the byte count (0 = end of input); out() takes
// decoded bytes and returns false to abort (size cap). True on a clean end.
// Uses temporary heap (about 43 KB on ESP32), released before returning.
// ---------------------------------------------------------------------------
using InflateIn  = size_t (*)(void* ctx, uint8_t* buf, size_t cap);
using InflateOut = bool   (*)(void* ctx, const uint8_t* data, size_t n);
bool inflateRaw(InflateIn in, InflateOut out, void* ctx);

} // namespace plat
