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
#if defined(BBS_HAS_SSH) && BBS_HAS_SSH
#include "esp_vfs_eventfd.h"       // the SSH links' wake descriptors (1.1.2)
// read and write on those descriptors (wakePost, wakeTake). The Waveshare's
// build got <unistd.h> only by way of the USB-Serial-JTAG console's headers
// below, so an SSH board with its console on a UART (the Makerfabs) failed
// to compile without it.
#include <unistd.h>
#endif
#if SOC_USB_SERIAL_JTAG_SUPPORTED && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"      // the console on the S3's own USB
#include "driver/usb_serial_jtag_vfs.h"
#define BBS_CONSOLE_USJ 1
#endif
#ifdef BBS_SD_SDMMC1
#include "driver/sdmmc_host.h"           // a card slot wired for SDMMC (board.h)
#endif
#include "ff.h"                          // sdList: FatFs's directory entries, sizes and all
#include "diskio_sdmmc.h"                // the card's drive number
#include "freertos/semphr.h"             // the runner's lock and its wake (1.1.2)
#if CONFIG_IDF_TARGET_ESP32
#include "esp_rom_sys.h"                 // resetReason: the RTC's own reset cause
#endif
#ifdef BBS_HAS_CAMERA
#include "esp_camera.h"                 // Espressif's camera driver (Apache-2.0)
#include "esp_log.h"                          // camOpen quiets the gpio driver's tag (1.1.2)
#include "jpge.h"                        // its JPEG encoder, for the watermark
#if CONFIG_IDF_TARGET_ESP32
#include "esp32/rom/tjpgd.h"             // the ROM's JPEG decoder: no flash
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/tjpgd.h"
#endif
#include <new>
#endif
#if defined(BBS_HAS_LCD) && !defined(BBS_LCD_RGB)
#include "esp_lcd_panel_io.h"            // the panel: esp_lcd over SPI or i80
#include "esp_lcd_io_spi.h"
#ifdef BBS_LCD_I80
#include "esp_lcd_io_i80.h"              // a parallel panel (board.h, BBS_LCD_I80)
#endif
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_commands.h"
#include "driver/ledc.h"                 // its backlight, dimmed by PWM
#endif
#if defined(BBS_HAS_TOUCH) && !defined(BBS_TOUCH_POLL)
#include "driver/i2c.h"                  // the touch controller, once at start: the legacy
                                         // driver, which the camera's SCCB (esp32-camera 2.1.7
                                         // on IDF 5.3) uses too; the two drivers cannot mix
#endif
#ifdef BBS_HAS_CHIP_TEMP
#include "driver/temperature_sensor.h"   // the chip's own sensor, for the panel
#endif
#ifdef BBS_HAS_ETH
#include "esp_eth.h"                     // the W5500 (1.1.2): IDF 5.3.1's own driver
#include "esp_eth_mac_spi.h"
#include "esp_mac.h"                     // its address, from the chip's efuse
#include "esp_event.h"
#include "esp_log.h"
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
// measure: a partition's or the card's size and use, read now.
//
// esp_littlefs_info calls lfs_fs_size, which walks every block of every file
// (esp_littlefs.c:313 in the 1.22.3 component): about 85 ms a partition, and
// it holds that partition's lock throughout. On the loop that froze every
// caller (SYS asked for two, 170 ms; DASH once a minute; every plugin write
// once a minute). Since 1.1.2 only the runner asks, and core/space.h keeps
// the figures for every screen and for the plugins' reserve guard.
// ---------------------------------------------------------------------------
bool measure(Part p, uint64_t& total, uint64_t& used) {
    total = used = 0;
    if (p == PART_CARD) {
        uint64_t freeB = 0;
        if (!sdSpace(total, freeB)) return false;
        used = total > freeB ? total - freeB : 0;
        return true;
    }
    const char* label = p == PART_SCREENS ? BBS_FS_LABEL : p == PART_USER ? BBS_USER_LABEL : BBS_LOGS_LABEL;
    size_t t = 0, u = 0;
    if (esp_littlefs_info(label, &t, &u) != ESP_OK) return false;
    total = t;
    used  = u;
    return true;
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

// markLoop / onLoop: see platform.h (1.1.2).
static TaskHandle_t g_loopTask = nullptr;
void markLoop() { g_loopTask = xTaskGetCurrentTaskHandle(); }
bool onLoop()   { return g_loopTask && xTaskGetCurrentTaskHandle() == g_loopTask; }

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

// ---------------------------------------------------------------------------
// The background runner's task (1.1.2, core/runner). On the BBS task's core
// and three below its priority, so whenever the loop has anything to do it
// runs and the runner waits; the stack is the heap's, internal, since jobs
// write the card. The function goes in as the task's parameter: the camera
// worker's trampoline kept it in one global the new task read later, so a
// second start before the first task ran would have run the second job twice.
//
// The lock is a critical section, not a mutex: it is held for a handful of
// instructions around the queue, both tasks are on core 1, and it costs 8
// bytes of static DRAM where a static mutex costs about 80. The wake is a
// task notification on the runner, whose handle is set here as it starts;
// runner.cpp only wakes a task it knows to be alive (under the same lock).
// ---------------------------------------------------------------------------
namespace {
portMUX_TYPE g_runMux  = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t g_runTask = nullptr;

void runTramp(void* p) {
    reinterpret_cast<void (*)()>(p)();
    vTaskDelete(nullptr);
}
}   // namespace

bool taskStart(void (*fn)(), uint32_t stackBytes, const char* name) {
    TaskHandle_t h = nullptr;
    BaseType_t ok = xTaskCreatePinnedToCore(runTramp, name, stackBytes, reinterpret_cast<void*>(fn),
                                            BBS_TASK_PRIO > 3 ? BBS_TASK_PRIO - 3 : 1, &h,
                                            BBS_TASK_CORE);
    if (ok != pdPASS) return false;
    g_runTask = h;
    return true;
}

void taskSleep(uint32_t ms) {
    vTaskDelay(ms ? pdMS_TO_TICKS(ms) : 1);
}

uint32_t taskStackFree() {
    return static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr)) * sizeof(StackType_t);
}

void runLock()   { portENTER_CRITICAL(&g_runMux); }
void runUnlock() { portEXIT_CRITICAL(&g_runMux); }

bool runWait(uint32_t ms) {
    return ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms)) != 0;
}

