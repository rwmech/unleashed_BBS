// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         host/test_link.cpp
// Module:       Tests / the µnleashed link (1.2.0)
//
// Purpose:      The link's engine, both ends of it, through a simulated
//               radio that loses, duplicates and reorders frames and on
//               which the host can change channel, with no board and no
//               ESP-NOW. Plus the wire (CRC-16, COBS, the header) and the
//               cryptography against published vectors.
//
//               The medium delivers a frame only to a node on the channel
//               the sender was on, which is the rule that makes a router's
//               channel hop cut a satellite off until it rescans. Time is
//               simulated, a millisecond a step, so a run that covers minutes
//               of retries takes a fraction of a second.
//
//               The expected values for CRC-16 and HKDF are published ones
//               (CRC catalogue; RFC 5869 test case 1), typed in here, not
//               produced by the code under test.
//
// Targets:      the Linux host build (make test)
// See also:     LINK.md, src/core/link.h
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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "../src/core/link.h"
#include "../src/core/linkcrypto.h"
#include "mbedtls/ccm.h"

using namespace ulink;

static int fails = 0, passes = 0;

static void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passes; else ++fails;
}

// ---------------------------------------------------------------------------
// A deterministic PRNG, so a failing run is the same run every time.
// ---------------------------------------------------------------------------
struct Rand {
    uint64_t s;
    explicit Rand(uint64_t seed) : s(seed ? seed : 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<uint32_t>(s); }
    bool chance(uint32_t percent) { return next() % 100 < percent; }
};

// ---------------------------------------------------------------------------
// The medium
// ---------------------------------------------------------------------------
static uint32_t g_now = 1000;

struct Node;

struct Air {
    struct Frame {
        uint32_t at;
        Node* to;
        Mac from;
        std::vector<uint8_t> data;
    };
    std::vector<Node*> nodes;
    std::deque<Frame> flying;
    Rand rnd{ 12345 };
    uint32_t loss = 0, dup = 0, reorder = 0;     // percent
    std::vector<std::vector<uint8_t>> captured;  // every frame, for the replay tests
    void transmit(Node& from, const Mac* to, const uint8_t* f, size_t n);
    void step();
};

struct Node : Io {
    Air& air;
    Mac mac;
    uint8_t chan = 1;
    bool present = true;                  // false: switched off
    uint32_t busyUntil = 0;
    std::deque<std::pair<Mac, std::vector<uint8_t>>> inbox;
    Rand rnd;
    uint32_t chanMoves = 0;

    Node(Air& a, uint8_t last, uint64_t seed) : air(a), rnd(seed) {
        uint8_t m[6] = { 0x02, 0, 0, 0, 0, last };
        memcpy(mac.b, m, 6);
        air.nodes.push_back(this);
    }
    bool send(const Mac* to, const uint8_t* f, size_t n) override {
        if (!present) return false;
        busyUntil = g_now + 2;            // about a 250-byte frame's airtime at 1 Mbps
        air.transmit(*this, to, f, n);
        return true;
    }
    bool idle() override { return g_now >= busyUntil; }
    size_t recv(uint8_t* out, size_t cap, Mac& from, int8_t& rssi) override {
        if (inbox.empty()) return 0;
        auto& fr = inbox.front();
        size_t n = fr.second.size() < cap ? fr.second.size() : cap;
        memcpy(out, fr.second.data(), n);
        from = fr.first;
        rssi = -50;
        inbox.pop_front();
        return n;
    }
    uint32_t millis() override { return g_now; }
    uint32_t micros() override { return g_now * 1000; }
    void random(uint8_t* out, size_t n) override { for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(rnd.next()); }
    uint8_t channel() override { return chan; }
    void setChannel(uint8_t c) override { if (c != chan) ++chanMoves; chan = c; }
    uint32_t unixTime() override { return 1790000000u + g_now / 1000; }
};

void Air::transmit(Node& from, const Mac* to, const uint8_t* f, size_t n) {
    captured.emplace_back(f, f + n);
    for (Node* nd : nodes) {
        if (nd == &from || !nd->present) continue;
        if (to && !(nd->mac == *to)) continue;
        if (nd->chan != from.chan) continue;
        if (rnd.chance(loss)) continue;
        uint32_t delay = 1 + (rnd.chance(reorder) ? rnd.next() % 30 : 0);
        Frame fr{ g_now + delay, nd, from.mac, std::vector<uint8_t>(f, f + n) };
        flying.push_back(fr);
        if (rnd.chance(dup)) { fr.at += 1 + rnd.next() % 10; flying.push_back(fr); }
    }
}

