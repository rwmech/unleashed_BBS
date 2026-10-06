<!--
µnleashed BBS: CONTRIBUTING.md

How to send a change to the firmware: the flow, the Developer Certificate
of Origin, the licence, copyright, AI tools, and what review looks like.

Copyright 2026 - Robert Mech
License: GNU General Public License v3 or later
SPDX-License-Identifier: GPL-3.0-or-later

Documentation for µnleashed BBS, part of the same distribution as the
source. See the LICENSE file for terms.
-->

# Contributing to µnleashed BBS

**Applies to versions:** firmware 1.2.0 (the v1.2.0 tag and main since it).

µnleashed is a BBS that lives on a microcontroller. It is built for the
legacy serial community, the 8086 boxes, 6502 machines, VT220s on a serial
line and everything in between, and for anybody who wants to run a board of
their own. Contributions are welcome, and two kinds have a guide of their
own:

- **A new board**, so the firmware runs on hardware it does not support
  yet: [ADDING_A_BOARD.md](ADDING_A_BOARD.md).
- **A new sat**, a small ESP32 that talks to a board over the µnleashed
  link (a camera, a door, a sensor): [ADDING_A_SAT.md](ADDING_A_SAT.md).

Anything else (a bug fix, a screen, a plugin, a document) follows the flow
on this page.

## Before you start

- **Ask first for anything bigger than a fix.** Open an issue that says
  what you want to change and why, before writing the code. The project
  works this way internally too: a proposal, a yes, then the build. It
  saves you writing something that does not fit.
- **Read what the project already decided.** [CLAUDE.md](CLAUDE.md) is the
  design history: every settled decision, and the reasoning behind it,
  with the date and who made it. If your change touches something listed
  there, say so in the PR.
- **Know the rules the code is held to.** The ones a reviewer will check
  first:
  - **No lag for anybody else.** Nothing a feature does may stall callers
    who are not using it. Slow work (a card scan, a JPEG encode, a network
    call) runs off the BBS loop on the background runner
    (`src/core/runner.*`) or in slices small enough not to show, and a
    caller who has to wait is shown that the board is working. SYS's "Loop
    worst" and "Slow passes" are the measure.
  - **The microcontroller is the point.** Logins, accounts, policy,
    screens and features live on the board or on an ESP32 sat. No change
    may make a Raspberry Pi or any other computer necessary.
  - **Build for the smallest part.** The reference board is a bare
    ESP32-WROOM-32E with no PSRAM: about 180 KB of static RAM, 4 MB of
    flash. Static allocation, no heap in the BBS loop. A feature that only
    fits on a bigger chip is left out of the small builds entirely, not
    switched off.
  - **Extras go on sats.** The core carries what a BBS must have. Heavier
    extras run on a sat beside it.
  - **Every terminal, every width.** A screen works at 40 columns
    (PETSCII-40) and 80 (ANSI, PETSCII-80 and plain ASCII), and degrades
    sensibly on the machines that cannot take all of it. No one machine
    is the target.

## The flow

1. **Fork** github.com/rwmech/unleashed_BBS and clone your fork.
2. **Branch** from `main`: one branch a change, named for what it does
   (`fix/forum-wrap`, `board/acme-s3-lcd`).
3. **Build and test on your PC** (below), then on hardware if the change
   reaches hardware.
4. **Commit with a sign-off** (`git commit -s`, below).
5. **Update the documents your change makes wrong, in the same commit.**
   COMMANDS.md for a command or setting, CHANGELOG.md for anything a sysop
   or caller would notice, and any guide whose steps changed, including
   its "Applies to versions" line (below).
6. **Open a pull request** against `main` that says what changed, why, and
   how you tested it. A PR for a board also carries the bench proof that
   [ADDING_A_BOARD.md](ADDING_A_BOARD.md) lists.

## Building and testing on a PC

The whole core builds as a Linux program (`host/`), so most of the test
suite runs with no board at all. Linux or WSL, with:

```
sudo apt-get install -y g++ make python3 zlib1g-dev lrzsz
```

The host build links mbedTLS 3.6.0, the version ESP-IDF 5.3.1 carries.
`host/Makefile` finds it in PlatformIO's ESP-IDF package when that is
installed as `~/.platformio/packages/framework-espidf@3.50301.0`.
Otherwise, point `MBEDTLS_DIR` at a copy: PlatformIO's (the
`components/mbedtls/mbedtls` folder inside its ESP-IDF package), or a
clone:

```
git clone --depth 1 --branch v3.6.0 https://github.com/Mbed-TLS/mbedtls ~/mbedtls
git -C ~/mbedtls submodule update --init --depth 1
export MBEDTLS_DIR=~/mbedtls
```

