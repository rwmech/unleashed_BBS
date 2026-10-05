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
 *               BBS_BOARD_WS_S3TOUCH43B  Waveshare ESP32-S3-Touch-LCD-4.3B:
 *                                      ESP32-S3-WROOM-1-N16R8, an 800 x 480
 *                                      RGB panel with GT911 touch, a CH422G
 *                                      expander, a TF slot, an RTC, RS485
 *                                      and CAN. Pins in its block below.
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
 *               BBS_BOARD_WS_S3ETH     Waveshare ESP32-S3-ETH: the ESP32-S3R8,
 *                                      16 MB flash, a W5500 Ethernet
 *                                      controller (Ethernet first, Wi-Fi the
 *                                      fallback), a camera connector, a TF
 *                                      slot and one WS2812B. No display.
 *
 *               BBS_BOARD_MF_S3PAR35   Makerfabs ESP32-S3 Parallel TFT with
 *                                      Touch 3.5", hardware v1.0: the
 *                                      N16R2 (2 MB quad PSRAM), a 480 x 320
 *                                      ILI9488 on a 16-bit i80 bus, a micro
 *                                      SD slot, the console on a CP2104.
 *
 *               BBS_BOARD_MF_S3PAR35V2 the same board, hardware v2.0: the
 *                                      N16R8 (8 MB octal PSRAM), the panel's
 *                                      strobes moved off the PSRAM's pins,
 *                                      FT6236 touch, the console on the
 *                                      chip's own USB.
 *
 *               BBS_BOARD_GT_4848S040  Guition ESP32-4848S040 (sold as
 *                                      AITRIP 4.0"): ESP32-S3-WROOM-1-N16R8,
 *                                      a 4" 480 x 480 ST7701S on the RGB bus
 *                                      with GT911 touch, a TF slot, the
 *                                      console on a CH340. Pins in its block.
 *
 *               Capabilities a profile may define:
 *                 BBS_HAS_LCD       a panel the panel plugin drives
 *                 BBS_LCD_RGB       that panel is on the S3's RGB bus, its
 *                                   frame buffer in PSRAM, not an SPI one
 *                 BBS_RGB_ST7701    that RGB panel is an ST7701 set up over
 *                                   3-wire SPI first, with the DMA streaming
 *                                   a framebuffer (platform_esp32_st7701.cpp),
 *                                   not the 4.3B's bounce fill
 *                 BBS_HAS_TOUCH     a touch controller on the glass: read
 *                                   as taps on its INT line (BBS_TOUCH_*),
 *                                   or polled over I2C with BBS_TOUCH_POLL
 *                 BBS_TOUCH_POLL    the controller is polled for reports
 *                                   (the 4.3B's GT911, touchPoll)
 *                 BBS_TOUCH_FT6236  the taps controller is a FocalTech
 *                                   FT6236, not the default CST816
 *                                   (BBS_TOUCH_CHIP its name)
 *                 BBS_HAS_CHIP_TEMP the chip's own temperature sensor, on
 *                                   the panel
 *                 BBS_SD_CS_EXPANDER the card's chip select is an expander
 *                                   pin held low, not a GPIO
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
 *                 BBS_SPI_SHARED    the panel and the card are two devices
 *                                   on one SPI bus (SPI2), raised once for
 *                                   both; a panel band is never waited for
 *                                   behind a card command
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
// The VFS table (1.2.1): 12 on every S3, from sdkconfig.defaults.esp32s3.
// With SSH and a card mounted an S3 fills the IDF's default 8, and a card
// that is the ninth user fails to mount as ESP_ERR_NO_MEM. A stale
// sdkconfig.<env> keeps 8.
#if CONFIG_IDF_TARGET_ESP32S3 && CONFIG_VFS_MAX_COUNT < 12
#error "an S3 build needs CONFIG_VFS_MAX_COUNT of 12 (sdkconfig.defaults.esp32s3): delete sdkconfig.<env> and build again"
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
#define BBS_BOARD_VERSION     "1.1.9"

// SSH (1.1.2 core, S3 1.1.3): encrypted logins on the board's own port,
// beside telnet (src/core/sshd.h). Ten from 1.2.2 (Rob: "we can also do
// a full 10 lines ssh right?"), so every one of the ten caller lines may be
// an SSH link; it was eight because a preview did not need more, which was a
// judgement rather than a limit. The arithmetic: ten at BBS_SSH_PSRAM_EACH
// (48 KB) plus BBS_SSH_PSRAM_KEEP (128 KB) is 608 KB of this board's 8 MB.
// A board with less PSRAM sets its own, lower, figure, and a sysop lowers
// any board's with `ssh_lines` (CONFIG network).
#define BBS_HAS_SSH           1
#define BBS_SSH_MAX           10

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
// Waveshare ESP32-S3-Touch-LCD-4.3B ("Development Board B", in its case)
//
// ESP32-S3-WROOM-1-N16R8 (read on the bench as ESP32-S3 QFN56 rev v0.2,
// 16 MB flash, 8 MB octal PSRAM), native USB only (303A:1001), a 4.3"
// 800 x 480 ST7262 panel on a 16-bit RGB bus, a GT911 touch controller, a
// CH422G I2C expander, a TF slot on SPI, a PCF85063A RTC, RS485, CAN and two
// isolated inputs and outputs. Every pin below is from Waveshare's own
// schematic (ESP32-S3-Touch-LCD-4.3B-Sch.pdf) cross-checked against their
// demos for this exact board (github.com/waveshareteam/ESP32-S3-Touch-LCD-
// 4.3B at 04cc7ee: waveshare_rgb_lcd_port.h, CH422G.h, 03_SD_Test, 04_RTC_
// Test); the table with a source for each pin is release-prep/ws43b/pins.md.
// The schematic, the wiki and the demos agree on every pin this profile
// drives. The one disagreement, the Arduino demos' "USB_SEL" on EXIO5, is
// copied from the plain 4.3 board: the B has no USB/CAN switch, and EXIO5 is
// its second isolated input.
//
// The board uses every GPIO the module brings out, so there is no activity
// LED, no pixel for the lights, and no BOOT button for the firmware: GPIO0
// is the BOOT key and the panel's G3 line at once, driven by the RGB bus
// while the panel runs. The BOOT-hold reset and the backup window's button
// are off, as on the ESP32-CAM.
//
// The CH422G has one direction bit for all eight of its IO pins, so reading
// the isolated inputs would let go of the backlight, both resets and the
// card's chip select together. The firmware keeps it an output port and does
// not read DI0 and DI1.
// ===========================================================================
#if defined(BBS_BOARD_WS_S3TOUCH43B)

#if defined(ESP_PLATFORM) && !CONFIG_IDF_TARGET_ESP32S3
#error "BBS_BOARD_WS_S3TOUCH43B is an ESP32-S3 board: build it for the esp32s3 target"
#endif
#if defined(BBS_BOARD_WS_S3LCD147)
#error "one board profile at a time"
#endif
#ifndef BBS_CHIP_S3
#define BBS_CHIP_S3 1                 // the host's stand-in, see above
#endif

#define BBS_BOARD_NAME        "Waveshare ESP32-S3-Touch-LCD-4.3B"
#define BBS_HAS_LCD           1
#define BBS_BOARD_PLUGINS     1       // the panel

#define BBS_BOARD_TAG         "WS43B"
#define BBS_BOARD_VERSION     "1.0.7"

// PSRAM (sdkconfig.defaults.ws43b over the S3 layer): the panel's frame
// buffer, and the program and its constants run from it (XIP), so a flash
// write does not stop the glass being fed. A build that lost the layer would
// otherwise link quietly without it.
#define BBS_HAS_PSRAM         1
#if defined(ESP_PLATFORM) && !(CONFIG_SPIRAM && CONFIG_SPIRAM_FETCH_INSTRUCTIONS && CONFIG_SPIRAM_RODATA)
#error "BBS_BOARD_WS_S3TOUCH43B needs PSRAM with XIP: sdkconfig.defaults.ws43b was not applied (delete sdkconfig.ws_s3touch43b*)"
#endif

// SSH as on the Waveshare stick: the same S3 image machinery, one define,
// and the same ten (1.2.2) in the same 8 MB. Ten at 48 KB each plus the
// 128 KB kept back is 608 KB; the panel's 400 x 240 picture is 192 KB and
// its RGB driver keeps no frame buffer of its own (flags.no_fb and the
// bounce buffers), so neither is near the 8 MB.
#define BBS_HAS_SSH           1
#define BBS_SSH_MAX           10

// The internal heap a plugin may not take at start, the stick's figure
// (Wi-Fi's and lwIP's buffers are in PSRAM on an S3 with it).
#define BBS_HEAP_RESERVE      16384

// Nothing free for an LED or a button (above).
#define BBS_LED_GPIO          -1
#define BBS_BOOT_GPIO         -1
#define BBS_BACKUP_GPIO       -1

// The lights plugin on, with no pin for either output: nothing is wired to
// light, but the panel draws the strip's effect as its row of square LEDs
// whether or not a strip is wired, and that row is part of the panel.
#define BBS_LIGHTS_ON         1
// And its default effect switchboard (11 in lights::kStripFx): a lamp a
// line, a caller's in rank colour and a free one dim steady blue that
// flickers with traffic, so the glass's light bar means something with
// callers and is alive without them (Rob, 2026-09-28).
#define BBS_LIGHTS_STRIP_FX   11

// The TF slot, SPI only (D1 and D2 go to pull-ups and nowhere else): MOSI 11,
// CLK 12, MISO 13, and chip select on the expander's EXIO4, held low for good
// as Waveshare's own SD demo holds it, the card being the only device on that
// bus. So the sd plugin's CS is "none" (-1) and the driver runs with no CS.
#define BBS_HAS_SD_SLOT       1
#define BBS_SD_CS_EXPANDER    1
#define BBS_SD_CS             -1
#define BBS_SD_MOSI           11
#define BBS_SD_CLK            12
#define BBS_SD_MISO           13

// The serial bridge on the RS485 transceiver (an SP3485 that turns itself
// round from the TX line, so there is no direction pin): RXD 43, TXD 44.
// Those are UART0's default pins, free here because the console is the
// chip's own USB. Off until a sysop enables it.
#define BBS_SERIAL_RX         43
#define BBS_SERIAL_TX         44

// The panel, an 800 x 480 ST7262 on the RGB bus: data in the order esp_lcd
// wants it (B3-B7, G2-G7, R3-R7), the clock, the syncs and DE. Timing as
// every Waveshare demo for this panel: 16 MHz, falling edge, H and V pulse
// 4, back porch 8, front porch 8, which is 39 frames a second.
//
// The panel plugin draws a 400 x 240 picture and the platform shows it at
// twice the size: the 8 x 16 text is 16 x 32 on the glass, about 3.7 mm
// tall, readable across a desk. BBS_LCD_WIDTH and HEIGHT are that picture.
// The picture is in PSRAM and goes to the panel through two bounce buffers
// in internal RAM, refilled from an interrupt with every pixel doubled
// (platform_esp32_rgb.cpp), so no 768 KB frame buffer exists at all. Four
// lines a buffer, not the demos' ten: 12.8 KB of internal RAM rather than
// 32, measured on the bench as the difference between a 11 KB and a 30 KB
// largest free block, for an interrupt every 205 us instead of 512.
#define BBS_LCD_RGB           1
#define BBS_LCD_DRIVER        "ST7262"
#define BBS_LCD_SCALE         2
#define BBS_LCD_PHYS_W        800
#define BBS_LCD_PHYS_H        480
#define BBS_RGB_DATA          14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40
#define BBS_RGB_PCLK          7
#define BBS_RGB_HSYNC         46
#define BBS_RGB_VSYNC         3
#define BBS_RGB_DE            5
#define BBS_RGB_PCLK_HZ       16000000
#define BBS_RGB_HPW           4
#define BBS_RGB_HBP           8
#define BBS_RGB_HFP           8
#define BBS_RGB_VPW           4
#define BBS_RGB_VBP           8
#define BBS_RGB_VFP           8
#define BBS_RGB_BOUNCE_LINES  4
#define BBS_LCD_WIDTH         400
#define BBS_LCD_HEIGHT        240
#define BBS_LCD_LIST_MAX      12      // two columns of six at 400 x 240
// The panel plugin's settings that an RGB panel has no use for, as the
// plugin's defaults() reads them.
#define BBS_LCD_MOSI          -1
#define BBS_LCD_SCLK          -1
#define BBS_LCD_CS            -1
#define BBS_LCD_DC            -1
#define BBS_LCD_RST           -1
#define BBS_LCD_BL            -1
#define BBS_LCD_XOFF          0
#define BBS_LCD_YOFF          0
#define BBS_LCD_ORIENT        0
#define BBS_LCD_INVERT        0
#define BBS_LCD_BGR           0
#define BBS_LCD_MIRROR        0
#define BBS_LCD_MHZ           10
#define BBS_LCD_BACKLIGHT     100     // on or off only: the boost's enable is an expander pin

// The I2C bus: the touch controller, the expander and the RTC. 400 kHz, as
// the demos run it, with 4.7K pull-ups on the board.
#define BBS_I2C_SDA           8
#define BBS_I2C_SCL           9

// The GT911 touch controller at 0x5D: INT on GPIO4, reset on EXIO1. INT is
// held low while reset rises, which is what picks 0x5D, and then let go
// (Waveshare's Arduino sequence; their IDF demo leaves it driven low).
#define BBS_HAS_TOUCH         1
#define BBS_TOUCH_POLL        1       // polled over I2C (touchPoll), not taps on INT
#define BBS_TOUCH_INT         4
#define BBS_TOUCH_ADDR        0x5D

// The CH422G's IO port, one bit a pin (EXIOn is bit n): touch reset EXIO1,
// the backlight's boost and the panel's DISP EXIO2, the panel's reset
// EXIO3, the card's chip select EXIO4. EXIO0 and EXIO5 are the isolated
// inputs' collectors and are written low, as the demos write them: high
// would fight the opto-coupler when an input is on.
#define BBS_EX_TP_RST         0x02
#define BBS_EX_BACKLIGHT      0x04
#define BBS_EX_LCD_RST        0x08
#define BBS_EX_SD_CS          0x10

// The S3's own temperature sensor, shown on the panel's system row. It
// measures the die, not the room: a figure to watch for a board in a closed
// case, not a thermometer.
#define BBS_HAS_CHIP_TEMP     1

// Pins the board owns (syscfg::pinProblem refuses them with the reason).
//   LCD      the RGB bus, the touch controller's INT and the I2C pair (GPIO0,
//            the BOOT key, is the bus's G3)
//   WIRED    the RTC's interrupt (6) and the CAN transceiver (15, 16)
// The card's three lines and RS485's two are the sd plugin's and the serial
// bridge's settings, held by them, as on the other boards.
#define BBS_PINS_LCD          BBS_RGB_DATA, BBS_RGB_PCLK, BBS_RGB_HSYNC, BBS_RGB_VSYNC, BBS_RGB_DE, \
                              BBS_TOUCH_INT, BBS_I2C_SDA, BBS_I2C_SCL
#define BBS_PINS_WIRED        6, 15, 16

#endif  // BBS_BOARD_WS_S3TOUCH43B

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

// 35 characters: HARDWARE's Board row puts it under the version at 40
// columns, where 44 ("Makerfabs ESP32-S3 Parallel TFT 3.5" (v1.0)") wrapped.
#define BBS_BOARD_NAME        "Makerfabs S3 Parallel TFT 3.5\" v1.0"
#define BBS_HAS_LCD           1
#define BBS_BOARD_PLUGINS     1       // the panel

#define BBS_BOARD_TAG         "MF35"
#define BBS_BOARD_VERSION     "1.1.7"     // ten SSH lines (1.2.2)

// SSH (1.1.2 core, MF35 1.1.0), as on the Waveshare S3: the shared port 6400
// and ssh_port 6422, host keys in userdata/ssh. Ten from 1.2.2, and this
// is the board the figure had to be checked on, since its 2 MB of quad PSRAM
// is the least of any SSH profile. The arithmetic: the panel's framebuffer is
// 480 x 320 x 2 = 300 KB, and the 1.1.1 bench read 1.71 MB of PSRAM free with
// it up; ten sessions at their 48 KB budget (BBS_SSH_PSRAM_EACH) plus the
// 128 KB kept back (BBS_SSH_PSRAM_KEEP) is 608 KB, which leaves about 1.1 MB
// of that measured 1.71 MB. sshd::cap() still lowers the figure live if PSRAM
// is short at a connect, so the floor is a refused connection, never a board
// out of memory. The limit that bites first is internal RAM (the SSH task's
// 16 KB stack and lwIP's per-socket buffers), not PSRAM: read the internal
// heap's low on the bench with the lines full.
#define BBS_HAS_SSH           1
#define BBS_SSH_MAX           10

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
// and are held by it). 46 is a strapping pin, unconnected here. 38, 39 and
// 40 are the FT6236's I2C pair and INT on the glass's flex (MF35 1.1.3): not
// driven by this profile, but wired, so a pin handed out there (the serial
// bridge's TX on 40) would fight the controller.
#define BBS_PINS_CONSOLE      43, 44
#define BBS_PINS_ONBOARD      38, 39, 40
#define BBS_PINS_LCDBUS       BBS_LCD_DATA_PINS
#define BBS_PINS_STRAP        46
#define BBS_PINS_HOLD_LOW     45

#endif  // BBS_BOARD_MF_S3PAR35

// ===========================================================================
// Makerfabs ESP32-S3 Parallel TFT with Touch 3.5" (ILI9488), hardware v2.0
//
// What Makerfabs sell now (SKU ESP32S335D). The bench board (COM29, MF35V2
// 1.0.0 on 1.2.0) says "ESP32-S3 Parallel TFT with Touch 3.5" ili9488 v2.0"
// on its silkscreen and "MCN16R8" on the module, and esptool reads ESP32-S3
// QFN56 rev v0.2, 8 MB embedded PSRAM (AP_3v3), 16 MB flash (46/4018).
// ESP32-S3-WROOM-1-N16R8: the v1.0's board with the panel's three strobes
// moved off octal PSRAM's pins (33 to 37). Two USB-C: "USB-TTL", a CP2104
// (IC1) on UART0 (43, 44) with DTR/RTS auto-reset, and "USB-NATIVE", the
// chip's own (19, 20).
//
// Every pin is from Makerfabs' v2.0 schematic, "ESP32-S3 Parallel TFT with
// Touch 3.5'' ili9488 v2.0.sch" (github.com/Makerfabs/Makerfabs-ESP32-S3-
// Parallel-TFT-with-Touch, hardware/, at 7670a17, read as a netlist),
// cross-checked against the firmware Makerfabs ship for it at the same
// commit: firmware/SD16_3.5, example/touch_keyboard_v2 (Parallel16_9488.h)
// and IDF/matouch's 3-5-ili9488-ft6236 board config. They agree on every
// pin this profile drives. The table, each pin with its source, is
// release-prep/mf35v2/pins.md.
//
// The v1.0's schematic trap is still in this one: the module symbol is an
// ESP32-S2-SOLO's, and the nets "IO47/DB0" and "IO48/LCD_RD" land on its
// pins named IO33 and IO34. Every Makerfabs firmware drives D0 on 47 and RD
// on 48, and the net names say so; the symbol's pin names are the S2's.
//
// The console is the chip's own USB (USB-Serial-JTAG), the S3 layer's
// default, not the v1.0's UART0: the bench board is cabled to USB-NATIVE,
// and a console on UART0 leaves the native port output-only, so Improv and
// the web installer's Update would not answer there. The USB-TTL port still
// flashes (the ROM's own UART download, auto-reset) but carries no console.
//
// Touch: an FT6236 capacitive controller on the glass's flex, connector P2
// (1 VDD, 2 SDA, 3 SCL, 4 INT, 5 RST, 6 GND): SDA IO38, SCL IO39, INT IO40,
// each with a 10K pull-up on the board, RST on the chip's EN. Address 0x38
// (Makerfabs' FT6236.h). The same I2C pair goes to J2, the Mabee I2C
// socket. An NS2009 resistive controller's footprint (U8) shares the bus
// and INT for the resistive variant; this board is the capacitive one.
// ===========================================================================
#if defined(BBS_BOARD_MF_S3PAR35V2)

#if defined(ESP_PLATFORM) && !CONFIG_IDF_TARGET_ESP32S3
#error "BBS_BOARD_MF_S3PAR35V2 is an ESP32-S3 board: build it for the esp32s3 target"
#endif
#ifndef BBS_CHIP_S3
#define BBS_CHIP_S3 1                 // the host's stand-in, see above
#endif

// 35 characters, as the v1.0's: HARDWARE's Board row at 40 columns.
#define BBS_BOARD_NAME        "Makerfabs S3 Parallel TFT 3.5\" v2.0"
#define BBS_HAS_LCD           1
#define BBS_BOARD_PLUGINS     1       // the panel

#define BBS_BOARD_TAG         "MF35V2"
#define BBS_BOARD_VERSION     "1.0.4"     // ten SSH lines (1.2.2)

// SSH as on the v1.0 and the Waveshare: the shared port 6400 and ssh_port
// 6422. Ten from 1.2.2; 608 KB (ten at 48 KB plus the 128 KB kept back)
// beside the 300 KB framebuffer is under a tenth of this board's 8 MB, and
// the v1.0 fits the same ten in 2 MB.
#define BBS_HAS_SSH           1
#define BBS_SSH_MAX           10

// PSRAM: the N16R8's 8 MB, octal, as the S3 layer has it. 33 to 37 are the
// PSRAM's here, and pinProblem refuses them as on the Waveshare. A build
// that picked up the v1.0's quad layer would find no PSRAM.
#define BBS_HAS_PSRAM         1
#if defined(ESP_PLATFORM) && !(CONFIG_SPIRAM && CONFIG_SPIRAM_MODE_OCT)
#error "BBS_BOARD_MF_S3PAR35V2 needs octal PSRAM: the S3 layer was not applied (delete sdkconfig.makerfabs_s3_par35v2*)"
#endif
// The VFS table: 12, the S3 layer's for every S3, and the one guard near the
// top of this file refuses a stale sdkconfig that keeps 8 (1.2.1; the lane's
// own layer line and guard folded into it at the dev.9 merge).
// The console on the chip's own USB (the S3 layer's, below). A stale
// sdkconfig with the v1.0's UART0 console builds cleanly and then Improv never
// answers on the USB-NATIVE port.
#if defined(ESP_PLATFORM) && !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#error "BBS_BOARD_MF_S3PAR35V2 needs the console on USB-Serial-JTAG (the S3 layer): delete sdkconfig.makerfabs_s3_par35v2*"
#endif

// The Waveshare S3's reserve: Wi-Fi's and lwIP's buffers go to PSRAM.
#define BBS_HEAP_RESERVE      16384

// No LED the firmware can drive: LED1 is not fitted (its transistor's gate
// is on TXD through a resistor that is not fitted either) and the power LED
// is on 3V3. The lights plugin ships off with no pin, as on the v1.0.
#define BBS_LED_GPIO          -1

// The card slot in SPI mode, as on the v1.0: CS IO1, MOSI IO2, SCLK IO42,
// MISO IO41 (the schematic's IO1/CS, IO2/MOSI, IO42/SCLK, IO41/MISO; DAT1
// and DAT2 pulled up, no card-detect line).
#define BBS_HAS_SD_SLOT       1
#define BBS_SD_CS             1
#define BBS_SD_MOSI           2
#define BBS_SD_CLK            42
#define BBS_SD_MISO           41

// No serial bridge pins as shipped. J1, the Mabee GPIO socket, is IO19 and
// IO20, the chip's own USB D- and D+ (the same nets, through 33R, as the
// USB-NATIVE port), which is this board's console: pinProblem refuses both.
// The CP2104's UART0 (43, 44) is refused below. A sysop gives the bridge its
// pins in CONFIG serial.
#define BBS_SERIAL_RX         -1
#define BBS_SERIAL_TX         -1

// The panel, as the v1.0's but for the strobes: WR IO18 (the net IO18/
// LCD_WR, 10K pull-up), RD IO48, D/C IO17 (IO17/LCD_RS), CS IO46 (IO46/
// LCD_CS, 10K pull-down: a strapping pin, and low is what it wants at
// boot). D0 to D15 are the v1.0's, and so are the backlight (IO45 through
// 1K to an AO3400, 10K pull-down) and the reset on EN. IM1 is strapped to
// 3V3 through a 0R: the 16-bit bus. 20 MHz is Makerfabs' own WR clock
// (Parallel16_9488.h, freq_write 20000000).
//
// The glass's turn is taken to be the v1.0's until it is seen: landscape,
// 480 x 320, with the USB-C edge at the bottom (Rob's choice for this board,
// 2026-09-30: "I prefer the display with the USB pointing down").
#define BBS_LCD_ILI9488       1
#define BBS_LCD_I80           1       // the pins below: WR and RD, not MOSI and SCLK
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
#define BBS_LCD_ORIENT        3       // the USB plug down: landscape
#define BBS_LCD_PLUG_SCANS    1, 0, 3, 2   // the v1.0's turns for plug up, left, right, down
#define BBS_LCD_INVERT        0
#define BBS_LCD_BGR           1
#define BBS_LCD_MIRROR        1
#define BBS_LCD_MHZ           20
#define BBS_LCD_MHZ_NOTE      "20 as Makerfabs run it; 10 if unsure."
#define BBS_LCD_BACKLIGHT     60

// Touch as taps on INT (the legacy I2C driver, asked once at the panel's
// start, as the Touch-LCD-2's CST816). Nothing else in this image uses I2C.
// Taps carry no position, so the panel's turn does not reach them. The
// FT6236's INT is low while a finger is down in its default mode, so one
// tap is one falling edge; its chip ID is at 0xA3.
#define BBS_HAS_TOUCH         1
#define BBS_TOUCH_FT6236      1
#define BBS_TOUCH_CHIP        "FT6236"
#define BBS_TOUCH_SDA         38
#define BBS_TOUCH_SCL         39
#define BBS_TOUCH_INT         40
#define BBS_TOUCH_ADDR        0x38

// Pins the board owns.
//   WIRED    43 and 44, UART0 to the CP2104: its TXD drives 44 whenever the
//            USB-TTL port is powered, and the ROM prints on 43 at reset
//   ONBOARD  the touch controller's I2C pair and INT
//   LCDBUS   the panel's data bus (its six control pins are the panel
//            plugin's settings and are held by it)
// 45 (the backlight) is held low from start-up until the panel's PWM takes
// it. 46 is the panel's CS, a setting held by the panel plugin.
#define BBS_PINS_WIRED        43, 44
#define BBS_PINS_ONBOARD      BBS_TOUCH_SDA, BBS_TOUCH_SCL, BBS_TOUCH_INT
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
#if defined(BBS_BOARD_WS_S3LCD147) || defined(BBS_BOARD_WS_S3TOUCH43B) || defined(BBS_BOARD_MF_S3PAR35)
#error "one board profile at a time"
#endif

#define BBS_BOARD_NAME        "Freenove ESP32-WROVER CAM"
#define BBS_BOARD_PLUGINS     1       // the camera

#define BBS_BOARD_TAG         "FNCAM"
#define BBS_BOARD_VERSION     "1.0.10"

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
#if defined(BBS_BOARD_WS_S3LCD147) || defined(BBS_BOARD_FN_WROVER_CAM) || defined(BBS_BOARD_WS_S3TOUCH43B) || defined(BBS_BOARD_MF_S3PAR35)
#error "one board profile at a time"
#endif

#define BBS_BOARD_NAME        "AI-Thinker ESP32-CAM"
#define BBS_BOARD_PLUGINS     1       // the camera

#define BBS_BOARD_TAG         "ESPCAM"
#define BBS_BOARD_VERSION     "1.0.7"

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
// the rule) and the 4.3B's "WS43B". Shown as 1.1.2-hw.1 (WS2 1.0.2).
#define BBS_BOARD_TAG         "WS2"
#define BBS_BOARD_VERSION     "1.0.8"

// SSH as on the LCD-1.47: the same S3R8 and the same 8 MB of PSRAM, so the
// same ten from 1.2.2 (608 KB). This board's camera wants PSRAM too, and the
// two never collide at a fixed figure: sshd::cap() weighs what is free at
// each connect, and a snap's buffers are weighed the same way.
#define BBS_HAS_SSH           1
#define BBS_SSH_MAX           10

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
// GPIO 18 alone. UART0's 43 and 44 are on the header too; the console is
// the chip's own USB, so CONFIG takes them since 1.2.1 (BBS_CONSOLE_UART0),
// but 43 carries the ROM's boot banner at every reset: not for a LED or a
// relay.
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
#define BBS_PANEL_TEMP_WARM   65      // warm from 65 C: the S3R8 is rated to 65 C ambient

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
// S3's own rule refuses already. What is left for a sysop: GPIO 18, and
// UART0's 43 and 44 on the header since 1.2.1 (the console is USB), 43 with
// the ROM's boot banner on it at every reset.
//   CAMERA   every wired camera line, PWDN included
//   ONBOARD  the touch and IMU bus (47, 48), the touch INT (46), the IMU's
//            INT1 (3) and the battery divider (5)
#define BBS_PINS_CAMERA       BBS_CAM_PWDN, BBS_CAM_XCLK, BBS_CAM_SIOD, BBS_CAM_SIOC, BBS_CAM_D7, \
                              BBS_CAM_D6, BBS_CAM_D5, BBS_CAM_D4, BBS_CAM_D3, BBS_CAM_D2, \
                              BBS_CAM_D1, BBS_CAM_D0, BBS_CAM_VSYNC, BBS_CAM_HREF, BBS_CAM_PCLK
#define BBS_PINS_ONBOARD      BBS_TOUCH_SDA, BBS_TOUCH_SCL, BBS_TOUCH_INT, 3, 5

#endif  // BBS_BOARD_WS_S3TOUCH2

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
#define BBS_BOARD_VERSION     "1.0.8"

// PSRAM (sdkconfig.defaults.esp32s3: octal, as on the S3 stick). A build
// that lost the layer would otherwise link quietly without it.
#define BBS_HAS_PSRAM         1
#if defined(ESP_PLATFORM) && !CONFIG_SPIRAM
#error "BBS_BOARD_WS_S3ETH needs PSRAM: sdkconfig.defaults.esp32s3 was not applied (delete sdkconfig.ws_s3eth*)"
#endif

// SSH as on the Waveshare S3 stick: the same chip, the same PSRAM, the same
// ten from 1.2.2 (608 KB of 8 MB). This is the board most likely to see them
// all: it is the one on a wire, where "most stable connection" and encrypted
// logins are the pair a sysop exposes.
#define BBS_HAS_SSH           1
#define BBS_SSH_MAX           10
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
// Guition ESP32-4848S040 (the 4" 480 x 480 "86 box" panel; the bench board is
// sold as "AITRIP ESP32-S3 4.0 inch Color LCD Display Development Board")
//
// Read on the bench (COM30): ESP32-S3 QFN56 rev v0.2, 8 MB embedded PSRAM
// (AP_3v3), 16 MB flash (68/4018), a CH340 (1A86:7523) on UART0. That is the
// ESP32-S3-WROOM-1-N16R8, its PSRAM octal. The USB-C port goes to the CH340
// only: the chip's own USB pins are the touch controller's SDA (19) and the
// panel's G1 (20), so the console is UART0 and nothing is on USB-Serial-JTAG.
//
// A 4" 480 x 480 ST7701S IPS panel on the 16-bit RGB bus, set up first over
// a 3-wire SPI (9-bit words: a D/C bit then 8) that shares its clock and data
// with the TF slot; a GT911 capacitive touch controller on I2C with no INT or
// reset line; the backlight on GPIO 38 through a transistor (PWM); a micro SD
// slot on SPI. GPIO 40, 2 and 1 leave the front board as L1, L2 and L3
// (through 0R links R25 to R27) on the 2x4 header that joins it to the rear
// board, where the relay variant (ESP32-4848S040C_I_Y_3) has its three
// relays. The bench board has no relays (Rob, from its back, 2026-09-30): the
// rear plate carries that header, a serial connector (UART0), the battery's
// and the speaker's (the NS4168's output). Moving the links to R21 to R23
// gives the three to the NS4168's I2S inputs instead; as shipped they are not.
//
// Every pin is from two sources that agree: Espressif's ESP32_Display_Panel
// library's board header for this design (supported/jingcai/BOARD_JINGCAI_
// ESP32_4848S040C_I_Y_3.h; Jingcai is Guition's maker) and the vendor's own
// pin table (as transcribed in github.com/NorthernMan54/ESP32-4848S040),
// with the card's four from the same table and homeding's page. The
// firmware this board shipped with is that same library: its ST7701 init
// table, decoded from the factory image, is the header's byte for byte, and
// its UI toggles 40, 1 and 2. The table with every source is
// release-prep/g4848/pins.md.
//
// The glass is square and drawn whole: the panel plugin's square layout
// (BBS_PANEL_SQUARE, internal/tty-ux-panel-g4848-2026-10-01.md), the node
// board across the full width and the Makerfabs' right column under it. A
// 480 x 320 skin is shown framed, 80 rows down.
// ===========================================================================
#if defined(BBS_BOARD_GT_4848S040)

#if defined(ESP_PLATFORM) && !CONFIG_IDF_TARGET_ESP32S3
#error "BBS_BOARD_GT_4848S040 is an ESP32-S3 board: build it for the esp32s3 target"
#endif
#ifndef BBS_CHIP_S3
#define BBS_CHIP_S3 1                 // the host's stand-in, see above
#endif

// 33 characters, under HARDWARE's 40-column limit for the Board row.
#define BBS_BOARD_NAME        "Guition ESP32-4848S040 4\" 480x480"
#define BBS_HAS_LCD           1
#define BBS_BOARD_PLUGINS     1       // the panel

#define BBS_BOARD_TAG         "G4848"
// 1.0.3 stood on both sides of the dev.15 merge for different code (the
// integration's own, and the lane's gradient), so that build took 1.0.4: a
// version that does not identify a build is worse than none. 1.0.5 is the
// ten SSH lines of 1.2.2.
#define BBS_BOARD_VERSION     "1.0.5"

// SSH as on every S3 board: the shared port 6400 and ssh_port 6422. Ten
// from 1.2.2: 608 KB (ten at 48 KB plus the 128 KB kept back) beside this
// board's 450 KB framebuffer and a photo's buffers, in 8 MB of PSRAM.
#define BBS_HAS_SSH           1
#define BBS_SSH_MAX           10

// PSRAM (the S3 layer's octal, and sdkconfig.defaults.g4848's XIP): the
// panel's framebuffer, which the RGB DMA reads 45 times a second, and the
// program itself, so a flash write does not turn the cache off under it.
#define BBS_HAS_PSRAM         1
#if defined(ESP_PLATFORM) && !(CONFIG_SPIRAM && CONFIG_SPIRAM_MODE_OCT && CONFIG_SPIRAM_FETCH_INSTRUCTIONS && \
                               CONFIG_SPIRAM_RODATA)
#error "BBS_BOARD_GT_4848S040 needs octal PSRAM with XIP: sdkconfig.defaults.g4848 was not applied (delete sdkconfig.guition_4848s040*)"
#endif
// SSH and a card fill the IDF's 8 VFS slots with no headroom (the Makerfabs'
// review): 12, as on every S3 display board. A stale sdkconfig keeps 8.
#if defined(ESP_PLATFORM) && CONFIG_VFS_MAX_COUNT < 12
#error "BBS_BOARD_GT_4848S040 needs CONFIG_VFS_MAX_COUNT of 12 (sdkconfig.defaults.g4848): delete sdkconfig.guition_4848s040*"
#endif
// The console on UART0 (the CH340), and no USB-Serial-JTAG console of any
// kind: its pads are the touch controller's and the panel's here. The S3
// layer's is USB-Serial-JTAG, so a build that lost this board's layer would
// put the console, and Improv, on a port that is not wired.
#if defined(ESP_PLATFORM) && !(CONFIG_ESP_CONSOLE_UART_DEFAULT && CONFIG_ESP_CONSOLE_SECONDARY_NONE)
#error "BBS_BOARD_GT_4848S040 needs the console on UART0 and no secondary (sdkconfig.defaults.g4848): delete sdkconfig.guition_4848s040*"
#endif

// The internal heap a plugin may not take at start, the S3 boards' figure.
#define BBS_HEAP_RESERVE      16384

// No LED the firmware can drive, and GPIO0, the BOOT key, is the panel's R4
// line: no BOOT-hold reset and no backup-window button, as on the 4.3B.
#define BBS_LED_GPIO          -1
#define BBS_BOOT_GPIO         -1
#define BBS_BACKUP_GPIO       -1

// The lights plugin on with no pin, as on the 4.3B: nothing is wired to
// light, but the panel draws the strip's effect in its LED row. switchboard.
#define BBS_LIGHTS_ON         1
#define BBS_LIGHTS_STRIP_FX   11

// The TF slot in SPI mode: CS 42, MOSI 47, CLK 48, MISO 41. MOSI and CLK are
// also the ST7701's 3-wire SPI (SDA and SCK), whose CS is 39: held high from
// start-up so the card's traffic never reaches the panel. The panel's setup
// is bit-banged on 39, 48 and 47 as the factory firmware does it, with the
// card's bus held and its two pins lent for the length of it
// (platform_esp32_st7701.cpp).
#define BBS_HAS_SD_SLOT       1
#define BBS_SD_CS             42
#define BBS_SD_MOSI           47
#define BBS_SD_CLK            48
#define BBS_SD_MISO           41
// The card's pins the panel's setup drives (CS high through it, MOSI and CLK
// as the 3-wire link): CONFIG gives them to no page but CONFIG sd's, whether
// or not sd is on (pinTaken, bbs_sysop.cpp), since sd off holds nothing and
// the panel's next start would take them from whatever was given them.
#define BBS_PINS_PANEL_CARD   BBS_SD_CS, BBS_SD_MOSI, BBS_SD_CLK

// No serial bridge pins as shipped: 43 and 44 are the console (the rear
// plate's serial connector is the same UART0), and which of the 2x4 header's
// pins carry 1, 2 and 40 is not in any published pinout found. A sysop who
// has traced them gives the bridge its pins in CONFIG serial.
#define BBS_SERIAL_RX         -1
#define BBS_SERIAL_TX         -1

// The panel: 480 x 480 on the RGB bus, data in the order esp_lcd wants it
// (B0-B4, G0-G5, R0-R4), the clock, the syncs and DE. The vendor's timing
// is 26 MHz, rising edge, H pulse 10 back 10 front 20, V pulse 10 back 10
// front 10, from a framebuffer through bounce buffers. Here the DMA streams
// the framebuffer from PSRAM itself, no bounce buffers, so the refresh costs
// the CPU nothing (Rule no. 1); 12 MHz keeps that stream to 23 MB/s, 45 frames
// a second, where 26 MHz would be 98 frames and 52 MB/s against Wi-Fi and the
// program running from the same PSRAM. The ST7701's setup (SWRESET, MADCTL,
// COLMOD 0x60 as the vendor's RGB666 setting gives it, then the vendor's
// table) is Espressif's own ST7701 driver's order.
#define BBS_LCD_RGB           1
#define BBS_RGB_ST7701        1
#define BBS_LCD_DRIVER        "ST7701S"
#define BBS_LCD_SCALE         1
#define BBS_LCD_PHYS_W        480
#define BBS_LCD_PHYS_H        480
#define BBS_RGB_DATA          4, 5, 6, 7, 15, 8, 20, 3, 46, 9, 10, 11, 12, 13, 14, 0
#define BBS_RGB_PCLK          21
#define BBS_RGB_HSYNC         16
#define BBS_RGB_VSYNC         17
#define BBS_RGB_DE            18
#define BBS_RGB_PCLK_HZ       12000000
#define BBS_RGB_HPW           10
#define BBS_RGB_HBP           10
#define BBS_RGB_HFP           20
#define BBS_RGB_VPW           10
#define BBS_RGB_VBP           10
#define BBS_RGB_VFP           10
#define BBS_RGB_SPI_CS        39      // the ST7701's 3-wire SPI; SCK and SDA are the card's CLK and MOSI
#define BBS_RGB_SPI_SCK       48
#define BBS_RGB_SPI_SDA       47
#define BBS_RGB_BL            38      // the backlight, LEDC PWM, high is on
// The picture the plugin draws: the whole glass, 480 x 480, in the square
// layout (BBS_PANEL_BIG from the RAM figures below, BBS_PANEL_SQUARE from the
// size). BBS_RGB_YOFF is where the platform puts it: 0, the glass.
#define BBS_LCD_WIDTH         480
#define BBS_LCD_HEIGHT        480
#define BBS_RGB_YOFF          0
#define BBS_LCD_RAM_SHORT     480     // the big layout's figures: an RGB panel has no RAM
#define BBS_LCD_RAM_LONG      480
// The panel plugin's settings that an RGB panel has no use for.
#define BBS_LCD_MOSI          -1
#define BBS_LCD_SCLK          -1
#define BBS_LCD_CS            -1
#define BBS_LCD_DC            -1
#define BBS_LCD_RST           -1
#define BBS_LCD_BL            -1
#define BBS_LCD_XOFF          0
#define BBS_LCD_YOFF          0
#define BBS_LCD_ORIENT        0
#define BBS_LCD_INVERT        0
#define BBS_LCD_BGR           0
#define BBS_LCD_MIRROR        0
#define BBS_LCD_MHZ           10
#define BBS_LCD_BACKLIGHT     60      // percent: this backlight dims
#define BBS_LCD_BL_NOTE       "0 is dark; 60 as shipped."

// The GT911 on I2C (SDA 19, SCL 45, 400 kHz, the board's pull-ups), polled
// as on the 4.3B. No INT and no reset line reach the chip's pins, so the
// address it chose at power-up is not ours to pick: 0x5D is asked first and
// 0x14 second.
#define BBS_HAS_TOUCH         1
#define BBS_TOUCH_POLL        1       // polled over I2C (touchPoll), not taps on INT
#define BBS_TOUCH_ADDR        0x5D
#define BBS_TOUCH_ADDR2       0x14
#define BBS_I2C_SDA           19
#define BBS_I2C_SCL           45

// The S3's own temperature sensor, a cell of the square's system block. The
// WROOM-1 N16R8 is rated to 65 C ambient, and an 86 box puts it behind a
// backlight in a wall: warm from 65, as on the Touch-LCD-2.
#define BBS_HAS_CHIP_TEMP     1
#define BBS_PANEL_TEMP_WARM   65

// Pins the board owns (syscfg::pinProblem refuses them with the reason).
//   CONSOLE  43, 44: UART0 to the CH340, the console and Improv
//   LCD      the RGB bus, the panel's SPI CS, the backlight and the touch
//            controller's I2C pair (0, 3, 45 and 46 are strapping pins among
//            them; 19 and 20 are refused first as the chip's USB pins)
// The card's four lines are the sd plugin's settings, held by it; 47 and 48
// are the panel's SPI too, which the card's bus carries. 1, 2 and 40 are the
// sysop's: they go to the 2x4 header and nothing on this board drives them.
// On a relay variant they would click its relays; this profile is not for it.
#define BBS_PINS_CONSOLE      43, 44
#define BBS_PINS_LCD          BBS_RGB_DATA, BBS_RGB_PCLK, BBS_RGB_HSYNC, BBS_RGB_VSYNC, BBS_RGB_DE, \
                              BBS_RGB_SPI_CS, BBS_RGB_BL, BBS_I2C_SDA, BBS_I2C_SCL
// The backlight dark from the first instruction until the panel's PWM takes
// it; the ST7701's CS is held high the same way (platform_esp32_st7701.cpp).
#define BBS_PINS_HOLD_LOW     38

#endif  // BBS_BOARD_GT_4848S040

// One board profile at a time. Each block above checks the profiles written
// before it; this counts them all, so a profile added later is never missed.
#if (defined(BBS_BOARD_WS_S3LCD147) + defined(BBS_BOARD_WS_S3TOUCH43B) + defined(BBS_BOARD_MF_S3PAR35) + \
     defined(BBS_BOARD_FN_WROVER_CAM) + defined(BBS_BOARD_AI_ESP32CAM) + defined(BBS_BOARD_WS_S3TOUCH2) + \
     defined(BBS_BOARD_WS_S3ETH) + defined(BBS_BOARD_MF_S3PAR35V2) + defined(BBS_BOARD_GT_4848S040)) > 1
