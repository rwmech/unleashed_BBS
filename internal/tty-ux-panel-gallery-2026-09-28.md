# The Photos viewer on the panel: swipe, auto-show and slideshow (1.2.0)

Design spec for Rob's 1.2.0 feature: "swiping left or right to show the last
pictures from the camera or sat camera. Most boards with displays can do
this." Plus, the same day: "an option to make that the default screen when
pictures are taken, or 'show new photos on the display for x seconds before
returning to status page'".

**Revision 1 (2026-09-28), after Rob read revision 0.** What changed:

- No ring and no slots. The viewer reads the Photos folders on the card
  directly, one directory pass a step on the runner, and loads the next or
  previous file. `photos/.recent` and the 20-entry ring are gone.
- Photos is its own system, not a caption laid over the status panel: its
  own ground, accent, title, frame, progress strip and controls, entered
  from the status panel with a wipe and left back to it. Every board's
  layout is redrawn for that.
- A progress indicator while a file is found and decoded: the strip under
  the title, with the title saying `finding` then `loading 62%`.
- The auto-show's settings (Show new, and which kinds) move to the camera
  side, CONFIG cameras. CONFIG panel keeps Photos on the panel and the
  slideshow interval, both on its main page. The Photos sub-page is gone.
- The name clash gets the simplest fix there is: the next free second.
- Everything else stands as recommended in revision 0.

Read against:

- `internal/tty-ux-panel-2026-09-24.md` (the 1.47, revisions 0 to 2);
- `release-prep/wt-ws43b/internal/tty-ux-panel-ws43b-2026-09-28.md` (4.3B,
  with revision 1), and its `src/plugins/panel.cpp`, `src/board.h`,
  `src/platform/platform_esp32_rgb.cpp` (the GT911 poll);
- `release-prep/ws2/tty-ux-panel-ws2-2026-09-28.md` (the 2-inch, revisions
  1 and 2), and `release-prep/wt-ws2/src/plugins/panel.cpp`, `src/board.h`;
- `release-prep/wt-mf35/internal/tty-ux-panel-mf35-2026-09-26.md`,
  `wt-mf35/src/board.h` (both MF35 revisions), `wt-mf35/SKINS.md` (the JPEG
  path);
- `release-prep/wt-skins/internal/tty-ux-skin-widgets-2026-09-26.md`;
- the link branch, `release-prep/wt-link` at 3b43496: `src/core/photos.h`,
  `src/plugins/camera_rules.h`, `src/plugins/camera.cpp` (its settings
  table), `src/core/bbs_sysop.cpp` (CONFIG cameras and CONFIG sats);
- camsat's BBS side, `release-prep/wt-camsat-mb/bbs/camsat.cpp`;
- the main tree's CONFIG machinery, `src/core/plugin.h` and
  `src/core/bbs_sysop.cpp` (`pageOwner`, `kCoreRows`, `Form::kMaxFields`).

Nothing was built, run or put on a glass. The boxes were laid out by
`viewer.py` in the session scratchpad from the numbers in this report, one
character to 8 px, and every box was checked to sit inside its glass. One
read was refused by the machine's guard: the ROM TJpgDec header under
`.platformio/packages` (not a dev directory). The claim that depends on it,
that the S3 ROM decoder scales, rests on Espressif's `esp_jpeg` README
instead, and is marked as a check for the builder.

## The verdict

Buildable, and simpler than revision 0: the card is the gallery, so there
is nothing to keep in step with it. Two gaps have to close first. A photo
on the card does not say which camera took it, so the caption Rob asked for
needs one sentence added to the JPEG comment every photo already carries.
And no board reads a touch position yet, so a swipe cannot be seen anywhere
today. CONFIG panel is full on three boards and needs the Glass sub-page
from revision 0. The built-in camera's own CONFIG page is full, and missing
on three of the four display boards, so the auto-show's rows belong on
CONFIG cameras, the page every board with a camera or a satellite has. The
viewer is a small program of its own: a slate mat instead of the status
panel's black, `PHOTOS` in violet where the status panel has its blue bar,
the picture in a keyline frame, a strip that shows finding, loading and the
timer, and home and play plates in the corners. The frame costs the photo
a quarter of its area on the 4.3B against revision 0, 248 x 186 instead of
288 x 216, and that is the price of it reading as its own place. The 1.47
has no touch and its BOOT button is spoken for three times over, so it gets
the auto-show and not the browsing.

## Rob's decisions, built in (2026-09-28)

- The sysop chooses which kinds take the glass: snaps, motion, timelapse.
  Three yes/no rows, since CONFIG has no multi-choice kind. They live on
  CONFIG cameras, with Show new.
- Show new: 10 s by default.
- Bursts, from one camera or several callers: always the latest.
- A tap or swipe during the auto-show continues through the history.
- One on-screen button switches viewer (manual) and slideshow (automatic);
  the slideshow's interval is a setting on CONFIG panel.
- Silent mode and silent hours are honoured: an auto-show never lights a
  blanked panel. A tap wakes it through silent for a set time, then silent
  resumes.
- "Photos on the panel", yes or no, on CONFIG panel: no means no picture
  ever shows. Default yes.
- No slots and no ring: the viewer reads the card.
- Photos is its own system, entered from the status panel and left back
  to it.
- The name clash: any simple fix, in the link lane.
- Stands from revision 0: the defaults, silent hours, no jump while
  browsing, the snapper's handle, the 1.47 on auto-show only, and
  `PluginSetting::page` with the Glass sub-page.

## Findings, worst first

### 1. A photo does not say which camera took it, and two cameras can collide

- `camera_rules.h`: names are `SNAP-20260924-171204.JPG`,
  `timelapse/TL-...JPG`, `motion/MO-...JPG` (and the handle schemes). No
  camera number, no camera name.
- The JPEG comment says the board, the time and who: `camera.cpp:1028` and
  `camsat.cpp:421` write the same text. No camera.
- So "camera number and name" in the caption has no source today.

Fix, the smallest that works with no ring: the comment gains one sentence,
`Camera 2 garden.`, after `snapped by <who>.` (or `taken by the board,
<folder>.`). The camera writes its own number and name; camsat puts the
satellite's into the comment text it already sends in `snapMsg`. The viewer
reads the comment on the runner before the decode: `ComSink` writes it as
the first segment after SOI, so it costs no extra read. A photo taken
before 1.2.0 has no camera sentence and its caption shows the kind word
instead (below). Hand-back to the camera and link lanes.

**The bug, found in revision 0.** Both cameras name by the second. Two
cameras snapping in the same second both ask for
`SNAP-20260928-143200.JPG`; `photos::file` refuses a taken name
(`photos.h`, "False when rel is taken"), and the second photo is lost. The
same for two motion satellites firing together.

The simple fix Rob asked for, the link lane's: **on a taken name, file it
under the next second's stamp, trying up to five.** The name keeps the
camera's own shape, so `nameKey`, retention and the viewer's ordering need
no change at all. The cost is a stamp up to a few seconds late on the rare
photo that collided, which nobody will notice.

### 2. CONFIG panel is full on three of four boards, and CONFIG camera is no home for the auto-show

`kCoreRows` is 4 and `Form::kMaxFields` is 16, so a plugin page holds 12
of its own rows. Counted from the source:

