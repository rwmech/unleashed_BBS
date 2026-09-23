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
FRESH=no
ARGS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --tag)   TAG="$2"; shift 2 ;;
        --card)  CARD=yes; shift ;;
        # A board as it leaves the web installer: no staff passwords in its
        # config, so it runs on the published default and offers setup.
        # Pair it with --only=first_setup; the rest of the suite assumes a
        # configured sysop.
        --fresh) FRESH=yes; shift ;;
        *)       ARGS="$ARGS $1"; shift ;;
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
# The host has no radio. This makes it report being on a network, the way a
# board that joined through include/secrets.h does, so CONFIG wifi's
# fallback to the live network can be tested (test_config_wifi_live).
export BBS_HOST_SSID=HostNet

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

BBS_BACKUP_TEST_OPEN=1 ./bbs_host "$DATA" "$PORT" > "$LOG" 2>&1 &
PID=$!
sleep 1

cd "$PROJ"
set +e
# 3600, not 1200. The full suite passed twenty minutes in 0.22.0 and the
# limit killed it mid-test with no summary line, which reads like a hang
# rather than a clock running out.
BBS_DATA="$DATA" timeout 3600 python3 -u tools/testclient.py 127.0.0.1 "$PORT" $ARGS > "$OUT" 2>&1
RC=$?
set -e
kill $PID 2>/dev/null || true

echo "tag $TAG  port $PORT  directory $DIRPORT  card $CARD"
echo "$(grep -c '  PASS' "$OUT" 2>/dev/null || echo 0) passed, $(grep -c '  FAIL' "$OUT" 2>/dev/null || echo 0) failed"
grep -n 'FAIL' "$OUT" 2>/dev/null || true
tail -1 "$OUT"
echo "full output: $OUT"
exit $RC
