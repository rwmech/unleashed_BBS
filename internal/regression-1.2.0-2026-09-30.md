# Full host regression, 1.2.0 (2026-09-30)

- **Commit tested:** `v1.2.0`, cc63c5f9fcb7248ab63aeaecac2c3c3af2d37e5b
  ("release.py: its own docstring tripped the notice check").
- **Camera satellite:** unleashed_camsat `v1.1.0`, 2dbbf54, whose
  `core.lock` pins the same cc63c5f.
- **Where:** a claude.ai cloud session. Ubuntu 24.04, g++ 13.3.0, Python 3,
  4 cores, 15 GB. Running as root (matters for one skip, below).
- **mbedTLS:** 3.6.0 from github.com/Mbed-TLS/mbedtls, tag v3.6.0, with the
  `framework` submodule, as `MBEDTLS_DIR=/opt/mbedtls`.
- Pull request: rwmech/unleashed_BBS#3, progress in its comments.

## Verdict

No firmware bug found. 6,475 checks passed across the five runs, and 4
failed. All four are test-side problems: two are timing on the host's 4x
fast clock, one is a stale line the test matches, and one is an order
dependency between groups. Each passes when rerun alone on the real clock.

| Run | Command | Passed | Failed |
|---|---|---|---|
| 1 | `make test` in `host/` | 1,226 checks in 18 programs, plus formats and licence | 0 |
| 2 | `tools/harness.sh --jobs 4 --tag reg` | 4,942 | 3 |
| 3 | `tools/harness.sh --tag regsat --ext camsat --card --only=sats,radio` | 122 | 1 |
| 4 | `test_link` under ASan and UBSan | 179 | 0 |
| 5 | `tools/lrzsz_check.py`, 6 ways | 6 of 6 transfers | 0 |

## Run 1: `make test`

`make test MBEDTLS_DIR=/opt/mbedtls` in `host/`, 13 s. Every program ends
with 0 failed:

- test_link 179, test_skin 218 and test_xmodem 162
- test_wrap 11, test_forums_ptr 22, test_compose 48 and test_codes 17
- test_improv 39, test_recovery 44, test_ring 51 and test_tzones 24
- test_cardnames 27, test_panel 161, test_calllog 20 and test_silent 36
- test_camera 139, test_photos_cfg 14 and test_sshlink 14

That is 1,226 checks. `formats self-test: 22 cases pass` and `formats: 130
files, no format newlib nano cannot print`. `licence: no GPL-2.0 SPDX lines`.

## Run 2: the full suite

`MBEDTLS_DIR=/opt/mbedtls bash tools/harness.sh --jobs 4 --tag reg`. It ran
199 tests in 34 lanes, 4 at a time, with and without a card, on a fast
clock at 4x. It took 35.5 minutes of wall clock and 8,325 s of lanes. The
load average stayed under 1, so a larger `--jobs` would have been safe.

| Mode | Passed | Failed | Tests |
|---|---|---|---|
| no card | 1,637 | 0 | 199 |
| card | 2,036 | 1 | 199 |
| no card fresh | 84 | 0 | 4 |
| no card / card s3 | 147 / 183 | 0 / 0 | 13 / 13 |
| no card / card ws43b | 104 / 114 | 0 / 0 | 11 / 11 |
| no card / card ws2 | 100 / 109 | 1 / 1 | 11 / 11 |
| no card / card wseth | 91 / 100 | 0 / 0 | 11 / 11 |
| no card / card mf35 | 100 / 109 | 0 / 0 | 11 / 11 |
| no card fncam | 23 | 0 | 1 |
| no card espcam | 5 | 0 | 1 |
| **total** | **4,942** | **3** | |

84 SKIPs, all expected from the lane layout (a profile or card test on a
lane without one), except the two under "What could not run".

### Failure 1: `test_doors`, "and the door opens again after" (card lane `reg-c3`)

- **The check:** `tools/testclient.py:2078`,
  `c.wait_for(b"CLOCK DOOR", 10)` after `doors 2`, once CONFIG link has been
  saved twice.
- **Alone:** the test needs `test_radio_link` first (NEEDS), so it was run
  as `--tests=test_radio_link,test_doors`.
  - Real clock: 4 of 4 pass, 44 checks each.
  - Fast clock (`--fast`): 2 of 3 pass. The failure reproduces
    intermittently.
  - The same test passed in the suite's no-card lane.
