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

## 0.17.1, 2026-09-19

The SD card. Its own version number because it is its own flash: 0.17.0 and
this were briefly the same version, which meant ABOUT could not tell a sysop
which of the two was on the board and PLUGINS was the only way to find out.
Every flashed build gets its own number.

### The SD card

- **An optional SD card, mounted FAT32 over SPI.** Four wires. A board with no card is still a complete board, and that is the case the tests run by default. What goes on the card is what grows without limit and can be lost: message bases, file areas, a sysop's own screens. What stays on internal flash is everything that has to survive the card failing, which is the accounts, the configuration and the caller log.
- FAT32 rather than LittleFS so the card can be pulled and read on any laptop. That is the whole point of it. The price is that FAT is not safe against losing power mid-write, which is exactly why nothing that matters lives there.
- `SD`, `SD MOUNT` and `SD UNMOUNT`, sysop only. **Mounting pauses the board** for a few hundred milliseconds while it negotiates over SPI, so it happens at boot before any caller exists, or when a sysop asks and is told. Nothing retries on a timer, and there is no insertion event: there is no card-detect line on this wiring, and probing the bus to find out would be the same stall repeated forever.
- `SD` with no card names the pins it tried. "No card found" on its own sends somebody to re-seat a card that was never the problem, and the three failures get three different messages, because "no card", "not FAT32" and "miswired" are three different evenings.
- A card is never reformatted to make an error go away. A card that will not mount is far more often somebody's card with their files on it than a card that wants erasing.
- Screens on the card override the stock set **per file**, so one custom screen does not mean supplying all of them, and pulling the card falls back rather than losing them.
- **Fixed: a config reload gave every plugin its commands a second time.** The command table was only reset at boot, so each reload added another copy of every running plugin's table until it was full. From then on whichever plugins came last were running, shown as running, and answering "Unknown command" to their own verbs. It took a reload to show, so nothing caught it, and at four plugins it had already been costing `announce` its commands. Found because a fifth plugin made it obvious.
- **Fixed before it shipped, by the code review:** saving any CONFIG page unmounted and remounted the card, stalling every caller's line; unmounting while a caller was paused mid-screen left a descriptor into a torn-down filesystem; a failed mount left the SPI bus holding the old pins, so correcting a pin in CONFIG changed nothing and the sysop was sent to check wiring that was already right; GPIO36 was refused for MISO although input-only pins are exactly what MISO is for; the open-file budget was five against sixteen nodes, so the sixth caller silently got the flash screen instead of the card's; and the screen lookup preferred any format on the card over the right format in flash, so one stray `.asc` would have taken every C64 caller off PETSCII while looking like it worked.
- Flash is 68.1% of the slot, up from 63.9%: FATFS and the SD driver cost 63 KB, and they cost it whether or not a card is fitted.

## 0.17.0, 2026-09-19

The partition rebalance and sixteen nodes. Flashed 2026-09-19.

### Sixteen nodes and a bigger user partition

