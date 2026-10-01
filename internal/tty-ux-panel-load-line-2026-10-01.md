# The panel's load line and drive icon, 1.2.1

tty-ux, 2026-10-01. Every glass: the 1.47" stick (172 x 320), the 2"
(240 x 320), the 4.3B (400 x 240 logical, doubled to 800 x 480), the
Makerfabs (480 x 320) and the Guition (480 x 480).

Read against `release-prep/wt-g4848`, branch board-g4848, head b014cba.
Line numbers below are that tree's.

Rob's brief, as the coordinator relayed it and as specified here:

- The moving dot shows the current load by its colour.
- The line it travels is coloured by history: each stretch keeps the colour
  of the load when the dot passed over it.
- The drive light becomes an HDD icon in the icon row, beside the SD glyph,
  with a tiny light: amber for the card, cool white for flash, dark at idle,
  red for a storage error, in the drive light's styles.
- The ten line lights at the foot are unchanged; the drive lamp leaves that
  row.
- Nothing else goes. The system cells (heap, temperature, card, peak,
  uptime, slow), the traffic sweep, the hourglass glyph and every other
  figure stay. The load line is added to the dot line, not traded for a
  stat.

## The verdict

Good, and cheap. The dot line is the one moving thing on every glass and it
currently says only "alive"; giving it the loop's load costs no new
measurement (the core already times every pass), no extra band per frame
(the trail is painted inside the rectangle the dot already sends), no RAM
for history (the framebuffer is the record) and 8 bytes of core state on
display boards only. The drive lamp move is the visible fix: on the 4.3B
and the G4848 it is a 20 x 12 or 24 x 16 slab that idles at 24/255 of 10%,
which glass renders as a dead brown LED, sitting where the eye expects an
eleventh line light. It becomes an 11 x 9 glyph with a 3 x 3 window that is
black at rest, which reads as "lamp off" instead of "lamp broken". Two
things need care: a 1 px rail is too thin to carry colour at 0.10 to 0.15
mm a pixel, so it goes to 2 px on every glass but the 4.3B; and the dot
currently stops dead under an animated strip, which would freeze the load
line, so it may stand aside one frame in two, never every frame.

## Findings, worst first

### 1. The drive lamp reads as a dead LED, and as part of the line lights

- Where: `panel.cpp:1970-1984` draws `g_layout.drive` with `drawLed(...,
  gleam)` from `lights::panelDrive`. Boxes: tall `panel_gfx.h:965`
  `R(4, 219, 20, 12)`; square `panel_gfx.h:1318` `R(6, 448, 24, 16)`. The
  stick, the 2" and the Makerfabs have no lamp on glass today.
- Why it fails: `drawDrive` (`lights.cpp:569`) idles at amber level 24 of
  255, shaded by `drive_bright` (10% as shipped). The 1.2.1 floor in
  `drawLed` (`panel_gfx.h:689-699`) lifts that to 40 in the top channel:
  amber at 40 is rgb(40, 20, 0), brown. Beside ten bright segments it is the
  one that looks failed, and its shape matches theirs, so it reads as line
  zero.
- Width: all, on the two glasses that draw it.

Now, G4848 foot, y 448, one column = 8 px:

```
      0         1         2         3         4         5
      012345678901234567890123456789012345678901234567890123456789
448   ddd   #### #### oooo  oooo #### oooo  oooo oooo  oooo oooo     lamp 6..29 brown, bar 48..473
```

Then:

```
      0         1         2         3         4         5
      012345678901234567890123456789012345678901234567890123456789
448   ##### ##### ooooo ooooo ##### ooooo ooooo ooooo ooooo ooooo     bar 7..472, no lamp
```

Rule:

- Delete `L.drive` from both layouts (leave the field, empty, so the
  `refreshLeds` block at `:1972` is skipped by its own `empty()` test, or
  delete the block; either is one edit).
- The bar takes the row the foot rule spans, so its left edge lines up with
  the rule and the node board instead of starting at a hole:
  - square: `L.leds = R(4, 448, 472, 16)`. `segAt`: segments 43 x 16, 4 px
    apart, first at x 7, last ends x 472. Was 39 wide from x 48.
  - tall: `L.leds = R(4, 219, 392, 12)`. Segments 35 x 12, first at x 7,
    last ends x 392. Was 32 wide from x 40.
