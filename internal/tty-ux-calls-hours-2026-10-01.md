# CALLS by hour: two columns of 12 (tty-ux, 2026-10-01)

Queue item F5, for 1.2.1. Spec only; no code changed.

Measured on the host build (`host/bbs_host`, rowCalls unchanged since 334f230) on
127.0.0.1:7731, a throwaway data directory with a seeded 50-record caller log,
driven by `tools/testclient.py`'s Caller and rendered back to a character grid.
Seven captures: ANSI at 40x24, 80x24, 132x24 and 132x50 (NAWS), PETSCII-40,
PETSCII-80, plain ASCII. Code: `Bbs::cmdCalls` and `Bbs::rowCalls`,
`src/core/bbs_shell.cpp:3020-3093`.

## The verdict

CALLS is a 40 column chart that grew sideways when `rowWidth` was unclamped,
and the growth went into the one dimension it did not need. It is 28 list rows
at every width, so it pages at every width on every 24 or 25 row terminal, and
the screen a caller is left with has lost the title and the small hours. At 132
a single call is a 13 cell bar. The fix is cheap and contained: two panes of 12
hours, 00-11 left and 12-23 right, at every width from 40 up, with the count
moved beside its hour. That is 16 list rows at 40 and 15 at 80 and 132, one
screen everywhere, no `[More]`, no new state, no RAM. Fixable, not a rethink.

## What it draws today (measured)

| Terminal | rowWidth | Bar room | Row ends at col | Rule ends at col | List rows | `[More]` after | Bytes |
|---|---|---|---|---|---|---|---|
| PETSCII-40, 40x25 | 39 | 29 | 36 | 38 | 28 | 23 rows (00-21) | 1,470 |
| ANSI 40x24 | 39 | 29 | 36 | 38 | 28 | 22 rows (00-20) | 2,204 |
| ANSI 80x24, UTF-8 | 79 | 69 | 76 | 78 | 28 | 22 rows | 3,776 |
| PETSCII-80, 80x25 | 79 | 69 | 76 | 78 | 28 | 23 rows | 2,956 |
| ASCII 80x24 | 79 | 69 | 76 | 78 | 28 | 22 rows | 2,171 |
| ANSI 132x24, UTF-8 | 131 | 121 | 128 | 130 | 28 | 22 rows | 5,806 |
| ANSI 132x50 | 131 | 121 | 128 | 130 | 28 | none | 5,771 |

Nothing wraps at any width. Every other fault is below.

## Findings, worst first

### 1. It never fits one screen, and the title scrolls off

`rowCalls`, rows 0-27: title, 24 hours, rule, two footer lines. 28 list rows
against `pageRows` = rows - 2, which is 22 on a 24 row terminal and 23 on a C64.
The command echo above and the blank line and prompt below make it 31 lines.

Breaks at: every width, every terminal, on any screen under 31 rows.

PETSCII-40 on a C64, page one (the `[More]` row is rubbed out on resume and row
21 lands on it):

```
0         1         2         3
0123456789012345678901234567890123456789
[1] Main: calls
▒Calls▒by▒hour▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒50▒calls▒
00 ███                              1
01                                  0
 ...  (02 to 19)
20 █████████████████████████████    9
21 ████████████                     4
[More] Y/n/c
```

And what is left on the C64 after SPACE, all 25 rows:

```
0         1         2         3
0123456789012345678901234567890123456789
04                                  0
05                                  0
06 ███                              1
07 ██████                           2
08 ███                              1
09 ██████                           2
10 ███                              1
11 ███                              1
12 ██████                           2
13 ██████                           2
14 ██████                           2
15 ██████                           2
16 █████████                        3
17 █████████                        3
18 ████████████                     4
19 ████████████████                 5
20 █████████████████████████████    9
21 ████████████                     4
22 █████████                        3
23 ██████                           2
───────────────────────────────────────
Busiest 20:00 with 9
Last 50 calls kept

[1] Main:
```

