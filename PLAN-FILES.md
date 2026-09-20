<!--
 ===========================================================================
  µnleashed BBS
 ===========================================================================

 File:         PLAN-FILES.md
 Purpose:      The file subsystem, start to finish: the bugs in the way, the
               configuration it needs, the transfer protocol, and the two
               things the protocol is for.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v2 or later
 SPDX-License-Identifier: GPL-2.0-or-later
 ===========================================================================
-->

# The file subsystem

Rob's order, and it is the right one: nothing can be built on a broken
foundation, nothing can be configured through the wrong interface, and
download and upload are both the same protocol wearing different hats.

| Phase | What | Depends on |
|---|---|---|
| 1 | Bug fixes | nothing |
| 2 | Configuration fixes | 1 |
| 3 | FILES as a subsystem | 2 |
| 4 | XMODEM | nothing, but lands after 3 |
| 5 | Download | 3, 4 |
| 6 | Upload | 3, 4, 5 |
| 7 | ZMODEM | 5, 6, and measurements from them |

---

## Phase 1: bug fixes

Small, and all of them are in the way of the rest.

- **A mangled literal in `files.cpp`.** A `'\0'` became a real NUL byte in
  the source through a shell heredoc. In the working tree now, warns, must go
  before anything builds clean. This is the fourth time a heredoc has eaten an
  escape in this project and the rule against it is already written down.
- **An absolute card path is not understood.** `SD` prints the screens folder
  as `/sd/screens`, so that is what a sysop types, and the area code treats a
  path as relative to the card and turns it into `/sd//sd/screens`, silently
  pointing the area at nothing. `/sd/screens`, `sd/screens`, `/screens` and
  `screens` must all mean the same folder. Half-written in the working tree.
- **`SHOW`, `HIDE` and `LURK` do not tell the directory.** `publicBusy()`
  counts a sysop line only when it is visible, so toggling visibility changes
  the figure the directory publishes and nothing nudges it. The fix is one
  hook meaning "what the outside can see about who is on has changed", called
  from login, logoff and all three, not three calls bolted onto three
  commands.

## Phase 2: configuration fixes

`CONFIG files` is the wrong shape and Rob is right about it. Four values
crammed into one text row: the format is invisible unless you already know
it, `Area 1` as a label says nothing, and there is no way to set a level per
area without knowing to type pipes.

**Each area row becomes a button that opens its own page** (Rob's shape, and
better than a separate `AREAS` command: one way in, and the nesting is where
the detail belongs rather than crammed into a row).

- On `CONFIG files`, `Area 1` is a button showing a summary, the area's name
  or its path, so the page still tells you what is configured at a glance.
- Enter on it opens **Area 1's own page**: **Path** (text), **Name** (text),
  **Read** (`FF_CYCLE`), **Write** (`FF_CYCLE`). The cycle field is what the
  user manager already uses for a caller's Level, so a level becomes a thing
  you step through rather than a word you have to spell correctly.
- Save returns to the `files` page, which is the behaviour a nested form
  should have and the thing that makes it feel like one interface.
- Save writes back through `syscfg::write`, the same path `CONFIG` uses, so
  the file keeps its comments and its order.
- The stored form stays `path | name | read | write` so a hand-edited
  `system.cfg` still works and is still readable.

**What this needs that does not exist yet**, and is the actual work in this
phase:

- **A button field.** `Save` and `Cancel` are hardcoded at the bottom of the
  form, not a field type. This needs `FF_ACTION`: a field drawn as a button,
  focusable, where Enter returns a new `Form::Res::Open` along with which
  field it was. About the same size as `FF_CYCLE` was.
- **A page stack in CONFIG.** Today a page is opened and saved or cancelled.
  A sub-page has to remember where it came from and go back there, which is
  one more piece of session state and the thing to get right: a caller
  dropping the line inside a sub-page must not leave the editing guard held.
- Worth doing once and properly, because the same button-opens-a-page shape
  is what the queued dashboard rework and any future multi-page settings
  will want.

Also here: the board creates an area's folder on save, which it already does
at start.

## Phase 3: FILES becomes a subsystem

Rob's shape. `FILES` should not be a command that prints a list; it should be
somewhere you go.

- `FILES` **enters** the subsystem, the way `CHAT` enters the room. The
  plugin owns the session (`Bbs::own`), so it gets the keys and its own
  prompt, and `Q` or ESC leaves.
- **A screen plays on the way in**, `screens/files.*`, the same optional
  treatment `chatin` gets for the chat room: a board without the file plays
  nothing and carries on.
- Then **a menu of the areas, in columns**, numbered 1 to N. The number of
  columns comes from the terminal width: one column at 40, two or three at
  80. A C64 caller and a PuTTY caller should both get something that fits
  rather than both getting the narrow case, which is the mistake already made
  once with the description column and fixed.
- Picking a number opens that area: its files, with sizes and descriptions.
- Inside an area the keys are the point: **D** download, **U** upload, **Q**
  back to the area menu. That is where phases 5 and 6 land.
- Areas a caller may not read are not in the menu and their numbers are still
  their numbers, as now.

## Phase 4: XMODEM

The protocol on its own, testable without a card.

