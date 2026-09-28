/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/platform/platform_esp32_rgb.cpp
 * Module:       Platform / ESP32-S3 RGB panel boards (BBS_LCD_RGB)
 *
 * Purpose:      The panel, its touch controller, its I2C expander and the
 *               chip's temperature sensor on a board whose display is on the
 *               S3's RGB bus: the Waveshare ESP32-S3-Touch-LCD-4.3B. The same
 *               plat::lcd* calls the SPI panel answers (platform.h), so the
 *               panel plugin draws the one way on every board.
 *
 * Design:       The glass is fed continuously: an RGB panel has no memory of
 *               its own, so the S3 sends it a whole frame 39 times a second.
 *               The IDF driver does that from two small bounce buffers in
 *               internal RAM, refilled from an interrupt as each is sent.
 *               This file gives the driver NO frame buffer of its own
 *               (flags.no_fb) and fills the bounce buffers itself
 *               (on_bounce_empty), from the picture the panel plugin draws:
 *               400 x 240, every pixel twice across and every row twice
 *               down. So the interrupt reads 192 KB of PSRAM a frame rather
 *               than 768 KB, which is a quarter of the memory traffic and of
 *               the time spent in the interrupt, and 768 KB of PSRAM is never
 *               allocated at all.
 *
 *               The picture shown (g_rgb.show, PSRAM) is not the plugin's
 *               own framebuffer: lcdDraw copies a finished band into it, so
 *               the glass never shows a field cleared and not yet redrawn.
 *               A refill can still read a band while it is being copied,
 *               which shows for one frame (25 ms) at most. A copy is a few
 *               hundred microseconds; nothing here waits on the panel.
 *
 *               The refill interrupt is allocated on the core that starts
 *               the panel, the BBS task's (core 1), and fires about 4,900
 *               times a second for 1,600 bytes of PSRAM each. That is CPU
 *               the loop does not get; SYS's loop figures are where it
 *               shows. It keeps running while the panel plugin is switched
 *               off (stop() keeps the panel up across a CONFIG save's
 *               restart, and cannot tell the two apart), dark, until the
 *               board restarts.
 *
 *               The program runs from PSRAM (sdkconfig.defaults.ws43b,
 *               SPIRAM_XIP_FROM_PSRAM), so a flash write no longer turns the
 *               cache off, and the interrupt can read g_show while LittleFS
 *               erases a block: without that the glass would tear on every
 *               write, and an interrupt reading PSRAM with the cache off
 *               would crash the board.
 *
 *               The expander (CH422G) is a set of I2C addresses, one a
 *               command. Its eight IO pins share one direction bit, so it is
 *               kept an output port for good and written whole from one
 *               byte (g_ex). The touch controller (GT911) is read on the BBS
 *               loop, one short I2C read when the panel plugin polls, which
 *               it does every 50 ms: about 150 us.
 *
 * Libraries:    ESP-IDF esp_lcd (RGB), esp_driver_i2c, esp_driver_tsens
 * Targets:      ESP32-S3 boards with BBS_LCD_RGB
 * See also:     src/platform/platform.h, src/board.h, src/plugins/panel.cpp
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

#if defined(ESP_PLATFORM) && defined(BBS_LCD_RGB)

#include "platform.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if defined(BBS_HAS_CHIP_TEMP)
#include "driver/temperature_sensor.h"
#endif

#include <cstdio>
#include <cstring>