#error "one board profile at a time"
#endif

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
#ifndef BBS_HAS_LCD
#define BBS_PANEL_BIG         0
#define BBS_PANEL_SQUARE      0
#endif
#ifdef BBS_HAS_LCD
#ifndef BBS_LCD_DRIVER
#define BBS_LCD_DRIVER        "ST7789"  // the panel's controller, unless a profile says
#endif
#ifndef BBS_LCD_RAM_SHORT               // its memory, unturned: the ST7789's 240 x 320
#define BBS_LCD_RAM_SHORT     240       // (an RGB panel has none; the figures go unused)
#define BBS_LCD_RAM_LONG      320
#endif
// The big glass (the Makerfabs' 480 x 320): its own layout and fields in the
// panel plugin, and the core's per-line traffic bits it reads (Bbs::
// takePanelTraffic). Nothing else pays for either.
#define BBS_PANEL_BIG         (BBS_LCD_RAM_LONG >= 400)
// The square glass (480 x 480, the G4848, internal/tty-ux-panel-g4848-
// 2026-10-01.md): the big layout's square branch, its LEFT column, six recent
// events, a 228-column sweep, the ring banner and framed 480 x 320 skins.
// Exactly 480: every box of it is absolute. Off, the MF35's code is as it was.
#define BBS_PANEL_SQUARE      (BBS_PANEL_BIG && BBS_LCD_WIDTH == 480 && BBS_LCD_HEIGHT == 480)
#ifndef BBS_LCD_MHZ_NOTE                // CONFIG panel's note on the SPI clock, 38 at most
#define BBS_LCD_MHZ_NOTE      "10 is safe; the panel's limit is 62.5."
#endif
#ifndef BBS_LCD_BL_NOTE                 // an RGB panel's backlight note: the 4.3B's is on or off
#define BBS_LCD_BL_NOTE       "On or off only: 0 is off."
#endif
#endif
#if defined(BBS_HAS_TOUCH) && !defined(BBS_TOUCH_CHIP)
#define BBS_TOUCH_CHIP        "CST816"  // the taps controller's name (the Touch-LCD-2's)
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

