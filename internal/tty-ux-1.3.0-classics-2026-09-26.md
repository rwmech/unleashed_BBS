# tty-ux: the 1.3.0 BBS classics, 2026-09-26

The screens for the six 1.3.0 classics: rotating information pages, sysop
chat, oneliners with a word list, the CALLS top tens, the voting booth, and
credits. Each at 40 columns (PETSCII-40) and 80 (ANSI, PETSCII-80, plain
ASCII), with plain ASCII's line-prompt fallback.

Method: the host build at 1.1.2-dev.3 (a055624) on 127.0.0.1:6871, tag
`ttyux130`, with a card. Real callers at ANSI-80, PETSCII-40 and plain
ASCII, their bytes played onto a grid (`render_lines` for ANSI, a C64 screen
model for PETSCII). Every proposed mock-up below was generated from the
column rules stated beside it and checked by script: no row passes 39 at
40 or 79 at 80. A reverse bar is drawn as its text; the bullets say which
rows are bars.

## Verdict

The six fit the board's existing furniture with almost no new shapes: the
title bar, the rule, the `-->` voice, the cursor menu with its number
fallback, CONFIG's composite sub-pages and the form. What needs building is
a bar-chart row and a shared cursor menu, both used three times, and a
16-byte-a-record tally file that three features share. Nothing adds a byte
to a `Session`; the whole set is about 430 bytes of static DRAM, and the
largest piece of that can be made smaller. Sysop chat is already built
(1.1.0, 1.1.1) and works; what it lacks is an arrival, and measuring it
found the worst thing in this report, which is not new at all: a PETSCII-40
caller answered by the sysop gets an 80-column screen wrapped into 23 rows
before the conversation starts. Three stock screens have no 40-column
version. That, and a room line that loses its last words, go first.

## Found while measuring, worst first

These exist today. Each is a hand-back, not part of the new screens, but
the new screens sit on top of them.

### F1. Three stock screens are 78 columns wide on a 40 column screen

`chatin`, `newuser` and `rules` have no `.seq`, and their `.asc` is 78
wide. SCREENS.md: "Commodore and plain ASCII screens: at most 39
characters per line." Measured widest `.asc` line: `chatin` 78, `newuser`
78, `rules` 78. A PETSCII-40 caller gets every line wrapped. `rules` plays
at registration, `newuser` straight after it, `chatin` every time they join
the room, including when the sysop answers their ring.

What a PETSCII-40 caller saw when the sysop answered (host capture):

```
          1         2         3
0123456789012345678901234567890123456789
                                 ENTERIN
G CHAT
  --------------------------------------
--------------------------------------
  Everyone in the room sees what you typ
e. /p sends a line to
  one person, which is quieter but not p
rivate: it crosses the
  wire in the clear and the sysop has th
e log. Nothing here is kept once the buf
```

- The rule is 76 wide, so every rule is two rows. The title is split
  across two rows. Words are cut mid-letter.
- Fix: `screen-artist` draws `chatin.seq`, `newuser.seq` and `rules.seq`
  at 39 columns, and brings each `.asc` down to 39, which is SCREENS.md's
  own rule. ANSI callers keep the 78-column `.ans`. Every stock screen then
  has a `.seq`, and `tools/mkscreens.py` should refuse to write an `.asc`
  over 39, the way `release.py` refuses a GPL-2.0 line.

### F2. A room line loses up to 26 characters, and three people see three lengths

Measured: an 80-column caller types a 64-character line; a PETSCII-40
reader receives this and nothing more.

```
b'\x9f#2\x97:\x99\xd2OVER2\x97)\x05 \x05HELLO FROM THE EIGHTY COLUMN\r
  \x05SIDE OF THE ROOM, HOW IS\r\x05'

          1         2         3
0123456789012345678901234567890123456789
#2:Rover2) hello from the eighty column
side of the room, how is
```

The line was `...how is the weather`. Three limits disagree:

- `chat.cpp:674` lets the caller type 64 (`kLineMax`, or `cols - 2`).
- `chat.cpp:2833` shows the sender `%.60s` of it.
- `chat.cpp:578` stores the line in the ring at 64 characters including the
  tag, so everybody else gets 64 minus the tag: 53 for `#2:Rover2) `,
  down to 38 for a 20-character handle on node 10.

