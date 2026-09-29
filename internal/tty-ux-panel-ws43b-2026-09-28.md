# The 4.3B panel: tightening the tall landscape layout

Design review of the `panel` plugin on the Waveshare ESP32-S3-Touch-LCD-4.3B:
an 800 x 480 ST7262 on the RGB bus, drawn as a 400 x 240 logical picture and
shown with every pixel doubled. Read against the working tree of
`release-prep/wt-ws43b` (uncommitted, 2026-09-28): `src/plugins/panel.cpp`,
`src/plugins/panel_gfx.h` (`layout()`, the `tall` case), `panel_font.h`,
`src/board.h` (block `BBS_BOARD_WS_S3TOUCH43B`) and
`src/platform/platform_esp32_rgb.cpp`. Prior art: revision 2 of
`internal/tty-ux-panel-2026-09-24.md` (the 1.47 stick, which shipped) and
`wt-mf35/internal/tty-ux-panel-mf35-2026-09-26.md` (480 x 320, another
branch).

Nothing was built, run or rendered on a glass: Rob's rule is no testing
without his go. The mock-ups below are laid out by a scratch script from the
exact boxes in this report, one character to 8 logical px, so the columns are
arithmetic rather than typing. Font metrics were read from the Spleen arrays.

Every size is in logical px (the 400 x 240 picture) with the glass size
beside it where it matters. The 2x doubling stays.

## The glass

- 800 x 480 over 4.3 inches: 933 px diagonal, 217 ppi, 0.117 mm a physical
  pixel, **0.234 mm a logical pixel**. Active area about 94 x 56 mm.
- 8 x 16 face: a 3.75 mm cell, capitals rows 2 to 11, so **2.3 mm caps**.
  At the 5 arcminute limit of normal sight that is legible to about 1.6 m and
  comfortable at arm's length. This is the arm's-length face.
- 16 x 32 face: a 7.5 mm cell, capitals rows 6 to 25, descenders to row 31,
  so **4.7 mm caps**. Legible to about 3 m. This is the across-the-room face.
- 50 small glyphs or 25 big ones across; 15 small rows or 7 big ones down.
- Status glyphs, 9 x 12 at most: 2.1 x 2.8 mm. At arm's length a shape,
  across a room a coloured dot. That is enough: their colour is their meaning.

## The verdict

Good, and fixable inside the `tall` branch without touching the stick. The
scaling decision was right: 8 x 16 at 2x is the correct face for the lists,
and the two-column body reads. What is left is the 40-column habit in three
places. The header is the stick's header, so the one message that must carry
across a room, `<handle> is ringing`, is 2.3 mm tall. The two columns do not
match: the right one has no heading, starts on the heading line and holds
seven rows against the left's six. And the vertical budget was not re-measured:
10 px of black sits above the foot rule while the callers cap at six, so seven
on already hides one behind `+N more` while old logoffs keep their rows. Fix
the header, square the columns, let callers flow into the right column, and
the panel reads as built for this glass.

## Revision 1: after Rob saw the glass (2026-09-28)

Rob has the 4.3B on his desk: touch works and it looks good. Three changes
from him, specified here. They replace finding 5's squares and the system
row's thermometer; everything else in this report stands. Still read and
mocked only, no build or glass.

### 1. The lights: a light bar across the foot, and the drive lamp

Rob: the square size is fine for now, but use them in more places, circles
"or something fun", or rectangles taking the whole bottom area, which "might
look better than how you have it". It does. The rectangles win on this glass,
and the reason is distance: ten 16 px squares are a row of beads 200 px wide
that means something only up close. Ten segments across the whole foot make a
bar graph, and in the nodes family of effects the number of lit segments IS
the number of callers, readable from across the room. That is the one figure
the header could not carry at that distance (finding 1 gave the header the
ring, not the count).

Where the lights go:

- **The foot, full width, as segments.** The strip, one segment a pixel.
- **The foot's left end, the drive lamp.** The lights plugin already
  computes the drive light every frame on this board (its tick draws both
  outputs while the panel wants the strip), and nothing shows it: the 4.3B
  has no drive pin. Put it on the glass. A short lamp at the left of the
  foot is the 1541 beside the machine: amber on a card access, cool white on
  flash, red blinking on a storage error, the amber idle glow at rest, all
  from `drawDrive()` as it stands. It is the fun part and it costs 3 bytes.
- **Not the header.** The bell's blink and the ring are the only things
  that move up there; a strip echoed into the bar or the band would compete
  with the one message the header exists for.
- **Not a light edge.** A thin copy of the strip down a side or along the
  track repeats the same information in a smaller size. The track already
  has its dot; the glass needs one place for the lights, drawn well.

