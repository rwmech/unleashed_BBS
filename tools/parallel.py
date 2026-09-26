#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/parallel.py
Module:       Tools / parallel test lanes

Purpose:      harness.sh --jobs N. Runs a selection of tools/testclient.py
                 as lanes side by side, N boards at a time, with and without
                 a card at once, and merges what they found into one summary.

                 Rob, 1.1.2: "test coverage is great but it shouldn't take
                 up as long as the code writing". The serial suite spends
                 nearly all of its time waiting, on the board's clock or on
                 its own sleeps, so running many of it at once costs little
                 more than running one.

                 A lane is one ordinary tools/harness.sh run: its own tag,
                 port, data directory, card and directory port, exactly the
                 isolation that was built for parallel agents. Within a lane
                 the tests keep the declared order (ORDER_NAMES), so a lane
                 is always a subsequence of the serial run, never a
                 reshuffle of it.

                 What a lane may not do to a test is written in
                 tools/testclient.py and read here through --plan:
                   ALONE     a board to itself (test_ban bans 127.0.0.1)
                   NEEDS     run after these, on the same board
                   REALTIME  times real seconds: a lane on the real clock
                   FRESH_TESTS    each also gets a --fresh board of its own
                   PROFILE_TESTS  each profile's tests also run on its build
                 Everything else is packed by its measured time
                 (tools/test-times.txt), longest first, onto lanes of about
                 equal length, and the lanes are started longest first.

                 Lanes run on the host's fast clock (BBS_FAST_TIMERS,
                 host/platform_host.cpp) unless --no-fast, or unless a lane
                 holds a REALTIME test.

                 Ports: a test starts copies of its board at fixed offsets
                 from the harness port (PORT + 3705 and so on), so two lanes
                 whose ports differ by the gap between two offsets can meet.
                 The offsets are read from testclient.py and each worker's
                 port is chosen so none of its ports is any other worker's.
                 A run claims one of two port blocks, 11000-21499 and
                 21500-31999, with a lock file, so two --jobs runs at once
                 cannot meet either: clear of the 6500-10899 a tag-derived
                 harness run can use, and under the kernel's ephemeral
                 range (32768 up). A lane's leftover boards are found by the
                 BBS_LANE it hands them, never by port or name.

Usage:        tools/harness.sh --jobs N [--only=... | --changed RANGE |
                                         --tests=a,b] [--card | --no-card]
              python3 tools/parallel.py --jobs N [same options]
                  --no-fast        every lane on the real clock
                  --fast=K         the clock factor for fast lanes (default 4)
                  --no-extra       leave out the --fresh and profile lanes
                  --solo           one test per lane, NEEDS ignored: how an
                                   order dependency is found (a test that
                                   fails alone and passes in the suite)
                  --lane-secs S    aim each lane at about S seconds
                  --tag BASE       lane tags are BASE-n0, BASE-c3 ... and the
                                   merged output is /tmp/bbs-BASE/out.txt
                  --test-timeout=N passed to every lane

Libraries:    Python 3 standard library only
Targets:      Linux host build (WSL), Python 3.8
See also:     tools/harness.sh, tools/testclient.py, tools/testtimes.py

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

import json
import os
import pathlib
import re
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))
import testtimes  # noqa: E402

DEFAULT_SECS = 40.0          # a test with no kept figure
LANE_OVERHEAD = 3.0          # a lane's board start and data copy
# Two blocks of ports, one --jobs run in each at a time, claimed with a lock
# file for as long as the run lasts. Two runs at once (two agents, or a run
# started while another is going) otherwise pick the same ports, and each
# one's clean-up kills the other's boards: the first try at this did exactly
# that, and it read as forty unrelated failures.
PORT_BLOCKS = [(11000, 21500), (21500, 32000)]
PROFILE_BIN = {"": "bbs_host", "s3": "bbs_host_s3", "fncam": "bbs_host_fncam",
               "espcam": "bbs_host_espcam"}


# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
class Opts:
    def __init__(self, argv):
        self.jobs = 8
        self.tag = "main"
        self.modes = ["n", "c"]           # no card, card
        self.fast = 4
        self.extra = True
        self.solo = False
        self.lane_secs = None
        self.select = []                  # --only= / --tests= passed through
        self.passthru = []                # --test-timeout= and the like
        i = 0
        while i < len(argv):
            a = argv[i]
            if a == "--jobs":
                self.jobs = int(argv[i + 1])
                i += 1
            elif a.startswith("--jobs="):
                self.jobs = int(a.split("=", 1)[1])
            elif a == "--tag":
                self.tag = argv[i + 1]
                i += 1
            elif a == "--card":
                self.modes = ["c"]
            elif a == "--no-card":
                self.modes = ["n"]
            elif a == "--no-fast":
                self.fast = 1
            elif a == "--fast":
                self.fast = 4
            elif a.startswith("--fast="):
                # The same reading as BBS_FAST_TIMERS: 0 off, 1 the default
                # factor, 2 and up that factor.
                k = int(a.split("=", 1)[1])
                self.fast = 1 if k <= 0 else 4 if k == 1 else min(k, 20)
            elif a == "--no-extra":
                self.extra = False
            elif a == "--solo":
                self.solo = True
            elif a == "--lane-secs":
                self.lane_secs = float(argv[i + 1])
                i += 1
            elif a.startswith("--only=") or a.startswith("--tests="):
                self.select.append(a)
            elif a in ("--backup", "--ban"):
                pass                      # a full --jobs run always has both
            elif a.startswith("--"):
                self.passthru.append(a)
            i += 1
        self.jobs = max(1, self.jobs)
        # harness.sh's default tag is "main"; lanes under it would share
        # /tmp/bbs-main with an ordinary run somebody starts meanwhile.
        if self.tag == "main":
            self.tag = "jobs"


# ---------------------------------------------------------------------------
# The plan
# ---------------------------------------------------------------------------
def load_plan(opts):
    args = [sys.executable, str(TOOLS / "testclient.py"), "--plan"] + opts.select
    if not opts.select:
        args += ["--backup", "--ban"]
    out = subprocess.run(args, capture_output=True, text=True, cwd=ROOT)
    if out.returncode != 0:
        sys.stderr.write(out.stdout + out.stderr)
        sys.exit(2)
    return json.loads(out.stdout)


def load_times():
    times = {}
    p = TOOLS / "test-times.txt"
    if p.exists():
        for line in p.read_text().splitlines():
            if line.startswith("#") or not line.strip():
                continue
            name, secs = line.split()
            times[name] = float(secs)
    return times


class Lane:
    def __init__(self, mode, tests, fast, fresh=False, board="", why=""):
        self.mode = mode          # "n" or "c"
        self.tests = tests        # in declared order
        self.fast = fast          # clock factor, 1 real
        self.fresh = fresh
        self.board = board
        self.why = why
        self.est = 0.0
        self.tag = ""
        self.port = 0
        self.rc = None
        self.secs = 0.0
        self.harness_out = ""


def clusters_of(selected, plan, solo):
    """The selection as clusters that must share a board: a test with
    everything NEEDS says it needs (added when the selection left it out),
    merged transitively. Returns a list of sorted test lists."""
    order = {n: i for i, n in enumerate(plan["order"])}
    key = lambda n: order.get(n, len(order))          # noqa: E731
    if solo:
        return [[t] for t in sorted(set(selected), key=key)]
    needs = plan["needs"]
    parent = {}

    def find(x):
        parent.setdefault(x, x)
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        parent[find(a)] = find(b)

    wanted = list(selected)
    todo = list(selected)
    seen = set(selected)
    while todo:
        t = todo.pop()
        for pre in needs.get(t, []):
            union(t, pre)
            if pre not in seen:
                seen.add(pre)
                wanted.append(pre)
                todo.append(pre)
    for t in wanted:
        find(t)
    groups = {}
    for t in wanted:
        groups.setdefault(find(t), []).append(t)
    return [sorted(g, key=key) for g in groups.values()]


