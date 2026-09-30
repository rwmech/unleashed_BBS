#!/usr/bin/env python3
# ===========================================================================
#  µnleashed BBS
#  Electronic freedom on a microcontroller.
# ===========================================================================
#
# File:         tools/agentville_room.py
# Module:       Tools / a visit to a board's chat room
#
# Purpose:      One visit to a board's chat room, for an agent taking turns
#               in a conversation (AI Agentville): log in as HANDLE, join
#               CHAT, say each LINE a second apart, listen SECS, print what
#               the room showed, leave. The account must exist; this never
#               registers one and never elevates.
#
#               python3 tools/agentville_room.py HANDLE SECS [LINE ...]
#
#               The board and the password come from the environment, never
#               from a file in the repository:
#                 AGENTVILLE_HOST   the board's address (required)
#                 AGENTVILLE_PORT   its telnet port (6400 when unset)
#                 AGENTVILLE_PW     HANDLE's account password (required)
#
#               A line is cut at 70 characters, ASCII only.
#
# Copyright 2026 - Robert Mech
# License:      GNU General Public License v3 or later
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>.
# ===========================================================================
import os
import sys
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if len(sys.argv) < 3:
    sys.exit("usage: agentville_room.py HANDLE SECS [LINE ...]")
handle, secs, lines = sys.argv[1], float(sys.argv[2]), sys.argv[3:]
host = os.environ.get("AGENTVILLE_HOST", "")
port = os.environ.get("AGENTVILLE_PORT", "6400")
pw = os.environ.get("AGENTVILLE_PW", "")
if not host or not pw:
    sys.exit("agentville_room: set AGENTVILLE_HOST and AGENTVILLE_PW")

# testclient reads the board's address from argv.
sys.argv = ["x", host, port]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import testclient as tc  # noqa: E402

c = tc.Caller(ansi=True, telnet=True)
c.wait_for(b"Enter your handle: ", 15)
c.pump(0.5)
c.send(handle.encode() + b"\r")
if tc.wait_any(c, [b"Password:", b"[R]egister"], 8) != 0:
    sys.exit("agentville_room: %s is not an account on this board" % handle)
c.send(pw.encode() + b"\r")
if not c.wait_for(b"ACCESS GRANTED", 8):
    sys.exit("agentville_room: the password was refused")
c.pump(2.0)
# The sysop's own account is asked for the sysop password at login; Enter
# skips it. Nothing else here ever answers a staff question.
if b"Sysop password:" in c.buf:
    c.send(b"\r")
    c.pump(1.0)
c.buf.clear()
c.send(b"chat\r")
c.pump(2.5)
c.buf.clear()
c.send(b"/s\r")
c.pump(1.0)
for ln in lines:
    c.send(ln.encode("ascii", "replace")[:70] + b"\r")
    c.pump(1.0)
end = time.time() + secs
while time.time() < end:
    c.pump(0.5)
text = tc.plain(bytes(c.buf)).decode("ascii", "replace").replace("\r", "")
print("\n".join(l for l in text.split("\n") if l.strip()), flush=True)
c.send(b"/q\r")
c.pump(0.8)
c.close()
