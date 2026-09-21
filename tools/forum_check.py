#!/usr/bin/env python3
# ===========================================================================
#  µnleashed BBS
#  Electronic freedom on a microcontroller.
# ===========================================================================
#
# File:         tools/forum_check.py
# Module:       Tools / format verification
#
# Purpose:      Read a forum's files from the outside and say whether they
#                   are what the format says they are. Can also build a forum
#                   by hand for the board to read back.
#
# Design:       **Shares no code with the board, on purpose.** A format
#               tested only by the program that wrote it is a format that
#               agrees with itself. This project has paid for that lesson
#               twice: tools/testclient.py was written alongside the board
#               and honoured its telnet quirks, so two real bugs passed every
#               test while the hardware failed; and lrzsz found an XMODEM bug
#               precisely because it was somebody else's implementation.
#
#               So the offsets below are typed in from PLAN-FORUMS.md rather
#               than imported from anywhere. If the board changes and this
#               is not changed with it, that disagreement is the point.
#
# Usage:        python3 tools/forum_check.py <dir>           check a forum
#               python3 tools/forum_check.py <dir> --build   make a test forum
#
# Targets:      any Python 3, run on a laptop with the card in hand
# See also:     PLAN-FORUMS.md, src/plugins/forums.cpp
#
# Copyright 2026 - Robert Mech
# License:      GNU General Public License v2 or later
# SPDX-License-Identifier: GPL-2.0-or-later
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <https://www.gnu.org/licenses/>. The full
# text is in the LICENSE file at the top of this repository.
# ===========================================================================
"""Verify, or build, a µnleashed BBS forum on disk."""

import os
import sys
import time

# The format, typed in from the plan rather than imported. These numbers are
# the contract; if the board disagrees with them, one of the two is wrong and
# this tool exists to say so.
REC = 128
MAGIC = b"#UBF1"

FIELDS = [
    #  name           offset  width  kind
    ("number",             0,     6, "dec"),
    ("flags",              7,     2, "raw"),
    ("author id",         10,     8, "hex"),
    ("author handle",     19,    20, "text"),
    ("epoch",             40,    10, "dec"),
    ("segment",           51,     4, "dec"),
    ("offset",            56,     6, "dec"),
    ("length",            63,     4, "dec"),
    ("subject hash",      68,     8, "hex"),
    ("subject",           77,    49, "text"),
]
TABS = [6, 9, 18, 39, 50, 55, 62, 67, 76]      # every field is tab separated
CRLF = 126

fails = 0
checks = 0


def check(ok, what):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
        print("  FAIL  " + what)
    return ok


def field(rec, name):
    for n, at, w, kind in FIELDS:
        if n == name:
            raw = rec[at:at + w]
            if kind == "dec":
                return int(raw.decode("ascii", "replace").strip() or "0")
            if kind == "hex":
                return int(raw.decode("ascii", "replace").strip() or "0", 16)
            return raw.decode("ascii", "replace").rstrip()
    raise KeyError(name)


