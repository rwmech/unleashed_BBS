// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         host/linkpeer.cpp
// Module:       Host / a pretend satellite on the link (1.2.0)
//
// Purpose:      A peer for the host board's link (host/linkradio_host.cpp),
//               built from the same engine a real peer runs
//               (src/core/link.cpp), so tools/testclient.py can pair a
//               device, list its doors and send a caller through one, end to
//               end, with no radio. As a door box it has two doors:
//
//                 Echo   sends back what it gets, upper-cased; q finishes
//                 Clock  says the time left it was handed, then waits for
//                        TIMEUP or CLOSE and reports which in its log
//
//               It prints what a door author would want to see on its
//               console: the pairing code, every handoff line, every
//               TIMEUP and CLOSE, so a test can read them.
//
//               One satellite, several boards (1.2.0): with --board2 it also
//               plays a second board on its own port, so a test can share
//               the satellite between the board under test and another,
//               revoke one, and put the other on a different channel. The
//               second board is driven by lines on stdin:
//
//                 b2 pair        open its pairing, and say yes when asked
//                 b2 share N     as the owner, let one more board pair (N s)
//                 b2 forget      let the satellite go (UNPAIR)
//                 b2 revoke      as the owner, revoke the board under test
//                 b2 chan C      move it to channel C
//                 pair           the satellite starts pairing afresh
//
//               and it prints what happened, each line starting "b2 ".
//
//   linkpeer --port P --host H [--chan C] [--state FILE] [--pair] [--name N]
//            [--kind doorbox|camsat] [--board2 Q] [--board2-chan C]
//
//               P its UDP port, H the host board's, C the channel it starts
//               on (it scans to find the host either way), FILE where it
//               keeps its pairings, --pair to pair rather than use FILE, Q
//               the second board's port.
//
// Targets:      the Linux host build
// See also:     LINK.md, tools/testclient.py (test_radio_link, test_doors,
//               test_link_shared)
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
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "../src/core/link.h"
#include "../src/core/linkfam.h"

using namespace ulink;
using namespace linkfam;

namespace {

uint32_t nowMs() {
    timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint32_t>(tv.tv_sec * 1000ull + tv.tv_usec / 1000);
}

Mac macOf(uint16_t port) {
    Mac m;
    const uint8_t b[6] = { 0x02, 0, 0, 0, static_cast<uint8_t>(port >> 8), static_cast<uint8_t>(port) };
    memcpy(m.b, b, 6);
    return m;
}

class Udp : public Io {
public:
    int fd = -1;
    uint16_t port = 0;
    uint16_t bcast[2] = { 0, 0 };        // where a broadcast goes
    uint8_t chan = 1;
    bool open(uint16_t p) {
        port = p;
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(p);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) return false;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        return true;
    }
    void to(uint16_t p, const uint8_t* f, size_t n) {
        uint8_t buf[300];
        buf[0] = chan;
        memcpy(buf + 1, f, n);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(p);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sendto(fd, buf, n + 1, 0, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    }
    bool send(const Mac* m, const uint8_t* f, size_t n) override {
        if (m) { to(static_cast<uint16_t>((m->b[4] << 8) | m->b[5]), f, n); return true; }
        for (uint16_t b : bcast) if (b) to(b, f, n);
        return true;
    }
    bool idle() override { return true; }
    size_t recv(uint8_t* out, size_t cap, Mac& from, int8_t& rssi) override {
        for (;;) {
            uint8_t buf[300];
            sockaddr_in a{};
            socklen_t al = sizeof(a);
            ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&a), &al);
            if (n <= 1) return 0;
            if (buf[0] != chan) continue;
            from = macOf(ntohs(a.sin_port));
            rssi = -45;
            size_t k = static_cast<size_t>(n - 1) < cap ? static_cast<size_t>(n - 1) : cap;
            memcpy(out, buf + 1, k);
            return k;
        }
    }
    uint32_t millis() override { return nowMs(); }
    uint32_t micros() override { return nowMs() * 1000; }
    void random(uint8_t* out, size_t n) override { for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(::random()); }
    uint8_t channel() override { return chan; }
    void setChannel(uint8_t c) override { chan = c; }
};

Udp      g_io;
Engine*  g_eng = nullptr;
const char* g_state = nullptr;
struct Caller { bool used; uint8_t peer; uint16_t sess; uint8_t door; };
Caller   g_callers[8];

