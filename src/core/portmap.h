// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/portmap.h
// Module:       Core / the board asks the router to forward its port (1.2.2)
//
// Purpose:      A sysop should not have to open a router menu to let callers
//               in. Three small protocols let a host ask the router on its
//               own network to forward a port to it, and where any of them
//               is switched on the result is exactly a hand-made port
//               forward: direct, nobody in the middle, nothing to trust.
//               That is why this is the first rung of the reachability
//               ladder (internal/study-broker-sat-2026-10-05.md phase C)
//               and the only one that reaches the base ESP32, because it
//               has nothing to do with SSH.
//
//               PCP (RFC 6887) and NAT-PMP (RFC 6886), both UDP to port
//               5351 on the default gateway. PCP is the newer one and the
//               two share the port, so the board asks in PCP first: a
//               NAT-PMP-only gateway answers UNSUPP_VERSION with version 0
//               in its reply, which RFC 6887 section 9 says means "speak
//               NAT-PMP to me".
//
//               Then UPnP IGD, which is the one most routers actually have
//               and the reason the probe does not stop at two (1.2.2, after
//               the bench: see "Why UPnP" below). It is a different shape
//               from the other two, three stacked protocols rather than one
//               datagram: an SSDP search over UDP, an HTTP GET of a device
//               description, and SOAP POSTs to a control URL found inside
//               it. A gateway that answers none of the three has port
//               mapping off, or has never had it, and the board says so.
//
// Off:          `port_map = no` as shipped, CONFIG network. UPnP and its
//               relatives have a security reputation and some sysops will
//               have turned them off in the router on purpose; a board that
//               silently asked would be taking that decision for them.
//
// Honest:       Three rules, each of which this project has paid for once
//               before in some other feature:
//
//                 - the board says **mapped**, never **reachable**. A
//                   mapping that the router granted proves the router did
//                   as it was asked, not that a packet from outside
//                   arrives: that needs somebody outside to try, which
//                   nothing here can do (the study, section 3.1). "Checked
//                   from outside" waits for the broker.
//                 - a failure says which failure. A router with the feature
//                   off, a router that answers nothing, and a router that
//                   granted a mapping on an address the carrier owns are
//                   three different evenings, and the third is the common
//                   one.
//                 - **the external address is worth more than the
//                   mapping.** All three protocols hand back the router's
//                   own outside address, and when that address is private
//                   (10/8, 172.16/12, 192.168/16, 100.64/10, 169.254/16)
//                   the board is behind a carrier NAT or a second router
//                   and the mapping cannot help, however well it worked.
//                   Nothing else on the reachability ladder can tell a
//                   sysop that for the cost of one UDP packet.
//
//                   **And it is exactly what a silent router takes away.**
//                   The verdict needs an answer to read the address out of,
//                   so on a gateway that speaks none of the three the board
//                   reaches Why::NoAnswer and the carrier question is never
//                   asked at all: structurally unavailable, not merely
//                   unknown. That is the single strongest argument for
//                   covering a third protocol, and it is why this file
//                   keeps asking hourly rather than giving up for good.
//
// Not mapped:   `backup_port`. The backup window is deliberately local
//               only, it carries the Wi-Fi password in a download, and a
//               hole punched to it from the internet would be the worst
//               single thing this firmware could ask a router for. The
//               telnet port always; the SSH port too, on a board that has
//               one bound.
//
// Rule no. 1:   nothing here runs long enough to be seen. There is no
//               blocking call in any of the three: the gateway's address is
//               a register read (plat::gatewayIp), and an exchange is a
//               non-blocking sendto and recvfrom, or for UPnP a
//               non-blocking connect, send and recv, driven from Bbs::tick,
//               which is how announce drives its HTTP POST. It is
//               deliberately NOT a runner job: the runner is serial, one
//               job at a time, so a job sitting on a 1.75 s UDP timeout
//               would hold a caller's FILES page, a forum write and a photo
//               prune behind it. A state machine on the loop costs a load
//               and a compare in the passes where there is nothing to do.
//
//               The same rule is what decided UPnP's XML: the description
//               is never held. It is scanned byte by byte as it arrives
//               (the Lit matchers in the .cpp), so a router whose
//               description is twenty kilobytes costs the same couple of
//               hundred bytes as one whose description is two, and nothing
//               asks the heap for a block the ESP32-CAM may not have.
//
// Sockets:      one socket, and only while an exchange is in flight: UDP
//               for PCP, NAT-PMP and UPnP's SSDP search, TCP for UPnP's
//               HTTP and SOAP. Never two at once, so the budget below is
//               the same figure it was with two protocols.
//               The board already oversubscribes CONFIG_LWIP_MAX_SOCKETS
//               (16, and IDF 5.3.1 caps it there on every chip): two
//               listeners, ten caller nodes, the sysop node, the busy line,
//               two for the backup window and one for announce is 17, which
//               Bbs::busyFits manages. So this never holds one: it is
//               opened for a request, closed when the answer comes or the
//               tries run out, and a socket() that fails is one more thing
//               to say and try again later. Once a mapping is held the
//               socket exists for about 2 s an hour; in the worst retry
//               loop, a router that answers and refuses something which
//               could clear by itself, it is 1.75 s a minute, and that is
//               the figure to have in mind rather than the happy one.
//               `BBS_SOCK_RESERVE` deliberately does not count it and says
//               there why.
//
//               **The duty cycle above is the two UDP protocols' and UPnP
//               raises it**, which is worth writing down beside the figure
//               rather than leaving the old one to be quoted. A full probe
//               is PCP 1.75 s, NAT-PMP 1.75 s and UPnP up to `kUpnpMs`
//               (8 s), so about 11.5 s; a router that answers something
//               which could clear by itself repeats that every minute,
//               which is nearer 13% than the 3% of a leased mapping, and a
//               718 walk is up to three UPnP exchanges back to back. In
//               practice a LAN exchange is tens of milliseconds and the
//               socket is gone again, but the worst case is the figure the
//               trade was decided on, so it is the figure recorded.
//
// Leases:       a mapping is a lease, not a setting. RFC 6886 recommends
//               7200 s and both RFCs say to renew at about half of what was
//               granted, so the board asks for 7200 and renews at half the
//               granted lifetime, which is an hour on a router that grants
//               what was asked. A router that reboots forgets every
//               mapping, and both UDP protocols carry a clock for exactly
//               that: an epoch that goes backwards against the board's own
//               means the gateway lost its state, and every mapping is
//               asked for again at once rather than at the next renewal.
//
//               **UPnP has neither of those for free.** A lease duration is
//               optional there and a good many routers accept only 0, which
//               the IGD spec defines as "never expires" and which answers a
//               question nobody asked: a hole that outlives the board. So
//               the board asks for 7200, falls back to a permanent mapping
//               when the router answers 725 OnlyPermanentLeasesSupported,
//               says PERMANENT on every surface rather than printing a
//               lease that is not one, and re-asserts it hourly anyway,
//               which is also the only way a UPnP router's reboot is ever
//               noticed (the equivalent clock is GetStatusInfo's NewUptime,
//               a fourth SOAP call that would buy an hour of latency and
//               nothing else).
//
//               **And a permanent mapping outlives the board, which the
//               board cannot fix and so says instead.** Switching the
//               setting off gives it back, and so does the mapping being
//               re-asserted onto a new address; but a board simply
//               unplugged leaves the router forwarding to an address DHCP
//               may later hand to somebody else's laptop. The mitigations
//               are that a lease is always ASKED for first, so this only
//               happens on a router that refuses leases, where a hand-made
//               forward would be permanent too; and that COMMANDS.md and
//               PORTMAP's own Lease row say so. Deleting on the way down
//               does not help: SHUTDOWN leaves the board running, and
//               nothing runs when the power goes.
//
// Why UPnP:     because it is the one of the three that consumer routers
//               tend to have switched ON. 1.2.2-portmap.2 shipped PCP and
//               NAT-PMP; the first real router the feature ever met
//               answered neither, sixteen asks over a session, and that is
//               what put a third protocol on the list.
//
//               **But not for the reason that reading suggested, and the
//               correction is the more useful half.** The box was a
//               MikroTik, and RouterOS does implement NAT-PMP (7.13 and
//               later) as well as UPnP IGD. It had NAT-PMP switched on and
//               no interfaces declared, which is how RouterOS is told
//               which side is outside, and with none it answers nothing at
//               all. So the bench was a router configured on no interface,
//               not a router missing a protocol, and nothing here should
//               say a vendor lacks a protocol on the strength of one
//               unconfigured box.
//
//               **Confirmed from the other side the same day**: with the
//               interfaces declared, that router granted a NAT-PMP mapping
//               on the first ask, and PCP stayed silent in the same
//               exchanges. So on RouterOS it is NAT-PMP and UPnP IGD and
//               not PCP, and the no-interfaces trap is a real failure mode
//               and not a guess, which is why Why::NoAnswer's words have
//               three limbs and whatToDo's sentence has a paragraph about
//               it.
//
//               The case for UPnP survives it intact: a router that wants
//               an interface declared before it will answer belongs to a
//               sysop who could have forwarded the port by hand, and this
//               feature is for the person who cannot or will not. UPnP is
//               what tends to be on out of the box, which is also why
//               whyText and whatToDo named it first long before it was
//               spoken here.
//
//               **And it is a failure mode this board cannot tell from
//               absence**, which is why Why::NoAnswer's words and
//               whatToDo's sentence both carry it: "switched on in the
//               menu" and "answering on your network" are two different
//               things, and the board only ever sees the second.
//
// Our own:      and not miniupnpc, which was the obvious candidate and was
//               read before it was turned down. Its licence fits
//               (BSD-3-Clause combines with GPLv3) and its size fits ("less
//               than 50KB code size"), so neither of those decided it.
//               What decided it is that its whole API is synchronous and
//               its memory shape is wrong for this board:
//
//                 - upnpDiscover(delay, ...) waits out the delay for SSDP
//                   replies, miniwget's receivedata() is a 5,000 ms
//                   blocking read, and UPNP_AddPortMapping is a blocking
//                   round trip. On the loop that is Rule no. 1 broken three
//                   ways, and the runner is ruled out above, so adopting it
//                   means a task of its own: a new stack off the heap and
//                   new cross-task state for a feature that currently has
//                   neither.
//                 - getHTTPResponse mallocs 2,048 bytes and reallocs to
//                   Content-Length, so the WHOLE description sits in one
//                   heap block. On the ESP32-CAM, where the largest free
//                   block during a snap has been measured in the twenties
//                   of kilobytes, a ten to twenty kilobyte block is the
//                   "refused for memory" class this project has already
//                   hit three times. The streaming scan here holds none of
//                   it.
//
//               Patching both out of it leaves a vendored 50 KB tree whose
//               control flow we no longer use, which is worse than the
//               ~400 lines below. The two trees that ARE vendored here,
//               wolfSSH and esp32-camera, are both things we could not
//               write; SSDP, one HTTP GET and three SOAP actions are not.
//
// Known limits: written down because each is a router that will not work
//               and the reason will not be obvious:
//
//                 - the description's <URLBase> is ignored and the control
//                   URL is resolved against LOCATION's own host and port,
//                   or taken whole when it is absolute. URLBase is
//                   deprecated in UPnP Device Architecture 1.1 precisely
//                   because so many devices filled it in wrongly, and
//                   trusting it costs a buffer to be misled by.
//                 - a LOCATION whose host is a name rather than an address
//                   is refused, because resolving it would be DNS on the
//                   loop.
//                 - the control URL is taken from the <service> element
//                   that carries the WAN*Connection service type, in
//                   either order within that element, but a device that
//                   puts its controlURL outside the <service> it belongs
//                   to is not followed.
//                 - no event subscription, no GetStatusInfo, no IGD2
//                   AddAnyPortMapping: three actions and nothing else.
//                 - a LOCATION, or an absolute control URL, whose host is
//                   not the default gateway is refused. That is a security
//                   bound and not a convenience: an SSDP reply is a
//                   REDIRECT, UDP source addresses spoof, and without it
//                   one datagram from anything on the LAN sends the board
//                   to fetch a stranger's description, take their control
//                   URL, believe their outside address and publish their
//                   port to the directory as the one callers should dial.
//                   The cost is a router that serves its description from
//                   a second LAN address of its own, which is rare enough
//                   to be worth the bound.
//                 - a control URL carrying anything but printable
//                   non-space bytes is refused whole, because it goes into
//                   an HTTP request line and one CR LF inside it is a
//                   second request of the description author's choosing.
//                 - `<service>` and `</service>` are matched EXACTLY, and
//                   since the control URL is only read inside them that is
//                   load-bearing rather than incidental: a description
//                   writing `<service >`, which is legal XML, yields no
//                   control URL at all. Accepted, because the alternative
//                   is a tag parser.
//
// Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build,
//               where BBS_HOST_GATEWAY points it at a fake router on
//               127.0.0.1 so the tests drive the real packets.
// See also:     RFC 6886 (NAT-PMP), RFC 6887 (PCP), UPnP Device
//               Architecture 1.1 section 1.3 (SSDP search) and the IGD
//               WANIPConnection:1 service template (AddPortMapping,
//               DeletePortMapping, GetExternalIPAddress),
//               internal/study-broker-sat-2026-10-05.md section 14.2,
//               src/core/bbs_sysop.cpp (CONFIG network), COMMANDS.md
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
// ===========================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace portmap {

