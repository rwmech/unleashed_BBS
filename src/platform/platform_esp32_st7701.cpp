/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/platform/platform_esp32_st7701.cpp
 * Module:       Platform / ESP32-S3 ST7701 RGB panel boards (BBS_RGB_ST7701)
 *
 * Purpose:      The panel, its touch controller and its backlight on a board
 *               whose display is an ST7701 on the S3's RGB bus: the Guition
 *               ESP32-4848S040. The same plat::lcd* calls every other panel
 *               answers (platform.h), so the panel plugin draws the one way
 *               on every board. The 4.3B's RGB panel is platform_esp32_rgb.cpp,
 *               untouched by this one.
 *
 * Design:       Two buses, used one after the other.
 *
 *               The ST7701 is told how to drive its glass over a 3-wire SPI
 *               once, at the panel's first start: 9-bit words, a D/C bit and
 *               then 8, clocked on the rising edge with CS low for a command
 *               and its parameters, bit-banged on GPIOs as the factory
 *               firmware's own driver does it (ESP32_Display_Panel's 3-wire
 *               IO; an SPI peripheral sending the same words was never
 *               answered). Its SCK and SDA are the TF slot's CLK and MOSI,
 *               so with a card's SPI2 bus up the bus is held for the whole
 *               setup (a card command on another task waits) and the two
 *               pins are taken off its signals and given back after. The
 *               chip's status cannot be read back on this board. CS
 *               39 is held high from start-up (a constructor, below), so the
 *               card's traffic at its boot mount is never taken for the
 *               panel's. The sequence is Espressif's own ST7701 driver's:
 *               SWRESET, 120 ms; MADCTL and COLMOD 0x60 (the vendor's); then the
 *               vendor's table, as the factory firmware sends it, ending in
 *               sleep out, 120 ms, and display on.
 *
 *               Then the RGB bus feeds the glass continuously from one
 *               480 x 480 framebuffer the driver keeps in PSRAM, which the
 *               DMA reads by itself: no bounce buffers and no refill
 *               interrupt, so the refresh costs the CPU nothing (Rule no. 1).
 *               At 12 MHz that is 23 MB/s of PSRAM, 45 frames a second. The
 *               program runs from PSRAM too (XIP, sdkconfig.defaults.g4848),
 *               so a flash write leaves the cache on and the DMA fed; a frame
 *               the DMA fell behind on is restarted at the next VSYNC
 *               (LCD_RGB_RESTART_IN_VSYNC), never left shifted.
 *
 *               The panel plugin draws a 480 x 320 picture (the big glass's
 *               layout) and lcdDraw copies its bands into the framebuffer
 *               BBS_RGB_YOFF rows down, through the driver's draw_bitmap,
 *               which writes the cache back so the DMA sees them. The rows
 *               above and below stay black: the framebuffer is allocated
 *               zeroed and nothing draws there.
 *
 *               The backlight is LEDC PWM on GPIO 38 (1 kHz, 10 bits, the
 *               vendor's figures), held low from start-up (BBS_PINS_HOLD_LOW)
 *               until the first frame is out. The GT911 is polled on the BBS
 *               loop, one short I2C read every 50 ms, as on the 4.3B; it has
 *               no INT or reset line here, so the address it took at power-up
 *               is asked for, 0x5D and then 0x14.
 *
 * Libraries:    ESP-IDF esp_lcd (RGB), esp_driver_spi, esp_driver_i2c,
 *               esp_driver_ledc
 * Targets:      ESP32-S3 boards with BBS_LCD_RGB and BBS_RGB_ST7701
 * See also:     src/platform/platform.h, src/board.h, src/plugins/panel.cpp,
 *               release-prep/g4848/pins.md (outside the repository)
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
#include "../config.h"

#if defined(ESP_PLATFORM) && defined(BBS_LCD_RGB) && defined(BBS_RGB_ST7701)

#include "platform.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "soc/spi_periph.h"
#include "esp_private/spi_common_internal.h"      // spi_bus_get_attr: the card's bus pins
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstring>

namespace plat {

namespace {

constexpr int kW = BBS_LCD_WIDTH, kH = BBS_LCD_HEIGHT;           // the picture
constexpr int kPW = BBS_LCD_PHYS_W, kPH = BBS_LCD_PHYS_H;        // the glass
constexpr int kY0 = BBS_RGB_YOFF;                                // where the picture sits
static_assert(BBS_LCD_SCALE == 1, "the ST7701 path shows the picture at its own size");
static_assert(kW == kPW && kY0 >= 0 && kY0 + kH <= kPH, "the picture must lie inside the glass");

// A band for lcdDraw: 24 rows of the picture, as on the 4.3B.
constexpr uint32_t kBandPixels = static_cast<uint32_t>(kW) * 24u;

// GT911 registers.
constexpr uint16_t kTpStatus = 0x814E;   // bit 7 ready, low nibble the touch count

// The backlight's PWM: its own timer and channel (the SPI panels' and the
// camera's are 0, and neither is on this board, but nothing here needs to
// know that).
constexpr ledc_timer_t   kBlTimer = LEDC_TIMER_1;
constexpr ledc_channel_t kBlChan  = LEDC_CHANNEL_1;
constexpr uint32_t       kBlMax   = 1023;                         // 10 bits

// ---------------------------------------------------------------------------
// The ST7701's setup over 3-wire SPI
// ---------------------------------------------------------------------------
// One command: its byte, up to 16 parameter bytes, and the wait after it.
struct Cmd {
    uint8_t cmd;
    uint8_t n;
    uint8_t data[16];
    uint8_t delayMs;
};

// Espressif's ST7701 driver's preamble (esp_lcd_st7701_rgb.c: command 2 off,
// MADCTL, COLMOD), then the vendor's table as the factory firmware carries
// it, decoded from its image (release-prep/g4848/pins.md): the same bytes as
// ESP32_Display_Panel's board header for the ESP32-4848S040C_I_Y_3. It ends
// with sleep out (120 ms) and display on. SWRESET goes first, on its own
// (st7701Setup).
//
// COLMOD 0x60, 18 bits a pixel on the panel's side, as the factory sends it:
// that header's colour bits are RGB666, which the driver turns into 0x60.
// The board wires the 16 data lines to the 18-bit bus's positions (R on
// DB17-13, G on DB11-6, B on DB5-1, DB12 and DB0 not driven), so the S3's
// RGB565 lands on the top bits of each colour either way; 0x60 is the
// setting this glass is known to run.
constexpr Cmd kInit[] = {
    { 0xFF, 5,  { 0x77, 0x01, 0x00, 0x00, 0x00 }, 0 },
    { 0x36, 1,  { 0x00 }, 0 },                                     // MADCTL: RGB order, not mirrored
    { 0x3A, 1,  { 0x60 }, 0 },                                     // COLMOD: 18 bits, the vendor's
    { 0xFF, 5,  { 0x77, 0x01, 0x00, 0x00, 0x10 }, 0 },
    { 0xC0, 2,  { 0x3B, 0x00 }, 0 },
    { 0xC1, 2,  { 0x0D, 0x02 }, 0 },
    { 0xC2, 2,  { 0x31, 0x05 }, 0 },
    { 0xCD, 1,  { 0x00 }, 0 },
    { 0xB0, 16, { 0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18 }, 0 },
    { 0xB1, 16, { 0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18 }, 0 },
    { 0xFF, 5,  { 0x77, 0x01, 0x00, 0x00, 0x11 }, 0 },
    { 0xB0, 1,  { 0x60 }, 0 },
    { 0xB1, 1,  { 0x32 }, 0 },
    { 0xB2, 1,  { 0x07 }, 0 },
    { 0xB3, 1,  { 0x80 }, 0 },
    { 0xB5, 1,  { 0x49 }, 0 },
    { 0xB7, 1,  { 0x85 }, 0 },
    { 0xB8, 1,  { 0x21 }, 0 },
    { 0xC1, 1,  { 0x78 }, 0 },
    { 0xC2, 1,  { 0x78 }, 0 },
    { 0xE0, 3,  { 0x00, 0x1B, 0x02 }, 0 },
    { 0xE1, 11, { 0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44 }, 0 },
    { 0xE2, 12, { 0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00 }, 0 },
    { 0xE3, 4,  { 0x00, 0x00, 0x11, 0x11 }, 0 },
    { 0xE4, 2,  { 0x44, 0x44 }, 0 },
    { 0xE5, 16, { 0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0 }, 0 },
    { 0xE6, 4,  { 0x00, 0x00, 0x11, 0x11 }, 0 },
    { 0xE7, 2,  { 0x44, 0x44 }, 0 },
    { 0xE8, 16, { 0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0 }, 0 },
    { 0xEB, 7,  { 0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40 }, 0 },
    { 0xEC, 2,  { 0x3C, 0x00 }, 0 },
    { 0xED, 16, { 0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA }, 0 },
    { 0xFF, 5,  { 0x77, 0x01, 0x00, 0x00, 0x13 }, 0 },
    { 0xE5, 1,  { 0xE4 }, 0 },
    { 0xFF, 5,  { 0x77, 0x01, 0x00, 0x00, 0x00 }, 0 },
    { 0x11, 0,  { 0 }, 120 },                                      // sleep out
    { 0x29, 0,  { 0 }, 0 },                                        // display on
};

// The 3-wire link, bit-banged on GPIOs, as the factory firmware drives it
// (ESP32_Display_Panel's esp_lcd_panel_io_3wire_spi: its line config in the
// factory image is CS 39, SCL 48, SDA 47, each a plain GPIO). Mode 0: SCL
// idles low, SDA is set while SCL is low and taken on the rising edge, CS
// low for a command and its parameters. Words of 9 bits: D/C (0 a command,
// 1 a parameter), then the byte MSB first. About 100 kHz.
//
// The first cut sent the same words through SPI2's peripheral on the card's
// bus and the chip never answered (every read 0xFFFF, the glass black with
// the backlight lit). The vendor's own driver does not use a peripheral, and
// this does what it does.
constexpr gpio_num_t kCs  = static_cast<gpio_num_t>(BBS_RGB_SPI_CS);
constexpr gpio_num_t kScl = static_cast<gpio_num_t>(BBS_RGB_SPI_SCK);
constexpr gpio_num_t kSda = static_cast<gpio_num_t>(BBS_RGB_SPI_SDA);
constexpr uint32_t   kHalfUs = 5;

void bit(bool v) {
    gpio_set_level(kSda, v);
    esp_rom_delay_us(kHalfUs);
    gpio_set_level(kScl, 1);
    esp_rom_delay_us(kHalfUs);
    gpio_set_level(kScl, 0);
}

void word9(bool dc, uint8_t v) {
    bit(dc);
    for (int b = 7; b >= 0; --b) bit((v >> b) & 1);
}

// send: one command and its parameters, CS low for all of it, and the wait
// after it.
void send(uint8_t cmd, const uint8_t* data, uint8_t n, uint32_t delayMs) {
    gpio_set_level(kCs, 0);
    esp_rom_delay_us(kHalfUs);
    word9(false, cmd);
    for (uint8_t i = 0; i < n; ++i) word9(true, data[i]);
    esp_rom_delay_us(kHalfUs);
    gpio_set_level(kCs, 1);
    esp_rom_delay_us(kHalfUs * 2);
    if (delayMs) vTaskDelay(pdMS_TO_TICKS(delayMs));
}

// No read-back: the chip's status cannot be read on this board. RDDPM and
// RDDCOLMOD clocked back on SDA read 0xFFFF (the pull-up) even with the
// glass lit by this very setup, so the panel's SDA output evidently does not
// come back to GPIO 47, and a status line would say "nothing answered" about
// a working panel.

// csQuiet: the ST7701's chip select driven high as a plain GPIO.
void csQuiet() {
    gpio_set_level(kCs, 1);
    gpio_set_direction(kCs, GPIO_MODE_OUTPUT);
}

bool g_ctlDone = false;       // the ST7701 has been set up this boot

// st7701Setup: the whole sequence on the three GPIOs. SCL and SDA are the TF
// slot's CLK and MOSI, so while a card's SPI2 bus is up they are taken off
// its signals for the setup, with the bus held (a device of its own, no CS,
// acquired: a card command on another task waits), and given back after.
// With no bus, the pins are left as reset GPIOs for the sd plugin's next
// mount. The slot's CS is held high throughout so a card ignores the words.
// Blocks about 260 ms (two 120 ms waits): a plugin's start only.
bool st7701Setup(const char*& why, esp_err_t& err) {
    constexpr spi_host_device_t kHost = SPI2_HOST;               // the sd plugin's (SDSPI_DEFAULT_HOST)
    err = ESP_OK;
    spi_device_handle_t hold = nullptr;
    const spi_bus_attr_t* bus = spi_bus_get_attr(kHost);
    if (bus) {
        if (bus->bus_cfg.mosi_io_num != BBS_RGB_SPI_SDA || bus->bus_cfg.sclk_io_num != BBS_RGB_SPI_SCK) {
            why = "the card's SPI pins were moved off the panel's (47 and 48)";
            err = ESP_ERR_INVALID_STATE;
            return false;
        }
        spi_device_interface_config_t dc = {};
        dc.clock_speed_hz = 1000000;
        dc.spics_io_num   = -1;
        dc.queue_size     = 1;
        err = spi_bus_add_device(kHost, &dc, &hold);
        if (err == ESP_OK) err = spi_device_acquire_bus(hold, portMAX_DELAY);
        if (err != ESP_OK) {
            if (hold) spi_bus_remove_device(hold);
            why = "the card's SPI bus could not be held for the panel's setup";
            return false;
        }
    } else {
        // No bus: no card is mounted. gpio_config, as for 39: GPIO 42's IO
        // MUX function 0 is JTAG's MTMS.
        //
        // 42, 47 and 48 are the sd plugin's settings, so pinProblem cannot
        // refuse them to other plugins without refusing them to the card:
        // with sd off, a sysop could give them to another plugin, and this
        // setup would drive them. A rule for pins a panel shares with the
        // card, the sd plugin exempt, is queued for 1.2.1.
        gpio_set_level(static_cast<gpio_num_t>(BBS_SD_CS), 1);
        gpio_config_t cs = {};
        cs.pin_bit_mask = 1ULL << BBS_SD_CS;
        cs.mode         = GPIO_MODE_OUTPUT;
        gpio_config(&cs);
        gpio_set_level(static_cast<gpio_num_t>(BBS_SD_CS), 1);
    }

    // The two shared pins as GPIOs: SCL low, SDA driven (and an input, as
    // SPI2 has its MOSI). With a card's bus up, this takes them off SPI2's
    // signals.
    gpio_set_level(kScl, 0);
    gpio_set_level(kSda, 1);
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << BBS_RGB_SPI_SCK);
    io.mode         = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    io.pin_bit_mask = (1ULL << BBS_RGB_SPI_SDA);
    io.mode         = GPIO_MODE_INPUT_OUTPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_config(&io);
    esp_rom_gpio_connect_out_signal(BBS_RGB_SPI_SCK, SIG_GPIO_OUT_IDX, false, false);
    esp_rom_gpio_connect_out_signal(BBS_RGB_SPI_SDA, SIG_GPIO_OUT_IDX, false, false);
    csQuiet();

    send(0x01, nullptr, 0, 120);                                   // SWRESET
    for (const Cmd& c : kInit) send(c.cmd, c.data, c.n, c.delayMs);

    if (bus) {
        // Back to the card: SPI2's clock and data out on the same pins, as
        // spi_bus_initialize routed them (MOSI also its input, for 3-wire).
        esp_rom_gpio_connect_out_signal(BBS_RGB_SPI_SCK, spi_periph_signal[kHost].spiclk_out, false, false);
        esp_rom_gpio_connect_out_signal(BBS_RGB_SPI_SDA, spi_periph_signal[kHost].spid_out, false, false);
        spi_device_release_bus(hold);
        spi_bus_remove_device(hold);
    } else {
        gpio_reset_pin(kScl);
        gpio_reset_pin(kSda);
    }
    csQuiet();
    return true;
}

// ---------------------------------------------------------------------------
// The panel and the backlight
// ---------------------------------------------------------------------------
struct Rgb {
    esp_lcd_panel_handle_t panel = nullptr;
    bool                   up    = false;
    bool                   blUp  = false;      // the LEDC channel has the pin
    bool                   fade  = false;      // the LEDC fade is installed
    uint8_t                pct   = 0;          // the backlight as last set
    LcdCfg                 cfg;
};
Rgb g_rgb;

void rgbDelete() {
    if (g_rgb.panel) {
        esp_lcd_panel_del(g_rgb.panel);
        g_rgb.panel = nullptr;
    }
    g_rgb.up = false;
}

bool blBegin() {
    if (g_rgb.blUp) return true;
    ledc_timer_config_t t = {};
    t.speed_mode      = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.timer_num       = kBlTimer;
    t.freq_hz         = 1000;
    t.clk_cfg         = LEDC_AUTO_CLK;
    ledc_channel_config_t ch = {};
    ch.gpio_num   = BBS_RGB_BL;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel    = kBlChan;
    ch.timer_sel  = kBlTimer;
    ch.duty       = 0;
    if (ledc_timer_config(&t) != ESP_OK || ledc_channel_config(&ch) != ESP_OK) {
        log("panel: the backlight's PWM would not start");
        return false;
    }
    // The hardware fade, for coming on from dark (lcdBacklight). Already
    // installed by somebody else is fine.
    const esp_err_t f = ledc_fade_func_install(0);
    g_rgb.fade = f == ESP_OK || f == ESP_ERR_INVALID_STATE;
    g_rgb.blUp = true;
    return true;
}

// ---------------------------------------------------------------------------
// The touch controller
// ---------------------------------------------------------------------------
i2c_master_bus_handle_t g_bus = nullptr;
i2c_master_dev_handle_t g_tp  = nullptr;
bool     g_tpUp   = false;
uint16_t g_tpAddr = 0;
// A controller that stops answering: after kTpFails in a row the poll backs
// off to one try every kTpRetryMs, said once on the console, as on the 4.3B.
constexpr uint8_t  kTpFails   = 3;
constexpr uint32_t kTpRetryMs = 10000;
uint8_t  g_tpFails = 0;
uint32_t g_tpFailAt = 0;
bool     g_tpLiftOwed = false;              // answered again: report "no finger" once

bool tpRead(uint16_t reg, uint8_t* out, size_t n) {
    const uint8_t a[2] = { static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF) };
    return i2c_master_transmit_receive(g_tp, a, 2, out, n, 10) == ESP_OK;
}

bool tpWrite(uint16_t reg, uint8_t v) {
    const uint8_t a[3] = { static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF), v };
    return i2c_master_transmit(g_tp, a, 3, 10) == ESP_OK;
}

// tpBegin: the I2C bus, and the GT911 at whichever of its two addresses it
// answers. Once; a controller that did not answer is not asked again until
// the board restarts.
void tpBegin() {
    if (g_tp || g_bus) return;
    i2c_master_bus_config_t b = {};
    b.i2c_port          = I2C_NUM_0;
    b.sda_io_num        = static_cast<gpio_num_t>(BBS_I2C_SDA);
    b.scl_io_num        = static_cast<gpio_num_t>(BBS_I2C_SCL);
    b.clk_source        = I2C_CLK_SRC_DEFAULT;
    b.glitch_ignore_cnt = 7;
    b.flags.enable_internal_pullup = 1;
    esp_err_t e = i2c_new_master_bus(&b, &g_bus);
    if (e != ESP_OK) {
        log("panel: the touch controller's I2C bus would not start (%s)", esp_err_to_name(e));
        g_bus = nullptr;
        return;
    }
#ifndef BBS_RELEASE
    // Development builds only, once a boot: who else is on the touch bus, one
    // line. It answered Rob's question on the bench (2026-10-01): only the
    // GT911, at 0x14 and 0x5D; no IMU (0x68-0x6B) and no AXP2101 (0x34). About
    // 30 ms of the panel's start; release images leave it out.
    {
        char seen[112 * 5 + 1] = "";
        size_t at = 0;
        for (uint16_t a = 0x08; a < 0x78; ++a)
            if (i2c_master_probe(g_bus, a, 10) == ESP_OK && at + 6 < sizeof(seen))
                at += static_cast<size_t>(snprintf(seen + at, sizeof(seen) - at, " 0x%02X", a));
        log("panel: I2C on %d/%d answered at%s", BBS_I2C_SDA, BBS_I2C_SCL, at ? seen : " none");
        // An ID register for the parts Rob asked about, where something
        // answered at their address: AXP2101 at 0x34 (0x03, 0x4A), IMUs at
        // 0x68/0x69 (MPU/ICM 0x75, BMI 0x00) and 0x6A/0x6B (QMI8658 0x00,
        // LSM6 0x0F).
        const struct { uint8_t addr, reg; } kIds[] = {
            { 0x34, 0x03 }, { 0x68, 0x75 }, { 0x68, 0x00 }, { 0x69, 0x75 }, { 0x69, 0x00 },
            { 0x6A, 0x00 }, { 0x6A, 0x0F }, { 0x6B, 0x00 }, { 0x6B, 0x0F } };
        for (const auto& id : kIds) {
            if (i2c_master_probe(g_bus, id.addr, 10) != ESP_OK) continue;
            i2c_device_config_t d = {};
            d.dev_addr_length = I2C_ADDR_BIT_LEN_7;
            d.device_address  = id.addr;
            d.scl_speed_hz    = 400000;
            i2c_master_dev_handle_t h = nullptr;
            if (i2c_master_bus_add_device(g_bus, &d, &h) != ESP_OK) continue;
            uint8_t v = 0;
            const bool ok = i2c_master_transmit_receive(h, &id.reg, 1, &v, 1, 20) == ESP_OK;
            i2c_master_bus_rm_device(h);
            log("panel: I2C 0x%02X register 0x%02X = %s0x%02X", id.addr, id.reg, ok ? "" : "(no read) ",
                static_cast<unsigned>(v));
        }
    }
#endif  // !BBS_RELEASE
    const uint16_t kAddrs[2] = { BBS_TOUCH_ADDR, BBS_TOUCH_ADDR2 };
    for (uint16_t a : kAddrs) {
        if (i2c_master_probe(g_bus, a, 20) != ESP_OK) continue;
        i2c_device_config_t d = {};
        d.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        d.device_address  = a;
        d.scl_speed_hz    = 400000;
        if (i2c_master_bus_add_device(g_bus, &d, &g_tp) != ESP_OK) { g_tp = nullptr; break; }
        g_tpAddr = a;
        break;
    }
    uint8_t st = 0;
    g_tpUp = g_tp && tpRead(kTpStatus, &st, 1);
    if (!g_tpUp)
        log("panel: the GT911 touch controller did not answer at 0x%02X or 0x%02X",
            static_cast<unsigned>(BBS_TOUCH_ADDR), static_cast<unsigned>(BBS_TOUCH_ADDR2));
}

// A panel that failed to start is not tried again until the board restarts,
// as on the 4.3B: a CONFIG save of the panel's page would pay the setup again.
bool g_failed = false;

bool fail(char* err, size_t n, const char* why, esp_err_t e) {
    if (err && n) snprintf(err, n, "%s (%s)", why, esp_err_to_name(e));
    log("panel: %s (%s)", why, esp_err_to_name(e));
    g_failed = true;
    return false;
}

// The ST7701's chip select high from the first instruction, before the sd
// plugin's boot mount clocks the shared wires. A constructor, as the board's
// held-low pins are (platform_esp32.cpp): the GPIO calls need no task.
// gpio_config, not gpio_set_direction alone: it selects the pad's GPIO
// function too, and GPIO 39's IO MUX function 0 is the JTAG MTCK, so a bare
// direction call is not proof the level reaches the pin.
__attribute__((constructor)) void panelCsHighAtBoot() {
    const gpio_num_t g = static_cast<gpio_num_t>(BBS_RGB_SPI_CS);
    gpio_set_level(g, 1);
    gpio_config_t c = {};
    c.pin_bit_mask = 1ULL << BBS_RGB_SPI_CS;
    c.mode         = GPIO_MODE_OUTPUT;
    c.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_config(&c);
    gpio_set_level(g, 1);
}

}   // namespace

