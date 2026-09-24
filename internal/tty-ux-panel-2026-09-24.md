# The S3 status panel: layout, icons, palette, signal, motion

Design spec for the `panel` plugin on the Waveshare ESP32-S3-LCD-1.47:
172 x 320 ST7789, portrait, USB plug at the top. Measured against
`release-prep/wt-s3/src/plugins/panel.cpp`, `panel_gfx.h`, `panel_font.h`,
`lights.cpp`, `platform_esp32.cpp` (the LCD band) and the directory's
`server.py` (the palette, the line art, the wordmark, the run card's dots).
Every box below was rendered at 1:1 from the real Spleen arrays and the
site's tokens quantised through RGB565 before it was written down. No code
was changed.

The glass: 1.47 inch diagonal over 363 px is 0.103 mm a pixel, 247 ppi. A
1 px line is 0.1 mm and disappears at arm's length. A 2 px line is 0.2 mm
and reads as a stroke. Everything drawn here is 2 px.

## Revision 1: Rob's changes (2026-09-24)

Rob, on the revision 0 busy mock-up: the uptime, the address and the card
belong in the header, like a phone's status bar, with the Wi-Fi signal as
a vertical bar that "should visibly move"; callers as one small line, not
a hero; no wordmark; the lamps as square 80s LEDs in a line; the freed
space for who is on and who called; a letter when the sysop has mail and
something unmistakable while a caller is ringing; "the header should have
a lot of icons"; and the name and the IP rotating in the header with a
fade. This section is the spec for all of that. It supersedes the
revision 0 layout, header, hero, lamps and wordmark below. The revision 0
palette, 16 x 16 icons, type rules and travelling dot stand and are used
here.

Mock-ups, rendered 1:1 from the real Spleen arrays and written at 4x, in
the session scratchpad (`p_busy.png`, `p_quiet.png` and `p_long.png` are
the files Rob has open, overwritten; the revision 0 renders are kept as
`p1_*.png`):

- `p_busy.png`: four callers on, eight events, seven status glyphs lit.
- `p_quiet.png`: nobody on, no card, weak signal, two glyphs.
- `p_long.png`: a 21-glyph name and a 21-glyph address.
- `p2_busy_ip.png`, `p2_busy_up.png`: the header's second and third pages.
- `p2_fade.png`: mid-fade, step 5 of 8.
- `p2_ring.png`: a caller ringing for the sysop.
- `p2_offline.png`: no network, card error, held listing, restart warning.
- `p2_landscape.png`, `p2_landscape_quiet.png`: 320 x 172.

### The verdict on revision 1

It works, and it is a better panel than revision 0: a status bar a sysop
reads the way they read a phone's, then the two lists that answer "who is
here" and "what happened", one line of health, and a strip of LEDs. The
one cost is motion: the rotating header redraws its 168 x 16 slot sixteen
times per page turn, which is 24 KB/s averaged, a twentieth of the panel's
band budget and by far the biggest steady load on the glass. Acceptable;
measured; stated below so nobody discovers it as a mystery.

### Header: two rows, 42 px, then the track

Row 1, the bar, `R(0,0,w,22)` in 0x196F: one slot, `R(2,3,168,16)`
portrait (21 glyphs), `R(2,3,312,16)` landscape (39). Row 2, the band,
`R(0,22,w,20)` in #0e1a48 (0x08C9): the status glyphs packed from x=4,
then the Wi-Fi bar, then the clock at the right. The track `R(0,42,w,1)`
in 0x4BF3 with the dot on it, as revision 0.

The slot rotates through three pages, 3 s each, with a fade between:

| Page | Text | Colour | Note |
|---|---|---|---|
| A | the board name | ink | cut at a word boundary past half the slot; never mid-word when a space allows |
| B | `IP:port` | dial | never cut: 21 glyphs is the IPv4 maximum and the slot holds 21 |
| C | `up 12d 3h  7.4 GB` | ink | the uptime, two spaces, the card's free space; `up 12d 3h` alone with no card |

No network: page B shows `no network` in `--risk`. Shown, not skipped,
because a board off the air is the thing the desk most needs to see, and
the rhythm of the rotation should not change when it happens.

The fade: 8 steps, one every 60 ms (three panel ticks), 480 ms out and 480
ms in, so a page turn is 0.96 s and a full cycle is 10.4 s. Each step
redraws the slot only, with the text colour interpolated toward the bar
colour: unpack both to 8 bits a channel, `c = bar + (text - bar) * k / 8`,
repack with `rgb()`. Cost: 2,688 px (5,376 B, one band) a step, 16 steps
a turn, three turns a cycle: 258 KB per 10.4 s, 24 KB/s averaged. Nothing
else on the panel comes near it; the dot is 1.2 KB/s.

A live ring overrides the rotation: the slot shows `<handle> is ringing`
in `--busy` for as long as the ring lasts (the ring's own limit, 45 s at
most), then the rotation resumes at page A. Two redraws of the slot, one
at each end.

### The status row: glyphs that appear only when true

Packed left from x=4 on the band, each its own width plus a 3 px gap, in
this order, so
a quiet board shows one hollow card and a tower, and a busy one fills up.
Vertically centred in the 16 px text row (y = 24 + (16 - h) / 2). Each is
a silhouette, the phone convention at this size, with 1 px cut lines
where a line-art outline would blur. Colours are the site's tokens with
their site meaning. Every condition is an in-RAM figure; none reads a
file or walks the heap.

| # | Glyph | Size | Shown when | Colour | Flag it reads |
|---|---|---|---|---|---|
| 1 | SD card | 9x12 | always | dial filled with a card; dim hollow without; risk filled on a card error | `plat::sdInfo().mounted`, cached by the sd plugin (3 s); the error needs a flag from sd (hand-back) |
| 2 | bell | 9x11 | a caller is ringing the sysop | busy, blinking 2 Hz | the live ring the bus already holds one of |
| 3 | envelope | 11x8 | the sysop has unread mail (missed rings are delivered as mail from this release) | busy | the cheap flag the board will expose (hand-back) |
| 4 | upload | 9x10 | a file upload awaits approval | warm | `files` `g_pending` > 0 (RAM) |
| 5 | open padlock | 9x11 | the backup window is open | warm | the window's open flag |
| 6 | tower | 9x10 | announce is on and has a state | live when `online`, warm when `held`, risk otherwise | announce `g_state` (RAM) |
| 7 | person | 8x10 | a member of staff is logged in | risk for the sysop, yellow for a co-sysop (the rank colours) | a scan of the twelve sessions, once per 500 ms text pass |
| 8 | triangle bang | 9x9 | the last restart was not a clean one | risk | `plat::resetReason()` read once at boot; cleared when staff log in, as the reboots.log notice is |
| 9 | hourglass | 7x9 | a slow pass in the last 60 s | warm | the slow-pass counter SYS reports, compared with its value a minute ago |

The nine glyphs and their eight 3 px gaps are 104 px; the row has 114
before the Wi-Fi bar. The set cannot overflow.

```
sd (9x12) filled     sd hollow            mail (11x8)          bell (9x11)
XXXXXXX..            XXXXXXX..            XXXXXXXXXXX          ....X....
XXXXXXXX.            X......X.            X.XXXXXXX.X          ...XXX...
XX.X.X.XX            X.......X            XX.XXXXX.XX          ..XXXXX..
XX.X.X.XX            X.......X            XXX.XXX.XXX          ..XXXXX..
XX.X.X.XX            X.......X            XXXX.X.XXXX          ..XXXXX..
XXXXXXXXX            X.......X            XXXXXXXXXXX          .XXXXXXX.
XXXXXXXXX            X.......X            XXXXXXXXXXX          .XXXXXXX.
XXXXXXXXX            X.......X            XXXXXXXXXXX          XXXXXXXXX
XXXXXXXXX            X.......X                                 XXXXXXXXX
XXXXXXXXX            X.......X                                 .........
XXXXXXXXX            X.......X                                 ...XXX...
XXXXXXXXX            XXXXXXXXX

upload (9x10)        padlock (9x11)       tower (9x10)         person (8x10)
....X....            ...XXX...            ....X....            ...XX...
...XXX...            ..X...X..            ..X.X.X..            ..XXXX..
..XXXXX..            ..X...X..            .X..X..X.            ..XXXX..
.XXXXXXX.            ..X......            .X..X..X.            ...XX...
...XXX...            ..X......            ..X.X.X..            ........
...XXX...            XXXXXXXXX            ....X....            .XXXXXX.
...XXX...            XXXXXXXXX            ....X....            XXXXXXXX
X.......X            XXXX.XXXX            ...XXX...            XXXXXXXX
X.......X            XXXX.XXXX            ..XXXXX..            XXXXXXXX
XXXXXXXXX            XXXXXXXXX            .XXXXXXX.            XXXXXXXX
                     XXXXXXXXX

warn (9x9)           slow (7x9)
....X....            XXXXXXX
...XXX...            .X...X.
...X.X...            .XX.XX.
..XX.XX..            ..XXX..
..XX.XX..            ...X...
.XXX.XXX.            ..X.X..
.XXXXXXX.            .X...X.
XXXX.XXXX            .XXXXX.
XXXXXXXXX            XXXXXXX
```

The row is redrawn as one rect, `R(4,24,110,16)`, 1,760 px, one band,
whenever any flag changes. The bell's blink redraws its own 9 x 11 cell:
99 px four times a second, 792 B/s for the length of the ring.

The ring, stated: the bell blinks in `--busy` at 2 Hz and row 1 says who
is ringing, for the ring's duration and not a second longer. Not the bar
changing colour: that is a 7,224 px redraw per blink and it would fight
the fade.

### The Wi-Fi bar

`R(clockX - 10, 24, 6, 16)`: a 1 px outline, an inner well of 4 x 14. It
fills from the bottom, `h = (rssi + 90) * 14 / 40` clamped to 0..14, so
one pixel is 2.9 dB and the whole -90..-50 range is the well's height. No
hysteresis: Rob wants it to move, and at this size a pixel flickering is
the signal breathing, not a fault.

- Fill colour: live at -67 dBm and up, warm from -75 to -68, risk below.
  The same numbers as SYS's words and the strip's `wifi` effect.
- Outline: dim when joined. Not joined: outline in risk and no fill.
- Sampling: `plat::wifiRssi()` four times a second from the panel's tick,
  gated at 250 ms. One `esp_wifi_sta_get_ap_info` call each, a record
  copy, tens of microseconds. Redraw when `h` or the colour changes: 96
  px, 192 B, at most four times a second, 768 B/s worst case.

### Layout, portrait 172 x 320

Rob's order: header, then callers, then the lists, then the LEDs. Left
margin 4, right edge 168, row pitch 20 (16 of glyph, 4 of air).

```
      x: 0   8  16  24  32  40  48  56  64  72  80  88  96 104 112 120 128 136 144 152 160 168
  y      |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
  0  ####################################################################################  bar
  3  # S3 Demo  /  192.168.0.86:6400  /  up 12d 3h  7.4 GB                             #  slot 2..169, rotating
 22  ====================================================================================  band
 24  = [sd][mail][up][lock][tower][staff][slow]                            [bar]  16:16 =  glyphs from 4; bar 118..123; clock 128..167
 42  ---------------------------------------o--------------------------------------------  track, dot
 48      [callers] Callers 4/11                                                            heading, struct
 68       1] quantumrob                                                     2h            callers on, up to 4
 88       2) S3Bench                                                       12m
108       3* visitor                                                        4m
128       5> daytona                                                       51m
146      ---------------------------------------------------------------------------      rule in the air gap
148      [in]  16:20 visitor                                                              recent, 10 - callers rows
168      [in]  16:08 S3Bench
188      [bell] 15:41 quantumrob
208      [out] 15:12 zaphod
228      [in]  14:50 zaphod
248      [out] 14:02 daytona
266      ---------------------------------------------------------------------------
268      [chip] 118K          [handset] 23 today                                          system row
292      ---------------------------------------------------------------------------
298      [][][][][][][][][][]                                                             LEDs, cell 16, y 298..313
320
```

| Element | x | y | w | h | Face | Colour |
|---|---|---|---|---|---|---|
| bar | 0 | 0 | 172 | 22 | | bar 0x196F |
| slot | 2 | 3 | 168 | 16 | 8x16 | by page |
| band | 0 | 22 | 172 | 20 | | band 0x08C9 |
| glyph strip | 4 | 24 | 110 | 16 | bitmaps | by flag |
| Wi-Fi bar | 118 | 24 | 6 | 16 | | by RSSI |
| clock | 128 | 24 | 40 | 16 | 8x16 right | yellow; dim `--:--` |
| track | 0 | 42 | 172 | 1 | | track |
| heading icon | 4 | 48 | 16 | 16 | callers | struct; dim at 0 |
| heading | 24 | 48 | 144 | 16 | `Callers 4/11` | struct; dim at 0 |
| slot k, k = 0..9 | 4 | 68 + 20k | 164 | 16 | | lists |
| separator rule | 4 | 68 + 20a - 2 | 164 | 1 | | rule; only when a > 0 |
| rule | 4 | 266 | 164 | 1 | | rule |
| system row | 4 | 268 | 164 | 16 | | below |
| rule | 4 | 292 | 164 | 1 | | rule |
| LED row | 4 | 298 | 164 | 16 | | below |

The vertical budget: 22 + 20 + 1 (header) + 5 + 16 (heading) + 4 + 10 x
20 (slots, the separator rule inside an air gap) + 16 (system) + 8 + 1 +
5 + 16 (LEDs) + 6 = 320.

### Layout, landscape 320 x 172

The header is the same two rows (the slot is 39 glyphs and never cuts a
real name). Two columns under it, a full-width system row, one row of
LEDs.

| Element | x | y | w | h |
|---|---|---|---|---|
| slot | 2 | 3 | 312 | 16 |
| glyph strip | 4 | 24 | 110 | 16 |
| Wi-Fi bar | 266 | 24 | 6 | 16 |
| clock | 276 | 24 | 40 | 16 |
| heading | 4 (icon), 24 (text) | 48 | 152 | 16 |
| callers rows 1..3 | 4 | 68, 88, 108 | 152 | 16 |
| column rule | 160 | 48 | 1 | 76 |
| recent rows 1..4 | 166 (icon), 186 (text) | 48, 68, 88, 108 | 150 | 16 |
| rule | 4 | 126 | 312 | 1 |
| system row | 4 | 130 | 312 | 16 |
| rule | 4 | 150 | 312 | 1 |
| LED row | 10 | 154 | 300 | 16 |

Landscape rows: 3 callers, 4 recent. The system row has room for a
third figure there: `peak 6` (most lines busy at once since boot,
`peakNodes_`, RAM).

### The lists

Callers on now, then the recent events, in one run of slots. `a` is the
number of callers shown, `min(on, 4)` in portrait and `min(on, 3)` in
landscape, where "on" is what `shown()` already admits: visible, not
lurking, with a handle. The recent list takes the rest: `10 - a` slots in
portrait, 4 in landscape. A rule sits in the air gap under the last
caller row when `a > 0`. When `a` changes, every slot below the change is
redrawn: up to ten rows of 2,624 px, ten bands, 200 ms, on a login or a
logoff, which is the right moment to spend it.

A caller row, `x=4`: the node as `%2u` in the rank colour, the rank mark
in the rank colour, the handle in ink from x=36, and the time on right
aligned to 168 in dim. Rank colours are `markColor()`'s mapped to the
site: `]` sysop risk, `>` co-sysop yellow, `*` guest dim, `)` user ink.
Time on is `now - Session::loginAt` as `4m`, `51m`, `2h`, `1d`: at most
3 glyphs, so the handle has 12 glyphs (`(168 - 24 - 8 - 36) / 8`) and is
cut there; `BBS_USER_MAX` is 20. In landscape's 152 px column the handle
has 11. Callers are listed by node number, the first `a` of them; the
heading carries the true count.

A recent row: the event's icon at x=4 (login live, guest warm, logoff dim,
bell busy), `HH:MM handle` from x=24 in ink, dim, faint by age, with the
bell keeping its colour, as revision 0. The ring is 10 deep, 41 bytes an
entry, 410 B. With nothing in it the first slot shows the quiet ring and
`nothing yet`.