- The ten lights' colours, effects and meaning do not change. Only the box
  grows, so the bar is centred on the glass rather than pushed right of a
  missing part.
- `bigLedAt` is not used for the square's bar (`ledBar` is true), so no
  change there. The Makerfabs, the 2" and the stick keep their feet exactly.

4.3B, y 219, the same rule:

```
      0         1         2         3         4
      01234567890123456789012345678901234567890123456789
now   dd   ##### #### oooo #### oooo oooo oooo oooo oooo     lamp 4..23, bar 40..395
then  #### #### #### oooo #### oooo oooo oooo oooo oooo      bar 7..392
```

### 2. The 1 px rail cannot carry four colours

- Where: `L.track` is `R(0, 42, w, 1)` (`panel_gfx.h:883`, `:1281`,
  `:1342`), `R(0, 54, w, 1)` on the tall glass (`:946`).
- Measured pitch, from each glass's diagonal and pixel count:

| Glass | Logical | mm a px on glass | 1 px rail | 2 px rail |
|---|---|---|---|---|
| stick 1.47" | 172 x 320 | 0.103 | 0.10 mm | 0.21 mm |
| 2" | 240 x 320 | 0.127 | 0.13 mm | 0.25 mm |
| 4.3B 4.3" | 400 x 240 (x2) | 0.234 logical | 0.23 mm | 0.47 mm |
| Makerfabs 3.5" | 480 x 320 | 0.154 | 0.15 mm | 0.31 mm |
| Guition 4.0" | 480 x 480 | 0.150 | 0.15 mm | 0.30 mm |

- A 0.10 to 0.15 mm line shows a hue only as a tint of grey; green and
  yellow at that width are indistinguishable at arm's length.
- Rule: the rail is 2 px on every glass except the 4.3B, which stays 1
  logical px because its bounce buffer already doubles it to 2 physical.
  Target: 0.2 to 0.3 mm of line everywhere.
  - small and big: `L.track = R(0, 42, w, 2)`. Rows 42 and 43. Row 43 is
    air today (nothing until the heading at 48), so nothing moves.
  - tall: unchanged, `R(0, 54, w, 1)`.
- Put the height in `L.track.h` and read it in `dotBox`, `dotErase` and
  `dotDraw`, not a second constant.

### 3. The dot freezes under an animated strip, which would freeze the line

- Where: `dotStep` (`panel.cpp:2006`) returns when `refreshLeds` queued
  more than one LED, and on the big glass while the queue holds 3.
- With rainbow, scanner, boing or blinken on the strip, every frame queues
  all ten, so the dot never moves. Today that only costs the "alive" cue.
  With the load line it costs the whole reading: the line stops recording.
- Rule: the dot may stand aside on at most one frame in two. A frame it
  stood aside, the next frame it moves regardless of `leds` (small) or of
  the queue depth (big, still never above 4 queued). Cost: one rectangle of
  28 to 32 px every other frame under a busy strip.
- Consequence, stated rather than hidden: under a busy strip the dot runs
  at half speed, so a lap covers twice the time. The line still reads left
  to right as older to newer; it is a time line, not a clock.

### 4. The icon row is full on the stick and the 2" with every glyph on

- Where: `pack()` (`panel_gfx.h:504`) has no limit. Strip caps: 110 small
  and tall, 122 wide portrait (`:896`), 120 big (`:1282`, `:1343`).
- Arithmetic with the new 11 px icon, every glyph lit at once:

| Glass | Glyphs | Ink | Gaps at 3 | Total | Gaps at 2 | Total | Cap |
|---|---|---|---|---|---|---|---|
| stick, tall (no camera) | 10 | 91 | 27 | 118 | 18 | 109 | 110 |
| 2" (camera) | 11 | 102 | 30 | 132 | 20 | 122 | 122 |
| Makerfabs, Guition | 10 | 91 | 27 | 118 | n/a | 118 | 120 |
| typical: SD, HDD, tower, staff | 4 | 37 | 9 | 46 | | | |

- Rule: `pack()` packs at `kGlyphGap` 3. If `p.end` passes the strip's
  right edge (`glyphs.x + glyphs.w`), it packs again at 2. Decided from the
  status alone, so equal statuses still draw equal pixels and the
  `statusWords` key still holds.
- Every glass then fits every glyph with 0 or 1 px to spare. The big glass
  never needs the second pack today; a big glass with a camera would need
  its cap raised to 126 (the band word starts at 134), not before.