// Which protocol the gateway speaks. Unknown until it has been asked;
// None means it was asked in all three and said nothing.
//
// Appended rather than inserted: Upnp goes after Pmp and before None, which
// is also the probe order, and nothing stores a Proto on disk or sends one
// over the link, so the numbering is free to grow at the end.
enum class Proto : uint8_t { Unknown, Pcp, Pmp, Upnp, None };

// Why the board is not holding a mapping, in one byte so the words live in
// flash rather than in a buffer (whyText).
enum class Why : uint8_t {
    Ok,          // nothing wrong: a mapping is held
    Off,         // port_map = no
    NoNetwork,   // no address yet, or no default gateway to ask
    NoAnswer,    // asked in both protocols, the gateway said nothing
    Refused,     // the gateway answered and said no (feature off, or quota)
    GatewayBusy, // the gateway has no address of its own, or no room
    Carrier,     // mapped, but the outside address is a private one
    Behind,      // PCP says there is another NAT between here and it
    NoSocket,    // the socket budget had nothing spare
    Lapsed,      // a mapping was held and the renewals stopped working
};

struct Status {
    Proto    proto   = Proto::Unknown;
    Why      why     = Why::Off;
    bool     on      = false;    // the setting
    bool     held    = false;    // at least one mapping is believed live
    bool     asking  = false;    // an exchange is in flight
    uint32_t external = 0;       // the router's outside IPv4, network order, 0 unknown
    bool     carrier  = false;   // external is a private address: callers cannot reach it
    uint16_t port     = 0;       // the telnet port's granted outside port, 0 none
    uint16_t sshPort  = 0;       // the SSH port's, 0 none or no SSH
    uint32_t leftSecs = 0;       // seconds before the lease would run out, 0 when permanent
    // A UPnP router that grants only permanent mappings (IGD error 725).
    // leftSecs is then 0 and means "never expires", not "expired", so every
    // surface that prints a lease has to ask this first: a row reading
    // "0h00m left" over a working mapping is the confidently-wrong class
    // this file exists to avoid. The board still re-asserts it hourly.
    bool     permanent = false;
    uint32_t asOf     = 0;       // clk::epoch of the last answer, 0 never
    // The protocol's own last result code. 16 bits because UPnP's are
    // three digits (718 ConflictInMappingEntry, 725 permanent leases
    // only) where NAT-PMP's and PCP's are one or two.
    uint16_t code     = 0;
    uint16_t asked    = 0;       // exchanges sent since boot
    uint16_t failed   = 0;       // and how many got nowhere
};