Rule: the ring entry holds the tag plus the full typed text. Size the ring
line at `kLineMax + kTagMax` (tag `#10:` 4 + handle 20 + mark 1 + space 1
= 26), or cap typing at `kLineMax - kTagMax`. The first costs 26 bytes per
ring line (48 lines as shipped: 1,248 bytes of heap, the ring is
`calloc`'d at start); the second costs nothing and gives 38 characters,
which is what a 40-column caller can type anyway. Rob's call; the second
is the honest one on a WROOM.

### F3. Your own words carry the other person's name

In a sticky private conversation (every answered ring is one) your own line
is drawn as `P>` plus the partner's tag (`chat.cpp:2548-2557`). Measured,
the caller's screen:

```
P#S:OpSys] hi, what's up?
P>#S:OpSys] my upload keeps failing
[>S]
```

The second line is the caller's own. It reads as the sysop saying it. The
`>` is one character, dark, and the only difference. The receipt it
replaced ("/p to #2 sent.") was worse; this is still wrong. Spec in item 2.

### F4. LAST prints node 10 as 0

`bbs_shell.cpp:2532`: `'0' + (r.node % 10)`. Node 10's calls are listed as
node 0. DASH had the same bug and it was fixed with `nodeLabel`; LAST was
not. Use `nodeLabel` (two columns) and widen the N column by one at both
widths: at 40 the row is 32 today, so it fits.

### F5. CALLS by hour is 28 rows

Title, 24 bars, rule, two footer lines: 28 rows against 24. It stops at
`[More]` on every terminal. At 80 it fits one screen as two columns of 12
hours (drawn under item 4); at 40 it stays one column and pages. Item 4 adds
a footer line naming the new lists, so fix this in the same change.

## Rules shared by all six

- Widths come from `rowWidth(s)`: 39 at 40, 79 at 80, 131 at 132. The
  "wide" layout is `rowWidth >= 59`, the test LAST and the forms already
  use (`cols >= 60`, `Form::wide` at 80).
- Handles in a column: 20 wide at 80, 11 at 40, `listHandle`, with the
  rank marker in the column before (`*` `>` `]`), as LAST does. A list that
  shows markers ends with the key line `*GUEST  >CO-SYSOP  ]SYSOP`.
- Titles are `rowTitle` (Cyan bar), rules are `rowRule`, notices are
  `markedLine` (`--> ` then a hanging indent of 4), in the room
  `chat::roomSay`. No new bar colours except where item 2 says.
- A cursor menu's footer follows FILES: `Files: cursor keys and Enter, or a
  number. Q quits` at 80, `Files: cursors, Enter, number. Q quits` at 40,
  `Files: number opens an area, Q quits` on plain ASCII.
- Plain ASCII: `rowBar` already draws `Title ---- right`; forms already ask
  line by line (`Plugin enabled [yes]:` with `1 yes  2 no` above).
- Dates a sysop types: `2026-12-31`, and nothing else. It is the one form
  that means the same thing in every country. Shown back as `31 Dec 2026`,
  or `31 Dec` where there is no room, as WHOIS does.

### New helpers, and where existing ones suffice

| Helper | Where | For | Why |
|---|---|---|---|
| `Term::blockRun(tl, n)` | term.cpp | every bar chart | PETSCII draws a block as `RVS ON, space, RVS OFF`, 3 bytes a cell (term.cpp:513-517). A 16-cell bar is 48 bytes; as one run it is 18. Ten bars at 40: 480 bytes against 180. ANSI and ASCII emit n glyphs as now. CALLS by hour should use it too (bbs_shell.cpp:2978) |
| `Bbs::rowMeter(s, value, peak, cells, colour, col)` | bbs_shell.cpp | CALLS TOP lists, vote results, CALLS by hour | `cells * value / peak`, at least 1 when value > 0, padded to `cells` with spaces, through `blockRun`. The CALLS-by-hour arithmetic, once |
| a shared cursor menu | core, lifted out of files.cpp's area menu | VOTE, SHOP, then FILES | Reverse bar on the row, cursor keys, Enter, digits, the footer grammar above, the number-only fallback. FILES has the only copy today (`visibleAreas`). The third copy is where the highlight and the opened row drift apart, which is the bug `visibleAreas` exists to prevent |
| `bbsu::parseDay("2026-12-31")` | bbs_util.h | Until, Closes | Day number or refusal. CONFIG and the poll form both validate with it |
| `censor::check(text, chat)` | new core module | oneliners, forums, chat | Returns the first banned word or null |
| `tally::` | new core module | top lists, credits | A second fixed-record file beside `callstats.dat` (item 4) |
| `BusKind::Rotate` | bus.h, appended | rotation at the prompt | The `Ring` kind's shape: the text is a page number and a sequence, dropped when stale. Plus `Mailbox::replace(kind, msg)` so a caller never queues two |

Existing helpers that suffice: `rowTitle`, `rowBar`, `rowRule`,
`rowText`, `rowSeg`/`rowEnd`, `markedLine`, `chat::roomSay`,
`startPluginList` with `listHold` (every list here is paged by the core),
`Form` with `ask()` (every question before a save), CONFIG's composite
sub-pages (`CfgPart`, `CK_CYCLE`, `CK_TEXT`), `codes::wrap` with its
`Painter` (page text keeps its colours), `showScreen`.

### RAM

| Feature | Session | Static DRAM | Heap | On flash |
|---|---|---|---|---|
| Rotating pages | 0 | about 60: 10 pages grow 5 bytes each (rotate and where in one byte, until as a day number), plus the rotation clock and sequence | 0 | three more parts on the existing `pageN` lines |
| Sysop chat | 0 | 0 (`g_answered` exists) | 0 | none |
| Oneliners | 0 | about 4 | 0 | `oneliners.txt`, 10 records of 88 bytes |
| Word list | 0 | about 8 | up to 720, at config load, freed at reload | 6 lines in `system.cfg` |
| Top tens | 0 | about 300: one 10-row result table (id, value, handle), seized by whoever lists, like the forums' subject table; plus 24 for chat's per-slot line counter | 0 | `tally.dat`, 16 bytes an account, 4 KB at 250 |
| Voting | 0 | about 4 | 0 | `polls.dat`, 8 polls of about 320 bytes |
| Credits | 0 | about 40: an 8-item shop registry | 0 | in `tally.dat` |
| Camera extra snap | 0 | 1 byte per camera limit window, camera boards only | 0 | none |

About 430 bytes of static DRAM in all. Against the ESP32-CAM's 3,960 free
(1.1.2-dev.1) that is 11%. The 300-byte result table is the one to watch:
if the camera boards cannot spare it, the runner job can write its ten rows
into the requesting caller's `s.compose`, which is free while they read a
list, handed over by `Session::call` exactly as other runner jobs identify
a caller. That makes it zero, at the cost of the job checking the caller is
still the one who asked.

No feature here puts a field in a `Session`. Every cursor position uses
`s.listIdx` and `s.ownerData`, which a list or a plugin-owned session
already has.

---

## 1. Rotating information pages

Rob's decisions, as settled: per page Rotate yes/no, Where any of Logon,
Main and Chat, Until a date; shown whole, title and message, 8 lines at
most at the reader's width; one board-wide interval of 5 minutes; nothing
posts where nobody is.

### CONFIG info: the page's sub-page

Measured today, 80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
INFO PAGE 1
───────────────────────────────────────────────────────────────────────────────

 Page title           Meetups
 Who may read it      all.....................................................

                      [ Save ]  [ Cancel ]

 Tab or arrows move, Enter is next, Space steps a choice, F1 saves, ESC quits
```

Proposed, 80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
INFO PAGE 1
───────────────────────────────────────────────────────────────────────────────

 Page title           Meetups
 Who may read it      all.....................................................
 Rotate this page     yes.....................................................
 Show it where        main+chat...............................................
 Rotate until         2026-12-31

                      [ Save ]  [ Cancel ]

 Where it rotates: logon, main, chat, or two joined with +, or all
```

Proposed, 40:

```
          1         2         3
0123456789012345678901234567890123456789
INFO PAGE 1
---------------------------------------

 Title     Meetups
 Read      all........................
 Rotate    yes........................
 Where     main+chat..................
 Until     2026-12-31

           [ Save ]  [ Cancel ]

 logon, main, chat, two with +, or all
```

Plain ASCII, line by line:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
INFO PAGE 1
-------------------------------------------------------------------------------

Page title [Meetups]:
1 all  2 users  3 staff  4 co2  5 co1  6 sysop
Who may read it [all]:
1 yes  2 no
Rotate this page [no]: 1
1 logon  2 main  3 chat  4 logon+main  5 logon+chat  6 main+chat  7 all
Show it where [main]: 6
Rotate until [] (a date like 2026-12-31, Enter for none):
Save (Y/n)?
```

- Three parts appended to `kInfoParts` (bbs_sysop.cpp:1176), after Read,
  so every `pageN = Title | level` already written parses unchanged and
  means "does not rotate":
  - `{ "Rotate", CK_CYCLE, 3, 0, "no|yes", "Rotate this page" }`
  - `{ "Where", CK_CYCLE, 10, 0, "main|chat|logon|logon+main|logon+chat|main+chat|all", "Show it where" }`
  - `{ "Until", CK_TEXT, 10, 0, nullptr, "Rotate until" }`
  The line becomes `page1 = Meetups | all | yes | main+chat | 2026-12-31`,
  60 characters at the longest title, inside `kSettingMax` 120.
- Where is one cycle, not three yes/no rows. Rob said "any of Logon, Main,
  Chat", which is one question; one row keeps the sub-page at 5 fields; and
  the packed value reads as words in `system.cfg`. Seven choices is the
  whole set, so nothing needs typing. `main` comes first in the cycle
  because it is the common case, and a new rotating page defaults to it.
- Rotate stays a switch beside Where, so a sysop can pause a page without
  losing where it went.
- Status notes, on the status line while the field has focus:
  - Rotate, 80: `Shown whole where somebody is, every few minutes. 8 lines at most.`
    40: `Shown whole, 8 lines at most.`
  - Where, as drawn above.
  - Until, 80: `A date like 2026-12-31. Blank rotates until you turn it off.`
    40: `Like 2026-12-31. Blank: no end.`
- Until is checked with `bbsu::parseDay` on Save. A bad one fails the
  field: `Until: a date like 2026-12-31` (30 characters, fits both).
  A date already past saves, and the status says
  `Ended 31 Dec 2025: it won't rotate.` rather than refusing: the sysop may
  be switching it off on purpose.

### CONFIG info: the plugin's page

The page gains one row, and the page buttons say which pages rotate. 80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 Info page 0          [ House rules                                          ]
 Info page 1          [ Meetups                            rotates main+chat ]
 Info page 2          [ Staff notes                                          ]
 Info page 3          [ not set                                              ]
 Rotate every (min)   5.......................................................
```

40:

```
          1         2         3
0123456789012345678901234567890123456789
 Page 0    [ House rules             ]
 Page 1    [ Meetups, rotates        ]
 Page 2    [ Staff notes             ]
 Page 3    [ not set                 ]
 Interval  5..........................
```

- `rotate_minutes`, a `PS_NUM` 1..60, default 5, appended to info's
  `kSettings` (info.cpp:230). Four core rows, ten pages and this one is 15
  fields, one under `Form::kMaxFields`. Label `Interval` at 40 (not
  `Rotate`, which the sub-page already uses for yes/no), `Rotate every
  (min)` at 80.
- The button summary: at 80 the rotating state is right-aligned inside the
  56-column box (`rotates main+chat`, and `ended` once Until has passed);
  at 40 the box holds 25, so the title is cut to 16 and `, rotates`
  follows it (`Meetups, rotates`). This needs
  `cfgSummary` (bbs_sysop.cpp:1542) to take a per-composite summary
  function, appended to `CfgComposite` so the other three tables are
  untouched.

### When a rotating page is too long

The board counts the page's rows with `codes::wrap` at the narrowest
reader: `rowWidth - 4` at 40 columns, which is 35. It asks on Save only
when the page rotates, only when that count passes 8, and only when
Rotate, Where or the text changed. The question, through `Form::ask` on
the Rotate field, says what goes wrong (Rob: not "Save anyway?"):

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
                      [ Save ]  [ Cancel ]

 At 40 columns page 0 is 13 lines: 7 show, then INFO 0. Rotate it? (y/N)
```

```
          1         2         3
0123456789012345678901234567890123456789
           [ Save ]  [ Cancel ]

 13 lines at 40: 7 show. Rotate? (y/N)
```

- 72 and 37 characters, inside the 78 and 38 of the status line.
- `warnAbove` (plugin.h:208) cannot do this: it compares a number the
  sysop typed. This is a check on a file the sub-page does not show, so it
  is a per-composite check, called from the sub-page's save, that may call
  `ask()`. Append it to `CfgComposite` beside the summary function.
- A rotating page with no text yet saves without a question and says
  `Page 1 has no text yet. INFO 1 EDIT writes it.` on the status line.
- The editor says it too. `INFO 1 EDIT` on a rotating page adds the limit
  to its header, and `/s` on a page over it says so after `Page 1 saved.`:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 Page 1: Meetups

Up to 32 lines. /s saves, /a leaves it. It rotates: 8 lines show, 35 wide.
───────────────────────────────────────────────────────────────────────────────
 1: _
```

```
          1         2         3
0123456789012345678901234567890123456789
 Page 1: Meetups

Up to 32 lines. /s saves, /a leaves it.
It rotates: 8 lines of 35 show.
---------------------------------------
 1: _
```

  After `/s`, over the limit: `--> Saved. It rotates, and at 40 columns
  it is 13 lines: 7 show.` through `markedLine`.

### How a longer page shows

At every place, a reader whose width makes the page more than 8 rows gets
7 rows and `(more: INFO 0)` as the eighth, `(more: /i0)` in the room. The
block is never taller than 1 + 8 rows. 40:

```
          1         2         3
0123456789012345678901234567890123456789
--> House rules  (INFO 0)
    Board rules, the short version.
    No hate: that is the one that gets
    you removed.
    Nothing here is encrypted, so never
    reuse a password.
    Chat and mail are not private; the
    sysop reads the logs.
    (more: INFO 0)

[2] Main: _
```

The same page at 80 is 7 rows and shows whole. That is what "counted at
the reader's width" means in practice, and why the CONFIG question counts
at 40.

### At the main prompt

80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
[1] Main: time
Sat 26 Sep 2026 21:13 UTC
On for 12 minutes, 48 left.

--> Meetups  (INFO 1)
    We meet every Tuesday at 10a in the room. Bring a friend
    and a question. Newcomers welcome.

    Next topic: getting YMODEM working on a VT220 box.

[1] Main: _
```

40:

```
          1         2         3
0123456789012345678901234567890123456789
[1] Main: time
Sat 26 Sep 2026 21:13 UTC
On for 12 minutes, 48 left.

--> Meetups  (INFO 1)
    We meet every Tuesday at 10a in the
    room. Bring a friend
    and a question. Newcomers welcome.

    Next topic: getting YMODEM working
    on a VT220 box.

[1] Main: _
```

- It lands the way a page does: the idle prompt row is lifted and reused,
  the block prints, one blank row, the prompt comes back. 4 rows of text at
  80, 6 at 40 for this page.
- Row 0 of the block: `-->` Cyan, title White, `(INFO 1)` DarkGrey. The
  door in brackets is how the caller finds it again, and each place names
  its own door.
- Body: `codes::wrap` at `rowWidth - 4`, indented 4, Grey, the page's own
  colour codes kept (`Newcomers welcome.` is Yellow in this page). The
  writer's line breaks stand; blank lines count toward the 8.
- Never across a half-typed line (Rob): delivered only at the main prompt
  with the line editor empty, not in a list, a screen, a form or `[More]`.
  A caller who is typing gets it after their Enter, at the next empty
  prompt. A newer rotation replaces one still waiting; a caller never has
  two.
- Carried as `BusKind::Rotate` (text: page number and rotation sequence),
  delivered by `deliverShell`, dropped if its sequence is not the current
  one. Zero bytes per session. Posted only to callers at the main prompt who
  may read that page, which is "nothing posts where nobody is".

### In the room

80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
#2:Petra) anybody tried the new file area?
--> Meetups  (/i1)
    We meet every Tuesday at 10a in the room. Bring a friend
    and a question. Newcomers welcome.

    Next topic: getting YMODEM working on a VT220 box.
#4:amiga_al) yes, works from the A500
_
```

40:

```
          1         2         3
0123456789012345678901234567890123456789
#2:Petra) anybody tried the new file
area?
--> Meetups  (/i1)
    We meet every Tuesday at 10a in the
    room. Bring a friend
    and a question. Newcomers welcome.

    Next topic: getting YMODEM working
    on a VT220 box.
#4:amiga_al) yes, works from the A500
_
```

- The room's own voice, `chat::roomSay`, so the marker is the room's
  colour. No blank row after it: the room never has them between lines.
- It goes into the ring as one entry, a marker line for page n, so it
  waits behind a half-typed line like any room line, counts in "n room
  lines went by" for a caller in private mode, and is replayed by `/sh`.
- Each reader's `flush` expands it at their width from the page file into
  their `s.compose`, which is free in the room unless they are writing
  mail there. If it is not free, they get the title row alone. One file
  open per reader per rotation, at most ten every five minutes, and at most
  one a pass.
- History replay to somebody joining, and `/sh`, show the title row only.
  A joiner does not need eight rows of an old notice.
- Posted only when at least one caller in the room may read the page.

### At logon

After the motd (or `newuser`), before the caller lands. The page as
`INFO n` draws it, so it reads as reading, not as an interruption. 80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
Welcome back, Petra!
Connected to node 2 of 10 on Sat 26 Sep at 21:08.
Time brought to you by NTP.

You're the 12th caller today and have 60 minutes.
This is call 13 for you; the last was 09/26 20:40.

3 information pages. INFO reads them.
 Meetups                                                                INFO 1
We meet every Tuesday at 10a in the room. Bring a friend
and a question. Newcomers welcome.

Next topic: getting YMODEM working on a VT220 box.
───────────────────────────────────────────────────────────────────────────────
[H]ELP for commands.

[2] Main: _
```

40:

```
          1         2         3
0123456789012345678901234567890123456789
Welcome back, Petra!
Node 2 of 10, Sat 26 Sep at 21:08.

You're the 12th caller today and have
60 minutes.
This is call 13 for you; the last was
09/26 20:40.

3 information pages. INFO reads them.
 Meetups                        INFO 1
We meet every Tuesday at 10a in the
room. Bring a friend
and a question. Newcomers welcome.

Next topic: getting YMODEM working on a
VT220 box.
---------------------------------------
[H]ELP for commands.

[2] Main: _
```

- Row with ` Meetups ... INFO 1` is the Cyan reverse bar (`rowTitle`).
- 20 rows at 40 with no motd, inside a C64's 25. Bar, at most 8 rows of
  body, rule: 10 rows at most for the page.
- Body at the full row width, not indented: it is a page being read.
  The 8-row limit counts at `rowWidth - 4` everywhere, so a page that
  passed CONFIG's check never needs cutting here.
- The hook: the core calls info from `arrive` (bbs.cpp:2147) once the
  first screen is done, the way it calls `chat::inRoom`. An appended
  `onArrive` hook is cleaner; either works.
- One page per login, the next Logon page in turn, is my recommendation.
  See the decision at the end.

---

## 2. Sysop chat

Built in 1.1.0 and 1.1.1: answering a ring puts both in the room in
private mode, aimed at each other (`bbs_ring.cpp:580` and `:590`,
`chat::converse` at chat.cpp:3534). The mechanics are right: held room
lines, the count when it ends, `/sh`. The arrival is not.

### What they get today

The caller at 80, after the sysop presses A (host capture):

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
                                 ENTERING CHAT
  ────────────────────────────────────────────────────────────────────────────
  Everyone in the room sees what you type. /p sends a line to
  one person, which is quieter but not private: it crosses the
  wire in the clear and the sysop has the log. Nothing here is
  kept once the buffer rolls over.

      ∙ /s        who else is here
      ∙ /p n      a line to one person
      ∙ /welcome  read this again
      ∙ /help     the rest of the commands
      ∙ /q        leave, or press ESC

  ────────────────────────────────────────────────────────────────────────────

--> Main: 1 here. /s who, /q quits.
#1:Rover) (you)
--> 1 in Main
--> The sysop answered. You're in the chat room, and what you type goes to the
    sysop only. /q leaves.
--> You won't see other callers while talking directly
*** #S:OpSys] joined
[>S]
```

- 21 rows before the caller can type, and the first thing they read is
  "Everyone in the room sees what you type", which is the opposite of what
  is happening. The sysop gets the same screen.
- At 40 it is worse (F1): about 30 rows, the room's welcome wrapped, the
  replay of whatever the room said last (`kJoinShow`, 8 lines of joins and
  leaves in the capture), and the screen's title scrolled away.
- The sysop's screen has no reminder of why they are there. The ring's
  reason was on the `[A]nswer` question and is gone.

### Proposed

The screen is cleared on both sides. No `chatin`, no roll call, no
history. A Yellow bar says who you are talking to, one line says what
this is, a rule, and the ring's reason as the first line of the
conversation, in the caller's own tag, on both screens.

Caller, 80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 Talking with the sysop                                              /q leaves
Only the two of you see these lines. /p* goes back to the room.
───────────────────────────────────────────────────────────────────────────────
#1:Rover) need a hand with uploads
#S:OpSys] hi, what's up?
#1:Rover) my upload keeps failing
[>S] _
```

Sysop, 80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 Talking with Rover                                         node 1   /q leaves
Only the two of you see these lines. /p* goes back to the room.
───────────────────────────────────────────────────────────────────────────────
#1:Rover) need a hand with uploads
#S:OpSys] hi, what's up?
#1:Rover) my upload keeps failing
[>1] _
```

Caller, 40:

```
          1         2         3
0123456789012345678901234567890123456789
 Talking with the sysop      /q leaves
Only the two of you see these lines.
/p* goes back to the room.
---------------------------------------
#1:Rover) need a hand with uploads
#S:OpSys] hi, what's up?
#1:Rover) my upload keeps failing
[>S] _
```

Sysop, 40:

```
          1         2         3
0123456789012345678901234567890123456789
 Rover               node 1  /q leaves
Only the two of you see these lines.
/p* goes back to the room.
---------------------------------------
#1:Rover) need a hand with uploads
[>1] _
```

Plain ASCII, 80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
Talking with the sysop ---------------------------------------------- /q leaves
Only the two of you see these lines. /p* goes back to the room.
-------------------------------------------------------------------------------
#1:Rover) need a hand with uploads
#S:OpSys] hi, what's up?
[>S] _
```

- Row 0 is `rowBar(s, Color::Yellow, ...)`: Yellow because the
  ring's answer is already Yellow (`converse`, chat.cpp:3534), and a
  sysop conversation is the ring's continuation, not the room. The
  sysop's title is the caller's handle: `Talking with Rover` at 80, the
  handle alone at 40, where `node 10  /q leaves` (18) leaves 19 for it and
  `rowBar` cuts a 20-character handle by one.
- Header: 3 rows at 80, 4 at 40. The conversation has 20 rows at 80x24 and
  21 at 40x25.
- The reason: `ring_.reason` is copied before `ringClear()` (bbs_ring.cpp,
  just above :580) and posted as the caller's line, private to the two.
  The sysop's `Answered. What you type goes to Rover only.` line and the
  caller's `The sysop answered...` line are replaced by the bar and the
  one line. Both said "the chat room", which the caller never asked for.
- Your own lines in your own tag, no `P`, in every sticky conversation
  (F3): `#1:Rover) my upload keeps failing` on Rover's screen. The `[>S]`
  marker already says every line goes to the sysop, and the bar says the
  room cannot see it, so the `P` on every line says nothing the screen does
  not. A one-off `/p 3 text` in the open room keeps `P` and `P>`: there the
  distinction is real.
- The partner leaving, 40 (the `kGone` state that exists, in words):

```
          1         2         3
0123456789012345678901234567890123456789
#S:OpSys] try YMODEM, not XMODEM
--> OpSys left. /q leaves, /p* goes
    back to the room.
[>S] _
```

- `/p*` from here: the room's existing words, `Back to the room. n room
  lines went by: /sh n shows them.`, and they are now in the room without
  having seen `chatin`. `/welcome` exists for that; no change.
- The rest of the room still sees `*** #1:Rover) joined` and
  `*** #S:OpSys] joined`. No change: they are in the room.
- Builder: `converse` gains a flag meaning "arrived by a ring", which
  `join` reads to skip `showScreen(s, "chatin")` (chat.cpp:1932), the roll
  call and the history replay, and to clear and draw the header instead.
  `g_answered` already marks the pair.

---

## 3. Oneliners, and the word list

### ONELINERS

A wall of short lines, newest first, ten kept. `ONELINERS` shows them and
offers one input line. Callers with an account post; guests read.

80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 Oneliners                                                        newest first
 Rover                Tuesday meetup was great, bring snacks next time
]quantumrob           Board is back after the card swap. Thanks for waiting.
 amiga_al             hello from an A500 on a WiFi modem
