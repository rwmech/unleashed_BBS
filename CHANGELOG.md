<!--
 ===========================================================================
  µnleashed BBS
  Electronic freedom on a microcontroller.
 ===========================================================================

 File:         CHANGELOG.md
 Module:       Documentation / history

 Purpose:      Every released build, newest first: what changed, when, and
               whether it has run on real hardware.

 Audience:     Anyone picking the project up, and the next build's planning.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v3 or later
 SPDX-License-Identifier: GPL-3.0-or-later
 ===========================================================================
-->

# Changelog

Every released build of µnleashed BBS, newest first. Versions are `MAJOR.MINOR.PATCH`; while the core is being built each minor number is one flashed build. Dates are the day the work landed in the repository.

A build is only marked **on hardware** once it has run on a real ESP32-WROOM-32E with a caller connected. Everything else is host-tested through `tools/testclient.py`.

## 1.2.1 (in development)

Host-tested only so far; the board versions move when their own code does.

Merged in 1.2.1-dev.9 (rel-1.2.1): the core lane (dev.1 to dev.8), the
forums lane (forums.1 to forums.5) and the Makerfabs v2.0 board lane. The
boards it moved have their versions bumped in 1.2.1-dev.10.

### A new photo on the panel (1.2.1-photo.1, lane D; S3 1.1.6, WS43B 1.0.4, MF35 1.1.4, MF35V2 1.0.1, WS2 1.0.5)

Built and sized, not yet tested on the host or a board.

