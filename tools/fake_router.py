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
#                         upnp     drops PCP and NAT-PMP and answers UPnP
#                                  IGD instead: an SSDP search on
#                                  --ssdp-port, a device description and
#                                  SOAP over HTTP on --http-port. The one
#                                  protocol RouterOS and most consumer
#                                  routers actually have.
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
#                   UPnP only, each a shape a real router has:
#                         perm      answers 725 OnlyPermanentLeasesSupported
#                                   to any non-zero lease, so the board must
#                                   ask again with 0 and then show the
#                                   mapping as permanent
#                         taken     answers 718 ConflictInMappingEntry for
#                                   the first outside number asked for and
#                                   grants the next, so the board must walk
#                                   up and announce must publish what it got
#                         ppp       a description whose WAN service is
#                                   WANPPPConnection:1, so the SOAPACTION
#                                   must follow it
#                         ctlabs    an ABSOLUTE controlURL on a second HTTP
#                                   port, which is the only case where the
#                                   control service is not where the
#                                   description was
#                         revorder  <controlURL> BEFORE <serviceType> inside
#                                   the WAN service element, which the UPnP
#                                   template does not do and some devices do
#                         nosvc     a valid description with no WAN
#                                   connection service in it at all
#                         bigdesc   a description padded past 20 KB, so the
#                                   streaming scan is read across many ticks
#                         ctlname   a LOCATION naming a HOSTNAME rather than
#                                   an address, which the board refuses
#                                   rather than resolving on the loop
#                         ctlelsewhere  a LOCATION on a THIRD host, which is
#                                   what anything on the LAN can put in a
#                                   spoofed SSDP reply; the board must
#                                   refuse it rather than go and fetch it
#                         ctlcrlf   a controlURL carrying CR LF and a second
#                                   request line, which the board must
#                                   refuse whole rather than put in its own
#                                   POST line
#                         ctllong   a controlURL of 95 characters, the
#                                   longest the board's capture holds, which
#                                   is what found its request buffer four
#                                   bytes short
#                         hugedesc  a description past the board's 64 KB cap,
#                                   so its run-away branch is reached
#
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
#               has been checked. That is as true of the UPnP side added in
#               1.2.2-portmap.3 as of the two UDP protocols.
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
"""A stand-in NAT-PMP, PCP and UPnP IGD router, written from the specs."""