- **The flash is split by what is actually stored on it.** `storage` held 18.5 KB of screens in a 736 KB partition, sized back when the accounts lived there too, and the hundred-account cap came from the 128 KB left over rather than from anything real. Same 896 KB of data region, redistributed: `logs` 32 KB, `userdata` 608 KB, `storage` 256 KB. `storage` stays last so a filesystem upload can still only reach the screens. **Breaking layout change:** one `pio run -t erase` before the first flash, because the old contents sit where the new ones go.
- Six caller lines become sixteen. That is 65 KB more static RAM, ten more sessions at about 5.7 KB each, and the socket budget goes to 24: every caller holds one, and the listener, mDNS and the backup window want theirs.
- **Fixed: a node number above nine printed as punctuation.** `nodeChar` returned `'0' + id`, so node 10 was `:` and node 11 was `;`. Replaced by `nodeName` for prose and `nodeLabel` for the fixed column in a list. Digits rather than letters, because a node argument is parsed with `strtol`: the number in the list has to be the number you type at the prompt.
- The caller log had the same bug one layer down. DASH printed `'0' + (node % 10)`, so node 12 would have appeared as "2" — worse than a wrong glyph, because it names a different line.
- Every list that gained a column gave one back. A 40 column row that becomes 41 wraps, and a refresh screen then leaves its own tail behind on every redraw. `NODES` was at 60 and 39 columns exactly.
- DASH could not grow a row per node: it redraws from home, so a frame taller than the terminal corrupts itself. Its node block is six rows plus a summary, busy lines first and free lines filling the rest. A quiet board looks the way it always did; a busy one spends its rows on callers instead of on "waiting for caller" sixteen times. WHO still lists every line.
- `max_users` is 250, not the ~1,380 the partition now holds. The cap is an index width: the account and list indices are all `uint8_t`, and one of them is the row counter in every list on the board. Widening it touches every list, which is not work to land in the same build as a partition move. Raising it later costs no erase.

## 0.16.1, 2026-09-19

- **Fixed: a token saved by the truncating firmware was still being sent.** 0.16.0 stopped storing a short token but happily loaded one, so a board that had run the old firmware kept posting its four-character wreckage and kept minting duplicate listings. Anything under 16 characters is now ignored on load and the board registers again cleanly.
- The activity LED holds for a full second once the board is actually listening. Wi-Fi being up is not the same as the board being ready, and without a sign the only way to find out was to dial in and be refused.
- The board records why it started. A crash, watchdog or brownout reboot is written to `reboots.log` on the logs partition with the time, the next staff member to log in is told in plain words, and `SYS` shows it beside the uptime. A board that restarts on its own is otherwise invisible: the only symptom is an uptime that keeps starting over.

## 0.16.0, 2026-09-19

- **Fixed: the board kept only the first few characters of its directory token.** The reply was read with a single `recv` into a 256 byte buffer and parsed immediately, but a TCP read boundary is not a message boundary: the 32 character token arrived split across packets and the board stored the four characters that had landed. It then never matched that token again, so every heartbeat minted a brand new listing. One board produced ninety of them in fourteen hours. The reply is now accumulated until the headers are complete, and a token shorter than 16 characters is refused outright rather than overwriting a good one.
- The board has a name of its own. `board_name` in `system.cfg`, `@BOARD@` in screens, and the announce plugin starts from it instead of asking you to type it twice. `@BBS@` still means the software, so the credit line stays true. Welcome and goodbye now lead with the board.
- A caller arriving or leaving pushes an update to the directory rather than leaving it up to ten minutes out of date. `nudge_seconds` (default 60, 0 disables) is the shortest gap between pushes, so six callers arriving together is one update.

## 0.15.2, 2026-09-19

- **Fixed: refresh screens showed `??nleashed BBS`.** The row truncation added in 0.14.0 walked the text a byte at a time, so the micro sign's two bytes each went through the charset map on their own and each came back as `?`. Counting columns is the terminal layer's job now (`Term::textCols`), because it is the only thing that knows which bytes make a character.
- Ctrl-L clears the screen and redraws what you were half way through typing, the way it does in every other shell. SHIFT+CLR/HOME does the same on a C64. Every byte below 0x20 except a handful was previously discarded before it ever became a key, so Ctrl-L had never arrived at all.

## 0.15.1, 2026-09-19

- `TIME -1` takes a line off the clock: no per-call limit, no daily limit, no idle hangup, until it hangs up. `TIME n -1` does it to somebody else's node. It lasts for the call only, so nobody ends up quietly unlimited for ever. `OFF`, `NONE`, `UNLIMITED` and `NOLIMIT` all work too. The cost is that `-1` no longer means "take one minute away".
- A sysop who has made themselves visible now counts in what the board tells a directory, so a board with somebody sitting on it stops advertising itself as empty. It reports 1 of 7 rather than 0 of 6.
- `/welcome` in chat replays the screen you came in on. Not `/w`, which has been the who list since 0.10.0.
- The board reports its offset from UTC in the announce payload, so a directory can describe its busy hours in local time.

