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
#include "esp_system.h"      // esp_reset_reason
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_littlefs.h"
#include "driver/uart.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
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

const char* userBase() {
    return BBS_USER_BASE;
}

bool userInfo(uint32_t& total, uint32_t& used) {
    size_t t = 0, u = 0;
    if (esp_littlefs_info(BBS_USER_LABEL, &t, &u) != ESP_OK) return false;
    total = static_cast<uint32_t>(t);
    used  = static_cast<uint32_t>(u);
    return true;
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
    h.totalBytes   = static_cast<uint32_t>(heap_caps_get_total_size(MALLOC_CAP_8BIT));
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
// The SD card, over SPI.
//
// SPI and not SDMMC on purpose. SDMMC is faster, but it is nailed to fixed
// pins on the ESP32, two of which are strapping pins, and it wants pull-ups
// a plain breakout module does not always have. SPI works on any four free
// pins and on every cheap module, which is what somebody following the wiring
// page will actually have in their hand. A BBS moves a few kilobytes at a
// time; the card is never the slow part of a 2400 baud illusion.
//
// Everything here is blocking, which is why nothing in this file is ever
// called from the BBS loop. See the note in platform.h.
// ---------------------------------------------------------------------------
static sdmmc_card_t* g_card  = nullptr;
static bool          g_mount = false;
static uint32_t      g_speed = 0;
// Whether *this* code called spi_bus_initialize, so it knows whether it is
// entitled to free the bus. Without it, a mount that failed after the bus
// came up left the bus initialised with the old pins: the sysop corrected a
// pin in CONFIG, the next attempt got ESP_ERR_INVALID_STATE, the tolerance
// for that swallowed it, and the card was probed on the old wiring forever
// while the config showed the new number. That is the exact troubleshooting
// loop the wiring page sends people into.
static bool          g_busUp = false;
static SdPins        g_busPins;

const char* sdBase() {
    return g_mount ? BBS_SD_MOUNT : "";
}

bool sdMount(const SdPins& pins, char* err, size_t errLen) {
    auto fail = [&](const char* why) {
        if (err && errLen) snprintf(err, errLen, "%s", why);
        return false;
    };
    if (err && errLen) err[0] = '\0';
    if (g_mount) return true;                      // already up, nothing to do

    // The bus is configured with MOSI, MISO and CLK, so a change to any of
    // them needs it rebuilt. CS is a device setting and does not.
    if (g_busUp && (g_busPins.mosi != pins.mosi || g_busPins.miso != pins.miso ||
                    g_busPins.clk  != pins.clk)) {
        spi_bus_free(SDSPI_DEFAULT_HOST);
        g_busUp = false;
    }

    // SDSPI_HOST_DEFAULT() is the IDF's own macro and does not list every
    // member of the struct it initialises, so it warns under the project's
    // warning settings. Not our bug and not one we can fix upstream, but a
    // warning that is always there is a warning nobody reads, so it is
    // silenced here and nowhere else.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t devDefaults = SDSPI_DEVICE_CONFIG_DEFAULT();
#pragma GCC diagnostic pop
    host.max_freq_khz = pins.speedKHz ? pins.speedKHz : 20000;
    spi_bus_config_t bus = {};
    bus.mosi_io_num     = pins.mosi;
    bus.miso_io_num     = pins.miso;
    bus.sclk_io_num     = pins.clk;
    bus.quadwp_io_num   = -1;
    bus.quadhd_io_num   = -1;
    bus.max_transfer_sz = 4000;

    if (!g_busUp) {
        esp_err_t be = spi_bus_initialize(static_cast<spi_host_device_t>(host.slot),
                                          &bus, SDSPI_DEFAULT_DMA);
        // INVALID_STATE means somebody else owns this bus. Carry on and try
        // the card, but do not record it as ours: freeing a bus this code did
        // not raise would take it out from under whatever did.
        if (be != ESP_OK && be != ESP_ERR_INVALID_STATE)
            return fail("SPI bus would not start: check the pin numbers");
        if (be == ESP_OK) { g_busUp = true; g_busPins = pins; }
    }

    sdspi_device_config_t dev = devDefaults;
    dev.gpio_cs = static_cast<gpio_num_t>(pins.cs);
    dev.host_id = static_cast<spi_host_device_t>(host.slot);

    esp_vfs_fat_sdmmc_mount_config_t cfg = {};
    // format_if_mount_failed stays false, deliberately. A card that does not
    // mount is far more often somebody's card with their files on it, in the
    // wrong format or badly seated, than a card that wants erasing. Wiping it
    // to make an error message go away is not a decision firmware gets to
    // make on a sysop's behalf.
    cfg.format_if_mount_failed = false;
    cfg.max_files              = BBS_SD_MAX_FILES;
    cfg.allocation_unit_size   = 16 * 1024;

    esp_err_t e = esp_vfs_fat_sdspi_mount(BBS_SD_MOUNT, &host, &dev, &cfg, &g_card);
    if (e != ESP_OK) {
        g_card = nullptr;
        // Put the bus back down. A failed mount that leaves the bus up holds
        // the old pins and makes the next attempt, with corrected wiring,
        // fail identically.
        if (g_busUp) { spi_bus_free(SDSPI_DEFAULT_HOST); g_busUp = false; }
        // Three different evenings, so three different messages. The error
        // code goes in the log as well: the first cut mapped everything that
        // was not a timeout or a bad filesystem onto one catch-all string,
        // and when a real card hit that branch the message named three things
        // to check and none of them was the problem.
        plat::log("sd: mount failed at %u kHz: %s (0x%x)",
                  static_cast<unsigned>(host.max_freq_khz), esp_err_to_name(e),
                  static_cast<unsigned>(e));
        if (e == ESP_ERR_TIMEOUT || e == ESP_ERR_NOT_FOUND)
            return fail("no card found: check it is seated, and the CS pin");
        if (e == ESP_FAIL)
            return fail("card found but no FAT filesystem: format it FAT32");
        // Out of memory is its own answer and must not be filed under
        // "check your wiring". The first version of this lumped it in with
        // everything that was not a timeout and told the sysop to try a
        // lower bus speed, which is advice that cannot work: the mount had
        // not got as far as the bus. Three attempts were spent on it.
        if (e == ESP_ERR_NO_MEM)
            return fail("not enough memory to mount the card: see MEM");
        char why[72];
        snprintf(why, sizeof(why), "card answered then failed (%s)", esp_err_to_name(e));
        return fail(why);
    }

    g_mount = true;
    g_speed = static_cast<uint32_t>(g_card->max_freq_khz);
    plat::log("sd: mounted %s at %s, %u MHz",
              g_card->is_mmc ? "MMC" : (g_card->ocr & (1u << 30)) ? "SDHC/SDXC" : "SDSC",
              BBS_SD_MOUNT, static_cast<unsigned>(g_speed / 1000));
    return true;
}

void sdUnmount() {
    if (!g_mount) return;
    esp_vfs_fat_sdcard_unmount(BBS_SD_MOUNT, g_card);
    // The bus comes down with it, but only if this code raised it.
    if (g_busUp) { spi_bus_free(SDSPI_DEFAULT_HOST); g_busUp = false; }
    g_card  = nullptr;
    g_mount = false;
    g_speed = 0;
    plat::log("sd: unmounted");
}

SdInfo sdInfo() {
    SdInfo i;
    if (!g_mount || !g_card) return i;
    i.mounted  = true;
    i.speedKHz = g_speed;
    snprintf(i.type, sizeof(i.type), "%s",
             g_card->is_mmc ? "MMC" : (g_card->ocr & (1u << 30)) ? "SDHC/SDXC" : "SDSC");

    // By mount point, not by drive number. The first version called
    // f_getfree("0:"), which is right only because this is the only FAT
    // volume on the board: the drive number comes from the first free slot
    // in the FATFS table, so "0:" was a coincidence rather than a contract.
    // esp_vfs_fat_info reads the drive out of the mount it was given and
    // cannot be wrong. It is also the cheap path: the failure mode of the
    // old call was a whole-FAT scan on a card whose free-cluster hint was
    // stale, which is what pulling a FAT card mid-write produces, and that
    // scan would have run on the DASH refresh timer.
    uint64_t total = 0, freeB = 0;
    if (esp_vfs_fat_info(BBS_SD_MOUNT, &total, &freeB) == ESP_OK) {
        i.totalKB = static_cast<uint32_t>(total / 1024ULL);
        i.freeKB  = static_cast<uint32_t>(freeB / 1024ULL);
    }
    return i;
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
uint32_t g_ledHold  = BBS_LED_PULSE_MS;   // how long the current show lasts
}

// ---------------------------------------------------------------------------
// Why we booted. esp_reset_reason is only meaningful before anything else
// resets it, so it is read once and kept.
// ---------------------------------------------------------------------------
esp_reset_reason_t g_reset = ESP_RST_UNKNOWN;
bool               g_resetRead = false;

void readReset() {
    if (!g_resetRead) { g_reset = esp_reset_reason(); g_resetRead = true; }
}

const char* resetReason() {
    readReset();
    switch (g_reset) {
        case ESP_RST_POWERON:  return "power on";
        case ESP_RST_EXT:      return "reset pin";
        case ESP_RST_SW:       return "software restart";
        case ESP_RST_PANIC:    return "crash (panic)";
        case ESP_RST_INT_WDT:  return "interrupt watchdog";
        case ESP_RST_TASK_WDT: return "task watchdog";
        case ESP_RST_WDT:      return "watchdog";
        case ESP_RST_BROWNOUT: return "brownout (power dipped)";
        case ESP_RST_DEEPSLEEP: return "woke from deep sleep";
        case ESP_RST_SDIO:     return "sdio";
        default:               return "unknown";
    }
}

bool resetWasCrash() {
    readReset();
    return g_reset == ESP_RST_PANIC || g_reset == ESP_RST_INT_WDT ||
           g_reset == ESP_RST_TASK_WDT || g_reset == ESP_RST_WDT ||
           g_reset == ESP_RST_BROWNOUT;
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
    g_ledHold  = BBS_LED_PULSE_MS;
    if (!g_ledOn) {
        g_ledOn = true;
        gpio_set_level(static_cast<gpio_num_t>(g_ledGpio), 1);
    }
}

void ledSignal(uint32_t now, uint32_t ms) {
    if (g_ledGpio < 0) return;
    g_ledSince = now;
    g_ledHold  = ms;
    g_ledOn    = true;
    gpio_set_level(static_cast<gpio_num_t>(g_ledGpio), 1);
}

void activityTick(uint32_t now) {
    if (g_ledOn && now - g_ledSince >= g_ledHold) {
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