No title, no 00-03. At ANSI 80x24 the final screen starts at hour 05. The chart
exists to show the shape of a day, and the page break cuts the day at 21:00 or
22:00, which on most boards is the middle of the evening peak. A single column
cannot fix this at 40 either: 24 hours plus a title is 25 rows, and `pageRows`
is at most 23 on any terminal this board detects.

### 2. The wide terminal's columns went into bar length, not layout

`room = rowWidth(s) - 10` (bbs_shell.cpp:3063) is the whole width rule. At 80
the peak bar is 69 cells, at 132 it is 121. With 9 calls at the peak, one call
is 7 cells at 80 and 13 at 132. The right two thirds of a 132 column screen is a
black field with a column of counts at its far edge.

Breaks at: 80 and 132. Width was never the constraint; rows were.

```
0         1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
05                                                                          0
06 ███████                                                                  1
07 ███████████████                                                          2
 ...
20 █████████████████████████████████████████████████████████████████████    9
21 ██████████████████████████████                                           4
```

### 3. The count is 70 columns from its hour

Label at col 0, count right-aligned ending at col 76 (80) or col 128 (132). To
read "how many at 07:00" the eye crosses the whole screen along an empty row.
At 40 it is tolerable; at 80 it is a ruler exercise.

### 4. Widths that do not add up

- `room = w - 10`, but the row spends 8 columns outside the bar: `"%02u "` is 3
  and `" %4u"` is 5. So every hour row ends 2 columns short of the rule and the
  title bar: col 36 against 38 at 40, col 76 against 78 at 80.
- `%4u` for a count whose maximum is `BBS_CALLLOG_SIZE`, 50: two of the four
  digits can never be used.
- The title's right text ("50 calls") ends at col w-2; the counts under it end
  at col w-3. One column ragged, directly under the bar that sets the edge.

Arithmetic at 40: 3 + 29 + 5 = 37 used of 39. At 80: 3 + 69 + 5 = 77 of 79.

### 5. A zero hour prints "0" down the right edge

Six of the 24 rows on a quiet board are `NN   ...   0`. With colour the label
and the zero are DarkGrey and recede. On plain ASCII there is no DarkGrey, so a
column of zeros carries the same weight as the real counts.

### 6. PETSCII spends 3 bytes a bar cell

`Term::glyph(Glyph::Block)` on PETSCII is `0x12 0x20 0x92` per cell (term.cpp:514),
reverse on and off around every space. A 29 cell bar is 87 bytes where 31 would
do. Not visible, but it is most of the 1,470 bytes a C64 caller receives, at
2400 baud for anyone on a real modem.

### 7. Small copy points

- "1 calls" in the title and "with 1" in the footer when the count is one.
- Hours are the board's local time (`clk::fmtEpoch`, the board's `tz`), and
  nothing says so. A caller in another zone reads 20:00 as their own evening.
- "Last 50 calls kept" restates the ring size every time, including when the
  title already says 50.
- CLAUDE.md's 0.11.0 note calls CALLS a staff screen. It is `CF_NONE` on the
  account menu: every caller sees it, so it is a public screen and should be
  designed as one.

## The fix

### Layout rule

Two panes, one hour from each half per row. Row k (k = 0..11) carries hour k on
the left and hour k + 12 on the right. Reading order is down the left, then down
the right: the morning and the afternoon, which is how everybody already divides
a day. Not three or four columns at 132: a break at 08:00 or 16:00 means
nothing, and the extra width is better spent on bar resolution.

Two panes at every width where the bar gets at least 10 cells, which is
`rowWidth(s) >= 35`. That covers 40, 80 and 132. Below 35 (only a NAWS window
narrower than 36 columns can get there) the single column stays, re-ordered as
below, and pages as it does now.

Paging at 40 is not right and is not needed: the two-pane layout is 16 list
rows and fits a C64 with 6 rows to spare.

### Geometry, in columns

With `w = rowWidth(s)` and `D` = digits in `BBS_CALLLOG_SIZE` (2 today; derive
it, do not type it):

