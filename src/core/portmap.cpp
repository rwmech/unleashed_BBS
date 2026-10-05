/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/portmap.cpp
 * Module:       Core / the board asks the router to forward its port (1.2.2)
 *
 * See portmap.h for what this is for and why it is shaped this way. What
 * follows is how the two protocols are actually spoken, with the clause of
 * each RFC every rule comes from, because most of these numbers were wrong
 * in a first draft written from memory.
 *
 * PCP, RFC 6887
 *   Request: a 24 byte header then 36 bytes of MAP data, 60 in all.
 *     header  0      version = 2
 *             1      R (top bit) 0 for a request | opcode; MAP is 1
 *             2-3    reserved
 *             4-7    requested lifetime, seconds, network order
 *             8-23   the client's own IP, 128 bits; IPv4 goes in as the
 *                    IPv4-mapped form ::ffff:a.b.c.d (section 5)
 *     MAP     0-11   mapping nonce, 96 bits, the client's own random value.
 *                    A refresh of the same mapping MUST carry the same
 *                    nonce (section 11.2), so it is kept per port.
 *             12     protocol from the IANA registry; TCP is 6
 *             13-15  reserved, 24 bits
 *             16-17  internal port
 *             18-19  suggested external port; 0 means "anything"
 *             20-35  suggested external IP, 128 bits; zero means "anything"
 *   Response: the same 60 bytes with the header's byte 1 top bit set
 *     (0x81 for MAP), byte 3 the result code, 4-7 the granted lifetime,
 *     8-11 the server's epoch, and in the MAP part the ASSIGNED external
 *     port at 18-19 and the assigned external IP at 20-35.
 *   Result codes (section 7.4): 0 SUCCESS, 1 UNSUPP_VERSION,
 *     2 NOT_AUTHORIZED, 3 MALFORMED_REQUEST, 4 UNSUPP_OPCODE,
 *     5 UNSUPP_OPTION, 6 MALFORMED_OPTION, 7 NETWORK_FAILURE,
 *     8 NO_RESOURCES, 9 UNSUPP_PROTOCOL, 10 USER_EX_QUOTA,
 *     11 CANNOT_PROVIDE_EXTERNAL, 12 ADDRESS_MISMATCH,
 *     13 EXCESSIVE_REMOTE_PEERS.
 *   ADDRESS_MISMATCH is the useful one: the server saw a source address
 *   that is not the one in the header, so something is translating between
 *   the board and that server and asking it is pointless.
 *   Version negotiation (section 9): a client sends the highest version it
 *   has, and "if the version number in the UNSUPP_VERSION response is zero
 *   then that means this is a NAT-PMP server".
 *
 * NAT-PMP, RFC 6886
 *   Map request, 12 bytes:
 *             0      version = 0
 *             1      opcode; 1 maps UDP, 2 maps TCP
 *             2-3    reserved
 *             4-5    internal port
 *             6-7    suggested external port
 *             8-11   requested lifetime, seconds
 *   Map response, 16 bytes:
 *             0      version = 0
 *             1      128 + the opcode
 *             2-3    result code
 *             4-7    seconds since start of epoch
 *             8-9    internal port
 *             10-11  the mapped external port
 *             12-15  the granted lifetime
 *   Result codes (section 3.5): 0 success, 1 unsupported version,
 *     2 not authorised or refused (the box can map, the user turned it
 *     off), 3 network failure (the box has no DHCP lease of its own),
 *     4 out of resources, 5 unsupported opcode.
 *   A public-address request is version 0 opcode 0, two bytes, and its
 *   12 byte reply carries the external address. It is asked only where
 *   NAT-PMP is the protocol in use, because the map reply carries
 *   everything except the address while PCP's carries that too.
 *   Retransmission (section 3.1): 250 ms, doubling, and after a ninth
 *   attempt the client "SHOULD conclude that this gateway does not support
 *   NAT Port Mapping Protocol". Nine attempts is over two minutes, which is
 *   right for a client that must eventually succeed and wrong for a probe
 *   that must DECIDE; a gateway on the same subnet answers in under a
 *   millisecond, so this sends kTries times at 250, 500 and 1000 ms and
 *   calls it absent, then asks the whole question again an hour later in
 *   case somebody has just turned the feature on in the router menu.
 *   Lifetime (section 3.3): "The RECOMMENDED Port Mapping Lifetime is 7200
 *   seconds", and "the client SHOULD begin trying to renew the mapping
 *   halfway to expiry time, like DHCP". PCP says the same in different
 *   words (section 11.2.3, 1/2 to 5/8 of the lifetime), so one rule serves
 *   both: half of what was granted.
 *   Lifetime 0 deletes a mapping, with the suggested external port set to
 *   zero (section 3.4). That is what switching the setting off sends.
 *   The gateway rebooting (section 3.6): the client keeps the last epoch it
 *   saw and when it saw it, and if a new epoch is more than 2 seconds below
 *   7/8 of the locally elapsed time added to the old one, the gateway has
 *   "undergone a reboot or other loss of port mapping state" and every
 *   mapping MUST be renewed at once. PCP's own check (section 8.5) is a
 *   tighter 15/16; the more forgiving 7/8 is used for both, because a
 *   genuine reboot puts the epoch back to nearly zero and trips either by a
 *   mile, while a gateway with a coarse clock can trip the tight one for
 *   nothing and the cost of a false positive is a pointless re-request.
 *
 * Libraries:    none (libc, BSD sockets)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     src/core/portmap.h
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
#include "portmap.h"

#include "clock.h"
#include "sysconfig.h"
#include "../config.h"
#include "../platform/platform.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstdio>
#include <cstring>