// ---------------------------------------------------------------------------
// The console (1.2.1). BBS_CONSOLE_UART0 is 1 where the console is UART0,
// which flashing, the serial monitor and Improv then use, so a pin setting
// on BBS_CONSOLE_TX or BBS_CONSOLE_RX would break them: the classic ESP32
// boards (1 and 3), and an S3 whose console is a UART, the Makerfabs (43 and
// 44). An S3 whose console is the chip's own USB (the Waveshares) has UART0
// free, and the 4.3B ships its RS485 bridge on 43 and 44. On the board the
// generated sdkconfig says which; the host, with no sdkconfig, takes it from
// the profile: a classic chip, or an S3 that lists BBS_PINS_CONSOLE.
// On the Waveshare S3s, GPIO 43 shows the chip's boot messages for a moment
// at every reset (CONFIG_BOOT_ROM_LOG_ALWAYS_ON): avoid it for a LED or a
// relay. CONFIG does not refuse it, since the 4.3B's RS485 bridge is there.
// ---------------------------------------------------------------------------
#if defined(ESP_PLATFORM)
#if defined(CONFIG_ESP_CONSOLE_UART_NUM) && CONFIG_ESP_CONSOLE_UART_NUM == 0
#define BBS_CONSOLE_UART0     1
#else
#define BBS_CONSOLE_UART0     0
#endif
#elif !defined(BBS_CHIP_S3) || defined(BBS_PINS_CONSOLE)
#define BBS_CONSOLE_UART0     1
#else
#define BBS_CONSOLE_UART0     0
#endif
#if defined(BBS_CHIP_S3)
#define BBS_CONSOLE_TX        43      // UART0's default pins
#define BBS_CONSOLE_RX        44
#else
#define BBS_CONSOLE_TX        1
#define BBS_CONSOLE_RX        3
#endif
