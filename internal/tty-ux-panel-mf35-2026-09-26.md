# The MF35 panel: a status skin for 480 x 320, and machine skins on the card

Design spec for the `panel` plugin on the Makerfabs ESP32-S3 SPI TFT 3.5"
(ILI9488, 480 x 320, 18-bit colour over SPI, ESP32-S3-WROOM-1 N16R2: 16 MB
flash, 2 MB PSRAM). Written against v1.1.1 in `release-prep/wt-mf35`:
`src/plugins/panel.cpp`, `panel_gfx.h`, `panel_feed.h`, `panel_font.h`,
`lights.cpp`/`lights.h`, `core/bbs.h`, `core/bbs_shell.cpp` (the NODES and
DASH row code) and `platform/platform_esp32.cpp` (the LCD band). Built on
`internal/tty-ux-panel-2026-09-24.md` revisions 1 and 2, which is what
shipped on the Waveshare. No source was changed.

Every picture below was rendered from the real Spleen arrays in
`panel_font.h`, the real icons and glyphs in `panel_gfx.h` and the site
tokens quantised through RGB565, by scripts in the session scratchpad
(`tools/kit.py`, `status_skin.py`, `skinkit.py`, `skins.py`, `skins2.py`,
`build_skins.py`). The machine previews composite the status text and the
glow renderer specified here onto the painted art, dithered to RGB565 the way
the decoder stores it. All posted to Rob on Discord.

Mock-ups, in `scratchpad/mf35-mockups/`, each at 1:1 (`_1x`) and 2x with a
ruler (`_2x`):

- `status_quiet`: nobody on, seven calls today.
- `status_three`: three callers, one idle, an upload waiting.
- `status_full_ring`: the sysop line and all ten lines, one still logging in,
  a caller ringing.
- `status_closed`: a fresh board, closed, no network, no card, unclean start.
- `status_portrait_three`: the portrait arrangement, 320 x 480.
- `skin_pc`, `skin_c64`, `skin_apple2`, `skin_atari`, `skin_imsai`: each
  stock skin with three callers on and its lights lit; `skin_*_drive6x.png`
  is the drive light at 6x, to judge the glow.

The stock skin folders are in `scratchpad/mf35-skins/<name>/`:
`background.jpg`, `key.png` (the placeholder layer) and `skin.txt` (derived
from the key the way `mkskin.py keys` will do it).

## The verdict

The shipped layout is wrong for this glass and cannot be scaled into being
right: its landscape branch caps the callers at three and leaves 146 px of
the 320, 46%, black. The board needs its own layout, and the extra room buys
exactly what Rob asked for: all eleven lines at once as a fixed node board,
what each caller is doing, their terminal, recent calls, a traffic sweep and
the health figures, every one of them already in RAM. That is the built-in
`status` skin, it keeps the Waveshare's header, glyphs, antenna, clock, dot,
palette and square LEDs pixel for pixel, and it is the default because it is
the only skin that shows everything. The machine skins are a JPEG and a
manifest on the card, with the status text on the machine's own screen and
glow LEDs driven by the lights engine; they cost about 12 KB of flash, 300 KB
of PSRAM while one is shown, and no art in the firmware. Both are buildable
in `panel.cpp`/`panel_gfx.h` as a third layout plus a skin loader, with
small hand-backs to `Bbs`, `lights`, the platform's LCD band and
`PluginSetting`.

## What the shipped code draws on this glass today

`layout()` takes the landscape branch for 480 x 320. The callers loop is
`while (n < 3 && ...)`, the recent loop takes four rows, and the system row
and the LEDs are anchored to the foot (`rule1` at `h - 46`). Measured from
the source:

```
  y   0         1         2         3         4         5
      012345678901234567890123456789012345678901234567890123456789
  0   ############################################################
  3   192.168.0.86:6400                  (one 59-glyph slot, rotating)
 22   ============================================================
 24   [sd][up][tw]                                        |  16:20
 42   --------------------------o---------------------------------
 48   [] Callers 3/11              |[] 16:16 visitor
 68    1) daytona              12m |[] 16:08 daytona
 88    2* visitor               4m |[] 15:58 zaphod
108    5) S3Bench              51m |[] 15:41 visitor
128                                |
 ..                                |      146 px of black, rows 128 to 273
274   ------------------------------------------------------------
278   [] 118K       [] 23 today    [] peak 6
298   ------------------------------------------------------------
302                    [][][][][][][][][][]
```

- Three caller rows. Past three on, two names and `+N more`: a board with
  all ten lines busy shows two of them.
- A 59-glyph slot rotating a 17-glyph name: 42 columns of blue for nothing.
- A caller row has 29 glyphs of room and uses a node, a handle and a time
  on. No doing, no terminal.

This is the 40-column habit again: sized for the small case, never
re-measured.

## Part 1: the status skin

### The glass and the wire

- 3.5" over 577 px diagonal: 165 ppi, 0.154 mm a pixel. The Waveshare is
  0.103 mm. An 8 x 16 Spleen glyph is 1.2 x 2.5 mm here against 0.8 x 1.6
  there, so 8 x 16 stays the text face and reads comfortably at arm's
  length. A 1 px rule is 0.15 mm and is visible; strokes stay 2 px.
- 60 glyph columns by 20 glyph rows at 8 x 16; 30 by 10 at 16 x 32.
- The ILI9488 on SPI takes only 18-bit colour, 3 bytes a pixel. The whole
  glass is 460,800 bytes, 92 ms at 40 MHz.
- A band is 3,840 px: 11,520 bytes of internal DMA staging, 2.30 ms on the
  wire at 40 MHz. One band a 20 ms tick is 50 bands a second, 192,000 px a
  second. A full redraw is 40 bands, 0.80 s.
- The real limit is transactions, not pixels. Every rectangle is at least
  one tick, however small. Ten 20 x 16 LED cells cost ten ticks; the same
  ten as one 220 x 16 rectangle cost one. Everything below is sized to that.
- The framebuffer stays RGB565 in PSRAM, 307,200 bytes. `lcdDraw` expands
  each pixel to the panel's 6-6-6 as it copies into the staging buffer (a
  256-entry table per channel, or `v << 1 | v >> 4` on red and blue): about
  40 us a band on the S3. Every token in `tok::` is exact in 565, so the
  status skin loses nothing.

### What the big glass shows that the Waveshare cannot

- All eleven lines at once, one fixed row per node, sysop line first. A
  login redraws one row; nothing reflows. This is how NODES reads, which is
  the layout Rob said he preferred to DASH.
- What each caller is doing: `Session::doing`, the verb of their last
  command, faint once they have been idle five minutes.
- Their terminal: `PET40`, `PET80`, `CP437`, `UTF8`, `ASCII`, from
  `term.shortName()`. On a board built for the whole legacy range, this
  column is the story.
- A line connecting or logging in: `(logging in)`, as WHO and DASH say it.
- A pip on each line that moves bytes.
- Traffic: bytes a second in and out, and a ten-minute sweep graph.
- Calls today as a heading, the peak, the slow-pass count, the uptime and the
  card's free space, each permanently on the glass rather than one page in
  three.
