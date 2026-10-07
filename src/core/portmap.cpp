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
 * UPnP IGD, UPnP Device Architecture 1.1 and the IGD service template
 *   Three protocols stacked, and the board speaks the smallest useful part
 *   of each. Every number below is from the specification, not from memory.
 *
 *   1. SSDP search (UDA 1.1 section 1.3.2). An M-SEARCH request over UDP:
 *        M-SEARCH * HTTP/1.1
 *        HOST: 239.255.255.250:1900
 *        MAN: "ssdp:discover"
 *        MX: 2
 *        ST: upnp:rootdevice
 *      sent to the multicast group AND unicast to the default gateway. Two
 *      sends because the multicast one is what the specification describes
 *      and the unicast one (UDA 1.1's own "unicast search", same headers to
 *      the device's port 1900) reaches a router whose LAN bridge drops
 *      multicast, which some do. The reply is a datagram of HTTP-shaped
 *      headers whose LOCATION names the device description.
 *
 *      ST is upnp:rootdevice rather than InternetGatewayDevice:1, and that
 *      is a deliberate trade. The narrower search misses an IGD:2 router
 *      that does not also advertise :1, and miniupnpc works round it by
 *      sending four searches in turn. The wider one is answered by every
 *      UPnP device on the LAN, a television and a printer included, and
 *      what makes it safe here is that only the gateway's own answer is
 *      read: the source address is checked against plat::gatewayIp the same
 *      way the UDP protocols' replies are. A router that answers and has no
 *      WAN connection service is then the same outcome as one that does not
 *      answer, which is the truth about it.
 *
 *      Header names in SSDP are case-insensitive (it is HTTP's grammar), so
 *      LOCATION is found case-insensitively; everything else here is XML,
 *      which is not.
 *
 *   2. The device description, fetched with an ordinary HTTP/1.1 GET of
 *      LOCATION's path. Inside it, the service this board wants is a
 *      <service> whose <serviceType> is
 *        urn:schemas-upnp-org:service:WANIPConnection:1   (or :2, IGD2)
 *        urn:schemas-upnp-org:service:WANPPPConnection:1  (a PPP link)
 *      and what is wanted out of that element is its <controlURL>. The
 *      template in UDA 1.1 section 2.3 puts serviceType first and
 *      controlURL fourth, but the scan here does not rely on that: it keeps
 *      the last controlURL seen inside the current <service> and decides at
 *      </service>, so either order works for one buffer.
 *
 *   3. SOAP, one POST per action to that control URL. Three actions:
 *        AddPortMapping          NewRemoteHost (empty), NewExternalPort,
 *                                NewProtocol, NewInternalPort,
 *                                NewInternalClient, NewEnabled,
 *                                NewPortMappingDescription,
 *                                NewLeaseDuration
 *        DeletePortMapping       NewRemoteHost, NewExternalPort, NewProtocol
 *        GetExternalIPAddress    no arguments; answers
 *                                <NewExternalIPAddress>
 *      A fault comes back as HTTP 500 carrying
 *      <UPnPError><errorCode>NNN</errorCode>, and the codes that mean
 *      something different from "no" are:
 *        402 invalid args, 501 action failed, 606 not authorised
 *        713 specified array index invalid
 *        714 NoSuchEntryInArray   (a delete of a mapping that is not there,
 *                                 which is a successful delete)
 *        718 ConflictInMappingEntry  the external port belongs to another
 *                                 internal host, so another number is
 *                                 tried: that is what externalPort() and
 *                                 announce's published port are for
 *        725 OnlyPermanentLeasesSupported  the lease is asked for again as
 *                                 0, which the IGD template defines as
 *                                 never expiring
 *      AddPortMapping returns no values, so the external port of a granted
 *      mapping is the one that was asked for and not one read back. IGD2's
 *      AddAnyPortMapping does return a port and is not used.
 *
 *      Nothing here reads the HTTP status line. What decides the answer is
 *      the body: an errorCode is a fault, an <action>Response is a success,
 *      and a connection that closes with neither is a refusal with no code.
 *      That is one parse rather than two, and it cannot disagree with
 *      itself the way a 200 carrying a fault would.
 *
 *   The description is scanned as it arrives and never held (portmap.h,
 *   Rule no. 1), which is why the matching is the little Lit machines below
 *   rather than strstr over a buffer.
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
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstdio>
#include <cstring>
#include <strings.h>            // strncasecmp: SSDP header names have no case

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
constexpr uint8_t kAskPcp  = 0;
constexpr uint8_t kAskPmp  = 1;
constexpr uint8_t kAskUpnp = 2;

constexpr uint8_t F_WANT  = 0x01;   // this port should be mapped
constexpr uint8_t F_HELD  = 0x02;   // granted, and its lifetime has not run out
constexpr uint8_t F_NONCE = 0x04;   // the PCP nonce is set, so a refresh reuses it
// Granted with no expiry: a UPnP router that answered 725 and took a lease
// of 0, which the IGD template defines as never expiring. lifeSecs then
// says nothing about this mapping and lapse() must leave it alone, which is
// the whole reason the flag exists rather than a sentinel in lifeSecs.
constexpr uint8_t F_PERM  = 0x08;

struct Slot {
    uint16_t internal  = 0;
    uint16_t external  = 0;
    uint32_t lifeSecs  = 0;         // what was granted
    uint32_t grantedAt = 0;         // millis of the grant
    uint8_t  nonce[12] = {};        // PCP's, per mapping (RFC 6887 section 11.2)
    uint8_t  flags     = 0;
    // How many outside port numbers have been tried for this mapping after
    // a UPnP 718 ConflictInMappingEntry, which says the number belongs to
    // another host on the LAN. Reset on a grant and on a changed port. The
    // other two protocols need none of this: they pick a free number
    // themselves and tell the board which one.
    uint8_t  clash     = 0;
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
// Which slots the give-back has already asked about, one bit each. g_release
// counted ATTEMPTS, so with two mappings and a router that answers the add
// and not the delete, slot 0 was asked twice and slot 1 never: on a UPnP
// router that only grants permanent mappings, that is an SSH hole left open
// for good (the code review's second pass). One try per held slot now.
uint8_t  g_relTried = 0;
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
// When the exchange in flight started, for UPnP's whole-exchange budget.
// The UDP protocols count sends instead, because each send is one datagram
// and the answer is the next one; a UPnP exchange is three protocols deep
// and a stall can be in any of them.
uint32_t g_began    = 0;
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
// The protocol's own last result code. 16 bits because UPnP's are three
// digits (402, 718, 725) where the two UDP protocols' are one or two.
uint16_t g_code     = 0;
// What refused() last said on the console, so it says it again only when
// the reason or the code moves. Its own, because g_code is set by the
// callers before they call.
Why      g_loggedWhy  = Why::Ok;
uint16_t g_loggedCode = 0xFFFF;
uint16_t g_asked    = 0;
uint16_t g_failed   = 0;

void waitFor(uint32_t now, uint32_t ms) { g_waitFrom = now; g_waitMs = ms; }

// askOf: the exchange kind for a protocol already settled on. Unknown and
// None both answer PCP, which is where a fresh probe starts.
uint8_t askOf(Proto p) {
    return p == Proto::Pmp ? kAskPmp : p == Proto::Upnp ? kAskUpnp : kAskPcp;
}
bool  waiting(uint32_t now) { return g_waitMs && plat::since(now, g_waitFrom) < g_waitMs; }

// ===========================================================================
// UPnP IGD. See the file comment for the protocols and the clause each rule
// comes from. Everything here is spread across ticks: the whole feature's
// one rule is that no pass of the loop waits for the network.
// ===========================================================================

// SSDP's group and port (UDA 1.1 section 1.3.1). The port is a constant on
// a board; on the host a test names its stand-in router's in BBS_HOST_SSDP,
// because 1900 is one number and the lanes run side by side.
constexpr uint16_t kSsdpPort  = 1900;
// 239.255.255.250, turned into network order with htonl at the send rather
// than written out as a packed literal: the literal is right only on a
// little-endian target, which both of ours are and neither of which should
// be the reason a constant is correct.
constexpr uint32_t kSsdpHost  = 0xEFFFFFFAu;

// The whole exchange's budget, from begin() to an answer: an SSDP search, a
// description that may be twenty kilobytes and a SOAP round trip, all on a
// LAN. Past it the exchange is abandoned exactly as a silent UDP one is.
// Generous on purpose: the cost of being slow here is nothing, and the cost
// of being impatient is a router that works being reported as absent.
constexpr uint32_t kUpnpMs    = 8000;

// What one pass may read and scan. 512 bytes of scanning is single-figure
// microseconds and two recv calls are tens, so a pass stays far inside the
// 100 us line; a description arriving in 512 byte bites still finishes in a
// fraction of a second, because the loop runs hundreds of passes in one.
constexpr uint16_t kUpnpChunk = 256;
constexpr uint8_t  kUpnpReads = 2;

// And the most of a description that is ever looked at. A router that
// streams for ever, or one whose description genuinely has no WAN service
// in the first 64 KB, is a router this does not work with; the alternative
// is a loop with no end in it.
constexpr uint32_t kUpnpMaxXml = 65536;

// Outside port numbers tried after a 718 before giving up. Three: the one
// the board listens on, and two above it. More would be a port scan of the
// router's own table, and a sysop who needs a fourth has a conflict worth
// looking at by hand.
constexpr uint8_t  kClashTries = 3;

// What a mapping is called in the router's own table. A sysop reading that
// table should recognise it at once; this is the only string the board puts
// in somebody else's user interface.
constexpr char kMapDesc[] = "unleashed BBS";

// Where a UPnP exchange has got to.
constexpr uint8_t U_SEARCH = 0;     // SSDP out, waiting for a LOCATION
constexpr uint8_t U_DESC   = 1;     // GET the description, scan for a control URL
constexpr uint8_t U_SOAP   = 2;     // POST the action, scan for its answer

// Which action a U_SOAP exchange is making.
constexpr uint8_t A_ADD = 0;
constexpr uint8_t A_DEL = 1;
constexpr uint8_t A_EXT = 2;

// The service types, in the order they are preferred. WANIPConnection is an
// ordinary routed link and WANPPPConnection is a PPP one (DSL); a router
// with both answers on either, and the IP one is the right first guess.
constexpr uint8_t S_IP  = 0;
constexpr uint8_t S_PPP = 1;

// ---------------------------------------------------------------------------
// Lit: one literal, matched across read boundaries so the description never
// has to be held. feed() returns true on the byte that completes it.
//
// The restart is the naive one, "does this byte start the pattern again",
// which is EXACT for every pattern whose first byte never recurs inside it.
// A pattern like "aab" would need the real failure function. That condition
// is checkable at compile time, so litSafe below checks it rather than a
// comment hoping somebody reads it: the fourth time in this project's
// history that a comment asserting a property was believed is three times
// too many.
// ---------------------------------------------------------------------------
constexpr bool litSafe(const char* p) {
    for (size_t i = 1; p[i]; ++i)
        if (p[i] == p[0]) return false;
    return true;
}
struct Lit {
    const char* pat;
    uint8_t     at;
    bool feed(char c) {
        if (c == pat[at]) {
            if (!pat[++at]) { at = 0; return true; }
            return false;
        }
        at = (c == pat[0]) ? 1 : 0;
        return false;
    }
    void reset() { at = 0; }
};

// What the scanner is currently copying out, 0 for nothing. A capture runs
// from the byte after its literal to the next '<', which is how XML element
// text ends.
constexpr uint8_t C_NONE = 0;
constexpr uint8_t C_CTL  = 1;       // a <controlURL>, into g_uCtl
constexpr uint8_t C_VER  = 2;       // the digit after "WANxxConnection:"
constexpr uint8_t C_ERR  = 3;       // a SOAP <errorCode>, into g_uCode
constexpr uint8_t C_IP   = 4;       // <NewExternalIPAddress>, into g_uVal

uint8_t  g_uStage  = U_SEARCH;
uint8_t  g_uAct    = A_ADD;
uint32_t g_uHost   = 0;             // the description and control host, network order
uint16_t g_uHttp   = 0;             // and its TCP port
char     g_uPath[96] = {};          // LOCATION's path, for the GET
char     g_uCtl[96]  = {};          // the control URL's path; also the scan's capture
char     g_uVal[16]  = {};          // the outside address a GetExternalIPAddress gave
uint8_t  g_uSvc    = S_IP;          // which service the control URL belongs to
uint8_t  g_uVer    = 1;             // its version digit, 1 or 2
bool     g_uPerm   = false;         // ask for lease 0: this router gave 725
// Throw the discovery away before the NEXT exchange, rather than now.
//
// PORTMAP NOW and a CONFIG network save both reach askNow, and the shell
// runs earlier in a pass than Bbs::tick calls portmap::tick, so an
// immediate upnpForget() could land between a SOAP request's first and last
// byte: sendUpnpReq rebuilds the request from g_uCtl, g_uHost and g_uHttp on
// every pass, so a forget mid-send puts "POST  HTTP/1.1 / HOST: -:0" on the
// wire, or a splice of two different requests, and the router's 400 came
// back as "your router said no" for an hour on a router that works. It is a
// flag consumed by upnpBegin instead, which only ever runs between
// exchanges (the code review, HIGH).
bool     g_uRefind = false;
// The last upnpBegin failed at socket() rather than at connect(). See
// begin()'s UPnP arm: the two want different answers and only this can
// tell them apart.
bool     g_noSock  = false;
// Where a TCP stage has got to. Three states rather than a "connected"
// flag, because "the request is all away" is a third thing and rebuilding
// the request on a pass that has nothing left to send is waste.
constexpr uint8_t P_CONN = 0;
constexpr uint8_t P_SEND = 1;
constexpr uint8_t P_READ = 2;
uint8_t  g_uPhase  = P_CONN;
bool     g_uDone   = false;         // the answer this exchange wanted has been seen
uint16_t g_uSent   = 0;             // bytes of the request already away
uint16_t g_uCode   = 0;             // the SOAP errorCode, 0 none
uint32_t g_uRead   = 0;             // bytes of body scanned, against kUpnpMaxXml
uint8_t  g_uCap    = C_NONE;
uint8_t  g_uCapLen = 0;
uint16_t g_uExt    = 0;             // the outside port this exchange asked for
// Set while a <service> element is open, and cleared at its end, so a
// controlURL and a serviceType can arrive in either order inside it.
bool     g_uInSvc  = false;
// A control URL went past and was refused for holding a byte an HTTP
// request line must not carry. Kept because the two failures want different
// words: a description with no WAN service is one thing a sysop can do
// nothing about, and a description whose control URL this board will not
// use is a hand-made forward (the code review's second pass; the header's
// second Honest rule).
bool     g_uCtlBad = false;
bool     g_uSvcHit = false;         // this service is a WAN connection one
bool     g_uCtlHit = false;         // and a controlURL has been captured for it

// The literals, one set per stage. Their match positions are state, which
// is the whole point: a pattern may be cut in half by a recv boundary.
Lit g_lSvcOpen  { "<service>",                 0 };
Lit g_lSvcShut  { "</service>",                0 };
Lit g_lWanIp    { "WANIPConnection:",          0 };
Lit g_lWanPpp   { "WANPPPConnection:",         0 };
Lit g_lCtl      { "<controlURL>",              0 };
Lit g_lErr      { "<errorCode>",               0 };
Lit g_lAddOk    { "AddPortMappingResponse",    0 };
Lit g_lDelOk    { "DeletePortMappingResponse", 0 };
Lit g_lExtIp    { "<NewExternalIPAddress>",    0 };

// Every one of them, against the restart's precondition. A tenth pattern
// that needs a real failure function fails the build here rather than
// quietly matching text that is not it.
static_assert(litSafe("<service>") && litSafe("</service>") &&
              litSafe("WANIPConnection:") && litSafe("WANPPPConnection:") &&
              litSafe("<controlURL>") && litSafe("<errorCode>") &&
              litSafe("AddPortMappingResponse") &&
              litSafe("DeletePortMappingResponse") &&
              litSafe("<NewExternalIPAddress>"),
              "a Lit pattern whose first byte recurs needs a real failure function");

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

// Declared here and defined with the rest of the UPnP machinery below:
// drop() needs it, and the shortest honest way to say so is a prototype
// rather than moving three stages of a protocol above it.
void upnpForget();

// noSockSoon: the board ran out of sockets part way through an exchange.
//
// Told apart from a router that said nothing, because they want opposite
// answers: a shortage is the board's own and clears in seconds, so it keeps
// what it has learned (the control URL) and asks again in a minute, where
// giveUp would forget the URL, blame the router in the log and wait an hour.
// Returns true when it handled it, so a caller reads as one line.
bool noSockSoon(uint32_t now);

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
        g_slot[i].flags &= static_cast<uint8_t>(~(F_HELD | F_PERM));
        g_slot[i].external = 0;
        g_slot[i].clash    = 0;
    }
    g_ext      = 0;
    g_extAsked = false;
    // And whatever UPnP discovery found, because the two callers of this
    // are a changed network and a gateway that lost its state, and in both
    // the description's address is a guess. One SSDP search and one GET is
    // what re-finding it costs; believing a stale control URL costs an hour
    // of a sysop reading "your router said no".
    upnpForget();
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
            s.clash    = 0;
            s.flags &= static_cast<uint8_t>(~(F_HELD | F_NONCE | F_PERM));
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
        // A UPnP router that would only make a permanent mapping gave no
        // lease to run out, so lifeSecs says nothing about this one: it is
        // re-asserted hourly like the rest and never expires here.
        if (s.flags & F_PERM) continue;
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

// ===========================================================================
// UPnP IGD, the three stages. Nothing below waits for the network.
// ===========================================================================

// ssdpPort: 1900 on a board (UDA 1.1 section 1.3.1). On the host a test
// names its stand-in router's port, for the same reason gatewayPort does.
uint16_t ssdpPort() {
#ifdef BBS_HOST
    const uint16_t p = plat::hostSsdpPort();
    if (p) return p;
#endif
    return kSsdpPort;
}

// svcUrn: the service type this board found, written out. The SOAPACTION
// header and the action element's xmlns must both carry the service type
// the control URL belongs to, and a router with a WANPPPConnection answers
// nothing to a WANIPConnection action.
const char* svcUrn(char* out, size_t n) {
    snprintf(out, n, "urn:schemas-upnp-org:service:WAN%sConnection:%u",
             g_uSvc == S_PPP ? "PPP" : "IP", static_cast<unsigned>(g_uVer));
    return out;
}

// ---------------------------------------------------------------------------
// upnpForget: throw away what discovery found, so the next exchange starts
// at the SSDP search again.
//
// Called where the network moved under the board (drop) and where the
// control URL stopped working, never merely because an action was refused:
// a router that says 718 has answered, and re-discovering it would turn one
// refusal into a full three-stage round every minute.
// ---------------------------------------------------------------------------
void upnpForget() {
    g_uHost    = 0;
    g_uHttp    = 0;
    g_uPath[0] = '\0';
    g_uCtl[0]  = '\0';
    g_uVal[0]  = '\0';
    g_uSvc     = S_IP;
    g_uVer     = 1;
    // g_uPerm is NOT cleared: it is a fact about this router's firmware,
    // learned from a 725, and a router does not stop being
    // permanent-leases-only because the board re-read its description.
    // Clearing it would send the same 7200 again and collect the same 725,
    // once an hour, for ever.
}

// resetScan: the matchers and the per-element flags, before a body is read.
// Every Lit holds a match position, so a stale one would see the first half
// of a pattern that arrived in the LAST reply.
void resetScan() {
    g_lSvcOpen.reset(); g_lSvcShut.reset();
    g_lWanIp.reset();   g_lWanPpp.reset();  g_lCtl.reset();
    g_lErr.reset();     g_lAddOk.reset();   g_lDelOk.reset(); g_lExtIp.reset();
    g_uCap    = C_NONE;
    g_uCapLen = 0;
    g_uInSvc  = false;
    g_uSvcHit = false;
    g_uCtlHit = false;
    g_uCtlBad = false;
    g_uDone   = false;
    g_uRead   = 0;
    g_uCode   = 0;
}

// ---------------------------------------------------------------------------
// sendSsdp: one M-SEARCH. Twice over: to the group, which is what the
// specification describes, and unicast to the gateway, which is UDA 1.1's
// own unicast search and reaches a router whose LAN bridge drops multicast.
// The unicast form carries the device's own HOST and no MX, as the
// specification has it, rather than the multicast message sent to a second
// address.
// ---------------------------------------------------------------------------
void sendSsdp(uint32_t now) {
    char req[256];
    char host[16];
    sockaddr_in to = {};
    to.sin_family = AF_INET;

    // Unicast, to the router itself. First, because it is the one that
    // matters on a host test and on a router that filters the group.
    addrText(g_gw, host, sizeof(host));
    snprintf(req, sizeof(req),
             "M-SEARCH * HTTP/1.1\r\n"
             "HOST: %s:%u\r\n"
             "MAN: \"ssdp:discover\"\r\n"
             "ST: upnp:rootdevice\r\n"
             "\r\n", host, static_cast<unsigned>(ssdpPort()));
    to.sin_port        = htons(ssdpPort());
    to.sin_addr.s_addr = g_gw;
    sendto(g_fd, req, strlen(req), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));

#ifndef BBS_HOST
    // And to the group. MX is seconds a device may wait before answering,
    // and 1 rather than the specification's suggested larger values because
    // only the gateway's answer is read and it is on the same subnet: a
    // longer MX buys a quieter LAN for devices this board ignores anyway.
    //
    // Host builds skip this: a test lane has nothing listening on the group
    // and should not put a datagram on the operator's own network to find
    // that out.
    snprintf(req, sizeof(req),
             "M-SEARCH * HTTP/1.1\r\n"
             "HOST: 239.255.255.250:%u\r\n"
             "MAN: \"ssdp:discover\"\r\n"
             "MX: 1\r\n"
             "ST: upnp:rootdevice\r\n"
             "\r\n", static_cast<unsigned>(kSsdpPort));
    to.sin_port        = htons(kSsdpPort);
    to.sin_addr.s_addr = htonl(kSsdpHost);
    sendto(g_fd, req, strlen(req), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
#endif

    g_sentAt = now;
    ++g_tries;
}

// ---------------------------------------------------------------------------
// upnpBegin: start a UPnP exchange about slot i.
//
// With a control URL already in hand it goes straight to the SOAP POST;
// without one it opens the UDP socket and searches. The socket is bound to
// the board's own address so that on a board with a wired port and a radio
// both, the search leaves by the interface callers reach the board on,
// which is the one that needs forwarding to.
// ---------------------------------------------------------------------------
// openTcp: a non-blocking connection to the description or control host,
// and the scanners cleared for the body that will come back. Shared by the
// SOAP stage and by the hand-off from the description to it.
bool openTcp(uint32_t now) {
    closeFd();
    g_noSock = false;
    g_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_fd < 0) { g_noSock = true; return false; }
    const int fl = fcntl(g_fd, F_GETFL, 0);
    fcntl(g_fd, F_SETFL, fl | O_NONBLOCK);
    sockaddr_in a = {};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(g_uHttp);
    a.sin_addr.s_addr = g_uHost;
    const int r = connect(g_fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    if (r == 0) {
        g_uPhase = P_SEND;
    } else if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EALREADY) {
        g_uPhase = P_CONN;
    } else {
        closeFd();
        return false;
    }
    g_uSent  = 0;
    g_sentAt = now;
    resetScan();
    return true;
}

bool upnpBegin(uint32_t now) {
    // A flag left standing costs nothing: upnpBegin is the only reader, so
    // a g_uRefind set on an edge whose next round settled on PCP or NAT-PMP
    // just means the FIRST UPnP exchange after it re-discovers, which is
    // what it should do anyway. It is not state that can be wrong, only
    // state that can be early.
    // Never on the way OUT. A CONFIG save that switches the setting off
    // calls askNow too, which armed this, and consuming it here threw away
    // the control URL the mapping was granted on: the DeletePortMapping
    // then had to complete a whole fresh search and description fetch
    // inside kUpnpMs before it could be sent, and on a 725 router that
    // give-back is the only mitigation portmap.h offers for a mapping that
    // never expires (the code review's second pass). A delete wants the
    // URL the mapping was made on, never a newer one.
    if (g_uRefind && g_uAct != A_DEL) {
        // A sysop asked for this: PORTMAP NOW, or a CONFIG save. The
        // permanent-lease fact goes with it, because the one thing NOW is
        // reached for is a router whose menu, or whose box, has just
        // changed. It costs one 725 round on a router that has not.
        g_uRefind = false;
        g_uPerm   = false;
        upnpForget();
    }
    if (g_uCtl[0] && g_uHost) {
        g_uStage = U_SOAP;
        return openTcp(now);
    }

    g_uStage  = U_SEARCH;
    g_uCtl[0] = '\0';                // nothing half-discovered can look found
    g_noSock  = false;
    g_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_fd < 0) { g_noSock = true; return false; }
    const int fl = fcntl(g_fd, F_GETFL, 0);
    fcntl(g_fd, F_SETFL, fl | O_NONBLOCK);
    sockaddr_in me = {};
    me.sin_family      = AF_INET;
    me.sin_addr.s_addr = g_self;
    // Said when it fails, because the comment above is then untrue: the
    // search leaves by whatever interface the route picks, which on a board
    // with a wire and a radio both may not be the one callers reach.
    if (bind(g_fd, reinterpret_cast<sockaddr*>(&me), sizeof(me)) != 0)
        plat::log("portmap: could not bind the UPnP search to this board's own "
                  "address; it will leave by the default route");
#ifndef BBS_HOST
    // Which interface the group leaves by, and how far it may travel. UDA
    // 1.1 says the TTL should default to 2; the router is on-link, so this
    // is belt and braces either way. Both are best effort: a stack that
    // refuses them still sends the unicast search.
    // Guarded by name: lwIP defines these only with
    // LWIP_MULTICAST_TX_OPTIONS, so a missing macro would be a build
    // failure on an image that otherwise works, and a stack without them
    // still sends to the group by its route.
#ifdef IP_MULTICAST_IF
    in_addr mif = {};
    mif.s_addr = g_self;
    setsockopt(g_fd, IPPROTO_IP, IP_MULTICAST_IF, &mif, sizeof(mif));
#endif
#ifdef IP_MULTICAST_TTL
    const uint8_t ttl = 2;
    setsockopt(g_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
#endif
#endif
    resetScan();
    sendSsdp(now);
    return true;
}

// ---------------------------------------------------------------------------
// takeLocation: pull the description's host, port and path out of an SSDP
// reply. False when there is nothing usable in it.
//
// SSDP carries HTTP's header grammar, so the name is matched without case.
// A LOCATION whose host is a NAME is refused rather than resolved: that
// would be DNS on the loop, and a router that cannot name itself by address
// in its own SSDP reply is not one this will work with (portmap.h).
// ---------------------------------------------------------------------------
bool takeLocation(const char* reply) {
    const char* p = reply;
    const char* loc = nullptr;
    for (; *p; ++p) {
        if ((*p == 'l' || *p == 'L') && strncasecmp(p, "location:", 9) == 0) { loc = p + 9; break; }
    }
    if (!loc) return false;
    while (*loc == ' ' || *loc == '\t') ++loc;
    if (strncasecmp(loc, "http://", 7) != 0) return false;
    loc += 7;

    char host[48];
    size_t h = 0;
    while (*loc && *loc != ':' && *loc != '/' && *loc != '\r' && *loc != '\n' && *loc != ' ') {
        if (h + 1 >= sizeof(host)) return false;
        host[h++] = *loc++;
    }
    host[h] = '\0';
    if (!h) return false;
    const in_addr_t a = inet_addr(host);
    if (a == INADDR_NONE) return false;
    // And it must be the GATEWAY's address. This is the one place the
    // "only the gateway's answer is read" guard does not carry itself: a
    // PCP or NAT-PMP reply contains no address the board then goes and
    // contacts, where an SSDP reply's LOCATION is a redirect. UDP source
    // addresses spoof, so without this one datagram from anything on the
    // LAN made the board fetch a stranger's description, take their control
    // URL, believe their outside address, and publish their port to the
    // directory as the one callers should dial (the code review, HIGH).
    // A real IGD serves its description from the address it routes on.
    if (a != static_cast<in_addr_t>(g_gw)) return false;

    uint16_t port = 80;
    if (*loc == ':') {
        ++loc;
        unsigned v = 0;
        while (*loc >= '0' && *loc <= '9') { v = v * 10u + static_cast<unsigned>(*loc++ - '0'); if (v > 65535u) return false; }
        if (!v) return false;
        port = static_cast<uint16_t>(v);
    }

    size_t n = 0;
    if (*loc != '/') { g_uPath[n++] = '/'; }
    // Printable, non-space ASCII only, the same rule feedDesc's control URL
    // already uses, and for the same reason: this path goes straight into a
    // "GET %s HTTP/1.1" request line. Stopping only at CR, LF, space and
    // NUL let tab, 0x0B, 0x0C, 0x7F and every byte from 0x80 up through.
    // No header injection was possible either way, since CR and LF were
    // both already excluded; what it bought was a malformed GET the router
    // answers 400 to, reported to the sysop as "the router answered
    // nothing" about a router that works. One rule for both paths.
    // Written against an explicit unsigned byte, not `*loc > 0x20`: whether
    // plain char is signed is a per-target choice, so the signed form would
    // reject 0x80 and up on Xtensa and accept them on a target where char
    // is unsigned, which is the worst kind of difference to leave in a
    // parser of somebody else's bytes.
    for (;;) {
        const unsigned char c = static_cast<unsigned char>(*loc);
        if (c <= 0x20 || c >= 0x7F) break;
        if (n + 1 >= sizeof(g_uPath)) return false;
        g_uPath[n++] = *loc++;
    }
    g_uPath[n] = '\0';
    if (!n) { g_uPath[0] = '/'; g_uPath[1] = '\0'; }
    g_uHost = a;
    g_uHttp = port;
    return true;
}

// ---------------------------------------------------------------------------
// takeControl: the control URL the description gave, resolved against the
// host LOCATION named.
//
// An absolute one moves the host and port with it, which is the only case
// where a router's control service lives somewhere other than its
// description. <URLBase> is not read: it is deprecated in UDA 1.1 because
// so many devices filled it in wrongly, and a wrong base is worse than none
// (portmap.h, Known limits).
// ---------------------------------------------------------------------------
bool takeControl() {
    if (!g_uCtl[0]) return false;
    if (strncasecmp(g_uCtl, "http://", 7) != 0) {
        if (g_uCtl[0] != '/') {
            // A relative path with no leading slash. Rare and legal; one
            // slash in front of it is the resolution against "/".
            //
            // Refused rather than cut when the slash will not fit: a URL one
            // character short draws a 404, which lands on refused(0) and
            // reads as "your router said no" for an hour, where this gives
            // serviceUpnp's accurate line instead (the code review's second
            // pass).
            if (strlen(g_uCtl) + 2 > sizeof(g_uCtl)) return false;
            char tmp[sizeof(g_uCtl)];
            snprintf(tmp, sizeof(tmp), "/%.*s", static_cast<int>(sizeof(tmp) - 2), g_uCtl);
            memcpy(g_uCtl, tmp, strlen(tmp) + 1);
        }
        return true;
    }
    // Absolute: keep the path, and the port, but NOT a host that is not
    // the gateway, for the reason takeLocation gives. A device description
    // is a document from the network and an absolute URL in it is a
    // redirect like any other.
    const char* p = g_uCtl + 7;
    char host[48];
    size_t h = 0;
    while (*p && *p != ':' && *p != '/') {
        if (h + 1 >= sizeof(host)) return false;
        host[h++] = *p++;
    }
    host[h] = '\0';
    const in_addr_t a = inet_addr(host);
    if (a == INADDR_NONE) return false;
    if (a != static_cast<in_addr_t>(g_gw)) return false;
    uint16_t port = 80;
    if (*p == ':') {
        ++p;
        unsigned v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10u + static_cast<unsigned>(*p++ - '0'); if (v > 65535u) return false; }
        if (!v) return false;
        port = static_cast<uint16_t>(v);
    }
    char tmp[sizeof(g_uCtl)];
    snprintf(tmp, sizeof(tmp), "%s", *p ? p : "/");
    memcpy(g_uCtl, tmp, strlen(tmp) + 1);
    g_uHost = a;
    g_uHttp = port;
    return true;
}