The heading: the callers icon and `Callers 4/11` in `--struct`, the
site's colour for headings and column names, and its first use on the
panel. Dim at `0/11`.

### What a sysop wants at a glance: the candidates, ranked

The rule: only a figure the board already holds in RAM, per the DASH cost
rule. Anything that reads a file or walks the heap is out, whatever it
would say.

| Rank | Want to know | Where it went | Source | Cost |
|---|---|---|---|---|
| 1 | somebody needs me: a ring now, mail or a missed ring, an upload waiting | bell, envelope, upload glyphs; the ring line in the slot | the bus's ring, the mail flag, `files` `g_pending` | RAM |
| 2 | who is on, and how long they have been on | the callers list with time on | the session pool, `Session::loginAt` | RAM |
| 3 | what happened | the recent list | the panel's own ring, fed by `onLogin`, `onLogoff`, `bus::lastPage` | RAM |
| 4 | am I on the air, and listed | the Wi-Fi bar, page B, the tower glyph | `wifiRssi`, `netInfo` (5 s), announce `g_state` | one driver call a quarter second; RAM |
| 5 | is the board healthy | the heap figure, the hourglass, the triangle, the red card | `plat::heapFree()` (a counter, not the walk), the slow-pass counter, `resetReason()`, the sd flag | RAM |
| 6 | is it exposed | the open padlock | the backup window's flag | RAM |
| 7 | how busy today | `23 today`; landscape adds `peak 6` | the count the core computes at each login (`bbs.cpp:1688`), captured in the panel's `onLogin`; `peakNodes_` | zero file reads by the panel; stale only across midnight until the next login, which the row's word "today" tolerates |
| 8 | staff present | the person glyph | a twelve-session scan every 500 ms | RAM |

