// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/plugins/link.cpp
// Module:       Plugins / the µnleashed link (1.2.0)
//
// Purpose:      The board's end of the µnleashed link (LINK.md): the radio,
//               the engine (core/link.h), the pairings on userdata, the
//               sysop's LINK commands, and the table of message families
//               other plugins register into (plugins/link.h).
//
//               Off until the sysop switches it on ([plugin:link] enabled =
//               yes, or CONFIG link). Off, it costs flash and a few hundred
//               bytes of static RAM, and nothing else: the radio is left
//               alone and nothing is allocated.
//
//               Where the work runs (Rule no. 1): the engine's poll is this
//               plugin's tick, every 20 ms (PF_FAST), at most eight control
//               frames a call. Bulk fragments never touch the loop: the radio
//               sorts them into their own ring, and one job on the background
//               runner (core/runner.h, 1.1.2) takes them (pumpRx), feeds the
//               family's sink (pumpBulk) and does the pairing arithmetic,
//               lingering 50 ms after the last fragment so a picture is one
//               job, not one a tick. On a tree without the runner (this
//               branch before the 1.1.2 merge) tick does a bounded slice of
//               the same work, which breaks the rule under a stream and is
//               why bulk is not to be used before the merge.
//
//               Pairings are <userdata>/p/link/peers: not in the backup zip,
//               so a restored board pairs its devices again (Rob,
//               2026-09-26). Keys are never shown and never logged.
//
// Libraries:    none beyond the core
// Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1) and the Linux host build
// See also:     LINK.md, src/plugins/link.h, src/platform/linkradio.h
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

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <strings.h>

#include "../core/bbs.h"
#include "../core/bbs_util.h"
#include "../core/clock.h"
#include "../core/disk.h"
#include "../core/linkcrypto.h"
#include "../core/linkfam.h"
#include "../core/photos.h"
#include "../core/satwords.h"
#include "../core/plugin.h"
#include "../platform/linkradio.h"
#include "../platform/platform.h"

#if __has_include("../core/runner.h")
#include "../core/runner.h"
#define LINK_HAS_RUNNER 1
#endif

using ulink::Engine;
using ulink::Mac;

namespace {

constexpr const char kName[]     = "link";
constexpr uint8_t    kFamilies   = 4;
constexpr uint8_t    kCtrlSlots  = 8;      // the loop's ring
// The bulk window, and the runner's ring that holds one window of fragments:
// 16 on a board without PSRAM (3.5 KB of window), 64 on one with it (14 KB,
// in PSRAM), chosen at start (plat::linkRadioPsram). The camsat bench on the
// S3 (72-88 KB/s at 16, no retries) was paced by the window, not the radio.
constexpr uint8_t    kBulkWinSmall = 16;
constexpr uint8_t    kBulkWinBig   = 64;
uint8_t              g_bulkWin     = kBulkWinSmall;
constexpr uint32_t   kLingerMs   = 50;     // the job waits this long for the next fragment
constexpr uint32_t   kJobMaxMs   = 500;    // and gives the runner back after this long
constexpr uint32_t   kPairMs     = 120000;
constexpr const char kPeersFile[] = "peers";

// ---------------------------------------------------------------------------
// State. Static: the family table and two pointers. Everything else is
// allocated when the link starts.
// ---------------------------------------------------------------------------
const linkp::Family* g_fam[kFamilies] = {};
uint8_t g_index = 0xFF;

struct Meta {                          // what the board knows of a pairing beyond the engine's
    char     name[17];
    uint8_t  kind;
    bool     checked;
    uint32_t pairedAt;
    uint8_t  recv;                     // RECV_*: what this board takes from a shared camera (1.2.0)
    uint8_t  camno;                    // its camera number here, 0 = auto
    uint8_t  chan;                     // the channel it was last up on, 0 unknown
};

// Who shares a satellite, from its PEERS (1.2.0). RAM only: the satellite
// sends it again whenever it changes and each time the link comes up.
struct Shared {
    uint8_t             n;
    ulink::SharedBoard  b[Engine::kHosts];
};

struct Ctx;
// The live link. Atomic because the runner's job reads it to find out that
// its engine has been stopped (a CONFIG save restarts the plugins while a
// job may still be running).
std::atomic<Ctx*> g_ctx{ nullptr };

// The radio, as the engine sees it. An engine that is no longer the live
// one (in the grave, its job still running) neither sends nor takes frames:
// the rings may already belong to its successor.
class Radio : public ulink::Io {
public:
    Ctx* owner = nullptr;
    bool live() const { return owner && owner == g_ctx.load(); }
    bool send(const Mac* to, const uint8_t* f, size_t n) override {
        return live() && plat::linkRadioSend(to ? to->b : nullptr, f, n);
    }
    bool idle() override { return plat::linkRadioIdle(); }
    size_t recv(uint8_t* out, size_t cap, Mac& from, int8_t& rssi) override {
        return live() ? plat::linkRadioRecv(out, cap, from.b, rssi) : 0;
    }
    // On the runner. Under the link lock, so stop() cannot free the ring
    // under it.
    size_t recvBulk(uint8_t* out, size_t cap, Mac& from, int8_t& rssi) override {
        plat::linkLock();
        const size_t n = live() ? plat::linkRadioRecvBulk(out, cap, from.b, rssi) : 0;
        plat::linkUnlock();
        return n;
    }
    void lock() override { plat::linkLock(); }
    void unlock() override { plat::linkUnlock(); }
    bool associated() override { return plat::linkRadioAssociated(); }
    uint32_t millis() override { return plat::millis(); }
    uint32_t micros() override { return plat::micros(); }
    void random(uint8_t* out, size_t n) override {
        for (size_t i = 0; i < n; i += 4) {
            uint32_t r = plat::random32();
            for (size_t k = 0; k < 4 && i + k < n; ++k) out[i + k] = static_cast<uint8_t>(r >> (8 * k));
        }
    }
    uint8_t channel() override { return plat::linkRadioChannel(); }
    bool addPeer(const Mac& m) override { return plat::linkRadioAddPeer(m.b); }
    void delPeer(const Mac& m) override { plat::linkRadioDelPeer(m.b); }
    uint32_t unixTime() override { return clk::valid() ? clk::epoch() : 0; }
    uint32_t heapFree() override { return plat::heapFree(); }
};

struct Ctx {
    Radio            radio;
    Engine*          eng = nullptr;
    uint8_t*         win = nullptr;
    Meta             meta[Engine::kPeers] = {};
    Shared           shared[Engine::kPeers] = {};
    bool             saveDue = false;  // the pairings file, written from tick, not from an event
    // the sysop pairing, if one is: their node, and where they are in it
    uint8_t          pairNode = 0xFF;
    uint8_t          pairStep = 0;     // 1 waiting, 2 asked, 3 paired: codes match?
    // CONFIG sats' Share and Unpair, asked on the sysop's screen (code
    // review, 1.2.0: an Enter on the button acted at once)
    uint8_t          askNode = 0xFF;
    uint8_t          askPeer = 0;
    uint8_t          askWhat = 0;      // linkp::ASK_*
    ulink::PairInfo  asking;
    int              pairedAs = -1;
    // the runner's job
    Engine::PairJob  pj;
    bool             pjTaken = false;
#ifdef LINK_HAS_RUNNER
    struct Job : runner::Job { Ctx* ctx = nullptr; };   // the runner hands back the Job; this finds its Ctx
    Job              job;
#endif
};

// savePeersSoon: the pairings file is written on the next tick. For events,
// which the engine raises inside its poll.
void savePeersSoon() {
    if (Ctx* c = g_ctx.load()) c->saveDue = true;
}

int rngRunner(void*, unsigned char* out, size_t n) {
    for (size_t i = 0; i < n; i += 4) {
        uint32_t r = plat::random32();
        for (size_t k = 0; k < 4 && i + k < n; ++k) out[i + k] = static_cast<unsigned char>(r >> (8 * k));
    }
    return 0;
}

#ifdef LINK_HAS_RUNNER
// The work the loop hands off, on the runner: pairing's arithmetic, then the
// bulk fragments from their ring into the engine and on to the family's sink,
// until none has come for kLingerMs or the job has had the runner kJobMaxMs.
void jobWork(runner::Job& j) {
    Ctx& c = *static_cast<Ctx::Job&>(j).ctx;
    if (c.pjTaken) Engine::pairRun(c.pj, rngRunner, nullptr);
    if (!c.eng) return;
    const uint32_t t0 = plat::millis();
    uint32_t heard = t0;
    uint8_t spins = 0;
    while (c.radio.live()) {
        const bool a = c.eng->pumpRx(16);
        const bool b = c.eng->pumpBulk(16);
        const uint32_t now = plat::millis();
        if (a || b) heard = now;
        else if (now - heard >= kLingerMs) break;
        if (now - t0 >= kJobMaxMs) break;
        // The runner sits below the BBS task, so the loop is never kept
        // waiting; the breath is for the idle task and whatever else is queued.
        if (!(a || b) || ++spins == 0) runner::breathe();
    }
}
#else
// Before the 1.1.2 merge: a bounded slice on the loop.
void work(Ctx& c) {
    if (c.pjTaken) Engine::pairRun(c.pj, rngRunner, nullptr);
    if (!c.eng) return;
    c.eng->pumpRx(8);
    c.eng->pumpBulk(8);
}
#endif

// ---------------------------------------------------------------------------
// The families
// ---------------------------------------------------------------------------
const linkp::Family* fam(void* cx, uint8_t id) {
    if (!cx || cx != g_ctx.load()) return nullptr;   // a stopped engine's job, still running
    for (const linkp::Family* f : g_fam) if (f && f->id == id) return f;
    return nullptr;
}

bool evMessage(void* cx, uint8_t peer, uint16_t sess, uint8_t family, uint8_t type, const uint8_t* p, size_t n) {
    const linkp::Family* f = fam(cx, family);
    if (!f || !f->message) return true;             // nobody listening: taken and dropped
    return f->message(peer, sess, type, p, n);
}
bool evBulkBegin(void* cx, uint8_t peer, uint16_t sess, uint8_t family, uint8_t type, uint32_t total) {
    const linkp::Family* f = fam(cx, family);
    return f && f->bulkBegin && f->bulkBegin(peer, sess, type, total);
}
bool evBulkData(void* cx, uint8_t peer, uint16_t sess, uint8_t family, const uint8_t* p, size_t n) {
    const linkp::Family* f = fam(cx, family);
    return f && f->bulkData ? f->bulkData(peer, sess, p, n) : false;
}
void evBulkFinish(void* cx, uint8_t peer, uint16_t sess, uint8_t family, bool ok) {
    const linkp::Family* f = fam(cx, family);
    if (f && f->bulkFinish) f->bulkFinish(peer, sess, ok);
}
void evBulkEnd(void* cx, uint8_t peer, uint16_t sess, uint8_t family, bool ok) {
    const linkp::Family* f = fam(cx, family);
    if (f && f->bulkEnd) f->bulkEnd(peer, sess, ok);
}
void evBulkSent(void* cx, uint8_t peer, uint16_t sess, uint8_t family, bool ok) {
    const linkp::Family* f = fam(cx, family);
    if (f && f->bulkSent) f->bulkSent(peer, sess, ok);
}
void evReset(void* cx, uint8_t peer, uint16_t sess, uint8_t family, uint8_t reason) {
    const linkp::Family* f = fam(cx, family);
    if (f && f->reset) f->reset(peer, sess, reason);
}
void evPeerState(void* cx, uint8_t peer, bool up) {
    Ctx* c = g_ctx.load();
    if (!c || cx != c) return;
    if (peer < Engine::kPeers) {
        plat::log("link: %s \"%s\" %s", ulink::kindName(c->meta[peer].kind), c->meta[peer].name,
                  up ? "is up" : "went quiet");
        // Where it was last heard, for LINK's footnote when this board's
        // router moves and a shared satellite stays with its other boards.
        if (up && c->meta[peer].chan != plat::linkRadioChannel()) {
            c->meta[peer].chan = plat::linkRadioChannel();
            savePeersSoon();
        }
    }
    for (const linkp::Family* f : g_fam) if (f && f->peerState) f->peerState(peer, up);
}

// ---------------------------------------------------------------------------
// Pairings on userdata
// ---------------------------------------------------------------------------
void macText(const Mac& m, char* out, size_t n) {
    snprintf(out, n, "%02x:%02x:%02x:%02x:%02x:%02x", m.b[0], m.b[1], m.b[2], m.b[3], m.b[4], m.b[5]);
}

bool macParse(const char* s, Mac& m) {
    unsigned v[6];
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
    for (int i = 0; i < 6; ++i) { if (v[i] > 255) return false; m.b[i] = static_cast<uint8_t>(v[i]); }
    return true;
}

bool hexKey(const char* s, uint8_t key[linkcrypto::kKey]) {
    for (size_t i = 0; i < linkcrypto::kKey; ++i) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) return false;
        key[i] = static_cast<uint8_t>(v);
    }
    return true;
}

