#!/usr/bin/env bash
# ===========================================================================
#  µnleashed BBS
#  Electronic freedom on a microcontroller.
# ===========================================================================
#
# File:         tools/regress.sh
# Module:       Testing / parallel regression
#
# Purpose:      Run the test suite as several independent areas at once,
#                   so a full pass costs the slowest area rather than the
#                   sum of all of them, and so an area nobody touched can be
#                   left out entirely.
#
# Design:       Rob, 2026-09-22: "we never even fucking touched the Xmode and
#               Ymodem so why are we testing that at all. That is my point
#               that we need to break up these regressions or run them less
#               frequently. Breaking up by logical area also means we can
#               multithread em."
#
#               He is right on both halves. harness.sh already derives its
#               port, data directory, card and directory port from --tag,
#               precisely so two runs cannot collide, and that was built for
#               parallel AGENTS. The same property makes parallel AREAS free.
#
#               The transfer tests are most of the runtime on their own,
#               because XMODEM and YMODEM involve real protocol timeouts that
#               cannot be hurried. Serially they hold everything else up; in
#               their own lane they stop mattering.
#
# Usage:        tools/regress.sh                 every area, in parallel
#               tools/regress.sh messaging shell just those
#               tools/regress.sh --list          what the areas are
#               tools/regress.sh --no-card       skip the card areas
#
# Targets:      Linux host build (WSL)
# See also:     tools/harness.sh, tools/testclient.py
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
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"

# The areas, and what each one runs. Together they cover the suite; the
# coverage check below is what keeps that true as tests are added, because
# an area list that silently stops covering everything is worse than no area
# list at all.
#
# Each line: name | needs-card | --only argument
# STATUS: targeted runs are reliable; a full parallel run is NOT yet a
# substitute for the serial suite, and the reason is worth knowing.
#
# Each area runs on its own board with its own data directory, which is what
# makes them safe to run at once. It also strips away an ordering the suite
# has always depended on and nobody had written down. Two examples found the
# day this was built:
#
#   - the backup test asserts "Alice" appears in users.txt, and Alice is
#     registered by the accounts test;
#   - the privacy tests need a handle that has NOT been registered, so
#     adding accounts to their area to fix the first problem broke them.
#
# The dependencies run in both directions, so no arrangement of group
# membership makes them all pass. Tuning the groups until the colours go
# green would produce a tool that reports PASS for the wrong reason, which
# is worse than no tool.
#
# What it needs is tests that set up their own state. That is real work and
# it is worth doing: the measurement is 193 seconds against roughly six
# minutes for the same coverage.
#
# Until then:
#   tools/regress.sh messagng     one area while iterating   RELIABLE
#   tools/harness.sh --only=...   one or two tests           RELIABLE
#   tools/harness.sh --card       the whole suite, serial    THE GATE
#
# Each line: name | needs-card | --only argument
AREAS=(
  "terminal|no |ansi,petscii,ascii,telnet_first"
  "login   |no |accounts,guest,sysop,cosysop,user_admin,ban"
  "shell   |no |menus,sysinfo,page,about,privacy,config,fx,help"
  "messagng|yes|mail,forums,chat,room_commands"
  "storage |yes|files,sd,backup,screens"
  "xfer    |yes|xfer,upload"
  "plugins |no |plugins,announce,serial"
)

usage() {
    echo "usage: regress.sh [--list] [--no-card] [area ...]"
    echo
    echo "areas:"
    for a in "${AREAS[@]}"; do
        IFS='|' read -r name card only <<< "$a"
        printf "  %-9s card:%-3s %s\n" "$(echo "$name" | tr -d ' ')" "$(echo "$card" | tr -d ' ')" "$only"
    done
    echo
    echo "  tools/regress.sh                  every area, in parallel"
    echo "  tools/regress.sh messagng shell   just those two"
    echo "  tools/regress.sh --no-card        leave out the areas needing a card"
}

NOCARD=no
WANT=()
for arg in "$@"; do
    case "$arg" in
        --list|-l) usage; exit 0 ;;
        --no-card) NOCARD=yes ;;
        -*)        usage; exit 2 ;;
        *)         WANT+=("$arg") ;;
    esac
done

# Build the run list.
RUNS=()
for a in "${AREAS[@]}"; do
    IFS='|' read -r name card only <<< "$a"
    name="$(echo "$name" | tr -d ' ')"
    card="$(echo "$card" | tr -d ' ')"
    if [ ${#WANT[@]} -gt 0 ]; then
        hit=no
        for w in "${WANT[@]}"; do [ "$w" = "$name" ] && hit=yes; done
        [ "$hit" = no ] && continue
    fi
    [ "$NOCARD" = yes ] && [ "$card" = yes ] && continue
    RUNS+=("$name|$card|$only")
done

if [ ${#RUNS[@]} -eq 0 ]; then
    echo "nothing selected"
    usage
    exit 2
fi

echo "running ${#RUNS[@]} area(s) in parallel"
echo

START=$(date +%s)
PIDS=()
NAMES=()

for r in "${RUNS[@]}"; do
    IFS='|' read -r name card only <<< "$r"
    # Each area gets its own tag, which is what gives it its own port, data
    # directory and card. That isolation was built for parallel agents and
    # is exactly what makes parallel areas safe.
    flags="--tag rg$name --only=$only"
    [ "$card" = yes ] && flags="$flags --card"
    ( "$HERE/harness.sh" $flags > "/tmp/rg-$name.log" 2>&1 ) &
    PIDS+=($!)
    NAMES+=("$name")
    printf "  started %-9s %s\n" "$name" "$only"
done

echo
FAILED=0
for i in "${!PIDS[@]}"; do
    if wait "${PIDS[$i]}"; then
        st="PASS"
    else
        st="FAIL"
        FAILED=$((FAILED + 1))
    fi
    n="${NAMES[$i]}"
    passes=$(grep -c "  PASS" "/tmp/bbs-rg$n/out.txt" 2>/dev/null || echo 0)
    printf "  %-4s %-9s %4s checks   /tmp/rg-%s.log\n" "$st" "$n" "$passes" "$n"
    if [ "$st" = FAIL ]; then
        grep -n "FAIL" "/tmp/bbs-rg$n/out.txt" 2>/dev/null | head -8 | sed 's/^/        /'
    fi
done

END=$(date +%s)
echo
echo "$((END - START))s wall clock"

# Coverage: a test in no area is a test that a full parallel run silently
# skips, which would make this worse than the serial suite rather than
# better. Naming them is cheap and keeps the area list honest.
ALL_ONLY=$(printf '%s\n' "${AREAS[@]}" | cut -d'|' -f3 | tr ',' '\n' | tr -d ' ' | sort -u)
MISSING=$(grep -o '^def test_[a-z0-9_]*' "$HERE/testclient.py" | sed 's/^def test_//' | sort -u | while read -r t; do
    hit=no
    while read -r pat; do
        [ -n "$pat" ] && case "$t" in *"$pat"*) hit=yes ;; esac
    done <<< "$ALL_ONLY"
    [ "$hit" = no ] && echo "$t"
done)
if [ -n "$MISSING" ]; then
    echo
    echo "NOT IN ANY AREA (a full run would skip these):"
    echo "$MISSING" | sed 's/^/  /'
fi

exit $([ "$FAILED" -eq 0 ] && echo 0 || echo 1)
