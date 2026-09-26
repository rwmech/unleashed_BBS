#!/bin/sh
# ===========================================================================
#  µnleashed BBS
#  Electronic freedom on a microcontroller.
# ===========================================================================
#
# File:         tools/harness.sh
# Module:       Test harness
#
# Purpose:      Build the host BBS, stand one up on a throwaway data
#               directory, run tools/testclient.py against it, and clean up.
#
#               Isolated by default. Every run gets its own port, its own
#               data directory, its own card and its own log, derived from a
#               tag. Two runs with different tags cannot see each other.
#
#               That matters because this used to live in /tmp as a script
#               with the port and the paths written into it, and it killed
#               bbs_host by process name. Two agents working on unrelated
#               files would each tear down the other's board mid-test, and
#               the symptom was a broken pipe that reads exactly like a bug
#               in whatever was being tested. Parallel work is gated by the
#               tag now rather than by remembering.
#
# Usage:        tools/harness.sh [--tag NAME] [--card] [testclient args...]
#
#                 --tag NAME   isolate this run (default "main")
#                 --card       give the board an SD card
#                 --board s3   run the host build of the Waveshare S3 profile
#                              (bbs_host_s3: the S3's pin rules, that board's
#                              defaults, the panel). Pair it with
#                              --only=board_s3; the rest of the suite is
#                              written for the reference board.
#                 --board fncam  the Freenove ESP32-WROVER CAM profile
#                              (bbs_host_fncam), with --only=board_fncam
#                 --board espcam the AI-Thinker ESP32-CAM profile
#                              (bbs_host_espcam), with --only=board_espcam
#
#                 --changed RANGE   work out --only from what a git range
#                              touched, instead of naming it by hand. RANGE
#                              is anything "git diff --name-only" accepts,
#                              such as main..HEAD or HEAD~5..HEAD.
#                              tools/changed_groups.py maps each changed
#                              file to the tools/testclient.py groups (or
#                              test names) it can affect, prints "file ->
#                              groups" for each one, and then runs exactly
#                              as if --only=<those groups> had been typed.
#                              A file the table has no mapping for still
#                              selects something: the widest sensible set,
#                              which here means the whole suite, and the
#                              file is named so that is not a silent
#                              choice. A range that only touched docs or
#                              non-test tooling says so and exits 0 without
#                              building or running anything.
#                 --changed-dry-run RANGE   the same selection, printed and
#                              exited on before the host build starts, so
#                              it can be checked with no board involved.
#                              Setting BBS_CHANGED_DRY=1 alongside
#                              --changed RANGE does the same thing; the two
#                              forms exist so a script can use whichever is
#                              easier to spell.
#
#               Examples:
#                 tools/harness.sh --backup
#                 tools/harness.sh --tag files --card --only=files
#                 tools/harness.sh --changed main..HEAD
#                 BBS_CHANGED_DRY=1 tools/harness.sh --changed main..HEAD
#
#                 --only takes a comma separated list, and a few words stand
#                 for groups of tests that share a subsystem:
#
#                   messaging  mail, forums, chat        (the shared editor)
#                   places     forums, files, chat, xfer (session owners)
#                   storage    files, forums, sd, backup (anything on the card)
#                   shell      menus, sysinfo, config    (the core's screens)
#                   login      accounts, guest, sysop    (getting in)
#                   terminal   ansi, petscii, ascii      (the three flavours)
#
#                 tools/harness.sh --tag m --card --only=messaging
#                 tools/harness.sh --tag m --card --only=forums,sysinfo
#
#                 A full run is the gate before a commit or a flash. These
#                 are for the twenty runs in between, and a change that is
#                 cheap to verify gets verified far more often.
#
#               Results land in /tmp/bbs-<tag>/out.txt and the board's log
#               in /tmp/bbs-<tag>/host.log.
#
#               Each test has a budget of its own (1.1.1): 900 s, or
#               --test-timeout=N passed through to testclient.py, or
#               BBS_TEST_TIMEOUT. A test that runs out, or raises, fails by
#               name and the run goes on. The whole run's clock is only a
#               backstop: BBS_HARNESS_TIMEOUT, four hours by default.
#
# Targets:      Linux host build (WSL)
# See also:     CLAUDE.md
#
# Copyright 2026 - Robert Mech
# License:      GNU General Public License v3 or later
# SPDX-License-Identifier: GPL-3.0-or-later
# ===========================================================================

set -e