void runWake() {
    if (g_runTask) xTaskNotifyGive(g_runTask);
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
#ifdef BBS_HAS_ETH
    // The wired port in PSRAM's place (1.1.2): the directory cuts this badge
    // at 40 characters, and "ESP32-S3 · 8 MB · Ethernet · ETH 1.0.0", with
    // the board's version that announce adds, is 38. Ethernet is what sets
    // the board apart; its PSRAM is on the HARDWARE screen.
    snprintf(out, n, "%s%s%sEthernet", model, size, kDot);
#else
    const bool psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
    snprintf(out, n, "%s%s%s%s", model, size, psram ? kDot : "", psram ? "PSRAM" : "");
#endif
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
#ifdef BBS_SPI_SHARED
// ---------------------------------------------------------------------------
// The panel and the card on one SPI bus (board.h, BBS_SPI_SHARED: the
// Waveshare ESP32-S3-Touch-LCD-2, whose panel and TF slot share MOSI and
// SCLK). One host, SPI2 (the card's default), raised by whichever of the two
// starts first with the card's MISO and a transfer size a panel band fits,
// and freed when the last lets it go.
//
// The SPI driver already takes turns between the two devices, but a card
// command holds the bus from start to end, and a write's busy wait inside
// one can run to hundreds of milliseconds. The panel's bands are sent from
// the BBS loop, which must never wait for that (Rule no. 1). Its bring-up
// and teardown (lcdBegin, lcdEnd: a plugin start, a CONFIG save) may wait
// out one card command; they are allowed to block anyway. So every card
// command goes through sharedCmd, which holds g_sharedMux for the command,
// and lcdDraw only TRIES the mutex: when a card command is running on
// another task, the band is skipped and the panel sends it on a later tick.
// The panel holds the mutex only while its band is being queued (the three
// address commands, microseconds); the band's DMA then runs on its own, and
// a card command that follows waits for it on the runner, about 2 ms at
// 40 MHz. The loop's own card reads (a screen played from the card) take the
// mutex as any card command does; they cannot meet the panel's, which is on
// the same task.
// ---------------------------------------------------------------------------
namespace {
constexpr spi_host_device_t kSharedHost  = SDSPI_DEFAULT_HOST;       // SPI2
constexpr int               kSharedBytes = 320 * 16 * 2;              // a panel band (kBandPixels)
enum : uint8_t { SHARED_CARD = 1, SHARED_PANEL = 2 };
uint8_t           g_sharedUsers = 0;
int8_t            g_sharedMosi = -1, g_sharedMiso = -1, g_sharedSclk = -1;
SemaphoreHandle_t g_sharedMux = nullptr;
StaticSemaphore_t g_sharedMuxBuf;

// sharedUp: `who` on the bus. The bus is raised if nobody holds it, with
// miso (or the board's card MISO, for the panel, which reads nothing), and
// otherwise must be on the same MOSI and clock, since both ends are wired to
// one pair. why says what was wrong.
bool sharedUp(uint8_t who, int mosi, int miso, int sclk, const char*& why) {
    if (!g_sharedMux) g_sharedMux = xSemaphoreCreateMutexStatic(&g_sharedMuxBuf);
    if (g_sharedUsers) {
        if (mosi != g_sharedMosi || sclk != g_sharedSclk || (miso >= 0 && miso != g_sharedMiso)) {
            why = "the panel and the card share one SPI bus: give both the same MOSI, clock and MISO pins";
            return false;
        }
        g_sharedUsers = static_cast<uint8_t>(g_sharedUsers | who);
        return true;
    }
    spi_bus_config_t bus = {};
    bus.mosi_io_num     = mosi;
    bus.miso_io_num     = miso >= 0 ? miso : BBS_SD_MISO;
    bus.sclk_io_num     = sclk;
    bus.quadwp_io_num   = -1;
    bus.quadhd_io_num   = -1;
    bus.max_transfer_sz = kSharedBytes;
    if (spi_bus_initialize(kSharedHost, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        why = "the SPI bus would not start: check the pin numbers";
        return false;
    }
    g_sharedMosi  = static_cast<int8_t>(mosi);
    g_sharedMiso  = static_cast<int8_t>(bus.miso_io_num);
    g_sharedSclk  = static_cast<int8_t>(sclk);
    g_sharedUsers = who;
    return true;
}

// cardQuiet: the slot's chip select held high while the panel talks on the
// bus and the card is not mounted (none at boot, or SD UNMOUNT), so a card
// already in SPI mode ignores the panel's bytes. A card never yet put into
// SPI mode listens on its CMD line (MOSI) whatever CS says; the next
// mount's reset (CMD0 with CS low) brings it round from whatever it made of
// them. The card driver takes the pin over again at the next mount.
void cardQuiet() {
    if (BBS_SD_CS < 0) return;
    gpio_set_level(static_cast<gpio_num_t>(BBS_SD_CS), 1);
    gpio_set_direction(static_cast<gpio_num_t>(BBS_SD_CS), GPIO_MODE_OUTPUT);
}

void panelQuiet();                                     // below

// sharedDown: `who` off the bus, and the bus freed when nobody is left.
void sharedDown(uint8_t who) {
    if (!(g_sharedUsers & who)) return;
    g_sharedUsers = static_cast<uint8_t>(g_sharedUsers & ~who);
    if (!g_sharedUsers) spi_bus_free(kSharedHost);
    else if (who == SHARED_CARD) cardQuiet();          // the panel goes on without it
    else panelQuiet();                                 // the card goes on without the panel
}

// panelQuiet: the panel's chip select held high while the card talks on the
// bus before the panel has started (WS2 1.0.1). The card mounts first at
// boot (sd is PF_EARLY), and the panel's CS is a strapping pin pulled low at
// reset, so the controller took the card's traffic for its own; its SDA is
// bidirectional, and a read command it made of those bytes drove the MOSI
// line against us. On the bench the boot mount failed with
// ESP_ERR_INVALID_CRC every time and SD MOUNT with the panel up succeeded.
// esp_lcd takes the pin over when the panel starts.
void panelQuiet() {
    if (BBS_LCD_CS < 0 || (g_sharedUsers & SHARED_PANEL)) return;
    gpio_set_level(static_cast<gpio_num_t>(BBS_LCD_CS), 1);
    gpio_set_direction(static_cast<gpio_num_t>(BBS_LCD_CS), GPIO_MODE_OUTPUT);
}

// sharedCmd: the SPI card host's own command, under the mutex (see above).
esp_err_t sharedCmd(int slot, sdmmc_command_t* cmd) {
    xSemaphoreTake(g_sharedMux, portMAX_DELAY);
    const esp_err_t e = sdspi_host_do_transaction(slot, cmd);
    xSemaphoreGive(g_sharedMux);
    return e;
}
}   // namespace
#endif  // BBS_SPI_SHARED

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
#ifdef BBS_SD_CS_EXPANDER
    // The card's chip select is the board's expander pin, held low from the
    // expander's first write: the driver is given no CS (board.h).
    if (!boardExpander()) return fail("the board's I2C expander did not answer");
#endif
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
#elif defined(BBS_SPI_SHARED)
    // The panel's bus (above): raised here if the panel has not raised it,
    // and every card command through sharedCmd.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
#pragma GCC diagnostic pop
    host.max_freq_khz   = pins.speedKHz ? pins.speedKHz : 20000;
    host.do_transaction = sharedCmd;
    {
        const char* why = nullptr;
        if (!sharedUp(SHARED_CARD, pins.mosi, pins.miso, pins.clk, why)) return fail(why);
        panelQuiet();                                  // before the panel is up (boot)
    }
    dev.gpio_cs = static_cast<gpio_num_t>(pins.cs);
    dev.host_id = kSharedHost;
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
#ifdef BBS_SPI_SHARED
        sharedDown(SHARED_CARD);
#endif
        if (g_busUp) { spi_bus_free(SDSPI_DEFAULT_HOST); g_busUp = false; }
        // Three different evenings, so three different messages. The error
        // code goes in the log as well: the first cut mapped everything that
        // was not a timeout or a bad filesystem onto one catch-all string,
        // and when a real card hit that branch the message named three things
        // to check and none of them was the problem.
        plat::log("sd: mount failed at %u kHz: %s (0x%x)",
                  static_cast<unsigned>(host.max_freq_khz), esp_err_to_name(e),
                  static_cast<unsigned>(e));
#if defined(BBS_SD_SDMMC1) || defined(BBS_SD_CS_EXPANDER)
        // No CS pin a sysop could have got wrong: the slot's own wiring.
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
        // ESP_ERR_NO_MEM is also what esp_vfs_register says for a full VFS
        // table (vfs.c, s_vfs_count >= VFS_MAX_COUNT), which is how the
        // Makerfabs lost its card with 1.7 MB free (1.2.1: both are named).
        // 36 columns: SD shows it indented two, inside a C64's 39.
        if (e == ESP_ERR_NO_MEM)
            return fail("no memory or VFS table full: see MEM");
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
#ifdef BBS_SPI_SHARED
    sharedDown(SHARED_CARD);                       // and the panel is not on it
#endif
    if (g_busUp) { spi_bus_free(SDSPI_DEFAULT_HOST); g_busUp = false; }
    g_card  = nullptr;
    g_mount = false;
    g_speed = 0;
    plat::log("sd: unmounted");
}

// sdInfoStale: called by mount and unmount. Nothing is kept here since
// 1.1.2 (sdInfo reads registers only), so there is nothing to forget.
static void sdInfoStale() {}

// sdInfo: registers only, never the FAT (1.1.2). The size is the card's own
// (its CSD, read at mount), which is what the size printed on it is rounded
// from; free space is a measurement, the runner's (measure(PART_CARD)), and
// core/space.h keeps it. Asking esp_vfs_fat_info here, as this did, put a
// possible whole-FAT scan on whichever screen asked.
SdInfo sdInfo() {
    SdInfo i;
    if (!g_mount || !g_card) return i;
    i.mounted  = true;
    i.speedKHz = g_speed;
    snprintf(i.type, sizeof(i.type), "%s",
             g_card->is_mmc ? "MMC" : (g_card->ocr & (1u << 30)) ? "SDHC/SDXC" : "SDSC");
    const uint64_t bytes = static_cast<uint64_t>(g_card->csd.capacity) *
                           static_cast<uint64_t>(g_card->csd.sector_size);
    i.totalKB = static_cast<uint32_t>(bytes / 1024ULL);
    return i;
}

// sdSpace: FAT's own figure, by mount point rather than drive number (the
// drive number comes from the first free slot in the FATFS table, so "0:"
// was a coincidence rather than a contract). A card with a stale free-cluster
// hint, which is what pulling one mid-write leaves, makes this a scan of the
// whole FAT: the runner's, never the loop's.
bool sdSpace(uint64_t& total, uint64_t& freeBytes) {
    total = freeBytes = 0;
    if (!g_mount) return false;
    return esp_vfs_fat_info(BBS_SD_MOUNT, &total, &freeBytes) == ESP_OK;
}

// sdList: FatFs's directory entries, sizes and all, one read of the folder.
bool sdList(const char* rel, SdListFn fn, void* ctx) {
    if (!g_mount || !g_card || !fn) return false;
    char path[160];
    snprintf(path, sizeof(path), "%u:/%s", static_cast<unsigned>(ff_diskio_get_pdrv_card(g_card)), rel ? rel : "");
    FF_DIR d;
    if (f_opendir(&d, path) != FR_OK) return false;
    FILINFO fi;
    // A read that fails part way is not the end of the folder (1.2.0-link.15:
    // the photo system's prune never takes a partial folder for a whole one).
    bool whole = true;
    for (;;) {
        if (f_readdir(&d, &fi) != FR_OK) { whole = false; break; }
        if (!fi.fname[0]) break;
        if (fi.fname[0] == '.') continue;
        if (!fn(ctx, fi.fname, (fi.fattrib & AM_DIR) != 0, static_cast<uint32_t>(fi.fsize))) break;
    }
    f_closedir(&d);
    return whole;
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
#ifdef BBS_HAS_ETH
    // On the wire, the address is the wire's: the one the board gives out
    // (main.cpp, netPick). The station's own, while it is joined beside it
    // (1.2.1), is staIp; a station not joined may still hold a lease for
    // the IDF's lost-IP time, which is not an address anyone reaches.
    if (n.valid) snprintf(n.staIp, sizeof(n.staIp), "%s", n.ip);
    const EthInfo e = ethInfo();
    n.ethLink = e.link;
    n.ethFull = e.full;
    n.ethMbps = e.mbps;
    if (e.up) {
        esp_ip4_addr_t a;
        a.addr = e.ip;
        n.onEth = true;
        snprintf(n.ip, sizeof(n.ip), IPSTR, IP2STR(&a));
    }
#endif
    return n;
}

#ifdef BBS_HAS_ETH
// ===========================================================================
// Ethernet: the W5500 (1.1.2). See platform.h. IDF 5.3.1's own driver
// (components/esp_eth/src/spi/w5500, CONFIG_ETH_SPI_ETHERNET_W5500 in the
// board's sdkconfig layer), on SPI3 with the board's pins (board.h).
//
// The events arrive on the default event loop's task; the loop reads what
// they left through ethInfo. Every field is one word written whole, so a
// read on the other core sees the old value or the new one, never half.
// ===========================================================================
namespace {
esp_eth_handle_t  g_eth       = nullptr;
bool              g_ethStarted = false;   // esp_eth_start succeeded: ethInfo's "started"
volatile bool     g_ethLink   = false;
volatile bool     g_ethUp     = false;
volatile bool     g_ethFull   = false;
volatile uint16_t g_ethMbps   = 0;
volatile uint32_t g_ethIp     = 0;

void onEthEvent(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == ETH_EVENT && id == ETHERNET_EVENT_CONNECTED) {
        esp_eth_handle_t h = *static_cast<esp_eth_handle_t*>(data);
        eth_speed_t  sp = ETH_SPEED_10M;
        eth_duplex_t dx = ETH_DUPLEX_HALF;
        esp_eth_ioctl(h, ETH_CMD_G_SPEED, &sp);
        esp_eth_ioctl(h, ETH_CMD_G_DUPLEX_MODE, &dx);
        g_ethMbps = sp == ETH_SPEED_100M ? 100 : 10;
        g_ethFull = dx == ETH_DUPLEX_FULL;
        g_ethLink = true;
        plat::log("eth: link up, %u Mb/s %s duplex", static_cast<unsigned>(g_ethMbps),
                  g_ethFull ? "full" : "half");
    } else if (base == ETH_EVENT && id == ETHERNET_EVENT_DISCONNECTED) {
        // The address lwIP holds lingers until its lost-IP timer runs out,
        // but nothing reaches it with the cable out: down is down now.
        g_ethLink = false;
        g_ethUp   = false;
        plat::log("eth: link down");
    } else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP) {
        auto* e = static_cast<ip_event_got_ip_t*>(data);
        g_ethIp = e->ip_info.ip.addr;
        g_ethUp = g_ethLink;
        plat::log("eth: address " IPSTR, IP2STR(&e->ip_info.ip));
    } else if (base == IP_EVENT && id == IP_EVENT_ETH_LOST_IP) {
        g_ethUp = false;
        g_ethIp = 0;
        plat::log("eth: address lost");
    }
}
}   // namespace

