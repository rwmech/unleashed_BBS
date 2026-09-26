# Skin widgets: live BBS data on the five stock scenes

Design spec for the stock panel skins on the Makerfabs ESP32-S3 Parallel
TFT 3.5" (MF35): 480 x 320 landscape, ILI9488 on a 16-bit i80 bus at a
20 MHz WR clock, RGB565 straight from the framebuffer. Measured against
`release-prep/wt-skins` on `panel-skins` (a1fe73f, with the 6x12 face
uncommitted in `panel_font.h`): `SKINS.md`, `skins/stock/*/skin.txt`,
`tools/mkskins_stock.py`, `src/plugins/panel.cpp`, `skin_draw.h`,
`panel_gfx.h`, `src/config.h`, and the five lit previews in
`release-prep/skins-site/previews/`. The glyph sizes were read out of the
Spleen arrays in `panel_font.h`, not assumed. Every box below was checked by
script: inside 480 x 320, no two overlapping, and its width divided by its
face into columns. No code was changed.

Brief: Rob on the glass, "looks good but ... we need some more info, not
just the more or less static image. Can we flesh out what's on the monitor,
or turn parts of the devices themselves to have more bbs info".

## The verdict

Fixable, and worth doing properly, because right now the scenes are
wallpaper with a status block pasted on the tube. Every monitor shows the
same seven words in one face and one colour, all five read identically once
you stop looking at the plastic, and each scene has two live lamps among a
dozen painted ones. The IMSAI is the worst of it: 42 painted lamps and 22 of
them can never light. The vocabulary being built is enough to fix all five
with three small additions (a `+n more` row, a blink, and two lamp states for
a modem). The main design move is to give each scene one job it does better
than the others (the PC is the sysop's waiting-for-caller screen, the Atari
is the event feed, the Apple is the whole switchboard, the front panel is
lamps that mean something) and to put a modem on the desk of three of them,
because a modem's front panel is the one object whose lamps were built to
say exactly what a BBS wants to say at a glance. Two of the current screens
also fail contrast and must change colour whatever else happens.

## The glass, measured

- Active area 73.44 x 48.96 mm over 480 x 320: **0.153 mm a pixel** both
  ways, 166 ppi.
- Cap heights, read from the arrays: tiny 6x12 is 8 px, small 8x16 is 10 px,
  big 16x32 is 20 px. x-heights 6, 7 and 14.
- ISO 9241-303 sets 16 minutes of arc as the minimum height of a Latin
  character, 20 to 22 as what a display must be able to give. At 16':

| Face or figure | Cap px | Cap mm | Readable to | Use it for |
|---|---|---|---|---|
| tiny 6x12 | 8 | 1.22 | 26 cm | printouts, footers, a full roster, painted legends |
| small 8x16 | 10 | 1.53 | 33 cm | the lead list on a screen |
| big 16x32 | 20 | 3.06 | 66 cm | one headline word |
| digits H=24 | 24 | 3.67 | 79 cm | the across-the-desk count |
| digits H=28 | 28 | 4.28 | 92 cm | the same, where there is room |

Arm's length is 60 to 70 cm. **At arm's length only big and seven-segment
digits of 24 px and up are text.** Small is a lean-in face and tiny is a
pick-it-up face. That sets the design, not the other way round:

- What must read from the chair is carried by lamps, digits and the *shape*
  of a list (how many rows are lit), never by small or tiny text.
- Each scene answers three questions at arm's length: is the board up, how
  many are on, does anything need me (ring, mail, error, closed).
- Tiny is never the only place a ring or an error shows.

**The minimums I would accept:**

- The across-the-desk count: `digits` at H >= 24 px, or `big`.
- The lead list on a screen: `small`. `tiny` only for a full roster whose
  waiting rows are dimmed (so the lit rows read as a shape), a printout, a
  footer or a legend.
- A lamp: lens >= 5 px (0.77 mm) with halo >= 4, so the lit box is >= 13 px
  (2 mm). Attention lamps (ring, error) blink (proposal P2).
- Painted legends under a lamp that means something: the tiny face at 1:1.
  The current legends are the 8x16 face painted at one third scale
  (`label(..., px=1)` on the 3x canvas), which makes each glyph 2.7 x 5.3 px:
  texture, not text. That was fine while the lamps meant nothing.
- Text against the art under it: 4.5:1 or better, measured at the lightest
  point of the field it sits on.

## Rules that apply to all five

### The node row, and how it fits a width

Fields, left to right: node (2, `" 1"`, `"10"`, `" S"`), rank mark (1:
`)` caller, `*` guest, `>` co-sysop, `]` sysop), a space, handle, a space,
doing, then time on (3 glyphs: `4m`, `51m`, `2h`, `1d`) **right-aligned to
the box's right edge**, as the status layout does.

- Full row: 2 + 1 + 1 + 16 + 1 + 8 + 1 + 3 = **33 columns**.
- Handles: `BBS_USER_MAX` is 20, but the panel already cuts at 16
  (`%.16s` in `panel.cpp`). The widgets cut at 16 too, so the desk agrees
  with itself. The brief's "16" is the panel's cut, not the account limit.
- Doing: `BBS_DOING_MAX` is 10; the verbs worth telling apart are at most 6
  (CHAT, FILES, MAIL, FORUMS, CONFIG, WHO). Budget 8.

Given `C = W / glyph width`, shrink in this order and stop as soon as the
row fits:

- doing 8 to 6 (C = 31)
- handle 16 to 12 (C = 27)
- drop doing and its space; handle back up to `min(16, C - 9)` (C = 26 and
  below)
- handle down to 8 (C = 17)
- drop time on (C = 14); below that, node, mark and handle only.

Cut hard, no ellipsis: a `~` costs a character the column does not have and
the alignment is what makes the list read.

A waiting row (`free=yes`): node, two spaces, `waiting`. The sysop's line is
listed only while the sysop is visible, as `publicNodes()` counts it.

### The event row

`HH:MM kind handle`, kinds `login guest logoff page ring`. Worst case
`21:40 logoff` plus 16 is **29 columns**; `login` with an 8 character handle
is 20. Cut the handle at the right edge.

### Colour carries the same meaning everywhere

- **Seven-segment red is "callers online"** in every scene that has digits
  (`#FF3020` lit, unlit segments `#2A0604`, which is what the art's `segs()`
  already paints at `c * 0.16`).
