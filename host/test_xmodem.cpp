/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         host/test_xmodem.cpp
 * Module:       Host test build / XMODEM and YMODEM engine tests
 *
 * Purpose:      Wires an xmodem::Engine sender to an xmodem::Engine
 *                  receiver through a pipe that can corrupt bytes, drop
 *                  control bytes and go silent, and compares the file at
 *                  the far end byte for byte.
 *
 *               The YMODEM checks are here to prove one thing above all
 *               others: the same file goes through XMODEM padded and
 *               through YMODEM byte exact. Both halves of that are
 *               asserted, because a test that only checks the YMODEM case
 *               cannot tell a working length field from a file that was
 *               never padded in the first place.
 *
 *               The clock is virtual: time only moves when the wire is
 *               idle, so a 300 KB transfer costs no wall time and a ten
 *               second protocol timeout costs four hundred loop passes.
 *               That is what makes the retry and timeout cases testable at
 *               all rather than "it looked right".
 *
 *               The transfers use files of 0xFF bytes on purpose. Telnet's
 *               IAC is 0xFF, and the escaping for it lives in the socket
 *               layer, not in the engine. An ASCII test file would sail
 *               past that whole class of bug and the first casualty would
 *               be somebody's .PRG.
 *
 * Usage:        make test_xmodem && ./test_xmodem
 *
 * Libraries:    libc, C++17
 * Targets:      Linux host test build
 * See also:     PLAN-FILES.md, src/core/xmodem.h
 *
 * Copyright 2026 - Robert Mech
 * License:      GNU General Public License v3 or later
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>. The full
 * text is in the LICENSE file at the top of this repository.
 * ===========================================================================
 */

#include "core/xmodem.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

using namespace xmodem;

// ===========================================================================
// Scoreboard
// ===========================================================================
static int g_checks = 0;
static int g_fails  = 0;

static bool check(const char* what, bool ok) {
    ++g_checks;
    if (!ok) ++g_fails;
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    return ok;
}

// ===========================================================================
// A file in memory, on both ends
// ===========================================================================
struct Src {
    const uint8_t* p;
    size_t         n;
    size_t         off;
    uint16_t       chunkCap;   // 0 = whatever is asked for (a short-read source)
    int            calls;
};

static uint16_t srcRead(void* ctx, uint8_t* dst, uint16_t max) {
    Src* s = static_cast<Src*>(ctx);
    ++s->calls;
    size_t left = s->n - s->off;
    size_t take = left < max ? left : max;
    if (s->chunkCap && take > s->chunkCap) take = s->chunkCap;
    memcpy(dst, s->p + s->off, take);
    s->off += take;
    return static_cast<uint16_t>(take);
}

struct Dst {
    uint8_t* p         = nullptr;
    size_t   cap       = 0;
    size_t   n         = 0;
    int      failAfter = -1;   // -1 never; otherwise refuse once this many are in
    int      blocks    = 0;
    // What block 0 said, and whether this owner will take it. Appended with
    // defaults, so the XMODEM fixtures that fill the first five members
    // still compile and still say nothing about YMODEM.
    char     name[96]  = {};
    uint32_t size      = 0;
    int      opens     = 0;
    size_t   nAtOpen   = 0;    // bytes written when open() was called: must be 0
    bool     refuse    = false;
};

static bool dstWrite(void* ctx, const uint8_t* src, uint16_t len) {
    Dst* d = static_cast<Dst*>(ctx);
    if (d->failAfter >= 0 && d->blocks >= d->failAfter) return false;
    if (d->n + len > d->cap) return false;
    memcpy(d->p + d->n, src, len);
    d->n += len;
    ++d->blocks;
    return true;
}

// dstOpen: the YMODEM owner being handed the name and the length out of
// block 0. It records when it was called as well as what with, because
// "before any data" is half the contract.
static bool dstOpen(void* ctx, const char* name, uint32_t size) {
    Dst* d = static_cast<Dst*>(ctx);
    ++d->opens;
    d->nAtOpen = d->n;
    snprintf(d->name, sizeof d->name, "%s", name);
    d->size = size;
    return !d->refuse;
}

// ===========================================================================
// The wire
// ===========================================================================
struct Pipe {
    static const size_t kCap = 65536;
    uint8_t buf[kCap];
    size_t  rd = 0, wr = 0;

    size_t used() const { return wr - rd; }
    void push(const uint8_t* b, size_t n) {
        if (rd == wr) { rd = wr = 0; }
        if (wr + n > kCap) { fprintf(stderr, "pipe overflow\n"); exit(2); }
        memcpy(buf + wr, b, n);
        wr += n;
    }
    size_t pop(uint8_t* b, size_t max) {
        size_t n = used();
        if (n > max) n = max;
        memcpy(b, buf + rd, n);
        rd += n;
        return n;
    }
};

// ---------------------------------------------------------------------------
// Tap: parses the sender's stream so a fault can be aimed at one block
// rather than at a byte offset that moves when the framing changes.
// ---------------------------------------------------------------------------
struct Tap {
    bool crc        = true;
    int  hitBlock   = -1;     // corrupt the Nth block seen (1-based)
    int  everyN     = 0;      // corrupt every Nth block seen
    bool hit1K      = false;  // corrupt every 1K block (forces the fallback)
    int  spoilAt    = 4;      // byte within the block to flip, -1 = the check byte
    int  maxHits    = 1;
    bool eatEot     = false;  // swallow the first EOT
    int  hits       = 0;
    int  eotsEaten  = 0;
    int  blocksSeen = 0;
    // parser
    bool     inPkt  = false;
    uint16_t len    = 0;
    uint16_t pos    = 0;
    bool     spoil  = false;

    size_t apply(uint8_t* b, size_t n) {
        size_t out = 0;
        for (size_t i = 0; i < n; ++i) {
            if (!inPkt) {
                if (b[i] == SOH || b[i] == STX) {
                    uint16_t d = (b[i] == STX) ? kBlk1K : kBlk128;
                    inPkt = true;
                    len   = static_cast<uint16_t>(3 + d + (crc ? 2 : 1));
                    pos   = 0;
                    ++blocksSeen;
                    spoil = (hits < maxHits) &&
                            ((hitBlock > 0 && blocksSeen == hitBlock) ||
                             (everyN > 0 && blocksSeen % everyN == 0) ||
                             (hit1K && d == kBlk1K));
                    if (spoil) ++hits;
                } else if (b[i] == EOT && eatEot && eotsEaten < 1) {
                    ++eotsEaten;          // the end of file never arrives
                    continue;
                }
            }
            if (inPkt) {
                const uint16_t at = (spoilAt < 0) ? static_cast<uint16_t>(len - 1)
                                                  : static_cast<uint16_t>(spoilAt);
                if (spoil && pos == at) b[i] = static_cast<uint8_t>(b[i] ^ 0x5A);
                if (++pos >= len) { inPkt = false; spoil = false; }
            }
            b[out++] = b[i];
        }
        return out;
    }
};

// ---------------------------------------------------------------------------
// Drop: loses the Nth ACK coming back, which is the lost-ACK case, or the
// Nth 'C'. Losing the second 'C' is the YMODEM case: the header was
// acknowledged but the start character that follows it never arrived, and
// the sender is left holding a block 0 the receiver already has.
// ---------------------------------------------------------------------------
struct Drop {
    int  ackNo   = -1;    // drop the Nth ACK (1-based)
    int  crcNo   = -1;    // drop the Nth 'C' (1-based)
    int  acks    = 0;
    int  crcs    = 0;
    int  dropped = 0;

    size_t apply(uint8_t* b, size_t n) {
        size_t out = 0;
        for (size_t i = 0; i < n; ++i) {
            if (b[i] == ACK    && ++acks == ackNo) { ++dropped; continue; }
            if (b[i] == CRCREQ && ++crcs == crcNo) { ++dropped; continue; }
            b[out++] = b[i];
        }
        return out;
    }
};

// ---------------------------------------------------------------------------
// Link: sender and receiver, back to back
// ---------------------------------------------------------------------------
struct Link {
    Engine   s, r;
    Pipe     sr, rs;
    Tap      tap;
    Drop     drop;
    uint32_t now   = 0;
    size_t   chunk = 64;
    long     iters = 0;

