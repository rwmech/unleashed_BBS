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
 *                  esp_driver_gpio, esp_rom (ROM miniz tinfl), esp_wifi,
 *                  esp_partition and esp_system (the BOOT-hold reset),
 *                  esp_driver_rmt (the lights plugin's pixels)
 * Targets:      ESP32-WROOM-32E, ESP-IDF 5.3.1
 * See also:     README.md
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

#include "platform.h"
#include "../config.h"
#include "esp_timer.h"
#include "esp_system.h"      // esp_reset_reason
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_chip_info.h"   // hardware(): which chip
#include "esp_flash.h"       // hardware(): how much flash it really has
#include "esp_efuse.h"       // chipInfo(): the ESP32's package, from its fuses
#include "esp_private/esp_clk.h"   // chipInfo(): the CPU clock as it runs now
#if CONFIG_SPIRAM
#include "esp_psram.h"       // chipInfo(): the PSRAM chip's size
#endif
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_littlefs.h"
#include "driver/uart.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/rmt_tx.h"       // pixels: WS2812B on the RMT peripheral
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"   // stackFree, stackDeeper: the task's stack
#include "freertos/task.h"
#include "esp_attr.h"            // RTC_NOINIT_ATTR: the restart note
#include "esp_partition.h"       // factoryErase
#include "esp_task_wdt.h"        // factoryErase feeds the watchdog between partitions
#include "soc/soc_caps.h"        // the RMT's block size and DMA, per chip
#if SOC_USB_SERIAL_JTAG_SUPPORTED && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"      // the console on the S3's own USB
#include "driver/usb_serial_jtag_vfs.h"
#define BBS_CONSOLE_USJ 1
#endif
#ifdef BBS_SD_SDMMC1
#include "driver/sdmmc_host.h"           // a card slot wired for SDMMC (board.h)
#endif
#ifdef BBS_HAS_CAMERA
#include "ff.h"                          // sdList: FatFs's directory entries, sizes and all
#include "diskio_sdmmc.h"                // the card's drive number
#include "esp_camera.h"                  // Espressif's camera driver (Apache-2.0)
#include "jpge.h"                        // its JPEG encoder, for the watermark
#if CONFIG_IDF_TARGET_ESP32
#include "esp32/rom/tjpgd.h"             // the ROM's JPEG decoder: no flash
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/tjpgd.h"
#endif
#include <new>
#endif
#ifdef BBS_HAS_LCD
#include "esp_lcd_panel_io.h"            // the panel: esp_lcd over SPI
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_commands.h"
#include "driver/ledc.h"                 // its backlight, dimmed by PWM
#endif
extern "C" {
#include "miniz.h"
}

// The heap a sysop is shown and the loop watches. On a board with PSRAM the
// 8-bit heap is eight megabytes of it plus the internal RAM, and the internal
// RAM is what actually runs out (lwIP, Wi-Fi, a DMA buffer); a figure with
// the PSRAM in it would say "plenty" to the last byte. Without PSRAM the two
// are the same heap.
#if CONFIG_SPIRAM
#define BBS_HEAP_CAPS (MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)
#else
#define BBS_HEAP_CAPS MALLOC_CAP_8BIT
#endif
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

// ---------------------------------------------------------------------------
// LittleFS free space, remembered.
//
// esp_littlefs_info calls lfs_fs_size, which walks every block of every file
// (esp_littlefs.c:313 in the 1.22.3 component). On the board that is about
// 85 ms a partition with the BBS loop stopped: SYS asked for two and froze
// every caller for 170 ms, and DASH asked once a second. Rob felt it as "slow
// after BYE", because the sysop is who opens those screens. The host sums a
// few file sizes on Linux, so no host test could ever have shown it.
//
// So each figure is taken once and kept. The screens partition changes only
// when a backup restore or a filesystem upload rewrites it, and a restore
// calls fsInfoStale(). User data changes whenever anybody does anything, but
// its figure only feeds free-space displays and the plugins' reserve guard,
// and a figure up to a minute old serves both: the guard holds 32 KB back,
// and nothing a caller writes in a minute comes near that.
// ---------------------------------------------------------------------------
struct LfsFigure { uint32_t total = 0, used = 0, at = 0; };
static LfsFigure g_userFig, g_dataFig;
static constexpr uint32_t kUserInfoMs = 60000;

// lfsInfo: the kept figure, taken again when it is older than maxAgeMs
// (0 means keep it until fsInfoStale says otherwise).
static bool lfsInfo(const char* label, LfsFigure& f, uint32_t maxAgeMs,
                    uint32_t& total, uint32_t& used) {
    uint32_t now = millis();
    if (!f.at || (maxAgeMs && now - f.at >= maxAgeMs)) {
        size_t t = 0, u = 0;
        if (esp_littlefs_info(label, &t, &u) != ESP_OK) return false;
        f.total = static_cast<uint32_t>(t);
        f.used  = static_cast<uint32_t>(u);
        f.at    = now ? now : 1;
    }
    total = f.total;
    used  = f.used;
    return true;
}

void fsInfoStale() {
    g_dataFig.at = 0;
    g_userFig.at = 0;
}

bool userInfo(uint32_t& total, uint32_t& used) {
    return lfsInfo(BBS_USER_LABEL, g_userFig, kUserInfoMs, total, used);
}

const char* fsBase() {
    return BBS_FS_MOUNT;
}

const char* logsBase() {
    return BBS_LOGS_MOUNT;
}

const char* powerSave() {
    wifi_ps_type_t ps = WIFI_PS_NONE;
    if (esp_wifi_get_ps(&ps) != ESP_OK) return "?";
    switch (ps) {
        case WIFI_PS_NONE:       return "none";
        case WIFI_PS_MIN_MODEM:  return "min";
        case WIFI_PS_MAX_MODEM:  return "max";
        default:                 return "?";
    }
}

uint32_t heapFree() {
    // heap_caps_get_free_size sums a per-heap counter. No walk, no lock held
    // across a traversal, which is the whole reason this exists separately.
    return static_cast<uint32_t>(heap_caps_get_free_size(BBS_HEAP_CAPS));
}

uint32_t stackFree() {
    // Bytes. The kernel reports in StackType_t units, and in the IDF's
    // Xtensa port StackType_t is uint8_t (portSTACK_TYPE, portmacro.h), so
    // the unit is already a byte and the multiply is by one. It stays so the
    // figure is right on a port where it is not. This comment used to say
    // the mark was in words, which was never true here; believing it and
    // "fixing" the code to match would have made a tight stack read four
    // times roomier than it is, which is the failure direction that matters.
    return static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr)) * sizeof(StackType_t);
}

uint32_t stackSize() {
    return BBS_TASK_STACK;
}

// stackDeeper: read the band of fill just under the old mark, a word at a
// time. The kernel paints a new task's stack with 0xA5 (tskSTACK_FILL_BYTE)
// from pxTaskGetStackStart, the lowest address, upwards, and knownFree bytes
// of that were untouched when last measured, so the first written byte sat
// at lo + knownFree. BBS_STACK_BAND (512) is 128 word reads, a few
// microseconds, against the 100 or so a full stackFree costs reading
// several KB a byte at a time.
uint32_t stackDeeper(uint32_t knownFree) {
    constexpr uint32_t kBand = BBS_STACK_BAND;
    const uint8_t* lo = pxTaskGetStackStart(nullptr);
    if (!lo || knownFree < 4) return 0;
    uint32_t top  = knownFree & ~3u;                        // word aligned, inside the fill
    uint32_t base = top > kBand ? top - kBand : 0;
    for (uint32_t at = base; at < top; at += 4) {
        uint32_t v;
        memcpy(&v, lo + at, sizeof(v));                     // one aligned load
        if (v != 0xA5A5A5A5u) return stackFree();
    }
    return 0;
}

HeapStats heap() {
    HeapStats h;
    h.freeBytes    = static_cast<uint32_t>(heap_caps_get_free_size(BBS_HEAP_CAPS));
    h.minFree      = static_cast<uint32_t>(heap_caps_get_minimum_free_size(BBS_HEAP_CAPS));
    h.largestBlock = static_cast<uint32_t>(heap_caps_get_largest_free_block(BBS_HEAP_CAPS));
    h.totalBytes   = static_cast<uint32_t>(heap_caps_get_total_size(BBS_HEAP_CAPS));
    h.valid        = true;
    return h;
}

bool fsInfo(uint32_t& total, uint32_t& used) {
    return lfsInfo(BBS_FS_LABEL, g_dataFig, 0, total, used);   // see userInfo
}

// ---------------------------------------------------------------------------
// hardware: the chip, the flash this image can use and any PSRAM the
// firmware can use, for the directory's system badge. Flash is the size in
// the image header, which is what esp_flash_get_size returns in IDF 5.3.1
// (esp_flash_spi_init.c sets the default chip's size from it), not what the
// chip has: a 16 MB module running the 4 MB image says 4 MB. That is the
// honest figure for what this build can use, and it is kept on purpose.
// PSRAM is counted from the heap, so it too is named only when this build
// actually uses it.
// ---------------------------------------------------------------------------
static const char* chipFamily(esp_chip_model_t m) {
    switch (m) {
        case CHIP_ESP32S2: return "ESP32-S2";
        case CHIP_ESP32S3: return "ESP32-S3";
        case CHIP_ESP32C3: return "ESP32-C3";
        case CHIP_ESP32C2: return "ESP32-C2";
        case CHIP_ESP32C6: return "ESP32-C6";
        case CHIP_ESP32H2: return "ESP32-H2";
        case CHIP_ESP32P4: return "ESP32-P4";
        default:           return "ESP32";              // the ESP32 itself
    }
}