// One line a pairing. Version 2 (1.2.0, a header line says so):
//   mac kind checked pairedAt recv camno chan key name
// recv is what this board takes from a shared camera (RECV_*), camno its
// camera number here (0 auto), chan the channel it was last up on. Version
// 1 (no header): mac kind checked pairedAt key name. The name last, because
// a name written before 1.2.0 may have spaces.
constexpr const char kPeersHeader[] = "#link-peers 2";

void savePeers() {
    Ctx* c = g_ctx;
    if (!c || !c->eng) return;
    char path[128], tmp[136];
    if (!plugins::path(g_index, kPeersFile, path, sizeof(path))) {
        plat::log("link: cannot write the pairings (no room on userdata?)");
        return;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = disk::open(tmp, "w");
    if (!f) return;
    fprintf(f, "%s\n# unleashed link pairings: mac kind checked paired recv camno chan key name\n", kPeersHeader);
    for (uint8_t i = 0; i < Engine::kPeers; ++i) {
        if (!c->eng->peerUsed(i)) continue;
        char mac[18], key[33];
        macText(c->eng->peerMac(i), mac, sizeof(mac));
        const uint8_t* k = c->eng->peerKey(i);
        for (size_t b = 0; b < linkcrypto::kKey; ++b) snprintf(key + 2 * b, 3, "%02x", k[b]);
        const Meta& m = c->meta[i];
        fprintf(f, "%s %u %u %u %u %u %u %s %s\n", mac, m.kind, m.checked ? 1u : 0u,
                static_cast<unsigned>(m.pairedAt), m.recv, m.camno, m.chan, key, m.name);
        linkcrypto::wipe(key, sizeof(key));
    }
    // fclose flushes what is left and can fail on its own (a full partition),
    // so both answers count.
    const bool flushed = fflush(f) == 0;
    const bool ok = (fclose(f) == 0) && flushed;
    // Same partition: LittleFS replaces the old file in the rename, so it is
    // never removed first (CLAUDE.md, "never remove a live file to make
    // room for a rename").
    if (!ok || rename(tmp, path) != 0) {
        remove(tmp);
        plat::log("link: the pairings were not saved");
    }
}

void loadPeers() {
    Ctx* c = g_ctx;
    char path[128];
    if (!plugins::readPath(g_index, kPeersFile, path, sizeof(path))) return;
    FILE* f = disk::open(path, "r");
    if (!f) return;
    char line[160];
    bool v2 = false;
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, kPeersHeader, sizeof(kPeersHeader) - 1)) { v2 = true; continue; }
        if (line[0] == '#' || line[0] == '\n') continue;
        char mac[24] = {}, key[40] = {};
        unsigned kind = 0, checked = 0, at = 0, recv = linkfam::RECV_ALL, camno = 0, chan = 0;
        int used = 0;
        if (v2) {
            if (sscanf(line, "%23s %u %u %u %u %u %u %39s %n", mac, &kind, &checked, &at, &recv, &camno, &chan, key,
                       &used) < 8)
                continue;
        } else if (sscanf(line, "%23s %u %u %u %39s %n", mac, &kind, &checked, &at, key, &used) < 5) {
            continue;
        }
        Mac m;
        uint8_t k[linkcrypto::kKey];
        if (!macParse(mac, m) || strlen(key) != 32 || !hexKey(key, k)) continue;
        int idx = c->eng->addPeer(m, k, static_cast<uint8_t>(kind));
        linkcrypto::wipe(k, sizeof(k));
        linkcrypto::wipe(key, sizeof(key));
        if (idx < 0) break;
        Meta& me = c->meta[idx];
        char* name = line + used;
        name[strcspn(name, "\r\n")] = '\0';
        snprintf(me.name, sizeof(me.name), "%s", name);
        me.kind = static_cast<uint8_t>(kind);
        me.checked = checked != 0;
        me.pairedAt = at;
        me.recv = static_cast<uint8_t>(recv & linkfam::RECV_ALL);
        me.camno = static_cast<uint8_t>(camno <= 9 ? camno : 0);
        me.chan = static_cast<uint8_t>(chan <= 14 ? chan : 0);
    }
    linkcrypto::wipe(line, sizeof(line));
    fclose(f);
}