def make_lanes(opts, plan, times):
    order = {n: i for i, n in enumerate(plan["order"])}
    key = lambda n: order.get(n, len(order))          # noqa: E731
    selected = plan["selected"]
    est = lambda t: times.get(t, DEFAULT_SECS)         # noqa: E731
    clusters = clusters_of(selected, plan, opts.solo)
    alone = set(plan["alone"])
    realtime = set(plan["realtime"])

    total = sum(est(t) for c in clusters for t in c) * len(opts.modes)
    # Lanes of about a third of an even share each: small enough that the
    # longest-first start evens out what the estimates got wrong, big
    # enough that a lane's start-up is noise.
    target = opts.lane_secs or max(60.0, total / (opts.jobs * 3.0))

    lanes = []
    for mode in opts.modes:
        buckets = {}          # (fast) -> [[tests, est], ...]
        for c in sorted(clusters, key=lambda c: -sum(est(t) for t in c)):
            fast = 1 if (opts.fast <= 1 or any(t in realtime for t in c)) else opts.fast
            secs = sum(est(t) for t in c)
            if opts.solo or any(t in alone for t in c):
                lanes.append(Lane(mode, sorted(c, key=key), fast,
                                  why="alone" if any(t in alone for t in c) else "solo"))
                lanes[-1].est = secs
                continue
            open_ = buckets.setdefault(fast, [])
            # Longest first into the emptiest lane that stays under the
            # target, else a new lane.
            open_.sort(key=lambda b: b[1])
            for b in open_:
                if b[1] + secs <= target:
                    b[0].extend(c)
                    b[1] += secs
                    break
            else:
                open_.append([list(c), secs])
        for fast, open_ in buckets.items():
            for tests, secs in open_:
                lane = Lane(mode, sorted(tests, key=key), fast)
                lane.est = secs
                lanes.append(lane)

    if opts.extra and not opts.solo:
        sel = set(selected)
        for t in plan["fresh"]:
            if t in sel:
                lane = Lane("n", [t], opts.fast if t not in realtime else 1,
                            fresh=True, why="fresh")
                lane.est = est(t)
                lanes.append(lane)
        # A profile's tests on its own build, packed the same way as the
        # rest, with the profile's NEEDS kept together. With a card too
        # where PROFILE_CARD says the profile's tests use one (the S3's
        # SSH YMODEM); the others were only ever run without.
        for board, tests in plan["profiles"].items():
            mine = [t for t in tests if t in sel]
            if not mine:
                continue
            modes = opts.modes if board in plan.get("profile_card", []) else ["n"]
            for mode in modes:
                open_ = []
                for c in sorted(clusters_of(mine, plan, False), key=lambda c: -sum(est(t) for t in c)):
                    fast = 1 if (opts.fast <= 1 or any(t in realtime for t in c)) else opts.fast
                    secs = sum(est(t) for t in c)
                    open_.sort(key=lambda b: b[1])
                    for b in open_:
                        if b[2] == fast and b[1] + secs <= target:
                            b[0].extend(c)
                            b[1] += secs
                            break
                    else:
                        open_.append([list(c), secs, fast])
                for tests_, secs, fast in open_:
                    lane = Lane(mode, sorted(tests_, key=key), fast, board=board,
                                why="profile " + board)
                    lane.est = secs
                    lanes.append(lane)
    return lanes


def port_offsets():
    """Every offset from the harness port a test binds a copy at, each taken
    as a run of four: test_boot_hold binds base + 1 to base + 3 from its
    PORT + 3200, the only place a port is worked out from another."""
    offs = {0, 1000, 2000}                        # board, backup window, directory
    for f in ("testclient.py", "harness.sh"):     # harness.sh: the S3's ssh_port, PORT + 1500
        src = (TOOLS / f).read_text(encoding="utf-8")
        offs.update(int(m) for m in re.findall(r"PORT\s*\+\s*(\d+)", src))
    return sorted({o + k for o in offs for k in range(4)})


def slot_ports(n, block):
    """A harness port for each of up to n workers, inside one port block, so
    that no port any lane on one worker can bind is a port a lane on another
    can. Ports belong to the worker, not the lane: only lanes running at the
    same moment can meet, and a worker runs one lane at a time. Fewer than n
    when the block is full (27 fit in one with the offsets of 1.1.2-dev.3)."""
    lo, hi = block
    offs = port_offsets()
    span = offs[-1]
    used = set()
    bases = []
    base = lo
    while len(bases) < n and base + span < hi:
        mine = {base + o for o in offs}
        if not (mine & used):
            used |= mine
            bases.append(base)
        base += 1
    return bases, offs


def claim_block():
    """Lock one of PORT_BLOCKS for this run, waiting for one to come free.
    The lock is dropped when the process ends, however it ends."""
    import fcntl
    said = False
    while True:
        for i, block in enumerate(PORT_BLOCKS):
            f = open("/tmp/bbs-parallel-ports-%d.lock" % i, "w")
            try:
                fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return f, block
            except OSError:
                f.close()
        if not said:
            print("parallel: both port blocks are in use by other --jobs runs; waiting", flush=True)
            said = True
        time.sleep(2)


