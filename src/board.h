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
 *               BBS_BOARD_WS_S3TOUCH2  Waveshare ESP32-S3-Touch-LCD-2: the
 *                                      ESP32-S3R8 again (16 MB flash, 8 MB
 *                                      octal PSRAM), a 2" 240 x 320 ST7789T3
 *                                      with CST816D touch, a camera (OV5640
 *                                      as sold), a TF slot on the panel's
 *                                      SPI bus, a QMI8658 IMU and a Li-ion
 *                                      charger. The first board with a panel
 *                                      and a camera both.
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
 *                 BBS_SPI_SHARED    the panel and the card are two devices
 *                                   on one SPI bus (SPI2), raised once for
 *                                   both; a panel band is never waited for
 *                                   behind a card command
 *                 BBS_HAS_TOUCH     a touch controller on the glass, read
 *                                   as taps on its INT line (BBS_TOUCH_*)
 *                 BBS_HAS_CHIP_TEMP the chip's own temperature sensor, on
 *                                   the panel
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
// Waveshare ESP32-S3-Touch-LCD-2 (the 2-inch one, board lane board-ws2)
//
// ESP32-S3R8 (read as QFN56 revision v0.2, embedded PSRAM 8 MB AP_3v3 on
// the bench), 16 MB flash (W25Q128JVSI), native USB-C only. A 2" 240 x 320
// IPS on an ST7789T3 with a CST816D touch controller, a 24-pin camera
// connector (sold with an OV5640; the OV2640 fits too), a TF slot, a
// QMI8658 IMU, an ETA6098 Li-ion charger with its battery on a divider to
// GPIO 5. Every pin below is from Waveshare's schematic (its PinOut table
// and netlist, ESP32-S3-Touch-LCD-2-SchDoc.pdf, 2025-01-18) and was checked
// against Waveshare's own demo for this board (ESP32-S3-Touch-LCD-2-Demo:
// ESP-IDF 05_lvgl_camera, 01_sd_card_test, 04_lvgl_battery, 02_lvgl_qmi8658;
// Arduino 01_factory). The table and its sources: release-prep/ws2/pins.md.
//
// The panel and the card share one SPI bus: MOSI 38 and SCLK 39 go to both,
// MISO 40 to the card only. So the two are devices on SPI2 (BBS_SPI_SHARED,
// platform_esp32.cpp), not a bus each as on the LCD-1.47.
// ===========================================================================
#if defined(BBS_BOARD_WS_S3TOUCH2)

#if defined(ESP_PLATFORM) && !CONFIG_IDF_TARGET_ESP32S3
#error "BBS_BOARD_WS_S3TOUCH2 is an ESP32-S3 board: build it for the esp32s3 target"
#endif
#if defined(BBS_BOARD_WS_S3LCD147) || defined(BBS_BOARD_FN_WROVER_CAM) || defined(BBS_BOARD_AI_ESP32CAM)
#error "one board profile at a time"
#endif
#ifndef BBS_CHIP_S3
#define BBS_CHIP_S3 1                 // the host's stand-in
#endif

#define BBS_BOARD_NAME        "Waveshare ESP32-S3-Touch-LCD-2"
#define BBS_HAS_LCD           1
#define BBS_HAS_CAMERA        1
#define BBS_BOARD_PLUGINS     2       // the panel and the camera

// "WS2": Waveshare, 2 inch, beside the LCD-1.47's "S3" (which is older than
// the rule) and the 4.3B's "WS43B". Shown as 1.1.2 (WS2 1.0.0).
#define BBS_BOARD_TAG         "WS2"
#define BBS_BOARD_VERSION     "1.0.0"

// SSH as on the LCD-1.47: the same S3R8 and the same 8 MB of PSRAM.
#define BBS_HAS_SSH           1
#define BBS_SSH_MAX           8