- The board name permanently. The slot rotates only the two ways to dial.
- A word in the band for the two states with no glyph: `CLOSED to callers`
  and `SHUTTING DOWN`.
- The Wi-Fi figure in dBm beside the antenna.
- The camera's last snap, on a board with a camera.
- Skins (part 2).

### Layout, landscape 480 x 320

One character is 8 px across; one line is one layout row, labelled with its
y. Rendered from the layout, not typed.

Three callers (`status_three`):

```
  y   0         1         2         3         4         5
      012345678901234567890123456789012345678901234567890123456789
  0   ############################################################
  3   The Rusty Antenna                         192.168.0.86:6400
 22   ============================================================
 24   [sd][up][tw]                                     |-58 16:20
 42   --------------------------o---------------------------------
 48   [] Callers 3/11   DOING      ON TERM  |[] Calls 23 today
 68     S                                   |[] 16:16 visitor
 88   * 1) daytona      FORUMS    12m CP437 |[] 16:08 daytona
108     2* visitor      CHAT       4m PET40 |[] 15:58 zaphod
128     3                                   |[] 15:41 visitor
148     4                                   |[] 14 in 1.2K out
168     5) S3Bench      FILES     51m UTF8  |graph 160 x 56, axis
188     6                                   |^^^^^^^^^^^^^^^^^^^^
208     7                                   |vvvvvvvvvvvvvvvvvvvv
228     8                                   |[] 118K   [] 0 slow
248     9                                   |[] 7.4 GB [] peak 6
268    10                                   |[] 12d 3h
290   -----------------------------------------------------------
296                   [][][][][][][][][][]
```

Every line busy, a ring (`status_full_ring`):

```
  y   0         1         2         3         4         5
      012345678901234567890123456789012345678901234567890123456789
  0   ############################################################
  3   The Rusty Antenna                        visitor is ringing
 22   ============================================================
 24   [sd][bl][ml][up][tw][st][sl]                     |-58 16:20
 42   --------------------------o---------------------------------
 48   [] Callers 11/11  DOING      ON TERM  |[] Calls 61 today
 68   * S] quantumrob   SYS        2h CP437 |[] 16:19 visitor
 88   * 1) daytona      FORUMS    12m CP437 |[] 16:18 apple2e
108   * 2* visitor      CHAT       4m PET40 |[] 16:15 zaphod
128     3> cosysop      BROADCAST 33m UTF8  |[] 16:12 amiga1200
148     4) vt220fan     MAIL       9m ASCII |[] 42 in 48K out
168   * 5) S3Bench      FILES     51m UTF8  |graph 160 x 56, axis
188     6) apple2e      WHO        2m ASCII |^^^^^^^^^^^^^^^^^^^^
208   * 7) breadbin64   CHAT       1h PET40 |vvvvvvvvvvvvvvvvvvvv
228     8) IBM5150      INFO      18m CP437 |[] 61K    [] 3 slow
248     9) amiga1200    FORUMS     7m CP437 |[] 7.4 GB [] peak 11
268    10  (logging in)               PET80 |[] 12d 3h
290   -----------------------------------------------------------
296                   [][][][][][][][][][]
```

Nobody on (`status_quiet`): the same grid with eleven faint node numbers,
`Callers 0/11` in dim, the last four events and a flat graph. The table
reads as an idle switchboard, not a missing list.

Every rectangle. Text boxes are 16 tall; x ranges are inclusive.

| Element | x | y | w | h | Content | Colour |
|---|---|---|---|---|---|---|
| bar | 0 | 0 | 480 | 22 | | `kBar` |
| name | 4 | 3 | 208 | 16 | board name, `cutWords` at 26 glyphs, static | `kInk` |
| slot | 284 | 3 | 192 | 16 | 24 glyphs, right aligned, rotating | by page |
| band | 0 | 22 | 480 | 20 | | `kBand` |
| glyph strip | 4 | 24 | 120 | 16 | the nine glyphs, as shipped | by flag |
| band word | 134 | 24 | 254 | 16 | 31 glyphs, left aligned | warm or risk |
| antenna | 396 | 24 | 6 | 16 | as shipped | by RSSI |
| dBm | 406 | 24 | 24 | 16 | `-58`, right aligned | antenna's colour; `--` faint |
| clock | 436 | 24 | 40 | 16 | `HH:MM`, right aligned | `kYellow`; `--:--` dim |
| track | 0 | 42 | 480 | 1 | the dot runs here | `kTrack` |
| callers heading | 6 | 48 | 16 + 116 | 16 | icon at 6, `Callers 4/11` at 26 | `kStruct`; dim at 0 |
| column labels | 150, 238, 262 | 48 | | 16 | `DOING`, `ON` (right to 253), `TERM` | `kFaint` |
| row k, k = 0..10 | 6 | 68 + 20k | 296 | 16 | row 0 the sysop line, row k node k | below |
| pip column | 6 | 68 | 6 | 216 | one 6 x 6 pip per row at y + 5 | `kDial` |
| column rule | 308 | 48 | 1 | 236 | | `kRule` |
| calls heading | 316 | 48 | 160 | 16 | handset icon, `Calls 23 today` at 336 | `kStruct`; dim at 0 |
| recent j, j = 0..3 | 316 | 68 + 20j | 160 | 16 | icon at 316, `HH:MM handle` at 336, 17 glyphs | as shipped: ink, dim, faint |
| traffic label | 316 | 148 | 160 | 16 | traffic icon, `14 in 1.2K out` at 336 | in `kDial`, out `kLive`, words dim |
| graph | 316 | 168 | 160 | 56 | the sweep, axis at y 196 | in `kDial` up, out `kLive` down, axis `kRule` |
| system cell A, row r | 316 | 228 + 20r | 78 | 16 | icon at 316, figure at 336, 7 glyphs | icon `kDial`, figure below |
| system cell B, row r | 398 | 228 + 20r | 78 | 16 | icon at 398, figure at 418, 7 glyphs | as A |
| foot rule | 4 | 290 | 472 | 1 | | `kRule` |
| LED row | centred | 296 | n x cell | 16 | the strip | from the lights frame |

The vertical budget: bar 22, band 20, track 1, air 5, heading 16, air 4,
eleven rows at a 20 px pitch (220, the last ending at 284), air 6, rule 1,
air 5, LEDs 16, margin 8. Total 320.

The horizontal budget of a caller row, 36 glyphs from x 14 to 301:

| Column | x | Glyphs | Content |
|---|---|---|---|
| pip | 6..11 | | 6 x 6 at y + 5 |
| node | 14..29 | 2 | `nodeLabel()`: ` S`, ` 1`, `10` |
| mark | 30..37 | 1 | `markFor()`; `)` for a plain caller, as shipped |
| handle | 46..141 | 12 | cut at 12; `BBS_USER_MAX` is 20 |
| doing | 150..221 | 9 | `Session::doing`, `-` when empty (the rule of `Bbs::doingText`, which is private); the longest verbs are 9 (`BROADCAST`, `CHATCLEAR`) |
| on | 230..253 | 3 | `fmtOnFor(now - loginAt)`, right aligned |
| term | 262..301 | 5 | `term.shortName()` |