| Board | Rows on CONFIG panel today | Free |
|---|---|---|
| 1.47 (ST7789) | Driver, Pins, Width, Height, X offset, Y offset, USB plug, Invert, Mirror, Colours, SPI MHz, Bright % | 0 |
| WS2 (ST7789, touch) | Sleep min (in Driver's row), Pins, Width, Height, X offset, Y offset, USB plug, Invert, Mirror, Colours, SPI MHz, Bright % | 0 |
| MF35 (i80) | Skin, Pins, Width, Height, X offset, Y offset, USB plug, Invert, Mirror, Colours, bus clock, Bright % | 0, and a touch driver wants Sleep min too |
| 4.3B (RGB) | Driver, Light, Sleep min | 9 |

Fix, as revision 0 and as Rob approved: one appended member on
`PluginSetting`, `const char* page`, naming the `PS_PAGE` row a setting is
listed under whatever its key; `pageOwner` checks it before the prefix.
About ten lines of core, no key renamed, no file migrated. A Glass sub-page
then takes the rows a sysop sets once, and the main page has room for
Photos and Slide s.

**CONFIG camera** (the built-in camera plugin, `camera.cpp` on the link
branch) has twelve rows on its main page: Snap, Photos, Size, Quality,
Names, Watermark, Keep days, Max snaps, Floor MB, and the Flash, Timelapse
and Picture buttons. It is full. And it exists only where the camera plugin
runs: the WS2, the Freenove and the ESP32-CAM. The 4.3B, the 1.47 and the
MF35 have cameras only as satellites, so they have no CONFIG camera at all,
and the auto-show would have nowhere to be set on the three boards most
likely to use it with a satellite.

**CONFIG cameras** (the core page on the link branch, `kCameras`) is the
board-wide camera page: "which camera SNAPSHOT uses", one row today,
reached from CONFIG sats' "Default camera". It exists on every board with a
camera or a satellite, and it is about all of them, which is exactly what
the auto-show is about. The auto-show's four rows go there. Rob's words were
"CONFIG camera"; this is the camera side of CONFIG, one letter over, and the
decisions list asks him to confirm it.

### 3. No board reads a touch position

- 4.3B: `touchPoll(bool& down)` reads the GT911's status byte (0x814E) and
  nothing else.
- WS2: taps from the INT line only, by design, and no I2C at all.
- MF35: an FT6236 on I2C 38/39, INT 40, in both revisions' schematics, and
  no driver. `BBS_HAS_TOUCH` is not set.

A swipe is a direction and a distance, so all three need a position. Fix:
`plat::touchPoll(bool& down, int16_t& x, int16_t& y)`, in the panel's drawn
pixels and its current orientation, and one recogniser in `panel.cpp` for
all three.

| Controller | Read | Bytes | Layout |
|---|---|---|---|
| GT911 (4.3B, 0x5D) | 0x814E..0x8153 | 6 | status (bit 7 ready, low nibble count), track id, X lo, X hi, Y lo, Y hi; physical 800 x 480, halve for the drawn 400 x 240 |
| CST816D (WS2, 0x15) | 0x02..0x06 | 5 | finger count, X hi (low 4 bits), X lo, Y hi, Y lo |
| FT6236 (MF35, 0x38) | 0x02..0x06 | 5 | the CST816D's shape: TD_STATUS, P1_XH (event in bits 7:6), P1_XL, P1_YH, P1_YL |

The CST816D's own gesture codes (register 0x01) are not used: two chips
with two sets of fixed thresholds would make one gesture feel different on
two boards, and the GT911 has none in normal mode.

### 4. The 4.3B draws at half resolution

The RGB driver fills its bounce buffers from a 400 x 240 picture, every
pixel doubled, so the refill interrupt on core 1 reads 192 KB of PSRAM a
frame instead of 768 KB. The photo is 248 x 186 drawn, 496 x 372 on the
glass: about 108 ppi, a desktop monitor's density, clean with a proper
area-average. A native photo layer would roughly triple the interrupt's
PSRAM traffic, from about 7.5 MB/s to about 22 MB/s, on the loop's core.
That is SYS's question to answer, not taste's. Ship at the drawn
resolution.

### 5. The runner is shared, and a step is a directory pass and a decode

A step reads the set's folders (see "What it shows") and then decodes the
file: estimated 20 to 200 ms for the directory reads and 0.3 to 1 s for the
decode, all on the runner, where every caller's FILES page and forum walk
waits behind it.

- A step a person asked for (a swipe, a tap, an auto-show) posts at once.
- A slideshow step posts only when the runner is quiet (nothing queued,
  nothing running): hand-back `runner::quiet()`. A slideshow can wait a
  second; a caller's listing should not.
- No prefetch: one photo buffer, one job at a time. If swipes feel slow on
  the bench, a prefetch of the neighbour in the direction of travel is the
  next step, and it is a performance change, not a design one.
- Measured on the bench: SYS's slow passes and loop average do not move
  with a slideshow at 5 s and two callers listing files.

Nothing of either is on the loop.

### 6. The 1.47 has no touch, and its BOOT button is taken

GPIO0 on the 1.47 already means three things: ROM download mode if held at
reset, the BOOT-hold password and factory reset if held within 10 s of
start, and the backup window when pressed with the sysop logged in. A
fourth meaning makes every press ambiguous, and the likeliest wrong
outcome, the backup window, puts the Wi-Fi password on the network. The
1.47 gets the auto-show, which needs no input.

### 7. The status panels have no room for a photos button, and need none

Every status layout is specified to the pixel and built. Entry is a swipe,
which needs no pixels, and the auto-show teaches it: the first time a new
photo takes the glass, the viewer is already open. The 4.3B review's rule
still holds and is what makes the viewer's targets acceptable: a touch may
change what the glass shows, never what the board does.

## Which boards get what

| Board | Touch | Browsing | Auto-show | Photo, drawn | On the glass |
|---|---|---|---|---|---|
| 4.3B, 400 x 240 drawn | GT911 | yes | yes | 248 x 186 | 58.0 x 43.5 mm |
| MF35 landscape, 480 x 320 | FT6236, driver owed | yes, once the driver lands | yes | 354 x 266 | 54.2 x 40.7 mm |
| WS2 portrait, 240 x 320 | CST816D, positions owed | yes | yes | 230 x 172 | 29.2 x 21.8 mm |
| 1.47 portrait, 172 x 320 | none | no | yes | 162 x 121 | 16.7 x 12.5 mm |

Revision 0's photos, for the trade: 288 x 216, 394 x 296, 240 x 180 and
172 x 129. The frame, the keyline and the side margins cost 26% of the area
on the 4.3B, 19% on the MF35, 8% on the WS2 and 12% on the 1.47.

## Photos, its own system

What makes the viewer read as a different place, and not the status panel
with a picture pasted on:

- **Its own ground: the mat.** The whole glass in `kRule`, #2c2c38 (RGB565
  0x2967), a dark slate. The status panel is black under a blue bar; the
  viewer is a photo mounted on grey board. Nobody mistakes one for the
  other across a room.
- **Its own accent: violet.** `PHOTOS` in `kName`, #b48ef0 (0xB47E), the
  site's `--name`, which on the panel appears only inside the wordmark.
  Violet is Photos' colour and nothing else's on the glass, so it means
  "you are in Photos" wherever it shows: the title, the slideshow's timer,
  the pause icon, a pressed plate.
- **Its own title.** `PHOTOS` (or `TIMELAPSE`, the other set) at the top
  left, the way a program names itself, with what it is doing after it
  (`NEW`, `slideshow`) and where you are at the right (`3 of 214`,
  `finding`, `loading 62%`).
- **A frame.** The picture sits in a 1 px `kBg` black keyline on the mat, a
  print in a mount, letterboxed to its own shape, never cropped.
- **Its own progress strip**, 4 px under the title: black track, the fill
  `kDial` while a file loads, `kName` while a timer runs.
- **Its own controls.** Home and play plates in the bottom corners, black
  with a `kDim` edge on the mat; chevrons in the side margins.
- **An arrival and a departure.** Entering wipes the glass to the mat top
  to bottom (a full redraw, a band a tick: 0.2 s on the 4.3B, 0.8 s on the
  MF35), title first, then the frame, then the picture. Leaving redraws the
  status panel (or the skin) top to bottom, the path silent's end takes
  today. Nothing of the status panel shows while Photos is up: no bar, no
  band, no dot, no lamps.

Colours on the mat, WCAG contrast on the site's values against #2c2c38:
`kInk` 8.2:1, `kYellow` 9.7:1, `kDial` 8.4:1, `kWarm` 6.5:1, `kName`
5.3:1. `kDim` is 4.0:1, so it is used for the plates' edges, the chevrons
and one short hint, never for a figure; `kFaint` is not used on the mat at
all. The black keyline is 1.4:1 against the mat, which is what a mount's
edge is: found, not seen, and the picture inside it is what carries.

## What it shows: the card, directly

- **Two sets.** *Photos*: the Photos folder itself (`SNAP-...`), each of its
  sub-folders other than `timelapse` and `motion` (the "by handle" names,
  `SNAP-...` only), and `motion/` (`MO-...`). *Timelapse*: `timelapse/`
  (`TL-...`). Snaps and motion shots are one set because they are both
  events; the timelapse is its own because at one frame a minute it would
  bury every event under a day of sky.
- **Only names the camera writes.** `nameKey` decides, as retention does,
  so a sysop's own `garden.jpg` in Photos is not in the viewer.
- **Order**: the 14-digit stamp in the name, newest first; a tie (two
  cameras, one second, before the link lane's fix) by the whole path.
- **A step, on the runner**, given the set, the current photo's stamp and
  path, and a direction:
  - for each folder of the set, one `plat::sdList` pass (FatFs's own read
    of the folder, one open, no stat per entry);
  - for each name the camera wrote, compare its stamp with the current:
    keep the nearest newer and the nearest older, count them all, count the
    newer;
  - answer: the neighbour's path and size, its position (newer count plus
    one) and the total.
  - No list is kept and nothing is sorted: the memory is one candidate and
    two counters, whatever the folder holds.