// ---------------------------------------------------------------------------
// feedDesc: one byte of the device description.
//
// The control URL is captured into g_uCtl whenever one goes past, and the
// decision is made at </service>: if that element also carried a WAN
// connection service type, the URL in hand belongs to it. That is what lets
// one buffer serve both orderings (see the file comment).
//
// The one case this has to get right, and the reason the stand-in router's
// description puts a Layer3Forwarding service with a controlURL of its OWN
// in front of the WAN one: a scanner that kept the first controlURL it saw
// would POST every action at the wrong service and the router would answer
// perfectly sensible refusals to all of them.
// ---------------------------------------------------------------------------
void feedDesc(char c) {
    if (g_uCap != C_NONE) {
        if (c == '<') {
            if (g_uCap == C_CTL) { g_uCtl[g_uCapLen] = '\0'; g_uCtlHit = g_uCapLen > 0; }
            g_uCap = C_NONE;
            // The '<' that ended the text still has to be offered to the
            // matchers, or "</service>" right after a controlURL is missed.
        } else if (g_uCap == C_CTL) {
            // Printable and not a space. The capture is a document from the
            // network and it goes straight into "POST %s HTTP/1.1\r\n", so
            // one CR LF inside it would let the description's author add a
            // second request of their own choosing, sent from the board's
            // own trusted address (the code review, HIGH). A URL has no
            // business holding any of these, so a byte outside the range
            // voids the whole capture rather than cutting it short.
            if (c < 0x21 || c > 0x7E) {
                g_uCap    = C_NONE;
                g_uCapLen = 0;
                g_uCtlHit = false;
                g_uCtlBad = true;
                g_uCtl[0] = '\0';
                return;
            }
            if (static_cast<size_t>(g_uCapLen) + 1 < sizeof(g_uCtl)) g_uCtl[g_uCapLen++] = c;
            return;
        } else if (g_uCap == C_VER) {
            // The one digit after "WANxxConnection:". Anything else leaves
            // the version at 1, which every IGD answers to.
            if (c >= '1' && c <= '9') g_uVer = static_cast<uint8_t>(c - '0');
            g_uCap = C_NONE;
            return;
        }
    }

    if (g_lSvcOpen.feed(c)) {
        g_uInSvc  = true;
        g_uSvcHit = false;
        g_uCtlHit = false;
        // Reset at both ends of a service element: g_lCtl is only fed while
        // g_uInSvc, so a position left part way through "<controlURL>" by
        // one element could otherwise be completed by text in the next
        // (the code review, LOW).
        g_lCtl.reset();
        return;
    }
    if (g_lSvcShut.feed(c)) {
        g_lCtl.reset();
        if (g_uSvcHit && g_uCtlHit) { g_uDone = true; return; }
        // Not the service wanted, so whatever URL was captured is somebody
        // else's and must not be left looking like an answer.
        g_uCtlHit  = false;
        g_uCtl[0]  = '\0';
        g_uInSvc   = false;
        g_uSvcHit  = false;
        return;
    }
    if (g_lWanIp.feed(c))  { g_uSvcHit = true; g_uSvc = S_IP;  g_uCap = C_VER; return; }
    if (g_lWanPpp.feed(c)) { g_uSvcHit = true; g_uSvc = S_PPP; g_uCap = C_VER; return; }
    // Only inside a <service>: g_uInSvc is what makes the "not followed"
    // line in portmap.h's Known limits true rather than hopeful, and it is
    // what keeps a device-level controlURL, if one ever appeared, from
    // being handed to the SOAP stage as a WAN connection's.
    if (g_uInSvc && g_lCtl.feed(c)) { g_uCap = C_CTL; g_uCapLen = 0; g_uCtlHit = false; return; }
}