Then 6 px of air and the rule at 308. The right column is 20 glyphs from 316
to 475; its widest line, `Calls 123 today` or `16:12 amiga1200`, ends by 456.

### The header

The Waveshare's two rows, 42 px, unchanged in height, colours and glyphs.
Three changes, each because the width allows it.

- The name is permanent, on the left, `cutWords` at 26 glyphs. A board's
  identity should not be one page in three on a screen you watch.
- The slot is 24 glyphs, right aligned to 476, and rotates two pages, 3 s
  each with the shipped fade: A `IP:port` in `kDial`, B `host.local:port`
  in `kDial` (the mDNS name from `syscfg::get().hostname`). Both are the
  board's dial string, so the slot means one thing: how to call it. With no
  network both pages say `no network` in `kRisk`, as page B does now. The
  uptime and the card move to the system cells, where they stay.
- A ring overrides the slot exactly as shipped: `<handle> is ringing` in
  `kBusy`, the handle cut so the words stay whole.
- The fade redraws only the text's extent, not the slot: the rectangle is
  `R(476 - w, 3, w, 16)` with `w` the wider of the old and new text. A
  17-glyph address is 2,176 px, one band; the whole slot would be 3,072.
  The framebuffer still clears the whole slot; only the queued rectangle
  shrinks. 16 steps a turn, one turn every 3.96 s: about 4 transactions a
  second.
- The band word, `R(134, 24, 254, 16)`: `SHUTTING DOWN` in `kRisk` while
  `Bbs::listening() && !Bbs::answering()`, else `CLOSED to callers` in
  `kWarm` while `syscfg::get().closed`, else nothing. The only two board
  states that have no glyph, and the two a sysop walking past most needs to
  read. Redrawn on change only.
- The dBm figure, `R(406, 24, 24, 16)`: the mean of the last four antenna
  samples, redrawn at most once a second, in the antenna's own colour. The
  antenna keeps its raw 4 Hz movement; the number is the steady reading.
  Unsmoothed, a figure that jitters 2 dB every quarter second is noise.

### The callers table

- Fixed rows. Row 0 is the session with `role == Role::Sysop`; row k is the
  session with `id == k`, k = 1..10. The busy line (`id == BBS_MAX_NODES +
  1`) is never shown. Rows = `min(11, BBS_MAX_NODES + 1)`.
- A free line, and a hidden or lurking one, shows only its node number in
  `kFaint`. Identical pixels, so the desk gives away no more than WHO does
  to a caller. `shown()` is unchanged.
- A session that is up but not logged in (`st != Free && !loggedIn`): the
  node number and `Bbs::preLoginName(s)` in `kFaint` at x 46, and the
  terminal once detection has one. The heading keeps `publicBusy()` /
  `publicNodes()`, as shipped.