// ---------------------------------------------------------------------------
// Pairing, from the sysop's side
// ---------------------------------------------------------------------------
void evPairAsk(void* cx, const ulink::PairInfo& who) {
    Ctx* c = g_ctx.load();
    if (!c || cx != c) return;
    c->asking = who;
}

// nameTaken: another pairing, or the board's built-in camera, already has
// this name (ignoring case). SNAPSHOT and SATS take a satellite by name, and
// the first match would win.
bool nameTaken(const char* name, int self) {
    Ctx* c = g_ctx.load();
    if (!c) return false;
    for (uint8_t i = 0; i < Engine::kPeers; ++i)
        if (static_cast<int>(i) != self && c->eng->peerUsed(i) && !strcasecmp(c->meta[i].name, name)) return true;
    for (uint8_t i = 0; i < photos::cameras(); ++i) {
        const photos::Camera* cam = photos::camera(i);
        if (cam && cam->order == 0 && !strcasecmp(cam->name, name)) return true;
    }
    return false;
}

// oneWord: a name as SNAPSHOT can take it: printable, a space becomes '-'.
void oneWord(const char* in, char* out, size_t n) {
    size_t k = 0;
    for (; *in && k + 1 < n; ++in) {
        const char ch = *in;
        if (ch == ' ') out[k++] = '-';
        else if (ch > 0x20 && ch < 0x7F) out[k++] = ch;
    }
    out[k] = '\0';
}

// uniqueName: want, made one word and unique (a "-2", "-3" on the end).
void uniqueName(const char* want, int self, char* out, size_t n) {
    char base[17];
    oneWord(want, base, sizeof(base));
    if (!base[0]) snprintf(base, sizeof(base), "%s", satwords::kSatShort);
    // All digits would read as a camera number to SNAPSHOT.
    bool digits = true;
    for (const char* p = base; *p; ++p) digits = digits && *p >= '0' && *p <= '9';
    if (digits) {
        char tmp[17];
        snprintf(tmp, sizeof(tmp), "%s-%.11s", satwords::kSatShort, base);
        snprintf(base, sizeof(base), "%s", tmp);
    }
    snprintf(out, n, "%s", base);
    for (unsigned k = 2; k < 10 && nameTaken(out, self); ++k) {
        char tail[4];
        snprintf(tail, sizeof(tail), "-%u", k);
        const size_t keep = n - 1 - strlen(tail) < strlen(base) ? n - 1 - strlen(tail) : strlen(base);
        snprintf(out, n, "%.*s%s", static_cast<int>(keep), base, tail);
    }
}

void evPaired(void* cx, uint8_t peer, const ulink::PairInfo& who) {
    Ctx* c = g_ctx.load();
    if (!c || cx != c || peer >= Engine::kPeers) return;
    Meta& m = c->meta[peer];
    m = Meta();
    uniqueName(who.name[0] ? who.name : ulink::kindName(who.kind), peer, m.name, sizeof(m.name));
    m.kind = who.kind;
    m.checked = false;
    m.pairedAt = clk::valid() ? clk::epoch() : 0;
    m.recv = linkfam::RECV_ALL;
    m.chan = plat::linkRadioChannel();
    c->shared[peer] = Shared();
    c->pairedAs = peer;
    savePeers();
    char mac[18];
    macText(who.mac, mac, sizeof(mac));
    plat::log("link: paired %s \"%s\" %s as peer %u", ulink::kindName(who.kind), m.name, mac, peer);
}

Session* pairSession() {
    Ctx* c = g_ctx;
    if (!c || c->pairNode == 0xFF) return nullptr;
    struct Find { uint8_t node; Session* s; } fd{ c->pairNode, nullptr };
    Bbs::instance().eachSession([](void* ctx, Session& s) {
        Find* f = static_cast<Find*>(ctx);
        if (s.st != SState::Free && s.id == f->node) f->s = &s;
    }, &fd);
    if (fd.s && !Bbs::instance().owns(*fd.s, g_index)) return nullptr;
    return fd.s;
}

void pairEnd(Session* s, Color col, const char* text) {
    Ctx* c = g_ctx;
    if (c) {
        // A device that asked and was not paired leaves nothing behind in
        // ESP-NOW's own peer table, which holds 20 in all: the offer to it
        // added it there.
        if (c->eng && c->pairedAs < 0 && !c->asking.mac.zero() && c->eng->peerIndex(c->asking.mac) < 0)
            plat::linkRadioDelPeer(c->asking.mac.b);
        if (c->eng) c->eng->closePairing();
        c->pairNode = 0xFF;
        c->pairStep = 0;
        c->pairedAs = -1;
        c->asking = ulink::PairInfo();
    }
    if (!s) return;
    s->term.nl(s->tl);
    s->term.color(s->tl, col);
    s->term.text(s->tl, text);
    Bbs::instance().release(*s);
}

// ---------------------------------------------------------------------------
// One satellite, several boards (1.2.0)
// ---------------------------------------------------------------------------
// A satellite shared with boards on another channel asked to pair: the
// engine refuses it, and the sysop pairing here is told why, in words.
void evPairRefused(void* cx, const ulink::PairInfo& who, uint8_t theirChan) {
    Ctx* c = g_ctx.load();
    if (!c || cx != c) return;
    char mac[18];
    macText(who.mac, mac, sizeof(mac));
    plat::log("link: %s \"%s\" %s works on channel %u for its other boards; this board is on %u: not paired",
              ulink::kindName(who.kind), who.name, mac, theirChan, plat::linkRadioChannel());
    Session* s = pairSession();
    if (!s || c->pairStep != 1) return;
    const unsigned mine = plat::linkRadioChannel();
    char a[96], b[96];
    s->term.nl(s->tl);
    if (Bbs::instance().rowWidth(*s) >= 60) {
        snprintf(a, sizeof(a), "This %s works on channel %u for its other boards and this board is on %u.",
                 satwords::kSat, static_cast<unsigned>(theirChan), mine);
        Bbs::instance().rowText(*s, Color::LightRed, a);
        Bbs::instance().rowText(*s, Color::Grey, "Boards sharing a satellite must be on one Wi-Fi channel, which usually means");
        Bbs::instance().rowText(*s, Color::Grey, "one router.", false);
    } else {
        snprintf(a, sizeof(a), "This %s works on channel %u for", satwords::kSat, static_cast<unsigned>(theirChan));
        snprintf(b, sizeof(b), "its other boards; this board is on %u.", mine);
        Bbs::instance().rowText(*s, Color::LightRed, a);
        Bbs::instance().rowText(*s, Color::LightRed, b);
        Bbs::instance().rowText(*s, Color::Grey, "Boards sharing a satellite must be on");
        Bbs::instance().rowText(*s, Color::Grey, "one Wi-Fi channel: usually one router.", false);
    }
    pairEnd(s, Color::Grey, "Not paired.");
}

void evPeersList(void* cx, uint8_t peer, const ulink::SharedBoard* boards, uint8_t n) {
    Ctx* c = g_ctx.load();
    if (!c || cx != c || peer >= Engine::kPeers) return;
    Shared& sh = c->shared[peer];
    sh.n = n > Engine::kHosts ? Engine::kHosts : n;
    for (uint8_t i = 0; i < sh.n; ++i) sh.b[i] = boards[i];
}

// The satellite let this board go: its owner revoked it.
void evUnpaired(void* cx, uint8_t peer) {
    Ctx* c = g_ctx.load();
    if (!c || cx != c || peer >= Engine::kPeers) return;
    plat::log("link: \"%s\" let this board go (its owner revoked it)", c->meta[peer].name);
    c->meta[peer] = Meta();
    c->shared[peer] = Shared();
    savePeersSoon();
}

// ownsIt: this board owns satellite peer, from its PEERS. Unknown (no PEERS
// yet): -1.
int ownsIt(uint8_t peer) {
    Ctx* c = g_ctx.load();
    if (!c || peer >= Engine::kPeers || !c->shared[peer].n) return -1;
    const Shared& sh = c->shared[peer];
    for (uint8_t i = 0; i < sh.n; ++i)
        if (sh.b[i].flags & ulink::SB_YOU) return (sh.b[i].flags & ulink::SB_OWNER) ? 1 : 0;
    return -1;
}