Left out, and why:

- Busiest hour: CALLS buckets it by walking the caller log. A file pass.
- Loop worst and average: the hourglass carries the actionable half (a
  slow pass in the last minute); the microsecond figures are SYS's.
- Stack free: a high-water mark that only ever falls, logged to the
  console at each new low; not glanceable and not actionable from a desk.
- Heap or card as a bar: a figure in K is honest, a bar without a scale is
  not, and the figure's colour steps (ink above 40 K, warm to 20 K, risk
  below) are the warning.
- The card's free bar: page C has the figure every 10 s.
- The last restart reason as text: the triangle says "look", `reboots.log`
  and SYS say why.
- The strip's mode: the LEDs show it.
- DASH's RSSI number: the bar shows it, with more resolution than a number
  a sysop can read at 8 px.

### The system row

`R(4,268,164,16)` portrait, `R(4,130,312,16)` landscape.

- `[chip] 118K`: `plat::heapFree() / 1024`, refreshed at most every 5 s.
  Ink at 40 K and above, warm from 20 K, risk below. The counter read, not
  `plat::heap()`, which walks the pool under a critical section and is
  the exact call `heapWatch` was found paying for two versions.
- `[handset] 23 today`: the calls-today figure captured at login, plus one
  for each login since. `0 today` after a boot until the first caller.
- Landscape adds `[callers] peak 6`.

Redraw: the row, 2,624 px, one band, when a figure's text changes: the
heap at most once per 5 s, the calls once per login.

### The LEDs

One row, square, retro: `R(4,298,164,16)` portrait, `R(10,154,300,16)`
landscape. Cell `c = min(16, w / n)`, side `s = max(6, c - 4)`, the row
centred, each LED centred in its cell.

- Lit, colour k: an `s x s` square in k/3 (the 1 px glow edge) with an
  `(s-2) x (s-2)` core in k. At cell 16: 12 outer, 10 core.
- Off: a 1 px outline `(s-2) x (s-2)` in `--rule` 0x2967 round a fill in
  #14141b 0x10A3, inset one more than a lit LED, so lit ones swell.
- Colours from the lights frame through `ledLevel()`, as now.

| strip_count | portrait cell | side | row width | landscape cell |
|---|---|---|---|---|
| 1 to 10 | 16 | 12 | 16n (160 at ten) | 16 |
| 11 | 14 | 10 | 154 | 16 |
| 12 | 13 | 9 | 156 | 16 |
| 13 | 12 | 8 | 156 | 16 |
| 14 | 11 | 7 | 154 | 16 |
| 15 | 10 | 6 | 150 | 16 |
| 16 | 10 | 6 | 160 | 16 |

A lit LED is its 16 x 16 cell, 256 px, 512 B; the whole row of ten is
2,624 px, one band. A `rainbow` strip at 25 fps costs 131 KB/s, a quarter
of the 525 KB/s the disc lamps cost (which was over the panel's ceiling
and throttled by the queue), which is the other thing squares buy.

`stripGrid()` is no longer needed for the panel: the row is one line by
design. `stripCell()` becomes the cell arithmetic above.

### Motion, all of it

- The rotation's fade: 24 KB/s averaged, one band a step, 16 steps in
  each 3.48 s page.