namespace plat {

namespace {

constexpr int kW = BBS_LCD_WIDTH, kH = BBS_LCD_HEIGHT;           // the picture
constexpr int kPW = BBS_LCD_PHYS_W, kPH = BBS_LCD_PHYS_H;        // the glass
static_assert(kW * BBS_LCD_SCALE == kPW && kH * BBS_LCD_SCALE == kPH,
              "the picture at BBS_LCD_SCALE must be the glass");
static_assert(BBS_LCD_SCALE == 2, "the bounce fill doubles; another scale wants another fill");
static_assert(BBS_RGB_BOUNCE_LINES % 2 == 0, "a bounce buffer holds whole picture rows");
static_assert((kPH / BBS_RGB_BOUNCE_LINES) % 2 == 0, "the IDF wants the frame an even number of buffers");

// A band for lcdDraw: 24 rows of the picture, 19 KB copied PSRAM to PSRAM.
constexpr uint32_t kBandPixels = static_cast<uint32_t>(kW) * 24u;

// The CH422G's commands, each its own 7-bit address (its datasheet, 5.4).
constexpr uint16_t kExMode = 0x24;       // WR_SET: bit 0 IO_OE, the port an output
constexpr uint16_t kExIo   = 0x38;       // WR_IO: IO7..IO0

// GT911 registers.
constexpr uint16_t kTpStatus = 0x814E;   // bit 7 ready, low nibble the touch count

// ---------------------------------------------------------------------------
// The I2C bus and the expander
// ---------------------------------------------------------------------------
i2c_master_bus_handle_t g_bus   = nullptr;
i2c_master_dev_handle_t g_exMode = nullptr, g_exIo = nullptr, g_tp = nullptr;
bool                    g_exUp  = false;
// The port as last written. As shipped: touch and panel out of reset, the
// card selected, the backlight off; EXIO0 and EXIO5 (the isolated inputs)
// low, as the demos write them.
uint8_t                 g_ex    = BBS_EX_TP_RST | BBS_EX_LCD_RST;

bool exWrite() {
    const uint8_t b = g_ex;
    return i2c_master_transmit(g_exIo, &b, 1, 20) == ESP_OK;
}

bool addDev(uint16_t addr, i2c_master_dev_handle_t& out) {
    i2c_device_config_t d = {};
    d.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    d.device_address  = addr;
    d.scl_speed_hz    = 400000;
    if (i2c_master_bus_add_device(g_bus, &d, &out) == ESP_OK) return true;
    out = nullptr;
    return false;
}

// ---------------------------------------------------------------------------
// The picture and the panel
// ---------------------------------------------------------------------------
struct Rgb {
    esp_lcd_panel_handle_t panel = nullptr;
    uint16_t*              show  = nullptr;     // the picture on the glass, PSRAM
    bool                   up    = false;
    bool                   lit   = false;
    LcdCfg                 cfg;
};
Rgb g_rgb;

// fill: one bounce buffer, rows of the glass from pos_px on, each picture
// pixel twice across and each picture row twice down. In IRAM, as the IDF
// requires of this callback; it reads g_show from PSRAM, which the XIP
// configuration keeps readable through a flash write.
IRAM_ATTR bool fill(esp_lcd_panel_handle_t, void* buf, int pos_px, int len_bytes, void*) {
    const uint16_t* src = g_rgb.show;
    uint32_t* out = static_cast<uint32_t*>(buf);
    const int rows = len_bytes / (kPW * 2);
    const int row0 = pos_px / kPW;
    for (int r = 0; r < rows; r += 2) {
        const uint16_t* in = src + static_cast<size_t>((row0 + r) >> 1) * kW;
        uint32_t* first = out;
        for (int x = 0; x < kW; ++x) {
            const uint32_t p = in[x];
            *out++ = p | (p << 16);
        }
        memcpy(out, first, kPW * 2);             // the same row again
        out += kPW / 2;
    }
    return false;
}

void rgbDelete() {
    if (g_rgb.panel) {
        esp_lcd_panel_del(g_rgb.panel);
        g_rgb.panel = nullptr;
    }
    g_rgb.up = false;
}

// ---------------------------------------------------------------------------
// The touch controller
// ---------------------------------------------------------------------------
bool g_tpUp = false;
// A controller that stops answering: after kTpFails in a row the poll backs
// off to one try every kTpRetryMs, said once on the console, so a stuck bus
// costs one I2C timeout every ten seconds rather than two every 50 ms (and
// the IDF driver's error line with each).
constexpr uint8_t  kTpFails   = 3;
constexpr uint32_t kTpRetryMs = 10000;
uint8_t  g_tpFails = 0;
uint32_t g_tpFailAt = 0;
bool     g_tpLiftOwed = false;              // answered again: report "no finger" once

// tpReset: the GT911's reset with INT held low, which picks address 0x5D,
// then INT let go (Waveshare's Arduino sequence for this board). Blocks
// about 310 ms: a plugin's start only.
void tpReset() {
    gpio_config_t c = {};
    c.pin_bit_mask = 1ULL << BBS_TOUCH_INT;
    c.mode = GPIO_MODE_OUTPUT;
    gpio_config(&c);
    gpio_set_level(static_cast<gpio_num_t>(BBS_TOUCH_INT), 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    g_ex = static_cast<uint8_t>(g_ex & ~BBS_EX_TP_RST);
    exWrite();
    vTaskDelay(pdMS_TO_TICKS(100));
    g_ex = static_cast<uint8_t>(g_ex | BBS_EX_TP_RST);
    exWrite();
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_reset_pin(static_cast<gpio_num_t>(BBS_TOUCH_INT));
    c.mode = GPIO_MODE_INPUT;
    gpio_config(&c);
}

bool tpRead(uint16_t reg, uint8_t* out, size_t n) {
    const uint8_t a[2] = { static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF) };
    return i2c_master_transmit_receive(g_tp, a, 2, out, n, 10) == ESP_OK;
}

bool tpWrite(uint16_t reg, uint8_t v) {
    const uint8_t a[3] = { static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF), v };
    return i2c_master_transmit(g_tp, a, 3, 10) == ESP_OK;
}

// A panel that failed to start is not tried again until the board restarts:
// a start is about 420 ms of resets on the BBS loop, and a CONFIG save of the
// panel's page would otherwise pay it again every time, with callers on.
bool g_failed = false;

bool fail(char* err, size_t n, const char* why, esp_err_t e) {
    if (err && n) snprintf(err, n, "%s (%s)", why, esp_err_to_name(e));
    log("panel: %s (%s)", why, esp_err_to_name(e));
    g_failed = true;
    return false;
}

}   // namespace