    // pump: run until both sides stop, or the loop budget is gone. Time
    // only moves on an idle pass, so a clean transfer takes no virtual time
    // and a stall reaches its timeout quickly.
    bool pump(long budget = 400000) {
        uint8_t b[4096];
        while (budget-- > 0) {
            ++iters;
            s.tick(now);
            r.tick(now);

            bool moved = false;
            size_t n = s.pull(b, chunk, now);
            if (n) { n = tap.apply(b, n); if (n) sr.push(b, n); moved = true; }
            n = r.pull(b, chunk, now);
            if (n) { n = drop.apply(b, n); if (n) rs.push(b, n); moved = true; }

            n = sr.pop(b, chunk);
            if (n) { r.feed(b, n, now); moved = true; }
            n = rs.pop(b, chunk);
            if (n) { s.feed(b, n, now); moved = true; }

            if (!s.running() && !r.running() && !s.pending() && !r.pending() &&
                sr.used() == 0 && rs.used() == 0) return true;
            if (!moved) now += 25;
        }
        return false;
    }
};

// ---------------------------------------------------------------------------
// transfer: one whole file through a fresh link, with the result checked
// ---------------------------------------------------------------------------
struct Outcome {
    bool     finished  = false;
    bool     ok        = false;   // both ends Done and the bytes match
    bool     exact     = false;   // and not one byte more than the file
    size_t   got       = 0;
    uint32_t pad       = 0;
    uint16_t sErrs     = 0, rErrs = 0;
    uint16_t sBlkLen   = 0;
    uint32_t sBlocks   = 0, rBlocks = 0;
    uint32_t rBytes    = 0;
    Engine::Err sErr = Engine::Err::None, rErr = Engine::Err::None;
    // what the receiver learned from block 0
    bool     rYm       = false;
    int      opens     = 0;
    size_t   nAtOpen   = 0;
    uint32_t hdrSize   = 0;
    char     hdrName[96] = {};
};

// transfer: yname turns the whole thing into YMODEM. Passing it is the only
// difference between the two protocols from a caller's point of view, which
// is the point: the same fixture drives both and the results are compared.
static Outcome transfer(const uint8_t* file, size_t n, bool crc, bool allow1k,
                        size_t chunk = 64, Tap tap = Tap(), Drop drop = Drop(),
                        int failWriteAfter = -1, uint16_t srcChunkCap = 0,
                        const char* yname = nullptr, bool refuseOpen = false) {
    Link*   L   = new Link();
    uint8_t* out = static_cast<uint8_t*>(malloc(n + kBlk1K + 16));
    Src src { file, n, 0, srcChunkCap, 0 };
    Dst dst { out, n + kBlk1K + 16, 0, failWriteAfter, 0 };
    dst.refuse = refuseOpen;

    L->chunk  = chunk;
    L->tap    = tap;
    L->tap.crc = yname ? true : crc;    // YMODEM is CRC-16 only
    L->drop   = drop;
    if (yname) {
        L->s.beginSendY(srcRead, &src, yname, static_cast<uint32_t>(n), L->now, allow1k);
        L->r.beginRecvY(dstOpen, dstWrite, &dst, L->now);
    } else {
        L->s.beginSend(srcRead, &src, L->now, allow1k);
        L->r.beginRecv(dstWrite, &dst, L->now, crc);
    }

    Outcome o;
    o.finished = L->pump();
    o.got      = dst.n;
    o.pad      = L->r.trailingPad();
    o.sErrs    = L->s.errors();
    o.rErrs    = L->r.errors();
    o.sBlkLen  = L->s.blockSize();
    o.sBlocks  = L->s.blocks();
    o.rBlocks  = L->r.blocks();
    o.rBytes   = L->r.bytes();
    o.sErr     = L->s.error();
    o.rErr     = L->r.error();
    o.rYm      = L->r.ymodem();
    o.opens    = dst.opens;
    o.nAtOpen  = dst.nAtOpen;
    o.hdrSize  = dst.size;
    snprintf(o.hdrName, sizeof o.hdrName, "%s", dst.name);
    o.ok       = o.finished && L->s.done() && L->r.done() &&
                 dst.n >= n && memcmp(out, file, n) == 0;
    // Everything past the file must be pad and nothing else.
    for (size_t i = n; i < dst.n && o.ok; ++i) if (out[i] != SUB) o.ok = false;
    o.exact    = o.ok && dst.n == n;

    free(out);
    delete L;
    return o;
}

// ---------------------------------------------------------------------------
// fill: deterministic test data. An LCG, so a failure is reproducible.
// ---------------------------------------------------------------------------
static void fill(uint8_t* p, size_t n, uint32_t seed) {
    uint32_t x = seed ? seed : 1;
    for (size_t i = 0; i < n; ++i) {
        x = x * 1103515245u + 12345u;
        p[i] = static_cast<uint8_t>(x >> 16);
    }
}

// ===========================================================================
// The checks
// ===========================================================================

