<!--
 ===========================================================================
  µnleashed BBS
  Electronic freedom on a microcontroller.
 ===========================================================================

 File:         NEXT.md
 Module:       Documentation / next build specification

 Purpose:      What 0.17.0 contains, why, and what it costs. Written to be
               built from: every section says what changes, where, and how
               it is verified.

 Audience:     Whoever builds it. Decisions still open are marked and
               belong to Rob.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v2 or later
 SPDX-License-Identifier: GPL-2.0-or-later
 ===========================================================================
-->

# 0.17.0: the build that makes it a BBS

Three things at once: the small stuff that has piled up, the one change that
unblocks a web installer, and message bases, which are the feature that turns
this from a chat system with accounts into a bulletin board.

Poker is specified here too but is the build after, for reasons in its own
section.

## The budget

Measured, not estimated. Current build:

- **Flash** 1,004 KB of the 1,536 KB OTA slot, **65%**
- **Static RAM** 118 KB of 320 KB, **37%**

Per-module costs from the current image, which is what the estimates below
are calibrated against:

| Module | Flash |
|---|---|
| `chat` (rooms, moderation, mail, vote to kick, colours) | 15.2 KB |
| `announce` (non-blocking HTTP, config, commands) | 9.3 KB |
| `serialbridge` | 3.6 KB |
| `example` | 1.6 KB |

Chat is the yardstick: a plugin with real features costs about 15 KB.

Projected after this build:

| Part | Flash | Static RAM |
|---|---|---|
| Bugs and small features | +3 KB | +0.2 KB |
| Wi-Fi runtime config and Improv | +6 KB | +0.3 KB |
| Message bases | +18-25 KB | +2-4 KB |
| **0.17.0 total** | **~1,035 KB, 67%** | **~122 KB, 38%** |

And with everything currently imagined:

| Later | Flash | Static RAM |
|---|---|---|
| Poker | +20-30 KB | +1-2 KB |
| SD card plugin | +8-12 KB | +1 KB |
| XMODEM / YMODEM and file areas | +12-18 KB | +2 KB |
| **Everything** | **~1,090 KB, 71%** | **~127 KB, 40%** |

There is room for all of it with about 450 KB of the slot spare. For contrast
a Lua runtime is 100-220 KB of flash before a single door exists, plus 20-40 KB
of heap per state: more than message bases, poker, files and SD combined.

Note for later: a disabled plugin costs no RAM, because it claims nothing until
`start()`, but its code is still in the image. Reducing **flash** means
compiling it out, which is the build-profiles work. At 65% that is not urgent.

---

# Part 1: bugs and small features

Everything Rob has reported that is not already fixed. Small, independent, and
worth doing first so the build has visible results early.

## 1.1 Node lists disagree about a caller who has not logged in

**Seen:** a node with somebody mid-login reads differently in each list.

**Measured on the host:** `WHO` says `(connecting)`, `DASH` says
`(logging in)`, `NODES` says neither. The handling exists in
`rowWho`/`rowDash` and the strings differ; `NODES` takes a different path.

**Change:** one helper used by all three. `(connecting)` while
`SState::Detect` or `Intro`, `(logging in)` from the handle prompt onward,
`-- waiting for caller --` only for a genuinely `Free` node.

**Where:** `src/core/bbs_shell.cpp`, `rowWho`, `rowDash`, and the nodes path.

**Verified by:** a caller that connects and stops at the handle prompt while
another session reads all three lists. Add to `tools/testclient.py`.

## 1.2 No bell when anybody arrives

**Seen:** a caller logging in or joining chat is silent. The only bells are
pages, broadcasts and form errors.

**Change:** bell then notice, the way a page already does it, on login and on
chat join. Two settings so a busy board can turn them off separately:
`bell_login` and `bell_chat`, both `yes` by default.

**Where:** `completeLogin` in `src/core/bbs.cpp`, `join()` in
`src/plugins/chat.cpp`.

**Watch for:** a caller in chat should not get a bell for their own join, and
`DND` must suppress both.

## 1.3 Chat does not show the room by itself

**Seen:** Rob, "show `/s` automatically after a period of time, see what DDial
did".

**Honest note:** DDial's actual behaviour is not documented anywhere I could
find. Wikipedia and ddial.com describe a seven line Apple II chat server at
300 baud and nothing about how it displayed the roster. This is a design, not
a reconstruction.