import argparse
import select
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
        # UPnP's own table, keyed by the OUTSIDE port, because that is what
        # IGD's DeletePortMapping names and the UDP protocols' requests name
        # the inside one. One dict for both looked tidier and meant a UPnP
        # delete after a NAT-PMP add matched nothing (the code review).
        self.umaps = {}         # external port -> expiry
        # The first outside number the taken fault refused. Only that one is
        # refused: the board walks UP through numbers rather than re-asking
        # for the same one, so refusing every first sighting refused the
        # whole walk and the granted-on-another-number path was unreachable
        # (the code review).
        self.taken_first = None

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
        if self.args.proto in ("natpmp", "deaf", "upnp") or self.gone_deaf():
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
        if self.args.proto in ("deaf", "unsupp2", "upnp") or self.gone_deaf():
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

    # -- UPnP IGD ----------------------------------------------------------
    #
    # Three protocols, written from UPnP Device Architecture 1.1 and the IGD
    # WANIPConnection service template and from nothing in the firmware.
    #
    # The description below deliberately puts a Layer3Forwarding service,
    # with a controlURL of its OWN, in FRONT of the WAN one: a client that
    # keeps the first controlURL it sees would POST its actions at the wrong
    # service, and this is the only cheap way to find that out.

    def ssdp_port(self):
        return self.args.ssdp_port or (self.args.port + 1)

    def http_port(self):
        return self.args.http_port or (self.args.port + 2)

    def ctl_port(self):
        """Where the control service lives: the same HTTP server, except
        under ctlabs, where an absolute controlURL names another port."""
        if self.args.fault == "ctlabs":
            return self.http_port() + 1
        return self.http_port()

    def svc_type(self):
        return ("urn:schemas-upnp-org:service:WANPPPConnection:1"
                if self.args.fault == "ppp"
                else "urn:schemas-upnp-org:service:WANIPConnection:1")

    def ctl_url(self):
        if self.args.fault == "ctlabs":
            return "http://%s:%u/ctl/IPConn" % (self.args.bind, self.ctl_port())
        if self.args.fault == "ctlcrlf":
            # A control URL carrying a request line of its own. The board
            # puts the control URL straight into "POST %s HTTP/1.1", so
            # without a check on what the capture may hold, this makes it
            # issue a second request of the author's choosing from its own
            # trusted address. The board must refuse the whole URL.
            return ("/ctl/IPConn HTTP/1.1\r\nX-Injected: yes\r\n\r\n"
                    "POST /ctl/Evil")
        if self.args.fault == "ctllong":
            # Exactly 95 characters, the longest the board's capture holds,
            # which is also what found its request buffer four bytes short.
            # 1 + 87 + 7; it read 93 when first written, which would still
            # have caught the old 352 but not the worst case it names.
            url = "/" + ("c" * 87) + "/IPConn"
            assert len(url) == 95, len(url)
            return url
        return "/ctl/IPConn"

    def description(self):
        """The device description: one <service> the board must ignore, then
        the WAN one it wants, nested the way a real IGD nests it."""
        svc = self.svc_type()
        ident = ('<serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId>'
                 '<SCPDURL>/WANIPCn.xml</SCPDURL>')
        if self.args.fault == "revorder":
            # controlURL FIRST, which the template does not do and some
            # devices do anyway.
            wan = ('<controlURL>%s</controlURL>'
                   '<serviceType>%s</serviceType>%s'
                   '<eventSubURL>/evt/IPConn</eventSubURL>'
                   % (self.ctl_url(), svc, ident))
        elif self.args.fault == "nosvc":
            wan = ('<serviceType>'
                   'urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1'
                   '</serviceType>'
                   '<serviceId>urn:upnp-org:serviceId:WANCommonIFC1</serviceId>'
                   '<SCPDURL>/WANCfg.xml</SCPDURL>'
                   '<controlURL>/ctl/CommonIfCfg</controlURL>'
                   '<eventSubURL>/evt/CommonIfCfg</eventSubURL>')
        else:
            wan = ('<serviceType>%s</serviceType>%s'
                   '<controlURL>%s</controlURL>'
                   '<eventSubURL>/evt/IPConn</eventSubURL>'
                   % (svc, ident, self.ctl_url()))
        pad = ""
        if self.args.fault == "hugedesc":
            # Past the board's 64 KB cap, so the branch that gives up on a
            # description that runs away is actually reached. Note for a
            # test: the board reads 512 bytes a pass, so 64 KB needs about
            # 128 passes, against kUpnpMs of 8 s, which on the harness's 4x
            # clock is 2 s of wall time. On a loaded lane the budget can
            # win the race, so assert the OUTCOME (nothing mapped) rather
            # than the "ran past" line, or raise kUpnpReads.
            pad = "<modelDescription>%s</modelDescription>" % ("padding " * 9000)
        if self.args.fault == "bigdesc":
            # Legal XML padding that contains no tag the board looks for, so
            # the only thing it tests is reading a body across many ticks.
            pad = "<modelDescription>%s</modelDescription>" % ("padding " * 2600)
        return ('<?xml version="1.0"?>'
                '<root xmlns="urn:schemas-upnp-org:device-1-0">'
                '<specVersion><major>1</major><minor>0</minor></specVersion>'
                '<device>'
                '<deviceType>'
                'urn:schemas-upnp-org:device:InternetGatewayDevice:1'
                '</deviceType>'
                '<friendlyName>fake_router</friendlyName>'
                '<manufacturer>unleashed tests</manufacturer>'
                '<modelName>stand-in</modelName>'
                + pad +
                '<UDN>uuid:00000000-0000-0000-0000-00000000f00d</UDN>'
                '<serviceList><service>'
                '<serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1'
                '</serviceType>'
                '<serviceId>urn:upnp-org:serviceId:L3Forwarding1</serviceId>'
                '<SCPDURL>/l3f.xml</SCPDURL>'
                '<controlURL>/ctl/L3F</controlURL>'
                '<eventSubURL>/evt/L3F</eventSubURL>'
                '</service></serviceList>'
                '<deviceList><device>'
                '<deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>'
                '<deviceList><device>'
                '<deviceType>'
                'urn:schemas-upnp-org:device:WANConnectionDevice:1'
                '</deviceType>'
                '<serviceList><service>' + wan + '</service></serviceList>'
                '</device></deviceList></device></deviceList>'
                '</device></root>')

    def ssdp(self, pkt, peer, sock):
        """An M-SEARCH, answered with the description's LOCATION (UDA 1.1
        section 1.3.3)."""
        if self.args.proto != "upnp" or self.gone_deaf():
            log("dropped an SSDP datagram")
            return
        if not pkt.decode("latin-1").upper().startswith("M-SEARCH"):
            return
        host = self.args.bind
        if self.args.fault == "ctlname":
            host = "fakerouter.invalid"
        elif self.args.fault == "ctlelsewhere":
            # A LOCATION on a THIRD host, which is what anything on the LAN
            # can put in a spoofed SSDP reply. The board must refuse it: an
            # SSDP reply is a redirect, so without a check the board fetches
            # a stranger's description, takes their control URL, believes
            # their outside address and publishes their port to the
            # directory. 127.0.0.2 is loopback to the kernel and is not the
            # gateway to the board, which is exactly the distinction.
            host = "127.0.0.2"
        loc = "http://%s:%u/rootDesc.xml" % (host, self.http_port())
        reply = ("HTTP/1.1 200 OK\r\n"
                 "CACHE-CONTROL: max-age=1800\r\n"
                 "EXT:\r\n"
                 "LOCATION: %s\r\n"
                 "SERVER: fake_router/1.0 UPnP/1.0\r\n"
                 "ST: upnp:rootdevice\r\n"
                 "USN: uuid:00000000-0000-0000-0000-00000000f00d"
                 "::upnp:rootdevice\r\n"
                 "\r\n" % loc)
        log("SSDP M-SEARCH answered with %s" % loc)
        sock.sendto(reply.encode("latin-1"), peer)

    def soap_fault(self, code, text):
        body = ('<?xml version="1.0"?>'
                '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"'
                ' s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
                '<s:Body><s:Fault><faultcode>s:Client</faultcode>'
                '<faultstring>UPnPError</faultstring><detail>'
                '<UPnPError xmlns="urn:schemas-upnp-org:control-1-0">'
                '<errorCode>%u</errorCode>'
                '<errorDescription>%s</errorDescription>'
                '</UPnPError></detail></s:Fault></s:Body></s:Envelope>'
                % (code, text))
        return 500, body

    def soap_ok(self, action, inner=""):
        body = ('<?xml version="1.0"?>'
                '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"'
                ' s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
                '<s:Body><u:%sResponse xmlns:u="%s">%s</u:%sResponse>'
                '</s:Body></s:Envelope>'
                % (action, self.svc_type(), inner, action))
        return 200, body

    def soap(self, head, body):
        """One SOAP action. The action comes from SOAPACTION, which is where
        a real IGD reads it, and the service type inside it is CHECKED:
        answering an action aimed at WANIPConnection on a router that serves
        WANPPPConnection would hide exactly the bug the ppp fault exists
        for."""
        act = ""
        for line in head.split("\r\n"):
            if line.upper().startswith("SOAPACTION:"):
                act = line.split(":", 1)[1].strip().strip('"')
        if "#" not in act:
            return self.soap_fault(401, "Invalid Action")
        urn, name = act.rsplit("#", 1)
        if urn != self.svc_type():
            log("SOAPACTION names %s and this router serves %s"
                % (urn, self.svc_type()))
            return self.soap_fault(401, "Invalid Action")

        if self.args.fault == "refuse":
            return self.soap_fault(606, "Action not authorized")
        if self.args.fault == "busy":
            return self.soap_fault(729, "ConflictWithOtherMechanisms")

        if name == "GetExternalIPAddress":
            addr = self.external_addr()
            log("UPnP GetExternalIPAddress -> %s" % addr)
            return self.soap_ok(
                name, "<NewExternalIPAddress>%s</NewExternalIPAddress>"
                % ("" if addr == "0.0.0.0" else addr))

        def arg(tag):
            a = "<%s>" % tag
            b = "</%s>" % tag
            if a not in body or b not in body:
                return ""
            return body.split(a, 1)[1].split(b, 1)[0]

        def port(tag):
            """A port argument, or None when it is missing or not a number.
            A real IGD answers 402 to either; letting int() raise would kill
            this process instead, which a test reads as the router dying."""
            v = arg(tag)
            if not v.isdigit():
                return None
            n = int(v)
            return n if 1 <= n <= 65535 else None

        ext = port("NewExternalPort")
        if name == "DeletePortMapping":
            if ext is None:
                return self.soap_fault(402, "Invalid Args")
            if ext in self.umaps:
                del self.umaps[ext]
                log("UPnP DeletePortMapping %u" % ext)
                return self.soap_ok(name)
            log("UPnP DeletePortMapping %u: nothing there" % ext)
            return self.soap_fault(714, "NoSuchEntryInArray")

        if name != "AddPortMapping":
            return self.soap_fault(401, "Invalid Action")
        # Every argument IGD's AddPortMapping requires is checked, not just
        # logged. A stand-in that grants a request with a missing or wrong
        # argument agrees with whatever the client sends, which is the one
        # thing the header of this file says it exists not to do: a firmware
        # change that sent the outside port where the inside one belongs
        # would have been answered with success.
        inn = port("NewInternalPort")
        client = arg("NewInternalClient")
        proto = arg("NewProtocol")
        life = arg("NewLeaseDuration")
        if ext is None or inn is None:
            log("UPnP AddPortMapping with no usable ports")
            return self.soap_fault(402, "Invalid Args")
        if proto not in ("TCP", "UDP"):
            log("UPnP AddPortMapping protocol %r" % proto)
            return self.soap_fault(402, "Invalid Args")
        if not client or client.count(".") != 3:
            log("UPnP AddPortMapping client %r" % client)
            return self.soap_fault(402, "Invalid Args")
        if arg("NewEnabled") not in ("1", "0"):
            return self.soap_fault(402, "Invalid Args")
        if life and not life.isdigit():
            return self.soap_fault(402, "Invalid Args")
        if self.args.fault == "perm" and life not in ("", "0"):
            log("UPnP AddPortMapping refused lease %s" % life)
            return self.soap_fault(725, "OnlyPermanentLeasesSupported")
        if self.args.fault == "taken":
            if self.taken_first is None:
                self.taken_first = ext
            if ext == self.taken_first:
                # ONE number belongs to somebody else and the next is free:
                # exactly a router with one old mapping left behind by
                # another host, and the only shape that lets the board's
                # walk up through numbers actually finish.
                log("UPnP AddPortMapping %u is taken" % ext)
                return self.soap_fault(718, "ConflictInMappingEntry")
        self.umaps[ext] = time.time() + 7200
        log("UPnP AddPortMapping %u -> %s:%u proto %s lease %s desc %r"
            % (ext, client, inn, proto, life,
               arg("NewPortMappingDescription")))
        return self.soap_ok(name)

    def http(self, conn):
        """One HTTP request: the description, or a SOAP action. Handled
        inline because the board is the only client, and a thread per
        connection would be more machinery than this needs."""
        conn.settimeout(2.0)
        data = b""
        try:
            while b"\r\n\r\n" not in data and len(data) < 65536:
                chunk = conn.recv(4096)
                if not chunk:
                    break
                data += chunk
            if not data:
                return
            head, _, rest = data.partition(b"\r\n\r\n")
            head = head.decode("latin-1")
            first = head.split("\r\n", 1)[0]
            want = 0
            for line in head.split("\r\n"):
                if line.upper().startswith("CONTENT-LENGTH:"):
                    want = int(line.split(":", 1)[1].strip() or 0)
            while len(rest) < want:
                chunk = conn.recv(4096)
                if not chunk:
                    break
                rest += chunk
            # The witness for the ctlcrlf fault, and it has to be here
            # rather than in the test: what a test can see from outside is
            # only that no POST arrived, which is also what "the board gave
            # up for some other reason" looks like. A header or a request
            # line the fault's own control URL carried, arriving at all, is
            # positive proof the injection went through, so a test can
            # assert its ABSENCE and mean something. Logged whatever the
            # fault, so the day a different path lets one through it is
            # still seen.
            if "X-INJECTED" in head.upper():
                log("HTTP request carried an INJECTED header: %s" % first)
            for line in head.split("\r\n")[1:] + rest.decode("latin-1").split("\r\n"):
                up = line.upper()
                if (up.startswith(("GET ", "POST ", "HEAD ", "PUT "))
                        and "HTTP/1." in up):
                    log("HTTP request carried a SECOND request line: %s" % line)
                    break
            if self.gone_deaf():
                log("HTTP %r while deaf" % first)
                return
            if first.upper().startswith("GET"):
                log("HTTP %s" % first)
                code, payload = 200, self.description()
            else:
                log("HTTP %s" % first)
                code, payload = self.soap(head, rest.decode("latin-1"))
            out = ('HTTP/1.1 %u %s\r\n'
                   'CONTENT-TYPE: text/xml; charset="utf-8"\r\n'
                   'CONTENT-LENGTH: %u\r\n'
                   'CONNECTION: close\r\n'
                   '\r\n%s'
                   % (code, "OK" if code == 200 else "Internal Server Error",
                      len(payload), payload))
            conn.sendall(out.encode("latin-1"))
        except (socket.timeout, OSError) as e:
            log("HTTP connection failed: %s" % e)
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def serve(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((self.args.bind, self.args.port))
        reading = [s]
        ssdp = None
        tcp = None
        if self.args.proto == "upnp":
            # SSDP and HTTP live on ports of their own, so one stand-in
            # router serves all three protocols and a lane never has to bind
            # 1900 or 80.
            ssdp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            ssdp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            ssdp.bind((self.args.bind, self.ssdp_port()))
            tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            tcp.bind((self.args.bind, self.http_port()))
            tcp.listen(4)
            reading += [ssdp, tcp]
            if self.args.fault == "ctlabs":
                ctl = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                ctl.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                ctl.bind((self.args.bind, self.ctl_port()))
                ctl.listen(4)
                reading.append(ctl)
        log("proto %s fault %s on %s:%u, outside %s"
            % (self.args.proto, self.args.fault, self.args.bind, self.args.port,
               self.external_addr()))
        if ssdp is not None:
            log("SSDP on %s:%u, HTTP on %s:%u"
                % (self.args.bind, self.ssdp_port(),
                   self.args.bind, self.http_port()))
        while True:
            self.maybe_forget()
            try:
                ready, _, _ = select.select(reading, [], [], 0.25)
            except (OSError, ValueError):
                return
            except KeyboardInterrupt:
                return
            for sock in ready:
                try:
                    if sock.type == socket.SOCK_STREAM:
                        conn, _peer = sock.accept()
                        self.http(conn)
                        continue
                    pkt, peer = sock.recvfrom(1500)
                except OSError:
                    continue
                except KeyboardInterrupt:
                    return
                if not pkt:
                    continue
                if ssdp is not None and sock is ssdp:
                    self.ssdp(pkt, peer, sock)
                    continue
                # The version byte routes it, which is the whole negotiation
                # (RFC 6887 section 9). Version 2 is PCP; anything else goes
                # to the NAT-PMP side, which answers version 0 and refuses
                # the rest.
                reply = self.pcp(pkt) if pkt[0] == 2 else self.natpmp(pkt)
                if reply is None:
                    log("dropped a %u byte request (version %u)"
                        % (len(pkt), pkt[0]))
                    continue
                sock.sendto(reply, peer)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", type=int, default=15351,
                   help="UDP port to listen on (5351 is the real one)")
    p.add_argument("--bind", default="127.0.0.1")
    p.add_argument("--proto", default="pcp",
                   choices=["pcp", "natpmp", "unsupp", "unsupp2", "deaf", "upnp"],
                   help="which protocol this router speaks")
    p.add_argument("--fault", default="none",
                   choices=["none", "refuse", "busy", "carrier", "noaddr",
                            "otherport", "reboot", "short", "godeaf",
                            "perm", "taken", "ppp", "ctlabs", "revorder",
                            "nosvc", "bigdesc", "hugedesc", "ctlname",
                            "ctlelsewhere", "ctlcrlf", "ctllong"],
                   help="what goes wrong, independently of the protocol")
    p.add_argument("--ssdp-port", type=int, default=0,
                   help="UPnP: the SSDP port (1900 is the real one); "
                        "0 means --port plus one")
    p.add_argument("--http-port", type=int, default=0,
                   help="UPnP: the description and SOAP port; "
                        "0 means --port plus two")
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
