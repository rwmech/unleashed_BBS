<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 - Robert Mech -->

# Breaking the test suite into three axes

**Applies to versions:** firmware 1.2.1 and later. Written 2026-10-04.

Rob, 2026-10-04: "we need to break up all test plans by the following
1. ESP32 vs ESP32-S3, 2. Board Specific, 3. Feature Testing. The problem now
is that its haphazard and we need to isolate functional testing (feature
testing) outside the board specific updates. I see little reason to run
regression testing on say a WS43 if we did all the changes to the core code
for chat. Only functional testing needs to be performed there. So break up
the testing and agents by this and then for each release we'll focus only on
functional regression unless we've added new features that warrant a larger
expansion of testing."

## Why, in numbers

Measured on the v1.2.1 tree:

| | Count |
|---|---|
| Tests in `tools/testclient.py` | 214 |
| Board tests by name (`test_board_*`) | 11 |
| Tests reading a board fact (a pin, a profile, a panel) | 38 |
| So feature tests carrying a board assumption | about 27 |
| Tests inside any `GROUPS` entry | 99 |
| Tests reachable only by their own name | about 115 |

Two consequences, and both bit us in 1.2.1:

- **A shared test that assumes the reference board fails on a board that is
  merely different.** Every board failure in 1.2.1 was this, not a bug: 60 on
  the 4.3B (the lights tests using the WROOM's pins), 3 on the ETH board (DASH
  and SYS wanting Wi-Fi where that board shows Ethernet), the card tests
  wanting the WROOM's SD pins, 12 on the Makerfabs v2.0 (a test counting
  keypresses down a menu whose rows had moved).
- **`--only=<group>` is not a subset of the suite, it is a different set.**
  Over half the tests are in no group, so a targeted run silently skips them,
  and `src/core/bbs.cpp` maps to the full suite because nothing finer exists.

## The three axes

Every test belongs to exactly one axis. The axis is declared in one table, so
a test cannot drift between them silently.

### 1. Feature (functional)

What a caller or a sysop can do: logging in, chat, mail, forums, files,
transfers, CONFIG, the shell, lists, screens, bans, the backup window, sats,
doors, photos as a system.

- Runs on the **reference host build only** (`bbs_host`, the WROOM profile).
- **May not read a board fact.** No pin numbers, no panel, no camera sensor,
  no network interface, no board version. A feature test that needs one is
  wrong: either the fact is incidental and comes from a table, or the test
  belongs on axis 3.
- Keeps the two orthogonal dimensions that are about features rather than
  boards: **with a card and without** (file areas, forums and photos only
  exist with one), and a **fresh board** for first boot and setup.
- This is the regression that runs at every release.

### 2. Chip (ESP32 vs ESP32-S3)

What differs by chip family rather than by board:

- the usable GPIO range (`BBS_GPIO_OUT_MAX`, 39 against 48) and which pins
  are refused by name;
- the console pins (1/3 against 43/44) and whether the console is a UART or
  native USB;
- PSRAM, the partition layout and the erase-below rule (4 MB against 8 MB);
- SSH's presence at all, and the socket budget that follows it;
- the VFS slot table, newlib nano, 240 MHz;
- anything in `src/platform/platform_esp32*.cpp` or the shared
  `sdkconfig.defaults*` layers.

Runs on **one representative of each family**: `bbs_host` and `bbs_host_s3`.
Not on every board.

### 3. Board specific

One suite per profile, and only that board's own facts: its pin table and
what it refuses by name, its panel and that panel's layout, its card bus, its
camera and sensor, its lights, its buttons, the version string it reports.

Runs **only when that board's own code changes**, and on the bench when the
board is plugged in. Ten profiles today: esp32 (reference), s3, fncam,
espcam, ws43b, ws2, wseth, mf35, mf35v2, g4848.

## Selection: what a change runs

`tools/changed_groups.py`'s table is rewritten to map a path to an axis
rather than to a list of group names:

| Changed | Runs |
|---|---|
| `src/core/*`, `src/plugins/*` (not a board block) | feature |
| `src/platform/platform_esp32.cpp`, `platform_host.cpp` | feature + chip |
| `src/platform/platform_esp32_rgb.cpp`, `_st7701.cpp` | chip + the boards that use it |
| `sdkconfig.defaults`, `sdkconfig.defaults.esp32s3` | chip |
| a board's block in `src/board.h`, `sdkconfig.defaults.<board>` | that board only |
| `src/config.h`, version line only | nothing |
| `src/config.h`, anything else | feature + chip |
| `tools/testclient.py`, `tools/harness.sh`, `tools/parallel.py` | the axis of the tests touched |

**The fallback stays loud, not silent:** a path that matches nothing runs
everything and names the file, as it does today.

## What a release runs

Rob's rule: **functional regression only**, unless new features warrant more.

- Always: the feature axis, with a card and without, plus the fresh boards.
- When a chip layer moved: the chip axis on both families.
- When a board's own code moved: that board's suite, and nothing for the
  other nine.
- On the bench: a smoke check on whatever boards are plugged in, which is
  bring-up rather than testing.

## The agents

Three runners instead of one, so a brief cannot wander across axes:

- **feature-tests**: the feature axis on the reference build. Knows nothing
  about pins or panels and is told not to look.
- **chip-tests**: the chip axis on both families.
- **board-tests**: one board at a time, given the profile name, its notes
  under `release-prep/<board>/` and its block in `board.h`.

## The work to get there

1. **Classify all 214 tests** into the three axes, in one table beside
   `ORDER_NAMES`. A test with no axis fails the suite rather than defaulting.
2. **Fix the about 27 feature tests that carry a board fact.** Each either
   reads the fact from a table or moves to axis 3. No silent fallback to the
   WROOM's values: a missing row skips the test loudly, which is how the ETH
   board ended up testing pins that are its own RGB data lines.
3. **Make the groups cover every test**, so a targeted run is a real subset.
   The 115 orphans get a group or an axis-only home.
4. **Rewrite `changed_groups.py`** to the table above.
5. **`tools/harness.sh --axis feature|chip|board` and `--board <profile>`**,
   with `--jobs` unchanged underneath.
6. **Three agent briefs** in `.claude/agents/`.

## Settled by Rob, 2026-10-04

- **Board suites at a release: only the boards whose own code changed**, plus
  a bench smoke on whatever is plugged in. A change to core chat runs no board
  suite at all, which is the whole point of the split.
- **The feature axis runs on the reference build, and on the S3 build only for
  features that are S3-only** (SSH, the panel). Core behaviour is
  chip-independent by design, so running the whole feature suite twice buys
  little and doubles the clock. If a core feature ever turns out to behave
  differently on the S3, that is a chip-axis test, not a reason to run
  everything twice.
