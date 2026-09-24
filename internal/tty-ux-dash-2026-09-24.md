# tty-ux: the sysop dashboard (DASH), 2026-09-24

Spec for the DASH rework. Rob: "can you fix the dash problem, ultimately I
need a dashboard of some sort." Read from the source at 1.1.0-dev.4, rows
generated from the exact printf formats and checked against each width.
No board was run.

## Verdict

DASH is a 36-column frame with 24 fixed rows plus one row per running
plugin, so it scrolls on every 80x24 terminal a stock board has, shows six
of twelve sessions in an order that changes between frames, and pays six
file opens and a heap walk per refresh inside the loop. Not fixable by
editing rows: rebuild it the way NODES is built (one row per session, node
order, columns from `rowWidth`), add a "waiting on you" row and two vitals
rows, and feed every figure from a kept counter. One page at 80, three at
40, one wide page at 132. About 300 bytes of static DRAM and no new file
reads.

## Measured today

Terminal geometry from `detect.cpp:128-189`: ANSI 80x24 (NAWS may change
it), plain ASCII 80x24 always, PETSCII-40 40x25, PETSCII-80 80x25. So
"40 columns" is the C64 and nothing else; plain ASCII is 80 wide with no
home and no reverse.

DASH at 80, `rowDash` (bbs_shell.cpp:1313-1463), with a card, files,
forums, announce and lights running:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 SYSOP DASHBOARD                                           Thu 24 Sep 22:07:15
unleashed BBS 1.1.0-dev.4  up 3d 02:14
NTP ok heap 78K min 61K  disk 512K
-------------------------------------------------------------------------------
 N Handle       Doing      Idle Left
 1 quantumrob   FORUMS    00:12   47
 2 (logging in) login     00:03   --
 3*guest        CHAT      01:40    8
 5>steve        MAIL      00:00   --
 7 amiga_al     FILES     04:12   22
 S]quantumrob   DASH      00:01   --
7 in use, 1 not shown, 4 free
-------------------------------------------------------------------------------
Calls 12 today  bans 0  busy free
WiFi -35 dBm  Backup closed
Last calls    *GUEST  >CO-SYSOP  ]SYSOP
 quantumrob  1 09/24 21:40   47 min
*guest       3 09/24 21:12    8 min
 amiga_al    7 09/24 20:58   22 min
>steve       5 09/24 19:30   15 min
 vt220bob    2 09/24 18:02    9 min
-------------------------------------------------------------------------------
SD: SDHC  29684 MB free of 30436
Files: 2 areas
3 forums, 41 messages
Directory: online  12 sent 0 failed
Lights: drive flicker, strip nodes
Refresh 5s, any key stops
No idle limit
```

- 29 rows. Rows 0-21 are fixed (22), then one per plugin `status()`
  (:1430-1442), then two footer rows. `sd` is `PF_ON` and always answers
  ("SD: no card"), so a stock board draws 25 rows and `DASH n` scrolls one
  row on every redraw of an 80x24 terminal. With a card it is 27 to 29
  rows. The frame-height rule at :1306 is right and the plugin rows break
  it.
- The node block is 36 columns at every width (:1351, :1411): 43 columns
  black at 80, 95 at 132.
- Six of twelve sessions (`kDashNodeRows`, bbs.h:637), busy lines first
  (`dashNode`, :1279), so node 7 sits on a different row each frame as
  callers come and go. A refresh screen that reorders itself is the thing
  the eye cannot rest on.
- No address, no terminal. `Session::ip` (bbs.h:149) is free to show and
  NODES already shows it.
- "Last calls" has no address either, while LAST shows one to a sysop at
  60 columns or more (:1487).
- Any key stops it (bbs.cpp:3051). There are no pages to turn.
- The `*GUEST >CO-SYSOP ]SYSOP` key is on the "Last calls" heading (:1384),
  nine rows below the node rows it explains.

NODES at 80, `rowNodes` (bbs_sysop.cpp:350-417):

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 Nodes                                                                 4 of 10
 N Handle               IP              Terminal  Left  Idle
 1 quantumrob           192.168.1.23    ANSI-CP43   47 00:12
 2 (logging in)         203.0.113.9     ANSI-UTF8   -- 00:03
 3*guest                10.0.0.7        PETSCII-4    8 01:40
 4 -
```