- **Cause: a race in the test, not the board.**
  - `peer.lines.clear()` runs once, at `testclient.py:2059`, before the
    first of two CONFIG link saves.
  - Each save restarts the link, and linkpeer prints `link up` after each
    one.
  - `LinkPeer.wait()` (`testclient.py:1874`) returns the first matching
    line anywhere in `lines`. So `peer.wait("link up", 10)` at
    `testclient.py:2075` is met at once by the first restart's line.
  - The test then relies on its fixed `time.sleep(1.5)` for the second
    restart's door list.
  - In the failing solo run, the sysop hung up at 3002.450, and the board
    logged `link: door "shelf" is up` at 3004.093, 1.74 s after the second
    restart at 3002.353. So `doors 2` went in before the door list had
    come back.
- **Suggested fix (test only):**
  - clear `peer.lines` again before the second save, or wait for the second
    `link up`;
  - then wait on `DOORS` listing the door rather than a fixed sleep.

### Failures 2 and 3: `test_board_ws2`, "a tap turns the header to its next page" (both ws2 lanes)

- **The check:** `tools/testclient.py:7951`. It reads the header slot's
  page, sends `panel tap`, waits for `Tapped`, pumps 0.3 s, reads again and
  expects the next page. It tries three times.
- **Alone:**
  - Real clock: 5 of 5 pass (3 without a card, 2 with), 25 checks each.
  - Fast clock: 4 of 6 pass. The failure reproduces intermittently.
- **Cause: the test's timing on the fast clock, not the panel.**
  - The slot turns by itself every `kHoldMs` 3,000 ms plus a 16-step fade
    of `kStepMs` 60 ms (`src/plugins/panel.cpp:218-227`, turn logic at
    ~718-737). That is about 4 s, and at 4x about 1 real second.
  - Each attempt spans two panel reads, the `Tapped` round trip and 0.3 s.
    So a turn of the board's own often lands between the first read and
    the tap, and the tap then moves one page further than the test expects.
    Three tries do not always escape it.
  - `test_board_ws43b` exercises the same panel code and is already in
    `REALTIME` (`testclient.py:~20740`) for this reason. `test_board_ws2` is
    not.
- **Suggested fix (test only):** add `test_board_ws2` to `REALTIME` with
  the same reason as the 4.3B.

## Run 3: the camera satellite end to end

- **First attempt:**
  `MBEDTLS_DIR=/opt/mbedtls bash tools/harness.sh --tag regsat --ext camsat --card --only=sats,radio`
  stopped with `harness: ext/camsat is missing: tools/plugins.py fetch camsat`.
