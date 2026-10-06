<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 - Robert Mech -->

# Test suite audit: unit, integration, regression

**Applies to versions:** firmware 1.2.1, `main` at `cb42511`. Written
2026-10-06, phase 1 of the three-tier job. Nothing in `tools/testclient.py`,
`host/` or `src/` was changed to write it.

The 1.2.2 lanes (`/p0` and `/p11`, the 32-hex token refusal, `ssh_port` in
announce, the plugin on/off list) have not merged, so none of their new
tests is in any count below. Phase 2 has to re-run the measurements on the
1.2.2 tag before it cuts anything.

Rob, 2026-10-06: "Tests SHOULD be as follows: Unit, Integration,
Regression. This shouldnt be run regression on everything every fucking
time." The rule this audit measures against: **an edge case belongs in a
unit test, and an integration test exists only to prove the wiring.** The
exception is the small set of tests that make the host behave like the
board, or test against somebody else's implementation. Those are kept and
labelled (section 6).

## 0. The answer in five lines

- **The suite is inverted, as Rob said, but the bigger waste is repetition,
  not edge cases.** 38% of lane time goes on running the same checks
  again. 25% is a second, card-mode copy of 119 tests that never touch the
  card. 13% is the eleven SSH tests on six more profiles, which compile the
  same SSH code with the same constants and give identical results
  (section 4).
- **Cutting both repetitions removes 2,362 of 5,812 executed checks and
  about 3,600 of 9,450 lane-seconds**, and loses no catch: neither
  repetition has ever found anything the first copy did not.
- **The logic-to-unit move is real but smaller than the line count
  suggests.** Readers estimate 1,146 of the 2,661 check sites as edge cases
  of logic. Only 26 of the 101 logic-heavy tests have that logic in a pure
  function today; 74 need a `src/` split first (section 7), so phase 2
  cannot move most of it without firmware work.
- **The catch experiments mostly said keep.** I ran 15 checks or check
  groups against the parent of their fix, or against a mutation that puts
  their bug back. 13 caught their bug. Two did not, and are cut or fixed
  here. One was a whole test (`test_room_narrow_effects`). The other was
  one check in `test_forums`. Two other `test_forums` checks that reading
  called vacuous turned out to catch (section 3). **Reading alone is not
  evidence for deleting a check**, and the appendix's reading notes are
  marked unverified for that reason.
- **Two harness holes matter more than any cut.** Five camera tests never
  run under `harness.sh --jobs`, the parallel runner (section 5.1). And
  several checks can never fail, so they are worse than missing (section
  5.2).

## 1. Measurements (rule B)

### 1.1 Inventory, against the brief's figures

| | Brief | Measured on `cb42511` |
|---|---|---|
| `tools/testclient.py` lines | 23,096 | **22,714** |
| `def test_` functions | 216 | **214** |
| `check(` call sites | 2,718 | **2,661** (2,576 inside tests, 85 in five helpers: `clash_check`, `operator_body`, `operator_ends_body`, `published_default_configured`, `published_default_fresh`) |
| Host unit-test files | 21 | **20** `host/test_*.cpp`, built into 21 binaries (`test_panel` twice, once as `test_panel_g4848`), **1,237 checks, about 2 s** for all of `make test` |
| SSH tests per profile | 13 | **11** (`login`, `new_caller`, `signup_no_privacy_offer`, `resize`, `host_keys`, `telnet_unchanged`, `full`, `failed_logins`, `dedicated_port`, `socket_budget`, `ymodem`). `test_ssh_lines` and `test_ssh_ten` do not exist. Neither does `host/test_bans.cpp`, which the brief cites for its coverage labels. |
| Full regression, executed checks | 6,475 (1.2.0) | **5,812** (this run). Most of the difference is likely the environment, not the suite: this container runs as root (one test SKIPs), has no `unleashed_directory` checkout beside the repo (`test_announce_directory` SKIPs), and has no fetched camsat (`test_sats` SKIPs). |

The full run: `sh tools/harness.sh --jobs 6 --tag audit --backup`, 4 cores,
fast clock x4. **ALL PASS, 5,812 passed, 0 failed, 46 lanes, 27.4 min of
wall clock, 9,453 s of lanes.** mbedTLS 3.6.0 was fetched into the
scratchpad because the cloud has no PlatformIO package, and
`MBEDTLS_DIR` pointed at it. `tools/harness.sh` is not executable in a
fresh clone (committed from Windows), so it has to be run as
`sh tools/harness.sh`.

### 1.2 Where the 5,812 come from

| Source | Executed checks | Lane time |
|---|---|---|
| Reference board, with a card | 2,197 | |
| Reference board, without a card | 1,790 | |
| `--fresh` boards (4 tests) | 84 | |
| SSH profiles: the 11 SSH tests | 1,239 (177 per profile, identical on all 7) | |
| Board profiles: the `test_board_*` and panel tests | 502 | |
| **Total** | **5,812** from 2,661 sites | 9,453 s |

The multiplication, and what each copy buys:

- **Card and no card: every test runs in both modes.** 150 tests run the
  same number of checks in both. Of those, 31 do mention the card in their
  body (a mail limit of 3 against 12, a card badge, a seeded screen), so
  their second run can differ. **The other 119 never mention it: 1,300
  checks and 2,390 s run twice to the same end.** The 21 tests that only
  mean anything with a card (files, forums, photos, backups on the card)
  already SKIP without one.
- **SSH x7: 1,062 checks and 1,254 s on the six profiles after the S3**,
  every count identical (section 4).
- Board profiles (502) are what the board axis is for and are not
  duplication.

### 1.3 Checks per test, the worst 20

Executed on the reference board, both modes added together. Static site
counts are in the appendix; a loop makes the two differ.

| Executed | Test | Card / no card |
|---|---|---|
| 120 | forums_page_fit | 60 / 60 |
| 90 | backup | 45 / 45 |
| 76 | lights_frames | 38 / 38 |
| 70 | accounts | 35 / 35 |
| 68 | lights_count | 34 / 34 |
| 68 | files | 67 / 1 |
| 64 | operator | 32 / 32 |
| 64 | announce_reliable | 32 / 32 |
| 62 | sysop | 31 / 31 |
| 62 | doors | 31 / 31 |
| 62 | boot_hold | 31 / 31 |
| 58 | backup_card | 56 / 2 |
| 52 | config_silent | 26 / 26 |
| 52 | announce_badges | 27 / 25 |
| 50 | lights_manual | 25 / 25 |
| 50 | ansi | 25 / 25 |
| 47 | forums | 47 / 0 |
| 46 | sysop_account | 23 / 23 |
| 46 | config_lights | 23 / 23 |
| 42 | sysop_burst_wrong | 21 / 21 |

`test_forums_page_fit` heads the list without being big: it loops one
assertion over 11 to 15 body lengths at two widths, and that is the
textbook unit test (section 7). `test_lights_frames`, `_count` and
`_manual` are 170 executed checks of colour arithmetic through a telnet
session.

### 1.4 The 20 slowest

Slowest lane each, in seconds, from the run's `TIME` lines:

| s | Test | Why it is slow |
|---|---|---|
| 276.0 | announce_reliable | a compressed day of heartbeats, real milliseconds (REALTIME) |
| 126.4 | announce_join_refused | waits out the directory's real 30 s, twice (REALTIME) |
| 118.4 | lights_frames | samples animation at real moments (REALTIME) |
| 100.7 | board_ws43b | panel holds read in real seconds (REALTIME) |
| 92.8 | lights_silent | |
| 91.5 | board_s3_silent | |
| 80.5 | lights_count | every effect at 1..16 pixels through LIGHTS |
| 76.1 | board_s3 | |
| 71.8 | panel_photo_show | |
| 67.7 | operator_ends | |
| 64.9 | announce_join_prompt | 31 real seconds (REALTIME) |
| 64.6 | board_s3_skin | |
| 64.4 | announce_join_in_flight | (REALTIME) |
| 61.9 | config_areas | |
| 59.7 | sysop_account | |
| 55.2 | operator | |
| 53.9 | files | |
| 50.7 | backups_area | |
| 47.3 | board_ws2 | |
| 43.8 | accounts | |

The longest lane is 521 s. With the repetition gone, `--jobs 6` on 4
cores is bounded by `test_announce_reliable` at 276 s, which earns its
time (section 6).

## 2. Classification of all 214

Six readers each took 36 tests and judged every one against the source it
exercises. The full table is Appendix A. The tiers:

| Tier | Tests | Wiring checks (est.) | Logic checks (est.) | Meaning |
|---|---|---|---|---|
| wiring | 56 | 420 | 118 | proves a caller reaches it: already the right shape |
| mixed | 69 | 552 | 626 | wiring plus a pile of edge cases |
| logic | 32 | 65 | 198 | mostly edge cases of one function, through a telnet session |
| env | 36 | 145 | 104 | makes the host behave like the board, or plays somebody else's software |
| profile | 21 | 218 | 100 | a board's own facts |
| **total** | **214** | **1,400** | **1,146** | |

The check figures are estimates read from the code, loops counted once,
and only roughly sum to the 2,661 sites. They size the job; they do not
define it.

Where the logic of the 101 logic and mixed tests lives today:

| Purity | Tests | What phase 2 can do |
|---|---|---|
| pure, already unit-tested | 10 | delete the integration copies of what the unit test already asserts, keep the wiring check |
| pure, no unit test | 16 | write the unit test now, no `src/` change, then cut |
| needs a `src/` split | 74 | nothing until the split is made and built on the board (section 7) |
| n/a | 1 | `test_privacy`: copy checks, which want a lint over the generated screens, not a unit test |

So **about a quarter of the logic is movable in phase 2 without touching
`src/`.** The rest is a firmware job, and should be scheduled as one, the
way `forums_ptr.h` and `panel_photo_fit.h` were.

## 3. Catch or coverage (rule A)

### Method

Two experiments, both in scratch worktrees in the scratchpad, never this
checkout:

- **Replay:** the test as it stood at its fix commit `C`, run against `C^`'s
  `src/` and `host/` (the bug present) and against `C`'s own (the fix). Fails
  before and passes after: a catch. This works only where the fix and the test
  landed in a commit small enough that `C^` already had the feature; a test
  born with its feature fails on `C^` for having nothing to call, which proves
  nothing.
- **Mutation:** `HEAD` with the bug put back by a one-line edit (the edits
  are in the scratchpad as `mut/*.py`), and both the candidate for deletion
  and the test that would survive it run against it. This is the honest test
  of "is A redundant given B": it asks whether B alone still fails.

Every run was also made against the unmutated tree; all passed there.

### Results

| Experiment | Test (checks) | Bug present | Fixed | Verdict |
|---|---|---|---|---|
| replay `5b9f25b` (1.1.2-dev.5, F2) | room_narrow_whole_line | 5 of 6 fail | 6/0 | **catch** |
| replay `5b9f25b` (F3) | room_private_own_tag | 4 of 6 fail | 6/0 | **catch** |
| replay `5b9f25b` (F4) | last_node_ten | 2 of 5 fail | 5/0 | **catch** |
| replay `5b9f25b` | room_narrow_effects | 2/0 (passes) | 2/0 | not its bug: see the mutation |
| mutation `room_margin` (the room renders a line as one row cut to its believed margin, no wrap of its own: the 0.22.0 bug class) | room_narrow_effects | **passes 2/0** | | **coverage only: cut** |
| same | room_narrow_whole_line | 4 of 6 fail | | catch: it survives |
| same | room_private_own_tag | 2 of 6 fail | | catch |
| same | chat (14), room_commands (19) | pass | | not their bug, as expected |
| replay `2f57650` (0.17.11, mail never replaced) | mail_never_lost | 4 of 11 fail | 23/0 | **catch** |
| replay `d926cc7` (0.19.1, a stopped listing hands you back) | list_abort_returns | 1 of 4 fails | 4/0 | **catch** |
| replay `fe100e9` (1.2.1-calls.2, µ at the cut) | signs_name_board | 1 of 4 fails | 4/0 | **catch** |
| same | calls_one_screen, welcome_connecting | pass | | not their bug |
| replay `ae1106e` (1.2.1-forums.2) | forums_header_rebuild | 4 of 6 fail | 6/0 | **catch** |
| same | forums_seg_range | 4 of 6 fail | 6/0 | **catch** |
| same | forums_segments (13), forums_remove (10) | pass | | not their bug |
| replay `42d02e3` (1.1.1-dev.3, bad TZ strings) | config_tz_bad and config_timezone | 5 of 18 fail, all tz_bad's | 18/0 | **catch**, and see the next row |
| mutation `tz_valid` (`tzones::valid` accepts anything) | host `test_tzones` alone | 3 fail | | **the unit test alone catches it** |
| same | config_tz_bad | 5 of 6 fail | | its per-string refusals are coverage of `test_tzones`; one is the wiring proof |
| same | config_timezone (12) | pass | | not its bug |
| replay `c1c225d` (0.21.9, levels seeded by position) | config_forum_levels | 4 of 55 fail, all its | 55/0 | **catch** |
| replay `4714620` (0.20.0, CONFIG files dropped two levels) | config_area_keeps_every_part | 3 of 9 fail | 46/0 | **catch** |
| mutation `forums_noread` (nothing is ever marked read) | forums: "the subject that was read is no longer marked new" | **fails** | | **catch**, though reading called it vacuous |
| same | forums: "the forum list agrees with the subjects underneath it" | **fails** | | **catch**, though reading called it vacuous |
| mutation `forums_allread` (everything counts as read) | forums: "and the other subject still reports what is left in IT" | **passes** | | **vacuous, confirmed**: `b"new" in after` is satisfied by the title bar |

What the table says, beyond the rows:

- **13 of the 15 tests or checks tried catch their own bug.** The suite is
  not padded with checks that never mattered; it is padded with
  repetition and with logic exercised the expensive way.
- **A reader's "this is vacuous" was wrong two times in three** on the
  one test where it could be put to the proof. That is why no check is
  cut in this audit on reading alone, and why Appendix B's notes are
  marked unverified.
- **An overlap is not a redundancy.** `test_room_narrow_effects` and
  `test_room_narrow_whole_line` read as the same path, and the replay of
  F2 shows the first passing over the second's bug. Only the mutation of
  the first's own bug class settles it, and it settles it against the
  first. The likely reason: its 40-column reader is a plain ASCII caller,
  and plain ASCII is laid out 80 wide, so the line never crosses a margin.
  Phase 2 can confirm that by printing the reader's width before cutting.
- **Moving a check is safe only if the unit test fails on the same
  mutation**, as `test_tzones` did for `tz_valid`. Phase 2 should run
  that mutation pair for every move, and the replays above are the ones
  that can be reused: `ae1106e` for `parseHead`, `42d02e3` for the TZ
  strings.

Not tried, and why. The 1.1.0 dash lane (`52b5b33`), the lights lane
(`bf0518f`, `111f410`) and the forms lane (`d1b772c`) each added a feature
and its tests in one commit, so a replay proves nothing, and their bugs are
layout arithmetic that wants the `src/` split first. Their mutations belong
with the split in phase 2, because the unit test they would be measured
against does not exist yet.

## 4. SSH on seven profiles

`PROFILE_TESTS` lists the same 11 SSH tests for `s3`, `ws43b`, `ws2`,
`wseth`, `mf35`, `mf35v2` and `g4848`, and `PROFILE_CARD` runs each with
and without a card: 154 runs of code gated by one define.

**What could differ per profile, checked against the source rather than
assumed:**

- `BBS_SSH_MAX` is 8 in every one of the seven `board.h` blocks.
- `BBS_SSH_PSRAM_EACH` (48 KB) and `BBS_SSH_PSRAM_KEEP` (128 KB) are
  defined once, in `config.h`, for every board.
- `sshd::cap()` is `min(BBS_SSH_MAX, used + (psram_free - KEEP) / EACH)`.
  On the host, PSRAM is `BBS_HOST_PSRAM`, 8 MB by default and 1.7 MB on
  `mf35` (harness.sh). Both give a cap of 8. `test_ssh_full` sets its own
  figure for one session, so the profile's figure does not reach it.
