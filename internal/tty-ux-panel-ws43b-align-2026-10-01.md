# The 4.3B panel, aligned to the square: 1.2.2

For **1.2.2**. Measured against the worktree `release-prep/wt-121i`, branch
`rel-1.2.1-int`, head d5b0218, 1.2.1-dev.15, which carries every 1.2.1 lane,
so both layouts and the load line are in one tree.

Glasses:

| Board | Glass | Drawn canvas | Scale | Panel code |
|---|---|---|---|---|
| Waveshare ESP32-S3-Touch-LCD-4.3B | 800 x 480 RGB | **400 x 240** | 2:1, every pixel doubled | `Layout`, the `tall` branch (`panel_gfx.h:981`) |
| Guition ESP32-S3-4848S040 | 480 x 480 RGB | **480 x 480** | 1:1 | `BigLayout` + `bigSquare` (`panel_gfx.h:1325`), `PANEL_BIG` |

Rob's brief, narrowed: "the caller block and I also like the in and out, is
that transmit along with the right corner that shows all the status, the ws43
is close I think it just needs tweak to better align". So this is an
alignment pass over three elements. Nothing else is in scope.

## The verdict

He is right: the 4.3B is close, and the three things he named need 11 numbers
changed, 3 new fields and no new arrangement. Two of the three are missing
rather than misaligned (the 4.3B has no in/out figures and no dBm reading at
all, because both live inside `#if PANEL_BIG` and the 4.3B is not a big
glass), and the third, the caller block, is 8 px of left margin away from the
square's. The per-line gain is the **traffic pip**, a 6 x 6 dial square beside
each line that moved bytes, which is exactly "the line info when coms are
going across" and which the 4.3B does not have. It costs nothing: the 8 px it
needs comes out of the margin, and the handle still holds 15 glyphs. Cost is
about 260 bytes of static DRAM, drawing only on the panel side, one core gate
to widen. Nothing moves vertically, so the row budget is untouched.

## 1. What "in" and "out" are, from the code

Rob asked. The answer:

| Label | Source | Means | Colour |
|---|---|---|---|
| `in` | `Bbs::bytesIn()` → `rxBytes_` | bytes **read from** callers' sockets: keystrokes, uploads. The board receives. | `kDial` (light blue) |
| `out` | `Bbs::bytesOut()` → `txBytes_` | bytes **written to** callers' sockets: screens, downloads. The board transmits. | `kLive` (green) |

`bbs.h:629` states it: "every byte read from and written to a caller's socket
since boot". Sampled every 4 s by `graphSample` (`panel.cpp:1738`) and shown
as bytes a second. Board-wide, every line summed, not per line.

**The label does not read clearly, and Rob's question is the proof.** It is
`<fig> in <fig> out` with a double-headed arrow icon; "in" and "out" are
relative to nothing stated, and the two colours that distinguish them in the
sweep are not tied to the words. The fix costs no pixels and 8 px less width:

```
  now    [<>] 1.2K in 1.2K out          20 + 16 glyphs = 148 px
  after  [<>] RX 1.2K  TX 1.2K          20 + 15 glyphs = 140 px
```

- `RX` in `kDim`, its figure in `kDial`; `TX` in `kDim`, its figure in `kLive`.
- RX/TX is the vocabulary the board already uses for the same quantity: the
  switchboard strip effect is specified as free lamps "flickering with real
  traffic", and `Bbs::takeTraffic` returns `rx` and `tx`.
- This changes the square too, because `drawTraffic` is one function. That is
  the right outcome: both glasses then say the same thing, and the question
  cannot be asked again. **Rob's call**, one line: keep `in`/`out`, or take
  `RX`/`TX`.

## 2. Element by element: square against 4.3B

Only the three elements in scope. Every figure is on each board's own drawn
canvas.

### The caller block