namespace portmap {
namespace {

// The one port both protocols answer on (RFC 6886 section 3.2, RFC 6887
// section 8.1). On the host a test puts a stand-in router somewhere else,
// because 5351 is one number and the test lanes run side by side.
constexpr uint16_t kPort     = 5351;

// Asked for, in seconds: RFC 6886's recommendation. Routers clamp what they
// do not like and the board renews against what came back, not this.
constexpr uint32_t kLifeSecs = 7200;

// The sends in one exchange, and the first gap; it doubles, so 250 + 500 +
// 1000 and 1.75 s to decide. See the retransmission note above.
constexpr uint8_t  kTries    = 3;
constexpr uint32_t kFirstMs  = 250;

// How long before the whole question is asked again when the gateway
// answered nothing at all, or said a flat no. An hour: the one thing that
// changes is a sysop going into the router menu, and a round is at most
// three 60 byte PCP packets and three 12 byte NAT-PMP ones, which is
// nothing to anybody.
constexpr uint32_t kRetryMs  = 3600u * 1000u;

// And after an answer that could clear by itself (a quota freeing up, a
// caller hanging up and giving a socket back).
constexpr uint32_t kSoonMs   = 60u * 1000u;

// The longest a held mapping goes without looking at the network at all.
// The renewal is an hour out, but the same look is what notices this board's
// address or its router changing, and that cannot wait an hour.
constexpr uint32_t kCheckMs  = 60u * 1000u;

// The mappings this board wants: the telnet port, and the SSH port on a
// board that binds one. backup_port is deliberately not here (portmap.h).
#if BBS_HAS_SSH
constexpr uint8_t  kSlots    = 2;
#else
constexpr uint8_t  kSlots    = 1;
#endif

// Which protocol an exchange is speaking. Not Proto, because an exchange
// can be in PCP while the settled answer is still Unknown.
constexpr uint8_t kAskPcp = 0;
constexpr uint8_t kAskPmp = 1;

constexpr uint8_t F_WANT  = 0x01;   // this port should be mapped
constexpr uint8_t F_HELD  = 0x02;   // granted, and its lifetime has not run out
constexpr uint8_t F_NONCE = 0x04;   // the PCP nonce is set, so a refresh reuses it

struct Slot {
    uint16_t internal  = 0;
    uint16_t external  = 0;
    uint32_t lifeSecs  = 0;         // what was granted
    uint32_t grantedAt = 0;         // millis of the grant
    uint8_t  nonce[12] = {};        // PCP's, per mapping (RFC 6887 section 11.2)
    uint8_t  flags     = 0;
};

Slot     g_slot[kSlots];
Proto    g_proto    = Proto::Unknown;
Why      g_why      = Why::Off;
bool     g_on       = false;        // the setting, as this module last saw it
int      g_fd       = -1;           // only while an exchange is in flight
uint8_t  g_at       = 0;            // the slot this exchange is about
uint8_t  g_tries    = 0;            // sends made in this exchange
uint8_t  g_ask      = kAskPcp;      // which protocol it is speaking
uint8_t  g_release  = 0;            // lifetime-0 requests still to try, on the way out
bool     g_addrAsk  = false;        // this exchange is NAT-PMP's public-address request
bool     g_delete   = false;        // ... or a lifetime 0
// NAT-PMP's public-address request has been made for the address we hold.
// Not "g_ext is still zero": a router with no lease of its own answers zero
// for ever, and asking until the answer is non-zero is a packet a pass.
bool     g_extAsked = false;
// PORTMAP NOW: ask about the next wanted mapping whether or not it is held.
// Consumed by due() and cleared by begin().
bool     g_force    = false;
// The last exchange was about a mapping rather than the outside address, so
// the address gets the next one (tick). A hint, not state that can be
// wrong: a stale one costs one extra address request.
bool     g_mapTried = false;
uint32_t g_sentAt   = 0;
// "Nothing before g_waitMs has passed since g_waitFrom". A deadline held as
// a stamp and compared signed reads "not yet" again after 24.86 days (the
// review of 1.2.0-panel.1), so every wait here is an elapsed time through
// plat::since, which is this tree's one rule for them.
uint32_t g_waitFrom = 0;
uint32_t g_waitMs   = 0;
uint32_t g_gw       = 0;            // the gateway, network order
uint32_t g_self     = 0;            // our own address, for PCP's header
uint32_t g_ext      = 0;            // the router's outside address, network order
uint32_t g_epoch    = 0;            // the gateway's clock, last seen
uint32_t g_epochAt  = 0;            // and the millis we saw it
uint32_t g_asOf     = 0;            // clk::epoch of the last ANSWER, 0 never
uint8_t  g_code     = 0;            // the protocol's own last result code
// What refused() last said on the console, so it says it again only when
// the reason or the code moves. Its own, because g_code is set by the
// callers before they call.
Why      g_loggedWhy  = Why::Ok;
uint8_t  g_loggedCode = 0xFF;
uint16_t g_asked    = 0;
uint16_t g_failed   = 0;

void waitFor(uint32_t now, uint32_t ms) { g_waitFrom = now; g_waitMs = ms; }
bool  waiting(uint32_t now) { return g_waitMs && plat::since(now, g_waitFrom) < g_waitMs; }

// ---------------------------------------------------------------------------
// privateAddr: an address callers on the internet cannot reach. The point
// of the whole feature is to find out whether the router's outside address
// is one of these, because a mapping onto a carrier's address is granted,
// correct and useless.
//
// 10/8, 172.16/12 and 192.168/16 are RFC 1918 (a second router inside the
// house); 100.64/10 is RFC 6598, the carrier-grade NAT range, which is the
// common one; 169.254/16 is a router that never got a lease; 127/8 and 0/8
// are nonsense out here. The board's own cgnat_local setting is about
// trusting callers FROM 100.64/10 on a Tailscale network and says nothing
// about whether the internet can reach an address in it, so it is not read.
// ---------------------------------------------------------------------------
bool privateAddr(uint32_t netOrder) {
    const uint32_t a  = ntohl(netOrder);
    const uint8_t  b0 = static_cast<uint8_t>(a >> 24);
    const uint8_t  b1 = static_cast<uint8_t>(a >> 16);
    if (!a) return true;
    if (b0 == 10 || b0 == 127 || b0 == 0) return true;
    if (b0 == 172 && (b1 & 0xF0) == 16) return true;
    if (b0 == 192 && b1 == 168) return true;
    if (b0 == 100 && (b1 & 0xC0) == 64) return true;       // 100.64.0.0/10
    if (b0 == 169 && b1 == 254) return true;
    if (b0 >= 224) return true;                            // multicast and reserved
    return false;
}

void put16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v >> 8); p[1] = static_cast<uint8_t>(v); }
void put32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24); p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);  p[3] = static_cast<uint8_t>(v);
}
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t get32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
}

// gatewayPort: 5351 on a board. On the host a test names its stand-in
// router's port in BBS_HOST_GATEWAY, so the lanes do not fight over one
// number and nothing has to bind a privileged port.
uint16_t gatewayPort() {
#ifdef BBS_HOST
    const uint16_t p = plat::hostGatewayPort();
    if (p) return p;
#endif
    return kPort;
}