bool lcdBegin(const LcdCfg& c, char* err, size_t errLen) {
    if (err && errLen) err[0] = '\0';
    if (g_failed) {
        if (err && errLen) snprintf(err, errLen, "the panel did not start; restart the board");
        return false;
    }
    if (g_rgb.up) lcdEnd();

    if (!g_ctlDone) {
        const char* why = nullptr;
        esp_err_t e = ESP_OK;
        if (!st7701Setup(why, e)) return fail(err, errLen, why, e);
        g_ctlDone = true;
    }
    tpBegin();

    esp_lcd_rgb_panel_config_t pc = {};
    pc.clk_src                     = LCD_CLK_SRC_DEFAULT;
    pc.timings.pclk_hz             = BBS_RGB_PCLK_HZ;
    pc.timings.h_res               = kPW;
    pc.timings.v_res               = kPH;
    pc.timings.hsync_pulse_width   = BBS_RGB_HPW;
    pc.timings.hsync_back_porch    = BBS_RGB_HBP;
    pc.timings.hsync_front_porch   = BBS_RGB_HFP;
    pc.timings.vsync_pulse_width   = BBS_RGB_VPW;
    pc.timings.vsync_back_porch    = BBS_RGB_VBP;
    pc.timings.vsync_front_porch   = BBS_RGB_VFP;
    pc.timings.flags.pclk_active_neg = 0;                  // rising edge, the vendor's
    pc.data_width                  = 16;
    pc.bits_per_pixel              = 16;
    pc.num_fbs                     = 1;
    pc.bounce_buffer_size_px       = 0;                    // the DMA reads the framebuffer itself
    pc.psram_trans_align           = 64;
    pc.hsync_gpio_num              = BBS_RGB_HSYNC;
    pc.vsync_gpio_num              = BBS_RGB_VSYNC;
    pc.de_gpio_num                 = BBS_RGB_DE;
    pc.pclk_gpio_num               = BBS_RGB_PCLK;
    pc.disp_gpio_num               = -1;
    static const int kData[16] = { BBS_RGB_DATA };
    for (int i = 0; i < 16; ++i) pc.data_gpio_nums[i] = kData[i];
    pc.flags.fb_in_psram           = 1;
    esp_err_t e = esp_lcd_new_rgb_panel(&pc, &g_rgb.panel);
    if (e != ESP_OK) return fail(err, errLen, "the RGB panel would not start", e);
    e = esp_lcd_panel_reset(g_rgb.panel);
    if (e == ESP_OK) e = esp_lcd_panel_init(g_rgb.panel);   // the refresh starts here
    if (e != ESP_OK) {
        rgbDelete();
        return fail(err, errLen, "the RGB panel would not start its refresh", e);
    }
    g_rgb.up  = true;
    g_rgb.cfg = c;
    blBegin();
    lcdBacklight(c.backlight);
    log("panel: %s %ux%u on the RGB bus at %u MHz, drawn %ux%u at row %u; touch %s", BBS_LCD_DRIVER,
        static_cast<unsigned>(kPW), static_cast<unsigned>(kPH), static_cast<unsigned>(BBS_RGB_PCLK_HZ / 1000000),
        static_cast<unsigned>(kW), static_cast<unsigned>(kH), static_cast<unsigned>(kY0),
        g_tpUp ? "up" : "not answering");
    if (g_tpUp) log("panel: GT911 at 0x%02X", static_cast<unsigned>(g_tpAddr));
    return true;
}