## The HDD icon

### Shape

11 x 9, a 3.5" drive's slab seen from the front: a top face, a seam, a
front face with the light at its right. Same silhouette style as the other
glyphs (`panel_gfx.h:443-465`), 2 px of body at its thinnest.

Pixel scale, one character = 1 px, band y 24, glyphs at x 4, gap 3. `#` is
the SD glyph (card in), `H` the drive body, `*` its light lit, `t` the
tower:

```
  x  000000000011111111112222222222333333333344444444
     012345678901234567890123456789012345678901234567
 24  ................................................
 25  ................................................
 26  ....#######.....................................
 27  ....########.....HHHHHHHHH........t.............
 28  ....##.#.#.##...HHHHHHHHHHH.....t.t.t...........
 29  ....##.#.#.##...HHHHHHHHHHH....t..t..t..........
 30  ....##.#.#.##...H.........H....t..t..t..........
 31  ....#########...HHHHHHHHHHH.....t.t.t...........
 32  ....#########...HHHHHHH***H.......t.............
 33  ....#########...HHHHHHH***H.......t.............
 34  ....#########...HHHHHHH***H......ttt............
 35  ....#########....HHHHHHHHH......ttttt...........
 36  ....#########..................ttttttt..........
 37  ....#########...................................
```

At rest the `***` window is black (`kBg`), a dark lamp in a grey case.

```
constexpr Glyph kGlyphHdd = { 11, 9, { 0x7FC0, 0xFFE0, 0xFFE0, 0x8020, 0xFFE0,
                                       0xFE20, 0xFE20, 0xFE20, 0x7FC0 } };
// the light: rows 5..7, columns 7..9 of the glyph, mask 0x01C0
```

### Place

- `G_HDD` goes into `Slot` right after `G_SD`, and is always packed, like
  the SD glyph: internal flash is always there. SD stays at x 4 as it has
  always been; HDD is at x 16 (gap 3) or x 15 (gap 2) on every glass,
  because SD is always first and always 9 wide.
- y is the glyph row's: the icon is vertically centred (`glyphBox`), so its
  top is `glyphs.y + 3`.
- The light window, absolute: `R(hdd.x + 7, glyphs.y + 8, 3, 3)`.

| Glass | glyphs.y | HDD box (gap 3) | Light window |
|---|---|---|---|
| stick, both orientations | 24 | 16, 27, 11 x 9 | 23, 32, 3 x 3 |
| 2" | 24 | 16, 27 | 23, 32 |
| 4.3B | 36 | 16, 39 | 23, 44 |
| Makerfabs, Guition | 24 | 16, 27 | 23, 32 |

Physical size: 1.1 x 0.9 mm on the stick, 1.7 x 1.4 mm on the 480 glasses;
the light 0.3 to 0.45 mm square. Comparable to the SD glyph beside it,
which is the point: it is a status glyph, not a lamp.

### Colours

| State | Body | Light | Hex |
|---|---|---|---|
| idle, any style | `kDim` | `kBg`, black | 8A8A8A, 000000 |
| card access | `kDim` | amber, the lights plugin's `kAmber` at full | FF8200 |
| flash access | `kDim` | cool white, `kCoolWhite` at full | AAC8FF |
| storage error, lit half | `kDim` | `kRisk` | E06C6C |
| storage error, dark half | `kDim` | `kBg` | 000000 |
| lights not running, or `drive_fx = off` | `kFaint` | `kBg` | 6A6A72 |

- Full level, not `drive_bright`. `drive_bright` sets a wired pixel's
  brightness in a case; the glass has its own backlight. The 10% shading is
  exactly what made the lamp brown.
- The error red is the panel's `kRisk`, the same red the SD glyph turns for
  a card error (`cardState`, `panel.cpp:1155`), so one error reads as one
  colour across both glyphs.
- Dark at idle in every style. The breathe style's idle breath and the
  other styles' amber glow stay on a wired pixel and on a skin's drive lens;
  the icon does not show them. That glow on glass is the bug in finding 1.
- `kFaint` body for "no drive light": a sysop who switched the lights off
  sees a drive that is not reporting, not a drive that is idle.

### Timing: the styles kept

The icon follows the same state as the wired drive light, so the light on
the glass and the pixel in a case blink together. Nothing is re-timed.