bool ethBegin(const char* hostname) {
    auto fail = [](const char* what, esp_err_t e) {
        plat::log("eth: %s failed (%s); running on Wi-Fi alone", what, esp_err_to_name(e));
        return false;
    };
    // The W5500's interrupt line is a GPIO interrupt, through the IDF's ISR
    // service. Already installed is fine: whoever did it first, it serves all.
    esp_err_t e = gpio_install_isr_service(0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return fail("the GPIO interrupt service", e);

    spi_bus_config_t bus = {};
    bus.mosi_io_num   = BBS_ETH_MOSI;
    bus.miso_io_num   = BBS_ETH_MISO;
    bus.sclk_io_num   = BBS_ETH_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    const spi_host_device_t host = static_cast<spi_host_device_t>(BBS_ETH_SPI_HOST);
    e = spi_bus_initialize(host, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK) return fail("the SPI bus", e);

    spi_device_interface_config_t dev = {};
    dev.mode           = 0;
    dev.clock_speed_hz = BBS_ETH_MHZ * 1000 * 1000;
    dev.spics_io_num   = BBS_ETH_CS;
    dev.queue_size     = 20;
    eth_w5500_config_t wcfg = ETH_W5500_DEFAULT_CONFIG(host, &dev);
    wcfg.int_gpio_num = BBS_ETH_INT;

    eth_mac_config_t mcfg = ETH_MAC_DEFAULT_CONFIG();
    // The receive task beside Wi-Fi and lwIP on core 0 (this is app_main,
    // which runs there), never on the BBS loop's core: at priority 15 it
    // would take the loop's core for every frame.
    mcfg.flags |= ETH_MAC_FLAG_PIN_TO_CORE;
    eth_phy_config_t pcfg = ETH_PHY_DEFAULT_CONFIG();
    pcfg.reset_gpio_num = BBS_ETH_RST;

    esp_eth_mac_t* mac = esp_eth_mac_new_w5500(&wcfg, &mcfg);
    esp_eth_phy_t* phy = mac ? esp_eth_phy_new_w5500(&pcfg) : nullptr;
    if (!mac || !phy) {
        if (phy) phy->del(phy);
        if (mac) mac->del(mac);
        spi_bus_free(host);
        return fail("the W5500 driver", ESP_FAIL);
    }
    esp_eth_config_t ecfg = ETH_DEFAULT_CONFIG(mac, phy);
    e = esp_eth_driver_install(&ecfg, &g_eth);
    if (e != ESP_OK) {
        // No answer from the chip lands here (its version register is read
        // at init): a board with its W5500 unfitted or dead. The MAC's init
        // may already have hooked the INT pin's interrupt to the object
        // about to be freed, and the driver's error path leaves it there: an
        // edge on the pin would then run into freed memory.
        gpio_isr_handler_remove(static_cast<gpio_num_t>(BBS_ETH_INT));
        gpio_reset_pin(static_cast<gpio_num_t>(BBS_ETH_INT));
        phy->del(phy);
        mac->del(mac);
        spi_bus_free(host);
        g_eth = nullptr;
        return fail("the W5500", e);
    }
    // The W5500 has no address of its own: the chip's Ethernet one, from
    // its efuse (the base MAC plus 3 on the S3), so it is this board's, stable.
    uint8_t addr[6];
    if (esp_read_mac(addr, ESP_MAC_ETH) == ESP_OK) esp_eth_ioctl(g_eth, ETH_CMD_S_MAC_ADDR, addr);

    // Its own netif, "ETH_DEF" as mDNS expects, and the default route over
    // Wi-Fi's while both are up (an Improv trial on a wired board): 128
    // against the station's 100.
    esp_netif_inherent_config_t inh = ESP_NETIF_INHERENT_DEFAULT_ETH();
    inh.route_prio = 128;
    esp_netif_config_t ncfg = {};
    ncfg.base  = &inh;
    ncfg.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
    esp_netif_t* nif = esp_netif_new(&ncfg);
    if (!nif) {
        esp_eth_driver_uninstall(g_eth);    // deinits the MAC and PHY, frees neither
        phy->del(phy);
        mac->del(mac);
        spi_bus_free(host);
        g_eth = nullptr;
        return fail("the Ethernet netif", ESP_ERR_NO_MEM);
    }
    esp_netif_set_hostname(nif, hostname);
    // A failure from here on leaves the driver installed and unused: the
    // board runs on Wi-Fi alone, and rare enough not to tear down.
    // esp_netif_attach calls the glue's post_attach without testing it, so
    // a glue that could not be made is caught here, not as a boot crash.
    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(g_eth);
    if (!glue) return fail("the Ethernet netif glue", ESP_ERR_NO_MEM);
    e = esp_netif_attach(nif, glue);
    if (e != ESP_OK) return fail("attaching the Ethernet netif", e);
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &onEthEvent, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &onEthEvent, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, &onEthEvent, nullptr);
    e = esp_eth_start(g_eth);
    if (e != ESP_OK) return fail("starting Ethernet", e);
    g_ethStarted = true;
    plat::log("eth: W5500 started, waiting for a link");
    return true;
}

