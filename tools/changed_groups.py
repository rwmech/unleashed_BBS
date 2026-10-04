#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/changed_groups.py
Module:       Tools / test selection

Purpose:      Works out what a git range should run: which of the three
                 test axes (feature, chip, board), which board profiles,
                 and which of tools/testclient.py's GROUPS inside them.
                 tools/harness.sh --changed then runs exactly that instead
                 of the whole suite.

                 Rob, 2026-10-04: "we need to break up all test plans by
                 the following 1. ESP32 vs ESP32-S3, 2. Board Specific,
                 3. Feature Testing ... I see little reason to run
                 regression testing on say a WS43 if we did all the changes
                 to the core code for chat."

                 So the table below maps a source path to an AXIS first and
                 to group words second, which is the change from 1.2.1:
                 a change to core chat now runs the feature axis and no
                 board suite at all, and a change to one board's block in
                 src/board.h runs that board and nothing else. Before this
                 it ran every board's tests, which is where 1.2.1's board
                 failures came from: a shared test asserting the reference
                 board's pins on a board that is merely different.

                 Three paths are worked out from the diff's content rather
                 than from its name, because the name is too coarse:
                   - src/config.h and src/board.h, where a pure version
                     bump changes no behaviour (VERSION_ONLY, as before);
                   - src/board.h, where only the boards whose own #if
                     blocks moved are selected (board_blocks_touched);
                   - tools/testclient.py, where the axes of the tests the
                     diff actually touched are selected, and anything
                     outside a test function means the machinery moved and
                     every axis runs (testclient_axes).

                 A file that matches nothing in the table is not assumed
                 safe: it gets the widest sensible set, which here means
                 every axis and the full suite, and the file is named so
                 the reason is visible rather than silent. Docs, internal/
                 notes and tooling that is not itself a test are the one
                 deliberate exception: they are given an explicit empty
                 entry, because "selects nothing" is the honest answer for
                 a README, not a fallback.

                 The group words stay deliberately a little wider than the
                 file they name, the same principle GROUPS itself is built
                 on (CLAUDE.md, "Groups: ..."): the file areas and the
                 forums share list machinery, mail lives inside chat, and a
                 handful of core files (the session loop, the terminal
                 layer, the telnet framing, the plugin API) are load
                 bearing enough that scoping them narrowly would be
                 guessing, so they are mapped to FULL outright.

Usage:        python3 tools/changed_groups.py <git-range>

                 Prints one "path -> selection" line per changed file, a
                 blank line, and then three lines tools/harness.sh reads:

                   AXES=feature,chip        the axes to run (or NONE)
                   BOARDS=ws2,wseth         board profiles needing their own
                                            run, each one --axis board
                                            --board NAME (or NONE)
                   GROUPS=messaging,shell   the --only words inside those
                                            axes, or FULL (no --only at
                                            all), or NONE (nothing to run)

                 GROUPS keeps exactly the meaning it had before this
                 change, so anything that reads only that line still works.

                 Exit status is always 0: this script reports, it does not
                 fail a build. A bad git range is the one thing it exits
                 non-zero for, since there is nothing useful to report.

Libraries:    Python 3 standard library only
Targets:      developer PC, Python 3 (called from tools/harness.sh, WSL)
See also:     tools/harness.sh, tools/testclient.py, tools/regress.sh,
              internal/test-reorg-2026-10-04.md

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

import fnmatch
import re
import os
import subprocess
import sys

# The sentinel meaning "do not try to scope this, run everything". Never a
# word tools/testclient.py's GROUPS or ORDER_NAMES would use, so it can
# never collide with a real --only word if it leaked into one by mistake.
FULL = "FULL"

# The three axes, in the order a release runs them.
FEATURE, CHIP, BOARD = "feature", "chip", "board"
AXIS_ORDER = [FEATURE, CHIP, BOARD]

# Every board profile, and the #define that opens its block in src/board.h.
# The same table tools/testclient.py keeps (BOARD_DEFINES) plus "" for the
# reference board, whose facts the feature and chip axes cover.
BOARD_DEFINES = {
    "s3":     "BBS_BOARD_WS_S3LCD147",
    "fncam":  "BBS_BOARD_FN_WROVER_CAM",
    "espcam": "BBS_BOARD_AI_ESP32CAM",
    "ws43b":  "BBS_BOARD_WS_S3TOUCH43B",
    "ws2":    "BBS_BOARD_WS_S3TOUCH2",
    "wseth":  "BBS_BOARD_WS_S3ETH",
    "mf35":   "BBS_BOARD_MF_S3PAR35",
    "mf35v2": "BBS_BOARD_MF_S3PAR35V2",
    "g4848":  "BBS_BOARD_GT_4848S040",
}
ALL_BOARDS = sorted(BOARD_DEFINES)
# The S3 profiles. A chip layer that is the S3's reaches all of them.
S3_BOARDS = ["s3", "ws43b", "ws2", "wseth", "mf35", "mf35v2", "g4848"]

