# 1.1.1 milestone regression, 2026-09-25

Run against `v1.1.1-dev.1` (38fcca9) in its own worktree
(`release-prep/wt-reg111`, detached), per the release flow that runs the
full regression after a `.0`/`-dev` is published on the site (CLAUDE.md,
"Release flow from 1.1.0 on"). Every generated `sdkconfig.<env>` was absent
before building (no stale-defaults trap). All tests ran on 127.0.0.1; no
COM port was opened. No source was changed.

**Verdict: safe.** Every build is clean. `make test` is 584/584. The full
suite's only two failure clusters are both pre-existing test bugs, not
firmware regressions, and both are root-caused below with file:line. One
real gap was found: the ESP32-CAM host profile has almost no camera-test
coverage, which is a testing hole to close, not a defect in 1.1.1-dev.1.
Sizes are stable against the 1.1.1-dev.0 reference figures in CLAUDE.md;
nothing crosses the 10 KB flash / 2 KB RAM flag threshold.

## Sizes, all eight environments, 0 warnings on every one

Measured off each ELF's `_bss_end` symbol against the DRAM segment origin
(`memory.ld`), not PlatformIO's RAM percentage, per CLAUDE.md's standing
rule. ESP32/WROVER-class boards: origin `0x3FFB0000`, ceiling 180,736.
S3: origin `0x3FC88000`, ceiling 341,760 (shared with IRAM).

| Env | Static DRAM used | Free | Flash (bin) | vs 1.1.1-dev.0 |
|---|---|---|---|---|
| esp32dev | 163,008 | 17,728 | 1,291,056 | DRAM +0, flash +272 |
| esp32dev_release | 163,008 | 17,728 | 1,291,008 | DRAM +0, flash +224 |
| ws_s3_lcd147 | 249,712 / 341,760 | 92,048 | 1,328,592 | DRAM +0, flash +48 |
| ws_s3_lcd147_release | 249,712 / 341,760 | 92,048 | 1,328,608 | DRAM +0, flash +64 |
| freenove_wrover_cam | 174,488 | 6,248 | 1,363,888 | DRAM +120, flash +8,432 |
| freenove_wrover_cam_release | 174,488 | 6,248 | 1,363,888 | DRAM +120, flash +8,432 |
| esp32cam_aithinker | 175,944 | 4,792 | 1,417,616 | DRAM +120, flash +8,560 |
| esp32cam_aithinker_release | 175,944 | 4,792 | 1,417,616 | DRAM +120, flash +8,560 |

Reference figures (CLAUDE.md, 1.1.1-dev.0): WROOM 163,008 (17,728 free),
flash 1,290,784; Freenove 174,368 (6,368 free), flash 1,355,456; ESP32-CAM
175,824 (4,912 free), flash 1,409,056; S3 249,712 of 341,760, flash
1,328,544.

None of these deltas cross the ~10 KB flash / ~2 KB RAM flag threshold. The
esp32dev and S3 images are effectively byte-identical to dev.0 (a few
hundred bytes, noise-level). Both camera boards moved together by about
+120 bytes DRAM and +8.4 to +8.6 KB flash, which is attributable to
dev.1's documented work: the picture-correction tables (camera_pic.h),
the wordmark (camera_mark.h and camera_brand.h) and the OV2640 AWB fix.
Worth a line in the next optimize report if it keeps growing at that rate
on every camera-touching release, but it is not a flag today.

Build commands, each clean, exit 0, 0 warnings:
pio run -e esp32dev, esp32dev_release, ws_s3_lcd147, ws_s3_lcd147_release,
freenove_wrover_cam, freenove_wrover_cam_release, esp32cam_aithinker,
esp32cam_aithinker_release.

## make test (host/)

584/584 passed, 0 failed, across all 13 binaries, no warnings.
Per-binary counts: 11, 22, 48, 17, 39, 44, 51, 18, 27, 137, 13, 36, 121.

## The reference board (esp32dev), full suite

### Without a card

`tools/harness.sh --tag reg111a --backup` (the full ORDER_NAMES sequence
plus test_backup and test_ban, appended by --backup).

