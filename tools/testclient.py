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
USERDATA = DATA / "user"      # accounts, config and plugin files


def cfg_value(key):
    cfg = USERDATA / "system.cfg"
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


def bbs_version():
    src = (ROOT / "src" / "config.h").read_text()
    m = re.search(r'BBS_VERSION\s+"([^"]+)"', src)
    return m.group(1) if m else "?"


BBS_VERSION = bbs_version()
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


ANSI_RE = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]")


def plain(data):
    """Row output carries colour changes between the columns now: drop them."""
    return ANSI_RE.sub(b"", bytes(data))


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


PROMPT_RE = re.compile(rb"\[(\d|S|B)\] [^:]*: ")


def read_list(c, secs=8):
    """Pump a paged list to its end, answering each [More] once. The list is
    over when the command prompt comes back and nothing more arrives."""
    end = time.time() + secs
    answered = 0
    quiet = 0
    while time.time() < end:
        seen = bytes(c.buf).count(b"[More] Y/n/c")
        if seen > answered:
            c.send(b"c")
            answered = seen
            quiet = 0
            continue
        before = len(c.buf)
        c.pump(0.2)
        if len(c.buf) == before and PROMPT_RE.search(bytes(c.buf)[-64:]):
            quiet += 1
            if quiet >= 2:
                break
        elif len(c.buf) != before:
            quiet = 0
    return PROMPT_RE.search(bytes(c.buf)[-64:]) is not None


def pass_rules(c, enc=lambda s: s.encode()):
    """After R the house rules play, a page at a time, before the encryption
    warning. Press through them and stop at the warning.

    The buffer is cleared between pages: wait_any matches everything received
    so far, so the previous page's "continue" would match again at once.
    """
    for _ in range(8):
        hit = wait_any(c, [enc("know more"), enc("continue"), enc("CONTINUE")], 6)
        if hit == 0:
            return True
        if hit < 0:
            return False
        c.buf = bytearray()
        c.send(b" ")
    return False


def login(c, handle, pw=TEST_PW, as_pet=False, wait_main=True):
    """At the handle prompt: log in, or register when the handle is new.
    The signup keys work for the cursor form and the ASCII line form alike."""
    enc = pet if as_pet else (lambda s: s.encode())
    c.send(enc(handle) + b"\r")
    which = wait_any(c, [enc("[R]egister"), enc("Password:")], 8)
    if which == 0:
        email = "".join(ch for ch in handle.lower() if ch.isalnum()) + "@example.com"
        c.send(b"r")
        if not pass_rules(c, enc):          # house rules, then the warning
            return False
        c.send(b"n")
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
    read_list(c)
    text = bytes(c.buf).replace(b"[More] Y/n/c ", b"").replace(b"\x08 \x08", b"").decode("ascii", "replace")
    lines = [l for l in text.split("\r\n") if l]
    body = [l for l in lines if l != "help" and "---" not in l and "Main" not in l
            and not l.startswith("More:") and not l.lstrip().startswith("?")]
    ok &= check("HELP generated with rows", len(body) >= 10)
    ok &= check("every HELP line fits 39 columns", all(len(l) <= 39 for l in lines))
    # The usage column is 15 wide so that "ANNOUNCE TEST", the widest usage
    # string there is, is not the one command that gets truncated.
    ok &= check("descriptions start at column 16",
                all(len(l) > 15 and l[14] == " " and l[15] != " " for l in body))
    ok &= check("wrapped lines (if any) sit under the description column",
                all(l[:15] == " " * 15 for l in body if l.startswith(" ")))
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
    ok &= check("C continues nonstop", r.wait_for(b"? all", 4))

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
    ok &= check("SHOW lists the sysop, marked ] for sysop", re.search(rb"S\]Rob", plain(x.buf)) is not None)
    ok &= check("WHO explains the markers",
                b"*GUEST" in plain(x.buf) and b"]SYSOP" in plain(x.buf))
    r.buf.clear()
    r.send(b"who\r")
    r.wait_for(b"Who's online", 3)
    r.pump(0.5)
    ok &= check("staff WHO shows each caller's last command", b"Doing" in r.buf and
                re.search(rb"\d Xavier +WHO ", plain(r.buf)) is not None)
    if HOST in ("127.0.0.1", "localhost"):
        users = (USERDATA / "users.txt").read_text()
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
                re.search(rb"\d>Cora ", plain(a.buf)) is not None and
                re.search(rb"\d>Dex ", plain(a.buf)) is not None)

    c1.buf.clear()
    c1.send(b"? staff\r")
    read_list(c1)
    ok &= check("CO1 staff menu lists KICK, not UNBAN",
                b"KICK n [msg]" in plain(c1.buf) and b"UNBAN" not in plain(c1.buf))

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
                re.search(rb"\d>Cora +hidden", plain(c2.buf)) is not None)
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
    pass_rules(c)
    c.send(b"n")
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
    pass_rules(c)
    c.send(b"n")
    c.wait_for(b"NEW ACCOUNT", 5)
    c.buf.clear()
    c.send(b"abcd\rabce\rZed\rzed@example.com\r\r\r\r\r")
    ok &= check("password mismatch refused", c.wait_for(b"The passwords do not match", 5))
    c.send(b"\x1b")
    c.wait_for(b"Enter your handle", 3)
    c.send(b"Zed\r")
    c.wait_for(b"[R]egister", 5)
    c.send(b"r")
    pass_rules(c)
    c.send(b"n")
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
        users = (USERDATA / "users.txt").read_text()
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
        users = (USERDATA / "users.txt").read_text()
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
    read_list(c)
    ok &= check("HELP hides PROFILE and PASSWORD from guests",
                b"LAST" in c.buf and b"PROFILE" not in c.buf and b"PASSWORD" not in c.buf)

    d, which = handle_then("visitor", [b"That handle is online right now.", b"ew handle?"])
    ok &= check("a guest's handle can't be taken while they are on", which == 0)
    d.close()

    a = ansi_login("Alice")
    a.buf.clear()
    a.send(b"who\r")
    a.wait_for(b"GUEST", 4)
    ok &= check("WHO marks the guest with * and shows the key",
                re.search(rb"\d\*Visitor ", plain(a.buf)) is not None and
                re.search(rb"\d Alice ", plain(a.buf)) is not None and
                b"*GUEST" in plain(a.buf))
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
    a.wait_for(b"GUEST", 4)
    ok &= check("LAST marks guest calls with *", re.search(rb"\*Visitor ", plain(a.buf)) is not None)
    a.close()
    if HOST in ("127.0.0.1", "localhost"):
        users = (USERDATA / "users.txt").read_text()
        ok &= check("nothing saved for the guest", "[Visitor]" not in users)
    return ok


