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
 *                                      flash, 8 MB PSRAM, a camera (an
 *                                      OV2640 documented, a GC0308 found)
 *                                      and an SDMMC 1-bit card slot. Researched in
 *                                      internal/PLAN-freenove-cam.md.
 *
 *               BBS_BOARD_AI_ESP32CAM  AI-Thinker ESP32-CAM: ESP32-D0WDQ6,
 *                                      4 MB flash, PSRAM (8 MB chip, 4 MB
 *                                      mapped), an OV2640,
 *                                      a micro SD slot (run over SPI)
 *                                      and a flash LED on GPIO 4.
 *
 *               Capabilities a profile may define:
 *                 BBS_HAS_LCD       a panel the panel plugin drives
 *                 BBS_CHIP_S3       the chip is an ESP32-S3 (pin rules,
 *                                   RMT sizing, the console on native USB)
 *                 BBS_HAS_PSRAM     the board has PSRAM its build turns on
 *                 BBS_HAS_SD_SLOT   a card slot on the board itself, so
 *                                   HARDWARE says "slot, empty" with no card
 *                                   in (the reference board's card is
 *                                   optional wiring, and says nothing)
 *                 BBS_SD_SDMMC1     the card slot is SDMMC 1-bit, on the
 *                                   BBS_SDMMC_* pins, not SPI
 *                 BBS_PINS_*        pins the board owns, which pinProblem
 *                                   refuses with the reason
 *                 BBS_PINS_HOLD_LOW pins driven low first thing at boot
 *                                   (a transistor's base that must not
 *                                   float: the ESP32-CAM's flash LED)
 *                 BBS_LED_ACTIVE_LOW the board's own LED (BBS_LED_GPIO)
 *                                   lights on a low pin
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
// Every board's printf is newlib nano (sdkconfig.defaults, 1.1.2), and the
// formats in src/ are checked for it (tools/check_formats.py). A generated
// sdkconfig.<env> from before 1.1.2 says "# CONFIG_NEWLIB_NANO_FORMAT is not
// set", which is a value and wins over the defaults, so it would build the
// full printf without a word and a bench image would not be what ships.
#if !CONFIG_NEWLIB_NANO_FORMAT
#error "the nano printf in sdkconfig.defaults was not applied: delete sdkconfig.<env> and build again"
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
#define BBS_BOARD_VERSION     "1.1.3"

// SSH (1.1.2 core, S3 1.1.3, a preview): encrypted logins on the board's
// own port, beside telnet (src/core/sshd.h). Eight at once: this board's
// 8 MB of PSRAM would hold far more at about 48 KB each, but eight is more
// than a preview on ten nodes needs, and the lower figure bounds the one SSH
// task's work. A board with 2 MB of PSRAM sets its own, lower, figure.
#define BBS_HAS_SSH           1
#define BBS_SSH_MAX           8

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
#define BBS_HAS_SD_SLOT       1
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
#define BBS_BOARD_VERSION     "1.0.8"

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
#define BBS_HAS_SD_SLOT       1
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

// The camera, on Freenove's CAMERA_MODEL_WROVER_KIT pins. PWDN and RESET are
// not wired. The camera plugin reads these and nothing else, so another
// camera board is another block here, not another plugin.
//
// Freenove document an OV2640, and the kit on the bench carries a GalaxyCore
// GC0308 (read over SCCB on 2026-09-25: one device, at 0x21, ID 0x9B at
// register 0x00): 640x480 at most, and no JPEG encoder of its own, so the
// platform takes RGB565 and encodes it (jpegRaw). Both drivers are built
// (sdkconfig.defaults.fncam), and the bring-up finds out which is there.
//   BBS_CAM_SENSOR      the sensor as shipped, named until a bring-up has
//                       found the real one (plat::camSensor)
//   BBS_CAM_SIZES       what CONFIG camera offers until a bring-up has found
//                       the sensor (the GC0308's sizes); after it, every
//                       size that sensor gives (camera_pic.h)
//   BBS_CAM_SIZE        the size as shipped, one of them
//   BBS_CAM_FLASH_PIN   the flash output as shipped (Rob: "leave a single
//                       pin, just make it neopixel or ... relay high on the
//                       pin"): the first free pin that is no strap
//   BBS_CAM_FLASH       its mode as shipped: 0 off, 1 pixel, 2 pin. Off
//                       here: this board has no pixel of its own
#define BBS_HAS_CAMERA        1
#define BBS_CAM_SENSOR        "GC0308"
#define BBS_CAM_SIZES         "qvga|vga"
#define BBS_CAM_SIZE          1       // vga
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
// AI-Thinker ESP32-CAM (the original design, sold under many names: the
// bench's is an Aideepen on an ESP32-CAM-MB programmer with a CH340G)
//
// ESP32-D0WDQ6 (read as revision v1.0 on the bench), 4 MB flash (d8/4016),
// PSRAM on GPIO 16 and 17 (an 8 MB chip, of which the ESP32 maps 4 MB),
// an OV2640 on the 24-pin ribbon (read over
// SCCB on 2026-09-25: one device at 0x30, PID 0x26, VER 0x42, MID 0x7FA2),
// a micro SD slot on the SDMMC pins, a bright white flash LED on GPIO 4
// (through a transistor), a small red LED on GPIO 33 (active low).
//
// Camera pins: arduino-esp32's CameraWebServer example, camera_pins.h,
// CAMERA_MODEL_AI_THINKER (PWDN 32, RESET -1, XCLK 0, SIOD 26, SIOC 27,
// Y9..Y2 35 34 39 36 21 19 18 5, VSYNC 25, HREF 23, PCLK 22), and the same
// map in the log of the ESPHome image the board shipped with. The camera
// driver probed the OV2640 and took UXGA frames on exactly these pins.
//
// GPIO 0 is the camera's XCLK here, so it cannot also be a button: the
// BOOT-hold reset and the backup window's button are off on this board
// (the MB programmer's IO0 button still selects download mode at reset,
// which is the ROM's and needs no firmware). The backup window can still be
// given a button on a free pin in CONFIG.
//
// The card is run over SPI (CS 13, MOSI 15, CLK 14, MISO 2), not SDMMC.
// On the bench SDMMC never got an answer to ACMD41, one bit or four, 20 MHz
// or 400 kHz, with two different cards, while SPI on the same slot mounted
// a 32 GB SDHC at once (2026-09-25). SPI also leaves GPIO 4 (the flash LED,
// the slot's D1) and GPIO 12 (D2, the flash-voltage strap) alone.
// ===========================================================================
#if defined(BBS_BOARD_AI_ESP32CAM)

#if defined(ESP_PLATFORM) && !CONFIG_IDF_TARGET_ESP32
#error "BBS_BOARD_AI_ESP32CAM is an ESP32 board: build it for the esp32 target"
#endif
#if defined(BBS_BOARD_WS_S3LCD147) || defined(BBS_BOARD_FN_WROVER_CAM)
#error "one board profile at a time"
#endif

#define BBS_BOARD_NAME        "AI-Thinker ESP32-CAM"
#define BBS_BOARD_PLUGINS     1       // the camera

#define BBS_BOARD_TAG         "ESPCAM"
#define BBS_BOARD_VERSION     "1.0.5"

// PSRAM (sdkconfig.defaults.espcam). A build that lost the sdkconfig layer
// would otherwise link quietly without it.
#define BBS_HAS_PSRAM         1
#if defined(ESP_PLATFORM) && !CONFIG_SPIRAM
#error "BBS_BOARD_AI_ESP32CAM needs PSRAM: sdkconfig.defaults.espcam was not applied (delete sdkconfig.esp32cam_aithinker*)"
#endif

// The activity LED is the small red one on GPIO 33, which is wired from
// 3V3 through the LED to the pin: on when the pin is low.
#define BBS_LED_GPIO          33
#define BBS_LED_ACTIVE_LOW    1

// GPIO 4 is the flash LED's transistor, through a resistor to its base,
// with nothing on the board holding it low: a floating or pulled-up pin
// lights it (a pull-up alone is enough, which is how an SDMMC card in
// four-bit mode, with the IDF's internal pull-ups, turns it on). Driven
// low in the start-up code, before app_main, the card or any plugin
// and left to the camera's flash from then on (off as shipped).
#define BBS_PINS_HOLD_LOW     4

// GPIO 0 is XCLK (above): no BOOT button for the firmware.
#define BBS_BOOT_GPIO         -1
#define BBS_BACKUP_GPIO       -1

// The card slot over SPI, on the slot's own lines: CS is its D3, MOSI its
// CMD, CLK its CLK, MISO its D0. They are the sd plugin's settings, as on
// the WROOM, so CONFIG says the sd plugin holds them.
#define BBS_HAS_SD_SLOT       1
#define BBS_SD_CS             13
#define BBS_SD_MOSI           15
#define BBS_SD_CLK            14
#define BBS_SD_MISO           2

// The serial bridge: no pins as shipped. With the camera, the card and the
// PSRAM wired, what is left is GPIO 33 (the red LED, active low) and GPIO 4
// (the flash LED): a sysop who wants the bridge gives it those in CONFIG
// serial, and the flash then has to be off. Not GPIO 12: a device's pull-up
// there at reset sets the flash to 1.8 V and the board does not boot.
#define BBS_SERIAL_RX         -1
#define BBS_SERIAL_TX         -1

// The camera. The OV2640 encodes JPEG itself, up to UXGA (1600x1200).
// The flash is the board's own LED on GPIO 4, as a "pin" flash, off as
// shipped: it is a very bright white LED, and it gets hot if left on.
#define BBS_HAS_CAMERA        1
#define BBS_CAM_SENSOR        "OV2640"
#define BBS_CAM_SIZES         "qvga|vga|svga|xga|hd|sxga|uxga"
#define BBS_CAM_SIZE          3       // xga
#define BBS_CAM_FLASH_PIN     4
#define BBS_CAM_FLASH         0
#define BBS_CAM_PWDN          32
#define BBS_CAM_RESET         -1
#define BBS_CAM_XCLK          0
#define BBS_CAM_SIOD          26
#define BBS_CAM_SIOC          27
#define BBS_CAM_D7            35      // Y9
#define BBS_CAM_D6            34      // Y8
#define BBS_CAM_D5            39      // Y7
#define BBS_CAM_D4            36      // Y6
#define BBS_CAM_D3            21      // Y5
#define BBS_CAM_D2            19      // Y4
#define BBS_CAM_D1            18      // Y3
#define BBS_CAM_D0            5       // Y2
#define BBS_CAM_VSYNC         25
#define BBS_CAM_HREF          23
#define BBS_CAM_PCLK          22

// Pins the board owns (syscfg::pinProblem refuses them with the reason).
// What is left for a sysop: 4 (the flash LED) and 33 (the red LED). The
// card's four are the sd plugin's settings, held by it as on the WROOM.
#define BBS_PINS_PSRAM        16, 17
#define BBS_PINS_CONSOLE      1, 3
#define BBS_PINS_CAMERA       BBS_CAM_PWDN, BBS_CAM_XCLK, BBS_CAM_SIOD, BBS_CAM_SIOC, BBS_CAM_D7, \
                              BBS_CAM_D6, BBS_CAM_D5, BBS_CAM_D4, BBS_CAM_D3, BBS_CAM_D2, \
                              BBS_CAM_D1, BBS_CAM_D0, BBS_CAM_VSYNC, BBS_CAM_HREF, BBS_CAM_PCLK
#define BBS_PINS_STRAP        12

#endif  // BBS_BOARD_AI_ESP32CAM

// ===========================================================================
// Waveshare ESP32-S3-ETH (ETH 1.0.0, 1.1.2)
//
// ESP32-S3R8 (read as revision v0.2 on the bench: 8 MB octal PSRAM in the
// package), a 16 MB W25Q128 beside it, native USB only (USB-C to GPIO 19
// and 20), a WIZnet W5500 10/100 Ethernet controller on SPI with an RJ45, a
// 24-pin DVP camera connector (an OV5640 on the bench's board, found at
// bring-up; the schematic's sheet says OV2640, and both work), a TF slot
// wired for SPI, one WS2812B, a BOOT button on GPIO 0, and a header for an
// optional PoE module (not fitted on the bench's). No display.
//
// Every pin is from Waveshare's schematic (ESP32-S3-ETH-Schematic.pdf,
// 2024-12-21, net names quoted in release-prep/wseth/pins.md) and checked
// against their wiki's tables and the factory image the board shipped with
// (an Arduino 2.0.11 build of their ESP32-S3-ETH-Camera demo, read off the
// board before the first flash).
//
// The camera's power is switched: GPIO 8 drives the gate of a P-channel
// MOSFET (Q3, APM2307) through 1 MΩ, with 10 MΩ pulling it up, so the
// camera's 2.8 V and 1.5 V regulators are off until GPIO 8 is driven low.
// That is exactly what esp32-camera's PWDN line does (high, then low, at
// every bring-up), so the pin is the camera's PWDN here. The OV2640's own
// PWDN is tied low on the board and its RESET is an RC on the board.
// ===========================================================================
#if defined(BBS_BOARD_WS_S3ETH)

#if defined(ESP_PLATFORM) && !CONFIG_IDF_TARGET_ESP32S3
#error "BBS_BOARD_WS_S3ETH is an ESP32-S3 board: build it for the esp32s3 target"
#endif
#if defined(BBS_BOARD_WS_S3LCD147) || defined(BBS_BOARD_FN_WROVER_CAM) || defined(BBS_BOARD_AI_ESP32CAM)
#error "one board profile at a time"
#endif
#ifndef BBS_CHIP_S3
#define BBS_CHIP_S3 1                 // the host's stand-in
#endif

#define BBS_BOARD_NAME        "Waveshare ESP32-S3-ETH"
#define BBS_BOARD_PLUGINS     1       // the camera

#define BBS_BOARD_TAG         "ETH"
#define BBS_BOARD_VERSION     "1.0.1"

// PSRAM (sdkconfig.defaults.esp32s3: octal, as on the S3 stick). A build
// that lost the layer would otherwise link quietly without it.
#define BBS_HAS_PSRAM         1
#if defined(ESP_PLATFORM) && !CONFIG_SPIRAM
#error "BBS_BOARD_WS_S3ETH needs PSRAM: sdkconfig.defaults.esp32s3 was not applied (delete sdkconfig.ws_s3eth*)"
#endif

// SSH as on the Waveshare S3 stick: the same chip, the same PSRAM.
#define BBS_HAS_SSH           1
#define BBS_SSH_MAX           8
// The internal heap a plugin may not take at start, as on the stick.
#define BBS_HEAP_RESERVE      16384

// Ethernet (1.1.2, this board only): the W5500 on SPI3, the primary
// interface, with Wi-Fi as the fallback (main.cpp, "Ethernet first").
//   BBS_ETH_SPI_HOST  the SPI host, SPI3: the card is on SPI2 (the sd
//                     plugin's SDSPI_HOST_DEFAULT), on pins of its own
//   BBS_ETH_MHZ       the SPI clock. The W5500's datasheet guarantees 33.3
//                     and IDF's own example runs 16 to 36; 20 is inside
//                     both, and a 10/100 link is the limit, not the bus
#if defined(ESP_PLATFORM) && !CONFIG_ETH_SPI_ETHERNET_W5500
#error "BBS_BOARD_WS_S3ETH needs the W5500 driver: sdkconfig.defaults.wseth was not applied (delete sdkconfig.ws_s3eth*)"
#endif
#define BBS_HAS_ETH           1
#define BBS_ETH_SPI_HOST      2       // SPI3_HOST
#define BBS_ETH_MHZ           20
#define BBS_ETH_MOSI          11
#define BBS_ETH_MISO          12
#define BBS_ETH_SCLK          13
#define BBS_ETH_CS            14
#define BBS_ETH_INT           10
#define BBS_ETH_RST           9

// No plain LED: the W5500 drives the RJ45's LINK and ACT lamps itself, and
// GPIO 2 (the reference board's LED) is the camera's HREF here.
#define BBS_LED_GPIO          -1

// The lights plugin on the board's WS2812B (RGB_DIN, GPIO 21 through R15),
// on from the first boot because the pixel is on the board. GRB, the part's
// own order, until LIGHTS TEST on the bench says otherwise.
#define BBS_LIGHTS_ON         1
#define BBS_LIGHTS_DRIVE_PIN  21

// The TF slot over SPI, the schematic's nets: SD_CS GPIO 4 (through R7),
// SD_MOSI 6 (the slot's CMD), SD_CLK 7, SD_MISO 5 (its D0), each pulled up
// with 10 kΩ. D1 and D2 are not wired, so SPI (or SDMMC one-bit) is all the
// slot can do. Its own pins: nothing shared with the W5500.
#define BBS_HAS_SD_SLOT       1
#define BBS_SD_CS             4
#define BBS_SD_MOSI           6
#define BBS_SD_CLK            7
#define BBS_SD_MISO           5

// The serial bridge on header GPIO 16 (RX) and 17 (TX), the reference
// board's own numbers, off until enabled. Not 43 and 44: UART0, where the
// ROM prints its banner at every reset. GPIO 16 is also the camera's VSYNC
// if R29 is fitted instead of R19 (it is not, on the boards Waveshare ship).

// The camera. The schematic's Y2 to Y9 are the driver's D0 to D7. Sold with
// an OV5640 (the wiki, the factory demo and the bench's board: PID 0x5640
// at 0x3C); an OV2640 on the same connector works too.
#define BBS_HAS_CAMERA        1
#define BBS_CAM_SENSOR        "OV5640"
#define BBS_CAM_SIZES         "qvga|vga|svga|xga|hd|sxga|uxga"
#define BBS_CAM_SIZE          3       // xga
#define BBS_CAM_FLASH_PIN     -1      // no flash LED on this board
#define BBS_CAM_FLASH         0
#define BBS_CAM_PWDN          8       // the camera's power switch, see above
#define BBS_CAM_PWDN_IS_POWER 1       // so camClose drives it high: the camera off between snaps
#define BBS_CAM_RESET         -1
#define BBS_CAM_XCLK          3
#define BBS_CAM_SIOD          48
#define BBS_CAM_SIOC          47
#define BBS_CAM_D7            18      // Y9
#define BBS_CAM_D6            15      // Y8
#define BBS_CAM_D5            38      // Y7
#define BBS_CAM_D4            40      // Y6
#define BBS_CAM_D3            42      // Y5
#define BBS_CAM_D2            46      // Y4
#define BBS_CAM_D1            45      // Y3
#define BBS_CAM_D0            41      // Y2
#define BBS_CAM_VSYNC         1       // through R19
#define BBS_CAM_HREF          2
#define BBS_CAM_PCLK          39

// Pins the board owns (syscfg::pinProblem refuses them with the reason).
// 26 to 37 (flash and octal PSRAM) and 19, 20 (USB) are the S3's rule. The
// card's four are the sd plugin's settings, held by it as on the WROOM. What
// is left for a sysop: 0 (BOOT), 16, 17, 21 (the pixel), 43 and 44. The
// core's two pin keys (activity_led_gpio, backup_button_gpio) are held to
// 0-39 on every chip, so 43 and 44 are for the plugins' pins only.
#define BBS_PINS_ETH          BBS_ETH_MOSI, BBS_ETH_MISO, BBS_ETH_SCLK, BBS_ETH_CS, \
                              BBS_ETH_INT, BBS_ETH_RST
#define BBS_PINS_CAMERA       BBS_CAM_PWDN, BBS_CAM_XCLK, BBS_CAM_SIOD, BBS_CAM_SIOC, BBS_CAM_D7, \
                              BBS_CAM_D6, BBS_CAM_D5, BBS_CAM_D4, BBS_CAM_D3, BBS_CAM_D2, \
                              BBS_CAM_D1, BBS_CAM_D0, BBS_CAM_VSYNC, BBS_CAM_HREF, BBS_CAM_PCLK

#endif  // BBS_BOARD_WS_S3ETH

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
#ifndef BBS_LED_ACTIVE_LOW
#define BBS_LED_ACTIVE_LOW    0       // the board's own LED lights on a high pin
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
