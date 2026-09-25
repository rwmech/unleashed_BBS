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
 *               backupButton*, activityLed*, diskPulse, diskSeen, pixels*,
 *               inflateRaw
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
#include "../board.h"          // the pins a board profile ships with

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
    // The board profile's (board.h). On the WROOM, the board Rob wired: CS
    // D5, MOSI D23, CLK D18, MISO D19. GPIO5 is a strapping pin, but it only
    // sets the SDIO slave timing, which this board never uses, so a card on
    // it cannot stop a boot (ESP32 datasheet, strapping pins). This said
    // otherwise until 0.22.0. On the Waveshare S3 the TF slot, 21/15/14/16.
    int8_t   cs   = BBS_SD_CS;
    int8_t   mosi = BBS_SD_MOSI;
    int8_t   clk  = BBS_SD_CLK;
    int8_t   miso = BBS_SD_MISO;
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
// The console as a byte pipe, for Improv Wi-Fi Serial (main.cpp): the port
// the board was flashed through, which is the one the browser holds open.
// On the ESP32 that is UART0, through the board's USB-serial bridge. On the
// S3 boards it is the chip's own USB (USB-Serial-JTAG): the Waveshare stick
// has no bridge at all, and UART0 goes only to two header pins that no
// browser will ever hear.
//
// consoleBegin: give the console a driver so it can be read without
//               blocking. Once, first thing at boot. False when the driver
//               would not install, and then Improv is off.
// consoleRead:  what has arrived, never waiting.
// consoleWrite: bytes exactly as given: no newline translation, which
//               stdout would do to a length or checksum byte of 0x0A. The
//               caller holds stdout's lock so a log line cannot land inside.
//               A port nobody is reading (a board on a charger) drops the
//               bytes rather than holding the loop.
// ---------------------------------------------------------------------------
bool   consoleBegin();
size_t consoleRead(uint8_t* buf, size_t cap);
void   consoleWrite(const uint8_t* b, size_t n);

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
// diskPulse / diskSeen: storage was touched, for the lights plugin's drive
// light.
//
// Called from the storage paths themselves, so it has to cost nothing worth
// measuring: it records the time and bumps a count, and lights nothing. The
// plugin reads diskSeen() at its own frame rate and decides what that looks
// like, so a board with no lights pays two stores per call and no more.
// Counts rather than a flag, because two readers (or a reader that missed a
// frame) must each see "something happened since I last looked" without
// clearing it for the other. They wrap; only a change means anything.
//
// DISK_CARD is the SD card, DISK_FLASH the board's own LittleFS partitions,
// DISK_ERROR a read or write that failed on either.
// ---------------------------------------------------------------------------
enum DiskKind : uint8_t { DISK_CARD, DISK_FLASH, DISK_ERROR, DISK_KINDS };
void diskPulse(DiskKind kind);

struct DiskSeen {
    uint32_t at[DISK_KINDS]    = {};   // millis of the last pulse of each kind
    uint16_t count[DISK_KINDS] = {};   // pulses of each kind, wrapping
};
DiskSeen diskSeen();

// ---------------------------------------------------------------------------
// Addressable pixels: WS2812B, for the lights plugin's two outputs.
//
// On the board each output is an RMT transmit channel, the peripheral that
// exists so nobody has to bit-bang a 150 ns tolerance in a critical section
// on a chip that is also running Wi-Fi. A frame is handed over and goes out
// in hardware; pixelsShow never waits for it. On the host each output is a
// record of the last frame shown, which is what the tests read back.
//
// Colours go in as RGB, three bytes a pixel, and come back out of
// pixelsFrame as RGB. The order they go out on the wire is each output's,
// given at pixelsBegin: GRB on a WS2812B, but not on every strip sold as one,
// and the Waveshare S3's onboard pixel appears to want RGB. This layer does
// the reordering; the plugin only names the order (1.1.0).
//
// pixelsBegin: claim a pin for an output of count pixels, in one of the
//              PixOrder orders. False when the pin cannot drive one, or no
//              RMT channel is free. Beginning an output that is already
//              running ends it first.
// pixelsEnd:   dark, then let the pin go. Waits up to a few milliseconds for
//              a frame in flight, which is why only a plugin's stop calls it.
// pixelsShow:  send a frame. False while the last one is still going out, in
//              which case the caller simply draws a fresh one next time. A
//              frame identical to the last one sent is not sent again.
// pixelsFrame: the last frame this output was given, as RGB, into room for
//              cap pixels (cap * 3 bytes), and how many it wrote. 0 for an
//              output that is not running. What LIGHTS shows, and what the
//              tests read.
// ---------------------------------------------------------------------------
constexpr uint8_t kPixelOuts = 2;     // the drive light and the effect strip