| `drive_fx` | Lit for | Notes |
|---|---|---|
| pc | 60 ms after each access; a run longer than 60 ms flickers (a frame off at random, 1 in 4) | `kPcHold` |
| 1541 | 150 ms, solid through an access | `k1541Hold` |
| disk2 | 1,000 ms, the motor running on | `kDisk2Hold` |
| breathe | 80 ms; idle dark on the icon | `kBreatheHold` |
| off | never | the icon goes `kFaint` |
| error, every style but off | 500 ms red, 500 ms dark, for 10 s | `kErrorHalf`, `kErrorShow` |

Rule for the hand-off from the lights plugin (`lights.cpp:532-570`):

- `drawDrive` already decides lit, which store, error, or idle before it
  shades. It records that as one byte, `g_shownDisk`, beside
  `g_shownDrive` (`lights.cpp:862`): `DK_IDLE`, `DK_CARD`, `DK_FLASH`,
  `DK_ERR_ON`, `DK_ERR_OFF`, `DK_OFF`.
- `uint8_t lights::panelDisk()` returns it, or `DK_OFF` while the plugin is
  not running. One byte of static RAM.
- The panel reads it in the strip block (`kStripMs`, 40 ms). A 60 ms pc
  flash is always seen by at least one 40 ms read.

### Redraw

- `statusWords` gains nothing for the light: the row's key stays the
  glyphs present and their colours, so a flicker never repacks the row.
  The body colour (`kDim` or `kFaint`) is in the key.
- `drawGlyphs` paints the light from the shown state whenever it redraws
  the row, because its fill erased it.
- A new `diskTick`, beside `bellTick` and on the same pattern, fills only
  the 3 x 3 window when `panelDisk()` differs from what is shown. One 9 px
  rectangle a change, at most 25 a second during a long pc flicker.
- Skipped while the square's ring banner has the band (`ringBanner()`) and
  while a skin owns the glass; `ringEnd` invalidates `F_GLYPHS`, which
  repaints the light as it stands.
- Against today: the 4.3B and the Guition sent a 240 or 384 px lamp at the
  same rate; they now send 9. The stick, the 2" and the Makerfabs gain up
  to 25 tiny rectangles a second while the disk is busy, which they did not
  have. On the SPI glasses that is a window command and 18 bytes a time.

## The load line

### The metric

- Load is the BBS task's duty: the fraction of wall time the loop spends
  working rather than waiting in `select()`. The core already measures each
  pass's work as `dt` (`bbs.cpp:595`), smooths it into `loopAvgUs_`
  (`:670`) for SYS and counts passes over 50 ms as `slowCount_` (`:633`).
  The panel takes the same `dt`, summed. No new measurement.
- Core addition, under `BBS_HAS_LCD` beside the other panel accessors
  (`bbs.h:410-430`), so no board without a display grows:
  - `uint32_t loadWorkUs_`: `+= dt` every pass. A running total; it wraps
    every 71 minutes of work and the panel only ever takes a difference.
  - `uint32_t loadPeakUs_`: the longest `dt` since the panel last took it.
  - `void takeLoad(uint32_t& workUs, uint32_t& peakUs)`: both, and the
    peak reset to 0.
  - Cost: 8 bytes, one add and one compare a pass.
- Panel sample, every `kLoadMs` = 250 ms, on the panel's own clock:

```
dW    = workUs - g_loadW          (uint32 subtraction, wrap-safe)
dT    = micros() - g_loadT        (plat::micros, wrap-safe)
duty  = min(100, dW * 100 / dT)   (dW under 1,000,000: no overflow)
slow  = slowPasses() != g_loadSlow
peak  = peakUs
```

- `dT` is `plat::micros`, never `plat::millis`: the host's fast clock
  (`BBS_FAST_TIMERS`) scales millis only, so duty stays true on the host.
- A sample whose `dT` is over 1 s (after silent mode, a skin, a stall) is a
  baseline only: take the three figures, draw no conclusion. A slow pass
  ten minutes ago must not paint red now.
- The fallback, if the core is not to be touched: `duty ~ loopAvgUs_ x
  delta(loopPasses_) / dT`, which still needs two accessors and is wrong
  under bursts because the average covers about eight passes. The running
  total is exact and the same size. Use it.

What the panel's own drawing costs is inside `dt` (the plugins phase).
That is right: it is load the loop carries.

### Thresholds

