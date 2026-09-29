<!-- DRAFT for the site agent to place, written 2026-09-26, revised the same
day for the Skins file area, and again for the live widgets (field, digits,
nodes, events, meter, graph, lamp), the tiny face and the rebuilt stock
skins. Not published.
Page: "Make your own skin", a new page for the directory site (suggested path
/skins, linked from /hardware's display boards and from /setup's CONFIG panel
section). Audience: a sysop with a display board, not a programmer.

Every fact is from the panel-skins worktree (branch panel-skins), checked in
the source rather than the prose: src/plugins/skin_manifest.h (the grammar and
its limits), skin_jpeg.h (the decoder's rules, including the fill bytes and
empty segment refusals), skin.cpp (skinPath: a folder first, then the pair;
scanSkins: what CONFIG lists; want() and skin::uploaded(): when a skin is
tried again or reloaded; the not-found messages), files.cpp (kAreaSkins,
skinFile, placeBackup: the Skins area, its levels, the refusal text, replace
on resend, E), skin_draw.h and lights.cpp (how the lamps are lit, the drive
light's colours, the strip past Strip len staying dark, the lights plugin
needing to be on), panel.cpp (the CONFIG row, PANEL's lines including the
skin's name in brackets, the words each status line shows), skin_seed.h (the
stock set on the card), tools/mkskin.py (every command on this page was run
against a copy of a stock skin: check, preview, jpeg, leds -o, pair, pack),
host/skins/cases.txt (the error messages).

