<!--
µnleashed BBS: ADDING_A_SAT.md

Building a sat: a small ESP32 that talks to a board over the µnleashed
link. What a sat is, the link it speaks, a sat's own repository against
core.lock, the board-side plugin, external plugins, testing and releases.

Copyright 2026 - Robert Mech
License: GNU General Public License v3 or later
SPDX-License-Identifier: GPL-3.0-or-later

Documentation for µnleashed BBS, part of the same distribution as the
source. See the LICENSE file for terms.
-->

# Adding a sat

**Applies to versions:** firmware 1.2.0 (plugin API 1.3), and camsat 1.1.0
as the worked example.

A **sat** (short for satellite; plural **sats**) is any box on the
µnleashed link: a cheap ESP32 near a board, paired with it, talking to it
over ESP-NOW. Sats are how the project grows without growing the core: the
board carries what a BBS must have, and extras run on a sat beside it. A
sat is a microcontroller too; nothing here needs a Raspberry Pi or any
other computer.

Read [CONTRIBUTING.md](CONTRIBUTING.md) first for the flow, the sign-off
and the licence, and [LINK.md](LINK.md) for the protocol itself. This page
is the route through them. The camera sat,
[unleashed_camsat](https://github.com/rwmech/unleashed_camsat), is the
worked example throughout: a whole sat, firmware and board-side plugin, in
a repository of its own.

It is written for somebody working with Claude Code, as
[ADDING_A_BOARD.md](ADDING_A_BOARD.md) is. Point it at LINK.md, this page
and camsat's repository, and use the project's agents in `.claude/agents/`
(`code-review` before the PR). It can read the link and camsat far faster
than you can; it cannot see your sat, so the bench is yours.

## What kind of sat

There are two kinds, and which one you are building decides how much of
the core you touch.

- **A door sat** is one a caller goes into. The caller types `DOORS` to
  see what is up, `UPLINK name` or `UPLINK n` to go in, and is handed
  across with who they are, their terminal and their minutes left; three
  Ctrl-C in a row (RUN/STOP on PETSCII) brings them home. The program on
  the sat is a door, as on any BBS. **The board side already exists**: the
  core's `doors` plugin speaks the DOOR family (LINK.md, "Family 2:
  DOOR"). A door sat is sat firmware only, in any language that can run on
  the box and speak the link.
- **An orbiter** feeds the board data: a camera, remote control of GPIO,
  sensors. A camera orbiter fits what exists (the CAMERA family, the
  camera registry, SNAPSHOT, Photos). Any other kind of orbiter needs a
  board-side plugin of its own, and usually small core changes too: a
  message family id, a kind id and its words.

The words are settled and live in `src/core/satwords.h`: **sat**, **sats**,
**orbiter**, **door sat**, and the verbs `SATS`, `DOORS`, `UPLINK`. A
sat's type shown on screen is `camera`, `door`, `gpio`, `sensor` or
`device`; "camsat" is only the camera sat's firmware and repository name,
never a word a caller sees. Every line the board prints about a sat fits
39 columns with a 16-character sat name, except the two that echo what
the caller typed.

## The link, in one page

All of this is [LINK.md](LINK.md); these are the parts a sat author meets
first.

- **The radio.** ESP-NOW on ESP-IDF 5.3.1: 250-byte frames, a 20-byte
  header, at most 222 bytes of payload a frame; longer messages are sent
  in fragments, up to 512 KB. On the host build the radio is UDP on
  127.0.0.1, which is how the tests run with no hardware.
- **The link encrypts for itself.** Peers are added to ESP-NOW
  unencrypted, and every frame after pairing is sealed with AES-128-CCM,
  the header as associated data. Do not add your own crypto: use the
  engine, which is the same code on the board and on the sat.
- **Pairing.** P-256 ECDH with commit-then-reveal, and a 4-digit code
  both ends show. On the board the sysop types `LINK PAIR` (a 2-minute
  window) and confirms
  `Pair camera "garden" <mac>, code 1234? (y/N)`. On the sat: one with a
  pairing button holds it for 3 s; one without, like camsat, is in
  pairing mode for 5 minutes after boot while it holds no pairing. A board holds 8 pairings; a sat holds up to 5
  boards, the first being its owner (who shares, revokes, and whose
  settings are used).
- **Channels.** ESP-NOW rides the board's Wi-Fi channel. The board never
  changes channel; the sat scans for it, and every board sharing one sat
  must be on one channel (in practice the same router). A board on its
  Ethernet cable with Wi-Fi unjoined cannot pair sats in 1.2.0.
- **Sessions.** In-order, acknowledged messages inside a session, with
  retries and a window. Bulk transfers (a picture) are opened and written
  on the board's background runner, never on the BBS loop.
- **Families and kinds.** A **family** is a set of messages: 0 LINK
  (control), 1 CAMERA, 2 DOOR, 3 reserved for 1.3.0's inbound caller
  sessions, 128 to 239 for plugins, 240 to 254 experimental and never in a
  release. A **kind** says what a peer is: 1 camsat (shown "camera"),
  2 doorbox ("door"), 3 reserved (gateway). Both tables live in LINK.md,
  and an id is assigned by adding a row there, the way a PROTOCOL.md field
  is.

## Your sat's repository

A sat lives in a repository of its own, under the GPL-3.0-or-later like
the core. camsat's layout is the one to copy:

```
unleashed-plugin.ini      the board-side plugin's manifest
bbs/                      the board-side plugin's sources (empty for a door sat)
firmware/                 the sat's own firmware (a PlatformIO, ESP-IDF project)
  platformio.ini
  src/                    main.cpp, radio.*, board.h (with <NAME>_VERSION) ...
  tools/fetch_core.py     copies the link out of the core at core.lock's commit
  test/                   host tests of the sat's own logic
core.lock                 which core commit the firmware builds its link from
tools/release.py          builds the release images
.github/workflows/release.yml
LICENSE
README.md                 what it does, the hardware, flashing, pairing
```

**core.lock** pins the core the sat speaks:

```
source = https://github.com/rwmech/unleashed_BBS
commit = cc63c5f9fcb7248ab63aeaecac2c3c3af2d37e5b
```

(that commit is the v1.2.0 tag). `firmware/tools/fetch_core.py` runs as a
PlatformIO pre-script and copies the link's sources out of the core at
exactly that commit into `firmware/core/`: `src/core/link.h`, `link.cpp`,
`linkcrypto.h`, `linkcrypto.cpp`, `linkfam.h`, `satwords.h` and `crc32.h`
(camsat also takes the picture helpers it shares with the board). The sat
and the board then run the same link code, not two copies of it. A local
path with `-` as the commit builds against that working tree, for
development; a release refuses a local path.

**The sat's side of the link.** The sat runs `ulink::Engine` over its own
ESP-NOW radio (camsat's `firmware/src/radio.*` is the one to start from;
the core's own radio is not copied). It tells the board what it is and
how to pair:

```
engine.setIdentity(ulink::KIND_DOORBOX, MYSAT_VERSION, 1u << ulink::FAM_DOOR);
engine.startPairing(ulink::KIND_DOORBOX, name, MYSAT_VERSION);
```

camsat does the same with `KIND_CAMSAT` and `FAM_CAMERA`
(`firmware/src/main.cpp`). Keep the pairings in NVS, give the box a way to
forget its boards (camsat: hold IO0 for 5 s while it runs; holding IO0 at
power-up is the ESP32's flashing mode instead), and follow the board's
channel as LINK.md's "Channels" says.

## A door sat

For a door sat that is nearly all: the sat lists its doors, takes a
caller in with the handoff line, and gives them back. The handoff line
(LINK.md, "The handoff line"):

```
UNLEASHED-DOOR 1 node=3 session=12 handle=N0CALL rank=user cols=80 rows=24 term=ansi minutes=42 board=Unleashed+HQ
```

A space in a value is sent as `+`; `rank` is guest, user, co2, co1 or
sysop; `term` is ascii, ansi, pet40 or pet80, so a door knows the width
and character set without probing again. The door says when it has
finished, the board says when the caller's time is up, and neither trusts
the caller's keystrokes to mean either (LINK.md, "How a caller goes in and
comes back"). The board passes every byte through, Ctrl-C included, and
takes the caller back on the third Ctrl-C within 1.5 s.

`host/linkpeer` in the core is a pretend door box (`--kind doorbox`) that
the core's own tests drive: read it as a reference for the DOOR family
from the sat's side.

## An orbiter: the board-side plugin

A new orbiter needs code on the board, and that code lives in your
repository's `bbs/` folder, built into the firmware as an **external
plugin** (PLUGINS.md, "Plugins in their own repositories").

**The manifest**, `unleashed-plugin.ini` (camsat's):

```
name        = camsat
version     = 1.1.0
api         = 1.3
descriptor  = kCamsatPlugin
requires    = link sd
boards      = *
license     = GPL-3.0-or-later
```

`name` matches the lock entry (lower case, digits and `_`, up to 15),
`descriptor` names the plugin's `Plugin` in `bbs/`, `api` is the plugin
API it was written for, `license` must combine with GPL-3.0.

**The plugin** is an ordinary plugin (PLUGINS.md): a `Plugin` descriptor
with hooks and commands. Three things are particular to a sat's:

- **Declare the API** in a source file: `UNLEASHED_PLUGIN_API(1, 3);`.
  The build fails, saying the core is too old, if the core's API is
  behind; the major must match and the minor be at most the core's
  (`src/core/plugin.h`, `BBS_PLUGIN_API_MAJOR`/`MINOR`).
- **Register a family** in `start()` and unregister it in `stop()`:
  `linkp::registerFamily(f)` with a `linkp::Family` (`src/plugins/link.h`)
  of handlers for single-frame messages, bulk transfers, resets and peers
  coming up. `bulkData` and `bulkFinish` run **on the background runner**,
  not the loop: that is where card writes for a big transfer belong.
  Everything else runs on the loop and must be quick (Rule no. 1: nothing
  a feature does may stall the callers who are not using it).
- **A camera** registers with the camera registry, not its own commands:
  `photos::addCamera` with a `photos::Camera` (`src/core/photos.h`), and
  SNAPSHOT, the camera numbers, the per-caller limits, Photos, pruning
  and SATS come with it. camsat's `reconcile()` in `bbs/camsat.cpp` is the
  example.

**External plugins are held to more than shipped ones.**

- One with storage must be `PF_SD`, its files on the card: an external
  plugin is never given space on the board's flash, whatever flags it
  claims.
- A name that clashes with a shipped plugin is refused at start.
- External plugins come after `link` in the registry, so they stop after
  the link has; do not rely on sending a farewell from `stop()`.

**What may need the core, as a PR to unleashed_BBS:**

- a **family id** (a row in LINK.md's table). In 1.2.0 the board's family
  table holds 4 families: DOOR is taken by the core's doors plugin, and
  CAMERA by camsat's plugin when it is built in. HELLO
  carries a peer's families as a 32-bit mask, so how a plugin family in
  128 to 239 is advertised is not settled yet. Ask in your issue before
  you pick an id;
- a **kind id**: `KIND_*` in `src/core/link.h`, its word in
  `src/core/satwords.h` and `ulink::kindName()` in `src/core/link.cpp`;
- anything shown in SATS or CONFIG sats for a kind other than camera.
  In 1.2.0 SATS lists camera sats only.

## Building it into a board

`plugins.lock` in the core pins each external plugin to one commit:

```
# name    source                                       commit
mysat     https://github.com/you/unleashed_mysat       <40-character commit>
```

A local path and `-` work while developing (`../unleashed_mysat  -`
builds your working tree); a release refuses both. Then:

```
python3 tools/plugins.py fetch mysat    # into ext/mysat, at the pinned commit
python3 tools/plugins.py check mysat    # the manifest, the API, every SPDX line
```

`check` refuses a name that differs from the lock, an API the core does
not have, a licence that does not combine with GPL-3.0, a source file with
no SPDX line, and any copyright or licence line naming Anthropic or
Claude.

A PlatformIO environment names the plugins it builds in with
`custom_ext_plugins = mysat`; `tools/pio_plugins.py` fetches them before
the build. In 1.2.0 no shipped environment names one, and camsat's line in
`plugins.lock` is still commented out: the camera sat's board side is not
in the v1.2.0 images yet.

## Testing

On the host, with no radio and no hardware (CONTRIBUTING.md has the host
setup):

```
make -C host EXT="mysat" bbs_host_ext                         # the board with your plugin
bash tools/harness.sh --tag mysat --ext mysat --card --only=radio,sats
```

- `--ext NAME` builds the host board with the plugin and switches it on.
- `host/linkpeer` stands in for a sat over the host's UDP radio
  (`--kind doorbox` or `--kind camsat`). A sat of a new kind gets its
  pretend peer the same way, in `linkpeer` or beside it, and a test in
  `tools/testclient.py` that drives it: `test_sats` is the model for an
  orbiter, `test_doors` for a door sat. A test that needs your plugin
  SKIPs without it.
- `bash tools/test_ext_plugin.sh` checks the external-plugin machinery
  itself with the minimal plugin in `tools/testplugin/`, which is also the
  smallest possible starting point for `bbs/`.
- `make -C host test` runs `test_link`, the link engine against a
  simulated radio.
- The sat's own logic gets host tests in its repository: camsat's
  scheduling is a pure header with `firmware/test/test_sched.cpp`, built
  with ASan and UBSan.

Then on the bench: flash the sat, `LINK PAIR` on a board, compare the
codes, and use it. `SATS` and `CONFIG sats` show the pairing; the
console's log shows the link coming up.

## Releasing a sat

camsat's pipeline is the model, and its asset names are the ones the
project's site fetches:

- A tag `vX.Y.Z` on the sat's repository runs `.github/workflows/release.yml`,
  which runs `tools/release.py --tag vX.Y.Z` and publishes a GitHub release
  (a pre-release when the tag has a suffix).
- `tools/release.py` refuses a tag that is not the version in
  `firmware/src/board.h` (camsat's `CAMSAT_VERSION`), a dirty tree, a
  core.lock on a local path or not a full commit, a GPL-2.0 SPDX line, and
  a notice line naming Anthropic or Claude. It builds the firmware and checks
  the version is in the image.
- The assets: `bootloader.bin` (at 0x1000), `partitions.bin` (0x8000),
  `firmware.bin` (0x10000), `version.txt`, `THIRD_PARTY_NOTICES.md` and
  `SHA256SUMS`.
- Keep the version the same in all three places it is written: the
  firmware's board.h, `unleashed-plugin.ini` and the plugin descriptor.
  Only the first is checked against the tag.
- When the sat's board side is to ship in the official images, the core
  pins it in `plugins.lock` and names it in the release environments'
  `custom_ext_plugins`, in a core PR.

**The site.** The project's site lists each sat kind on its own install
page under /satellites, and its fetcher downloads each sat's newest
release and checks it against `SHA256SUMS`. Adding a new kind there is
done by the project in the site's own repository: name your repository,
your release's asset set and the hardware in your PR, and it is added.

## Checklist

- [ ] An issue first: what the sat does, which kind, and any family or
      kind id it needs.
- [ ] A repository of your own, GPL-3.0-or-later, every source with an
      SPDX line, laid out like camsat.
- [ ] `core.lock` pinned to a released core commit.
- [ ] The sat pairs, follows the channel, keeps and forgets its pairings.
- [ ] For an orbiter: the board-side plugin, `UNLEASHED_PLUGIN_API`, its
      family registered and unregistered, bulk work on the runner.
- [ ] `plugins.py check` passes; the host build with `--ext` passes the
      suite and your plugin's own test.
- [ ] Any core change (an id, a word, SATS) as its own signed-off PR to
      unleashed_BBS, with its documents.
- [ ] A release pipeline that builds the images and refuses a version
      mismatch.
- [ ] A README: the hardware, flashing, pairing, and what a sysop sees.

Changes to the firmware or to camsat that make this page wrong update it
in the same commit, and move its "Applies to versions" line.