void hardware(char* out, size_t n) {
    static constexpr char kDot[] = " \xC2\xB7 ";   // a middle dot, spaced
    esp_chip_info_t chip = {};
    esp_chip_info(&chip);
    const char* model = chipFamily(chip.model);
    uint32_t flash = 0;
    if (esp_flash_get_size(nullptr, &flash) != ESP_OK) flash = 0;
    char size[16] = "";
    if (flash >= 1024u * 1024u)
        snprintf(size, sizeof(size), "%s%u MB", kDot, static_cast<unsigned>(flash / (1024u * 1024u)));
    else if (flash)
        snprintf(size, sizeof(size), "%s%u KB", kDot, static_cast<unsigned>(flash / 1024u));
    const bool psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
    snprintf(out, n, "%s%s%s%s", model, size, psram ? kDot : "", psram ? "PSRAM" : "");
}

// ---------------------------------------------------------------------------
// chipInfo: see platform.h. On the ESP32 the package is in the fuses
// (esp_efuse_get_pkg_ver, EFUSE_RD_CHIP_VER_PKG_* in soc/efuse_defs.h), so
// the model says which die and package; other chips give their family. The
// CPU clock is esp_clk_cpu_freq, which follows every switch of the clock, so
// 240 here means the chip is running at 240, whatever the sdkconfig asked
// for. Flash as hardware() gives it. PSRAM: the chip's size from esp_psram
// (8 MB on a WROVER-E or an ESP32-CAM), and what the heap was given of it,
// which on the ESP32 is at most the 4 MB it can map.
// ---------------------------------------------------------------------------
void chipInfo(ChipInfo& o) {
    o = ChipInfo();
    esp_chip_info_t chip = {};
    esp_chip_info(&chip);
    const char* model = chipFamily(chip.model);
#if CONFIG_IDF_TARGET_ESP32
    // The names esptool prints for the same fuse (esptool 4.5.1,
    // targets/esp32.py get_chip_description), so HARDWARE agrees with the
    // installer's log: a WROOM-32E is "ESP32-D0WD-V3", not the IDF
    // constant's name (D0WDQ5, never a sold part). One core means the
    // single-core S0 die; revision 3 adds "-V3" to a D0WD and makes a
    // PICO-D4 a PICO-V3.
    const bool single = chip.cores == 1;
    const bool rev3   = chip.revision / 100 == 3;
    const char* pkg   = nullptr;
    switch (esp_efuse_get_pkg_ver()) {
        case 0:  pkg = single ? "ESP32-S0WDQ6" : "ESP32-D0WDQ6";  break;
        case 1:  pkg = single ? "ESP32-S0WD"   : "ESP32-D0WD";    break;
        case 2:  pkg = "ESP32-D2WD";                              break;
        case 4:  pkg = "ESP32-U4WDH";                             break;
        case 5:  pkg = rev3 ? "ESP32-PICO-V3" : "ESP32-PICO-D4";  break;
        case 6:  pkg = "ESP32-PICO-V3-02";                        break;
        case 7:  pkg = "ESP32-D0WDR2-V3";                         break;
        default: break;
    }
    if (pkg) {
        const bool v3 = rev3 && !strncmp(pkg, "ESP32-D0WD", 10) && !strstr(pkg, "-V3");
        snprintf(o.model, sizeof(o.model), "%s%s", pkg, v3 ? "-V3" : "");
    } else {
        snprintf(o.model, sizeof(o.model), "%s", model);
    }
#else
    snprintf(o.model, sizeof(o.model), "%s", model);
#endif
    o.rev    = chip.revision;
    o.cores  = chip.cores;
    o.cpuMHz = static_cast<uint16_t>(esp_clk_cpu_freq() / 1000000);
    uint32_t flash = 0;
    if (esp_flash_get_size(nullptr, &flash) == ESP_OK) o.flash = flash;
#if CONFIG_SPIRAM
    o.psram = static_cast<uint32_t>(esp_psram_get_size());
#endif
    o.psramHeap = static_cast<uint32_t>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    o.psramFree = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    o.heapFree  = static_cast<uint32_t>(heap_caps_get_free_size(BBS_HEAP_CAPS));
    o.heapLow   = static_cast<uint32_t>(heap_caps_get_minimum_free_size(BBS_HEAP_CAPS));
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

static void sdInfoStale();                        // defined with sdInfo below

bool sdMount(const SdPins& pins, char* err, size_t errLen) {
    auto fail = [&](const char* why) {
        if (err && errLen) snprintf(err, errLen, "%s", why);
        return false;
    };
    if (err && errLen) err[0] = '\0';
    if (g_mount) return true;                      // already up, nothing to do
    // Whatever was cached describes a card that is not this one.
    sdInfoStale();
#ifdef BBS_SD_SDMMC1
    // A slot wired for SDMMC (board.h, BBS_SD_SDMMC1): the SDMMC host, one
    // data line, on the profile's pins. On the ESP32 the host's slot 1 is on
    // the IO MUX and can only be CLK 14, CMD 15, D0 2, so the profile's pins
    // are checked against that at compile time; on a chip that routes the
    // host through the GPIO matrix (the S3) they are the pins it uses. One
    // line, not four: on the Freenove D1 is a camera line and D2 is GPIO 12,
    // the flash-voltage strap, which a card's pull-up would hold high at
    // reset. Everything after the mount (FAT at /sd, sdInfo, unmount) is the
    // SPI path's, unchanged: both end in the same VFS.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    sdmmc_host_t        host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
#pragma GCC diagnostic pop
    host.max_freq_khz = pins.speedKHz ? pins.speedKHz : 20000;
    slot.width        = 1;
#if SOC_SDMMC_USE_GPIO_MATRIX
    slot.clk = static_cast<gpio_num_t>(BBS_SDMMC_CLK);
    slot.cmd = static_cast<gpio_num_t>(BBS_SDMMC_CMD);
    slot.d0  = static_cast<gpio_num_t>(BBS_SDMMC_D0);
    slot.d1  = GPIO_NUM_NC;
    slot.d2  = GPIO_NUM_NC;
    slot.d3  = GPIO_NUM_NC;
#else
    static_assert(BBS_SDMMC_CLK == 14 && BBS_SDMMC_CMD == 15 && BBS_SDMMC_D0 == 2,
                  "the ESP32's SDMMC slot 1 is fixed: CLK 14, CMD 15, D0 2");
#endif
    // The chip's weak pull-ups on top of whatever the board fits. They are
    // released at reset, so GPIO 2's does not reach the download-mode strap.
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
#else
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
#endif

    esp_vfs_fat_sdmmc_mount_config_t cfg = {};
    // format_if_mount_failed stays false, deliberately. A card that does not
    // mount is far more often somebody's card with their files on it, in the
    // wrong format or badly seated, than a card that wants erasing. Wiping it
    // to make an error message go away is not a decision firmware gets to
    // make on a sysop's behalf.
    cfg.format_if_mount_failed = false;
    cfg.max_files              = BBS_SD_MAX_FILES;
    cfg.allocation_unit_size   = 16 * 1024;

#ifdef BBS_SD_SDMMC1
    esp_err_t e = esp_vfs_fat_sdmmc_mount(BBS_SD_MOUNT, &host, &slot, &cfg, &g_card);
#else
    esp_err_t e = esp_vfs_fat_sdspi_mount(BBS_SD_MOUNT, &host, &dev, &cfg, &g_card);
#endif
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
#ifdef BBS_SD_SDMMC1
        if (e == ESP_ERR_TIMEOUT || e == ESP_ERR_NOT_FOUND)
            return fail("no card found: check it is seated");
#else
        if (e == ESP_ERR_TIMEOUT || e == ESP_ERR_NOT_FOUND)
            return fail("no card found: check it is seated, and the CS pin");
#endif
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
    sdInfoStale();                                 // the figures die with the card
    esp_vfs_fat_sdcard_unmount(BBS_SD_MOUNT, g_card);
    // The bus comes down with it, but only if this code raised it.
    if (g_busUp) { spi_bus_free(SDSPI_DEFAULT_HOST); g_busUp = false; }
    g_card  = nullptr;
    g_mount = false;
    g_speed = 0;
    plat::log("sd: unmounted");
}

// How long a free-space reading is considered current. Three seconds is
// long enough that a refresh screen redrawing once a second does not pay
// for it every frame, and short enough that a sysop watching a transfer
// sees the figure move.
static constexpr uint32_t kSdInfoMs = 3000;
static SdInfo   g_sdInfo;
static uint32_t g_sdInfoAt = 0;          // 0 means nothing cached

// sdInfoStale: called by mount and unmount. The figures are about a card,
// so they stop meaning anything the moment the card does.
static void sdInfoStale() { g_sdInfoAt = 0; g_sdInfo = SdInfo(); }

SdInfo sdInfo() {
    uint32_t now = millis();
    if (g_sdInfoAt && now - g_sdInfoAt < kSdInfoMs) return g_sdInfo;

    SdInfo i;
    if (!g_mount || !g_card) {
        g_sdInfo = i;
        g_sdInfoAt = now ? now : 1;
        return i;
    }
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
    g_sdInfo   = i;
    g_sdInfoAt = now ? now : 1;
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
// The console as a byte pipe, for Improv (main.cpp). See platform.h.
// ===========================================================================
#if BBS_CONSOLE_USJ
// The chip's own USB. Installing the driver takes the port over from the
// console's polling path, so the console is pointed at the driver too:
// otherwise log lines would be written straight into the hardware FIFO
// while the driver's interrupt fed it Improv packets from its own buffer,
// and the two would interleave on the wire. Through the driver, a port
// nobody is reading costs one 50 ms wait and then drops (the IDF's
// usbjtag_tx_char_via_driver), so a board on a phone charger does not
// stall on its own log.
bool consoleBegin() {
    usb_serial_jtag_driver_config_t c = {};
    c.tx_buffer_size = 512;         // an Improv result is at most ~270 bytes
    c.rx_buffer_size = 256;
    if (usb_serial_jtag_driver_install(&c) != ESP_OK) return false;
    usb_serial_jtag_vfs_use_driver();
    return true;
}

size_t consoleRead(uint8_t* buf, size_t cap) {
    int n = usb_serial_jtag_read_bytes(buf, static_cast<uint32_t>(cap), 0);
    return n > 0 ? static_cast<size_t>(n) : 0;
}

// All or nothing: the driver's buffer is a byte ring that either takes the
// whole packet or refuses it, so a packet is never half sent. 20 ms is room
// for a host that is reading; a port nobody is reading refuses at once
// after that, and the installer asks again.
void consoleWrite(const uint8_t* b, size_t n) {
    usb_serial_jtag_write_bytes(b, n, pdMS_TO_TICKS(20));
}
#else
// UART0, through the board's USB-serial bridge. The log keeps writing the
// way it always has; only input changes hands, and nothing else here ever
// read the console.
bool consoleBegin() {
    return uart_driver_install(UART_NUM_0, 256, 0, 0, nullptr, 0) == ESP_OK;
}

size_t consoleRead(uint8_t* buf, size_t cap) {
    int n = uart_read_bytes(UART_NUM_0, buf, static_cast<uint32_t>(cap), 0);
    return n > 0 ? static_cast<size_t>(n) : 0;
}

void consoleWrite(const uint8_t* b, size_t n) {
    uart_write_bytes(UART_NUM_0, b, n);
}
#endif

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
int8_t   g_ledForce = -1;                 // ledOverride: -1 traffic drives it, 0 off, 1 on
bool     g_ledQuiet = false;              // ledSilent: silent mode, traffic shows nothing
// ledLevel: the pin level for on or off. The board's own LED may light on a
// low pin (board.h, BBS_LED_ACTIVE_LOW); an LED a sysop wired elsewhere is
// taken to light on a high one. Folds to the plain level on every board
// whose LED is active high.
inline uint32_t ledLevel(bool on) {
    return (on ? 1u : 0u) ^ ((BBS_LED_ACTIVE_LOW && g_ledGpio == BBS_LED_GPIO) ? 1u : 0u);
}
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

// No case for ESP_RST_EXT. The IDF documents it as not applicable to the
// ESP32, the S3 does not return it either, and the RESET (EN) button reads
// as a power on on both chips, as does the reset at the end of a flash over
// a USB-serial bridge. Its old words, "reset pin", said otherwise; should a
// chip ever return it, "unknown" is at least not a wrong answer.
// ESP_RST_USB is the S3's reset through its own USB port, which is how it
// comes back after every flash there. IDF 5.3.1 has it in the enum on every
// target, so it needs no guard; the ESP32 simply never returns it.
const char* resetReason() {
    readReset();
    switch (g_reset) {
        case ESP_RST_POWERON:  return "power on";
        case ESP_RST_USB:      return "USB reset";
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

// activityLedBegin: once per pin. app_main brings the LED up straight after
// the settings are read, before the network, because the BOOT-hold watch
// shows its stages on it from the first seconds (1.1.0); Bbs::begin asks
// again for the same pin later and that call is a no-op. Driving GPIO2 this
// early is fine: it is a strapping pin only while reset is released, and by
// the time any code runs the ROM has read it.
void activityLedBegin(int gpio) {
    if (gpio >= 0 && gpio == g_ledGpio) return;          // already up
    if (gpio < 0 || gpio >= GPIO_NUM_MAX) { log("led: activity LED off"); return; }
    gpio_config_t c = {};
    c.pin_bit_mask = 1ULL << gpio;
    c.mode         = GPIO_MODE_OUTPUT;
    c.intr_type    = GPIO_INTR_DISABLE;
    if (gpio_config(&c) != ESP_OK) { log("led: gpio %d config failed", gpio); return; }
    g_ledGpio = gpio;
    gpio_set_level(static_cast<gpio_num_t>(gpio), ledLevel(g_ledForce > 0));
    log("led: activity LED on gpio %d", gpio);
}

void ledOverride(int8_t state) {
    g_ledForce = state < 0 ? -1 : (state ? 1 : 0);
    if (g_ledGpio < 0) return;
    if (g_ledForce < 0) g_ledOn = false;                  // handed back dark
    gpio_set_level(static_cast<gpio_num_t>(g_ledGpio), ledLevel(g_ledForce > 0));
}

void ledSilent(bool on) {
    g_ledQuiet = on;
    if (!on || g_ledGpio < 0 || g_ledForce >= 0) return;   // the override keeps what it shows
    g_ledOn = false;
    gpio_set_level(static_cast<gpio_num_t>(g_ledGpio), ledLevel(false));
}

void activityPulse(uint32_t now) {
    if (g_ledGpio < 0 || g_ledForce >= 0 || g_ledQuiet) return;
    g_ledSince = now;
    g_ledHold  = BBS_LED_PULSE_MS;
    if (!g_ledOn) {
        g_ledOn = true;
        gpio_set_level(static_cast<gpio_num_t>(g_ledGpio), ledLevel(true));
    }
}

void ledSignal(uint32_t now, uint32_t ms) {
    if (g_ledGpio < 0 || g_ledForce >= 0 || g_ledQuiet) return;
    g_ledSince = now;
    g_ledHold  = ms;
    g_ledOn    = true;
    gpio_set_level(static_cast<gpio_num_t>(g_ledGpio), ledLevel(true));
}

void activityTick(uint32_t now) {
    if (g_ledForce >= 0) return;
    if (g_ledOn && now - g_ledSince >= g_ledHold) {
        g_ledOn = false;
        gpio_set_level(static_cast<gpio_num_t>(g_ledGpio), ledLevel(false));
    }
}

// ===========================================================================
// Recovery (1.1.0): the BOOT button, the factory erase, and a restart that
// says why
// ===========================================================================

#if BBS_BOOT_GPIO >= 0
namespace {
bool g_bootPinUp = false;
}
#endif

// bootButtonDown: GPIO0, active low. Pulled up here as well as on the board,
// because a module on a carrier with no BOOT button has nothing else holding
// the pin, and a floating GPIO0 read as a press for 7 s would reset the
// sysop password.
bool bootButtonDown(uint32_t) {
#if BBS_BOOT_GPIO < 0
    return false;                                 // no BOOT button on this board (board.h)
#else
    if (!g_bootPinUp) {
        gpio_config_t c = {};
        c.pin_bit_mask = 1ULL << BBS_BOOT_GPIO;
        c.mode         = GPIO_MODE_INPUT;
        c.pull_up_en   = GPIO_PULLUP_ENABLE;
        c.pull_down_en = GPIO_PULLDOWN_DISABLE;
        c.intr_type    = GPIO_INTR_DISABLE;
        if (gpio_config(&c) != ESP_OK) return false;
        g_bootPinUp = true;
    }
    return gpio_get_level(static_cast<gpio_num_t>(BBS_BOOT_GPIO)) == 0;
#endif
}

// factoryErase: each partition taken out of the VFS first, so nothing can
// read a filesystem being erased under it, then erased whole. Whole, not
// formatted: a LittleFS format writes a new superblock and leaves every old
// block readable on the chip, and a factory reset of somebody's accounts
// should not leave their password hashes there. The watchdog is fed between
// the two, because an erase of 608 KB is seconds of work; the erase itself
// yields (CONFIG_SPI_FLASH_YIELD_DURING_ERASE), so the idle task still runs.
// feedIfWatched: esp_task_wdt_reset from a task the watchdog is not watching
// logs "task not found" as an error, and a release before the BBS loop
// starts (a board still waiting for its network) is exactly that.
static void feedIfWatched() {
    if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
}

bool factoryErase(char* err, size_t errLen) {
    const char* const labels[] = { BBS_USER_LABEL, BBS_LOGS_LABEL };
    for (const char* label : labels) {
        const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                            ESP_PARTITION_SUBTYPE_ANY, label);
        if (!p) { snprintf(err, errLen, "%s", label); return false; }
        esp_vfs_littlefs_unregister(label);             // not mounted is fine too
        feedIfWatched();
        if (esp_partition_erase_range(p, 0, p->size) != ESP_OK) {
            snprintf(err, errLen, "%s", label);
            return false;
        }
        feedIfWatched();
    }
    return true;
}

// The note survives a software restart in RTC slow memory, which the startup
// code leaves alone (RTC_NOINIT_ATTR) and which costs no DRAM. A magic word
// beside it, because after a power cut that memory holds whatever it holds.
namespace {
constexpr uint32_t kNoteMagic = 0x55A7B007u;
RTC_NOINIT_ATTR uint32_t g_noteMagic;
RTC_NOINIT_ATTR uint32_t g_noteValue;
int16_t g_noteRead = -1;                // what restartNote found, -1 not looked yet
}

void restart(uint8_t note) {
    g_noteValue = note;
    g_noteMagic = note ? kNoteMagic : 0;
    fflush(stdout);
    esp_restart();
}

uint8_t restartNote() {
    if (g_noteRead < 0) {
        readReset();
        bool mine = g_reset == ESP_RST_SW && g_noteMagic == kNoteMagic && g_noteValue < 256;
        g_noteRead = mine ? static_cast<int16_t>(g_noteValue) : 0;
        g_noteMagic = 0;                // said once
    }
    return static_cast<uint8_t>(g_noteRead);
}

// ===========================================================================
// diskPulse: storage was touched. A time and a count, nothing lit here: see
// platform.h. Only ever called from the BBS task.
// ===========================================================================

namespace {
uint32_t g_diskAt[DISK_KINDS]    = {};
uint16_t g_diskCount[DISK_KINDS] = {};
}

void diskPulse(DiskKind kind) {
    if (kind >= DISK_KINDS) return;
    g_diskAt[kind] = millis();
    ++g_diskCount[kind];
}

DiskSeen diskSeen() {
    DiskSeen d;
    for (uint8_t i = 0; i < DISK_KINDS; ++i) {
        d.at[i]    = g_diskAt[i];
        d.count[i] = g_diskCount[i];
    }
    return d;
}

// ===========================================================================
// Pixels: WS2812B on the RMT peripheral (esp_driver_rmt, driver/rmt_tx.h).
//
// One TX channel and one simple encoder per output. rmt_transmit is called
// with queue_nonblocking, and nothing here ever waits on it except
// pixelsEnd: the done interrupt clears a flag, and a frame offered while the
// last is still on the wire is refused rather than queued, because the
// plugin draws a fresh one 20 ms later anyway and a queued frame is a stale
// one.
// ===========================================================================

namespace {

// 20 MHz: a tick is 50 ns and a bit is 25 ticks, 1.25 us, exactly the
// 800 kHz the WS2812B datasheet asks for. Its timings, each +-150 ns: a 0 is
// 0.40 us high then 0.85 us low, a 1 is 0.80 us high then 0.45 us low.
constexpr uint32_t kPixHz = 20u * 1000u * 1000u;
constexpr uint16_t kT0H = 8, kT0L = 17, kT1H = 16, kT1L = 9;
// The latch that ends a frame: low for at least 50 us on the original part
// and 280 us on the V5 revision sold since, and a strip does not say which
// it is. 300 us serves both, as two halves of one symbol, since a duration
// field holds at most 32,767 ticks.
constexpr uint16_t kLatchHalf = 3000;

struct PixOut {
    rmt_channel_handle_t chan  = nullptr;
    rmt_encoder_handle_t enc   = nullptr;
    int8_t               pin   = -1;
    uint8_t              count = 0;
    uint8_t              order = PIX_GRB;           // kPixWire's row for this output
    bool                 sent  = false;             // grb holds a frame that went out
    volatile bool        busy  = false;             // set here, cleared by the done interrupt
    uint8_t              grb[kPixelMax * 3] = {};   // the wire bytes, in order, kept until sent
};

// The whole frame fits the channel's memory with nothing refilled part way,
// on every chip, for the longest output there can be. The ESP32 has eight
// 64-symbol blocks and the drive light takes one; the S3 sends its strip by
// DMA and so is not bounded by its blocks at all (see pixelsBegin).
static_assert(SOC_RMT_MEM_WORDS_PER_CHANNEL >= 1 * 24 + 2,
              "the drive light's one pixel fits one block");
#if !SOC_RMT_SUPPORT_DMA
static_assert(static_cast<size_t>(kPixelMax) * 24u + 2u <=
              (SOC_RMT_TX_CANDIDATES_PER_GROUP - 1u) * SOC_RMT_MEM_WORDS_PER_CHANNEL,
              "the longest strip no longer fits the channel memory beside the drive light");
#endif
PixOut            g_pix[kPixelOuts];
rmt_symbol_word_t g_bit0, g_bit1, g_latch;
bool              g_symbols = false;

void pixSymbols() {
    if (g_symbols) return;
    g_bit0.level0  = 1; g_bit0.duration0  = kT0H; g_bit0.level1  = 0; g_bit0.duration1  = kT0L;
    g_bit1.level0  = 1; g_bit1.duration0  = kT1H; g_bit1.level1  = 0; g_bit1.duration1  = kT1L;
    g_latch.level0 = 0; g_latch.duration0 = kLatchHalf;
    g_latch.level1 = 0; g_latch.duration1 = kLatchHalf;
    g_symbols = true;
}

// pixEncode: bytes to symbols, most significant bit first, then the latch.
// Whole bytes only, so symbols already written divided by 8 is exactly how
// many bytes are done. The driver calls it again with more room, or with its
// own overflow buffer, whenever it returns short.
size_t pixEncode(const void* data, size_t size, size_t written, size_t room,
                 rmt_symbol_word_t* out, bool* done, void* arg) {
    (void)arg;
    const uint8_t* b = static_cast<const uint8_t*>(data);
    size_t at = written / 8u;
    size_t n  = 0;
    while (at < size && room - n >= 8u) {
        for (uint8_t m = 0x80; m; m = static_cast<uint8_t>(m >> 1))
            out[n++] = (b[at] & m) ? g_bit1 : g_bit0;
        ++at;
    }
    if (at >= size && room - n >= 1u) {
        out[n++] = g_latch;
        *done = true;
    }
    return n;
}

// pixDone: the frame has left. The interrupt's only job here.
bool pixDone(rmt_channel_handle_t chan, const rmt_tx_done_event_data_t* ev, void* ctx) {
    (void)chan;
    (void)ev;
    static_cast<PixOut*>(ctx)->busy = false;
    return false;
}

bool pixSend(PixOut& p) {
    rmt_transmit_config_t tc = {};
    tc.loop_count               = 0;
    tc.flags.eot_level          = 0;       // the line rests low, which a pixel ignores
    tc.flags.queue_nonblocking  = 1;
    p.busy = true;
    if (rmt_transmit(p.chan, p.enc, p.grb, static_cast<size_t>(p.count) * 3u, &tc) != ESP_OK) {
        p.busy = false;
        p.sent = false;          // grb holds a frame that never left: send it again
        return false;
    }
    p.sent = true;
    return true;
}

void pixForget(PixOut& p) {
    p.chan  = nullptr;
    p.enc   = nullptr;
    p.pin   = -1;
    p.count = 0;
    p.order = PIX_GRB;
    p.sent  = false;
    p.busy  = false;
}

}   // namespace

bool pixelsBegin(uint8_t out, int pin, uint8_t count, uint8_t order) {
    if (out >= kPixelOuts || !count || count > kPixelMax || order >= PIX_ORDERS) return false;
    PixOut& p = g_pix[out];
    if (p.chan) pixelsEnd(out);
    if (pin < 0 || !GPIO_IS_VALID_OUTPUT_GPIO(pin)) return false;
    pixSymbols();

    rmt_tx_channel_config_t cc = {};
    cc.gpio_num      = static_cast<gpio_num_t>(pin);
    cc.clk_src       = RMT_CLK_SRC_DEFAULT;
    cc.resolution_hz = kPixHz;
    // The whole frame fits the channel's own memory, so it goes out without
    // the interrupt refilling it part way. A refill late by more than a bit
    // time, which one Wi-Fi interrupt can manage, stretches a low into a
    // latch and the strip shows half a frame. 24 symbols a pixel, the latch
    // and the driver's end marker, in whole blocks of the chip's size: 64
    // symbols on the ESP32, 48 on the S3 (1.1.0; this rounded to 64 on every
    // chip, which the S3's driver then rounded up again to two of its
    // blocks for a one-pixel drive light).
    constexpr size_t kBlock = SOC_RMT_MEM_WORDS_PER_CHANNEL;
    size_t need = static_cast<size_t>(count) * 24u + 2u;
    cc.mem_block_symbols = (need + kBlock - 1u) / kBlock * kBlock;
    cc.trans_queue_depth = 2;
#if SOC_RMT_SUPPORT_DMA
    // The strip on a chip whose RMT has DMA (the S3, TX channel 3 only):
    // the frame is encoded whole into a DMA buffer of its own size, the same
    // no-refill property with the DMA feeding the channel, so the strip is
    // not bounded by the eight small blocks. The drive light's one pixel
    // stays in a block of channel memory, which leaves the DMA channel free.
    // mem_block_symbols is the DMA buffer here: even, and at least a block.
    const bool dma = out == 1;
    if (dma) {
        cc.flags.with_dma    = 1;
        cc.mem_block_symbols = need < kBlock ? kBlock : (need + 1u) & ~static_cast<size_t>(1);
    }
#endif

    rmt_simple_encoder_config_t ec = {};
    ec.callback = pixEncode;

    rmt_tx_event_callbacks_t cb = {};
    cb.on_trans_done = pixDone;

    esp_err_t e = rmt_new_tx_channel(&cc, &p.chan);
#if SOC_RMT_SUPPORT_DMA
    // No DMA to be had (another driver holds every GDMA channel): channel
    // memory, if the frame fits the TX blocks the drive light leaves (three
    // of the S3's four, 144 symbols: five pixels). A longer strip is said to
    // be what it is, rather than sent off to check its wiring.
    if (e != ESP_OK && dma) {
        constexpr size_t kLeft = (SOC_RMT_TX_CANDIDATES_PER_GROUP - 1u) * kBlock;
        if (need > kLeft) {
            pixForget(p);
            log("lights: no DMA channel free for the strip (%s), and %u pixels need it",
                esp_err_to_name(e), static_cast<unsigned>(count));
            return false;
        }
        cc.flags.with_dma    = 0;
        cc.mem_block_symbols = (need + kBlock - 1u) / kBlock * kBlock;
        log("lights: no DMA for the strip (%s), trying channel memory", esp_err_to_name(e));
        e = rmt_new_tx_channel(&cc, &p.chan);
    }
#endif
    if (e != ESP_OK) {
        pixForget(p);
        log("lights: gpio %d would not take an RMT channel (%s)", pin, esp_err_to_name(e));
        return false;
    }
    e = rmt_new_simple_encoder(&ec, &p.enc);
    if (e == ESP_OK) e = rmt_tx_register_event_callbacks(p.chan, &cb, &p);
    if (e == ESP_OK) e = rmt_enable(p.chan);
    if (e != ESP_OK) {
        if (p.enc) rmt_del_encoder(p.enc);
        rmt_del_channel(p.chan);
        pixForget(p);
        log("lights: gpio %d would not start (%s)", pin, esp_err_to_name(e));
        return false;
    }
    p.pin   = static_cast<int8_t>(pin);
    p.count = count;
    p.order = order;
    p.sent  = false;
    p.busy  = false;
    memset(p.grb, 0, sizeof(p.grb));
    return true;
}

void pixelsEnd(uint8_t out) {
    if (out >= kPixelOuts) return;
    PixOut& p = g_pix[out];
    if (!p.chan) return;
    // Dark before the pin goes: a strip keeps the last frame it latched for
    // as long as it has power, so leaving without one leaves it lit.
    rmt_tx_wait_all_done(p.chan, 20);
    memset(p.grb, 0, sizeof(p.grb));
    if (pixSend(p)) rmt_tx_wait_all_done(p.chan, 20);
    rmt_disable(p.chan);
    rmt_del_channel(p.chan);                      // hands the pin back to plain GPIO
    rmt_del_encoder(p.enc);
    int pin = p.pin;
    pixForget(p);
    // And hold it low rather than floating, so noise on a long data wire is
    // not read as the start of a frame.
    gpio_config_t c = {};
    c.pin_bit_mask = 1ULL << pin;
    c.mode         = GPIO_MODE_OUTPUT;
    c.intr_type    = GPIO_INTR_DISABLE;
    if (gpio_config(&c) == ESP_OK) gpio_set_level(static_cast<gpio_num_t>(pin), 0);
}

bool pixelsShow(uint8_t out, const uint8_t* rgb, uint8_t count) {
    if (out >= kPixelOuts || !rgb) return false;
    PixOut& p = g_pix[out];
    if (!p.chan) return false;
    if (count > p.count) count = p.count;
    uint8_t grb[kPixelMax * 3] = {};
    const uint8_t* w = kPixWire[p.order];
    for (uint8_t i = 0; i < count; ++i) {
        grb[i * 3]     = rgb[i * 3 + w[0]];
        grb[i * 3 + 1] = rgb[i * 3 + w[1]];
        grb[i * 3 + 2] = rgb[i * 3 + w[2]];
    }
    const size_t len = static_cast<size_t>(p.count) * 3u;
    if (p.sent && !memcmp(grb, p.grb, len)) return true;      // nothing new to say
    if (p.busy) return false;
    memcpy(p.grb, grb, len);
    return pixSend(p);
}

uint8_t pixelsFrame(uint8_t out, uint8_t* rgb, uint8_t cap) {
    if (out >= kPixelOuts || !rgb) return 0;
    const PixOut& p = g_pix[out];
    if (!p.chan) return 0;
    uint8_t n = p.count < cap ? p.count : cap;
    const uint8_t* w = kPixWire[p.order];
    for (uint8_t i = 0; i < n; ++i)
        for (uint8_t k = 0; k < 3; ++k) rgb[i * 3 + w[k]] = p.grb[i * 3 + k];
    return n;
}

#ifdef BBS_HAS_LCD
// ===========================================================================
// The panel (BBS_HAS_LCD): an ST7789 through the IDF's esp_lcd, on SPI3.
//
// SPI3 because the SD card's SPI mode takes SPI2 (SDSPI_DEFAULT_HOST), and
// the S3 has exactly those two for general use; Waveshare's demo puts the
// panel on SPI3 as well.
//
// Never waiting in the loop. esp_lcd sends colour data as a queued DMA
// transaction and returns, but the address commands in front of it are
// polled and first wait for anything still queued, so lcdDraw is only
// accepted once the last one has finished: the done interrupt clears a
// flag, lcdReady reads it. The colour data goes from a staging buffer in
// internal DMA memory, allocated here at begin, because the SPI driver
// copies anything it cannot DMA from (PSRAM, where the framebuffer is) into
// a buffer it allocates per transaction, which would be heap in the loop.
// ===========================================================================
namespace {

// A band: 16 rows of a 320-pixel line, 10 KB. About 8 ms on the wire at
// 10 MHz and 2 ms at 40, one per plugin tick.
constexpr uint32_t kBandPixels = 320u * 16u;

struct Lcd {
    esp_lcd_panel_io_handle_t io     = nullptr;
    esp_lcd_panel_handle_t    panel  = nullptr;
    uint16_t*                 stage  = nullptr;
    bool                      busUp  = false;
    bool                      blUp   = false;
    volatile bool             busy   = false;   // a band is on the wire
    LcdCfg                    cfg;
};
Lcd g_lcd;

bool lcdDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    g_lcd.busy = false;
    return false;
}

// The panel's own settings after the controller's reset defaults, from
// Waveshare's demo for this module (its Vernon_ST7789T driver's init table:
// porch, gate and VCOM voltages, power, and the two gamma curves). The IDF's
// generic ST7789 init sends only sleep-out, MADCTL, COLMOD and RAMCTRL, which
// lights the panel with the controller's defaults; these are what the panel
// maker tuned it to. Register values, sent as the demo sends them.
struct LcdInit { uint8_t cmd; uint8_t n; uint8_t data[14]; };
const LcdInit kLcdInit[] = {
    { 0xB2, 5,  { 0x0C, 0x0C, 0x00, 0x33, 0x33 } },                   // porch
    { 0xB7, 1,  { 0x75 } },                                           // gate voltages
    { 0xBB, 1,  { 0x1A } },                                           // VCOM
    { 0xC0, 1,  { 0x80 } },                                           // LCM control
    { 0xC2, 2,  { 0x01, 0xFF } },                                     // VDV/VRH enable
    { 0xC3, 1,  { 0x13 } },                                           // VRH
    { 0xC4, 1,  { 0x20 } },                                           // VDV
    { 0xC6, 1,  { 0x0F } },                                           // frame rate, 60 Hz
    { 0xD0, 2,  { 0xA4, 0xA1 } },                                     // power control
    { 0xE0, 14, { 0xD0, 0x0D, 0x14, 0x0D, 0x0D, 0x09, 0x38, 0x44, 0x4E, 0x3A, 0x17, 0x18, 0x2F, 0x30 } },
    { 0xE1, 14, { 0xD0, 0x09, 0x0F, 0x08, 0x07, 0x14, 0x37, 0x44, 0x4D, 0x38, 0x15, 0x16, 0x2C, 0x2E } },
};

void lcdBlSet(uint8_t pct) {
    if (!g_lcd.blUp) return;
    uint32_t duty = pct >= 100 ? 8191u : static_cast<uint32_t>(pct) * 8191u / 100u;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

bool lcdFail(char* err, size_t n, const char* why, esp_err_t e) {
    if (err && n) snprintf(err, n, "%s (%s)", why, esp_err_to_name(e));
    log("panel: %s (%s)", why, esp_err_to_name(e));
    lcdEnd();
    return false;
}

}   // namespace

bool lcdBegin(const LcdCfg& c, char* err, size_t errLen) {
    lcdEnd();
    if (err && errLen) err[0] = '\0';
    esp_err_t e;

    spi_bus_config_t bus = {};
    bus.mosi_io_num     = c.mosi;
    bus.miso_io_num     = -1;
    bus.sclk_io_num     = c.sclk;
    bus.quadwp_io_num   = -1;
    bus.quadhd_io_num   = -1;
    bus.max_transfer_sz = static_cast<int>(kBandPixels * 2u);
    e = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK) return lcdFail(err, errLen, "the SPI bus would not start: check the pins", e);
    g_lcd.busUp = true;

    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num       = c.cs;
    io.dc_gpio_num       = c.dc;
    io.spi_mode          = 0;
    io.pclk_hz           = static_cast<unsigned>(c.mhz) * 1000u * 1000u;
    io.trans_queue_depth = 2;
    io.on_color_trans_done = lcdDone;
    io.lcd_cmd_bits      = 8;
    io.lcd_param_bits    = 8;
    e = esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI3_HOST), &io, &g_lcd.io);
    if (e != ESP_OK) return lcdFail(err, errLen, "the panel would not take the SPI bus", e);

    esp_lcd_panel_dev_config_t pc = {};
    pc.reset_gpio_num = c.rst;
    pc.rgb_ele_order  = c.bgr ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB;
    // Little-endian colour data (RAMCTRL's ENDIAN bit, which the demo sets
    // too): the framebuffer's uint16_t pixels go out exactly as they sit in
    // memory, with no byte swap on the way.
    pc.data_endian    = LCD_RGB_DATA_ENDIAN_LITTLE;
    pc.bits_per_pixel = 16;
    e = esp_lcd_new_panel_st7789(g_lcd.io, &pc, &g_lcd.panel);
    if (e != ESP_OK) return lcdFail(err, errLen, "the ST7789 driver would not start", e);

    // Reset and init block for their datasheet delays (10 ms low, 10 ms
    // high, 100 ms after sleep-out). This is a plugin's start, never the loop.
    e = esp_lcd_panel_reset(g_lcd.panel);
    if (e == ESP_OK) e = esp_lcd_panel_init(g_lcd.panel);
    if (e != ESP_OK) return lcdFail(err, errLen, "the panel did not answer its reset", e);
    for (const LcdInit& in : kLcdInit) {
        e = esp_lcd_panel_io_tx_param(g_lcd.io, in.cmd, in.data, in.n);
        if (e != ESP_OK) return lcdFail(err, errLen, "the panel refused its settings", e);
    }

    // Rotation: the ST7789's usual four (MADCTL MV, MX, MY). Then the
    // mirror, for glass wired mirrored against the controller's memory, as
    // this module's is: Waveshare's demo mirrors X to draw portrait the
    // right way round. The mirror is along the glass's long side, which is
    // the controller's X until the axes are swapped and its Y after.
    bool swap = false, mx = false, my = false;
    switch (c.rotation) {
        case 90:  swap = true;  mx = true;  break;
        case 180: mx = true;    my = true;  break;
        case 270: swap = true;  my = true;  break;
        default:  break;                                           // 0
    }
    if (c.mirror) {
        if (swap) my = !my;
        else      mx = !mx;
    }
    esp_lcd_panel_invert_color(g_lcd.panel, c.invert);
    esp_lcd_panel_swap_xy(g_lcd.panel, swap);
    esp_lcd_panel_mirror(g_lcd.panel, mx, my);
    esp_lcd_panel_set_gap(g_lcd.panel, c.xoff, c.yoff);

    g_lcd.stage = static_cast<uint16_t*>(heap_caps_malloc(kBandPixels * 2u, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!g_lcd.stage) return lcdFail(err, errLen, "no DMA memory for the panel's band", ESP_ERR_NO_MEM);

    // The backlight, on LEDC at 5 kHz as the demo runs it. Only once the
    // panel is set up: the gate's pull-down holds it dark until then.
    if (c.bl >= 0) {
        ledc_timer_config_t t = {};
        t.speed_mode      = LEDC_LOW_SPEED_MODE;
        t.duty_resolution = LEDC_TIMER_13_BIT;
        t.timer_num       = LEDC_TIMER_0;
        t.freq_hz         = 5000;
        t.clk_cfg         = LEDC_AUTO_CLK;
        ledc_channel_config_t ch = {};
        ch.gpio_num   = c.bl;
        ch.speed_mode = LEDC_LOW_SPEED_MODE;
        ch.channel    = LEDC_CHANNEL_0;
        ch.timer_sel  = LEDC_TIMER_0;
        ch.duty       = 0;
        if (ledc_timer_config(&t) == ESP_OK && ledc_channel_config(&ch) == ESP_OK) g_lcd.blUp = true;
        else log("panel: the backlight on gpio %d would not start", c.bl);
    }
    esp_lcd_panel_disp_on_off(g_lcd.panel, true);
    g_lcd.cfg  = c;
    g_lcd.busy = false;
    lcdBlSet(c.backlight);
    log("panel: ST7789 %ux%u, rotation %u, %u MHz, on SPI3", static_cast<unsigned>(c.width),
        static_cast<unsigned>(c.height), static_cast<unsigned>(c.rotation), static_cast<unsigned>(c.mhz));
    return true;
}

bool lcdSame(const LcdCfg& c) {
    const LcdCfg& o = g_lcd.cfg;
    return g_lcd.panel && o.mosi == c.mosi && o.sclk == c.sclk && o.cs == c.cs && o.dc == c.dc &&
           o.rst == c.rst && o.bl == c.bl && o.width == c.width && o.height == c.height &&
           o.xoff == c.xoff && o.yoff == c.yoff && o.rotation == c.rotation &&
           o.invert == c.invert && o.bgr == c.bgr && o.mirror == c.mirror && o.mhz == c.mhz;
}

void lcdEnd() {
    if (g_lcd.blUp) {
        lcdBlSet(0);
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        g_lcd.blUp = false;
    }
    if (g_lcd.panel) {
        esp_lcd_panel_disp_on_off(g_lcd.panel, false);   // waits out a band in flight
        esp_lcd_panel_del(g_lcd.panel);
        g_lcd.panel = nullptr;
    }
    if (g_lcd.io) {
        esp_lcd_panel_io_del(g_lcd.io);
        g_lcd.io = nullptr;
    }
    if (g_lcd.busUp) {
        spi_bus_free(SPI3_HOST);
        g_lcd.busUp = false;
    }
    heap_caps_free(g_lcd.stage);
    g_lcd.stage = nullptr;
    g_lcd.busy  = false;
    g_lcd.cfg   = LcdCfg();
}

bool lcdReady() {
    return g_lcd.panel && g_lcd.stage && !g_lcd.busy;
}

uint32_t lcdBandPixels() {
    return kBandPixels;
}

bool lcdDraw(const uint16_t* fb, uint16_t stride, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!lcdReady() || !fb || !w || !h) return false;
    if (static_cast<uint32_t>(w) * h > kBandPixels) return false;
    uint16_t* out = g_lcd.stage;
    for (uint16_t r = 0; r < h; ++r) {
        memcpy(out, fb + static_cast<size_t>(y + r) * stride + x, static_cast<size_t>(w) * 2u);
        out += w;
    }
    g_lcd.busy = true;
    esp_err_t e = esp_lcd_panel_draw_bitmap(g_lcd.panel, x, y, x + w, y + h, g_lcd.stage);
    if (e != ESP_OK) {
        g_lcd.busy = false;
        return false;
    }
    return true;
}

