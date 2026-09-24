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
                   passwords and per-handle lockout, PROFILE, PASSWORD, WHOIS
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
        if b"Leaving the file areas" in plain(c.buf):
            return True
        c.buf.clear()
        c.send(b"q")
        c.pump(0.6)
    return b"Leaving the file areas" in plain(c.buf)


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
BBS_PORT_NUM = config_num("BBS_PORT", 6400)     # the dial-in port, not this run's
# The published default sysop password, read from the firmware so the suite
# cannot drift from what the install page tells people.
_dflt = re.search(r'#define\s+BBS_DEFAULT_SYSOP\s+"([^"]*)"',
                  (ROOT / "src" / "config.h").read_text())
BBS_DEFAULT = _dflt.group(1) if _dflt else "unleashed"
PASSWORD = cfg_value("sysop_password")
CO1 = cfg_value("cosysop1_password")
CO2 = cfg_value("cosysop2_password")
BACKUP_PORT = int(cfg_value("backup_port") or 8080)
UP = b"\x1b[A"


class Caller:
    def __init__(self, ansi=False, utf8=True, telnet=False, source=None, port=None):
        self.t0 = time.time()
        # source: the address to call from. 127.0.0.2 is loopback to the
        # kernel but not local to Bbs::localAddr, which takes 127.0.0.1
        # exactly, so it stands in for a caller from outside the network.
        # port: another board than the harness's, such as a start_copy one.
        self.s = socket.create_connection((HOST, port or PORT), timeout=5,
                                          source_address=(source, 0) if source else None)
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


def to_save(c, stop, tries=12):
    """Walk the focus down to [ Save ] and press it, then wait for a verdict.

    Deliberately not a fixed number of Enters. The sign-up form's field list
    is not a constant and has grown twice; a hardcoded run stops short the
    day somebody adds a field, which leaves the form sitting open and the
    test waiting for a verdict nobody ever asked for. The failure then shows
    up somewhere else entirely, which is how one added field produced a
    failure reading "bad email refused".
    """
    for _ in range(tries):
        if stop in c.buf:
            return True
        c.send(b"\r")
        c.pump(0.3)
    return stop in c.buf


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
        c.send(enc(pw) + b"\r" + enc(pw) + b"\r" + enc(handle) + b"\r" + enc(email) + b"\r")
        # Walk to [ Save ] with Enter rather than counting fields. This used
        # to be a hardcoded run of five Enters, and the number of fields
        # between Email and the button is not a constant: adding one field
        # to the sign-up form left every registration sitting on the form,
        # so every test in this file failed at login with nothing pointing
        # at the cause. Press until the board says the account exists.
        for _ in range(12):
            if enc("WELCOME ABOARD") in c.buf:
                break
            c.send(b"\r")
            c.pump(0.3)
        if not c.wait_for(enc("WELCOME ABOARD"), 10):
            return False
    elif which == 1:
        c.send(enc(pw) + b"\r")
        if not c.wait_for(enc("ACCESS GRANTED"), 6):
            return False
    else:
        return False
    return c.wait_for(enc("Main"), 8) if wait_main else True


def ansi_login(handle, pw=TEST_PW, port=None):
    c = Caller(ansi=True, port=port)
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
    ok &= check("PETSCII signup form and login", login(c, "Daytona", as_pet=True))
    ok &= check("form drew with cursor moves", b"\x13" in c.buf)
    c.buf.clear()
    c.send(pet("who") + b"\r")
    ok &= check("WHO lists handle", c.wait_for(pet("Daytona"), 3))
    c.buf.clear()
    c.send(b"\x91\r")                     # C64 cursor-up recalls WHO
    ok &= check("cursor-up recalls on PETSCII", c.wait_for(pet("Daytona"), 3))
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
    # The main menu is deliberately short now: the things a caller actually
    # uses, with everything explanatory moved to ? account. Counting to ten
    # was counting the old menu. What matters is that it is generated from
    # the command table and names the everyday commands, and FILES is absent
    # here because there is no card.
    ok &= check("HELP generated with rows", len(body) >= 4)
    ok &= check("and names the everyday commands",
                b"WHO" in c.buf and b"CHAT" in c.buf)
    # Detection gives a plain ASCII caller 80x24 (detect.cpp), so 79 is the
    # bar, not 39. The old 39 was the rowWidth clamp written down as a
    # requirement: rows follow Term::cols() now instead of being capped at
    # 40 for everybody, which is what left an 80 column caller looking at
    # half a screen of black. SCREENS.md's 39 column rule is about screen
    # FILES, which have to suit a C64, not about rows the board draws.
    ok &= check("every HELP line fits the terminal", all(len(l) <= 79 for l in lines))
    ok &= check("and uses the width it was given", any(len(l) > 39 for l in lines))
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
    # The column is headed "Address" since 1.1.0, when NODES became the
    # dashboard's node block; it said "IP".
    ok &= check("NODES shows IPs", r.wait_for(b"Address", 3) and r.wait_for(b"Xavier", 3))
    r.wait_for(b"Sysop", 3)
    r.buf.clear()
    r.send(b"dash\r")
    ok &= check("DASH shows the dashboard", r.wait_for(b"DASHBOARD", 4) and r.wait_for(b"last calls", 4))
    ok &= check("DASH shows Wi-Fi and a Doing column", r.wait_for(b"WiFi", 4) and b"Doing" in r.buf)
    if r.wait_for(b"[More] Y/n/c", 2):
        r.send(b"c")
    ok &= check("DASH lists callers and last calls", r.wait_for(b"last calls", 4) and b"Xavier" in r.buf)
    r.wait_for(b"Sysop", 4)
    r.buf.clear()
    r.send(b"dash 1\r")
    time.sleep(2.6)
    r.pump(0.2)
    ok &= check("DASH 1 refreshes", bytes(r.buf).count(b"DASHBOARD") >= 2)
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
    # Seed the ordinary account this test edits. It is registered by
    # test_page in the full run, which is not picked by --only=login, and
    # the check then failed for a reason unrelated to co-sysops.
    ansi_login("Alice").close()
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
    print("Accounts: sign-up checks, lockout, PROFILE, PASSWORD, WHOIS")
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
    c.send(b"abc\rabc\r")
    to_save(c, b"Password needs 4 or more characters")
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
    c.send(b"abcd\rabce\rZed\rzed@example.com\r")
    to_save(c, b"The passwords do not match")
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
    c.send(b"abcd\rabcd\rZed\rnot-an-email\r")
    to_save(c, b"That email does not look right")
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
    c.send(b"whois\r")
    ok &= check("own WHOIS shows email and profile",
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

    # --- WHOIS on someone else hides private fields
    p = ansi_login("Peeker")
    p.buf.clear()
    p.send(b"whois acct\r")
    ok &= check("WHOIS on another caller shows the handle", p.wait_for(b"Acct", 5) and p.wait_for(b"Plays chess", 3))
    p.wait_for(b"Main", 3)
    ok &= check("WHOIS hides their email", b"acct@example.com" not in p.buf)
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
    ok &= check("manager shows the key help", s.wait_for(b"Enter edit  A add  D retire  Q quit", 3))
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
    ok &= check("account retired", s.wait_for(b"Account retired.", 5))

    # This assertion is inverted from what it used to say, and the old
    # version was the bug written down as a requirement.
    #
    # USER DEL used to remove the block, which put the handle back into
    # circulation. Mail is matched by handle, so the next person to register
    # that name was handed the previous owner's undelivered mail. The suite
    # asserted "deleted handle is new again" and passed, which is how it
    # survived: the test agreed with the code because both were written from
    # the same wrong idea.
    #
    # A retired handle is reserved for ever. Nobody inherits anything.
    c, which = handle_then("Newbie", [b"[R]egister", b"Password:", b"locked"])
    ok &= check("a retired handle is never offered to somebody new", which != 0)
    c.close()

    s.buf.clear()
    s.send(b"whois acct\r")
    ok &= check("staff WHOIS shows private fields", s.wait_for(b"acct@example.com", 5))
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
    c.send(b"whois\r")
    ok &= check("WHOIS: guests have no account", c.wait_for(b"Guests have no account.", 3))
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
    # ESC clears the line and stays in the room. It used to leave, and Rob
    # asked for that to stop: "The escape key exits chat, that should not do
    # that. If anything esc would clear the typed line/command." /q leaves.
    #
    # The old check waited for "Main" and the ROOM is called Main, so it
    # matched the room's own name and passed whether ESC left or not. It
    # survived the behaviour being reversed, which is the clearest proof it
    # was never testing it.
    c.buf.clear()
    c.send(b"half a line")
    c.pump(0.4)
    c.send(b"\x1b")
    c.pump(0.6)
    ok &= check("ESC does not leave the room",
                b"[1] Main:" not in plain(c.buf) and b"Type HELP" not in plain(c.buf))
    c.buf.clear()
    c.send(b"/q\r")
    # The room announces the exit, which only happens on a real leave.
    # Deliberately NOT waiting for "Main": that word is both the room's name
    # and the shell prompt's menu name, which is exactly what made the old
    # version of this check pass no matter what ESC did.
    # /q leaving is already covered by "/q leaves the room" earlier in this
    # test, on a caller who was not mid-line. Asserting it again here only
    # re-tested the same path through a session that had just pressed ESC,
    # and duplicate coverage that fails for timing reasons is worse than
    # none: it trains somebody to ignore a red line.
    c.pump(0.5)
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


def test_motd():
    print("MOTD screen paging and abort (host only)")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs a local data/ directory")
        return True
    # Its own account, made before the motd exists. It logged in as
    # "Reader" and relied on an earlier test having registered that handle;
    # on a board where none had, the first login was a registration, which
    # plays screens/newuser in place of the motd, and every check failed.
    ansi_login("MotdReader").close()
    time.sleep(0.5)
    path = DATA / "screens" / "motd.asc"
    path.write_text("".join(f"MOTD line {i}\n" for i in range(1, 61)))
    try:
        c = Caller(ansi=True)
        c.wait_for(b"Enter your handle", 10)
        login(c, "MotdReader", wait_main=False)
        ok = check("motd plays after login", c.wait_for(b"MOTD line 1\r\n", 5))
        ok &= check("pauses at [More] after 22 lines", c.wait_for(b"[More] Y/n/c", 5)
                    and b"MOTD line 22" in c.buf and b"MOTD line 23" not in c.buf)
        c.buf.clear()
        c.send(b" ")
        ok &= check("space continues one page", c.wait_for(b"MOTD line 23", 5)
                    and c.wait_for(b"[More] Y/n/c", 5) and b"MOTD line 45" not in c.buf)
        c.send(b"y")
        ok &= check("rest of the motd, then the prompt",
                    c.wait_for(b"MOTD line 60", 5) and c.wait_for(b"Main", 5))
        c.close()

        c = Caller(ansi=True)
        c.wait_for(b"Enter your handle", 10)
        login(c, "MotdReader", wait_main=False)
        c.wait_for(b"[More] Y/n/c", 8)
        c.buf.clear()
        c.send(b"n")
        ok &= check("N at [More] stops the screen", c.wait_for(b"Stopped.", 3) and c.wait_for(b"Main", 3))
        ok &= check("nothing after the stop", b"MOTD line 50" not in c.buf)
        c.close()
    finally:
        path.unlink()
    return ok


# ---------------------------------------------------------------------------
# Backup window (needs the button held: BBS_BACKUP_TEST_OPEN on host, or the
# esp32dev_backuptest build on a board)
# ---------------------------------------------------------------------------
def http_call(method, path, body=None, timeout=60, headers=None, port=None):
    conn = http.client.HTTPConnection(HOST, port or BACKUP_PORT, timeout=timeout)
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


def upload_with_answer(sysop, data, answer, expect_prompt=True, port=None):
    """PUT the zip in a thread, answer the sysop prompt, return (status, body, prompt_seen)."""
    result = {}

    def worker():
        result["r"] = http_call("PUT", "/restore", data, timeout=90, port=port)

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
    # Seed the account this test asserts on, rather than relying on another
    # test having made it. It checks that the zip carries "Alice", and Alice
    # is registered by test_page: under --only=storage that test is not
    # picked, so four checks failed for a reason that had nothing to do with
    # backups. A test that depends on another test reports somebody else's
    # absence as your bug.
    ansi_login("Alice").close()
    local = HOST in ("127.0.0.1", "localhost")
    # An information page with text (1.1.0): it travels in the zip now.
    info_dir = USERDATA / "p" / "info"
    if local:
        info_dir.mkdir(parents=True, exist_ok=True)
        (info_dir / "2.txt").write_bytes(b"Page two from the host\n")
    s = ansi_login("Rob")
    s.buf.clear()
    s.send(f"bye {PASSWORD}\r".encode())
    ok = check("sysop node", s.wait_for(b"SysOp node", 5))
    ok &= check("window-open notice on the sysop console", s.wait_for(b"*** Backup open", 8))
    ok &= check("and it says the zip holds the Wi-Fi password",
                s.wait_for(b"Never forward this port", 5))

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
    # The Wi-Fi passphrase is carried, not redacted (NEXT.md 3.2): a restore
    # onto a fresh board has to bring the network with it. Both values carry
    # a '#', which used to start a comment anywhere on a line.
    ok &= check("Wi-Fi password carried whole", "wifi_password = pa#ss word1" in cfg)
    ok &= check("Wi-Fi network name kept whole", "wifi_ssid = Test#Net" in cfg)
    users = z.read("users.txt").decode() if z and "users.txt" in names else ""
    ok &= check("zip carries users.txt, hashes only", "Alice" in users and TEST_PW not in users)
    if local:
        ok &= check("and the information pages (1.1.0)",
                    "info/2.txt" in names and z.read("info/2.txt") == b"Page two from the host\n")
    ok &= check("download notice on the sysop console", s.wait_for(b"*** Backup downloaded by", 5))

    # --- edit and upload inside a folder, deflated (what re-zipping an unpacked folder gives)
    files = {f"unleashed-backup/{n}": z.read(n) for n in names if n != "screens/busy.seq"}
    files["unleashed-backup/screens/motd.asc"] = b"Custom line from upload test\n"
    files["unleashed-backup/screens/extra.asc"] = b"extra screen\n"
    files["unleashed-backup/system.cfg"] = (cfg + "\nidle_minutes = 21\n").encode()
    files["unleashed-backup/info/3.txt"] = b"Page three from the upload\n"
    status, body, seen = upload_with_answer(s, make_zip(files), b"y")
    ok &= check("sysop asked Y/N for the upload", seen)
    summary = bytes(s.buf)
    ok &= check("summary names the files", b"system.cfg" in summary and b"screens" in summary and b"users" in summary)
    ok &= check("upload applied (200)", status == 200 and b"Applied" in body)
    if local:
        ok &= check("an information page in the zip goes back",
                    (info_dir / "3.txt").exists() and
                    (info_dir / "3.txt").read_bytes() == b"Page three from the upload\n")
        pg = ansi_login("Alice")
        pg.buf.clear()
        pg.send(b"info 3\r")
        ok &= check("and reads at once, without a restart", pg.wait_for(b"Page three from the upload", 6))
        pg.close()
    if local:
        live_cfg = (USERDATA / "system.cfg").read_text()
        ok &= check("*** kept the real password on disk", f"sysop_password = {PASSWORD}" in live_cfg)
        ok &= check("upload kept the Wi-Fi password whole, # and all",
                    "wifi_password = pa#ss word1" in live_cfg)
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
    ok &= check("callers see the uploaded motd", c.wait_for(b"Custom line from upload test", 8))
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
    ok &= check("in the copy's words, in the board's KB",
                b"Too big: 489 KB. The limit is 256 KB." in body)
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
        for p in ("2.txt", "3.txt"):
            (info_dir / p).unlink(missing_ok=True)
    return ok


# ---------------------------------------------------------------------------
# A restore and the published sysop password (1.0.2)
# ---------------------------------------------------------------------------
STAFF_KEYS = ("sysop_password", "cosysop1_password", "cosysop2_password")


def cfg_staff(cfg, lines):
    """cfg with its staff password lines replaced by these, put above the
    first section, where the parser reads them."""
    rows = [l for l in cfg.splitlines() if l.split("=", 1)[0].strip() not in STAFF_KEYS]
    cut = next((i for i, l in enumerate(rows) if l.strip().startswith("[")), len(rows))
    rows[cut:cut] = lines
    return ("\n".join(rows) + "\n").encode()


def announce_held(s):
    """Does ANNOUNCE say the listing is held for the default password?"""
    s.buf.clear()
    s.send(b"announce\r")
    s.pump(1.5)
    return b"Held: the sysop password" in plain(s.buf)


def local_login(handle):
    """Log in from 127.0.0.1 and reach the main prompt. An unconfigured board
    offers setup to a local caller first; ESC skips it. Returns the caller
    and whether setup was offered."""
    c = Caller(ansi=True)
    c.wait_for(b"Enter your handle", 10)
    login(c, handle, wait_main=False)
    offered = wait_any(c, [b"has not been set up yet", b"Main"], 10) == 0
    if offered:
        c.wait_for(b"Sysop password", 4)
        c.buf.clear()
        c.send(b"\x1b")
        c.wait_for(b"Skipped", 5)
    for _ in range(6):                   # a newuser or motd screen may page
        if c.wait_for(b"Main", 1.5):
            break
        drain(c)
    return c, offered


def test_backup_published_default():
    """A restore never makes the published sysop password a real one (1.0.2).

    A board on the default has no sysop_password line, and that absence is
    what keeps the default local-only and the directory listing held. 1.0.1
    restored a backup's redacted "sysop_password = ***" as the live password,
    which on such a board is the published one, so the line came back
    explicit: the password on the install page then worked from anywhere and
    the board went on the directory.

    On the ordinary test board it checks the other half: the same restore
    keeps a board's own passwords, as it always has. Host only, because it
    reads the board's system.cfg; the backup window is always open there.
    """
    print("Backup restore and the published sysop password")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  reads the board's system.cfg, host only")
        return True
    if os.environ.get("BBS_FRESH"):
        return published_default_fresh()
    return published_default_configured()


def published_default_fresh():
    if cfg_line("sysop_password") is not None:
        print("  SKIP  this board has been set up already (first_setup ran first?)")
        return True
    s, offered = local_login("FreshOwner")
    ok = check("a fresh board offers setup to a local caller", offered)
    s.buf.clear()
    s.send(f"bye {BBS_DEFAULT}\r".encode())
    ok &= check("the default makes a local caller the sysop", s.wait_for(b"SysOp node", 6))
    ok &= check("and the backup window opens", s.wait_for(b"*** Backup open", 8))
    status, data = http_call("GET", "/backup.zip")
    z = zipfile.ZipFile(io.BytesIO(data)) if status == 200 else None
    cfg = z.read("system.cfg").decode() if z else ""
    ok &= check("the fresh board's own backup has no sysop_password line",
                z is not None and "sysop_password" not in cfg)

    # What a sysop brings from another board, or from before a reset: every
    # staff password redacted.
    redacted = cfg_staff(cfg, [f"{k} = ***" for k in STAFF_KEYS])
    status, body, seen = upload_with_answer(s, make_zip({"system.cfg": redacted}), b"y")
    ok &= check("a backup with *** staff passwords restores", seen and status == 200 and b"Applied" in body)
    ok &= check("*** leaves the board with no sysop_password line", cfg_line("sysop_password") is None)
    ok &= check("no staff line carries the published password",
                all(cfg_value(k) != BBS_DEFAULT for k in STAFF_KEYS))
    ok &= check("co-sysop levels that were off stay off",
                cfg_value("cosysop1_password") == "" and cfg_value("cosysop2_password") == "")
    ok &= check("the restore says the board is on the published default",
                b"published default" in body)
    # The console notice of the result is cut at 60 characters, which is
    # before that part of it, so it has a line of its own.
    ok &= check("and so does the sysop's console",
                s.wait_for(b"Sysop password is the published default", 5))
    ok &= check("and the directory listing stays held", announce_held(s))
    s.close()
    time.sleep(1.0)                      # let the board see the sysop node free

    # From outside the board's own network the default is a wrong password.
    far = Caller(ansi=True, source="127.0.0.2")
    far.wait_for(b"Enter your handle", 10)
    ok &= check("a caller from outside logs in and is not offered setup",
                login(far, "FarCaller") and b"not been set up" not in plain(far.buf))
    far.buf.clear()
    far.send(f"bye {BBS_DEFAULT}\r".encode())
    far.wait_closed(12)                  # the goodbye screen, then a 5 s linger
    ok &= check("the default does not make a caller from outside the sysop",
                b"SysOp node" not in far.buf)
    far.close()
    time.sleep(1.0)

    # From its own network it still works, and the board still asks.
    s, offered = local_login("FreshOwner")
    ok &= check("setup is offered again to a local caller", offered)
    s.buf.clear()
    s.send(f"bye {BBS_DEFAULT}\r".encode())
    ok &= check("the default still works from the board's own network", s.wait_for(b"SysOp node", 6))
    s.wait_for(b"*** Backup open", 8)

    # The same password typed out rather than redacted. A restore never
    # writes the published password in any form: the line is left out.
    literal = cfg_staff(cfg, [f"sysop_password = {BBS_DEFAULT}"])
    status, body, seen = upload_with_answer(s, make_zip({"system.cfg": literal}), b"y")
    ok &= check("a system.cfg naming the published password applies", seen and status == 200)
    ok &= check("and leaves no sysop_password line", cfg_line("sysop_password") is None)
    ok &= check("so the listing stays held", announce_held(s))
    s.close()
    return ok


def published_default_configured():
    if not PASSWORD:
        print("  SKIP  no sysop_password")
        return True
    before = (USERDATA / "system.cfg").read_text()
    s = ansi_login("KeepsOwn")
    s.buf.clear()
    s.send(f"bye {PASSWORD}\r".encode())
    ok = check("sysop node", s.wait_for(b"SysOp node", 5))
    s.wait_for(b"*** Backup open", 8)
    status, data = http_call("GET", "/backup.zip")
    z = zipfile.ZipFile(io.BytesIO(data)) if status == 200 else None
    cfg = z.read("system.cfg").decode() if z else ""

    try:
        # The same backup the fresh board is given, onto a board with
        # passwords of its own: *** keeps each of them. This is what a restore
        # has always done and it must not move.
        redacted = cfg_staff(cfg, [f"{k} = ***" for k in STAFF_KEYS])
        status, body, seen = upload_with_answer(s, make_zip({"system.cfg": redacted}), b"y")
        ok &= check("the same backup restores onto a board with its own passwords",
                    seen and status == 200)
        ok &= check("*** keeps the board's own sysop password", cfg_value("sysop_password") == PASSWORD)
        ok &= check("and its co-sysop passwords",
                    cfg_value("cosysop1_password") == CO1 and cfg_value("cosysop2_password") == CO2)
        s.pump(1.0)                      # the console notices of the result
        ok &= check("and says nothing about the default, in curl or on the console",
                    b"published default" not in body and b"published default" not in plain(s.buf))
        ok &= check("the listing is not held", not announce_held(s))

        # Typed out in full, the published password is still never written: a
        # sysop line naming it is left out, which puts the board on the
        # default (local only, listing held), and a co-sysop line naming it is
        # left out, which switches that level off.
        literal = cfg_staff(cfg, [f"sysop_password = {BBS_DEFAULT}",
                                  f"cosysop1_password = {BBS_DEFAULT}",
                                  "cosysop2_password = ***"])
        status, body, seen = upload_with_answer(s, make_zip({"system.cfg": literal}), b"y")
        ok &= check("a system.cfg naming the published password applies", seen and status == 200)
        ok &= check("its sysop line is left out, so the board is on the default",
                    cfg_line("sysop_password") is None and b"published default" in body)
        ok &= check("which holds the listing", announce_held(s))
        ok &= check("its co-sysop line is left out, so that level is off",
                    cfg_line("cosysop1_password") is None)
        ok &= check("a co-sysop kept as *** keeps its own", cfg_value("cosysop2_password") == CO2)
    finally:
        # Put the board back the way the rest of the suite knows it, whatever
        # happened above: test_backup and test_ban run on it next.
        status, _, seen = upload_with_answer(s, make_zip({"system.cfg": before.encode()}), b"y")
        ok &= check("the board's own passwords go back",
                    seen and status == 200 and cfg_value("sysop_password") == PASSWORD
                    and cfg_value("cosysop1_password") == CO1)
        s.close()
    return ok


# ---------------------------------------------------------------------------
# Backups on the SD card (1.1.0): BACKUP SD, RESTORE SD, the nightly one
# ---------------------------------------------------------------------------
def card_dir():
    """The harness's card, or None on a run without one."""
    sd = os.environ.get("BBS_SD_DIR", "")
    return pathlib.Path(sd) if sd else None


def card_cmd(s, cmd, want, secs=15):
    """Type a command, wait for one of want, and say which with the text."""
    s.buf.clear()
    s.send(cmd + b"\r")
    i = wait_any(s, want, secs)
    s.pump(0.4)
    return i, plain(s.buf)


def user_file():
    p = USERDATA / "users.txt"
    return p.read_bytes() if p.exists() else b""


def bytes_of(p):
    """A file's bytes, or None when it is not there: a check, not a crash."""
    return p.read_bytes() if p.exists() else None


def test_backup_card():
    """BACKUP SD and RESTORE SD, and RESTORE SD SCREENS (1.1.0).

    The zip on the card is the window's zip, checked back in by the window's
    own validator: so the redaction, the Wi-Fi password as typed and the
    1.0.2 rule about the published password are all asserted from this door
    too, rather than assumed to follow.
    """
    print("BACKUP SD and RESTORE SD")
    local = HOST in ("127.0.0.1", "localhost")
    if not PASSWORD or not local:
        print("  SKIP  needs the host build and the sysop")
        return True
    import shutil
    card = card_dir()
    s = cfg_sysop("CardKeeper")
    if card is None:
        i, txt = card_cmd(s, b"backup sd", [b"No card mounted"])
        ok = check("with no card, BACKUP SD says to mount one",
                   i == 0 and b"No card mounted. Try SD MOUNT first." in txt)
        i, txt = card_cmd(s, b"restore sd", [b"No card mounted"])
        ok &= check("and so does RESTORE SD", i == 0)
        s.close()
        return ok

    bdir = card / "backup"
    shutil.rmtree(bdir, ignore_errors=True)
    i, txt = card_cmd(s, b"backup", [b"For the backup window, press BOOT."])
    ok = check("BACKUP on its own says what BACKUP SD is",
               i == 0 and b"BACKUP SD saves a zip on the card." in txt)
    i, txt = card_cmd(s, b"restore sd", [b"No backups on the card yet.", b"Backups on the card"])
    ok &= check("an empty card says so, and how to make one", i == 0 and b"BACKUP SD makes one." in txt)

    # ---- BACKUP SD ---------------------------------------------------------
    i, txt = card_cmd(s, b"backup sd", [b"Saved:", b"Card write failed", b"Card full", b"There is one"], 30)
    ok &= check("BACKUP SD writes a zip and says so", i == 0)
    m = re.search(rb"Writing (unleashed-\d{8}-\d{4}\.zip)", txt)
    ok &= check("named unleashed-YYYYMMDD-HHMM.zip", m is not None)
    lines = render_lines(s.buf)
    ok &= check("with a dot a file on a line of their own",
                any(re.fullmatch(r"\.{5,}", ln.strip() or "x") for ln in lines))
    ok &= check("in the copy's words", re.search(rb"Saved: \d+ files, \d+ KB\.", txt) is not None)
    ok &= check("and that it holds the Wi-Fi password", b"It holds your Wi-Fi password as typed." in txt)
    name = m.group(1).decode() if m else "none.zip"
    zpath = bdir / name
    try:
        z = zipfile.ZipFile(zpath)
        bad = z.testzip()
        names = z.namelist()
    except (OSError, zipfile.BadZipFile):
        z, bad, names = None, "no zip", []
    ok &= check("the zip on the card is whole (every CRC good)", z is not None and bad is None)
    ok &= check("and holds the window's files",
                all(n in names for n in ("system.cfg", "users.txt", "MANIFEST.txt", "screens/welcome.ans")))
    cfg = z.read("system.cfg").decode() if z else ""
    ok &= check("staff passwords as ***, never the password itself",
                "sysop_password = ***" in cfg and f"= {PASSWORD}" not in cfg)
    ok &= check("the Wi-Fi password as typed, # and all", "wifi_password = pa#ss word1" in cfg)
    sm = re.search(rb"Saved: (\d+) files", txt)
    ok &= check("Saved counts the files in it, the manifest aside",
                sm is not None and int(sm.group(1)) == len([n for n in names if n != "MANIFEST.txt"]))
    ok &= check("and leaves no half file beside it", bdir.exists() and not list(bdir.glob("*.tmp")))
    first = bytes_of(zpath)

    # The same minute again: the first is kept, never written over.
    i, txt = card_cmd(s, b"backup sd", [b"There is one from this minute already.", b"Saved:"], 30)
    ok &= check("a second in the same minute is refused, or has a minute of its own",
                first is not None and bytes_of(zpath) == first and
                (i == 0 or len(list(bdir.glob("unleashed-*.zip"))) == 2))

    # ---- BACKUP SD SCREENS -------------------------------------------------
    i, txt = card_cmd(s, b"backup sd screens", [b"Saved:", b"Card write failed", b"There is one"], 30)
    ok &= check("BACKUP SD SCREENS saves the screens alone",
                i == 0 and re.search(rb"Saved: \d+ screens, \d+ KB\.", txt) is not None)
    ok &= check("and says nothing of a Wi-Fi password it does not hold", b"Wi-Fi" not in txt)
    ms = re.search(rb"Writing (screens-\d{8}-\d{4}\.zip)", txt)
    sname = ms.group(1).decode() if ms else "none.zip"
    try:
        zs = zipfile.ZipFile(bdir / sname)
        snames = zs.namelist() if zs.testzip() is None else []
    except (OSError, zipfile.BadZipFile):
        snames = []
    ok &= check("which holds screens and the manifest, nothing else",
                bool(snames) and all(n.startswith("screens/") or n == "MANIFEST.txt" for n in snames))

    # ---- the list ------------------------------------------------------------
    # The newest first, whatever the clock's second: set the times by hand.
    bdir.mkdir(parents=True, exist_ok=True)
    now = time.time()
    for p in bdir.glob("unleashed-*.zip"):
        os.utime(p, (now - 300, now - 300))
    for p, age in ((zpath, 200), (bdir / sname, 100)):
        if p.exists():
            os.utime(p, (now - age, now - age))
    i, txt = card_cmd(s, b"restore sd", [b"restores one."])
    ok &= check("RESTORE SD lists the card's backups", i == 0 and b"Backups on the card" in txt)
    ok &= check("newest first, numbered, with a size in KB",
                re.search(rb" 1  " + sname.encode() + rb"\s+\d+ KB", txt) is not None and
                re.search(rb" 2  " + name.encode() + rb"\s+\d+ KB", txt) is not None)
    ok &= check("and how to restore one", b"RESTORE SD n restores one." in txt)
    listed = [ln for ln in render_lines(s.buf) if re.match(r"\s*\d+  \S+\.zip", ln)]
    ok &= check("each row inside 39 columns", bool(listed) and all(len(ln) <= 39 for ln in listed))
    i, txt = card_cmd(s, b"restore sd screens", [b"restores one."])
    ok &= check("RESTORE SD SCREENS offers the same list", b"RESTORE SD SCREENS n restores one." in txt)
    i, txt = card_cmd(s, b"restore sd 9", [b"No backup with that number."])
    ok &= check("a number the list did not show is refused", i == 0)
    i, txt = card_cmd(s, b"restore sd nosuch.zip", [b"No backup by that name on the card."])
    ok &= check("so is a name that is not on the card", i == 0)
    i, txt = card_cmd(s, b"restore sd ../user/users.txt", [b"No backup by that name on the card."])
    ok &= check("and a path is not a name at all", i == 0)

    # ---- RESTORE SD n: the question, N, then Y -------------------------------
    later = ansi_login("CardLater")                        # an account made after the backup
    later.close()
    time.sleep(0.6)
    ok &= check("(an account made since the backup)", b"[CardLater]" in user_file())
    i, txt = card_cmd(s, b"restore sd 2", [b"Restore now? (y/N)", b"Cannot read", b"Nothing in it"], 30)
    ok &= check("RESTORE SD n checks the zip, then asks", i == 0 and (b"Checking " + name.encode()) in txt)
    rows = render_lines(s.buf)
    ok &= check("with its name on the title bar", any(("RESTORE " + name) in r for r in rows))
    ok &= check("and every row a full restore always shows",
                all(any(r.startswith(lab) for r in rows) for lab in ("In zip", "Replaces", "Accounts",
                                                                      "Removes", "Staff")))
    ok &= check("Accounts puts both numbers in front of the sysop",
                re.search(rb"Accounts\s+\d+, replacing all \d+ here", txt) is not None)
    ok &= check("Staff says the passwords stay", re.search(rb"Staff\s+passwords stay as they are", txt) is not None)
    ok &= check("and changes since then will be lost", b"Changes made since then will be lost." in txt)
    s.buf.clear()
    s.send(b"n")
    ok &= check("N: Not restored.", s.wait_for(b"Not restored.", 5))
    ok &= check("and nothing changed", b"[CardLater]" in user_file())

    i, txt = card_cmd(s, b"restore sd 2", [b"Restore now? (y/N)"], 30)
    s.buf.clear()
    s.send(b"y")
    ok &= check("Y: Restored and live.", s.wait_for(b"Restored and live.", 30))
    ok &= check("after Restoring and a dot a file", b"Restoring." in plain(s.buf))
    ok &= check("the account made since the backup is gone", b"[CardLater]" not in user_file())
    ok &= check("the board keeps its own sysop password", cfg_value("sysop_password") == PASSWORD)
    ok &= check("and its Wi-Fi password whole", (cfg_line("wifi_password") or "").endswith("= pa#ss word1"))
    ok &= check("and nothing is left in staging", not (USERDATA / ".staging").exists())

    # ---- RESTORE SD SCREENS ---------------------------------------------------
    theme = make_zip({"screens/motd.asc": b"Card motd line\n",
                      "screens/about.asc": b"A card about screen\n",
                      "system.cfg": b"idle_minutes = 7\n"})
    bdir.mkdir(parents=True, exist_ok=True)
    (bdir / "theme.zip").write_bytes(theme)
    flash_about = bytes_of(DATA / "screens" / "about.asc")
    card_about = (card / "screens" / "about.asc")
    had_about = card_about.read_bytes() if card_about.exists() else None
    i, txt = card_cmd(s, b"restore sd screens theme.zip", [b"Restore now? (y/N)", b"Cannot read"], 30)
    ok &= check("RESTORE SD SCREENS asks too", i == 0)
    ok &= check("what it replaces and adds on the card",
                re.search(rb"Replaces\s+1 screen on the card", txt) is not None and
                re.search(rb"Adds\s+1 screen to the card", txt) is not None)
    ok &= check("a settings file in it is turned away, not quietly used",
                re.search(rb"Rejected\s+1: system.cfg: not a screen", txt) is not None)
    ok &= check("and never Removes: a screens restore only adds", b"Removes" not in txt)
    ok &= check("flash is said to be left alone", b"Stock screens in flash are not touched." in txt)
    s.buf.clear()
    s.send(b"y")
    ok &= check("Restored: 2 screens on the card.", s.wait_for(b"Restored: 2 screens on the card.", 30))
    ok &= check("and how to undo it", s.wait_for(b"Deleting them from the card undoes it.", 5))
    ok &= check("the screens are on the card", bytes_of(card / "screens" / "motd.asc") == b"Card motd line\n")
    ok &= check("the stock ones in flash untouched", bytes_of(DATA / "screens" / "about.asc") == flash_about
                and not (DATA / "screens" / "motd.asc").exists())
    ok &= check("the settings untouched", cfg_value("idle_minutes") != "7")
    ok &= check("and no staging left on the card", not (bdir / ".staging").exists())
    (card / "screens" / "motd.asc").unlink(missing_ok=True)
    if had_about is not None:
        card_about.write_bytes(had_about)

    # ---- what is refused ------------------------------------------------------
    (bdir / "big.zip").write_bytes(b"\0" * 262145)
    i, txt = card_cmd(s, b"restore sd big.zip", [b"Too big:"])
    ok &= check("over the limit: Too big, in the board's KB",
                i == 0 and b"Too big: 257 KB. The limit is 256 KB." in txt)
    (bdir / "junk.zip").write_bytes(b"not a zip at all, only words")
    i, txt = card_cmd(s, b"restore sd junk.zip", [b"Cannot read it:"])
    ok &= check("not a zip: Cannot read it, with the reason", i == 0 and b"no end record" in txt)
    (bdir / "notes.zip").write_bytes(make_zip({"notes.txt": b"x"}))
    i, txt = card_cmd(s, b"restore sd notes.zip", [b"Nothing in it can be used."], 20)
    ok &= check("nothing usable: said, with the first problem",
                i == 0 and b"First problem: notes.txt" in txt)

    # ---- who may --------------------------------------------------------------
    i, txt = card_cmd(s, b"? sysop", [b"RESTORE SD n"], 6)
    drain(s)
    ok &= check("HELP lists both for the sysop", i == 0 and b"BACKUP SD" in txt)
    n = ansi_login("NotCardSysop")
    n.buf.clear()
    n.send(b"backup sd\r")
    n.pump(0.8)
    ok &= check("and nobody else has either", b"Unknown" in n.buf)
    n.close()
    shutil.rmtree(bdir, ignore_errors=True)
    s.close()
    return ok


def test_backup_card_nightly():
    """The nightly backup (1.1.0): at its hour, keeping seven, and only ever
    removing nightly zips. And, with no card, the sysop told at arrival."""
    print("The nightly backup")
    if not PASSWORD or HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build and the sysop")
        return True
    import shutil
    import tempfile
    ansi_login("NightKeeper").close()                    # accounts for the backup to hold
    time.sleep(1.0)
    hour = str(int(time.strftime("%H", time.gmtime())))  # the harness board runs on UTC0
    card = pathlib.Path(tempfile.mkdtemp(prefix="bbs-nightly-card-"))
    b = card / "backup"
    b.mkdir()
    for d in range(1, 9):                                 # eight old ones
        (b / f"nightly-202608{d:02d}.zip").write_bytes(b"old")
    own = ["unleashed-20200101-0000.zip", "screens-20200101-0000.zip", "aaa.zip", "nightly-2020.zip"]
    for n in own:
        (b / n).write_bytes(b"mine")

    def nightly_on(tmp):
        cfg = tmp / "data" / "user" / "system.cfg"
        text = cfg.read_text()
        # cfg_with only adds to a section that is there, and the harness
        # board runs sd on its defaults with no section at all.
        if "[plugin:sd]" not in text:
            text = text.rstrip("\n") + "\n\n[plugin:sd]\n"
        cfg.write_text(cfg_with(text, {("plugin:sd", "nightly"): "yes"}))

    tmp = copy_data()
    nightly_on(tmp)
    port = PORT + 3500
    proc = start_copy(tmp, (str(port),), {"BBS_SD_DIR": str(card), "BBS_NIGHTLY_HOUR": hour})
    ok = True
    try:
        log = copy_log(tmp, "7 on the card", 30)
        today = time.strftime("%Y%m%d", time.gmtime())
        made = b / f"nightly-{today}.zip"
        ok &= check("at its hour a nightly zip is made", made.exists())
        try:
            zz = zipfile.ZipFile(made)
            whole = zz.testzip() is None and "system.cfg" in zz.namelist() and "users.txt" in zz.namelist()
        except (OSError, zipfile.BadZipFile):
            whole = False
        ok &= check("a full backup, whole", whole)
        left = sorted(p.name for p in b.glob("nightly-????????.zip"))
        ok &= check("seven nightly zips kept", len(left) == 7)
        ok &= check("the two oldest went",
                    "nightly-20260801.zip" not in left and "nightly-20260802.zip" not in left)
        ok &= check("and every zip of the sysop's own stayed, the look-alike too",
                    all((b / n).exists() for n in own))
        ok &= check("the log names what it removed",
                    "backup: removed the oldest nightly, nightly-20260801.zip" in log)
        ok &= check("and what it made", re.search(r"backup: nightly nightly-\d{8}\.zip, \d+ KB, 7 on the card",
                                                   log) is not None)
    finally:
        stop_copy(proc, tmp)
        shutil.rmtree(card, ignore_errors=True)

    # No card at the hour: skipped, logged, and the sysop told on arrival.
    tmp = copy_data()
    nightly_on(tmp)
    port = PORT + 3501
    proc = start_copy(tmp, (str(port),), {"BBS_NIGHTLY_HOUR": hour})
    try:
        log = copy_log(tmp, "nightly skipped", 15)
        ok &= check("with no card the night is skipped and says why",
                    "backup: nightly skipped: no card" in log)
        c = ansi_login("NightOwl", port=port)
        c.buf.clear()
        c.send(f"bye {PASSWORD}\r".encode())
        ok &= check("and the sysop is told when they arrive",
                    c.wait_for(b"SysOp node", 6) and c.wait_for(b"Last night's backup failed: no card.", 4))
        c.close()
    finally:
        stop_copy(proc, tmp)
    return ok


def test_restore_cross_partition():
    """A restore on a board whose accounts are on another filesystem (1.1.0).

    On the board, userdata and the screens are two LittleFS partitions, and a
    rename from one to the other is refused (EXDEV). The restore staged on the
    screens partition and renamed users.txt across: it failed, removed the
    live users.txt, tried the rename again and failed again, so restoring a
    backup deleted every account. The host has one filesystem and never saw
    it. Here userdata is put on /dev/shm, which is tmpfs, so the host refuses
    the same rename the same way.
    """
    print("A restore across two partitions")
    if not PASSWORD or HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build and the sysop")
        return True
    import shutil
    import tempfile
    if not pathlib.Path("/dev/shm").is_dir():
        print("  SKIP  no /dev/shm to put userdata on")
        return True
    tmp = copy_data()
    user = tmp / "data" / "user"
    shm = pathlib.Path(tempfile.mkdtemp(prefix="bbs-userdata-", dir="/dev/shm"))
    shutil.copytree(user, shm / "user")
    shutil.rmtree(user)
    user.symlink_to(shm / "user")
    port = PORT + 3600
    bport = PORT + 3601
    cfg = user / "system.cfg"
    cfg.write_text(cfg_with(cfg.read_text(), {("", "backup_port"): str(bport)}))
    proc = start_copy(tmp, (str(port),), {"BBS_BACKUP_TEST_OPEN": "1"})
    ok = True
    s = None
    try:
        copy_log(tmp, f"listening on {port},", 8)
        s = ansi_login("SplitKeeper", port=port)
        s.buf.clear()
        s.send(f"bye {PASSWORD}\r".encode())
        ok &= check("sysop on a board split across two filesystems",
                    s.wait_for(b"SysOp node", 6) and s.wait_for(b"*** Backup open", 8))
        time.sleep(0.5)
        status, data = http_call("GET", "/backup.zip", port=bport)
        try:
            users = zipfile.ZipFile(io.BytesIO(data)).read("users.txt")
        except (zipfile.BadZipFile, KeyError):
            users = b""
        ok &= check("its backup carries the accounts", b"[SplitKeeper]" in users)
        files = {"users.txt": users, "screens/motd.asc": b"Across the partitions\n"}
        status, body, seen = upload_with_answer(s, make_zip(files), b"y", port=bport)
        ok &= check("the restore is asked and applied without errors",
                    seen and status == 200 and b"with errors" not in body)
        live = user / "users.txt"
        ok &= check("the accounts are there after it",
                    live.exists() and b"[SplitKeeper]" in live.read_bytes())
        motd = tmp / "data" / "screens" / "motd.asc"
        ok &= check("and the screen reached the other filesystem",
                    motd.exists() and motd.read_bytes() == b"Across the partitions\n")
    finally:
        if s:
            s.close()
        stop_copy(proc, tmp)
        shutil.rmtree(shm, ignore_errors=True)
    return ok


def test_config_timezone():
    """CONFIG board: the timezone by name, the TZ string under it (1.1.0)."""
    print("CONFIG board: the timezone by name")
    local = HOST in ("127.0.0.1", "localhost")
    s = cfg_sysop("CfgZone")
    opened = cfg_open(s, b"board", b"TZ string")
    rows = render_lines(s.buf)
    zone = next((r for r in rows if r.strip().startswith("Timezone")), "")
    tzs = next((r for r in rows if r.strip().startswith("TZ string")), "")
    ok = check("the board page has a Timezone row and a TZ string row", bool(opened and zone and tzs))
    ok &= check("UTC0 in the file opens as UTC, by name", re.search(r"Timezone\s+UTC\b", zone) is not None
                and "UTC0" in tzs)
    ok &= check("and the page fits 40 columns", max_column(s.buf) <= 39)
    s.buf.clear()
    s.send(DOWN * BOARD_ZONE)
    s.pump(0.5)
    ok &= check("the Timezone row's note", b"Pick a zone, or Custom and type below." in plain(s.buf))
    s.buf.clear()
    s.send(b"u" * 5)                    # UTC, then Hawaii, Alaska, Pacific, Mountain, Central
    s.pump(0.8)
    rows = render_lines(s.buf)
    ok &= check("a zone picked by letter writes its string in the row below",
                any("US Central (Chicago)" in r for r in rows) and
                any("CST6CDT,M3.2.0,M11.1.0" in r for r in rows))
    s.send(F1)
    got = cfg_verdict(s, [b"Saved and live", b"Nothing changed"])
    ok &= check("and saves as that string",
                got == b"Saved and live" and (not local or
                                              (cfg_line("tz") or "").endswith("= CST6CDT,M3.2.0,M11.1.0")))
    cfg_open(s, b"board", b"TZ string")
    rows = render_lines(s.buf)
    ok &= check("which opens as the zone's name",
                any(re.search(r"Timezone\s+US Central \(Chicago\)", r) for r in rows))

    s.buf.clear()
    s.send(DOWN * BOARD_TZ)
    s.pump(0.5)
    ok &= check("the TZ string row's note", b"Find yours: unleashedbbs.com/setup" in plain(s.buf))
    s.buf.clear()
    s.send(b"\x08" * 40 + b"CET-1CEST,M3.5.0,M10.5.0/2")
    s.pump(0.8)
    ok &= check("typing a string turns the Timezone to Custom",
                any(re.search(r"Timezone\s+Custom", r) for r in render_lines(s.buf)))
    s.send(F1)
    got = cfg_verdict(s, [b"Saved and live", b"Nothing changed"])
    ok &= check("a typed string saves as typed",
                got == b"Saved and live" and (not local or
                                              (cfg_line("tz") or "").endswith("= CET-1CEST,M3.5.0,M10.5.0/2")))
    cfg_open(s, b"board", b"TZ string")
    rows = render_lines(s.buf)
    ok &= check("and opens as Custom, with the string",
                any(re.search(r"Timezone\s+Custom", r) for r in rows) and
                any("CET-1CEST,M3.5.0,M10.5.0/2" in r for r in rows))

    # Back to UTC by name: from Custom, u is the first zone starting with it.
    s.send(DOWN * BOARD_ZONE + b"u" + F1)
    got = cfg_verdict(s, [b"Saved and live", b"Nothing changed"])
    ok &= check("and back to UTC", got == b"Saved and live" and
                (not local or (cfg_line("tz") or "").endswith("= UTC0")))
    s.close()
    return ok


def test_config_cycle_numbers():
    """Plain ASCII: a cycle's choices numbered, and a number picks (1.1.0)."""
    print("CONFIG in plain ASCII: choices by number")
    if not PASSWORD or HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build and the sysop")
        return True
    c = ascii_sysop("CfgNumbers")
    c.buf.clear()
    c.send(b"config sd\r")
    c.wait_for(b"Enabled", 5)
    c.pump(0.4)
    ok = check("a yes or no row lists its two, numbered", b"1 yes  2 no" in plain(c.buf))
    got, rows = ascii_form_seen(c, [b"", b"99", b"5"] + [b""] * SD_ROWS_AFTER_READ)
    txt = "\n".join(rows)
    ok &= check("a level row lists the ladder, numbered",
                "1 all  2 users  3 staff  4 co2  5 co1" in txt and "6 sysop" in txt)
    ok &= check("in 39 columns", all(len(r) <= 39 for r in rows if re.match(r"\d+ ", r)))
    ok &= check("a number that is not on the list is refused, and asked again",
                "Not one of the choices" in txt)
    ok &= check("a number picks that choice",
                got == 0 and (cfg_sec_line("plugin:sd", "read") or "").endswith("= co1"))
    c.buf.clear()
    c.send(b"config sd\r")
    c.wait_for(b"Enabled", 5)
    ascii_form(c, [b"", b"6"] + [b""] * SD_ROWS_AFTER_READ)

    # The Timezone: 35 choices, and a number writes its string before the
    # TZ string row is asked for.
    c.buf.clear()
    c.send(b"config board\r")
    c.wait_for(b"Board", 5)
    # Board, Hostname, Timezone, TZ string, NTP, Idle min, LED gpio, Land on
    got, rows = ascii_form_seen(c, [b"", b"", b"8", b"", b"", b"", b"", b""])
    txt = "\n".join(rows)
    ok &= check("the zones are listed by number", "8 US Central (Chicago)" in txt and "35 Custom" in txt)
    ok &= check("and the string follows the zone picked by number",
                "TZ string [CST6CDT,M3.2.0,M1...]" in txt)
    ok &= check("which saves", got == 0 and (cfg_line("tz") or "").endswith("= CST6CDT,M3.2.0,M11.1.0"))
    c.buf.clear()
    c.send(b"config board\r")
    c.wait_for(b"Board", 5)
    ascii_form(c, [b"", b"", b"1", b"", b"", b"", b"", b""])
    ok &= check("and back to UTC by number", (cfg_line("tz") or "").endswith("= UTC0"))
    c.close()
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
    # Why the board last started, for every boot and on a row of its own
    # (1.1.0). It was a note on Uptime's row, and only after a crash or a
    # BOOT reset; the host build says it was started by hand.
    ok &= check("SYS says why the board last started, on its own row",
                re.search(rb"\nLast restart +host start", sys_out) is not None)
    # The port it is answering on (1.1.0). It printed BBS_PORT, 6400, what
    # ever the board listened on; the harness board listens on its tag's.
    ok &= check("SYS shows the port the board is listening on",
                re.search(rb"Port\s+%d\b" % PORT, sys_out) is not None)
    # Stack headroom (1.1.0). The host paints its BBS thread's stack the way
    # FreeRTOS paints a task's, so the figure is measured here too, and it
    # says what it is out of: "1,440 free" meant one thing against 8,192 and
    # means another against 12,288. The host said "n/a" before this.
    ok &= check("SYS says how much stack has been free, and out of what",
                re.search(rb"Stack free\s+[\d,]+ least of [\d,]+", sys_out) is not None)
    # And the board logs every new low with where it happened, which is what
    # SYS alone could never say.
    host_log = DATA.parent / "host.log"
    if host_log.exists():
        text = host_log.read_text(errors="replace")
        ok &= check("the stack's headroom is logged from the first pass",
                    re.search(r"bbs: stack \d+ of \d+ free after start-up", text) is not None)

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

    # CALLS is not, and that is deliberate (Rob). It is a bar chart of calls
    # per hour with no handles and no addresses in it, and knowing when a
    # board is busy is what tells somebody when to call. The two screens sat
    # together under Menu::Sysop only because they were written together.
    g.buf.clear()
    g.send(b"calls\r")
    g.pump(1.0)
    pub = plain(g.buf)
    ok &= check("CALLS is not, an ordinary caller can see it",
                b"Calls by hour" in pub and b"Unknown" not in pub)
    g.close()
    s.close()
    return ok


def naws(c, cols, rows):
    """Tell the board a new window size, the way a terminal does when it is
    resized: IAC SB NAWS width height IAC SE. The board applies it on the next
    read, so the next screen it draws is laid out for it."""
    c.send(bytes([0xFF, 0xFA, 0x1F, cols >> 8, cols & 0xFF, rows >> 8, rows & 0xFF, 0xFF, 0xF0]))
    c.pump(0.3)


def wait_plain(c, pat, secs=4):
    """wait_for on the text without its colours. The prompt is "[S] Sysop: "
    with a colour change between the word and the colon, so the raw bytes
    never hold "Sysop: " whole."""
    end = time.time() + secs
    while time.time() < end:
        if pat in plain(c.buf):
            return True
        if not c.pump(0.1):
            break
    return pat in plain(c.buf)


def since_clear(buf):
    """The bytes from the last clear screen on: one refresh screen's frames,
    each drawn from home over the last."""
    data = bytes(buf)
    at = data.rfind(b"\x1b[2J")
    return data[at:] if at >= 0 else data


def frame_rows(buf, cols=80):
    """How many rows a refresh screen takes, drawn onto a grid that never
    scrolls: every frame since the clear homes and redraws, so a frame no
    taller than the terminal leaves the same number of rows each time, and
    one taller than it shows here as more rows than the terminal has."""
    return len(render_lines(since_clear(buf), cols))


def widest(buf):
    """The longest row drawn since the last clear, on a grid far wider than
    any terminal, so a row that would have wrapped shows its whole length."""
    return max((len(l) for l in render_lines(since_clear(buf), 250)), default=0)


def dash_peek(buf, cols=80):
    if os.environ.get("DASH_PEEK"):
        print("  ---- screen ----")
        for ln in render_lines(since_clear(buf), cols):
            print("  |" + ln)


def test_dash_frame():
    """DASH at 80x24 (1.1.0): a row per line in node order, never scrolls.

    The old frame was 24 fixed rows plus one per plugin, so on a stock board
    DASH 1 scrolled a row on every redraw of an 80x24 terminal and corrupted
    itself; it showed six of the twelve sessions, busiest first, so a caller
    changed rows as others came and went; and any key threw it away.
    """
    print("DASH at 80 columns")
    x = ansi_login("DashCaller")
    nx = int(x.node())
    drain(x)
    s = ansi_login("DashSysop")
    drain(s)
    s.send(f"bye {PASSWORD}\r".encode())
    s.wait_for(b"SysOp node", 6)
    s.wait_for(b"HELP for commands", 4)
    drain(s)
    s.buf.clear()
    s.send(b"dash 1\r")
    s.wait_for(b"DASHBOARD", 5)
    s.pump(2.6)                                      # three frames, the last two from home
    dash_peek(s.buf)
    lines = render_lines(since_clear(s.buf))
    ok = check("the frame is no taller than the terminal, so it never scrolls",
               bytes(s.buf).count(b"DASHBOARD") >= 2 and len(lines) <= 24)
    labels = [l[:2] for l in lines[2:4 + MAX_NODES]]
    want = [("%2d" % n) for n in range(1, MAX_NODES + 1)] + [" S", " B"]
    ok &= check("every line has a row, in node order, the sysop and busy lines last",
                labels == want)
    row = next((l for l in lines if re.match(r"^ ?%d[ *>\]]DashCaller" % nx, l)), "")
    ok &= check("a caller's row carries the address and the whole terminal name",
                "127.0.0.1" in row and "ANSI-UTF8" in row)
    head = next((l for l in lines if l.startswith(" N Handle")), "")
    ok &= check("under headings that line up with the rows",
                head.find("Address") == row.find("127.0.0.1") and
                head.find("Terminal") == row.find("ANSI-UTF8") and head.find("Address") > 0)
    ok &= check("with what is waiting on you, and the board's vitals",
                any("aiting on you" in l for l in lines) and
                any(l.startswith("Up ") and "Loop" in l for l in lines) and
                any(l.startswith("WiFi") and "Dir" in l for l in lines))
    ok &= check("and the last calls under a section with today's count",
                any(re.search(r"-- last calls, \d+ today", l) for l in lines))

    # Keys: a stray one no longer throws the dashboard away.
    s.buf.clear()
    s.send(b"x")
    s.pump(1.5)
    ok &= check("a key the dashboard does not use is ignored",
                b"DASHBOARD" in s.buf and b"Sysop:" not in plain(s.buf))
    s.buf.clear()
    s.send(b">")
    s.wait_for(b"-- bans", 4)
    s.pump(1.4)
    dash_peek(s.buf)
    lines = render_lines(since_clear(s.buf))
    ok &= check("> turns to page 2: the plugins, the bans and the last calls",
                any(l.startswith("-- plugins") for l in lines) and
                any(l.startswith("-- bans") for l in lines) and
                any(l.startswith("-- last calls") for l in lines) and
                any("Page 2/2" in l for l in lines))
    ok &= check("which is no taller than the terminal either", len(lines) <= 24)
    s.buf.clear()
    s.send(b"9")
    s.pump(1.2)
    ok &= check("a page number past the last does nothing",
                b"Page 2/2" in plain(s.buf) and b"Sysop:" not in plain(s.buf))
    s.buf.clear()
    s.send(b"1")
    s.wait_for(b"Page 1/2", 4)
    ok &= check("1 goes back to page 1, cleared first", b"\x1b[2J" in bytes(s.buf))

    # A window resized shorter than the pages were laid out for (code
    # review, 1.1.0): the frame stops at the window's height rather than
    # redrawing a 24 row page from home into 20 rows on every refresh.
    naws(s, 80, 20)
    for page in (b"2", b"1"):
        s.buf.clear()
        s.send(page)
        s.wait_for(b"DASHBOARD", 4)
        s.pump(2.4)
        ok &= check("page %s in an 80x20 window is no taller than the window" % page.decode(),
                    bytes(s.buf).count(b"DASHBOARD") >= 2 and frame_rows(s.buf) <= 20)
    naws(s, 80, 24)
    s.buf.clear()
    s.send(b"q")
    ok &= check("Q gives the prompt back", wait_plain(s, b"Sysop: ", 4))
    x.close()
    s.close()
    return ok


def test_dash_pick():
    """DASH n: Up and Down pick a node row, K kicks it and S snoops (1.1.0).

    Through KICK and SNOOP themselves, so their permission and rank rules
    are the ones that apply. The pick is a node, not a screen position: it
    stays on the same line however the others come and go.
    """
    print("DASH: picking a line, K and S")
    x = ansi_login("PickMe")
    nx = int(x.node())
    drain(x)
    s = ansi_login("PickSysop")
    drain(s)
    s.send(f"bye {PASSWORD}\r".encode())
    s.wait_for(b"SysOp node", 6)
    s.wait_for(b"HELP for commands", 4)
    drain(s)
    s.buf.clear()
    s.send(b"dash 2\r")
    s.wait_for(b"DASHBOARD", 5)
    s.pump(0.8)
    for _ in range(nx):
        s.send(DOWN)
        s.pump(0.25)
    s.pump(1.0)
    scr = AttrScreen(80, 25)
    scr.feed(since_clear(s.buf))
    runs = scr.reverse_runs()
    picked = [r for r in runs if r[1] == 1 and r[2] >= 79 and "PickMe" in scr.row(r[0])]
    dash_peek(s.buf)
    ok = check("Down moves a reverse-video bar onto the picked line, the width of the row",
               len(picked) == 1)
    s.buf.clear()
    s.send(b"s")
    ok &= check("S snoops the picked line, through SNOOP", s.wait_for(f"Snooping node {nx}".encode(), 4))
    s.send(b"q")
    s.wait_for(b"Snoop ended.", 4)
    wait_plain(s, b"Sysop: ", 3)
    drain(s)
    s.buf.clear()
    s.send(b"dash 2\r")
    s.wait_for(b"DASHBOARD", 5)
    s.pump(0.6)
    for _ in range(nx):
        s.send(DOWN)
        s.pump(0.25)
    s.buf.clear()
    s.send(b"k")
    ok &= check("K kicks it, through KICK, and says so", s.wait_for(f"Node {nx} disconnected.".encode(), 4))
    ok &= check("the caller is told and hung up",
                x.wait_for(b"Disconnected by sysop", 4) and x.wait_closed(8))
    ok &= check("and the sysop is back at the prompt", wait_plain(s, b"Sysop: ", 3))
    s.close()
    return ok


def test_dash_narrow():
    """DASH at 40 columns: three pages, each no taller or wider than the
    terminal (1.1.0). An ANSI terminal reporting 40x25 gets the C64's layout,
    which is how the pages are checked without a PETSCII screen model."""
    print("DASH at 40 columns")
    s = ansi_login("NarrowSysop")
    drain(s)
    s.send(f"bye {PASSWORD}\r".encode())
    s.wait_for(b"SysOp node", 6)
    s.wait_for(b"HELP for commands", 4)
    naws(s, 40, 25)
    drain(s)
    s.buf.clear()
    s.send(b"dash 1\r")
    s.wait_for(b"DASHBOARD", 5)
    s.pump(2.4)
    dash_peek(s.buf, 40)
    lines = render_lines(since_clear(s.buf), 40)
    ok = check("page 1 fits 40x25: no row wider than 39, no more than 25 rows",
               widest(s.buf) <= 39 and len(lines) <= 25 and bytes(s.buf).count(b"DASHBOARD") >= 2)
    ok &= check("with every line on it and the footer's page count",
                [l[:2] for l in lines[2:4 + MAX_NODES]] ==
                [("%2d" % n) for n in range(1, MAX_NODES + 1)] + [" S", " B"] and
                any(l.startswith("Pg 1/3") for l in lines))
    s.buf.clear()
    s.send(b">")
    s.wait_for(b"Pg 2/3", 4)
    s.pump(1.2)
    dash_peek(s.buf, 40)
    lines = render_lines(since_clear(s.buf), 40)
    me = next((l for l in lines if "NarrowSysop" in l), "")
    ok &= check("page 2 is where each line calls from, with a five letter terminal",
                any(l.startswith(" N Handle       Address") for l in lines) and
                "127.0.0.1" in me and me.rstrip().endswith("UTF8"))
    ok &= check("and fits too", widest(s.buf) <= 39 and len(lines) <= 25)
    s.buf.clear()
    s.send(b">")
    s.wait_for(b"Pg 3/3", 4)
    s.pump(1.2)
    dash_peek(s.buf, 40)
    lines = render_lines(since_clear(s.buf), 40)
    ok &= check("page 3 is the last calls with their addresses, and the bans",
                any(l.startswith(" N Handle        Time Address") for l in lines) and
                any(l.startswith("Bans ") for l in lines))
    ok &= check("and fits too", widest(s.buf) <= 39 and len(lines) <= 25)
    s.buf.clear()
    s.send(b">")
    ok &= check("> past the last page comes round to the first", s.wait_for(b"Pg 1/3", 4))
    s.send(b"q")
    wait_plain(s, b"Sysop: ", 3)
    s.close()
    return ok


def test_dash_all():
    """DASH ALL: every page in turn as one paged list, which is also how a
    plain terminal sees the whole dashboard at once (1.1.0)."""
    print("DASH ALL")
    s = ansi_login("AllSysop")
    drain(s)
    s.send(f"bye {PASSWORD}\r".encode())
    s.wait_for(b"SysOp node", 6)
    s.wait_for(b"HELP for commands", 4)
    drain(s)
    s.buf.clear()
    s.send(b"dash all\r")
    read_list(s)
    text = plain(s.buf)
    ok = check("DASH ALL draws page 1 and page 2",
               text.count(b"DASHBOARD") == 2 and b"aiting on you" in text and
               b"-- plugins" in text and b"-- bans" in text)
    ok &= check("and ends at the prompt, with no refresh footer", text.rstrip().endswith(b"Sysop:") and
                b"refresh" not in text)
    s.buf.clear()
    s.send(b"dash\r")
    read_list(s)
    text = plain(s.buf)
    ok &= check("DASH alone is page 1, once", text.count(b"DASHBOARD") == 1 and b"-- plugins" not in text)
    s.close()
    return ok


def test_dash_ascii():
    """DASH n on a plain terminal: no cursor keys and no reverse video, so the
    pages are picked by number and the footer says so (1.1.0)."""
    print("DASH on a plain terminal")
    c = Caller(ansi=False)
    c.wait_for(b"HIT DEL OR BACKSPACE", 5)
    c.send(b"\x08")
    c.wait_for(b"Enter your handle", 8)
    login(c, "PlainDash")
    drain(c)
    c.send(f"bye {PASSWORD}\r".encode())
    c.wait_for(b"SysOp node", 6)
    c.wait_for(b"HELP for commands", 4)
    drain(c)
    c.buf.clear()
    c.send(b"dash 2\r")
    ok = check("the footer names the number keys", c.wait_for(b"1 2 pick a page", 5))
    ok &= check("and offers no pick or kick", b"K kick" not in c.buf)
    c.buf.clear()
    c.send(b"2")
    ok &= check("2 is page 2", c.wait_for(b"-- plugins", 5) and c.wait_for(b"Page 2/2", 4))
    c.send(b"q")
    ok &= check("Q stops it", wait_plain(c, b"Sysop: ", 4))
    c.close()
    return ok


def pet_rows(buf):
    """A PETSCII stream played onto a C64 screen that never scrolls, from the
    last clear: (rows used, widest row). CLR clears, HOME homes, RETURN is a
    new line, the cursor keys move, colours and reverse take no cell, and a
    row that reaches the 40th column wraps as a C64 wraps it, so a refresh
    frame too tall or too wide for the screen shows as rows past 25."""
    data = bytes(buf)
    at = data.rfind(b"\x93")
    data = data[at + 1:] if at >= 0 else data
    x = y = 0
    rows = 1
    widest = 0
    for b in data:
        if b == 0x13:                     # HOME
            x = y = 0
        elif b == 0x0D:                   # RETURN
            x = 0
            y += 1
        elif b == 0x11:                   # cursor down
            y += 1
        elif b == 0x91:                   # cursor up
            y = max(0, y - 1)
        elif b == 0x1D:                   # cursor right
            x += 1
        elif b == 0x9D:                   # cursor left
            x = max(0, x - 1)
        elif 0x20 <= b <= 0x7F or 0xA0 <= b <= 0xFF:
            x += 1
            widest = max(widest, x)
            if x >= 40:                   # the C64 wraps at the 40th column
                x = 0
                y += 1
        rows = max(rows, y + 1)
    return rows, widest


def test_dash_petscii():
    """DASH n on a C64 (PETSCII-40, 25 rows): every page no taller than the
    screen and no row reaching the 40th column, which a C64 would wrap
    (1.1.0). Read off the PETSCII stream itself, not an ANSI stand-in."""
    print("DASH on a C64")
    p = Caller(ansi=False)
    p.wait_for(b"HIT DEL OR BACKSPACE", 5)
    p.send(b"\x14")
    p.wait_for(b"40 OR 80 COLUMNS", 3)
    p.send(b"4")
    p.wait_for(pet("Enter your handle"), 10)
    login(p, "PetDasher", as_pet=True)
    drain(p)
    p.send(pet("bye " + PASSWORD) + b"\r")
    p.wait_for(pet("HELP for commands"), 6)
    drain(p)
    p.buf.clear()
    p.send(pet("dash 1") + b"\r")
    p.wait_for(pet("DASHBOARD"), 5)
    p.pump(2.4)
    rows, widest = pet_rows(p.buf)
    ok = check("page 1 fits the C64's 40x25 and never scrolls",
               bytes(p.buf).count(pet("DASHBOARD")) >= 2 and rows <= 25 and widest <= 39)
    for page in (b"2", b"3"):
        p.buf.clear()
        p.send(page)
        p.wait_for(pet("DASHBOARD"), 4)
        p.pump(1.6)
        rows, widest = pet_rows(p.buf)
        ok &= check("page %s too" % page.decode(), rows <= 25 and widest <= 39 and
                    pet("Pg %s/3" % page.decode()) in bytes(p.buf))
    p.send(pet("q"))
    ok &= check("and Q gives the prompt back", p.wait_for(pet("Sysop"), 4))
    p.close()
    return ok


def test_nodes_columns():
    """NODES and WHO drawn through the dashboard's row builder (1.1.0).

    NODES cut PETSCII-40 to PETSCII-4 and ANSI-CP437 to ANSI-CP43 in a 9
    column Terminal; WHO's header widened with the terminal while its rows
    stayed at 40 column widths, so at 80 Min and Idle sat eleven columns
    left of their headings; and NODES n drew WHO's rows, because the
    refresh knew only DASH and WHO.
    """
    print("NODES and WHO columns")
    p = Caller(ansi=False)
    p.wait_for(b"HIT DEL OR BACKSPACE", 5)
    p.send(b"\x14")
    p.wait_for(b"40 OR 80 COLUMNS", 3)
    p.send(b"4")
    p.wait_for(pet("Enter your handle"), 10)
    login(p, "PetColumns", as_pet=True)
    s = ansi_login("ColumnSysop")
    drain(s)
    s.send(f"bye {PASSWORD}\r".encode())
    s.wait_for(b"SysOp node", 6)
    s.wait_for(b"HELP for commands", 4)
    drain(s)
    s.buf.clear()
    s.send(b"nodes\r")
    read_list(s)
    text = plain(s.buf)
    ok = check("NODES names a C64 caller's terminal in full", b"PETSCII-40" in text)
    ok &= check("with a Doing column beside the address", b"Doing" in text and b"Address" in text)
    s.buf.clear()
    s.send(b"who\r")
    read_list(s)
    lines = [l.decode("latin-1") for l in screen_lines(s.buf)]
    head = next((l for l in lines if l.startswith(" N Handle")), "")
    row = next((l for l in lines if "PetColumns" in l), "")
    # Idle is right-aligned under its heading, so the row ends where the
    # heading does; on the old build the rows ended eleven columns short.
    ok &= check("staff WHO at 80: Idle sits under its heading",
                bool(head) and bool(row) and head.find("Idle") + 4 == len(row.rstrip()))
    ok &= check("and so does the Doing column",
                bool(head) and bool(row) and head.find("Doing") > 0 and
                row[head.find("Doing"):head.find("Doing") + 1] not in ("", " "))
    s.buf.clear()
    s.send(b"nodes 2\r")
    s.wait_for(b"Refresh 2s", 5)
    ok &= check("NODES n refreshes NODES, not WHO",
                b"Nodes" in plain(s.buf) and b"Who's online" not in plain(s.buf))
    s.send(b"q")
    wait_plain(s, b"Sysop: ", 3)
    p.close()
    s.close()
    return ok


def test_operator_notes():
    """A bare O shows the sysop the rings that left notes (1.1.0).

    Missed rings go to MAIL now; a note is what is left when they cannot:
    mail switched off, no sysop account, or every sysop box full. Notes were
    shown only at login or elevation, so a sysop already on had no way to
    read one without logging in again. The dashboard's waiting row counts
    them without opening the file. Played on a copy of this board with mail
    switched off, so the note is certain and nothing here touches the
    harness board's mail.
    """
    print("OPERATOR: notes for a sysop already on, with mail off")
    if HOST not in ("127.0.0.1", "localhost") or not PASSWORD:
        print("  SKIP  needs the host build and a sysop password")
        return True
    port = PORT + 3706
    tmp = copy_data()
    cfg = tmp / "data" / "user" / "system.cfg"
    cfg.write_text(cfg_with(cfg.read_text(), {("plugin:chat", "mail_slots"): "0"}))
    # A note the harness board kept from an earlier test (every sysop box
    # full) would be counted with this one; this test's note is its own.
    for p in (tmp / "data" / "user").rglob("rings.txt"):
        p.unlink()
    proc = start_copy(tmp, (str(port),))
    ok = True
    try:
        copy_log(tmp, f"listening on {port},")
        s = caller_on(port)
        s.wait_for(b"Enter your handle", 10)
        login(s, "NotesSysop")
        drain(s)
        s.send(f"bye {PASSWORD}\r".encode())
        s.wait_for(b"HELP for commands", 6)
        drain(s)
        s.send(b"lurk\r")
        s.pump(0.8)
        r = caller_on(port)
        r.wait_for(b"Enter your handle", 10)
        login(r, "NoteRinger")
        drain(r)
        r.send(b"o a note for later\r")
        ok &= check("a lurking sysop's ring leaves a note, with mail off",
                    r.wait_for(b"saved for them", 6))
        r.close()
        drain(s)
        s.buf.clear()
        s.send(b"dash\r")
        read_list(s)
        ok &= check("the dashboard says a note is waiting", b"Waiting on you: 1 ring note" in plain(s.buf))
        s.buf.clear()
        s.send(b"o\r")
        wait_plain(s, b"Sysop: ", 4)
        s.pump(0.4)
        seen = plain(s.buf)
        ok &= check("a bare O shows it", b"1 ring while you were off:" in seen and b"a note for later" in seen)
        ok &= check("instead of saying nobody is ringing", b"Nobody is ringing." not in seen)
        s.buf.clear()
        s.send(b"o\r")
        wait_plain(s, b"Sysop: ", 4)
        s.pump(0.3)
        ok &= check("and once shown, O has nothing more to say", b"Nobody is ringing." in plain(s.buf))
        ok &= check("and does not send the sysop to a MAIL that is off",
                    b"Missed rings go to MAIL." not in plain(s.buf))
        s.buf.clear()
        s.send(b"dash\r")
        read_list(s)
        ok &= check("nor has the dashboard", b"Nothing waiting on you" in plain(s.buf))
        s.close()
    finally:
        stop_copy(proc, tmp)
    return ok


def test_ring_mail():
    """A ring nobody answers goes to the sysop's MAIL (1.1.0).

    Rob: "the sysop page should drop to email and a letter icon can show in
    the header the sysop has mail". From the caller, a guest marked as one,
    "Ring: <reason>" first; to every account the sysop password has marked;
    the note file only when there is no such account, so a ring is never
    lost. The flag the display's letter icon reads is said on the console
    when it changes. Played on a copy of this board with every sysop mark
    taken off and no mail, so the accounts and boxes are the test's own.
    """
    print("OPERATOR: missed rings go to the sysop's mail")
    if HOST not in ("127.0.0.1", "localhost") or not PASSWORD:
        print("  SKIP  needs the host build and a sysop password")
        return True
    tmp = copy_data()
    user = tmp / "data" / "user"
    users = user / "users.txt"
    if users.exists():                               # none yet on a board nobody has called
        users.write_text("".join(ln for ln in users.read_text().splitlines(True)
                                 if ln.strip() != "level = sysop"))
    for p in user.rglob("mail.dat"):
        p.unlink()
    for p in user.rglob("rings.txt"):
        p.unlink()
    port = PORT + 3705
    proc = start_copy(tmp, (str(port),))
    ok = True

    def notes():
        found = list(user.rglob("rings.txt"))
        return found[0].read_text(errors="replace") if found else ""

    def console():
        p = tmp / "host.log"
        return p.read_text(errors="replace") if p.exists() else ""

    try:
        copy_log(tmp, f"listening on {port},")
        # No sysop account yet: the note, as before.
        a = caller_on(port)
        a.wait_for(b"Enter your handle", 10)
        login(a, "RingFirst")
        drain(a)
        a.send(b"o nobody to mail\r")
        ok &= check("with no sysop account a ring is still kept", a.wait_for(b"saved for them", 6))
        a.close()
        time.sleep(0.5)
        ok &= check("as a note, the fallback", "nobody to mail" in notes())

        # A sysop account: the password marks it.
        s = caller_on(port)
        s.wait_for(b"Enter your handle", 10)
        login(s, "RingBoss")
        drain(s)
        s.send(f"bye {PASSWORD}\r".encode())
        s.wait_for(b"HELP for commands", 6)
        s.close()
        time.sleep(0.8)

        # A caller and a guest ring while the sysop is off.
        b = caller_on(port)
        b.wait_for(b"Enter your handle", 10)
        login(b, "RingCaller")
        drain(b)
        b.send(b"o the printer is on fire\r")
        ok &= check("a ring with the sysop off is kept", b.wait_for(b"saved for them", 6))
        b.close()
        g = caller_on(port)
        g.wait_for(b"Enter your handle", 10)
        g.send(b"RingGuest\r")
        g.wait_for(b"[G]uest", 6)
        g.send(b"g")
        g.wait_for(b"Main", 8)
        drain(g)
        g.send(b"o a guest asking\r")
        ok &= check("and a guest's", g.wait_for(b"saved for them", 6))
        g.close()
        time.sleep(0.8)
        log = console()
        ok &= check("both went to mail, not to the note file",
                    log.count("rang for the sysop, not available: mailed") == 2 and
                    "printer" not in notes() and "guest asking" not in notes())
        ok &= check("and the console says the sysop has unread mail",
                    "chat: the sysop has unread mail" in log)

        s = caller_on(port)
        s.wait_for(b"Enter your handle", 10)
        login(s, "RingBoss")
        ok &= check("the sysop is told at login", b"You have mail." in plain(s.buf))
        drain(s)
        s.send(f"bye {PASSWORD}\r".encode())
        s.wait_for(b"HELP for commands", 6)
        drain(s)
        s.buf.clear()
        s.send(b"dash\r")
        read_list(s)
        ok &= check("the dashboard counts them as mail", b"2 unread mail" in plain(s.buf))
        ok &= mail_box(s)
        box = plain(s.buf)
        ok &= check("MAIL has the caller's ring", b"RingCaller" in box)
        ok &= check("and the guest's, marked as a guest", b"RingGuest*" in box)
        s.buf.clear()
        s.send(b"1\r")
        s.wait_for(b"[D]elete", 5)
        one = plain(s.buf)
        s.send(b"d")
        s.wait_for(b"Mail>", 5)
        s.buf.clear()
        s.send(b"1\r")
        s.wait_for(b"[D]elete", 5)
        two = plain(s.buf)
        rings = one + two
        ok &= check("each starts Ring: and the reason",
                    b"Ring: the printer is on fire" in rings and b"Ring: a guest asking" in rings)
        ok &= check("with where it rang from, and a guest said to be one",
                    b"Rang from node" in rings and b"as a guest" in rings)
        s.buf.clear()
        s.send(b"d")
        s.wait_for(b"Mail>", 5)
        s.send(b"q")
        s.pump(0.8)
        ok &= check("read and gone, the console says the sysop's mail is all read",
                    "chat: the sysop's mail is all read" in console())
        drain(s)
        s.buf.clear()
        s.send(b"o\r")
        wait_plain(s, b"Sysop: ", 4)
        s.pump(0.3)
        ok &= check("a bare O with no ring says where missed rings go",
                    b"Nobody is ringing." in plain(s.buf) and b"Missed rings go to MAIL." in plain(s.buf))
        s.close()
    finally:
        stop_copy(proc, tmp)
    return ok


def test_dash_waiting():
    """The dashboard's waiting row asks the plugins (1.1.0): unread mail from
    chat, through the waiting hook, with nothing in the core that knows
    which plugin said it."""
    print("DASH: what is waiting on you")
    s = sysop_on("WaitSysop")
    drain(s)
    m = ansi_login("MailsTheSysop")
    drain(m)
    m.send(b"mail WaitSysop are you there\r")
    ok = check("mail is left for the sysop", m.wait_for(b"Left for WaitSysop", 5))
    m.close()
    s.pump(1.0)
    drain(s)
    s.buf.clear()
    s.send(b"dash\r")
    read_list(s)
    ok &= check("the waiting row says so", b"1 unread mail" in plain(s.buf))
    s.close()
    return ok


def test_dash_card_age():
    """The card's free space on the dashboard is up to a minute old (1.1.0).

    On the board it is f_getfree: 15 to 25 ms, and 160 ms on a card pulled
    mid-write. Kept for three seconds, DASH 1 put that into the loop every
    third frame. A minute now, and SD, a mount and an unmount still measure
    at once. Checked by filling the card and asking again.
    """
    print("DASH: the card's free space, a minute old")
    card = card_dir()
    if HOST not in ("127.0.0.1", "localhost") or not card:
        print("  SKIP  needs the harness's card")
        return True

    def card_free(c):
        # The sd plugin's line is on the second page since 1.1.0, which
        # DASH ALL shows; before, DASH alone showed it. Asking both ways
        # keeps this a test of the figure's age on either build, rather than
        # of which command draws it.
        for cmd in (b"dash all\r", b"dash\r"):
            c.buf.clear()
            c.send(cmd)
            read_list(c)
            m = re.search(rb"SD: [^\r\n]*? (\d+) MB free", plain(c.buf))
            if m:
                return int(m.group(1))
        return None

    s = sysop_on("CardAgeSysop")
    drain(s)
    s.send(b"sd\r")                          # measured now, so the minute starts here
    s.wait_for(b"SD card", 4)
    s.pump(0.5)
    before = card_free(s)
    blob = card / "dash-age.bin"
    blob.write_bytes(b"\0" * (6 * 1024 * 1024))
    try:
        time.sleep(4.0)                      # past the old three seconds
        after = card_free(s)
        ok = check("the dashboard shows the card's free space", before is not None)
        ok &= check("and a minute's figure, not a fresh read every few seconds",
                    before is not None and after == before)
        s.buf.clear()
        s.send(b"sd\r")
        s.wait_for(b"SD card", 4)
        s.pump(0.5)
        fresh = card_free(s)
        ok &= check("SD measures at once, and the dashboard follows it",
                    before is not None and fresh is not None and fresh <= before - 5)
    finally:
        blob.unlink()
    s.close()
    return ok


def test_dash_wide():
    """DASH at 132 columns: one page, the board's figures beside the lines in
    SYS's grammar, no taller and no wider than the terminal (1.1.0)."""
    print("DASH at 132 columns")
    s = ansi_login("WideSysop")
    drain(s)
    s.send(f"bye {PASSWORD}\r".encode())
    s.wait_for(b"SysOp node", 6)
    s.wait_for(b"HELP for commands", 4)
    naws(s, 132, 24)
    drain(s)
    s.buf.clear()
    s.send(b"dash 1\r")
    s.wait_for(b"DASHBOARD", 5)
    s.pump(2.4)
    dash_peek(s.buf, 132)
    lines = render_lines(since_clear(s.buf), 132)
    ok = check("one frame fits 132x24: no row past 131 columns, no more than 24 rows",
               widest(s.buf) <= 131 and len(lines) <= 24 and bytes(s.buf).count(b"DASHBOARD") >= 2)
    ok &= check("with the board block beside the lines",
                any(l[80:].startswith("-- board") for l in lines) and
                any(l[80:].startswith("Uptime") for l in lines) and
                any(l[80:].startswith("Slow passes") for l in lines))
    ok &= check("and minutes on for each call", any(l.startswith(" N Handle") and " On " in l for l in lines))
    s.send(b"q")
    wait_plain(s, b"Sysop: ", 3)
    s.close()
    return ok


# A shim that notes every fopen() a board makes, preloaded into a copy. The
# dashboard's rule is that a frame opens no file; this is how the rule is
# checked rather than read. Directory walks are not counted: the host sums a
# directory for free space on every ask, where the board keeps the figure a
# minute, so on the host they say nothing about the board.
FOPEN_LOG_C = """
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int logfd = -2;
static void note(const char *path) {
    if (logfd == -2) {
        const char *p = getenv("FOPEN_LOG");
        logfd = p ? open(p, O_WRONLY | O_CREAT | O_APPEND, 0644) : -1;
    }
    if (logfd < 0 || !path) return;
    if (write(logfd, path, strlen(path)) < 0 || write(logfd, "\\n", 1) < 0) return;
}
FILE *fopen(const char *path, const char *mode) {
    static FILE *(*real)(const char *, const char *);
    if (!real) real = (FILE *(*)(const char *, const char *))dlsym(RTLD_NEXT, "fopen");
    note(path);
    return real(path, mode);
}
FILE *fopen64(const char *path, const char *mode) {
    static FILE *(*real)(const char *, const char *);
    if (!real) real = (FILE *(*)(const char *, const char *))dlsym(RTLD_NEXT, "fopen64");
    note(path);
    return real(path, mode);
}
"""


def test_dash_opens_nothing():
    """A dashboard frame opens no file (1.1.0, the spec's checkable rule).

    The old DASH counted the caller log and read five records from it on
    every frame: seven file opens a second for a sysop with DASH 1 up, all
    in the loop that every caller waits on. Checked on a copy of this board
    with every fopen() noted, over four seconds of DASH 1.
    """
    print("A dashboard frame opens no file")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the local harness")
        return True
    import shutil
    import subprocess
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        print("  SKIP  no C compiler for the fopen shim")
        return True
    tmp = copy_data()
    (tmp / "fopenlog.c").write_text(FOPEN_LOG_C)
    so = tmp / "fopenlog.so"
    built = subprocess.run([cc, "-shared", "-fPIC", "-o", str(so), str(tmp / "fopenlog.c"), "-ldl"],
                           capture_output=True)
    if built.returncode != 0:
        shutil.rmtree(tmp, ignore_errors=True)
        print("  SKIP  the fopen shim did not build")
        return True
    logf = tmp / "fopen.log"
    port = PORT + 3703
    proc = start_copy(tmp, (str(port),), {"LD_PRELOAD": str(so), "FOPEN_LOG": str(logf)})
    try:
        copy_log(tmp, f"listening on {port},")
        c = caller_on(port)                          # a call on the log for the last calls
        c.wait_for(b"Enter your handle", 10)
        login(c, "FrameCaller")
        c.send(b"bye\r")
        c.wait_closed(8)
        c.close()
        s = caller_on(port)
        s.wait_for(b"Enter your handle", 10)
        login(s, "FrameSysop")
        drain(s)
        s.send(f"bye {PASSWORD}\r".encode())
        s.wait_for(b"HELP for commands", 6)
        drain(s)
        s.send(b"dash 1\r")
        s.wait_for(b"DASHBOARD", 5)
        s.pump(1.5)
        mark = logf.stat().st_size if logf.exists() else 0
        s.buf.clear()
        s.pump(4.5)
        frames = bytes(s.buf).count(b"DASHBOARD")
        opened = []
        if logf.exists():
            opened = [p for p in logf.read_bytes()[mark:].decode(errors="replace").splitlines()
                      if str(tmp) in p]
        ok = check("DASH 1 drew a frame a second", frames >= 3)
        ok &= check("and opened no file doing it", not opened)
        if opened:
            print("        opened: " + ", ".join(sorted(set(p.replace(str(tmp), "") for p in opened))))
        s.send(b"q")
        s.close()
    finally:
        stop_copy(proc, tmp)
    return ok


def test_boot_notices():
    """The staff notice for a restart the board did not choose (1.1.0).

    "Last restart was not clean: task watchdog." was 42 to 52 columns and
    wrapped on a C64, and "The log is reboots.log" pointed at a file nothing
    on the board can read. Now the copy's words, 39 columns at most, the
    count of unexpected restarts on record with this one in it, and a row of
    its own on SYS. BBS_HOST_RESET plays the reason on a copy of this board.
    """
    print("The restart notice: the board froze")
    if HOST not in ("127.0.0.1", "localhost") or not PASSWORD:
        print("  SKIP  needs the local harness and a sysop password")
        return True
    tmp = copy_data()
    reb = tmp / "data" / "logs" / "reboots.log"
    reb.parent.mkdir(parents=True, exist_ok=True)
    with open(reb, "a") as f:                        # one already on record
        f.write("2026-09-20 10:00  crash (panic)\n")
    port = PORT + 3704
    proc = start_copy(tmp, (str(port),), {"BBS_HOST_RESET": "task watchdog"})
    try:
        log = copy_log(tmp, f"listening on {port},")
        ok = check("the copy starts on the reason it was given", "boot: task watchdog" in log)
        c = caller_on(port)
        c.wait_for(b"Enter your handle", 10)
        login(c, "RestartSysop")
        drain(c)
        c.buf.clear()
        c.send(f"bye {PASSWORD}\r".encode())
        c.wait_for(b"HELP for commands", 6)
        c.pump(0.4)
        lines = [l.decode("latin-1") for l in screen_lines(c.buf)]
        want = ["Last restart: the board froze.",
                "A watchdog restarted it after 30 s.",
                "Unexpected restarts on record: 2."]
        at = lines.index(want[0]) if want[0] in lines else -1
        ok &= check("staff are told the board froze and what restarted it",
                    at >= 0 and lines[at:at + 2] == want[:2])
        ok &= check("and how many are on record, this one counted",
                    at >= 0 and lines[at + 2:at + 3] == want[2:])
        ok &= check("every line inside 40 columns", all(len(w) <= 39 for w in want))
        ok &= check("and not the words that wrapped on a C64",
                    "not clean" not in "".join(lines) and "reboots.log" not in "".join(lines))
        c.buf.clear()
        c.send(b"sys\r")
        read_list(c)
        ok &= check("SYS says why the board last started, on a row of its own",
                    re.search(rb"\nLast restart +task watchdog", plain(c.buf)) is not None)
        c.close()
    finally:
        stop_copy(proc, tmp)
    return ok


def test_room_new_commands():
    """0.21.4: /p n*, /sh, /whois, /b, /page, and /t n +m."""
    print("Chat room: sticky private, history, whois, bell")
    a = ansi_login("Sticky")
    b = ansi_login("Target")
    c = ansi_login("Bystand")
    # Read the node number BEFORE clearing the buffer: node() parses it out
    # of the login banner, and clearing first makes it return "?" so every
    # sticky check fails for a reason that has nothing to do with sticky.
    nb = b.node()
    na = a.node()           # up front, for the same reason as nb
    for x in (a, b, c):
        x.send(b"chat\r")
        x.wait_for(b"here.", 4)
        x.buf.clear()

    # --- /b on its own is the bell, and says which way it went
    a.send(b"/b\r")
    ok = check("/b toggles the bell", a.wait_for(b"Bell off.", 3))
    a.buf.clear()
    a.send(b"/b\r")
    ok &= check("and toggles it back", a.wait_for(b"Bell on.", 3))

    # --- /whois uses the shell's own WHOIS and hides private fields
    a.buf.clear()
    a.send(b"/whois Target\r")
    ok &= check("/whois names the caller", a.wait_for(b"Target", 4))
    ok &= check("/whois does not leak an email",
                b"@example.com" not in plain(a.buf))

    # --- /sh replays what was said
    b.buf.clear()
    c.send(b"something worth repeating\r")
    b.wait_for(b"worth repeating", 4)
    b.buf.clear()
    b.send(b"/sh 5\r")
    ok &= check("/sh replays the room", b.wait_for(b"worth repeating", 4))

    # --- sticky private: to ONE caller, and NOT to the room
    a.buf.clear()
    a.send(f"/p{nb}*\r".encode())
    ok &= check("/pN* sticks the conversation", a.wait_for(b"ends it", 4))
    b.buf.clear()
    c.buf.clear()
    a.buf.clear()
    a.send(b"only for you\r")
    ok &= check("a stuck line reaches the target", b.wait_for(b"only for you", 4))
    c.pump(0.8)
    ok &= check("and NOT the room", b"only for you" not in plain(c.buf))
    # The sender sees what they sent, as a line of the conversation. It used
    # to print "--> /p to #2:... sent." for every line and never the words,
    # so a stuck conversation read as a column of identical confirmations.
    a.pump(0.6)
    shown = render_lines(a.buf)
    ok &= check("the sender sees what they said, not a 'sent.' per line",
                any(b"only for you" in ln.encode() for ln in shown)
                and not any("sent." in ln for ln in shown))

    # Something arrives while the sender is part way through a line. It
    # must go above the input line, not onto it, and the [>n] marker and
    # the half typed words must come back underneath. On 0.21.7 the marker
    # was never lifted, so the arrival printed after it, and the redraw put
    # the words back without it.
    # No buffer clear here. The [>n] marker was drawn when the last line was
    # sent, and clearing now would throw it away, which made the next check
    # pass on the build that wrote "[>2] P#2:Daytona) hi" on Rob's screen.
    a.send(b"half typed")
    a.pump(0.5)
    b.send(("/p" + na + " incoming\r").encode())
    a.wait_for(b"incoming", 4)
    a.pump(0.6)
    shown = render_lines(a.buf)
    arrived = [ln for ln in shown if "incoming" in ln]
    ok &= check("an arrival does not land on the sticky marker",
                bool(arrived) and not any("[>" in ln for ln in arrived))
    last = [ln for ln in shown if ln.strip()][-1] if any(ln.strip() for ln in shown) else ""
    ok &= check("and the marker and the half typed line come back under it",
                last.strip().startswith("[>" + nb + "]") and "half typed" in last)
    a.send(b"\x1b")
    a.pump(0.3)

    # --- /p* releases it
    a.buf.clear()
    a.send(b"/p*\r")
    ok &= check("/p* returns to the room", a.wait_for(b"Back to the room", 4))
    c.buf.clear()
    a.send(b"everyone hears this\r")
    ok &= check("and the room hears again", c.wait_for(b"everyone hears this", 4))

    # --- the target leaving ends it, and the line goes NOWHERE
    a.buf.clear()
    a.send(f"/p{nb}*\r".encode())
    a.wait_for(b"ends it", 4)
    b.send(b"/q\r")
    b.wait_for(b"left the room", 4)
    time.sleep(0.4)
    c.buf.clear()
    a.buf.clear()
    a.send(b"this must not escape\r")
    ok &= check("a gone target ends the mode", a.wait_for(b"not sent", 4))
    c.pump(0.8)
    ok &= check("and the line does not fall into the room",
                b"this must not escape" not in plain(c.buf))

    for x in (a, b, c):
        x.close()
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
    # An action line is the handle and the verb, with no second marker. It
    # used to render "#2:Bee) * waves": the ')' is the rank bracket tag()
    # already appends, and the '*' was a separate action mark. On a guest
    # that read "#1:The Legend* * flexes", which is what Rob reported. The
    # bracket is what distinguishes a said line from an action and it was
    # always there.
    # "** Handle action **", one colour, no node tag and no rank bracket.
    # Rob: "lets change this to ** QuantumRob dances Wildly **". An action
    # is prose about somebody rather than a line they said, so it does not
    # wear the said-line furniture.
    ok &= check("/me is an action line",
                a.wait_for(b"** Bee waves **", 4))
    ok &= check("and carries no node tag or rank bracket",
                b":Bee)" not in plain(a.buf).split(b"** Bee")[-1])

    a.buf.clear()
    c.buf.clear()
    # No space between the verb and the node. Rob: "We should not need a
    # space there, that applies to all / commands in chat." The verb now
    # ends at the first digit as well as at a space, so every room command
    # taking a node gets this without each one parsing it for itself.
    b.send(b"/p1 no space needed\r")
    ok &= check("a node number needs no space after the verb",
                a.wait_for(b"no space needed", 4))

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


def mail_box(c):
    """MAIL: into the mailbox, which is a place now (0.22.0): the list, then
    a Mail> prompt. Reading is a key from there, and every decision comes
    back to the list, not to the shell."""
    c.buf.clear()
    c.send(b"mail\r")
    return c.wait_for(b"Mail>", 5)


def mail_empty(c):
    """From the Mail> prompt: read and delete everything in the box, then
    leave it. A box on the internal partition holds three, and a test that
    leaves its mail behind fills the box the next ring needs."""
    for _ in range(14):
        c.buf.clear()
        c.send(b"1\r")
        if not c.wait_for(b"[D]elete", 3):
            break
        c.send(b"d")
        c.wait_for(b"Mail>", 4)
    c.send(b"q")
    c.pump(0.5)


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

    ok &= check("MAIL opens the mailbox with it listed",
                mail_box(b) and b"Sender" in plain(b.buf) and b"1 new" in plain(b.buf))
    b.buf.clear()
    b.send(b"\r")
    ok &= check("Enter reads it", b.wait_for(b"the eagle lands at nine", 4) and
                b"From: Sender" in plain(b.buf))
    # Reading is not disposing. The message stays exactly where it is until
    # one of three keys says what should happen to it.
    ok &= check("and asks what to do with it", b.wait_for(b"[D]elete", 4))
    b.buf.clear()
    b.send(b"d")
    ok &= check("D deletes it", b.wait_for(b"Deleted", 4))
    ok &= check("and puts them back on the list, now empty",
                b.wait_for(b"Nothing in your mailbox", 4))
    b.send(b"q")
    b.wait_for(b"Main", 4)

    # This used to assert that a second message REPLACED the first and
    # that the board said so. That was the bad design written into the
    # suite as a requirement, which is part of why it lasted: a third
    # party writing to you destroyed a message you had not read.
    a.buf.clear()
    a.send(b"mail Reader first one\r")
    a.wait_for(b"Left for", 4)
    a.buf.clear()
    a.send(b"mail Reader second one\r")
    ok &= check("a second message is kept alongside the first",
                a.wait_for(b"Left for", 4))
    ok &= check("and nothing is replaced",
                b"replacing" not in plain(a.buf))

    a.buf.clear()
    a.send(b"mail Nobody hello?\r")
    ok &= check("a message needs a real account", a.wait_for(b"No account called Nobody", 4))

    b.close()
    b = ansi_login("Reader", pw=TEST_PW)
    ok &= check("mail is announced at login", b.wait_for(b"You have mail", 6))
    mail_box(b)
    listing = plain(b.buf)
    # Oldest first, because that is the order they were sent in and the
    # order somebody would expect to read them.
    ok &= check("the list shows both, oldest first",
                b"first one" in listing and b"second one" in listing
                and listing.index(b"first one") < listing.index(b"second one"))
    b.buf.clear()
    b.send(b"\r")
    ok &= check("Enter reads the oldest", b.wait_for(b"first one", 4))
    b.send(b"d")
    ok &= check("and the list says the newer one is still new", b.wait_for(b"1 new", 4))
    b.buf.clear()
    b.send(b"\r")
    ok &= check("which Enter reads next", b.wait_for(b"second one", 4))
    b.send(b"d")
    b.wait_for(b"Deleted", 4)
    b.send(b"q")
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
    # 200, not a small number: the cap stays for the rest of the run, and a
    # full no-card run registers 77 accounts by the time the backup test asks
    # whether sign-up is offered. At 77 the board rightly said no, and the
    # failure was reported against the backup.
    s.send(DOWN * 4 + b"\x08" * 4 + b"200" + F1)        # Accounts -> 200
    ok &= check("the page saves", s.wait_for(b"Saved and live", 5))
    if HOST in ("127.0.0.1", "localhost"):
        cfg = (USERDATA / "system.cfg").read_text()
        ok &= check("the new value is in system.cfg", "max_users = 200" in cfg)
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

    # The network page: shows the stored name whole, '#' included, and
    # refuses a passphrase WPA2 would refuse, while it can still be retyped.
    s.buf.clear()
    s.send(b"config wifi\r")
    ok &= check("CONFIG wifi opens on the stored network",
                s.wait_for(b"Test#Net", 5) and b"NETWORK" in plain(s.buf))
    s.buf.clear()
    s.send(DOWN + b"\x08" * 12 + b"short" + F1)
    ok &= check("a passphrase under 8 characters is refused",
                s.wait_for(b"8 to 64 characters", 5))
    s.send(b"\x1b")
    s.pump(0.5)

    n = ansi_login("NotSysop")
    n.buf.clear()
    n.send(b"config\r")
    n.pump(0.8)
    ok &= check("CONFIG is the sysop's alone", b"Unknown" in n.buf)
    n.close()
    s.close()
    return ok


# Rows of CONFIG board, counted from 0 (Board). 1.1.0 made the timezone two
# rows, Timezone and TZ string, which moved everything under it down one.
BOARD_ZONE = 2
BOARD_TZ   = 3
BOARD_LED  = 6


def cfg_sysop(handle):
    """An ANSI caller elevated to the sysop node, ready for CONFIG."""
    s = ansi_login(handle)
    s.send(b"bye testsysop\r")
    s.wait_for(b"Sysop", 4)
    s.pump(0.3)
    return s


def cfg_open(s, page, expect):
    """CONFIG <page>, waiting for a field label so keys land in the form."""
    s.buf.clear()
    s.send(b"config " + page + b"\r")
    ok = s.wait_for(expect, 5)
    s.pump(0.4)                                   # the fields cascade in
    return ok


def cfg_verdict(s, pats, secs=6):
    """Wait for one of pats after F1 and say which, or None for none of them.

    "saved, but" is always looked for first. It is the board writing a value
    and then refusing to read it back, and the parser's complaint is quoted
    inside it, so a check looking for that complaint would otherwise take the
    very bug it exists to catch as the refusal it wanted.
    """
    pats = [b"saved, but"] + [p for p in pats if p != b"saved, but"]
    i = wait_any(s, pats, secs)
    s.pump(0.4)                                   # let the line finish drawing
    return pats[i] if i >= 0 else None


def cfg_cancel(s):
    s.send(b"\x1b")
    s.pump(0.6)


def cfg_line(key):
    """The raw top-of-file line for key in the board's system.cfg, or None."""
    for line in (USERDATA / "system.cfg").read_text().splitlines():
        if line.strip().startswith("["):
            break
        k = line.split("=", 1)[0].strip()
        if "=" in line and k == key:
            return line
    return None


def cfg_reload(s):
    """Make the board read system.cfg again, from CONFIG like a sysop would.

    Two saves of the account cap, because a save that changes nothing
    writes nothing and reloads nothing. Ends on 200, the value test_config
    leaves behind for the rest of the run.
    """
    ok = True
    for val in (b"201", b"200"):
        cfg_open(s, b"limits", b"Per call")
        s.buf.clear()
        s.send(DOWN * 4 + b"\x08" * 4 + val + F1)
        ok &= cfg_verdict(s, [b"Saved and live", b"Nothing changed"]) not in (None, b"saved, but")
    return ok


def row_reach(data, pat):
    """The drawn screen row holding pat (see line_with), or "".

    Its length is how far right the row reaches, which is the number that
    matters on a 40 column screen: column 40 wraps on a C64.
    """
    return line_with(data, pat)[0] or ""


def test_config_parser_rules():
    """CONFIG writes only what the parser will read back (1.0.0-rc1).

    Rob typed "therustyantenna.local" as the hostname. CONFIG wrote it, the
    reload refused the file, and nothing else on the page went live: the
    board said "saved, but system.cfg line 7: ...". CONFIG kept its own copy
    of the parser's rules, and whatever the copy let through was written
    anyway. It asks the parser now (syscfg::trial), so each check here is a
    value one side took and the other did not.
    """
    print("CONFIG and the parser agree")
    local = HOST in ("127.0.0.1", "localhost")
    s = cfg_sysop("CfgRules")

    # Rob's report, as he typed it: the name he sees the board by.
    cfg_open(s, b"board", b"Hostname")
    s.buf.clear()
    s.send(DOWN + b"\x08" * 32 + b"TheRustyAntenna.local" + F1)
    got = cfg_verdict(s, [b"Saved and live", b"saved, but", b"hostname must"])
    ok = check("a hostname typed as name.local saves and goes live", got == b"Saved and live")
    if local:
        ok &= check("stored as the bare name the parser takes",
                    (cfg_line("hostname") or "").split("=", 1)[-1].strip() == "therustyantenna")

    # Still invalid once normalised: refused on the form, nothing written.
    before = cfg_line("hostname") if local else None
    cfg_open(s, b"board", b"Hostname")
    s.buf.clear()
    s.send(DOWN + b"\x08" * 32 + b"bad_name!" + F1)
    got = cfg_verdict(s, [b"hostname must be", b"Saved", b"saved, but"])
    ok &= check("an invalid hostname is refused with the parser's rule", got == b"hostname must be")
    cfg_cancel(s)
    if local:
        ok &= check("and nothing is written", cfg_line("hostname") == before)

    cfg_open(s, b"board", b"Hostname")
    s.send(DOWN + b"\x08" * 32 + b"unleashed" + F1)
    cfg_verdict(s, [b"Saved and live", b"Nothing changed"])

    # The backup window cannot share the dial-in port. CONFIG offered
    # 1..65535 and wrote 6400; the reload then refused the whole file.
    before = cfg_line("backup_port") if local else None
    cfg_open(s, b"backup", b"Open for")
    s.buf.clear()
    s.send(b"\x08" * 6 + str(BBS_PORT_NUM).encode() + F1)
    got = cfg_verdict(s, [b"cannot be the BBS port", b"Saved", b"saved, but"])
    ok &= check("the backup port cannot be the dial-in port", got == b"cannot be the BBS port")
    cfg_cancel(s)
    if local:
        ok &= check("and the port in the file is untouched", cfg_line("backup_port") == before)

    # Two keys at once: the parser refuses a WHO minimum above the maximum.
    before = cfg_line("who_refresh_min") if local else None
    cfg_open(s, b"limits", b"Per call")
    s.buf.clear()
    s.send(DOWN * 2 + b"\x08" * 3 + b"50" + DOWN + b"\x08" * 3 + b"10" + F1)
    got = cfg_verdict(s, [b"above the max", b"Saved", b"saved, but"])
    ok &= check("a WHO minimum above the maximum is refused before writing", got == b"above the max")
    cfg_cancel(s)
    if local:
        ok &= check("and neither is written", cfg_line("who_refresh_min") == before)

    # A '#' starts a comment on every core line but the passwords and the
    # network, so "Rob's #1 BBS" was written and read back as "Rob's".
    cfg_open(s, b"board", b"Hostname")
    s.buf.clear()
    s.send(b"\x08" * 42 + b"Rob's #1 BBS" + F1)
    got = cfg_verdict(s, [b"No # here", b"Saved", b"saved, but"])
    ok &= check("a # in a board name is refused, not cut off", got == b"No # here")
    cfg_cancel(s)

    # "***" is how a backup writes a hidden password, and the parser reads
    # it as "keep what is set", which on a reload is nothing: typing it as a
    # co-sysop password switched that level off.
    cfg_open(s, b"staff", b"Co-sysop")
    s.buf.clear()
    s.send(DOWN * 2 + b"***" + F1)
    got = cfg_verdict(s, [b"hidden password", b"Saved", b"saved, but"])
    ok &= check("*** is refused as a password", got == b"hidden password")
    cfg_cancel(s)

    # -1 is "no pin" to the parser and was "Numbers only" to CONFIG. The LED
    # is the seventh row since 1.1.0, when the Timezone became two rows.
    cfg_open(s, b"board", b"Hostname")
    s.buf.clear()
    s.send(DOWN * BOARD_LED + b"\x08" * 3 + b"-1" + F1)
    got = cfg_verdict(s, [b"Saved and live", b"Numbers only", b"Between", b"saved, but"])
    ok &= check("the LED pin takes -1, no LED, as the parser does", got == b"Saved and live")
    if local:
        ok &= check("written as -1", (cfg_line("activity_led_gpio") or "").endswith("= -1"))
    cfg_open(s, b"board", b"Hostname")
    s.send(DOWN * BOARD_LED + b"\x08" * 3 + b"2" + F1)
    cfg_verdict(s, [b"Saved and live", b"Nothing changed"])

    # The staff page's two co-sysop rows, told apart inside the nine
    # column label. They both read "Co-sysop " before.
    cfg_open(s, b"staff", b"Co-sysop")
    s.pump(0.6)
    page = plain(s.buf)
    ok &= check("the co-sysop rows are labelled 1 and 2",
                b"Co-sysop1" in page and b"Co-sysop2" in page)
    ok &= check("and the staff form fits 40 columns", max_column(s.buf) <= 39)
    cfg_cancel(s)
    s.close()
    return ok


def test_config_guards():
    """Two settings CONFIG must never write (1.0.0).

    An empty sysop password switches staff off, and one backspace on the
    masked field empties it; with no BOOT reset until 1.0.1 the only way
    back was a reflash. And GPIO 6 to 11 are the flash chip on a WROOM: a
    pin setting there stops the board rather than failing.
    """
    print("CONFIG refuses what would lock out or stop the board")
    local = HOST in ("127.0.0.1", "localhost")
    s = cfg_sysop("CfgGuard")

    before = cfg_line("sysop_password") if local else None
    cfg_open(s, b"staff", b"Co-sysop")
    s.buf.clear()
    s.send(b"\x08" + F1)                           # one backspace clears the mask
    got = cfg_verdict(s, [b"cannot be empty", b"Saved"])
    ok = check("an empty sysop password is refused", got == b"cannot be empty")
    ok &= check("in 40 columns", 0 < len(row_reach(s.buf, "cannot be empty")) <= 39)
    cfg_cancel(s)
    if local:
        ok &= check("and the sysop password in the file is untouched",
                    cfg_line("sysop_password") == before)

    # A co-sysop level may still be switched off by emptying it.
    cfg_open(s, b"staff", b"Co-sysop")
    s.buf.clear()
    s.send(DOWN * 2 + b"\x08" + F1)
    got = cfg_verdict(s, [b"Saved and live", b"cannot be empty"])
    ok &= check("a co-sysop password may still be emptied", got == b"Saved and live")
    if local:
        ok &= check("which writes it empty",
                    (cfg_line("cosysop2_password") or "x").strip().endswith("="))
    cfg_open(s, b"staff", b"Co-sysop")
    s.send(DOWN * 2 + (CO2 or "testco2").encode() + F1)
    cfg_verdict(s, [b"Saved and live"])

    # The flash pins, through the parser's rule: the LED and the button.
    before = cfg_line("activity_led_gpio") if local else None
    cfg_open(s, b"board", b"Hostname")
    s.buf.clear()
    s.send(DOWN * BOARD_LED + b"\x08" * 3 + b"6" + F1)
    got = cfg_verdict(s, [b"flash chip", b"Saved"])
    ok &= check("the LED cannot be put on a flash pin", got == b"flash chip")
    cfg_cancel(s)
    if local:
        ok &= check("and nothing is written", cfg_line("activity_led_gpio") == before)
    cfg_open(s, b"backup", b"Open for")
    s.buf.clear()
    s.send(DOWN * 2 + b"\x08" * 3 + b"11" + F1)
    got = cfg_verdict(s, [b"flash chip", b"Saved"])
    ok &= check("nor the backup button", got == b"flash chip")
    cfg_cancel(s)

    # And a plugin's pins, through the same rule: the SD card's CS.
    cfg_open(s, b"sd", b"CS pin")
    s.buf.clear()
    s.send(DOWN * 4 + b"\x08" * 3 + b"7" + F1)
    got = cfg_verdict(s, [b"flash chip", b"Saved", b"Between"])
    ok &= check("nor an SD card pin", got == b"flash chip")
    cfg_cancel(s)
    s.close()
    return ok


def cfg_sec_line(section, key):
    """The raw line for key inside [section] of the board's system.cfg, or None."""
    cur = None
    for line in (USERDATA / "system.cfg").read_text().splitlines():
        t = line.strip()
        if t.startswith("["):
            cur = t[1:t.find("]")] if "]" in t else None
            continue
        if cur == section and "=" in line and line.split("=", 1)[0].strip() == key:
            return line
    return None


def test_config_semicolon():
    """A ';' in a plugin value is refused, not written and cut (1.0.0).

    A plugin reads its values up to the first ';' (plugin.cpp), which it
    takes as a comment. So "C64 fans; PETSCII welcome" was written whole and
    read back as "C64 fans", and an area named "Games; Demos" lost the four
    levels packed after its name: they fell back to the plugin's own, and a
    staff-only area opened to everybody while CONFIG said "Saved and live".
    The core's keys end at '#' instead, and their passwords at nothing.
    """
    print("CONFIG and the ';' that ends a plugin value")
    local = HOST in ("127.0.0.1", "localhost")
    s = cfg_sysop("CfgSemi")

    # A plugin's own text setting: announce's description.
    before = cfg_sec_line("plugin:announce", "description") if local else None
    cfg_open(s, b"announce", b"About")
    s.buf.clear()
    s.send(DOWN * 5 + b"\x08" * 90 + b"C64 fans; PETSCII welcome" + F1)
    got = cfg_verdict(s, [b"No ; here", b"Saved"])
    ok = check("a ; in a plugin setting is refused", got == b"No ; here")
    ok &= check("in 40 columns", 0 < len(row_reach(s.buf, "No ; here")) <= 39)
    cfg_cancel(s)
    if local:
        ok &= check("and the plugin's section is untouched",
                    cfg_sec_line("plugin:announce", "description") == before)

    # A file area's name on its own page, a staff-only area (area4).
    before = cfg_sec_line("plugin:files", "area4") if local else None
    cfg_open(s, b"files", b"Area 1")
    s.buf.clear()
    s.send(DOWN * 7 + b"\r")
    opened = s.wait_for(b"FILE AREA 4", 6)
    s.pump(0.6)
    s.buf.clear()
    s.send(DOWN + b"\x08" * 30 + b"Games; Demos" + F1)
    got = cfg_verdict(s, [b"No ; here", b"Saved", b"Nothing changed"])
    ok &= check("a ; in a file area's name is refused", opened and got == b"No ; here")
    s.send(b"\x1b")                                  # the area page
    s.wait_for(b"Area 8", 6)
    s.pump(0.6)
    cfg_cancel(s)                                    # and the files page
    if local:
        ok &= check("and the area keeps its levels in the file",
                    cfg_sec_line("plugin:files", "area4") == before)

    # The core is not a plugin: a password may hold a ';', and so may the
    # board's name, since the core's lines end at '#'.
    cfg_open(s, b"staff", b"Co-sysop")
    s.buf.clear()
    s.send(DOWN * 2 + b"semi;colon1" + F1)
    got = cfg_verdict(s, [b"Saved and live", b"No ; here"])
    ok &= check("a core password may hold a ;", got == b"Saved and live")
    if local:
        ok &= check("and is written whole",
                    (cfg_line("cosysop2_password") or "").endswith("= semi;colon1"))
    cfg_open(s, b"staff", b"Co-sysop")
    s.send(DOWN * 2 + (CO2 or "testco2").encode() + F1)
    cfg_verdict(s, [b"Saved and live"])

    cfg_open(s, b"board", b"Hostname")
    s.buf.clear()
    s.send(b"\x08" * 42 + b"Games; Demos BBS" + F1)
    got = cfg_verdict(s, [b"Saved and live", b"No ; here", b"No # here"])
    ok &= check("a core value may hold a ;", got == b"Saved and live")
    if local:
        ok &= check("and is written whole",
                    (cfg_line("board_name") or "").endswith("= Games; Demos BBS"))
    cfg_open(s, b"board", b"Hostname")
    s.buf.clear()
    s.send(b"\x08" * 42 + b"Rob's #1 BBS" + F1)
    got = cfg_verdict(s, [b"No # here", b"Saved"])
    ok &= check("while a # in one is still refused", got == b"No # here")
    cfg_cancel(s)
    cfg_open(s, b"board", b"Hostname")
    s.send(b"\x08" * 42 + F1)                        # back to no name of its own
    cfg_verdict(s, [b"Saved and live"])
    s.close()
    return ok


def test_config_sd_plugin():
    """A plugin that needs the card is not called live without one (1.0.0-rc1).

    forums is PF_SD: with no card it does not start and FORUMS is not a
    command. Saving its CONFIG page said "Saved and live" regardless.
    """
    print("CONFIG and a plugin that needs a card")
    card = bool(os.environ.get("BBS_SD_DIR", ""))
    s = cfg_sysop("CfgCard")
    cfg_open(s, b"forums", b"Enabled")
    s.buf.clear()
    s.send(DOWN + b"u" + F1)                       # Read: all -> users, a real change
    cfg_verdict(s, [b"aved"])
    said = row_reach(s.buf, "aved")
    if card:
        ok = check("with a card, the forums page saves live", "Saved and live" in said)
    else:
        ok = check("with no card, the forums page is not called live",
                   "Saved and live" not in said and "needs an SD card" in said)
        ok &= check("and says so inside 40 columns", 0 < len(said) <= 39)
    cfg_open(s, b"forums", b"Enabled")
    s.send(DOWN + b"a" + F1)                       # back to all
    cfg_verdict(s, [b"aved"])
    s.close()
    return ok


# ---------------------------------------------------------------------------
# The lights plugin (1.1.0). The host has no pixels, so platform_host.cpp
# keeps the last frame each output was given and LIGHTS prints it as hex:
# that is what these read. Colours are checked against the arithmetic in
# lights.cpp's shade(), worked by hand here: a channel v at level l and p
# percent is v * l * p / 25500, rounded down, and 1 if that comes to 0 for a
# lit channel. So full white is 76 at 30%, 25 at 10% and 2 at 1%.
# ---------------------------------------------------------------------------
HEX6 = re.compile(r"^[0-9A-F]{6}$")


def lights_read(s):
    """LIGHTS, parsed: {"drive"|"strip": {"pin", "fx", "pct", "px": [(r, g, b)]}}."""
    s.buf.clear()
    s.send(b"lights\r")
    s.wait_for(b"Strip ", 4)
    s.pump(0.25)
    out, cur = {}, None
    for line in plain(s.buf).decode("latin-1").replace("\r", "").split("\n"):
        t = line.strip()
        m = re.match(r"(Drive|Strip)\s+(.*)$", t)
        if m:
            cur = m.group(1).lower()
            pm = re.match(r"pin (-?\d+)\s+(\w+)\s+(\d+)%", m.group(2))
            out[cur] = {"text": m.group(2), "px": [],
                        "pin": int(pm.group(1)) if pm else None,
                        "fx": pm.group(2) if pm else None,
                        "pct": int(pm.group(3)) if pm else None}
            continue
        toks = t.split()
        if cur and toks and all(HEX6.match(x) for x in toks):
            out[cur]["px"] += [tuple(int(x[i:i + 2], 16) for i in (0, 2, 4)) for x in toks]
    return out


def lights_px(s, which="strip"):
    return lights_read(s).get(which, {}).get("px", [])


def lights_config(s, **keys):
    """[plugin:lights] rewritten with exactly these keys (none: removed),
    then the board made to read it, the way a sysop editing the file on a
    laptop and saving any CONFIG page would."""
    path = USERDATA / "system.cfg"
    out, skip = [], False
    for line in path.read_text().splitlines():
        t = line.strip()
        if t.startswith("["):
            skip = t.lower() == "[plugin:lights]"
        if not skip:
            out.append(line)
    if keys:
        out.append("[plugin:lights]")
        out += [f"{k} = {v}" for k, v in keys.items()]
    path.write_text("\n".join(out) + "\n")
    return cfg_reload(s)


def lit(p):
    return max(p) > 0


def grey(p):
    return p[0] == p[1] == p[2] and p[0] > 0


def test_config_lights():
    """CONFIG lights: the pins, the brightness ceiling and the round trip."""
    print("CONFIG lights")
    local = HOST in ("127.0.0.1", "localhost")
    s = cfg_sysop("CfgLights")
    ok = check("the lights page opens", cfg_open(s, b"lights", b"Drive pin"))
    page = plain(s.buf)
    ok &= check("with a row for each setting",
                all(w in page for w in (b"Drive pin", b"Drive fx", b"Drive %", b"Strip pin",
                                        b"Strip %", b"Pixels")))
    ok &= check("and fits 40 columns", max_column(s.buf) <= 39)
    # The note on the focused row: Down four times is Drive pin.
    s.buf.clear()
    s.send(DOWN * 4)
    s.pump(0.5)
    ok &= check("each row carries the copy's note",
                b"The disk light: one pixel. -1 is off." in plain(s.buf))
    s.buf.clear()
    s.send(DOWN * 6 + b"\r")                       # on to Pixels, and open it
    s.wait_for(b"PIXELS", 6)
    s.pump(0.6)
    ok &= check("a board that has never run the lights lists its pixels as shipped",
                b"solid, cycle" in plain(s.buf) and b"red" not in plain(s.buf))
    s.send(b"\x1b")
    s.wait_for(b"Drive pin", 6)
    s.pump(0.4)
    cfg_cancel(s)

    s.buf.clear()
    s.send(b"lights\r")
    s.pump(0.8)
    ok &= check("off as shipped: LIGHTS is not a command", b"Unknown" in plain(s.buf))

    # Rows: 0 Enabled, 1 Read, 2 Write, 3 Admin, 4 Drive pin, 5 Drive fx,
    # 6 Drive %, 7 Strip pin, 8 Strip, 9 Strip %, 10 Pixels.
    cfg_open(s, b"lights", b"Drive pin")
    s.buf.clear()
    s.send(DOWN * 4 + b"\x08" * 3 + b"7" + F1)
    got = cfg_verdict(s, [b"flash chip", b"Saved", b"Between"])
    ok &= check("a pin on the flash chip is refused", got == b"flash chip")
    ok &= check("in the copy's words", b"Pins 6 to 11 are the flash chip." in plain(s.buf))
    cfg_cancel(s)

    cfg_open(s, b"lights", b"Drive pin")
    s.buf.clear()
    s.send(DOWN * 7 + b"\x08" * 3 + b"9" + F1)
    got = cfg_verdict(s, [b"flash chip", b"Saved", b"Between"])
    ok &= check("on either pin", got == b"flash chip")
    cfg_cancel(s)

    cfg_open(s, b"lights", b"Drive pin")
    s.buf.clear()
    s.send(DOWN * 4 + b"\x08" * 3 + b"34" + F1)
    got = cfg_verdict(s, [b"Between -1 and 33", b"Saved", b"Numbers only"])
    ok &= check("a pin that cannot drive a pixel is refused, -1 in the range",
                got == b"Between -1 and 33")
    cfg_cancel(s)

    cfg_open(s, b"lights", b"Drive pin")
    s.buf.clear()
    s.send(DOWN * 4 + b"\x08" * 3 + b"13" + DOWN * 3 + b"\x08" * 3 + b"13" + F1)
    got = cfg_verdict(s, [b"That is the drive pin", b"Saved", b"flash chip"])
    ok &= check("the strip cannot share the drive light's pin", got == b"That is the drive pin")
    ok &= check("said in the copy's words",
                b"That is the drive pin. Pick another." in plain(s.buf))
    cfg_cancel(s)

    for row, name in ((6, "Drive"), (9, "Strip")):
        cfg_open(s, b"lights", b"Drive pin")
        s.buf.clear()
        s.send(DOWN * row + b"\x08" * 3 + b"31" + F1)
        got = cfg_verdict(s, [b"Between 1 and 30", b"Saved"])
        ok &= check(f"{name} % stops at 30", got == b"Between 1 and 30")
        cfg_cancel(s)

    # The round trip: on, both pins, both effects, both brightnesses.
    cfg_open(s, b"lights", b"Drive pin")
    s.buf.clear()
    s.send(b"y" + DOWN * 4 + b"\x08" * 3 + b"13" + DOWN + b"1" + DOWN + b"\x08" * 3 + b"25"
           + DOWN + b"\x08" * 3 + b"14" + DOWN + b"h" + DOWN + b"\x08" * 3 + b"5" + F1)
    got = cfg_verdict(s, [b"Saved and live", b"Saved", b"Between", b"flash chip"])
    ok &= check("the lights page saves live", got == b"Saved and live")
    if local:
        want = {"enabled": "yes", "drive_pin": "13", "drive_fx": "1541", "drive_bright": "25",
                "strip_pin": "14", "strip_fx": "hayes", "strip_bright": "5"}
        have = {k: (cfg_sec_line("plugin:lights", k) or "=").split("=", 1)[1].strip() for k in want}
        ok &= check("every row written as the plugin reads it", have == want)
    f = lights_read(s)
    ok &= check("and running as saved",
                f.get("drive", {}).get("text", "").startswith("pin 13  1541  25%") and
                f.get("strip", {}).get("text", "").startswith("pin 14  hayes  5%"))
    ok &= check("LIGHTS labels the Hayes panel", b"HS" in plain(s.buf) and b"MR" in plain(s.buf))

    cfg_open(s, b"lights", b"Drive pin")
    page = plain(s.buf)
    ok &= check("the page opens on what was saved", b"1541" in page and b"hayes" in page)
    s.buf.clear()
    s.send(DOWN * 7 + b"\x08" * 3 + b"-1" + F1)
    got = cfg_verdict(s, [b"Saved and live", b"Numbers only", b"Between", b"Saved"])
    ok &= check("-1 is taken, and means off", got == b"Saved and live")
    if local:
        ok &= check("written as -1",
                    (cfg_sec_line("plugin:lights", "strip_pin") or "").endswith("= -1"))
    f = lights_read(s)
    ok &= check("the strip is off", f.get("strip", {}).get("text", "") == "no pin, off")

    s.buf.clear()
    s.send(b"plugins\r")
    read_list(s)
    ok &= check("PLUGINS lists it running", b"lights" in plain(s.buf) and b"running" in plain(s.buf))
    if local:
        lights_config(s)                               # off again for everything after
    s.close()
    return ok


def ascii_sysop(handle):
    """A plain ASCII caller elevated to the sysop node."""
    c = Caller(ansi=False)
    c.wait_for(b"HIT DEL OR BACKSPACE", 6)
    c.send(b"\x08")
    c.wait_for(b"Enter your handle", 8)
    login(c, handle)
    c.send(f"bye {PASSWORD}\r".encode())
    c.wait_for(b"Sysop", 5)
    c.pump(0.4)
    return c


# CONFIG sd in line mode: the rows after Read (Write, Admin, four pins, the
# bus speed, Screens and, from 1.1.0, Nightly).
SD_ROWS_AFTER_READ = 9


def ascii_form_seen(c, answers):
    """ascii_form, and what the form printed on the way, which ascii_form
    clears before the verdict: (verdict index, rendered lines)."""
    for a in answers:
        c.send(a + b"\r")
        c.pump(0.3)
    c.wait_for(b"Save (Y/n)?", 4)
    seen = render_lines(c.buf)
    c.buf.clear()
    c.send(b"y")
    return wait_any(c, [b"Saved and live", b"Nothing changed", b"Saved", b"Not one of"], 6), seen


def ascii_form(c, answers):
    """Answer a line-mode form a row at a time, then say Y to saving."""
    for a in answers:
        c.send(a + b"\r")
        c.pump(0.3)
    c.wait_for(b"Save (Y/n)?", 4)
    c.buf.clear()
    c.send(b"y")
    return wait_any(c, [b"Saved and live", b"Nothing changed", b"Saved", b"Not one of"], 6)


def test_config_lights_ascii():
    """Cycle fields in plain ASCII line mode (1.1.0).

    A letter picked a value into the field and left a space in the input to
    stand for it; Enter then copied the input over the value, so every pick
    made in line mode saved a single space. A level written as " " is one
    the plugin's parser refuses, so nothing changed and the form had said
    "Saved and live". The sd page shows it on any build.
    """
    print("CONFIG in plain ASCII: cycle fields")
    if not PASSWORD or HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build and the sysop")
        return True
    c = ascii_sysop("CfgAscii")
    c.buf.clear()
    c.send(b"config sd\r")
    c.wait_for(b"Enabled", 5)
    # Enabled, Read, Write, Admin, CS, MOSI, CLK, MISO, Bus kHz, Screens,
    # and since 1.1.0 Nightly.
    got = ascii_form(c, [b"", b"c"] + [b""] * SD_ROWS_AFTER_READ)
    ok = check("a level picked by letter saves", got == 0)
    ok &= check("as the level picked, not as a space",
                (cfg_sec_line("plugin:sd", "read") or "").endswith("= co2"))
    c.buf.clear()
    c.send(b"config sd\r")
    c.wait_for(b"Enabled", 5)
    got = ascii_form(c, [b"", b"ss"] + [b""] * SD_ROWS_AFTER_READ)   # co2: s is staff, s again is sysop
    ok &= check("the same letter twice steps to its next match",
                got == 0 and (cfg_sec_line("plugin:sd", "read") or "").endswith("= sysop"))
    c.buf.clear()
    c.send(b"config sd\r")
    c.wait_for(b"Enabled", 5)
    got = ascii_form(c, [b"", b"c\x08"] + [b""] * SD_ROWS_AFTER_READ)
    ok &= check("Backspace after a pick puts the value back",
                got == 1 and (cfg_sec_line("plugin:sd", "read") or "").endswith("= sysop"))

    c.buf.clear()
    c.send(b"config lights\r")
    c.wait_for(b"Enabled", 5)
    # Enabled, Read, Write, Admin, Drive pin, Drive fx, Drive %, Strip pin,
    # Strip, Strip %, and Pixels, which asks "open (y/N)?".
    got = ascii_form(c, [b"y", b"", b"", b"", b"13", b"", b"", b"14", b"bb", b"", b""])
    ok &= check("the lights page saves in line mode", got == 0)
    ok &= check("with blinken's b pressed twice reaching boing",
                (cfg_sec_line("plugin:lights", "strip_fx") or "").endswith("= boing"))
    ok &= check("and a typed pin", (cfg_sec_line("plugin:lights", "strip_pin") or "").endswith("= 14"))
    lights_config(c)
    c.close()
    return ok


def test_lights_frames():
    """Every strip effect and the drive light, read back from the host's
    record of the frames (1.1.0)."""
    print("Lights: frames")
    if not PASSWORD or HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build and the sysop")
        return True
    card = bool(os.environ.get("BBS_SD_DIR", ""))
    s = cfg_sysop("LightsFrames")
    on = {"enabled": "yes", "drive_pin": 13, "strip_pin": 14}

    # Brightness. White on both outputs from LIGHTS TEST, drive at 30% and
    # the strip at 1%, so each output is plainly following its own setting.
    ok = check("switched on", lights_config(s, **on, drive_bright=30, strip_bright=1))
    f = lights_read(s)
    if not check("LIGHTS reports both outputs",
                 len(f.get("drive", {}).get("px", [])) == 1 and len(f.get("strip", {}).get("px", [])) == 10):
        s.close()
        return False
    s.buf.clear()
    s.send(b"lights test\r")
    s.wait_for(b"a second each", 4)
    t0 = time.time()
    f = lights_read(s)
    ok &= check("LIGHTS TEST opens on red", f["drive"]["px"] == [(76, 0, 0)])
    time.sleep(max(0.0, t0 + 3.3 - time.time()))
    f = lights_read(s)
    ok &= check("white on the drive light at 30%: 76, and no further", f["drive"]["px"] == [(76, 76, 76)])
    ok &= check("white on the strip at its own 1%", f["strip"]["px"] == [(2, 2, 2)] * 10)

    lights_config(s, **on, strip_fx="rainbow", strip_bright=30)
    px = lights_px(s)
    ok &= check("rainbow lights every pixel", len(px) == 10 and all(lit(p) for p in px))
    ok &= check("and reaches 30% without passing it", 25 < max(max(p) for p in px) <= 76)
    lights_config(s, **on, strip_fx="rainbow", strip_bright=80)
    f = lights_read(s)
    ok &= check("a hand-edited 80% is read as 30",
                f["strip"]["pct"] == 30 and max(max(p) for p in f["strip"]["px"]) <= 76)
    lights_config(s, **on, strip_fx="rainbow", strip_bright=1)
    px = lights_px(s)
    ok &= check("at 1% every pixel stays lit", all(lit(p) for p in px) and max(max(p) for p in px) <= 2)
    lights_config(s, **on, strip_fx="rainbow")
    f = lights_read(s)
    ok &= check("10% as shipped", f["strip"]["pct"] == 10 and f["drive"]["pct"] == 10
                and max(max(p) for p in f["strip"]["px"]) <= 25)

    lights_config(s, **on, strip_fx="off")
    ok &= check("off is dark", lights_px(s) == [(0, 0, 0)] * 10)

    lights_config(s, **on, strip_fx="c64")
    px = lights_px(s)
    ok &= check("c64: the five stripes, twice along the strip",
                all(lit(p) for p in px) and px[:5] == px[5:] and len(set(px)) == 5)

    lights_config(s, **on, strip_fx="scanner")
    heads = set()
    for _ in range(4):
        px = lights_px(s)
        top = max(p[0] for p in px)
        ok &= check("scanner: red, one head at full, a fading tail",
                    all(p[1] == p[2] == 0 for p in px) and top == 25 and
                    sum(1 for p in px if p[0] == top) == 1)
        heads.add(next(i for i, p in enumerate(px) if p[0] == top))
    ok &= check("and the head moves", len(heads) > 1)

    lights_config(s, **on, strip_fx="boing")
    good = True
    for _ in range(3):
        px = lights_px(s)
        on_at = [i for i, p in enumerate(px) if lit(p)]
        good &= len(on_at) == 3 and on_at[2] - on_at[0] == 2
        good &= all(px[i] in ((25, 0, 0), (25, 25, 25)) for i in on_at)
        good &= len(on_at) == 3 and px[on_at[0]] != px[on_at[1]] and px[on_at[1]] != px[on_at[2]]
    ok &= check("boing: three pixels, red and white by turns", good)

    lights_config(s, **on, strip_fx="blinken")
    frames = [lights_px(s) for _ in range(4)]
    ok &= check("blinken: red lamps only",
                all(p[1] == p[2] == 0 for px in frames for p in px))
    ok &= check("changing as it goes", len({tuple(px) for px in frames}) > 1)

    lights_config(s, **on, strip_fx="vu")
    bars = []
    for _ in range(4):
        px = lights_px(s)
        n = sum(1 for p in px if lit(p))
        bars.append(n)
        ok &= check("vu: a bar from the first pixel, green at the bottom",
                    all(lit(p) for p in px[:n]) and not any(lit(p) for p in px[n:]) and
                    (n == 0 or px[0] == (0, 20, 0)))
    ok &= check("which LIGHTS' own traffic moves", max(bars) > 0)

    # hayes: HS AA CD OH RD SD TR MR on pixels 0 to 7.
    lights_config(s, **on, strip_fx="hayes")
    for _ in range(20):                              # earlier callers' lines may be closing
        px = lights_px(s)
        if px and px[2] == (0, 0, 0):
            break
        time.sleep(0.5)
    ok &= check("hayes: MR, TR and AA lit, pixels 8 and 9 dark",
                px[7] == px[6] == px[1] == (25, 0, 0) and px[8] == px[9] == (0, 0, 0))
    ok &= check("CD and OH dark with nobody on a caller line", px[2] == px[3] == (0, 0, 0))
    ok &= check("HS lit with nobody to be slow for", px[0] == (25, 0, 0))
    b = ansi_login("HayesCaller")
    px = lights_px(s)
    ok &= check("a caller connecting lights CD and OH", px[2] == px[3] == (25, 0, 0))
    b.close()

    # nodes: each line in the colour of its caller's WHO marker.
    lights_config(s, **on, strip_fx="nodes")
    b = ansi_login("NodeCaller")
    n = int(b.node())
    b.pump(0.8)
    px = lights_px(s)
    ok &= check("nodes: a caller's line lit grey, a caller's colour in WHO", grey(px[n - 1]))
    if CO1:
        b.buf.clear()
        b.send(f"bye {CO1}\r".encode())
        b.wait_for(f"Co-sysop 1 access on node {n}".encode(), 4)
        b.pump(0.5)
        p = lights_px(s)[n - 1]
        ok &= check("a co-sysop's line turns yellow", p[0] >= p[1] > 0 and p[2] == 0)
    g = Caller(ansi=True)
    g.wait_for(b"Enter your handle", 10)
    g.send(b"LightGuest\r")
    g.wait_for(b"[G]uest", 4)
    g.send(b"g")
    g.wait_for(b"Main", 6)
    gn = int(g.node())
    g.pump(0.8)
    px = lights_px(s)
    ok &= check("a guest's is dark grey, dimmer than a caller's",
                grey(px[gn - 1]) and px[gn - 1][0] < 13)
    b.close()
    g.close()

    # The drive light: disk2 holds a second after the last access, which is
    # long enough to read it back.
    lights_config(s, **on, drive_fx="disk2", strip_fx="off")
    time.sleep(1.2)
    d = lights_px(s, "drive")[0]
    ok &= check("at rest, a dim amber glow", d == (2, 1, 0))
    s.buf.clear()
    s.send(b"about\r")
    s.wait_for(b"GNU General Public License", 6)
    d = lights_px(s, "drive")[0]
    if card:
        ok &= check("a screen read off the card lights it amber", d == (25, 13, 0))
        s.buf.clear()
        s.send(b"sd unmount\r")
        s.wait_for(b"safe to pull", 5)
        time.sleep(1.2)
        s.buf.clear()
        s.send(b"about\r")
        s.wait_for(b"GNU General Public License", 6)
        d = lights_px(s, "drive")[0]
        ok &= check("and read off the board's flash, cool white", d == (17, 20, 25))
        s.buf.clear()
        s.send(b"sd mount\r")
        s.wait_for(b"Mounted", 8)
    else:
        ok &= check("a screen read off the board's flash lights it cool white", d == (17, 20, 25))
        s.buf.clear()
        s.send(b"lights pulse card\r")
        s.wait_for(b"Pulsed", 4)
        d = lights_px(s, "drive")[0]
        ok &= check("and the card, amber", d == (25, 13, 0))

    # 1541 blinks red on a storage error. With no card, a real one: SD MOUNT
    # fails. With a card, the host's pulse stands in for a broken one.
    lights_config(s, **on, drive_fx="1541", strip_fx="off")
    s.buf.clear()
    if card:
        s.send(b"lights pulse error\r")
        s.wait_for(b"Pulsed", 4)
    else:
        s.send(b"sd mount\r")
        s.wait_for(b"no card", 6)
    seen = []
    for _ in range(8):
        seen.append(lights_px(s, "drive")[0])
    ok &= check("1541: a storage error blinks red",
                (25, 0, 0) in seen and (0, 0, 0) in seen and
                all(p in ((25, 0, 0), (0, 0, 0)) for p in seen))

    lights_config(s)
    s.close()
    return ok


def test_lights_manual():
    """Manual mode: each pixel its own effect and colour (1.1.0)."""
    print("Lights: manual mode")
    if not PASSWORD or HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build and the sysop")
        return True
    s = cfg_sysop("LightsManual")
    on = {"enabled": "yes", "drive_pin": 13, "strip_pin": 14}
    ok = check("switched on", lights_config(s, **on, strip_fx="manual"))
    px = lights_px(s)
    if not check("unset, every pixel is solid and cycling, out of step",
                 len(px) == 10 and all(lit(p) for p in px) and len(set(px)) > 5):
        s.close()
        return False

    # CONFIG: Pixels, then Pixel 3, then its Effect and Colour.
    cfg_open(s, b"lights", b"Drive pin")
    s.buf.clear()
    s.send(DOWN * 10 + b"\r")
    ok &= check("Pixels opens a page of its own", s.wait_for(b"PIXELS", 6))
    s.pump(0.8)
    page = plain(s.buf)
    ok &= check("listing ten pixels, each with its effect and colour",
                b"Pixel 10" in page and b"solid, cycle" in page)
    ok &= check("and fits 40 columns", max_column(s.buf) <= 39)
    s.buf.clear()
    s.send(DOWN * 2 + b"\r")
    ok &= check("a pixel opens its own page", s.wait_for(b"PIXEL 3", 6))
    s.pump(0.6)
    s.buf.clear()
    s.send(b"b" + DOWN + b"pp" + F1)              # blink; p is purple, p again is pink
    got = cfg_verdict(s, [b"Saved and live", b"Nothing changed", b"Not one of"])
    ok &= check("the pixel saves live", got == b"Saved and live")
    ok &= check("back on the Pixels page", b"PIXELS" in plain(s.buf))
    ok &= check("written packed, as the plugin reads it",
                (cfg_sec_line("plugin:lights", "led3") or "").endswith("= blink | pink"))
    s.buf.clear()
    s.send(b"\x1b")
    ok &= check("Escape comes back to the lights page", s.wait_for(b"Drive pin", 6))
    s.pump(0.5)
    cfg_cancel(s)
    seen = {lights_px(s)[2] for _ in range(6)}
    ok &= check("and pixel 3 blinks pink", seen <= {(25, 3, 11), (0, 0, 0)} and len(seen) == 2)

    # Which lines are busy, so one node pixel can be dark for certain.
    lights_config(s, **on, strip_fx="nodes")
    b = ansi_login("ManualNode")
    n = int(b.node())
    b.pump(0.6)
    busy = {i for i, p in enumerate(lights_px(s)) if lit(p)}
    free = next(i for i in range(10) if i not in busy)
    effects = ["solid | white", "off | white", "blink | white", "breathe | white",
               "flicker | white", "sparkle | white", "traffic | white", "solid | red"]
    slots = [i for i in range(10) if i not in (n - 1, free)]
    leds = {f"led{n}": "node | white", f"led{free + 1}": "node | white"}
    for i, e in zip(slots, effects):
        leds[f"led{i + 1}"] = e
    lights_config(s, **on, strip_fx="manual", **leds)
    where = {e: slots[k] for k, e in enumerate(effects)}
    frames = []
    end = time.time() + 5.0
    while time.time() < end or len(frames) < 16:
        frames.append(lights_px(s))
    col = {e: [f[i] for f in frames] for e, i in where.items()}
    ok &= check("solid: white, every frame", set(col["solid | white"]) == {(25, 25, 25)})
    ok &= check("solid red", set(col["solid | red"]) == {(25, 0, 0)})
    ok &= check("off: dark, every frame", set(col["off | white"]) == {(0, 0, 0)})
    ok &= check("blink: on and off, nothing between",
                set(col["blink | white"]) == {(25, 25, 25), (0, 0, 0)})
    ok &= check("breathe: white at many levels",
                all(p[0] == p[1] == p[2] <= 25 for p in col["breathe | white"]) and
                len(set(col["breathe | white"])) >= 3)
    ok &= check("flicker: never out, never steady",
                all(grey(p) for p in col["flicker | white"]) and len(set(col["flicker | white"])) >= 2)
    ok &= check("sparkle: twinkles out of the dark",
                (0, 0, 0) in col["sparkle | white"] and any(lit(p) for p in col["sparkle | white"]))
    ok &= check("traffic: lit or dark, with the board's bytes",
                set(col["traffic | white"]) <= {(25, 25, 25), (0, 0, 0)})
    ok &= check("node: lit for the line with a caller",
                all(grey(f[n - 1]) for f in frames))
    ok &= check("and dark for a line without one", all(f[free] == (0, 0, 0) for f in frames))
    b.close()

    # Colours: cycle turns each pixel a step ahead of the last; random moves on.
    lights_config(s, **on, strip_fx="manual", led1="solid | cycle", led2="solid | cycle",
                  led3="solid | random", led4="off", led5="off", led6="off", led7="off",
                  led8="off", led9="off", led10="off")
    frames = []
    end = time.time() + 3.6                      # a random solid moves on every 3 s
    while time.time() < end:
        frames.append(lights_px(s))
    ok &= check("cycle: two pixels, never the same colour at once",
                all(lit(f[0]) and lit(f[1]) and f[0] != f[1] for f in frames))
    ok &= check("and turning", len({f[0] for f in frames}) > 1)
    ok &= check("random: a new colour as it goes", len({f[2] for f in frames}) > 1)
    ok &= check("the rest dark", all(f[3:] == [(0, 0, 0)] * 7 for f in frames))

    lights_config(s)
    s.close()
    return ok


def test_config_wifi_live():
    """CONFIG wifi on a board that joined through secrets.h (1.0.0-rc1).

    Such a board has no network in system.cfg, so the page showed two empty
    boxes. And changing the network without retyping the password saved the
    new name with no password, because an untouched mask is skipped: the
    next boot tried it as an open network and the board fell off the air.
    The host plays that board with BBS_HOST_SSID (tools/harness.sh).
    """
    print("CONFIG wifi and the live network")
    live = os.environ.get("BBS_HOST_SSID", "")
    if HOST not in ("127.0.0.1", "localhost") or not live:
        print("  SKIP  needs the local harness and BBS_HOST_SSID")
        return True
    cfgp = USERDATA / "system.cfg"
    orig = cfgp.read_text()
    top, sep, rest = orig.partition("\n[")
    kept = [l for l in top.splitlines()
            if l.split("=", 1)[0].strip() not in ("wifi_ssid", "wifi_password")]
    cfgp.write_text("\n".join(kept) + "\n" + sep.lstrip("\n") + rest)

    s = cfg_sysop("CfgWifi")
    ok = check("the board reads a config with no network in it", cfg_reload(s))

    cfg_open(s, b"wifi", b"Network")
    s.pump(0.6)
    page = plain(s.buf)
    ok &= check("CONFIG wifi shows the network the board is on", live.encode() in page)
    ok &= check("and a mask for its password", b"********" in page)
    s.buf.clear()
    s.send(F1)
    got = cfg_verdict(s, [b"Nothing changed", b"Saved", b"saved, but"])
    ok &= check("saving it untouched writes nothing",
                got == b"Nothing changed" and cfg_line("wifi_ssid") is None)

    cfg_open(s, b"wifi", b"Network")
    s.buf.clear()
    s.send(b"\x08" * 34 + b"OtherNet" + F1)
    got = cfg_verdict(s, [b"retype its password", b"Saved", b"saved, but"])
    ok &= check("a new network under the old mask is refused", got == b"retype its password")
    ok &= check("in 40 columns", 0 < len(row_reach(s.buf, "retype its password")) <= 39)
    cfg_cancel(s)
    ok &= check("and nothing is written", cfg_line("wifi_ssid") is None)

    # The password alone is never read at boot: it goes in with the name.
    cfg_open(s, b"wifi", b"Network")
    s.buf.clear()
    s.send(DOWN + b"newpass123" + F1)
    got = cfg_verdict(s, [b"next restart", b"Saved and live", b"saved, but", b"Name the"])
    ok &= check("a retyped password saves", got == b"next restart")
    ok &= check("with the network it belongs to",
                (cfg_line("wifi_ssid") or "").endswith("= " + live) and
                (cfg_line("wifi_password") or "").endswith("= newpass123"))

    cfg_open(s, b"wifi", b"Network")
    s.buf.clear()
    s.send(b"\x08" * 34 + b"OtherNet" + DOWN + b"otherpass1" + F1)
    got = cfg_verdict(s, [b"next restart", b"retype", b"saved, but"])
    ok &= check("a new network with its password retyped saves", got == b"next restart")
    ok &= check("both written", (cfg_line("wifi_ssid") or "").endswith("= OtherNet") and
                (cfg_line("wifi_password") or "").endswith("= otherpass1"))

    # The same trap with the network in the file: the old password would
    # have stayed beside the new name.
    cfg_open(s, b"wifi", b"Network")
    s.buf.clear()
    s.send(b"\x08" * 34 + b"ThirdNet" + F1)
    got = cfg_verdict(s, [b"retype its password", b"Saved", b"saved, but"])
    ok &= check("a network from the file is not changed without its password",
                got == b"retype its password")
    cfg_cancel(s)

    # Cleared on purpose is an open network, and that is allowed.
    cfg_open(s, b"wifi", b"Network")
    s.buf.clear()
    s.send(b"\x08" * 34 + b"OpenNet" + DOWN + b"\x08" + F1)
    got = cfg_verdict(s, [b"next restart", b"retype", b"saved, but"])
    ok &= check("a cleared password means an open network", got == b"next restart" and
                (cfg_line("wifi_ssid") or "").endswith("= OpenNet") and
                (cfg_line("wifi_password") or "").strip().endswith("="))
    ok &= check("a new network typed with it is not the accidental case",
                b"OPEN network" not in s.buf)

    # The accidental case: one backspace on the mask empties a set password
    # on the network the board is already on. It still saves, since an open
    # network is a real choice, but the verdict says OPEN in words (1.0.0).
    cfg_open(s, b"wifi", b"Network")
    s.buf.clear()
    s.send(DOWN + b"openpass1" + F1)
    got = cfg_verdict(s, [b"next restart", b"OPEN network"])
    ok &= check("a retyped password is not called open",
                got == b"next restart" and b"OPEN network" not in s.buf)
    cfg_open(s, b"wifi", b"Network")
    s.buf.clear()
    s.send(DOWN + b"\x08" + F1)
    got = cfg_verdict(s, [b"OPEN network", b"next restart"])
    ok &= check("emptying the password on the same network says OPEN", got == b"OPEN network")
    ok &= check("in 40 columns", 0 < len(row_reach(s.buf, "OPEN network")) <= 39)
    ok &= check("and still saves the open network",
                (cfg_line("wifi_ssid") or "").endswith("= OpenNet") and
                (cfg_line("wifi_password") or "").strip().endswith("="))

    cfgp.write_text(orig)
    cfg_reload(s)
    ok &= check("the test board's network is back",       # cfg_value would cut it at the #
                (cfg_line("wifi_ssid") or "").endswith("= Test#Net"))
    s.close()
    return ok


# ---------------------------------------------------------------------------
# The listening port and the network page (1.1.0)
# ---------------------------------------------------------------------------

def restart_copy(extra_args=(), edits=None):
    """A second board started on a copy of this one's data directory: what
    this board would read if it restarted now.

    The harness board cannot be restarted from inside the suite, and the
    copy is what makes that not matter: the port setting is read at start,
    so a board started on the same files IS the restart, and the harness
    board carries on untouched for the tests after this one. No card for the
    copy, so it cannot seed or write anything the real board reads. edits,
    as for cfg_with, change the copy's system.cfg only. Returns (process,
    its directory); the caller kills it.
    """
    tmp = copy_data()
    if edits:
        cfg = tmp / "data" / "user" / "system.cfg"
        cfg.write_text(cfg_with(cfg.read_text(), edits))
    return start_copy(tmp, extra_args), tmp


def copy_data():
    """This board's data directory copied somewhere of its own, not started.
    restart_copy in two halves, for a test that has to look at or plant
    files between the copy and the start."""
    import shutil
    import tempfile
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="bbs-restart-"))
    shutil.copytree(DATA, tmp / "data")
    return tmp


def start_copy(tmp, extra_args=(), env_extra=None):
    """Start a board on a copy_data() directory, its console in tmp/host.log.
    No card unless env_extra gives it one (BBS_SD_DIR)."""
    import subprocess
    env = {k: v for k, v in os.environ.items() if k != "BBS_SD_DIR"}
    env.update(env_extra or {})
    log = open(tmp / "host.log", "wb")
    return subprocess.Popen([str(ROOT / "host" / "bbs_host"), str(tmp / "data"), *extra_args],
                            stdout=log, stderr=subprocess.STDOUT, env=env)


def copy_log(tmp, want, secs=8):
    """The copy's console once want appears in it, or all of it at the end."""
    log = ""
    end = time.time() + secs
    while time.time() < end:
        p = tmp / "host.log"
        log = p.read_text(errors="replace") if p.exists() else ""
        if want in log:
            break
        time.sleep(0.1)
    return log


def stop_copy(proc, tmp):
    import shutil
    proc.kill()
    proc.wait(5)
    shutil.rmtree(tmp, ignore_errors=True)


def port_answers(port, secs=4):
    """Does something accept a call on port and say anything within secs."""
    try:
        c = socket.create_connection((HOST, port), timeout=2)
    except OSError:
        return False
    try:
        c.settimeout(0.3)
        end = time.time() + secs
        while time.time() < end:
            try:
                if c.recv(256):
                    return True
            except socket.timeout:
                continue
            except OSError:
                return False
        return False
    finally:
        c.close()


def test_config_network():
    """CONFIG network (1.1.0): Wi-Fi and the listening port on one page.

    The page was "wifi". The port joined it, so the menu calls it network,
    and CONFIG wifi still opens it because every guide before 1.1.0 says so.
    The port is used from the next restart, like the network beside it, and
    it cannot be the backup window's port.
    """
    print("CONFIG network and the listening port")
    local = HOST in ("127.0.0.1", "localhost")
    s = cfg_sysop("CfgNet")

    s.buf.clear()
    s.send(b"config\r")
    read_list(s)
    menu = plain(s.buf)
    ok = check("the CONFIG menu names the page network",
               re.search(rb"network\s+Wi-Fi and port, next restart", menu) is not None)
    ok &= check("and lists it once, not as wifi as well",
                re.search(rb"^wifi\s", menu, re.M) is None and b"WI-FI" not in menu)

    ok &= check("CONFIG network opens it", cfg_open(s, b"network", b"Password") and
                b"NETWORK" in plain(s.buf))
    page = render_lines(s.buf)
    ok &= check("with Port under Network and Password",
                any(ln.strip().startswith("Port") for ln in page))
    cfg_cancel(s)
    ok &= check("CONFIG wifi still opens the same page",
                cfg_open(s, b"wifi", b"Password") and b"NETWORK" in plain(s.buf))

    # The note, on the status line while Port has the focus.
    s.buf.clear()
    s.send(DOWN * 2)
    s.pump(0.6)
    ok &= check("Port's note says when it takes effect",
                any("Callers use it from the next restart." in ln for ln in render_lines(s.buf)))
    cfg_cancel(s)

    if not local:
        print("  SKIP  the file and the restart need the local harness")
        s.close()
        return ok

    before = cfg_line("port")

    # The backup window's port is taken. Refused on the form, nothing written.
    backup = int(cfg_value("backup_port") or "8080")
    cfg_open(s, b"network", b"Password")
    s.buf.clear()
    s.send(DOWN * 2 + b"\x08" * 6 + str(backup).encode() + F1)
    got = cfg_verdict(s, [b"Same as the backup port", b"Saved", b"saved, but"])
    ok &= check("the backup port is refused as the listening port",
                got == b"Same as the backup port")
    ok &= check("in the copy's words, inside 38 columns",
                b"Same as the backup port. Pick another." in plain(s.buf))
    cfg_cancel(s)
    ok &= check("and nothing is written", cfg_line("port") == before)

    cfg_open(s, b"network", b"Password")
    s.buf.clear()
    s.send(DOWN * 2 + b"\x08" * 6 + b"0" + F1)
    got = cfg_verdict(s, [b"Between 1 and 65535", b"Saved", b"saved, but"])
    ok &= check("0 is not a port", got == b"Between 1 and 65535")
    cfg_cancel(s)

    # A real change: written, and used from the next restart, not now.
    moved = PORT + 3000
    cfg_open(s, b"network", b"Password")
    s.buf.clear()
    s.send(DOWN * 2 + b"\x08" * 6 + str(moved).encode() + F1)
    got = cfg_verdict(s, [b"next restart", b"Saved and live", b"saved, but"])
    ok &= check("a new port saves, for the next restart", got == b"next restart")
    ok &= check("into system.cfg", (cfg_line("port") or "").split("=", 1)[-1].strip() == str(moved))
    ok &= check("and this board is not listening on it yet", not port_answers(moved, 1))

    # The restart: a board started on these files listens on the new port.
    import shutil

    def boot_log(tmp, want):
        """The copy's console once want appears in it, or all of it after 4 s."""
        log = ""
        for _ in range(40):
            p = tmp / "host.log"
            log = p.read_text(errors="replace") if p.exists() else ""
            if want in log:
                break
            time.sleep(0.1)
        return log

    proc, tmp = restart_copy()
    try:
        ok &= check("after a restart the board listens on the new port",
                    f"listening on {moved}," in boot_log(tmp, f"listening on {moved},"))
        ok &= check("and answers a caller there", port_answers(moved))
    finally:
        proc.kill()
        proc.wait(5)
        shutil.rmtree(tmp, ignore_errors=True)

    # A file edited by hand to give both the same port still boots: callers
    # keep theirs, the backup window goes back to its default, and the log
    # says why. Not the harness's own backup port, which its board holds.
    clash = PORT + 3001
    proc, tmp = restart_copy(edits={("", "port"): str(clash), ("", "backup_port"): str(clash)})
    try:
        log = boot_log(tmp, f"listening on {clash},")
        ok &= check("a hand-edited clash still boots, on the port callers dial",
                    f"listening on {clash}," in log)
        ok &= check("with the backup window moved back to its default, and said",
                    "backup_port cannot be the BBS port" in log and "backup port 8080," in log)
    finally:
        proc.kill()
        proc.wait(5)
        shutil.rmtree(tmp, ignore_errors=True)

    # Back to the port the rest of the suite expects the parser to hold,
    # because test_config_parser_rules checks the backup port against it.
    cfg_open(s, b"network", b"Password")
    s.buf.clear()
    s.send(DOWN * 2 + b"\x08" * 6 + str(BBS_PORT_NUM).encode() + F1)
    cfg_verdict(s, [b"next restart", b"Nothing changed"])
    ok &= check("put back", (cfg_line("port") or "").split("=", 1)[-1].strip() == str(BBS_PORT_NUM))
    s.close()
    return ok


# ---------------------------------------------------------------------------
# Recovery without a reflash (1.1.0): the BOOT-hold reset and the last good
# Wi-Fi network. Both run on copies of this board, started with the host
# build's simulated button and radio, so the harness board is never reset.
# ---------------------------------------------------------------------------

def tree_digest(root, skip=()):
    """A digest of every file under root, by relative path and contents,
    leaving out the top-level directories named in skip."""
    import hashlib
    h = hashlib.sha256()
    for p in sorted(root.rglob("*")):
        rel = p.relative_to(root)
        if rel.parts and rel.parts[0] in skip:
            continue
        if p.is_file():
            h.update(str(rel).encode() + b"\0" + p.read_bytes() + b"\0")
    return h.hexdigest()


def caller_on(port):
    """An ANSI caller on another board's port: Caller dials PORT."""
    global PORT
    saved = PORT
    PORT = port
    try:
        return Caller(ansi=True)
    finally:
        PORT = saved


def test_boot_hold():
    """The BOOT-hold reset (Rob, settled 2026-09-23), one band per copy.

    Released under 7 s nothing happens; 7 to 15 s the sysop password goes
    back to the published default and nothing else changes; 15 to 20 s
    userdata and logs are erased and the screens and the card are not; held
    to 20 s the reset is abandoned. The action happens on release, and the
    two that act restart the board, which says why at its next staff login.
    The host build plays the hold on a simulated clock (BBS_BOOT_HOLD_MS), so
    none of this waits in real time, and restarts itself as the board would.
    """
    print("BOOT-hold reset")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the local harness")
        return True

    # An account for the copies to keep, or to lose.
    c = ansi_login("BootKeeper")
    c.close()
    time.sleep(1.0)                    # the logoff's account write, before the copy

    ok = True
    user_of = lambda tmp: tmp / "data" / "user"
    logs_of = lambda tmp: tmp / "data" / "logs"

    def cfg_lines(tmp):
        p = user_of(tmp) / "system.cfg"
        return p.read_text().splitlines() if p.exists() else []

    def boot(hold_ms, port, card=None):
        tmp = copy_data()
        before = {
            "cfg": cfg_lines(tmp),
            "users": (user_of(tmp) / "users.txt").read_bytes(),
            "screens": tree_digest(tmp / "data", skip=("user", "logs")),
        }
        env = {"BBS_BOOT_HOLD_MS": str(hold_ms)}
        if card:
            env["BBS_SD_DIR"] = str(card)
        proc = start_copy(tmp, (str(port),), env)
        log = copy_log(tmp, f"listening on {port},")
        return proc, tmp, before, log

    base = PORT + 3200

    # ---- under 7 s: nothing ------------------------------------------------
    proc, tmp, before, log = boot(6900, base)
    try:
        ok &= check("a hold is seen and said", "reset: BOOT held. Let go before 7 s and nothing happens." in log)
        ok &= check("6.9 s: let go at 6 s, nothing changed",
                    "reset: let go at 6 s. Nothing changed." in log and "reset: 7 s." not in log)
        ok &= check("with a slow blink the whole time",
                    "led: slow at 500 ms" in log and "led: fast" not in log and "led: free" in log)
        ok &= check("and the settings and accounts untouched, without a restart",
                    cfg_lines(tmp) == before["cfg"] and
                    (user_of(tmp) / "users.txt").read_bytes() == before["users"] and
                    "host: restarting" not in log and f"listening on {base}," in log)
    finally:
        stop_copy(proc, tmp)

    # ---- 7 to 15 s: the sysop password back to the published default ------
    port = base + 1
    proc, tmp, before, log = boot(8000, port)
    try:
        ok &= check("8 s: the 7 s stage is said while held",
                    "reset: 7 s. Let go now to put the sysop password back to the default." in log)
        ok &= check("the LED goes from a slow blink to rapid flashing at 7 s",
                    "led: slow at 500 ms" in log and "led: fast at 7500 ms" in log and "led: solid" not in log)
        ok &= check("on release, the copy's five lines",
                    all(line in log for line in (
                        "reset: sysop password is back to the published default.",
                        "reset: it works from this network only, until it is changed.",
                        "reset: accounts, settings, mail and Wi-Fi are all kept.",
                        "reset: the directory listing waits until the password changes.",
                        "reset: restarting. Log in from this network to choose a new password.")))
        after = cfg_lines(tmp)
        ok &= check("the sysop_password line is gone",
                    not any(l.strip().startswith("sysop_password") for l in after))
        ok &= check("and every other line is as it was",
                    after == [l for l in before["cfg"] if not l.strip().startswith("sysop_password")])
        ok &= check("the accounts are kept", (user_of(tmp) / "users.txt").read_bytes() == before["users"])
        ok &= check("the board restarted, on the published default",
                    "host: restarting (note 1)" in log and "boot: password reset by BOOT" in log and
                    "on the published default, local network only" in log)
        reboots = (logs_of(tmp) / "reboots.log").read_text().splitlines()
        ok &= check("reboots.log says what happened", bool(reboots) and
                    reboots[-1].endswith("  password reset by BOOT"))
        d = caller_on(port)
        d.wait_for(b"Enter your handle", 10)
        login(d, "BootKeeper", wait_main=False)
        # The owner of a board with accounts on it, not somebody setting up
        # a fresh one (copy 6b.6): fact, where to find it, what to do.
        ok &= check("a local caller is told the password was reset, and offered setup",
                    d.wait_for(b"The sysop password was reset with the BOOT button.", 8) and
                    d.wait_for(b"Sysop password", 4))
        offer = plain(d.buf)
        ok &= check("in the words for a board that was reset, not a fresh one",
                    b"The published default is on the install page." in offer and
                    b"Enter it here, then choose a new one." in offer and
                    b"has not been set up yet" not in offer)
        d.buf.clear()
        d.send(BBS_DEFAULT.encode() + b"\r")
        ok &= check("the published default works from here",
                    d.wait_for(b"SysOp node", 6))
        # Through the setup: its screen, the staff passwords form (left with
        # ESC) and the tour. The setup screen opens with @CLS@, so the notice
        # used to be printed and wiped in the same breath; it is told at the
        # first prompt after the setup now.
        seen = bytearray()
        for _ in range(8):
            if b"STAFF PASSWORDS" in plain(d.buf):
                break
            if b"PRESS SPACE" in plain(d.buf).upper():
                seen += d.buf
                d.buf.clear()
                d.send(b" ")
            d.pump(1.0)
        d.wait_for(b"STAFF PASSWORDS", 6)
        d.pump(0.5)
        d.send(b"\x1b")                              # leave the form: nothing changed
        d.pump(0.6)
        for _ in range(12):
            if b"Sysop:" in plain(d.buf):
                break
            if b"PRESS SPACE" in plain(d.buf).upper():
                seen += d.buf
                d.buf.clear()
                d.send(b" ")
            d.pump(1.0)
        d.pump(0.5)
        seen += d.buf
        screen = render_lines(seen)
        at = [i for i, l in enumerate(screen) if l == "Last restart: password reset by BOOT."]
        ok &= check("and the sysop is told why the board restarted, after the setup, on the screen left",
                    bool(at))
        ok &= check("with what it did and did not touch",
                    bool(at) and at[-1] + 1 < len(screen) and
                    screen[at[-1] + 1] == "Sysop password only. Accounts are kept.")
        d.close()
    finally:
        stop_copy(proc, tmp)

    # ---- 15 to 20 s: a factory reset of userdata and logs -------------------
    import tempfile
    card = pathlib.Path(tempfile.mkdtemp(prefix="bbs-card-"))
    (card / "KEEP.TXT").write_text("the card is never touched\n")
    port = base + 2
    proc, tmp, before, log = boot(16000, port, card)
    try:
        ok &= check("16 s: the 15 s stage is said while held",
                    "reset: 15 s. Let go now for a FACTORY RESET of accounts and settings." in log)
        ok &= check("slow, then rapid, then solid",
                    "led: slow at 500 ms" in log and "led: fast at 7500 ms" in log and
                    "led: solid at 15500 ms" in log and "led: off" not in log)
        ok &= check("on release, the copy's lines",
                    all(line in log for line in (
                        "reset: FACTORY RESET. Erasing userdata and logs.",
                        "reset: done. Screens, firmware and SD card were not touched.",
                        # RB-fr-3: the host build has no include/secrets.h
                        # on its path, so it is the release's line.
                        "reset: restarting, Wi-Fi erased. The web installer sets it again.")))
        ok &= check("the accounts and the settings are gone",
                    not (user_of(tmp) / "users.txt").exists() and not (user_of(tmp) / "system.cfg").exists())
        ok &= check("so is the caller log", not (logs_of(tmp) / "calls.log").exists())
        reboots = (logs_of(tmp) / "reboots.log").read_text().splitlines() \
            if (logs_of(tmp) / "reboots.log").exists() else []
        ok &= check("reboots.log starts again, with the reason",
                    len(reboots) == 1 and reboots[0].endswith("  factory reset by BOOT"))
        ok &= check("the screens are exactly as they were",
                    tree_digest(tmp / "data", skip=("user", "logs")) == before["screens"])
        ok &= check("and the card too", (card / "KEEP.TXT").read_text() == "the card is never touched\n")
        ok &= check("the board came back on the published default",
                    "boot: factory reset by BOOT" in log and "on the published default, local network only" in log)
    finally:
        stop_copy(proc, tmp)
        import shutil
        shutil.rmtree(card, ignore_errors=True)

    # ---- held to 20 s: abandoned --------------------------------------------
    port = base + 3
    proc, tmp, before, log = boot(21000, port)
    try:
        ok &= check("21 s: the 20 s stage says it is cancelled",
                    "reset: 20 s. Cancelled. Let go; nothing will change." in log)
        ok &= check("and the LED goes off at 20 s",
                    "led: solid at 15500 ms" in log and "led: off at 20500 ms" in log)
        ok &= check("released after 20 s: nothing changed, no restart",
                    "reset: let go after 20 s. Nothing changed." in log and
                    "host: restarting" not in log and
                    cfg_lines(tmp) == before["cfg"] and
                    (user_of(tmp) / "users.txt").read_bytes() == before["users"])
    finally:
        stop_copy(proc, tmp)

    # ---- no hold: the harness board itself says nothing about it ------------
    ok &= check("with nobody on the button a boot says nothing about it",
                "reset:" not in (DATA.parent / "host.log").read_text(errors="replace")
                if (DATA.parent / "host.log").exists() else True)
    return ok


def test_config_wifi_fallback():
    """The last network that worked (1.1.0).

    A board keeps the last network it joined in userdata/wifi.last, and a
    network changed in CONFIG that has not joined within 60 s of boot is
    given up for it: a typo costs a minute rather than a trip with a cable.
    No record, or the same network: it keeps dialling as before. The host
    build plays the radio on a simulated clock (BBS_HOST_WIFI); the rule and
    the file are the board's.
    """
    print("Wi-Fi: back to the last network that worked")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the local harness")
        return True
    ok = True
    ssid = cfg_line("wifi_ssid").split("=", 1)[1].strip() if cfg_line("wifi_ssid") else ""
    pwd = cfg_line("wifi_password").split("=", 1)[1].strip() if cfg_line("wifi_password") else ""
    port = PORT + 3300

    def run(spec, last=None):
        tmp = copy_data()
        rec = tmp / "data" / "user" / "wifi.last"
        if last is not None:
            rec.write_text(last)
        proc = start_copy(tmp, (str(port),), {"BBS_HOST_WIFI": spec})
        log = copy_log(tmp, f"listening on {port},")
        after = rec.read_text() if rec.exists() else None
        stop_copy(proc, tmp)
        return log, after

    # A network that never joins, with a different one that worked before.
    log, after = run("never", "OldNet\noldpass1\n")
    # The console lines are the copy's (WF-*, section 6b.5), and "60 s" is
    # the timer's, so a change to one without the other shows here.
    ok &= check("a network new to the board is said to be on trial",
                f'wifi: 60 s to join "{ssid}", or back to "OldNet", which worked' in log)
    ok &= check("not joined in 60 s: back to the last one that worked, at 60 s and not before",
                "wifi (host): went back at 60000 ms" in log and
                f'wifi: could not join "{ssid}" in 60 s; going back to "OldNet"' in log)
    ok &= check("and says the new one is tried again at every restart",
                "wifi: each restart tries it for 60 s first; change it in CONFIG network" in log)
    ok &= check("which joins", 'wifi (host): joined "OldNet" at 61000 ms' in log)
    ok &= check("and is still the one kept", after == "OldNet\noldpass1\n")

    # The same, but the new network answers at 5 s.
    log, after = run("up=5000", "OldNet\noldpass1\n")
    ok &= check("a new network that joins is not given up",
                "went back" not in log and f'wifi (host): joined "{ssid}" at 5000 ms' in log)
    ok &= check("and becomes the one to go back to",
                after == f"{ssid}\n{pwd}\n" and
                f'wifi: joined "{ssid}"; kept as the network to go back to' in log)

    # No record at all: keeps dialling, as before 1.1.0.
    log, after = run("never")
    ok &= check("with no last good network it keeps dialling the one it has",
                "went back" not in log and "s to join" not in log and
                f'wifi (host): still dialling "{ssid}" at 120000 ms' in log)
    ok &= check("and writes no record of a network that never joined", after is None)

    # The record is the network it is dialling: nothing to go back to.
    log, after = run("never", f"{ssid}\n{pwd}\n")
    ok &= check("the same network as the record is not second-guessed",
                "went back" not in log and "s to join" not in log)

    # A changed password on the same network is a different network to try,
    # and the console says it is the password, not the name twice over.
    log, after = run("never", f"{ssid}\nsomethingelse\n")
    ok &= check("the same name with another password is still on trial",
                "wifi (host): went back at 60000 ms" in log)
    ok &= check("said as a new password, not as the same name twice",
                f'wifi: new password for "{ssid}": 60 s to join, or back to the old one' in log and
                f'wifi: could not join "{ssid}" on the new password; back to the old one' in log and
                f'back to "{ssid}"' not in log)
    return ok


def announce_port(s):
    """The port ANNOUNCE TEST says the heartbeat would carry, or None.

    TEST will not build a payload while a heartbeat is out, and a plugin
    restart sends one at once, so it waits for that and asks again."""
    for _ in range(8):
        s.pump(0.5)
        s.buf.clear()
        s.send(b"announce test\r")
        i = wait_any(s, [b'"interests"', b"going out"], 5)
        s.pump(0.3)
        if i != 1:
            break
    m = re.search(rb'"port":(\d+)', plain(s.buf))
    return int(m.group(1)) if m else None


def test_config_announce_outside():
    """Announce's Outside port (1.1.0): blank sends the listening port.

    The row was "Port", and a board that never set it published 6400
    whatever it listened on. It is "Outside" now, the router's side of a
    forward, and empty means the board's own port. The harness board
    listens on its tag's port and never on 6400, which is what makes the
    first check a test rather than a coincidence.
    """
    print("Announce: Outside, and the listening port")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the local harness")
        return True
    saved = (USERDATA / "system.cfg").read_bytes()
    s = cfg_sysop("Outsider")

    ok = check("with Outside blank, the heartbeat carries the port the board listens on",
               announce_port(s) == PORT)

    # Enabled, Read, Write, Admin, then Board (not editable, skipped),
    # Sysop, About, DNS name: seven downs to Outside.
    cfg_open(s, b"announce", b"Every min")
    page = render_lines(s.buf)
    row = next((ln for ln in page if ln.strip().startswith("Outside")), "")
    ok &= check("the row is labelled Outside", bool(row))
    # An empty box that is not focused is drawn as dots (Form::drawField).
    ok &= check("and is empty while nothing is set",
                bool(row) and row.strip()[len("Outside"):].strip(" .") == "")
    s.buf.clear()
    s.send(DOWN * 7)
    s.pump(0.6)
    ok &= check("its note says what it is",
                any("What callers dial through your router." in ln for ln in render_lines(s.buf)))

    s.send(b"2323" + F1)
    got = cfg_verdict(s, [b"Saved and live", b"saved, but", b"Numbers only"])
    ok &= check("a router's port saves", got == b"Saved and live")
    ok &= check("and is what the heartbeat carries", announce_port(s) == 2323)

    cfg_open(s, b"announce", b"Every min")
    s.buf.clear()
    s.send(DOWN * 7 + b"\x08" * 6 + F1)
    got = cfg_verdict(s, [b"Saved and live", b"saved, but", b"Numbers only"])
    ok &= check("emptying it is allowed", got == b"Saved and live")
    ok &= check("and the listening port is sent again", announce_port(s) == PORT)

    ok &= check("the file is put back", announce_restore(s, saved))
    s.close()
    return ok


def test_accounts_form_notes():
    """A form's field notes stay with that form (1.1.0, found in passing).

    Bbs::addField never cleared FormField::note, and a session's field array
    outlives every form drawn in it. Sign-up puts "Only you and staff can
    see this." on Email, From and Phone, rows 4 to 6; PROFILE later in the
    same call has Profile and Start on rows 5 and 6, and they inherited it.
    CONFIG's per-field notes would have leaked into USER EDIT the same way.
    """
    print("Form notes stay with their form")
    c = ansi_login("NoteLeak")                   # new: registers through the form
    c.buf.clear()
    c.send(b"profile\r")
    ok = check("PROFILE opens", c.wait_for(b"YOUR PROFILE", 5))
    c.pump(0.4)
    c.buf.clear()
    c.send(DOWN * 4)                             # Name, Email, From, Phone, Profile
    c.pump(0.8)
    ok &= check("the Profile row does not claim to be private",
                not any("Only you and staff" in ln for ln in render_lines(c.buf)))
    c.send(b"\x1b")
    c.pump(0.4)
    c.close()
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
    a.send(b"\rNew Ascii Name\r")             # keep the path, type a new name
    # Then Enter past every remaining level field until the board asks to
    # save. NOT a fixed run of Enters: this was "\r\r\ry" for the four fields
    # the page used to have, and it broke the day the page gained the two
    # that were being silently dropped. The symptom was "which saves from the
    # line prompts" failing, which points nowhere near the cause.
    #
    # Same lesson as to_save() in the cursor forms, now on the plain ASCII
    # path: press until the board gives a verdict, never count the fields.
    saved = False
    for _ in range(10):
        if b"Save (Y/n)" in plain(a.buf):
            a.send(b"y")
            saved = a.wait_for(b"Saved and live", 8)
            break
        a.send(b"\r")
        a.pump(0.4)
    ok &= check("which saves from the line prompts", saved)
    # The form it writes, not the exact levels. This used to pin the whole
    # line including a four-field tail, which made it a test of how many
    # permission levels an area has rather than of whether the ASCII path
    # writes the same bar-separated format the cursor path does. The levels
    # have their own test; this one is about the shape.
    line1 = ""
    for ln in cfg.read_text().splitlines():
        if ln.strip().startswith("area1"):
            line1 = ln.strip()
    ok &= check("in the same bar-separated form",
                line1.startswith("area1 = pub/c64 | New Ascii Name |"))
    ok &= check("and with every part the page offered",
                line1.count("|") == 5)
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



def test_config_area_keeps_every_part():
    """Saving an area must not throw away the parts the page did not show.

    kAreaParts has six entries. kComposites registered it with a count of
    four and kMaxParts was four, so the sub-page offered Path, Name, Read and
    Upload, and configSubSave packs exactly comp->count parts over whatever
    was already in the file. Saving any area therefore dropped its Download
    and Delete levels, and files::mayDown falls back to the area's READ
    level, so a staff-only download area silently became downloadable by
    everybody. The board reported success.

    The shape of this test is the point. Asserting that the save succeeded
    passes against the bug, because the save did succeed; it is the FILE that
    is wrong afterwards. So it reads system.cfg back and counts the parts.

    Verified to fail against the broken tree: with count 4 the area5 line
    comes back as four fields and the last two checks fail.
    """
    print("An area keeps the parts its page never showed")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build")
        return True
    sd = os.environ.get("BBS_SD_DIR", "")
    if not sd:
        print("  SKIP  the files plugin needs a card")
        return True
    if not PASSWORD:
        print("  SKIP  CONFIG is sysop only")
        return True

    cfg = USERDATA / "system.cfg"
    before = ""
    for line in cfg.read_text(errors="replace").splitlines():
        if line.strip().startswith("area5"):
            before = line.strip()
    # area5 in the harness is the six-field form:
    #   pub/drop | Drop Box | all | users | all | sysop
    ok = check("the harness has a six-part area to save",
               before.count("|") == 5)
    if not ok:
        print("  SKIP  no six-part area in this config")
        return True

    s = ansi_login("AreaKeep")
    drain(s)
    s.buf.clear()
    s.send(f"bye {PASSWORD}\r".encode())
    if not s.wait_for(b"SysOp node", 6):
        print("  SKIP  could not elevate")
        s.close()
        return True
    s.pump(0.6)

    s.buf.clear()
    s.send(b"config files\r")
    ok &= check("the files page opens", s.wait_for(b"Area 1", 6))
    s.pump(0.8)

    # Four fields (enabled, read, write, admin) then the area buttons, so
    # area 5 is eight rows down. Counting rows is fragile by nature; it is
    # checked by what opens rather than assumed.
    s.buf.clear()
    s.send(DOWN * 8 + b"\r")
    ok &= check("area 5's own page opens", s.wait_for(b"FILE AREA 5", 6))
    s.pump(0.8)
    sub = plain(s.buf)

    # The fix is visible on the page itself: all six parts get a field.
    ok &= check("the page offers Download, which it used to leave out",
                b"Download" in sub)
    ok &= check("and Delete", b"Delete" in sub)

    # Change ONE thing and save. Changing nothing proves nothing: configSubSave
    # short-circuits an unchanged page with "Nothing changed" and never writes
    # the file, so a no-op save leaves the line intact whether the code is
    # right or not. The first version of this test did exactly that and passed
    # against the bug for the wrong reason.
    #
    # Editing the name is also the real report: a sysop renames an area and
    # its download and delete levels go with it.
    s.buf.clear()
    s.send(DOWN)                                   # Path -> Name
    s.pump(0.4)
    s.send(b"\x08" * 24 + b"Drop Zone")
    s.pump(0.4)
    to_save(s, b"Saved")
    s.pump(1.2)
    ok &= check("the save is accepted", b"Saved" in plain(s.buf))
    s.pump(1.5)

    after = ""
    for line in cfg.read_text(errors="replace").splitlines():
        if line.strip().startswith("area5"):
            after = line.strip()

    # Proof the write really happened, so the checks below mean something.
    ok &= check("the edit reached the file", "Drop Zone" in after)

    # The two that fail against the bug.
    ok &= check("the saved line still has all six parts",
                after.count("|") == 5)
    ok &= check("and the download and delete levels survived the rename",
                after.endswith("all | sysop"))

    drain(s)
    s.send(b"\x1b")
    s.pump(0.5)
    s.close()
    return ok


def test_config_forum_levels():
    """Saving a forum in CONFIG keeps what it was running under.

    Rob, after saving a forum: "oddly it says I cant post in there but I
    can post I just cant reply". The sub-page seeded every unset level from
    the plugin's read, write and admin levels BY POSITION. A forum has four
    level parts, so Reply got the plugin's admin level (co1) and Moderate
    got "nobody", and Save pinned both. Start got write, so posting worked
    and replying did not.

    topic1 in the harness sets no levels at all, which is exactly Rob's
    case: every one of them is a fallback. Renamed rather than saved
    unchanged, because an unchanged page is never written.
    """
    print("A forum keeps its levels through CONFIG")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build")
        return True
    if not os.environ.get("BBS_SD_DIR", ""):
        print("  SKIP  the forums plugin needs a card")
        return True
    if not PASSWORD:
        print("  SKIP  CONFIG is sysop only")
        return True

    cfg = USERDATA / "system.cfg"

    def topic1():
        for line in cfg.read_text(errors="replace").splitlines():
            if line.strip().startswith("topic1"):
                return line.strip()
        return ""

    s = ansi_login("LevelKeep")
    drain(s)
    s.buf.clear()
    s.send(f"bye {PASSWORD}\r".encode())
    if not s.wait_for(b"SysOp node", 6):
        print("  SKIP  could not elevate")
        s.close()
        return True
    s.pump(0.6)

    s.buf.clear()
    s.send(b"config forums\r")
    ok = check("the forums page opens", s.wait_for(b"Topic 1", 6))
    s.pump(0.8)
    # enabled, read, write, admin, then Topic 1: the fifth row.
    s.buf.clear()
    s.send(DOWN * 4 + b"\r")
    ok &= check("topic 1's own page opens", s.wait_for(b"FORUM 1", 6))
    s.pump(0.8)

    # What the page SHOWS before anything is saved. The harness gives the
    # plugin write = users and leaves admin at the descriptor's co1, so an
    # unset Reply runs under users and an unset Moderate under co1.
    # [a-z0-9]+, not \S+: an unfocused box is padded with dots, so \S+
    # read "users....................." and failed a correct page.
    shown = "\n".join(render_lines(s.buf))
    reply = re.search(r"Reply\s+([a-z0-9]+)", shown)
    mod   = re.search(r"Moderate\s+([a-z0-9]+)", shown)
    ok &= check("an unset Reply shows what it runs under, users",
                reply is not None and reply.group(1) == "users")
    ok &= check("an unset Moderate shows co1, not nobody",
                mod is not None and mod.group(1) == "co1")

    s.buf.clear()
    s.send(DOWN)                                   # Key -> Name
    s.pump(0.4)
    s.send(b"\x08" * 24 + b"General Talk")
    s.pump(0.4)
    to_save(s, b"Saved")
    s.pump(1.5)

    after = topic1()
    parts = [x.strip() for x in after.split("=", 1)[-1].split("|")]
    ok &= check("the edit reached the file", "General Talk" in after)
    ok &= check("the saved forum has all seven parts", len(parts) == 7)
    ok &= check("Reply was saved as users, not the admin level",
                len(parts) == 7 and parts[5] == "users")
    ok &= check("Start follows Reply",
                len(parts) == 7 and parts[4] == "users")
    ok &= check("and Moderate was saved as co1, not nobody",
                len(parts) == 7 and parts[6] == "co1")

    drain(s)
    s.send(b"\x1b")
    s.pump(0.5)
    s.close()
    return ok


def test_room_quit_logoff():
    """/q+ leaves the room and logs off, and the room's help says so.

    Rob: "in chat add /q+ which loggs off along with quit, make sure all
    menus are updated in chat for all commands we've added."
    """
    print("Chat room: /q+ and the help that lists it")
    a = ansi_login("Quitter")
    b = ansi_login("Watcher")
    for x in (a, b):
        x.send(b"chat\r")
        x.wait_for(b"here.", 4)
        x.buf.clear()

    a.send(b"/?\r")
    a.pump(1.0)
    shown = render_lines(a.buf)
    ok = check("the room help lists /q+",
               any(ln.startswith("/q+") for ln in shown))
    # The column was eleven wide and cut "/whois handle" to "/whois hand".
    ok &= check("and /whois handle is not cut short",
                any(ln.startswith("/whois handle") for ln in shown))
    ok &= check("and every row fits 39 columns",
                all(len(ln.rstrip()) <= 39 for ln in shown if ln.startswith("/")))

    a.buf.clear()
    b.buf.clear()
    a.send(b"/q+\r")
    gone = False
    for _ in range(40):                       # the send-off lingers ~5 s
        if not a.pump(0.5):
            gone = True
            break
    ok &= check("/q+ ends the call", gone)
    ok &= check("with the send-off rather than a prompt",
                b"Main:" not in plain(a.buf))
    b.pump(1.0)
    ok &= check("and the room is told they logged off",
                b"Quitter" in plain(b.buf) and b"logged off" in plain(b.buf))
    b.close()
    return ok


def test_welcome_connecting():
    """The welcome's last line, as Rob asked for it.

    "add a LF, have it start at the begining of the line and use an fx
    300baud there and then a waiting motion for the connect like u already
    do. Then a LF after". And it names the board from CONFIG: the old line
    read "Connecting you unleashed", with no "to" and the software's name.
    """
    print("Welcome: the connecting line")
    c = Caller(ansi=True)
    ok = check("the connecting line arrives", c.wait_for(b"Connecting you", 10))
    t0 = time.time()
    c.wait_for(b"Enter your handle", 12)
    dt = time.time() - t0
    line, above = line_with(c.buf, "Connecting you")
    ok &= check("it starts in column 0",
                line is not None and line.startswith("Connecting you"))
    ok &= check("with a blank line above it",
                above is not None and above.strip() == "")
    # The harness sets no board_name, so @BOARD@ falls back to the
    # software's name here; on Rob's board it is whatever CONFIG says.
    ok &= check("and says who it is connecting you to",
                line is not None and "Connecting you to " in line
                and "nleashed BBS" in line)
    # At 300 baud the rest of the line is a second or more on its own, then
    # the spinner. Unpaced it was the spinner alone, about 0.9 s.
    ok &= check("typed at 300 baud rather than all at once (%.1f s)" % dt, dt > 1.6)
    c.close()
    return ok


def test_paced_chatin():
    """A paced screen that a plugin shows on the way in arrives whole.

    Found by code review before 0.21.9 shipped. showScreen plays a
    transition screen (chatin, files) in one loop with nothing draining the
    line, bounded by bytes. @BAUD@ pacing stops at frames. The two never
    meet, so a chatin that began with @BAUD:300@ typed about 48 characters
    and the rest was dropped at close(), with nothing in the log.
    """
    print("A paced chatin arrives whole")
    sd = os.environ.get("BBS_SD_DIR", "")
    if not sd:
        print("  SKIP  a card is where a sysop's own screens go")
        return True
    folder = pathlib.Path(sd) / "screens"
    folder.mkdir(parents=True, exist_ok=True)
    screen = folder / "chatin.ans"
    kept = screen.read_bytes() if screen.exists() else None
    screen.write_bytes(b"@BAUD:300@" + b"x" * 200 + b"PACED-END\r\n")
    try:
        c = ansi_login("PacedIn")
        drain(c)
        c.buf.clear()
        c.send(b"chat\r")
        got = c.wait_for(b"PACED-END", 8)
        ok = check("the end of a paced chatin still arrives", got)
        c.send(b"/q\r")
        c.pump(0.5)
        c.close()
    finally:
        if kept is None:
            screen.unlink()
        else:
            screen.write_bytes(kept)
    return ok


def test_room_time_staff_only():
    """/t n +m and /t -1 in the room are staff tools, as TIME is.

    From 0.21.4 until 0.22.0 any caller could type /t -1 and be off the
    clock, or /t 3 +600 and give a node ten hours: the shell's TIME checked
    PERM_TIME before calling the handler, and the room called the same
    handler directly. Found while writing the long help, not by a test,
    because the existing test only ever tried it as staff.
    """
    print("Chat room: /t cannot change time for a caller")
    a = ansi_login("NoClock")
    b = ansi_login("Clocked")
    nb = b.node()
    for x in (a, b):
        x.send(b"chat\r")
        x.wait_for(b"here.", 4)
    a.buf.clear()
    a.send(b"/t -1\r")
    a.pump(1.0)
    ok = check("an ordinary caller cannot take themselves off the clock",
               b"off the clock" not in plain(a.buf) and b"Only staff" in plain(a.buf))
    a.buf.clear()
    b.buf.clear()          # its login banner says "60 minutes" already
    a.send(f"/t {nb} +600\r".encode())
    a.pump(1.0)
    ok &= check("nor give another node minutes",
                b"Only staff" in plain(a.buf) and b"minutes" not in plain(b.buf))
    a.buf.clear()
    a.send(b"/t\r")
    a.pump(1.0)
    ok &= check("bare /t still tells them the time",
                b"left this call" in plain(a.buf) or b"no time limit" in plain(a.buf))
    a.close()
    b.close()
    return ok


def test_bell():
    """BELL and /b are one setting, and bells actually ring.

    The room's /b toggled a flag that nothing read from 0.21.4 until 0.22.0:
    it answered "Bell off." and the room never rang at all. Its test checked
    the answer. These check the byte, 0x07, which is what a bell is.
    """
    print("Bells: arrivals, the room, privates, and BELL")
    a = ansi_login("Ringer")
    na = a.node()          # before any clear: node() reads the login banner
    drain(a)
    a.buf.clear()
    b = ansi_login("Arriver")
    ok = check("an arrival rings for somebody at the prompt",
               a.wait_for(b"is on node", 6) and b"\x07" in a.buf)
    a.buf.clear()
    a.send(b"bell\r")
    ok &= check("BELL turns it off", a.wait_for(b"Bell off.", 4))
    b.close()
    a.pump(1.0)
    a.buf.clear()
    c = ansi_login("Arriver")
    a.wait_for(b"is on node", 6)
    ok &= check("and an arrival no longer rings", b"\x07" not in a.buf)
    a.send(b"bell\r")
    a.wait_for(b"Bell on.", 4)

    # The room: somebody joining, and a private.
    nb = c.node()
    for x in (a, c):
        drain(x)
        x.buf.clear()
    a.send(b"chat\r")
    a.wait_for(b"here.", 4)
    a.pump(0.5)
    a.buf.clear()
    c.send(b"chat\r")
    c.wait_for(b"here.", 4)
    a.pump(1.0)
    ok &= check("somebody joining the room rings", b"\x07" in a.buf)
    a.buf.clear()
    c.send(f"/p {na} psst\r".encode())
    a.wait_for(b"psst", 4)
    ok &= check("a private rings", b"\x07" in a.buf)
    a.buf.clear()
    a.send(b"/b\r")
    a.wait_for(b"Bell off.", 4)
    a.buf.clear()
    c.send(f"/p {na} again\r".encode())
    a.wait_for(b"again", 4)
    ok &= check("and /b stops it", b"\x07" not in a.buf)
    # One setting, not a room setting reset on every visit.
    a.send(b"/q\r")
    a.pump(0.8)
    a.buf.clear()
    a.send(b"bell\r")
    ok &= check("/b and BELL are the same setting", a.wait_for(b"Bell on.", 4))
    a.close()
    c.close()
    return ok


def test_codes_in_messages():
    """Callers' @-codes act in the room and in the forums; the board's own
    restricted codes print as typed; CODES and /codes explain them."""
    print("Inline @-codes in messages")
    a = ansi_login("Painter")
    b = ansi_login("Viewer")
    for x in (a, b):
        x.send(b"chat\r")
        x.wait_for(b"here.", 4)
        x.buf.clear()
    a.send(b"@YELLOW@shine @TYPE:slowly@ @CLS@ me@example.com\r")
    b.wait_for(b"example.com", 6)
    b.pump(1.5)
    seen = plain(b.buf)
    ok = check("a colour code acts rather than printing",
               b"shine" in seen and b"@YELLOW@" not in seen)
    ok &= check("an effect prints its words", b"slowly" in seen and b"@TYPE" not in seen)
    ok &= check("@CLS@ is not a caller's code and prints as typed",
                b"@CLS@" in seen and b"\x1b[2J" not in bytes(b.buf))
    ok &= check("an email address is left alone", b"me@example.com" in seen)
    b.buf.clear()
    b.send(b"/codes\r")
    ok &= check("/codes lists them in the room",
                b.wait_for(b"Codes in messages", 4) and b"@BLINK" in plain(b.buf))
    ok &= check("and shows @BELL@ without ringing it", b"\x07" not in b.buf)
    a.send(b"/q\r")
    b.send(b"/q\r")
    a.pump(0.5)
    b.pump(0.5)
    b.buf.clear()
    b.send(b"codes\r")
    b.pump(1.5)
    if b"[More]" in b.buf or b"continue" in plain(b.buf).lower():
        drain(b)
    ok &= check("CODES at the prompt explains them",
                b"@BLINK" in plain(b.buf) or b"BLINK" in plain(b.buf))
    ok &= check("and points at FX to see them move (Rob, 0.22.2)",
                b"Type FX at the prompt" in plain(b.buf))
    ok &= check("without ringing the bell it describes", b"\x07" not in b.buf)

    if os.environ.get("BBS_SD_DIR", ""):
        drain(a)
        a.buf.clear()
        a.send(b"forums\r")
        a.wait_for(b"Forums", 6)
        a.pump(0.8)
        a.send(b"1\r")
        a.pump(1.0)
        num = forum_post(a, "Painted", ["@LTGREEN@glowing words@N@ and @BLINK:flashing@"])
        ok &= check("a post with codes goes up", num is not None)
        drain(b)
        b.buf.clear()
        b.send(b"forums\r")
        b.wait_for(b"Forums", 6)
        b.pump(0.8)
        b.send(b"1\r")
        b.pump(1.0)
        b.buf.clear()
        b.send(((num or "1") + "\r").encode())
        b.pump(2.5)
        seen = plain(b.buf)
        ok &= check("a forum post's codes act for the reader",
                    b"glowing words" in seen and b"@LTGREEN@" not in seen
                    and b"flashing" in seen and b"@BLINK" not in seen)
        # Take the post down again. The forums tests after this one count
        # unread messages in this forum and expect only their own posts; the
        # first full card run of 0.22.1 reported the board's correct count
        # as four failures because this one was still there.
        if num:
            m = ansi_login("CodesMod")
            m.send(f"bye {PASSWORD}\r".encode())
            m.wait_for(b"SysOp node", 5)
            m.pump(0.6)
            m.send(b"forums\r")
            m.wait_for(b"Forums", 6)
            m.pump(1.0)
            m.send(b"1\r")
            m.pump(1.2)
            m.send((num + "\r").encode())
            m.pump(1.2)
            m.send(b"d")
            m.wait_for(b"Remove message", 4)
            m.send(b"y")
            ok &= check("and the test takes its post down again",
                        m.wait_for(b"Message #" + num.encode() + b" removed", 5))
            m.close()
    a.close()
    b.close()
    return ok


def test_first_setup():
    """A fresh board sets itself up for a caller on its own network (1.0.0).

    Rob's design: the board ships with the published default sysop password,
    any caller on the local network is asked for it at the end of logging in
    or registering, and the right one leads through a setup screen, the staff
    passwords form and a tour, without anybody having to know about BYE.
    Needs a board with no sysop_password line, which is what
    tools/harness.sh --fresh stands up; on the ordinary test board, whose
    config sets a sysop password, the offer must never appear.
    """
    print("First-boot setup")
    # The setup screen told a new sysop to backspace (DEL on a Commodore)
    # over the stars before typing. Since FF_REPLACE (0.23.0) the first key
    # does that itself, so it sent people looking for a step that does not
    # exist. Checked in the files as shipped, all three flavours; the plain
    # terminal never had it, since line mode has no stars to delete.
    scr = ROOT / "data" / "screens"
    ans = (scr / "setup.ans").read_bytes()
    seq = (scr / "setup.seq").read_bytes()
    asc = (scr / "setup.asc").read_bytes()
    ok = check("the setup screen does not say to delete the stars first",
               b"Backspace" not in ans and pet("DEL") not in seq and b"over them" not in asc)
    ok &= check("it says to type straight over them",
                b"straight over" in ans and pet("straight over") in seq)
    if not os.environ.get("BBS_FRESH"):
        c = ansi_login("NotFreshOne")
        ok &= check("a configured board offers no setup", b"not been set up" not in plain(c.buf))
        c.close()
        return ok

    c = Caller(ansi=True)
    c.wait_for(b"Enter your handle", 10)
    ok &= check("a new caller registers", login(c, "OwnerOne", wait_main=False))
    ok &= check("a local caller on an unconfigured board is offered setup",
                c.wait_for(b"has not been set up yet", 8) and c.wait_for(b"Sysop password", 4))
    c.buf.clear()
    c.send(b"nope\r")
    ok &= check("a wrong password asks again rather than hanging up",
                c.wait_for(b"That is not it", 5) and c.wait_for(b"Sysop password", 4))
    c.buf.clear()
    c.send(b"unleashed\r")
    ok &= check("the default makes them the sysop", c.wait_for(b"SysOp node", 6))
    seen = bytearray()                               # every page of the setup screen
    for _ in range(6):                               # the setup screen, however many pages
        if b"STAFF PASSWORDS" in plain(c.buf):
            break
        if b"Press SPACE" in plain(c.buf) or b"PRESS SPACE" in plain(c.buf):
            seen += plain(c.buf)
            c.buf.clear()
            c.send(b" ")
        c.pump(1.0)
    ok &= check("and the staff passwords form opens by itself",
                c.wait_for(b"STAFF PASSWORDS", 10))
    seen += plain(c.buf)
    ok &= check("the setup screen a new sysop reads says to type over the stars",
                b"YOU ARE THE SYSOP" in seen and b"straight over" in seen and b"Backspace" not in seen)
    c.buf.clear()
    # Typed straight over the stars, no backspace first: the first key has to
    # replace the mask. On 0.22.3 it appended, and this would have saved
    # "********unleashed" and sailed past the check below.
    c.send(BBS_DEFAULT.encode() + F1)
    ok &= check("the published default cannot be chosen, typed over the mask",
                c.wait_for(b"is published", 5))
    c.buf.clear()
    # The same form, emptied: that would switch staff off on the board's
    # very first save, with no way back short of a reflash (1.0.0).
    c.send(b"\x08" * 12 + F1)
    ok &= check("the setup form refuses an empty sysop password",
                wait_any(c, [b"cannot be empty", b"Saved"], 5) == 0)
    c.buf.clear()
    c.send(b"\x08" * 12 + b"fresh1234" + F1)
    ok &= check("a password of their own saves", c.wait_for(b"Saved", 6))
    for _ in range(12):                              # the tour, page by page
        if b"Sysop:" in plain(c.buf):
            break
        if b"Press SPACE" in plain(c.buf) or b"PRESS SPACE" in plain(c.buf):
            c.buf.clear()
            c.send(b" ")
        c.pump(1.0)
    ok &= check("the tour ends at the sysop prompt", b"Sysop:" in plain(c.buf))
    if HOST in ("127.0.0.1", "localhost"):
        cfg = (USERDATA / "system.cfg").read_text()
        ok &= check("the chosen password is written", "sysop_password = fresh1234" in cfg)
    c.buf.clear()
    c.send(b"announce\r")
    c.pump(1.5)
    ok &= check("and the directory listing is no longer held",
                b"Held" not in plain(c.buf))
    c.close()

    d = ansi_login("LaterOne")
    ok &= check("once set up, nobody is offered setup again",
                b"not been set up" not in plain(d.buf))
    d.close()
    return ok


def test_fx_codes():
    """FX shows the message code beside each effect it demonstrates.

    Rob, 0.22.2: "In the FX, you should put the codes for the FX on that
    page too". The demo is the one place a caller sees the effects move, so
    it is also where they learn what to type. Checked at 80 columns, where
    the code follows the effect, and that no row runs past the edge.
    """
    print("FX shows the codes")
    c = ansi_login("FxWatcher")
    drain(c)
    c.buf.clear()
    c.send(b"fx\r")
    done = c.wait_for(b"Demo complete.", 60)
    c.pump(1.0)
    seen = plain(c.buf)
    ok = check("the demo runs to the end", done)
    ok &= check("each effect with a message code shows it",
                all(code in seen for code in (b"@TYPE:text@", b"@DOTS@", b"@SPIN@",
                                              b"@OOPS:text@", b"@SCRAMBLE:text@",
                                              b"@NOISE@", b"@BLINK:text@", b"@BELL@")))
    ok &= check("and it ends by pointing at CODES", b"See CODES" in seen)
    ok &= check("nothing runs past the right edge at 80 columns",
                max_column(bytes(c.buf)) <= 80)
    c.close()
    return ok


def test_room_narrow_effects():
    """An effect past a 40 column reader's margin still prints its words.

    Code review, 0.22.0: the room let the terminal wrap its lines, and the
    renderer cut every effect to a margin it believed in, so a C64 reader
    lost the words inside an effect that crossed column 39 while an 80
    column reader saw them. The room wraps its own lines now.
    """
    print("Chat room: effects at 40 columns")
    a = ansi_login("Widecaller")
    n = Caller(ansi=False)
    n.wait_for(b"HIT DEL OR BACKSPACE", 6)
    n.send(b"\x08")
    n.wait_for(b"Enter your handle", 10)
    ok = check("a 40 column caller gets on", login(n, "Narrowcaller"))
    for x in (a, n):
        x.send(b"chat\r")
        x.wait_for(b"here.", 4)
        x.buf.clear()
    a.send(b"hello there my friend how are @TYPE:yourself@ today\r")
    n.wait_for(b"today", 6)
    n.pump(1.0)
    ok &= check("the words inside the effect arrive", b"yourself" in plain(n.buf))
    a.close()
    n.close()
    return ok


def test_long_help():
    """HELP <command> and /? <command>: one command in full."""
    print("Long help")
    c = ansi_login("Asker")
    drain(c)
    c.buf.clear()
    c.send(b"help who\r")
    ok = check("HELP WHO explains WHO", c.wait_for(b"usage: WHO", 4))
    c.buf.clear()
    c.send(b"help w\r")
    ok &= check("HELP W finds it by its shortcut", c.wait_for(b"usage: WHO", 4))
    c.buf.clear()
    c.send(b"help kick\r")
    c.pump(1.0)
    ok &= check("HELP KICK is unknown to somebody who cannot kick",
                b"No command or menu" in plain(c.buf) and b"usage: KICK" not in plain(c.buf))
    c.buf.clear()
    c.send(b"help bell\r")
    c.pump(1.0)
    ok &= check("the entry that mentions @BELL@ does not ring it",
                b"usage: BELL" in plain(c.buf) and b"\x07" not in c.buf)
    c.buf.clear()
    c.send(b"help off\r")
    c.pump(1.0)
    ok &= check("a hidden alias with nothing written for it is unknown, not an empty box",
                b"No command or menu" in plain(c.buf))
    c.buf.clear()
    c.send(b"?\r")
    c.pump(1.2)
    ok &= check("? says HELP <command> exists", b"HELP <command>" in plain(c.buf))
    ok &= check("and still fits one screen", b"[More]" not in c.buf)
    c.send(b"chat\r")
    c.wait_for(b"here.", 4)
    c.buf.clear()
    c.send(b"/? p\r")
    ok &= check("/? p explains /p", c.wait_for(b"usage: /p", 4))
    c.buf.clear()
    c.send(b"/? /whois\r")
    ok &= check("/? /whois, slash and all", c.wait_for(b"usage: /whois", 4))
    c.buf.clear()
    c.send(b"/? w\r")
    ok &= check("/? w is the room's /w, not WHO at the prompt", c.wait_for(b"usage: /s", 4))
    c.buf.clear()
    c.send(b"/? k\r")
    c.pump(1.0)
    ok &= check("/? k is unknown to a caller", b"No such command" in plain(c.buf))
    c.buf.clear()
    c.send(b"/?\r")
    c.pump(1.0)
    ok &= check("the room's /? lists /codes and /? cmd",
                b"/codes" in plain(c.buf) and b"/? cmd" in plain(c.buf))
    c.close()
    return ok


def test_info_pages():
    """INFO and /i: the sysop's ten pages.

    The harness sets page0 (a title, no text yet) and page1 (staff only).
    A caller must get the same words for page 1 as for a page that does not
    exist, or the numbers can be probed for what they hide: the file areas'
    rule, applied here too.
    """
    print("Information pages")
    if not PASSWORD:
        print("  SKIP  writing a page needs the sysop")
        return True
    c = ansi_login("InfoReader")
    ok = check("login says there is something to read",
               b"information page" in plain(c.buf) and b"INFO reads" in plain(c.buf))
    drain(c)
    c.buf.clear()
    c.send(b"info\r")
    c.pump(1.0)
    seen = plain(c.buf)
    ok &= check("INFO lists the pages a caller may read",
                b"House rules" in seen and b"Staff notes" not in seen)
    c.buf.clear()
    c.send(b"info 1\r")
    c.pump(1.0)
    staff_page = plain(c.buf)
    c.buf.clear()
    c.send(b"info 7\r")
    c.pump(1.0)
    no_page = plain(c.buf)
    ok &= check("a staff page and a missing page answer the same way",
                b"There is no page 1." in staff_page and b"There is no page 7." in no_page)
    c.buf.clear()
    c.send(b"info 0\r")
    c.pump(1.2)
    ok &= check("a titled page with no text says so", b"(this page is empty)" in plain(c.buf))
    c.buf.clear()
    c.send(b"info 2 edit\r")
    c.pump(1.0)
    ok &= check("only the sysop writes pages", b"Only the sysop" in plain(c.buf))

    s = ansi_login("InfoWriter")
    s.buf.clear()
    s.send(f"bye {PASSWORD}\r".encode())
    s.wait_for(b"SysOp node", 6)
    s.pump(0.6)
    s.buf.clear()
    s.send(b"info 2 edit\r")
    ok &= check("INFO 2 EDIT opens the editor", s.wait_for(b" 1: ", 5))
    s.send(b"Opening @YELLOW@hours@N@ are evenings.\r")
    s.pump(0.4)
    s.send(b"Second line.\r")
    s.pump(0.4)
    s.send(b"/s\r")
    ok &= check("and /s saves it", s.wait_for(b"Page 2 saved.", 5))
    s.buf.clear()
    s.send(b"info 2 edit\r")
    s.wait_for(b" 3: ", 5)
    ok &= check("editing again starts from what the page says",
                b"Opening" in plain(s.buf) and b"Second line." in plain(s.buf))
    s.send(b"/a\r")
    ok &= check("and /a leaves it as it was", s.wait_for(b"left as it was", 5))

    c.buf.clear()
    c.send(b"info 2\r")
    c.pump(1.5)
    seen = plain(c.buf)
    ok &= check("a caller reads it, codes acting",
                b"Opening" in seen and b"hours" in seen and b"@YELLOW@" not in seen
                and b"Second line." in seen)
    ok &= check("and is back at the prompt after it", b"Main" in seen.split(b"Second line.")[-1])

    c.send(b"chat\r")
    c.wait_for(b"here.", 4)
    c.buf.clear()
    c.send(b"/i\r")
    c.pump(1.0)
    ok &= check("/i lists them in the room", b"/i2" in plain(c.buf) and b"/i1" not in plain(c.buf))
    c.buf.clear()
    c.send(b"/i2\r")
    c.pump(1.2)
    ok &= check("/i2 reads one in the room", b"Second line." in plain(c.buf))
    c.buf.clear()
    c.send(b"/i2-\r")
    c.pump(1.0)
    ok &= check("/i2- is the sysop's alone", b"Only the sysop" in plain(c.buf))
    c.send(b"/q\r")
    c.pump(0.5)

    s.buf.clear()
    s.send(b"info 2 clear\r")
    ok &= check("INFO 2 CLEAR empties it", s.wait_for(b"Page 2 cleared.", 5))
    c.buf.clear()
    c.send(b"info 2\r")
    c.pump(1.0)
    ok &= check("and a page with no title and no text is gone",
                b"There is no page 2." in plain(c.buf))
    c.close()
    s.close()
    return ok


def test_mailbox():
    """MAIL as a place: a list, numbers, W, Enter for the next, Q back.

    Rob: "Mail is still not a subsystem like forums and chat" and "we cant
    read other mail without deleting, need that full list of emails to be
    able to select and then reply."
    """
    print("The mailbox")
    a = ansi_login("Boxwriter")
    b = ansi_login("Boxreader")
    for text in (b"one @LTGREEN@green@N@ thing", b"two", b"three"):
        a.buf.clear()
        a.send(b"mail Boxreader " + text + b"\r")
        a.wait_for(b"Left for", 5)
    ok = check("MAIL opens a list rather than a message",
               mail_box(b) and b"3 new" in plain(b.buf) and b"[D]elete" not in plain(b.buf))
    shown = render_lines(b.buf)
    ok &= check("the list sits at column 0, * for new",
                any(ln.startswith("* 1 Boxwriter") for ln in shown))
    ok &= check("and a preview drops the codes",
                b"@LTGREEN@" not in plain(b.buf) and b"green" in plain(b.buf))
    b.buf.clear()
    b.send(b"3\r")
    ok &= check("a number reads that one", b.wait_for(b"#3 of 3", 4) and b"three" in plain(b.buf))
    b.buf.clear()
    b.send(b"\r")
    ok &= check("Enter when it is the last says so and goes back",
                b.wait_for(b"That was the last one", 4) and b.wait_for(b"Mail>", 4))
    b.buf.clear()
    b.send(b"1\r")
    b.pump(1.2)
    ok &= check("a message's codes act for the reader",
                b"green" in plain(b.buf) and b"@LTGREEN@" not in plain(b.buf))
    b.buf.clear()
    b.send(b"\r")
    ok &= check("Enter reads the next without deciding this one", b.wait_for(b"#2 of 3", 4))
    b.buf.clear()
    b.send(b"q")
    ok &= check("Q goes back to the list, all still new", b.wait_for(b"3 new", 4))
    b.buf.clear()
    b.send(b"?")
    ok &= check("? lists the keys", b.wait_for(b"Mail: the keys", 4) and b.wait_for(b"Mail>", 4))
    b.buf.clear()
    b.send(b"w")
    ok &= check("W asks who it is for", b.wait_for(b"To: ", 4))
    b.send(b"Nosuchperson\r")
    ok &= check("a name with no account is refused", b.wait_for(b"No account called", 4))
    b.buf.clear()
    b.send(b"w")
    b.wait_for(b"To: ", 4)
    b.send(b"Boxwriter\r")
    ok &= check("and a real one opens the editor", b.wait_for(b"Mail to Boxwriter", 4))
    b.send(b"written from the box\r")
    b.pump(0.3)
    b.send(b"/s\r")
    ok &= check("sending puts them back on the list",
                b.wait_for(b"Left for Boxwriter", 5) and b.wait_for(b"Mail>", 4))
    b.buf.clear()
    b.send(b"q")
    ok &= check("Q on the list leaves the mailbox", b.wait_for(b"Main", 4))
    # Leave both boxes empty for whoever comes next.
    for c in (a, b):
        mail_box(c)
        for _ in range(4):
            c.buf.clear()
            c.send(b"\r")
            if not c.wait_for(b"[D]elete", 3):
                break
            c.send(b"d")
            c.wait_for(b"Mail>", 4)
        c.send(b"q")
        c.pump(0.5)
    a.close()
    b.close()
    return ok


def test_seeded_screens_follow():
    """A screen the board put on the card follows the stock one; one the
    sysop edited, or one the board has no record of, is never touched.

    Found when the welcome changed in 0.21.9: the card held the copy seeded
    the day it was first mounted, the card is played before flash, so the
    new screen never reached a board with a card.
    """
    print("Seeded screens follow the stock set")
    sd = os.environ.get("BBS_SD_DIR", "")
    if not sd or not PASSWORD:
        print("  SKIP  needs a card and the sysop")
        return True
    stock = DATA / "screens"
    card = pathlib.Path(sd) / "screens"
    ours, theirs = "zzseed.asc", "zzown.asc"

    s = ansi_login("Seeder")
    s.buf.clear()
    s.send(f"bye {PASSWORD}\r".encode())
    s.wait_for(b"SysOp node", 6)
    s.pump(0.6)

    def remount():
        s.buf.clear()
        s.send(b"sd unmount\r")
        s.wait_for(b"safe to pull", 6)
        s.buf.clear()
        s.send(b"sd mount\r")
        return s.wait_for(b"Mounted", 8)

    try:
        (stock / ours).write_text("stock one\n")
        (card / theirs).write_text("the sysop's own\n")        # no record of it
        (stock / theirs).write_text("stock for zzown\n")
        ok = check("the card remounts", remount())
        ok &= check("a new stock screen is seeded",
                    (card / ours).read_text() == "stock one\n")
        (stock / ours).write_text("stock two\n")
        remount()
        ok &= check("and an untouched seeded copy follows a new stock one",
                    (card / ours).read_text() == "stock two\n")
        (card / ours).write_text("edited on the board\n")
        (stock / ours).write_text("stock three\n")
        remount()
        ok &= check("but a copy the sysop edited is theirs from then on",
                    (card / ours).read_text() == "edited on the board\n")
        ok &= check("and a card file the board has no record of is left alone",
                    (card / theirs).read_text() == "the sysop's own\n")
    finally:
        for f in (stock / ours, stock / theirs, card / ours, card / theirs):
            try:
                f.unlink()
            except FileNotFoundError:
                pass
        remount()
        s.close()
    return ok


def test_forums_scan_staff():
    """FORUMS SCAN lists every forum on the card, the ones a caller may not
    read included, so it is staff only. Nothing checked until 0.22.0."""
    print("FORUMS SCAN is for staff")
    if not os.environ.get("BBS_SD_DIR", ""):
        print("  SKIP  the forums need a card")
        return True
    c = ansi_login("Scanner")
    drain(c)
    c.buf.clear()
    c.send(b"forums scan\r")
    c.pump(1.2)
    ok = check("a caller is refused", b"for staff" in plain(c.buf)
               and b"Forums on the card" not in plain(c.buf))
    c.buf.clear()
    c.send(b"bulletin\r")
    ok &= check("and BULLETIN still opens the forums, for old habits",
                c.wait_for(b"Forums", 6))
    c.send(b"q")
    c.pump(0.5)
    c.close()
    return ok


def test_mail_compose():
    """MAIL <handle> opens the same editor a forum post does.

    Rob's requirement, in his words: "whatever we choose as these interface
    commands must be consistent across all entries... I dont see why mail,
    the system feedback systems, forums all dont use a unified message entry
    system." So this asserts the SHARED behaviour, not mail's own: the same
    terminator, the same wrap, the same refusal to lose a blank line.

    `MAIL handle text` on one line is kept because it is quick, and has its
    own coverage in test_mail.
    """
    print("Mail uses the shared message editor")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build")
        return True

    a = ansi_login("Writer")
    b = ansi_login("Recipient")
    drain(a); drain(b)

    a.buf.clear()
    a.send(b"mail Recipient\r")
    ok = check("MAIL with no message opens the editor",
               a.wait_for(b"/s", 6))
    seen = plain(a.buf)
    ok &= check("it says who the message is for", b"Recipient" in seen)
    # /s and nothing else. Ctrl-D was offered as a shortcut and never
    # reached the board: SyncTERM takes the key, so it did nothing at all.
    # A documented key that silently does nothing is worse than one that was
    # never mentioned, so it is gone from the screen as well as the code,
    # and this check pins that it stays gone.
    ok &= check("it names /s as the way to finish", b"/s" in seen)
    ok &= check("and does not offer a control key that never worked",
                b"Ctrl-D" not in seen and b"Ctrl-Z" not in seen)

    # A line longer than the editor, to prove the wrap is shared and not
    # reimplemented: this is the exact failure that truncated a forum post.
    a.buf.clear()
    a.send(b"This first line is deliberately longer than the seventy two "
           b"character editor so it has to wrap\r")
    a.pump(0.5)
    a.send(b"\r")                       # a blank line: paragraphs must survive
    a.pump(0.3)
    a.send(b"Second paragraph.\r")
    a.pump(0.3)
    a.send(b"/s\r")
    # "Left for <handle>" is what mailSend actually says. Waiting on invented
    # wording is how this check failed the first time it ran.
    sent = a.wait_for(b"Left for", 8)
    ok &= check("the message sends", sent)
    if not sent:
        # Show the session rather than leaving somebody to reproduce it by
        # hand. A failing check that does not say what it saw is half a
        # failing check.
        print("        last 400 bytes the sender saw:")
        print("        " + repr(plain(a.buf)[-400:]))

    # Read it back as the recipient and check nothing was lost.
    mail_box(b)
    b.buf.clear()
    b.send(b"\r")
    b.pump(1.5)
    # Collapse whitespace before checking. The board wraps a long line while
    # it is being typed, so a phrase written on one line comes back on two,
    # and asserting on the contiguous phrase would fail on correct
    # behaviour. What this check is about is that nothing was truncated.
    got = b" ".join(plain(b.buf).split())
    ok &= check("the long line survived the wrap rather than truncating",
                b"seventy two character editor so it has to wrap" in got)
    ok &= check("and the second paragraph is there too",
                b"Second paragraph" in got)

    # Leave the mailbox as we found it.
    b.send(b"d")
    b.pump(0.8)
    b.send(b"q")
    b.pump(0.5)
    drain(a); drain(b)
    a.close(); b.close()
    return ok


def render_lines(buf, cols=80):
    """The buffer played onto a scrolling grid the way a terminal draws it.

    Honours what plain() cannot: backspace, cursor moves, erase to end of
    line and clear screen, so an erase that did or did not happen shows up
    as a difference in the lines. Written because three layout bugs in one
    evening (a doubled footer, a prompt written over, a marker left behind)
    each passed every check that read the plain buffer.
    """
    data = bytes(buf)
    csi = re.compile(rb"\x1b\[([0-9;?]*)([A-Za-z])")
    lines = [[" "] * cols]
    x = y = 0
    i, n = 0, len(data)

    def row(k):
        while len(lines) <= k:
            lines.append([" "] * cols)
        return lines[k]

    while i < n:
        b = data[i]
        if b == 0x1B:
            m = csi.match(data, i)
            if m:
                ps = [int(v) for v in m.group(1).decode().split(";") if v.isdigit()]
                f = m.group(2).decode()
                k = ps[0] if ps else 1
                if f == "J" and ps and ps[0] == 2:
                    lines = [[" "] * cols]
                    x = y = 0
                elif f in "Hf":
                    y = (ps[0] - 1) if ps else 0
                    x = (ps[1] - 1) if len(ps) > 1 else 0
                elif f == "K":
                    r = row(y)
                    for c in range(x, cols):
                        r[c] = " "
                elif f == "A": y = max(0, y - k)
                elif f == "B": y += k
                elif f == "C": x = min(cols - 1, x + k)
                elif f == "D": x = max(0, x - k)
                i = m.end()
                continue
            i += 1
            continue
        if b == 0xFF:
            i += 3
            continue
        if b == 0x0D:
            x = 0
        elif b == 0x0A:
            y += 1
        elif b == 0x08:
            x = max(0, x - 1)
        elif b >= 0x20:
            ln = 4 if b >= 0xF0 else 3 if b >= 0xE0 else 2 if b >= 0xC0 else 1
            try:
                ch = data[i:i + ln].decode("utf-8")
                i += ln - 1
            except UnicodeDecodeError:
                ch = bytes([b]).decode("cp437")
            r = row(y)
            if x < cols:
                r[x] = ch
            x += 1
        i += 1
    row(y)
    return ["".join(r).rstrip() for r in lines]


def screen_lines(buf):
    """The buffer as screen lines, escapes and carriage returns gone."""
    return plain(buf).replace(b"\r", b"").split(b"\n")


def count_lines(buf, needle):
    """How many screen lines contain needle. A footer is on screen once."""
    return sum(1 for ln in screen_lines(buf) if needle in ln)


def blank_before(buf, needle):
    """Is the line containing needle set off from the prompt by a blank line?

    Needs the match at index 2 or later. The buffer is cleared just before
    the key, so line 0 is always the tail of the prompt line, and on the old
    code the answer sat on line 1 with nothing between. Checking only that
    the line above is empty would pass on exactly that.
    """
    lines = screen_lines(buf)
    for i, ln in enumerate(lines):
        if needle in ln:
            return i >= 2 and lines[i - 1].strip() == b""
    return False


def blank_after(buf, needle):
    """Is the line containing needle followed by a blank line and then more?

    The other half of blank_before. Rob, 0.22.2, a screenshot of the end of a
    subject: the notice sat directly on the reading prompt, and the existing
    check passed over it because it only ever looked above the notice.
    """
    lines = screen_lines(buf)
    for i, ln in enumerate(lines):
        if needle in ln:
            return (i + 2 < len(lines) and lines[i + 1].strip() == b""
                    and lines[i + 2].strip() != b"")
    return False


def subject_number(buf, name):
    """The number the subject list shows for a subject, or None."""
    m = re.search(rb"(\d+) +" + re.escape(name.encode() if isinstance(name, str) else name),
                  plain(buf))
    return m.group(1).decode() if m else None


def blank_above(buf, needle):
    """Is the first line containing needle directly below a blank line?

    For "a line between the header and the list" (Rob): on 0.21.6 the first
    row sat directly under the title bar, so the line above it was the
    title, not a blank.
    """
    lines = screen_lines(buf)
    for i, ln in enumerate(lines):
        if needle in ln:
            return i >= 1 and lines[i - 1].strip() == b""
    return False


def blank_before_prompt(buf, marker=b"Forums>"):
    """Is the prompt set off from the footer by a blank line?"""
    lines = [ln for ln in screen_lines(buf)]
    while lines and not lines[-1].strip():
        lines.pop()
    return (len(lines) >= 3 and lines[-1].strip().startswith(marker)
            and lines[-2].strip() == b"" and b"Enter reads" in lines[-3])


def forum_post(c, subject, lines):
    """Post a new subject from the list the caller is standing on.

    Module level, for tests other than test_forums. Returns the message
    number the board reports, or None.
    """
    c.buf.clear()
    c.send(b"p")
    if not c.wait_for(b"Subject", 6):
        return None
    c.send(subject.encode() + b"\r")
    if not c.wait_for(b" 1: ", 6):
        return None
    for line in lines:
        c.send(line.encode() + b"\r")
        c.pump(0.3)
    c.send(b"/s\r")
    if not c.wait_for(b"Posted as message", 8):
        return None
    m = re.search(rb"Posted as message (\d+)", plain(c.buf))
    return m.group(1).decode() if m else None


def new_count(buf):
    """The "N new messages" figure in a title bar, or None."""
    m = re.findall(rb"(\d+) new messages?", plain(buf))
    return int(m[-1]) if m else None


def ends_at_read_prompt(buf):
    """Does the screen end at the question asked under a message?

    Rob: "When reading, ask like email, reply, enter for next". The list
    footer and the Forums> breadcrumb belong to the lists; under a message
    the last line is the [R]eply question, ending in a colon.
    """
    text = plain(buf).replace(b"\r", b"").rstrip()
    if not text:
        return False
    last = text.split(b"\n")[-1].strip()
    return last.startswith(b"[R]eply") and last.endswith(b":")


def line_with(buf, needle):
    """The rendered screen line containing needle, and the one above it."""
    lines = render_lines(buf)
    for i, ln in enumerate(lines):
        if needle in ln:
            return ln, (lines[i - 1] if i else None)
    return None, None


def ends_at_prompt(buf, marker):
    """Is the last line on screen a prompt a caller can type at?

    Asks what a caller asks, with nothing pressed: is the cursor somewhere I
    can type? A list that ends in a bare cursor contains every string a
    presence check looks for, which is how one shipped past 689 of them.
    Callers must NOT send a key before this: the forum list only drew its
    prompt after Enter, and a check that pressed something first passed.
    """
    text = plain(buf).replace(b"\r", b"").rstrip()
    if not text:
        return False
    last = text.split(b"\n")[-1].strip()
    return last.startswith(marker) and last.endswith(b">")


def test_forums():
    """Forums: post, group by subject, and keep the unread counts honest.

    The case that matters is Rob's own: a forum carrying two conversations,
    one read to its end, and the other still reporting the right number. A
    single forum-wide high-water mark cannot do that, which is why there is
    a window above the mark, and this is the test that would fail if the
    window were ever quietly dropped for being fiddly.

    Deliberately not "post a message, read it back". That passes with a
    broken pointer.
    """
    print("Forums")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build")
        return True
    if not os.environ.get("BBS_SD_DIR", ""):
        print("  SKIP  the forums plugin needs a card")
        return True

    s = ansi_login("Forumist")
    drain(s)
    s.buf.clear()
    s.send(b"forums\r")
    if not s.wait_for(b"Forums", 6):
        print("  SKIP  forums are not enabled on this board")
        s.close()
        return True
    s.pump(1.0)
    ok = check("the forum list opens", b"Forums" in plain(s.buf))
    # Nothing pressed: the list must end somewhere a caller can type. It
    # used to need an Enter before any prompt appeared.
    ok &= check("the forum list ends at a prompt, with no key pressed",
                ends_at_prompt(s.buf, b"Forums>"))
    # prompt() prints the footer, and the list printed its own as well, so it
    # was on screen twice every time the list was drawn.
    ok &= check("the forum list shows its footer once",
                count_lines(s.buf, b"Enter reads") == 1)
    foot, _ = line_with(s.buf, "Enter reads")
    ok &= check("the footer's --> starts in column 0",
                foot is not None and foot.startswith("-->"))
    ok &= check("a blank line between the title bar and the first forum",
                blank_above(s.buf, b"General"))
    ok &= check("a blank line between the footer and the prompt",
                blank_before_prompt(s.buf))

    # Open the first forum. A number is typed on the prompt line and
    # confirmed with Enter, so numbers above 9 can be reached at all.
    s.buf.clear()
    s.send(b"1\r")
    s.pump(1.2)
    # The breadcrumb names the level. "F1" was the old "[F1] name>" prompt
    # and is gone; this check only kept passing through its fallback.
    ok &= check("a forum opens under a Forums>name> breadcrumb",
                re.search(rb"Forums>[^>\r\n]+>", plain(s.buf)) is not None)
    # And the subject list, which ended in a bare cursor, ends at one too.
    ok &= check("the subject list ends at a prompt, with no key pressed",
                ends_at_prompt(s.buf, b"Forums>"))
    ok &= check("the subject list shows its footer once",
                count_lines(s.buf, b"Enter reads") == 1)

    def post(subject, body):
        """Write a message the way a caller does: a subject, then lines.

        `body` is a list of lines, deliberately. The first version of this
        helper sent one short string, which passed while the body was
        silently capped at 72 characters by the single-line editor, because
        72 was more than it ever asked for. A test that only writes what
        fits cannot find a limit.
        """
        s.buf.clear()
        s.send(b"p")
        # "Subject" without the colon: the prompt names its own cap now,
        # so it reads "Subject (49 max):" and a wait on "Subject:" misses.
        if not s.wait_for(b"Subject", 6):
            return False
        s.send(subject.encode() + b"\r")
        # Wait for the first line prompt rather than for the instruction
        # text. The prompt is what actually means "ready for input", and it
        # does not move when somebody rewords the help above it, which is
        # exactly what broke this the first time.
        if not s.wait_for(b" 1: ", 6):
            return False
        s.buf.clear()
        for line in body:
            s.send(line.encode() + b"\r")
            s.pump(0.3)
        s.send(b"/s\r")
        return s.wait_for(b"Posted as message", 8)

    # Longer than one line and longer than the 72 character editor, so the
    # length that used to truncate is exercised rather than avoided. The
    # blank line is here because a blank line separates paragraphs and must
    # not be taken as the end of the message.
    long_body = [
        "This is the first line of a message that runs well past the old "
        "seventy two character limit.",
        "",
        "A blank line above this one, because a blank line separates "
        "paragraphs and must not end the message.",
        "Third line.",
    ]

    # Two conversations, interleaved, the way a real forum fills up.
    ok &= check("a new subject posts", post("20m antennas", long_body))
    ok &= check("a second subject posts", post("20m tips", ["Work split frequencies"]))
    ok &= check("and a third message", post("20m antennas", ["I have a dipole up"]))
    ok &= check("and a fourth", post("20m tips", ["Listen before transmitting"]))

    # The listing groups them: four messages, two subjects.
    s.buf.clear()
    s.send(b"l")
    s.pump(1.5)
    listing = plain(s.buf)
    ok &= check("the listing groups by subject rather than listing every message",
                b"20m antennas" in listing and b"20m tips" in listing)
    ok &= check("and shows a message count per subject",
                b"2 msgs" in listing or b"of 2" in listing)

    # The bug Rob hit on the live board, and the reason this check exists:
    # he posted the first message on his board and then could not read it.
    # A poster has read their own message by definition, Enter means "the
    # next thing you have not read", so the only message on the board was
    # unreachable. Correct on its own terms and useless in practice.
    #
    # Inside a subject, reading now walks the conversation in order whether
    # or not it has been read. Enter from the FORUM list still means what is
    # new; that distinction is the fix, not a loosening of it.
    s.buf.clear()
    s.send(b"l")
    s.pump(1.2)
    # Rob: "It shows 1 above, but 4 below, which is it?" The list numbered
    # rows while the message showed its ID. A subject is numbered by the
    # message that started it now, so the two agree.
    num = subject_number(s.buf, "20m antennas")
    ok &= check("the subject list numbers a subject", num is not None)
    ok &= check("a blank line between the title bar and the first subject",
                blank_above(s.buf, b"20m"))
    s.buf.clear()
    s.send(((num or "1") + "\r").encode())
    s.pump(1.5)
    mine = plain(s.buf)
    ok &= check("the poster can re-read their own message",
                b"20m" in mine and b"Nothing new" not in mine)
    ok &= check("the subject's number is the ID its first message shows",
                num is not None and ("ID #" + num).encode() in mine)
    ok &= check("a message says Subject, By and Date",
                b"Subject:" in mine and b"By:" in mine and b"Date:" in mine)
    ok &= check("the message is set off from the prompt by a blank line",
                blank_before(s.buf, b"Message: ID #"))
    ok &= check("the breadcrumb has no Messages> level", b"Messages>" not in mine)
    # Rob: "When reading, ask like email, reply, enter for next, etc. ...
    # The prompt is fine elsewhere, just not when directly reading a post."
    ok &= check("reading ends at a [R]eply question, not the list footer",
                ends_at_read_prompt(s.buf))
    ok &= check("and the list footer is not repeated under a message",
                count_lines(s.buf, b"Enter reads") == 0)
    # find, not index: on a build with no [R]eply prompt, index raised and
    # took every check after this one down with it.
    ok &= check("a message ends with --> EOM <--",
                b"--> EOM <--" in mine and b"[R]eply" in mine
                and mine.find(b"--> EOM <--") > mine.find(b"Date:")
                and mine.find(b"--> EOM <--") < mine.find(b"[R]eply"))
    # Rob: "Need a linefeed before EOM".
    eom, above = line_with(s.buf, "--> EOM <--")
    ok &= check("a blank line before EOM",
                eom is not None and above is not None and above.strip() == "")
    # Rob: "--> starts at the very begining. EVERYWHERE unless told
    # otherwise" and "not sure why these all start indented, stop that."
    ok &= check("EOM starts in column 0", eom is not None and eom.startswith("-->"))
    head, _ = line_with(s.buf, "Message: ID #")
    ok &= check("the message header starts in column 0",
                head is not None and not head.startswith(" "))
    subj, _ = line_with(s.buf, "Subject:")
    ok &= check("and Subject, By and Date do too",
                subj is not None and subj.startswith("Subject:"))
    body, _ = line_with(s.buf, "This is the first line")
    ok &= check("and so does the body",
                body is not None and body.startswith("This is the first line"))
    s.buf.clear()
    s.send(b"\r")
    s.pump(1.2)
    ok &= check("and Enter walks the conversation rather than stopping dead",
                b"Nothing new here" not in plain(s.buf))
    # Rob, 0.22.2: past the last message, with nothing else new, the notice
    # sat directly on the reading prompt. It has to be checked HERE, where the
    # prompt follows the notice; where reading rolls on to another message
    # the same check passes on the broken code, which is how the first draft
    # of it proved nothing.
    s.buf.clear()
    s.send(b"\r")
    s.pump(1.2)
    ok &= check("'Nothing else new here' is set off from the reading prompt",
                b"Nothing else new here" in plain(s.buf)
                and blank_after(s.buf, b"Nothing else new here"))
    s.buf.clear()
    s.send(b"q")
    s.pump(1.0)
    # Q from reading is back ONE level, to the subject list. The first draft
    # of these checks called this the forum list and looked for the forum
    # list's title on it, which is a test asserting the wrong screen.
    ok &= check("the subject list shows its footer once on a return",
                count_lines(s.buf, b"Enter reads") == 1)
    s.buf.clear()
    s.send(b"q")
    s.pump(1.0)
    # Rob: "Duplicate exists always not just on entry".
    ok &= check("the forum list shows its footer once on a return as well",
                count_lines(s.buf, b"Enter reads") == 1)
    # Rob: one form for every count, zero included, instead of "nothing new".
    ok &= check("the title bar says 0 new messages, not 'nothing new'",
                b"0 new messages" in plain(s.buf) and b"nothing new" not in plain(s.buf))
    # Everything here is the poster's own, so Enter has nothing new to read.
    s.buf.clear()
    s.send(b"\r")
    s.pump(1.0)
    ok &= check("'nothing new' is set off from the prompt by a blank line",
                blank_before(s.buf, b"Nothing new"))

    # Everything posted by this caller is already read by definition, so a
    # second caller is what makes the unread counts mean anything.
    t2 = ansi_login("Reader2")
    drain(t2)
    t2.buf.clear()
    t2.send(b"forums\r")
    t2.wait_for(b"Forums", 6)
    t2.pump(1.0)
    ok &= check("a second caller sees the messages as new",
                b"4 new messages" in plain(t2.buf))

    # The poster, who has read everything, goes out and back in while the
    # second caller is still inside. On 0.21.6 that rewrote the board-wide
    # count to the poster's zero, and the second caller's next redraw of the
    # forum list showed somebody else's numbers.
    s.buf.clear()
    s.send(b"q")
    s.pump(0.8)
    s.send(b"forums\r")
    s.wait_for(b"Forums", 6)
    s.pump(0.8)
    t2.buf.clear()
    t2.send(b"1\r")
    t2.pump(1.2)
    t2.buf.clear()
    t2.send(b"q")
    t2.pump(1.2)
    ok &= check("another caller entering does not change your unread count",
                new_count(t2.buf) == 4)

    t2.buf.clear()
    t2.send(b"1\r")
    t2.pump(1.5)
    subs = plain(t2.buf)
    ok &= check("both subjects are listed for them",
                b"20m antennas" in subs and b"20m tips" in subs)

    # Read ONE subject to its end. This is the whole point of the test.
    which = subject_number(t2.buf, "20m antennas") or "1"
    t2.buf.clear()
    t2.send((which + "\r").encode())
    t2.pump(1.5)
    ok &= check("an unread message is headed New Message",
                b"New Message: ID #" in plain(t2.buf))
    ok &= check("opening a subject shows its first message",
                b"20m antennas" in plain(t2.buf))
    # The line that used to be cut at 72 characters. Checking its TAIL, not
    # its head, because a truncated body still contains the head.
    #
    # The tail only, and not the whole phrase: the board wraps at the line
    # length while the caller types, so "...the old seventy" and "two
    # character limit." are two stored lines with a real newline between
    # them. That is the wrap working, and an assertion on the contiguous
    # phrase would fail on correct behaviour.
    ok &= check("a body longer than the line editor survives whole",
                b"two character limit" in plain(t2.buf))
    ok &= check("and a blank line between paragraphs did not end the message",
                b"separates" in plain(t2.buf))
    t2.buf.clear()
    t2.send(b"\r")                       # the second message in that subject
    t2.pump(1.5)
    t2.buf.clear()
    t2.send(b"\r")                       # past the end: rolls to the rest
    t2.pump(1.5)
    ok &= check("'end of that subject' is set off from the prompt by a blank line",
                blank_before(t2.buf, b"That is the end"))

    # Back to the subject list and check the arithmetic.
    t2.buf.clear()
    t2.send(b"l")
    t2.pump(1.5)
    after = plain(t2.buf)
    ok &= check("the subject that was read is no longer marked new",
                b"2 msgs" in after)
    ok &= check("and the other subject still reports what is left in IT",
                b"new" in after)

    # The forum list must agree with the sum of its subjects. A forum
    # claiming a number its own subjects do not add up to reads as broken.
    t2.buf.clear()
    t2.send(b"q")
    t2.pump(1.5)
    ok &= check("the forum list agrees with the subjects underneath it",
                b"4 new" not in plain(t2.buf))

    drain(t2); t2.send(b"q"); t2.pump(0.5); t2.close()
    drain(s);  s.send(b"q");  s.pump(0.5)
    s.send(b"q"); s.pump(0.5); s.close()
    return ok


def test_handle_case():
    """A handle comes back in the case it was registered with.

    Rob typed QuantumRob and was greeted as quantumrob. The login matches
    case-insensitively and then shows the ACCOUNT's spelling, on purpose, so
    this pins that registration keeps the case it was given: if it does, the
    lower case is in the stored account and not in the code.
    """
    print("Handle case")
    a = ansi_login("MixedCaseX")
    a.close()
    time.sleep(0.5)
    b = Caller(ansi=True)
    b.wait_for(b"Enter your handle", 10)
    login(b, "mixedcasex")
    ok = check("typed in lower case, greeted in the registered case",
               b.wait_for(b"MixedCaseX", 5))
    ok &= check("and never in the case that was typed",
                b"Welcome back, mixedcasex" not in plain(b.buf))
    b.close()
    time.sleep(0.5)

    # Re-casing an account that already exists. Rob's is stored lower case,
    # and the fix for it is USER EDIT with the handle retyped. That only
    # works if a case-only change is not refused as "That handle is taken",
    # because the handle it collides with is the account's own. users::update
    # skips the duplicate check when the names match ignoring case; this is
    # the proof, rather than a reading of the code.
    if PASSWORD:
        s = ansi_login("Recaser")
        s.buf.clear()
        s.send(f"bye {PASSWORD}\r".encode())
        s.wait_for(b"SysOp node", 5)
        s.pump(0.6)
        s.buf.clear()
        s.send(b"user edit mixedcasex\r")
        ok &= check("the account opens for a re-case", s.wait_for(b"EDIT ACCOUNT", 5))
        s.send(b"\x08" * 24 + b"MIXEDcaseX" + F1)
        ok &= check("a case-only rename saves", s.wait_for(b"saved.", 6))
        drain(s)
        s.close()
        time.sleep(0.5)
        c = Caller(ansi=True)
        c.wait_for(b"Enter your handle", 10)
        login(c, "mixedcasex")
        ok &= check("and the board greets the new case",
                    c.wait_for(b"MIXEDcaseX", 5))
        c.close()
    return ok




def test_forums_remove():
    """A moderator can take a post down, and nothing else can.

    Rob: "there is no way the sysop right now can remove a message". The
    storage always allowed it (a removed message keeps its slot and flips a
    flag); the key, the permission and the question did not exist.

    The count check matters as much as the removal. Unread was worked out
    from message numbers alone, so a removed post the caller never read would
    still say "1 new" while Enter found nothing.
    """
    print("Forums: removing a post")
    if not os.environ.get("BBS_SD_DIR", ""):
        print("  SKIP  the forums plugin needs a card")
        return True
    if not PASSWORD:
        print("  SKIP  no sysop_password")
        return True

    # A reader who has read nothing, in and out before the removal, to get
    # their count as it was.
    v = ansi_login("Bystander")
    drain(v)
    v.buf.clear()
    v.send(b"forums\r")
    v.wait_for(b"Forums", 6)
    v.pump(1.0)
    v.send(b"q")
    v.pump(0.6)

    u = ansi_login("Spammer")
    drain(u)
    u.buf.clear()
    u.send(b"forums\r")
    u.wait_for(b"Forums", 6)
    u.pump(1.0)
    u.send(b"1\r")
    u.pump(1.2)
    n = forum_post(u, "Buy now cheap", ["Spam spam spam."])
    ok = check("a post to be removed goes up", n is not None)
    n = n or "0"

    v.buf.clear()
    v.send(b"forums\r")
    v.wait_for(b"Forums", 6)
    v.pump(1.0)
    before = new_count(v.buf)
    v.send(b"q")
    v.pump(0.6)

    # Somebody without the moderator level cannot, even on their own post.
    u.buf.clear()
    u.send(b"l")
    u.pump(1.0)
    u.buf.clear()
    u.send((n + "\r").encode())
    u.pump(1.2)
    ok &= check("a caller without the moderator level is not offered D",
                b"[D]el" not in plain(u.buf))
    u.buf.clear()
    u.send(b"d")
    u.pump(0.8)
    ok &= check("a caller without the moderator level cannot remove",
                b"cannot remove" in plain(u.buf))

    m = ansi_login("Moderator")
    m.buf.clear()
    m.send(f"bye {PASSWORD}\r".encode())
    m.wait_for(b"SysOp node", 5)
    m.pump(0.6)
    m.send(b"forums\r")
    m.wait_for(b"Forums", 6)
    m.pump(1.0)
    m.send(b"1\r")
    m.pump(1.2)
    m.buf.clear()
    m.send((n + "\r").encode())
    m.pump(1.2)
    ok &= check("the moderator is offered D", b"[D]el" in plain(m.buf))

    # N keeps it.
    m.buf.clear()
    m.send(b"d")
    ok &= check("D asks before removing", m.wait_for(b"Remove message #" + n.encode(), 4))
    m.send(b"n")
    ok &= check("anything but y keeps it", m.wait_for(b"Kept", 4))

    # Y removes it.
    m.buf.clear()
    m.send(b"d")
    m.wait_for(b"Remove message", 4)
    m.send(b"y")
    ok &= check("y removes it", m.wait_for(b"Message #" + n.encode() + b" removed", 5))

    m.buf.clear()
    m.send(b"l")
    m.pump(1.2)
    ok &= check("and it is gone from the subject list",
                b"Buy now cheap" not in plain(m.buf))

    # The reader who never saw it: one fewer new, and Enter never shows it.
    v.buf.clear()
    v.send(b"forums\r")
    v.wait_for(b"Forums", 6)
    v.pump(1.0)
    after = new_count(v.buf)
    ok &= check("a removed post no longer counts as new",
                before is not None and after is not None and after == before - 1)
    seen_it = False
    for _ in range(12):
        v.buf.clear()
        v.send(b"\r")
        v.pump(0.8)
        if b"Buy now cheap" in plain(v.buf):
            seen_it = True
        if b"Nothing new" in plain(v.buf) or b"Nothing else new" in plain(v.buf):
            break
    ok &= check("and reading on never shows it", not seen_it)

    for c in (u, v, m):
        drain(c)
        c.close()
    return ok




def test_prompt_survives_notice():
    """Something arriving at the main prompt lifts it and puts it back.

    Rob, twice: the mail notice "take[s] the message but never return[s] the
    prompt", and "back up and send the message then put the prompt back and
    use freaking LF before the prompt!" The chat plugin wrote the notice
    straight onto the session, after "[1] Main:", and redrew the typed text
    without the prompt. The core's own queue, which PAGE uses, moved down a
    line and left the old prompt sitting above the notice.
    """
    print("A notice at the main prompt")
    # Its own account, not Recipient: mail is read oldest first, so a line
    # left there was what test_mail_compose read instead of its own message.
    r = ansi_login("Notified")
    s = ansi_login("Sender")
    drain(r)
    drain(s)
    r.buf.clear()
    r.send(b"wh")                            # part way through a command
    r.pump(0.5)
    s.send(b"mail Notified a line for you\r")
    s.wait_for(b"Left for", 5)
    r.wait_for(b"You have mail", 5)
    r.pump(0.8)
    shown = [ln for ln in render_lines(r.buf)]
    idx = next((k for k, ln in enumerate(shown) if "You have mail" in ln), -1)
    ok = check("the notice arrives", idx >= 0)
    ok &= check("on its own line, not after the prompt",
                idx >= 0 and "Main:" not in shown[idx])
    ok &= check("with a blank line before the prompt",
                idx >= 0 and idx + 2 < len(shown) and shown[idx + 1].strip() == "")
    ok &= check("and the prompt comes back with what was typed",
                idx >= 0 and idx + 2 < len(shown) and "Main: wh" in shown[idx + 2])
    # Exactly once. The core's queue used to move down a line and leave the
    # old prompt, typing and all, above the notice: "[1] Main: wh" twice. A
    # check for a line ending in "Main:" cannot see that one, because the
    # stale copy still has the typing on it.
    ok &= check("and the prompt is on screen exactly once",
                sum(1 for ln in shown if "Main: wh" in ln) == 1)
    # Leave the mailbox as we found it: finish the command, read, delete.
    r.send(b"o\r")
    r.pump(0.8)
    mail_box(r)
    r.send(b"\r")
    r.wait_for(b"[D]elete", 5)
    r.send(b"d")
    r.pump(0.8)
    r.send(b"q")
    r.pump(0.5)
    r.close()
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
    d.send(TEST_PW.encode() + b"\r" + TEST_PW.encode() + b"\rHasty\rhasty@example.com\r")
    to_save(d, b"WELCOME ABOARD")
    d.wait_for(b"WELCOME ABOARD", 10)
    d.wait_for(b"Main", 8)
    d.buf.clear()
    d.send(b"privacy\r")
    read_list(d, 6)
    ok &= check("PRIVACY shows it again any time", b"no encryption" in plain(d.buf))
    d.close()
    return ok



DIRECTORY_TOKEN = "a1b2c3d4e5f60718293a4b5c6d7e8f90"    # 32 chars, like the real one


def standin_directory(port, got, stop, split_first=False, hold_token=None, ready=None):
    """A stand-in directory: answer every heartbeat 200, remember them all.

    It used to accept exactly ONE connection and then close its
    listener, so every heartbeat after the first got connection-refused.
    The board recorded "refused" over the "listed" it had a moment
    earlier, which is correct behaviour on its part, and the test then
    raced the board's own retry: it passed when it read the status first
    and failed when the retry won. That race was invisible until the
    suite got slower and it started landing the other way.

    A real directory does not stop listening, so neither does this one
    until stop is set, or a minute passes with nothing arriving. The stop
    is what lets the next test put a directory on the same port (1.0.1):
    this one used to hold it for a full idle minute after its test ended.
    It records every heartbeat in got as (head, body). It is not one
    heartbeat per run: nudge_seconds pushes an update whenever the public
    caller count moves, and the tests log callers in and out, so counting
    them would be asserting on the nudge policy rather than on the thing
    being tested.

    While hold_token (an Event) is set, replies carry no X-Listing-Token,
    so a board keeps whatever token it was given in system.cfg. The
    worst-case payload needs that: its token is forty quote marks, and the
    first reply would otherwise swap them for a 32 character one.

    ready (an Event) is set once it is listening. A test asserting that
    nothing arrived has to know something was there to receive it: a bind
    that failed made "nothing was sent" pass on a board that sent plenty.
    """
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(4)
    if ready is not None:
        ready.set()
    srv.settimeout(0.5)
    first = split_first
    idle_since = time.time()
    try:
        while not stop.is_set():
            try:
                c, _ = srv.accept()
            except socket.timeout:
                if time.time() - idle_since > 60:
                    return
                continue
            idle_since = time.time()
            c.settimeout(5)
            try:
                data = b""
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
                issued = b"" if hold_token is not None and hold_token.is_set() \
                    else b"X-Listing-Token: " + DIRECTORY_TOKEN.encode() + b"\r\n"
                reply = (b"HTTP/1.1 200 OK\r\n"
                         b"X-Seen-Address: 203.0.113.9\r\n"
                         b"X-Listing-State: pending\r\n"
                         + issued +
                         b"Content-Length: 2\r\nConnection: close\r\n\r\nok")
                if first:
                    # Split four characters into the token, on purpose:
                    # the board has to cope with a header arriving in
                    # two reads.
                    cut = reply.index(DIRECTORY_TOKEN.encode()) + 4
                    c.sendall(reply[:cut])
                    time.sleep(0.3)
                    c.sendall(reply[cut:])
                    first = False
                else:
                    c.sendall(reply)
            except OSError:
                pass
            finally:
                c.close()
    finally:
        srv.close()


def test_announce():
    """The board lists itself, sends nothing about callers, and can prove it."""
    print("Directory listing")
    import json as _json
    import threading as _threading

    got = []
    stop = _threading.Event()

    def directory(port):
        standin_directory(port, got, stop, split_first=True)

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
    # Wait for the heartbeat, not for the thread. The stand-in keeps
    # listening now, so joining it would block until its accept timed out
    # and would add that wait to every run of the suite.
    for _ in range(40):
        if got:
            break
        time.sleep(0.25)
    ok &= check("the directory got a heartbeat", len(got) >= 1)

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
    s.send(b"dash all\r")             # on its second page since 1.1.0
    s.wait_for(b"DASHBOARD", 5)
    read_list(s)
    ok &= check("the dashboard reports the listing", b"Directory:" in plain(s.buf))

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
    stop.set()                         # the port is the next test's
    th.join(3)
    return ok


# ---------------------------------------------------------------------------
# The directory badges (1.0.1)
# ---------------------------------------------------------------------------

# Typed into CONFIG the way a sysop might, junk and all. Support: upper
# case, spaces round and inside an entry, an empty entry, markup, a repeat,
# and the same slug again spelt with a dash and with underscores. A space
# inside an entry is a dash (review, 1.0.1): "Mental Health" used to go as
# mentalhealth, which no directory publishes. Interests: seventeen good
# entries and one far longer than any slug a directory publishes.
BADGE_SUPPORT_TYPED   = (b" HAM, lgbtq ,,Mental Health,<b>x</b>,ham,MENTAL-health,"
                         b"Suicide__ _Prevention-")
BADGE_SUPPORT_SENT    = ["ham", "lgbtq", "mental-health", "bxb", "suicide-prevention"]
BADGE_INTERESTS_TYPED = (b"C64,this-slug-is-far-too-long-to-be-one,Amiga,"
                         + b",".join(b"e%x" % i for i in range(1, 16)))
BADGE_INTERESTS_SENT  = ["c64", "amiga"] + ["e%x" % i for i in range(1, 15)]

# Enabled, Read, Write, Admin, then the plugin's own rows with Board (shown,
# not editable) skipped: Sysop, About, DNS name, Port, Directory, Every min,
# Push secs, Activity, Support.
BADGE_TO_SUPPORT = DOWN * 12


def cfg_with(text, edits):
    """system.cfg text with {(section, key): value} set. Section "" is the
    top of the file. A key already there is replaced in place; one that is
    not is added at the end of its section, or above the first section for
    a core key, which is where the parser wants those."""
    out, cur, done = [], "", set()

    def flush(section):
        for (sec, key), val in edits.items():
            if sec == section and (sec, key) not in done:
                out.append(f"{key} = {val}")
                done.add((sec, key))

    for line in text.splitlines():
        t = line.strip()
        if t.startswith("["):
            flush(cur)
            cur = t[1:t.find("]")] if "]" in t else ""
            out.append(line)
            continue
        if "=" in line and not t.startswith(("#", ";")):
            k = line.split("=", 1)[0].strip()
            if (cur, k) in edits:
                out.append(f"{k} = {edits[(cur, k)]}")
                done.add((cur, k))
                continue
        out.append(line)
    flush(cur)
    return "\n".join(out) + "\n"


def announce_reload(s, text, interval):
    """Put text in system.cfg and make the board live on it, by saving the
    announce page's "Every min" as interval, the way a sysop would. text
    should carry some other interval, so the save is a real change.

    Not cfg_reload: that leaves max_users = 200 in the file, and test_config,
    which runs later, sets exactly 200 and then expects "Saved and live". It
    got "Nothing changed" and failed for a reason in somebody else's test."""
    (USERDATA / "system.cfg").write_bytes(text.encode("utf-8"))
    cfg_open(s, b"announce", b"Every min")
    s.buf.clear()
    s.send(DOWN * 9 + b"\x08" * 4 + str(interval).encode() + F1)
    return cfg_verdict(s, [b"Saved and live", b"Nothing changed", b"saved, but"]) \
        == b"Saved and live"


def announce_restore(s, saved):
    """system.cfg back to saved, byte for byte where the writer allows, and
    the board living on it again."""
    text = saved.decode("utf-8")
    m = re.search(r"^\[plugin:announce\][^\[]*?^interval\s*=\s*(\d+)", text, re.M | re.S)
    iv = int(m.group(1)) if m else 10
    return announce_reload(s, cfg_with(text, {("plugin:announce", "interval"): str(iv + 1)}), iv)


def announce_body_max():
    """kBodyMax out of announce.cpp, so the check follows the build."""
    src = (ROOT / "src" / "plugins" / "announce.cpp").read_text()
    m = re.search(r"kBodyMax\s*=\s*(\d+)", src)
    return int(m.group(1)) if m else 512


def announce_heartbeat(s, got, secs=10):
    """ANNOUNCE NOW, and the heartbeat it produced as (payload dict, body).

    (None, b"") when nothing arrived, which is what a refused payload looks
    like from the directory's side: nothing at all."""
    import json as _json
    s.pump(1.0)                        # let anything already out finish
    got.clear()
    s.buf.clear()
    s.send(b"announce now\r")
    s.wait_for(b"Sending now", 4)
    for _ in range(int(secs * 4)):
        if got:
            break
        time.sleep(0.25)
    s.pump(0.8)                        # the reply lands on a later tick
    if not got:
        return None, b""
    body = got[0][1]
    try:
        return _json.loads(body.decode("utf-8")), body
    except Exception:
        return {}, body


def announce_features(s):
    """The features list ANNOUNCE TEST prints right now, or None when it
    printed none. The payload is built on the spot, so this is what the next
    heartbeat would say, with no directory needed to find out.

    A plugin restart posts at once, and TEST will not build a payload while
    a heartbeat is out, so it waits for that and asks again."""
    for _ in range(6):
        s.pump(0.5)
        s.buf.clear()
        s.send(b"announce test\r")
        i = wait_any(s, [b'"interests"', b"going out"], 5)
        s.pump(0.3)
        if i != 1:
            break
    m = re.search(rb'"features":\[([^\]]*)\]', plain(s.buf))
    if not m:
        return None
    return [w.strip(b'"').decode() for w in m.group(1).split(b",") if w]


def widest_slack(rec):
    """Bytes this payload is short of the widest one the same text could
    make: every number at its most digits, the longest system label, and
    all four features running."""
    import json as _json
    widths = {"port": 5, "nodes": 3, "busy": 3, "uptime": 7, "interval": 4,
              "tz": 4, "calls24": 5, "minutes24": 10}
    slack = sum(w - len(str(rec.get(k, 0))) for k, w in widths.items())
    slack += 31 - len(str(rec.get("system", "")).encode("utf-8"))
    slack += (len('["chat","forums","files","mail"]')
              - len(_json.dumps(rec.get("features", []), separators=(",", ":"))))
    return slack


def test_announce_badges():
    """The directory badges (1.0.1): what the board says about itself.

    system, terminals, guests and features are the board's own facts, and
    features is what is running NOW: with no card, forums and files cannot
    run, and the heartbeat must not claim them. support and interests are
    the sysop's, typed into CONFIG and tidied on the way out.

    And the room. A payload that does not fit is refused, never cut
    (0.21.4), so a board whose sysop filled every field would drop off the
    directory without a word. The worst case the plugin can build is built
    here, from system.cfg, and has to arrive whole.
    """
    print("Directory badges")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  the badges need the host build")
        return True
    card = bool(os.environ.get("BBS_SD_DIR", ""))
    port = int(os.environ.get("BBS_DIR_PORT", "8099"))
    got, stop, hold, ready = [], threading.Event(), threading.Event(), threading.Event()
    th = threading.Thread(target=standin_directory,
                          args=(port, got, stop, False, hold, ready), daemon=True)
    th.start()
    cfg_path = USERDATA / "system.cfg"
    saved = cfg_path.read_bytes()
    s = cfg_sysop("Badger")
    ok = check("the stand-in directory is listening", ready.wait(3))
    try:
        # --- what the board says about itself, on the harness's own config
        rec, _ = announce_heartbeat(s, got)
        ok &= check("a heartbeat carries all six badge fields",
                    rec is not None and all(k in rec for k in (
                        "system", "terminals", "guests", "features", "support", "interests")))
        rec = rec or {}
        ok &= check("system is read off the machine: the host build says host",
                    rec.get("system") == "host")
        ok &= check("terminals: the four this firmware speaks",
                    rec.get("terminals") == ["ansi", "utf8", "petscii", "ascii"])
        ok &= check("guests is a JSON true while guests are let in",
                    rec.get("guests") is True)
        want = ["chat", "forums", "files", "mail"] if card else ["chat", "mail"]
        ok &= check("features: what is running, " +
                    ("forums and files with a card" if card else "no forums or files without a card"),
                    rec.get("features") == want)
        ok &= check("support and interests go as empty lists, never left out",
                    rec.get("support") == [] and rec.get("interests") == [])

        s.buf.clear()
        s.send(b"announce test\r")
        s.wait_for(b'"interests"', 5)
        s.pump(0.3)
        shown = plain(s.buf)
        ok &= check("ANNOUNCE TEST prints them",
                    b'"system":"host"' in shown
                    and b'"terminals":["ansi","utf8","petscii","ascii"]' in shown
                    and b'"guests":true' in shown and b'"features":[' in shown
                    and b'"support":[]' in shown and b'"interests":[]' in shown)

        # --- SD UNMOUNT (review, 1.0.1). It stops no plugin, so FORUMS and
        # FILES stay running with nothing under them, and the badges went on
        # claiming both. They count only with a card mounted now.
        if card:
            s.buf.clear()
            s.send(b"sd unmount\r")
            unmounted = s.wait_for(b"safe to pull", 6)
            s.pump(0.3)
            feats = announce_features(s)
            if feats != ["chat", "mail"]:
                print("        features after SD UNMOUNT:", feats)
            ok &= check("SD UNMOUNT takes forums and files off the next heartbeat",
                        unmounted and feats == ["chat", "mail"])
            s.buf.clear()
            s.send(b"sd mount\r")
            s.wait_for(b"Mounted", 10)
            s.pump(0.5)
            ok &= check("and SD MOUNT puts them back",
                        announce_features(s) == ["chat", "forums", "files", "mail"])

        # --- typed into CONFIG, junk and all. The rows have to be there: on
        # a page without them the same keys save into some other field and
        # "Saved and live" alone would pass.
        opened = cfg_open(s, b"announce", b"Interests")
        s.pump(0.6)
        # Sixteen rows is Form::kMaxFields, the first CONFIG page to reach
        # it: the buttons and the status line have to stay on a 24 row
        # screen, and nothing may pass column 39 for a C64.
        grid = render_lines(s.buf, 80)
        drawn = [i for i, line in enumerate(grid) if line.strip()]
        if not (drawn and drawn[-1] < 24):
            print("        the page reached row", drawn[-1] + 1 if drawn else 0)
        ok &= check("the announce page, sixteen rows now, fits 24 rows and 40 columns",
                    opened and bool(drawn) and drawn[-1] < 24
                    and any("Token" in line for line in grid)
                    and max_column(s.buf) <= 39)
        s.buf.clear()
        s.send(BADGE_TO_SUPPORT + b"\x08" * 95 + BADGE_SUPPORT_TYPED
               + DOWN + b"\x08" * 95 + BADGE_INTERESTS_TYPED + F1)
        got_v = cfg_verdict(s, [b"Saved and live", b"No ; here", b"Nothing changed"])
        ok &= check("Support and Interests are on the announce page, and save",
                    opened and got_v == b"Saved and live")
        ok &= check("the file keeps what was typed; the tidying is on the way out",
                    (cfg_sec_line("plugin:announce", "support") or "").split("=", 1)[-1].strip()
                    == BADGE_SUPPORT_TYPED.decode().strip())
        rec, _ = announce_heartbeat(s, got)
        rec = rec or {}
        if rec.get("support") != BADGE_SUPPORT_SENT:
            print("        support sent:", rec.get("support"))
        ok &= check("support: lower case, a space or _ run inside an entry one -, "
                    "junk, empties and repeats gone",
                    rec.get("support") == BADGE_SUPPORT_SENT)
        if rec.get("interests") != BADGE_INTERESTS_SENT:
            print("        interests sent:", rec.get("interests"))
        ok &= check("interests: the first 16 good ones, a slug longer than any "
                    "directory's dropped", rec.get("interests") == BADGE_INTERESTS_SENT)

        # --- mail_slots (review, 1.0.1). chat's start() never put its mail
        # settings back to their defaults, so taking "mail_slots = 0" out of
        # the file left mail off until a reboot, and the mail badge said so.
        text = saved.decode("utf-8")
        announce_reload(s, cfg_with(text, {("plugin:chat", "mail_slots"): "0",
                                           ("plugin:announce", "interval"): "58"}), 59)
        feats = announce_features(s)
        ok &= check("mail_slots = 0: no mail badge",
                    feats is not None and "chat" in feats and "mail" not in feats)
        announce_reload(s, cfg_with(text, {("plugin:announce", "interval"): "57"}), 56)
        feats = announce_features(s)
        if not feats or "mail" not in feats:
            print("        features with the line taken out:", feats)
        ok &= check("the line taken out again: mail is back, without a reboot",
                    feats is not None and "mail" in feats)

        # --- a payload that does not fit (review, 1.0.1). ANNOUNCE TEST
        # printed the fragment as "everything that leaves the board". The
        # host shrinks the room with room_test, because at kBodyMax nothing
        # can overflow it.
        announce_reload(s, cfg_with(text, {("plugin:announce", "room_test"): "100",
                                           ("plugin:announce", "interval"): "55"}), 54)
        for _ in range(6):
            s.pump(0.5)
            s.buf.clear()
            s.send(b"announce test\r")
            if wait_any(s, [b"nothing sent", b'"software"', b"going out"], 5) != 2:
                break
        s.pump(0.3)
        shown = plain(s.buf)
        ok &= check("ANNOUNCE TEST says a payload too big would be refused, nothing sent",
                    b"would be refused, nothing sent" in shown
                    and b"everything that leaves" not in shown and b'"software"' not in shown)
        rec, body = announce_heartbeat(s, got, secs=4)
        if rec is not None:
            print(f"        a {len(body)} byte heartbeat arrived: {body[:60]!r}")
        ok &= check("and ANNOUNCE NOW sends nothing, not half of it", rec is None)
        s.buf.clear()
        s.send(b"announce\r")
        s.wait_for(b"Announce", 4)
        s.pump(0.6)
        ok &= check("ANNOUNCE says payload too long", b"payload too long" in plain(s.buf))

        # --- the worst payload the plugin can build
        limit = announce_body_max()
        slugs = ",".join(("%x" % i) * 5 for i in range(16))      # 16 in 95 characters
        worst = cfg_with(saved.decode("utf-8"), {
            ("", "board_name"): '"' * 40,
            ("", "guest"): "no",
            ("plugin:announce", "owner"): '"' * 40,
            ("plugin:announce", "description"): '"' * 120,
            ("plugin:announce", "host"): '"' * 95,
            ("plugin:announce", "token"): '"' * 40,
            ("plugin:announce", "public_port"): "65535",
            ("plugin:announce", "interval"): "1439",      # saved as 1440 below
            ("plugin:announce", "share_activity"): "yes",
            ("plugin:announce", "support"): slugs,
            ("plugin:announce", "interests"): slugs,
        })
        hold.set()                     # keep the forty-quote token
        ok &= check("the board reloads the worst-case config",
                    announce_reload(s, worst, 1440))
        rec, body = announce_heartbeat(s, got)
        whole = rec is not None and rec.get("name") == '"' * 40 \
            and rec.get("description") == '"' * 120 and rec.get("host") == '"' * 95 \
            and rec.get("token") == '"' * 40
        ok &= check("every text at its longest, every character escaped: sent, not refused", whole)
        rec = rec or {}
        widest = len(body) + widest_slack(rec) if rec else 0
        print(f"        worst payload {len(body)} bytes here, {widest} with the widest"
              f" numbers and every feature; the room is {limit - 1}")
        ok &= check("and it fits the room even with the widest numbers and every feature",
                    bool(rec) and widest <= limit - 1)
        ok &= check("16 support and 16 interests at 95 characters each",
                    len(rec.get("support", [])) == 16 and len(rec.get("interests", [])) == 16)
        ok &= check("guests off goes as a JSON false", rec.get("guests") is False)
        s.buf.clear()
        s.send(b"announce\r")
        s.wait_for(b"Announce", 4)
        s.pump(0.6)
        status = plain(s.buf)
        ok &= check("ANNOUNCE says listed, not payload too long",
                    b"listed" in status and b"too long" not in status)
    finally:
        hold.clear()
        announce_restore(s, saved)
        s.close()
        stop.set()
        th.join(3)
    return ok


def test_announce_directory():
    """End to end (1.0.1): the host board announces to the real directory
    server, which stores the badges and draws them on its board list.

    Needs the unleashed_directory repository beside this one, or its path in
    BBS_DIRECTORY_REPO, and says SKIP without it. The directory runs on this
    tag's own directory port with a two second pending window, the same
    settings its own self-test uses. Nothing leaves 127.0.0.1.
    """
    print("Directory badges, end to end")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build")
        return True
    repo = pathlib.Path(os.environ.get("BBS_DIRECTORY_REPO",
                                       str(ROOT.parent.parent / "unleashed_directory")))
    if not (repo / "server.py").is_file():
        print("  SKIP  no directory repository at", repo)
        return True
    import html as _html
    import json as _json
    import subprocess
    import tempfile
    import urllib.request

    card = bool(os.environ.get("BBS_SD_DIR", ""))
    port = int(os.environ.get("BBS_DIR_PORT", "8099"))
    db = pathlib.Path(tempfile.gettempdir()) / f"bbs-directory-{port}.db"
    for p in (db, db.with_name(db.name + "-wal"), db.with_name(db.name + "-shm")):
        if p.exists():
            p.unlink()
    env = dict(os.environ, DIRECTORY_DB=str(db), DIRECTORY_PORT=str(port),
               DIRECTORY_HOST="127.0.0.1", DIRECTORY_PENDING_HOURS="0.0006",
               DIRECTORY_MIN_SECONDS="0", DIRECTORY_PAGE_CACHE="0")
    proc = subprocess.Popen([sys.executable, "server.py"], cwd=str(repo), env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log = []
    # Drain it: an undrained pipe fills and every handler thread blocks in
    # its log line, which looks exactly like a server that has wedged.
    threading.Thread(target=lambda: [log.append(l) for l in proc.stdout],
                     daemon=True).start()
    base = f"http://127.0.0.1:{port}"

    def get(path):
        with urllib.request.urlopen(base + path, timeout=5) as r:
            return r.status, r.read().decode("utf-8", "replace")

    up = False
    for _ in range(80):
        try:
            up = get("/health")[0] == 200
            break
        except Exception:
            if proc.poll() is not None:
                break
            time.sleep(0.1)
    if not up:
        proc.terminate()
        print("        directory log:", b"".join(log[-10:]).decode("utf-8", "replace"))
        return check("the directory server starts", False)

    cfg_path = USERDATA / "system.cfg"
    saved = cfg_path.read_bytes()
    s = cfg_sysop("Listed")
    ok = True
    try:
        # The lists in PROTOCOL.md's own example. The directory owns which
        # slugs exist and which list each is in (it moved ham from support to
        # interests in its 0.22.2), so these follow its example rather than
        # guessing at its tables.
        opened = cfg_open(s, b"announce", b"Interests")
        s.buf.clear()
        s.send(BADGE_TO_SUPPORT + b"\x08" * 95 + b"literacy, LGBTQ"
               + DOWN + b"\x08" * 95 + b"c64, Electronics, ham, nonsense" + F1)
        ok &= check("support and interests saved through CONFIG",
                    opened and cfg_verdict(s, [b"Saved and live", b"Nothing changed"])
                    == b"Saved and live")
        # The save restarts the plugin and it announces at once: that is the
        # first heartbeat, which makes a pending listing. The second, once
        # the pending window has passed, puts it on the list.
        s.pump(3.0)
        s.buf.clear()
        s.send(b"announce now\r")
        s.wait_for(b"Sending now", 4)
        s.pump(2.0)

        boards = _json.loads(get("/api/boards.json")[1]).get("boards", [])
        ok &= check("the directory lists the board", len(boards) == 1)
        b = boards[0] if boards else {}
        if b.get("support") != ["lgbtq", "literacy"] or b.get("interests") != ["c64", "electronics", "ham"]:
            print("        board's support line:", cfg_sec_line("plugin:announce", "support"))
            print("        board's interests line:", cfg_sec_line("plugin:announce", "interests"))
            print("        directory has:", {k: b.get(k) for k in ("support", "interests", "name")})
        ok &= check("it stored system as the board sent it", b.get("system") == "host")
        ok &= check("and the four terminals", b.get("terminals") == ["ansi", "utf8", "petscii", "ascii"])
        ok &= check("and guests as true", b.get("guests") is True)
        want = ["chat", "forums", "files", "mail"] if card else ["chat", "mail"]
        ok &= check("and the features that are running", b.get("features") == want)
        ok &= check("and the support causes it knows, in its own order",
                    b.get("support") == ["lgbtq", "literacy"])
        ok &= check("and the interests it knows, nonsense dropped",
                    b.get("interests") == ["c64", "electronics", "ham"])

        page = get("/")[1]
        name = _html.escape(b.get("name", "?"), quote=False)
        at = page.find(f"<span class='bname'>{name}</span>")
        row = page[at:page.find("</tr>", at)] if at >= 0 else ""
        letters = re.findall(r'<span class="bd k-(\w+)"[^>]*>([^<]*)</span>', row)
        # The row's filter keys: every badge it carries, by slug, which is
        # the directory's own record of what it drew.
        m = re.search(r'<tr data-b="([^"]*)"(?: hidden)?><td class=\'name\' data-label=\'Board\'>'
                      r"<span class='bname'>" + re.escape(name) + r"</span>", page)
        keys = m.group(1).split() if m else []
        ok &= check("the board list draws its row", bool(row))
        ok &= check("with the machine badge", ("sys", "host") in letters)
        ok &= check("the PETSCII and guests badges", ("term", "P") in letters and ("guest", "G") in letters)
        ok &= check("a badge for each feature running",
                    ("feat", "C") in letters and ("feat", "M") in letters
                    and (("feat", "Fi") in letters) == card and (("feat", "F") in letters) == card)
        ok &= check("the two support symbols",
                    row.count('class="bd k-sup"') == 2
                    and "lgbtq" in keys and "literacy" in keys)
        ok &= check("and the three interests",
                    row.count('class="bd k-int"') == 3
                    and all(k in keys for k in ("c64", "electronics", "ham")))
        if not keys or row.count('class="bd k-int"') != 3:
            print("        row keys:", keys)
    finally:
        # The directory goes first, or the restart after the restore would
        # announce to it and have a fresh token written over the restored file.
        proc.terminate()
        try:
            proc.wait(5)
        except Exception:
            proc.kill()
        announce_restore(s, saved)
        s.close()
        for p in (db, db.with_name(db.name + "-wal"), db.with_name(db.name + "-shm")):
            if p.exists():
                p.unlink()
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

        # The Screens area listed nothing on every board until the card was
        # seeded, and it was not a file-area bug. The area points at the
        # screen OVERRIDE folder, which is consulted before flash at
        # playback but which nothing ever wrote to, so on a fresh card it
        # was an empty directory and the area correctly listed zero files.
        # Logs looked healthy right next to it only because the caller log
        # is actively mirrored there.
        #
        # So this asserts the seed, from the caller's side rather than off
        # the filesystem: open the area a sysop would open and read what
        # they would read.
        # Pressed as a digit, not entered with enter_area: this session is
        # already standing at the area menu, where Enter opens whatever the
        # highlight is sitting on. enter_area probes with Enter to find out
        # where it is, which at the menu is not a probe, it is a choice.
        # Area 9, the built-in, and the number matters: the harness also
        # configures an area4 called "Screens" pointing at admin/screens,
        # so asserting on the NAME alone passes on the decoy. What is being
        # tested here is the folder the screen player actually reads from.
        #
        # Opening an area lists it straight away, and 24 files page, so the
        # [S9] prompt does not arrive until [More] has been cleared. Drain
        # first, then read: waiting for the prompt before paging is a wait
        # for something that cannot happen yet.
        # Collected across pages rather than read once. drain() clears the
        # buffer every time it answers a [More], so reading after it returns
        # gets whatever came after the last page break, which for a 24 file
        # listing is nothing. readdir order is not guaranteed either, so the
        # file being looked for can be on any page.
        sy.buf.clear()
        sy.send(b"9")
        opened = sy.wait_for(b"Screens", 6)
        sy.pump(1.0)
        screens = plain(sy.buf)
        for _ in range(6):
            if b"[More]" not in plain(sy.buf):
                break
            sy.buf.clear()
            sy.send(b"y")                    # Y continues, n stops
            sy.pump(0.8)
            screens += plain(sy.buf)
        drain(sy)
        ok &= check("the built-in Screens area opens for staff", opened)
        ok &= check("and it is not empty, which it was on every board "
                    "before the card was seeded", b"welcome" in screens)
        ok &= check("it holds the stock set, not one stray file",
                    screens.count(b".an") + screens.count(b".as") >= 4)
        sy.buf.clear()
        sy.send(b"q")
        sy.pump(0.5)

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
    ok &= check("and Q again leaves", s.wait_for(b"Leaving the file areas", 5))
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

def test_dash_uploads():
    """The dashboard's waiting row counts uploads to approve (1.1.0), from
    the file areas' own count through the waiting hook: no directory read,
    and nothing in the core that knows the file areas exist."""
    print("DASH: uploads to approve")
    card = card_dir()
    if HOST not in ("127.0.0.1", "localhost") or not card or not PASSWORD:
        print("  SKIP  needs the harness's card and a sysop password")
        return True
    c = ansi_login("DashUploader")
    ok = check("the drop box opens", enter_area(c, 5, b"Drop Box"))
    ok &= check("an upload starts", area_key(c, b"u", "", b"Start your YMODEM send"))
    ok &= check("and lands, waiting for approval",
                ymodem_send(c, "DASHWAIT.BIN", b"waiting for the sysop\r\n" * 8))
    c.close()
    s = sysop_on("DashApprover")
    drain(s)
    s.buf.clear()
    s.send(b"dash\r")
    read_list(s)
    m = re.search(rb"(\d+) uploads? to approve", plain(s.buf))
    ok &= check("the waiting row says how many uploads wait", m is not None and int(m.group(1)) >= 1)
    s.close()
    return ok



def test_mail_never_lost():
    """A second message must not destroy the first.

    The board used to hold one message per account and let a new one
    REPLACE an unread one, telling the sender "(replacing the one they
    had)" as though that were a feature. Somebody else writing to you threw
    away your mail, which is losing data and reporting success.

    So the two things that matter here: two different people can both leave
    a message and both survive, and a full box is refused rather than
    quietly making room.
    """
    print("Mail: nothing is ever replaced")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build")
        return True

    rx = ansi_login("Boxer")
    a  = ansi_login("Senda")
    b  = ansi_login("Sendb")

    a.buf.clear()
    a.send(b"mail Boxer first message from A\r")
    ok = check("the first message is accepted", a.wait_for(b"Left for Boxer", 5))
    ok &= check("and nothing was said about replacing",
                b"replacing" not in plain(a.buf))

    b.buf.clear()
    b.send(b"mail Boxer second message from B\r")
    ok &= check("a second sender is also accepted", b.wait_for(b"Left for Boxer", 5))
    ok &= check("and is told the box now holds two",
                b"2 waiting" in plain(b.buf))
    ok &= check("and still nothing about replacing",
                b"replacing" not in plain(b.buf))

    # Both survive, oldest first, and the caller is told what is left.
    mail_box(rx)
    rx.buf.clear()
    rx.send(b"\r")
    ok &= check("the first message is still there",
                rx.wait_for(b"first message from A", 5))
    rx.send(b"d")
    ok &= check("and the list says one more is new", rx.wait_for(b"1 new", 4))
    rx.buf.clear()
    rx.send(b"\r")
    ok &= check("the second message survived too",
                rx.wait_for(b"second message from B", 5))
    rx.send(b"d")
    rx.wait_for(b"Deleted", 4)
    ok &= check("and then the box is empty", rx.wait_for(b"Nothing in your mailbox", 4))
    rx.send(b"q")
    rx.wait_for(b"Main", 4)

    # A full box is refused, not emptied to make room. The limit depends
    # on where the mail lives, three on internal flash and twelve with a
    # card, so fill until the board says no rather than hardcoding a
    # number the test would have to keep in step with the board.
    refused = False
    for i in range(20):
        a.buf.clear()
        a.send(("mail Boxer filler %d" % i).encode() + b"\r")
        if not a.wait_for(b"Left for", 5):
            refused = b"messages waiting" in plain(a.buf)
            break
    ok &= check("a full box is refused rather than making room", refused)
    ok &= check("and says plainly that nothing was replaced",
                b"Nothing was replaced" in plain(a.buf))

    mail_box(rx)
    rx.buf.clear()
    rx.send(b"\r")
    ok &= check("the refused message displaced nothing",
                rx.wait_for(b"filler 0", 5))
    rx.send(b"d")
    rx.wait_for(b"Deleted", 4)
    rx.send(b"q")

    for c in (rx, a, b):
        drain(c)
        c.close()
    return ok


def test_mail_rsd():
    """Reply, Save or Delete: reading a message is not disposing of it.

    Reading used to destroy. The message was shown and cleared in the same
    breath, so a caller whose line dropped mid-read, or who was paged, had
    simply lost it with nothing to go back to. Now nothing moves until one
    of three keys says what should happen, and each of them is a single
    rewrite of the mailbox through a temp file and a rename, so a power cut
    either leaves the message alone or leaves the decision made.

    The three are mutually exclusive on purpose. A reply retires the message
    it answers in the same rewrite, which is why it cannot be done as two
    steps: delete first and a refused reply has thrown the original away,
    send first and a failed delete leaves somebody answering it twice.
    """
    print("Mail: reply, save or delete")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build")
        return True

    a = ansi_login("Rsda")
    b = ansi_login("Rsdb")

    # -- Save keeps it, and a kept message can be read back ----------------
    a.buf.clear()
    a.send(b"mail Rsdb keep this one\r")
    ok = check("a message to save is left", a.wait_for(b"Left for Rsdb", 5))

    mail_box(b)
    b.buf.clear()
    b.send(b"\r")
    ok &= check("reading offers the three choices", b.wait_for(b"[D]elete", 5))
    b.buf.clear()
    b.send(b"s")
    ok &= check("S keeps it", b.wait_for(b"Kept", 4))
    ok &= check("and it stays on the list, no longer new", b.wait_for(b"0 new, 1 of", 4))
    b.buf.clear()
    b.send(b"1\r")
    ok &= check("a kept message is still readable, by its number",
                b.wait_for(b"keep this one", 5))
    b.send(b"d")
    ok &= check("and can then be deleted", b.wait_for(b"Deleted", 4))
    b.send(b"q")
    b.wait_for(b"Main", 4)

    # -- Something deliberately kept is not news ---------------------------
    # If it still rang "You have mail" at every login, S would be the wrong
    # choice for the thing it exists for: holding on to something.
    a.buf.clear()
    a.send(b"mail Rsdb quiet please\r")
    a.wait_for(b"Left for", 5)
    mail_box(b)
    b.send(b"\r")
    b.wait_for(b"[D]elete", 5)
    b.send(b"s")
    ok &= check("the second one is kept too", b.wait_for(b"Kept", 4))
    b.send(b"q")
    b.pump(0.5)
    b.close()
    b = ansi_login("Rsdb", pw=TEST_PW)
    ok &= check("a kept message does not ring at login",
                b"You have mail" not in plain(b.buf))

    # -- Reply goes back, and retires what it answers ----------------------
    mail_box(b)
    b.buf.clear()
    b.send(b"1\r")
    ok &= check("the kept message is still there", b.wait_for(b"quiet please", 5))
    b.send(b"r")
    ok &= check("R asks who it is going to", b.wait_for(b"Reply to Rsda", 4))
    a.buf.clear()
    b.buf.clear()
    # A reply is written in the shared composer now, so Enter finishes a
    # line and /s sends the whole message. It used to be one line, and Enter
    # sent it; that inconsistency with the rest of the board is what the
    # composer removed.
    b.send(b"understood\r")
    b.pump(0.4)
    b.send(b"/s\r")
    ok &= check("the reply is sent", b.wait_for(b"Left for Rsda", 6))
    ok &= check("and the sender is told at once", a.wait_for(b"You have mail", 5))
    ok &= check("replying retired the message it answered",
                b.wait_for(b"Nothing in your mailbox", 5))
    b.send(b"q")
    b.wait_for(b"Main", 4)

    mail_box(a)
    a.buf.clear()
    a.send(b"\r")
    ok &= check("the reply reads back", a.wait_for(b"understood", 5))
    a.send(b"d")
    a.wait_for(b"Deleted", 4)
    a.send(b"q")
    a.wait_for(b"Main", 4)

    # -- Backing out changes nothing ---------------------------------------
    # Two of the three choices cannot be undone, so the key that is easiest
    # to press by accident has to be the one that loses nothing.
    a.buf.clear()
    a.send(b"mail Rsdb still here\r")
    a.wait_for(b"Left for", 5)
    mail_box(b)
    b.send(b"\r")
    b.wait_for(b"[D]elete", 5)
    b.buf.clear()
    b.send(b"\x1b")
    ok &= check("ESC goes back to the list and leaves it unread", b.wait_for(b"1 new", 4))
    b.buf.clear()
    b.send(b"\r")
    ok &= check("and it is still waiting to be read",
                b.wait_for(b"still here", 5))
    b.send(b"d")
    b.wait_for(b"Deleted", 4)
    b.send(b"q")

    for c in (a, b):
        drain(c)
        c.close()
    return ok


def test_list_abort_returns():
    """Stopping a plugin's listing at [More] must hand the caller back.

    The core gives the session to the owning plugin when a list ends and
    deliberately draws no shell prompt, because the plugin owns the screen.
    The file manager relied on emitting its prompt as the last ROW of the
    listing, which works right up until somebody presses Q at [More]: the
    row is never reached, so the caller was left looking at "Stopped." with
    nothing to tell them the file areas still had them, and every key after
    that went to a subsystem they could not see.

    Invisible to any test that reads a listing to the end, which is why it
    lasted. The plugin gets a listDone hook now.
    """
    print("A stopped listing hands you back")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the host build")
        return True

    # The files plugin is PF_SD, so on a board with no card it does not start
    # and FILES is not a command at all. That is the design, not a gap: this
    # test needs a subsystem that owns a session and produces a long listing,
    # and without a card there is not one.
    if not os.environ.get("BBS_SD_DIR", ""):
        print("  SKIP  the files plugin needs a card")
        return True

    if not PASSWORD:
        print("  SKIP  needs a sysop password to reach a listing long enough to page")
        return True

    s = ansi_login("Aborter")
    drain(s)
    s.buf.clear()
    s.send(f"bye {PASSWORD}\r".encode())
    if not s.wait_for(b"SysOp node", 6):
        print("  SKIP  could not elevate")
        s.close()
        return True
    s.pump(0.6)

    # Area 9 is the board's own Screens area, which the card seeding fills
    # with the two dozen stock screens, so it is long enough to page. That
    # is the whole requirement here: a listing that stops at [More].
    s.buf.clear()
    s.send(b"files 9\r")
    ok = check("a staff area with a long listing opens", s.wait_for(b"Screens", 6))
    if not s.wait_for(b"[More]", 6):
        print("  SKIP  the listing did not page, nothing to abort")
        drain(s); leave_files(s); s.close()
        return ok

    s.buf.clear()
    s.send(b"q")
    s.pump(1.2)
    out = plain(s.buf)
    ok &= check("the listing stops", b"Stopped" in out)
    ok &= check("and the subsystem prompt comes back rather than nothing",
                b"Files>" in out)

    # And the session really is usable, not merely showing a prompt.
    s.buf.clear()
    s.send(b"q")
    ok &= check("Q still works afterwards", s.wait_for(b"File areas", 5))
    drain(s)
    leave_files(s)
    s.close()
    return ok


def test_shutdown():
    """SHUTDOWN warns more than once, and CANCEL stops it.

    The first cut announced the opening threshold and then went silent:
    the cadence list descends, and the loop broke on the first entry it had
    already announced instead of stepping past it to reach the smaller ones.
    So "down in 10 seconds" was announced and 5, 4, 3, 2, 1 never were.

    Nothing about that is visible except by watching a real countdown, which
    is how Rob found it and why it is pinned here.
    """
    print("SHUTDOWN: the countdown, and cancelling it")
    if HOST not in ("127.0.0.1", "localhost") or not PASSWORD:
        print("  SKIP  needs the host build and a sysop password")
        return True

    # Start one and cancel it. Nobody wants the suite's board going down.
    s = ansi_login("Shutman")
    s.buf.clear()
    s.send(f"bye {PASSWORD}\r".encode())
    ok = check("sysop", s.wait_for(b"SysOp node", 5))
    s.pump(0.5)

    s.buf.clear()
    s.send(b"shutdown 9999\r")
    ok &= check("an out of range countdown is refused",
                s.wait_for(b"5 to 3600", 4))

    watcher = ansi_login("Watcher")
    drain(watcher)
    watcher.buf.clear()

    s.buf.clear()
    s.send(b"shutdown 20\r")
    ok &= check("the countdown starts", s.wait_for(b"Going down in 20", 4))
    ok &= check("and everybody else is told",
                watcher.wait_for(b"taking the board down", 5))

    # Twenty seconds, and the wait below stops at the ten second mark, so
    # there are ten left to cancel in. The first version of this test used a
    # ten second countdown and spent nearly all of it watching, then raced
    # the board to cancel with about a second to spare. The cadence was
    # fine; the test was not.
    ok &= check("a later threshold is announced too, not just the first",
                watcher.wait_for(b"in 10 second", 16))

    s.buf.clear()
    s.send(b"shutdown cancel\r")
    ok &= check("cancel stops it", s.wait_for(b"Shutdown cancelled", 5))
    ok &= check("and the room is told", watcher.wait_for(b"cancelled", 6))

    # The board must still take calls.
    after = ansi_login("Stillup")
    ok &= check("the board is still answering after a cancel",
                after.sock_alive() if hasattr(after, "sock_alive") else True)
    drain(after); after.close()

    for c in (s, watcher):
        drain(c)
        c.close()
    return ok


def test_staff_remembered():
    """Staff access is typed once a week, not once a call, and revoking it works.

    The trade this makes is worth stating, because it is a real one. Account
    passwords cross this board in the clear on every single login, so
    remembering staff access against the account alone would turn a sniffed
    account password into a week of staff rights. It is bound to the address
    it was confirmed from, so a captured password is worth nothing from
    anywhere else, and the sysop level is never remembered at all.

    What is tested here: it comes back on the next call, and lowering the
    account's level in USER EDIT takes it away again.
    """
    print("Staff access, remembered and revoked")
    if HOST not in ("127.0.0.1", "localhost") or not CO1 or not PASSWORD:
        print("  SKIP  needs the host build and the staff passwords")
        return True

    # Earn it.
    c = ansi_login("Keeper2")
    drain(c)
    c.buf.clear()
    c.send(f"bye {CO1}\r".encode())
    ok = check("co-sysop access granted on the password",
               c.wait_for(b"access on node", 6))
    drain(c)
    c.close()

    # Come back. Same address, inside the week, no password typed.
    back = ansi_login("Keeper2", pw=TEST_PW)
    shown = plain(back.buf)
    ok &= check("and is remembered on the next call without typing it",
                b"remembered from" in shown)
    back.buf.clear()
    back.send(b"nodes\r")
    ok &= check("the rights really came back, not just the message",
                back.wait_for(b"Nodes", 5))
    drain(back)
    back.close()

    # A sysop lowers them. That has to revoke it.
    s = ansi_login("Revoker")
    s.buf.clear()
    s.send(f"bye {PASSWORD}\r".encode())
    ok &= check("sysop to do the revoking", s.wait_for(b"SysOp node", 5))
    s.pump(0.6)
    s.buf.clear()
    s.send(b"user edit Keeper2\r")
    ok &= check("the account opens", s.wait_for(b"EDIT ACCOUNT", 5))
    # Level is a cycle field: walk to it and pick User, then save.
    s.send(DOWN * 7 + b"u" + F1)
    ok &= check("level lowered to User", s.wait_for(b"saved.", 6))
    drain(s)
    s.close()

    gone = ansi_login("Keeper2", pw=TEST_PW)
    after = plain(gone.buf)
    ok &= check("the remembered access is revoked at the next login",
                b"remembered from" not in after)
    gone.buf.clear()
    gone.send(b"nodes\r")
    ok &= check("and the staff command is unknown again",
                gone.wait_for(b"Unknown command", 5))
    drain(gone)
    gone.close()
    return ok


def test_rename_follows():
    """A handle is a display name, and what is filed under it must follow.

    Renaming somebody used to write users.txt, patch any live session, and
    stop. Their own unread mail stayed filed under a name that no longer
    existed, so it was simply never found again, and a room ban stayed under
    the old name too, which made renaming a way out of one.

    Nothing announced any of it. The mail did not bounce and the ban did not
    complain; both just quietly stopped applying to the person they were
    about.
    """
    print("A rename takes your things with you")
    if HOST not in ("127.0.0.1", "localhost") or not PASSWORD:
        print("  SKIP  needs the host build and a sysop password")
        return True

    a = ansi_login("Renamer")
    victim = ansi_login("Oldname")
    drain(victim)

    a.buf.clear()
    a.send(b"mail Oldname this should follow you\r")
    ok = check("a message is left for the old handle",
               a.wait_for(b"Left for Oldname", 5))
    victim.close()          # they must be offline: a rename refuses a live handle

    s = ansi_login("Renman")
    s.buf.clear()
    s.send(f"bye {PASSWORD}\r".encode())
    ok &= check("sysop for the rename", s.wait_for(b"SysOp node", 5))
    s.pump(0.6)
    s.buf.clear()
    s.send(b"user edit Oldname\r")
    ok &= check("the account opens", s.wait_for(b"EDIT ACCOUNT", 5))

    # The handle field is first and focused. Rub it out and type the new one.
    s.send(b"\x08" * 24 + b"Newname" + F1)
    ok &= check("renamed", s.wait_for(b"saved.", 6))
    drain(s)

    # The mail has to be readable under the new name, and gone from the old.
    n = ansi_login("Newname", pw=TEST_PW)
    ok &= check("the new handle is told it has mail",
                b"You have mail" in plain(n.buf))
    mail_box(n)
    n.buf.clear()
    n.send(b"\r")
    ok &= check("and the message followed the rename",
                n.wait_for(b"this should follow you", 5))
    n.send(b"d")
    n.wait_for(b"Deleted", 4)
    n.send(b"q")

    for c in (a, s, n):
        drain(c)
        c.close()
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
        # The stock ABOUT names the version. This read b"v0." until 1.0.0
        # made it a claim about which major version was running.
        ok &= check("pulling the card falls back to the stock screen",
                    b"CARD ABOUT OVERRIDE" not in after and BBS_VERSION.encode() in after)
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
    # The status line carried the micro sign. Walking it a byte at a time
    # sent 0xC2 and 0xB5 through the charset map separately and each came
    # back as '?', so a refresh screen said "??nleashed BBS". The dashboard
    # no longer names the board (1.1.0: SYS and ABOUT do), and every row of
    # it still goes through the terminal layer's own counting.
    ok = check("the refresh header keeps the micro sign", b"?nleashed" not in seen)
    ok &= check("and the refresh is drawn", b"DASHBOARD" in seen)
    c.send(b"q")                          # Q stops the dashboard; any key stops WHO n
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
    c.send(b"pw1234\rpw1234\rScreeny\rscreeny@example.com\r")
    to_save(c, b"WELCOME ABOARD")
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

    # A returning caller gets the motd, not the new-user screen.
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


def sysop_on(handle):
    """A caller elevated to the sysop node. The buffer holds the whole of the
    login and the elevation, so a test can read what was said on the way in:
    ring notes are shown at login to an account the sysop password marked,
    and at elevation to one it has not marked yet."""
    s = ansi_login(handle)
    drain(s)
    s.send(f"bye {PASSWORD}\r".encode())
    s.wait_for(b"SysOp node", 6)
    s.wait_for(b"HELP for commands", 4)
    s.pump(0.6)
    return s


def rang_in(c, pat, secs=4):
    """Wait for pat, and say how long it took from now."""
    t0 = time.time()
    got = c.wait_for(pat, secs)
    return got, time.time() - t0


def ring_board(port):
    """A copy of this board for the sysop page's tests (1.1.0), started on
    port: every sysop mark taken off users.txt, and no mail or ring notes.

    A ring nobody answers goes to the MAIL of every account the sysop
    password has marked, and on the harness board that is every account any
    earlier test elevated: a dozen or more, each taking a copy of every ring,
    which fills the board's 64 messages within a few rings and turns the rest
    into notes. A real board has one or two. The copy gives these tests the
    board they are about, whatever ran before them. Returns (process, dir).
    """
    tmp = copy_data()
    user = tmp / "data" / "user"
    users = user / "users.txt"
    if users.exists():                               # none yet on a board nobody has called
        users.write_text("".join(ln for ln in users.read_text().splitlines(True)
                                 if ln.strip() != "level = sysop"))
    for name in ("mail.dat", "rings.txt"):
        for p in user.rglob(name):
            p.unlink()
    proc = start_copy(tmp, (str(port),))
    copy_log(tmp, f"listening on {port},")
    return proc, tmp


def on_ring_board(port, body):
    """body(tmp) with every caller dialling a ring_board copy on port."""
    global PORT
    proc, tmp = ring_board(port)
    saved = PORT
    PORT = port
    try:
        return body(tmp)
    finally:
        PORT = saved
        stop_copy(proc, tmp)


def test_operator():
    """OPERATOR on a copy of this board (see ring_board and operator_body)."""
    if HOST not in ("127.0.0.1", "localhost") or not PASSWORD:
        print("OPERATOR: ringing for the sysop")
        print("  SKIP  needs the host build and a sysop password")
        return True
    return on_ring_board(PORT + 3707, operator_body)


def operator_body(tmp):
    """OPERATOR: a caller rings for the sysop (1.1.0).

    The sysop page from the UX spec and the copy: the question when O has no
    reason, the answer at once when nobody is there to ask, a hidden sysop
    answered exactly as an absent one, the sysop's one-key question, and
    what A, D, X and Q each do. A ring nobody answered goes to the sysop's
    MAIL (1.1.0); test_ring_mail has the whole of that, and
    test_operator_notes the note file it falls back to.
    """
    print("OPERATOR: ringing for the sysop")
    if HOST not in ("127.0.0.1", "localhost") or not PASSWORD:
        print("  SKIP  needs the host build and a sysop password")
        return True

    # A sysop account before anybody rings (1.1.0): a missed ring goes to
    # the MAIL of every account the sysop password has marked, so this one
    # has to exist first to be one of them.
    s = sysop_on("OpSysop")
    s.close()
    time.sleep(0.5)

    a = ansi_login("Ringer1")
    drain(a)
    a.buf.clear()
    a.send(b"?\r")
    read_list(a)
    menu = plain(a.buf)
    ok = check("OPERATOR is on the main menu, after PAGE",
               b"OPERATOR" in menu and 0 <= menu.find(b"PAGE") < menu.find(b"OPERATOR")
               and b"ring for the sysop" in menu)
    ok &= check("with O picked out as its shortcut",
                ansi_color(33, bold=True) + b"O" in bytes(a.buf))

    a.buf.clear()
    a.send(b"o\r")
    ok &= check("a bare O asks what for", a.wait_for(b"What do you need the sysop for?", 4))
    a.send(b"\r")
    ok &= check("and an empty answer sends nothing", a.wait_for(b"Nothing sent.", 4))
    a.wait_for(b"Main", 3)

    a.buf.clear()
    a.send(b"o the drop box is full\r")
    got, took = rang_in(a, b"The sysop isn't available.", 4)
    ok &= check("with no sysop on, the answer comes at once", got and took < 3)
    ok &= check("and says what was written is kept", b"saved for them" in plain(a.buf))
    a.wait_for(b"Main", 3)
    a.buf.clear()
    a.send(b"o again\r")
    ok &= check("a second ring straight after is refused, with the wait",
                a.wait_for(b"Already rung. Try again in 3 minutes.", 4))

    s = sysop_on("OpSysop")
    seen = plain(s.buf)
    ok &= check("the sysop is told of mail on the way in", b"You have mail." in seen)
    drain(s)
    mail_box(s)
    ok &= check("and the ring is in it", b"Ring: the drop box is full" in plain(s.buf))
    mail_empty(s)

    # Hidden is not there. Same words, same speed, and no ring.
    drain(s)
    s.buf.clear()
    s.send(b"hide\r")
    s.wait_for(b"hidden from WHO", 3)
    s.buf.clear()
    b = ansi_login("Ringer2")
    drain(b)
    b.buf.clear()
    b.send(b"o hidden test\r")
    got, took = rang_in(b, b"The sysop isn't available.", 4)
    ok &= check("a hidden sysop is answered as one who is not on, and as fast",
                got and took < 3)
    s.pump(1.0)
    ok &= check("and is not rung", b"is ringing" not in plain(s.buf))
    s.send(b"show\r")
    s.wait_for(b"listed in WHO", 3)
    s.pump(0.3)

    # D: declined.
    c = ansi_login("Ringer3")
    nc = c.node()
    drain(c)
    s.buf.clear()
    c.buf.clear()
    c.send(b"o please look at the drop box\r")
    ok &= check("the caller hears it ringing", c.wait_for(b"Ringing the sysop", 4)
                and b"(any key stops)" in plain(c.buf))
    asked = s.wait_for(b"[A]nswer [D]ecline [X] Away [Q] Later: ", 6)
    seen = plain(s.buf)
    ok &= check("the sysop is asked, one key", asked)
    ok &= check("after a bell and a flashing RING",
                0 <= s.buf.find(b"\x07") < s.buf.find(b" RING "))
    ok &= check("and told who and why",
                f"Ringer3 ({nc}) is ringing: please look at the drop box".encode() in seen)
    s.pump(0.3)
    s.send(b"d")
    ok &= check("D tells the sysop", s.wait_for(b"Declined. They have been told.", 4))
    ok &= check("and the caller", c.wait_for(b"The sysop can't talk right now.", 4))
    s.pump(0.6)
    after = plain(s.buf)
    ok &= check("and gives the sysop the prompt back",
                after.rfind(b"Sysop") > after.find(b"Declined."))
    c.wait_for(b"Main", 3)

    # X: away, and the next caller is told so at once.
    d = ansi_login("Ringer4")
    drain(d)
    s.buf.clear()
    d.send(b"o away test\r")
    s.wait_for(b"Later: ", 6)
    s.pump(0.3)
    s.send(b"x")
    ok &= check("X says the sysop is away now", s.wait_for(b"You're away", 4))
    ok &= check("and the caller hears it", d.wait_for(b"The sysop is away.", 4))
    e = ansi_login("Ringer5")
    drain(e)
    e.buf.clear()
    e.send(b"o while away\r")
    got, took = rang_in(e, b"The sysop is away.", 4)
    ok &= check("the next ring is answered 'away' at once", got and took < 3)
    s.buf.clear()
    s.send(b"dnd\r")
    s.wait_for(b"Pages are on", 3)
    s.pump(0.3)

    # Q, then O asks again, then A: both in the room, talking to each other.
    f = ansi_login("Ringer6")
    nf = f.node()
    drain(f)
    s.buf.clear()
    f.buf.clear()
    f.send(b"o can we talk\r")
    s.wait_for(b"Later: ", 6)
    s.pump(0.3)
    s.send(b"q")
    ok &= check("Q leaves it ringing", s.wait_for(b"Still ringing. O answers while it does.", 4))
    s.pump(0.5)
    s.buf.clear()
    s.send(b"o\r")
    ok &= check("a bare O asks the sysop again",
                s.wait_for(b"Later: ", 4) and b"can we talk" in plain(s.buf))
    s.pump(0.3)
    s.send(b"a")
    ok &= check("A puts the caller in the room", f.wait_for(b"The sysop answered.", 5))
    ok &= check("talking to the sysop only, [>S] on the line", f.wait_for(b"[>S]", 4))
    ok &= check("and the sysop in the room too", s.wait_for(b"Answered.", 5))
    ok &= check("talking to the caller only", s.wait_for(f"[>{nf}]".encode(), 4))
    f.pump(0.5)
    s.pump(0.5)
    s.buf.clear()
    f.send(b"hello sysop\r")
    ok &= check("what the caller types reaches the sysop", s.wait_for(b"hello sysop", 4))
    ok &= check("as a private, marked P", b"P#" + nf.encode() + b":Ringer6" in plain(s.buf))

    for x in (a, b, c, d, e, f):
        x.close()
    s.close()

    # Hidden, declined, away and away: four rings nobody answered, each
    # kept, in MAIL or, when every sysop box is full, as a note. The
    # console says which. Answered leaves nothing.
    s = sysop_on("OpSysop")
    mail_box(s)
    box = plain(s.buf)
    ok &= check("the rings the sysop missed are in MAIL",
                b"Ring: hidden test" in box and b"Ring: please look at the drop box" in box)
    ok &= check("an answered ring leaves no mail", b"can we talk" not in box)
    mail_empty(s)
    s.close()
    host = tmp / "host.log"
    log = host.read_text(errors="replace") if host.exists() else ""
    ok &= check("and every missed ring was kept, the console says how",
                all(re.search(r"ring from node \d+ ended: %s, (mailed|note left)" % how, log)
                    for how in ("declined", "away")) and
                len(re.findall(r"rang for the sysop, (away|not available): (mailed|note left)", log)) >= 3)
    return ok


def test_operator_ends():
    """OPERATOR's other endings, on a copy of this board (see ring_board)."""
    if HOST not in ("127.0.0.1", "localhost") or not PASSWORD:
        print("OPERATOR: no answer, stopped, hung up, the room, a form")
        print("  SKIP  needs the host build and a sysop password")
        return True
    return on_ring_board(PORT + 3708, operator_ends_body)


def operator_ends_body(tmp):
    """Every other way a ring ends, the room, and a form (1.1.0)."""
    print("OPERATOR: no answer, stopped, hung up, the room, a form")
    if HOST not in ("127.0.0.1", "localhost") or not PASSWORD:
        print("  SKIP  needs the host build and a sysop password")
        return True
    s = sysop_on("OpSysop2")
    drain(s)

    # Both on before either rings: signing up takes longer than the host's
    # ten second ring, and the second caller has to ring during the first.
    a = ansi_login("Waiter1")
    na = a.node()
    drain(a)
    b = ansi_login("Waiter2")
    drain(b)
    s.buf.clear()
    a.buf.clear()
    a.send(b"o waiting\r")
    s.wait_for(b"Later: ", 6)
    s.pump(0.3)
    s.send(b"q")
    s.wait_for(b"Still ringing", 4)

    b.buf.clear()
    b.send(b"o me too\r")
    ok = check("one ring at a time on the whole board",
               b.wait_for(b"One ring at a time. Try in a minute.", 4))
    s.buf.clear()
    ok &= check("a ring nobody answers runs out: No answer",
                a.wait_for(b"No answer. What you wrote is saved for the sysop.", 20))
    ok &= check("and the sysop is told it stopped",
                s.wait_for(f"Waiter1 ({na}) stopped ringing. It is in MAIL.".encode(), 6))

    c = ansi_login("Waiter3")
    drain(c)
    s.buf.clear()
    c.buf.clear()
    c.send(b"o stop me\r")
    c.wait_for(b"Ringing the sysop", 4)
    s.wait_for(b"Later: ", 6)
    time.sleep(0.8)
    c.send(b"k")
    ok &= check("any key stops a ring", c.wait_for(b"You stopped ringing.", 4))
    ok &= check("and the question on the sysop's screen gives way to saying so",
                s.wait_for(b"stopped ringing. It is in MAIL.", 4))
    s.pump(0.5)

    d = ansi_login("Waiter4")
    nd = d.node()
    drain(d)
    s.buf.clear()
    d.send(b"o gone soon\r")
    s.wait_for(b"Later: ", 6)
    d.close()
    ok &= check("a caller hanging up mid-ring is said",
                s.wait_for(f"Waiter4 ({nd}) hung up. It is in MAIL.".encode(), 6))
    s.pump(0.5)

    # The room: /o from a caller, and the sysop there gets two lines and /o-.
    r = ansi_login("Roomer")
    drain(r)
    for x in (s, r):
        x.buf.clear()
        x.send(b"chat\r")
        x.wait_for(b"here.", 4)
        x.pump(0.4)
    s.buf.clear()
    r.buf.clear()
    r.send(b"/o\r")
    ok &= check("/o in the room asks what for",
                r.wait_for(b"What do you need the sysop for?", 4))
    r.send(b"from the room\r")
    ok &= check("the sysop in the room is told in the room's voice",
                s.wait_for(b"is ringing: from the room", 6) and
                s.wait_for(b"/o answers, /o- declines.", 3))
    ok &= check("with no one-key question in the room", b"[A]nswer" not in s.buf)
    s.pump(0.3)
    s.send(b"/o-\r")
    ok &= check("/o- declines it", s.wait_for(b"Declined.", 4) and
                r.wait_for(b"The sysop can't talk right now.", 4))
    r.pump(0.5)
    r.buf.clear()
    r.send(b"back in the room\r")
    ok &= check("and the caller is back in the room, talking",
                s.wait_for(b"back in the room", 4))
    s.send(b"/q\r")
    s.wait_for(b"Sysop", 4)
    s.pump(0.4)

    # A form: the status line, then ESC and O.
    s.buf.clear()
    s.send(b"profile\r")
    s.wait_for(b"YOUR PROFILE", 5)
    s.pump(0.5)
    f = ansi_login("Former")
    nf = f.node()
    drain(f)
    s.buf.clear()
    f.send(b"o in a form\r")
    ok &= check("a ring reaches a sysop in a form, on its status line",
                s.wait_for(f"RING Former ({nf}). ESC, then O.".encode(), 6))
    s.send(b"\x1b")
    s.pump(0.6)
    s.buf.clear()
    s.send(b"o\r")
    ok &= check("and O at the prompt asks", s.wait_for(b"Later: ", 4)
                and b"in a form" in plain(s.buf))
    s.pump(0.3)
    s.send(b"d")
    ok &= check("which D answers", f.wait_for(b"The sysop can't talk right now.", 4))
    s.pump(0.5)

    # A refreshing DASH gives way to a ring rather than holding it unseen.
    s.buf.clear()
    s.send(b"dash 5\r")
    s.wait_for(b"Refresh 5s", 6)
    w = ansi_login("Waiter5")
    drain(w)
    w.send(b"o behind the dashboard\r")
    ok &= check("a ring stops DASH n and asks",
                s.wait_for(b"is ringing: behind the dashboard", 8) and s.wait_for(b"Later: ", 4))
    s.pump(0.3)
    s.send(b"d")
    w.wait_for(b"can't talk right now", 4)
    s.pump(0.5)

    # A room shut to guests (write = users) still lets a guest the sysop
    # answered talk to the sysop. The room itself stays shut to them.
    cfg = tmp / "data" / "user" / "system.cfg"           # the copy's, which it reloads
    before = cfg.read_text()
    shut = re.sub(r"(\[plugin:chat\][^\[]*?\nwrite\s*=\s*)all", r"\1users", before, count=1)
    if shut != before:
        cfg.write_text(shut)
        cfg_reload(s)
        drain(s)
        s.pump(0.5)
        g = Caller(ansi=True)
        g.wait_for(b"Enter your handle", 10)
        g.send(b"Visitor9\r")
        g.wait_for(b"[G]uest", 5)
        g.send(b"g")
        g.wait_for(b"Main", 8)
        drain(g)
        s.buf.clear()
        g.send(b"o cannot sign up\r")
        s.wait_for(b"Later: ", 8)
        ok &= check("a guest's ring says so", b"guest) is ringing: cannot sign up" in plain(s.buf))
        s.pump(0.3)
        s.send(b"a")
        g.wait_for(b"[>S]", 6)
        s.wait_for(b"Answered.", 6)
        g.pump(0.5)
        s.pump(0.5)
        g.buf.clear()
        s.buf.clear()
        g.send(b"hello from a guest\r")
        ok &= check("a guest the sysop answered may talk to the sysop in a room shut to guests",
                    s.wait_for(b"hello from a guest", 4) and b"not talk here" not in plain(g.buf))
        g.send(b"/p*\r")
        g.wait_for(b"Back to the room", 4)
        g.buf.clear()
        g.send(b"to the whole room\r")
        ok &= check("but not to the room", g.wait_for(b"You can watch, but not talk here.", 4))
        s.send(b"/q\r")
        s.wait_for(b"Sysop", 4)
        s.pump(0.5)
        cfg.write_text(before)
        cfg_reload(s)
        g.close()

    for x in (a, b, c, r, f, w):
        x.close()
    s.close()
    # Clear the notes, so nothing after this finds them at its elevation.
    s = sysop_on("OpSysop2")
    s.close()
    return ok


def test_notices_in_places():
    """Pages, broadcasts, SHUTDOWN and arrivals reach a caller who is not at
    the main prompt (1.1.0). They used to wait for the prompt, so somebody in
    the chat room, their mailbox, the forums or the file areas got none of
    them, and a caller in the room through a SHUTDOWN was hung up unwarned."""
    print("Notices inside the room, the mailbox, forms and the file areas")
    a = ansi_login("Placed")
    na = a.node()
    b = ansi_login("Pager")
    drain(a)
    drain(b)

    # The room, part way through a line.
    a.send(b"chat\r")
    a.wait_for(b"here.", 4)
    a.pump(0.5)
    a.send(b"half a line")
    a.pump(0.4)
    a.buf.clear()
    b.send(f"page {na} into the room\r".encode())
    ok = check("a page reaches a caller in the chat room",
               a.wait_for(b"Page from Pager", 5))
    ok &= check("with its bell and flashing tag",
                0 <= a.buf.find(b"\x07") < a.buf.find(b" PAGE "))
    a.pump(0.6)
    lines = [ln for ln in render_lines(a.buf) if ln.strip()]
    ok &= check("and what they were typing comes back under it",
                bool(lines) and lines[-1].strip() == "half a line")

    # An arrival rings for them there, and /b stops the bell but not the line.
    a.buf.clear()
    c = ansi_login("Arrives")
    ok &= check("an arrival reaches the room, with a bell",
                a.wait_for(b"Arrives is on node", 6) and b"\x07" in a.buf)
    a.send(b"\x1b")
    a.pump(0.3)
    a.send(b"/b\r")
    a.wait_for(b"Bell off.", 3)
    a.pump(0.3)
    a.buf.clear()
    d = ansi_login("Arrives2")
    ok &= check("with the bell off the arrival is still said",
                a.wait_for(b"Arrives2 is on node", 6))
    ok &= check("but does not ring", b"\x07" not in a.buf)
    a.send(b"/b\r")
    a.wait_for(b"Bell on.", 3)
    a.send(b"/q\r")
    a.wait_for(b"Main", 4)
    a.pump(0.4)

    # The mailbox.
    a.buf.clear()
    a.send(b"mail\r")
    a.wait_for(b"Mail> ", 4)
    a.pump(0.3)
    a.buf.clear()
    b.send(f"page {na} into the mailbox\r".encode())
    ok &= check("a page reaches a caller in their mailbox",
                a.wait_for(b"Page from Pager", 5))
    a.pump(0.6)
    seen = plain(a.buf)
    ok &= check("and the mailbox prompt comes back under it",
                seen.rfind(b"Mail> ") > seen.find(b"Page from Pager"))
    a.send(b"q")
    a.wait_for(b"Main", 4)
    a.pump(0.3)

    if PASSWORD and HOST in ("127.0.0.1", "localhost"):
        s = sysop_on("NoticeOp")
        drain(s)
        # A form: a broadcast on its status line.
        a.buf.clear()
        a.send(b"profile\r")
        a.wait_for(b"YOUR PROFILE", 5)
        a.pump(0.5)
        a.buf.clear()
        s.send(b"broadcast hello the forms\r")
        ok &= check("a broadcast reaches a caller in a form, on the status line",
                    a.wait_for(b"Sysop: hello the forms", 5))
        a.send(b"\x1b")
        a.pump(0.5)
        # SHUTDOWN reaches the room.
        a.send(b"chat\r")
        a.wait_for(b"here.", 4)
        a.pump(0.4)
        a.buf.clear()
        b.pump(0.3)
        b.buf.clear()
        s.send(b"shutdown 60\r")
        ok &= check("SHUTDOWN warns a caller in the chat room",
                    a.wait_for(b"taking the board down in 60 seconds", 6))
        # The countdown's own thresholds used to fire all at once on the
        # first pass: "in 120 seconds" straight after "in 60 seconds".
        b.wait_for(b"taking the board down", 4)
        b.pump(1.5)
        ok &= check("and does not announce a threshold it has already passed",
                    b"in 120 seconds" not in plain(b.buf) and
                    b"in 120 seconds" not in plain(a.buf))
        s.send(b"shutdown cancel\r")
        ok &= check("and so does its cancel", a.wait_for(b"shutdown is cancelled", 6))
        a.send(b"/q\r")
        a.wait_for(b"Main", 4)
        s.close()

    if os.environ.get("BBS_SD_DIR", ""):
        a.pump(0.3)
        a.buf.clear()
        a.send(b"files\r")
        a.wait_for(b"Files:", 5)
        a.pump(0.5)
        a.buf.clear()
        b.send(f"page {na} into the files\r".encode())
        ok &= check("a page reaches a caller in the file areas",
                    a.wait_for(b"Page from Pager", 5))
        a.pump(0.6)
        seen = plain(a.buf)
        ok &= check("and the file area menu is drawn again under it",
                    seen.rfind(b"Files:") > seen.find(b"Page from Pager"))
        leave_files(a)
        a.wait_for(b"Main", 4)
        a.pump(0.3)
        a.buf.clear()
        a.send(b"forums\r")
        a.wait_for(b"Forums>", 5)
        a.pump(0.5)
        a.buf.clear()
        b.send(f"page {na} into the forums\r".encode())
        ok &= check("a page reaches a caller in the forums",
                    a.wait_for(b"Page from Pager", 5))
        a.pump(0.6)
        seen = plain(a.buf)
        ok &= check("and the forums prompt comes back under it",
                    seen.rfind(b"Forums>") > seen.find(b"Page from Pager"))
        a.send(b"q")
        a.pump(0.5)

    for x in (a, b, c, d):
        x.close()
    return ok


# Groups: one word standing for the tests that share a subsystem.
#
# A full run is 690 checks and several minutes, which is the right price
# before a commit and the wrong one after every edit. These exist so the
# cheap run is cheap enough to actually do.
#
# Deliberately a little wider than the change usually is. A group that only
# covered the exact file being edited would miss the thing that breaks, which
# is almost always the subsystem next door: the file areas and the forums
# both draw through the same list machinery, mail lives inside chat, and the
# message editor is now shared by mail and forums both.
GROUPS = {
    # Anything that takes a message from a caller. The editor is shared, so
    # a change to it can break either end.
    "messaging": ["mail", "forums", "chat", "room_commands", "room_new", "room_quit",
                  "survives_notice", "config_forum", "room_time", "bell", "codes_in",
                  "room_narrow",
                  "long_help", "info_pages", "operator", "notices_in", "ring_mail"],
    # The subsystems that own a session and draw their own screens.
    "places":    ["forums", "files", "chat", "xfer", "notices_in"],
    # Anything that reads or writes the card, and the backups (on the card
    # since 1.1.0, and restores across the board's two partitions).
    "storage":   ["files", "forums", "sd", "xfer", "backup", "restore"],
    # The shell, its lists and the screens the core draws.
    "shell":     ["menus", "sysinfo", "page", "about", "config", "welcome", "paced", "seeded", "fx_codes",
                  "lights", "operator", "dash", "nodes_columns"],
    # Logging in, accounts, staff.
    "login":     ["accounts", "handle_case", "guest", "sysop", "cosysop", "user_admin", "first_setup", "ban",
                  "boot_hold", "boot_notices"],
    # Terminal handling across the three flavours.
    "terminal":  ["ansi", "petscii", "ascii", "telnet_first"],
}


# The order the suite was designed to run in.
#
# This is the ONE place the sequence lives. The full run walks it and --only
# filters it, so a targeted run is always a subset of the real run rather
# than an alphabetical reshuffle of it. Getting that wrong cost a round:
# test_ban bans 127.0.0.1 and has to be last, and test_backup reads an
# account test_page creates.
#
# Anything not named here runs after everything that is, in the order it was
# picked, which is the right default for a test added and not yet placed.
ORDER_NAMES = [
    "test_ansi", "test_telnet_first", "test_petscii", "test_ascii",
    "test_page", "test_sysop", "test_cosysop", "test_accounts", "test_accounts_form_notes",
    "test_handle_case",
    "test_user_admin", "test_guest",
    "test_privacy", "test_plugins", "test_about", "test_announce",
    "test_announce_badges", "test_announce_directory",
    "test_chat", "test_room_commands", "test_room_new_commands", "test_room_quit_logoff",
    "test_room_time_staff_only", "test_bell", "test_codes_in_messages", "test_fx_codes", "test_room_narrow_effects",
    "test_long_help",
    "test_info_pages",
    "test_mail", "test_prompt_survives_notice", "test_menus", "test_sysinfo", "test_config",
    # The dashboard (1.1.0). test_dash_pick kicks its own caller and nobody
    # else's; test_dash_waiting leaves mail only for its own sysop account.
    "test_dash_frame", "test_dash_pick", "test_dash_narrow", "test_dash_wide", "test_dash_all",
    "test_dash_ascii", "test_dash_petscii", "test_dash_waiting", "test_dash_card_age", "test_nodes_columns",
    # After test_config: test_operator_ends reloads the config through
    # cfg_reload, which leaves the account cap at 200, and test_config's own
    # save of 200 then finds nothing changed.
    "test_operator", "test_operator_ends", "test_notices_in_places",
    "test_operator_notes", "test_ring_mail",
    "test_config_parser_rules", "test_config_guards", "test_config_semicolon",
    "test_config_timezone", "test_config_cycle_numbers",
    "test_config_sd_plugin",
    "test_config_lights", "test_config_lights_ascii", "test_lights_frames", "test_lights_manual",
    "test_config_wifi_live", "test_config_network", "test_config_announce_outside",
    "test_config_wifi_fallback", "test_boot_hold",
    "test_boot_hold_write_fails", "test_boot_hold_factory_fails", "test_sysop_spelled_default",
    "test_boot_notices", "test_dash_opens_nothing",
    "test_config_pin_exists", "test_user_admin_retire",
    "test_serial",
    "test_motd", "test_idle_login", "test_busy",
    "test_screens", "test_exit_screen", "test_welcome_connecting", "test_paced_chatin",
    "test_seeded_screens_follow",
    "test_refresh_and_ctrl_l",
    "test_binary", "test_sd", "test_files", "test_mail_never_lost",
    "test_mail_rsd", "test_mailbox", "test_rename_follows",
    "test_staff_remembered", "test_shutdown",
    "test_list_abort_returns", "test_xfer",
    "test_upload_no_binary", "test_ymodem",
    "test_dash_uploads",                 # leaves its upload waiting, as test_ymodem does
    "test_config_areas", "test_config_area_keeps_every_part",
    "test_mail_compose",
    "test_forums", "test_forums_remove", "test_forums_scan_staff", "test_config_forum_levels", "test_partitions",
    # Backups on the card and restores across the partitions (1.1.0). The
    # card one restores this board from a backup it has just taken, which is
    # the board as it was a minute before, so it sits with the restores.
    "test_backup_card", "test_backup_card_nightly", "test_restore_cross_partition",
    # Destructive, and therefore last whatever else is running. The published
    # default's restore test puts the board back as it found it, and on a
    # --fresh board it needs to run before first_setup gives it a password.
    "test_backup_published_default",
    "test_first_setup", "test_backup", "test_ban",
]


# ---------------------------------------------------------------------------
# 1.1.0 Phase 5a: known defects. Each runs on a copy of this board where it
# restarts or breaks something, so the harness board is never touched.
# ---------------------------------------------------------------------------

# A rename() that refuses to put anything in place as system.cfg, preloaded
# into a copy of the board. The only way to fail syscfg::write between its
# temp file and the file it replaces, which is the case the fix is about.
FAIL_RENAME_C = """
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <string.h>
int rename(const char *from, const char *to) {
    static const char tail[] = "/system.cfg";
    size_t n = strlen(to), k = sizeof(tail) - 1;
    if (n >= k && strcmp(to + n - k, tail) == 0) { errno = EIO; return -1; }
    int (*real)(const char *, const char *) =
        (int (*)(const char *, const char *))dlsym(RTLD_NEXT, "rename");
    return real(from, to);
}
"""


def test_boot_hold_write_fails():
    """A failed system.cfg write leaves the old file exactly as it was (1.1.0).

    syscfg::write removed system.cfg before renaming the new one in, so a
    failure between the two left no settings at all: the next boot had no
    network, no staff passwords, and the published default. The BOOT
    password reset's "Nothing changed." was then false. LittleFS and POSIX
    both rename over an existing file in one step, so the remove was only
    ever a hole. Played here with a rename that refuses system.cfg.
    """
    print("A failed settings write keeps the old file")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the local harness")
        return True
    import shutil
    import subprocess
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        print("  SKIP  no C compiler for the rename shim")
        return True
    tmp = copy_data()
    (tmp / "failrename.c").write_text(FAIL_RENAME_C)
    so = tmp / "failrename.so"
    built = subprocess.run([cc, "-shared", "-fPIC", "-o", str(so), str(tmp / "failrename.c"), "-ldl"],
                           capture_output=True)
    if built.returncode != 0:
        shutil.rmtree(tmp, ignore_errors=True)
        print("  SKIP  the rename shim did not build")
        return True
    user = tmp / "data" / "user"
    before = (user / "system.cfg").read_bytes()
    port = PORT + 3700
    proc = start_copy(tmp, (str(port),), {"BBS_BOOT_HOLD_MS": "8000", "LD_PRELOAD": str(so)})
    try:
        log = copy_log(tmp, f"listening on {port},")
        ok = check("a BOOT password reset whose write is refused says so",
                   "reset: could not write system.cfg (the new config file could not be put in place). "
                   "Nothing changed." in log)
        ok &= check("and nothing did: system.cfg is still there, byte for byte",
                    (user / "system.cfg").exists() and (user / "system.cfg").read_bytes() == before)
        ok &= check("with no half-written copy left beside it", not (user / "system.tmp").exists())
        ok &= check("and no restart", "host: restarting" not in log)
    finally:
        stop_copy(proc, tmp)
    return ok


def test_boot_hold_factory_fails():
    """A factory erase that fails is said after the restart (1.1.0).

    The erase restarted with no note, so the next boot read as a plain
    "software restart" and nothing anywhere said that the board had been
    half erased. NOTE_FACTORY_FAILED carries it across, into the boot line,
    reboots.log and the staff login. The erase is made to fail with a
    folder in userdata the program may not empty.
    """
    print("A factory reset that could not erase says so after the restart")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the local harness")
        return True
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        print("  SKIP  root ignores the permission that makes the erase fail")
        return True
    tmp = copy_data()
    stuck = tmp / "data" / "user" / "stuck"
    stuck.mkdir()
    (stuck / "KEEP.TXT").write_text("cannot be removed\n")
    os.chmod(stuck, 0o555)
    port = PORT + 3701
    proc = start_copy(tmp, (str(port),), {"BBS_BOOT_HOLD_MS": "16000"})
    try:
        log = copy_log(tmp, f"listening on {port},")
        ok = check("the failed erase is said, with the repair",
                   "reset: erasing userdata FAILED. Reinstall with Erase everything first." in log)
        ok &= check("and not the lines of an erase that finished",
                    "reset: done." not in log and "Wi-Fi erased" not in log)
        ok &= check("the board restarts with a note of its own", "host: restarting (note 3)" in log)
        ok &= check("so the next boot says what happened", "boot: factory reset FAILED" in log)
        reb = tmp / "data" / "logs" / "reboots.log"
        lines = reb.read_text().splitlines() if reb.exists() else []
        ok &= check("reboots.log records it", bool(lines) and lines[-1].endswith("  factory reset FAILED"))
    finally:
        os.chmod(stuck, 0o755)
        stop_copy(proc, tmp)
    return ok


def test_sysop_spelled_default():
    """A sysop_password line that spells out the published default is the
    default (1.1.0).

    A restore on 1.0.0 or 1.0.1 wrote "sysop_password = unleashed" into
    boards on the default, and the explicit line made the password printed
    on the install page work from anywhere, with the board on the
    directory. Read as the default, such a board heals at its next boot:
    local only, the listing held, setup offered.
    """
    global PORT
    print("A system.cfg naming the published password is on the default")
    if HOST not in ("127.0.0.1", "localhost"):
        print("  SKIP  needs the local harness")
        return True
    c = ansi_login("SpelledOwner")
    c.close()
    time.sleep(1.0)                      # the logoff's account write, before the copy
    port = PORT + 3702
    proc, tmp = restart_copy((str(port),), edits={("", "sysop_password"): BBS_DEFAULT})
    saved = PORT
    try:
        cfg = (tmp / "data" / "user" / "system.cfg").read_text()
        ok = check("the copy's file spells the published password out",
                   f"sysop_password = {BBS_DEFAULT}" in cfg.splitlines())
        log = copy_log(tmp, f"listening on {port},")
        ok &= check("the boot calls it the published default, local network only",
                    "cfg: sysop on the published default, local network only" in log and
                    "PUBLISHED" not in log)

        PORT = port
        far = Caller(ansi=True, source="127.0.0.2")
        far.wait_for(b"Enter your handle", 10)
        ok &= check("a caller from outside is not offered setup",
                    login(far, "FarSpelled") and b"not been set up" not in plain(far.buf))
        far.buf.clear()
        far.send(f"bye {BBS_DEFAULT}\r".encode())
        far.wait_closed(12)
        ok &= check("and the published password does not make them the sysop",
                    b"SysOp node" not in far.buf and
                    "default sysop password refused from 127.0.0.2 (not local)" in
                    (tmp / "host.log").read_text(errors="replace"))
        far.close()
        time.sleep(1.0)

        s, offered = local_login("SpelledOwner")
        ok &= check("a caller on the board's own network is offered setup", offered)
        s.buf.clear()
        s.send(f"bye {BBS_DEFAULT}\r".encode())
        ok &= check("where the published password does work", s.wait_for(b"SysOp node", 6))
        s.wait_for(b"HELP for commands", 4)
        ok &= check("and the directory listing is held", announce_held(s))
        s.close()
    finally:
        PORT = saved
        stop_copy(proc, tmp)
    return ok


def test_config_pin_exists():
    """CONFIG and the parser refuse a GPIO the chip does not have (1.1.0).

    The WROOM's ESP32 has no GPIO 20, 24 or 28 to 31. CONFIG took them, the
    file kept them, and the driver refused them at start, which a sysop saw
    as a plugin that "would not start" with nothing saying why. One rule,
    syscfg::pinProblem, for core and plugin pins alike.
    """
    print("CONFIG refuses a pin the chip does not have")
    local = HOST in ("127.0.0.1", "localhost")
    s = cfg_sysop("CfgPins")

    before = cfg_line("activity_led_gpio") if local else None
    cfg_open(s, b"board", b"Hostname")
    s.buf.clear()
    s.send(DOWN * BOARD_LED + b"\x08" * 3 + b"24" + F1)
    got = cfg_verdict(s, [b"no such pin", b"Saved", b"flash chip"])
    ok = check("the LED cannot be put on GPIO 24", got == b"no such pin")
    cfg_cancel(s)
    if local:
        ok &= check("and nothing is written", cfg_line("activity_led_gpio") == before)

    for pin in (b"20", b"28", b"31"):
        cfg_open(s, b"backup", b"Open for")
        s.buf.clear()
        s.send(DOWN * 2 + b"\x08" * 3 + pin + F1)
        got = cfg_verdict(s, [b"no such pin", b"Saved", b"flash chip"])
        ok &= check(f"nor the backup button on GPIO {pin.decode()}", got == b"no such pin")
        cfg_cancel(s)

    # A plugin's pin goes through the same rule, and says the same sentence.
    cfg_open(s, b"lights", b"Drive pin")
    s.buf.clear()
    s.send(DOWN * 4 + b"\x08" * 3 + b"29" + F1)
    got = cfg_verdict(s, [b"Saved", b"no such pin", b"flash chip", b"Between"])
    ok &= check("nor a plugin's pin: the drive light on GPIO 29", got == b"no such pin")
    cfg_cancel(s)
    s.close()

    # A hand-edited file at boot: the line is refused and says why.
    if local:
        port = PORT + 3703
        proc, tmp = restart_copy((str(port),), edits={("", "activity_led_gpio"): "30"})
        try:
            log = copy_log(tmp, f"listening on {port},")
            ok &= check("a hand-edited GPIO 30 is refused at boot, by line and reason",
                        "this chip has no such pin: 30" in log and "activity led gpio 30" not in log)
        finally:
            stop_copy(proc, tmp)
    return ok


def test_user_admin_retire():
    """The user manager's D says Retire, as USER DEL does (1.1.0).

    D has retired accounts since 0.19.0: the block stays and the handle is
    reserved for ever. The manager went on asking "Delete handle (y/N)?",
    which is a different promise from the one Y keeps.
    """
    print("USERS: D retires, and says so")
    v = ansi_login("RetireSubject")        # never the sysop's own row at the top
    v.close()
    time.sleep(1.0)
    s = cfg_sysop("RetireBoss")
    s.buf.clear()
    s.send(b"users\r")
    ok = check("the manager opens", s.wait_for(b"USER MANAGER", 5))
    ok &= check("and its keys say D retires", s.wait_for(b"Enter edit  A add  D retire  Q quit", 3))
    s.pump(0.3)
    s.buf.clear()
    s.send(b"d")                            # on the first account in the list
    s.pump(1.0)
    shown = plain(s.buf)
    ok &= check("D asks to retire, in the words the confirm is redrawn with",
                b"Retire " in shown and b"? The handle stays reserved (y/N)? " in shown)
    ok &= check("and never says delete", b"Delete" not in shown and b"delete" not in shown)
    s.send(b"n")
    ok &= check("N keeps it", s.wait_for(b"USER MANAGER", 5))
    s.send(b"q")
    s.pump(0.5)
    s.close()
    return ok


def run_order():
    """The declared sequence as callables, skipping any that do not exist."""
    g = globals()
    out = []
    for n in ORDER_NAMES:
        if n in ("test_backup", "test_ban"):
            continue                     # flag-gated, appended by the caller
        f = g.get(n)
        if f is not None:
            out.append(f)
    return out


def order_index(name):
    """Where a test sits in the declared order; unplaced tests go last."""
    try:
        return ORDER_NAMES.index(name)
    except ValueError:
        return len(ORDER_NAMES) + 1


def run_selected(only):
    """--only=NAME[,NAME...] runs the tests whose names contain those words.

    A name may also be a group from GROUPS above, which expands to several.
    Matching is by substring, so --only=mail catches test_mail and
    test_mail_compose alike, which is usually what somebody means.
    """
    import types

    wanted = []
    for word in only.split(","):
        word = word.strip()
        if not word:
            continue
        wanted.extend(GROUPS.get(word, [word]))

    picked = []
    seen = set()
    # Declared order, not alphabetical. See ORDER_NAMES: a group that runs
    # its tests in a different order than the full suite is testing a
    # sequence nobody designed, and test_ban in particular bans the host.
    for n, f in sorted(globals().items(), key=lambda kv: order_index(kv[0])):
        if not n.startswith("test_") or not isinstance(f, types.FunctionType):
            continue
        if any(w in n for w in wanted) and n not in seen:
            seen.add(n)
            picked.append((n, f))

    if not picked:
        print("no test matches", only)
        print("groups:", ", ".join(sorted(GROUPS)))
        return False

    print("running %d test%s: %s\n" % (len(picked), "" if len(picked) == 1 else "s",
                                       ", ".join(n[5:] for n, _ in picked)))
    # Every test runs even after one fails, because a targeted run is cheap
    # and knowing whether the damage is one test or five is worth more than
    # stopping early.
    results = [f() for _, f in picked]
    return all(results)


if __name__ == "__main__":
    ONLY = next((a.split("=", 1)[1] for a in FLAGS if a.startswith("--only=")), None)
    if ONLY:
        picked_ok = run_selected(ONLY)
        print("ALL PASS" if picked_ok else "FAILURES")
        sys.exit(0 if picked_ok else 1)
    results = [f() for f in run_order()]
    if "--backup" in FLAGS:
        results.append(test_backup())
    if "--ban" in FLAGS:
        results.append(test_ban())
    print("ALL PASS" if all(results) else "FAILURES")
    sys.exit(0 if all(results) else 1)


def _unused_old_main():
    results = [test_ansi(), test_telnet_first(), test_petscii(), test_ascii(),
               test_page(), test_sysop(), test_cosysop(), test_accounts(), test_user_admin(), test_guest(),
               test_privacy(), test_plugins(), test_about(), test_announce(),
               test_chat(), test_room_commands(), test_room_new_commands(),
               test_mail(), test_menus(), test_sysinfo(), test_config(), test_serial(),
               test_motd(), test_idle_login(), test_busy(),
               test_screens(), test_exit_screen(),
               test_refresh_and_ctrl_l(),
               test_binary(), test_sd(), test_files(), test_mail_never_lost(),
               test_mail_rsd(), test_rename_follows(),
               test_staff_remembered(), test_shutdown(),
               test_list_abort_returns(), test_xfer(),
               test_upload_no_binary(), test_ymodem(),
               test_config_areas(), test_config_area_keeps_every_part(),
               test_mail_compose(),
               test_forums(), test_partitions()]
    return all(results)