- 60 columns used at 80, 39 exactly at 40. Every session, node order, the
  address, a count in the title. This is why it reads better.
- `%-9.9s` (:354) cuts `PETSCII-40` to `PETSCII-4` and `ANSI-CP437` to
  `ANSI-CP43` (`Term::nameOf`, term.cpp:200-202). Ten wide fixes it; there
  are 19 spare columns.
- No Doing column. A sysop deciding whether to KICK wants "what are they
  doing" beside "where from".

WHO at 80, `rowWho` (bbs_shell.cpp:1115-1212), found on the way:

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 N Handle               Doing        Min  Idle
 1 quantumrob   FORUMS      13 00:12
```

- The header widens with the terminal (:1131-1135) and the rows do not
  (:1201, :1203 are fixed `%-12.12s` and `%-10.10s`). At 80 the header is
  46 wide and every row 36, so Min and Idle sit 11 columns left of their
  headings. One row builder for the three screens fixes this for nothing.

## What DASH costs the loop today, per frame

All inside the session phase of the sysop's node, which is where The Rusty
Antenna's 541 slow passes in 24 minutes were attributed. Rebuilding the
screen without fixing this moves the bug, so the data plan comes first.

| Call | Where | Cost |
|---|---|---|
| `calllog::countSince` | :1360 | fopen, 50 x 52-byte reads, fclose on the logs partition |
| `calllog::get` x 5 | :1447 | 5 x (fopen, fseek, fread, fclose) |
| `plat::heap()` | :1334 | `heap_caps_get_largest_free_block`, a heap walk under a critical section (the 0.19.2 `heapWatch` lesson) |
| `plat::wifiRssi()` | :1368 | `esp_wifi_sta_get_ap_info`, a call into the Wi-Fi task |
| `plugins::freeBytes()` | :1335 | cached 60 s (`platform_esp32.cpp:105-133`); the 85 ms LittleFS walk lands in this pass once a minute |
| `sd::status()` | :1436 | `cardInfo()` cached 3 s (sd.cpp:92-104); `f_getfree` every third frame at `DASH 1`, 15 to 25 ms typical, 160 ms worst on a stale FSINFO |
| `BanList::at` x 8, `dashNode` x 6 | :1357, :1392 | nothing |

SYS has the same shape one screen over: `rowSys` takes `plat::netInfo()`
and `plat::heap()` at the top of the function (:1630-1631), once per ROW,
29 times a listing.

**The rule for the builder, checkable:** a dashboard frame opens no file,
walks no heap and no filesystem, and makes at most one call into the Wi-Fi
task. Every figure on it comes from a counter the board already keeps or a
cache whose age is set somewhere else. `grep -n 'fopen\|plat::heap()\|sdInfo\|fsInfo\|userInfo\|countSince\|calllog::get' ` over the dash rows
returns nothing. The frame's own cost is formatting, well under 1 ms
against `BBS_SLOW_PASS_US` at 50 ms, and `DASH 1` left open for ten
minutes on a board with a card adds nothing to the slow-pass log.

**Cost per frame after this spec:** one `DashSnap` fill at row 0 (see
data plan), about 25 `snprintf`, 2 to 2.5 KB of output. No I/O.

## The spec

### Column plan, 80 columns (ANSI, PETSCII-80, plain ASCII)

Node row `%s%c%-20.20s %-10.10s %5s %4s %-15.15s %-10.10s`, 72 wide:

| Col | Width | Field | Source |
|---|---|---|---|
| 0-1 | 2 | node | `nodeLabel(o)` |
| 2 | 1 | mark | `markFor(o)`, `markColor` |
| 3-22 | 20 | handle | `o.user` or `preLoginName(o)`, `BBS_USER_MAX` is 20 |
| 24-33 | 10 | doing | `doingText(o)`, `lurking`/`hidden` for a hidden staff line, `BBS_DOING_MAX` is 10 |
| 35-39 | 5 | idle | `fmtIdle(now - o.lastInput)` |
| 41-44 | 4 | left | minutes from `secondsLeft`, `--` when unlimited or not logged in |
| 46-60 | 15 | address | `o.ip` |
| 62-71 | 10 | terminal | `o.term.name()`, ten so nothing is cut |

Free line: ` 4 -` in DarkGrey, as NODES. Twelve rows always: nodes 1 to
10, then ` S`, then ` B`. Node order, never sorted.

Calls row on the same grid, `%s%c%-20.20s %-11s %4u      %-15.15s %-10.10s`,
72 wide: node, mark, handle, when (`%m/%d %H:%M`) under Doing, minutes under
Idle, address and terminal under their own headings. `CallRec` carries
`ip`, `term` and `charset` (calllog.h:48-51).

### 80 columns, page 1

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 DASHBOARD                                     4 of 10 on  Thu 24 Sep 22:07:15
 N Handle               Doing       Idle Left Address         Terminal
 1 quantumrob           FORUMS     00:12   47 192.168.1.23    ANSI-CP437
 2 (logging in)         login      00:03   -- 203.0.113.9     ANSI-UTF8
 3*guest                CHAT       01:40    8 10.0.0.7        PETSCII-40
 4 -
 5>steve                MAIL       00:00   -- 172.16.0.44     ASCII
 6 -
 7 amiga_al             FILES      04:12   22 192.168.1.77    ANSI-CP437
 8 -
 9 -
10 -
 S]quantumrob           DASH       00:01   -- 192.168.1.50    ANSI-CP437
 B -
-------------------------------------------------------------------------------
Waiting on you: 2 ring notes  1 upload to approve  3 unread mail
Up 3d 02:14  Heap 78K low 61K  Stack 4,120  Loop 58us worst 342ms  Slow 12
WiFi -35 dBm ch 6 awake  Data 512K  Card 29 GB  Dir listed  Backup closed
-- last calls, 12 today -------------------------------------------------------
 1 quantumrob           09/24 21:40   47      192.168.1.23    ANSI-CP437
 3*guest                09/24 21:12    8      10.0.0.7        PETSCII-40
 7 amiga_al             09/24 20:58   22      192.168.1.77    ANSI-CP437
Page 1/2  < > page  Up Dn pick  K kick  S snoop  Q quits  refresh 5s
No idle limit
```

