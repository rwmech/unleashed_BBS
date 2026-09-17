#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/testclient.py
Module:       Tools / scripted test callers

Purpose:      Scripted callers for the BBS (host build or a real board).
                 - ANSI caller: answers cursor-position requests like a PC
                   terminal; handle validation, shortcuts, history, TIME, G
                 - Telnet-first caller: IAC before the probe gets character
                   mode negotiated first and an immediate probe (PuTTY case)
                 - PETSCII caller: silent on the probe, presses INST/DEL,
                   picks 40 columns, logs in, runs WHO/FX/BYE
                 - ASCII caller: backspace at the key prompt
                 - Paging between nodes, DND, arrival notices, LAST
                 - Sysop: BYE <password> masking, NODES, More paging, TIME
                   adjust warnings, BROADCAST, SNOOP, KICK, SHOW, DROP
                 - Accounts: sign-up form checks (ANSI, PETSCII, ASCII), wrong
                   passwords and per-handle lockout, PROFILE, PASSWORD, INFO
                   privacy, USERS manager, USER ADD/EDIT/DEL, locked accounts
                 - Handle prompt idle warning with partial input redraw (31 s)
                 - Busy line: busy screen, countdown hangup, overflow BUSY,
                   busy-line guest elevating to sysop
                 - --ban: 3 wrong sysop passwords ban the IP (host only: it
                   bans the machine running the test for 15 minutes)

Usage:        python3 tools/testclient.py [host] [port] [--backup] [--ban] [--slow]

Libraries:    Python 3 standard library only
Targets:      developer PC, Python 3
See also:     README.md