- `test_ssh_socket_budget` brings its own 16- and 12-socket tables through
  `LD_PRELOAD`, whatever the profile.
- The SSH code paths (`sshd.cpp`, `bbs_ssh.cpp`, `sshlink.h`) have no
  `BBS_HAS_LCD`, `BBS_HAS_ETH` or board conditional inside them.

**What the run shows:** all 11 tests, all 7 profiles, identical check
counts, 177 per profile, 0 failures.

**History:** the six later profiles joined the list at `db3165b`, a review
saying they "carry BBS_HAS_SSH and had no SSH test on their own profile".
That was completeness, not a finding. No commit in the history records an
SSH test failing on one profile and passing on the S3. The two per-profile
SSH bugs this project has had (`<unistd.h>` on a UART-console board at
`166ecae`, and the VFS table full on the MF35) were target compile and
target runtime failures that the host cannot reproduce on any profile.

**Which genuinely need a profile:** none of the 11, on the host. The
brief expected `full`, `socket_budget` and the PSRAM ceiling to need one;
the source says the ceiling is the same constant on every SSH board, and
the two tests set their own environment anyway. If a board ever gets its
own `BBS_SSH_MAX` or PSRAM budget, `sshd::cap` is a five-line pure
function that a unit test can sweep across every board's figures in
milliseconds. That is better than a telnet run per board (section 7).

**Proposal:** the 11 SSH tests run on the S3 lane only, with and without
a card. That matches Rob's 2026-10-04 axes decision ("the feature axis
runs on the reference build, and on the S3 build only for features that
are S3-only"). On the other six profiles, nothing: the board axis's own
`test_board_*` already proves each profile boots, and a profile whose SSH
setup moves (its own block in `board.h`) runs the S3 SSH set on itself
under the axes doc's "that board's own code changed" rule. Saving: 1,062
checks, 1,254 s.

**Rule A for this cut:** there is no fix commit to replay, because the
copies were never added for a bug. The evidence that they cannot catch
anything the S3 copy does not is the source above: same constants, same
code, no board conditional. Phase 2 should keep one guard: a
`static_assert`-style check that all SSH boards share `BBS_SSH_MAX`, or a
unit sweep of `cap()`. Then a board that diverges brings its test back
with it.

## 5. Found on the way, out of scope

### 5.1 Harness holes (fix before any cut, they are worse than any redundancy)

- **Five camera tests never run under `harness.sh --jobs`, the parallel
  runner:** `test_camera`, `test_camera_registry`,
  `test_camera_failed_start`, `test_camera_silent` and
  `test_camera_one_at_a_time`. Each SKIPs unless `HOST_BOARD` is in
  `CAM_BOARD` (testclient.py ~9268 and on), and `PROFILE_TESTS` gives
  `fncam` and `espcam` only their `test_board_*`, so no lane carries them.
  Measured: 0 checks executed in all 46 lanes. Only a hand-typed
  `--board fncam --card --only=camera` reaches them. `test_camera_failed_start`
  is an env test (section 6), so this is a hole in the part of the suite
  most worth having. **Fix: add them to `PROFILE_TESTS["fncam"]`** (and put
  `fncam` in `PROFILE_CARD`), a one-line table change.
- `test_boot_hold_factory_fails` always SKIPs as root (`geteuid() == 0`),
  and the cloud runs as root. An `LD_PRELOAD` unlink or rmdir that fails
  would let it run anywhere.
- `test_announce_directory` SKIPs unless the directory repo sits at
  `../unleashed_directory`; nothing says so outside the SKIP line.
- `test_restore_ends_screens` was written for esp_littlefs refusing to
  rename over an open file (EBUSY). Its own docstring says the host renames
  anyway, so the bug is not reproduced; only the notice is checked. An
  `LD_PRELOAD` rename that returns EBUSY on an open destination would make
  it an env test (as `test_rewrites_keep_old` does for FatFs's EEXIST).
- Positive controls missing on the env tests that prove an absence:
  `test_dash_opens_nothing` (no check that the fopen shim logged anything,
  so a shim that failed to load passes), `test_config_one_pass` and the
  `test_lag_*` open counts (no check that the hostio open log switched on).
  Each wants one check that the instrument saw the login's own opens.

### 5.2 Checks that cannot fail (verified against the code)

| Test | Check | Why it cannot fail |
|---|---|---|
| telnet_first | "NAWS size applied" | `check(..., True)` |
| menus | "a plain caller is not offered the staff menu" | `b"? staff" not in main`: the footer grammar prints `STAFF` in brackets, never "? staff", for anybody |
| shutdown | "the board is still answering after a cancel" | `after.sock_alive() if hasattr(after, "sock_alive") else True`: `Caller` has no `sock_alive`, so it is `True` |
| boot_notices | "every line inside 40 columns" | measures the test's own `want` literals, not what the board drew (the equality checks before it already pin the lines) |
| forums | "and the other subject still reports what is left in IT" | `b"new" in after` passes when nothing is new (mutation `forums_allread`) |

**The fix for each is to make it able to fail, not to delete it**, because
each was written for something real. The other "possibly vacuous" notes in
Appendix B are from reading, and the `test_forums` experiment shows how
often reading is wrong. Phase 2 should verify each one by mutation before
acting on it.

### 5.3 A test asserting a bug as correct

**None found** in the current tree, by six readers. The nearest are stale
docstrings: `test_mail` still says "replaced rather than doubled" (the
0.17.11 bug its body now refutes), `test_mail_rsd` describes the temp-file
write that 1.1.2 replaced, and `test_dash_card_age` says "a minute old". Fix
the words, so the next reader does not take them as the specification.

### 5.4 Order and shared-state hazards seen while reading

These are not verified by experiment; they are listed so phase 2 does not
trip over them while splitting the file.

- `test_config_area_keeps_every_part` renames area 5 to "Drop Zone" and
  never renames it back. `test_xfer`, `test_ymodem`, `test_dash_uploads` and
  `test_upload_no_binary` open it as "Drop Box". Only ORDER_NAMES keeps them
  apart.
- `test_config_lights_ascii` leaves `[plugin:sd] read = sysop` on the shared
  board.
- Several tests elevate with a literal `bye testsysop` instead of
  `PASSWORD`: `test_room_commands`, `test_sysinfo`, `test_refresh_and_ctrl_l`,
  `test_closed_configured` and `test_announce`.
- CONFIG rows reached by counting DOWN presses, which is the shape that broke
  `test_board_mf35v2` in dev.12: the closed and setup tests,
  `test_config_chat_colours`, `_announce_desc`, `_warn_levels`,
  `_forums_grow` and `test_forms_narrow`. `cfg_walk_to` exists for this.
- `test_card_screens_manifest` hard-codes the FNV of the 0.18.0
  `welcome.asc`, rebuilt from today's stock file. The 1.2.2 screen redraw
  will change that file, and the pre-manifest block then skips quietly
  behind `if old:`.
- `test_lag_files` and `test_lag_forums` time real microseconds and are not
  in REALTIME.

## 6. The environment set: keep, and label

These are the tests the brief calls "worth more than the rest combined". Each
reproduces something the host does not do by itself, or plays somebody
else's software. **None of them is a candidate for any cut.** Each should
carry a one-line comment in phase 2 naming the difference it reproduces, and
the split file should keep them together, so nobody "simplifies" one into a
logic test.

| Test | What it makes the host do |
|---|---|
| restore_cross_partition | userdata on /dev/shm: a rename between partitions fails with EXDEV, as between two ESP-IDF VFS mounts (the 0.14.0 to 1.0.2 account wipe) |
| screens_install | the card's screens on /dev/shm, the same EXDEV |
| rewrites_keep_old | `LD_PRELOAD` rename: EEXIST on an existing destination as FatFs does, and EIO |
| boot_hold_write_fails | `LD_PRELOAD` rename: EIO for system.cfg between the temp file and the live one |
| boot_hold_factory_fails | a folder the erase cannot empty (SKIPs as root: 5.1) |
| ssh_socket_budget | `LD_PRELOAD` table of lwIP's 16 sockets, and 12 to force exhaustion |
| dash_opens_nothing | `LD_PRELOAD` fopen counter: the host pays nothing per open, LittleFS and FAT do (needs a positive control: 5.1) |
| lag_screens, lag_files, lag_forums, lag_logins, lag_login_calls, lag_logoff_calls, lag_last_calls, lag_announce_calls, lag_backup_get, forums_long_read | hostio.txt gives every open the board's cost, so a slow pass shows (Rule no. 1) |
| forums_segments, mail_in_place, config_one_pass | hostio.txt's open log counts what the board would pay for; forums_segments also reads the result with `tools/forum_check.py`, which shares no code with the board |
| camera_failed_start | `BBS_CAM_FAIL=2` holds the DMA block as a partial `esp_camera_init` does (never runs today: 5.1) |
| telnet_first | PuTTY: IAC WILL NAWS before the server speaks |
| upload_no_binary | a telnet client that refuses BINARY and pads every bare CR with NUL for ever |
| sysop_burst, sysop_burst_wrong | SyncTERM's Alt+L autologin, three lines in one send |
| announce_reliable | no DNS, a directory that goes down, one that never answers, cut and garbage replies, fd and RSS over a compressed day |
| announce_join_prompt, _join_in_flight, _join_refused | the directory's real 30 s limit and the board's 10 s timeout, on the wall clock |
| announce_directory | the real `unleashed_directory` server, a contract against another implementation |
| ssh_login, ssh_new_caller, ssh_ymodem | a real wolfSSH client (`host/ssh_call`) |
| ssh_host_keys | clients that accept one host-key type only, as cryptlib SyncTERM and SyncTERM 1.10 do |
| ssh_dedicated_port | a banner-first client, as cryptlib and SyncTERM 1.9 are |
| ssh_full | reads the refusal as a raw SSH client would: identification, one DISCONNECT, no KEXINIT |

Inside otherwise ordinary tests, three pieces are env too and must survive a
split. `test_accounts`' `AttrScreen` models a terminal that ignores SGR 27:
the orange-block bleed was invisible to every model that honoured it.
`test_doors` sends Ctrl-C as telnet's Interrupt Process. `test_xfer` drives
a deliberately dumb XMODEM client rather than a library.

**Not in the suite and worth naming:** `tools/lrzsz_check.py` (lrzsz's `sz`,
including the binary-refusing client) is the one test the board cannot talk
itself into passing, and it is manual because it needs `lrzsz` installed. The
stubborn-CRC sender that found the SyncTERM upload bug is not a test at all.
Phase 2 should make both part of the regression tier, installing lrzsz where
it can.

## 7. What `src/` would need, for the logic to move

Reported, not made: each is a firmware change and needs a board build to
verify. Grouped by the payoff in checks:

| Split | Unlocks (tests) | Est. logic checks |
|---|---|---|
| `lights.cpp` effect frames, `shade()`, `drawStrip`, `countKey`, the word keys into a `lights_fx.h` | lights_frames, lights_manual, lights_count, lights_wifi, lights_order | about 70 |
| the DASH page builder (DashSnap to rows) and the NODES/WHO/LAST row builders out of `bbs_shell.cpp` | dash_frame, dash_narrow, dash_wide, dash_petscii, nodes_columns, last_node_ten | about 30 |
| the CONFIG save rules in `bbs_sysop.cpp`: composite seeding and packing, per-value character rules, the warn threshold, pin holders, the network block | config_area_keeps_every_part, config_forum_levels, config_semicolon, config_warn_levels, config_pin_holders, config_wifi_live, config_forums_grow, config_lights | about 60 |
| form geometry by width (`Form::labelWidth`, `boxCol`, `lineWidth` exist; the draw does not) | forms_wide, forms_narrow, whois_wide | about 20 |
| the forum reader's row and page-fit machine | forums_page_fit (120 executed) | 5 sites, looped |
| chat: the ring line format, private prefix and tag, token bucket, mail slot choice and R/S/D flags | room_narrow_whole_line, room_private_own_tag, chat, mail_never_lost, mail_rsd | about 25 |
| the ring (operator) state machine in `bbs_ring.cpp` | operator, operator_ends | about 15 |
| `Bbs::closedAdmits` and `sysopAccount`'s resolution order | closed_fresh, closed_configured, sysop_account | about 45 |
| the backup name format, card listing and nightly retention | backup_card, backup_card_nightly | about 45 |
| `sshd::cap` into a header | ssh_full, and the per-board guard in section 4 | small, but it is the one that lets the SSH profile copies go with confidence |

**Already pure, no `src/` change needed, just a unit test:** `syscfg::trial`,
`pinProblem`, `crossCheck`, `redactLine`, `unredactLine` and the `#` rule
(`sysconfig.cpp` is already linked standalone by `host/test_photos_cfg`);
`guard.h`'s `BanList` (with `aheadTake`, most of `test_sysop_burst_wrong`'s 21
checks) and `LoginGuard`, given a `plat::millis` stub; `guard.cpp::localNet`;
`users::validHandle` and `validEmail`; `helptext::find`; `files.cpp::notUpload`;
the `Telnet` input class. The file-local helpers `forums.cpp::parseHead`
(header rebuild and segment range, both confirmed catches above) and
`chat.cpp::idOfTag` (squelch 10) are pure too, but need moving into a header.
That is the smallest kind of `src/` change, and the phase 2 session should
flag it rather than make it.

## 8. The proposal

Applied in phase 2, in this order, each step measured before and after.

### Step 1: stop running things twice (no test changes, no rule-A risk)

1. **SSH on the S3 lane only** (section 4): about 1,062 checks and 1,254 s
   off, with a unit guard on `cap()` or a shared-constant assertion.
2. **One card mode for the 119 tests that never mention the card**
   (Appendix A marks them "one card mode"): about 1,300 checks and
   2,390 s off. That is a table, `CARD_BLIND` beside `ALONE` and `NEEDS`,
   which `tools/parallel.py` reads. Phase 2 should confirm each by running
   the pair once and diffing the two outputs line by line, not only the
   counts. The 31 tests that do mention the card stay in both modes.
3. **Close the camera hole** (5.1). It adds checks, which is the point.

After step 1: about 3,450 executed checks on the same 2,661 sites, roughly
5,800 s of lanes, and nothing lost.

### Step 2: cut what was shown not to catch

- `test_room_narrow_effects`: cut. Mutation `room_margin` passes it while
  `test_room_narrow_whole_line` fails. Before cutting, confirm its reader's
  width, as section 3 says.