- 24 rows: title 1, header 1, nodes 12, rule 1, waiting 1, vitals 2,
  section 1, calls `rows() - 21` clamped 1 to 5 (24 rows: 3, 25: 4, 26 or
  more: 5), footer 2. Never taller than the terminal.
- Title: `rowTitle("DASHBOARD", right)`, right is `"%u of %u on  %a %d %b
  %H:%M:%S"` at 60 columns or more. The count is `activeNodes()`; "on"
  because the sysop is counted in the title of the screen they are on.
- Waiting row: Yellow when anything, DarkGrey `Nothing waiting on you`
  when nothing. Pieces joined with two spaces, in this order: ring notes,
  uploads to approve, unread mail, `backup Y/N` when
  `backup_.awaitingApproval()`. A live ring never shows here: it stops the
  watch (bbs.cpp:2509) and asks at the prompt, which is right.
- Vitals row 1: `Up <fmtUptime>  Heap <free>K low <low>K  Stack <least>
  Loop <avg>us worst <max>ms  Slow <n>`. Row 2: `WiFi <rssi> dBm ch <ch>
  <awake|SLEEPING>  Data <free>K  Card <free> GB|MB|none  Dir <state>
  Backup <closed|open m:ss|Y/N>`. Labels Grey, figures White, a figure in
  LightRed when: heap free under `BBS_HEAP_RESERVE`, stack under 1,024,
  radio not awake, directory `held`, slow count changed since the last
  frame. Widest measured 74 and 74.
