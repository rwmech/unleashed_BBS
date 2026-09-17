/*
 * File:        src/core/guard.cpp
 * Description: Ban list and daily time bank (see guard.h).
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc)
 */
#include "guard.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>

namespace {

bool sameUser(const char* a, const char* b) {
    while (*a && *b) {
        if (toupper(static_cast<unsigned char>(*a)) != toupper(static_cast<unsigned char>(*b))) return false;
        ++a; ++b;
    }
    return *a == *b;
}

} // namespace

// ===========================================================================
// BanList
// ===========================================================================

bool BanList::banned(uint32_t ip, uint32_t now) {
    for (auto& e : slots_) {
        if (e.ip != ip || !e.until) continue;
        if (static_cast<int32_t>(now - e.until) < 0) return true;
        e = Entry();                              // ban expired
    }
    return false;
}

// ---------------------------------------------------------------------------
// fail: count inside the window; reuse the stalest slot when full
// ---------------------------------------------------------------------------
bool BanList::fail(uint32_t ip, uint32_t now) {
    Entry* slot   = nullptr;
    Entry* empty  = nullptr;
    Entry* oldest = nullptr;      // oldest non-banned entry, evicted when full
    for (auto& e : slots_) {
        if (e.ip == ip) { slot = &e; break; }
        if (!e.ip) { if (!empty) empty = &e; continue; }
        if (!e.until && (!oldest || e.firstFail < oldest->firstFail)) oldest = &e;
    }
    if (!slot) {
        slot = empty ? empty : (oldest ? oldest : &slots_[0]);
        *slot = Entry();
        slot->ip = ip;
    }
    if (slot->until) return false;                // already banned

    if (!slot->fails || now - slot->firstFail > BBS_BAN_WINDOW_MS) {
        slot->fails     = 0;
        slot->firstFail = now;
    }
    if (++slot->fails >= BBS_BAN_TRIES) {
        slot->until = now + BBS_BAN_MS;
        if (!slot->until) slot->until = 1;       // 0 means "not banned"
        return true;
    }
    return false;
}

bool BanList::clear(uint32_t ip) {
    for (auto& e : slots_) {
        if (e.ip == ip) { e = Entry(); return true; }
    }
    return false;
}

bool BanList::at(uint8_t i, uint32_t now, Entry& out) const {
    if (i >= BBS_BAN_SLOTS) return false;
    const Entry& e = slots_[i];
    if (!e.ip || !e.until || static_cast<int32_t>(now - e.until) >= 0) return false;
    out = e;
    return true;
}

// ===========================================================================
// LoginGuard
// ===========================================================================

bool LoginGuard::locked(const char* handle, uint32_t now) {
    for (auto& e : slots_) {
        if (!e.until || !sameUser(e.handle, handle)) continue;
        if (static_cast<int32_t>(now - e.until) < 0) return true;
        e = Entry();                              // lock expired
    }
    return false;
}

// ---------------------------------------------------------------------------
// fail: count inside the window; reuse an empty or the oldest slot
// ---------------------------------------------------------------------------
bool LoginGuard::fail(const char* handle, uint32_t now) {
    Entry* slot   = nullptr;
    Entry* spare  = nullptr;
    for (auto& e : slots_) {
        if (e.handle[0] && sameUser(e.handle, handle)) { slot = &e; break; }
        if (!e.handle[0]) { if (!spare) spare = &e; continue; }
        if (!e.until && (!spare || (spare->handle[0] && e.firstFail < spare->firstFail))) spare = &e;
    }
    if (!slot) {
        slot = spare ? spare : &slots_[0];
        *slot = Entry();
        strncpy(slot->handle, handle, BBS_USER_MAX);
    }
    if (slot->until) return false;
    if (!slot->fails || now - slot->firstFail > BBS_LOCK_WINDOW_MS) {
        slot->fails     = 0;
        slot->firstFail = now;
    }
    if (++slot->fails >= BBS_LOCK_FAILS) {
        slot->until = now + BBS_LOCK_MS;
        if (!slot->until) slot->until = 1;
        return true;
    }
    return false;
}

void LoginGuard::clear(const char* handle) {
    for (auto& e : slots_) {
        if (e.handle[0] && sameUser(e.handle, handle)) e = Entry();
    }
}

// ===========================================================================
// IP text helpers
// ===========================================================================

void ipToText(uint32_t ip, char* out, size_t n) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&ip);
    snprintf(out, n, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}

bool ipFromText(const char* s, uint32_t& out) {
    uint8_t b[4];
    for (int i = 0; i < 4; ++i) {
        if (!isdigit(static_cast<unsigned char>(*s))) return false;
        char* end = nullptr;
        long v = strtol(s, &end, 10);
        if (v < 0 || v > 255) return false;
        b[i] = static_cast<uint8_t>(v);
        s = end;
        if (i < 3) {
            if (*s != '.') return false;
            ++s;
        }
    }
    if (*s && *s != ' ') return false;
    memcpy(&out, b, 4);
    return true;
}
