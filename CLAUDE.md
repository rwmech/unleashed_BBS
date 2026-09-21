<!--
µnleashed BBS: CLAUDE.md

Design history and current state of the project, kept for contributors and AI assistants.

Copyright 2026 - Robert Mech
License: GNU General Public License v2 or later
SPDX-License-Identifier: GPL-2.0-or-later

Documentation for µnleashed BBS, part of the same distribution as the
source. See the LICENSE file for terms.
-->

# µnleashed BBS: project context

Read README.md for layout and build, COMMANDS.md for every command and setting, USERS.md for accounts, BACKUP.md for the backup window, SCREENS.md for screen rules. This file is the design history and current state. Keep it current with every build and decision.

## What this is

µnleashed BBS (ASCII: `unleashed`; repo github.com/rwmech/unleashed_BBS, private) is a telnet BBS framework hosted on an ESP32 that grows into an "IoT terminal server". The name is about electronic freedom: real hardware reachable without a web browser. Don't brand it to the ESP32, and remember that UCBBS/µCBBS clashes with the 1990 C64 program Ultra-Com BBS. A C64 dials in through TeensyROM (Swiftlink/Ethernet emulation at $DE00). PC callers use SyncTERM, PuTTY or telnet. The core is minimal; everything else bolts on as plugins.

Prior art check (done): no BBS software runs on an ESP32. ESP32 only shows up client-side (Zimodem, Meatloaf). No native Home Assistant client exists for the C64 (HomeTo64 needs an Ultimate 64 REST API). This is open ground.

## Settled decisions