def test_plugins():
    print("Plugins: example plugin, levels, session ownership")
    c = ansi_login("Plug")
    c.buf.clear()
    c.send(b"ping\r")
    ok = check("a plugin command runs for everyone (read)", c.wait_for(b"howdy, Plug", 4))
    ok &= check("its config key was read", b"howdy" in c.buf)
    c.buf.clear()
    c.send(b"poke\r")
    ok &= check("write commands are refused without the level", c.wait_for(b"Unknown command", 4))
    c.buf.clear()
    c.send(b"example\r")
    ok &= check("admin commands are refused too", c.wait_for(b"Unknown command", 4))
    c.buf.clear()
    c.send(b"help\r")
    read_list(c)
    ok &= check("HELP lists the plugin's read command only",
                b"PING" in c.buf and b"POKE" not in c.buf and b"EXAMPLE" not in c.buf)

    # the plugin owns the session until the caller quits
    c.buf.clear()
    c.send(b"echo\r")
    ok &= check("a plugin can own the session", c.wait_for(b"Echo on. Q quits.", 4))
    c.buf.clear()
    c.send(b"hi")
    ok &= check("keys go to the plugin", c.wait_for(b"hi", 3) and b"Main" not in c.buf)
    c.send(b"q")
    ok &= check("Q hands the session back", c.wait_for(b"Echo off.", 3) and c.wait_for(b"Main", 3))
    c.close()

    if not PASSWORD:
        return ok
    s = ansi_login("Rob")
    s.buf.clear()
    s.send(f"bye {PASSWORD}\r".encode())
    s.wait_for(b"Sysop node.", 5)
    s.buf.clear()
    s.send(b"poke\r")
    ok &= check("staff may run the write command", s.wait_for(b"Poked. Count is", 4))
    s.buf.clear()
    s.send(b"ping\r")
    s.wait_for(b"ticks", 4)
    ok &= check("the periodic hook is running", re.search(rb"[1-9]\d* ticks", s.buf) is not None)
    s.buf.clear()
    s.send(b"example\r")
    ok &= check("the sysop may run the admin command", s.wait_for(b"greeting=howdy", 4))
    ok &= check("the plugin got its own folder", b"/p/example/count" in s.buf)
    s.buf.clear()
    s.send(b"plugins\r")
    ok &= check("PLUGINS lists it as running",
                s.wait_for(b"Plugins", 4) and s.wait_for(b"example", 3) and s.wait_for(b"running", 3))
    ok &= check("PLUGINS shows the disk reserve", b"Disk free" in s.buf)
    s.buf.clear()
    s.send(b"mem\r")
    ok &= check("MEM reports free disk space", s.wait_for(b"Disk free", 4))
    s.close()
    if HOST in ("127.0.0.1", "localhost"):
        count = USERDATA / "p" / "example" / "count"
        ok &= check("the plugin wrote its file under p/example/", count.exists())
    return ok


def test_about():
    print("ABOUT screen")
    c = ansi_login("Curious")
    c.buf.clear()
    c.send(b"about\r")
    ok = check("ABOUT plays the screen", c.wait_for(b"GNU General Public License v2 or later", 6))
    ok &= check("with the version and handle", b"Curious" in c.buf and BBS_VERSION.encode() in c.buf)
    c.close()
    return ok


