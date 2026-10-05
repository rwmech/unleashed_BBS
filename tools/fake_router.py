#!/usr/bin/env python3
# ===========================================================================
#  µnleashed BBS
#  Electronic freedom on a microcontroller.
# ===========================================================================
#
# File:         tools/fake_router.py
# Module:       Tools / a stand-in router for the port-mapping tests
#
# Purpose:      src/core/portmap.cpp speaks PCP (RFC 6887) and NAT-PMP
#               (RFC 6886) to the router on the board's own network. Nothing
#               in a test harness has a router, and the one thing a port
#               mapper must never be tested against is a client written
#               beside it, so this is a server written from the RFCs and
#               from nothing in the firmware: it shares no code, no
#               constants and no header with the board.
#
#               That is the same rule lrzsz settled for XMODEM: a tolerant
#               implementation written alongside agrees with whatever the
#               other end does, and only a strict one written separately
#               finds the bug.
#
# Use:          python tools/fake_router.py --port 15351 --proto pcp
#               then run the host board with
#               BBS_HOST_GATEWAY=127.0.0.1:15351
#
#               --proto is WHICH protocol the router speaks, and --fault is
#               WHAT goes wrong, because the two are independent and the
#               interesting cases are combinations. A single --mode had the
#               carrier fault reachable only over PCP, which is the one
#               protocol where the firmware reads the outside address for
#               free: the bug that matters lives in "NAT-PMP plus carrier"
#               and a single switch could not express it.
#
#                 --proto pcp      answers PCP (and NAT-PMP if asked)
#                         natpmp   drops PCP silently, as many routers do,
#                                  and answers NAT-PMP: the fallback path
#                         unsupp   answers PCP with UNSUPP_VERSION and
#                                  version 0, RFC 6887 section 9's "I am a
#                                  NAT-PMP server", then speaks NAT-PMP
#                         unsupp2  answers UNSUPP_VERSION with version 2,
#                                  i.e. some other PCP version: the branch
#                                  where the board's drop to NAT-PMP is a
#                                  guess rather than something the RFC
#                                  licenses, and otherwise unreachable
#                         deaf     answers nothing at all: the common case
#
#                 --fault none      grants what is asked
#                         refuse    says no (the feature switched off in the
#                                   router's menu)
#                         busy      NETWORK_FAILURE / no lease of its own
#                         carrier   grants on a 100.64/10 address, so the
#                                   board must say "NOT on the internet"
#                         noaddr    claims 0.0.0.0 as its outside address
#                         otherport grants a DIFFERENT outside port, which
#                                   announce must then publish
#                         reboot    grants, then after --reboot-after
#                                   seconds puts its epoch back and forgets.
#                                   Only observable with --max-life 20 as
#                                   well: at the default 7200 nothing asks
#                                   again inside a test's lifetime, so the
#                                   board would notice the reset epoch an
#                                   hour later. The two go together; they
#                                   are not alternatives.
#                         short     grants a 20 second lifetime, so a
#                                   renewal falls due in 10 and the renewal
#                                   path runs inside a test's lifetime
#                         godeaf    answers normally, then after
#                                   --deaf-after seconds LOGS every request
#                                   and answers none. Not the same as
#                                   --proto deaf: a mapping is granted
#                                   first, and the datagrams are still
#                                   counted, which is what lets a test tell
#                                   "it gave up" from "it is still asking"
#                                   after the setting is switched off. The
#                                   one mode a dead router process cannot
#                                   stand in for, because a dead process
#                                   logs nothing and the count stops either
#                                   way.
#
# NOT RUN:      written with the firmware and deliberately not executed:
#               every testing plan needs Rob's explicit OK. Only its syntax
#               has been checked.
#
# Copyright 2026 - Robert Mech
# License:      GNU General Public License v3 or later
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or (at your
# option) any later version. See the LICENSE file at the top of this
# repository.
# ===========================================================================
"""A stand-in NAT-PMP / PCP router, written from the RFCs."""

import argparse
import socket
import struct
import sys
import time

# RFC 6887 section 7.4, and RFC 6886 section 3.5. Written out rather than
# imported from anywhere, which is the point of this file.
PCP_SUCCESS = 0
PCP_UNSUPP_VERSION = 1
PCP_NOT_AUTHORIZED = 2
PCP_UNSUPP_OPCODE = 4
PCP_NETWORK_FAILURE = 7

PMP_SUCCESS = 0
PMP_UNSUPP_VERSION = 1
PMP_NOT_AUTHORIZED = 2
PMP_NETWORK_FAILURE = 3


def log(msg):
    sys.stdout.write("fake_router: %s\n" % msg)
    sys.stdout.flush()


class Router(object):
    def __init__(self, args):
        self.args = args
        self.started = time.time()
        self.forgot_at = None
        self.maps = {}          # internal port -> (external port, expiry)

    def epoch(self):
        """Seconds since this router started keeping mappings.

        A reboot puts it back to nearly zero, which is exactly the signal
        RFC 6886 section 3.6 tells a client to watch for.
        """
        base = self.forgot_at if self.forgot_at else self.started
        return int(time.time() - base)

    def maybe_forget(self):
        if self.args.fault != "reboot" or self.forgot_at:
            return
        if time.time() - self.started < self.args.reboot_after:
            return
        self.forgot_at = time.time()
        self.maps = {}
        log("forgetting every mapping and putting the epoch back (a reboot)")

    def external_for(self, internal):
        if self.args.fault == "otherport":
            return (internal + 1) & 0xFFFF
        return internal

    def external_addr(self):
        if self.args.fault == "carrier":
            return self.args.carrier_addr
        if self.args.fault == "noaddr":
            return "0.0.0.0"
        return self.args.outside

    def lifetime(self, asked):
        if asked == 0:
            return 0
        if self.args.fault == "short":
            return 20
        return min(asked, self.args.max_life)

    def gone_deaf(self):
        """True once --fault godeaf has had its --deaf-after seconds."""
        return (self.args.fault == "godeaf"
                and time.time() - self.started >= self.args.deaf_after)

    def refusal(self, pcp):
        """The result code this fault makes a success into, or 0."""
        if self.args.fault == "refuse":
            return PCP_NOT_AUTHORIZED if pcp else PMP_NOT_AUTHORIZED
        if self.args.fault == "busy":
            return PCP_NETWORK_FAILURE if pcp else PMP_NETWORK_FAILURE
        return 0

    # -- PCP, RFC 6887 ------------------------------------------------------
    def pcp(self, pkt):
        """A version 2 request. The dispatcher only sends version 2 here."""
        if len(pkt) < 24:
            return None
        r_op = pkt[1]
        if r_op & 0x80:
            return None                          # a response, not a request
        if (r_op & 0x7F) != 1:
            # Not MAP. RFC 6887 section 7.4's UNSUPP_OPCODE, rather than
            # silence, so a client that ever sends PEER or ANNOUNCE is told.
            return struct.pack(">BBBBII", 2, 0x80 | (r_op & 0x7F), 0,
                               PCP_UNSUPP_OPCODE, 0, self.epoch()) + b"\0" * 12
        if self.args.proto in ("natpmp", "deaf") or self.gone_deaf():
            return None                          # dropped, as many routers do
        if self.args.proto == "unsupp":
            # RFC 6887 section 9: version 0 in the reply is how a client
            # learns to drop to NAT-PMP.
            return struct.pack(">BBBBII", 0, 0x81, 0, PCP_UNSUPP_VERSION, 0,
                               self.epoch()) + b"\0" * 12
        if self.args.proto == "unsupp2":
            # UNSUPP_VERSION with version 2: a server that speaks some other
            # PCP version and NOT NAT-PMP. The RFC licenses nothing here, so
            # whatever the board does with it is a guess worth seeing.
            return struct.pack(">BBBBII", 2, 0x81, 0, PCP_UNSUPP_VERSION, 0,
                               self.epoch()) + b"\0" * 12
        if len(pkt) < 60:
            return None
        asked_life = struct.unpack(">I", pkt[4:8])[0]
        mapdata = pkt[24:60]
        nonce = mapdata[0:12]
        proto = mapdata[12]
        internal = struct.unpack(">H", mapdata[16:18])[0]

        code = self.refusal(True)
        life = self.lifetime(asked_life) if code == PCP_SUCCESS else 0
        ext = 0
        if code == PCP_SUCCESS and life:
            ext = self.external_for(internal)
            self.maps[internal] = (ext, time.time() + life)
            log("PCP MAP proto %u port %u -> %s:%u for %u s"
                % (proto, internal, self.external_addr(), ext, life))
        elif code == PCP_SUCCESS:
            self.maps.pop(internal, None)
            log("PCP MAP port %u deleted" % internal)

        head = struct.pack(">BBBBII", 2, 0x81, 0, code, life, self.epoch()) + b"\0" * 12
        # The IPv4-mapped form, ::ffff:a.b.c.d (RFC 6887 section 5).
        ip16 = b"\0" * 10 + b"\xff\xff" + socket.inet_aton(self.external_addr())
        body = nonce + bytes([proto]) + b"\0\0\0" + \
            struct.pack(">HH", internal, ext) + ip16
        return head + body

    # -- NAT-PMP, RFC 6886 --------------------------------------------------
    def natpmp(self, pkt):
        if len(pkt) < 2:
            return None
        # unsupp2 is a router that speaks another PCP version and no
        # NAT-PMP, so it must be silent here too, or the board's guess would
        # succeed and the branch would not be exercised as the dead end it is.
        if self.args.proto in ("deaf", "unsupp2") or self.gone_deaf():
            return None
        version, op = pkt[0], pkt[1]
        if version != 0:
            # RFC 6886 section 3.5: a version it does not know gets result 1
            # with version 0 in the reply, which is also RFC 6887 section 9's
            # "this is a NAT-PMP server". Only reached if the dispatcher ever
            # routes a non-version-2, non-version-0 packet here.
            return struct.pack(">BBHI", 0, 128, PMP_UNSUPP_VERSION, self.epoch()) + \
                socket.inet_aton("0.0.0.0")
        if op == 0:                              # the public address request
            code = self.refusal(False)
            log("NAT-PMP public address -> %s%s" % (self.external_addr(),
                                                    "" if code == 0 else " (result %u)" % code))
            return struct.pack(">BBHI", 0, 128, code, self.epoch()) + \
                socket.inet_aton(self.external_addr())
        if op not in (1, 2):
            # RFC 6886 section 3.5 result 5 is "unsupported opcode"; 1 is
            # for a version. `op` can be up to 255, so it is packed as one
            # byte with the reply bit set rather than added to 128.
            return struct.pack(">BBHI", 0, (128 | op) & 0xFF, 5, self.epoch())
        if len(pkt) < 12:
            return None
        internal = struct.unpack(">H", pkt[4:6])[0]
        asked_life = struct.unpack(">I", pkt[8:12])[0]

        code = self.refusal(False)
        life = self.lifetime(asked_life) if code == PMP_SUCCESS else 0
        ext = 0
        if code == PMP_SUCCESS and life:
            ext = self.external_for(internal)
            self.maps[internal] = (ext, time.time() + life)
            log("NAT-PMP map port %u -> %u for %u s" % (internal, ext, life))
        elif code == PMP_SUCCESS:
            self.maps.pop(internal, None)
            log("NAT-PMP map port %u deleted" % internal)
        return struct.pack(">BBHIHHI", 0, (128 | op) & 0xFF, code, self.epoch(),
                           internal, ext, life)

    def serve(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((self.args.bind, self.args.port))
        s.settimeout(0.25)
        log("proto %s fault %s on %s:%u, outside %s"
            % (self.args.proto, self.args.fault, self.args.bind, self.args.port,
               self.external_addr()))
        while True:
            self.maybe_forget()
            try:
                pkt, peer = s.recvfrom(1500)
            except socket.timeout:
                continue
            except KeyboardInterrupt:
                return
            if not pkt:
                continue
            # The version byte routes it, which is the whole negotiation
            # (RFC 6887 section 9). Version 2 is PCP; anything else goes to
            # the NAT-PMP side, which answers version 0 and refuses the rest.
            reply = self.pcp(pkt) if pkt[0] == 2 else self.natpmp(pkt)
            if reply is None:
                log("dropped a %u byte request (version %u)" % (len(pkt), pkt[0]))
                continue
            s.sendto(reply, peer)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", type=int, default=15351,
                   help="UDP port to listen on (5351 is the real one)")
    p.add_argument("--bind", default="127.0.0.1")
    p.add_argument("--proto", default="pcp",
                   choices=["pcp", "natpmp", "unsupp", "unsupp2", "deaf"],
                   help="which protocol this router speaks")
    p.add_argument("--fault", default="none",
                   choices=["none", "refuse", "busy", "carrier", "noaddr",
                            "otherport", "reboot", "short", "godeaf"],
                   help="what goes wrong, independently of the protocol")
    p.add_argument("--outside", default="203.0.113.9",
                   help="the outside address to claim (RFC 5737 documentation range)")
    p.add_argument("--carrier-addr", default="100.76.3.9",
                   help="what the carrier mode claims (RFC 6598, 100.64/10)")
    p.add_argument("--max-life", type=int, default=7200,
                   help="the longest lifetime this router will grant")
    p.add_argument("--reboot-after", type=float, default=15.0,
                   help="seconds before the reboot mode forgets everything")
    p.add_argument("--deaf-after", type=float, default=10.0,
                   help="seconds before the godeaf mode stops answering")
    args = p.parse_args()
    try:
        Router(args).serve()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
