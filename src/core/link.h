// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/link.h
// Module:       Core / the µnleashed link's protocol engine (1.2.0)
//
// Purpose:      One end of the µnleashed link: the host (the BBS) or a peer
//               (a camera satellite, a door box). Framing, sealing, the
//               replay window, pairing, the session key, heartbeats,
//               reliable messages, fragmented bulk messages with a window,
//               and on a peer the channel scan. LINK.md is the
//               specification; this is it in code.
//
//               It knows nothing of the BBS, of sessions or of ESP-NOW. The
//               radio is an Io the embedder supplies (the link plugin on the
//               board, a simulated medium in host/test_link.cpp, the
//               satellite's own firmware), and everything the engine learns
//               goes out through Events. So the satellite builds this same
//               file, and the tests drive two engines through a medium that
//               drops, duplicates, reorders and changes channel.
//
//               Where the work runs (Rule no. 1, LINK.md "Where the work
//               runs"): poll() is the loop's, bounded to kFramesPerPoll
//               frames a call; pumpBulk() is the background runner's and is
//               the only function another thread may call. The two share a
//               bulk message's window through atomics and nothing else.
//
//               Memory: an Engine is allocated by its owner when the link
//               starts (heap, or PSRAM where the board has it) and freed
//               when it stops. Nothing here is static and nothing is
//               allocated per frame.
//
// Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1), the Linux host build, and
//               the link's peers
// See also:     LINK.md, src/core/linkcrypto.h, src/plugins/link.cpp
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
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "linkcrypto.h"

