/*
 * File:        src/core/clock.cpp
 * Description: Wall clock helpers (see clock.h).
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc time)
 */
#include "clock.h"
#include <ctime>
#include <cstdio>

namespace {
constexpr time_t kValidAfter = 1700000000;   // Nov 2023: anything earlier is unset
}

namespace clk {

bool valid() {
    return time(nullptr) > kValidAfter;
}

uint32_t epoch() {
    time_t t = time(nullptr);
    return t > kValidAfter ? static_cast<uint32_t>(t) : 0;
}

size_t fmtEpoch(char* buf, size_t n, const char* strftimeFmt, uint32_t e) {
    if (!n) return 0;
    if (!e) return static_cast<size_t>(snprintf(buf, n, "--"));
    time_t t = static_cast<time_t>(e);
    struct tm lt;
    localtime_r(&t, &lt);
    size_t w = strftime(buf, n, strftimeFmt, &lt);
    if (!w) buf[0] = '\0';
    return w;
}

size_t fmt(char* buf, size_t n, const char* strftimeFmt) {
    return fmtEpoch(buf, n, strftimeFmt, epoch());
}

uint32_t dayKey(uint32_t millisNow) {
    uint32_t e = epoch();
    if (!e) return millisNow / 86400000u + 1u;
    time_t t = static_cast<time_t>(e);
    struct tm lt;
    localtime_r(&t, &lt);
    return static_cast<uint32_t>((lt.tm_year + 1900) * 1000 + lt.tm_yday);
}

} // namespace clk