>steve                new C64 demos in the Downloads area
 vt220bob             a real VT220 still works fine here
───────────────────────────────────────────────────────────────────────────────
*GUEST  >CO-SYSOP  ]SYSOP
Yours (Enter skips): _
```

40:

```
          1         2         3
0123456789012345678901234567890123456789
 Oneliners                newest first
 Rover       Tuesday meetup was great,
  bring snacks next time
]quantumrob  Board is back after the
  card swap. Thanks for waiting.
 amiga_al    hello from an A500 on a
  WiFi modem
>steve       new C64 demos in the
  Downloads area
 vt220bob    a real VT220 still works
  fine here
---------------------------------------
*GUEST  >CO-SYSOP  ]SYSOP
Yours (Enter skips):
_
```

- Wide: marker col 0, handle cols 1-20, text from col 22, at most 56
  characters: ends by col 77. Narrow: marker col 0, handle cols 1-11, text
  from col 13 for 26 columns, continuation rows at col 2 for 37. 56
  characters take 2 rows at 40 in the usual case, 3 at worst.
- Text: 56 characters at 80, and `cols - 2` (38) at 40, with the input on
  its own row. That is the room's rule for a line (chat.cpp:674), for the
  same reason: a line editor that runs past the right edge of a C64 is one
  backspace cannot walk back through.
- Colour: handle LightBlue as in the room, text Grey, marker as
  `markColor`. `@`-codes are not honoured here: a wall of ten lines is
  where one blinking entry ruins the rest. Printed as typed.
- Ten kept, a fixed-record file `<userdata>/oneliners.txt` (4 id, 4 when,
  21 handle, 57 text, 2 spare: 88 bytes), written in place over the oldest.
  Ten entries take about 20 rows at 40, 25 with the bar, rule, key and
  input, so `ONELINERS` goes through `startPluginList` and pages at
  `[More]`; at 80 it is 14 rows and never pages.
- At logon: newest 5, no input, then `ONELINERS adds yours.` (22), after
  the rotating Logon page. A setting, `oneliners_at_logon`, on by default.
  7 rows at 80, about 12 at 40.
- On `? chat`, rank below MAIL. No shortcut: the free letters are few and
  a digit would be read as a node number.

### A post refused

The rule for every refusal: say it was not posted, name the word, keep
what was typed.

Oneliner, 80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
Yours (Enter skips): what the heck is this board
--> Not posted: "heck" isn't allowed here. Change it and press Enter.
Yours (Enter skips): what the heck is this board_
```