- **Lamps follow their device's period colour** (the front panel and the
  modem all red, a PC's power and keyboard lamps green, drive lamps as
  painted). Meaning is carried by position and legend, as it was on the real
  things, and the two states that want somebody (ring, error) blink so they
  stand out of a row of same-coloured lamps.
- **Mono screens carry rank by the mark character**, as a real terminal did
  (C64 blues, green phosphor, the TV). **The colour screen (PC) also tints**
  node and mark by rank and the kind word by kind, in the status layout's
  hues lifted for a blue ground (proposal P3).
- The status layout's own `#E06C6C` sysop red is 3.64:1 on DOS blue and
  fails; use the lifted set in the PC section.

### Rule no. 1: what this costs the loop

- Text is gathered twice a second already. A widget redraws only when its
  words change: the node list once a minute per caller (time on) and on a
  login or logoff, events on an event, digits when the count moves, the graph
  one column twice a second.
- The measured cost is the PSRAM copy: `SKINS.md` gives about 15 ms for the
  whole 153,600 px background, so **0.098 us a pixel** to put art back under
  a line. The wire is not the cost (16 bit at 20 MHz is 7.7 ms a whole frame,
  DMA).
- **Give widgets a pixel budget of 16,384 px a 40 ms frame**, about 1.6 ms of
  copy plus glyphs, carried to the next frame in turn the way `kLedBudget`
  is, and nothing drawn while the send queue holds more than four (the rule
  the LEDs already follow).
- The biggest single changes in this spec, and how many frames they take at
  that budget:

| Change | Pixels | Frames |
|---|---|---|
| apple2 roster redrawn whole | 240 x 132 = 31,680 | 2 |
| pc node list redrawn whole | 288 x 96 = 27,648 | 2 |
| atari events shift up one | 192 x 128 = 24,576 | 2 |
| imsai printout shifts up one | 210 x 84 = 17,640 | 2 |
| a login on the pc (list, events, digits, footer) | about 36,000 | 3 (120 ms) |

- Per-node lamps ("bright on their keystrokes") need a per-line activity
  figure. Keep the last-seen byte counts in the panel's own 11-entry table,
  not in `Session`: every byte in a Session costs twelve.

### One flag, once

The node row's **doing** column is a staff-only column in WHO, and
`panel.cpp`'s own rule is that "the desk must not give away any more than WHO
does". The glass sits on the sysop's desk and the verbs are coarse (never
arguments), so I would show it. That is Rob's call; the stock skins below
assume yes, and dropping it is one fit-rule step (the rows get their handle
back).

## Findings, worst first

### 1. The monitors are one status block in one face, and read identical

- **Every scene.** `skins/stock/*/skin.txt`, the `text` and `lines` pair.
- All five show `name address [uptime] callers today [heap] who`, small
  face, one colour, left-aligned. The plastic changes; the information does
  not. `who` gets whatever rows are left (two in the previews), so a board
  with five on shows two and says nothing about the other three.
- The fix is a different job per scene, specified below, and proposal P1 so
  a list that runs out of rows says so.

### 2. Two screens fail contrast

Measured as WCAG contrast against the painted field's lightest point:

| Scene | Text | Field centre | Ratio | Fix | New ratio |
|---|---|---|---|---|---|
| c64 | `#A89CFF` | `#4436A0` | 3.89 | text `#C4BAFF`, field centre `#3A2E90` | 5.99 (7.33 at the edge) |
| atari | `#B8D0FF` | `#2A64B8` | 3.74 | text `#E8F0FF`, TV blue centre `#1C4A94`, edge `#0E2650` | 7.47 |

Both are one colour in `skin.txt` and one radial in `mkskins_stock.py`. Do
these first whatever else waits: they are wrong today on the glass.

### 3. Lamps that can never light, and legends nobody can read

- **imsai:** 42 lamps painted, 18 live (DISK, INTE, the 16 address lamps).
  PROGRAMMED OUTPUT and DATA, 16 lamps, are dark for ever, as are six status
  lamps.
- **pc, c64, apple2, atari:** two live lamps each.
- Every legend is painted at a third scale (finding in "The glass").
- Fix: the scene specs below give every painted lamp a job or say plainly
  it is dark, and `mkskins_stock.py` gains the 6x12 face in `label()` at 1:1
  (`px=3` on the 3x canvas) for any legend under a live lamp.

### 4. Specs per scene

In the order to build them. Every scene has: a map (one character is one
8 x 16 px cell, so the maps are coarse; the pixel tables are the authority),
the content mock at each area's own column count with a ruler, the art
changes, and a `skin.txt` draft. Proposals used are marked `P1` to `P6`
and defined in their own section.

Map key, all scenes: `N` name, `C` clock, `a` address, `c` callers,
`t` today, `u` uptime, `!` ring, `W` node list, `E` events, `G` graph,
`8` seven-segment digits, `S` meter (or the sysop's lamp on the imsai),
`R` run lamp, `D` drive light, `A` activity lamp, `K` card lamp,
`m` mail, `r` ring, `k` closed (lock); on a modem `M O C I r s` are
MR (run), AA (open), CD (online), RI (ring), RD (rx), SD (tx).

---

#### pc: the waiting-for-caller screen

The PC side of the hobby ran the board on the machine in front of the sysop,
and what that machine showed between calls was a WFC screen: a bar across
the top, the lines and who is on them, the last few events, a status line.
That is the richest thing any of the five monitors can show, and the tower
and keyboard carry the across-the-desk figures.

**Before** (text 46,58 224 x 128, small, 28 x 8):

```
          1         2
 1234567890123456789012345678
+----------------------------+
|The Rusty Antenna           |
|192.168.0.40:6400           |
|up 3d 4h                    |
|Callers 2/11                |
|14 calls today              |
|84K free                    |
| 1) alice 7m                |
| 2) bob 1h                  |
+----------------------------+
```

**After, the scene:**

```
pc, after (1 char = 8x16 px)
      0         1         2         3         4         5
      012345678901234567890123456789012345678901234567890123456789
   0   +----------------------------------------+ +-------------+
  16   |  NNNNNNNNNNNNNNNNNNNNNNNNNNNNNN CCCCCC | |             |
  32   |  NNNNNNNNNNNNNNNNNNNNNNNNNNNNNN CCCCCC | |             |
  48   |  WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW | |             |
  64   |  WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW | |             |
  80   |  WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW | |  88888      |
  96   |  WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW | |  88888      |
 112   |  WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW | |  88888      |
 128   |  WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW | |             |
 144   |  EEEEEEEEEEEEEEEEEEEEEEEGGGGGGGGGGGGGG | |             |
 160   |  EEEEEEEEEEEEEEEEEEEEEEEGGGGGGGGGGGGGG | | R    A    D |
 176   |  EEEEEEEEEEEEEEEEEEEEEEEGGGGGGGGGGGGGG | |             |
 192   |  aaaaaaaaaaaaaaaattttttttttttuuuuuuuuu | |             |
 208   |  aaaaaaaaaaaaaaaattttttttttttuuuuuuuuu | |             |
 224   |                                        | |             |
 240   +----------------------------------------+ |             |
 256    +--------------------------------------+  |             |
 272    |                                  m   |  |             |
 288    |                                  r   |  |             |
 304    +----------------------------------k---+  +-------------+
```

**After, the tube's top half** (small, 36 columns, x 34 to 322). The bar is
painted grey; the header row is painted in the art at tiny 1:1, placed at the
small columns' pixel positions (`Ln` x 34, `Handle` x 66, `Doing` x 202,
`On` right edge at x 322):

