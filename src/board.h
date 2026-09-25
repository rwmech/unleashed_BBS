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
 *               BBS_BOARD_FN_WROVER_CAM  Freenove ESP32-WROVER CAM (FNK0060,
 *                                      pinout 3.0): ESP32-WROVER-E, 4 MB
 *                                      flash, 8 MB PSRAM, an OV2640 and an
 *                                      SDMMC 1-bit card slot. Researched in
 *                                      internal/PLAN-freenove-cam.md.
 *
 *               Capabilities a profile may define:
 *                 BBS_HAS_LCD       a panel the panel plugin drives
 *                 BBS_CHIP_S3       the chip is an ESP32-S3 (pin rules,
 *                                   RMT sizing, the console on native USB)
 *                 BBS_HAS_PSRAM     the board has PSRAM its build turns on
 *                 BBS_SD_SDMMC1     the card slot is SDMMC 1-bit, on the
 *                                   BBS_SDMMC_* pins, not SPI
 *                 BBS_PINS_*        pins the board owns, which pinProblem
 *                                   refuses with the reason
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E, ESP32-S3 (ESP-IDF 5.3.1) and the Linux host
 * See also:     ESP32_BOARD_CHOICE.md, platformio.ini
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

// The profile's own version, beside the core's (Rob, 1.1.0: "version the S3
// slightly different ... since we have the core versions and s3 versions
// that compile different"). BBS_VERSION stays the core's, shared by every
// board; this one moves when this profile's own code does. Shown wherever
// the version is shown, as BBS_VERSION_SHOWN (config.h) puts it. The
// reference board defines neither.
#define BBS_BOARD_TAG         "S3"
#define BBS_BOARD_VERSION     "1.1.0"

// The internal heap a plugin may not take at start (config.h). 16 KB, not
// the WROOM's 40: with PSRAM, Wi-Fi's and lwIP's buffers go there
// (sdkconfig.defaults.esp32s3, TRY_ALLOCATE_WIFI_LWIP), and so does the
// backup inflater, which is above the IDF's always-internal threshold. The
// IDF keeps its own 32 KB internal pool for stacks and DMA besides. At 40 on
// the first flash, the lights and the panel were refused with 35 KB free.
#define BBS_HEAP_RESERVE      16384

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

// The serial bridge on header IO2 (RX) and IO1 (TX). Not the header's RXD
// and TXD (44 and 43): those are UART0's, and the chip's ROM prints its boot
// banner on them at every reset, which a device wired there would read as
// input. 16 and 17 are the TF slot; IO3 is a strapping pin.
#define BBS_SERIAL_RX         2
#define BBS_SERIAL_TX         1

// The panel: ST7789, 172 x 320, driven portrait. SDA 45, SCL 40, CS 42,
// D/C 41, RES 39, backlight 48 (active high through an N-MOSFET). The
// 172-pixel axis sits 34 into the controller's 240 (Waveshare's demo and
// espp agree). BGR and inverted, as the demo drives it.
//
// Portrait because of how the stick is used (Rob): it hangs from a USB-A
// port with the plug at the top and the screen facing the room, so the
// glass is tall and narrow. The width, height and offsets are the glass
// with the plug up, which with the mirror is exactly what Waveshare's demo
// draws. CONFIG panel's "USB plug" turns it (Rob, 1.1.0: on a cable, read
// in landscape): left and right draw landscape at 320 x 172, down is
// portrait upside down, and the panel works out the rest (panel_gfx.h,
// scanFor).
//
// The demo's clock is
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
#define BBS_LCD_WIDTH         172
#define BBS_LCD_HEIGHT        320
#define BBS_LCD_XOFF          34
#define BBS_LCD_YOFF          0
#define BBS_LCD_ORIENT        0       // the USB plug: 0 up, 1 left, 2 right, 3 down
#define BBS_LCD_INVERT        1
#define BBS_LCD_BGR           1
#define BBS_LCD_MIRROR        1       // the demo mirrors X to draw portrait upright
#define BBS_LCD_MHZ           10      // 10 | 20 | 40
#define BBS_LCD_BACKLIGHT     60      // percent

#endif  // BBS_BOARD_WS_S3LCD147

// ===========================================================================
// Freenove ESP32-WROVER CAM (the FNK0060 kit, pinout revision 3.0)
//
// ESP32-WROVER-E (ESP32-D0WD-V3, read as revision v3.1 on the bench), 4 MB
// flash, 8 MB quad PSRAM on GPIO 16 and 17, an OV2640 on a 24-pin ribbon, a
// micro SD slot wired for SDMMC 1-bit on 14, 15 and 2, USB-C through a
// CH340. Every pin below is from Freenove's own pinout drawing and sketches,
// cited in internal/PLAN-freenove-cam.md: ESP32_Pinout_V3.0.png, the
// CAMERA_MODEL_WROVER_KIT block of Sketch_06.1's camera_pins.h, and
// Sketch_03.1_SDMMC_Test.
//
// No NeoPixel is documented: the V3.0 drawing marks four on-board LEDs, IO2,
// TX, RX and ON, all plain, and neither the drawing, the sketches nor the C
// tutorial mention a WS2812 on the board (checked 2026-09-24). So the lights
// plugin ships off with no drive pin, as on the WROOM, until the bench shows
// otherwise. IO2's LED is the card's D0 and is never blinked.
// ===========================================================================
#if defined(BBS_BOARD_FN_WROVER_CAM)

