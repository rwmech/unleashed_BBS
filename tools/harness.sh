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
#
#               Examples:
#                 tools/harness.sh --backup
#                 tools/harness.sh --tag files --card --only=files
#
#               Results land in /tmp/bbs-<tag>/out.txt and the board's log
#               in /tmp/bbs-<tag>/host.log.
#
# Targets:      Linux host build (WSL)
# See also:     CLAUDE.md
#
# Copyright 2026 - Robert Mech
# License:      GNU General Public License v2 or later
# SPDX-License-Identifier: GPL-2.0-or-later
# ===========================================================================

set -e

TAG=main
CARD=no
ARGS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --tag)  TAG="$2"; shift 2 ;;
        --card) CARD=yes; shift ;;
        *)      ARGS="$ARGS $1"; shift ;;
    esac
done

PROJ=$(cd "$(dirname "$0")/.." && pwd)
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

# Delete the previous result before building. A failed build exits here, and
# leaving the last run's output behind means the next look at it shows a full
# passing run that has nothing to do with the code on disk. That has already
# fooled one agent into reporting a clean suite against a tree that would not
# compile.
rm -f "$OUT"

cd "$PROJ/host"
make -s

# Kill only this tag's board. Matching on the process name would take down a
# parallel run's board, which is the whole thing this file exists to stop.
pkill -f "bbs_host $DATA" 2>/dev/null || true
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
CFG

if [ "$CARD" = yes ]; then
    mkdir -p "$CARDDIR/pub/c64" "$CARDDIR/pub/empty"
    echo 'CBM PRG content' > "$CARDDIR/pub/c64/GAME.PRG"
    echo 'a text file'     > "$CARDDIR/pub/c64/NOTES.TXT"
    echo 'GAME.PRG A game from the card' > "$CARDDIR/pub/c64/FILES.BBS"
    export BBS_SD_DIR="$CARDDIR"
fi

BBS_BACKUP_TEST_OPEN=1 ./bbs_host "$DATA" "$PORT" > "$LOG" 2>&1 &
PID=$!
sleep 1

cd "$PROJ"
set +e
BBS_DATA="$DATA" timeout 1200 python3 -u tools/testclient.py 127.0.0.1 "$PORT" $ARGS > "$OUT" 2>&1
RC=$?
set -e
kill $PID 2>/dev/null || true

echo "tag $TAG  port $PORT  directory $DIRPORT  card $CARD"
echo "$(grep -c '  PASS' "$OUT" 2>/dev/null || echo 0) passed, $(grep -c '  FAIL' "$OUT" 2>/dev/null || echo 0) failed"
grep -n 'FAIL' "$OUT" 2>/dev/null || true
tail -1 "$OUT"
echo "full output: $OUT"
exit $RC