// boardExpander: the I2C bus and the CH422G up, its port an output, written
// as g_ex holds it: the card selected, both resets released, the backlight
// off. Once; every later call is true at once. The sd plugin calls it before
// its first mount (the card's chip select is an expander pin) and the panel
// before its first draw, whichever is first.
bool boardExpander() {
    if (g_exUp) return true;
    if (!g_bus) {
        i2c_master_bus_config_t b = {};
        b.i2c_port          = I2C_NUM_0;
        b.sda_io_num        = static_cast<gpio_num_t>(BBS_I2C_SDA);
        b.scl_io_num        = static_cast<gpio_num_t>(BBS_I2C_SCL);
        b.clk_source        = I2C_CLK_SRC_DEFAULT;
        b.glitch_ignore_cnt = 7;
        b.flags.enable_internal_pullup = 1;
        esp_err_t e = i2c_new_master_bus(&b, &g_bus);
        if (e != ESP_OK) {
            log("board: the I2C bus would not start (%s)", esp_err_to_name(e));
            g_bus = nullptr;
            return false;
        }
    }
    // Each device on its own, so a retry registers whichever did not take
    // the first time rather than skipping them all because the bus is up.
    if ((!g_exMode && !addDev(kExMode, g_exMode)) || (!g_exIo && !addDev(kExIo, g_exIo)) ||
        (!g_tp && !addDev(BBS_TOUCH_ADDR, g_tp))) {
        log("board: the I2C devices would not register");
        return false;
    }
    const uint8_t oe = 0x01;                               // IO_OE: the port drives
    if (i2c_master_transmit(g_exMode, &oe, 1, 20) != ESP_OK || !exWrite()) {
        log("board: the CH422G expander did not answer on I2C");
        return false;
    }
    g_exUp = true;
    return true;
}

