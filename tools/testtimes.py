#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/testtimes.py
Module:       Tools / test timing

Purpose:      Reads the output of one or more tools/testclient.py runs (the
                 out.txt a harness run leaves) and reports what each test
                 cost. run_test prints "  TEST  <name>" when a test starts
                 and "  TIME  <name> <seconds>" when it ends, so every check
                 line in between belongs to that test.

                 Three uses:
                   - the slowest-test report (Rob, 1.1.2: "test coverage is
                     great but it shouldn't take up as long as the code
                     writing"), where the time goes first;
                   - the check list, "<test> | PASS|FAIL | <check>" per line,
                     which is how a change to the suite is proven not to
                     have made it weaker: the same checks pass before and
                     after (--checks, then diff the two files);
                   - tools/test-times.txt, the kept per-test figures that
                     harness.sh --jobs packs its lanes by (--save).

Usage:        python3 tools/testtimes.py OUT.txt [OUT.txt ...]
                  prints the 20 slowest tests and the total
              python3 tools/testtimes.py --top 40 OUT.txt
              python3 tools/testtimes.py --checks OUT.txt > checks.txt
              python3 tools/testtimes.py --save tools/test-times.txt OUT.txt ...
                  writes "<test> <seconds>", the largest figure seen for a
                  test across the files given (a card run and a cardless
                  run of the same test differ; the pack wants the worse)

Libraries:    Python 3 standard library only
Targets:      developer PC, Python 3
See also:     tools/harness.sh, tools/testclient.py, tools/parallel.py

Copyright 2026 - Robert Mech
License:      GNU General Public License v3 or later
SPDX-License-Identifier: GPL-3.0-or-later

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>. The full
text is in the LICENSE file at the top of this repository.
===========================================================================
"""

import re
import sys

TEST_RE = re.compile(r"^  TEST  (test_\w+)\s*$")
TIME_RE = re.compile(r"^  TIME  (test_\w+) ([0-9.]+)\s*$")
# BBS_WAIT_STATS=1 runs: the fixed waiting inside a test (time.sleep, and
# pump(secs) outside a wait loop).
WAIT_RE = re.compile(r"^  WAIT  (test_\w+) sleep ([0-9.]+) pump ([0-9.]+)\s*$")
CHECK_RE = re.compile(r"^  (PASS|FAIL|SKIP)  (.*?)\s*$")
# A check name that carries a figure measured on the day (a time, a count of
# bytes, a port) would make two identical runs differ. Digits are folded to
# one '#' for the comparison only.
DIGITS = re.compile(r"\d+(?:[.,]\d+)*")


def fixed_waits(path):
    """{test: seconds of fixed waiting} from a BBS_WAIT_STATS=1 run; the
    worst figure when a test appears more than once."""
    out = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = WAIT_RE.match(line.rstrip("\n"))
            if m:
                secs = float(m.group(2)) + float(m.group(3))
                out[m.group(1)] = max(out.get(m.group(1), 0.0), secs)
    return out


def parse(path):
    """[(test, seconds, [(verdict, check)])] in the order the file ran them."""
    out = []
    cur = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            m = TEST_RE.match(line)
            if m:
                cur = [m.group(1), None, []]
                out.append(cur)
                continue
            m = TIME_RE.match(line)
            if m:
                if cur is None or cur[0] != m.group(1):
                    cur = [m.group(1), None, []]
                    out.append(cur)
                cur[1] = float(m.group(2))
                cur = None
                continue
            m = CHECK_RE.match(line)
            if m:
                name = cur[0] if cur else "(outside a test)"
                if cur is None:
                    cur = [name, None, []]
                    out.append(cur)
                cur[2].append((m.group(1), m.group(2)))
    return out


def fold(check):
    return DIGITS.sub("#", check)


def main(argv):
    top = 20
    mode = "report"
    save = None
    files = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--top":
            top = int(argv[i + 1])
            i += 2
            continue
        if a == "--checks":
            mode = "checks"
        elif a == "--save":
            mode = "save"
            save = argv[i + 1]
            i += 2
            continue
        else:
            files.append(a)
        i += 1
    if not files:
        print(__doc__.split("Usage:")[1].split("Libraries:")[0])
        return 2

    runs = [(p, parse(p)) for p in files]

    if mode == "checks":
        for _, tests in runs:
            for name, _, checks in tests:
                for verdict, check in checks:
                    print(f"{name} | {verdict} | {fold(check)}")
        return 0

    worst = {}
    for _, tests in runs:
        for name, secs, _ in tests:
            if secs is not None:
                worst[name] = max(worst.get(name, 0.0), secs)

    if mode == "save":
        with open(save, "w", encoding="utf-8", newline="\n") as f:
            f.write("# Per-test wall time in seconds, the worse of the runs it was\n")
            f.write("# measured from. tools/testtimes.py --save writes it and\n")
            f.write("# tools/parallel.py packs harness.sh --jobs lanes by it.\n")
            for name in sorted(worst):
                f.write(f"{name} {worst[name]:.1f}\n")
        print(f"wrote {len(worst)} figures to {save}")
        return 0

    for path, tests in runs:
        timed = [(n, s) for n, s, _ in tests if s is not None]
        total = sum(s for _, s in timed)
        npass = sum(1 for _, _, c in tests for v, _ in c if v == "PASS")
        nfail = sum(1 for _, _, c in tests for v, _ in c if v == "FAIL")
        fixed = fixed_waits(path)
        print(f"{path}: {len(timed)} tests, {total:.0f} s in tests "
              f"({total / 60:.1f} min), {npass} passed, {nfail} failed")
        if fixed:
            print(f"  fixed waiting (sleep and pump) {sum(fixed.values()):.0f} s of it")
            print(f"  {'seconds':>8}  {'share':>6}  {'fixed':>6}  test")
        else:
            print(f"  {'seconds':>8}  {'share':>6}  test")
        for n, s in sorted(timed, key=lambda t: -t[1])[:top]:
            share = 100 * s / total if total else 0
            if fixed:
                print(f"  {s:8.1f}  {share:5.1f}%  {fixed.get(n, 0.0):6.1f}  {n}")
            else:
                print(f"  {s:8.1f}  {share:5.1f}%  {n}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