```
          1         2         3
 123456789012345678901234567890123456
+------------------------------------+
|The Rusty Antenna              21:47|   bar: grey, ink #10106A
|Ln  Handle           Doing        On|   painted, tiny, #55FFFF
| S] rob              CONFIG       2h|
| 1) alice            CHAT         7m|
| 2* visitor42        FILES        1m|
| 3> carol            MAIL        12m|
| 4) thedarkknight198 FORUMS      51m|
| +2 more                            |   P1
+------------------------------------+
```

**After, the tube's bottom third** (tiny, 48 columns): events in 30 columns
at the left, the traffic sweep in 100 px at the right, a painted grey status
bar under the footer fields:

```
          1         2         3         4
 123456789012345678901234567890123456789012345678
+------------------------------------------------+
|21:47 login bob                 .:|:.  .|||:.   |
|21:40 ring alice               .:|||:..:|||||:. |
|21:38 guest visitor42          :||||||||||||||: |
|                                                |
|192.168.0.40:6400     14 calls today    up 3d 4h|
+------------------------------------------------+
```

| Widget | Box (x, y, w, h) | Face | Columns x rows | Cut |
|---|---|---|---|---|
| `field name` | 34, 24, 232, 16 | small | 29 x 1 | names over 29 of 39 |
| `clock` | 282, 24, 40, 16 | small | 5 | |
| `nodes` | 34, 56, 288, 96 | small | 36 x 6 | nothing at 36 (row is 33); a 7th caller and on go to `+n more` |
| `events` newest first | 34, 158, 180, 36 | tiny | 30 x 3 | nothing (worst 29) |
| `graph traffic` | 222, 158, 100, 36 | | 100 samples = 50 s | |
| `field address` | 34, 202, 126, 12 | tiny | 21 | nothing (worst IPv4:port is 21) |
| `field today` | 166, 202, 90, 12 | tiny | 15 | |
| `field uptime`, right | 262, 202, 60, 12 | tiny | 10 | past `up 999d 23h` |
| `digits online` | 378, 90, 34, 28 | H 28, N 2 | | |
| `lamp run` POWER | 372, 172 d6 | | | |
| `activity` TURBO | 412, 172 d6 | | | |
| `drive pc` HDD | 452, 172 d6 | | | |
| `lamp mail`, `ring`, `closed` | 300 at 276, 292, 308, d5 | | | |

**Art changes** (`skin_pc`):

- Rebalance for the tube. Monitor slab 8,4 336 x 248; bezel 18,12
  316 x 218; tube 26,18 300 x 206, radius 12. The tower narrows to 352,10
  120 x 300; the keyboard to 16,262 320 x 54, keys x 22 to 288.
- The tube is DOS blue, not grey on black: radial `#1428B0` centre to
  `#0A1460` edge, scanlines as now, gloss down from 0.10 to 0.05 so it does
  not wash the top rows.
- Paint the two grey bars (`#A8A8A8`, flat, no gradient): 30,22 292 x 20 and
  30,199 292 x 17. Paint the column header in tiny `#55FFFF` (9.55:1) and a
  1 px rule `#00AAAA` at y 155 from x 34 to 322.
- The tower, top down: one 5.25" bay 360,20 104 x 26 (its lamp at 452,40
  stays painted and unlit, and says nothing); the 3.5" floppy 368,54
  88 x 18; the display window 364,84 96 x 40, `#140606`; tiny legend
  `CALLERS` in `#6D6656` centred at x 412, y 128. **Paint no "MHz"**: it
  would label a caller count as a clock speed. TURBO and RESET buttons at
  y 136; the three lamps at y 172 with tiny legends `POWER`, `TURBO`, `HDD`
  centred under them at y 180; key lock at y 214; vents from y 250.
- The keyboard's three lock lamps become an indicator block at its right end:
  lenses green `#40FF50` at x 300, y 276 / 292 / 308, tiny legends `MAIL`,
  `RING`, `LOCK` at x 308, ink `#6D6656`. Scroll Lock becomes the board being
  locked to callers, which is the one pun here worth keeping.
- **The floppy-label idea is dropped, deliberately.** The 3.5" slot is
  60 x 3 px; a label big enough to read is 80 x 12 at tiny (13 columns, cuts a
  16 character handle), and a changing value on paper is the wrong material.
  The last caller is on the WFC, first row of events.

**Colours on DOS blue** (contrast against `#1020A8`): ink `#FFFFFF` 11.71;
events `#A8A8A8` 4.92. With P3: sysop `#FF9090` 5.38, co-sysop `#FFFF55`
10.97, guest `#B0B0B0` 5.40, caller `#FFFFFF`; login `#5DDC7A` 6.68, guest
`#F0B860` 6.54, logoff `#A8A8A8` 4.92, page and ring `#FF9A60` 5.60. Bar
ink `#10106A` on `#A8A8A8` 6.77.

