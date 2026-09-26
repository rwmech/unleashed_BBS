// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/link.cpp
// Module:       Core / the µnleashed link's protocol engine (1.2.0)
//
// Purpose:      See link.h. The order of this file: the wire (header, CRCs,
//               COBS), the tables, receiving, transmitting, the timers,
//               pairing, the runner's bulk pump, and the public calls.
//
// Libraries:    mbedTLS, through linkcrypto
// Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1), the Linux host build, and
//               the link's peers
// See also:     LINK.md
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3 of the License, or (at your
// option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>. The full
// text is in the LICENSE file at the top of this repository.
// ===========================================================================
#include "link.h"

#include <cstdio>
#include <cstring>
#include <new>

#include "crc32.h"

namespace ulink {

// ===========================================================================
// The wire
// ===========================================================================
namespace {

inline void put16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); }
inline void put32(uint8_t* p, uint32_t v) { put16(p, static_cast<uint16_t>(v)); put16(p + 2, static_cast<uint16_t>(v >> 16)); }
inline uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t get32(const uint8_t* p) { return get16(p) | (static_cast<uint32_t>(get16(p + 2)) << 16); }

// Before: is a before b in a 16-bit sequence space (serial number arithmetic).
inline bool before(uint16_t a, uint16_t b) { return static_cast<int16_t>(a - b) < 0; }

// Time: has now reached t, across the 32-bit millis wrap.
inline bool reached(uint32_t now, uint32_t t) { return static_cast<int32_t>(now - t) >= 0; }

void copyName(char* dst, size_t cap, const uint8_t* src, size_t n) {
    size_t i = 0;
    for (; i < n && i + 1 < cap && src[i]; ++i) dst[i] = (src[i] >= 0x20 && src[i] < 0x7F) ? static_cast<char>(src[i]) : '?';
    dst[i] = '\0';
}

void putName(uint8_t* dst, size_t n, const char* s) {
    memset(dst, 0, n);
    if (s) for (size_t i = 0; i < n && s[i]; ++i) dst[i] = static_cast<uint8_t>(s[i]);
}

// Guard: the embedder's recursive lock for the length of a public call, so
// pumpRx on the runner and the loop's calls never meet inside the engine.
struct Guard {
    Io& io;
    explicit Guard(Io& i) : io(i) { io.lock(); }
    ~Guard() { io.unlock(); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

// The HKDF labels (LINK.md, Pairing and keys; Sessions).
const uint8_t kInfoSess[] = { 's','e','s','s' };

}  // namespace

void packHeader(const Header& h, uint8_t out[kHdr]) {
    out[0] = h.ver;
    out[1] = h.family;
    out[2] = h.type;
    out[3] = h.flags;
    put16(out + 4, h.session);
    put16(out + 6, h.seq);
    put16(out + 8, h.frag);
    put16(out + 10, h.nfrag);
    out[12] = h.len;
    out[13] = h.chan;
    put16(out + 14, h.crc);
    put32(out + 16, h.pn);
}

bool unpackHeader(const uint8_t* in, size_t n, Header& h) {
    if (n < kHdr) return false;
    h.ver     = in[0];
    h.family  = in[1];
    h.type    = in[2];
    h.flags   = in[3];
    h.session = get16(in + 4);
    h.seq     = get16(in + 6);
    h.frag    = get16(in + 8);
    h.nfrag   = get16(in + 10);
    h.len     = in[12];
    h.chan    = in[13];
    h.crc     = get16(in + 14);
    h.pn      = get32(in + 16);
    return true;
}

uint16_t crc16(uint16_t crc, const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        crc ^= static_cast<uint16_t>(d[i]) << 8;
        for (int b = 0; b < 8; ++b) crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

uint32_t bulkFrags(uint32_t total) {
    // Two at least, so a bulk message is never mistaken for a single frame:
    // nfrag is what tells a receiver which it is.
    uint32_t n = (total + kPreamble + kPayloadMax - 1) / kPayloadMax;
    return n < 2 ? 2 : n;
}

uint16_t frameCrc(const uint8_t hdr[kHdr], const uint8_t* payload, size_t n) {
    uint16_t c = crc16(0xFFFF, hdr, 14);
    c = crc16(c, hdr + 16, 4);
    return crc16(c, payload, n);
}

bool clearType(uint8_t family, uint8_t type) {
    if (family != FAM_LINK) return false;
    switch (type) {
        case T_DISCOVER: case T_BEACON: case T_HELLO: case T_HELLO_ACK:
        case T_PAIR_HELLO: case T_PAIR_OFFER: case T_PAIR_NONCE: case T_PAIR_REVEAL:
            return true;
        default:
            return false;
    }
}

size_t cobsEncode(const uint8_t* in, size_t n, uint8_t* out, size_t cap) {
    size_t o = 1, code = 0;
    uint8_t run = 1;
    if (cap < 2) return 0;
    for (size_t i = 0; i < n; ++i) {
        if (in[i] == 0) {
            out[code] = run;
            code = o++;
            run = 1;
            if (o > cap) return 0;
        } else {
            if (o >= cap) return 0;
            out[o++] = in[i];
            if (++run == 0xFF) {
                out[code] = run;
                code = o++;
                run = 1;
                if (o > cap) return 0;
            }
        }
    }
    out[code] = run;
    if (o >= cap) return 0;
    out[o++] = 0;
    return o;
}

size_t cobsDecode(const uint8_t* in, size_t n, uint8_t* out, size_t cap) {
    size_t i = 0, o = 0;
    while (i < n) {
        uint8_t code = in[i++];
        if (code == 0) return 0;
        for (uint8_t k = 1; k < code; ++k) {
            if (i >= n || in[i] == 0 || o >= cap) return 0;
            out[o++] = in[i++];
        }
        if (code != 0xFF && i < n) {
            if (o >= cap) return 0;
            out[o++] = 0;
        }
    }
    return o;
}

bool Mac::operator==(const Mac& o) const { return memcmp(b, o.b, 6) == 0; }
bool Mac::zero() const { static const uint8_t z[6] = {}; return memcmp(b, z, 6) == 0; }

const char* kindName(uint8_t kind) {
    switch (kind) {
        case KIND_CAMSAT:  return "camsat";
        case KIND_DOORBOX: return "doorbox";
        default:           return "device";
    }
}

const char* dropName(uint8_t d) {
    static const char* const kNames[D_COUNT] = {
        "short", "version", "length", "stranger", "unsealed", "tag", "replay", "crc",
        "order", "window", "session", "ring",
    };
    return d < D_COUNT ? kNames[d] : "?";
}

// ===========================================================================
// The tables
// ===========================================================================
namespace {
enum : uint8_t { PS_IDLE, PS_SCAN, PS_HELLO, PS_WAITUP, PS_UP };   // a peer's link state
// Pairing, commit then reveal (LINK.md, "Pairing and keys"):
//   host: KEYGEN (runner) -> OFFERING (answer HELLO with OFFER, NONCE with
//         REVEAL; the code is known once NONCE came) -> ASKING the sysop ->
//         CONFIRMING (PAIR_DONE until PAIR_ACK)
//   peer: HELLOING (PAIR_HELLO on every channel) -> SHARED (ECDH) ->
//         NONCING (PAIR_NONCE until REVEAL) -> WAITDONE -> DONE_
enum : uint8_t { PAIR_NONE, PAIR_KEYGEN, PAIR_OFFERING, PAIR_ASKING, PAIR_CONFIRMING,  // host
                 PAIR_HELLOING, PAIR_SHARED, PAIR_NONCING, PAIR_WAITDONE, PAIR_DONE_,  // peer
                 PAIR_COMPUTING };                                            // either, on the runner
enum : uint8_t { BX_FREE, BX_RECV, BX_BEGUN, BX_FINISHING, BX_OK, BX_BAD, BX_ABORT, BX_CANCEL, BX_REFUSED };

// Deferred events: what pumpRx on the runner would tell the owner, told by
// the next poll on the loop instead.
enum : uint8_t { DE_UP = 1, DE_RESET, DE_BULKSENT };

// helloTag: HELLO and HELLO_ACK travel before a session key exists, so they
// are authenticated with the pairing key instead: the first 8 bytes of
// HMAC-SHA256(k_link, dir || type || payload before the tag). A spoofed
// HELLO can then neither change what the host believes of a peer nor keep
// replacing its pending key.
void helloTag(const uint8_t key[linkcrypto::kKey], uint8_t dir, uint8_t type,
              const uint8_t* p, size_t n, uint8_t out[8]) {
    uint8_t buf[64];
    buf[0] = dir;
    buf[1] = type;
    memcpy(buf + 2, p, n);
    uint8_t mac[32];
    linkcrypto::hmac(key, linkcrypto::kKey, buf, n + 2, mac);
    memcpy(out, mac, 8);
    linkcrypto::wipe(mac, sizeof(mac));
}

bool tagOk(const uint8_t a[8], const uint8_t b[8]) {
    uint8_t d = 0;
    for (int i = 0; i < 8; ++i) d |= static_cast<uint8_t>(a[i] ^ b[i]);
    return d == 0;
}

// The HELLO and HELLO_ACK layouts.
constexpr size_t kHelloLen    = 39;   // np 16, ver, kind, fw 12, families 4, win, epoch 4
constexpr size_t kHelloAckLen = 37;   // np 16, nh 16, win, epoch 4
}  // namespace

struct Engine::Peer {
    bool      used = false;
    Mac       mac;
    uint8_t   kind = KIND_UNKNOWN;
    uint8_t   kLink[linkcrypto::kKey] = {};
    uint8_t   kSess[linkcrypto::kKey] = {};
    bool      haveSess = false;
    uint8_t   kPend[linkcrypto::kKey] = {};
    bool      havePend = false;
    uint8_t   np[16] = {};            // host: the pending HELLO's nonce; peer: its own
    uint8_t   nh[16] = {};            // host: its nonce for that HELLO
    uint32_t  txPn = 1;
    uint32_t  rxHigh = 0;
    uint64_t  rxMask = 0;
    uint32_t  pendHigh = 0;
    uint64_t  pendMask = 0;
    // What the pending HELLO said, applied only once its key is proved.
    uint8_t   pendKind = KIND_UNKNOWN;
    uint8_t   pendWin = 16;
    uint32_t  pendEpoch = 0;
    uint32_t  epoch = 0;              // the far end's boot epoch, 0 before the first
    bool      ackUsed = false;        // peer: the HELLO_ACK for np is taken already
    uint32_t  rekeyAt = 0;            // host: when it last asked for a new HELLO
    bool      up = false;
    uint8_t   state = PS_IDLE;        // peer role only
    uint8_t   bulkWin = 16;           // the far end's receive window for bulk
    uint32_t  beaconAt = 0;
    uint32_t  downSince = 0;
    PeerStats st;
};

struct Engine::Sess {
    bool      used = false;
    bool      remote = false;         // the far end opened it
    uint8_t   peer = 0;
    uint16_t  id = 0;
    uint8_t   family = 0;
    uint16_t  txSeq = 0;              // next seq to give a message
    uint16_t  txAcked = 0;            // every seq before this is acknowledged
    uint16_t  rxExpect = 0;           // the next seq to deliver
    bool      ackDue = false;
    bool      ackNow = false;
    uint32_t  ackAt = 0;
    bool      refusing = false;       // the owner said "no room": advertise 0
    bool      closing = false;        // closeAfter: drop once nothing is in flight
    uint32_t  lastActive = 0;
};

struct Engine::TxMsg {
    bool            used = false;
    uint8_t         peer = 0;
    uint16_t        sess = 0;
    uint16_t        seq = 0;
    uint8_t         family = 0;
    uint8_t         type = 0;
    bool            rel = true;
    bool            bulk = false;
    bool            sentOnce = false;
    uint8_t         len = 0;
    uint8_t         data[kPayloadMax] = {};
    // bulk
    const uint8_t*  src = nullptr;
    uint32_t        total = 0;
    uint32_t        crc = 0;
    uint16_t        nfrag = 0;
    uint16_t        cum = 0;          // every fragment before this is received
    uint32_t        sack = 0;         // bit i: fragment cum + 1 + i received
    uint16_t        next = 0;         // the next fragment to send
    uint16_t        win = 16;
    // retransmission
    uint8_t         tries = 0;
    uint32_t        rto = Engine::kRtoMs;
    uint32_t        due = 0;
    uint32_t        sentAt = 0;
    bool            blocked = false;  // the far end said win 0
};

struct Engine::Ctrl {
    bool     used = false;
    uint8_t  peer = 0xFF;             // 0xFF: to mac, clear
    Mac      mac;
    bool     broadcast = false;
    uint8_t  type = 0;
    bool     sealed = false;
    bool     pairKey = false;         // sealed with the pairing key, pn 0
    uint8_t  len = 0;
    uint8_t  data[120] = {};
};

struct Engine::BulkRx {
    std::atomic<uint8_t>  st{ BX_FREE };
    std::atomic<uint16_t> consumed{ 0 };
    std::atomic<uint64_t> have{ 0 };
    uint8_t   peer = 0;
    uint16_t  sess = 0;
    uint16_t  seq = 0;
    uint8_t   family = 0;
    uint8_t   type = 0;
    uint16_t  nfrag = 0;
    uint32_t  total = 0;
    uint32_t  crcWant = 0;
    uint8_t   lens[Engine::kBulkWinMax] = {};
    // the runner's
    uint32_t  crcRun = 0;
    uint32_t  got = 0;
    // the loop's
    uint16_t  ackedAt = 0;
    uint16_t  sinceAck = 0;
};

struct Engine::Pair {
    uint8_t   st = PAIR_NONE;
    bool      open = false;           // host: the sysop's window
    uint32_t  until = 0;
    bool      asked = false;
    PairInfo  info;                   // host: who asks; peer: the host
    uint8_t   np[16] = {};
    uint8_t   nh[16] = {};
    uint8_t   priv[linkcrypto::kPriv] = {};
    uint8_t   pubMine[linkcrypto::kPub] = {};
    uint8_t   pubTheirs[linkcrypto::kPub] = {};
    uint8_t   secret[linkcrypto::kSecret] = {};   // the ECDH secret, until the nonces are both in
    uint8_t   commit[16] = {};        // SHA-256(pub_h || nh), first 16 bytes
    uint8_t   key[linkcrypto::kKey] = {};
    uint8_t   theirChan = 0;
    uint32_t  resendAt = 0;
    uint32_t  offerAt = 0;            // host: the last OFFER, so a HELLO storm gets one a 100 ms
    // peer
    uint8_t   kind = KIND_UNKNOWN;
    char      name[17] = {};
    char      fw[13] = {};
};

// ===========================================================================
// Construction
// ===========================================================================
Engine::Engine(Role role, Io& io, const Events& ev, uint8_t bulkWin, uint8_t* bulkMem)
    : role_(role), io_(io), ev_(ev),
      npeers_(role == Role::Host ? kPeers : 1),
      bulkWin_(bulkWin > kBulkWinMax ? kBulkWinMax : (bulkWin < 2 ? 2 : bulkWin)),
      bulkMem_(bulkMem) {
    peers_ = new (std::nothrow) Peer[npeers_];
    sess_  = new (std::nothrow) Sess[kSessions];
    tx_    = new (std::nothrow) TxMsg[kTxMsgs];
    ctrl_  = new (std::nothrow) Ctrl[kCtrl];
    bulk_  = new (std::nothrow) BulkRx;
    pair_  = new (std::nothrow) Pair;
    // A random boot epoch, and session ids starting somewhere random, so an
    // end that restarts is not taken for the one before it (LINK.md,
    // "Sessions"): the far end drops the old sessions when the epoch in a
    // proved HELLO changes, and a new id rarely lands on an old one anyway.
    uint8_t r[6];
    io_.random(r, sizeof(r));
    epoch_ = get32(r);
    if (!epoch_) epoch_ = 1;
    const uint16_t off = static_cast<uint16_t>(get16(r + 4) % 0x7FFE);
    nextSess_ = static_cast<uint16_t>((role == Role::Host ? 1 : 0x8001) + off);
}

Engine::~Engine() {
    if (peers_) for (uint8_t i = 0; i < npeers_; ++i) linkcrypto::wipe(&peers_[i], sizeof(Peer));
    if (pair_) linkcrypto::wipe(pair_, sizeof(Pair));
    delete[] peers_;
    delete[] sess_;
    delete[] tx_;
    delete[] ctrl_;
    delete bulk_;
    delete pair_;
}

bool Engine::ok() const { return peers_ && sess_ && tx_ && ctrl_ && bulk_ && pair_ && bulkMem_; }

void Engine::drop(uint8_t reason) { if (reason < D_COUNT) ++drops_[reason]; }

uint32_t Engine::dropsTotal() const {
    uint32_t t = 0;
    for (uint32_t d : drops_) t += d;
    return t;
}

// ---------------------------------------------------------------------------
// Replay window: a pn is new if above the highest seen, or among the 64 below
// it and not yet seen. pn 0 is the pairing confirmation's and never a session
// frame's.
// ---------------------------------------------------------------------------
namespace {
bool replayOk(uint32_t high, uint64_t mask, uint32_t pn) {
    if (pn == 0) return false;
    if (pn > high) return true;
    uint32_t back = high - pn;
    if (back >= 64) return false;
    return !(mask & (1ull << back));
}
void replayMark(uint32_t& high, uint64_t& mask, uint32_t pn) {
    if (pn > high) {
        uint32_t shift = pn - high;
        mask = shift >= 64 ? 0 : (mask << shift);
        mask |= 1;
        high = pn;
    } else {
        mask |= 1ull << (high - pn);
    }
}
}  // namespace

// ===========================================================================
// Tables
// ===========================================================================
Engine::Sess* Engine::findSess(uint8_t pi, uint16_t id) {
    for (uint8_t i = 0; i < kSessions; ++i)
        if (sess_[i].used && sess_[i].peer == pi && sess_[i].id == id) return &sess_[i];
    return nullptr;
}

const Engine::Sess* Engine::findSess(uint8_t pi, uint16_t id) const {
    for (uint8_t i = 0; i < kSessions; ++i)
        if (sess_[i].used && sess_[i].peer == pi && sess_[i].id == id) return &sess_[i];
    return nullptr;
}

Engine::Sess* Engine::newSess(uint8_t pi, uint16_t id, uint8_t family) {
    for (uint8_t i = 0; i < kSessions; ++i) {
        if (sess_[i].used) continue;
        Sess& s = sess_[i];
        s = Sess();
        s.used = true;
        s.peer = pi;
        s.id = id;
        s.family = family;
        s.lastActive = io_.millis();
        return &s;
    }
    return nullptr;
}

// bury: remember a session closed here, the oldest tombstone making way.
void Engine::bury(const Sess& s, uint32_t now) {
    Tomb* t = &tombs_[0];
    for (Tomb& x : tombs_) {
        if (!x.used) { t = &x; break; }
        if (static_cast<int32_t>(x.at - t->at) < 0) t = &x;
    }
    t->used = true;
    t->told = false;
    t->peer = s.peer;
    t->id = s.id;
    t->expect = s.rxExpect;
    t->at = now;
}

Engine::Tomb* Engine::tombOf(uint8_t pi, uint16_t id, uint32_t now) {
    for (Tomb& t : tombs_) {
        if (!t.used) continue;
        if (reached(now, t.at + kTombMs)) { t.used = false; continue; }
        if (t.peer == pi && t.id == id) return &t;
    }
    return nullptr;
}

void Engine::dropSess(Sess& s) {
    for (uint8_t i = 0; i < kTxMsgs; ++i) {
        TxMsg& m = tx_[i];
        if (!m.used || m.peer != s.peer || m.sess != s.id) continue;
        if (m.bulk) emit(DE_BULKSENT, m.peer, m.sess, m.family, 0);
        m.used = false;
    }
    BulkRx& b = *bulk_;
    uint8_t st = b.st.load();
    if (st != BX_FREE && b.peer == s.peer && b.sess == s.id) {
        // The runner may be inside this message: mark it, and bulkEndCheck
        // hands it back once the runner is out. A compare-and-swap, because
        // the runner moves the state itself and a plain store could land on
        // top of its OK.
        if (st == BX_RECV || st == BX_REFUSED) {
            b.st.store(BX_FREE);
        } else {
            while (st != BX_FREE && st != BX_CANCEL && !b.st.compare_exchange_weak(st, BX_CANCEL)) {}
        }
    }
    s.used = false;
}

void Engine::emit(uint8_t kind, uint8_t pi, uint16_t sess, uint8_t family, uint8_t v) {
    if (onRunner_) {
        // pumpRx on the runner: the owner's handlers are the loop's, so the
        // event waits for the next poll.
        if (ndeferred_ < kDeferred) deferred_[ndeferred_++] = Deferred{ kind, pi, family, v, sess };
        return;
    }
    switch (kind) {
        case DE_UP:       if (ev_.peerState) ev_.peerState(ev_.ctx, pi, v != 0); break;
        case DE_RESET:    if (ev_.reset) ev_.reset(ev_.ctx, pi, sess, family, v); break;
        case DE_BULKSENT: if (ev_.bulkSent) ev_.bulkSent(ev_.ctx, pi, sess, family, v != 0); break;
        default: break;
    }
}

void Engine::farRestarted(uint8_t pi) {
    // Its sessions are gone on its side: fail them here, telling nobody on
    // the far end (it has never heard of them) and the owners here.
    for (uint8_t k = 0; k < kSessions; ++k)
        if (sess_[k].used && sess_[k].peer == pi) failSess(sess_[k], R_RESTART, false);
    // And it numbers its sessions from the start again: forget the closed ones.
    for (Tomb& t : tombs_) if (t.used && t.peer == pi) t.used = false;
}

void Engine::failSess(Sess& s, uint8_t reason, bool tellFar) {
    uint8_t pi = s.peer;
    uint16_t id = s.id;
    uint8_t fam = s.family;
    if (tellFar && peers_[pi].haveSess) {
        uint8_t p[3];
        put16(p, id);
        p[2] = reason;
        queueCtrl(pi, nullptr, T_RESET, p, sizeof(p), true);
    }
    dropSess(s);
    emit(DE_RESET, pi, id, fam, reason);
}

uint8_t Engine::inFlight(const Sess& s) const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < kTxMsgs; ++i)
        if (tx_[i].used && tx_[i].peer == s.peer && tx_[i].sess == s.id) ++n;
    return n;
}

void Engine::peerDown(uint8_t pi) {
    Peer& p = peers_[pi];
    bool was = p.up;
    p.up = false;
    p.downSince = io_.millis();
    if (role_ == Role::Peer) { p.state = PS_SCAN; scanAt_ = 0; }
    if (was && ev_.peerState) ev_.peerState(ev_.ctx, pi, false);
}

void Engine::derivePending(Peer& p, const uint8_t np[16], const uint8_t nh[16]) {
    uint8_t salt[32];
    memcpy(salt, np, 16);
    memcpy(salt + 16, nh, 16);
    linkcrypto::hkdf(salt, sizeof(salt), p.kLink, sizeof(p.kLink), kInfoSess, sizeof(kInfoSess),
                     p.kPend, sizeof(p.kPend));
}

// ===========================================================================
// Transmit
// ===========================================================================
bool Engine::sendFrame(const Mac* to, uint8_t pi, Header& h, const uint8_t* p, bool seal,
                       const uint8_t* keyOverride, uint32_t pnOverride) {
    const uint8_t dir = role_ == Role::Host ? DIR_HOST : DIR_PEER;
    h.ver  = kVersion;
    h.chan = io_.channel();
    h.flags = static_cast<uint8_t>((h.flags & F_REL) | (seal ? F_SEC : 0));
    h.crc  = 0;
    if (seal) {
        if (keyOverride) {
            h.pn = pnOverride;
        } else {
            Peer& pr = peers_[pi];
            if (!pr.haveSess) return false;
            if (pr.txPn == 0) pr.txPn = 1;
            h.pn = pr.txPn++;
        }
    } else {
        h.pn = 0;
    }
    uint8_t* f = frame_;
    packHeader(h, f);
    h.crc = frameCrc(f, p, h.len);
    put16(f + 14, h.crc);
    size_t n = kHdr + h.len;
    if (seal) {
        const uint8_t* key = keyOverride ? keyOverride : peers_[pi].kSess;
        if (!linkcrypto::seal(key, dir, h.pn, f, kHdr, p, h.len, f + kHdr, f + kHdr + h.len)) return false;
        n += kTag;
    } else if (h.len) {
        memcpy(f + kHdr, p, h.len);
    }
    const Mac* dst = to ? to : (pi < npeers_ ? &peers_[pi].mac : nullptr);
    if (!io_.send(dst, f, n)) return false;
    if (pi < npeers_) ++peers_[pi].st.tx;
    return true;
}

bool Engine::queueCtrl(uint8_t pi, const Mac* to, uint8_t type, const uint8_t* p, size_t n,
                       bool sealed, uint16_t sess) {
    (void)sess;
    if (n > sizeof(Ctrl::data)) return false;
    for (uint8_t i = 0; i < kCtrl; ++i) {
        Ctrl& c = ctrl_[i];
        if (c.used) continue;
        c = Ctrl();
        c.used = true;
        c.peer = pi;
        if (to) c.mac = *to;
        c.broadcast = (pi == 0xFF && !to);
        c.type = type;
        c.sealed = sealed;
        c.len = static_cast<uint8_t>(n);
        if (n) memcpy(c.data, p, n);
        return true;
    }
    return false;
}

bool Engine::buildAck(Sess& s, uint8_t* out, size_t& n) {
    // sess(2) cumSeq(2) flags(1) bulkSeq(2) bulkCum(2) sack(4) win(2) = 15
    memset(out, 0, 15);
    put16(out, s.id);
    put16(out + 2, s.rxExpect);
    uint16_t win = s.refusing ? 0 : kStreamWin;
    BulkRx& b = *bulk_;
    uint8_t bst = b.st.load();
    if (bst != BX_FREE && b.peer == s.peer && b.sess == s.id) {
        const uint16_t consumed = b.consumed.load();
        const uint64_t have = b.have.load();
        uint16_t c = consumed;
        while (c < b.nfrag && c < consumed + bulkWin_ && (have & (1ull << (c % bulkWin_)))) ++c;
        uint32_t sack = 0;
        for (uint8_t i = 0; i < 32; ++i) {
            uint32_t g = static_cast<uint32_t>(c) + 1 + i;
            if (g >= b.nfrag || g >= static_cast<uint32_t>(consumed) + bulkWin_) break;
            if (have & (1ull << (g % bulkWin_))) sack |= 1u << i;
        }
        out[4] = 1;
        put16(out + 5, b.seq);
        put16(out + 7, c);
        put32(out + 9, sack);
        win = static_cast<uint16_t>(consumed + bulkWin_ - c);
        b.ackedAt = consumed;
        b.sinceAck = 0;
    }
    put16(out + 13, win);
    n = 15;
    return true;
}

// transmitOne: the next frame, by priority: control, then acknowledgements,
// then messages whose time has come. False when there was nothing to send.
bool Engine::transmitOne() {
    const uint32_t now = io_.millis();

    // 1. Control frames, oldest first.
    for (uint8_t i = 0; i < kCtrl; ++i) {
        Ctrl& c = ctrl_[i];
        if (!c.used) continue;
        c.used = false;
        Header h;
        h.family = FAM_LINK;
        h.type = c.type;
        h.len = c.len;
        if (c.pairKey) {
            sendFrame(&c.mac, 0xFF, h, c.data, true, pair_->key, 0);
        } else if (c.peer != 0xFF) {
            if (c.sealed && !peers_[c.peer].haveSess) continue;
            sendFrame(nullptr, c.peer, h, c.data, c.sealed);
        } else {
            sendFrame(c.broadcast ? nullptr : &c.mac, 0xFF, h, c.data, false);
        }
        return true;
    }

    // 2. Acknowledgements that are due.
    for (uint8_t i = 0; i < kSessions; ++i) {
        Sess& s = sess_[i];
        if (!s.used || !s.ackDue) continue;
        if (!s.ackNow && !reached(now, s.ackAt)) continue;
        if (!peers_[s.peer].haveSess) continue;
        uint8_t p[16];
        size_t n = 0;
        buildAck(s, p, n);
        Header h;
        h.family = FAM_LINK;
        h.type = T_ACK;
        h.len = static_cast<uint8_t>(n);
        s.ackDue = false;
        s.ackNow = false;
        sendFrame(nullptr, s.peer, h, p, true);
        return true;
    }

    // 3. Messages. The lowest seq of each session goes first, so a stream
    // stays in order even when a retransmission and a new message are both
    // due.
    TxMsg* best = nullptr;
    for (uint8_t i = 0; i < kTxMsgs; ++i) {
        TxMsg& m = tx_[i];
        if (!m.used) continue;
        Peer& pr = peers_[m.peer];
        if (!pr.up || !pr.haveSess) continue;
        const Sess* s = findSess(m.peer, m.sess);
        if (!s) { m.used = false; continue; }
        // Within the stream window from the oldest unacknowledged message,
        // and nothing after a bulk message that is still going.
        if (!before(m.seq, static_cast<uint16_t>(s->txAcked + kStreamWin))) continue;
        bool behindBulk = false;
        for (uint8_t k = 0; k < kTxMsgs; ++k) {
            const TxMsg& o = tx_[k];
            if (o.used && o.bulk && &o != &m && o.peer == m.peer && o.sess == m.sess && before(o.seq, m.seq)) behindBulk = true;
        }
        if (behindBulk) continue;
        bool ready;
        if (m.bulk) {
            const bool fresh = m.next < m.nfrag && m.next < static_cast<uint32_t>(m.cum) + m.win && !m.blocked;
            ready = fresh || (m.sentOnce && reached(now, m.due));
        } else {
            ready = !m.sentOnce || reached(now, m.due);
        }
        if (!ready) continue;
        if (!best || (m.peer == best->peer && m.sess == best->sess && before(m.seq, best->seq))) best = &m;
    }
    if (!best) return false;
    TxMsg& m = *best;
    Header h;
    h.family  = m.family;
    h.type    = m.type;
    h.session = m.sess;
    h.seq     = m.seq;
    h.flags   = m.rel ? F_REL : 0;

    // A retry counts against the message only if the far end has been heard
    // since the last try: then it is alive and not taking this message. A
    // far end that has gone quiet is a link outage, which the peer-down rules
    // handle (a minute's grace), so a channel hop costs a pause, not a
    // session.
    auto countTry = [&]() -> bool {
        m.rto = m.rto * 2 > kRtoMaxMs ? kRtoMaxMs : m.rto * 2;
        ++peers_[m.peer].st.retries;
        if (!reached(peers_[m.peer].st.lastHeard, m.sentAt)) return true;
        // Only the oldest message of a session counts its tries: the ones
        // behind it are waiting on it, not failing (the receiver takes a
        // session's messages in order and drops those after a gap).
        const Sess* hs = findSess(m.peer, m.sess);
        if (hs && m.seq != hs->txAcked) return true;
        if (++m.tries <= kTries) return true;
        Sess* s = findSess(m.peer, m.sess);
        if (s) failSess(*s, R_RETRIES, true);
        return false;
    };

    if (!m.bulk) {
        if (m.sentOnce) {
            if (!m.blocked && !countTry()) return true;
        } else {
            m.tries = 1;
        }
        h.len = m.len;
        sendFrame(nullptr, m.peer, h, m.data, true);
        m.sentAt = now;
        m.sentOnce = true;
        m.due = now + (m.blocked ? 500 : m.rto);
        if (!m.rel) m.used = false;             // fire and forget
        return true;
    }

    // Bulk: a new fragment if the window has room, else a retransmission
    // from the first one not acknowledged, skipping what the SACK says came.
    bool retrans = false;
    if (!(m.next < m.nfrag && m.next < static_cast<uint32_t>(m.cum) + m.win && !m.blocked)) {
        // The timer ran out with nothing new allowed.
        if (!m.blocked && !countTry()) return true;
        m.next = m.cum < m.nfrag ? m.cum : static_cast<uint16_t>(m.nfrag - 1);
        retrans = true;
    }
    uint16_t f = m.next;
    // payload: fragment 0 opens with the preamble
    uint8_t* p = scratch_;
    size_t n;
    if (f == 0) {
        put32(p, m.total);
        put32(p + 4, m.crc);
        size_t take = m.total < kPayloadMax - kPreamble ? m.total : kPayloadMax - kPreamble;
        memcpy(p + kPreamble, m.src, take);
        n = kPreamble + take;
    } else {
        uint32_t off = static_cast<uint32_t>(f) * kPayloadMax - kPreamble;
        uint32_t left = off < m.total ? m.total - off : 0;
        n = left < kPayloadMax ? left : kPayloadMax;
        if (n) memcpy(p, m.src + off, n);
    }
    h.frag  = f;
    h.nfrag = m.nfrag;
    h.len   = static_cast<uint8_t>(n);
    sendFrame(nullptr, m.peer, h, p, true);
    m.sentAt = now;
    if (!m.sentOnce) { m.sentOnce = true; m.tries = 1; }
    // Advance, skipping fragments the receiver already has.
    uint16_t nx = static_cast<uint16_t>(f + 1);
    while (nx < m.nfrag && nx > m.cum && nx - m.cum - 1 < 32 && (m.sack & (1u << (nx - m.cum - 1)))) ++nx;
    m.next = nx;
    if (retrans || m.next >= m.nfrag || m.next >= static_cast<uint32_t>(m.cum) + m.win) m.due = now + (m.blocked ? 500 : m.rto);
    return true;
}

// ===========================================================================
// Receive
// ===========================================================================
bool Engine::openFrame(uint8_t pi, const Header& h, const uint8_t* hdr, const uint8_t* ct,
                       uint8_t* pt, bool& pending) {
    Peer& p = peers_[pi];
    const uint8_t dir = role_ == Role::Host ? DIR_PEER : DIR_HOST;
    const uint8_t* tag = ct + h.len;
    pending = false;
    bool replay = false;
    if (p.haveSess) {
        if (!replayOk(p.rxHigh, p.rxMask, h.pn)) {
            replay = true;                    // seen already, or too old: not worth a decrypt
        } else if (linkcrypto::open(p.kSess, dir, h.pn, hdr, kHdr, ct, h.len, pt, tag)) {
            replayMark(p.rxHigh, p.rxMask, h.pn);
            return true;
        }
    }
    if (p.havePend && replayOk(p.pendHigh, p.pendMask, h.pn) &&
        linkcrypto::open(p.kPend, dir, h.pn, hdr, kHdr, ct, h.len, pt, tag)) {
        replayMark(p.pendHigh, p.pendMask, h.pn);
        pending = true;
        return true;
    }
    drop(replay ? D_REPLAY : D_TAG);
    return false;
}

void Engine::onFrame(const uint8_t* f, size_t n, const Mac& from, int8_t rssi) {
    Header h;
    if (n < kHdr || !unpackHeader(f, n, h)) { drop(D_SHORT); return; }
    if (h.ver != kVersion) { drop(D_VERSION); return; }
    const bool sealed = (h.flags & F_SEC) != 0;
    if (n != kHdr + h.len + (sealed ? kTag : 0) || h.len > kPayloadMax || h.nfrag == 0 || h.frag >= h.nfrag) {
        drop(D_LENGTH);
        return;
    }
    const int pix = peerIndex(from);
    const uint8_t* body = f + kHdr;

    // Pairing confirmations are sealed with the pairing key, before the pair
    // is a peer at all.
    if (sealed && h.family == FAM_LINK && (h.type == T_PAIR_DONE || h.type == T_PAIR_ACK)) {
        Pair& pr = *pair_;
        const uint8_t dir = role_ == Role::Host ? DIR_PEER : DIR_HOST;
        uint8_t pt[kPayloadMax];
        if (role_ == Role::Host && h.type == T_PAIR_ACK && pr.st == PAIR_CONFIRMING && from == pr.info.mac &&
            h.pn == 0 && linkcrypto::open(pr.key, dir, 0, f, kHdr, body, h.len, pt, body + h.len)) {
            int idx = pix >= 0 ? pix : addPeer(from, pr.key, pr.info.kind);
            if (idx < 0) { pr.st = PAIR_NONE; return; }
            memcpy(peers_[idx].kLink, pr.key, sizeof(pr.key));
            peers_[idx].kind = pr.info.kind;
            peers_[idx].haveSess = false;
            peers_[idx].up = false;
            PairInfo who = pr.info;
            memcpy(who.key, pr.key, sizeof(who.key));
            closePairing();                     // wipes the keys and drops any PAIR_DONE still queued
            if (ev_.paired) ev_.paired(ev_.ctx, static_cast<uint8_t>(idx), who);
            linkcrypto::wipe(&who, sizeof(who));
            return;
        }
        if (role_ == Role::Peer && h.type == T_PAIR_DONE && (pr.st == PAIR_WAITDONE || pr.st == PAIR_DONE_) &&
            from == pr.info.mac && h.pn == 0 &&
            linkcrypto::open(pr.key, dir, 0, f, kHdr, body, h.len, pt, body + h.len)) {
            Ctrl* c = nullptr;
            for (uint8_t i = 0; i < kCtrl; ++i) if (!ctrl_[i].used) { c = &ctrl_[i]; break; }
            if (c) {
                *c = Ctrl();
                c->used = true;
                c->mac = from;
                c->type = T_PAIR_ACK;
                c->pairKey = true;
            }
            if (pr.st != PAIR_DONE_) {
                pr.st = PAIR_DONE_;
                pr.resendAt = io_.millis() + 5000;         // stay here while the host takes the ACK
                scanAt_ = 0;
                Peer& hp = peers_[0];
                hp = Peer();
                hp.used = true;
                hp.mac = from;
                memcpy(hp.kLink, pr.key, sizeof(pr.key));
                hp.state = PS_SCAN;
                hostChan_ = io_.channel();
                io_.addPeer(from);
                PairInfo who = pr.info;
                memcpy(who.key, pr.key, sizeof(who.key));
                if (ev_.paired) ev_.paired(ev_.ctx, 0, who);
                linkcrypto::wipe(&who, sizeof(who));
            }
            return;
        }
        drop(D_TAG);
        return;
    }

    if (!sealed) {
        if (!clearType(h.family, h.type)) { drop(D_UNSEALED); return; }
        uint8_t pt[kPayloadMax];
        memcpy(pt, body, h.len);
        uint8_t hdr[kHdr];
        memcpy(hdr, f, kHdr);
        if (frameCrc(hdr, pt, h.len) != h.crc) { drop(D_CRC); return; }
        if (pix >= 0) {
            peers_[pix].st.rssi = rssi;
            peers_[pix].st.channel = h.chan;
        }
        onClear(h, pt, from);
        return;
    }

    if (pix < 0) { drop(D_UNKNOWN_PEER); return; }
    const uint8_t pi = static_cast<uint8_t>(pix);
    uint8_t pt[kPayloadMax];
    bool pending = false;
    if (!openFrame(pi, h, f, body, pt, pending)) { ++peers_[pi].st.drops; return; }
    if (frameCrc(f, pt, h.len) != h.crc) { drop(D_CRC); ++peers_[pi].st.drops; return; }

    Peer& p = peers_[pi];
    if (pending) {
        // A new key has proved itself: it replaces the old one, and what its
        // HELLO said is believed now. The sessions carry on under it (a
        // channel hop is a pause, not a loss), unless the far end has
        // restarted since the last key: then every session it had is gone
        // on its side, and a new one with a reused id must not be taken for
        // a duplicate of the old.
        memcpy(p.kSess, p.kPend, sizeof(p.kSess));
        p.haveSess = true;
        p.havePend = false;
        p.rxHigh = p.pendHigh;
        p.rxMask = p.pendMask;
        p.txPn = 1;
        p.kind = p.pendKind;
        p.bulkWin = p.pendWin;
        linkcrypto::wipe(p.kPend, sizeof(p.kPend));
        if (p.epoch && p.pendEpoch != p.epoch) farRestarted(pi);
        p.epoch = p.pendEpoch;
    }
    p.st.lastHeard = io_.millis();
    p.st.rssi = rssi;
    p.st.channel = h.chan;
    ++p.st.rx;
    if (role_ == Role::Host && !p.up) {
        p.up = true;
        emit(DE_UP, pi, 0, 0, 1);
    }
    if (role_ == Role::Peer && p.state == PS_UP) {
        // Any sealed frame from the host proves it is there: the next PING
        // can wait its full interval.
        pingMiss_ = 0;
        pingAt_ = io_.millis() + kPingMs;
    }
    if (role_ == Role::Peer && h.chan && h.chan != io_.channel()) {
        // The host moved (or we are on a neighbour and heard it through the
        // skirt): go where it says it is.
        io_.setChannel(h.chan);
        hostChan_ = h.chan;
    }
    onSealed(pi, h, pt);
}

void Engine::onClear(const Header& h, const uint8_t* p, const Mac& from) {
    const uint32_t now = io_.millis();
    const int pix = peerIndex(from);
    Pair& pr = *pair_;

    if (role_ == Role::Host) {
        // Not while the host is reconnecting to its router: its channel is
        // then wherever the scan has it, and a peer that trusted a BEACON
        // from there would sit on the wrong channel (bench, 2026-09-26).
        if (!io_.associated()) return;
        switch (h.type) {
            case T_DISCOVER: {
                if (pix < 0) return;
                Peer& pe = peers_[pix];
                if (pe.beaconAt && !reached(now, pe.beaconAt + 1000)) return;
                pe.beaconAt = now;
                uint8_t b[18];
                b[0] = io_.channel();
                b[1] = kVersion;
                putName(b + 2, 16, boardName_);
                queueCtrl(static_cast<uint8_t>(pix), nullptr, T_BEACON, b, sizeof(b), false);
                return;
            }
            case T_HELLO: {
                if (pix < 0 || h.len < kHelloLen + 8) return;
                Peer& pe = peers_[pix];
                uint8_t want[8];
                helloTag(pe.kLink, DIR_PEER, T_HELLO, p, kHelloLen, want);
                if (!tagOk(want, p + kHelloLen)) { drop(D_TAG); return; }
                if (!pe.havePend || memcmp(pe.np, p, 16) != 0) {
                    memcpy(pe.np, p, 16);
                    io_.random(pe.nh, 16);
                    derivePending(pe, pe.np, pe.nh);
                    pe.havePend = true;
                    pe.pendHigh = 0;
                    pe.pendMask = 0;
                }
                // Believed once a frame under its key proves it (onFrame).
                pe.pendKind = p[17];
                pe.pendWin = p[34] ? p[34] : 16;
                pe.pendEpoch = get32(p + 35);
                uint8_t a[kHelloAckLen + 8];
                memcpy(a, pe.np, 16);
                memcpy(a + 16, pe.nh, 16);
                a[32] = bulkWin_;
                put32(a + 33, epoch_);
                helloTag(pe.kLink, DIR_HOST, T_HELLO_ACK, a, kHelloAckLen, a + kHelloAckLen);
                queueCtrl(static_cast<uint8_t>(pix), nullptr, T_HELLO_ACK, a, sizeof(a), false);
                return;
            }
            case T_PAIR_HELLO: {
                // kind, name[16], fw[12], pub[65]
                if (!pr.open || reached(now, pr.until) || h.len < 94) return;
                if (pr.st == PAIR_OFFERING && from == pr.info.mac) {
                    // Still scanning: it has not heard the OFFER on this channel yet.
                    if (!pr.offerAt || reached(now, pr.offerAt + 100)) {
                        pr.offerAt = now;
                        uint8_t o[98];
                        memcpy(o, pr.pubMine, linkcrypto::kPub);
                        memcpy(o + 65, pr.commit, 16);
                        putName(o + 81, 16, boardName_);
                        o[97] = io_.channel();
                        queueCtrl(0xFF, &pr.info.mac, T_PAIR_OFFER, o, sizeof(o), false);
                    }
                    return;
                }
                if (pr.st != PAIR_NONE) return;          // one device at a time
                const uint32_t until = pr.until;
                pr = Pair();
                pr.open = true;
                pr.until = until;
                pr.info.mac = from;
                pr.info.kind = p[0];
                copyName(pr.info.name, sizeof(pr.info.name), p + 1, 16);
                copyName(pr.info.fw, sizeof(pr.info.fw), p + 17, 12);
                memcpy(pr.pubTheirs, p + 29, linkcrypto::kPub);
                pr.st = PAIR_KEYGEN;                     // the runner makes the key pair, the secret, the commitment
                return;
            }
            case T_PAIR_NONCE: {
                // np[16]: the device's nonce, sent only after it had the host's commitment
                if (h.len < 16 || !(from == pr.info.mac)) return;
                if (pr.st == PAIR_OFFERING) {
                    memcpy(pr.np, p, 16);
                    deriveLink(true);
                    pr.st = PAIR_ASKING;
                    pr.asked = false;
                }
                if (pr.st == PAIR_ASKING || pr.st == PAIR_CONFIRMING) {
                    if (memcmp(pr.np, p, 16) != 0) return;
                    queueCtrl(0xFF, &pr.info.mac, T_PAIR_REVEAL, pr.nh, 16, false);
                }
                return;
            }
            default:
                return;
        }
    }

    // Peer
    Peer& hp = peers_[0];
    switch (h.type) {
        case T_BEACON: {
            if (!hp.used || !(from == hp.mac) || h.len < 2) return;
            if (hp.state != PS_SCAN) return;
            // BEACON's channel is the one to trust: a MAC-layer ACK only says
            // some radio heard a frame, never where the host is (bench).
            const uint8_t ch = p[0] ? p[0] : io_.channel();
            if (ch != io_.channel()) io_.setChannel(ch);
            hostChan_ = ch;
            if (ev_.channel) ev_.channel(ev_.ctx, ch);
            hp.state = PS_HELLO;
            helloAt_ = 0;                                  // send HELLO now
            return;
        }
        case T_HELLO_ACK: {
            if (!hp.used || !(from == hp.mac) || h.len < kHelloAckLen + 8) return;
            if (hp.state != PS_HELLO && hp.state != PS_WAITUP) return;
            if (memcmp(p, hp.np, 16) != 0) return;         // not an answer to our HELLO
            // Once only per HELLO: a replayed or duplicated ACK must not put
            // the packet numbers back to 1 under the same key.
            if (hp.ackUsed) return;
            uint8_t want[8];
            helloTag(hp.kLink, DIR_HOST, T_HELLO_ACK, p, kHelloAckLen, want);
            if (!tagOk(want, p + kHelloAckLen)) { drop(D_TAG); return; }
            hp.ackUsed = true;
            uint8_t nh[16];
            memcpy(nh, p + 16, 16);
            derivePending(hp, hp.np, nh);
            memcpy(hp.kSess, hp.kPend, sizeof(hp.kSess));
            linkcrypto::wipe(hp.kPend, sizeof(hp.kPend));
            hp.haveSess = true;
            hp.txPn = 1;
            hp.rxHigh = 0;
            hp.rxMask = 0;
            hp.bulkWin = p[32] ? p[32] : 16;
            // Authentic (its tag) and fresh (it echoes this HELLO's nonce, and
            // only once): if the host's epoch changed, it restarted, and the
            // sessions this end had with it are gone on its side.
            {
                const uint32_t ep = get32(p + 33);
                if (hp.epoch && ep != hp.epoch) farRestarted(0);
                hp.epoch = ep;
            }
            hp.state = PS_WAITUP;
            // Prove the key at once: a PING under it makes the host switch.
            pingAt_ = 0;
            helloAt_ = now;
            return;
        }
        case T_PAIR_OFFER: {
            // pub_h[65], commit[16], name[16], chan
            if (pr.st != PAIR_HELLOING || h.len < 98) return;
            pr.info.mac = from;
            memcpy(pr.pubTheirs, p, linkcrypto::kPub);
            memcpy(pr.commit, p + 65, 16);
            copyName(pr.info.name, sizeof(pr.info.name), p + 81, 16);
            pr.theirChan = p[97];
            if (pr.theirChan && pr.theirChan != io_.channel()) io_.setChannel(pr.theirChan);
            hostChan_ = pr.theirChan ? pr.theirChan : io_.channel();
            pr.st = PAIR_SHARED;                           // pairCompute makes the secret
            return;
        }
        case T_PAIR_REVEAL: {
            // nh[16]: it must be what the host committed to before it saw np
            if (pr.st != PAIR_NONCING || h.len < 16 || !(from == pr.info.mac)) return;
            uint8_t c[48 + linkcrypto::kPub];
            memcpy(c, pr.pubTheirs, linkcrypto::kPub);
            memcpy(c + linkcrypto::kPub, p, 16);
            uint8_t d[32];
            linkcrypto::sha256(c, linkcrypto::kPub + 16, d);
            if (!tagOk(d, pr.commit) || !tagOk(d + 8, pr.commit + 8)) { drop(D_TAG); return; }
            memcpy(pr.nh, p, 16);
            deriveLink(false);
            pr.st = PAIR_WAITDONE;
            // The code to show (on the console, an LED): the host shows the sysop the same.
            if (ev_.pairAsk) ev_.pairAsk(ev_.ctx, pr.info);
            return;
        }
        default:
            return;
    }
}

// deriveLink: k_link and the 4-digit code from the ECDH secret, both nonces
// and both public keys (LINK.md, "Pairing and keys"). Either end, once it
// has np and nh; the secret is wiped here.
void Engine::deriveLink(bool host) {
    Pair& pr = *pair_;
    static const uint8_t kLabel[] = { 'u','n','l','e','a','s','h','e','d',' ','l','i','n','k',' ','1' };
    uint8_t salt[32];
    memcpy(salt, pr.np, 16);
    memcpy(salt + 16, pr.nh, 16);
    uint8_t info[sizeof(kLabel) + 2 * linkcrypto::kPub];
    memcpy(info, kLabel, sizeof(kLabel));
    const uint8_t* pubP = host ? pr.pubTheirs : pr.pubMine;
    const uint8_t* pubH = host ? pr.pubMine : pr.pubTheirs;
    memcpy(info + sizeof(kLabel), pubP, linkcrypto::kPub);
    memcpy(info + sizeof(kLabel) + linkcrypto::kPub, pubH, linkcrypto::kPub);
    uint8_t okm[linkcrypto::kKey + 2];
    linkcrypto::hkdf(salt, sizeof(salt), pr.secret, sizeof(pr.secret), info, sizeof(info), okm, sizeof(okm));
    memcpy(pr.key, okm, linkcrypto::kKey);
    pr.info.code = static_cast<uint16_t>(get16(okm + linkcrypto::kKey) % 10000);
    linkcrypto::wipe(okm, sizeof(okm));
    linkcrypto::wipe(pr.secret, sizeof(pr.secret));
}

void Engine::onSealed(uint8_t pi, const Header& h, const uint8_t* p) {
    if (h.family == FAM_LINK) { onLinkMsg(pi, h, p); return; }
    onFamily(pi, h, p);
}

void Engine::onLinkMsg(uint8_t pi, const Header& h, const uint8_t* p) {
    Peer& pe = peers_[pi];
    const uint32_t now = io_.millis();
    switch (h.type) {
        case T_PING:
            if (role_ != Role::Host) return;
            if (h.len >= 9) {
                pe.st.uptime = get32(p);
                pe.st.heap = get32(p + 4);
                pe.st.farRssi = static_cast<int8_t>(p[8]);
            }
            {
                uint8_t b[5];
                put32(b, io_.unixTime());
                b[4] = io_.channel();
                queueCtrl(pi, nullptr, T_PONG, b, sizeof(b), true);
            }
            return;
        case T_PONG:
            if (role_ != Role::Peer) return;
            pingMiss_ = 0;
            if (pe.state != PS_UP) {
                pe.state = PS_UP;
                pe.up = true;
                pingAt_ = now + kPingMs;
                if (ev_.peerState) ev_.peerState(ev_.ctx, 0, true);
            }
            if (h.len >= 5) {
                uint32_t t = get32(p);
                if (t && ev_.clock) ev_.clock(ev_.ctx, t);
            }
            return;
        case T_ACK:
            onAck(pi, p, h.len);
            return;
        case T_RESET: {
            if (h.len < 3) return;
            if (get16(p) == 0) {
                // Session 0 is the link: the host asks for a new key.
                if (role_ == Role::Peer && p[2] == R_REKEY && pe.state == PS_UP) { pe.state = PS_HELLO; helloAt_ = 0; }
                return;
            }
            Sess* s = findSess(pi, get16(p));
            if (!s) return;
            uint16_t id = s->id;
            uint8_t fam = s->family;
            dropSess(*s);
            emit(DE_RESET, pi, id, fam, p[2]);
            return;
        }
        default:
            return;
    }
}

void Engine::onAck(uint8_t pi, const uint8_t* p, size_t n) {
    if (n < 15) return;
    const uint16_t id  = get16(p);
    const uint16_t cum = get16(p + 2);
    const bool     hasBulk = p[4] & 1;
    const uint16_t bseq = get16(p + 5);
    const uint16_t bcum = get16(p + 7);
    const uint32_t sack = get32(p + 9);
    const uint16_t win  = get16(p + 13);
    Sess* s = findSess(pi, id);
    if (!s) return;
    const uint32_t now = io_.millis();
    s->lastActive = now;
    if (before(s->txAcked, cum)) s->txAcked = cum;
    for (uint8_t i = 0; i < kTxMsgs; ++i) {
        TxMsg& m = tx_[i];
        if (!m.used || m.peer != pi || m.sess != id) continue;
        if (before(m.seq, cum)) {
            if (m.bulk && ev_.bulkSent) {
                m.used = false;
                emit(DE_BULKSENT, pi, id, m.family, 1);
            }
            m.used = false;
            continue;
        }
        if (m.bulk) {
            if (!hasBulk || bseq != m.seq) {
                // The receiver is busy with another bulk message: it says
                // so with no bulk fields and window 0.
                if (win == 0 && m.seq == cum) { m.blocked = true; m.due = now + 500; }
                continue;
            }
            bool progress = before(m.cum, bcum) || sack != m.sack;
            m.cum = bcum;
            m.sack = sack;
            m.win = win;
            m.blocked = (win == 0);
            if (m.next < m.cum) m.next = m.cum;
            if (progress) { m.tries = 1; m.rto = kRtoMs; }
            m.due = now + (m.blocked ? 500 : m.rto);
        } else if (m.seq == cum) {
            // the next message the far end wants: win 0 means wait, not retry
            m.blocked = (win == 0);
            if (m.blocked) m.due = now + 500;
        }
    }
}

void Engine::onFamily(uint8_t pi, const Header& h, const uint8_t* p) {
    const uint32_t now = io_.millis();
    Sess* s = findSess(pi, h.session);
    if (!s) {
        if (Tomb* t = tombOf(pi, h.session, now)) {
            // Closed here lately. A resend of something this end took: say
            // so again (the last ACK was lost). Anything new: the session is
            // over, and the far end hears it.
            drop(D_NOSESSION);
            if (!peers_[pi].haveSess) return;
            if (before(h.seq, t->expect)) {
                uint8_t a[15] = {};
                put16(a, h.session);
                put16(a + 2, t->expect);
                put16(a + 13, kStreamWin);
                queueCtrl(pi, nullptr, T_ACK, a, sizeof(a), true);
            } else {
                uint8_t r[3];
                put16(r, h.session);
                r[2] = R_CLOSED;
                queueCtrl(pi, nullptr, T_RESET, r, sizeof(r), true);
            }
            return;
        }
        const bool farOpened = role_ == Role::Host ? (h.session & 0x8000) != 0 : (h.session & 0x8000) == 0;
        if (farOpened && h.seq != 0 && h.seq < kStreamWin) {
            // The session's first message was lost and a later one got here
            // first. Say nothing: the first comes again and opens it.
            drop(D_NOSESSION);
            return;
        }
        if (!farOpened || h.seq != 0 || h.session == 0) {
            uint8_t r[3];
            put16(r, h.session);
            r[2] = R_UNKNOWN;
            queueCtrl(pi, nullptr, T_RESET, r, sizeof(r), true);
            drop(D_NOSESSION);
            return;
        }
        s = newSess(pi, h.session, h.family);
        if (!s) {
            uint8_t r[3];
            put16(r, h.session);
            r[2] = R_BUSY;
            queueCtrl(pi, nullptr, T_RESET, r, sizeof(r), true);
            drop(D_NOSESSION);
            return;
        }
        s->remote = true;
    }
    s->lastActive = now;

    if (h.nfrag > 1) { onFragment(pi, *s, h, p); return; }

    if (!(h.flags & F_REL)) {
        if (ev_.message) ev_.message(ev_.ctx, pi, s->id, h.family, h.type, p, h.len);
        return;
    }
    if (h.seq == s->rxExpect) {
        bool took = ev_.message ? ev_.message(ev_.ctx, pi, s->id, h.family, h.type, p, h.len) : true;
        // The owner may have closed the session from inside the call. The
        // message was still taken, and the far end must hear so, or it
        // retries into a session this end no longer knows.
        s = findSess(pi, h.session);
        if (!s) {
            if (took && peers_[pi].haveSess) {
                uint8_t a[15] = {};
                put16(a, h.session);
                put16(a + 2, static_cast<uint16_t>(h.seq + 1));
                put16(a + 13, kStreamWin);
                queueCtrl(pi, nullptr, T_ACK, a, sizeof(a), true);
            }
            return;
        }
        if (took) {
            ++s->rxExpect;
            s->refusing = false;
            if (!s->ackDue) { s->ackDue = true; s->ackAt = now + kAckDelayMs; }
        } else {
            s->refusing = true;
            s->ackDue = true;
            s->ackNow = true;
        }
        return;
    }
    // A duplicate (its ack was lost) or one from ahead of a gap: say where
    // this end is, at once, and deliver nothing.
    if (!before(h.seq, s->rxExpect)) drop(D_ORDER);
    s->ackDue = true;
    s->ackNow = true;
}

void Engine::onFragment(uint8_t pi, Sess& s, const Header& h, const uint8_t* p) {
    const uint32_t now = io_.millis();
    BulkRx& b = *bulk_;
    if (before(h.seq, s.rxExpect)) { s.ackDue = true; s.ackNow = true; return; }   // done already
    if (h.seq != s.rxExpect) { drop(D_ORDER); s.ackDue = true; s.ackNow = true; return; }

    uint8_t st = b.st.load();
    if (st == BX_FREE) {
        b.peer = pi;
        b.sess = s.id;
        b.seq = h.seq;
        b.family = h.family;
        b.type = h.type;
        b.nfrag = h.nfrag;
        b.total = 0;
        b.crcWant = 0;
        b.crcRun = 0;
        b.got = 0;
        b.ackedAt = 0;
        b.sinceAck = 0;
        b.consumed.store(0);
        b.have.store(0);
        b.st.store(BX_RECV);
        st = BX_RECV;
    } else if (b.peer != pi || b.sess != s.id || b.seq != h.seq) {
        // Another bulk message has the window: tell this sender to wait.
        drop(D_WINDOW);
        uint8_t a[15] = {};
        put16(a, s.id);
        put16(a + 2, s.rxExpect);
        queueCtrl(pi, nullptr, T_ACK, a, sizeof(a), true);
        return;
    }
    if (st != BX_RECV && st != BX_BEGUN) return;          // finishing or cancelled
    if (h.nfrag != b.nfrag) { drop(D_LENGTH); return; }

    // consumed and have, read as one: pumpBulk moves consumed and then clears
    // the slot's bit, so the pair is only consistent if consumed did not move
    // while have was read. Otherwise a late duplicate of the fragment just
    // taken could be written into its old slot and read later as the
    // fragment a window further on.
    uint16_t consumed;
    uint64_t have;
    do {
        consumed = b.consumed.load();
        have = b.have.load();
    } while (b.consumed.load() != consumed);
    if (h.frag < consumed) { s.ackDue = true; s.ackNow = true; return; }
    if (h.frag >= consumed + bulkWin_) { drop(D_WINDOW); s.ackDue = true; s.ackNow = true; return; }
    const uint8_t slot = static_cast<uint8_t>(h.frag % bulkWin_);
    const uint64_t bit = 1ull << slot;
    if (have & bit) { s.ackDue = true; s.ackNow = true; return; }

    if (h.frag == 0) {
        if (h.len < kPreamble) { drop(D_LENGTH); return; }
        const uint32_t total = get32(p);
        const uint32_t frags = bulkFrags(total);
        bool yes = total <= kBulkMax && frags == h.nfrag;
        b.total = total;
        b.crcWant = get32(p + 4);
        if (yes) yes = ev_.bulkBegin ? ev_.bulkBegin(ev_.ctx, pi, s.id, h.family, h.type, total) : false;
        if (!yes) {
            // On the runner, the loop tells the far end and the owner.
            if (onRunner_) b.st.store(BX_REFUSED);
            else { b.st.store(BX_FREE); failSess(s, R_REFUSED, true); }
            return;
        }
    }
    memcpy(bulkMem_ + static_cast<size_t>(slot) * kPayloadMax, p, h.len);
    b.lens[slot] = h.len;
    b.have.fetch_or(bit);
    if (h.frag == 0) b.st.store(BX_BEGUN);

    ++b.sinceAck;
    s.ackDue = true;
    if (b.sinceAck >= bulkWin_ / 2 || h.frag + 1 == h.nfrag) s.ackNow = true;
    else if (!s.ackNow) s.ackAt = now + kAckDelayMs;
}

// ===========================================================================
// The runner's side
// ===========================================================================
bool Engine::pumpRx(uint16_t maxFrames) {
    if (!ok()) return false;
    bool any = false;
    for (uint16_t i = 0; i < maxFrames; ++i) {
        uint8_t f[kFrameMax];
        Mac from;
        int8_t rssi = 0;
        size_t n = io_.recvBulk(f, sizeof(f), from, rssi);
        if (!n) break;
        any = true;
        Guard g(io_);
        // Only a family's fragments belong here. The radio sorted them by a
        // header nobody has authenticated yet, so anything else is dropped.
        Header h;
        if (n < kHdr || !unpackHeader(f, n, h) || h.nfrag < 2 || h.family == FAM_LINK) {
            drop(D_LENGTH);
            continue;
        }
        const uint32_t t0 = io_.micros();
        onRunner_ = true;
        onFrame(f, n, from, rssi);
        onRunner_ = false;
        const uint32_t us = io_.micros() - t0;
        if (us > usMax_) usMax_ = us;
    }
    if (any) {
        Guard g(io_);
        flushAcks();
    }
    return any;
}

// flushAcks: the acknowledgements due now, sent from here rather than at
// the loop's next tick. On the runner a bulk sender then hears within a
// frame's time, not up to 20 ms later: with a window of 16 fragments that
// wait, not the radio, was the transfer's speed (camsat bench, 2026-09-26:
// 17 KB/s through the loop). Under the lock.
void Engine::flushAcks() {
    for (uint8_t i = 0; i < kSessions; ++i) {
        Sess& s = sess_[i];
        if (!s.used || !s.ackDue || !s.ackNow) continue;
        if (!peers_[s.peer].haveSess) continue;
        if (!io_.idle()) return;
        uint8_t p[16];
        size_t n = 0;
        buildAck(s, p, n);
        Header h;
        h.family = FAM_LINK;
        h.type = T_ACK;
        h.len = static_cast<uint8_t>(n);
        s.ackDue = false;
        s.ackNow = false;
        sendFrame(nullptr, s.peer, h, p, true);
    }
}

bool Engine::bulkWaiting() const {
    const BulkRx& b = *bulk_;
    if (b.st.load() != BX_BEGUN) return false;
    const uint16_t c = b.consumed.load();
    return c < b.nfrag && (b.have.load() & (1ull << (c % bulkWin_)));
}

bool Engine::pumpBulk(uint16_t maxFrags) {
    BulkRx& b = *bulk_;
    pumping_.store(true);
    uint16_t done = 0;
    bool more = false;
    while (done < maxFrags) {
        if (b.st.load() != BX_BEGUN) break;
        const uint16_t c = b.consumed.load();
        if (c >= b.nfrag) break;
        const uint8_t slot = static_cast<uint8_t>(c % bulkWin_);
        const uint64_t bit = 1ull << slot;
        if (!(b.have.load() & bit)) break;
        const uint8_t* d = bulkMem_ + static_cast<size_t>(slot) * kPayloadMax;
        size_t n = b.lens[slot];
        if (c == 0) { d += kPreamble; n = n > kPreamble ? n - kPreamble : 0; }
        if (b.got + n > b.total) n = b.total - b.got;
        b.crcRun = crc32::update(b.crcRun, d, n);
        b.got += static_cast<uint32_t>(n);
        bool ok = ev_.bulkData ? ev_.bulkData(ev_.ctx, b.peer, b.sess, b.family, d, n) : true;
        // consumed first, then the slot's bit: the other order lets the loop,
        // still reading the old consumed, take a late duplicate of fragment c
        // into the slot and mark it, where fragment c + window then finds it.
        b.consumed.store(static_cast<uint16_t>(c + 1));
        b.have.fetch_and(~bit);
        ++done;
        // State changes by compare-and-swap from what the runner expects: the
        // loop may have cancelled the message meanwhile (the session was
        // reset), and a plain store would put an OK on top of that.
        if (!ok) { uint8_t e = BX_BEGUN; b.st.compare_exchange_strong(e, BX_ABORT); break; }
        if (c + 1 == b.nfrag) {
            uint8_t e = BX_BEGUN;
            if (!b.st.compare_exchange_strong(e, BX_FINISHING)) break;    // cancelled
            const bool whole = b.got == b.total && b.crcRun == b.crcWant;
            // Still on the runner: the sink finishes its card work here (a
            // picture's rename into Photos), never on the loop.
            if (ev_.bulkFinish) ev_.bulkFinish(ev_.ctx, b.peer, b.sess, b.family, whole);
            e = BX_FINISHING;
            b.st.compare_exchange_strong(e, whole ? BX_OK : BX_BAD);
            break;
        }
    }
    // Room made: half a window taken since the last ACK told the sender, so
    // tell it now rather than at the loop's next tick.
    if (done && b.st.load() == BX_BEGUN) {
        Guard g(io_);                                   // ackedAt is the lock's
        if (b.st.load() == BX_BEGUN && static_cast<uint16_t>(b.consumed.load() - b.ackedAt) >= bulkWin_ / 2) {
            if (Sess* s = findSess(b.peer, b.sess)) {
                s->ackDue = true;
                s->ackNow = true;
                flushAcks();
            }
        }
    }
    more = bulkWaiting();
    pumping_.store(false);
    return more;
}

// bulkEndCheck (loop): a message the runner finished, or one cancelled, is
// handed back here, where the owner may be told and the session moved on.
void Engine::bulkEndCheck() {
    BulkRx& b = *bulk_;
    const uint8_t st = b.st.load();
    if (st == BX_REFUSED) {
        // pumpRx refused a message on the runner: say so from here.
        const uint8_t pi = b.peer;
        const uint16_t sid = b.sess;
        b.st.store(BX_FREE);
        if (Sess* s = findSess(pi, sid)) failSess(*s, R_REFUSED, true);
        return;
    }
    if (st == BX_CANCEL) {
        if (pumping_.load()) return;
        uint8_t pi = b.peer;
        uint16_t sid = b.sess;
        uint8_t fam = b.family;
        b.st.store(BX_FREE);
        if (ev_.bulkEnd) ev_.bulkEnd(ev_.ctx, pi, sid, fam, false);
        return;
    }
    if (st != BX_OK && st != BX_BAD && st != BX_ABORT) {
        // Tell a sender held by the window that the runner made room.
        if (st == BX_BEGUN) {
            const uint16_t c = b.consumed.load();
            if (static_cast<uint16_t>(c - b.ackedAt) >= bulkWin_ / 2) {
                Sess* s = findSess(b.peer, b.sess);
                if (s) { s->ackDue = true; s->ackNow = true; }
            }
        }
        return;
    }
    if (pumping_.load()) return;
    const uint8_t pi = b.peer;
    const uint16_t sid = b.sess;
    const uint8_t fam = b.family;
    b.st.store(BX_FREE);
    if (ev_.bulkEnd) ev_.bulkEnd(ev_.ctx, pi, sid, fam, st == BX_OK);
    Sess* s = findSess(pi, sid);
    if (!s) return;
    if (st == BX_OK) {
        ++s->rxExpect;
        s->ackDue = true;
        s->ackNow = true;
    } else {
        failSess(*s, st == BX_BAD ? R_BADCRC : R_ABORTED, true);
    }
}

// ===========================================================================
// Timers
// ===========================================================================
void Engine::timers(uint32_t now) {
    if (role_ == Role::Host) hostTimers(now);
    else                     peerTimers(now);
    bulkEndCheck();
    // Sessions closed with closeAfter go once their last message is taken,
    // leaving a tombstone: our last ACK may have been lost, and the far
    // end's resend has to be acknowledged again, not answered "no such
    // session" nor taken as a new session's first message (camsat bench,
    // 2026-09-26: a picture filed here and reported failed there).
    for (uint8_t k = 0; k < kSessions; ++k) {
        Sess& s = sess_[k];
        if (!s.used || !s.closing || s.ackDue || inFlight(s)) continue;
        bury(s, now);
        dropSess(s);
    }
    // And the far end is told, a little later, so its side of the session
    // does not sit in its table until it idles out: a peer that does not
    // close its own filled its 16 after 16 pictures (camsat bench). Late
    // enough that a lost last ACK has been resent and answered first.
    for (Tomb& t : tombs_) {
        if (!t.used || t.told || !reached(now, t.at + kTombTellMs)) continue;
        if (!peers_[t.peer].haveSess) { t.told = true; continue; }
        uint8_t r[3];
        put16(r, t.id);
        r[2] = R_CLOSED;
        if (queueCtrl(t.peer, nullptr, T_RESET, r, sizeof(r), true)) t.told = true;
    }
}

void Engine::hostTimers(uint32_t now) {
    for (uint8_t i = 0; i < npeers_; ++i) {
        Peer& p = peers_[i];
        if (!p.used) continue;
        if (p.up && reached(now, p.st.lastHeard + kHostQuietMs)) peerDown(i);
        // A peer gone for a minute takes its sessions with it.
        if (!p.up && p.downSince && reached(now, p.downSince + 60000)) {
            for (uint8_t k = 0; k < kSessions; ++k)
                if (sess_[k].used && sess_[k].peer == i) failSess(sess_[k], R_RETRIES, false);
            p.downSince = 0;
        }
        // Only a peer starts a HELLO, so the host asks for one long before
        // its own packet numbers could wrap under the key.
        if (p.haveSess && !p.havePend && p.txPn > 0x7FFFFF00u && reached(now, p.rekeyAt)) {
            p.rekeyAt = now + 1000;
            uint8_t r[3] = { 0, 0, R_REKEY };
            queueCtrl(i, nullptr, T_RESET, r, sizeof(r), true);
        }
    }
    // Sessions the far end opened and then went quiet on: their owners are
    // told, as for any session that ends without them.
    for (uint8_t k = 0; k < kSessions; ++k) {
        Sess& s = sess_[k];
        if (s.used && s.remote && !s.closing && !inFlight(s) && reached(now, s.lastActive + kSessIdleMs))
            failSess(s, R_CLOSED, false);
    }
    // Pairing.
    Pair& pr = *pair_;
    if (pr.open && reached(now, pr.until)) { closePairing(); return; }
    if (pr.st == PAIR_ASKING && !pr.asked) {
        pr.asked = true;
        if (ev_.pairAsk) ev_.pairAsk(ev_.ctx, pr.info);
    }
    if (pr.st == PAIR_CONFIRMING && reached(now, pr.resendAt)) {
        pr.resendAt = now + 100;
        for (uint8_t i = 0; i < kCtrl; ++i) {
            Ctrl& c = ctrl_[i];
            if (c.used) continue;
            c = Ctrl();
            c.used = true;
            c.mac = pr.info.mac;
            c.type = T_PAIR_DONE;
            c.pairKey = true;
            break;
        }
    }
}

void Engine::peerTimers(uint32_t now) {
    Peer& hp = peers_[0];
    Pair& pr = *pair_;

    // Pairing: PAIR_HELLO on each channel in turn, a quarter second each,
    // until a host offers.
    if (pr.st == PAIR_HELLOING) {
        if (!scanAt_ || reached(now, scanAt_)) {
            scanCh_ = static_cast<uint8_t>(scanCh_ % 13 + 1);
            io_.setChannel(scanCh_);
            scanAt_ = now + 250;
            uint8_t b[94];
            b[0] = pr.kind;
            putName(b + 1, 16, pr.name);
            putName(b + 17, 12, pr.fw);
            memcpy(b + 29, pr.pubMine, linkcrypto::kPub);
            queueCtrl(0xFF, nullptr, T_PAIR_HELLO, b, sizeof(b), false);
        }
        return;
    }
    // Our nonce, until the host reveals its own.
    if (pr.st == PAIR_NONCING) {
        if (!pr.resendAt || reached(now, pr.resendAt)) {
            pr.resendAt = now + 250;
            queueCtrl(0xFF, &pr.info.mac, T_PAIR_NONCE, pr.np, 16, false);
        }
        return;
    }
    if (pr.st == PAIR_SHARED || pr.st == PAIR_COMPUTING || pr.st == PAIR_WAITDONE) return;
    if (!hp.used) return;

    switch (hp.state) {
        case PS_IDLE:
            hp.state = PS_SCAN;
            scanAt_ = 0;
            // fallthrough
        case PS_SCAN:
            // Just paired: the host is on this channel, and it has to hear
            // our PAIR_ACK before it will answer a DISCOVER. Stay and ask
            // here for a few seconds rather than wander off scanning.
            if (pr.st == PAIR_DONE_) {
                if (!reached(now, pr.resendAt)) {
                    if (!scanAt_ || reached(now, scanAt_)) {
                        scanAt_ = now + 100;
                        queueCtrl(0xFF, nullptr, T_DISCOVER, nullptr, 0, false);
                    }
                    return;
                }
                closePairing();
                scanAt_ = 0;
            }
            if (!scanAt_ || reached(now, scanAt_)) {
                // The last channel that worked first, then 1 to 13.
                if (!scanAt_ && hostChan_) scanCh_ = hostChan_;
                else scanCh_ = static_cast<uint8_t>(scanCh_ % 13 + 1);
                io_.setChannel(scanCh_);
                scanAt_ = now + kDwellMs;
                queueCtrl(0xFF, nullptr, T_DISCOVER, nullptr, 0, false);
            }
            return;
        case PS_HELLO:
        case PS_WAITUP:
            if (!helloAt_ || reached(now, helloAt_ + 1000)) {
                if (helloAt_ && ++pingMiss_ > 3) { pingMiss_ = 0; hp.state = PS_SCAN; scanAt_ = 0; return; }
                io_.random(hp.np, 16);
                hp.ackUsed = false;
                hp.state = PS_HELLO;
                hp.haveSess = false;
                uint8_t b[kHelloLen + 8];
                memcpy(b, hp.np, 16);
                b[16] = kVersion;
                b[17] = kind_;
                putName(b + 18, 12, fw_);
                put32(b + 30, families_);
                b[34] = bulkWin_;
                put32(b + 35, epoch_);
                helloTag(hp.kLink, DIR_PEER, T_HELLO, b, kHelloLen, b + kHelloLen);
                queueCtrl(0, nullptr, T_HELLO, b, sizeof(b), false);
                helloAt_ = now;
            }
            if (hp.state == PS_WAITUP && (!pingAt_ || reached(now, pingAt_))) {
                pingAt_ = now + 300;
                uint8_t b[9];
                put32(b, now / 1000);
                put32(b + 4, io_.heapFree());
                b[8] = static_cast<uint8_t>(hp.st.rssi);
                queueCtrl(0, nullptr, T_PING, b, sizeof(b), true);
            }
            return;
        case PS_UP:
            // A sleeping sender does not wait out PING's misses: three MAC
            // failures in a row and it looks for the host at once (bench,
            // 2026-09-26). The MAC ACK only says a radio heard it, never that
            // the host is still there, so success proves nothing either way.
            if (fastRescan_ && io_.macFailStreak() >= 3) { pingMiss_ = 0; peerDown(0); return; }
            if (reached(now, pingAt_)) {
                if (pingMiss_ >= kPingMisses) { pingMiss_ = 0; peerDown(0); return; }
                // Unanswered, the next one goes after a second, not five.
                pingAt_ = now + 1000;
                ++pingMiss_;
                uint8_t b[9];
                put32(b, now / 1000);
                put32(b + 4, io_.heapFree());
                b[8] = static_cast<uint8_t>(hp.st.rssi);
                queueCtrl(0, nullptr, T_PING, b, sizeof(b), true);
            }
            if (hp.txPn > 0x7FFFFFFFu) { hp.state = PS_HELLO; helloAt_ = 0; }   // rekey long before a wrap
            return;
        default:
            return;
    }
}

// ===========================================================================
// Public
// ===========================================================================
void Engine::poll() {
    if (!ok()) return;
    const uint32_t now = io_.millis();

    // Received frames, a bounded number (Rule no. 1), each under the lock on
    // its own so pumpRx on the runner waits for one frame at most.
    for (uint8_t i = 0; i < kFramesPerPoll; ++i) {
        Mac from;
        int8_t rssi = 0;
        uint8_t f[kFrameMax];
        size_t n = io_.recv(f, sizeof(f), from, rssi);
        if (!n) break;
        Guard g(io_);
        const uint32_t t0 = io_.micros();
        onFrame(f, n, from, rssi);
        const uint32_t us = io_.micros() - t0;
        usAvg_ = usAvg_ ? (usAvg_ * 7 + us) / 8 : us;
        if (us > usMax_) usMax_ = us;
    }

    Guard g(io_);
    // What pumpRx found on the runner, told here, on the loop, in order.
    // Copied out first: a handler may send, and a send may defer nothing
    // here, but it must not find the list half walked.
    Deferred todo[kDeferred];
    const uint8_t nd = ndeferred_;
    memcpy(todo, deferred_, sizeof(Deferred) * nd);
    ndeferred_ = 0;
    for (uint8_t i = 0; i < nd; ++i) emit(todo[i].kind, todo[i].peer, todo[i].sess, todo[i].family, todo[i].v);

    timers(now);

    // Up to four frames at the radio at once (Io::idle says when it will take
    // another: 4 outstanding at the MAC was +53% at 24 Mbps on the bench).
    for (uint8_t i = 0; i < 4 && io_.idle(); ++i)
        if (!transmitOne()) break;
}

int Engine::addPeer(const Mac& mac, const uint8_t key[linkcrypto::kKey], uint8_t kind) {
    if (!ok()) return -1;
    Guard g(io_);
    int at = peerIndex(mac);
    if (at < 0) for (uint8_t i = 0; i < npeers_; ++i) if (!peers_[i].used) { at = i; break; }
    if (at < 0) return -1;
    Peer& p = peers_[at];
    p = Peer();
    p.used = true;
    p.mac = mac;
    p.kind = kind;
    memcpy(p.kLink, key, sizeof(p.kLink));
    p.state = PS_SCAN;
    io_.addPeer(mac);
    return at;
}

void Engine::removePeer(uint8_t pi) {
    Guard g(io_);
    if (pi >= npeers_ || !peers_[pi].used) return;
    for (uint8_t k = 0; k < kSessions; ++k)
        if (sess_[k].used && sess_[k].peer == pi) failSess(sess_[k], R_CLOSED, false);
    for (Tomb& t : tombs_) if (t.used && t.peer == pi) t.used = false;
    if (peers_[pi].up && ev_.peerState) ev_.peerState(ev_.ctx, pi, false);
    io_.delPeer(peers_[pi].mac);
    linkcrypto::wipe(&peers_[pi], sizeof(Peer));
    peers_[pi] = Peer();
}

bool Engine::peerUsed(uint8_t pi) const { return pi < npeers_ && peers_[pi].used; }
bool Engine::peerUp(uint8_t pi) const { return pi < npeers_ && peers_[pi].used && peers_[pi].up; }
uint8_t Engine::peerKind(uint8_t pi) const { return pi < npeers_ ? peers_[pi].kind : static_cast<uint8_t>(KIND_UNKNOWN); }
const Mac& Engine::peerMac(uint8_t pi) const { static const Mac z; return pi < npeers_ ? peers_[pi].mac : z; }
const PeerStats& Engine::peerStats(uint8_t pi) const { static const PeerStats z; return pi < npeers_ ? peers_[pi].st : z; }
const uint8_t* Engine::peerKey(uint8_t pi) const {
    static const uint8_t z[linkcrypto::kKey] = {};
    return pi < npeers_ ? peers_[pi].kLink : z;
}
uint8_t Engine::peerCount() const { uint8_t n = 0; for (uint8_t i = 0; i < npeers_; ++i) n += peers_[i].used; return n; }
uint8_t Engine::peersUp() const { uint8_t n = 0; for (uint8_t i = 0; i < npeers_; ++i) n += peers_[i].used && peers_[i].up; return n; }
bool Engine::hostUp() const { return role_ == Role::Peer && peers_[0].used && peers_[0].up; }

int Engine::peerIndex(const Mac& mac) const {
    for (uint8_t i = 0; i < npeers_; ++i) if (peers_[i].used && peers_[i].mac == mac) return i;
    return -1;
}

void Engine::setBoardName(const char* name) {
    Guard g(io_);
    snprintf(boardName_, sizeof(boardName_), "%s", name ? name : "");
}

void Engine::openPairing(uint32_t ms) {
    Guard g(io_);
    closePairing();
    Pair& pr = *pair_;
    pr.open = true;
    pr.until = io_.millis() + ms;
    pairUntil_ = pr.until;
}

void Engine::closePairing() {
    Guard g(io_);
    Pair& pr = *pair_;
    // A PAIR_DONE still queued would go out sealed with the wiped, all-zero
    // key: take it back with the rest.
    for (uint8_t i = 0; i < kCtrl; ++i) if (ctrl_[i].used && ctrl_[i].pairKey) ctrl_[i].used = false;
    linkcrypto::wipe(&pr, sizeof(pr));
    pr = Pair();
}

bool Engine::pairingOpen() const { return pair_->open; }

void Engine::pairAnswer(bool yes) {
    Guard g(io_);
    Pair& pr = *pair_;
    if (pr.st != PAIR_ASKING) return;
    if (!yes) { closePairing(); return; }
    pr.st = PAIR_CONFIRMING;
    pr.resendAt = 0;
    // The Y gives the device time to finish whatever was left of the window:
    // it pairs on PAIR_DONE, and a window closing before its PAIR_ACK came
    // would leave it paired and the board not.
    const uint32_t now = io_.millis();
    if (!reached(pr.until, now + 30000)) pr.until = now + 30000;
}

bool Engine::pairComputeWanted() const {
    return pair_->st == PAIR_KEYGEN || pair_->st == PAIR_SHARED;
}

namespace {
int rngThunk(void* ctx, unsigned char* out, size_t n) {
    static_cast<Io*>(ctx)->random(out, n);
    return 0;
}
}  // namespace

// The slow part of pairing, in three steps so the middle one can run on
// another task: take copies what it needs out of the engine (loop), run
// computes with nothing of the engine's (runner), give puts the answer back
// (loop). The engine's pairing state is only ever touched by the loop.
//
//   host (KEYGEN): its key pair, the ECDH secret, its nonce nh and the
//                  commitment to it, SHA-256(pub_h || nh), made before it has
//                  seen the device's nonce;
//   peer (SHARED): the ECDH secret, and its nonce np, chosen after it has
//                  the host's commitment.
bool Engine::pairTake(PairJob& j) {
    Guard g(io_);
    Pair& pr = *pair_;
    if (pr.st != PAIR_KEYGEN && pr.st != PAIR_SHARED) return false;
    j = PairJob();
    j.keygen = pr.st == PAIR_KEYGEN;
    if (j.keygen) io_.random(pr.nh, 16);
    else          io_.random(pr.np, 16);
    memcpy(j.nh, pr.nh, 16);
    memcpy(j.priv, pr.priv, sizeof(j.priv));
    memcpy(j.pubTheirs, pr.pubTheirs, sizeof(j.pubTheirs));
    j.from = pr.st;
    pr.st = PAIR_COMPUTING;
    return true;
}

void Engine::pairRun(PairJob& j, linkcrypto::Rng rng, void* rctx) {
    j.ok = false;
    if (j.keygen && !linkcrypto::keypair(rng, rctx, j.priv, j.pubMine)) return;
    if (!linkcrypto::shared(rng, rctx, j.priv, j.pubTheirs, j.secret)) return;
    if (j.keygen) {
        uint8_t c[linkcrypto::kPub + 16];
        memcpy(c, j.pubMine, linkcrypto::kPub);
        memcpy(c + linkcrypto::kPub, j.nh, 16);
        uint8_t d[32];
        linkcrypto::sha256(c, sizeof(c), d);
        memcpy(j.commit, d, 16);
    }
    linkcrypto::wipe(j.priv, sizeof(j.priv));
    j.ok = true;
}

void Engine::pairGive(PairJob& j) {
    Guard g(io_);
    Pair& pr = *pair_;
    if (pr.st != PAIR_COMPUTING) { linkcrypto::wipe(&j, sizeof(j)); return; }   // closed meanwhile
    if (!j.ok) {
        if (j.from == PAIR_KEYGEN) closePairing();
        else                       pr.st = PAIR_HELLOING;
        linkcrypto::wipe(&j, sizeof(j));
        return;
    }
    memcpy(pr.secret, j.secret, sizeof(pr.secret));
    linkcrypto::wipe(pr.priv, sizeof(pr.priv));
    if (j.from == PAIR_KEYGEN) {
        memcpy(pr.pubMine, j.pubMine, sizeof(pr.pubMine));
        memcpy(pr.commit, j.commit, sizeof(pr.commit));
        pr.st = PAIR_OFFERING;                 // OFFER on its next PAIR_HELLO
        pr.offerAt = 0;
    } else {
        pr.st = PAIR_NONCING;                  // PAIR_NONCE until the REVEAL
        pr.resendAt = 0;
    }
    linkcrypto::wipe(&j, sizeof(j));
}

bool Engine::pairCompute() {
    PairJob j;
    if (!pairTake(j)) return false;
    pairRun(j, rngThunk, &io_);
    pairGive(j);
    return true;
}

void Engine::startPairing(uint8_t kind, const char* name, const char* fw) {
    Guard g(io_);
    closePairing();
    Pair& pr = *pair_;
    pr.kind = kind;
    snprintf(pr.name, sizeof(pr.name), "%s", name ? name : "");
    snprintf(pr.fw, sizeof(pr.fw), "%s", fw ? fw : "");
    if (!linkcrypto::keypair(rngThunk, &io_, pr.priv, pr.pubMine)) return;
    pr.st = PAIR_HELLOING;
    scanAt_ = 0;
}

void Engine::stopPairing() { closePairing(); }

void Engine::setIdentity(uint8_t kind, const char* fw, uint32_t families) {
    Guard g(io_);
    kind_ = kind;
    snprintf(fw_, sizeof(fw_), "%s", fw ? fw : "");
    families_ = families;
}

uint16_t Engine::openSession(uint8_t pi, uint8_t family) {
    Guard g(io_);
    if (!ok() || pi >= npeers_ || !peers_[pi].used) return 0;
    for (int tries = 0; tries < 0x7FFF; ++tries) {
        uint16_t id = nextSess_;
        if (role_ == Role::Host) nextSess_ = static_cast<uint16_t>(nextSess_ >= 0x7FFF ? 1 : nextSess_ + 1);
        else                     nextSess_ = static_cast<uint16_t>(nextSess_ >= 0xFFFF ? 0x8001 : nextSess_ + 1);
        if (findSess(pi, id)) continue;
        Sess* s = newSess(pi, id, family);
        return s ? id : 0;
    }
    return 0;
}

void Engine::closeSession(uint8_t pi, uint16_t id) {
    Guard g(io_);
    Sess* s = findSess(pi, id);
    if (s) dropSess(*s);
}

void Engine::closeAfter(uint8_t pi, uint16_t id) {
    Guard g(io_);
    Sess* s = findSess(pi, id);
    if (!s) return;
    // Never dropped here, even with nothing in flight: an ACK this end owes
    // (the one for the message being handled right now, when this is called
    // from its event) has still to go, and poll() drops the session after it,
    // leaving a tombstone (camsat bench, 2026-09-26).
    s->closing = true;
}

void Engine::resetSession(uint8_t pi, uint16_t id, uint8_t reason) {
    Guard g(io_);
    Sess* s = findSess(pi, id);
    if (!s) return;
    if (peers_[pi].haveSess) {
        uint8_t p[3];
        put16(p, id);
        p[2] = reason;
        queueCtrl(pi, nullptr, T_RESET, p, sizeof(p), true);
    }
    dropSess(*s);
}

bool Engine::canSend(uint8_t pi, uint16_t id) const {
    Guard g(io_);
    const Sess* s = findSess(pi, id);
    if (!s) return false;
    uint8_t n = 0;
    bool freeSlot = false;
    for (uint8_t i = 0; i < kTxMsgs; ++i) {
        if (!tx_[i].used) freeSlot = true;
        else if (tx_[i].peer == pi && tx_[i].sess == id) ++n;
    }
    return freeSlot && n < kStreamWin;
}

uint8_t Engine::sessionFamily(uint8_t pi, uint16_t id) const {
    Guard g(io_);
    const Sess* s = findSess(pi, id);
    return s ? s->family : 0xFF;
}

int Engine::send(uint8_t pi, uint16_t id, uint8_t family, uint8_t type, const void* p, size_t n, bool reliable) {
    Guard g(io_);
    if (!ok() || n > kPayloadMax || family == FAM_LINK) return -1;
    Sess* s = findSess(pi, id);
    if (!s || s->closing) return -1;
    if (!canSend(pi, id)) return 0;
    for (uint8_t i = 0; i < kTxMsgs; ++i) {
        TxMsg& m = tx_[i];
        if (m.used) continue;
        m = TxMsg();
        m.used = true;
        m.peer = pi;
        m.sess = id;
        m.seq = reliable ? s->txSeq++ : s->txSeq;
        m.family = family;
        m.type = type;
        m.rel = reliable;
        m.len = static_cast<uint8_t>(n);
        if (n) memcpy(m.data, p, n);
        s->lastActive = io_.millis();
        return 1;
    }
    return 0;
}

int Engine::sendBulk(uint8_t pi, uint16_t id, uint8_t family, uint8_t type, const uint8_t* data, uint32_t n) {
    Guard g(io_);
    if (!ok() || n > kBulkMax || family == FAM_LINK || (!data && n)) return -1;
    Sess* s = findSess(pi, id);
    if (!s || s->closing) return -1;
    if (!canSend(pi, id)) return 0;
    for (uint8_t i = 0; i < kTxMsgs; ++i) {
        TxMsg& m = tx_[i];
        if (m.used) continue;
        m = TxMsg();
        m.used = true;
        m.peer = pi;
        m.sess = id;
        m.seq = s->txSeq++;
        m.family = family;
        m.type = type;
        m.bulk = true;
        m.src = data;
        m.total = n;
        m.crc = crc32::update(0, data, n);
        m.nfrag = static_cast<uint16_t>(bulkFrags(n));
        m.win = peers_[pi].bulkWin ? peers_[pi].bulkWin : 16;
        s->lastActive = io_.millis();
        return 1;
    }
    return 0;
}

}  // namespace ulink