- The Wi-Fi bar: up to 768 B/s, at most four transactions a second.
- The bell: 792 B/s during a ring only.
- The dot on the track: 1.2 KB/s, 25 transactions a second, yielding on
  any pass where the LED row queued (revision 0's rule).
- The clock: 128 px a minute.
- Nothing else moves. In the header the fade and the bar share the
  budget with room to spare: the panel can send 500 KB/s.

### What it costs

- Flash, data: nine status glyphs, 9 to 12 rows of 2 bytes each, 190 B;
  the two new 16 x 16 icons (chip, handset) 64 B on top of revision 0's
  256 B; no wordmark. Under 600 B of data in total. Code: the rotation
  and its fade, the status row, the bar, the two lists, the system row,
  the LED row: about 3 KB. No new font.
- Static RAM, all inside the S3-only plugin: the event ring 410 B; the
  slot's page, step and timer 8 B; nine flag bytes and their last-drawn
  copies 18 B; the RSSI sample, its timer and the last-drawn height 7 B;
  the heap figure and its timer 8 B; calls today 2 B; `g_shown` grows
  from 8 fields to about 18 (the slot, the clock, the heading, ten list
  slots, the system row's two figures, the glyph strip's packed state)
  at 48 B each, about 500 B more. Roughly 1 KB net.
- Redraw at rest, quiet board: the fade 24 KB/s, the dot 1.2 KB/s, the
  bar under 1 KB/s. About 26 KB/s and 35 transactions a second, against
  a ceiling of one band (10 KB) per 20 ms tick, 500 KB/s.

### Hand-backs for the builder

- Flags to expose, each a byte or a bool in RAM with no file behind it:
  sysop has unread mail (covers missed rings); a ring is live and from
  whom; the backup window is open; announce's `g_state` as a small enum;
  `files::pendingCount()`; the sd plugin's error state; the slow-pass
  counter (SYS has it); `plat::resetReason()` classified as clean or not.
- The calls-today figure: `Bbs` computes it at each login at
  `bbs.cpp:1688`; keep it in a member the panel's `onLogin` can read, so
  the panel never calls `calllog::countSince` itself.
- The event ring stores the kind, not the verb text: login, guest,
  logoff, page, ring. `bus::lastPage()` already tells ring from page.
- `Field` becomes: slot, clock, heading, ten list slots, two system
  figures, and the packed glyph strip as one field whose "text" is the
  flag bytes, so `put()`'s compare-and-redraw covers it unchanged.
- The rotation is a small state machine in `refreshText()`: page, step,
  and the time the page began; the fade's colour is a function of step.
  `tick()` already runs every 20 ms, so a 60 ms step is every third
  tick.
- The row-1 slot and the band's glyph strip repaint on their own
  background (bar and band), never `kBg`; `put()` already picks the
  background by field.
- `test_panel.cpp`: assert the slot holds 21 glyphs; the word-boundary
  cut; the fade's colour at k=0 is the text and at k=8 is the bar; the
  bar's fill height at -90, -89, -70, -50, -49 and 0; the glyph row's
  width with all nine flags set is under 114; the slot allocation for
  `on` = 0, 1, 4, 5 and 11; no LED cell overlaps another for n = 1..16
  in both boxes.

### Revision 1 implementation order

- Palette tokens and the LED row (replaces the disc lamps): visible in one
  sitting.
- The two-row header with the clock, the Wi-Fi bar and the SD glyph; the
  slot showing the name only.
- The heading and the two lists with the slot allocation; the system row.
- The rotation and its fade; the ring override.
- The remaining status glyphs as their flags are exposed, one at a time:
  mail, upload, padlock, tower, person, triangle, hourglass.
- Landscape.


## Revision 0 (2026-09-24, earlier the same day)

Everything from here down is the first spec, kept because revision 1
builds on it: the palette table, the eight 16 x 16 line-art icons, the
type rules, the travelling dot and the hand-back list still apply.
Superseded by revision 1: the two-row header with the fan, the hero
figure, the round lamps, the wordmark, and both layout tables.

### The verdict

The bones are right and stay: the blue bar with the name and the clock, the
big green figure, ten lamps in two rows, a framebuffer redrawn by region and
sent a band a tick. What is wrong is everything between the figure and the
lamps: four lines of the same 8 x 16 face at four arbitrary colours, a
two-line event field that is one line tall in use, and then 70 blank rows,
22% of the glass, before ten bright hollow rings that are the loudest thing
on a quiet board. Fixable in one plugin with no new font and under 600
bytes of new data: the site's palette replaces eight ad hoc colours, eight
16 x 16 icons replace the label words, three rulers and a 22 px row pitch
replace the wall of text, the last three events replace the last one, the
lamps get a glow and a proper off state, a Wi-Fi fan goes beside the clock,
and the wordmark takes the foot of the screen. The result is the site on a
stick.

## Findings, worst first

### 1. A third of the glass says nothing

`layout()` portrait: `F_EVENT` is `R(4,142,164,32)`, two lines for a
string that fits on one, and the strip is `R(0,180,172,136)`, in which ten
lamps take 64 rows and the other 72 are split above and below them. In the
photo the band from the event line to the first lamp is 70 px.

Row budget today (portrait, y):

```
  0  bar 20
 28  callers on
 46  1 of 11 (32 tall)
 88  address
106  uptime
124  card
142  event, 32 tall, using 16
180  ----- strip box 136 tall -----
216    lamps 64 tall
280  -----
320
```

Fix: the budget in section "Layout, portrait". Rules at 84, 134 and 206
divide the glass into hero, dial, events, lamps; the events grow to three
rows; the wordmark takes rows 288 to 311. Nothing is padded to fill.

### 2. Colour is decoration, not meaning

Address cyan, uptime grey, card orange, event white, figure green. Five
colours on five rows that mean nothing by their colour, and none of them
is the site's: `kGreen` #50F050 against `--live` #5ddc7a, `kCyan` #60D8FF
against `--dial` #7fd4ff, `kAmber` #FFA020 against `--warm` #e0a94e,
`kYellow` #FFD840 against the banner's #ffd35c, `kGrey` #909090 against
`--dim` #8a8a8a. Close enough that they look like a slightly wrong copy.

Fix: the token table in "Colour". Text is `--ink`; only the address is
`--dial` (the site's "things you can act on": dial it); only the figure is
`--live` (the site's "up, and nothing else"); age is the site's three
greys; a page is `--busy`; a guest is `--warm`.

### 3. The off lamps are the brightest thing on a quiet board

`lamp()` draws a 2 px rim in `kRim` #484848 round `kUnlit` #141414. Ten of
those at 30 px across (cell 34, r = 15) are ten bright rings, and in the photo they outweigh
the address. An off lamp should be found, not seen.

Fix: unlit is a 2 px ring in `--rule` #2c2c38 (RGB565 0x2967) round a fill
of #14141b (0x10A3), two radii smaller than a lit lamp. Lit is the disc
plus a two-step glow ring so a lit lamp swells past the off ones. Section
"The strip".

### 4. No signal indicator

Rob's ask. The lights plugin already reads `plat::wifiRssi()` once a
second in its `wifi` effect and already fixes the thresholds: -67 dBm good,
-75 fair, below weak. The panel uses the same numbers so the fan and the
strip never disagree. Section "Wi-Fi".

### 5. The address can clip, and a clipped address is a wrong number

Today `F_ADDR` is `R(4,88,164,16)`: 20 glyphs. `255.255.255.255:65535` is
21. Unlikely on a home network, but with an icon in front the box drops
to 18 glyphs, and `192.168.1.100:6400` is 18 exactly while
`192.168.100.100:6400` is 20. That last shape is common.

Fix: the address row has two forms. Up to 18 glyphs: globe at x=4, text
from x=24. Over 18: no globe, text from x=4, which holds 21 glyphs in 168
px. `line()` already cuts at whole glyphs; this rule means it never has to.

### 6. The board name is cut mid-word

`line()` cuts at a whole glyph. "The Rusty Antenna" in an 11 glyph box is
"The Rusty A". With the fan in the bar the portrait name box is 88 px, 11
glyphs (was 14).

Fix: cut at the last space that lands at or past half the box; otherwise
at the glyph. "The Rusty Antenna" becomes "The Rusty". Landscape has 30
glyphs and never cuts a real name.

### 7. The event line spends its width on a verb

"16:08 logoff S3Bench" is 20 glyphs, which is why the field was given two
lines. An icon carries the verb in 16 px and the line is "16:08 S3Bench",
13 glyphs, one row. That is what pays for three events instead of one.

## Layout, portrait 172 x 320

Every box as `x, y, w, h`. Text sits on the top row of its box; every text
box is 16 tall for the 8 x 16 face and 32 for the 16 x 32. Left margin 4,
right edge 168, so every rule and every row is 164 wide. Row pitch 22 (16
of glyph, 6 of air).

```
      x: 0   8  16  24  32  40  48  56  64  72  80  88  96 104 112 120 128 136 144 152 160 168
  y      |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
  0  ####################################################################################  bar
  3  #  S3 Demo                                                  (fan)    16:16  #  name 4..91, fan 105..121, clock 128..167
 21  ####################################################################################
 22  =========================================o==========================================  track, dot
 28                     [callers]  callers on                                              icon 35..50, label 57..136
 46                    1    o f    1 1                                                     16x32, centred in 0..171
 84      ----------------------------------------------------------------------------      rule 4..167
 90      [globe] 192.168.0.86:6400                                                         icon 4..19, text 24..167
112      [clock] 14m                                        [card] 7.4 GB                  text 24..79; card text right to 168
134      ----------------------------------------------------------------------------
140      [login] 16:08 S3Bench                                                             ink
162      [logoff] 16:02 S3Bench                                                            dim
184      [bell]  15:41 quantumrob                                                          faint (bell stays busy)
206      ----------------------------------------------------------------------------
212      (  6..37 )( 38..69 )( 70..101)(102..133)(134..165)                                lamps row 1, y 212..243
244      (        )(        )(        )(        )(        )                                lamps row 2, y 244..275
288                      µ N L E A S H E D                                                 24..147, y 288..311
320
```

| Element | x | y | w | h | Face | Align | Colour |
|---|---|---|---|---|---|---|---|
| bar | 0 | 0 | 172 | 22 | | | bar 0x196F |
| name | 4 | 3 | 88 | 16 | 8x16 | left | ink on bar |
| fan | 105 | 6 | 17 | 10 | bitmap | | by step |
| clock | 128 | 3 | 40 | 16 | 8x16 | right | yellow on bar |
| track | 0 | 22 | 172 | 1 | | | track 0x4BF3 |
| dot | x-1 | 21 | 3 | 3 | | moving | dial, white centre |
| callers icon | 35 | 28 | 16 | 16 | bitmap | | dim |
| label | 57 | 28 | 80 | 16 | 8x16 | | dim |
| hero | 0 | 46 | 172 | 32 | 16x32 | centre | live, dim when 0 |
| rule | 4 | 84 | 164 | 1 | | | rule |
| globe | 4 | 90 | 16 | 16 | bitmap | | dial |
| address | 24 | 90 | 144 | 16 | 8x16 | left | dial |
| address, long | 4 | 90 | 164 | 16 | 8x16 | left | dial |
| clock icon | 4 | 112 | 16 | 16 | bitmap | | dial |
| uptime | 24 | 112 | 56 | 16 | 8x16 | left | ink |
| card icon | 168-w-20 | 112 | 16 | 16 | bitmap | | dial, faint if none |
| card | 168-w | 112 | w | 16 | 8x16 | right | ink, faint if none |
| rule | 4 | 134 | 164 | 1 | | | rule |
| event 1 icon | 4 | 140 | 16 | 16 | bitmap | | by kind |
| event 1 | 24 | 140 | 144 | 16 | 8x16 | left | ink |
| event 2 | 24 | 162 | 144 | 16 | 8x16 | left | dim |
| event 3 | 24 | 184 | 144 | 16 | 8x16 | left | faint |
| rule | 4 | 206 | 164 | 1 | | | rule |
| strip box | 4 | 212 | 164 | 64 | | | |
| wordmark | 24 | 288 | 124 | 24 | bitmap 2x | centre | six-row sweep |

Where the numbers come from:

- The label and its icon are centred as one unit: 16 + 6 + 80 = 102, so
  x = (172 - 102) / 2 = 35.
- The hero centres in the full width: "1 of 11" is 7 glyphs, 112 px;
  "10 of 11" is 128.
- Uptime is at most 7 glyphs (56 px) and ends at 80; the card icon is never
  left of 100 (6 glyph card, 48 px: icon at 168 - 48 - 4 - 16 = 100). The
  two cells cannot touch.
- Events: 144 px is 18 glyphs, "HH:MM " plus a 12 glyph handle. A longer
  handle is cut at 12 (`BBS_USER_MAX` is 20). Landscape cuts at 10.
- The strip box is 164 x 64; `stripGrid()` unchanged scores 2 x 5 at cell
  32 as 2048 against 1 x 10 at 16 (1024) and 3 x 4 at 21 (933). Lamps land
  at x = 6 + 32k, y = 212 and 244. No layout change needed for the grid.
- The wordmark is 62 x 12 pixels drawn at 2 px a pixel: 124 x 24, at
  x = (172 - 124) / 2 = 24. Bottom margin 8.

Vertical rhythm, in rows: bar 22, track 1, air 5, label 16, air 2, hero
32, air 6, rule, air 5, row, row, air 5, rule, air 5, row, row, row, air 5,
rule, air 5, lamps 64, air 12, wordmark 24, margin 8. Total 320.

## Layout, landscape 320 x 172

The address goes on a full-width row under the bar (the column form clips
it at 15 glyphs), the strip is one row of ten along the bottom (a strip is
a line), and the hero and the events share the middle.

```
      x: 0        40       80      120      160      200      240      280      320
  y      |        |        |        |        |        |        |        |        |
  0  ############################################################################  bar 22
  3  #  S3 Demo                                                (fan)  16:16   #  name 4..243, fan 253..269, clock 276..315
 22  ==========================================o=================================  track
 27                          [globe] 192.168.0.86:6400                            centred as one unit
 47      --------------------------------------------------------------------      rule 4..315
 52         [callers] callers on          |  [clock] 14m         [card] 7.4 GB    left col 4..155, right col 166..315
 70            1  o f  1 1                |  [login]  16:08 S3Bench               column rule x=160, y 52..131
 96                                       |  [logoff] 16:02 S3Bench
108        µ N L E A S H E D              |  [bell]   15:41 quantumrob            wordmark 18..141, y 108..131
136      --------------------------------------------------------------------      rule
140      ( )( )( )( )( )( )( )( )( )( )                                           10 cells of 30, x 10..309, y 140..169
172
```

| Element | x | y | w | h |
|---|---|---|---|---|
| bar | 0 | 0 | 320 | 22 |
| name | 4 | 3 | 240 | 16 |
| fan | 253 | 6 | 17 | 10 |
| clock | 276 | 3 | 40 | 16 |
| track | 0 | 22 | 320 | 1 |
| globe + address | centred: icon at (320 - (22 + 8n)) / 2 | 27 | 22 + 8n | 16 |
| rule | 4 | 47 | 312 | 1 |
| callers icon + label | 4 + (152 - 102) / 2 = 29 | 52 | 102 | 16 |
| hero | 4 | 70 | 152 | 32 |
| wordmark | 18 | 108 | 124 | 24 |
| column rule | 160 | 52 | 1 | 80 |
| clock icon, uptime | 166, 188 | 52 | 16, 56 | 16 |
| card icon, card | 316-w-20, 316-w | 52 | 16, w | 16 |
| events 1..3 icon | 166 | 74, 96, 118 | 16 | 16 |
| events 1..3 | 188 | 74, 96, 118 | 128 | 16 |
| rule | 4 | 136 | 312 | 1 |
| strip box | 10 | 140 | 300 | 30 |

The uptime cell ends at 244 and the card icon is never left of 246 (6 glyph
card: 316 - 48 - 4 - 16 = 248). `stripGrid()` on 300 x 30 gives 1 x 10 at
cell 30. `test_panel.cpp` asserts a strip at least 24 tall: 30 passes.

## The header bar

Kept: blue, name left, clock right. Two changes.

- 22 rows, not 20. The 16 px face had 2 px above and below; it has 3 now.
  In the photo the clock sits on the bar's edge.
- A 1 px track under it in `--dial` at 60%, RGB565 0x4BF3. The site's run
  card is a `--dial` wash with a `--dial` border at 0.6 alpha; this is that
  border. It is also the rail the dot runs on.

Name box: `x=4, w = fanX - 6 - 4` rounded down to a multiple of 8. Portrait
88 px (11 glyphs), landscape 240 (30). Clip rule as finding 6. Ink on bar:
contrast 7.5:1 against 0x196F.

Clock box: `R(w-44, 3, 40, 16)`, right aligned, `--yellow` #ffd35c
(0xFE8B), which is the site's yellow (the banner and the focus ring) and
within a shade of today's. `--:--` in `--dim` while the clock is not valid,
not in yellow: yellow is for a real time.

No gradient on the bar. A vertical sweep over 22 rows is three visible
bands of blue and looks like a mistake; the site's bars are flat. The
wordmark at the foot carries the sweep instead.

No micro sign in the bar. It was tried at 12 x 12 in `--name` before the
name and cost three glyphs of name. The wordmark at the foot is the
identity, at 20 px cap height rather than 12.

## Wi-Fi

Position: `R(clockX - 6 - 17, 6, 17, 10)`, between the name and the clock,
the phone convention. 17 x 10, a 2 px dot and three 2 px arcs on a 90
degree fan, radii 3, 6 and 9 about a centre 0.2 px below the box.

```
   layer map, 17 x 10 (. off, 0 dot, 1 2 3 arcs)
   ......33333......
   ....333333333....
   ..3333.....3333..
   ..33..22222..33..
   ....222222222....
   ....222...222....
   .......111.......
   ......11111......
   .......000.......
   .......000.......
```

Store it as four 1-bit planes of 10 rows x 3 bytes (120 bytes), or one
17 x 10 byte map (170 bytes). Drawing is `fill(box, kBar)` then one pixel
loop.

Steps and colours, aligned with `lights.cpp` so the fan and the strip's
`wifi` effect never contradict each other:

| Step | Lit | RSSI | Colour |
|---|---|---|---|
| 4 | dot + 3 arcs | >= -60 dBm | live 0x5EEF |
| 3 | dot + 2 arcs | -67 to -61 | live |
| 2 | dot + 1 arc | -75 to -68 | warm 0xE549 |
| 1 | dot | < -75 | risk 0xE36D |
| none | dot | not joined (rssi 0) | dot risk, arcs faint 0x6B4E |

Unlit arcs are always drawn, in `--faint`, so the fan keeps its shape at
one bar. Hysteresis: a step is gained at its threshold and lost 3 dB below
it (-63, -70, -78 going down), which stops a reading sitting on -67
flickering the colour. Redraw only when the step changes: 170 px, one
band. Cost rule: `plat::wifiRssi()` once a second, gated inside
`refreshText()` (which already runs every 500 ms), the same one driver
call the lights plugin makes. Not from `netInfo()`, which also reads the
netif and runs every 5 s.

When not joined the address row says `no network` in `--dim` with the
globe in `--faint`; the fan's red dot is the only red on the screen.

## Icons

Eight, all 16 x 16, 1 bit, 2 px pen with round caps, drawn to the site's
`.o` hand (stroke 1.4 in a 56 box on a 96 ppi monitor is 1.4 to 2.8 px;
on 247 ppi that weight is 2 px). One fill each at most, the site's "a dot
is the only fill". Rasterised from geometry with a 50% coverage rule, so
they are symmetric. Each row is one 16-bit word, MSB left: 32 bytes an
icon, 256 bytes for the set.

```
callers                globe                  clock                  card
................       ................       ................       ...XXXXXXXX.....
................       ....XXXXXXXX....       ....XXXXXXXX....       ...XXXXXXXXX....
......XXXX......       ...XXXXXXXXXX...       ...XXXXXXXXXX...       ...XX.....XXX...
.....XXXXXX.....       ..XXXXX..XXXXX..       ..XXX..XX..XXX..       ...XX.XX.XXXXX..
.....XXXXXX.....       .XXX.X....X.XXX.       .XXX...XX...XXX.       ...XX.XX.XX.XX..
.....XXXXXX.....       .XX.XX....XX.XX.       .XX....XX....XX.       ...XX.XX.XX.XX..
.....XXXXXX.....       .XX.XX....XX.XX.       .XX....XX....XX.       ...XX.......XX..
......XXXX......       .XXXXXXXXXXXXXX.       .XX....XXXXX.XX.       ...XX.......XX..
.......XX.......       .XXXXXXXXXXXXXX.       .XX....XXXXX.XX.       ...XX.......XX..
....XXXXXXXX....       .XX.XX....XX.XX.       .XX..........XX.       ...XX.......XX..
...XXXX..XXXX...       .XX.XX....XX.XX.       .XX..........XX.       ...XX.......XX..
..XXX......XXX..       .XXX.X....X.XXX.       .XXX........XXX.       ...XX.......XX..
.XXX........XXX.       ..XXXXX..XXXXX..       ..XXX......XXX..       ...XX.......XX..
.XX..........XX.       ...XXXXXXXXXX...       ...XXXXXXXXXX...       ...XX.......XX..
.XX..........XX.       ....XXXXXXXX....       ....XXXXXXXX....       ...XXXXXXXXXXX..
XX............XX       ................       ................       ...XXXXXXXXXXX..

login                  logoff                 bell                   quiet
..........XXX...       ...XXX..........       ................       ................
.........XXXXX..       ..XXXXX.........       ................       ................
..........XXXX..       ..XXXX..........       ......XXXX......       .....XXXXXX.....
............XX..       ..XX............       ....XXXXXXXX....       ...XXXXXXXXXX...
......X.....XX..       ..XX.......X....       ...XXX....XXX...       ...XX......XX...
......XX....XX..       ..XX.......XX...       ...XX......XX...       ..XX........XX..
.......XX...XX..       ..XX........XX..       ...XX......XX...       ..XX........XX..
.XXXXXXXXX..XX..       ..XX..XXXXXXXXX.       ..XXX......XXX..       ..XX........XX..
.XXXXXXXXX..XX..       ..XX..XXXXXXXXX.       ..XXX......XXX..       ..XX........XX..
.......XX...XX..       ..XX........XX..       ..XXX......XXX..       ..XX........XX..
......XX....XX..       ..XX.......XX...       ..XX........XX..       ..XX........XX..
......X.....XX..       ..XX.......X....       ..XXXXXXXXXXXX..       ...XX......XX...
............XX..       ..XX............       .XXXXXXXXXXXXXX.       ...XXXXXXXXXX...
..........XXXX..       ..XXXX..........       .......XX.......       .....XXXXXX.....
.........XXXXX..       ..XXXXX.........       .......XX.......       ................
..........XXX...       ...XXX..........       .......XX.......       ................
```

Rows as words:

```
callers  0x0000,0x0000,0x03C0,0x07E0,0x07E0,0x07E0,0x07E0,0x03C0,0x0180,0x0FF0,0x1E78,0x381C,0x700E,0x6006,0x6006,0xC003
globe    0x0000,0x0FF0,0x1FF8,0x3E7C,0x742E,0x6C36,0x6C36,0x7FFE,0x7FFE,0x6C36,0x6C36,0x742E,0x3E7C,0x1FF8,0x0FF0,0x0000
clock    0x0000,0x0FF0,0x1FF8,0x399C,0x718E,0x6186,0x6186,0x61F6,0x61F6,0x6006,0x6006,0x700E,0x381C,0x1FF8,0x0FF0,0x0000
card     0x1FE0,0x1FF0,0x1838,0x1B7C,0x1B6C,0x1B6C,0x180C,0x180C,0x180C,0x180C,0x180C,0x180C,0x180C,0x180C,0x1FFC,0x1FFC
login    0x0038,0x007C,0x003C,0x000C,0x020C,0x030C,0x018C,0x7FCC,0x7FCC,0x018C,0x030C,0x020C,0x000C,0x003C,0x007C,0x0038
logoff   0x1C00,0x3E00,0x3C00,0x3000,0x3010,0x3018,0x300C,0x33FE,0x33FE,0x300C,0x3018,0x3010,0x3000,0x3C00,0x3E00,0x1C00
bell     0x0000,0x0000,0x03C0,0x0FF0,0x1C38,0x1818,0x1818,0x381C,0x381C,0x381C,0x300C,0x3FFC,0x7FFE,0x0180,0x0180,0x0180
quiet    0x0000,0x0000,0x07E0,0x1FF8,0x1818,0x300C,0x300C,0x300C,0x300C,0x300C,0x300C,0x1818,0x1FF8,0x07E0,0x0000,0x0000
```

What each means and wears:

| Icon | Where | Colour |
|---|---|---|
| callers | before "callers on" | dim |
| globe | address row | dial (faint with no network) |
| clock | uptime | dial |
| card | card free | dial (faint with no card) |
| login | an account login; also a guest | live; warm for a guest |
| logoff | a logoff | dim |
| bell | a page, or the sysop rung | busy, and stays busy as it ages |
| quiet | no events yet | faint |

The card's two contacts are 2 x 3 px, the one place a detail is under 2 px
tall, and it is a texture inside a 2 px outline rather than a line on its
own. The globe's meridian is a 1.6 px pen and reads as the site's `.d`
detail stroke.

A generic `blit1(canvas, x, y, rows, fg)` that paints only set bits is the
one primitive needed: the field's `fill(box, bg)` has already cleared
under it.

## Type

- 8 x 16 Spleen for every line of figures, as now.
- 16 x 32 Spleen for the hero, as now.
- No larger digit face. A 24 x 48 face makes "10 of 11" 192 px in a 172
  px screen, and "1/11" at that size loses the words. The hero is already
  twice everything else and 3.3 mm tall on this glass, which reads across
  a desk. The width budget, not taste, decides it.
- The hero centres in the full width. The label and its icon centre as one
  unit above it. Stats and events are left aligned on x=24 with the icon
  column on x=4; the card and the clock are right aligned on 168. Four
  alignments, each used for one thing.
- Uptime is written to fit 7 glyphs: `14m` under an hour, `3h 14m` under a
  day, `12d 3h` under 100 days, `123d` after. Today's `up 00:14` beside a
  clock icon reads as a second clock.
- Card: `7.4 GB`, `29 GB`, `512 MB`, at most 6 glyphs; `--` with the icon
  and text in `--faint` when there is no card. Today's `card 7.4 GB free`
  is 16 glyphs to say what an icon and 6 glyphs say.
- Events: `HH:MM handle`. The icon is the verb.

## Colour

The site's tokens, packed by `panelgfx::rgb()`. Third column is what the
565 quantisation gives back, so nobody is surprised on the glass.

| Token | Site | RGB565 | On glass | Used for |
|---|---|---|---|---|
| bg | #0b0b0f | 0x0000 | #000000 | background (black; the site's near-black is the same on an IPS) |
| surface | #14141b | 0x10A3 | #101418 | unlit lamp fill |
| rule | #2c2c38 | 0x2967 | #292c39 | rules, column rule, unlit lamp ring |
| ink | #c8c8c8 | 0xCE59 | #cecbce | board name, newest event, uptime, card |
| dim | #8a8a8a | 0x8C51 | #8c8a8c | "callers on", second event, hero at 0, logoff icon |
| faint | #6a6a72 | 0x6B4E | #6b6973 | third event, quiet icon, unlit fan arcs, no-card, no-network |
| live | #5ddc7a | 0x5EEF | #5adf7b | the hero, the login icon, the fan at 3 and 4 |
| warm | #e0a94e | 0xE549 | #e7aa4a | a guest's login icon, the fan at 2 |
| dial | #7fd4ff | 0x7EBF | #7bd7ff | the address, the stat icons, the dot |
| track | dial x 0.6 | 0x4BF3 | #4a829c | the rail under the bar |
| busy | #ef8b5a | 0xEC4B | #ef8a5a | the bell |
| risk | #e06c6c | 0xE36D | #e76d6b | the fan's dot when weak or not joined, nothing else |
| yellow | #ffd35c | 0xFE8B | #ffd35a | the clock |
| bar | #182c78 | 0x196F | #182c7b | the bar, kept |
| wm1..6 | #e2d4ff #b48ef0 #8f7ae8 #6f84e0 #4a7fc8 #3f6cab | 0xE6BF 0xB47E 0x8BDD 0x6C3C 0x4BF9 0x3B75 | | the wordmark's six rows |

Not used: `--name` #b48ef0 (it appears only inside the wordmark, where it
belongs) and `--struct` #4ce0e0 (the panel has no column headings).

Contrast in a lit room, at 60% backlight (WCAG ratios on the site's
values): ink on bar 7.5:1, yellow on bar 8.8:1, dim on black 6.1:1, faint
on black 3.9:1 (the third event is meant to recede), dial on black 12.8:1,
live on black 12.0:1, warm on black 10.0:1. The rule at #2c2c38 on black is
1.5:1 and that is what a hairline is; if it vanishes on the real glass,
the next step up is `--faint` at 1 px, not a 2 px rule.

The lamps' own colours come from the lights frame through `ledLevel()` as
now, so a WHO rank colour on the strip is that colour on the glass.

## The strip

Box `R(4, 212, 164, 64)` portrait, `R(10, 140, 300, 30)` landscape.
`stripGrid()` is unchanged and produces, for every `strip_count`:

| n | portrait 164 x 64 | landscape 300 x 30 |
|---|---|---|
| 1 | 1 x 1, cell 64 (drawn as 40) | 1 x 1, cell 30 |
| 2 | 2 x 1, cell 64 (40) | 2 x 1, 30 |
| 3 | 3 x 1, cell 54 (40) | 3 x 1, 30 |
| 4 | 4 x 1, cell 41 (40) | 4 x 1, 30 |
| 5 | 5 x 1, cell 32 | 5 x 1, 30 |
| 6 | 3 x 2, cell 32 | 6 x 1, 30 |
| 7 | 4 x 2, cell 32 (4 over 3) | 7 x 1, 30 |
| 8 | 4 x 2, cell 32 | 8 x 1, 30 |
| 9 | 5 x 2, cell 32 (5 over 4) | 9 x 1, 30 |
| 10 | 5 x 2, cell 32 | 10 x 1, 30 |
| 11, 12 | 6 x 2, cell 27 | 11 x 1 at 27, 12 x 1 at 25 |
| 13, 14 | 7 x 2, cell 23 | 13 x 1 at 23, 14 x 1 at 21 |
| 15 | 5 x 3, cell 21 | 15 x 1, 20 |
| 16 | 8 x 2, cell 20 | 16 x 1, 18 |

One rule to add: a cell is capped at 40. One lamp at cell 64 is a 60 px
saucer; at 40 it is a lamp.

A lamp in a cell of size s, centred at the cell's centre, `r = s/2 - 2`:

- Lit, colour c: `disc(r, c/6)`, `disc(r-1, c/3)`, `disc(r-2, c)`. The two
  outer rings are the glow, computed per channel on the 8-bit values
  before packing (`live` gives 0x0922 and 0x1A45 round 0x5EEF). A lit lamp
  reaches radius r; an off lamp stops at r-2, so lit lamps swell.
- Unlit: `disc(r-2, rule 0x2967)`, `disc(r-3, surface 0x10A3)`. A 1 px ring
  at the site's rule grey with a near-black fill. Found, not seen.
- A cell under 14 (r < 5, which needs n >= 15 on landscape): lit is one
  disc of c, unlit is one disc of rule. No room for a glow.

At cell 32: lit disc r=12 with a 2 px glow to r=14; unlit ring r=12 round
fill r=11. Cell 30 (landscape): lit r=11, glow to 13.

Redraw cost is what it is today: a lamp is its cell, 32 x 32 = 1024 px, 2
KB, and `changed > 3` still sends the whole box, 164 x 64 = 10,496 px,
which is 3 bands (a band is 5,120 px; a full-width portrait band is 29
rows).

## Motion

One dot on the track under the bar: the run card's lamp, the same object.

- 3 x 3 in `--dial` 0x7EBF at `(x-1, 21)`, centre pixel white 0xFFFF, and
  a three pixel tail on row 22 at `(x-1)` 0x5475, `(x-2)` 0x2A2A, `(x-3)`
  0x1105 (dial at 4/6, 2/6, 1/6).
- 1 px per 40 ms, left to right, wrapping at the right edge. 172 px is 6.9
  s a crossing in portrait, 12.8 s in landscape. The site's row hover dot
  crosses a name in 1 s and the run card laps in 20 s; this sits between
  and reads as calm. Not two dots half a lap apart: on a straight rail two
  dots read as a progress bar.
- Redraw: `unite(old, new)` is `R(x-4, 21, 8, 3)`, 24 px, 48 bytes, one
  `lcdDraw` a step: repaint row 21 in bar, row 22 in track, row 23 in bg,
  then the tail and the dot. 25 transactions a second, 1.2 KB/s. It moves
  from `refreshStrip()` on the 40 ms cadence that already exists.
- It yields. On a pass where the strip queued more than one cell, the dot
  does not step. A `rainbow` strip at 25 fps wants 3 bands per 40 ms
  against the 2 the tick allows; with the dot standing aside the strip
  gets both and the queue's `contains` dedupe holds the glass a frame
  behind the framebuffer rather than growing. With `nodes`, the shipped
  effect, nothing changes for minutes and the dot has the rail to itself.
- Nothing else moves. The clock's colon blinking was considered (128 px a
  second) and rejected: one moving thing says alive, two say busy.

## What it costs

- Flash, data: icons 256 B, fan 120 B, wordmark 96 B plus 12 B of colour,
  under 500 B. Code: a bit blit, the fan, the wordmark, an event ring, the
  dot, and the layout arithmetic, about 2 KB. No new font: 0.
- Static RAM, all inside the panel plugin, which is compiled only for the
  S3 (512 KB of SRAM, 8 MB PSRAM) and is empty on the WROOM: the event
  ring 3 x (1 kind + 40 text) = 123 B replacing today's 48; two more
  `g_shown` entries, 96 B; the dot's x, 2 B; the RSSI step and its
  timestamp, 5 B. About 180 B net.
- Framebuffer: unchanged, 110 KB in PSRAM.
- Redraw, at rest with the `nodes` effect: the dot at 1.2 KB/s and 25 DMA
  transactions a second; the clock once a minute, 128 px; the fan only on
  a step change, 170 px; a login, one lamp (1024 px) and three event rows,
  each its own 164 x 16 rect of 2,624 px, one band each. A full redraw at start is 55,040
  px, 11 bands, 220 ms.
- Panel time in the BBS loop: the same one band a tick, never waited on.
  Nothing here changes the scheme.

## What stays as it is

- The bar: blue 0x196F, name in ink on the left, clock in yellow on the
  right. Rob likes it and it is right: it is the one place the panel is a
  device and not a page.
- The hero: "callers on" over "1 of 11" in 16 x 32, `--live`, centred.
  Already the focal point in the photo.
- Two rows of five. `stripGrid()` picks it by its own scoring in the new
  box; it is not hard-coded.
- The redraw scheme: figures recomposed twice a second, redrawn only on
  change; the strip every 40 ms, a lamp at a time; one band a tick by DMA.
  Every element above is specified inside it.
- `ledLevel()`: a 10% strip frame reading as lit on glass is the right
  correction and the glow rings sit on top of it, not instead of it.
- Spleen at 8 x 16 and 16 x 32: two faces, no third.
- PANEL's text report of what the glass shows: extend it to list three
  events and the fan's step, so the tests keep reading the panel without
  a panel.

## Implementation order

Cheapest and most visible first. Each step is on the glass on its own.

- Tokens. Replace `kWhite kGrey kYellow kGreen kCyan kAmber kRim kUnlit`
  with the table; unlit lamp as ring-in-rule over surface, lit lamp with
  the two glow rings. One sitting, and the board looks like the site
  before anything moves.
- The row budget. Bar to 22 with the track; the three rules; icons on the
  three stat rows; uptime and card formats; the card right aligned; the
  address rule with its two forms; the name's word-boundary cut. This
  removes the dead band.
- Three events. `F_EVENT` becomes three fields with a kind each; the ring;
  newest first in ink, dim, faint; the bell keeps its colour; the quiet
  state. Extend PANEL and `test_panel.cpp`.
- The fan. `plat::wifiRssi()` once a second in `refreshText()`, the step
  function with hysteresis (pure, unit test it), redraw on change.
- The wordmark at the foot, drawn once in `redrawAll()`.
- The dot, from `refreshStrip()`, with the yield rule.
- Landscape: the address row, the two columns, the one-row strip, and the
  cap of 40 on a cell. Check `test_panel.cpp`'s "no two figures overlap"
  against the new boxes for both orientations.

Hand-backs for whoever builds it, since none of this is a code change I
can make:

- `Field` grows to `F_EVENT1..3` and the layout needs an icon per field
  (`Layout::icon[f]`, `Layout::iconColour` set at `put()` time), or a
  `putIcon(f, icon, iconFg, text, textFg)` beside `put()`. Both keep the
  "redraw only on change" contract if the compare covers the icon too.
- The event ring wants the kind stored, not derived from the text: login,
  guest, logoff, page, ring. `bus::lastPage()` already distinguishes ring
  from page.
- `stripCell()` gets the cap of 40 on `sz` in one place.
- The fan and the dot both paint over the bar, so they must repaint with
  `kBar` behind them, never `kBg`; `put()` already makes that choice for
  `F_NAME` and `F_CLOCK`.
- `test_panel.cpp` should assert: every icon box is disjoint from every
  text box; the address row's long form holds 21 glyphs; the name cut
  never lands mid-word when a space is available past half the box; the
  fan's step function against -59, -60, -63, -64, -67, -70, -75, -78 and 0
  in both directions.
