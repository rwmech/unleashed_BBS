#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
===========================================================================
 µnleashed BBS
 Electronic freedom on a microcontroller.
===========================================================================

File:         tools/changed_groups.py
Module:       Tools / test selection

Purpose:      Works out which of tools/testclient.py's GROUPS (and a few
                 test names outside any group) a git range actually touches,
                 so tools/harness.sh --changed can run only the tests a
                 patch could have broken instead of the whole suite.

                 Rob's rule: a patch runs the groups it touches, not
                 everything. The table below is the map from a source path
                 to the group or test-name words that tools/testclient.py's
                 run_selected() already understands (GROUPS keys expand to
                 several tests; anything else is matched as a literal
                 substring against a test's name, which is how --only has
                 always worked).

                 A file that matches nothing in the table is not assumed
                 safe: it gets the widest sensible set, which here means
                 the full suite (no --only filter at all), and the file is
                 named so the reason is visible rather than silent. Docs,
                 internal/ notes and tooling that is not itself a test are
                 the one deliberate exception: they are given an explicit
                 empty entry, because "selects nothing" is the honest
                 answer for a README, not a fallback.

                 The table is deliberately a little wider than the file it
                 names, the same principle GROUPS itself is built on
                 (CLAUDE.md, "Groups: ..."): the file areas and the forums
                 share list machinery, mail lives inside chat, and a
                 handful of core files (the session loop, the terminal
                 layer, the telnet framing, the plugin API) are load
                 bearing enough that scoping them narrowly would be
                 guessing, so they are mapped to FULL outright.

Usage:        python3 tools/changed_groups.py <git-range>

                 Prints one "path -> selection" line per changed file, a
                 blank line, and a final "GROUPS=..." line that is the
                 only line tools/harness.sh reads. GROUPS is a comma list
                 of --only words, the word FULL (run the whole suite, no
                 --only), or the word NONE (nothing changed that a test
                 could see; exit without running).

                 Exit status is always 0: this script reports, it does not
                 fail a build. A bad git range is the one thing it exits
                 non-zero for, since there is nothing useful to report.

Libraries:    Python 3 standard library only
Targets:      developer PC, Python 3 (called from tools/harness.sh, WSL)
See also:     tools/harness.sh, tools/testclient.py, tools/regress.sh

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
import subprocess
import sys

# The sentinel meaning "do not try to scope this, run everything". Never a
# word tools/testclient.py's GROUPS or ORDER_NAMES would use, so it can
# never collide with a real --only word if it leaked into one by mistake.
FULL = "FULL"