# ---------------------------------------------------------------------------
# The table. Each entry is (glob, axes, boards, tokens).
#
#   glob    matched with fnmatch against the path git gives us, relative to
#           the repository root (so "*" also matches "/", which is what lets
#           one line cover "anywhere under this name").
#   axes    which of feature/chip/board this path can break.
#   boards  which board profiles need a suite of their own. Only ever set
#           where the path is a board's: a core change runs no board suite,
#           which is the whole point of the split.
#   tokens  --only words inside those axes: a GROUPS key (expands to
#           several tests), a literal word matched as a substring of a
#           test's name (exactly how --only already behaves), or FULL.
#
# An empty axes list with an empty tokens list is a deliberate "this file
# cannot break a test", not an omission.
#
# A path can match more than one line; axes, boards and tokens all union,
# because a wider selection is always the safe direction.
#
# Keep the globs specific. "src/core/bbs*" would also catch bbs_shell.cpp,
# bbs_sysop.cpp and every other bbs_*.cpp file the moment it was added,
# which is why bbs.cpp/bbs.h are named exactly rather than with a trailing
# star.
# ---------------------------------------------------------------------------
TABLE = [
    # -----------------------------------------------------------------
    # Board profiles. src/board.h picks the board at compile time and
    # carries the pin-refusal rules for all of them; which boards a change
    # to it reaches is read from the diff (board_blocks_touched) rather
    # than from this line, which is the widest answer and the fallback.
    # Each per-board sdkconfig.defaults only affects its own board.
    # -----------------------------------------------------------------
    ("src/board.h", [BOARD, CHIP], ALL_BOARDS, []),
    ("sdkconfig.defaults.ws43b",  [BOARD], ["ws43b"],  []),
    ("sdkconfig.defaults.fncam",  [BOARD], ["fncam"],  []),
    ("sdkconfig.defaults.espcam", [BOARD], ["espcam"], []),
    ("sdkconfig.defaults.ws2",    [BOARD], ["ws2"],    []),
    ("sdkconfig.defaults.wseth",  [BOARD], ["wseth"],  []),
    ("sdkconfig.defaults.mf35",   [BOARD], ["mf35"],   []),
    ("sdkconfig.defaults.mf35v2", [BOARD], ["mf35v2"], []),
    ("sdkconfig.defaults.g4848",  [BOARD], ["g4848"],  []),

    # -----------------------------------------------------------------
    # The chip layers. sdkconfig.defaults is shared by every ESP build
    # (nano printf, 240 MHz); the S3 layer is the S3 family's, and the
    # partition table that goes with it. None of them is one board's.
    # -----------------------------------------------------------------
    ("sdkconfig.defaults", [CHIP], [], []),
    ("sdkconfig.defaults.esp32s3", [CHIP], S3_BOARDS, []),
    # test_partitions is on the feature axis, so these are feature AND chip:
    # with chip alone the selection was chip tests whose names hold neither
    # "storage" nor "partitions", which is no tests at all.
    ("partitions.csv", [FEATURE, CHIP], [], ["storage", "partitions"]),
    ("partitions_s3.csv", [FEATURE, CHIP], S3_BOARDS, ["storage", "partitions"]),
    # The platform layer: what the chip does, and what the host stands in
    # for. Both can break a feature, so both run the feature axis too.
    ("src/platform/platform_esp32.cpp", [FEATURE, CHIP], [], [FULL]),
    ("src/platform/platform_host.cpp", [FEATURE, CHIP], [], [FULL]),
    ("src/platform/platform.h", [FEATURE, CHIP], [], [FULL]),
    # The panel buses: a chip peripheral driving particular boards' glass.
    ("src/platform/platform_esp32_rgb.cpp", [CHIP, BOARD], ["ws43b", "g4848"], []),
    ("src/platform/platform_esp32_st7701.cpp", [CHIP, BOARD], ["g4848"], []),
    ("src/platform/linkradio*", [FEATURE], [], ["radio", "sats"]),

    # -----------------------------------------------------------------
    # Core: files load-bearing enough that scoping them would be a guess.
    # src/config.h is the one most-touched line in the tree ("bump
    # BBS_VERSION every commit"), and a version bump changes no behaviour
    # a test could see: VERSION_ONLY below reads the diff and downgrades a
    # pure bump before this line is consulted.
    # -----------------------------------------------------------------
    ("src/config.h", [FEATURE, CHIP], [], [FULL]),
    ("src/core/bbs.cpp", [FEATURE], [], [FULL]),
    ("src/core/bbs.h", [FEATURE], [], [FULL]),
    ("src/core/bbs_util.h", [FEATURE], [], [FULL]),
    ("src/core/bbs_shell*", [FEATURE], [], [FULL]),
    ("src/core/plugin*", [FEATURE], [], [FULL]),
    ("src/core/term*", [FEATURE], [], [FULL]),
    ("src/core/telnet*", [FEATURE], [], [FULL]),
    ("src/core/timeline*", [FEATURE], [], [FULL]),

    # -----------------------------------------------------------------
    # Core: the rest, scoped to what they actually touch. All feature:
    # none of these is a chip fact or one board's wiring.
    # -----------------------------------------------------------------
    ("src/core/backup*", [FEATURE], [], ["storage"]),
    ("src/core/bbs_backup*", [FEATURE], [], ["storage"]),
    ("src/core/bbs_hardware*", [FEATURE], [], ["shell"]),
    ("src/core/bbs_ring*", [FEATURE], [], ["messaging", "shell"]),
    ("src/core/bbs_screens*", [FEATURE], [], ["shell", "storage", "login", "session"]),
    ("src/core/bbs_sysop*", [FEATURE], [], ["login", "shell", "session"]),
    ("src/core/bbs_users*", [FEATURE], [], ["login", "shell"]),
    ("src/core/bus*", [FEATURE], [], ["messaging", "places", "shell"]),
    ("src/core/calllog*", [FEATURE], [],
     ["shell", "login", "lag_logoff_calls", "lag_last_calls", "lag_announce_calls"]),
    ("src/core/cardnames*", [FEATURE], [], ["storage"]),
    ("src/core/claims*", [FEATURE], [], ["messaging", "storage", "shell", "login"]),
    ("src/core/clock*", [FEATURE], [], ["shell", "login"]),
    ("src/core/codes*", [FEATURE], [], ["messaging", "shell"]),
    # compose.h and composer.*: one shared editor, one entry.
    ("src/core/compose*", [FEATURE], [], ["messaging"]),
    ("src/core/crc32*", [FEATURE], [], ["storage"]),
    ("src/core/detect*", [FEATURE], [], ["terminal", "session"]),
    ("src/core/disk*", [FEATURE], [], ["storage", "lag"]),
    ("src/core/editor*", [FEATURE], [], ["terminal", "login", "shell", "session"]),
    ("src/core/form*", [FEATURE], [], ["shell", "login"]),
    ("src/core/fx*", [FEATURE], [], ["shell", "login", "messaging"]),
    ("src/core/guard*", [FEATURE], [], ["login"]),
    ("src/core/helptext*", [FEATURE], [], ["shell", "messaging"]),
    ("src/core/improv*", [FEATURE], [], ["shell", "login"]),
    ("src/core/netfallback*", [FEATURE], [], ["shell", "login"]),
    ("src/core/recovery*", [FEATURE], [], ["login"]),
    ("src/core/ring.h", [FEATURE], [], ["messaging", "shell"]),
    # runner.*: the background task every slow job moved onto in 1.1.2, so
    # everything that posts to it. The camera's jobs are a board's, so the
    # camera profiles come with it.
    ("src/core/runner*", [FEATURE, BOARD], ["fncam", "espcam", "ws2", "wseth"],
     ["storage", "messaging", "places", "shell", "login", "announce", "camera", "lag"]),
    ("src/core/screens*", [FEATURE], [], ["shell", "storage", "login", "session", "lag"]),
    ("src/core/sha256*", [FEATURE], [], ["login"]),
    # space.*: the kept free-space figures (1.1.2): MEM, SYS, DASH,
    # HARDWARE and every plugin's write guard read them.
    ("src/core/space*", [FEATURE], [], ["shell", "storage", "plugins", "space_kept"]),
    # silent.cpp/.h: the switch and hours, which config, lights and the
    # camera's flash LED and the S3 backlight all read. The panels and the
    # cameras have silent tests of their own, on the board axis.
    ("src/core/silent*", [FEATURE, BOARD],
     ["s3", "fncam", "espcam", "ws2", "wseth"], ["shell", "silent"]),
    # sysconfig.*: syscfg::pinProblem lives here, which is the chip's own
    # pin rule, so this one is feature AND chip.
    ("src/core/sysconfig*", [FEATURE, CHIP], [], ["shell", "login"]),
    ("src/core/tzones*", [FEATURE], [], ["shell"]),
    ("src/core/users*", [FEATURE], [],
     ["login", "messaging", "rename_follows", "lag_logins", "lag_login_calls",
      "lag_logoff_calls", "lag_last_calls"]),
    ("src/core/xmodem*", [FEATURE], [], ["storage", "transfers"]),
    # SSH: compiled only where BBS_HAS_SSH, so it is the feature axis's
    # S3-only part (FEATURE_S3), run as --axis feature --board s3.
    ("src/core/sshd*", [FEATURE], [], ["ssh"]),
    ("src/core/sshlink*", [FEATURE], [], ["ssh"]),
    ("src/core/bbs_ssh*", [FEATURE], [], ["ssh", "login", "terminal"]),
    ("components/wolfssh/*", [FEATURE], [], ["ssh"]),
    ("host/ssh_call.cpp", [FEATURE], [], ["ssh"]),
    ("src/core/ziparc*", [FEATURE], [], ["storage", "partitions"]),
    # The link and what rides on it (1.2.0).
    ("src/core/link*", [FEATURE], [], ["radio", "sats"]),
    ("src/core/satwords.h", [FEATURE], [], ["radio", "sats"]),
    # photos.* and cameras.*: the photo system is every board's, the camera
    # tests are the camera boards'.
    ("src/core/photos*", [FEATURE, BOARD], ["fncam", "espcam", "ws2", "wseth"],
     ["camera", "sats", "storage"]),
    ("src/core/cameras*", [FEATURE, BOARD], ["fncam", "espcam", "ws2", "wseth"],
     ["camera", "sats"]),

    # -----------------------------------------------------------------
    # Plugins.
    # -----------------------------------------------------------------
    ("src/plugins/announce*", [FEATURE], [], ["announce", "login"]),
    ("src/plugins/link*", [FEATURE], [], ["radio", "sats", "config"]),
    ("src/plugins/doors*", [FEATURE], [], ["radio"]),
    ("src/plugins/camera*", [FEATURE, BOARD], ["fncam", "espcam", "ws2", "wseth"],
     ["camera"]),
    ("src/plugins/chat*", [FEATURE], [], ["messaging", "places", "rename_follows"]),
    ("src/plugins/example*", [FEATURE], [], ["plugins"]),
    ("src/plugins/files*", [FEATURE], [], ["storage", "places", "lag"]),
    ("src/plugins/forums*", [FEATURE], [], ["messaging", "places", "storage", "partitions"]),
    ("src/plugins/info*", [FEATURE], [], ["messaging"]),
    # The lights' GPIO range is the chip's (CONFIG lights is on the chip
    # axis for exactly that), so this is feature AND chip.
    ("src/plugins/lights*", [FEATURE, CHIP], [], ["shell", "storage"]),
    # panel.cpp draws every glass, so a change to it is every panel board's.
    ("src/plugins/panel*", [FEATURE, BOARD],
     ["s3", "ws43b", "ws2", "mf35", "mf35v2", "g4848"], ["shell"]),
    ("src/plugins/registry*", [FEATURE], [], ["plugins", "shell"]),
    ("src/plugins/sd*", [FEATURE], [], ["storage"]),
    ("src/plugins/serialbridge*", [FEATURE, CHIP], [], ["serial"]),

    # -----------------------------------------------------------------
    # host/: the two files linked into every host build, and the
    # standalone unit tests that make test runs, not this harness.
    # -----------------------------------------------------------------
    ("host/main_host.cpp", [FEATURE, CHIP], [], [FULL]),
    ("host/Makefile", [FEATURE, CHIP, BOARD], ALL_BOARDS, [FULL]),
    ("host/linkpeer.cpp", [FEATURE], [], ["radio", "sats"]),
    ("host/linkradio_host.cpp", [FEATURE], [], ["radio", "sats"]),
    ("host/test_*.cpp", [], [], []),   # make test, not tools/testclient.py

    # -----------------------------------------------------------------
    # The test tooling. testclient.py's own axes are read from its diff
    # (testclient_axes); the two harness scripts can change what any
    # selection means, so they get everything.
    # -----------------------------------------------------------------
    ("tools/testclient.py", AXIS_ORDER, ALL_BOARDS, [FULL]),
    ("tools/harness.sh", AXIS_ORDER, ALL_BOARDS, [FULL]),
    ("tools/parallel.py", AXIS_ORDER, ALL_BOARDS, [FULL]),
    # This file. It decides what every selection means, so a change to it
    # cannot be allowed to select nothing: run on itself before this line
    # existed, it fell through to the "tools/*.py selects nothing" bucket
    # below and answered GROUPS=NONE, which is the silent skip the whole
    # exercise is about.
    ("tools/changed_groups.py", AXIS_ORDER, ALL_BOARDS, [FULL]),

    # -----------------------------------------------------------------
    # Screen art actually served to callers. Not every screen file has
    # its own test, but this is the set the suite exercises directly.
    # -----------------------------------------------------------------
    ("data/screens/*", [FEATURE], [], ["shell", "storage", "login", "session"]),

    # -----------------------------------------------------------------
    # Deliberately nothing: docs, internal notes, generated art, and the
    # tooling that is not itself a test. Named explicitly rather than
    # left to the fallback, because "selects nothing" here is the answer,
    # not a gap in the table.
    # -----------------------------------------------------------------
    ("*.md", [], [], []),
    ("internal/*", [], [], []),
    ("brand/*", [], [], []),
    ("release-prep/*", [], [], []),
    (".claude/*", [], [], []),
    (".github/*", [], [], []),
    ("*.png", [], [], []), ("*.jpg", [], [], []), ("*.jpeg", [], [], []),
    ("*.gif", [], [], []), ("*.svg", [], [], []),
    # tools/testclient.py, harness.sh and parallel.py above already
    # override these two.
    ("tools/*.py", [], [], []),
    ("tools/*.sh", [], [], []),
    ("tools/test-times.txt", [], [], []),   # the figures --jobs packs by
    ("data/*.example", [], [], []),
    ("LICENSE", [], [], []),
]


