<!--
 ===========================================================================
  µnleashed BBS
  Electronic freedom on a microcontroller.
 ===========================================================================

 File:         internal/test-speed-2026-09-26.md
 Module:       Working notes / testing

 Purpose:      What the host suite cost before and after the 1.1.2 test
               speed work (branch test-speed), where the time went, and
               what was proven not to have got weaker.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v3 or later
 SPDX-License-Identifier: GPL-3.0-or-later
 ===========================================================================
-->

# Test speed, 2026-09-26

Rob's rule: "test coverage is great but it shouldn't take up as long as the
code writing". The 1.1.2 scope asked for `--jobs`, a host-only fast clock, a
timing run naming the 20 slowest tests with their fixed sleeps replaced, and
each profile built once per run. Branch `test-speed`, from main at b278284.
No file under `src/` changed.

## The numbers

| Run | Wall clock | Tests | Checks |
|---|---:|---:|---:|
| Before: serial, no card (main 0f7fbb4) | 75.8 min | 170 | 1,500 pass, 0 fail |
| Before: serial, with a card, alongside it | 98.5 min | 170 | 1,863 pass, 37 fail (see below) |
| Before: both, one after the other | 174 min | | |
| After: `--jobs 24`, both modes, plus four `--fresh` boards and every profile | **5.1 min** | 180 x 2, and 30 on fresh and profile boards | 3,843 pass |
| After: the same on the real clock (`--no-fast`), before the SSH merge | 8.8 min | 170 x 2, and 8 | 3,611 pass |
| After: `--jobs 24 --solo`, every test on a board of its own | 4.9 min | 340 | |
| After: `--jobs 12 --only=messaging`, both modes (while a full run was going) | 2.2 min | 33 x 2 | |
| After: `--jobs 24 --only=ssh,board_s3`, both modes | 1.5 min | 12 x 2 | |

The full `--jobs` run does more than the serial gate did. The serial run
SKIPped the four `--fresh` tests and every profile test; the lanes give each
`--fresh` test a board of its own and run the S3 (with and without a card,
SSH included), Freenove and ESP32-CAM profiles on their own builds.

A patch's `--changed` run: every commit on main this week resolves to FULL,
because each touches `tools/testclient.py` or `src/config.h` beyond the
version line, so a patch's targeted run is the full 5 minutes. A change
confined to one subsystem is the group's time, 1.5 to 2.5 minutes.

The long pole is `test_announce_reliable` (275 s on the real clock, and it
must stay there, below), so no number of lanes brings the full run under
about 4.6 minutes. Making that test fast-clock clean is the next lever:
its heartbeat interval would have to be scaled by the clock factor so the
socket counts land between posts, and the "day of heartbeats" would then
run at plugin-tick speed, about 40 s instead of 150.

## Where the time went (before)

The 20 slowest tests, serial and real clock, with what each costs after. The
"card" column before is inflated for four tests by the cascade described
below (closed_configured, ban, space_kept, backup_published_default);
backups_area's 268 s was its own, the protocol clients' fixed waits. "Fixed waiting" is `time.sleep` plus `pump(secs)`
outside a wait loop, measured with `BBS_WAIT_STATS=1`.

| Test | Before, no card | Before, card | After, real clock (card) | After, --jobs lane (card) | Fixed waiting on the real clock |
|---|---:|---:|---:|---:|---:|
| test_announce_reliable | 275 | 275 | 274 | 274 | 264 |
| test_backups_area | (skips) | 268 | 73 | 51 | 28 |
| test_closed_configured | 45 | 179 | 45 | 25 | 16 |
| test_sysop_account | 137 | 137 | 137 | 60 | 25 |
| test_operator_ends | 134 | 134 | 134 | 67 | 38 |
| test_announce_join_refused | 127 | 127 | 126 | 126 | 110 |
| test_ban | 43 | 120 | 43 | 15 | 4 |
| test_lights_frames | 117 | 118 | 118 | 118 | 63 |
| test_lights_silent | 57 | 113 | 110 | 87 | 95 |
| test_accounts | 106 | 106 | 106 | 43 | 16 |
| test_operator | 106 | 106 | 106 | 55 | 30 |
| test_lights_count | 104 | 104 | 104 | 80 | 54 |
| test_space_kept | 23 | 101 | 23 | 12 | 9 |
| test_config_areas | (skips) | 96 | 96 | 61 | 36 |
| test_backup_published_default | 14 | 91 | 14 | 9 | 8 |
| test_notices_in_places | 64 | 71 | 71 | 38 | 26 |
| test_files | 10 | 70 | 70 | 53 | 37 |
| test_room_private | 69 | 69 | 69 | 41 | 26 |
| test_config_warn_levels | 66 | 66 | 66 | 41 | 31 |
| test_announce_join_prompt | 66 | 66 | 65 | 65 | 50 |