- **Why:** `python3 tools/plugins.py fetch camsat` answers
  `plugins: camsat is not in plugins.lock`. The camsat line in
  `plugins.lock` is still commented out ("once unleashed_camsat has its
  first commit"), although camsat has shipped v1.1.0.
- **Workaround, as the brief allows:** cloned unleashed_camsat into
  `ext/camsat` at `v1.1.0` (2dbbf54, whose core pin is cc63c5f), then ran
  the same command. 4 min 19 s.
- **Result:** 122 passed, 1 failed. It ran test_radio_link, test_doors,
  test_doors_petscii, test_link_shared and test_sats.

### Failure 4: `test_sats`, "the sat's log on the card names the failure and who asked"

- **The check:** `tools/testclient.py:2366-2368`. It looks for
  `\tsat <n> shelf\tthe satellite's queue is full\tSatWatcher` in
  `logs/camsat-<n>-errors.log` on the card.
- **What the card held:** the right lines, under a different name:

  ```
  2026-09-30 17:32:25	sat 1 shelf-2	the satellite's queue is full	SatWatcher
  2026-09-30 17:32:26	sat 1 shelf-2	the satellite's queue is full	SatWatcher40
  ```

- **Alone:** `--tests=test_sats` with `--ext camsat --card` gives 50
  passed, 0 failed, and the log names the sat `shelf`.
- **Cause: an order dependency between the two groups.**
  - The radio group runs first and leaves a door box paired as `shelf`.
  - The camera sat then pairs under the same wanted name, and
    `uniqueName` (`src/plugins/link.cpp:445-461`) makes it `shelf-2`, which
    is the board doing what it should.
  - The test takes the sat's number from the board but hard-codes the
    name.
- **Suggested fix (test only):** read the sat's name from SATS the way
  its number is read, or give the camera sat a name of its own.
- Not in the full suite, where `test_sats` SKIPs without `--ext camsat`,
  so this combination is the only place it runs.

## Run 4: the link under the sanitizers

The Makefile has no sanitizer target for `test_link`: `SAN=1` only changes
flags, and the existing binary counts as up to date. So it was built on
its own with `SAN=1`'s flags, into `/tmp`, leaving `host/` alone:

```
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I../src -DBBS_HOST -I/opt/mbedtls/include -Iext-gen \
    -o /tmp/test_link_san test_link.cpp ../src/core/link.cpp ../src/core/linkcrypto.cpp \
    libmbedtls_host.a -lpthread
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 /tmp/test_link_san
```

- **Result:** 179 passed, 0 failed, exit 0. No `runtime error`, no
  AddressSanitizer report and no leaks.
- mbedTLS itself was the ordinary `-O2` library, not sanitized.

## Run 5: lrzsz

`python3 tools/lrzsz_check.py` cannot run on its own. It needs a board
already listening with the harness's card and its Drop Box (area 5):
`BBS_SD_DIR=<card> lrzsz_check.py <port> [...]`.

- **The board:** `harness.sh --tag lrz --card --no-build
  --tests=test_about` built the data folder and the card (2 passed). Then
  `bbs_host` was started on that data on port 6777, with
  `BBS_SD_DIR=/tmp/bbs-lrz/card`.

| Arguments | Protocol | Binary | Result |
|---|---|---|---|
| (none) | XMODEM | agreed | PASS, 1,152 received for 1,026 sent (padding, as designed) |
| `--refuse-binary` | XMODEM | refused | PASS, 1,152 / 1,026 |
| `--ymodem` | YMODEM | agreed | PASS, 1,026 / 1,026 exact |
| `--ymodem --refuse-binary` | YMODEM | refused | PASS, 1,026 / 1,026 |
| `--delay 20` | XMODEM | agreed | PASS (the 17 s file-dialog case) |
| `--ymodem --delay 20` | YMODEM | agreed | PASS |

## Slowest 20 tests

`python3 tools/testtimes.py /tmp/bbs-reg/out.txt` reported 518 test runs,
8,312 s in tests (138.5 min), 4,942 passed and 3 failed. A name appears
twice when it ran in both card modes.

```
   seconds   share  test
     276.7    3.3%  test_announce_reliable
     276.2    3.3%  test_announce_reliable
     126.7    1.5%  test_announce_join_refused
     126.7    1.5%  test_announce_join_refused
     118.6    1.4%  test_lights_frames
     117.2    1.4%  test_lights_frames
     100.9    1.2%  test_board_ws43b
      99.3    1.2%  test_board_ws43b
      88.6    1.1%  test_board_s3_silent
      80.2    1.0%  test_lights_count
      80.2    1.0%  test_lights_count
      76.3    0.9%  test_board_s3
      76.0    0.9%  test_board_s3
      74.5    0.9%  test_board_s3_silent
      70.4    0.8%  test_lights_silent
      67.7    0.8%  test_operator_ends
      67.7    0.8%  test_operator_ends
      65.0    0.8%  test_announce_join_prompt
      64.9    0.8%  test_announce_join_prompt
      64.8    0.8%  test_board_s3_skin
```

## What could not run, and other notes

- **`test_announce_directory` SKIPped in both modes:** "no directory
  repository at /home/unleashed_directory". The directory repository is not
  in this session's scope, so the check against the real directory server
  did not run.
- **`test_boot_hold_factory_fails` SKIPped in both modes:** "root ignores
  the permission that makes the erase fail". The cloud session runs as
  root. It needs a non-root user to run.
- **camsat is not in `plugins.lock`**, so `--ext camsat` cannot fetch it
  (Run 3). The comment there predates camsat's first release; with v1.1.0
  out, the line can be pinned.
- **`tools/*.sh` and `tools/*.py` are mode 100644 in git**, so
  `tools/harness.sh` gives "Permission denied" on a fresh Linux clone. All
  runs used `bash tools/harness.sh`. This is the same Windows
  executable-bit issue the site cutover hit.
- **Two harness runs building at once can collide.** When I started two
  plain (non `--jobs`) runs with builds at the same moment, one failed in
  make with `mv: cannot stat 'ext-gen/ext_plugins.h.new'`: both write the
  same temp name in `host/ext-gen/`. `--jobs` builds once, so the suite
  itself is not exposed. It was my doing, reran with `--no-build`, and is
  noted only because CLAUDE.md promises tags isolate runs.
- **New warnings from GCC 13.3** (the WSL builds use an older GCC). None
  shows a bug; each is bounded by `snprintf` or harmless.
  - `src/plugins/skin_draw.h:704`: `-Wclass-memaccess`, a `memset` of
    `skin::Rgb`, which has default member initializers. Harmless: it is
    all `uint8_t`.
  - `src/plugins/files.cpp:878`: `-Wformat-truncation` into `full[160]`.
  - `src/plugins/forums.cpp:2792`: `-Wformat-truncation` into `line[96]`.
  - `src/core/photos.cpp:659`: `-Wformat-truncation` into `sysFolder` (16).
- **ws2 and doors reproduce only on the fast clock**, never on the real
  one (9 real-clock reruns, all clean). A `--no-fast` full run would not
  have shown them, which is an argument for keeping the fast lanes.