- **Entering** is the same pass with the current stamp taken as the future:
  the answer is the newest.
- **What it costs**: FAT with long names keeps about 128 bytes of directory
  a file. 200 photos is 25 KB, read in about 17 ms over SPI; 2,000 is 256 KB,
  about 170 ms. A by-handle board reads one more folder per handle folder a
  step: thirty handles is thirty-two reads. If the bench finds that slow,
  the fix is to keep the list of folders (not files) between steps.
- **The ends.**
  - Newer from position 1: back to the status panel.
  - Older at the last: nothing moves, and the position flashes `kWarm` for
    300 ms (`214 of 214`). A swipe past the end is answered, not ignored.
  - The slideshow wraps from the oldest to the newest.
- **"3 of 214"** is free: the pass counted it.
- **A photo filed while somebody browses** is found by the next step, and
  the total grows. The title shows a `NEW` chip from the moment it is filed
  (below) until the person steps back to position 1.
- **A photo deleted while it is shown** (retention, ERASE, a laptop) still
  has neighbours: a step looks for stamps around the current one, never for
  the file itself.
- **No card**: no viewer. A swipe at the status panel does nothing.
- **No camera and no satellite on the board**: no auto-show; the viewer
  still opens if the card holds photos from before.

What the auto-show needs from the cameras is one event, not a list:
`photos::lastFiled()`, the photo just filed (path, camera number and name,
kind, handle, and whether the snapper was hidden or lurking) and a serial
that moves with each. The camera and camsat set it from `finish()`, on the
loop, once the runner has said the photo is on the card. About 100 bytes
of RAM. It is the one place the viewer learns anything the card cannot tell
it: that a photo is new, and whether its snapper is hiding right now.

## Positions and gestures

Position 0 is the status panel (or the skin on the MF35). Positions 1 to N
are the set's photos, newest first. Left is newer, right is older, as a
phone's photo roll runs.

```
   [status]  <->  [1 newest]  <->  [2]  <->  ...  <->  [N oldest]
   swipe left moves right along this line; swipe right moves left
```