TAG=main
CARD=no
FRESH=no
BIN=bbs_host
ARGS=""
CHANGED_RANGE=""
CHANGED_DRY=no
case "${BBS_CHANGED_DRY:-}" in 1|yes) CHANGED_DRY=yes ;; esac
while [ $# -gt 0 ]; do
    case "$1" in
        --tag)   TAG="$2"; shift 2 ;;
        --card)  CARD=yes; shift ;;
        --changed)          CHANGED_RANGE="$2"; shift 2 ;;
        --changed-dry-run)  CHANGED_RANGE="$2"; CHANGED_DRY=yes; shift 2 ;;
        --board)
            case "$2" in
                s3)    BIN=bbs_host_s3;    export BBS_HOST_BOARD=s3 ;;
                fncam) BIN=bbs_host_fncam; export BBS_HOST_BOARD=fncam ;;
                espcam) BIN=bbs_host_espcam; export BBS_HOST_BOARD=espcam ;;
                *)  echo "harness: no board profile called $2 (s3, fncam, espcam)"; exit 2 ;;
            esac
            shift 2 ;;
        # A board as it leaves the web installer: no staff passwords in its
        # config, so it runs on the published default and offers setup.
        # Pair it with --only=first_setup or --only=backup_published; the
        # rest of the suite assumes a configured sysop.
        --fresh) FRESH=yes; shift ;;
        *)       ARGS="$ARGS $1"; shift ;;
    esac
done

PROJ=$(cd "$(dirname "$0")/.." && pwd)

# --changed: work out --only from a git range rather than typing it by
# hand, and --changed-dry-run (or BBS_CHANGED_DRY=1 alongside --changed)
# stops here, before anything is built, so the selection can be checked
# with no board involved.
if [ -n "$CHANGED_RANGE" ]; then
    set +e
    SEL=$(python3 "$PROJ/tools/changed_groups.py" "$CHANGED_RANGE")
    RC=$?
    set -e
    echo "$SEL"
    if [ $RC -ne 0 ]; then
        echo "harness: tools/changed_groups.py could not read the range '$CHANGED_RANGE'"
        exit 2
    fi
    WORDS=$(echo "$SEL" | grep '^GROUPS=' | tail -1 | cut -d= -f2)
    if [ "$WORDS" = "NONE" ]; then
        echo "harness: $CHANGED_RANGE touched nothing a test could see; not running"
        exit 0
    fi
    if [ "$CHANGED_DRY" = yes ]; then
        exit 0
    fi
    if [ "$WORDS" != "FULL" ]; then
        ARGS="$ARGS --only=$WORDS"
    fi
    # FULL: no --only is added, which is the harness's own way of asking
    # for the whole suite.
fi

DIR=/tmp/bbs-$TAG
DATA=$DIR/data
CARDDIR=$DIR/card
OUT=$DIR/out.txt
LOG=$DIR/host.log

# A port from the tag, so two tags cannot collide and the same tag is stable
# across runs. 6400 stays the hand-testing port and is never taken here.
PORT=$(printf '%s' "$TAG" | cksum | cut -d' ' -f1)
PORT=$((6500 + PORT % 400))
# The announce test stands up a directory of its own. Give it a port from
# the same tag, or two parallel runs bind the same one and the failure
# lands on the BBS socket looking like a board bug.
DIRPORT=$((PORT + 2000))
export BBS_DIR_PORT=$DIRPORT
# The host has no radio. This makes it report being on a network, the way a
# board that joined through include/secrets.h does, so CONFIG wifi's
# fallback to the live network can be tested (test_config_wifi_live).
export BBS_HOST_SSID=HostNet
# A ring for the sysop rings for 45 s on a board. Ten here, so the suite can
# watch one run out without sitting through the rest (host build only; see
# ring::ringMs). Long enough that a scripted sysop always answers first.
export BBS_RING_MS=10000

# Delete the previous result before building. A failed build exits here, and
# leaving the last run's output behind means the next look at it shows a full
# passing run that has nothing to do with the code on disk. That has already
# fooled one agent into reporting a clean suite against a tree that would not
# compile.
rm -f "$OUT"

cd "$PROJ/host"
make -s "$BIN"

# Kill only this tag's board. Matching on the process name would take down a
# parallel run's board, which is the whole thing this file exists to stop.
pkill -f "$BIN $DATA" 2>/dev/null || true
sleep 0.3

rm -rf "$DIR"
mkdir -p "$DATA/user" "$CARDDIR"
cp -r "$PROJ/data/." "$DATA/"
rm -f "$DATA/system.cfg" "$DATA/users.txt" "$DATA/calls.log"
rm -rf "$DATA/logs"

