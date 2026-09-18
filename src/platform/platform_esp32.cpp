/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/platform/platform_esp32.cpp
 * Module:       Platform layer (ESP32)
 *
 * Purpose:      ESP32 (ESP-IDF) implementation of the platform layer.
 *
 * Libraries:    ESP-IDF: esp_timer, esp_hw_support (esp_random), heap,
 *                  esp_driver_gpio, esp_rom (ROM miniz tinfl), esp_wifi
 * Targets:      ESP32-WROOM-32E, ESP-IDF 5.3.1
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

#include "platform.h"
#include "../config.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_littlefs.h"
#include "driver/uart.h"
extern "C" {
#include "miniz.h"
}
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace plat {

uint32_t millis() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

uint32_t micros() {
    return static_cast<uint32_t>(esp_timer_get_time());
}

uint32_t random32() {
    return esp_random();
}

const char* fsBase() {
    return BBS_FS_MOUNT;
}

const char* logsBase() {
    return BBS_LOGS_MOUNT;
}

HeapStats heap() {
    HeapStats h;
    h.freeBytes    = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_8BIT));
    h.minFree      = static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    h.largestBlock = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    h.valid        = true;
    return h;
}

bool fsInfo(uint32_t& total, uint32_t& used) {
    size_t t = 0, u = 0;
    if (esp_littlefs_info(BBS_FS_LABEL, &t, &u) != ESP_OK) return false;
    total = static_cast<uint32_t>(t);
    used  = static_cast<uint32_t>(u);
    return true;
}

// ---------------------------------------------------------------------------
// Device serial port on UART2 (UART0 stays the console and flashing port).
// The ESP32 routes UART signals through the GPIO matrix, so any free pin
// works, not just the default 16 and 17.
// ---------------------------------------------------------------------------
namespace {
constexpr uart_port_t kDevUart = UART_NUM_2;
bool     g_serialOpen = false;
uint32_t g_framing    = 0;

uart_word_length_t wordLen(uint8_t bits) {
    switch (bits) {
        case 5:  return UART_DATA_5_BITS;
        case 6:  return UART_DATA_6_BITS;
        case 7:  return UART_DATA_7_BITS;
        default: return UART_DATA_8_BITS;
    }
}

uart_parity_t parityOf(char p) {
    if (p == 'E' || p == 'e') return UART_PARITY_EVEN;
    if (p == 'O' || p == 'o') return UART_PARITY_ODD;
    return UART_PARITY_DISABLE;
}
}   // namespace

bool serialOpen(int rxPin, int txPin, uint32_t baud, uint8_t bits, char parity, uint8_t stop) {
    if (g_serialOpen) serialClose();
    uart_config_t cfg = {};
    cfg.baud_rate = static_cast<int>(baud);
    cfg.data_bits = wordLen(bits);
    cfg.parity    = parityOf(parity);
    cfg.stop_bits = stop == 2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    if (uart_driver_install(kDevUart, 2048, 512, 0, nullptr, 0) != ESP_OK) return false;
    if (uart_param_config(kDevUart, &cfg) != ESP_OK ||
        uart_set_pin(kDevUart, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        uart_driver_delete(kDevUart);
        return false;
    }
    g_serialOpen = true;
    g_framing    = 0;
    return true;
}

void serialClose() {
    if (!g_serialOpen) return;
    uart_driver_delete(kDevUart);
    g_serialOpen = false;
}

bool serialIsOpen() { return g_serialOpen; }

bool serialSetLine(uint32_t baud, uint8_t bits, char parity, uint8_t stop) {
    if (!g_serialOpen) return false;
    bool ok = uart_set_baudrate(kDevUart, baud) == ESP_OK;
    ok = uart_set_word_length(kDevUart, wordLen(bits)) == ESP_OK && ok;
    ok = uart_set_parity(kDevUart, parityOf(parity)) == ESP_OK && ok;
    ok = uart_set_stop_bits(kDevUart, stop == 2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1) == ESP_OK && ok;
    g_framing = 0;
    return ok;
}

size_t serialRead(uint8_t* buf, size_t cap) {
    if (!g_serialOpen) return 0;
    size_t waiting = 0;
    if (uart_get_buffered_data_len(kDevUart, &waiting) != ESP_OK || !waiting) return 0;
    if (waiting > cap) waiting = cap;
    int n = uart_read_bytes(kDevUart, buf, waiting, 0);
    return n > 0 ? static_cast<size_t>(n) : 0;
}

size_t serialWrite(const uint8_t* data, size_t n) {
    if (!g_serialOpen) return 0;
    int w = uart_write_bytes(kDevUart, reinterpret_cast<const char*>(data), n);
    return w > 0 ? static_cast<size_t>(w) : 0;
}

uint32_t serialFramingErrors() { return g_framing; }

int8_t wifiRssi() {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
    return ap.rssi;
}

// ---------------------------------------------------------------------------
// netInfo: SSID, channel and signal come from the station record, the
// address from the default station netif. Anything missing leaves the
// field empty rather than failing the whole call.
// ---------------------------------------------------------------------------
NetInfo netInfo() {
    NetInfo n;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        snprintf(n.ssid, sizeof(n.ssid), "%.32s", reinterpret_cast<const char*>(ap.ssid));
        n.channel = ap.primary;
        n.rssi    = ap.rssi;
        n.valid   = true;
    }
    esp_netif_t* nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (nif && esp_netif_get_ip_info(nif, &ip) == ESP_OK && ip.ip.addr) {
        snprintf(n.ip, sizeof(n.ip), IPSTR, IP2STR(&ip.ip));
    }
    return n;
}