Shapes, and why not circles: a circle at this scale is a 16 px staircase in
0.47 mm steps. It reads as round at arm's length, but ten of them spaced across
356 px are still beads with gaps, and they do not fill the foot. Circles belong
to the 1.2.0 skins, where a machine's front panel has round lamps in the art.
The stick keeps its squares.

The foot, all in the box `R(4,220,392,18)` from finding 4's budget:

| Element | x | y | w | h | Rule |
|---|---|---|---|---|---|
| drive lamp | 4 | 222 | 20 | 14 | one lamp, the drive effect's colour |
| strip region | 40 | 222 | 356 | 14 | segments centred in it |
| segment i | `x0 + i (w + 4)` | 222 | `w` | 14 | `w = min(48, (356 - 4 (n - 1)) / n)`, `x0 = 40 + (356 - (n w + 4 (n - 1))) / 2` |

| n | segment | row | x | on the glass |
|---|---|---|---|---|
| 10 | 32 x 14 | 356 | 40..395 | 7.5 x 3.3 mm |
| 16 | 18 x 14 | 348 | 44..391 | 4.2 x 3.3 mm |
| 1 to 6 | 48 x 14 | 48n + 4(n-1) | centred | capped, so a short strip is not slabs |

The 16 px of black between the lamp and the strip (x 24..39) separates the
drive from the lines; at 16 the gap is 20.

Drawing, the shipped LED style stretched to a rectangle, so `drawLed()` works
unchanged on the new boxes:

- Lit: the whole segment in the colour at a third (the glow edge), the core
  one pixel in at full colour, and one "fun" touch: the core's top row in the
  colour mixed halfway to white, a 2 physical px gleam that makes a flat
  rectangle read as a lit lamp cap. `mix(col, kWhite, 1, 2)` over
  `R(x+1, y+1, w-2, 1)`.
- Off: a `kRule` ring one pixel in, `kSurface` inside it, as shipped. A dark
  foot is then a row of ten unlit segments, an idle switchboard, not a hole.

The foot at 4 logical px a column, ten segments, three lit:

```
col (4 px)  0         1         2         3         4         5         6         7         8         9
            0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
y 222..235   ddddd    ######## ######## oooooooo oooooooo ######## oooooooo oooooooo oooooooo oooooooo oooooooo
```

`d` the drive lamp, `#` a lit segment, `o` an unlit one. x 4..23, then
x 40..395 in 32 px segments with 4 px gaps.

Cost: the whole foot is 392 x 18 = 7,056 px, under one 9,600 px band, so the
shipped rule (more than three changed, send the whole box) sends one band a
frame at worst. The dot's stand-aside rule is unchanged.

Code shape, all gated so the stick's glass is untouched:

