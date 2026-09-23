#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/board_smoke.py
Module:       Tools / on-board smoke test

Purpose:      A read-only walk through a live board, for checking a release
              on real hardware where the full suite must never run. The
              suite rewrites CONFIG (Wi-Fi included), bans the caller's
              address on purpose and registers dozens of accounts; this
              changes nothing. It logs in (as a guest, or as one account),
              runs a fixed list of commands that only read, times each
              reply, and logs off.

              What it reports, per command: time to the first byte, time
              until the prompt came back, bytes received, and whether the
              board answered, said "Unknown command" (absent, e.g. FILES on
              a board with no card, or a staff screen for a caller without
              the rank), or never came back.

Usage:        python3 tools/board_smoke.py HOST [PORT]
                  [--handle NAME --password-file F]  log in as this account;
                                                  it is registered if new
                                                  (--password PW also works,
                                                  but shows on the command line)
              Without --handle it logs in as a guest and creates nothing.
              With an account, staff screens (SYS, MEM, NODES) are tried
              and reported as absent when the rank does not allow them.

Safety:       The command list is fixed below and holds nothing that
              writes. No password is ever guessed, so nothing counts toward
              a ban. A setup offer on an unconfigured board is declined
              with ESC.

Libraries:    Python 3 standard library; tools/testclient.py beside it
See also:     tools/testclient.py (the full suite, host only)

Copyright 2026 - Robert Mech
License:      GNU General Public License v2 or later
SPDX-License-Identifier: GPL-2.0-or-later
===========================================================================
"""

import argparse
import sys
import time
from pathlib import Path

# testclient reads HOST and PORT from argv at import; give it none, then set
# them from our own arguments.
_argv, sys.argv = sys.argv, sys.argv[:1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import testclient as tc                                   # noqa: E402
sys.argv = _argv

# Read-only commands, in order. (verb, what it is, how to leave it)
# "leave" is sent once the command has drawn, for the ones that take the
# session: the chat room and the mailbox are places, not commands.
COMMANDS = [
    ("?",      "help menu",            None),
    ("WHO",    "who is on",            None),
    ("TIME",   "time left",            None),
    ("LAST",   "caller log",           None),
    ("CALLS",  "calls by hour",        None),
    ("INFO",   "information pages",    None),
    ("CODES",  "message codes screen", None),
    ("ABOUT",  "about screen",         None),
    ("CHAT",   "chat room",            b"/q\r"),
    ("MAIL",   "mailbox",              b"q"),
    ("FORUMS", "forums (card only)",   b"q"),
    ("FILES",  "file areas (card only)", b"q"),
    ("MEM",    "memory",               None),
    ("SYS",    "system screen (staff)", None),
    ("NODES",  "all lines (staff)",    None),
]


def settle(c, secs=8):
    """Turn pages and stop lists until the prompt is back and quiet.
    Returns True when the prompt came back."""
    end = time.time() + secs
    quiet = 0
    while time.time() < end:
        seen = tc.plain(c.buf)
        tail = bytes(c.buf)[-96:]
        # Answer a pause once, then forget it: the text stays in the buffer
        # after the answer, and answering it again types a stray key at the
        # prompt (the second run of this script sent "nINFO").
        if b"[More]" in seen[-40:]:
            c.buf.clear()
            c.send(b"n")
        elif b"Press SPACE" in seen[-60:] or b"PRESS SPACE" in seen[-60:]:
            c.buf.clear()
            c.send(b" ")
        before = len(c.buf)
        c.pump(0.25)
        if len(c.buf) == before and tc.PROMPT_RE.search(tail):
            quiet += 1
            if quiet >= 2:
                return True
        elif len(c.buf) != before:
            quiet = 0
    return tc.PROMPT_RE.search(bytes(c.buf)[-96:]) is not None


def at_prompt(c, secs=6):
    """Wait until the session sits at the command prompt with nothing more
    arriving. A command typed while a list is still paging has its first
    letter eaten as a page key (INFO arrives as NFO): the gotcha CLAUDE.md
    already records for the suite, and the first run of this script hit it."""
    return settle(c, secs)


def run_one(c, verb, leave):
    at_prompt(c)
    c.buf.clear()
    t0 = time.time()
    c.send(verb.encode() + b"\r")
    first = None
    while time.time() - t0 < 5 and first is None:
        c.pump(0.02)
        if c.buf:
            first = time.time() - t0
    # An unknown command is answered in place on the prompt line, and the
    # prompt is not drawn again, so waiting for one would only time out.
    if c.wait_for(b"Unknown command", 1.0):
        return first, time.time() - t0, len(c.buf), "absent"
    if leave:
        c.pump(1.0)
        c.send(leave)
    back = settle(c)
    done = time.time() - t0
    return first, done, len(c.buf), "ok" if back else "NO PROMPT"


def main():
    ap = argparse.ArgumentParser(description="Read-only smoke test of a live board")
    ap.add_argument("host")
    ap.add_argument("port", nargs="?", type=int, default=6400)
    ap.add_argument("--handle")
    ap.add_argument("--password")
    ap.add_argument("--password-file",
                    help="read the password from the first line of this file, "
                         "so it never appears on a command line")
    a = ap.parse_args()
    if a.password_file:
        a.password = Path(a.password_file).read_text(encoding="utf-8").splitlines()[0].strip()
    if a.handle and not a.password:
        sys.exit("--handle needs --password or --password-file (no default password on a real board)")
    tc.HOST, tc.PORT = a.host, a.port

    t0 = time.time()
    c = tc.Caller(ansi=True, utf8=True)
    detected = c.wait_for(b"DETECTED", 10)
    t_detect = time.time() - t0
    if not c.wait_for(b"Enter your handle", 15):
        sys.exit("no handle prompt")
    t_prompt = time.time() - t0

    if a.handle:
        if not tc.login(c, a.handle, a.password, wait_main=False):
            sys.exit("login failed")
    else:
        c.send(b"smoke%d\r" % (int(time.time()) % 10000))
        if tc.wait_any(c, [b"[G]uest", b"Guest"], 8) < 0:
            sys.exit("no guest option (guest access switched off?)")
        c.send(b"g")
    # An unconfigured board offers setup to a local caller: decline it.
    hit = tc.wait_any(c, [b"configuring", b"Main"], 10)
    if hit == 0:
        c.send(b"\x1b")
    settle(c, 12)
    login_s = time.time() - t0

    print("Board %s:%d  detected %s in %.2f s, handle prompt at %.2f s, logged in by %.2f s"
          % (a.host, a.port, "yes" if detected else "NO", t_detect, t_prompt, login_s))
    print()
    print("| Command | What | First byte | Prompt back | Bytes | Result |")
    print("|---|---|---:|---:|---:|---|")
    bad = 0
    for verb, what, leave in COMMANDS:
        first, done, n, verdict = run_one(c, verb, leave)
        if verdict == "NO PROMPT":
            bad += 1
        print("| %s | %s | %s | %.0f ms | %d | %s |" % (
            verb, what, "%.0f ms" % (first * 1000) if first is not None else "none",
            done * 1000, n, verdict))
    c.buf.clear()
    c.send(b"BYE\r")
    closed = c.wait_closed(30)
    print()
    print("Logged off: %s" % ("line closed" if closed else "LINE STILL OPEN after 30 s"))
    c.close()
    return 1 if bad or not closed else 0


if __name__ == "__main__":
    sys.exit(main())