```
skin 1
panel 480 320
name Beige tower
; the WFC screen: bar, lines, events, traffic, status
field 34 24 232 name colour=#10106A
clock 282 24 colour=#10106A
nodes 34 56 288 96 colour=#FFFFFF tint=yes
events 34 158 180 36 size=tiny colour=#A8A8A8 order=newest tint=yes
graph 222 158 100 36 traffic colour=#5DDC7A
field 34 202 126 address size=tiny colour=#10106A
field 166 202 90 today size=tiny colour=#10106A
field 262 202 60 uptime size=tiny colour=#10106A align=right
; the tower: callers on in the turbo display, POWER, TURBO, HDD
digits 378 90 28 2 online colour=#FF3020 dim=#2A0604
lamp 372 172 6 run colour=#40FF50 halo=5
activity 412 172 6 colour=#FFC020 halo=5
drive 452 172 6 pc halo=5
; the keyboard's lock lamps: MAIL, RING, LOCK
lamp 300 276 5 mail colour=#40FF50 halo=4
lamp 300 292 5 ring colour=#40FF50 halo=4 blink=yes
lamp 300 308 5 closed colour=#40FF50 halo=4
```

At arm's length: the red digits (92 cm at H 28), POWER, the HDD flicker,
and a blinking RING. Lean in and it is a WFC screen.

---

#### atari: the event feed on the television

The TV is the one screen here that should scroll, because that is what a
television attached to a home computer did. Events come in at the bottom.
The TV's side panel gets a channel readout, which late sets had, showing
callers on, and a signal meter, which is the Wi-Fi.

**Before** (text 40,58 176 x 128, small, 22 x 8):

```
          1         2
 1234567890123456789012
+----------------------+
|The Rusty Antenna     |
|192.168.0.40:6400     |
|Callers 2/11          |
|14 calls today        |
|21:40 login alice     |
| 1) alice 7m          |
| 2) bob 1h            |
|                      |
+----------------------+
```

**After, the scene:**

```
atari, after
      0         1         2         3         4         5
      012345678901234567890123456789012345678901234567890123456789
   0   +----------------------------------+
  16   |  NNNNNNNNNNNNNNNNNNNCCCCC        |  +-----------------+
  32   |  NNNNNNNNNNNNNNNNNNNCCCCC        |  |  M O  C I  r s  |
  48   |  EEEEEEEEEEEEEEEEEEEEEEEEE       |+-+-----------------++
  64   |  EEEEEEEEEEEEEEEEEEEEEEEEE       ||                    |
  80   |  EEEEEEEEEEEEEEEEEEEEEEEEE       ||                    |
  96   |  EEEEEEEEEEEEEEEEEEEEEEEEE       ||                    |
 112   |  EEEEEEEEEEEEEEEEEEEEEEEEE 88888 ||                    |
 128   |  EEEEEEEEEEEEEEEEEEEEEEEEE 88888 ||                    |
 144   |  EEEEEEEEEEEEEEEEEEEEEEEEE 88888 ||                    |
 160   |  EEEEEEEEEEEEEEEEEEEEEEEEE  SSS  ||                    |
 176   |  EaaaaaaaaaaaaaaaaaaaaaaaE  SSS  ||   R D              |
 192   |                             SSS  |+--------------------+
 208   +----------------------------------+
 224            +----------------------------------------------A-+
 240            |                                                |
 256            |                                                |
 272            |                                                |
 288            |                                                |
 304            +------------------------------------------------+
```

**After, the screen** (small, 24 columns, x 34 to 226; the address under it
in tiny):

```
          1         2
 123456789012345678901234
+------------------------+
|The Rusty Antenna  21:47|
|21:02 login alice       |
|21:09 guest visitor42   |
|21:15 login carol       |
|21:20 login thedarkknigh|   handle cut at 12 (login) or 11 (logoff)
|21:31 logoff zed        |
|21:38 page carol        |
|21:40 ring alice        |
|21:47 login bob         |   newest at the bottom
+------------------------+
          1         2
 12345678901234567890123456789
+-----------------------------+
|      192.168.0.40:6400      |   tiny, centred
+-----------------------------+
```

| Widget | Box | Face | Columns x rows | Cut |
|---|---|---|---|---|
| `field name` | 34, 30, 144, 16 | small | 18 | names over 18 |
| `clock` | 184, 30, 40, 16 | small | 5 | |
| `events` oldest first | 34, 50, 192, 128 | small | 24 x 8 | handles over 12 (login) or 11 (logoff) |
| `field address`, centred | 42, 180, 176, 12 | tiny | 29 | nothing |
| `digits online` | 247, 122, 30, 24 | H 24, N 2 | | |
| `meter rssi` up | 254, 160, 12, 34 | | | |
| `lamp run` PWR | 330, 176 d5 | | | |
| `drive 1541` BUSY | 350, 176 d5 | | | |
| `activity` | 460, 233 d4 | | | |
| modem, six lamps | x 336 to 436 step 20, y 36, d5 | | | |

The cut at 12 is the price of small on a TV. Tiny would fit 32 columns and
cut nothing, and would also make the one scene built for reading events
unreadable from the chair. Small stays.

The round corners decide the margins: radius 20, so the first row at 6 px
below the glass's top edge needs 5.7 px of inset and the last at 4 px above
the bottom needs 8. Every box above clears both.

**Art changes** (`skin_atari`):

- Screen 24,24 208 x 172, radius 20; TV blue radial `#1C4A94` centre to
  `#0E2650` edge (finding 2). Scanlines as now.
- Side panel 238,22 44 x 180. Knobs r 12 at (260,50) and (260,92). A channel
  window `#140606` at 244,118 32 x 32, digits inside. A recessed dark slot
  for the meter at 252,158 16 x 38, painted, so `background=none` shows the
  slot where the bar is not. Grille below it dropped.
- A modem on top of the drive (below, "The modem").
- PWR on the drive stops being painted lit: it is `run`.
- The gold keys have no lamps and get none. There are no "console lamps" on
  this machine beyond its power lamp, which stays the activity lamp.