Oneliner, 40:

```
          1         2         3
0123456789012345678901234567890123456789
Yours (Enter skips):
what the heck is this board
--> Not posted: "heck" isn't allowed
    here. Change it and press Enter.
what the heck is this board_
```

Forum post at `/s`, 80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 4: and honestly this is a heck of a board
 5: /s
--> Not posted: line 4 has "heck", which this board doesn't allow.
    Backspace here brings line 4 back to fix; /a abandons.
 5: _
```

Forum post at `/s`, 40:

```
          1         2         3
0123456789012345678901234567890123456789
 4: and honestly this is a heck of a
    board
 5: /s
--> Not posted: line 4 has "heck",
    which this board doesn't allow.
    Backspace here brings line 4 back;
    /a abandons.
 5: _
```

Chat line, when chat is on the list, 40:

```
          1         2         3
0123456789012345678901234567890123456789
#2:Petra) evening all
--> Not sent: "heck" isn't allowed in
    the room.
what the heck_
```

- The forum message names the line because a C64 cannot scroll back to
  find it, and backspace on an empty line already pops the previous one
  (0.21.1), so the fix is keys the caller knows. A subject line refused at
  the subject prompt gets the oneliner's words and the prompt again.
- The room line is refused to the sender only; the room sees nothing. `/p`
  lines are chat and are checked too when the option is on.
- Mail is not checked. It is private; Rob's rule was "everything public".
- The word is named because the list is not a secret (anybody can find it
  by trying) and a caller who cannot see what is wrong retypes the same
  thing.

### CONFIG censor

A core page, `CONFIG censor`, because it applies across three plugins and
none of them owns it. 80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
CENSOR
───────────────────────────────────────────────────────────────────────────────

 Check chat too       no......................................................
 Banned words 1       heck, darn, frak*
 Banned words 2
 Banned words 3
 Banned words 4
 Banned words 5
 Banned words 6

                      [ Save ]  [ Cancel ]

 Commas between words. Case does not matter. frak* catches frakking too.
```

