/*
 * File:        src/core/guard.h
 * Description: Abuse and usage tracking, all in fixed static tables.
 *
 *   BanList   wrong sysop passwords per IP. BBS_BAN_TRIES failures inside
 *             BBS_BAN_WINDOW_MS ban that IP for BBS_BAN_MS. RAM only, a
 *             reboot clears it.
 *   LoginGuard wrong account passwords per handle. BBS_LOCK_FAILS inside
 *             BBS_LOCK_WINDOW_MS lock that handle for BBS_LOCK_MS. RAM
 *             only: failures never write to flash.
 *
 *   IPs are the raw 4-byte s_addr value, same byte order as the socket.
 * Listing:     COMPLETE FILE
 * Libraries:   none
 */
#pragma once
#include <cstdint>
#include <cstddef>
#include "../config.h"

class BanList {
public:
    struct Entry {
        uint32_t ip        = 0;
        uint8_t  fails     = 0;
        uint32_t firstFail = 0;
        uint32_t until     = 0;     // 0 = not banned
    };

    // banned: true while an active ban covers ip
    bool banned(uint32_t ip, uint32_t now);

    // fail: count a wrong password. True if this failure started a ban.
    bool fail(uint32_t ip, uint32_t now);

    // clear: forget ip (correct password or UNBAN). False if not listed.
    bool clear(uint32_t ip);

    // at: entry i if it is an active ban (for BANS)
    bool at(uint8_t i, uint32_t now, Entry& out) const;

private:
    Entry slots_[BBS_BAN_SLOTS];
};

class LoginGuard {
public:
    // locked: true while the handle is locked out
    bool locked(const char* handle, uint32_t now);

    // fail: count a wrong password. True if this failure locked the handle.
    bool fail(const char* handle, uint32_t now);

    // clear: a correct password forgets earlier failures
    void clear(const char* handle);

private:
    struct Entry {
        char     handle[BBS_USER_MAX + 1] = {};
        uint8_t  fails     = 0;
        uint32_t firstFail = 0;
        uint32_t until     = 0;     // 0 = not locked
    };
    Entry slots_[BBS_LOCK_SLOTS];
};

// ip helpers shared by the shell (dotted quad <-> raw s_addr bytes)
void ipToText(uint32_t ip, char* out, size_t n);
bool ipFromText(const char* s, uint32_t& out);
