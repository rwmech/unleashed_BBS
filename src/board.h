/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/board.h
 * Module:       Core / board profiles
 *
 * Purpose:      What differs between the boards this firmware is built for,
 *               in one place. A board is a profile, never a fork: the same
 *               source, one PlatformIO environment that names the board
 *               (-DBBS_BOARD_...), and the defaults below. Everything a
 *               board adds (a display, its CONFIG page, its commands) is
 *               compiled only under the capability that board defines, so
 *               the reference image carries none of it.
 *
 * Design:       The reference board is the bare ESP32-WROOM-32E, and it is
 *               the floor: every default at the bottom of this file is its
 *               value, and a board profile only overrides. A profile's pins
 *               are defaults, not rules: every one is a setting a sysop can
 *               change in CONFIG when a board's spec changes.
 *
 *               BBS_BOARD_WS_S3LCD147  Waveshare ESP32-S3-LCD-1.47 (SKU
 *                                      28317): ESP32-S3R8, 16 MB flash, 8 MB
 *                                      octal PSRAM, native USB only, a TF
 *                                      slot, one WS2812B and a 1.47" ST7789
 *                                      panel. Pins from Waveshare's own
 *                                      schematic, researched in
 *                                      internal/board-waveshare-s3-lcd147-
 *                                      2026-09-24.md.
 *
 *               Capabilities a profile may define:
 *                 BBS_HAS_LCD       a panel the panel plugin drives
 *                 BBS_CHIP_S3       the chip is an ESP32-S3 (pin rules,
 *                                   RMT sizing, the console on native USB)
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E, ESP32-S3 (ESP-IDF 5.3.1) and the Linux host
 * See also:     ESP32_BOARD_CHOICE.md, platformio.ini
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

// ---------------------------------------------------------------------------
// The chip. On the board it is the build target's, from sdkconfig; on the
// host a board profile stands in for it, so a host build of an S3 profile
// applies the S3's pin rules.
// ---------------------------------------------------------------------------
#if defined(ESP_PLATFORM)
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32S3
#define BBS_CHIP_S3 1
#endif
#endif

// ===========================================================================
// Waveshare ESP32-S3-LCD-1.47
// ===========================================================================
#if defined(BBS_BOARD_WS_S3LCD147)

#if defined(ESP_PLATFORM) && !CONFIG_IDF_TARGET_ESP32S3
#error "BBS_BOARD_WS_S3LCD147 is an ESP32-S3 board: build it for the esp32s3 target"
#endif
#ifndef BBS_CHIP_S3
#define BBS_CHIP_S3 1                 // the host's stand-in, see above
#endif

#define BBS_BOARD_NAME        "Waveshare ESP32-S3-LCD-1.47"
#define BBS_HAS_LCD           1
#define BBS_BOARD_PLUGINS     1       // the panel

// No plain LED on this board. GPIO38 is the WS2812B's data line, which a
// plain on/off would read as noise; the lights plugin drives it instead.
#define BBS_LED_GPIO          -1

// The lights plugin: on from the first boot, because the pixel is on the
// board rather than wired by somebody. Drive light on the onboard WS2812B.
// Its colour order is not confirmed: the part is a WS2812B (conventionally
// GRB), but the one piece of code known to show the right colours on this
// board puts R, G, B on the wire in that order, so RGB. LIGHTS TEST settles
// it on the bench: red first.
#define BBS_LIGHTS_ON         1
#define BBS_LIGHTS_DRIVE_PIN  38
#define BBS_LIGHTS_DRIVE_ORDER 1      // RGB, in lights::kOrders' order

// The TF slot in SPI mode, the schematic's own net names: SD_CS 21,
// SD_MOSI 15, SD_SCLK 14, SD_MISO 16. D1 and D2 are pulled up on the board.
#define BBS_SD_CS             21
#define BBS_SD_MOSI           15
#define BBS_SD_CLK            14
#define BBS_SD_MISO           16