- pane prefix: hour 2, space 1, count D, space 1 = `D + 4` (6 today)
- gutter `G` = 3: space, divider, space
- pane width `P = (w - G) / 2`, integer
- bar width `B = P - (D + 4)`
- left pane starts at col 0, right pane at `R = P + 3`
- an odd `w - G` leaves one spare column at the far right, never in the middle

| Element | Columns | 40 (w 39) | 80 (w 79) | 132 (w 131) |
|---|---|---|---|---|
| left hour | 0-1 | 0-1 | 0-1 | 0-1 |
| left count, right-aligned | 3 to 2+D | 3-4 | 3-4 | 3-4 |
| left bar | D+4 to P-1 | 6-17 (12) | 6-37 (32) | 6-63 (58) |
| gutter space | P | 18 | 38 | 64 |
| divider | P+1 | 19 | 39 | 65 |
| gutter space | P+2 | 20 | 40 | 66 |
| right hour | R to R+1 | 21-22 | 41-42 | 67-68 |
| right count | R+3 to R+2+D | 24-25 | 44-45 | 70-71 |
| right bar | R+D+4 to R+P-1 | 27-38 (12) | 47-78 (32) | 73-130 (58) |
| last column used | 2P+2 | 38 | 78 | 130 |

The right bar's last cell is the rule's last cell at all three widths, and no
row ever writes the terminal's final column (39, 79, 131), so a C64 never
line-links a row.

PETSCII-80 is the 80 column geometry. Plain ASCII is 80x24 from detection, so it
is the 80 column geometry too.

The left pane is padded with spaces from the end of its bar to col P-1, so the
divider lands on the same column every row. The right pane is not padded: CALLS
is not a refresh screen, and `rowEnd` handles the newline.

### The bar

| Terminal | Bar cell | Divider | Rule |
|---|---|---|---|
| ANSI, CP437 | `Glyph::Block`, 0xDB | `Glyph::VLine`, 0xB3 | `rowRule` (0xC4) |
| ANSI, UTF-8 | U+2588 through `Term::cp437` | U+2502 | U+2500 |
| PETSCII-40 and -80 | reverse space (`Glyph::Block`) | 0xDD (`Glyph::VLine`) | 0xC0 |
| Plain ASCII | `#` | `|` | `-` |

All of these exist in `Term::glyph` today. Nothing new to draw.

Scale: one peak for all 24 hours, shared by both panes, so the halves compare.
`bar = v * B / peak`, integer floor, and a non-zero hour gets at least 1 cell.
That is today's rule with `B` in place of `room`. Floor rather than rounding on
purpose: at B = 12 and a peak of 9, rounding draws 1 call as 1 cell and 2 calls
as 3, which reads as triple. Floor gives 1 and 2.

The peak hour's bar is exactly B. At 132 one call is 6 cells; no cap, because a
chart that stops at half a pane under a full width title bar reads unfinished.

### An empty hour

Hour label DarkGrey, count shown as `-` right-aligned in the count field,
DarkGrey, no bar. The dash reads as "none" on every terminal, plain ASCII
included, where DarkGrey does not exist and a `0` would weigh the same as a 9.

### Colour

Unchanged where it already meant something:

| Piece | Colour |
|---|---|
| title bar | Cyan reverse (`rowTitle`) |
| hour with calls | LightBlue |
| hour without | DarkGrey |
| count | LightGrey; Yellow on the peak hour(s) |
| bar | LightGreen; Yellow on the peak hour(s) |
| divider | DarkGrey |
| rule | Cyan (`rowRule`) |
| busiest line | Cyan |
| note | DarkGrey |

Every hour that ties the peak is Yellow, as today. Plain ASCII loses the colour;
the busiest line names the hour, which is enough.

### Totals

Left: `Busiest 20:00 with 9 calls` in Cyan (`with 1 call` for one). Right, in
DarkGrey, ending at col w-2 so it lines up with the title's right text:
`Board time. The log keeps 50 calls.`