# A line that is exactly a BBS_VERSION or BBS_BOARD_VERSION #define. Every
# added or removed line in a diff has to match this for the change to count
# as a version bump and nothing else; one other line anywhere in the same
# file's diff means treat it as a real change to src/config.h or
# src/board.h.
VERSION_LINE_RE = re.compile(r'^#define\s+BBS_(BOARD_)?VERSION\b')

# Downgrades for a file that is FULL by table, when the diff turns out to
# be a version bump alone. Checked before TABLE, keyed by exact path. The
# tuple is (axes, boards, tokens, reason).
VERSION_ONLY_DOWNGRADE = {
    "src/config.h": ([FEATURE], [], ["shell"],
                     "version bump only (BBS_VERSION); no behaviour changed"),
    "src/board.h": ([FEATURE], [], ["shell"],
                    "version bump only (BBS_BOARD_VERSION); no behaviour changed"),
}


def git(args, cwd=None):
    """git, or git.exe when git cannot see the repository.

    A git worktree made on Windows (release-prep/wt-*) has a .git file that
    points at a C:/ path, which Linux git in WSL cannot follow, so --changed
    failed in every worktree. WSL runs Windows programs, and git.exe reads
    the same worktree from the Windows side."""
    result = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0 and "not a git repository" in result.stderr:
        try:
            alt = subprocess.run(["git.exe"] + args, cwd=cwd, capture_output=True, text=True)
        except OSError:
            return result
        if alt.returncode == 0:
            alt.stdout = alt.stdout.replace("\r\n", "\n")
            return alt
    return result