bool lcdSame(const LcdCfg& c) {
    return g_rgb.up && g_rgb.cfg.width == c.width && g_rgb.cfg.height == c.height;
}

void lcdEnd() {
    lcdBacklight(0);
    rgbDelete();
    g_rgb.cfg = LcdCfg();
}

bool lcdReady() {
    return g_rgb.up;
}

uint32_t lcdBandPixels() {
    return kBandPixels;
}

// lcdDraw: a band of the plugin's picture into the framebuffer, a row at a
// time through the driver, which copies it and writes the cache back so the
// DMA reads what was drawn. A copy of at most 22 KB; nothing waits on the
// panel.
bool lcdDraw(const uint16_t* fb, uint16_t stride, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!g_rgb.up || !fb || !w || !h) return false;
    if (static_cast<uint32_t>(w) * h > kBandPixels) return false;
    if (x + w > kW || y + h > kH) return false;
    for (uint16_t r = 0; r < h; ++r) {
        const int row = kY0 + y + r;
        if (esp_lcd_panel_draw_bitmap(g_rgb.panel, x, row, x + w, row + 1,
                                      fb + static_cast<size_t>(y + r) * stride + x) != ESP_OK)
            return false;
    }
    return true;
}

// lcdBacklight: the PWM's duty, percent of full. Dark while the panel is
// down, whatever was asked.
void lcdBacklight(uint8_t pct) {
    if (pct > 100) pct = 100;
    const uint8_t want = g_rgb.up ? pct : 0;
    if (!g_rgb.blUp) { g_rgb.pct = 0; return; }
    if (want == g_rgb.pct) return;
    const uint32_t duty = kBlMax * want / 100u;
    // Coming on from dark, the hardware fades it up over 300 ms, started and
    // left to run (NO_WAIT): a backlight stepped from 0 to 60% in one go
    // pulled the supply hard enough to drop the CH340 off USB at every boot.
    // Any other change (dimming, sleep, silent) stops a fade still running
    // and sets the duty at once, so nothing here waits on one.
    if (g_rgb.fade && g_rgb.pct == 0 && want > 0) {
        if (ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, kBlChan, duty, 300) == ESP_OK &&
            ledc_fade_start(LEDC_LOW_SPEED_MODE, kBlChan, LEDC_FADE_NO_WAIT) == ESP_OK) {
            g_rgb.pct = want;
            return;
        }
    }
    if (g_rgb.fade) ledc_fade_stop(LEDC_LOW_SPEED_MODE, kBlChan);
    if (ledc_set_duty(LEDC_LOW_SPEED_MODE, kBlChan, duty) == ESP_OK &&
        ledc_update_duty(LEDC_LOW_SPEED_MODE, kBlChan) == ESP_OK)
        g_rgb.pct = want;
}