bool lcdBegin(const LcdCfg& c, char* err, size_t errLen) {
    if (err && errLen) err[0] = '\0';
    if (g_failed) {
        if (err && errLen) snprintf(err, errLen, "the panel did not start; restart the board");
        return false;
    }
    if (g_rgb.up) lcdEnd();
    if (!boardExpander()) return fail(err, errLen, "the board's I2C expander did not answer", ESP_FAIL);

    if (!g_rgb.show) {
        g_rgb.show = static_cast<uint16_t*>(heap_caps_calloc(static_cast<size_t>(kW) * kH, 2,
                                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!g_rgb.show) return fail(err, errLen, "no PSRAM for the picture", ESP_ERR_NO_MEM);
    }

    // The panel's reset (EXIO3, low 10 ms, then 100 ms to settle), as the
    // Arduino board definition pulses it, and the touch controller's.
    g_ex = static_cast<uint8_t>(g_ex & ~(BBS_EX_LCD_RST | BBS_EX_BACKLIGHT));
    exWrite();
    vTaskDelay(pdMS_TO_TICKS(10));
    g_ex = static_cast<uint8_t>(g_ex | BBS_EX_LCD_RST);
    exWrite();
    vTaskDelay(pdMS_TO_TICKS(100));
    g_rgb.lit = false;
    tpReset();
    uint8_t st = 0;
    g_tpUp = tpRead(kTpStatus, &st, 1);
    if (!g_tpUp) log("panel: the GT911 touch controller did not answer at 0x%02X", BBS_TOUCH_ADDR);

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
    pc.timings.flags.pclk_active_neg = 1;
    pc.data_width                  = 16;
    pc.bits_per_pixel              = 16;
    pc.bounce_buffer_size_px       = static_cast<size_t>(kPW) * BBS_RGB_BOUNCE_LINES;
    pc.psram_trans_align           = 64;
    pc.hsync_gpio_num              = BBS_RGB_HSYNC;
    pc.vsync_gpio_num              = BBS_RGB_VSYNC;
    pc.de_gpio_num                 = BBS_RGB_DE;
    pc.pclk_gpio_num               = BBS_RGB_PCLK;
    pc.disp_gpio_num               = -1;
    static const int kData[16] = { BBS_RGB_DATA };
    for (int i = 0; i < 16; ++i) pc.data_gpio_nums[i] = kData[i];
    pc.flags.no_fb                 = 1;                    // the bounce fill is the picture
    esp_err_t e = esp_lcd_new_rgb_panel(&pc, &g_rgb.panel);
    if (e != ESP_OK) return fail(err, errLen, "the RGB panel would not start", e);
    esp_lcd_rgb_panel_event_callbacks_t cb = {};
    cb.on_bounce_empty = fill;
    e = esp_lcd_rgb_panel_register_event_callbacks(g_rgb.panel, &cb, nullptr);
    if (e == ESP_OK) e = esp_lcd_panel_reset(g_rgb.panel);
    if (e == ESP_OK) e = esp_lcd_panel_init(g_rgb.panel);   // the refresh starts here
    if (e != ESP_OK) {
        rgbDelete();
        return fail(err, errLen, "the RGB panel would not start its refresh", e);
    }
    g_rgb.up  = true;
    g_rgb.cfg = c;
    lcdBacklight(c.backlight);
    log("panel: %s %ux%u on the RGB bus, drawn %ux%u at %u MHz; touch %s", BBS_LCD_DRIVER,
        static_cast<unsigned>(kPW), static_cast<unsigned>(kPH), static_cast<unsigned>(kW),
        static_cast<unsigned>(kH), static_cast<unsigned>(BBS_RGB_PCLK_HZ / 1000000),
        g_tpUp ? "up" : "not answering");
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

bool lcdDraw(const uint16_t* fb, uint16_t stride, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!g_rgb.up || !fb || !w || !h) return false;
    if (static_cast<uint32_t>(w) * h > kBandPixels) return false;
    if (x + w > kW || y + h > kH) return false;
    for (uint16_t r = 0; r < h; ++r)
        memcpy(g_rgb.show + static_cast<size_t>(y + r) * kW + x,
               fb + static_cast<size_t>(y + r) * stride + x, static_cast<size_t>(w) * 2u);
    return true;
}

// lcdBacklight: on or off, whatever the percent: the boost that lights the
// panel has an enable and no dimming input on this board (EXIO2).
void lcdBacklight(uint8_t pct) {
    const bool on = pct > 0 && g_rgb.up;
    if (!g_exUp || on == g_rgb.lit) {
        g_rgb.lit = on && g_exUp;
        return;
    }
    g_ex = static_cast<uint8_t>(on ? (g_ex | BBS_EX_BACKLIGHT) : (g_ex & ~BBS_EX_BACKLIGHT));
    if (exWrite()) g_rgb.lit = on;
}

void* psramAlloc(size_t n) {
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void psramFree(void* p) {
    heap_caps_free(p);
}

// touchPoll: the GT911's status register: bit 7 a new report, the low
// nibble how many fingers it saw. One I2C exchange to read it and, when a
// report is there, a second to clear it. False with no new report or no
// controller answering.
//
// A controller that stops answering is backed off (above). When it answers
// again the first poll reports "no finger", so a finger that was down when
// it went quiet does not swallow the next tap.
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

#if defined(BBS_HAS_CHIP_TEMP)
// chipTemp: the die's temperature, whole degrees C. The sensor is installed
// on the first call and read in a few microseconds after that. The 20 to
// 100 C range (2 C error), not the -10 to 80 one: a die in a closed case
// runs in the fifties, and the panel's warning bands start at 60 and 75, so
// the range has to reach past them rather than fail at 80 and leave the last
// figure standing.
bool chipTemp(int& celsius) {
    static temperature_sensor_handle_t t = nullptr;
    static bool tried = false;
    if (!t) {
        if (tried) return false;
        tried = true;
        temperature_sensor_config_t c = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
        if (temperature_sensor_install(&c, &t) != ESP_OK) { t = nullptr; return false; }
        if (temperature_sensor_enable(t) != ESP_OK) { temperature_sensor_uninstall(t); t = nullptr; return false; }
    }
    float f = 0;
    if (temperature_sensor_get_celsius(t, &f) != ESP_OK) return false;
    celsius = static_cast<int>(f < 0 ? f - 0.5f : f + 0.5f);
    return true;
}
#endif

}   // namespace plat

#endif  // ESP_PLATFORM && BBS_LCD_RGB