// kPixelMax: the most pixels an output may have, on every chip (1.1.0; ten
// before the strip's length became a setting). Every buffer that holds a
// frame is this long, statically.
//
// Sixteen, because that is what every link in the chain can carry, the
// tightest first:
//   - CONFIG's Pixels page, one row a pixel for manual mode, holds sixteen
//     rows (Form::kMaxFields). A seventeenth pixel could never be given an
//     effect. lights.cpp asserts it.
//   - The ESP32 sends a whole frame out of the RMT channel's own memory with
//     no refill, because a refill a Wi-Fi interrupt delays by one bit time
//     stretches a low into a latch. 16 x 24 + 2 = 386 symbols is 7 of its
//     eight 64-symbol blocks, and the drive light has the eighth. 18 would
//     fit; 19 would not.
//   - The S3's blocks are 48 symbols, so that way it holds 13 beside the
//     drive light. Its strip goes on the one RMT channel with DMA instead
//     (TX channel 3), where the whole frame sits in a DMA buffer: the same
//     no-refill property, with room for far more than sixteen.
constexpr uint8_t kPixelMax  = 16;

// The orders a pixel's bytes can go out in. kPixWire[order][k] is the RGB
// channel (0 red, 1 green, 2 blue) sent as the k-th byte. The words for
// them, in the same order, are lights.h's kOrders.
enum PixOrder : uint8_t { PIX_GRB, PIX_RGB, PIX_BRG, PIX_RBG, PIX_GBR, PIX_BGR, PIX_ORDERS };
constexpr uint8_t kPixWire[PIX_ORDERS][3] = {
    { 1, 0, 2 }, { 0, 1, 2 }, { 2, 0, 1 }, { 0, 2, 1 }, { 1, 2, 0 }, { 2, 1, 0 },
};

bool    pixelsBegin(uint8_t out, int pin, uint8_t count, uint8_t order = PIX_GRB);
void    pixelsEnd(uint8_t out);
bool    pixelsShow(uint8_t out, const uint8_t* rgb, uint8_t count);
uint8_t pixelsFrame(uint8_t out, uint8_t* rgb, uint8_t cap);