- `Layout` gains `ledShape` (SQUARE on the stick, BAR on tall) and `drive`
  (the lamp's box, empty on the stick). `segAt(box, i, n)` beside `ledAt()`
  returns the same `Led` (cell and lamp) for a bar; `refreshLeds()` picks by
  shape.
- lights: keep the drive frame the way `g_shown` keeps the strip's
  (`g_shownDrive[3]`), and `lights::panelDrive(uint8_t rgb[3], uint8_t& pct)`
  beside `panelFrame()`. 3 bytes. The panel draws the lamp through
  `glassLevel()` like the strip and redraws it only when its colour changes.
- The gleam is drawn only for BAR.

### 2. The default strip effect: `switchboard`, new, cheap

Today the default is `nodes` (`defaults()`, `g_stripFx = SF_NODES`): one
pixel per line in the caller's rank colour. It means the right thing and
looks dead exactly when the panel should look alive: with nobody on, every
segment is dark. The lively effects that exist (`rainbow`, `scanner`,
`boing`, `c64`) are decoration with no meaning. `blinken` tracks traffic but
says nothing a person can read.

Specify one new effect that keeps `nodes`' meaning and adds a pulse:

- A busy line: exactly `nodes`. The rank colour at full level, dipping to 60
  for a frame on traffic (`blip`).
- The free lines: a scanner ghost. A head sweeps end to end over the whole
  strip, one pixel every 120 ms (1.2 s a pass at ten), its tail decaying by
  5/8 a frame as `scanner`'s does, drawn only on free pixels and at a quarter
  level (64 of 255), so it passes behind the callers without competing with
  them. Colour: the site's dial, `{127, 212, 255}`: a free line is something
  a caller can dial into, which is what dial means on the site and the
  panel. Not amber, which is the drive lamp's idle glow next door.
- All lines busy: no ghost, the whole bar in rank colours. Nobody on: the
  ghost sweeps the full width, and the board says it is up and waiting.
- Across the room: bright segments are callers, a moving faint one means
  alive.

Name `switchboard`, 11 characters: exactly what `pick()`'s 12-byte word
buffer holds. It is appended to `kStripFx` after `wifi`, never inserted, so
no existing number moves (the `static_assert` on `wordIndex` checks it).
`needLines` gains it. Per frame: one `gather()` (already paid by `nodes`),
n multiply-and-shift for the tail, n `put()` calls. The same order of work as
`scanner` plus `nodes`, at the plugin's frame rate, nothing on the panel's
side.

Per board: `defaults()` reads `BBS_LIGHTS_STRIP_FX` (a word index, default
`SF_NODES` in the fallback block of board.h), and the 4.3B block sets it to
`switchboard`. The stick keeps `nodes`, so its strip and glass are unchanged.
Brightness stays 10% as shipped: `glassLevel()` already draws 10% at 147 of
255 on the glass, and a strip wired later must not start at full current.
The ghost at 64 of 255 at 10% lands at about 34 of 255 on the glass: a dim
blue on black, visible and quiet.

It drives a wired strip the same way, so a sysop who adds one on this board
sees what the glass shows.

### 3. The system row's icons: the chip is the chip, the heap is memory

Rob: the temperature is the S3's own sensor, so it gets the chip. The heap
then needs its own icon, or two figures on one row carry the same picture.

- Temperature: `kIconChip`, as shipped, `kDial`. A die with pins is exactly
  what is being measured.
- Free heap: a new `kIconRam`, a memory module, `kDial`. A wide, flat body
  with three chips and contact fingers, so the two silhouettes differ at a
  glance: square and pinned against wide and toothed.
- `kIconTherm` is then unused on this glass; drop it (32 bytes).

```
kIconRam            kIconChip (shipped, for comparison)
................    ................   0x0000
................    .....XX..XX.....   0x0000
XXXXXXXXXXXXXXXX    .....XX..XX.....   0xFFFF
XXXXXXXXXXXXXXXX    .....XXXXXX.....   0xFFFF
XX............XX    ....XXXXXXXX....   0xC003
XX.XX..XX..XX.XX    .XXXXXXXXXXXXXX.   0xD99B
XX.XX..XX..XX.XX    .XXXXX....XXXXX.   0xD99B
XX.XX..XX..XX.XX    ...XXX....XXX...   0xD99B
XX............XX    ...XXX....XXX...   0xC003
XXXXXXXXXXXXXXXX    .XXXXX....XXXXX.   0xFFFF
XXXXXXXXXXXXXXXX    .XXXXXXXXXXXXXX.   0xFFFF
.XX.XX....XX.XX.    ....XXXXXXXX....   0x6C36
.XX.XX....XX.XX.    .....XXXXXX.....   0x6C36
................    .....XX..XX.....   0x0000
................    .....XX..XX.....   0x0000
................    ................   0x0000
```

```
constexpr Icon kIconRam = { 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xC003, 0xD99B, 0xD99B, 0xD99B,
                            0xC003, 0xFFFF, 0xFFFF, 0x6C36, 0x6C36, 0x0000, 0x0000, 0x0000 };
```

Every stroke is 2 px, the house rule, except the fingers' 1 px gaps. The body
sits on rows 2 to 12, level with the 8x16 capitals (rows 2 to 11), so the icon
and `118K` share a line.

The system row, after:

```
col   0         1         2         3         4
      01234567890123456789012345678901234567890123456789
  202 [RAM] 118K        @@ peak 4       [CHIP] 41C
```

| Cell | x | Icon | Figure | Colour |
|---|---|---|---|---|
| heap | 4 | `kIconRam` | `118K` | ink from 40 K, warm to 20 K, risk below |
| peak | 136 | `kIconCallers` | `peak 4` | ink |
| temperature | 268 | `kIconChip` | `41C` | ink to 59, warm to 74, risk from 75 |

The stick still draws `kIconChip` for its heap. Changing that would change the
stick's glass, so it is gated to tall now. At the stick's next panel revision
it should take `kIconRam` too, so that the chip means the chip on every glass.

### Revision 1, what stays

- The vertical budget of finding 4: the foot box is still 220..237, only its
  contents change.
- The squares on the stick, and `ledAt()` as it is.
- `nodes` as the stick's default and as a choice here.
- The drive effects as they are; this revision only shows them.

### Revision 1, implementation order

- The icons: `kIconRam` in, heap and temperature swapped on tall. Minutes.
- The light bar: `segAt`, `ledShape`, the gleam.
- The drive lamp: `panelDrive` in lights, the lamp's box and redraw.
- `switchboard` and `BBS_LIGHTS_STRIP_FX`.

## What it draws today (400 x 240, `tall`)

Boxes as `layout()` computes them: bar `R(0,0,400,22)`, slot `R(2,3,392,16)`,
band `R(0,22,400,20)`, glyphs `R(4,24,110,16)`, antenna `R(346,24,6,16)`,
clock `R(356,24,40,16)`, track `R(0,42,400,1)`, heading `R(24,48,172,16)`,
column rule `R(200,48,1,144)`, caller rows `R(4,68+20k,192,16)` k 0..5,
recent rows `R(206,48+20j,190,16)` j 0..6, rule1 `R(4,194,392,1)`, system row
`R(4,198,392,16)` at x 4, 100, 200, 300, rule2 `R(4,218,392,1)`, LEDs
`R(10,222,380,16)` (cell 16, square 12, a row of ten 160 wide from x 120).

```
BEFORE (4 on)
col   0         1         2         3         4
      01234567890123456789012345678901234567890123456789
    0 ##################################################  bar 0..21
    3 The Rusty Antenna#################################  slot, 8x16, 49 glyphs, 17 used
   22 ==================================================  band 22..41
   24 [sd][ml][tw]                               A 16:20  glyphs 4..113, empty 114..345
   42 -----------------o--------------------------------  track
   48 @@ Callers 4/11          | >> 16:20 visitor         an event on the heading line
   68  S] quantumrob        2h | >> 16:16 S3Bench
   88  1) daytona          12m | << 16:08 daytona
  108  2* visitor           4m | !! 15:58 zaphod
  128  5) S3Bench          51m | >> 15:41 visitor
  148                          | << 15:12 zaphod
  168                          | >> 14:50 zaphod         rows end 184
  184                          |                         10 px of black, 184..193
  194 --------------------------------------------------  rule1
  198 ## 118K     ## 23 today  @@ peak 4    ## 41C
  218 --------------------------------------------------  rule2
  222                [][][][][][][][][][]                 12 px LEDs in 16 px cells, x 120..279
```

`@@` a 16 x 16 icon, `>> << !!` the login, logoff and bell icons, `A` the
antenna. The grid is 8 px, so the column rule at x 200 and the icon at x 206
are shown one column apart.

## Findings, worst first

### 1. The ring is 2.3 mm tall on a glass read across a room

`panel_gfx.h` `layout()`, the lines before the branch: `L.bar = R(0,0,w,22)`,
`L.slot = R(2,3,...,kSmallH)`, and `panel.cpp` `drawSlot()` draws with
`big = false`. The 400-wide canvas gives the slot 49 small glyphs; the name
uses 17 and the other 32 are blue. Worse, the ring override (`slotTick`,
`<handle> is ringing` in `kBusy`) is the one message the panel exists to
carry to somebody not at the desk (`panel.cpp` says so at `wake`), and at 2.3 mm
caps it is a smear of orange from three metres.

Should be: the bar 34 tall, the slot in the 16 x 32 face, 24 glyphs.

- Bar `R(0,0,400,34)`. Slot `R(4,1,392,32)`: 392 / 16 = 24 glyphs. The cell
  sits at y 1..32, so capitals land on rows 7..26: 7 px of blue above, 7
  below, centred. Descenders reach row 32, one pixel clear of the band. A
  32-tall bar at y 0 was checked and rejected: `g`, `j`, `p`, `y` reach row
  31 and sit on the band's edge, and board names have a `y` in them.
- The bar is then 14% of the glass's height. The stick's landscape bar is
  22 of 172, 12.8%. Same proportion, not a hero.
- Page A: the name, `cutWords` at 24. Page B: `IP:port`, 21 glyphs at the
  IPv4 maximum (`255.255.255.255:65535`), whole. Page C:
  `up 12d 3h  7.4 GB`, 17. The ring: the handle cut at `24 - 11 = 13` glyphs
  so ` is ringing` stays whole.
- Colours unchanged: ink, dial, risk for `no network`, busy for the ring,
  the fade mixed toward `kBar`.
- Considered and rejected: flashing the whole bar in busy during a ring. The
  big busy words already read at three metres, and a 2 Hz flashing stripe
  8 mm tall on a desk is an alarm, not a status.

### 2. The two columns do not match

`layout()`, tall case: caller rows `R(4,68+20k,192,16)`, recent rows
`R(206,48+20j,190,16)`. The left column is a heading and six rows; the right
is seven rows with no heading, the first on the heading's own line, so
`16:20 visitor` reads as part of `Callers 4/11`. The columns are 192 and 190
wide, and the gutters either side of the rule are 4 and 6.

Should be: two identical columns, each a heading and six rows.

- Left column x 4..193, right x 206..395, both 190 wide. Column rule at
  x 199. Gutters 5 and 6, margins 4 and 4: `4 + 190 + 5 + 1 + 6 + 190 + 4`
  is 400.
- Right heading: the handset icon at `(206,60)` and `Calls 23 today` at
  `R(226,60,170,16)`, `kStruct`, `kDim` at 0. It is the MF35 spec's heading,
  so the two big glasses agree, and it gives the right column a figure the
  way `Callers 4/11` gives the left one.
- The calls-today figure moves out of the system row into that heading, with
  its icon. Nothing is lost; nothing is shown twice.
- Equal widths matter for finding 3: a caller row that flows into the right
  column has to look exactly like one on the left.

### 3. Seven on already hides a caller while old logoffs keep their rows

`callerCap = tall ? kListMax / 2 : 3` and `allocate()`'s landscape branch,
which gives the recent list the right column whatever happens on the left.
With six caller slots, seven on is five names and `+2 more`, while the right
column spends seven rows on logins and logoffs from an hour ago. That is the
opposite of Rob's revision 2 rule for the stick: "sacrifice the last log for
the callers online ... fits all 10 nodes".

Should be: callers flow down the left column and on into the right one; the
recent list takes what they leave, below a rule.

- Twelve slots: 0..5 left, 6..11 right, both at `y = 80 + 20 (k mod 6)`.
- `callers = min(on, 12)`. `recent0 = max(callers, 6)`, `recentN = 12 -
  recent0`. A rule in the air above slot `callers` when
  `6 < callers < 12`, drawn as the stick's portrait one is (`slotRule`,
  `gaps` on).
