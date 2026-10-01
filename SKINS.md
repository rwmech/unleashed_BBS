<!--
µnleashed BBS: SKINS.md

Panel skins: the folder, skin.txt's grammar, the picture and its decoder,
how the board draws a skin, the limits, making one, and tools/mkskin.py.

Copyright 2026 - Robert Mech
License: GNU General Public License v3 or later
SPDX-License-Identifier: GPL-3.0-or-later

Documentation for µnleashed BBS, part of the same distribution as the
source. See the LICENSE file for terms.
-->

# Panel skins

**Applies to versions:** firmware 1.2.0.

A board with a display (`BBS_HAS_LCD`) draws its status panel one of two
ways. The built-in way, `status`, is the drawn layout `COMMANDS.md`
describes under `panel`. A **skin** is a picture instead: a painted or
photographed machine, 480 by 320 on the Makerfabs 3.5" panel. The board
lights its real lamps over the picture and writes its status lines on
it. The drive light follows the board's disk activity, the activity lamp
its network traffic, and a row of lamps the lights plugin's strip.

The first board it is for is the Makerfabs ESP32-S3 SPI TFT 3.5" (ILI9488,
480 x 320 landscape). Nothing in the format is tied to it. A skin names the
panel size it was drawn for, and one drawn for 172 x 320 runs on the
Waveshare S3's glass with the plug up. The host tests do exactly that.

