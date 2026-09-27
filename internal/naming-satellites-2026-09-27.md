# Naming the satellites (2026-09-27)

A proposal for Rob. Nothing is built or published from it. Once Rob picks,
the chosen lines go to `explain` for the site and screens, and to the link
lane for the firmware.

Settled before this: "satellite", "sat" for short, is the umbrella word, and
`SATS` lists them. Rob's brief: a sat is either a data feeder to the board's
plugins (camera, GPIO, sensors) or a door you go into (the building game),
"beam you up or teleport to the sat", best-sounding words as long as they
keep the sat theme.

## Bottom line

| Question | Recommend | Alternatives | One-line reason |
|---|---|---|---|
| Data-feeding kind | **feeder sat** ("feeder") | orbiter; sensor sat | Says what it does in plain words, and it is Rob's own "data feeder" |
| Kind a caller goes into | **door sat**; the program stays a **door** | game sat | "Door" is the word every old caller looks for; the sat theme lives in the device and the verb |
| Going in | **`BEAM`** (`BEAM name`, `BEAM n`) | `UPLINK`, `DOCK` | Four letters, generic English, pure sat theme, no clash with any verb, shortcut or room command |
| Coming back | no command: "**beamed back home**", and a **home key** | "beamed back down" | Inside a door every key belongs to the door, so the way back is a key, and "down" already means offline in LINK |
| Status words | **ready, asleep, no signal, full** | in range, overhead | Plain, and they fit a sat on a cable as well as one on the radio |
| Type names | **camera, door, gpio, sensor** | cam | Replaces "camsat" and "doorbox" on screen; the ids on the wire and in the pairings file do not change |

One real bug found on the way, in the link lane's code, not a naming
question: the door's way back (Ctrl-] three times) cannot work for two
large groups of callers. See "Hand-backs", item 1.

## What was checked

- Every verb and shortcut in the core and plugin command tables on main
  (`src/core/bbs_shell.cpp` and `src/plugins/*.cpp`) and on the link lane
  (`rel-1.2.0-link`), plus COMMANDS.md and CHAT.md.
- `findCommand` (bbs_shell.cpp:385) matches a whole verb or a single-letter
  shortcut, never a prefix, so a new verb can only clash by being exactly an
  existing verb or by taking a used letter.
- The link lane already ships user-facing words that this proposal renames:
  `DOORS [n]` ("games and more, on a door box"), `LINK` / `LINK PAIR` /
  `LINK FORGET n`, device kinds shown as `camsat` and `doorbox`
  (link.cpp:201-202), the headings `Kind State RSSI Heard Checked`, and the
  lines "The door box went quiet.", "You left the door.", "Opening Shed...".
  The site's /satellites page says "door box" too.
- Words already meaning something here, which rules them out as sat words
  under the "one word, one meaning" rule: **node** (a caller line, and the
  maker scene's usual word for an extra ESP32, see Hackaday), **probe** (the
  connect-time terminal probe, the card probe), **relay** (the planned
  routing middleware, and GPIO relays), **beacon** (a LINK frame type),
  **drop** (the `DROP` staff command, although "drop to a door" is the
  period phrase), **landing** (where a caller lands after login),
  **station** (DDial's word for a linked board, which the superchat will
  bring back).

## 1. Names for the two kinds

### The kind a caller goes into: door sat

**Recommend: the device is a "door sat"; the program on it stays a "door".**