// tick: one step, from the tail of Bbs::tick. Loop only. A load and a
// compare when the setting is off or there is nothing due.
//
// The ports are the ones the board is LISTENING on, which is why they are
// arguments rather than read from the settings here: a port changed in
// CONFIG waits for a restart, and forwarding the new number before the
// board answers on it would send callers to a closed port. sshPort is 0 on
// a board with no SSH port of its own, and is ignored where SSH is not
// compiled in at all.
void tick(uint32_t now, uint16_t telnetPort, uint16_t sshPort);

// askNow: ask again at the next tick, whatever the schedule said. PORTMAP
// NOW, and a CONFIG network save, so a sysop who has just switched the
// feature on in the router menu does not wait an hour or reboot.
void askNow();

// status: what to tell a sysop. A copy, loop only.
Status status();

// whyText: Status::why in words, from flash. The long form for 80 columns
// and over, 52 characters at most because CONFIG's read-only box holds 55
// and scrolls a longer value to its tail; whyShort is the 40 column one.
const char* whyText(const Status& st);
const char* whyShort(const Status& st);

// whatToDo: the sentence a sysop can act on, nullptr when there is nothing
// to do. No length limit: PORTMAP wraps it to the caller's width, and it is
// the whole reason that command exists beside the one-line surfaces.
const char* whatToDo(const Status& st);

// line: one line for CONFIG network's read-only row and for SYS, built for
// the width (wide is 80 columns and over). Writes at most n bytes.
void line(char* out, size_t n, bool wide);

// addrText: an IPv4 address in network order as a.b.c.d, "-" for zero.
// PORTMAP prints the outside address with it.
void addrText(uint32_t netOrder, char* out, size_t n);

// externalPort: the outside port a mapping granted for this internal port,
// 0 when none is held. announce publishes it when the sysop left "Outside
// port" blank, because a router that already had 6400 taken grants another
// number and the directory must advertise the one callers can dial.
uint16_t externalPort(uint16_t internal);

}  // namespace portmap