One row when both fit with at least 2 spaces between them
(`len(a) + 2 + len(b) <= w - 1`), otherwise two rows, the second being the short
form `Board time. The log keeps 50.` That is a fit test, not a frozen width:
80 and 132 get one row, 40 gets two.

The title's right text stays `N calls`, singular at one.

### The empty state

Unchanged: title, `Nothing logged with a clock yet.` in DarkGrey, rule. Three
rows at every width.

### Row map for rowCalls

Two-pane mode (`w >= 35`):

- `listIdx` 0: title
- 1 to 12: the hour pair `listIdx - 1` and `listIdx + 11`
- 13: rule
- 14: totals (one row, or the first of two)
- 15: the short note, only when it did not fit on row 14
- then false

Single-column fallback (`w < 35`): today's rows, with the same pane prefix
(`HH NN bar`), `B = w - (D + 4)`, the dash for an empty hour and the same
totals. It pages; nothing narrower than 36 columns is detected today.

No new Session fields, no statics: `callHours_` and `callsCounted_` already hold
everything.

## The mock-ups

Generated from the rules above (`mock.py` in the session scratchpad), not
typed. Seed: 50 calls, peak 9 at 20:00.

### PETSCII-40 on a C64 (ANSI at 40 is the same grid)

```
0         1         2         3
0123456789012345678901234567890123456789
[1] Main: calls
▒Calls▒by▒hour▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒50▒calls▒
00  1 █            │ 12  2 ██
01  -              │ 13  2 ██
02  -              │ 14  2 ██
03  -              │ 15  2 ██
04  -              │ 16  3 ████
05  -              │ 17  3 ████
06  1 █            │ 18  4 █████
07  2 ██           │ 19  5 ██████
08  1 █            │ 20  9 ████████████
09  2 ██           │ 21  4 █████
10  1 █            │ 22  3 ████
11  1 █            │ 23  2 ██
───────────────────────────────────────
Busiest 20:00 with 9 calls
Board time. The log keeps 50.

[1] Main:
```

19 of 25 rows, title on screen, whole day visible. ▒ is the reverse-video
title bar; on the glass the bars are reverse spaces in light green and the 20:00
bar and its 9 in yellow.

### ANSI 80 (PETSCII-80 is the same grid on 25 rows)

```
0         1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
[1] Main: calls
▒Calls▒by▒hour▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒50▒calls▒
00  1 ███                              │ 12  2 ███████
01  -                                  │ 13  2 ███████
02  -                                  │ 14  2 ███████
03  -                                  │ 15  2 ███████
04  -                                  │ 16  3 ██████████
05  -                                  │ 17  3 ██████████
06  1 ███                              │ 18  4 ██████████████
07  2 ███████                          │ 19  5 █████████████████
08  1 ███                              │ 20  9 ████████████████████████████████
09  2 ███████                          │ 21  4 ██████████████
10  1 ███                              │ 22  3 ██████████
11  1 ███                              │ 23  2 ███████
───────────────────────────────────────────────────────────────────────────────
Busiest 20:00 with 9 calls                 Board time. The log keeps 50 calls.

[1] Main:
```

18 of 24 rows.

### Plain ASCII 80

```
0         1         2         3         4         5         6         7
01234567890123456789012345678901234567890123456789012345678901234567890123456789
[1] Main: calls
Calls by hour -------------------------------------------------------- 50 calls
00  1 ###                              | 12  2 #######
01  -                                  | 13  2 #######
02  -                                  | 14  2 #######
03  -                                  | 15  2 #######
04  -                                  | 16  3 ##########
05  -                                  | 17  3 ##########
06  1 ###                              | 18  4 ##############
07  2 #######                          | 19  5 #################
08  1 ###                              | 20  9 ################################
09  2 #######                          | 21  4 ##############
10  1 ###                              | 22  3 ##########
11  1 ###                              | 23  2 #######
-------------------------------------------------------------------------------
Busiest 20:00 with 9 calls                 Board time. The log keeps 50 calls.

[1] Main:
```

No cursor addressing and no reverse video used anywhere in it; it is the same
row stream with the ASCII glyphs.

### ANSI 132