// PSRAM (sdkconfig.defaults.esp32s3 and this board's own layer,
// sdkconfig.defaults.ws2): the camera needs it, and a build that lost the
// layer would otherwise link quietly without it.
#define BBS_HAS_PSRAM         1
#if defined(ESP_PLATFORM) && !CONFIG_SPIRAM
#error "BBS_BOARD_WS_S3TOUCH2 needs PSRAM: the S3 sdkconfig layer was not applied (delete sdkconfig.ws_s3touch2*)"
#endif
#if defined(ESP_PLATFORM) && !CONFIG_OV5640_SUPPORT
#error "BBS_BOARD_WS_S3TOUCH2 ships with an OV5640: sdkconfig.defaults.ws2 was not applied (delete sdkconfig.ws_s3touch2*)"
#endif

// The internal heap a plugin may not take at start: the LCD-1.47's 16 KB,
// for the same reason (Wi-Fi's and lwIP's buffers in PSRAM).
#define BBS_HEAP_RESERVE      16384

// No LED the firmware can drive: LED1 is the charger's (its STAT pin) and
// LED2 the power's. No WS2812 either. The lights plugin is on all the same,
// with no pin: the glass's row of LEDs is its strip, the only one this board
// will have (the panel spec: a rule over nothing otherwise). -1 drives
// nothing.
#define BBS_LED_GPIO          -1
#define BBS_LIGHTS_ON         1
// And its default effect switchboard (11 in lights::kStripFx), drawn as
// round lamps: Rob, on both new glasses, "meaningful": a lamp a line in the
// caller's rank colour while callers are on (nodes), and with nobody on dim
// steady lamps that flicker only with real traffic (no sweep). The 4.3B's
// effect, so the two boards match. The LCD-1.47 keeps nodes and its squares.
#define BBS_LIGHTS_STRIP_FX   11
#define BBS_PANEL_LED_ROUND   1

// The TF slot, SPI mode, on the panel's bus: CS 41 (the slot's CD/D3),
// MOSI 38, CLK 39, MISO 40.
#define BBS_HAS_SD_SLOT       1
#define BBS_SD_CS             41
#define BBS_SD_MOSI           38
#define BBS_SD_CLK            39
#define BBS_SD_MISO           40
#define BBS_SPI_SHARED        1

// The serial bridge: no pins as shipped. With the camera, the card, the
// panel, the touch and IMU bus and the battery sense wired, what is left is
// GPIO 18 alone: UART0's 43 and 44 are on the header too, but they are the
// console port CONFIG keeps free (and 43 carries the ROM's boot banner).
#define BBS_SERIAL_RX         -1
#define BBS_SERIAL_TX         -1

// The panel: ST7789T3, 240 x 320, the controller's whole RAM, portrait (no
// swap, no gap, RGB order, colours inverted: Waveshare's 05_lvgl_camera).
// Mirrored in X: the demo passes no mirror, but on the glass (Rob's photo,
// 2026-09-28, the first flash) every line read back to front without it,
// as on the LCD-1.47. Touch is read as taps only, so no coordinate needs
// the same flip. No reset line: the panel's RESET is on an
// RC, reached from IO0 only through a resistor the board leaves unfitted
// (R16, NC/0R), so it is reset by command. Backlight on IO1 through an
// SS8050, active high. The demo clocks the panel at 80 MHz; 40 is the
// fastest this firmware offers, and a 10 KB band is then about 2 ms on the
// wire, the longest the shared bus is ever held by the panel.
#define BBS_LCD_MOSI          38
#define BBS_LCD_SCLK          39
#define BBS_LCD_CS            45
#define BBS_LCD_DC            42
#define BBS_LCD_RST           -1
#define BBS_LCD_BL            1
#define BBS_LCD_WIDTH         240
#define BBS_LCD_HEIGHT        320
#define BBS_LCD_XOFF          0
#define BBS_LCD_YOFF          0
#define BBS_LCD_ORIENT        0       // "up" is the demo's portrait here; which edge the USB is on, the glass will say
#define BBS_LCD_INVERT        1
#define BBS_LCD_BGR           0
#define BBS_LCD_MIRROR        1       // the glass reads back to front without it
#define BBS_LCD_MHZ           40
#define BBS_LCD_BACKLIGHT     60      // percent