- A caller row: node and mark in the rank colour (`]` risk, `>` yellow,
  `*` dim, `)` ink), handle in `kInk`, doing in `kStruct` and in `kFaint`
  once `now - lastInput >= 300,000 ms` (DASH greys idle at the same five
  minutes), on in `kDim`, terminal in `kFaint` (DASH's DarkGrey).
- Doing is `kStruct` because the terminal side draws Doing in Cyan and
  `--struct` is the site's cyan; the same thing is the same colour on both.
- The pip: `kDial` 6 x 6 at `(6, y + 5)` when that line moved bytes in the
  last 500 ms. The whole pip column, `R(6, 68, 6, 216)`, is one field and
  one rectangle, redrawn at most twice a second: eleven pips blinking
  separately would be eleven transactions.
- The pip needs its own traffic bits. `takeTraffic()` clears on read and the
  lights plugin is its one reader, so the panel cannot share it. Hand-back:
  a `uint16_t panelMoved_` in `Bbs`, `#ifdef BBS_HAS_LCD` like
  `panelToday_`, OR-ed where `rxSeen_` and `txSeen_` are set, and a
  `takePanelTraffic()` that clears it. 2 bytes. Without it, the pip falls
  back to `now - lastInput < 1,000 ms`, which shows typing but not a caller
  reading a long forum thread.
- Each row is a field with a key of its visible words, so an on-time that
  ticks from `11m` to `12m` redraws that row alone.

### Calls and the recent list

- The heading carries today's count: `Calls 23 today`, from `g_today` as
  now. `kStruct`, dim at 0. It replaces the system row's `23 today`.
- Four recent rows, newest first, the shipped icons, colours and ageing (ink,
  dim, faint; a bell keeps `kBusy`). The ring stays 10 deep. With nothing in
  it the first row shows the quiet icon and `nothing yet`, as shipped.
- 17 glyphs: `HH:MM ` and an 11-glyph handle.

### Traffic

- The label: the traffic icon (new, below), then the last sample's rate in
  and out as bytes a second, at most 4 glyphs each: `0`, `340`, `1.2K`,
  `12K`, `1.2M`. In `kDial`, out `kLive`, the words `in` and `out` in
  `kDim`; a figure of 0 in `kDim`.
- The graph, `R(316, 168, 160, 56)`: 160 columns, one sample every 4 s, ten
  minutes and 40 seconds. The axis is y 196 in `kRule`. In grows up from
  195 (28 px), out grows down from 197 (27 px).
- The scale is fixed and logarithmic, so it never rescales and never has to
  redraw the whole graph: `h = min(H, (bitlen(rate) * H + 16) / 17)` with
  `bitlen(0) = 0`, `bitlen(1) = 1` and `H` 28 up, 27 down. 17 doublings to
  full scale, 128 KB/s. Typing lands at 4 to 9 px, a screen at 19, a
  YMODEM transfer at 27 of 28.
- It sweeps, it does not scroll. A write head moves left to right; each
  sample draws one column at the head and clears the two columns ahead of it
  to black, axis included, so "now" is the gap. One rectangle, 3 x 56 = 168
  px, one transaction every 4 s. Scrolling the graph would be 8,960 px, 3
  bands, every 4 s, for no gain.
- State: a ring of 160 two-byte samples (in height, out height), the head,
  the last `bytesIn()`/`bytesOut()` totals and the sample time: 333 bytes.
  The ring lets `redrawAll()` put the graph back after silent mode or a
  restart of the plugin.

### The system cells

Six half cells in three rows, each an icon and at most 7 glyphs. Each is a
field; each redraws when its words change.

| Cell | Icon | Figure | Colour | Source, cadence |
|---|---|---|---|---|
| 228 A | chip | `118K` | ink from 40 K, warm to 20 K, risk below (as shipped) | `plat::heapFree()`, 5 s |
| 228 B | hourglass (new) | `3 slow` | ink; warm while a slow pass is under a minute old | `Bbs::slowPasses()`, 500 ms |
| 248 A | card | `7.4 GB` | ink; `--` faint with no card; risk while the card is in error | `g_free` and `cardState()`, as shipped |
| 248 B | callers | `peak 6` | ink | `Bbs::peakNodes()`, 500 ms |
| 268 A | clock | `12d 3h` | ink | `fmtUptime`, 500 ms |
| 268 B | camera (new) | `14:02` | ink; dim when none today | camera boards only, below |

The card and clock icons are revision 0's words, which never made it into
`panel_gfx.h`; they go in with this layout. The camera cell exists only
where `BBS_HAS_CAMERA` is defined, and nothing moves into it elsewhere: the
block ends one cell short, which reads as the end of a block, not a hole.
On a camera board it shows the time of the last snap, from a
`camera::lastSnapAt()` that does not exist yet (4 bytes in the camera plugin,
optional), and `busy` in `kWarm` while a snap runs (`camera::busy()`,
exists).

### The LED row

- One row, centred, box height 16 at y 296.
- `cell = min(22, 240 / n)`, `side = clamp(cell - 6, 8, 14)`, each LED
  centred in its cell. At ten: cell 22, side 14, the row 220 x 16 from x
  130. At sixteen: cell 15, side 9, 240 x 16.
- The rule is that a whole row is at most 240 x 16 = 3,840 px: one band.
  A `rainbow` strip, every LED changing every frame, then costs one
  transaction a frame, 25 a second.
- Lit and off exactly as shipped: the colour at a third for the glow edge,
  a core one pixel in; off is a `kRule` ring round a `kSurface` fill.
- The shipped "more than three changed, send the row" rule stays.

### New icons

Three 16 x 16 rows of words, drawn like the shipped set:

```
traffic            camera             hourglass
................   ................   ................
....XX..........   ................   ..XXXXXXXXXXXX..
...XXXX.........   .....XXXXXX.....   ...XX......XX...
..XXXXXX........   .XXXXXXXXXXXXXX.   ...XX......XX...
.XX.XX.XX.......   .XX..........XX.   ....XX....XX....
....XX..........   .XX...XXXX...XX.   .....XX..XX.....
....XX......XX..   .XX..XX..XX..XX.   ......XXXX......
....XX......XX..   .XX.XX....XX.XX.   .......XX.......
....XX......XX..   .XX.XX....XX.XX.   .......XX.......
....XX......XX..   .XX..XX..XX..XX.   ......XXXX......
............XX..   .XX...XXXX...XX.   .....XX..XX.....
........XX.XX.XX   .XX..........XX.   ....XX.XX.XX....
.........XXXXXX.   .XXXXXXXXXXXXXX.   ...XX.XXXX.XX...
..........XXXX..   ................   ...XXXXXXXXXX...
...........XX...   ................   ..XXXXXXXXXXXX..
................   ................   ................
```

```
traffic   0x0000,0x0C00,0x1E00,0x3F00,0x6D80,0x0C00,0x0C0C,0x0C0C,0x0C0C,0x0C0C,0x000C,0x00DB,0x007E,0x003C,0x0018,0x0000
camera    0x0000,0x0000,0x07E0,0x7FFE,0x6006,0x63C6,0x6666,0x6C36,0x6C36,0x6666,0x63C6,0x6006,0x7FFE,0x0000,0x0000,0x0000
hourglass 0x0000,0x3FFC,0x1818,0x1818,0x0C30,0x0660,0x03C0,0x0180,0x0180,0x03C0,0x0660,0x0DB0,0x1BD8,0x1FF8,0x3FFC,0x0000
```

### Motion, and the transaction budget

The ceiling is 50 transactions a second. What spends them:

| Mover | Size | Rate | Transactions/s |
|---|---|---|---|
| the dot | 5 x 3 = 15 px | 2 px a step every 80 ms, 19.2 s a lap | 12.5, and none on a pass where the queue holds 3 or more |
| the slot's fade | text extent, 1 band | 16 steps per 3.96 s | 4 |
| antenna | 96 px | on change, at most 4 Hz | up to 4 |
| dBm | 384 px | on change, at most 1 Hz | up to 1 |
| bell blink | 99 px | 2 Hz during a ring | 4 |
| pips | 1,296 px | at most 2 Hz | 2 |
| graph | 168 px | every 4 s | 0.25 |
| clock | 640 px | once a minute | 0 |
| caller rows | 4,736 px, 2 bands | on-time once a minute each, doing on a command | about 0.5 |
| LED row | at most 3,840 px, 1 band | on change, at most 25 Hz | up to 25 |

- A quiet board at rest: the dot, the fade and the antenna, about 21
  transactions a second and about 9 K px a second: 5% of the pixel budget.
- The worst case, all lines busy, a `rainbow` strip and a ring: about 53
  asked for. The dot stands aside, leaving about 40. It fits because the LED
  row is one band and the pips are one rectangle.
- The dot moves 2 px every 80 ms rather than 1 px every 40 ms: the same 25 px
  a second, half the transactions. It yields on queue depth, not only on the
  LEDs, because on this glass the queue is the thing that runs out.
- Hand-back, and it matters more here than on the Waveshare: `Dirty::add`,
  when full, unites the whole queue into one rectangle. On 480 x 320 the
  union of the dot, a caller row and the LED row is most of the glass: 40
  bands, 0.8 s during which the LED row and the fade stall. Instead merge
  the pair whose union grows least, which keeps 23 small rectangles and one
  medium one. About 20 lines in `panel_gfx.h`, testable on the host.

### Portrait, 320 x 480

The same blocks, placed again; no new drawing code. Worth building because
the table is 36 glyphs and the glass is 40, and a sysop who stands the
module upright should not get the 172-wide design stretched.

| Element | x | y | w | h |
|---|---|---|---|---|
| name | 4 | 3 | 128 | 16 (16 glyphs, `cutWords`) |
| slot | 148 | 3 | 168 | 16 (21 glyphs: the IPv4 maximum) |
| glyph strip, antenna, dBm, clock | as landscape, from the right edge 316 | 24 | | |
| band word | none: no room between the glyphs and the antenna |
| callers heading, rows 0..10 | as landscape | 48, 68 + 20k | 296 | 16 |
| rule | 4 | 290 | 312 | 1 |
| calls heading | 6 | 296 | 148 | 16 |
| recent j = 0..3 | 6 | 316 + 20j | 148 | 16 (16 glyphs) |
| traffic label | 166 | 296 | 150 | 16 |
| graph | 166 | 316 | 148 | 56 |
| system cells | 6 and 166 | 396, 416, 436 | 148 each | 16 |
| rule | 4 | 456 | 312 | 1 |
| LED row | centred | 462 | | 16 |

`status_portrait_three` shows it. Machine skins are landscape art and do not
turn: on a portrait glass the panel shows the status skin and PANEL says why.

### Choosing the layout

`layout(w, h)` gains a third and fourth case before the existing two:
`w >= 400 && h >= 300` is this landscape, `w >= 300 && h >= 400` this
portrait. The Waveshare's 172 x 320 and 320 x 172 fall through to the
shipped branches untouched.

### Fields and memory

- Fields: name, slot, glyphs, band word, antenna, dBm, clock, callers
  heading, pip column, 11 rows, calls heading, 4 recent, traffic label, 6
  system cells: 32. `g_shown[32][80]` is 2,560 bytes, up from 1,280. The
  graph and the LEDs keep their own state, as the LEDs do now.
- The traffic ring and its state 333 bytes; the dBm samples 5; the pip bits
  2. About 1.6 KB of static RAM in all, in a plugin that exists only on S3
  boards. The S3's static data is 249,712 of 341,760 at 1.1.0.
- Framebuffer 307,200 bytes of PSRAM, 15% of the board's 2 MB.

## Part 2: skins

### The shape

- A skin is a folder on the card, `skins/<name>/`, holding `background.jpg`
  and `skin.txt`. Nothing else is read.
- `status` is built into the firmware and is always there. With no card, a
  missing skin, or a skin that fails any check, the panel shows `status`,
  logs one line with the file, the line and the reason, and PANEL repeats
  it. A broken skin never leaves the glass dark.
- CONFIG picks the skin: `status` or any folder under `skins/` holding a
  `skin.txt`. The default is `status`, for reading rather than for
  nostalgia: it is the only skin that shows all eleven lines, what each is
  doing, the traffic and the health figures, and it is the only one that
  works with no card. A machine screen holds 6 to 8 lines of 19 to 22
  columns.
- Silent mode blanks everything, as now: backlight off, nothing sent, the
  whole glass redrawn before it is lit again.
- Landscape 480 x 320 only.

### skin.txt, format 1

`key = value`, one a line, ASCII, the way `system.cfg` reads. A line that
starts with `#` is a comment; there are no trailing comments. Units are
pixels on the 480 x 320 glass, origin top left. Positions are an LED's
centre and size, or a rectangle's `x y w h`.

| Key | Value | Required | Meaning |
|---|---|---|---|
| `format` | `1` | yes, first | anything else refuses the skin |
| `title` | 1 to 24 printable ASCII | no | CONFIG's name for it; the folder name if absent |
| `screen` | `x y w h` | no | the status text rectangle |
| `screen_fg` | `#rrggbb` | with `screen` | text colour |
| `screen_bg` | `art` or `#rrggbb` | no, `art` | `art`: text over the picture, restored from it; a colour: the rectangle filled |
| `screen_font` | `small` or `big` | no, `small` | 8 x 16 or 16 x 32 |
| `screen_pad` | 0 to 16 | no, 4 | pixels inside the rectangle before the text grid |
| `drive` | `cx cy size shape style` | no | the drive light; style `pc`, `1541`, `disk2`, `breathe` or `off` |
| `drive_colour` | `lights` or `#rrggbb` | no, `lights` | a tint for idle and access; an error blink stays red |
| `activity` | `cx cy size shape #rrggbb` | no | lit while the board's lines move bytes |
| `lamp1` to `lamp4` | `cx cy size shape #rrggbb` | no | always lit: power lamps |
| `led1` to `led16` | `cx cy size shape` | no | the strip; `ledN` is strip pixel N |
| `bus1` to `bus8` | `cx cy size shape #rrggbb` | no | the low byte of all traffic, `bus1` the top bit |
| `flag1` to `flag16` | `cx cy size shape #rrggbb condition` | no | lit while the condition holds |
| `clock` | `x y w h font #rrggbb bg align` | no | `HH:MM`; `--:--` without a valid clock |
| `callers` | `x y w h font #rrggbb bg align` | no | callers on now, `publicBusy()` |
| `today` | `x y w h font #rrggbb bg align` | no | calls today |

`shape` is `round` or `square`. `size` is the diameter or side, 4 to 24.
`font` is `small` or `big`; `bg` is `art` or `#rrggbb`; `align` is `left`,
`right` or `centre`.

Conditions, each a figure the panel already reads twice a second:

| Condition | True while | Source |
|---|---|---|
| `ring` | a caller rings the sysop; blinks at 2 Hz | `Bbs::ringing()` |
| `mail` | the sysop has unread mail | `Bbs::sysopMail()` |
| `upload` | an upload waits for approval | `files::pendingCount() > 0` |
| `backup` | the backup window is open | `Bbs::backupOpen()` |
| `listed` | the directory lists the board | `announce::listing() == 1` |
| `staff` | staff are on and shown | the session scan |
| `restart` | the last start was not clean, until staff log in | `crashBoot()`, as the triangle |
| `slow` | a slow pass in the last minute | as the hourglass |
| `card` | a card is mounted | `sdcard::panel()` |
| `carderr` | the card will not mount, or a read failed in the last 10 s | as the red SD glyph |
| `closed` | the board is closed to callers | `syscfg::get().closed` |
| `answering` | the board takes calls | `Bbs::answering()` |
| `full` | every public line is busy | `publicBusy() == publicNodes()` |
| `callers` | anybody is on | `publicBusy() > 0` |
| `rx` | bytes read in the last 100 ms, lit and dark at least 100 ms each | `bytesIn()` delta |
| `tx` | bytes written, the same | `bytesOut()` delta |

The checks, the same in the firmware and in `mkskin.py`. Any failure refuses
the whole skin:

- The file is at most 4,096 bytes and 120 characters a line. A key given
  twice is refused. An unknown key is logged and ignored, so a format-1
  board can read a skin written for a later one.
- Numbering is contiguous from 1 within each family (`led1` to `ledN`). At
  most 48 LEDs of all kinds together.
- Every rectangle lies on the glass: `x >= 0`, `y >= 0`, `x + w <= 480`,
  `y + h <= 320`.
- `screen` holds at least 8 columns and 1 row of its font inside its pad.
- Every LED's glow box (below) lies on the glass.
- No two glow boxes overlap, and none overlaps `screen` or a figure. This is
  load-bearing, not tidiness: each is repainted from the clean art on its
  own, so two that share pixels would erase each other's light.
- Colours are `#rrggbb`; every word is one of its list.
- The folder name is 1 to 15 characters of `a-z 0-9 -`, and `status` is
  reserved.

### background.jpg

- Exactly 480 x 320, baseline JPEG (SOF0 or SOF1), 8-bit, three components
  or one. Progressive is refused by name, because the decoder cannot read
  it and "not a baseline JPEG" is something a person can fix in any editor.
- At most 204,800 bytes. The stock art is 22 to 34 KB at quality 88; a
  photograph at the same quality is 60 to 100 KB.
- Decoded once, on a worker task (the camera plugin's `plat::taskStart`
  pattern, below the BBS task), never in the loop. The `esp_jpeg` component
  drives the TJpgDec in the S3's ROM, so the decoder costs no flash and a
  work area of about 3 KB on the worker's stack while it runs. Confirm the
  ROM route in the managed component before relying on it.
- Each MCU block comes out as RGB888 and goes into a clean copy in PSRAM as
  RGB565 through a 4 x 4 ordered dither. Without the dither a CRT's glow
  gradient bands visibly at five bits of red and blue; with it the previews
  show none.
- The clean copy is 307,200 bytes of PSRAM, allocated only while a card
  skin is showing and freed on a switch back to `status`. With the
  framebuffer it is 600 KB, 29% of the 2 MB.
- The card read and the decode take a few hundred milliseconds on the
  worker; measure both on the board before quoting a figure anywhere. When
  the worker is done the loop copies the clean art into the framebuffer in
  bands of rows (never all 300 KB in one pass: at most 16 rows a tick),
  draws the overlays and queues the glass. 0.8 s to the glass.

Nothing is assumed about the art beyond its size. Rob's own photographs of
his machines drop in as they are, with a `skin.txt` saying where their
screen and lamps are.

### The status text rectangle

- The grid is `cols = (w - 2 pad) / glyph width`, `rows = (h - 2 pad) /
  glyph height`.
- Text is the skin's `screen_fg`. Secondary text (idle doing, time on,
  events) is 60% fg and 40% background; with `screen_bg = art` the
  background counts as black. Two states override the costume, because
  meaning beats it: a ring is `kBusy`, a card error or an unclean restart
  is `kRisk`.