def reap(tag):
    """Kill any host board a lane left behind: a copy a test started and did
    not stop because the test failed first. Left alone it would hold its port
    into the worker's next lane, whose own copy would then fail to bind,
    reading like a bug in the board. A lane's boards are known by the
    BBS_LANE=<tag> they inherit from it (run_lane sets it), never by port or
    name, so nothing of another run is ever touched."""
    want = ("BBS_LANE=" + tag).encode()
    for d in pathlib.Path("/proc").iterdir():
        if not d.name.isdigit():
            continue
        try:
            argv0 = (d / "cmdline").read_bytes().split(b"\0")[0]
            if not os.path.basename(argv0).startswith(b"bbs_host"):
                continue
            env = (d / "environ").read_bytes().split(b"\0")
        except OSError:
            continue
        if want in env:
            try:
                os.kill(int(d.name), 9)
            except OSError:
                pass


# ---------------------------------------------------------------------------
# Running
# ---------------------------------------------------------------------------
def build(boards):
    """Each profile this run needs, built once before any lane starts; the
    lanes run harness.sh --no-build. Under a lock, so a second --jobs run
    from the same tree waits for the first's build instead of writing the
    same binary at the same moment."""
    import fcntl
    import zlib
    targets = sorted({PROFILE_BIN[b] for b in boards})
    if "s3" in boards:
        targets.append("ssh_call")                # the S3's SSH tests call in with it
    t0 = time.time()
    lock = open("/tmp/bbs-parallel-build-%08x.lock" % zlib.crc32(str(ROOT).encode()), "w")
    fcntl.flock(lock, fcntl.LOCK_EX)
    r = subprocess.run(["make", "-s", "-j%d" % len(targets)] + targets, cwd=ROOT / "host")
    lock.close()
    if r.returncode != 0:
        sys.exit("parallel: the host build failed")
    return time.time() - t0


def run_lane(lane, opts):
    args = ["sh", str(TOOLS / "harness.sh"), "--tag", lane.tag, "--port", str(lane.port),
            "--no-build", "--tests=" + ",".join(lane.tests)] + opts.passthru
    if lane.mode == "c":
        args.append("--card")
    if lane.fast > 1:
        args.append("--fast=%d" % lane.fast)
    if lane.fresh:
        args.append("--fresh")
    if lane.board:
        args += ["--board", lane.board]
    t0 = time.time()
    env = dict(os.environ)
    env.pop("BBS_CHANGED_DRY", None)
    env["BBS_LANE"] = lane.tag              # what reap() knows its boards by
    r = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, env=env)
    lane.secs = time.time() - t0
    lane.rc = r.returncode
    lane.harness_out = r.stdout + r.stderr


def run_all(lanes, opts):
    queue = sorted(lanes, key=lambda l: -l.est)
    lock = threading.Lock()
    done = [0]
    nworkers = min(opts.jobs, len(lanes))
    port_lock, block = claim_block()
    bases, offs = slot_ports(nworkers, block)
    if len(bases) < nworkers:
        print("parallel: %d workers fit in a port block; running %d at a time" %
              (len(bases), len(bases)), flush=True)

    def worker(port):
        while True:
            with lock:
                if not queue:
                    return
                lane = queue.pop(0)
            lane.port = port
            try:
                run_lane(lane, opts)
            except Exception as e:             # noqa: BLE001, reported as the lane's failure
                lane.rc = -1
                lane.harness_out = "parallel: %s: %s" % (type(e).__name__, e)
            reap(lane.tag)
            with lock:
                done[0] += 1
                verdict = "ok" if lane.rc == 0 else "FAILED"
                print("  %3d/%d  %-14s %-6s %5.0f s  %s" %
                      (done[0], len(lanes), lane.tag, verdict, lane.secs,
                       ",".join(t[5:] for t in lane.tests)[:90]), flush=True)

    threads = [threading.Thread(target=worker, args=(b,)) for b in bases]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    port_lock.close()


# ---------------------------------------------------------------------------
# Merging
# ---------------------------------------------------------------------------
MODE_NAME = {"n": "no card", "c": "card"}


