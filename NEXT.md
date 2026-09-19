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

# 0.17.0: reclaim the board, then make it a BBS

Four things: reclaim the flash sitting idle, go to sixteen nodes, the Wi-Fi
change that unblocks a web installer, and the queued bugs. Message bases and
the SD card follow immediately after, as one piece of work.

## The hardware, confirmed rather than assumed

Read off the chip with `esptool flash_id`, not taken from the build config:

- **ESP32-D0WD-V3**, revision 3, dual core, 240 MHz, 40 MHz crystal
- **4 MB flash**, device id `0x4016`
- **No PSRAM.** This is a WROOM, not a WROVER.

`platformio.ini` says `board_upload.flash_size = 4MB`, which turns out to be
right. It was an assumption that happened to match, and it had never once been
checked against the hardware.

## The budget

- **Flash** 1,004 KB of the 1,536 KB OTA slot, **65%**
- **Static RAM** 118 KB of 320 KB, **37%**

Per-module costs measured from the current image, which every estimate below
is calibrated against:

| Module | Flash |
|---|---|
| `chat` (rooms, moderation, mail, vote to kick, colours) | 15.2 KB |
| `announce` (non-blocking HTTP, config, commands) | 9.3 KB |
| `ziparc` (the backup zip) | 10.6 KB |
| `backup` (the window) | 6.6 KB |
| `serialbridge` | 3.6 KB |

A plugin with real features costs about 15 KB. That is the yardstick.

| Part | Flash | Static RAM |
|---|---|---|
| Queued bugs and small features | +3 KB | +0.2 KB |
| Sixteen nodes | 0 | **+58 KB** |
| Wi-Fi runtime config and Improv | +6 KB | +0.3 KB |
| **0.17.0** | **~1,013 KB, 66%** | **~176 KB, 55%** |
| Message bases and SD, next | +26-37 KB | +3-5 KB |
| Poker, later | +20-30 KB | +1-2 KB |
| **Everything imagined** | **~1,080 KB, 70%** | **~183 KB, 57%** |

Flash is not the constraint and will not become one. RAM is, and sixteen nodes
is what spends it.

---

# Part 1: reclaim the wasted partition

The finding that changes the plan. The 4 MB splits into ~3.2 MB of app slots
and **896 KB of data partitions**:

| Partition | Size | Holding |
|---|---|---|
| `logs` | 32 KB | the caller log, a few KB |
| `userdata` | 128 KB | accounts, config, plugin files |
| `storage` | 736 KB | **18.5 KB of screens** |

`storage` is **97.5% empty**. It was sized when screens and accounts shared
it; the 0.14.0 split moved accounts out and nobody revisited the number.

That 128 KB is where the hundred-user cap comes from. A `UserRec` is about
450 bytes, so 128 KB holds roughly 250 accounts with nothing else on the
partition, and `BBS_MAX_USERS` sits at 100 to stay clear of it.

## The rebalance

The same 896 KB, redistributed:

| Partition | Now | New | Why |
|---|---|---|---|
| `logs` | 32 KB | 32 KB | ~50 calls, enough, and LittleFS is power-fail safe |
| `userdata` | 128 KB | **608 KB** | **~1,380 accounts** |
| `storage` | 736 KB | 256 KB | 14x what the stock screens need |

`storage` stays **last** in the table. PlatformIO's `uploadfs` writes the last
spiffs partition, and that ordering is the only reason `flashall` cannot reach
anything but the screens.

`BBS_MAX_USERS` rises to 1000, still a `system.cfg` setting so a sysop can
hold it lower.

**Accounts stay on internal LittleFS permanently.** They are the one thing
that must survive a card failing, and LittleFS is log-structured and power-fail
safe in a way FAT is not. The SD card never holds accounts.

## The cost, and why now

A partition change is a **full erase**: accounts, config and the announce
token all go. Same event as 0.14.0.