void lcdBacklight(uint8_t pct) {
    lcdBlSet(pct);
}

void* psramAlloc(size_t n) {
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void psramFree(void* p) {
    heap_caps_free(p);
}
#endif  // BBS_HAS_LCD

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

#ifdef BBS_HAS_CAMERA
// ===========================================================================
// The camera (BBS_HAS_CAMERA). Pins and the sensor are the board profile's
// (board.h); nothing here knows which board it is on. Every function but
// pinOut and camDmaLargest is the camera worker's, never the loop's.
// ===========================================================================
namespace {

camera_fb_t* g_camFb = nullptr;
bool         g_camUp = false;
// A sensor with no JPEG encoder of its own (the GC0308 on the FNK0060 this
// was written for): frames come as RGB565 and jpegRaw encodes them. Learnt
// at the first bring-up and kept, so the JPEG attempt is paid once a boot.
bool         g_camRaw = false;
char         g_camName[12] = "";
// The largest frame the last sensor found gives, and its PID, for the
// sizes CONFIG offers and for camMeter.
uint16_t     g_camMaxW = 0, g_camMaxH = 0;
uint16_t     g_camPid  = 0;
// Raw frames are two bytes a pixel through the ESP32's I2S camera DMA, and
// cam_task copies each half buffer into the frame in PSRAM. At the JPEG
// path's 20 MHz XCLK it fell behind (cam_hal "EV-EOF-OVF", every frame lost
// on the bench, GC0308 at VGA), so a raw sensor runs slower.
constexpr int kCamRawXclk = 10000000;
// The frame sizes CONFIG camera offers, by the words it uses.
framesize_t camSize(const char* name) {
    struct Size { const char* word; framesize_t fs; };
    static const Size kSizes[] = {
        { "qvga", FRAMESIZE_QVGA }, { "vga", FRAMESIZE_VGA }, { "svga", FRAMESIZE_SVGA },
        { "xga", FRAMESIZE_XGA },   { "hd", FRAMESIZE_HD },   { "sxga", FRAMESIZE_SXGA },
        { "uxga", FRAMESIZE_UXGA }, { "qxga", FRAMESIZE_QXGA },
    };
    for (const Size& z : kSizes)
        if (name && !strcmp(name, z.word)) return z.fs;
    return FRAMESIZE_SVGA;
}

// The worker's task: one at a time, so one trampoline.
struct Worker { void (*fn)(void*); void* arg; };
Worker g_worker;

void workerMain(void*) {
    g_worker.fn(g_worker.arg);
    vTaskDelete(nullptr);
}

// camMemLog: internal RAM as the camera sees it, on the console, so a
// failed bring-up says which budget it met. The walks are the worker's.
void camMemLog(const char* when) {
    plat::log("camera: %s: internal free %u, largest %u, largest DMA %u", when,
              static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
              static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
              static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)));
}

}   // namespace