- **A photo shows on the display the moment it is filed** (Rob, 2026-09-30:
  "Snap happens, show pic on screen for 1 minute, if a new one comes in
  update, after 1 minute stop"). On every board with a panel, a picture
  filed in Photos by any camera, the built-in one or a camera sat, takes the
  glass for a minute: the whole picture, fitted inside the glass and never
  cropped or enlarged, on a slate mat with a thin black keyline, `PHOTOS
  NEW` (or `TIMELAPSE NEW`) across the top, a violet strip under it that
  shrinks as the minute runs out, and a caption: the kind's icon, the
  camera's number and name as `SNAPSHOT` takes them, the caller who took a
  snap, and when (`today 14:32`). A newer photo replaces it and starts the
  minute again; while the newer one is being decoded the one on the glass
  stays, so the glass never drops back to the status panel between two. A
  minute after the last, the status panel (or the skin) comes back, drawn
  whole. A tap on a touch board ends it early. A ring, or the board shutting
  down, ends it at once and has the glass; the photo does not come back
  after. Silent mode shows nothing, and a photo filed while silent is not
  saved up for when it ends. A board whose panel Sleep has put it dark wakes
  for the photo and goes dark again its Sleep minutes after the show.
- **What each glass draws**: the Waveshare LCD-1.47 portrait 172 x 320 a
  4:3 photo at 162 x 121 with the caption on two rows (camera and who, then
  when), landscape 320 x 172 at 157 x 118; the Touch-LCD-2 240 x 320 at
  230 x 172; the 4.3B at its drawn 400 x 240, 248 x 186; both Makerfabs at
  480 x 320, 354 x 266. A 480 x 480 glass (the G4848, in its own lane) is
  framed by the same rule at 456 x 342, the caption under the picture like a
  label on a print.
- **CONFIG photos** gains four rows on a board with a display, after the
  others so none moves: **New photos on panel** (`photos_show`, yes),
  **Show new snaps** (`photos_show_snaps`, yes: a caller's or your own
  `SNAPSHOT`, any camera, a sat's included), **Show motion shots**
  (`photos_show_motion`, yes) and **Show timelapse** (`photos_show_tl`, no:
  a frame a minute would hold the glass for ever). Read at each new photo,
  so a save is live. The WROOM, the Freenove, the ESP32-CAM and the ETH
  board have no panel and no rows; there the keys are unknown and ignored.
- **`PANEL`** says it in two lines above `bands sent`: `Photos on, snaps
  motion: showing, 41 s left` (or `loading one`, or `3 shown`, or `Photos
  off`), and `Last photo 1024x768 at 1/2, 412 ms`, or why the last one was
  not shown. The console logs each decode: its size, the scale, the time,
  the PSRAM it took and the runner's spare stack.
- **Rule no. 1.** The card read and the decode are one job on the
  background runner: the ROM's JPEG decoder at the largest of 1/1, 1/2, 1/4
  and 1/8 still at least the drawn size (`plat::jpegDecode` takes a scale
  now), then an area average down to it, streamed through a ring of 24
  accumulator rows. The picture and the ring are PSRAM, the ring for the
  decode only and the picture until it is in the framebuffer: 39 to 188 KB
  of picture and 30 to 68 KB of ring on today's boards (Makerfabs: 256 KB at
  most, beside the 300 KB framebuffer). The loop draws the frame 32 glass
  rows a pass and queues the glass once, sent a band a pass as always. The
  decode stays out of everybody else's way on the runner too: it is not
  posted while a camera is taking or bringing in a picture (the runner is the
  camera's worker and a sat's sink), it is called off and posted again if one
  starts, and it steps aside at the next row of blocks whenever another job
  is queued behind it (a FILES page, a forum walk), four times at most before
  it runs to the end (`runner::waiting`, display boards only). A newer photo
  waiting holds the one on the glass past its minute, three minutes at most.
- **For the swipe gallery later**: the frame, the fit, the scaler and the
  decode job are its own (`src/plugins/panel_photo.h`); only stepping
  through the card and the touch positions are left to build.
- Under it: `photos::lastFiled` and `filedSerial`, display boards only (the
  photo just filed by any camera, its kind and camera, one record under the
  runner's lock), and `photos::Writer::camera`, appended, for a camera to
  name itself: the built-in camera does (the Touch-LCD-2); camsat does not
  yet, so a sat is told by which one is busy as its photo is taken in, or by
  being the only one.
- **Sizes (1.2.1-photo.2, off the ELFs, against dev.10):** static DRAM
  +1,064 on the LCD-1.47 and the 4.3B, +1,072 on the WS2 and both
  Makerfabs: free LCD-1.47 78,672, 4.3B 76,080, WS2 65,040, MF35 79,184,
  MF35V2 73,928. Images: 1,499,088, 1,515,840, 1,588,096, 1,509,856 and
  1,521,968 bytes (release builds). The display envs and esp32dev build
  with no warnings. **Boards without a display are unchanged:** everything
  new in shared files is inside `BBS_HAS_LCD`; esp32dev and its release
  build, checked against the same tree before the change, have all 60
  application objects identical section by section and the same static
  DRAM (15,048 free, as at dev.10), and the image moves only by link
  relaxation (-64 bytes on esp32dev, 0 on its release). The Freenove,
  ESP32-CAM and ETH were not rebuilt.
- **On the bench (2026-10-01, the Touch-LCD-2 on its own hub, the photo.3
  release image).** A snap at each of the camera's seven sizes showed on the
  glass, every one decoded and framed, with no refusal and no photo missed.
  Decoded on the runner, off the loop: 640x480 at 1/2 in 427 ms, 800x600 at
  1/2 in 621 ms, 1024x768 at 1/4 in 895 ms, 1280x720 at 1/4 in 1,009 ms,
  1280x1024 at 1/4 in 1,719 ms, 1600x1200 at 1/4 in 2,591 ms and 2048x1536
  at 1/8 in 1,651 ms. The picture took 103,500 to 128,800 bytes of PSRAM
  (the fit's own figures: 230x172 for 4:3, 230x129 for 16:9, 230x184 for
  5:4), the runner had 4,088 bytes of stack spare at every decode, and
  internal heap low moved 44 bytes across the whole run (15,319 to 15,275).
  **Rule no. 1 holds:** two slow passes happened during the run and neither
  is the show's. Both are 118 ms with 105 ms of it inside two file opens, in
  the session phase with the sysop in CAMERA, which is `CAMERA SET` rewriting
  system.cfg; the nearest decode had finished 1.7 s earlier and no decode
  appears in any loop pass. That is the write-side-on-the-loop class the
  1.1.2 audit left knowingly, beside the Freenove's 414 ms CONFIG save.
- **Tests (1.2.1-photo.3)**: `host/test_panel_photo.cpp` (`make test`) checks
  the frame on all seven glasses, the decoder's scale for every camera size,
  the caption and the scaler against the host's TJpgDec at all four scales,
  on JPEG fixtures in `host/photos/` (made by its `mkfixtures.py`).
  `test_panel_photo_show` (the Touch-LCD-2 profile) snaps through a copy of
  the board whose stub camera gives a real JPEG (`BBS_CAM_HOST_JPEG`, the
  host only) and reads PANEL's photo line. The viewer's pure half moved into
  `src/plugins/panel_photo_fit.h` for the unit test; no behaviour moved.

### Login and the core (1.2.1-dev.1 to dev.8)

- **SyncTERM's autologin reaches the sysop node** (1.2.1-dev.1). Alt+L on a
  telnet entry sends the handle, the password and the entry's system
  password in one burst. The sysop's own account is asked `Sysop password:`
  at login, and until now the question dropped every key held from before
  it, so the system password was thrown away and the autologin stopped
  there. A line already held when the question appears is taken as the
  answer, and a right one elevates exactly as a typed one does. A wrong held
  line is a skip: nothing is said, the stars are rubbed out, and it is never
  counted toward the address's ban, because what was held may be a command
  typed straight after the password. Only a line typed after the question
  counts, as before. An uncounted guess needs a limit of its own, or the
  burst would be a way round the ban for anybody holding the sysop's account
  password: one held answer an address a ban window (15 minutes), after which
  held keys are dropped as they were and the question waits. A right staff
  password clears it; an empty held line or ESC gives it back. Costs no
  static RAM (both new fields sit in padding).

- **A held Enter alone does not answer the sysop question** (1.2.1-dev.2,
  from the code review of dev.1). A double Enter at the password, a
  client's CR LF (over SSH there is no telnet filter to eat the LF), or a
  SyncTERM entry with an empty system password that still sends its CR
  skipped the question and left the sysop at Main needing BYE. Held line
  endings are dropped now and the question waits for a typed answer, with
  the held try unspent. And, as it stands: a wrong stored system password
  is ignored once; within 15 minutes the question then waits for you to
  type it.

The 1.1.3 queue and the 1.2.0 full run's findings (1.2.1-dev.3):

- **`/sq 10` hides node 10.** The room read one character of a line's
  `#n:` tag, so a line from node 10 was node 1's: `/sq 10` hid nothing and
  `/sq 1` hid node 10 as well.
- **CONFIG's core pin rows reach GPIO 40 to 48 on the S3s** (the LED and
  the backup button stopped at 39). They follow the chip's highest pin, so
  the ESP32 images are unchanged; `pinProblem` still refuses what each
  profile owns.
- **The VFS table is 12 on every S3**, in the shared S3 layer
  (`sdkconfig.defaults.esp32s3`) rather than four boards' own, and the
  LCD-1.47 gets it too: with SSH and a card an S3 was at 8 of 8. One board.h
  guard refuses an S3 image built without it. The card's out-of-memory
  message names a full VFS table as well as memory.
- **FILES opens areas past 10 by number.** A digit that could start a
  longer number the caller can open (1, with Photos at 12) waits for the
  rest and Enter; one that cannot opens its area at once, as before. Plain
  ASCII reaches Photos and Timelapse by number. `#` still works.
- **The camera's first snap after a boot is the size CONFIG saved.** Its
  size was chosen from the board's list before any sensor had answered, so
  an OV2640 on the Freenove came up at VGA with UXGA saved; the worker now
  brings it up again at the size the sensor gives, once a boot at most.
- **The first timed shot after a boot is not lost.** It fell due before
  the heap had settled and was refused for memory, and with it that slot's
  picture. A timed shot is held now: none in the board's first 30 s, then
  tried once a second for up to a minute (or half the interval), then
  dropped with a line on the console.
- **SSH offers only the ciphers it has** (S3): wolfSSH's list named
  aes192-gcm and aes192-ctr, which this build of wolfCrypt leaves out.
- Tests: `test_doors` waits for the second restart's door list,
  `test_board_ws2` runs on the real clock, `test_sats` reads the sat's name
  from SATS, the skin unit test's GCC 13 warning is gone, `plugins.lock`
  pins camsat v1.1.0, the host's S3 profiles refuse 43 and 44 as the
  console, and the shared CONFIG pin tests read the profile's pins
  (`PIN_BOARD`). New: `test_room_squelch_ten`, `test_files_typed_number`.
- From the code review of dev.3 (1.2.1-dev.4):
  - FILES: Logs (10) no longer counts as a longer number, so a co-sysop
    with areas 1 to 10 gets area 1 on the key again.
  - CONFIG refuses UART0's pins as "the console port" only where the
    console is UART0: the ESP32 boards and the Makerfabs. The Waveshare S3s
    run their console on the chip's own USB, and the 4.3B's RS485 bridge
    ships on 43 and 44, which CONFIG serial refused as its own console.
  - The camera's held timed shot retries without walking the heap from the
    loop, says a worker that would not start once rather than every second,
    and goes when the timelapse is switched off.
- From the review of dev.4 (1.2.1-dev.5): a held timed shot the worker
  refuses for memory (the largest block, which the free counter cannot see)
  keeps its hold inside its minute, and the retries weigh the whole heap
  every 5 s of the wait rather than never. COMMANDS.md says that GPIO 43 on
  the Waveshare S3s carries the boot messages at every reset.
- Every caller-visible line 1.2.1 added or changed fits 39 columns at 40:
  the card's out-of-memory reason is `no memory or VFS table full: see MEM`
  (36, shown indented two by SD), and `/sq` answers
  `Node 10 hidden. Joins, leaves show.` or `Node 10 back.` at 40 (39 and 17
  with the marker, where the 80-column sentence is 51).
- Tests only (1.2.1-dev.6, from the targeted run on dev.5): `test_board_ws2`
  asserted the two bugs dev.3 fixed (the LED row stopping at 39, and 1 and
  3 as the console on every host profile) and now expects GPIO 46-48 and 3
  refused as the board's own; `test_config_serial_rows` read the dot-filled
  rest of an 80-column box as part of the value.
- **A stamp taken after the pass's clock read** (1.2.1-dev.7, the sweep after
  the forums lane's serviceWait fix, below). A time stamped from `plat::millis()` part way
  through a pass, or on another task, is ahead of the `now` read at the top
  of the pass, and an unsigned `now - stamp` then reads as 49 days. Made
  signed where it could happen:
  - Photos: every FILES.BBS tidy a prune handed back was dropped at once
    as "not made" (the tidies' minute was stamped in the same tick).
  - Doors: a close begun from a key gave up at once rather than retrying
    CLOSE for 5 s.
  - The panel (touch boards): a CONFIG save put the glass to sleep straight
    away with Sleep set, and turned the header a page.
  - The link radio: a send failure landing between the rate check's clock
    read and its load counted as 30 s clean, and the rate went back up.
  - The ban window (the sysop question's held try, stamped mid-pass) could
    read as expired and give the try back.
  - WHO and NODES could show 49 days idle for a caller who had just typed;
    the drive light and a skin's node lamps could miss a frame; the SSH
    task could take one pass without waiting; the backup window's idle
    check (safe today, its held states skip it).
- Tests only (1.2.1-dev.8): on the WS2, which ships no bridge pins,
  `test_config_serial_rows` took the board's "Saved, not running" for RX on
  43 as a refusal, then left RX on 43 and expected the baud save to be
  live. It accepts that answer now and puts RX back to the profile's -1.
  The WS2's board.h comments said CONFIG kept 43 and 44 as the console; it
  has not since dev.4 (the console is USB), and they say so.
- **One rule for elapsed time, `plat::since(now, at)`** (1.2.1-dev.8, the
  review of dev.7). The signed differences dev.7 used fixed a stamp a moment
  ahead of now and broke the other way: any real gap past 24.8 days turned
  negative. One storage error, then 24.8 quiet days, and the drive light
  (and a skin's lens) blinked red for 24.8 days; a ban entry of one or two
  wrong passwords stopped ageing out. `since` is unsigned with a 65 s skew
  allowance: a stamp up to that far ahead is 0, and a real gap keeps the
  clock's 49.7 days. Every site dev.7 touched uses it. Unit test
  `host/test_since.cpp`, in `make test`.

### The forums (1.2.1-forums.1 to forums.5)

Forum phase 0 from `internal/fidonet-zmodem-2026-09-29.md`: worth doing
without FidoNet, and what FidoNet stands on.

#### 1.2.1-forums.1, 2026-09-30, built, not yet tested

- **A long message reads whole.** A body over 1,728 bytes could not be
  shown: the reader read it into a 1,729-byte buffer on the stack. It reads
  the card a window at a time now, so any body the format can hold (9,999
  bytes, its four-digit length field) is shown. Writing a post is unchanged:
  32 lines and 1,536 characters.
- **A message is drawn a row at a time**, through the same list machinery
  as every other list on the board, instead of all at once into the
  caller's 3 KB output buffer. A slow terminal is paced rather than
  overrun, and **a message longer than the screen stops at
  `[More] Y/n/c`**, the way the lists do; `C` reads the rest without
  stopping, `Q` stops (the message still counts as read).
- **A post opens one body segment.** Every post opened each full 128 KB
  body file from `M0000.TXT` up to find the one with room, so a busy forum
  cost more card opens with every 128 KB it held. The forum's header now
  names the current segment (`seg=` in `INDEX.TXT`'s record 0; the index
  format itself is unchanged). A forum from before 1.2.1 is looked through
  once, at its first post, and the answer written down. Older firmware
  reads the new header and ignores the key.
- **Every forum write goes through one queue on the background runner**:
  posts, removals, read pointers and a new forum's header. The loop never
  writes a forum file, so a post costs other callers nothing, and the
  FidoNet tosser planned for 1.4.0 joins the same queue rather than being a
  second writer (the card's FAT files have no locking between tasks). A post
  is confirmed once it is on the card, usually in a pass or two, with the
  spinner after a quarter of a second; behind a long runner job (a camera
  snap) it can take seconds, and after 10 s `ESC` hands the caller back
  while the post still lands.
- Fixed on the way: a read pointer write that failed to open `PTRS.TXT`
  fell back to creating it afresh, which on a card that refused one open
  would have emptied every caller's read pointers; now only a file that is
  not there is created. The same for a forum's header. A forum whose header
  is unreadable gets one that counts the records the index holds, rather
  than `newest=0`, which would have put the next post over message 1.
- Fixed on the way: a forum or subject list whose closing prompt was the
  row that filled the screen drew `[More]` under the prompt.

#### 1.2.1-forums.2, 2026-09-30, built, not yet tested

From the code review of 1.2.1-forums.1:

- **A rebuilt header counts the live messages.** A forum whose header is
  torn got one whose count was the number of records, which forgets
  removals, so every caller was told the removed messages were unread. The
  count is read from the records' live flags now. A header that simply
  failed to read is left alone and the error logged: only 128 bytes without
  the forum's magic, or a file shorter than a header, is rebuilt.
- **A removal is reported as done once it is**: when the message's flag is
  on the card but the header's count would not save, the moderator was told
  "That did not save". It says removed now, and the header is recounted
  from the records in the background so the card agrees.
- **A `seg=` past M9999 in a header is ignored** and the post looks for its
  segment, rather than writing the body to a file its record could not
  name.
- A record or a removal flag whose sync to the card fails no longer has the
  header written after it.
- A message's `[More]` never stops with only the reading question left.

#### 1.2.1-forums.3, 2026-09-30, built, not yet tested

From the code review of 1.2.1-forums.2:

- **A header is believed only if it fits its file.** `newest=` and
  `count=` must be there and be numbers, `newest` must name a record the
  index holds, and `count` cannot be more than `newest`. A missing or
  garbled `newest` read as 0, so the next post went over message 1; a huge
  one sent the post far past the end of the file. Anything else is taken as
  torn and rebuilt from the records, now also when a post finds it.
- **No page of a message is taller than the screen.** Every row the reader
  draws is one line, so the pager's count is the screen's: two rows drew two
  lines each, and a page could scroll an unread line off the top of a
  24-row screen. The end of a message (the blank, `--> EOM <--`, the blank
  and the question) stays together on one page.
- A post whose header will not save has that header recounted from the
  records in the background, as a removal's already was, so a header left
  torn does not wait for the next post to be rebuilt.
- The second try at a header that failed to save goes through a fresh
  open of the file: the card's file system keeps a write error on the file
  it happened to, so a retry on the same one could never work.
- The "rebuilt the header" line is logged after the header is on the card.

#### 1.2.1-forums.4, 2026-09-30, built, not yet tested

- **A post whose header did not save is told it may not have**, not that
  it did not: the background recount can make it readable after all, and a
  caller told "did not save" posts it again. `--> That may not have saved.
  Look before posting it again.` (at 40 columns: `--> It may have saved.
  Check first.`).
- **Every line the forums' new answers print is one line at 40 columns**
  (Rob: "esp wordwrap related"): the long form where it fits the row, a
  short one of 39 columns or fewer where it does not, the way the room's
  lines do. The reader's error notes are one line everywhere, and the end of
  a message that could not be read keeps to one page like any other.

#### 1.2.1-forums.5, 2026-09-30, a core fix found by the forum tests' run

- **`SCREENS` could answer "The list could not be made" at once.** Its
  wait for the list compared two clock readings without a sign, and the
  wait's start is stamped a moment after the pass's own clock is read, so
  the first look could see a difference of minus one millisecond as four
  billion and give up on the spot. Intermittent on a board; frequent on the
  test host's fast clock. `MEM FORCE` and `SYS FORCE` share the same wait
  and the same fix. At the 1.2.1-dev.9 merge the comparison became
  `plat::since(now, s.waitFrom)`, the core's one rule for elapsed time.

### The boards: MF35V2 1.0.0 and MF35 1.1.3

From the board lane board-mf35v2 (branched from v1.2.0, built and benched
there as `1.2.0 (MF35V2 1.0.0)`), merged at 1.2.1-dev.9. Its image set,
`esp32s3-mf35v2`, is no longer tag_only: a plain v1.2.1 builds it.

- **The board Makerfabs sell now**: the ESP32-S3 Parallel TFT with Touch 3.5"
  hardware v2.0 (ESP32-S3-WROOM-1-N16R8, 8 MB octal PSRAM), a profile of its
  own (`BBS_BOARD_MF_S3PAR35V2`, envs `makerfabs_s3_par35v2` and `_release`).
  The v1.0's image does not run it: the v2.0 moved the panel's WR, D/C and CS
  to 18, 17 and 46 because 35 to 37 are octal PSRAM's pins.
- Everything the v1.0 has: the 480 x 320 status skin and the panel
  skins, SSH on 6400 and 6422 (eight at once), the S3's 8 MB layout (the first
  install is an erase), the link, and the VFS table at 12 (the shared S3
  layer's, since the merge).
- **The console is the chip's own USB** (the port marked USB-NATIVE), not the
  v1.0's CP2104: that is where Improv and the installer's Update answer. The
  USB-TTL port still flashes but carries no console.
- **Touch**: the FT6236 on the glass's flex (I2C 38/39, INT 40, address 0x38)
  is read as taps, as on the Touch-LCD-2: a tap wakes the glass and turns the
  header, and CONFIG panel's sleep timer applies. PANEL names the chip.
- Landscape with the USB edge at the bottom by default (Rob's choice).
- Pins from Makerfabs' v2.0 schematic, read as a netlist, cross-checked against
  their SD16_3.5 firmware, touch_keyboard_v2 and their IDF board config
  (release-prep/mf35v2/pins.md). CONFIG refuses 43 and 44 (the CP2104's), 38,
  39 and 40 (touch), 19 and 20 (the USB, also on the J1 socket) and the panel's
  data bus.
- On the lane (1.2.0): static DRAM 266,720 of 341,760 (75,040 free), image
  1,507,216 bytes. The Waveshare stick's and the v1.0's code and data are unchanged (only the
  ELF's own hash in each image moved, with the debug line numbers).
- On the bench: 8 MB PSRAM found, the card mounted over SPI, the FT6236
  answered (chip ID 0x64), the panel up at 20 MHz, Wi-Fi by Improv, first-boot
  setup, telnet and SSH logins on both ports. Internal heap 49,203 free, 37,843
  at its lowest. The glass itself is for Rob to confirm.
- A build left with a stale sdkconfig whose console is UART0 is refused by
  name (board.h), rather than building an image whose Improv never answers
  on USB-NATIVE.
- **MF35 1.1.3 (the v1.0), in the same lane:** CONFIG refuses 38, 39 and 40,
  the FT6236's I2C pair and INT on the v1.0's glass flex too ("That pin is
  wired on the board"). Nothing drove them, so a sysop could give them to the
  serial bridge or the lights and fight the touch controller. Only the pin
  check and the version string change in that image.

### Board versions (1.2.1-dev.10)

Each board whose own behaviour moved in 1.2.1 has its version bumped (Rob):

- **S3 1.1.5** (Waveshare ESP32-S3-LCD-1.47): the VFS table is 12, from the
  shared S3 layer, so SSH and a card no longer fill it; and CONFIG no longer
  refuses 43 and 44 as the console, which is the chip's own USB here.
- **WS43B 1.0.3** (Waveshare 4.3B): 43 and 44 are no longer refused as the
  console, so CONFIG serial takes the RS485 bridge's own pins.
- **WS2 1.0.4** (Waveshare Touch-LCD-2): 43 and 44 are a sysop's to use, no
  longer refused as the console (the console is USB).
- **ETH 1.0.3** (Waveshare ETH): 43 and 44 likewise.
- **FNCAM 1.0.9** (Freenove): the first timed shot after a boot is held
  rather than lost, and the camera is brought up again at the size the
  sensor really gives, so the first snap is the size CONFIG saved.
- **ESPCAM 1.0.6** (AI-Thinker ESP32-CAM): the held timed shot and the
  size reopen, as on the Freenove.
- MF35 1.1.3 and MF35V2 1.0.0 are unchanged: both moved already in their
  own lane.

### The merge review's low items (1.2.1-dev.11)

Comments, docs and a test, no firmware behaviour: platformio.ini's MF35V2
note no longer says its layer holds the VFS table; a board.h comment (the
WS2's serial pins) where a heredoc had left a backspace escape in place of
"BS" names `BBS_CONSOLE_UART0` again, the only such escape in the tree; and `test_config_serial_rows`
asserts that the Makerfabs v2.0 refuses 43 as wired on the board (and that
no other profile does), where it had passed on any answer but the console.
The three bench-only ESP32 envs (`esp32dev_backuptest`, `esp32dev_diag`,
`esp32dev_wdttest`) build with no warnings too, so all 21 envs do.

### Sizes

Static DRAM off the ELFs at 1.2.1-dev.10, the 18 board and release envs
built with no warnings (each _release env the same as its bench env):

| Board | Static DRAM | Free | Image |
|---|---|---|---|
| ESP32 (WROOM) | 165,688 of 180,736 | 15,048 | 1,273,648 |
| Freenove (FNCAM 1.0.9) | 176,624 of 180,736 | 4,112 | 1,349,904 |
| ESP32-CAM (ESPCAM 1.0.6) | 178,096 of 180,736 | 2,640 | 1,405,424 |
| Waveshare LCD-1.47 (S3 1.1.5) | 262,024 of 341,760 | 79,736 | 1,490,736 |
| Waveshare 4.3B (WS43B 1.0.3) | 264,616 of 341,760 | 77,144 | 1,507,456 |
| Waveshare Touch-LCD-2 (WS2 1.0.4) | 275,648 of 341,760 | 66,112 | 1,578,656 |
| Waveshare ETH (ETH 1.0.3) | 266,360 of 341,760 | 75,400 | 1,516,208 |
| Makerfabs 3.5" v1.0 (MF35 1.1.3) | 261,504 of 341,760 | 80,256 | 1,501,760 |
| Makerfabs 3.5" v2.0 (MF35V2 1.0.0) | 266,760 of 341,760 | 75,000 | 1,513,648 |

Images are the release builds. Against 1.2.0 the ESP32 boards are 32 to 80
bytes down (WROOM 15,080 to 15,048 free, Freenove 4,192 to 4,112, ESP32-CAM
2,720 to 2,640); the ESP32-CAM is still the floor. dev.10's longer version
string cost 8 bytes on the ESP32 boards and the Makerfabs (alignment).

## 1.2.0 (S3 1.1.4, WS43B 1.0.2, WS2 1.0.3, ETH 1.0.2, MF35 1.1.2, FNCAM 1.0.8, ESPCAM 1.0.5), 2026-09-29: the hardware release

**Out early for testing: this release has not been through the full
regression yet.** It shipped on smoke tests (every image built, `make test`,
the targeted groups with and without a card); the full run follows the tag
and 1.2.1 patches what it finds.

What a sysop sees, gathered from the link entries below:

- **Sats.** The µnleashed link pairs this board with small ESP32 boxes over
  ESP-NOW, encrypted, on every board including the WROOM. A **camera sat**
  (the unleashed_camsat firmware on an ESP32-CAM, no card needed) adds a
  camera to any board anywhere in radio range, and up to five boards can
  share one. A **door sat** is one a caller goes into with `UPLINK n` or
  `UPLINK name`, and home is Ctrl-C three times. `SATS` lists them; `CONFIG
  sats` pairs, shares and unpairs them, and `CONFIG sat <name>` sets one up.
  Every board on a sat has to be on the same Wi-Fi channel, and the board
  says so when one isn't.
- **One SNAPSHOT for every camera.** `SNAPSHOT n` picks the camera: the
  built-in one is 1, sats 2 to 9. A sat snap shows `Contacting camera sat
  #n...`, waits in line when the sat is busy, and a failure is one line
  naming the camera. Each sat has an error log in the card's Logs area.
- **CONFIG photos** holds the photo system: per-caller snap limits (one
  budget across every camera), retention and pruning for every camera, the
  default camera. `CONFIG camera` is only the built-in camera's hardware.
  Two pictures in one second both keep.
- **Four more boards in the full release**: the Waveshare ESP32-S3-Touch-LCD-4.3B,
  ESP32-S3-Touch-LCD-2 and ESP32-S3-ETH, and the Makerfabs ESP32-S3 Parallel
  TFT 3.5-inch, all with SSH. A plain release now carries all eight image
  sets.
- **Panel skins** on the boards with a display, from the Skins file area
  (14), and every camera's snaps in the panel's recent list.
- **Plugins from their own repositories** can be built into the firmware
  (`plugins.lock`, `tools/plugins.py`).
- **On the ETH board, sats can't pair while it runs on the wire** (Wi-Fi
  stands by unjoined); SATS and CONFIG sats say so. Wi-Fi beside the wire is
  1.2.1.

## 1.2.0-link (in development, not released), 2026-09-26

The µnleashed link, its lane (rel-1.2.0-link-r3, rebased onto main at
a3dcf01, after the v1.1.2 tag). Host-tested; the camera satellite has run
on the bench since link.4 (LINK.md has the figures).

**1.2.0-link.18: a camera sat says what it is doing**
- **`SNAPSHOT n` picks the camera**, as it already did: the built-in camera
  is 1, the camera sats 2 to 9 (SATS lists them). HELP's row says so
  (`take a photo; n picks the camera`), and `HELP SNAPSHOT` is new.
- **While a sat takes a caller's photo**: `--> Contacting camera sat #2...`
  with the spinner.
- **A busy sat is a wait, not a refusal.** With other boards' pictures ahead
  on the sat, the caller sees `--> Camera sat #2 is busy, 2 ahead of
  you...` with the spinner and the board asks again every 2 s, within the
  60 s a picture has to start. With the board's own one picture on the air,
  the next caller waits their turn in a line of four (`... 1 ahead of
  you...`); any key leaves it (`--> Stopped.`). At 40 columns the lines say
  `Sat #2` instead of `Camera sat #2`.
- **Failures are one line naming the camera**: `didn't answer.`, `took too
  long.`, `'s picture came damaged.`, `'s camera gave no picture.`, `is
  full, try again soon.`, `is busy, try again soon.` (it stayed busy for the
  whole minute), anything else as `Camera sat #2: <reason>.`
- **Each sat has a log on the card**: `camsat-<n>-errors.log` in the Logs
  area (FILES 10), one tab-separated line a failure and a picture: when, the
  sat's number and name, what happened, and the caller's handle or
  `timelapse`/`motion`. Written on the runner, never the loop; at 64 KB it
  becomes `camsat-<n>-errors.old` and a new one starts. No card: the console
  only, as before.
- **On Ethernet the link cannot reach a sat** (Wi-Fi stands by unjoined, and
  ESP-NOW needs it): SATS (staff) and CONFIG sats say `The link needs Wi-Fi.
  This board is on Ethernet, so sats can't pair.` (`On Ethernet: the link
  needs Wi-Fi.` at 40). The fix, Wi-Fi alongside the wire, is 1.2.1's.
- **LINK.md reserves family 3 (CALLIN) and kind 3 (gateway)** for 1.3.0's
  inbound caller sessions, so nothing in 1.2 reuses them.
- At 40 columns the snapshot count reads `Snapshot 1/10 this hour, 1/20
  today.` and `Photo saved:` puts `It is in FILES, area 12.` on its own line,
  and the download question drops its double spaces, on the built-in camera
  and the sats both: each was 40 or more wide.
- A caller's snapshot from a sat is counted when the picture is filed, so a
  busy, full or silent sat costs nothing; a key stops a picture the sat has
  not started (`--> Stopped.`), and a caller who hangs up while a busy sat is
  being asked again gives the air to the next in line.
- With another sat's picture on the air, the wait is said as the board's:
  `--> Camera sats busy here, 1 ahead of you...`.
- Plugin API 1.3 (satwords' camera sat lines, `linkp::onWire`); camsat 1.1.0
  needs it.

**1.2.0-link.17 (S3 1.1.4, WS43B 1.0.2, WS2 1.0.3, MF35 1.1.2): panel skins**
- **Panel skins join 1.2.0**, from the panel-skins lane (1.2.0-skins.6, its
  history under the skins entries below): CONFIG panel's Skin row, the
  Skins file area, PANEL's skin lines, the five stock skins seeded onto the
  card. On all four boards with a display. The stock skins are drawn for
  480 x 320, so they are offered on the Makerfabs; the other glasses offer
  `status` and any card skin drawn for their size.
- **The Skins file area is 14 on a board with a display**, after Photos (12)
  and Timelapse (13), which every board has since 1.2.0.
- **CONFIG panel on the Touch-LCD-2** has Sleep and Skin both, and gives up
  X offset and Y offset for them (its 240 x 320 glass fills the controller,
  so both are 0; system.cfg still takes them). The 4.3B's page gains Skin, last,
  so none of its rows move.
- **A sat's snap shows in the panel's recent list.** The count of callers'
  snaps moved from the camera plugin to the photo system
  (`photos::callerSnaps`), which counts every caller's picture filed, by the
  built-in camera or a sat.

**1.2.0-link.16: the hardware preview merged in**
- **The four boards of v1.1.2-hardware-preview join 1.2.0**: the Waveshare
  ESP32-S3-Touch-LCD-4.3B (WS43B 1.0.1), ESP32-S3-Touch-LCD-2 (WS2 1.0.2)
  and ESP32-S3-ETH (ETH 1.0.2), and the Makerfabs Parallel TFT 3.5"
  (MF35 1.1.1), with the camera DMA and VFS fixes and the switchboard lamps.
  Their history is under the 1.1.2-hw entries below.
- **A plain vX.Y.Z release builds all eight image sets.** The four were
  tag_only while they were a preview; a preview or board tag
  (`v1.2.0-hardware-preview`, `v1.2.0-ws2.1`) still builds only its own.
- The ETH profile builds its plugins from their own repositories like
  every other board (`custom_ext_plugins`), and its camera files through
  `photos::` as the others do.

**1.2.0-link.15: pruning for every camera**
- **CONFIG photos' retention now applies to every camera's photos**, a
  camera sat's included (Rob's decision). Until link.14 only the built-in
  camera pruned Photos and Timelapse, after its own snaps, so a board whose
  camera was a sat kept every picture until the card filled.
- **Same rules, same settings.** Callers' photos are one group, timelapse/
  another with the timelapse's limits, and the floor takes the board's own
  shots first. A sat's motion/ pictures are a group of their own, kept by
  the timelapse's limits until they have their own, and first to go for the
  floor. A camera sat takes no picture while the card is under its floor, as
  the built-in camera already did (plugin API 1.2: `photos::tally`). Only a name of a
  camera's exact shape is ever counted or removed, as before.
- **Never on the loop** (Rule no. 1): the prune is the photo system's job on
  the background runner (`photos::tick` posts it), after any picture is
  filed, when a camera starts, when CONFIG photos' retention changes, and
  once a day. A removed photo's FILES.BBS line still goes through the file
  areas' queue.
- The camera's own survey now only clears a half-written photo and looks for
  the sensor at start; CAMERA's counts are the photo system's (every
  camera's photos). The console's "N old photos removed" line is
  `photos:` rather than `camera:`, and the snap line no longer ends in
  "N removed".
- A walk that could not see every photo (a card with more than 16,384, or
  memory short) still prunes by age and count but never for the floor,
  which would have taken callers' photos while an unseen timelapse folder
  stood; the floor then goes by the card's own free space. The prune's
  tables are in PSRAM on any board that has it, not only camera boards.
- A prune that emptied more than four callers' folders lost the FILES.BBS
  tidies past the fourth (the file areas' queue holds four); they are now
  handed over as the queue has room.
- A prune starts two seconds after the last picture filed, with the runner
  free and no camera taking or bringing in a picture, so a burst of
  pictures is one prune after it and a sat's transfer is never held up by
  one. While the card is under its floor the board recounts every five
  minutes, so its own shots resume once space is freed. FILES.BBS tidies
  always leave a queue slot for a new picture's description.
- `plat::sdList` says false when a folder's read fails part way, so a
  partial folder is never taken for a whole one.
- Tests: `host/test_photos_cfg.cpp`, `syscfg::parseFile` alone: a `photos_`
  line wins over the camera's old line in either order, an old line stands
  in only where there is none (fails five checks with the rule removed).
  `test_camera` waits for the count and the prune instead of sleeping past
  them. `test_sats` opens the camera sat's page by its number: after the
  radio group a door sat is called "shelf" too, and the name opened the
  door's page (it failed the same way on link.14 when run after radio).
- Plugin API 1.2 (`photos::Tally`, `tally`, `pruneSoon`, `pruning`,
  `systemFolder`); camsat needs it for its floor check.

**1.2.0-link.14: the photo system's limits and retention on CONFIG photos**
- **CONFIG photos holds each caller's snaps an hour and a day** (1 to 20,
  10 and 20 as shipped, every camera together; they were fixed) **and
  retention** (`photos_keep`, `photos_max`, `photos_floor`,
  `photos_tl_keep`, `photos_tl_max`), which was the built-in camera's
  (`keep`, `max`, `floor`, `tl_keep`, `tl_max` in `[plugin:camera]`).
- **Nothing a sysop set is lost.** A file with the camera's old lines runs
  with them and CONFIG photos shows them; its first save writes the
  `photos_` key for each and drops the old lines, so a file never keeps both. A
  `photos_` line wins over an old one put back by hand, and a restore of an
  older backup is read the same way. CONFIG camera no longer shows them and
  CAMERA SET refuses them.
- `photos::Budget` carries the limits (API 1.1), so a camera sat says "3 of
  5 this hour" by the board's figures.
- Tests: `test_photos_config` (the old lines shown, the save migrating them,
  the new line winning, the parser's range), limits in `test_camera`, and in
  `test_sats` a picture whose second is taken lands on the first free one.

**1.2.0-link.13: two cameras in one second; the CONFIG names**
- **A second picture in the same second is kept.** The built-in camera
  and a camera sat that stamp a picture in the same second asked for the
  same name, and the second picture was thrown away: at every timelapse
  slot, since both start a slot on the same second. (Two sats cannot
  clash: the camsat plugin takes one picture at a time, and so does the
  built-in camera.) It is filed under
  the next second's stamp now, up to five seconds on (Rob's rule, the
  gallery spec): `photos::fileAs`, `camrules::laterName`. The name keeps
  its shape, so retention and the listings need nothing new, and the
  caller is offered the name it was filed under. The built-in camera uses
  it; the camera satellite plugin does from camsat's photo-names branch.
  The plugin API is 1.1 for it (`photos::fileAs`), so a camsat that uses
  it refuses by name to build into an older core. The host refuses a
  rename onto a name the way the card's FatFs does.
- **The CONFIG names Rob settled (2026-09-28): one word per thing.**
  CONFIG photos is the photo system (the default camera now, the gallery's
  auto-show rows to come); CONFIG cameras still opens it, unlisted. CONFIG
  camera is the built-in camera's hardware. CONFIG sats lists the sats;
  CONFIG sat <name> opens one, and a camera sat's page has a Camera
  settings button (the camsat plugin's page, which every camera sat this
  board owns runs). CONFIG camsat still opens it, unlisted while it is
  running; while it is off, CONFIG and CONFIG sats show it, since its page
  is where it is switched on. `CONFIG sat 2` takes a camera number too.

**1.2.0-link.12: on 1.1.2**
- Rebased onto main at a3dcf01 (v1.1.2 at cfc76bb): the login and logoff
  split across passes, the per-pass open count, the small printf, the
  test lanes. The link's formats pass `tools/check_formats.py`, and so do
  camsat's. `--only==test_x` (an exact name) is kept beside 1.1.2's
  `--tests=`; `harness.sh --ext` builds under 1.1.2's `--no-build` rule.
- **Found by the code review of the rebase, where the link met 1.1.2:**
  - A CONFIG link save now gives a caller in a door back ("--> Lost the
    signal."). 1.1.2 restarts only the plugins whose settings moved, so the
    link restarted under a running doors, and a caller in a door sent keys
    to a session the new link never had, hearing nothing until the home
    key. The link tells every family its peers are gone before it stops.
    `test_doors` checks it, and failed without the fix.
  - The pairings file opens through `disk::open` (the drive light, the
    pass's open count) and waits for a pass that has written nothing to
    flash, like the caller log and the call figures.
  - `harness.sh --jobs` builds `host/linkpeer` with the boards and reaps
    one a lane left behind; the door and sharing tests say they need
    `test_radio_link`'s pairing (NEEDS); `host/linkpeer` keeps the board's
    fast clock (`BBS_FAST_TIMERS`), or on a x4 lane the board heard its
    pings a quarter as often and dropped it.
  - `tools/release.py` checks a carried plugin's formats for newlib nano;
    `tools/changed_groups.py` maps the link's files to `radio`, `sats` and
    `camera` rather than the whole suite.
- LINK.md has the camsat bench's link.7 figures: a two-hour soak of 106
  of 106 pictures at a median 75 KB/s with 3 slow passes, all at the
  start, and the robustness runs.
- internal/link-redundancy-2026-09-28.md: what keeps the link up today,
  and store-and-forward on the satellite proposed for 1.2.0.
- Tests: the full host suite in 12 lanes (`--changed a3dcf01..HEAD`),
  4,101 checks, 0 failed; `--ext camsat --card --only=sats,radio` 99/0;
  `make test` passes. Eleven envs, no warnings.
- Static DRAM off the ELF, against 1.1.2: WROOM 165,104 (15,632 free,
  +888), Freenove 176,440 (4,296 free, +688), ESP32-CAM 177,912 (2,824
  free, +704), S3 255,432 of 341,760 (+1,144). Images: 1,255,824,
  1,335,104, 1,390,288 and 1,430,928 bytes.

**1.2.0-link.11: a sat's type is camera or door**
- LINK's kind column and the pairing question say `camera` and `door`
  (Rob, 2026-09-27; `Pair camera "garden" ...`), from satwords.h, which
  also holds `gpio` and `sensor` for the types to come. "camsat" stays
  only as the camera satellite's firmware and repository name.

**1.2.0-link.10: the settled words**
- Rob's names (2026-09-27): "sat" and "sats"; an **orbiter** is a sat that
  feeds the board data (camera, GPIO, sensors, Home Assistant); a **door
  sat** is one a caller goes into. What callers read says "door sat" where
  it said "door box" (DOORS, and the line when one goes quiet). All of it
  in `src/core/satwords.h`.
- **UPLINK** goes into a door sat (Rob's verb; BEAM was rejected):
  `UPLINK n`, `UPLINK name` (a door, or a sat: straight in with one door,
  its doors listed with more), `UPLINK` alone lists them. HELP has one row,
  `DOORS | UPLINK  games up on the sats`. No shortcut, and no clash with any
  verb, shortcut or room command. The board speaks in its own voice on the
  way: `--> Uplinking to shed...`, `--> Home is Ctrl-C three times.` and,
  however a caller comes back, `--> Back home.` (after `--> Time's up.` or
  `--> Lost the signal.`, or the door's own words). All within 39 columns.
- `Bbs::markedLine` is public, so a plugin speaks in the board's voice
  rather than a copy of it.
- internal/sat-types-2026-09-27.md: what a relay, a GPIO, a Home Assistant
  and a third party's sat would each need.

**1.2.0-link.9: SATS, CONFIG sats and camera numbers that stay put**
- **SATS [n]**, for callers and staff (tty-ux-sats; Rob: "as long as
  nothing security wise is revealed to regular users"). A caller sees each
  satellite they may see or snap: its number for SNAPSHOT, name, type,
  awake, asleep or not answering, and its last picture, and nothing of the
  radio, the keys, the other boards or the firmware. `test_sats` checks
  that by content (a MAC, a channel, a key fingerprint, a signal figure, a
  rate) at 80, 40 and plain ASCII, and checks the check finds all five in
  staff's view. Staff get the radio, and `SATS n` one in full, the MAC,
  the fingerprint and the other boards' names needing NODES as well. No
  shortcut: S would be SNAPSHOT's if anything's.
- **Camera numbers do not move**: the built-in camera is 1, a satellite
  keeps the number CONFIG sats sets (2 to 9) or takes the lowest free.
  SNAPSHOT, CAMERA and SATS agree.
- **CONFIG sats**: a button a satellite, and a page each for its name,
  number and whether this board receives its timelapse and motion
  pictures (sent to the satellite at once). Pair, Share and Unpair run
  the LINK commands; Default and Settings open CONFIG cameras and CONFIG
  camsat. Share and Unpair leave the form and ask `(y/N)` on the sysop's
  screen first. Not yet as the spec has it: Default and Settings do not
  come back to CONFIG sats, and the other boards are read-only rows (LINK
  REVOKE revokes).
- **Fixed by the code review, before the commit:** Share and Unpair acted
  on a plain Enter, which is how a sysop walks the form to Save; a name of
  all digits could never be picked by name (refused, and a device's own
  such name gets `sat-` in front); SATS on a board with none said "none
  open to you"; two satellites asking for one number swapped it at every
  renumber; a camera number saved showed the old one until the next
  second. On the satellite (unleashed_camsat): an older board took every
  timelapse twice, a busy camera lost a motion picture, a board that gave
  up a queued SNAP still had it taken, a board gone mid-group held up the
  rest, another board's SNAP could light the owner's flash, and a full NVS
  could lose the pairing in the upgrade.
- On the satellite (unleashed_camsat, from its bench): a picture given up
  mid-way (a CONFIG save during it) has its part-file closed and removed on
  the runner, not the loop (a 51 to 87 ms slow pass on the card); a sensor
  that reads PID 0xFF twice after an EN reset gets a third try, powered down
  500 ms and given 300 ms to wake; the rate to each board is kept per board,
  since sending to two in turn reset it at every change and a fallback to
  1 Mbps never held; and a board that answers after a reset is back at
  24 Mbps at once rather than after 30 s at 1 Mbps (tried, and a marginal
  path falls back again within about a second).
- The link row in SYS and HARDWARE has its comma (`on, ch 6, 1 of 1 up`).
- For the camera satellite's side (unleashed_camsat, branch multiboard):
  `linkp::satInfo`, `peerRecv`, `peerCamNo`, `Family::settingsChanged`,
  and `photos::Camera`'s `number`, `pairing`, `levels`, `facts`.
- host/linkpeer plays a camera satellite (`--kind camsat`: STATUS,
  SETTINGS_OK with the owner bit, a small JPEG a SNAP, `busy N P`), and
  `tools/harness.sh --ext camsat` builds the board with a plugin from its
  own repository: `test_sats` runs the camsat plugin end to end.
- Static DRAM +40 on esp32dev (164,688, 16,048 free), +56 on the camera
  boards (the ESP32-CAM at 3,248 free) and the S3.

**1.2.0-link.8: one satellite, several boards; the door's way out**
- **A satellite pairs with up to 5 boards** (Rob: "Having one camera
  accessible by 5 boards would rock"), each its own pairing and key. The
  design record is `internal/link-multiboard-2026-09-27.md`; LINK.md,
  "One satellite, several boards", has the rules. New link frames:
  PAIR_OPEN, UNPAIR (both ways), REVOKE and PEERS; PAIR_HELLO carries the
  satellite's channel so a board on another one says why it will not pair.
- LINK: pairings numbered from 1; `LINK SHARE n` and `LINK REVOKE n board`
  (the owner); `LINK FORGET n` tells the satellite first; a Shared column,
  the other boards under a shared satellite, and a footnote for one not
  heard or a router that moved. At 40 columns the footer is four short
  lines (two of its lines were 41 and 47 characters, and wrapped on a C64).
  Names are one word and unique, because SNAPSHOT takes a camera by name.
- The pairings file is version 2 (recv, camno and chan); version 1 reads.
- **The way out of a door is 0x03 three times within 1.5 s**: Ctrl-C on a
  PC terminal, RUN/STOP on PETSCII, and the board's break everywhere else.
  It was Ctrl-] (0x1D), which is cursor-right on PETSCII (three moves right
  in a game threw a C64 caller out) and which telnet clients keep as their
  own escape. The door's arrival line names the key for the caller's
  terminal. The telnet layer turns IAC IP and IAC BRK into 0x03.
- **Fixed, found on the way:** pairing on a board up for more than 24.8
  days never finished (PAIR_DONE waited on `reached(now, 0)`, false for half
  the millisecond clock's range). linkpeer runs on wall-clock milliseconds,
  which is how it showed.
- **Fixed by the code review, before the commit:** a share window hopped
  every channel and starved the boards it already served (every session
  died at about 80 s); a share that stopped part way wedged the satellite
  until a power cycle; a lost board's failed sends made a sleeping
  satellite drop its good board; a PEERS that did not fit the queue was
  lost; a clear BEACON could rename a board in every other board's view;
  a forgotten pairing's queued frames could go to the next device in its
  slot. Each has a test that fails on the code before the fix.
- host/linkpeer plays a second board (`--board2`, driven by lines on
  stdin), so the harness tests sharing, revoking and the channel refusal
  (`test_link_shared`).
- Sizes off the ELFs: static DRAM esp32dev 164,648 (16,088 free, +280 on
  link.7), Freenove 175,960 (4,776 free), ESP32-CAM 177,432 (3,304 free), S3 254,952 of
  341,760. Images 1,311,568 / 1,384,944 / 1,440,240 / 1,486,480. The full suite
  as 16 tagged runs side by side: 3,540 checks, with and without a card.

**1.2.0-link.7: on 1.1.2, and one filing for every picture**
- Rebased onto main (1.1.2-dev.3): the link's job runs on the background
  runner by itself now (`__has_include("core/runner.h")` found it), so
  picture fragments, reassembly and the pairing arithmetic are off the loop
  for real; the bounded slice on the tick is gone from the build.
- The built-in camera files its pictures through `photos::` (`open`,
  `write`, `file`), the path a satellite's pictures take, and provides the
  Photos area (`photos::provide`). The file areas ask `photos::present` and
  `photos::levels`, no longer the camera, so **Photos and Timelapse (areas
  12 and 13) are on every board**, shown while something takes pictures: a
  WROOM with a camera satellite has them. The photos' description queue
  and `files::sendPhoto` are on every board too, so a satellite's plugin can
  offer "Download it now?".
- From the camsat bench on link.6 (S3 plus satellite, runner in: 72-88
  KB/s, no retries, no slow pass from any picture):
  - the bulk window is 64 fragments on a board with PSRAM (16 without),
    chosen at start, since the window was what paced the transfer;
  - a caller asking for a camera the board does not have is told the ones
    it has (`No such camera. Try: 1 camera, 2 garden.`), since `CAMERA` is
    staff's; staff are still pointed at `CAMERA`.
- `photos::file` refuses a name with more than one folder in it, as the
  Photos area lists one level.

**The link** ([LINK.md](LINK.md))
- One ESP-NOW protocol in the core for devices beside the board: a camera
  satellite, a door box. Specified first, then built to it, with Rob's
  decisions of 2026-09-26: our own AES-128-CCM on every frame with the
  header as associated data (the IDF 5.3.1 receive callback cannot say
  whether ESP-NOW decrypted a frame, and a MAC is easily spoofed), 8
  pairings, pairings kept out of the backup.
- Pairing is a sysop's `LINK PAIR` plus a physical act on the device: P-256
  ECDH, HKDF, and a 4-digit code both ends show. A fresh session key at
  every HELLO, used only once a sealed frame proves it, so a HELLO anybody
  can send cannot cut a working device off.
- Reliable, in-order messages; bulk messages (a picture) in fragments with
  a window and selective acknowledgement; a packet-number replay window;
  retries counted only while the far end is heard, so a router's channel
  hop is a pause and not a lost session. A satellite scans the channels to
  find the board and follows it when the router moves.
- Where the work runs (Rule no. 1): the loop opens control frames, eight a
  pass at most, and `LINK` reports what a frame costs it; picture
  fragments, reassembly, the CRC-32, the card writes and the pairing
  arithmetic are the background runner's (1.1.2) once it is in the tree.
- CCM built from one CBC and one CTR call over mbedTLS's AES, after the
  camsat bench measured mbedtls_ccm at 320 us a frame on an ESP32 and
  1,000 us on an S3: byte for byte mbedtls_ccm's, checked against RFC 3610.
- `LINK`, `LINK PAIR`, `LINK FORGET n`, `LINK NAME n name`; a `Radio link`
  row in SYS and HARDWARE for staff.
- Budget on the WROOM, measured: 142 bytes of static DRAM, 34,293 bytes of
  flash (ESP-NOW's own library 6,604 of it), about 15 KB of heap only while
  the link is on.

**Doors**
- `DOORS` lists what the door boxes on the air offer; `DOORS n` hands the
  caller over with one line a door can read in ten lines of Python
  (`UNLEASHED-DOOR 1 node=3 ... handle=Big+Dave ... term=pet40 minutes=42`),
  and takes them back when the door finishes, when their time runs out
  (TIMEUP with ten seconds' grace), when the box goes quiet, or on the
  break key three times within a second and a half (Ctrl-C on a PC
  terminal, RUN/STOP on a C64; the door's arrival line names it). Not
  Ctrl-], as first built: 0x1D is cursor-right on PETSCII, and telnet
  clients keep Ctrl-] as their own escape. Several callers share one box. Nothing a box sends is ever
  read by the board as input.

**Plugins in their own repositories**
- A plugin can live in a git repository of its own and be built in at a
  pinned commit (`plugins.lock`, `custom_ext_plugins`, `tools/plugins.py`,
  `tools/pio_plugins.py`) with no edit to the core: the build generates
  `ext_plugins.h` and the registry expands it. The plugin API is numbered
  (1.0) and a plugin says what it needs (`UNLEASHED_PLUGIN_API(1, 0)`).
  `tools/release.py` refuses a plugin that is not at its locked commit, a
  local path, or a licence the firmware cannot carry, and records every
  plugin and commit in `release.txt`. `tools/testplugin/` is the template;
  `tools/test_ext_plugin.sh` the test.
- A plugin outside the repository may now start: only the board's flash is
  kept for shipped plugins (`PF_CORE`), and a plugin that stores anything
  keeps it on the card (`PF_SD`).

**Also**
- `src/core/photos.*`: filing a picture in Photos, for the built-in camera
  and the camera satellite both, with FILES.BBS's single writer. The camera
  and the file areas move onto it at the 1.1.2 merge.
- `Bbs::callSecondsLeft` for plugins. The command table follows the plugin
  count (it was 12, written beside the table).
- The host build compiles ESP-IDF's own mbedTLS 3.6.0 (`MBEDTLS_DIR`), and
  `host/linkpeer` is a pretend door box on the host's UDP radio for the
  tests (`--only=radio`). `host/test_link` runs in `make test`: 118 checks,
  clean under ASan and UBSan.

**1.2.0-link.6: one SNAPSHOT for every camera**
- The camsat engineer's registry, approved 2026-09-26: `photos::Camera`,
  added and removed by whatever takes pictures. The built-in camera is
  camera 1, then satellites by pairing. `SNAPSHOT` and `CAMERA` are the
  core's (`src/core/cameras.cpp`), there while the board has a camera and
  gone with the last one.
  - `SNAPSHOT` takes CONFIG cameras' Default (a new core page, key
    `camera`), else the built-in camera, else the first that is up.
    `SNAPSHOT n` or `SNAPSHOT name` picks one.
  - `CAMERA` with one camera is that camera's own view and `CAMERA SET`, as
    before. With more it lists them; `CAMERA n ...` reaches camera n.
- A caller's snap limits are one budget across every camera (Rob), kept by
  the core and carried across a rename by the core. The built-in camera
  checks its plugin levels itself now, since its commands left its table.
- `Bbs::hasCommands` and `dropCommands` take one command table out by
  pointer; the core's tables survive a CONFIG reload.
- `test_camera_registry` (camera boards, with a card).
- The engine tells the far end when it closes a session (RESET CLOSED,
  two seconds after, from the tombstone), so a peer that never closes its
  own side no longer fills its 16 (camsat's 6.5-minute soak on link.5:
  54 KB/s, 0 retries, 39 of 39 timelapse pictures, but a satellite that did
  not close its side refused every SNAP after the 16th). `test_link` has
  it: 24 host-opened sessions, the peer closing none.

**1.2.0-link.5: the bench's figures, and the code review**
- The camsat bench (2026-09-26, the S3 on Rob's router and an ESP32-CAM)
  set the radio: 802.11g 24 Mbps per peer, down to 1 Mbps after three
  MAC failures in a row and back after 30 s clean; four frames
  outstanding (+53%); DISCOVER listens 20 ms a channel; Long Range stays
  off (it raised the board's own gateway pings to 29-55 ms).
- Picture fragments never touch the loop: the radio sorts them into their
  own ring and the runner's job takes them, opens them and feeds the sink,
  staying 50 ms after the last one. The loop taking eight a pass lost half
  a picture at 24 Mbps; on the task path the bench saw no slow passes.
- Pairing is commit then reveal (`PAIR_NONCE`, `PAIR_REVEAL`): the host
  commits to its nonce before it sees the device's, so nobody in the middle
  can steer two exchanges to one code. The key and code are bound to both
  public keys.
- HELLO and HELLO_ACK carry an HMAC tag under the pairing key, so only a
  paired device is answered, and a random boot epoch, so a device that
  restarted ends its old sessions at once. The host answers nothing while
  it is off its router; a device believes only BEACON's channel; a
  sleeping satellite rescans after three failed sends.
- Doors: a CLOSE the box's full window will not take is retried for 5 s
  after the caller is back at the prompt; the doors stop before the link,
  and the link sends what is queued before its radio goes.
- The link plugin: a stopped link's runner job neither sends nor delivers
  (a CONFIG save while a picture arrives); a finished job of a stopped link
  is collected and freed; the pairings file checks `fclose`; heap declared
  20 KB. `LINK` shows both rings and devices on the slow rate. The start
  line gives the channel only once the board has joined its router.
- **The first real pictures** (camsat on c948de3: 5 of 5 filed, XGA, about
  87 KB, but 17 KB/s and about 16 retries a picture) found two engine
  faults:
  - a session closed with `closeAfter` straight after its picture was
    forgotten before its last ACK had gone, and the sender's resend was
    answered "no such session": filed here, reported failed there. A
    closed session now goes only after the ACK it owes, and leaves a
    tombstone (8, 30 s) that ACKs a resend again and answers anything new
    with RESET, never opening it as a new session. `test_link` has the case
    (8 pictures, each session closed at once, 30% loss), failing without it;
  - the ACKs a bulk sender waits for went out at the loop's next tick, up
    to 20 ms after the fragments landed: with a 16-fragment window that
    wait, not the radio, set the speed. The runner now sends the ACKs that
    are due as soon as it has taken fragments, and when it has made half a
    window of room. To be measured on the bench against camsat's 440 KB/s.
- A plugin from its own repository gets no flash whatever flags it claims,
  and a second plugin with a name already taken is not started.
- Photos refuses a backslash or a colon in a name from a satellite.
- CAMERA family: SNAP carries the watermark text and the JPEG comment
  (the satellite does the pixel work), every picture is a host SNAP (EVENT
  asks for one), SETTINGS is 24 bytes (`src/core/linkfam.h`).
## 1.1.2-hw.2 (WS43B 1.0.1, WS2 1.0.2, ETH 1.0.2, MF35 1.1.1), 2026-09-28: the switchboard lamps

- **switchboard keeps every free line lit** (Rob's pick of the two versions the merge met). A free line is dim, steady dial blue, flickering up with RX on the even lamps and TX on the odd ones; a caller's line is the caller's rank colour, dipping on traffic; the sysop's line has no lamp. It no longer goes dark when somebody is on. This is the 4.3B lane's version, now for every board whose strip is set to switchboard.

## 1.1.2-hw.1 (WS43B 1.0.1, WS2 1.0.2, ETH 1.0.2, MF35 1.1.1), 2026-09-28: the hardware preview

Four new S3 boards on the 1.1.2 core, merged from their lanes (board-ws43b,
board-ws2, board-wseth, board-mf35-112) onto v1.1.2 for one pre-release,
`v1.1.2-hardware-preview`. Not through the full regression: host smoke runs
only, and **no firmware image was built in the session that merged it**
(the build host could not reach PlatformIO's registry), so every env still
has to be built, with 0 warnings and DRAM read off the ELF, before the tag.

- **The boards**, each detailed in its own entry below: the Waveshare
  ESP32-S3-Touch-LCD-4.3B (`esp32s3-ws43b`), ESP32-S3-Touch-LCD-2
  (`esp32s3-ws2`) and ESP32-S3-ETH (`esp32s3-eth`), and the Makerfabs
  ESP32-S3 Parallel TFT 3.5" v1.0 (`esp32s3-mf35`). All four run SSH.
- **The core moves to 1.1.2-hw.1** because shared code changed: the lights'
  `switchboard` strip effect (a new CONFIG choice on every board; the
  shipped boards' defaults are unchanged) and `strip_fx` names up to 11
  characters. The two lanes built switchboard differently; the Touch-LCD-2's
  is kept: `nodes` while anybody WHO shows is on (the sysop's line included,
  so the strip never says "waiting" beside "Callers 1/11"), and dim steady
  dial-blue lamps that flicker with traffic while nobody is. On the 4.3B
  that means a free line's lamp is dark while a caller is on, where the
  4.3B's own version kept it dim blue.
- **The S3 camera's DMA block** (ETH's fix, also in WS2): `kCamDmaBlock` is
  17 KB on an S3 (16 x 1 KB for JPEG, CAMERA_DMA_BUFFER_SIZE_MAX 16 KB in the
  board's layer, a static_assert tying the two), not the ESP32's 33 KB that
  refused every snap. Both S3 camera boards (WS2, ETH) carry it; the 4.3B
  and the Makerfabs have no camera.
- **The VFS table, 8 to 12** (the Makerfabs' fix, MF35 1.1.1), now in the
  4.3B's, the Touch-LCD-2's and the ETH's sdkconfig layers too, each with a
  board.h `#error` on a stale sdkconfig: with a card mounted every S3 with
  SSH sat at 8 of 8. WS43B 1.0.0 to 1.0.1, WS2 1.0.1 to 1.0.2, ETH 1.0.1 to
  1.0.2. The Waveshare LCD-1.47 and every board that shipped in v1.1.2 are
  left as released (queued for 1.1.3).
- **Two touch controllers, told apart**: the 4.3B polls its GT911 over the
  new I2C driver (`BBS_TOUCH_POLL`, `platform_esp32_rgb.cpp`), the
  Touch-LCD-2 counts its CST816's taps on INT over the legacy driver (the
  camera's SCCB needs it). The legacy driver is never compiled into the
  4.3B's image. `plat::chipTemp` is one function, in tenths of a degree.
- **The 4.3B's tap**: a tap that landed while the slot was already fading
  (the board's own turn) was counted and then held the usual 3 s, not 10;
  every tap now marks the next page's long hold. `test_board_ws43b` failed
  on it about half the time, on the lane as well.
- **camera.cpp**: the caller-snap count the Touch-LCD-2's panel reads is
  compiled only on a board with a panel, so the Freenove's and the
  ESP32-CAM's camera code is as released.
- **release.py**: `v1.1.2-hardware-preview` (the core's X.Y.Z and a name in
  `PREVIEW_TAGS`) builds exactly the four preview sets into
  `release/1.1.2-hardware-preview/`; a plain `v<BBS_VERSION>` builds the
  four released boards and none of the preview's; a board's own tag
  (`v1.1.2-ws2.1`) builds its set alone; `--board DIR` builds one set into
  `release/<version>-DIR/`. Every earlier check stays.
- From the merge's code review: the three Waveshares' SSH tests are in
  their host profiles (`PROFILE_TESTS`, `PROFILE_CARD`, `ssh_ready`), as the
  S3 stick's and the Makerfabs' were; the core's per-line traffic bits for
  the Makerfabs' big glass are compiled only there (`BBS_PANEL_BIG`), not on
  every panel board; the Touch-LCD-2 draws its own memory-stick icon again
  (`kIconStick`), which the merge had swapped for the 4.3B's; and the VFS
  comments say what was counted rather than a list that did not add up.
- One board profile at a time is now also checked by a count of every
  `BBS_BOARD_` define in board.h. The ETH lane's committed host binary
  (`host/bbs_host_wseth`, 9 MB) is left out and ignored.
- Tested (smoke, host): `make test` clean; `--only=board_<b>` without a card
  on each profile: WS43B 28/0 (three runs), WS2 25/0, ETH 14/0, MF35 24/0;
  the lights and config groups on the reference board: see the commit.

## 1.1.2 (WS43B 1.0.0), 2026-09-28: the Waveshare ESP32-S3-Touch-LCD-4.3B

A new board, as a board profile on the 1.1.2 core (branch board-ws43b, from
v1.1.2), for a board pre-release. The core stays 1.1.2; the board says
`1.1.2 (WS43B 1.0.0)`. The other boards' images change only by what every
board shares: a switchboard effect in the lights, the panel's layout code
(the stick's two layouts are unchanged, pixel for pixel), and a new CONFIG
refusal for board pins nobody else has.

- **The board**: ESP32-S3-WROOM-1-N16R8, 16 MB flash, 8 MB octal PSRAM,
  native USB only, a 4.3" 800 x 480 ST7262 on the RGB bus, GT911 touch, a
  CH422G I2C expander, a TF slot on SPI, a PCF85063A RTC, RS485, CAN and two
  isolated inputs and outputs. Every pin from Waveshare's schematic,
  cross-checked against their demos for this board (`src/board.h`). The
  Arduino demos' `USB_SEL` on EXIO5 is the plain 4.3 board's; on the B it is
  an isolated input.
- **The panel on an RGB bus**: no frame buffer in the driver. The bounce
  buffers (two of four lines, 12.8 KB of internal RAM) are refilled from an
  interrupt out of a 400 x 240 picture in PSRAM, every pixel doubled, so the
  interrupt reads a quarter of what a full frame would and 768 KB of PSRAM is
  never allocated. The program runs from PSRAM (`SPIRAM_XIP_FROM_PSRAM`), so a
  flash write no longer turns the cache off under the panel.
- **The layout** (internal/tty-ux-panel-ws43b-2026-09-28.md): the landscape
  layout grown for the glass, with a large-face header slot, a band word for
  shutting down and closed, two equal columns, callers flowing into the right
  column, `nobody on`, a system row of heap, peak and the chip's temperature,
  and the lights as a light bar with the drive light's lamp.
- **Touch**: a tap turns the header's page and holds it ten seconds, or
  wakes a panel the new `sleep` setting put to sleep. Polled every 50 ms over
  I2C, backed off to every ten seconds if the controller stops answering.
- **The card's chip select** is an expander pin held low, as Waveshare's own
  demo holds it: the sd plugin shows it and does not take it as a setting.
- **The serial bridge** defaults to the RS485 port (43, 44). No activity LED
  and no BOOT-hold reset: the board has no free GPIO, and GPIO0 is the
  panel's G3.
- **Lights**: `switchboard`, a new strip effect (a lamp a line: a caller's
  line in rank colour, a free line dim steady blue that flickers with traffic), the default on this board and a new choice in CONFIG
  lights on every board (whose defaults are unchanged); `BBS_LIGHTS_STRIP_FX`
  picks a board's default.
- **release.py**: a board pre-release tag (`v1.1.2-ws43b.1`) builds that
  board's set only; the set is `tag_only`, so no plain release carries it
  until the profile merges into one.
- Tested (functional only, on Rob's OK). Host: the WS43B profile 28/0
  without a card and 29/0 with one, the Waveshare stick's 71/0 both ways,
  the lights and CONFIG groups on the reference board 396/0 and 453/0,
  `make test` clean. Bench on COM23: boot and Wi-Fi rejoin after a
  SHUTDOWN and reset, telnet and SSH (6400 and 6422) logins, the card and
  FILES, PANEL at one and seven callers (the flow, five recent rows), the
  band word for closed and for a shutdown countdown, the panel's sleep,
  and switchboard kept across a reboot. Touch and the glass itself are
  Rob's eyes and fingers.
## 1.1.2 (WS2 1.0.1), 2026-09-28, board pre-release

WS2 1.0.1: the S3 camera's DMA check. Every SNAPSHOT was refused for memory,
because the check asked for the ESP32's 33 KB block; on the S3 esp32-camera
takes 16 x 1 KB for JPEG and at most CAMERA_DMA_BUFFER_SIZE_MAX for raw
frames, which `sdkconfig.defaults.ws2` now sets to 16 KB, so a bring-up needs
17 KB (`kCamDmaBlock`, with a static_assert tying the two). The ETH lane's fix,
applied the same way.

And the card at boot: it mounts before the panel starts, and the panel's CS
(GPIO45, a strapping pin pulled low at reset) left the ST7789 listening, so
it answered the card's traffic on its bidirectional SDA and every boot mount
failed with ESP_ERR_INVALID_CRC (a later SD MOUNT worked). The panel's CS is
held high while the card uses the bus without the panel (`panelQuiet`). On
the bench after both: the card mounted at boot (7.4 GB SDHC), FILES listed
Photos and Timelapse, and two SNAPSHOTs filed in Photos at about 4.8 s each.

### WS2 1.0.0

A new board, as a pre-release that carries its image set and nothing else.
The core is 1.1.2 unchanged. The panel and lights changes below are
gated on this board's glass and defines: the LCD-1.47 draws exactly as it
did. It has booted on one board on the bench; it has not
been through the regression.

**The Waveshare ESP32-S3-Touch-LCD-2** (`BBS_BOARD_WS_S3TOUCH2`,
`pio run -e ws_s3touch2`, WS2 1.0.0)
- The glass is mirrored in X, as the LCD-1.47's is (seen on the first
  flash); touch is read as taps only, so no coordinate needed the flip.
- ESP32-S3R8 (8 MB octal PSRAM, the LCD-1.47's chip), 16 MB flash, native
  USB-C. The LCD-1.47's 8 MB layout (`partitions_s3.csv`) and S3 layer;
  `sdkconfig.defaults.ws2` adds the camera's sensors (OV5640 and OV2640).
  SSH as on the LCD-1.47.
- Pins from Waveshare's schematic (its PinOut table and netlist) checked
  against Waveshare's own demo for the board: `release-prep/ws2/pins.md`.
  The camera's fifteen, the touch and IMU bus (47, 48), the touch INT (46),
  the IMU's INT1 (3) and the battery divider (5) are refused by name
  (`BBS_PINS_ONBOARD`, "that pin is wired on the board"). GPIO 18 is the one
  a sysop can use; the camera ships with no flash pin so it does not hold
  it.
- **The panel and the TF card share an SPI bus** (MOSI 38, clock 39, MISO
  40): `BBS_SPI_SHARED` raises SPI2 once for both. Every card command runs
  under a mutex the panel only tries, so a band is sent on a later tick
  rather than the BBS loop waiting behind a card write. The card's chip
  select is held high while the panel talks and no card is mounted. CONFIG
  lets the two share those pins (the panel's `pinShares`).
- **The panel at 240 x 320**, an ST7789T3 at 40 MHz, specified in
  `release-prep/ws2/tty-ux-panel-ws2-2026-09-28.md`: the header slot
  centred, a longer uptime page, a third system figure (the chip's own
  temperature sensor, `41C`, with a CPU icon, the heap's icon a memory
  stick beside it), the glyph strip widened to 122 px for a camera glyph
  packed last while a picture is taken, a caller's snap in the recent list
  (`EV_SNAP`), round lamps for the strip, and the rule over the strip
  cleared when there are no LEDs. The lights plugin ships on with no pin
  and `switchboard` as its effect (`BBS_LIGHTS_STRIP_FX`, the 4.3B's: a
  lamp a line in the caller's rank colour while anybody is on; dim steady
  lamps that flicker with real traffic while nobody is; no sweep, Rob's
  call), since the glass is this board's only strip.
- **Touch**: the CST816D is asked once at start over I2C (who it is, and
  to pulse INT on a touch) and then read as taps from its INT line by an
  interrupt, so nothing on the loop touches I2C. A tap cuts the header to
  its next page; CONFIG panel's **Sleep** (in Driver's row on a touch board,
  0 never as shipped) darkens the glass after that many minutes, and a tap
  or a ring wakes it. `PANEL TAP` taps the host's glass.
- **The camera**: the OV5640 (JPEG from the sensor, sizes to QXGA, XGA as
  shipped). Its exposure and gain are read to decide when a frame has
  settled, as the OV2640's are.
- Not used yet: the QMI8658 IMU and the battery divider.
- `tools/release.py`: the board's set is `esp32s3-ws2`, `tag_only`, with
  the board pre-release tag logic (`--tag v1.1.2-ws2.1`).
- Host: `bbs_host_ws2`, `tools/harness.sh --board ws2`, `test_board_ws2`,
  and round lamps and the tenth glyph in `test_panel`.
## Waveshare ESP32-S3-ETH, ETH 1.0.1 on core 1.1.2 (board pre-release), 2026-09-28

A new board, shown as `1.1.2 (ETH 1.0.1)`: the Waveshare ESP32-S3-ETH, an
ESP32-S3R8 with 16 MB of flash, 8 MB of PSRAM, a W5500 10/100 Ethernet port,
an OV5640 camera, a TF slot and one WS2812B. Built, code-reviewed and on
the bench (2026-09-28): the W5500 links at 100 Mb/s full duplex, DHCP
answers in about 1 s, and telnet setup and login and SSH on 6400 and 6422
all work over Ethernet, with the card mounted. Two SNAPSHOTs filed (about
5 s each, the camera powered up cold each time), and CONFIG network's
Ethernet row switched No then Yes, each restart on the right interface.
ETH 1.0.1 fixed the snap: the camera's internal DMA check used the ESP32's
33 KB figure where the S3 needs 17 KB (CAMERA_DMA_BUFFER_SIZE_MAX 16 KB on
this board), and every snap was refused for memory. The cable-pull
fallback is still to run; host tests go to a cloud run.

- **Ethernet first, Wi-Fi as the fallback.** The board takes an address
  over DHCP on the wire and serves telnet, SSH, mDNS, NTP and announce there.
  Wi-Fi stays set up but does not join while the wire works: if Ethernet has
  no address 10 s after boot, or 3 s after losing it, Wi-Fi joins, and when
  the wire comes back Wi-Fi stands down again. Callers on the interface that
  went away are dropped; new calls arrive on the other. Improv still sets
  the Wi-Fi network, and its trial still uses the radio while the wire is up.
  `ethernet = no` (CONFIG network, **Ethernet first**) runs on Wi-Fi alone.
- SYS names the interface in use (`Ethernet 100 Mb/s full duplex`, Wi-Fi
  `standby, the fallback`, Radio `standby, calls on Ethernet` rather than a
  false "SLEEPING"), DASH shows `Eth 100M` in Wi-Fi's place, HARDWARE lists
  `Ethernet 100 Mb/s` (or `no link`, or `off`), and the directory's system badge
  reads `ESP32-S3 · 8 MB · Ethernet · ETH 1.0.1`.
- The camera (Photos and Timelapse, as on the other camera boards), the SD
  card over SPI, SSH as on the Waveshare stick, the lights on the board's
  WS2812B (GPIO 21). The camera's power is switched by GPIO 8, which the
  firmware drives as the camera's PWDN line.
- The W5500's pins (9 to 14) and the camera's are refused by name in every
  pin setting. Other boards' images are unchanged: every change is behind
  the board's own define.
- First install is an erase, as on every S3 since 1.1.2.
## 1.1.2 (MF35 1.1.1), 2026-09-28, hardware preview

The Makerfabs board on the 1.1.2 core, for the combined
`v1.1.2-hardware-preview`. The core is 1.1.2 unchanged apart from one
missing include; every other board's image is the one 1.1.2 shipped.
**On hardware**: a bench smoke test on the v1.0 board (boot, Wi-Fi, telnet,
SSH on 6422 and 6400, HARDWARE, PANEL, the card). It has not been through
the full regression.

- **MF35 1.1.1: the SD card mounts again.** 1.1.0 on the bench said "no
  card: not enough memory" with 1.7 MB of PSRAM free. The IDF's VFS table
  holds 8, and this board fills it with two consoles (UART0 and the USB
  secondary), lwIP's sockets, three LittleFS partitions and SSH's eventfd,
  so the card's FAT was the ninth; `esp_vfs_register` reports a full table
  as ESP_ERR_NO_MEM. `sdkconfig.defaults.mf35` sets `CONFIG_VFS_MAX_COUNT`
  to 12 (16 bytes), and board.h refuses to build on a stale sdkconfig
  that still says 8.

**The Makerfabs ESP32-S3 Parallel TFT with Touch 3.5" (ILI9488), hardware
v1.0** (`BBS_BOARD_MF_S3PAR35`, `pio run -e makerfabs_s3_par35`, MF35 1.1.1)
- The 1.1.1 preview's profile (MF35 1.0.0, tag `v1.1.1-mf35.1`) brought
  onto 1.1.2 as it was: the N16R2 (16 MB flash, 2 MB quad PSRAM), the CP2104
  console on UART0 (`sdkconfig.defaults.mf35`), the micro SD slot on SPI,
  the 480 x 320 ILI9488 on esp_lcd's 16-bit i80 bus with its status skin,
  and the pin rules. v1.0 only: the v2.0 sibling in that preview (octal
  PSRAM, never on a bench) is not carried here.
- **SSH**, as on the Waveshare S3: callers on the telnet port 6400 and on
  `ssh_port` 6422, Ed25519 and ECDSA host keys made at first start, up to
  **eight sessions at once**. They fit its 2 MB of PSRAM: the panel's
  framebuffer is 300 KB, the 1.1.1 bench read 1.71 MB free with it up, and
  eight sessions at their 48 KB budget plus the 128 KB kept back come to
  512 KB. The live limit still falls if PSRAM is short when a client
  connects.
- **The S3's 8 MB flash layout** (`partitions_s3.csv`): two 3 MB program
  slots, 1,376 KB of userdata, 512 KB of screens. **The first install over
  the 1.1.1 preview must be a new install, with an erase**, as on the
  Waveshare: the data partitions moved. Take a backup first.
- Fixed in the core for any SSH board whose console is a UART: the SSH
  links' wake code used `read` and `write` without `<unistd.h>`, which the
  Waveshare's build only got through its USB console's headers. And an SSH
  caller's typing now lights their node's pip on a panel, as a telnet
  caller's does (`Bbs::sshRead`). **Both touch shared code the Waveshare's
  image is built from, so the S3's own board version (S3 1.1.3) must bump
  when this lane merges**; it is left alone here so the lanes merge cleanly.
- The board's name is `Makerfabs S3 Parallel TFT 3.5" v1.0` (35 characters),
  so HARDWARE's Board row fits 40 columns; it was 44 and wrapped.
- `tools/release.py --tag v1.1.2-mf35.1` builds its set alone
  (`esp32s3-mf35-*`, `tag_only`). Host: `harness.sh --board mf35` now
  builds with SSH and runs the SSH tests beside `test_board_mf35`.

## 1.1.2 (S3 1.1.3, FNCAM 1.0.8, ESPCAM 1.0.5), 2026-09-27

A patch: the board no longer stalls everybody while one caller does
something slow, SSH on the Waveshare S3 as a preview, and about 70 KB of
flash back on every board. Released on the host tests and a code review of
each part; the full regression follows the tag, and anything it finds goes
into 1.1.3. The bench checked the runner, SSH and ordinary use on the S3
and the Freenove; the login and logoff fix (dev.6) is host-tested only.
The dev entries below have the detail.

**Before you install**
- **On the Waveshare ESP32-S3-LCD-1.47 the first 1.1.2 install is a new
  install, with an erase.** The S3 has its own 8 MB layout now. Accounts,
  settings and mail on an S3 do not survive it: take a backup first and
  restore it afterwards. The ESP32 boards (the WROOM, the Freenove, the
  ESP32-CAM) update as usual and keep everything.

**What a sysop sees**
- **No stalls from one caller's slow work** (rule no. 1). Logging in and
  off, LAST, CALLS, SCREENS and SCREENS INSTALL, file listings, the forums,
  restores, the backup window's download, CONFIG, free-space figures and
  the directory announce all either moved onto a background task or stopped
  holding the board: on the bench logins were 64 to 247 ms each and
  announce rounds up to 285 ms, and every other caller waited through them.
- **SSH on the S3, a preview**: `ssh -p 6400 handle@board`, on the board's
  own port, an account's handle and password; or its own port, 6422.
- **The directory hears about callers within seconds**, not at the next
  heartbeat, and ANNOUNCE shows staff what was last sent and what came back.
- **About 70 KB less flash on every board** (newlib's small printf).
- **Time warnings reach a caller inside the room, the forums and the file
  areas**, not only at the prompt.
- **Fixes**: a room line arrives whole; your own private line carries your
  tag; LAST shows node 10 as 10; an upload's staging folder no longer lists
  its own files as uploads; an external reset is no longer called a
  watchdog; a hostname change says it applies at the next restart; a board
  with no name no longer calls itself by the software's; HARDWARE's card
  free has its thousands separator and unit; one snapshot at a time, and
  the second caller is told whose.
- **The console says more when something is slow**: every slow pass names
  its phase, node and command, and now how many files it opened.

## 1.1.2-dev.6, 2026-09-27

The last blocker on 1.1.2's tag: logins, logoffs, LAST and announce's
activity figures were each a slow pass on the bench (the S3: logins 64-94
ms, logoffs 55-186, LAST 70-88; the Freenove: logins 69-247, logoffs
78-231, an announce round 60-285 ms growing with the caller log). Core
only, so the board versions do not move. Host-tested; not yet on hardware.

- **A login is five short passes, not one long one.** The account read and
  the start of the password check; the thousand SHA-256 rounds, 100 a pass
  behind the spinner that already took 650 ms of the caller's time (about
  27 ms of work in one piece at 240 MHz); the verdict and the greeting; the
  motd, looked for in each flavour on the card and in flash; the landing.
  One check at a time on the board (a claim), so a second caller's waits a
  few passes. What a caller types straight after the password is kept for
  the prompt, as before. The boot log says what a check costs on the board:
  `users: a password check is N us of work`.
- **A login reads the account once.** Remembered staff access, the sysop's
  own-account question, the ring notes and the landing all read users.txt
  again (two opens each, with callstats.dat); they use the record the
  password was checked against. "The Nth caller today" is worked out by the
  board once a second when the day turns, never by the first login of a day.
- **The caller log is written with one block copy, not two.** The record
  went in at its slot and a seek back wrote the header; on LittleFS a seek
  in the middle of a write ends it, so every hang-up copied the log's block
  twice, a 4 KB erase each with both cores stopped. It is written front to
  back now, the records ahead of the slot carried across from a second
  handle, and LittleFS still commits it whole at the close. And one a
  pass: two callers hanging up together (two BYEs, a SHUTDOWN, a Wi-Fi
  drop) paid two in one pass; a record arriving in a pass that has already
  written flash waits in a queue of four for the next one that has not (a
  fifth writes the oldest at once: nothing is dropped from the log).
- **A call's figures are counted a pass later, by id.** The logoff read the
  account through users.txt to add one call to callstats.dat. The id is
  kept from the login, and the record is read, added to and written back in
  one open, in the first pass that has written nothing else to flash (a
  queue of four; a fifth writes the oldest at once, never drops one).
- **What the code review found, fixed before the commit.** A restore can
  give an id taken at a login to somebody else, so a restore now drops the
  queued figures (they were for the file it replaces) and a call that
  logged in before it is counted by handle, as before. A missing
  callstats.dat with accounts on the board is made from users.txt at the
  next hang-up, not started empty with one record. The caller log is only
  made afresh when it is not there: `w+b` after an open that failed for
  another reason would have emptied it. A password waiting its turn behind
  another node's check is wiped when its caller hangs up.
- **Every walk of the caller log opens it once a pass.** LAST, CALLS and
  announce's `calls24`/`minutes24` read a record at a time and opened the
  file for each one past the newest five: up to fifty opens in a pass. The
  first read in a pass opens it and the loop's tail closes it.
- **The slow-pass line names the files.** `opens N (W to write) Tus`: how
  many the loop opened in that pass, how many to write on flash, and the
  time in the opens themselves. The next bench reads the cause off the log.
- Host model (tests only): `hostio.txt` takes `write=N` (a flash write's
  block copy) and `hash=N` (a thousand rounds' cost), and screens are
  opened through `disk::open`, so their probes are charged too.
- Tests: `test_lag_login_calls`, `test_lag_logoff_calls`,
  `test_lag_last_calls` and `test_lag_announce_calls`, at the bench's sizes
  (45 to 70 calls, a card), each failing on 1.1.2-dev.5 first under the same
  costs (login 79 ms, logoff 68, LAST 138, announce 208) and passing here
  (worst 15, 34, under 5, under 5). The login test also runs three checks
  at once with the first caller hanging up mid-check (worst 21 ms).
  `test_calllog` checks the ring's layout across a wrap.
- Static DRAM off the ELF: WROOM 164,216 (16,520 free, +416), Freenove
  175,752 (4,984 free, +432), ESP32-CAM 177,208 (3,528 free, +432), S3
  254,288 of 341,760 (+416): the caller-log and call-figure queues, the
  password check's running state and the open tally. Images: 1,185,136,
  1,259,936, 1,313,056 and 1,361,760 bytes. Eleven envs, no warnings.

## 1.1.2-dev.5, 2026-09-26

Three small fixes found by the 1.3.0 spec work, before 1.1.2's tag. Core
only, so the board versions do not move. Host-tested; not yet on hardware.

- **A room line arrives whole.** A caller may type 64 characters, but the
  sender saw 60 of them back, and the ring that carries a line to everybody
  else held 64 characters tag and all, so readers got the line short by its
  tag: "how is the weather" arrived as "how is", and a 20 character handle
  on node 10 left 38. A ring line is now the longest tag, a space and all 64
  typed characters (90), wrapped at each reader's width as effects already
  were. `/me` was cut at 44 of the 60 it can take and is whole too. The
  ring is heap claimed when chat starts: 1,248 bytes more at the default 48
  lines, up to 52 KB more at the 2,000 line ceiling. Static DRAM unchanged
  on every board, off the ELF: WROOM 163,800, Freenove 175,320, ESP32-CAM
  176,776, S3 253,872 of 341,760. Images +208, +16, +144 and +256 bytes.
  On the camera boards the ring stays in internal RAM (it is under the
  16 KB above which allocations go to PSRAM), so the ESP32-CAM, at about
  9 KB free with the camera up, is where the 1,248 bytes are worth
  watching.
- **Your own private line carries your own tag.** In a sticky private
  (`/p3*`, and every answered ring) your copy of what you said printed `P>`
  and the other person's tag, so `P>#S:OpSys] my upload keeps failing` read
  as the sysop saying it. It was a deliberate mirror in 0.21.8 ("the mirror
  of the `P#1:...` the other side sees"); it is your tag now, and the `[>3]`
  on the input line says who it went to. `P>` itself is unchanged. The `P`
  and `P>` in front of a private are counted when the line wraps, so the
  first row no longer runs a column or two past a 40 column screen.
- **Held room lines are not lost to a full buffer.** Handing a caller the
  lines said while they typed checked for room once, before the first, and
  a line that did not fit was dropped with its place already passed. It
  checks before each line now, and what does not fit waits for the next
  key or post. Found by the code review; longer lines made it likelier.
- **History is wrapped at a word.** Joining and `/sh` replay lines plain,
  and with lines up to 90 characters the terminal's own wrap cut words in
  half; they wrap at the reader's width now.
- **LAST shows node 10 as 10.** It printed `'0' + node % 10`, so node 10's
  calls were listed as node 0. Two columns now, as DASH draws a call; the
  widest rows are 68 at 80 columns (with the IP) and 32 at 40.
- Tests: `test_room_narrow_whole_line`, `test_room_private_own_tag` and
  `test_last_node_ten`, each run against 1.1.2-dev.4 first and failing
  there.

## 1.1.2-dev.4 (S3 1.1.3, FNCAM 1.0.8, ESPCAM 1.0.5), 2026-09-26

Part 2 of 1.1.2: the small printf, and DASH says how old its kept figures
are, plus the test-speed tools. Host-tested; not yet on hardware.

**The small printf, on every board**
- **About 70 KB less flash on every board**: `CONFIG_NEWLIB_NANO_FORMAT`,
  newlib's nano printf and scanf in place of the full ones. Off the ELF:
  the WROOM 1,251,616 to 1,181,872 bytes (-69,744), the Freenove -69,408,
  the ESP32-CAM -69,872, the S3 with SSH 1,427,808 to 1,358,432 (-69,376).
  Static DRAM unchanged on all four. The formatter's stack frame goes from
  800 bytes to 160 (with 48 and 48 for the two helpers under it).
- Nano has no `%ll`, `%z`, `%hh`, `%j`, `%t` or positional arguments, and
  on a board `%llu` prints `lu` and moves every argument after it. The one
  that existed was `CAMERA`'s "Oldest kept" date, whose three figures
  would each have come out as `lu`; it prints the date from 32 bits now.
- **`tools/check_formats.py` refuses those formats in `src/`**, floats and
  `PRIu64` too, run by `make test` in `host/` and by `tools/release.py`,
  each time after its own self-test. The host build is glibc and prints
  all of them, so no test that runs a board could ever have seen one.
- **A board build stops if the nano setting did not take.** A
  `sdkconfig.<env>` generated before this says it is not set, which wins
  over `sdkconfig.defaults`, so it would have built the full printf without
  a word. `board.h` refuses such a build (delete the file and build again),
  and `release.py` checks each release's generated sdkconfig.
- Floats still print: the IDF links nano's float formatting in by itself.
  The checker refuses them anyway; nothing needs them.
- wolfSSH and wolfCrypt on the S3 were checked from the compiled objects:
  their logging is compiled out, and the two functions that format
  anything (a hex dump and a name lookup, `%02X`, `%04X`, `%s`, `%d`) are
  not in the linked image. esp_littlefs prints its geometry errors with
  `PRIu64`, so those console lines show wrong figures under nano.

**DASH**
- **The rule over the vitals says when the kept figures were measured**,
  as `MEM` and `SYS` do: `-- ending in . as of 14:02 ---` at 40 columns,
  `-- figures ending in . as of 14:02, MEM FORCE measures now ---` at 80.
  It is the row the rule already had, so the page is no taller and no last
  call is given up. At 132 columns the Data free row's note says `bytes,
  as of 14:02`.

**Testing** (tools and the host build only, no firmware change: landed
between dev.3 and this build; `internal/test-speed-2026-09-26.md` has the
figures)
- `tools/harness.sh --jobs N` runs the suite as lanes side by side
  (`tools/parallel.py`), with and without a card at once, plus a `--fresh`
  board for each fresh test and each board profile on its own build, and
  merges one verdict. The full run is 5.1 minutes at 24 lanes, against 75.8
  and 98.5 serial; a group is 1.5 to 2.5.
- The host board's clock can run fast (`BBS_FAST_TIMERS`, `harness.sh
  --fast`, on by default in `--jobs`): the busy countdown, the goodbye
  linger, detection, idle and time warnings and heartbeats pass 4x quicker.
  Loop timing and the wall clock stay real. Tests that time real seconds are
  listed with their reasons (`REALTIME`) and run on a real clock.
- Each profile is built once per `--jobs` run; lanes use `--no-build`.
- Per-test times (`TEST`/`TIME` lines, `tools/testtimes.py`), exact
  selection (`--tests=a,b`), and the lane rules beside ORDER_NAMES
  (`ALONE`, `NEEDS`, `REALTIME`, `FRESH_TESTS`, `PROFILE_TESTS`).
- The XMODEM and YMODEM test clients no longer wait a fixed tenth of a
  second for every ACK: a 42 KB transfer took 35 s.
- `login()` in the test client no longer takes the sign-up form's "Main"
  for the prompt.

## 1.2.0-skins.6 (S3 1.1.3), 2026-09-26: a re-uploaded skin stays up

Not a release. Found on the MF35 bench (board engineer's console): sending
a new copy of the skin on the glass through the Skins area let it go at
once, so the status layout was drawn whole, 30.5 ms in the plugins phase
(the pass took 57.6 ms). The skin on the glass now stays until the new copy
is read, as a skin change already did; a load already running is called
off and started again so the new copy is the one read.
- A skin's reads open through `disk::open` and `disk::dir`, like every
  other storage path since 1.1.1, so the drive light sees them and the
  host's per-open costs apply.
- `test_board_s3_skin` re-uploads the skin on the glass with the card's
  opens slowed and checks PANEL never shows status meanwhile; it fails on
  skins.5 and passes now. S3 host profile 98/0, test_skin 218/0.
- On the MF35 (1.2.0-skins.5): all five stock skins uploaded over the Skins
  area and each switched to. Loads 329 to 383 ms on the runner with 4,544
  bytes of its 8,192 stack spare; ten minutes idle with the pc skin up
  showed no slow pass.

## 1.2.0-skins.5 (S3 1.1.3), 2026-09-26: the skins are 1.2.0's

Not a release. Main's call: panel skins and the live widgets are part of
the 1.2.0 hardware release, and 1.1.2 is a patch that ships without them,
so this lane stays out of the 1.1.2 tag. The version and every "(1.1.2)"
this lane wrote on a skin feature say 1.2.0 now; what is 1.1.2's (the
runner, the S3's 8 MB layout, SSH) keeps its number. SKINS.md says the S3
layout now has room to embed the stock set, which is still to do.

## 1.1.2-skins.4 (S3 1.1.3), 2026-09-26: skins on 1.1.2-dev.3, and the review

Not a release: the panel-skins lane brought onto main at 1.1.2-dev.3 (SSH,
the background runner) as one commit, and the code review's findings
fixed. Rob approved all five scenes as drawn.
- A skin's load is a job on the background runner (`core/runner.h`), not a
  task of its own: the per-task trampoline this lane added is gone, and SD
  UNMOUNT and a remount wait for it through `runner::busy()` like any other
  job. The card meter reads the runner's kept figure (`core/space.h`).
- The callers graph could paint above its own box when a visible sysop
  left (old samples against fewer lines): every fraction is held to its box.
- A graph was redrawn whole on every sample; it is hashed by its column
  heights now, so a flat sweep costs nothing. No widget unit may draw more
  than 16,384 pixels at once (the reader refuses it, with the unit named),
  and the widgets hold back while the glass is behind, as the LEDs do.
- The lamp state for the sysop's line is `sysop` (it was `nodeS`, which
  read as "nodes" in the any-case reader); `node1`..`node16` are read in
  any case. mkskin.py refuses a colour with a sign in it, as the board does.
- The traffic rate after a long gap is worked out in 64 bits; a new skin
  resets the scene in place rather than through a 1.3 KB temporary.
- SKINS.md says what the `online` word, the node and `online` lamps and the
  `card` lamp count, and the unit limit, the graph's 240 samples and a
  list's 32 rows.
- The panel's JPEG decoder is its own file (`platform_esp32_jpeg.cpp`),
  and the host's TJpgDec object has its own name under the sanitisers, so
  a plain build after an ASan one no longer links an instrumented object.
- Host: test_skin 218/0 under ASan, mkskin selftest 100/100, S3 host
  profile (board_s3, with a card) 96/0, storage with a card 462/0.
- Sizes off the ELF, no warnings: S3 image 1,468,256 (+40,448 on dev.3's
  1,427,808), static DRAM 259,592 of 341,760 (+5,720 on 253,872); WROOM
  163,800, identical to dev.3; Freenove 175,328 (+8 on 175,320).

## 1.1.2-skins.3 (S3 1.1.3), 2026-09-26: the other four stock skins, live

Not a release: a checkpoint on the panel-skins lane, host-tested only. Rob
approved the Beige tower's layout and the doing column (the verb only).
- The Breadbin, Beige lid, Cream and wood and Front panel skins rebuilt to
  the tty-ux spec: a monitor listing who is on (and who is ringing), a
  40-column switchboard with the free lines dim, a TV scrolling the events
  with the channel readout as callers on and the tuning meter as the Wi-Fi,
  and a front panel whose status, DATA and address lamps are the board and
  its lines, with the events on green-bar paper. A modem on three desks
  lights MR, AA, CD, RI, RD, SD as a real one did.
- Contrast fixed on the C64 field (5.99:1) and the TV (7.47:1).
- The host renders' sample board has ten lines, as every board does.
- SKINS.md's stock table and example follow.

## 1.1.2-skins.2 (S3 1.1.3), 2026-09-26: live widgets on a skin

Not a release: a checkpoint on the panel-skins lane, host-tested only.
Rob, on the first skins: "we need some more info, not just the more or
less static image".
- skin.txt gains live widgets, each placed as often as its limit allows:
  `field` (one value with an optional label), `digits` (a seven-segment
  display), `nodes` (who is on, a row a line), `events` (the last logins,
  logoffs, pages and rings), `meter` (traffic, heap, card, callers or
  signal), `graph` (a two-minute sweep of traffic or callers) and `lamp` (a
  lens lit by a state: a line in use, the sysop on, ring, mail, closed,
  listed, the card, rx, tx, disk, error). New line words `online`, `lines`,
  `lastcaller`, `rssi`, `peak`, `version`; a third face, `size=tiny`
  (Spleen 6 x 12).
- Widgets redraw only when what they show changed (a hash per unit), at
  most 16,384 pixels a pass, carried on from where the last pass stopped.
  Figures come from RAM; traffic and callers are sampled twice a second.
- `tools/mkskin.py` reads the same grammar (94 shared cases) and its
  preview draws the widgets with sample data. SKINS.md documents all of it.
- The Beige tower stock skin is rebuilt as a waiting-for-caller screen, to
  the tty-ux spec in `internal/tty-ux-skin-widgets-2026-09-26.md`. The
  other four stock skins are unchanged until Rob has seen it.
- Host: test_skin 212/0 under ASan.

## 1.1.2-skins.1 (S3 1.1.3), 2026-09-26: panel skins, a lane for 1.1.2

Not a release: the panel-skins lane, from 1.1.1, to merge with the Makerfabs
3.5" board and then into 1.1.2. Host-tested; not yet on a panel.

- **Panel skins** ([SKINS.md](SKINS.md)). A board with a display can show
  a picture of a machine in place of its drawn status layout. The board's
  real lamps are lit on the picture: the drive light in the skin's own
  style (`pc`, `1541`, `disk2`, `breathe`, from the same disk state and
  error blink as a wired one), an activity lamp for traffic, and up to 16
  strip LEDs in whatever effect the lights plugin runs, wired or not. The
  status lines go in a rectangle of the skin's choosing, with an optional
  clock. A skin is a folder on the card, `skins/<name>/`, holding
  `background.jpg` (the panel's size) and `skin.txt`. The manifest's
  grammar is strict, and every fault is reported with its line.
- **The Skins file area** (12, or 14 with a camera): the sysop sends a skin
  as the pair `<name>.txt` and `<name>.jpg` over YMODEM, straight in with no
  approval, and CONFIG offers it at once. A file sent again replaces the
  one there, and a skin on the glass is reloaded from it. The loader reads
  a skin's folder first, then the pair. `mkskin.py pair` makes one.
- **Chosen in `CONFIG panel`**, the `Skin` row (`Panel skin` at 80 columns),
  in the row Driver had, the page being full. It offers `status` (the
  built-in, as shipped) and the card's skins drawn for this glass's size.
  `PANEL` names the skin on the glass, why the one set is not showing, and
  what CONFIG offers.
- **Nothing slow on the loop** (Rule no. 1). Reading, parsing, checking and
  decoding are a worker's, into a PSRAM block of its own. The loop copies
  the picture in 32 rows a tick, then redraws only the LEDs and lines that
  changed, 8,192 LED pixels a frame at most, the rest in turn on the next
  frames. It stops queueing LEDs while the panel's send queue is backed up.
  A skin that cannot be used shows the status layout, says why on the
  console and in `PANEL`, and is read again at the next CONFIG save, when a
  card comes or goes, or when a file for it arrives; never every tick. The
  console gives each load's time and the worker's least free stack.
- **The decoder is the ESP32-S3's ROM TJpgDec** (`esp32s3/rom/tjpgd.h`,
  `jd_prepare` at `0x40000858`): no flash. Its limits (baseline, YCbCr,
  8-bit, 4:4:4/4:2:2/4:2:0, 512-byte segments) are checked first, on the
  board and on the host alike, with the reason in words. The host decodes
  with TJpgDec R0.03 in `host/tjpgd`.
- **`tools/mkskin.py`**: `check`, `leds` (skin.txt lines from key colours
  painted on a copy of the art, `-o` for plain ASCII), `preview` (lit, as
  the panel draws it), `jpeg` (cropped to fill, never stretched unless
  asked), `pair`, `pack`, and `selftest` (the board's cases, which the two
  readers must agree on).
- **Five stock skins** in `skins/stock/`, painted by
  `tools/mkskins_stock.py` with no maker's name or logo: a beige tower, a
  breadbin with its drive, a beige computer with a lid and two floppy
  drives, a cream computer with its drive and a wood-grain TV, and a front
  panel whose 16 address lamps are the strip. They come to about 105 KB of
  JPEG. The code that seeds them onto a card, and keeps them current without
  touching a skin the sysop changed, is in (`skin_seed.h`). The set itself
  is embedded with 1.1.2's S3 layout (3 MB app slots); until then it ships
  as a zip.
- **Fixed on the way:** the panel's two Spleen faces were a copy in every
  file that drew text (a namespace-scope `constexpr` array has internal
  linkage). They are `inline constexpr` now: one copy an image.
- **`plat::taskStart` carries its own function and argument per task**,
  and is built for display boards too. It held one global slot, which a
  camera snap and a skin load at once would have shared. 1.1.2's shared
  runner replaces it.
- `lights::panelDrive`: the drive light's frame in a given style, for a
  skin.
- Size, off the ELF against 1.1.1: the Waveshare S3 image +24,208 bytes
  (1,294,432) and static DRAM +2,576 (250,360 of 341,760); the Freenove
  +608 bytes, static DRAM the same; the WROOM +416 bytes, +8 static DRAM.
  No warnings on any of the three.
- Tests: `host/test_skin` (159 checks: the shared cases, a 100,000-case
  fuzz, the JPEG rules against Pillow's files, the drawing's invariants and
  the LED budget, the seeding rule, the stock skins drawn) and
  `test_board_s3_skin` on the host board (24: loaded, drawn and read back,
  refused with its line, the Skins area's upload).
## 1.1.2-dev.3 (S3 1.1.3): SSH on the S3, a preview, 2026-09-26

Part 3 of 1.1.2. The ESP32 images (the WROOM, the Freenove, the ESP32-CAM)
are unchanged: everything below is compiled only where a board profile
sets `BBS_HAS_SSH`, which today is the Waveshare ESP32-S3-LCD-1.47.
Host-tested; not yet on the board.

**The first 1.1.2 install on an S3 must be a new install, with an erase.**
The S3 has its own 8 MB flash layout now, and its data partitions moved.
Accounts, settings and mail on an S3 do not survive that one install: take
a backup first and restore it afterwards. The directory's system badge
says `ESP32-S3 · 8 MB · PSRAM` from here on, since the image header is
8 MB so that one image boots on 8 MB S3s too.

**Callers**
- **SSH on the board's own port** (`ssh -p 6400 handle@board`). The connect
  settle already waited 300 ms for a telnet client to speak; a client whose
  first bytes are `SSH-2.0-` is SSH, everything else telnet exactly as
  before. No second listener and no second port forward. An SSH caller
  takes an ordinary node.
- The SSH user name is the handle. An account's handle takes that
  account's password, checked by the board with the prompt's lockout, and
  the caller arrives logged in (`Signed in over SSH as ...`, `ACCESS
  GRANTED`) with no handle or password prompt. Any other name gets in with
  no password to the ordinary handle prompt, to register or visit. Staff
  rights still come only from `BYE <password>` or the sysop account's login
  question.
- `--> Connection via SSH is Secure.`, with "Secure." in bold yellow.
- Three wrong passwords end a connection; each counts toward the handle's
  lockout (5 in 15 minutes), and a connection that ends on wrong passwords
  counts once toward the address's ban, which then holds telnet off too.
- The size comes from the SSH client, a resize included; the character set
  still from the probe, which over SSH never asks the PETSCII question.
  There is no telnet on an SSH link, so XMODEM and YMODEM bytes cross
  untouched.
- **SSH slots**: 8 at once on the Waveshare, or fewer when PSRAM cannot hold
  another session when a client connects. Past that the client is told
  `--> All SSH ports are full` and the connection closes with no key
  exchange (one SSH_MSG_DISCONNECT in the clear, reason 12): OpenSSH prints
  `Received disconnect from ...: --> All SSH ports are full`, PuTTY shows
  it in its error box. A telnet caller is never refused because of SSH.
- **SSH's own port, 6422** (`ssh_port`, CONFIG network's last row, 0 off),
  where the board speaks first with no terminal detection: for SyncTERM
  1.9 and older, whose SSH (cryptlib) waits to hear the server, and on the
  shared port only ever hears the telnet probe. Everything else works on
  either port. Never the same as `port` or `backup_port`. Advertised over
  mDNS as `_ssh._tcp`. A full board there says `--> All lines are busy`.
- **The socket budget.** The second listener would make lwIP's worst case
  17 of its 16 sockets (two listeners, ten nodes, the sysop node, the busy
  line, the backup window's two, announce's one). The busy line goes first:
  with SSH's port on it is offered only while the sysop node is free, so
  the worst case is 16 (2 + 11 + 2 + 1). Nodes are never refused for it:
  a caller moving to the sysop node while the busy line is held, and a new
  caller taking the node they left, can still reach 17 while both stay,
  which costs one announce attempt or one backup-window client and nothing
  else. A board that runs out drops the caller at accept, as lwIP does. A
  budget refusal says so on the console.
- A board already using 6422 (its backup window, say) runs without SSH's
  port after the upgrade and says so; its CONFIG saves and restores are not
  refused over the default. Refusals on SSH's port read the client's
  identification off before closing, so they arrive as words, not a reset;
  its callers get the same keepalive as telnet's. SYS shows the bound port,
  and mDNS advertises `_ssh._tcp` only once the listener is up.

**Sysops**
- Host keys, Ed25519 and ECDSA P-256 (both, for SyncTERM 1.10's and the
  older cryptlib's choices), made on the board at the first start and kept
  in `userdata/ssh/`. Not in the backup zip: a downloaded backup cannot make
  another board answer as this one. An erase or a factory reset makes new
  ones.
- `SYS` and `HARDWARE` show staff `SSH  n of m` (in use, and the cap now)
  and both keys' fingerprints; every caller's `HARDWARE` lists `SSH` among
  the capabilities. The console logs each SSH connection, refusal, login
  and ending, and the SSH task's least stack free.

**Under the bonnet**
- wolfSSH 1.5.0 on wolfCrypt 5.9.4, vendored unchanged in
  `components/wolfssh/` (the subset `user_settings.h` compiles), GPLv3 or
  later, compiled for the S3 only. Software crypto: wolfCrypt's hardware
  SHA and AES would share the peripherals with the Wi-Fi's mbedTLS under a
  second lock.
- The SSH task (core 0, priority 2, a 16 KB internal stack) owns the socket,
  the key exchange and the crypto; the BBS loop never runs any of it. Each
  link is a pair of 4 KB rings in PSRAM and an eventfd the loop's
  `select()` waits on as it does a socket. Every wolfSSH allocation goes to
  PSRAM first. Authentication is the loop's, asked for by the task.
- The S3's own `partitions_s3.csv` (two 3 MB program slots, 1,376 KB of
  userdata, 512 KB of screens, storage still last), its image header at
  8 MB, and its Wi-Fi static buffers 16/16 to 10/10 with the block-ack
  window at 10 (about 19 KB of internal RAM back).
- `tools/release.py` takes each family's offsets from its own partition
  table and checks the built `partitions.bin` against it.
- Sizes, off the ELF, on 1.1.2-dev.1 (part 1): the S3 image 1,427,808
  bytes (+136,176 on 1,291,632, of a 3 MB slot now), static DRAM 253,872
  of 341,760 (+3,376: eight links, the host keys, the SSH listener, three
  Session fields). The ESP32 images' application objects and libraries
  match dev.1's section for section and their static DRAM is identical
  (163,800, 175,320, 176,776); the linked images move by 0, +128 and +32
  bytes, the Xtensa linker's call relaxation, which varies between links.
- Found by the code review and fixed before the commit: the SSH task could
  spin without waiting (a client that never opens its window, or a socket
  that stops taking output) and starve core 0's idle task into a watchdog
  restart; it now waits on the socket, never runs a long streak without a
  wait, pauses after a pass that held the CPU, and gives a hung-up link ten
  seconds to take its last output. Wakes between the loop and the task
  could be lost (a push into a part-full ring, room made in a full one).
  "none" after wrong passwords undid the ban's count, and a connection
  could ask the loop for users.txt lookups without end (eight questions a
  connection now, two handshakes at once an address). A host key that
  would not read was replaced with a new one; now SSH stays off and says
  so. A closed board's own account signed in over SSH met the closed sign.

**Tests**
- `test_ssh_login`, `test_ssh_new_caller`, `test_ssh_resize`,
  `test_ssh_host_keys`, `test_ssh_telnet_unchanged`, `test_ssh_full`,
  `test_ssh_failed_logins`, `test_ssh_ymodem` (`--only=ssh`, with
  `harness.sh --board s3`; each SKIPs on the reference board). They call in
  with `host/ssh_call`, wolfSSH's own client: this machine does not let an
  OpenSSH client run. The refusal is checked by a parser in the test that
  shares nothing with the board. `host/test_sshlink` puts 4 MB through the
  ring on two threads (`make test`).

## 1.1.2-dev.1 (S3 1.1.2, FNCAM 1.0.7, ESPCAM 1.0.4), 2026-09-26

Part 1 of 1.1.2: the lag the 1.1.1 audit found
(`internal/audit-1.1.2-2026-09-26.md`), and the bugs from the 1.1.1 bench
check. Rule no. 1: nothing a feature does may stall the callers who are not
using it. Host-tested with targeted runs; not yet on hardware.

**The background runner**
- **One task for everything slow** (`src/core/runner.*`). Pinned to the BBS
  task's core, three priorities below it, so it only runs in the loop's idle
  time; started when a job is posted and gone three seconds after the last;
  an 8 KB stack from the heap, not static RAM; a static queue of 8. A job's
  state is set last by the runner, so the loop reads results only once they
  are whole, and a job that answers a caller records the caller's connection
  number (`Session::call`) and node rather than a `Session*`, so a caller
  who hangs up mid-job is never answered in somebody else's session. The
  host runs the same jobs on a thread. `SYS` shows its lowest stack and its
  longest job.
- The camera's worker is a runner job now, unchanged, and its own task and
  trampoline are gone.

**The ten audited stalls, and the rest**
- **The directory's name is looked up on the runner**, before each
  heartbeat (lwIP answers from its cache while the TTL runs), and a lookup
  that fails keeps the last address. It was a blocking lookup on the loop at
  every start, so at every `CONFIG` save, and after every refused connect,
  whatever the comment above it said.
- **Call figures are 16-byte records in `callstats.dat`**, updated in place
  at a logoff, rather than a rewrite of the whole of `users.txt` for four
  numbers. Made from `users.txt` on the first boot; `users.txt` keeps the
  fields so an older firmware still reads it. In the backup zip; an older
  zip restores the figures from its `users.txt`.
- **The handles are indexed in memory**, rebuilt whenever the board
  rewrites `users.txt`: the handle prompt, sign-up's checks, the account
  count and a lookup by id no longer read the file to its end.
- **Forums walk the index in slices**: one open of `INDEX.TXT` a walk, 64
  records a slice and 128 a pass, for the subject list, next-unread, the
  unread counts, `FORUMS SCAN` and the forum list's counts. A spinner if a
  walk runs long. The read pointers live in memory and are written when the
  caller leaves a forum or logs off, rather than at every message.
- **`SCREENS` and `SCREENS INSTALL` read on the runner**: one table from
  one read of each folder and one of the card's manifest, where each row
  stat'ed every flavour and read the manifest again. The install's plan runs
  there too; its renames stay on the loop, beside closing any caller's open
  screen.
- **File listings read a page at a time on the runner**, into a page
  cache shared by the callers on one area: one walk of the folder and one
  read of `FILES.BBS` a page, where each row reopened the folder, walked to
  its file and searched `FILES.BBS`. Photos too.
- **The backup window's download** reads every file for its CRC on the
  runner before the 200 goes out, where it did it on the loop in one pass.
- **A restore unpacks, checks and writes on the runner**, a file at a time.
- **Mail is written in place**: a send takes a free slot, a delete empties
  one, keeping one flips a byte. Every one of those rewrote `mail.dat`.
- **CONFIG reads `system.cfg` once to open a page**, where each field
  searched the file from the top, and a save restarts only the plugin whose
  section it wrote and any whose section is not what it started on
  (every plugin for a core page, and all of them for `sd`'s, which the
  others wait on).
- **Free space is measured on the runner, and kept.** MEM, SYS, DASH,
  HARDWARE, PLUGINS and every plugin's write guard asked LittleFS, which
  walks every block (170 ms on a board), or FAT, which reads its table. The
  figures are measured at boot and at every staff login, a kept one ends in
  `.`, and one line says when: `Figures ending in . are as of 14:02.`
  `MEM FORCE` and `SYS FORCE` (staff) measure now, with a spinner. A
  restore or `SCREENS INSTALL` marks only its own partition to be measured
  again. The heap figures are still read fresh, and `heapFree()` is the
  counter, not the allocator walk.
- **The caller log's copy on the card is written on the runner** at a
  hang-up, through a small queue.
- A co-sysop typing the staff password again the same day, from the same
  address, no longer rewrites `users.txt` to move the remembered date; the
  week runs from the day's first time.

**Bugs**
- **`users.txt` is never removed to make room for a rename.** The fallback
  after a failed rename deleted the live accounts and tried again, the
  shape that lost every account on a restore until 1.0.3.
- **`FILES.BBS` has one writer.** The camera wrote photo descriptions into
  the Photos folders' `FILES.BBS` while the file areas could be editing the
  same file on the loop, and FatFs does not lock. It asks the files plugin
  now (`files::photoDesc`, `files::photoTidy`), which queues the edit and
  does it on the runner.
- **An upload's staging folder listed its own files as uploads.** An
  auto-approved upload's description landed in `.pending/FILES.BBS`, and `P`
  offered `FILES.BBS` for approval, over the area's real one. The staging
  folder's `FILES.BBS` and `UPLOADS.BBS` are never uploads, in the list, the
  count and the approval; an auto-approved description goes straight into
  the area.
- **One snapshot at a time**, and the second caller is told whose: `-->
  Camera in use by node 3, try again in a minute` (`by the board` for a
  timed one), wrapped at a word on 40 columns (Rob).
- **The time warnings reach a caller inside the room, the forums, the file
  areas and the mailbox** (found on TRA: Rob was cut off mid-chat at his
  limit with no warning). Only a caller at a prompt was ever told. They
  come in through the same door as a page, in the board's own voice, with
  the bell: `--> 5 minutes left on this call`, `--> 1 minute left on this
  call`, wrapped inside 39 on 40 columns. At a screen's `Press SPACE to
  continue` the line goes under it and the question again. (A caller inside
  a plugin is never idle, so there is no idle warning to carry.)
- **An external reset is not a watchdog.** The classic ESP32 reports a
  press of EN, or a serial port toggling it, as an RTC watchdog reset, and
  the board said it had crashed. It reads the ROM's own reason now and says
  `reset pin (EN or a serial port)`.
- **The private conversation's count left out what the caller saw.**
  `/p*` after the partner left said `1 room line went by` for the leave
  notice the caller had just read. Only lines not shown are counted.
- **A hostname saved in CONFIG said `Saved and live`**, and it is used
  from the next restart. It says so.
- **HARDWARE's card free** had no thousands separator and no unit.
- **A board with no name set called itself by the software's**: `µnleashed
  BBS running µnleashed BBS v1.1.1`. `@BOARD@` falls back to the hostname,
  in screens and in messages alike.
- The camera's bring-up no longer logs `gpio_install_isr_service` at every
  snap.
- The ESP32-CAM's host tests named the Freenove in their skip messages.

**Announce, rock solid** (Rob: "when a caller joins announce should send
that out asap, it doesnt")
- **A caller change is sent within seconds**: a login, a logoff, a guest,
  `SHOW`, `HIDE` and `LURK`, gathered for two seconds and sent as soon as
  the directory's 30 s per-address limit allows. It waited out a 60 s gap,
  was dropped outright if a heartbeat was on the wire, and was never sent
  again if the directory refused it, which left the directory up to ten
  minutes behind. `nudge_seconds` is 32 as shipped (the board times the gap
  from when its heartbeat left, the directory from when it arrived), and a
  refused change is sent again up to three times.
- After failures the timed heartbeat backs off from 30 s, doubling, up to
  the interval, so a directory that comes back hears from the board in
  minutes.
- A reply whose headers never finished is a failure and nothing in it is
  kept: a token cut part way would have replaced the good one.
- **Every heartbeat's outcome is one console line**, with the count it
  carried; `ANNOUNCE` shows staff the last send, its answer and the next.
- A reliability suite on the host, against a stand-in directory: a join
  and a leave in seconds, DNS gone and back, a directory gone and back, one
  that never answers (timed out, its socket closed), replies refused, not
  HTTP, too long and cut off, a token issued again, a CONFIG save mid-post,
  closed with the largest payload, the published default holding the
  listing, and a day of heartbeats with the sockets and the memory flat.

**Testing**
- The host can give every open a cost (`<data>/hostio.txt`: card and flash
  microseconds, and `log` for a console line an open) and prints every slow
  pass, so a test drives an audited path at a realistic size (250 accounts,
  200 files, 2,000 forum messages) and asserts no pass over 50 ms, or counts
  what the path opened.
- `tools/harness.sh --changed <range>` works out the test groups from the
  files a git range touched (`tools/changed_groups.py`).

## 1.1.1 (S3 1.1.2, FNCAM 1.0.6, ESPCAM 1.0.3), 2026-09-25

The patch to 1.1.0, with what 1.1.0 left and what its first days found.
Host-tested with targeted runs; the full regression follows.

**Callers**
- **Every caller is told how they are connected** (Rob), once the terminal
  is known and before any screen: `--> Connection via Telnet is not
  secure`, on the welcome, the busy line and the closed sign alike. 39
  columns. Built from what the link is, so SSH (1.2.0) is one more row:
  `--> Connection via SSH is Secure.`
- **`/p<n>*` is a private conversation** (Rob: "it should just be who
  you're privately talking to"). While it is on, the caller sees the two of
  them: their privates both ways and the room's notices about either. The
  room says `--> You won't see other callers while talking directly`,
  wrapped at a word on 40 columns. What the room says meanwhile is held in
  its ring, not shown and not lost: `/p*` says `Back to the room. 3 room
  lines went by: /sh 3 shows them.`, or, when the room outran its ring,
  how many `/sh` can still show. The partner leaving is shown, `They have
  left. /p* goes back to the room.`, and the next line goes nowhere rather
  than into the room. An answered ring puts both in it. The room's
  private mode, so the 1.2.0 sysop chat is this and not a second thing.
- The "Talking to #3:... only" line wraps at 40 columns.
- `/sh` shows what the output buffer holds and says how many are left
  (`20 of 48 shown: /sh 28 for the rest.`): the whole ring at once was
  more than one buffer, and a put that does not fit is dropped, newest
  first.
- The chat room resets its line rate, room name and colours when its
  settings are read again: a line taken out of the section kept the old
  value running until a restart.
- The name is µnleashed where a person reads it: Improv's firmware name
  (UTF-8; the installer matches either spelling since site 1.3.8), the
  photos' JPEG comment and the FX demo's marquee (a real µ on UTF-8, CP437's
  on ANSI, a u on PETSCII and plain ASCII). The hostname, the default
  password and announce's `software` stay ASCII.

**Sysops**
- **`SCREENS INSTALL`** (Rob): the card's own screens copied into flash so
  they play with the card out. Checked whole first (names, sizes, count and
  the partition's room in blocks) and refused whole; each put live by a
  rename within the partition, its stock copy moved to `screens/.stock`;
  callers let go of flash screens first; the card's manifest marks them the
  sysop's. `SCREENS INSTALL STOCK` puts the stock set back. One step a pass
  with a spinner, never a burst. Not beside a backup or a restore.
- **100.64.0.0/10 is local only when CONFIG network says so** (Rob): new
  `cgnat_local` (CGNAT/Tailscale LAN, `CGNAT` at 40), off as shipped, live.
  It trusted everybody behind the same carrier NAT before. One rule for
  "local" now (`guard.h` `localNet`), asked by the shell and the backup
  port, which also takes 127.0.0.1 alone where it took all of 127/8. A
  backup-window caller refused from 100.64/10 is told which setting it is
  (`local network only; see CONFIG network, CGNAT.`), and so is the
  console, for the window and for the published default.
- **A closed board stays listed, marked closed** (Rob): heartbeats go on
  with `"closed": true` and the directory shows it as temporarily closed;
  1.1.0 held it and a long close restarted its waiting period. The
  published default is still never announced. `ANNOUNCE` and `ANNOUNCE
  TEST` show it.
- **A TZ string the board cannot read is refused** (TZ-bad): newlib's
  tzset gives up on one and runs unnamed UTC, silently. CONFIG refuses it
  on the row; a file read at boot or restored drops that line, logged. A
  file's line newlib reads correctly with something after it (a `;` note
  by hand) is kept, as newlib would run it, and the tail named in the log.
- CONFIG refuses `activity_led_gpio` on the card slot's pins on the
  ESP32-CAM and the S3, as the next read would have dropped it.
- The backup test build (`esp32dev_backuptest`) compiles without warnings.
- The drive light on every storage path: users, the caller log and its
  card mirror, system.cfg, the zip and backup staging, chat's mail, forums,
  files, info, rings, reboots.log and the camera's photos (`core/disk.h`).
- HARDWARE and SYS take what the board has running once, when the list
  starts, so a card or camera arriving at `[More]` cannot repeat or skip
  the capability line.
- A WROOM backup restored onto the ESP32-CAM or the S3 carries
  `activity_led_gpio` on the card slot's pins (2 is the ESP32-CAM's MISO):
  that line is dropped at boot, logged, and the board's own LED kept. The
  sd plugin's own pin settings are untouched.

**Flash, every board** (Rob, from `internal/memory-2026-09-25-1.1.1.md`):
IPv6, Wi-Fi SoftAP and WPA2-Enterprise off in `sdkconfig.defaults`. Every
socket is IPv4, the board is only ever a station, and nothing sets an
802.1X identity.

**Tests and tools**
- `tools/harness.sh`: a budget per test (900 s, `--test-timeout=N`) instead
  of one hour for the whole run, which a card run of six groups outgrew; a
  test that runs out or raises fails by name and the run goes on.
- The lights tests follow the board profile (pins, range, what ships on).
- `test_closed_configured` takes an explicit `closed = no` (a restore
  writes one by design) as an open board.
- `test_announce_directory` reads the board list at `/directory` (site
  1.3.0 moved it); passes against the directory at 1.3.10.
- The camera tests run on the ESP32-CAM profile too (its OV2640, its flash
  on GPIO 4), and `board_espcam` exists, as `harness.sh --help` promised.
- `make test`: `test_calllog` links again (`diskPulse` stubbed).
- `copy_data` skips the board's own temp files, which could vanish mid
  copy and take a test down.
- New: `test_link_line`, `test_room_private`, `test_screens_install`,
  `test_cgnat_local`, `test_announce_closed`, `test_config_tz_bad`,
  `test_lights_disk`, `test_board_espcam`; checks in `test_busy`,
  `test_hardware`, `test_fx_codes`, `test_camera`, `test_tzones`.

**Not in 1.1.1**: the badge pick-list for announce (estimated at a day of
its own: a scrolling checklist widget at 40, 80 and in plain ASCII, the
generator from the directory's `badges.json` and the CONFIG hook), left for
Rob to schedule. Newlib nano printf, silent assertions and `.bss` in PSRAM
are 1.2.0.

## 1.1.1-dev.1 (FNCAM 1.0.5, ESPCAM 1.0.2: the picture), 2026-09-25

The first outdoor photo on the Freenove came out white, median 247 of 255
on every channel, and Brightness -2, Contrast 2 and Exposure -2 changed
nothing. Host-tested; waits for the board to be back on USB.

- **The camera waits for the exposure to settle.** It comes up from cold
  for every photo, and the GC0308 starts on its default exposure, which
  outdoors is many times too long. Three frames were waited, before its
  auto exposure had moved. Now the sensor's own frame average (Y_average,
  0xD4) is read each frame against its target (0xD3), up to 2.5 s.
- **The GC0308's settings, written by the board.** esp32-camera 2.1.7's
  driver has contrast, saturation and the exposure target, but its
  brightness is `set_dummy`. All four are written after the driver's own
  (GC0308 datasheet, page 0: 0xB5, 0xB3, 0xB1/0xB2, 0xD3).
- **Auto levels and gamma** on CONFIG camera's Picture page, on any
  sensor, on the worker before the encode: three 256-entry tables from a
  histogram, never a second frame.
- **The size list follows the sensor**: up to VGA on a GC0308, UXGA on an
  OV2640. A saved size too big for the sensor is used as the largest it
  gives, with one log line, and kept in the file. CAMERA and PLUGINS name
  the sensor, or say none was found, or none looked for yet.
- The OV2640's own JPEG is still what is saved when there is nothing to
  do to it; with the watermark or a correction it is decoded a strip at a
  time and encoded again, on the worker, now at quality 90 (it was 83).
- **The OV2640's green cast.** `set_awb_gain` was given `wb ? 1 : 0`, so
  with White on auto the white balance was measured and never applied.
  AWB gain, AEC, AEC2 and AGC are all on at every bring-up now, and the
  OV2640 is settled by its own exposure and gain (sensor bank 0x45,
  0x10, 0x04 and 0x00) holding still, up to 1.5 s. Its lens shading, raw
  gamma, black and white pixel correction and DCW are on.
- **FILES marked Photos and Timelapse "(staff)" with Photos = all** (Rob,
  on PixelBBS). The marker read the photo areas' placeholder level, which
  is Sysop and never used; access itself was right. The marker and the
  check now take the level from one helper (`areaRead`).
- **The watermark carries the µnleashed wordmark** bottom left, the site's
  own drawing (tools/mkbrand.py writes camera_brand.h from LOGO_ROWS),
  white blended in at 37.5%, the stamp text's height; dropped when it
  would come near the text. Watermark = no removes both.
- JPEG quality 10 as shipped (12 before), with one step down logged if a
  frame will not come whole. XCLK stays per sensor: 20 MHz for a sensor
  that encodes (the OV2640), 10 MHz for a raw one (the GC0308).
- Measured on the ESP32-CAM (1.1.1-dev.0 base, 160 MHz): GC0308 at VGA
  settled in 6 frames (average 71, target 72), 5.8 s a snap with levels
  (0.35 s of it the correction); OV2640 at UXGA 13.1 s with levels (the
  eighth-size histogram 0.8 s) against 10.5 s without. No slow pass in
  any of them.

## 1.1.1-dev.0 (ESPCAM 1.0.1), 2026-09-25, pre-release

**Out early for testing.** A development build, published as a GitHub
pre-release so the web installer can offer the AI-Thinker ESP32-CAM as a
preview. It has not been through the full regression: it had targeted
sanity runs only (login, shell and storage, with and without a card, and
the ESP32-CAM host profile). The WROOM, the Waveshare S3 and the Freenove
stay on the 1.1.0 release on the installer; their images in this
pre-release carry the core changes below.

`tools/release.py` builds the new board as a fourth set, `esp32-cam-*` in
the release assets and `esp32-cam/` in the install layout, chipFamily
ESP32 like the Freenove's, so the installer's picker has to ask which
board rather than read it off the chip. The core changes, on every board:

- **240 MHz on every board** (Rob). `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`
  in `sdkconfig.defaults`, the IDF default being 160; each generated
  `sdkconfig.<env>` checked to say 240. Rule no. 1 is paid in CPU time as
  well as design. The snap time, SYS's loop figures and slow passes are
  measured on the bench after the flash.
  A chip rated for 160 MHz (the D2WD and single-core parts) will not boot
  it: the bootloader refuses. None of the supported boards carries one.
- **`HARDWARE` (`HW`), for every caller** (Rob: "so others can see how
  neat it is"): the chip and its revision, named as esptool names it
  (`ESP32-D0WD-V3` on a WROOM-32E), the cores and the clock they run at
  now (read from the clock, so 240 means 240), flash, PSRAM, the board and
  its version, and what it has running (a card and its size and bus, a
  camera and its sensor, the LCD panel, lights once a pin is wired). Staff also see
  PSRAM free, internal heap free and lowest, and card free. Never the
  network. On the `? account` menu after `ABOUT`, with long help.
  `plat::chipInfo` reads it all from registers and counters; the card is
  the sd plugin's kept figure, never its FAT.
- **SYS gains a hardware section**, last, drawn by the same code, less the
  heap rows its memory section already has.
- ABOUT's built-in card (a board with no `screens/about.*`) says `HARDWARE`
  shows what the board runs on. The stock about screens do not yet: that
  line is the screen artist's to add.
- A board profile with a card slot of its own says so (`BBS_HAS_SD_SLOT`),
  and the card's rounding to its printed size moved from announce to
  `backup.h` (`sdCardGB`) so HARDWARE and the directory badge agree.

### ESPCAM 1.0.1 (AI-Thinker ESP32-CAM profile)

A new board profile, `BBS_BOARD_AI_ESP32CAM` (envs `esp32cam_aithinker`
and `esp32cam_aithinker_release`). **On hardware**
(an Aideepen ESP32-CAM on an ESP32-CAM-MB, COM15): ESP32-D0WDQ6 rev 1.0,
4 MB flash, PSRAM (8 MB chip, 4 MB mapped), a genuine OV2640 (SCCB 0x30,
PID 0x26, VER 0x42). SNAPSHOT at UXGA with the watermark and the flash on
GPIO4, downloaded by YMODEM, and FILES area 12.

- The card runs over **SPI**, not SDMMC. In one-bit mode IDF 5.3.1 leaves
  D3 (GPIO13) floating at CMD0 (it drives D3 high only for four bits and
  up, sdmmc_host.c 600-614), the card takes that as SPI mode, and ACMD41 is
  never answered: two cards, every width and speed. SPI mounted at once.
- **GPIO4 (the white flash LED) is held low from start-up** by a
  constructor (`BBS_PINS_HOLD_LOW`). Nothing on the board holds the
  transistor's base low, so a floating or pulled-up pin lights the LED.
- The red LED on GPIO33 is the activity LED, which lights on a low pin
  (`BBS_LED_ACTIVE_LOW`, new).
- **No BOOT button**: GPIO0 is the camera clock, so the BOOT-hold resets
  and the backup window's button are off (`BBS_BOOT_GPIO` and
  `BBS_BACKUP_GPIO` -1), said on CONFIG backup's Button row.
- No revision minimum in its sdkconfig layer (`sdkconfig.defaults.espcam`),
  since ESP32-CAMs still carry revision 1 chips: the PSRAM cache workaround
  is in, as on no other profile.
- Flashing needs the SD card out: GPIO2 is the card's MISO and a
  download-mode strap, and a seated card holds it high at reset.
- Measured: a UXGA snap took 10.6 s (bring-up 854 ms, the frame 320 ms,
  the watermark's decode and re-encode plus the card write 9,317 ms), with
  no slow pass during it. Internal heap fell to 9,159 with the camera up.
  XGA is the size as shipped.
- The WROOM, S3 and Freenove images: the shared changes (BOOT and backup
  button defines, the LED polarity) are compiled out on every other profile.

## 1.1.0, 2026-09-25

**Out early for testing.** 1.1.0 has not been through the full regression
yet. It is released after targeted sanity runs so it can be tried on real
boards now; the full regression and a memory report run next, and 1.1.1
patches whatever they find.

In short:
- A new board is closed to callers until its sysop opens it.
- Recovery without a reflash: hold BOOT to reset the sysop password or
  the whole board, a new Wi-Fi network that fails falls back to the last
  one that worked, and a frozen board restarts itself.
- The listening port is a setting, and the web installer recognises a
  board already running µnleashed.
- The lights plugin (a drive light and a ten-pixel strip) and silent mode.
- OPERATOR, the sysop page, and notices that reach callers inside chat,
  the forums, files and mail.
- Backups on the SD card, a restore that waits for a quiet board, and the
  timezone picked by name.
- Forms and CONFIG laid out for 80 columns as well as 40, the sysop's
  dashboard reworked, and missed pages mailed to one account.
- Two more boards: the Waveshare ESP32-S3-LCD-1.47 (S3 1.1.1) and the
  Freenove ESP32-WROVER camera board (FNCAM 1.0.3).
- GPL v3 or later.
- The directory learns the SD card's size.

**The release build.**
- **Closed until opened** (Rob). CONFIG board gains "Stop taking calls"
  (`closed`, "Closed" at 40 columns), the page's last row. While it is
  yes:
  - every caller gets the busy line's sign and countdown, worded "Closed
    by the sysop for now", or `screens/closed.*` when a board has one;
  - a key during the countdown opens a login that takes one account, the
    sysop's (CONFIG board's Sysop, else the last to elevate, else an
    account the sysop password marked, else the first account on the
    board); any other handle, with an account or without, is told "Closed
    by the sysop. Call again later." before any password, and the line
    drops. A login already at its password when the board closed is
    asked again before it is let in;
  - no sign-ups and no guests, except that a board with no accounts at
    all shows its first caller no sign: they register, and that account
    is the one let in afterwards;
  - announce holds the listing, as it does on the published default,
    and SYS's traffic section says `Callers  closed`.
- With no `closed` line the board is closed exactly while it is on the
  published default password. **A board whose sysop already chose a
  password, which is every board set up before 1.1.0, stays open when it
  is upgraded.** The first CONFIG save that writes anything, and the end
  of the setup, write `closed = yes` out, so choosing a password never
  opens a fresh board by itself. The BOOT password reset writes out the
  closed state it found, so a running board whose sysop lost the password
  does not come back shut, and a restored system.cfg with no closed line
  is given the board's live state, so a restore never opens or shuts a
  board by itself. A `closed` line the parser cannot read is a problem and
  leaves the default standing, never an open board.
- A board whose system.cfg spells the published password out (a restore
  on 1.0.0 or 1.0.1 could write one) reads as on the default since 1.1.0,
  and so starts closed. Its sysop's account still gets in.
- The setup says so before the password step ("It stays closed to callers
  until you open it in CONFIG board"), the setup screen has a "Closed until
  you open it" section, and the end of the setup, and every sysop arrival
  while the board is closed, says how to open it. The sysop's account
  answering its login question on a closed board goes on to CONFIG board,
  on the row that opens it.
- Fixed: stopping the setup screen (Space, ESC or BREAK) left the setup
  armed, and the next screen the new sysop played ended by opening
  CONFIG staff.
- The setup form's Sysop row, on a board still on the published default,
  says its stars are that default and must be changed now; the setup
  screen says the same.
- announce sends `sd`, the card's size in GB rounded up to what is
  printed on it (1, 2, 4 ... 1024), only while a card is mounted.
- The directory tests expect its short badge codes (`ltrcy`, `elctr`),
  and a board profile's version in the system badge.
- Board versions: S3 1.1.1 (silent mode reached the panel after 1.1.0),
  FNCAM 1.0.3 (silent mode reached the camera's flash after 1.0.2).

**Left for 1.1.1**, with what the full regression and the memory report
find:
- The drive light on every storage path: `plat::diskPulse` in users,
  the caller log and its card mirror, system.cfg, the zip and backup
  staging, chat's mail, forums, files, info and reboots.log (PLAN-1.1.0
  Phase 5, lights).
- CONFIG checks a typed custom POSIX TZ string and refuses a bad one
  (TZ-bad).
- The badge pick-list in CONFIG announce, generated from the directory's
  badges.json.
- The harness's one-hour limit, too short for six groups with a card.
- On a fresh closed board the first account to register is the one let
  in. If a stranger beats the owner to it (a port forwarded before setup),
  the way back is the BOOT factory reset. Proposed: while the password is
  still the published default, also let a caller on the board's own
  network log in. Rob's call.
- Closing stops the heartbeats, so a long close can let the directory
  expire a listing and restart its waiting period: the maintenance window
  problem, to be designed with it.
- The lights tests assume the reference board's lights and fail on the S3
  and Freenove host profiles (as they did on 1.1.0-dev.15): profile-aware
  expectations.

The dev builds, as they landed:

**1.1.0-dev.0, the foundation.**
- Static RAM: 20,528 bytes free, up from 4,016. The backup's export and
  import state share one buffer, the serial bridge allocates its buffer
  when it starts, the zip code's config checks reuse the config parser's
  scratch, and 22 account records that sat in static RAM for a moment's
  use now live on the stack.
- The BBS task stack is 12 KB (from 8 KB). SYS shows the least it has
  ever had free out of 12,288, and every new low is logged to the console
  with what the board was doing at the time.

**1.1.0-dev.1, network and installer.**
- CONFIG's Wi-Fi page is now **network** (`CONFIG wifi` still opens it)
  and adds **Port**, the port the board listens on. The default is 6400,
  the range 1 to 65535, it is used from the next restart, and CONFIG
  refuses it if it equals the backup port. A home router that can only
  forward a port to the same port can now reach a second board. mDNS, the
  console's dial-in line, Improv's telnet address, SYS and announce all
  follow it.
- Announce's "Port" is now **Outside**: the port callers dial through
  your router. Left empty, it sends the listening port.
- Improv answers from early in boot, so the web installer, which gives up
  1.5 s after opening the port (and opening the port resets the board),
  recognises a board already running µnleashed and offers Update. When
  the board joins Wi-Fi it says so unprompted, and an open installer dialog
  switches to Change Wi-Fi.
- Fixed: a form's field note carried into the next form, so sign-up's
  "Only you and staff" line showed under PROFILE.
- The privacy screen is drawn at 80 columns for ANSI terminals (it was
  40-column art on an 80-column screen), in the same frame as the rules
  and welcome screens. The words are unchanged, and PETSCII and ASCII are
  byte-identical.
- Carries the 1.0.2 security fix.

**1.1.0-dev.3, recovery without a reflash.**
- Hold BOOT after a restart (press RESET, let go, then hold BOOT within
  10 s). What happens depends on how long it was held when you let go:
  under 7 s, nothing (the activity LED blinks slowly while it counts); 7
  to 15 s puts the sysop password back to the published default (the LED
  flashes fast); 15 to 20 s is a factory reset of accounts, settings and
  logs, leaving the screens, the firmware and the SD card alone (the LED
  is solid); held to 20 s abandons it (the LED goes off). Both actions
  restart the board, and the next staff login says why. A factory reset
  also costs the directory listing unless a backup is restored afterwards.
- The board remembers the last network it joined. A network changed in
  CONFIG that has not joined within a minute of boot is given up, and the
  board goes back to the last one that worked and logs it.
- The task watchdog now restarts a board whose loop has wedged, instead
  of leaving it answering nothing until somebody pulls the plug. The
  restart shows as "task watchdog" in the restart log and at staff login.
- Fixed: the crash notice at staff login ran its two sentences into one
  line, and never showed on the sysop node.

**1.1.0-dev.4, lights.**
- A new plugin, `lights`: two WS2812B outputs driven by the RMT peripheral,
  off until switched on and given pins (`CONFIG lights`).
- A drive light: amber for the card, cool white for the board's flash, a
  slow red blink after a storage error, a dim glow at rest, in the styles
  `pc`, `1541`, `disk2` and `breathe`.
- A strip of ten: `nodes` (one pixel per caller line, in its WHO rank
  colour), `hayes` (a Smartmodem's front panel from the board's real
  state), `blinken`, `scanner`, `c64`, `boing`, `vu`, `rainbow`, and
  `manual`, where each pixel has its own effect and colour.
- Brightness is a percentage per output, 1 to 30, 10 as shipped. 30 is a
  ceiling in the firmware, not only on the form.
- `LIGHTS` shows what each output was last sent; `LIGHTS TEST` shows red,
  green, blue and white for checking the wiring.
- CONFIG: a plugin pin whose range starts at -1 takes -1 as off, and two
  pins on one page cannot be the same pin. Two new setting kinds for
  plugins: a cycle field, and a button to a page of rows, which the lights
  use for their ten pixels.
- Forms: the same letter twice steps to the next choice starting with it,
  so `c` twice on a level is co1. Plain ASCII line mode saved every such
  pick as a single space; it keeps the pick now.

**1.1.0-dev.5, the sysop page, and notices wherever you are.**
- **OPERATOR** (`O`, and `/o` in the chat room) rings for the sysop, with
  a reason typed after it or asked for. A spinner runs for up to 45 s and
  any key stops it. The sysop gets a bell and a flashing RING wherever
  they are, then `[A]nswer [D]ecline [X] Away [Q] Later`; in the room it
  is two lines with `/o` and `/o-`, and in a form it is the status line.
  Answering puts both callers in the chat room talking only to each other.
  An unanswered ring leaves a note, up to 8, kept through a restart and
  shown once at the sysop's next login or elevation. One ring every 3
  minutes, three a call, one at a time on the board. A hidden or lurking
  sysop is answered exactly as an absent one, in words and in timing.
- **Notices reach you wherever you are.** Pages, broadcasts, SHUTDOWN's
  countdown, "You have mail" and arrivals now reach callers in the chat
  room, the forums, the file areas, the mailbox and the page editor, with
  the input line lifted and put back as typed. Broadcasts and rings also
  reach a form's status line. Plugins get two new hooks for it,
  `liftInput` and `restoreInput`.
- **Bells:** an arrival rings a bell wherever it reaches you; `BELL` or
  `/b` silences it.
- Fixed: SHUTDOWN announced thresholds it had already passed
  (`SHUTDOWN 20` said 120, 60 and 30 seconds first). In a sticky private
  conversation, room lines landed after the `[>n]` marker and the marker
  could be drawn twice. The sysop's node shows as `[>S]`, not `[>0]`.
  FILES no longer carries a half-answered question into the next visit.

**1.1.0-dev.6, backups on the SD card, and the timezone by name.**
- `BACKUP SD` writes the backup window's zip to the card
  (`backup/unleashed-YYYYMMDD-HHMM.zip`), a step each loop pass with a dot
  a file, so callers are not held up. `BACKUP SD SCREENS` writes the
  screens alone.
- `RESTORE SD` lists the card's backups, newest first. `RESTORE SD n`
  checks one exactly as an upload through the backup window is checked,
  shows what it replaces (accounts, screens, staff passwords, and the
  Wi-Fi network when it differs) and restores on Y. `RESTORE SD SCREENS n`
  puts screens into the card's screens folder, never touching the ones in
  flash, and never removes anything.
- Nightly backups: `nightly = yes` on `CONFIG sd` makes one at 03:00 and
  keeps the last seven `nightly-YYYYMMDD.zip`. Backups made by hand are
  never pruned. A missed night is logged and told to staff when they
  arrive.
- **Fixed: restoring a backup through the backup window deleted every
  account on a real board**, from 0.14.0 on. The upload was unpacked on
  the screens partition and moved to the accounts partition, which the
  ESP32 refuses between partitions; the fallback then deleted the live
  accounts file and failed the same way. Restores now unpack on the
  accounts partition, and nothing removes a live file before its
  replacement is whole. (Also shipped on its own as 1.0.3.)
- Backup limits are 256 KB for the zip and 256 KB unpacked, checked
  against the room actually free: "Too big" and "Board full" say which.
- The information pages travel in the backup zip. A restore that brings
  back system.cfg or a page restarts the plugins, so it is live at once.
- The backup window's restore goes live a file a loop pass, not in one
  stall.
- `CONFIG board`: Timezone is picked by name from 34 zones, or Custom,
  above the TZ string it sets.
- Plain ASCII: every cycle field lists its choices numbered, and a number
  picks one.
- `SD UNMOUNT` and a pin change let go of a backup being written first.

**1.1.0-dev.7, fixes.**
- Settings are written over system.cfg by a rename, never by removing
  the old file first, so a failed write, a BOOT password reset's
  included, leaves the old file whole.
- A system.cfg that spells out the published sysop password is treated
  as the default: local network only, the listing held, setup offered.
  This heals boards a 1.0.0 or 1.0.1 restore wrote it into.
- Every pin setting refuses GPIOs the chip does not have (on a WROOM:
  20, 24 and 28 to 31), from the chip's own list, and says "This chip has
  no such pin."
- Restarts: "USB reset" is named (S3), nothing claims the RESET button is
  "reset pin", and a factory reset whose erase failed restarts as
  "factory reset FAILED" rather than a plain software restart. New
  console lines for the password band's restart, the factory band, and
  the Wi-Fi fallback, including the same network with a new password.
- The user manager's D says Retire, as `USER DEL` does.

**1.1.0-dev.8, a second board: the Waveshare ESP32-S3-LCD-1.47.**
- A build profile of its own, `ws_s3_lcd147`, never a fork. Everything
  specific to the board (the screen, its pins, PSRAM) is behind the
  board's defines, so none of it is compiled into the ESP32 image, which
  grew 1.3 KB of flash and 72 bytes of static RAM for the features below.
- The board carries its own version beside the core's and shows both:
  `1.1.0-dev.8 (S3 1.0.0)`. The directory still compares the core number.
- The screen shows the board's name, callers on, the address, uptime, the
  card and the last events, from the `panel` plugin, with a CONFIG page
  for its pins. The RGB LED on the board is the drive light.
- The TF slot is the SD card, with no wiring.
- Wi-Fi and lwIP buffers live in PSRAM, which is what leaves the plugins
  room on this chip; the plugin reserve is 16 KB on a board with PSRAM.
- Pin settings refuse the S3's flash, PSRAM and USB pins.
- Lights, on every board: the strip takes 1 to 16 pixels, each output has
  a colour order, a `wifi` strip effect shows the signal as a meter, and
  brightness goes to 100%.
- Releases carry an image set for each chip, each with its own manifest,
  and a tag with a `-` in it is published as a pre-release.
- On hardware: flashed to the S3 from COM12 (flash #2): it boots, joins,
  mounts the card, and the screen and LED work. The panel's redesign is
  still to come.

**1.1.0-dev.9, backups you can reach, and a restore that waits.**
- A Backups file area, number 11 in FILES, the sysop's alone: the card's
  backup folder. Download a backup by YMODEM or XMODEM; a .zip sent there
  goes straight in, with no approval, ready for `RESTORE SD`. At the area
  menu, `#`, a number and Enter reaches an area past 10. A zip with
  XMODEM's 0x1A padding after its end still restores.
- A restore waits until nobody else is on. After the sysop's Y, at either
  door, it says how many callers it is waiting for; F puts it live with a
  warning to them, N gives it up, and it gives up by itself after
  `backup_window_minutes`. New callers get the busy line meanwhile, and
  curl is told the same as the sysop.
- `SCREENS` lists every screen with the size of its .ans, .asc and .seq,
  and whether callers get it from flash, the card's seeded copy or the
  sysop's own. `SCREENS VIEW name[.ext] [FLASH]` plays one. Staff only.
- A restore refuses a system.cfg that empties the sysop password, reports
  a co-sysop left off because their line named the published password,
  checks that restored screens fit while they are being swapped in, and
  lets go of every screen it replaces first.
- A caller on the welcome screen when the card is unmounted goes on to log
  in, rather than being stranded with no prompt.
- `RESTORE SD SCREENS` marks what it imports as the sysop's own, so a
  stock update never replaces it. Card screens seeded before 0.22.1 that
  nobody edited now follow the stock set.
- A backup left half written on the card is removed at the next mount, and
  a half-uploaded one no longer counts as an upload awaiting approval.
- A board with no card no longer probes for one at every CONFIG save;
  `SD MOUNT`, a pin change and a restart still look. `SD UNMOUNT` keeps the
  card out through CONFIG saves.
- Mail, information pages, FILES.BBS and the card's screen record are
  replaced by renaming over the old file, never by removing it first.

**1.1.0-dev.10 (S3 1.1.0), the S3's status panel, redesigned.**
- A phone-style status bar: the board's name, its address and its uptime
  with the card's free space turn every 3 s with a fade, and a caller
  ringing the sysop takes the slot. Beneath it, glyphs that appear only
  while true: the card, a ringing bell, sysop mail, an upload awaiting
  approval, the backup window, the directory listing, staff on, an
  unclean restart and a slow pass. Then a Wi-Fi antenna that fills with
  the signal, and the clock.
- Callers on now, one row each up to all ten, with the recent logins,
  logoffs and rings taking whatever rows are left; "+N more" past what
  fits. A system row, and the strip's lamps as square LEDs.
- CONFIG panel's "USB plug" setting turns the screen four ways: up (as
  before), left, right, down. Left and right are landscape, with a
  two-column layout. An old `rotation` line is still read.
- The sd plugin reports a card that is present but will not mount, and a
  recent failed read, for the panel's card glyph.
- The ESP32 image is unchanged by this: all of it is behind the board's
  defines.

**1.1.0-dev.11, GPL v3 or later.**
- µnleashed BBS is now under the GNU General Public License, version 3 or
  later (was version 2 or later). Robert Mech holds the whole copyright
  and made the change on 2026-09-24. The firmware links Apache-2.0 code
  (ESP-IDF, espressif/mdns, and the camera driver to come), and Apache-2.0
  combines cleanly with GPLv3 but not with GPLv2.
- `LICENSE` is the GPLv3 text; every SPDX line is `GPL-3.0-or-later` and
  every file notice says version 3. ABOUT, the welcome and goodbye screens
  and THIRD_PARTY_NOTICES say v3.
- `tools/release.py` refuses a release while any tracked file still
  carries a GPL-2.0 SPDX line, and `make test` in `host/` checks the same,
  so a file added later on an old header fails.
- Nothing else changed; the firmware behaves exactly as dev.10.

**1.1.0-dev.12, forms at 80 columns.**
- Forms have two layouts. Under 80 columns it is the 40-column card. At 80
  and up: long labels, a 56-column box, the profile as two rows of 74 and a
  fuller hint. Plain ASCII uses the long labels and shows 60 characters of
  a value.
- CONFIG board: "Board LED" (Onboard LED GPIO at 80) and "Guest min". Plugin
  page titles in capitals; the page list indented. The ANSI form hint at
  40 fits its line.
- CONFIG serial: RX, TX, baud and format. CONFIG chat: every setting, and
  the colours on a Colours page.
- CONFIG forums shows the topics set and one empty row, growing to twelve,
  then a Topics button to all sixteen.
- CONFIG refuses a pin anything else on the board holds and names the
  holder.
- Lights brightness is 1 to 100. Past 30 CONFIG asks first and says why:
  "Strip % over 30 may brown out the board without a 5 V supply. Keep it?
  (y/N)".
- Plain ASCII: "-" empties a set password, so an open network can be chosen.
- Fixed: announce's description was cut to 95 characters when saved in
  CONFIG; a forum topic packed longer than 95 characters lost its Reply and
  Moderate levels when saved from its page; WHOIS drew a 39-column card at
  80; form refusals were cut at 60 characters; a full 80-column redraw
  could lose its tail (keys now wait for 2,600 bytes of output room).

**1.1.0-dev.13, the sysop's dashboard, and missed pages to one account.**
- DASH is a real screen, laid out for 40, 80 and 132 columns, with pages
  to move between and K/S to pick; NODES and WHO share its node rows.
- A calls-today count kept in RAM, a "Last restart" row on SYS, and board
  notices shown after setup.
- Missed sysop pages are mailed to one account: CONFIG board's Sysop,
  held by account id so a rename keeps the mail and a new account taking
  the old name does not get it, or else the last account to elevate to
  sysop. First-boot setup names the account that set the board up.
- The sysop's account is asked for the sysop password at login: Enter
  skips; it is BYE's own check and ban count, and an account login never
  grants staff by itself.
- The S3 display's mail envelope follows that account from boot.
- MEM reads the card's free space from the sd plugin's cache.


**1.1.0-dev.14, silent mode.**
- CONFIG board's Silent switch and optional silent hours (from and until,
  HH:MM local, may cross midnight, waiting for NTP) put out the activity
  LED, both lights outputs and the S3 panel's backlight. Each light keeps
  its settings and returns exactly as it was; the panel redraws the whole
  glass before relighting. SYS shows whether the board is silent and why.
  The power LED is on 3V3 and cannot be switched off: tape it or lift it.
- `board::silent()` is the one-byte query every light asks, the camera's
  flash included.
- A half range or a bad time in system.cfg is read as no hours and
  logged, never a refusal of the whole file.

**Freenove ESP32-WROVER CAM (FNCAM 1.0.0).**
- Phase 1: a new board profile, `BBS_BOARD_FN_WROVER_CAM` (environments
  `freenove_wrover_cam` / `freenove_wrover_cam_release`), on the
  ESP32-WROVER-E's 8 MB quad PSRAM (`sdkconfig.defaults.fncam`, ESP32
  revision 3 minimum). `CONFIG` refuses a pin the board has already wired
  to its PSRAM, its console, the card slot or the camera, the same way it
  refuses one another switched-on plugin holds. No NeoPixel is documented
  on the FNK0060 (checked against Freenove's own pinout drawing and
  sketches, 2026-09-24), so the lights plugin ships off with no drive pin,
  as on the WROOM.
- Phase 2: the card slot is SDMMC 1-bit (CLK 14, CMD 15, D0 2) rather than
  SPI, because the WROOM's four default SPI pins are this board's camera
  data lines. The `sd` plugin's SPI pin settings are read and ignored,
  logged once, instead of being refused.
- The camera plugin (`BBS_HAS_CAMERA`): `SNAPSHOT` for a caller and a
  timelapse of the board's own, into two new file areas, Photos (12) and
  Timelapse (13). Ten photos an hour and twenty a day per caller, rolling
  and sysop-exempt. Retention by age, by count and by a card space floor,
  touching only files shaped exactly like the camera's own. A watermark
  drawn with the camera driver's own JPEG encoder (`espressif/esp32-camera`
  2.1.7, Apache-2.0) and decoded with the chip's own ROM TJpgDec. Bringing
  the sensor up, taking the frame and writing the card all run on a worker
  task off the BBS loop, so a photo adds no lag for anyone else on the
  board. Full detail: COMMANDS.md, `camera` under Plugins.
- Host-tested only so far. Of the three phases, only phase 1 (the board
  profile and PSRAM) has actually been flashed and run on the physical
  board; the SD card and the camera plugin have not yet had bench time.

**FNCAM 1.0.1: the camera could not start on the board.**
- On the bench FNCAM 1.0.0 answered the first `SNAPSHOT` with "No photo:
  the camera would not start" and the next with "The camera needs memory
  the board is using". MEM showed 38,855 bytes of internal RAM free and a
  largest block of 31,744, and the camera driver needs one 32,768-byte
  internal DMA buffer. Enabling PSRAM had cost the internal RAM: the IDF
  forces 16 static Wi-Fi TX buffers when `SPIRAM_USE_MALLOC` is on and
  raises the static RX buffers from 10 to 16, about 35 KB more than the
  WROOM holds, and the 32 KB reserve pool that ordinary `malloc()` never
  takes is itself too small for the block once the heap's header is in
  it. Now ten of each Wi-Fi buffer and a 40 KB pool
  (`sdkconfig.defaults.fncam`).
- The check before a snap counts the worker task's stack and the driver's
  task as well as the DMA block, and is made again on the worker before
  the driver is touched. A refusal, a failed start and a successful one
  all log internal RAM free and the largest DMA block. A sensor that does
  not answer is reported as "no camera found" (the driver says
  `ESP_ERR_NOT_SUPPORTED`, which was reported as "would not start"), and a
  partial start is torn down only when something of it is left.
- No "Smile...": the caller sees the spinner, "Developing..." and the
  result.

**FNCAM 1.0.2: the camera is a GC0308, and it works.**
- With the memory there, the driver still found no sensor. A raw SCCB scan
  on the bench (a diagnostic build, not kept) found one device, at 0x21,
  with 0x9B at register 0x00: a GalaxyCore GC0308, not the OV2640
  Freenove's documents name. It is 640x480 at most and has no JPEG
  encoder, which the whole capture path had assumed.
- Both drivers are built (GC0308 and OV2640). The bring-up asks for JPEG
  and, when the sensor cannot give it, asks again for RGB565 and remembers
  that for the rest of the boot. A raw frame is copied off the sensor and
  encoded on the worker, sixteen rows at a time, by the same encoder the
  watermark already used, with the watermark drawn on the way.
- Raw frames run the sensor clock at 10 MHz: at 20 every frame was lost
  (the driver's `EV-EOF-OVF`).
- Size is `qvga | vga`, `vga` as shipped. The watermark is fitted to the
  frame the sensor actually gave, and CAMERA names the sensor found.
- The worker's stack is 8 KB; encoding left 2,008 of 6 KB free.
- On the board: 640x480, about 33 KB a photo, 3.6 to 3.9 s from SNAPSHOT
  to saved, downloaded by YMODEM, listed in FILES 12, two in a row. With
  the camera up, 14 KB of internal RAM is free.


**1.1.0-dev.15 (FNCAM 1.0.2), the camera.**
- A third board: the Freenove ESP32-WROVER camera board, with PSRAM, the
  card slot over SDMMC, and a camera plugin. `SNAPSHOT` takes a photo at
  once and offers "Download it now?"; Photos and Timelapse are file areas
  12 and 13; limits of 10 an hour and 20 a day per caller; naming, age,
  count and card-space retention; a small who-and-when watermark.
- The sensor on Rob's board is a GalaxyCore GC0308 (640x480, no JPEG of
  its own), not the OV2640 Freenove documents: frames are captured raw and
  encoded on a worker task below the BBS loop, so nobody lags.
- The flash (pixel or a pin driven high, with a lead time) stays dark in
  silent mode.
- announce sends `camera` in features while a sensor answered this boot.

## 1.0.2, 2026-09-23

A security fix. Restoring a backup could turn the published default sysop
password into a real one.

- **What went wrong.** A board fresh from the installer, or one whose sysop
  has not chosen a password yet, runs on the published default,
  `unleashed`. It accepts that password only from its own network, and it
  keeps itself off the directory until the sysop chooses one. Both rules
  rest on one thing: `system.cfg` having no `sysop_password` line.
  A backup never carries a staff password. The download writes
  `sysop_password = ***`, and a restore puts the board's current password
  back in its place. On a board still on the default, the current password
  is the default, so the restore wrote `sysop_password = unleashed` into
  the file. From then on the board treated it as a password somebody had
  chosen: it worked from any address, and the board listed itself on the
  directory.
- **How it happened in practice.** You flash a new board, log in from home
  with the default, and restore the backup from your old board to bring
  your settings and accounts across. The board is now listed, and anybody
  who has read the install page can become its sysop from the internet.
- **The fix.** A restore never writes the published password. A staff
  password line that would carry it, whether it arrived as `***` or typed
  out, is left out of the file. For the sysop that keeps the board on the
  default: its own network only, off the directory, and setup offered to a
  local caller again. For a co-sysop it leaves that level off. When the
  board is still on the default afterwards, the restore says so, in curl's
  `Applied:` line and on the sysop console.
  A board with passwords of its own is unchanged: `***` keeps them, as
  before.
- **If you restored a backup on 1.0.0 or 1.0.1 before choosing a sysop
  password**, your board may still be using the published password as a
  real one, and updating to 1.0.2 does not change that by itself. Log in as
  sysop and set your own in `CONFIG staff`. A board whose `ANNOUNCE` shows
  no "Held" line although nobody ever chose a sysop password is one of
  these, and from 1.0.2 its serial console says so at boot:
  `cfg: sysop on the PUBLISHED password, from anywhere`. Restoring a backup
  again on 1.0.2 also puts it back on the default.
- Tests: a fresh board restores a backup with `***` staff passwords, and
  afterwards a caller from outside its network cannot use the default,
  the listing stays held and setup is offered again; a board with its own
  passwords keeps them; a `system.cfg` that names the published password
  is left without that line.

## 1.0.1, 2026-09-23

The directory's badges, sent by the board. From site 0.21 the directory
shows small badges under each board's name; until now only the ones it
works out itself could appear, because no firmware sent the rest.

- The announce plugin sends:
  - `system`: the machine the board runs on, read off the chip, e.g.
    "ESP32 · 4 MB". The sysop never types it.
  - `terminals`: ANSI, UTF-8, PETSCII and plain ASCII, always, for this
    firmware.
  - `guests`: the guest setting.
  - `features`: whichever of chat, mail, forums and files are running at
    that moment. A board without a card never claims forums or files.
  - `support` and `interests`: two new rows on CONFIG's announce page. A
    sysop types slugs from the directory's /badges page, separated by
    commas. They are tidied before sending: lower case, letters, digits
    and dashes only, 16 at most. The directory ignores any it does not
    know.
- The payload has room for all of it. The body, the request and the reply
  now share one buffer, big enough for the worst case (1,319 bytes with
  every value at full length and every character escaped). Static RAM
  went down 160 bytes on the way.
- `ANNOUNCE TEST` shows the new fields, and refuses while a heartbeat is
  in flight, because they share the buffer. A payload too big to send is
  reported as "It would be refused, nothing sent" rather than printed in
  part.
- A refused payload no longer opens a connection to the directory: the
  request is built before connecting, so nothing touches the network
  unless it can be sent whole.
- Forums and files count as running features only while a card is
  mounted, so `SD UNMOUNT` takes them off the next heartbeat. A card
  pulled without `SD UNMOUNT` is still not noticed until the next boot.
- A badge name typed with spaces, "Mental Health", is sent as
  `mental-health`: runs of spaces, underscores and dashes become one dash.
- Chat resets its mail settings when the configuration is reloaded, so
  taking `mail_slots` out of system.cfg brings mail back without a reboot.
- This release shipped on the code review plus targeted runs of
  announce, config, plugins and messaging, with and without a card: 135
  and 191, 172 and 244, no failures. The full regression runs after the
  tag, and anything it finds goes into 1.0.2.
- Tests: the suite's stand-in directory stops when told to, and a new
  end-to-end test runs the real directory server on 127.0.0.1, then checks
  that the badges are stored and shown.

The rest of what was planned as 1.0.1 moved to 1.0.2, so the badges could
ship on their own.

## 1.0.0, 2026-09-23

The first public release. 0.23.0 passed the test that mattered: a new
ESP32, flashed from the web installer by somebody following the page,
joined Wi-Fi through the browser, took its first login, walked its sysop
through changing the passwords, and ran as a board. 1.0.0 is that build
with the problems the test turned up fixed, the documentation brought
into line with the code, and the repository made fit to publish.

**CONFIG checks what it writes.** Every core setting now goes through the
parser's own rules before anything is saved, so the form refuses what
the board would refuse, instead of writing it and then failing to reload.
- A hostname typed as `name.local` is saved as `name`. Before, the file
  took it, the reload refused the whole file, and nothing else on that
  page went live.
- Also caught now: a backup port that clashes with the BBS port; a WHO
  minimum above its maximum; a `#` in a value where it would have started
  a comment; `***` typed as a password, which switched that staff level
  off; spaces at either end of a password or network name; and any text
  at all for the landing place.
- 0 is accepted for the idle, call, day and guest minute limits, meaning
  never or unlimited, and -1 for the LED and button pins, meaning none.
- The sysop password can no longer be cleared from CONFIG. An empty one
  switched staff off, with no way back short of reflashing.
- GPIO 6 to 11 are refused for every pin setting, because on the WROOM
  they are wired to the flash chip.
- A `;` is refused in a plugin's settings and in a file area's parts. A
  plugin reads a value only up to the first `;`, so an area named "Games;
  Demos" silently lost the permission levels written after its name and
  fell open to everybody, while CONFIG said "Saved and live".
- Emptying the Wi-Fi password on an unchanged network still saves, since
  that is how an open network is chosen, but CONFIG now says "Saved: OPEN
  network" rather than letting a stray keystroke pass unremarked.

**CONFIG says what happened.**
- Saving a plugin that needs an SD card, with no card mounted, now says
  so instead of "Saved and live".
- CONFIG wifi shows the network the board is actually on, even when it
  joined through the compiled-in fallback. It refuses a new network name
  unless the password is retyped, or cleared for an open network. Before,
  a changed name was saved with no password and the board fell off the
  network at the next boot.
- The staff page labels the rows Co-sysop1 and Co-sysop2; both used to
  read "Co-sysop".
- The first-boot setup screen no longer says to delete the stars before
  typing a new password.

**Release safety.** `tools/release.py` refuses to build a release if any
tracked file carries a copyright, licence or author line naming Anthropic
or Claude, or if a firmware image mentions either. The copyright is the
author's alone.

**Documentation.** The staff table in COMMANDS.md was one table broken by
prose, and GitHub rendered most of it as text; it is whole now. It was
also checked row by row against the source. PLUGINS.md lists every hook
in declaration order, and the settings table. A handful of stale figures
and prompts were corrected in README.md, USERS.md and PLUGINS.md. The
working notes moved from `reports/` to `internal/`, which says what they
are.

**The repository.** The history was rewritten once, before publication,
so that no commit carries a private address or network detail. Content
and order are unchanged; commit hashes before 1.0.0 are new.

Not in 1.0.0, and next: the BOOT-hold reset and the Wi-Fi fallback to the
last good network (1.0.1, both need bench time), the stack audit and the
watchdog reboot (1.0.1), and notices reaching callers inside plugins
(1.0.2).

## 0.23.0, 2026-09-23

The web installer's firmware: a board flashed by somebody else can be set
up by them. Rob's design, built to be flashed from /install and tested
there before 1.0.0.

- **One published default sysop password, `unleashed`**, used only while
  `system.cfg` has no `sysop_password` line at all (a present-but-empty line
  still means no sysop). Honoured only from the board's own network: from
  anywhere else `BYE unleashed` is a wrong password, ban count and all.
  Co-sysop passwords stay blank, and blank cannot be logged in with.
- **The setup flow.** Any caller on the local network who logs in or
  registers while the board is on the default is asked for the sysop
  password, no BYE needed. The right one makes them sysop, plays
  `screens/setup`, opens `CONFIG staff`, and after that form plays the
  paged `screens/newsysop` tour, then the prompt. A wrong one asks again;
  Enter on nothing asks again; only ESC (the left arrow on a Commodore)
  skips. The first draft let an empty Enter skip, and a stray Enter left
  over from the sign-up form threw the setup away; the second dropped keys
  typed while the question printed, and ate the first letters of the
  password. Both were found by the test, not by reading.
- **CONFIG refuses the published default as anybody's chosen password**:
  written to the file it would stop being "the default" and work from
  anywhere while still being on the install page.
- **Announce holds the directory listing** while the default is set, and
  ANNOUNCE and DASH say so.
- **A set password field no longer keeps its mask** (found by the screen
  artist): the eight stars sat in the buffer and typing appended to them,
  so a sysop who did not backspace first saved `********newpass`. The
  first key now replaces the value (`FF_REPLACE`). The setup test types
  over the mask without backspacing and fails on 0.22.3.
- **Four small CONFIG bugs** found by the web agent capturing the setup
  guide's screens: the backup window and WHO max accepted values the parser
  then ignored (both 1..60 now, as the parser says); plugin pages showed
  `; comment` text as part of a value; the page list wrapped two rows at
  40 columns; and `system.cfg.example` had stale comments.
- `localAddr` counts `100.64/10` as local, matching the backup port, so a
  sysop on Tailscale is local to both.
- **Releases.** `esp32dev_release` builds with `BBS_RELEASE`, which ignores
  `include/secrets.h` even when it is there. `tools/release.py` builds the
  five images from `data/screens` only (never `data/system.cfg`), checks the
  partition offsets against `partitions.csv`, searches every image for any
  password or network name the machine knows and refuses on a match, builds
  the licence notices from the exact packages used, and writes both the
  flat GitHub Release assets with `SHA256SUMS` and the directory server's
  `firmware/<ver>/esp32/` layout. Verified: both Wi-Fi values are in the
  developer build and in neither release image. `.github/workflows/release.yml`
  runs it on a `v*` tag and publishes the release.
- New `test_first_setup` (11 checks), run by `tools/harness.sh --fresh`, a
  board with no staff passwords.

## 0.22.3, 2026-09-23

The bug batch from Rob's first session on 0.22.1.

- **Staff screens no longer freeze the board.** `esp_littlefs_info` walks
  every block of every file (esp_littlefs.c:313 in the 1.22.3 component),
  about 85 ms a partition with the loop stopped; SYS asked for two, DASH for
  one every second, and every plugin write paid one in its reserve guard.
  The platform keeps each figure now: taken at boot, the screens partition
  kept until a backup restore says otherwise, user data refreshed at most
  once a minute. Rob's serial log is what named it: every slow pass after
  the BYE was `node 0 SYS`, 169-170 ms, four of four.
- **An effect is never split by the editor's wrap.** The wrap broke at the
  space inside `@BLINK:Special Effects@`, the halves were stored as two
  lines, and both printed as typed. `compose::wrapPoint` carries an open
  effect whole; forums, mail and the info pages all share it. Chat never
  had the bug (one line, wrapped only at the reader). Four unit checks.
- **A blank line between a notice and the reading prompt**, at every site
  with that shape (13), not only the end-of-subject one in Rob's
  screenshot. The check sits where the prompt follows the notice; the first
  draft checked where reading rolls on to a message, passed on the broken
  code, and was moved.
- **FX shows the message code beside each effect it demonstrates**, on the
  same line at 80 columns and under it at 40, and ends by pointing at
  CODES; **the CODES screen points at FX** in all three flavours. New
  `test_fx_codes`.
- **MAIL logs its own timing** when the mailbox takes over 20 ms, split into
  the sizing and drawing passes. It cost 186 ms on the board and nothing in
  the code explains that much; the host cannot show a flash cost, so the
  next serial log will say where it went. The 118 ms login in the same log
  is the deliberate 1,000-round password hash (it started about 80 ms before
  the login line), budgeted at under 100 ms; left alone.
- A stale AddressSanitizer build of `test_codes` looped on the known WSL
  DEADLYSIGNAL problem in 9 runs of 20 and looked like a hang. Its sources
  had not changed, so make never rebuilt it. Rebuilt plainly; stable.
- Targeted runs only this round, as Rob asked: messaging, shell and storage
  with a card (499 passed), messaging without one (172 passed), unit tests
  clean. Static DRAM 176,880 of 180,736 (3,856 free). Flash 74.8%.
- Not on hardware.

## 0.22.2, 2026-09-23

No code change: the version moves because every commit gets one. Records
Improv working on the board, and the bug batch found on it (CLAUDE.md, the
queue), including the cause of the "slow after BYE" report: SYS, MEM, DASH
and PLUGINS ask LittleFS how full it is, and that call walks every block of
every file, about 170 ms with the whole board stopped each time.

## 0.22.1, 2026-09-22

Improv, and the Wi-Fi network out of the source. Committed together with
0.22.0, which was built and tested but never committed on its own. Built to
NEXT.md part 3.

- **The network lives in `system.cfg`** as `wifi_ssid` and `wifi_password`,
  on `userdata`, so it survives a reflash. `include/secrets.h` is optional
  now and only a fallback when the config has none, so a published binary
  carries nobody's home network. Rob's board keeps working unchanged: its
  config has no Wi-Fi keys yet, so it falls back to `secrets.h`.
- **Improv Wi-Fi Serial** on the console UART: current state, device info,
  scan, and set network. A new network is tried for 30 seconds and saved
  only if it joins; if it does not, the board goes back to the one it had.
  Improv listens for as long as the board runs, not just at first boot. A
  board with no network says so on the console every 30 seconds.
- Written here, not taken from the official SDK: that SDK is Apache-2.0,
  which the FSF lists as incompatible with GPLv2. `src/core/improv.*` is
  the packets only and has 24 unit checks (`host/test_improv.cpp`), with
  the expected bytes worked by hand rather than produced by the codec.
- Checked against the browser side before writing the glue:
  `sdk-serial-js` only looks for a packet at the start of a line, so every
  packet goes out with a newline in front. It also resets on a 0x0A in the
  first nine bytes, which no packet this board sends can contain. ESP Web
  Tools waits 10 s for Improv after a flash and 45 s for a join.
- **Packets hold stdout's lock and do not go through stdout.** A log line
  from another task spliced into a packet fails its checksum and is never
  seen, so the write takes the same lock ESP_LOG does. And stdout turns
  0x0A into CR LF, which would corrupt any length or checksum byte that
  happens to be 10, so the bytes go out through `uart_write_bytes`. The
  IDF's newlib has no `flockfile`, and its `_flockfile` macro does not
  compile as C++, so the lock is taken with `__lock_acquire_recursive`.
- **`#` no longer starts a comment on a password or network line.** It did
  anywhere on any line, so `pa#ss` was stored as `pa` with nothing said. The
  three staff passwords, `wifi_ssid` and `wifi_password` now take the rest
  of the line as typed. A `#` at the start of a line is still a comment.
- **The backup carries the Wi-Fi password, and the port answers only
  local addresses** (Rob's call in NEXT.md 3.2). A restore onto a fresh
  board brings its network with it. A non-private source gets a 403 and a
  line on the sysop console; `100.64/10` counts as local so Tailscale
  works. The open notice says the zip holds the Wi-Fi password and never
  to forward the port, rather than promising "local network only": the
  check reads the source address, and a router that rewrites it on
  forwarded traffic gets past it.
- **Code review of the Improv glue**, all fixed: a trial now succeeds only
  when the board is on the network being tried, checked by SSID, not on
  any `WIFI_UP` (a stray reconnect to the old network could have saved
  untested credentials); the reconnect handler asks again whether Improv
  has the radio after its log line; a scan that never finishes gives up
  after 15 s and releases the radio; a name or passphrase with control
  bytes or an outer space is refused, since the config would trim it and
  the board would fail after the reboot; a failed UART install turns
  Improv off rather than logging an error every pass. And a hand-edited
  `password = x # note` now logs a warning at boot, by key and never by
  value, since that `#` is part of the value now.
- **A test left `max_users = 77` behind** for the rest of the run, and the
  0.22.0 tests pushed a full no-card run to exactly 77 accounts by the
  backup test, which then failed on sign-up not being offered. The board
  was right. Earlier full runs had hit the old 1200 s limit before
  reaching it. The same first full run found a second leftover: the 0.22.0
  inline-codes test left its forum post up, and the forums test that
  follows it counts unread messages expecting only its own. It takes the
  post down now. Neither was a board bug; both were a test not cleaning up
  what it left, which is the rule 0.21.8 already wrote down.
- `CONFIG wifi`: network and passphrase, used from the next restart and
  never live. Changing the network under a telnet session would drop the
  sysop who changed it. A passphrase under 8 characters is refused before
  it is written.
- Static DRAM: 176,856 of 180,736, leaving **3,880** (was 4,736). Improv's
  parser, the saved and trial credentials, and the two new config fields,
  which exist twice because `reload` parses into a second `SysConfig`.
  Flash 74.7%.
- **On hardware (Improv):** Rob flashed it and provisioned the board over
  Improv from a browser on 2026-09-23, first try. The rest of 0.22.0 and
  0.22.1 has not been reported on the board yet. The local-address refusal
  cannot be exercised by the host suite, which only ever connects from
  127.0.0.1.

## 0.22.0, 2026-09-22

One lot, as Rob set it: mail as a place, inline codes, BELL, long help, the
information pages, and the bugs found on the way. Every new check was run
against 0.21.9 in a worktree and failed there.

**Mail is a place.** `MAIL` opens your mailbox: a list with `*` for new, the
sender, the date and a preview, and the box's size ("2 new, 4 of 12", where
12 is with a card and 3 without). A number reads one, Enter reads the
oldest new one, W writes and asks who to, `?` lists the keys, Q leaves.
Reading shows the forums' header, the body with its codes, and EOM, and
asks `[R]eply [S]ave [D]elete` with Enter for the next and Q back to the
list; nothing is touched until one of those is pressed. Every decision
comes back to the list. `MAIL handle` and the room's `/e` work as before.
Rob: "we cant read other mail without deleting, need that full list".

**Inline codes** in forum posts, mail and chat lines: `@RED@` and the rest
of the palette, `@N@`, `@BLINK:text@`, `@SCRAMBLE:text@`, `@TYPE:text@`,
`@OOPS:text@`, `@SPIN@`, `@DOTS@`, `@NOISE@`, `@RULE@`, `@BELL@`, `@BOARD@`,
`@DATE@`, `@TIME@`, and `@@` for an @. `@`, not `{{ }}`, because a C64 has
no braces. Callers cannot clear, pause or slow somebody else's screen, and
cannot use `@USER@`. Eight codes a message; the rest print as typed, and so
does anything that is not a code, so an email address is safe. `CODES`
shows a new screen, `/codes` a short list.

**BELL**, the same setting as the room's `/b`, and bells now ring: pages,
broadcasts, somebody logging on, somebody joining the room, a private, and
`@BELL@`. The room's `/b` had toggled a flag nothing read since 0.21.4.

**HELP WHO**, **HELP W**, **/? p**: one command in full, 75 entries written
from the source, a command a caller cannot use is as unknown to HELP as to
the prompt. The room's `/?` moves staff commands to `/? staff` so it fits a
24 line screen.

**Information pages**: `INFO` (or `I`) lists them, `INFO 3` reads one with
paging, `INFO 3 EDIT` writes one in the message editor, `INFO 3 CLEAR`
empties it; `/i`, `/i3`, `/i3-` in the room. Titles and who may read each
page are set in `CONFIG info`. A page a caller may not read answers exactly
like a page that does not exist.

**Fixed, and two of these were live on the board:**
- **Any caller in the room could change time.** `/t -1` took them off the
  clock and `/t 3 +600` gave a node ten hours: the shell checked the
  permission before calling the handler, the room did not. The check is in
  the handler now.
- **`FORUMS SCAN` showed anybody every forum**, including ones they may not
  read. Staff only now.
- **A card's screens never followed a new build.** The card is played
  before flash and held the copy seeded when it was first mounted. The
  board now records what it put there and refreshes its own copies; a
  screen the sysop edited, or one it has no record of, is never touched.
  **Delete the old `welcome.*` from the card once**: it was seeded before
  the record existed.
- The screen player takes `@@` as a literal `@`, so a screen can show a
  code instead of running it.
- `FORUMS` answers to `BULLETIN` again, as CLAUDE.md said it did.
- `CONFIG FORUMS TOPICS`, which the board told sysops to type, was not a
  page; the hint says `CONFIG forums`.

**Found by code review before it shipped:** a room line's effect lost its
words for a 40 column reader once it crossed the margin (the room wraps its
own lines now); drawing the mailbox re-read the mail file per message with a
whole-partition space check on every read, a stall with a full box (reads no
longer pay the write guard, `plugins::readPath`); the information pages
asked the filesystem ten times at every login (a bit mask now); `HELP OFF`
printed an empty box; `/? w` explained WHO instead of `/w`.

## 0.21.9, 2026-09-22

Rob's batch from the 0.21.8 flash. Every new check was run against 0.21.8
in a worktree and failed there.

**Replying in a forum was refused while posting worked.** Rob: "oddly it
says I cant post in there but I can post I just cant reply". A CONFIG
sub-page filled every level the file did not set from the plugin's read,
write and admin levels by position. That held for two level parts and not
for a forum's four: Reply got the plugin's admin level (co1), Moderate got
"nobody", and Save wrote both into the file. Each level part now names what
it falls back to, taken from the plugin's own rules, so the page shows what
the forum was already running under. File areas had the same fault with
Download, which showed the admin level instead of the area's Read.
**A forum already saved keeps the wrong level in `system.cfg`**: set Reply
back to `users` in `CONFIG forums`.

**Reading a message asks what to do with it**, the way mail does:
`[R]eply  [Enter] Next  [P]ost  [Q] Back:`, with `[D]elete` for a moderator
and a shorter form at 40 columns. The lists keep their footer and
breadcrumb. Rob: "When reading, ask like email ... The prompt is fine
elsewhere, just not when directly reading a post."

**Column 0, everywhere.** Rob: "--> starts at the very begining. EVERYWHERE
unless told otherwise" and "not sure why these all start indented, stop
that." The forums drew their header, fields, body, notices and footer one
column in; none of them do now, and neither do the two editors' headers.
A blank line sits before `--> EOM <--` in forums and mail.

**The welcome's last line** reads "Connecting you to" and the board's name
from `board_name`, from column 0 after a blank line, typed at 300 baud, then
the spinner. It said "Connecting you unleashed". The pacing is a new screen
token, `@BAUD:n@`, which slows that screen only and never touches the
caller's own `BAUD` setting. A key finishes it at full speed, and the
screens a plugin shows on the way in (`chatin`, `files`) ignore it, because
they are drawn in one go and a paced one lost everything after its first
48 characters; code review found that before it shipped. **Boards with a card need the old copy
removed**: the card holds a seeded `welcome.*` that is played in preference
to the one in flash.

**`/q+` leaves the room and logs off**, with the room told and the same
send-off as `BYE`. The room's `/?` lists it, and its command column went
from 11 to 14, which had cut `/whois handle` to `/whois hand`.

Also: `system.cfg.example` shows `board_name` and no longer shows the
announce `name` key the plugin ignores; COMMANDS.md no longer calls the
forums "being built"; ESP32_BOARD_CHOICE.md carries today's figures rather
than 0.17.1's; a host build warning about the subject number width is gone.

## 0.21.8, 2026-09-22

The input line survives something arriving, in the room and at the main
prompt. Every new check was run against 0.21.7 in a separate worktree and
failed there; two of the first drafts did not, and were rewritten until they
could see what they named.

**In the room.** Rob: "when a message is received you print the message but
then the prompt disappears because you print over it." Five places lifted
the input line and put it back, and every one lifted and restored only the
typed text, not the `[>n]` marker in front of it. So an arriving private
line printed after the marker (`[>2] P#2:Daytona) hi`) and the typing came
back without it. `wipeInput()` already erased the marker; `restoreInput()`
now puts back the same whole: marker, then typing.

**The sender sees what they said.** A stuck conversation printed `--> /p to
#2:Daytona sent.` for every line and never the words, because the typing
that would have shown them was wiped to make room for the receipt. Now it
prints the line, `P>#2:Daytona) hi`, the mirror of the `P#1:...` the other
side sees. A plain `/p` keeps the confirmation Rob specified.

**At the main prompt.** Rob: "you take the message but never return the
prompt, back up and send the message then put the prompt back and use
freaking LF before the prompt!" Two faults, and both are fixed:

- The chat plugin wrote "You have mail" straight onto the recipient's
  session with the room's own interrupt, which only knows the room's input
  line: it printed after `[1] Main:` and redrew the typing with no prompt in
  front. In the forums or the file areas it would have written across the
  screen. Mail notices now go through the core's queue unless the recipient
  is in the room.
- The core's queue, which PAGE, BROADCAST and arrivals already used, moved
  down a line and left the old prompt, typing and all, above the notice.
  It now erases the prompt and the typing (their width is known exactly and
  the editor has no cursor keys, so this lands on column 0 on every
  terminal), prints the notice there, leaves a blank line, and draws the
  prompt with the typing back on it. **Pages, broadcasts and arrival notices
  get the same fix**, because they are the same code.

**Tests can now see a screen.** `render_lines()` plays the bytes onto a
scrolling grid the way a terminal does, honouring backspace, cursor moves and
erase. The plain-text buffer cannot see an erase at all, which is how a
doubled footer, a prompt written over and a marker left behind each passed
every check that read it.

## 0.21.7, 2026-09-22

Removing a post, spacing Rob asked for, and a count that belonged to the
wrong person. Tests written first and run against 0.21.6, where every new
one failed.

**A moderator can remove a post.** Rob: "there is no way the sysop right
now can remove a message". `D` on the message on screen, for callers with
the forum's `mod` level, asks `Remove message #N? (y/N)` and only `y`
removes it: a mistyped key must never cost somebody their post. The
permission is checked again at the answer rather than trusted from the
question, because a CONFIG save in between can change it. Every removal is
logged with who did it.

On the card it is one byte: the record's live flag goes from `.` to `X`. A
byte cannot be half written, nothing moves, every other message keeps its
number and every read pointer keeps its meaning. The body stays in its
segment file, so a removal can be undone by hand. The header's count, which
is live messages, goes down by one, which is what `forum_check.py` already
checks it against.

**Unread counts had to learn about removal first.** They were worked out
from message numbers alone, which was exact while nothing could be removed.
Once it can, a removed post the caller never read would still say "1 new"
while Enter found nothing. `liveUnread()` walks the unread records only in a
forum that has had a removal (its live count is below its highest number),
so a board that never removes anything pays nothing.

**And they belonged to the wrong person.** `Forum::unread` was commented
"for the caller on this session", and it lived in the board-wide forum
table. The second caller into the forums overwrote the first caller's
counts, and the first saw somebody else's numbers on their next redraw. Per
caller now, 384 bytes. The fourth comment in this project to describe
something its code did not do.

**Spacing and markers, from Rob's screenshots:** a blank line between each
list's title bar and its first row; a blank line between the footer and the
prompt on every screen; `--> EOM <--` at the end of every message, forum
and mail alike ("at the end of messages (all)"). The subject list's title
says "N new messages" too, the same form as the forum list's ("make it
universal").

## 0.21.6, 2026-09-22

The forums as Rob laid them out after flashing 0.21.5, plus three bugs his
screenshots found. Focused tests (forums, handle case, messaging, places,
login); every new check was run against 0.21.5 first and failed there.

**The footer printed twice, every time a list was drawn.** `prompt()` prints
the footer; 0.21.5 made each list print one too before calling it. Rob:
"Duplicate exists always not just on entry." Mine, from the fix for the
missing prompt, and it passed because the check that fix added asked only
whether the screen ENDED at a prompt. `count_lines()` now asks how many
times the footer is on screen.

**The footer came out cyan after an end-of-subject notice.** The second
footer was a bare `text()` with no colour of its own, so it inherited the
notice's. Both go through `say()`, which sets one.

**A blank line before every answer to a key.** Rob, three times: "Linefeed
before --> That is the end", "Linefeed before --> Nothing new", and the
same above the message header. One `notice()` helper now carries every
answer to a single key at the prompt, so the next one cannot be added
without it. A line finished with Enter in an editor has already moved down
and gets one newline, not two; the distinction is written into the helper.

**The message header, as Rob wrote it out:**

```
 == New Message: ID #4 -------------------------
 Subject: Wrapping test
 By:      QuantumRob
 Date:    22 Sep 12:32
 -----------------------------------------------
 the body, one column in

 --> Enter reads on. # - Jump to subject. P posts. ? help. Q back.
Forums>Unleashed BBS>
```

"New Message" only when this caller has not read it. `Messages>` is gone
from the breadcrumb: "why do we need messages, we're in the forum topic".

**Subjects are numbered by the message that started them.** Rob: "It shows
1 above, but 4 below, which is it?" The list numbered rows, so a subject's
number moved whenever another subject came or went and never matched
anything on the message itself. The index is never compacted, so a message
ID is permanent, and numbering a subject by its first message makes the
number in the list the number the message shows. No new file: the spec's
`SUBJ.TXT` would have given subjects their own small numbers, which is
exactly the two-numbers-for-one-thing Rob was objecting to.

**A number is typed on the prompt line and confirmed with Enter.** It used
to be one keypress, so only 1 to 9 could ever be reached, against sixteen
forums and sixty-four subjects allowed, and IDs run past 9 almost at once.

**The status line moved under the rule** (Rob): a blank line either side,
"N new messages are ready to read. [Enter] to start reading unread." when
there is something, and "Nothing new since your last call." when not. Both
lists do it the same way.

**`g_ask` survived its caller.** `enter()` and `onLogoff()` reset every
field around it and not it, so a caller who dropped part way through a
subject, a post or a number left the next caller on that node typing into
an editor they could not see. Same shape as `pendingLand` in 0.19.2.

**A handle's case, checked rather than assumed.** Rob logged in as
`QuantumRob` and was greeted as `quantumrob`. `test_handle_case` proves
registration keeps case and that login greets with the stored spelling, so
the lower case is in his account. It also proves the fix works: a
case-only rename through `USER EDIT` is not refused as "taken" by the
account's own name, and the board greets the new case.

**The forum list's title bar says "N new messages" for every count**,
zero included (Rob). It said a lower-case "nothing new" when there was
nothing, which read as a fragment next to a title.

Also: the message body guard went 40 to 96 lines, because 1,536 characters
at 35 columns is about 45 lines and the old limit would have cut the end off
a full post on a C64.

## 0.21.5, 2026-09-22

The forums get the layout that was specified for them in April and never
built. Rob, after flashing 0.21.4: "The requested headers and additional
graphics layout was not added", and then "Missing prompt, no graphis, crappy
layout".

He is right, and the failure is worth naming rather than glossing:
`internal/ux-message-boards.md` is 4,345 lines, it was commissioned for
exactly this, and two versions shipped without implementing it. The process
rule that exists to prevent this ("the specialists advise the builder") was
written down the same week and then not followed.

**Neither list drew a prompt.** The subject list ended with three subjects
and a bare cursor; the forum list needed an Enter press before a prompt
appeared. Cause: `listDone` fires only on an **abort**, by design (0.19.1:
"a listing that ran to the end has already drawn its prompt"), and neither
forum list ever drew one. The footer and the prompt are the last rows of
each list now, which is what that design always assumed.

**689 tests passed over it**, which is the part worth keeping. Every check in
the suite asserts that a **string is present**; not one asserts that a screen
is **usable**. A list with no prompt contains every string the tests look for.

**F2, the breadcrumb.** `[F1] Unleashed BBS>` is gone:

```
Forums>                 the forum list
Forums>C64>             the subject list
Forums>C64>Messages>    reading
```

No number in it, deliberately: a message is `#412` and a subject is `12`, and
a prompt carrying one next to the other in the post rule is a collision
waiting to happen. Fixed words and `>` in `color_title`, the forum's name in
`color_subject`, name cut to `rowWidth - 17`. Every input inside FORUMS is a
single keypress, so a long prompt costs no typing room.

**F1, the reading screen: three pieces at three rhythms.**

```
 12. 1541 alignment disk        5 msgs      context bar, once per subject
 == #412 --------------------- 2 of 5      post rule, once per message
 Daytona  19 Sep 21:14                      byline, two colours
```

The reading loop does not clear the screen, so anything drawn per message is
drawn forty times in a session: a reverse bar per message would be a cyan
stripe every eight rows. A rule per message is what a message separator is,
and what Usenet and every mail digest printed. `Glyph::HLine2` against
`HLine` is two line weights, which reads as a rule with a heavy start on
PETSCII and CP437 alike.

New `subjectPosition()` supplies the "2 of 5" by walking the index oldest
first, so "1 of 5" is the message that started the conversation. Read rather
than cached, because a cached count is stale the moment anybody else posts.

**F4, the subject list** gets the action row (`--> Read the 4 new here`),
drawn only when something is unread, because a row offering nothing is
furniture a caller learns to skip. Both lists close with `rowRule`, which
every other list on this board already did.

**The backspace-pop showed every recalled line twice.** Rob's screenshot had
`5:` and `6:` each appearing with two different bodies. The code erased the
prompt on the current line and redrew the recalled text there, but the text
being recalled is one screen line further up: it was committed, a newline
printed, and the next prompt drawn. It goes up to that line and clears it
now. `left(w)` before `eraseEol(w)` is how column 0 is reached, because there
is no carriage-return primitive above the terminal layer and `Term::ch('\r')`
is deliberately a no-op.

**A forum post has room for paragraphs.** `BBS_COMPOSE_ROWS` 16 to 32, free
(it is a counter). `BBS_COMPOSE_MAX` 1152 to 1536, which is per session and
so costs 4,608 bytes of static DRAM across the twelve. Headroom goes 10,504
to 5,872. Spending nearly half the reclaimed RAM on this is deliberate: the
reason it was reclaimed was to make the board better, and a message base
whose messages are too short to hold an argument is the thing the board is
for. Rob: "I wanted to be able to have a few paragraphs."

Agent models: `code-review`, `optimize`, `tty-ux` and `screen-artist` move to
fable; `explain` stays on opus; sonnet is the floor everywhere else, no haiku
(Rob). The A/B protocol, with the scoring rule fixed **before** the runs, is
in `internal/model-ab-2026-09.md`.

## 0.21.4, 2026-09-22

The room grows six commands, the claims table takes over the last two owner
guards, and the word wrap bug Rob saw on the board is fixed.

**Word wrap printed the erase sequence instead of performing it.** Rob's
screenshot: `...we end up with cra? ?? ?? ?` where the carried word should
have been rubbed out. `Term::ch` translates for the terminal's charset and
maps every byte below 0x20 to `'?'`, so `term.ch(tl, '\b')` has never emitted
a backspace on any terminal. Four sites hand-rolled BS-space-BS through the
one call that cannot carry a control byte: the wrap rub-out and the
backspace-pop prompt rub-out, in forums and in mail alike.

`Term::eraseBack` is the primitive that already did this correctly and per
terminal (PETSCII DEL, an ANSI CSI run, BS-space-BS otherwise) and it existed
the whole time. **`Term::ch` is for text**; anything that moves the cursor or
erases goes through a Term primitive, because that is the layer that knows
what the terminal is.

**Six room commands, approved from `internal/chat-commands-2026-09-22.md`,
plus one Rob added.** 24 bytes of static DRAM between them, because every one
reuses machinery that already exists:

| Command | What it does |
|---|---|
| `/p3*`, `/p*` | stick the conversation to one node, and end it |
| `/sh [n]` | replay what the room has said |
| `/whois <handle>` | who is that |
| `/b` | bell on or off |
| `/page n <why>` | get their attention, as distinct from talking to them |
| `/t n +m` | give a caller minutes, staff only |

- **`/whois`, not `/info`.** Rob approved it as `/info`, which was the
  report's name, but `INFO` is the information pages now and `/i0`-`/i9` is
  how the room reaches them. They would coexist mechanically, because the
  verb-ends-at-a-digit rule splits them, but a caller would have to know
  that rule to predict which one they were getting.
- **`/whois`, `/page` and `/t n +m` call the shell's own handlers**, which
  were made public for it, the way the row helpers were in 0.17.3. Two
  implementations of "show me a caller's profile" is how one of them ends up
  showing a field the other hides, and Rob's condition was that it use the
  same public fields PROFILE does.
- **A sticky line is rewritten as `/p <node> <text>` and put back through
  the ordinary command path**, so the P marker, the away note, the rate
  limit and the sender's confirmation cannot drift from a typed `/p`.
- **The input line carries `[>3]` the whole time it is on.** The entire risk
  of a sticky private is forgetting you are in one, and it is counted in
  `stickyCols` so `wipeInput` erases it rather than leaving one behind on
  every re-arm.
- **If the target leaves, the mode ends and the line is not sent anywhere.**
  Falling back to the room would be exactly the accident the marker exists
  to prevent. The test asserts the line reaches nobody, not merely that the
  mode ended.
- **`/b` only toggles the bell when it is bare.** `/b handle` has been the
  staff bar-from-the-room command since 0.11.0, and a shortcut that shadows
  an existing one is the FX/FILES bug, which went unnoticed for months.

**Two bugs found by building this, both pre-existing:**

- **`cmdInfo` drew its own prompt** while `cmdPage` did not, so the room's
  `/whois` silently walked callers out of chat and back to the shell. The
  prompt moved to the command table, where every other handler's is.
- **`*` did not break a verb**, so `/p*` parsed as a three character verb
  matching nothing and answered "Unknown command". It breaks a verb now, for
  the same reason a digit does.

**`claims.h` now owns all three guards.** The forums' subject table and the
transfer engine joined CONFIG, and migrating the third one taught the
mechanism something:

- **A LOCK is exclusive** (CONFIG, the transfer engine): a second caller is
  refused and told why. `take()`, and act on false.
- **A CACHE is shared scratch with a tag saying whose data is in it** (the
  subject table): the right answer for a second caller is to refill it, not
  to refuse. `seize()`, which always succeeds.

Using `take()` for the subject table made the second caller into a forum
unable to list its subjects, which a test caught. Both kinds want the same
release discipline, which is why they share a table.

`claims::transfer` also covers `moveSession`: sysop elevation changes a
caller's node id, and a claim filed under the id they left could never be
released, locking the resource until a reboot.

**`SYS` reports stack headroom**, the least the BBS task's stack has ever had
free. Twenty two `UserRec` scratch buffers are static, each with a comment
saying that keeps them off the task stack, and not one of those comments was
backed by a measurement; turning them into locals is worth about 10 KB. This
is the measurement, and it is the same argument as the loop phase timing in
0.19.2: the cure for reasoning about a thing from the outside is making the
board say.

**`FX` gave up `F` to `FILES`.** Both declared it, the core table registers
first, `findCommand` returns the first match, so FILES's documented shortcut
had never once worked and COMMANDS.md said it did.

**Two harness defects, and both reported board bugs that did not exist.**

- **`--only` ran tests alphabetically while the full suite runs a hand
  ordered list.** `test_ban` bans 127.0.0.1 and is deliberately last; under
  `--only=login` it ran second and every later test died with a broken pipe.
  The order lives in one place now, `ORDER_NAMES`, which the full run walks
  and `--only` filters, so a targeted run is always a subset of the real run.
- **`test_backup` and `test_cosysop` depended on an account `test_page`
  creates.** Under `--only=storage` that test is not picked, and four backup
  checks failed for a reason that had nothing to do with backups. Both seed
  their own account now. A test that depends on another test reports
  somebody else's absence as your bug.

Docs: `BBS_RX_ROOM` is 1,700 bytes and CLAUDE.md said 1 KB; a Session is
6,980 bytes and both CLAUDE.md and `config.h` said 6,000, which is 14% low
and is the figure sizing decisions get made against.

## 0.21.3, 2026-09-22

Static RAM, and the start of one mechanism where there were three. Smoke
tested (shell, login, messaging); 0.21.2 underneath it passed the full card
suite at 676 checks.

**Headroom went from 4,776 bytes to 10,504.** Measured off the ELF, not off
PlatformIO, which called the same build 53.7% while it was at 97.4% of the
real ceiling. Two tables stopped storing text they only ever compared:

- **`users::validateFile`'s `seen` table: 5,250 bytes to 1,000.** It holds
  every handle read so far, purely to answer "have I met this one already".
  That is an equality question, so it holds 250 hashes now. Stated rather
  than buried: two handles that hash alike would be reported as duplicates
  and the upload refused, about one in 137,000 uploads, and it fails toward
  refusing a good backup rather than accepting a bad one.
- **CONFIG's `g_cfgWas`: 1,536 bytes to 64.** It held what each field looked
  like when the page opened, for one `strcmp` deciding whether to write it.
  This also permanently removes a bug that was live once: the old table held
  a **truncated** copy (`"%.47s"` into 96 bytes), so any value longer than 47
  characters always compared unequal to itself and was rewritten on every
  save whether or not it had been touched. A hash covers the whole string, so
  there is no length left to get wrong.

**One FNV-1a in the tree.** The forums had their own; `bbsu::hash` and
`bbsu::foldHash` are now shared by the forums, the users.txt validator and
the CONFIG page. `subjectHash` forwards to `foldHash` with the same folding
and the same 0 sentinel, so every forum already written groups exactly as it
did.

**`claims.h`: one owner table where there were three.** Rob, on being shown
them: "why would we have 3 versions and not one ... we should be combining
into something reusable right?" He is right, and they were three separate
inventions of one idea rather than one used three ways:

| Was | Type | Scope |
|---|---|---|
| `g_cfgOwner` | `const Session*` | who is editing CONFIG |
| `g_subjWho` / `g_subjFor` | two `uint8_t` | who filled the subject table, for which forum |
| the transfer engine | `bool` | somebody is transferring |

Three types, three scopes, and three release paths that each had to remember
to clear themselves. `claims::take/holds/release/releaseAll` replaces them,
keyed by **node and never by handle**, because the same person can be on two
lines at once and they are two callers as far as a resource is concerned.

**The half that actually prevents the bug is `releaseAll` in `openSession`,
not just `closeSession`.** Sessions come from a static pool, so a claim left
behind by a dropped caller is inherited by whoever dials in next, and every
bug of this shape here has been exactly that: `pendingLand` dropping the next
caller into the chat room, the squelch and away masks leaking between
callers, the subject table serving one caller's forum to another. Clearing on
arrival cannot be skipped by an exit path that did not run.

**CONFIG is migrated; the subject table and the transfer engine follow in
0.21.4.** Deliberately not all three at once: if the mechanism is wrong,
moving one subsystem means one subsystem is wrong, and this gets a flash on
real hardware before the other two lean on it. `g_cfgOwner` survives as a
pointer read only for the "X is editing the settings" message and is never
branched on, because a `Session*` from a static pool is precisely the thing
that goes stale.

**Not done, and why.** Unioning the backup's `ZipExport` and `ZipImport`
(3,872 bytes) was approved and then withdrawn on reading the code: both own
open `FILE*` handles and the cleanup path calls `exp_.abort()`
unconditionally, so sharing storage means hand-managing two object lifetimes
inside the rescue path a sysop uses when something has already gone wrong.
Moving the whole backup window to the heap (about 10,800 more) was rejected
for the same reason in stronger form: it would turn "the backup window always
opens" into "it opens if there is heap", and the moment it would fail is the
moment it is needed.

## 0.21.2, 2026-09-22

Naming, and the things a 40 column terminal made unreadable. Host tested,
not yet flashed.

**"Bulletin" is retired.** It was doing three jobs and about to be asked for
a fourth, which is why the word kept coming back in conversation after
conversation. Rob: "im so tired of dealing with this BS on the word
bulletin."

- `screens/bulletin.*` is **`screens/motd.*`**. It is the screen shown after
  login, which is what a motd is everywhere else. Deliberately not
  `welcome_anything`, because `screens/welcome` is the pre-login banner and
  sharing that word rebuilds the confusion. No board ships one, so nothing
  on disk moved and nothing needs migrating.
- The sysop's information pages are **`INFO` / `I`** at the prompt and
  **`/i0`-`/i9`** in the room, so the letter matches the word in both
  places. Rob's reasoning: these are information pages generally, and news
  is a thing you put on one rather than the name of the rack.
- **`INFO [handle]` became `WHOIS [handle]`** to free that word, and it
  should always have been WHOIS: it answers "who is this" and sits beside
  WHO, which answers "who is on". No hidden INFO alias was kept, which is a
  deliberate break with the usual courtesy, because the verb is being reused
  and an alias would send an old habit somewhere wrong.
- The word survives in exactly two places, both back-compat for data already
  written: the hidden `BULLETIN` alias for FORUMS, and `landFromKey()`
  reading `land = bulletin` as forums.

**A composed line follows the terminal instead of a constant.** Forums and
mail both opened the editor at `BBS_LINE_MAX` (72) and triggered their wrap
at the same number whatever the caller was sitting at. On a C64 that is a
four character line number plus 72 characters against a 40 column screen, so
the terminal wrapped every line, the board's own wrap never fired, and the
numbers down the left went out of step with the text beside them.

- New `compose::lineWidth(cols, prompt, hardMax)`, shared by both, so the
  editor's capacity and the wrap trigger cannot drift apart. They were two
  copies of one constant in two files, which is how they would have.
- Mail's row cap went 12 to 16. A narrower line means fewer characters per
  row, and 12 rows at ~35 columns would have capped a C64 caller at about
  420 characters against a 512 character allowance: a smaller mailbox on a
  narrow terminal for no stated reason. 16 x 72 is exactly
  `BBS_COMPOSE_MAX`, so the buffer still cannot be overrun.

**Mail wraps at the reader's width.** `mailRead` printed up to 512
characters as one run and let the terminal break it wherever it landed,
which on 40 columns is mid-word every third line. It goes through
`bbsu::wrap` now, the same function and the same argument as forums: a
message typed at 72 columns has to be readable on a C64, and one typed at 35
should not sit in a stripe down an 80 column screen.

**Fourteen system lines in the forums were wider than a 40 column screen**,
the worst at 67 characters. They go through one `say()` that wraps at the
reader's width, with a leading `-->` or indent treated as furniture so
continuations line up under the words rather than under the arrow.
Shortening them all to 39 was the obvious fix and the wrong one: it would
have made every wide terminal worse, which is the habit the `rowWidth`
rework already corrected once.

**Ctrl-D is no longer advertised, and that was live.** The body editor's
help line still offered it as a way to send, two sittings after Rob asked
for it to go and with `compose.h` already recording that it never reaches
the board because SyncTERM eats it. The wording now comes from
`compose::kHowToEnd`, which mail already used, so the two cannot drift.

**`announce` read past the end of its own buffer and put it on the wire.**
Found by the memory review and verified before fixing. `snprintf` returns
the length it *would* have written, and both `g_bodyLen` and `g_reqLen` were
assigned straight from it, so on truncation each was larger than the array it
described. Two consequences, and the second is the bad one: `Content-Length`
announced more bytes than followed it, so the directory read invalid JSON and
the board was quietly not listed; and the send loop, `send(g_fd, g_request +
g_sent, g_reqLen - g_sent, 0)`, read past the end of a 768 byte buffer.

Reachable through ordinary use rather than as a worst case. `name`, `owner`,
`description`, `host` and `token` are all CONFIG values and a CONFIG value
buffer is 96 bytes, so five of them is 475 characters before a byte of JSON
skeleton, against a 512 byte body. A board with its description filled in
trips it.

Both lengths are clamped to what was really written, and a payload that did
not fit is now **refused rather than sent**: a truncated body is invalid JSON,
so sending it means being delisted for a reason nobody can see, which is the
same argument as "held must never be silent" in the directory notes. The
console says which field to shorten.

The comment on `kBodyMax` read "the payload never gets near this". **Third
time in this project a comment has been believed over the arithmetic**, after
the `heapWatch` comment that described a call it was making and the `rowEnd`
comment that stated an invented mechanism as fact.

**`kMaxParts` is checked rather than asserted in prose.** It is 8, over
`kAreaParts` (6) and `kTopicParts` (7), and the loops read
`i < comp->count && i < kMaxParts`, so an eighth part would not overflow
anything: it would be silently dropped on save. That is precisely the file
area bug that shipped in 0.20.0, where a count of 4 over six parts took
Download and Delete off every area a sysop edited. Two `static_assert`s now
fail the build when a part is added. **Fourth instance of a bound written
beside a table instead of computed from it.**

Also recorded rather than built: the chat command decisions from
`internal/chat-commands-2026-09-22.md` (six approved, three rejected, plus
`/t n +m` to give a caller more time), and the DDial roster finding, which
is that the five minute timer was the rotator's and carried the *network*
roster across a link rather than reprinting the local one.

## 0.21.1, 2026-09-22

One way to write a message, and the radio stops sleeping.

### Message entry, everywhere

Rob, on finding a forum post cut off mid-sentence: "the message length ended
where the t did in the screen shot, cant enter more this was supposed to have
a lot of space for a message, how did this truncate." And on the shape of the
fix: "I dont see why mail, the system feedback systems, forums all dont use a
unified message entry system."

- **A body could never have been longer than 72 characters.** `s.ed` is the
  single-line editor: its buffer is `char buf_[BBS_LINE_MAX + 1]`, 73 bytes,
  and `begin()` takes a `uint8_t`. Asking it for 1,728 did not fail or warn,
  it silently gave back 72. `FF_TEXTAREA` is no better at four 37 column
  rows.
- **New `src/core/compose.h`, shared by everything that takes a body.** Forum
  posts and mail use it today and the feedback plugin gets it free. A second
  copy would drift from the first, which is a shape this project has already
  paid for. 36 checks in `host/test_compose.cpp`.
- **A message is written a line at a time**, up to 24 lines for a forum post
  and 12 for mail, which is what `MailRec` holds. `/s` on a line of its own
  sends it, `/a` throws it away, and Ctrl-D or Ctrl-Z also send. **`/s` is
  the one the screen names**, because it is typeable on every keyboard ever
  built: telnet clients swallow some control keys and a C64's Ctrl
  combinations are not a PC's. Naming only the control key would strand
  exactly the callers this board is for.
- **Long lines wrap while you type** rather than refusing keystrokes. The
  break goes at the last space and the unfinished word carries to the next
  line. A caller who stops being echoed mid-sentence reads that as the board
  having frozen, which is how it was reported.
- **Backspace on an empty line takes the previous line back for editing**, as
  many times as you like, down to an empty message. Line-at-a-time entry
  commits a line the moment Enter is pressed, and without this a typo three
  lines back could only be fixed by abandoning the whole message: worse than
  the fixed line length it replaced, because that wall was at least visible.
- **`MAIL <handle>` with no text opens the same screen**: cleared, with a
  header naming the recipient. `MAIL handle your message` on one line still
  works, because it is quick. Mail's body stays at 512 characters, which is
  what the record holds; giving mail a forum-sized body means moving the text
  out of `MailRec` into its own file, and that is a format change rather than
  something to smuggle in here.
- Posting a forum message clears the screen and draws a header with the forum
  and the subject. Before this the subject prompt appeared under whatever was
  on screen, which arriving from `?` is the help text.
- The prompts are theme keys rather than literals, in the plugin's own
  config section the way chat's are.

### Reading, which was unreachable

- **A poster could not read their own message.** Rob posted the first message
  on the board and then could not open it: a poster has read their own post
  by definition, `Enter` means "the next thing you have not read", so the
  only message on the board was unreachable. Correct on its own terms and
  useless in practice.
  Inside a subject, reading now walks the conversation in order whether or
  not it has been read. `Enter` from the forum list still means what is new;
  that distinction is the fix rather than a loosening of it. Pinned by a
  regression check.
- `1 subjects` is `1 subject`, and the same for messages.

### The radio was sleeping again

Measured on the live board while Rob reported lag, 116 pings: median 27 ms
and **nine samples clustered at 1011 to 1066 ms**, against a gateway flat at
0 ms over 20. A tight cluster on a round number is a timer, not
interference, and Rob's own hypothesis named the trigger: DASH sends a frame
and then goes deliberately quiet for a second, which is the gap that lets a
station doze.

- **`CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE` was on**, which lets the
  IDF put the radio into power management by itself. Off now: this board is
  mains powered and the saving buys nothing.
- **The 0.18.0 fix only re-asserted `WIFI_PS_NONE` at `IP_EVENT_STA_GOT_IP`**,
  and a reconnect that keeps the same lease never raises it. Now asserted on
  `WIFI_EVENT_STA_CONNECTED` as well, through one `noSleep()` so the two
  cannot drift.
- **`SYS` has a Radio row**, read back from `esp_wifi_get_ps()` rather than
  assumed: `awake`, or `SLEEPING, expect ~1s lag` in red. This has been
  chased from the outside twice and got the wrong answer once. A board that
  says which mode it is in turns the next one into a reading.

### And the stall that is not the radio

- `SYS` showed `Loop worst 193,833 us in session, node 1` with **9 slow
  passes in 31 minutes**, which settles a question a high-water mark could
  not: it recurs, and it is on a caller's own path. 193 ms is not 1,020 ms,
  so this is a second and separate fault, still open.
- The worst pass now records **what that caller was doing**, from
  `Session::doing`, so the next reading names a command rather than a phase.
  "in session" leaves a screen off the card, a directory walk and a
  `users.txt` rewrite all equally likely; a verb does not.

## 0.21.0, 2026-09-21

Forums, phase 2: posting, reading, and conversations that stay together.

- **Three levels, and one key that skips all of them.** Browsing is forum to
  subject to message. Reading is Enter, which walks everything unread
  wherever it lives and never asks the caller to choose. Both exist on
  purpose: the drill-down is for somebody hunting one conversation, and the
  fast path is what a regular caller uses every call. If the drill-down were
  the only way in, this would be slower than the flat design it replaced.
- **Grouping is by a stored hash, never by the subject text.** A reply
  carries its parent's hash rather than rehashing what it says, so renaming a
  subject cannot split a conversation and two that happen to read alike
  cannot be merged. Case and surrounding space are folded, because
  "20m Antennas" and "20m antennas " are the same conversation to everybody
  except a computer.
- **The read pointer is a mark plus a 16 byte window of what was read above
  it**, and the window is what makes a subject list honest. A single
  forum-wide mark cannot serve one: read a subject to its end and the mark
  declares every older subject read too, or refuse to move it and the subject
  still says "3 new" straight after somebody read all three. Either way the
  number is a lie and it is the first thing anybody checks.
  **It self-drains**, which is what makes 16 bytes enough rather than merely
  small: catching up gives the space back, so ordinary sequential reading
  never accumulates anything. Its failure direction is deliberate and tested:
  it never marks an unread message read, it only forgets that one was read
  and shows it again.
  `src/plugins/forums_ptr.h` holds it with no board, card or session in it,
  and `host/test_forums_ptr.cpp` has **22 checks** including Rob's own
  interleaved-subject example walked through by hand.
- **Bodies are word-wrapped when they are read, at the reader's width**, not
  at the width they were typed. A message written at 72 columns has to read
  on a C64 and one written at 35 should not sit in a stripe down an 80 column
  screen, and the reader's width is not knowable when the message is written.
- **The screen is not cleared between messages**, and this is the one place
  the board's own "screens clear" rule is deliberately reversed. Reading is a
  scroll, not a view: the previous message is the context for this one.
- **`tools/forum_check.py`, which shares no code with the board.** It reads a
  forum from the outside against offsets typed in from the plan, and it can
  build one by hand for the board to read back. A format tested only by the
  program that wrote it is a format that agrees with itself, and this project
  has paid for that twice: the test client honoured the board's own telnet
  quirks so two real bugs passed every test while hardware failed, and lrzsz
  found an XMODEM bug precisely by being somebody else's implementation.
  **It immediately earned its place**, see below.
- **Two bugs, both found by tests rather than by reading the code:**
  - `Enter` did nothing. The comparison was against `'
'` and the terminal
    layer decodes Enter to `KEY_ENTER`, which is `0x100`. So the key that is
    the entire fast path silently never matched. The test reported it as "the
    subject that was read is no longer marked new", which points nowhere near
    the cause: only the first message had ever been shown, because opening a
    subject is what displayed it.
  - **A per-caller number was being written into a board-wide file.** One
    field held both the forum's live message total, which belongs in the
    header, and this caller's unread count, which must never be persisted.
    The post path incremented the reader's figure and then saved it, so a
    forum with four messages recorded `count=1`. Nothing a caller could see
    would have shown it, because the header is only read at start.
    `forum_check.py` printed the header next to the records and the
    disagreement was obvious. Split into `total` and `unread`, and the
    checker now pins it.

Host: 663 checks with a card, 469 without, 0 failures. Unit tests: 22 for the
read pointer, 11 for the word wrap, 82 for the transfer engine.

## 0.20.0, 2026-09-21

Forums, phase 1: the formats, and a list to prove they are reachable.

**What a caller sees is a forum list and nothing else.** Said plainly because
this is the phase people skip. Posting and reading are phase 2; what phase 1
buys is the file formats frozen, and those are what every later phase stands
on.

- **A live permission bug fixed first, because it was shipping.** `CONFIG
  files` defined six parts for an area and registered the table with a count
  of four, so the sub-page showed Path, Name, Read and Upload, and saving any
  area packed four parts over the six in the file. `files::mayDown` then
  falls back to the area's READ level, which is sensible in isolation and
  meant **a staff-only download area silently became downloadable by
  everybody** the moment a sysop renamed it. The board reported success.
  The count is derived from the table now (`CFG_PARTS`), so adding a part
  cannot be half done. Same shape as the CONFIG Board page rendering 5 of 7
  fields, and the same fix.
  **The test is the part worth keeping.** Its first version saved the area
  without changing anything and passed against the broken code, because
  `configSubSave` short-circuits an unchanged page and never writes the file
  at all. It edits the name first now, which is also the real report: rename
  an area, lose its permissions.
- **Documentation bug in the same six fields, found the same afternoon.**
  CLAUDE.md gave the wire format as `read | down | up | del`; `readKey`
  parses `read | up | down | del`. A sysop following the documentation would
  have set the download level where the upload level goes. Two permission
  bugs in one set of six fields, one in the editor and one in the prose,
  which is what a packed bar-separated value invites.
- **`rowTitle` never truncated**, and every caller until now passed a short
  literal, so the missing clamp was invisible. A forum subject is typed by a
  caller: 49 characters into a 39 column bar would run long, wrap, and leave
  the reverse attribute hanging down the next line. Now `rowBar(colour, ...)`
  with truncation, and `rowTitle` is that in Cyan.
- **`bbsu::wrap`**, word wrap at the reader's width rather than the writer's,
  because a message typed at 72 columns has to read on a C64 and one typed at
  35 should not sit in a stripe down an 80 column screen. Eleven unit tests
  under ASan and UBSan, wired into `make test`. **Two bugs in it were found
  by those tests and neither was visible in a reading of the code**: a word
  ending exactly on the margin was thrown onto the next line, wasting half a
  row, and a line could end in a space, which on a reverse-video row is a
  visible notch. The queued "profile text should word wrap" item wants this
  same function.
- `Glyph::HLine2`, the double rule, appended to the enum because the
  translation tables index it. Plain ASCII has no colour and no reverse
  video, so `===` against `---` is what marks the row a caller acts on.
- **The forums plugin.** `PF_SD`: no card, no plugin, and `FORUMS` is not a
  command at all. Sixteen topic areas, which is where `Form::kMaxFields`
  actually puts the wall rather than a number somebody picked. Four levels
  per forum, and the split between `start` and `reply` is what makes a
  read-only announcements forum work *better* than read-only:
  `read=all, start=co1, reply=users` is "staff post the news, anybody may
  answer", which boards wanted and could not say. `CONFIG FORUMS TOPICS`
  edits them as labelled fields rather than a bar-separated line.
- **The index format is frozen.** 128 bytes a record, four to a sector,
  message N at byte offset N x 128 forever. Nothing packs, compacts or
  rewrites it; a deleted message keeps its slot and flips a flag, because the
  usual way a read pointer stops meaning anything is a well-meaning
  compaction pass. Guarded by asserts on **offsets and the total**, never on
  a sum of field widths, which is the version that fails on correct code the
  day padding appears and has already cost this project a round.
- **Grouping is by a stored subject hash and never by the display string**,
  so renaming a subject, or disambiguating two that collide, cannot split a
  thread or merge two.
- `CALLS` is public (Rob). It is a bar chart of calls per hour with no
  handles and no addresses in it, and knowing when a board is busy is what
  tells somebody when to call. The same figures are already on the
  directory's website for any board sharing activity.
- "Out of files." is "Leaving the file areas. Returning to the BBS...", which
  reads as leaving a room rather than as the board having run out of
  something.

## 0.19.2, 2026-09-21

The loop says where it went, and two bugs found looking for the one it did not explain.

- **"Loop worst" was a bare number.** SYS reported that a pass took 280 ms and
  never which part of it did, so both stall investigations so far opened with a
  guess, and the first one guessed wrong and wrote the guess into a comment as
  fact. `tick()` now times its five phases separately, keeps the phase name and
  the node behind the worst pass, and logs one console line per slow pass with
  the whole split. **`Slow passes` on SYS is the number that was actually
  missing:** a high-water mark cannot tell one stall at boot from a stall every
  minute, and that was the first question worth asking both times.
- Permanent rather than a diagnostic build. Five `esp_timer` reads against a
  58 us average pass is affordable, and a stall that only appears on a real
  board at hour three is exactly the one a special build switched on afterwards
  never catches.
- **`heapWatch` took the cost its own comment said it avoided, for two
  versions.** The comment reads "It deliberately does NOT call plat::heap(),
  which walks the allocator"; the next line was `plat::heap().freeBytes`, and
  that reaches `heap_caps_get_largest_free_block`, which walks the entire pool
  under `portENTER_CRITICAL`: interrupts off on core 1, holding a spinlock the
  allocator on core 0 contends for, once a second, on every board. New
  `plat::heapFree()` is the counter read the comment always described.
  The lesson is about the comment, not the call: a comment asserting what the
  code does *not* do reads as a decision already taken rather than a claim to
  check, so it survives review in a way a wrong positive claim would not.
- **`pendingLand` and `landing` were never reset in `openSession`**, while six
  siblings in the same block were. Sessions come from a static pool, so a
  caller who dropped the line during the bulletin left the flag set and the
  next caller on that node was dropped into the chat room by the first screen
  they played. Reachable without disconnecting too: `abortOutput` cleared
  `pendingPrompt` and not this, so Ctrl-C out of the bulletin and then `ABOUT`
  landed somebody somewhere they never asked to go. Reset in both places.

Measured against the live board while chasing this: storage work costs about
40 ms of ICMP and a command touching no storage costs 17 ms, which is a real
correlation and an order of magnitude short of the 280 ms stall. No code path
was found that blocks that long in one operation. The instrumentation is what
settles it.

Host: 638 checks with a card, 468 without, 0 failures.

## 0.19.1, 2026-09-21

Stopping a listing hands you back to where you were.

- **`Q` at `[More]` inside a subsystem left the caller nowhere.** The core
  hands a finished list back to the plugin that owns the session and
  deliberately draws no shell prompt, because the plugin owns the screen. The
  file manager printed its prompt as the last *row* of a listing, which works
  right up until somebody stops the listing early: that row is never reached,
  so the caller was left looking at "Stopped." with nothing to say the file
  areas still had them, and every key after that went to a subsystem they
  could not see.
- New `listDone(Session&, bool aborted)` plugin hook, **appended** to
  `Plugin` like everything after the line in that struct, dispatched from
  `Bbs::listEnded(s, aborted)`. `files` redraws its prompt on an abort only:
  a listing that ran to the end has already drawn one.
- Invisible to any test that reads a listing to the end, which is why it
  lasted. `test_list_abort_returns` now drives the case that matters.
- Naming: the message boards are **forums**, settled in 0.18.0 and carried
  into `LAND_FORUMS` and the `Start` field. Two stale comments still said
  "the bulletin plugin". `screens/bulletin.*` keeps its name because it is
  the login notice screen, which is the collision that forced the rename.

Host: 638 checks with a card, 468 without, 0 failures.

## 0.19.0, 2026-09-21

A handle stops being an identity, the board can be taken down on purpose, and the radio stops going to sleep mid-call.

### Identity: a handle is a name, not a person

- **Accounts have an id now**, 32 bit, assigned once and **never reused**. It is one more key in `users.txt`, and the board gives one to every account that lacks one on the first boot after upgrading, counting up from the highest already present. The next id is derived rather than stored, so there is no counter to keep in step, lose in a restore, or have disagree with the file.
- **`USER DEL` retires instead of removing, and the confirm says so.** This closes a live bug rather than tidying one: deleting the block put the handle back into circulation, mail is matched by handle, so **the next person to register that name was handed the previous owner's undelivered mail**. The suite asserted that as correct behaviour ("deleted handle is new again"), which is how it survived; the test now asserts the opposite and passes.
- Retiring is also what makes a derived id counter safe, since a removed block would put its id back in play. The two rules hold each other up.
- **A rename takes your things with you.** Renaming somebody used to write `users.txt`, patch any live session, and stop: their own unread mail stayed filed under a name that no longer existed, and a room ban did too, which made renaming a way out of one. A new `onRename` plugin hook carries it, and chat rewrites both the mailbox and the ban list. Nothing announced either failure before; the mail did not bounce and the ban did not complain, they simply stopped applying to the person they were about.
- **Staff access is typed once a week, not once a call**, and it is **bound to the address it was confirmed from**. That binding is the whole point: account passwords cross this board in the clear on every login, so remembering staff rights against the account alone would turn a sniffed account password into a week of staff access. From anywhere else the password is asked for again. **The sysop level is never remembered**, because it can change every password on the board. With no valid clock it fails closed and asks.
- **Lowering somebody's level in USER EDIT revokes it** at their next login, so the user manager is a real revocation rather than a change of marker. A session already elevated keeps what it has until it drops; KICK is the answer when "now" is what was meant.
- **What was deliberately not done:** threading ids through `mail.dat` and the ban list. `MailRec` is a fixed-size record with a static assert guarding its layout, so adding an id changes `sizeof` and every existing mailbox needs converting. Following renames fixes the same visible bugs without migrating live data. The forums will store ids natively, which leaves mail as the only holdout and a much smaller job than it is today.

### SHUTDOWN

- `SHUTDOWN [n]` announces to every node, counts down (5 to 3600 seconds, default 60), then hangs up on everyone including the sysop, each through the ordinary send-off screen and linger. `SHUTDOWN CANCEL` stops it and says so.
- **Afterwards the board keeps answering and says it has been shut down.** Closing the listener would give a connection refused, which is indistinguishable from a crashed board, a dead Wi-Fi link or a wrong address, and an unexplained failure is the expensive kind. The board is powered either way, so silence saves nothing.
- Warnings go at 120, 60, 30, 10, 5, 4, 3, 2, 1 rather than once a second, through the message bus so they reach a caller mid-form or inside the chat room. A line a second for two minutes is noise people stop reading, and on 40 columns it is the whole screen.
- No confirmation prompt: the countdown is the confirmation, and there is a cancel. The transfer warning is said rather than enforced, because knowing which caller is mid-download needs a hook into the plugin that owns the engine and a sysop can see it in NODES.

### Heap, and the reboot nobody saw

- **A heap watchdog.** The board was restarting on its own and the only evidence was `MEM`'s "heap low since boot" figure having gone **up**, which a high-water mark can only do across a reboot: two readings twelve minutes apart, 25,204 then 50,224. It now samples once a second and logs at 40K, 24K and 16K on the way down, once per threshold, so the next one leaves a trail instead of just rebooting.
- **lwIP send buffer and window 5760 to 2880 per socket, Wi-Fi dynamic buffers 32 to 16.** Those are per-socket heap allocations across sixteen sockets, sized for bulk TCP this board does not do: XMODEM and YMODEM are stop-and-wait, so a large window is never filled, and chat lines are hundreds of bytes. 2880 is still two full segments at the 1440 byte MSS. All four confirmed present in the generated `sdkconfig`, because a value out of range there reverts silently rather than warning.
- `MEM` no longer bypasses the SD plugin's cache, so it stops doing a real `esp_vfs_fat_info()` on every call: measured at 15 to 25 ms typical and 160 ms worst on a real card, on a caller's own keystroke. The cache moved to the platform layer, where mount and unmount can invalidate it.

### Fixed, all of it found by driving the live board

- **Tab does nothing, on every form.** Every form footer says "Tab or arrows move" and `0x09` was dropped by the ANSI decoder, so the form's tab branch was unreachable. Shift-tab too: the decoder answered `ESC O Z` while xterm and SyncTERM send `ESC [ Z`.
- **`FILES 99` stranded you inside the door.** It opened the file areas, said "No area by that number", and left every command you typed afterwards to be eaten by the subsystem: `term 80` came back as `File number: 80`. A number that names nothing is now refused at the prompt without opening anything.
- **`Q` at the file-number prompt did nothing** while the line directly above it said `Q/ESC back`.
- **`WHO` truncated handles at 12 characters** on a 132 column screen, where `BBS_USER_MAX` is 20. The column follows the terminal now.
- `? staff` drew an empty box for an ordinary caller instead of saying it is not for them, and `HELP nonsense` silently printed the main menu rather than admitting it had not understood.
- `PAGE` on an empty node said "not taking pages", contradicting the WHO the caller had just read. Hidden and DND stay lumped together deliberately, so HIDE cannot be detected by probing; an empty line gives nothing away that WHO does not already show.
- The room's help ran two columns together: `/email h m` is exactly ten characters against a ten wide column.
- **`NODES n`** refreshes like `WHO n`, and `rowNodes` is fixed-height now so it cannot corrupt itself the way DASH did.
- **Multiple sysop logins from the LAN.** The sysop node holds one caller and has to, so a second sysop used to be hung up on. From the local network they now get sysop rights in place on their own line, like a co-sysop. From the internet the old behaviour stands, so the board never has two sysop sessions open to the outside at once.

## 0.18.0, 2026-09-21

The board stops going quiet for a second at a time, reading a message stops destroying it, callers choose where they land, and the room has a voice of its own.

### The stall, and it was never the BBS

- **Wi-Fi power save was left on, and it cost about a second at a time.** `esp_wifi_set_ps(WIFI_PS_NONE)` sat on the line after `esp_wifi_start()`. Starting the station raises `WIFI_EVENT_STA_START`, whose handler calls `esp_wifi_connect()` immediately, so that call was racing the association. Its return value was never checked, so a failure said nothing. And nothing re-applied it after a reconnect, which with `CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE` leaves the board on the IDF default of `WIFI_PS_MIN_MODEM`.
- A station in `MIN_MODEM` sleeps between DTIM beacons and the access point buffers for it. Measured on the live board **with no callers connected at all**: median ping 13 ms, 90th percentile **1003 ms**, and seventeen of the slow samples inside a 31 ms window at exactly 1.00 s. A cluster that tight on a round number is a timer, not interference. The gateway in the same window never exceeded 1 ms. Poll the board continuously and every spike disappears.
- It is applied in the `IP_EVENT_STA_GOT_IP` handler now, where it cannot race association and runs again after every reconnect, and the return value is checked.
- **Why it looked like a BBS bug for weeks:** the delay lands on whoever pauses to read and then types, so it never appears while you are typing. `DASH 1` sends a frame and then goes deliberately quiet for a second, which is precisely the gap that lets the radio doze, which is why the dashboard was where it showed up worst. A strong RSSI reading made it look like anything but the radio; power save is not a signal problem.
- **The diagnosis was reached by pinging the board**, because ICMP is answered by lwIP on core 0 and never touches the BBS loop, the timeline or the card. When the stall reproduces with nothing running, the application is not the cause.

### Refresh screens send less

- A refresh screen redraws from home, so every row covered the row underneath by writing spaces out to the full width. At 132 columns a DASH frame was 4485 bytes, of which 2704 were padding. It erases to end of line instead, four bytes, and the frame is 2096. PETSCII has no erase to end of line and keeps the spaces.
- **This is a bandwidth saving and not a bug fix, and the distinction was paid for.** It was first committed with a comment claiming that a frame larger than `BBS_TL_BYTES` was written all-or-nothing and dropped whole, and that this was what stalled callers. That is not what the code does: `serviceWatch` draws row by row while the timeline has 512 bytes free and only begins a new frame once the previous one has drained, so a frame is built across as many passes as it needs. Measured on the board, a 2957 byte frame at 132 columns arrived intact every time for eighty seconds, and keypress latency during it was a flat 31 ms, so `BBS_RX_ROOM` was never starving input either. A plausible mechanism that the code does not implement is worse in a comment than no explanation at all, and both comments were corrected.

### Where you land

- **A caller picks where login puts them.** `Start` on the account form: `Main`, `Chat`, `Bulletin`, or `Default`. The sysop sets the board's own default with `landing` in CONFIG, and `Default` follows it, so changing the board setting moves exactly the people who never expressed a preference and nobody else.
- `Default` is 0, which is what every account written before this existed already parses as. Nothing needs converting.
- **`[H]ELP for commands.` moved.** It used to print to everybody at login. It is advice about the main prompt and only the main prompt, so telling somebody who is about to be dropped into the chat room to press H is directions for a place they are not going. It now prints only when the main prompt is where they actually land.
- **A landing this board cannot do falls back to the main prompt, silently.** That is what lets `Forums` be offered before the message boards are written, and it is also right for a board that has switched chat off: nobody should be greeted with "Unknown command" because of a preference they set months ago. One consequence worth knowing: the forums choice does nothing yet.
- **It is `Forums`, not `Bulletin`.** On this system "board" already means the BBS itself and "messages" would blur into MAIL, so forums is the word a caller who has never used a BBS already knows. It also settles a real collision: `screens/bulletin.*` is the notice screen that plays at login and is something else entirely, so the word was about to do three jobs at once. An account written by the earlier build carrying `land = bulletin` is read as `forums` and rewritten on its next save, because silently reverting somebody's choice to the board default is the kind of loss nobody thinks to look for.

### The room has a voice

- **Anything the board says in the room is marked `-->`.** The room has no prompt character, DDial style, so `No such command` was indistinguishable from somebody typing those words. Four columns removes the ambiguity.
- Deliberately not on everything. `***` join and leave notices keep their own mark, because they are events rather than answers; the welcome screen is artwork, not the board talking; and the room command list gets the marker on its heading only, since an arrow on all sixteen rows turns a table into a wall.
- **One change at `tell()` does it, decided by whether the caller is in the room.** `MAIL` at the shell and `/e` in the room run the same code and print the same sentences, and only one of the two is somebody standing in a chat room. Deciding it once is also what stops the two drifting apart.
- `color_marker` themes it like every other part of a room line.
- **`/s` now prints on the way in**, between the room banner and the join notices. The banner says how many are here; somebody walking in wants to know who, and making them type `/s` to find out the one thing they came in wondering is a step for nothing. Split out of `who()` so joining and `/s` cannot show different things.

### The card gets the screens

- **The Screens file area listed nothing on every board, and it was not a file-area bug.** The area points at the screen *override* folder, which the screen player reads before flash but which nothing ever wrote to. On a fresh card it was an empty directory and the area correctly listed zero files. Logs looked healthy beside it only because the caller log is actively mirrored there.
- The stock screens are now copied to the card on mount, 24 files and about 21 KB, **only where the card does not already have that file**. A file on the card is somebody's edit and the whole point of the override is that it wins, so this fills gaps and never overwrites. Flash is never written, so pulling the card still falls back to the set the board shipped with.
- A copy that fails anywhere removes its own half-file. A truncated screen on the card would override the good flash copy and play as line noise to every caller until somebody noticed.

### Fixed

- **`EXAMPLE` reported a path that does not exist.** It printed its storage path with `%.40s`, which is a truncation and not a pad, so a data directory more than about forty characters deep reported `.../p/example/co`. Same shape as the `%9.9s` that had `SYS` showing the board's address as `192.168.0`. A long line wrapping is untidy; a cut path is a wrong answer somebody goes looking for on disk.
- **A static assert that was wrong about its own record.** The `MailRec` guard summed field widths to 563 and compared that with `sizeof`, which is 564: it missed two bytes of padding that have always sat between `from[]` and `at`, and which `flags` and `spare` now occupy. It failed a layout that was byte-for-byte unchanged on disk. It asserts the total size and the offset of `at` now, which is what a file format is actually made of.
- **Three tests walked to `[ Save ]` by counting Enters.** Adding one field to the sign-up form left every registration sitting on the form, and the suite reported it as "bad email refused". Replaced with a `to_save()` helper that presses until the board gives a verdict, so the next field cannot do this again.
- COMMANDS.md still said version 0.10.0 and six caller nodes.

### Mail: Reply, Save or Delete

- **Reading is no longer disposing.** `MAIL` used to show a message and clear it in the same breath, so a caller whose line dropped mid-read, or who was paged, had simply lost it with nothing to go back to. The message is now shown and then `[R]eply  [S]ave  [D]elete:` asks what should happen to it. Nothing is touched until one of those keys is pressed.
- Each of the three is a single rewrite of the mailbox through a temp file and a rename, so a power cut either leaves the message alone or leaves the decision made, never half of it.
- **The three are mutually exclusive on purpose, and a reply retires the message it answers in the same rewrite.** That cannot be done as two steps without choosing which way to fail: delete first and a refused reply has thrown the original away, send first and a failed delete leaves somebody answering the same message twice. `mailSend` gained a `dropIdx` and now returns whether it stored anything, so a reply refused for a full box leaves the original sitting there to try again with.
- A reply takes the slot the message it answers gives back, so a board whose mail is full can still be replied to.
- **Save is `MF_KEPT`, not a second copy.** A kept message stays in the box, stops ringing "You have mail", and still counts against the limit, because it is still taking up room somebody else cannot use. `mailWaiting` and the room's own notice count unread mail rather than anything addressed to you, or S would be the wrong choice for the one thing it exists for.
- The abort keys and Enter leave the message unread, which is the outcome that loses nothing. Any other key is ignored rather than guessed at, because two of the three choices cannot be undone.
- **A caller reading mail at the shell is not in the chat room, even though the plugin owns their keys.** `MAIL` borrows the session so it can read single keys, and the code was answering "is this caller in the room" with "does the plugin own this session", which stopped being the same question. Split into `joined` (counted in the room, shown in `/s`) and `listening` (ready to be shown a line now). Without it, somebody reading mail at the shell prompt would be counted in the room and sent everything anybody said.
- Room lines are held while a caller is deciding and flushed when they are done, the same path that already holds lines for somebody part way through typing. Nothing is lost and nothing lands across the prompt.
- **A static assert that was wrong about its own record.** The guard on `MailRec` summed the field widths to 563 and compared that with `sizeof`, which is 564: it missed the two bytes of padding that have always sat between `from[]` and `at`, and which `flags` and `spare` now occupy. So it failed a layout that was byte-for-byte correct and unchanged on disk. It now asserts the total size and the offset of `at`, which is what a file format is actually made of.

### Files

- **The file door stops wiping its own banner.** Moving the highlight cleared the screen and redrew, so `screens/files` flashed away on the first cursor key and the door looked like a menu rather than a way in. The menu now redraws over exactly the rows it drew last time and leaves everything above it alone. Terminals that cannot move the cursor fall back to the clear, as before.
- **A finished upload is asked what it is.** A file with no description is a filename in a list, which tells the next caller nothing, and asking later means asking somebody who has moved on. The description is stored beside the file in the staging folder's own `FILES.BBS` and carried across when the upload is approved, so the uploader's own words survive the approval step.
- Somebody who could have approved the upload does not queue behind themselves: a caller with delete rights on the area goes straight live. Making a sysop approve their own upload is ceremony rather than review.

## 0.17.5, 2026-09-20

File transfer, and the file area finally does what a file area is for.

### Transfer

- **YMODEM, and it is the default both ways.** `DOWNLOAD <file>` sends by YMODEM; add `X` if your terminal only speaks XMODEM. A bare `UPLOAD` receives by YMODEM and takes the filename off the wire; `UPLOAD <file>` is the XMODEM form, which needs the name because XMODEM has none.
- **Why that matters, which is the only reason YMODEM is here: the length.** XMODEM has no length field, so its last block is padded out with `0x1A` and a received file can be up to 1023 bytes longer than the original. The engine refuses to strip that padding, and it is right to: `0x1A` is a perfectly legal byte inside a C64 `.PRG`, and a receiver that guesses truncates somebody's file. YMODEM's block 0 carries the exact byte count, so there is nothing left to guess.
- One transfer at a time, board-wide. XMODEM is stop-and-wait, so a transfer is mostly spent waiting, and a second engine would cost 1.1 KB of static RAM to overlap two idle things. A second caller is told to try again in a moment rather than queued.
- Nothing blocks. The engine is fed as the far end answers and nudged from the plugin tick for timeouts, so a transfer advances one block per round trip. That is XMODEM behaving correctly, not the board being slow.

### Uploads wait for staff

- An upload lands in a `.pending` folder inside the area and **is not in the area** until staff approve it. A staging folder rather than a flag on the file, and that is the whole safety argument: with a flag, every listing and download path has to remember to check it, and forgetting one check serves an unapproved file. With a folder, an unapproved file simply is not there. Approval is a rename on the same filesystem, so it is near atomic, and a power cut mid-upload leaves junk in staging rather than in the public area.
- `UPLOADS` lists what is waiting, `APPROVE` makes one live, `REJECT` throws it away. Staff are told the count at login, which costs no card reads because the count is kept rather than counted.
- A filename arriving in a YMODEM header is checked exactly as hard as one typed at a prompt, and one over 63 characters is refused rather than shortened. A file written under a name nobody chose is worse than a transfer that plainly did not start.
- Caps from the start rather than after somebody finds them: 4 MB per upload, 20 waiting per area.

### Four permission levels per area

- `area1 = path | name | read | up | down | del`. Read sees and lists, up puts files in, down takes them out, del removes them and approves or rejects uploads. Before this there was one `write` level doing two different jobs, which meant a board could not offer downloads without also offering uploads.
- Upload sits before download on that line even though it reads oddly. It is where the old single `write` level was, and moving it would silently have turned every configured area's upload level into its download level.

### Fixed

- **Long filenames on the card, without which a file area is unusable.** The IDF defaults FATFS to 8.3, where a name of more than eight characters is not awkward but *invalid*: FatFs refuses to create it and refuses to open it. A folder called `textfiles` was never created, the area listed as configured, and the only symptom was "that folder is not on the card" with nothing in the log. Every file already on the card with an ordinary laptop-made name was invisible too, which would only have surfaced once downloads worked. No host test could have caught this: the host build uses the real Linux filesystem.
- **A lost ACK could desynchronise a whole XMODEM transfer.** The sender times out and resends on its own clock while the receiver, having timed out on the same pass, already has a NAK on the wire for the same block. The sender acted on that NAK, sent a third copy, collected a second ACK for one block, and was one ACK ahead of itself for the rest of the file, failing near the end after every byte had already arrived. Whether the two timeouts align is luck, which is why this never showed up before. Found by writing the YMODEM loopback test.
- An area whose folder cannot be created now says which area and which path, instead of failing silently and leaving a sysop to find out from a caller.

### Notes

- Engine: 161 self-checks, clean under ASan and UBSan. `sizeof(Engine)` 1208 bytes, one 1K block plus change.
- Board: 494 checks with a card, 371 without. Flash 69.8%, static RAM 148,188 bytes.

## 0.17.2, 2026-09-20

The file subsystem. Areas you can walk into, a settings page that opens
other pages, and the groundwork that file transfer will stand on.

### Files

- **`FILES` is a place, not a command.** It takes the session the way `CHAT` does, plays `screens/files` if the board has one, and lays the areas out as a numbered menu in as many columns as the terminal has room for. A digit opens an area, `Q` goes back one level, `Q` again leaves. Going back one level rather than all the way out is the thing a subsystem has that a command does not.
- **An area is a folder mounted under a name**, and it can carry its own read and write levels: `area1 = pub/c64 | C64 Downloads | staff | sysop`. An area a caller may not read is not listed for them, and opening it by number is refused in the same words as a number that is not an area at all, so the command cannot be used to find out which numbers are hiding something.
- **Setting one up no longer means finding a PC.** Typing a path into CONFIG creates the folder on the card, parents and all.
- **`CONFIG files` shows each area as a button** that opens a page of its own, with Path, Name, Read and Write as proper fields and the levels as pickers rather than words you have to spell. Save lands you back on the files page. Plain ASCII has no cursor to put a button under, so it asks instead.
- Descriptions live in `FILES.BBS` in each folder, plain text, editable on a laptop with the card in hand. Written through a temp file and a rename, because FAT is not safe against losing power mid-write.
- The caller log is mirrored to the card, one plain text file per month. The ring on the logs partition stays what `LAST` reads, so pulling the card costs the long history and nothing else.
- `MEM` shows the card's free space, and now also shows the session pool beside the heap. The pool is the largest thing the firmware owns and it was invisible: decided at compile time, so it never appeared as heap usage, and a sysop looking at a small heap had no way to see the node count holding a hundred kilobytes.

### Ten nodes, not sixteen

- **Sixteen did not fit.** A session is 6,000 bytes and eighteen of them was 108,000 bytes of static RAM; the link failed with `dram0_0_seg overflowed by 104 bytes`. The figure that matters is not the 320 KB the part advertises but what is left for static data once the ROM and the radio have taken theirs, and PlatformIO's percentage is measured against the larger number: it read 54.9% while being over.
- Ten caller lines, plus the busy line and the hidden sysop node. That frees 36,000 bytes, which is what the transfer buffers and what follows them will be spent from.
- [ESP32_BOARD_CHOICE.md](ESP32_BOARD_CHOICE.md) records which parts this runs on and roughly how many nodes each would carry, with the arithmetic shown and a warning that only the WROOM has actually been tested.

### Fixed

- **The board had ten sockets, not the twenty four it asked for, and that was self-inflicted.** IDF 5.3.1 caps `CONFIG_LWIP_MAX_SOCKETS` at 16 and **discards an out-of-range value in a defaults file rather than clamping it**, silently. 0.17.0 "raised" it from a working 16 to 24 and the board quietly fell back to the default of 10; with the listener, mDNS and SNTP taking three, seven callers filled a board advertising sixteen nodes and the eighth connection simply failed. Found by reading a map file, not by anything failing, because nobody has had seven callers at once.
  It also reframes the node count: sixteen sessions would have fit in DRAM within about a hundred bytes. **The limit was sockets all along**, and sixteen of them is what makes ten caller lines the honest number for this part.

- **A config reload rewrote every long setting whether or not it had changed.** Values were compared using only their first 47 characters against a 96 byte buffer, so anything longer never matched itself. The long buffers exist precisely for values like announce's comma-separated directory list.
- **`SHOW`, `HIDE` and `LURK` did not tell the directory.** The published caller count includes a staff member only while they are visible, so toggling visibility changed what the board advertised and nothing pushed the update. There is one hook for it now, called from login, logoff and all three, rather than three calls bolted onto three commands.
- **An absolute card path was not understood.** `SD` prints the screens folder as `/sd/screens`, so that is what a sysop types, and it was being treated as relative to the card and turned into `/sd//sd/screens`, pointing the area at nothing.

### Groundwork for file transfer

- The XMODEM and YMODEM engine is written and tested on its own: 82 checks, clean under ASan and UBSan, 2,696 bytes of code, one 1K buffer. It knows nothing about sockets, sessions or the card.
- **Two ways binary would have been corrupted, both closed, and both invisible to an ordinary test.** Telnet normalised CR on input, silently deleting a `0x0A` or `0x00` that followed a `0x0D`; in a file those are data. And there was no outbound path that escaped IAC without also translating the charset, so nothing could carry a file out. A caller in raw mode now gets bytes rather than decoded keys, because during a transfer an `0x1B` is not an escape sequence and an `0x0D` is not Enter.
- The suite proves it rather than assuming: it sends every byte value, the CR pairs, and a run of `0xFF`, and checks the board counted and summed exactly what was sent. That test found an out-of-bounds read in the new raw path within minutes of existing.

## 0.17.1, 2026-09-19

The SD card. Its own version number because it is its own flash: 0.17.0 and
this were briefly the same version, which meant ABOUT could not tell a sysop
which of the two was on the board and PLUGINS was the only way to find out.
Every flashed build gets its own number.

### The SD card

- **An optional SD card, mounted FAT32 over SPI.** Four wires. A board with no card is still a complete board, and that is the case the tests run by default. What goes on the card is what grows without limit and can be lost: message bases, file areas, a sysop's own screens. What stays on internal flash is everything that has to survive the card failing, which is the accounts, the configuration and the caller log.
- FAT32 rather than LittleFS so the card can be pulled and read on any laptop. That is the whole point of it. The price is that FAT is not safe against losing power mid-write, which is exactly why nothing that matters lives there.
- `SD`, `SD MOUNT` and `SD UNMOUNT`, sysop only. **Mounting pauses the board** for a few hundred milliseconds while it negotiates over SPI, so it happens at boot before any caller exists, or when a sysop asks and is told. Nothing retries on a timer, and there is no insertion event: there is no card-detect line on this wiring, and probing the bus to find out would be the same stall repeated forever.
- `SD` with no card names the pins it tried. "No card found" on its own sends somebody to re-seat a card that was never the problem, and the three failures get three different messages, because "no card", "not FAT32" and "miswired" are three different evenings.
- A card is never reformatted to make an error go away. A card that will not mount is far more often somebody's card with their files on it than a card that wants erasing.
- Screens on the card override the stock set **per file**, so one custom screen does not mean supplying all of them, and pulling the card falls back rather than losing them.
- **Fixed: a config reload gave every plugin its commands a second time.** The command table was only reset at boot, so each reload added another copy of every running plugin's table until it was full. From then on whichever plugins came last were running, shown as running, and answering "Unknown command" to their own verbs. It took a reload to show, so nothing caught it, and at four plugins it had already been costing `announce` its commands. Found because a fifth plugin made it obvious.
- **Fixed before it shipped, by the code review:** saving any CONFIG page unmounted and remounted the card, stalling every caller's line; unmounting while a caller was paused mid-screen left a descriptor into a torn-down filesystem; a failed mount left the SPI bus holding the old pins, so correcting a pin in CONFIG changed nothing and the sysop was sent to check wiring that was already right; GPIO36 was refused for MISO although input-only pins are exactly what MISO is for; the open-file budget was five against sixteen nodes, so the sixth caller silently got the flash screen instead of the card's; and the screen lookup preferred any format on the card over the right format in flash, so one stray `.asc` would have taken every C64 caller off PETSCII while looking like it worked.
- Flash is 68.1% of the slot, up from 63.9%: FATFS and the SD driver cost 63 KB, and they cost it whether or not a card is fitted.

## 0.17.0, 2026-09-19

The partition rebalance and sixteen nodes. Flashed 2026-09-19.

### Sixteen nodes and a bigger user partition

- **The flash is split by what is actually stored on it.** `storage` held 18.5 KB of screens in a 736 KB partition, sized back when the accounts lived there too, and the hundred-account cap came from the 128 KB left over rather than from anything real. Same 896 KB of data region, redistributed: `logs` 32 KB, `userdata` 608 KB, `storage` 256 KB. `storage` stays last so a filesystem upload can still only reach the screens. **Breaking layout change:** one `pio run -t erase` before the first flash, because the old contents sit where the new ones go.
- Six caller lines become sixteen. That is 65 KB more static RAM, ten more sessions at about 5.7 KB each, and the socket budget goes to 24: every caller holds one, and the listener, mDNS and the backup window want theirs.
- **Fixed: a node number above nine printed as punctuation.** `nodeChar` returned `'0' + id`, so node 10 was `:` and node 11 was `;`. Replaced by `nodeName` for prose and `nodeLabel` for the fixed column in a list. Digits rather than letters, because a node argument is parsed with `strtol`: the number in the list has to be the number you type at the prompt.
- The caller log had the same bug one layer down. DASH printed `'0' + (node % 10)`, so node 12 would have appeared as "2" — worse than a wrong glyph, because it names a different line.
- Every list that gained a column gave one back. A 40 column row that becomes 41 wraps, and a refresh screen then leaves its own tail behind on every redraw. `NODES` was at 60 and 39 columns exactly.
- DASH could not grow a row per node: it redraws from home, so a frame taller than the terminal corrupts itself. Its node block is six rows plus a summary, busy lines first and free lines filling the rest. A quiet board looks the way it always did; a busy one spends its rows on callers instead of on "waiting for caller" sixteen times. WHO still lists every line.
- `max_users` is 250, not the ~1,380 the partition now holds. The cap is an index width: the account and list indices are all `uint8_t`, and one of them is the row counter in every list on the board. Widening it touches every list, which is not work to land in the same build as a partition move. Raising it later costs no erase.

## 0.16.1, 2026-09-19

- **Fixed: a token saved by the truncating firmware was still being sent.** 0.16.0 stopped storing a short token but happily loaded one, so a board that had run the old firmware kept posting its four-character wreckage and kept minting duplicate listings. Anything under 16 characters is now ignored on load and the board registers again cleanly.
- The activity LED holds for a full second once the board is actually listening. Wi-Fi being up is not the same as the board being ready, and without a sign the only way to find out was to dial in and be refused.
- The board records why it started. A crash, watchdog or brownout reboot is written to `reboots.log` on the logs partition with the time, the next staff member to log in is told in plain words, and `SYS` shows it beside the uptime. A board that restarts on its own is otherwise invisible: the only symptom is an uptime that keeps starting over.

## 0.16.0, 2026-09-19

- **Fixed: the board kept only the first few characters of its directory token.** The reply was read with a single `recv` into a 256 byte buffer and parsed immediately, but a TCP read boundary is not a message boundary: the 32 character token arrived split across packets and the board stored the four characters that had landed. It then never matched that token again, so every heartbeat minted a brand new listing. One board produced ninety of them in fourteen hours. The reply is now accumulated until the headers are complete, and a token shorter than 16 characters is refused outright rather than overwriting a good one.
- The board has a name of its own. `board_name` in `system.cfg`, `@BOARD@` in screens, and the announce plugin starts from it instead of asking you to type it twice. `@BBS@` still means the software, so the credit line stays true. Welcome and goodbye now lead with the board.
- A caller arriving or leaving pushes an update to the directory rather than leaving it up to ten minutes out of date. `nudge_seconds` (default 60, 0 disables) is the shortest gap between pushes, so six callers arriving together is one update.

## 0.15.2, 2026-09-19

- **Fixed: refresh screens showed `??nleashed BBS`.** The row truncation added in 0.14.0 walked the text a byte at a time, so the micro sign's two bytes each went through the charset map on their own and each came back as `?`. Counting columns is the terminal layer's job now (`Term::textCols`), because it is the only thing that knows which bytes make a character.
- Ctrl-L clears the screen and redraws what you were half way through typing, the way it does in every other shell. SHIFT+CLR/HOME does the same on a C64. Every byte below 0x20 except a handful was previously discarded before it ever became a key, so Ctrl-L had never arrived at all.

## 0.15.1, 2026-09-19

- `TIME -1` takes a line off the clock: no per-call limit, no daily limit, no idle hangup, until it hangs up. `TIME n -1` does it to somebody else's node. It lasts for the call only, so nobody ends up quietly unlimited for ever. `OFF`, `NONE`, `UNLIMITED` and `NOLIMIT` all work too. The cost is that `-1` no longer means "take one minute away".
- A sysop who has made themselves visible now counts in what the board tells a directory, so a board with somebody sitting on it stops advertising itself as empty. It reports 1 of 7 rather than 0 of 6.
- `/welcome` in chat replays the screen you came in on. Not `/w`, which has been the who list since 0.10.0.
- The board reports its offset from UTC in the announce payload, so a directory can describe its busy hours in local time.

## 0.15.0, 2026-09-18

Settings you can find, and a send-off everybody gets.

- A plugin now declares what `CONFIG` should offer. Until now a plugin's settings page was built from whatever keys `system.cfg` already contained, which meant a setting nobody had written yet was invisible: the announce plugin could read a board name, owner, description, DNS name and directory list, but there was no way to set any of them short of editing the file by hand. All nine are on the form now, on a fresh board, with the running value already in them.
- A board can advertise a name of its own (`yourboard.example.net`, say) instead of whatever address the directory saw, and can list itself in several directories at once with a comma-separated list. Both were always in the protocol; neither was reachable.
- The wordmark is redrawn with half-block characters, which carry two pixels per cell vertically and so allow a real stroke weight instead of chunky squares. The micro sign is set as a lowercase letter on the shared baseline with its stem below it, rather than a capital squashed to make room for a tail.
- New screens: the house rules when you press R to register, a short welcome once you are in, and a transition into chat. All three are optional, and a board without the files behaves exactly as before.
- The goodbye screen now plays however the call ended, not only when you typed BYE, and the line is held open for five seconds afterwards so it is not a screen that flashes past on its way to a closed socket. A caller who never logged in still gets the short version.
- HELP no longer truncates its own longest command.

## 0.14.0, 2026-09-18

Flashing the board stops costing you the board.

- The flash layout is split by who owns what. `storage` (736 KB) holds the screens and is the only partition a filesystem upload rewrites; `userdata` (128 KB) holds accounts, the live configuration and each plugin's files; `logs` is 32 KB, which is forty times the caller log rather than four hundred times. `storage` is kept last in the table because PlatformIO's `uploadfs` writes the last spiffs partition, so that is the only thing it can reach.
- `pio run -t flashall` is therefore no longer destructive: firmware and screens in one command, and the accounts, the configuration, the chat mail, the room ban list and the directory listing token all stay put.
- On a blank board the configuration is seeded once from the copy shipped with the screens. Without that, a fresh board would have no sysop password and no way ever to have staff.
- A restore routes each file in the backup zip back to the partition it belongs on. The zip format is unchanged and older backups still work.
- Nine checks cover the split, including simulating the destructive half of a filesystem upload and proving the accounts are still there afterwards.
- Breaking layout change: an existing board needs one full erase, because the partitions move.

## 0.13.0, 2026-09-18

The board can put itself on the map.

- `announce` plugin: a small heartbeat to a directory server so callers can find the board, and the board learns its own public address back from the reply, which is dynamic DNS for the price of a couple of hundred bytes every ten minutes. Off until switched on, never sends anything about a caller, and `ANNOUNCE TEST` prints the exact payload before anybody has to trust it. Several directories at once, comma separated.
- ANNOUNCE.md documents the wire format so anybody can run a directory, and says plainly that the default one's house rules bind the project rather than its users.
- The directory issues a token on the first heartbeat and the board writes it back into its own config through the same writer `CONFIG` uses, so a listing survives a reboot and nobody else can claim it. The reply also carries the listing's state and how long until it is public, which `ANNOUNCE` shows as `pending, public in 2h41m` rather than leaving a sysop staring at an empty page for three hours.
- `share_activity` (off by default) adds calls and caller-minutes over the last day, counted from the caller log. A directory can rank by them so a small board with five friends on it outranks a famous dead one. Counts only: no handles, no addresses, nothing about who.
- The board sends its heartbeat interval, so a directory knows when to call it quiet rather than guessing.
- The companion directory server is its own repository, also GPL v2 or later: github.com/rwmech/unleashed_directory

## 0.12.0, 2026-09-18

Disclosure before anybody types a password, and the documentation to go with it.

- Nobody types a password before being told the link is in the clear. Registering now warns that the connection is not encrypted and that the password must not be one used anywhere else, then asks "Would you like to know more?". Yes plays the new `privacy` screen: what telnet does and does not protect, how the password is stored, what the sysop can see, and an honest answer to "what is my real risk". The sign-up form opens when it finishes.
- `PRIVACY` shows the same screen at any time, and it is an ordinary screen file a sysop can rewrite.
- PUBLIC.md is new: how to put a board on the internet, what forwarding a port actually exposes, and the risks that are real. The address problem leads it, because a home connection's address changes and a board nobody can find twice is no use.
- CLIENTS.md is new: every machine that can call in, what terminal software it runs and what puts it on the wire, including phones. README carries the short version.
- README opens with what the board is for rather than a feature list.

## 0.11.0, 2026-09-18

Help, screens, the chat room's command set, messages and a settings manager.

- HELP is now a set of menus. `?` lists the commands people actually use, `? chat`, `? account`, `? staff`, `? sysop` list an area, and `? all` walks every section. Commands are ordered by how often they get used, each section has its own title bar, and the shortcut letter is picked out inside the word (`[W]HO`).
- Colour pass over the lists: WHO colours the node, the rank marker, the handle, what a caller is doing and the idle clock separately; the rank key under the list is drawn in the same colours as the markers; MEM is laid out as labelled figures with thousands separators.
- `SYS` (staff): the whole board on one screen, grouped into network, memory, storage, load and traffic. SSID, signal with a plain-English quality word, channel, address, heap, storage used and free, uptime, scheduler load in microseconds, nodes busy with the peak since boot, calls answered, plugins running and active IP bans.
- `CALLS` (staff): the caller log bucketed by hour of the day as a bar chart, with the busiest hour named. One pass over the log, no new storage.
- Chat colours are configurable. The node number, the punctuation that carries the rank, the handle, the text, replayed history and room notices each have their own `[plugin:chat]` colour key, and any C64 colour name works.
- Chat history depth is a setting (`history`), claimed once when the plugin starts and given back when it stops, so a board with more RAM can hold a whole evening of talk.
- A colour name parser in the terminal layer (`colorByName`), so settings files can name colours.
- `plat::micros()` and `plat::netInfo()` in the platform layer for loop timing and the network panel.
- Chat room commands: `/?`, `/p` for a private line, `/me`, `/a` away notes, `/sq` squelch, `/t`, `/clear`, on top of `/s` and `/q`. A squelch hides one node's chat for the rest of the call and never hides joins, leaves or moderation.
- Moderation: staff `/k` kicks a node out of the room, `/b` and `/unb` keep a room ban list in the plugin's folder, `/bans` lists it. With no staff in the room and three or more callers, `/vk` opens a vote: two thirds of everyone but the target, sixty seconds, and it can only remove somebody from the room, never ban them.
- Messages: one per caller, up to 512 characters, 32 slots, expiring after 14 days, all configurable. `MAIL` at the prompt, `/email` and `/e` in the room, "You have mail" at login and on the way into the room. Replacing an unread message tells the sender. The documentation says plainly that mail is not private.
- `CONFIG` (sysop only): the settings as pages, each one the same form the user manager uses, including a page per plugin. Only what changed is written, the rest of `system.cfg` keeps its comments and ordering, passwords are masked and left alone unless retyped, and the board reloads immediately.
- README: what the board is for, the long list of machines that can call in, and a hardware integration section covering Wi-Fi, the serial bridge and GPIO.
- CHAT.md documents the room and the message system.
- This changelog.

## 0.10.0, 2026-09-17, on hardware

Chat and the serial bridge, the first two real plugins.

- Chat room in the DDial and Gtalk style: one room, `#2:Daytona)` line tags carrying node, handle and rank, no blank lines between posts, no prompt character, just the cursor at the start of the line.
- A caller's own typing is never disturbed: lines that arrive while you are part way through yours are held until you press Enter, per caller, not for the room.
- Nothing is dropped. The room keeps a 48-line buffer, a caller who joins sees the last few lines, and anyone whose held lines are close to filling the buffer has their typing lifted, the room printed underneath, and their line put back.
- Per-caller rate limit, 80 lines a minute after a burst of 8, which is faster than anyone types and slow enough that nobody can flood the room. Only the caller who trips it is told.
- `/s` lists the room, `/q` leaves, `CHATCLEAR` empties the history.
- Chat is on by default; a board that only wants a log viewer can switch it off.
- Serial bridge plugin: one operator drives the second UART, everyone else watches, `SERIAL SET 9600 8N1` changes the line, 1 KB of scrollback, and slow watchers are told how many bytes they skipped rather than holding the board up. Flash, console and input-only pins are refused.
- Plugins carry their own default access levels, so a board works before anyone edits `system.cfg`.

## 0.9.0, 2026-09-17

The plugin API (phase C5).

- Static `Plugin` descriptors compiled in, with hooks for start, stop, tick, connect, login, logoff and keys.
- A plugin can own a session, so keys go to it instead of the shell, with a scratch word per session for its own state.
- Per-plugin `read`, `write` and `admin` levels on the ladder `all | users | staff | co2 | co1 | sysop`, set in a `[plugin:name]` section.
- Per-plugin storage under `<fs>/p/<name>/`, with a free-space floor the core keeps for itself.
- Requirements are checked at boot; a plugin that cannot run says why in `PLUGINS`.
- `ABOUT` screen, editable like any other screen file.
- Example plugin as a template: PING, POKE, ECHO and EXAMPLE.

## 0.8.0, 2026-09-17, on hardware

Staff ranks on accounts.

- Entering a staff password marks the account with that rank, so staff are recognised on later calls.
- Staff may only modify their own level and below.
- DDial-style markers everywhere: `>` co-sysop, `]` sysop, `*` guest, with a key line under each list.
- Co-sysops and sysops see hidden and lurking callers.
- Unknown keys in `users.txt` are reported with a line number instead of being silently dropped.
- Fixes from the first full code review of the account system.
- Measured on the board: heap free 143,344, minimum 118,268, largest block 110,592, session 5,596 bytes each.

## Licensing, 2026-09-17

- GPL-2.0-or-later across the tree, SPDX headers on every source file, purpose and design notes in each header, `LICENSE` with the full GPLv2 text and `THIRD_PARTY_NOTICES.md`.

## 0.7.0, 2026-09-17, on hardware

Guests and the polish pass.

- Guest logins: any unused handle, marked `*` in every list, 15 minutes, nothing saved, no staff elevation.
- The handle prompt is the same for everyone; accounts get a password prompt, new handles are offered registration or a guest call.
- Input effects in place: errors and passwords resolve on the line they were typed on, and the password field turns into `ACCESS GRANTED`.
- Page and broadcast alerts: a bell, a rubout, then the message.
- Title bars and rules on the lists.
- Staff see a Doing column in WHO and DASH.
- Wi-Fi signal strength on the dashboard.

## 0.6.0, 2026-09-17

User accounts (phase C2).

- `users.txt` as `[handle]` blocks, rewritten through a temp file and a rename.
- Sign-up form and user manager as cursor-driven forms on ANSI and PETSCII, line prompts on plain ASCII.
- Salted SHA-256, a thousand rounds.
- Three wrong passwords per call hangs up; five per handle in fifteen minutes locks it.
- `INFO`, `PROFILE`, `PASSWORD`, `USERS`, `USER ADD | EDIT | DEL`.
- `self_register` and `max_users` settings.
- Input backpressure: a socket is only read while the caller's output buffer has room, so a pasted burst cannot lose output.

## 0.5.0, 2026-09-17, on hardware

- Command registry (phase C3): `Command` tables with verbs, shortcuts, permissions and help, all generated from one place, ready for plugins to register into.
- `WHO n` and `DASH [n]` refresh screens that redraw in place without scrolling a 24-row terminal.
- TCP keepalive on caller sockets, so a C64 switched off at the wall drops its node.
- Activity LED.

## 0.4.0, 2026-09-17

- Backup window: hold the BOOT button while the sysop is logged in to open HTTP for a few minutes. Download needs no confirmation and redacts passwords; upload is staged, validated and applied only after the sysop says yes.
- Separate `logs` partition with fixed-size rings, never part of the backup.

## 0.3.0, 2026-09-16

- Renamed to µnleashed BBS, new screens.

## 0.2.0, 2026-09-16

- The core: listener, six caller nodes, busy line, hidden sysop node, connect-time terminal detection, screens, line editor with history, paged output, the message bus, paging, broadcast, do-not-disturb, staff access and IP bans.