Do it in this build, while Rob owns the only board with real data on it. Once
other people are running boards, a layout change stops being free and starts
needing a migration path nobody wants to write.

**Before flashing:** open the backup window and download the zip. Restore it
afterwards. The zip carries `system.cfg` and `users.txt` and puts both back.

---

# Part 2: sixteen nodes

Six has never made the board sweat, the loop is cooperative and mostly idle,
and a board that can be full is a board worth calling back to.

`BBS_MAX_NODES` goes to 16. The cost is RAM: a `Session` is ~5.7 KB, mostly
its 3 KB timeline, and sixteen nodes plus the busy line plus the sysop is
eighteen sessions.

| Nodes | Sessions | Static RAM | of 320 KB |
|---|---|---|---|
| 6 (now) | 8 | 118 KB | 37% |
| **16** | **18** | **~176 KB** | **55%** |
| 24 | 26 | ~221 KB | 69% |

Past 24 wants PSRAM, which this module does not have. Sixteen is comfortable.

## What comes with it

- **Socket budget** from 16 to at least 24, with lwIP's per-socket memory
  reviewed alongside.
- **Busy line** boundary: the seventeenth caller now gets the busy screen.
- **The lists stop fitting.** Sixteen node rows plus a title plus a footer is
  more than 25 rows, so `WHO`, `NODES` and `DASH` need paging on a C64 screen.
  At 40 columns this is the real work in this part, not the node count itself.
- **Node characters past 9.** The marker is one character today. Check
  everything that formats a node, and `@NODES@` with it.

---

# Part 3: Wi-Fi at runtime, and Improv

The change that unblocks distribution.

## Why

`include/secrets.h` defines `WIFI_SSID` and `WIFI_PASS` and `main.cpp`
compiles them in. Any binary built today carries the builder's home network
password in plaintext and could not join anybody else's network anyway. A web
installer is impossible until this moves.

## 3.1 Credentials move to the config

- `wifi_ssid` and `wifi_pass` in `system.cfg` on `userdata`, surviving a
  reflash like everything else there.
- `secrets.h` stays as a build-time default only, used when the config has no
  SSID, and leaves the documented path.
- `CONFIG board` gains both, the password as `CK_PASS` so it masks and is only
  written when retyped.
- Changing either needs a reboot, and the form says so.

## 3.2 The backup zip: not redacted, but the port refuses strangers

The zip carries `system.cfg`, so it now contains the Wi-Fi password. **Rob's
call and it stands: not redacted.** The window is a separate port, opened by
hand with the BOOT button, and it closes itself.

Two things instead of redaction.

**Say it where it is used.** The window already announces itself when the
button is pressed. That line gains the constraint, so nobody has to have read
any documentation:

```
*** Backup open 5 min, local network only
    http://192.168.1.50:8080/backup.zip
    Holds your accounts, settings and wifi password. Not reachable
    from outside, and never forward this port.
```

Minutes and address come from the live config, so a board set to ten says ten.

**And enforce it.** The download needs no confirmation by design, which is
right for a sysop at their own desk and wrong for a stranger who found an open
port. The backup server refuses any source address that is not private:
`10/8`, `172.16/12`, `192.168/16`, `127/8` and the IPv6 equivalents. A
forwarded port then cannot be used from outside at all, including when UPnP
forwarded it without anybody asking. About 20 lines in `src/core/backup.cpp`.

A VPN still works, since that puts the caller on a private address.

## 3.3 No credentials, no board

On boot with an empty `wifi_ssid`: the console says how to set it and repeats
every 30 seconds, the LED gives a slow double blink distinct from the one
second ready flash, and Improv is listening.

## 3.4 Improv Wi-Fi Serial

The protocol ESP Web Tools speaks over the serial connection it just flashed
with, so a browser can ask for the network and hand it over. This is how WLED
does it.

