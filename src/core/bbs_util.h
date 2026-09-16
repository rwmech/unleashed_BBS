/*
 * File:        src/core/bbs_util.h
 * Description: Small helpers shared by bbs.cpp, bbs_shell.cpp and
 *              bbs_sysop.cpp. Internal to the core, not a plugin API.
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc)
 */
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdio>
#include "bbs.h"

namespace bbsu {

// ieq: case-insensitive string equality
inline bool ieq(const char* a, const char* b) {
    while (*a && *b) {
        if (toupper(static_cast<unsigned char>(*a)) != toupper(static_cast<unsigned char>(*b))) return false;
        ++a; ++b;
    }
    return *a == *b;
}

// nodeChar: '1'..'6' for callers, 'S' sysop node, 'B' busy line
inline char nodeChar(const Session& s) {
    if (s.role == Role::Sysop) return 'S';
    if (s.role == Role::Busy)  return 'B';
    return static_cast<char>('0' + s.id);
}

// fmtIdle: mm:ss, minutes capped at 99
inline void fmtIdle(char* out, size_t n, uint32_t ms) {
    uint32_t secs = ms / 1000u;
    uint32_t m    = secs / 60u;
    if (m > 99) { m = 99; secs = 99u * 60u + 59u; }
    snprintf(out, n, "%02u:%02u", static_cast<unsigned>(m), static_cast<unsigned>(secs % 60u));
}

// can: session holds a staff permission (Perm bit)
inline bool can(const Session& s, uint16_t bit) {
    return (s.perms & bit) != 0;
}

// outranks: a may act on b (KICK, SNOOP) only from a higher staff level
inline bool outranks(const Session& a, const Session& b) {
    return static_cast<uint8_t>(a.level) > static_cast<uint8_t>(b.level);
}

// isAbortKey: stops a listing, a screen or the FX demo
inline bool isAbortKey(int k) {
    return k == ' ' || k == KEY_ESC || k == KEY_BREAK;
}

constexpr const char kMoreText[]    = "[More] Y/n/c ";
constexpr uint8_t    kMoreLen       = sizeof(kMoreText) - 1;
constexpr const char kConfirmText[] = "Log off (Y/N)? ";

} // namespace bbsu