EthInfo ethInfo() {
    EthInfo i;
    i.started = g_ethStarted;
    i.link    = g_ethLink;
    i.up      = g_ethUp;
    i.full    = g_ethFull;
    i.mbps    = g_ethMbps;
    i.ip      = g_ethIp;
    return i;
}
#endif  // BBS_HAS_ETH

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

#ifndef BBS_BACKUP_TEST_OPEN          // the test build holds the button, and has none
namespace {
int      g_btnGpio   = -1;
bool     g_btnLast   = false;    // debounced state, true = pressed
bool     g_btnRaw    = false;
uint32_t g_btnSince  = 0;
constexpr uint32_t kDebounceMs = 50;
}
#endif

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

// The RTC's own reset cause, beside the IDF's reading of it (1.1.2). On the
// classic ESP32 the IDF files RESET_REASON_SYS_RTC_WDT (0x10) under
// ESP_RST_WDT, and 0x10 is also what the chip reports after a reset on its
// EN pin that a USB-serial bridge (the ESP32-CAM-MB, opening its port) or a
// RESET button makes: the bench's ESP32-CAM said "watchdog" and told the
// sysop the board had frozen when somebody had only opened the console. Our
// own watchdogs never reach 0x10: the task watchdog panics (a software reset
// with the TASK_WDT hint), the interrupt watchdog is MWDT1, and the RTC
// watchdog's other causes (0x09, 0x0D) are core and CPU resets. The one real
// hang that lands there is the bootloader's own RTC watchdog, a start that
// never reached the app, which is rare enough to name beside the button.
bool g_extReset = false;

