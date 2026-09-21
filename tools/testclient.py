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


def send_maybe(c, data):
    """Send, and treat a closed connection as an outcome rather than a crash.

    Some tests deliberately provoke a hangup and then keep typing: the
    lockout test sends wrong passwords until the board drops the call, and
    whether the drop lands on one attempt or the next depends on timing. The
    board closing the socket is the thing being tested, so a send that
    arrives after it is expected, not a failure. Without this the test dies
    with BrokenPipeError and takes the rest of the suite with it.

    Returns True if the bytes went, False if the far end had already gone.
    """
    try:
        c.send(data)
        return True
    except (BrokenPipeError, ConnectionResetError, OSError):
        return False


def drain(c, tries=12):
    """Get a session back to a command prompt, however it is currently held.

    A list that filled the screen stops at "[More] Y/n/c" and a screen with
    more than one page stops at "Press SPACE to continue". Both hold the
    session until a key arrives, and both then eat the first characters of
    whatever the test sends next, as page keys rather than as a command. That
    has broken three tests in this file, each time reporting working code as
    broken, and each time the give-away was that the test passed on its own
    and failed in a full run, because the lists are longer once the suite has
    made fifty calls.

    Call this after any command whose output might not fit a screen.
    """
    for _ in range(tries):
        seen = plain(c.buf)
        if b"[More]" in seen:
            c.buf.clear()
            c.send(b"n")            # n stops a list
        elif b"Press SPACE to continue" in seen or b"PRESS SPACE" in seen:
            c.buf.clear()
            c.send(b" ")            # any key turns a screen's page
        else:
            return True
        c.pump(0.6)
    return False


def leave_files(c, tries=4):
    """Get a caller out of the file subsystem, from whatever depth.

    Q goes back one level, so from inside an area it lands on the area menu
    rather than at the command prompt. A test that sends one Q and then a
    command types that command into the file room, where the letters are
    swallowed and any digit in it opens an area. That broke six checks in
    one run and none of them were the code's fault.
    """
    for _ in range(tries):
        if b"Out of files" in plain(c.buf):
            return True
        c.buf.clear()
        c.send(b"q")
        c.pump(0.6)
    return b"Out of files" in plain(c.buf)


def ansi_color(fg, bold=False, reverse=False):
    """The bytes Term::color emits for one colour.

    Derived rather than hardcoded: these assertions used to spell out
    ESC[1;33m, and when the terminal layer started leading every colour with
    SGR 0 they failed on the spelling while the behaviour was right. What the
    tests care about is that the shortcut letter is yellow, not how yellow is
    encoded this month.
    """
    out = "\x1b[0;%d" % fg
    if bold:
        out += ";1"
    if reverse:
        out += ";7"
    return (out + "m").encode()


def config_num(name, fallback):
    """A numeric #define from src/config.h, so the suite follows the build."""
    src = (ROOT / "src" / "config.h").read_text()
    m = re.search(r'#define\s+' + name + r'\s+(\d+)', src)
    return int(m.group(1)) if m else fallback


BBS_VERSION = bbs_version()
MAX_NODES   = config_num("BBS_MAX_NODES", 6)
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

    def _options(self, d):
        """Answer telnet option negotiation, the way a terminal does.

        This client used to ignore negotiation entirely, which was fine while
        the board never asked for anything that mattered. It asks for RFC 856
        binary before a transfer now, and a client that never replies has not
        agreed, so the board correctly keeps stripping the NUL a terminal puts
        after a bare CR. Silence is an answer, and it was the wrong one.

        _binary["refuse"] makes this decline, which is the terminal that
        broke on real hardware.
        """
        i = 0
        while i + 2 < len(d):
            if d[i] != 0xFF or d[i + 1] not in (0xFB, 0xFD):   # WILL, DO
                i += 1
                continue
            cmd, opt = d[i + 1], d[i + 2]
            if opt == 0x00:                                   # TRANSMIT-BINARY
                if _binary["refuse"]:
                    self.s.sendall(bytes([0xFF, 0xFC if cmd == 0xFD else 0xFE, 0x00]))
                elif cmd == 0xFD:                             # DO  -> WILL
                    self.s.sendall(bytes([0xFF, 0xFB, 0x00]))
                    _binary["on"] = True
                else:                                         # WILL -> DO
                    self.s.sendall(bytes([0xFF, 0xFD, 0x00]))
            i += 3

    def _answer(self, d):
        """Emulate an ANSI terminal answering ESC[6n with the cursor column."""
        self._options(d)
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
        # Case-insensitive, and \d+ not \d. The login says "Connected to
        # node 1 of 10" now, and the old pattern also could not have read a
        # two digit node on a ten line board.
        m = re.search(rb"node (\d+) of", self.buf, re.IGNORECASE)
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


def form_const(name, fallback):
    """A layout constant out of form.h, so these checks follow the form."""
    src = (ROOT / "src" / "core" / "form.h").read_text()
    m = re.search(r'constexpr uint8_t\s+' + name + r'\s*=\s*(\d+)', src)
    return int(m.group(1)) if m else fallback


BOX_COL = form_const("kBoxCol", 12)    # first column of a field's input box
BOX_W = form_const("kBoxW", 27)        # how wide that box is


def colours_self_contained(data):
    """Every SGR that sets a foreground has to clear the attributes first.

    ESC[1;33m sets bold and a foreground and clears nothing, so a reverse
    attribute that is still set survives it; ESC[0;33;1m does not. That is
    the rule that fixed the form's reverse-video bleed and it is cheap to
    hold, so hold it.
    """
    for m in re.finditer(rb"\x1b\[([0-9;]*)m", bytes(data)):
        p = m.group(1).decode().split(";")
        if any(v.isdigit() and 30 <= int(v) <= 37 for v in p) and p[0] != "0":
            return False
    return True


class AttrScreen:
    """An 80x25 screen model that keeps the reverse-video ATTRIBUTE.

    plain() throws the escapes away, so no text check can see a highlight
    that was never turned off: the bleed IS the escape stream. This model
    also IGNORES SGR 27 (reverse off) on purpose, because ANSI.SYS never
    implemented it and SyncTERM does not act on it.

    That is exactly how the sign-up form failed on Rob's SyncTERM at 0.17.0:
    reverse was turned off with ESC[27m alone, every colour sent afterwards
    was ESC[1;NNm, and so the label of every row the cursor had left stayed
    painted as a filled block. Every text assertion in this file passed.
    """

    CSI = re.compile(rb"\x1b\[([0-9;?]*)([A-Za-z])")

    def __init__(self, cols=80, rows=25):
        self.cols, self.rows = cols, rows
        self.ch = [[" "] * cols for _ in range(rows)]
        self.rv = [[0] * cols for _ in range(rows)]
        self.x = self.y = 0
        self.rev = 0

    def feed(self, data):
        data = bytes(data)
        i, n = 0, len(data)
        while i < n:
            b = data[i]
            if b == 0x1B:
                m = self.CSI.match(data, i)
                if m:
                    self._csi(m.group(1).decode(), m.group(2).decode())
                    i = m.end()
                    continue
                i += 1
            elif b == 0xFF:                       # telnet IAC: skip the command
                i += 3
            elif b == 0x0D:
                self.x = 0
                i += 1
            elif b == 0x0A:
                self.y = min(self.rows - 1, self.y + 1)
                i += 1
            elif b == 0x08:
                self.x = max(0, self.x - 1)
                i += 1
            elif b >= 0x20:                       # one glyph, one cell, UTF-8 or not
                self._put("?" if b >= 0x80 else chr(b))
                i += 1 if b < 0x80 else (2 if b < 0xE0 else (3 if b < 0xF0 else 4))
            else:
                i += 1

    def _put(self, c):
        if self.x < self.cols:
            self.ch[self.y][self.x] = c
            self.rv[self.y][self.x] = self.rev
            self.x += 1

    def _csi(self, params, final):
        p = [int(v) for v in params.split(";") if v.isdigit()]
        if final in "Hf":
            self.y = max(0, min(self.rows - 1, (p[0] - 1) if p else 0))
            self.x = max(0, min(self.cols - 1, (p[1] - 1) if len(p) > 1 else 0))
        elif final == "J" and p and p[0] == 2:
            self.ch = [[" "] * self.cols for _ in range(self.rows)]
            self.rv = [[0] * self.cols for _ in range(self.rows)]
        elif final == "K":
            for x in range(self.x, self.cols):
                self.ch[self.y][x] = " "
                self.rv[self.y][x] = self.rev
        elif final == "A":
            self.y = max(0, self.y - (p[0] if p else 1))
        elif final == "B":
            self.y = min(self.rows - 1, self.y + (p[0] if p else 1))
        elif final == "C":
            self.x = min(self.cols - 1, self.x + (p[0] if p else 1))
        elif final == "D":
            self.x = max(0, self.x - (p[0] if p else 1))
        elif final == "m":
            for v in (p or [0]):
                if v == 0:
                    self.rev = 0
                elif v == 7:
                    self.rev = 1
                # 27 is deliberately not handled: see the class comment.

    def reverse_runs(self):
        """[(row, first column, width)] for every run of reverse video, both
        counted from 1 the way a screen is."""
        out = []
        for y in range(self.rows):
            x = 0
            while x < self.cols:
                if self.rv[y][x]:
                    s = x
                    while x < self.cols and self.rv[y][x]:
                        x += 1
                    out.append((y + 1, s + 1, x - s))
                else:
                    x += 1
        return out

    def row(self, y):
        return "".join(self.ch[y - 1]).rstrip()


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
    # The screen's own bytes, not Term::color: welcome.ans is ANSI art and
    # carries its escapes literally, so this one does not follow the
    # terminal layer's encoding.
    ok &= check("@NODE@ expanded", c.wait_for(b"Node \x1b[1;33m", 2))
    ok &= check("@BBS@ shows a real micro sign", c.wait_for("µnleashed BBS".encode(), 3))
    ok &= check("handle prompt", c.wait_for(b"Enter your handle", 8))
    c.buf.clear()
    c.send(b"[3;20R\r")
    ok &= check("terminal junk rejected as handle", c.wait_for(b"Use letters", 3))
    c.buf.clear()
    ok &= check("new handle registers through the form", login(c, "Rob"))
    ok &= check("welcome by handle", b"Welcome" in c.buf and b"Rob" in c.buf)
    ok &= check("time left shown", b"minutes" in c.buf)
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
    ok = check("BYE <password> reaches the sysop node", r.wait_for(b"SysOp node", 4))
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
    ok &= check("HELP reaches its footer", r.wait_for(b"?|H|HELP", 5))
    r.pump(1.0)
    # The point of the rework: one screen, no pager. A menu that stops at
    # [More] every time teaches people to mash a key through it, and then
    # they never read any of it. The old test asserted the paging, which was
    # the board's worst habit written down as a requirement.
    ok &= check("and does not page, because it fits",
                b"[More]" not in plain(r.buf))
    ok &= check("the sysop menu is offered", b"SYSOP" in plain(r.buf))
    drain(r)

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
    c.buf.clear()                       # from here the buffer is the form's own bytes
    c.send(b"n")
    c.wait_for(b"NEW ACCOUNT", 5)
    c.send(b"abcd\rabce\rZed\rzed@example.com\r\r\r\r\r")
    ok &= check("password mismatch refused", c.wait_for(b"The passwords do not match", 5))

    # --- the attribute stream, not the stripped text.
    #
    # That last line walked the focus down every field and on to [ Save ],
    # which is the path Rob was on when SyncTERM showed the NEW ACCOUNT form
    # with a filled orange block across the Name, Email and Address labels.
    # Nothing in this file could see it, because plain() had already thrown
    # the attributes away. The rule is simple and worth asserting: the only
    # thing a form may leave in reverse video is the focused field's box.
    c.pump(1.5)                         # fail() blinks the message: let it finish
    form = bytes(c.buf)
    scr = AttrScreen()
    scr.feed(form)
    runs = scr.reverse_runs()
    one_box = len(runs) == 1 and runs[0][1] == BOX_COL and runs[0][2] == BOX_W
    if not one_box:
        for r in runs:
            print("        reverse at row %d col %d, %d wide: %r"
                  % (r[0], r[1], r[2], scr.row(r[0])))
    ok &= check("the form leaves exactly one run of reverse video", len(runs) == 1)
    ok &= check("and it is the focused field's box (col %d, %d wide), not a label"
                % (BOX_COL, BOX_W), one_box)
    ok &= check("every colour the form sends clears the attributes before it",
                colours_self_contained(form))
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
    ok &= check("second call: welcome back, call 2", c.wait_for(b"Welcome back, ", 5) and c.wait_for(b"This is call 2 for you", 3))
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
        if not send_maybe(c, b"wrong1\r"):
            break                                      # already dropped, which is the point
        c.wait_for(b"ACCESS DENIED", 6)
    ok &= check("3 wrong passwords hang up the call", c.wait_for(b"Too many wrong passwords.", 5) and c.wait_closed(8))
    c.close()
    c, _ = handle_then("Locky", [b"Password:"])
    for _ in range(2):
        c.buf.clear()
        # The fifth miss for this handle drops the call, and whether that is
        # this attempt or the next depends on how fast the board got here.
        if not send_maybe(c, b"wrong2\r"):
            break
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
    ok = check("sysop node", s.wait_for(b"SysOp node", 5))
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
    ok &= check("guest time limit 15 minutes", b"have 15 minutes" in plain(c.buf))
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
    gh = plain(c.buf)
    if b"PROFILE" in gh or b"PASSWORD" in gh or b"WHO" not in gh:
        print("      DBG guest help: %r" % gh[-400:])
    ok &= check("HELP hides PROFILE and PASSWORD from guests",
                b"WHO" in gh and b"PROFILE" not in gh and b"PASSWORD" not in gh)

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
                    c.wait_closed(12) and b"SysOp node" not in c.buf and PASSWORD.encode() not in c.buf)
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
    s.wait_for(b"SysOp node", 5)
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
    s.wait_for(b"SysOp node", 5)
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
    ok &= check("partial input redrawn",
                c.wait_for(b"Enter your handle: " + ansi_color(37, bold=True) + b"Ro", 2))
    if "--slow" in FLAGS:
        ok &= check("hangup at 60 s", c.wait_for(b"IDLE TIMEOUT", 32) and c.wait_closed(8))
    c.close()
    return ok