// ownerName: the owner's name from PEERS, "" unknown.
const char* ownerName(uint8_t peer) {
    Ctx* c = g_ctx.load();
    if (!c || peer >= Engine::kPeers) return "";
    const Shared& sh = c->shared[peer];
    for (uint8_t i = 0; i < sh.n; ++i) if (sh.b[i].flags & ulink::SB_OWNER) return sh.b[i].name;
    return "";
}

void pairTick() {
    Ctx* c = g_ctx;
    if (!c || c->pairNode == 0xFF) return;
    Session* s = pairSession();
    if (!s) { pairEnd(nullptr, Color::Grey, ""); return; }      // the sysop left
    if (c->pairStep == 1 && c->asking.mac.zero() == false) {
        char mac[18], buf[120];
        macText(c->asking.mac, mac, sizeof(mac));
        snprintf(buf, sizeof(buf), "Pair %s \"%s\" %s, code %04u? (y/N) ", ulink::kindName(c->asking.kind),
                 c->asking.name, mac, static_cast<unsigned>(c->asking.code));
        s->term.nl(s->tl);
        s->term.color(s->tl, Color::Yellow);
        s->term.text(s->tl, buf);
        c->pairStep = 2;
        return;
    }
    if (c->pairStep == 3 && c->pairedAs >= 0) {
        char buf[96];
        snprintf(buf, sizeof(buf), "Paired. Does the device show %04u too? (y/N) ",
                 static_cast<unsigned>(c->asking.code));
        s->term.nl(s->tl);
        s->term.color(s->tl, Color::LightGreen);
        s->term.text(s->tl, buf);
        c->pairStep = 4;
        return;
    }
    if ((c->pairStep == 1 || c->pairStep == 3) && c->eng && !c->eng->pairingOpen() && c->pairedAs < 0) {
        pairEnd(s, Color::Grey, c->pairStep == 1 ? "Nobody asked to pair. The window is closed."
                                                 : "The device did not finish pairing.");
    }
}

void cmdShare(Bbs& b, Session& s, const char* arg);
void cmdForget(Bbs& b, Session& s, const char* arg);

void onKey(Session& s, int k, uint32_t now) {
    (void)now;
    Ctx* c = g_ctx;
    if (c && s.id == c->askNode) {
        Bbs& b = Bbs::instance();
        c->askNode = 0xFF;
        if (k == 'y' || k == 'Y') {
            s.term.text(s.tl, "Y");
            char arg[4];
            snprintf(arg, sizeof(arg), "%u", c->askPeer + 1u);
            s.st = SState::Shell;                 // released without a prompt: the command draws one
            b.release(s);
            s.term.nl(s.tl);
            if (c->askWhat == linkp::ASK_SHARE) cmdShare(b, s, arg);
            else                                cmdForget(b, s, arg);
        } else {
            s.term.text(s.tl, "N");
            s.term.nl(s.tl);
            s.term.color(s.tl, Color::Grey);
            s.term.text(s.tl, "Not changed.");
            b.release(s);
        }
        return;
    }
    if (!c || s.id != c->pairNode) { Bbs::instance().release(s); return; }
    const bool yes = k == 'y' || k == 'Y';
    const bool stop = k == 'q' || k == 'Q' || k == KEY_ESC || k == KEY_BREAK;
    switch (c->pairStep) {
        case 1:
            if (stop) pairEnd(&s, Color::Grey, "Pairing stopped.");
            return;
        case 2:
            if (yes) {
                s.term.text(s.tl, "Y");
                s.term.nl(s.tl);
                s.term.color(s.tl, Color::Grey);
                s.term.text(s.tl, "Pairing...");
                c->eng->pairAnswer(true);
                c->pairStep = 3;
            } else {
                pairEnd(&s, Color::Grey, "Not paired.");
            }
            return;
        case 4: {
            const bool camera = c->pairedAs >= 0 && c->meta[c->pairedAs].kind == ulink::KIND_CAMSAT;
            if (c->pairedAs >= 0) {
                c->meta[c->pairedAs].checked = yes;
                savePeers();
            }
            // A camera satellite: where its settings on this board are
            // (tty-ux-sats). Its number comes when it is listed, a second on.
            if (camera) {
                s.term.nl(s.tl);
                s.term.color(s.tl, Color::LightGreen);
                s.term.text(s.tl, yes ? "Paired and checked." : "Paired, codes not checked.");
                pairEnd(&s, Color::Grey, Bbs::instance().rowWidth(s) >= 60
                                             ? "CONFIG sats sets its number and what it sends here."
                                             : "CONFIG sats: its number, what it sends.");
                return;
            }
            pairEnd(&s, Color::LightGreen, yes ? "Paired and checked." : "Paired, codes not checked.");
            return;
        }
        default:
            if (stop) pairEnd(&s, Color::Grey, "Pairing stopped.");
            return;
    }
}

void onLogoff(Session& s) {
    Ctx* c = g_ctx.load();
    if (c && s.id == c->pairNode) pairEnd(nullptr, Color::Grey, "");
    if (c && s.id == c->askNode) c->askNode = 0xFF;
}

// ---------------------------------------------------------------------------
// Start and stop
// ---------------------------------------------------------------------------
void freeCtx(Ctx* c) {
    if (!c) return;
    delete c->eng;
    c->eng = nullptr;
    plat::linkFree(c->win);
    c->win = nullptr;
    linkcrypto::wipe(&c->pj, sizeof(c->pj));
    c->~Ctx();
    plat::linkFree(c);
}

// A stopped link whose job the runner has not handed back yet. Two, because
// a CONFIG save straight after another can stop a link while the last one
// is still waiting; a third waits for one of them (the job is bounded, and
// it leaves at once once its engine is not the live one).
constexpr uint8_t kGraves = 2;
Ctx* g_grave[kGraves] = {};

bool jobIdle(Ctx* c) {
#ifdef LINK_HAS_RUNNER
    // A finished job is DONE, not IDLE, until somebody collects it, and for
    // a buried context nobody else will.
    if (runner::done(c->job)) runner::collect(c->job);
    return runner::idle(c->job);
#else
    (void)c;
    return true;
#endif
}

void buryGraves() {
    for (Ctx*& g : g_grave)
        if (g && jobIdle(g)) { freeCtx(g); g = nullptr; }
}

void toGrave(Ctx* c) {
    for (;;) {
        buryGraves();
        for (Ctx*& g : g_grave) if (!g) { g = c; return; }
        plat::linkRadioWait(1);
    }
}

bool start(Bbs& bbs) {
    (void)bbs;
    g_index = plugins::indexOf(kName);
    buryGraves();
    g_bulkWin = plat::linkRadioPsram() ? kBulkWinBig : kBulkWinSmall;
    if (!plat::linkRadioStart(kCtrlSlots, g_bulkWin)) {
        plat::log("link: no radio (ESP-NOW would not start)");
        return true;                  // the commands stay, and say why
    }
    void* mem = plat::linkAlloc(sizeof(Ctx));
    if (!mem) { plat::linkRadioStop(); return false; }
    Ctx* c = new (mem) Ctx();
    c->radio.owner = c;
    const uint8_t win = g_bulkWin;
    c->win = static_cast<uint8_t*>(plat::linkAlloc(static_cast<size_t>(win) * ulink::kPayloadMax));
    ulink::Events ev;
    ev.ctx       = c;
    ev.message   = evMessage;
    ev.bulkBegin = evBulkBegin;
    ev.bulkData  = evBulkData;
    ev.bulkEnd   = evBulkEnd;
    ev.bulkFinish = evBulkFinish;
    ev.bulkSent  = evBulkSent;
    ev.reset     = evReset;
    ev.peerState = evPeerState;
    ev.pairAsk   = evPairAsk;
    ev.paired    = evPaired;
    ev.pairRefused = evPairRefused;
    ev.peersList = evPeersList;
    ev.unpaired  = evUnpaired;
    c->eng = c->win ? new (std::nothrow) Engine(ulink::Role::Host, c->radio, ev, win, c->win) : nullptr;
    if (!c->eng || !c->eng->ok()) {
        freeCtx(c);
        plat::linkRadioStop();
        plat::log("link: not enough memory to start");
        return false;
    }
#ifdef LINK_HAS_RUNNER
    c->job.name = "link";
    c->job.work = jobWork;
    c->job.ctx  = c;
#endif
    g_ctx = c;
    c->eng->setBoardName(syscfg::get().boardName[0] ? syscfg::get().boardName : "unleashed");
    loadPeers();
    // The channel is the router's, and means nothing until the board has
    // joined it (camsat bench: channel 6 printed before association).
    if (plat::linkRadioAssociated())
        plat::log("link: on, channel %u, %u pairing%s", plat::linkRadioChannel(), c->eng->peerCount(),
                  c->eng->peerCount() == 1 ? "" : "s");
    else
        plat::log("link: on, %u pairing%s, on the router's channel once it joins", c->eng->peerCount(),
                  c->eng->peerCount() == 1 ? "" : "s");
    return true;
}

