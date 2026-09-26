# 1.1.1 release-gate regression, final, 2026-09-25

Run against main at `d6681f7` (the merge of `rel-1.1.1`, `BBS_VERSION`
"1.1.1"), in its own worktree (`release-prep/wt-reg111b`, detached), as
the release-gate full regression called for once a milestone's `.0`/`-dev`
work has settled and before the tag is treated as final. Baseline is
today's `internal/regression-1.1.1-2026-09-25.md` (run against
`v1.1.1-dev.1`, 38fcca9). Every generated `sdkconfig.<env>` and `.pio/`
build directory were absent before building. All tests ran on 127.0.0.1;
no COM port was opened. No source was changed.

**Verdict: SAFE TO SHIP.** All 11 environments (8 board envs + the 3
bench-test envs) build clean, 0 warnings. `make test` is 590/590. The
full suite ran to completion in a single pass on both the no-card and
the card profile of the reference board (no split needed, the harness's
new 900 s per-test budget and 4-hour backstop were never touched):
**1421/0/28 skip, ALL PASS** (no card) and **1817/0/9 skip, ALL PASS**
(card). Both of dev.1's failure clusters are gone: `test_announce_directory`
and `test_closed_configured` are already fixed on this tree (confirmed by
reading the diff in the test file and by reproducing both in isolation).
Every host profile (S3, Freenove, AI-Thinker ESP32-CAM) passes with and
without a card, including new coverage on espcam's own camera path that
dev.1 flagged as a hole. All four `lrzsz_check.py` combinations pass. All
eight `--fresh` combinations pass. Sizes are stable and move in exactly
the direction CLAUDE.md's own dev.0 note already recorded and the task
predicted (roughly -1.7 KB static DRAM, -60 KB flash per board, from the
IPv6/SoftAP/Enterprise trims); nothing crosses the 10 KB flash / 2 KB RAM
flag threshold. No blockers found.

## 1. Sizes, all eleven environments, 0 warnings on every one

Measured off each ELF's `_bss_end` symbol against the DRAM segment
origin (`memory.ld`), not PlatformIO's RAM percentage, per CLAUDE.md's
standing rule. ESP32/WROVER-class boards: origin `0x3FFB0000`, ceiling
180,736. S3: origin `0x3FC88000`, ceiling 341,760 (shared with IRAM).

| Env | Static DRAM used | Free | % of DRAM | Flash (bin) | % of 1.5 MB OTA slot |
|---|---|---|---|---|---|
| esp32dev | 161,336 | 19,400 | 89.3% | 1,230,432 | 78.2% |
| esp32dev_release | 161,336 | 19,400 | 89.3% | 1,230,432 | 78.2% |
| ws_s3_lcd147 | 247,784 of 341,760 | 93,976 | 72.5% | 1,270,224 | 80.8% |
| ws_s3_lcd147_release | 247,784 of 341,760 | 93,976 | 72.5% | 1,270,240 | 80.8% |
| freenove_wrover_cam | 172,808 | 7,928 | 95.6% | 1,303,280 | 82.9% |
| freenove_wrover_cam_release | 172,808 | 7,928 | 95.6% | 1,303,280 | 82.9% |
| esp32cam_aithinker | 174,264 | 6,472 | 96.4% | 1,355,840 | 86.2% |
| esp32cam_aithinker_release | 174,264 | 6,472 | 96.4% | 1,355,856 | 86.2% |
| esp32dev_backuptest (bench) | 161,324 | 19,412 | 89.3% | 1,229,876 | 78.2% |
| esp32dev_diag (bench) | 161,332 | 19,404 | 89.3% | 1,230,576 | 78.2% |
| esp32dev_wdttest (bench) | 161,332 | 19,404 | 89.3% | 1,230,356 | 78.2% |