void closeFd() {
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
}

// drop: forget every mapping. A board whose own address changed, or a
// gateway that lost its state, has nothing left whatever it was told.
//
// The outside address goes with them, and so does the NAT-PMP flag that
// says it has been asked for: a router that restarts can come back on a
// different address, or off a public one onto the carrier's, and that is
// exactly the change a sysop most needs told about. Keeping it meant the
// address a board reported was the one from before the reboot for the rest
// of its uptime, with nothing short of toggling the setting to refresh it
// (code review, HIGH).
//
// The PCP nonce deliberately STAYS, and the case that settles it is the
// FALSE positive rather than the real reboot. After a real reboot the
// gateway holds nothing, so neither the old nonce nor a fresh one matches
// anything and either creates exactly one mapping. But forgot() uses RFC
// 6886's forgiving 7/8 rule on purpose and so can trip on a gateway with a
// coarse clock, and there the old mapping is still live: a fresh nonce for
// the same internal port would ask for a SECOND one and eat the gateway's
// quota, where the same nonce refreshes the one that is there (RFC 6887
// section 11.2). Only a changed port is a changed mapping, and want()
// clears the nonce there.
void drop() {
    for (uint8_t i = 0; i < kSlots; ++i) {
        g_slot[i].flags &= static_cast<uint8_t>(~F_HELD);
        g_slot[i].external = 0;
    }
    g_ext      = 0;
    g_extAsked = false;
}

bool anyHeld() {
    for (uint8_t i = 0; i < kSlots; ++i)
        if (g_slot[i].flags & F_HELD) return true;
    return false;
}

// ---------------------------------------------------------------------------
// want: the ports to map. The ones the board is actually listening on, not
// the configured ones: a port changed in CONFIG waits for a restart, and
// forwarding the new number before the board answers on it would send
// callers to a closed port.
//
// A changed port is a different mapping, so what was held for the old one
// is forgotten rather than renewed, and the PCP nonce goes with it because
// RFC 6887 ties a nonce to one mapping.
// ---------------------------------------------------------------------------
void want(uint16_t telnet, uint16_t ssh) {
    uint16_t ports[kSlots] = { telnet };
#if BBS_HAS_SSH
    ports[1] = ssh;
#else
    (void)ssh;
#endif
    for (uint8_t i = 0; i < kSlots; ++i) {
        Slot& s = g_slot[i];
        if (s.internal != ports[i]) {
            s.internal = ports[i];
            s.external = 0;
            s.flags &= static_cast<uint8_t>(~(F_HELD | F_NONCE));
        }
        if (ports[i]) s.flags |= F_WANT;
        else          s.flags &= static_cast<uint8_t>(~F_WANT);
    }
}

// lapse: a mapping whose granted lifetime has run out while the renewals
// were failing. Said once, as "ran out", rather than left claiming to be
// held: a sysop acting on a stale "mapped" is worse off than one told the
// truth.
void lapse(uint32_t now) {
    for (uint8_t i = 0; i < kSlots; ++i) {
        Slot& s = g_slot[i];
        if (!(s.flags & F_HELD)) continue;
        if (plat::since(now, s.grantedAt) / 1000u < s.lifeSecs) continue;
        s.flags &= static_cast<uint8_t>(~F_HELD);
        s.external = 0;
        if (g_why == Why::Ok || g_why == Why::Carrier) g_why = Why::Lapsed;
        plat::log("portmap: the mapping for port %u has run out",
                  static_cast<unsigned>(s.internal));
    }
}

// ---------------------------------------------------------------------------
// due: the slot that most wants asking about, 0xFF when none does.
//
// A slot wants an exchange when it is wanted and either holds nothing or is
// past half its granted lifetime, the renewal rule both RFCs give. Half of
// 7200 is an hour.
// ---------------------------------------------------------------------------
uint8_t due(uint32_t now) {
    for (uint8_t i = 0; i < kSlots; ++i) {
        const Slot& s = g_slot[i];
        if (!(s.flags & F_WANT)) continue;
        if (g_force || !(s.flags & F_HELD)) return i;
        if (plat::since(now, s.grantedAt) / 1000u >= s.lifeSecs / 2u) return i;
    }
    return 0xFF;
}

// ---------------------------------------------------------------------------
// forgot: did the gateway lose its mappings since the last answer?
// RFC 6886 section 3.6's rule, used for PCP too (see the file comment). The
// first answer after a boot sets the baseline and can never trip it.
// ---------------------------------------------------------------------------
bool forgot(uint32_t epoch, uint32_t now) {
    if (!g_epochAt) return false;                          // no baseline yet
    const uint32_t mine  = plat::since(now, g_epochAt) / 1000u;
    const uint32_t least = g_epoch + (mine - mine / 8u);   // 7/8 of my elapsed time
    return epoch + 2u < least;
}