Summed over every test: 4,545 s no card and 5,911 s with a card before;
2,845 s and 3,307 s in the fast lanes after. On the real clock the fixed
waiting was 2,471 of 10,294 s, 24 %. The rest was the board's own time:
detection, the fx on every login and form, the five second goodbye linger,
the busy countdown. That is why the fast clock, not sleep hunting, is most
of the win per test, and the lanes are most of the win overall.

Per check (`BBS_CHECK_TIMES=1`), the slow stretches inside the top tests
were logins and registrations (6 to 10 s each on the real clock, about 2 s
fast), a hangup waiting out the linger (5 s, 1.25 s fast), and the
protocol clients below.

## What changed

- **`harness.sh --jobs N`** (`tools/parallel.py`). The selection (full,
  `--only`, `--tests`, or `--changed`) is split into lanes packed by each
  test's measured time (`tools/test-times.txt`), longest first, each lane an
  ordinary harness run with its own tag, port, data directory and card, run
  N at a time, with and without a card at once, merged into one verdict and
  one `out.txt`. Within a lane the tests keep ORDER_NAMES' order.
- **Isolation markers** in `tools/testclient.py`, read by `--plan`:
  `ALONE` (test_ban), `NEEDS` (the two order dependencies found),
  `REALTIME` (six tests that time real seconds), `FRESH_TESTS` and
  `PROFILE_TESTS` / `PROFILE_CARD`.
- **Ports.** A test starts copies at fixed offsets from the harness port,
  so two lanes can meet on a copy's port. Each worker gets a port chosen so
  none of its offsets is another worker's (27 fit in a block), and a run
  claims one of two blocks (11000-21499, 21500-31999) with a lock file, so
  two `--jobs` runs at once cannot meet. A lane's leftover boards are found
  by the `BBS_LANE` they inherit, never by port. The first version handed
  out ports per lane from 11000 for every run and reaped by port; two runs
  at once then killed each other's boards, which read as forty unrelated
  failures. That is the case the block lock exists for.
- **One build per run.** `--jobs` builds every profile it needs (and
  `ssh_call` for the S3) once, under a lock, and every lane runs with
  `--no-build`. Lanes used to be able to race `make` on the same binary.
- **The fast clock**, `BBS_FAST_TIMERS` (`--fast[=N]`, default in `--jobs`):
  `host/platform_host.cpp` runs `plat::millis`, `taskSleep`, `runWait` and the
  simulated camera at 4x the wall. `plat::micros` stays real, because slow
  passes and the lag tests are measured with it, and `time()` stays real.
  A test that times a board timer compares against `board_secs(s)`; a test
  that times something on the wall is in REALTIME and runs on a real lane.
- **Fixed waits replaced with waits for state.** The XMODEM/YMODEM clients
  waited a fixed 0.1 s for every ACK (`pump(0.1)`), which made a 42 KB
  upload take 35 s; they now return as data arrives (`Caller.pump_some`).
  `test_backups_area` went from 268 s (card, serial) to 73 s on the real
  clock. The harness waits for "listening on" instead of `sleep 1`.
  `wait_for` itself was tried with `pump_some` and reverted: twenty checks
  in the file areas lean on the tenth of a second of trailing output a
  `pump(0.1)` collects after the pattern, and the comment on `wait_for` now
  says so.