// ---------------------------------------------------------------------------
// crc16 the slow way, to prove the nibble table
// ---------------------------------------------------------------------------
static uint16_t crc16Bitwise(const uint8_t* d, uint16_t n) {
    uint16_t crc = 0;
    for (uint16_t i = 0; i < n; ++i) {
        crc ^= static_cast<uint16_t>(d[i] << 8);
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

static bool test_checks() {
    printf("Checksums\n");
    const uint8_t v[] = "123456789";
    bool ok = check("CRC-16/XMODEM of \"123456789\" is 0x31C3", crc16(v, 9) == 0x31C3);

    uint8_t buf[1024];
    fill(buf, sizeof buf, 7);
    bool same = true;
    for (uint16_t n = 0; n <= 1024 && same; n += 7)
        same = (crc16(buf, n) == crc16Bitwise(buf, n));
    ok &= check("the nibble table agrees with the bit loop at every length", same);

    uint8_t zeros[128] = {};
    ok &= check("a block of zeros checksums to zero both ways",
                crc16(zeros, 128) == 0 && sum8(zeros, 128) == 0);

    uint8_t ff[128];
    memset(ff, 0xFF, sizeof ff);
    ok &= check("128 bytes of 0xFF sum to 0x80", sum8(ff, 128) == 0x80);
    return ok;
}

// ---------------------------------------------------------------------------
// The sizes that break naive implementations
// ---------------------------------------------------------------------------
static bool test_sizes() {
    printf("File sizes\n");
    static uint8_t buf[4096];
    fill(buf, sizeof buf, 11);
    bool ok = true;

    Outcome e = transfer(buf, 0, true, true);
    ok &= check("an empty file transfers", e.ok && e.got == 0 && e.sBlocks == 0);

    struct { size_t n; const char* what; } cases[] = {
        { 1,    "one byte"                 },
        { 127,  "one byte short of a block" },
        { 128,  "exactly one 128 block"    },
        { 129,  "one block plus one byte"  },
        { 1023, "one byte short of 1K"     },
        { 1024, "exactly one 1K block"     },
        { 1025, "1K plus one byte"         },
        { 4096, "four 1K blocks"           },
    };
    for (auto& c : cases) {
        Outcome o = transfer(buf, c.n, true, true);
        char msg[96];
        snprintf(msg, sizeof msg, "%s (%u bytes) arrives intact",
                 c.what, static_cast<unsigned>(c.n));
        ok &= check(msg, o.ok);
    }

    // The padding is real and it is the caller's to deal with.
    Outcome p = transfer(buf, 100, true, true);
    ok &= check("a 100 byte file arrives as 128 padded bytes",
                p.ok && p.got == 128);
    ok &= check("and the engine says how much of it is padding", p.pad == 28);

    Outcome q = transfer(buf, 1024, true, true);
    ok &= check("a file that fills its block exactly reports no padding",
                q.ok && q.got == 1024 && q.pad == 0);
    ok &= check("and a clean transfer counts no errors at either end",
                q.sErrs == 0 && q.rErrs == 0);

    // A file that really ends in 0x1A cannot be told from padding. Nobody
    // can fix that; the point is that the engine does not pretend it can.
    static uint8_t sub[200];
    fill(sub, sizeof sub, 13);
    sub[199] = SUB;
    sub[198] = SUB;
    Outcome s = transfer(sub, 200, true, true);
    ok &= check("trailing 0x1A in the file is counted as padding, as documented",
                s.ok && s.pad == 58);
    return ok;
}

// ---------------------------------------------------------------------------
// The 0xFF case. No IAC escaping happens in this layer, and this is the
// fixture that will catch it if somebody adds it in the wrong place.
// ---------------------------------------------------------------------------
static bool test_binary() {
    printf("Binary transparency\n");
    static uint8_t ff[5000];
    memset(ff, 0xFF, sizeof ff);
    bool ok = true;

    Outcome a = transfer(ff, sizeof ff, true, true);
    ok &= check("5000 bytes of 0xFF transfer byte for byte (CRC, 1K)", a.ok);

    Outcome b = transfer(ff, sizeof ff, false, true);
    ok &= check("and again with the 8-bit checksum", b.ok);

    Outcome c = transfer(ff, sizeof ff, true, false);
    ok &= check("and again in 128 byte blocks", c.ok && c.sBlkLen == kBlk128);

    // Every byte value, including the control bytes the protocol uses.
    static uint8_t every[256 * 8];
    for (size_t i = 0; i < sizeof every; ++i) every[i] = static_cast<uint8_t>(i);
    Outcome d = transfer(every, sizeof every, true, true);
    ok &= check("a file containing SOH, EOT, ACK, NAK, CAN and SUB as data is fine",
                d.ok);
    return ok;
}

// ---------------------------------------------------------------------------
// Block numbers wrap at 255 and the wrap is where a transfer normally dies
// ---------------------------------------------------------------------------
static bool test_wrap() {
    printf("Block number wrap\n");
    const size_t n1k = 300u * 1024u;            // 300 1K blocks
    uint8_t* big = static_cast<uint8_t*>(malloc(n1k));
    fill(big, n1k, 17);
    bool ok = true;

    Outcome a = transfer(big, n1k, true, true, 512);
    ok &= check("300 KB in 1K blocks (the number wraps once) arrives intact", a.ok);
    ok &= check("and it really was 300 blocks", a.rBlocks == 300);

    const size_t n128 = 40u * 1024u;            // 320 128-byte blocks
    Outcome b = transfer(big, n128, true, false, 512);
    ok &= check("40 KB in 128 byte blocks (320 of them) arrives intact", b.ok);
    ok &= check("and it really was 320 blocks", b.rBlocks == 320);

    Outcome c = transfer(big, n128, false, false, 512);
    ok &= check("and the same again on the 8-bit checksum", c.ok);
    free(big);
    return ok;
}

// ---------------------------------------------------------------------------
// Damage
// ---------------------------------------------------------------------------
static bool test_errors() {
    printf("Errors on the line\n");
    static uint8_t buf[8192];
    fill(buf, sizeof buf, 19);
    bool ok = true;

    Tap t;
    t.hitBlock = 3;
    Outcome a = transfer(buf, sizeof buf, true, true, 64, t);
    ok &= check("a corrupted block is NAKed and the retransmission accepted", a.ok);
    ok &= check("and the receiver counted the error", a.rErrs >= 1);
    ok &= check("and the sender resent the block", a.sErrs >= 1);

    Tap t2;
    t2.hitBlock = 1;
    Outcome b = transfer(buf, 4096, true, true, 64, t2);
    ok &= check("a corrupted first block recovers too", b.ok);

    Tap t3;
    t3.hitBlock = 3;
    Outcome c = transfer(buf, sizeof buf, false, true, 64, t3);
    ok &= check("and on the 8-bit checksum as well", c.ok);

    // Two damaged blocks in a row, then clean.
    Tap t4;
    t4.hitBlock = 2;
    t4.maxHits  = 1;
    Outcome d = transfer(buf, sizeof buf, true, false, 16, t4);
    ok &= check("damage in 128 byte mode with a 16 byte wire recovers", d.ok);
    return ok;
}

// ---------------------------------------------------------------------------
// The lost ACK. The receiver must acknowledge the repeat and throw it away,
// not write the block twice.
// ---------------------------------------------------------------------------
static bool test_lost_ack() {
    printf("A lost ACK\n");
    static uint8_t buf[4096];
    fill(buf, sizeof buf, 23);
    bool ok = true;

    Drop d;
    d.ackNo = 2;
    Outcome a = transfer(buf, sizeof buf, true, true, 64, Tap(), d);
    ok &= check("the sender times out and repeats the block", a.ok && a.sErrs >= 1);
    ok &= check("the receiver stored four blocks, not five", a.rBlocks == 4);
    ok &= check("and the file is exactly its own length", a.got == 4096);

    Drop d2;
    d2.ackNo = 1;
    Outcome b = transfer(buf, 1024, true, true, 64, Tap(), d2);
    ok &= check("a lost ACK on the very first block recovers",
                b.ok && b.rBlocks == 1 && b.got == 1024);
    return ok;
}

// ---------------------------------------------------------------------------
// 1K down to 128. The receiver in this test cannot keep a 1K block intact,
// which is the real-world case: a long block on a dirty line.
// ---------------------------------------------------------------------------
static bool test_fallback() {
    printf("1K falling back to 128\n");
    static uint8_t buf[4096];
    fill(buf, sizeof buf, 29);

    Tap t;
    t.hit1K   = true;
    t.maxHits = 100;
    Outcome a = transfer(buf, sizeof buf, true, true, 64, t);
    bool ok = check("a 1K block that keeps failing drops to 128 and the file lands",
                    a.ok);
    ok &= check("and the sender finished in 128 byte blocks", a.sBlkLen == kBlk128);
    ok &= check("without running out of retries", a.sErr == Engine::Err::None);
    return ok;
}

// ---------------------------------------------------------------------------
// A dirty line, for as long as a real call. This is the run that finds a
// protocol that recovers once and then quietly slips a block out of step.
// ---------------------------------------------------------------------------
static bool test_soak() {
    printf("A dirty line, 64 KB at a time\n");
    const size_t n   = 64u * 1024u;
    uint8_t*     buf = static_cast<uint8_t*>(malloc(n));
    fill(buf, n, 67);
    bool ok = true;

    struct { int every; int at; bool crc; bool k1; const char* what; } runs[] = {
        { 2, 4,  true,  true,  "damage in the data of every other 1K block" },
        { 3, -1, true,  true,  "damage in the CRC of every third block"     },
        { 2, 4,  false, false, "damage every other 128 block, checksum"     },
        { 5, 7,  true,  false, "damage every fifth 128 block, CRC"          },
    };
    for (auto& r : runs) {
        Tap t;
        t.everyN  = r.every;
        t.spoilAt = r.at;
        t.maxHits = 100000;
        Outcome o = transfer(buf, n, r.crc, r.k1, 128, t);
        char msg[128];
        snprintf(msg, sizeof msg, "64 KB survives %s", r.what);
        ok &= check(msg, o.ok);
    }
    free(buf);
    return ok;
}

// ---------------------------------------------------------------------------
// The EOT is one byte with no check on it, so losing it is normal
// ---------------------------------------------------------------------------
static bool test_lost_eot() {
    printf("A lost EOT\n");
    static uint8_t buf[2048];
    fill(buf, sizeof buf, 71);

    Tap t;
    t.eatEot  = true;
    Outcome o = transfer(buf, sizeof buf, true, true, 64, t);
    bool ok = check("an EOT that never arrives is asked for again", o.ok);
    ok &= check("and the file still ends where it should", o.got == 2048);
    ok &= check("having cost one retry", o.sErrs >= 1);
    return ok;
}

// ===========================================================================
// Hand-built streams: cases a loopback cannot produce
// ===========================================================================

// mkPkt: one block on the wire, exactly as a sender would frame it
static size_t mkPkt(uint8_t* out, uint8_t num, const uint8_t* data, uint16_t len,
                    bool crc) {
    size_t i = 0;
    out[i++] = (len == kBlk1K) ? STX : SOH;
    out[i++] = num;
    out[i++] = static_cast<uint8_t>(0xFF - num);
    memcpy(out + i, data, len);
    i += len;
    if (crc) {
        uint16_t c = crc16(data, len);
        out[i++] = static_cast<uint8_t>(c >> 8);
        out[i++] = static_cast<uint8_t>(c & 0xFF);
    } else {
        out[i++] = sum8(data, len);
    }
    return i;
}

// mkHdr: a YMODEM block 0. A null name gives the empty header, which is how
// a sender says there is nothing more coming.
static size_t mkHdr(uint8_t* out, const char* name, const char* size, bool crc) {
    uint8_t pay[kBlk128];
    memset(pay, 0, sizeof pay);
    size_t i = 0;
    if (name) {
        while (*name && i < sizeof pay - 1) pay[i++] = static_cast<uint8_t>(*name++);
        pay[i++] = 0;
        if (size) while (*size && i < sizeof pay) pay[i++] = static_cast<uint8_t>(*size++);
    }
    return mkPkt(out, 0, pay, kBlk128, crc);
}

// settle: run a lone engine forward until it stops producing, collecting
// whatever it sends
struct Lone {
    Engine   e;
    uint8_t  out[4096];
    size_t   outN = 0;
    uint32_t now  = 0;

    void collect(int passes = 4) {
        uint8_t b[256];
        for (int i = 0; i < passes; ++i) {
            e.tick(now);
            size_t n = e.pull(b, sizeof b, now);
            if (n && outN + n <= sizeof out) { memcpy(out + outN, b, n); outN += n; }
        }
    }
    void give(const uint8_t* b, size_t n) { e.feed(b, n, now); }
    int  count(uint8_t v) const {
        int c = 0;
        for (size_t i = 0; i < outN; ++i) if (out[i] == v) ++c;
        return c;
    }
};

static bool test_duplicate() {
    printf("A duplicated block\n");
    static uint8_t data[128];
    fill(data, sizeof data, 31);
    static uint8_t got[1024];

    Lone L;
    Dst  d { got, sizeof got, 0, -1, 0 };
    L.e.beginRecv(dstWrite, &d, L.now, true);
    L.collect();
    bool ok = check("the receiver opens with 'C'", L.outN >= 1 && L.out[0] == CRCREQ);

    uint8_t pkt[1100];
    size_t  n = mkPkt(pkt, 1, data, 128, true);
    L.outN = 0;
    L.give(pkt, n);
    L.collect();
    ok &= check("block 1 is acknowledged", L.count(ACK) == 1);

    // The same block again: the sender never saw the ACK.
    L.outN = 0;
    L.give(pkt, n);
    L.collect();
    ok &= check("the repeat is acknowledged again", L.count(ACK) == 1);
    ok &= check("but it is not stored twice", d.blocks == 1 && d.n == 128);

    // Block 2 still fits where it should.
    static uint8_t data2[128];
    fill(data2, sizeof data2, 37);
    n = mkPkt(pkt, 2, data2, 128, true);
    L.outN = 0;
    L.give(pkt, n);
    L.collect();
    ok &= check("and the next block still lands in the right place",
                d.blocks == 2 && d.n == 256 && memcmp(got + 128, data2, 128) == 0);

    const uint8_t eot = EOT;
    L.outN = 0;
    L.give(&eot, 1);
    L.collect();
    ok &= check("the first EOT is questioned, not believed",
                !L.e.done() && L.count(NAK) == 1);
    L.outN = 0;
    L.give(&eot, 1);
    L.collect();
    ok &= check("the second EOT ends it", L.e.done() && L.count(ACK) == 1);
    return ok;
}

// ---------------------------------------------------------------------------
// The EOT is a single byte with nothing protecting it
// ---------------------------------------------------------------------------
static bool test_eot() {
    printf("The EOT handshake\n");
    static uint8_t d1[128], d2[128], got[1024];
    fill(d1, sizeof d1, 73);
    fill(d2, sizeof d2, 79);
    uint8_t       pkt[1100];
    const uint8_t eot = EOT;
    bool          ok  = true;

    // An EOT that is really a line hit between two blocks.
    {
        Lone L;
        Dst  d { got, sizeof got, 0, -1, 0 };
        L.e.beginRecv(dstWrite, &d, L.now, true);
        L.collect();
        L.give(pkt, mkPkt(pkt, 1, d1, 128, true));
        L.collect();
        L.give(&eot, 1);                        // the hit
        L.collect();
        L.give(pkt, mkPkt(pkt, 2, d2, 128, true));
        L.collect();
        ok &= check("a stray EOT between blocks does not truncate the file",
                    L.e.running() && d.blocks == 2);
        L.give(&eot, 1);
        L.collect();
        L.give(&eot, 1);
        L.collect();
        ok &= check("and the real end of the file still works afterwards",
                    L.e.done() && d.n == 256 && memcmp(got + 128, d2, 128) == 0);
    }

    // A sender that does not resend an EOT it has already sent must not
    // cost the caller a file that arrived in full.
    {
        Lone L;
        Dst  d { got, sizeof got, 0, -1, 0 };
        L.e.beginRecv(dstWrite, &d, L.now, true);
        L.collect();
        L.give(pkt, mkPkt(pkt, 1, d1, 128, true));
        L.collect();
        L.outN = 0;
        L.give(&eot, 1);
        L.collect();                            // the NAK goes out, nothing returns
        int guard = 0;
        while (L.e.running() && guard++ < 2000) { L.collect(1); L.now += 25; }
        ok &= check("a sender that will not resend its EOT is taken at its word",
                    L.e.done() && d.blocks == 1 && d.n == 128);
    }
    return ok;
}

static bool test_sequence() {
    printf("A block out of sequence\n");
    static uint8_t data[128];
    fill(data, sizeof data, 41);
    static uint8_t got[1024];

    Lone L;
    Dst  d { got, sizeof got, 0, -1, 0 };
    L.e.beginRecv(dstWrite, &d, L.now, true);
    L.collect();

    uint8_t pkt[1100];
    L.give(pkt, mkPkt(pkt, 1, data, 128, true));
    L.outN = 0;
    L.collect();
    // Block 3 when block 2 is due is not a retry, it is a lost block: the
    // file would have a hole in it, so the transfer has to stop.
    L.give(pkt, mkPkt(pkt, 3, data, 128, true));
    L.outN = 0;
    L.collect();
    bool ok = check("a skipped block number aborts the transfer",
                    L.e.failed() && L.e.error() == Engine::Err::Sequence);
    ok &= check("and the other end is told with CANs", L.count(CAN) >= 2);
    ok &= check("and nothing extra was stored", d.blocks == 1);
    return ok;
}

static bool test_bad_header() {
    printf("A damaged block number\n");
    static uint8_t data[128];
    fill(data, sizeof data, 43);
    static uint8_t got[1024];

    Lone L;
    Dst  d { got, sizeof got, 0, -1, 0 };
    L.e.beginRecv(dstWrite, &d, L.now, true);
    L.collect();

    uint8_t pkt[1100];
    size_t  n = mkPkt(pkt, 1, data, 128, true);
    pkt[2] ^= 0xFF;                    // the complement no longer agrees
    L.outN = 0;
    L.give(pkt, n);
    L.now += 1000;                     // the line goes quiet, the purge ends
    L.collect(8);
    bool ok = check("a block number that does not match its complement is NAKed",
                    L.count(NAK) >= 1 && d.blocks == 0);

    // The sender tries again, correctly this time.
    n = mkPkt(pkt, 1, data, 128, true);
    L.outN = 0;
    L.give(pkt, n);
    L.collect();
    ok &= check("and the clean retransmission is accepted",
                L.count(ACK) == 1 && d.blocks == 1);
    return ok;
}

static bool test_noise() {
    printf("Line noise before the first block\n");
    static uint8_t data[128];
    fill(data, sizeof data, 47);
    static uint8_t got[1024];

    Lone L;
    Dst  d { got, sizeof got, 0, -1, 0 };
    L.e.beginRecv(dstWrite, &d, L.now, true);
    L.collect();

    const uint8_t junk[] = { 0x0D, 0x0A, 'O', 'K', 0x0D, 0x0A, 0x7F, 0x00 };
    L.give(junk, sizeof junk);
    uint8_t pkt[1100];
    L.outN = 0;
    L.give(pkt, mkPkt(pkt, 1, data, 128, true));
    L.collect();
    bool ok = check("rubbish before the first block is ignored",
                    L.count(ACK) == 1 && d.blocks == 1 &&
                    memcmp(got, data, 128) == 0);
    return ok;
}

// ---------------------------------------------------------------------------
// Aborts, from each end
// ---------------------------------------------------------------------------
static bool test_cancel() {
    printf("CAN CAN\n");
    static uint8_t data[1024];
    fill(data, sizeof data, 53);
    bool ok = true;

    // The receiver gives up on a sender that is mid-file.
    {
        Lone L;
        Src  s { data, sizeof data, 0, 0, 0 };
        L.e.beginSend(srcRead, &s, L.now, true);
        const uint8_t start = CRCREQ;
        L.give(&start, 1);
        L.collect();
        const uint8_t cans[2] = { CAN, CAN };
        L.give(cans, 2);
        ok &= check("two CANs stop a sender",
                    L.e.failed() && L.e.error() == Engine::Err::RemoteCancel);
    }

    // And the other way about.
    {
        Lone L;
        static uint8_t got[1024];
        Dst d { got, sizeof got, 0, -1, 0 };
        L.e.beginRecv(dstWrite, &d, L.now, true);
        L.collect();
        const uint8_t cans[2] = { CAN, CAN };
        L.give(cans, 2);
        ok &= check("two CANs stop a receiver",
                    L.e.failed() && L.e.error() == Engine::Err::RemoteCancel);
    }

    // One CAN is a data byte's worth of bad luck, not an abort.
    {
        Lone L;
        static uint8_t got[1024];
        Dst d { got, sizeof got, 0, -1, 0 };
        L.e.beginRecv(dstWrite, &d, L.now, true);
        L.collect();
        const uint8_t one[3] = { CAN, 'x', CAN };
        L.give(one, 3);
        ok &= check("a single CAN, twice, with something between, does not",
                    L.e.running());
    }

    // Our own cancel has to reach the other end.
    {
        Lone L;
        Src  s { data, sizeof data, 0, 0, 0 };
        L.e.beginSend(srcRead, &s, L.now, true);
        const uint8_t start = CRCREQ;
        L.give(&start, 1);
        L.collect();
        L.outN = 0;
        L.e.cancel();
        L.collect();
        ok &= check("cancel() sends the CAN sequence and stops",
                    L.e.failed() && L.e.error() == Engine::Err::LocalCancel &&
                    L.count(CAN) >= 2);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Silence, from each end. A transfer that hangs holds a node.
// ---------------------------------------------------------------------------
static bool test_silence() {
    printf("Nobody at the other end\n");
    bool ok = true;

    // A receiver whose sender never answers: 'C' for a long time, then NAK
    // for a while in case the far end is checksum-only, then give up rather
    // than hold the line for ever.
    //
    // The numbers are deliberately patient. They used to be three 'C's and
    // thirty seconds, which is fine against a program and far too short
    // against somebody choosing a file in a terminal dialog: the board
    // switched to asking with NAK after nine seconds while the terminal had
    // already committed to CRC, and every block of every upload was then
    // checked the wrong way and refused.
    {
        Lone L;
        static uint8_t got[1024];
        Dst d { got, sizeof got, 0, -1, 0 };
        L.e.beginRecv(dstWrite, &d, L.now, true);
        int guard = 0;
        while (L.e.running() && guard++ < 40000) { L.collect(1); L.now += 25; }
        ok &= check("a receiver times out when the sender never starts",
                    L.e.failed() && L.e.error() == Engine::Err::NoStart);
        ok &= check("having asked for CRC first", L.out[0] == CRCREQ);
        ok &= check("then fallen back to the checksum NAK", L.count(NAK) >= 1);
        // A full minute of CRC is the requirement: that is how long a person
        // takes to find a file. Falling back before then is the bug.
        // One poll every kStartPollMs, so counting the CRC polls is the
        // same statement as "it stayed on CRC for this long".
        ok &= check("and it stayed on CRC for well over a minute",
                    L.count(CRCREQ) >= 25);
        ok &= check("and gave up eventually, not for ever",
                    L.now >= 150000 && L.now <= 220000);
    }

    // A sender whose receiver never starts.
    {
        Lone L;
        static uint8_t data[256];
        Src  s { data, sizeof data, 0, 0, 0 };
        L.e.beginSend(srcRead, &s, L.now, true);
        int guard = 0;
        while (L.e.running() && guard++ < 8000) { L.collect(1); L.now += 25; }
        ok &= check("a sender times out when the receiver never starts",
                    L.e.failed() && L.e.error() == Engine::Err::NoStart);
        ok &= check("and it sent nothing at all while it waited", L.outN == 0);
    }

    // A sender that starts and is then abandoned: ten retries of one block.
    {
        Lone L;
        static uint8_t data[4096];
        fill(data, sizeof data, 59);
        Src  s { data, sizeof data, 0, 0, 0 };
        L.e.beginSend(srcRead, &s, L.now, true);
        const uint8_t start = CRCREQ;
        L.give(&start, 1);
        int guard = 0;
        while (L.e.running() && guard++ < 20000) { L.collect(1); L.now += 25; }
        ok &= check("a sender gives up after its retries when the ACKs stop",
                    L.e.failed() && L.e.error() == Engine::Err::TooManyErrors);
        ok &= check("and it told the other end", L.count(CAN) >= 2);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// The owner's side of the contract
// ---------------------------------------------------------------------------
static bool test_owner() {
    printf("The owner's contract\n");
    static uint8_t buf[2048];
    fill(buf, sizeof buf, 61);
    bool ok = true;

    // A socket that can only take one byte at a time still works.
    Outcome a = transfer(buf, 512, true, true, 1);
    ok &= check("a wire that moves one byte at a time still completes", a.ok);

    // A source that only ever returns a few bytes a call must not insert
    // padding in the middle of the file.
    Outcome b = transfer(buf, sizeof buf, true, true, 64, Tap(), Drop(), -1, 100);
    ok &= check("a source that short-reads does not pad the middle of the file",
                b.ok && b.got == 2048);

    // A sink that refuses has to stop the transfer, not lose the file
    // quietly. 2048 bytes is two 1K blocks, and the second one is refused.
    Outcome c = transfer(buf, sizeof buf, true, true, 64, Tap(), Drop(), 1);
    ok &= check("a write that fails cancels the transfer",
                !c.ok && c.rErr == Engine::Err::SinkFailed);
    ok &= check("and the sender is told, rather than left sending",
                c.sErr == Engine::Err::RemoteCancel);

    // feed() stops at the end of the transfer: what follows is the caller
    // typing again, and it belongs to the terminal, not to us.
    {
        Lone L;
        static uint8_t data[64];
        Src  s { data, sizeof data, 0, 0, 0 };
        L.e.beginSend(srcRead, &s, L.now, true);
        const uint8_t start = CRCREQ;
        L.give(&start, 1);
        L.collect();                       // block 1 goes out
        const uint8_t ack = ACK;
        L.give(&ack, 1);
        L.collect();                       // EOT goes out
        const uint8_t tail[6] = { ACK, 'w', 'h', 'o', '\r', 'x' };
        size_t used = L.e.feed(tail, sizeof tail, L.now);
        ok &= check("feed() consumes the final ACK and stops there",
                    L.e.done() && used == 1);
    }

    // A transfer can be run again on the same engine.
    {
        Lone L;
        static uint8_t got[1024];
        Dst d { got, sizeof got, 0, -1, 0 };
        L.e.beginRecv(dstWrite, &d, L.now, true);
        L.collect();
        L.e.cancel();
        L.collect();
        L.e.reset();
        ok &= check("reset() puts an engine back to idle",
                    L.e.status() == Engine::Stat::Idle &&
                    L.e.error() == Engine::Err::None && !L.e.pending());
    }
    return ok;
}

// ===========================================================================
// YMODEM
// ===========================================================================

// ---------------------------------------------------------------------------
// The whole point of the exercise: the same file, both ways.
//
// 2000 bytes is not a multiple of 128 and not a multiple of 1024, so XMODEM
// has to pad it and YMODEM has to trim it. Asserting both halves is what
// makes this test worth anything: a YMODEM check on its own cannot tell a
// working length field from a file that was never padded.
// ---------------------------------------------------------------------------
static bool test_ymodem_exact() {
    printf("YMODEM: the length in block 0\n");
    static uint8_t buf[4096];
    fill(buf, sizeof buf, 83);
    bool ok = true;

    Outcome y = transfer(buf, 2000, true, true, 64, Tap(), Drop(), -1, 0, "GAME.PRG");
    ok &= check("a 2000 byte file arrives byte exact under YMODEM",
                y.ok && y.exact && y.got == 2000);

    Outcome x = transfer(buf, 2000, true, true);
    ok &= check("and the same file under plain XMODEM arrives padded",
                x.ok && !x.exact && x.got == 2048 && x.pad == 48);

    ok &= check("the YMODEM receiver was given the name",
                strcmp(y.hdrName, "GAME.PRG") == 0);
    ok &= check("and reports it back", y.rYm);
    ok &= check("and was given the exact length", y.hdrSize == 2000);
    ok &= check("open() was called once, before a single byte was written",
                y.opens == 1 && y.nAtOpen == 0);
    ok &= check("the padding it cut off is reported rather than guessed",
                y.pad == 48);
    ok &= check("and bytes() ends equal to the file's own length",
                y.rBytes == 2000);
    ok &= check("nothing went wrong on a clean line",
                y.sErrs == 0 && y.rErrs == 0);

    // A file that really ends in 0x1A. XMODEM cannot tell that from padding
    // and says so; YMODEM has a length and keeps every byte.
    static uint8_t sub[300];
    fill(sub, sizeof sub, 89);
    sub[299] = SUB;
    sub[298] = SUB;
    sub[297] = SUB;
    Outcome z = transfer(sub, 300, true, true, 64, Tap(), Drop(), -1, 0, "SUB.PRG");
    ok &= check("a file that really ends in 0x1A keeps those bytes under YMODEM",
                z.ok && z.exact && z.got == 300 &&
                z.pad == 84 && memcmp(buf, buf, 1) == 0);
    return ok;
}

// ---------------------------------------------------------------------------
// The sizes, again, because the trim happens on the last block and the last
// block is where every off-by-one lives
// ---------------------------------------------------------------------------
static bool test_ymodem_sizes() {
    printf("YMODEM file sizes\n");
    static uint8_t buf[5000];
    fill(buf, sizeof buf, 97);
    bool ok = true;

    struct { size_t n; const char* what; } cases[] = {
        { 0,    "an empty file"             },
        { 1,    "one byte"                  },
        { 127,  "one byte short of a block" },
        { 128,  "exactly one 128 block"     },
        { 129,  "one block plus one byte"   },
        { 1023, "one byte short of 1K"      },
        { 1024, "exactly one 1K block"      },
        { 1025, "1K plus one byte"          },
        { 4999, "an awkward 4999"           },
    };
    for (auto& c : cases) {
        Outcome o = transfer(buf, c.n, true, true, 64, Tap(), Drop(), -1, 0, "F.BIN");
        char msg[112];
        snprintf(msg, sizeof msg, "%s (%u bytes) arrives at exactly %u bytes",
                 c.what, static_cast<unsigned>(c.n), static_cast<unsigned>(c.n));
        ok &= check(msg, o.ok && o.exact && o.got == c.n);
    }

    // An empty file still has to open: the sysop asked for a file and a
    // zero byte one is a file, not a failure.
    Outcome e = transfer(buf, 0, true, true, 64, Tap(), Drop(), -1, 0, "EMPTY.TXT");
    ok &= check("an empty file still names itself on the way through",
                e.ok && e.opens == 1 && strcmp(e.hdrName, "EMPTY.TXT") == 0 &&
                e.hdrSize == 0);

    // 128 byte blocks only, which is what an old terminal program will do.
    Outcome s = transfer(buf, 2000, true, false, 64, Tap(), Drop(), -1, 0, "S.BIN");
    ok &= check("and it is exact in 128 byte blocks too",
                s.ok && s.exact && s.sBlkLen == kBlk128);

    // 0xFF everywhere, for the same reason the XMODEM tests use it: the IAC
    // escaping belongs to the socket layer and must not appear here.
    static uint8_t ff[3333];
    memset(ff, 0xFF, sizeof ff);
    Outcome f = transfer(ff, sizeof ff, true, true, 64, Tap(), Drop(), -1, 0, "IAC.PRG");
    ok &= check("3333 bytes of 0xFF arrive byte exact under YMODEM",
                f.ok && f.exact && f.got == sizeof ff);
    return ok;
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------
static bool test_ymodem_names() {
    printf("YMODEM filenames\n");
    static uint8_t buf[600];
    fill(buf, sizeof buf, 103);
    bool ok = true;

    Outcome a = transfer(buf, 600, true, true, 64, Tap(), Drop(), -1, 0, "readme.1st");
    ok &= check("a name with a dot in it survives the round trip",
                a.ok && strcmp(a.hdrName, "readme.1st") == 0);

    static const char kLong[] = "A_VERY_LONG_NAME_WITH.SEVERAL.DOTS.AND.MORE.TXT";
    Outcome b = transfer(buf, 600, true, true, 64, Tap(), Drop(), -1, 0, kLong);
    ok &= check("and so does a long one", b.ok && strcmp(b.hdrName, kLong) == 0);

    // The last name that fits. One more than this and the engine refuses
    // rather than writing the file under a shortened name.
    char full[kMaxName];
    memset(full, 'N', sizeof full);
    full[kMaxName - 5] = '.';
    full[kMaxName - 4] = 'B';
    full[kMaxName - 3] = 'I';
    full[kMaxName - 2] = 'N';
    full[kMaxName - 1] = '\0';
    Outcome d = transfer(buf, 600, true, true, 64, Tap(), Drop(), -1, 0, full);
    ok &= check("a name exactly as long as the buffer allows still fits",
                d.ok && strlen(d.hdrName) == kMaxName - 1 &&
                strcmp(d.hdrName, full) == 0);

    // An empty name would read as the end of the batch at the far end, so
    // the sender must not send one.
    Outcome c = transfer(buf, 600, true, true, 64, Tap(), Drop(), -1, 0, "");
    ok &= check("an empty name is replaced rather than closing the batch",
                c.ok && c.opens == 1 && c.hdrName[0] != '\0' && c.got == 600);
    return ok;
}

// ---------------------------------------------------------------------------
// The owner's side of block 0
// ---------------------------------------------------------------------------
static bool test_ymodem_open() {
    printf("YMODEM: the owner refusing a file\n");
    static uint8_t buf[2048];
    fill(buf, sizeof buf, 107);
    bool ok = true;

    Outcome a = transfer(buf, sizeof buf, true, true, 64, Tap(), Drop(), -1, 0,
                         "NOROOM.ZIP", true);
    ok &= check("open() returning false stops the transfer",
                !a.ok && a.rErr == Engine::Err::SinkFailed);
    ok &= check("and the sender is told rather than left sending",
                a.sErr == Engine::Err::RemoteCancel);
    ok &= check("and nothing at all was written",
                a.got == 0 && a.rBlocks == 0);
    ok &= check("but the owner did get to see the name it was refusing",
                a.opens == 1 && strcmp(a.hdrName, "NOROOM.ZIP") == 0);
    return ok;
}

// ---------------------------------------------------------------------------
// Block 0 on the wire, byte by byte. A loopback proves the two ends agree
// with each other; this proves they agree with the protocol.
// ---------------------------------------------------------------------------
static bool test_ymodem_wire() {
    printf("YMODEM block 0 on the wire\n");
    static uint8_t data[100];
    fill(data, sizeof data, 109);
    bool ok = true;

    Lone L;
    Src  s { data, sizeof data, 0, 0, 0 };
    L.e.beginSendY(srcRead, &s, "TEST.TXT", 100, L.now, true);
    L.collect();
    ok &= check("a YMODEM sender says nothing until it is asked", L.outN == 0);

    const uint8_t c = CRCREQ;
    L.give(&c, 1);
    L.collect(8);
    ok &= check("block 0 is a 128 byte block numbered 0",
                L.outN == 3 + kBlk128 + 2 &&
                L.out[0] == SOH && L.out[1] == 0x00 && L.out[2] == 0xFF);
    ok &= check("carrying the name, a NUL, and the length in decimal",
                memcmp(L.out + 3, "TEST.TXT\0" "100", 12) == 0);
    ok &= check("and NUL padded from there to the end of the block",
                L.out[3 + 12] == 0 && L.out[3 + kBlk128 - 1] == 0);
    ok &= check("with a CRC over it like any other block",
                (static_cast<uint16_t>((L.out[3 + kBlk128] << 8) |
                                        L.out[3 + kBlk128 + 1])) ==
                crc16(L.out + 3, kBlk128));
    ok &= check("the engine knows what it is sending",
                L.e.ymodem() && L.e.fileSize() == 100 &&
                strcmp(L.e.fileName(), "TEST.TXT") == 0);

    // Block 0 is not the file: an ACK for it must not count as payload.
    const uint8_t ack = ACK;
    L.outN = 0;
    L.give(&ack, 1);
    L.collect(8);
    ok &= check("an ACK for block 0 does not start the file on its own",
                L.outN == 0 && L.e.bytes() == 0 && L.e.blocks() == 0);

    L.give(&c, 1);
    L.collect(8);
    ok &= check("the second 'C' is what starts it, at block 1",
                L.outN >= 3 && L.out[0] == SOH && L.out[1] == 0x01 &&
                L.out[2] == 0xFE);

    // The file is 100 bytes, so one 128 byte block padded out, then the EOT.
    L.outN = 0;
    L.give(&ack, 1);
    L.collect(8);
    ok &= check("and one short block later the file is over", L.count(EOT) == 1);

    // The receiver NAKs the first EOT, the sender repeats it, the receiver
    // ACKs. Under YMODEM that is still not the end.
    const uint8_t nak = NAK;
    L.outN = 0;
    L.give(&nak, 1);
    L.collect(8);
    ok &= check("the repeated EOT goes out", L.count(EOT) == 1);
    L.outN = 0;
    L.give(&ack, 1);
    L.collect(8);
    ok &= check("and the sender is not finished yet: the batch is still open",
                !L.e.done() && L.e.running() && L.outN == 0);

    L.give(&c, 1);
    L.collect(8);
    ok &= check("one more 'C' fetches the empty block 0 that closes it",
                L.outN == 3 + kBlk128 + 2 && L.out[0] == SOH &&
                L.out[1] == 0x00 && L.out[2] == 0xFF && L.out[3] == 0x00);
    bool allNul = true;
    for (uint16_t i = 0; i < kBlk128; ++i) if (L.out[3 + i]) allNul = false;
    ok &= check("and it really is empty, all 128 bytes of it", allNul);

    L.give(&ack, 1);
    L.collect(8);
    ok &= check("the ACK for that ends the transfer", L.e.done());
    return ok;
}

// ---------------------------------------------------------------------------
// Block 0 coming the other way, hand built. These are the cases the loopback
// cannot produce because our own sender never sends them.
// ---------------------------------------------------------------------------
static bool test_ymodem_headers() {
    printf("YMODEM headers the loopback cannot make\n");
    static uint8_t data[128], got[2048];
    uint8_t        pkt[1100];
    const uint8_t  eot = EOT;
    fill(data, sizeof data, 113);
    bool ok = true;

    // An empty block 0 straight away: the sender has nothing to send.
    {
        Lone L;
        Dst  d { got, sizeof got, 0, -1, 0 };
        L.e.beginRecvY(dstOpen, dstWrite, &d, L.now);
        L.collect();
        ok &= check("a YMODEM receiver opens with 'C', never NAK",
                    L.outN >= 1 && L.out[0] == CRCREQ);
        L.outN = 0;
        L.give(pkt, mkHdr(pkt, nullptr, nullptr, true));
        L.collect();
        ok &= check("an empty block 0 ends the batch there and then",
                    L.e.done() && L.count(ACK) == 1 && d.opens == 0 && d.n == 0);
    }

    // A header with a name but no length. That is legal and it means the
    // sender is not telling us, so the padding stays: guessing is what this
    // engine refuses to do.
    {
        Lone L;
        Dst  d { got, sizeof got, 0, -1, 0 };
        L.e.beginRecvY(dstOpen, dstWrite, &d, L.now);
        L.collect();
        L.outN = 0;
        L.give(pkt, mkHdr(pkt, "NOSIZE.BIN", nullptr, true));
        L.collect();
        ok &= check("a header with no length is still accepted",
                    d.opens == 1 && d.size == 0 &&
                    strcmp(d.name, "NOSIZE.BIN") == 0);
        ok &= check("and is answered with ACK then 'C'",
                    L.outN >= 2 && L.out[0] == ACK && L.out[1] == CRCREQ);

        uint8_t blk[kBlk128];
        memcpy(blk, data, 100);
        memset(blk + 100, SUB, 28);
        L.outN = 0;
        L.give(pkt, mkPkt(pkt, 1, blk, kBlk128, true));
        L.collect();
        ok &= check("without a length every byte is written, padding and all",
                    d.n == 128 && memcmp(got, blk, 128) == 0);
        ok &= check("which is exactly the XMODEM behaviour, on purpose",
                    L.e.fileSize() == 0 && L.e.trailingPad() == 28);
    }

    // What lrzsz and SyncTERM actually put in block 0: the length, a space,
    // and the modification time in octal. The time is optional and we have
    // no use for it, but the space in front of it must not swallow the digit
    // before it or the length comes out wrong.
    {
        Lone L;
        Dst  d { got, sizeof got, 0, -1, 0 };
        L.e.beginRecvY(dstOpen, dstWrite, &d, L.now);
        L.collect();
        L.give(pkt, mkHdr(pkt, "REAL.ZIP", "12345 14176501234 100644", true));
        L.collect();
        ok &= check("a real sender's header, with an octal mtime after the length",
                    d.opens == 1 && d.size == 12345 &&
                    strcmp(d.name, "REAL.ZIP") == 0);
    }

    // A length that is not a multiple of the block size: the last write is
    // short and the padding never reaches the owner.
    {
        Lone L;
        Dst  d { got, sizeof got, 0, -1, 0 };
        L.e.beginRecvY(dstOpen, dstWrite, &d, L.now);
        L.collect();
        L.give(pkt, mkHdr(pkt, "SHORT.BIN", "200", true));
        L.collect();
        uint8_t blk[kBlk128];
        memcpy(blk, data, kBlk128);
        L.give(pkt, mkPkt(pkt, 1, blk, kBlk128, true));
        L.collect();
        uint8_t tail[kBlk128];
        fill(tail, 72, 127);
        memset(tail + 72, SUB, kBlk128 - 72);
        L.give(pkt, mkPkt(pkt, 2, tail, kBlk128, true));
        L.collect();
        ok &= check("the last block is cut at the stated length",
                    d.n == 200 && memcmp(got, blk, 128) == 0 &&
                    memcmp(got + 128, tail, 72) == 0);
        ok &= check("and the owner saw two writes, the second a short one",
                    d.blocks == 2);
        L.give(&eot, 1);
        L.collect();
        L.give(&eot, 1);
        L.collect();
        L.outN = 0;
        L.give(pkt, mkHdr(pkt, nullptr, nullptr, true));
        L.collect();
        ok &= check("and the empty header after the EOT finishes it",
                    L.e.done() && d.n == 200);
    }

    // A name too long for the buffer. Shortening it would write the file
    // under a name nobody chose.
    {
        char big[200];
        memset(big, 'X', sizeof big);
        big[sizeof big - 1] = '\0';
        Lone L;
        Dst  d { got, sizeof got, 0, -1, 0 };
        L.e.beginRecvY(dstOpen, dstWrite, &d, L.now);
        L.collect();
        L.outN = 0;
        L.give(pkt, mkHdr(pkt, big, "10", true));
        L.collect();
        ok &= check("a name too long to hold is refused, not shortened",
                    L.e.failed() && L.e.error() == Engine::Err::SinkFailed &&
                    d.opens == 0);
        ok &= check("and the sender is told with CANs", L.count(CAN) >= 2);
    }

    // A second file. We take one, and the one we have is already safe.
    {
        Lone L;
        Dst  d { got, sizeof got, 0, -1, 0 };
        L.e.beginRecvY(dstOpen, dstWrite, &d, L.now);
        L.collect();
        L.give(pkt, mkHdr(pkt, "ONE.BIN", "128", true));
        L.collect();
        L.give(pkt, mkPkt(pkt, 1, data, kBlk128, true));
        L.collect();
        L.give(&eot, 1);
        L.collect();
        L.give(&eot, 1);
        L.collect();
        L.outN = 0;
        L.give(pkt, mkHdr(pkt, "TWO.BIN", "128", true));
        L.collect();
        ok &= check("a second file in the batch is refused, not stored",
                    L.e.done() && d.opens == 1 && d.n == 128 &&
                    strcmp(d.name, "ONE.BIN") == 0);
        ok &= check("and the sender is told to stop", L.count(CAN) >= 2);
    }

    // A sender that never closes the batch. The file is written and
    // complete; failing it over the bookkeeping would be absurd.
    {
        Lone L;
        Dst  d { got, sizeof got, 0, -1, 0 };
        L.e.beginRecvY(dstOpen, dstWrite, &d, L.now);
        L.collect();
        L.give(pkt, mkHdr(pkt, "LONELY.BIN", "128", true));
        L.collect();
        L.give(pkt, mkPkt(pkt, 1, data, kBlk128, true));
        L.collect();
        L.give(&eot, 1);
        L.collect();
        L.give(&eot, 1);
        L.collect();
        // Long enough for the new start-poll budget: the receiver asks for
        // the closing header for as long as it would wait for a first
        // block, which is now minutes rather than seconds.
        int guard = 0;
        while (L.e.running() && guard++ < 40000) { L.collect(1); L.now += 25; }
        ok &= check("a sender that never sends the closing header loses nothing",
                    L.e.done() && d.n == 128 && memcmp(got, data, 128) == 0);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Damage, with a header in front of it. Block 0 has to be retransmitted like
// any other block, and the second 'C' can go missing like any other byte.
// ---------------------------------------------------------------------------
static bool test_ymodem_errors() {
    printf("YMODEM on a dirty line\n");
    static uint8_t buf[16384];
    fill(buf, sizeof buf, 131);
    bool ok = true;

    // Block 0 itself is the first block the tap sees.
    Tap t0;
    t0.hitBlock = 1;
    Outcome a = transfer(buf, 3000, true, true, 64, t0, Drop(), -1, 0, "HDR.PRG");
    ok &= check("a corrupted block 0 is NAKed and resent",
                a.ok && a.exact && a.got == 3000 && a.rErrs >= 1);
    ok &= check("and the name still came through",
                strcmp(a.hdrName, "HDR.PRG") == 0 && a.hdrSize == 3000);

    // The 'C' that follows the header's ACK goes missing. The sender resends
    // block 0, the receiver recognises the repeat and re-sends ACK and 'C'.
    Drop d;
    d.crcNo = 2;
    Outcome b = transfer(buf, 3000, true, true, 64, Tap(), d, -1, 0, "LOSTC.PRG");
    ok &= check("a lost 'C' after the header recovers instead of deadlocking",
                b.ok && b.exact && b.got == 3000);
    ok &= check("and the file was still opened exactly once", b.opens == 1);

    // An ACK lost mid-file, which is the classic write-it-twice trap, now
    // with a length that would mask it.
    Drop d2;
    d2.ackNo = 3;
    Outcome c = transfer(buf, 3000, true, true, 64, Tap(), d2, -1, 0, "ACK.PRG");
    ok &= check("a lost ACK under YMODEM does not duplicate a block",
                c.ok && c.exact && c.got == 3000);

    // And a long one on a line that is failing constantly.
    Tap t;
    t.everyN  = 3;
    t.spoilAt = 4;
    t.maxHits = 100000;
    Outcome e = transfer(buf, sizeof buf, true, true, 128, t, Drop(), -1, 0, "SOAK.ZIP");
    ok &= check("16 KB survives damage in every third block and stays exact",
                e.ok && e.exact && e.got == sizeof buf);

    // The EOT is as unprotected under YMODEM as it is under XMODEM.
    Tap te;
    te.eatEot = true;
    Outcome f = transfer(buf, 2500, true, true, 64, te, Drop(), -1, 0, "EOT.PRG");
    ok &= check("an EOT that never arrives is asked for again and the file is exact",
                f.ok && f.exact && f.got == 2500);
    return ok;
}

// ---------------------------------------------------------------------------
// The block number wraps at 255, so the 256th block is numbered 0, which is
// the header's number. A receiver that decides "block 0 means header" on the
// number alone corrupts every file past 256 blocks at exactly that point.
// ---------------------------------------------------------------------------
static bool test_ymodem_wrap() {
    printf("YMODEM across the block number wrap\n");
    const size_t n   = 40u * 1024u + 37u;      // 321 blocks of 128, and a tail
    uint8_t*     buf = static_cast<uint8_t*>(malloc(n));
    fill(buf, n, 139);
    bool ok = true;

    Outcome a = transfer(buf, n, true, false, 512, Tap(), Drop(), -1, 0, "WRAP.D64");
    ok &= check("a file of 321 blocks crosses the wrap and stays byte exact",
                a.ok && a.exact && a.got == n);
    ok &= check("and it really was 321 blocks", a.rBlocks == 321);
    ok &= check("with the file opened once, not twice", a.opens == 1);

    // Now lose the ACK for block 256, the one numbered 0. The sender repeats
    // it and the receiver must treat it as the duplicate data block it is,
    // not as a second file header.
    Drop d;
    d.ackNo = 257;            // block 0's ACK is the first, so block 256's is 257
    Outcome b = transfer(buf, n, true, false, 512, Tap(), d, -1, 0, "WRAP2.D64");
    ok &= check("a lost ACK on the block numbered 0 is not mistaken for a header",
                b.ok && b.exact && b.got == n && b.opens == 1);
    ok &= check("and the block was stored once, not twice", b.rBlocks == 321);

    free(buf);
    return ok;
}

// ---------------------------------------------------------------------------
// A stale NAK, and it is the nastiest thing in this file.
//
// An ACK is lost. The sender times out and resends the block off its own
// clock. The receiver, which timed out on the same pass, has already put a
// NAK on the wire for the same missing ACK. A sender that acts on that NAK
// sends a third copy, collects a second ACK for one block, and is one ACK
// ahead of itself for the rest of the file: every later ACK is read as the
// answer to the block after the one it was for, and the file ends with the
// receiver calling a block out of sequence.
//
// Whether the two timeouts land on the same pass is pure alignment, and the
// YMODEM handshake happens to produce it, which is how this was found. It
// was always reachable under plain XMODEM too. The sweep is over how much of
// a block crosses the wire per pass, because that is what moves the
// alignment: below about 160 bytes a block takes several passes and the
// sender's resend arrives before the receiver ever NAKs.
// ---------------------------------------------------------------------------
static bool test_stale_nak() {
    printf("A NAK for a block already being resent\n");
    static uint8_t buf[8192];
    fill(buf, sizeof buf, 149);
    bool ok   = true;
    bool allY = true, allX = true;

    for (size_t ck = 32; ck <= 1024; ck += 32) {
        Drop d;
        d.ackNo = 3;
        Outcome y = transfer(buf, 4096, true, false, ck, Tap(), d, -1, 0, "N.D64");
        Outcome x = transfer(buf, 4096, true, false, ck, Tap(), d);
        if (!(y.ok && y.exact && y.got == 4096)) allY = false;
        if (!(x.ok && x.got == 4096 + 0))        allX = false;
    }
    ok &= check("a lost ACK recovers at every wire width under YMODEM", allY);
    ok &= check("and at every wire width under plain XMODEM", allX);

    // The same thing on 1K blocks, where a whole block crossing in one pass
    // is the normal case rather than the awkward one.
    Drop d;
    d.ackNo = 2;
    Outcome k = transfer(buf, sizeof buf, true, true, 2048, Tap(), d, -1, 0, "K.ZIP");
    ok &= check("and on 1K blocks with the whole block crossing at once",
                k.ok && k.exact && k.got == sizeof buf);

    // The price of ignoring that NAK: if the resend we sent on our own clock
    // is itself corrupt, the NAK that says so is the one we swallow, and the
    // block waits for our next timeout instead of going again at once. That
    // is a slower recovery, not a lost file, and this pins it.
    Tap  t;
    t.everyN  = 2;
    t.spoilAt = 6;
    t.maxHits = 100000;
    Drop d2;
    d2.ackNo = 4;
    Outcome m = transfer(buf, 4096, true, false, 512, t, d2, -1, 0, "MESS.D64");
    ok &= check("a lost ACK on top of a line corrupting every other block "
                "still lands the file", m.ok && m.exact && m.got == 4096);
    Outcome mx = transfer(buf, 4096, true, false, 512, t, d2);
    ok &= check("and the same under plain XMODEM", mx.ok && mx.got == 4096);
    return ok;
}

// ---------------------------------------------------------------------------
// An engine is reused, and a YMODEM transfer must not leave anything behind
// for the plain XMODEM one that follows it
// ---------------------------------------------------------------------------
static bool test_ymodem_reuse() {
    printf("YMODEM state does not leak into the next transfer\n");
    static uint8_t buf[500];
    fill(buf, sizeof buf, 137);
    bool ok = true;

    Lone L;
    Src  s { buf, 500, 0, 0, 0 };
    L.e.beginSendY(srcRead, &s, "FIRST.BIN", 500, L.now, true);
    ok &= check("a YMODEM engine says so", L.e.ymodem());

    L.e.reset();
    ok &= check("and after reset() it does not",
                !L.e.ymodem() && L.e.fileSize() == 0 && L.e.fileName()[0] == '\0');

    // The same engine now doing plain XMODEM must pad again.
    static uint8_t got[1024];
    Dst d { got, sizeof got, 0, -1, 0 };
    L.e.beginRecv(dstWrite, &d, L.now, true);
    L.collect();
    ok &= check("and a plain XMODEM receive on it behaves as it always did",
                !L.e.ymodem() && L.outN >= 1 && L.out[0] == CRCREQ);
    return ok;
}

// ---------------------------------------------------------------------------
// Size on the board. A per-node engine would be sixteen of these.
// ---------------------------------------------------------------------------
static bool test_size() {
    printf("Footprint\n");
    printf("        (sizeof(xmodem::Engine) = %u bytes)\n",
           static_cast<unsigned>(sizeof(Engine)));
    // 1128 bytes before YMODEM, 1208 after: block 0's name buffer, the two
    // lengths and the open callback. Still one engine on the board, not one
    // per node, so the 80 bytes buy the exact file size for nothing that
    // matters.
    return check("one engine is a block buffer plus change, under 1280 bytes",
                 sizeof(Engine) <= 1280);
}

int main() {
    bool ok = true;
    ok &= test_checks();
    ok &= test_sizes();
    ok &= test_binary();
    ok &= test_wrap();
    ok &= test_errors();
    ok &= test_lost_ack();
    ok &= test_fallback();
    ok &= test_soak();
    ok &= test_lost_eot();
    ok &= test_duplicate();
    ok &= test_eot();
    ok &= test_sequence();
    ok &= test_bad_header();
    ok &= test_noise();
    ok &= test_cancel();
    ok &= test_silence();
    ok &= test_owner();
    ok &= test_ymodem_exact();
    ok &= test_ymodem_sizes();
    ok &= test_ymodem_names();
    ok &= test_ymodem_open();
    ok &= test_ymodem_wire();
    ok &= test_ymodem_headers();
    ok &= test_ymodem_errors();
    ok &= test_ymodem_wrap();
    ok &= test_stale_nak();
    ok &= test_ymodem_reuse();
    ok &= test_size();

    printf("\n%d checks, %d failures: %s\n", g_checks, g_fails,
           g_fails ? "FAILURES" : "ALL PASS");
    return (ok && !g_fails) ? 0 : 1;
}