def test_busy():
    print("Busy line (%d nodes)" % MAX_NODES)
    callers = [Caller(ansi=True) for _ in range(MAX_NODES)]
    time.sleep(0.5 + MAX_NODES * 0.05)       # every node has to finish detection
    over = Caller(ansi=True)                 # the caller past the last node
    ok = check("the caller past the last node gets the busy screen",
               over.wait_for(b"lines are busy", 5))
    ok &= check("countdown shown", over.wait_for(b"Disconnecting in", 5))
    t0 = time.time()
    eighth = Caller()                        # and the one after that is refused
    ok &= check("the next caller gets BUSY", eighth.wait_for(b"BUSY", 3))
    ok &= check("and is dropped at once", eighth.wait_closed(2))
    ok &= check("busy line hangs up after countdown", over.wait_for(b"NO CARRIER", 13))
    ok &= check("busy countdown ~10 s", 9 < time.time() - t0 < 12)
    over.close()
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
        ok &= check("busy-line guest reaches sysop node", guest.wait_for(b"SysOp node", 4))
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
    ok = check("sysop node", s.wait_for(b"SysOp node", 5))
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
    # The footer is a grammar now, not a sentence: "?|H|HELP [CHAT|ACCOUNT|ALL]"
    ok &= check("? names the other menus",
                b"?|H|HELP" in main and b"CHAT" in main and b"ACCOUNT" in main)
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
    ok &= check("the shortcut letter is picked out",
                ansi_color(33, bold=True) + b"W" in bytes(c.buf))
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

    # Saving restarts the plugins, and each one registers its command table
    # on the way up. Nothing used to take the old registrations back down, so
    # the table filled with duplicates and, once it was full, whichever
    # plugins came last were left with no commands at all: running, shown as
    # running, and answering "Unknown command" to their own verbs.
    #
    # Checked by counting rather than by waiting for the overflow, because
    # the overflow depends on how many plugins are compiled in and how many
    # table slots there are. A duplicate row in HELP is the leak itself, and
    # it shows on the first reload rather than the third.
    s.buf.clear()
    s.send(b"sd\r")
    ok &= check("a plugin still has its commands after a reload",
                s.wait_for(b"SD card", 5))
    s.buf.clear()
    s.send(b"? sysop\r")
    s.pump(1.2)
    listed = plain(s.buf)
    ok &= check("and is listed in HELP exactly once, not once per reload",
                listed.count(b"SD MOUNT") == 1)
    ok &= check("which is true of the last plugin in the table too",
                listed.count(b"ANNOUNCE TEST") == 1)
    if b"[More]" in listed or b"Press SPACE" in listed:
        s.send(b"n")
        s.wait_for(b"Sysop", 4)

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


def max_column(data):
    """The rightmost screen column anything in this output lands on.

    A small screen model rather than a byte search, because the form draws
    by moving the cursor and then writing a run, and a colour change in the
    middle of a run breaks any pattern that tries to measure it in one go.
    Only the column is tracked: what matters is that nothing reaches past
    column 39, because column 40 wraps on a C64 and a wrapped form row
    scrolls the screen out from under the caller.

    Measured on the ANSI caller, which proves the 40-column case too: the
    form's layout is fixed (label at 2, box at 12, 27 wide) and does not
    follow the terminal's width.
    """
    col = 1
    worst = 0
    i = 0
    while i < len(data):
        b = data[i]
        if b == 0x1B:
            m = ANSI_RE.match(data, i)
            if not m:
                i += 1
                continue
            seq = m.group(0)
            body = seq[2:-1]
            if seq.endswith(b"H"):
                parts = body.split(b";")
                col = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 1
            elif seq.endswith(b"D"):          # scramble rewrites its text in place
                col = max(1, col - (int(body) if body.isdigit() else 1))
            elif seq.endswith(b"C"):
                col += int(body) if body.isdigit() else 1
            i = m.end()
            continue
        if b == 0x0D:
            col = 1
        elif b == 0x08:
            col = max(1, col - 1)
        elif 0x80 <= b <= 0xBF:
            pass                              # UTF-8 tail byte: no column of its own
        elif b >= 0x20:
            col += 1
            worst = max(worst, col - 1)
        i += 1
    return worst