void* psramAlloc(size_t n) {
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void psramFree(void* p) {
    heap_caps_free(p);
}

// touchPoll: the GT911's status register, as on the 4.3B: bit 7 a new
// report, the low nibble how many fingers it saw. One I2C exchange to read
// it and, when a report is there, a second to clear it. False with no new
// report or no controller answering.
bool touchPoll(bool& down) {
    down = false;
    if (!g_tpUp) return false;
    const uint32_t now = millis();
    if (g_tpFails >= kTpFails && now - g_tpFailAt < kTpRetryMs) return false;
    uint8_t st = 0;
    if (!tpRead(kTpStatus, &st, 1)) {
        g_tpFailAt = now;
        if (g_tpFails < kTpFails && ++g_tpFails == kTpFails)
            log("panel: the touch controller stopped answering; trying every %u s",
                static_cast<unsigned>(kTpRetryMs / 1000));
        return false;
    }
    if (g_tpFails) {
        if (g_tpFails >= kTpFails) log("panel: the touch controller answers again");
        g_tpFails = 0;
        g_tpLiftOwed = true;
    }
    if (g_tpLiftOwed) {
        g_tpLiftOwed = false;
        return true;                                        // down stays false: no finger
    }
    if (!(st & 0x80)) return false;                         // no new report: nothing changed
    down = (st & 0x0F) > 0;
    tpWrite(kTpStatus, 0);                                  // the report is taken
    return true;
}

}   // namespace plat

#endif  // ESP_PLATFORM && BBS_LCD_RGB && BBS_RGB_ST7701