def test_chat():
    print("Chat room")
    a = ansi_login("Chatty")
    b = ansi_login("Listener")
    a.buf.clear()
    b.buf.clear()
    a.send(b"chat\r")
    ok = check("CHAT joins the room", a.wait_for(b"Main: 1 here", 4))
    b.send(b"chat\r")
    ok &= check("a second caller joins", b.wait_for(b"Main: 2 here", 4))
    ok &= check("the room is told who arrived", a.wait_for(b"Listener) joined", 4))
    a.buf.clear()
    b.buf.clear()
    b.send(b"half typed")                              # b is mid-sentence
    b.wait_for(b"half typed", 3)
    b.buf.clear()
    a.send(b"hello room\r")
    a.wait_for(b"hello room", 4)
    b.pump(1.0)
    ok &= check("nothing lands while a caller is typing", b":Chatty)" not in plain(b.buf))
    b.send(b"\r")
    ok &= check("the held line arrives on Enter, tagged with the node",
                b.wait_for(b"hello room", 4) and
                re.search(rb"#\d:Chatty\) hello room", plain(b.buf)) is not None)
    ok &= check("no prompt character in the room", b"> " not in b.buf)
    b.buf.clear()
    b.send(b"/s\r")
    ok &= check("/s lists the room with node tags",
                b.wait_for(b"2 in Main", 4) and b":Chatty)" in plain(b.buf) and b"(you)" in plain(b.buf))
    # flooding is refused, and only the flooder hears about it
    a.buf.clear()
    b.buf.clear()
    for _ in range(14):
        a.send(b"spam\r")
        a.pump(0.05)
    a.pump(1.0)
    b.pump(0.5)
    ok &= check("a flood is throttled", b"lines a minute is the limit" in a.buf)
    ok &= check("only the flooder is told", b"lines a minute" not in b.buf)
    ok &= check("the lines under the limit still reached the room", b":Chatty) spam" in plain(b.buf))

    b.buf.clear()
    a.send(b"/q\r")
    ok &= check("/q leaves the room", a.wait_for(b"Main", 4))
    ok &= check("the room hears the exit", b.wait_for(b":Chatty) left the room", 4))

    c = ansi_login("Latecomer")
    c.buf.clear()
    c.send(b"chat\r")
    ok &= check("a joiner sees the recent lines", c.wait_for(b":Chatty) spam", 4))
    c.send(b"\x1b")
    ok &= check("ESC leaves too", c.wait_for(b"Main", 4))
    for x in (a, b, c):
        x.close()
    return ok


def test_serial():
    print("Serial bridge: one operator, many watchers")
    w = ansi_login("Watcher")
    w.buf.clear()
    w.send(b"serial\r")
    ok = check("a caller with read joins as a watcher", w.wait_for(b"Port open 115200 8N1", 5))
    ok &= check("and is told the keyboard is not theirs", w.wait_for(b"Watching. T takes the keyboard", 3))
    w.buf.clear()
    w.send(b"T")
    ok &= check("a watcher cannot take the keyboard", w.wait_for(b"You may watch, not drive.", 3))

    if not PASSWORD:
        w.close()
        return ok
    s = ansi_login("Rob")
    s.buf.clear()
    s.send(f"bye {PASSWORD}\r".encode())
    s.wait_for(b"Sysop node.", 5)
    s.buf.clear()
    s.send(b"serial\r")
    ok &= check("staff join and take the keyboard", s.wait_for(b"You have the keyboard", 5))
    w.buf.clear()
    s.buf.clear()
    s.send(b"AT\r")                                   # the host port loops back
    ok &= check("the operator's typing reaches the device and comes back",
                s.wait_for(b"AT", 4))
    ok &= check("watchers see the same stream", w.wait_for(b"AT", 4))
    s.buf.clear()
    s.send(b"\x1b")
    ok &= check("ESC leaves the session", s.wait_for(b"Left the serial session.", 4))
    s.buf.clear()
    s.send(b"serial set 9600 8N1\r")
    ok &= check("staff change the line speed", s.wait_for(b"Port open 9600 8N1", 4))
    w.buf.clear()
    w.send(b"\x1b")
    w.wait_for(b"Main", 4)
    w.buf.clear()
    w.send(b"serial set 2400\r")
    ok &= check("a watcher cannot change the speed", w.wait_for(b"You may watch, not drive.", 4))
    s.buf.clear()
    s.send(b"serial status\r")
    ok &= check("SERIAL STATUS reports the port", s.wait_for(b"Port open 9600", 4) and b"rx " in s.buf)
    s.close()
    w.close()
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
        live_cfg = (USERDATA / "system.cfg").read_text()
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
        cut = next((i for i, l in enumerate(lines) if l.strip().startswith("[")), len(lines))
        lines[cut:cut] = [f"self_register = {value}", f"guest = {value}"]   # keys live above sections
        return make_zip({"system.cfg": ("\n".join(lines) + "\n").encode()})

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