def test_config_areas():
    """A file area is a page of its own, not four values in one text box.

    The row on CONFIG files is a button showing the area's name; Enter opens
    Path, Name, Read and Write as fields; Save writes the bar-separated form
    back so a hand-edited system.cfg still parses, and lands on the page the
    button was on.
    """
    print("CONFIG area pages")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build")
        return True
    sd = os.environ.get("BBS_SD_DIR", "")
    if not sd:
        print("  SKIP  the files plugin needs a card")
        return True

    cfg = USERDATA / "system.cfg"
    s = ansi_login("Areas")
    s.send(b"bye " + PASSWORD.encode() + b"\r")
    s.wait_for(b"Sysop", 4)
    s.buf.clear()

    # ---- the files page: rows that lead somewhere -------------------------
    s.send(b"config files\r")
    ok = check("the files page opens", s.wait_for(b"Area 1", 6))
    s.pump(1.0)
    page = plain(s.buf)
    ok &= check("an area row is a button showing its name",
                b"[ C64 Downloads" in page)
    ok &= check("not the stored format a sysop would have to already know",
                b"pub/c64 | C64 Downloads" not in page)
    ok &= check("and a row nobody has set up says so", b"[ not set" in page)
    ok &= check("nothing on the page reaches past column 39",
                max_column(bytes(s.buf)) <= 39)

    # ---- the area's own page ---------------------------------------------
    s.buf.clear()
    s.send(DOWN * 4 + b"\r")
    ok &= check("Enter on the row opens that area's own page",
                s.wait_for(b"FILE AREA 1", 6))
    s.pump(1.0)
    sub = plain(s.buf)
    ok &= check("with a field for each part",
                b"Path" in sub and b"Name" in sub and b"Read" in sub and b"Write" in sub)
    ok &= check("the path is a value of its own", b"pub/c64" in sub)
    # area1 in the harness names no levels, so the form shows the ones the
    # area is actually running under rather than a blank that would step to
    # "all" on the first press and quietly open it to everybody.
    ok &= check("and a level the file never named shows what it is running as",
                b"all" in sub and b"staff" in sub)
    ok &= check("the area page fits 40 columns too",
                max_column(bytes(s.buf)) <= 39)

    # ---- edit, cycle, save -----------------------------------------------
    s.buf.clear()
    s.send(b"\x08" * 8 + b"pub/edited")
    s.pump(0.4)
    s.send(DOWN + b"\x08" * 16 + b"Edited Area")
    s.pump(0.4)
    s.send(DOWN + b" ")                       # Read: all -> users
    s.pump(0.4)
    ok &= check("space steps the level to the next one on the ladder",
                b"users" in plain(s.buf))
    s.buf.clear()
    s.send(F1)
    ok &= check("saving lands back on the files page", s.wait_for(b"Area 8", 6))
    s.pump(1.2)
    back = plain(s.buf)
    ok &= check("and says the save took", b"Saved and live" in back)
    ok &= check("the row now shows the new name", b"[ Edited Area" in back)

    body = cfg.read_text()
    ok &= check("system.cfg keeps the bar-separated form the plugin parses",
                "area1 = pub/edited | Edited Area | users | staff" in body)
    ok &= check("and the rest of the file is untouched",
                "area2 = pub/empty | Empty Area" in body and
                "sysop_password = testsysop" in body)
    ok &= check("the board made the new area's folder on the card",
                (pathlib.Path(sd) / "pub" / "edited").is_dir())

    # ---- cancel changes nothing ------------------------------------------
    s.buf.clear()
    s.send(DOWN + b"\r")                      # Area 1 -> Area 2, open it
    ok &= check("the next area opens from the same page",
                s.wait_for(b"FILE AREA 2", 6))
    s.pump(0.6)
    s.buf.clear()
    s.send(DOWN + b"\x08" * 16 + b"Never Saved")
    s.pump(0.4)
    s.send(b"\x1b")
    ok &= check("cancelling comes back to the files page as well",
                s.wait_for(b"Area 8", 6))
    s.pump(1.0)
    after = plain(s.buf)
    ok &= check("saying nothing changed", b"Nothing changed" in after)
    ok &= check("and the row still reads as it did", b"[ Empty Area" in after)
    ok &= check("with nothing written to the file",
                "area2 = pub/empty | Empty Area" in cfg.read_text() and
                "Never Saved" not in cfg.read_text())

    # ---- and the page itself still leaves CONFIG -------------------------
    s.buf.clear()
    s.send(b"\x1b")
    ok &= check("ESC on the page itself leaves CONFIG",
                s.wait_for(b"Cancelled, nothing changed", 6))

    # ---- a line dropped inside a sub-page releases the editor ------------
    # The one thing most likely to go wrong: the guard is released on save,
    # on cancel and in closeSession, and a nested page must not slip past
    # all three. Checked with a real drop rather than a logoff.
    #
    # The sysop node holds exactly one caller, so the dropped session has to
    # be gone before the next one elevates, or the second BYE is refused and
    # dropped and this reads as a failure that is not there.
    s.buf.clear()
    s.send(b"config files\r")
    s.wait_for(b"Area 1", 6)
    s.send(DOWN * 4 + b"\r")
    ok &= check("a sysop can be two pages deep", s.wait_for(b"FILE AREA 1", 6))
    s.close()
    time.sleep(1.5)

    t = ansi_login("Areas2")
    t.send(b"bye " + PASSWORD.encode() + b"\r")
    t.wait_for(b"Sysop", 5)
    t.buf.clear()
    t.send(b"config files\r")
    opened = t.wait_for(b"Area 1", 6)
    ok &= check("dropping the line inside one does not lock CONFIG out",
                opened and b"is editing the settings" not in plain(t.buf))

    # ---- put area1 back, through the same page ---------------------------
    t.buf.clear()
    t.send(DOWN * 4 + b"\r")
    t.wait_for(b"FILE AREA 1", 6)
    t.pump(0.6)
    t.send(b"\x08" * 12 + b"pub/c64")
    t.pump(0.4)
    t.send(DOWN + b"\x08" * 16 + b"C64 Downloads")
    t.pump(0.4)
    t.send(DOWN + b"a")                       # Read: back to all
    t.pump(0.4)
    t.buf.clear()
    t.send(F1)
    t.wait_for(b"Area 8", 6)
    t.pump(1.0)
    ok &= check("a letter picks a level straight off the ladder",
                "area1 = pub/c64 | C64 Downloads | all | staff" in cfg.read_text())
    t.send(b"\x1b")
    t.pump(0.6)
    t.close()
    time.sleep(1.2)

    # ---- the narrow terminal and the one with no cursor at all -----------
    p = Caller(ansi=False)
    p.wait_for(b"HIT DEL OR BACKSPACE", 6)
    p.send(b"\x14")
    p.wait_for(b"40 OR 80 COLUMNS", 5)
    p.send(b"4")
    p.wait_for(pet("Enter your handle"), 10)
    ok &= check("a C64 caller gets on the board", login(p, "Petarea", as_pet=True))
    p.send(pet("bye " + PASSWORD) + b"\r")
    p.wait_for(pet("Sysop"), 6)
    p.buf.clear()
    p.send(pet("config files") + b"\r")
    ok &= check("40 columns gets the same buttons", p.wait_for(pet("[ C64 Downloads"), 6))
    p.buf.clear()
    p.send(b"\x11" * 4 + b"\r")               # C64 CRSR down, then RETURN
    ok &= check("and CRSR plus RETURN opens the area",
                p.wait_for(pet("FILE AREA 1"), 6))
    p.send(b"\x5f")                           # C64 left-arrow cancels
    ok &= check("left-arrow comes back to the files page",
                p.wait_for(pet("Area 8"), 6))
    p.send(b"\x5f")
    p.pump(0.8)
    p.close()
    time.sleep(1.2)

    a = Caller(ansi=False)
    a.wait_for(b"HIT DEL OR BACKSPACE", 6)
    a.send(b"\x08")
    a.wait_for(b"Enter your handle", 10)
    ok &= check("a plain ASCII caller gets on the board", login(a, "Asciiarea"))
    a.send(b"bye " + PASSWORD.encode() + b"\r")
    a.wait_for(b"Sysop", 6)
    a.buf.clear()
    a.send(b"config files\r")
    a.wait_for(b"Enabled", 6)
    a.send(b"\r\r\r\r")                       # past enabled, read, write, admin
    a.pump(1.0)
    # No cursor to put a button under, so the row becomes a question.
    ok &= check("plain ASCII asks rather than draws a button",
                b"open (y/N)" in plain(a.buf) and b"[ Area" not in plain(a.buf))
    a.buf.clear()
    a.send(b"y")
    ok &= check("and Y opens the area's page there too",
                a.wait_for(b"FILE AREA 1", 6))
    a.buf.clear()
    a.send(b"\rNew Ascii Name\r\r\ry")        # keep path, new name, keep levels, save
    ok &= check("which saves from the line prompts", a.wait_for(b"Saved and live", 8))
    ok &= check("in the same bar-separated form",
                "area1 = pub/c64 | New Ascii Name | all | staff" in cfg.read_text())
    a.send(b"\x1b")
    a.pump(0.6)
    a.close()
    time.sleep(1.2)

    # Leave area1 as the rest of the suite expects to find it.
    z = ansi_login("Areas3")
    z.send(b"bye " + PASSWORD.encode() + b"\r")
    z.wait_for(b"Sysop", 5)
    z.send(b"config files\r")
    z.wait_for(b"Area 1", 6)
    z.send(DOWN * 4 + b"\r")
    z.wait_for(b"FILE AREA 1", 6)
    z.pump(0.6)
    z.send(DOWN + b"\x08" * 20 + b"C64 Downloads")
    z.pump(0.4)
    z.send(F1)
    z.wait_for(b"Area 8", 6)
    z.pump(0.8)
    ok &= check("and the area is back the way the suite found it",
                "area1 = pub/c64 | C64 Downloads | all | staff" in cfg.read_text())
    z.send(b"\x1b")
    z.pump(0.5)
    z.close()
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



DIRECTORY_TOKEN = "a1b2c3d4e5f60718293a4b5c6d7e8f90"    # 32 chars, like the real one


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
            reply = (b"HTTP/1.1 200 OK\r\n"
                     b"X-Seen-Address: 203.0.113.9\r\n"
                     b"X-Listing-State: pending\r\n"
                     b"X-Listing-Token: " + DIRECTORY_TOKEN.encode() + b"\r\n"
                     b"Content-Length: 2\r\nConnection: close\r\n\r\nok")
            # Split four characters into the token, on purpose.
            cut = reply.index(DIRECTORY_TOKEN.encode()) + 4
            c.sendall(reply[:cut])
            time.sleep(0.3)
            c.sendall(reply[cut:])
        finally:
            c.close()
            srv.close()

    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  announce needs the host build")
        return True

    # The stand-in directory's port. From the environment so parallel runs
    # do not fight over it: it was the one shared resource the harness did
    # not isolate, and two runs at once produced a broken pipe on the BBS
    # socket, which reads exactly like a bug in the board.
    port = int(os.environ.get("BBS_DIR_PORT", "8099"))
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

    # A reply arriving in pieces must not leave the board holding half a
    # token. This is the bug that produced ninety listings for one board.
    s.pump(1.5)                      # the reply lands on a later plugin tick
    s.buf.clear()
    s.send(b"announce test\r")
    s.wait_for(b'"software"', 5)
    payload = plain(s.buf).decode("latin-1", "replace")
    import re as _re
    m = _re.search(r'"token":"([^"]*)"', payload)
    held = m.group(1) if m else ""
    if held != DIRECTORY_TOKEN:
        print(f"        (board holds {held!r}, expected {DIRECTORY_TOKEN!r})")
    ok &= check("a token split across packets is kept whole",
                held == DIRECTORY_TOKEN)
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
        # MAX_NODES, or one more when the sysop is on and visible. That is
        # publicNodes() working as designed: the sysop line is a real line,
        # so a board with the operator on it has one more and one busier.
        # The test used to assume MAX_NODES because the sysop always started
        # hidden, which stopped being true when the default flipped.
        ok &= check("it reports the lines, not who is on them",
                    rec.get("nodes") in (MAX_NODES, MAX_NODES + 1) and
                    isinstance(rec.get("busy"), int))
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