**1,364 passed, 6 failed, 25 skipped.** The 6 failures are one cluster,
test_announce_directory, root-caused below (finding 1). No other
failures. The 25 skips are all card-gated tests correctly recognising
there is no card.

### With a card

The harness's fixed 3,600 second internal timeout (tools/harness.sh, the
`timeout 3600` around testclient.py) is too short for the card run, as
CLAUDE.md's own notes on the bk-1.1.0 and dash-1.1.0 lanes already say.
The first attempt (--tag reg111c --card --backup) ran for the full hour
and was killed mid test_mail_compose (exit 124), having reached:

**1,403 passed, 6 failed, 6 skipped** (partial). The 6 failures are the
same test_announce_directory cluster as the no-card run, at the same
position in the sequence.

Split the remainder by group, per the task's instruction, using the same
--only= mechanism CLAUDE.md already documents for targeted runs:

- --tag reg111c2 --card --only=forums,partitions (the two groups right
  after where the timeout landed): 77 passed, 0 failed.
- --tag reg111c3 --card --only=backup_card,restore_cross_partition,
  backups_area,card_screens_manifest,restore_checks,restore_staff_report,
  restore_ends_screens,sd_no_reprobe,rewrites_keep_old,
  restore_waits_quiet,screens_command,closed_configured,
  backup_published_default (everything from the backup-card block to
  the end of the card-specific ORDER_NAMES tests, 14 test functions):
  207 passed, 1 failed, test_closed_configured, root-caused below
  (finding 2).
- --tag reg111c5 --card --only=backup,ban (the two flag-gated tests that
  only run via --backup/--ban, plus a re-verification of the
  backup-card group they overlap with): 154 passed, 0 failed, backup
  and ban both clean, and the backup-card group repeated identically
  clean, so the one failure in reg111c3 was not a flake.

**Card run combined: every section of the full suite ran at least once
with a card. Two distinct failing checks, both classified below; nothing
else.**

## The --fresh tests (task item 5)

Each of these needs "a --fresh board of its own" (the comment above their
entry in ORDER_NAMES, tools/testclient.py near line 13627). Running
first_setup, closed_fresh and setup_abort together in one --only=
invocation shares one board across all three, and the first one to
complete setup consumes the "still on the default" precondition the next
two need: 16 checks failed that way on my first attempt (tag
reg111freshc). That is my own invocation mistake, not a product bug,
corrected by giving each test its own --fresh board, which is what the
harness's --fresh flag and the tests' own comments require.

Run individually, each on its own fresh board, with and without a card:

| Test | No card | With card |
|---|---|---|
| first_setup | 17/0 | 17/0 |
| closed_fresh | 42/0 | 42/0 |
| setup_abort | 6/0 | 6/0 |
| backup_published_default | 19/0 | 19/0 |

All eight runs clean.

## Host profiles: S3, Freenove (--board fncam), ESP32-CAM (--board espcam)

### Waveshare S3

--only=board_s3 runs test_board_s3 and test_board_s3_silent.

- No card: 71 passed, 0 failed.
- With card: 71 passed, 0 failed.

### Freenove ESP32-WROVER CAM

--only=board_fncam (test_board_fncam):
- No card: 23 passed, 0 failed.
- With card: 23 passed, 0 failed.

--only=camera (test_camera, test_camera_failed_start, test_camera_silent,
test_announce_camera):
- No card: 5 passed, 3 skipped (the three that need a card correctly say
  so: needs tools/harness.sh --board fncam --card).
- With card: 65 passed, 0 failed. This exercises the actual dev.1
  camera-picture changes (AWB fix, auto levels, wordmark, bring-up
  settling) end to end.

### AI-Thinker ESP32-CAM

--only=camera against --board espcam:
- No card: 1 passed, 3 skipped.
- With card: 1 passed, 3 skipped.

The 1 pass both times is test_announce_camera's "no camera board, no
camera feature" branch. See finding 3: this is a coverage gap, not a
clean bill of health for the espcam profile's camera path.