namespace ulink {

// ---------------------------------------------------------------------------
// The wire (LINK.md, Frames)
// ---------------------------------------------------------------------------
constexpr uint8_t  kVersion     = 1;
constexpr size_t   kFrameMax    = 250;                          // ESP_NOW_MAX_DATA_LEN on IDF 5.3.1
constexpr size_t   kHdr         = 20;
constexpr size_t   kTag         = linkcrypto::kTag;
constexpr size_t   kPayloadMax  = kFrameMax - kHdr - kTag;      // 222
constexpr size_t   kPreamble    = 8;                            // u32 total, u32 crc32, on fragment 0
constexpr uint32_t kBulkMax     = 512u * 1024u;                 // the largest bulk message anybody takes

enum : uint8_t { FAM_LINK = 0, FAM_CAMERA = 1, FAM_DOOR = 2 };
enum : uint8_t { F_REL = 0x01, F_SEC = 0x02 };
enum : uint8_t {
    T_DISCOVER = 1, T_BEACON, T_HELLO, T_HELLO_ACK, T_PING, T_PONG, T_ACK, T_RESET,
    T_PAIR_HELLO = 16, T_PAIR_OFFER, T_PAIR_DONE, T_PAIR_ACK,
};
enum : uint8_t { DIR_HOST = 'H', DIR_PEER = 'P' };

// RESET reasons, one byte, shown in LINK and the console.
enum : uint8_t {
    R_CLOSED = 1,     // the owner is done with the session
    R_RETRIES,        // eight tries went unanswered
    R_REFUSED,        // the receiver will not take this message (too big, not now)
    R_BUSY,           // no room for another session or bulk message
    R_BADCRC,         // a bulk message's CRC-32 did not check
    R_UNKNOWN,        // a session the receiver does not know
    R_ABORTED,        // the receiver's sink gave up (a card write failed)
};

struct Header {
    uint8_t  ver     = kVersion;
    uint8_t  family  = 0;
    uint8_t  type    = 0;
    uint8_t  flags   = 0;
    uint16_t session = 0;
    uint16_t seq     = 0;
    uint16_t frag    = 0;
    uint16_t nfrag   = 1;
    uint8_t  len     = 0;
    uint8_t  chan    = 0;
    uint16_t crc     = 0;
    uint32_t pn      = 0;
};

void     packHeader(const Header& h, uint8_t out[kHdr]);
bool     unpackHeader(const uint8_t* in, size_t n, Header& h);
// crc16: CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). "123456789" is 0x29B1.
uint16_t crc16(uint16_t crc, const uint8_t* d, size_t n);
// bulkFrags: the fragments a bulk message of total bytes takes, 2 at least.
uint32_t bulkFrags(uint32_t total);
// frameCrc: over header bytes 0-13 and 16-19, then the plaintext payload.
uint16_t frameCrc(const uint8_t hdr[kHdr], const uint8_t* payload, size_t n);
// clearType: may this family/type travel unsealed? Only the six that come
// before a key exists.
bool     clearType(uint8_t family, uint8_t type);

// COBS for the serial transport: encode appends the 0x00 delimiter; decode
// takes a frame without it. Both return 0 when the output does not fit or
// the input is malformed.
size_t cobsEncode(const uint8_t* in, size_t n, uint8_t* out, size_t cap);
size_t cobsDecode(const uint8_t* in, size_t n, uint8_t* out, size_t cap);

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------
struct Mac {
    uint8_t b[6] = {};
    bool operator==(const Mac& o) const;
    bool zero() const;
};

enum class Role : uint8_t { Host, Peer };

// Peer kinds, as HELLO and PAIR_HELLO carry them.
enum : uint8_t { KIND_UNKNOWN = 0, KIND_CAMSAT = 1, KIND_DOORBOX = 2 };
const char* kindName(uint8_t kind);

// What the engine needs from outside. Every call is from the loop, except
// that nothing here is called from pumpBulk().
class Io {
public:
    virtual ~Io() = default;
    // send one frame. to null is broadcast. False when the radio refused it.
    virtual bool send(const Mac* to, const uint8_t* f, size_t n) = 0;
    // idle: the last frame's send callback has come back, so another may go.
    virtual bool idle() = 0;
    // recv: one received frame, or 0 when there is none.
    virtual size_t recv(uint8_t* out, size_t cap, Mac& from, int8_t& rssi) = 0;
    virtual uint32_t millis() = 0;
    virtual uint32_t micros() = 0;
    virtual void random(uint8_t* out, size_t n) = 0;
    // channel: the host's home channel (host), the channel the radio is on (peer)
    virtual uint8_t channel() = 0;
    // setChannel: a peer moving to find its host. Never called on a host.
    virtual void setChannel(uint8_t ch) { (void)ch; }
    // addPeer/delPeer: the radio's own peer table (ESP-NOW needs one for
    // unicast). Unencrypted: the link seals for itself.
    virtual bool addPeer(const Mac& m) { (void)m; return true; }
    virtual void delPeer(const Mac& m) { (void)m; }
    // unixTime: the host's clock for PONG, 0 when it has none (no NTP yet).
    virtual uint32_t unixTime() { return 0; }
    // heapFree: a peer's free heap, reported in PING.
    virtual uint32_t heapFree() { return 0; }
};

// A pairing request as the host sees it, or a pairing as either side stores it.
struct PairInfo {
    Mac      mac;
    uint8_t  kind = KIND_UNKNOWN;
    char     name[17] = {};          // the device's own name for itself
    char     fw[13]   = {};          // its firmware version
    uint16_t code = 0;               // the 4-digit code both sides show
    uint8_t  key[linkcrypto::kKey] = {};   // k_link, only in Events::paired
};

// What the engine tells its owner. ctx is passed back. Every call is from
// poll(), on the loop, except bulkData, which pumpBulk() makes.
struct Events {
    void* ctx = nullptr;
    // A single-frame message, in order. Return false for "no room now": the
    // engine keeps it unaccepted and tells the sender to wait.
    bool (*message)(void* ctx, uint8_t peer, uint16_t sess, uint8_t family, uint8_t type,
                    const uint8_t* p, size_t n) = nullptr;
    // The start of a bulk message (its fragment 0). False refuses it.
    bool (*bulkBegin)(void* ctx, uint8_t peer, uint16_t sess, uint8_t family, uint8_t type,
                      uint32_t total) = nullptr;
    // The next bytes of that message, in order. On the runner. False aborts.
    bool (*bulkData)(void* ctx, uint8_t peer, uint16_t sess, uint8_t family,
                     const uint8_t* p, size_t n) = nullptr;
    // The bulk message is complete: ok when every byte came and the CRC-32 checked.
    void (*bulkEnd)(void* ctx, uint8_t peer, uint16_t sess, uint8_t family, bool ok) = nullptr;
    // The same moment, on the runner and before bulkEnd: for the sink to
    // finish its own slow work (closing and renaming a file) off the loop.
    // Appended; a message that is cancelled gets bulkEnd(false) only.
    void (*bulkFinish)(void* ctx, uint8_t peer, uint16_t sess, uint8_t family, bool ok) = nullptr;
    // A bulk message this end sent has been taken whole (ok) or given up.
    void (*bulkSent)(void* ctx, uint8_t peer, uint16_t sess, uint8_t family, bool ok) = nullptr;
    // A session ended by the far end or by retries running out.
    void (*reset)(void* ctx, uint8_t peer, uint16_t sess, uint8_t family, uint8_t reason) = nullptr;
    // A peer's link session came up or went down.
    void (*peerState)(void* ctx, uint8_t peer, bool up) = nullptr;
    // Host: a device asks to pair (code worked out). The owner shows the
    // sysop and calls pairAnswer.
    void (*pairAsk)(void* ctx, const PairInfo& who) = nullptr;
    // Both: pairing finished. The owner stores who (with its key) and, on a
    // host, the engine has already added it as peer `peer`.
    void (*paired)(void* ctx, uint8_t peer, const PairInfo& who) = nullptr;
    // Peer: the channel it settled on (for its own records and console).
    void (*channel)(void* ctx, uint8_t ch) = nullptr;
    // Peer: the host's clock, from PONG (unix seconds; never 0).
    void (*clock)(void* ctx, uint32_t unix) = nullptr;
};

// Counters for LINK and SYS.
enum : uint8_t {
    D_SHORT, D_VERSION, D_LENGTH, D_UNKNOWN_PEER, D_UNSEALED, D_TAG, D_REPLAY, D_CRC,
    D_ORDER, D_WINDOW, D_NOSESSION, D_RING, D_COUNT
};
const char* dropName(uint8_t d);

struct PeerStats {
    uint32_t rx = 0, tx = 0, retries = 0, drops = 0;
    uint32_t lastHeard = 0;          // millis
    int8_t   rssi = 0;               // as this end hears the far one
    int8_t   farRssi = 0;            // as the far end hears this one (PING)
    uint32_t heap = 0;               // the far end's free heap (PING)
    uint32_t uptime = 0;             // the far end's uptime in seconds (PING)
    uint8_t  channel = 0;            // the channel it was last heard on
};

class Engine {
public:
    // Sizes. A host holds up to kPeers pairings (Rob: 8); a peer holds one.
    static constexpr uint8_t  kPeers         = 8;
    static constexpr uint8_t  kSessions      = 16;
    static constexpr uint8_t  kTxMsgs        = 12;
    static constexpr uint8_t  kCtrl          = 8;
    static constexpr uint8_t  kFramesPerPoll = 8;
    static constexpr uint8_t  kStreamWin     = 4;     // messages in flight per session
    static constexpr uint8_t  kBulkWinMax    = 64;    // fragments, a window's bitmap is 64 bits
    static constexpr uint8_t  kTries         = 8;
    static constexpr uint32_t kRtoMs         = 150;
    static constexpr uint32_t kRtoMaxMs      = 1200;
    static constexpr uint32_t kAckDelayMs    = 20;
    static constexpr uint32_t kPingMs        = 5000;
    static constexpr uint8_t  kPingMisses    = 3;
    static constexpr uint32_t kHostQuietMs   = 20000; // a host marks a peer down after this
    static constexpr uint32_t kDwellMs       = 60;    // a peer's listen per channel while scanning
    static constexpr uint32_t kSessIdleMs    = 120000;
    static constexpr uint32_t kPairResendMs  = 500;