void readReset() {
    if (!g_resetRead) {
        g_reset = esp_reset_reason();
#if CONFIG_IDF_TARGET_ESP32
        g_extReset = g_reset == ESP_RST_WDT &&
                     esp_rom_get_reset_reason(0) == RESET_REASON_SYS_RTC_WDT;
#endif
        g_resetRead = true;
    }
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
    if (g_extReset) return "reset pin (EN or a serial port)";
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
    if (g_extReset) return false;
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

#if defined(BBS_HAS_LCD) && !defined(BBS_LCD_RGB)   // an RGB panel: platform_esp32_rgb.cpp
// ===========================================================================
// The panel (BBS_HAS_LCD) through the IDF's esp_lcd: an ST7789 on SPI3 (the
// Waveshare), or an ILI9488 on a 16-bit i80 parallel bus, the S3's LCD_CAM
// peripheral (the Makerfabs Parallel TFT, BBS_LCD_I80).
//
// SPI3 because the SD card's SPI mode takes SPI2 (SDSPI_DEFAULT_HOST), and
// the S3 has exactly those two for general use; Waveshare's demo puts the
// panel on SPI3 as well. A board whose panel and card share their wires
// (BBS_SPI_SHARED) has them both on SPI2 instead (sharedUp, above). The i80
// bus shares nothing with either.
//
// Never waiting in the loop. esp_lcd sends colour data as a queued DMA
// transaction and returns, but the address commands in front of it are
// polled and first wait for anything still queued, so lcdDraw is only
// accepted once the last one has finished: the done interrupt clears a
// flag, lcdReady reads it. The colour data goes from a staging buffer in
// internal DMA memory, allocated here at begin, because the SPI driver
// copies anything it cannot DMA from (PSRAM, where the framebuffer is) into
// a buffer it allocates per transaction, which would be heap in the loop.
// The i80 path keeps the same staging buffer, for the same reason and so the
// two paths send a band the same way.
// ===========================================================================
#if defined(BBS_LCD_ILI9488) && !defined(BBS_LCD_I80)
#error "the ILI9488 is driven on a 16-bit i80 bus only (board.h, BBS_LCD_I80)"
#endif
namespace {

#ifdef BBS_LCD_I80
// A band: 12 rows of a 480-pixel line, RGB565 as the framebuffer holds it
// (COLMOD 0x55 over 16 bits), 11.25 KB of internal DMA memory. About 0.3 ms on
// the bus at a 20 MHz WR clock, one per plugin tick.
constexpr uint32_t kBandPixels = 480u * 12u;
constexpr uint32_t kBpp        = 2u;
#else
// A band: 16 rows of a 320-pixel line, 10 KB. About 8 ms on the wire at
// 10 MHz and 2 ms at 40, one per plugin tick.
constexpr uint32_t kBandPixels = 320u * 16u;
constexpr uint32_t kBpp        = 2u;
#ifdef BBS_SPI_SHARED
static_assert(kBandPixels * 2u <= static_cast<uint32_t>(kSharedBytes),
              "the shared bus is raised for one panel band");
constexpr spi_host_device_t kLcdHost = kSharedHost;    // with the card (above)
#else
constexpr spi_host_device_t kLcdHost = SPI3_HOST;
#endif
#endif

struct Lcd {
    esp_lcd_panel_io_handle_t io     = nullptr;
    esp_lcd_panel_handle_t    panel  = nullptr;   // the ST7789's esp_lcd driver; unused for the ILI9488
#ifdef BBS_LCD_I80
    esp_lcd_i80_bus_handle_t  i80    = nullptr;   // the parallel bus
#endif
    uint8_t*                  stage  = nullptr;   // kBandPixels x kBpp bytes
    bool                      up     = false;     // set up and on: bands may be drawn
    bool                      busUp  = false;
    bool                      blUp   = false;
    int8_t                    blPin  = -1;      // the backlight's pin while blUp
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
struct LcdInit { uint8_t cmd; uint8_t n; uint8_t data[15]; };
#ifdef BBS_LCD_ILI9488
// The ILI9488's own settings after its software reset: the gamma curves,
// power, VCOM, interface, frame rate, inversion, display function, entry
// mode and adjust control. The values the common ILI9488 drivers send for a
// TN glass like this one (the IDF component registry's atanisoft/
// esp_lcd_ili9488 "default" table, which Makerfabs' own IDF board support
// uses for their ILI9488 boards, and LovyanGFX's Panel_ILI9488, which their
// Parallel TFT demos run). Register values as they send them; MADCTL and
// COLMOD follow in lcdBegin.
const LcdInit kLcdInit[] = {
    { 0xE0, 15, { 0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78, 0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F } },
    { 0xE1, 15, { 0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45, 0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F } },
    { 0xC0, 2,  { 0x17, 0x15 } },                                     // power control 1
    { 0xC1, 1,  { 0x41 } },                                           // power control 2
    { 0xC5, 3,  { 0x00, 0x12, 0x80 } },                               // VCOM
    { 0xB0, 1,  { 0x00 } },                                           // interface mode: SDO on
    { 0xB1, 1,  { 0xA0 } },                                           // frame rate, 60 Hz
    { 0xB4, 1,  { 0x02 } },                                           // inversion: 2-dot
    { 0xB6, 3,  { 0x02, 0x02, 0x3B } },                               // display function
    { 0xB7, 1,  { 0xC6 } },                                           // entry mode
    { 0xF7, 4,  { 0xA9, 0x51, 0x2C, 0x02 } },                         // adjust control 3
};
#else
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
#endif

void lcdBlSet(uint8_t pct) {
    if (!g_lcd.blUp) {
        log("panel: backlight asked for %u%%, but it has no pin running", static_cast<unsigned>(pct));
        return;
    }
    uint32_t duty = pct >= 100 ? 8191u : static_cast<uint32_t>(pct) * 8191u / 100u;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    // Said on the console at every change (start, silent, a CONFIG save:
    // never per frame), with the duty given to the LEDC, so a dark glass can
    // be told from a backlight that was never asked for.
    log("panel: backlight gpio %d at %u%%, PWM duty %u of 8191, active high", g_lcd.blPin,
        static_cast<unsigned>(pct), static_cast<unsigned>(duty));
}

bool lcdFail(char* err, size_t n, const char* why, esp_err_t e) {
    if (err && n) snprintf(err, n, "%s (%s)", why, esp_err_to_name(e));
    log("panel: %s (%s)", why, esp_err_to_name(e));
    lcdEnd();
    return false;
}

#ifdef BBS_LCD_ILI9488
// iliMadctl: the ILI9488's MADCTL for a rotation and the glass's mirror, the
// same four turns and the same mirror rule as the ST7789 path below (and as
// panelgfx::scanFor works them out): MY 0x80, MX 0x40, MV 0x20, BGR 0x08.
uint8_t iliMadctl(const LcdCfg& c) {
    bool swap = false, mx = false, my = false;
    switch (c.rotation) {
        case 90:  swap = true;  mx = true;  break;
        case 180: mx = true;    my = true;  break;
        case 270: swap = true;  my = true;  break;
        default:  break;
    }
    if (c.mirror) {
        if (swap) my = !my;
        else      mx = !mx;
    }
    return static_cast<uint8_t>((my ? 0x80 : 0) | (mx ? 0x40 : 0) | (swap ? 0x20 : 0) | (c.bgr ? 0x08 : 0));
}

// iliWindow: the column and row addresses of a rectangle, the gaps added.
esp_err_t iliWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    const uint16_t x0 = static_cast<uint16_t>(x + g_lcd.cfg.xoff), x1 = static_cast<uint16_t>(x0 + w - 1);
    const uint16_t y0 = static_cast<uint16_t>(y + g_lcd.cfg.yoff), y1 = static_cast<uint16_t>(y0 + h - 1);
    const uint8_t ca[4] = { static_cast<uint8_t>(x0 >> 8), static_cast<uint8_t>(x0), static_cast<uint8_t>(x1 >> 8),
                            static_cast<uint8_t>(x1) };
    const uint8_t ra[4] = { static_cast<uint8_t>(y0 >> 8), static_cast<uint8_t>(y0), static_cast<uint8_t>(y1 >> 8),
                            static_cast<uint8_t>(y1) };
    esp_err_t e = esp_lcd_panel_io_tx_param(g_lcd.io, 0x2A, ca, 4);        // CASET
    if (e == ESP_OK) e = esp_lcd_panel_io_tx_param(g_lcd.io, 0x2B, ra, 4); // RASET
    return e;
}
#endif

}   // namespace

bool lcdBegin(const LcdCfg& c, char* err, size_t errLen) {
    lcdEnd();
    if (err && errLen) err[0] = '\0';
    esp_err_t e;

#ifdef BBS_LCD_I80
    // The i80 bus: WR is LcdCfg's mosi and RD its sclk (board.h). RD is only
    // for reading the panel, which nothing does: held high, idle.
    if (c.sclk >= 0) {
        gpio_config_t g = {};
        g.pin_bit_mask = 1ULL << c.sclk;
        g.mode = GPIO_MODE_OUTPUT;
        gpio_config(&g);
        gpio_set_level(static_cast<gpio_num_t>(c.sclk), 1);
    }
    esp_lcd_i80_bus_config_t bus = {};
    static const int kData[] = { BBS_LCD_DATA_PINS };
    static_assert(sizeof(kData) / sizeof(kData[0]) == 16, "a 16-bit bus names sixteen data pins");
    bus.dc_gpio_num = static_cast<gpio_num_t>(c.dc);
    bus.wr_gpio_num = static_cast<gpio_num_t>(c.mosi);
    bus.clk_src     = LCD_CLK_SRC_DEFAULT;
    for (int i = 0; i < 16; ++i) bus.data_gpio_nums[i] = static_cast<gpio_num_t>(kData[i]);
    bus.bus_width          = 16;
    bus.max_transfer_bytes = kBandPixels * kBpp;
    e = esp_lcd_new_i80_bus(&bus, &g_lcd.i80);
    if (e != ESP_OK) return lcdFail(err, errLen, "the parallel bus would not start: check the pins", e);
    g_lcd.busUp = true;

    esp_lcd_panel_io_i80_config_t io = {};
    io.cs_gpio_num       = static_cast<gpio_num_t>(c.cs);
    io.pclk_hz           = static_cast<uint32_t>(c.mhz) * 1000u * 1000u;
    io.trans_queue_depth = 2;
    io.on_color_trans_done = lcdDone;
    io.lcd_cmd_bits      = 8;
    io.lcd_param_bits    = 8;
    io.dc_levels.dc_idle_level  = 0;
    io.dc_levels.dc_cmd_level   = 0;
    io.dc_levels.dc_dummy_level = 0;
    io.dc_levels.dc_data_level  = 1;
    e = esp_lcd_new_panel_io_i80(g_lcd.i80, &io, &g_lcd.io);
    if (e != ESP_OK) return lcdFail(err, errLen, "the panel would not take the parallel bus", e);
#else
#ifdef BBS_SPI_SHARED
    {
        const char* why = nullptr;
        if (!sharedUp(SHARED_PANEL, c.mosi, -1, c.sclk, why)) return lcdFail(err, errLen, why, ESP_ERR_INVALID_ARG);
        if (!(g_sharedUsers & SHARED_CARD)) cardQuiet();
    }
#else
    spi_bus_config_t bus = {};
    bus.mosi_io_num     = c.mosi;
    bus.miso_io_num     = -1;
    bus.sclk_io_num     = c.sclk;
    bus.quadwp_io_num   = -1;
    bus.quadhd_io_num   = -1;
    bus.max_transfer_sz = static_cast<int>(kBandPixels * kBpp);
    e = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK) return lcdFail(err, errLen, "the SPI bus would not start: check the pins", e);
#endif
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
    e = esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(kLcdHost), &io, &g_lcd.io);
    if (e != ESP_OK) return lcdFail(err, errLen, "the panel would not take the SPI bus", e);
#endif

