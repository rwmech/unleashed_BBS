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
    uint32_t totalBytes;    // the whole 8-bit heap, free or not
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
// micros: monotonic microseconds since boot, for loop timing. Wraps every
// 71 minutes; callers use unsigned subtraction, so a wrap is harmless.
// ---------------------------------------------------------------------------
uint32_t micros();

// ---------------------------------------------------------------------------
// fsBase: mount point of the data filesystem (screens, system.cfg), no
// trailing slash. logsBase: mount point of the separate logs filesystem.
// ---------------------------------------------------------------------------
const char* fsBase();
const char* logsBase();

// ---------------------------------------------------------------------------
// userBase: mount point of the partition holding what the callers own:
// accounts, the live configuration, and each plugin's files. Separate from
// fsBase on purpose, because a filesystem upload replaces everything under
// fsBase and must never take the accounts with it.
// ---------------------------------------------------------------------------
const char* userBase();

// userInfo: size and used bytes of that partition, for the space a plugin
// is allowed to claim. False when the platform cannot tell.
bool userInfo(uint32_t& total, uint32_t& used);

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
// netInfo: what the radio is attached to, for the sysop's system screen.
// valid is false on the host build and before the join completes.
// ---------------------------------------------------------------------------
struct NetInfo {
    char    ssid[33] = {};
    char    ip[16]   = {};
    uint8_t channel  = 0;
    int8_t  rssi     = 0;
    bool    valid    = false;
};
NetInfo netInfo();

// ---------------------------------------------------------------------------
// fsInfo: size and used bytes of the data filesystem. False when the
// platform cannot tell.
// ---------------------------------------------------------------------------
bool fsInfo(uint32_t& total, uint32_t& used);

// ---------------------------------------------------------------------------
// The SD card (the sd plugin). Optional: a board with no card is a complete
// board, and everything that must survive stays on internal flash whatever
// is plugged in.
//
// Mounting blocks. An SPI card negotiation is a few hundred milliseconds of
// synchronous work, and there is no way to do it from the BBS loop without
// every caller feeling it. So it happens at plugin start, before there are
// any callers, or when a sysop asks for it and knows what they asked for.
// Nothing polls for a card in the background, which is also why there is no
// insertion event: there is no card-detect line on the wiring this supports,
// and probing the bus to find out would be the same stall on a timer.
//
// Sizes are in kilobytes rather than bytes so a 32 GB card still fits a
// uint32_t. That avoids dragging 64-bit printf support into the image for
// the sake of one status line.
// ---------------------------------------------------------------------------
struct SdPins {
    int8_t   cs   = 5;   // the board Rob wired: CS D5, MOSI D23, CLK D18, MISO D19
    int8_t   mosi = 23;  // GPIO5 is a strapping pin, so cs is the one to move
    int8_t   clk  = 18;  // first if a board will not boot with a card attached
    int8_t   miso = 19;
    // Bus speed in kHz. A setting rather than a constant because it is the
    // first thing to change when a card enumerates and then fails its first
    // real read, which is what dupont jumpers to a breakout produce: the card
    // is found at 400 kHz, the driver steps up, and the bus no longer carries
    // a clean edge. Telling a sysop to reflash to try a slower bus is not an
    // answer, so this is in system.cfg.
    uint16_t speedKHz = 20000;
};

struct SdInfo {
    char     type[12] = {};   // "SDSC", "SDHC/SDXC", "MMC", empty when unmounted
    uint32_t totalKB  = 0;
    uint32_t freeKB   = 0;
    uint32_t speedKHz = 0;
    bool     mounted  = false;
};

// sdMount: mount FAT32 at sdBase(). Blocking, see above. On failure writes a
// reason into err (a sysop has to be told which of "no card", "not FAT32" and
// "miswired" it was, because they are three different evenings) and returns
// false. Mounting when already mounted succeeds and changes nothing.
bool sdMount(const SdPins& pins, char* err, size_t errLen);

// sdUnmount: flush and release. Safe to call when not mounted.
void sdUnmount();

// sdBase: mount point, no trailing slash. Empty string when no card, which
// is the test callers should use: a board without a card is not an error.
const char* sdBase();

SdInfo sdInfo();

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

// ledSignal: hold the activity LED on for ms, for something worth noticing
// rather than ordinary traffic. Used once at boot so a sysop can see the
// board is listening instead of guessing and dialling in too early.
void ledSignal(uint32_t now, uint32_t ms);

// ---------------------------------------------------------------------------
// resetReason / resetWasCrash: why this boot happened, in words a sysop can
// read. A crash that reboots cleanly is invisible, so the board has to say
// so itself rather than leaving somebody to infer it from an uptime that
// keeps starting over.
// ---------------------------------------------------------------------------
const char* resetReason();
bool        resetWasCrash();

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
