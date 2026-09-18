<!--
 ===========================================================================
  µnleashed BBS
  Electronic freedom on a microcontroller.
 ===========================================================================

 File:         CHANGELOG.md
 Module:       Documentation / history

 Purpose:      Every released build, newest first: what changed, when, and
               whether it has run on real hardware.

 Audience:     Anyone picking the project up, and the next build's planning.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v2 or later
 SPDX-License-Identifier: GPL-2.0-or-later
 ===========================================================================
-->

# Changelog

Every released build of µnleashed BBS, newest first. Versions are `MAJOR.MINOR.PATCH`; while the core is being built each minor number is one flashed build. Dates are the day the work landed in the repository.

A build is only marked **on hardware** once it has run on a real ESP32-WROOM-32E with a caller connected. Everything else is host-tested through `tools/testclient.py`.

## 0.12.0, 2026-09-18

Disclosure before anybody types a password, and the documentation to go with it.

- Nobody types a password before being told the link is in the clear. Registering now warns that the connection is not encrypted and that the password must not be one used anywhere else, then asks "Would you like to know more?". Yes plays the new `privacy` screen: what telnet does and does not protect, how the password is stored, what the sysop can see, and an honest answer to "what is my real risk". The sign-up form opens when it finishes.
- `PRIVACY` shows the same screen at any time, and it is an ordinary screen file a sysop can rewrite.
- PUBLIC.md is new: how to put a board on the internet, what forwarding a port actually exposes, and the risks that are real. The address problem leads it, because a home connection's address changes and a board nobody can find twice is no use.
- CLIENTS.md is new: every machine that can call in, what terminal software it runs and what puts it on the wire, including phones. README carries the short version.
- README opens with what the board is for rather than a feature list.

## 0.11.0, 2026-09-18

Help, screens, the chat room's command set, messages and a settings manager.

- HELP is now a set of menus. `?` lists the commands people actually use, `? chat`, `? account`, `? staff`, `? sysop` list an area, and `? all` walks every section. Commands are ordered by how often they get used, each section has its own title bar, and the shortcut letter is picked out inside the word (`[W]HO`).
- Colour pass over the lists: WHO colours the node, the rank marker, the handle, what a caller is doing and the idle clock separately; the rank key under the list is drawn in the same colours as the markers; MEM is laid out as labelled figures with thousands separators.
- `SYS` (staff): the whole board on one screen, grouped into network, memory, storage, load and traffic. SSID, signal with a plain-English quality word, channel, address, heap, storage used and free, uptime, scheduler load in microseconds, nodes busy with the peak since boot, calls answered, plugins running and active IP bans.
- `CALLS` (staff): the caller log bucketed by hour of the day as a bar chart, with the busiest hour named. One pass over the log, no new storage.
- Chat colours are configurable. The node number, the punctuation that carries the rank, the handle, the text, replayed history and room notices each have their own `[plugin:chat]` colour key, and any C64 colour name works.
- Chat history depth is a setting (`history`), claimed once when the plugin starts and given back when it stops, so a board with more RAM can hold a whole evening of talk.
- A colour name parser in the terminal layer (`colorByName`), so settings files can name colours.
- `plat::micros()` and `plat::netInfo()` in the platform layer for loop timing and the network panel.
- Chat room commands: `/?`, `/p` for a private line, `/me`, `/a` away notes, `/sq` squelch, `/t`, `/clear`, on top of `/s` and `/q`. A squelch hides one node's chat for the rest of the call and never hides joins, leaves or moderation.
- Moderation: staff `/k` kicks a node out of the room, `/b` and `/unb` keep a room ban list in the plugin's folder, `/bans` lists it. With no staff in the room and three or more callers, `/vk` opens a vote: two thirds of everyone but the target, sixty seconds, and it can only remove somebody from the room, never ban them.
- Messages: one per caller, up to 512 characters, 32 slots, expiring after 14 days, all configurable. `MAIL` at the prompt, `/email` and `/e` in the room, "You have mail" at login and on the way into the room. Replacing an unread message tells the sender. The documentation says plainly that mail is not private.
- `CONFIG` (sysop only): the settings as pages, each one the same form the user manager uses, including a page per plugin. Only what changed is written, the rest of `system.cfg` keeps its comments and ordering, passwords are masked and left alone unless retyped, and the board reloads immediately.
- README: what the board is for, the long list of machines that can call in, and a hardware integration section covering Wi-Fi, the serial bridge and GPIO.
- CHAT.md documents the room and the message system.
- This changelog.

## 0.10.0, 2026-09-17, on hardware

Chat and the serial bridge, the first two real plugins.