- Checksum and CRC-16, 128 byte and 1K blocks. Receive and send.
- **IAC escaping is a requirement, not a risk.** A byte of 0xFF in the data
  is telnet's IAC and has to be doubled on the way out and collapsed on the
  way in. Every BBS and every client does this, which is why XMODEM over
  telnet works everywhere; it is about ten lines and the outbound half is
  already done in `Term::iacEscape`.
  What makes it worth naming is the failure mode, which hides from testing:
  skip it and an ASCII file transfers perfectly, because it contains no 0xFF
  at all. The first thing to break is somebody's .PRG or .ZIP, silently, in a
  way that reads as line noise. **So the test suite transfers a file full of
  0xFF bytes**, not a text file.
- **Raw mode.** No charset translation, no PETSCII mapping, no CR/LF
  rewriting. The session has to stop being a terminal for the duration.
- **Nothing may block.** A transfer runs from the plugin's key handler and
  its tick, a block at a time, inside the cooperative loop. A read from the
  card is a blocking FAT read: fast, but it needs measuring before a 1K block
  per packet is assumed safe with sixteen nodes on.
- **Three transfer slots, and browsing is not limited** (Rob's call, refined).
  Listing a folder needs no buffer at all, so the limit belongs on transfers
  rather than on the room: three concurrent transfers, and a fourth caller is
  told the drives are busy. Authentic, and every real board worked that way.
  No read-ahead buffer: a 1K read at 20 MHz is well under a millisecond, so
  read a block per packet straight into the block buffer. Three slots is then
  3 KB of static RAM rather than the 15 KB a read-ahead scheme would want.

### YMODEM comes with it

XMODEM cannot say what a file is called or how long it is, so a caller names
the file themselves and the last block arrives padded with 0x1A. YMODEM is
the same engine with a block zero carrying the name and the exact size, which
is perhaps twenty percent more code and removes both problems. Build one
engine that speaks both, prefer YMODEM, fall back to XMODEM for anything that
cannot do it.

## Phase 5: download

- `D` in an area, pick a file, the board sends it.
- The caller's terminal has to be told to start receiving, which is a thing
  the sysop's documentation must cover because it is the part people get
  wrong.
- **The per-call clock must not cut a transfer off mid-file.** Do both: pause
  the clock during a transfer, the way the idle clock already pauses for a
  plugin-owned session, and tell a caller up front when a file plainly will
  not fit in the time they have left.
- Set expectations on speed in the documentation. XMODEM is stop-and-wait, so
  throughput is bounded by round-trip time per block rather than by
  bandwidth: roughly 50 KB/s on a LAN with 1K blocks, nearer 10 KB/s across
  the internet at 100 ms. A megabyte is a couple of minutes. That is normal,
  and saying so stops somebody concluding the board is broken.

## Phase 6: upload

- `U` in an area, the board receives into it.
- **The write level is checked at the moment of writing, not at the moment of
  entering the area.** Easy to get wrong months later by reading `CF_WRITE`
  on the command and assuming that is the whole story. An area can be read by
  staff and written by the sysop alone, and the upload path has to honour
  that.
- Write to a temp name, then rename, the same discipline the description file
  already uses: FAT is not power-fail safe and a half-received file that
  looks like a whole one is worse than no file.
- Refuse a name that is not safe, the same check `DESC` uses.
- Free space is checked before accepting, not discovered at the end.
- An upload area with approval, and moving files between areas, is Rob's
  later ask and belongs after this works at all.

## Phase 7: ZMODEM

Rob wants it and it is worth having: it streams instead of waiting for an
ACK per block, which is five to ten times faster on a link with any latency,
and it resumes a broken transfer.

It is deliberately last, and not as a brush-off.

**Streaming is the whole point and also the whole cost.** A sender that does
not wait for an ACK has to hold everything it has sent but not had confirmed,
ready to send again from wherever the receiver says it went wrong. That
buffer is the RAM, and keeping it fed without stalling a cooperative loop
that also has fifteen other callers on it is the hard part. Built
half-heartedly it has ZMODEM's complexity and XMODEM's speed, which is the
worst of both.

**The reference client cannot use it.** A C64 through TeensyROM is a 1 MHz
6510 doing CRC-32 on a continuous stream with a sliding window to manage. It
will negotiate its way down to XMODEM. So ZMODEM is a speed improvement for
SyncTERM and PuTTY users and buys nothing at all for the machine this project
exists for, which is a reason to do it properly rather than early.

**It wants measurements that phases 5 and 6 produce.** What the socket
actually sustains, what a card read actually costs under load, how much of
the loop sixteen callers leave free. Those are the numbers that size a
retransmit window. Guessing them first and measuring afterwards is how the
buffer ends up either useless or enormous.

Also worth knowing: ZMODEM has its own escaping (ZDLE) layered under
telnet's IAC doubling, so two escaping schemes end up stacked. That is
normal and every BBS does it, but it is a place to be careful rather than
clever.

---

## What this unlocks, and why the order matters

Pointing an area at `/sd/screens` is the case that shows the design is right:
an area is a name on a path, not a managed store, so that area **is** the
screens folder. When phase 6 lands, uploading a screen into it replaces the
board's screen in place, with no import step and no second copy to
reconcile. That is only true because areas were built as paths, and it is
worth not losing.