- The sysop line plus ten nodes is eleven, so every caller always has a row
  and at least one recent event stays on the glass. `+N more` stays as the
  fallback and is never reached on a ten-node board.
- `BBS_LCD_LIST_MAX` in `board.h` goes 13 to 12, and its comment to "six and
  six".

| on | left column | right column | rule |
|---|---|---|---|
| 0 | `nobody on` (finding 7), then blank | 6 recent | no |
| 1 | 1 caller, 5 blank | 6 recent | no |
| 6 | 6 callers | 6 recent | no |
| 7 | 6 callers | 1 caller, rule, 5 recent | above slot 7, y 98 |
| 10 | 6 callers | 4 callers, rule, 2 recent | above slot 10, y 158 |
| 11 | 6 callers | 5 callers, rule, 1 recent | above slot 11, y 178 |

### 4. The vertical budget was not re-measured

The last list row ends at y 184 and rule1 is at 194: 10 px (2.3 mm) of black
at the foot of both columns, while rows are rationed. Finding 1 needs 12 px
more header. The budget below finds it by closing that gap and dropping
rule2, which the LED row does not need (finding 5).

| y | h | what |
|---|---|---|
| 0 | 34 | bar |
| 34 | 20 | band; its contents at y 36 |
| 54 | 1 | track; the dot's 3 x 3 covers 53..55 |
| 55 | 5 | air, as the stick's `track + 6` |
| 60 | 16 | the two headings |
| 76 | 4 | air, and the slot boxes' `gaps` rows |
| 80 | 116 | six rows at pitch 20, the last 180..195 |
| 196 | 2 | air, as the stick's 2 px under its last row |
| 198 | 1 | rule1 |
| 199 | 3 | air |
| 202 | 16 | system row |
| 218 | 2 | air |
| 220 | 18 | LED box; 16 px squares at 221..236 |
| 238 | 2 | margin; 3 px under the squares to the edge |

