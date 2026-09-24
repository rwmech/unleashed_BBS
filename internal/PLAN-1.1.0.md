# µnleashed BBS 1.1.0: the build plan

Approved by Rob, 2026-09-23 ("Build this whole release, run regression and fix
all bugs ... Go"). One release, built in phases, one firmware developer at a
time on this tree, targeted runs after each phase, then the release gate.
CLAUDE.md holds the history behind each item; this file is what to build.

## Rules for every phase

- Read CLAUDE.md first. Static allocation, no heap in the loop, Term
  primitives only, Plugin descriptors are append-only, and every behaviour
  change gets a check proven to fail on the code before it.
- Measure static DRAM off the ELF (`_bss_end - 0x3FFB0000`, ceiling
  180,736) at the end of every phase. 1.0.1 is 176,720.
- Harness runs use `tools/harness.sh --tag <name>`, 127.0.0.1 only. Never
  the live board or the live directory.
- Commit messages carry no AI line. The developer does not commit; I do,
  once per phase, as `1.1.0-dev.N`.
- Chrome, if ever needed: `--headless=new` on the command line only. No
  playwright or any automation library.

## Design and copy

- Layout, keys and flow: `internal/tty-ux-1.1.0-2026-09-23.md`. It is
  binding unless it conflicts with this plan, in which case ask.
- Every string: `internal/copy-1.1.0-2026-09-23.md`, from the copywriter.
- Decisions the UX report settled:
  - The sysop page is `OPERATOR`, shortcut `O`, and `/o` in the room.
  - Answering puts both callers in the chat room with private messages
    already aimed at each other.
  - Away is the existing DND.
  - Notices reaching callers inside plugins (pages, broadcasts, SHUTDOWN,
    the sysop page) is now part of 1.1.0, as two appended Plugin hooks,
    because the sysop page cannot ring "wherever they are" without it.
  - Cycle fields get a numbered pick in plain ASCII line mode.
  - Plugin pin fields accept -1.
  - A `PS_CYCLE` plugin setting kind, with appended `choices`.
  - Announce's label is `Outside`.

- The copy file's "hand-backs" section is part of the spec. In particular:
  - the oversized-zip error;
  - the KB units in SCREENS.md;
  - nightly backups named `nightly-YYYYMMDD.zip`, so pruning never
    touches manual ones;
  - the restore confirmation always showing its Removes, Accounts and
    Staff rows;
  - the ring status line falling back to node only;
  - progress dots on their own line;
  - a hidden or lurking sysop indistinguishable from an absent one in
    timing as well as in wording.
- Timezone table: check the tz development tree's note that Manitoba
  moves to permanent UTC-5 on 2026-11-01 before tagging 1.1.0.
- SECURITY, fixed separately as 1.0.2 and carried into 1.1.0. Restoring a
  backup on a board on the published default wrote `sysop_password =
  unleashed` explicitly: `unredactLine` wrote the live password. The
  default then worked from any address, and announce stopped holding the
  listing. The 1.1.0 backup work (the Phase 0 union, and Phase 3's SD
  restore) must keep the 1.0.2 fix and its tests.

## Phase 0: foundation

- RAM (from `internal/optimize-2026-09-23-1.0.0.md`):
  - union the backup `exp_`/`imp_` state (3,872), with the state gate on
    `dropClient`'s two `exp_` calls that the report names;
  - allocate the serial bridge's 1 KB buffer at plugin start, the way
    chat's history is (1,024);
  - ziparc's two SysConfig probes parse into syscfg's `g_scratch` (820).
- Stack: log `stackFree` at each new low with the phase and `doing`, raise
  the BBS task stack from 8,192 to 12,288 (heap, not static), then move the
  22 static `UserRec` scratch buffers to locals in two tranches, measuring
  the low water after each. Fix the platform comment that calls the stack
  size words: it is bytes.
- `kCoreRows`: one constant for CONFIG's fixed plugin-page rows, used by
  the announce static_assert and the page builder.
- Update the stale pre-rewrite commit IDs in CLAUDE.md and internal/ to
  the rewritten ones where they can be found, and remove the rest.

## Phase 1: network and installer

- Listening port: `port` in system.cfg, default 6400, 1..65535, refused if
  it equals `backup_port`. It goes on the CONFIG wifi page, which the
  CONFIG menu now calls "network" (`CONFIG wifi` still works), and is used
  from the next restart. mDNS, the Improv telnet URL, SYS and the login
  line follow it.
- Announce: "Port" is relabelled "Outside port" (9-character label
  column), its note says it is what callers dial through the router, and
  when blank it defaults to the listening port.
- Improv:
  - The board answers the installer's first query in time after the
    port-open reset, so a board already running µnleashed is offered
    Update.
  - Once Wi-Fi joins, the board sends its provisioned state and telnet URL
    unprompted.
  - Check the vendored sdk-serial-js in the directory repo
    (vendor/esp-web-tools) for how it treats unsolicited state and result
    packets before designing, and say what it does.

## Phase 2: recovery (bench-tested by Rob)

- BOOT-hold reset, settled in CLAUDE.md. BOOT is pressed after RESET is
  released. The action happens on release:
  - under 7 s: nothing, and the LED blinks slowly while counting;
  - 7 to 15 s: sysop password back to the published default; the LED
    flashes rapidly;
  - 15 to 20 s: factory reset of userdata and logs, never the screens,
    the firmware or the SD card; the LED is solid;
  - at 20 s: abort, the LED goes off, and releasing does nothing.
  Every stage is logged to the console. A board with no activity LED gets
  the same timings. The factory reset also costs the directory listing,
  unless a backup is restored afterwards, and the docs and the /install
  reset section must say so.
- CONFIG wifi fallback: keep the last network that connected. If a changed
  one has not connected within 60 s of boot, fall back to the last good
  one and log it.
- Watchdog: `CONFIG_ESP_TASK_WDT_PANIC`, so a wedged board reboots.
  `reboots.log` already records why a board restarted.

## Phase 3: features

- SD backup and restore:
  - `BACKUP SD` writes the same zip the backup window serves to the card
    at `/backup/unleashed-YYYYMMDD-HHMM.zip`.
  - `RESTORE SD <file>` validates it exactly as an HTTP upload is
    validated, shows what it will replace, and applies it only on Y.
  - A nightly option, off by default, keeps the last 7 backups.
  - The Wi-Fi password rule from 0.22.1 stands: backups on the card carry
    it, and the docs say so.
- A screens-only zip, for mass updates (Rob):
  - `BACKUP SD SCREENS` exports only the screens.
  - `RESTORE SD SCREENS <file>` imports only screens, validated with the
    same screen rules, into the card's screens folder. That folder is the
    override layer, so stock screens in flash are never touched, and
    deleting the files undoes it.
  - Imported screens count as the sysop's own for the seeded-screens
    manifest (Phase 5), so a stock update never overwrites them.
- Timezone picker:
  - CONFIG's Timezone cycles through a table of common zones shown by
    name, e.g. "US Central (Chicago)", with the POSIX string behind each,
    then a blank "Custom" entry that takes a typed POSIX string.
  - The field's note says it is a POSIX TZ string and where to find one.
  - An existing tz that matches a table entry shows as that entry;
    anything else shows as Custom with its string.
- Sysop page:
  - A caller rings for the sysop. When the sysop is on, the sysop gets a
    bell and a notice wherever they are, and can answer (which opens a
    chat between the two), decline or set away.
  - When the sysop is not on, the page is left as a note shown at the
    sysop's next login.
  - Rate-limited per caller, so nobody can ring for ever.
  - The command's name and shortcut come from the UX design in
    internal/tty-ux-1.1.0-*.md.
- Bells:
  - An arrival notice rings a bell when somebody logs in.
  - Callers already in the chat room get a bell when somebody joins.
  - Both respect each caller's BELL setting.
- Lights, a new plugin `lights` with its own CONFIG page:
  - Two NeoPixel (WS2812B, GRB) outputs on RMT (`esp_driver_rmt`, IDF
    5.3.1). No library and no bit-banging.
  - Drive light: 1 pixel, pin default -1 (off; Rob's case uses GPIO13).
    Amber for card access, cool white for internal flash, a slow red blink
    on a storage error, a dim idle glow. A minimum on-time makes a 2 ms
    read visible.
  - Effect strip: 10 pixels, pin default -1 (off).
    - Modes: `nodes` (the default), `scanner` (a Larson sweep), `rainbow`
      and `off`.
    - In nodes mode each pixel is one caller line: off when free, a
      colour by rank when a caller is on (the same rank colours as WHO),
      and a brief flicker on that node's traffic.
  - A brightness cap setting, default low (about 32 of 255): ten pixels at
    full white draw about 600 mA, more than USB supplies. The docs say to
    give a strip its own 5 V feed, with a 330 to 470 ohm resistor in the
    data line and 100 nF across the supply.
  - Both pins refuse the flash pins (6 to 11) and each other, and are
    applied when the plugin restarts.
  - `plat::diskPulse(kind)` is called from the storage paths: screen
    player, users, calllog, syscfg, ziparc, plugin paths, the sd plugin.
  - The host build records the pixel colours so tests can read them.
  - Update at most about 50 times a second, driven from the loop without
    blocking. RMT sends asynchronously.

## Phase 4: badges (firmware side)

- Short badge codes: the proposal is `internal/badge-codes-proposal-2026-09-23.md`.
  - ANNOUNCE.md, system.cfg.example and the tests use the codes.
  - The CONFIG Support and Interests notes point at /badges, and input is
    accepted in any case.
  - The optional pick-list: a CONFIG sub-page per list, arrow and Space
    to toggle, generated at build time from the directory's code table by
    a tool script, so the board and the site cannot drift.
- SD size: an `sd` announce field, the card size in GB rounded up to the
  printed size (1, 2, 4 ... 1024), sent only while a card is mounted.

## Phase 5: fixes

- The seeded-screens manifest. When `sd` seeds a stock screen onto the
  card, it records a hash. At mount, a card copy that still matches the
  manifest is replaced when the flash copy differs. A card copy that does
  not match was edited by the sysop, or imported, and is left alone.
- CONFIG forums grows with the topics. It shows the topics set plus one
  empty row, up to 12 (16 fields minus the 4 level rows). Beyond that, a
  list page with one button per topic, the way file areas work, reaching
  all 16.
- Plain ASCII line mode can clear a password field, which is how an open
  network is chosen.
- The user manager's D key says "Retire", as USER DEL does.
- privacy.ans is reflowed to about 72 columns for ANSI only. The copy is
  explain's and the art screen-artist's. PETSCII and ASCII are unchanged.
- The Rusty Antenna logged 541 slow passes in 24 minutes (1.0.0, no card,
  one caller, a 342 ms worst in the session phase). Find the cause from
  the code and the slow-pass logging; suspect a cardless board retrying
  its mount. Fix it.
- Everything the post-tag 1.0.1 regression and the 1.1.0 regression find.
- From 1.0.2 (Rob: "keep going"; my recommendation stands):
  - A system.cfg line that spells out the published default is treated
    as the default at boot. That board is local-only with its listing
    held, and is offered setup, until a real password is set. It fixes
    boards an earlier restore already affected.
  - A restore that would leave `sysop_password =` empty is refused,
    because it would switch staff off; CONFIG already refuses that.
  - A co-sysop line dropped for being the default is reported in the
    restore result, not only in the serial log.

## Release gate

- Code review of the whole diff.
- The full regression, with every failure fixed and the check rerun.
- A milestone memory report in internal/.
- Rob's bench list, confirmed on hardware:
  - BOOT timings and LED;
  - the Wi-Fi fallback;
  - the watchdog;
  - both NeoPixel outputs and every strip mode;
  - the port setting, with a forward that uses the same number;
  - installer recognition and the address in Telnet details.
- Then tag v1.1.0, and deploy the paired site version: the short codes,
  the SD badge, the go-public guide's "one board per port", /setup for the
  new CONFIG pages (network, lights, timezone), and /install's reset
  section gated on 1.1.0.