    // bulkWin: fragments in the receive window (16 on a board without PSRAM).
    // mem: where the bulk window goes, bulkWin * kPayloadMax bytes, owned by
    // the caller for as long as the engine lives.
    Engine(Role role, Io& io, const Events& ev, uint8_t bulkWin, uint8_t* bulkMem);
    ~Engine();

    Role role() const { return role_; }

    // -- the loop -------------------------------------------------------------
    // poll: take up to kFramesPerPoll received frames, run the timers, and
    // hand the radio one frame if it is idle. Never blocks.
    void poll();

    // -- the runner -----------------------------------------------------------
    // pumpBulk: move a bulk message's received fragments, in order, into its
    // sink, and check its CRC-32 at the end. At most maxFrags a call. True
    // while there is more it could do. The only call that is not the loop's.
    bool pumpBulk(uint16_t maxFrags);
    // bulkWaiting: whether pumpBulk has anything to do (for posting the job).
    bool bulkWaiting() const;

    // -- peers ----------------------------------------------------------------
    // addPeer: a stored pairing, at start. Returns its index, or -1 when full.
    int  addPeer(const Mac& mac, const uint8_t key[linkcrypto::kKey], uint8_t kind);
    void removePeer(uint8_t peer);
    bool peerUsed(uint8_t peer) const;
    bool peerUp(uint8_t peer) const;
    uint8_t peerKind(uint8_t peer) const;
    const Mac& peerMac(uint8_t peer) const;
    const PeerStats& peerStats(uint8_t peer) const;
    // peerKey: a pairing's k_link, for its owner to store. Never shown, never logged.
    const uint8_t* peerKey(uint8_t peer) const;
    uint8_t peerCount() const;
    uint8_t peersUp() const;
    int  peerIndex(const Mac& mac) const;