bool camOpen(const CamCfg& c, char* err, size_t errLen) {
    auto fail = [&](const char* why) {
        if (err && errLen) snprintf(err, errLen, "%s", why);
        return false;
    };
    if (err && errLen) err[0] = '\0';
    if (g_camUp) camClose();
    if (!heap_caps_get_total_size(MALLOC_CAP_SPIRAM)) return fail("no PSRAM on this board");
    // Measured here, on the worker, with its own stack already taken: the
    // driver's one 32 KB DMA buffer is its first large allocation and the
    // one that fails, and a failure costs a sensor probe and a bus set up
    // and torn down for nothing. The loop's check before the worker started
    // could not see the worker's stack come out of the same memory.
    if (camDmaLargest() < kCamDmaBlock || camInternalFree() < kCamInternal) {
        camMemLog("not enough memory to start");
        return fail("not enough memory for the camera");
    }

    camera_config_t cfg = {};
    cfg.pin_pwdn     = BBS_CAM_PWDN;
    cfg.pin_reset    = BBS_CAM_RESET;
    cfg.pin_xclk     = BBS_CAM_XCLK;
    cfg.pin_sccb_sda = BBS_CAM_SIOD;
    cfg.pin_sccb_scl = BBS_CAM_SIOC;
    cfg.pin_d7 = BBS_CAM_D7; cfg.pin_d6 = BBS_CAM_D6; cfg.pin_d5 = BBS_CAM_D5; cfg.pin_d4 = BBS_CAM_D4;
    cfg.pin_d3 = BBS_CAM_D3; cfg.pin_d2 = BBS_CAM_D2; cfg.pin_d1 = BBS_CAM_D1; cfg.pin_d0 = BBS_CAM_D0;
    cfg.pin_vsync    = BBS_CAM_VSYNC;
    cfg.pin_href     = BBS_CAM_HREF;
    cfg.pin_pclk     = BBS_CAM_PCLK;
    cfg.xclk_freq_hz = g_camRaw ? kCamRawXclk : 20000000;
    cfg.ledc_timer   = LEDC_TIMER_0;
    cfg.ledc_channel = LEDC_CHANNEL_0;
    // JPEG from the sensor when it can; RGB565 when it cannot, encoded on
    // this task by jpegRaw. A size the sensor cannot do is brought down to
    // its largest by the driver, and the frame says what it really is.
    cfg.pixel_format = g_camRaw ? PIXFORMAT_RGB565 : PIXFORMAT_JPEG;
    cfg.frame_size   = camSize(c.size);
    cfg.jpeg_quality = c.quality;
    cfg.fb_count     = 1;
    cfg.fb_location  = CAMERA_FB_IN_PSRAM;
    // A frame is only taken once the last was handed back, so the frame
    // after a flash has come on is one that began after it (camGrab's caller
    // throws one away first).
    cfg.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
    cfg.sccb_i2c_port = -1;

    esp_err_t e = esp_camera_init(&cfg);
    if (e == ESP_ERR_NOT_SUPPORTED && !g_camRaw) {
        // Either nothing answered on the bus, or a sensor answered that
        // cannot give JPEG ("JPEG format is not supported on this sensor"):
        // the same code for both, so ask again for RGB565, which every
        // sensor the driver knows gives. Only the second is then a camera.
        if (esp_camera_sensor_get()) esp_camera_deinit();
        cfg.pixel_format = PIXFORMAT_RGB565;
        cfg.xclk_freq_hz = kCamRawXclk;
        e = esp_camera_init(&cfg);
        if (e == ESP_OK) {
            g_camRaw = true;
            plat::log("camera: the sensor gives no JPEG: raw frames, encoded on the worker");
        }
    }
    if (e != ESP_OK) {
        plat::log("camera: init failed: %s (0x%x)", esp_err_to_name(e), static_cast<unsigned>(e));
        // esp_camera_init tears down what it built on every failing path
        // but one (cam_init, which frees its own), so as a rule this finds
        // nothing left. A sensor still registered is a partial start, and
        // everything it holds goes back: the DMA block, cam_task, the SCCB
        // bus and XCLK's LEDC channel.
        if (esp_camera_sensor_get()) esp_camera_deinit();
        camMemLog("after the failed start");
        // No sensor on the bus is ESP_ERR_NOT_SUPPORTED in 2.1.7 (camera_
        // probe: "Detected camera not supported"), not NOT_DETECTED. A DMA
        // buffer the heap could not give is ESP_FAIL (cam_dma_config),
        // which the largest DMA block left tells apart from the rest.
        if (e == ESP_ERR_CAMERA_NOT_DETECTED || e == ESP_ERR_NOT_SUPPORTED || e == ESP_ERR_NOT_FOUND)
            return fail("no camera found: check the ribbon");
        if (e == ESP_ERR_NO_MEM || camDmaLargest() < kCamDmaBlock)
            return fail("not enough memory for the camera");
        return fail("the camera would not start");
    }
    g_camUp = true;
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        camera_sensor_info_t* info = esp_camera_sensor_get_info(&s->id);
        static bool said = false;
        if (!said) {
            plat::log("camera: sensor %s (PID 0x%04x), %s", info ? info->name : "unknown",
                      static_cast<unsigned>(s->id.PID), g_camRaw ? "RGB565" : "JPEG");
            said = true;
        }
        snprintf(g_camName, sizeof(g_camName), "%s", info ? info->name : "unknown");
        g_camPid = s->id.PID;
        if (info && info->max_size < FRAMESIZE_INVALID) {
            g_camMaxW = resolution[info->max_size].width;
            g_camMaxH = resolution[info->max_size].height;
        }
        // Each driver fills in what its sensor has; one that left a setting
        // out is skipped rather than called through a null.
        if (s->set_vflip)          s->set_vflip(s, c.flip ? 1 : 0);
        if (s->set_hmirror)        s->set_hmirror(s, c.mirror ? 1 : 0);
        if (s->set_brightness)     s->set_brightness(s, c.bright);
        if (s->set_contrast)       s->set_contrast(s, c.contrast);
        if (s->set_saturation)     s->set_saturation(s, c.saturation);
        if (s->set_ae_level)       s->set_ae_level(s, c.exposure);
        // Every automatic control on (1.1.1). The AWB gain was switched
        // off whenever White was auto (c.wb ? 1 : 0), which on an OV2640
        // leaves the white balance measured and never applied: the green
        // cast. It is on whatever the mode; the mode picks the gains.
        if (s->set_exposure_ctrl)  s->set_exposure_ctrl(s, 1);
        if (s->set_aec2)           s->set_aec2(s, 1);
        if (s->set_gain_ctrl)      s->set_gain_ctrl(s, 1);
        if (s->set_whitebal)       s->set_whitebal(s, 1);
        if (s->set_awb_gain)       s->set_awb_gain(s, 1);
        if (s->set_wb_mode)        s->set_wb_mode(s, c.wb);
        // The sensor's own corrections, where its driver has them (the
        // OV2640's DSP; a no-op on the GC0308): lens shading, the raw
        // gamma, black and white pixel correction, downsize cropping.
        if (s->set_lenc)           s->set_lenc(s, 1);
        if (s->set_raw_gma)        s->set_raw_gma(s, 1);
        if (s->set_bpc)            s->set_bpc(s, 1);
        if (s->set_wpc)            s->set_wpc(s, 1);
        if (s->set_dcw)            s->set_dcw(s, 1);
        if (s->set_special_effect) s->set_special_effect(s, c.effect);
        // The plugin's own writes for this sensor, after the driver's, so
        // they win where both set a register (camera_pic.h, the GC0308).
        if (c.nRegs && c.regsPid == s->id.PID && s->set_reg) {
            int bad = 0;
            for (uint8_t i = 0; i < c.nRegs && i < 8; ++i)
                bad += s->set_reg(s, c.regs[i][0], 0xFF, c.regs[i][1]) < 0;
            if (bad) plat::log("camera: %d of %u picture registers not written", bad,
                               static_cast<unsigned>(c.nRegs));
        }
    }
    return true;
}

