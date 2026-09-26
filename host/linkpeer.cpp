// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         host/linkpeer.cpp
// Module:       Host / a pretend door box on the link (1.2.0)
//
// Purpose:      A peer for the host board's link (host/linkradio_host.cpp),
//               built from the same engine a real peer runs
//               (src/core/link.cpp), so tools/testclient.py can pair a
//               device, list its doors and send a caller through one, end to
//               end, with no radio. It is a door box with two doors:
//
//                 Echo   sends back what it gets, upper-cased; q finishes
//                 Clock  says the time left it was handed, then waits for
//                        TIMEUP or CLOSE and reports which in its log
//
//               It prints what a door author would want to see on its
//               console: the pairing code, every handoff line, every
//               TIMEUP and CLOSE, so a test can read them.
//
//   linkpeer --port P --host H [--chan C] [--state FILE] [--pair] [--name N]
//            [--kind doorbox|camsat]
//
//               P its UDP port, H the host board's, C the channel it starts
//               on (it scans to find the host either way), FILE where it
//               keeps its pairing, --pair to pair rather than use FILE.
//
// Targets:      the Linux host build
// See also:     LINK.md, tools/testclient.py (test_radio_link, test_doors)
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

class Udp : public Io {
public:
    int fd = -1;
    uint16_t port = 0, host = 0;
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
        to(m ? static_cast<uint16_t>((m->b[4] << 8) | m->b[5]) : host, f, n);   // broadcast: the host
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
            const uint16_t p = ntohs(a.sin_port);
            const uint8_t m[6] = { 0x02, 0, 0, 0, static_cast<uint8_t>(p >> 8), static_cast<uint8_t>(p) };
            memcpy(from.b, m, 6);
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
struct Caller { bool used; uint16_t sess; uint8_t door; };
Caller   g_callers[8];

void save(const PairInfo& who) {
    if (!g_state) return;
    FILE* f = fopen(g_state, "w");
    if (!f) return;
    fprintf(f, "%02x:%02x:%02x:%02x:%02x:%02x ", who.mac.b[0], who.mac.b[1], who.mac.b[2], who.mac.b[3],
            who.mac.b[4], who.mac.b[5]);
    for (uint8_t b : who.key) fprintf(f, "%02x", b);
    fprintf(f, "\n");
    fclose(f);
}

bool load() {
    if (!g_state) return false;
    FILE* f = fopen(g_state, "r");
    if (!f) return false;
    unsigned m[6];
    char key[40];
    bool ok = fscanf(f, "%x:%x:%x:%x:%x:%x %39s", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5], key) == 7;
    fclose(f);
    if (!ok || strlen(key) != 32) return false;
    Mac mac;
    uint8_t k[16];
    for (int i = 0; i < 6; ++i) mac.b[i] = static_cast<uint8_t>(m[i]);
    for (int i = 0; i < 16; ++i) { unsigned v; sscanf(key + 2 * i, "%2x", &v); k[i] = static_cast<uint8_t>(v); }
    return g_eng->addPeer(mac, k, KIND_UNKNOWN) == 0;
}

void sendList(uint16_t sess) {
    uint8_t b[2 + 2 * (2 + kDoorName)] = {};
    b[0] = 4;
    b[1] = 2;
    b[2] = 1; b[3] = 2; snprintf(reinterpret_cast<char*>(b + 4), kDoorName, "Echo");
    b[4 + kDoorName] = 2; b[5 + kDoorName] = 1; snprintf(reinterpret_cast<char*>(b + 6 + kDoorName), kDoorName, "Clock");
    g_eng->send(0, sess, FAM_DOOR, DOOR_LIST, b, sizeof(b));
}

Caller* callerOf(uint16_t sess) {
    for (Caller& c : g_callers) if (c.used && c.sess == sess) return &c;
    return nullptr;
}

bool onMessage(void*, uint8_t, uint16_t sess, uint8_t family, uint8_t type, const uint8_t* p, size_t n) {
    if (family != FAM_DOOR) return true;
    switch (type) {
        case DOOR_LIST_ASK:
            sendList(sess);
            g_eng->closeAfter(0, sess);
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
                g_eng->send(0, sess, FAM_DOOR, DOOR_REFUSED, r, sizeof(r));
                return true;
            }
            *c = Caller{ true, sess, p[0] };
            g_eng->send(0, sess, FAM_DOOR, DOOR_OPEN_OK, nullptr, 0);
            const char* hello = c->door == 1 ? "ECHO DOOR. Q quits.\r\n" : "CLOCK DOOR. Waiting for time.\r\n";
            g_eng->send(0, sess, FAM_DOOR, DOOR_DATA, hello, strlen(hello));
            return true;
        }
        case DOOR_DATA: {
            Caller* c = callerOf(sess);
            if (!c) return true;
            if (c->door == 1) {
                for (size_t i = 0; i < n; ++i) {
                    if (p[i] == 'q' || p[i] == 'Q') {
                        const uint8_t fin[] = { 0, 'E', 'c', 'h', 'o', ' ', 's', 'a', 'y', 's', ' ', 'b', 'y', 'e', '.' };
                        g_eng->send(0, sess, FAM_DOOR, DOOR_FINISHED, fin, sizeof(fin));
                        g_eng->closeAfter(0, sess);
                        c->used = false;
                        return true;
                    }
                }
                uint8_t up[kPayloadMax];
                for (size_t i = 0; i < n; ++i) up[i] = static_cast<uint8_t>(toupper(p[i]));
                return g_eng->send(0, sess, FAM_DOOR, DOOR_DATA, up, n) >= 0;
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
            Caller* c = callerOf(sess);
            if (c) c->used = false;
            g_eng->closeSession(0, sess);
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

}  // namespace

int main(int argc, char** argv) {
    uint16_t port = 0, host = 0;
    uint8_t chan = 1, kind = KIND_DOORBOX;
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
    }
    if (!port || !host) {
        fprintf(stderr, "linkpeer --port P --host H [--chan C] [--state FILE] [--pair] [--name N]\n");
        return 2;
    }
    srandom(static_cast<unsigned>(time(nullptr)) ^ port);
    g_io.host = host;
    g_io.chan = chan;
    if (!g_io.open(port)) { perror("linkpeer: bind"); return 1; }
    static uint8_t win[16 * kPayloadMax];
    Events ev;
    ev.message = onMessage;
    ev.paired = [](void*, uint8_t, const PairInfo& who) {
        printf("paired code %04u\n", static_cast<unsigned>(who.code));
        fflush(stdout);
        save(who);
    };
    ev.peerState = [](void*, uint8_t, bool up) { printf("link %s\n", up ? "up" : "down"); fflush(stdout); };
    ev.channel = [](void*, uint8_t ch) { printf("channel %u\n", ch); fflush(stdout); };
    g_eng = new Engine(Role::Peer, g_io, ev, 16, win);
    g_eng->setIdentity(kind, "test 1.0", 1u << FAM_DOOR);
    if (pair || !load()) {
        printf("pairing\n");
        fflush(stdout);
        g_eng->startPairing(kind, name, "test 1.0");
    }
    for (;;) {
        g_eng->poll();
        if (g_eng->pairComputeWanted()) g_eng->pairCompute();
        usleep(2000);
    }
}
