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
- Reference target: bare ESP32-WROOM-32E, Bluetooth off (520 KB SRAM, 4 MB flash, no PSRAM). The core is sized for it; plugins declare their own needs.
- C++ for core and hardware. Lua only for doors later. Static allocation, no heap in the BBS loop (exceptions: temporary inflate buffers during a backup upload).
- 6 caller nodes, a busy line session (7th caller: detection, busy screen, 10 s countdown), a hidden sysop node. Overflow callers get `BUSY` and a drop. Socket budget 16.
- One dial-in port, 6400. Connect-time terminal detection: settle 300 ms (a telnet client that speaks IAC first gets character mode negotiated and an immediate probe; this fixed PuTTY's line-mode "[3;20R" leak), CPR probe with UTF-8 test glyph, else PETSCII/ASCII key prompt, then 40/80 columns for PETSCII.
- Security posture: plaintext telnet accepted (the C64 side cannot encrypt). Staff passwords in `system.cfg`. `BYE <password>` elevates; a wrong one is a plain logoff and counts toward an IP ban (3 in 15 min = 15 min). No TOTP, no email codes.
- Staff: one sysop (hidden node, all permissions), co-sysop 1 and 2 (stay on their node, permissions from the `[access]` matrix in `system.cfg`, `NOLIMITS` is a matrix row). Rank: KICK/SNOOP only on lower levels.
- Storage is split by who owns it (0.14.0). `storage` holds the screens and is the only partition `uploadfs` rewrites, because PlatformIO writes the LAST spiffs partition and `storage` is kept last on purpose. `userdata` holds `users.txt`, `system.cfg` and `<user>/p/<plugin>/`, so accounts, config, chat mail and the announce token survive a reflash. `logs` holds the caller log. The backup zip still carries `system.cfg`, `users.txt` and screens as one file, and `ziparc::livePath` routes each entry back to the partition it belongs on. `syscfg` seeds `userdata/system.cfg` from the shipped copy once on a blank board, otherwise a fresh board has no sysop password and can never have staff. Paths: `plat::fsBase()` screens, `plat::userBase()` user data, `plat::logsBase()` logs; plugin free space is measured with `plat::userInfo`.
- Backup window: BOOT button (GPIO0) while the sysop is logged in opens HTTP on `backup_port` for `backup_window_minutes`. Download needs no confirmation and redacts passwords as `***`; upload is staged, validated, and applied only after the sysop's Y. HTTP runs inside the BBS select loop (no httpd task, saves 27 KB flash, host-testable).
- Commands: C3 is a command registry (verbs, shortcuts, permission, help, handler) that plugins register into. The navigation tree is deferred; if it returns it is a device namespace (`ls /gpio`), not a BBS menu tree.
- Plugin order after core: GPIO, chat (DDial/Gtalk style), serial bridge (sysop/LAN only, raw stream handoff), HA. Then mail, message bases, XMODEM, MQTT, Lua doors, federation.
- SD card plugin (planned): replaces the onboard filesystem for all data, takes over file management, and lets logging be redirected to the card. Onboard storage caps user accounts at about 100; more than that requires the SD card plugin. Core file access must stay behind `plat::fsBase()` / `plat::logsBase()` so the plugin can swap the mounts.
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
- **Commit and push after every build**, and again at checkpoints during long
  work so there is a history to go back to. COMMANDS.md, README.md,
  CHANGELOG.md and this file are updated in the same change, not afterwards.
- **Never commit** `include/secrets.h`, `data/system.cfg`, `CLAUDE.local.md` or
  `data/calls.log`. No passwords in git history, ever.
- **Proposal before new code.** Confirm the approach, get a go-ahead, then
  write it. A bug I introduced myself is still a proposal, just a short one.
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

## Current state (0.15.0, host-tested, not flashed)

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
- Messages are a fixed-size record file (`p/chat/mail.dat`) rewritten through a temp file; RAM holds only the addressees and dates (`g_mailTo`, `g_mailAt`) so "you have mail" costs no reads. One message per account, newest replaces an unread one and the sender is told, 32 slots, 512 characters, 14 days, all configurable. Documented plainly as not private.
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
- 0.9.0 is the plugin API (C5). A plugin is a static descriptor (name, version, needs, hooks, commands) in `src/plugins/`, listed in `registry.cpp`, switched on in its own `[plugin:name]` section. Levels are read/write/admin against the ladder all|users|staff|co2|co1|sysop, with commands tagged `CF_READ`/`CF_WRITE`/`CF_ADMIN` (untagged counts as write, fail shut). Hooks: start, stop, tick (250 ms), onConnect, onLogin, onLogoff, onKey. A plugin can own a session (`Bbs::own`/`release`, `SState::Plugin`), which pauses the idle clock but not the call limit; that is what the serial bridge and doors will use. Each plugin gets `<fs>/p/<name>/` (PF_CORE only; PF_SD plugins are refused until the SD card lands) and the core keeps `BBS_FS_RESERVE` free. `plat::fsInfo` reports free space, shown in MEM, DASH and PLUGINS. Config keys must sit above the first section.
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

Queued for the next build (Rob's plan, in order):

- Wi-Fi credentials move out of `include/secrets.h` and into `system.cfg` on the `userdata` partition, then Improv Wi-Fi Serial so a browser can provision a board over the same connection it flashed it with. This is the prerequisite for a web installer: today the SSID and passphrase are compiled in, so any published binary carries whoever built it's home network password, and a shared binary could never join anybody else's network anyway.
- A web installer page in the style of WLED, once the above lands: an ESP Web Tools manifest, the binaries in a `releases/` directory with the third-party licences, and the directory server hosting a copy while the firmware repo is private.
- A bell when somebody logs in and when somebody joins the chat room (Rob). Neither rings today: the only bells are pages, broadcasts and form errors, so a caller arriving is silent. Wants the same treatment as a page: bell, then the notice.

- Build profiles: PlatformIO environments for a logger-only board, a chat-only board and the full board, rather than forking the repository.
- XMODEM / YMODEM and the SD card file plugin.
- GPIO plugin with a named point table and a dashboard (needs Rob's pin list).
- OTA updates, and browser flashing with ESP Web Tools.
- Zones, maintenance mode, watchdog.
- A carrier PCB for the module.

HA stays parked (its TLS was the only real RAM risk). Lua stays the drop-in path for doors, one door at a time.

- Announce plugin (Rob, 0.13.0): opt-in directory listing. Plain HTTP POST of a ~200 byte JSON payload (name, owner, description, host, port, nodes, busy, uptime, token) to a comma separated list of directories, default `http://unleashedbbs.com/announce` (Rob owns unleashedbbs .com/.net/.org). No TLS on purpose: the payload is public and mbedTLS would cost more heap than the plugin. The socket is non-blocking and driven from `tick()`; DNS is resolved once at `start()` while the board is quiet and only re-resolved after a failure, so a slow lookup never stalls a caller. The reply's optional `X-Seen-Address` header is how a board behind a changing address learns its public address, which makes this a rough DDNS as a side effect. `ANNOUNCE TEST` prints the payload without sending. The directory server is to be a separate open-source repo; ANNOUNCE.md is the protocol spec so anybody can run one.
- Tabled: robustness/pen test script (`tools/robustness.py`, untracked stub).
- Tagline: current screens are fine for now.

## Build gotchas already hit

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
- Wi-Fi: SSIDs are case-sensitive (`HOMENET`). The board scans all channels and joins the strongest AP. Auth timeouts (reason 2/15/39) turned out to be an unplugged AP, not firmware.
- A non-interactive shell cannot push to GitHub the first time: Git Credential Manager needs one interactive browser sign-in (done on this PC).

## Open questions

- Can TeensyROM+ run a generic CRT, Swiftlink and its REU ($DF00) at the same time? Needs a bench test or the TeensyROM Discord.
- Banked carts (EasyFlash, Magic Desk) use $DE00 for bank select and collide with Swiftlink, so banked cart means no network. A generic 8K/16K cart is fine.

## Reference links

- TeensyROM usage: https://github.com/SensoriumEmbedded/TeensyROM/blob/main/docs/General_Usage.md
- HomeTo64: https://github.com/JKnivesworthy/hometo64
- Image BBS source: https://github.com/Pinacolada64/ImageBBS
- Synchronet PETSCII: https://wiki.synchro.net/howto:petscii
- esp_littlefs: https://github.com/joltwallet/esp_littlefs
- ESP-IDF 5.3.1 mbedTLS RAM: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-reference/protocols/mbedtls.html