**Change:** a `roster` setting in the chat section, minutes, default 10, `0`
off. On the timer, print the room list, but:

- through the held-line path chat already uses, so it never lands in the
  middle of somebody typing
- only if the room has changed since the last one. An unchanged list repeating
  on a timer is noise and people stop reading it
- never to a caller who has been idle longer than the interval: they are not
  reading it either

**Where:** `tick()` in `src/plugins/chat.cpp`.

## 1.4 Sysop page

**Seen:** every board had one. `PAGE` today is caller to caller.

**Change:** `SYSOP [message]` rings the operator wherever they are, on their
own node and on the console. Needs:

- an away state the sysop sets, with the caller told rather than left hanging
- a way to decline that says so
- a rate limit per caller per call, so one person cannot ring the bell all
  night. Three per call, then "the sysop has been told"
- the page goes in the caller log, since it is the sysop's own record

**Where:** new command in `src/core/bbs_shell.cpp`, state on `Session`.

## 1.5 Wi-Fi credentials are compiled into the firmware

Covered in Part 2. Listed here because it is a **bug**, not a feature: any
binary built today carries the builder's home network password in plaintext.

---

# Part 2: Wi-Fi at runtime, and Improv

The one change that unblocks everything about distribution.

## Why

`include/secrets.h` defines `WIFI_SSID` and `WIFI_PASS` and `main.cpp` compiles
them in. Two consequences:

1. A published binary contains whoever built it's Wi-Fi password. `strings`
   finds it in seconds.
2. A shared binary could not join anybody else's network anyway, so a web
   installer is impossible, not merely awkward.

## 2.1 Credentials move to the config

- `wifi_ssid` and `wifi_pass` in `system.cfg`, on the `userdata` partition, so
  they survive a reflash like everything else there.
- `secrets.h` stays supported as a **build-time default only**, for people who
  want it, and is used only when the config has no SSID. It is removed from
  the documented path.
- `CONFIG board` gains both. The password is a `CK_PASS` field, so it shows as
  a mask and is only written when retyped, like the staff passwords.
- Changing either needs a reboot, and the form says so.

**The backup zip.** It carries `system.cfg`, so a backup now contains the
Wi-Fi password. Rob's call, and it stands: **not redacted**. The backup window
is a separate port, opened by hand with the BOOT button while the sysop is
logged in, and it closes itself. A sysop who forwards that port has made a
different mistake.

Two things follow from that rather than from redaction.

**Say it where it is used, not where it is documented.** The window already
announces itself on the sysop's screen when the button is pressed:

```
*** Backup open 5 min: http://192.168.1.50:8080/backup.zip
```

That line gains the constraint, so nobody has to have read anything:

```
*** Backup open 5 min, local network only
    http://192.168.1.50:8080/backup.zip
    Holds your accounts, settings and wifi password. Not reachable
    from outside, and never forward this port.
```

Both numbers come from the live config rather than being written into the
string, so a board with `backup_window_minutes = 10` says ten. BACKUP.md gets
the same line, but the screen is the one that matters: it is in front of the
only person who can act on it, at the moment they are acting.

**And enforce it, because documentation only protects people who read it.**
The download needs no confirmation by design, which is the right call for a
sysop at their own desk and the wrong one for a stranger who found an open
port. The backup HTTP server should refuse any connection whose source address
is not private: `10/8`, `172.16/12`, `192.168/16`, `127/8`, and the IPv6
equivalents. A forwarded port then still cannot be used from outside, whether
it was forwarded deliberately, by accident, or by UPnP without anybody asking.
About 20 lines in `src/core/backup.cpp`, and it turns a documented rule into a
structural one.

A sysop who genuinely wants remote backups can reach the board over a VPN,
which puts them on a private address and works unchanged.

## 2.2 No credentials, no board

A board with no SSID cannot do anything useful, so it has to say so rather
than sit silently retrying. On boot with an empty `wifi_ssid`:

- the console prints how to set it, and keeps printing every 30 seconds
- the LED gives a slow double blink, distinct from the one second "ready"
  flash
- Improv, below, is listening

## 2.3 Improv Wi-Fi Serial

The protocol ESP Web Tools speaks over the same serial connection it flashed
with, so a browser can ask for the network and hand it to the board. This is
how WLED does it.