A framed state machine on the console UART alongside the logging. Packets
start with `IMPROV` so ordinary log output is never mistaken for one. Four
commands: state, device info, scan, set credentials. On success it returns the
address to dial. Roughly 4-5 KB.

**Verified by:** a scripted Improv client against the host build's serial
loopback, plus one manual check by Rob with a real browser and a real board.

---

# Part 4: the queued bugs

Small, independent, and worth doing first so the build shows results early.

## 4.1 Node lists disagree about a caller who has not logged in

Measured on the host: `WHO` says `(connecting)`, `DASH` says `(logging in)`,
`NODES` says neither. One helper for all three. `(connecting)` during
`SState::Detect` and `Intro`, `(logging in)` from the handle prompt on,
`-- waiting for caller --` only for a genuinely `Free` node.

## 4.2 No bell when anybody arrives

Only pages, broadcasts and form errors ring today. Bell then notice, as a page
already does, on login and on chat join. `bell_login` and `bell_chat`, both
`yes`. A caller gets no bell for their own arrival, and `DND` suppresses both.

## 4.3 Chat does not show the room by itself

A `roster` setting in minutes, default 10, `0` off. Printed through the
held-line path so it never lands mid-sentence, only when the room has changed
since the last one, and never to a caller idle longer than the interval.

DDial's actual behaviour is not documented anywhere findable. This is a
design, not a reconstruction.

## 4.4 Sysop page

`SYSOP [message]` rings the operator on their node and on the console. Needs
an away state that tells the caller rather than leaving them hanging, a way to
decline that says so, a limit of three per call, and an entry in the caller
log since it is the sysop's own record.

---

# Next build: message bases and SD, together

One piece of work. A message base that expects a card cannot ship before the
card support does.

## 5.1 What lives where

| Internal flash (LittleFS) | SD card (FAT32) |
|---|---|
| Firmware | Message bases |
| Wi-Fi, staff passwords, announce token | File areas |
| **Accounts, up to ~1,380** | Optional: bigger logs, custom screens |
| Stock screens, 18.5 KB | |
| Caller log | |

**Without a card the board is complete**: chat, mail, accounts, screens,
caller log, directory listing, serial bridge, and a handful of messages for
trying it out. More than CBBS had. The card is what a board with ambitions
adds.

Screens stay internal because 18.5 KB against a 256 KB partition is not a
problem worth solving. SD **overrides** them when present, which is not
duplication: internal holds the set that ships, the card holds the sysop's
own. Pull the card and the board runs on stock screens rather than failing.

## 5.2 Why FAT32

Not convenience. **Pulling the card and reading your messages on a laptop is
the "you own your data" claim made physical.** LittleFS on the card would be
consistent and unreadable anywhere else.

The cost is power-fail safety and it is real: FAT's file, table and directory
updates are not atomic, and a board losing power mid-write can lose a cluster
chain. Mitigated by keeping everything that must survive on internal LittleFS,
by write-then-rename with an explicit flush, and by the fact that the format
creating the risk is also the one a sysop can repair with `chkdsk`.

## 5.3 Message storage

Fixed-size records, so a message is found by seeking rather than reading. 1 KB
of text each, which on a card is free and on internal flash is exactly why the
no-card mode is capped at a handful.

**A version byte in every file header from the first release.** When a record
format changes the plugin migrates or refuses with a clear reason. Retrofitting
that after people have real boards is expensive; doing it now is free.

RAM holds a per-area index, so it scales with message **count**, not size. On
a card holding thousands, the index is paged rather than held whole.

## 5.4 Areas, reading and writing

Areas are named, with read and write levels, defined in `system.cfg`. Messages
are flat within an area, numbered from 1, with a `reply_to` so a thread can be
followed without the storage being a tree. No editing; delete by author or
staff.

`AREAS`, `AREA n`, `MSGS`, `READ [n]`, `POST`, `REPLY n`, `KILL n`. All
`CF_ACCOUNT`, so guests read and do not post. Unread tracking is one
`uint16_t` high-water mark per area on `UserRec`.