void Air::step() {
    for (auto it = flying.begin(); it != flying.end();) {
        if (it->at <= g_now) {
            // Delivered only if the receiver is still on the channel: a
            // receiver that moved while the frame was in the air misses it.
            if (it->to->present) it->to->inbox.emplace_back(it->from, it->data);
            it = flying.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------
// One end: an engine and what its events told us.
// ---------------------------------------------------------------------------
struct End {
    Node node;
    std::vector<uint8_t> win;
    Engine* eng = nullptr;
    Events ev;
    // what arrived
    std::vector<std::string> msgs;       // "family/type/payload"
    std::vector<uint8_t> bulk;
    int bulkBegun = 0, bulkOk = 0, bulkBad = 0;
    int bulkSentOk = 0, bulkSentFail = 0;
    std::vector<uint8_t> resets;
    int ups = 0, downs = 0;
    bool asked = false;
    PairInfo askedWho;
    bool paired = false;
    PairInfo pairedWho;
    bool refuse = false;                 // message(): say "no room"
    uint32_t clockSeen = 0;

    End(Air& a, uint8_t last, Role role, uint8_t bulkWin, uint64_t seed)
        : node(a, last, seed), win(static_cast<size_t>(bulkWin) * kPayloadMax) {
        ev.ctx = this;
        ev.message = [](void* c, uint8_t, uint16_t, uint8_t fam, uint8_t type, const uint8_t* p, size_t n) {
            End& e = *static_cast<End*>(c);
            if (e.refuse) return false;
            e.msgs.push_back(std::to_string(fam) + "/" + std::to_string(type) + "/" +
                             std::string(reinterpret_cast<const char*>(p), n));
            return true;
        };
        ev.bulkBegin = [](void* c, uint8_t, uint16_t, uint8_t, uint8_t, uint32_t) {
            End& e = *static_cast<End*>(c);
            ++e.bulkBegun;
            e.bulk.clear();
            return true;
        };
        ev.bulkData = [](void* c, uint8_t, uint16_t, uint8_t, const uint8_t* p, size_t n) {
            End& e = *static_cast<End*>(c);
            e.bulk.insert(e.bulk.end(), p, p + n);
            return true;
        };
        ev.bulkEnd = [](void* c, uint8_t, uint16_t, uint8_t, bool ok) {
            End& e = *static_cast<End*>(c);
            if (ok) ++e.bulkOk; else ++e.bulkBad;
        };
        ev.bulkSent = [](void* c, uint8_t, uint16_t, uint8_t, bool ok) {
            End& e = *static_cast<End*>(c);
            if (ok) ++e.bulkSentOk; else ++e.bulkSentFail;
        };
        ev.reset = [](void* c, uint8_t, uint16_t, uint8_t, uint8_t r) { static_cast<End*>(c)->resets.push_back(r); };
        ev.peerState = [](void* c, uint8_t, bool up) { End& e = *static_cast<End*>(c); if (up) ++e.ups; else ++e.downs; };
        ev.pairAsk = [](void* c, const PairInfo& w) { End& e = *static_cast<End*>(c); e.asked = true; e.askedWho = w; };
        ev.paired = [](void* c, uint8_t, const PairInfo& w) { End& e = *static_cast<End*>(c); e.paired = true; e.pairedWho = w; };
        ev.clock = [](void* c, uint32_t t) { static_cast<End*>(c)->clockSeen = t; };
        eng = new Engine(role, node, ev, bulkWin, win.data());
    }
    ~End() { delete eng; }
};

// run: step the world for ms, the runner pumping every `pumpEvery` ms.
static uint32_t g_pumpEvery = 1;
static void run(Air& air, std::vector<End*> ends, uint32_t ms) {
    for (uint32_t i = 0; i < ms; ++i) {
        ++g_now;
        air.step();
        for (End* e : ends) {
            e->eng->poll();
            if (e->eng->pairComputeWanted()) e->eng->pairCompute();
            if (g_now % g_pumpEvery == 0) e->eng->pumpBulk(4);
        }
    }
}

template <class F>
static bool runUntil(Air& air, std::vector<End*> ends, uint32_t maxMs, F done) {
    for (uint32_t i = 0; i < maxMs; ++i) {
        if (done()) return true;
        run(air, ends, 1);
    }
    return done();
}

// pairUp: a fresh host and peer, paired and with the link up.
static bool pairUp(Air& air, End& host, End& peer, uint8_t hostChan) {
    host.node.chan = hostChan;
    peer.node.chan = 1;
    host.eng->setBoardName("Test Board");
    host.eng->openPairing(120000);
    peer.eng->setIdentity(KIND_DOORBOX, "t1.0", 0x6);
    peer.eng->startPairing(KIND_DOORBOX, "shelf", "t1.0");
    if (!runUntil(air, { &host, &peer }, 20000, [&] { return host.asked; })) return false;
    host.eng->pairAnswer(true);
    if (!runUntil(air, { &host, &peer }, 20000, [&] { return host.paired && peer.paired; })) return false;
    return runUntil(air, { &host, &peer }, 20000, [&] { return host.eng->peerUp(0) && peer.eng->hostUp(); });
}

// sendN: messages fmt % i for i in [from, to) from a to b on session s,
// waiting while the window is full. False, and says why, if the engine
// refuses one for good or the wait passes two simulated minutes.
static bool sendN(Air& air, End& a, End& b, uint16_t s, const char* fmt, int from, int to) {
    uint32_t guard = 0;
    for (int i = from; i < to;) {
        char m[16];
        snprintf(m, sizeof(m), fmt, i);
        int r = a.eng->send(0, s, FAM_DOOR, 5, m, strlen(m));
        if (r == 1) { ++i; continue; }
        if (r < 0 || ++guard > 120000) {
            printf("    (sending stopped at %d: %s)\n", i, r < 0 ? "refused" : "timed out");
            return false;
        }
        run(air, { &a, &b }, 1);
    }
    return true;
}

static bool inOrder(const std::vector<std::string>& got, const char* fmt, int n) {
    if (static_cast<int>(got.size()) != n) {
        printf("    (%zu of %d arrived)\n", got.size(), n);
        return false;
    }
    for (int i = 0; i < n; ++i) {
        char m[16], w[24];
        snprintf(m, sizeof(m), fmt, i);
        snprintf(w, sizeof(w), "2/5/%s", m);
        if (got[static_cast<size_t>(i)] != w) {
            printf("    (message %d is %s)\n", i, got[static_cast<size_t>(i)].c_str());
            return false;
        }
    }
    return true;
}

static std::vector<uint8_t> blob(size_t n, uint32_t seed) {
    Rand r(seed);
    std::vector<uint8_t> b(n);
    for (auto& x : b) x = static_cast<uint8_t>(r.next());
    return b;
}

// ===========================================================================
int main() {
    printf("The wire\n");
    {
        const uint8_t s[] = { '1','2','3','4','5','6','7','8','9' };
        check("CRC-16/CCITT-FALSE of \"123456789\" is 0x29B1", crc16(0xFFFF, s, sizeof(s)) == 0x29B1);

        Header h;
        h.family = 2; h.type = 5; h.flags = F_REL | F_SEC; h.session = 0x8123; h.seq = 0xBEEF;
        h.frag = 7; h.nfrag = 9; h.len = 200; h.chan = 11; h.crc = 0x1234; h.pn = 0xA1B2C3D4;
        uint8_t b[kHdr];
        packHeader(h, b);
        Header g;
        check("a header unpacks to what was packed",
              unpackHeader(b, sizeof(b), g) && g.family == 2 && g.type == 5 && g.flags == 3 && g.session == 0x8123 &&
              g.seq == 0xBEEF && g.frag == 7 && g.nfrag == 9 && g.len == 200 && g.chan == 11 && g.crc == 0x1234 &&
              g.pn == 0xA1B2C3D4);
        check("the header is little-endian: pn's low byte at 16", b[16] == 0xD4 && b[19] == 0xA1);
        check("a short header is refused", !unpackHeader(b, kHdr - 1, g));
        check("a payload is 222 bytes at most", kPayloadMax == 222);
        check("a bulk message is two fragments at least", bulkFrags(0) == 2 && bulkFrags(10) == 2);
        check("214 bytes fit fragment 0 exactly", bulkFrags(214) == 2 && bulkFrags(215) == 2 && bulkFrags(437) == 3);

        std::vector<uint8_t> in = { 0, 1, 0, 0, 2, 3, 0 };
        uint8_t enc[64], dec[64];
        size_t ne = cobsEncode(in.data(), in.size(), enc, sizeof(enc));
        bool noZero = ne > 0 && enc[ne - 1] == 0;
        for (size_t i = 0; i + 1 < ne; ++i) noZero = noZero && enc[i] != 0;
        size_t nd = cobsDecode(enc, ne - 1, dec, sizeof(dec));
        check("COBS: no zero inside a frame, one at its end", noZero);
        check("COBS: zeros come back", nd == in.size() && memcmp(dec, in.data(), nd) == 0);
        std::vector<uint8_t> big(600);
        for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>(i % 255 + 1);   // runs longer than 254
        uint8_t enc2[700], dec2[700];
        ne = cobsEncode(big.data(), big.size(), enc2, sizeof(enc2));
        nd = cobsDecode(enc2, ne - 1, dec2, sizeof(dec2));
        check("COBS: a run past 254 bytes comes back", ne && nd == big.size() && memcmp(dec2, big.data(), nd) == 0);
        uint8_t bad[] = { 5, 1, 2 };
        check("COBS: a code running past the end is refused", cobsDecode(bad, sizeof(bad), dec, sizeof(dec)) == 0);
        check("COBS: an encode that does not fit says 0", cobsEncode(big.data(), big.size(), enc2, 100) == 0);
    }

    printf("The cryptography\n");
    {
        // RFC 5869, test case 1
        uint8_t ikm[22];
        memset(ikm, 0x0b, sizeof(ikm));
        const uint8_t salt[] = { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c };
        const uint8_t info[] = { 0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9 };
        const uint8_t want[42] = {
            0x3c,0xb2,0x5f,0x25,0xfa,0xac,0xd5,0x7a,0x90,0x43,0x4f,0x64,0xd0,0x36,0x2f,0x2a,
            0x2d,0x2d,0x0a,0x90,0xcf,0x1a,0x5a,0x4c,0x5d,0xb0,0x2d,0x56,0xec,0xc4,0xc5,0xbf,
            0x34,0x00,0x72,0x08,0xd5,0xb8,0x87,0x18,0x58,0x65 };
        uint8_t okm[42];
        check("HKDF-SHA256 matches RFC 5869 test case 1",
              linkcrypto::hkdf(salt, sizeof(salt), ikm, sizeof(ikm), info, sizeof(info), okm, sizeof(okm)) &&
              memcmp(okm, want, sizeof(want)) == 0);

        {
            // RFC 3610, packet vector 1: 8 bytes of header, 23 of payload, an 8-byte tag.
            uint8_t k[16], iv[13] = { 0x00,0x00,0x00,0x03,0x02,0x01,0x00,0xA0,0xA1,0xA2,0xA3,0xA4,0xA5 };
            uint8_t hdr[8], pl[23], out[23], tg[8], back[23];
            for (int i = 0; i < 16; ++i) k[i] = static_cast<uint8_t>(0xC0 + i);
            for (int i = 0; i < 8; ++i) hdr[i] = static_cast<uint8_t>(i);
            for (int i = 0; i < 23; ++i) pl[i] = static_cast<uint8_t>(8 + i);
            const uint8_t want[31] = { 0x58,0x8C,0x97,0x9A,0x61,0xC6,0x63,0xD2,0xF0,0x66,0xD0,0xC2,0xC0,0xF9,0x89,
                                       0x80,0x6D,0x5F,0x6B,0x61,0xDA,0xC3,0x84,0x17,0xE8,0xD1,0x2C,0xFD,0xF9,0x26,0xE0 };
            bool sealed = linkcrypto::ccmSeal(k, iv, hdr, 8, pl, 23, out, tg);
            check("CCM matches RFC 3610 packet vector 1",
                  sealed && memcmp(out, want, 23) == 0 && memcmp(tg, want + 23, 8) == 0);
            check("and opens it", linkcrypto::ccmOpen(k, iv, hdr, 8, out, 23, back, tg) && memcmp(back, pl, 23) == 0);

            // Byte for byte mbedTLS's own CCM, for every payload length a frame can carry.
            Rand r(4242);
            bool same = true;
            for (size_t n = 0; n <= kPayloadMax && same; ++n) {
                uint8_t kk[16], nn[13], ad[kHdr], p[kPayloadMax], c1[kPayloadMax], c2[kPayloadMax], t1[8], t2[8];
                for (auto& x : kk) x = static_cast<uint8_t>(r.next());
                for (auto& x : nn) x = static_cast<uint8_t>(r.next());
                for (auto& x : ad) x = static_cast<uint8_t>(r.next());
                for (auto& x : p) x = static_cast<uint8_t>(r.next());
                mbedtls_ccm_context cc;
                mbedtls_ccm_init(&cc);
                mbedtls_ccm_setkey(&cc, MBEDTLS_CIPHER_ID_AES, kk, 128);
                mbedtls_ccm_encrypt_and_tag(&cc, n, nn, 13, ad, sizeof(ad), p, c1, t1, 8);
                mbedtls_ccm_free(&cc);
                same = linkcrypto::ccmSeal(kk, nn, ad, sizeof(ad), p, n, c2, t2) &&
                       memcmp(c1, c2, n) == 0 && memcmp(t1, t2, 8) == 0;
            }
            check("the two-call CCM is mbedTLS's CCM for payloads of 0 to 222 bytes", same);
        }

        uint8_t key[16];
        for (int i = 0; i < 16; ++i) key[i] = static_cast<uint8_t>(0x40 + i);
        uint8_t ad[kHdr] = { 1, 2, 3, 4, 5 };
        const char* msg = "a door keystroke, sealed";
        size_t n = strlen(msg);
        uint8_t ct[64], pt[64], tag[8];
        check("seal", linkcrypto::seal(key, 'H', 42, ad, sizeof(ad), reinterpret_cast<const uint8_t*>(msg), n, ct, tag));
        check("the ciphertext is not the plaintext", memcmp(ct, msg, n) != 0);
        check("open with the same key, direction and pn",
              linkcrypto::open(key, 'H', 42, ad, sizeof(ad), ct, n, pt, tag) && memcmp(pt, msg, n) == 0);
        check("a different pn does not open", !linkcrypto::open(key, 'H', 43, ad, sizeof(ad), ct, n, pt, tag));
        check("the other direction does not open (no reflection)",
              !linkcrypto::open(key, 'P', 42, ad, sizeof(ad), ct, n, pt, tag));
        uint8_t ad2[kHdr];
        memcpy(ad2, ad, sizeof(ad));
        ad2[1] ^= 1;
        check("a changed header does not open (it is associated data)",
              !linkcrypto::open(key, 'H', 42, ad2, sizeof(ad2), ct, n, pt, tag));
        ct[3] ^= 0x80;
        check("a changed byte of ciphertext does not open", !linkcrypto::open(key, 'H', 42, ad, sizeof(ad), ct, n, pt, tag));

        Rand r(99);
        auto rng = [](void* c, unsigned char* o, size_t k) {
            Rand& rr = *static_cast<Rand*>(c);
            for (size_t i = 0; i < k; ++i) o[i] = static_cast<unsigned char>(rr.next());
            return 0;
        };
        uint8_t a[32], A[65], b[32], B[65], s1[32], s2[32];
        bool kp = linkcrypto::keypair(rng, &r, a, A) && linkcrypto::keypair(rng, &r, b, B);
        check("two P-256 key pairs", kp && A[0] == 0x04 && B[0] == 0x04);
        check("both ends reach the same ECDH secret",
              linkcrypto::shared(rng, &r, a, B, s1) && linkcrypto::shared(rng, &r, b, A, s2) && memcmp(s1, s2, 32) == 0);
        B[40] ^= 1;
        check("a public key off the curve is refused", !linkcrypto::shared(rng, &r, a, B, s1));
    }

    printf("Pairing and the link coming up\n");
    {
        Air air;
        End host(air, 1, Role::Host, 16, 111);
        End peer(air, 2, Role::Peer, 16, 222);
        host.node.chan = 6;
        peer.node.chan = 1;
        host.eng->setBoardName("Test Board");
        peer.eng->setIdentity(KIND_DOORBOX, "t1.0", 0x6);
        peer.eng->startPairing(KIND_DOORBOX, "shelf", "t1.0");
        run(air, { &host, &peer }, 5000);
        check("with no window open the host asks nobody", !host.asked);
        host.eng->openPairing(120000);
        check("the host asks once a peer says hello on its channel",
              runUntil(air, { &host, &peer }, 20000, [&] { return host.asked; }));
        check("it names the device", host.askedWho.kind == KIND_DOORBOX && !strcmp(host.askedWho.name, "shelf") &&
                                     !strcmp(host.askedWho.fw, "t1.0"));
        check("and knows its MAC", host.askedWho.mac == peer.node.mac);
        check("the code is 4 digits", host.askedWho.code < 10000);
        run(air, { &host, &peer }, 3000);
        check("nothing is paired until the sysop says yes", !host.paired && !peer.paired);
        host.eng->pairAnswer(true);
        check("both ends finish pairing",
              runUntil(air, { &host, &peer }, 20000, [&] { return host.paired && peer.paired; }));
        check("the code the peer shows is the host's", peer.pairedWho.code == host.askedWho.code);
        check("both hold the same key", memcmp(peer.pairedWho.key, host.pairedWho.key, 16) == 0);
        check("the peer followed the host to channel 6", peer.node.chan == 6);
        check("the host has it as peer 0", host.eng->peerUsed(0) && host.eng->peerKind(0) == KIND_DOORBOX);
        check("the link comes up on both ends",
              runUntil(air, { &host, &peer }, 20000, [&] { return host.eng->peerUp(0) && peer.eng->hostUp(); }));
        check("each end was told", host.ups == 1 && peer.ups == 1);
        run(air, { &host, &peer }, 6000);
        check("the peer has the host's clock from PONG", peer.clockSeen > 1790000000u);
        check("the host has the peer's heartbeat", host.eng->peerStats(0).lastHeard + 6000 >= g_now);
    }

    printf("Pairing refused\n");
    {
        Air air;
        End host(air, 1, Role::Host, 16, 5);
        End peer(air, 2, Role::Peer, 16, 6);
        host.node.chan = 3;
        host.eng->openPairing(120000);
        peer.eng->startPairing(KIND_CAMSAT, "garden", "c1");
        runUntil(air, { &host, &peer }, 20000, [&] { return host.asked; });
        host.eng->pairAnswer(false);
        run(air, { &host, &peer }, 10000);
        check("a No pairs nothing", !host.paired && !peer.paired && !host.eng->peerUsed(0));
        check("and closes the window", !host.eng->pairingOpen());
    }

    printf("Messages, clean air\n");
    {
        Air air;
        End host(air, 1, Role::Host, 16, 7);
        End peer(air, 2, Role::Peer, 16, 8);
        check("paired and up", pairUp(air, host, peer, 6));
        uint16_t s = host.eng->openSession(0, FAM_DOOR);
        check("the host opens a session", s != 0 && (s & 0x8000) == 0);
        check("twenty go", sendN(air, host, peer, s, "m%02d", 0, 20));
        runUntil(air, { &host, &peer }, 5000, [&] { return peer.msgs.size() >= 20; });
        check("twenty messages arrive, in order, once each", inOrder(peer.msgs, "m%02d", 20));
    }

    printf("Messages through loss, duplication and reordering\n");
    {
        Air air;
        End host(air, 1, Role::Host, 16, 9);
        End peer(air, 2, Role::Peer, 16, 10);
        check("paired and up", pairUp(air, host, peer, 1));
        air.loss = 25; air.dup = 15; air.reorder = 30;
        uint16_t s = host.eng->openSession(0, FAM_DOOR);
        check("a hundred go", sendN(air, host, peer, s, "k%03d", 0, 100));
        runUntil(air, { &host, &peer }, 60000, [&] { return peer.msgs.size() >= 100; });
        check("a hundred messages arrive in order, none twice, none missing", inOrder(peer.msgs, "k%03d", 100));
        for (uint8_t d = 0; d < D_COUNT; ++d)
            if (host.eng->drops(d) || peer.eng->drops(d))
                printf("    (drops %-8s host %u peer %u)\n", dropName(d), host.eng->drops(d), peer.eng->drops(d));
        printf("    (retries host %u peer %u, downs host %d peer %d)\n", host.eng->peerStats(0).retries,
               peer.eng->peerStats(0).retries, host.downs, peer.downs);
        for (uint8_t r : host.resets) printf("    (host reset, reason %u)\n", r);
        for (uint8_t r : peer.resets) printf("    (peer reset, reason %u)\n", r);
        check("no session was given up", host.resets.empty() && peer.resets.empty());
        check("duplicates and reordering were seen and dropped",
              peer.eng->drops(D_REPLAY) + peer.eng->drops(D_ORDER) > 0);
        // And back the other way, the peer opening the session.
        uint16_t ps = peer.eng->openSession(0, FAM_DOOR);
        check("a peer's session id has the top bit set", (ps & 0x8000) != 0);
        check("thirty go back", sendN(air, peer, host, ps, "o%02d", 0, 30));
        runUntil(air, { &host, &peer }, 60000, [&] { return host.msgs.size() >= 30; });
        check("thirty the other way, in order", inOrder(host.msgs, "o%02d", 30));
    }

    printf("A picture: a bulk message through a lossy window\n");
    {
        Air air;
        End host(air, 1, Role::Host, 16, 11);
        End peer(air, 2, Role::Peer, 64, 12);
        check("paired and up", pairUp(air, host, peer, 11));
        air.loss = 15; air.dup = 10; air.reorder = 20;
        std::vector<uint8_t> jpeg = blob(30 * 1024, 77);
        uint16_t ps = peer.eng->openSession(0, FAM_CAMERA);
        check("the peer queues 30 KB", peer.eng->sendBulk(0, ps, FAM_CAMERA, 2, jpeg.data(),
                                                           static_cast<uint32_t>(jpeg.size())) == 1);
        g_pumpEvery = 7;                     // a runner that lags the loop
        bool done = runUntil(air, { &host, &peer }, 120000, [&] { return host.bulkOk + host.bulkBad > 0 && peer.bulkSentOk + peer.bulkSentFail > 0; });
        g_pumpEvery = 1;
        check("it finishes", done);
        check("the host's sink got every byte, in order", host.bulk == jpeg);
        check("and the CRC-32 checked", host.bulkOk == 1 && host.bulkBad == 0 && host.bulkBegun == 1);
        check("the sender was told it was taken", peer.bulkSentOk == 1 && peer.bulkSentFail == 0);
        printf("    (window drops %u, retries %u)\n", host.eng->drops(D_WINDOW), peer.eng->peerStats(0).retries);
        // A second one on the same session goes too.
        std::vector<uint8_t> small = blob(500, 3);
        peer.eng->sendBulk(0, ps, FAM_CAMERA, 2, small.data(), static_cast<uint32_t>(small.size()));
        runUntil(air, { &host, &peer }, 60000, [&] { return host.bulkOk == 2; });
        check("a second picture on the same session", host.bulkOk == 2 && host.bulk == small);
    }

    printf("A picture the host refuses\n");
    {
        Air air;
        End host(air, 1, Role::Host, 16, 13);
        End peer(air, 2, Role::Peer, 16, 14);
        check("paired and up", pairUp(air, host, peer, 4));
        host.ev.bulkBegin = [](void*, uint8_t, uint16_t, uint8_t, uint8_t, uint32_t) { return false; };
        delete host.eng;
        // A fresh engine with the refusing events, and the same pairing.
        host.eng = new Engine(Role::Host, host.node, host.ev, 16, host.win.data());
        host.eng->addPeer(peer.node.mac, host.pairedWho.key, KIND_CAMSAT);
        runUntil(air, { &host, &peer }, 30000, [&] { return host.eng->peerUp(0); });
        std::vector<uint8_t> jpeg = blob(4000, 1);
        uint16_t ps = peer.eng->openSession(0, FAM_CAMERA);
        peer.eng->sendBulk(0, ps, FAM_CAMERA, 2, jpeg.data(), static_cast<uint32_t>(jpeg.size()));
        runUntil(air, { &host, &peer }, 20000, [&] { return !peer.resets.empty(); });
        check("the sender hears REFUSED", !peer.resets.empty() && peer.resets[0] == R_REFUSED);
        check("and its bulk message is given up", peer.bulkSentFail == 1);
    }

    printf("No room: the receiver makes the sender wait, not fail\n");
    {
        Air air;
        End host(air, 1, Role::Host, 16, 15);
        End peer(air, 2, Role::Peer, 16, 16);
        check("paired and up", pairUp(air, host, peer, 6));
        uint16_t s = host.eng->openSession(0, FAM_DOOR);
        peer.refuse = true;                  // a caller's terminal with no room
        host.eng->send(0, s, FAM_DOOR, 5, "x", 1);
        run(air, { &host, &peer }, 30000);   // far longer than 8 retries would take
        check("nothing delivered while there is no room", peer.msgs.empty());
        check("and the session was not given up", host.resets.empty());
        peer.refuse = false;
        runUntil(air, { &host, &peer }, 5000, [&] { return !peer.msgs.empty(); });
        check("it arrives once there is room", peer.msgs.size() == 1 && peer.msgs[0] == "2/5/x");
    }

    printf("The router changes channel\n");
    {
        Air air;
        End host(air, 1, Role::Host, 16, 17);
        End peer(air, 2, Role::Peer, 16, 18);
        check("paired and up on 6", pairUp(air, host, peer, 6));
        uint16_t s = host.eng->openSession(0, FAM_DOOR);
        check("ten go on 6", sendN(air, host, peer, s, "c%02d", 0, 10));
        run(air, { &host, &peer }, 200);
        host.node.chan = 11;                 // the router hopped; the host follows it
        check("ten more are queued", sendN(air, host, peer, s, "c%02d", 10, 20));
        bool back = runUntil(air, { &host, &peer }, 60000, [&] { return peer.msgs.size() >= 20; });
        check("the peer finds the host on 11 and every message arrives", back && peer.node.chan == 11);
        check("in order, none twice: the pause lost nothing", inOrder(peer.msgs, "c%02d", 20));
        check("the session survived the hop", host.resets.empty() && peer.resets.empty());
        check("the peer saw its link go down and come back", peer.downs >= 1 && peer.ups >= 2);
    }

    printf("Strangers, forgeries and replays\n");
    {
        Air air;
        End host(air, 1, Role::Host, 16, 19);
        End peer(air, 2, Role::Peer, 16, 20);
        check("paired and up", pairUp(air, host, peer, 6));
        uint16_t s = peer.eng->openSession(0, FAM_DOOR);
        peer.eng->send(0, s, FAM_DOOR, 5, "real", 4);
        runUntil(air, { &host, &peer }, 3000, [&] { return host.msgs.size() == 1; });
        check("a real message arrives", host.msgs.size() == 1);

        // Replay every sealed frame the air has carried so far, from the peer.
        size_t before = host.msgs.size();
        uint32_t dropsBefore = host.eng->dropsTotal();
        for (auto& f : air.captured) host.node.inbox.emplace_back(peer.node.mac, f);
        run(air, { &host, &peer }, 500);
        check("replayed frames deliver nothing", host.msgs.size() == before);
        check("and are counted as drops", host.eng->dropsTotal() > dropsBefore);

        // A frame in the clear, claiming the peer's MAC, of a type that must be sealed.
        Header h;
        h.family = FAM_DOOR; h.type = 5; h.flags = F_REL; h.session = s; h.seq = 1; h.len = 4; h.chan = 6;
        uint8_t f[kFrameMax];
        packHeader(h, f);
        memcpy(f + kHdr, "fake", 4);
        h.crc = frameCrc(f, f + kHdr, 4);
        packHeader(h, f);
        uint32_t unsealed = host.eng->drops(D_UNSEALED);
        host.node.inbox.emplace_back(peer.node.mac, std::vector<uint8_t>(f, f + kHdr + 4));
        run(air, { &host, &peer }, 50);
        check("an unsealed door message under a spoofed MAC is dropped",
              host.msgs.size() == before && host.eng->drops(D_UNSEALED) == unsealed + 1);

        // Sealed under a key that is not the pairing's.
        uint8_t wrong[16] = { 9, 9, 9 };
        h.flags = F_REL | F_SEC; h.pn = 5000;
        packHeader(h, f);
        h.crc = frameCrc(f, reinterpret_cast<const uint8_t*>("fake"), 4);
        packHeader(h, f);
        linkcrypto::seal(wrong, DIR_PEER, 5000, f, kHdr, reinterpret_cast<const uint8_t*>("fake"), 4, f + kHdr, f + kHdr + 4);
        uint32_t tags = host.eng->drops(D_TAG);
        host.node.inbox.emplace_back(peer.node.mac, std::vector<uint8_t>(f, f + kHdr + 4 + kTag));
        run(air, { &host, &peer }, 50);
        check("a frame sealed with the wrong key is dropped", host.msgs.size() == before && host.eng->drops(D_TAG) > tags);

        // A stranger's MAC.
        Mac stranger;
        stranger.b[5] = 0x77;
        uint32_t strangers = host.eng->drops(D_UNKNOWN_PEER);
        host.node.inbox.emplace_back(stranger, std::vector<uint8_t>(f, f + kHdr + 4 + kTag));
        run(air, { &host, &peer }, 50);
        check("a sealed frame from a stranger is dropped", host.eng->drops(D_UNKNOWN_PEER) == strangers + 1);

        // A forged HELLO does not cut the working link off.
        uint8_t hello[kHdr + 35] = {};
        Header hh;
        hh.family = FAM_LINK; hh.type = T_HELLO; hh.len = 35; hh.chan = 6;
        packHeader(hh, hello);
        hh.crc = frameCrc(hello, hello + kHdr, 35);
        packHeader(hh, hello);
        host.node.inbox.emplace_back(peer.node.mac, std::vector<uint8_t>(hello, hello + sizeof(hello)));
        run(air, { &host, &peer }, 200);
        peer.eng->send(0, s, FAM_DOOR, 5, "still", 5);
        runUntil(air, { &host, &peer }, 3000, [&] { return host.msgs.size() == before + 1; });
        check("a forged HELLO does not displace the working key", host.msgs.size() == before + 1 &&
                                                                  host.msgs.back() == "2/5/still");
    }

    printf("A peer that goes away, and one that comes back new\n");
    {
        Air air;
        End host(air, 1, Role::Host, 16, 21);
        End peer(air, 2, Role::Peer, 16, 22);
        check("paired and up", pairUp(air, host, peer, 6));
        uint16_t s = host.eng->openSession(0, FAM_DOOR);
        peer.node.present = false;           // unplugged
        host.eng->send(0, s, FAM_DOOR, 5, "anyone", 6);
        run(air, { &host, &peer }, 15000);
        check("no reset while the peer might yet come back", host.resets.empty());
        run(air, { &host, &peer }, 90000);
        check("the host marks it down after 20 s of silence", host.downs == 1 && !host.eng->peerUp(0));
        check("and gives up its sessions a minute later", host.resets.size() == 1 && host.resets[0] == R_RETRIES);

        // It comes back as a freshly booted device with the same pairing.
        peer.node.present = true;
        uint8_t key[16];
        memcpy(key, peer.pairedWho.key, 16);
        delete peer.eng;
        peer.eng = new Engine(Role::Peer, peer.node, peer.ev, 16, peer.win.data());
        peer.eng->setIdentity(KIND_DOORBOX, "t1.1", 0x6);
        peer.eng->addPeer(host.node.mac, key, KIND_UNKNOWN);
        peer.node.chan = 1;
        check("the rebooted peer finds the host and comes up",
              runUntil(air, { &host, &peer }, 30000, [&] { return host.eng->peerUp(0) && peer.eng->hostUp(); }));
        uint16_t s2 = host.eng->openSession(0, FAM_DOOR);
        host.eng->send(0, s2, FAM_DOOR, 5, "hi again", 8);
        runUntil(air, { &host, &peer }, 3000, [&] { return !peer.msgs.empty(); });
        check("and a new session works", !peer.msgs.empty() && peer.msgs.back() == "2/5/hi again");
    }

    printf("\n%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