    // -- pairing --------------------------------------------------------------
    // Host: openPairing lets PAIR_HELLO in for ms. pairAnswer is the sysop's
    // Y or N to the request pairAsk reported.
    void openPairing(uint32_t ms);
    void closePairing();
    bool pairingOpen() const;
    void pairAnswer(bool yes);
    // The slow part of pairing (P-256: tens of milliseconds on an ESP32):
    // the key pair and the shared secret. In three steps, so the middle one
    // can run on the background runner while the engine stays the loop's:
    //   pairTake  (loop)   copy what it needs into j; false when nothing wants doing
    //   pairRun   (any task) the arithmetic, touching nothing but j
    //   pairGive  (loop)   put the answer back
    // pairCompute is the three in a row, for a peer's own loop and the tests.
    struct PairJob {
        bool     keygen = false;
        uint8_t  from = 0;
        bool     ok = false;
        uint8_t  np[16] = {}, nh[16] = {};
        uint8_t  priv[linkcrypto::kPriv] = {};
        uint8_t  pubMine[linkcrypto::kPub] = {};
        uint8_t  pubTheirs[linkcrypto::kPub] = {};
        uint8_t  key[linkcrypto::kKey] = {};
        uint16_t code = 0;
    };
    bool pairComputeWanted() const;
    bool pairTake(PairJob& j);
    static void pairRun(PairJob& j, linkcrypto::Rng rng, void* rctx);
    void pairGive(PairJob& j);
    bool pairCompute();
    // Peer: startPairing broadcasts PAIR_HELLO on every channel until a host
    // answers or stopPairing.
    void startPairing(uint8_t kind, const char* name, const char* fw);
    void stopPairing();

    // Peer: what it tells the host about itself in HELLO.
    void setIdentity(uint8_t kind, const char* fw, uint32_t families);

    // -- sessions and messages -----------------------------------------------
    // openSession: a session id for talking to peer, 0 when none is free.
    uint16_t openSession(uint8_t peer, uint8_t family);
    // closeSession: forget it here (the family says goodbye in its own words).
    void closeSession(uint8_t peer, uint16_t sess);
    // closeAfter: forget it once what is queued on it has been taken, so a
    // last message ("CLOSE") still goes. Nothing more may be sent on it.
    void closeAfter(uint8_t peer, uint16_t sess);
    // resetSession: tell the far end, then forget it.
    void resetSession(uint8_t peer, uint16_t sess, uint8_t reason);
    // send: a single-frame message. 1 queued, 0 not now (the session's window
    // or the queue is full: try again next pass), -1 never (no such peer or
    // session, too long).
    int  send(uint8_t peer, uint16_t sess, uint8_t family, uint8_t type,
              const void* p, size_t n, bool reliable = true);
    // sendBulk: a message of any size up to kBulkMax, in fragments. data
    // must stay valid until Events::bulkSent. Same returns as send.
    int  sendBulk(uint8_t peer, uint16_t sess, uint8_t family, uint8_t type,
                  const uint8_t* data, uint32_t n);
    // canSend: would send() on this session take a message now?
    bool canSend(uint8_t peer, uint16_t sess) const;
    // sessionFamily: which family a live session belongs to, 0xFF for none.
    uint8_t sessionFamily(uint8_t peer, uint16_t sess) const;