Door should survive, and it is not a close call. A door has meant "the
program the board hands you over to and takes you back from" since the
1980s (Wikipedia's definition is exactly that), the door networks of today
still use it (BBSLink, DoorParty), and a caller who has used any board
types DOORS without thinking. Replacing it with a sat word would save the
theme and cost the one thing the theme is for: the caller recognising what
this board has.

The two words also do different jobs, which is the naming rule working
rather than bending: the **door** is the program (the building game), the
**door sat** is the box it runs on. One door sat can offer several doors
(DOOR LIST carries up to eight), so they genuinely need two names. "Door
box" retires, in favour of "door sat", so the device has one name.

- Alternative: **game sat**. Plainer for a newcomer, but LINK.md says doors
  are "games and utilities", and a door sat running a weather door or a
  text adventure editor would be misnamed.
- Rejected: **station**. The best space word for a place you go aboard, but
  DDial used "station" for a linked board ("each station sends an
  abbreviated form of its /S list"), and the superchat will link stations
  in exactly that sense.

### The data-feeding kind: feeder sat

**Recommend: "feeder sat", "feeder" for short.**

It says what the thing does (it feeds the board pictures, readings and pin
states) in Rob's own words, and a caller who meets it needs no explanation.
It also makes the site's best example write itself: a feeder sat at the
birdfeeder.

The class word matters less than it looks. A caller sees the type (camera,
sensor) in SATS, never the class, so "feeder" lives mainly on the site, in
CONFIG's headings and in the docs.

- Alternative, more themed: **orbiter**. Real spaceflight usage: an orbiter
  circles and looks, and nobody goes aboard. Good copy ("orbiters watch,
  door sats take you in"), but a reader has to be told what it means.
- Alternative, plainest: **sensor sat**. Wrong for the camera (to a caller
  a camera is not a sensor) and for GPIO outputs, if a GPIO sat ever drives
  a relay.
- Rejected: **feed** as the noun (in FidoNet an echomail "feed" is the
  board you take mail from, and the federation work may bring that back;
  "feeder" is the device, "feed" is not used). **Spy sat**: callers would
  love it, and it is the wrong thing to say about a camera on a board that
  makes the privacy argument openly. **Probe**, **relay**, **beacon**,
  **node**: taken, above.

### The umbrella: widen it by one sentence

LINK.md already lets a door box sit on a serial cable, and be a PC or a
Raspberry Pi with a USB adapter. The settled definition says a sat is "an
extra ESP32 box linked over ESP-NOW", so a Pi door box on a cable would be
a sat that is not one. **Suggest: a sat is anything on the µnleashed link,
by radio or by cable.** If the site wants a word for the cable kind,
**tethered** is the real spaceflight term (NASA and ASI flew the Tethered
Satellite System on STS-46 in 1992), and it is accurate: same sat, held on
a wire.

## 2. The verb: BEAM

**Recommend `BEAM`.** Rob's own word, four letters, the same on every
keyboard that calls here, and it says "you are about to go somewhere" in a
way `DOORS 2` never did.

- `BEAM` alone shows the doors list (the same as `DOORS`).
- `BEAM name` goes to a door or a sat by name (`BEAM blockland`, `BEAM
  shed`). A sat with one door goes straight in; a sat with several shows
  its doors to pick from.
- `BEAM n` goes into door n **from the DOORS list**. SATS shows names, not
  numbers, to callers, so a caller never sees two different "2"s: one
  number per thing, and it is the number on the list in front of them (the
  forums lesson of 0.21.6).
- `DOORS` stays, on the main menu, as the classic door menu, and `DOORS n`
  keeps working through the same path. One HELP row for both, the way
  `G | BYE` is one row: `DOORS | BEAM   games up on the sats` (12 and 20
  characters, inside the 40-column HELP layout).
- `DOOR` as a hidden alias of `DOORS`, for fingers that remember other
  boards. Optional: a hidden `TP` alias of `BEAM`, a wink for anybody who
  plays block games, where /tp is the teleport command. The verb is
  generic; skip it if it reads as cute.
- **No shortcut letter.** `B` is free, but it is where some callers' hands
  go for "bye", and a caller logging off who lands in a game is a bad
  minute. Typing four letters to go somewhere is not a burden.
- **No room command in 1.2.0.** `/b` already means bell (alone) and bar a
  handle (with an argument) in the room, so a room verb would have to be
  `/beam` in full. Callers leave with `/q` and beam from the prompt.
- The staff Doing column shows `BEAM` (it holds the verb, as it does for
  every command), rather than the lane's `DOOR`.

### Collisions checked

| Candidate | Existing verb? | Existing shortcut? | Room command? | Other |
|---|---|---|---|---|
| **BEAM** | no (BYE, BELL, BANS, BAUD, BACKUP, BROADCAST, BULLETIN share only the B) | B unused | /b is bell and bar; /beam free | Generic verb. No BBS product called Beam found. Keep the catchphrase ("Beam me up") and "energize" / "transporter" out of the copy |
| UPLINK | no | U unused at the prompt (U is upload inside FILES) | free | Real radio term, and fits "up". But *Uplink* (Introversion, 2001) is a well-known hacker game with a cult following in exactly this audience, and in radio the uplink is the signal, not a passenger. Six letters |
| DOCK | no | D unused at the prompt (D is describe or delete inside subsystems) | free | Plain, four letters. But you dock with a station, not with a satellite, and "dock" reads as a USB dock to a maker |
| LAUNCH | no | L unused at the prompt | free | You launch a sat, not a caller; and "launch" is what every app does, so it adds no flavour |
| TELEPORT | no | T is TERM | /t is time | Eight letters on a C64 keyboard every time. And Teleport is a well-known SSH access product (goteleport.com), on a board about to add SSH. Fine in copy, not as the verb |
| WARP, JUMP | no | W is WHO, J unused | /w is /s | WARP is travel, not transfer, and a Star Trek and Cloudflare word; JUMP collides with "jump host" in SSH and with jumping in lists |

### Coming back

There is no command for coming back, and there cannot be one: inside a
door every key is the door's (raw input, doors.cpp `onBytes`). The way back
is one of three things: the door finishes, time runs out, or the caller
presses the **home key**. So "coming back" is a word on screen and a name
for a key, not a verb:

- On screen: **"beamed back home"**. Not "beamed back down", although it
  completes the "up" nicely: LINK's staff list already shows a peer as
  `up` or `down`, and "Shed is down" must keep meaning "Shed is off the
  air".
- The key: call it **home** everywhere ("Home is Ctrl-C three times").
  Which key it should be is item 1 of the hand-backs.

## 3. The on-screen lines

The board's voice, marked `-->`. Widths measured with a 16-character sat
name (the pairing's name field is 16), a 24-character door name (DOOR
LIST's field) and a 40-character board name (`boardName[41]`). Everything
in the 40 column is at most 39; everything in the 80 column is at most 79.

| Moment | 40 columns | 80 columns |
|---|---|---|
| Leaving | `--> Beaming up to Shed...` (37 max) | `--> Beaming up to Shed. Ctrl-C three times brings you home.` (71 max) |
| The home key, after the door answers | `--> Home is Ctrl-C three times.` (31); PETSCII: `--> Home is RUN/STOP three times.` (33) | on the leaving line, above |
| Back, the door finished | `--> Beamed back home.` (21) | `--> Beamed back home to The Rusty Antenna.` (65 max) |
| Back, time's up | `--> Time's up. Beamed back home.` (32) | same |
| Back, the sat vanished mid-door | `--> Lost the signal. Beamed back home.` (38) | `--> Lost the signal from Shed. Beamed back home.` (60 max) |
| Sat asleep or out of range | `--> No signal from Shed.` (36 max), then `--> It may be asleep or out of range.` (37) | `--> No signal from Shed: asleep or out of range. Try later.` (71 max) |
| Sat full | `--> Shed is full.` (29 max), then `--> Try again in a few minutes.` (31) | `--> Shed is full right now. Try again in a few minutes.` (67 max) |
| One door full (DOOR REFUSED, full) | `--> Blockland is full.` (37 max), then the same second line | one line, as above |
| The sat did not answer in 5 s | `--> Shed didn't answer.` (35 max), then `--> Try again in a few minutes.` | one line |
| The link is busy | `--> The link is busy. Try again soon.` (37) | same |
| No sats at all | `--> No sats in range.` (21) | same |

Notes on the lines:

- A door's own words (FINISHED and REFUSED carry up to 60 characters) are
  the door's voice, not the board's: print them on their own line without
  the `-->`, then the board's line. At 40 columns they wrap at a word.
- "Shed is full" is for a sat whose sessions are all taken; "Blockland is
  full" for one door at its player limit. REFUSED's reason code says which.
- "asleep" and "no signal" are told apart only in SATS, where the board
  knows the sat is set to deep sleep (it is the source of truth for that
  setting, CAMERA SETTINGS `sleep`). On the way in, one honest line covers
  both.

### The transition, from what fx:: already has

- **Going up:** the leaving line ends in `fx::dots` (three dots, about
  250 ms each) while the board waits for OPEN_OK; if the door is slow, an
  `fx::spinner` in `Spin::Dots` (`. o O o`, a signal pulsing) until it
  answers or the 5 s timeout. When it answers, one short `fx::lineNoise`
  burst (about 12 characters, held 150 ms, then erased): the static of the
  hand-over. Then the home line, then the door.
- **Coming home:** `fx::scramble` on "Beamed back home.", four rounds at
  about 40 ms: the words resolve out of noise, the arrival mirroring the
  departure. It is the same effect as the `@SCRAMBLE@` code callers
  already use.
- **Degrading:** ANSI and PETSCII get all of it (the spinner is quadrant
  blocks on PETSCII, `kScramb` is ASCII-only by design). **Plain ASCII gets
  the dots only and the words printed once**: the noise and the scramble
  work by backing over what they printed, and CLIENTS.md lists a Teletype
  Model 33, which has no way to take a character back off paper. Every step
  goes through `fitSteps`, so a full output buffer shortens the effect
  rather than delaying the door (rule no. 1).
- Total, under a second and a half on a quick sat. Any key skips it, as
  with every other animation.

## 4. SATS

One list of devices, readable by everybody. Door sats first (they are the
ones a caller can go to), then feeders, each group by name. No numbers for
callers (see section 2).

### Callers, 40 columns

```
 Name             Type    Status
 Shed             door    ready
 Garden           camera  ready
 Greenhouse       sensor  asleep
---------------------------------------
BEAM name goes up. SATS name tells more.
```

Widest data row with a 16-character name: 36 columns. The footer line is
40, one too many; at 40 it reads `BEAM name goes up.` alone, and `SATS
name` is in the long help.

### Callers, 80 columns

```
 Name             Type    Status    Latest          Try
 Shed             door    ready     2 of 4 aboard   BEAM shed
 Garden           camera  ready     photo at 14:02  SNAPSHOT garden
 Greenhouse       sensor  asleep    21.5 C at 13:55
```

Columns 1+16, 1+7, 1+9, 1+15, then Try: the widest row, a 16-character
name in both Name and Try, is 77.

- **Latest** is what the sat last said, in words: callers aboard for a door
  sat, the last photo's time for a camera, the last reading for a sensor
  (metric, then the time). This is the column that makes somebody on an
  8-bit machine call a friend over: the greenhouse temperature, from 1985.
- **Try** is the command that reaches it. It teaches the verbs without a
  help screen.
- `SATS name` shows one sat in full: type, status, latest, what it offers
  (a door sat's doors, a camera's sizes), and how to reach it.

### Status words

| Callers see | Means |
|---|---|
| ready | on the link and answering |
| asleep | a sat set to deep sleep, between wake-ups (the board knows, it sends the setting) |
| no signal | should be answering and is not |
| full | a door sat with every session taken |

Staff see the same four, plus `pairing` (a pairing in progress) and
`not checked` (paired without the code being confirmed, from LINK's
Checked column).

### Staff

Staff get three more columns at 80 in place of Try: **Signal** (dBm),
**Heard** (last frame, as a time), **Boards** (`2 of 5`, the boards sharing
this sat, from "one satellite, many boards"). At 40 each sat gets one
indented second line: `    -67 dBm, heard 14:02, boards 2/5` (36).

**Suggest SATS takes over LINK's device table.** LINK today lists the
pairings (`Kind State RSSI Heard Checked`) and the radio's counters. With
SATS, that is two lists of the same devices under two names. SATS becomes
the one list of devices; `LINK` keeps the radio itself: channel, frames,
retries, drops, loop time, ring high-water. Pairing moves to CONFIG sats
(already planned), and `LINK PAIR` and `LINK FORGET` stay as hidden
aliases for anybody who learnt them in a preview.

## 5. CONFIG sats and the type names

### The page

`CONFIG sats`, one button per paired sat carrying its name (the file areas
pattern), then a **Pair a new sat** button. Labels at 40 are 9 characters
at most:

| 40 label | 80 label | What |
|---|---|---|
| Name | Name | 16 characters, what callers see and type after BEAM or SNAPSHOT |
| Type | Type | read-only: camera, door, gpio, sensor |
| Status | Status | read-only, the staff status words |
| Gets | This board gets | feeders: pictures, readings or pins, on or off; door sats: which of its doors to offer |
| Seen by | Who sees it | level, for SATS |
| Used by | Who may use it | level, for BEAM or SNAPSHOT |
| Boards | Boards sharing it | read-only, `2 of 5` |
| Unpair | Unpair this sat | button, asks `Unpair Shed? (y/N)` |

"Unpair", not "forget": Rob's word in the CONFIG sats note is "pair and
unpair", and one verb per act. The pairing question keeps its shape with
the type in place of the old kind name: `Pair camera "camsat-6cc8"
20:50:0d:18:6c:c8, code 4821? (y/N)`.

The page heading for the two groups, if CONFIG lists them apart: **Door
sats** and **Feeders** (9 characters at 40: `Door sats`, `Feeders`).

### Type names

| Type (on screen, CONFIG, console) | Class | Wire kind id | Replaces |
|---|---|---|---|
| camera | feeder | 1 (unchanged) | camsat |
| door | door sat | 2 (unchanged) | doorbox |
| gpio | feeder | new | |
| sensor | feeder | new | |

- The pairings file stores the kind as a **number** (link.cpp:308), so
  renaming what `kindName` shows needs no migration. One side effect: a sat
  paired with no name of its own is named after its kind (link.cpp:370),
  so new unnamed pairings would be called "camera" or "door" rather than
  "camsat" or "doorbox"; pairings already made keep their stored names.
- **camsat stays the name of the firmware and its repository**
  (unleashed_camsat), which is a product name, not a type. The type a
  sysop sees is "camera".
- "camera" is also the name of a board's built-in camera in `SNAPSHOT` and
  `CAMERA`. That is fine as a type (a type column is not a name column),
  but a camera sat should never be *named* "camera" by default, or
  `SNAPSHOT camera` becomes ambiguous. Default an unnamed camera sat to
  "camsat-" plus the end of its MAC, as the satellite already does.
- **gpio or sensor, one type or two?** The GPIO plugin's queued design
  folds sensors into the GPIO point table (a point has a type: DHT22,
  DS18B20, PIR). If a sensor sat is just remote points, then one type,
  `gpio`, with sensors as its points, and SATS says "sensor" in Type when
  every point on it is a sensor. Kept as two in the table above because
  that is Rob's list; worth settling when the GPIO plugin is designed.

## Hand-backs

1. **The door's way home does not work for two groups of callers** (link
   lane, doors.cpp `onBytes`: three 0x1D bytes in a row).
   - **PETSCII callers are thrown out of the door by three cursor-rights.**
     On PETSCII, 0x1D is CRSR RIGHT (the board's own term.cpp:597 decodes
     it as `KEY_RIGHT`). In a building game, three rights in a row is
     walking, so every PETSCII caller gets ejected by moving.
   - **Callers on a plain telnet client never reach the board with it.**
     Ctrl-] is the standard telnet client's escape character (it opens the
     client's own command prompt), and CLIENTS.md lists GNU inetutils
     telnet as a way in.
   - Suggested instead: **0x03 three times**. It is Ctrl-C on every ASCII
     and ANSI terminal and RUN/STOP on PETSCII (term.cpp:593 and :630 both
     decode it as `KEY_BREAK`), so it is the one byte that means "stop" on
     every machine the board serves, and it is already the documented
     abort key everywhere else on the board. Three in a row inside a short
     window (say 2 s) keeps a door that uses Ctrl-C itself usable. The
     home line on screen then names it per terminal class: RUN/STOP on
     PETSCII, Ctrl-C elsewhere. This is the link lane's call; the naming
     above works with whatever key it picks.
2. **Site, for `explain`:** /satellites says "door box" (becomes "door
   sat"), and has a "Radio satellites" heading for a kind still to be
   designed. Every sat uses the radio, so "radio satellite" will read as
   "all of them". Worth asking Rob what that kind carries (LoRa? a
   433 MHz receiver? an SDR?) and naming it by that, the way camera and
   sensor are named. Not reserving a type name for it until then.
3. **Link lane strings to change with the rename**, once Rob picks:
   "on a door box" (DOORS help), "No door boxes: the link is off.", "No
   door box is on the air.", "The door box went quiet.", "Opening
   Shed..." (becomes the Beaming line), LINK's `Kind` heading (becomes
   `Type`), and `kindName`'s two strings.
4. **Doing column:** `BEAM`, set by the verb as for every command; the lane
   sets `DOOR` by hand today.

## Sources

- [Door (bulletin board system), Wikipedia](https://en.wikipedia.org/wiki/Door_(bulletin_board_system)): the definition, "drop to" a door, dropfiles, BBSLink as today's remote door host.
- [Door server providers: DoorParty, BBSLink, Exodus (ViSiON/3 issue #382)](https://github.com/ViSiON-3/vision-3-bbs/issues/382) and [DoorParty in the WWIV docs](https://docs.wwivbbs.org/en/latest/chains/doorparty/): "door" is still the live word in the BBS revival.
- [ENiGMA½ BBS: Door Servers](https://nuskooler.github.io/enigma-bbs/modding/door-servers.html).
- [Diversi-DIAL archives](https://www.ddial.com/archives.php): "station" for a linked board (quoted in CLAUDE.md).
- [Hackaday.io: an ESP-NOW mesh](https://hackaday.io/project/166868-my-attempt-at-an-esp-now-mesh) and [Random Nerd Tutorials: ESP-NOW](https://randomnerdtutorials.com/esp-now-esp32-arduino-ide/): makers call the extra boards "nodes", which is why that word is out here.
- [Uplink (video game), Wikipedia](https://en.wikipedia.org/wiki/Uplink_(video_game)) and [Uplink on Steam](https://store.steampowered.com/app/1510/Uplink/).
- [Teleport (software), Wikipedia](https://en.wikipedia.org/wiki/Teleport_(software)) and [Gravitational is now Teleport](https://goteleport.com/blog/gravitational-is-teleport/).
- [telnet(1), Oracle](https://docs.oracle.com/cd/E19455-01/806-0624/6j9vek5i5/index.html), [telnet, QNX](https://www.qnx.com/developers/docs/6.4.0/neutrino/utilities/t/telnet.html) and [tn, IBM AIX](https://www.ibm.com/docs/en/aix/7.3.0?topic=t-tn-command): Ctrl-] is the telnet client's escape character.
- [PETSCII, Wikipedia](https://en.wikipedia.org/wiki/PETSCII) and [C64-Wiki: control character](https://www.c64-wiki.com/wiki/control_character); the byte values above were confirmed in the board's own `src/core/term.cpp`.
- [TSS 1, 1R, Gunter's Space Page](https://space.skyrocket.de/doc_sdat/tss-1.htm) and [NASA NTRS: results from the TSS missions](https://ntrs.nasa.gov/api/citations/20160006982/downloads/20160006982.pdf): "tethered satellite".