- Section rule `rowSection("last calls, 12 today")`. The count is the
  cached figure below, not a file pass.
- Footer row 1 in Cyan, 68 wide; row 2 is `rowWatchFooter`'s second line
  as today (idle, `page!`).

### 80 columns, page 2

```
          1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
 DASHBOARD                                     4 of 10 on  Thu 24 Sep 22:07:15
-- plugins --------------------------------------------------------------------
SD: SDHC  29684 MB free of 30436
Files: 2 areas
3 forums, 41 messages
Directory: online  12 sent 0 failed
Lights: drive flicker, strip nodes
-- bans -----------------------------------------------------------------------
198.51.100.4     12 min left
(seven blank rows: BBS_BAN_SLOTS is 8, the frame keeps the height)
-- last calls, 12 today -------------------------------------------------------
 1 quantumrob           09/24 21:40   47      192.168.1.23    ANSI-CP437
 3*guest                09/24 21:12    8      10.0.0.7        PETSCII-40
 7 amiga_al             09/24 20:58   22      192.168.1.77    ANSI-CP437
 5>steve                09/24 19:30   15      172.16.0.44     ASCII
 2 vt220bob             09/24 18:02    9      203.0.113.9     ASCII
Page 2/2  < > page  Q quits  refresh 5s
No idle limit
```

- 24 rows: title, section, 5 plugin rows (blank when fewer, cut at
  `rowWidth`), section, 8 ban rows or `none`, section, 5 calls, footer 2.
- Plugin rows in Cyan as today. The forums line should start with its
  name like the others (`Forums: 3, 41 messages`); it is the one that does
  not.

### 40 columns (PETSCII-40, 25 rows), three pages

Node row `%s%c%-12.12s %-9.9s %5s %4s`, 36 wide: the current DASH row,
which is right at this width. Handle at column 3 on every page so the grid
does not move between pages.

Page 1:

```
          1         2         3
0123456789012345678901234567890123456789
 DASHBOARD           4 of 10  22:07:15
 N Handle       Doing      Idle Left
 1 quantumrob   FORUMS    00:12   47
 2 (logging in) login     00:03   --
 3*guest        CHAT      01:40    8
 4 -
 5>steve        MAIL      00:00   --
 6 -
 7 amiga_al     FILES     04:12   22
 8 -
 9 -
10 -
 S]quantumrob   DASH      00:01   --
 B -
---------------------------------------
Waiting: 2 notes  1 upload  3 mail
Up 3d 02:14  Heap 78K low 61K
Stack 4,120  Loop 58us worst 342ms
WiFi -35 dBm  Slow 12  Dir listed
Data 512K  Card 29 GB  Backup closed
-- last calls, 12 today ---------------
 1 quantumrob   09/24 21:40   47
 3*guest        09/24 21:12    8
Pg 1/3  < > page  Up Dn K S  Q quit 5s
No idle limit
```

- 25 rows exactly: title, header, 12 nodes, rule, waiting, 4 vitals,
  section, 2 calls, footer 2. Calls `rows() - 23`, so an ANSI terminal
  that reports 40x24 gets one.
- Title right at under 60 columns: `"%u of %u  %H:%M:%S"`.
- Calls row `%s%c%-12.12s %-11s %4u`, 32 wide, no address: page 3 has it.

Page 2, where from:

```
          1         2         3
0123456789012345678901234567890123456789
 DASHBOARD           4 of 10  22:07:15
 N Handle       Address         Term
 1 quantumrob   192.168.1.23    CP437
 2 (logging in) 203.0.113.9     UTF8
 3*guest        10.0.0.7        PET40
 4 -
 5>steve        172.16.0.44     ASCII
 6 -
 7 amiga_al     192.168.1.77    CP437
 8 -
 9 -
10 -
 S]quantumrob   192.168.1.50    CP437
 B -
---------------------------------------
SD: SDHC  29684 MB free of 30436
Files: 2 areas
3 forums, 41 messages
Directory: online  12 sent 0 failed
Lights: drive flicker, strip nodes
(three blank rows)
Pg 2/3  < > page  Q quit  5s
No idle limit
```