// ---------------------------------------------------------------------------
// sendAsk: one request onto the wire. The packet is built fresh each send
// and lives on the stack, so nothing but the slot table is static. A send
// that fails is treated as one that was not answered: the next try is the
// retransmission the RFC would have made anyway.
// ---------------------------------------------------------------------------
void sendAsk(uint32_t now) {
    uint8_t pkt[60];
    size_t  len  = 0;
    Slot&   s    = g_slot[g_at];
    const uint32_t life = g_delete ? 0u : kLifeSecs;
    // The port we would like: zero on a delete, which RFC 6886 section 3.4
    // requires; otherwise the external one already granted, so a refresh
    // keeps the number callers have been given, else the internal one,
    // which is the number a sysop expects to see forwarded.
    const uint16_t wantExt = g_delete ? 0 : (s.external ? s.external : s.internal);

    if (g_ask == kAskPcp) {
        memset(pkt, 0, sizeof(pkt));
        pkt[0] = 2;                                        // version
        pkt[1] = 1;                                        // R = 0, opcode MAP
        put32(pkt + 4, life);
        // The client's own address, IPv4-mapped into the 128 bit field
        // (RFC 6887 section 5): ::ffff:a.b.c.d.
        pkt[8 + 10] = 0xFF;
        pkt[8 + 11] = 0xFF;
        memcpy(pkt + 8 + 12, &g_self, 4);
        uint8_t* m = pkt + 24;
        if (!(s.flags & F_NONCE)) {
            for (uint8_t i = 0; i < 12; i += 4) {
                const uint32_t r = plat::random32();
                memcpy(s.nonce + i, &r, 4);
            }
            s.flags |= F_NONCE;
        }
        memcpy(m, s.nonce, 12);
        m[12] = 6;                                         // TCP, IANA protocol number
        put16(m + 16, s.internal);
        put16(m + 18, wantExt);
        len = 60;
    } else {
        memset(pkt, 0, 12);
        if (g_addrAsk) {
            len = 2;                                       // version 0, opcode 0
        } else {
            pkt[1] = 2;                                    // map TCP
            put16(pkt + 4, s.internal);
            put16(pkt + 6, wantExt);
            put32(pkt + 8, life);
            len = 12;
        }
    }

    sockaddr_in to = {};
    to.sin_family      = AF_INET;
    to.sin_port        = htons(gatewayPort());
    to.sin_addr.s_addr = g_gw;
    sendto(g_fd, pkt, len, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
    g_sentAt = now;
    ++g_tries;
}

// ---------------------------------------------------------------------------
// begin: start an exchange about slot i in protocol ask.
//
// False when there was no socket to be had, which is a thing to say rather
// than to hide: CONFIG_LWIP_MAX_SOCKETS is 16, IDF 5.3.1 caps it there on
// every chip, and the board already oversubscribes it (portmap.h).
// ---------------------------------------------------------------------------
bool begin(uint8_t i, uint8_t ask, bool addrAsk, bool del, uint32_t now) {
    closeFd();
    g_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_fd < 0) {
        g_why = Why::NoSocket;
        waitFor(now, kSoonMs);
        plat::log("portmap: no socket free to ask the router; trying again in a minute");
        return false;
    }
    const int fl = fcntl(g_fd, F_GETFL, 0);
    fcntl(g_fd, F_SETFL, fl | O_NONBLOCK);
    g_at      = i;
    g_ask     = ask;
    g_addrAsk = addrAsk;
    g_delete  = del;
    g_tries   = 0;
    g_force   = false;                   // PORTMAP NOW is spent on this ask
    ++g_asked;
    sendAsk(now);
    return true;
}

// giveUp: this exchange got nowhere. Either fall through to the other
// protocol, or conclude that the gateway does not do this at all.
void giveUp(uint32_t now) {
    closeFd();
    ++g_failed;
    const bool del = g_delete;
    // PCP said nothing. A gateway that speaks only NAT-PMP should have
    // answered UNSUPP_VERSION, but plenty of them simply drop a version
    // they do not know, so NAT-PMP is asked anyway.
    //
    // "Not settled on a working protocol", not "never asked": this used to
    // require Proto::Unknown, which the line below sets to None the first
    // time both are silent, so from the SECOND hourly round on the board
    // probed PCP alone for ever. That falsified the recovery this feature is
    // documented to have, and it failed for the protocol the comment above
    // says is the likely one: a sysop with a NAT-PMP-only router who
    // switched UPnP on after the board gave up was never found again (the
    // third review pass).
    if (g_proto != Proto::Pcp && g_proto != Proto::Pmp && g_ask == kAskPcp) {
        begin(g_at, kAskPmp, false, del, now);
        return;
    }
    if (g_proto == Proto::Unknown) {
        g_proto = Proto::None;
        g_why   = Why::NoAnswer;
        plat::log("portmap: the router answered neither PCP nor NAT-PMP on port %u; "
                  "nothing is forwarded", static_cast<unsigned>(gatewayPort()));
    } else if (!anyHeld()) {
        g_why = Why::NoAnswer;
    }
    waitFor(now, g_proto == Proto::None ? kRetryMs : kSoonMs);
}

// ---------------------------------------------------------------------------
// granted: a mapping came back. Record it, judge the outside address, and
// say the one line a sysop reads off the console. False when the gateway
// said yes and gave nothing, which the caller must not treat as a mapping.
// ---------------------------------------------------------------------------
bool granted(uint16_t ext, uint32_t life, uint32_t now) {
    Slot& s = g_slot[g_at];
    // A gateway may grant less than was asked for, and zero means it granted
    // nothing at all whatever its result code said.
    if (!life || !ext) {
        g_why = Why::Refused;
        s.flags &= static_cast<uint8_t>(~F_HELD);
        s.external = 0;
        ++g_failed;
        return false;
    }
    // More than was asked for is out of spec in both RFCs, and an absurd
    // figure walks into the half-life arithmetic below (a lifetime over
    // about 8.6 million seconds overflows (life / 2) * 1000 in 32 bits) and
    // into a display sized for four digits of hours (code review).
    if (life > kLifeSecs) life = kLifeSecs;
    s.external  = ext;
    s.lifeSecs  = life;
    s.grantedAt = now;
    s.flags |= F_HELD;
    // The refusal throttle forgets on a success, or a refusal that comes
    // back after a spell of working would be silent for ever (third review).
    g_loggedWhy  = Why::Ok;
    g_loggedCode = 0xFF;
    // Granted after the setting went off: the edge handler in tick() has
    // already run for this pass, so only this can arm the give-back for a
    // mapping the gateway handed over on its way out. Once, with no re-arm,
    // so it cannot become the release loop the edge arming replaced.
    if (!g_on && !g_release) g_release = kSlots;
    // The address decides whether the mapping is worth anything, and this is
    // the finding none of the other reachability rungs can get this cheaply.
    // Three ways, not two: a gateway that grants a mapping while having no
    // outside address of its own used to fall through to "mapped by your
    // router", which with Outside showing "-" is the same contradiction the
    // PCP refusal code was giving (the review's second pass, on PCP, the
    // protocol tried FIRST).
    g_why = !g_ext             ? Why::GatewayBusy
          : privateAddr(g_ext) ? Why::Carrier
                               : Why::Ok;
    char addr[16] = "?";
    if (g_ext) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&g_ext);
        snprintf(addr, sizeof(addr), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    }
    plat::log("portmap: %s mapped port %u to %s:%u for %lu s%s",
              g_proto == Proto::Pcp ? "PCP" : "NAT-PMP",
              static_cast<unsigned>(s.internal), addr, static_cast<unsigned>(ext),
              static_cast<unsigned long>(life),
              g_why == Why::Carrier ? "; that address is NOT on the internet" : "");
    return true;
}