40:

```
          1         2         3
0123456789012345678901234567890123456789
CENSOR
---------------------------------------

 In chat   no.........................
 Words 1   heck, darn, frak*
 Words 2
 Words 3
 Words 4
 Words 5
 Words 6

           [ Save ]  [ Cancel ]

 a, b, c   frak* = frak and frakking
```

- `system.cfg` keys `censor_chat = no` and `censor1` .. `censor6`, 120
  characters each, so a backup carries them and a sysop can edit them on a
  laptop.
- Matching: case-insensitive, whole words (a word boundary is anything
  that is not a letter or a digit), a trailing `*` matches any ending.
  Checked against `codes::plain` of the text, so `@RED@heck@N@` is caught.
  Spelling tricks (`h3ck`) are not caught and the note does not pretend
  otherwise.
- Loaded into one heap buffer at config load, at most 720 bytes, freed and
  reloaded on a CONFIG save; nothing when the list is empty. Never on the
  loop.
- Applies to everyone, staff included. Simpler to explain, and a sysop
  who wants a word should take it off the list.

---

## 4. CALLS TOP, UPLOADERS, CHATTERS, POSTERS

Four top-ten screens under CALLS, drawn as bar charts like CALLS by hour.
`CALLS` alone stays the hour chart. POSTERS is the obvious fourth ("and
more"): forum posts, which the forums already count.

80 (bar 47 cells):

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 Top callers                                                   calls, all time
 # Handle                 Calls
 1]quantumrob               412 ███████████████████████████████████████████████
 2 amiga_al                 208 ███████████████████████
 3>steve                    151 █████████████████
 4 Rover                     97 ███████████
 5 vt220bob                  64 ███████
 6 Petra                     40 ████
 7 tandy1000                 22 ██
 8 pet2001                   17 █
 9 kaypro                     9 █