Deltas against today's dev.1 report (esp32dev 163,008/17,728 free,
1,291,056 flash; S3 249,712/341,760, 1,328,592; Freenove 174,488/6,248,
1,363,888; ESP32-CAM 175,944/4,792, 1,417,616): every board moved by
about -1,672 to -1,928 bytes of static DRAM and -58 KB to -61.8 KB of
flash, matching the task's own expectation of roughly -1.7 KB static and
-60 KB image per board, from the IPv6/SoftAP/Enterprise trims, almost
exactly. These also match CLAUDE.md's own already-recorded 1.1.1 figures
(161,336 / 172,808 / 174,264 / 247,784 static DRAM; flash within a few
hundred bytes) to within measurement noise from the small fixes that
landed between that note and the rel-1.1.1 merge. No board crosses the
flag thresholds; the reference WROOM has more headroom than at dev.1
(19,400 bytes free, 89.3% of DRAM rather than dev.1's implied 90.2%).

`include/secrets.h` does not exist in this worktree (a clean checkout of
`d6681f7`), so every build, release and non-release, compiled with no
fallback Wi-Fi baked in.

Build commands, each clean, exit 0, 0 warnings:
`pio run -e esp32dev -e esp32dev_release -e ws_s3_lcd147 -e
ws_s3_lcd147_release -e freenove_wrover_cam -e freenove_wrover_cam_release
-e esp32cam_aithinker -e esp32cam_aithinker_release` and, separately,
`pio run -e esp32dev_backuptest -e esp32dev_diag -e esp32dev_wdttest`
(the three bench-only envs the task also asked for).

## 2. make test (host/)

**590/590 passed, 0 failed**, across all 13 binaries, no warnings. (Was
584/584 at dev.1 across the same 13 binaries; the 8th binary's count
grew from 18 to 24, plus 6 checks, consistent with the camera-picture
work already reviewed in the dev.1 report.)

## 3. The reference board (esp32dev), full suite, single pass

`tools/harness.sh --tag reg111b --backup` (no card) and `tools/harness.sh
--tag reg111bcard --card --backup` (with a card), both run to completion
in one invocation each. Neither test hit its own 900 s budget
(`--test-timeout`) and neither run's clock touched the 4-hour backstop
(`BBS_HARNESS_TIMEOUT`); there is no "outlasted BBS_HARNESS_TIMEOUT"
line in either output, and both boards' processes exited cleanly at the
end of their own run. The 1.1.1 per-test budget and 4-hour backstop did
exactly what they were built for: the card run, which needed splitting
into five separate `--only=` invocations against dev.1's 1-hour outer
clock, now runs whole in one shot.

### Without a card

**1,421 passed, 0 failed, 28 skipped.** Every skip is correctly gated
(needs a card, needs `--board s3`/`fncam`/`espcam`, needs a `--fresh`
board of its own); none is an unexpected skip.

### With a card

**1,817 passed, 0 failed, 9 skipped.** Every skip is again a board
profile or a `--fresh`-only test correctly declining on this board.

Zero failures across 3,238 checks between the two runs. Both of dev.1's
two failure clusters are gone, and reproduced/confirmed as already fixed
rather than as newly passing by luck:

- **test_announce_directory** (tools/testclient.py, around line 11570):
  the line dev.1 flagged, `page = get("/")[1]`, now reads `page =
  get("/directory")[1]`, with a comment in the source itself saying the
  board list moved to /directory in site 1.3.0 and / is the front page.
  All six checks that failed at dev.1 (the row draw, the machine badge,
  the PETSCII and guests badges, a badge per running feature, the two
  support symbols, the three interests) pass in both full-suite runs and
  in the isolated named run in section 7. The unleashed_directory sibling
  repo beside this worktree is at 1.3.10, consistent with the fix.
- **test_closed_configured**: the opening assertion's own printed text
  now reads "the board has its own sysop password and no closed line, or
  closed = no" (bbs-reg111bcard/out.txt, around line 1895), which is
  dev.1's own recommended fix direction: accept an explicit closed = no
  as equivalent to no line, matching what the restore path actually
  promises. It passes at its declared position in ORDER_NAMES, directly
  after the three backup/restore tests that write an explicit closed =
  no into the restored system.cfg, which is exactly the sequence that
  failed it at dev.1.

**test_operator_notes** (the temp-file race the developer fixed): its
"OPERATOR: notes for a sysop already on, with mail off" section runs
clean in both the no-card and card full suites, and again in the named
run below. No sign of the race.

## 4. Host profiles: S3, Freenove (--board fncam), ESP32-CAM (--board espcam)

Binaries built once each (make -s bbs_host_s3 bbs_host_fncam
bbs_host_espcam), then exercised in parallel under distinct harness
tags/ports; no two invocations rebuilt the same binary concurrently.

| Profile | Test | No card | With card |
|---|---|---|---|
| S3 | --only=board_s3 | 71/0 | 71/0 |
| Freenove | --only=board_fncam | 23/0 | 23/0 |
| Freenove | --only=camera | 5 passed, 3 correctly SKIP (needs card) | 66/0 |
| AI-Thinker ESP32-CAM | --only=board_espcam | 5/0 | 5/0 |
| AI-Thinker ESP32-CAM | --only=camera | 5 passed, 3 correctly SKIP | 63/0 |

All ten invocations exited 0. This closes dev.1's finding 3 (the espcam
host profile had almost no camera-behaviour test coverage): test_board_espcam
now exists (tools/testclient.py:6445, "the board's own LED is the red one
on 33 ... CONFIG refuses the LED on GPIO 2, the card's MISO ... a WROOM's
LED on GPIO 2 is dropped at boot") and CAM_BOARD (tools/testclient.py:6543)
now carries an espcam entry (OV2640, uxga/xga, pin 4), so test_camera,
test_camera_failed_start, test_camera_silent and test_announce_camera all
run against the AI-Thinker profile too, not only Freenove. With a card, 63
checks exercise the AWB and picture-correction work end to end on espcam's
own pin table and sensor default, the same work dev.1 had reviewed on the
Freenove profile alone.

**Minor, cosmetic, not a regression and not blocking:** the three
skipped-without-a-card messages under --only=camera on the espcam profile
still print "needs tools/harness.sh --board fncam --card" literally, even
when run with --board espcam (tools/testclient.py, the shared SKIP strings
in test_camera/test_camera_silent/test_announce_camera). It skips
correctly either way; the wording just names the wrong board. Worth a
one-line fix (parameterise the message on HOST_BOARD) next time that file
is touched, not worth holding the release for.

## 5. The --fresh tests, each on its own --fresh board

Each of these needs a --fresh board of its own (the harness's --fresh
flag and the tests' own top-of-function comments); each was run
individually, with and without a card, per dev.1's own corrected method
(running several together on one board consumes the "still on the
default" precondition the others need).

| Test | No card | With card |
|---|---|---|
| first_setup | 17/0 | 17/0 |
| closed_fresh | 42/0 | 42/0 |
| setup_abort | 6/0 | 6/0 |
| backup_published_default | 19/0 | 19/0 |

All eight runs clean, and every count matches dev.1 exactly.

## 6. tools/lrzsz_check.py

lrzsz (sz/rz) confirmed installed in WSL (/usr/bin/sz, /usr/bin/rz).
Stood up a minimal board by hand (its own system.cfg, a Drop Box file
area, a card directory), same shape as dev.1's method, since the script
drives a live board directly rather than through harness.sh. All four
combinations pass, byte counts matching dev.1 exactly:

| Protocol | Telnet BINARY | Result |
|---|---|---|
| XMODEM | agreed | PASS, 1,152 received for 1,026 sent (0x1A padding, documented) |
| XMODEM | refused | PASS, 1,152 received for 1,026 sent |
| YMODEM | agreed | PASS, 1,026 received for 1,026 sent (byte-exact) |
| YMODEM | refused | PASS, 1,026 received for 1,026 sent (byte-exact) |

## 7. The named 1.1.1 tests, confirmed in ORDER_NAMES and passing

Per the task, ran link_line, busy, fx_codes, announce_closed,
closed_configured, cgnat_local, board_espcam, hardware, config_tz_bad,
room_private, lights_disk, screens_install, announce_directory as an
explicit --only= list, both without and with a card, on the reference
board (board_espcam correctly SKIPs here; it is exercised for real under
--board espcam in section 4).

- No card: **116 passed, 0 failed, ALL PASS.**
- With card: **139 passed, 0 failed, ALL PASS.**

Every one of the thirteen names matched a test in ORDER_NAMES and ran in
its declared position (substring match against run_selected); none was
silently absent. This run is also where test_closed_configured's fixed
wording was first spotted (see section 3), and where
test_announce_directory's six previously-failing checks were confirmed
passing against the real unleashed_directory sibling repo (1.3.10) in
isolation, before being cross-checked in the full ordered run.

## 8. Findings

No blocking findings. One cosmetic, non-blocking item, already covered
in section 4: the espcam profile's camera-SKIP messages under
--only=camera still name fncam instead of the running board. Fix at
leisure; does not affect correctness or test validity, since the SKIP
condition itself (card is None) is right for either board.

## Verdict

**Safe to ship 1.1.1 as tagged at d6681f7.** Every board and bench
environment builds clean with no warnings. The host suite, the full
reference-board suite (both profiles, single pass, no timeout split
needed), every host board profile, every --fresh scenario, all four
lrzsz combinations, and the thirteen named 1.1.1 tests all pass. Both
failure clusters found in today's earlier dev.1 regression are already
fixed on this tree and were re-verified rather than assumed. Sizes moved
in exactly the direction and magnitude expected from the network-stack
trims, with no board within a hair of its flag threshold. Nothing here
blocks the tag; the one cosmetic wording nit in section 4 is worth a
line in the next patch, not a reason to hold this one.
