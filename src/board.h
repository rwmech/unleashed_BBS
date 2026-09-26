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
 *               BBS_BOARD_MF_S3PAR35  Makerfabs ESP32-S3 Parallel TFT with
 *                                      Touch 3.5", hardware v1.0: ESP32-S3-
 *                                      WROOM-1-N16R2, 16 MB flash, 2 MB quad
 *                                      PSRAM, a CP2104 and native USB, a TF
 *                                      slot and a 480 x 320 ILI9488 on a
 *                                      16-bit parallel bus. Pins from
 *                                      Makerfabs' schematic (its block below).
 *
 *               BBS_BOARD_MF_S3PAR35V2  the same board, hardware v2.0: an
 *                                      N16R8 (8 MB octal PSRAM) with the
 *                                      panel's strobes moved off 35 to 37.
 *                                      Not yet on the bench.
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
 *                 BBS_LCD_ILI9488   that panel is an ILI9488, not the default
 *                                   ST7789; BBS_LCD_RAM_SHORT/LONG its memory
 *                 BBS_LCD_I80       on a 16-bit i80 parallel bus
 *                                   (BBS_LCD_DATA_PINS), not SPI
 *                 BBS_PSRAM_QUAD    an S3 whose PSRAM is quad: 33 to 37 are
 *                                   free pins, not the PSRAM's
 *                 BBS_PINS_LCDBUS   the panel's parallel data bus
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
#define BBS_BOARD_VERSION     "1.1.2"

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
// Makerfabs ESP32-S3 Parallel TFT with Touch 3.5" (ILI9488), hardware v1.0
//
// ESP32-S3-WROOM-1-N16R2: 16 MB quad flash and 2 MB QUAD PSRAM in the chip's
// package (read on the bench, COM18: ESP32-S3 QFN56 v0.1, "Embedded PSRAM
// 2MB (AP_3v3)", flash c8/4018). Two USB-C: "USB-TTL", a CP2104 on UART0 (43,
// 44) with DTR/RTS auto-reset, which is the console, the flashing port and
// Improv's port; and "USB", the chip's own (19, 20). A 3.5" 480 x 320 ILI9488
// on a 16-bit i80 parallel bus, an FT6236 capacitive touch controller on I2C
// (38, 39, INT 40), a micro SD slot on SPI, FLASH and RST buttons, a green
// power LED wired to 3V3, and two Mabee (Grove) sockets: J1 on IO17 and IO18,
// J2 the I2C.
//
// Every pin is from Makerfabs' own schematic for this revision, "ESP32-S3
// Parallel TFT with Touch v1.0(3.5'' ili9488).sch" in github.com/Makerfabs/
// Makerfabs-ESP32-S3-Parallel-TFT-with-Touch (hardware/, commit d4c75c6),
// read as a netlist, and from the firmware Makerfabs shipped for it
// (firmware/SD16_3.5 in the same commit). The board says "v1.0" on its back.
//
// A trap in that schematic, and the reason for the second source: its module
// symbol is an ESP32-S2-SOLO's, so two nets carry the S2's pin names. The
// net "IO33/DB0" is the S3's IO47 and "IO34/LCD_RD" its IO48: Makerfabs'
// firmware drives D0 on 47 and RD on 48, and their v2.0 schematic renames
// exactly those two nets IO47/DB0 and IO48/LCD_RD. Every other net name is
// the S3's own.
//
// This revision runs WR, D/C and CS over IO35 to IO37, which are OCTAL
// PSRAM's pins. It works because the v1.0 carries the quad N16R2, whose PSRAM
// shares the flash's pins (26 to 32) and leaves 33 to 37 free
// (BBS_PSRAM_QUAD below). Makerfabs' v2.0 moved those three to 18, 17 and 46
// to carry an octal N16R8: another image, another profile, not this one.
//
// The card (1, 2, 41, 42) and the panel share no pin and no bus: the card is
// on SPI2, the panel on the LCD_CAM peripheral's i80 bus.
// ===========================================================================
#if defined(BBS_BOARD_MF_S3PAR35)

#if defined(ESP_PLATFORM) && !CONFIG_IDF_TARGET_ESP32S3
#error "BBS_BOARD_MF_S3PAR35 is an ESP32-S3 board: build it for the esp32s3 target"
#endif
#if defined(BBS_BOARD_WS_S3LCD147)
#error "one board profile at a time"
#endif
#ifndef BBS_CHIP_S3
#define BBS_CHIP_S3 1                 // the host's stand-in, see above
#endif