- A small state machine on the console UART, alongside the existing logging.
  Packets are framed and start with `IMPROV`, so ordinary log output is not
  mistaken for one.
- Supports the four commands: current state, device info, scan for networks,
  and set credentials.
- On credentials: write them to `system.cfg`, join, and report the result. On
  success return the URL to dial: `telnet://<ip>:6400`.
- Estimated 4-5 KB of flash.

**Verified by:** the host build cannot speak serial to a browser, so this
needs a scripted Improv client on the host build's serial loopback, plus one
manual check by Rob with a real browser and a real board.

## 2.4 Then, and only then, the installer

Not in this build. Once 2.1 to 2.3 are done and Rob has flashed and confirmed:

- `releases/` in the repo with the four binaries, a `manifest.json` for ESP Web
  Tools, and a `THIRD_PARTY_NOTICES` covering ESP-IDF, LittleFS, mDNS and the
  ROM inflate
- the directory server hosts a copy while the firmware repo is private, with
  `deploy/update.sh` pulling the current one
- a `/install` page on the directory with the install button

---

# Part 3: message bases

The feature that makes this a bulletin board rather than a chat system with
accounts.

## Why, since Rob asked whether they would be used

Chat needs two people online at once. On a board with a handful of callers
that almost never happens. A message base is the thing that works when nobody
else is there, and it is the reason somebody calls back tomorrow. It is also
what CBBS actually was: in 1978 one phone line meant you could not all be on
at once, so the messages were the board.

They do not need the SD card. At roughly 300 bytes a message the 128 KB
`userdata` partition holds 300 to 400, which is more than a small board will
produce in a year. SD raises the ceiling later; it is not a prerequisite.

## 3.1 Shape

- **Areas.** Named, each with its own read and write level, defined in
  `system.cfg`. Start with three on a fresh board: General, Sysop, and one the
  sysop renames.
- **Messages** are flat within an area, numbered from 1, with a `reply_to`
  field so a reader can follow a thread without the storage being a tree.
- **No editing.** A message can be deleted by its author or by staff. Editing
  history is a problem nobody on a board this size needs.

## 3.2 Storage

One file per area on `userdata`, `p/msg/<area>.dat`, fixed-size records so a
message can be found by seeking rather than by reading the file:

```
struct MsgRec {
    uint16_t num;              // 1-based within the area, 0 = deleted
    uint16_t replyTo;          // 0 = not a reply
    char     from[BBS_USER_MAX + 1];
    char     to[BBS_USER_MAX + 1];   // empty = to all
    char     subject[41];
    uint32_t at;               // epoch
    uint16_t len;
    char     text[BBS_MSG_CHARS + 1];
};
```

`BBS_MSG_CHARS` starts at 1024. A record is then about 1.1 KB, and a 128 KB
partition shared with accounts and plugin files holds perhaps 60 to 80 at that
size. **Open question for Rob below.**

RAM holds only a small index per area, rebuilt at start: number, author, date,
subject offset. At 48 bytes an entry and 64 entries that is 3 KB per area,
which is why areas are capped at three until SD lands.

Written through a temp file and renamed, like `users.txt` and `mail.dat`.

## 3.3 Reading and writing

Commands, all `CF_ACCOUNT` so guests can read but not post:

| Command | What it does |
|---|---|
| `AREAS` | list the areas, with unread counts |
| `AREA n` | choose one |
| `MSGS` | list messages in the current area, newest first, paged |
| `READ [n]` | read one, or the next unread |
| `POST` | write one |
| `REPLY n` | write one with `reply_to` set and the subject carried over |
| `KILL n` | delete, author or staff only |

Unread tracking is one `uint16_t` high-water mark per area on the `UserRec`,
which is one new field and one new `kUserFields` row.

## 3.4 Writing on a terminal from 1982

This is the hard part, not the storage.

- **ANSI:** the existing `Form` widget for subject and recipient, then a
  simple full-screen editor for the body: type, backspace, arrow keys within
  the current line, F1 to save, ESC to abandon.
- **PETSCII at 40 columns:** the same, but the body editor is line-based. No
  cursor addressing beyond what the form already does.
- **Plain ASCII:** line by line, `.S` to save, `.A` to abort, the way every
  board did it. This is also the fallback whenever the editor cannot be drawn.