- With `screen_bg = art` every glyph cell is restored from the clean copy
  before its lit pixels are drawn: a glyph routine that takes its "off"
  pixels from the clean art rather than from one colour. Hand-back.
- Each line of the grid is a field with its own key, so a line that did not
  change is not redrawn or sent. At most 20 lines.

What the grid holds, in the order it gives things up as it shrinks:

- Line 1, always: `Callers 3/11`, or `<handle> is ringing` in `kBusy` for
  as long as the ring lasts. The clock, right aligned in dim, when the line
  has room and the skin has no `clock` figure.
- Caller lines, one per caller shown, in node order: node, mark, handle,
  doing, time on. The handle keeps at least 8 glyphs; the doing takes what
  is left up to 9 and is dropped under 5; the time on needs 30 columns.
  Idle callers' doing is dim. If the callers outnumber the lines, the last
  line is `+N more`.
- Event lines, newest first, `+16:16 visitor` for a login or guest,
  `-15:58 zaphod` for a logoff, `!15:41 visitor` for a page or ring in
  `kBusy`. One is kept for the events when the callers would take every
  line, from 3 rows up.
- A flags line, last, from 3 rows up, only while one holds: `mail`,
  `uploads`, `closed`, `card`, `restart`, in `kBusy` or `kRisk`.