#ifdef BBS_LCD_ILI9488
    // No esp_lcd driver for the ILI9488 in IDF 5.3.1, and only a handful of
    // commands are needed, so they are sent here. A reset pin when there is
    // one, else the software reset (this board's RESET is the chip's EN).
    // The reset, sleep-out and display-on waits are the datasheet's (5 ms
    // after a reset before a command, 120 ms before sleep-out, 5 ms after
    // it): this is a plugin's start, never the loop.
    if (c.rst >= 0) {
        gpio_config_t g = {};
        g.pin_bit_mask = 1ULL << c.rst;
        g.mode = GPIO_MODE_OUTPUT;
        gpio_config(&g);
        gpio_set_level(static_cast<gpio_num_t>(c.rst), 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(static_cast<gpio_num_t>(c.rst), 1);
    } else {
        e = esp_lcd_panel_io_tx_param(g_lcd.io, 0x01, nullptr, 0);         // SWRESET
        if (e != ESP_OK) return lcdFail(err, errLen, "the panel did not answer its reset", e);
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    for (const LcdInit& in : kLcdInit) {
        e = esp_lcd_panel_io_tx_param(g_lcd.io, in.cmd, in.data, in.n);
        if (e != ESP_OK) return lcdFail(err, errLen, "the panel refused its settings", e);
    }
    const uint8_t mad = iliMadctl(c), colmod = 0x55;                      // RGB565 over 16 bits
    e = esp_lcd_panel_io_tx_param(g_lcd.io, 0x36, &mad, 1);                 // MADCTL
    if (e == ESP_OK) e = esp_lcd_panel_io_tx_param(g_lcd.io, 0x3A, &colmod, 1);   // COLMOD
    if (e == ESP_OK) e = esp_lcd_panel_io_tx_param(g_lcd.io, c.invert ? 0x21 : 0x20, nullptr, 0);
    if (e == ESP_OK) e = esp_lcd_panel_io_tx_param(g_lcd.io, 0x11, nullptr, 0);   // SLPOUT
    if (e != ESP_OK) return lcdFail(err, errLen, "the panel refused its settings", e);
    vTaskDelay(pdMS_TO_TICKS(120));
#else
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
#endif

    g_lcd.stage = static_cast<uint8_t*>(heap_caps_malloc(kBandPixels * kBpp, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
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
        if (ledc_timer_config(&t) == ESP_OK && ledc_channel_config(&ch) == ESP_OK) {
            g_lcd.blUp  = true;
            g_lcd.blPin = c.bl;
        } else {
            log("panel: the backlight on gpio %d would not start", c.bl);
        }
    }
#ifdef BBS_LCD_ILI9488
    esp_lcd_panel_io_tx_param(g_lcd.io, 0x29, nullptr, 0);                // DISPON
#else
    esp_lcd_panel_disp_on_off(g_lcd.panel, true);
#endif
    g_lcd.cfg  = c;
    g_lcd.busy = false;
    g_lcd.up   = true;
    lcdBlSet(c.backlight);
#ifdef BBS_LCD_I80
    log("panel: %s %ux%u, rotation %u, %u MHz, on a 16-bit parallel bus", BBS_LCD_DRIVER,
        static_cast<unsigned>(c.width), static_cast<unsigned>(c.height), static_cast<unsigned>(c.rotation),
        static_cast<unsigned>(c.mhz));
#else
    log("panel: %s %ux%u, rotation %u, %u MHz, on SPI%d", BBS_LCD_DRIVER, static_cast<unsigned>(c.width),
        static_cast<unsigned>(c.height), static_cast<unsigned>(c.rotation), static_cast<unsigned>(c.mhz),
        static_cast<int>(kLcdHost) + 1);
#endif
    return true;
}

bool lcdSame(const LcdCfg& c) {
    const LcdCfg& o = g_lcd.cfg;
    return g_lcd.up && o.mosi == c.mosi && o.sclk == c.sclk && o.cs == c.cs && o.dc == c.dc &&
           o.rst == c.rst && o.bl == c.bl && o.width == c.width && o.height == c.height &&
           o.xoff == c.xoff && o.yoff == c.yoff && o.rotation == c.rotation &&
           o.invert == c.invert && o.bgr == c.bgr && o.mirror == c.mirror && o.mhz == c.mhz;
}

void lcdEnd() {
    if (g_lcd.blUp) {
        lcdBlSet(0);
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        g_lcd.blUp  = false;
        g_lcd.blPin = -1;
    }
    if (g_lcd.panel) {
        esp_lcd_panel_disp_on_off(g_lcd.panel, false);   // waits out a band in flight
        esp_lcd_panel_del(g_lcd.panel);
        g_lcd.panel = nullptr;
    }
#ifdef BBS_LCD_ILI9488
    if (g_lcd.io && g_lcd.up) {
        // A command waits out a band in flight, as the ST7789 path's does.
        esp_lcd_panel_io_tx_param(g_lcd.io, 0x28, nullptr, 0);            // DISPOFF
        esp_lcd_panel_io_tx_param(g_lcd.io, 0x10, nullptr, 0);            // SLPIN
    }
#endif
    if (g_lcd.io) {
        esp_lcd_panel_io_del(g_lcd.io);
        g_lcd.io = nullptr;
    }
    if (g_lcd.busUp) {
#ifdef BBS_LCD_I80
        esp_lcd_del_i80_bus(g_lcd.i80);
        g_lcd.i80 = nullptr;
        // RD was a plain output held high: give it back, or a pin moved in
        // CONFIG leaves the old one driven.
        if (g_lcd.cfg.sclk >= 0) gpio_reset_pin(static_cast<gpio_num_t>(g_lcd.cfg.sclk));
#elif defined(BBS_SPI_SHARED)
        sharedDown(SHARED_PANEL);
#else
        spi_bus_free(SPI3_HOST);
#endif
        g_lcd.busUp = false;
    }
    heap_caps_free(g_lcd.stage);
    g_lcd.stage = nullptr;
    g_lcd.up    = false;
    g_lcd.busy  = false;
    g_lcd.cfg   = LcdCfg();
}

bool lcdReady() {
    return g_lcd.up && g_lcd.stage && !g_lcd.busy;
}

uint32_t lcdBandPixels() {
    return kBandPixels;
}

bool lcdDraw(const uint16_t* fb, uint16_t stride, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!lcdReady() || !fb || !w || !h) return false;
    if (static_cast<uint32_t>(w) * h > kBandPixels) return false;
#ifdef BBS_SPI_SHARED
    // A card command on another task holds the bus: this band waits for a
    // later tick rather than the loop waiting for the card (see sharedCmd).
    if (xSemaphoreTake(g_sharedMux, 0) != pdTRUE) return false;
#endif
    uint16_t* out = reinterpret_cast<uint16_t*>(g_lcd.stage);
    for (uint16_t r = 0; r < h; ++r) {
        memcpy(out, fb + static_cast<size_t>(y + r) * stride + x, static_cast<size_t>(w) * 2u);
        out += w;
    }
#ifdef BBS_LCD_ILI9488
    // The framebuffer's RGB565 as it sits in memory: on the 16-bit bus a
    // pixel is one WR cycle, its low byte on D0 to D7, which is the ILI9488's
    // 16-bit RGB565 order (D15 to D11 red).
    if (iliWindow(x, y, w, h) != ESP_OK) return false;
    g_lcd.busy = true;
    esp_err_t e = esp_lcd_panel_io_tx_color(g_lcd.io, 0x2C, g_lcd.stage,                 // RAMWR
                                            static_cast<size_t>(w) * h * kBpp);
#else
    g_lcd.busy = true;
    esp_err_t e = esp_lcd_panel_draw_bitmap(g_lcd.panel, x, y, x + w, y + h, g_lcd.stage);
#ifdef BBS_SPI_SHARED
    xSemaphoreGive(g_sharedMux);
#endif
#endif
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

#if defined(BBS_HAS_TOUCH) && !defined(BBS_TOUCH_POLL)
// ---------------------------------------------------------------------------
// Touch (BBS_HAS_TOUCH): taps counted from the controller's INT line. The
// CST816's INT goes low for a moment at each report while a finger is down,
// about every 10 ms, so an edge that follows the last by less than
// kTapGapMs is the same touch. I2C only at start, on port 0 (the camera's
// SCCB has port 1, esp32-camera's default), with the driver removed again.
// ---------------------------------------------------------------------------
namespace {
volatile uint16_t g_taps     = 0;
volatile int64_t  g_tapLast  = 0;
portMUX_TYPE      g_tapMux   = portMUX_INITIALIZER_UNLOCKED;   // the ISR and touchTaps
bool              g_touchIsr = false;
uint8_t           g_touchId  = 0;
constexpr i2c_port_t kTouchPort = I2C_NUM_0;

void IRAM_ATTR touchEdge(void*) {
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&g_tapMux);
    if (now - g_tapLast >= static_cast<int64_t>(kTapGapMs) * 1000) g_taps = static_cast<uint16_t>(g_taps + 1);
    g_tapLast = now;
    portEXIT_CRITICAL_ISR(&g_tapMux);
}

#ifdef BBS_TOUCH_FT6236
// The FT6236's (FocalTech's FT6x36 register map, MF35V2): 0xA3 the chip
// ID. Its INT needs no setting: in the default mode it is low while a
// finger is down, which is one falling edge a touch.
constexpr uint8_t kRegChipId  = 0xA3;
#else
// The CST816's registers (Hynitron's CST816S register map, which the D
// shares): 0xA7 ChipID, 0xFA IrqCtl (0x40 EnTouch: pulse while touched;
// 0x20 EnChange: pulse when the touch changes), 0xFE DisAutoSleep.
constexpr uint8_t kRegChipId  = 0xA7;
constexpr uint8_t kRegIrqCtl  = 0xFA;
constexpr uint8_t kIrqTouch   = 0x60;
#endif
}   // namespace

bool touchBegin(char* err, size_t errLen) {
    if (err && errLen) err[0] = '\0';
    // The interrupt first: the taps count whether or not I2C answers below.
    if (!g_touchIsr) {
        gpio_config_t io = {};
        io.pin_bit_mask  = 1ULL << BBS_TOUCH_INT;
        io.mode          = GPIO_MODE_INPUT;
        io.pull_up_en    = GPIO_PULLUP_ENABLE;
        io.intr_type     = GPIO_INTR_NEGEDGE;
        gpio_config(&io);
        const esp_err_t svc = gpio_install_isr_service(0);
        if ((svc == ESP_OK || svc == ESP_ERR_INVALID_STATE) &&
            gpio_isr_handler_add(static_cast<gpio_num_t>(BBS_TOUCH_INT), touchEdge, nullptr) == ESP_OK)
            g_touchIsr = true;
        else
            log("touch: the interrupt on gpio %d would not start", BBS_TOUCH_INT);
    }
    // Once: who it is, and INT pulsing on a touch. A few ms, a plugin's start.
    i2c_config_t c = {};
    c.mode             = I2C_MODE_MASTER;
    c.sda_io_num       = BBS_TOUCH_SDA;
    c.scl_io_num       = BBS_TOUCH_SCL;
    c.sda_pullup_en    = GPIO_PULLUP_ENABLE;
    c.scl_pullup_en    = GPIO_PULLUP_ENABLE;
    c.master.clk_speed = 400000;
    bool ok = i2c_param_config(kTouchPort, &c) == ESP_OK &&
              i2c_driver_install(kTouchPort, I2C_MODE_MASTER, 0, 0, 0) == ESP_OK;
    if (ok) {
        uint8_t reg = kRegChipId, id = 0;
        ok = i2c_master_write_read_device(kTouchPort, BBS_TOUCH_ADDR, &reg, 1, &id, 1, pdMS_TO_TICKS(50)) == ESP_OK;
        if (ok) {
            g_touchId = id;
#ifndef BBS_TOUCH_FT6236
            const uint8_t w[2] = { kRegIrqCtl, kIrqTouch };
            if (i2c_master_write_to_device(kTouchPort, BBS_TOUCH_ADDR, w, 2, pdMS_TO_TICKS(50)) != ESP_OK)
                log("touch: chip 0x%02x would not take its interrupt setting", static_cast<unsigned>(id));
#endif
        }
        i2c_driver_delete(kTouchPort);
    }
    if (!ok) {
        if (err && errLen) snprintf(err, errLen, "the touch controller did not answer at 0x%02x",
                                    static_cast<unsigned>(BBS_TOUCH_ADDR));
        return false;
    }
    log("touch: " BBS_TOUCH_CHIP " chip 0x%02x at 0x%02x, taps on gpio %d", static_cast<unsigned>(g_touchId),
        static_cast<unsigned>(BBS_TOUCH_ADDR), BBS_TOUCH_INT);
    return true;
}

uint8_t touchChip() { return g_touchId; }

// touchTaps: taken and cleared as one, under the ISR's lock, so an edge
// between the two is never lost.
uint16_t touchTaps() {
    portENTER_CRITICAL(&g_tapMux);
    const uint16_t n = g_taps;
    g_taps = 0;
    portEXIT_CRITICAL(&g_tapMux);
    return n;
}
#endif  // BBS_HAS_TOUCH && !BBS_TOUCH_POLL (the 4.3B polls its GT911: platform_esp32_rgb.cpp)
#endif  // BBS_HAS_LCD

#ifdef BBS_HAS_CHIP_TEMP
// chipTemp: the S3's own sensor, installed at the first call (the panel's
// start) for 20 to 100 C: the die runs above the air round it, and the
// panel's warm and risk steps (65, 75) sit inside that range, with an error
// under 2 C (the IDF's table for the S3).
bool chipTemp(int& tenthsC) {
    static temperature_sensor_handle_t h = nullptr;
    static bool tried = false;
    if (!h) {
        if (tried) return false;
        tried = true;
        temperature_sensor_config_t c = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
        if (temperature_sensor_install(&c, &h) != ESP_OK || temperature_sensor_enable(h) != ESP_OK) {
            h = nullptr;
            log("temp: the chip's sensor would not start");
            return false;
        }
    }
    float c = 0;
    if (temperature_sensor_get_celsius(h, &c) != ESP_OK) return false;
    tenthsC = static_cast<int>(c * 10.0f + (c < 0 ? -0.5f : 0.5f));
    return true;
}
#endif

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

// camMemLog: internal RAM as the camera sees it, on the console, so a
// failed bring-up says which budget it met. The walks are the worker's.
void camMemLog(const char* when) {
    plat::log("camera: %s: internal free %u, largest %u, largest DMA %u", when,
              static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
              static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
              static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)));
}