#if defined(ESP_PLATFORM) && !CONFIG_IDF_TARGET_ESP32
#error "BBS_BOARD_FN_WROVER_CAM is an ESP32 board: build it for the esp32 target"
#endif
#if defined(BBS_BOARD_WS_S3LCD147)
#error "one board profile at a time"
#endif

#define BBS_BOARD_NAME        "Freenove ESP32-WROVER CAM"
#define BBS_BOARD_PLUGINS     1       // the camera

#define BBS_BOARD_TAG         "FNCAM"
#define BBS_BOARD_VERSION     "1.0.1"

// PSRAM (sdkconfig.defaults.fncam). Wi-Fi's and lwIP's buffers go there.
// The internal reserve stays the WROOM's 40 KB until the bench's MEM says
// what it should be (the plan's phase 3): not guessed. A build that lost
// the sdkconfig layer (a stale sdkconfig.freenove_wrover_cam, PlatformIO not
// passing SDKCONFIG_DEFAULTS) would otherwise link quietly without PSRAM.
#define BBS_HAS_PSRAM         1
#if defined(ESP_PLATFORM) && !CONFIG_SPIRAM
#error "BBS_BOARD_FN_WROVER_CAM needs PSRAM: sdkconfig.defaults.fncam was not applied (delete sdkconfig.freenove_wrover_cam*)"
#endif

// No activity LED: the only plain user LED is IO2, which is the card's D0.
#define BBS_LED_GPIO          -1

// The card slot: SDMMC 1-bit, CLK 14, CMD 15, D0 2 (Freenove: "Please do not
// modify it"). The ESP32's SDMMC slot 1 is on the IO MUX, so these are the
// only pins it can be. The SPI pins are none: all four of the WROOM's
// defaults (5, 23, 18, 19) are camera data lines here.
#define BBS_SD_SDMMC1         1
#define BBS_SDMMC_CLK         14
#define BBS_SDMMC_CMD         15
#define BBS_SDMMC_D0          2
#define BBS_SD_CS             -1
#define BBS_SD_MOSI           -1
#define BBS_SD_CLK            -1
#define BBS_SD_MISO           -1

// The serial bridge on header IO33 (RX) and IO32 (TX), off until enabled.
// The WROOM's 16 and 17 are this board's PSRAM.
#define BBS_SERIAL_RX         33
#define BBS_SERIAL_TX         32

// The camera, an OV2640 (Freenove's CAMERA_MODEL_WROVER_KIT). PWDN and RESET
// are not wired. The camera plugin reads these and nothing else, so another
// camera board is another block here, not another plugin.
//   BBS_CAM_SENSOR      the sensor the build carries a driver for (only
//                       that one: sdkconfig.defaults.fncam)
//   BBS_CAM_SIZES       what CONFIG camera offers, the sensor's own sizes
//   BBS_CAM_SIZE        the size as shipped, one of them
//   BBS_CAM_FLASH_PIN   the flash output as shipped (Rob: "leave a single
//                       pin, just make it neopixel or ... relay high on the
//                       pin"): the first free pin that is no strap
//   BBS_CAM_FLASH       its mode as shipped: 0 off, 1 pixel, 2 pin. Off
//                       here: this board has no pixel of its own
#define BBS_HAS_CAMERA        1
#define BBS_CAM_SENSOR        "OV2640"
#define BBS_CAM_SIZES         "qvga|vga|svga|xga|sxga|uxga"
#define BBS_CAM_SIZE          2       // svga
#define BBS_CAM_FLASH_PIN     13
#define BBS_CAM_FLASH         0
#define BBS_CAM_PWDN          -1
#define BBS_CAM_RESET         -1
#define BBS_CAM_XCLK          21
#define BBS_CAM_SIOD          26
#define BBS_CAM_SIOC          27
#define BBS_CAM_D7            35      // Y9
#define BBS_CAM_D6            34      // Y8
#define BBS_CAM_D5            39      // Y7
#define BBS_CAM_D4            36      // Y6
#define BBS_CAM_D3            19      // Y5
#define BBS_CAM_D2            18      // Y4
#define BBS_CAM_D1            5       // Y3
#define BBS_CAM_D0            4       // Y2
#define BBS_CAM_VSYNC         25
#define BBS_CAM_HREF          23
#define BBS_CAM_PCLK          22

// Pins the board itself owns, which syscfg::pinProblem refuses for every
// pin setting, with the reason. What is left for a sysop to wire is 13, 32
// and 33, which is the truth about this board.
//   PSRAM    16, 17: the WROVER's PSRAM chip select and clock
//   CONSOLE  1, 3: UART0 to the CH340, which is the console, Improv and
//            the flashing port
//   CARD     the slot's three lines
//   CAMERA   every wired camera line
//   STRAP    12, MTDI, which sets the flash voltage at reset: anything that
//            pulls it high at reset stops the board booting
#define BBS_PINS_PSRAM        16, 17
#define BBS_PINS_CONSOLE      1, 3
#define BBS_PINS_CARD         BBS_SDMMC_CLK, BBS_SDMMC_CMD, BBS_SDMMC_D0
#define BBS_PINS_CAMERA       BBS_CAM_XCLK, BBS_CAM_SIOD, BBS_CAM_SIOC, BBS_CAM_D7, \
                              BBS_CAM_D6, BBS_CAM_D5, BBS_CAM_D4, BBS_CAM_D3, BBS_CAM_D2, \
                              BBS_CAM_D1, BBS_CAM_D0, BBS_CAM_VSYNC, BBS_CAM_HREF, BBS_CAM_PCLK
#define BBS_PINS_STRAP        12

#endif  // BBS_BOARD_FN_WROVER_CAM

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