#define BBS_BOARD_NAME        "Makerfabs ESP32-S3 Parallel TFT 3.5\" (v1.0)"
#define BBS_HAS_LCD           1
#define BBS_BOARD_PLUGINS     1       // the panel

#define BBS_BOARD_TAG         "MF35"
#define BBS_BOARD_VERSION     "1.0.0"

// PSRAM (sdkconfig.defaults.mf35: quad, where the S3 layer says octal). A
// build that lost the layer would find no PSRAM, or with octal would drive
// the panel's bus pins as PSRAM.
#define BBS_HAS_PSRAM         1
#define BBS_PSRAM_QUAD        1       // pinProblem: 33 to 37 are free on this part
#if defined(ESP_PLATFORM) && !(CONFIG_SPIRAM && CONFIG_SPIRAM_MODE_QUAD)
#error "BBS_BOARD_MF_S3PAR35 needs quad PSRAM: sdkconfig.defaults.mf35 was not applied (delete sdkconfig.makerfabs_s3_par35*)"
#endif

// The Waveshare S3's reserve, for the same reason: Wi-Fi's and lwIP's
// buffers go to PSRAM (the S3 layer's TRY_ALLOCATE_WIFI_LWIP).
#define BBS_HEAP_RESERVE      16384

// No LED the firmware can drive: LED1 is not fitted and the power LED is on
// 3V3. The lights plugin ships off with no pin, as on the WROOM.
#define BBS_LED_GPIO          -1

// The card slot in SPI mode: CS IO1, MOSI IO2, SCLK IO42, MISO IO41 (the
// schematic's IO1/CS, IO2/MOSI, IO42/SCLK, IO41/MISO). DAT1 and DAT2 are
// pulled up on the board; there is no card-detect line.
#define BBS_HAS_SD_SLOT       1
#define BBS_SD_CS             1
#define BBS_SD_MOSI           2
#define BBS_SD_CLK            42
#define BBS_SD_MISO           41

// The serial bridge on J1, the Mabee GPIO socket: IO17 RX, IO18 TX, off until
// enabled. Both also feed the NC7WZ07 buffer to the speaker header (SPK),
// whose inputs load them no more than a probe, and IO18 has a 10K pull-up.
#define BBS_SERIAL_RX         17
#define BBS_SERIAL_TX         18

// The panel: ILI9488, 320 x 480 as the controller's memory runs, on a 16-bit
// i80 bus. D0 to D15 are IO47, 21, 14, 13, 12, 11, 10, 9, 3, 8, 16, 15, 7, 6,
// 5 and 4 (the schematic's DB0 to DB15, D0 read as IO47 above); WR IO35, RD
// IO48, D/C IO36 (LCD_RS), CS IO37. The panel's RESET is the chip's EN, so
// there is no reset pin: it is reset by command. RD is held high: nothing
// reads it.
//
// The backlight: IO45 through 1K to an AO3400's gate, pulled down 10K, so on
// when high, off from reset. IO45 is a strapping pin, and its pull-down is
// the level it wants: it is driven low from the start-up code
// (BBS_PINS_HOLD_LOW) until the panel's PWM takes it.
//
// Over 16-bit parallel the ILI9488 takes RGB565 (COLMOD 0x55), so the
// framebuffer goes to the glass as it is. 20 MHz is Makerfabs' own WR clock
// (their LovyanGFX config, freq_write 20000000).
//
// Upright portrait on this glass is MX set, as on the Waveshare's (the IDF
// ILI9488 component's default MADCTL, MX | BGR, and LovyanGFX's rotation 0):
// mirror yes. Landscape, 480 x 320, with the USB-C edge at the bottom, as
// shipped (Rob, on the glass, 2026-09-26).
//
// CONFIG panel's "USB plug" names where the USB-C edge is, as on the
// Waveshare, but this glass sits the other way round against its board, so
// each word is mapped to the panel's own turn (panel_gfx.h, Orient: up,
// left, right, down): plug up is its "left", plug down its "right", as seen
// on the bench. Plug left and right are the two portraits; which of them is
// which has not been seen yet.
#define BBS_LCD_ILI9488       1
#define BBS_LCD_I80           1       // the pins below: WR and RD, not MOSI and SCLK
#define BBS_LCD_DRIVER        "ILI9488"
#define BBS_LCD_RAM_SHORT     320
#define BBS_LCD_RAM_LONG      480
#define BBS_LCD_DATA_PINS     47, 21, 14, 13, 12, 11, 10, 9, 3, 8, 16, 15, 7, 6, 5, 4
#define BBS_LCD_MOSI          35      // WR
#define BBS_LCD_SCLK          48      // RD
#define BBS_LCD_CS            37
#define BBS_LCD_DC            36
#define BBS_LCD_RST           -1
#define BBS_LCD_BL            45
#define BBS_LCD_WIDTH         320
#define BBS_LCD_HEIGHT        480
#define BBS_LCD_XOFF          0
#define BBS_LCD_YOFF          0
#define BBS_LCD_ORIENT        3       // the USB plug down: landscape
#define BBS_LCD_PLUG_SCANS    1, 0, 3, 2   // the turn for plug up, left, right, down
#define BBS_LCD_INVERT        0
#define BBS_LCD_BGR           1
#define BBS_LCD_MIRROR        1
#define BBS_LCD_MHZ           20
#define BBS_LCD_MHZ_NOTE      "20 as Makerfabs run it; 10 if unsure."
#define BBS_LCD_BACKLIGHT     60