The widgets revision: every fact from SKINS.md "Live widgets (1.2.0)", the
geometry rules and "The stock skins", checked against skin_manifest.h (limits,
defaults, option lists), skin_draw.h (the node row, digits clamped to all
nines, the meter scales, rows cut at the box), panel.cpp (hidden and lurking
staff kept out of the events) and skins/stock/*/skin.txt. Every skin.txt
line on the page was run through mkskin.py's parser, the green terminal
example through `check` and `preview` with a painted stand-in background,
and the two over-budget figures (a 480 x 35 graph, a six-digit readout 64
tall) were confirmed refused with the messages quoted.

For the site agent:
- Version, settled 2026-09-26 (main): panel skins AND the live widgets are
  part of the 1.2.0 hardware release; 1.1.2 is a patch that ships without
  them. So the whole page says "coming in 1.2.0" until a release carries
  it, and is then gated "::: from 1.2.0". The body says only "a board whose
  firmware is older than the widgets", on purpose, so the gate is the one
  place the number lives.
- Placeholder: /STOCK-SKINS-ZIP below is the stock skins download, skins.zip
  in card layout. Put the real link in.
- The feature ships with the S3 build that carries skins (COMMANDS.md says
  1.1.2). Gate the page, or its "you need" line, with "::: from" once the
  version is settled. The automatic seeding is written as "a later release"
  on purpose: its version is not settled.
- The Makerfabs board is not on /hardware yet. Link it once it is.
- The "Get the tool" step assumes mkskin.py is on main by the time this goes
  live.
- Numbered lists are used only where the order matters. Code blocks between
  numbered steps sit at column 0 on purpose: md_render does not take an
  indented fence inside a list item, and continues the numbering after one.
Copyright 2026 Robert Mech. -->
# Make your own skin

A board with a screen can dress itself up as a machine from another decade. A
skin is a picture of a computer, a terminal or a front panel, and the board
brings the machine's own parts to life: the drive lamp flickers when the card
is read, a monitor lists who is on, a seven-segment readout counts the
callers, a front panel has a lamp for every line, and a modem on the desk
lights CD when somebody is connected and blinks RI when somebody rings for
the sysop.

You draw the machine. The board makes it glow.

A skin is two files, a picture and a short text file, and nothing you put in
them can break the board. If a skin is missing or has a mistake in it, the
screen shows the board's built-in layout, called `status`, and the board tells
you what was wrong and on which line.

You can send a skin to the board over the same connection you call it with,
from your terminal program, without touching the card.

## What you need

- **A board with a display.** The skins that come with the board are drawn
  for the Makerfabs ESP32-S3 with the 3.5 inch screen, which is 480 by 320
  pixels. The Waveshare S3's small screen, 172 by 320, takes skins too, drawn
  for its size. See [the tested boards](/hardware).
- **An SD card** in the board's card slot. Skins live on the card.
- **A terminal program that can send a file by YMODEM**, such as SyncTERM,
  to send skins to the board. Or a card reader, to copy them onto the card.
- **A picture**, from any paint program, photo editor or pixel art tool.
- **A text editor**: Notepad, TextEdit in plain text mode, nano, anything that
  saves plain text.
- **For the helper tool, optional but worth it:** Python 3.8 or later and the
  Pillow imaging library, on a Windows, Mac or Linux computer.

## The skins that come with it

Five, drawn for the 480 by 320 screen. Each one gives the board's figures to
the parts of the machine that would have shown them, and each does one job
the others do not:

| Name | The scene | What is live on it |
|---|---|---|
| `pc` | A beige tower PC, its monitor and keyboard | The monitor is a sysop's waiting-for-caller screen: the board's name and the time, every caller with their rank in colour, the latest events, a two-minute traffic graph, and the address, calls today and uptime along the foot. A two-digit readout on the tower counts the callers. POWER is the board up, TURBO is network traffic, HDD is the drive light. The keyboard's three lamps are MAIL, RING (blinking) and LOCK, lit while the board is closed to callers |
| `c64` | A breadbin home computer, its disk drive, a monitor and a modem | The monitor lists who is on, under the board's name, the time, the address and who is ringing, with callers and calls today at the bottom. The drive's PWR lamp is the board up and DRV the drive light. The computer's power lamp flickers with traffic |
| `apple2` | A beige computer with a lid, a green screen, two floppy drives and a modem | The screen is the whole switchboard: the name, the address and every line in tiny letters, the waiting ones dimmed so the busy ones stand out. The top drive's IN USE lamp is the drive light, the bottom drive's lamp says a card is in. The keyboard lamp flickers with traffic |
| `atari` | A cream computer, its disk drive with a modem on top, and a wood-grain television | The television is an event feed, newest at the bottom, with the board's name, the time and the address. The set's channel readout is the number of callers on, its tuning meter the Wi-Fi signal. The drive's PWR and BUSY lamps are the board up and the drive light |
| `imsai` | A front panel of lamps and paddle switches, with green-bar printer paper | The paper prints the events. The readout shows the name, the time, the address and the callers. The status lamps are RUN, WAIT (closed to callers), INT (a ring, blinking), HLTA (a storage error), INP and OUT (traffic in and out), DISK and MAIL. Three data lamps are OPS (staff on), DIR (listed in the directory) and CARD. The address row has a lamp for each caller line, with A15 for the sysop's. Eight PROGRAMMED OUTPUT lamps show the lights effect |

The three with a modem on the desk light its six lamps the way a real modem
did, which the lamps section below explains.

[Download the stock skins](/STOCK-SKINS-ZIP): `skins.zip`, laid out the way
the card wants it. A later release puts them on the card by itself.

None of them carries a maker's logo or name. They are drawings of the kind of
machine, not of a product.

## The five-minute version

The quickest way to a skin of your own is to start from one that works.

1. Unzip the stock skins on your computer. You get a `skins` folder with one
   folder inside for each skin.
2. Copy one of them, the `pc` folder say, and name the copy after your skin:
   `my_tower`. A skin's name is 1 to 24 letters, digits, `_` or `-`, and
   cannot be `status`.
3. Replace `background.jpg` in the copy with your own picture, exactly 480 by
   320 pixels, saved as an ordinary JPEG with the progressive option off.
4. Open `skin.txt` in the copy and move the lights, the text and the widgets
   to where they sit on your picture. The sections below say how.
5. Check it, look at it lit, and turn it into the pair of files the board
   takes:

```
python tools/mkskin.py check my_tower
python tools/mkskin.py preview my_tower -o my_tower.png
python tools/mkskin.py pair my_tower -o to_send
```

6. Call the board, log in as the sysop and type `FILES 12`, which opens the
   Skins area. Press `U` and send `to_send/my_tower.txt` by YMODEM, then `U`
   again for `to_send/my_tower.jpg`.
7. Type `CONFIG panel`, choose `my_tower` on the **Skin** row and save.

## Get the tool

`mkskin.py` checks a skin by the board's own rules, converts pictures, finds
the lights for you, shows the skin lit, and packs it for sending, all before
anything reaches the board. It is in the `tools` folder of [the firmware's
repository](https://github.com/rwmech/unleashed_BBS): download the whole
repository (the green **Code** button, then **Download ZIP**) and run the tool
from inside it, because the preview borrows the screen's font from the
repository's source.

Pillow installs with:

```
python -m pip install pillow
```

On Windows the command may be `py` rather than `python`. On a Debian or Ubuntu
system that refuses `pip`, use `sudo apt install -y python3-pil` instead.

The `check` command needs no Pillow at all, only Python.

## The picture

**The picture is exactly the screen's size.** 480 by 320 for the 3.5 inch
screen. The board does not resize, and a picture of any other size is
refused. If you have turned the screen with the **USB plug** setting, the size
turns with it: stood on end, the Waveshare's screen is 172 wide and 320 tall,
on its side 320 by 172.

**The picture is a baseline JPEG.** The ESP32-S3 draws it with a small JPEG
decoder built into the chip, and that decoder is old and particular:

- Baseline, not progressive. If your editor's save dialog has a
  **Progressive** option, turn it off.
- Colour. A greyscale JPEG is refused, and so is CMYK, which is a printing
  setting.
- 8 bits a channel, which is what nearly every editor saves.
- Chroma subsampling of 4:4:4, 4:2:2 or 4:2:0. Every common setting is one of
  these.

If a picture will not pass, or you are not sure what your editor did, let the
tool make the JPEG for you. It takes a PNG, a JPEG or most other formats:

```
python tools/mkskin.py jpeg my_art.png -o my_tower/background.jpg
```

A picture of another shape is cropped from its middle to fit, never
stretched (add `--stretch` if you want it stretched). For the Waveshare, add
`--size 172x320`.

**Paint every lamp dark, the way it looks switched off.** The board adds light
on top of the picture and never takes any away, the way a real lamp behind
coloured plastic does. A dark red lens glows red when the board lights it and
sits there looking like an unlit lamp when it does not. A lens painted bright
already has nowhere to go.

Leave the machine's screen area in the picture looking like a screen, dark and
plain: the board writes its status lines there.

The stock pictures are about 20 KB each. Size is not a worry.

## skin.txt

A short text file that says where each light and the text go. Here is a
simple one, complete. It uses only the basics; the live widgets further down
build on them.

```
skin 1
panel 480 320
name Plain tower
; the hard disk lamp, a flicker for every read
drive 428 210 6 pc halo=5
; the turbo lamp, here for traffic
activity 392 210 6 colour=#FFC020 halo=5
; the monitor's tube, grey on black
text 46 58 224 128 colour=#C8C8C0 background=none
lines name address uptime callers today heap who
clock 222 38 colour=#C8C8C0
```

Positions are in pixels, counted from the top left corner of the picture: X
across to the right, Y down. Any paint program shows you the X and Y of the
spot under the mouse pointer.

What each line means:

| Line | What it does |
|---|---|
| `skin 1` | The format. Always the first line, always `1` |
| `panel 480 320` | The screen it is drawn for, width then height. Must match the picture |
| `name Plain tower` | A title, up to 24 characters. `PANEL` shows it beside the skin's name. CONFIG lists skins by their names, not their titles |
| `drive X Y D STYLE` | The drive light: its centre, the lens's diameter in pixels, and its style: `pc`, `1541`, `disk2` or `breathe` |
| `activity X Y D` | The activity light, which blinks with network traffic. `colour=#RRGGBB` sets its colour: green if you leave it out |
| `strip N` | How many strip lamps the skin has, 1 to 16. A `led` line for each follows |
| `led I X Y D` | Strip lamp number I: its centre and diameter. Each lamp from 1 to N, once |
| `text X Y W H` | The rectangle the status lines are written in: its top left corner, width and height |
| `lines ...` | What the rectangle shows, top to bottom. The words are in the next table but one |
| `clock X Y` | The time, as HH:MM, with its top left corner at X Y |
| `field`, `digits`, `nodes`, `events`, `meter`, `graph`, `lamp` | The live widgets, each explained in [its own section](#live-widgets) below |

Every line but `skin` and `panel` is optional. The lines in this table appear
once each, apart from `led` once for each strip lamp, and the live widgets as
many times as their limits allow. A skin with no lights and no text is a
picture on the screen, and that is allowed.

The details:

- A lens is 2 to 64 pixels across.
- `halo=N` on any light sets how far its glow spreads past the lens, 0 to 32
  pixels. Leave it out and the glow is half the lens's diameter.
- Options go after the fixed values, as `name=value`, with no spaces around
  the `=`. `colour` can also be spelt `color`.
- Colours are written `#RRGGBB`, the six-digit hex colour every paint program
  shows: `#FF3A20` is a warm red.
- Words can be in capitals or not. Spaces or tabs separate them.
- A line starting with `#` is a comment, and so is anything after a `;`.
- Plain text only, at most 120 characters a line and 4 KB for the file.

The text rectangle and the clock take these options, and so do the widgets
that write words (`field`, `nodes` and `events`):

| Option | What it does |
|---|---|
| `size=tiny`, `small` or `big` | Tiny letters are 6 by 12 pixels, small ones 8 by 16, big ones 16 by 32. Small if you leave it out |
| `colour=#RRGGBB` | The letters' colour. Light grey for text and yellow for the clock if you leave it out |
| `shadow=#RRGGBB` | A shadow one pixel down and to the right, for text over a busy picture. `none` for no shadow, as it is if you leave it out |
| `background=#RRGGBB` | A solid colour behind each line. `none`, as it is if you leave it out, writes the letters straight onto the picture |
| `align=left`, `centre` or `right` | Where each line sits in the rectangle. The text rectangle and a `field`, not the clock or the lists |

**Pick the size for the distance.** On the 3.5 inch screen, tiny capitals are
about 1.2 mm tall, small about 1.5 mm and big about 3 mm. From your chair, at
arm's length, only big letters and seven-segment digits 24 pixels tall or
more read comfortably. Small is for leaning in, and tiny suits a printout, a
full list of lines or a footer. So put what you want to see across the desk
(is the board up, how many are on, does anything need you) on lamps, digits
and big letters.

**Nothing may overlap.** Each light and each lamp takes a square box, its lens
plus its halo on every side. The text rectangle, the clock and every widget
take their own rectangles. No two of them may touch the same pixel, and
everything has to fit on the screen. The board redraws each thing by itself,
many times a second, and this is what lets it do that without smudging its
neighbour. Lamps close together want a smaller halo: a 6 pixel lens with
`halo=5` takes a 16 pixel box.

## The lights

Three kinds, each drawn as light rather than paint: a lens that glows brighter
at its centre, with a soft halo spreading onto the picture around it.

**The drive light** follows the board's real storage: amber when the SD card
is read or written, cool white for the board's own memory, and a slow red
blink after a storage error. Its style is how it behaves:

- `pc`: a short flash on each access and a flicker through a long one, like
  an IBM PC's hard disk lamp.
- `1541`: lit for the whole access, like the Commodore drive.
- `disk2`: lit for about a second after the last access, like the Apple II
  Disk II, whose motor kept running.
- `breathe`: a slow pulse at rest, with the access colour on top.

**The activity light** blinks with network traffic, in any colour you give it.
A power lamp or a turbo lamp on the picture makes a good one.

**The strip** is up to 16 lamps that show whatever effect the lights plugin is
running: `nodes` with a lamp for each caller line, `hayes` like a modem's front
panel, `blinken` like a mainframe's, and the rest listed on [the lights
page](/lights). It works with no LED strip wired to the board at all.

The drive light and the strip come from the lights plugin, so it has to be
switched on. On some boards it is on as shipped. If those lamps stay dark on
your skin, type `CONFIG lights` and switch it on; it runs happily with nothing
wired. Two of its settings reach the skin as well:

- **Strip len** is how many strip lamps the lights plugin drives, 10 as
  shipped. A skin with 16 lamps wants it set to 16, or lamps 11 to 16 stay
  dark.
- **Brightness** dims the lamps on the picture as it would dim a real LED.
  At 30% or more they are drawn at full brightness.

The activity light does not need the plugin, and neither do the lamps below,
which are a fourth kind of light: each one lit by a fact about the board that
you choose.

## Placing the lights with key colours

Finding the centre of a 6 pixel lens by hovering a mouse over it gets tedious
by the third lamp. The tool can find them for you: paint a dot of a bright
colour over each lamp on a copy of your picture, and it measures each dot and
writes the lines.

Any colour works as a key, as long as it appears nowhere else in the picture.
These are the ones the example below uses:

| Paint | Colour | How many |
|---|---|---|
| The drive light | magenta `#FF00FF` | One dot |
| The activity light | cyan `#00FFFF` | One dot |
| The strip lamps | yellow `#FFFF00` | Up to 16 dots |
| The text rectangle | green `#00FF00` | One filled rectangle |

1. Make a copy of your background, at full size, and save it as a PNG. Work
   on the copy only, never on `background.jpg`.
2. Paint a solid dot of its key colour over each lamp, the size of the lens,
   and a filled rectangle where the status lines go. Turn antialiasing off in
   your brush settings if you can.
3. Run the tool on the copy, naming each colour, the drive style you want and
   where to write the result:

```nowrap
python tools/mkskin.py leds keyed.png --key drive=#FF00FF --key activity=#00FFFF --key led=#FFFF00 --key text=#00FF00 --style 1541 -o my_tower/skin.txt
```

4. It writes a ready `skin.txt`, something like this:

```
skin 1
panel 480 320
drive 306 166 13 1541
activity 445 230 11
strip 4
led 1 265 285 11
led 2 285 285 11
led 3 305 285 11
led 4 325 285 11
text 48 52 189 113
lines name address callers who
```

5. Open it in a text editor and add what the tool cannot guess: a `name`, the
   activity light's colour, the text's colour, and the `lines` you want.

Worth knowing:

- `-o` replaces whatever file it names, so point it at a new skin's folder,
  or leave it off and the lines appear on screen for you to copy.
- `-o` writes plain text the board reads. Sending the screen output to a
  file with `>` in Windows PowerShell writes a kind of text file the board
  refuses, so use `-o` there.
- Strip lamps are numbered the way you would read a panel: the top row first,
  left to right, then the next row down.
- The diameter is the dot's width or height, whichever is larger. Paint the
  dot the size of the lens, not the size of the glow.
- A clock can be placed the same way with `--key clock=#RRGGBB`: its dot marks
  the clock's top left corner.
- `--halo N` sets the same halo on every light it writes.
- If a colour is not found, the tool says so and leaves that light out.
  JPEG saving blurs colours, which is why the copy is a PNG.
- The tool finds the drive light, the activity light, the strip, the text
  rectangle and the clock. Lamps and the live widgets are placed by hand:
  read their positions off your paint program.

## The text and the clock

The words on the `lines` line choose what the rectangle shows, one per row,
top to bottom:

| Word | Shows | For example |
|---|---|---|
| `name` | The board's name | The Rusty Antenna |
| `address` | The address and port callers dial | 192.168.0.40:6400 |
| `uptime` | How long since the board started | up 3d 4h |
| `callers` | Callers on, out of the lines there are | Callers 2/11 |
| `today` | Calls since midnight | 14 calls today |
| `heap` | Free memory | 84K free |
| `card` | Free space on the SD card | card 29 GB free |
| `clock` | The time | 21:47 |
| `date` | The date | Sat 26 Sep |
| `last` | The most recent login, logoff, page or ring | 21:40 login alice |
| `ring` | Who is ringing for the sysop, empty when nobody is | alice is ringing |
| `blank` | An empty row, for spacing | |
| `who` | Who is on, one caller a row, in every row left | 1) alice 7m |

`who` fills the rest of the rectangle, so it goes last. Until the board has
the time from the internet, `clock` shows `--:--` and `date` shows nothing.

The rectangle has to be tall enough for its lines: 12 pixels a row for tiny
letters, 16 for small, 32 for big. Six small lines need a rectangle at least
96 pixels tall. A line wider than the rectangle is cut short at its edge;
small letters are 8 pixels wide, so a 224 pixel wide rectangle holds 28 of
them, or 37 tiny ones.

The separate `clock` line puts the time anywhere on the picture, on top of a
TV cabinet or in a panel's readout. It is 30 by 12 pixels in tiny letters, 40
by 16 in small ones and 80 by 32 in big ones.

The text rectangle is the quick way to put words on a screen. The live
widgets, next, are the way to put each figure exactly where the machine would
have shown it.

## Live widgets

A skin comes alive when the machine's own parts carry the board's figures. A
monitor that is a waiting-for-caller screen. A red seven-segment readout on a
tower's front, showing how many callers are on. A front panel whose lamps are
the caller lines. A tuning meter on a television that is really the Wi-Fi
signal. Seven kinds of widget do this, and each can be placed where you like,
as many times as its limit allows.

A board whose firmware is older than the widgets shows the `status` layout
instead of a skin that uses them, and `PANEL` gives the reason with its line:
`'field' is not a skin directive`, or whichever widget it met first.

### A small example

A green-screen terminal with a readout and a modem beside it. Everything on
the screen is live:

```
skin 1
panel 480 320
name Green terminal
; the screen: the board's name, then every line
field 40 30 240 name colour=#4CFF7A
nodes 40 50 240 132 size=tiny colour=#4CFF7A free=yes dim=#2A9A48
field 40 186 240 address size=tiny colour=#4CFF7A
; traffic over the last two minutes, at the foot of the screen
graph 40 206 240 60 traffic colour=#4CFF7A
; a readout on the cabinet: callers on
digits 330 40 28 2 online
; the Wi-Fi signal as a bar
meter 330 90 60 10 rssi
; the modem: MR AA CD RI RD SD
lamp 330 280 5 run halo=4
lamp 348 280 5 open halo=4
lamp 366 280 5 online halo=4
lamp 384 280 5 ring halo=4 blink=yes
lamp 402 280 5 rx halo=4
lamp 420 280 5 tx halo=4
```

Line by line:

- `field` writes the board's name across the top of the screen, in phosphor
  green.
- `nodes` lists every caller line in tiny letters, one to a row. Busy lines
  show who is on; `free=yes` lists the empty ones too, as `waiting`, and
  `dim=` draws those in a darker green so the busy lines stand out.
- The second `field` writes the address callers dial, under the list.
- `graph` draws the last two minutes of network traffic as a sweep.
- `digits` is a two-digit red readout of how many callers are on.
- `meter` is a bar that fills with the Wi-Fi signal's strength.
- The six `lamp` lines are a modem's front panel, explained under
  [the lamps](#lamp-a-light-that-means-something).

Put a `background.jpg` behind it with a terminal and a modem painted in the
right places, and `mkskin.py check` passes it as it stands.

### field: one line of words

A `field` writes one figure, one letter tall, wherever it belongs on the
picture: the name across a monitor's top edge, the address on a cabinet's
badge plate, the version on a label.

```
field 300 40 160 online label=On_line:_ colour=#FFD35C
```

That writes `On line: 3` from 300 pixels across and 40 down, in a box 160
pixels wide. The box is as tall as one letter of its size.

- **The value** is any word from the text table above except `who` and
  `blank`, or one of the six in the table below.
- **`label=`** puts fixed words in front of the figure, up to 12 characters.
  A space would end the option, so write `_` where you want one.
- It takes `size`, `colour`, `shadow`, `background` and `align`, the same as
  the text rectangle. Words that do not fit are cut at the box's edge.

The extra values a field can show:

| Word | Shows | For example |
|---|---|---|
| `online` | How many lines are busy, the way the directory counts them. A caller still at the login prompt counts, so this can be one or two more than the node list shows | 3 |
| `lines` | How many lines the board has, counted the same way | 11 |
| `lastcaller` | The handle of the most recent caller to log in | alice |
| `rssi` | The Wi-Fi signal strength | -58 dBm |
| `peak` | The most lines busy at once since the board started | 5 |
| `version` | The firmware version | 1.2.0 (MF35 1.1.0) |

### digits: a seven-segment readout

A red LED readout, the kind on a clock radio, a test meter or a PC case's
front. It shows a number.

```
digits 378 90 28 2 online colour=#FF3020 dim=#2A0604
```

That is a two-digit readout 28 pixels tall showing how many callers are on,
which is what the stock `pc` skin puts in its tower's window.

- The line is `digits X Y H N VALUE`: the top left corner, the height (10 to
  64 pixels), how many digits (1 to 6) and what to show.
- Each digit is a little over half as wide as it is tall, with a gap of a
  sixth of the height between digits. The readout above is 34 by 28 pixels.
- **The value** is one of the numbers: `online`, `lines`, `today`, `peak`,
  `heap` (in KB), `rssi` (without its minus sign, so -58 dBm shows `58`) or
  `clock`. The clock needs 4 digits and lights the colon between them.
- A number too big for the digits shows as all nines.
- **`colour=`** is the lit segments, red `#FF3020` if you leave it out.
  **`dim=`** draws the unlit segments faintly, the way a real readout shows
  its dark segments. Leave it out, or write `dim=none`, and the unlit
  segments are whatever your picture has there, so you can paint them in
  yourself.
- 24 pixels or taller reads from across the desk. That is the place for the
  one number you want to see from your chair.

### nodes: who is on each line

A list of the caller lines, one row a line, the sysop's line first. This is
the monitor that shows who is on.

```
nodes 36 78 240 80 colour=#C4BAFF
```

A row reads like `3> bob  FILES  1h`, in columns: the line number, a mark for
the caller's rank, the handle, the command they are running and how long they
have been on. The marks are the board's usual ones: `)` a caller, `*` a guest,
`>` a co-sysop and `]` the sysop, whose line is numbered `S`.

- The line is `nodes X Y W H`: a rectangle, at least 8 characters wide and
  one row tall. It shows as many rows as fit.
- **`free=yes`** lists every line, the empty ones as `waiting`. Leave it out
  and the list shows only the callers who are on.
- **`dim=#RRGGBB`** draws the waiting rows in a second, darker colour.
- **`tint=yes`** colours each row's line number and mark by rank, for a
  colour screen.
- More callers on than rows: the last row says how many it could not show,
  as ` +3 more`.
- On a narrow list the row drops the command first, then the time on.
- It takes `size`, `colour`, `shadow` and `background`.

### events: what happened lately

The board's most recent logins, guest calls, logoffs, pages and rings, one to
a row, as `21:40 login alice`. The board keeps the last ten. It makes a fine
printout on a teleprinter, or a feed on a television.

```
events 28 6 210 84 size=tiny colour=#2A2A30 order=oldest
```

- The line is `events X Y W H`, with the same minimum size as `nodes`.
- **`order=newest`**, as it is if you leave it out, puts the newest at the
  top. **`order=oldest`** puts the newest at the foot, so the list scrolls up
  like paper coming out of a printer.
- **`tint=yes`** colours the kind of event (`login`, `ring` and so on).
- It takes `size`, `colour`, `shadow` and `background`.

### meter: a bar

A bar that fills with a figure, with a bright leading edge: a signal meter, a
fuel gauge, a VU meter on a tape deck.

```
meter 254 160 12 34 rssi dir=up
```

That is a thin upright bar, 12 by 34 pixels, filling from the bottom with the
Wi-Fi signal. The stock `atari` skin uses it as its television's tuning
meter.

- The line is `meter X Y W H SOURCE`, at least 2 by 2 pixels.
- **`dir=right`**, as it is if you leave it out, fills from left to right.
  **`dir=up`** fills from the bottom.
- **`colour=`** is the bar, green `#5DDC7A` if you leave it out.
  **`background=#RRGGBB`** fills the empty part of the box; leave it out and
  your picture shows through.

| Source | The bar is full at | Empty at |
|---|---|---|
| `traffic` | 64 KB a second of network traffic, on a log scale, so a trickle still moves it | no traffic |
| `heap` | as much free memory as the board has had since it started | none free |
| `card` | an empty SD card | a full one |
| `callers` | every line busy | nobody on |
| `rssi` | a Wi-Fi signal of -50 dBm or stronger | -90 dBm or weaker |

### graph: the last two minutes

A sweep across a box, like an oscilloscope or a chart recorder, the newest at
the right. The board takes two samples a second and draws one pixel column
each, so a graph 240 pixels wide shows two minutes.

```
graph 40 206 240 60 traffic
```

- The line is `graph X Y W H SOURCE`, at least 2 by 2 pixels.
- **The source** is `traffic`, on the same scale as the meter, or `callers`.
- **`colour=`** and **`background=`** work as they do for the meter.
- The board keeps 240 samples. A graph wider than 240 pixels leaves its left
  part empty, so 240 is the widest worth drawing.

### lamp: a light that means something

A lamp is drawn the way the drive light is, as light glowing through a lens
you painted dark, but you choose what lights it. This is how a front panel's
lamps, a modem's lamps or a keyboard's lock lights come to mean something.

```
lamp 84 278 5 ring colour=#FF3020 halo=4 blink=yes
```

That is a 5 pixel red lens that blinks while somebody is ringing for the
sysop.

- The line is `lamp X Y D STATE`: the centre, the lens's diameter (2 to 64
  pixels) and the state that lights it.
- **`colour=`** is its light, red `#FF2A10` if you leave it out.
- **`halo=`** works as it does for the other lights.
- **`blink=yes`** flashes it twice a second while it is lit. Use it for the
  lamps that want somebody, such as a ring, so they stand out of a row of
  lamps the same colour.

What lights a lamp:

| State | Lit while |
|---|---|
| `node1` to `node16` | Somebody is on that caller line. It glows softly while they are on and flares to full brightness each time they press a key. The board has ten caller lines today, so `node1` to `node10` are the ones that light |
| `sysop` | The sysop is on the sysop's line, visible |
| `online` | A caller is on any caller line |
| `staff` | Staff are on, visible |
| `rx` | Bytes came in over the network |
| `tx` | Bytes went out over the network |
| `disk` | The card or the board's own memory was read or written: a short flash on each access, in the lamp's one colour |
| `error` | There was a storage error. It blinks by itself, half a second on and half off, for ten seconds |
| `card` | An SD card is in and working. Dark while the card has an error |
| `run` | Always, while the board is up. A power lamp |
| `closed` | The board is closed to callers, from `CONFIG board` |
| `open` | The board is taking calls |
| `ring` | A caller is ringing for the sysop |
| `mail` | The sysop has mail waiting that has not been read |
| `listed` | The board is listed in the directory |

### A modem's lamps

A modem's front panel is the best example of a device whose lamps already
meant something, and the stock skins put one on three desks. Each lamp keeps
its original meaning, as near as a BBS on Wi-Fi can have it:

| Lamp | On a real modem | The state | On the board |
|---|---|---|---|
| MR | Modem ready | `run` | The board is up |
| AA | Auto answer is on | `open` | The board is taking calls |
| CD | Carrier detect: a call is connected | `online` | Somebody is on |
| RI | Ring indicator | `ring`, with `blink=yes` | Somebody is ringing for the sysop |
| RD | Receive data | `rx` | Data coming in |
| SD | Send data | `tx` | Data going out |

The same thinking works for any machine you draw. A mainframe's WAIT lamp can
be `closed`, an INT lamp `ring`, a disk cabinet's READY lamp `card`. Look at
what the lamps on the real thing were for, then find the board fact nearest
to it.

### Every widget at a glance

| Line | Most in one skin | What it draws |
|---|---|---|
| `field X Y W VALUE` | 24 | One line of words bound to a figure |
| `digits X Y H N VALUE` | 4 | A seven-segment readout, 1 to 6 digits |
| `nodes X Y W H` | 2 | Who is on each line |
| `events X Y W H` | 2 | The last ten logins, logoffs, pages and rings |
| `meter X Y W H SOURCE` | 8 | A bar filling with a figure |
| `graph X Y W H SOURCE` | 4 | Two minutes of traffic or callers |
| `lamp X Y D STATE` | 32 | A lens lit by a state |

The options each takes:

| Line | Options |
|---|---|
| `field` | `label`, `size`, `colour`, `shadow`, `background`, `align` |
| `digits` | `colour`, `dim` |
| `nodes` | `size`, `colour`, `shadow`, `background`, `free`, `dim`, `tint` |
| `events` | `size`, `colour`, `shadow`, `background`, `order`, `tint` |
| `meter` | `colour`, `background`, `dir` |
| `graph` | `colour`, `background` |
| `lamp` | `colour`, `halo`, `blink` |

### The limits you will meet

The board keeps its callers first. It redraws only the widgets whose figures
changed, and it draws a bounded amount each time round, so no skin can make a
caller wait. That sets a few limits:

- **One widget may not draw more than 16,384 pixels at once.** A field, the
  clock, a readout, a meter or a graph counts its whole box. The text
  rectangle and the two lists count one row at a time, so they can be as
  tall as you like. A graph 240 by 68 fits. A graph the full 480 pixel width
  of the screen fits only if it is 34 pixels tall or less, and half of it
  would be empty anyway. The tallest six-digit readout, 64 pixels, is 260 by
  64 and misses by a little; five digits fit.
- **A graph shows 240 samples**, so pixels past 240 across stay empty.
- **A list draws 32 rows at most**, more than any list on these screens has
  room for.
- **How many of each**: 24 fields, 4 readouts, 2 node lists, 2 event lists,
  8 meters, 4 graphs and 32 lamps.
- **All the lights together**, the drive light, the activity light, the
  strip and the lamps, may cover 65,536 pixels of boxes. Thirty-two lamps of
  a normal size come nowhere near it: an 8 pixel lens with `halo=5` is an 18
  pixel square, 324 pixels.
- **The smallest sizes**: a field one character wide, a list 8 characters
  wide and one row tall, a meter or a graph 2 by 2 pixels, a readout 10
  pixels tall.

`mkskin.py check` reports every one of those that is a mistake before the
board ever sees the skin, with its line: too many of a kind, a widget too big
or too small, one that overlaps another or runs off the screen. For example:

```
my_tower:
  skin.txt line 12: graph 1 draws 19200 pixels at once; 16384 at most
```

A graph wider than 240 pixels and a list taller than 32 rows are not
mistakes, so `check` passes them. The preview shows you what they look like.

### What the glass shows about people

A member of staff who is hidden or lurking never appears in a list, never
shows in the events and never lights a lamp, the same way they stay out of
`WHO`. The node list shows the command each caller is running, which `WHO`
shows only to staff: the screen sits on the sysop's desk. It is the command's
name only, such as `CHAT` or `FILES`, never anything the caller typed.

## Check it and see it lit

Before it goes to the board, check the skin:

```
python tools/mkskin.py check my_tower
```

A good skin says `my_tower: ok`. A skin with a mistake says what and where,
with the same words the board would use:

```
my_tower:
  skin.txt line 9: led 2's box overlaps led 1's (line 8)
```

Then look at it:

```
python tools/mkskin.py preview my_tower -o my_tower.png
```

Open `my_tower.png`. It is your picture lit the way the board lights it, with
made-up figures in the screen's own font, so you can see whether the text
sits in the monitor and the glow sits on the lamp. The drive light shows
amber, as if the card were being read, and some of the strip lamps are lit
and some are not. The widgets are filled in as if a few callers were on: a
node list with three of them, a run of events, a traffic graph, bars part
full, readouts showing numbers, and the lamps for those callers lit.

## Sending it to the board

On a board with a display, the file areas have one called Skins. It is area
12 on the 3.5 inch board, and 14 on a board that also has a camera. Staff can
look in it and download from it; only the sysop can send to it, and what the
sysop sends is in at once, with no approval step.

A transfer carries one file, not a folder, so the Skins area takes a skin as
a pair of files with the skin's name: `my_tower.txt`, which is its `skin.txt`,
and `my_tower.jpg`, which is its picture. The tool makes the pair from a skin
folder, checking it first:

```
python tools/mkskin.py pair my_tower -o to_send
```

That leaves `my_tower.txt` and `my_tower.jpg` in a folder called `to_send`.
Then:

1. Call the board and log in as the sysop.
2. Type `FILES 12`, or `FILES 14` on a board with a camera. At the file area
   menu, `#12` and Enter does the same.
3. Press `U`. The board says it is ready; start a YMODEM upload in your
   terminal program and choose `my_tower.txt`.
4. Press `U` again and send `my_tower.jpg` the same way.
5. Type `CONFIG panel`. On the **Skin** row (**Panel skin** on a wide
   terminal), press Space to step through the choices to your skin, and save.

The board loads the skin in the background, so callers are never held up. The
`status` layout stays on the screen until the skin is ready.

Worth knowing:

- **Sending a file that is already there replaces it.** Send a new
  `my_tower.jpg` and the board reads the skin again, even if it is on the
  screen at that moment.
- **The board refuses any other name** as the transfer starts, before any of
  the file is kept, and says why: `Skins takes <name>.txt and <name>.jpg,
  name 1-24 of A-Z 0-9 _ -.` The name follows the same rule as a skin's
  folder.
- **`E` erases a file there**, by its number in the list. With either half
  gone, CONFIG stops offering that skin.
- **A folder beats a pair.** If the card has a folder `skins/my_tower/` as
  well as the pair, the board reads the folder. Give a skin you send a name
  of its own.

## Or copy it onto the card

If you would rather use a card reader, a skin can go on the card as a folder,
the same layout as the stock skins:

```
skins/
  my_tower/
    background.jpg
    skin.txt
```

1. Log in as the sysop and type `SD UNMOUNT`, so the card can come out safely.
2. Take the card out and copy your skin's folder into the card's `skins`
   folder. Make the `skins` folder at the top of the card if it is not there.
3. Put the card back and type `SD MOUNT`. The board may pause for a moment
   while it reads the card.
4. Choose it in `CONFIG panel` as above.

**Changing a skin that is already on the screen this way.** The board keeps a
loaded skin in memory and does not reread the card for it by itself. After
copying a new version onto the card, choose `status`, save, choose your skin
again and save. Restarting the board works too. Sending the new version
through the Skins area instead does all of that for you.

## Choosing a skin

The **Skin** row in `CONFIG panel` lists `status` and up to 16 skins from the
card, in name order. It lists only skins that have both their files, whose
`skin.txt` reads cleanly, and that are drawn for this screen's size. If yours
is missing from the list, run `check` on it.

## When it does not show

Type `PANEL`. Among what it prints, a line says which skin is on the screen,
with its title from the `name` line: `Skin my_tower (My tower)`. When it is
not the one you chose, that line says `Skin status` and a red line under it
says why:

```
Skin status
Not my_tower: skin.txt line 9: led 2's box overlaps led 1's (line 8)
```

The same reason goes to the board's console. The ones you are likely to meet:

| PANEL says | What to do |
|---|---|
| `Not my_tower: background.jpg is 640x480; the panel is 480x320` | Make the picture exactly the screen's size. `mkskin.py jpeg` does it |
| `Not my_tower: background.jpg: progressive JPEG: save it as baseline (not progressive)` | Save it again with the progressive option off, or use `mkskin.py jpeg` |
| `Not my_tower: background.jpg: greyscale: save it in colour (YCbCr)` | Save it as a colour JPEG, even if the picture is black and white |
| `Not my_tower: background.jpg: fill bytes between segments: save it again as baseline` | Rare. Save it again, or use `mkskin.py jpeg` |
| `Not my_tower: background.jpg: an empty segment the decoder refuses: save it again` | Rare. Save it again, or use `mkskin.py jpeg` |
| `Not my_tower: skin.txt line 9: led 2's box overlaps led 1's (line 8)` | Move the lamps apart or give them a smaller `halo` |
| `Not my_tower: skin.txt line 4: 'drvie' is not a skin directive` | A typo on that line |
| `Not my_tower: skin.txt line 7: 3 lines of 16 px need 48 px; the rectangle is 40` | Make the text rectangle taller, or show fewer lines |
| `Not my_tower: skin.txt line 1: a character that is not plain ASCII (byte 239)` | The editor saved the file as "UTF-8 with BOM". Save it as plain text, ASCII or UTF-8 without BOM |
| `Not my_tower: drawn for a 480x320 panel; this one is 172x320` | The skin is for another screen, or the screen has been turned |
| `Not my_tower: no skins/my_tower/ or skins/my_tower.txt on the card` | Neither the folder nor the pair is there, or the name differs by a letter |
| `Not my_tower: no background.jpg (or my_tower.jpg) beside its skin.txt` | The picture is missing: send `my_tower.jpg`, or copy `background.jpg` into the folder |
| `Not my_tower: no SD card` | The card is not in, or not mounted: `SD MOUNT` |
| `Not my_tower: loading` | Nothing is wrong. Give it a second |

The line numbers count every line of `skin.txt`, blank lines and comments
included, starting from 1, the way a text editor numbers them.

**After fixing a skin that failed,** the board tries it again when you send a
new file for it through the Skins area, when you save any page of `CONFIG`,
after `SD UNMOUNT` and `SD MOUNT`, or after a restart. It does not keep
retrying on its own, so that a broken skin is not reread over and over.

## Sharing a skin

When a skin looks right, the tool packs it for somebody else:

```
python tools/mkskin.py pack my_tower -o my_tower.zip
```

It checks the skin first and refuses to pack one with a mistake in it. The zip
holds `skins/my_tower/` with both files, so whoever receives it unzips it onto
the top of their card as it is, or runs `pair` on the folder and sends it
through their Skins area. Several skins go in one zip:
`python tools/mkskin.py pack my_tower my_terminal -o my_skins.zip`. A
`README.txt` in a skin's folder goes into the zip too, which is a good place
to say who drew it.

Two things before you share:

- **Use art you made or have the rights to.** A photo from a web search
  belongs to whoever took it.
- **Leave makers' logos and names off the machine.** A drawing of a beige
  tower is yours to give away. A company's badge on it is not.

## When the board puts the stock skins on the card

A later release puts the five stock skins on the card by itself and keeps them
current when new versions ship. It never overwrites one you have changed: once
you change any file in a stock skin's folder, the board treats the whole
folder as yours and leaves it alone. A stock folder you delete comes back, so
make your own skins under names of their own rather than editing `pc` or
`c64` in place.