| Level | When | Dot body | Rail | Hex (rail) |
|---|---|---|---|---|
| no reading | after a redraw, before the dot passes | none | `kRule` | 2C2C38 |
| blue, idle | duty under 15% | `kDial` | `kTrack` | 4C7F99 |
| green | duty 15% to 39% | `kLive` | `kLive` | 5DDC7A |
| yellow | duty 40% to 74%, or a pass of 25 ms or more | `kYellow` | `kYellow` | FFD35C |
| red | duty 75% or more, or a slow pass (over `BBS_SLOW_PASS_US`, 50 ms) | `kRisk` | `kRisk` | E06C6C |

- Why blue under 15: the display boards idle at 881 to about 1,000 us of
  work a pass (the 4.3B and stick bench figures in CLAUDE.md) against a
  10 ms `select()` (`BBS_SELECT_MS`, `config.h:271`): about 8 to 9% duty
  with nobody on. 15 leaves 6 points of margin. The Guition and the
  Makerfabs have no idle figure on record; the first bench read of PANEL's
  load line (below) confirms or moves the edge. One table of four
  constants, no per-board fudge.
- Why a slow pass is red whatever the duty: a pass over 50 ms is Rule no. 1
  failing, the one thing callers feel. A 25 ms pass is half way there:
  yellow, "pops up" without crying wolf.
- Blue is today's rail colour exactly. An idle board looks as it does now.
- The rail carries green, yellow and red at full strength and blue at 60%
  (`kTrack`), on purpose: idle is quiet, load is loud. The dot is the same
  token at full strength in every level, with its white core.

### Response

- Up: at the next sample. Worst case 250 ms, plus one frame (40 ms small,
  80 ms big) for the dot to show it.
- Down: two samples in a row below the shown level, then one level a
  sample. Red to blue: at least 0.75 s after the trigger ends.
- Red holds 2 s from its last trigger. One slow pass then paints 50 px of
  red (2 s at 25 px a second): readable across a room, a tenth to a third
  of the line depending on the glass.
- No colour flickers faster than 4 Hz, because the level only changes at a
  sample.

### The line: length, fade, sampling

- The dot paints the rail behind it with the current level as it leaves
  each pixel. One lap is the history. To the left of the dot is newest; the
  first pixel to its right is one lap old.
- Speed unchanged: 25 px a second everywhere (1 px every 40 ms small and
  tall, 2 px every 80 ms big). The dot's pace is the board's heartbeat and
  stays the same on every glass.

| Glass | Rail | y, h | Dot step | Lap | Line pixels sampled |
|---|---|---|---|---|---|
| stick portrait | 172 | 42, 2 | 1 px / 40 ms | 6.9 s | each step |
| stick landscape | 320 | 42, 2 | 1 px / 40 ms | 12.8 s | each step |
| 2" | 240 | 42, 2 | 1 px / 40 ms | 9.6 s | each step |
| 4.3B | 400 | 54, 1 | 1 px / 40 ms | 16.0 s | each step |
| Makerfabs | 480 | 42, 2 | 2 px / 80 ms | 19.2 s | each step |
| Guition | 480 | 42, 2 | 2 px / 80 ms | 19.2 s | each step |

- The level changes at most every 250 ms, so a stretch of one colour is at
  least 6 px long. The rail's resolution is one pixel; its colour's is a
  quarter second.
- No fade. Every stretch keeps its colour until the dot comes round again,
  which is what Rob asked for. A fade would mean resending the whole rail
  on a timer (480 x 2 px, one band a second at the least) to say what the
  dot's position already says: where now is.
- The line before the first lap, and after anything that redraws the
  glass (start, silent mode ending, a skin handing back), is `kRule` grey:
  no reading, rather than a blue that would claim the board was idle while
  nobody was watching. The dot then paints it in over one lap.
- A wake from the touch boards' sleep paints the rail grey too: the dot
  was held still, so what is ahead of it is older than a lap. One band, at
  the wake.

### The dot

Rail 2 px (every glass but the 4.3B):

```
x:     x-3 x-2 x-1  x  x+1 x+2
y-1          D   D   D   D          band's last row
y      t1  t2  D   W   W   D        rail row 1
y+1    t1  t2  D   W   W   D        rail row 2
y+2          D   D   D   D          air
```

- `D` body, 4 x 4, the level's token. `W` core, 2 x 2, `kWhite`. `t2` and
  `t1` the tail, 2/6 and 1/6 of the token, on the two rail rows.
