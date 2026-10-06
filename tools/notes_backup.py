#!/usr/bin/env python3
"""Copy notes/ somewhere a clean checkout cannot take it.

notes/ is gitignored, which keeps it out of the history and also out of
every protection git gives a tracked file: `git clean -xfd`, a fresh
clone, or deleting the worktree all take it with no warning. So it gets
copied out of the repo entirely.

    python tools/notes_backup.py            copy to ../../notes-backup/<date>/
    python tools/notes_backup.py --list     what is already backed up
    python tools/notes_backup.py --keep 60  change the 30-day retention

Copyright 2026 - Robert Mech. SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import datetime as dt
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
NOTES = os.path.join(REPO, "notes")
# Two levels up from the repo: alongside it in Development/BBS, not inside
# it, so nothing git does to the worktree reaches the copies.
DEST = os.path.abspath(os.path.join(REPO, "..", "..", "notes-backup"))


def sizeof(path):
    total = 0
    for root, _dirs, files in os.walk(path):
        for f in files:
            try:
                total += os.path.getsize(os.path.join(root, f))
            except OSError:
                pass
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--keep", type=int, default=30, help="days to keep")
    args = ap.parse_args()

    if args.list:
        if not os.path.isdir(DEST):
            print("no backups yet at %s" % DEST)
            return
        for name in sorted(os.listdir(DEST)):
            p = os.path.join(DEST, name)
            if os.path.isdir(p):
                n = sum(len(f) for _r, _d, f in os.walk(p))
                print("%s  %d files  %.1f KB" % (name, n, sizeof(p) / 1024.0))
        return

    if not os.path.isdir(NOTES):
        sys.exit("nothing to back up: %s does not exist" % NOTES)

    files = [f for _r, _d, fs in os.walk(NOTES) for f in fs]
    if not files:
        print("notes/ is empty, nothing to do")
        return

    today = dt.date.today().isoformat()
    out = os.path.join(DEST, today)
    # Same day twice: replace rather than merge, so the copy is what
    # notes/ looks like NOW and not an accumulation of what it ever held.
    if os.path.isdir(out):
        shutil.rmtree(out)
    shutil.copytree(NOTES, out)
    print("copied %d files (%.1f KB) to %s" % (len(files), sizeof(out) / 1024.0, out))

    # Retention. Only touches directories whose name is a date we wrote,
    # so anything a person put there by hand is left alone.
    cutoff = dt.date.today() - dt.timedelta(days=args.keep)
    dropped = 0
    for name in sorted(os.listdir(DEST)):
        p = os.path.join(DEST, name)
        if not os.path.isdir(p):
            continue
        try:
            when = dt.date.fromisoformat(name)
        except ValueError:
            continue                      # not ours, leave it
        if when < cutoff:
            shutil.rmtree(p)
            dropped += 1
    if dropped:
        print("dropped %d backup(s) older than %d days" % (dropped, args.keep))


if __name__ == "__main__":
    main()