10 coco3                      3 █
───────────────────────────────────────────────────────────────────────────────
*GUEST  >CO-SYSOP  ]SYSOP
CALLS TOP  UPLOADERS  CHATTERS  POSTERS    CALLS alone: calls by hour
```

40 (bar 16 cells):

```
          1         2         3
0123456789012345678901234567890123456789
 Top callers                  all time
 # Handle        Calls
 1]quantumrob      412 ████████████████
 2 amiga_al        208 ████████
 3>steve           151 █████
 4 Rover            97 ███
 5 vt220bob         64 ██
 6 Petra            40 █
 7 tandy1000        22 █
 8 pet2001          17 █
 9 kaypro            9 █
10 coco3             3 █
---------------------------------------
*GUEST  >CO-SYSOP  ]SYSOP
CALLS TOP|UPLOADERS|CHATTERS|POSTERS
```

Plain ASCII, 80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
Top uploaders -------------------------------------------------- files approved
 # Handle                 Files
 1]quantumrob               412 ###############################################
 2 amiga_al                 208 #######################
 3>steve                    151 #################
-------------------------------------------------------------------------------
*GUEST  >CO-SYSOP  ]SYSOP
CALLS TOP  UPLOADERS  CHATTERS  POSTERS    CALLS alone: calls by hour
```

An empty list, 40:

```
          1         2         3
0123456789012345678901234567890123456789
 Top chatters               room lines
Nobody has talked in the room yet.
---------------------------------------
CALLS TOP|UPLOADERS|CHATTERS|POSTERS
```

- The row, in columns: rank `%2u` cols 0-1, marker col 2, handle `H` wide
  from col 3 (H = 20 wide, 11 narrow), a space, the figure `%7s` with
  commas, a space, then the bar. Bar cells = `rowWidth - 12 - H`: 47 at
  80, 16 at 40, 99 at 132. Every row is exactly `rowWidth`, so nothing
  wraps at any width.
- Bar: `rowMeter`, scaled to the first row, at least one cell for any
  figure above zero. The top row Yellow, the rest LightGreen, the figure
  LightGrey: the same colours as CALLS by hour, so a bar means one thing
  on this board.
- 15 rows, inside 24. No `[More]`.
- Titles and right texts, wide / narrow:
  - `Top callers`: `calls, all time` / `all time`
  - `Top uploaders`: `files approved` / `files`
  - `Top chatters`: `room lines` / `room lines`
  - `Top posters`: `forum posts` / `posts`
- Empty: `Nobody has called yet.`, `Nobody has uploaded yet.`, `Nobody has
  talked in the room yet.`, `Nobody has posted yet.` One row, no header,
  no key.
- Who is counted: accounts only, retired accounts left out. Guests never
  appear, so the `*` in the key never shows; keep the key line anyway,
  since every marked list on the board has it.

### Where the figures come from

- Calls: `callstats.dat` already has them (users.cpp:397, `StatRec`,
  16 bytes). TOP can ship first on that alone.
- Uploads, room lines, forum posts: nothing counts them. A second
  fixed-record file, `<userdata>/tally.dat`, the same shape and the same
  in-place writes as `callstats.dat`, 16 bytes an account at its id:

  | Offset | Size | Field |
  |---|---|---|
  | 0 | 4 | id (0 is a hole) |
  | 4 | 2 | credits (item 6) |
  | 6 | 2 | uploads approved |
  | 8 | 4 | room lines |
  | 12 | 2 | forum posts |
  | 14 | 2 | spare |

  Assert on offsets and the total, never on a sum of widths (the MailRec
  lesson).
- When each is written: an upload when staff approve it (the uploader is in
  `UPLOADS.BBS`), a post when it is saved, room lines counted in chat's
  per-slot array (12 x 2 bytes) and written once when the caller leaves the
  room or hangs up. One flash write per call for chat, not per line.
- The ten rows are found on the runner (one read of a 4 KB file, one pass
  of users.txt for the ten handles), with `listHold` while it works, the
  spinner if it takes long enough to see.
- `callstats.dat`'s partner rules hold: a restore that brings one without
  the other refuses it alone, as 1.1.2 does for `callstats.dat` and
  `users.txt`.

### CALLS by hour at 80 (F5)

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 Calls by hour                                                       147 calls
00                                   0  12 ██████████                        8
01                                   0  13 █████████                         7
02                                   0  14 ██████                            5
03                                   0  15 █████                             4
04                                   0  16 ███████                           6
05                                   0  17 ███████████                       9
06 █                                 1  18 ██████████████████               14
07 ██                                2  19 ██████████████████████████       20
08 ███                               3  20 ██████████████████████████████   23
09 ██████                            5  21 ███████████████████████          18
10 █████                             4  22 ███████████                       9
11 ███████                           6  23 ███                               3
───────────────────────────────────────────────────────────────────────────────
Busiest 20:00 with 23. Last 50 calls kept.
CALLS TOP  UPLOADERS  CHATTERS  POSTERS
```

- Two columns at `rowWidth >= 59`: each half is the hour (3), a bar of
  `(rowWidth - 18) / 2` cells (30 at 80, 56 at 132) and ` %4u` (5), with
  two spaces between the halves: 78 columns at 80. 16 rows, no `[More]`.
  At 40, one column as now, and it pages.
- The two footer lines join into one at 80 as drawn. At 40:
  `Busiest 20:00 with 23.` then the menu line.

---

## 5. The voting booth

`VOTE` (`V`, which is free: the taken letters are C F H ? I M O T W).
On `? chat`.

### The booth

80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 Voting booth                                                           2 open
 1  * Should the board get a second phone line?                     12 votes
 2    Best 8-bit sound chip                                          4 votes
 3  - Next meetup topic                                              9 votes
───────────────────────────────────────────────────────────────────────────────
* not voted yet   - closed
Vote: cursor keys and Enter, or a number. N new poll. Q quits
```

40:

```
          1         2         3
0123456789012345678901234567890123456789
 Voting booth                   2 open
 1 * Should the board get a second
 2   Best 8-bit sound chip
 3 - Next meetup topic
---------------------------------------
* not voted yet   - closed
Vote: cursors, Enter, number. Q quits
```

Plain ASCII:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
Voting booth ----------------------------------------------------------- 2 open
 1  * Should the board get a second phone line?                     12 votes
 2    Best 8-bit sound chip                                          4 votes
 3  - Next meetup topic                                              9 votes