void stop() {
    Ctx* c = g_ctx.load();
    if (!c) { plat::linkRadioStop(); return; }
    // Every family is told each peer is gone before the engine goes. Since
    // 1.1.2 a CONFIG save restarts only the plugins whose settings moved, so
    // a save of CONFIG link alone stops and starts the link under a running
    // doors (or camsat): without this, a caller in a door kept sending to a
    // session the new engine never had, and heard nothing until the home
    // key. Told now, doors gives them back ("Lost the signal") and forgets
    // the door list, and its CLOSEs are queued for the polls below. At a
    // full restart or a shutdown the doors plugin has already stopped
    // (registry order) and its family is gone, so it hears nothing twice.
    for (uint8_t i = 0; i < Engine::kPeers; ++i)
        if (c->eng->peerUsed(i) && c->eng->peerUp(i))
            for (const linkp::Family* f : g_fam) if (f && f->peerState) f->peerState(i, false);
    // Give the queued CLOSEs to the radio before it goes: a few polls, a
    // few milliseconds, once, at a restart or a shutdown.
    for (uint8_t i = 0; i < 4; ++i) {
        c->eng->poll();
        plat::linkRadioWait(5);
    }
    g_ctx = nullptr;
    if (c->pairNode != 0xFF) {
        struct Find { uint8_t node; Session* s; } fd{ c->pairNode, nullptr };
        Bbs::instance().eachSession([](void* ctx, Session& ss) {
            Find* f = static_cast<Find*>(ctx);
            if (ss.st != SState::Free && ss.id == f->node) f->s = &ss;
        }, &fd);
        if (fd.s && Bbs::instance().owns(*fd.s, g_index)) Bbs::instance().release(*fd.s);
    }
    // Under the link lock: a job inside recvBulk finishes before the rings
    // are freed, and finds itself not live after.
    plat::linkLock();
    plat::linkRadioStop();
    plat::linkUnlock();
    // The runner may still be inside this engine: leave it to be freed once
    // the job is back.
    if (!jobIdle(c)) { toGrave(c); return; }
    freeCtx(c);
}