def test_files():
    """File areas on the card: the areas, one area's files, descriptions.

    Runs in both harness modes and checks a different thing in each. With no
    card the plugin is PF_SD and does not start at all, so FILES is not a
    command: that is the behaviour, and a board that quietly offered an empty
    file area on hardware that cannot hold files would be worse.
    """
    print("File areas")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build")
        return True

    sd = os.environ.get("BBS_SD_DIR", "")
    s = ansi_login("Browser")

    if not sd:
        s.buf.clear()
        s.send(b"files\r")
        ok = check("with no card there is no FILES command",
                   s.wait_for(b"Unknown command", 4))
        s.close()
        return ok

    # ---- FILES is a place, not a command ---------------------------------
    # It takes the session the way CHAT takes it: the plugin owns the keys,
    # the shell prompt is not underneath, and Q leaves. A list started from
    # inside it hands the session back to the plugin rather than dropping the
    # caller at the command line, which is what listEnded exists for.
    s.buf.clear()
    s.send(b"files\r")
    ok = check("FILES opens the file area", s.wait_for(b"File areas", 5))
    s.pump(0.8)
    raw_menu = bytes(s.buf)
    areas = plain(s.buf)
    ok &= check("with its own prompt, not the shell's",
                b"Q quits" in areas)

    # Entering is arriving somewhere, so the screen is cleared. Without this
    # each view prints under the last one and the subsystem reads as a
    # command that keeps talking rather than as a place. The complaint that
    # produced this test was "screen clears bro".
    ok &= check("and the screen was cleared on the way in",
                b"\x1b[2J" in raw_menu)

    # An ANSI caller points and shoots; the numbers stay as the fallback for
    # plain ASCII, which has neither a cursor nor reverse video.
    ok &= check("an ANSI caller is offered the cursor keys",
                b"cursor keys" in areas)
    ok &= check("and one area is highlighted to point at",
                b"\x1b[7m" in raw_menu)

    # Down then Enter opens whatever the bar is sitting on. The menu is a
    # grid, so down moves by a row rather than by one area, and the test
    # only needs to prove the pair agree: the area that opens is the area
    # that was lit. When drawing and the keys each worked the column count
    # out for themselves, they disagreed.
    s.buf.clear()
    s.send(b"\x1b[B")
    ok &= check("the cursor moves and the menu is redrawn",
                s.wait_for(b"File areas", 5))
    s.pump(0.6)
    moved = plain(s.buf)

    s.buf.clear()
    s.send(b"\x1b[A")            # back up to the first area
    s.pump(0.6)
    s.buf.clear()
    s.send(chr(13).encode())
    ok &= check("Enter opens the highlighted area", s.wait_for(b"C64 Downloads", 5))
    ok &= check("and that list leaves cleanly", leave_files(s))

    s.buf.clear()
    s.send(b"files\r")
    ok &= check("back in the file areas", s.wait_for(b"File areas", 5))
    s.pump(0.8)
    areas = plain(s.buf)

    # The board provides two areas of its own, Screens and Logs, so a fresh
    # card is not an empty room and a sysop has the screen folder and the
    # caller log to hand. Both are staff-only, and this caller is not staff,
    # so the interesting assertion here is the negative one: an area you may
    # not read is not listed, rather than listed and refused.
    ok &= check("an ordinary caller is not shown the board's own areas",
                b"Screens" not in areas and b"Logs" not in areas)

    # A single keypress picks an area: no Enter, because this is a menu.
    s.buf.clear()
    s.send(b"1")
    ok &= check("a digit opens that area", s.wait_for(b"C64 Downloads", 5))
    s.pump(0.8)
    inside = plain(s.buf)
    ok &= check("and the files are there", b"GAME.PRG" in inside)

    # ---- the same menu as staff ------------------------------------------
    # Proves the built-ins exist and are reachable rather than merely hidden
    # from everybody, which a permission bug would look exactly like.
    if PASSWORD:
        sy = ansi_login("Keeper")
        sy.buf.clear()
        sy.send(f"bye {PASSWORD}\r".encode())
        ok &= check("staff session on the sysop node", sy.wait_for(b"SysOp node", 5))
        sy.wait_for(b"Sysop", 3)
        sy.buf.clear()
        sy.send(b"files\r")
        ok &= check("staff open the file areas", sy.wait_for(b"File areas", 5))
        sy.pump(0.8)
        smenu = plain(sy.buf)
        ok &= check("the board's own Screens area is there for staff",
                    b"Screens" in smenu)
        ok &= check("and the Logs area", b"Logs" in smenu)
        ok &= check("both marked so a sysop can see they are shut",
                    smenu.count(b"(staff)") >= 2)
        sy.buf.clear()
        sy.send(b"q")
        sy.pump(0.5)
        sy.close()
    ok &= check("with the area's own prompt", b"L lists" in inside)

    # Q goes back one level, not all the way out. That distinction is the
    # thing a subsystem has that a command does not.
    s.buf.clear()
    s.send(b"q")
    ok &= check("Q goes back to the area menu", s.wait_for(b"File areas", 5))
    s.pump(0.6)
    s.buf.clear()
    s.send(b"q")
    ok &= check("and Q again leaves", s.wait_for(b"Out of files", 5))
    s.buf.clear()
    s.send(b"who\r")
    ok &= check("the shell has the session back",
                s.wait_for(b"Who's online", 5))
    ok &= check("and that list can be left cleanly", drain(s))

    # Back in for the rest of the checks.
    s.buf.clear()
    s.send(b"files\r")
    s.wait_for(b"File areas", 5)
    s.pump(0.8)
    areas = plain(s.buf)
    ok &= check("an area shows under the name the sysop gave it",
                b"C64 Downloads" in areas)
    ok &= check("and the path it maps to is not shown to callers",
                b"pub/c64" not in areas)
    ok &= check("a second area is listed too", b"Empty Area" in areas)

    # ---- inside an area --------------------------------------------------
    s.buf.clear()
    s.send(b"q")
    s.pump(0.4)
    s.buf.clear()
    s.send(b"files 1\r")
    ok &= check("FILES n enters and opens in one go",
                s.wait_for(b"C64 Downloads", 5))
    s.pump(0.8)
    listing = plain(s.buf)
    ok &= check("the files are listed", b"GAME.PRG" in listing and b"NOTES.TXT" in listing)
    ok &= check("with a size", b"K" in listing)
    ok &= check("and the description out of FILES.BBS",
                b"A game from the card" in listing)
    ok &= check("the description file is not itself listed as a file",
                b"FILES.BBS" not in listing)

    # ---- the board makes an area's folder itself -------------------------
    # area3 in the harness config points at a path that is deliberately not
    # on the card. Configuring an area should not mean pulling the card and
    # finding a PC, so the plugin creates the folder, parents and all, when
    # it starts.
    made = pathlib.Path(sd) / "made" / "bythebbs"
    ok &= check("an area folder that did not exist was created", made.is_dir())
    ok &= check("and it opens as an ordinary, empty area",
                enter_area(s, 3, b"Made By The BBS"))

    # ---- an area with its own levels -------------------------------------
    # area4 is read staff, write sysop. A plain caller must not see it in the
    # list and must not be able to open it by guessing the number, and the
    # refusal has to read the same as a number that is not an area at all: a
    # different message would turn FILES into a way to find out which numbers
    # are hiding something.
    ok &= check("a staff-only area is not in a plain caller's list",
                b"Screens" not in areas)
    # Guessing a number is a thing you do at the SECTION MENU. Inside a
    # section a digit picks a file, not an area, so this has to step back
    # up one level first. Q does that; a second Q would leave entirely.
    s.buf.clear()
    s.send(b"q")
    s.wait_for(b"File areas", 4)
    s.buf.clear()
    s.send(b"4")
    ok &= check("and cannot be opened by guessing its number",
                s.wait_for(b"No area by that number", 4))

    # ---- a number that is not an area ------------------------------------
    s.buf.clear()
    s.send(b"9")
    ok &= check("a number with no area is refused in the same words",
                s.wait_for(b"No area by that number", 4))

    # ---- an empty area is not an error -----------------------------------
    ok &= check("an empty area opens", enter_area(s, 2, b"Empty Area"))
    ok &= check("an empty area says so rather than looking broken",
                b"Nothing in here yet" in plain(s.buf))

    # ---- writing a description -------------------------------------------
    s.buf.clear()
    s.send(b"bye " + PASSWORD.encode() + b"\r")
    s.wait_for(b"Sysop", 4)
    s.buf.clear()
    s.send(b"files 1\r")
    s.wait_for(b"C64 Downloads", 5)
    s.pump(0.6)
    s.buf.clear()
    # The same area, from the sysop, is there. Leave the room first: BYE is
    # a shell command and the room owns the keys while a caller is in it.
    ok &= check("the file room can be left from any depth", leave_files(s))
    s.buf.clear()
    s.send(b"bye " + PASSWORD.encode() + b"\r")
    s.wait_for(b"Sysop", 5)
    s.buf.clear()
    s.send(b"files\r")
    s.wait_for(b"File areas", 5)
    s.pump(0.8)
    ok &= check("but the sysop sees it", b"Screens" in plain(s.buf))
    ok &= check("and it is marked as restricted", b"(staff)" in plain(s.buf))
    ok &= check("and that list can be left cleanly", drain(s))

    s.buf.clear()
    # Describing lives in the file manager, not on the command list.
    # D takes the number the listing showed, then the text.
    ok &= check("the section opens", enter_area(s, 1, b"C64 Downloads"))
    n = number_of(s, b"NOTES.TXT")
    ok &= check("NOTES.TXT is numbered in the listing", n is not None)
    if n is not None:
        s.buf.clear()
        s.send(b"d")
        ok &= check("D asks which number", s.wait_for(b"Describe which", 4))
        s.buf.clear()
        s.send(str(n).encode() + b"\r")
        ok &= check("then asks for the text", s.wait_for(b"Description", 4))
        s.buf.clear()
        s.send(b"Some notes I made\r")
        ok &= check("a description is written", s.wait_for(b"Described", 5))
        s.buf.clear()
        s.send(b"l")
        s.pump(1.2)
        ok &= check("and shows on the next listing",
                    b"Some notes I made" in plain(s.buf))
        ok &= check("without disturbing the one already there",
                    b"A game from the card" in plain(s.buf))
    ok &= check("out of the room once more", leave_files(s))

    # The file on the card has to be readable on a PC, which is the whole
    # reason the card is FAT32. Checked as a file, not through the BBS.
    desc = pathlib.Path(sd) / "pub" / "c64" / "FILES.BBS"
    body = desc.read_text()
    ok &= check("FILES.BBS is still plain text a laptop can read",
                "NOTES.TXT" in body and "GAME.PRG" in body)
    ok &= check("and has no leftover temp file beside it",
                not (pathlib.Path(sd) / "pub" / "c64" / "FILES.BBS.tmp").exists())

    # ---- a caller cannot walk out of the section -------------------------
    # There is no typed filename left to climb out with: describing,
    # downloading and erasing all take a number off the listing, and a
    # number can only ever name a file the section already showed. The
    # old "../../users.txt" case is not refused, it is unreachable, and
    # that is a better answer than a check. What can still be typed is a
    # number, so that is what gets tested.
    ok &= check("back in the section", enter_area(s, 1, b"C64 Downloads"))
    s.buf.clear()
    s.send(b"d")
    s.wait_for(b"Describe which", 4)
    s.buf.clear()
    s.send(b"999\r")
    ok &= check("a number with no file behind it is refused",
                s.wait_for(b"No file with that number", 4))

    # Nothing but Q leaves the section. Every action used to end at
    # Bbs::prompt, which is right for a command and wrong for a door:
    # erasing one file dropped the caller out to the shell, so clearing
    # three meant walking back in three times. Rob found it on the board
    # ("it shouldnt drop you out when you erase a file"), and the shape
    # is easy to reintroduce, so it is pinned here.
    ok &= check("a refusal leaves the caller where they were",
                b"Files>" in plain(s.buf))

    # An erase that actually happens, on a file made for the purpose. An
    # earlier version deleted NOTES.TXT, which the transfer tests count on,
    # and the symptom was four failures in a different test entirely.
    with open(os.path.join(sd, "pub", "c64", "ERASEME.TMP"), "wb") as f:
        f.write(b"throwaway")
    s.buf.clear()
    s.send(b"l")
    s.pump(1.0)
    gone = number_of(s, b"ERASEME.TMP")
    if gone is not None:
        s.buf.clear()
        s.send(b"e")
        s.wait_for(b"Erase which", 4)
        s.buf.clear()
        s.send(str(gone).encode() + b"\r")
        ok &= check("erase names the file before asking",
                    s.wait_for(b"Erase ERASEME.TMP?", 4))
        s.buf.clear()
        s.send(b"y")
        ok &= check("the file goes", s.wait_for(b"erased", 4))
        s.pump(0.6)
        ok &= check("and the caller is still in the section, not the shell",
                    b"Files>" in plain(s.buf))

    ok &= check("and leaves the caller in the section", leave_files(s))

    # ---- the caller log is mirrored, not moved ---------------------------
    # The ring on the logs partition stays the record LAST reads, because it
    # is the sysop's security log and must not depend on a card being seated.
    # The card gets a plain text copy that can run to months.
    caller = ansi_login("Mirrored")
    caller.send(b"bye\r")
    caller.pump(0.8)
    caller.close()
    time.sleep(0.8)

    logs = sorted((pathlib.Path(sd) / "logs").glob("calls-*.log")) if \
           (pathlib.Path(sd) / "logs").is_dir() else []
    ok &= check("a call is mirrored to a month file on the card", bool(logs))
    if logs:
        body = logs[-1].read_text(errors="replace")
        ok &= check("the mirrored line names the caller", "Mirrored" in body)
        ok &= check("and is plain text a laptop can read",
                    chr(9) in body and "node" in body)

    # And the ring is still the thing LAST reads, card or no card. Asked on
    # the session that is already the sysop: the sysop node holds one caller,
    # so a second session elevating is refused and dropped, which is correct
    # behaviour and cost this test a run to work out.
    s.buf.clear()
    s.send(b"last\r")
    ok &= check("LAST still reads the internal ring", s.wait_for(b"Mirrored", 5))
    s.pump(0.6)
    # LAST pages once the ring has a screenful in it, which it does by this
    # point in a full run but not when this test is run on its own.
    ok &= check("and the list can be left cleanly", drain(s))
    s.buf.clear()
    s.send(b"mem\r")
    s.pump(1.2)
    ok &= check("MEM shows the card's free space when one is mounted",
                b"Card free" in plain(s.buf))

    s.close()
    return ok


