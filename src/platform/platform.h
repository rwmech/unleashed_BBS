/*
 * File:        src/platform/platform.h
 * Description: Thin platform layer. The core only touches the hardware
 *              through these calls, so the same core compiles for the
 *              ESP32 (platform_esp32.cpp) and for a Linux host test build
 *              (host/platform_host.cpp). Sockets use the BSD API, which
 *              lwIP provides on the ESP32.
 * Listing:     COMPLETE FILE
 * Libraries:   none
 */
#pragma once
#include <cstdint>
#include <cstddef>

namespace plat {

struct HeapStats {
    uint32_t freeBytes;     // current free 8-bit heap
    uint32_t minFree;       // lowest free heap since boot
    uint32_t largestBlock;  // largest contiguous free block
    bool     valid;         // false on hosts that cannot report
};

// ---------------------------------------------------------------------------
// millis: monotonic milliseconds since boot (wraps ~49 days, callers use
// unsigned subtraction so wrap is harmless)
// ---------------------------------------------------------------------------
uint32_t millis();

// ---------------------------------------------------------------------------
// random32: hardware RNG on ESP32, libc rand on host
// ---------------------------------------------------------------------------
uint32_t random32();

// ---------------------------------------------------------------------------
// fsBase: mount point of the data filesystem, no trailing slash
// ---------------------------------------------------------------------------
const char* fsBase();

// ---------------------------------------------------------------------------
// heap: heap statistics for MEM command and C1 per-session measurement
// ---------------------------------------------------------------------------
HeapStats heap();

// ---------------------------------------------------------------------------
// log: printf-style line to the console (UART on ESP32, stdout on host)
// ---------------------------------------------------------------------------
void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

} // namespace plat