-------------------------------------------------------------------------------
* not voted yet   - closed
Vote: a number, or Q: _
```

- Wide row: number `%2u` cols 0-1, two spaces, state col 4, space,
  question cols 6-65 (60), space, count `%3u votes` ending col 75.
  Narrow: number cols 0-1, space, state col 3, space, question from col 5,
  cut at the last word that fits in 34 (never mid-word; the poll's own
  screen shows it whole). No count at 40.
- State: `*` open and you have not voted (MAIL's `*` for "new"), blank
  open and voted, `-` closed. The key row under the rule says so.
- The selected row is the menu's reverse bar across the full row width.
- Newest poll first. Eight polls at most, so the booth is at most 12 rows.
- `N new poll` is in the footer only for staff with the plugin's write
  level; at 40 it is left out and `VOTE NEW` does the same.

### A poll

80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 Poll 1                                                     closes 30 Sep 2026
Should the board get a second phone line?

 1  Yes, a second line
 2  No, one is plenty
 3  Only on weekends
───────────────────────────────────────────────────────────────────────────────
Pick: cursor keys and Enter, or a number. Q goes back
```

40:

```
          1         2         3
0123456789012345678901234567890123456789
 Poll 1                  closes 30 Sep
Should the board get a second phone
line?

 1  Yes, a second line
 2  No, one is plenty
 3  Only on weekends
---------------------------------------
Pick: cursors, Enter, number. Q back
```

- The screen is cleared on the way in; the booth, a poll and its results
  are three places, not one scroll.
- The question wraps at `rowWidth`, White. Answers Grey, the selected one
  the reverse bar. No `(y/N)` after picking: Enter on an answer is the
  vote, and `--> Your vote is in: Yes, a second line.` goes above the
  results. A vote is final; changing it is not offered. Say so in the
  footer only if Rob wants changes allowed later.
- Guests, and anybody below the poll's level, get the results (if the poll
  shows them) and `--> Guests can't vote.` or `--> This poll is for
  staff.` instead of the pick line.
- Plain ASCII: `Pick 1 to 3, or Q: `.

### Results

80 (bar 38 cells):

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 Poll 1 results                                                       12 votes
Should the board get a second phone line?

 Yes, a second line             ██████████████████████████████████████   7  58%
 No, one is plenty              █████████████████████                    4  33%
 Only on weekends               █████                                    1   8%
───────────────────────────────────────────────────────────────────────────────
You voted: Yes, a second line.
Enter goes back to the booth.
```

40 (bar 30 cells):

```
          1         2         3
0123456789012345678901234567890123456789
 Poll 1 results               12 votes
Should the board get a second phone
line?

Yes, a second line
██████████████████████████████   7  58%
No, one is plenty
█████████████████                4  33%
Only on weekends
████                             1   8%
---------------------------------------
You voted: Yes, a second line.
Enter goes back to the booth.
```

- Wide row: space, answer cols 1-30, space, bar `rowWidth - 41` cells (38
  at 80, 90 at 132), space, count `%3u`, space, `%3u%%`. Narrow: the
  answer on its own row, then bar `rowWidth - 9` cells (30), count and
  percent. Six answers at 40 is 12 rows of bars plus 7: 19, inside 25.
- Leading answer Yellow, the rest LightGreen, as every chart here.
  Percentages are rounded down and are allowed not to sum to 100.
- A poll's results show "after voting" by default: see the decision at the
  end. Settings `always`, `voted`, `closed`.

### Making one (staff)

A form, `NEW POLL`, from `N` in the booth or `VOTE NEW`. 80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
NEW POLL
───────────────────────────────────────────────────────────────────────────────

 Question             Should the board get a second phone line?
 Answer 1             Yes, a second line
 Answer 2             No, one is plenty
 Answer 3             Only on weekends
 Answer 4
 Answer 5
 Answer 6
 Closes on            2026-09-30
 Who may vote         users...................................................
 Results shown        after voting............................................

                      [ Save ]  [ Cancel ]

 Two answers at least. Blank rows are left out.
```

40:

```
          1         2         3
0123456789012345678901234567890123456789
NEW POLL
---------------------------------------

 Question  Should the board get a se
 Answer 1  Yes, a second line
 Answer 2  No, one is plenty
 Answer 3  Only on weekends
 Answer 4
 Answer 5
 Answer 6
 Closes    2026-09-30
 Voters    users......................
 Results   voted......................

           [ Save ]  [ Cancel ]

 Two answers at least.
```

- 10 fields. Question 60 characters (it scrolls in the 27-column box at
  40, as long values already do), answers 30, Closes a date or blank for
  "until closed by hand", Voters a cycle `users|co2|co1|sysop`, Results a
  cycle `always|voted|closed` (the long words at 80: `after voting`,
  `when it closes`).
- The field buffers are carved out of `s.compose` (60 + 6 x 31 + 11 + 7 +
  7 = about 275 bytes), which is free while a form is open. Nothing new in
  the Session.
- Refusals by `fail()`: fewer than two answers, a Closes date that does
  not parse, eight polls already open (`Eight polls is the most. Close one
  first.`, 43 at 80; `Eight polls is the most.` at 40).
- Closing and removing: `C` and `D` on a poll's screen for staff (with a
  `(y/N)` for D), and `VOTE n CLOSE`, `VOTE n DELETE` at the prompt.
- Storage: `<userdata>/polls.dat`, 8 fixed records: the text above, six
  `uint16` counts, the close day, the levels, and a 32-byte bitmap of who
  voted by account id (ids stop at `max_users`, 250). About 320 bytes a
  poll, in place.

---

## 6. Credits, and what to call the page

### The name: SHOP

- The unit stays **credits**: it is the word callers expect for this.
- The command and the CONFIG page are **SHOP**, not CREDITS.
- Why not CREDITS: the board already has a queued use for "credits", the
  lifetime supporters' names on the stock ABOUT screen. Two meanings of one
  word is the thing this project renames at the second use, not the
  fourth. Nothing is built for the ABOUT block yet, so call that one
  **Supporters** and "credits" keeps one meaning.
- Why not STORE: this board has a partition called `storage`, MEM and SYS
  talk about storage, and FILES is where things get stored. STORE would be
  read as file storage.
- SHOP is four letters, one meaning, a verb and a noun, and on every
  keyboard that calls here. No shortcut: `S` is a word away from
  SHUTDOWN.

### Earning, at login

80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
You're the 12th caller today and have 60 minutes.
This is call 13 for you; the last was 09/25 20:40.
Your 40 unused minutes on 25 Sep made 4 credits. You have 12; SHOP spends them.
```

40:

```
          1         2         3