- **`--tests=a,b`** exact names, **`--list`** and **`--plan`** in the test
  client; **`TEST`/`TIME`** lines per test; `tools/testtimes.py` for the
  slowest report, the check list (`--checks`) and the kept figures
  (`--save`); `BBS_CHECK_TIMES=1` and `BBS_WAIT_STATS=1` for finding the slow
  stretch of a slow test.

## Order dependencies

The `--solo` run (every test alone, both modes) found exactly two tests that
lean on another's leftovers, both now in NEEDS:

- `test_cosysop` needs `test_sysop`: "a co-sysop cannot edit the sysop's
  account" edits Rob, whom test_sysop registers and elevates.
- `test_user_admin` needs `test_accounts`: "staff WHOIS shows private
  fields" reads Acct's email.

Everything else passes alone. A NEEDS entry is a record, not a fix: each
test should seed its own account.

## Test bugs the fast clock found

- `login()` returned early. The sign-up form's Start row says "Main", so the
  wait for "Main" matched the form, before the greeting and the newuser
  screen arrived, and a test that cleared its buffer and typed a command
  read the screen as its own output (test_ascii's HELP checks). It now
  counts only what arrived after ACCESS GRANTED or WELCOME ABOARD.
- Four checks timed board timers against the wall: the PETSCII detection
  timeout, the busy countdown, the goodbye linger and welcome's 300 baud line,
  plus the SSH settle. Each now measures against `board_secs()`, with
  `FAST_SLACK` (0.5 s on a fast board, nothing on a real one) for a loaded
  host.

## REALTIME, and why

| Test | Why it stays on the real clock |
|---|---|
| test_announce_join_prompt | sleeps 31 real seconds against the stand-in directory's 30 s |
| test_announce_join_in_flight | holds a post 9 s against the board's 10 s timeout |
| test_announce_join_refused | the retry must be 30 s or more later, measured by the directory |
| test_announce_reliable | counts sockets between heartbeats timed in board milliseconds |
| test_lights_frames | samples the strip's animation at real moments |
| test_lag_screens | hostio costs are real microseconds; the caller's wait is a board minute |

## Proof that nothing got weaker

- **Same checks.** Every check that passed in the serial baseline passes in
  the `--jobs` run, both modes (digits folded, `tools/testtimes.py --checks`
  on each side). The jobs run passes more: `test_announce_directory`'s 14
  (the baseline ran from a copy of the tree with no directory repository
  beside it, so it SKIPped) and, with a card, the 37 the baseline's cascade
  failed.
- **A broken build still fails.** With the permission check in
  `Bbs::cmdTimeAdjust` reverted (0.22.0's fix: the room's `/t` let any caller
  change anybody's time) in a scratch copy, main's serial harness fails
  `test_room_time_staff_only`'s "an ordinary caller cannot take themselves
  off the clock" and "nor give another node minutes", and nothing else;
  `--jobs 12 --only=messaging` fails exactly those two, with and without a
  card, and nothing else.
- **The board is byte for byte the same.** `pio run -e esp32dev` on b278284
  and on test-speed: all 49 application objects identical once debug info
  (which carries each worktree's path) is stripped. `firmware.bin` differs
  only in the app description's build time.

## Open, for the firmware lane

- **The serial card baseline failed 37 checks from `test_space_kept` on**,
  and none of them failed in any lane or alone. From 539855 in its host log,
  after `test_config_one_pass`'s sysop left, callers connected and were
  detected but never reached a login for seven minutes; the next login was
  test_backup's Alice. `test_config_one_pass,test_space_kept,
  test_uploads_pending_bbs` together on a card pass. The run shared the
  machine with other agents' harness runs, whose tag-derived ports sit in
  the same 6500-10899 range with the same offsets, so interference is
  possible and not proven. Worth one more serial card run on a quiet
  machine before 1.1.2 ships.
- **SCREENS under load.** `test_screens_command` once waited more than 8 s
  for the list in a 16-lane run, and did not again in six more full runs.
- `test_ssh_dedicated_port` binds an absolute 6422 on one copy. Two `--jobs`
  runs whose S3 lanes reach that test at the same moment would meet there.