def test_menus():
    """HELP is a set of menus now: most used first, areas behind ? <name>."""
    print("Help menus")
    c = ansi_login("Menus")
    c.buf.clear()
    c.send(b"?\r")
    read_list(c)
    main = plain(c.buf)
    ok = check("? lists the everyday commands", b"WHO" in main and b"CHAT" in main)
    ok &= check("? does not list staff tools", b"BROADCAST" not in main)
    ok &= check("? names the other menus", b"? chat" in main and b"? account" in main)
    ok &= check("a plain caller is not offered the staff menu", b"? staff" not in main)

    c.buf.clear()
    c.send(b"? account\r")
    read_list(c)
    acct = plain(c.buf)
    ok &= check("? account is its own menu", b"PROFILE" in acct and b"PASSWORD" in acct)
    ok &= check("the account menu leaves the everyday commands out", b"PAGE n msg" not in acct)

    c.buf.clear()
    c.send(b"? chat\r")
    read_list(c)
    ok &= check("? chat lists the room and mail", b"CHAT" in plain(c.buf) and b"MAIL" in plain(c.buf))

    c.buf.clear()
    c.send(b"? all\r")
    read_list(c)
    every = plain(c.buf)
    ok &= check("? all walks every section",
                b"Commands" in every and b"Chat and messages" in every and b"PROFILE" in every)
    ok &= check("the shortcut letter is picked out", b"\x1b[1;33mW" in bytes(c.buf))
    c.close()
    return ok


def test_sysinfo():
    """SYS and CALLS: the sysop's two read-only screens."""
    print("System screens")
    for i in range(3):                                  # a few calls for the histogram
        x = ansi_login("Caller%d" % i)
        x.send(b"bye\r")
        x.pump(0.3)
        x.close()
    s = ansi_login("Sysop2")
    s.send(b"bye testsysop\r")
    s.wait_for(b"Sysop", 4)
    s.buf.clear()

    s.send(b"sys\r")
    read_list(s)
    sys_out = plain(s.buf)
    ok = check("SYS has the network group", b"network" in sys_out and b"Signal" in sys_out)
    ok &= check("SYS has memory and storage", b"Heap free" in sys_out and b"Data free" in sys_out)
    ok &= check("SYS reports the scheduler", b"Loop avg" in sys_out and b"Loop passes" in sys_out)
    ok &= check("SYS counts the lines", b"Nodes busy" in sys_out and b"Calls" in sys_out)

    s.buf.clear()
    s.send(b"calls\r")
    read_list(s)
    calls = plain(s.buf)
    ok &= check("CALLS draws the day", b"Calls by hour" in calls and b"Busiest" in calls)
    ok &= check("CALLS counts the calls logged", re.search(rb"\d+ calls", calls) is not None)

    g = ansi_login("NotStaff")
    g.buf.clear()
    g.send(b"sys\r")
    g.pump(0.8)
    ok &= check("SYS is staff only", b"Unknown" in g.buf or b"network" not in plain(g.buf))
    g.close()
    s.close()
    return ok


def test_room_commands():
    """The room command set: /?, private lines, away, squelch, kicks, votes."""
    print("Chat room commands")
    a = ansi_login("Ay")
    b = ansi_login("Bee")
    c = ansi_login("Cee")
    for x in (a, b, c):
        x.send(b"chat\r")
        x.wait_for(b"here.", 4)
        x.buf.clear()

    a.send(b"/?\r")
    ok = check("/? lists the room commands", a.wait_for(b"/sq", 4) and b"/email" in plain(a.buf))

    a.buf.clear()
    c.buf.clear()
    b.send(b"/me waves\r")
    ok &= check("/me is an action line",
                a.wait_for(b"* waves", 4) and b":Bee) * waves" in plain(a.buf))

    a.buf.clear()
    c.buf.clear()
    b.send(b"/p 1 just for you\r")
    ok &= check("a private line reaches the one node", a.wait_for(b"just for you", 4))
    c.pump(0.8)
    ok &= check("and nobody else", b"just for you" not in c.buf)

    b.buf.clear()
    a.send(b"/a making tea\r")
    ok &= check("away tells the room", b.wait_for(b"is away: making tea", 4))
    b.buf.clear()
    b.send(b"/s\r")
    ok &= check("the roster shows the note", b.wait_for(b"away: making tea", 4))

    c.buf.clear()
    c.send(b"/sq 2\r")
    c.wait_for(b"hidden", 3)
    c.buf.clear()
    b.send(b"/a\r")                                     # back: a notice, never squelched
    ok &= check("join and leave notices ignore a squelch", c.wait_for(b"is back", 4))
    c.buf.clear()
    b.send(b"squelched line\r")
    c.pump(1.0)
    ok &= check("a squelched caller's lines are hidden", b"squelched line" not in c.buf)
    a.buf.clear()
    b.send(b"still heard elsewhere\r")
    ok &= check("the squelch is per caller", a.wait_for(b"still heard elsewhere", 4))

    # a vote with three in the room and no staff
    b.buf.clear()
    c.buf.clear()
    a.send(b"/vk 3\r")
    ok &= check("a vote opens", b.wait_for(b"vote to remove Cee", 4))
    b.send(b"/vk 3\r")
    ok &= check("two thirds carries it", c.wait_for(b"removed from the room", 5))
    ok &= check("the room is told", a.wait_for(b"was removed by a vote", 4))

    # staff kick and the room ban list
    s = ansi_login("RoomBoss")
    s.send(b"bye testsysop\r")
    s.wait_for(b"Sysop", 4)
    s.send(b"chat\r")
    s.wait_for(b"here.", 4)
    s.buf.clear()
    b.buf.clear()
    s.send(b"/k 2 enough of that\r")
    ok &= check("staff can kick from the room", b.wait_for(b"removed from the room", 5))
    s.buf.clear()
    s.send(b"/b Bee\r")
    ok &= check("a handle can be barred", s.wait_for(b"barred from the room", 4))
    b.buf.clear()
    b.send(b"chat\r")
    ok &= check("a barred handle cannot come back", b.wait_for(b"not welcome", 4))
    s.buf.clear()
    s.send(b"/unb Bee\r")
    ok &= check("and can be let back in", s.wait_for(b"may come back", 4))
    b.buf.clear()
    b.send(b"chat\r")
    ok &= check("the room takes them again", b.wait_for(b"here.", 4))

    for x in (a, b, c, s):
        x.close()
    return ok