void tick(uint32_t now) {
    (void)now;
    buryGraves();
    Ctx* c = g_ctx.load();
    if (!c || !c->eng) return;
    c->eng->poll();
    // One flash write a pass (1.1.2): the caller log and the call figures
    // wait for a pass that has written nothing, and so do the pairings.
    if (c->saveDue && !disk::tally().writes) { c->saveDue = false; savePeers(); }

    // Hand the slow work off.
#ifdef LINK_HAS_RUNNER
    if (runner::done(c->job)) {
        runner::collect(c->job);
        if (c->pjTaken) { c->eng->pairGive(c->pj); c->pjTaken = false; }
    }
    if (runner::idle(c->job)) {
        if (!c->pjTaken && c->eng->pairComputeWanted()) c->pjTaken = c->eng->pairTake(c->pj);
        if (c->pjTaken || c->eng->bulkWaiting() || plat::linkRadioBulkWaiting()) runner::post(c->job);
    }
#else
    // Until the 1.1.2 runner is merged: on the loop, bounded. The pairing
    // arithmetic is one slow pass per pairing, a sysop standing at the board.
    if (!c->pjTaken && c->eng->pairComputeWanted()) c->pjTaken = c->eng->pairTake(c->pj);
    work(*c);
    if (c->pjTaken) { c->eng->pairGive(c->pj); c->pjTaken = false; }
#endif
    pairTick();
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
void say(Session& s, Color col, const char* text) {
    s.term.color(s.tl, col);
    s.term.text(s.tl, text);
}

bool needLink(Bbs& b, Session& s) {
    if (g_ctx && g_ctx.load()->eng) return true;
    say(s, Color::LightRed, plat::linkRadioUp() || !plugins::running(g_index)
                               ? "The link is off. CONFIG link turns it on."
                               : "The link has no radio on this board.");
    b.prompt(s);
    return false;
}

void ago(uint32_t ms, char* out, size_t n) {
    const uint32_t s = ms / 1000;
    if (s < 60)        snprintf(out, n, "%us", static_cast<unsigned>(s));
    else if (s < 3600) snprintf(out, n, "%um", static_cast<unsigned>(s / 60));
    else               snprintf(out, n, "%uh", static_cast<unsigned>(s / 3600));
}

// sharedText: a pairing's sharing, from its PEERS. 40: "3 own" or "2"; 80:
// "3, this board" or "2, Porch BBS". Empty when not shared or not known.
void sharedText(uint8_t i, bool wide, char* out, size_t n) {
    Ctx* c = g_ctx;
    out[0] = '\0';
    const Shared& sh = c->shared[i];
    if (sh.n < 2) return;
    const int own = ownsIt(i);
    if (wide) snprintf(out, n, "%u, %.16s", sh.n, own == 1 ? "this board" : ownerName(i));
    else      snprintf(out, n, "%u%s", sh.n, own == 1 ? " own" : "");
}

// otherRows: under a shared satellite, a row for each other board.
void otherRows(Bbs& b, Session& s, uint8_t i, bool wide) {
    Ctx* c = g_ctx;
    const Shared& sh = c->shared[i];
    if (sh.n < 2) return;
    for (uint8_t k = 0; k < sh.n; ++k) {
        const ulink::SharedBoard& o = sh.b[k];
        if (o.flags & ulink::SB_YOU) continue;
        const bool owner = o.flags & ulink::SB_OWNER;
        const bool heard = o.flags & ulink::SB_HEARD;
        char line[80];
        if (wide)
            snprintf(line, sizeof(line), "      %-16.16s  %s%s", o.name, owner ? "owner, " : "",
                     heard ? "heard" : "not heard");
        else
            snprintf(line, sizeof(line), "      %-12.12s %s%s", o.name, owner ? "owner, " : "",
                     heard ? "heard" : "not heard");
        b.rowText(s, heard ? Color::Grey : Color::Yellow, line);
    }
}

void cmdList(Bbs& b, Session& s) {
    Ctx* c = g_ctx;
    Engine& e = *c->eng;
    char right[24], line[112];
    snprintf(right, sizeof(right), "channel %u", plat::linkRadioChannel());
    b.rowTitle(s, "Radio link", right);
    const bool wide = b.rowWidth(s) >= 60;
    // Sub-rows only while the whole list fits the screen (less 8 for the
    // title, the footer and the prompt); past that SATS n lists a
    // satellite's boards.
    uint8_t rows = 0;
    for (uint8_t i = 0; i < Engine::kPeers; ++i)
        if (e.peerUsed(i)) rows = static_cast<uint8_t>(rows + 1 + (c->shared[i].n > 1 ? c->shared[i].n - 1 : 0));
    const uint8_t height = s.term.rows() ? s.term.rows() : 24;
    const bool subRows = rows + 8 <= height;
    if (!e.peerCount()) {
        b.rowText(s, Color::Grey, "No devices paired. LINK PAIR pairs one.");
    } else {
        b.rowText(s, Color::Cyan, wide ? " #  Name             Kind     State  RSSI  Heard  Checked  Shared, owner"
                                       : " #  Name       Kind    State RSSI Brd");
        const uint32_t now = plat::millis();
        for (uint8_t i = 0; i < Engine::kPeers; ++i) {
            if (!e.peerUsed(i)) continue;
            const ulink::PeerStats& st = e.peerStats(i);
            char heard[8] = "-", shared[24];
            if (st.lastHeard) ago(now - st.lastHeard, heard, sizeof(heard));
            sharedText(i, wide, shared, sizeof(shared));
            if (wide) {
                snprintf(line, sizeof(line), " %u  %-16.16s %-8s %-6s %4d  %-5s  %-7s  %s", i + 1u, c->meta[i].name,
                         ulink::kindName(c->meta[i].kind), e.peerUp(i) ? "up" : "down",
                         static_cast<int>(st.rssi), heard, c->meta[i].checked ? "yes" : "no", shared);
            } else {
                snprintf(line, sizeof(line), " %u  %-10.10s %-7.7s %-5s %4d %s", i + 1u, c->meta[i].name,
                         ulink::kindName(c->meta[i].kind), e.peerUp(i) ? "up" : "down", static_cast<int>(st.rssi),
                         shared);
            }
            // rowText does not cut a line on a list, so the row is cut here.
            line[b.rowWidth(s) < sizeof(line) ? b.rowWidth(s) : sizeof(line) - 1] = '\0';
            b.rowText(s, e.peerUp(i) ? Color::LightGreen : Color::Grey, line);
            if (subRows && e.peerUp(i)) otherRows(b, s, i, wide);
        }
        if (!subRows && rows > e.peerCount()) b.rowText(s, Color::Grey, "SATS n lists a satellite's boards.");
    }
    uint32_t rx = 0, tx = 0, re = 0;
    for (uint8_t i = 0; i < Engine::kPeers; ++i) {
        if (!e.peerUsed(i)) continue;
        rx += e.peerStats(i).rx;
        tx += e.peerStats(i).tx;
        re += e.peerStats(i).retries;
    }
    char a[16], bb[16], r[16], d[16], fr[16], fw[16];
    bbsu::fmtCommas(rx, a, sizeof(a));
    bbsu::fmtCommas(tx, bb, sizeof(bb));
    bbsu::fmtCommas(re, r, sizeof(r));
    bbsu::fmtCommas(e.dropsTotal(), d, sizeof(d));
    bbsu::fmtCommas(e.frameUsAvg(), fr, sizeof(fr));
    bbsu::fmtCommas(e.frameUsMax(), fw, sizeof(fw));
    if (wide) {
        snprintf(line, sizeof(line), "Frames in %s, out %s, retried %s, dropped %s.", a, bb, r, d);
        b.rowText(s, Color::Grey, line);
    } else {
        // Four short lines at 40: the two long ones wrapped on a C64.
        snprintf(line, sizeof(line), "Frames in %s, out %s.", a, bb);
        b.rowText(s, Color::Grey, line);
        snprintf(line, sizeof(line), "Retried %s, dropped %s.", r, d);
        b.rowText(s, Color::Grey, line);
    }
    if (e.dropsTotal()) {
        char* p = line;
        size_t left = sizeof(line);
        int w = snprintf(p, left, "Dropped:");
        for (uint8_t k = 0; k < ulink::D_COUNT && w > 0 && static_cast<size_t>(w) < left; ++k) {
            if (!e.drops(k)) continue;
            p += w;
            left -= static_cast<size_t>(w);
            w = snprintf(p, left, " %s %u", ulink::dropName(k), static_cast<unsigned>(e.drops(k)));
        }
        line[b.rowWidth(s) < sizeof(line) ? b.rowWidth(s) : sizeof(line) - 1] = '\0';
        b.rowText(s, Color::Grey, line);
    }
    // Rule no. 1, read off the board: the cost per frame and how full each
    // receive ring has been (LINK.md, "Where the work runs").
    if (wide) {
        snprintf(line, sizeof(line), "Per frame %s us, worst %s. Rings high %u/%u, bulk %u/%u.", fr, fw,
                 static_cast<unsigned>(plat::linkRadioRingHigh()), static_cast<unsigned>(kCtrlSlots),
                 static_cast<unsigned>(plat::linkRadioBulkHigh()), static_cast<unsigned>(g_bulkWin));
        b.rowText(s, Color::Grey, line);
    } else {
        snprintf(line, sizeof(line), "Frame %sus, worst %s.", fr, fw);
        b.rowText(s, Color::Grey, line);
        snprintf(line, sizeof(line), "Rings high %u/%u, bulk %u/%u.", static_cast<unsigned>(plat::linkRadioRingHigh()),
                 static_cast<unsigned>(kCtrlSlots), static_cast<unsigned>(plat::linkRadioBulkHigh()),
                 static_cast<unsigned>(g_bulkWin));
        b.rowText(s, Color::Grey, line);
    }
    if (const uint8_t slow = plat::linkRadioSlowPeers()) {
        snprintf(line, sizeof(line), wide ? "%u device%s at 1 Mbps after failed sends, 24 again after 30 s clean."
                                          : "%u device%s at 1 Mbps (failed sends).",
                 static_cast<unsigned>(slow), slow == 1 ? "" : "s");
        b.rowText(s, Color::Yellow, line);
    }
    // A shared satellite: one channel for all its boards, said plainly.
    for (uint8_t i = 0; i < Engine::kPeers; ++i) {
        if (!e.peerUsed(i) || c->shared[i].n < 2) continue;
        const char* nm = c->meta[i].name;
        if (!e.peerUp(i)) {
            const unsigned was = c->meta[i].chan, now = plat::linkRadioChannel();
            if (!was || was == now) continue;
            if (wide) {
                snprintf(line, sizeof(line), "%s is shared with %u board%s; all must be on one Wi-Fi channel. This board",
                         nm, c->shared[i].n - 1u, c->shared[i].n == 2 ? "" : "s");
                b.rowText(s, Color::Yellow, line);
                snprintf(line, sizeof(line), "is on %u now: %s was last heard on %u.", now, nm, was);
                b.rowText(s, Color::Yellow, line);
            } else {
                snprintf(line, sizeof(line), "%s is shared with %u board%s; all", nm, c->shared[i].n - 1u,
                         c->shared[i].n == 2 ? "" : "s");
                b.rowText(s, Color::Yellow, line);
                b.rowText(s, Color::Yellow, "must be on one Wi-Fi channel. This");
                snprintf(line, sizeof(line), "board is on %u, %s was on %u.", now, nm, was);
                b.rowText(s, Color::Yellow, line);
            }
            continue;
        }
        for (uint8_t k = 0; k < c->shared[i].n; ++k) {
            const ulink::SharedBoard& o = c->shared[i].b[k];
            if ((o.flags & ulink::SB_YOU) || (o.flags & ulink::SB_HEARD)) continue;
            if (wide) {
                snprintf(line, sizeof(line), "%s is not heard: all of %s's boards must be on one Wi-Fi channel.",
                         o.name, nm);
                b.rowText(s, Color::Yellow, line);
            } else {
                snprintf(line, sizeof(line), "%s is not heard: all of", o.name);
                b.rowText(s, Color::Yellow, line);
                snprintf(line, sizeof(line), "%s's boards need one channel.", nm);
                b.rowText(s, Color::Yellow, line);
            }
        }
    }
    if (e.peerCount() >= Engine::kPeers) b.rowText(s, Color::Yellow, "All 8 pairings are taken: LINK FORGET frees one.");
    b.rowRule(s);
    b.prompt(s);
}

void cmdPair(Bbs& b, Session& s) {
    Ctx* c = g_ctx;
    if (c->pairNode != 0xFF) {
        say(s, Color::LightRed, "Somebody is pairing a device already.");
        b.prompt(s);
        return;
    }
    if (c->eng->peerCount() >= Engine::kPeers) {
        say(s, Color::LightRed, "All 8 pairings are taken. LINK FORGET frees one.");
        b.prompt(s);
        return;
    }
    if (!b.own(s, g_index)) { b.prompt(s); return; }
    b.setDoing(s, "LINK PAIR");
    c->pairNode = s.id;
    c->pairStep = 1;
    c->pairedAs = -1;
    c->asking = ulink::PairInfo();
    c->eng->openPairing(kPairMs);
    say(s, Color::Cyan, "Pairing is open for 2 minutes.");
    s.term.nl(s.tl);
    say(s, Color::Grey, "Put the device in pairing mode. Q stops.");
}

// pairingArg: "n" (from LINK, counted from 1) to a pairing index, -1 when
// there is no such pairing. rest, if given, is what follows the number.
int pairingArg(const char* arg, const char** rest = nullptr) {
    Ctx* c = g_ctx;
    char* end = nullptr;
    const long n = strtol(arg, &end, 10);
    if (!*arg || end == arg || n < 1 || n > Engine::kPeers || !c->eng->peerUsed(static_cast<uint8_t>(n - 1))) return -1;
    if (rest) {
        while (*end == ' ') ++end;
        *rest = end;
    }
    return static_cast<int>(n - 1);
}

void cmdForget(Bbs& b, Session& s, const char* arg) {
    Ctx* c = g_ctx;
    const int n = pairingArg(arg);
    if (n < 0) {
        say(s, Color::LightRed, "LINK FORGET n, with n from LINK.");
        b.prompt(s);
        return;
    }
    const uint8_t i = static_cast<uint8_t>(n);
    const bool told = c->eng->peerUp(i);
    const bool shared = c->shared[i].n > 1;
    char name[17], buf[96];
    snprintf(name, sizeof(name), "%s", c->meta[i].name);
    plat::log("link: forgot peer %u \"%s\"%s", i + 1u, name, told ? "" : " (out of reach)");
    // Told first when it is up (UNPAIR), so a shared satellite frees this
    // board's slot and its other boards hear it.
    c->eng->forget(i);
    c->meta[i] = Meta();
    c->shared[i] = Shared();
    savePeers();
    if (told)
        snprintf(buf, sizeof(buf), "Forgot %s. It was told.", name);
    else if (b.rowWidth(s) >= 60)
        snprintf(buf, sizeof(buf), shared ? "Forgot %s here. Out of reach, it still counts this board: revoke or reset."
                                          : "Forgot %s here. It was out of reach.", name);
    else
        snprintf(buf, sizeof(buf), "Forgot here. %s was out of reach.", name);
    say(s, Color::LightGreen, buf);
    b.prompt(s);
}

void cmdName(Bbs& b, Session& s, const char* arg) {
    Ctx* c = g_ctx;
    const char* rest = nullptr;
    const int n = pairingArg(arg, &rest);
    if (n < 0 || !rest || !*rest) {
        say(s, Color::LightRed, "LINK NAME n name, with n from LINK.");
        b.prompt(s);
        return;
    }
    // One word: SNAPSHOT and SATS take a satellite by name, and a name with
    // a space could never be picked.
    char name[17];
    size_t k = 0;
    for (const char* p = rest; *p && k + 1 < sizeof(name); ++p) {
        if (*p == ' ') {
            say(s, Color::LightRed, "One word: SNAPSHOT takes it by name.");
            b.prompt(s);
            return;
        }
        if (*p > 0x20 && *p < 0x7F) name[k++] = *p;
    }
    name[k] = '\0';
    bool digits = name[0] != '\0';
    for (const char* p = name; *p; ++p) digits = digits && *p >= '0' && *p <= '9';
    if (digits) {
        say(s, Color::LightRed, "Not all digits: SNAPSHOT 3 is a number.");
        b.prompt(s);
        return;
    }
    if (nameTaken(name, n)) {
        say(s, Color::LightRed, "Already a camera's name. Pick another.");
        b.prompt(s);
        return;
    }
    snprintf(c->meta[n].name, sizeof(c->meta[n].name), "%s", name);
    savePeers();
    say(s, Color::LightGreen, "Renamed.");
    b.prompt(s);
}

// LINK SHARE n: the owner lets one more board pair with satellite n, for two
// minutes (PAIR_OPEN). Only the owner may; the satellite checks that too.
void cmdShare(Bbs& b, Session& s, const char* arg) {
    Ctx* c = g_ctx;
    const int n = pairingArg(arg);
    if (n < 0) {
        say(s, Color::LightRed, "LINK SHARE n, with n from LINK.");
        b.prompt(s);
        return;
    }
    const uint8_t i = static_cast<uint8_t>(n);
    const bool wide = b.rowWidth(s) >= 60;
    const char* nm = c->meta[i].name;
    char buf[112];
    if (ownsIt(i) == 0) {
        snprintf(buf, sizeof(buf), wide ? "Only %s's owner, %s, can share it." : "Only %s's owner shares it.", nm,
                 ownerName(i));
    } else if (c->shared[i].n >= Engine::kHosts) {
        snprintf(buf, sizeof(buf), wide ? "%s has %u boards, the most one satellite takes. Revoke one first."
                                        : "%s has %u boards, the most.", nm, static_cast<unsigned>(Engine::kHosts));
    } else if (!c->eng->peerUp(i) || !c->eng->sharePairing(i, static_cast<uint16_t>(kPairMs / 1000))) {
        snprintf(buf, sizeof(buf), "%s is not answering. Try later.", nm);
    } else {
        plat::log("link: \"%s\" takes one more board for 2 minutes", nm);
        snprintf(buf, sizeof(buf), wide ? "%s takes one more board for 2 minutes: run LINK PAIR on that board now."
                                        : "Open for 2 min. LINK PAIR there now.", nm);
        say(s, Color::LightGreen, buf);
        b.prompt(s);
        return;
    }
    say(s, Color::LightRed, buf);
    b.prompt(s);
}

// LINK REVOKE n board: the owner has another board forgotten by satellite n.
// board is its number among the other boards under n in LINK, or its name.
void cmdRevoke(Bbs& b, Session& s, const char* arg) {
    Ctx* c = g_ctx;
    const char* rest = nullptr;
    const int n = pairingArg(arg, &rest);
    if (n < 0 || !rest || !*rest) {
        say(s, Color::LightRed, "LINK REVOKE n board, with n from LINK.");
        b.prompt(s);
        return;
    }
    const uint8_t i = static_cast<uint8_t>(n);
    const Shared& sh = c->shared[i];
    const ulink::SharedBoard* who = nullptr;
    char* end = nullptr;
    const long k = strtol(rest, &end, 10);
    uint8_t seen = 0;
    for (uint8_t j = 0; j < sh.n && !who; ++j) {
        if (sh.b[j].flags & ulink::SB_YOU) continue;
        ++seen;
        if ((end != rest && *end == '\0' && k == seen) || !strcasecmp(sh.b[j].name, rest)) who = &sh.b[j];
    }
    char buf[96];
    if (ownsIt(i) != 1) {
        snprintf(buf, sizeof(buf), "Only %s's owner revokes a board.", c->meta[i].name);
    } else if (!who) {
        snprintf(buf, sizeof(buf), "No board by that name on %s. LINK lists them.", c->meta[i].name);
    } else if (!c->eng->peerUp(i) || !c->eng->revoke(i, who->mac)) {
        snprintf(buf, sizeof(buf), "%s is not answering. Try later.", c->meta[i].name);
    } else {
        plat::log("link: revoked \"%s\" from \"%s\"", who->name, c->meta[i].name);
        snprintf(buf, sizeof(buf), "Revoked %s.", who->name);
        say(s, Color::LightGreen, buf);
        b.prompt(s);
        return;
    }
    say(s, Color::LightRed, buf);
    b.prompt(s);
}

void cmdLink(Bbs& b, Session& s, const char* arg, uint32_t) {
    if (!needLink(b, s)) return;
    while (*arg == ' ') ++arg;
    auto word = [&](const char* w) {
        size_t n = strlen(w);
        return !strncasecmp(arg, w, n) && (arg[n] == '\0' || arg[n] == ' ');
    };
    const bool sysop = s.level == Access::Sysop;
    if (!*arg) { cmdList(b, s); return; }
    static const char* const kSysopWords[] = { "PAIR", "FORGET", "NAME", "SHARE", "REVOKE" };
    for (const char* w : kSysopWords) {
        if (!word(w)) continue;
        if (!sysop) { say(s, Color::LightRed, "Only the sysop pairs and forgets devices."); b.prompt(s); return; }
        const char* rest = arg + strlen(w);
        while (*rest == ' ') ++rest;
        if (!strcmp(w, "PAIR"))        cmdPair(b, s);
        else if (!strcmp(w, "FORGET")) cmdForget(b, s, rest);
        else if (!strcmp(w, "NAME"))   cmdName(b, s, rest);
        else if (!strcmp(w, "SHARE"))  cmdShare(b, s, rest);
        else                           cmdRevoke(b, s, rest);
        return;
    }
    say(s, Color::LightRed, b.rowWidth(s) >= 60 ? "LINK, LINK PAIR, FORGET n, NAME n name, SHARE n or REVOKE n board."
                                                : "LINK [PAIR|FORGET|NAME|SHARE|REVOKE]");
    b.prompt(s);
}
const Command kCommands[] = {
    { "LINK", "", 0, CF_READ, "LINK [PAIR]", "the radio link: devices, pairing", cmdLink, Menu::Staff, 60 },
};

const char* status() {
    static char buf[40];
    Ctx* c = g_ctx;
    if (!c || !c->eng) return nullptr;
    snprintf(buf, sizeof(buf), "link ch %u, %u of %u up", plat::linkRadioChannel(), c->eng->peersUp(),
             c->eng->peerCount());
    return buf;
}

}  // namespace