| | Square 480 x 480 | 4.3B 400 x 240 | Worth copying? |
|---|---|---|---|
| Shape | 11 fixed rows, one a line (S + nodes 1-10), one column 462 px | 12 flowing slots, 2 columns of 6, 190 px each; callers fill from the top, recent events take the rest | **No.** 11 x 20 = 220 px against 116 px of body. Two columns of 6 is the 2:1 answer and it already holds 12, one more than the square. |
| Free lines | shown: " 3  free" in faint, "closed" / "not answering" when the board is not taking calls | not shown at all | **No**, not here: showing 11 lines would take every slot and leave the recent list nowhere. See §4. |
| **Traffic pip** | 6 x 6 `kDial` square at x 6, row y + 5, for each line that moved bytes since the panel last looked; refreshed twice a second | **absent**: the 4.3B's only traffic sign is the light bar at the foot | **Yes. This is the item.** It is the per-line "coms are going across". |
| Left margin | pips 6..12, air 2, row content from 14 | row content from 4, no gutter | **Yes**, copy the gutter: 8 px. |
| Row columns | node 14, mark 30, handle 46 (20 glyphs), DOING 232, ON right to 352, LEFT right to 400, TERM 424 | node r.x, mark r.x+16, handle r.x+32 (15-16 glyphs), ON right to r.x+r.w | DOING/LEFT/TERM **no**: 182 px is 22 glyphs. See §4. |
| Heading | icon x 6 (= pip x), text x 26, then DOING / ON / LEFT / TERM in faint | icon x 4, text x 24, no column labels | Icon and text are **already the square's relationship** (8 px left of the rows): nothing moves. Add the `ON` label. |
| Gap mark→handle | 8 px (30+8 → 46 is 8 px of air after the mark's cell) | 8 px (16+8 → 32) | identical already; it is where the pip's 8 px comes from |

### The in and out

| | Square | 4.3B | Worth copying? |
|---|---|---|---|
| Figures | `B.traf` R(248, 296, 228, 16), the right deck's heading, on `kBg` | **absent** | **Yes**, the figures. |
| Sweep | `B.graph` R(248, 316, 228, 56), 228 columns, 15.2 min, in above the axis in dial and out below in live | **absent** | **No.** See §4. |
| Cadence | one column every 4 s (`kSampleMs`) | n/a | keep 4 s for the figures |

### The right-hand corner

| | Square | 4.3B now | 4.3B after |
|---|---|---|---|
| Band | R(0, 22, 480, 20), content y 24 | R(0, 34, 400, 20), content y 36 | unchanged |
| Glyph strip | R(4, 24, **120**, 16) | R(4, 36, **110**, 16) | R(4, 36, **120**, 16) |
| Band word | R(**134**, 24, 246, 16) | R(**122**, 36, 216, 16) | R(**134**, 36, 166, 16) |
| Antenna | R(**388**, 24, 6, 16) | R(346, 36, 6, 16) | R(**308**, 36, 6, 16) |
| **dBm** | R(**398**, 24, 32, 16), mean of the last four RSSI samples, right-aligned, `signalColour`, "--" in faint unjoined | **absent** | R(**318**, 36, 32, 16) |
| Clock | R(436, 24, 40, 16) | R(356, 36, 40, 16) | unchanged |
| Gaps ant→dBm→clock→edge | 4, 6, 4 | n/a, 4, 4 | **4, 6, 4** |

Two things fall out of the strip being 10 px narrow:

- **The icon spacing differs between the two glasses, and Rob can see it.**
  `pack()` lays the row out at a 3 px gap and repacks at 2 px if it would pass
  the strip's right edge. The 4.3B's ten glyphs (no camera on this board) are
  91 px of ink: 118 at gap 3, which is past 4 + 110 = 114, so it **repacks at
  2 px** and ends at 113. The square's right edge is 4 + 120 = 124, so 122
  fits and it stays at gap 3. Widening the 4.3B's box to 120 puts both glasses
  on a 3 px gap in every state, which is the alignment.
- The band word then starts at 134 on both, which is where the in/out figures
  go.

## 3. The 4.3B's after layout, in pixels

### Before and after, the changed rects only

| Element | Before | After | Why |
|---|---|---|---|
| `L.glyphs` | R(4, 36, 110, 16) | R(4, 36, 120, 16) | the square's cap; stops the 2 px repack |
| `L.ant` | R(346, 36, 6, 16) | R(308, 36, 6, 16) | room for the dBm |
| `L.word` | R(122, 36, 216, 16) | R(134, 36, 166, 16) | `ant.x - 8 - 134`; the square's left edge |
| `B.dbm` equivalent | — | R(318, 36, 32, 16) | `clock.x - 6 - 32` |
| `L.list[0..5]` | R(4, 80 + 20k, 190, 16) | R(**12**, 80 + 20k, **182**, 16) | the pip gutter |
| `L.list[6..11]` | R(206, 80 + 20k, 190, 16) | R(**214**, 80 + 20k, **182**, 16) | the pip gutter |
| pip column, left | — | R(4, 80, 6, 116) | `colX`, rows' y..y+116 |
| pip column, right | — | R(206, 80, 6, 116) | |

Everything else is byte for byte as it is: `bar` R(0,0,400,34), `slot`
R(4,1,392,32), `band` R(0,34,400,20), `track` R(0,54,400,1), `headIcon`
R(4,60,16,16), `head` R(24,60,170,16), `head2Icon` R(206,60,16,16), `head2`
R(226,60,170,16), `colRule` R(199,60,1,136), `rule1` R(4,198,392,1), `sys`
R(4,200,392,16) with cells at 4 / 136 / 268 (heap, peak, temp), `leds`
R(4,219,392,12) as a light bar.

### Inside a caller row, after

The row rect starts 8 px in, and the 8 px of air that already sits between
the rank mark and the handle is what pays for it. The handle-to-ON clearance
drops from 8 px to 4 px so the handle keeps its glyph count.

| Part | Before (left column) | After (left column) | Width |
|---|---|---|---|
| pip | — | x 4, y + 5, 6 x 6, `kDial` | 6 |
| node label | x 4 | x 12 | 16 |
| rank mark | x 20 | x 28 | 8 |
| handle | x 36, maxW `190 - 32 - tw - 8` | x 44, maxW `182 - 32 - tw - 4` | 126 → **122** px |
| ON | right to x 194 | right to x 194 | `tw` |

Handle glyphs, `BBS_USER_MAX` 20: 15 with a 3-glyph ON ("51m"), 16 with a
2-glyph one ("2h"). **Identical before and after.** Right column: add 202 to
every x.

### Ruler and mock-up

Before, the body at 400 px wide (one ruler column = 4 px):

```
      0         1         2         3         4         5         6         7         8         9
      0.........0.........0.........0.........0.........0.........0.........0.........0.........0.
  36  [sd][hd][tw][st]                                               |-- 16:20       band, gap 2
  54  --------------------------o-----------------------------------------------      rail
  60  [] Callers 3/11                      () Calls 23 today
  80   1) Daytona                  51m  |  (+) 16:18 Daytona
 100   3) Quantum                   7m  |  (-) 16:02 Hobbit
 120  10* guest42                   2m  |  (+) 15:47 Quantum
 140                                    |  (!) 15:31 Daytona
 160                                    |  (+) 15:12 N0CALL
 180                                    |  (-) 14:58 guest7
```

After:

```
      0         1         2         3         4         5         6         7         8         9
      0.........0.........0.........0.........0.........0.........0.........0.........0.........0.
  36  [sd] [hd] [tw] [st]   [<>] RX 1.2K  TX 340       | -58  16:20   band, gap 3, dBm added
  54  --------------------------o-----------------------------------------------      rail
  60  [] Callers 3/11                 ON   () Calls 23 today         ON
  80  #  1) Daytona                  51m  |  (+) 16:18 Daytona
 100  #  3) Quantum                   7m  |  (-) 16:02 Hobbit
 120     10* guest42                  2m  |  (+) 15:47 Quantum
 140                                     |  (!) 15:31 Daytona
 160                                     |  (+) 15:12 N0CALL
 180                                     |  (-) 14:58 guest7
```

`#` is the 6 x 6 pip: lines 1 and 3 moved bytes in the last half second,
line 10 did not. `[<>]` is `kIconTraffic`, already drawn. `-58` is the dBm.

### The pip's rules

- Mask: the lines that read or wrote bytes since the panel last asked, a bit
  per `Session::id` (`Bbs::takePanelTraffic`), accumulated between text passes
  the way `g_moved` does on the big glass.
- A slot gets a pip when it holds a caller **and** that caller's id bit is set.
  A slot holding a recent event, a "+N more" row or nothing gets none.
- One field a column, each its own rect, each queued separately. Never one
  field over both: the union of the two columns is 208 x 116 = 24,128 px,
  three bands, for a 36 px change.
- The field's key is the column's 6-bit "slot has a pip" mask, computed from
  the slot allocation, so a slot that stops being a caller clears its pip.
- Drawn exactly as `bigPips` draws it: `fill(R(pipX, row.y + 5, 6, 6), kDial)`
  after a `kBg` clear of the column.

### The in/out figures: one box, two tenants

`L.word` R(134, 36, 166, 16) carries both, and the word wins:

| Board state | What the box shows | Width |
|---|---|---|
| shutting down | `SHUTTING DOWN` in `kRisk` | 104 px |
| closed to callers | `CLOSED to callers` in `kWarm` | 136 px |
| otherwise | `[<>] RX 1.2K  TX 340` | 140 px at worst |

- One field, not two: only one tenant draws, so `F_WORD`'s key becomes the
  word when there is one and the rate pair when there is not. No new field.
- Background `kBand`, not `kBg`: the box is on the band.
- Refreshed on the traffic sampler's 4 s clock, not the 500 ms text clock, so
  the figures do not twitch.
- Accepted: while the board is shutting down or closed, the figures are not
  shown. Both are states a sysop is standing in front of the board for, and
  neither has a sweep on the square either.

### The heading's ON label

`text(r.x + r.w - 16, r.y, "ON", kFaint, ...)` in both heading fields, at
x 178 and x 380, right-aligned over the rows' own ON figures. The square has
it (`bigHead`); the 4.3B's right-hand figure is unlabelled today. 16 px of
170 in `head` (which uses 96 for "Callers 3/11") and of `head2` (112 for
"Calls 23 today"): both fit.

## 4. What a 2:1 canvas cannot borrow

Only for the three elements. The rule throughout: do not squash, drop.

- **The square's five-column caller row.** 462 px is 57 glyphs; the 4.3B's
  column is 182 px, 22 glyphs. `node(2) + mark(1) + handle(15) + ON(3)` is
  already 21 of the 22. DOING needs 9 glyphs, LEFT 3, TERM 5. There is no
  smaller face (8 x 16 drawn is 16 x 32 on the glass; `panel_font.h` has two
  faces and the small one is the small one). **Do not shrink the handle to fit
  DOING in.** A handle a sysop cannot read is worse than a DOING column they
  can get from `WHO`. What the 4.3B gets instead is the pip, which is the one
  thing no other screen can tell them in real time.
- **A 20-glyph handle.** The square shows the whole of `BBS_USER_MAX`; the
  4.3B cuts at 15. Accepted and unchanged by this pass.
- **The fixed 11-row node board.** 11 x 20 = 220 px against 116 px of body.
  Two columns of 6 is the 2:1 answer, and it holds 12, one more than the
  square's 11. The cost is that free lines are not listed. **Do not** lay the
  11 lines across the two columns: it takes every slot, the recent list goes,
  and the glass stops saying what just happened. If Rob wants free lines on
  the 4.3B later, the honest trade is the recent list, and that is a layout
  decision for him, not an alignment tweak.
- **The traffic sweep.** 228 x 56 on the square. The 4.3B's body is 116 px and
  every row of it is a caller line. A 20 px sweep on a fixed log scale of 17
  doublings is 1.2 px a doubling: a line that cannot be read is worse than no
  line. The figures come without it. The sweep's own history lives on the
  square and in `SYS`.
- **The right-hand corner borrows exactly.** The band is 20 px on both glasses
  and the corner cluster is 88 px wide on both (antenna + dBm + clock). Nothing
  is lost and the gaps match to the pixel.

## 5. Row budget, 4.3B after

Nothing moves vertically. 240 rows:

| y | Height | What |
|---|---|---|
| 0 | 34 | bar, the 16 x 32 rotating slot |
| 34 | 20 | band: glyphs, **in/out**, antenna, **dBm**, clock |
| 54 | 1 | load line's rail (2 physical px) |
| 55 | 5 | air |
| 60 | 16 | headings, with `ON` |
| 76 | 4 | air |
| 80 | 116 | six caller rows a column, pitch 20, with the pip gutter |
| 196 | 2 | air |
| 198 | 1 | rule |
| 200 | 16 | system row: heap, peak, temp |
| 216 | 3 | air |
| 219 | 12 | light bar |
| 231 | 9 | clear of the bottom edge (Rob's photo, 2026-09-28) |

## 6. Touch, and the 1.2.2 hamburger

**No touch area moves.** This pass is drawing only. The 4.3B's `touchTick`
(`panel.cpp:2350`) treats a finger down anywhere as one tap, with no regions
at all: it wakes a sleeping backlight, dismisses a new photo's show, or turns
the header's slot to its next page and holds it `kTapHoldMs`.

**There is a collision with the hamburger, and it is not caused by this pass.**
Neither glass has a free corner today:

| Glass | Bar | Band's right end | Foot |
|---|---|---|---|
| 4.3B | one 392 px slot, R(4,1,392,32), left-aligned | after this pass 308..396 is antenna, dBm, clock | light bar 4..396, 9 px clear below |
| Square | name 4..212, slot 284..476 | 388..476 antenna, dBm, clock | light bar 4..476 |

What this pass does consume is the 4.3B's band dead space: the word box was
166-216 px of bare blue whenever the board was neither closed nor shutting
down, and the in/out figures now sit in 140 px of it. If the hamburger was
going to live in the band, it no longer can.

**Reservation for the hamburger spec, so it fits on both glasses in the same
place:** the bar's top-right, a 32 x 32 touch target, drawn as a 16 x 16 icon
centred in it.

| Glass | Reserve | Slot becomes | Cost |
|---|---|---|---|
| 4.3B | bar x 364..395, icon at (372, 9) | R(4, 1, 352, 32), 22 glyphs big face | 2 glyphs off a long board name. The address (18 glyphs) and `up 3h 14m  29 GB` (17) are untouched. |
| Square | bar x 444..475, icon at (452, 3) | R(284, 3, 152, 16), 19 glyphs | the slot is right-aligned, so its text shifts left 40 px; an 18-glyph address still fits, a long mDNS name is cut. |

Handed back: the corner and the icon are the hamburger spec's to settle. What
this report fixes is that the band is no longer available for it.

## 7. Cost

| Item | Cost |
|---|---|
| New fields in the base `Field` enum | 3: dBm, pips-left, pips-right. `g_shown` is `[kFields][80]`, so **+240 bytes static DRAM** on every `BBS_HAS_LCD` board, the 1.47" stick and the 2" included, where the three are unused. |
| New state | rate pair 8 B, RSSI ring 7 B, pip masks 2 B, `g_moved` 2 B ≈ **20 bytes**, on the boards that compile it. |
| `Bbs` | `panelMoved_` is `#if BBS_PANEL_BIG` (`bbs.h:1304`), as is `takePanelTraffic` (`:444`). **2 bytes** where it becomes available. |
| Flash | small: `drawTraffic`, `bigDbm` and `bigPips` already exist; they come out of the `PANEL_BIG` block rather than being written. |
| Drawing only? | On the panel, yes: every change is a rect and a few `text` calls. Off the panel, no: one core gate widens (below). |
| Framebuffer | unchanged, 400 x 240 x 2 = 192 KB in PSRAM. |

### Rule no. 1

A band on the 4.3B is 400 x 24 = **9,600 px** (`platform_esp32_rgb.cpp:112`),
one band a flush, one flush a tick, and the panel is `PF_FAST` (20 ms), so
50 bands a second.

| New rect | Pixels | Bands | How often |
|---|---|---|---|
| pip column, each | 6 x 116 = 696 | 1 | at most 2 a second |
| dBm | 32 x 16 = 512 | 1 | at most 1 a second (`kDbmMs`) |
| in/out figures | 166 x 16 = 2,656 | 1 | once every 4 s |

Worst case about **5.3 extra bands a second of 50**, and no rect is split
across bands, so no pass sends more than one already does. The largest new
composition, the 166 x 16 field, is smaller than a caller row (182 x 20 =
3,640 px) which is already composed twice a second. A full redraw is
unchanged: `redrawAll` queues the glass as one rectangle, 96,000 px, 10 bands,
200 ms, exactly as today.

### Handed back: one code decision

`panelMoved_`, `takePanelTraffic`, `g_rateIn`/`g_rateOut`, `graphSample`'s
rate half, and `bigDbm`'s RSSI averaging all sit inside `#if BBS_PANEL_BIG`,
and the 4.3B has `BBS_PANEL_BIG` 0 (`board.h:1671`: `BBS_LCD_RAM_LONG >= 400`,
and the 4.3B leaves the RAM figures at the ST7789's 240 x 320). All three
borrows need them where that is 0. **The gate's name and shape are the
builder's**, not this spec's: the smallest split is a second capability that
the 4.3B and every big glass both set, leaving the sweep, the node board, the
six cells and the ring banner on `BBS_PANEL_BIG` as they are. Do not simply
turn `BBS_PANEL_BIG` on for the 4.3B: `bigGlass(400, 240)` is false, so
`B.on` would be false and the big fields would be compiled, unused, for
nothing.

## 8. What stays as it is

- **The load line's rail and dot, and the drive glyph.** Rob reads both fine
  at a glance. Sizes, colours and the 2 px rail untouched.
- **The light bar.** The first pixel is right; the bar spans the foot at
  R(4, 219, 392, 12) and nothing in this pass goes near it.
- **The 4.3B's three-page rotating slot** (name, address, uptime, with the
  fade) against the square's fixed name plus two-page slot. Rob did not name
  the bar, the 4.3B's big face is why its address is legible across a room,
  and the hamburger reservation already shortens it. Leave it.
- **The system row.** Heap, peak, temp at 4 / 136 / 268. The square's six
  cells need 160 rows the 4.3B has not got, and all three figures here are
  the ones worth having.
- **The recent list** sharing the slots, and the rule that divides it from the
  callers. It indents 8 px with the caller rows, which keeps its icon column
  on the caller rows' node column exactly as today.
- **The two-column gutter**: `colRule` at 199, left column ending 194, right
  starting 206. 5 px and 6 px of air either side of the rule, which is the
  square's deck gutter (6 and 7) to within a pixel.
- **The heading icons at x 4 and x 206.** They are already 8 px left of the
  rows' content after this pass, which is precisely the square's relationship
  (icon at the pip column's left edge, rows 8 px in).
- **Vertical rhythm**: 4 px of air between the heading and the first row, 2 px
  in the band above and below a 16 px glyph row, pitch 20. Both glasses agree
  already.

## 9. The other panels, as a note

Not a design, and none of it is 1.2.2 unless Rob says:

| Panel | Should it follow? |
|---|---|
| Makerfabs 3.5" v1.0 and v2.0, 480 x 320 | Already `PANEL_BIG`: it has the node board, the pips, the sweep and the dBm. The one thing it lacks is the square's LEFT column and six recent events, which is `BBS_PANEL_SQUARE`'s. Nothing from this pass applies. |
| Guition 480 x 480 | The reference. Only the `RX`/`TX` relabel reaches it, and only if Rob takes it. |
| Waveshare 2", 240 x 320 portrait | The pip gutter would fit (its rows are 232 px) and would be the same win: per-line traffic on a glass whose only traffic sign is the round lamps. The in/out figures would not: its band word has no room beside the glyphs. A separate pass, after the 4.3B is on the glass and Rob has looked at it. |
| Waveshare 1.47" stick, 172 x 320 | No. 164 px is 20 glyphs and the row is full; a pip would cost a glyph of handle, and the stick has a real WS2812 drive light and the strip in front of it. Leave it. |

## 10. Implementation order

Cheapest and most visible first.

1. **The right corner** (item 3). Widen `L.glyphs` to 120, move `L.word` to
   134, move `L.ant` to 308, add the dBm box at 318. One function, four
   numbers, and the glyph spacing stops changing with the board's state. No
   new field but the dBm's.
2. **The in/out figures** (item 2). `drawTraffic` into `L.word` on `kBand`,
   the word winning when there is one, on the 4 s sampler. No new field.
3. **The `RX`/`TX` relabel**, if Rob takes it. One function, both glasses.
4. **The pip gutter** (item 1). Row rects to x 12 / 214 at 182 wide, the
   handle's trailing clearance 8 → 4, the two pip fields. This is the one that
   needs the core gate widened, so it goes last of the four even though it is
   the item Rob cares most about.
5. **The heading `ON` label.** Two `text` calls.
6. Host render at 400 x 240 (`PANEL SHOT`) at three states and read it back
   against the ruler above: nobody on; three callers with two moving bytes;
   every line busy with every glyph lit and the board closed. The third is the
   one that proves the band still fits.