| Gesture | At the status | In Photos |
|---|---|---|
| swipe left (finger right to left) | open Photos at the newest | one older; at N, the end flash |
| swipe right | nothing | one newer; from 1, back to the status |
| tap, right half | the header's next page (as now) | one older |
| tap, left half | the header's next page (as now) | one newer; from 1, back to the status |
| swipe up or down | nothing | the other set (Photos and Timelapse), at its newest |
| home plate | | back to the status |
| play plate | | viewer to slideshow, or back |
| long press | nothing (kept for the 4.3B's lights-out idea) | nothing |

The halves are the glass split at `w / 2`, less the two plates' targets. A
swipe up or down with no timelapse on the card does nothing.

**What a tap and a swipe are**, one recogniser, in drawn pixels:

- Down point, last point, and the largest distance from the down point.
- **Tap:** lifted within 400 ms, never more than the slop from the down
  point.
- **Swipe:** lifted within 800 ms, the long axis at least the swipe length
  and at least twice the short axis (within 27 degrees).
- **Long press:** held over 800 ms within the slop.
- Anything else is nothing: a slow drag, a diagonal, a palm.
- Judged on lift. A tap therefore acts on lift, not on the first contact as
  the 4.3B does today: about 100 ms later, the price of telling a tap from
  the start of a swipe. A wake acts on contact, and the rest of that touch
  is ignored until lift.
- A touch that starts on a plate and moves past the slop is a swipe. Plates
  act on lift within the slop.

Thresholds: slop 3 mm, swipe `min(12 mm, 30% of the glass's width)`, from a
new board define `BBS_LCD_UM_PER_PX` (micrometres a drawn pixel: 234, 153,
127, 103). Vertical swipes use the glass's height the same way.

| Board | mm a drawn px | Glass width | Slop | Swipe |
|---|---|---|---|---|
| 4.3B | 0.234 | 93.6 mm | 13 px | 51 px (12.0 mm) |
| MF35 landscape | 0.153 | 73.4 mm | 20 px | 78 px (12.0 mm) |
| MF35 portrait | 0.153 | 49.0 mm | 20 px | 78 px (12.0 mm) |
| WS2 portrait | 0.127 | 30.5 mm | 24 px | 72 px (9.1 mm) |
| WS2 landscape | 0.127 | 40.6 mm | 24 px | 94 px (12.0 mm) |

A tap drifts 1 to 2 mm, a deliberate thumb swipe runs 15 to 25 mm, and
12 mm sits well clear of both. The 30% cap is for the WS2's 30 mm glass.

**Sampling**, on the loop:

- GT911: every 50 ms while nothing is down, as now, and every 20 ms while a
  finger is. One 6-byte read, about 250 us at 400 kHz. The first contact
  can be seen up to 50 ms late: a 20 mm flick in 150 ms still measures
  13 mm.
- CST816D and FT6236: read on the INT flag the ISR already sets, at most
  every 20 ms, 5 bytes, about 150 us. Nothing is read while nothing is
  touched.
- A finger seen down with no report for 300 ms is taken as lifted.

## Modes, and what moves them

- **STATUS**: the status panel or the skin.
- **AUTO**: a new photo showing itself for Show new's seconds.
- **VIEW**: somebody is browsing; nothing moves by itself.
- **SLIDE**: the slideshow; a step every Slide s seconds.

| Event | STATUS | AUTO | VIEW | SLIDE |
|---|---|---|---|---|
| a new photo of a kind that takes the glass | AUTO, that photo | AUTO, the newer photo; the timer restarts | stays; `NEW` in the title | jumps to it and carries on from it |
| a new photo of a kind that does not | nothing | nothing | `NEW` in the title | nothing; the next step finds it |
| swipe or tap, older | VIEW at 1 (swipe only) | VIEW, one older | one older | one older; the timer restarts |
| swipe or tap, newer | nothing | STATUS | one newer, or STATUS from 1 | one newer, or STATUS from 1 |
| swipe up or down | nothing | VIEW, the other set | the other set | the other set, still SLIDE |
| home plate | | STATUS | STATUS | STATUS |
| play plate | | SLIDE from here | SLIDE from here | VIEW here |
| the timer runs out | | STATUS | | one older; after N, the newest |
| 60 s with no touch | | | STATUS | nothing: a slideshow runs until stopped |
| a ring | the ring shows | STATUS, ring | STATUS, ring | STATUS, ring |
| a CONFIG save | STATUS | STATUS | STATUS | STATUS |

- **AUTO's timer starts when the photo is whole on the glass**, not when it
  was filed, so a slow find or decode never eats the show.
- **Show new = stay**: AUTO with no timer. A touch makes it VIEW, with
  VIEW's 60 s.
- **Bursts, Rob's rule**: the newest wins. A photo filed while another is
  loading cancels that load (the job's cancel flag) and loads its own. Five
  callers snapping in ten seconds is five loads of the newest, each held
  its full time once whole.
- **An auto-show opens its photo's set**: a timelapse frame opens
  Timelapse, a snap or a motion shot opens Photos, so a swipe from it moves
  through its own kind.
- **VIEW is not interrupted by a new photo** (Rob, standing). The `NEW`
  chip is the notice; the total grows at the next step.
- **VIEW's 60 s**: long enough to look and call somebody over, short enough
  that a panel left in Photos is the status panel again before anybody
  wonders whether the board is up.
- **A ring beats everything.** The ring lives in the status header, so any
  Photos state ends at the ring, and Photos does not come back after it.

### Dark: silent, silent hours and sleep

- **Silent** (the switch or the hours): the backlight is off and nothing is
  drawn, as now. A new photo does not light the glass. It is on the card
  when anybody looks.
- **A touch lights a silent panel for 60 s.** Touch is polled while silent,
  which it is not today. The first touch only wakes, on contact, and shows
  the status panel. Every touch after restarts the 60 s, and everything
  works meanwhile, Photos and the auto-show included. When the 60 s run out
  Photos closes and the glass goes dark again. The lights, the strip and
  the activity LED stay dark throughout: the override is the glass, not
  "silent off". A ring does not light a silent panel, as today.
- **Sleep** (Sleep min): a new photo of a kind that takes the glass wakes
  the panel for its show, and the sleep clock restarts from the show's end.
  Sleep means "dark while nothing happens"; silent means "dark".
- The 1.47 has no touch, so its silent hours cannot be overridden from the
  desk.

## The layout rule, every board

- **Title** `R(0,0,w,20)`, text at y 2: `PHOTOS` or `TIMELAPSE` in `kName`
  from x 4, the mode word after two spaces, and the position or the load
  state right-aligned to `w - 4`.
- **Strip** `R(0,20,w,4)`: black track; a `kDial` fill left to right for a
  load; a 24 px `kDial` segment sweeping while a folder is read; in AUTO and
  SLIDE a `kName` fill of `w x remaining / total`, shrinking to the right,
  redrawn every 250 ms (one 4-row rect, under a band).
- **Landscape**: caption `R(0,h-20,w,20)` at the foot, text at `h - 18`;
  the photo area from y 28 to `h - 24`, less 16 px margins at the sides;
  the picture letterboxed in it with its 1 px keyline, centred. The side
  margins that are left take the plates and chevrons.
- **Portrait**: the picture spans the width less 4 px margins, then two
  caption rows under it (camera and who; when), the block centred between
  the strip and the dock; on a touch board the dock is the foot's
  `plate + 16` px, plates 8 px in.
- **Plates**: square, at least 8 mm on the glass, black `kBg` with a 1 px
  `kDim` edge, icon `kInk` (pause in `kName` during SLIDE); pressed, 150 ms
  of `kName` fill with the icon in `kBg`. They sit in the side margins when
  a margin is at least `plate + 8` wide, else over the picture's corners.
  Target: the plate and 8 px round it.
- **Chevrons**: 8 x 13, `kDim`, centred in each side margin at least 24 px
  wide, level with the picture's middle. Left always (newer, or home);
  right while there is an older photo. Hints, not buttons: the halves are
  the targets.

## The caption

| Field | Colour | Example | Rule |
|---|---|---|---|
| kind icon | snap `kDial`, motion `kWarm`, timelapse `kInk` | | always |
| camera number | `kDial`: it is what SNAPSHOT takes | `2` | when the comment has it |
| camera name | `kInk` | `garden` | cut to fit, 16 at most |
| who | `kInk`, two spaces after | `quantumrob` | snaps only; only if 6 or more glyphs fit |
| when | `kYellow`, as the status clock: yellow is for a real time | `today 14:32`, `27 Sep 14:32` | from the name's stamp; never cut |

- Landscape: one row, when right-aligned. Portrait: camera and who on the
  first row, when on the second.
- Shrink order, stop when it fits: who cut to 6, who dropped, name cut. The
  number and when are never cut. A cut is hard, no ellipsis.
- A photo with no camera sentence in its comment (taken before 1.2.0): the
  kind word in `kInk` in place of number and name: `snap`, `motion`,
  `timelapse`.
- **Who** comes from the comment's `snapped by <who>`. A photo on the card
  shows its snapper: `FILES.BBS` already says `Taken by <handle>` to every
  caller who can see Photos, so the sysop's glass tells nobody anything new.
  The one exception is the auto-show of a photo just taken by a caller who
  is hidden or lurking right now (`lastFiled()` says so): no handle,
  because the glass would be announcing that the caller is on.
- Glyphs a row: 4.3B 49, MF35 landscape 59, WS2 portrait 29, 1.47 portrait
  21.

## Mock-ups

One character is 8 drawn pixels across; rows are sampled every 12 or 16 px,
plus each box's first row. `.` the mat, `+` the keyline, `:` the picture,
`~` the strip's track, `=` a `kName` timer fill, `#` a `kDial` load fill,
`[ H ]` home, `[ > ]` play, `[ = ]` pause, `<` `>` chevrons. Icons: `@@`
camera (snap), `MM` motion, `TT` timelapse. The 8 px grid rounds boxes out
to whole characters: on the MF35 the home plate ends at x 56 and the
picture's keyline starts at x 62, though both land in column 7.

### Now: the 4.3B's status panel, where Photos is entered

From the 4.3B spec, "AFTER, four on". Nothing on it changes.

```
col   0         1         2         3         4
      01234567890123456789012345678901234567890123456789
    0 ##################################################  bar 0..33
    1 T h e   R u s t y   A n t e n n a ################
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
  198 --------------------------------------------------
  202 ## 118K          @@ peak 4       ## 41C
  220 dd   ==============================================
             <-------- swipe left anywhere: Photos, at the newest
```

### 4.3B, Photos (VIEW)

```
col (8 px)   0         1         2         3         4
             01234567890123456789012345678901234567890123456789
  y   2      PHOTOS...................................3 of 214.   title on the mat
  y  20      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   strip, 4 px
  y  28      .........++++++++++++++++++++++++++++++++.........   keyline 250x188 at x 75, y 28; photo 248x186
  y  40      .........+::::::::::::::::::::::::::::::+.........
  y  52      .........+::::::::::::::::::::::::::::::+.........
  y  64      .........+::::::::::::::::::::::::::::::+.........
  y  76      .........+::::::::::::::::::::::::::::::+.........
  y  88      .........+::::::::::::::::::::::::::::::+.........
  y 100      .........+::::::::::::::::::::::::::::::+.........
  y 112      .........+::::::::::::::::::::::::::::::+.........
  y 124      ....<....+::::::::::::::::::::::::::::::+...>.....
  y 136      .........+::::::::::::::::::::::::::::::+.........
  y 148      .........+::::::::::::::::::::::::::::::+.........
  y 160      .........+::::::::::::::::::::::::::::::+.........
  y 172      ..[ H  ].+::::::::::::::::::::::::::::::+.[ >  ]..   plates 40x40 (9.4 mm) at y 172..211
  y 184      ..[ H  ].+::::::::::::::::::::::::::::::+.[ >  ]..
  y 196      ..[ H  ].+::::::::::::::::::::::::::::::+.[ >  ]..
  y 208      ..[ H  ].+::::::::::::::::::::::::::::::+.[ >  ]..
  y 222      @@ 2 garden  quantumrob...............today 14:32.   caption: camera, who | when
  y 232      ..................................................
```

### 4.3B, a motion shot showing itself (AUTO)

```
col (8 px)   0         1         2         3         4
             01234567890123456789012345678901234567890123456789
  y   2      PHOTOS  NEW..............................1 of 215.   title on the mat
  y  20      ==============================~~~~~~~~~~~~~~~~~~~~   strip: the timer, 6 s of 10 left (kName)
  y  28      .........++++++++++++++++++++++++++++++++.........   keyline 250x188 at x 75, y 28; photo 248x186
  y  40      .........+::::::::::::::::::::::::::::::+.........
  y  52      .........+::::::::::::::::::::::::::::::+.........
  y  64      .........+::::::::::::::::::::::::::::::+.........
  y  76      .........+::::::::::::::::::::::::::::::+.........
  y  88      .........+::::::::::::::::::::::::::::::+.........
  y 100      .........+::::::::::::::::::::::::::::::+.........
  y 112      .........+::::::::::::::::::::::::::::::+.........
  y 124      ....<....+::::::::::::::::::::::::::::::+...>.....
  y 136      .........+::::::::::::::::::::::::::::::+.........
  y 148      .........+::::::::::::::::::::::::::::::+.........
  y 160      .........+::::::::::::::::::::::::::::::+.........
  y 172      ..[ H  ].+::::::::::::::::::::::::::::::+.[ >  ]..   plates 40x40 (9.4 mm) at y 172..211
  y 184      ..[ H  ].+::::::::::::::::::::::::::::::+.[ >  ]..
  y 196      ..[ H  ].+::::::::::::::::::::::::::::::+.[ >  ]..
  y 208      ..[ H  ].+::::::::::::::::::::::::::::::+.[ >  ]..
  y 222      MM 3 porch............................today 03:12.   caption: camera, who | when
  y 232      ..................................................
```

In SLIDE the title reads `PHOTOS  slideshow`, the `kName` timer runs to
the next step, and the play plate shows pause, `[ = ]`.

### 4.3B, loading the next photo

The keyline is drawn at the new photo's size at once, the inside black, the
title says where it is, and the strip fills by bytes decoded. The picture's
rows appear as the decoder hands them over (see "Finding, loading and the
other states"); the mock shows the moment before the first rows land.

```
col (8 px)   0         1         2         3         4
             01234567890123456789012345678901234567890123456789
  y   2      PHOTOS................................loading 62%.   title on the mat
  y  20      ###############################~~~~~~~~~~~~~~~~~~~   strip: 62% of the file decoded (kDial)
  y  28      .........++++++++++++++++++++++++++++++++.........   keyline 250x188 at x 75, y 28; photo 248x186
  y  40      .........+                              +.........
  y  52      .........+                              +.........
  y  64      .........+                              +.........
  y  76      .........+                              +.........
  y  88      .........+                              +.........
  y 100      .........+                              +.........
  y 112      .........+                              +.........
  y 124      .........+                              +.........
  y 136      .........+                              +.........
  y 148      .........+                              +.........
  y 160      .........+                              +.........
  y 172      ..[ H  ].+                              +.[ >  ]..   plates 40x40 (9.4 mm) at y 172..211
  y 184      ..[ H  ].+                              +.[ >  ]..
  y 196      ..[ H  ].+                              +.[ >  ]..
  y 208      ..[ H  ].+                              +.[ >  ]..
  y 222      @@ 2 garden  quantumrob...............today 14:32.   caption: camera, who | when
  y 232      ..................................................
```

### 4.3B, no photos yet

```
col (8 px)   0         1         2         3         4
             01234567890123456789012345678901234567890123456789
  y   2      PHOTOS...................................none yet.   title on the mat
  y  20      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   strip, 4 px
  y  28      .........++++++++++++++++++++++++++++++++.........   keyline 250x188 at x 75, y 28
  y  40      .........+                              +.........
  y  52      .........+                              +.........
  y  64      .........+                              +.........
  y  76      .........+                              +.........
  y  88      .........+                              +.........
  y 100      .........+             @@@@             +.........   the camera icon, 2x, kDim
  y 112      .........+                              +.........
  y 124      .........+        No photos yet         +.........   kInk
  y 136      .........+                              +.........
  y 148      .........+     SNAPSHOT takes one.      +.........   kDim
  y 160      .........+                              +.........
  y 172      ..[ H  ].+                              +.........   home only: nothing to play
  y 184      ..[ H  ].+                              +.........
  y 196      ..[ H  ].+                              +.........
  y 208      ..[ H  ].+                              +.........
  y 232      ..................................................
```

### 4.3B, every box (400 x 240 drawn)

| Element | x | y | w | h | Colour |
|---|---|---|---|---|---|
| mat | 0 | 0 | 400 | 240 | `kRule` |
| title text | 4 | 2 | 392 | 16 | `PHOTOS` `kName`; mode; position `kInk` |
| strip | 0 | 20 | 400 | 4 | track `kBg`; fill `kDial` or `kName` |
| keyline | 75 | 28 | 250 | 188 | `kBg` |
| photo | 76 | 29 | 248 | 186 | |
| home plate | 17 | 172 | 40 | 40 | `kBg`, 1 px `kDim`, icon `kInk` |
| play plate | 343 | 172 | 40 | 40 | as home; pause `kName` in SLIDE |
| home target | 9 | 164 | 56 | 56 | the plate and 8 px round it |
| play target | 335 | 164 | 56 | 56 | |
| chevrons | 33 / 358 | 115 | 8 | 13 | `kDim` |
| caption text | 4 | 222 | 392 | 16 | per field |

Margins beside the keyline: 75 px (17.6 mm), so the plates sit wholly on
the mat.

### MF35 landscape, the longest caption

A 16-glyph satellite name and a 20-glyph handle, both whole:

```
col (8 px)   0         1         2         3         4         5
             012345678901234567890123456789012345678901234567890123456789
  y   2      PHOTOS...........................................20 of 2000.   title on the mat
  y  20      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   strip, 4 px
  y  28      .......++++++++++++++++++++++++++++++++++++++++++++++.......   keyline 356x268 at x 62, y 28; photo 354x266
  y  44      .......+::::::::::::::::::::::::::::::::::::::::::::+.......
  y  60      .......+::::::::::::::::::::::::::::::::::::::::::::+.......
  y  76      .......+::::::::::::::::::::::::::::::::::::::::::::+.......
  y  92      .......+::::::::::::::::::::::::::::::::::::::::::::+.......
  y 108      .......+::::::::::::::::::::::::::::::::::::::::::::+.......
  y 124      .......+::::::::::::::::::::::::::::::::::::::::::::+.......
  y 140      .......+::::::::::::::::::::::::::::::::::::::::::::+.......
  y 156      .......+::::::::::::::::::::::::::::::::::::::::::::+.......
  y 172      .......+::::::::::::::::::::::::::::::::::::::::::::+.......
  y 188      .......+::::::::::::::::::::::::::::::::::::::::::::+.......
  y 204      .......+::::::::::::::::::::::::::::::::::::::::::::+.......
  y 220      .......+::::::::::::::::::::::::::::::::::::::::::::+.......
  y 236      .......+::::::::::::::::::::::::::::::::::::::::::::+.......
  y 240      [  H   ]::::::::::::::::::::::::::::::::::::::::::::[  >   ]   plates 52x52 (8.0 mm) at y 240..291
  y 252      [  H   ]::::::::::::::::::::::::::::::::::::::::::::[  >   ]
  y 268      [  H   ]::::::::::::::::::::::::::::::::::::::::::::[  >   ]
  y 284      [  H   ]::::::::::::::::::::::::::::::::::::::::::::[  >   ]
  y 302      @@ 4 shed_camera_16ch  daytona_on_the_c128.....27 Sep 18:02.   caption: camera, who | when
  y 316      ............................................................
```

- Keyline `R(62,28,356,268)`, photo 354 x 266; plates `R(5,240,52,52)` and
  `R(423,240,52,52)` in the 62 px margins (the grid draws them touching the
  keyline; in pixels there are 5 px of mat between).
- Portrait, 320 x 480: photo 310 x 232, keyline `R(4,81,312,234)`, captions
  at y 319 and 339, plates `R(8,420,52,52)` and `R(260,420,52,52)`.

### WS2 portrait (VIEW, then AUTO)

```
col (8 px)   0         1         2
             012345678901234567890123456789
  y   2      PHOTOS...............3 of 214.   title on the mat
  y  20      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   strip, 4 px
  y  28      ++++++++++++++++++++++++++++++   keyline 232x174 at x 4, y 28; photo 230x172
  y  44      +::::::::::::::::::::::::::::+
  y  60      +::::::::::::::::::::::::::::+
  y  76      +::::::::::::::::::::::::::::+
  y  92      +::::::::::::::::::::::::::::+
  y 108      +::::::::::::::::::::::::::::+
  y 124      +::::::::::::::::::::::::::::+
  y 140      +::::::::::::::::::::::::::::+
  y 156      +::::::::::::::::::::::::::::+
  y 172      +::::::::::::::::::::::::::::+
  y 188      +::::::::::::::::::::::::::::+
  y 204      ..............................
  y 208      @@ 2 garden  quantumrob.......   caption: camera, who
  y 220      ..............................
  y 228      today 14:32...................   caption: when
  y 236      ..............................
  y 248      .[  H   ]............[  >   ].   plates 64x64 (8.1 mm) at y 248..311
  y 252      .[  H   ]............[  >   ].
  y 268      .[  H   ]............[  >   ].
  y 284      .[  H   ]............[  >   ].
  y 300      .[  H   ]............[  >   ].
  y 316      ..............................
```

- Keyline `R(4,28,232,174)`, photo 230 x 172; captions at y 206 and 226;
  plates `R(8,248,64,64)` and `R(168,248,64,64)`, targets `R(0,240,80,80)`
  and `R(160,240,80,80)`. The 96 px between the plates are nothing.
- No side margins, so no chevrons. The halves are `x < 120` and
  `x >= 120`, y 24 to 239.
- The portrait budget is full: the second caption row ends at y 245, 3 px
  above the plates. Nothing else goes on this glass.
- AUTO: the title reads `PHOTOS  NEW` and the strip carries the timer:

```
col (8 px)   0         1         2
             012345678901234567890123456789
  y   2      PHOTOS  NEW..........1 of 215.   title on the mat
  y  20      ==================~~~~~~~~~~~~   strip: the timer, 6 s of 10 left (kName)
  y  28      ++++++++++++++++++++++++++++++   keyline 232x174 at x 4, y 28; photo 230x172
  y  44      +::::::::::::::::::::::::::::+
  y 188      +::::::::::::::::::::::::::::+
  y 208      MM 3 porch....................   caption: camera, who
  y 228      today 03:12...................   caption: when
  y 248      .[  H   ]............[  >   ].   plates 64x64 (8.1 mm) at y 248..311
```

### WS2 landscape (320 x 240)

```
col (8 px)   0         1         2         3
             0123456789012345678901234567890123456789
  y   2      PHOTOS.........................3 of 214.   title on the mat
  y  20      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   strip, 4 px
  y  28      ....++++++++++++++++++++++++++++++++....   keyline 250x188 at x 35, y 28; photo 248x186
  y  40      ....+::::::::::::::::::::::::::::::+....
  y  52      ....+::::::::::::::::::::::::::::::+....
  y  64      ....+::::::::::::::::::::::::::::::+....
  y  76      ....+::::::::::::::::::::::::::::::+....
  y  88      ....+::::::::::::::::::::::::::::::+....
  y 100      ....+::::::::::::::::::::::::::::::+....
  y 112      ....+::::::::::::::::::::::::::::::+....
  y 124      .<..+::::::::::::::::::::::::::::::+.>..
  y 136      ....+::::::::::::::::::::::::::::::+....
  y 148      [   H   ]::::::::::::::::::::::[   >   ]   plates 64x64 (8.1 mm) at y 148..211
  y 160      [   H   ]::::::::::::::::::::::[   >   ]
  y 172      [   H   ]::::::::::::::::::::::[   >   ]
  y 184      [   H   ]::::::::::::::::::::::[   >   ]
  y 196      [   H   ]::::::::::::::::::::::[   >   ]
  y 208      [   H   ]::::::::::::::::::::::[   >   ]
  y 222      @@ 2 garden  quantumrob.....today 14:32.   caption: camera, who | when
  y 232      ........................................
```

The margins are 35 px, under a 64 px plate's 72, so the plates sit over the
picture's bottom corners, black on the photo: the one place the frame gives
way. The empty state's lines centre in the picture's area above the plates'
tops (y 28..147 here), never behind a plate.

### 1.47, AUTO only

```
col (8 px)   0         1         2
             012345678901234567890
  y   2      PHOTOS  NEW..........   title on the mat
  y  20      ============~~~~~~~~~   strip: the timer, 6 s of 10 left (kName)
  y  28      .....................
  y  44      .....................
  y  60      .....................
  y  76      .....................
  y  90      +++++++++++++++++++++   keyline 164x123 at x 4, y 90; photo 162x121
  y  92      +:::::::::::::::::::+
  y 108      +:::::::::::::::::::+
  y 124      +:::::::::::::::::::+
  y 140      +:::::::::::::::::::+
  y 156      +:::::::::::::::::::+
  y 172      +:::::::::::::::::::+
  y 188      +:::::::::::::::::::+
  y 204      +:::::::::::::::::::+
  y 219      MM 3 porch...........   caption: camera, who
  y 236      .....................
  y 239      today 03:12..........   caption: when
  y 252      .....................
  y 268      .....................
  y 284      .....................
  y 300      .....................
  y 316      .....................
```

The block (keyline 123, gap 4, two captions 40) is centred between the
strip and the foot, so the mat above and below is even: 66 px over, 63
under. No position at the title's right: there is no browsing, so
"1 of 215" would be a number nobody can act on.

```
col (8 px)   0         1         2         3
             0123456789012345678901234567890123456789
  y   2      PHOTOS  NEW.............................   title on the mat
  y  20      ========================~~~~~~~~~~~~~~~~   strip: the timer, 6 s of 10 left (kName)
  y  28      ..........++++++++++++++++++++..........   keyline 159x120 at x 80, y 28; photo 157x118
  y  40      ..........+::::::::::::::::::+..........
  y  52      ..........+::::::::::::::::::+..........
  y  64      ..........+::::::::::::::::::+..........
  y  76      ..........+::::::::::::::::::+..........
  y  88      ..........+::::::::::::::::::+..........
  y 100      ..........+::::::::::::::::::+..........
  y 112      ..........+::::::::::::::::::+..........
  y 124      ..........+::::::::::::::::::+..........
  y 136      ..........+::::::::::::::::::+..........
  y 148      ........................................
  y 154      MM 3 porch..................today 03:12.   caption: camera, who | when
  y 160      ........................................
```

1.47 landscape, 320 x 172: photo 157 x 118 at x 81, one caption row.

## Finding, loading and the other states

- **The moment of a step**: on a swipe, a tap or an auto-show the old
  picture is cleared at once (a person's step cuts, as the status panel's
  tapped pages do), the keyline stays at its size, the caption clears, and
  the title's right says `finding`, with a 24 px `kDial` segment sweeping
  the strip. That is the directory pass.
- **Loading**: once the file is found, the caption is drawn (the name gives
  the kind and the time; the comment, read first, gives camera and who), the
  keyline is redrawn at the new picture's size, and the title says
  `loading 62%` while the strip fills in `kDial`: bytes the decoder has
  consumed over the file's size, which the directory pass already knows. A
  true measure of the work left, for one counter in the read callback.
- **The rows land as they are decoded.** TJpgDec hands the picture over a
  row of blocks at a time, top to bottom, and the loop has to copy each
  finished row into the framebuffer anyway. Showing them as they come is
  free, so the picture builds downwards while the strip fills. It is not a
  low-resolution first pass: that would cost a second decode, because
  TJpgDec's time is the Huffman decode of the whole file whatever the
  scale.
- **Whole**: the title shows the position; in AUTO and SLIDE the strip
  turns to the `kName` timer.
- **Empty** (the set has no photos yet): the camera icon at 2x in `kDim`,
  `No photos yet` in `kInk`, `SNAPSHOT takes one.` in `kDim`. The title
  says `none yet`; home plate only.
- **No card**: there is no Photos to open.
- **Can't show this one**: the warn triangle at 2x in `kWarm`, `Can't show
  this one` in `kInk`, and why in `kDim`: `Gone from the card.` or `Can't
  read this one.` (at most 20 glyphs, so the 1.47 holds it). The caption
  stays; a swipe moves on.

## Decoding: what runs where

- **The loop**: the touch read, the recogniser, the modes, the title,
  caption and strip (compare and redraw, as the status fields do), copying
  finished rows into the framebuffer (0.1 us a pixel, the skins' figure: a
  9,600 px band is about 1 ms), the timer.
- **The runner**: one job a step. The directory pass; then open the file,
  read the comment, `skin::checkJpeg`, decode with the ROM TJpgDec through
  `plat::jpegDecode`, area-average into the photo buffer, publish rows done
  and bytes read.
- **Scale.** Decode at the largest of 1/1, 1/2, 1/4, 1/8 whose output is
  still at least the photo's drawn size, then area-average down, streamed,
  with no scaled copy held: UXGA into the 4.3B's 248 x 186 decodes at 1/4
  (400 x 300); the OV5640's 2592 x 1944 into the WS2's 230 x 172 at 1/8
  (324 x 243). Never enlarged. `plat::jpegDecode` needs a scale argument:
  TJpgDec's `jd_decomp` takes one, and Espressif's `esp_jpeg` README lists
  the S3 ROM build's fixed settings as including "output descaling
  enabled". Confirm it against `esp32s3/rom/tjpgd.h` (the guard refused my
  read of it). Without it the decode is full size and 4 to 16 times the
  work: UXGA near 2 s.
- **Our photos pass the ROM's rules**: jpge's re-encode (the watermark) and
  the OV2640 and OV5640 hardware JPEG are baseline YCbCr, 4:2:0 or 4:2:2. A
  photo that fails `checkJpeg` is "Can't read this one", never a crash.
- **Memory, PSRAM**: one photo buffer, allocated when Photos opens and
  freed 30 s after the status panel is back.

| Board | Photo buffer | Board PSRAM |
|---|---|---|
| 4.3B | 92,256 B | 8 MB |
| MF35 v1.0 | 188,328 B | 2 MB, beside a skin's 300 to 428 KB |
| WS2 | 79,120 B | 8 MB |
| 1.47 | 39,204 B | 8 MB |

- **Heap during a step**: TJpgDec's 5 KB work area and one row of blocks of
  accumulators (photo width x 3 channels x 4 bytes x up to 16 rows), under
  16 KB at the MF35's 354, freed when the job ends.
- **Static DRAM** (S3 display profiles only): `lastFiled()` about 100 B;
  the viewer's state (current path and stamp, set, position, total, mode,
  the recogniser) under 128 B; the settings 8 B. Under 256 B. Nothing on the
  WROOM or the camera boards without a glass.
- **Flash**: about 5 KB of code; five 16 x 16 icons and the chevron,
  186 B; no font. The decoder is in ROM.

## Skins

- Photos covers whatever is up: the status layouts, and on the MF35 the
  status skin or a machine skin.
- While it is up the status keeps gathering its figures twice a second
  (RAM) and draws nothing: no dot, no fade, no LED row, no skin lamps, no
  strip. The lights plugin keeps computing; only the glass stops.
- Leaving: `g_shown` is invalidated and the status is drawn whole, or a
  machine skin is copied back from its PSRAM block, the path silent's end
  takes. A top-down wipe: 0.2 s on the 1.47, 0.8 s on the MF35.
- The WS2's recent row for a caller's snap (`EV_SNAP`) and its band's camera
  glyph stay: text, not pictures, and not governed by the privacy switch.

## Privacy: "Photos on the panel"

- CONFIG panel, main page. **No**: no picture ever reaches the glass. No
  Photos (a swipe does nothing), no auto-show, no slideshow, whatever
  CONFIG cameras says.
- **Default yes.** The panel is the sysop's own glass, the cameras are the
  sysop's own cameras, and nothing on it is more private than the Photos
  area it reads, which callers already browse at the camera's levels. A
  display feature shipped off is a feature nobody finds. The room-facing
  board is the exception, and it is one row to turn off.
- The camera and satellite docs say it where a camera is set up: "New
  photos show on this board's display; CONFIG panel, Photos, turns that
  off."
- The panel does not apply the Photos area's read level. The glass is the
  sysop's; the switch is the control.

## CONFIG: where each setting lands

| Setting | Page | Key | Why there |
|---|---|---|---|
| Photos on the panel | CONFIG panel, main | `photos` in `[plugin:panel]` | it is about this glass |
| Slideshow interval | CONFIG panel, main (touch boards) | `slide` in `[plugin:panel]` | only a glass has a slideshow |
| Show new | CONFIG cameras | `show_new` (core) | it is about new photos, from any camera |
| Snaps, Motion, Timelapse | CONFIG cameras | `show_snaps`, `show_motion`, `show_timelapse` (core) | which cameras' events take the glass |
| the glass's geometry | CONFIG panel, Glass sub-page | unchanged keys, `page = "glass"` | set once, never again |

CONFIG camera (the built-in camera's own page) gets nothing: it is full,
and three of the four display boards do not have it. The show rows exist
only on a display board (`BBS_HAS_LCD`); a WROOM with a satellite has no
glass to show on and no rows.

### CONFIG panel, main page, after

Rows after the core four (Enabled, Read, Write, Admin); `>` is a button.
No existing key is renamed; only where some are listed changes.

| Board | Rows | Count |
|---|---|---|
| 1.47 | Photos, Bright %, USB plug, Glass >, Pins > | 5 of 12 |
| WS2 | Photos, Slide s, Bright %, Sleep min, USB plug, Glass >, Pins > | 7 of 12 |
| MF35 | Skin, Photos, Slide s, Bright %, Sleep min, USB plug, Glass >, Pins > | 8 of 12 |
| 4.3B | Driver, Photos, Slide s, Light, Sleep min | 5 of 12 |

| Key | 40: label (9) | 80: label (20) | Kind | Choices | Default | 40 note (38) | 80 note (78) |
|---|---|---|---|---|---|---|---|
| `photos` | `Photos` | `Photos on the panel` | PS_YESNO | | yes | `No: no photo ever shows on the glass.` | `No keeps every photo off this glass: no viewer, and new ones never shown.` |
| `slide` | `Slide s` | `Slideshow, seconds` | PS_CYCLE | `5\|10\|20\|30\|60` | `10` | `Seconds a photo shows in a slideshow.` | `Seconds each photo stays up in a slideshow, started with the play button.` |
| `glass` | `Glass` | `Glass and bus` | PS_PAGE | | | `Size, offsets, colours and the clock.` | `The glass's size, offsets, colour order, mirror and the bus clock.` |

- The Glass button's text is the driver and the size, `ST7789 172x320`.
- The Glass page holds, with `page = "glass"`: Driver (PS_INFO, from the
  1.47's main page), Width, Height, X offset, Y offset, Invert, Mirror,
  Colours, SPI MHz (the MF35's bus clock). Nine rows. Not on the 4.3B, whose
  RGB wiring is the board's. `PANEL` still names the driver.
- Order on the main page: the look (Skin), whether photos show, the
  slideshow, how bright, when dark, which way up, then the wiring nobody
  touches after the first day.

### CONFIG cameras, after (display boards)

| Key | 40: label (9) | 80: label (20) | Kind | Choices | Default | 40 note (38) | 80 note (78) |
|---|---|---|---|---|---|---|---|
| `camera` | `Default` | `Default camera` | CK_TEXT | | blank | as today | as today |
| `show_new` | `Show new` | `Show new photos for` | CK_CYCLE | `off\|5\|10\|30\|60\|300\|stay` (touch), `off\|5\|10\|30\|60\|300` (the 1.47) | `10` | `Seconds a new photo stays up.` | `Seconds a new photo holds the display; stay: until touched.` |
| `show_snaps` | `Snaps` | `New SNAPSHOTs` | CK_YESNO | | yes | `A SNAPSHOT, a caller's or yours.` | `A SNAPSHOT from any camera, built in or a sat, a caller's or your own.` |
| `show_motion` | `Motion` | `New motion shots` | CK_YESNO | | yes | `A shot a motion sensor set off.` | `A picture a motion sensor set off, on any camera.` |
| `show_timelapse` | `Timelapse` | `New timelapse frames` | CK_YESNO | | no | `Each timelapse frame as it comes.` | `Every timelapse frame as it arrives: often, so usually no.` |

- Five rows. Every label fits 9 and 20; every note fits 38 and 78.
- `CK_CYCLE` on a core row needs its choices the way the plugin rows have
  them (`cfgChoices`); if core rows cannot carry a list yet, that is a
  small hand-back.
- **Why three yes/no rows and not one choice**: CONFIG has no multi-choice
  kind, and a cycle through the seven combinations of three kinds is seven
  presses to reach "motion only" on a C64. Three rows read at a glance on
  every terminal, plain ASCII's questions included.
- **Why snaps and motion yes, timelapse no**: a snap is somebody wanting to
  be seen, a motion shot is what a sysop set a sensor for, and a timelapse
  every minute would make the panel a photo frame nobody asked for.
- When CONFIG panel's Photos is no, `show_new`'s note at 80 says so
  instead: `Off: CONFIG panel's Photos is no, so nothing shows.` (51).
- The two fixed times are constants, not rows: VIEW's 60 s idle and the
  silent override's 60 s.

## PANEL

Three lines appended to its report, so the host tests read Photos without
a glass:

```
Photos on, new ones 10 s: snaps motion
Showing 3 of 214 (view, photos), camera 2 garden, today 14:32
Touch seen, 214 taps, 31 swipes, last at 188,104
```

`last at x,y` is also the bench's way to check each board's touch
orientation against its "USB plug" setting.

## Icons

16 x 16, 2 px pen, top bit left, the house rule. The camera icon is the WS2
spec's `kIconCamera`; timelapse is revision 0's `clock`; the warn triangle
is the status glyph at 2x.

```
kIconPlay            kIconPause           kIconHome            kIconMotion
................     ................     .......XX.......     ................
................     ................     ......XXXX......     ................
...XX...........     ...XXXX..XXXX...     .....XX..XX.....     .XX..........XX.
...XXXX.........     ...XXXX..XXXX...     ....XX....XX....     XX............XX
...XXXXXX.......     ...XXXX..XXXX...     ...XX......XX...     XX..XX....XX..XX
...XXXXXXXX.....     ...XXXX..XXXX...     ..XX........XX..     XX.XX......XX.XX
...XXXXXXXXXX...     ...XXXX..XXXX...     .XXXXXXXXXXXXXX.     XX.XX.XXXX.XX.XX
...XXXXXXXXXXXX.     ...XXXX..XXXX...     ..XX........XX..     XX.XX.XXXX.XX.XX
...XXXXXXXXXXXX.     ...XXXX..XXXX...     ..XX........XX..     XX.XX.XXXX.XX.XX
...XXXXXXXXXX...     ...XXXX..XXXX...     ..XX..XXXX..XX..     XX.XX.XXXX.XX.XX
...XXXXXXXX.....     ...XXXX..XXXX...     ..XX..XXXX..XX..     XX.XX......XX.XX
...XXXXXX.......     ...XXXX..XXXX...     ..XX..XXXX..XX..     XX..XX....XX..XX
...XXXX.........     ...XXXX..XXXX...     ..XX..XXXX..XX..     XX............XX
...XX...........     ...XXXX..XXXX...     ..XX..XXXX..XX..     .XX..........XX.
................     ................     ..XXXXXXXXXXXX..     ................
................     ................     ..XXXXXXXXXXXX..     ................

kChevronL, 8 x 13 (kChevronR is its mirror), rows top to bottom:
......XX  .....XX.  ....XX..  ...XX...  ..XX....  .XX.....  XX......
.XX.....  ..XX....  ...XX...  ....XX..  .....XX.  ......XX
```

```
kIconPlay   0x0000,0x0000,0x1800,0x1E00,0x1F80,0x1FE0,0x1FF8,0x1FFE,0x1FFE,0x1FF8,0x1FE0,0x1F80,0x1E00,0x1800,0x0000,0x0000
kIconPause  0x0000,0x0000,0x1E78,0x1E78,0x1E78,0x1E78,0x1E78,0x1E78,0x1E78,0x1E78,0x1E78,0x1E78,0x1E78,0x1E78,0x0000,0x0000
kIconHome   0x0180,0x03C0,0x0660,0x0C30,0x1818,0x300C,0x7FFE,0x300C,0x300C,0x33CC,0x33CC,0x33CC,0x33CC,0x33CC,0x3FFC,0x3FFC
kIconMotion 0x0000,0x0000,0x6006,0xC003,0xCC33,0xD81B,0xDBDB,0xDBDB,0xDBDB,0xDBDB,0xD81B,0xCC33,0xC003,0x6006,0x0000,0x0000
kChevronL   0x0300,0x0600,0x0C00,0x1800,0x3000,0x6000,0xC000,0x6000,0x3000,0x1800,0x0C00,0x0600,0x0300
```

- Motion is a sensor firing: a dot and two pairs of arcs, `kWarm`. It shows
  only in Photos' caption, never beside the band's announce tower it
  resembles.
- The house has a solid door, so no stroke under 2 px.
- Play is filled: an outline triangle at 16 px reads as an arrow, which on
  a glass with chevrons is the wrong word.

## Hand-backs for the builder

- **The comment** (camera and link lanes): `Camera <n> <name>.` added to
  every photo's JPEG comment; camsat puts the satellite's into `snapMsg`'s
  comment text.
- **`photos::lastFiled()`** and its serial: the photo just filed, its
  camera, kind, handle and whether the snapper was hidden or lurking; set by
  the camera and camsat in `finish()`.
- **The name clash** (link lane): on a taken name, the next second's stamp,
  up to five tries.
- **`PluginSetting::page`** (core, appended, default nullptr); `pageOwner`
  checks it first; the geometry rows get `page = "glass"` on the ST7789 and
  i80 boards.
- **CONFIG cameras**: the four show rows, `BBS_HAS_LCD` only; `CK_CYCLE`
  with choices on a core row if it cannot yet.
- **Touch positions**: `plat::touchPoll(bool& down, int16_t& x, int16_t& y)`
  in drawn pixels and the current orientation; the GT911 read widened to 6
  bytes; the CST816D read on INT; an FT6236 driver for both MF35 revisions
  (`BBS_HAS_TOUCH`, I2C 38/39, INT 40, 0x38, the pins in
  `BBS_PINS_ONBOARD`). If a sysop gives the serial bridge J2's lines, touch
  is off and the MF35 keeps its auto-show.
- **Touch while silent**: polled, with the 60 s glass-only override.
- **`BBS_LCD_UM_PER_PX`** per display profile: 234, 153, 127, 103.
- **`plat::jpegDecode` with a scale and a bytes-read count**, after
  confirming descaling in the ROM header.
- **`runner::quiet()`**: nothing queued, nothing running.
- **The panel**: the Photos mode (mat, title, strip, keyline, caption,
  plates, chevrons), the step job (the directory pass and the decode), the
  four states, the recogniser as a pure function
  (`gesture::classify(down, last, maxDist, ms, umPerPx)`), status drawing
  suspended while Photos is up, `g_shown` invalidated on the way out,
  PANEL's three lines.
- **The host has no touch**: a host-only `<data>/touch.txt` read by the
  host's `touchPoll` (lines `down x y`, `up`, with a delay), the same shape
  as `hostio.txt`, so a test can swipe.
- **Tests**, for when Rob okays a run: the recogniser at every boundary per
  board (slop, swipe length, 2:1 direction, 400 and 800 ms, the lost-lift
  300 ms); every Photos box inside its glass and no two overlapping except
  plates over a picture where the margin is narrower; the step against a
  folder of photos, a sysop's own `garden.jpg`, handle folders, a deleted
  current photo, two photos in one second, both ends and the wrap; the
  comment parsed with and without the camera sentence; every cell of the
  mode table; the scale choice for VGA, XGA, UXGA and 2592 x 1944 into each
  photo size; the caption's shrink order at 21, 29, 49 and 59 glyphs; the
  CONFIG pages' row counts per board, every label and note against 9, 20,
  38 and 78.

## What stays as it is

- **Every status layout, pixel for pixel.** No chip, no region, no new
  field on the status glass. The swipe needs no pixels.
- **A tap on the status panel turns the header's page**, and the 4.3B's
  10 s hold on a tapped page. Only its moment moves, from contact to lift.
- **The ring rule**: a live ring shows, whatever else was up.
- **Silent keeps every light dark**: the override lights the glass only,
  and a ring does not light a silent panel.
- **The palette and its meanings.** Photos adds no new colour: the mat is
  the site's rule grey and the accent is the site's `--name`, both already
  in the panel's table. Dial still means what you can act on (the camera
  number), yellow a real time, live new, warm attention.
- **8 x 16 Spleen only.** The caption is arm's-length text; the picture is
  what carries across a room.
- **The 4.3B's 2x picture**, until a measurement says otherwise.
- **The WS2's snap event and camera glyph.**
- **The 4.3B review's principle**: a touch changes what the glass shows,
  never what the board does.

## Implementation order

Cheapest and most visible first. Each step is on a glass on its own.

- The comment's camera sentence and `photos::lastFiled()`, in the camera
  and camsat. No glass yet; the caption and the auto-show need them.
- The auto-show on the 4.3B: its CONFIG panel has room, and it is on Rob's
  desk. The Photos mode's frame, the step job with the scale, the progress
  strip, the timer, `NEW`; the CONFIG cameras rows and CONFIG panel's
  Photos. No touch position needed.
- GT911 positions and the recogniser. VIEW: swipes, halves, the home plate,
  the chevrons, the two sets, empty, can't show.
- SLIDE: the play plate and Slide s, with `runner::quiet()`.
- Touch through silent, the 60 s override.
- `PluginSetting::page` and the Glass page on the ST7789 boards. The WS2's
  CST816D positions; the WS2's Photos and the 1.47's auto-show.
- The FT6236 driver; the MF35's Photos over the status skin and the machine
  skins.
- The name clash rides with the link lane; Photos does not wait for it.

## Summary

- Photos is its own place: slate mat, `PHOTOS` in violet, a keyline frame,
  a progress strip, home and play plates.
- Entered by a swipe from the status panel; left by a swipe back, the home
  plate, 60 s idle or a ring.
- It reads the card: one directory pass a step finds the next file by the
  stamp in its name. No ring, no list, no sort.
- A swipe or a tap on a half moves one photo; up or down switches Photos
  and Timelapse.
- While a file is found and decoded the strip and the title show it:
  `finding`, then `loading 62%`.
- New photos take the glass for 10 s, snaps and motion, set on CONFIG
  cameras.
- CONFIG panel keeps Photos on the panel and Slide s, on its main page.
- Browsing is never interrupted; silent never lights; a touch lights the
  glass alone for 60 s.
- 1.47: auto-show only. The frame costs 8% to 26% of the photo's area
  against revision 0.
- Owed first: the camera in the JPEG comment, touch positions, the Glass
  sub-page, a decode scale.

## Decisions for Rob

- The auto-show rows on **CONFIG cameras**, not CONFIG camera: the built-in camera's page is full and missing on three display boards.
- The frame: **accept 248 x 186 on the 4.3B** (26% smaller than revision 0) for Photos to read as its own place.
- Photos' colours: **the site's rule grey as the mat and `--name` violet as the accent.**
- Two sets, **Photos (snaps and motion) and Timelapse**, switched by a vertical swipe.
- No prefetch: **one photo buffer**; add a prefetch only if the bench finds swipes slow.
- The camera in the caption: **one sentence in the JPEG comment**; older photos show the kind word.
- The snapper's handle: **shown from the comment**, hidden only on the auto-show of a caller hiding right now.
- The name clash: **the next free second**, in the link lane.
- Stands from revision 0: Photos on the panel yes; Show new 10 s with snaps and motion; VIEW not interrupted; a new photo wakes a sleeping panel, never a silent one; the silent override 60 s, glass only; VIEW idle 60 s; the 1.47 on auto-show only; `PluginSetting::page` and the Glass sub-page; the 4.3B at its drawn 400 x 240.

Sources:

- [CST816D datasheet (Waveshare's copy)](https://files.waveshare.com/wiki/common/CST816D_datasheet_En_V1.3.pdf): register 0x01 gesture codes, 0x02 to 0x06 finger count and position.
- [FT6X36 datasheet (FocalTech, BuyDisplay's copy)](https://www.buydisplay.com/download/ic/FT6236-FT6336-FT6436L-FT6436_Datasheet.pdf): TD_STATUS and P1 registers.
- [FT6x06 application note (Adafruit's copy)](https://cdn-shop.adafruit.com/datasheets/FT6x06_AN_public_ver0.1.3.pdf): the same register family.
- [esp_jpeg README (Espressif)](https://github.com/espressif/idf-extra-components/blob/master/esp_jpeg/README.md): the ROM TJpgDec's fixed settings, output descaling enabled.
- GT911 registers as already cited in `tty-ux-panel-ws43b-2026-09-28.md` (0x814E status, 0x8150 to 0x8153 point 1).