// ---------------------------------------------------------------------------
// For other plugins (plugins/link.h)
// ---------------------------------------------------------------------------
namespace linkp {

bool registerFamily(const Family& f) {
    if (f.id == ulink::FAM_LINK || f.id == 0xFF) return false;
    for (const Family*& slot : g_fam) {
        if (slot && slot->id == f.id) {
            if (slot == &f) return true;
            plat::log("link: family %u is \"%s\"'s, not \"%s\"'s", f.id, slot->name, f.name);
            return false;
        }
    }
    for (const Family*& slot : g_fam) if (!slot) { slot = &f; return true; }
    plat::log("link: no room for family \"%s\"", f.name);
    return false;
}

void unregisterFamily(uint8_t id) {
    for (const Family*& slot : g_fam) if (slot && slot->id == id) slot = nullptr;
}

ulink::Engine* engine() { return g_ctx ? g_ctx.load()->eng : nullptr; }

const char* peerName(uint8_t peer) {
    if (!g_ctx || peer >= Engine::kPeers || !g_ctx.load()->eng->peerUsed(peer)) return "";
    return g_ctx.load()->meta[peer].name;
}

uint8_t peerRecv(uint8_t peer) {
    Ctx* c = g_ctx;
    if (!c || peer >= Engine::kPeers || !c->eng->peerUsed(peer)) return linkfam::RECV_ALL;
    return c->meta[peer].recv;
}

uint8_t peerCamNo(uint8_t peer) {
    Ctx* c = g_ctx;
    if (!c || peer >= Engine::kPeers || !c->eng->peerUsed(peer)) return 0;
    return c->meta[peer].camno;
}

uint8_t channel() { return g_ctx ? plat::linkRadioChannel() : 0; }

bool onWire() {
#ifdef BBS_HAS_ETH
    return plat::ethInfo().up;
#else
    return false;
#endif
}

bool satInfo(uint8_t peer, SatInfo& out) {
    out = SatInfo();
    Ctx* c = g_ctx;
    if (!c || !c->eng || peer >= Engine::kPeers || !c->eng->peerUsed(peer)) return false;
    Engine& e = *c->eng;
    const Meta& m = c->meta[peer];
    const ulink::PeerStats& st = e.peerStats(peer);
    out.up = e.peerUp(peer);
    snprintf(out.name, sizeof(out.name), "%s", m.name);
    out.kind = m.kind;
    out.mac = e.peerMac(peer);
    out.rssi = st.rssi;
    out.farRssi = st.farRssi;
    out.lastHeard = st.lastHeard;
    out.rx = st.rx;
    out.tx = st.tx;
    out.retries = st.retries;
    out.drops = st.drops;
    out.slow = plat::linkRadioPeerSlow(out.mac.b);
    out.chan = out.up ? plat::linkRadioChannel() : m.chan;
    out.recv = m.recv;
    out.camno = m.camno;
    out.checked = m.checked;
    const Shared& sh = c->shared[peer];
    out.boards = sh.n;
    out.owned = static_cast<int8_t>(ownsIt(peer));
    snprintf(out.owner, sizeof(out.owner), "%s", ownerName(peer));
    for (uint8_t k = 0; k < sh.n && out.nothers < ulink::Engine::kHosts; ++k)
        if (!(sh.b[k].flags & ulink::SB_YOU)) out.others[out.nothers++] = sh.b[k];
    uint8_t d[32];
    linkcrypto::sha256(e.peerKey(peer), linkcrypto::kKey, d);
    snprintf(out.fp, sizeof(out.fp), "%02X%02X %02X%02X %02X%02X %02X%02X", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
    linkcrypto::wipe(d, sizeof(d));
    return true;
}

bool ask(Bbs& b, Session& s, uint8_t peer, uint8_t what) {
    Ctx* c = g_ctx;
    if (!c || !c->eng || peer >= Engine::kPeers || !c->eng->peerUsed(peer) || c->askNode != 0xFF) return false;
    if (!b.own(s, g_index)) return false;
    c->askNode = s.id;
    c->askPeer = peer;
    c->askWhat = what;
    const bool wide = b.rowWidth(s) >= 60;
    char q[96];
    if (what == ASK_SHARE)
        snprintf(q, sizeof(q), wide ? "Open %s to one more board for 2 minutes? (y/N) " : "Share %s for 2 minutes? (y/N) ",
                 c->meta[peer].name);
    else
        snprintf(q, sizeof(q), wide ? "Unpair %s? It is forgotten on this board. (y/N) " : "Unpair %s? (y/N) ",
                 c->meta[peer].name);
    s.term.color(s.tl, Color::Yellow);
    s.term.text(s.tl, q);
    s.term.color(s.tl, Color::White);
    return true;
}

bool setSat(uint8_t peer, const char* name, uint8_t camno, uint8_t recv, char* why, size_t n) {
    Ctx* c = g_ctx;
    if (!c || !c->eng || peer >= Engine::kPeers || !c->eng->peerUsed(peer)) {
        snprintf(why, n, "That satellite is not paired now");
        return false;
    }
    for (const char* p = name; *p; ++p)
        if (*p == ' ') { snprintf(why, n, "One word: SNAPSHOT takes it by name"); return false; }
    if (!*name) { snprintf(why, n, "A satellite needs a name"); return false; }
    bool digits = true;
    for (const char* p = name; *p; ++p) digits = digits && *p >= '0' && *p <= '9';
    if (digits) { snprintf(why, n, "Not all digits: SNAPSHOT 3 is a number"); return false; }
    if (nameTaken(name, peer)) { snprintf(why, n, "Already a camera's name. Pick another"); return false; }
    if (camno && (camno < 2 || camno > 9)) { snprintf(why, n, "2 to 9, or auto"); return false; }
    for (uint8_t i = 0; camno && i < Engine::kPeers; ++i)
        if (i != peer && c->eng->peerUsed(i) && c->meta[i].camno == camno) {
            snprintf(why, n, "Camera %u is %s's. Pick another", camno, c->meta[i].name);
            return false;
        }
    Meta& m = c->meta[peer];
    const bool recvChanged = m.recv != (recv & linkfam::RECV_ALL) || m.camno != camno || strcmp(m.name, name);
    snprintf(m.name, sizeof(m.name), "%s", name);
    m.camno = camno;
    m.recv = static_cast<uint8_t>(recv & linkfam::RECV_ALL);
    savePeers();
    // The camera family (camsat) sends SETTINGS when a satellite comes up;
    // a change of what this board takes goes to it now.
    if (recvChanged)
        for (const linkp::Family* f : g_fam)
            if (f && f->settingsChanged) f->settingsChanged(peer);
    return true;
}

int peerOfKind(uint8_t kind, uint8_t n) {
    if (!g_ctx) return -1;
    for (uint8_t i = 0; i < Engine::kPeers; ++i) {
        if (!g_ctx.load()->eng->peerUsed(i) || g_ctx.load()->eng->peerKind(i) != kind) continue;
        if (n-- == 0) return i;
    }
    return -1;
}

}  // namespace linkp

bool linkHwRow(char* val, size_t valN, char* note, size_t noteN, bool& warn) {
    Ctx* c = g_ctx;
    warn = false;
    if (!c || !c->eng) return false;
    // The comma: SYS joins the value and the note with a space, and plain
    // ASCII read "ch 6 0 of 0" (tty-ux-sats).
    snprintf(val, valN, "on, ch %u,", plat::linkRadioChannel());
    if (c->eng->peerCount() >= Engine::kPeers) {
        snprintf(note, noteN, "%u up, peers full", c->eng->peersUp());
        warn = true;
    } else {
        snprintf(note, noteN, "%u of %u up", c->eng->peersUp(), c->eng->peerCount());
    }
    return true;
}

extern const Plugin kLinkPlugin = {
    { kName, "The unleashed link (ESP-NOW)", "1.0", 20 * 1024, 1024, PF_CORE | PF_FAST,
      PlugLevel::Staff, PlugLevel::Staff, PlugLevel::Sysop },
    start,
    stop,
    tick,
    nullptr,                 // onConnect
    nullptr,                 // onLogin
    onLogoff,
    onKey,
    status,
    kCommands,
    sizeof(kCommands) / sizeof(kCommands[0]),
    nullptr,                 // settings: Enabled and the levels, the core's rows
    0,
    nullptr,                 // setting
    nullptr,                 // rows
    nullptr,                 // onPresence
    nullptr,                 // onBytes
    nullptr,                 // onRename
    nullptr,                 // listDone
};
