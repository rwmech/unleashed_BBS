/*
 * File:        src/platform/platform_esp32.cpp
 * Description: ESP32 (ESP-IDF) implementation of the platform layer.
 * Listing:     COMPLETE FILE
 * Libraries:   ESP-IDF: esp_timer, esp_hw_support (esp_random), heap
 */
#include "platform.h"
#include "../config.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include <cstdarg>
#include <cstdio>

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

} // namespace plat