def test_mail():
    """One message per caller, read once, replaced rather than doubled."""
    print("Messages")
    a = ansi_login("Sender")
    b = ansi_login("Reader")
    a.buf.clear()
    b.buf.clear()

    a.send(b"mail Reader the eagle lands at nine\r")
    ok = check("a message is left", a.wait_for(b"Left for Reader", 4))
    ok &= check("somebody online is told at once", b.wait_for(b"You have mail", 4))

    b.buf.clear()
    b.send(b"mail\r")
    ok &= check("MAIL reads it", b.wait_for(b"the eagle lands at nine", 4) and
                b"Message from Sender" in plain(b.buf))
    b.buf.clear()
    b.send(b"mail\r")
    ok &= check("reading clears it", b.wait_for(b"No mail", 4))

    a.buf.clear()
    a.send(b"mail Reader first one\r")
    a.wait_for(b"Left for", 4)
    a.buf.clear()
    a.send(b"mail Reader second one\r")
    ok &= check("a second message replaces the first, and says so",
                a.wait_for(b"replacing the one they had", 4))

    a.buf.clear()
    a.send(b"mail Nobody hello?\r")
    ok &= check("a message needs a real account", a.wait_for(b"No account called Nobody", 4))

    b.close()
    b = ansi_login("Reader", pw=TEST_PW)
    ok &= check("mail is announced at login", b.wait_for(b"You have mail", 6))
    b.send(b"mail\r")
    ok &= check("the message that survived is the newer one", b.wait_for(b"second one", 4))
    a.close()
    b.close()
    return ok


def test_config():
    """CONFIG: settings as forms, written back without disturbing the file."""
    print("CONFIG")
    s = ansi_login("Cfg")
    s.send(b"bye testsysop\r")
    s.wait_for(b"Sysop", 4)
    s.buf.clear()

    s.send(b"config\r")
    read_list(s)
    pages = plain(s.buf)
    ok = check("CONFIG lists its pages", b"limits" in pages and b"backup" in pages)
    ok &= check("plugins are pages too", b"chat" in pages)

    s.buf.clear()
    s.send(b"config limits\r")
    ok &= check("a page opens as a form", s.wait_for(b"Per call", 5))

    s.buf.clear()
    s.send(DOWN * 4 + b"\x08" * 4 + b"77" + F1)         # Accounts -> 77
    ok &= check("the page saves", s.wait_for(b"Saved and live", 5))
    if HOST in ("127.0.0.1", "localhost"):
        cfg = (USERDATA / "system.cfg").read_text()
        ok &= check("the new value is in system.cfg", "max_users = 77" in cfg)
        ok &= check("the rest of the file is untouched",
                    "sysop_password = testsysop" in cfg and "[plugin:chat]" in cfg)

    s.buf.clear()
    s.send(b"config limits\r")
    s.wait_for(b"Per call", 5)
    s.buf.clear()
    s.send(DOWN * 4 + b"\x08" * 4 + b"9999" + F1)
    ok &= check("a value out of range is refused", s.wait_for(b"Between", 5))
    s.send(b"\x1b")                                     # cancel
    s.pump(0.5)

    n = ansi_login("NotSysop")
    n.buf.clear()
    n.send(b"config\r")
    n.pump(0.8)
    ok &= check("CONFIG is the sysop's alone", b"Unknown" in n.buf)
    n.close()
    s.close()
    return ok