Reuse `LineEditor` for a line rather than writing a second editor.

## 3.5 Cost

Estimated 18-25 KB of flash, 2-4 KB of static RAM. Storage is the real
constraint and it is a setting, not a code problem.

---

# Part 4: poker

Specified now, built after 0.17.0. It is the most fun and the least load
bearing, and message bases should not wait behind it.

## Why C++ rather than Lua

Rob asked and the answer has not changed: no Lua runtime exists, and adding one
costs 100-220 KB of flash plus 20-40 KB of heap per state before a card is
dealt. Everything poker needs (shared table state, an action clock, chip
persistence, a shuffle from the hardware RNG) lives in C++ regardless, so a Lua
layer would mostly call back into C++.

Build it in C++, and deliberately shape `deal`, `act`, `showdown` and `render`
as the interface a Lua door API would later expose. Then Lua arrives with a
working reference to port rather than a blank file.

## 4.1 Shape

- One table, Texas hold'em, **six seats**. Six because the board has six caller
  nodes, and because a six-seat table fits 40 columns without cruelty.
- Unlimited observers at read level. Observers never see a hole card until
  showdown, and then only cards that were shown.
- The plugin owns a joined session the way chat does. `POKER` from the prompt,
  or `/poker` from the chat room, and `/q` returns to wherever they came from.

## 4.2 The hand

A state machine driven from `tick()`: waiting, blinds, preflop, flop, turn,
river, showdown, payout. An action clock per player, default 45 seconds,
folding on timeout.

The shuffle is Fisher-Yates from `esp_random()`, which is the hardware RNG.
Never `rand()`.

Hand evaluation: a compact 7-card evaluator without large lookup tables,
about 3-5 KB. Slower than a table-driven one and irrelevant at this scale,
where a showdown happens once every few minutes.

## 4.3 Chips and statistics

Play money, per account, in `p/poker/chips.dat` on `userdata` so it survives a
reflash. Statistics per account: hands played, hands won, biggest pot, total
time at the table. Shown by a `POKER STATS` command and on the caller's
`INFO`.

**Open question below** on whether a bankroll can run dry.

## 4.4 The table on a 40 column screen

The actual design problem. Six seats, a board of five cards, a pot, and a
prompt, in 40 columns and 25 rows, redrawn without flicker on a C64 at 1200
baud. Card notation is two characters (`As`, `Kh`), suits coloured where the
terminal has colour and lettered where it does not.

Budget the rendering before writing the logic: if it does not fit on a C64 it
does not ship, because a poker room a C64 cannot play at misses the point of
the project.

## 4.5 Cost

Estimated 20-30 KB of flash, 1-2 KB of static RAM plus per-session render
scratch.

---

# Deferred, deliberately

Not in this build, listed so they are not forgotten:

- Build profiles. Worth doing when flash gets tight, which at 65% it is not.
- SD card plugin, then XMODEM/YMODEM and file areas.
- GPIO plugin. Needs Rob's pin list.
- OTA updates.
- Zones, maintenance mode, hardware watchdog feeding.
- 40/80/terminal-width support, still queued from way back.
- A carrier PCB.
- Lua, once there is a door worth porting.
- HA, still parked on TLS memory.

---

# Open questions, for Rob

1. **Message size.** 1 KB a message gives 60-80 messages on `userdata`. 512
   bytes doubles that and is still four times what DDial allowed. Which?
2. **Areas.** Three fixed areas to start, or sysop-definable from the
   beginning? Three is simpler and the index RAM is why.
3. **Poker chips.** Everyone starts each session with the same stack, or a
   persistent bankroll that can run dry and need topping up by the sysop?
4. **Poker seats.** Six to match the node count, or fewer for a roomier 40
   column layout?
5. **Showdown.** Do observers see every hole card at showdown, or only those
   the players actually showed?

---

# How this build is verified

- `bbs-regression` before every commit: both targets built, suite run, sizes
  reported against the figures at the top of this file.
- `bbs-qa` for anything a caller sees, in all three terminal types. Message
  posting and the poker table both qualify.
- `code-review` on the storage formats and the Improv state machine in
  particular: fixed-size records and a serial protocol are both places where a
  partial read has already cost this project a day.
- Rob flashes. Nothing here is done until he has.
