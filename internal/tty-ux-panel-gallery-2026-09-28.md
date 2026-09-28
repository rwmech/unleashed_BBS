# The photo gallery on the panel: swipe, auto-show and slideshow (1.2.0)

Design spec for Rob's 1.2.0 feature: "swiping left or right to show the last
pictures from the camera or sat camera. Most boards with displays can do
this." Plus, the same day: "an option to make that the default screen when
pictures are taken, or 'show new photos on the display for x seconds before
returning to status page'".

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
  `src/plugins/camera_rules.h`, `src/plugins/camera.cpp`;
- camsat's BBS side, `release-prep/wt-camsat-mb/bbs/camsat.cpp`;
- the main tree's CONFIG machinery, `src/core/plugin.h` and
  `src/core/bbs_sysop.cpp` (`pageOwner`, `kCoreRows`, `Form::kMaxFields`).

Nothing was built, run or put on a glass. The boxes were laid out by
`gallery_mock.py` and `gallery_art2.py` in the session scratchpad from the
numbers in this report, one character to 8 px, and every box was checked
inside its glass. One read was refused by the machine's guard: the ROM
TJpgDec header under `.platformio/packages` (not a dev directory). The claim
that depends on it, that the S3 ROM decoder scales, rests on Espressif's
`esp_jpeg` README instead, and is marked as a check for the builder.

## The verdict

Buildable, and a good fit for these glasses, but it lands on six gaps that
have to close first, and one of them is a real bug. A photo on the card does
not say which camera took it, so the caption Rob asked for has no source,
and two cameras snapping in the same second ask for the same name and one
picture is lost. CONFIG panel is full on three of the four boards. Only the
4.3B reads its touch controller, and it reads no position, so a swipe cannot
be seen anywhere today. None of that is hard: a 20-entry ring of recent
photos in `photos::`, a sub-page for the glass's geometry, and a position
from each touch driver. The design itself is one model on every board: the
status panel is position 0, photos are 1 to 20 to its right, a swipe or a
tap on either half moves one step, two plates (home and play) sit in the
bottom corners, the caption takes the header's place, and the track under
it becomes the timer. The 1.47 has no touch and its BOOT button is spoken
for three times over, so it gets the auto-show and not the gallery.

## Rob's decisions, built in (2026-09-28)

- The sysop chooses which kinds take the glass: snaps, motion, timelapse.
  Specified as three yes/no rows, since CONFIG has no multi-choice kind.
- Bursts, from one camera or several callers: always the latest.
- A tap or swipe during the auto-show continues through the history.
- One on-screen button switches viewer (manual) and slideshow (automatic),
  with the slideshow's interval as a setting.
- Silent mode and silent hours are honoured: an auto-show never lights a
  blanked panel. A tap wakes it through silent for a set time, then silent
  resumes.
- A privacy switch, "Photos on the panel": off, no picture ever shows.
- Everything on CONFIG panel, with a sub-page. No new page, no alias.

## Findings, worst first

### 1. A photo does not say which camera took it, and two cameras can collide

- `camera_rules.h`: names are `SNAP-20260924-171204.JPG`,
  `timelapse/TL-...JPG`, `motion/MO-...JPG` (and the handle schemes). No
  camera number, no camera name.
- The JPEG comment says the board, the time and who: `camera.cpp:1028` and
  `camsat.cpp:421` write the same text. No camera either.
- `photos::file` knows the relative name and nothing else.
- So "camera number and name" in the caption has no source today.
- **The bug, found on the way.** Both cameras name by the second. A caller
  snapping camera 1 and another snapping camera 2 in the same second both
  ask for `SNAP-20260928-143200.JPG`. `photos::file` refuses a taken name
  (`photos.h`, "False when rel is taken"), and camsat reports "a photo with
  that name is already there". The second photo is lost. The same holds for
  two motion satellites firing together (`motion/MO-...`). Rare, real, and
  it grows with every satellite. Hand-back to the link lane: on a taken
  name, file it as `...-<camera number>.JPG` and teach `nameKey` the
  suffix, so retention still counts it.

Fix for the caption: a ring of the 20 most recent photos in `photos::`,
filled by the camera and camsat at the moment they file, kept on the card
so it survives a restart. Specified in "What the gallery holds".

### 2. CONFIG panel is full on three of four boards

`kCoreRows` is 4 and `Form::kMaxFields` is 16, so a plugin page holds 12
of its own rows. Counted from the source:

| Board | Rows on the main page | Free |
|---|---|---|
| 1.47 (ST7789) | Driver, Pins, Width, Height, X offset, Y offset, USB plug, Invert, Mirror, Colours, SPI MHz, Bright % | 0 |
| WS2 (ST7789, touch) | Sleep min (in Driver's row), Pins, Width, Height, X offset, Y offset, USB plug, Invert, Mirror, Colours, SPI MHz, Bright % | 0 |
| MF35 (i80) | Skin, Pins, Width, Height, X offset, Y offset, USB plug, Invert, Mirror, Colours, bus clock, Bright % | 0, and a touch driver wants Sleep min too |
| 4.3B (RGB) | Driver, Light, Sleep min | 9 |

A `PS_PAGE` button collects rows by key prefix (`pageOwner` in
`bbs_sysop.cpp`: the button's key then a digit or an underscore). So the
Photos page is free: its keys are `photo_*`. Making room for its button is
not: none of the existing geometry keys can move to a sub-page without being
renamed, and renaming a key in a file every sysop already has is a migration.

Fix: one appended member on `PluginSetting`, `const char* page`, naming the
`PS_PAGE` row a setting belongs on whatever its key. `pageOwner` checks it
before the prefix. About ten lines of core, no key renamed, no file
migrated. Then a Glass sub-page takes the eight rows a sysop sets once and
never again, and the main page keeps what a sysop actually changes. Section
"CONFIG panel".

### 3. No board reads a touch position

- 4.3B: `touchPoll(bool& down)` reads the GT911's status byte (0x814E) and
  nothing else.
- WS2: taps from the INT line only, by design ("no coordinate needs...",
  board.h), and no I2C at all.
- MF35: an FT6236 on I2C 38/39, INT 40, in both revisions' schematics, and
  no driver. `BBS_HAS_TOUCH` is not set.

A swipe is a direction and a distance, so all three need a position. Fix:
`plat::touchPoll(bool& down, int16_t& x, int16_t& y)`, in the panel's drawn
pixels and the panel's current orientation, and one recogniser in
`panel.cpp` for all three. The registers:

| Controller | Read | Bytes | Layout |
|---|---|---|---|
| GT911 (4.3B, 0x5D) | 0x814E..0x8153 | 6 | status (bit 7 ready, low nibble count), track id, X lo, X hi, Y lo, Y hi; physical 800 x 480, halve for the drawn 400 x 240 |
| CST816D (WS2, 0x15) | 0x02..0x06 | 5 | finger count, X hi (low 4 bits), X lo, Y hi, Y lo |
| FT6236 (MF35, 0x38) | 0x02..0x06 | 5 | the same shape as the CST816D: TD_STATUS, P1_XH (event in bits 7:6), P1_XL, P1_YH, P1_YL |

The CST816D and FT6236 also offer their own gesture codes (the CST816D's
register 0x01: 0x03 slide left, 0x04 slide right, 0x05 click). Not used: two
chips with two sets of fixed thresholds would make one gesture feel
different on two boards, and the GT911 has none in normal mode. One
recogniser, thresholds in millimetres.

### 4. The 4.3B draws at half resolution

The RGB driver fills its bounce buffers from a 400 x 240 picture, every
pixel doubled, so the refill interrupt on core 1 reads 192 KB of PSRAM a
frame instead of 768 KB. A photo drawn there is at most 288 x 216 pixels,
shown at 576 x 432 on the glass: about 108 ppi, a desktop monitor's
density. With a proper area-average it reads clean, not blocky.

A native photo layer (the refill reading a full-resolution buffer in the
photo's rows) would roughly triple the interrupt's PSRAM traffic, from
about 7.5 MB/s to about 22 MB/s, on the loop's core. That is a Rule no. 1
question, and it is answered by SYS's loop figures, not by taste. Ship at
the drawn resolution; measure before offering more.

### 5. The runner is shared, and a decode is not short

The skins decode their background on the shared runner (`skin.cpp`,
1.1.2): about 150 ms for 480 x 320, once, at a CONFIG save. A photo is
decoded at every swipe and every slideshow step, from a bigger file:
estimated 0.3 to 1 s including the card read (a UXGA JPEG is 150 to 300 KB).
Every caller's FILES page and forum walk waits behind a runner job.

Fix, three rules:

- A decode a person is waiting for (a swipe, an auto-show) posts at once.
- A prefetch or a slideshow step posts only when the runner is quiet
  (nothing queued, nothing running): hand-back `runner::quiet()`.
- Measure on the bench: SYS's slow passes do not move with the gallery in a
  slideshow and two callers listing files.

Nothing of the decode is on the loop.

### 6. The 1.47 has no touch, and its BOOT button is taken

GPIO0 on the Waveshare 1.47 already means three things: ROM download mode
if held at reset, the BOOT-hold password and factory reset if held within
10 s of start, and the backup window when pressed with the sysop logged in.
A fourth meaning ("press to see a photo") makes every press ambiguous, and
the likeliest wrong outcome, opening the backup window, puts the Wi-Fi
password on the network. No.

The 1.47 gets the auto-show, which needs no input, and not the gallery.

### 7. The status panels have no room for a photos button, and need none

Every status layout is specified to the pixel and built. A tappable
"photos" chip would take a region from one of them per board and change a
glass Rob has approved. Entry is a swipe, which needs no pixels, and the
auto-show teaches it: the first time a new photo takes the glass, it is
already the gallery.

The 4.3B review's rule survives, and is what makes regions acceptable now:
a tap may change what the glass shows, never what the board does. Every tap
and plate in the gallery only changes the picture.

## Which boards get what

| Board | Touch | Gallery | Auto-show | Photo, drawn | On the glass |
|---|---|---|---|---|---|
| 4.3B, 400 x 240 drawn | GT911 | yes | yes | 288 x 216 | 67.4 x 50.5 mm |
| MF35 landscape, 480 x 320 | FT6236, driver owed | yes, once the driver lands | yes | 394 x 296 | 60.3 x 45.3 mm |
| WS2 portrait, 240 x 320 | CST816D, positions owed | yes | yes | 240 x 180 | 30.5 x 22.9 mm |
| 1.47 portrait, 172 x 320 | none | no | yes | 172 x 129 | 17.7 x 13.3 mm |
| ETH, the camera boards without a glass | | | | | |

Landscape on the WS2 and 1.47 and portrait on the MF35 follow the same rule
(below) and are mocked there.

## What the gallery holds

- **The newest 20 photos across every camera, newest first.** 20 because
  it is a morning of snaps on a busy board and a swipe count a finger will
  make; "3 of 20" is Rob's own example.
- **Snaps and motion shots, every one. Timelapse: one slot per camera,
  always its newest frame.** A timelapse at its 10 s minimum would push
  every snap out of the ring in 200 s. One slot per camera keeps "what the
  garden looks like now" in the gallery without drowning anything. A new
  frame replaces that camera's slot and moves it to the front.
- The rows that choose what takes the glass (below) do not choose what is
  kept. A timelapse that never auto-shows is still in the gallery.
- **Only photos a camera filed.** A sysop's own `garden.jpg` in Photos is
  not a camera's and is not shown, the same line retention draws.
- **The ring**, in `photos::` so SATS and anything later can read it too:

```
struct Recent {                 // 96 bytes
    char     rel[48];           // under Photos: "motion/MO-20260928-031200.JPG"
    char     name[17];          // the camera's name when it was taken: "porch"
    char     who[21];           // the snapper's handle, "" for motion and timelapse
    uint32_t at;                // epoch seconds, from the name's own stamp
    uint8_t  cam;               // camera number 1..9, 0 unknown
    uint8_t  kind;              // SNAP, MOTION, TIMELAPSE
    uint8_t  flags;             // HIDDEN: the snapper was hidden or lurking
    uint8_t  spare;
};
```

  - `photos::filed(rel, cam, name, kind, who, hidden)`, called by the camera
    and camsat on the loop, in their `finish()`, once the runner has said the
    photo is on the card. RAM only; it bumps `photos::recentSerial()`.
  - `photos::recent(i)`, newest first, and `recentCount()`.
  - The name is kept, not looked up later: a satellite renamed next week did
    not take last week's picture under its new name.
- **Kept on the card**: `<card>/photos/.recent`, 20 records of 96 bytes,
  1,920 bytes, rewritten whole by a runner job after each `filed()`. One
  open, front to back. The leading dot keeps it out of every listing.
- **Seeded once when `.recent` is missing** (the first 1.2.0 boot, a card
  from another board): a runner job reads Photos, `motion/` and
  `timelapse/` with `plat::sdList` (one read a folder), plus each handle
  folder if "Name snaps" is "by handle", and keeps the 20 newest by
  `nameKey`. Camera 0, name and who unknown: the caption shows the kind
  word instead (below).
- A photo that has gone (retention, ERASE, a laptop) is dropped from the
  ring when the decoder cannot open it, and the gallery steps on to the next
  in the direction of travel.
- Static DRAM: 1,920 bytes on the S3 display profiles only, or PSRAM with
  `EXT_RAM_BSS_ATTR` where the profile allows it. Nothing on the WROOM or
  the camera boards without a glass.

## Positions and gestures

Position 0 is the status panel (or the skin on the MF35). Positions 1 to N
are the photos, newest first. Left is newer, right is older, the way every
phone's photo roll runs.

```
   [status]  <->  [1 newest]  <->  [2]  <->  ...  <->  [20 oldest]
   swipe left moves right along this line; swipe right moves left
```

| Gesture | At the status | On a photo |
|---|---|---|
| swipe left (finger right to left) | open the gallery at 1 | one older; at N, the position flashes `kWarm` 300 ms and nothing moves |
| swipe right | nothing | one newer; from 1, back to the status |
| tap, right half of the glass | the header's next page (as now) | one older |
| tap, left half of the glass | the header's next page (as now) | one newer; from 1, back to the status |
| tap on the home plate | | back to the status |
| tap on the play plate | | viewer to slideshow, or back |
| long press | nothing (kept for the 4.3B's lights-out idea) | nothing |
| vertical swipe | nothing | nothing |

The halves are the whole glass split at `w / 2`, less the two plates'
targets. A tap in the caption bar counts by its half too.

**What a tap and a swipe are**, one recogniser, in drawn pixels:

- Down point, last point, and the largest distance from the down point.
- **Tap:** lifted within 400 ms of going down, never more than the slop
  from the down point.
- **Swipe:** lifted within 800 ms, `|dx| >= swipe`, `|dx| >= 2 |dy|`
  (within 27 degrees of level).
- **Long press:** held over 800 ms within the slop.
- Anything else is nothing: a slow drag, a diagonal, a palm.
- Judged on lift, never in motion. A tap therefore acts on lift, not on the
  first contact as the 4.3B does today: about 100 ms later, and the price
  of telling a tap from the start of a swipe. A wake is the exception: it
  acts on contact, and the rest of that touch is ignored until lift.
- A touch that starts on a plate and moves past the slop is a swipe, not a
  press. Plates act on lift within the slop.

Thresholds: slop 3 mm, swipe `min(12 mm, 30% of the glass's width)`, from a
new board define `BBS_LCD_UM_PER_PX` (micrometres a drawn pixel: 234, 153,
127, 103).

| Board | mm a drawn px | Glass width | Slop | Swipe |
|---|---|---|---|---|
| 4.3B | 0.234 | 93.6 mm | 13 px | 51 px (12.0 mm) |
| MF35 landscape | 0.153 | 73.4 mm | 20 px | 78 px (12.0 mm) |
| MF35 portrait | 0.153 | 49.0 mm | 20 px | 78 px (12.0 mm) |
| WS2 portrait | 0.127 | 30.5 mm | 24 px | 72 px (9.1 mm) |
| WS2 landscape | 0.127 | 40.6 mm | 24 px | 94 px (12.0 mm) |

Why these: a tap drifts 1 to 2 mm, a deliberate thumb swipe on a phone runs
15 to 25 mm, and 12 mm sits well clear of both. The 30% cap is for the WS2,
whose whole glass is 30 mm wide.

**Sampling**, on the loop:

- GT911: every 50 ms while nothing is down, as now, and every 20 ms while a
  finger is. One 6-byte read, about 250 us at 400 kHz. The first contact
  can be seen up to 50 ms late, which shortens a fast flick's measured
  travel by at most its first 50 ms: a 20 mm flick in 150 ms still measures
  13 mm.
- CST816D and FT6236: read on the INT flag the ISR already sets, at most
  every 20 ms, 5 bytes, about 150 us. Nothing is read while nothing is
  touched.
- A finger seen down with no report for 300 ms is taken as lifted, so a
  lost lift report cannot leave the recogniser holding a touch.

## Modes, and what moves them

Four states while the glass is lit, plus the dark ones.

- **STATUS**: the status panel or the skin.
- **AUTO**: a new photo showing itself for "Show new" seconds.
- **VIEW**: somebody is browsing; nothing moves by itself.
- **SLIDE**: the slideshow; a step every "Slide s" seconds.

| Event | STATUS | AUTO | VIEW | SLIDE |
|---|---|---|---|---|
| a new photo of a kind that takes the glass | AUTO at 1 | AUTO at the newer 1; the timer restarts | stays; the position shifts, `NEW` marker | jumps to 1 and carries on from it |
| a new photo of a kind that does not | ring only | ring only | the position shifts | ring only |
| swipe or tap, older | VIEW at 1 (swipe only) | VIEW at 2 | one older | one older; the step timer restarts |
| swipe or tap, newer | nothing | STATUS | one newer, or STATUS from 1 | one newer, or STATUS from 1 |
| home plate | | STATUS | STATUS | STATUS |
| play plate | | SLIDE from 1 | SLIDE from here | VIEW here |
| the timer runs out | | STATUS | | one older; after N, 1 |
| 60 s with no touch | | | STATUS | nothing: a slideshow runs until stopped |
| a ring | the ring shows | STATUS, ring | STATUS, ring | STATUS, ring |
| a CONFIG save | STATUS | STATUS | STATUS | STATUS |

- **AUTO's timer starts when the photo is whole on the glass**, not when it
  arrived, so a slow decode never eats the show.
- **"Show new" = stay**: AUTO with no timer. A touch makes it VIEW, with
  VIEW's 60 s.
- **Bursts, Rob's rule**: the newest wins. A photo arriving while another
  is decoding cancels that decode (the job's cancel flag) and starts its
  own. Five callers snapping in ten seconds is five redraws of position 1,
  each held for the full time once it is whole.
- **VIEW is not interrupted by a new photo.** Somebody with a finger on the
  glass is looking at a particular picture; pulling it away is hostile. The
  bar's position shows the shift (`3 of 20` becomes `4 of 20`) and a `NEW`
  chip sits before it until they swipe back to 1. This is the one place the
  spec narrows "always show the latest"; see the decisions.
- **VIEW's 60 s**: long enough to look at a photo and call somebody over,
  short enough that a panel left in the gallery is the status panel again
  before anyone wonders if the board is up.
- **A ring beats everything.** It is the one thing the panel exists to make
  unmissable, and the ring lives in the status header. Any gallery state
  ends at the ring, and the gallery does not come back after it: the ring
  is newer news than the photo.

### Dark: silent, silent hours and sleep

- **Silent** (the switch or the hours): the backlight is off and nothing is
  drawn, as now. A new photo does not light the glass. It goes into the
  ring, and it is there when anyone looks.
- **A touch lights a silent panel for 60 s** (Rob's override). Touch is
  polled while silent, which it is not today. The first touch only wakes,
  on contact, and shows the status panel. Every touch after restarts the 60
  s. Everything works while lit, the gallery and the auto-show included.
  When the 60 s run out the gallery closes and the glass goes dark again.
  The lights, the strip and the activity LED stay dark throughout: the
  override is the glass, not "silent off". A ring still does not light a
  silent panel, as today.
- **Sleep** ("Sleep min", the backlight off after no touch): a new photo of
  a kind that takes the glass wakes the panel for its show, and the sleep
  clock restarts from the show's end. Sleep means "dark while nothing
  happens", and a new photo is something happening; silent means "dark",
  and is kept. See the decisions.
- The 1.47 has no touch, so its silent hours cannot be overridden from the
  desk. As now.

## The layout rule, every board

The photo takes the scarce dimension; the caption takes the free one.

- **Landscape** (`w > h`): one caption row. Bar `R(0,0,w,22)` in `kBar`,
  track `R(0,22,w,2)`, photo box `R(0,24,w,h-24)`.
- **Portrait**: two caption rows, the status header's own geometry, so the
  glass does not jump when the gallery opens. Bar `R(0,0,w,22)`, band
  `R(0,22,w,20)` in `kBand`, track `R(0,42,w,2)`, photo box below. On a
  touch board the photo box stops `plate + 16` above the foot and the
  plates sit in that strip.
- **The photo is letterboxed, never cropped**, on every board: the largest
  rectangle of the picture's own shape inside the box, centred. A motion
  shot's subject is often at the edge that a crop would take, and a photo is
  a record of what the camera saw. Bars are `kBg` black.
- **The track is the timer.** In AUTO and SLIDE, the left part of the 2 px
  track is `kDial`, `w x remaining / total` wide, shrinking to the right,
  redrawn every 250 ms (one 2-row rect, under a band). In VIEW it is plain
  `kTrack`. The dot is the status panel's and does not run in the gallery.
- **Plates**: two, square, at least 8 mm on the glass, 8 px in from the
  sides and the foot. Home bottom left, play bottom right. When the
  letterbox bar is narrower than the plate, the plate sits over the photo's
  corner on a `kSurface` fill: every video player does this and nobody
  reads it as a fault.
- **Chevrons**: 8 x 13, `kFaint`, centred in each letterbox bar that is 24
  px or wider. Left always (newer, or the status); right while there is an
  older photo. Hints, not buttons: the halves are the targets.

## The caption

What it says, in order of importance, and what gives way first.

| Field | Where | Colour | Example | Rule |
|---|---|---|---|---|
| kind icon | x 4, y 3 | snap `kDial`, motion `kWarm`, timelapse `kDim` | | always |
| camera number | x 24 | `kDial`: it is what SNAPSHOT takes | `2` | always when known |
| camera name | after it | `kInk` | `garden` | cut to fit, 16 at most |
| who | two spaces after | `kDim` | `quantumrob` | snaps only; only if 6 or more glyphs fit; never for a hidden or lurking snapper |
| when | right block | `kYellow`, as the status clock: yellow is for a real time | `today 14:32`, `27 Sep 14:32` | always whole |
| position | right end | `kInk` | `3 of 20` | always whole |
| NEW chip | in position's place (AUTO) or before it (VIEW) | `kBg` on a `kLive` fill, 28 x 16 | `NEW` | |

- Landscape: one row. The right block is `when  position` right-aligned to
  the text box's end; the left block gets what is left less 2.
- Portrait: the bar holds icon, number, name and who; the band holds when
  (left) and position (right).
- Shrink order, stop when it fits: who cut to 6, who dropped, name cut. The
  number, when and position are never cut. A cut is hard, no ellipsis, as
  the node rows are.
- Camera unknown (a seeded entry): no number, and the kind word in `kInk`
  in the name's place: `snap`, `motion`, `timelapse`.
- Glyphs: 4.3B 46, MF35 landscape 56, WS2 portrait 26 and 29, 1.47 portrait
  18 and 20; landscape WS2, 1.47 and MF35 portrait 36.

## Mock-ups

One character is 8 drawn pixels across. `#` the bar, `=` the band (portrait)
or the lit timer on the track, `-` the track, `:` the photo, `[ H ]` the
home plate, `[ > ]` play, `[ = ]` pause, `<` `>` the chevrons. Icons: `@@`
camera (snap), `MM` motion, `TT` timelapse.

### Now: the 4.3B's status panel, where the gallery is entered

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
             <-------- swipe left anywhere: the gallery at 1
```

### 4.3B, the gallery (VIEW)

```
col (8 px)   0         1         2         3         4
             01234567890123456789012345678901234567890123456789
  y   3      @@#2 garden  quantumrob######today 14:32  3 of 20#   bar: 46 glyphs from x 24
  y  22      --------------------------------------------------   track
  y  24             ::::::::::::::::::::::::::::::::::::          photo 288x216 at x 56..343, y 24..239
  y  56             ::::::::::::::::::::::::::::::::::::
  y  88             ::::::::::::::::::::::::::::::::::::
  y 120             ::::::::::::::::::::::::::::::::::::
  y 136         <   ::::::::::::::::::::::::::::::::::::   >
  y 168             ::::::::::::::::::::::::::::::::::::
  y 200       [ H ] :::::::::::::::::::::::::::::::::::: [ > ]    plates 40x40 (9.4 mm) at y 192..231
  y 216       [ H ] :::::::::::::::::::::::::::::::::::: [ > ]
  y 232             ::::::::::::::::::::::::::::::::::::
```

### 4.3B, a motion shot showing itself (AUTO)

```
col (8 px)   0         1         2         3         4
             01234567890123456789012345678901234567890123456789
  y   3      MM#3 porch#######################today 03:12  NEW#   the NEW chip in kLive
  y  22      ==============================--------------------   the timer, 6 s of 10 left
  y  24             ::::::::::::::::::::::::::::::::::::          photo 288x216
  y 136         <   ::::::::::::::::::::::::::::::::::::   >
  y 200       [ H ] :::::::::::::::::::::::::::::::::::: [ > ]
  y 232             ::::::::::::::::::::::::::::::::::::
```

In SLIDE the bar reads `TT#1 camera ... today 09:10  7 of 20`, the timer
runs to the next step, and the play plate shows pause, `[ = ]`, its icon in
`kLive`.

### 4.3B, no photos yet

```
col (8 px)   0         1         2         3         4
             01234567890123456789012345678901234567890123456789
  y   3      @@#Photos################################none yet#
  y  22      --------------------------------------------------
  y 104                             @@@@                          the camera icon, 2x, kFaint
  y 136                        No photos yet                      kDim
  y 168                     SNAPSHOT takes one.                   kFaint
  y 200       [ H ]                                               home only: nothing to play
```

### 4.3B, every box (400 x 240 drawn)

| Element | x | y | w | h | Colour |
|---|---|---|---|---|---|
| bar | 0 | 0 | 400 | 22 | `kBar` |
| kind icon | 4 | 3 | 16 | 16 | by kind |
| caption text | 24 | 3 | 368 | 16 | 46 glyphs, per field |
| track / timer | 0 | 22 | 400 | 2 | `kTrack`; `kDial` for the time left |
| photo box | 0 | 24 | 400 | 216 | `kBg` |
| photo, 4:3 | 56 | 24 | 288 | 216 | |
| left bar, right bar | 0 / 344 | 24 | 56 | 216 | `kBg`, 13.1 mm each |
| chevrons | 24 / 368 | 126 | 8 | 13 | `kFaint` |
| home plate | 8 | 192 | 40 | 40 | `kSurface`, 1 px `kRule`, icon `kInk` |
| play plate | 352 | 192 | 40 | 40 | as home; pause icon `kLive` in SLIDE |
| home target | 0 | 184 | 56 | 56 | the plate and 8 px round it |
| play target | 344 | 184 | 56 | 56 | |

### MF35 landscape, the longest caption

A 16-glyph satellite name and a 20-glyph handle, the handle cut at 12:

```
col (8 px)   0         1         2         3         4         5
             012345678901234567890123456789012345678901234567890123456789
  y   3      @@#4 shed_camera_16ch  daytona_on_t##27 Sep 18:02  20 of 20#   bar: 56 glyphs from x 24
  y  22      ------------------------------------------------------------   track
  y  24           ::::::::::::::::::::::::::::::::::::::::::::::::::        photo 394x296 at x 43..436, y 24..319
  y  88           ::::::::::::::::::::::::::::::::::::::::::::::::::
  y 168        <  :::::::::::::::::::::::::::::::::::::::::::::::::: >
  y 232           ::::::::::::::::::::::::::::::::::::::::::::::::::
  y 264      [  H  ]::::::::::::::::::::::::::::::::::::::::::::::[  >  ]   plates 52x52 (8.0 mm) at y 260..311
  y 296      [  H  ]::::::::::::::::::::::::::::::::::::::::::::::[  >  ]
  y 312           ::::::::::::::::::::::::::::::::::::::::::::::::::
```

- Bars 43 px (6.6 mm): narrower than a plate, so each plate sits 13 px over
  the photo's bottom corner.
- Plates `R(4,260,52,52)` and `R(424,260,52,52)`; targets 8 px round them.
- Portrait, 320 x 480: bar, band and track as the portrait rule; photo
  320 x 240 at y 108..347; plates `R(8,420,52,52)` and `R(260,420,52,52)`
  in the strip below.

### WS2 portrait, VIEW and AUTO

```
col (8 px)   0         1         2
             012345678901234567890123456789
  y   3      @@#2 garden  quantumrob#######   bar: 26 glyphs from x 24
  y  24      today 14:32===========3 of 20=   band: when | position, 29 glyphs
  y  42      ------------------------------   track
  y  60      ::::::::::::::::::::::::::::::   photo 240x180 at y 52..231
  y 108      ::::::::::::::::::::::::::::::
  y 156      ::::::::::::::::::::::::::::::
  y 220      ::::::::::::::::::::::::::::::
  y 236
  y 252       [  H   ]            [  >   ]    plates 64x64 (8.1 mm) at y 248..311
  y 284       [  H   ]            [  >   ]
  y 316

col (8 px)   0         1         2
             012345678901234567890123456789
  y   3      MM#3 porch####################
  y  24      today 03:12===============NEW=   NEW in place of the position
  y  42      ==================------------   the timer
```

- Photo box `R(0,44,240,196)`, photo 240 x 180 at y 52, 8 px of black above
  and below it.
- Plates `R(8,248,64,64)` and `R(168,248,64,64)`. Targets `R(0,240,80,80)`
  and `R(160,240,80,80)`. The 88 px between them are nothing: a tap there
  is ignored, not a half.
- The photo halves are `x < 120` and `x >= 120`, `y 44..239`.
- No chevrons: there are no letterbox bars.

### WS2 landscape (320 x 240)

```
col (8 px)   0         1         2         3
             0123456789012345678901234567890123456789
  y   3      @@#2 garden########today 14:32  3 of 20#   bar: 36 glyphs; who does not fit, dropped
  y  22      ----------------------------------------   track
  y  24        ::::::::::::::::::::::::::::::::::::     photo 288x216 at x 16..303
  y 104        ::::::::::::::::::::::::::::::::::::
  y 168      [   H   ]::::::::::::::::::::::[   >   ]   plates 64x64 (8.1 mm) at y 168..231, over the corners
  y 216      [   H   ]::::::::::::::::::::::[   >   ]
  y 232        ::::::::::::::::::::::::::::::::::::
```

The empty state's three lines centre in the box above the plates' tops
(y 24..167 here), never behind a plate.

### 1.47, AUTO only

```
col (8 px)   0         1         2
             012345678901234567890
  y   3      MM#3 porch###########   bar: 18 glyphs
  y  24      today 03:12======NEW=   band: 20 glyphs
  y  42      ============---------   the timer
  y  76                              73 px of black: the letterbox
  y 124      :::::::::::::::::::::   photo 172x129 at y 117..245
  y 172      :::::::::::::::::::::
  y 236      :::::::::::::::::::::
  y 268                              74 px of black
```

Centred in the box, not hung under the header: a photo pinned to the top of
a 320-tall glass with 150 px of black under it reads as a page that did not
finish loading; the same black split top and bottom reads as a letterbox.

Landscape, 320 x 172: one caption row, photo 197 x 148 at x 61..257, y 24.

## Loading, and the three other states

- **Loading.** The caption is drawn at once from the ring, which is RAM. The
  photo box goes black (a person's swipe cuts; the board fades nothing,
  following the status panel's rule that pages a person turns cut). A
  spinner sits in its centre: eight 2 x 2 dots on a 12 px ring, 16 x 16,
  one `kDial` and seven `kFaint`, a step every 100 ms, 256 px a step.
- **Then the photo paints in, top to bottom**, as the decoder hands over
  each row of blocks: the loop copies finished rows into the framebuffer, at
  most one band a tick. It is the picture arriving over a line, which is the
  honest way for a BBS to show one, and it needs no low-resolution second
  pass: TJpgDec's time is the Huffman decode of the whole file, so a quick
  1/8 first pass would cost nearly a second full decode for a smudge.
- **A prefetched photo** (the next one in the direction of travel, decoded
  while the current one is looked at) is shown whole in one go: no spinner,
  no paint-in.
- **Empty**: the camera icon at 2x in `kFaint`, `No photos yet` in `kDim`,
  `SNAPSHOT takes one.` in `kFaint`; the bar says `Photos`, the position
  field `none yet`. Home plate only.
- **No card**: as empty, the second line `Photos live on the SD card.`
  (27 glyphs; the 1.47 and WS2 portrait say `Photos need the card.`).
- **Can't show this one**: the warn triangle at 2x in `kWarm`, `Can't show
  this one` in `kDim`, and why in `kFaint`: `Gone from the card.` or `Can't
  read this one.` (at most 20 glyphs, so the 1.47 holds it). The caption
  stays; a swipe moves on. A gone photo leaves the ring.
- **No camera and no satellite on the board** (`photos::present()` false)
  **and nothing in the ring**: there is no gallery. A swipe at the status
  does nothing, and the Photos sub-page says so in its first row's note.

## Decoding: what runs where

- **The loop**: the touch read, the recogniser, the state machine, the
  caption fields (compare-and-redraw, as the status fields), copying
  finished photo rows into the framebuffer (0.1 us a pixel, the skins'
  figure: a 9,600 px band is about 1 ms), the timer and the spinner.
- **The runner**: one job per photo. Open the file on the card, check it
  with `skin::checkJpeg`, decode with the ROM TJpgDec through
  `plat::jpegDecode`, area-average into the photo buffer, publish rows done.
  Also the `.recent` rewrite and the one-time seed.
- **Scale.** Decode at the largest of 1/1, 1/2, 1/4, 1/8 whose output is
  still at least the photo's drawn size, then area-average down to it,
  streamed, with no scaled copy held: UXGA into the 4.3B's 288 x 216 decodes
  at 1/4 (400 x 300); the OV5640's 2592 x 1944 into the WS2's 240 x 180 at
  1/8. Never enlarged. `plat::jpegDecode` needs a scale argument: TJpgDec's
  `jd_decomp` takes one, and Espressif's `esp_jpeg` README lists the ROM
  build's fixed settings as including "output descaling enabled". Confirm
  it against `esp32s3/rom/tjpgd.h` anyway (the guard refused my read of it).
  Without it, the decode is full size and 4 to 16 times the work: UXGA near
  2 s.
- **Our photos all pass the ROM's rules**: jpge's re-encode (the watermark)
  and the OV2640/OV5640 hardware JPEG are baseline, YCbCr, 4:2:0 or 4:2:2.
  A photo that fails `checkJpeg` gets "Can't read this one", never a crash.
- **Memory, PSRAM**: the photo buffer, allocated when the gallery or an
  auto-show opens and freed 30 s after the status is back, so a quick
  return is instant.

| Board | Photo buffer | With the prefetch | Board PSRAM |
|---|---|---|---|
| 4.3B | 124,416 B | 248,832 B | 8 MB |
| MF35 v1.0 | 233,248 B | 466,496 B | 2 MB: no prefetch while a skin loads (two skins are 600 KB then) |
| WS2 | 86,400 B | 172,800 B | 8 MB |
| 1.47 | 44,376 B | no prefetch: no browsing | 8 MB |

- **Heap during a decode**: TJpgDec's 5 KB work area and one row of blocks
  of accumulators (photo width x 3 channels x 4 bytes x up to 16 rows),
  under 16 KB at the MF35's 394, freed when the job ends.
- **Static DRAM** (S3 display profiles only): the ring 1,920 B, the
  gallery's state and the recogniser under 128 B, the six settings 8 B.
  About 2 KB. The S3 is at 253,872 of 341,760.
- **Flash**: about 5 KB of code (the recogniser, the states, the scaler, the
  layout), five 16 x 16 icons and the chevron, 186 B. No font. The decoder
  is in ROM.
- **The loop, measured on the bench before 1.2.0 ships**: SYS's slow passes
  and loop average, with a slideshow at 5 s and two callers listing files,
  against the same without the gallery. The 4.3B's GT911 poll at 20 ms is
  about 250 us a poll while a finger is down, 1.25% of the loop for the
  length of a touch.

## Skins

- The gallery covers whatever is up: the status layouts, and on the MF35
  the status skin or a machine skin. It draws into the same framebuffer.
- While it is up, the status keeps gathering its figures twice a second
  (RAM) and draws nothing: no dot, no fade, no LED row, no skin lamps, no
  strip. The lights plugin keeps computing; only the glass stops.
- Leaving: the field compare cache (`g_shown`) is invalidated and the status
  is drawn whole, or a machine skin is copied back from its PSRAM block, the
  same path silent's end takes today. The glass shows the photo until the
  status bands replace it top to bottom: 0.2 s on the 1.47, 0.8 s on the
  MF35. A top-down wipe, the same as silent ending.
- The WS2's recent row for a caller's snap (`EV_SNAP`) and its band's camera
  glyph stay as specified: text, not pictures, and not governed by the
  privacy switch.

## Privacy: "Photos on the panel"

- One yes/no on the Photos sub-page. **No**: no picture ever reaches the
  glass. No gallery (a swipe does nothing), no auto-show, no slideshow. The
  ring is still kept, so turning it back on shows the last 20 at once.
- **Default yes.** The panel is the sysop's own glass, the cameras are the
  sysop's own cameras, and nothing on it is more private than the Photos
  area it reads, which callers already browse at the camera's levels. A
  display feature shipped off is a feature nobody finds. The room-facing
  board is the exception, and it is one row to turn off.
- Say it where the sysop sets up a camera: the note on the row, and a line
  in the camera and satellite docs, "New photos show on this board's
  display; CONFIG panel, Photos, turns that off."
- The panel does not apply the Photos area's read level. The glass is the
  sysop's; the switch is the control.

## CONFIG panel

### The main page, after

Rows after the core four (Enabled, Read, Write, Admin). Buttons marked `>`.
Existing keys keep their names; only where they are listed changes.

| Board | Rows | Count |
|---|---|---|
| 1.47 | Photos >, Bright %, USB plug, Glass >, Pins > | 5 of 12 |
| WS2 | Photos >, Bright %, Sleep min, USB plug, Glass >, Pins > | 6 of 12 |
| MF35 | Skin, Photos >, Bright %, Sleep min, USB plug, Glass >, Pins > | 7 of 12 |
| 4.3B | Driver, Photos >, Light, Sleep min | 4 of 12 |

The order is by how often a sysop changes it: the look (Skin), what shows
(Photos), how bright, when dark, which way up, then the wiring nobody
touches after the first day.

| Key | 40: label (9) | 80: label (20) | Kind | Button text (setting()) |
|---|---|---|---|---|
| `photo` | `Photos` | `Photos and new ones` | PS_PAGE | `off`, `on, new: 10 s`, `on, new: stay`, `on, new: none` |
| `glass` | `Glass` | `Glass and bus` | PS_PAGE | `ST7789 172x320` (driver and size) |

- `photo` notes: 40 `On, off, and what takes the glass.` (34); 80 `Whether
  photos show here, which new ones take the glass, and the slideshow.` (75).
- `glass` notes: 40 `Size, offsets, colours and the clock.` (37); 80
  `The glass's size, offsets, colour order, mirror and the bus clock.` (66).
- **The Glass page** holds, with `page = "glass"` and their keys unchanged:
  Driver (PS_INFO, moved from the 1.47's main page), Width, Height, X
  offset, Y offset, Invert, Mirror, Colours, SPI MHz (the MF35's bus clock
  row). Nine rows. Not on the 4.3B, whose RGB wiring is the board's.
- `PANEL` still names the driver, as it does on the WS2 today.

### The Photos page

All keys under `[plugin:panel]`, collected by the existing prefix rule.
A sub-page shows only its own rows, so there is no core-row cost.

| Key | 40: label (9) | 80: label (20) | Kind | Choices, range | Default | 40 note (38) | 80 note (78) |
|---|---|---|---|---|---|---|---|
| `photo_on` | `On panel` | `Photos on the panel` | PS_YESNO | | yes | `No: no photo ever shows on the glass.` | `No keeps every photo off this glass: no gallery, and new ones never shown.` |
| `photo_show` | `Show new` | `Show new photos for` | PS_CYCLE | `off\|5\|10\|30\|60\|300\|stay` (touch), `off\|5\|10\|30\|60\|300` (1.47) | `10` | `Seconds a new photo stays up.` | `Seconds a new photo holds the glass, then status; stay: until touched.` |
| `photo_snaps` | `Snaps` | `New SNAPSHOTs` | PS_YESNO | | yes | `A SNAPSHOT, a caller's or yours.` | `A SNAPSHOT from any camera, built in or a sat, a caller's or your own.` |
| `photo_motion` | `Motion` | `New motion shots` | PS_YESNO | | yes | `A shot a motion sensor set off.` | `A picture a motion sensor set off, on any camera.` |
| `photo_timelapse` | `Timelapse` | `New timelapse frames` | PS_YESNO | | no | `Each timelapse frame as it comes.` | `Every timelapse frame as it arrives: often, so usually no.` |
| `photo_slide` | `Slide s` | `Slideshow, seconds` | PS_CYCLE | `5\|10\|20\|30\|60` | `10` | `Seconds a photo shows in a slideshow.` | `Seconds each photo stays up in a slideshow, started with the play button.` |

- Six rows on a touch board, five on the 1.47 (no Slide s). Every label is
  9 or fewer at 40 and 20 or fewer at 80; every note fits 38 and 78. The
  one that was over, `photo_show`'s 80-column note, is cut to fit above
  (70).
- **Why three yes/no rows and not one choice.** CONFIG has no multi-choice
  kind, and a cycle through the seven combinations of three kinds is seven
  presses to reach "motion only" on a C64. Three rows read at a glance on
  every terminal, plain ASCII's questions included.
- **Why defaults of snaps and motion yes, timelapse no**: a snap is somebody
  wanting to be seen, a motion shot is the thing a sysop set a sensor for,
  and a timelapse every minute would make the panel a photo frame nobody
  asked for.
- On a board with no camera and no satellite, `photo_on`'s note says `No
  camera yet: pair a sat or add one.` The rows still save, so a satellite
  paired later works with what was set.
- **The two fixed times are constants, not rows**: VIEW's 60 s idle and the
  silent override's 60 s. Rows can follow if a sysop asks.

## PANEL

Three lines appended to its report, so the host tests read the gallery
without a glass:

```
Photos on, 20 kept, new ones 10 s: snaps motion
Showing 3 of 20 (view), camera 2 garden, today 14:32
Touch seen, 214 taps, 31 swipes, last at 188,104
```

`last at x,y` is also the bench's way to check each board's touch
orientation against its "USB plug" setting.

## Icons

16 x 16, 2 px pen, top bit left, the house rule. The camera icon is the
WS2 spec's `kIconCamera`; timelapse is revision 0's `clock`; the warn
triangle is the status glyph at 2x.

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

kChevronL (8 x 13; kChevronR is its mirror)
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

- Motion is a sensor firing: a dot and two pairs of arcs, `kWarm`. It is
  16 x 16 and appears only in the gallery's bar, so it never sits beside the
  band's 9 x 10 announce tower it resembles.
- The house has a solid door, so no stroke under 2 px.
- Play is filled: an outline triangle at 16 px reads as an arrow, which on
  a glass with chevrons is the wrong word.

## Hand-backs for the builder

- **`photos::` ring** (core, S3 display profiles): `Recent`, `filed()`,
  `recent()`, `recentCount()`, `recentSerial()`, `.recent` on the card,
  the seed job. The camera and camsat call `filed()` from `finish()` with
  the camera's number (`numberOf`), name, kind, handle and whether the
  snapper was hidden or lurking.
- **The name collision** (link lane): a taken name files as
  `...-<camera number>.JPG`; `nameKey` accepts the suffix.
- **`PluginSetting::page`** (core, appended with a default of nullptr);
  `pageOwner` checks it before the prefix; the panel's geometry rows get
  `page = "glass"` on the ST7789 and i80 boards.
- **Touch positions**: `plat::touchPoll(bool& down, int16_t& x, int16_t& y)`
  in drawn pixels and the current orientation; the GT911 read widened to 6
  bytes; the WS2's CST816D read on INT; an FT6236 driver for both MF35
  revisions (`BBS_HAS_TOUCH`, I2C 38/39, INT 40, 0x38, the pins in
  `BBS_PINS_ONBOARD`). If a sysop gives the serial bridge J2's lines, touch
  is off and the MF35 keeps its auto-show.
- **Touch while silent**: polled, with the 60 s panel-only override.
- **`BBS_LCD_UM_PER_PX`** per display profile: 234, 153, 127, 103.
- **`plat::jpegDecode` with a scale**, after confirming `JD_USE_SCALE` in the
  ROM build.
- **`runner::quiet()`**: nothing queued, nothing running.
- **The panel**: the four states, the recogniser as a pure function
  (`gesture::classify(down, last, maxDist, ms, mmPerPx)`), the layout from
  `layout()`'s width and height, status drawing suspended while the gallery
  is up, `g_shown` invalidated on the way out, PANEL's three lines.
- **The host has no touch**: a host-only `<data>/touch.txt` read by the
  host's `touchPoll` (lines `down x y`, `up`, with a delay), the same shape
  as `hostio.txt`, so a test can swipe.
- **Tests**, for when Rob okays a run: the recogniser at every boundary per
  board (slop, swipe length, 2:1 direction, 400 and 800 ms, the lost-lift
  300 ms); every gallery box inside its glass and no two overlapping except
  plates over a photo where the bar is narrower; the ring (20, newest
  first, one timelapse slot a camera, a gone photo dropped, `.recent` round
  trip, the seed's order by `nameKey`); every cell of the state table; the
  scale choice for VGA, XGA, UXGA and 2592 x 1944 into each photo size; the
  caption's shrink order at 18, 26, 36, 46 and 56 glyphs; the CONFIG pages'
  row counts per board and every label and note against 9, 20, 38 and 78.

## What stays as it is

- **Every status layout, pixel for pixel.** No chip, no region, no new
  field on the status glass. The swipe needs no pixels.
- **A tap on the status panel turns the header's page**, and the 4.3B's
  10 s hold on a tapped page. Only its moment moves, from contact to lift.
- **The ring rule**: a live ring shows, whatever else was up.
- **Silent keeps every light dark**: the override lights the glass only,
  and a ring does not light a silent panel.
- **The palette and its meanings**: dial for what you can act on (the
  camera number), yellow for a real time, live for new, warm for attention,
  the three greys for weight. No new colour.
- **8 x 16 Spleen only.** No third face: the caption is arm's-length text,
  and the photo is what carries across a room.
- **The 4.3B's 2x picture**, until a measurement says otherwise.
- **The WS2's snap event and camera glyph.**
- **The 4.3B review's principle**: a touch changes what the glass shows,
  never what the board does.

## Implementation order

Cheapest and most visible first. Each step is on a glass on its own.

- The `photos::` ring, `filed()` from both cameras, `.recent` and the seed.
  No glass yet, and everything after needs it.
- The auto-show on the 4.3B: its CONFIG page has room, and it is on Rob's
  desk. Layout, the decode job with the scale, paint-in, the timer on the
  track, the NEW chip, the Photos page. No touch position needed.
- GT911 positions and the recogniser. VIEW: swipes, the halves, the home
  plate, the chevrons, empty, no card, can't show.
- SLIDE: the play plate and Slide s.
- Touch through silent, the 60 s override.
- `PluginSetting::page` and the Glass page on the ST7789 boards. The WS2's
  CST816D positions; the WS2 gallery and the 1.47 auto-show.
- The FT6236 driver; the MF35 gallery over the status skin and the machine
  skins.
- The prefetch, once the runner's quiet rule is in and the bench has read
  SYS with a slideshow running.
- The name-collision fix rides with the link lane, whenever it lands; the
  gallery does not wait for it.

## Summary

- One model on every glass: status at 0, photos 1 to 20, left newer, right
  older, a swipe or a tap on a half moves one.
- Two plates, home and play/pause, 8 to 9.4 mm, bottom corners.
- The caption takes the header's place; the track under it is the timer.
- Letterbox, never crop. Paint-in top to bottom while the runner decodes.
- New photos take the glass for 10 s by default, snaps and motion only.
- Viewing is not interrupted; the auto-show and slideshow jump to the latest.
- Silent never lights; a touch lights the glass alone for 60 s.
- 1.47: auto-show only. BOOT stays off photos.
- Owed first: a photo ring that knows its camera, a Glass sub-page, touch
  positions, a decode scale.
- Found on the way: two cameras in one second lose a photo.

## Decisions for Rob

- "Photos on the panel" default: **yes**; the room-facing board turns it off.
- "Show new" default: **10 s, snaps and motion yes, timelapse no.**
- Ring size: **20**, with **one timelapse slot per camera** holding its newest frame.
- A new photo while somebody is browsing: **do not jump; show a NEW marker.** Auto-show and slideshow jump.
- A new photo on a sleeping panel: **wake it for the show**; never a silent one.
- The silent override: **60 s from the last touch, glass only, a constant.**
- VIEW's idle timeout: **60 s, a constant.**
- The 1.47: **auto-show only; BOOT is not a photo button.**
- CONFIG panel's room: **`PluginSetting::page` and a Glass sub-page** (about ten lines of core, no key renamed).
- The 4.3B: **photos at the drawn 400 x 240**; native resolution only after SYS says the interrupt can afford it.
- The snapper's handle in the caption: **yes**, never for a hidden or lurking caller.
- The same-second name collision: **fix it in the link lane** with a camera-number suffix on a taken name.

Sources:

- [CST816D datasheet (Waveshare's copy)](https://files.waveshare.com/wiki/common/CST816D_datasheet_En_V1.3.pdf): register 0x01 gesture codes, 0x02 to 0x06 finger count and position.
- [FT6X36 datasheet (FocalTech, BuyDisplay's copy)](https://www.buydisplay.com/download/ic/FT6236-FT6336-FT6436L-FT6436_Datasheet.pdf): TD_STATUS and P1 registers.
- [FT6x06 application note (Adafruit's copy)](https://cdn-shop.adafruit.com/datasheets/FT6x06_AN_public_ver0.1.3.pdf): the same register family.
- [esp_jpeg README (Espressif)](https://github.com/espressif/idf-extra-components/blob/master/esp_jpeg/README.md): the ROM TJpgDec's fixed settings, output descaling enabled.
- GT911 registers as already cited in `tty-ux-panel-ws43b-2026-09-28.md` (0x814E status, 0x8150 to 0x8153 point 1).