# ---------------------------------------------------------------------------
# The table. Each entry is (glob, tokens). glob is matched against the
# path git gives us, relative to the repository root, with fnmatch (so "*"
# also matches "/", which is what lets one line cover "anywhere under this
# name" without a second "**" convention to explain). tokens is a list of
# words meaningful to tools/testclient.py's --only: a GROUPS key (expands to
# several tests), a literal word matched as a substring of a test's name
# (exactly how --only already behaves for a name outside every group), or
# the FULL sentinel. An empty list is a deliberate "this file cannot break
# a test", not an omission.
#
# A path can match more than one line; the tokens union, because a wider
# selection is always the safe direction and duplicate words cost nothing
# (tools/testclient.py's run_selected() already dedupes by test name).
#
# Keep the globs specific. "src/core/bbs*" would also catch bbs_shell.cpp,
# bbs_sysop.cpp and every other bbs_*.cpp file the moment it was added,
# which is why bbs.cpp/bbs.h are named exactly rather than with a trailing
# star.
# ---------------------------------------------------------------------------
TABLE = [
    # -----------------------------------------------------------------
    # Board profiles. src/board.h picks the board at compile time and
    # carries the pin-refusal rules for all of them, so a change to it
    # can move any board's behaviour; the per-board sdkconfig.defaults
    # files only ever affect their own board's build.
    # -----------------------------------------------------------------
    # Same version-bump exception as src/config.h below: BBS_BOARD_VERSION
    # is bumped with a board's own changes and a pure bump of it changes no
    # board's behaviour, so VERSION_ONLY downgrades that case too.
    ("src/board.h", ["board_s3", "board_fncam", "board_espcam", "shell"]),
    ("sdkconfig.defaults.esp32s3", ["board_s3"]),
    ("sdkconfig.defaults.fncam", ["board_fncam"]),
    ("sdkconfig.defaults.espcam", ["board_espcam"]),

    # -----------------------------------------------------------------
    # Core: files load-bearing enough that scoping them would be a guess.
    # -----------------------------------------------------------------
    # src/config.h is FULL by default, but "bump BBS_VERSION every commit"
    # (CLAUDE.md) means this is the single most-touched line in the tree,
    # and a version bump changes no behaviour a test could see. VERSION_ONLY
    # below checks the actual diff content and downgrades a pure version
    # bump before this line is ever consulted.
    ("src/config.h", [FULL]),
    ("src/core/bbs.cpp", [FULL]),
    ("src/core/bbs.h", [FULL]),
    ("src/core/bbs_util.h", [FULL]),
    ("src/core/bbs_shell*", [FULL]),
    ("src/core/plugin*", [FULL]),
    ("src/core/term*", [FULL]),
    ("src/core/telnet*", [FULL]),
    ("src/core/timeline*", [FULL]),

    # -----------------------------------------------------------------
    # Core: the rest, scoped to what they actually touch.
    # -----------------------------------------------------------------
    ("src/core/backup*", ["storage"]),
    ("src/core/bbs_backup*", ["storage"]),
    ("src/core/bbs_hardware*", ["shell"]),
    ("src/core/bbs_ring*", ["messaging", "shell"]),
    ("src/core/bbs_screens*", ["shell", "storage", "login", "motd", "exit_screen", "privacy"]),
    ("src/core/bbs_sysop*", ["login", "shell", "staff_remembered", "shutdown"]),
    ("src/core/bbs_users*", ["login", "shell"]),
    ("src/core/bus*", ["messaging", "places", "shell"]),
    ("src/core/calllog*", ["shell", "login"]),
    ("src/core/cardnames*", ["storage"]),
    ("src/core/claims*", ["messaging", "storage", "shell"]),
    ("src/core/clock*", ["shell", "login"]),
    ("src/core/codes*", ["messaging", "shell"]),
    # compose.h and composer.*: one shared editor, one entry.
    ("src/core/compose*", ["messaging"]),
    ("src/core/crc32*", ["storage"]),
    ("src/core/detect*", ["terminal"]),
    ("src/core/disk*", ["storage"]),
    ("src/core/editor*", ["terminal", "login", "shell", "idle_login"]),
    ("src/core/form*", ["shell", "login"]),
    ("src/core/fx*", ["shell", "login", "messaging"]),
    ("src/core/guard*", ["login"]),
    ("src/core/helptext*", ["shell", "messaging"]),
    ("src/core/improv*", ["shell", "login"]),
    ("src/core/netfallback*", ["shell", "login"]),
    ("src/core/recovery*", ["login"]),
    ("src/core/ring.h", ["messaging", "shell"]),
    ("src/core/screens*", ["shell", "storage", "login", "motd", "exit_screen", "privacy"]),
    ("src/core/sha256*", ["login"]),
    # silent.cpp/.h: the switch and hours, which config, lights and the
    # camera's flash LED and the S3 backlight all read.
    ("src/core/silent*", ["shell", "silent"]),
    ("src/core/sysconfig*", ["shell", "login"]),
    ("src/core/tzones*", ["shell"]),
    ("src/core/users*", ["login", "messaging", "rename_follows"]),
    ("src/core/xmodem*", ["storage", "binary", "upload_no_binary", "ymodem", "list_abort"]),
    ("src/core/ziparc*", ["storage", "partitions"]),

    # -----------------------------------------------------------------
    # Plugins.
    # -----------------------------------------------------------------
    ("src/plugins/announce*", ["announce", "login"]),
    ("src/plugins/camera*", ["camera", "board_fncam", "board_espcam"]),
    ("src/plugins/chat*", ["messaging", "places", "rename_follows"]),
    ("src/plugins/example*", ["plugins"]),
    ("src/plugins/files*", ["storage", "places"]),
    ("src/plugins/forums*", ["messaging", "places", "storage", "partitions"]),
    ("src/plugins/info*", ["messaging"]),
    ("src/plugins/lights*", ["shell", "storage"]),
    ("src/plugins/panel*", ["board_s3", "shell"]),
    ("src/plugins/registry*", ["plugins", "shell"]),
    ("src/plugins/sd*", ["storage"]),
    ("src/plugins/serialbridge*", ["serial"]),

    # -----------------------------------------------------------------
    # host/: the two files linked into every host build, and the
    # standalone unit tests that make test runs, not this harness.
    # -----------------------------------------------------------------
    ("host/platform_host.cpp", [FULL]),
    ("host/main_host.cpp", [FULL]),
    ("host/Makefile", [FULL]),
    ("host/test_*.cpp", []),   # make test, not tools/testclient.py

    # -----------------------------------------------------------------
    # The test client itself. A change here can change what every group
    # means, so it gets everything.
    # -----------------------------------------------------------------
    ("tools/testclient.py", [FULL]),

    # -----------------------------------------------------------------
    # Screen art actually served to callers. Not every screen file has
    # its own test, but this is the set the suite exercises directly.
    # -----------------------------------------------------------------
    ("data/screens/*", ["shell", "storage", "login", "motd", "exit_screen", "privacy"]),

    # -----------------------------------------------------------------
    # Deliberately nothing: docs, internal notes, generated art, and the
    # tooling that is not itself a test. Named explicitly rather than
    # left to the fallback, because "selects nothing" here is the answer,
    # not a gap in the table.
    # -----------------------------------------------------------------
    ("*.md", []),
    ("internal/*", []),
    ("brand/*", []),
    ("release-prep/*", []),
    (".claude/*", []),
    (".github/*", []),
    ("*.png", []), ("*.jpg", []), ("*.jpeg", []), ("*.gif", []), ("*.svg", []),
    ("tools/*.py", []),   # tools/testclient.py above already overrides this
    ("tools/*.sh", []),
    ("data/*.example", []),
    ("LICENSE", []),
]