def test_binary():
    """Binary survives the wire, which is the prerequisite for any transfer.

    Two ways this fails, and both hide from an ordinary test:

    Telnet doubles 0xFF as IAC. Miss the unescaping and every 0xFF arrives
    twice. A text file contains none at all, so a transfer of one looks
    perfect and the first real download is quietly ruined.

    Telnet also normalises CR, dropping a 0x0A or 0x00 that follows a 0x0D.
    In a file those are data. Same shape of bug, different byte pair, and a
    test that only checks 0xFF will sail past it.

    So this sends every byte value, plus the pairs that trip the CR rule,
    plus a run of 0xFF, and checks the board counted what was sent and got
    the same arithmetic back.
    """
    print("Binary safety on the wire")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build")
        return True

    payload = bytes(range(256))                       # every value, 0xFF included
    payload += bytes([0x0D, 0x0A, 0x0D, 0x00, 0x0D])  # the CR pairs that get eaten
    payload += bytes([0xFF]) * 64                     # a run of IAC
    payload = payload.replace(bytes([0x03]), bytes([0x02]))   # 0x03 ends the echo

    s = ansi_login("Binary")
    s.buf.clear()
    s.send(b"raw\r")
    ok = check("raw mode starts", s.wait_for(b"Raw on.", 5))

    s.buf.clear()
    # IAC has to be doubled going up as well: this client is a telnet client.
    s.send(payload.replace(bytes([0xFF]), bytes([0xFF, 0xFF])))
    s.pump(1.5)
    s.send(bytes([0x03]))                             # end it
    ok &= check("raw mode ends", s.wait_for(b"RAW ", 5))

    seen = plain(s.buf)
    want_n = len(payload)
    want_sum = sum(payload)
    ok &= check("every byte arrived, none eaten by the CR rule",
                ("RAW %d bytes" % want_n).encode() in seen)
    ok &= check("and arrived unaltered, none doubled or dropped",
                ("sum %d" % want_sum).encode() in seen)

    s.buf.clear()
    s.send(b"who\r")
    ok &= check("the shell has the session back", s.wait_for(b"Who\'s online", 5))
    ok &= check("and that list can be left cleanly", drain(s))
    s.close()
    return ok



# ---------------------------------------------------------------------------
# XMODEM, both directions, against the real engine on the real socket.
#
# This is the test the transfer work needed and did not have. Everything
# XMODEM gets wrong is invisible to a test that only checks the board said
# "complete": a doubled 0xFF, a CR eaten by telnet's line-ending rule, a
# block boundary off by one. So this moves a payload built to trip exactly
# those, and compares bytes.
#
# The client here is deliberately a dumb, literal XMODEM implementation
# rather than a library: a library would paper over the same mistakes the
# board might make, and then both ends would agree on something wrong.
# ---------------------------------------------------------------------------
SOH, STX, EOT, ACK, NAK, CAN, SUB, CRCREQ = 1, 2, 4, 6, 0x15, 0x18, 0x1A, 0x43


def _crc16(d):
    crc = 0
    for b in d:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _unescape(buf):
    """Telnet IAC unescaping. 0xFF 0xFF is one data 0xFF."""
    _note_options(buf)
    out = bytearray()
    i = 0
    while i < len(buf):
        if buf[i] == 0xFF and i + 1 < len(buf) and buf[i + 1] == 0xFF:
            out.append(0xFF)
            i += 2
        else:
            out.append(buf[i])
            i += 1
    return bytes(out)


# Whether the board has negotiated RFC 856 TRANSMIT-BINARY with us.
#
# This client used to send raw bytes and double 0xFF, and nothing else, which
# is NOT what a telnet client does. In NVT ASCII a sender transmitting a bare
# CR must follow it with LF or NUL, and a real terminal obeys that. Because
# this client did not, every test passed against a board that never asked for
# binary mode, while SyncTERM NAKed every block of every upload.
#
# So the client now behaves like a terminal: it pads CRs until the board asks
# it not to. Remove the negotiation from the board and these tests fail, which
# is the point of having them.
# "on"     : the far end has agreed and we have stopped padding CRs.
# "refuse" : behave like a terminal that does not do telnet BINARY at all.
#            It keeps padding, for ever, which is correct NVT behaviour and
#            is what a board must cope with rather than assume away.
_binary = {"on": False, "refuse": False}


def _note_options(raw):
    """Watch the stream for the board asking for binary, either direction."""
    if _binary["refuse"]:
        return                      # we never agree, so we never stop padding
    i = 0
    while i + 2 < len(raw):
        if raw[i] == 0xFF and raw[i + 1] in (0xFB, 0xFD) and raw[i + 2] == 0x00:
            _binary["on"] = True
            i += 3
        elif raw[i] == 0xFF and raw[i + 1] in (0xFC, 0xFE) and raw[i + 2] == 0x00:
            _binary["on"] = False
            i += 3
        else:
            i += 1


def _escape(data):
    out = data.replace(b"\xff", b"\xff\xff")
    if not _binary["on"]:
        # NVT ASCII: a bare CR is followed by NUL. This is the byte that
        # corrupted every upload from a real terminal.
        out = out.replace(b"\r", b"\r\x00")
    return out