- PlatformIO on ESP-IDF 5.3.1, pinned `espressif32@6.9.0`. ESPHome dropped. The Espressif VS Code extension is disabled for this workspace (it fights PlatformIO).
- Reference target: bare ESP32-WROOM-32E, Bluetooth off (520 KB SRAM, ~180 KB usable DRAM, 4 MB flash, no PSRAM). The core is sized for it; plugins declare their own needs.
- **Build for the smallest part, run on the bigger ones** (Rob's framing: a VIC-20 design that also runs on a C64, a C128 and an Amiga). The WROOM is the floor, and sizing to it is what produced the 40 column layouts, the static allocation, the 6,000 byte session and the habit of measuring rather than assuming. Every one of those makes the board better on a larger part, not merely possible.
  The reverse never works. Build for an S3 and hope it squeezes onto a WROOM and you find out at link time, which is exactly what happened at sixteen nodes.
  **Two cores and on-chip Wi-Fi are a requirement, not a preference** (Rob). The loop is pinned to core 1 because Wi-Fi and lwIP own core 0, and that split is what keeps the radio's work off callers' latency; a single core would run but not run well, and fixing it properly means restructuring the core rather than changing a setting. That rules out the C3, C6, S2 and H2 on cores and the P4 on having no radio at all, leaving the ESP32 and S3 families. See [ESP32_BOARD_CHOICE.md](ESP32_BOARD_CHOICE.md).
  An ESP32-S3 with PSRAM is the upgrade path if more callers are ever wanted: same dual core split, and `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` puts the session pool in PSRAM so it stops competing for internal DRAM. **Not the P4**, which has the most SRAM of the family at 768 KB and no integrated Wi-Fi at all: Espressif's own answer there is a second chip as a wireless companion, which is two chips and a host protocol for a board whose whole premise is telnet over Wi-Fi.
- **Who calls in: the legacy serial community, not one machine** (Rob, and worth holding on to because it is easy to drift from). 8086 boxes, 6502 machines, a VT220 on a serial line, and everything in between. The C64 through TeensyROM is one caller among them and gets attention because PETSCII and 40 columns are the tightest constraints, not because it is the target. A design argument that rests on what a C64 can do is the wrong argument: the right question is whether a feature works across the range and degrades sensibly for the machines that cannot take all of it. File transfer is the live example, where the answer is to offer XMODEM, YMODEM and ZMODEM and let a caller use what their machine handles.
- C++ for core and hardware. Lua only for doors later. Static allocation, no heap in the BBS loop (exceptions: temporary inflate buffers during a backup upload).
- 10 caller nodes (6 until 0.17.0, briefly 16), a busy line session (the caller past the last node: detection, busy screen, 10 s countdown), a hidden sysop node. Overflow callers get `BUSY` and a drop. Socket budget 24.
- **The real static RAM ceiling is 180,736 bytes**, and it is in the linker script, not on any datasheet: `memory.ld` sets `dram0_0_seg` to `org = 0x3FFB0000, len = 0x2c200`, and `sections.ld` asserts `_bss_end` stays inside it. Measure with `_bss_end - 0x3FFB0000`. **PlatformIO's RAM percentage is against 327,680, so multiply it by 1.81 to get the truth: 55% on its scale is the wall.** At 0.17.2 the board is at 146,732, which is 81% of what it actually has and 44.8% of what PlatformIO claims.
- **A `Session` is 5,716 bytes, not 6,000** (measured from DWARF, confirmed against `nodes_[10]` at 57,160). Earlier figures in this file and in the commit history say 6,000 and are 5% pessimistic; the shape of the arithmetic is unchanged.
- **`CONFIG_LWIP_MAX_SOCKETS` was silently 10, and that was self-inflicted.** IDF 5.3.1's Kconfig is `range 1 16, default 10`, and a value outside the range in a defaults file is **discarded rather than clamped**. Block A "raised" it from a working 16 to 24, so the board got 10. The listener, mDNS and SNTP take three, so seven callers filled a board advertising sixteen and the eighth `accept()` failed. Now 16, which is the hard maximum. **The node count was never really RAM-bound, it was socket-bound**, and sixteen sockets is what makes ten caller lines the honest number.
  The lesson generalises: a setting that is out of range does not warn, it reverts, and the generated `sdkconfig.esp32dev` is the only place the truth appears. Check it after changing a default.
- **Sixteen nodes did not fit, and the way it failed is worth remembering.** A `Session` is 6,000 bytes, so eighteen of them was 108,000 bytes of static RAM, and the link failed with `dram0_0_seg overflowed by 104 bytes`. The number that matters is not the 320 KB of SRAM the part advertises: it is what is left for statically allocated DRAM after the ROM and the radio have taken theirs, and PlatformIO's RAM percentage is measured against the larger figure, so it read 54.9% while actually being over. Ten nodes is twelve sessions and 72,000 bytes, which leaves room for the transfer buffers and what follows them.
- Over half a session is its output buffer, `BBS_TL_BYTES` at 3,072. That is the first dial to reach for if RAM is ever short again, and the reason it is 3,072 is that a PETSCII form redraw is about 1.2 KB and a smaller buffer once dropped output for a C64 caller typing ahead.
- One dial-in port, 6400. Connect-time terminal detection: settle 300 ms (a telnet client that speaks IAC first gets character mode negotiated and an immediate probe; this fixed PuTTY's line-mode "[3;20R" leak), CPR probe with UTF-8 test glyph, else PETSCII/ASCII key prompt, then 40/80 columns for PETSCII.
- Security posture: plaintext telnet accepted (the C64 side cannot encrypt). Staff passwords in `system.cfg`. `BYE <password>` elevates; a wrong one is a plain logoff and counts toward an IP ban (3 in 15 min = 15 min). No TOTP, no email codes.
- Staff: one sysop (hidden node, all permissions), co-sysop 1 and 2 (stay on their node, permissions from the `[access]` matrix in `system.cfg`, `NOLIMITS` is a matrix row). Rank: KICK/SNOOP only on lower levels.
- Storage is split by who owns it (0.14.0). `storage` holds the screens and is the only partition `uploadfs` rewrites, because PlatformIO writes the LAST spiffs partition and `storage` is kept last on purpose. `userdata` holds `users.txt`, `system.cfg` and `<user>/p/<plugin>/`, so accounts, config, chat mail and the announce token survive a reflash. `logs` holds the caller log. The backup zip still carries `system.cfg`, `users.txt` and screens as one file, and `ziparc::livePath` routes each entry back to the partition it belongs on. `syscfg` seeds `userdata/system.cfg` from the shipped copy once on a blank board, otherwise a fresh board has no sysop password and can never have staff. Paths: `plat::fsBase()` screens, `plat::userBase()` user data, `plat::logsBase()` logs; plugin free space is measured with `plat::userInfo`.
- Backup window: BOOT button (GPIO0) while the sysop is logged in opens HTTP on `backup_port` for `backup_window_minutes`. Download needs no confirmation and redacts passwords as `***`; upload is staged, validated, and applied only after the sysop's Y. HTTP runs inside the BBS select loop (no httpd task, saves 27 KB flash, host-testable).
- Commands: C3 is a command registry (verbs, shortcuts, permission, help, handler) that plugins register into. The navigation tree is deferred; if it returns it is a device namespace (`ls /gpio`), not a BBS menu tree.
- Plugin order after core: GPIO, chat (DDial/Gtalk style), serial bridge (sysop/LAN only, raw stream handoff). Then mail, message bases, XMODEM, Lua doors, federation.
- SD card (0.17.0 Block B, done): the `sd` plugin mounts FAT32 over SPI and the card is optional throughout. It does **not** take the accounts: `users.txt` stays on `userdata` because it is the one thing that has to survive a card failing, and LittleFS is power-fail safe in a way FAT is not. File areas, message bases and a sysop's own screens are what the card is for. Still planned: logging redirected to the card, and the file manager (Block C).
- `max_users` is 250 and that is an index width, not a space limit. `userdata` holds roughly 1,380 accounts at ~450 bytes each, but `users::count`, `users::at`, `Session::ulSel`/`ulTop`/`ulCount` and `Session::listIdx` are all `uint8_t`, and `listIdx` is the row counter in *every* list on the board. Setting the cap to 1000 made the compiler turn it into 232 in a `uint8_t` field, which is how this was found. Widening is mechanical but touches every list, so it waits for a build that is not also moving a partition. Raising it later costs no erase.
- User accounts (0.6.0): form and list screens (cursor-driven on ANSI and PETSCII, line prompts on plain ASCII) instead of typed commands, except `USER DEL`; self-registration on by default (`self_register`); password typed twice; salted SHA-256 x1000 (simple on purpose, not PBKDF2: the file has to be stolen first); 3 wrong per call hang up, 5 per handle in 15 min lock it (RAM); staff elevation stays on `BYE <password>`, separate from accounts, so a guessed account password never grants staff; flashy fx on login, sign-up and forms. `users.txt` is a `[handle]` block file, rewritten via temp file + rename; adding a field is one `UserRec` member plus one `kUserFields` row. USERS.md documents it.
- Guests (0.7.0, Rob's revision): one handle prompt for everyone. A known handle gets `Password:`; an unknown one gets `[R]egister, [G]uest or [N]ew handle?` (only the options `self_register`/`guest`/`max_users` allow). Guests keep the typed handle, marked `*` in WHO, LAST (`CallRec::F_GUEST`), NODES and DASH with a `* guest` footnote under the list. A handle held by an online guest is refused ("online right now"), rechecked when R/G is pressed. No account, nothing saved (`saveCallStats` skips them), `guest_minutes` (15) per call and no daily limit. The call still goes to the caller log (sysop's security record). No reserved guest names (only SYSOP). `CF_ACCOUNT` commands (PROFILE, PASSWORD) are hidden and unknown for guests. Guests can't elevate (Rob): `cmdBye` doesn't check the argument for a guest, so `BYE <password>` is a plain logoff and nothing counts toward a ban. Busy-line sysop elevation is unaffected (the busy line is not a guest).
- Input effects (0.7.0): the handle, password and command editors use `F_STAY` (Enter does not move to a new line). A rejected handle or unknown command rubs out, flashes the reason in place (`inputError`, short text on 40 columns) and re-arms on the same line; known commands print their own newline first. Passwords: spinner, rub out the stars, `ACCESS GRANTED` in place; denied flashes and clears for a retry on the line. Masked input shows at most 24 stars so it never wraps on a C64. Pages and broadcasts: bell, flashing tag, rub out, message. Lists and MEM/TERM/TIME open with `rowTitle` (reverse bar on ANSI/PETSCII, dashed on ASCII) and lists close with a rule.
- Staff rank on accounts (0.8.0, Rob): entering a staff password marks the caller's account (`users.txt` `level = user|co2|co1|sysop`, `markAccount()`), which drives the DDial-style marker between node number and handle in WHO, NODES, LAST (CallRec rank flags), DASH and the user manager, with a `*GUEST  >CO-SYSOP  ]SYSOP` key under each list. Staff may only manage accounts at their own rank or below (`mayManage`), and the form's `Level` field (new `FF_CYCLE` field type) offers their rank and below, so nobody self-promotes. Staff also see hidden and lurking sessions (marked `hidden` / `lurking` in the Doing column); the old masking of senior staff in NODES is gone.
- Staff Doing column (0.7.0): `Session::doing` holds the verb of the last dispatched command (never arguments, never BYE, never unknown input, so a mistyped password can't show). WHO shows it instead of Terminal for staff with `NODES`; DASH always. Plugins can set it later for doors.
- Input backpressure: a session's socket is only read, and held keys only fed, while its timeline has `BBS_RX_ROOM` (1 KB) free. Form redraws (~1.2 KB on PETSCII) overflowed the old 2 KB timeline when keys were typed ahead; `BBS_TL_BYTES` is now 3 KB.
- Workflow: commit and push after every flashed build. COMMANDS.md, README.md, CHANGELOG.md and this file are updated in the same change. CLIENTS.md holds the full list of machines that can call in and README.md carries the short version of the same list: change one, change the other. Code review at phase checkpoints; a robustness/pen test of the live board before any internet exposure (tabled for now).

## Processes

These are the standing rules about who does what. They are not preferences,
they are the process, and getting them wrong wastes Rob's time.

- **Rob flashes the board. I never do.** I build, test on the host, report the
  exact command and what to expect, and stop there.
- **Rob deploys the directory server. I never do.** The process is: the change
  goes into the `unleashed_directory` repo, pushed; Rob runs
  `sudo /srv/unleashed_directory/deploy/update.sh` on the droplet. I have no
  SSH access to it and am not to go looking for a way in. "Get it on the
  website" means "get it into the repo", not "connect to the server".
- **Tests and subagents stay on 127.0.0.1.** Never send traffic at the live
  board or the live directory unless Rob asks for an on-board test. My
  "external" checks once ran over his own LAN and every result was hairpin
  NAT, which sent him chasing a router problem that did not exist.
- **Bump `BBS_VERSION` with every change that gets committed** (Rob,
  2026-09-20). The patch number is how a board says which build it is
  running, so two commits must never report the same version. This went
  wrong the day it was written down: three separate commits all said
  0.17.3, and the only way to tell which one was on the board was that its
  HELP had no DOWNLOAD in it. A version that does not identify a build is
  worse than no version, because it is trusted.
- **Commit and push after every build**, and again at checkpoints during long
  work so there is a history to go back to. COMMANDS.md, README.md,
  CHANGELOG.md and this file are updated in the same change, not afterwards.
- **Never commit** `include/secrets.h`, `data/system.cfg`, `CLAUDE.local.md` or
  `data/calls.log`. No passwords in git history, ever.
- **Proposal before new code.** Confirm the approach, get a go-ahead, then
  write it. A bug I introduced myself is still a proposal, just a short one.
- **Batch board work, do not drift into it.** Rob asks for board features while other work is in flight. They go in the queue below, and they get built together as one version with one regression run and one flash. Wandering off to implement or investigate a queued item mid-task is how a session ends with six half-finished things and nothing flashed.
- **A web change is not verified until the rendered page has been checked.** grep on the HTML proves a string is present, not that a stylesheet applied, an element is positioned, or a menu is readable. Fetch the page and check the CSS rules actually reached it; if a change is visual, say plainly that it has not been looked at rather than implying it has.
- **A patch script that gets replaced takes its edits with it.** Rewriting a scratchpad script and rerunning it silently drops anything the earlier version did. Either re-check the file afterwards or make each change once and confirm it landed. The nav stylesheet went missing exactly this way and shipped unstyled.
- **Use the agents.** `.claude/agents/` holds ten. They exist because each one encodes a bug that already shipped here, and running one is cheaper than finding those bugs the way they were found the first time.

| Agent | When |
|---|---|
| `bbs-regression` | before any commit touching `src/`, and always before telling Rob a build is ready to flash |
| `web-regression` | before any commit touching the directory server |
| `bbs-qa` | a change a caller can see: screens, the terminal layer, lists, the shell. It dials the host build and reads the bytes, which is how `??nleashed` and a Ctrl-L that never worked were found |
| `web-qa` | a change a reader can see. grep on HTML proves a string is present, not that a stylesheet applied |
| `code-review` | anything non-trivial, before the commit. It hunts the shapes that have actually shipped here: a partial thing treated as whole, a guard bounding the wrong quantity, a fix that closes the door on the legitimate case |
| `docs` | **developer-facing writing.** README, COMMANDS, USERS, PLUGINS, BACKUP, SCREENS, CHANGELOG, PROTOCOL, the design history. Its rule is to check every claim against the source, not against other prose |
| `optimize` | RAM or flash is tight, or before committing to a feature that needs room. Reads, measures and researches, then writes **one report** to `reports/` and changes nothing. Its first rule is not to trust PlatformIO's RAM percentage: this board read 54.9% while being over the DRAM limit |
| `explain` | **human-facing writing.** The website, and the board's screens: `welcome`, `rules`, `privacy`, `newuser`, `chatin`, `goodbye`. Researches current facts on the web first, because router menus and client software move. Writes for somebody who does not know what telnet is |
| `tty-ux` | **design consultant**, terminal screens and the website both. Judges whether a layout reads as designed or accidental, and specifies the fix in real units: columns and characters, or px and ch. Measures at 40, 80 and 132, or at 1920, 1366 and 390. Writes **one report** and never code. Added 2026-09-20 because the board draws for 40 columns everywhere and never re-measures |
| `screen-artist` | **draws the screens.** ANSI/CP437 at 80x24, PETSCII at 40x25, plain ASCII. Writes screen files and `tools/mkscreens.py`, never `src/`. Its rule is to render what it drew back to a character grid with a ruler and read it: an unterminated colour run and a frame one cell out both look perfect in a hex dump |

  The two writing agents split by **audience, not by importance**. If a person
  reads it, `explain` owns it. If a developer reads it, `docs` owns it.

  Neither of the design pair writes BBS source. `tty-ux` specifies and
  `screen-artist` draws; anything either of them finds that needs a code
  change comes back as a hand-back, not a patch.

  **Gate parallel agents on runtime resources, not on code areas.** This was
  learned the hard way and it is the standing rule. Two agents editing
  different files still collide if they both stand up a board, because the
  old scripts each did `pkill bbs_host` and bound port 6400: two runs at once
  killed each other's board mid-test, and the symptom was a broken pipe that
  reads exactly like a bug in the code under test. `tools/harness.sh --tag
  NAME [--card]` is the fix. It derives the port from the tag, gives the run
  its own data directory, card and directory port, and matches its `pkill` on
  the data directory rather than the process name. Anything that binds a
  socket gets a tag; a web agent gets an explicit port in its prompt.
- **Every milestone gets a fresh optimization report** (Rob). When a block or a version reaches its regression run, send the `optimize` agent off as part of that run and put its report in `reports/`. Dated, one per milestone, kept. The point is the trend as much as the findings: a figure that has quietly grown by 2 KB a milestone is invisible in any single report and obvious across four, and the cheapest time to notice something is eating the budget is before it matters. The report is also what makes a size decision reviewable rather than remembered.
- **Verify before asserting.** Claims get checked against the source or a
  primary reference first. Stale warnings and confident wrong answers cost
  more than saying "I do not know yet".

## Phase plan

| Phase | Scope | State |
|---|---|---|
| C1 | listener, nodes, detection, screens, editor, effects, shell | done, verified on C64 and PuTTY |
| C2 | user accounts, auth, lockout, self-registration | done (0.6.0/0.7.0, registration and guests verified on hardware): accounts, salted SHA-256, lockout, forms, user manager, guests; plus staff passwords, co-sysop matrix, IP bans |
| C3 | command registry (tree deferred) | done: `Command` tables, generated HELP, `registerCommands()` for plugins |
| C4 | message bus, WHO, PAGE, notices, BROADCAST, DND | done |
| C5 | plugin API, requirements check, diagnostics | done in 0.9.0: descriptors, hooks, config sections, levels, storage, session ownership, PLUGINS command, example plugin |
| C6 | network zones, bans, maintenance mode, OTA | partial: bans |

Also done: busy line, paging (`[More]`), abort keys, command history, time limits (per call, per day), caller log (`LAST`), NTP + TZ, mDNS, backup window, config reload without reboot.

## File subsystem UX (0.17.3, in progress)

- **The screen is cleared between views.** Entering FILES, going back to the
  area menu and opening an area each start fresh. Without it every view
  printed underneath the last one, so pressing a number three times gave
  three stacked listings and no sense of having gone anywhere. One exception:
  if `screens/files` played on the way in, the menu does not wipe it. Rob:
  "screen clears bro. Without them you just feel like youre in a bad layout
  not a gateway door to files."
- **The area menu is cursor-driven, with numbers as the fallback.** Cursor
  keys move a reverse-video bar, Enter opens what it is sitting on, and the
  numbers still work. Plain ASCII has neither a cursor nor reverse video, so
  it gets the numbers and the prompt says so; the same split the forms make.
  Drawing and the cursor keys both go through one `visibleAreas()` helper on
  purpose: when each worked the column count out for itself, the highlight
  could sit on a different area than the one that opened, and a caller reads
  that as the board being broken rather than as an off-by-one.
- **Two areas the board provides**, so a fresh card is not an empty room:
  `Screens` on the card's screen folder and `Logs` on its log folder, both
  staff read and sysop write. They sit at fixed numbers 9 and 10 above the
  eight configured ones, because giving them the first free slot would move
  them every time somebody added a folder. `0` means ten at the menu, or the
  Logs area would be reachable by cursor and not by number, and plain ASCII
  has no cursor. Logs only works because the log is mirrored to the card; the
  ring on the internal partition is not reachable from a file area and should
  not be.
- Fixed on the way past: the menu computed its column count from `widest + 6`
  while printing cells `widest + 14` wide, so a wide board packed four columns
  into a line that fitted three and wrapped every row. First instance of the
  wide-terminal habit to be found and fixed rather than specified.
- `screens/files` did not exist, so callers walked into a bare menu. Being
  drawn by `screen-artist` in all three flavours.

## Current state (0.17.1 Block C, host-tested, not flashed)

- Host: 438 checks with a card, 402 without, 0 failures. ESP32: flash 68.4% of the slot, static RAM 54.9%. No warnings on either target.
- **A plugin can produce a paged list.** `ListKind` was a fixed enum in the core, so a plugin with more output than a screen had to print straight into the timeline and either truncate it or outrun a slow terminal. `Bbs::startPluginList` hands the session to the core's list machinery and calls the plugin's new `rows()` hook once per line, so the plugin gets paging, the `[More]` prompt, the abort keys and the output backpressure rather than reimplementing any of it. Message bases need exactly this, so it is groundwork as much as it is file areas.
- `rowText`, `rowRule`, `rowTitle` and `rowWidth` moved from private to public for the same reason: a plugin drawing its own rows should produce rows that look like everything else.
- The `rows` hook is **appended** to `Plugin`, like everything after the line in that struct. The descriptors are filled positionally, so a field inserted in the middle silently shifts every existing one; the compiler caught it when the first attempt put it before `settings`, but it is a trap worth naming.
- **The `files` plugin.** An area is a folder the sysop mounts under a name: `area1 = pub/c64 | C64 Downloads`. The path is never shown to callers, so an area can point at a folder somebody already has, and a folder with no config entry is not an area, which is what lets a sysop keep their own files on the same card. Eight areas, the limit being the CONFIG form's sixteen fields.
- It is `PF_SD`, so on a board with no card it does not start and `FILES` is not a command at all. That is deliberate: a board that offered an empty file area on hardware that cannot hold files would be worse than one that does not offer the command.
- Descriptions are `FILES.BBS` in each folder, one line per file, the way every BBS did it. Read on demand rather than cached: a folder of two hundred files would be a 12 KB table on a board whose heap is in tens of KB, and rereading the file per row costs nothing that matters while a sysop editing it on a laptop sees the change on the next listing rather than after a reboot. Written back through temp file, flush, rename, because FAT is not power-fail safe and this is the only thing the board writes to the card.
- The listing's description column follows the terminal: 18 characters at 40 columns, 58 at 80. The first cut used the narrow case for both, which is meaner than any real BBS managed and wastes width that is sitting there.
- A directory handle is never held across a row. Sixteen callers cannot hold sixteen open `DIR` handles against a FATFS budget of twenty, and a handle held across a page break is held until somebody presses a key, which may be never. The folder is reopened and walked to the wanted row each time.
- **The caller log is mirrored to the card, not moved.** The ring on the logs partition stays the record `LAST` reads, because it is the sysop's security log and must not depend on a card being seated. The card gets a plain text copy, one file per month, tab separated so a spreadsheet opens it and a human can still read it. A failed mirror is quiet after the first complaint: a caller hanging up must not be held up, and a pulled card should not produce one console line per call.
- `MEM` shows the card's free space in megabytes when one is mounted.
- **A sysop sets an area up from the board, not from a PC.** Typing a path into CONFIG creates the folder on the card, parents and all, when the plugins restart on save. Without it, configuring an area meant pulling the card, finding a PC, making a folder and putting the card back, which is a strange thing to ask of somebody already logged in as the sysop of the machine that owns the card. A typo makes a junk folder rather than an area that silently does not work, and of those two a junk folder is far easier to notice.
- Two test bugs worth remembering, both of which reported working code as broken. A paged screen eats the next command, so `PRIVACY` followed by `SD UNMOUNT` fed "s" and "d" to the screen as page keys. And **the sysop node holds one caller**: a second session sending `BYE <password>` while somebody is already on it is refused and dropped, which is correct and is not what a test expects unless it was written knowing that.

### The file subsystem (PLAN-FILES.md phases 1 to 4, and the groundwork for 5 and 6)

- **A CONFIG page can open a sub-page.** `FF_ACTION` is a button field and `Form::Res::Open` says which one was pressed; `CONFIG files` shows each area as a button carrying its name, and opening one gives Path, Name, Read and Write as proper fields with the levels as cycle pickers. Save returns to the parent page. Plain ASCII has no cursor to put a button under, so it asks instead: `Area 1 [C64 Downloads] open (y/N)?`.
  The trap that was worth getting right: a caller dropping the line two pages deep must not leave the CONFIG editing guard held, and cancelling a sub-page must not release it, because the sysop never left CONFIG.
  Worth knowing: **saving an area pins its levels.** The sub-page seeds Read and Write with what the area is effectively running under, and Save writes all four parts, so an area that was inheriting the plugin's levels stops inheriting. Deliberate, and documented, but inherit-by-default would need a seventh rung on the ladder meaning "the plugin's".
- **Fixed in passing, and it was live:** `g_cfgWas[i]` was filled with `"%.47s"` into a 96 byte buffer, so any CONFIG value longer than 47 characters always compared unequal to itself and was rewritten on every page save whether or not it had been touched. The 96 byte buffers exist precisely for long values such as announce's comma-separated `servers`.

### File transfer: phases 4, 5 and 6, built 2026-09-20

- **XMODEM, XMODEM-1K and YMODEM.** YMODEM landed in 0.17.5 and is the
  default in both directions; XMODEM is there for terminals that only speak
  it. `DOWNLOAD <file>` is YMODEM, `DOWNLOAD <file> X` is XMODEM, bare
  `UPLOAD` is YMODEM and takes the name off the wire, `UPLOAD <file>` is
  XMODEM and needs the name because XMODEM has none.
- **Why YMODEM was worth it, in one line: the length.** XMODEM pads its last
  block with 0x1A and the engine refuses to strip that, correctly, because
  0x1A is legal inside a .PRG and a receiver that guesses truncates somebody's
  file. Block 0 carries the exact byte count, and the spec is explicit that
  the receiver "stores the specified number of characters, discarding any
  padding". The test asserts both halves: the same file comes back padded
  under XMODEM and byte-exact under YMODEM, so the test cannot quietly agree
  with a broken implementation.
- **A name arriving in block 0 is untrusted input** and goes through exactly
  the checks a typed one does, plus "already live here" and "already waiting".
  A name over 63 characters is refused rather than shortened: a file written
  under a name nobody chose is worse than a transfer that plainly did not
  start.
- **A real XMODEM bug fell out of writing the YMODEM loopback test**, and it
  was always reachable. An ACK is lost; the sender times out and resends on
  its own clock; the receiver, having timed out on the same pass, already has
  a NAK on the wire for the same block. The sender acted on that NAK, sent a
  third copy, collected a second ACK for one block, and was **one ACK ahead of
  itself for the rest of the file**, dying with `Err::Sequence` near the end
  after all the data had arrived. Whether the two timeouts land on the same
  pass is pure alignment, which is why the existing fixtures never hit it.
  The fix is one bool, `selfNak_`, swallowing exactly one NAK per
  self-initiated resend, which extends a policy the code already stated in
  `Ph::SendData`. **Verified by disabling it**: three lost-ACK checks fail
  without it and 161 pass with it. The cost is that a genuinely corrupt resend
  waits for the next 10 s timeout instead of going again at once. Slower
  recovery, not a lost file.
- `sizeof(Engine)` went 1128 to 1208 on the host, about 1204 on the target.
- **One transfer at a time, board-wide**, which is the engine's own documented
  design. XMODEM is stop-and-wait, so a transfer is mostly idle, and a second
  engine would cost 1.1 KB of static RAM to overlap two things that are each
  waiting. A second caller is told to try in a moment rather than queued: a
  queue on a ten line board is a way of making somebody watch nothing happen.
- **The pump is sized against the room actually left, then halved.**
  `Term::raw` doubles every 0xFF for telnet and a timeline `put()` is
  all-or-nothing, so asking for more than fits drops a block silently and
  stalls the transfer with no error anywhere.
- A caller dropping mid-transfer takes the engine with them. Sessions come
  from a static pool, so a stale `Session*` would have been handed to whoever
  dialled in next and the board would have pushed a file at them.
- **`UPLOAD` had to be `CF_READ`, not `CF_WRITE`, and the test is what found
  it.** A command flag is checked against the *plugin's* levels, so tagging
  upload `CF_WRITE` meant a caller needed the plugin's write level merely to
  invoke it, and the per-area upload level could never be reached: an area
  configured to say "users may upload here" was unreachable for exactly the
  users it named. The command flag answers "may you use the file areas at
  all"; `mayUp()` answers "may you upload into this one". Same shape as the
  review agent's own third pattern, a guard bounding the wrong quantity.
- **The round-trip test is the point.** Everything XMODEM gets wrong is
  invisible to a test that only checks the board said "complete": a doubled
  0xFF, a CR eaten by telnet's line-ending rule, a block boundary off by one.
  `test_xfer` moves a payload built to trip exactly those, compares bytes, and
  drives a deliberately dumb XMODEM client rather than a library, because a
  library would paper over the same mistakes the board might make and then
  both ends would agree on something wrong. 22 checks.
- The harness gained `area5 = pub/drop | Drop Box | all | users | all | sysop`,
  which exercises the six-field format and gives an ordinary caller somewhere
  to upload. `area1` deliberately stays staff-only so the refusal is tested too.

### The upload bug, third and last: the board got bored waiting

- **The cause, found from Rob's own serial log rather than from reasoning.** The board polls `C` for CRC-16 every three seconds while waiting for a transfer to start. `kCrcPolls` was **3**, so after nine seconds it concluded nobody could do CRC and switched to asking with `NAK`, which means the 8-bit checksum. SyncTERM had committed to CRC-16 on the first `C` and went on sending 133 byte blocks; the board read 132 and checked an 8-bit sum against the first CRC byte. Every block of every upload refused, with both ends behaving exactly as specified.
  **Seventeen seconds of choosing a file in a dialog was the whole bug.**
- **Why lrzsz could not find it, which is the lesson.** `sz` is adaptive: when the board dropped to checksum, `sz` followed it down and the two agreed. Reproducing it needed a deliberately *stubborn* sender that commits to CRC on the first `C` and never reconsiders, which is what SyncTERM and most real terminals do. **A reference implementation is necessary and not sufficient: a tolerant one hides the bug a strict one finds.**
- Reproduced at three delays, before and after: start immediately passes, start after 20 s fails with every block NAKed, start after 60 s fails with no start byte at all. After the fix all three pass.
- **The fix is three timing constants, no protocol logic.** `kCrcPolls` 3 to 40 (two minutes of asking in CRC, then a minute the old way for a genuinely checksum-only sender, which is exactly the kind of machine this board is for). A new `kStartPolls` (60) separates "how long to wait for a person to find a file" from `kMaxErrors`, which is the error budget for bad blocks on a live transfer and a different question. `kSenderWaitMs` 60 s to 180 s for the same reason on a download, where the caller is choosing somewhere to save.
  **Rob's number is the requirement: up to a full minute to find and select a file.** Anything that expires inside that is a bug, however correct it looks against a program.

### Transfers, and the two bugs the project's own tests could not see

- **The board never negotiated RFC 856 TRANSMIT-BINARY.** `Telnet::setBinary` set an internal flag and nothing else, so the link stayed NVT ASCII, where a sender transmitting a bare CR must follow it with LF or NUL. SyncTERM obeys that; the board no longer stripped the padding byte, which is what `setBinary` existed to stop; every block containing a `0x0D` arrived a byte long and was NAKed. Uploads only, because the board's own output does not pad, which is why Rob's downloads worked and both failing screenshots were uploads.
- **Then the fix had the same bug from the other side.** `setBinary` set the flag on the *request* rather than on the reply. A terminal that declines binary, or never implements it, keeps padding for ever, and the board had already stopped stripping. **Asking is not agreeing.** Only `WILL BINARY` from the far end flips the input path now.
- **Both bugs were invisible to `tools/testclient.py`, and for the same reason: it was written alongside the board and agreed with it.** It sent raw bytes and doubled `0xFF`, which is not what a telnet client does, and it then honoured the binary request, which a real terminal may not. Each time, every test passed while the hardware failed. This is the single most expensive habit in this project's testing and it has now cost two rounds.
- **`tools/lrzsz_check.py` exists so there is one test the board cannot talk itself into passing.** It drives `sz` from lrzsz, the reference XMODEM and YMODEM sender, over a pty with a telnet layer in between, and it runs four ways: XMODEM and YMODEM, each against a client that agrees to binary and one that refuses. Needs `lrzsz` installed (`sudo apt-get install -y lrzsz`), so it is a manual check rather than part of the suite. Results that matter: all four pass, and the byte counts show the padding argument directly, 1152 received for a 1026 byte file under XMODEM against exactly 1026 under YMODEM.
  **The rule to take from it:** when a protocol is involved, test against somebody else's implementation. A client written next to the server tests that the two agree, not that either is right.
- `tools/testclient.py` now pads CRs like a terminal, with `_binary["refuse"]` to play one that will not do binary at all. `test_upload_no_binary` pins the second bug; verified by reverting the fix, which fails three of its checks.

### The file manager (Rob's design, 0.17.7)

- **FILES is a place and everything about files happens inside it.** `DOWNLOAD`, `UPLOAD`, `APPROVE`, `REJECT`, `ERASE` and `DESC` are gone from the shell entirely. Rob, on seeing them in HELP: "All this should live inside the file manager."
- **The design is Rob's and it is better than the one it replaced.** Mine put transfers on the command side and then bolted keys on, which meant the one place the files are was the one place you could not ask for one: the plugin owns the keys inside FILES, so "download GAME.PRG" was not a command there, it was nine keypresses, and the `l` in download means "L lists". That is what produced the stray listing I first blamed on a test.
- `[S1] Files>` names the section the way a node number is shown everywhere else. `L` lists numbered, a number picks a file and gets a dialog, `U` uploads, `D` describes, `P` shows what is waiting, `A` approves by number or `A` for all, `R` rejects, `E` erases, `?` is help on the screen it applies to.
- **Nothing takes a typed filename any more, and that fixed a real bug rather than being tidier.** `APPROVE <file>` acted on "the area you last opened", so a sysop looking at a list that showed `10 test.txt` in Logs, while standing in area 1, got "no upload by that name is waiting here". The leading number in that list was the *area*, which is exactly how it was read as a file number. Approval now happens while standing in the section, over that section's own numbered queue.
- **A number is also a security property, not just convenience.** It can only ever name a file the section has already shown, so `../../users.txt` is not refused, it is unreachable. The old traversal check was replaced with one that a number can still get wrong: a number with no file behind it.
- **A refusal has to survive binary mode.** A YMODEM receiver can only say no with a CAN, which the far end renders as "Canceled remotely" and nothing else. Rob's uploads were being refused as duplicates of files left in `.pending` by earlier failed attempts, and the board knew exactly why and had no way to say it. The reason is kept in `g_why` and printed once the line is readable again.

### Upload, approval and per-area permissions (Rob, 2026-09-20, agreed shape for phase 6)

- **Four levels per area, not two.** DONE. Before this an area had `read` and `write`, and
  `write` gates exactly one thing, `DESC`. Rob's point: when upload lands, one
  level would have to mean both "can pull files out" and "can push files in",
  which are not the same trust. So: `read` (the area appears and lists),
  `down` (download), `up` (upload), `del` (remove files).
  **`del` also covers approving and rejecting uploads**, because rejecting an
  upload and deleting a file are the same physical act, which keeps this at
  four levels rather than five.
  Config becomes `area1 = path | name | read | up | down | del`, which is
  the order `readKey` in files.cpp actually parses and the order
  `kAreaParts` lists. This line said `read | down | up | del` until
  2026-09-21, which would have had a sysop following it set the download
  level where the upload level goes. Old four
  field lines still parse: `write` becomes `up`, `down` defaults to `read`,
  and `del` defaults to the plugin's admin level rather than to `up`, so it
  fails shut.
- **An upload lands unapproved and goes live only when staff say so** (Rob):
  upload, then approval, then live, with a notice to co-sysop 2 and above on
  login.
- **Pending files go in a `.pending/` subfolder inside the area, not in the
  area with a marker.** This is the one design decision here that is not
  cosmetic. A marker means every listing and download path has to remember to
  check it, and forgetting one check serves an unapproved file: fail-open. A
  staging folder means an unapproved file is simply not in the area, so the
  existing listing and download code needs no changes at all to keep it
  invisible. Approval is a rename on the same filesystem, cheap and near
  atomic on FAT, and a power cut mid-upload leaves junk in staging rather
  than in the public area. It is still on the card with the files and still
  readable on a laptop, which is what Rob asked for.
- `UPLOADS.BBS` in the staging folder carries who and when, tab separated the
  way the caller log mirror already is: `name<TAB>handle<TAB>epoch<TAB>desc`.
  Appended on upload, the line removed on approval; approving moves the file
  and appends its description to the area's real `FILES.BBS`, which is
  machinery that already exists.
- The pending count is held in RAM, counted at plugin start and on mount,
  incremented on upload and decremented on approve or reject, so a staff
  login can say "3 uploads awaiting approval" without touching the card. Same
  shape as chat's "you have mail", which exists precisely so knowing costs no
  reads. A card edited externally goes stale until the next start or mount,
  which is the accepted trade.
- `UPLOADS` is the command, staff only, drawn with `startPluginList` and the
  cursor selection built in 0.17.3: area, name, who, when, size, A to
  approve, D to reject.
- **Decide the caps before building, not after.** Uploads are the first thing
  on this board a caller can use to consume unbounded resources, so a per
  area cap and a per caller rate limit belong in the first version or the
  first person who finds it fills the card. Same concern as the feedback
  plugin.

### The file subsystem, the rest

- **`FILES` is a place, not a command.** It takes the session the way `CHAT` does, plays `screens/files` if the board has one, and shows the areas as a numbered menu in as many columns as the terminal has room for. A digit opens an area, `Q` goes back one level, `Q` again leaves. That distinction, back one level rather than all the way out, is what a subsystem has that a command does not.
- **A paged list can now be started from inside a plugin that owns the session.** A finished list called `prompt()`, which dropped the caller at the shell without saying so; `listEnded()` hands the session back to the owning plugin instead. Message bases need the same thing, so it is groundwork twice over.
- **Two binary-safety holes closed before the transfer phases**, both found by the code that will depend on them rather than by a test:
  - `Telnet` normalised CR on input, silently deleting a `0x0A` or `0x00` that followed a `0x0D`. In a file that is data, and dropping it corrupts the file. `setBinary(true)` keeps IAC handling and leaves CR alone; `setEnabled(false)` is not a substitute because it also stops IAC being unescaped, so every `0xFF` would arrive doubled instead.
  - There was no outbound path that escaped IAC without also translating the charset. `Term::text` and `Term::ch` both translate and `ByteSink::put` escapes nothing, so neither could carry a file. `Term::raw` does exactly one thing: doubles `0xFF`, touches nothing else.
- The XMODEM and YMODEM engine is written and tested standalone: 82 checks, clean under ASan and UBSan, 2,696 bytes of code on the target, one 1K buffer. It knows nothing about sockets, sessions or the card, and deliberately does no IAC escaping, because that belongs to whatever owns the socket.

### Mail: reading is not disposing (0.17.12)

- **`MAIL` showed a message and cleared it in the same breath.** A caller whose line dropped mid-read, or who was paged, had lost it with nothing to go back to. Reading now shows the message and asks `[R]eply  [S]ave  [D]elete:`, and nothing is touched until a key answers. Rob's words on the version before this one: "I cant believe you thought that was ok."
- **Each choice is one rewrite of the mailbox through a temp file and a rename**, so a power cut leaves the message alone or leaves the decision made, never half of it.
- **A reply retires the message it answers in the same rewrite**, which is why R, S and D are one choice rather than things somebody does in turn. Two steps would mean picking which way to fail: delete first and a refused reply has thrown the original away; send first and a failed delete leaves them answering the same message again. `mailSend` took a `dropIdx` and started returning whether it stored anything. A reply also takes the slot its original gives back, so a board whose mail is full can still be replied to.
- **Save is `MF_KEPT`, not a copy.** It stays in the box, stops ringing "You have mail", and still counts against the limit because it is still taking up room. That meant `mailWaiting` and the room's own notice had to count *unread* mail rather than anything addressed to you, or S would ring forever and be the wrong choice for the one thing it exists for.
- **One predicate was answering two questions, and they had quietly stopped being the same one.** `Bbs::owns(s, g_index)` meant "is this caller in the chat room" everywhere in the plugin, which held right up until `MAIL` at the shell started borrowing the session to read single keys. Split into `joined` (counted in the room, listed in `/s`) and `listening` (ready to be shown a line right now). Without the split, somebody reading mail at the shell prompt is counted in the room and sent every line anybody types. Worth watching for generally: a plugin that owns a session for two different reasons needs to say which.
- Room lines are **held** while a caller decides and flushed by `mailDone`, reusing the path that already holds lines for somebody part way through typing. Nothing is lost and nothing lands across the prompt.
- **A static assert that was wrong about its own record, and failed correct code.** The `MailRec` guard summed the field widths to 563 and compared it with `sizeof`, which is 564: it missed the two bytes of padding that have always sat between `from[]` and `at` at offsets 42 and 43, and which the new `flags` and `spare` now occupy. The record did not grow, the on-disk format did not move, and the assert still fired. It checks `sizeof(MailRec) == 564` and `offsetof(MailRec, at) == 44` now. **A file format is offsets and a total size, not a list of field widths**, and an assert written the other way fails exactly when the layout is right.

### Identity: a handle is a name, not a person (0.19.0)

- **The bug that forced this was live, not theoretical.** `USER DEL` removed the block, which put the handle back into circulation, and mail is matched by handle, so the next person to register that name was handed the previous owner's undelivered mail. **The suite asserted it as correct behaviour** ("deleted handle is new again"), which is exactly how it survived: the test agreed with the code because both were written from the same wrong idea. Rob's instinct found it from the outside, asking what happens when a handle changes.
- `UserRec::id`, 32 bit, assigned once, **never reused**. The next id is derived (highest plus one) rather than stored, which is correct by construction **and only correct because accounts are never removed**. The two rules hold each other up: not removing is what makes a derived counter safe, and a derived counter is what makes not removing pay for itself.
- **`USER DEL` retires.** The block stays, the handle is reserved for ever, nothing is orphaned. `users::purge()` blanks what the board knows about a person while keeping that user 42 existed, which is the honest answer to "make them gone" and the same argument the mail history already settled.
- **A rename carries what is filed under the old name.** New `onRename(old, new)` plugin hook; chat rewrites `mail.dat` and the room ban list. Before it, renaming hid a caller's own unread mail from them and **walked them out of a room ban**, and neither failure said anything.
- **Staff access remembered for a week, bound to the address it was confirmed from.** The binding is the point, and it is specific to this board: **account passwords cross a telnet BBS in the clear on every login**, so remembering staff rights against the account alone would turn a sniffed account password into a week of staff access. The sysop level is never remembered. No valid clock fails closed.
- **A deliberate deviation from the plan, and the reasoning is the useful part.** The plan had mail and bans moving to ids. `MailRec` is a fixed-size record with a static assert on its layout, so that means changing `sizeof` and converting every live mailbox, on the one board that exists, to fix a bug that has a cheaper fix. Following renames gets the same visible outcome with no format change. The forums will store ids natively, so mail ends up the only holdout and a far smaller job later. **Prefer the fix that does not migrate somebody's data when both fixes close the same hole.**
- **The positional-descriptor trap caught me exactly as CLAUDE.md predicted.** `onRename` inserted before `onBytes` shifted every field after it, and the compiler said so. Append-only is not a style rule here, it is the only safe edit.

### A stopped listing has to hand you back (0.19.1)

- **The core draws no prompt when a plugin owns the session, and that is
  correct.** `listEnded()` gives the caller back to the owning plugin
  precisely so the plugin can decide what the screen says. The file manager
  met that by printing its prompt as the last *row* of the listing, which is
  the same thing right up until somebody presses `Q` at `[More]`: the row is
  never reached, so the caller sat looking at "Stopped." with nothing saying
  the file areas still had them, and every key afterwards went to a
  subsystem they could not see.
- `listDone(Session&, bool aborted)`, **appended** to `Plugin`, dispatched
  from `Bbs::listEnded(s, aborted)`. Only on an abort: a listing that ran to
  the end has already drawn its prompt, and a second one is a blank line and
  a repeat.
- **The shape worth remembering is the one this shares with the mail bug in
  0.17.12.** A plugin that owns a session for two different reasons has to
  say which, and a core that hands control back without saying why leaves the
  plugin guessing. Both were fixed by making the distinction explicit rather
  than by inferring it.
- **Invisible to every test that reads a listing to the end**, which is why
  it survived. The test that finds it has to abort deliberately, and it also
  has to reach a listing long enough to page: the first version of
  `test_list_abort_returns` fell back to an empty area and reported SKIP
  while looking like a pass. It elevates to sysop and uses the Screens area
  now. It also needs a card, because `files` is `PF_SD` and does not exist on
  a cardless board; without that guard it failed the no-card suite for a
  reason that had nothing to do with what it tests.

### The stall was Wi-Fi power save, and how it was finally caught (0.18.0)

- **`esp_wifi_set_ps(WIFI_PS_NONE)` was on the line after `esp_wifi_start()`, and never took.** Starting the station raises `WIFI_EVENT_STA_START`, whose handler calls `esp_wifi_connect()` at once, so the call raced association. Its return was unchecked, and nothing re-applied it after a reconnect, which with `CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE` leaves the board on the IDF default `WIFI_PS_MIN_MODEM`. It lives in the `IP_EVENT_STA_GOT_IP` handler now, where it cannot race and runs on every reconnect, with the return checked.
- **The measurement that settled it: ping the board with no callers on it.** ICMP is answered by lwIP on core 0 and never touches the BBS loop, the timeline or the card, so a stall that reproduces with nothing running is not the application. Median 13 ms, p90 **1003 ms**, seventeen slow samples inside a 31 ms window at exactly 1.00 s, against a gateway that never exceeded 1 ms. **A tight cluster on a round number is a timer; interference gives a smooth tail.** Keep the board busy and every spike disappears.
- **A strong RSSI reading sent everybody the wrong way**, including me. Power save is not a signal problem, and -35 dBm is entirely consistent with a station that is asleep.
- **`DASH 1` was where it showed up because DASH goes quiet.** It sends a frame and then deliberately says nothing for a second, which is exactly the gap that lets the radio doze. The subsystem that looked broken was the one that idles the most.
- **I got the cause wrong first, confidently, and shipped the wrong explanation in a comment.** I measured that a DASH frame was 4485 bytes at 132 columns against a 3072 byte `BBS_TL_BYTES`, concluded the frame was dropped whole, and wrote that into `rowEnd` as fact. It is not what the code does: `serviceWatch` draws row by row while the timeline has 512 bytes free and starts a new frame only once the old one has drained, so frames are built across passes. A 2957 byte frame arrived intact every time for eighty seconds and keypress latency during it was a flat 31 ms. **The arithmetic was right and the mechanism was invented**, which is the dangerous combination, because the numbers make it persuasive. The padding fix is worth keeping as bandwidth; the comments now say so and say what actually stalled callers.
- **`CONFIG_ESP_TASK_WDT_PANIC` is not set**, so the task watchdog warns on serial and never reboots. A wedged board stays wedged until it is power cycled. That is the concrete gap behind the "Watchdog" item already in the queue.
- **`cmdMem` calls `plat::sdInfo()` directly and bypasses the sd plugin's three second cache**, so every MEM does a real `esp_vfs_fat_info()`. Measured at 15 to 25 ms typical and 160 ms worst on a real card. Small, real, and not what anybody was chasing.

### Where a caller lands, and the room's own voice (0.18.0)

- **A caller chooses where login puts them** (Rob). `Start` on the account form, `Main | Chat | Bulletin | Default`, with `landing` in CONFIG as the board's default. `Default` is 0, which is exactly what every account written before this parses as, so nothing needed converting and a board that changes its mind moves only the people who never expressed a preference.
- **A landing the board cannot do falls back to the main prompt, silently.** `users::landVerb()` names the command and `findCommand()` decides whether it exists for this caller, so `Bulletin` could be offered before the bulletin plugin is written and 0.19 needs no landing changes, only the plugin. It is also the right answer for a board with chat switched off: nobody should meet "Unknown command" because of a preference they set months ago.
- **`[H]ELP for commands.` moved out of the login banner.** It is advice about the main prompt and nowhere else, so printing it to somebody about to be dropped into the chat room was directions to a place they were not going.
- **The room marks its own voice with `-->`** (Rob, DDial/Gtalk style). The room has no prompt character, so `No such command` read exactly like somebody typing those words. `color_marker` themes it.
  Not on everything, and the exclusions are the design: `***` join and leave notices are events rather than answers and already read that way; the welcome screen is artwork, not the board talking; the room command list gets it on the heading only, because an arrow on all sixteen rows turns a table into a wall.
- **One change at `tell()` carries it, gated on `joined(s)`.** `MAIL` at the shell and `/e` in the room are the same code printing the same sentences, and only one of the two is somebody standing in a chat room. Deciding once, at the one place every system line already went through, is also what stops forty call sites drifting apart. This is the `joined`/`listening` split from 0.17.12 earning its keep a second time.
- `/s` prints on the way into the room, split out of `who()` so joining and `/s` can never show different things.
- **The Screens file area was empty on every board, and it was not a file-area bug.** The area points at the screen *override* folder: consulted before flash at playback, never written to by anything. `sd` now seeds the stock set onto the card at mount, gaps only, never overwriting, and never writing flash. A failed copy removes its own half-file, because a truncated screen on the card would override the good flash copy and play as line noise until somebody noticed.
- **Adding one form field broke registration in three tests**, each of which walked to `[ Save ]` by counting Enters. The suite reported it as "bad email refused", which is three steps from the cause. `to_save()` presses until the board gives a verdict. **A test that counts fields is a test that breaks the day somebody adds one**, and the failure never points at the field.

## Previous state (0.17.1 Block B, flashed by Rob 2026-09-19)

- Host: 392+ scripted checks in two modes, no card and with one (`BBS_SD_DIR` set). ESP32: image 1044.7 KB (68.0% of the slot), static RAM 174.6 KB (54.8%). No warnings on either target.
- Flash went 63.9% to 68.0%: FATFS, the SD protocol layer and the SPI host cost 62.9 KB, and they cost it on a board with no card too. Static RAM only moved 652 bytes, because FATFS takes its work area from the heap at mount.
- `plat::sdMount` / `sdUnmount` / `sdBase` / `sdInfo` are the new platform calls. SPI rather than SDMMC: SDMMC is faster but nailed to fixed pins, two of them strapping pins, and wants pull-ups a cheap breakout does not always have. SPI works on any four free pins and on every module somebody following the wiring page will actually have. A BBS moves kilobytes; the card is never the slow part of a 2400 baud illusion.
- **Mounting blocks and nothing hides that.** An SPI card negotiation is a few hundred milliseconds of synchronous work, so it happens at plugin start, before any caller exists, or when a sysop types `SD MOUNT` and is told the board will pause. Nothing polls for a card on a timer, which also means there is no insertion event: there is no card-detect line on this wiring, and probing the bus to find out would be the same stall repeated forever. The host stub is fast, and its comment says so, because a host stub that is quicker than the real thing is how a blocking call ends up somewhere it cannot be.
- `format_if_mount_failed` is false on purpose. A card that will not mount is far more often somebody's card with their files on it than a card that wants erasing, and wiping it to make an error message go away is not a decision firmware makes on a sysop's behalf. The three failures get three different messages, because "no card", "not FAT32" and "miswired" are three different evenings.
- Speed is capped at 20 MHz rather than the driver's 40. Dupont jumpers to a breakout are an unshielded bus, and a card that enumerates at 40 and then corrupts a read under load is a worse evening than one that is slightly slower.
- The card's screens override the stock set per file, so one custom screen does not mean supplying all of them, and pulling the card falls back rather than losing them. `sdScreensDir()` is declared in `screens.h` and defined by the plugin: the core asks "is there another place to look" without knowing plugins exist.
- `PF_SD` used to mean "refused until the SD card lands". It now means "this plugin's files live on the card", and such a plugin gets `<sd>/p/<name>/`, is refused when no card is mounted, and has its `storageBytes` weighed against the card rather than against a flash partition it will never touch. No fallback to internal flash: a plugin that quietly writes somewhere other than where it said would leave a sysop pulling the card and finding nothing on it.
- `PF_EARLY` is new and exists for exactly one situation: `plugins::begin()` runs those plugins first, because whether a `PF_SD` plugin may start depends on `sd` having already had its go at mounting. Registry order would have given the same answer today and a different one the first time somebody tidied the list.
- **Bug found by the new plugin, not caused by it:** `tableCount_` was only reset in `Bbs::begin()`, never on a config reload. A reload stops the plugins and starts them again, each registering its command table on the way up, so the table grew by the number of running plugins every time until it was full; from then on whichever plugins came last were running, shown as running, and answering "Unknown command" to their own verbs. It needed a reload to show, so nothing caught it, and at four plugins it had been quietly costing `announce` its commands. `dropPluginCommands()` now runs before the restart, and `kCommandTables` is 12. The regression test counts HELP rows rather than waiting for the overflow, because the overflow depends on how many plugins are compiled in: a duplicate row *is* the leak, and it shows on the first reload rather than the third.

### What the code review of Block B found

Run with the `code-review` agent before the commit, on the whole diff. Seven
findings worth the name, all fixed in the same change. Worth keeping because
each one is a shape that will come back.

- **A config save unmounted and remounted the card.** `start()` is not only the
  boot path: saving any CONFIG page stops and starts every plugin. So changing
  `idle_minutes` stalled every caller's line for a full SPI re-negotiation, and
  the comments in both the plugin and the platform header asserted that could
  not happen. `start()` is idempotent now and `stop()` leaves the card alone;
  the mount is board-level state, and only `SD UNMOUNT` or a reboot takes it
  down. A plugin's own lifecycle is not the same as the lifetime of what it
  opens.
- **Unmounting with a screen open on the card.** A `ScreenPlayer` holds a file
  for as long as the screen plays, and at a page break that is until the caller
  presses a key, possibly never. Unmounting under that leaves a descriptor into
  a torn-down VFS, and the slot is reused by the next mount, so the stale handle
  can come back as somebody else's file rather than as an error. `ScreenPlayer`
  remembers whether it opened from the card, and `Bbs::closeCardScreens()` runs
  before any unmount.
- **A failed mount left the SPI bus initialised with the old pins.** The next
  attempt got `ESP_ERR_INVALID_STATE`, which the code tolerated, so the card was
  probed on the old wiring. The sysop corrects a pin in CONFIG, sees the same
  "no card found", and concludes the card or the wiring is bad. This is exactly
  the loop the wiring page sends people into. Fixed with `g_busUp`, which also
  stops the code freeing a bus it did not raise.
- **`usablePin` refused GPIO 34-39 for MISO.** Those pins are input-only, and
  MISO is an input: 36 is a good MISO pin and is what those pins are for. The
  `PluginSetting` row already allowed 0..39 for it while the validator did not,
  so CONFIG accepted 36, wrote it to `system.cfg`, and the reload silently kept
  19 while the form went on showing 36. The rejection went to the serial
  console, which a telnet sysop cannot see. A guard aimed at the legitimate
  case, which is the third shape in the review agent's own list.
- **`BBS_SD_MAX_FILES` was 5 against 18 sessions.** The sixth caller to open a
  screen from the card got the flash copy instead, silently, because a failed
  open is indistinguishable here from a file the card does not have. It is
  `BBS_MAX_NODES + 4` now, about 11 KB of heap at mount.
- **Screen lookup was directory-major, so a lower-preference format on the card
  beat the right format in flash.** Dropping one `welcome.asc` on a card to try
  the override would have taken every C64 caller off `.p40` and every ANSI
  caller off `.ans`, and it would have looked like it worked. It is
  extension-major now: for each format in the terminal's order, card then flash.
  The first version of the test *encoded* the wrong behaviour, which is its own
  lesson about writing the test from the implementation.
- **`f_getfree("0:")` was right only by accident.** The drive number comes from
  the first free FATFS slot, so `"0:"` held because the card is the only FAT
  volume. `esp_vfs_fat_info(mount, ...)` reads the drive out of the mount and
  cannot be wrong. The old call also had a bad failure mode: on a card with a
  stale free-cluster hint, which is what pulling a FAT card mid-write produces,
  it scans the whole FAT, and `status()` is called from the DASH refresh timer.
  `SdInfo` is cached in the plugin for three seconds on top of that.
- Also: a paged list holds a flat index across the command tables, and
  `dropPluginCommands()` renumbers them, so a config save under a caller sitting
  at `[More]` would resume at the wrong command. Lists are ended on reload the
  way plugin-owned sessions already were.

## Previous state (0.17.0 Block A, flashed by Rob 2026-09-19)

- Host: 392/392 scripted checks (`tools/testclient.py --backup`), 0 failures. ESP32: image 981.8 KB (63.9% of the slot), static RAM 174.6 KB (54.6% of 320 KB). No app warnings.
- Block A of the 0.17.0 plan: the partition rebalance and sixteen nodes. Blocks B (SD card), C (file areas), D (16-node list fallout) and E (queued bugs) follow, with the regression run after D.
- The data region is the same 896 KB, redistributed: `logs` 32 KB, `userdata` 608 KB (was 128), `storage` 256 KB (was 736). `storage` held 18.5 KB of screens in 736 KB because it was sized when accounts lived there too, and the hundred account cap came from the 128 KB left over rather than from anything real. `storage` stays last so `uploadfs` can only ever reach the screens. Breaking layout change: one `pio run -t erase` before the first flash.
- Static RAM went from 117 KB to 174.6 KB. That is ten more sessions at ~5.7 KB each, and it drops heap free at boot from roughly 143 KB to roughly 78 KB. Worth measuring again once the SD card driver and FATFS are in, because FATFS wants a per-volume work area and SDMMC wants DMA buffers. If it gets tight the trade is nodes, not the card.
- Sixteen nodes broke how a node is written down. `nodeChar` returned `'0' + id`, so node 10 printed as `:` and node 11 as `;`. It is gone, replaced by `nodeName` ("16", for prose) and `nodeLabel` (" 1", "16", for a fixed column), both returning a three byte `NodeStr` by value; pass `.t` to printf-family functions, because varargs will not apply the conversion operator. Digits and not letters, because `nodeByArg` parses a node argument with `strtol`: the number in the list has to be the number you type.
- Every format string that gained a column gives one back, or a 40 column row becomes 41, wraps, and a refresh screen leaves its tail behind on every redraw. `NODES` was at 60 and 39 columns exactly, so Terminal and Handle each lost a character.
- The caller log had the same bug one layer down: DASH printed `'0' + (node % 10)`, so node 12 would have shown as "2". That is worse than a wrong glyph, because it names a different line.
- DASH could not grow a row per node. It is a refresh screen that redraws from home, so a frame taller than the terminal scrolls and corrupts itself. Its node block is now `kDashNodeRows` (6) rows plus a summary, busy lines first and free lines filling what is left: a quiet board looks the way it always did, a busy one spends its rows on callers instead of on "waiting for caller" sixteen times. WHO is the paged list that still shows every line.
- `tools/testclient.py` reads `BBS_MAX_NODES` out of `config.h` through a new `config_num()` rather than saying 6 in three places. The busy line tests had started failing for a reason that had nothing to do with what they test, which is how you train yourself to ignore failures.

## Previous state (0.15.0, host-tested, not flashed)

- Host build: 385/385 scripted checks (`tools/testclient.py --backup`), 0 failures. ESP32 0.15.0: image 1001 KB (63.7% of the slot), static RAM 117 KB. No app warnings.
- A plugin declares what CONFIG should offer. `Plugin` gained a `PluginSetting` table and a `setting()` hook, both appended so the existing descriptors compile untouched. Before this, a plugin's page was built from whatever keys `system.cfg` already carried, so announce's name, owner, description, host and servers were unreachable on a fresh board: the only way to set a board's name was to know the key existed and edit the file by hand. The hook supplies the running value, so a blank means "not set" rather than "set to something I cannot show you". Undeclared keys found in the file are still listed, so nothing added by hand is dropped on a save.
- Sizing that followed: announce declares nine settings, which with the four core keys is thirteen fields, so `Form::kMaxFields` went 10 to 16. A comma-separated directory list does not fit 48 bytes, so the CONFIG value buffers are 96; the form already scrolls a value wider than its box. About 1.7 KB of static RAM.
- The wordmark is a 6x12 face drawn with CP437 half blocks (0xDB, 0xDF, 0xDC). A half block fills the top or bottom of a cell, so a text row carries two pixel rows and a letter can have a 2px stroke and a shoulder, which three cells never could. `Term::cp437` already re-encodes for UTF-8 terminals, so one file serves both. The micro sign is set at an x-height on the shared baseline with its stem below, a real descender rather than a capital squashed to fit. PETSCII keeps the old 3x5 face: 40 columns, no half-block trick, and Rob is drawing that one himself.
- Three new screens, all optional (a board without the file carries on): `rules` plays on R before anything is typed, two pages because 24 rows is the small terminal; `newuser` plays once registration succeeds; `chatin` plays on joining the room. The encryption warning deliberately stays immediately in front of the password, because putting the rules between them would weaken the one thing it exists to do.
- `showScreen()` is the plugin-facing screen call and is deliberately not `playScreen()`: that one hands the session to the screen player and returns the caller to the prompt, which is wrong for a plugin that owns the session. It is bounded by the screen ending and by the room left in the caller's timeline.
- The send-off runs however a call ends. `goodbye()` and `hangup()` were two endings and only one played a screen; both go through `exitScreen()` now, and the line is held open for `BBS_EXIT_LINGER_MS` (5 s) after the last byte is away, because a screen followed immediately by a closed socket is one most terminals never draw. Deliberate exception: a caller who never logged in gets the terse message, because the busy line and a refused password have nothing to say goodbye to and a detection timeout means the terminal type is still unknown, so an ANSI screen would arrive as line noise.
- `kUsageCol` was 13 and the widest usage string, "ANNOUNCE TEST", is 13, so the longest command on the board was the one HELP truncated. It is 15.

## Previous state (0.14.0, host-tested, not flashed)

- Host build: 364/364 scripted checks (`tools/testclient.py --backup`), 0 failures. ESP32 0.14.0: image 1000 KB (63.6% of the slot), static RAM 114 KB. No app warnings.
- 0.14.0 splits the flash by who owns what is on it. The sizing was the giveaway: `logs` was 128 KB for a log that fills about 3 KB a day. It is 32 KB now, and the 96 KB plus a slice of `storage` became `userdata` (128 KB).
- What makes it work is the ordering rule that was already there. PlatformIO's `uploadfs` writes the LAST spiffs partition, so keeping `storage` last means a filesystem upload can only reach the screens. `flashall` stopped being destructive without needing a new flag or a prompt.
- On the protected side: `users.txt`, `system.cfg`, `p/<plugin>/` (chat mail, the room ban list) and the announce token. A sysop can now reflash a board without losing its directory listing, which was the specific thing that made updates cost something.
- `syscfg::seed()` copies the shipped `system.cfg` to `userdata` once on a blank board. Without it a fresh board has no sysop password and no way to ever have staff: a board nobody can administer. Verified end to end on the host (blank user dir, seed line in the log, `BYE <password>` reaching the sysop node).
- `ziparc::livePath` routes each restored file back to the partition it belongs on, so the backup zip format is unchanged and older backups still restore. Only the three staff passwords are redacted in a download, so the announce token survives a backup and restore too.
- This is a breaking layout change: moving a partition needs one full erase (`pio run -t erase`), because the old filesystem contents sit where the new ones go. After that erase, `flashall` is safe permanently.
- Four places in the docs and one in `pio_flashall.py` still warned that flashing wipes the accounts, and one claimed `uploadfs` erases the caller log, which had not been true since the log got its own partition. Stale warnings about data loss are worse than none: they are what a sysop reads before deciding whether an update is safe.

## Previous state (0.13.0, host-tested, not flashed)

- Host build: 350/350 scripted checks (`tools/testclient.py --backup`), also under ASan/UBSan. `--only=<name>` runs one test, which is how a single failure gets chased without an eight minute wait.
- ESP32 0.13.0: image 999 KB (63.5% of the slot), static RAM 114 KB. No app warnings.
- 0.12.0: nobody types a password uninformed. Registering warns that telnet is not encrypted, offers "Would you like to know more?", and Y plays `screens/privacy.*` before the form opens. Needed `SState::AskKnowMore` and `Session::pendingForm` (a screen that leads into a form rather than back to the prompt). `PRIVACY` replays it.
- 0.13.0 is the `announce` plugin: an opt-in ~200 byte JSON POST to a directory every few minutes. Off until switched on, sends nothing about callers, and `ANNOUNCE TEST` prints the exact payload. Non-blocking socket driven from `tick()`; DNS resolved once at `start()` while the board is quiet. The directory issues a token which the board writes back to its own config with `syscfg::write()`, and the reply's `X-Seen-Address` is how a board behind a changing address learns its public address, which makes this a rough DDNS as a side effect. `share_activity` adds 24 hour call and caller-minute counts for ranking, off by default.
- Trap worth remembering: on a non-blocking socket, `SO_ERROR` is 0 while a connect is still in progress, so writability has to be the test and the error check only means anything after it. Getting that backwards works on loopback and fails on a real network.
- The directory server is a separate repository (`rwmech/unleashed_directory`, GPL v2 or later): Python 3 stdlib, SQLite, one file, 39 self-test checks. One process serves three faces by Host header: the board list, the argument, and the API. Anti-spam is three hours of sustained heartbeats plus one automatic listing per address (per `/64` on v6), never an outbound probe, because a directory that connects to whatever a stranger posts is a port scanner with a public API. Tokens are anti-hijack only and the protocol says so.

## Previous state (0.11.0, host-tested)

- Host build: 327/327 scripted checks (`tools/testclient.py --backup`), also under ASan/UBSan (run the sanitizer server under `setarch $(uname -m) -R`).
- ESP32 0.11.0: image 987 KB (62.8% of the slot), static RAM 111 KB. No app warnings.
- Help is a set of menus. `Command` gained `Menu menu` and `uint8_t rank`; the table order is the display order, so "sorted by how often it is used" costs nothing at runtime. `?` shows Main (chat commands with rank < 20 are lifted onto it), `? chat|account|staff|sysop` show one section, `? all` walks them all (`Session::helpAll`, with `Menu::Hidden` as the marker for "everything"). Section title bars come from `rowTitle`, and `helpUsage()` prints the leading `[X]` shortcut in yellow inside the word. Only the first bracket group is a shortcut: `[n]` later in a usage string is an argument and stays literal.
- Row helpers: `rowSeg`/`rowEnd` build a coloured row while counting columns, so refresh screens still pad to the full width. `statRow`/`statNum` are the label + figure + dim note layout, with `fmtCommas` for thousands separators and `markColor` so a rank marker is the same colour everywhere.
- `SYS` and `CALLS` are staff screens (`ListKind::Sys`, `ListKind::Calls`). SYS needed `plat::netInfo()` (SSID, channel, RSSI, address; empty on the host) and `plat::micros()`. Scheduler timing is measured around the work in `tick()` after `select()` returns, not the wait: `loopAvgUs_` (smoothed 7/8), `loopMaxUs_`, `loopPasses_`, plus `callsBoot_` and `peakNodes_` counted in `openSession`. CALLS buckets the caller log into `callHours_[24]` in one pass before drawing.
- Chat gained the room command set, moderation and messages, all in `src/plugins/chat.cpp`. Squelch and away notes are per node (`g_squelch`, `g_away`), cleared on logoff and cleared out of everybody else's mask when a node drops, so a new caller never inherits one. Notices (anything not starting with `#`) are never squelched. The room ban list is a plain file in the plugin folder. The vote to kick only opens with no staff in the room and three or more callers, needs two thirds of everyone but the target, closes after 60 s from the plugin's `tick`, and can only remove somebody from the room.
- Messages are a fixed-size record file (`p/chat/mail.dat`) rewritten through a temp file; RAM holds only the addressees and dates (`g_mailTo`, `g_mailAt`) so "you have mail" costs no reads. 512 characters, 14 days, configurable. Documented plainly as not private.
- **Mail is never replaced (0.17.11). The old behaviour was wrong and Rob was right to call it out.** It held one message per account and a new one *replaced* an unread one, telling the sender "(replacing the one they had)" as though that were a feature. Somebody else writing to you destroyed your mail: that is losing data and reporting success, and no storage argument justifies it.
  Now: multiple messages per person, oldest read first, and a full box is **refused** with a message the sender can act on. **How full depends on where the mail lives**, which is the shape the mail phase always wanted: 3 per person on internal flash, which is shared with the accounts and has to survive, and 12 with a card, which has room and no reason to ration. 64 slots board-wide.
  Two things this needed underneath: `mailRewrite` drops one record by position rather than every record addressed to a handle, and `mailRead` reads the slot the index found rather than searching for the handle again, which would have read the same message for ever while the rest piled up behind it. Both were invisible while nobody could have two.
- Chat colours are settings, parsed by the new `colorByName()` in the terminal layer. `showLine()` re-parses a stored line (`#2:Daytona) hi`) into node, punctuation, handle and text instead of storing colour with it. The room buffer is one `calloc` in `start()` and a `free()` in `stop()`, sized by `history` (8..2000), falling back to the default if the board cannot spare it. That is the first heap allocation outside the backup path, and it is at plugin start, never in the loop.
- `CONFIG` is the sysop's settings manager (new `CF_SYSOP` command flag: sysop only whatever the `[access]` matrix says, because it can change the staff passwords). Pages are tables of `CfgField`; a plugin page is built from the four core keys plus whatever keys that section already has. Values come from the file first, then from the running config, so a blank never means "unset by accident". `syscfg::write()` rewrites `system.cfg` key by key, keeping comments, order and unknown keys, adding a missing section at the end, through a temp file and a rename. Only changed fields are written, passwords show as a mask and are skipped unless retyped, and one session at a time may edit (a static guard released on save, cancel and `closeSession`). Saving reloads the config and restarts the plugins, and any caller sitting inside a plugin is handed back to the prompt first.
- Docs: CHAT.md (the room and messages), CHANGELOG.md (every build, kept current), README gained "What it's for", "What can call in" and "Hardware integration".
- Disclosure at sign-up (Rob): pressing R warns that telnet is not encrypted and that the password must be unique, then offers "Would you like to know more?" (a Starship Troopers nod). Y plays `screens/privacy.*` and the sign-up form opens when the screen finishes, which needed `SState::AskKnowMore` and `Session::pendingForm` (a screen that leads into a form rather than back to the prompt). `PRIVACY` replays it any time. The screen is generated by `tools/mkscreens.py` in all three terminal flavours and is deliberately plain-spoken: the honest line is that this is conversation on a five dollar chip, and the only real risk is a reused password.
- Not yet on hardware: everything in 0.11.0.

## Previous state (0.10.0, built, not flashed)

- Host build: 226/226 scripted checks (`tools/testclient.py --backup`), also under ASan/UBSan. Layouts checked through a C64 screen model (in-place errors and passwords stay on one 40-column line, title bars 39 wide).
- ESP32 0.7.0: image 923 KB (58.7% of the slot), static RAM 104 KB; session 5,688 bytes. No app warnings. Flashed by Rob.
- 0.7.0 adds: guests, input effects in place, page/broadcast alerts, title bars and closing rules on lists, staff Doing column (WHO, DASH), Wi-Fi RSSI on DASH (`plat::wifiRssi`), `guest` and `guest_minutes` keys.
- Host build: 234/234 scripted checks, also under ASan/UBSan (run the sanitizer server under `setarch $(uname -m) -R`).
- ESP32 0.8.0: image 928 KB (59.0% of the slot), static RAM 107 KB. No app warnings.
- Measured on the board (0.8.0, C64 via TeensyROM, 1 node active): heap free 143,344, heap min since boot 118,268, largest block 110,592, session 5,596 bytes x 8. Sessions are static, so extra callers barely touch the heap; the gap between free and min is transient (screens, backup staging, Wi-Fi). This is the budget plugins are sized against.
- Verified on the board: the [R]egister / [G]uest / [N]ew handle prompt, N returning to the handle prompt, in-place ACCESS GRANTED, and the MEM title bar.
- Host build: 272/272 scripted checks, also under ASan/UBSan.
- ESP32 0.10.0: image 966 KB (61.4% of the slot), static RAM 109 KB. No app warnings.
- Plugin defaults (Rob): a descriptor carries `PF_ON` (runs without a `[plugin:name]` section, `enabled = no` turns it off) and its own default read/write/admin levels. Chat is `PF_ON` with write = all, so a fresh board has a chat room and everyone can talk; serial and example wait to be switched on.
- Chat has no prompt character (Rob): the cursor sits at the start of the line, DDial style. Lines never interrupt a caller mid-sentence: the room keeps a numbered ring and each session remembers the last line it saw in `Session::ownerData` (a per-session scratch word the owning plugin gets), so held lines flush on Enter, on an empty line, or when the caller backspaces to nothing. `/s` lists the room (`/w` still works). The room buffer is 48 lines so held lines are never dropped in practice; a caller whose held lines approach the buffer is interrupted (line lifted, room printed, line put back) rather than losing anything. Per-caller rate limit: 80 lines a minute with a burst of 8 (`rate =`, 6..600), a token bucket per node; only the caller who trips it is told.
- Chat line format (Rob, from the C64 screenshot): `#<node>:<handle><bracket> text`, where the bracket is `)` caller, `*` guest, `>` co-sysop, `]` sysop. No blank lines between messages: each screen erases its own input line, prints the chat line, then redraws the input line underneath, so a caller mid-sentence keeps what they typed. The chat editor uses `F_STAY` so a sent line is rewritten in its finished form.
- 0.10.0 ships the first two plugins. chat: one room, DDial style, the plugin owns each joined session and drives the ordinary line editor, so backspace and the terminal drivers behave as usual; incoming lines print above the caller's half-typed line, which is then redrawn; a short history (8 lines) replays to whoever joins; /w lists, /q or ESC leaves. serial: the device sits on UART2 (never the console, so flashing and the monitor keep working), pins/baud/format come from its config section and can be changed live with SERIAL SET; one operator holds the keyboard (write level, T takes a free seat) while any number of watchers with read see the same stream; a watcher whose output buffer is full is told how many bytes it skipped rather than stalling the device; 1 KB of scrollback replays on join. `plat::serial*` is the new platform call; the host build loops back (or opens BBS_SERIAL_DEV) so both are testable off the board.
- 0.9.0 is the plugin API (C5). A plugin is a static descriptor (name, version, needs, hooks, commands) in `src/plugins/`, listed in `registry.cpp`, switched on in its own `[plugin:name]` section. Levels are read/write/admin against the ladder all|users|staff|co2|co1|sysop, with commands tagged `CF_READ`/`CF_WRITE`/`CF_ADMIN` (untagged counts as write, fail shut). Hooks: start, stop, tick (250 ms), onConnect, onLogin, onLogoff, onKey. A plugin can own a session (`Bbs::own`/`release`, `SState::Plugin`), which pauses the idle clock but not the call limit; that is what the serial bridge and doors will use. Each plugin gets `<fs>/p/<name>/` (PF_CORE only) and the core keeps `BBS_FS_RESERVE` free. (0.17.0 changed what PF_SD means: see the current state section.) `plat::fsInfo` reports free space, shown in MEM, DASH and PLUGINS. Config keys must sit above the first section.
- 0.8.0 is the code-review build: staff rank flags plus every fix from the three-part review of the 0.6.0/0.7.0 accounts work. Fixed: a failed users.txt write no longer deletes accounts; an unreadable users.txt is never treated as empty (`users::lookup`); the per-handle lockout no longer evicts a live counter and the per-call try count is not reset by ESC; PROFILE and USER EDIT write onto a freshly read record; the password prompt re-reads the account (lock, reset or delete while typing now counts); read-only form fields cannot be typed into and sign-up re-checks the handle; a handle held by a caller mid sign-up counts as taken; held input no longer turns a cursor key into ESC; `BBS_RX_ROOM` is 1700 so a redraw is never cut; sign-up forms get their own 2/3 minute timeout and warnings on the status line; typed passwords are wiped at hangup; the refresh footer is clamped to the row width; users.txt values keep their spaces, over-long values and bad hashes are refused with a line number, unknown keys warn instead of rejecting; the backup download streams a snapshot of users.txt; users.txt may be up to 160 KB in the zip and is checked against the uploaded max_users.
- Verified on hardware at 0.7.0 (Rob): registration works, guest login works, DASH shows Wi-Fi RSSI and it follows the signal.
- Not yet reported on hardware: the user manager and forms on the C64 (cursor moves, reverse boxes, F1, left-arrow), title bars and in-place rubouts on the C64, the PAGE alert, password hashing time, heap with 6 callers.
- Code review of the 0.6.0/0.7.0 changes: done (three reviewers: storage, forms, login flow). Pen test skipped for this checkpoint by Rob. Known and accepted: an attacker can lock any known handle for 15 minutes with 5 wrong passwords (handles are public in WHO); revisit if the board goes on the internet.

## State at 0.6.0 (host-tested, shipped inside 0.7.0)

- Host build: 202/202 scripted checks. Every test login goes through an account (`login()` registers a new handle through the form).
- ESP32 0.6.0: image 919 KB (58.4% of the slot), static RAM 104 KB; session 5,672 bytes, pool 45 KB.
- 0.6.0 adds: accounts (`users.txt`, in the backup zip, validated on upload), sign-up form, `Password:` with fx, `LoginGuard` per-handle lockout (replaced the per-IP time bank; daily minutes now live in the account), `INFO`/`PROFILE`/`PASSWORD`, `USERS` manager and `USER ADD|EDIT|DEL` under the new `USERS` permission (CO1 yes, CO2 no by default), `self_register` and `max_users` keys, input backpressure.
- Hardware checks for 0.6.0 once flashed: sign-up form and user manager on the C64 (PETSCII cursor moves, reverse-video boxes, F1 save, left-arrow cancel), password hashing time on the ESP32 (1000 SHA-256 rounds should be well under 100 ms), heap with 6 callers.

## Previous state (0.5.0)

- Host build: 149/149 scripted checks. The old GCC 9 ASan in WSL sometimes dies at startup with `AddressSanitizer:DEADLYSIGNAL` (ASLR vs new kernel); run the sanitizer server under `setarch $(uname -m) -R` (ASLR off), it is not a BBS bug.
- ESP32: image ~900 KB (57% of the 1.5 MB slot), static RAM ~81 KB, heap ~186 KB free with the BBS listening (0.3.0 figure).
- 0.5.0 adds: TCP keepalive on caller sockets (60 s idle, 3 x 10 s; lwIP `LWIP_TCP_KEEPALIVE` is on), activity LED (`activity_led_gpio`, default 2), the C3 command registry with generated 40-column HELP, `WHO n` and `DASH [n]` refresh screens (home + padded rows, last row without newline so 24-row terminals never scroll, footer shows the idle clock). The stock help screen is gone.
- Verified on hardware: the keepalive drops the sysop line when the C64 is switched off; the blue LED on GPIO2 blinks with traffic.
- Flash layout: 2 x 1.5 MB OTA app, `logs` 32 KB, `userdata` 128 KB, `storage` 736 KB.
- Hardware verified: PuTTY, C64 via TeensyROM, every PETSCII glyph (spinner 0xBE/0xBC/0xAC/0xBB, 0xC0, 0xDD, 0xA6, 0xA4), backup window download and upload.

## Next builds (approved)

0.5.0: done and bench-checked (keepalive, LED).

0.6.0: accounts, host-tested, never flashed on its own.

0.7.0: guests plus the queued notes (input fx, password in place, page alerts, list graphics, staff Doing column, DASH RSSI). Flashed; registration, guests and RSSI verified.

0.8.0: code-review fixes and staff rank markers. Flashed and verified on the board.

0.9.0: plugin API, example plugin, ABOUT screen, PLUGINS command, free-space reporting. Flashed by Rob.

0.10.0: chat and serial bridge plugins. Built, waiting for Rob to flash.

Still open on serial: autoprobe (listen at each common speed and score framing errors, a stretch goal), capture to file for later download, and hardware flow control (rts/cts keys exist in the config but the driver is set to no flow control). Rob has not yet said what is wired to the port.

0.11.0: help menus, list colours, SYS and CALLS, the chat room command set with moderation and the vote to kick, messages, CONFIG. Host-tested, waiting for Rob to flash.

**0.17.0 is specified in [NEXT.md](NEXT.md)**: the queued bugs, Wi-Fi at runtime plus Improv, and message bases, with measured size budgets and poker specified for the build after. Five questions in it are Rob's to answer before building starts.

Queued for the next build (Rob's plan, in order):

- **`privacy.ans` is 40 column art on an 80 column screen** (found by the ordinary-caller QA pass, 0.19.0). Measured: `privacy.ans` is 41 columns over 77 lines and four pages, while `rules.ans`, `newuser.ans` and `chatin.ans` are all 78. So the one screen a cautious newcomer reads immediately before typing a password is half width and takes twice the pages, while everything around it fills the terminal.
  Not a bug in the player: `PRIVACY_PAGES` in `tools/mkscreens.py` is hard-wrapped to 39 columns on purpose, with a comment saying so, and the ANSI build shares that text. Fixing it means re-flowing the copy at about 72 columns for the ANSI variant only, which moves every page break, so it is a job for `explain` and `screen-artist` rather than a patch. The PETSCII and ASCII versions stay exactly as they are.

- **A digit in an empty file area opens a file-number prompt** (same QA pass). Area says "Nothing in here yet", then pressing `5` asks `File number:` and answers "No file with that number." It should say the area is empty. Cosmetic, and the cheap fix is to answer from the area's own emptiness rather than opening a question whose answer is already known.

- **An encrypted option, as well as plaintext, not instead of it** (Rob, 2026-09-21). Not "telnet is plaintext, live with it", but "plaintext for the machines that need it, SSH for the machines that can". That is a far better answer to the privacy argument the site and the `privacy` screen both have to make, and it costs one port.
  **SSH rather than TLS, and the reason is certificates, not crypto.** TLS means either a self-signed certificate every client warns about, or a CA and a renewal process on a board meant to sit on a shelf for a year. SSH has host keys: generate one at first boot, clients trust on first use like every other SSH server, nothing expires. `ssh board` is also genuinely simple on every OS, which was the actual requirement.
  **The constraint is RAM per session, not CPU.** The ESP32 has hardware AES and SHA so the crypto is cheap; what costs is buffers. A TLS record buffer pair defaults to 16 KB each way, tunable to roughly 12-20 KB per session with max-fragment-length; wolfSSH tuned tight is in the same range. Against 50-80 KB of free heap that is **two or three concurrent encrypted sessions on a WROOM**, which is fine because this is an option beside the ten plaintext lines rather than a replacement for them. Flash is roughly 40-60 KB against about 465 KB spare.
  Shape: a second listener on its own port feeding the same session pool, with its own cap (`ssh_nodes = 2`). A caller is a caller once they are in.
  **The gotcha is plumbing, not memory.** SSH is not a socket that can be swapped in: it has a channel layer, a key exchange and window management above TCP. The seam exists, because output already goes through `ByteSink` and the telnet layer already sits between the socket and the session, but this is a genuine port and a phase of its own, not a config flag.
  An S3 with PSRAM moves the per-session buffers off internal DRAM and makes ten encrypted sessions plausible, the same argument that already governs the node count.

- **Doors go horizontal: a second ESP32 on the serial port, not Lua in the core** (Rob, 2026-09-21). **This replaces the Lua plan and takes it off the roadmap.**
  Rob's framing: "Id rather go horizontal on this and plug in another device to the existing one which FEELS more legit like adding BBS hardware." He is right on both counts, the feeling and the engineering.
  **It is also what real boards actually did.** Multi-node systems ran doors on separate machines and handed the caller across; the DOS world formalised it as dropfiles (`DOOR.SYS`, `DORINFO1.DEF`) and FOSSIL drivers. Adding a box to add a capability is not a workaround, it is the tradition.
  **What it saves is not small.** A Lua core is on the order of 100 KB of flash plus heap for the VM, on a board with about 29 KB of DRAM left and bulletins still to fit. It also means a door can never wedge the BBS, can be written in anything, and can be changed without reflashing the board.
  **Most of the machinery already exists.** The serial bridge plugin owns UART2, hands a raw stream to a session, and already has the one-operator-plus-watchers model. A door is that with a protocol on top, which is the same observation Rob made about terminal mode and board linking being the same capability.
  **Four things to design, and the first is the real work:**
  - **The handoff has to carry who the caller is.** Handle, rank, columns, terminal type, minutes left. That is the dropfile problem and it is why dropfiles existed. A short header sent before the stream is handed over, and it must be a format somebody writing a door in Python on the other end can parse in ten lines.
  - **A way back.** The door has to be able to say "I am finished, take them back", and the board has to be able to say "their time is up, give them back", without either trusting the caller's own keystrokes to mean it.
  - **One serial port is one caller at a time**, which is the genuine limit of this design and should be said out loud rather than discovered. Several doors means several ports, a mux, or a door box that multiplexes on its own side.
  - **Who owns terminal negotiation.** The board has already detected PETSCII or ANSI and the width; the door needs to be told rather than probing again.

- **`SHUTDOWN [n]`, a sysop macro** (Rob, 2026-09-21). Announce to every node that the board is going down in n seconds, count down, then hang up on everyone including the sysop, gracefully, and stop accepting logins. A physical reboot brings it back.
  Shape: a board-level state rather than a per-session one, the countdown driven from the existing tick, and each session taken out through `goodbye()` so the send-off screen and the 5 s linger still happen. It is a macro over things that already exist, which is why it is small.
  **Four things to decide before building it:**
  - **"Stop accepting logins" should not mean closing the listener.** A closed port gives a connection refused, which is indistinguishable from the board having crashed, a dead Wi-Fi link or a wrong address, and this board's whole history says an unexplained failure is the expensive kind. Keep accepting, say "this board has been shut down by the sysop and needs restarting", and drop. The board is still powered either way, so nothing is saved by going silent.
  - **It needs a way out.** A sysop who types the wrong number, or realises somebody is mid-upload, must be able to stop it: `SHUTDOWN CANCEL`, announced to everyone the same way the countdown was.
  - **What it does to a transfer in flight.** A caller three minutes into a download loses it. Either refuse to start a shutdown while a transfer is running, or say so in the warning so the sysop can choose. Refusing silently would be worse than either.
  - **The announcements want a cadence, not a tick per second.** A line a second for two minutes is noise people stop reading, and on a 40 column screen it is the whole screen. Something like 120, 60, 30, 10, 5, 4, 3, 2, 1.
  It also has to be logged, for the same reason the caller log exists: a board that went down needs to say whether somebody did it on purpose.

- **Board linking, DDial style** (Rob, to be designed when the terminal mode plugin is). Link two boards and the chat room spans both, so a quiet board borrows company from a busier one and neither needs more nodes of its own. That is what DDial did and it is the feature that made small systems worth calling.
  Rob's insight is the part worth keeping: **this is the same capability as dialing out.** A board that can open an outbound connection and hand a stream to a caller can talk to a modem, to a serial device, or to another BBS, and the far end being a peer rather than a terminal is a matter of what speaks on it. So linking is not a separate subsystem; it is the terminal mode plugin with a protocol on top, and designing terminal mode without that in mind is how it ends up needing rewriting.
  Sequence: terminal mode first, then linking as a protocol over it. Discuss the design when terminal mode comes up rather than now.
  Worth noting against the existing roadmap: "federation" has been on the plugin list since the start and this is what it should mean. Chat that spans boards, not a directory that spans them.

- **A thinking effect in `fx::`, in BBS words** (Rob, low priority, and a nice one). The shape is the status line a modern AI tool shows while it works: a marker and a single changing word, "Musing...", quietly cycling, and it settles on a marker. The BBS version uses the vocabulary of the era rather than borrowing that one: Dialing, Handshaking, Negotiating, Carrier detected, Connecting, Parsing, Spooling, Decoding.
  The marker is `*` (Rob), and that is the right choice for this board rather than a decorative one: an asterisk exists in ASCII, PETSCII and CP437 alike, so it lands identically on a C64, a VT220 and SyncTERM. Anything prettier would need three fallbacks and would look wrong on at least one of them. The spinner animates while the board is working and the `*` is what it comes to rest on. `fx::` already has the spinner and the typewriter to build it from, so this is mostly picking words and a cadence. Worth using wherever the board makes somebody wait, which today is the terminal detection probe and would later be a transfer starting up.

- **Explain the privacy trade honestly, and much better than it is explained now** (Rob, next directory update, and the same words belong on the board). The current line on the site says telnet is plain text because a C64 cannot do TLS and that pretending otherwise would be worse. True, and far too thin: it names the limitation and leaves a reader to imagine the risk, which they will do badly in both directions.
  Rob's framing, which is the actual deliverable here:
  1. **Open communication over the internet is radio.** You transmit, whoever is on the channel hears you, and that is the whole of it. A walkie-talkie, not a sealed envelope.
  2. **Somebody has to be trying.** Being able to listen is not the same as listening. It takes a sniffer or equivalent tooling placed somewhere on the path; the board decides who hears what, the wire simply carries it in the clear. If the radio is not switched on and tuned in, nobody heard you.
  3. **The real risk is low and not zero, and the comparison matters.** These are public conversations. What would you say in a bar or a coffee house? Somebody at the next table could hear. Now weigh that against a website that records and tracks everything by design. The BBS is the local bar. Yes, somebody could be parked outside with equipment; for almost everybody that is an edge case, and saying so is more honest than implying either that it is safe or that it is dangerous.
  4. **So: say what you would say in public, and use a password you use nowhere else.**
  Then a link after it, in Rob's words: `[read about the real risks of open communications]`, going to a page that does the long version properly.
  **Also update it wherever else this is explained**, not just the one paragraph: the board's `privacy` screen (generated by `tools/mkscreens.py`, replayed by the `PRIVACY` command and shown before a caller types a password at sign-up), the house rules page, and the new privacy section in `USERS.md`. One explanation, several places, and they should not drift.

- **Feedback and bug reports from callers** (Rob, needs the card). A caller writes a note, chooses whether it is feedback or a bug, and says which part of the board it is about; the sysop scrolls through what has come in. Stored on the SD card and `PF_SD`, so a board without a card simply does not offer it.
  Fits the shape already built: a plugin with its own folder on the card, a `Form` for writing one (the kind is an `FF_CYCLE`, the body an `FF_TEXTAREA`), and the sysop's list is exactly what the new `rows()` hook and `startPluginList` exist for. Plain text on the card for the same reason `FILES.BBS` is: a sysop can read the lot on a laptop with the card in hand, and a bug report that can only be read through the thing that is broken is not much of a bug report.
  **Worth deciding before building:** a caller who can write is a caller who can fill a card, so it wants a cap and a rate limit per handle from the start rather than after somebody discovers it. And guests: let them report, since the login is often what is broken, but record that it was a guest so the sysop knows there is no account to reply to.
  One thing to confirm with Rob: "an area to list the bug" reads as a field naming which part of the board it concerns, chat or files or logging in, so a sysop can sort them. Confirm before building in case he meant a file area.

- **Directory: moderation, reports and a way to enforce the rules** (Rob, next directory round). The house rules page exists and nothing enforces it, which Rob's point covers exactly: rules you cannot enforce are not rules. Wanted: block a listing, a UI to do it from, and community reports where more than three hold an account for review. Optional for anybody else running a directory, but Rob's has to have it.
  The `reports` table is already in the schema (`board_id`, `at`, `address`, `reason`) and nothing writes to it, so the storage is there and the feature is not.
  **Four things to get right, each of which is how this goes wrong:**
  1. **The report button is itself the abuse vector.** Three reports taking a board off the list means three people, or one person with three addresses, can delist a competitor. Dedupe by reporter `/64` the way listings already are, rate limit reports per address, require the three to be from distinct networks, and keep the reporter's address so a pattern of malicious reporting is visible. It will still be gameable; the aim is to make it cost more than it is worth.
  2. **Held must never be silent.** A board that vanishes with no explanation is the exact bug just fixed, where reconnecting was what delisted it. A held listing has to be visible to its own sysop: the announce reply already carries `X-Listing-State`, so `held` belongs there with a reason, and the board shows "held for review" instead of quietly disappearing. That part belongs in `PROTOCOL.md` even though the policy does not, so any directory can express it.
  3. **A UI means authentication, and the site has none at all today.** No login, no sessions, no JavaScript by choice. That is the largest piece of this and it should be decided deliberately rather than grown: a signed token in a URL, HTTP basic auth behind TLS, or no web UI at all and an extension to `dbtool.sh`, which is already the shape for "a human fixes the database". Worth asking whether the review queue actually needs a browser.
  4. **Reversible, and with a record.** Every hold, release and block keeps who did it and why, for the same reason the caller log exists. A moderation action nobody can audit is indistinguishable from a bug.

- **Directory: pin Unleashed HQ at the top** (Rob, for the next directory round, not the board). Rob's board is the reference copy and he wants it first in the list always. His framing: no new protocol codes or signals, the server just holds static text about the board and pulls the live figures from the ordinary record, so the board keeps announcing exactly as every other board does.
  Shape to build: a config value in the systemd unit naming the pinned listing (`DIRECTORY_PIN_*`, alongside the other settings) rather than a literal in `server.py`, and the static blurb in a file beside `pages/`. Live fields, callers on, last seen and uptime, come from the database row as now.
  **Render it as a marked row above the list, not as an unexplained first place.** The directory's whole argument is that nobody is privileged on it, `PROTOCOL.md` and the README both say a directory nobody can replace would contradict the point, and a hidden pin is the sort of thing somebody eventually finds by reading the source. A visible "reference board" row is honest, and it is more useful to a newcomer than a silent number one, because what they actually want to know is where to call the implementation the software came from. Making the pin a config value keeps "run your own and pin your own" true.

- **A disk activity light, with selectable styles** (Rob, 0.17.1, **high priority**, wanted soon, for a retro case build). The machinery is already there: `plat::activityLedBegin` / `activityPulse` / `activityTick` blinks a network LED with a 40 ms pulse, and a disk light is the same pattern on a second pin. What it needs is a `plat::diskPulse()` called from the places that actually touch storage, which is about eight call sites (`ScreenPlayer`, `users`, `calllog`, `syscfg`, `ziparc`, the plugin paths, the SD plugin) rather than one hook, because the board calls stdio directly and there is no single chokepoint. Styles worth having: flicker with a minimum on-time so a 2 ms read is still visible (IBM PC), solid for the whole operation with a slow blink on error (1541, and the board has real error states to drive that from), on for a second after the last access (Apple II Disk II, whose motor kept running), and PWM breathing via LEDC for free. **Settled: one NeoPixel, driven by RMT.** Rob has plenty of them, and colour carries the distinction that would otherwise have needed two lamps: amber for the card, cool white for internal flash, a slow red blink for a real error such as a failed card read, and a dim idle glow so the case looks alive. One hole in the panel.

  **Pins are settled, from Rob's schematic (2026-09-20).** `J1` is the pixel: pin 1 GND, pin 2 (+) to VIN, pin 3 data to **GPIO13**. GPIO13 is a good choice and worth recording why: it is not a strapping pin, has no boot-time role, and RMT can drive it. `J4` on the same sheet is the SD card, D23/D19/D18/D5 plus GND and 3V3, which matches the SPI wiring already in the config.
  **Level shifting: raised, and settled by Rob, no shifter.** The schematic powers the pixel from VIN at 5 V while the ESP32 drives data at 3.3 V, and a WS2812B's datasheet logic high is 0.7 x VDD = 3.5 V. I flagged it; Rob uses the 5 V variants and has built these for years without a shifter, which is the common real-world result rather than a lucky one. Settled, do not raise it again.
  What goes in instead, and it is good practice regardless of levels: **330 to 470 ohm in series on the data line at the pixel**, which damps reflections on the stub and is frequently the thing that makes a 3.3 V driven 5 V pixel behave, plus **100 nF across the pixel supply** and bulk electrolytic on a bank for inrush.
  **The ten-pixel IMSAI-style bank Rob wants next is a different power problem**, and it is worth designing for now rather than discovering later: ten pixels at full white is roughly 600 mA, past what VIN off USB will hold up and far past the 3V3 regulator. That board wants its own 5 V feed plus the diode trick or a proper level shifter. The firmware side is nearly free, since a chain is the same RMT channel with more bytes in the encoder.

  No library and no bit-banging. `esp_driver_rmt` is already in IDF 5.3.1 (`rmt_tx.h`, `rmt_encoder.h`, confirmed on disk), it is hardware-timed, and it costs one entry in `src/CMakeLists.txt` plus a small encoder. Bit-banging WS2812 needs 150 ns tolerance held in a critical section with interrupts disabled, on a board that is also running Wi-Fi; the RMT peripheral exists precisely so nobody has to.

  Electrical note for the build: WS2812B reads data high at 0.7 x VDD, so a 5 V pixel wants 3.5 V and the ESP32 drives 3.3 V. Marginally out of spec, works most of the time, and "most of the time" is the worst kind of fault. Powering the pixel from 3.3 V is dimmer, free and entirely in spec, which for a panel indicator run dim is the right trade. A signal diode in series with 5 V is the alternative if full brightness is ever wanted.

- **Accounts: "Address" becomes where a caller hails from** (Rob, 0.17.1). A town and a country, not a postal address: nobody should be typing their street into a plaintext telnet board, and the field has always been asking for the wrong thing. One row in `kUserFields` plus the label, and the stored key stays `address` so existing `users.txt` files are untouched.
- **Accounts: say on the form which fields are private** (Rob, 0.17.1). Confirmed by reading the code rather than assuming: `email`, `address` and `phone` are `UF_PRIVATE`, and `cmdInfo` shows a private field only to the account's owner or to somebody holding `PERM_USERS`, so the posture is already right. What is wrong is that a caller typing their phone number at sign-up is told nothing about who can read it. The form needs the note next to the field, at the moment of typing, not in a policy screen somewhere. `UF_PRIVATE` already marks exactly the right set, so the note can be driven off the flag rather than written out per field.
- **Profile text should word wrap, with the effects** (Rob, stretch). The profile is four rows of 37 columns on a C64 and is currently stored and shown as typed. Wrapping it on output means wrapping on a column count that differs per terminal, which is why it is a stretch and not a line of code.
- **MEM should show the SD card's free space when one is mounted** (Rob, 0.17.1). It reports flash today. `plat::sdInfo()` already returns the figures and the sd plugin already caches them.

- **Phase: mail worth the name** (Rob, after message bases). Today chat owns mail: one message per account, 32 slots, 512 characters, readable only from inside the room. Wanted: read and **reply from the main prompt**, not just in chat. And with a card present, mail that is not rationed. Rob's framing is two tiers, a small one that works on any board and a larger one the card unlocks. Design note for whoever builds it: make that one plugin whose storage moves, not two plugins with two mailbox formats. Two would mean two file layouts, two sets of commands and a migration the day somebody adds a card, and the tier is a property of where the files live rather than of what the feature is.
- **DASH at 80 columns: show the address** (Rob, 2026-09-21, part of the dashboard rework below). When DASH gets its width-aware layout, spend some of the new room on the caller's IP: on the last-calls block and on whoever is online now. `LAST` already shows the address to a sysop on a wide terminal and DASH does not, which is backwards, because DASH is the screen a sysop leaves open.
  Rob, in the same breath and worth keeping because it is a judgement on the design rather than a feature request: **"Nodes looks better than dash IMHO."** DASH is a refresh screen built as one fixed frame; NODES is a plain list that simply lays out well. Before adding anything to DASH, work out what NODES is doing right, because the answer may be that the dashboard should be built the way NODES is rather than given more fields.

- **Phase: the sysop dashboard reworked as a real screen** (Rob, after the above). DASH is one fixed frame sized for 80 columns and it is already too small for what it wants to say: sixteen nodes do not fit, so it shows six and a summary. Wanted: know whether the terminal is 40 or 80 columns and lay out for it, and on 40 columns page through several pages of the same information. Arrow keys to move between pages and a clear way out, rather than the current "any key stops it". That makes it an interactive screen rather than a refreshing list, which is a different thing from everything else in the shell and is why it is its own phase.

- **SHOW, HIDE and LURK do not update the directory, and they change what it shows** (Rob, 0.17.1). Verified: `Bbs::publicBusy()` counts the sysop line only when that session is visible, exactly so a board with somebody on it does not advertise itself as empty. So toggling visibility changes the figure the directory publishes, and nothing tells the announce plugin. Only `onLogin` and `onLogoff` nudge. The fix is not three calls bolted onto three commands: the right shape is one optional hook meaning "what the outside can see about who is on has changed", called from login, logoff, SHOW, HIDE and LURK, with announce hanging its nudge on that instead of on the two login hooks. Guests and the busy line come free that way, and so does anything later that changes the public count.

- **The ANSI form highlight bleed: DONE, and it was already fixed.** Investigated 2026-09-20 by driving a real ANSI session and rendering the attribute stream rather than reading the code.
  **Root cause, and it is a good lesson.** `Term::reverse(o,false)` used to emit `ESC[27m` and nothing else. **ANSI.SYS never implemented SGR 27 and SyncTERM does not act on it**, so reverse simply stayed on. Meanwhile `Term::color()` emitted `ESC[1;33m`, which sets bold and a foreground and clears *nothing*, so the stuck highlight survived every colour change. Only the non-bold palette entries happened to clear it, which is exactly why some rows looked fine and others were filled blocks.
  The orange came from one path: `Form::fail()` calls `fx::blink` (leaves reverse on), then `drawButtons()` whose LightGreen and LightRed clear nothing, then repaints the focused label in Yellow. Yellow under a stale reverse is orange.
  **Fixed by `c7a0eec` in 0.17.1 Block B**, in the terminal layer rather than the form: every colour now leads with SGR 0, and `reverse(o,false)` re-asserts the colour instead of sending SGR 27. It was fixed as a side effect of the Block B reverse-video work and nobody connected it back to this report. All 23 `reverse()` call sites were checked and are balanced.
  **The method is the part worth keeping**: a screen model that *ignores SGR 27 the way a real terminal does*, run against both the Block A tree and the current one. A model that honours SGR 27 shows Block A as clean, which is precisely why this never showed up in testing and only ever appeared on Rob's terminal. The suite's `plain()` strips escapes, so nothing in it could ever have seen this: the bleed **is** the escape stream.
  Regression check added and verified to fail against the Block A tree: one reverse run per frame, it is the focused box and not a label, and every colour clears the attributes before it.
- **The `Again` field's solid box is not a bug**, corrected from the original report. An empty *focused* field is 27 reversed spaces by design, and the unmasked `Name` field renders identically when focused and empty. Stars do echo into `Again`; the screenshot caught the form between the two password entries. **Worth a UX note rather than a fix:** a solid block is indistinguishable from "my typing is not echoing", which is why it was reported as a bug in the first place. Fold that into the menu and theme rework.
- ~~**The ANSI form leaves highlight behind and hides typed password characters**~~ (Rob, seen on SyncTERM at 0.17.0 Block A while re-registering on the freshly erased board). Two symptoms in one screenshot of NEW ACCOUNT: moving between rows leaves a filled orange block sitting across the Name, Email and Address labels, so the reverse-video attribute from the selected row is not being cleared off the rows it left; and the `Again` field draws as a solid filled box with no stars while `Password` above it shows its stars correctly. Rob's instruction: fix it in QA when Block B is under way, with `bbs-qa` driving a real ANSI session rather than by reading the code. Worth checking on PETSCII and plain ASCII in the same pass, and worth checking whether it predates Block A: the form renderer was not touched by it, and accounts surviving a reflash is exactly why nobody had opened this form in a while.
- The window title reaches SyncTERM with the micro sign mangled ("SyncTERM - \u25c6nleashed"). Low priority, but it is the same class as the `??nleashed` bug: a µ crossing a layer that does not know its encoding.

- Wi-Fi credentials move out of `include/secrets.h` and into `system.cfg` on the `userdata` partition, then Improv Wi-Fi Serial so a browser can provision a board over the same connection it flashed it with. This is the prerequisite for a web installer: today the SSID and passphrase are compiled in, so any published binary carries whoever built it's home network password, and a shared binary could never join anybody else's network anyway.
- A web installer page in the style of WLED, once the above lands: an ESP Web Tools manifest, the binaries in a `releases/` directory with the third-party licences, and the directory server hosting a copy while the firmware repo is private.
- Chat shows the room list by itself every so often (Rob, "see what DDial did"). Worth noting: DDial's exact behaviour is not in any documentation I could find, only descriptions of it as a 7-line Apple II chat server, so this is a design rather than a reconstruction. Proposed: a `roster` interval setting in minutes, 0 to switch it off, printed through the same held-line path chat already uses so it never lands in the middle of somebody typing; and only when the room has actually changed since the last one, because a list that repeats itself unchanged is noise and people stop reading it.
- Node lists should say what a caller is doing before they have a handle (Rob). Checked on the host: WHO says "(connecting)", DASH says "(logging in)", NODES says neither, so the gap is real but narrower than it looked. Make all three agree, and prefer "logging in" once detection is done.
- A sysop page (Rob): a caller can ring for the sysop and the sysop can answer, the way every board had. PAGE exists caller to caller; this is the one that gets the operator's attention wherever they are, and needs a way to be away, a way to decline, and something that does not let one caller ring a bell forever.
- A bell when somebody logs in and when somebody joins the chat room (Rob). Neither rings today: the only bells are pages, broadcasts and form errors, so a caller arriving is silent. Wants the same treatment as a page: bell, then the notice.

### Wide terminals: rowWidth unclamped (0.17.10)

- `Bbs::rowWidth` returned `(cols < 40 ? cols : 40) - 1` with the comment "lists and menus are laid out for 40 columns everywhere". That one line is where most of the board's wasted screen came from. It was always per-caller, since `cols` comes from the session; only the clamp made it narrow. Now it is `cols - 1`, capped at 132 because a 200 column reverse-video bar is a stripe rather than a design.
- **An unknown width gets 40, not 80.** A terminal that never said how wide it is has not promised anything, and a row that wraps is worse everywhere than a row that is short somewhere. Only terminals that actually report a width get the benefit.
- Most of the board followed for free, because it already derived from `rowWidth`: the HELP description column, the CALLS histogram, the refresh frame truncation, every bar, rule and padded row.
- **Two real bugs fell out of it, both latent.** The refresh footer did `buf[rowWidth(s)] = 0` into a 64 byte buffer, which at 79 columns is a stack smash and crashed the board. And `SYS` formatted its value column with `%9.9s`, where the `.9` is a truncation: the board reported its own address as "192.168.0" with the host part missing. **A misaligned value is untidy; a truncated one is a wrong answer somebody acts on.**
- The suite had the old clamp written down as a requirement, "every HELP line fits 39 columns", for a caller detection gives 80x24. SCREENS.md's 39 column rule is about screen *files*, which have to suit a C64, not about rows the board draws.

### Small things fixed in the same pass

- **"Address" is "From"** (Rob). A town and a country, not a postal address: nobody should type their street into a board whose own sign-up screen says nothing here is encrypted. The stored key stays `address`, so every `users.txt` already written keeps working.
- **Private fields say so while you are typing in them.** `FormField` gained a `note`, shown on the status line for the focused field and driven off `UF_PRIVATE` rather than written out per field. The posture was already right; a caller entering a phone number was simply never told, and a policy screen elsewhere is not an answer.
  Trap worth keeping: `Form::f_` is a pointer that `begin()` aims at `s.fields`, so setting a note through the Form before `begin()` dereferences whatever the last form left there. On a fresh session that is nothing at all, and it crashed the board the moment anybody opened the sign-up form. Notes go on `s.fields[]` directly.
- **WHO, DASH and NODES agree on a caller with no handle yet.** They said "(connecting)", "(logging in)" and "(no handle)" respectively. One `preLoginName()` now, which keeps the genuinely useful distinction between still detecting the terminal and waiting for a password, and drops "(no handle)", which described the record rather than the person.

### The shell and menu rework: done in 0.17.9, except themes and the door hold

Seven of Rob's nine, and the two left are the ones that want design rather
than editing.

1. **A blank line after ACCESS GRANTED.** The welcome ran straight into the
   password and read as one crushed block.
2. **The login speaks in sentences.** "Connected to node 1 of 10 on Sun 20
   Sep at 22:07." / "Time brought to you by NTP." / "You're the 1st caller
   today and have 60 minutes." The caller count comes from
   `calllog::countSince(clk::todayStart())`, one pass over at most fifty
   records, once per login: right across a reboot and across midnight with
   no state to maintain.
3. **The sysop arrives visible.** `d.visible` was false on elevation, so a
   board could have its operator on it and look empty to every caller, which
   is the opposite of what a sysop node is for. Invisibility is what you ask
   for with LURK. The elevation also calls `presenceChanged()` now, because
   the public count moved.
4. **`?` fits one page.** 11 rows, no pager. It used to stop at `[More]`
   every single time, which teaches people to mash a key through it and then
   they never read any of it.
5. **The main menu is what callers use**: WHO, PAGE, G|BYE, FILES, CHAT,
   MAIL. HELP, INFO, TIME, LAST, CLS and ABOUT moved to `? account`, because
   they are the board describing itself rather than what anybody came for.
   **G and BYE are one row**, `[G] | BYE  log off (G asks first)`: a caller
   does not need both explained.
6. **DESC is gone from the shell** and lives in the file manager as `D`.
9. **Menus advertise Q/ESC**, and the help footer is a grammar built from
   `menuName()` so it always lists exactly what `menuFromText()` accepts:
   `?|H|HELP [CHAT|ACCOUNT|STAFF|SYSOP|ALL]`.

Still open from the nine: **7, the files door being wiped by the first
keypress** (wants the `fx::` loading effect, same piece of work as the
thinking effect), and **8, themes**, which is a decision between a CONFIG
colour page and a `theme.txt` on the card.

**Two regressions this caused, and what they teach.**

- **`CF_HELPONLY` on BYE took staff access off the board.** That flag means
  "never dispatched", and BYE is both how a caller logs off and how staff
  elevate, so `bye <password>` became "Unknown command". `Menu::Hidden` is
  what keeps a command off a menu; the flag decides whether it runs at all.
  The suite caught it as 139 failures, which is the system working.
- **`publicNodes()` started advertising 11 nodes.** It adds the sysop line
  when the sysop is visible, which is correct and documented, and only
  looked wrong because the sysop used to start hidden. The board was right;
  the test had encoded the old default.
- The test client's `node()` parsed `"Node (\d) of"`, which the reworded
  login broke and which could never have read a two digit node anyway. It is
  `node (\d+) of`, case-insensitive, now.

## The shell and menu rework (Rob, 2026-09-20, the next block)

Nine items from one sitting with 0.17.3 on the board. They are one piece of
work, not nine: every one of them is the shell's presentation, and doing them
separately would mean redrawing the same screens five times. **`tty-ux`
specifies the layout before any of it is written**, and the wide-terminal
`rowWidth` item below is part of the same block.

1. **A linefeed after ACCESS GRANTED or DENIED.** Login runs straight into
   the welcome with nothing between them and it reads as one crushed block.
2. **Say more at login, in full sentences.** Rob's copy, close to verbatim:
   "Welcome back <user>. Connected to node 1 of 10 at <date time>. Time
   brought to you by NTP." then a blank line, then "You're the 7th caller
   today and have 60 minutes for this session." then "[H]ELP for commands."
   The NTP mention is deliberate: it is a small brag and it tells a sysop the
   clock is real.
3. **A sysop who elevates with BYE stays visible.** Today elevation lands
   them lurking, which is backwards: the common case is the sysop being
   around, and invisibility should be the thing you ask for. New wording:
   "You've been switched to the SysOp node. / Node 1 is free for callers. /
   Shown in WHO, use LURK to become invisible. / HELP for commands."
4. **`?` must fit one page.** It pages every single time, which trains
   people to hammer a key through it. One screen, no `[More]`.
5. **Reorganise the menus by what people actually use, and write them like a
   command reference.** The first menu currently leads with the least useful
   things. Rob wants the top menu to be WHO, FILES, CHAT, MAIL and one
   logoff entry, with everything explanatory moved off it. **`G` and `BYE`
   collapse to one line**: a caller does not need both explained, only that
   it logs off. And the usage lines get a proper grammar rather than prose,
   in the shape `?/H/HELP [CHAT | ACCOUNT | STAFF | SYSOP | ALL]`. Rob on
   the current text: "this reads like shit."
6. **Reconsider `DESC`.** Rob: "wtf does this actually do now that we have
   the file area." It writes the description shown beside a file in the
   listing, so the function is still needed, but as a top-level command
   typed at the shell against "the area you last opened" it is an odd thing
   to hand a caller. The likely answer is that it stops being a command:
   an uploader is asked for the description as part of the upload, and staff
   get a chance to edit it at the approval step. That removes a command from
   the menu, which item 5 wants anyway.
7. **The files door is wiped by the first keypress.** The screen plays and
   then the menu clears it. Rob wants either the banner to stay, or the
   banner plus a short hold with a loading effect and some `fx::` before the
   screen clears and the menu appears. The second is nicer and the `fx::`
   thinking-effect already queued below is the same piece of work.
8. **Themes.** The reverse-video cyan header is fine as a default, but the
   colours should be somebody's to choose. Two options and Rob is happy
   either way: a CONFIG page of colour pickers, or a `theme.txt` in the
   screens folder that a sysop edits on a laptop, with the built-in defaults
   used when it is absent. Decide during the menu rework, since that is when
   the colour choices are all in one place anyway. The card already
   overrides screens per file, so `theme.txt` fits the existing shape.
9. **ESC works, so say so.** Menus advertise `Q` and should say `Q/ESC`.

- **`SYS` truncated the board's own IP address**, found by Rob reading it off the screen: "Address 192.168.0". `statRow` formatted its value with `%9.9s`, and the `.9` is a truncation, not a pad. An IPv4 address is up to fifteen characters. It pads without cutting now. Worth noting as the same habit as the 40-column layouts: a column width chosen once, frozen, and then quietly wrong for a value nobody re-checked. **A misaligned value is untidy; a truncated one is a wrong answer somebody acts on.**
- **A 126 ms stall in the BBS loop, seen on the board** (Rob's SYS screen, 0.17.6: `Loop worst 126,146 us` against a 39 us average over 70,092 passes). That is a visible hitch for every caller on the board, not just the one who caused it, because the loop is cooperative. The suspects are all synchronous card work on the caller's path: `opendir`/`readdir` walked per listing row, `stat` per row, and the FILES.BBS reopen that `findDesc` does for every file in a listing. Measure before fixing: `plat::micros()` around the listing row builder and around the transfer pump would say which, and the figure is already collected, so this is instrumentation rather than guesswork. Worth doing before message bases, which will walk directories the same way.
- **Wide-terminal layout rework (Rob, scheduled straight after upload and download in this batch).** The board draws for 40 columns everywhere and never re-measures, so SyncTERM at 80 gets wrapped descriptions with half the screen black. HELP is the worst case and the screenshot is unambiguous: two-line wraps on "this menu; x picks another" with forty columns of unused screen to the right. This is not one constant, it is a habit spread across every drawing path, so the fix is a rule rather than a patch: a layout takes its widths from `Term::cols()` with a stated minimum, never from a frozen guess. `tty-ux` specifies it before anybody writes code. Rob's words: "it doesnt look like butt and all jamed together being 40col when on an 80col or wider device. This constantly triggers me."

  **Start at `Bbs::rowWidth`** (`src/core/bbs_shell.cpp:395`), because it is the single clamp most of this hangs off: `return (cols < 40 ? cols : 40) - 1`, comment and all, "lists and menus are laid out for 40 columns everywhere". Rob asked why it could not just be set per caller and the answer is that **it already is**: `cols` comes from `s.term.cols()`, so the width is per session and only the clamp makes it narrow. I first called this a one-line change with board-wide blast radius, which overstated it, and the correction is the useful part.
  Unclamping fixes several things for free, because they already derive from it: `descW = rowWidth(s) - kUsageCol` at :638 is the HELP description column in the screenshot and wraps at 40 *because of* the clamp; the CALLS histogram at :1431; the refresh-frame truncation at :878; every bar, rule and padded row at :418, :439, :478, :484, :496.
  The residual work is the screens that never call `rowWidth` and carry hand-built fixed columns: NODES is laid out at exactly 39 and WHO likewise, so they would sit 40 wide under an 80 wide title bar. Cosmetic mismatch rather than breakage, and it is the actual scope of the job.
  Keep the zero guard: a NAWS negotiation carrying zero reaches Telnet, and zero underflows to 255, which pads 255 reverse-video spaces and paints a bar down the screen.
- **Website cleanup**, from `reports/website-copy-review-2026-09-20.md` and `reports/tty-ux-website-2026-09-20.md`. The copy plan is to be executed by `explain`, not `docs`: it is human-facing. Confirmed P1s include `article .warn` never having had a left margin (the shorthand `margin:14px 0` sets `margin-left:0`, and the earlier fix moved `article .pull` instead, which is why it never took), `md_render` having no ordered-list support so 53 numbered router steps render as run-on paragraphs, `/about` and `/data` returning 404 on a single-domain deployment, and the manifesto still saying six callers plus a sysop line when it is ten plus the hidden node.
- **YMODEM: DONE in 0.17.5**, built before the menu rework rather than after, so Rob could test uploads in one flash. Kept here for the reasoning. (Rob asked why it mattered, and the
  earlier note did not say). XMODEM has no length field: the last block is
  padded with 0x1A, so every downloaded file arrives up to 127 bytes longer
  than the original, 1023 with 1K blocks. The engine reports the padding and
  deliberately refuses to strip it, because 0x1A is a legal byte inside a
  .PRG and a receiver that guesses is a receiver that truncates somebody's
  file. A .ZIP tolerates the junk; a .PRG or a .D64 does not. **YMODEM's
  block 0 carries the filename and the exact byte count**, which fixes the
  padding and also means an upload no longer needs the caller to type the
  filename first. The engine already does 1K blocks, CRC-16 and the framing,
  so this is block 0 plus truncate-to-size, not a rewrite. ZMODEM stays last
  by Rob's standing rule.
- Build profiles: PlatformIO environments for a logger-only board, a chat-only board and the full board, rather than forking the repository.
  **And a bigger-board profile** (Rob asked whether more memory means more nodes: it does). `BBS_MAX_NODES` already drives everything, since Block A parameterised the lists and gave node numbers two digits, so a WROVER or an S3 is a sizing change rather than a port. The move that actually unlocks it is `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`, which puts static `.bss` in PSRAM and so takes the session pool, the thing that broke the build at sixteen nodes, off internal DRAM entirely.
  Moves with it: `CONFIG_LWIP_MAX_SOCKETS`, and the knowledge that PSRAM is slower so a session's output buffer living there costs a little on every write. Hard ceilings at 255 in both cases, because `Session::id` and `Session::listIdx` are `uint8_t`.
  The real limit is not RAM, it is the radio. Ten concurrent telnet sessions over Wi-Fi is comfortable and fifty is a different engineering problem; 20 to 30 on a WROVER looks right and anything past that wants measuring before it is promised.
- XMODEM / YMODEM and the SD card file plugin.
- GPIO plugin with a named point table and a dashboard (needs Rob's pin list).
- OTA updates, and browser flashing with ESP Web Tools.
- **Watchdog.** Keep. The ESP32's task watchdog reboots the chip if the BBS loop ever wedges, instead of leaving a board that looks alive and answers nothing until somebody notices and pulls the plug. The machinery to explain it afterwards already exists: `reboots.log` records why the board started and the next staff member to log in is told. The cheapest of the three and the most valuable for a board left running unattended.
- **Maintenance window, not maintenance mode** (Rob, and a better design than what it replaces). Maintenance mode was a board feature for a problem the board does not have: if a sysop is working on it, it is off. The actual pain is that the directory forgets a board while it is down. So the board declares a window before the sysop pulls the plug, and the directory keeps the listing up and marked through it rather than letting it go quiet, until heartbeats come back on their own.
  **Up to three days, the sysop says how long, plus two hours on top** (Rob). Three days because moving equipment to a new house or a new rack is a real thing that happens and an hour is not enough for it; the two hours because maintenance runs over and a sysop who said one hour and took three should not lose their listing for it.
  A `MAINTENANCE <hours|days>` command on the board is the whole of the board side: it sends the signal and then the sysop pulls the plug.
  No mode on the board, no screen, no state to get wrong: one signal and then the board is simply off, which is what it was going to be anyway.
  Two things to get right when it is built. It belongs in `PROTOCOL.md` so any directory can implement it, not just this one. And it needs a cap: a listing that can declare maintenance indefinitely is a listing that sits on the page forever without ever being reachable, so the three day cap is enforced by the directory and not merely requested by the board, allow **one window per seven days** (Rob) so it cannot become a way of living on the page permanently, require it to have been online to claim one, and stop granting them to a board that has done nothing else for a week.
- **Zones: tabled** (Rob). Treating callers differently by where they connect from, which the serial bridge already does in miniature by being LAN and sysop only. Worth discussing later; not obviously needed.
- A carrier PCB for the module.

**Home Assistant and MQTT are dropped** (Rob, 0.17.1). Not deferred, dropped.
The project set out as an "IoT terminal server" and became a real BBS
instead, and Rob's read is that HA was novelty more than anything: with it
gone, MQTT has nothing left to carry. That is worth about 40 KB of flash and
9 KB of static RAM, and the RAM is the part that matters, because the
roadmap did not fit in the DRAM that is actually left.

It also settles what this is. A board with file areas, message bases, mail
and doors is a BBS. A board with those plus a thermostat integration was
two projects sharing a chip.

GPIO stays: a named point table and a dashboard is a BBS feature in the
1980s sense, it is small, and it does not drag TLS in behind it. Lua stays
the drop-in path for doors, one door at a time, but it is its own decision
rather than a given: a Lua core is on the order of 100 KB of flash and wants
heap for the VM, which is a bigger commitment than anything else on the
list.

- Announce plugin (Rob, 0.13.0): opt-in directory listing. Plain HTTP POST of a ~200 byte JSON payload (name, owner, description, host, port, nodes, busy, uptime, token) to a comma separated list of directories, default `http://unleashedbbs.com/announce` (Rob owns unleashedbbs .com/.net/.org). No TLS on purpose: the payload is public and mbedTLS would cost more heap than the plugin. The socket is non-blocking and driven from `tick()`; DNS is resolved once at `start()` while the board is quiet and only re-resolved after a failure, so a slow lookup never stalls a caller. The reply's optional `X-Seen-Address` header is how a board behind a changing address learns its public address, which makes this a rough DDNS as a side effect. `ANNOUNCE TEST` prints the payload without sending. The directory server is to be a separate open-source repo; ANNOUNCE.md is the protocol spec so anybody can run one.
- Tabled: robustness/pen test script (`tools/robustness.py`, untracked stub).
- Tagline: current screens are fine for now.

## Build gotchas already hit

- **A paused screen eats the next command.** A multi-page screen stops at "Press SPACE to continue" and *any* key advances a page: it is the screen player's page break, not the list pager's `[More]`, and nothing stops it. A test that leaves a screen paused and then sends `sd unmount` feeds "s" and "d" to the screen as page keys and leaves `unmount` at the prompt as an unknown command. That cost three rounds of chasing a bug in working code today. Page to the end before doing anything else, and do not send the same screen command twice in a row.
- Checking two things about one screen means playing it once and asserting twice on the captured output, not running the command twice.
- On Xtensa, `uint32_t` is `unsigned long`. Cast to `unsigned` for `%u`; `plat::log` has a printf format attribute.
- The IDF's newlib is not glibc. `struct tm` has no `tm_gmtoff`, so a UTC offset has to be worked out by comparing `localtime_r` with `gmtime_r` for the same instant. The host build compiles the glibc version happily, so this class of mistake only shows up on `pio run`.
- Partition labels `storage` and `logs`. `littlefs` as a name collides with an IDF 5.3 subtype name.
- PlatformIO `uploadfs` writes to the LAST data partition of subtype spiffs/fat/littlefs, so `storage` must stay last in `partitions.csv`. esp_littlefs mounts by label with any subtype.
- PlatformIO registers `src/` as an extra component, not `main`: IDF deps are listed in `src/CMakeLists.txt` (incl. `esp_driver_gpio`, `esp_rom` for ROM `tinfl_decompress`). LittleFS and mDNS come from `src/idf_component.yml`.
- A Component Manager newer than IDF 5.3 expects fails with `--interface_version` errors. Use the version PlatformIO bundles.
- `platformio.ini` must stay ASCII (µ only in C sources as `\xC2\xB5`).
- glibc defines `P_ALL` (waitid): permission bits are `PERM_*`.
- New source files need a clean build (`pio run -t clean`): the component uses `GLOB_RECURSE`.
- Git on this PC has `core.autocrlf`; `.gitattributes` keeps screens binary and text LF.
- Windows PowerShell 5.1: `curl` is Invoke-WebRequest, use `curl.exe`; no `Set-Content -NoNewline`.
- Tera Term only begins telnet option negotiation when the port is 23. On 6400 it opens the socket and says nothing, so the IAC-first path in connect-time detection never fires and the caller falls through to the CPR probe. Not a bug here, but it explains a Tera Term caller taking the slow route, and it is documented behaviour in Tera Term's own manual.
- Wi-Fi: SSIDs are case-sensitive (`HOMENET`). The board scans all channels and joins the strongest AP. Auth timeouts (reason 2/15/39) turned out to be an unplugged AP, not firmware.
- A non-interactive shell cannot push to GitHub the first time: Git Credential Manager needs one interactive browser sign-in (done on this PC).
- **The IDF defaults FATFS to 8.3 names, and no host test can ever catch it.** `CONFIG_FATFS_LFN_NONE` is the default, and under it a path component of more than eight characters is not awkward, it is an *invalid name*: FatFs refuses to create it and refuses to open it. Rob typed `storage/textfiles` into CONFIG, the `mkdir` was refused because `textfiles` is nine characters, the area listed as configured, and the only symptom was "that folder is not on the card" with nothing in the log. Every file already on the card with an ordinary laptop-made name would also have been invisible, which would only have surfaced once downloads worked. Now `CONFIG_FATFS_LFN_HEAP=y` with `CONFIG_FATFS_MAX_LFN=128`, costing 3,452 bytes of flash and no static RAM, since the buffer is heap per open handle. **The host build uses the real Linux filesystem, so long names worked in every test ever run here.** This class of bug is target-only and the generated `sdkconfig.esp32dev` is the only place it is visible. Two lessons: check the generated config for filesystem behaviour rather than assuming the default is sane, and remember that the host build is not a filesystem simulator.
- A file area path is relative to the **card root**, so `storage/textfiles` means `/sd/storage/textfiles`. `storage` is also the name of a flash partition, which makes it an easy thing to type by mistake. File areas are card-only on purpose: a sysop cannot point an area at the screens in flash and hand them out.
- **Capture a server's stdout with a drain, or you will diagnose a phantom.** The directory server logs one blocking `print(..., flush=True)` per request from the handler thread. Start it under `subprocess.PIPE` without reading the pipe and the buffer fills after a few KB, every thread blocks inside `log_message`, and the server stops answering while staying alive. That looks exactly like a wedge under load and it cost two investigations on 2026-09-20: an agent reported the server "exiting with code 1 after an aborted image fetch", and a reproduction attempt then wedged it on demand and blamed client aborts. Draining the pipe, 360 abusive connections across every abort shape survived with the server still serving. The exit code 1 was `terminate()` on Windows. Two rules out of it: read the pipe in a thread, and when a server appears hung, dump the thread stacks (`faulthandler.dump_traceback_later`) before forming a theory. The stacks named the real line in one shot after two wrong guesses.

## Open questions

- Can TeensyROM+ run a generic CRT, Swiftlink and its REU ($DF00) at the same time? Needs a bench test or the TeensyROM Discord.
- Banked carts (EasyFlash, Magic Desk) use $DE00 for bank select and collide with Swiftlink, so banked cart means no network. A generic 8K/16K cart is fine.

## Reference links

- TeensyROM usage: https://github.com/SensoriumEmbedded/TeensyROM/blob/main/docs/General_Usage.md
- HomeTo64: https://github.com/JKnivesworthy/hometo64
- Image BBS source: https://github.com/Pinacolada64/ImageBBS
- Synchronet PETSCII: https://wiki.synchro.net/howto:petscii
- YMODEM, Chuck Forsberg's original spec: https://pauillac.inria.fr/~doligez/zmodem/ymodem.txt
  Checked rather than recalled when the engine work was specified. The four
  points that matter: the receiver opens with 'C'; block 0 is a NUL-terminated
  filename then the length as a decimal string, unused bytes NUL; the receiver
  ACKs block 0 and then sends 'C' again to start the file; and the receiver
  **must** truncate to the stated length, "discarding any padding added by the
  sender to fill up the last block". That truncation is the spec, not an
  optimisation, and it is the whole reason YMODEM is worth having here.
- esp_littlefs: https://github.com/joltwallet/esp_littlefs
- ESP-IDF 5.3.1 mbedTLS RAM: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-reference/protocols/mbedtls.html