- `dotBox(x) = R(x - 3, y - 1, 6, 4)`.
- `dotErase` fills row y-1 with `kBand` (or `kBar` under the square's ring
  banner, as now), rows y and y+1 with the current level's rail colour,
  row y+2 with `kBg`. That one change is the whole trail: the pixels the
  old footprint leaves uncovered are exactly the ones the dot just passed.
- 4.3B, rail 1 logical px: today's 3 x 3 dot with its 1 px white core and
  two tail pixels, in the level's token. `dotBox` as now.
- Under the ring banner the square's dot runs on as now; the banner is
  rows 0 to 41, the rail 42 and 43.

### The bands it costs

| What | Pixels | Rate | Against today |
|---|---|---|---|
| a dot step, small | 7 x 4 = 28 | 25 a second | was 6 x 3 = 18; same one rectangle |
| a dot step, big | 8 x 4 = 32 | 12.5 a second | was 7 x 3; same one rectangle |
| the trail | 0 extra | | painted inside the step's rectangle |
| the level | 0 | 4 samples a second, three integer reads | |
| a grey rail at a wake | w x 2, at most 960 | at a wake | one band |
| a redraw | the rail inside the whole glass | as now | none |

- History RAM: none. The framebuffer is the record, and every path that
  repaints the rail (`redrawAll`, the wake) is a path where the history is
  meant to restart. If a future path must keep the line across a redraw,
  a nibble a column is 240 bytes.
- Panel state: about 16 bytes (last work, last time, last slow count,
  level, hold timer, fall count, the shown disk state).
- Work on the loop: under 10 us a sample. `lcdDraw` stays DMA and returns
  at once; nothing waits on the glass. Rule no. 1 is untouched.

## Per glass

### The 1.47" stick

- Band: SD at x 4, HDD at x 16, light at 23, 32. With every glyph lit the
  row packs at gap 2 and ends at x 112, 5 px short of the antenna at 118.
- Rail: 172 x 2 at y 42, a 6.9 s lap in portrait, 12.8 s turned landscape.
- Foot: unchanged. The stick has a real WS2812 on GPIO 38 as its wired
  drive light (`board.h:231`); it and the icon show the same state and
  blink together.
- It is the shortest history of the five. That is acceptable: the line is
  "what has the board been doing these last seconds", and SYS keeps the
  long figures.

Header now and then, one column = 4 px:

```
  y   0         1         2         3         4
      0123456789012345678901234567890123456789012
  0   ###########################################   bar
  3   The Rusty Antenna
 24    [sd] [tw] [st]                 |  16:20      band
 42   -----------------o-------------------------   rail 1 px kTrack, dot kDial

 24    [sd][hd] [tw] [st]             |  16:20      band, HDD at x 16
 42   gggyyyrrrrrrrrrrrryyggobbbbbbbbbbbbbbbbbbbb   rail 2 px: newest left of the dot
```

### The 2"

- Band: as the stick; with every glyph and the camera lit, gap 2 and the
  row ends at x 125 against a cap of 122 from x 4: exact fit.
- Rail: 240 x 2 at y 42, 9.6 s a lap. Foot: the round lamps, unchanged.

### The 4.3B

- Band at y 34, glyphs y 36: HDD at x 16, light at 23, 44. The band word
  starts at x 122; the glyph row ends at 112 at worst.
- Rail: 400 x 1 logical at y 54, 2 physical px. 16 s a lap.
- Foot: the lamp goes, the bar is `R(4, 219, 392, 12)`, finding 1.

### The Makerfabs

- Big layout, glyphs `R(4, 24, 120, 16)`: everything at gap 3, ends 121.
  Band word at 134, untouched.
- Rail: 480 x 2 at y 42, 19.2 s a lap. Foot: the 16 square LEDs row,
  unchanged (it never had a lamp).
- Turned portrait (320 x 480): the same band and a 320 px rail, 12.8 s.

### The Guition, 480 x 480