# A line that is exactly a BBS_VERSION or BBS_BOARD_VERSION #define. Every
# added or removed line in a diff has to match this for the change to count
# as a version bump and nothing else; one other line anywhere in the same
# file's diff means treat it as a real change to src/config.h or
# src/board.h.
VERSION_LINE_RE = re.compile(r'^#define\s+BBS_(BOARD_)?VERSION\b')

# Downgrades for a file that is FULL by table, when the diff turns out to
# be a version bump alone. Checked before TABLE, keyed by exact path.
VERSION_ONLY_DOWNGRADE = {
    "src/config.h": ({"shell"}, "version bump only (BBS_VERSION); no behaviour changed"),
    "src/board.h": ({"shell"}, "version bump only (BBS_BOARD_VERSION); no behaviour changed"),
}


def diff_body_lines(git_range, root, path):
    """Every added or removed line for one path's diff, the +/- stripped and
    the +++ / --- file headers left out. --unified=0 keeps this to just the
    changed lines, which is all a content check needs."""
    result = subprocess.run(["git", "diff", "--unified=0", "--no-color", git_range, "--", path],
                             cwd=root, capture_output=True, text=True)
    out = []
    for line in result.stdout.splitlines():
        if line.startswith("+++") or line.startswith("---"):
            continue
        if line[:1] in ("+", "-"):
            out.append(line[1:].strip())
    return out


def is_version_bump_only(git_range, root, path):
    lines = diff_body_lines(git_range, root, path)
    return bool(lines) and all(VERSION_LINE_RE.match(l) for l in lines)


def load_table_matches(path):
    """All tokens for a path: the union of every glob in TABLE it matches,
    and whether anything in TABLE matched it at all (as opposed to an
    empty list because the match itself says "nothing to run")."""
    tokens = set()
    matched = False
    for glob, toks in TABLE:
        if fnmatch.fnmatch(path, glob):
            matched = True
            tokens.update(toks)
    return tokens, matched


def classify(path, git_range, root):
    """Returns (tokens, reason) for one changed file. tokens is a set of
    --only words and/or FULL; an empty set with matched=True means the
    file was explicitly told to select nothing."""
    if path in VERSION_ONLY_DOWNGRADE and is_version_bump_only(git_range, root, path):
        return VERSION_ONLY_DOWNGRADE[path]
    tokens, matched = load_table_matches(path)
    if not matched:
        return {FULL}, "no mapping for this file; widest sensible set"
    if not tokens:
        return set(), "docs/tooling, no test impact"
    return tokens, ", ".join(sorted(tokens))


def repo_root():
    return subprocess.run(["git", "rev-parse", "--show-toplevel"],
                           capture_output=True, text=True, check=True).stdout.strip()


def changed_files(git_range, root):
    """git diff --name-only over the range, run from the repo root so a
    relative path in TABLE always means the same thing regardless of the
    caller's working directory."""
    result = subprocess.run(["git", "diff", "--name-only", git_range],
                             cwd=root, capture_output=True, text=True)
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
        print("GROUPS=NONE")
        return

    all_tokens = set()
    for path in files:
        tokens, reason = classify(path, git_range, root)
        all_tokens |= tokens
        if not tokens:
            print("%s  ->  (nothing; %s)" % (path, reason))
        elif FULL in tokens:
            print("%s  ->  FULL (%s)" % (path, reason))
        else:
            print("%s  ->  %s" % (path, reason))

    print()
    if not all_tokens:
        print("changed: docs/tooling only; nothing to run")
        print()
        print("GROUPS=NONE")
    elif FULL in all_tokens:
        print("changed: touches core files too central to scope; running the full suite")
        print()
        print("GROUPS=FULL")
    else:
        words = ",".join(sorted(all_tokens))
        print("changed: %d file(s), selected groups: %s" % (len(files), words))
        print()
        print("GROUPS=%s" % words)


if __name__ == "__main__":
    main()