```
skin 1
panel 480 320
name Cream and wood
; the television: the board's name and the events, newest at the bottom
field 34 30 144 name colour=#E8F0FF shadow=#0A1A38
clock 184 30 colour=#E8F0FF shadow=#0A1A38
events 34 50 192 128 order=oldest colour=#E8F0FF shadow=#0A1A38
field 42 180 176 address size=tiny colour=#E8F0FF align=centre
; the channel readout is callers on, the tuning meter is the signal
digits 247 122 24 2 online colour=#FF3020 dim=#2A0804
meter 254 160 12 34 rssi colour=#5DDC7A dir=up
; the drive: PWR, BUSY; the computer's power lamp for traffic
lamp 330 176 5 run colour=#FF2A18 halo=5
drive 350 176 5 1541 halo=5
activity 460 233 4 colour=#FF3A20 halo=4
; the modem on the drive
lamp 336 36 5 run colour=#FF3020 halo=4
lamp 356 36 5 open colour=#FF3020 halo=4
lamp 376 36 5 online colour=#FF3020 halo=4
lamp 396 36 5 ring colour=#FF3020 halo=4 blink=yes
lamp 416 36 5 rx colour=#FF3020 halo=4
lamp 436 36 5 tx colour=#FF3020 halo=4
```

No shadow on the tiny address: a 1 px shadow under a 6 px face smears.

---

#### The modem (atari, c64, apple2)

A generic external modem: a low charcoal box with a smoked front strip and a
row of six lamps, no name, no badge. It appears in three scenes because it is
the BBS's own device; putting it on the desk is what turns "a computer" into
"a board". Its lamps were designed to be read across a room, and each maps
to its original meaning:

| Legend | Original meaning | State | Proposal |
|---|---|---|---|
| MR | modem ready | `run` | |
| AA | auto answer on | `open` (not closed) | P4 |
| CD | carrier detect | `online` (anyone on) | P4 |
| RI | ring indicator | `ring`, blinking | P2 |
| RD | receive data | `rx` | |
| SD | send data | `tx` | |

- Box: charcoal `#3A3A3E` top to `#26262A`, a smoked strip `#141416` behind
  the lamps, feet. Lenses red `#FF2410`, d 5, halo 4 (13 px boxes).
- Legends: tiny at 1:1, `#C8C8C0`, two letters (12 px) centred under each
  lamp. Pitch 17 to 20 px, so legends never touch.
- Placement per scene: atari 312,20 148 x 38 on the drive's top (lamps
  y 36, legends y 44); c64 8,258 128 x 46 on the desk left of the breadbin
  (lamps y 278, legends y 288); apple2 352,256 120 x 44 on the desk under the
  drives' stand (lamps y 274, legends y 283).
- Without P4, MR, RI, RD and SD still work and AA and CD are painted dark.
  The modem is still worth it; P4 makes it complete.

---

#### c64: the screen lists who is on

The breadbin's screen does one thing: who is on. Its drive and the modem
say the rest.

**Before** (text 48,52 188 x 112, small, 23 x 7):

```
          1         2
 12345678901234567890123
+-----------------------+
|The Rusty Antenna      |
|192.168.0.40:6400      |
|Callers 2/11           |
|14 calls today         |
| 1) alice 7m           |
| 2) bob 1h             |
|                       |
+-----------------------+
```

**After, the scene:**

```
c64, after
      0         1         2         3         4         5
      012345678901234567890123456789012345678901234567890123456789
   0  +-------------------------------------+
  16  |   NNNNNNNNNNNNNNNNNNNNNNNN CCCCCC   |
  32  |   aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa   |
  48  |   !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!   |
  64  |   WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW   |+------------------+
  80  |   WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW   ||                  |
  96  |   WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW   ||                  |
 112  |   WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW   ||                  |
 128  |   WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW   ||                  |
 144  |   ccccccccccccccttttttttttttttttt   ||                  |
 160  |   ccccccccccccccttttttttttttttttt   ||  R D             |
 176  |                                     |+------------------+
 192  |                                     |
 208  +-------------------------------------+
 224                   +--------------------------------------A--+
 240                   |                                         |
 256   +--------------+|                                         |
 272   | M  O C I r  s||                                         |
 288   +--------------+|                                         |
 304                   +-----------------------------------------+
```

**After, the field** (small, 30 columns x 9 rows, x 36 to 276). The ring row
is blank until somebody rings, so it doubles as the gap between the header
and the list:

```
          1         2         3
 123456789012345678901234567890
+------------------------------+
|The Rusty Antenna        21:47|
|192.168.0.40:6400             |
|bob is ringing                |   yellow #F0E070, blank otherwise
| S] rob             CONFIG  2h|   handle 15, doing 6: the row fits 30
| 1) alice           CHAT    7m|
| 2* visitor42       FILES   1m|
| 3> carol           MAIL   12m|
| +3 more                      |   P1
|Callers 7/11    14 calls today|
+------------------------------+
```

| Widget | Box | Face | Columns x rows | Cut |
|---|---|---|---|---|
| `field name` | 36, 30, 184, 16 | small | 23 | names over 23 |
| `clock` | 236, 30, 40, 16 | small | 5 | |
| `field address` | 36, 46, 240, 16 | small | 30 | nothing |
| `field ring` | 36, 62, 240, 16 | small | 30 | handles over 16 (`%.24s is ringing` fits 30 at 16) |
| `nodes` | 36, 78, 240, 80 | small | 30 x 5 | handles over 15, doing over 6; a 6th caller to `+n more` |
| `field callers` | 36, 158, 104, 16 | small | 13 | |
| `field today`, right | 148, 158, 128, 16 | small | 16 | past 999 calls |
| `lamp run` PWR | 336, 170 d6 | | | |
| `drive 1541` DRV | 354, 170 d6 | | | |
| `activity` | 452, 235 d5 | | | |
| modem | x 30 to 120 step 18, y 278, d5 | | | |

**Art changes** (`skin_c64`):

- Monitor slab 6,4 300 x 218; border glass 18,14 276 x 176, radius 10; field
  32,26 248 x 152, radial `#3A2E90` centre to `#2E2472` edge (finding 2).
- The drive moves right and narrows: 312,64 160 x 128, face 320,104
  144 x 80. PWR at (336,170) stops being painted lit; tiny legends `PWR` and
  `DRV` at 1:1.
- The modem on the desk at 8,258.
- No painted `READY.`: a static prompt over live rows reads as a hung
  machine.

