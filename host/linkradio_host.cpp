// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         host/linkradio_host.cpp
// Module:       Host / the µnleashed link's radio as UDP (1.2.0)
//
// Purpose:      linkradio.h on the Linux host build, so the link plugin runs
//               in the ordinary test board and host/linkpeer.cpp can be a
//               camera satellite or a door box beside it.
//
//               Each "radio" is a UDP socket on 127.0.0.1. Its MAC is
//               02:00:00:00 and its port, so a frame to a MAC goes to that
//               port. Every datagram starts with one byte, the sender's
//               channel, and a receiver on another channel drops it: the rule
//               that makes a channel hop cut a peer off, as the air does.
//
//               BBS_LINK_PORT   the host board's port (no port: no radio,
//                               and the link plugin says so)
//               BBS_LINK_CHAN   its channel, 6 unless set; <data>/linkchan,
//                               if present, overrides it and is read again
//                               every second, so a test can move the host
//               BBS_LINK_BCAST  ports a broadcast goes to (comma list)
//
// Targets:      the Linux host build
// See also:     src/platform/linkradio.h, host/linkpeer.cpp
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
#include "../src/platform/linkradio.h"
#include "../src/platform/platform.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace plat {

namespace {
int      g_fd = -1;
uint16_t g_port = 0;
uint8_t  g_chan = 6;
uint32_t g_chanAt = 0;
uint32_t g_moves = 0;

uint8_t channelNow() {
    const uint32_t now = millis();
    if (g_chanAt && now - g_chanAt < 1000) return g_chan;
    g_chanAt = now ? now : 1;
    char path[512];
    snprintf(path, sizeof(path), "%s/linkchan", userBase());
    FILE* f = fopen(path, "r");
    if (f) {
        unsigned c = 0;
        if (fscanf(f, "%u", &c) == 1 && c >= 1 && c <= 13 && c != g_chan) {
            log("link: the host moved from channel %u to %u", g_chan, c);
            g_chan = static_cast<uint8_t>(c);
            ++g_moves;
        }
        fclose(f);
    }
    return g_chan;
}

void sendTo(uint16_t port, const uint8_t* f, size_t n) {
    uint8_t buf[300];
    if (n + 1 > sizeof(buf)) return;
    buf[0] = channelNow();
    memcpy(buf + 1, f, n);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(g_fd, buf, n + 1, 0, reinterpret_cast<sockaddr*>(&a), sizeof(a));
}
}  // namespace

bool linkRadioStart(uint8_t) {
    if (g_fd >= 0) return true;
    const char* p = getenv("BBS_LINK_PORT");
    if (!p || !*p) return false;
    g_port = static_cast<uint16_t>(atoi(p));
    const char* c = getenv("BBS_LINK_CHAN");
    if (c && atoi(c) >= 1 && atoi(c) <= 13) g_chan = static_cast<uint8_t>(atoi(c));
    g_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_fd < 0) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(g_port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(g_fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        close(g_fd);
        g_fd = -1;
        return false;
    }
    fcntl(g_fd, F_SETFL, fcntl(g_fd, F_GETFL, 0) | O_NONBLOCK);
    return true;
}

void linkRadioStop() {
    if (g_fd >= 0) close(g_fd);
    g_fd = -1;
}

bool linkRadioUp() { return g_fd >= 0; }

bool linkRadioSend(const uint8_t* mac, const uint8_t* f, size_t n) {
    if (g_fd < 0) return false;
    if (mac) {
        sendTo(static_cast<uint16_t>((mac[4] << 8) | mac[5]), f, n);
        return true;
    }
    const char* b = getenv("BBS_LINK_BCAST");
    if (!b) return true;
    char list[128];
    snprintf(list, sizeof(list), "%s", b);
    for (char* t = strtok(list, ","); t; t = strtok(nullptr, ",")) sendTo(static_cast<uint16_t>(atoi(t)), f, n);
    return true;
}

bool linkRadioIdle() { return true; }

size_t linkRadioRecv(uint8_t* out, size_t cap, uint8_t mac[6], int8_t& rssi) {
    if (g_fd < 0) return 0;
    for (;;) {
        uint8_t buf[300];
        sockaddr_in a{};
        socklen_t al = sizeof(a);
        ssize_t n = recvfrom(g_fd, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&a), &al);
        if (n <= 1) return 0;
        if (buf[0] != channelNow()) continue;          // on another channel: not heard
        const uint16_t port = ntohs(a.sin_port);
        const uint8_t m[6] = { 0x02, 0, 0, 0, static_cast<uint8_t>(port >> 8), static_cast<uint8_t>(port) };
        memcpy(mac, m, 6);
        rssi = -40;
        size_t k = static_cast<size_t>(n - 1) < cap ? static_cast<size_t>(n - 1) : cap;
        memcpy(out, buf + 1, k);
        return k;
    }
}

bool linkRadioAddPeer(const uint8_t*) { return true; }
void linkRadioDelPeer(const uint8_t*) {}

uint8_t linkRadioChannel() { return channelNow(); }

void linkRadioMac(uint8_t mac[6]) {
    const uint8_t m[6] = { 0x02, 0, 0, 0, static_cast<uint8_t>(g_port >> 8), static_cast<uint8_t>(g_port) };
    memcpy(mac, m, 6);
}

uint32_t linkRadioRingDrops()    { return 0; }
uint8_t  linkRadioRingHigh()     { return 0; }
uint32_t linkRadioSendFails()    { return 0; }
uint32_t linkRadioChannelMoves() { return g_moves; }

void* linkAlloc(size_t n) { return malloc(n); }
void  linkFree(void* p) { free(p); }

}  // namespace plat