// resultWhy: a protocol's own refusal in this module's words. The two
// codings differ, so each is read against its own RFC.
Why resultWhy(uint8_t code) {
    if (g_ask == kAskPcp) {                     // RFC 6887 section 7.4
        switch (code) {
            case 2:  return Why::Refused;             // NOT_AUTHORIZED
            case 7:                                   // NETWORK_FAILURE
            case 8:                                   // NO_RESOURCES
            case 10:                                  // USER_EX_QUOTA
            // CANNOT_PROVIDE_EXTERNAL: the server has no outside address to
            // give. NOT Why::Carrier, which says "mapped, and that address
            // is your carrier's" and whose advice opens "the mapping
            // worked": here nothing was mapped and the outside address was
            // never read, so the sysop would have been handed a flat
            // contradiction (code review). Carrier is the privateAddr
            // verdict's alone.
            case 11: return Why::GatewayBusy;
            case 12: return Why::Behind;              // ADDRESS_MISMATCH
            default: return Why::Refused;
        }
    }
    switch (code) {                             // RFC 6886 section 3.5
        case 2:  return Why::Refused;                 // the user turned the feature off
        case 3:                                       // the box has no lease of its own
        case 4:  return Why::GatewayBusy;             // out of resources
        default: return Why::Refused;
    }
}

// refused: the gateway answered and the answer was no.
void refused(uint8_t code, uint32_t now, const char* which) {
    g_why  = resultWhy(code);
    g_code = code;
    ++g_failed;
    // A flat refusal is a router setting and will not clear by itself; the
    // others can, so they are asked again in a minute.
    waitFor(now, g_why == Why::Refused ? kRetryMs : kSoonMs);
    // Said when it changes, not once a minute for ever: a kSoonMs refusal is
    // 1,440 console lines a day in the log a sysop reads to diagnose
    // something else (code review; the 1.1.2 runner::post throttle's shape).
    //
    // Its own pair of stamps, which only this function writes. Comparing
    // against g_code did nothing, because both callers set g_code before
    // calling, so 7, 8, 10 and 11 are all Why::GatewayBusy and three of the
    // four would have been silent (the review's second pass).
    if (g_loggedWhy != g_why || g_loggedCode != code) {
        g_loggedWhy  = g_why;
        g_loggedCode = code;
        plat::log("portmap: %s refused port %u, result %u", which,
                  static_cast<unsigned>(g_slot[g_at].internal), static_cast<unsigned>(code));
    }
}

// ---------------------------------------------------------------------------
// service: one step of an exchange in flight. Reads whatever is there,
// retransmits when the gap has passed, gives up after kTries.
// ---------------------------------------------------------------------------
void service(uint32_t now) {
    uint8_t pkt[64];
    for (;;) {
        sockaddr_in from = {};
        socklen_t   flen = sizeof(from);
        const ssize_t n = recvfrom(g_fd, pkt, sizeof(pkt), 0,
                                   reinterpret_cast<sockaddr*>(&from), &flen);
        if (n <= 0) break;
        // Only the gateway's word counts. A UDP socket hears from anybody,
        // and a stranger's packet must not be able to tell the board it has
        // a mapping it does not have, or an outside address it has not got.
        if (from.sin_addr.s_addr != g_gw) continue;
        if (n < 2) continue;

        if (g_ask == kAskPcp) {
            // A NAT-PMP-only gateway answers a version 2 request with
            // version 0 (RFC 6887 section 9: "that means this is a NAT-PMP
            // server"), so the whole exchange starts again in NAT-PMP.
            if (pkt[0] == 0) {
                const bool del = g_delete;
                closeFd();
                begin(g_at, kAskPmp, false, del, now);
                return;
            }
            if (n < 24 || pkt[0] != 2 || pkt[1] != 0x81) continue;
            const uint8_t code = pkt[3];
            if (code == 1) {                           // UNSUPP_VERSION, version 2 in the reply
                const bool del = g_delete;
                closeFd();
                begin(g_at, kAskPmp, false, del, now);
                return;
            }
            // The mapping this reply is about must be the one asked for.
            // RFC 6887 section 8.3 requires the nonce to be checked and a
            // mismatch "silently discarded"; the internal port is the same
            // question asked the other way. The source address above is the
            // real guard, but what gets through without these is a false
            // outside address, and through announce a wrong port published
            // to the directory, which sends every caller to a closed port.
            if (n >= 60) {
                const uint8_t* mm = pkt + 24;
                if (memcmp(mm, g_slot[g_at].nonce, 12) != 0) continue;
                if (get16(mm + 16) != g_slot[g_at].internal) continue;
            }
            g_code  = code;
            g_proto = Proto::Pcp;
            // When the router last ANSWERED, not when it was last
            // asked: a router that has gone deaf never moves this,
            // which is the honest thing for an "as of" to do.
            g_asOf  = clk::epoch();
            const uint32_t epoch = get32(pkt + 8);
            const bool     lost  = forgot(epoch, now);
            g_epoch   = epoch;
            g_epochAt = now;
            closeFd();
            if (code != 0) { refused(code, now, "PCP"); return; }
            if (n < 60) {
                // Success with no MAP part to read. Not a refusal, so it is
                // not said as one; nothing is held and it is asked again.
                g_why = Why::GatewayBusy;
                ++g_failed;
                waitFor(now, kSoonMs);
                plat::log("portmap: PCP answered success in %d bytes, which is too short "
                          "to carry a mapping", static_cast<int>(n));
                return;
            }
            if (g_delete) {
                g_slot[g_at].flags &= static_cast<uint8_t>(~F_HELD);
                g_slot[g_at].external = 0;
                waitFor(now, 0);
                return;
            }
            // A gateway that lost its state is cleared BEFORE this grant is
            // recorded, or the mapping just given would be thrown away too
            // and every renewal would ask twice. drop() clears the outside
            // address as well, so it is read from this packet afterwards.
            if (lost) {
                drop();
                plat::log("portmap: the router restarted and forgot its mappings; asking again");
            }
            // The assigned external address is the MAP part's last 16
            // bytes. IPv4 comes back IPv4-mapped, so it is the last four.
            const uint8_t* m = pkt + 24;
            memcpy(&g_ext, m + 20 + 12, 4);
            g_extAsked = true;                         // PCP's reply carries it
            if (granted(get16(m + 18), get32(pkt + 4), now)) waitFor(now, 0);
            else                                             waitFor(now, kRetryMs);
            return;
        }

        // NAT-PMP
        if (pkt[0] != 0) continue;
        const bool addrReply = pkt[1] == 128;              // 128 + opcode 0
        const bool mapReply  = pkt[1] == 130;              // 128 + opcode 2 (TCP)
        if (!addrReply && !mapReply) continue;
        // And it must answer the question asked. During an address exchange
        // g_at is 0, so without this a map reply for the telnet port would
        // pass the internal-port check below and be taken as a grant.
        if (g_addrAsk != addrReply) continue;
        if (n < (addrReply ? 12 : 16)) continue;
        // The reply's internal port says which mapping it is about; a map
        // reply for another port is not this exchange's (see the PCP note).
        if (mapReply && get16(pkt + 8) != g_slot[g_at].internal) continue;
        const uint16_t code = get16(pkt + 2);
        g_code  = static_cast<uint8_t>(code > 255 ? 255 : code);
        g_proto = Proto::Pmp;
        g_asOf  = clk::epoch();
        const uint32_t epoch = get32(pkt + 4);
        const bool     lost  = forgot(epoch, now);
        g_epoch   = epoch;
        g_epochAt = now;
        closeFd();
        if (code != 0) { refused(g_code, now, "NAT-PMP"); return; }
        if (addrReply) {
            memcpy(&g_ext, pkt + 8, 4);
            if (!g_ext) {
                // The router answered and has no outside address of its own,
                // which is a mapping onto nothing. Said rather than asked
                // again every pass (the first review), and g_extAsked is
                // LEFT false so it is asked again in a minute: a router
                // still waiting on its own WAN lease will have one shortly,
                // and that is not a settled answer. Safe only because a due
                // mapping is picked before this question (see tick): asking
                // above due() would starve the renewal for ever.
                g_why = Why::GatewayBusy;
                waitFor(now, kSoonMs);
                return;
            }
            g_extAsked = true;
            if (anyHeld()) {
                g_why = privateAddr(g_ext) ? Why::Carrier : Why::Ok;
                // Said on the console here as well as on PORTMAP and SYS.
                // On NAT-PMP the mapping is granted BEFORE the address is
                // known, so granted()'s own line cannot carry the verdict,
                // and without this the carrier finding reached every screen
                // except the one a sysop reads over the serial port — on
                // the protocol where it matters most (found by the host
                // test, not by the code reviews).
                if (g_why == Why::Carrier) {
                    char a[16];
                    addrText(g_ext, a, sizeof(a));
                    plat::log("portmap: the router's outside address is %s, which is NOT on "
                              "the internet: callers cannot reach it", a);
                }
            }
            waitFor(now, 0);
            return;
        }
        if (g_delete) {
            g_slot[g_at].flags &= static_cast<uint8_t>(~F_HELD);
            g_slot[g_at].external = 0;
            waitFor(now, 0);
            return;
        }
        if (lost) {
            drop();
            plat::log("portmap: the router restarted and forgot its mappings; asking again");
        }
        if (granted(get16(pkt + 10), get32(pkt + 12), now)) waitFor(now, 0);
        else                                                waitFor(now, kRetryMs);
        return;
    }

    // Nothing yet. The gap doubles with each try, as both RFCs ask.
    const uint32_t gap = kFirstMs << (g_tries ? g_tries - 1 : 0);
    if (plat::since(now, g_sentAt) < gap) return;
    if (g_tries >= kTries) { giveUp(now); return; }
    sendAsk(now);
}

}  // namespace

