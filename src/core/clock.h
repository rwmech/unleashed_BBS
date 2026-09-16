/*
 * File:        src/core/clock.h
 * Description: Wall clock helpers. Time comes from NTP on the ESP32 and
 *              from the OS on the host; until the clock is set every
 *              formatter prints "--" so nothing shows 1970.
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc time)
 */
#pragma once
#include <cstdint>
#include <cstddef>

namespace clk {

// valid: true once the wall clock has been set
bool valid();

// epoch: seconds since 1970, 0 when not valid
uint32_t epoch();

// fmt: strftime of the current local time; "--" when not valid
size_t fmt(char* buf, size_t n, const char* strftimeFmt);

// fmtEpoch: strftime of a stored epoch; "--" when e is 0
size_t fmtEpoch(char* buf, size_t n, const char* strftimeFmt, uint32_t e);

// dayKey: local calendar day (YYYYDDD). Before NTP sync, a day number
// derived from uptime so daily limits still reset.
uint32_t dayKey(uint32_t millisNow);

} // namespace clk
