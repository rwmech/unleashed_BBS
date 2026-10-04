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
# Usage:        tools/harness.sh [--tag NAME] [--card] [--axis AXIS]
#                                [testclient args...]
#
#                 --tag NAME   isolate this run (default "main")
#                 --card       give the board an SD card
#                 --axis AXIS  run one of the three test axes (1.2.1a,
#                              internal/test-reorg-2026-10-04.md), or a
#                              comma list of them:
#
#                                feature  what a caller or a sysop can do.
#                                         The regression that runs at every
#                                         release, on the reference build.
#                                         With --board it is Rob's carve-out
#                                         alone: the S3-only features (SSH).
#                                chip     what differs by chip family: the
#                                         usable GPIO range, the pins refused
#                                         by name, the console port. Run it
#                                         twice, once plain and once
#                                         --board s3.
#                                board    one board's own facts. Needs
#                                         --board NAME: it is that profile's
#                                         suite and nothing else.
#
#                              It composes with --board, --card, --jobs,
#                              --changed and --only: --axis feature
#                              --only=messaging is the messaging group's
#                              feature tests, and nothing from the other two
#                              axes. Without --axis nothing changes: a run
#                              selects exactly what it always did.
#
#                              Examples:
#                                tools/harness.sh --axis feature --jobs 16
#                                tools/harness.sh --axis chip --board s3
#                                tools/harness.sh --axis board --board ws2 --card
#                 --board s3   run the host build of the Waveshare S3 profile
#                              (bbs_host_s3: the S3's pin rules, that board's
#                              defaults, the panel). Pair it with
#                              --only=board_s3; the rest of the suite is
#                              written for the reference board.
#                 --board fncam  the Freenove ESP32-WROVER CAM profile
#                              (bbs_host_fncam), with --only=board_fncam
#                 --board espcam the AI-Thinker ESP32-CAM profile
#                              (bbs_host_espcam), with --only=board_espcam
#                 --board ws43b  the Waveshare ESP32-S3-Touch-LCD-4.3B profile
#                              (bbs_host_ws43b), with --only=board_ws43b
#                 --board ws2  the Waveshare ESP32-S3-Touch-LCD-2 profile
#                              (bbs_host_ws2), with --only=board_ws2
#                 --board wseth  the Waveshare ESP32-S3-ETH profile
#                              (bbs_host_wseth), with --only=board_wseth
#                 --board mf35 the Makerfabs ESP32-S3 Parallel TFT 3.5" profile
#                              (bbs_host_mf35), with --only=board_mf35
#                 --board mf35v2 its hardware v2.0 (bbs_host_mf35v2),
#                              with --only=board_mf35v2
#                 --board g4848 the Guition ESP32-4848S040 profile
#                              (bbs_host_g4848), with --only=board_g4848
#
#                 --changed RANGE   work out --only from what a git range
#                              touched, instead of naming it by hand. RANGE
#                              is anything "git diff --name-only" accepts,
#                              such as main..HEAD or HEAD~5..HEAD.
#                              tools/changed_groups.py maps each changed
#                              file to an axis, to the board profiles that
#                              need a suite of their own, and to the
#                              tools/testclient.py groups (or test names)
#                              it can affect; it prints "file -> selection"
#                              for each one, and then this runs exactly as
#                              if --axis=<those axes> --only=<those groups>
#                              had been typed. Board profiles cannot be run
#                              here (each needs its own build), so they are
#                              named for a run of their own, or handed to
#                              tools/parallel.py by --jobs, which builds
#                              every profile it needs.
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
#                 --jobs N     run the selection as lanes side by side, N
#                              boards at a time, with and without a card at
#                              once, and merge the results into one summary
#                              (tools/parallel.py; 1.1.2 test speed). Takes
#                              --only, --changed, --tests and --card or
#                              --no-card (one mode only). Each lane is an
#                              ordinary run of this script with its own tag,
#                              port and data directory, keeps the declared
#                              order among its tests, and runs on a fast
#                              clock unless a test in it measures real time.
#                              Isolation markers in tools/testclient.py keep
#                              the tests that need a board to themselves.
#                 --fast[=N]   run the board on the host's fast clock
#                              (BBS_FAST_TIMERS, host/platform_host.cpp): the
#                              busy countdown, the goodbye linger, detection,
#                              idle and time warnings and the heartbeat all
#                              pass N times quicker (4 without a figure).
#                              Tests that time something real SKIP on it and
#                              say so; --jobs runs those on a real clock.
#                 --port P     this port instead of the one from the tag
#                              (--jobs hands each lane one that no other
#                              lane's copies can meet)
#                 --no-build   use the binary already built (--jobs builds
#                              each profile once, before any lane starts)
#                 --tests=a,b  exactly these tests by full name (testclient)
#
#               Examples:
#                 tools/harness.sh --backup
#                 tools/harness.sh --jobs 16
#                 tools/harness.sh --jobs 12 --changed main..HEAD
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
EXT=""
ARGS=""
CHANGED_RANGE=""
CHANGED_DRY=no
JOBS=""
MODE_ARG=""
FAST=""
PORT_ARG=""
BUILD=yes
AXIS=""
BOARD_NAME=""
case "${BBS_CHANGED_DRY:-}" in 1|yes) CHANGED_DRY=yes ;; esac