// ---------------------------------------------------------------------------
// feedSoap: one byte of a SOAP reply. What decides the answer is the body
// and never the HTTP status, so that the two cannot disagree (file comment).
// ---------------------------------------------------------------------------
void feedSoap(char c) {
    if (g_uCap != C_NONE) {
        if (c == '<') {
            // The kind is read BEFORE the capture is cleared: testing
            // g_uCap after setting it to C_NONE is a condition that is
            // always true, and it would have called every '<' after an
            // address the end of the answer.
            const uint8_t was = g_uCap;
            g_uCap = C_NONE;
            if (was == C_IP) {
                g_uVal[g_uCapLen] = '\0';
                // Done even when the element was EMPTY, which is a router
                // whose own WAN link is down. extTook then reads it as "no
                // address of its own" and asks again in a minute, where
                // reading on to the far end's close instead would have
                // landed on "the router answered something this board did
                // not understand".
                if (g_uAct == A_EXT) g_uDone = true;
            }
            // A fault code IS the answer, so the exchange ends here rather
            // than at the close. Without this a reply whose connection
            // stayed open past the budget turned a 725 ("ask for a
            // permanent mapping") into "gave up", and the retry that code
            // exists for never happened.
            if (was == C_ERR && g_uCode) g_uDone = true;
            // The '<' that ended the text still goes to the matchers below.
        } else if (g_uCap == C_ERR) {
            if (c >= '0' && c <= '9' && g_uCode < 6553u)
                g_uCode = static_cast<uint16_t>(g_uCode * 10u + static_cast<uint16_t>(c - '0'));
            return;
        } else if (g_uCap == C_IP) {
            if (static_cast<size_t>(g_uCapLen) + 1 < sizeof(g_uVal)) g_uVal[g_uCapLen++] = c;
            return;
        }
    }
    if (g_lErr.feed(c))   { g_uCap = C_ERR; g_uCode = 0; return; }
    if (g_lExtIp.feed(c)) { g_uCap = C_IP;  g_uCapLen = 0; g_uVal[0] = '\0'; return; }
    if (g_uAct == A_ADD && g_lAddOk.feed(c)) { g_uDone = true; return; }
    if (g_uAct == A_DEL && g_lDelOk.feed(c)) { g_uDone = true; return; }
}