// The second board, when there is one.
Udp      g_io2;
Engine*  g_b2 = nullptr;
bool     g_b2yes = false;
bool     g_b2asked = false;
uint16_t g_hostPort = 0;

// The pairings, one line a board: mac key ord name. A line with only a mac
// and a key is the one-board format before 1.2.0's sharing.
void save() {
    if (!g_state) return;
    FILE* f = fopen(g_state, "w");
    if (!f) return;
    for (uint8_t i = 0; i < Engine::kHosts; ++i) {
        if (!g_eng->peerUsed(i)) continue;
        const Mac& m = g_eng->peerMac(i);
        fprintf(f, "%02x:%02x:%02x:%02x:%02x:%02x ", m.b[0], m.b[1], m.b[2], m.b[3], m.b[4], m.b[5]);
        const uint8_t* k = g_eng->peerKey(i);
        for (int b = 0; b < 16; ++b) fprintf(f, "%02x", k[b]);
        fprintf(f, " %u %s\n", g_eng->peerOrd(i), g_eng->peerName(i)[0] ? g_eng->peerName(i) : "-");
    }
    fclose(f);
}

bool load() {
    if (!g_state) return false;
    FILE* f = fopen(g_state, "r");
    if (!f) return false;
    char line[200];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned m[6], ord = 0;
        char key[40] = {};
        int at = 0;
        int got = sscanf(line, "%x:%x:%x:%x:%x:%x %39s %u %n", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5], key, &ord, &at);
        if (got < 7 || strlen(key) != 32) continue;
        char* name = at ? line + at : line + strlen(line);   // the rest of the line: a name may have spaces
        name[strcspn(name, "\r\n")] = '\0';
        if (got >= 8 && name[0]) ++got;
        Mac mac;
        uint8_t k[16];
        for (int i = 0; i < 6; ++i) mac.b[i] = static_cast<uint8_t>(m[i]);
        for (int i = 0; i < 16; ++i) { unsigned v; sscanf(key + 2 * i, "%2x", &v); k[i] = static_cast<uint8_t>(v); }
        const char* nm = (got >= 9 && strcmp(name, "-")) ? name : nullptr;
        if (g_eng->addPeer(mac, k, KIND_UNKNOWN, static_cast<uint8_t>(got >= 8 ? ord : 0), nm) >= 0) ++n;
    }
    fclose(f);
    return n > 0;
}

void sendList(uint8_t peer, uint16_t sess) {
    uint8_t b[2 + 2 * (2 + kDoorName)] = {};
    b[0] = 4;
    b[1] = 2;
    b[2] = 1; b[3] = 2; snprintf(reinterpret_cast<char*>(b + 4), kDoorName, "Echo");
    b[4 + kDoorName] = 2; b[5 + kDoorName] = 1; snprintf(reinterpret_cast<char*>(b + 6 + kDoorName), kDoorName, "Clock");
    g_eng->send(peer, sess, FAM_DOOR, DOOR_LIST, b, sizeof(b));
}

Caller* callerOf(uint8_t peer, uint16_t sess) {
    for (Caller& c : g_callers) if (c.used && c.peer == peer && c.sess == sess) return &c;
    return nullptr;
}