# One of the three axes, or a comma list of them (1.2.1a). Validated here so
# a typo is refused before a board is built rather than quietly running
# nothing.
axis_ok() {
    for a in $(echo "$1" | tr ',' ' '); do
        case "$a" in
            feature|chip|board) ;;
            *) echo "harness: no test axis called $a (feature, chip, board)"
               return 1 ;;
        esac
    done
    return 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --tag)   TAG="$2"; shift 2 ;;
        --axis)  AXIS="$2"; shift 2 ;;
        --axis=*) AXIS="${1#--axis=}"; shift ;;
        --card)  CARD=yes; MODE_ARG="--card"; shift ;;
        --no-card) MODE_ARG="--no-card"; shift ;;
        --jobs)  JOBS="$2"; shift 2 ;;
        --jobs=*) JOBS="${1#--jobs=}"; shift ;;
        --fast)  FAST=1; shift ;;
        --fast=*) FAST="${1#--fast=}"; shift ;;
        --port)  PORT_ARG="$2"; shift 2 ;;
        --no-build) BUILD=no; shift ;;
        --changed)          CHANGED_RANGE="$2"; shift 2 ;;
        --changed-dry-run)  CHANGED_RANGE="$2"; CHANGED_DRY=yes; shift 2 ;;
        --board)
            BOARD_NAME="$2"
            case "$2" in
                s3)    BIN=bbs_host_s3;    export BBS_HOST_BOARD=s3 ;;
                fncam) BIN=bbs_host_fncam; export BBS_HOST_BOARD=fncam ;;
                espcam) BIN=bbs_host_espcam; export BBS_HOST_BOARD=espcam ;;
                ws43b) BIN=bbs_host_ws43b; export BBS_HOST_BOARD=ws43b ;;
                ws2)   BIN=bbs_host_ws2;   export BBS_HOST_BOARD=ws2 ;;
                wseth) BIN=bbs_host_wseth; export BBS_HOST_BOARD=wseth ;;
                # The Makerfabs has 2 MB of PSRAM, 1.71 MB free with its
                # framebuffer on the 1.1.1 bench: SSH is counted against that.
                mf35)  BIN=bbs_host_mf35;  export BBS_HOST_BOARD=mf35
                       export BBS_HOST_PSRAM="${BBS_HOST_PSRAM:-1700000}" ;;
                mf35v2) BIN=bbs_host_mf35v2; export BBS_HOST_BOARD=mf35v2 ;;
                g4848) BIN=bbs_host_g4848; export BBS_HOST_BOARD=g4848 ;;
                *)  echo "harness: no board profile called $2 (s3, fncam, espcam, ws43b, ws2, wseth, mf35, mf35v2, g4848)"; exit 2 ;;
            esac
            shift 2 ;;
        # Plugins from their own repositories (1.2.0), already fetched into
        # ext/ (tools/plugins.py fetch NAME): the host board is built with
        # them (host/Makefile's bbs_host_ext) and each is switched on. The
        # camera satellite's tests need --ext camsat --card.
        --ext)   EXT="$2"; BIN=bbs_host_ext; shift 2 ;;
        # A board as it leaves the web installer: no staff passwords in its
        # config, so it runs on the published default and offers setup.
        # Pair it with --only=first_setup or --only=backup_published; the
        # rest of the suite assumes a configured sysop.
        --fresh) FRESH=yes; shift ;;
        *)       ARGS="$ARGS $1"; shift ;;
    esac
done

PROJ=$(cd "$(dirname "$0")/.." && pwd)

if [ -n "$AXIS" ]; then
    axis_ok "$AXIS" || exit 2
fi