Then:

```
make -C host test                                 # unit tests, format checker, licence check
bash tools/harness.sh --jobs 8 --changed origin/main..HEAD   # the groups your change touches
bash tools/harness.sh --jobs 8                    # the whole suite: with and without a card, every board profile
```

- Run the scripts with `bash`: they are stored without the executable bit.
- `--changed RANGE` works out which test groups your files can affect and
  runs those. `--changed-dry-run RANGE` only prints the choice.
- Results land in `/tmp/bbs-<tag>/out.txt`. A failure that passes when the
  test runs alone (`--tests=test_name`) is usually test order or the 4x
  fast clock the lanes use, not the board; say which in the PR.
- [README.md](README.md) has the board build (`pio run -t flashall`, the
  reference board `esp32dev` by default) and the rest of the layout.

## Sign your work: the Developer Certificate of Origin

Every commit in a PR carries a `Signed-off-by` line with your real name
and an address you can be reached at. It certifies the Developer
Certificate of Origin below, which says you have the right to send the
code under the project's licence. `git commit -s` adds the line for you:

```
git commit -s -m "forums: keep a code whole across the composer's wrap"
```

which ends the message with:

```
Signed-off-by: Your Name <you@example.com>
```

Forgot it? `git commit --amend -s` fixes the last commit, and
`git rebase --signoff main` fixes every commit on your branch (then
force-push your own branch; never anybody else's).

The certificate, version 1.1, from https://developercertificate.org/:

```
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.


Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```

## Licence and copyright

- **The firmware is GPL-3.0-or-later**, and so is everything you send.
  Every source file carries `SPDX-License-Identifier: GPL-3.0-or-later`.
  `make -C host test` and `tools/release.py` refuse a GPL-2.0 SPDX line,
  so a file started from an old header fails `make -C host test` and the
  release.
- **Your copyright is yours.** On a file you write, add your own line
  beside the licence, in the same form as the project's:

  ```
  Copyright 2026 - Your Name
  ```

  On a file you change, leave the existing lines as they are; you may add
  yours below them for a substantial change. The project's own lines name
  Robert Mech.
- **Code you did not write** keeps its own notice and must have a licence
  that combines with GPL-3.0 (Apache-2.0, MIT, BSD, ISC and the like).
  Say where it came from in the PR.
- **Nothing names an AI company or tool as an author.** No copyright,
  licence or author line in any file may name one, and no file may carry
  a "Co-Authored-By" or "Generated with" line naming one.
  `tools/release.py` refuses to build a release from a tree whose
  copyright, licence, author, Co-Authored-By or Generated-with line names
  Anthropic or Claude.

## AI tools

You may use AI tools, Claude Code included. The board and sat guides are
written for somebody working with Claude Code, and
`.claude/agents/` holds the project's own agents (code-review, bbs-qa and
the rest), which your Claude Code can use too.

- **Your commits may carry Claude Code's `Co-Authored-By` line** if you
  want them to. It is your commit and your call. (The project's own
  commits do not carry one; that is a house style, not a rule for you.)
- **You are the author.** You sign the DCO, so you answer for the code:
  read it, understand it, and test it before you send it. "The tool wrote
  it" is not a review.
- Keep AI lines in commit messages, not in files (above).

## What review looks like

1. **Rob reviews every PR.** He owns the project's direction; a change
   that is correct may still not fit, and the issue you opened first is
   how you find that out cheaply.
2. **A code-review pass.** An adversarial read of the diff for the bugs
   tests pass straight over: a partial read treated as a whole message,
   state that leaks from one caller to the next (sessions are a static
   pool), a guard that bounds the wrong quantity, a path that only fails
   on real hardware or a real network. The agent is
   `.claude/agents/code-review.md`; running it yourself first saves a
   round.
3. **A targeted host test run.** The groups your change touches
   (`--changed`), with and without a card, on every board profile the
   change reaches.
4. **Hardware, where it applies.** A board needs its bench proof
   ([ADDING_A_BOARD.md](ADDING_A_BOARD.md)). A change that can take time
   on the loop gets measured against SYS's loop figures.

A board ships first as a board pre-release (one board's images, labelled
preview on the installer) and joins the next full release after it.

## Keeping the guides true

Every guide in this repository opens with an **Applies to versions** line:
the firmware versions it is true for (and, for the sat guide, the camsat
versions). A change that makes a guide wrong fixes the guide in the same
commit and moves its line. A guide that describes behaviour the current
release does not have yet says so.
