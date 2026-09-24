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
 * Interfaces:   millis, random32, fsBase, logsBase, heap, hardware, wifiRssi, log,
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
// is allowed to claim. False when the platform cannot tell. The figure may
// be up to a minute old: measuring it on LittleFS walks the whole partition
// with the loop stopped, so it is measured once and kept.
bool userInfo(uint32_t& total, uint32_t& used);

// fsInfoStale: forget the kept free-space figures, after something rewrote a
// partition wholesale (a backup restore). The next ask measures again.
void fsInfoStale();

// ---------------------------------------------------------------------------
// heap: heap statistics for MEM command and per-session measurement
// ---------------------------------------------------------------------------
HeapStats heap();

// Free bytes alone, and nothing else. heap() also asks for the largest free
// block, which on the IDF walks the whole pool under a critical section with
// interrupts off and a spinlock core 0's allocator contends for. That is the
// right price for MEM, which a person types; it is the wrong price for
// anything on the loop's own path. This is a counter read.
uint32_t heapFree();

// stackFree: the least free space the BBS task's stack has EVER had, in
// bytes, or 0 where it cannot be measured.
//
// Not "free right now": the high water mark, which is the only version of
// this number worth having. A reading taken at the prompt says nothing
// about the deepest call the board has made since boot, and the deepest
// call is the one that overflows.
//
// It exists to settle an argument rather than to decorate a screen. Twenty
// two UserRec scratch buffers were static, each with a comment saying that
// kept them "off the task stack", and making them ordinary locals returned
// about 10 KB of static DRAM in 1.1.0, once the stack had grown to take
// them. Whether that stays safe is a measurement, and this is it.
//
// On the host it is the same measurement of a different stack: the BBS runs
// on a thread whose stack is painted the way FreeRTOS paints a task's, so the
// figure is real, but it is x86-64 frames under glibc rather than Xtensa
// frames under newlib. Compare host figures with each other, never with the
// board's.
uint32_t stackFree();

// stackSize: the BBS task's whole stack in bytes, what stackFree is out of.
// 0 where it is not known.
uint32_t stackSize();

// stackDeeper: has the stack been used below the mark stackFree last gave?
// knownFree is that figure. Returns the new, lower stackFree when it has,
// and 0 when it has not, which is nearly always.
//
// stackFree reads every free byte from the bottom up and is too dear to call
// several times a pass. This reads only a band just under the old mark,
// which is where a deeper call almost always writes first. Almost: a frame
// that reserves a large buffer and leaves the far end of it untouched can
// step over the band, so the loop also calls stackFree once a second and
// catches what this missed, only without knowing which phase did it.
uint32_t stackDeeper(uint32_t knownFree);

// powerSave: what mode the radio is really in, as a word.
//
// "none", "min" or "max", or "" on a build with no radio. Not what the
// application asked for: what esp_wifi_get_ps() reports back. The lag this
// exists for was diagnosed twice from the outside and got the wrong answer
// once, and the whole reason it was hard is that nothing on the board would
// say.
const char* powerSave();

// ---------------------------------------------------------------------------
// hardware: what this board is, in a few words, for the directory's system
// badge: "ESP32 · 4 MB" or "ESP32-S3 · 16 MB · PSRAM" (UTF-8 middle dots).
// Read off the running firmware, never typed by the sysop. The chip model
// is the chip's own; the flash is the flash this image can use (the size in
// its header), which on a larger module running a smaller image is less
// than the chip has. PSRAM is named only when the firmware can use it: a
// WROVER running the WROOM build says nothing about the PSRAM it is not
// using. "host" on the host build. Call it once, at start.
// ---------------------------------------------------------------------------
void hardware(char* out, size_t n);

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
    int8_t   mosi = 23;  // GPIO5 is a strapping pin, but it only sets the SDIO
    int8_t   clk  = 18;  // slave timing, which this board never uses, so a card
    int8_t   miso = 19;  // on it cannot stop a boot (ESP32 datasheet, strapping
                         // pins). This said otherwise until 0.22.0.
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
// gpio -1 disables it. Begin may be called more than once: app_main brings
// the LED up before the network for the BOOT-hold watch, and Bbs::begin's
// call for the same pin then changes nothing.
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
// Recovery without a reflash (1.1.0, core/recovery). Additive: nothing above
// changed to make room for these.
// ---------------------------------------------------------------------------

// bootButtonDown: is the BOOT button (BBS_BOOT_GPIO) pressed right now? now
// is the caller's clock, which the host build plays a simulated hold against
// (BBS_BOOT_HOLD_MS, pressed from 500 ms for that long). The board sets the
// pin up on the first call, so it can be asked before anything else runs.
bool bootButtonDown(uint32_t now);

// ledOverride: take the activity LED away from traffic. 1 holds it on, 0
// holds it off, -1 hands it back. While it is held, activityPulse,
// activityTick and ledSignal leave it alone. Remembered if the LED is not
// set up yet, and applied when activityLedBegin runs.
void ledOverride(int8_t state);

// factoryErase: erase the userdata and logs partitions, whole. Never the
// screens partition, the firmware or the SD card. The board is to restart
// straight after, and the mount at boot formats what it finds erased. False
// with the partition's name in err when one could not be erased.
bool factoryErase(char* err, size_t errLen);

// restart: restart the board, leaving note for the next boot to read through
// restartNote. Does not return on the board. The host build starts itself
// again in place, which is what lets a test see the boot that follows.
void restart(uint8_t note);

// restartNote: the note the previous boot left with restart, or 0. Only a
// software restart carries one: a power cut or a crash says nothing, however
// the memory it lived in happens to read.
uint8_t restartNote();

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