# --changed: work out the axes, the board profiles and --only from a git
# range rather than typing them by hand, and --changed-dry-run (or
# BBS_CHANGED_DRY=1 alongside --changed) stops here, before anything is
# built, so the selection can be checked with no board involved.
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
    CH_AXES=$(echo "$SEL" | grep '^AXES=' | tail -1 | cut -d= -f2)
    CH_BOARDS=$(echo "$SEL" | grep '^BOARDS=' | tail -1 | cut -d= -f2)
    if [ "$WORDS" = "NONE" ]; then
        echo "harness: $CHANGED_RANGE touched nothing a test could see; not running"
        exit 0
    fi
    # The axes the range named, unless --axis said otherwise by hand. An
    # older changed_groups.py prints no AXES line at all, and then this
    # behaves exactly as it used to: the groups, every axis.
    if [ -z "$AXIS" ] && [ -n "$CH_AXES" ] && [ "$CH_AXES" != "NONE" ]; then
        AXIS="$CH_AXES"
    fi
    # Board suites need their own build, so this run cannot do them. Say
    # which, and how: never a silent skip.
    if [ -n "$CH_BOARDS" ] && [ "$CH_BOARDS" != "NONE" ] && [ -z "$BOARD_NAME" ]; then
        echo "harness: $CHANGED_RANGE also needs these boards' own suites: $CH_BOARDS"
        for b in $(echo "$CH_BOARDS" | tr ',' ' '); do
            echo "harness:   tools/harness.sh --axis board --board $b"
        done
        echo "harness:   or tools/harness.sh --jobs N --changed $CHANGED_RANGE, which builds each profile"
    fi
    # Only board suites to run, and no profile named: this run has nothing
    # to do, and the lines above say what does.
    if [ "$AXIS" = "board" ] && [ -z "$BOARD_NAME" ] && [ -z "$JOBS" ]; then
        echo "harness: nothing for this run; the board suites above are each a run of their own"
        exit 0
    fi
    if [ "$CHANGED_DRY" = yes ]; then
        exit 0
    fi
    if [ "$WORDS" != "FULL" ]; then
        ARGS="$ARGS --only=$WORDS"
    fi
    # FULL: no --only is added, which is the harness's own way of asking
    # for everything the axis holds.
fi

# The axis goes to tools/testclient.py, which filters whatever the rest of
# the flags selected. It also goes to tools/parallel.py below, which passes
# it to --plan so the lanes are planned from the axis rather than packed and
# then filtered.
if [ -n "$AXIS" ]; then
    ARGS="$ARGS --axis=$AXIS"
fi

# --jobs: the lanes are planned, built for and run by tools/parallel.py, each
# lane one ordinary run of this script. The selection (--only, --tests, or
# the --changed words worked out above) goes with it.
if [ -n "$JOBS" ]; then
    exec python3 "$PROJ/tools/parallel.py" --jobs "$JOBS" --tag "$TAG" $MODE_ARG         ${FAST:+--fast=$FAST} $ARGS
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
if [ -n "$PORT_ARG" ]; then
    PORT=$PORT_ARG
fi
# The announce test stands up a directory of its own. Give it a port from
# the same tag, or two parallel runs bind the same one and the failure
# lands on the BBS socket looking like a board bug.
DIRPORT=$((PORT + 2000))
export BBS_DIR_PORT=$DIRPORT
# The host has no radio. This makes it report being on a network, the way a
# board that joined through include/secrets.h does, so CONFIG wifi's
# fallback to the live network can be tested (test_config_wifi_live).
# Set but empty (BBS_HOST_SSID= tools/harness.sh ...) plays a station that is
# not joined, for test_board_wseth's Wi-Fi-beside-the-wire half (1.2.1).
export BBS_HOST_SSID="${BBS_HOST_SSID-HostNet}"
# A ring for the sysop rings for 45 s on a board. Ten here, so the suite can
# watch one run out without sitting through the rest (host build only; see
# ring::ringMs). Long enough that a scripted sysop always answers first.
export BBS_RING_MS=10000
if [ -n "$FAST" ]; then
    export BBS_FAST_TIMERS="$FAST"
else
    unset BBS_FAST_TIMERS