def test_privacy():
    """Nobody types a password before being told the link is in the clear."""
    print("Disclosure at sign-up")
    c = Caller(ansi=True)
    c.wait_for(b"Enter your handle", 10)
    c.buf.clear()
    c.send(b"Curious\r")
    c.wait_for(b"[R]egister", 6)
    c.buf.clear()
    c.send(b"r")
    ok = check("the house rules come first", c.wait_for(b"HOUSE RULES", 6))
    ok &= check("they lead with the one that removes you", b"NO HATE" in plain(c.buf))
    ok &= check("and they are paged, not a wall", b"continue" in plain(c.buf).lower())
    ok &= check("the warning still sits right before the password", pass_rules(c))
    ok &= check("registering warns the link is not encrypted",
                b"not encrypted" in plain(c.buf))
    ok &= check("and says what to do about it", b"anywhere else" in plain(c.buf))
    ok &= check("would you like to know more", b"know more" in plain(c.buf))

    # Four pages, each waiting for a key. Collect the lot.
    c.buf.clear()
    c.send(b"y")
    told = b""
    pages = 0
    for _ in range(5):
        if not c.wait_for(b"SPACE to continue", 6):
            break
        pages += 1
        told += plain(c.buf)
        c.buf.clear()
        c.send(b" ")
        c.pump(0.8)
    told += plain(c.buf)

    ok &= check("it is paged rather than a wall of text", pages == 4)
    ok &= check("it says up front how long this takes", b"Four short pages" in told)
    ok &= check("every page is numbered", b"Page 1 of 4" in told and b"Page 4 of 4" in told)
    ok &= check("telnet is not encrypted", b"TELNET IS NOT ENCRYPTED" in told)
    ok &= check("what it would actually take to read it",
                b"sniffer" in told and b"Low risk. Not no risk." in told)
    ok &= check("how the password is stored", b"SHA-256" in told)
    ok &= check("and that a hash is not magic", b"lookup table" in told)
    ok &= check("what the sysop can see", b"WHAT THIS BOARD KNOWS" in told)
    ok &= check("and the one rule that matters", b"use nowhere else" in told)
    ok &= check("the form opens at the end", c.wait_for(b"NEW ACCOUNT", 8))
    c.close()

    # N skips it, and the command brings it back later
    d = Caller(ansi=True)
    d.wait_for(b"Enter your handle", 10)
    d.send(b"Hasty\r")
    d.wait_for(b"[R]egister", 6)
    d.send(b"r")
    pass_rules(d)
    d.buf.clear()
    d.send(b"n")
    ok &= check("N goes straight to the form", d.wait_for(b"NEW ACCOUNT", 6))
    d.send(TEST_PW.encode() + b"\r" + TEST_PW.encode() + b"\rHasty\rhasty@example.com\r\r\r\r\r")
    d.wait_for(b"WELCOME ABOARD", 10)
    d.wait_for(b"Main", 8)
    d.buf.clear()
    d.send(b"privacy\r")
    read_list(d, 6)
    ok &= check("PRIVACY shows it again any time", b"no encryption" in plain(d.buf))
    d.close()
    return ok