// Pins the board owns. 43 and 44 are UART0 to the CP2104. The panel's data
// bus is the board's (the six control pins are the panel plugin's settings
// and are held by it). 46 is a strapping pin, unconnected here.
#define BBS_PINS_CONSOLE      43, 44
#define BBS_PINS_LCDBUS       BBS_LCD_DATA_PINS
#define BBS_PINS_STRAP        46
#define BBS_PINS_HOLD_LOW     45

#endif  // BBS_BOARD_MF_S3PAR35

// ===========================================================================
// Makerfabs ESP32-S3 Parallel TFT with Touch 3.5" (ILI9488), hardware v2.0
//
// What Makerfabs sell now (product page, SKU ESP32S335D; wiki revision note):
// ESP32-S3-WROOM-1-N16R8, 16 MB flash and 8 MB OCTAL PSRAM, on the v1.0's
// board with the panel's three strobes moved off octal PSRAM's pins. Pins
// from Makerfabs' v2.0 schematic ("ESP32-S3 Parallel TFT with Touch 3.5''
// ili9488 v2.0.sch", commit 6e02066) checked against the firmware they ship
// for it (firmware/SD16_3.5 and example/touch_keyboard_v2 at the repository's
// head): D0 IO47, D1 to D15 as the v1.0, WR IO18, RD IO48, D/C IO17, CS IO46,
// the backlight on IO45. The card, touch, console, buttons and backlight
// circuit are the v1.0's. J1, the Mabee GPIO socket, moved onto IO19 and
// IO20, which are the chip's own USB: a sysop cannot have both.
//
// NOT YET ON THE BENCH (written 2026-09-26, the board ordered): built and
// host-tested only. The glass's turn against the board is taken to be the
// v1.0's, and nothing here has been seen on a v2.0 glass.
// ===========================================================================
#if defined(BBS_BOARD_MF_S3PAR35V2)

#if defined(ESP_PLATFORM) && !CONFIG_IDF_TARGET_ESP32S3
#error "BBS_BOARD_MF_S3PAR35V2 is an ESP32-S3 board: build it for the esp32s3 target"
#endif
#if defined(BBS_BOARD_WS_S3LCD147) || defined(BBS_BOARD_MF_S3PAR35)
#error "one board profile at a time"
#endif
#ifndef BBS_CHIP_S3
#define BBS_CHIP_S3 1                 // the host's stand-in, see above
#endif

#define BBS_BOARD_NAME        "Makerfabs ESP32-S3 Parallel TFT 3.5\" (v2.0)"
#define BBS_HAS_LCD           1
#define BBS_BOARD_PLUGINS     1       // the panel

#define BBS_BOARD_TAG         "MF35V2"
#define BBS_BOARD_VERSION     "1.0.0"

// PSRAM: the N16R8's 8 MB, octal, as the S3 layer already has it
// (sdkconfig.defaults.mf35v2 changes only the console). 33 to 37 are the
// PSRAM's, and pinProblem refuses them as on the Waveshare.
#define BBS_HAS_PSRAM         1
#if defined(ESP_PLATFORM) && !(CONFIG_SPIRAM && CONFIG_SPIRAM_MODE_OCT)
#error "BBS_BOARD_MF_S3PAR35V2 needs octal PSRAM: the S3 layer was not applied (delete sdkconfig.makerfabs_s3_par35v2*)"
#endif

