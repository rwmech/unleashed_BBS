// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/portmap.h
// Module:       Core / the board asks the router to forward its port (1.2.2)
//
// Purpose:      A sysop should not have to open a router menu to let callers
//               in. Two small protocols let a host ask the router on its own
//               network to forward a port to it, and where either is
//               switched on the result is exactly a hand-made port forward:
//               direct, nobody in the middle, nothing to trust. That is why
//               this is the first rung of the reachability ladder
//               (internal/study-broker-sat-2026-10-05.md phase C) and the
//               only one that reaches the base ESP32, because it has
//               nothing to do with SSH.
//
//               PCP (RFC 6887) and NAT-PMP (RFC 6886), both UDP to port
//               5351 on the default gateway. PCP is the newer one and the
//               two share the port, so the board asks in PCP first: a
//               NAT-PMP-only gateway answers UNSUPP_VERSION with version 0
//               in its reply, which RFC 6887 section 9 says means "speak
//               NAT-PMP to me". A gateway that answers neither has port
//               mapping off, or has never had it, and the board says so.
//
//               UPnP IGD is NOT here, and that is a size decision rather
//               than a judgement: see the note at the foot of this comment.
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
//                   mapping.** Both protocols hand back the router's own
//                   outside address, and when that address is private
//                   (10/8, 172.16/12, 192.168/16, 100.64/10, 169.254/16)
//                   the board is behind a carrier NAT or a second router
//                   and the mapping cannot help, however well it worked.
//                   Nothing else on the reachability ladder can tell a
//                   sysop that for the cost of one UDP packet.
//
// Not mapped:   `backup_port`. The backup window is deliberately local
//               only, it carries the Wi-Fi password in a download, and a
//               hole punched to it from the internet would be the worst
//               single thing this firmware could ask a router for. The
//               telnet port always; the SSH port too, on a board that has
//               one bound.
//
// Rule no. 1:   nothing here runs long enough to be seen. There is no
//               blocking call in either protocol: the gateway's address is
//               a register read (plat::gatewayIp), and the exchange is a
//               non-blocking sendto and a non-blocking recvfrom driven from
//               Bbs::tick, which is how announce drives its HTTP POST. It
//               is deliberately NOT a runner job: the runner is serial, one
//               job at a time, so a job sitting on a 1.75 s UDP timeout
//               would hold a caller's FILES page, a forum write and a photo
//               prune behind it. A state machine on the loop costs a load
//               and a compare in the passes where there is nothing to do.
//
// Sockets:      one UDP socket, and only while an exchange is in flight.
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
// Leases:       a mapping is a lease, not a setting. RFC 6886 recommends
//               7200 s and both RFCs say to renew at about half of what was
//               granted, so the board asks for 7200 and renews at half the
//               granted lifetime, which is an hour on a router that grants
//               what was asked. A router that reboots forgets every
//               mapping, and both protocols carry a clock for exactly that:
//               an epoch that goes backwards against the board's own means
//               the gateway lost its state, and every mapping is asked for
//               again at once rather than at the next renewal.
//
// Why no UPnP:  priced, not skipped. UPnP IGD needs SSDP multicast
//               discovery, then an HTTP GET of a device description XML
//               whose size the router chooses (AVM's is a few KB, some are
//               over ten), then a SOAP POST to a control URL found inside
//               it. miniupnpc is "less than 50KB code size" (BSD-3-Clause,
//               so it would combine with GPLv3) and the ESP32 ports are
//               Arduino-flavoured. Our own would be about 330 lines, 6 to
//               10 KB of flash and 500 to 700 bytes of static DRAM, the
//               last because the parse spans ticks and so cannot be a stack
//               local. **Both fit**: the WROOM image is at 81% of its slot
//               and the ESP32-CAM, which is the DRAM floor, has a couple of
//               kilobytes free (the measured figure lives in CLAUDE.md's
//               1.2.2-portmap.2 entry and nowhere else, so there is one
//               copy of it to keep current). So the
//               reason to stop is NOT size, and saying it was would have
//               been a lie: it is three stacked protocols with real vendor
//               divergence, that is where the days go, and there is no
//               router here to shake it out against.
//
//               **Rob's decision, 2026-10-05: not now, for that reason and
//               not for flash.** NAT-PMP and PCP cover the Apple-lineage
//               gear and a good deal of consumer kit, and this is revisited
//               when there is a real router to shake it out against. The
//               shape here is ready for it: Proto gains a value and the
//               probe tries it third, after NAT-PMP. The figures are in
//               CLAUDE.md's 1.2.2-portmap.2 entry.
//
// Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build,
//               where BBS_HOST_GATEWAY points it at a fake router on
//               127.0.0.1 so the tests drive the real packets.
// See also:     RFC 6886 (NAT-PMP), RFC 6887 (PCP),
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
// None means it was asked and said nothing, which is the common answer.
enum class Proto : uint8_t { Unknown, Pcp, Pmp, None };

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
    uint32_t leftSecs = 0;       // seconds before the lease would run out
    uint32_t asOf     = 0;       // clk::epoch of the last answer, 0 never
    uint8_t  code     = 0;       // the protocol's own last result code
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