```
skin 1
panel 480 320
name Breadbin and drive
; the monitor: who is on, in the machine's light blue
field 36 30 184 name colour=#C4BAFF
clock 236 30 colour=#C4BAFF
field 36 46 240 address colour=#C4BAFF
field 36 62 240 ring colour=#F0E070
nodes 36 78 240 80 colour=#C4BAFF
field 36 158 104 callers colour=#C4BAFF
field 148 158 128 today colour=#C4BAFF align=right
; the drive: PWR is the board up, DRV its storage
lamp 336 170 6 run colour=#40FF50 halo=5
drive 354 170 6 1541 halo=5
activity 452 235 5 colour=#FF3A20 halo=4
; the modem
lamp 30 278 5 run colour=#FF3020 halo=4
lamp 48 278 5 open colour=#FF3020 halo=4
lamp 66 278 5 online colour=#FF3020 halo=4
lamp 84 278 5 ring colour=#FF3020 halo=4 blink=yes
lamp 102 278 5 rx colour=#FF3020 halo=4
lamp 120 278 5 tx colour=#FF3020 halo=4
```

---

#### apple2: the whole switchboard, in 40 columns

The green screen shows every line, taken or waiting, in the machine's own 40
columns. It is the one roster on the desk. Tiny is justified here and only
here: the shape of the list (bright rows on, dim rows waiting) reads from the
chair; the names are for leaning in.

**Before** (text 88,54 186 x 112, small, 23 x 7): the same seven words as
the c64.

**After, the scene:**

```
apple2, after
      0         1         2         3         4         5
      012345678901234567890123456789012345678901234567890123456789
   0     +-------------------------------------+
  16     |   NNNNNNNNNNNNNNaaaaaaaaaaaaaaaaa   |
  32     |   WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW   |
  48     |   WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW   |
  64     |   WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW   |  +-------------+
  80     |   WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW   |  |             |
  96     |   WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW   |  |             |
 112     |   WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW   |  | D           |
 128     |   WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW   |  +-------------+
 144     |   WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW   |  +-------------+
 160     |   WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW   |  |             |
 176     |                                     |  |             |
 192    ++-------------------------------------++ | K           |
 208    |                                       | +-------------+
 224    |                                       |
 240    |                                       |
 256    |                                       | +-------------+
 272    |                                       | | M O C I  r s|
 288    | A                                     | +-------------+
 304    +---------------------------------------+
```

**After, the screen** (tiny, 40 columns x 12 rows, x 60 to 300):

```
          1         2         3         4
 1234567890123456789012345678901234567890
+----------------------------------------+
|The Rusty Antenna      192.168.0.40:6400|
| S] rob              CONFIG           2h|   #4CFF7A, 13.59:1
| 1) alice            CHAT             7m|
| 2* visitor42        FILES            1m|
| 3> carol            MAIL            12m|
| 4) thedarkknight198 FORUMS          51m|
| 5) bob              WHO              3m|
| 6) zed              CHAT             1h|
| 7  waiting                             |   #2A9A48 (P3 dim), 4.97:1
| 8  waiting                             |
| 9  waiting                             |
|10  waiting                             |
+----------------------------------------+
```

| Widget | Box | Face | Columns x rows | Cut |
|---|---|---|---|---|
| `field name` | 60, 26, 108, 12 | tiny | 18 | names over 18 |
| `field address`, right | 174, 26, 126, 12 | tiny | 21 | nothing |
| `nodes free=yes` | 60, 40, 240, 132 | tiny | 40 x 11 | nothing (row 33; all 10 lines plus S) |
| `drive disk2` top IN USE | 368, 126 d5 | | | |
| `lamp card` bottom IN USE | 368, 206 d5 | | | |
| `activity` keyboard | 36, 290 d4 | | | |
| modem | x 372 to 457 step 17, y 274, d5 | | | |

No clock on this one, on purpose: the 11 rows are the point, and the header
row holds name and address. The second drive's IN USE lamp lit steadily reads
as "there is a disk in drive 2": a card is mounted. Not perfect, and better
than a lamp that never lights.