- Row `%s%c%-12.12s %-15.15s %-5.5s`, 37 wide. The terminal is a
  five-character code, new `Term::shortName()`: `CP437`, `UTF8`, `PET40`,
  `PET80`, `ASCII`. A nine-character handle here (NODES's narrow row)
  would cut `quantumrob` to `quantumro` and `(logging in)` to
  `(logging`, and move the address column off the grid page 1 set.
- Announce's `Directory: held, the sysop password is the default` is 51
  and cuts to 39 mid-sentence; the plugin should say `Directory: held,
  default password` (34) when under 60 columns. Its problem, one line.

Page 3, calls and bans:

```
          1         2         3
0123456789012345678901234567890123456789
 DASHBOARD           4 of 10  22:07:15
 N Handle        Time Address
 1 quantumrob   21:40 192.168.1.23
 3*guest        21:12 10.0.0.7
 7 amiga_al     20:58 192.168.1.77
 5>steve        19:30 172.16.0.44
 2 vt220bob     18:02 203.0.113.9
---------------------------------------
Bans 1  Busy free  Backup closed
198.51.100.4     12 min left
(twelve blank rows)
Pg 3/3  < > page  Q quit  5s
No idle limit
```

- Calls row `%s%c%-12.12s %5s %-15.15s`, 37 wide. Time not date, and no
  minutes: at 40 the address is the point of this page and the date is on
  page 1. Ban rows as `rowBans`.

### 132 columns, one page (optional, after 40 and 80)

```
          1         2         3         4         5         6         7         8         9         0         1         2         3
012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901
 DASHBOARD                                                                                         4 of 10 on  Thu 24 Sep 22:07:15
 N Handle               Doing       Idle Left   On Address         Terminal     -- board ------------------------------------------
 1 quantumrob           FORUMS     00:12   47   13 192.168.1.23    ANSI-CP437   Uptime        3d 02:14
 2 (logging in)         login      00:03   --    0 203.0.113.9     ANSI-UTF8    Heap free       78,412 low 61,200
 3*guest                CHAT       01:40    8   31 10.0.0.7        PETSCII-40   Stack free       4,120 least of 12,288
 4 -                                                                            Loop avg            58 us, worst 342,118 in session
 5>steve                MAIL       00:00   --    2 172.16.0.44     ASCII        Slow passes         12 over 50ms
 6 -                                                                            Signal         -35 dBm excellent, ch 6, awake
 7 amiga_al             FILES      04:12   22   18 192.168.1.77    ANSI-CP437   Data free      524,288 bytes
 8 -                                                                            Card             29 GB free of 30
 9 -                                                                            Directory       online 12 sent
10 -                                                                            Backup          closed
 S]quantumrob           DASH       00:01   --    4 192.168.1.50    ANSI-CP437   Bans                 1 198.51.100.4, 12 min
 B -                                                                            Lights           drive flicker, strip nodes
-----------------------------------------------------------------------------------------------------------------------------------
Waiting on you: 2 ring notes  1 upload to approve  3 unread mail
-- last calls, 12 today -----------------------------------------------------------------------------------------------------------
 1 quantumrob           09/24 21:40   47           192.168.1.23    ANSI-CP437
 3*guest                09/24 21:12    8           10.0.0.7        PETSCII-40
 7 amiga_al             09/24 20:58   22           192.168.1.77    ANSI-CP437
 5>steve                09/24 19:30   15           172.16.0.44     ASCII
 2 vt220bob             09/24 18:02    9           203.0.113.9     ASCII
< > page  Up Dn pick  K kick  S snoop  Q quits  refresh 5s
No idle limit
```

- 24 rows. Left block columns 0-76: the 80 row plus `On` (minutes on this
  call, 4 wide, from `connectedAt`) between Left and Address. Gutter
  77-79. Right block from column 80, 51 wide: `statRow` lines (label 13,
  value 9, note), which is SYS's grammar, so the same figure reads the same
  on both screens. Rows 1-13 are built as `left.ljust(77) + 3 spaces +
  right`; the two vitals rows and page 2 disappear because everything is
  on this page. Calls always 5.
- If this is not built, the 80 layout at 132 is correct and merely wider
  in its bars. That is acceptable; a stripe of black is not.

### Plain ASCII

80x24, no home, no reverse, cursor keys not guaranteed.

- `DASH`: page 1 once, as a list. `DASH ALL`: every page in order through
  the ordinary pager. `DASH n`: page 1 redrawn below itself every n
  seconds, as `serviceWatch` already does for ASCII (bbs.cpp:2839).
- Keys in `DASH n`: digits pick a page, Q or ESC or Ctrl-C stop. No
  selection, no K or S; the footer says so: `Page 1/2  1 2 pick a page  Q
  quits  refresh 5s`.
- The rule row is `rowRule`'s dashes and the title is the dashed bar
  `rowBar` already draws for ASCII.

### Colour

Same thing, same colour, as WHO and NODES today: node LightBlue, mark by
`markColor`, handle LightGreen (White for the viewer's own line, DarkGrey
when hidden), doing Cyan, idle Grey and DarkGrey past five minutes, left
Grey and LightRed under five minutes, address Grey, terminal DarkGrey,
free line DarkGrey, rules and title Cyan, header LightBlue, waiting Yellow
or DarkGrey, vitals Grey labels and White figures with LightRed alarms,
plugin rows Cyan, footer Cyan and DarkGrey. The marker key line is not
drawn: the markers are the same on WHO, NODES and LAST and the key is
printed on each of those.

## What goes on it, ranked, with source and cost

| Rank | Item | Source | Cost per frame |
|---|---|---|---|
| 1 | every session: node, mark, handle, doing, idle, left | `Session` fields, `doingText`, `secondsLeft` | none |
| 2 | address and terminal per session | `Session::ip`, `term.name()` | none |
| 3 | waiting on you: ring notes, uploads, unread mail, backup Y/N | new `ringNotesWaiting_`; new plugin hook (files `g_pending`, chat `mailNewFor(s.user)`); `backup_.awaitingApproval()` | none |
| 4 | uptime, heap free, heap low, stack least | `plat::millis()`, `plat::heapFree()`, `heapLow_`, `stackLow_` | none; `plat::heap()` is not called |
| 5 | loop avg, worst, slow passes | `loopAvgUs_`, `loopMaxUs_`, `slowCount_` | none |
| 6 | Wi-Fi RSSI, channel, awake | one `plat::netInfo()` into the snapshot; `plat::powerSave()` | one Wi-Fi task call |
| 7 | data free | `plugins::freeBytes()`, cached 60 s | the once-a-minute 85 ms walk, wherever it lands |
| 8 | card free | sd `status()` figure with a 60 s age for the dash | one `f_getfree` a minute instead of every 3 s |
| 9 | directory state | announce `status()` | none |
| 10 | backup window | `backup_.isOpen()`, `closesAt()` | none |
| 11 | calls today, last 5 calls with address | new calllog RAM ring and cached count | none; one file pass at boot and one when the day rolls |
| 12 | bans | `BanList::at` x 8 | none |
| 13 | plugin status rows | `Plugin::status` | none by contract (plugin.h:222-226) |
| 14 | lights outputs | lights `status()` | none |

Not on it: NTP as a word (the clock in the title says it), the version
(SYS and ABOUT), biggest block and loop passes (SYS). DASH glances, SYS
explains; a dashboard that repeats SYS is two screens that drift.

## The snapshot, and how a frame is built

- `struct DashSnap` on `Bbs`, one for the board, about 64 bytes: heap
  free and low, stack least, loop avg, worst and slow, rssi, channel,
  awake, data free, card free, directory state pointer, backup state and
  seconds, bans, calls today, ring notes, and the waiting phrases. Filled
  at row 0 of every frame, read by every row after. Two sysops watching
  share it and read a figure at most one frame old.
- `rowSys` takes the same snapshot at its row 0 instead of `plat::heap()`
  and `plat::netInfo()` per row.
- Frame size at 80 on ANSI is about 2 to 2.5 KB (24 rows of 72 with
  colour runs and `ESC[K`); on PETSCII-80 nearer 2.2 KB because rows pad
  with spaces; at 40 about 1.2 KB. `BBS_TL_BYTES` is 3,072 and
  `serviceWatch` draws rows while 512 bytes and 16 frames are free
  (bbs.cpp:2823), so a frame is one pass when the timeline is empty and
  two when it is not. Nothing changes there.
- Refresh: `DASH n`, n 1 to 30 as today (`who_refresh_min/max`). A timed
  redraw homes and redraws the same page, no clear, no flicker, as now.
  A page change clears first (`term.cls`) then draws: one flash per key,
  and no tail from a page of a different shape.
- Every page at a given width has a fixed height, blank rows included, so
  the home-and-redraw rule holds on each page.

## Keys in `DASH n`

- Left, Right, `<`, `>`, `-`, `+`: previous and next page, wrapping.
- `1` to `3`: that page (2 pages at 80, 3 at 40, 1 at 132; a number past
  the last is ignored).
- Enter: redraw now.
- Q, ESC, Ctrl-C, Space: stop, `stopWatch` as today.
- Anything else: ignored. WHO n and NODES n keep "any key stops".
- Optional, ANSI and PETSCII only: Up and Down move a selection over the
  twelve node rows on page 1 (and page 2 at 40), drawn with
  `Term::reverse` on. The selection is a node id, so it stays on node 7
  across refreshes. K stops the watch and runs `KICK <n>` at the prompt
  (`cmdKick` already asks nothing and prints `Node 7 disconnected.`); S
  stops the watch and runs `cmdSnoop(s, "7")`. Both use the existing
  handlers and their rank checks; nothing new decides who may be kicked.
- Not offered: O. A live ring already stops the watch and asks at the
  prompt.

## Hand-backs for the builder

Static DRAM: 260 (calllog ring) + 64 (snapshot) + 2 (notes count) + 24
(two Session bytes x 12) is under 400 bytes against about 19 KB free.

- `calllog`: keep the newest 5 `CallRec` in a static ring (5 x 52 = 260
  bytes), filled by `loadHeader()` with one read and shifted by `append()`.
  `get(back)` for back under 5 answers from RAM. Keep `g_today` and
  `g_todayStart`: `append()` increments when `r.start >= g_todayStart`; a
  getter recomputes with one file pass only when `clk::todayStart()` has
  moved. That is "only when a call has actually ended" plus once a day.
- `Bbs::ringNotesWaiting_`, uint16: `++` in `ringSaveNote`, 0 in
  `ringNotes`. Gap to close with it: the notes are only shown at login or
  elevation (bbs_ring.cpp:777), so a sysop who sees `2 ring notes` on the
  dash has no way to read them until the next login. A bare `O` for the
  sysop with no ring live should call `ringNotes(s)` instead of `Nobody is
  ringing.` (:263-267).
- Plugin hook, appended after the last field of `Plugin` (positional, as
  plugin.h:256-258 warns): `bool (*waiting)(const Session& s, char* out,
  size_t n)`, "what this plugin has waiting on this staff member". files
  writes `2 uploads to approve` from `g_pending` (files.cpp:1349); chat
  writes `3 unread mail` from `mailNewFor(s.user)` (chat.cpp:900). The
  core joins the answers on the waiting row. Nothing couples the core to
  either plugin.
- `plat`: nothing new for the heap. `plat::heapFree()` exists
  (platform_esp32.cpp:154) and `heapLow_` is kept by `heapWatch`; DASH and
  SYS stop calling `plat::heap()` per frame and per row.
- sd plugin: `status()` reads a figure aged up to 60 s; `cardInfo(true)`
  for the SD command is unchanged.
- `Term::shortName()`: the five-character codes above, beside `nameOf`.
- `Session`: `uint8_t dashPage`, `uint8_t dashSel` (0xFF none), reset in
  `startWatch`. 24 bytes across the pool. Packing both into one byte is
  not worth the reading.
- One node-row builder, `Bbs::rowNodeLine(Session& viewer, const Session&
  o, uint8_t plan)`, used by DASH, NODES and WHO, plan chosen from
  `rowWidth(s)`: 80 (the 72-column row), 40 (the 36-column row), 40-from
  (the address row). NODES's row becomes the 80 plan with the Doing
  column, which fixes its terminal cut; WHO's row follows its own header
  again.
- `onKey` at `SState::Watch`: `if (s.watch == ListKind::Dash) dashKey(s,
  k, now); else stopWatch(s);` (bbs.cpp:3051).
- `serviceWatch`: on a page change `cls` before the draw; on a timed
  refresh `home` as now (bbs.cpp:2838). `rowDash` takes the page from
  `s.dashPage` and draws to the fixed height for that page and width.
- `cmdDash`: `DASH` page 1 once, `DASH ALL` every page as a paged list,
  `DASH n` refresh. `DASH n` keeps meaning seconds because WHO n and NODES
  n do.
- `rowWatchFooter`: a DASH footer row 1 per width (68 at 80, 38 at 40,
  ASCII variant), row 2 unchanged.
- helptext.cpp:62: the DASH entry describes the pages and keys.
- rowWidth's comment (bbs_shell.cpp:484-485) says plain ASCII stays at 39
  columns; the detector gives ASCII 80x24 (detect.cpp:172, :177). Harmless
  and wrong; fix the comment.

## DASH and NODES

- Rebuild DASH the way NODES is built: yes. Node order, a row per session,
  widths from `rowWidth`, the address. NODES's `%-9.9s` and missing Doing
  column are the two things it gets wrong.
- Merge them: no. `PERM_NODES` and `PERM_DASH` are separate rows in the
  `[access]` matrix (sysconfig.h:87, sysconfig.cpp:56), NODES is the
  co-sysop's plain list and the one that pages, and `NODES n` is a
  refresh screen whose "any key stops" should stay simple. One row
  builder, two verbs. NODES becomes exactly DASH page 1's node block with
  its own title and the marker key, and nothing else.

## What stays

- `serviceWatch`'s frame-across-passes and `rowEnd`'s `ESC[K`: measured
  correct in 0.18.0, and the frame sizes here are under one timeline.
- `rowTitle`, `rowRule`, `rowSection`, `statRow`: the house grammar, used
  unchanged.
- The 40-column node row: 36 wide, right the first time.
- `DASH n` bounds of 1 to 30 s and the second footer row.
- WHO n and NODES n stop on any key.
- LAST as the whole log with its own columns; SYS as the numbers screen.
- The `*GUEST >CO-SYSOP ]SYSOP` key stays on WHO, NODES and LAST.

## Order, cheapest and most visible first

- Data: calllog ring and today count, `ringNotesWaiting_`, the `waiting`
  hook, sd status age, `DashSnap` used by SYS too. This alone takes the
  file opens and the heap walk out of the loop and can ship before any
  screen changes.
- One node-row builder; NODES and WHO drawn through it. Three screens
  line up at 80 for the price of one function.
- DASH page 1 at 80 and 40, `DASH` and `DASH n`, Q to leave.
- Pages 2 and 3, the page keys, `DASH ALL`, the plain-ASCII footer.
- Selection with Up and Down, K and S. Optional.
- The 132 right-hand column. Optional.
