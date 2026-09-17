/*
 * File:        src/core/calllog.h
 * Description: Caller log: a fixed ring of BBS_CALLLOG_SIZE records in one
 *              file on the logs partition (<logs>/calls.log). One write per
 *              logoff, reads one record at a time for LAST. It never grows,
 *              is not part of the backup zip, and survives uploadfs.
 *
 *   File layout: 8-byte header (magic "CLG1", next u16, count u16),
 *   then BBS_CALLLOG_SIZE fixed-size CallRec slots.
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc stdio)
 */
#pragma once
#include <cstdint>
#include "../config.h"

struct CallRec {
    char     user[BBS_USER_MAX + 1] = {};
    char     ip[16]  = {};
    uint8_t  node    = 0;      // 0 = sysop node, BBS_MAX_NODES + 1 = busy line
    uint8_t  term    = 0;      // TermType
    uint8_t  charset = 0;      // Charset
    uint8_t  flags   = 0;      // F_SYSOP
    uint32_t start   = 0;      // epoch of login, 0 if the clock was not set
    uint32_t secs    = 0;      // call length

    enum : uint8_t { F_SYSOP = 1 };
};

namespace calllog {

// append: write one record at the ring head
bool append(const CallRec& r);

// count: records stored (up to BBS_CALLLOG_SIZE)
uint8_t count();

// get: back = 0 is the newest record
bool get(uint8_t back, CallRec& out);

} // namespace calllog