- `Nobody on.` in dim when nobody is, and the events fill the rest.
- One row at 40 columns or more: the headline, the newest event, the clock.

Measured on the stock skins with three callers on, as the previews show:

| Skin | Rectangle | Grid | What fits |
|---|---|---|---|
| pc | 190 44 176 132, pad 2 | 21 x 8 | headline and clock, 3 callers with doing, 4 events |
| c64 | 146 42 168 116, pad 2 | 20 x 7 | headline and clock, 3 callers with doing, 3 events |
| apple2 | 104 30 180 112, pad 2 | 22 x 6 | headline and clock, 3 callers with doing, 2 events |
| atari | 122 42 156 120, pad 2 | 19 x 7 | headline and clock, 3 callers with doing, 3 events |
| imsai | 30 55 420 18, pad 1 | 52 x 1 | headline, newest event, clock |

With all eleven on, a 7-row screen shows the headline, five callers,
`+6 more` and nothing else. That is the honest limit of a costume, and the
reason `status` is the default.

### The glow dot

A lit LED on a photograph has to look like light, not like a sticker. The
art paints each LED as it looks switched off; the firmware only ever adds
light to it, and an LED that goes dark is its box copied back from the
clean art.

- Size `d`, core radius `r = d / 2`, halo width `g = max(2, ceil(0.6 d))`.
- The glow box, which is what is repainted and queued: a square of side
  `2 * ceil(r + g) + 1` centred on the LED. 11 at d 4, 15 at d 6, 19 at d
  8, 23 at d 10, 55 at d 24. The largest, 3,025 px, is inside one band.
- Colour `c`, 8 bits a channel; brightness `v = max(c) / 255`.
- Halo, for a pixel at distance `t` from the centre with `r < t <= r + g`:
  `s = (t - r) / g`, `a = 0.8 v (1 - s)^1.6`, screen-blended over the clean
  art `B`: `out = 255 - (255 - B)(255 - a c) / 255`. Screen, because light
  adds and never darkens, and it cannot overflow.
- Core, `t <= r`: `c + (255 - c) * 0.45 v (1 - t / r)^2`, a hot centre that
  scales with brightness, with the edge anti-aliased over one pixel. Round
  measures `t` as distance, square as the larger of `|dx|` and `|dy|`.
- Everything above that depends only on size and shape is computed once at
  load: a per-(size, shape) map of the halo alpha and core weight, one byte
  each per pixel of the glow box. The stock skins use two sizes, so two maps
  of 225 and 361 bytes. Per frame it is integer arithmetic only, about 45 us
  for a d 8 LED on the S3.
- At most four glow boxes are computed per 20 ms tick; the rest wait for
  the next tick. A 16-LED `rainbow` frame then takes four ticks to compute
  and about 180 us of each. Rule no. 1: measure it in SYS's plugin phase
  on the bench; the loop's slow-pass threshold is 50 ms and this is under
  0.2.
- The colour an LED is drawn in is the frame's colour taken back to its
  effect level (`v * 100 / pct`, the first half of `glassLevel`), not
  dimmed by the brightness curve. A skin's LED is a picture of an LED:
  the strip's brightness setting limits a real pixel's current, and on a
  photograph a 58% LED reads as off. The status skin keeps `glassLevel`
  exactly as shipped.

### What the lights plugin must expose

The strip needs nothing new. `lights::panelFrame()` already hands over the
frame, and `wantPanel(true)` already makes the effects run with no strip
wired, so every skin shows the same frame a real strip would.

The drive light needs one function, and it must reuse `drawDrive()`, never
copy it:

```
// lights.h, BBS_HAS_LCD
// panelDrive: the drive light as a panel draws it, in a drive style of the
// panel's choosing. rgb at the effect's own level; kind says why it is lit
// (0 dark, 1 at rest, 2 access, 3 error), so a skin's tint can colour 1 and
// 2 and leave an error red. False while the lights plugin is not running.
bool panelDrive(uint8_t style, uint32_t now, uint8_t rgb[3], uint8_t& kind);
```

- `drawDrive(now, f)` becomes `drawDrive(style, now, f, &kind)` and the
  plugin's own tick passes `g_driveFx`. Same access timeline, same error
  blink, same holds (60 ms pc, 150 ms 1541, 1 s disk2). The `pc` style's
  flicker draws from the shared `rnd()`, which is harmless.
- So each machine's drive light behaves like that machine's drive (the
  1541's solid-for-the-access, the Disk II's motor running on) while the
  real pixel keeps the sysop's own style. The event timeline is identical
  for both; only the style differs, and a sysop who wants the glass and the
  pixel to match sets `drive_fx` to the skin's style.
- No drive pin: `drawDrive` runs in the lights tick whenever the panel is
  up, because `wantPanel` keeps the tick going. It already does for the
  strip.
- The lights plugin switched off: `panelFrame` returns 0 and `panelDrive`
  false, so the strip and the drive light stay as the art paints them,
  dark. Everything the panel owns still works: `activity`, lamps, `bus`,
  flags, figures and the screen text. PANEL says `lights off`.
- Static RAM in lights: none. `kind` is computed, not kept.