--only=board_espcam, the invocation tools/harness.sh's own help text
documents for this profile (line about 37: "the AI-Thinker ESP32-CAM
profile ... with --only=board_espcam"): 0 passed, 0 failed, and the
harness reports FAILURES. See finding 3.

## tools/lrzsz_check.py

lrzsz (sz/rz) is installed in WSL (/usr/bin/sz, /usr/bin/rz). Stood up a
minimal board by hand (own system.cfg, a Drop Box file area, a card
directory) since the script drives a live board directly rather than
through harness.sh. All four combinations pass:

| Protocol | Telnet BINARY | Result |
|---|---|---|
| XMODEM | agreed | PASS, 1,152 received for 1,026 sent (0x1A padding, as documented) |
| XMODEM | refused | PASS, 1,152 received for 1,026 sent |
| YMODEM | agreed | PASS, 1,026 received for 1,026 sent (byte-exact) |
| YMODEM | refused | PASS, 1,026 received for 1,026 sent (byte-exact) |

## Findings, most severe first

### 1. test_announce_directory checks a page the paired site moved off, in site 1.3.0

**Not a firmware regression.** Six checks fail every time this test runs
(both harness runs, and reproduced standalone): "the board list draws its
row", "with the machine badge", "the PETSCII and guests badges", "a badge
for each feature running", "the two support symbols", "and the three
interests" (tools/testclient.py around lines 10919 to 10933). Every
JSON-API assertion in the same test passes (the directory lists the
board, it stored system as the board sent it, the four terminals,
guests, features, support, interests) so the board's announce payload
and the directory's storage of it are both correct.

**Root cause, confirmed by manual reproduction** (a standalone
unleashed_directory server, a correct token-following two-heartbeat
sequence, then fetching both / and /directory): the paired
unleashed_directory repository (checked out beside this one, currently
at 1.3.10) moved its board list off the front page in its own site 1.3.0.
index_page() in unleashed_directory/server.py, around line 8135, says so
in its own docstring, roughly: "The front page (site 1.3.0): the pitch,
front_html(), and no list ... data is accepted and unused, for callers
from before the list moved." The board list is at /directory
(unleashed_directory/server.py, directory_page(), around line 8180) now.
Confirmed directly: fetching / for a listed test board gives zero
matches of its name anywhere in 1,710 lines of HTML; fetching /directory
for the same board finds it immediately.

test_announce_directory (tools/testclient.py, around line 10917) still
does page = get("/")[1]. That is the whole bug: change it to
get("/directory")[1] and every one of the six failing checks should
pass, since the row-parsing logic itself is otherwise sound.

**Not introduced by 1.1.1-dev.1.** None of dev.1's changed files
(camera.cpp, camera_pic.h, camera_mark.h, camera_brand.h, files.cpp,
platform_esp32.cpp, platform.h, board.h, config.h) touch announce or
this test. It is a test that went stale when the sibling site repo
moved, silently, because the two repos are not built or tested together
outside this one cross-repo test.

**Fix location:** tools/testclient.py, test_announce_directory, around
line 10917.

### 2. test_closed_configured's opening assertion cannot survive its own declared position in ORDER_NAMES

**Reproduced in isolation** (--tag ordchk --card --only=backup_card,
closed_configured: 83 passed, 1 failed) and **reproduced in the full
card run** (reg111c3, same single failure, same message). Not order
sensitivity from an unusual --only combination: this is ORDER_NAMES's
own declared sequence (tools/testclient.py, around lines 13627 to
13636), which places test_backup_card, test_backup_card_nightly and
test_restore_cross_partition immediately before test_closed_configured.

**Root cause**, src/core/ziparc.cpp, around lines 1355 to 1391
(ZipImport::applyItem): a restore always writes an explicit closed = yes
or closed = no line into the restored system.cfg, even when the original
backup had no line at all. This is deliberate and correctly documented
in the code and in CLAUDE.md's 1.1.0 notes, roughly: "a file that does
not say is given the live state ... so a restore never opens or shuts a
board by itself." It is the right behaviour for a restore.

The bug is in the test: tools/testclient.py around line 8876's opening
check, cfg_line("sysop_password") is not None and closed_value() is
None, demands there be no closed line at all, which can only be true on
a harness board that has never been restored onto. Since the test's own
declared position in ORDER_NAMES runs it directly after three tests that
each restore the board from a backup, the precondition it asserts is
never true where it runs. This is pre-existing (nothing in dev.1 touches
ziparc.cpp, closed, or this test), and the shape is exactly the kind of
stale assumption CLAUDE.md's own postmortems warn about: a check written
against how the board looked at the time, not against where it now sits
in the sequence.

**Fix direction:** either accept an explicit closed = no as equivalent
to "no line" in that assertion, or move test_closed_configured earlier
in ORDER_NAMES, before the backup-restore block. The former is smaller
and matches what the restore path actually promises, which is that the
live state carries forward, not that no line survives.

### 3. The ESP32-CAM (espcam) host profile has almost no camera-behaviour test coverage

test_camera, test_camera_failed_start and test_camera_silent are each
hardcoded to HOST_BOARD != "fncam" meaning SKIP (tools/testclient.py
around lines 6106, 6478 and 6584), so none of them ever run against
bbs_host_espcam, with or without a card. Only test_announce_camera
partially exercises espcam, and only its "not a camera board" branch,
since the harness's default system.cfg never enables the camera plugin
regardless of profile.

Worse, tools/harness.sh's own help text, around line 37, tells a reader
to run --only=board_espcam for this profile, matching the pattern set by
--only=board_s3 and --only=board_fncam. No test_board_espcam function
exists. Running the documented command matches zero tests and the
harness reports FAILURES, a false failure from following the tool's own
instructions, confirmed here (0 passed, 0 failed, exit non-zero because
run_selected returns False when nothing matches).

This predates 1.1.1-dev.1 (ESPCAM 1.0.1 shipped in 1.1.1-dev.0), so it is
not a regression from this milestone, but it means every camera change
that has landed on the AI-Thinker board so far, including the AWB and
picture-correction work reviewed in this very run, has been host-tested
on the Freenove profile only and never on espcam's own pin table, sensor
assumptions or flash-LED wiring. Worth closing before more camera work
lands, given CLAUDE.md's own account of how much of the espcam bring-up
(GPIO0 as XCLK, GPIO2 as the card, SDMMC one-bit not working on this
slot, GPIO4's flash LED on a bare pull-up) was found only on the bench.

**Fix direction:** either generalise the three camera tests to accept
HOST_BOARD in ("fncam", "espcam") with per-board expected values where
they differ (sensor, sizes), or write a test_board_espcam the way S3 and
Freenove have, and make the camera group run against espcam too.

### 4. Known, not investigated further, per the task's classification note

- **Lights tests fail on the S3, Freenove and ESP32-CAM host profiles.**
  Not reproduced here beyond confirming the reference-board lights suite
  itself is clean (see the card run above); these tests assume the
  reference board's pin table and are already known to fail on the other
  profiles.
- **test_closed_configured after test_backup_card** is finding 2 above,
  now root-caused rather than merely known.
- **"sparkle: twinkles out of the dark" is flaky** (tools/testclient.py
  around line 5465, samples about 16 or more frames over a 5 second
  window and asserts at least one lit and one dark frame). It passed
  cleanly in this run's one pass through the manual-effects section of
  the card run's lights suite, consistent with a probabilistic assertion
  rather than a hard failure; not re-run repeatedly to force the flake,
  since the task said to classify rather than rediscover it.

## Ranked fix list for 1.1.1

1. test_announce_directory: fetch /directory, not / (tools/testclient.py
   around line 10917). Cheap, fixes six false failures on every full run.
2. test_closed_configured: accept an explicit closed = no as "no line"
   at its declared position, or move it earlier in ORDER_NAMES
   (tools/testclient.py around lines 8876 and 13627 to 13636). Cheap,
   fixes the one remaining false failure on every card run.
3. Camera test coverage for the espcam host profile
   (tools/testclient.py around lines 6106, 6478 and 6584, and a
   test_board_espcam to match tools/harness.sh's own documented
   invocation). Larger, and the one with real consequence: it is the
   only gap here that could let an actual espcam regression through
   unnoticed.

None of these block the milestone; all three are test-side.