bool camGrab(const uint8_t*& buf, size_t& len, uint16_t& w, uint16_t& h) {
    if (!g_camUp) return false;
    if (g_camFb) camRelease();
    g_camFb = esp_camera_fb_get();
    if (!g_camFb) return false;
    buf = g_camFb->buf;
    len = g_camFb->len;
    w   = static_cast<uint16_t>(g_camFb->width);
    h   = static_cast<uint16_t>(g_camFb->height);
    return true;
}

void camRelease() {
    if (g_camFb) esp_camera_fb_return(g_camFb);
    g_camFb = nullptr;
}

void camClose() {
    camRelease();
    if (g_camUp) esp_camera_deinit();
    g_camUp = false;
}

uint32_t camDmaLargest() {
    return static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
}

uint32_t camInternalFree() {
    return static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

bool camRaw() { return g_camRaw; }

const char* camSensor() { return g_camName; }

bool camMaxSize(uint16_t& w, uint16_t& h) {
    w = g_camMaxW;
    h = g_camMaxH;
    return w && h;
}

// camMeter: the GC0308's frame average and AEC target, page 0 (GC0308
// DataSheet: P0:0xD4 Y_average, RO; P0:0xD3 AEC_target_Y; the page register
// is 0xFE, set again so a read never lands on another page). The OV2640's
// exposure and gain from its sensor bank (the driver's get_reg takes the
// bank in bit 8): AEC[15:10] in 0x45, AEC[9:2] in 0x10, AEC[1:0] in 0x04,
// GAIN in 0x00 (OV2640 datasheet, register tables, bank 1).
CamMeter camMeter(uint16_t& a, uint16_t& b) {
    sensor_t* s = g_camUp ? esp_camera_sensor_get() : nullptr;
    if (!s || !s->get_reg) return CAM_METER_NONE;
    if (g_camPid == GC0308_PID && s->set_reg) {
        if (s->set_reg(s, 0xFE, 0xFF, 0x00) < 0) return CAM_METER_NONE;
        const int y = s->get_reg(s, 0xD4, 0xFF), t = s->get_reg(s, 0xD3, 0xFF);
        if (y < 0 || t < 0) return CAM_METER_NONE;
        a = static_cast<uint16_t>(y);
        b = static_cast<uint16_t>(t);
        return CAM_METER_LUMA;
    }
    if (g_camPid == OV2640_PID) {
        const int hi = s->get_reg(s, 0x145, 0x3F), mid = s->get_reg(s, 0x110, 0xFF);
        const int lo = s->get_reg(s, 0x104, 0x03), gain = s->get_reg(s, 0x100, 0xFF);
        if (hi < 0 || mid < 0 || lo < 0 || gain < 0) return CAM_METER_NONE;
        a = static_cast<uint16_t>(hi << 10 | mid << 2 | lo);
        b = static_cast<uint16_t>(gain);
        return CAM_METER_EXPOSURE;
    }
    return CAM_METER_NONE;
}

bool camQuality(int quality) {
    sensor_t* s = g_camUp ? esp_camera_sensor_get() : nullptr;
    return s && s->set_quality && s->set_quality(s, quality) == 0;
}

void* camAlloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(n, MALLOC_CAP_8BIT);
}