#ifdef BBS_HAS_LCD
// ---------------------------------------------------------------------------
// The panel (BBS_HAS_LCD boards only: the panel plugin). An ST7789 on SPI,
// through the IDF's esp_lcd, colour data sent with DMA and never waited for.
// On the host, a record of the glass the tests can read back.
//
// lcdBegin:      bring the panel up on these settings: the bus, the reset,
//                the controller's init, the backlight. Blocking, about a
//                third of a second, so a plugin's start only. False with a
//                reason in err. Beginning again ends what was up first.
// lcdSame:       up, on exactly these settings. A CONFIG save restarts every
//                plugin, and a panel whose settings did not change has no
//                reason to go dark and through its reset for it.
// lcdEnd:        backlight off, the panel asleep, the bus given back.
// lcdReady:      nothing is on the wire: lcdDraw may be called. Colour data
//                goes out by DMA while the BBS loop gets on with everything
//                else, and this is how the panel plugin knows it is done.
// lcdBandPixels: the most pixels one lcdDraw can take (a staging buffer in
//                internal DMA memory, allocated at lcdBegin: DMA cannot
//                reach PSRAM without the SPI driver allocating a bounce
//                buffer per transfer, which would be heap in the loop).
// lcdDraw:       copy a w x h rectangle of fb (native RGB565, stride pixels
//                a row) to the staging buffer and send it to x, y. False,
//                sending nothing, when not ready or too big for one band.
// lcdBacklight:  percent, 0 dark.
// psramAlloc:    a block of the board's PSRAM, for a framebuffer (110 KB at
//                320 x 172), or nullptr. At a plugin's start, never in the
//                loop. psramFree gives it back.
// ---------------------------------------------------------------------------
struct LcdCfg {
    int8_t   mosi = -1, sclk = -1, cs = -1, dc = -1, rst = -1, bl = -1;
    uint16_t width = 0, height = 0;   // as drawn, after rotation
    uint16_t xoff = 0, yoff = 0;      // where that window sits in the controller's RAM
    uint16_t rotation = 0;            // 0, 90, 180 or 270
    bool     invert = false;          // INVON: an IPS panel is normally black
    bool     bgr = false;             // the panel's colour order
    bool     mirror = false;          // glass wired mirrored against the controller's RAM
    uint8_t  mhz = 10;                // the SPI clock
    uint8_t  backlight = 100;         // percent
};