0123456789012345678901234567890123456789
You're the 12th caller today and have
60 minutes.
This is call 13 for you; the last was
09/25 20:40.
+4 credits for unused time. 12 now.
```

- Earned from the unused minutes of a day the caller called, settled at
  their first login of a later day. `callstats.dat` already holds the day
  (`dayKey`) and the minutes used that day (`dayMinutes`), so the figure
  is `day_minutes - dayMinutes`, turned into credits at the page's rate,
  capped at the page's daily most. Nothing is earned on a day nobody
  called, and hanging up early earns nothing extra: the day's allowance is
  counted once.
- Guests earn nothing. Staff with no time limit earn nothing and do not
  see SHOP's time item.
- Said once, on the line after "This is call n", LightGreen, only when
  credits were added. A balance capped at the page's most says `You have
  100, the most you can keep.` instead of the count.

### SHOP

80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 Shop                                                               12 credits
 1  10 more minutes on this call                                   2 credits
 2  One more snapshot today                                        3 credits
───────────────────────────────────────────────────────────────────────────────
Unused time turns into credits: 10 minutes makes 1, up to 6 a day.
Shop: cursor keys and Enter, or a number. Q quits
```

40:

```
          1         2         3
0123456789012345678901234567890123456789
 Shop                       12 credits
 1  10 more minutes        2 credits
 2  One more snapshot      3 credits
---------------------------------------
10 unused minutes make 1 credit,
up to 6 a day.
Shop: cursors, Enter, number. Q quits
```

- Wide row: `%2u` cols 0-1, two spaces, item name cols 4-63 (60), space,
  `%3u credits` ending col 75. Narrow: name 20, `%3u credits`: 36 columns.
  An item a caller cannot afford is DarkGrey and still selectable, so the
  answer can say what it costs.
- The cursor menu is the shared one. Plain ASCII: `Shop: a number, or Q: `.
- The rate lines come from the page's settings, so they are always true.

Buying, 80 and 40:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
Buy 10 more minutes for 2 credits? (y/N) y
--> Done. 70 minutes left on this call; 10 credits left.
```

```
          1         2         3
0123456789012345678901234567890123456789
Buy 10 minutes for 2 credits? (y/N) y
--> Done. 70 minutes left on this
    call; 10 credits left.
```

```
          1         2         3
0123456789012345678901234567890123456789
--> That takes 3 credits and you have
    1.
```

- The question is on the row under the menu; the answer through
  `markedLine`; the menu is drawn again underneath with the new balance in
  the bar.
- Time bought is added to this call and to today, through the same path
  as `TIME n +m`, so the time warnings re-arm the way they do for staff.
- A snapshot bought raises today's camera limit for that handle by one
  (the camera's limit table, keyed by handle). The item appears only when
  the camera plugin runs and the caller may snap; on other boards the shop
  has one item and still reads as designed.
- More items later: a registry of up to 8 items (name, price, a `can` and
  a `buy` function) that plugins add to at start. The camera adds its own.

### CONFIG shop

80:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
SHOP
───────────────────────────────────────────────────────────────────────────────

 Plugin enabled       yes
 Who may shop         users...................................................
 Write level          sysop...................................................
 Admin level          sysop...................................................
 Unused min a credit  10
 Most credits a day   6
 Most credits kept    100
 Minutes a purchase   10
 Credits for time     2
 Credits for a snap   3

                      [ Save ]  [ Cancel ]

 Unused minutes of a day you called, turned into credits at your next login
```

40:

```
          1         2         3
0123456789012345678901234567890123456789
SHOP
---------------------------------------

 Enabled   yes
 Read      users......................
 Write     sysop......................
 Admin     sysop......................
 Minutes   10
 Day max   6
 Keep max  100
 Time buys 10
 Time cost 2
 Snap cost 3

           [ Save ]  [ Cancel ]

 Unused minutes that make 1 credit
```

- A plugin, `shop`, so it gets the core rows and its own page. The core
  Read row is relabelled `Who may shop` at 80 through the plugin's wide
  label, since read is what it means here.
- Keys and ranges: `credit_minutes` 1..60 (10), `credit_day` 0..100 (6, 0
  switches earning off), `credit_max` 1..999 (100), `time_minutes` 5..60
  (10), `time_price` 1..99 (2), `snap_price` 1..99 (3). The snap row is in
  the table only on camera builds, so no board shows a row it cannot use.
- Balance in `tally.dat` (item 4), `uint16`, capped at `credit_max`.

---

## What stays as it is

- `rowTitle`, `rowRule`, the reverse bar on ANSI and PETSCII and the
  dashed form on ASCII. Every new screen uses them unchanged. They already
  truncate a long title and never wrap.
- `markedLine`'s hanging indent of 4. It is what makes a multi-row notice
  read as one thing, and the rotation block is built on it.
- The room's private mode underneath sysop chat: holding lines, counting
  them, `/sh`, `kGone`. Only the arrival and the own-line tag change.
- The `INFO n` reading layout (bar, body at full width, rule), measured at
  both widths: right as it is, and the logon page reuses it.
- The forms at 40 and 80: 9 and 20-column labels, boxes at 12 and 23,
  status lines of 38 and 78. The poll form and three CONFIG pages fit
  inside them with no new geometry.
- The cursor-menu footer grammar from FILES. It is the right sentence;
  it only needs to live in one place.
- LAST at 40 is exactly as wide as it should be (32 columns, a clean
  table). Only its node digit is wrong (F4).
- The key line `*GUEST  >CO-SYSOP  ]SYSOP` under every marked list.

## Implementation order

Cheapest and most visible first.

- F2, the room line length, and F4, LAST's node digit. A constant and a
  helper each; both are wrong on screen today.
- F1, the three `.seq` screens and a 39-column `.asc` for each
  (`screen-artist`), with `mkscreens.py` refusing a wide `.asc`. Every
  PETSCII-40 caller who registers or joins the room sees the difference.
- Sysop chat's arrival (item 2) and the own-line tag (F3). No new data, no
  new bytes; it is the screen a caller sees at the one moment the sysop
  is paying attention.
- `Term::blockRun` and `rowMeter`, then CALLS TOP on `callstats.dat` alone,
  and CALLS by hour in two columns at 80 (F5).
- Rotating information pages (item 1): the three parts, the button
  summary, the save question, `BusKind::Rotate`, the ring entry, the logon
  page.
- The word list and ONELINERS (item 3). The list first, since forums use
  it too.
- `tally.dat`, then UPLOADERS, CHATTERS and POSTERS.
- The shared cursor menu, lifted out of FILES, then the voting booth
  (item 5).
- SHOP (item 6), last: it needs `tally.dat`, the cursor menu and the
  camera's hook.

## Decisions for Rob, one per feature

- **Rotating pages:** several pages marked Logon. One per login, the next
  in turn (recommended: the login stays inside a 25-row screen), or all of
  them, paged.
- **Sysop chat:** own lines in your own tag, and no `P`, in every sticky
  conversation (recommended), not only an answered ring. It changes a
  1.1.2 bench choice.
- **Oneliners and the word list:** refuse the post and name the word
  (recommended, as drawn), or post it with the word starred out.
- **CALLS lists:** leave the sysop's own account out of the top tens
  (recommended: every maintenance call counts, and it will top CALLERS on
  every board), or list everybody.
- **Voting booth:** results before a poll closes, shown after you vote
  (recommended, the default drawn), always, or only once it closes.
- **Credits:** earned from a day's unused minutes, settled at the next
  login (recommended: it cannot be farmed by calling and hanging up), or
  per call. And the name SHOP, with the ABOUT block called Supporters.