- [The folder](#the-folder)
- [skin.txt](#skintxt): the grammar, every directive, the rules
- [background.jpg](#backgroundjpg): what the board's decoder takes
- [How the board draws a skin](#how-the-board-draws-a-skin)
- [Limits and costs](#limits-and-costs)
- [Making a skin](#making-a-skin), and [tools/mkskin.py](#toolsmkskinpy)
- [The stock skins](#the-stock-skins), and how they reach a card
- [The code](#the-code): files, interface, tests

## The folder

```
<card>/skins/<name>/skin.txt          the manifest
<card>/skins/<name>/background.jpg    the picture, the panel's size
```

or, the same two files as a **pair** beside the folders, the shape a
transfer gives (a YMODEM upload names one file and no folder):

```
<card>/skins/<name>.txt               the manifest
<card>/skins/<name>.jpg               the picture
```

The board looks for the folder first, then the pair. `tools/mkskin.py pair`
turns a folder into a pair.

- `<name>`: 1 to 24 of `A-Z a-z 0-9 _ -`. `status` is the built-in and
  cannot be a folder's name. The name is what `CONFIG panel` offers and
  what `system.cfg` holds (`[plugin:panel] skin = <name>`).
- Anything else in the folder is ignored. A `README.txt` beside them rides
  along in `mkskin.py pack`.
- Skins live on the SD card only. The picture is decoded once into PSRAM
  when the skin is chosen or at boot, so pulling the card afterwards leaves
  the skin on the glass until the next restart or the next change of
  skin.

### Through the BBS: the Skins file area

On a board with a display the file areas gain a built-in **Skins** area
(`files.cpp`, `kAreaSkins`): number 12, or 14 on a board that also has a
camera, on the card's `skins/` folder. Staff may look and download; only the
sysop uploads. What the sysop uploads goes in at once, with no approval, as a
backup does: whole in staging first, then renamed in, so a broken transfer
leaves nothing half-written. It takes a skin's pair and nothing else:
`<name>.txt` and `<name>.jpg`, the name as a folder's would be, refused by
name before a byte is sent otherwise (`Skins takes <name>.txt and
<name>.jpg, name 1-24 of A-Z 0-9 _ -.`). A file sent again replaces the one
there, which is how a skin is corrected. Each upload or erase there calls
`skin::uploaded(name)`. That reads the card's list again (so CONFIG offers the
skin once both halves are in), clears a failure on record for that name, and
reloads a skin of that name from the card if it is the one on the glass.


## skin.txt

Plain ASCII, one directive a line. The reader is
`src/plugins/skin_manifest.h`; `tools/mkskin.py` holds the same rules, and
both read `host/skins/cases.txt` in their tests, so the two agree on every
case there.

### Lines

- A line is a directive, a blank line or a comment. A `#` as the first
  thing on a line makes it a comment. A `;` anywhere starts a comment to the
  end of the line (a `#` elsewhere is a colour).
- Words are separated by spaces or tabs. Directive and option names are
  read in any case. Numbers are plain decimal, at most five digits, with no
  sign (`+480`, `0x1E0` and `000480` are refused).
- CR LF line ends are fine.
- The file is at most **4096 bytes**, a line at most **120 characters**
  (not counting its CR), and a line at most **20 words**.
- Only printable ASCII and tabs. Any other byte, NUL included, is refused
  with its line.

### Directives

| Directive | Meaning |
|---|---|
| `skin 1` | The format. The **first directive**, always. A later format is refused (`skin format 2; this board reads format 1`). |
| `panel W H` | The panel it is drawn for, in pixels, 1 to 1024 each. **Required.** A skin for another size is not offered in CONFIG and, if named anyway, not loaded. |
| `name TEXT` | What a person calls it, the rest of the line: 1 to 24 characters. Optional. `PANEL` shows it beside the folder's name: `Skin c64 (Breadbin and drive)`. |
| `drive X Y D STYLE [halo=N]` | The drive light: centre `X Y`, lens diameter `D`, and the style it lights in: `pc`, `1541`, `disk2` or `breathe` (the lights plugin's `drive_fx` words). |
| `activity X Y D [colour=#RRGGBB] [halo=N]` | The network activity lamp, lit in its colour (the panel's green, `#5DDC7A`, when not given) on the frames the board moved bytes. |
| `strip N` | `N` strip LEDs follow, 1 to 16. Must come before its `led` lines. |
| `led I X Y D [halo=N]` | Strip LED `I` (1 to `N`), each exactly once. LED 1 shows the lights plugin's pixel 1. |
| `text X Y W H [size=..] [colour=..] [shadow=..] [background=..] [align=..]` | The status rectangle: top left `X Y`, `W` by `H`. |
| `lines WORD ...` | What the rectangle shows, top to bottom, one word a line, 1 to 16 words. |
| `clock X Y [size=..] [colour=..] [shadow=..] [background=..]` | `HH:MM` (`--:--` with no clock), top left at `X Y`. |

`skin` and `panel` are required and everything else is optional. Each
directive appears once, and `led` once per LED. `color=` is read as
`colour=`.

Options, `name=value`, each at most once and only where the table allows:

| Option | Values | Default |
|---|---|---|
| `halo=N` | 0 to 32: how far the glow reaches past the lens, pixels | half of `D`, 32 at most |
| `colour=#RRGGBB` | exactly `#` and six hex digits | text `#C8C8C8`, clock `#FFD35C`, activity `#5DDC7A` |
| `size=` | `tiny` (6 x 12, 1.2.0), `small` (8 x 16) or `big` (16 x 32), the panel's Spleen faces | `small` |
| `shadow=` | `#RRGGBB`, or `none`: a copy a pixel down and right, under the text | `none` |
| `background=` | `#RRGGBB`, filling the rectangle, or `none`: drawn over the picture | `none` |
| `align=` | `left`, `centre` (`center`) or `right`, per line (`text` only) | `left` |

Line words for `lines`:

| Word | Shows | Example |
|---|---|---|
| `name` | the board's name (`CONFIG board`) | `The Rusty Antenna` |
| `address` | the address and port to dial | `192.168.0.40:6400`, `no network` |
| `uptime` | since the board started | `up 3h 14m` |
| `callers` | callers on of lines, as the directory counts them | `Callers 2/11` |
| `today` | calls today | `14 calls today` |
| `heap` | free heap | `84K free` |
| `card` | the card's free space | `card 7.4 GB free`, `no card` |
| `clock` | the time | `21:47`, `--:--` |
| `date` | the date, blank with no clock | `Sat 26 Sep` |
| `last` | the newest event: a login, logoff, page or ring | `21:40 login alice` |
| `ring` | who is ringing the sysop, blank otherwise | `bob is ringing` |
| `who` | who is on, one a line, the sysop's line first, for **every row left** in the rectangle; so it comes last | ` 1) alice 7m` |
| `blank` | an empty line | |

Hidden and lurking staff are never named, as on the status layout.

### Live widgets (1.2.0)

So a drawn machine's own parts carry the board's figures: a monitor that is
a waiting-for-caller screen, a turbo display showing callers online, a front
panel whose lamps are the lines. Each may be placed as often as its limit
allows, and each joins the geometry rules below.

| Directive | Most | Draws |
|---|---|---|
| `field X Y W VALUE [label=TEXT] [size=] [colour=] [shadow=] [background=] [align=]` | 24 | one line, one glyph tall, bound to a value; `label` a fixed prefix of up to 12 characters, `_` for a space |
| `digits X Y H N VALUE [colour=#] [dim=#RRGGBB\|none]` | 4 | an `N`-digit (1 to 6) seven-segment display `H` tall (10 to 64): a digit `H x 11/20` wide, a sixth of `H` apart (2 at least); unlit segments in `dim`, or the art; `clock` takes 4 and lights the colon. Default red `#FF3020` |
| `nodes X Y W H [size=] [colour=] [shadow=] [background=] [free=yes\|no] [dim=#] [tint=yes\|no]` | 2 | who is on, one row a line, the sysop's first; `free=yes` every line, the free ones as `waiting` (in `dim` when given); more on than rows ends in ` +n more`; `tint` colours the node and mark by rank |
| `events X Y W H [size=] [colour=] [shadow=] [background=] [order=newest\|oldest] [tint=yes\|no]` | 2 | the last logins, guests, logoffs, pages and rings (ten kept), `HH:MM kind handle`; `oldest` puts the newest at the foot, like a printout; `tint` colours the kind |
| `meter X Y W H SOURCE [colour=] [background=] [dir=right\|up]` | 8 | a bar with a lit edge: `traffic` (bytes a second, log scale to 64 KB/s), `heap` (free of the most seen), `card` (free of its size), `callers` (on of lines), `rssi` (-90 to -50 dBm) |
| `graph X Y W H SOURCE [colour=] [background=]` | 4 | a sweep, newest at the right, one column a sample, two samples a second (240 kept, two minutes): `traffic` or `callers` |
| `lamp X Y D STATE [colour=#] [halo=N] [blink=yes\|no]` | 32 | a lens lit by a state, drawn as the LEDs are; default red `#FF2A10`; `blink` flashes it at 2 Hz while lit |

`nodes` and `events` are at least 8 characters wide and one row tall; a
field at least one character wide; a meter or graph at least 2 x 2.

**No one unit draws more than 16,384 pixels at once** (`kUnitPxMax`, the
widgets' budget a pass): a field, the clock, a display, a meter or a graph
is its whole box; a row of a list or of the text rectangle is its width by
one glyph. A graph of 240 x 68 fits; a graph the width of the glass does not.
A graph shows 240 samples at most, so one wider than 240 px leaves its left
part empty; a list draws 32 rows at most.

Values for a `field` are the line words above less `who` and `blank`, and:

| Word | Shows | Example |
|---|---|---|
| `online` | lines busy, as the directory counts them (a caller still at the login prompt counts, so this can be more than the lamps and the node list show) | `3` |
| `lines` | the board's lines, as the directory counts them | `11` |
| `lastcaller` | the newest login's handle | `alice` |
| `rssi` | the Wi-Fi signal | `-58 dBm`, `no Wi-Fi` |
| `peak` | the most lines busy at once since boot | `5` |
| `version` | the firmware's version as shown | `1.2.0 (MF35 1.1.0)` |

A `digits` display takes the numeric ones: `online`, `lines`, `today`,
`peak`, `heap` (K), `rssi` (its magnitude) and `clock` (HHMM).

Lamp states:

| State | Lit while |
|---|---|
| `node1` .. `node16` | a caller the panel may name (logged in, visible, not lurking) is on that line: a glow (90 of 255), flaring full on each keystroke (a key in the last 150 ms); any case, `NODE3` as well |
| `sysop` | the sysop is on the sysop's line, visible |
| `online` | a caller the panel may name is on a caller's line |
| `rx`, `tx` | bytes came in, went out, since the last frame |
| `disk` | storage was touched in the last 80 ms |
| `error` | a storage error, blinking half a second on and off for ten seconds |
| `run` | always: the board is up |
| `closed`, `open` | the board is closed to callers (`CONFIG board`), or not |
| `ring` | a caller is ringing the sysop |
| `mail` | the sysop has mail unread |
| `staff` | staff are on, visible |
| `listed` | the directory lists the board |
| `card` | an SD card is mounted and working (dark while it is in error) |

Hidden and lurking staff light no lamp and fill no row, as in WHO. A node
row shows what its caller is doing (the command verb), which WHO shows staff
only: the glass is on the sysop's desk.

### Geometry rules, checked once the file is read

- **An LED's box** is `D + 2 x halo` pixels square, centred on `X Y`: from
  `X - side/2` for `side` pixels, with integer division, so an odd side's
  centre is the middle of pixel `X`. That box is what the board redraws
  and sends.
- **Every box lies inside the panel:** each LED's and lamp's, the text
  rectangle, the clock's (5 glyphs by 1: 30 x 12 tiny, 40 x 16 small,
  80 x 32 big) and every widget's.
- **No two boxes overlap.** Redrawing one must never paint over another.
  Two boxes may touch. An overlap is reported on the later element's line,
  naming the earlier one: `line 11: led 3's box overlaps led 2's (line 10)`.
- **The LEDs' and lamps' boxes cover 65,536 pixels at most** between them
  (each has a two-byte-a-pixel weight map in PSRAM).
- `lines` needs `text`, and `text` needs `lines`.
- The rectangle is at least one glyph wide, and the words fit its height:
  `lines` words x glyph height <= `H`.
- A `strip N` has every `led` from 1 to `N`.

### Errors

The first fault found is reported and the rest of the file is not read. It
comes with its line number, or with none for a rule about the whole file:

```
line 1: the first line must be 'skin 1'
line 3: drive style 'amiga' is not pc, 1541, disk2 or breathe
line 5: led 3 is outside 1 to 2
line 7: text's box 400,0 100x16 runs off the 480x320 panel
line 3: 3 lines of 16 px need 48 px; the rectangle is 40
no panel line
the LEDs' boxes cover 81920 pixels between them; 65536 at most
```

`host/skins/cases.txt` holds every one of them as a case.

### An example

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
lamp 334 168 6 run colour=#40FF50 halo=5
drive 356 168 6 1541 halo=5
; the computer's power lamp flickers with traffic
activity 452 235 5 colour=#FF3A20 halo=4
; the modem: MR AA CD RI RD SD
lamp 30 278 5 run colour=#FF3020 halo=4
lamp 48 278 5 open colour=#FF3020 halo=4
lamp 66 278 5 online colour=#FF3020 halo=4
lamp 84 278 5 ring colour=#FF3020 halo=4 blink=yes
lamp 102 278 5 rx colour=#FF3020 halo=4
lamp 120 278 5 tx colour=#FF3020 halo=4
```

`skins/stock/imsai/skin.txt` has an eight-LED strip and 29 lamps.

## background.jpg

Exactly the size `panel` names. It is decoded by the **TJpgDec in the
ESP32-S3's ROM**. IDF 5.3.1 exposes it as
`components/esp_rom/include/esp32s3/rom/tjpgd.h`, with `jd_prepare` at
`0x40000858` and `jd_decomp` at `0x40000864` (`esp32s3.rom.ld`), and
`ESP_ROM_HAS_JPEG_DECODE` set in `esp32s3/esp_rom_caps.h`. It costs no
flash. It is ChaN's R0.01 of 2012, fixed at a 512-byte input buffer and
RGB888 output, and it takes:

- **baseline** sequential Huffman only (SOF0): not progressive, extended,
  lossless or arithmetic-coded;
- a marker and its length as four bytes: no fill `FF`s between segments,
  and no segment of length 2 or less (R0.01 refuses both; later TJpgDec
  takes both);
- **three components, YCbCr**: not greyscale (added in R0.02) and not CMYK;
- **8-bit** samples;
- luma sampled **1x1, 2x1 or 2x2** (4:4:4, 4:2:2, 4:2:0), chroma 1x1;
- quantisation tables 0 to 3, 8-bit; Huffman tables 0 and 1;
- every table and frame segment no longer than **512 bytes**.

`src/plugins/skin_jpeg.h` checks all of that before the decoder runs, on
the board and on the host alike, and gives the reason in words:
`progressive JPEG: save it as baseline (not progressive)`,
`greyscale: save it in colour (YCbCr)`. The host decodes with TJpgDec
R0.03 (`host/tjpgd`, set up as the ROM copy is fixed). R0.03 accepts more,
greyscale above all, so without the shared check a skin could pass on
the host and fail on the board.

Anything else in the file (EXIF, a JFIF header, ICC, a thumbnail, a
comment) is skipped. Restart markers are fine. A file cut short in its
picture passes the header check and is refused by the decoder.

Pillow's `save(..., "JPEG", progressive=False)` takes, at any
`subsampling`, and so does `mkskin.py jpeg`. The stock skins are 4:2:0 at
quality 86 with optimised tables, about 20 KB each.

## How the board draws a skin

- **The setting.** `[plugin:panel] skin = <name>`, the `Skin` row of
  `CONFIG panel` (`Panel skin` at 80 columns). A PS_CYCLE over
  `status`, the card's skins for this glass's size in name order, and the
  file's own value even while it is not on the card, so a page saved
  without the card does not refuse itself.
- **Loading is a worker's, never the loop's** (Rule no. 1). `skin::want`
  starts a job (`plat::taskStart`, on the BBS core, three priorities
  below it). The job:
  1. seeds the stock set onto a card seen for the first time since it was
     mounted (below);
  2. lists `skins/` for CONFIG, reading each `skin.txt` for its panel size;
  3. reads the chosen `skin.txt` (4 KB at most) and parses it;
  4. checks `background.jpg`, then decodes it into a PSRAM block of its
     own, `[Manifest | background RGB565 | LED weights]`, yielding after
     every row of blocks;
  5. works out each LED's weights.

  It touches nothing the loop uses. Whatever was on the glass stays there
  meanwhile: the skin before, put back from its copy, or the status
  layout. A skin changing to another never shows the status layout
  between them, which on the MF35's big glass cost the loop some 24 ms to
  draw whole. For the length of a load the old skin's block and the new
  one's are both in PSRAM (600 KB at 480 x 320, of the MF35's 1.7 MB
  free); a load that finds no room for the second is tried once more
  without the first. If the new one fails, the old one goes too and the
  status layout shows, since the setting names the new one. A load called
  off (another skin chosen, the panel stopped) costs nothing but itself.
- **Taking it over.** The loop takes the block once the job says done.
  It copies the background into the framebuffer **32 rows a tick** (a
  whole 480 x 320 copy at once is some 15 ms of PSRAM traffic) and queues
  the whole glass. The lines follow on the next tick and the LEDs on the
  one after, so no single pass carries all of it. The backlight waits for
  that frame when it is owed, as it does for the status layout. The
  console says how long the load took and the worker's least free stack:
  `panel: skin c64 loaded in 240 ms, worker stack 3100 bytes spare`
  (figures illustrative; the host has no stack figure and says 0).
- **Each frame.** The LEDs are re-read every 40 ms, as the status layout's
  strip is:
  - the drive light from `lights::panelDrive(style, ...)`: `drawDrive` in
    the skin's style, from the disk state the lights plugin keeps, error
    blink included;
  - the strip from `lights::panelFrame`, whatever its effect, wired or
    not;
  - the activity lamp from whether `Bbs::bytesIn() + bytesOut()` moved.

  **The widgets** (1.2.0) are units: a text row, the clock, a field, a
  display, a list's row, a meter, a graph. Each keeps a hash of the words or
  the figure it last drew. When the figures are fresh (twice a second), a
  sample was taken, or the last pass stopped short, a pass redraws the units
  whose hash moved (a graph's is its column heights, so a flat sweep is
  not redrawn), **16,384 pixels at most** (`kWidgetBudget`), and the
  next pass carries on from where it stopped. A pass that drew anything
  leaves the LEDs to the next tick. The figures are the panel's, gathered
  from RAM (`panel.cpp`, `skinFigures`); the traffic history and the
  keystrokes are the skin's own samples of `Bbs::bytesIn`/`bytesOut` and
  `Session::lastInput`. Like the LEDs, a pass is held back while more than four
  rectangles wait for the glass.
- Only an LED whose colour changed is redrawn, and **a frame redraws
  8,192 LED pixels at most** (`kLedBudget`). The loop time a frame costs is
  bounded by pixels, not by how many LEDs changed: at the 65,536-pixel cap,
  with a strip effect changing every lamp every frame, it would otherwise be
  tens of milliseconds. Past the budget the rest wait for the next frame,
  taken in turn from where this one stopped, so none starves; at the cap a
  full change reaches every lamp within eight frames. The changed boxes are
  merged where joining two wastes no more than half again what they cover,
  so a row of lamps goes as one rectangle. No LED is drawn at all **while
  the panel's send queue holds more than four**: the panel sends one band a
  tick, and an effect that changes every pixel every frame would otherwise
  queue faster than it drains. The state is read afresh next frame, so a
  skipped frame is lost, not late.
- **Light, not paint.** Each pixel of an LED's box has two weights worked
  out once at load (`skin::ledWeights`). The first is how much of the
  colour: the lens's coverage from a 4 x 4 supersample, brighter towards
  the middle (0.72 at the rim to 1.0), then a halo falling as the square of
  the distance. The second is the hot spot, in the middle half of the lens.
  The colour is **screen-blended** over the art, `255 - (255 - art)(255 -
  light) / 255`, which only ever brightens. A dark colour is a faint glow,
  and black leaves the art exactly as painted: **paint the lens unlit**. A
  bright colour runs towards white in the middle. Colours go through the
  status layout's `glassLevel`, so a skin's lamps are the brightness the
  status layout's squares are.
- **Text.** Twice a second the panel gathers the same figures its own
  layout shows and puts them into words (`skin::Figures`). Each line whose
  words changed is redrawn: its row put back from the background (or
  filled with `background=`), the shadow, then the glyphs, cut at the
  rectangle, never wrapped. `who` rows beyond those on show are blank.
- **Silent mode** blanks the panel as it always has. When it ends the skin
  is copied back and drawn whole before the light comes on.
- **A CONFIG save** restarts the plugins. A skin whose name and glass size
  did not change is put back from its copy without touching the card.
  One whose glass changed size (a turn to portrait) is let go and read
  again, and refused if it was drawn for the other size.
- **When it cannot be used**, for no card, no folder, no `skin.txt`, a
  fault in it, the wrong size, or a picture the decoder refuses, the status
  layout is shown. The console says `panel: skin <name>: <why>; showing the
  status skin` and `PANEL` says `Not <name>: <why>`. It is tried again at
  the panel's next start (any CONFIG save), when a card comes or goes, or
  when a file for it arrives in the Skins area, and never every tick. A
  task that would not start counts as a failure the same way.
- **The card is held** while a job runs: `SD UNMOUNT` answers `The panel is
  reading its skin. Try again in a moment.` and a remount for new pins
  waits for the next save, as they do for the camera.

`PANEL` names the skin on the glass (`Skin c64`), why the one set is not
showing, and what CONFIG offers (`Skins status c64 pc`).

## Limits and costs

| | |
|---|---|
| skin.txt | 4,096 bytes, 120 characters a line |
| panel | 1 to 1024 pixels a side, the glass's size exactly |
| LEDs | a drive light, an activity lamp and 1 to 16 strip LEDs; lens 2 to 64 px, halo 0 to 32 px; 65,536 px of boxes |
| lines | 16 words |
| folders CONFIG lists | 16, in name order |
| PSRAM, one loaded skin | the background (w x h x 2: 300 KB at 480 x 320), the manifest and two bytes a pixel of LED boxes (at most 128 KB), in one block beside the panel's own framebuffer; freed when the skin is let go |
| heap during a load | TJpgDec's 5 KB work area and its decoder object, 4 KB for `skin.txt`, a `Manifest`, a 6 KB task stack; all given back when the job ends |
| static DRAM (S3) | +2,576 bytes, measured off the ELF (250,360 of 341,760): the figures, the job's list of skins and results, CONFIG's choices, the scene |
| flash (S3) | +24,208 bytes of image (1,294,432 of the 1.5 MB slot); the decoder itself is in ROM. The WROOM and the camera boards: a few hundred bytes (the shared task code), no static DRAM |
| time | about 150 ms of decode for 480 x 320 on an S3 (Espressif's figure is 52 ms for 320 x 180) plus the card's read, all on the worker; on the loop, a 30 KB copy for ten ticks once, then a redraw per changed LED |

## Making a skin

1. **Paint or pick the picture** at the panel's size. Leave the lenses
   **unlit**: the board brings the light. Leave the screen or label area
   for the status lines as empty glass. Use your own art, or art you have
   the right to share. The stock skins carry no maker's name, badge or
   logo, and a skin you share should not either.
2. **Place the lamps.** On a copy of the picture, paint each lamp as a
   solid disc of a key colour, one colour per kind. Paint the rectangle
   for the text, and one for the clock if you want one, as solid key
   colours too:

   ```
   python tools/mkskin.py leds keyed.png --style 1541 \
       --key drive=#FF00FF --key activity=#00FFFF --key led=#FFFF00 \
       --key text=#00FF00 --key clock=#FF8000 -o skins/mine/skin.txt
   ```

   Each key colour's areas become lines: the centre and the larger side as
   the diameter. Strip LEDs are numbered by rows, top to bottom, then left
   to right. The text key gives the rectangle and a starting `lines`.
   Edit the result: colours, halo, the words.
3. **Make the JPEG** the board decodes:
   `python tools/mkskin.py jpeg art.png -o skins/mine/background.jpg`
   (480 x 320 unless `--size`, baseline, 4:2:0, optimised).
4. **Check** it: `python tools/mkskin.py check skins/mine`. Every fault is
   listed with its line, and the folder name is checked too.
5. **Preview** it lit: `python tools/mkskin.py preview skins/mine -o
   mine.png`. The LEDs use the board's weights and blend, the text the
   panel's own font, sample figures in each line. It is what the glass
   shows, give or take RGB565.
6. **Put it on the board.** Either copy the folder to `skins/` on the card,
   or, with the card in the board, `python tools/mkskin.py pair skins/mine
   -o up` and send `up/mine.txt` and `up/mine.jpg` by YMODEM into the Skins
   file area as the sysop. Then choose it in `CONFIG panel`. `PANEL` says
   whether it took.

## tools/mkskin.py

Python 3.8 or later. `check` and `selftest` need nothing else; the rest
need Pillow.

| Command | Does |
|---|---|
| `check FOLDER ...` | Each folder: its name, `skin.txt` by the board's grammar, `background.jpg` by the ROM decoder's rules and against `panel`. Exit 1 on any fault. |
| `leds IMAGE --key KIND=#RRGGBB ... [--style S] [--halo N] [-o FILE]` | `skin.txt` lines from key colours (kinds `drive`, `activity`, `led`, `text`, `clock`), matched within 24 a channel. `-o` writes plain ASCII with LF endings: a shell redirect in Windows PowerShell 5.1 writes UTF-16, which the board refuses. |
| `preview FOLDER -o OUT.png` | The skin lit: the drive light amber (a card read), the activity lamp in its colour, the strip in a sample pattern, sample status lines. Checks first. |
| `jpeg IMAGE -o OUT.jpg [--size WxH] [--quality Q] [--stretch]` | Any picture as a background the board takes: cropped from its middle to the panel's shape and scaled with Lanczos, never stretched unless asked. |
| `pair FOLDER ... -o DIR` | Each folder as the pair the Skins file area takes: `DIR/<name>.txt` and `DIR/<name>.jpg`, checked first. |
| `pack FOLDER ... -o skins.zip` | Checks every folder, then zips them as `skins/<name>/...` (the JPEG stored, the text deflated). Unzip it at the card's root. |
| `selftest` | `host/skins/cases.txt` through its own reader: it must agree with the board's on every case. |

`tools/mkskins_stock.py` paints the stock skins: Pillow and numpy, three
times the size, brought down with Lanczos. Each skin's `skin.txt` is
written from the same numbers that placed its lenses.

## The stock skins

`skins/stock/`, about 115 KB of JPEG for all five. Each puts the board's
figures on the machine's own parts (the tty-ux spec,
`internal/tty-ux-skin-widgets-2026-09-26.md`); a modem on three of the desks
has its six lamps as a real one did: MR `run`, AA `open`, CD `online`, RI
`ring` (blinking), RD `rx`, SD `tx`.

| Folder | The scene | The screen | The devices |
|---|---|---|---|
| `c64` | a breadbin home computer, its disk drive, a monitor, a modem | name, clock, address, who is ringing, who is on, callers, calls today | the drive's PWR `run` and DRV `1541`; the computer's power lamp for traffic; the modem |
| `pc` | a beige tower, monitor and keyboard | a waiting-for-caller screen: bar, lines (tinted by rank), events, a traffic graph, status | callers on in the tower's window; POWER `run`, TURBO traffic, HDD `pc`; the keyboard's MAIL, RING, LOCK (`closed`) |
| `apple2` | the lidded computer, a green screen, two floppy drives, a modem | every line in 40 columns, the free ones dim | the top drive's IN USE `disk2`, the bottom one's `card`; the keyboard lamp for traffic; the modem |
| `atari` | the cream computer, its drive with a modem on it, a wood-grain TV | the events, newest at the bottom, the address under them | the channel readout is callers on, the tuning meter the Wi-Fi; the drive's PWR `run` and BUSY `1541` |
| `imsai` | a front panel of lamps and paddle switches, green-bar paper | the paper is the events; the readout name, clock, address, callers | status RUN WAIT(`closed`) INT(`ring`) HLTA(`error`) INP OUT DISK MAIL; PROGRAMMED OUTPUT the strip's pixels 1 to 8; DATA OPS DIR CARD; the address row the lines, A15 the sysop's |

They reach a sysop two ways.

- **The site's zip**, `mkskin.py pack skins/stock/* -o skins.zip`: unpack it
  at the card's root.
- **Seeded onto the card by the board** (`skin_seed.h`) the first time a
  card is seen after a mount. This is the rule the stock screens follow,
  with a skin's folder as the unit. `skins/.seeded` holds each stock
  file's hash as written:
  - a skin not on the card is written;
  - one whose every file is as recorded follows the new stock;
  - one where the sysop changed or removed any file is the sysop's, and
    is never touched or recorded again;
  - one with no record is the board's only if every file is byte for
    byte stock.

  Each file goes down under a temporary name and is renamed in. The set
  comes from `skin::stockFiles()` (`skin_stock.cpp`), which is empty in
  this build. Five skins are about 115 KB, which the 1.1.1 layout's 1.5 MB
  app slots could not spare on an S3. The S3 boards have 1.1.2's layout
  with 3 MB slots now, so the set can be embedded; that is still to do.
  Until then a board shows the status layout and a sysop copies skins from
  the zip.

## The code

| File | What |
|---|---|
| `src/plugins/skin_manifest.h` | the grammar and its rules: `skin::parse` |
| `src/plugins/skin_jpeg.h` | the ROM decoder's rules: `skin::checkJpeg` |
| `src/plugins/skin_draw.h` | the LEDs' weights and blend, the text rows, the `Scene` that keeps a skin current, `Figures` |
| `src/plugins/skin_seed.h` | the stock set onto a card: `skin::seed` |
| `src/plugins/skin.h`, `skin.cpp` | the runtime: the job, the loop's side, the interface the panel calls |
| `src/plugins/skin_stock.cpp` | the stock set in the image (empty until 1.1.2's layout) |
| `src/plugins/files.cpp` | the Skins file area: a pair uploaded straight in, `skin::uploaded` |
| `src/plugins/panel.cpp` | calls it: `want` at start, `stop`, `tick` every tick, `redraw` after silent, and fills `figures()` from what its own layout shows |
| `src/plugins/lights.cpp` | `lights::panelDrive`: the drive light in a given style |
| `src/platform/platform_esp32_task.cpp` | `plat::jpegDecode` (the ROM's TJpgDec) and `taskStart` for camera and panel boards |
| `host/jpeg_host.cpp`, `host/tjpgd/` | the host's decoder: TJpgDec R0.03, configured as the ROM copy |
| `host/test_skin.cpp` | the unit tests below |
| `host/skins/cases.txt` | the manifest cases both readers run |
| `host/skins/jpeg/`, `mkfixtures.py` | JPEG fixtures from Pillow, good and refused, with Pillow's own decode |
| `host/skins/e2e/` | two skins for the Waveshare S3's glass, for `test_board_s3_skin` |
| `tools/mkskin.py`, `tools/mkskins_stock.py` | the workbench and the stock set |

**The interface the panel calls** (`skin.h`):

```cpp
void        want(const char* name, uint16_t w, uint16_t h);   // at the panel's start
void        stop();                                            // at its stop
bool        tick(panelgfx::Canvas& c, panelgfx::Dirty& d,
                 uint32_t now, bool fresh);                    // true: the skin has the glass
Figures&    figures();                                         // the status lines' words
void        redraw();                                          // after silent mode
bool        live(), copying();
const char* choices(), *running(), *why(), *title();
bool        cardBusy();
void        uploaded(const char* file);                        // from the Skins area
```

The panel keeps its framebuffer, its queue and its band-at-a-time flush.
The skin only draws into the one and adds to the other, from the loop.
When `tick` returns false after returning true, the panel draws its own
layout whole.

**The load is a job on the background runner** (`core/runner.h`):
`jobMain` is the job's work, run on the runner's task (`BBS_RUNNER_STACK`,
8,192 bytes) below the loop, and the loop takes the result once the runner
says DONE. SD UNMOUNT and a remount wait for it through `runner::busy()`
like any other job on the card.

**Tests.** `make test` in `host/` runs `test_skin`:

- every case in `cases.txt`;
- a fuzz of 100,000 mangled manifests: none crashes, clean under `make
  SAN=1`, and every one accepted obeys the geometry rules;
- `checkJpeg` on Pillow's files, every ROM refusal included, and the host
  decoder's pixels against Pillow's own;
- the drawing: light only brightens, dark restores the art pixel for
  pixel, nothing outside a box changes, a shorter line leaves nothing
  behind, what changed is queued, and the LED budget (two 64 x 64 lamps a
  frame at the cap, every one within eight frames);
- the seeding rule;
- each stock skin drawn whole to `host/skins/out/<name>.ppm` for a person
  to look at.

`tools/harness.sh --board s3 --card --only=board_s3_skin` runs the whole
thing in the host board: skins on its card, loaded, drawn, read back by
`PANEL SHOT`, a broken one refused with its line, a missing one named,
back to status, then a pair sent by YMODEM into the Skins area, offered by
CONFIG at once and loaded. `python tools/mkskin.py selftest` holds the PC's reader to
the board's.