// ---------------------------------------------------------------------------
// buildReq: the HTTP request for the stage this exchange is in, into caller
// storage. Built fresh on every pass and sent from g_uSent, so a partial
// send needs no buffer that outlives the pass: the same reason sendAsk
// builds its datagram on the stack.
//
// The body goes in at kHdrRoom so its length is known before the headers
// that must declare it are written, and is then moved back to meet them.
// Returns 0 when it would not fit, which is a refusal rather than a cut
// request: a SOAP body short of its closing tag is a fault the router would
// answer with 402 and a sysop would read as "your router said no".
//
// 448 and not the 352 this shipped with for an afternoon, which was FOUR
// bytes short of the longest header the capture allows. Counted literal by
// literal: the POST line is 16 plus a control path of up to 95, HOST 29,
// CONTENT-TYPE 42, CONTENT-LENGTH 21, SOAPACTION 17 plus a 47-character
// service type plus 20, USER-AGENT 48 with this version string, CONNECTION
// 19 and the blank line 2, which is 356. Past it buildReq returned 0 and
// the sysop was told "your router does not do this" about a router that
// does, with no console line naming the real cause (the code review).
constexpr size_t kHdrRoom = 448;

size_t buildReq(char* buf, size_t cap) {
    char host[16];
    addrText(g_uHost, host, sizeof(host));

    if (g_uStage == U_DESC) {
        const int n = snprintf(buf, cap,
            "GET %s HTTP/1.1\r\n"
            "HOST: %s:%u\r\n"
            "USER-AGENT: unleashed/" BBS_VERSION " UPnP/1.0\r\n"
            "CONNECTION: close\r\n"
            "\r\n", g_uPath, host, static_cast<unsigned>(g_uHttp));
        return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
    }

    char urn[80];
    svcUrn(urn, sizeof(urn));
    char* body = buf + kHdrRoom;
    const size_t broom = cap - kHdrRoom;
    int b = 0;
    if (g_uAct == A_EXT) {
        b = snprintf(body, broom,
            "<?xml version=\"1.0\"?>"
            "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
            " s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
            "<u:GetExternalIPAddress xmlns:u=\"%s\"></u:GetExternalIPAddress>"
            "</s:Body></s:Envelope>", urn);
    } else if (g_uAct == A_DEL) {
        b = snprintf(body, broom,
            "<?xml version=\"1.0\"?>"
            "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
            " s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
            "<u:DeletePortMapping xmlns:u=\"%s\">"
            "<NewRemoteHost></NewRemoteHost>"
            "<NewExternalPort>%u</NewExternalPort>"
            "<NewProtocol>TCP</NewProtocol>"
            "</u:DeletePortMapping></s:Body></s:Envelope>",
            urn, static_cast<unsigned>(g_uExt));
    } else {
        char me[16];
        addrText(g_self, me, sizeof(me));
        b = snprintf(body, broom,
            "<?xml version=\"1.0\"?>"
            "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
            " s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
            "<u:AddPortMapping xmlns:u=\"%s\">"
            "<NewRemoteHost></NewRemoteHost>"
            "<NewExternalPort>%u</NewExternalPort>"
            "<NewProtocol>TCP</NewProtocol>"
            "<NewInternalPort>%u</NewInternalPort>"
            "<NewInternalClient>%s</NewInternalClient>"
            "<NewEnabled>1</NewEnabled>"
            "<NewPortMappingDescription>%s</NewPortMappingDescription>"
            "<NewLeaseDuration>%lu</NewLeaseDuration>"
            "</u:AddPortMapping></s:Body></s:Envelope>",
            urn, static_cast<unsigned>(g_uExt),
            static_cast<unsigned>(g_slot[g_at].internal), me, kMapDesc,
            static_cast<unsigned long>(g_uPerm ? 0u : kLifeSecs));
    }
    if (b <= 0 || static_cast<size_t>(b) >= broom) {
        plat::log("portmap: the UPnP request for port %u does not fit; not sent",
                  static_cast<unsigned>(g_slot[g_at].internal));
        return 0;
    }

    const char* action = g_uAct == A_EXT ? "GetExternalIPAddress"
                       : g_uAct == A_DEL ? "DeletePortMapping"
                                         : "AddPortMapping";
    const int h = snprintf(buf, kHdrRoom,
        "POST %s HTTP/1.1\r\n"
        "HOST: %s:%u\r\n"
        "CONTENT-TYPE: text/xml; charset=\"utf-8\"\r\n"
        "CONTENT-LENGTH: %d\r\n"
        "SOAPACTION: \"%s#%s\"\r\n"
        "USER-AGENT: unleashed/" BBS_VERSION " UPnP/1.0\r\n"
        "CONNECTION: close\r\n"
        "\r\n", g_uCtl, host, static_cast<unsigned>(g_uHttp), b, urn, action);
    if (h <= 0 || static_cast<size_t>(h) >= kHdrRoom) {
        // Named, because the only other sign of it is the board reporting
        // a working router as absent.
        plat::log("portmap: the router's UPnP control address is too long for a "
                  "request (%d bytes of headers)", h);
        return 0;
    }
    memmove(buf + h, body, static_cast<size_t>(b));
    return static_cast<size_t>(h) + static_cast<size_t>(b);
}