bool     lcdBegin(const LcdCfg& c, char* err, size_t errLen);
bool     lcdSame(const LcdCfg& c);
void     lcdEnd();
bool     lcdReady();
uint32_t lcdBandPixels();
bool     lcdDraw(const uint16_t* fb, uint16_t stride, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void     lcdBacklight(uint8_t pct);
void*    psramAlloc(size_t n);
void     psramFree(void* p);
#endif  // BBS_HAS_LCD

#ifdef BBS_HAS_CAMERA
// ---------------------------------------------------------------------------
// The camera (BBS_HAS_CAMERA boards only: the camera plugin). Everything here
// but pinOut and camDmaLargest BLOCKS, for up to seconds, and is called only
// from the camera's worker task (taskStart), never from the BBS loop: the
// loop is cooperative, and a sensor's bring-up stalling it would stall every
// caller (Rob's rule no. 1: the online experience without lag is paramount).
//
// camOpen:       bring the sensor up with these settings (pins from board.h).
//                err says why not; the sensor found is named in the log.
// camGrab:       one frame; the pointer is good until camRelease. False
//                after a second with no frame. JPEG from a sensor that
//                encodes, RGB565 (w x h x 2, high byte first) from one that
//                does not: camRaw says which.
// camRaw:        the sensor up now gives RGB565, for jpegRaw to encode.
// camSensor:     the name of the sensor the last bring-up found, "" before
//                one has.
// camClose:      the sensor down and its memory back: the 32 KB DMA block and
//                the frame buffer. The camera is never left running.
// camDmaLargest: the largest internal DMA-capable block free now, which a
//                bring-up needs kCamDmaBlock of. No I/O, but it walks the
//                heap: once a snap, never a tick.
// camInternalFree: internal RAM free now, the DMA reserve pool included.
//                A bring-up needs kCamInternal of it: the DMA block, the
//                driver's own 4 KB task and its small change. The caller's
//                own task stack is on top (the plugin adds its worker's).
// camAlloc:      a block for a copy of a frame or the re-encoder's buffers,
//                PSRAM first. camFree gives it back.
// taskStart:     run fn(arg) once on a task of its own, on the BBS task's
//                core below its priority, so it only ever gets the loop's
//                idle time: that is the throttle. The task ends when fn does.
// taskSleep:     give the processor away for ms (0: to anything waiting).
// taskStackFree: the least free stack the calling task has had, in bytes.
// sdSpace:       the card's size and free space in bytes, read now (FAT's
//                own figure, which can mean a scan: the worker's, never the
//                loop's), not sdInfo's kept one. False with no card.
// sdList:        every entry of a folder on the card (path under sdBase(),
//                "photos/timelapse"), with whether it is a folder and its
//                size, in the order the card holds them, until fn returns
//                false. One read of the folder: FatFs's own directory entry
//                carries the size, so no entry is looked up again (a stat on
//                FAT searches the folder from the top, which made a walk of
//                a thousand photos a million entry reads). Dot entries are
//                left out. False when the folder cannot be opened.
// pinOut:        a GPIO as an output, driven high or low (the flash pin).
// jpegMark:      re-encode a JPEG a strip of rows at a time, calling draw on
//                each strip (RGB888) before it is encoded, and out with the
//                result as it is made. quality 1 to 100, higher is better.
//                False when this build cannot (the host) or the picture did
//                not decode; out may then have had a part, to discard.
// jpegRaw:       encode an RGB565 frame (camRaw) the same way, drawing on
//                each strip first. False when this build cannot (the host).
// ---------------------------------------------------------------------------
struct CamCfg {
    const char* size    = "svga";     // one of BBS_CAM_SIZES
    uint8_t     quality = 12;         // the sensor's, 0-63, lower is better
    bool        flip    = false;
    bool        mirror  = false;
    int8_t      bright = 0, contrast = 0, saturation = 0, exposure = 0;   // -2..2
    uint8_t     wb      = 0;          // auto|sunny|cloudy|office|home
    uint8_t     effect  = 0;          // none|negative|grey|red|green|blue|sepia
};

using MarkRowsFn = void (*)(void* ctx, uint8_t* rgb, uint16_t width, uint16_t y0, uint16_t rows);
using MarkOutFn  = bool (*)(void* ctx, const uint8_t* p, size_t n);

// What a bring-up of the ESP32's JPEG path takes from internal RAM
// (esp32-camera 2.1.7, read from its source): one 32,768-byte DMA buffer,
// eight half buffers of 4 KB that the ESP32's JPEG path fixes whatever
// CAMERA_DMA_BUFFER_SIZE_MAX says (target/esp32/ll_cam.c ll_cam_dma_sizes),
// in ONE piece (cam_hal.c cam_dma_config), with its sixteen descriptors and
// the driver's object beside it; then cam_task's 4 KB stack, its queues,
// the SCCB bus and the sensor's state. The block is rounded up to cover
// the descriptors and the heap's own headers.
constexpr uint32_t kCamDmaBlock = 32768u + 1024u;
constexpr uint32_t kCamInternal = kCamDmaBlock + 4096u + 2048u;

bool     camOpen(const CamCfg& c, char* err, size_t errLen);
bool     camGrab(const uint8_t*& buf, size_t& len, uint16_t& w, uint16_t& h);
void     camRelease();
void     camClose();
uint32_t camDmaLargest();
uint32_t camInternalFree();
bool     camRaw();
const char* camSensor();
void*    camAlloc(size_t n);
void     camFree(void* p);
bool     taskStart(void (*fn)(void*), void* arg, uint32_t stackBytes, const char* name);
void     taskSleep(uint32_t ms);
uint32_t taskStackFree();
bool     sdSpace(uint64_t& total, uint64_t& freeBytes);
using SdListFn = bool (*)(void* ctx, const char* name, bool dir, uint32_t size);
bool     sdList(const char* rel, SdListFn fn, void* ctx);
void     pinOut(int pin, bool high);
bool     jpegMark(const uint8_t* jpg, size_t len, uint8_t quality, MarkRowsFn draw, void* dctx,
                  MarkOutFn out, void* octx, uint16_t& width, uint16_t& height);
bool     jpegRaw(const uint8_t* rgb565, uint16_t w, uint16_t h, uint8_t quality, MarkRowsFn draw, void* dctx,
                 MarkOutFn out, void* octx);
#endif  // BBS_HAS_CAMERA

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

// ledSilent: silent mode (1.1.0, core/silent). While it is on, traffic and
// ledSignal leave the activity LED dark, and it is put out at once. Off hands
// it back to traffic, dark until the next pulse. ledOverride outranks it: the
// BOOT-hold watch shows its stages to the person holding the button, who is
// standing at the board and asked. Remembered if the LED is not set up yet.
void ledSilent(bool on);

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