// ---------------------------------------------------------------------------
void tick(uint32_t now, uint16_t telnetPort, uint16_t sshPort) {
    const bool on = syscfg::get().portMap;

    if (on != g_on) {
        g_on = on;
        if (on) {
            // Switched on: ask at once, and ask the protocol question again
            // in case the sysop has just changed the router as well. The
            // gateway's clock baseline goes too: a stamp from before the
            // board was last switched off can be days old, and forgot()
            // would read that as a gateway that had restarted.
            g_proto    = Proto::Unknown;
            g_why      = Why::NoNetwork;
            g_ext      = 0;
            g_extAsked = false;
            g_mapTried = false;
            g_epoch    = 0;
            g_epochAt  = 0;
            waitFor(now, 0);
            plat::log("portmap: asking the router to forward the board's ports");
        } else if (anyHeld()) {
            // Give back what was granted rather than leaving a hole open for
            // up to two hours. Best effort and strictly bounded: one request
            // a mapping, and then the slots are cleared whatever happened.
            //
            // Armed HERE, on the edge, and nowhere else. Arming it from the
            // state in the !on block below instead looked like it covered a
            // mapping granted after the edge, and made the re-arm test the
            // same test as the exhaustion test, so a deaf router meant a
            // socket and three packets every 1.75 s for ever with the
            // feature switched off (the code review's second pass). The
            // in-flight grant is caught in granted() instead, where it
            // cannot loop.
            g_release = kSlots;
            plat::log("portmap: off; giving back what the router granted");
        } else {
            plat::log("portmap: off");
        }
    }

    if (g_fd >= 0) { service(now); return; }     // an exchange in flight comes first

    if (!on) {
        if (g_release && anyHeld() && (g_proto == Proto::Pcp || g_proto == Proto::Pmp)) {
            for (uint8_t i = 0; i < kSlots; ++i) {
                if (!(g_slot[i].flags & F_HELD)) continue;
                --g_release;
                begin(i, g_proto == Proto::Pcp ? kAskPcp : kAskPmp, false, true, now);
                return;
            }
        }
        if (g_release || anyHeld()) { g_release = 0; drop(); }
        g_why    = Why::Off;
        g_delete = false;
        return;
    }

    lapse(now);

    // Nothing due, which is where nearly every pass ends. The network is
    // read BELOW this rather than above it: plat::netInfo asks the Wi-Fi
    // driver and then the netif, and gatewayIp the netif again, so reading
    // it here would be three driver calls on every one of thousands of
    // passes a second. Below, it is at most once a second with nothing
    // held and once a minute with a mapping held (kCheckMs, which is what
    // bounds how stale the address-change check can be). DashSnap exists
    // for the same reason (Rule no. 1).
    if (waiting(now)) return;

    // No address of our own, or no gateway to ask: nothing to do and nothing
    // wrong. Looked at once a second, not every pass. A board on Ethernet
    // asks the wire's router, because netInfo's ip is the interface callers
    // reach and so the one that needs forwarding to.
    const plat::NetInfo net = plat::netInfo();
    const uint32_t gw   = plat::gatewayIp();
    const uint32_t self = net.ip[0] ? inet_addr(net.ip) : 0;
    if (!gw || !self || self == INADDR_NONE) {
        // Only when nothing is held: one pass with no address, which a
        // reassociation or a DHCP renewal can give, must not rub out a live
        // "mapped" that no renewal will restore for an hour (code review).
        if (!anyHeld()) g_why = Why::NoNetwork;
        waitFor(now, 1000);
        return;
    }
    // Our own address, or the router itself, changed: whatever is forwarded
    // now points at somebody else's lease, so nothing is held however well
    // it was granted. A DHCP lease that moves is the quiet way a working
    // board goes dark, and a router swapped on the same subnet loses every
    // mapping without the board's own address moving at all.
    if (((g_self && self != g_self) || (g_gw && gw != g_gw)) && anyHeld()) {
        drop();
        plat::log("portmap: the network changed; asking the router again");
    }
    g_gw   = gw;
    g_self = self;

    want(telnetPort, sshPort);
    // Two questions want asking and only one exchange runs at a time: a
    // mapping, and (on NAT-PMP only, whose map reply carries no address)
    // the router's own outside address. They ALTERNATE, and getting that
    // wrong cost three review passes, once in each direction:
    //
    //  - the address below the "nothing due" EARLY RETURN meant that with
    //    one mapping wanted and granted, nothing was ever due again for an
    //    hour and the address was never asked: a WROOM said "mapped by your
    //    router" with the carrier test never run, while a board with two
    //    mappings self-corrected by accident, which is the wrong way round;
    //  - the address unconditionally FIRST starves the mapping, because a
    //    router with no outside address of its own answers zero for ever
    //    and would win every pass that is not waiting, so the lease would
    //    run out while the board asked about an address it will never get;
    //  - the mapping unconditionally first starves the address, because a
    //    router that answers and REFUSES leaves a slot wanted and unheld
    //    for ever, so due() always has something and the address is never
    //    read. That is the commonest failure of all, and it is exactly
    //    where a sysop wants to know whether forwarding could have helped.
    //
    // So: whichever was not asked last. g_mapTried is a hint and not state
    // that can be wrong; a stale one costs one extra address request.
    const uint8_t i = due(now);
    const bool wantAddr = g_proto == Proto::Pmp && !g_extAsked;
    // g_extAsked is set where the question is ANSWERED, not here: setting it
    // on the attempt meant one lost datagram, one refusal or one failed
    // socket() left the address never asked again for the life of the
    // mapping, which is the same outcome as not asking at all. Every failure
    // path has a waitFor, so retrying is not a storm. The address request is
    // slot-independent, so slot 0 asks.
    if (wantAddr && (i == 0xFF || g_mapTried)) {
        g_mapTried = false;
        begin(0, kAskPmp, true, false, now);
        return;
    }
    if (i != 0xFF) {
        g_mapTried = true;
        begin(i, g_proto == Proto::Pmp ? kAskPmp : kAskPcp, false, false, now);
        return;
    }
    {
        // Everything wanted is held. Come back when the earliest half-life
        // is up rather than looking at the clock every pass.
        uint32_t soonest = kRetryMs;
        for (uint8_t k = 0; k < kSlots; ++k) {
            const Slot& s = g_slot[k];
            if (!(s.flags & F_HELD)) continue;
            const uint32_t halfMs = (s.lifeSecs / 2u) * 1000u;
            const uint32_t gone   = plat::since(now, s.grantedAt);
            const uint32_t left   = halfMs > gone ? halfMs - gone : 0;
            if (left < soonest) soonest = left;
        }
        // Capped at a minute, even though the renewal is an hour away. The
        // wait also gates the cheap check above that notices this board's
        // address or its router changing, and at an hour the board would go
        // on advertising a mapping that points at somebody else's lease for
        // half a lease (the review's second pass). One netInfo a minute is
        // nothing against DASH's one a second on an open sysop screen, and
        // it bounds how long a confidently wrong status can stand.
        if (soonest > kCheckMs) soonest = kCheckMs;
        waitFor(now, soonest ? soonest : 1000u);
    }
}