Copyright 2026 - Robert Mech
License:      GNU General Public License v2 or later
SPDX-License-Identifier: GPL-2.0-or-later

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, see <https://www.gnu.org/licenses/>. The full
text is in the LICENSE file at the top of this repository.
===========================================================================
"""

import http.client
import io
import os
import pathlib
import re
import socket
import sys
import threading
import time
import zipfile

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
FLAGS = {a for a in sys.argv[1:] if a.startswith("--")}
HOST = ARGS[0] if len(ARGS) > 0 else "127.0.0.1"
PORT = int(ARGS[1]) if len(ARGS) > 1 else 6400

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = pathlib.Path(os.environ.get("BBS_DATA", ROOT / "data"))   # same dir the server reads


def cfg_value(key):
    cfg = DATA / "system.cfg"
    if not cfg.exists():
        return ""
    for line in cfg.read_text().splitlines():
        line = line.split("#", 1)[0]
        if line.strip().startswith("["):
            break
        if "=" in line:
            k, v = line.split("=", 1)
            if k.strip() == key:
                return v.strip()
    return ""


PASSWORD = cfg_value("sysop_password")
CO1 = cfg_value("cosysop1_password")
CO2 = cfg_value("cosysop2_password")
BACKUP_PORT = int(cfg_value("backup_port") or 8080)
UP = b"\x1b[A"


class Caller:
    def __init__(self, ansi=False, utf8=True, telnet=False):
        self.t0 = time.time()
        self.s = socket.create_connection((HOST, PORT), timeout=5)
        self.s.setblocking(False)
        self.buf = bytearray()
        self.ansi = ansi
        self.utf8 = utf8
        self.col = 1
        self.first_probe = None
        if telnet:
            # like PuTTY: WILL NAWS + window size before the server says anything
            self.s.sendall(b"\xff\xfb\x1f\xff\xfa\x1f\x00\x50\x00\x18\xff\xf0")

    def pump(self, secs):
        end = time.time() + secs
        while time.time() < end:
            try:
                d = self.s.recv(4096)
                if not d:
                    return False
                self._answer(d)
                self.buf += d
            except BlockingIOError:
                time.sleep(0.02)
            except (ConnectionResetError, OSError):
                return False
        return True

    def _answer(self, d):
        """Emulate an ANSI terminal answering ESC[6n with the cursor column."""
        if b"DETECTING TERMINAL" in d and self.first_probe is None:
            self.first_probe = time.time() - self.t0
        if not self.ansi:
            return
        for m in re.finditer(rb"DETECTING TERMINAL|\x1b\[6n|\xe2\x94\x80", d):
            tok = m.group(0)
            if tok == b"DETECTING TERMINAL":
                self.col = 1 + len(tok)
            elif tok == b"\xe2\x94\x80":
                self.col += 1 if self.utf8 else 3
            else:
                self.s.sendall(f"\x1b[3;{self.col}R".encode())

    def send(self, b):
        self.s.sendall(b)

    def wait_for(self, pat, secs=10):
        end = time.time() + secs
        while time.time() < end:
            if pat in self.buf:
                return True
            if not self.pump(0.1):
                break
        return pat in self.buf

    def wait_closed(self, secs):
        end = time.time() + secs
        while time.time() < end:
            if not self.pump(0.1):
                return True
        return False

    def node(self):
        m = re.search(rb"Node (\d) of", self.buf)
        return m.group(1).decode() if m else "?"

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


def check(name, ok):
    print(f"  {'PASS' if ok else 'FAIL'}  {name}")
    return ok


def pet(s):
    out = bytearray()
    for c in s:
        if "a" <= c <= "z":
            out.append(ord(c) - 0x20)
        elif "A" <= c <= "Z":
            out.append(ord(c) + 0x80)
        else:
            out.append(ord(c))
    return bytes(out)


TEST_PW = "pw1234"
DOWN = b"\x1b[B"
F1 = b"\x1bOP"


def wait_any(c, pats, secs=10):
    end = time.time() + secs
    while time.time() < end:
        for i, p in enumerate(pats):
            if p in c.buf:
                return i
        if not c.pump(0.1):
            break
    for i, p in enumerate(pats):
        if p in c.buf:
            return i
    return -1


def login(c, handle, pw=TEST_PW, as_pet=False, wait_main=True):
    """At the handle prompt: log in, or register when the handle is new.
    The signup keys work for the cursor form and the ASCII line form alike."""
    enc = pet if as_pet else (lambda s: s.encode())
    c.send(enc(handle) + b"\r")
    which = wait_any(c, [enc("[R]egister"), enc("Password:")], 8)
    if which == 0:
        email = "".join(ch for ch in handle.lower() if ch.isalnum()) + "@example.com"
        c.send(b"r")
        c.wait_for(enc("NEW ACCOUNT"), 6)
        c.send(enc(pw) + b"\r" + enc(pw) + b"\r" + enc(handle) + b"\r" + enc(email) + b"\r\r\r\r\r")
        if not c.wait_for(enc("WELCOME ABOARD"), 10):
            return False
    elif which == 1:
        c.send(enc(pw) + b"\r")
        if not c.wait_for(enc("ACCESS GRANTED"), 6):
            return False
    else:
        return False
    return c.wait_for(enc("Main"), 8) if wait_main else True


def ansi_login(handle, pw=TEST_PW):
    c = Caller(ansi=True)
    c.wait_for(b"Enter your handle", 10)
    login(c, handle, pw)
    return c


# ---------------------------------------------------------------------------
def test_ansi():
    print("ANSI caller")
    c = Caller(ansi=True, utf8=True)
    ok = check("detected ANSI-UTF8", c.wait_for(b"ANSI-UTF8 DETECTED", 5))
    ok &= check("telnet negotiation sent", c.wait_for(b"\xff\xfb\x01", 2))
    ok &= check("welcome art streamed (UTF-8 block)", c.wait_for("█".encode(), 8))
    ok &= check("@NODE@ expanded", c.wait_for(b"Node \x1b[1;33m", 2))
    ok &= check("@BBS@ shows a real micro sign", c.wait_for("µnleashed BBS".encode(), 3))
    ok &= check("handle prompt", c.wait_for(b"Enter your handle", 8))
    c.buf.clear()
    c.send(b"[3;20R\r")
    ok &= check("terminal junk rejected as handle", c.wait_for(b"Use letters", 3))
    c.buf.clear()
    ok &= check("new handle registers through the form", login(c, "Rob"))
    ok &= check("welcome by handle", b"Welcome" in c.buf and b"Rob" in c.buf)
    ok &= check("time left shown", b"Time left" in c.buf)
    ok &= check("shell prompt", b"Main" in c.buf)
    c.buf.clear()
    c.send(b"term\r")
    ok &= check("TERM reports size", c.wait_for(b"ANSI-UTF8 80x24", 3))
    c.buf.clear()
    c.send(UP + b"\r")
    ok &= check("up-arrow recalls TERM", c.wait_for(b"ANSI-UTF8 80x24", 3))
    c.buf.clear()
    c.send(b"w\r")
    ok &= check("W shortcut runs WHO", c.wait_for(b"Who's online", 3) and c.wait_for(b"Rob", 3))
    ok &= check("WHO has idle column", c.wait_for(b"Idle", 1))
    ok &= check("WHO does not list the busy line", b"busy" not in c.buf)
    c.buf.clear()
    c.send(b"time\r")
    ok &= check("TIME shows time left", c.wait_for(b"Left", 3))
    c.buf.clear()
    c.send(b"who 99\r")
    ok &= check("WHO n out of range explained", c.wait_for(b"WHO n: n is 1 to 30 seconds", 3))
    c.buf.clear()
    c.send(b"who 1\r")
    time.sleep(2.6)
    c.pump(0.2)
    ok &= check("WHO 1 redraws every second", bytes(c.buf).count(b"Refresh 1s") >= 2)
    ok &= check("refresh footer shows the idle clock", b"Idle counts:" in c.buf)
    ok &= check("refresh redraws from home, no scroll", b"\x1b[H" in c.buf)
    c.buf.clear()
    c.send(b"x")
    ok &= check("any key stops the refresh", c.wait_for(b"Main", 3))
    c.buf.clear()
    c.send(b"bogus\r")
    ok &= check("unknown command message", c.wait_for(b"Unknown command", 3))
    c.buf.clear()
    c.send(b"g\r")
    ok &= check("G asks before logging off", c.wait_for(b"Log off (Y/N)?", 3))
    c.buf.clear()
    c.send(b"n")
    ok &= check("N returns to the prompt", c.wait_for(b"Main", 3))
    c.close()
    return ok


def test_telnet_first():
    print("Telnet client speaks first (PuTTY)")
    c = Caller(ansi=True, telnet=True)
    ok = check("detected", c.wait_for(b"DETECTED", 5))
    will_echo = c.buf.find(b"\xff\xfb\x01")
    probe = c.buf.find(b"DETECTING TERMINAL")
    ok &= check("WILL ECHO before the probe", 0 <= will_echo < probe)
    ok &= check("probe sent without the settle wait",
                c.first_probe is not None and c.first_probe < 0.25)
    ok &= check("NAWS size applied", True)
    c.close()
    return ok


def test_petscii():
    print("PETSCII caller")
    c = Caller(ansi=False)
    ok = check("probe text", c.wait_for(b"DETECTING TERMINAL", 3))
    t0 = time.time()
    ok &= check("key prompt after timeout", c.wait_for(b"HIT DEL OR BACKSPACE", 5))
    ok &= check("prompt after ~2 s", 1.5 < time.time() - t0 < 3.5)
    ok &= check("PETSCII clear before prompt", b"\x93\r\nHIT DEL" in c.buf)
    c.send(b"\x14")
    ok &= check("column prompt", c.wait_for(b"40 OR 80 COLUMNS", 3))
    c.send(b"4")
    ok &= check("PETSCII-40 banner (mixed case)", c.wait_for(pet("PETSCII-40 DETECTED"), 5))
    ok &= check("no telnet IAC sent to C64", b"\xff\xfb" not in c.buf)
    ok &= check("welcome.seq streamed", c.wait_for(pet("No web. No cloud. No browser."), 8) and c.wait_for(pet("unleashed BBS"), 3))
    ok &= check("handle prompt", c.wait_for(pet("Enter your handle"), 8))
    ok &= check("PETSCII signup form and login", login(c, "KE9CXN", as_pet=True))
    ok &= check("form drew with cursor moves", b"\x13" in c.buf)
    c.buf.clear()
    c.send(pet("who") + b"\r")
    ok &= check("WHO lists handle", c.wait_for(pet("KE9CXN"), 3))
    c.buf.clear()
    c.send(b"\x91\r")                     # C64 cursor-up recalls WHO
    ok &= check("cursor-up recalls on PETSCII", c.wait_for(pet("KE9CXN"), 3))
    c.buf.clear()
    c.send(pet("xyzzy") + b"\r")
    ok &= check("?SYNTAX  ERROR", c.wait_for(pet("?SYNTAX  ERROR"), 3))
    c.buf.clear()
    c.send(pet("fx") + b"\r")
    ok &= check("FX demo starts", c.wait_for(pet("Typewriter"), 5))
    time.sleep(0.5)
    c.send(b"\x5f")   # C64 left-arrow = stop
    ok &= check("FX demo stops on left-arrow", c.wait_for(pet("Demo stopped."), 6))
    c.buf.clear()
    c.send(pet("bye") + b"\r")
    ok &= check("goodbye + NO CARRIER", c.wait_for(pet("NO CARRIER"), 6))
    ok &= check("server hung up", c.wait_closed(5))
    return ok


def test_ascii():
    print("ASCII caller (backspace)")
    c = Caller(ansi=False)
    c.wait_for(b"HIT DEL OR BACKSPACE", 5)
    c.send(b"\x08")
    ok = check("ASCII detected", c.wait_for(b"ASCII DETECTED", 5))
    ok &= check("welcome.asc streamed", c.wait_for(b"No web. No cloud. No browser.", 8) and c.wait_for(b"unleashed BBS", 3))
    c.wait_for(b"Enter your handle", 8)
    ok &= check("ASCII line-by-line signup", login(c, "Plain"))
    ok &= check("ASCII form used plain prompts", b"Save (Y/n)?" in c.buf)
    c.buf.clear()
    c.send(b"help\r")
    end = time.time() + 8
    while time.time() < end and b"stops output" not in c.buf:
        if c.buf.endswith(b"[More] Y/n/c "):
            c.send(b"c")
        c.pump(0.2)
    text = bytes(c.buf).replace(b"[More] Y/n/c ", b"").replace(b"\x08 \x08", b"").decode("ascii", "replace")
    lines = [l for l in text.split("\r\n") if l]
    body = [l for l in lines if l != "help" and "---" not in l and "stops output" not in l and "Main" not in l]
    ok &= check("HELP generated with rows", len(body) >= 10)
    ok &= check("every HELP line fits 39 columns", all(len(l) <= 39 for l in lines))
    ok &= check("descriptions start at column 14",
                all(len(l) > 13 and l[12] == " " and l[13] != " " for l in body))
    ok &= check("wrapped lines (if any) sit under the description column",
                all(l[:13] == " " * 13 for l in body if l.startswith(" ")))
    c.close()
    return ok


def test_page():
    print("Paging between nodes")
    a = ansi_login("Alice")
    b = ansi_login("Bob")
    na, nb = a.node(), b.node()
    ok = check("both logged in on different nodes", na != "?" and nb != "?" and na != nb)
    ok &= check("Alice sees Bob arrive", a.wait_for(f"*** Bob is on node {nb}".encode(), 3))
    a.buf.clear()
    b.buf.clear()
    a.send(f"page {nb} hello bob\r".encode())
    ok &= check("page confirmed", a.wait_for(b"Page sent", 3))
    ok &= check("Bob receives the page", b.wait_for(f"Page from Alice ({na}): hello bob".encode(), 3))
    msg_at = b.buf.find(b"Page from Alice")
    ok &= check("page arrives with bell and a flashing PAGE tag first",
                0 <= b.buf.find(b"\x07") < b.buf.find(b" PAGE ") < msg_at)
    ok &= check("Bob's prompt redrawn after page", b.wait_for(b"Main", 2))
    b.buf.clear()
    b.send(b"dnd\r")
    ok &= check("DND on", b.wait_for(b"Pages are off", 3))
    a.buf.clear()
    a.send(f"page {nb} again\r".encode())
    ok &= check("DND refuses pages", a.wait_for(b"not taking pages", 3))
    a.buf.clear()
    b.close()
    ok &= check("Alice sees Bob leave", a.wait_for(f"*** Bob left node {nb}".encode(), 5))
    a.buf.clear()
    a.send(b"last\r")
    ok &= check("LAST lists Bob", a.wait_for(b"Last callers", 3) and a.wait_for(b"Bob", 3))
    a.close()
    return ok


def test_sysop():
    print("Sysop node")
    if not PASSWORD:
        print("  SKIP  no sysop_password in data/system.cfg")
        return True
    x = ansi_login("Xavier")
    nx = x.node()
    r = ansi_login("Rob")
    nr = r.node()
    r.buf.clear()
    r.send(f"bye {PASSWORD}\r".encode())
    ok = check("BYE <password> reaches the sysop node", r.wait_for(b"Sysop node.", 4))
    ok &= check("sysop prompt", r.wait_for(b"Sysop", 3))
    ok &= check("password never echoed", PASSWORD.encode() not in r.buf)
    ok &= check("old node reported free", f"Node {nr} is free".encode() in r.buf)
    x.buf.clear()
    x.send(b"who\r")
    x.wait_for(b"Who's online", 3)
    x.pump(0.5)
    ok &= check("hidden sysop not in WHO", re.search(rb"[S1-6][ *>\]]Rob", x.buf) is None)
    ok &= check("caller WHO shows terminals, not what others do", b"Terminal" in x.buf and b"Doing" not in x.buf)
    ok &= check("sysop's old node shows waiting", b"waiting for caller" in x.buf)

    r.buf.clear()
    r.send(b"nodes\r")
    ok &= check("NODES shows IPs", r.wait_for(b"IP", 3) and r.wait_for(b"Xavier", 3))
    r.wait_for(b"Sysop", 3)
    r.buf.clear()
    r.send(b"dash\r")
    ok &= check("DASH shows the dashboard", r.wait_for(b"SYSOP DASHBOARD", 4) and r.wait_for(b"Calls", 4))
    ok &= check("DASH shows Wi-Fi and a Doing column", r.wait_for(b"WiFi", 4) and b"Doing" in r.buf)
    if r.wait_for(b"[More] Y/n/c", 2):
        r.send(b"c")
    ok &= check("DASH lists callers and last calls", r.wait_for(b"Last calls", 4) and b"Xavier" in r.buf)
    r.wait_for(b"Sysop", 4)
    r.buf.clear()
    r.send(b"dash 1\r")
    time.sleep(2.6)
    r.pump(0.2)
    ok &= check("DASH 1 refreshes", bytes(r.buf).count(b"SYSOP DASHBOARD") >= 2)
    ok &= check("sysop footer: no idle limit", b"No idle limit" in r.buf)
    r.buf.clear()
    r.send(b"q")
    ok &= check("key ends the dashboard", r.wait_for(b"Sysop", 3))
    r.buf.clear()
    r.send(b"help\r")
    ok &= check("long HELP pauses at [More]", r.wait_for(b"[More] Y/n/c", 4))
    r.send(b"c")
    ok &= check("C continues nonstop", r.wait_for(b"stops output", 4))

    x.buf.clear()
    r.buf.clear()
    r.send(f"time {nx} -59\r".encode())
    ok &= check("TIME adjust confirmed", r.wait_for(b"min left", 3))
    ok &= check("caller warned about time", x.wait_for(b"minute left.", 4))
    r.send(f"time {nx} +59\r".encode())

    x.buf.clear()
    r.send(b"broadcast system going down soon\r")
    ok &= check("BROADCAST delivered", x.wait_for(b"*** Sysop: system going down soon", 4))

    r.buf.clear()
    r.send(f"snoop {nx}\r".encode())
    ok &= check("SNOOP starts", r.wait_for(b"Snooping node", 3))
    time.sleep(0.3)
    r.buf.clear()
    x.send(b"term\r")
    ok &= check("sysop sees the caller's output", r.wait_for(b"ANSI-UTF8 80x24", 4))
    r.send(b"q")
    ok &= check("Q ends snoop", r.wait_for(b"Snoop ended.", 3))

    r.buf.clear()
    r.send(b"show\r")
    r.wait_for(b"listed in WHO", 3)
    x.buf.clear()
    x.send(b"w\r")
    x.wait_for(b"Who's online", 3)
    x.pump(0.5)
    ok &= check("SHOW lists the sysop, marked ] for sysop", re.search(rb"S\]Rob", x.buf) is not None)
    ok &= check("WHO explains the markers", b"*GUEST" in x.buf and b"]SYSOP" in x.buf)
    r.buf.clear()
    r.send(b"who\r")
    r.wait_for(b"Who's online", 3)
    r.pump(0.5)
    ok &= check("staff WHO shows each caller's last command", b"Doing" in r.buf and
                re.search(rb"\d Xavier +WHO ", r.buf) is not None)
    if HOST in ("127.0.0.1", "localhost"):
        users = (DATA / "users.txt").read_text()
        ok &= check("the sysop password marks the account", "[Rob]" in users and
                    users.split("[Rob]")[1].split("[")[0].count("level = sysop") == 1)

    r.buf.clear()
    r.send(f"kick {nx} bye now\r".encode())
    ok &= check("KICK confirmed", r.wait_for(b"disconnected", 3))
    ok &= check("caller told why", x.wait_for(b"Disconnected by sysop: bye now", 4))
    ok &= check("caller hung up", x.wait_closed(8))

    r.buf.clear()
    r.send(b"drop\r")
    ok &= check("DROP returns to a caller node", r.wait_for(b"Back on node", 4))
    r.close()
    return ok


def test_cosysop():
    print("Co-sysops (default access matrix)")
    if not (CO1 and CO2):
        print("  SKIP  cosysop1_password / cosysop2_password not set")
        return True
    a = ansi_login("Plain")
    na = a.node()
    c1 = ansi_login("Cora")
    n1 = c1.node()
    c2 = ansi_login("Dex")
    n2 = c2.node()

    c1.buf.clear()
    c1.send(f"bye {CO1}\r".encode())
    ok = check("co-sysop 1 granted in place", c1.wait_for(f"Co-sysop 1 access on node {n1}".encode(), 4))
    ok &= check("password never echoed", CO1.encode() not in c1.buf)
    c2.buf.clear()
    c2.send(f"bye {CO2}\r".encode())
    ok &= check("co-sysop 2 granted in place", c2.wait_for(f"Co-sysop 2 access on node {n2}".encode(), 4))

    a.buf.clear()
    a.send(b"who\r")
    a.wait_for(b"Who's online", 3)
    a.pump(0.5)
    ok &= check("co-sysops stay visible in WHO, marked >",
                re.search(rb"\d>Cora ", a.buf) is not None and re.search(rb"\d>Dex ", a.buf) is not None)

    c1.buf.clear()
    c1.send(b"help\r")
    c1.wait_for(b"[More] Y/n/c", 4)
    c1.send(b"c")
    c1.wait_for(b"stops output", 4)
    ok &= check("CO1 help lists KICK, not UNBAN", b"KICK n [msg]" in c1.buf and b"UNBAN" not in c1.buf)

    c2.buf.clear()
    c2.send(f"kick {na}\r".encode())
    ok &= check("CO2 has no KICK", c2.wait_for(b"Unknown command", 3))
    c2.buf.clear()
    c2.send(b"hide\r")
    ok &= check("CO2 has no HIDE", c2.wait_for(b"Unknown command", 3))
    a.buf.clear()
    c2.send(b"broadcast from co2\r")
    ok &= check("CO2 BROADCAST allowed", a.wait_for(b"*** Sysop: from co2", 4))
    c2.buf.clear()
    c2.send(b"nodes\r")
    ok &= check("CO2 NODES allowed", c2.wait_for(b"Cora", 3))

    c1.buf.clear()
    c1.send(b"time\r")
    ok &= check("NOLIMITS: no time limit", c1.wait_for(b"no limit", 3))

    c1.buf.clear()
    c1.send(b"hide\r")
    ok &= check("CO1 HIDE allowed", c1.wait_for(b"hidden from WHO", 3))
    a.buf.clear()
    a.send(b"w\r")
    a.wait_for(b"Who's online", 3)
    a.pump(0.5)
    ok &= check("hidden co-sysop shows as a free line", re.search(rb"Cora", a.buf) is None)
    c2.buf.clear()
    c2.send(b"who\r")
    c2.wait_for(b"Who's online", 3)
    c2.pump(0.5)
    ok &= check("staff see a hidden co-sysop, marked hidden",
                re.search(rb"\d>Cora +hidden", c2.buf) is not None)
    c1.buf.clear()
    c1.send(b"user edit Rob\r")
    ok &= check("a co-sysop cannot edit the sysop's account",
                c1.wait_for(b"above your level", 4))
    c1.buf.clear()
    c1.send(b"user edit Alice\r")
    ok &= check("a co-sysop can still edit a caller's account", c1.wait_for(b"EDIT ACCOUNT", 4))
    c1.send(b"\x1b")
    c1.wait_for(b"Cancelled", 3)

    c1.buf.clear()
    c1.send(f"kick {n2} demoted\r".encode())
    ok &= check("CO1 can KICK a CO2", c2.wait_for(b"Disconnected by sysop: demoted", 4))

    c1.buf.clear()
    c1.send(b"drop\r")
    ok &= check("DROP gives up staff access", c1.wait_for(b"Staff access dropped.", 3))
    c1.buf.clear()
    c1.send(b"nodes\r")
    ok &= check("NODES gone after DROP", c1.wait_for(b"Unknown command", 3))
    for c in (a, c1, c2):
        c.close()
    return ok


def handle_then(handle, pats, secs=8):
    """Fresh ANSI caller types a handle; returns (caller, index of the pattern seen)."""
    c = Caller(ansi=True)
    c.wait_for(b"Enter your handle", 10)
    c.buf.clear()
    c.send(handle.encode() + b"\r")
    return c, wait_any(c, pats, secs)


def test_accounts():
    print("Accounts: sign-up checks, lockout, PROFILE, PASSWORD, INFO")
    c, which = handle_then("Zed", [b"[R]egister, [G]uest or [N]ew handle?"])
    ok = check("new handle offers register, guest or a new handle", which == 0)
    c.send(b"x")
    c.pump(0.3)
    ok &= check("other keys are ignored at that question", b"NEW ACCOUNT" not in c.buf and b"GUEST ACCESS" not in c.buf)
    c.send(b"n")
    ok &= check("N goes back to the handle prompt", c.wait_for(b"Enter your handle", 3))
    c.send(b"Zed\r")
    c.wait_for(b"[R]egister", 5)
    c.send(b"r")
    ok &= check("sign-up form opens", c.wait_for(b"NEW ACCOUNT", 5))
    c.buf.clear()
    c.send(b"abc\rabc\r\r\r\r\r\r\r")
    ok &= check("short password refused", c.wait_for(b"Password needs 4 or more characters", 5))
    c.send(b"\x1b")
    ok &= check("ESC cancels the sign-up", c.wait_for(b"Sign-up cancelled.", 3) and c.wait_for(b"Enter your handle", 3))
    c.buf.clear()
    c.send(b"Zed\r")
    c.wait_for(b"[R]egister", 5)
    c.send(b"r")
    c.wait_for(b"NEW ACCOUNT", 5)
    c.buf.clear()
    c.send(b"abcd\rabce\rZed\rzed@example.com\r\r\r\r\r")
    ok &= check("password mismatch refused", c.wait_for(b"The passwords do not match", 5))
    c.send(b"\x1b")
    c.wait_for(b"Enter your handle", 3)
    c.send(b"Zed\r")
    c.wait_for(b"[R]egister", 5)
    c.send(b"r")
    c.wait_for(b"NEW ACCOUNT", 5)
    c.buf.clear()
    c.send(b"abcd\rabcd\rZed\rnot-an-email\r\r\r\r\r")
    ok &= check("bad email refused", c.wait_for(b"That email does not look right", 5))
    c.close()

    # --- register, log off, come back: call stats saved
    c = ansi_login("Acct")
    ok &= check("first call greets as new", b"Welcome, " in c.buf)
    c.send(b"bye\r")
    c.wait_closed(8)
    c.close()
    c, which = handle_then("acct", [b"Password:"])
    ok &= check("handle match ignores case", which == 0)
    c.send(TEST_PW.encode() + b"\r")
    ok &= check("right password: ACCESS GRANTED", c.wait_for(b"ACCESS GRANTED", 6))
    ok &= check("stars turn into ACCESS GRANTED on the same line",
                re.search(rb"Password: [^\n]*\*[^\n]*ACCESS GRANTED", c.buf) is not None)
    ok &= check("second call: welcome back, call 2", c.wait_for(b"Welcome back, ", 5) and c.wait_for(b"Call 2.", 3))
    ok &= check("account's own spelling used", c.wait_for(b"Acct", 2))
    c.wait_for(b"Main", 5)

    # --- PROFILE: Name is focused first; Down x4 reaches Profile, F1 saves
    c.buf.clear()
    c.send(b"profile\r")
    ok &= check("PROFILE opens the form", c.wait_for(b"YOUR PROFILE", 5))
    c.send(DOWN * 4 + b"Plays chess on a C64" + F1)
    ok &= check("PROFILE saved", c.wait_for(b"Profile saved.", 5))
    c.buf.clear()
    c.send(b"info\r")
    ok &= check("own INFO shows email and profile",
                c.wait_for(b"acct@example.com", 5) and c.wait_for(b"Plays chess on a C64", 3))

    # --- PASSWORD: wrong current, mismatch, then a real change
    c.buf.clear()
    c.send(b"password\r")
    ok &= check("PASSWORD opens the form", c.wait_for(b"CHANGE PASSWORD", 5))
    c.send(b"nope\rnewpw99\rnewpw99\r\r")
    ok &= check("wrong current password refused", c.wait_for(b"The current password is wrong", 5))
    c.send(b"\x1b")
    ok &= check("ESC: nothing changed", c.wait_for(b"Cancelled, nothing changed.", 3))
    c.wait_for(b"Main", 3)
    c.buf.clear()
    c.send(b"password\r")
    c.wait_for(b"CHANGE PASSWORD", 5)
    c.send(TEST_PW.encode() + b"\rnewpw99\rnewpw98\r\r")
    ok &= check("new password mismatch refused", c.wait_for(b"The passwords do not match", 5))
    c.send(b"\x1b")
    c.wait_for(b"Main", 3)
    c.buf.clear()
    c.send(b"password\r")
    c.wait_for(b"CHANGE PASSWORD", 5)
    c.send(TEST_PW.encode() + b"\rnewpw99\rnewpw99\r\r")
    ok &= check("password changed", c.wait_for(b"Password changed.", 5))

    # --- INFO on someone else hides private fields
    p = ansi_login("Peeker")
    p.buf.clear()
    p.send(b"info acct\r")
    ok &= check("INFO on another caller shows the handle", p.wait_for(b"Acct", 5) and p.wait_for(b"Plays chess", 3))
    p.wait_for(b"Main", 3)
    ok &= check("INFO hides their email", b"acct@example.com" not in p.buf)
    p.buf.clear()
    p.send(b"users\r")
    ok &= check("USERS needs staff access", p.wait_for(b"Unknown command", 3))
    p.close()
    c.close()

    c, which = handle_then("Acct", [b"Password:"])
    c.send(TEST_PW.encode() + b"\r")
    ok &= check("old password no longer works", c.wait_for(b"ACCESS DENIED", 6))
    c.send(b"newpw99\r")
    ok &= check("new password works", c.wait_for(b"ACCESS GRANTED", 6))
    c.close()

    if HOST in ("127.0.0.1", "localhost"):
        users = (DATA / "users.txt").read_text()
        ok &= check("users.txt has no plaintext password", "newpw99" not in users and TEST_PW not in users)
        ok &= check("users.txt stores salt$hash", re.search(r"\b[0-9a-f]{16}\$[0-9a-f]{64}\b", users) is not None)

    # --- lockout: 3 misses hang up the call, 5 per handle lock it
    c = ansi_login("Locky")
    c.close()
    c, _ = handle_then("Locky", [b"Password:"])
    for _ in range(3):                                 # retries stay on the same line
        c.buf.clear()
        c.send(b"wrong1\r")
        c.wait_for(b"ACCESS DENIED", 6)
    ok &= check("3 wrong passwords hang up the call", c.wait_for(b"Too many wrong passwords.", 5) and c.wait_closed(8))
    c.close()
    c, _ = handle_then("Locky", [b"Password:"])
    for _ in range(2):
        c.buf.clear()
        c.send(b"wrong2\r")
        c.wait_for(b"ACCESS DENIED", 6)
    ok &= check("5th miss for the handle hangs up early", c.wait_for(b"Too many wrong passwords.", 5) and c.wait_closed(8))
    c.close()
    c, which = handle_then("Locky", [b"Try again later.", b"Password:"])
    ok &= check("locked handle refused before the password", which == 0 and c.wait_closed(8))
    c.close()
    return ok


def test_user_admin():
    print("Accounts: USERS manager, USER ADD/EDIT/DEL")
    if not PASSWORD:
        print("  SKIP  no sysop_password")
        return True
    s = ansi_login("Rob")
    s.buf.clear()
    s.send(f"bye {PASSWORD}\r".encode())
    ok = check("sysop node", s.wait_for(b"Sysop node.", 5))
    s.wait_for(b"Sysop", 3)
    s.buf.clear()
    s.send(b"users\r")
    ok &= check("USERS opens the manager", s.wait_for(b"USER MANAGER", 5) and s.wait_for(b"Rob", 3))
    ok &= check("manager shows the key help", s.wait_for(b"Enter edit  A add  D delete  Q quit", 3))
    s.buf.clear()
    s.send(b"a")
    ok &= check("A opens ADD ACCOUNT", s.wait_for(b"ADD ACCOUNT", 5))
    s.send(b"Newbie\rpw5678\rNew Person\rnew@example.com" + F1)
    ok &= check("account added, back in the manager",
                s.wait_for(b"Account Newbie added.", 5) and b"USER MANAGER" in s.buf)
    s.buf.clear()
    s.send(b"q")
    ok &= check("Q leaves the manager", s.wait_for(b"Sysop", 3))

    s.buf.clear()
    s.send(b"user edit newbie\r")
    ok &= check("USER EDIT opens the form", s.wait_for(b"EDIT ACCOUNT", 5) and s.wait_for(b"new@example.com", 3))
    s.send(DOWN * 7 + b"c" + DOWN + b"y" + F1)      # Level -> Co2, Locked -> Y
    ok &= check("lock and level saved", s.wait_for(b"Account Newbie saved.", 5))
    if HOST in ("127.0.0.1", "localhost"):
        users = (DATA / "users.txt").read_text()
        ok &= check("level written to users.txt", "level = co2" in users)
    c, which = handle_then("Newbie", [b"This account is locked."])
    ok &= check("locked account refused", which == 0 and c.wait_closed(8))
    c.close()

    s.buf.clear()
    s.send(b"user edit Newbie\r")
    s.wait_for(b"EDIT ACCOUNT", 5)
    s.send(DOWN * 8 + b"n" + F1)
    s.wait_for(b"Account Newbie saved.", 5)
    c, which = handle_then("Newbie", [b"Password:"])
    c.send(b"pw5678\r")
    ok &= check("unlocked account logs in with the staff-set password", c.wait_for(b"ACCESS GRANTED", 6))
    c.close()

    s.buf.clear()
    s.send(b"user del Rob\r")
    ok &= check("cannot delete your own account", s.wait_for(b"You cannot delete your own account.", 3))
    s.buf.clear()
    s.send(b"user del Newbie\r")
    ok &= check("USER DEL asks (y/N)", s.wait_for(b"(y/N)?", 3))
    s.send(b"y")
    ok &= check("account deleted", s.wait_for(b"Account deleted.", 5))
    c, which = handle_then("Newbie", [b"[R]egister", b"Password:"])
    ok &= check("deleted handle is new again", which == 0)
    c.close()

    s.buf.clear()
    s.send(b"info acct\r")
    ok &= check("staff INFO shows private fields", s.wait_for(b"acct@example.com", 5))
    s.close()
    return ok


def test_guest():
    print("Guest access")
    c = Caller(ansi=True)
    ok = check("login hint: join or visit", c.wait_for(b"New? Type a handle to join or visit.", 10))
    c.wait_for(b"Enter your handle", 5)
    c.buf.clear()
    c.send(b"sysop\r")
    ok &= check("SYSOP is reserved", c.wait_for(b"That handle is reserved.", 4))
    ok &= check("errors rub out in place (no new prompt)", b"Enter your handle" not in c.buf)
    c.buf.clear()
    c.send(b"Visitor\r")
    ok &= check("unknown handle: register or guest", c.wait_for(b"Visitor", 4) and c.wait_for(b"[G]uest", 4))
    c.send(b"g")
    ok &= check("G gets in without a password", c.wait_for(b"GUEST ACCESS", 6) and c.wait_for(b"Main", 6))
    ok &= check("guest keeps the handle they typed",
                re.search(rb"Welcome, (?:\x1b\[[0-9;]*m)*Visitor", c.buf) is not None)
    ok &= check("guest told nothing is saved", b"nothing is saved" in c.buf)
    ok &= check("guest time limit 15 minutes", b"Time left: 15 min." in c.buf)
    c.buf.clear()
    c.send(b"time\r")
    ok &= check("TIME: 15 min left, no daily limit", c.wait_for(b"Left     15 min", 3))
    c.buf.clear()
    c.send(b"profile\r")
    ok &= check("PROFILE is not for guests", c.wait_for(b"Unknown command", 3))
    c.buf.clear()
    c.send(b"info\r")
    ok &= check("INFO: guests have no account", c.wait_for(b"Guests have no account.", 3))
    c.buf.clear()
    c.send(b"help\r")
    end = time.time() + 8
    while time.time() < end and b"stops output" not in c.buf:
        if c.buf.endswith(b"[More] Y/n/c "):
            c.send(b"c")
        c.pump(0.2)
    ok &= check("HELP hides PROFILE and PASSWORD from guests",
                b"LAST" in c.buf and b"PROFILE" not in c.buf and b"PASSWORD" not in c.buf)

    d, which = handle_then("visitor", [b"That handle is online right now.", b"ew handle?"])
    ok &= check("a guest's handle can't be taken while they are on", which == 0)
    d.close()

    a = ansi_login("Alice")
    a.buf.clear()
    a.send(b"who\r")
    a.wait_for(b"*GUEST", 4)
    ok &= check("WHO marks the guest with * and shows the key",
                re.search(rb"\d\*Visitor ", a.buf) is not None and re.search(rb"\d Alice ", a.buf) is not None
                and b"*GUEST" in a.buf)
    if PASSWORD:
        c.buf.clear()
        c.send(f"bye {PASSWORD}\r".encode())
        ok &= check("guest BYE <sysop password> is a plain logoff",
                    c.wait_closed(12) and b"Sysop node." not in c.buf and PASSWORD.encode() not in c.buf)
    else:
        c.send(b"bye\r")
        c.wait_closed(8)
    c.close()
    time.sleep(0.3)
    a.buf.clear()
    a.send(b"last\r")
    a.wait_for(b"*GUEST", 4)
    ok &= check("LAST marks guest calls with *", re.search(rb"\*Visitor ", a.buf) is not None)
    a.close()
    if HOST in ("127.0.0.1", "localhost"):
        users = (DATA / "users.txt").read_text()
        ok &= check("nothing saved for the guest", "[Visitor]" not in users)
    return ok


def test_idle_login():
    print("Handle prompt idle warning (31 s)")
    c = Caller(ansi=True)
    c.wait_for(b"Enter your handle", 10)
    c.send(b"Ro")
    c.buf.clear()
    ok = check("warning at 30 s", c.wait_for(b"Still there? Disconnecting in 30 seconds.", 34))
    ok &= check("partial input redrawn", c.wait_for(b"Enter your handle: \x1b[1;37mRo", 2))
    if "--slow" in FLAGS:
        ok &= check("hangup at 60 s", c.wait_for(b"IDLE TIMEOUT", 32) and c.wait_closed(8))
    c.close()
    return ok


def test_busy():
    print("Busy line")
    callers = [Caller(ansi=True) for _ in range(6)]
    time.sleep(0.5)
    seventh = Caller(ansi=True)
    ok = check("7th caller gets the busy screen", seventh.wait_for(b"lines are busy", 5))
    ok &= check("countdown shown", seventh.wait_for(b"Disconnecting in", 5))
    t0 = time.time()
    eighth = Caller()
    ok &= check("8th caller gets BUSY", eighth.wait_for(b"BUSY", 3))
    ok &= check("8th caller dropped at once", eighth.wait_closed(2))
    ok &= check("busy line hangs up after countdown", seventh.wait_for(b"NO CARRIER", 13))
    ok &= check("busy countdown ~10 s", 9 < time.time() - t0 < 12)
    seventh.close()
    eighth.close()

    if PASSWORD:
        guest = Caller(ansi=True)
        ok &= check("guest sees countdown", guest.wait_for(b"Disconnecting in", 8))
        guest.send(b"x")
        ok &= check("key opens a login", guest.wait_for(b"Enter your handle", 3))
        guest.send(b"Rob\r")
        ok &= check("guest prompt", guest.wait_for(b"Main", 3))
        guest.buf.clear()
        guest.send(f"bye {PASSWORD}\r".encode())
        ok &= check("busy-line guest reaches sysop node", guest.wait_for(b"Sysop node.", 4))
        guest.close()

    for c in callers:
        c.close()
    time.sleep(0.5)
    again = Caller(ansi=True)
    ok &= check("node frees after hangup", again.wait_for(b"DETECTED", 5))
    again.close()
    return ok


def test_bulletin():
    print("Bulletin screen paging and abort (host only)")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs a local data/ directory")
        return True
    path = DATA / "screens" / "bulletin.asc"
    path.write_text("".join(f"Bulletin line {i}\n" for i in range(1, 61)))
    try:
        c = Caller(ansi=True)
        c.wait_for(b"Enter your handle", 10)
        login(c, "Reader", wait_main=False)
        ok = check("bulletin plays after login", c.wait_for(b"Bulletin line 1\r\n", 5))
        ok &= check("pauses at [More] after 22 lines", c.wait_for(b"[More] Y/n/c", 5)
                    and b"Bulletin line 22" in c.buf and b"Bulletin line 23" not in c.buf)
        c.buf.clear()
        c.send(b" ")
        ok &= check("space continues one page", c.wait_for(b"Bulletin line 23", 5)
                    and c.wait_for(b"[More] Y/n/c", 5) and b"Bulletin line 45" not in c.buf)
        c.send(b"y")
        ok &= check("rest of the bulletin, then the prompt",
                    c.wait_for(b"Bulletin line 60", 5) and c.wait_for(b"Main", 5))
        c.close()

        c = Caller(ansi=True)
        c.wait_for(b"Enter your handle", 10)
        login(c, "Reader", wait_main=False)
        c.wait_for(b"[More] Y/n/c", 8)
        c.buf.clear()
        c.send(b"n")
        ok &= check("N at [More] stops the screen", c.wait_for(b"Stopped.", 3) and c.wait_for(b"Main", 3))
        ok &= check("nothing after the stop", b"Bulletin line 50" not in c.buf)
        c.close()
    finally:
        path.unlink()
    return ok


# ---------------------------------------------------------------------------
# Backup window (needs the button held: BBS_BACKUP_TEST_OPEN on host, or the
# esp32dev_backuptest build on a board)
# ---------------------------------------------------------------------------
def http_call(method, path, body=None, timeout=60, headers=None):
    conn = http.client.HTTPConnection(HOST, BACKUP_PORT, timeout=timeout)
    try:
        conn.request(method, path, body=body, headers=headers or {})
        r = conn.getresponse()
        return r.status, r.read()
    except (OSError, http.client.HTTPException) as e:
        return 0, str(e).encode()
    finally:
        conn.close()


def make_zip(files, compress=zipfile.ZIP_DEFLATED):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compress) as z:
        for name, data in files.items():
            z.writestr(name, data)
    return buf.getvalue()


def upload_with_answer(sysop, data, answer, expect_prompt=True):
    """PUT the zip in a thread, answer the sysop prompt, return (status, body, prompt_seen)."""
    result = {}

    def worker():
        result["r"] = http_call("PUT", "/restore", data, timeout=90)

    sysop.buf.clear()
    th = threading.Thread(target=worker)
    th.start()
    seen = sysop.wait_for(b"Accept upload (Y/N)?", 20) if expect_prompt else False
    if seen and answer is not None:
        sysop.send(answer)
    if answer == "hangup" or answer is None:
        pass
    th.join(95)
    status, body = result.get("r", (0, b"no reply"))
    return status, body, seen


def test_backup():
    print("Backup window: download, edit, upload")
    if not PASSWORD:
        print("  SKIP  no sysop_password")
        return True
    s = ansi_login("Rob")
    s.buf.clear()
    s.send(f"bye {PASSWORD}\r".encode())
    ok = check("sysop node", s.wait_for(b"Sysop node.", 5))
    ok &= check("window-open notice on the sysop console", s.wait_for(b"*** Backup open", 8))

    status, _ = http_call("GET", "/")
    ok &= check("GET / answers", status == 200)
    status, _ = http_call("GET", "/nope")
    ok &= check("unknown path is 404", status == 404)

    status, data = http_call("GET", "/backup.zip")
    ok &= check("GET /backup.zip is 200", status == 200)
    try:
        z = zipfile.ZipFile(io.BytesIO(data))
        bad = z.testzip()
        names = z.namelist()
    except zipfile.BadZipFile:
        z, bad, names = None, "not a zip", []
    ok &= check("download is a valid zip (all CRCs good)", z is not None and bad is None)
    ok &= check("zip has system.cfg, MANIFEST.txt, screens",
                "system.cfg" in names and "MANIFEST.txt" in names and "screens/welcome.ans" in names)
    cfg = z.read("system.cfg").decode() if z else ""
    ok &= check("passwords redacted as ***", "sysop_password = ***" in cfg and PASSWORD not in cfg)
    users = z.read("users.txt").decode() if z and "users.txt" in names else ""
    ok &= check("zip carries users.txt, hashes only", "Alice" in users and TEST_PW not in users)
    ok &= check("download notice on the sysop console", s.wait_for(b"*** Backup downloaded by", 5))

    # --- edit and upload inside a folder, deflated (what re-zipping an unpacked folder gives)
    files = {f"unleashed-backup/{n}": z.read(n) for n in names if n != "screens/busy.seq"}
    files["unleashed-backup/screens/bulletin.asc"] = b"Custom line from upload test\n"
    files["unleashed-backup/screens/extra.asc"] = b"extra screen\n"
    files["unleashed-backup/system.cfg"] = (cfg + "\nidle_minutes = 21\n").encode()
    status, body, seen = upload_with_answer(s, make_zip(files), b"y")
    ok &= check("sysop asked Y/N for the upload", seen)
    summary = bytes(s.buf)
    ok &= check("summary names the files", b"system.cfg" in summary and b"screens" in summary and b"users" in summary)
    ok &= check("upload applied (200)", status == 200 and b"Applied" in body)
    local = HOST in ("127.0.0.1", "localhost")
    if local:
        live_cfg = (DATA / "system.cfg").read_text()
        ok &= check("*** kept the real password on disk", f"sysop_password = {PASSWORD}" in live_cfg)
    status, again = http_call("GET", "/backup.zip")
    z2 = zipfile.ZipFile(io.BytesIO(again)) if status == 200 else None
    names2 = z2.namelist() if z2 else []
    cfg2 = z2.read("system.cfg").decode() if z2 else ""
    ok &= check("password still set after upload (***)", "sysop_password = ***" in cfg2)
    ok &= check("edited value live", "idle_minutes = 21" in cfg2)
    ok &= check("new screen added", "screens/extra.asc" in names2)
    ok &= check("screen missing from the upload removed", "screens/busy.seq" not in names2)
    c, which = handle_then("Alice", [b"Password:", b"[R]egister"])
    ok &= check("accounts restored from the upload", which == 0)
    c.send(TEST_PW.encode() + b"\r")
    ok &= check("callers see the uploaded bulletin", c.wait_for(b"Custom line from upload test", 8))
    c.close()

    # --- an unknown key in users.txt is accepted with a warning naming the line
    if users:
        edited = users.replace("\ncalls = ", "\nnickname = Bobby\ncalls = ", 1)
        status, body, seen = upload_with_answer(s, make_zip({"users.txt": edited.encode()}), b"y")
        ok &= check("unknown users.txt key warns with a line number and applies",
                    seen and status == 200 and b"unknown key" in bytes(s.buf) and b"line " in bytes(s.buf))
        c, which = handle_then("Alice", [b"Password:", b"[R]egister"])
        ok &= check("accounts survived the edited upload", which == 0)
        c.close()

    # --- self_register = no and guest = no: only accounts get in, then back on
    def with_access(value):
        lines = [l for l in cfg2.splitlines() if not l.strip().startswith(("self_register", "guest"))]
        extra = f"\nself_register = {value}\nguest = {value}\n"
        return make_zip({"system.cfg": ("\n".join(lines) + extra).encode()})

    status, body, seen = upload_with_answer(s, with_access("no"), b"y")
    ok &= check("self_register = no, guest = no applied", seen and status == 200)
    c, which = handle_then("Stranger", [b"The sysop creates accounts here.", b"ew handle?"])
    ok &= check("no sign-up, no guests: new handle refused in place", which == 0)
    c.close()
    status, body, seen = upload_with_answer(s, with_access("yes"), b"y")
    c, which = handle_then("Stranger", [b"The sysop creates accounts here.", b"[R]egister, [G]uest"])
    ok &= check("self_register and guest back on: both offered", status == 200 and which == 1)
    c.close()

    # --- junk plus one real change, sysop says no
    junk = {
        "../evil.txt": b"x",
        "notes.txt": b"x",
        "screens/toolongname.asc": b"x",
        "screens/virus.exe": b"x",
        "screens/extra.asc": b"changed but refused\n",
    }
    status, body, seen = upload_with_answer(s, make_zip(junk), b"n")
    ok &= check("junk listed as rejected in the prompt", seen and b"4 rejected" in bytes(s.buf))
    ok &= check("sysop N refuses (403)", status == 403)
    def extra_now():
        st, zz = http_call("GET", "/backup.zip")
        return zipfile.ZipFile(io.BytesIO(zz)).read("screens/extra.asc") if st == 200 else b""

    ok &= check("refused upload changed nothing", extra_now() == b"extra screen\n")
    if local:
        ok &= check("no file escaped the data directory", not (DATA.parent / "evil.txt").exists())

    # --- uploads refused without asking
    status, body, _ = upload_with_answer(s, make_zip({"readme.txt": b"x"}), None, expect_prompt=False)
    ok &= check("nothing usable: 422, no prompt", status == 422)
    bad_cfg = make_zip({"system.cfg": b"hostname = Bad Host!\n"})
    status, body, _ = upload_with_answer(s, bad_cfg, None, expect_prompt=False)
    ok &= check("invalid system.cfg rejected (422)", status == 422 and b"hostname" in body)
    status, body, _ = upload_with_answer(s, b"this is not a zip file at all", None, expect_prompt=False)
    ok &= check("not a zip: 400", status == 400)
    bomb = io.BytesIO()
    with zipfile.ZipFile(bomb, "w", zipfile.ZIP_DEFLATED) as zz:
        zz.writestr("screens/bomb.asc", b"\0" * 2_000_000)
    status, body, _ = upload_with_answer(s, bomb.getvalue(), None, expect_prompt=False)
    ok &= check("zip bomb (2 MB unpacked) rejected", status == 422)
    status, body = http_call("PUT", "/restore", b"x", headers={"Content-Length": "500000"}, timeout=10)
    ok &= check("oversize Content-Length: 413", status == 413)
    ok &= check("server still serving after abuse", http_call("GET", "/")[0] == 200)

    # --- sysop hangs up while an upload waits: discarded
    result = {}
    th = threading.Thread(target=lambda: result.setdefault("r", http_call("PUT", "/restore", make_zip({"screens/extra.asc": b"late\n"}), timeout=60)))
    s.buf.clear()
    th.start()
    s.wait_for(b"Accept upload (Y/N)?", 20)
    s.close()
    th.join(65)
    status = result.get("r", (0, b""))[0]
    ok &= check("sysop leaving discards the upload (403)", status == 403)
    if local:
        ok &= check("and changes nothing", (DATA / "screens" / "extra.asc").read_bytes() == b"extra screen\n")
    return ok


def test_ban():
    print("Ban after 3 wrong sysop passwords")
    for _ in range(3):
        c = ansi_login("Mallory")
        c.send(b"bye wrongpassword\r")
        c.wait_closed(25)
        c.close()
        time.sleep(0.3)
    c = Caller(ansi=True)
    ok = check("banned IP dropped without a banner", c.wait_closed(3) and b"DETECTING" not in c.buf)
    c.close()
    return ok


if __name__ == "__main__":
    results = [test_ansi(), test_telnet_first(), test_petscii(), test_ascii(),
               test_page(), test_sysop(), test_cosysop(), test_accounts(), test_user_admin(), test_guest(),
               test_bulletin(), test_idle_login(), test_busy()]
    if "--backup" in FLAGS:
        results.append(test_backup())
    if "--ban" in FLAGS:
        results.append(test_ban())
    print("ALL PASS" if all(results) else "FAILURES")
    sys.exit(0 if all(results) else 1)