bool onMessage(void*, uint8_t peer, uint16_t sess, uint8_t family, uint8_t type, const uint8_t* p, size_t n) {
    if (family != FAM_DOOR) return true;
    switch (type) {
        case DOOR_LIST_ASK:
            sendList(peer, sess);
            g_eng->closeAfter(peer, sess);
            return true;
        case DOOR_OPEN: {
            char line[256];
            size_t k = n - 1 < sizeof(line) - 1 ? n - 1 : sizeof(line) - 1;
            memcpy(line, p + 1, k);
            line[k] = '\0';
            printf("handoff %u %s\n", p[0], line);
            fflush(stdout);
            Caller* c = nullptr;
            for (Caller& x : g_callers) if (!x.used) { c = &x; break; }
            if (!c || (p[0] != 1 && p[0] != 2)) {
                uint8_t r[] = { DR_UNKNOWN, 'N', 'o', ' ', 's', 'u', 'c', 'h', ' ', 'd', 'o', 'o', 'r', '.' };
                g_eng->send(peer, sess, FAM_DOOR, DOOR_REFUSED, r, sizeof(r));
                return true;
            }
            *c = Caller{ true, peer, sess, p[0] };
            g_eng->send(peer, sess, FAM_DOOR, DOOR_OPEN_OK, nullptr, 0);
            const char* hello = c->door == 1 ? "ECHO DOOR. Q quits.\r\n" : "CLOCK DOOR. Waiting for time.\r\n";
            g_eng->send(peer, sess, FAM_DOOR, DOOR_DATA, hello, strlen(hello));
            return true;
        }
        case DOOR_DATA: {
            Caller* c = callerOf(peer, sess);
            if (!c) return true;
            if (c->door == 1) {
                for (size_t i = 0; i < n; ++i) {
                    if (p[i] == 'q' || p[i] == 'Q') {
                        const uint8_t fin[] = { 0, 'E', 'c', 'h', 'o', ' ', 's', 'a', 'y', 's', ' ', 'b', 'y', 'e', '.' };
                        g_eng->send(peer, sess, FAM_DOOR, DOOR_FINISHED, fin, sizeof(fin));
                        g_eng->closeAfter(peer, sess);
                        c->used = false;
                        return true;
                    }
                }
                uint8_t up[kPayloadMax];
                for (size_t i = 0; i < n; ++i) up[i] = static_cast<uint8_t>(toupper(p[i]));
                return g_eng->send(peer, sess, FAM_DOOR, DOOR_DATA, up, n) >= 0;
            }
            return true;
        }
        case DOOR_WARN:
            printf("warn %u\n", n ? p[0] : 0);
            fflush(stdout);
            return true;
        case DOOR_TIMEUP:
            printf("timeup\n");
            fflush(stdout);
            return true;
        case DOOR_CLOSE: {
            printf("close %u\n", n ? p[0] : 0);
            fflush(stdout);
            Caller* c = callerOf(peer, sess);
            if (c) c->used = false;
            g_eng->closeSession(peer, sess);
            return true;
        }
        case DOOR_RESIZE:
            printf("resize %u %u\n", n > 1 ? p[0] : 0, n > 1 ? p[1] : 0);
            fflush(stdout);
            return true;
        default:
            return true;
    }
}

void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

// The second board's events.
void b2Start(uint16_t port, uint8_t chan, uint16_t satPort) {
    g_io2.bcast[0] = satPort;
    g_io2.chan = chan;
    if (!g_io2.open(port)) { perror("linkpeer: bind board2"); exit(1); }
    static uint8_t win2[16 * kPayloadMax];
    Events ev;
    ev.pairAsk = [](void*, const PairInfo& who) {
        say("b2 asked %s", who.name);
        g_b2asked = true;           // answered from the loop: the engine asks before it waits
    };
    ev.paired = [](void*, uint8_t, const PairInfo&) { g_b2yes = false; say("b2 paired"); };
    ev.peerState = [](void*, uint8_t, bool up) { say("b2 %s", up ? "up" : "down"); };
    ev.pairRefused = [](void*, const PairInfo&, uint8_t ch) { say("b2 refused chan %u", ch); };
    ev.unpaired = [](void*, uint8_t) { say("b2 unpaired"); };
    ev.peersList = [](void*, uint8_t, const SharedBoard* b, uint8_t n) {
        char line[200];
        int w = snprintf(line, sizeof(line), "b2 peers %u", n);
        for (uint8_t i = 0; i < n && w > 0 && static_cast<size_t>(w) < sizeof(line); ++i)
            w += snprintf(line + w, sizeof(line) - static_cast<size_t>(w), " %u:%s", b[i].flags, b[i].name);
        say("%s", line);
    };
    g_b2 = new Engine(Role::Host, g_io2, ev, 16, win2);
    g_b2->setBoardName("Phantom BBS");
}

void command(char* line) {
    line[strcspn(line, "\r\n")] = '\0';
    if (!strcmp(line, "pair")) { g_eng->startPairing(KIND_DOORBOX, "shelf", "test 1.0"); say("pairing"); return; }
    if (!g_b2 || strncmp(line, "b2 ", 3)) return;
    const char* c = line + 3;
    if (!strcmp(c, "pair"))                 { g_b2yes = true; g_b2->openPairing(120000); say("b2 pairing open"); }
    else if (!strncmp(c, "share ", 6))      say("b2 share %s", g_b2->sharePairing(0, static_cast<uint16_t>(atoi(c + 6))) ? "sent" : "refused");
    else if (!strcmp(c, "forget"))          { g_b2->forget(0); say("b2 forget sent"); }
    else if (!strcmp(c, "revoke"))          say("b2 revoke %s", g_b2->revoke(0, macOf(g_hostPort)) ? "sent" : "refused");
    else if (!strncmp(c, "chan ", 5))       { g_io2.chan = static_cast<uint8_t>(atoi(c + 5)); say("b2 chan %u", g_io2.chan); }
}

}  // namespace