```
  y   0         1         2         3         4         5
      012345678901234567890123456789012345678901234567890123456789
  0   ############################################################   bar 22, kBar
  3   The Rusty Antenna                         192.168.0.86:6400
 22   ============================================================   band 20, kBand
 24   [sd][hd][up][tw]                                | -58 16:20    HDD at x 16, light 23,32
 42   bbbbbbggggyyyyrrrrrryyyyggggbbbbbbbbbbbbbbobbbbbbbbbbbbbbbbb   rail y 42..43
 48   [] Callers 3/11              DOING        ON  LEFT   TERM
 ...  (node board, deck, sweep and six cells exactly as the square spec)
440   ------------------------------------------------------------   rule
448   ##### ##### ooooo ooooo ##### ooooo ooooo ooooo ooooo ooooo     bar 7..472, no lamp
```

Reading the rail: the dot is at column 42. Columns 0 to 41 are the last
13.4 s, oldest at the left: idle, then a busy spell that peaked red (a slow
pass, held 2 s, about 50 px or six columns), easing back to blue. Columns
43 to 59 are 13.4 to 19.2 s ago.

- Ring banner: rows 0 to 41, so the HDD icon is hidden with the glyphs and
  the rail and dot carry on below it.
- Every box of `internal/tty-ux-panel-g4848-2026-10-01.md` stands except
  `L.drive` (gone) and `L.leds` (finding 1). Its note that free lamps go
  dark "because amber is the drive lamp's idle glow beside them" loses its
  second reason; the first (dim blue claims a caller can dial in) still
  holds, so the behaviour stays.

## States

| State | HDD icon | Load line |
|---|---|---|
| silent (switch or hours) | not drawn, backlight off | not drawn, not sampled; at the end the glass is redrawn, the rail grey, the first sample a baseline |
| touch sleep | drawn, unseen | dot held still as now, sampling on; at the wake the rail goes grey |
| closed | as usual | as usual: closed is a policy, not a load. An idle closed board is blue |
| shutting down | as usual | as usual. The band word says SHUTTING DOWN in `kRisk`; the line may be red at the same time only if the loop really is stalling, which is worth seeing then |
| shut down, board still on | as usual | as usual, blue |
| ring banner (square) | hidden with the glyph row | runs on |
| a skin from the card | the skin's own `drive` lens, its own style, unchanged | not drawn; sampling continues (cheap) so the dot's colour is right at once on hand-back; the rail comes back grey |

Skins are untouched in 1.2.1. Queued, not built: `load` as a `meter` and
`graph` SOURCE and a `lamp` state in the skin format, from the same duty
figure. The figure exists; the format's next minor can take it.

## PANEL

Two lines, so the bench and the host tests read what the glass shows:

```
Load      9% blue, longest pass 3 ms, 7 s a lap
Drive     idle, style pc
```

The first figure is what sets the blue edge on the Guition and the
Makerfabs.

## What stays as it is

- The ten line lights: what they show, their effects, their colours,
  switchboard's dark free lamps on a closed board. Only the 4.3B's and the
  Guition's boxes widen.
- The system cells, the traffic sweep, the hourglass glyph (a slow pass in
  the last minute), SYS's loop rows. The hourglass and a red line agree on
  purpose: one says "this minute", the other "here, on the time line".
- The dot's speed and its standing aside, except the one-frame-in-two cap.
- The SD glyph: first, x 4, its colours, red for a card error.
- The wired drive light in a case: its idle glow, its brightness, its
  styles. The icon takes the state, not the pixel.
- Skins' drive lens and styles.
- `drawLed`'s 40 floor (`panel_gfx.h:689`): no longer needed for the drive
  lamp, still right for the bar's dimmest lamps.

## Implementation order

- Finding 1, the feet: delete the lamp, widen `L.leds` on the 4.3B and the
  Guition. Two rectangles. The most visible fix and the one Rob saw.
- The HDD glyph: `kGlyphHdd`, `G_HDD` after `G_SD`, the gap-2 repack,
  `g_shownDisk` and `lights::panelDisk()`, `diskTick`. About 60 lines and 1
  byte.
- The core's `takeLoad` and its two counters under `BBS_HAS_LCD`. 8 bytes,
  3 lines in `tick()`.
- The level: the 250 ms sample, the thresholds, the hysteresis and the
  red hold, the 1 s baseline rule.
- The rail: `L.track.h` 2 except tall, the 4 x 4 dot, `dotErase` painting
  the level, grey at redraw and at wake.
- The stand-aside cap and the two PANEL lines.
- Later, its own minor: `load` in the skin format.

Flash under 1 KB in all; static RAM 8 bytes in the core and about 17 in
the panel and lights, display boards only. Nothing changes on a board
without a display.