void camFree(void* p) {
    heap_caps_free(p);
}

// taskStart: the worker, on the BBS task's core and three below its
// priority, so whenever the loop has anything to do it runs and the worker
// waits; the stack is internal, since the worker writes the card. One at a
// time: the camera plugin's job is the lock.
bool taskStart(void (*fn)(void*), void* arg, uint32_t stackBytes, const char* name) {
    g_worker.fn  = fn;
    g_worker.arg = arg;
    BaseType_t ok = xTaskCreatePinnedToCore(workerMain, name, stackBytes, nullptr,
                                            BBS_TASK_PRIO > 3 ? BBS_TASK_PRIO - 3 : 1, nullptr,
                                            BBS_TASK_CORE);
    return ok == pdPASS;
}

void taskSleep(uint32_t ms) {
    vTaskDelay(ms ? pdMS_TO_TICKS(ms) : 1);
}

uint32_t taskStackFree() {
    return static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr)) * sizeof(StackType_t);
}

bool sdSpace(uint64_t& total, uint64_t& freeBytes) {
    total = freeBytes = 0;
    if (!g_mount) return false;
    return esp_vfs_fat_info(BBS_SD_MOUNT, &total, &freeBytes) == ESP_OK;
}

bool sdList(const char* rel, SdListFn fn, void* ctx) {
    if (!g_mount || !g_card || !fn) return false;
    char path[160];
    snprintf(path, sizeof(path), "%u:/%s", static_cast<unsigned>(ff_diskio_get_pdrv_card(g_card)), rel ? rel : "");
    FF_DIR d;
    if (f_opendir(&d, path) != FR_OK) return false;
    FILINFO fi;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        if (fi.fname[0] == '.') continue;
        if (!fn(ctx, fi.fname, (fi.fattrib & AM_DIR) != 0, static_cast<uint32_t>(fi.fsize))) break;
    }
    f_closedir(&d);
    return true;
}