def xmodem_receive(s, timeout=25.0):
    """Receive a file the board is sending. Returns the payload, padding and all."""
    s.buf.clear()
    got = bytearray()
    blk = 1
    s.send(bytes([CRCREQ]))                     # ask for CRC-16
    end = time.time() + timeout
    pending = bytearray()
    while time.time() < end:
        s.pump(0.15)
        pending += _unescape(bytes(s.buf))
        s.buf.clear()
        while pending:
            if pending[0] == EOT:
                pending.pop(0)
                s.send(bytes([NAK]))            # the engine wants one NAK first
                end = time.time() + timeout
                # the second EOT is the real one
                s.pump(0.5)
                pending += _unescape(bytes(s.buf))
                s.buf.clear()
                if pending and pending[0] == EOT:
                    pending.pop(0)
                s.send(bytes([ACK]))
                return bytes(got)
            if pending[0] in (SOH, STX):
                size = 128 if pending[0] == SOH else 1024
                need = 3 + size + 2
                if len(pending) < need:
                    break
                frame = bytes(pending[:need])
                del pending[:need]
                num, inv, body, chk = frame[1], frame[2], frame[3:3 + size], frame[-2:]
                if num != (inv ^ 0xFF) or _crc16(body) != (chk[0] << 8 | chk[1]):
                    s.send(bytes([NAK]))
                    continue
                if num == (blk & 0xFF):
                    got += body
                    blk += 1
                s.send(bytes([ACK]))
                end = time.time() + timeout
            else:
                pending.pop(0)                  # noise before the first block
    return bytes(got)


def xmodem_send(s, data, timeout=25.0):
    """Send a file to the board. Waits for its 'C' or NAK first."""
    _note_options(bytes(s.buf))
    end = time.time() + timeout
    crc = None
    while time.time() < end and crc is None:
        s.pump(0.2)
        seen = _unescape(bytes(s.buf))
        for b in seen:
            if b == CRCREQ:
                crc = True
                break
            if b == NAK:
                crc = False
                break
        s.buf.clear()
    if crc is None:
        return False

    pos, blk = 0, 1
    while pos < len(data):
        chunk = data[pos:pos + 128]
        chunk = chunk + bytes([SUB]) * (128 - len(chunk))
        body = bytes([SOH, blk & 0xFF, (blk & 0xFF) ^ 0xFF]) + chunk
        body += (bytes([_crc16(chunk) >> 8, _crc16(chunk) & 0xFF]) if crc
                 else bytes([sum(chunk) & 0xFF]))
        for _ in range(6):
            s.buf.clear()
            s.send(_escape(body))
            done = time.time() + 6
            ans = None
            while time.time() < done and ans is None:
                s.pump(0.1)
                for b in _unescape(bytes(s.buf)):
                    if b in (ACK, NAK, CAN):
                        ans = b
                        break
                s.buf.clear()
            if ans == ACK:
                break
            if ans == CAN:
                return False
        else:
            return False
        pos += 128
        blk += 1

    s.buf.clear()
    s.send(bytes([EOT]))
    s.pump(0.5)
    s.send(bytes([EOT]))                        # the engine NAKs the first
    s.pump(0.5)
    return True


def test_xfer():
    """Download and upload, end to end, with the approval step in between."""
    print("File transfer: XMODEM down, XMODEM up, approval")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build")
        return True
    sd = os.environ.get("BBS_SD_DIR", "")
    if not sd:
        print("  SKIP  needs a card")
        return True
    if not PASSWORD:
        print("  SKIP  no sysop_password")
        return True

    # A payload built to break the two things that actually break: 0xFF is
    # telnet's escape and CR followed by LF or NUL is what the line-ending
    # rule used to eat.
    payload = bytes(range(256)) * 3
    payload += bytes([0x0D, 0x0A, 0x0D, 0x00, 0x0D]) * 10
    payload += bytes([0xFF]) * 100
    area = os.path.join(sd, "pub", "c64")
    os.makedirs(area, exist_ok=True)
    with open(os.path.join(area, "BINTEST.BIN"), "wb") as f:
        f.write(payload)

    s = ansi_login("Mover")
    ok = check("area opens for the transfer", enter_area(s, 1, b"C64 Downloads"))
    ok &= check("and the prompt names the section it is in",
                b"[S1]" in plain(s.buf))
    ok &= check("and says a number downloads",
                b"number downloads" in plain(s.buf) or b"number gets" in plain(s.buf))

    # ---- download --------------------------------------------------------
    # Explicitly plain XMODEM: YMODEM is the default now, and this test keeps
    # the older protocol covered rather than quietly stopping when the
    # default moved.
    # Find it by name. readdir order is not guaranteed and earlier tests add
    # and remove files, so a hardcoded number is a test that breaks for
    # reasons that have nothing to do with what it checks.
    ok &= check("the listing numbers the files", b" 1 " in plain(s.buf))
    bn = number_of(s, b"BINTEST.BIN")
    ok &= check("BINTEST.BIN is numbered in the listing", bn is not None)
    if bn is None:
        s.close()
        return False
    # X in the dialog asks for plain XMODEM, which this test keeps covered
    # now that YMODEM is the default.
    ok &= check("download announces itself",
                file_num(s, bn, b"Start your XMODEM receive", proto=b"x"))
    got = xmodem_receive(s)
    ok &= check("every byte of the file arrived",
                got[:len(payload)] == payload)
    ok &= check("and only XMODEM's own padding followed",
                set(got[len(payload):]) <= {SUB})
    ok &= check("the board says the download finished", s.wait_for(b"Download complete", 8))
    # And leaves the caller standing in the area they were in, so the next
    # file is one keypress away rather than a walk back down from the menu.
    ok &= check("and leaves the caller at the section prompt",
                s.wait_for(b"Files>", 4))

    # ---- an area that is not yours to upload to --------------------------
    # area1 sets no levels, so upload falls back to the plugin's write, which
    # is staff. An ordinary caller is not offered U at all there, which is
    # better than offering it and refusing: the prompt only shows what you
    # can actually do.
    ok &= check("a staff-only section does not offer an ordinary caller U",
                b"U upload" not in plain(s.buf) and b"U send" not in plain(s.buf))

    # ---- upload, into an area that allows it -----------------------------
    up = bytes([0xFF, 0x0D, 0x0A, 0x1A, 0x00]) * 40
    drop = os.path.join(sd, "pub", "drop")
    ok &= check("the drop box opens", enter_area(s, 5, b"Drop Box"))
    ok &= check("and offers U there",
                b"U upload" in plain(s.buf) or b"U send" in plain(s.buf))
    ok &= check("and names section 5 in its prompt", b"[S5]" in plain(s.buf))
    ok &= check("upload is offered",
                area_key(s, b"u", "SENTUP.BIN", b"Start your XMODEM send"))
    ok &= check("it says approval is needed first",
                b"waits for staff approval" in plain(s.buf))
    ok &= check("the board took the file", xmodem_send(s, up))
    ok &= check("and says so", s.wait_for(b"Upload complete", 10))

    # It is NOT in the area until staff approve it. This is the assertion the
    # whole staging-folder design exists for.
    ok &= check("the upload is not on the card in the area itself",
                not os.path.exists(os.path.join(drop, "SENTUP.BIN")))
    ok &= check("it is waiting in the staging folder",
                os.path.exists(os.path.join(drop, ".pending", "SENTUP.BIN")))
    with open(os.path.join(drop, ".pending", "SENTUP.BIN"), "rb") as f:
        landed = f.read()
    ok &= check("and what landed is what was sent", landed[:len(up)] == up)
    s.close()

    # ---- approval --------------------------------------------------------
    sy = ansi_login("Keeper2")
    sy.buf.clear()
    sy.send(f"bye {PASSWORD}\r".encode())
    ok &= check("staff reach the sysop node", sy.wait_for(b"SysOp node", 5))
    sy.wait_for(b"Sysop", 3)
    sy.buf.clear()
    # Approval lives in the file manager now, not on the command list. A
    # staff member walks into the section, P shows what is waiting with a
    # number beside it, and A takes that number. Nobody retypes a
    # filename off their own screen.
    ok &= check("staff can open the drop box", enter_area(sy, 5, b"Drop Box"))
    sy.buf.clear()
    sy.send(b"p")
    ok &= check("P lists what is waiting", sy.wait_for(b"SENTUP.BIN", 5))
    ok &= check("and numbers it", b" 1 SENTUP.BIN" in plain(sy.buf))
    ok &= check("approving by number says it is live",
                area_key(sy, b"a", "1", b"is live"))
    ok &= check("and the file is now in the area",
                os.path.exists(os.path.join(drop, "SENTUP.BIN")))
    ok &= check("and gone from the staging folder",
                not os.path.exists(os.path.join(drop, ".pending", "SENTUP.BIN")))
    sy.close()
    return ok



def enter_area(s, n, name, secs=6):
    """Open an area and come to rest at its prompt.

    DOWNLOAD and UPLOAD are not shell commands. FILES is a door, so a
    transfer is something done in the room: the area's own prompt offers
    D and U, and a key there opens a one-line question. A test that typed
    "download x" at the shell used to work and now correctly does nothing,
    which is the point of the change.
    """
    # Work out where we are before trying to get somewhere else, because
    # the two places take input completely differently. "files 5" typed
    # at a section prompt is not a command, it is seven keypresses, and
    # "e" there is erase. But leave_files at the SHELL is just as bad:
    # it types "qqqq" onto the command line, so the next command becomes
    # "qqqqfiles 5" and goes nowhere. That is exactly how this failed.
    #
    # Enter is the safe probe: at the shell it redraws the prompt, and
    # inside the subsystem it redraws the files prompt.
    s.buf.clear()
    s.send(b"\r")
    s.pump(0.5)
    if b"Files>" in plain(s.buf) or b"Files:" in plain(s.buf):
        leave_files(s)
    s.buf.clear()
    s.send(("files %d" % n).encode() + b"\r")
    if not s.wait_for(name, secs):
        return False
    drain(s)                       # page to the end of the listing
    return b"Files>" in plain(s.buf)