## 0.15.0, 2026-09-18

Settings you can find, and a send-off everybody gets.

- A plugin now declares what `CONFIG` should offer. Until now a plugin's settings page was built from whatever keys `system.cfg` already contained, which meant a setting nobody had written yet was invisible: the announce plugin could read a board name, owner, description, DNS name and directory list, but there was no way to set any of them short of editing the file by hand. All nine are on the form now, on a fresh board, with the running value already in them.
- A board can advertise a name of its own (`quantum.dnsfor.me`, say) instead of whatever address the directory saw, and can list itself in several directories at once with a comma-separated list. Both were always in the protocol; neither was reachable.
- The wordmark is redrawn with half-block characters, which carry two pixels per cell vertically and so allow a real stroke weight instead of chunky squares. The micro sign is set as a lowercase letter on the shared baseline with its stem below it, rather than a capital squashed to make room for a tail.
- New screens: the house rules when you press R to register, a short welcome once you are in, and a transition into chat. All three are optional, and a board without the files behaves exactly as before.
- The goodbye screen now plays however the call ended, not only when you typed BYE, and the line is held open for five seconds afterwards so it is not a screen that flashes past on its way to a closed socket. A caller who never logged in still gets the short version.
- HELP no longer truncates its own longest command.

## 0.14.0, 2026-09-18

Flashing the board stops costing you the board.

- The flash layout is split by who owns what. `storage` (736 KB) holds the screens and is the only partition a filesystem upload rewrites; `userdata` (128 KB) holds accounts, the live configuration and each plugin's files; `logs` is 32 KB, which is forty times the caller log rather than four hundred times. `storage` is kept last in the table because PlatformIO's `uploadfs` writes the last spiffs partition, so that is the only thing it can reach.
- `pio run -t flashall` is therefore no longer destructive: firmware and screens in one command, and the accounts, the configuration, the chat mail, the room ban list and the directory listing token all stay put.
- On a blank board the configuration is seeded once from the copy shipped with the screens. Without that, a fresh board would have no sysop password and no way ever to have staff.
- A restore routes each file in the backup zip back to the partition it belongs on. The zip format is unchanged and older backups still work.
- Nine checks cover the split, including simulating the destructive half of a filesystem upload and proving the accounts are still there afterwards.
- Breaking layout change: an existing board needs one full erase, because the partitions move.

## 0.13.0, 2026-09-18

The board can put itself on the map.

- `announce` plugin: a small heartbeat to a directory server so callers can find the board, and the board learns its own public address back from the reply, which is dynamic DNS for the price of a couple of hundred bytes every ten minutes. Off until switched on, never sends anything about a caller, and `ANNOUNCE TEST` prints the exact payload before anybody has to trust it. Several directories at once, comma separated.
- ANNOUNCE.md documents the wire format so anybody can run a directory, and says plainly that the default one's house rules bind the project rather than its users.
- The directory issues a token on the first heartbeat and the board writes it back into its own config through the same writer `CONFIG` uses, so a listing survives a reboot and nobody else can claim it. The reply also carries the listing's state and how long until it is public, which `ANNOUNCE` shows as `pending, public in 2h41m` rather than leaving a sysop staring at an empty page for three hours.
- `share_activity` (off by default) adds calls and caller-minutes over the last day, counted from the caller log. A directory can rank by them so a small board with five friends on it outranks a famous dead one. Counts only: no handles, no addresses, nothing about who.
- The board sends its heartbeat interval, so a directory knows when to call it quiet rather than guessing.
- The companion directory server is its own repository, also GPL v2 or later: github.com/rwmech/unleashed_directory

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