```
0         1         2         3         4         5         6         7         8         9         0         1         2         3
012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901
[1] Main: calls
▒Calls▒by▒hour▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒50▒calls▒
00  1 ██████                                                     │ 12  2 █████████████
01  -                                                            │ 13  2 █████████████
02  -                                                            │ 14  2 █████████████
03  -                                                            │ 15  2 █████████████
04  -                                                            │ 16  3 ███████████████████
05  -                                                            │ 17  3 ███████████████████
06  1 ██████                                                     │ 18  4 ██████████████████████████
07  2 █████████████                                              │ 19  5 ████████████████████████████████
08  1 ██████                                                     │ 20  9 ██████████████████████████████████████████████████████████
09  2 █████████████                                              │ 21  4 ██████████████████████████
10  1 ██████                                                     │ 22  3 ███████████████████
11  1 ██████                                                     │ 23  2 █████████████
───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Busiest 20:00 with 9 calls                                                                     Board time. The log keeps 50 calls.

[1] Main:
```

18 of 24 rows. A taller window (132x50) draws the same 15 rows: the rule is by
width only.

### Vertical budget

| Terminal | Rows | pageRows | List rows | With echo, blank, prompt | Pages |
|---|---|---|---|---|---|
| PETSCII-40 | 25 | 23 | 16 | 19 | 1 (was 2) |
| ANSI 40x24 | 24 | 22 | 16 | 19 | 1 (was 2) |
| ANSI 80 / ASCII 80 | 24 | 22 | 15 | 18 | 1 (was 2) |
| PETSCII-80 | 25 | 23 | 15 | 18 | 1 (was 2) |
| ANSI 132 | 24 | 22 | 15 | 18 | 1 (was 2) |

### Bytes

ANSI 80 UTF-8 falls from 3,776 measured to about 2.4 KB (estimate): 171 bar
cells instead of 376, and 355 spaces of pane padding instead of about 1,280 of
dead row. Measure after the build. PETSCII-40 shrinks in proportion, and further
with finding 6.

## What stays as it is

- `rowTitle` and `rowRule` at the full `rowWidth`: the bar and the rule already
  agree with each other at every width. The chart is what moves to meet them.
- The colours. LightGreen bars with the peak in Yellow already carry meaning
  the same way across the board; the change only adds a DarkGrey divider.
- Linear scale to the peak, floor, minimum one cell. It is honest at low
  counts, which is where a 50 call log lives.
- `cmdCalls` bucketing the log in one pass before the list starts. Rows stay
  arithmetic, there is no per-row file read, and rule no. 1 is untouched.
- The list machinery and its paging. CALLS stops needing `[More]` but keeps it,
  for the sub-36 column fallback and anything odd a NAWS reply produces.
- No screen clear. CALLS is a list like WHO and LAST, which print under the
  command; at 15 or 16 rows it no longer scrolls its own title away, which was
  the only reason to want one.
- The empty-state line.
- `HELP CALLS` ("Calls by hour of day, as a bar chart, from the last 50 calls")
  stays true. Nothing to regenerate.

## Implementation order

- Two panes at `rowWidth >= 35`, the geometry above, count before bar, `-` for
  an empty hour. This one change fixes findings 1 to 5 and is the whole of F5.
  One function, `rowCalls`, about 40 lines.
- The totals fit rule, the singular forms and "Board time". Same function, a
  few lines.
- PETSCII run batching: `Term::glyphs(Glyph::Block, n)` on PETSCII emits 0x12,
  n spaces, 0x92 (restoring reverse if it was on), instead of three bytes a
  cell. Terminal layer, separate commit; the fx progress bars get it too.
- A bbs-qa check, once Rob approves the plan: CALLS at 40, 80 and 132 on ANSI,
  PETSCII-40 and ASCII draws no `[More]` at 24 or 25 rows, 16 or 15 list rows,
  every row at most `rowWidth` columns, the divider on col P+1 in every hour row.
- CLAUDE.md: correct the 0.11.0 line that calls CALLS a staff screen.