def number_of(s, name):
    """The number the listing gave a file, or None.

    readdir order is not guaranteed, so a test that hardcodes "file 3"
    is a test that breaks the day somebody adds a file. Read the number
    off the screen the way a caller does.
    """
    for line in plain(s.buf).split(b"\n"):
        if name in line:
            head = line.strip().split(b" ", 1)[0]
            if head.isdigit():
                return int(head)
    return None


def file_num(s, n, wait, proto=b"y", secs=10):
    """Pick a numbered file at the section prompt and answer the dialog.

    The flow a caller sees: press the number, Enter, then one key in the
    "Download NAME? [Y]es [X]modem [N]o" dialog. The dialog names the file
    rather than repeating the number, because a number is easy to miscount
    off a listing and a filename is not.
    """
    s.buf.clear()
    s.send(str(n).encode())
    if not s.wait_for(b"File number", 4):
        return False
    s.buf.clear()
    s.send(b"\r")
    if not s.wait_for(b"Download", 5):
        return False
    s.buf.clear()
    s.send(proto)
    return s.wait_for(wait, secs)


def area_key(s, key, answer, wait, secs=8):
    """Press a letter in the section, answer its typed question."""
    s.buf.clear()
    s.send(key)
    if not s.wait_for(b"?", 4) and not s.wait_for(b":", 2):
        return False
    s.buf.clear()
    s.send(answer.encode() + b"\r")
    return s.wait_for(wait, secs)


def ymodem_receive(s, timeout=25.0):
    """Receive one file by YMODEM. Returns (name, size, payload).

    Written against the protocol rather than against our engine, on purpose:
    a client that mirrors the implementation agrees with it even when both
    are wrong. The payload is returned untrimmed so the test can prove the
    trimming happened at the right end.
    """
    s.buf.clear()
    pending = bytearray()
    name, size = None, 0
    got = bytearray()
    blk = 1
    stage = "hdr"
    s.send(bytes([CRCREQ]))
    end = time.time() + timeout

    while time.time() < end:
        s.pump(0.15)
        pending += _unescape(bytes(s.buf))
        s.buf.clear()
        progressed = True
        while pending and progressed:
            progressed = False
            if pending[0] in (SOH, STX):
                size_b = 128 if pending[0] == SOH else 1024
                need = 3 + size_b + 2
                if len(pending) < need:
                    break
                frame = bytes(pending[:need])
                del pending[:need]
                progressed = True
                num, inv = frame[1], frame[2]
                body, chk = frame[3:3 + size_b], frame[-2:]
                if num != (inv ^ 0xFF) or _crc16(body) != (chk[0] << 8 | chk[1]):
                    s.send(bytes([NAK]))
                    continue
                if stage == "hdr" and num == 0:
                    if body[0] == 0:                 # empty block 0: batch over
                        s.send(bytes([ACK]))
                        return name, size, bytes(got)
                    nul = body.index(0)
                    name = body[:nul].decode("ascii", "replace")
                    rest = body[nul + 1:].split(b"\x00", 1)[0].split()
                    size = int(rest[0]) if rest else 0
                    s.send(bytes([ACK]))
                    s.send(bytes([CRCREQ]))          # now send me the file
                    stage = "data"
                    end = time.time() + timeout
                    continue
                if stage == "trailer" and num == 0:
                    s.send(bytes([ACK]))
                    return name, size, bytes(got)
                if num == (blk & 0xFF):
                    got += body
                    blk += 1
                s.send(bytes([ACK]))
                end = time.time() + timeout
            elif pending[0] == EOT:
                pending.pop(0)
                progressed = True
                s.send(bytes([NAK]))                 # the engine wants one NAK
                s.pump(0.4)
                pending += _unescape(bytes(s.buf))
                s.buf.clear()
                if pending and pending[0] == EOT:
                    pending.pop(0)
                s.send(bytes([ACK]))
                s.send(bytes([CRCREQ]))              # ask for the trailer
                stage = "trailer"
                end = time.time() + timeout
            else:
                pending.pop(0)
                progressed = True
    return name, size, bytes(got)


# Everything the protocol client saw while it was draining the socket. The
# board prints "Upload complete" the moment it leaves binary mode, which is
# while this client is still watching for the last ACK, so without keeping a
# copy the test looks for a message that was thrown away a moment earlier.
_seen = bytearray()


def ymodem_send(s, fname, data, timeout=25.0):
    """Send one file by YMODEM, header block and closing block included."""
    _note_options(bytes(s.buf))
    def wait_for_byte(want, secs=8.0):
        stop = time.time() + secs
        while time.time() < stop:
            s.pump(0.1)
            raw = bytes(s.buf)
            _seen.extend(raw)
            seen = _unescape(raw)
            s.buf.clear()
            for b in seen:
                if b in want:
                    return b
        return None

    def block(num, payload):
        body = bytes([SOH, num & 0xFF, (num & 0xFF) ^ 0xFF]) + payload
        c = _crc16(payload)
        return body + bytes([c >> 8, c & 0xFF])

    if wait_for_byte({CRCREQ, NAK}, timeout) is None:
        return False

    hdr = fname.encode() + b"\x00" + str(len(data)).encode()
    hdr = hdr + bytes(128 - len(hdr))
    s.buf.clear()
    s.send(_escape(block(0, hdr)))
    if wait_for_byte({ACK}) != ACK:
        return False
    if wait_for_byte({CRCREQ}) != CRCREQ:
        return False

    pos, num = 0, 1
    while pos < len(data):
        chunk = data[pos:pos + 128]
        chunk = chunk + bytes([SUB]) * (128 - len(chunk))
        s.buf.clear()
        s.send(_escape(block(num, chunk)))
        if wait_for_byte({ACK, NAK, CAN}) != ACK:
            return False
        pos += 128
        num += 1

    s.buf.clear()
    s.send(bytes([EOT]))
    wait_for_byte({NAK, ACK}, 4)
    s.send(bytes([EOT]))
    wait_for_byte({ACK}, 4)
    if wait_for_byte({CRCREQ}, 6) != CRCREQ:
        return True                       # some senders stop here; not fatal
    s.buf.clear()
    s.send(_escape(block(0, bytes(128))))  # empty header closes the batch
    wait_for_byte({ACK}, 4)
    return True


def settle_after_transfer(s):
    """Clear the shell line of anything the protocol left on it.

    A transfer ends with the client still speaking protocol: the closing ACK,
    and under YMODEM a 'C' asking for the batch trailer. Whatever is in
    flight when the board leaves raw mode lands at the command prompt as
    typed characters, so the next command becomes "Cwho" and goes nowhere.
    This is the test client's mess rather than the board's, and a real
    terminal makes it too: sending a bare Enter first is what a person does
    without noticing.
    """
    s.pump(0.4)
    s.buf.clear()
    s.send(chr(13).encode())
    s.pump(0.4)
    s.buf.clear()


def test_upload_no_binary():
    """A terminal that will not do telnet BINARY must still be able to upload.

    This is the case that broke on real hardware while every test passed.
    The board asked for RFC 856 binary and then assumed it had got it, so it
    stopped stripping the NUL a terminal inserts after a bare CR. A terminal
    that never agreed went on inserting one, every block carrying a 0x0D
    arrived a byte long, and the board NAKed all of them.

    The test client used to honour the request, which is exactly why it
    never saw this. Here it refuses, the way the real terminal evidently
    does, and the upload still has to work: the board must keep NVT input
    handling until the far end actually says WILL BINARY.
    """
    print("Upload from a terminal that refuses telnet BINARY")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build")
        return True
    sd = os.environ.get("BBS_SD_DIR", "")
    if not sd:
        print("  SKIP  needs a card")
        return True

    drop = os.path.join(sd, "pub", "drop")
    # Deliberately full of the byte that triggers the padding.
    body = (bytes([0x0D]) * 8 + bytes(range(64)) + bytes([0x0D, 0x0A]) * 16) * 2

    _binary["refuse"] = True
    _binary["on"] = False
    try:
        s = ansi_login("NoBin")
        ok = check("the drop box opens", enter_area(s, 5, b"Drop Box"))
        ok &= check("upload is offered",
                    area_key(s, b"u", "NOBIN.BIN", b"Start your XMODEM send"))
        ok &= check("the board took it from a padding terminal",
                    xmodem_send(s, body))
        ok &= check("and says so", s.wait_for(b"Upload complete", 12))
        landed = os.path.join(drop, ".pending", "NOBIN.BIN")
        ok &= check("it is waiting in the staging folder", os.path.exists(landed))
        if os.path.exists(landed):
            with open(landed, "rb") as f:
                got = f.read()
            ok &= check("and every CR survived intact", got[:len(body)] == body)
            os.remove(landed)
        s.close()
    finally:
        _binary["refuse"] = False
        _binary["on"] = False
    return ok