Total 240. The system row's ink ends at 213, 7 px above the LED squares.

### 5. The LEDs were sized for a 172 px glass

Superseded by revision 1: the foot is a light bar of full-width segments
and the drive lamp. The box, `R(4,220,392,18)`, and the dropped rule2 stand.

`ledAt()` caps the cell at 16 and the square at `cell - 4`, so ten LEDs are
12 px squares (2.8 mm) in a 160 px row: 40% of the width. On the stick they
were 7% of the width each; here 3%. They are the panel's retro signature and
its busiest moving part, and they have shrunk to less than half their
proportion.

Should be: a cell of `min(20, w / n)`, the square `cell - 4`, the box 18 tall.

- Ten LEDs: cell 20, square 16 (3.75 mm, one small text cell), the row 200
  wide from x 100. Sixteen: `380 / 16 = 23`, so cell 20, square 16, the row
  320 wide from x 40. Nothing touches at any count.
- Lit and off exactly as shipped: glow edge at a third, core one pixel in;
  off a `kRule` ring round `kSurface`.
- rule2 goes on this glass. The LED row is a row of squares on black; it is
  its own edge. The foot is then one rule, the system row, and the LEDs.

### 6. 232 px of empty band

The glyphs end at x 113 (the full set is 104 px), the antenna starts at 346.
The two board states with no glyph, closed to callers and shutting down, are
exactly what a sysop walking past needs, and both accessors exist on this
branch: `syscfg::get().closed` and `Bbs::answering()` / `listening()`.