// ---------------------------------------------------------------------------
// sendUpnpReq: whatever is left of the request. False while there is more
// to go, true once the last byte is away.
// ---------------------------------------------------------------------------
bool sendUpnpReq(bool& bad) {
    // 1,088, which is kHdrRoom (448) plus 640 for the body. Measured
    // rather than guessed: the longest header is 355, so 93 spare, and the
    // longest body is an AddPortMapping with a 47-character service type,
    // 65535 for both ports, a 15-character address and the mapping's
    // description, which is 600, so 39 spare after its terminator. This is
    // the largest single buffer on this path, and it sits on the BBS task,
    // whose low-water mark has been as little as 1,440 bytes in this
    // project's history, so it is sized to what the arithmetic needs and
    // not rounded up. **The frame is not the figure**: serviceUpnp's own
    // char buf[768] and char buf[kUpnpChunk] are above it in the same call
    // chain, so the worst case for Bbs::tick -> portmap::tick -> service ->
    // serviceUpnp -> sendUpnpReq -> buildReq is nearer 2.2 KB. bbs.cpp's
    // stackWatch("tail", ...) covers it, so the board reports it; this
    // comment should not read as if 1,088 were the whole of it. If kMapDesc or BBS_VERSION ever grows past those 39 bytes,
    // buildReq returns 0 and says so on the console rather than cutting a
    // request, which is why 39 is a budget and not a hazard.
    char req[1088];
    static_assert(sizeof(req) - kHdrRoom >= 640, "no room for a SOAP body");
    const size_t len = buildReq(req, sizeof(req));
    if (!len) { bad = true; return false; }
    while (g_uSent < len) {
        const ssize_t n = send(g_fd, req + g_uSent, len - g_uSent, 0);
        if (n > 0) { g_uSent = static_cast<uint16_t>(g_uSent + n); continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return false;
        bad = true;
        return false;
    }
    return true;
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
// Declared here because begin's UPnP arm has to be able to abandon an
// exchange it could not even open, and giveUp is where that is said. The
// two call each other, bounded: giveUp only ever calls begin for the NEXT
// protocol in the chain, and the last of the three (UPnP) matches neither
// chain condition, so the nesting cannot go past two.
void giveUp(uint32_t now);

bool begin(uint8_t i, uint8_t ask, bool addrAsk, bool del, uint32_t now) {
    closeFd();
    g_at      = i;
    g_ask     = ask;
    g_addrAsk = addrAsk;
    g_delete  = del;
    g_tries   = 0;
    g_force   = false;                   // PORTMAP NOW is spent on this ask
    g_began   = now;

    if (ask == kAskUpnp) {
        // UPnP owns its own sockets, because the stage decides whether it
        // wants UDP or TCP, and the action decides which of the three it
        // is asking. The counters and the wait are the same for all three
        // protocols, so they stay here.
        g_uAct = del ? A_DEL : addrAsk ? A_EXT : A_ADD;
        // Which outside port this exchange is about. A refresh keeps the
        // number callers have been given; a first ask takes the internal
        // one, moved up by however many 718 conflicts have been met.
        Slot& s = g_slot[i];
        g_uExt = s.external ? s.external
                            : static_cast<uint16_t>(s.internal + s.clash);
        ++g_asked;
        if (!upnpBegin(now)) {
            closeFd();
            // Which of the two it was matters: a socket() that failed is
            // the board's own budget and the discovery is still good,
            // where a connect() that failed outright is a control service
            // that has gone and must be looked for again. Reporting both
            // as NoSocket had a board retrying a dead control URL every
            // minute while blaming itself (the code review, LOW).
            if (g_noSock) {
                g_why = Why::NoSocket;
                waitFor(now, kSoonMs);
                plat::log("portmap: no socket free to ask the router; "
                          "trying again in a minute");
            } else {
                giveUp(now);
            }
            return false;
        }
        return true;
    }

    g_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_fd < 0) {
        g_why = Why::NoSocket;
        waitFor(now, kSoonMs);
        plat::log("portmap: no socket free to ask the router; trying again in a minute");
        return false;
    }
    const int fl = fcntl(g_fd, F_GETFL, 0);
    fcntl(g_fd, F_SETFL, fl | O_NONBLOCK);
    ++g_asked;
    sendAsk(now);
    return true;
}

// giveUp: this exchange got nowhere. Either fall through to the other
// protocol, or conclude that the gateway does not do this at all.
bool noSockSoon(uint32_t now) {
    if (!g_noSock) return false;
    g_why = Why::NoSocket;
    waitFor(now, kSoonMs);
    closeFd();
    plat::log("portmap: no socket free to go on asking the router; "
              "trying again in a minute");
    return true;
}

void giveUp(uint32_t now) {
    closeFd();
    ++g_failed;
    const bool del = g_delete;
    // Read BEFORE the reset below, because the reset is what used to make
    // the console line print once an HOUR instead of once: Proto::None is
    // the latch that silences it, and setting Unknown at the top of every
    // round undid the latch every round (the code review's second pass).
    // It also stopped the line being true: a buildReq that refused a
    // too-long control URL reaches here on a router that answered UPnP
    // perfectly well.
    const bool wasUnknown = g_proto == Proto::Unknown;
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
    // A UPnP exchange that got nowhere throws its discovery away, so the
    // next round searches again: the three failures it covers are a search
    // nobody answered (nothing to forget), a description that would not
    // fetch, and a control URL that has stopped working, and only the last
    // is ambiguous. A REFUSAL does not come through here (refused does), so
    // a 718 or a 725 never costs a re-discovery.
    if (g_ask == kAskUpnp) upnpForget();
    // Nothing is held, so there is nothing to lose by asking the whole
    // question again from the top, and the recovery this file documents
    // depends on it: a board settled on NAT-PMP whose router is swapped for
    // a UPnP-only one kept asking NAT-PMP for ever, said "your router does
    // not do this", and could only be recovered by switching the setting
    // off and on, which nothing told the sysop (the code review, MEDIUM).
    // It is the same shape as the bug the comment below describes, one case
    // further out. A HELD mapping whose renewal missed one datagram does
    // not come through here, so the common case is untouched.
    if (!anyHeld()) g_proto = Proto::Unknown;
    const bool settled = g_proto == Proto::Pcp || g_proto == Proto::Pmp ||
                         g_proto == Proto::Upnp;
    if (!settled && g_ask == kAskPcp) {
        begin(g_at, kAskPmp, false, del, now);
        return;
    }
    // And then UPnP, which is the one most routers have. The two before it
    // are a datagram each, so reaching this costs about 1.75 seconds and no
    // caller notices either way.
    if (!settled && g_ask == kAskPmp) {
        begin(g_at, kAskUpnp, false, del, now);
        return;
    }
    if (g_proto == Proto::Unknown) {
        g_proto = Proto::None;
        g_why   = Why::NoAnswer;
        if (wasUnknown)
            plat::log("portmap: the router answered no PCP or NAT-PMP on port %u and no "
                      "UPnP on port %u; nothing is forwarded",
                      static_cast<unsigned>(gatewayPort()), static_cast<unsigned>(ssdpPort()));
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
bool granted(uint16_t ext, uint32_t life, uint32_t now, bool perm = false) {
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
    // A UPnP router that would only make a permanent mapping. lifeSecs is
    // still kLifeSecs so the half-life re-assert below works unchanged,
    // which is also the only thing that notices a UPnP router's reboot, but
    // the flag stops lapse() calling it expired and stops every surface
    // printing a lease that is not one.
    if (perm) s.flags |= F_PERM;
    else      s.flags &= static_cast<uint8_t>(~F_PERM);
    // Whatever number was settled on is the one to refresh, so the 718
    // walk starts from the board's own number again next time it is needed.
    s.clash = 0;
    // The refusal throttle forgets on a success, or a refusal that comes
    // back after a spell of working would be silent for ever (third review).
    g_loggedWhy  = Why::Ok;
    g_loggedCode = 0xFFFF;
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
    char life_[24];
    if (perm) snprintf(life_, sizeof(life_), "permanently");
    else      snprintf(life_, sizeof(life_), "for %lu s", static_cast<unsigned long>(life));
    plat::log("portmap: %s mapped port %u to %s:%u %s%s",
              g_proto == Proto::Pcp  ? "PCP"
            : g_proto == Proto::Upnp ? "UPnP" : "NAT-PMP",
              static_cast<unsigned>(s.internal), addr, static_cast<unsigned>(ext), life_,
              g_why == Why::Carrier ? "; that address is NOT on the internet" : "");
    return true;
}

// resultWhy: a protocol's own refusal in this module's words. The two
// codings differ, so each is read against its own RFC.
Why resultWhy(uint16_t code) {
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
    if (g_ask == kAskUpnp) {                    // the IGD service template
        switch (code) {
            // ConflictWithOtherMechanisms: the router has a static forward,
            // or another protocol's mapping, on this number. It can clear
            // by itself, so it is asked again in a minute rather than in an
            // hour.
            case 729: return Why::GatewayBusy;
            // Everything else from an IGD is a flat no: 402 invalid args,
            // 501 action failed, 606 not authorised, 715 and 716 wildcards,
            // 718 after the alternatives ran out, 724 same ports required,
            // 726 and 727 wildcard-only. None of them clears by waiting.
            default:  return Why::Refused;
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
void refused(uint16_t code, uint32_t now, const char* which) {
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
// extTook: the outside address an answer carried, judged. Shared by
// NAT-PMP's public-address reply and UPnP's GetExternalIPAddress, because
// the judgement is the valuable half of this feature and two copies of it
// would be two chances to differ.
//
// A zero address is a router with no WAN lease of its own, which is a
// mapping onto nothing: said, and asked again in a minute rather than
// treated as settled, because a router still waiting on its own lease will
// have one shortly. g_extAsked is deliberately left false there.
// ---------------------------------------------------------------------------
void extTook(uint32_t addr, uint32_t now) {
    g_ext = addr;
    if (!g_ext) {
        g_why = Why::GatewayBusy;
        waitFor(now, kSoonMs);
        return;
    }
    g_extAsked = true;
    if (anyHeld()) {
        g_why = privateAddr(g_ext) ? Why::Carrier : Why::Ok;
        // On NAT-PMP and on UPnP the mapping is granted BEFORE the address
        // is known, so granted()'s own line cannot carry the verdict; the
        // console is the screen a sysop reads to diagnose this.
        if (g_why == Why::Carrier) {
            char a[16];
            addrText(g_ext, a, sizeof(a));
            plat::log("portmap: the router's outside address is %s, which is NOT on "
                      "the internet: callers cannot reach it", a);
        }
    }
    waitFor(now, 0);
}

// ---------------------------------------------------------------------------
// upnpAnswer: a SOAP reply has been read to the end, or the router closed
// the connection. g_uCode is its fault code, 0 for none, and g_uDone says
// whether the answer this exchange asked for was actually in it.
//
// Three codes mean something other than "no", and each is answered by
// asking again rather than by reporting a failure:
//   714  a delete of a mapping that is not there: the delete is done
//   725  this firmware only makes permanent mappings: ask for one
//   718  this outside number belongs to another host: try the next
// ---------------------------------------------------------------------------
void upnpAnswer(uint32_t now) {
    closeFd();
    Slot& s = g_slot[g_at];
    g_proto = Proto::Upnp;
    // When the router last ANSWERED, as the other two protocols record it.
    g_asOf  = clk::epoch();

    if (g_uCode) {
        if (g_uAct == A_DEL && g_uCode == 714) {
            s.flags &= static_cast<uint8_t>(~(F_HELD | F_PERM));
            s.external = 0;
            waitFor(now, 0);
            return;
        }
        if (g_uAct == A_ADD && g_uCode == 725 && !g_uPerm) {
            g_uPerm = true;
            plat::log("portmap: UPnP will only make a mapping that never expires; "
                      "asking for one");
            waitFor(now, 0);
            return;
        }
        if (g_uAct == A_ADD && g_uCode == 718) {
            if (s.external) {
                // The number callers have been given now belongs to another
                // host on the LAN. Nothing is held, whatever the board
                // believed, and the walk starts at its own number again.
                s.flags &= static_cast<uint8_t>(~(F_HELD | F_PERM));
                s.external = 0;
                s.clash    = 0;
                ++g_failed;
                waitFor(now, 0);
                return;
            }
            if (s.clash + 1 < kClashTries) {
                ++s.clash;
                ++g_failed;
                plat::log("portmap: UPnP says outside port %u is taken; trying %u",
                          static_cast<unsigned>(g_uExt),
                          static_cast<unsigned>(s.internal + s.clash));
                waitFor(now, 0);
                return;
            }
            // Out of numbers to try. Reset the walk so the hourly round
            // starts at the board's own number, in case the conflict has
            // gone by then; without that a board sat on internal + 2 for
            // the rest of its uptime.
            s.clash = 0;
        }
        refused(g_uCode, now, "UPnP");
        return;
    }

    if (!g_uDone) {
        // The router answered and what came back was not the answer asked
        // for: a 404, an authentication challenge, a body cut short. A
        // refusal with no code of its own, said as one.
        //
        // And the discovery goes, which giveUp's own comment claimed to
        // cover and did not: a control service MOVED by a firmware update
        // lands here and not there, so the board POSTed at the old path
        // every hour for the rest of its uptime (the code review, MEDIUM).
        // It costs one search and one GET an hour, because Why::Refused
        // already waits kRetryMs.
        upnpForget();
        refused(0, now, "UPnP");
        return;
    }

    if (g_uAct == A_EXT) {
        const in_addr_t a = inet_addr(g_uVal);
        extTook(a == INADDR_NONE ? 0u : static_cast<uint32_t>(a), now);
        return;
    }
    if (g_uAct == A_DEL) {
        s.flags &= static_cast<uint8_t>(~(F_HELD | F_PERM));
        s.external = 0;
        waitFor(now, 0);
        return;
    }
    // AddPortMapping answers no values at all, so the outside port of a
    // granted mapping is the one that was asked for. IGD2's
    // AddAnyPortMapping does return one and is not used (portmap.h).
    if (granted(g_uExt, kLifeSecs, now, g_uPerm)) waitFor(now, 0);
    else                                          waitFor(now, kRetryMs);
}

// ---------------------------------------------------------------------------
// serviceUpnp: one step of a UPnP exchange. At most kUpnpReads x kUpnpChunk
// bytes are read and scanned in a pass, which keeps the pass inside the
// 100 us line however big the router's description is, and the whole
// exchange is abandoned after kUpnpMs exactly as a silent UDP one is.
// ---------------------------------------------------------------------------
void serviceUpnp(uint32_t now) {
    const bool late = plat::since(now, g_began) >= kUpnpMs;

    if (g_uStage == U_SEARCH) {
        char buf[768];
        // Bounded, unlike a search that read until the socket was empty:
        // SSDP is a LAN protocol and anything on the wire can put datagrams
        // in this socket, including with the gateway's address on them, so
        // an unbounded loop is a pass whose length somebody else chooses
        // (Rule no. 1). Eight is more answers than a search can honestly
        // produce from one router.
        for (uint8_t seen = 0; seen < 8; ++seen) {
            sockaddr_in from = {};
            socklen_t   flen = sizeof(from);
            const ssize_t n = recvfrom(g_fd, buf, sizeof(buf) - 1, 0,
                                       reinterpret_cast<sockaddr*>(&from), &flen);
            if (n <= 0) break;
            // Only the gateway's own answer is read, which is what makes a
            // search for upnp:rootdevice safe on a LAN full of televisions
            // (the file comment). It is also the guard the UDP protocols
            // use, for the same reason: a stranger must not be able to
            // hand the board a description to go and fetch.
            if (from.sin_addr.s_addr != g_gw) continue;
            buf[n] = '\0';
            if (!takeLocation(buf)) continue;
            g_uStage = U_DESC;
            if (!openTcp(now)) { if (!noSockSoon(now)) giveUp(now); return; }
            return;
        }
        if (late) { giveUp(now); return; }
        // Keep listening for the whole budget, and resend up to kTries
        // times on the way: a device may wait out MX before answering, so
        // giving up with the sends is giving up too early.
        const uint32_t gap = kFirstMs << (g_tries ? g_tries - 1 : 0);
        if (g_tries < kTries && plat::since(now, g_sentAt) >= gap) sendSsdp(now);
        return;
    }

    if (g_uPhase == P_CONN) {
        fd_set w;
        FD_ZERO(&w);
        FD_SET(g_fd, &w);
        timeval tv = {0, 0};
        if (select(g_fd + 1, nullptr, &w, nullptr, &tv) <= 0) {
            if (late) giveUp(now);
            return;
        }
        // A socket still connecting reports SO_ERROR 0, the same as one that
        // has finished, so writability is the test and this only means
        // anything after it (announce's own lesson, which cost a round
        // there: the other way works on loopback and fails on a LAN).
        int       err = 0;
        socklen_t el  = sizeof(err);
        if (getsockopt(g_fd, SOL_SOCKET, SO_ERROR, &err, &el) < 0 || err) {
            giveUp(now);
            return;
        }
        g_uPhase = P_SEND;
    }

    if (g_uPhase == P_SEND) {
        bool bad = false;
        if (!sendUpnpReq(bad)) {
            if (bad || late) giveUp(now);
            return;
        }
        g_uPhase = P_READ;
    }

    bool eof = false;
    char buf[kUpnpChunk];
    for (uint8_t k = 0; k < kUpnpReads && !g_uDone; ++k) {
        const ssize_t n = recv(g_fd, buf, sizeof(buf), 0);
        if (n > 0) {
            g_uRead += static_cast<uint32_t>(n);
            for (ssize_t j = 0; j < n && !g_uDone; ++j) {
                if (g_uStage == U_DESC) feedDesc(buf[j]);
                else                    feedSoap(buf[j]);
            }
            if (g_uRead >= kUpnpMaxXml) break;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        eof = true;                       // the router closed, or an error
        break;
    }

    if (g_uStage == U_DESC) {
        if (g_uDone) {
            if (!takeControl()) {
                plat::log("portmap: the router's UPnP description named a control address "
                          "this board cannot use");
                giveUp(now);
                return;
            }
            plat::log("portmap: UPnP found WAN%sConnection:%u at port %u",
                      g_uSvc == S_PPP ? "PPP" : "IP",
                      static_cast<unsigned>(g_uVer), static_cast<unsigned>(g_uHttp));
            g_uStage = U_SOAP;
            if (!openTcp(now)) { if (!noSockSoon(now)) giveUp(now); return; }
            return;
        }
        if (eof) {
            plat::log(g_uCtlBad
                ? "portmap: the router's UPnP description gave a control address this "
                  "board will not use"
                : "portmap: the router answered UPnP but its description has no "
                  "port-forwarding service");
            giveUp(now);
            return;
        }
        if (g_uRead >= kUpnpMaxXml) {
            plat::log("portmap: the router's UPnP description ran past %lu bytes; giving up",
                      static_cast<unsigned long>(kUpnpMaxXml));
            giveUp(now);
            return;
        }
        if (late) giveUp(now);
        return;
    }

    if (g_uDone || eof) { upnpAnswer(now); return; }
    if (g_uRead >= kUpnpMaxXml) { giveUp(now); return; }
    if (late) giveUp(now);
}

// ---------------------------------------------------------------------------
// service: one step of an exchange in flight. Reads whatever is there,
// retransmits when the gap has passed, gives up after kTries.
// ---------------------------------------------------------------------------
void service(uint32_t now) {
    if (g_ask == kAskUpnp) { serviceUpnp(now); return; }
    uint8_t pkt[64];
    // Bounded for the reason serviceUpnp's search loop is (the code
    // review's second pass caught that one of the two was left): this is an
    // unbound UDP socket, so anything that finds its ephemeral port inside
    // the 1.75 s exchange can spray it, and each datagram costs a recvfrom
    // on the loop. One reply is all either protocol sends.
    for (uint8_t seen = 0; seen < 8; ++seen) {
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
        g_code  = code;
        g_proto = Proto::Pmp;
        g_asOf  = clk::epoch();
        const uint32_t epoch = get32(pkt + 4);
        const bool     lost  = forgot(epoch, now);
        g_epoch   = epoch;
        g_epochAt = now;
        closeFd();
        if (code != 0) { refused(g_code, now, "NAT-PMP"); return; }
        if (addrReply) {
            // Judged by extTook, which UPnP's GetExternalIPAddress shares:
            // a zero address is a router with no lease of its own and is
            // asked again in a minute rather than settled, and the carrier
            // verdict is said on the console as well as on PORTMAP and
            // SYS. On NAT-PMP, as on UPnP, the mapping is granted BEFORE
            // the address is known, so granted()'s own line cannot carry
            // that verdict (found by the host test, not by a review).
            //
            // Safe only because a due mapping is picked before this
            // question (see tick): asking above due() would starve the
            // renewal for ever.
            uint32_t a = 0;
            memcpy(&a, pkt + 8, 4);
            extTook(a, now);
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
            // The UPnP discovery goes too, and so does the knowledge that
            // the router only makes permanent mappings: a sysop who has
            // just switched this on may have changed the router as well,
            // and that fact belongs to the old one. drop() does not clear
            // g_uPerm, deliberately, because there it IS the same router.
            //
            // Through the flag and not at once: an off-then-on inside one
            // second can leave a release exchange in flight, and this used
            // to rebuild that request from cleared state (see g_uRefind).
            g_uRefind  = true;
            g_relTried = 0;
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
            g_release  = kSlots;
            g_relTried = 0;
            plat::log("portmap: off; giving back what the router granted");
        } else {
            plat::log("portmap: off");
        }
    }

    if (g_fd >= 0) { service(now); return; }     // an exchange in flight comes first

    if (!on) {
        if (g_release && anyHeld() && g_proto != Proto::Unknown && g_proto != Proto::None) {
            for (uint8_t i = 0; i < kSlots; ++i) {
                if (!(g_slot[i].flags & F_HELD)) continue;
                if (g_relTried & (1u << i)) continue;
                g_relTried = static_cast<uint8_t>(g_relTried | (1u << i));
                --g_release;
                begin(i, askOf(g_proto), false, true, now);
                return;
            }
        }
        if (g_release || anyHeld()) { g_release = 0; g_relTried = 0; drop(); }
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
    // NAT-PMP and UPnP both answer the outside address in a question of
    // their own; PCP's map reply carries it, so there it is free.
    const bool wantAddr = (g_proto == Proto::Pmp || g_proto == Proto::Upnp) && !g_extAsked;
    // g_extAsked is set where the question is ANSWERED, not here: setting it
    // on the attempt meant one lost datagram, one refusal or one failed
    // socket() left the address never asked again for the life of the
    // mapping, which is the same outcome as not asking at all. Every failure
    // path has a waitFor, so retrying is not a storm. The address request is
    // slot-independent, so slot 0 asks.
    if (wantAddr && (i == 0xFF || g_mapTried)) {
        g_mapTried = false;
        begin(0, askOf(g_proto), true, false, now);
        return;
    }
    if (i != 0xFF) {
        g_mapTried = true;
        // **Nothing held means the question is asked from the top.** A
        // renewal goes to the protocol that granted the mapping, because
        // that is the one that will renew it; a round with nothing held
        // starts at PCP and walks all three, which costs two datagrams and
        // buys two things: a router swapped for one that speaks something
        // else is found in ONE round rather than over two, and giveUp's
        // "no PCP, no NAT-PMP, no UPnP" line is then true of the round it
        // is printed about rather than of a round that only tried one.
        begin(i, anyHeld() ? askOf(g_proto) : kAskPcp, false, false, now);
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
    // And on a UPnP router the description is read again, because the one
    // thing NOW is reached for is a router whose menu has just been changed
    // and whose control service may have come or gone with it. One search
    // and one GET, once, on a command a sysop typed. Armed rather than done
    // here: this runs on the shell's pass, which is above the in-flight
    // guard (see g_uRefind).
    if (g_proto == Proto::Upnp) g_uRefind = true;
    // Ask the whole question again whenever there is nothing to lose by it.
    // "None" alone was not enough: a board settled on one protocol and
    // holding nothing never tried the other two (the code review, MEDIUM).
    if (g_proto == Proto::None || !anyHeld()) g_proto = Proto::Unknown;
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
    // "Nothing recorded yet" is its own flag and not left == 0, because 0
    // is also a real remainder: a mapping whose lease has just run out and
    // which lapse() has not yet cleared would otherwise be overwritten by
    // the other slot's figure, and the row would print an hour left over a
    // mapping that has none (the code review, LOW).
    bool     have = false;
    uint32_t left = 0;
    for (uint8_t i = 0; i < kSlots; ++i) {
        const Slot& s = g_slot[i];
        if (!(s.flags & F_HELD)) continue;
        if (s.flags & F_PERM) { st.permanent = true; continue; }
        const uint32_t gone = plat::since(now, s.grantedAt) / 1000u;
        const uint32_t rem  = s.lifeSecs > gone ? s.lifeSecs - gone : 0;
        if (!have || rem < left) { left = rem; have = true; }
    }
    // A board holding one permanent mapping and one leased one has a lease
    // to show, so permanent is reported only when there is no lease left to
    // report: every surface then reads "permanent" rather than a figure
    // that is true of only half of what is held.
    st.leftSecs  = have ? left : 0;
    st.permanent = st.permanent && !have;
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
        // Three limbs, not two, and the third was learned from a real
        // router: a MikroTik with NAT-PMP switched ON and no interfaces
        // declared answers nothing, which looks from here exactly like a
        // router that has never had the feature. "Not set up" is the honest
        // middle ground between "off" and "does not have it", and all three
        // want different things of a sysop (whatToDo says which).
        case Why::NoAnswer:    return "no answer: off, not set up, or does not have it";
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
            return "Look for UPnP or NAT-PMP in your router's menu. Switched on is not "
                   "always enough: some routers also want to be told which of their "
                   "own connections is the outside one, and until they are they answer "
                   "nothing at all, which looks from here exactly like a router that "
                   "cannot do it. And some cannot, in which case the port has to be "
                   "forwarded by hand.";
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
        case Why::NoAnswer:    return "no answer";
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
    // `held AND external`, not `held`: granted() already makes a grant on a
    // router with no outside address of its own Why::GatewayBusy rather
    // than Why::Ok, and this row branched on `held` alone and never reached
    // the reason, so it read "-:6400 mapped, 1h59m left" over a router
    // whose WAN has no address at all (the code review's second pass). It
    // is a settled steady state and not a transient: a router still waiting
    // on its own lease is re-asked once a minute for ever. portmap.h calls
    // the address "worth more than the mapping", so the surface that cannot
    // show one must not be the one saying "mapped".
    //
    // Written as a condition on this branch rather than as an early return
    // above it, which is where it went first and which swallowed the
    // "asking the router..." line below: on a first probe nothing is held
    // and no address is known either, so an early return on !external would
    // have answered every in-flight exchange with its reason instead.
    if (st.held && st.external) {
        char addr[16];
        addrText(st.external, addr, sizeof(addr));
        // On a board where only the SSH mapping is held, the number shown is
        // the SSH one and says so, rather than reading as the telnet port.
        //
        // Known and left: the port shown is slot 0's and the lease is
        // whichever slot's is shorter, so a board holding a PERMANENT
        // telnet mapping and a leased SSH one prints the SSH lease beside
        // the telnet port. It needs a router that grants both kinds, which
        // takes g_uPerm being cleared between them, and it errs toward
        // under-claiming; PORTMAP shows the two mappings separately. The
        // honest fix is a Status field saying which slot the lease is for.
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
            // The lease, or the word for a mapping that has none: a UPnP
            // router that would only make a permanent one gave no figure,
            // and "0h00m left" over a working mapping is the
            // confidently-wrong class this file exists to avoid.
            // 32 and not 16: GCC bounds a %lu at ten digits whatever the
            // value can really be, and -Wformat-truncation is an error on
            // the target build, so the buffer is sized for the compiler's
            // arithmetic rather than for the two digits leftSecs can
            // actually produce.
            char lease[32];
            if (st.permanent) snprintf(lease, sizeof(lease), "permanent");
            else snprintf(lease, sizeof(lease), "%luh%02lum left",
                          static_cast<unsigned long>(st.leftSecs / 3600u),
                          static_cast<unsigned long>((st.leftSecs % 3600u) / 60u));
            const int len = snprintf(out, n, "%s:%u%s mapped, %s, as of %s",
                                     addr, shown, which, lease, when[0] ? when : "-");
            if (len < 0 || len > kWide)
                snprintf(out, n, "%s:%u%s mapped, %s", addr, shown, which, lease);
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