def merge(lanes, opts, plan, merged_path):
    """One out.txt for the whole run and one verdict. A test a lane was given
    that left no TIME line (the lane died under it) is a failure by name."""
    fails = []
    counts = {}
    ran = {}
    with open(merged_path, "w", encoding="utf-8") as out:
        for lane in sorted(lanes, key=lambda l: (l.mode, l.tag)):
            label = MODE_NAME[lane.mode] + (" fresh" if lane.fresh else "") + \
                (" " + lane.board if lane.board else "")
            out.write("== lane %s  %s  clock x%d  port %d  %s\n" %
                      (lane.tag, label, lane.fast, lane.port, ",".join(lane.tests)))
            p = pathlib.Path("/tmp/bbs-%s/out.txt" % lane.tag)
            # A lane that never ran has no out.txt of its own; one found
            # there is an earlier run's and must not be counted.
            if lane.rc is None or lane.rc < 0:
                p = pathlib.Path("/nonexistent")
            text = p.read_text(encoding="utf-8", errors="replace") if p.exists() else ""
            out.write(text)
            if not text.endswith("\n"):
                out.write("\n")
            parsed = testtimes.parse(str(p)) if p.exists() else []
            c = counts.setdefault(label, [0, 0])
            timed = set()
            for name, secs, checks in parsed:
                if secs is not None:
                    timed.add(name)
                    ran.setdefault(label, {})[name] = secs
                for verdict, check in checks:
                    if verdict == "PASS":
                        c[0] += 1
                    elif verdict == "FAIL":
                        c[1] += 1
                        fails.append((label, lane.tag, name, check))
            for t in lane.tests:
                if t not in timed:
                    c[1] += 1
                    fails.append((label, lane.tag, t, "never finished on its lane (see the lane's host.log)"))
            if lane.rc not in (0, 1) and not any(f[1] == lane.tag for f in fails):
                c[1] += 1
                fails.append((label, lane.tag, "-", "the lane's harness exited %s %s" %
                              (lane.rc, lane.harness_out.strip()[-200:])))
    return counts, fails, ran


def main(argv):
    opts = Opts(argv)
    plan = load_plan(opts)
    if not plan["selected"]:
        print("parallel: nothing selected")
        return 2
    times = load_times()
    lanes = make_lanes(opts, plan, times)
    for i, lane in enumerate(sorted(lanes, key=lambda l: (l.mode, -l.est))):
        lane.tag = "%s-%s%d" % (opts.tag, lane.mode if not lane.board else lane.board + lane.mode, i)

    boards = {""} | {l.board for l in lanes}
    t_start = time.time()
    print("parallel: %d tests, %d lanes, %d at a time, %s, fast clock %s" %
          (len(plan["selected"]), len(lanes), opts.jobs,
           " and ".join(MODE_NAME[m] for m in opts.modes),
           "x%d" % opts.fast if opts.fast > 1 else "off"))
    for lane in sorted(lanes, key=lambda l: l.tag):
        if lane.why:
            print("  %-14s %s: %s" % (lane.tag, lane.why, ",".join(lane.tests)))
    secs = build(boards)
    print("parallel: built %s in %.0f s" % (", ".join(sorted(PROFILE_BIN[b] for b in boards)), secs),
          flush=True)

    run_all(lanes, opts)

    merged_dir = pathlib.Path("/tmp/bbs-%s" % opts.tag)
    merged_dir.mkdir(parents=True, exist_ok=True)
    merged = merged_dir / "out.txt"
    counts, fails, ran = merge(lanes, opts, plan, merged)
    wall = time.time() - t_start

    print()
    for label in sorted(counts):
        p, f = counts[label]
        print("%-22s %5d passed, %d failed, %d tests" % (label, p, f, len(ran.get(label, {}))))
    for label, tag, name, check in fails:
        print("  FAIL  [%s %s] %s: %s" % (label, tag, name, check))
    slow = sorted(lanes, key=lambda l: -l.secs)[:3]
    print("longest lanes: " + ", ".join("%s %.0f s" % (l.tag, l.secs) for l in slow))
    print("%.0f s wall clock (%.1f min), %.0f s of lanes" %
          (wall, wall / 60, sum(l.secs for l in lanes)))
    print("merged output: %s" % merged)
    ok = not fails and all(l.rc == 0 for l in lanes)
    print("ALL PASS" if ok else "FAILURES")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