int main(int argc, char** argv) {
    uint16_t port = 0, host = 0, board2 = 0;
    uint8_t chan = 1, kind = KIND_DOORBOX, chan2 = 0;
    bool pair = false;
    const char* name = "shelf";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = static_cast<uint16_t>(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--host") && i + 1 < argc) host = static_cast<uint16_t>(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--chan") && i + 1 < argc) chan = static_cast<uint8_t>(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--state") && i + 1 < argc) g_state = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--pair")) pair = true;
        else if (!strcmp(argv[i], "--kind") && i + 1 < argc) kind = strcmp(argv[++i], "camsat") ? KIND_DOORBOX : KIND_CAMSAT;
        else if (!strcmp(argv[i], "--board2") && i + 1 < argc) board2 = static_cast<uint16_t>(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--board2-chan") && i + 1 < argc) chan2 = static_cast<uint8_t>(atoi(argv[++i]));
    }
    if (!port || !host) {
        fprintf(stderr, "linkpeer --port P --host H [--chan C] [--state FILE] [--pair] [--name N] [--board2 Q]\n");
        return 2;
    }
    srandom(static_cast<unsigned>(time(nullptr)) ^ port);
    g_hostPort = host;
    g_io.bcast[0] = host;
    g_io.bcast[1] = board2;
    g_io.chan = chan;
    if (!g_io.open(port)) { perror("linkpeer: bind"); return 1; }
    static uint8_t win[16 * kPayloadMax];
    Events ev;
    ev.message = onMessage;
    ev.paired = [](void*, uint8_t slot, const PairInfo& who) {
        say("paired code %04u", static_cast<unsigned>(who.code));
        say("paired slot %u", slot);
        save();
    };
    ev.peerState = [](void*, uint8_t slot, bool up) { say("link %s %u", up ? "up" : "down", slot); };
    ev.channel = [](void*, uint8_t ch) { say("channel %u", ch); };
    ev.unpaired = [](void*, uint8_t slot) { say("unpaired %u", slot); save(); };
    ev.shareOpened = [](void*, uint32_t s) { say("share open %u", static_cast<unsigned>(s)); };
    ev.pairAsk = [](void*, const PairInfo& who) { say("code %04u from %s", static_cast<unsigned>(who.code), who.name); };
    g_eng = new Engine(Role::Peer, g_io, ev, 16, win);
    g_eng->setIdentity(kind, "test 1.0", 1u << FAM_DOOR);
    if (board2) b2Start(board2, chan2 ? chan2 : chan, port);
    fcntl(0, F_SETFL, fcntl(0, F_GETFL, 0) | O_NONBLOCK);
    if (pair || !load()) {
        say("pairing");
        g_eng->startPairing(kind, name, "test 1.0");
    }
    char in[128];
    size_t inLen = 0;
    for (;;) {
        g_eng->poll();
        if (g_eng->pairComputeWanted()) g_eng->pairCompute();
        if (g_b2) {
            g_b2->poll();
            if (g_b2->pairComputeWanted()) g_b2->pairCompute();
            if (g_b2asked && g_b2yes) { g_b2asked = false; g_b2->pairAnswer(true); say("b2 answered yes, open %d", g_b2->pairingOpen()); }
        }
        { static uint32_t dbgAt = 0; if (getenv("LP_DEBUG") && nowMs() - dbgAt > 2000) { dbgAt = nowMs();
            char l[200]; int w = snprintf(l, sizeof(l), "dbg sat chan %u drops", g_io.chan);
            for (uint8_t d = 0; d < D_COUNT; ++d) w += snprintf(l + w, sizeof(l) - w, " %s=%u", dropName(d), g_eng->drops(d));
            if (g_b2) { w += snprintf(l + w, sizeof(l) - w, " | b2"); for (uint8_t d = 0; d < D_COUNT; ++d) w += snprintf(l + w, sizeof(l) - w, " %s=%u", dropName(d), g_b2->drops(d)); }
            say("%s", l); } }
        char ch;
        while (read(0, &ch, 1) == 1) {
            if (ch == '\n') { in[inLen] = '\0'; command(in); inLen = 0; }
            else if (inLen + 1 < sizeof(in)) in[inLen++] = ch;
        }
        usleep(2000);
    }
}