Should be: a band word at `R(122,36,216,16)`, 27 glyphs, left aligned, 8x16.
`SHUTTING DOWN` in `kRisk` while `listening() && !answering()`, else
`CLOSED to callers` in `kWarm` while `closed`, else nothing. Redrawn on
change only. It is the MF35 spec's band word. It is the one new figure in
this report; everything else only moves. Rob's call whether it goes in now.

### 7. Nobody on is six rows of black

At 0 on, the left column is a dim heading over 116 px of nothing, which reads
as a list that failed to draw. The right column already answers this with
`nothing yet`.

Should be: slot 0 shows the quiet icon in `kFaint` and `nobody on` in
`kDim`, the same shape as `nothing yet`. Blank below it.

### 8. The system row, three figures

Icons superseded by revision 1: the heap takes `kIconRam`, the temperature
`kIconChip`.

With calls-today in the right heading the row is free heap, peak and the
chip's temperature. Three cells of 132 px at x 4, 136, 268: the icon at the
cell's x in `kDial`, the figure at `x + 20`, up to 13 glyphs. Colours as
shipped: heap ink from 40 K, warm to 20 K, risk below; peak ink; temperature
ink to 59 C, warm to 74, risk from 75, `--C` dim before a reading. No fourth
figure: uptime and the card's free space are page C of the slot, where Rob
put them.

## The layout, after

```
AFTER, four on
col   0         1         2         3         4
      01234567890123456789012345678901234567890123456789
    0 ##################################################  bar 0..33
    1 T h e   R u s t y   A n t e n n a ################  slot, 16x32, cell 1..32, caps 7..26
   34 ==================================================  band 34..53
   36 [sd][ml][tw]                               A 16:20
   54 -----------------o--------------------------------  track
   60 @@ Callers 4/11         |@@ Calls 23 today
   80  S] quantumrob        2h|>> 16:20 visitor
  100  1) daytona          12m|>> 16:16 S3Bench
  120  2* visitor           4m|<< 16:08 daytona
  140  5) S3Bench          51m|!! 15:58 zaphod
  160                         |>> 15:41 visitor
  180                         |<< 15:12 zaphod
  198 --------------------------------------------------  rule1
  202 ## 118K          @@ peak 4       ## 41C
  220 dd   ==============================================  drive lamp 4..23, light bar 40..395 (revision 1)
```

```
AFTER, nobody on, board closed
col   0         1         2         3         4
      01234567890123456789012345678901234567890123456789
    0 ##################################################
    1 T h e   R u s t y   A n t e n n a ################
   34 ==================================================
   36 [sd][tw]       CLOSED to callers           A 16:20  band word, warm
   54 -----------------o--------------------------------
   60 @@ Callers 0/11         |@@ Calls 0 today            both headings dim
   80 () nobody on            |() nothing yet
  100                         |
  ...
  198 --------------------------------------------------
  202 ## 118K          @@ peak 4       ## 41C
  220 dd   ==============================================  drive lamp 4..23, light bar 40..395 (revision 1)
```

```
AFTER, ten on
col   0         1         2         3         4
      01234567890123456789012345678901234567890123456789
   60 @@ Callers 10/11        |@@ Calls 31 today
   80  1) daytona          12m| 7) breadbin64        1h
  100  2* visitor           4m| 8) IBM5150          18m
  120  3> cosysop          33m| 9) amiga1200         7m
  140  4) vt220fan          9m|10) zaphod            3m
  158                         |------------------------  rule in the air above slot 10
  160  5) S3Bench          51m|>> 16:20 visitor
  180  6) apple2e           2m|>> 16:16 S3Bench
```