cat > "$DATA/user/system.cfg" <<CFG
tz = UTC0
sysop_password = testsysop
cosysop1_password = testco1
cosysop2_password = testco2
call_minutes = 60
day_minutes = 480
backup_port = $((PORT + 1000))
# The host has no radio, so these only exercise the parser and the backup:
# a '#' and a space inside both, which must survive a download and an
# upload whole.
wifi_ssid = Test#Net
wifi_password = pa#ss word1

[plugin:example]
enabled = yes
read = all
write = staff
admin = sysop
greeting = howdy

[plugin:chat]
enabled = yes
read = all
write = all
admin = sysop
room = Main

[plugin:announce]
enabled = yes
name = Test Board
owner = Tester
description = A board under test
servers = http://127.0.0.1:$DIRPORT/announce
interval = 60

[plugin:serial]
enabled = yes
read = all
write = staff
admin = sysop
baud = 115200
format = 8N1

[plugin:files]
enabled = yes
read = all
write = staff
area1 = pub/c64 | C64 Downloads
area2 = pub/empty | Empty Area
area3 = made/bythebbs | Made By The BBS
area4 = admin/screens | Screens | staff | sysop
# Six fields: path | name | read | up | down | del. Exercises the
# four-level format and gives an ordinary caller somewhere to upload,
# which area1 deliberately does not.
area5 = pub/drop | Drop Box | all | users | all | sysop

[plugin:forums]
enabled = yes
read = all
write = users
# Seven fields: key | name | about | read | start | reply | mod.
# general leaves the levels to the plugin, which exercises every fallback.
topic1 = general | General | Anything at all
# news is the read-only-but-answerable shape: staff start subjects, anybody
# may reply to one. That split is the reason there are four levels and not
# two, so it gets a test rather than only a comment.
topic2 = news | Board News | What the sysop is up to | all | sysop | users | sysop

[plugin:info]
# page0 has a title and no text yet; page1 is staff only, and a caller must
# get the same answer for it as for a page that does not exist.
page0 = House rules | all
page1 = Staff notes | staff
CFG

if [ "$FRESH" = yes ]; then
    sed -i -E '/^(sysop|cosysop1|cosysop2)_password = /d' "$DATA/user/system.cfg"
    export BBS_FRESH=1
fi

if [ "$CARD" = yes ]; then
    mkdir -p "$CARDDIR/pub/c64" "$CARDDIR/pub/empty" "$CARDDIR/pub/drop"
    echo 'CBM PRG content' > "$CARDDIR/pub/c64/GAME.PRG"
    echo 'a text file'     > "$CARDDIR/pub/c64/NOTES.TXT"
    echo 'GAME.PRG A game from the card' > "$CARDDIR/pub/c64/FILES.BBS"
    export BBS_SD_DIR="$CARDDIR"
fi

BBS_BACKUP_TEST_OPEN=1 ./"$BIN" "$DATA" "$PORT" > "$LOG" 2>&1 &
PID=$!
sleep 1

cd "$PROJ"
set +e
# The clock is per test now (1.1.1): testclient.py gives each test its own
# budget (--test-timeout=N or BBS_TEST_TIMEOUT, 900 s), fails it by name if
# it runs out, and goes on to the next. This outer clock was the only one,
# an hour for the whole run, and a card run of six groups outgrew it: the
# run was killed mid-test with no summary line and everything after it never
# ran. It stays as a backstop against a wedged interpreter, long enough that
# no real run meets it: BBS_HARNESS_TIMEOUT, four hours by default.
BBS_DATA="$DATA" timeout "${BBS_HARNESS_TIMEOUT:-14400}" \
    python3 -u tools/testclient.py 127.0.0.1 "$PORT" $ARGS > "$OUT" 2>&1
RC=$?
set -e
kill $PID 2>/dev/null || true
if [ $RC -eq 124 ]; then
    echo "harness: the run outlasted BBS_HARNESS_TIMEOUT (${BBS_HARNESS_TIMEOUT:-14400} s) and was stopped" >> "$OUT"
fi

echo "tag $TAG  port $PORT  directory $DIRPORT  card $CARD"
# grep -c prints 0 and fails when nothing matches, so "|| echo 0" printed a
# second 0 on its own line: "25 passed, 0" then "0 failed".
NPASS=$(grep -c '  PASS' "$OUT" 2>/dev/null) || true
NFAIL=$(grep -c '  FAIL' "$OUT" 2>/dev/null) || true
echo "${NPASS:-0} passed, ${NFAIL:-0} failed"
grep -n 'FAIL' "$OUT" 2>/dev/null || true
tail -1 "$OUT"
echo "full output: $OUT"
exit $RC