#ifdef BBS_CAM_PWDN_IS_POWER
// ---------------------------------------------------------------------------
// The camera's supply, on a board whose PWDN line is a power switch (board.h,
// the ESP32-S3-ETH's Q3 on GPIO 8; 1.2.1-eth.8). Sequenced here, not by the
// driver: esp32-camera 2.1.7 holds PWDN high (off) for 10 ms and probes the
// bus 30 ms after it goes low (esp_camera.c 198-226). 10 ms does not empty
// the two LDOs' output capacitors, and the sensor's RESET is an RC to the
// always-on 3V3, so it is never pulsed after the board's first power-up: a
// sensor switched off a few seconds earlier came back from a part-emptied
// supply with no reset, and the probe found nothing. Measured on COM25
// (eth.4), snaps 1, 2, 3 and 5 s after the last one's download question:
// 4/4, 2/4, 4/4, 4/4 saved, and one more of six failed at about 3.5 s off
// earlier, all "no camera found". So:
//   off   a supply switched off is left off for kCamOffMs before it comes
//         on again (5 s: every snap with at least that much off time
//         probed), counted with plat::since from the switch-off; the board's
//         own reset counts as a switch-off at 0
//   on    then kCamSettleMs before the driver probes (the OV5640 asks
//         about 20 ms from power to SCCB)
// Both waits are vTaskDelay on the camera's worker (the runner), which
// already takes seconds for a snap; never the loop (Rule no. 1). A snap
// asked for straight after another waits out the off time behind its
// spinner, and so does any runner job queued behind it.
// ---------------------------------------------------------------------------
constexpr uint32_t kCamOffMs    = 5000;
constexpr uint32_t kCamSettleMs = 50;
bool     g_camPowered = false;   // GPIO 8 low: the supply on
uint32_t g_camOffAt   = 0;       // millis the supply went off; the reset is 0

void camPowerOff() {
    gpio_set_level(static_cast<gpio_num_t>(BBS_CAM_PWDN), 1);
    if (g_camPowered) g_camOffAt = plat::millis();
    g_camPowered = false;
}