```
AFTER, eleven on, a ring
col   0         1         2         3         4
      01234567890123456789012345678901234567890123456789
    0 ##################################################
    1 v i s i t o r   i s   r i n g i n g ##############  busy, held for the ring
   34 ==================================================
   36 [sd][bl][ml][tw][st]                       A 16:20  the bell blinks at 2 Hz
   54 -----------------o--------------------------------
   60 @@ Callers 11/11        |@@ Calls 31 today
   80  S] quantumrob        2h| 6) apple2e           2m
  100  1) daytona          12m| 7) breadbin64        1h
  120  2* visitor           4m| 8) IBM5150          18m
  140  3> cosysop          33m| 9) amiga1200         7m
  160  4) vt220fan          9m|10) zaphod            3m
  178                         |------------------------  rule above slot 11
  180  5) S3Bench          51m|!! 16:21 visitor
```

### Every box, 400 x 240 (`tall`)

| Element | x | y | w | h | Face | Colour |
|---|---|---|---|---|---|---|
| bar | 0 | 0 | 400 | 34 | | `kBar` |
| slot | 4 | 1 | 392 | 32 | 16x32, 24 glyphs | by page; `kBusy` ring |
| band | 0 | 34 | 400 | 20 | | `kBand` |
| glyph strip | 4 | 36 | 110 | 16 | bitmaps | by flag, as shipped |
| band word | 122 | 36 | 216 | 16 | 8x16, 27 glyphs | `kWarm` closed, `kRisk` shutting down |
| antenna | 346 | 36 | 6 | 16 | bitmap | by RSSI |
| clock | 356 | 36 | 40 | 16 | 8x16, right | `kYellow`; `--:--` `kDim` |
| track | 0 | 54 | 400 | 1 | | `kTrack`, the dot |
| callers icon | 4 | 60 | 16 | 16 | icon | `kStruct`; `kDim` at 0 |
| callers heading | 24 | 60 | 170 | 16 | 8x16 | as the icon |
| calls icon | 206 | 60 | 16 | 16 | handset | `kStruct`; `kDim` at 0 |
| calls heading | 226 | 60 | 170 | 16 | 8x16, `Calls 23 today` | as the icon |
| column rule | 199 | 60 | 1 | 136 | | `kRule` |
| slot k, k 0..5 | 4 | 80 + 20k | 190 | 16 | 8x16 | lists, as shipped |
| slot k, k 6..11 | 206 | 80 + 20(k-6) | 190 | 16 | 8x16 | lists, as shipped |
| air rule | slot's x | slot's y - 2 | 190 | 1 | | `kRule`, above slot `callers` when 6 < callers < 12 |
| rule1 | 4 | 198 | 392 | 1 | | `kRule` |
| system row | 4 | 202 | 392 | 16 | 8x16 | cells at x 4, 136, 268 |
| rule2 | none | | | | | |
| foot | 4 | 220 | 392 | 18 | | drive lamp and light bar, revision 1 |

Row internals are the shipped ones in a 190 px row. A caller row: node at x,
mark at x + 16, both in the rank colour; the handle at x + 32 in `kInk`, cut
at 15 glyphs (`(190 - 32 - 24 - 8) / 8`); the time on right aligned to
x + 190 in `kDim`. A recent row: icon at x, `HH:MM handle` at x + 20, 21
glyphs, the shipped ageing. No doing or terminal column: a 190 px row is 23
glyphs and those two columns want 14 of what the handle has.

## The tap

What it does now, and the verdict on each:

- A tap on a sleeping panel wakes the backlight and does nothing else. Right:
  the first touch is to see, not to act.
- A tap on a lit panel turns the slot to its next page now. Right, and on
  this glass it is the only thing a tap should do.
- Ignored mid-fade and during a ring. Right: the ring must stay up for its
  whole life.
- Not polled while silent. Right: silent is a sysop's decision, often at
  night, and a passing hand must not undo it.
- A ring wakes a sleeping panel. Right.

One fix, now:

- **A tapped page holds 10 s, not 3.** A person taps to read the address in
  order to dial it, and 3 s plus a 0.96 s fade is too short to type 21
  characters into a phone. `kTapHoldMs = 10000`: the tap sets the next hold
  to it, and the hold after that goes back to `kHoldMs`. A few lines in
  `touchTick` and `slotTick`. The stick has no touch, so its glass cannot
  change.

Later, and needing no coordinates:

- **Long press, lights out.** A finger held 1.5 s turns the backlight off
  until the next tap or a ring. The backlight has no PWM, so this is the
  dimmer this board cannot have. It needs a bench look first: `touchPoll`
  sees only new GT911 reports, so whether the controller keeps reporting
  while a finger rests decides whether "held" can be timed.

Not worth building: regions. The rule that decides it: a tap may change what
the glass shows, never what the board does, because the panel sits in a room
and has no login. Inside that rule there is nothing to target: every figure
is already on the glass. Regions would also need the GT911's point registers
(0x8150 to 0x8153, physical 800 x 480, halved for logical), a second I2C read
per report, for no gain.