// pinOut: the flash pin. Two register writes, safe from the loop.
void pinOut(int pin, bool high) {
    if (pin < 0 || !GPIO_IS_VALID_OUTPUT_GPIO(pin)) return;
    gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(pin), high ? 1 : 0);
}

// ---------------------------------------------------------------------------
// jpegMark: decode with the ROM's TJpgDec a block at a time into a strip of
// whole rows (one MCU tall: 8 or 16 rows), let the caller draw on the strip,
// and feed its rows to the camera driver's jpge encoder, which hands its
// output on as it is made. Nothing the size of the picture is ever held:
// at UXGA the strip is 1600 x 8 x 3 bytes, in PSRAM. Every strip yields,
// so the idle task on this core runs and the watchdog is fed.
// ---------------------------------------------------------------------------
namespace {

class MarkStream : public jpge::output_stream {
public:
    MarkStream(MarkOutFn out, void* ctx) : out_(out), ctx_(ctx) {}
    bool put_buf(const void* p, int len) override {
        if (!ok_) return false;
        ok_ = out_(ctx_, static_cast<const uint8_t*>(p), static_cast<size_t>(len));
        size_ += static_cast<uint>(len);
        return ok_;
    }
    uint get_size() const override { return size_; }
    bool ok() const { return ok_; }
private:
    MarkOutFn out_;
    void*     ctx_;
    uint      size_ = 0;
    bool      ok_   = true;
};

struct MarkJob {
    const uint8_t*       src;
    size_t               len, pos;
    uint8_t*             strip;
    uint16_t             w, h, stripH, stripY;
    jpge::jpeg_encoder*  enc;
    MarkRowsFn           draw;
    void*                dctx;
    bool                 ok;
};

UINT markIn(JDEC* jd, BYTE* buf, UINT n) {
    MarkJob* j = static_cast<MarkJob*>(jd->device);
    size_t left = j->len - j->pos;
    if (n > left) n = static_cast<UINT>(left);
    if (buf) memcpy(buf, j->src + j->pos, n);
    j->pos += n;
    return n;
}

UINT markOut(JDEC* jd, void* bitmap, JRECT* r) {
    MarkJob* j = static_cast<MarkJob*>(jd->device);
    const uint8_t* px = static_cast<const uint8_t*>(bitmap);
    const size_t bw = static_cast<size_t>(r->right - r->left + 1) * 3u;
    for (uint16_t y = r->top; y <= r->bottom; ++y) {
        uint8_t* dst = j->strip + (static_cast<size_t>(y - j->stripY) * j->w + r->left) * 3u;
        memcpy(dst, px, bw);
        px += bw;
    }
    if (r->right + 1u < j->w) return 1;                  // the strip is not whole yet
    const uint16_t rows = static_cast<uint16_t>(r->bottom - j->stripY + 1);
    if (j->draw) j->draw(j->dctx, j->strip, j->w, j->stripY, rows);
    for (uint16_t i = 0; i < rows; ++i) {
        if (!j->enc->process_scanline(j->strip + static_cast<size_t>(i) * j->w * 3u)) { j->ok = false; return 0; }
    }
    j->stripY = static_cast<uint16_t>(j->stripY + rows);
    vTaskDelay(1);                                       // the loop and the idle task first
    return 1;
}

}   // namespace