void log(const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("[%8lu] %s\n", static_cast<unsigned long>(millis()), buf);
}

// ===========================================================================
// Backup button: active low with pull-up (BOOT is GPIO0 on dev boards)
// ===========================================================================

namespace {
int      g_btnGpio   = -1;
bool     g_btnLast   = false;    // debounced state, true = pressed
bool     g_btnRaw    = false;
uint32_t g_btnSince  = 0;
constexpr uint32_t kDebounceMs = 50;
}

void backupButtonBegin(int gpio) {
#ifdef BBS_BACKUP_TEST_OPEN
    (void)gpio;
    log("backup: TEST BUILD, button treated as always pressed");
#else
    if (gpio < 0 || gpio >= GPIO_NUM_MAX) { log("backup: button disabled (gpio %d)", gpio); return; }
    gpio_config_t c = {};
    c.pin_bit_mask = 1ULL << gpio;
    c.mode         = GPIO_MODE_INPUT;
    c.pull_up_en   = GPIO_PULLUP_ENABLE;
    c.pull_down_en = GPIO_PULLDOWN_DISABLE;
    c.intr_type    = GPIO_INTR_DISABLE;
    if (gpio_config(&c) != ESP_OK) { log("backup: gpio %d config failed", gpio); return; }
    g_btnGpio = gpio;
    log("backup: button on gpio %d", gpio);
#endif
}

bool backupButtonPressed(uint32_t now) {
#ifdef BBS_BACKUP_TEST_OPEN
    (void)now;
    return true;
#else
    if (g_btnGpio < 0) return false;
    bool raw = gpio_get_level(static_cast<gpio_num_t>(g_btnGpio)) == 0;
    if (raw != g_btnRaw) { g_btnRaw = raw; g_btnSince = now; }
    if (raw != g_btnLast && now - g_btnSince >= kDebounceMs) {
        g_btnLast = raw;
        return raw;                               // report the press edge only
    }
    return false;
#endif
}

// ===========================================================================
// Activity LED: active high, driven only after boot (GPIO2 is a strap pin)
// ===========================================================================

namespace {
int      g_ledGpio  = -1;
bool     g_ledOn    = false;
uint32_t g_ledSince = 0;
}

void activityLedBegin(int gpio) {
    if (gpio < 0 || gpio >= GPIO_NUM_MAX) { log("led: activity LED off"); return; }
    gpio_config_t c = {};
    c.pin_bit_mask = 1ULL << gpio;
    c.mode         = GPIO_MODE_OUTPUT;
    c.intr_type    = GPIO_INTR_DISABLE;
    if (gpio_config(&c) != ESP_OK) { log("led: gpio %d config failed", gpio); return; }
    g_ledGpio = gpio;
    gpio_set_level(static_cast<gpio_num_t>(gpio), 0);
    log("led: activity LED on gpio %d", gpio);
}

void activityPulse(uint32_t now) {
    if (g_ledGpio < 0) return;
    g_ledSince = now;
    if (!g_ledOn) {
        g_ledOn = true;
        gpio_set_level(static_cast<gpio_num_t>(g_ledGpio), 1);
    }
}

void activityTick(uint32_t now) {
    if (g_ledOn && now - g_ledSince >= BBS_LED_PULSE_MS) {
        g_ledOn = false;
        gpio_set_level(static_cast<gpio_num_t>(g_ledGpio), 0);
    }
}

// ===========================================================================
// inflateRaw: ROM tinfl with a 32 KB circular dictionary
// ===========================================================================

bool inflateRaw(InflateIn in, InflateOut out, void* ctx) {
    tinfl_decompressor* d = static_cast<tinfl_decompressor*>(malloc(sizeof(tinfl_decompressor)));
    uint8_t* dict = static_cast<uint8_t*>(malloc(TINFL_LZ_DICT_SIZE));
    uint8_t* ibuf = static_cast<uint8_t*>(malloc(1024));
    bool ok = false;
    if (d && dict && ibuf) {
        tinfl_init(d);
        size_t inAvail = 0, inOff = 0, outPos = 0;
        bool eof = false;
        for (;;) {
            if (inAvail == 0 && !eof) {
                inAvail = in(ctx, ibuf, 1024);
                inOff   = 0;
                if (inAvail == 0) eof = true;
            }
            size_t inBytes  = inAvail;
            size_t outBytes = TINFL_LZ_DICT_SIZE - outPos;
            tinfl_status st = tinfl_decompress(d, ibuf + inOff, &inBytes, dict, dict + outPos, &outBytes,
                                               eof ? 0 : TINFL_FLAG_HAS_MORE_INPUT);
            inOff   += inBytes;
            inAvail -= inBytes;
            if (outBytes && !out(ctx, dict + outPos, outBytes)) break;
            outPos = (outPos + outBytes) & (TINFL_LZ_DICT_SIZE - 1);
            if (st == TINFL_STATUS_DONE) { ok = true; break; }
            if (st < 0) break;
            if (st == TINFL_STATUS_NEEDS_MORE_INPUT && eof) break;
        }
    }
    free(ibuf);
    free(dict);
    free(d);
    return ok;
}

} // namespace plat