Writing a message on a terminal from 1982 is the hard part, not the storage.
ANSI gets the `Form` widget plus a simple full-screen body editor. PETSCII at
40 columns gets the same with a line-based body. Plain ASCII gets line entry
with `.S` to save and `.A` to abort, which is also the fallback whenever the
editor cannot be drawn.

## 5.5 The file manager, and what it eventually replaces

File areas need a file manager: browse, describe, upload, download. Two
consequences.

**XMODEM/YMODEM is what makes it work remotely.** Pulling the card serves a
sysop standing next to the board; a caller in another state needs a transfer
protocol. But the file *area* ships before the protocol does: browse and read
descriptions first, downloads after.

**It eventually replaces the backup window.** `backup.cpp` and `ziparc.cpp`
are **17.2 KB** between them, more than the serial bridge and example plugin
combined. A file manager with transfers does everything the window does and
lets a co-sysop do it from another state.

Do not reclaim that space on a promise. The zip is **atomic**, capturing
config, accounts and screens at one instant and validating them as a set, and
a file manager that can read and write anything is a far larger permission
surface than a time-boxed, physically triggered window. Ship the file manager,
run a board on it, then delete 17.2 KB with confidence.

---

# Later: poker

Specified, and after the above. Most fun, least load bearing.

C++ rather than Lua: no Lua runtime exists and one costs 100-220 KB of flash
plus 20-40 KB of heap per state, more than message bases, files and poker put
together. Everything poker needs lives in C++ anyway. Shape `deal`, `act`,
`showdown` and `render` as the interface a Lua door API would later expose, so
Lua arrives with a working reference to port rather than a blank file.

Six seats, because six fits 40 columns without cruelty. Unlimited observers at
read level, never seeing a hole card before showdown. A state machine from
`tick()`, a 45 second action clock, Fisher-Yates from `esp_random()` and never
`rand()`. A compact 7-card evaluator without large tables, 3-5 KB, slow and
irrelevant at one showdown every few minutes. Play chips per account in
`p/poker/chips.dat` on `userdata`.

Budget the 40 column rendering **before** writing the logic. A poker room a
C64 cannot play at misses the point of the project.

---

# Deferred

Build profiles, GPIO (needs Rob's pin list), OTA updates, zones and
maintenance mode, hardware watchdog feeding, 40/80/terminal-width support, a
carrier PCB, Lua once there is a door worth porting, HA still parked on TLS
memory.

**Not needed: a 16 MB module.** Pin-compatible and a drop-in if wanted, but
the rebalanced partition table gets ~1,380 accounts out of the 4 MB board that
already exists. A WROVER with PSRAM is only worth looking at if nodes ever go
past 24.

**Keep OTA.** Both 1.5 MB app slots stay. Dropping `ota_1` would free 1.5 MB
with no better use, and OTA is the only update path that needs no cable: a web
installer still talks over USB.

---

# Open questions for Rob

1. **Message size.** 1 KB per message or 2 KB? On a card either is free; this
   only decides how many fit in the no-card test mode.
2. **Areas.** Three fixed to start, or sysop-definable from the beginning?
3. **Poker chips.** Same stack every session, or a persistent bankroll that can
   run dry and be topped up?
4. **Poker seats.** Six, or fewer for a roomier 40 column table?
5. **Showdown.** Do observers see every hole card, or only those actually
   shown?

---

# How this build is verified

- `bbs-regression` before every commit: both targets, the suite, sizes against
  the figures above.
- `bbs-qa` for anything a caller sees, in all three terminal types. Sixteen
  node rows on a 40 column screen especially.
- `code-review` on the Improv state machine and the partition change: a serial
  protocol and a storage layout are both places where a partial read has
  already cost this project a day.
- Rob flashes, and backs up first. Nothing here is done until he has.