void camPowerOn() {
    static bool pinSet = false;
    if (!pinSet) {
        gpio_config_t conf = {};
        conf.pin_bit_mask = 1ULL << BBS_CAM_PWDN;
        conf.mode = GPIO_MODE_OUTPUT;
        gpio_set_level(static_cast<gpio_num_t>(BBS_CAM_PWDN), 1);   // off until the hold is done
        gpio_config(&conf);
        gpio_set_level(static_cast<gpio_num_t>(BBS_CAM_PWDN), 1);
        pinSet = true;
    }
    if (g_camPowered) return;
    const uint32_t off = plat::since(plat::millis(), g_camOffAt);
    if (off < kCamOffMs) vTaskDelay(pdMS_TO_TICKS(kCamOffMs - off));
    gpio_set_level(static_cast<gpio_num_t>(BBS_CAM_PWDN), 0);
    g_camPowered = true;
    vTaskDelay(pdMS_TO_TICKS(kCamSettleMs));
}
#endif

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
    // driver's one DMA buffer (kCamDmaBlock) is its first large allocation and the
    // one that fails, and a failure costs a sensor probe and a bus set up
    // and torn down for nothing. The loop's check before the worker started
    // could not see the worker's stack come out of the same memory.
    if (camDmaLargest() < kCamDmaBlock || camInternalFree() < kCamInternal) {
        camMemLog("not enough memory to start");
        return fail("not enough memory for the camera");
    }

    camera_config_t cfg = {};
#ifdef BBS_CAM_PWDN_IS_POWER
    cfg.pin_pwdn     = -1;            // the supply is ours to sequence (camPowerOn)
#else
    cfg.pin_pwdn     = BBS_CAM_PWDN;
#endif
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

    // The driver's ll_cam_set_pin installs the GPIO ISR service at every
    // bring-up and leaves it installed at deinit, so from the second snap on
    // the IDF's gpio driver said "GPIO isr service already installed" on the
    // console at every snap (1.1.2, from the bench). The call is harmless
    // (the driver ignores the answer); the line is only noise. Quiet for the
    // bring-up, then as it was, and only its own tag.
    const esp_log_level_t gpioWas = esp_log_level_get("gpio");
    esp_log_level_set("gpio", ESP_LOG_NONE);
    auto initOnce = [&]() {
#ifdef BBS_CAM_PWDN_IS_POWER
        camPowerOn();
#endif
        esp_err_t r = esp_camera_init(&cfg);
        if (r == ESP_ERR_NOT_SUPPORTED && !g_camRaw && cfg.pixel_format == PIXFORMAT_JPEG) {
            // Either nothing answered on the bus, or a sensor answered that
            // cannot give JPEG ("JPEG format is not supported on this sensor"):
            // the same code for both, so ask again for RGB565, which every
            // sensor the driver knows gives. Only the second is then a camera.
            if (esp_camera_sensor_get()) esp_camera_deinit();
            cfg.pixel_format = PIXFORMAT_RGB565;
            cfg.xclk_freq_hz = kCamRawXclk;
            r = esp_camera_init(&cfg);
            if (r == ESP_OK) {
                g_camRaw = true;
                plat::log("camera: the sensor gives no JPEG: raw frames, encoded on the worker");
            } else {
                cfg.pixel_format = PIXFORMAT_JPEG;     // a retry asks for JPEG again
                cfg.xclk_freq_hz = 20000000;
            }
        }
        return r;
    };
    esp_err_t e = initOnce();
#ifdef BBS_CAM_PWDN_IS_POWER
    // Nothing on the bus: once more after a full power-off (the hold), in
    // case the supply came up from a part-emptied state after all.
    if (e == ESP_ERR_CAMERA_NOT_DETECTED || e == ESP_ERR_NOT_SUPPORTED || e == ESP_ERR_NOT_FOUND) {
        if (esp_camera_sensor_get()) esp_camera_deinit();
        plat::log("camera: no sensor answered (0x%x); power off, wait, and once more",
                  static_cast<unsigned>(e));
        camPowerOff();
        e = initOnce();
    }
#endif
    esp_log_level_set("gpio", gpioWas);
    if (e != ESP_OK) {
        plat::log("camera: init failed: %s (0x%x)", esp_err_to_name(e), static_cast<unsigned>(e));
        // esp_camera_init tears down what it built on every failing path
        // but one (cam_init, which frees its own), so as a rule this finds
        // nothing left. A sensor still registered is a partial start, and
        // everything it holds goes back: the DMA block, cam_task, the SCCB
        // bus and XCLK's LEDC channel.
        if (esp_camera_sensor_get()) esp_camera_deinit();
#ifdef BBS_CAM_PWDN_IS_POWER
        camPowerOff();                                              // the camera's power off again
#endif
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
#ifdef BBS_CAM_PWDN_IS_POWER
    // A board whose PWDN line switches the camera's supply (board.h): off
    // between snaps, as it was at reset, and stamped so the next bring-up
    // leaves it off long enough (camPowerOn). It sits on strapping pins
    // across a reset, so it is never left on.
    camPowerOff();
#endif
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
#if CONFIG_OV5640_SUPPORT
    // The OV5640 (the Touch-LCD-2's camera, WS2 1.0.0): exposure in 0x3500
    // to 0x3502, bits 19:4 whole lines, and gain in 0x350A-0x350B, ten bits
    // (OV5640 datasheet, the AEC/AGC registers). Its driver's get_reg reads
    // 16 bits for a mask over 0xFF and 24 for one over 0xFFFF.
    if (g_camPid == OV5640_PID) {
        const int exp = s->get_reg(s, 0x3500, 0xFFFFF), gain = s->get_reg(s, 0x350A, 0x3FF);
        if (exp < 0 || gain < 0) return CAM_METER_NONE;
        a = static_cast<uint16_t>(exp >> 4);
        b = static_cast<uint16_t>(gain);
        return CAM_METER_EXPOSURE;
    }
#endif
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

#if defined(BBS_HAS_SSH) && BBS_HAS_SSH
// ---------------------------------------------------------------------------
// The SSH server's footing (1.1.2, S3 only). The task sits on core 0 just
// above idle: Wi-Fi (23), lwIP (18) and the timer task take the CPU from it
// whenever they want, and core 1 stays the loop's. Its stack is internal:
// a PSRAM stack would be allowed on the S3 only while the task never touched
// flash, and the rule is easier kept by not needing it (research 3.3).
// ---------------------------------------------------------------------------
namespace {
struct SideTramp { void (*fn)(void*); void* arg; };
SideTramp g_side;
void sideMain(void*) {
    g_side.fn(g_side.arg);
    vTaskDelete(nullptr);
}
bool g_wakeReg = false;
}

bool sideTask(void (*fn)(void*), void* arg, uint32_t stackBytes, const char* name) {
    g_side.fn  = fn;
    g_side.arg = arg;
    BaseType_t ok = xTaskCreatePinnedToCore(sideMain, name, stackBytes, nullptr,
                                            BBS_SSH_PRIO, nullptr, BBS_SSH_CORE);
    return ok == pdPASS;
}

uint32_t sideStackFree() {
    return static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr)) * sizeof(StackType_t);
}

int wakeOpen(uint8_t wakeMax) {
    if (!g_wakeReg) {
        esp_vfs_eventfd_config_t cfg = ESP_VFS_EVENTD_CONFIG_DEFAULT();
        cfg.max_fds = wakeMax;
        if (esp_vfs_eventfd_register(&cfg) != ESP_OK) return -1;
        g_wakeReg = true;
    }
    return eventfd(0, 0);
}

void wakePost(int fd) {
    uint64_t one = 1;
    if (fd >= 0) (void)write(fd, &one, sizeof(one));
}

bool wakeTake(int fd) {
    uint64_t v = 0;
    return fd >= 0 && read(fd, &v, sizeof(v)) == static_cast<ssize_t>(sizeof(v)) && v;
}

void* extAlloc(size_t n) {
    return heap_caps_malloc_prefer(n, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void* extRealloc(void* p, size_t n) {
    return heap_caps_realloc_prefer(p, n, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void extFree(void* p) {
    heap_caps_free(p);
}

uint32_t extFreeBytes() {
    return static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
#endif  // BBS_HAS_SSH

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