### The frame rate the glass allows

- One band: 3,840 px, 11,520 bytes, 2.30 ms at 40 MHz. One a tick: 50 bands
  and 192,000 px a second. The LED areas are budgeted 25 transactions a
  second, one a 40 ms frame; the rest go to text, the header and the dot.
- At 25 frames a second, one frame of LED change must fit one rectangle of
  at most 3,840 px to cost one transaction. That is 17 glow boxes at d 6,
  10 at d 8, or the status skin's whole LED row.
- A group (the strip, `bus`, the flags, each single light) with more than
  three changed boxes in a frame is queued as the union of the changed boxes
  when that union is at most three bands, else box by box. A group whose
  last rectangle is still queued skips the frame: the glass runs a frame
  behind and the queue never grows.
- Frames a second for a group changing every frame =
  `25 / bands per frame`, less whatever else is moving. Per stock skin:

| Skin | Group | Glow box | Union | Bands | Full-change rate |
|---|---|---|---|---|---|
| pc | strip, 8 on the modem | 15 | 155 x 15 = 2,325 px | 1 | 25 fps |
| pc | drive, activity, 2 lamps | 15 | single | 1 each | per change |
| c64, apple2, atari | drive, lamps | 15 | single | 1 each | per change |
| imsai | strip, 16 address lamps | 19 | 409 x 19 = 7,771 px | 3 | about 8 fps under `rainbow`; `nodes`, `blinken` and `scanner` change a few at a time and run at 25 |
| imsai | bus, 8 output lamps | 19 | 159 x 19 = 3,021 px | 1 | 12.5 Hz by design |
| imsai | 11 flags | 19 | single | 1 each | at most 2 Hz; `rx`/`tx` 5 Hz |

Hand-back, optional and worth measuring: the wire is idle 88% of each tick.
A `lcdDraw` that takes a short list of small rectangles whose total is under
one band, sent back to back as one DMA chain, would let ten scattered LEDs
cost one tick instead of ten. It is a platform change and the budget above
does not depend on it.

### Loading, switching, failing

- At boot, `status` is drawn at once. If the configured skin is a card
  skin, the worker loads it, and the loop swaps it in when it is ready. The
  backlight waits for the first whole frame as now, with a 2 s limit, after
  which `status` is lit and the skin follows when ready.
- A CONFIG save restarts the plugins. The panel keeps the picture on the
  glass until the new skin is decoded, then draws it, rather than flashing
  `status` for half a second in between.
- No card at boot: `status`, and PANEL says `skin c64: no card`. When the
  sd plugin reports the card mounted after that, the panel tries once more.
  No polling.
- The card pulled while a skin is showing changes nothing: the art is in
  PSRAM. Only a change of skin reads the card.
- Every refusal names the file, the line and the rule:
  `panel: skin imsai skin.txt line 31: led4 glow overlaps led5`.

### CONFIG

- A `Skin` row (80 columns: `Panel skin`), cycling `status` and the folders
  found under `skins/` when the page opens. `PS_CYCLE` takes a constant
  `choices` string, so this needs a choices hook appended to
  `PluginSetting`: hand-back.
