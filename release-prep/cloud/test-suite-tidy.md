# Cloud job: put the test suite into three tiers

Paste this into a claude.ai cloud session on the µnleashed BBS repo.

**Why cloud:** host-only work. The cloud environment cannot build firmware
images (PlatformIO's registry is blocked), but the host build is plain g++
and make, so `make test` in `host/` and `tools/harness.sh` both work.
Nothing here needs a board.

---

## The shape Rob wants (2026-10-06, his words)

> Tests SHOULD be as follows: Unit, Integration, Regression.
> This shouldnt be run regression on everything every fucking time. we need
> unit tests to make sure the fucntion works and integration into the BBS
> but we certainly dont need more than that without a good fucking reason.

And, on the size: "6000 fucking tests? this isnt nasa".

**The three tiers, and what belongs in each:**

- **Unit** — does the function work. Pure logic, no board, no socket, no
  filesystem. Milliseconds. Every edge case lives here.
- **Integration** — is it wired into the BBS. **One path per behaviour**,
  proving a caller can reach it and the plumbing is connected. Not every
  permutation.
- **Regression** — the whole suite, run at a version boundary, **not on
  every change**.

The rule that follows, and it is the whole job: **an edge case belongs in a
unit test, and an integration test exists only to prove the wiring.** Today
edge cases are tested by standing up a board and driving telnet, which is
why there are 2,718 assertions in a 23,096-line file.

## The diagnosis: the suite is inverted

Measured (verify these yourself, they may have moved):

- `tools/testclient.py`: **23,096 lines**, one file
- **216** `def test_` functions, all of which stand up a board
- **2,718** `check(` calls
- **21** host unit-test files, `host/test_*.cpp`
- 1.2.0's full regression: **6,475 checks executed**

So the pyramid is upside down: nearly everything is integration.

**The pattern already exists in this repo and just was not applied widely.**
These are pure headers with real unit tests and no board in them:

- `src/core/forums_ptr.h` → `host/test_forums_ptr.cpp` (the read pointer)
- `src/platform/platform.h`'s `plat::since` → `host/test_since.cpp`
- `src/plugins/panel_photo_fit.h` → `host/test_panel_photo.cpp`
- `src/core/compose.h`, `src/core/claims.h`, `bbsu::wrap`

Each was split out deliberately so the logic could be tested without a
board. That is the model. Find the logic still being tested through a telnet
session that could be tested this way instead.

## The job, in order

**1. Classify all 216.** For each: is it proving *logic* (belongs in unit)
or *wiring* (belongs in integration)? Produce the list before changing
anything.

**2. Where logic is being tested through a board**, say what it would take
to move it: is the logic already in a pure function, or would `src/` need a
header split? **Do not split `src/` yourself** — report it. A `src/` change
needs a firmware build to verify and cloud cannot build one.

**3. Where you can move a check into an existing unit test, do it**, and
delete the integration check it replaces — but only under rule A below.

**4. Collapse the per-profile duplication.** `PROFILE_TESTS`
(testclient.py ~22778) lists **the same 13 SSH tests verbatim for 7
profiles** — 91 runs of code gated by one define, `BBS_HAS_SSH`. Most cannot
differ per profile. **But some can**: `test_ssh_full`,
`test_ssh_socket_budget`, `test_ssh_lines` and `test_ssh_ten` exist because
each board has a different PSRAM budget and `sshd::cap()` computes a live
ceiling from it. Work out which genuinely need a profile, with evidence.

**5. Split the 23,096-line file** by subsystem, matching the existing
`--only=` groups. Keep `ORDER_NAMES` as the single source of run order and
prove the selected set is identical before and after.

**6. Make the tiers runnable separately**, so "unit" and "integration" are
things you can ask for, and regression is explicitly the version-boundary
run rather than the default.

## Rules

**A. Never delete a check without proving it cannot catch anything.**

This project has repeatedly shipped tests that agreed with a bug:

- `USER DEL` recycling a handle was *asserted as correct behaviour* while it
  was handing one person's undelivered mail to the next person to register
  that name
- `tools/testclient.py` itself hid two real telnet bugs for a release,
  because it was written beside the board and shared its assumptions
- a usability check could not see a doubled footer, because it only looked
  at the last line

**The test for deletion: does it fail on a commit where the bug existed?**
`git log` has the fixes; run the check against the parent of its own fix. If
it passes there it is coverage rather than a catch, and is a candidate. If
it fails there it earned its place, however redundant it reads.
`host/test_bans.cpp` already labels two of its own checks this way — follow
that convention, and have anything kept as coverage say so in a comment.

**B. Measure before cutting, and report the measurement.** Not by instinct.
First deliverable is: `check()` calls per test (worst 20 first), which of
the 216 run per profile versus everywhere, the real executed count and where
the multiplication comes from, and the 20 slowest (`tools/testtimes.py` and
`tools/test-times.txt` already hold per-test times).

**C. `src/` is out of scope.** Report what wants splitting; change nothing.

**D. Keep the `--only=` groups wider than the file being edited.** They are
deliberately broad because what breaks here is usually the subsystem next
door — file areas and forums share the list machinery, mail lives inside
chat. Do not narrow them.

**E. Cut hard on logic, and KEEP the handful that exercise an environment
difference.** Rob's standard (2026-10-06): "if the code you wrote is not
crap a unit test and integration test inside the BBS itself is enough to
catch 90% of issues". That is right, and the useful corollary is what the
other 10% actually were here. **This project's most expensive bugs were not
logic bugs and no amount of integration testing would have found them:**

- `esp_vfs_rename` returns EXDEV between two VFS mounts, so restoring a
  backup **deleted every account** from 0.14.0 to 1.0.2. Every host test
  passed, because the host is one filesystem. What found it was putting
  `data/user` on /dev/shm so the host refuses the rename the way the board
  does.
- IDF defaults FATFS to 8.3 names, so a nine-character folder is an
  *invalid name* on the board and fine on the host. Target-only, and the
  generated `sdkconfig` was the only place it was visible.
- `CONFIG_LWIP_MAX_SOCKETS` out of range does not warn, it **reverts**, so
  the board ran 10 sockets while advertising 16.
- SyncTERM commits to CRC-16 on the first `C`; the board gave up after nine
  seconds and dropped to checksum. lrzsz could not reproduce it because
  lrzsz is adaptive and followed the board down.

So the tests to protect are the ones that make the host behave like the
board, or that test against somebody else's implementation. There are maybe
a dozen and they are worth more than the rest combined. **When in doubt
about a test, ask which of the two it is**: our logic (cut it, unit test the
function instead) or the environment (keep it, and say in a comment why).

## What success looks like

Not a target number. A suite where a unit test covers the logic, one
integration test proves each behaviour is reachable from a caller, the small
environment-difference set is intact and labelled, the full regression is a
version-boundary event, and a person can find the tests for a subsystem
without grepping 23,000 lines.

## Deliverables

1. `internal/test-suite-audit-2026-10-06.md`: the measurements, the
   classification of all 216, what moved to unit, what was deleted and the
   evidence per deletion, what you kept and why, and what wants a `src/`
   split that you did not make.
2. The changes on a branch, **not merged**.
3. Before and after: pass count, runtime, and proof the selected-test set is
   unchanged where it should be.

## House rules

- No `Co-Authored-By`, no AI line. Copyright and SPDX name Robert Mech
  alone, never Anthropic or Claude.
- Commit by file name, never `git add -A` or `git commit -a`: other agents
  have uncommitted work in this checkout.
- Do not push, do not tag, do not touch `src/`.
- Never put a password in an environment variable: cloud environment
  variables are readable by anyone using the environment.