**Art changes** (`skin_apple2`): monitor slab 30,4 300 x 196, bezel
38,12 284 x 178, screen 48,20 264 x 160 radius 14 (the 40 columns need
240 px plus margins; today's screen is 210). Drives and stand unchanged. The
modem on the desk at 352,256.

```
skin 1
panel 480 320
name Beige lid and drives
; the green screen: every line, taken or waiting
field 60 26 108 name size=tiny colour=#4CFF7A
field 174 26 126 address size=tiny colour=#4CFF7A align=right
nodes 60 40 240 132 size=tiny colour=#4CFF7A free=yes dim=#2A9A48
; the drives: the top one's IN USE is storage, the bottom one's a card in
drive 368 126 5 disk2 halo=5
lamp 368 206 5 card colour=#FF2A18 halo=5
activity 36 290 4 colour=#9CFF6A halo=4
; the modem
lamp 372 274 5 run colour=#FF3020 halo=4
lamp 389 274 5 open colour=#FF3020 halo=4
lamp 406 274 5 online colour=#FF3020 halo=4
lamp 423 274 5 ring colour=#FF3020 halo=4 blink=yes
lamp 440 274 5 rx colour=#FF3020 halo=4
lamp 457 274 5 tx colour=#FF3020 halo=4
```

---

#### imsai: a front panel whose lamps mean something

The front panel is the scene where the device itself carries almost
everything. Today its lamps are a light show: the strip's effect on the
address row, DISK, INTE, and 22 lamps painted dark for ever. After, every lit
lamp is a fact about the board, and the events come out on green-bar paper.

**Before:** readout 26,22 224 x 64 small (28 x 4: name, address, callers,
one caller); DISK = drive, INTE = activity, ADDRESS = the 16-LED strip;
PROGRAMMED OUTPUT, DATA and six status lamps never light.

**After, the scene:**

```
imsai, after
      0         1         2         3         4         5
      012345678901234567890123456789012345678901234567890123456789
   0  +--EEEEEEEEEEEEEEEEEEEEEEEEEEE-----------------------------+
  16  |  EEEEEEEEEEEEEEEEEEEEEEEEEEE   NNNNNNNNNNNNNNNNNNN CCCCC |
  32  |  EEEEEEEEEEEEEEEEEEEEEEEEEEE   aaaaaaaaaaaaaaaaaaaaaaaaa |
  48  |  EEEEEEEEEEEEEEEEEEEEEEEEEEE   ccccccccccccccccccccccccc |
  64  |  EEEEEEEEEEEEEEEEEEEEEEEEEEE    R  W   I  H  r  s   D  M |
  80  |  EEEEEEEEEEEEEEEEEEEEEEEEEEE                             |
  96  |                                                          |
 112  |                                                          |
 128  |  p   p  p  p    p  p  p  p    b   b  b  b  b   b  b  b   |
 144  |                                                          |
 160  |                                                          |
 176  |                                                          |
 192  |  S   .  .  .  .   .  0  9    8  7  6  5   4  3  2  1     |
 208  |                                                          |
 224  |                                                          |
 240  |                                                          |
 256  |                                                          |
 272  |                                                          |
 288  |                                                          |
 304  +----------------------------------------------------------+
```

`E` paper, `p` PROGRAMMED OUTPUT (the strip), `b` DATA, the address row by
line number (`0` is line 10, `.` dark).

**The printout** (tiny, 35 columns x 7 rows, x 28 to 238, newest at the
bottom like a real printer):

```
          1         2         3
 12345678901234567890123456789012345
+-----------------------------------+
|21:15 login carol                  |
|21:20 login thedarkknight198       |
|21:31 logoff zed                   |
|21:38 page carol                   |
|21:40 ring alice                   |
|21:44 logoff visitor42             |
|21:47 login bob                    |
+-----------------------------------+
```

**The readout** (small, 24 columns x 3 rows, x 270 to 464):

```
          1         2
 123456789012345678901234
+------------------------+
|The Rusty Antenna  21:47|
|192.168.0.40:6400       |
|Callers 7/11            |
+------------------------+
```

**The lamps, row by row:**

- **Status**, one row of eight at y 78, x 277 + 26k, d 8, red, legends tiny
  1:1 (four letters, 24 px, at a 26 px pitch):

| Legend | State | Why that legend |
|---|---|---|
| RUN | `run` | the machine is running |
| WAIT | `closed` | not taking calls: waiting |
| INT | `ring`, blinking | an interrupt: somebody wants the operator |
| HLTA | `error`, blinking | halted on a storage fault |
| INP | `rx` | input |
| OUT | `tx` | output |
| DISK | `drive 1541` | as today |
| MAIL | `mail` | no period legend fits; say the thing |

- **PROGRAMMED OUTPUT** (8, y 136): **the strip**, `strip 8`, the lights
  plugin's pixels 1 to 8. On the real machine this is the port a program
  writes to show what it likes, which is exactly what the strip's effect is.
  The plugin's strip is ten pixels; pixels 9 and 10 do not show here, and
  that is said in the skin's comment.
- **DATA** (8, y 136): D2 `staff` (legend `OPS`), D1 `listed` (`DIR`), D0
  `card` (`CARD`). D7 to D3 dark, as a real panel's high data bits usually
  are. Legends at x 392, 418, 444.
- **ADDRESS** (16, y 196): **the lines.** Line n on bit n-1, so line 1 is A0
  at the right and line 10 is A9: node1 x 430, node2 404, node3 378, node4
  352, node5 326, node6 300, node7 274, node8 248, node9 212, node10 186. A15
  (x 30) is the sysop's line (P5); A14 to A10 are dark on every board,
  because the socket cap holds every chip to ten caller lines. Legends become
  line numbers, `10` to `1` under A9 to A0 and `S` under A15, and the row
  label `ADDRESS` becomes `LINES`: costume is not worth a mystery.
- The power rocker's lamp stays painted lit. `activity` moves off INTE; INP
  and OUT do its job, split.

Live lamps: 18 of 42 before, 29 after (30 with P5), and every one of them
means something.

**Art changes** (`skin_imsai`):

- The paper: green-bar hanging over the panel's top left from above the
  glass, 12,0 244 x 104, a slight shadow on the panel, a torn bottom edge
  around y 104. Tractor strips 12 px each side with holes (x 12 to 24,
  244 to 256). Paper `#EEF0E4`, bands `#CDE6C4` three rows (36 px) deep
  aligned to the text rows from y 6. Ink `#2A2A30`: 12.37:1 on white,
  10.67:1 on green. The 6x12 face is a fair dot-matrix.
- The readout moves right and shrinks: window 264,12 206 x 54.
- The first silkscreen rule moves from y 100 to y 106.
- Relabel the status row and the DATA and ADDRESS legends as above, all tiny
  at 1:1.

```
skin 1
panel 480 320
name Front panel
; the printout: events, newest at the bottom
events 28 6 210 84 size=tiny colour=#2A2A30 order=oldest
; the readout
field 270 16 144 name colour=#6CFFE0
clock 424 16 colour=#6CFFE0
field 270 32 194 address colour=#6CFFE0
field 270 48 194 callers colour=#6CFFE0
; status: RUN WAIT INT HLTA INP OUT DISK MAIL
lamp 277 78 8 run colour=#FF2410 halo=5
lamp 303 78 8 closed colour=#FF2410 halo=5
lamp 329 78 8 ring colour=#FF2410 halo=5 blink=yes
lamp 355 78 8 error colour=#FF2410 halo=5 blink=yes
lamp 381 78 8 rx colour=#FF2410 halo=5
lamp 407 78 8 tx colour=#FF2410 halo=5
drive 433 78 8 1541 halo=5
lamp 459 78 8 mail colour=#FF2410 halo=5
; programmed output: the lights plugin's strip, pixels 1 to 8
strip 8
led 1 30 136 8 halo=5
led 2 56 136 8 halo=5
led 3 82 136 8 halo=5
led 4 108 136 8 halo=5
led 5 144 136 8 halo=5
led 6 170 136 8 halo=5
led 7 196 136 8 halo=5
led 8 222 136 8 halo=5
; data: OPS DIR CARD on D2 D1 D0
lamp 392 136 8 staff colour=#FF2410 halo=5
lamp 418 136 8 listed colour=#FF2410 halo=5
lamp 444 136 8 card colour=#FF2410 halo=5
; the lines, line n on address bit n-1; the sysop's on A15
lamp 430 196 8 node1 colour=#FF2410 halo=5
lamp 404 196 8 node2 colour=#FF2410 halo=5
lamp 378 196 8 node3 colour=#FF2410 halo=5
lamp 352 196 8 node4 colour=#FF2410 halo=5
lamp 326 196 8 node5 colour=#FF2410 halo=5
lamp 300 196 8 node6 colour=#FF2410 halo=5
lamp 274 196 8 node7 colour=#FF2410 halo=5
lamp 248 196 8 node8 colour=#FF2410 halo=5
lamp 212 196 8 node9 colour=#FF2410 halo=5
lamp 186 196 8 node10 colour=#FF2410 halo=5
lamp 30 196 8 nodeS colour=#FF2410 halo=5
```

About 2.2 KB, inside the 4,096 byte limit. 40 lamp boxes of 18 x 18 are
12,960 px, a fifth of the 65,536 px cap.

### 5. Checked for every scene

- Every box inside 480 x 320, no two overlapping: checked by script, all
  five clean.
- Lamp box totals: pc 1,275 px, c64 1,695, apple2 1,608, atari 1,608,
  imsai 12,960.
- One assumption to confirm when `digits` lands: I sized a digit cell as
  `ceil(0.6 H)` px (the art's `segs()` draws 0.55 H plus a gap), so N = 2 is
  34 px wide at H 28 and 30 px at H 24. If the widget measures wider, keep
  the left edge and re-check the box against its neighbours; the atari's
  window has 2 px spare.
- Lamp states are named more than once (`run` on a drive and on a modem).
  The reader must allow a state on several lamps; the "each directive once"
  rule is for `skin`, `panel`, `name`, `drive`, `activity`, `strip` and
  `clock`.

## Proposals: what the vocabulary lacks

Separate from the specs above. Each says what breaks without it.

- **P1 `+n more` on `nodes` (essential).** When callers outnumber rows, the
  last row reads ` +3 more`. Without it the widget shows the first rows and
  drops the rest silently, which is a partial list presented as whole: the
  pc drops a 7th caller, the c64 a 6th. Cost: one row format.
- **P2 `blink=yes` on `lamp` (essential for ring and error).** 2 Hz, the
  status layout's bell rate. A steady lamp in a row of steady lamps does not
  call anybody, and ring is the one state whose whole purpose is to call
  somebody. Cost: a lamp redraw at 2 Hz, 13 to 18 px square.
- **P3 `dim=#RRGGBB` on `nodes` for waiting rows, and `tint=yes` on `nodes`
  and `events`.** `dim` is what makes the apple2 roster readable as a shape
  from the chair; without it waiting rows are as bright as callers. `tint`
  colours node and mark by rank and the kind word by kind, in the lifted
  palette under the pc spec; only the pc uses it. Without either the scenes
  still work, flatter.
- **P4 lamp states `online` (anyone on) and `open` (the inverse of
  `closed`).** The modem's CD and AA. Without them those two lamps are dark,
  and CD is the modem's most important lamp.
- **P5 lamp state `nodeS`, the sysop's line.** Lit while the sysop is on and
  visible, never while hidden or lurking (the `shown()` rule). Only the imsai
  uses it. Without it A15 is dark.
- **P6 `size=tiny` on `clock`.** Not needed by these five (`field ... clock`
  covers it) but a custom skin will reach for it, and the directive already
  takes `small` and `big`.

Considered and not proposed:

- **A directory-listing style for `nodes` on the c64** (`"alice" PRG`, the
  block count as minutes on, `8 LINES FREE.`). Charming, and 16 characters is
  exactly a disk filename. It needs a per-row template in the grammar, which
  is a second list language for one skin. Not now.
- **Bits of a value as lamp states** (`online.0` to `online.7` on the DATA
  row). A number in binary on a front panel is authentic and redundant: the
  address row already shows who is on. The DATA row carries staff, listed and
  card instead, which nothing else on that scene shows.
- **Heap and card meters.** None of the five machines has a natural home for
  one, and a meter bolted to a TV cabinet reads as bolted on. They stay in
  the vocabulary for custom skins. The atari's meter is signal, because a TV
  had one.

## What stays as it is

- **The five scenes and their painting.** Three times the size, Lanczos
  down, unlit lenses brought up by screen blending: this is why they look
  like objects rather than icons, and nothing here changes the method.
- **Every existing drive light and its style**: the pc's HDD flicker, the
  c64's and atari's 1541 red with its error blink, the apple2's Disk II with
  the motor running on. Each is the right lamp in the right style already.
- **The activity lamps on the c64, atari and apple2**, on each computer's
  own power lamp. On the pc it stays on TURBO. On the imsai it moves to INP
  and OUT, split.
- **The apple2's drives and stand, the imsai's paddles, key and power
  rocker, every keyboard.** Decoration that is doing its job.
- **`text` and `lines` stay in the grammar** for custom skins and for
  anyone's skin already on a card; only the stock five move to widgets.
- **No names, badges or logos**, the modem included.
- **The clock** stays on four scenes; the apple2 gives it up for its roster
  on purpose.

## Implementation order

Cheapest and most visible first.

- **Contrast on the c64 and atari** (finding 2): two colours in each
  `skin.txt` and one radial each in `mkskins_stock.py`. Wrong on the glass
  today; needs no new widget.
- **The 6x12 face in `mkskins_stock.py`'s `label()`** at 1:1: every scene's
  legends need it, and it is the art tool, not the board.
- **P1 and P2** in the widget code: a row format and a 2 Hz lamp. Both small,
  and without them the specs above either lie by omission or ring silently.
- **The pc scene**, mocked up first (below), then built.
- **The atari scene** with the modem, which is its own `lamp` lines; P4
  completes it.
- **P4**, then **the c64 and apple2 scenes**, with **P3** for the apple2's
  dim rows and the pc's tint.
- **The imsai scene** last: the most new art (paper, relabelled legends,
  the readout moved), and **P5** for its sysop lamp.

## Which scene to mock up first: pc

- **It uses every widget in the vocabulary in one frame**: field, clock,
  nodes, events, graph, digits, lamp, drive, activity. The first render
  tests the renderer as much as the design, and a width or budget mistake
  shows here before it shows anywhere else.
- **It answers both halves of Rob's sentence at once**: the monitor
  fleshed out (a WFC screen with lines, events and traffic) and the devices
  carrying information (callers on in the turbo display, the keyboard's
  lock lamps saying MAIL, RING and LOCK).
- **It needs the fewest proposals.** Without P1 to P6 it still renders
  whole; P1 and P3 improve it, P2 makes RING blink.
- **It is the colour screen**, so it is where the colour rules get tested
  against real RGB565 on real glass, and every other scene borrows from
  what that shows.
- The imsai is the more striking picture and the stronger "devices carry the
  data" argument, and it is the wrong first mock: it needs the most new art
  and two proposals before it is itself.
