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
//               plugin's tick, every 20 ms (PF_FAST), at most eight frames a
//               call; a bulk message's reassembly and the pairing arithmetic
//               run as one job on the background runner (core/runner.h,
//               1.1.2). On a tree without the runner (this branch before the
//               1.1.2 merge) the same job is called from tick, which is the
//               one place that breaks the rule, and only until the merge.
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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <strings.h>

#include "../core/bbs.h"
#include "../core/clock.h"
#include "../core/linkcrypto.h"
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
constexpr uint8_t    kRingSlots  = 8;
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
};

struct Ctx;
Ctx* g_ctx = nullptr;
Ctx* g_grave = nullptr;                // a stopped link the runner has not let go of yet

// The radio, as the engine sees it.
class Radio : public ulink::Io {
public:
    bool send(const Mac* to, const uint8_t* f, size_t n) override {
        return plat::linkRadioSend(to ? to->b : nullptr, f, n);
    }
    bool idle() override { return plat::linkRadioIdle(); }
    size_t recv(uint8_t* out, size_t cap, Mac& from, int8_t& rssi) override {
        return plat::linkRadioRecv(out, cap, from.b, rssi);
    }
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
    // the sysop pairing, if one is: their node, and where they are in it
    uint8_t          pairNode = 0xFF;
    uint8_t          pairStep = 0;     // 1 waiting, 2 asked, 3 paired: codes match?
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

int rngRunner(void*, unsigned char* out, size_t n) {
    for (size_t i = 0; i < n; i += 4) {
        uint32_t r = plat::random32();
        for (size_t k = 0; k < 4 && i + k < n; ++k) out[i + k] = static_cast<unsigned char>(r >> (8 * k));
    }
    return 0;
}

// The work the loop hands off: pairing's arithmetic and a picture's bytes.
void work(Ctx& c) {
    if (c.pjTaken) Engine::pairRun(c.pj, rngRunner, nullptr);
    if (c.eng) {
        while (c.eng->pumpBulk(8)) {
#ifdef LINK_HAS_RUNNER
            runner::breathe();
#endif
        }
    }
}

#ifdef LINK_HAS_RUNNER
void jobWork(runner::Job& j) {
    work(*static_cast<Ctx::Job&>(j).ctx);
}
#endif

// ---------------------------------------------------------------------------
// The families
// ---------------------------------------------------------------------------
const linkp::Family* fam(uint8_t id) {
    for (const linkp::Family* f : g_fam) if (f && f->id == id) return f;
    return nullptr;
}

bool evMessage(void*, uint8_t peer, uint16_t sess, uint8_t family, uint8_t type, const uint8_t* p, size_t n) {
    const linkp::Family* f = fam(family);
    if (!f || !f->message) return true;             // nobody listening: taken and dropped
    return f->message(peer, sess, type, p, n);
}
bool evBulkBegin(void*, uint8_t peer, uint16_t sess, uint8_t family, uint8_t type, uint32_t total) {
    const linkp::Family* f = fam(family);
    return f && f->bulkBegin && f->bulkBegin(peer, sess, type, total);
}
bool evBulkData(void*, uint8_t peer, uint16_t sess, uint8_t family, const uint8_t* p, size_t n) {
    const linkp::Family* f = fam(family);
    return f && f->bulkData ? f->bulkData(peer, sess, p, n) : false;
}
void evBulkEnd(void*, uint8_t peer, uint16_t sess, uint8_t family, bool ok) {
    const linkp::Family* f = fam(family);
    if (f && f->bulkEnd) f->bulkEnd(peer, sess, ok);
}
void evBulkSent(void*, uint8_t peer, uint16_t sess, uint8_t family, bool ok) {
    const linkp::Family* f = fam(family);
    if (f && f->bulkSent) f->bulkSent(peer, sess, ok);
}
void evReset(void*, uint8_t peer, uint16_t sess, uint8_t family, uint8_t reason) {
    const linkp::Family* f = fam(family);
    if (f && f->reset) f->reset(peer, sess, reason);
}
void evPeerState(void*, uint8_t peer, bool up) {
    if (g_ctx && peer < Engine::kPeers)
        plat::log("link: %s \"%s\" %s", ulink::kindName(g_ctx->meta[peer].kind), g_ctx->meta[peer].name,
                  up ? "is up" : "went quiet");
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

// One line a pairing: mac kind checked pairedAt key name. The name last,
// because it may have spaces.
void savePeers() {
    Ctx* c = g_ctx;
    if (!c || !c->eng) return;
    char path[128], tmp[136];
    if (!plugins::path(g_index, kPeersFile, path, sizeof(path))) {
        plat::log("link: cannot write the pairings (no room on userdata?)");
        return;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "# unleashed link pairings: mac kind checked paired key name\n");
    for (uint8_t i = 0; i < Engine::kPeers; ++i) {
        if (!c->eng->peerUsed(i)) continue;
        char mac[18], key[33];
        macText(c->eng->peerMac(i), mac, sizeof(mac));
        const uint8_t* k = c->eng->peerKey(i);
        for (size_t b = 0; b < linkcrypto::kKey; ++b) snprintf(key + 2 * b, 3, "%02x", k[b]);
        fprintf(f, "%s %u %u %u %s %s\n", mac, c->meta[i].kind, c->meta[i].checked ? 1u : 0u,
                static_cast<unsigned>(c->meta[i].pairedAt), key, c->meta[i].name);
        linkcrypto::wipe(key, sizeof(key));
    }
    const bool ok = fflush(f) == 0;
    fclose(f);
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
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char mac[24] = {}, key[40] = {};
        unsigned kind = 0, checked = 0, at = 0;
        int used = 0;
        if (sscanf(line, "%23s %u %u %u %39s %n", mac, &kind, &checked, &at, key, &used) < 5) continue;
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
    }
    linkcrypto::wipe(line, sizeof(line));
    fclose(f);
}

// ---------------------------------------------------------------------------
// Pairing, from the sysop's side
// ---------------------------------------------------------------------------
void evPairAsk(void*, const ulink::PairInfo& who) {
    if (!g_ctx) return;
    g_ctx->asking = who;
}

void evPaired(void*, uint8_t peer, const ulink::PairInfo& who) {
    Ctx* c = g_ctx;
    if (!c || peer >= Engine::kPeers) return;
    Meta& m = c->meta[peer];
    snprintf(m.name, sizeof(m.name), "%s", who.name[0] ? who.name : ulink::kindName(who.kind));
    m.kind = who.kind;
    m.checked = false;
    m.pairedAt = clk::valid() ? clk::epoch() : 0;
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

void onKey(Session& s, int k, uint32_t now) {
    (void)now;
    Ctx* c = g_ctx;
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
            if (c->pairedAs >= 0) {
                c->meta[c->pairedAs].checked = yes;
                savePeers();
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
    if (g_ctx && s.id == g_ctx->pairNode) pairEnd(nullptr, Color::Grey, "");
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

bool jobBusy(Ctx* c) {
#ifdef LINK_HAS_RUNNER
    return c && !runner::idle(c->job);
#else
    (void)c;
    return false;
#endif
}

bool start(Bbs& bbs) {
    (void)bbs;
    g_index = plugins::indexOf(kName);
    if (g_grave && !jobBusy(g_grave)) { freeCtx(g_grave); g_grave = nullptr; }
    if (!plat::linkRadioStart(kRingSlots)) {
        plat::log("link: no radio (ESP-NOW would not start)");
        return true;                  // the commands stay, and say why
    }
    void* mem = plat::linkAlloc(sizeof(Ctx));
    if (!mem) { plat::linkRadioStop(); return false; }
    Ctx* c = new (mem) Ctx();
    const uint8_t win = 16;
    c->win = static_cast<uint8_t*>(plat::linkAlloc(static_cast<size_t>(win) * ulink::kPayloadMax));
    ulink::Events ev;
    ev.message   = evMessage;
    ev.bulkBegin = evBulkBegin;
    ev.bulkData  = evBulkData;
    ev.bulkEnd   = evBulkEnd;
    ev.bulkSent  = evBulkSent;
    ev.reset     = evReset;
    ev.peerState = evPeerState;
    ev.pairAsk   = evPairAsk;
    ev.paired    = evPaired;
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
    plat::log("link: on, channel %u, %u pairing%s", plat::linkRadioChannel(), c->eng->peerCount(),
              c->eng->peerCount() == 1 ? "" : "s");
    return true;
}

void stop() {
    Ctx* c = g_ctx;
    g_ctx = nullptr;
    if (!c) { plat::linkRadioStop(); return; }
    if (c->pairNode != 0xFF) {
        Session* s = nullptr;
        struct Find { uint8_t node; Session* s; } fd{ c->pairNode, nullptr };
        Bbs::instance().eachSession([](void* ctx, Session& ss) {
            Find* f = static_cast<Find*>(ctx);
            if (ss.st != SState::Free && ss.id == f->node) f->s = &ss;
        }, &fd);
        s = fd.s;
        if (s && Bbs::instance().owns(*s, g_index)) Bbs::instance().release(*s);
    }
    plat::linkRadioStop();
    // The runner may still be inside this engine: leave it to be freed once
    // the job is back.
    if (jobBusy(c)) { g_grave = c; return; }
    freeCtx(c);
}

void tick(uint32_t now) {
    (void)now;
    if (g_grave && !jobBusy(g_grave)) { freeCtx(g_grave); g_grave = nullptr; }
    Ctx* c = g_ctx;
    if (!c || !c->eng) return;
    c->eng->poll();

    // Hand the slow work off.
#ifdef LINK_HAS_RUNNER
    if (runner::done(c->job)) {
        runner::collect(c->job);
        if (c->pjTaken) { c->eng->pairGive(c->pj); c->pjTaken = false; }
    }
    if (runner::idle(c->job)) {
        if (!c->pjTaken && c->eng->pairComputeWanted()) c->pjTaken = c->eng->pairTake(c->pj);
        if (c->pjTaken || c->eng->bulkWaiting()) runner::post(c->job);
    }
#else
    // Until the 1.1.2 runner is merged: on the loop, bounded. The pairing
    // arithmetic is one slow pass per pairing, a sysop standing at the board.
    if (!c->pjTaken && c->eng->pairComputeWanted()) c->pjTaken = c->eng->pairTake(c->pj);
    work(*c);                         // at most a window's worth of fragments
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
    if (g_ctx && g_ctx->eng) return true;
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

void cmdList(Bbs& b, Session& s) {
    Ctx* c = g_ctx;
    Engine& e = *c->eng;
    char right[24], line[96];
    snprintf(right, sizeof(right), "channel %u", plat::linkRadioChannel());
    b.rowTitle(s, "Radio link", right);
    const bool wide = b.rowWidth(s) >= 60;
    if (!e.peerCount()) {
        b.rowText(s, Color::Grey, "No devices paired. LINK PAIR pairs one.");
    } else {
        b.rowText(s, Color::Cyan, wide ? " #  Name             Kind     State  RSSI  Heard  Checked"
                                       : " #  Name       Kind    State RSSI");
        const uint32_t now = plat::millis();
        for (uint8_t i = 0; i < Engine::kPeers; ++i) {
            if (!e.peerUsed(i)) continue;
            const ulink::PeerStats& st = e.peerStats(i);
            char heard[8] = "-";
            if (st.lastHeard) ago(now - st.lastHeard, heard, sizeof(heard));
            if (wide) {
                snprintf(line, sizeof(line), " %u  %-16.16s %-8s %-6s %4d  %-5s  %s", i, c->meta[i].name,
                         ulink::kindName(c->meta[i].kind), e.peerUp(i) ? "up" : "down",
                         static_cast<int>(st.rssi), heard, c->meta[i].checked ? "yes" : "no");
            } else {
                snprintf(line, sizeof(line), " %u  %-10.10s %-7.7s %-5s %4d", i, c->meta[i].name,
                         ulink::kindName(c->meta[i].kind), e.peerUp(i) ? "up" : "down", static_cast<int>(st.rssi));
            }
            b.rowText(s, e.peerUp(i) ? Color::LightGreen : Color::Grey, line);
        }
    }
    uint32_t rx = 0, tx = 0, re = 0;
    for (uint8_t i = 0; i < Engine::kPeers; ++i) {
        if (!e.peerUsed(i)) continue;
        rx += e.peerStats(i).rx;
        tx += e.peerStats(i).tx;
        re += e.peerStats(i).retries;
    }
    snprintf(line, sizeof(line), "Frames in %u, out %u, retried %u, dropped %u.", static_cast<unsigned>(rx),
             static_cast<unsigned>(tx), static_cast<unsigned>(re), static_cast<unsigned>(e.dropsTotal()));
    b.rowText(s, Color::Grey, line);
    if (e.dropsTotal()) {
        char* p = line;
        size_t left = sizeof(line);
        int w = snprintf(p, left, "Dropped:");
        for (uint8_t d = 0; d < ulink::D_COUNT && w > 0 && static_cast<size_t>(w) < left; ++d) {
            if (!e.drops(d)) continue;
            p += w;
            left -= static_cast<size_t>(w);
            w = snprintf(p, left, " %s %u", ulink::dropName(d), static_cast<unsigned>(e.drops(d)));
        }
        b.rowText(s, Color::Grey, line);
    }
    // Rule no. 1, read off the board: the loop's cost per frame and how full
    // the receive ring has been (LINK.md, "Where the work runs").
    snprintf(line, sizeof(line), "Per frame %u us, worst %u. Ring high %u of %u.",
             static_cast<unsigned>(e.frameUsAvg()), static_cast<unsigned>(e.frameUsMax()),
             static_cast<unsigned>(plat::linkRadioRingHigh()), static_cast<unsigned>(kRingSlots));
    b.rowText(s, Color::Grey, line);
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

void cmdForget(Bbs& b, Session& s, const char* arg) {
    Ctx* c = g_ctx;
    char* end = nullptr;
    long n = strtol(arg, &end, 10);
    if (!*arg || end == arg || n < 0 || n >= Engine::kPeers || !c->eng->peerUsed(static_cast<uint8_t>(n))) {
        say(s, Color::LightRed, "LINK FORGET n, with n from LINK.");
        b.prompt(s);
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "Forgot %s.", c->meta[n].name);
    plat::log("link: forgot peer %ld \"%s\"", n, c->meta[n].name);
    c->eng->removePeer(static_cast<uint8_t>(n));
    c->meta[n] = Meta();
    savePeers();
    say(s, Color::LightGreen, buf);
    b.prompt(s);
}

void cmdName(Bbs& b, Session& s, const char* arg) {
    Ctx* c = g_ctx;
    char* end = nullptr;
    long n = strtol(arg, &end, 10);
    while (end && *end == ' ') ++end;
    if (!*arg || end == arg || n < 0 || n >= Engine::kPeers || !c->eng->peerUsed(static_cast<uint8_t>(n)) ||
        !end || !*end) {
        say(s, Color::LightRed, "LINK NAME n name, with n from LINK.");
        b.prompt(s);
        return;
    }
    char name[17];
    size_t k = 0;
    for (const char* p = end; *p && k + 1 < sizeof(name); ++p)
        if (*p >= 0x20 && *p < 0x7F) name[k++] = *p;
    name[k] = '\0';
    snprintf(c->meta[n].name, sizeof(c->meta[n].name), "%s", name);
    savePeers();
    say(s, Color::LightGreen, "Renamed.");
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
    if (word("PAIR") || word("FORGET") || word("NAME")) {
        if (!sysop) { say(s, Color::LightRed, "Only the sysop pairs and forgets devices."); b.prompt(s); return; }
        if (word("PAIR"))   { cmdPair(b, s); return; }
        const char* rest = arg + (word("FORGET") ? 6 : 4);
        while (*rest == ' ') ++rest;
        if (word("FORGET")) cmdForget(b, s, rest);
        else                cmdName(b, s, rest);
        return;
    }
    say(s, Color::LightRed, "LINK, LINK PAIR, LINK FORGET n or LINK NAME n name.");
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

ulink::Engine* engine() { return g_ctx ? g_ctx->eng : nullptr; }

const char* peerName(uint8_t peer) {
    if (!g_ctx || peer >= Engine::kPeers || !g_ctx->eng->peerUsed(peer)) return "";
    return g_ctx->meta[peer].name;
}

int peerOfKind(uint8_t kind, uint8_t n) {
    if (!g_ctx) return -1;
    for (uint8_t i = 0; i < Engine::kPeers; ++i) {
        if (!g_ctx->eng->peerUsed(i) || g_ctx->eng->peerKind(i) != kind) continue;
        if (n-- == 0) return i;
    }
    return -1;
}

}  // namespace linkp

bool linkHwRow(char* val, size_t valN, char* note, size_t noteN, bool& warn) {
    Ctx* c = g_ctx;
    warn = false;
    if (!c || !c->eng) return false;
    snprintf(val, valN, "on, ch %u", plat::linkRadioChannel());
    if (c->eng->peerCount() >= Engine::kPeers) {
        snprintf(note, noteN, "%u up, peers full", c->eng->peersUp());
        warn = true;
    } else {
        snprintf(note, noteN, "%u of %u up", c->eng->peersUp(), c->eng->peerCount());
    }
    return true;
}

extern const Plugin kLinkPlugin = {
    { kName, "The unleashed link (ESP-NOW)", "1.0", 12 * 1024, 1024, PF_CORE | PF_FAST,
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
