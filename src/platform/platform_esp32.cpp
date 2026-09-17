/*
 * File:        src/platform/platform_esp32.cpp
 * Description: ESP32 (ESP-IDF) implementation of the platform layer.
 * Listing:     COMPLETE FILE
 * Libraries:   ESP-IDF: esp_timer, esp_hw_support (esp_random), heap,
 *              esp_driver_gpio, esp_rom (ROM miniz tinfl)
 */
#include "platform.h"
#include "../config.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
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