#define BBS_HEAP_RESERVE      16384
#define BBS_LED_GPIO          -1

// The card slot, as on the v1.0.
#define BBS_HAS_SD_SLOT       1
#define BBS_SD_CS             1
#define BBS_SD_MOSI           2
#define BBS_SD_CLK            42
#define BBS_SD_MISO           41

// No serial bridge pins as shipped: the only GPIO socket, J1, is the chip's
// own USB (19, 20), which pinProblem refuses. A sysop gives the bridge pins
// in CONFIG serial, on J2's I2C lines if touch is not wanted.
#define BBS_SERIAL_RX         -1
#define BBS_SERIAL_TX         -1

// The panel, as the v1.0's but for the strobes: WR IO18, RD IO48, D/C IO17,
// CS IO46 (a strapping pin, which the panel drives only once it starts).
#define BBS_LCD_ILI9488       1
#define BBS_LCD_I80           1
#define BBS_LCD_DRIVER        "ILI9488"
#define BBS_LCD_RAM_SHORT     320
#define BBS_LCD_RAM_LONG      480
#define BBS_LCD_DATA_PINS     47, 21, 14, 13, 12, 11, 10, 9, 3, 8, 16, 15, 7, 6, 5, 4
#define BBS_LCD_MOSI          18      // WR
#define BBS_LCD_SCLK          48      // RD
#define BBS_LCD_CS            46
#define BBS_LCD_DC            17
#define BBS_LCD_RST           -1
#define BBS_LCD_BL            45
#define BBS_LCD_WIDTH         320
#define BBS_LCD_HEIGHT        480
#define BBS_LCD_XOFF          0
#define BBS_LCD_YOFF          0
#define BBS_LCD_ORIENT        3       // the USB plug down: landscape (the v1.0's, unseen here)
#define BBS_LCD_PLUG_SCANS    1, 0, 3, 2
#define BBS_LCD_INVERT        0
#define BBS_LCD_BGR           1
#define BBS_LCD_MIRROR        1
#define BBS_LCD_MHZ           20
#define BBS_LCD_MHZ_NOTE      "20 as Makerfabs run it; 10 if unsure."
#define BBS_LCD_BACKLIGHT     60

#define BBS_PINS_CONSOLE      43, 44
#define BBS_PINS_LCDBUS       BBS_LCD_DATA_PINS
#define BBS_PINS_HOLD_LOW     45

#endif  // BBS_BOARD_MF_S3PAR35V2

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
#if defined(BBS_BOARD_WS_S3LCD147) || defined(BBS_BOARD_MF_S3PAR35) || defined(BBS_BOARD_MF_S3PAR35V2)
#error "one board profile at a time"
#endif

#define BBS_BOARD_NAME        "Freenove ESP32-WROVER CAM"
#define BBS_BOARD_PLUGINS     1       // the camera

#define BBS_BOARD_TAG         "FNCAM"
#define BBS_BOARD_VERSION     "1.0.6"

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
#if defined(BBS_BOARD_WS_S3LCD147) || defined(BBS_BOARD_FN_WROVER_CAM) || defined(BBS_BOARD_MF_S3PAR35) || \
    defined(BBS_BOARD_MF_S3PAR35V2)
#error "one board profile at a time"
#endif

#define BBS_BOARD_NAME        "AI-Thinker ESP32-CAM"
#define BBS_BOARD_PLUGINS     1       // the camera

#define BBS_BOARD_TAG         "ESPCAM"
#define BBS_BOARD_VERSION     "1.0.3"

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
#ifdef BBS_HAS_LCD
#ifndef BBS_LCD_DRIVER
#define BBS_LCD_DRIVER        "ST7789"  // the panel's controller, unless a profile says
#define BBS_LCD_RAM_SHORT     240       // its memory, unturned: 240 x 320
#define BBS_LCD_RAM_LONG      320
#endif
#ifndef BBS_LCD_MHZ_NOTE                // CONFIG panel's note on the SPI clock, 38 at most
#define BBS_LCD_MHZ_NOTE      "10 is safe; the panel's limit is 62.5."
#endif
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