def diff_body_lines(git_range, root, path):
    """Every added or removed line for one path's diff, the +/- stripped and
    the +++ / --- file headers left out. --unified=0 keeps this to just the
    changed lines, which is all a content check needs."""
    result = git(["diff", "--unified=0", "--no-color", git_range, "--", path], cwd=root)
    out = []
    for line in result.stdout.splitlines():
        if line.startswith("+++") or line.startswith("---"):
            continue
        if line[:1] in ("+", "-"):
            out.append(line[1:].strip())
    return out


def changed_line_numbers(git_range, root, path):
    """The line numbers on the NEW side that a path's diff touches.

    A hunk header is @@ -a,b +c,d @@; c..c+d-1 are the new lines. A pure
    deletion (d == 0) still touches the code around c, so it counts as c."""
    result = git(["diff", "--unified=0", "--no-color", git_range, "--", path], cwd=root)
    nums = set()
    for line in result.stdout.splitlines():
        m = re.match(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", line)
        if not m:
            continue
        start = int(m.group(1))
        count = int(m.group(2)) if m.group(2) is not None else 1
        if count == 0:
            nums.add(start)
        else:
            nums.update(range(start, start + count))
    return nums


def is_version_bump_only(git_range, root, path):
    lines = diff_body_lines(git_range, root, path)
    return bool(lines) and all(VERSION_LINE_RE.match(l) for l in lines)


def guard_spans(text):
    """(board, first_line, last_line) for each board's own #if block in
    src/board.h, one-based and inclusive.

    A block is only a block when it has the labelled `#endif // DEFINE` the
    file's own convention gives it, and it runs to THAT line, searched to the
    end of the file. Ending it at the next opener instead was wrong and wrong
    narrowly, which is the dangerous direction: every board's block contains
    a nested `#if defined(BBS_BOARD_WS_S3LCD147)` ("one board profile at a
    time") and compound guards such as
    `#if defined(A) || defined(B) || defined(C)`, so the 4.3B's block came
    out five lines long and lines 336 to 488, its body, were attributed to
    the Waveshare stick. Four real ETH-board commits then named the stick's
    suite and never the ETH board's.

    So: the opener must be the whole line (a compound guard is not a board's
    block), and an opener with no labelled `#endif` is not a block at all,
    which drops every nested guard. A line inside no block is shared
    board.h code and the caller treats it as every board's.

    A whole-line opener INSIDE a block already accepted is a nested guard,
    not a board's block: two of them are in src/board.h today, the
    "one board profile at a time" `#if defined(BBS_BOARD_WS_S3LCD147)` lines
    inside the 4.3B's and the Makerfabs' blocks, and neither has a labelled
    end. Those are skipped. A top-level opener with no labelled `#endif` is
    a file whose shape has moved, and that returns None so the caller runs
    everything rather than trusting a narrow answer."""
    lines = text.splitlines()
    opener = re.compile(r"#if defined\((BBS_BOARD_\w+)\)\s*$")
    out = []
    covered = 0                 # the last line of the block last accepted
    for i, line in enumerate(lines, 1):
        m = opener.match(line)
        if not m or i <= covered:
            continue
        key = m.group(1)
        want = re.compile(r"#endif\s*//\s*" + re.escape(key) + r"\s*$")
        for j in range(i, len(lines)):
            if want.match(lines[j]):
                out.append((key, i, j + 1))
                covered = j + 1
                break
        else:
            return None
    # A nested opener is fine while it is only a guard: the two
    # `#if defined(BBS_BOARD_WS_S3LCD147)` lines inside the 4.3B's and the
    # Makerfabs' blocks have no labelled end and are plainly not blocks.
    # One with a labelled end of its own is a different thing: it means the
    # outer block swallowed a real block whole, so every line of the inner
    # board was attributed to the outer one. That is a wrong narrow answer,
    # the one failure this file may not have, so it refuses instead.
    for key, a, z in out:
        for j in range(a, z - 1):
            other = opener.match(lines[j])
            if not other or other.group(1) == key:
                continue
            ends = re.compile(r"#endif\s*//\s*" + re.escape(other.group(1)) + r"\s*$")
            if any(ends.match(lines[k]) for k in range(j + 1, len(lines))):
                return None
    return out or None


def func_spans(text, pattern):
    """(name, first_line, last_line) for each function pattern opens, one
    based and inclusive, ending where the body dedents.

    Ending at the next `def` instead swallowed every module-level table and
    helper between two tests: 4,575 lines of tools/testclient.py, including
    GROUPS, ORDER_NAMES, the axis tables themselves and every per-profile
    table, were each attributed to whichever test happened to be above them.
    A change to the axis machinery then selected one arbitrary unrelated
    test. A line in no span is module-level code, and the caller treats that
    as the suite's machinery: every axis, the widest set."""
    lines = text.splitlines()
    out = []
    for i, line in enumerate(lines, 1):
        m = re.match(pattern, line)
        if not m:
            continue
        stop = i
        for j in range(i, len(lines)):
            # Blank, or indented: still inside the body. Anything else at
            # column zero has ended it.
            if lines[j].strip() and not lines[j][:1].isspace():
                break
            stop = j + 1
        out.append((m.group(1), i, stop))
    return out


def range_end(git_range):
    """The revision the diff's NEW side comes from, or None for the worktree.

    `git diff A..B` and `A...B` compare two commits, so the new side is B.
    `git diff A` with NO separator compares A with the WORKING TREE, so the
    new side is what is on disk, which is None here. Getting that backwards
    reads one side's text against the other side's line numbers, the mistake
    this pair of functions exists to stop, and it has now been got backwards
    in both directions: the first cut had `A..` right and a bare `A` wrong,
    the second had `A` right and `A..` wrong.

    Measured on this repository with a deliberately dirty src/board.h, since
    the two forms look alike and the difference is the whole point:

        git diff HEAD~1..      ->  CLAUDE.md            (commit to commit)
        git diff HEAD~1..HEAD  ->  CLAUDE.md            (the same thing)
        git diff HEAD~1        ->  CLAUDE.md, board.h   (the worktree)

    So `A..` means `A..HEAD` and `A...` means `merge-base(A,HEAD)..HEAD`:
    both have HEAD as the new side, never the worktree."""
    for sep in ("...", ".."):
        if sep in git_range:
            right = git_range.split(sep, 1)[1].strip()
            return right or "HEAD"          # "A.." is A..HEAD
    return None                             # "A" alone is A..worktree


def file_at_end(git_range, root, path):
    """One file as the diff's new side has it, or None for "could not tell".

    Read the worktree for a two-commit range and the text and the diff's
    new-side line numbers describe different files, which they do for any
    range that does not end at a clean HEAD: `--changed main..HEAD` with
    uncommitted edits, the normal state mid-work, or any `HEAD~5..HEAD~2`.
    The spans are then narrow and wrong rather than None, and these are the
    two paths whose whole job is to be narrow."""
    end = range_end(git_range)
    if end is None:
        # The new side is the working tree.
        full = os.path.join(root, *path.split("/"))
        try:
            return open(full, encoding="utf-8", errors="replace").read()
        except OSError:
            return None
    result = git(["show", "%s:%s" % (end, path)], cwd=root)
    if result.returncode != 0:
        return None                          # deleted there, or a bad rev
    return result.stdout


def board_blocks_touched(git_range, root):
    """Which board profiles' own #if blocks in src/board.h the diff moved.

    None means "could not tell", and the caller then uses the table's
    answer, which is every board. A change outside every block (the shared
    pin-refusal lists, a new capability) is also every board, and says so."""
    text = file_at_end(git_range, root, "src/board.h")
    if text is None:
        return None
    touched = changed_line_numbers(git_range, root, "src/board.h")
    if not touched:
        return None
    # Every whole-line opener has to have its labelled end, or guard_spans
    # says None and the caller runs everything. The count is NOT compared
    # with BOARD_DEFINES: an older board.h in a historical range has fewer
    # boards, and refusing it there threw away the narrowing for every
    # commit made before the newest board landed.
    blocks = guard_spans(text)
    if not blocks:
        return None
    by_define = {d: b for b, d in BOARD_DEFINES.items()}
    boards, outside = set(), False
    for n in sorted(touched):
        inside = [by_define.get(key) for key, a, z in blocks if a <= n <= z]
        inside = [b for b in inside if b]
        if inside:
            boards.update(inside)
        else:
            outside = True
    if outside:
        return None          # shared board.h code: every board
    return sorted(boards)


def testclient_axes(git_range, root):
    """The axes of the tests tools/testclient.py's diff actually touched.

    Returns (axes, boards, tokens) or None for "could not tell", which the
    caller turns into the table's answer: every axis and the full suite.
    A changed line outside every test function is the suite's machinery and
    is also None, because a change there can move what any selection
    means."""
    text = file_at_end(git_range, root, "tools/testclient.py")
    if text is None:
        return None
    touched = changed_line_numbers(git_range, root, "tools/testclient.py")
    if not touched:
        return None
    # The axis each test is on, read out of the file rather than kept here.
    axis_of = {}
    for axis, var in ((FEATURE, "AXIS_FEATURE"), (CHIP, "AXIS_CHIP"),
                      (BOARD, "AXIS_BOARD")):
        m = re.search(r"^%s = \[(.*?)^\]" % var, text, re.S | re.M)
        if not m:
            return None
        for n in re.findall(r'"(test_\w+)"', m.group(1)):
            axis_of[n] = axis
    board_of = {}
    m = re.search(r"^BOARD_TESTS = \{(.*?)^\}", text, re.S | re.M)
    if m:
        for row in re.finditer(r'"(\w*)":\s*(\[[^\]]*\](?:\s*\+\s*\w+)?)', m.group(1)):
            for n in re.findall(r'"(test_\w+)"', row.group(2)):
                board_of.setdefault(n, []).append(row.group(1))
            if "_CAMERA_TESTS" in row.group(2):
                board_of.setdefault("_CAMERA_TESTS", []).append(row.group(1))
    cam = re.search(r"^_CAMERA_TESTS = \[(.*?)\]", text, re.S | re.M)
    if cam:
        for n in re.findall(r'"(test_\w+)"', cam.group(1)):
            board_of.setdefault(n, []).extend(board_of.get("_CAMERA_TESTS", []))

    funcs = func_spans(text, r"def (test_\w+)\(\):")
    axes, boards, tokens = set(), set(), set()
    for n in sorted(touched):
        here = [name for name, a, z in funcs if a <= n <= z]
        if not here:
            return None              # shared machinery
        for name in here:
            axis = axis_of.get(name)
            if axis is None:
                return None          # a test with no axis: the suite says so
            axes.add(axis)
            tokens.add(name)
            if axis == BOARD:
                boards.update(board_of.get(name, ALL_BOARDS))
    return sorted(axes), sorted(boards), sorted(tokens)


def load_table_matches(path):
    """All (axes, boards, tokens) for a path: the union of every glob in
    TABLE it matches, and whether anything in TABLE matched it at all (as
    opposed to an empty answer because the match says "nothing to run")."""
    axes, boards, tokens = set(), set(), set()
    matched = False
    for glob, ax, bd, toks in TABLE:
        if fnmatch.fnmatch(path, glob):
            matched = True
            axes.update(ax)
            boards.update(bd)
            tokens.update(toks)
    return axes, boards, tokens, matched


def classify(path, git_range, root):
    """(axes, boards, tokens, reason) for one changed file."""
    if path in VERSION_ONLY_DOWNGRADE and is_version_bump_only(git_range, root, path):
        ax, bd, toks, why = VERSION_ONLY_DOWNGRADE[path]
        return set(ax), set(bd), set(toks), why

    # Two paths whose answer comes from the diff's content, because the
    # file name alone is far too wide: one board's block, and one test.
    if path == "src/board.h":
        only = board_blocks_touched(git_range, root)
        if only is not None:
            if not only:
                return set(), set(), set(), "no board's block moved"
            # The chip axis comes with it: a board's block carries
            # BBS_PINS_CONSOLE, BBS_PINS_WIRED and BBS_CHIP_S3, which are
            # exactly what the chip-axis tests assert per profile
            # (PB["console"], PB["wired43"] in test_config_serial_rows), and
            # that board's own suite does not cover them.
            return ({BOARD, CHIP}, set(only), set(),
                    "the %s block%s in board.h, and the chip axis" %
                    (", ".join(only), "" if len(only) == 1 else "s"))
    if path == "tools/testclient.py":
        picked = testclient_axes(git_range, root)
        if picked is not None:
            ax, bd, toks = picked
            return (set(ax), set(bd), set(toks),
                    "the %s axis of the tests touched (%s)" %
                    ("/".join(ax), ", ".join(t[5:] for t in toks)))

    axes, boards, tokens, matched = load_table_matches(path)
    if not matched:
        return (set(AXIS_ORDER), set(ALL_BOARDS), {FULL},
                "no mapping for this file; widest sensible set")
    if not axes and not tokens and not boards:
        return set(), set(), set(), "docs/tooling, no test impact"
    return axes, boards, tokens, ", ".join(sorted(axes) + sorted(tokens))


def repo_root():
    """The repository's top, or this script's parent's parent when git can
    only answer from the Windows side (whose path WSL would not open)."""
    result = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                            capture_output=True, text=True)
    if result.returncode == 0 and result.stdout.strip():
        return result.stdout.strip()
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def changed_files(git_range, root):
    """git diff --name-only over the range, run from the repo root so a
    relative path in TABLE always means the same thing regardless of the
    caller's working directory."""
    result = git(["diff", "--name-only", git_range], cwd=root)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.exit(2)
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: changed_groups.py <git-range>\n")
        sys.exit(2)
    git_range = sys.argv[1]
    root = repo_root()

    files = changed_files(git_range, root)
    if not files:
        print("no changes in %s" % git_range)
        print()
        print("AXES=NONE")
        print("BOARDS=NONE")
        print("GROUPS=NONE")
        return

    all_axes, all_boards, all_tokens = set(), set(), set()
    for path in files:
        axes, boards, tokens, reason = classify(path, git_range, root)
        all_axes |= axes
        all_boards |= boards
        all_tokens |= tokens
        if not axes and not boards and not tokens:
            print("%s  ->  (nothing; %s)" % (path, reason))
        elif FULL in tokens:
            print("%s  ->  FULL (%s)" % (path, reason))
        else:
            print("%s  ->  %s" % (path, reason))

    # A board suite is run per profile and nothing else, so the board axis
    # is only ever "on" because some board was named.
    if all_boards:
        all_axes.add(BOARD)
    elif BOARD in all_axes:
        all_axes.discard(BOARD)

    print()
    if not all_axes and not all_boards:
        print("changed: docs/tooling only; nothing to run")
        print()
        print("AXES=NONE")
        print("BOARDS=NONE")
        print("GROUPS=NONE")
        return

    axes = [a for a in AXIS_ORDER if a in all_axes]
    boards = sorted(all_boards)
    print("changed: %d file(s)" % len(files))
    print("  axes:   %s" % (",".join(axes) or "none"))
    print("  boards: %s" % (",".join(boards) or "none"))
    # GROUPS=FULL means "no --only filter at all", which is not the same as
    # "the whole suite" any more: the axis narrows it. A board-only change
    # is AXES=board BOARDS=ws2 GROUPS=FULL, and that runs the WS2's board
    # suite and nothing else.
    if FULL in all_tokens:
        print("  groups: FULL (no --only: files too central to scope)")
    elif not all_tokens:
        print("  groups: FULL (no --only: the axes above are the whole "
              "selection)")
    else:
        print("  groups: %s" % ",".join(sorted(all_tokens)))
    print()
    print("AXES=%s" % (",".join(axes) or "NONE"))
    print("BOARDS=%s" % (",".join(boards) or "NONE"))
    if FULL in all_tokens or not all_tokens:
        print("GROUPS=FULL")
    else:
        print("GROUPS=%s" % ",".join(sorted(all_tokens)))


if __name__ == "__main__":
    main()