def test_ymodem():
    """YMODEM both ways, and the thing YMODEM exists for: no padding."""
    print("File transfer: YMODEM, exact sizes")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build")
        return True
    sd = os.environ.get("BBS_SD_DIR", "")
    if not sd:
        print("  SKIP  needs a card")
        return True
    if not PASSWORD:
        print("  SKIP  no sysop_password")
        return True

    # Deliberately not a multiple of 128, so padding would show.
    body = bytes(range(256)) * 2 + bytes([0x0D, 0x0A, 0x00, 0xFF]) * 5 + b"tail"
    assert len(body) % 128 != 0
    area = os.path.join(sd, "pub", "c64")
    os.makedirs(area, exist_ok=True)
    with open(os.path.join(area, "EXACT.BIN"), "wb") as f:
        f.write(body)

    s = ansi_login("Yoda")
    ok = check("area opens", enter_area(s, 1, b"C64 Downloads"))

    # ---- download by YMODEM ---------------------------------------------
    # EXACT.BIN is the fourth file in this area once the xfer test has run,
    # so find it by name in the listing rather than by a fixed number.
    listing = plain(s.buf)
    num = None
    for line in listing.split(b"\n"):
        if b"EXACT.BIN" in line:
            head = line.strip().split(b" ", 1)[0]
            if head.isdigit():
                num = int(head)
            break
    ok &= check("EXACT.BIN is numbered in the listing", num is not None)
    if num is None:
        s.close()
        return False
    ok &= check("YMODEM is the default",
                file_num(s, num, b"Start your YMODEM receive"))
    name, size, got = ymodem_receive(s)
    ok &= check("block 0 carried the filename", name == "EXACT.BIN")
    ok &= check("block 0 carried the exact size", size == len(body))
    ok &= check("the bytes are right", got[:len(body)] == body)
    # The board is the sender here, so the last block is padded on the wire
    # by necessity: XMODEM framing has no short block. What YMODEM buys is
    # that block 0 told the receiver exactly where to stop, so trimming to
    # the stated size reproduces the file. That is the assertion worth
    # making, and the earlier one (len(got) >= len(body)) was always true
    # and proved nothing at all.
    ok &= check("and block 0's size trims the padding back to the original",
                got[:size] == body)
    ok &= check("the board agrees it finished", s.wait_for(b"Download complete", 8))
    settle_after_transfer(s)

    # ---- XMODEM on request still works, and still pads --------------------
    # Proves the two are genuinely different rather than the test lying.
    # Re-open the area rather than assuming the session is still where the
    # last step left it. A transfer leaves the caller at the area prompt,
    # but a protocol client can leave a stray byte behind, and a stray byte
    # at an area prompt is a keypress.
    ok &= check("back in the section", enter_area(s, 1, b"C64 Downloads"))
    ok &= check("X asks for plain XMODEM",
                file_num(s, num, b"Start your XMODEM receive", proto=b"x"))
    xgot = xmodem_receive(s)
    ok &= check("XMODEM carries the same bytes", xgot[:len(body)] == body)
    ok &= check("but pads the tail, which is why YMODEM exists",
                len(xgot) > len(body) and set(xgot[len(body):]) <= {SUB})
    s.wait_for(b"Download complete", 8)
    settle_after_transfer(s)

    # ---- upload by YMODEM, no filename typed ------------------------------
    drop = os.path.join(sd, "pub", "drop")
    up = bytes([0xFF, 0x0D, 0x0A, 0x1A]) * 33 + b"END"
    ok &= check("the drop box opens", enter_area(s, 5, b"Drop Box"))
    # An empty answer is the YMODEM case: the sending terminal already knows
    # the filename, so there is no reason to make somebody type it.
    ok &= check("an empty answer asks for YMODEM",
                area_key(s, b"u", "", b"Start your YMODEM send"))
    _seen.clear()
    ok &= check("the board took it", ymodem_send(s, "FROMTERM.BIN", up))
    s.pump(1.0)
    _seen.extend(bytes(s.buf))
    ok &= check("and says so", b"Upload complete" in plain(bytes(_seen)))
    ok &= check("named from block 0, not typed",
                os.path.exists(os.path.join(drop, ".pending", "FROMTERM.BIN")))
    with open(os.path.join(drop, ".pending", "FROMTERM.BIN"), "rb") as f:
        landed = f.read()
    ok &= check("and it landed byte-exact, trailing 0x1A and all",
                landed == up)
    ok &= check("still not visible in the area until approved",
                not os.path.exists(os.path.join(drop, "FROMTERM.BIN")))
    s.close()
    return ok


def test_sd():
    """The SD card, mounted and not.

    Both halves matter and they are different code. Without a card the board
    has to be a complete board that says so plainly; with one, the screens on
    the card have to win over the stock set without replacing it. The harness
    decides which by whether BBS_SD_DIR was set for the server, so this test
    asks the board what it has rather than assuming.
    """
    print("SD card")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build")
        return True

    s = ansi_login("Carder")
    s.send(b"bye " + PASSWORD.encode() + b"\r")
    s.wait_for(b"Sysop", 4)
    s.buf.clear()

    s.send(b"sd\r")
    s.wait_for(b"SD card", 4)
    s.pump(0.4)
    shown = plain(s.buf)
    mounted = b"no card" not in shown

    ok = check("SD reports the card either way", b"SD card" in shown)

    if not mounted:
        # The board with no card is the one most people will have, and it is
        # a supported configuration rather than a broken one. It has to say
        # which pins it tried, because "no card found" with no pin numbers
        # sends somebody to re-seat a card that was never the problem.
        ok &= check("it says there is no card", b"no card" in shown)
        ok &= check("and which pins it tried", b"CS 5" in shown and b"MOSI 23" in shown)
        ok &= check("and that the board is fine without one", b"runs fine without" in shown)

        s.buf.clear()
        s.send(b"sd unmount\r")
        ok &= check("unmounting nothing is not an error",
                    s.wait_for(b"No card is mounted", 4))

        # The claim the whole design rests on: no card, complete board.
        s.buf.clear()
        s.send(b"who\r")
        ok &= check("the board still works with no card", s.wait_for(b"Who's online", 4))
    else:
        ok &= check("it names the mount point", b"/" in shown)
        ok &= check("and reports free space", b"MB free of" in shown)
        ok &= check("and where screens come from", b"screens:" in shown)

        # The override is the part worth testing: a file on the card wins,
        # and a file only the stock set has still plays. Replacing rather
        # than overriding would be a silent way to lose every screen a
        # sysop did not think to copy.
        sd = pathlib.Path(os.environ["BBS_SD_DIR"])
        (sd / "screens").mkdir(parents=True, exist_ok=True)

        # The caller is ANSI, so .ans is the format it should get. A card
        # file of the same format wins.
        (sd / "screens" / "about.ans").write_text("CARD ABOUT OVERRIDE\n")
        s.buf.clear()
        s.send(b"about\r")
        s.pump(1.2)
        ok &= check("a screen on the card overrides the stock one",
                    b"CARD ABOUT OVERRIDE" in plain(s.buf))

        # One run of privacy, two things checked from the same output, because
        # privacy pauses at a page break and a second `privacy` typed while it
        # is paused has its "p" eaten as a page key. Two commands here is what
        # made this test fail against correct code.
        #
        # The card gets a privacy.asc. An ANSI caller should still get the
        # flash privacy.ans, because the lookup is extension-major: for each
        # format in the terminal's order, card first and then flash. The first
        # version searched per directory, so one .asc dropped on a card to try
        # the override silently took every C64 caller off .p40 and every ANSI
        # caller off .ans, and it looked like it had worked.
        (sd / "screens" / "privacy.asc").write_text("CARD PRIVACY ASCII\n")
        s.buf.clear()
        s.send(b"privacy\r")
        s.pump(1.2)
        played = plain(s.buf)
        ok &= check("a screen only flash has in this format still plays",
                    b"THE RISKS OF AN UNENCRYPTED BBS" in played)
        ok &= check("and a card .asc does not beat a flash .ans",
                    b"CARD PRIVACY ASCII" not in played)

        # privacy is four pages and pauses at "Press SPACE to continue". That
        # is the screen player's page break, not the list pager's [More], and
        # a key there advances to the next page rather than stopping. Leaving
        # it paused fed "s" and "d" of the next command to the screen as page
        # keys and left "unmount" at the prompt as an unknown command, so the
        # card never unmounted and this test called working code broken.
        # Page to the end rather than assuming how many pages there are.
        ok &= check("the privacy screen pauses for the reader",
                    b"Press SPACE to continue" in played)
        for _ in range(10):
            if b"Sysop:" in plain(s.buf):
                break
            s.send(b" ")
            s.pump(0.5)
        ok &= check("and paging through it reaches the prompt", b"Sysop:" in plain(s.buf))

        # A second caller parked mid-screen ON THE CARD while the sysop
        # unmounts. A ScreenPlayer holds the file open for as long as the
        # screen plays, and at a page break that is until a key arrives,
        # possibly never. Unmounting under that leaves a descriptor into a
        # torn-down filesystem, and on the board the VFS slot is reused by
        # the next mount, so the stale handle can come back as somebody
        # else's file rather than as an error. The caller has to be let go
        # first, and told why, rather than left on a half-drawn screen.
        #
        # privacy.ans, because the reader is an ANSI caller and the lookup is
        # extension-major: a .asc on the card would lose to the flash .ans
        # and the reader would never be holding a card file at all. The form
        # feed is what makes it stop and wait.
        (sd / "screens" / "privacy.ans").write_text(
            "CARD PRIVACY PAGE ONE" + chr(10) + chr(12) +
            "CARD PRIVACY PAGE TWO" + chr(10))
        reader = ansi_login("Reader")
        reader.send(b"privacy\r")
        onCard = reader.wait_for(b"CARD PRIVACY PAGE ONE", 6)
        ok &= check("a second caller is reading a screen off the card", onCard)
        ok &= check("and is parked at its page break",
                    reader.wait_for(b"Press SPACE to continue", 4))

        s.buf.clear()
        s.send(b"sd unmount\r")
        ok &= check("the unmount completes", s.wait_for(b"safe to pull", 6))
        reader.pump(1.5)
        ok &= check("the parked caller is told the card went away",
                    b"card was removed" in plain(reader.buf))
        reader.buf.clear()
        reader.send(b"who\r")
        ok &= check("and is back at a working prompt, not stuck",
                    reader.wait_for(b"Who's online", 6))
        reader.close()

        s.buf.clear()
        s.send(b"sd mount\r")
        ok &= check("the card mounts again after that", s.wait_for(b"Mounted", 6))
        (sd / "screens" / "privacy.ans").unlink()

        # Unmount and the board falls back rather than losing its screens.
        s.buf.clear()
        s.send(b"sd unmount\r")
        s.wait_for(b"safe to pull", 4)
        s.buf.clear()
        s.send(b"about\r")
        s.pump(1.2)
        after = plain(s.buf)
        ok &= check("pulling the card falls back to the stock screen",
                    b"CARD ABOUT OVERRIDE" not in after and b"v0." in after)
        s.buf.clear()
        s.send(b"sd mount\r")
        ok &= check("and it mounts again", s.wait_for(b"Mounted", 5))

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
               test_binary(), test_sd(), test_files(), test_xfer(), test_upload_no_binary(), test_ymodem(),
               test_config_areas(), test_partitions()]
    if "--backup" in FLAGS:
        results.append(test_backup())
    if "--ban" in FLAGS:
        results.append(test_ban())
    print("ALL PASS" if all(results) else "FAILURES")
    sys.exit(0 if all(results) else 1)