- `test_config_tz_bad`: keep one refusal as the wiring check ("CONFIG refuses
  a TZ string `tzones::valid` refuses"), "and nothing written", and the two
  boot-time checks (a bad line in the file is dropped, the rest is read),
  which are the parser's wiring. Cut the other per-string refusals: every string
  is already in `host/test_tzones.cpp`'s refusal list, and under mutation
  `tz_valid` the unit test fails (3) wherever the integration test does (5).
- The five checks in 5.2: make each able to fail. None is deleted.

### Step 3: move the logic that is already pure

The 16 "pure, no unit test" tests and the 10 "already unit-tested" ones
(Appendix A, purity column). Each one goes in four steps:

1. Write the unit test.
2. Run the same mutation against both the unit test and the integration
   check.
3. If the unit test fails where the integration check did, delete the
   integration check.
4. Keep one integration check per behaviour, the one Appendix A's readers
   named as the wiring proof.

### Step 4: split the file

By the existing `--only=` groups, kept as wide as they are (rule D), with
`ORDER_NAMES` still the one order. Proof of no change: `--plan` before and
after must print the same selection for the full run and for every group.
Split only after the 1.2.2 tag, as the brief says.

### Step 5: name the tiers

- `make test` is the **unit** tier.
- The **integration** tier is `harness.sh --jobs N` after steps 1 to 3,
  reference build, one card mode where blind.
- The **regression** tier is that plus both card modes for everything, the
  board and chip axes, the fresh boards and `lrzsz_check.py`. It runs at an
  x.Y.0 and on request.

This sits on the axes of `internal/test-reorg-2026-10-04.md` rather than
replacing them. Tiers say how much of a test runs; axes say on which build.

### What I would not do

- Cut any check because it reads redundant. Section 3's `test_forums` rows
  are the reason.
- Narrow a group. The F2 replay is the reason: the bug was in the room's
  storage, and it was the private-tag test next door that also caught it.
- Collapse the env set into "logic" because its assertions look ordinary.
  `test_restore_cross_partition` reads like a restore test, and it is the
  only thing that stands between this board and the 1.0.2 account wipe.

## 9. Reproducing this

- `tools/testclient.py` scanned with Python's `ast` for every `def test_`,
  its `check(` calls and the helpers that call `check`. The origin of each
  test is the first commit whose diff adds `def test_<name>(`, with the
  history unshallowed. Two tests came in through merges and show no origin.
- The full run: `MBEDTLS_DIR=<mbedTLS 3.6.0> sh tools/harness.sh --jobs 6
  --tag audit --backup`. Per-test counts come from each lane's
  `/tmp/bbs-audit-*/out.txt`, attributing `PASS` lines to the preceding
  `TEST` line.
- Replays: `git worktree add` at `C`, then `git checkout C^ -- src host`
  for the "before" side, and `sh tools/harness.sh --only=...` with the
  harness of that time. Before `--tests` and comma lists existed, one name
  went per run.
- Mutations: a worktree at `HEAD`, a one-line edit, then `--tests=`.

---

## Appendix A: every test

**Executed ref** is checks executed on the reference build with a card and
without one, in this run. **Elsewhere** is profile lanes (both modes added)
and the fresh boards. **Wiring/logic** is the readers' estimate of check
sites. **Purity** is whether the logic it tests is already a pure, unit-tested
function. **Proposal** is section 8's, per test. Tests are in file order.

| Test | Added | Tier | Executed ref (card/no) | Elsewhere | Wiring/logic (est.) | Logic lives in | Purity | Proposal |
|---|---|---|---|---|---|---|---|---|
| ansi | 111824f 09-16 | wiring | 25/25 |  | 21/4 | core/users.cpp::validHandle; core/bbs_shell.cpp::cmdWho (1..30 refresh | pure-untested | KEEP; one card mode |
| link_line | e7e510e 09-25 | wiring | 7/7 |  | 6/1 | core/bbs.cpp connection-line choice (long vs <47-col short form) | needs-split | KEEP; one card mode |
| telnet_first | 111824f 09-16 | env | 4/4 |  | 3/0 | core/detect.cpp Detector (IAC-first skips the settle); core/telnet.cpp | pure-untested | KEEP, label env; one card mode |
| petscii | 111824f 09-16 | wiring | 18/18 |  | 14/4 | core/detect.cpp (2 s key-prompt timeout, no IAC to PETSCII); core/term | pure-untested | KEEP; one card mode |
| ascii | 111824f 09-16 | mixed | 10/10 |  | 5/5 | core/bbs_shell.cpp::helpUsage and the HELP row layout (kUsageCol 15, r | needs-split | TRIM: keep wiring, move logic |
| page | 111824f 09-16 | wiring | 10/10 |  | 9/1 | core/bbs_shell.cpp::cmdPage, DND, bus notices | n/a | KEEP; one card mode |
| sysop | 111824f 09-16 | wiring | 31/31 |  | 26/5 | core/bbs_shell.cpp WHO/NODES row visibility and rank markers; Bbs::cmd | needs-split | KEEP; one card mode |
| cosysop | 111824f 09-16 | mixed | 18/18 |  | 6/12 | [access] matrix lookup (core/sysconfig.cpp access + PERM_*), Bbs::mayM | needs-split | TRIM: keep wiring, move logic; one card mode |
| accounts | 8b3096b 09-17 | mixed | 35/35 |  | 13/22 | core/users.cpp::validEmail/validHandle/setPassword/checkPassword; core | pure-untested | TRIM: keep wiring, move logic; one card mode |
| user_admin | 8b3096b 09-17 | mixed | 16/16 |  | 9/7 | core/bbs_users.cpp USER EDIT/DEL rules; users::retire/lookup | needs-split | TRIM: keep wiring, move logic; one card mode |
| guest | 63c887c 09-17 | mixed | 17/17 |  | 7/10 | core/bbs.cpp handle-prompt decision (reserved SYSOP, online hold, R/G  | needs-split | TRIM: keep wiring, move logic; one card mode |
| plugins | c20979b 09-17 | wiring | 16/16 |  | 14/2 | core/plugin.cpp level check (CF_READ/CF_WRITE/CF_ADMIN vs plugin level | needs-split | KEEP; one card mode |
| about | c20979b 09-17 | wiring | 2/2 |  | 2/0 |  | n/a | KEEP; one card mode |
| radio_link | e9b0a05 09-26 | wiring | 13/13 |  | 12/1 | core/link.cpp ulink::Engine pairing (commit-then-reveal, codes) | pure-tested | KEEP; one card mode |
| doors | e9b0a05 09-26 | mixed | 31/31 |  | 16/15 | plugins/doors.cpp leave-key counter (kLeaveKey 0x03 x3 within kLeaveMs | needs-split | TRIM: keep wiring, move logic; one card mode |
| link_shared | b5aeb1c 09-27 | wiring | 19/19 |  | 14/5 | core/link.cpp multiboard share/revoke and channel refusal | pure-tested | KEEP; one card mode |
| sats | 8654272 09-27 | mixed | 0/0 |  | 20/27 | plugins/link.cpp SATS row content per viewer (linkp::), link.cpp::uniq | needs-split | TRIM: keep wiring, move logic |
| doors_petscii | b5aeb1c 09-27 | mixed | 10/10 |  | 6/4 | src/core/satwords.h line lengths; doors.cpp leave-key counter | pure-untested | TRIM: keep wiring, move logic; one card mode |
| version_shown | c76b7e5 09-24 | wiring | 7/7 |  | 5/2 | core/bbs_hardware.cpp system badge / plugins/announce.cpp payload vers | needs-split | KEEP; one card mode |
| chat | 2c4e71d 09-17 | mixed | 14/14 |  | 8/6 | plugins/chat.cpp token bucket (g_tokens, kBurst, g_rate) and held-line | needs-split | TRIM: keep wiring, move logic; one card mode |
| serial | 2c4e71d 09-17 | wiring | 10/10 |  | 8/2 | plugins/serialbridge.cpp operator/watcher rules | n/a | KEEP; one card mode |
| idle_login | 111824f 09-16 | wiring | 2/2 |  | 2/1 | core/bbs.cpp idle timer at the handle prompt | n/a | KEEP; one card mode |
| busy | 111824f 09-16 | wiring | 12/12 |  | 10/2 | core/bbs.cpp startBusy (busy line, countdown, BUSY refusal) | n/a | KEEP; one card mode |
| motd | 144bc2d 09-22 | mixed | 6/6 |  | 3/3 | core Bbs pager (private Bbs::pageRows rule) and ScreenPlayer | needs-split | TRIM: keep wiring, move logic; one card mode |
| backup | 55f40e0 09-17 | mixed | 45/45 |  | 18/27 | core/ziparc.cpp entry validation (validScreenName, path, size, ratio l | pure-untested | TRIM: keep wiring, move logic; one card mode |
| backup_published_default | e79a5d5 09-23 | mixed | 12/12 | fresh 19 | 9/16 | core/sysconfig.cpp::unredactLine (published-default rule); guard.h loc | pure-untested | TRIM: keep wiring, move logic; one card mode |
| backup_card | 0b3bb89 09-24 | mixed | 56/2 |  | 16/42 | core/bbs_backup.cpp card backup naming/listing/refusals; core/ziparc.c | needs-split | TRIM: keep wiring, move logic |
| backup_card_nightly | 0b3bb89 09-24 | mixed | 9/9 |  | 4/5 | core/bbs_backup.cpp / plugins/sd.cpp nightly retention (nightly-YYYYMM | needs-split | TRIM: keep wiring, move logic |
| restore_cross_partition | 0b3bb89 09-24 | env | 5/5 |  | 5/0 | core/ziparc.cpp restore apply (rename into place) | n/a | KEEP, label env; one card mode |
| config_timezone | 0b3bb89 09-24 | mixed | 12/12 |  | 6/6 | core/tzones.h nameFor/posixFor; CONFIG cycle-by-letter and Custom swit | pure-tested | TRIM: keep wiring, move logic; one card mode |
| config_tz_bad | 42d02e3 09-25 | logic | 6/6 |  | 2/3 | core/tzones.h valid(); core/sysconfig.cpp tz line handling | pure-tested | TRIM to 1 wiring check; strings are in test_tzones (mutation tz_valid) |
| config_cycle_numbers | 0b3bb89 09-24 | mixed | 9/9 |  | 4/5 | core/form.cpp / bbs_config line-mode cycle prompt (numbered choices, p | needs-split | TRIM: keep wiring, move logic; one card mode |
| ban | 111824f 09-16 | wiring | 1/1 |  | 1/0 | core/guard.cpp BanList | pure-untested | KEEP; one card mode |
| menus | b596bdd 09-18 | mixed | 9/9 |  | 4/5 | core/bbs_shell.cpp help menus (Command::menu/rank tables, shortcut hig | needs-split | TRIM: keep wiring, move logic; one card mode |
| sysinfo | b596bdd 09-18 | wiring | 12/12 |  | 10/2 | core/bbs_sysop.cpp SYS/CALLS gating | n/a | KEEP; one card mode |
| calls_one_screen | db727ac 10-01 | logic | 15/15 |  | 1/5 | core/bbs_shell.cpp::rowCalls/cmdCalls two-pane hour layout | needs-split | MOVE logic to unit, keep 1 wiring check; one card mode |
| ssh_signup_no_privacy_offer | fe100e9 10-01 | profile | 0/0 | g4848 16, mf35 16, mf35v2 16, s3 16, ws2 16, ws43b 16, wseth 16 | 4/4 | src/core/bbs.cpp::askKnowMore, Bbs::handleOnline (held-handle states) | needs-split | S3 lane only; KEEP on its board axis |
| signs_name_board | fe100e9 10-01 | logic | 5/5 |  | 2/2 | src/core/bbs.cpp::signName (static); src/core/sysconfig.cpp::copyText | needs-split | MOVE logic to unit, keep 1 wiring check; one card mode |
| hardware | 801888a 09-25 | mixed | 19/17 |  | 8/15 | src/core/bbs_hardware.cpp::hwRow / capability line | needs-split | TRIM: keep wiring, move logic |
| dash_frame | 52b5b33 09-24 | mixed | 15/15 |  | 4/10 | src/core/bbs_shell.cpp DASH page builder (DashSnap -> rows) | needs-split | TRIM: keep wiring, move logic |
| dash_pick | 52b5b33 09-24 | wiring | 5/5 |  | 4/1 | src/core/bbs_shell.cpp DASH pick bar | n/a | KEEP; one card mode |
| dash_narrow | 52b5b33 09-24 | logic | 8/8 |  | 2/6 | src/core/bbs_shell.cpp DASH 40-column pages | needs-split | MOVE logic to unit, keep 1 wiring check; one card mode |
| dash_all | 52b5b33 09-24 | wiring | 3/3 |  | 3/0 |  | n/a | KEEP; one card mode |
| dash_ascii | 52b5b33 09-24 | wiring | 4/4 |  | 3/1 | src/core/bbs_shell.cpp DASH footer for plain terminals | n/a | KEEP; one card mode |
| dash_petscii | 52b5b33 09-24 | mixed | 4/4 |  | 1/2 | src/core/bbs_shell.cpp DASH 40-column pages in PETSCII | needs-split | TRIM: keep wiring, move logic; one card mode |
| nodes_columns | 52b5b33 09-24 | logic | 5/5 |  | 1/4 | src/core/bbs_shell.cpp row builder shared by NODES/WHO/DASH | needs-split | MOVE logic to unit, keep 1 wiring check; one card mode |
| operator_notes | 52b5b33 09-24 | wiring | 7/7 |  | 5/2 | src/core/bbs_ring.cpp::ringNotes / ring delivery fallback | needs-split | KEEP |
| ring_mail | 52b5b33 09-24 | mixed | 14/14 |  | 9/5 | src/core/bbs_ring.cpp ring delivery; src/plugins/chat.cpp mail flag | needs-split | TRIM: keep wiring, move logic; one card mode |
| sysop_account | ce4d086 09-24 | mixed | 23/23 |  | 8/15 | src/core/bbs.cpp::Bbs::sysopAccount, isSysopAccount; bbs_sysop.cpp CON | needs-split | TRIM: keep wiring, move logic; one card mode |
| sysop_burst | 7210199 09-30 | env | 4/4 |  | 4/0 | src/core/bbs.cpp::askSysop | n/a | KEEP, label env; one card mode |
| sysop_burst_wrong | 7210199 09-30 | env | 21/21 |  | 6/15 | src/core/guard.cpp::BanList::aheadTake/aheadGive/windowFrom; bbs.cpp:: | pure-untested | KEEP, label env; one card mode |
| dash_waiting | 52b5b33 09-24 | wiring | 2/2 |  | 2/0 |  | n/a | KEEP; one card mode |
| dash_card_age | 52b5b33 09-24 | mixed | 3/0 |  | 1/2 | src/core/space.cpp kept free-space figures; sd plugin status line | needs-split | TRIM: keep wiring, move logic |
| dash_wide | 52b5b33 09-24 | logic | 4/4 |  | 1/3 | src/core/bbs_shell.cpp DASH 132-column page | needs-split | MOVE logic to unit, keep 1 wiring check; one card mode |
| dash_opens_nothing | 52b5b33 09-24 | env | 2/2 |  | 1/1 | src/core/bbs_shell.cpp DASH frame (no file I/O) | n/a | KEEP, label env; one card mode |
| boot_notices | 52b5b33 09-24 | mixed | 6/6 |  | 3/3 | src/core/bbs.cpp::Bbs::bootNotice | needs-split | TRIM: keep wiring, move logic; one card mode |
| room_new_commands | d2f6f41 09-22 | mixed | 15/15 |  | 9/6 | src/plugins/chat.cpp sticky private (liftInput/restoreInput, marker re | needs-split | TRIM: keep wiring, move logic; one card mode |
| room_private | e936a65 09-25 | mixed | 17/17 |  | 6/11 | src/plugins/chat.cpp::aboutUs, flush (held count), privateBegin; ring  | needs-split | TRIM: keep wiring, move logic; one card mode |
| room_commands | b596bdd 09-18 | wiring | 19/19 |  | 13/6 | src/plugins/chat.cpp room command parser (verb ends at digit), squelch | needs-split | KEEP; one card mode |
| mail | b596bdd 09-18 | wiring | 15/15 |  | 11/4 | src/plugins/chat.cpp mail store (mailSend/mailRewrite), mailbox list o | needs-split | KEEP; one card mode |
| config | b596bdd 09-18 | wiring | 13/13 |  | 9/4 | src/core/bbs_sysop.cpp configSave ranges; syscfg::write; plugin comman | needs-split | KEEP |
| config_parser_rules | a488bc6 09-23 | logic | 15/15 |  | 3/12 | src/core/sysconfig.cpp::syscfg::trial / parser rules (hostname, backup | pure-untested | MOVE logic to unit, keep 1 wiring check; one card mode |
| config_guards | a488bc6 09-23 | logic | 9/9 |  | 2/7 | src/core/sysconfig.cpp::syscfg::pinProblem; bbs_sysop.cpp empty sysop  | pure-untested | MOVE logic to unit, keep 1 wiring check |
| photos_config | 6bfc426 09-28 | mixed | 13/13 |  | 4/9 | src/core/sysconfig.cpp camera->photos_ fold; bbs_sysop.cpp CONFIG phot | needs-split | TRIM: keep wiring, move logic; one card mode |
| config_semicolon | a488bc6 09-23 | logic | 10/10 |  | 2/8 | src/core/bbs_sysop.cpp configSave value rules (';' in plugin values, ' | needs-split | MOVE logic to unit, keep 1 wiring check; one card mode |
| config_sd_plugin | a488bc6 09-23 | wiring | 1/2 |  | 2/1 | src/core/bbs_sysop.cpp save verdict for PF_SD plugins | n/a | KEEP |
| config_lights | bf0518f 09-23 | mixed | 23/23 |  | 8/15 | src/plugins/lights.cpp settings; syscfg::pinProblem; bbs_sysop.cpp pin | needs-split | TRIM: keep wiring, move logic |
| config_lights_ascii | bf0518f 09-23 | mixed | 7/7 |  | 2/5 | src/core/form.cpp line-mode FF_CYCLE input | needs-split | TRIM: keep wiring, move logic; one card mode |
| lights_frames | bf0518f 09-23 | logic | 38/38 |  | 4/29 | src/plugins/lights.cpp::shade and effect frame builders (rainbow, c64, | needs-split | MOVE logic to unit, keep 1 wiring check |
| lights_manual | bf0518f 09-23 | logic | 25/25 |  | 6/19 | src/plugins/lights.cpp manual-mode per-pixel effects and colours; ligh | needs-split | MOVE logic to unit, keep 1 wiring check; one card mode |
| config_silent | c32b250 09-25 | mixed | 26/26 |  | 8/18 | src/core/silent.h::rule (pure-tested); src/core/sysconfig.cpp HH:MM pa | pure-tested | TRIM: keep wiring, move logic; one card mode |
| lights_silent | c32b250 09-25 | wiring | 14/14 |  | 9/6 | src/plugins/lights.cpp silent gate; src/core/silent.cpp::silentTick | n/a | KEEP |
| lights_disk | e936a65 09-25 | wiring | 3/0 |  | 3/0 | src/plugins/lights.cpp::noteDisk; src/core/disk.h::open | n/a | KEEP |
| lights_count | 111f410 09-24 | logic | 34/34 |  | 6/17 | src/plugins/lights.cpp::drawStrip (every effect at 1..16 px), lights.c | needs-split | MOVE logic to unit, keep 1 wiring check; one card mode |
| lights_wifi | c76b7e5 09-24 | logic | 16/16 |  | 1/4 | src/plugins/lights.cpp::drawStrip (SF_WIFI meter: dBm -> lit count and | needs-split | MOVE logic to unit, keep 1 wiring check; one card mode |
| lights_order | 111f410 09-24 | mixed | 8/8 |  | 4/4 | src/plugins/lights.cpp::wordKey/readKey (drive_order, strip_order, any | needs-split | TRIM: keep wiring, move logic; one card mode |
| board_ws43b | a47f9d7 09-28 | profile | 0/0 | ws43b 57 | 23/6 | src/plugins/panel_gfx.h layout (light bar segment positions, columns), | pure-tested | KEEP on its board axis |
| board_fncam | f188b0b 09-24 | profile | 0/0 | fncam 23 | 6/4 | src/core/sysconfig.cpp::pinProblem (board.h BBS_PINS_* reasons), parse | pure-untested | KEEP on its board axis |
| board_espcam | e7e510e 09-25 | profile | 0/0 | espcam 5 | 3/2 | src/core/sysconfig.cpp::pinProblem / parser line drop, CONFIG pin-hold | pure-untested | KEEP on its board axis |
| board_ws2 | a785139 09-28 | profile | 0/0 | ws2 50 | 10/5 | src/core/sysconfig.cpp::pinProblem + CONFIG pinShares holder check (bb | pure-untested | KEEP on its board axis |
| panel_photo_show | f6c85b5 10-01 | profile | 0/0 | ws2 38 | 9/9 | src/plugins/panel_photo.h show state (1-minute show, restart on newer, | needs-split | KEEP on its board axis |
| board_wseth | 5ea8846 09-28 | profile | 0/0 | wseth 38 | 11/3 | src/core/sysconfig.cpp::pinProblem (W5500/camera reasons); main.cpp/pl | pure-untested | KEEP on its board axis |
| board_mf35 | aab65be 09-28 | profile | 0/0 | mf35 48 | 15/6 | src/plugins/panel_gfx.h big layout (rules at x308/y290/y196), panel.cp | pure-tested | KEEP on its board axis |
| board_mf35v2 | 47ec15f 09-30 | profile | 0/0 | mf35v2 22 | 5/2 | src/core/sysconfig.cpp::pinProblem (WIRED, panel data bus, USB, octal  | pure-untested | KEEP on its board axis |
| board_g4848 | 26c6454 10-01 | profile | 0/0 | g4848 52 | 17/5 | src/plugins/panel_gfx.h square layout (rules y290/y440, x240), panel.c | pure-tested | KEEP on its board axis |
| camera | 2660a71 09-24 | profile | 0/0 |  | 24/20 | src/plugins/camera_rules.h (callerName/systemName, Window check/record | pure-tested | FIX HARNESS: never runs under --jobs; KEEP on its board axis |
| board_s3_skin | d9ed540 09-29 | profile | 0/0 | s3 27 | 16/5 | src/plugins/skin_manifest.h (skin.txt parse errors, 'line 3: drive sty | pure-tested | KEEP on its board axis |
| board_s3_silent | c32b250 09-25 | profile | 0/0 | s3 28 | 11/3 | src/core/silent.h (hours edges), panel.cpp silent redraw/backlight owe | pure-tested | KEEP on its board axis |
| camera_registry | 229821d 09-26 | profile | 0/0 |  | 6/1 | src/core/cameras.cpp (registry lookup by number/name), camera_rules.h  | needs-split | FIX HARNESS: never runs under --jobs; KEEP on its board axis |
| camera_failed_start | 642311b 09-25 | env | 0/0 |  | 5/0 | src/plugins/camera.cpp failure path (camClose on a partial bring-up) | n/a | FIX HARNESS: never runs under --jobs; KEEP, label env |
| announce_camera | (merge)  | profile | 1/1 |  | 4/2 | src/plugins/announce.cpp features list (camera only while plugin runs  | needs-split | KEEP on its board axis |
| camera_silent | (merge)  | profile | 0/0 |  | 7/2 | src/plugins/camera.cpp flash drive gated by silent (silent.h), lights. | pure-tested | FIX HARNESS: never runs under --jobs; KEEP on its board axis |
| board_s3 | 111f410 09-24 | profile | 0/0 | s3 114 | 38/14 | src/plugins/panel.cpp readKey/legacy (orientation words, rotation=90 - | needs-split | KEEP on its board axis |
| config_wifi_live | a488bc6 09-23 | mixed | 19/19 |  | 6/11 | src/core/bbs_sysop.cpp configSave network block (mask untouched / rety | needs-split | TRIM: keep wiring, move logic; one card mode |
| config_network | 70cf1c6 09-23 | mixed | 18/18 |  | 12/5 | src/core/sysconfig.cpp port/backup_port crossCheck and range; bbs_syso | pure-untested | TRIM: keep wiring, move logic; one card mode |
| boot_hold | 05c7599 09-23 | mixed | 31/31 |  | 19/12 | src/core/recovery.h::BootHold (bands 6.9/8/16/21 s, LED stages); recov | pure-tested | TRIM: keep wiring, move logic |
| config_wifi_fallback | 05c7599 09-23 | mixed | 12/12 |  | 3/9 | src/core/recovery.cpp wifi trial decision (no record / same network /  | needs-split | TRIM: keep wiring, move logic; one card mode |
| cgnat_local | e7e510e 09-25 | mixed | 12/12 |  | 6/6 | src/core/guard.cpp::localNet / cgnatAddr | pure-untested | TRIM: keep wiring, move logic; one card mode |
| config_announce_outside | 70cf1c6 09-23 | wiring | 9/9 |  | 7/2 | src/plugins/announce.cpp payload port (outside port blank -> listening | needs-split | KEEP; one card mode |
| accounts_form_notes | 70cf1c6 09-23 | logic | 2/2 |  | 1/1 | src/core/bbs*.cpp Bbs::addField (FormField::note reset) | needs-split | MOVE logic to unit, keep 1 wiring check; one card mode |
| config_areas | 1595822 09-20 | wiring | 37/0 |  | 30/6 | src/core/bbs_sysop.cpp configSubSave/composite pack (bar-separated are | needs-split | KEEP |
| config_area_keeps_every_part | 4714620 09-21 | logic | 9/0 |  | 4/3 | src/core/bbs_sysop.cpp kComposites count vs kAreaParts / configSubSave | needs-split | MOVE logic to unit, keep 1 wiring check |
| config_forum_levels | c1c225d 09-22 | logic | 9/0 |  | 3/6 | src/core/bbs_sysop.cpp CfgPart fallback seeding (IN_PART/k, IN_ADMIN,  | needs-split | MOVE logic to unit, keep 1 wiring check |
| room_quit_logoff | c1c225d 09-22 | wiring | 6/6 |  | 4/2 | src/plugins/chat.cpp room help table (column width) | n/a | KEEP; one card mode |
| welcome_connecting | c1c225d 09-22 | wiring | 5/5 |  | 2/3 | screens @BOARD@ fallback (board_name -> hostname), welcome layout | needs-split | KEEP; one card mode |
| paced_chatin | c1c225d 09-22 | wiring | 1/0 |  | 1/0 | src/core Bbs::showScreen / ScreenPlayer::fullSpeed | n/a | KEEP |
| room_time_staff_only | e04f5ba 09-22 | wiring | 3/3 |  | 3/0 | src/core cmdTimeAdjust PERM_TIME check | n/a | KEEP; one card mode |
| bell | e04f5ba 09-22 | wiring | 7/7 |  | 7/0 | src/core bell setting (BELL) shared with chat.cpp /b | n/a | KEEP; one card mode |
| codes_in_messages | e04f5ba 09-22 | mixed | 12/9 |  | 6/6 | src/core/codes.h::plain,codes::row (allow-list: @CLS@ not a caller cod | pure-tested | TRIM: keep wiring, move logic |
| first_setup | 9d896eb 09-23 | wiring | 3/3 | fresh 17 | 12/6 | src/core/bbs_config.cpp (CONFIG staff save: published default refused, | needs-split | KEEP; one card mode |
| setup_abort | b5127f4 09-25 | wiring | 0/0 | fresh 6 | 4/2 | src/core/bbs.cpp::abortOutput (clears setupStage with pendingLand) | needs-split | KEEP |
| closed_fresh | b5127f4 09-25 | mixed | 0/0 | fresh 42 | 22/20 | src/core/bbs.cpp::closedTo, Bbs::closedAdmits, closedRefuse; bbs_confi | needs-split | TRIM: keep wiring, move logic |
| closed_configured | b5127f4 09-25 | mixed | 21/21 |  | 12/9 | src/core/bbs.cpp::closedTo, closedAdmits (sysop_handle by id), closedR | needs-split | TRIM: keep wiring, move logic; one card mode |
| fx_codes | 6a908e0 09-23 | wiring | 5/5 |  | 2/3 | src/core/bbs_shell.cpp::cmdFx demo step table (code labels, marquee na | needs-split | KEEP; one card mode |
| room_narrow_effects | e04f5ba 09-22 | logic | 2/2 |  | 1/1 | src/core/codes.h::wrap/row via chat.cpp room line wrap | pure-tested | CUT (mutation room_margin: passed; whole_line caught it) |
| room_narrow_whole_line | 5b9f25b 09-26 | mixed | 6/6 |  | 2/4 | src/plugins/chat.cpp ring line size (longest tag + 64), echo cut, /me  | needs-split | TRIM: keep wiring, move logic; one card mode |
| room_private_own_tag | 5b9f25b 09-26 | logic | 6/6 |  | 1/4 | src/plugins/chat.cpp private line formatting (P>/P marker + own tag) a | needs-split | MOVE logic to unit, keep 1 wiring check; one card mode |
| last_node_ten | 5b9f25b 09-26 | logic | 5/5 |  | 1/4 | src/core/bbs_shell.cpp LAST row formatter (node column via bbs_util.h  | needs-split | MOVE logic to unit, keep 1 wiring check; one card mode |
| room_squelch_ten | f34b090 09-30 | logic | 10/10 |  | 2/8 | src/plugins/chat.cpp::idOfTag, squelched; /sq answer short form | pure-untested | MOVE logic to unit, keep 1 wiring check; one card mode |
| long_help | e04f5ba 09-22 | mixed | 12/12 |  | 5/7 | src/core/helptext.cpp::find (verb and shortcut lookup, room vs prompt  | pure-untested | TRIM: keep wiring, move logic; one card mode |
| info_pages | e04f5ba 09-22 | wiring | 16/16 |  | 11/5 | src/plugins/info.cpp (staff page answers like a missing page, empty-pa | needs-split | KEEP; one card mode |
| mailbox | e04f5ba 09-22 | wiring | 14/14 |  | 10/4 | src/plugins/chat.cpp mailbox list/reader (row format, preview strips c | needs-split | KEEP; one card mode |
| seeded_screens_follow | e04f5ba 09-22 | logic | 5/0 |  | 1/4 | src/plugins/sd.cpp::seedScreens, seededHash, kPastStock (manifest hash | needs-split | MOVE logic to unit, keep 1 wiring check |
| forums_scan_staff | e04f5ba 09-22 | wiring | 2/0 |  | 2/0 | src/plugins/forums.cpp FORUMS SCAN permission; BULLETIN alias | n/a | KEEP |
| mail_compose | 144bc2d 09-22 | mixed | 7/7 |  | 4/3 | src/core/compose.h/composer.h (shared editor wrap, blank line kept) | pure-tested | TRIM: keep wiring, move logic; one card mode |
| forums | 9962da7 09-21 | mixed | 47/0 |  | 16/31 | src/plugins/forums.cpp (list/subject/reader layout: footer once, blank | pure-tested for the unread arithmetic | TRIM: keep wiring, move logic |
| handle_case | 13cbecc 09-22 | logic | 5/5 |  | 1/4 | src/core/users.cpp (case-insensitive lookup returns stored spelling; u | needs-split | MOVE logic to unit, keep 1 wiring check; one card mode |
| forums_remove | 4c30839 09-22 | wiring | 10/0 |  | 7/3 | src/plugins/forums.cpp removal (live flag X, mod level) and liveUnread | needs-split | KEEP |
| prompt_survives_notice | c4f37a8 09-22 | wiring | 5/5 |  | 3/2 | src/core/bbs.cpp deliverMail/notify lift-and-restore of the prompt (pr | needs-split | KEEP; one card mode |
| privacy | bd915b6 09-18 | mixed | 19/19 |  | 8/11 | data/screens/rules.* and privacy.* copy (tools/mkscreens.py PRIVACY_PA | n/a | TRIM: keep wiring, move logic; one card mode |
| announce | 2e09ac9 09-18 | mixed | 14/14 |  | 7/7 | src/plugins/announce.cpp::buildBody (payload fields, no caller data) a | needs-split | TRIM: keep wiring, move logic; one card mode |
| announce_closed | e7e510e 09-25 | wiring | 5/5 |  | 3/2 | src/plugins/announce.cpp::buildBody (closed field only while closed) | needs-split | KEEP; one card mode |
| announce_join_prompt | c3fb112 09-26 | env | 2/2 |  | 2/0 | src/plugins/announce.cpp nudge scheduling (nudge_seconds vs last post) | needs-split | KEEP, label env; one card mode |
| announce_join_in_flight | c3fb112 09-26 | env | 3/3 |  | 1/2 | src/plugins/announce.cpp nudge() while g_stage != Idle | needs-split | KEEP, label env; one card mode |
| announce_join_refused | c3fb112 09-26 | env | 4/4 |  | 1/3 | src/plugins/announce.cpp retry after 429 | needs-split | KEEP, label env; one card mode |
| announce_reliable | c3fb112 09-26 | env | 32/32 |  | 8/21 | src/plugins/announce.cpp (DNS keep-last, connect/timeout/close, reply  | needs-split | KEEP, label env; one card mode |
| time_warn_in_plugins | b6bff44 09-26 | wiring | 7/6 |  | 4/3 | src/core/bbs.cpp::checkTimers via liftInput/restoreInput; Bbs::markedL | needs-split | KEEP |
| lag_screens | c3fb112 09-26 | env | 3/3 |  | 2/1 | src/core runner SCREENS table build (bbs_screens / runner job) | n/a | KEEP, label env |
| lag_files | c3fb112 09-26 | env | 4/4 |  | 3/1 | src/plugins/files.cpp page build on the runner (one walk a page, FILES | n/a | KEEP, label env |
| lag_forums | c3fb112 09-26 | env | 5/5 |  | 3/2 | src/plugins/forums.cpp sliced subject walk and reader (one index open, | n/a | KEEP, label env |
| forums_segments | a6ff273 09-30 | env | 13/13 |  | 4/9 | src/plugins/forums.cpp::findSeg, parseHead seg=, post writer on the ru | needs-split for the segment choice | KEEP, label env |
| forums_long_read | a6ff273 09-30 | env | 14/14 |  | 5/1 | src/plugins/forums.cpp reader (readRow windows into s.compose, paging  | needs-split | KEEP, label env |
| forums_header_rebuild | ae1106e 09-30 | logic | 6/6 |  | 2/4 | src/plugins/forums.cpp::parseHead, headRead, runSeed/countLive (torn h | pure-untested | MOVE logic to unit, keep 1 wiring check |
| forums_seg_range | ae1106e 09-30 | logic | 6/6 |  | 2/4 | src/plugins/forums.cpp::parseHead (seg= > kSegLast is unknown) and fin | pure-untested | MOVE logic to unit, keep 1 wiring check |
| forums_page_fit | 96c4d10 09-30 | logic | 60/60 |  | 1/5 | src/plugins/forums.cpp::readRow (RD_GAP/RD_EOM phases), Bbs pager row  | needs-split | MOVE logic to unit, keep 1 wiring check |
| lag_logins | c3fb112 09-26 | env | 8/8 |  | 2/1 | src/core/users.cpp handle index; callstats.dat record I/O | needs-split | KEEP, label env; one card mode |
| lag_login_calls | 88e44a1 09-27 | env | 16/16 |  | 9/0 | src/core/bbs_users.cpp login passes (SHA slices, motd/landing passes,  | n/a | KEEP, label env |
| lag_logoff_calls | 88e44a1 09-27 | env | 9/9 |  | 2/3 | src/core/calllog.cpp ring write (header next/count, slot) | pure-tested | KEEP, label env |
| lag_last_calls | 88e44a1 09-27 | env | 4/4 |  | 1/1 | src/core/calllog.cpp walk order | pure-tested | KEEP, label env |
| lag_announce_calls | 88e44a1 09-27 | env | 7/7 |  | 4/1 | src/plugins/announce.cpp activity count (calls24/minutes24) over calll | needs-split | KEEP, label env |
| mail_in_place | c3fb112 09-26 | env | 6/6 |  | 4/0 | src/plugins/chat.cpp mail slot writes | n/a | KEEP, label env; one card mode |
| config_one_pass | c3fb112 09-26 | env | 4/4 |  | 2/0 | src/core/bbs_config.cpp page open (one-pass system.cfg read) | n/a | KEEP, label env; one card mode |
| space_kept | c3fb112 09-26 | mixed | 10/10 |  | 6/3 | src/core/space.cpp kept figures; MEM/SYS formatting in bbs_shell.cpp ( | needs-split | TRIM: keep wiring, move logic; one card mode |
| lag_backup_get | c3fb112 09-26 | env | 4/4 |  | 2/0 | src/core/bbs_backup.cpp CRC scan on the runner | n/a | KEEP, label env; one card mode |
| uploads_pending_bbs | c3fb112 09-26 | logic | 3/0 |  | 1/2 | src/plugins/files.cpp::notUpload | pure-untested | MOVE logic to unit, keep 1 wiring check |
| camera_one_at_a_time | c3fb112 09-26 | profile | 0/0 |  | 1/1 | src/plugins/camera.cpp busy refusal; Bbs::markedLine wrap | pure-tested | FIX HARNESS: never runs under --jobs; KEEP on its board axis |
| announce_badges | d117e84 09-23 | mixed | 27/25 |  | 8/15 | src/plugins/announce.cpp::slugList, payload builder (JSON escape, kBod | needs-split | TRIM: keep wiring, move logic |
| announce_directory | d117e84 09-23 | env | 0/0 |  | 2/0 |  | n/a | KEEP, label env |
| files | c7e86d2 09-19 | mixed | 67/1 |  | 38/22 | src/plugins/files.cpp mayRead/mayUp/visibleAreas/areaMenu, findDesc/se | needs-split | TRIM: keep wiring, move logic |
| binary | 1595822 09-20 | logic | 6/6 |  | 4/2 | src/core/telnet.cpp Telnet input (IAC unescape, CR rule under binary) | pure-untested | MOVE logic to unit, keep 1 wiring check; one card mode |
| xfer | 50ca31d 09-20 | mixed | 28/0 |  | 18/8 | src/core/xmodem engine; src/plugins/files.cpp mayUp/staging | pure-tested | TRIM: keep wiring, move logic |
| upload_no_binary | a53d5c3 09-20 | env | 6/0 |  | 3/0 | src/core/telnet.cpp binary state on the input path | n/a | KEEP, label env |
| ymodem | cca54ae 09-20 | mixed | 19/0 |  | 9/8 | src/core/xmodem engine YMODEM block 0 (name, length, truncate) | pure-tested | TRIM: keep wiring, move logic |
| dash_uploads | 52b5b33 09-24 | wiring | 4/0 |  | 3/1 | files plugin waiting hook -> DASH | n/a | KEEP |
| mail_never_lost | 2f57650 09-21 | mixed | 12/12 |  | 5/6 | src/plugins/chat.cpp mailSend slot allocation and per-box cap (3 flash | needs-split | TRIM: keep wiring, move logic |
| mail_rsd | a9eb971 09-21 | mixed | 16/16 |  | 9/6 | src/plugins/chat.cpp mail flags (MF_KEPT), reply-retires-original, unr | needs-split | TRIM: keep wiring, move logic; one card mode |
| list_abort_returns | d926cc7 09-21 | wiring | 4/0 |  | 4/0 | src/core/bbs.cpp Bbs::listEnded -> Plugin::listDone | n/a | KEEP |
| shutdown | fe3f2a0 09-21 | mixed | 8/8 |  | 5/2 | src/core/bbs.cpp::Bbs::serviceShutdown (kSay cadence, shutSaid_) | needs-split | TRIM: keep wiring, move logic; one card mode |
| staff_remembered | fe3f2a0 09-21 | mixed | 8/8 |  | 5/2 | src/core/bbs_users.cpp remembered staff (staff_at, staff_ip, staff_lev | needs-split | TRIM: keep wiring, move logic; one card mode |
| rename_follows | fe3f2a0 09-21 | mixed | 6/6 |  | 4/1 | src/plugins/chat.cpp::onRename (mail.dat and room ban rewrite) | needs-split | TRIM: keep wiring, move logic; one card mode |
| sd | 2f928fa 09-19 | mixed | 17/6 |  | 12/6 | src/core/screens.cpp ScreenPlayer::open lookup order; sd plugin closeC | needs-split | TRIM: keep wiring, move logic |
| partitions | f553996 09-18 | wiring | 11/11 |  | 4/0 | plat::userBase vs plat::fsBase on the host | n/a | KEEP; one card mode |
| refresh_and_ctrl_l | 8d295c6 09-19 | wiring | 4/4 |  | 3/1 | src/core/editor.cpp Ctrl-L redraw; term charset for refresh rows | pure-untested | KEEP; one card mode |
| screens | fc59daf 09-18 | wiring | 17/17 |  | 12/0 | rules/newuser/chatin screen playback hooks | n/a | KEEP; one card mode |
| exit_screen | fc59daf 09-18 | wiring | 2/2 |  | 1/1 | src/core/bbs.cpp exitScreen + BBS_EXIT_LINGER_MS | n/a | KEEP; one card mode |
| operator | ce04a7a 09-24 | mixed | 32/32 |  | 22/11 | src/core/bbs_ring.cpp ring outcomes (hidden=absent, away, decline, coo | needs-split | TRIM: keep wiring, move logic; one card mode |
| operator_ends | ce04a7a 09-24 | mixed | 18/18 |  | 13/4 | src/core/bbs_ring.cpp end states (timeout, stop, hang-up, room /o, for | needs-split | TRIM: keep wiring, move logic; one card mode |
| notices_in_places | ce04a7a 09-24 | wiring | 16/12 |  | 13/3 | Plugin::liftInput/restoreInput; bus delivery to plugin-owned sessions | n/a | KEEP |
| ssh_login | 810eb50 09-26 | env | 0/0 | g4848 40, mf35 40, mf35v2 40, s3 40, ws2 40, ws43b 40, wseth 40 | 15/3 | src/core/bbs_ssh.cpp sshAuth/sshLogin; sshd.cpp host keys | n/a | S3 lane only; KEEP, label env |
| ssh_new_caller | 810eb50 09-26 | env | 0/0 | g4848 10, mf35 10, mf35v2 10, s3 10, ws2 10, ws43b 10, wseth 10 | 4/1 | src/core/bbs_ssh.cpp sshAuth 'none' path | n/a | S3 lane only; KEEP, label env |
| ssh_resize | 810eb50 09-26 | profile | 0/0 | g4848 4, mf35 4, mf35v2 4, s3 4, ws2 4, ws43b 4, wseth 4 | 2/0 |  | n/a | S3 lane only; KEEP on its board axis |
| ssh_host_keys | 810eb50 09-26 | env | 0/0 | g4848 14, mf35 14, mf35v2 14, s3 14, ws2 14, ws43b 14, wseth 14 | 4/1 | src/core/sshd.cpp host keys; src/core/ziparc.cpp backup file list | n/a | S3 lane only; KEEP, label env |
| ssh_telnet_unchanged | 810eb50 09-26 | profile | 0/0 | g4848 10, mf35 10, mf35v2 10, s3 10, ws2 10, ws43b 10, wseth 10 | 3/2 | src/core/bbs_ssh.cpp::Bbs::sshSniff (SSH-2.0- prefix match during Dete | needs-split | S3 lane only; KEEP on its board axis |
| ssh_full | 810eb50 09-26 | env | 0/0 | g4848 20, mf35 20, mf35v2 20, s3 20, ws2 20, ws43b 20, wseth 20 | 4/6 | src/core/bbs_ssh.cpp::Bbs::sshHandoff refusal (identification + one DI | needs-split | S3 lane only; KEEP, label env |
| ssh_failed_logins | 810eb50 09-26 | mixed | 0/0 | g4848 12, mf35 12, mf35v2 12, s3 12, ws2 12, ws43b 12, wseth 12 | 3/3 | src/core/guard.cpp (LoginGuard lockout 5/15 min, BanList strike per fa | needs-split | S3 lane only; TRIM: keep wiring, move logic |
| ssh_ymodem | 810eb50 09-26 | env | 0/0 | g4848 9, mf35 9, mf35v2 9, s3 9, ws2 9, ws43b 9, wseth 9 | 5/4 | src/core/xmodem.cpp over the SSH link (no IAC doubling, no CR padding) | pure-tested | S3 lane only; KEEP, label env |
| ssh_dedicated_port | 3b6e3bd 09-26 | env | 0/0 | g4848 24, mf35 24, mf35v2 24, s3 24, ws2 24, ws43b 24, wseth 24 | 8/3 | src/core/sysconfig.cpp::crossCheck (ssh_port vs port/backup_port clash | pure-untested | S3 lane only; KEEP, label env |
| ssh_socket_budget | 3b6e3bd 09-26 | env | 0/0 | g4848 18, mf35 18, mf35v2 18, s3 18, ws2 18, ws43b 18, wseth 18 | 6/3 | src/core/bbs_ssh.cpp::Bbs::busyFits (listeners+sessions+lingering+1+BB | needs-split | S3 lane only; KEEP, label env |
| boot_hold_write_fails | edf2431 09-24 | env | 4/4 |  | 2/2 | src/core/sysconfig.cpp::syscfg::write (temp + rename, no remove first) | needs-split | KEEP, label env; one card mode |
| boot_hold_factory_fails | edf2431 09-24 | env | 0/0 |  | 3/2 | src/core/recovery.cpp factory erase failure note (NOTE_FACTORY_FAILED) | needs-split | KEEP, label env |
| sysop_spelled_default | edf2431 09-24 | mixed | 7/7 |  | 4/3 | src/core/sysconfig.cpp::parseFile / useDefaultSysop (explicit publishe | pure-untested | TRIM: keep wiring, move logic; one card mode |
| config_pin_exists | edf2431 09-24 | mixed | 7/7 |  | 2/3 | src/core/sysconfig.cpp::syscfg::pinProblem (per-chip missing GPIOs fro | pure-untested | TRIM: keep wiring, move logic; one card mode |
| user_admin_retire | edf2431 09-24 | wiring | 5/5 |  | 3/0 |  | n/a | KEEP; one card mode |
| files_typed_number | f34b090 09-30 | mixed | 7/0 |  | 3/4 | src/plugins/files.cpp::startsMore / pastTen | needs-split | TRIM: keep wiring, move logic |
| backups_area | 94aa16e 09-24 | wiring | 31/0 |  | 25/6 | src/plugins/files.cpp Backups area (zip-only, no approval, staging cle | needs-split | KEEP |
| card_screens_manifest | 94aa16e 09-24 | mixed | 11/0 |  | 3/8 | src/plugins/sd.cpp seeding manifest (.seeded, kPastStock, 00000000 sys | needs-split | TRIM: keep wiring, move logic |
| restore_checks | 94aa16e 09-24 | logic | 5/0 |  | 1/4 | src/core/ziparc.cpp (sysopEmptied, peak-space check, EOCD search toler | needs-split | MOVE logic to unit, keep 1 wiring check |
| restore_staff_report | 94aa16e 09-24 | mixed | 10/10 |  | 6/4 | src/core/ziparc.cpp unredact/drop of published co-sysop password; src/ | needs-split | TRIM: keep wiring, move logic |
| restore_ends_screens | 94aa16e 09-24 | wiring | 5/5 |  | 4/0 |  | n/a | KEEP; one card mode |
| sd_no_reprobe | 94aa16e 09-24 | wiring | 6/6 |  | 4/2 | src/plugins/sd.cpp::start (probe only at boot, SD MOUNT, or pin/speed  | needs-split | KEEP |
| rewrites_keep_old | 94aa16e 09-24 | env | 11/11 |  | 4/5 | chat mail.dat rewrite, info page save, files FILES.BBS, sd .seeded (re | needs-split | KEEP, label env |
| restore_waits_quiet | 94aa16e 09-24 | wiring | 13/13 |  | 10/3 | src/core/bbs_backup.cpp restore hold state machine (wait, F, give-up a | needs-split | KEEP |
| screens_command | 94aa16e 09-24 | mixed | 15/12 |  | 8/10 | src/core/bbs_screens.cpp (screen name validation 'Not a screen name.', | needs-split | TRIM: keep wiring, move logic |
| screens_install | e936a65 09-25 | env | 18/0 |  | 6/12 | src/core/bbs_screens.cpp SCREENS INSTALL plan (whole-or-nothing, room, | needs-split | KEEP, label env |
| forms_wide | d1b772c 09-24 | logic | 12/12 |  | 3/9 | src/core/form.h::Form::labelWidth/boxCol/lineWidth and form.cpp draw a | needs-split | MOVE logic to unit, keep 1 wiring check |
| forms_narrow | d1b772c 09-24 | logic | 10/10 |  | 2/8 | src/core/form.h geometry at 40; plugin PluginSetting short labels/note | needs-split | MOVE logic to unit, keep 1 wiring check |
| forms_ascii_wide | d1b772c 09-24 | mixed | 7/7 |  | 3/4 | src/core/bbs_sysop.cpp line-mode CONFIG prompts ('[set, - clears]') | needs-split | TRIM: keep wiring, move logic; one card mode |
| whois_wide | d1b772c 09-24 | logic | 5/5 |  | 1/4 | src/core/bbs_users.cpp::cmdInfo layout by width | needs-split | MOVE logic to unit, keep 1 wiring check; one card mode |
| config_serial_rows | d1b772c 09-24 | profile | 6/6 |  | 3/4 | serialbridge.cpp PluginSetting rows; bbs_sysop.cpp console-pin rule (B | needs-split | KEEP on its board axis; one card mode |
| config_chat_colours | d1b772c 09-24 | wiring | 6/6 |  | 4/1 | chat.cpp PluginSetting table (colour sub-page) | n/a | KEEP; one card mode |
| config_announce_desc | d1b772c 09-24 | logic | 2/2 |  | 0/2 | src/core/bbs_sysop.cpp CONFIG value buffer length vs announce's 120 | needs-split | MOVE logic to unit, keep 1 wiring check; one card mode |
| config_pin_holders | d1b772c 09-24 | mixed | 7/7 |  | 2/5 | src/core/bbs_sysop.cpp::pinTaken (cross-plugin pin holders, BOOT, cons | needs-split | TRIM: keep wiring, move logic |
| config_forums_grow | d1b772c 09-24 | mixed | 7/7 |  | 3/4 | src/core/bbs_sysop.cpp PS_GROW grouping (groupInline, isPage) and comp | needs-split | TRIM: keep wiring, move logic; one card mode |
| config_warn_levels | d1b772c 09-24 | mixed | 11/11 |  | 4/6 | src/core/bbs_sysop.cpp warn check over PluginSetting::warnAbove (light | needs-split | TRIM: keep wiring, move logic; one card mode |

## Appendix B: what each test was written for, and the readers' notes

The "written for" column is taken from each test's docstring and comments. The notes are **from reading only and unverified**, except where section 3 or 5.2 settles them. On the one test where they could be checked (`test_forums`), two of three such notes were wrong. Treat each as a lead for a mutation, not a finding.

| Test | Bug it was written for (from its docstring) | Reading notes: possible weak or vacuous checks, order dependence (unverified unless section 5 says so) |
|---|---|---|
| ansi | PuTTY line-mode '[3;20R' CPR reply was accepted as a handle; now refused by validHandle. | 'WHO does not list the busy line' is a `not in` on the raw buffer; 'time left shown' only finds the word 'minutes'. |
| telnet_first | PuTTY's line-mode '[3;20R' leak: an IAC-first client now gets character mode before the CPR probe. | check('NAWS size applied', True) asserts nothing; the 80x24 NAWS sent is never verified (e.g. TERM). first_probe<0.25 is wall-clock and can flake on a loaded host. |
| petscii |  | 'prompt after ~2 s' is a wall-clock window (board_secs + FAST_SLACK) and can flake on a busy host. |
| ascii | rowWidth clamped every row to 40 cols (0.17.10); kUsageCol 13 truncated 'ANNOUNCE TEST'. | 'wrapped lines (if any) sit under the description column' is all() over a usually empty list at 80 cols: passes vacuously. |
| sysop | HELP used to page at [More] every time; the old test asserted the paging (0.17.9 rework). | 'hidden sysop not in WHO' regex [S1-6][ *>\]]Rob runs on raw x.buf (an escape between marker and handle defeats it) and only node digits 1-6 though there are 10 nodes: can pass while the sysop is shown. |
| cosysop |  | Order-dependent: 'a co-sysop cannot edit the sysop's account' needs Rob from test_sysop (NEEDS). 'hidden co-sysop shows as a free line' is only re.search(Cora) is None on raw buf. |
| accounts | SGR 27 ignored by SyncTERM/ANSI.SYS left reverse video across form labels (orange block, 0.17.1). |  |
| user_admin | USER DEL freed the handle; the next registrant inherited the old owner's undelivered mail (0.19.0). | 'a retired handle is never offered to somebody new' is which != 0, which also passes on -1 (no reply/timeout). 'staff WHOIS shows private fields' reads Acct from test_accounts (NEEDS). Form walked by DOWN*7/DOWN*8 field counts. |
| plugins |  | 'its config key was read' re-tests the same b'howdy' the previous check already matched. |
| radio_link |  | 'a caller cannot pair' is `not wait_for('Pairing is open')`: passes on silence; 'LINK lists it, up' waits for b'up', which almost anything contains. SKIPs without BBS_LINK_PORT/linkpeer or sysop password. |
| doors | CONFIG link save left a caller in a door silent (1.1.2); 0x1D exit hit PETSCII cursor-right (link.8). | Order-dependent on test_radio_link (NEEDS). 'Ctrl-] is not the way out' and 'three Ctrl-Cs spread...' are negative waits that pass if the session died. |
| link_shared |  | NEEDS test_radio_link; SKIPs without BBS_LINK_EXTRA_PORTS. 'and pairs nothing' is a None-wait that passes if linkpeer died. |
| sats | Unpair acted on the Enter that walks to Save and forgot the sat (link.9 review); shelf-2 broke the log check (1.2.0 run). | SKIPs unless --ext camsat --card. Leak checks are `not leaks` (vacuous on a short reply), though the staff-view self-check guards the regexes. 'shelf' vs 'shelf-2' depends on radio tests' leftovers (handled). |
| doors_petscii | Ctrl-] (0x1D) exit was PETSCII cursor-right: three moves right threw a C64 out (link.8). | NEEDS test_radio_link. 'four cursor-rights in a door stay in the door' is a negative wait that passes if the session died. |
| version_shown |  | SKIPs without the sysop password; 'the boot line says it' depends on host.log existing. |
| chat | ESC used to leave chat; the old check waited for 'Main' (also the room's name) and passed either way. | 'no prompt character in the room' is b'> ' not in raw buf; 'ESC does not leave the room' is two `not in` checks with no positive proof the caller is still in the room. |
| serial |  | 'the operator's typing reaches the device and comes back' could be met by local echo; the watcher's check is the real proof. |
| idle_login |  | 'hangup at 60 s' runs only with --slow, so the hangup is never checked in a normal run; the redraw check pins exact colour bytes. |
| busy |  | Assumes every node is free at the start: callers left lingering by earlier tests shift which socket is 'over'. |
| motd |  | 'nothing after the stop' is a `not in` check. |
| backup | '#' cut any line and would store half a Wi-Fi passphrase (0.22.1). | 'summary names the files' looks for 'users' anywhere; relies on the harness cfg's wifi_password 'pa#ss word1'. |
| backup_published_default | 1.0.1 restored '***' as the published default explicitly: it worked from anywhere and the board got listed (1.0.2). | static_checks=0 because every check is in published_default_fresh/_configured; the fresh half runs only on a --fresh board (BBS_FRESH), the configured half otherwise. |
| backup_card |  | Without a card only 2 checks run. 'a second in the same minute is refused, or has a minute of its own' accepts either outcome. |
| backup_card_nightly |  | Uses the UTC hour and date at start: a run straddling the hour or midnight can fail. |
| restore_cross_partition | Restoring a backup deleted every account, 0.14.0 to 1.0.2: EXDEV rename, then remove(dst) and fail again (1.0.3). | SKIPs off-host or without /dev/shm. |
| config_timezone |  | Walks rows with DOWN * BOARD_ZONE / BOARD_TZ counts, brittle when a row is added. |
| config_tz_bad | newlib tzset silently ran unnamed UTC on an unreadable custom TZ string (1.1.1 TZ-bad). |  |
| config_cycle_numbers |  | Answers are counted per row (13 blanks for board, SD_ROWS_AFTER_READ for sd), the field-counting pattern CLAUDE.md warns about; the restore of sd's read level is not checked. |
| ban |  | The one check (closed within 3 s, no 'DETECTING') would also pass if the board crashed or refused the socket; the three wrong attempts are never confirmed. ALONE: bans 127.0.0.1 for 15 min. |
| menus |  | 'a plain caller is not offered the staff menu' (b'? staff' not in main) is vacuous: the footer grammar never prints '? staff' for anyone. '? names the other menus' passes on CHAT, which is already a main-menu command. |
| sysinfo | CALLS was sysop-only; Rob made it public (no handles or addresses in it). | Elevates with a hardcoded 'bye testsysop' (not PASSWORD) and never checks it worked. 'SYS is staff only' (Unknown in buf or no 'network') passes on an empty reply. Stack-log check silently skipped without host.log. |
| calls_one_screen | CALLS was one column of 24 hours, 28 rows, paging mid-evening at every width (F5, 1.2.1). |  |
| ssh_signup_no_privacy_offer | SSH callers were offered telnet's 'NOT ENCRYPTED' privacy screen; a guest could take a registrant's handle during the pause (calls.3). | SKIPs on the reference board and every non-SSH profile (ssh_ready); 'and offers no privacy screen' is a negative (`know more` not in buf) after a 0.8 s pump. |
| signs_name_board | Signs named BBS_NAME not the board; a µ at the 40-byte/40-column cut printed as a lone 0xC2 '?' (1.2.1-calls.2/3). |  |
| hardware | The capability line changed mid-listing when a card was pulled while SYS sat at [More] (1.1.1). | 'a caller never sees the live figures' and 'nor anything about the network' are negatives on render output (sound only because earlier positive row checks prove the screen drew); `PSRAM none` and `Chip host` are asserted unconditionally even on profile runs; card-at-[More] check only runs with a card. |
| dash_frame | DASH 1 scrolled on 80x24 every redraw, showed 6 of 12 sessions busiest-first, any key quit; a window resized to 20 rows redrew 24 (1.1.0). | 'a key the dashboard does not use is ignored' passes if the board simply stalled (DASHBOARD from an earlier frame in buf counts). |
| nodes_columns | NODES cut PETSCII-40 to PETSCII-4; WHO rows misaligned 11 columns under 80-col headings; NODES n drew WHO's rows (1.1.0). |  |
| operator_notes | A sysop already on had no way to read ring notes without logging in again (1.1.0). |  |
| ring_mail | Missed rings went to a notes file only; Rob: 'the sysop page should drop to email' (1.1.0). | Several checks read host.log console lines (exact wording), brittle to log rewording. |
| sysop_account | Missed rings copied to every account ever marked sysop; a restored users.txt could make sysop.last name anybody. |  |
| sysop_burst | Rob's SyncTERM autologin stopped at 'Sysop password:' because askSysop dropped held keys (1.2.1-dev.1). |  |
| sysop_burst_wrong | A held lone Enter answered the question (dev.2); the ban window did not start at a held guess (dev.1 review). | Internally order-dependent: the second burst's 'held line is dropped' relies on the first wrong burst having spent the window's one allowance; 'with nothing said' and 'not run as a command' are negatives; several checks count host.log lines. |
| dash_card_age | DASH 1 put f_getfree (15-160 ms) into the loop every third frame (1.1.0). | Docstring says 'a minute old' but since 1.1.2 the figure is kept until FORCE/staff login; 'and a minute's figure' (after == before) only asserts caching and would pass if the figure never updated. SKIPs without a card. |
| dash_opens_nothing | The old DASH opened the caller log ~7 times a second in the loop (1.1.0). | Vacuous if the shim does not load or log: nothing asserts fopen.log recorded any open at all (e.g. the login's); only fopen/fopen64 are hooked, so an open()/opendir path would not be seen. |
| boot_notices | Restart notice was 42-52 columns (wrapped on a C64) and pointed at reboots.log, which nothing on the board can read (1.1.0). | 'every line inside 40 columns' checks the test's own `want` literals, not the board's output: always true. |
| room_new_commands | An arrival printed after the [>n] marker and the redraw lost it (0.21.7); the sender saw a 'sent.' per line, not the words. | '/whois names the caller' waits for b'Target', which the caller's own echo of '/whois Target' already contains, so it passes before WHOIS answers and the following no-leak check may read an incomplete reply. '/b toggles the bell' checks only the reply text (test_bell checks the 0x07). |
| room_private | /p* showed other callers' lines (1.1.1); the partner's own leave line was counted as 'went by' (1.1.2 bench). | Rewrites [plugin:chat] (history 8, rate 600) on the shared harness board and restores only on the normal path; a raised exception leaves history=8 for later tests. |
| room_commands | /me rendered '#1:The Legend* * flexes' with a doubled marker; verbs needed a space before the node number. | Hard-coded node numbers (/p1, /sq 2, /vk 3, /k 2) assume Ay, Bee, Cee land on nodes 1-3: order-dependent on a board with no leftover sessions; 'bye testsysop' hardcodes the password instead of PASSWORD. |
| mail | A second message REPLACED an unread one, and the old test asserted it as correct (0.17.11). | Docstring 'One message per caller, read once, replaced rather than doubled' describes the old bug the body now refutes (stale). |
| config | A reload re-registered plugin commands until the table filled, so later plugins answered 'Unknown command' to their own verbs. | 'bye testsysop' hardcodes the password; deliberately leaves max_users at 200 on the shared board, coupling later tests. |
| config_parser_rules | Rob's 'therustyantenna.local' hostname was written, then the reload refused the whole file and nothing on the page went live (1.0.0). | LED -1 half SKIPs where PIN_BOARD has no led_free. |
| config_guards | One backspace on the masked field emptied the sysop password, locking staff out until a reflash (1.0.0). | SD-pin half SKIPs where PB['sd_rows'] is empty. |
| photos_config |  | Rewrites the shared harness system.cfg directly; restored in finally, but cfg_reload there is unchecked. |
| config_semicolon | 'Games; Demos' as an area name was cut at ';', its levels fell back and a staff-only area opened to everybody (1.0.0). |  |
| config_sd_plugin | A PF_SD plugin's page said 'Saved and live' with no card (1.0.0-rc1). | Restores Read to 'all' unconditionally rather than to what it was; branch chosen by BBS_SD_DIR, so each run checks only one half. |
| config_lights |  | 'off as shipped: LIGHTS is not a command' and 'lists its pixels as shipped' rely on earlier tests having removed [plugin:lights] (order-dependent); round trip SKIPs on espcam/ws2/ws43b; whole test SKIPs on profiles with no LIGHTS_BOARD row (wseth, mf35, mf35v2, g4848). |
| config_lights_ascii | Plain ASCII line mode saved a cycle pick as a single space while saying 'Saved and live' (1.1.0). | Leaves [plugin:sd] read = sysop on the shared harness board (never restored); row counts rely on the per-profile SD_ROWS_AFTER_READ table (wrong on the 4.3B until dev.13), and a wrong count lands Enter on 'Save (Y/n)?'. |
| lights_frames |  | Animation checks ('the head moves', 'blinken changing', 'vu bar moved') sample real time and depend on host scheduling; hayes waits up to 10 s for earlier callers' lines to close (order-dependent on leftover sessions). |
| lights_manual |  | 'random: a new colour as it goes' needs a 3 s board timer inside 3.6 real s: not in REALTIME, so it relies on the 4x fast clock to have margin. |
| config_silent |  | 'and the console says so' is guarded by `if log.exists()` and silently skipped when host.log is absent. |
| lights_silent |  | Waits on the board's real wall-clock minute (up to ~2 min); SKIPs on profiles with no LIGHTS_BOARD row. |
| lights_disk | 1.1.0 lit the drive light only from screens and the sd plugin; users.txt, caller log never lit it. | SKIPs on every no-card lane (needs BBS_SD_DIR); SKIPs on ws2/wseth/mf35/mf35v2/g4848 (no LIGHTS_BOARD row). |
| lights_count | On 1.1.0-dev.7 the strip was always ten pixels whatever the setting. | CONFIG rows reached by DOWN*11 / DOWN*10 counts (the fragile shape CLAUDE.md replaced with cfg_walk_to); 'nodes' check assumes the caller's node <= 10. |
| lights_order |  | Rows reached by DOWN*12 count. |
| board_ws43b |  | REALTIME (5 real-second hold check); exact pixel coordinates (px(7+39*i+17,225)) make it a layout snapshot; 'no SPI pins line' is a not-in check that passes on an empty PANEL reply (guarded only by the earlier 'lit' check). |
| board_fncam | A WROOM backup's LED on GPIO 2 is this board's card; drop the line, not refuse the whole file (1.0.2 rule). |  |
| board_espcam | WROOM backup restored here set activity_led_gpio=2 (card MISO); the red LED on 33 went dark. | 'CONFIG refuses the LED on GPIO 2' accepts any verdict but None/'Saved' (got not in (None, b'Saved')), so a wrong reason passes. |
| board_ws2 | Until 1.2.1 the list asserted two bugs: LED row capped at 39, host console fixed at WROOM's 1/3. | REALTIME; the tap check retries 3 times to dodge the board's own header turn, which can mask a tap that does nothing if the board happens to turn on a retry; rows reached by DOWN*14 / DOWN*4 counts; '42C' is a host constant. |
| board_wseth |  | SATS check is skipped silently when SATS is 'Unknown command' (host camera off, so on the harness it never asserts); the not-joined half only runs without BBS_HOST_SSID, which harness.sh always sets; 'Wi-Fi beside wire' accepts either of two labels. |
| board_mf35 |  | Pins row reached by DOWN*5 (correct for v1.0 per CLAUDE.md, but the count shape broke mf35v2); restores system.cfg by raw write_text(before). |
| board_mf35v2 | Copied DOWN*5 from v1.0 failed: v2.0's touch puts Sleep above Skin; now walks by the row's note. |  |
| board_g4848 |  | Load-line regex also accepts 'no reading yet', so it passes before any sample; tap check uses a fixed 2 s sleep without REALTIME (works on the fast clock, but no hold is checked). |
| camera | FILES menu marked Photos '(staff)' from its placeholder level, not the camera's Photos setting (found on PixelBBS, 1.1.1). | Never runs under harness.sh --jobs: SKIPs on the reference board (HOST_BOARD '' not in CAM_BOARD) and is not in PROFILE_TESTS for fncam/espcam/ws2/wseth (fncam/espcam not in PROFILE_CARD either); only a manual --board fncam --card run reaches it. Also slow (real sleeps: 11 s timelapse, 7 snaps x 1.1 s). |
| board_s3_skin | Skin change/re-upload drew the status layout whole between skins, 24-30 ms of loop on MF35. | One check sets hostio.txt open cost to 500 ms to slow a reload enough to observe the old skin held (real-time dependent, not in REALTIME). |
| board_s3_silent |  | wait_minute_turn waits for a real board minute (slow); if board_clock is None the test fails with a constant check. |
| camera_registry |  | Never runs under harness.sh --jobs: SKIPs on the reference board (HOST_BOARD '' not in CAM_BOARD) and is not in PROFILE_TESTS for fncam/espcam/ws2/wseth (fncam/espcam not in PROFILE_CARD either); only a manual --board fncam --card run reaches it. |
| camera_failed_start | FNCAM 1.0.0: a failed start never closed, so the next snap said 'needs memory the board is using'. | Never runs under harness.sh --jobs: SKIPs on the reference board (HOST_BOARD '' not in CAM_BOARD) and is not in PROFILE_TESTS for fncam/espcam/ws2/wseth (fncam/espcam not in PROFILE_CARD either); only a manual --board fncam --card run reaches it. 'with no countdown' is a not-in check that passes on an empty reply. |
| announce_camera |  | Only 1 check runs on the reference board; the camera half never runs under --jobs (not in PROFILE_TESTS, needs --board fncam --card). 'sensor was found' also accepts any 'camera: SNAP-' line. |
| camera_silent |  | Never runs under harness.sh --jobs: SKIPs on the reference board (HOST_BOARD '' not in CAM_BOARD) and is not in PROFILE_TESTS for fncam/espcam/ws2/wseth (fncam/espcam not in PROFILE_CARD either); only a manual --board fncam --card run reaches it. 'the camera never started the pixel' asserts the absence of 'pixels: output 0 on gpio N' with no positive control for that string; 'drive light never went white' all() passes if no samples were read. |
| board_s3 | 1.1.0-dev.7 had no S3 profile: no panel, lights off, GPIO 7 refused as flash, card on 5. | Header page-turn checked over 12 real seconds (fine on the fast clock, slow otherwise); rows reached by DOWN*5/10/15 counts; writes system.cfg directly in panel_key. |
| config_wifi_live | secrets.h board showed empty boxes; changing SSID without retyping saved no password, board went off-air. | 'a new network typed with it is not the accidental case' (OPEN not in s.buf) is vacuous alone; SKIPs without BBS_HOST_SSID. |
| config_network | The listening port was build-time BBS_PORT; second board behind a same-port-only router was impossible. | 'this board is not listening on it yet' (not port_answers) passes vacuously; port restored only at the end, so a mid-test failure leaves port changed for test_config_parser_rules (documented dependency). |
| boot_hold | Setup screen's @CLS@ wiped the 'password reset by BOOT' notice; now told after setup. | Last check 'with nobody on the button a boot says nothing' is True when host.log is missing (else True). |
| cgnat_local | Shell took all 127/8, backup port took 100.64/10 unconditionally; one rule now, CGNAT opt-in. | 'put back: off again' parses the host log's last cfg summary (syscfg_cgnat_on), not the file. |
| config_announce_outside | The row was 'Port'; a board that never set it published 6400 whatever it listened on. | Outside row reached by DOWN*7. |
| accounts_form_notes | Bbs::addField never cleared FormField::note; PROFILE rows inherited sign-up's 'Only you and staff' note. | 'the Profile row does not claim to be private' is not any(...) over render_lines: passes on an empty or unrendered screen, and nothing first proves the note is shown on Email in the sign-up form or that DOWN*4 landed on Profile. |
| config_areas | A dropped line inside a CONFIG sub-page must release the editing guard. | Rows by DOWN*4 counts; restore checks are substrings, so leftover level parts from the ASCII save pass unnoticed. |
| config_area_keeps_every_part | Composite count 4 vs six parts: saving an area dropped Download/Delete, making staff-only downloads public. | Leaves area5 renamed 'Drop Zone' with levels pinned and never restores it: any later test in the same lane that opens 'Drop Box' (test_xfer, test_ymodem, test_dash_uploads, test_upload_no_binary use enter_area(5, b'Drop Box')) breaks if ordered after it. |
| config_forum_levels | Unset forum levels seeded by position: Reply got co1, Moderate nobody; Rob could post but not reply. | Leaves topic1 renamed 'General Talk' with all levels pinned; never restored (order-dependent leftover). |
| room_quit_logoff | The help column was eleven wide and cut '/whois handle' to '/whois hand'. | 'with the send-off rather than a prompt' (Main: not in buf) is vacuous on its own. |
| welcome_connecting | Line read 'Connecting you unleashed' / 'µnleashed BBS running µnleashed BBS' (no 'to', software name). |  |
| paced_chatin | showScreen's loop and @BAUD@ pacing never met: a paced chatin lost all but ~48 chars at close(). | SKIPs on every no-card lane. |
| room_time_staff_only | 0.21.4-0.22.0: any caller could /t -1 or give a node 600 minutes; the room bypassed PERM_TIME. |  |
| bell | /b toggled a flag nothing read (0.21.4-0.22.0); the old test checked only the 'Bell off.' answer. | 'and an arrival no longer rings' and 'and /b stops it' assert no 0x07 without checking the preceding wait_for('is on node' / 'again') succeeded, so they pass if the notice never arrives. |
| codes_in_messages |  | 'CODES at the prompt explains them' accepts plain b'BLINK' as a fallback, so it passes on any text containing BLINK; both 'without ringing' checks are `\x07 not in buf` and pass on an empty reply (guarded only by the preceding check); 'and the test takes its post down again' asserts test hygiene, not behaviour. |
| first_setup | 0.22.3 FF_REPLACE: typing over the mask saved '********unleashed'; setup screen told sysops to backspace over stars. | On the ordinary (non-fresh) board only 3 checks run, and 'a configured board offers no setup' is a `not in` that passes on any reply; CONFIG board reached by DOWN*12 (row counting, breaks when a row is added; cfg_walk_to exists). |
| setup_abort | 1.1.0 bug A: abortOutput cleared pendingLand not setupStage, so setup reopened CONFIG staff at random. | SKIPs on every non --fresh run; relies on password+space in one write landing the space while the screen still plays (timing-dependent on a loaded host); 'a screen played afterwards does not open CONFIG staff' is a `not in` over whatever arrived (partly guarded by the next check). |
| closed_fresh |  | CONFIG board's closed row reached by DOWN*12 three times (row counting); ~10 checks are exact copy strings ('New board: type a handle to set it up.', 'To open it, CONFIG board and set Stop taking calls to no.') that the 1.2.2 screen redesign will break; 'with no hint to join or visit' is a `not in` pair. |
| closed_configured | 1.1.0: boards set up before 1.1.0 (UHQ, TRA) must stay open on upgrade; 1.1.1 failed when run after test_backup_card. | Order-dependent: test_backup_card leaves closed = no, the test rewrites system.cfg to recover (documented); sends the literal b'testsysop' rather than PASSWORD for the elevation; CONFIG rows reached by DOWN*8/DOWN*4/UP*4 counting. |
| fx_codes | Rob 0.22.2: FX did not show the codes for the effects it demonstrates. |  |
| room_narrow_effects | Code review 0.22.0: C64 reader lost words inside an effect crossing column 39. | 'the words inside the effect arrive' (b'yourself' in buf) also passes if the code printed raw as '@TYPE:yourself@', so it cannot tell an acting effect from a broken one; no check of the 39-column fit. The 'narrow' caller is plain ASCII with unknown width (40 by default), not set by NAWS. |
| room_narrow_whole_line | 1.1.2 F2: ring slot 64 incl. tag cut lines ('how is the weather' arrived as 'how is'); /me cut at 44. |  |
| room_private_own_tag | 1.1.2 F3: own private line printed the partner's tag, reading as the partner saying it. | 'not the partner's' is a `not in` that passes on an empty sender buffer (guarded by the preceding positive check). |
| last_node_ten | 1.1.2 F4: LAST printed '0'+node%10, so node 10 showed as 0. | SKIPs on profiles with fewer than ten nodes; fills MAX_NODES-1 raw connections to reach node 10 (expensive, sensitive to a slow host finishing detection). |
| room_squelch_ten | 1.2.1: idOfTag read one character, so /sq 10 hid nothing and /sq 1 also hid node 10. | 'node 1 is not' and 'its lines are hidden' are `not in` after a 1 s pump, which pass if delivery is merely slow or broken (each paired with a positive check on the other node); SKIPs with fewer than ten nodes; fills MAX_NODES-2 connections. |
| long_help |  | 'and still fits one screen' is `[More] not in`, vacuous on an empty reply (guarded by the previous check). |
| info_pages | 0.22.0: the info pages are new; probing numbers must not reveal hidden pages. | Depends on the harness config seeding page0 (titled, empty) and page1 (staff only); SKIPs with no sysop password. |
| mailbox | Rob 0.22.0: mail could not be read without deleting; no list. | Cleanup loop deletes up to 4 messages without checking; a run that leaves mail in Boxreader's box would skew '3 new' next time. |
| seeded_screens_follow | 0.21.9: welcome change never reached a board with a card; seeded card copy shadowed new stock. | Writes test files into the harness board's own DATA/screens stock folder (shared state, cleaned in finally); `ok` would be unbound if the first write raised before the first check; SKIPs without a card and the sysop. |
| forums_scan_staff | 0.22.0: FORUMS SCAN showed unreadable forums to anybody. | SKIPs without a card. |
| mail_compose | Ctrl-D was offered as a finish key and never reached the board (SyncTERM takes it). | Docstring promises 'the same refusal to lose a blank line' but no check asserts the blank line survived (only 'Second paragraph' present); 'it names /s' duplicates the first check's wait; reads the oldest message in Recipient's box, so mail left there by another test makes it read the wrong one (order coupling noted in test_prompt_survives_notice). |
| forums | 0.21.x: poster could not reread own only message; Enter never matched KEY_ENTER; per-caller unread written board-wide; footer printed twice. | 'the subject that was read is no longer marked new' asserts b'2 msgs' in the list, true for both subjects whether read or not (vacuous); 'and the other subject still reports what is left in IT' asserts b'new' in the list, which the title bar always contains (vacuous); 'the forum list agrees with the subjects underneath it' is `b'4 new' not in`, passes on an empty reply; 'a second caller sees the messages as new' (b'4 new messages') depends on forum 1 holding no other test's posts (order coupling). |
| handle_case | 0.21.6: Rob typed QuantumRob, greeted as quantumrob (stored lower case); re-case via USER EDIT must not be refused. | 'and never in the case that was typed' is a `not in` (guarded by the positive before it). Permanently renames the account to MIXEDcaseX. |
| forums_remove | 0.21.7: no way to remove a post; removed unread post still said '1 new'. | Before/after new_count compares the forum list across time; another test posting to forum 1 in the same lane in between would break it (order coupling). SKIPs without card or sysop. |
| prompt_survives_notice | 0.21.8: mail notice left the prompt unreturned, or the old prompt above the notice. |  |
| privacy |  | 'it is paged rather than a wall of text' pins pages == 4 and ~8 checks pin exact copy ('Four short pages', 'Low risk. Not no risk.', 'NO HATE'); the 1.2.2 screen redesign will break them without any behaviour change. |
| announce | 1.0.1: a token split across packets produced ninety listings for one board. | Elevates with the literal b'bye testsysop' rather than PASSWORD; 'and carries no caller data' (b'Announcer' not in shown) passes on an empty reply. |
| announce_closed | 1.1.0 held closed boards off the directory so listings went stale. | Rewrites the harness board's own system.cfg (restored in finally); a crash mid-test leaves the board closed for later tests. |
| announce_join_prompt | 1.1.0/1.1.1: a join within 60 s of the last round waited for the gap. |  |
| announce_join_in_flight | 1.1.0/1.1.1: a join during an in-flight post was dropped. | 'the second join landed while the post was held' only measures the test's own timing, not board behaviour; if held is None the final check fails with no diagnosis. |
| announce_join_refused | 1.1.0/1.1.1: a refused post was never retried; join waited for the next heartbeat. | 'but not inside the directory's 30 s' is `p is None or ...`, passes when no retry came (guarded by the previous check). |
| announce_reliable | 1.1.2: cut reply header token at 20 chars passed kTokenMin and would have been saved. | 'a board on the published default posts nothing' (wait_post(0,8) is None) passes vacuously if the restarted copy never came up (copy_log's result is not checked); 'and nothing is posted' likewise relies on the board being alive; CONFIG rows reached by DOWN*9 counting. |
| time_warn_in_plugins | 1.1.2, found on TRA: Rob was cut off mid-chat at his call limit with no warning. | Assumes the harness call limit is 60 minutes (TIME -56 leaves four); forums half runs only with a card. |
| lag_screens | 1.1.1 bench: SCREENS held the loop 1.22-1.28 s on the ESP32-CAM. | 'with no pass over 50 ms' is `not slow` over copy_text(), which returns '' if host.log is missing, so it passes vacuously; nothing confirms hostio.txt took effect (no 'log' word, no open counted). |
| lag_files | 1.1.1 audit: each row reopened the folder and searched FILES.BBS, hundreds of opens per page. | Same vacuous `not slow` if host.log is absent; uses 'files 5' from the copied harness config, and the comment admits an earlier test may rename area 5 (copy_data copies the live board, so order leaks in); not in REALTIME though it times real microseconds. |
| lag_forums | 1.1.1 audit: subject list walked the whole index in one pass; next message reopened INDEX.TXT per record. | Same vacuous `not slow` if host.log is absent; not in REALTIME. |
| forums_segments | 1.2.1: every post opened M0000 and every full segment after it, opens growing with each 128 KB. | Exact open counts (o2 == 1, o3 == 2) are brittle to any added open on that path, by design. |
| forums_long_read | 1.2.1: bodies over 1,728 bytes were cut (stack buffer, one-pass draw into 3 KB timeline). | Final slow-pass check is vacuous if host.log is missing; INDEX.TXT offsets patched by hand at fixed byte positions (128+56..67), coupled to the record layout. |
| forums_header_rebuild | Code review 1.2.1-forums.1: rebuild set count = newest, forgetting removals, so removed messages showed unread. |  |
| forums_seg_range | Code review 1.2.1-forums.2: seg=34463 sent the body to M34463.TXT and wrote 3446 into the record. |  |
| forums_page_fit | EOM and header rows drew two lines per pager row, so a 24-row page scrolled an unread line off (forums.2 review) | 'note is said, on one line' half-asserts len(note)<=39 on the test's own constant; message 7 (0 lines) makes 'every line, in order, once' and 'no page over 23' trivially true |
| lag_logins | unknown handle walked users.txt on the loop; logoff rewrote all of users.txt for four figures | 'without users.txt being read' is ==0 and would pass if the hostio log never switched on (the later callstats r+b>=1 check is the only proof logging worked); 'a board with N accounts' checks the test's own setup |
| lag_login_calls | S3 bench: every login a 64-94 ms slow pass (three account reads, 1,000 SHA rounds, motd probes in one pass) | 'the caller log is not read at a login' is ==0 (vacuous if hostio logging is off), but the users.txt ==1 check beside it proves logging |
| lag_logoff_calls | caller-log append seeked mid-write (two LittleFS block copies) plus users.txt re-read: 55-186 ms logoffs |  |
| lag_last_calls | LAST opened the caller log once per row past five: 70-88 ms passes | 'caller log opened no more than once a page' is an upper bound that passes with 0 opens if hostio logging is off |
| lag_announce_calls | Freenove: each announce round 60-285 ms, one caller-log open per record | 'caller log opened once a count' is <=2 and passes with 0 opens if logging is off |
| mail_in_place | every mail send/keep/delete copied all 64 records through mail.dat.tmp |  |
| config_one_pass | each CONFIG field searched system.cfg from the top: 14 opens per plugin page | both open-count checks are upper bounds (n<=2) with no positive check, so they pass vacuously if the hostio log is not active |
| space_kept | MEM/SYS/DASH and the plugin write guard each walked LittleFS for free space: ~170 ms a figure |  |
| lag_backup_get | GET /backup.zip CRC-scanned every file in one pass before the 200: seconds of stall |  |
| uploads_pending_bbs | staging .pending/FILES.BBS listed by P as an upload; A would move it over the area's descriptions | both checks are negatives ('FILES.BBS' / 'UPLOADS.BBS' not in output) with no positive check of what P printed, so an empty or failed P passes; the pending-count side of the bug is untested |
| camera_one_at_a_time |  | always SKIPs on the reference board (needs --board fncam/espcam --card); races two snapshots 50 ms apart on wall-clock timing |
| announce_badges | SD UNMOUNT kept forums/files badges; chat start() never reset mail_slots; an oversized payload was printed as sent; 'Mental Health' sent as mentalhealth | BADGE_TO_SUPPORT is DOWN*12 (row counting; CLAUDE.md now prefers cfg_walk_to); rewrites the live harness system.cfg and relies on announce_restore |
| announce_directory |  | default repo path is ROOT.parent.parent/unleashed_directory (a sibling of the repo's PARENT, /home/unleashed_directory here), so it SKIPs unless BBS_DIRECTORY_REPO is set; 'stored system as host' fails on any board profile (no 'host · S3 x' branch, unlike test_announce_badges); pins the directory's badge codes and HTML class names, so a site change fails firmware tests |
| files | erase dropped the caller to the shell (Rob); Screens area empty until card seeded; menu drew 4 columns where 3 fit | 'a number with no area is refused in the same words' sends 9, which IS an area (the built-in Screens, staff-only), so it repeats the hidden-area case and the true no-area number is untested; 'Enter opens the highlighted area' goes DOWN then UP back to area 1, so it cannot catch the draw/keys column disagreement it was written for; several negatives on rendered menus |
| binary | telnet CR normalisation dropped 0x0A/0x00 after 0x0D and IAC doubling would corrupt any file |  |
| xfer | board never negotiated BINARY; UPLOAD tagged CF_WRITE made per-area upload levels unreachable | 'the listing numbers the files' is b' 1 ' in output, which almost any screen matches |
| upload_no_binary | board stopped stripping NVT CR-NUL on its own BINARY request; SyncTERM uploads NAKed every block |  |
| ymodem |  | 'block 0's size trims the padding back' is got[:size]==body, identical to 'the bytes are right' once size==len(body) |
| dash_uploads |  | count check is >=1 and pending uploads left by test_ymodem/test_upload_no_binary already satisfy it; leaves DASHWAIT.BIN pending for later tests |
| mail_never_lost | a new message REPLACED the recipient's unread one and told the sender '(replacing ...)' | 'nothing was said about replacing' / 'still nothing about replacing' are negatives for a string that no longer exists in src |
| mail_rsd | reading a message deleted it at once, so a dropped line or page lost it | 'a kept message does not ring at login' is a negative read straight after ansi_login returns, so a late or absent notice passes; docstring is stale (says temp-file rename; 1.1.2 writes in place) |
| list_abort_returns | Q at [More] in a plugin listing left the caller at 'Stopped.' with no prompt (0.19.1) | two SKIP paths return after the first check (listing did not page, could not elevate) and report pass; relies on area 9 holding >1 page of seeded screens |
| shutdown | countdown loop broke on the first already-said threshold, so only the first warning went out | 'the board is still answering after a cancel' is always True: Caller has no sock_alive, so the hasattr fallback passes; on the 4x fast clock only ~2.5 real s separate 'in 10 seconds' from the end, so a slow cancel would take down the shared harness board |
| staff_remembered |  | the security property (bound to the address) is never exercised: every call is from 127.0.0.1; sysop-never-remembered and week expiry untested; reuses handle Keeper2 that test_xfer elevates to sysop (order coupling); DOWN*7 row count to Level |
| rename_follows | rename left unread mail unfindable under the old name and a room ban escapable | docstring names the room-ban escape but no check covers the ban following the rename |
| sd | a card .asc beat the flash .ans (directory-major lookup); unmount under an open card screen left a stale descriptor | 'it names the mount point' is b'/' in output (near vacuous); with no PASSWORD it sends 'bye \r' and logs off |
| partitions |  | the wipe half is tautological: the test's own loop skips user/ and logs/, so 'does not touch the accounts/config/plugin files' only proves the test's loop; 'announce section on the protected side' ('[plugin:announce]' in cfg or 'announce' not in cfg) cannot fail meaningfully; it deletes the live harness board's screens and restores only top-level files |
| refresh_and_ctrl_l | µ sent byte-wise through the charset map showed '??nleashed'; Ctrl-L never worked | 'the refresh header keeps the micro sign' is vacuous: DASH no longer names the board (docstring says so); elevates with hardcoded 'bye testsysop' instead of PASSWORD; no HOST guard |
| screens |  | 5 checks pin screen copy ('NO HATE', 'NOTHING HERE IS ENCRYPTED', 'YOU ARE ON THE BOARD') that the 1.2.2 screen redesign rewrites; 'does not say there is no private mode' and 'returning caller does not see it again' are negatives |
| operator |  | wrapper has 0 checks (body is operator_body, ~33 checks); 'and is not rung' is a negative after a 1 s pump; 'as fast' compares real seconds (<3) |
| operator_ends |  | the guest-in-a-shut-room block (3 checks) is skipped silently, with no check or SKIP line, if the regex finds no 'write = all' under [plugin:chat]; 'with no one-key question in the room' is a negative |
| notices_in_places | pages, broadcasts and SHUTDOWN waited for the main prompt; a room caller was hung up by SHUTDOWN unwarned | 'does not announce a threshold it has already passed' only asserts 'in 120 seconds' is absent from a 60 s countdown, which a correct cadence never prints anyway |
| ssh_login |  | SKIPs on the reference board (needs --board s3 or another SSH profile); 'never not securely' is a negative |
| ssh_new_caller |  | SKIPs on the reference board (SSH profiles only) |
| ssh_resize |  | SKIPs on the reference WROOM profile (ssh_ready needs --board s3 or another SSH board). |
| ssh_host_keys |  | SKIPs on the reference WROOM profile (ssh_ready needs --board s3 or another SSH board). 'and carries no host key' passes vacuously if the zip fetch failed (names=[]), though the check before it would fail. |
| ssh_telnet_unchanged |  | SKIPs on the reference WROOM profile (ssh_ready needs --board s3 or another SSH board). Timing check board_secs(0.25)<=first_probe<1.5 is wall-clock sensitive under load. |
| ssh_full |  | SKIPs on the reference WROOM profile (ssh_ready needs --board s3 or another SSH board). |
| ssh_failed_logins | Ban bypass via a later 'none' auth and per-wrong counting (1.1.2 code review MEDIUM). | SKIPs on the reference WROOM profile (ssh_ready needs --board s3 or another SSH board). All verdicts read off host.log strings (copy_log), so a reworded console line fails it rather than a behaviour change. |
| ssh_ymodem |  | SKIPs on the reference WROOM profile (ssh_ready needs --board s3 or another SSH board). Also SKIPs without a card. |
| ssh_dedicated_port | Default ssh_port 6422 clashing with an upgraded board's backup_port 6422 made every later reload/restore refuse the file. | SKIPs on the reference WROOM profile (ssh_ready needs --board s3 or another SSH board). Also SKIPs with no ssh_port in system.cfg. 'within its own 12 s, without hanging' asserts took<20 right after wait_closed(20), so it is near-vacuous (does not test 12 s). |
| ssh_socket_budget |  | SKIPs on the reference WROOM profile (ssh_ready needs --board s3 or another SSH board). |
| boot_hold_write_fails | syscfg::write removed system.cfg before renaming, so a failed rename left no settings at all. |  |
| boot_hold_factory_fails | A failed factory erase restarted with no note, reading as a plain software restart. | Always SKIPs when the suite runs as root (geteuid()==0), which includes the cloud containers (/root): never exercised there. |
| sysop_spelled_default | 1.0.0/1.0.1 restores wrote 'sysop_password = unleashed', making the published password work from anywhere and listed. | Mutates global PORT for the duration (restored in finally); an exception between assignment and try would leak it, but the try starts right after. |
| config_pin_exists | CONFIG took GPIO 20/24/28-31, the driver refused them at start, and the plugin just 'would not start'. | Per-profile pins come from PB['missing']; backup-button loop and drive pin repeat the same rule (logic, not wiring). |
| user_admin_retire | The user manager's D asked 'Delete handle (y/N)?' while it retires (since 0.19.0). | 'and never says delete' is a negative string check; protected only because the positive check before it reads the same buffer. |
| files_typed_number | Area menu took each digit as a key, so Photos (12)/Timelapse (13) were unreachable by number; review: Logs (10) counted as longer. | The co-sysop branch prints SKIP mid-test and returns ok when the board shows the co-sysop an area past 10 (camera boards) or has no CO1: half the test silently absent there. Needs a card or SKIPs. |
| backups_area | BACKUP SD put a zip on the card with no way to fetch it over the line. | Comment says '1 opens area 1 the moment it is pressed, so 11 cannot be two keys' (stale since 1.2.1 startsMore); harmless but misleading. Restores the harness board itself (order-dependent: listed before destructive tests). |
| card_screens_manifest | Unleashed HQ's welcome seeded before the manifest never refreshed after 1.0.0; imported screens taken back by the stock rule. | '(the 0.18.0 welcome.asc, rebuilt)' rebuilds the old file from today's stock welcome.asc by exact tail and licence string and checks fnv1a==0x62b7208f: any edit to the stock welcome (the planned 1.2.2 screen redraw) fails it and silently skips the whole pre-manifest block via 'if old:'. |
| restore_checks | Space check was one file short at the swap peak; empty sysop password restorable; XMODEM-padded zip unreadable. |  |
| restore_staff_report | Co-sysop left off by a restore was said only on serial; a pre-login caller lost to the shell with no prompt when the card went. |  |
| restore_ends_screens | esp_littlefs refuses rename/remove of an open file (EBUSY), so a paused reader made the restore end 'with errors'. | Docstring admits the host renames over the open file anyway, so the EBUSY failure it was written for is not reproduced; it only checks the notice. |
| sd_no_reprobe | The Rusty Antenna, no card: 541 slow passes in 24 min from the sd plugin re-probing on every CONFIG save. | Counts a host.log line; on the host a probe costs nothing, so it proves the call count, not the stall. Pin move uses PB['sd_move'] per profile. |
| rewrites_keep_old | Rewrites removed the live file before renaming the temp over it; a failed rename lost all mail/descriptions/a page. |  |
| restore_waits_quiet | Rob watched TRA hang hard restoring 38 screens with callers on. | Real waits (hold 12 s via BBS_RESTORE_HOLD_MS, waits up to 25 s) make it slow; timer is board time, check under the fast clock. |
| screens_command |  | Order-sensitive in the past: failed after test_forums_segments from serviceWait's unsigned 'late' (fixed 1.2.1-dev.9). Card branch vs no-card branch assert different strings. |
| screens_install | Pulling the card lost a sysop's own screens (Rob, 1.1.1). | Needs a card, the sysop and CO1 (co-sysop check sends 'bye {CO1}' without checking CO1 is set). |
| forms_wide | Every form was a 40-column card at every width. | Column positions asserted by index (led[22], px[22], save==15) break on any layout tweak; label strings are copy checks. |
| forms_narrow | ANSI hint was 39 chars on a 38-column status line. | Positional walks (DOWN*BOARD_LED, DOWN*4, DOWN*6) like those that broke test_board_mf35v2 in dev.12. |
| forms_ascii_wide | Plain ASCII could not empty a password field, so could never choose an open network (1.0.2 queue). | 'the test board's password is back' checks the test's own cleanup, not the board. Depends on harness values Test#Net and 'pa#ss word1'. |
| whois_wide | WHOIS drew a 39-column card at every width. |  |
| config_serial_rows | Serial bridge declared no settings: page showed raw keys cut to nine and no pin rows. | Branches heavily on PB per profile; positional DOWN*4/DOWN*6 walks. Has already shipped a wrong expectation once (dev.6 split on dots). |
| config_chat_colours | Chat declared no settings, so color_room/color_private were never reachable on the page. | Walks rows by count (DOWN*10, DOWN*6): a new chat setting row silently moves it to the wrong field (the dev.12 test_board_mf35v2 shape); use cfg_walk_to. |
| config_announce_desc | CONFIG's buffer held 95 characters, cutting the description on save. | Walks by count (DOWN*5, 'Board is read-only' in the comment); a new row moves it. |
| config_pin_holders | LED, SD, serial and lights could all be given one GPIO, each saying 'Saved and live'. | Several halves pin_skip per profile; on profiles without PB['serial'] it returns early, so the 40-column check never runs there. |
| config_forums_grow | Only four topic rows offered against sixteen read; a packed topic over 95 chars lost its levels on save. | Positional DOWN*6/DOWN*4 walks. |
| config_warn_levels | Brightness clamped at 30%; Rob wanted up to 100 with a warning past 30. | Plain ASCII walk sends six blank Enters to reach Drive % by position; 30/31/100/101 boundaries are unit cases. |