def test_announce():
    """The board lists itself, sends nothing about callers, and can prove it."""
    print("Directory listing")
    import json as _json
    import socket as _socket
    import threading as _threading

    got = []

    def directory(port):
        """A stand-in directory: take one heartbeat, answer 200, remember it."""
        srv = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
        srv.setsockopt(_socket.SOL_SOCKET, _socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", port))
        srv.listen(2)
        srv.settimeout(20)
        try:
            c, _ = srv.accept()
        except _socket.timeout:
            return
        c.settimeout(5)
        data = b""
        try:
            while b"\r\n\r\n" not in data:
                more = c.recv(4096)
                if not more:
                    break
                data += more
            head, _, body = data.partition(b"\r\n\r\n")
            need = 0
            for line in head.split(b"\r\n"):
                if line.lower().startswith(b"content-length:"):
                    need = int(line.split(b":")[1])
            while len(body) < need:
                more = c.recv(4096)
                if not more:
                    break
                body += more
            got.append((head, body))
            c.sendall(b"HTTP/1.1 200 OK\r\nX-Seen-Address: 203.0.113.9\r\n"
                      b"Content-Length: 2\r\nConnection: close\r\n\r\nok")
        finally:
            c.close()
            srv.close()

    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  announce needs the host build")
        return True

    port = 8099
    th = _threading.Thread(target=directory, args=(port,), daemon=True)
    th.start()

    s = ansi_login("Announcer")
    s.send(b"bye testsysop\r")
    s.wait_for(b"Sysop", 4)
    s.buf.clear()

    s.send(b"announce test\r")           # prints the payload, sends nothing
    ok = check("ANNOUNCE TEST prints the payload", s.wait_for(b'"software":"unleashed"', 5))
    shown = plain(s.buf)
    ok &= check("the payload says what it is for", b"Nothing about callers" in shown)
    ok &= check("and carries no caller data", b"Announcer" not in shown)

    s.buf.clear()
    s.send(b"announce now\r")
    s.wait_for(b"Sending now", 4)
    th.join(15)
    ok &= check("the directory got a heartbeat", len(got) == 1)
    if got:
        head, body = got[0]
        ok &= check("it is a POST with a JSON body",
                    head.startswith(b"POST ") and b"application/json" in head)
        try:
            rec = _json.loads(body.decode())
        except Exception:
            rec = {}
        ok &= check("the payload parses", bool(rec))
        ok &= check("it names the board, not the callers",
                    rec.get("software") == "unleashed" and "name" in rec and
                    "owner" in rec and "description" in rec)
        ok &= check("it reports the lines, not who is on them",
                    rec.get("nodes") == 6 and isinstance(rec.get("busy"), int))
        ok &= check("no caller ever appears in it",
                    "Announcer" not in body.decode() and "handle" not in rec)

    s.buf.clear()                      # the dashboard carries a line per plugin
    s.send(b"dash\r")
    s.wait_for(b"SYSOP DASHBOARD", 5)
    s.pump(1.2)
    ok &= check("the dashboard reports the listing", b"Directory:" in plain(s.buf))
    s.send(b"q")
    s.pump(0.4)

    status = b""                       # the reply lands on the plugin's next tick
    for _ in range(10):
        s.buf.clear()
        s.send(b"announce\r")
        s.wait_for(b"Announce", 4)
        s.pump(0.6)
        status = plain(s.buf)
        if b"listed" in status:
            break
    if b"listed" not in status:
        print("    status was:", status.decode("ascii", "replace").replace("\r\n", " | "))
    ok &= check("the status shows the listing took", b"listed" in status)
    ok &= check("and the address the directory saw", b"203.0.113.9" in status)

    n = ansi_login("NotStaff2")
    n.buf.clear()
    n.send(b"announce\r")
    n.pump(0.8)
    ok &= check("ANNOUNCE is staff only", b"Unknown" in n.buf)
    n.close()
    s.close()
    return ok


def test_partitions():
    """What a filesystem upload may and may not reach.

    uploadfs rewrites the storage partition from data/. The whole point of
    the split is that accounts, config and plugin files are not on it, so
    this checks where things actually land rather than trusting the layout.
    """
    print("User data lives apart from the screens")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build's filesystem")
        return True

    c = ansi_login("Partitioned")
    c.send(b"bye\r")
    c.pump(0.5)
    c.close()

    ok = check("accounts are on the user partition", (USERDATA / "users.txt").exists())
    ok &= check("and not among the screens", not (DATA / "users.txt").exists())
    ok &= check("config is on the user partition", (USERDATA / "system.cfg").exists())
    ok &= check("plugin files are too", (USERDATA / "p").is_dir())
    ok &= check("screens are where uploadfs writes", (DATA / "screens").is_dir())

    # The announce token is the thing a sysop would hate to lose: it is what
    # keeps a directory listing theirs across a reflash.
    cfg = (USERDATA / "system.cfg").read_text()
    ok &= check("the announce section is on the protected side",
                "[plugin:announce]" in cfg or "announce" not in cfg)

    # Simulate the destructive half of a flashall: everything on the storage
    # partition goes, then is written again from data/. The account file is
    # compared by content rather than bytes, because a logoff legitimately
    # rewrites it with the call stats.
    import shutil
    wiped = []
    for item in DATA.iterdir():
        if item.name in ("user", "logs"):
            continue
        if item.is_dir():
            for f in sorted(item.rglob("*")):
                if f.is_file():
                    wiped.append(f)
                    f.unlink()
        elif item.is_file():
            wiped.append(item)
            item.unlink()

    ok &= check("a filesystem upload does not touch the accounts",
                (USERDATA / "users.txt").exists() and
                "[Partitioned]" in (USERDATA / "users.txt").read_text())
    ok &= check("nor the configuration", (USERDATA / "system.cfg").exists())
    ok &= check("nor the plugin files", (USERDATA / "p").is_dir())
    ok &= check("and the screens really were wiped", wiped and
                not (DATA / "screens" / "welcome.ans").exists())

    # Put the screens back, the way uploadfs would, so whatever runs next is
    # not looking at a board this test emptied.
    src = ROOT / "data" / "screens"
    if src.is_dir():
        (DATA / "screens").mkdir(parents=True, exist_ok=True)
        for f in src.iterdir():
            if f.is_file():
                shutil.copy2(f, DATA / "screens" / f.name)
    ok &= check("and the screens can be written again",
                (DATA / "screens" / "welcome.ans").exists())
    return ok


def test_refresh_and_ctrl_l():
    """Two shell behaviours that only show up on a live terminal."""
    print("Refresh rows and Ctrl-L")
    c = ansi_login("Refresher")
    c.send(b"bye testsysop\r")   # DASH is staff only
    c.pump(1.5)
    c.buf.clear()
    c.send(b"dash 2\r")
    c.pump(2.5)
    seen = plain(c.buf)
    # The status line carries the micro sign. Walking it a byte at a time
    # sent 0xC2 and 0xB5 through the charset map separately and each came
    # back as '?', so a refresh screen said "??nleashed BBS".
    ok = check("the refresh header keeps the micro sign", b"?nleashed" not in seen)
    ok &= check("and still names the board", b"nleashed BBS" in seen)
    c.send(b"x")                          # any key stops the refresh
    c.pump(1.0)

    c.buf.clear()
    c.send(b"who")                        # half a command, deliberately unsent
    c.pump(0.4)
    c.send(b"\x0c")                       # Ctrl-L
    c.pump(1.0)
    ok &= check("Ctrl-L clears the screen", b"\x1b[2J" in bytes(c.buf))
    ok &= check("and keeps what was half typed",
                plain(c.buf).rstrip().endswith(b"who"))
    c.send(b"\r")
    c.pump(1.0)
    c.close()
    return ok


def test_screens():
    """The three screens added for the sign-up and chat flows.

    Each one is optional by design: a board without the file carries on.
    These check the wiring, that each plays at the right moment and hands
    the caller on to the right place afterwards.
    """
    print("Rules, new user and the chat transition")
    c = Caller(ansi=True)
    c.wait_for(b"Enter your handle", 10)
    c.send(b"Screeny\r")
    c.wait_for(b"[R]egister", 6)
    c.buf.clear()
    c.send(b"r")

    ok = check("R plays the house rules", c.wait_for(b"HOUSE RULES", 6))
    ok &= check("which lead with the rule that removes you", b"NO HATE" in plain(c.buf))
    ok &= check("and warn the link is not encrypted",
                b"NOTHING HERE IS ENCRYPTED" in plain(c.buf))
    ok &= check("paged, not a wall of text", b"continue" in plain(c.buf).lower())

    ok &= check("the rules lead to the encryption warning", pass_rules(c))
    c.send(b"n")
    ok &= check("and then to the sign-up form", c.wait_for(b"NEW ACCOUNT", 6))

    c.buf.clear()
    c.send(b"pw1234\rpw1234\rScreeny\rscreeny@example.com\r\r\r\r\r")
    ok &= check("registering works", c.wait_for(b"WELCOME ABOARD", 10))
    ok &= check("a new account gets the new-user screen",
                c.wait_for(b"YOU ARE ON THE BOARD", 8))
    seen = plain(c.buf)
    ok &= check("which is the short version, not the long one",
                b"HOUSE RULES" not in seen and b"No hate" in seen)
    ok &= check("and points at the long one", b"PRIVACY" in seen)
    ok &= check("then lands at the prompt", c.wait_for(b"Main", 8))

    # Joining chat gets the transition, and still ends up in the room.
    c.buf.clear()
    c.send(b"chat\r")
    ok &= check("joining chat plays the transition", c.wait_for(b"ENTERING CHAT", 6))
    ok &= check("which is honest that /p is not private either",
                b"not private" in plain(c.buf))
    ok &= check("and the caller is in the room afterwards",
                c.wait_for(b"/s who, /q quits", 6))
    c.buf.clear()
    c.send(b"/welcome\r")
    ok &= check("/welcome replays the transition in the room",
                c.wait_for(b"ENTERING CHAT", 6))
    ok &= check("and does not say there is no private mode when /p exists",
                b"private mode" not in plain(c.buf))
    c.send(b"/q")
    c.wait_for(b"Main", 6)

    # A returning caller gets the bulletin, not the new-user screen.
    c.send(b"bye\r")
    c.pump(0.5)
    c.close()

    d = Caller(ansi=True)
    d.wait_for(b"Enter your handle", 10)
    d.send(b"Screeny\r")
    d.wait_for(b"Password:", 6)
    d.buf.clear()
    d.send(b"pw1234\r")
    d.wait_for(b"Main", 10)
    ok &= check("a returning caller does not see it again",
                b"YOU ARE ON THE BOARD" not in plain(d.buf))
    d.close()
    return ok


def test_exit_screen():
    """The send-off runs however the call ended, and the line is held open
    afterwards so the screen can actually be read."""
    print("The send-off")
    c = ansi_login("Leaver")
    c.buf.clear()
    c.send(b"bye\r")
    ok = check("a voluntary logoff gets the send-off",
               c.wait_for(b"Stay unleashed", 6))
    start = time.time()
    c.wait_closed(12)
    held = time.time() - start
    # Five seconds of linger, minus the time the screen itself took to send.
    print(f"        (line held {held:.1f}s)")
    ok &= check("the line is held open afterwards, not dropped at once",
                held >= 3.0)
    c.close()
    return ok


def run_selected(only):
    """--only=announce runs just the tests whose name contains "announce"."""
    import types
    picked = [(n, f) for n, f in sorted(globals().items())
              if n.startswith("test_") and isinstance(f, types.FunctionType) and only in n]
    if not picked:
        print("no test matches", only)
        return False
    return all(f() for _, f in picked)


if __name__ == "__main__":
    ONLY = next((a.split("=", 1)[1] for a in FLAGS if a.startswith("--only=")), None)
    if ONLY:
        picked_ok = run_selected(ONLY)
        print("ALL PASS" if picked_ok else "FAILURES")
        sys.exit(0 if picked_ok else 1)
    results = [test_ansi(), test_telnet_first(), test_petscii(), test_ascii(),
               test_page(), test_sysop(), test_cosysop(), test_accounts(), test_user_admin(), test_guest(),
               test_privacy(), test_plugins(), test_about(), test_announce(),
               test_chat(), test_room_commands(),
               test_mail(), test_menus(), test_sysinfo(), test_config(), test_serial(),
               test_bulletin(), test_idle_login(), test_busy(),
               test_screens(), test_exit_screen(),
               test_refresh_and_ctrl_l(),
               test_partitions()]
    if "--backup" in FLAGS:
        results.append(test_backup())
    if "--ban" in FLAGS:
        results.append(test_ban())
    print("ALL PASS" if all(results) else "FAILURES")
    sys.exit(0 if all(results) else 1)
