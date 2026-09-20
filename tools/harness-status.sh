#!/bin/sh
# ===========================================================================
#  µnleashed BBS
# ===========================================================================
#
# File:         tools/harness-status.sh
# Purpose:      Summarise one or more harness runs.
#
#               A file rather than a one-liner because the quoting does not
#               survive being passed through wsl.exe -- bash -lc '...' with
#               $() and grep patterns inside it, and every attempt produced a
#               different mangled command rather than an answer.
#
# Usage:        tools/harness-status.sh [tag...]      (default: every tag)
#
# Copyright 2026 - Robert Mech
# License:      GNU General Public License v2 or later
# SPDX-License-Identifier: GPL-2.0-or-later
# ===========================================================================

tags="$*"
[ -n "$tags" ] || tags=$(ls -d /tmp/bbs-* 2>/dev/null | sed 's|/tmp/bbs-||')

for t in $tags; do
    out=/tmp/bbs-$t/out.txt
    if [ ! -f "$out" ]; then
        echo "$t: no run"
        continue
    fi
    pass=$(grep -c '  PASS' "$out" 2>/dev/null)
    fail=$(grep -c '  FAIL' "$out" 2>/dev/null)
    last=$(tail -1 "$out")
    if pgrep -f "bbs_host /tmp/bbs-$t/data" >/dev/null 2>&1; then
        state=RUNNING
    else
        state=done
    fi
    echo "$t: $state  $pass pass  $fail fail  | $last"
    if [ "$fail" != "0" ]; then
        grep -n '  FAIL' "$out" | head -8 | sed 's/^/     /'
    fi
done