// The touch controller, CST816D, on the I2C bus it shares with the IMU:
// SDA 48, SCL 47, at 0x15; its INT on 46, its reset not wired. The panel
// reads taps from INT alone (an edge a touch), so nothing on the BBS loop
// ever waits on the bus. The IMU (QMI8658, 0x6B, INT1 on 3) and the battery
// divider (GPIO 5, ADC1 channel 4, a third of the cell) are wired and not
// yet used.
#define BBS_HAS_TOUCH         1
#define BBS_TOUCH_SDA         48
#define BBS_TOUCH_SCL         47
#define BBS_TOUCH_INT         46
#define BBS_TOUCH_ADDR        0x15

// The chip's own temperature sensor, on the panel's header rotation.
#define BBS_HAS_CHIP_TEMP     1

// The camera: Waveshare's pins (the demo's camera_pins block, and the
// schematic's CAM_* nets). PWDN 17, no RESET (the demo: "software reset
// will be performed"). The OV5640 gives JPEG itself, up to 2592 x 1944;
// CONFIG offers up to QXGA (2048 x 1536). XGA as shipped, as on the
// ESP32-CAM: a snap re-encodes for the watermark on the runner, and XGA
// keeps that to a few seconds. No flash on the board, and no flash pin as
// shipped: a switched-on camera holds its pin in CONFIG whatever the mode,
// so a default of 18 would take the one free GPIO. A sysop who wires a
// flash to 18 sets it in CONFIG camera.
#define BBS_CAM_SENSOR        "OV5640"
#define BBS_CAM_SIZES         "qvga|vga|svga|xga|hd|sxga|uxga|qxga"
#define BBS_CAM_SIZE          3       // xga
#define BBS_CAM_FLASH_PIN     -1
#define BBS_CAM_FLASH         0
#define BBS_CAM_PWDN          17
#define BBS_CAM_RESET         -1
#define BBS_CAM_XCLK          8
#define BBS_CAM_SIOD          21
#define BBS_CAM_SIOC          16
#define BBS_CAM_D7            2       // Y9
#define BBS_CAM_D6            7       // Y8
#define BBS_CAM_D5            10      // Y7
#define BBS_CAM_D4            14      // Y6
#define BBS_CAM_D3            11      // Y5
#define BBS_CAM_D2            15      // Y4
#define BBS_CAM_D1            13      // Y3
#define BBS_CAM_D0            12      // Y2
#define BBS_CAM_VSYNC         6
#define BBS_CAM_HREF          4
#define BBS_CAM_PCLK          9

// Pins the board owns (syscfg::pinProblem refuses them with the reason).
// The panel's six and the card's four are those plugins' settings, as on
// the LCD-1.47. 19 and 20 (the USB) and 26-37 (flash and octal PSRAM) the
// S3's own rule refuses already, and CONFIG keeps 43 and 44 as the console
// port. What is left for a sysop: GPIO 18.
//   CAMERA   every wired camera line, PWDN included
//   ONBOARD  the touch and IMU bus (47, 48), the touch INT (46), the IMU's
//            INT1 (3) and the battery divider (5)
#define BBS_PINS_CAMERA       BBS_CAM_PWDN, BBS_CAM_XCLK, BBS_CAM_SIOD, BBS_CAM_SIOC, BBS_CAM_D7, \
                              BBS_CAM_D6, BBS_CAM_D5, BBS_CAM_D4, BBS_CAM_D3, BBS_CAM_D2, \
                              BBS_CAM_D1, BBS_CAM_D0, BBS_CAM_VSYNC, BBS_CAM_HREF, BBS_CAM_PCLK
#define BBS_PINS_ONBOARD      BBS_TOUCH_SDA, BBS_TOUCH_SCL, BBS_TOUCH_INT, 3, 5

#endif  // BBS_BOARD_WS_S3TOUCH2

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
#ifndef BBS_LIGHTS_STRIP_FX
#define BBS_LIGHTS_STRIP_FX   0       // nodes, in lights::kStripFx's order
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