// askNow: PORTMAP NOW, and a CONFIG save that changed the row.
//
// g_force, not just a cleared wait: with everything held and inside its
// half-life, due() answers 0xFF and the wait is simply recomputed, so NOW
// said "asking your router now" and did nothing. The case a sysop actually
// reaches for is a held mapping on a carrier address, where they have
// changed something at the provider and want the address read again.
void askNow() {
    waitFor(plat::millis(), 0);
    g_force = true;
    // And the outside address, which on NAT-PMP is a separate question its
    // map reply does not answer: without this, NOW on a held carrier mapping
    // sent one refresh and recomputed the same verdict from the same stale
    // address, which is precisely the case it exists for (the review's
    // second pass). PCP's refresh carries the address, so this is free there.
    g_extAsked = false;
    if (g_proto == Proto::None) g_proto = Proto::Unknown;   // ask the question again
}

Status status() {
    Status st;
    st.on       = g_on;
    st.proto    = g_proto;
    st.why      = g_why;
    st.held     = anyHeld();
    st.asking   = g_fd >= 0;
    st.external = g_ext;
    st.carrier  = g_ext != 0 && privateAddr(g_ext);
    st.code     = g_code;
    st.asked    = g_asked;
    st.failed   = g_failed;
    st.asOf     = g_asOf;
    st.port     = (g_slot[0].flags & F_HELD) ? g_slot[0].external : 0;
#if BBS_HAS_SSH
    st.sshPort  = (g_slot[1].flags & F_HELD) ? g_slot[1].external : 0;
#endif
    const uint32_t now = plat::millis();
    uint32_t left = 0;
    for (uint8_t i = 0; i < kSlots; ++i) {
        const Slot& s = g_slot[i];
        if (!(s.flags & F_HELD)) continue;
        const uint32_t gone = plat::since(now, s.grantedAt) / 1000u;
        const uint32_t rem  = s.lifeSecs > gone ? s.lifeSecs - gone : 0;
        if (!left || rem < left) left = rem;
    }
    st.leftSecs = left;
    return st;
}

