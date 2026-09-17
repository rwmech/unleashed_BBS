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
// fsBase: mount point of the data filesystem (screens, system.cfg), no
// trailing slash. logsBase: mount point of the separate logs filesystem.
// ---------------------------------------------------------------------------
const char* fsBase();
const char* logsBase();

// ---------------------------------------------------------------------------
// heap: heap statistics for MEM command and per-session measurement
// ---------------------------------------------------------------------------
HeapStats heap();

// ---------------------------------------------------------------------------
// log: printf-style line to the console (UART on ESP32, stdout on host)
// ---------------------------------------------------------------------------
void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// ---------------------------------------------------------------------------
// backupButtonBegin / backupButtonPressed: the physical button that opens
// the backup window. Pressed() returns true once per debounced press.
// A test build (BBS_BACKUP_TEST_OPEN) reports a press on every call.
// ---------------------------------------------------------------------------
void backupButtonBegin(int gpio);
bool backupButtonPressed(uint32_t now);

// ---------------------------------------------------------------------------
// inflateRaw: decode a raw DEFLATE stream (ZIP method 8). in() fills a
// buffer and returns the byte count (0 = end of input); out() takes
// decoded bytes and returns false to abort (size cap). True on a clean end.
// Uses temporary heap (about 43 KB on ESP32), released before returning.
// ---------------------------------------------------------------------------
using InflateIn  = size_t (*)(void* ctx, uint8_t* buf, size_t cap);
using InflateOut = bool   (*)(void* ctx, const uint8_t* data, size_t n);
bool inflateRaw(InflateIn in, InflateOut out, void* ctx);

} // namespace plat