    // -- measures -------------------------------------------------------------
    uint32_t drops(uint8_t reason) const { return reason < D_COUNT ? drops_[reason] : 0; }
    uint32_t dropsTotal() const;
    uint32_t frameUsAvg() const { return usAvg_; }
    uint32_t frameUsMax() const { return usMax_; }
    uint8_t  hostChannel() const { return hostChan_; }   // peer: the host's, as last heard
    bool     hostUp() const;                             // peer: is its link session up

    // ok: every table was allocated. An engine that is not ok does nothing.
    bool     ok() const;
    // setBoardName: host, the name BEACON and PAIR_OFFER carry (16 characters).
    void     setBoardName(const char* name);

private:
    struct Peer;
    struct Sess;
    struct TxMsg;
    struct Ctrl;
    struct BulkRx;

    // receive
    void     onFrame(const uint8_t* f, size_t n, const Mac& from, int8_t rssi);
    void     onClear(const Header& h, const uint8_t* p, const Mac& from);
    void     onSealed(uint8_t pi, const Header& h, const uint8_t* p);
    void     onLinkMsg(uint8_t pi, const Header& h, const uint8_t* p);
    void     onAck(uint8_t pi, const uint8_t* p, size_t n);
    void     onFamily(uint8_t pi, const Header& h, const uint8_t* p);
    void     onFragment(uint8_t pi, Sess& s, const Header& h, const uint8_t* p);
    bool     openFrame(uint8_t pi, const Header& h, const uint8_t* hdr, const uint8_t* ct,
                       uint8_t* pt, bool& pending);
    // transmit
    bool     transmitOne();
    bool     sendFrame(const Mac* to, uint8_t pi, Header& h, const uint8_t* p, bool seal,
                       const uint8_t* keyOverride = nullptr, uint32_t pnOverride = 0);
    bool     queueCtrl(uint8_t pi, const Mac* to, uint8_t type, const uint8_t* p, size_t n,
                       bool sealed, uint16_t sess = 0);
    bool     buildAck(Sess& s, uint8_t* out, size_t& n);
    // timers
    void     timers(uint32_t now);
    void     peerTimers(uint32_t now);
    void     hostTimers(uint32_t now);
    void     bulkEndCheck();
    // tables
    Sess*    findSess(uint8_t pi, uint16_t id);
    const Sess* findSess(uint8_t pi, uint16_t id) const;
    Sess*    newSess(uint8_t pi, uint16_t id, uint8_t family);
    void     dropSess(Sess& s);
    void     failSess(Sess& s, uint8_t reason, bool tellFar);
    uint8_t  inFlight(const Sess& s) const;
    void     peerDown(uint8_t pi);
    void     derivePending(Peer& p, const uint8_t np[16], const uint8_t nh[16]);
    void     drop(uint8_t reason);

    Role        role_;
    Io&         io_;
    Events      ev_;
    Peer*       peers_;          // kPeers on a host, 1 on a peer
    uint8_t     npeers_;
    Sess*       sess_;
    TxMsg*      tx_;
    Ctrl*       ctrl_;
    BulkRx*     bulk_;
    uint8_t     bulkWin_;
    uint8_t*    bulkMem_;
    uint16_t    nextSess_ = 1;
    uint32_t    drops_[D_COUNT] = {};
    uint32_t    usAvg_ = 0, usMax_ = 0;
    uint8_t     hostChan_ = 0;
    // pairing
    struct Pair;
    Pair*       pair_;
    // peer identity
    uint8_t     kind_ = KIND_UNKNOWN;
    char        fw_[13] = {};
    uint32_t    families_ = 0;
    // peer: scanning
    uint8_t     scanCh_ = 0;
    uint32_t    scanAt_ = 0;
    uint32_t    pingAt_ = 0;
    uint8_t     pingMiss_ = 0;
    uint32_t    helloAt_ = 0;
    char        boardName_[17] = {};
    uint32_t    pairUntil_ = 0;
    std::atomic<bool> pumping_{ false };                 // pumpBulk is running
    uint8_t     frame_[kFrameMax];
    uint8_t     scratch_[kFrameMax];
};

}  // namespace ulink