fi
# The unleashed link (1.2.0): the host's radio is UDP on 127.0.0.1, on a port
# from the tag like every other, and the pretend door box (host/linkpeer)
# listens on the next one. test_radio_link and test_doors use them.
export BBS_LINK_PORT=$((PORT + 3000))
export BBS_LINK_PEER_PORT=$((PORT + 3001))
# Three more for test_link_shared's second board and second satellite
# (1.2.0), each in a block of its own 400 wide, so none of these three lands
# on another tag's (PORT is 6500 to 6899, one a tag; peer + 1 and + 2 did:
# a neighbouring tag's radio took one, and its satellite never started).
# The radio's pair above does not have that property: tag X's peer (+3001)
# is tag X+1's radio (+3000), the same exposure the TCP offsets have
# between adjacent tags. Tag-derived runs side by side want tags whose
# ports differ by more than one; harness.sh --jobs chooses its own ports
# (tools/parallel.py port_offsets reads every PORT + N here) and is clear.
export BBS_LINK_EXTRA_PORTS="$((PORT + 3500)),$((PORT + 3900)),$((PORT + 4300))"
export BBS_HOST_EXT="$EXT"

# Delete the previous result before building. A failed build exits here, and
# leaving the last run's output behind means the next look at it shows a full
# passing run that has nothing to do with the code on disk. That has already
# fooled one agent into reporting a clean suite against a tree that would not
# compile.
rm -f "$OUT"

cd "$PROJ/host"
if [ "$BUILD" = yes ]; then
    if [ -n "$EXT" ]; then
        for n in $(echo "$EXT" | tr ',' ' '); do
            [ -f "$PROJ/ext/$n/unleashed-plugin.ini" ] || { echo "harness: ext/$n is missing: tools/plugins.py fetch $n"; exit 2; }
        done
        make -s EXT="$(echo "$EXT" | tr ',' ' ')" "$BIN"
    else
        make -s "$BIN"
    fi
    # The SSH profiles (1.1.2: every S3 board): their tests call in with
    # wolfSSH's client.
    case "$BIN" in bbs_host_s3|bbs_host_ws43b|bbs_host_ws2|bbs_host_wseth|bbs_host_mf35|bbs_host_mf35v2|bbs_host_g4848) make -s ssh_call ;; esac
    make -s linkpeer
elif [ ! -x "$BIN" ] || { case "$BIN" in bbs_host_s3|bbs_host_ws43b|bbs_host_ws2|bbs_host_wseth|bbs_host_mf35|bbs_host_mf35v2|bbs_host_g4848) [ ! -x ssh_call ] ;; *) false ;; esac; } || [ ! -x linkpeer ]; then
    echo "harness: --no-build, and host/$BIN (or ssh_call, or linkpeer) has not been built"
    exit 2
fi

# Kill only this tag's board. Matching on the process name would take down a
# parallel run's board, which is the whole thing this file exists to stop.
# The pause is only for a board that was there to be killed.
if pkill -f "$BIN $DATA" 2>/dev/null; then
    sleep 0.3
fi

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

# The unleashed link and doors (1.2.0), on the host's UDP radio.
[plugin:link]
enabled = yes

[plugin:doors]
enabled = yes
CFG

# Each plugin from its own repository, switched on (--ext).
for n in $(echo "$EXT" | tr ',' ' '); do
    printf '\n[plugin:%s]\nenabled = yes\n' "$n" >> "$DATA/user/system.cfg"
done

# SSH's own port (1.1.2) on the SSH profiles, per tag like the others: 6422
# for every run would have two tags' boards fighting over it.
case "$BIN" in bbs_host_s3|bbs_host_ws43b|bbs_host_ws2|bbs_host_wseth|bbs_host_mf35|bbs_host_mf35v2|bbs_host_g4848)
        sed -i "s/^backup_port = .*/&\nssh_port = $((PORT + 1500))/" "$DATA/user/system.cfg" ;;
esac

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
# Until the board says it is listening, not a fixed second: a quiet machine
# has it up in a few milliseconds and a loaded one may take longer than one.
# Ten seconds at most, and then the tests find out for themselves.
for _ in $(seq 1 100); do
    grep -q "listening on" "$LOG" 2>/dev/null && break
    kill -0 $PID 2>/dev/null || break
    sleep 0.1
done

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

echo "tag $TAG  port $PORT  directory $DIRPORT  card $CARD${FAST:+  fast $FAST}"
# grep -c prints 0 and fails when nothing matches, so "|| echo 0" printed a
# second 0 on its own line: "25 passed, 0" then "0 failed".
NPASS=$(grep -c '  PASS' "$OUT" 2>/dev/null) || true
NFAIL=$(grep -c '  FAIL' "$OUT" 2>/dev/null) || true
echo "${NPASS:-0} passed, ${NFAIL:-0} failed"
grep -n 'FAIL' "$OUT" 2>/dev/null || true
tail -1 "$OUT"
echo "full output: $OUT"
exit $RC