// The serial bridge on the header's RXD and TXD, UART0's pins, which are
// free: the console is on the native USB port. 16 and 17 are the TF slot.
#define BBS_SERIAL_RX         44
#define BBS_SERIAL_TX         43

// The panel: ST7789, 172 x 320, driven landscape. SDA 45, SCL 40, CS 42,
// D/C 41, RES 39, backlight 48 (active high through an N-MOSFET). The
// 172-pixel axis sits 34 into the controller's 240 (Waveshare's demo and
// espp agree). BGR and inverted, as the demo drives it. The demo's clock is
// 12 MHz and the panel's own limit 62.5 MHz (16 ns write cycle); the SPI
// clock is 80 MHz divided by a whole number, so 12 would really be 11.4.
// 10 is the proven side of the demo's figure, and the panel only ever
// redraws what changed, a few KB at a time.
#define BBS_LCD_MOSI          45
#define BBS_LCD_SCLK          40
#define BBS_LCD_CS            42
#define BBS_LCD_DC            41
#define BBS_LCD_RST           39
#define BBS_LCD_BL            48
#define BBS_LCD_WIDTH         320
#define BBS_LCD_HEIGHT        172
#define BBS_LCD_XOFF          0
#define BBS_LCD_YOFF          34
#define BBS_LCD_ROTATION      90      // 0 | 90 | 180 | 270
#define BBS_LCD_INVERT        1
#define BBS_LCD_BGR           1
#define BBS_LCD_MIRROR        1       // the demo mirrors X to draw portrait upright
#define BBS_LCD_MHZ           10      // 10 | 20 | 40
#define BBS_LCD_BACKLIGHT     60      // percent

#endif  // BBS_BOARD_WS_S3LCD147

// ===========================================================================
// The reference board, the bare ESP32-WROOM-32E: every default a profile
// did not set. These are the values the WROOM has always had, so a build
// with no profile is byte for byte what it was.
// ===========================================================================
#ifndef BBS_BOARD_NAME
#define BBS_BOARD_NAME        "ESP32-WROOM-32E"
#endif
#ifndef BBS_BOARD_PLUGINS
#define BBS_BOARD_PLUGINS     0
#endif
#ifndef BBS_LED_GPIO
#define BBS_LED_GPIO          2       // blue LED on DOIT-style dev boards, -1 = none
#endif
#ifndef BBS_LIGHTS_ON
#define BBS_LIGHTS_ON         0       // off until a sysop switches it on
#endif
#ifndef BBS_LIGHTS_DRIVE_PIN
#define BBS_LIGHTS_DRIVE_PIN  -1      // and gives it a pin
#endif
#ifndef BBS_LIGHTS_DRIVE_ORDER
#define BBS_LIGHTS_DRIVE_ORDER 0      // GRB, the WS2812B's own order
#endif
#ifndef BBS_SD_CS
#define BBS_SD_CS             5       // the wiring page: CS D5, MOSI D23,
#define BBS_SD_MOSI           23      // CLK D18, MISO D19
#define BBS_SD_CLK            18
#define BBS_SD_MISO           19
#endif
#ifndef BBS_SERIAL_RX
#define BBS_SERIAL_RX         16      // UART2's usual pins on a WROOM-32E
#define BBS_SERIAL_TX         17
#endif

// ---------------------------------------------------------------------------
// GPIO ranges, per chip, for the plugins that check a pin before they drive
// it. pinProblem (core/sysconfig) is the rule about which pins are the
// flash's or the USB's; these are the plainer facts of the part.
//
//   BBS_GPIO_MAX      the highest GPIO number the chip has
//   BBS_GPIO_OUT_MAX  the highest that can drive an output. On the ESP32,
//                     34 to 39 are inputs only; the S3 has no input-only pins
// ---------------------------------------------------------------------------
#if defined(BBS_CHIP_S3)
#define BBS_GPIO_MAX          48
#define BBS_GPIO_OUT_MAX      48
#else
#define BBS_GPIO_MAX          39
#define BBS_GPIO_OUT_MAX      33
#endif