def check_forum(path):
    """Read INDEX.TXT and the bodies it points at, and verify all of it."""
    idx = os.path.join(path, "INDEX.TXT")
    if not os.path.exists(idx):
        print("no INDEX.TXT in " + path)
        return 1

    data = open(idx, "rb").read()
    print("%s: %d bytes" % (idx, len(data)))

    # The guarantee the whole design rests on: the file is a whole number of
    # fixed records, so message N is at N * REC and nothing has to be
    # searched for. A file that is not a multiple of the record size means
    # something wrote a short record, and every message after it is at the
    # wrong offset.
    check(len(data) % REC == 0,
          "index is not a whole number of %d byte records (%d left over)"
          % (REC, len(data) % REC))

    head = data[0:REC]
    check(head.startswith(MAGIC), "header does not start with %s" % MAGIC.decode())
    check(head[CRLF:CRLF + 2] == b"\r\n", "header does not end CR LF")

    hdr = {}
    for part in head.decode("ascii", "replace").split("\t"):
        if "=" in part:
            k, v = part.split("=", 1)
            hdr[k.strip()] = v.strip()
    newest = int(hdr.get("newest", "0") or 0)
    print("header: topic=%s newest=%s count=%s"
          % (hdr.get("topic", "?"), hdr.get("newest", "?"), hdr.get("count", "?")))

    total = len(data) // REC - 1
    check(newest <= total,
          "header says newest=%d but the file only holds %d messages" % (newest, total))

    subjects = {}
    live = 0
    for n in range(1, total + 1):
        rec = data[n * REC:(n + 1) * REC]
        where = "message %d" % n

        # The self-check the format builds in: the number written inside the
        # record must match the slot it is sitting in. They are redundant on
        # purpose, and the redundancy is what catches a misaligned write.
        num = field(rec, "number")
        if not check(num == n, "%s: record says it is message %d" % (where, num)):
            continue

        check(rec[CRLF:CRLF + 2] == b"\r\n", "%s: does not end CR LF" % where)
        for t in TABS:
            check(rec[t:t + 1] == b"\t",
                  "%s: no tab at offset %d, found %r" % (where, t, rec[t:t + 1]))

        flags = field(rec, "flags")
        check(flags[0] in ".X", "%s: bad live flag %r" % (where, flags[0]))
        check(len(flags) > 1 and flags[1] in ".!",
              "%s: bad pinned flag" % where)
        if flags[0] == ".":
            live += 1

        h = field(rec, "subject hash")
        subj = field(rec, "subject")
        # Grouping is by hash and never by text, so the same hash appearing
        # with two different subject strings is expected and fine (a renamed
        # subject). The reverse is what would be wrong.
        subjects.setdefault(h, set()).add(subj)

        seg, ofs, ln = field(rec, "segment"), field(rec, "offset"), field(rec, "length")
        if ln:
            body = os.path.join(path, "M%04d.TXT" % seg)
            if check(os.path.exists(body), "%s: body segment %s is missing" % (where, body)):
                size = os.path.getsize(body)
                check(ofs + ln <= size,
                      "%s: body runs past the end of segment %d (%d+%d > %d)"
                      % (where, seg, ofs, ln, size))

    print("%d messages, %d live, %d distinct subjects" % (total, live, len(subjects)))

    # The header's own count, against what the records actually say. These
    # disagreeing is how a per-caller number once got written into a
    # board-wide file: the post path incremented the reader's unread figure
    # and then saved that as the forum's total. Nothing a caller sees would
    # have shown it, because the header is only read at start.
    said = int(hdr.get("count", "-1") or -1)
    check(said == live,
          "header says count=%d but %d records are live" % (said, live))

    # A subject text used by two different hashes would mean grouping had
    # split a conversation in two, which is the failure the hash exists to
    # prevent. Worth naming rather than only counting.
    seen_text = {}
    for h, texts in subjects.items():
        for txt in texts:
            if txt in seen_text and seen_text[txt] != h:
                check(False, "subject %r appears under two hashes, %08X and %08X"
                      % (txt, seen_text[txt], h))
            seen_text[txt] = h

    print("\n%d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


def build_forum(path, count=12):
    """Write a forum by hand for the board to read back.

    The other half of testing a format from the outside: not only reading
    what the board wrote, but handing the board something it did not write
    and seeing whether it agrees.
    """
    os.makedirs(path, exist_ok=True)
    subjects = ["20m antennas", "20m tips", "Coffee"]
    bodies = []
    recs = []

    for n in range(1, count + 1):
        subj = subjects[n % len(subjects)]
        # FNV-1a over the folded subject, same as the board. Typed in from
        # the plan; if the board's hash ever changes, this disagreeing is
        # exactly what should happen.
        h = 2166136261
        for ch in subj.strip().lower():
            h ^= ord(ch)
            h = (h * 16777619) & 0xFFFFFFFF
        body = ("Message %d about %s.\n" % (n, subj)).encode()
        ofs = sum(len(b) for b in bodies)
        bodies.append(body)

        rec = bytearray(b" " * REC)
        def put(at, w, s):
            s = s.encode() if isinstance(s, str) else s
            rec[at:at + len(s[:w])] = s[:w]
        put(0, 6, "%06d" % n)
        rec[7] = ord(".")
        rec[8] = ord(".")
        put(10, 8, "%08X" % (n % 5 + 1))
        put(19, 20, "Tester")
        put(40, 10, "%010d" % int(time.time()))
        put(51, 4, "0000")
        put(56, 6, "%06d" % ofs)
        put(63, 4, "%04d" % len(body))
        put(68, 8, "%08X" % h)
        put(77, 49, subj)
        for t in TABS:
            rec[t] = ord("\t")
        rec[CRLF] = ord("\r")
        rec[CRLF + 1] = ord("\n")
        assert len(rec) == REC
        recs.append(bytes(rec))

    head = bytearray(b" " * REC)
    text = "#UBF1\ttopic=%s\tcount=%06d\tnewest=%06d\t" % (
        os.path.basename(path.rstrip("/")), count, count)
    head[0:len(text)] = text.encode()
    head[CRLF] = ord("\r")
    head[CRLF + 1] = ord("\n")

    with open(os.path.join(path, "INDEX.TXT"), "wb") as f:
        f.write(bytes(head))
        for r in recs:
            f.write(r)
    with open(os.path.join(path, "M0000.TXT"), "wb") as f:
        for b in bodies:
            f.write(b)
    print("wrote %d messages to %s" % (count, path))
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        print("\nusage: forum_check.py <dir> [--build [n]]")
        sys.exit(2)
    target = sys.argv[1]
    if "--build" in sys.argv:
        i = sys.argv.index("--build")
        n = int(sys.argv[i + 1]) if len(sys.argv) > i + 1 else 12
        sys.exit(build_forum(target, n))
    sys.exit(check_forum(target))