- Chat room in the DDial and Gtalk style: one room, `#2:Daytona)` line tags carrying node, handle and rank, no blank lines between posts, no prompt character, just the cursor at the start of the line.
- A caller's own typing is never disturbed: lines that arrive while you are part way through yours are held until you press Enter, per caller, not for the room.
- Nothing is dropped. The room keeps a 48-line buffer, a caller who joins sees the last few lines, and anyone whose held lines are close to filling the buffer has their typing lifted, the room printed underneath, and their line put back.
- Per-caller rate limit, 80 lines a minute after a burst of 8, which is faster than anyone types and slow enough that nobody can flood the room. Only the caller who trips it is told.
- `/s` lists the room, `/q` leaves, `CHATCLEAR` empties the history.
- Chat is on by default; a board that only wants a log viewer can switch it off.
- Serial bridge plugin: one operator drives the second UART, everyone else watches, `SERIAL SET 9600 8N1` changes the line, 1 KB of scrollback, and slow watchers are told how many bytes they skipped rather than holding the board up. Flash, console and input-only pins are refused.
- Plugins carry their own default access levels, so a board works before anyone edits `system.cfg`.

## 0.9.0, 2026-09-17

The plugin API (phase C5).

- Static `Plugin` descriptors compiled in, with hooks for start, stop, tick, connect, login, logoff and keys.
- A plugin can own a session, so keys go to it instead of the shell, with a scratch word per session for its own state.
- Per-plugin `read`, `write` and `admin` levels on the ladder `all | users | staff | co2 | co1 | sysop`, set in a `[plugin:name]` section.
- Per-plugin storage under `<fs>/p/<name>/`, with a free-space floor the core keeps for itself.
- Requirements are checked at boot; a plugin that cannot run says why in `PLUGINS`.
- `ABOUT` screen, editable like any other screen file.
- Example plugin as a template: PING, POKE, ECHO and EXAMPLE.

## 0.8.0, 2026-09-17, on hardware

Staff ranks on accounts.

- Entering a staff password marks the account with that rank, so staff are recognised on later calls.
- Staff may only modify their own level and below.
- DDial-style markers everywhere: `>` co-sysop, `]` sysop, `*` guest, with a key line under each list.
- Co-sysops and sysops see hidden and lurking callers.
- Unknown keys in `users.txt` are reported with a line number instead of being silently dropped.
- Fixes from the first full code review of the account system.
- Measured on the board: heap free 143,344, minimum 118,268, largest block 110,592, session 5,596 bytes each.

## Licensing, 2026-09-17

- GPL-2.0-or-later across the tree, SPDX headers on every source file, purpose and design notes in each header, `LICENSE` with the full GPLv2 text and `THIRD_PARTY_NOTICES.md`.

## 0.7.0, 2026-09-17, on hardware

Guests and the polish pass.

- Guest logins: any unused handle, marked `*` in every list, 15 minutes, nothing saved, no staff elevation.
- The handle prompt is the same for everyone; accounts get a password prompt, new handles are offered registration or a guest call.
- Input effects in place: errors and passwords resolve on the line they were typed on, and the password field turns into `ACCESS GRANTED`.
- Page and broadcast alerts: a bell, a rubout, then the message.
- Title bars and rules on the lists.
- Staff see a Doing column in WHO and DASH.
- Wi-Fi signal strength on the dashboard.

## 0.6.0, 2026-09-17

User accounts (phase C2).

- `users.txt` as `[handle]` blocks, rewritten through a temp file and a rename.
- Sign-up form and user manager as cursor-driven forms on ANSI and PETSCII, line prompts on plain ASCII.
- Salted SHA-256, a thousand rounds.
- Three wrong passwords per call hangs up; five per handle in fifteen minutes locks it.
- `INFO`, `PROFILE`, `PASSWORD`, `USERS`, `USER ADD | EDIT | DEL`.
- `self_register` and `max_users` settings.
- Input backpressure: a socket is only read while the caller's output buffer has room, so a pasted burst cannot lose output.

## 0.5.0, 2026-09-17, on hardware

- Command registry (phase C3): `Command` tables with verbs, shortcuts, permissions and help, all generated from one place, ready for plugins to register into.
- `WHO n` and `DASH [n]` refresh screens that redraw in place without scrolling a 24-row terminal.
- TCP keepalive on caller sockets, so a C64 switched off at the wall drops its node.
- Activity LED.

## 0.4.0, 2026-09-17

- Backup window: hold the BOOT button while the sysop is logged in to open HTTP for a few minutes. Download needs no confirmation and redacts passwords; upload is staged, validated and applied only after the sysop says yes.
- Separate `logs` partition with fixed-size rings, never part of the backup.

## 0.3.0, 2026-09-16

- Renamed to µnleashed BBS, new screens.

## 0.2.0, 2026-09-16

- The core: listener, six caller nodes, busy line, hidden sysop node, connect-time terminal detection, screens, line editor with history, paged output, the message bus, paging, broadcast, do-not-disturb, staff access and IP bans.