`Sleep after` 0 as shipped is right for a status panel.

## What stays as it is

- The 2x doubling and the 400 x 240 framebuffer. It is why the fonts need no
  third size and the bounce buffers stay 12.8 KB.
- 8x16 for every list row, heading and system figure. The lists are
  arm's-length content, and 16x32 rows would halve them to three a column.
- The 20 px pitch and 4 px of air: the stick's rhythm, and it reads.
- The palette and what each token means. The same thing is the same colour
  on the stick, the 4.3B and the site.
- The band: nine glyphs, their order, sizes and colours; the antenna; the
  clock in the band, small, as a phone's is.
- The track and the dot, the page rotation, the 8-step fade, the ring
  override. Only the slot's face changes.
- The callers heading's `publicBusy()/publicNodes()` and `shown()`: the desk
  gives away no more than WHO does.
- Revision 2's caller row and recent row formats and ageing.

## What would change the stick's glass: do not

The stick is `layout(172, 320)` and `layout(320, 172)`. Each of these would
move its pixels:

- Changing `kPitch`, `kSmallW`/`kSmallH`, or the bar, band, track, clock or
  antenna assignments made before the branch. Override them inside `tall`.
- Changing `ledAt()`'s cap of 16 for everyone. Give `Layout` an
  `ledCell` (16 default, 20 on tall) and pass it.
- Changing `allocate()`'s landscape branch unconditionally. Add a `flow`
  flag, set only on tall.
- Moving calls-today out of `drawSys()` for everyone, or drawing
  `nobody on` in every landscape. Gate both on the tall layout's flags.
- Drawing the slot big from the face alone. Use a `bigSlot` flag.

Glass-neutral on the stick, so allowed: growing `Field` by two (the calls
heading and the band word, empty boxes on the stick, never drawn; 160 bytes
of `g_shown`), and the fade optimisation below.

## Hand-backs for the builder

- `Layout` gains: `tall`, `bigSlot`, `flow`, `leftSlots` (6), `head2Icon`,
  `head2`, `word`, `ledCell`, and the system row's cell kinds (heap, today,
  peak, temperature) so `drawSys` draws what the layout names. The stick's
  kinds are {heap, today} and {heap, today, peak}; the tall glass's
  {heap, peak, temperature}.
- Bound the tall case as `w >= 380 && h >= 220 && h < 300`. The MF35 branch
  adds a `w >= 400 && h >= 300` layout; bounded this way the two cannot
  shadow each other whatever order the merge leaves them in.
- `gaps = true` on tall. The slot boxes then own the 4 px above them, where
  the air rule goes. Slot 0 and slot 6 boxes start at y 76; the headings end
  at 75, so nothing overlaps.
- `drawSlot`, `pageText` and `slotTick` take the glyph width from the face:
  `fit = slot.w / (bigSlot ? kBigW : kSmallW)`.
- `quietRow` takes its words, for `nobody on`, and keys on them.
- Optional: the fade queues only the text's extent,
  `R(4, 1, max(oldW, newW), 32)`, while still clearing the whole slot in the
  framebuffer. A big slot is 12,544 px, two of `lcdDraw`'s 9,600 px bands a
  step; a name of 18 glyphs or fewer is one. On the RGB bus a band is a
  19 KB PSRAM copy on the loop, well under a slow pass either way, so this is
  tidiness, not Rule 1.
- `host/test_panel.cpp`, for when Rob okays a run: the stick's two layouts
  unchanged field for field; every tall box inside 400 x 240 and no two text
  boxes overlapping; slots k and k + 6 on the same y; `allocate()` at on = 0,
  1, 6, 7, 10, 11 giving recent0 6, 6, 6, 7, 10, 11 and the rule false,
  false, false, true, true, true; the slot holding 24 big glyphs and
  `255.255.255.255:65535` whole; sixteen LEDs in 320 px with none touching.

## Implementation order

Cheapest and most visible first. All of it is the `tall` branch plus flags.

- The header: bar 34, the slot in 16x32, the band and track moved down, and
  the vertical budget of finding 4. One sitting, and the most visible change
  on the glass.
- Equal columns, the `Calls 23 today` heading, the three-cell system row.
- The flow: callers into the right column, the air rule, `BBS_LCD_LIST_MAX`
  12.
- Revision 1's icons, and `switchboard` as this board's default.
- The foot as revision 1 has it: the light bar and the drive lamp, rule2
  gone.
- `nobody on`.
- The 10 s hold on a tapped page.
- The band word, on Rob's go.
- Later: the long press, after a bench look at the GT911's reports.