- The panel page is at its limit (`static_assert` in `panel.cpp`: 12 rows
  plus the core's four). Move Width, Height, X offset, Y offset, Invert,
  Mirror and Colours to a `Glass` sub-page with `PS_PAGE`, the way the pins
  went. It frees six rows, and those seven are set once per board profile.

### mkskin.py

A host tool beside `tools/mkscreens.py`. Four commands:

- `check <folder>`: the firmware's rules, word for word, plus the frame
  budget table above for that skin (glow boxes, unions, bands, rates).
- `keys <folder> --key key.png`: reads a 480 x 320 PNG where every element
  is painted in a key colour, finds each colour's bounding box and writes
  the positions into `skin.txt`, keeping every other key. An LED's centre
  is the middle of its box and its size the box's longer side; round when
  it fills under 90% of the box. Paint with anti-aliasing off: colours are
  matched exactly.
- `preview <folder>`: the preview renderer used for this report: status
  text, figures and lit LEDs composited on the dithered art.
- `pack <folder>... -o skins.zip`: a zip whose paths are `skins/<name>/...`,
  for the site and for seeding.

Key colours:

| Element | RGB |
|---|---|
| `screen` | 0, 0, 255 |
| `drive` | 0, 255, 0 |
| `activity` | 0, 255, 255 |
| `lampN` | 255, 255, N |
| `ledN` | 255, 0, N |
| `busN` | 255, 128, N |
| `flagN` | 128, 0, N |
| `clock`, `callers`, `today` | 0, 128, 255 / 254 / 253 |

The stock `skin.txt` files were produced exactly this way from their
`key.png`, and every one passes the checks.

### Seeding and size

- The five stock JPEGs are 134 KB together; with manifests, 138 KB.
- They do not belong in firmware. The MF35 has 16 MB of flash, so its board
  profile can give the stock skins room: either a larger `storage`
  partition or a `skins` data partition, seeded onto the card at first
  mount through the same `.seeded` manifest the screens use, so a sysop's
  own edit is never overwritten. On a board with no room for them, the
  site's `skins.zip` is the way in: unzip to the card.
- Firmware cost: the loader, the manifest parser, the glow, the text tiers
  and the JPEG glue, about 12 KB of flash with the decoder in ROM. Static
  RAM about 600 bytes (48 LEDs at 8 bytes, their last colours, the screen,
  three figures, the title). PSRAM 300 KB while a card skin shows.

## Part 3: the stock skins

No logo, badge, maker's name or lettering is drawn anywhere; the panels'
legends are plain rules. The machine names appear only as folder names in
CONFIG. Each is 480 x 320 of painted art: gradients, bevels, drop shadows,
grain, CRT vignettes and glass reflections, unlit LED lenses with a glint.

### pc: Beige PC

A tower on the left, a CRT in amber on an external modem, a keyboard in
front, disks on the right.

- `screen = 190 44 176 132`, `#ffb000` over the art, 21 x 8.
- `drive = 104 184 6 round pc`, the tower's disk lamp, in the lights
  colours: amber for the card, cool white for flash, red for an error.
- `activity = 89 184 6 round #ffbe28`, the middle lamp.
- `lamp1 = 74 184 6 round #3ce060` (tower power), `lamp2 = 386 203 6 round
  #3ce060` (monitor power).
- `led1..8`: the modem's eight lamps at y 249, x 209 to 349. `hayes` is the
  natural effect here and draws the Smartmodem's panel from real state.
- `callers = 30 182 30 16 small #ff3a22 art right`: the tower's two-digit
  window shows how many are on.

### c64: Breadbin and drive

A monitor with the light-blue border and blue paper, the breadbin keyboard
with its four function keys, a drive, a joystick.

- `screen = 146 42 168 116`, `#8a7be0` over the art, 20 x 7. The paper is
  painted into the art under the tube's own vignette.
- `drive = 398 230 6 round 1541`, `drive_colour = #ff2a1a`: the drive's own
  red lamp, solid for the whole access and blinking red on an error, which
  is exactly what the real one did.
- `lamp1 = 330 219 6 round #ff3020` (the computer's power lamp),
  `lamp2 = 382 230 6 round #3ce060` (the drive's).
- No strip. The machine had no row of lamps and one is not invented.

### apple2: Wedge and two drives

A green-phosphor monitor on the wedge case, two drives stacked on the right.

- `screen = 104 30 180 112`, `#41ff6a` over the art, 22 x 6.
- `drive = 368 224 6 round disk2`, `drive_colour = #ff2a1a`: the upper
  drive's in-use lamp, which stays lit a second after the access, as the
  motor did. The lower drive's lamp is art only and stays dark.
- `lamp1 = 48 294 6 round #ffe0a0`, the power lamp under the keyboard.

### atari: Console, drive and TV

A woodgrain TV with its control strip, a black border and the blue
playfield; the console with its cartridge door and four function keys; the
drive with two lamps.

- `screen = 122 42 156 120`, `#a8c4ff` over the art, 19 x 7.
- `drive = 384 212 6 round 1541`, `drive_colour = #ff2a1a`, the busy lamp.
- `lamp1 = 306 226 6 round #ff3020` (console power), `lamp2 = 366 212 6
  round #ff3020` (drive power).

### imsai: Front panel

No monitor. The panel is the status.

- `screen = 30 55 420 18`, `#ff3b1f` over a smoked window in the top plate,
  52 x 1: `Callers 3/11  +16:16 visitor ... 16:20`.
- Row A, y 106: `bus1..8` at x 44 to 184, the traffic byte on the output
  lamps. It freezes when traffic stops, which is itself a reading.
- Row A, x 296 to 436: the drive at 296 (1541 style, red), then flags `rx`,
  `ring`, `tx`, `closed`, `backup`, `upload`, `mail`.
- Row B, y 140: flags `listed`, `answering`, `full`, `slow`: the run lamps.
- Row C, y 174: `led1..16` at x 44 to 434, the strip on the address lamps.
  With the default `nodes` effect they light in rank colours; `blinken` and
  `scanner` suit the panel better and change a few lamps at a time.
- `lamp1 = 96 268 8 round #ff2a1a`, beside the key switch.
- 36 LEDs of the 48 allowed.

## What stays as it is

- The Waveshare panel. Its two layouts, fields and costs are untouched: the
  new cases come first in `layout()` and are chosen only by glass size.
- The header's rows, colours, glyph set and order, the antenna and its dB
  mapping, the clock and its colours, the track and the dot. These are what
  make the two boards one family.
- The palette in `tok::`, and what each token means.
- The recent list's icons, colours and ageing; the ring and its override.
- Square LEDs, `glassLevel`, the lit and off drawing, "more than three
  changed, send the row". The status skin's LEDs do not glow: they are the
  family's square 80s LEDs, and the glow belongs to photographs.
- Two faces, 8 x 16 and 16 x 32. The big glass does not need a third: an 8 x
  16 glyph is already 2.5 mm tall here. The status skin uses no 16 x 32 at
  all, because Rob asked for callers as a line and not a hero; skins may
  use it for a figure.
- The redraw scheme: figures recomposed twice a second, redrawn only on
  change, queued, one band a tick by DMA, never waited on.
- Everything shown is a figure already in RAM. The only new state is the
  traffic ring, the dBm samples and, optionally, 2 bytes in `Bbs` and 4 in
  the camera plugin.
- Silent mode.
- Touch. The board has an FT6236; nothing here uses it, and nothing here
  needs it.

## Hand-backs for the builder

- `panel_gfx.h`: the landscape and portrait big layouts as specified; 32
  fields; the three new icons plus revision 0's clock and card; the LED row
  arithmetic; `Dirty::add` merging the cheapest pair when full.
- `panel.cpp`: the fixed node table, pre-login rows, doing, idle, terminal;
  the pip column; the calls heading; the traffic ring and sweep; the six
  system cells; the band word; the dBm figure; the dot at 2 px per 80 ms
  yielding on queue depth; the fade queueing only the text extent; PANEL
  listing the new fields and the skin in use with its refusal reason.
- `Bbs`: `panelMoved_` and `takePanelTraffic()`, 2 bytes, `BBS_HAS_LCD`.
  Optional; the pip falls back to input only.
- `lights`: `panelDrive(style, now, rgb, kind)` from a `drawDrive` that
  takes the style. No RAM.
- `camera`: `lastSnapAt()`, 4 bytes. Optional; camera boards only.
- Platform: the ILI9488 in 18-bit mode, a 3,840-px band, 565 to 666 in
  `lcdDraw`, `psramAlloc` for 307,200 bytes. Already under way in this
  worktree, uncommitted when this was written (`BBS_LCD_ILI9488`: a band of
  480 x 8, COLMOD 0x66, RGB565 expanded as the band is staged). This spec
  assumes exactly that and needs nothing more from it.
- The skin loader on a worker, the manifest parser with the checks, the
  dithering decode, the glow maps and renderer capped at four a tick, the
  status text tiers, a glyph routine that restores from the clean art.
- `PluginSetting`: a choices hook for the `Skin` row; the `Glass` sub-page.
- `tools/mkskin.py`; the stock skins from `scratchpad/mf35-skins/`; a board
  partition or the site zip for seeding.
- `test_panel.cpp`, on the host:
  - every big-layout rectangle on its glass, and no two field boxes
    overlapping, in both orientations;
  - eleven rows; a caller row's columns ending by 301;
  - the LED row at most 3,840 px for n = 1 to 16, no cell overlapping;
  - the graph's height for 0, 1, 5, 1,240 and 131,071 bytes a second
    (0, 2, 5, 19, 28 up);
  - the sweep's rectangle at the head and at the wrap;
  - `Dirty::add` full: the result under 20% of the glass for a queue of
    small rectangles;
  - the manifest parser: the stock five pass; a glow overlap, a rectangle
    off the glass, a missing `format`, a duplicate key, a gap in the
    numbering and a progressive JPEG each refuse with the named line;
  - the text tiers at 52 x 1, 21 x 8, 20 x 7 and 19 x 7 with 0, 3 and 11
    callers, against the lines this report lists.

## Implementation order

Cheapest and most visible first; each step is on the glass on its own.

- The ILI9488 band path and a 480 x 320 framebuffer (the platform lane's,
  and the gate for everything else).
- The big landscape layout: the header at 480 with the static name and the
  two-page slot, the fixed node table with doing, on and terminal, the calls
  heading and the recent list, the LED row. This alone turns 46% black
  into the whole board.
- The system cells, the band word and the dBm figure.
- The traffic label and the sweep.
- The motion budget: the dot, the fade extent, `Dirty::add`.
- The pips, with the `Bbs` hand-back.
- Portrait.
- Skins: the parser and its checks with `status` as the fallback, then the
  decode on the worker, then the screen text, then the glow and
  `lights::panelDrive`, then flags, `bus`, activity and figures.
- CONFIG: the `Glass` sub-page, then the `Skin` row.
- `mkskin.py`, the stock skins and their seeding.
- The camera cell, when a board with a camera and a glass exists.