bool jpegMark(const uint8_t* jpg, size_t len, uint8_t quality, MarkRowsFn draw, void* dctx,
              MarkOutFn out, void* octx, uint16_t& width, uint16_t& height) {
    constexpr size_t kPool = 3100;                       // TJpgDec's work area (its own figure)
    void* pool = camAlloc(kPool);
    JDEC* jd   = static_cast<JDEC*>(camAlloc(sizeof(JDEC)));
    MarkJob job = {};
    job.src = jpg; job.len = len; job.ok = true; job.draw = draw; job.dctx = dctx;
    bool done = false;
    uint8_t* strip = nullptr;
    void* encMem = nullptr;
    jpge::jpeg_encoder* enc = nullptr;
    MarkStream stream(out, octx);
    if (pool && jd && jd_prepare(jd, markIn, pool, kPool, &job) == JDR_OK) {
        job.w = static_cast<uint16_t>(jd->width);
        job.h = static_cast<uint16_t>(jd->height);
        job.stripH = static_cast<uint16_t>(jd->msy * 8);
        width = job.w;
        height = job.h;
        strip  = static_cast<uint8_t*>(camAlloc(static_cast<size_t>(job.w) * job.stripH * 3u));
        encMem = camAlloc(sizeof(jpge::jpeg_encoder));
        if (strip && encMem) {
            enc = new (encMem) jpge::jpeg_encoder();
            jpge::params p;
            p.m_quality     = quality < 1 ? 1 : quality > 100 ? 100 : quality;
            p.m_subsampling = jpge::H2V1;                // the sensor's own 4:2:2
            job.enc = enc;
            job.strip = strip;
            if (enc->init(&stream, job.w, job.h, 3, p)) {
                JRESULT r = jd_decomp(jd, markOut, 0);
                done = r == JDR_OK && job.ok && job.stripY == job.h && enc->process_scanline(nullptr) &&
                       stream.ok();
            }
            enc->deinit();
            enc->~jpeg_encoder();
        }
    }
    camFree(encMem);
    camFree(strip);
    camFree(jd);
    camFree(pool);
    return done;
}

// ---------------------------------------------------------------------------
// jpegHist: TJpgDec's own descaling at 1/8 (the ROM's JD_USE_SCALE is 1):
// every 8x8 block comes out as one pixel, its DC term, so no inverse DCT is
// done and a UXGA picture is 200 x 150 pixels of histogram. The entropy
// decoding is still the whole file's.
// ---------------------------------------------------------------------------
namespace {
struct HistJob {
    const uint8_t* src;
    size_t         len, pos;
    HistPixFn      fn;
    void*          ctx;
    uint32_t       blocks;
};

UINT histIn(JDEC* jd, BYTE* buf, UINT n) {
    HistJob* j = static_cast<HistJob*>(jd->device);
    size_t left = j->len - j->pos;
    if (n > left) n = static_cast<UINT>(left);
    if (buf) memcpy(buf, j->src + j->pos, n);
    j->pos += n;
    return n;
}

UINT histOut(JDEC* jd, void* bitmap, JRECT* r) {
    HistJob* j = static_cast<HistJob*>(jd->device);
    const size_t px = static_cast<size_t>(r->right - r->left + 1) * (r->bottom - r->top + 1);
    j->fn(j->ctx, static_cast<const uint8_t*>(bitmap), px);
    if ((++j->blocks & 63) == 0) vTaskDelay(1);         // the loop and the idle task first
    return 1;
}
}   // namespace

bool jpegHist(const uint8_t* jpg, size_t len, HistPixFn fn, void* ctx) {
    if (!jpg || !len || !fn) return false;
    constexpr size_t kPool = 3100;                       // TJpgDec's work area (its own figure)
    void* pool = camAlloc(kPool);
    JDEC* jd   = static_cast<JDEC*>(camAlloc(sizeof(JDEC)));
    HistJob job{ jpg, len, 0, fn, ctx, 0 };
    bool ok = pool && jd && jd_prepare(jd, histIn, pool, kPool, &job) == JDR_OK &&
              jd_decomp(jd, histOut, 3) == JDR_OK;
    camFree(jd);
    camFree(pool);
    return ok;
}

// ---------------------------------------------------------------------------
// jpegRaw: a sensor with no JPEG encoder gives RGB565, two bytes a pixel,
// high byte first (the driver's own order: conversions/to_jpg.cpp,
// rgb565_big_endian). A strip of 16 rows at a time goes to RGB888, gets the
// caller's drawing, and is fed to the same jpge encoder jpegMark uses. The
// frame itself is read, never written. Every strip yields, as jpegMark's do.
// ---------------------------------------------------------------------------
bool jpegRaw(const uint8_t* rgb565, uint16_t w, uint16_t h, uint8_t quality, MarkRowsFn draw, void* dctx,
             MarkOutFn out, void* octx) {
    if (!rgb565 || !w || !h) return false;
    constexpr uint16_t kStripH = 16;
    uint8_t* strip = static_cast<uint8_t*>(camAlloc(static_cast<size_t>(w) * kStripH * 3u));
    void* encMem   = camAlloc(sizeof(jpge::jpeg_encoder));
    bool done = false;
    MarkStream stream(out, octx);
    if (strip && encMem) {
        jpge::jpeg_encoder* enc = new (encMem) jpge::jpeg_encoder();
        jpge::params p;
        p.m_quality     = quality < 1 ? 1 : quality > 100 ? 100 : quality;
        p.m_subsampling = jpge::H2V1;
        bool ok = enc->init(&stream, w, h, 3, p);
        for (uint16_t y0 = 0; ok && y0 < h; y0 = static_cast<uint16_t>(y0 + kStripH)) {
            const uint16_t rows = static_cast<uint16_t>(h - y0 < kStripH ? h - y0 : kStripH);
            const uint8_t* src = rgb565 + static_cast<size_t>(y0) * w * 2u;
            uint8_t* dst = strip;
            for (size_t i = 0, n = static_cast<size_t>(rows) * w; i < n; ++i, src += 2) {
                const uint8_t hi = src[0], lo = src[1];
                *dst++ = static_cast<uint8_t>(hi & 0xF8);
                *dst++ = static_cast<uint8_t>(((hi & 0x07) << 5) | ((lo & 0xE0) >> 3));
                *dst++ = static_cast<uint8_t>((lo & 0x1F) << 3);
            }
            if (draw) draw(dctx, strip, w, y0, rows);
            for (uint16_t i = 0; ok && i < rows; ++i)
                ok = enc->process_scanline(strip + static_cast<size_t>(i) * w * 3u);
            vTaskDelay(1);                               // the loop and the idle task first
        }
        done = ok && enc->process_scanline(nullptr) && stream.ok();
        enc->deinit();
        enc->~jpeg_encoder();
    }
    camFree(encMem);
    camFree(strip);
    return done;
}
#endif  // BBS_HAS_CAMERA

} // namespace plat

#ifdef BBS_PINS_HOLD_LOW
// ---------------------------------------------------------------------------
// Pins the board needs held low from the start (board.h): the ESP32-CAM's
// flash LED on GPIO 4, whose transistor lights on a floating pin. A
// constructor, so it runs in the start-up code before app_main and before
// anything that could touch the card or the camera. The GPIO calls it makes
// need no task and no heap.
// ---------------------------------------------------------------------------
namespace {
__attribute__((constructor)) void holdLowAtBoot() {
    static const int kPins[] = { BBS_PINS_HOLD_LOW };
    for (int pin : kPins) {
        const gpio_num_t g = static_cast<gpio_num_t>(pin);
        gpio_set_pull_mode(g, GPIO_FLOATING);
        gpio_set_level(g, 0);
        gpio_set_direction(g, GPIO_MODE_OUTPUT);
    }
}
}   // namespace
#endif