// ---------------------------------------------------------------------------
// whyText: the reason in one line, and it is a LINE: CONFIG's read-only box
// holds 55 characters at 80 columns and scrolls a longer value to its TAIL,
// which on a row nothing can focus would show a sysop the end of a sentence
// and not the start. So every string here is 52 characters or fewer, and
// the advice that does not fit goes in whatToDo, which PORTMAP wraps.
// ---------------------------------------------------------------------------
const char* whyText(const Status& st) {
    switch (st.why) {
        case Why::Ok:          return "mapped by your router";
        case Why::Off:         return "off: yes asks your router to forward it";
        case Why::NoNetwork:   return "no network address yet";
        case Why::NoAnswer:    return "your router does not do this, or it is off";
        case Why::Refused:     return "your router said no";
        case Why::GatewayBusy: return "your router has no address of its own";
        case Why::Carrier:     return "mapped, but that address is your carrier's";
        case Why::Behind:      return "another router sits in between";
        case Why::NoSocket:    return "no socket was spare; asking again shortly";
        case Why::Lapsed:      return "the mapping ran out and has not come back";
    }
    return "-";
}

// ---------------------------------------------------------------------------
// whatToDo: the sentence a sysop can act on, or nothing when there is
// nothing to do. Printed wrapped by PORTMAP, so it has no length limit, and
// it is the whole reason that command exists: "your router said no" and
// "your router has no outside address of its own" want different evenings.
// ---------------------------------------------------------------------------
const char* whatToDo(const Status& st) {
    switch (st.why) {
        case Why::NoAnswer:
            return "Look for UPnP or NAT-PMP in your router's menu. Some routers have "
                   "neither, and then the port has to be forwarded by hand.";
        case Why::Refused:
            return "The router understood and said no, which usually means the feature is "
                   "switched off in its menu. Look for UPnP or NAT-PMP there.";
        case Why::GatewayBusy:
            return "Your router has no outside address of its own, or no room for another "
                   "mapping. Nothing to do here; it is asked again shortly.";
        case Why::Carrier:
            return "The mapping worked and it cannot help: that address is your internet "
                   "provider's, not yours, so callers never reach your router at all. Ask "
                   "them for a public address, or reach the board another way.";
        case Why::Behind:
            return "A second router sits between this board and the one that answered, so "
                   "forwarding here opens nothing. Move the board to the outer router, or "
                   "forward the port on both by hand.";
        case Why::Lapsed:
            return "The router granted a mapping and stopped renewing it. PORTMAP NOW asks "
                   "again; if it keeps happening, forward the port by hand.";
        case Why::Ok:
            return "This says the router did as it was asked, not that a caller gets "
                   "through. Dial the board from a phone on mobile data to be sure.";
        default:
            return nullptr;
    }
}

const char* whyShort(const Status& st) {
    switch (st.why) {
        case Why::Ok:          return "mapped";
        case Why::Off:         return "off";
        case Why::NoNetwork:   return "no address yet";
        case Why::NoAnswer:    return "router will not";
        case Why::Refused:     return "router said no";
        case Why::GatewayBusy: return "router busy";
        case Why::Carrier:     return "carrier address";
        case Why::Behind:      return "another router";
        case Why::NoSocket:    return "no socket spare";
        case Why::Lapsed:      return "ran out";
    }
    return "-";
}

void addrText(uint32_t netOrder, char* out, size_t n) {
    if (!n) return;
    if (!netOrder) { snprintf(out, n, "-"); return; }
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&netOrder);
    snprintf(out, n, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

// ---------------------------------------------------------------------------
// line: the one-line form, for CONFIG network's read-only row and for SYS.
//
// "as of" because a mapping is a belief: the gateway was asked, it answered,
// and nothing since has tested whether a packet from outside arrives. The
// same honesty as the kept free-space figures (src/core/space.*), and the
// reason the word is "mapped" and never "reachable".
// ---------------------------------------------------------------------------
void line(char* out, size_t n, bool wide) {
    if (!n) return;
    const Status st = status();
    if (!st.on) { snprintf(out, n, "%s", wide ? whyText(st) : "off"); return; }
    char when[8] = "";
    if (st.asOf) clk::fmtEpoch(when, sizeof(when), "%H:%M", st.asOf);
    if (st.held) {
        char addr[16];
        addrText(st.external, addr, sizeof(addr));
        // On a board where only the SSH mapping is held, the number shown is
        // the SSH one and says so, rather than reading as the telnet port.
        const unsigned shown = st.port ? st.port : st.sshPort;
        const char* which    = st.port ? "" : " ssh";
        // kWide is CONFIG's box at 80 columns, 56 less the one
        // Form::drawField keeps, and a longer value is scrolled to its TAIL:
        // on a row nothing can focus that shows the end of a sentence and
        // not the start, so nothing here may exceed it. The full form is
        // 53 with a 12-character address and 57 with "255.255.255.255" and
        // the ssh marker, so it is measured and the "as of" dropped rather
        // than guessed at: statRow's "a note that would wrap is left off"
        // pattern. The carrier form spends the lease's room on saying the
        // mapping is worthless, which is the more useful thing to know.
        constexpr int kWide = 55;
        if (wide && st.carrier) {
            snprintf(out, n, "%s:%u%s mapped, NOT on the internet", addr, shown, which);
        } else if (wide) {
            const unsigned long h = st.leftSecs / 3600u;
            const unsigned long m = (st.leftSecs % 3600u) / 60u;
            const int len = snprintf(out, n, "%s:%u%s mapped, %luh%02lum left, as of %s",
                                     addr, shown, which, h, m, when[0] ? when : "-");
            if (len < 0 || len > kWide)
                snprintf(out, n, "%s:%u%s mapped, %luh%02lum left", addr, shown, which, h, m);
        } else {
            snprintf(out, n, "%s %s", st.carrier ? "carrier addr" : "mapped", when);
        }
        return;
    }
    if (st.asking) { snprintf(out, n, "%s", wide ? "asking the router..." : "asking..."); return; }
    snprintf(out, n, "%s", wide ? whyText(st) : whyShort(st));
}

uint16_t externalPort(uint16_t internal) {
    for (uint8_t i = 0; i < kSlots; ++i) {
        const Slot& s = g_slot[i];
        if ((s.flags & F_HELD) && s.internal == internal) return s.external;
    }
    return 0;
}

}  // namespace portmap
