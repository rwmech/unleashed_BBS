# screens/codes: the copy

The words for `screens/codes`, the screen `CODES` shows at the main prompt
and `/codes` shows in the chat room. Written by `explain` on 2026-09-22 from
the decision in CLAUDE.md ("Inline codes in messages: decided 2026-09-22").
`screen-artist` draws it. Nothing else in the tree was touched.

Every line below was measured by script, not by eye: width, lines per page,
characters a C64 cannot type (`{ } | ~ \ _ ^` and backtick: none present),
and a copy of the screen player's own token scanner (`byteIn`, `tokChar` and
`runToken` in `src/core/screens.cpp`), which is how the problem in the next
section was found.

The rulers inside each block are for checking widths. They are not part of
the screen and are not counted in the line totals.

## Read this before drawing: four codes the player runs instead of showing

The screen player expands its own @-codes in every screen file, and four of
the message codes share their names:

| On the screen | What the player does with it today |
|---|---|
| `@BOARD@` | prints the board's name in its place |
| `@DATE@` | prints today's date |
| `@TIME@` | prints the time |
| `@BELL@` | rings the caller's bell |

Drawn as written, the fill-ins line would read as the board's name followed
by "this board's name", and the screen would beep at everyone who opens it.
They sit on 40-column page 2 (lines 12, 15 and 16) and on the 80-column page
(lines 14 and 15). Everything else prints literally, checked: `@@`, `@N@`,
the colours, the effects, `@SPIN@` (the player only knows `@SPIN:ms@`),
`@-codes`, and the example line.

The player abandons a token at the first byte that is not a letter, a digit
or a colon, and an abandoned token prints as typed. So put one invisible
non-token byte straight after the opening `@` of each of those four:

- `.ans`: any SGR, for example one that re-asserts the colour already in
  force. ESC is not a token character.
- `.seq` / `.p40`: any PETSCII colour byte. None of them falls in the ranges
  `tokChar` accepts (0x41-0x5A, 0xC1-0xDA, digits, colon).
- `.asc`: a bare CR, 0x0D. `Term::ch` drops CR in text mode
  (`src/core/term.cpp:237`), so it breaks the token and prints nothing.
  **Only in `.asc`.** In `.ans` a CR goes out raw and sends the cursor to
  column 0, and in PETSCII 0x0D is RETURN.

A colour change in front of the `@` does not help. The break has to sit
between the `@` and the name.

Two more traps from the same scanner:

- **Do not change the words in the example line.** Text between two codes is
  scanned as a token too, so `@YELLOW@Time@N@` would print the clock and
  `@RED@cls@N@` would clear the reader's screen. The words to keep out of
  that position are BBS, BOARD, VER, NODE, NODES, USER, TERM, COLS, DATE,
  TIME, CLS and BELL.
- If the player is ever taught `@@` as a literal `@` (the rule messages
  follow), every `@@` on this screen has to become `@@@@`, or "@@ prints one
  @" will read "@ prints one @".

Worth one check once it is drawn: play each flavour on the host and assert
the output contains the literal text `@BOARD@` and no 0x07 byte.

## For the builder: /codes in the room will lose pages 2 and 3

If `/codes` is built on `Bbs::showScreen`, the way `chatin`, `files` and
`forums` are, a 40-column caller sees page 1 and nothing else. At a form feed
`pump()` sets `paused_` and returns (`screens.cpp:299-303`), every later
`pump()` returns straight away while paused (`screens.cpp:282`), and
`showScreen` loops to its guard and closes the player
(`bbs.cpp:1846-1850`). Nothing says anything was dropped. It also stops once
the caller's timeline has under 256 bytes free (`bbs.cpp:1847`). The
80-column page is about 1,200 bytes of text, and about 1,700 with a colour
change either side of every code, against a 3,072 byte timeline that may
already hold the room's own output. Probably fits; worth measuring rather
than assuming.

The copy cannot fix this. Either `/codes` hands the caller to the paged
player the way `CODES` at the prompt does and puts them back in the room
afterwards, or the room shows something shorter.

## Roles, for colour

The same five kinds of line the privacy and rules screens use:

- **Title**: `CODES IN YOUR MESSAGES`, `THE FINE PRINT`.
- **Headings**: `COLOUR`, `EFFECTS`, `DROP-INS`, `FILL-INS`. On 80 columns
  they are the left-hand label column.
- **Codes**: from an opening `@` to its closing `@`. One colour for all of
  them teaches the reader "this colour is something you type". The colour
  names are the case for an exception, each drawn in its own colour. If so,
  check `@BLUE@` against a blue C64 background and `@DARKGREY@` against
  black. On ANSI, ORANGE and BROWN come out identical and so do GREY and
  LTGREY (`term.cpp:63-66`, both pairs map to the same SGR). That is
  accurate, and 40-column page 3 says so.
- **The example line**: set apart from the prose, codes in the code colour.
- **Page markers**: dim, as on privacy.

## The 40-column version

For C64 PETSCII and plain ASCII. Three pages, separated by form feeds like
`privacy`. Every line is at most 39 characters, and 22 lines is the most any
page has, which is exactly what a 24-row terminal pages at (`pageRows` is
rows minus 2), leaving room for "Press SPACE to continue".

### 40 columns, page 1 of 3 (22 lines)

```
         1         2         3
123456789012345678901234567890123456789
CODES IN YOUR MESSAGES

Type a code between two @ signs in a
forum post, in mail or in chat, and
everyone who reads it sees the effect.

  @YELLOW@Hello@N@ @OOPS:nerds@friends

Readers see "Hello" in yellow, then
"nerds" typed out and rubbed away,
and "friends" in its place.

COLOUR
@RED@ @GREEN@ @BLUE@ @YELLOW@ @CYAN@
@PURPLE@ @ORANGE@ @BROWN@ @WHITE@
@LTRED@ @LTGREEN@ @LTBLUE@
@GREY@ @LTGREY@ @DARKGREY@

Colour lasts until @N@ or the end of
the line.

Page 1 of 3
```

### 40 columns, page 2 of 3 (22 lines)

```
         1         2         3
123456789012345678901234567890123456789
EFFECTS, on up to 40 characters
@BLINK:text@     flashes a few times
@SCRAMBLE:text@  unscrambles into place
@TYPE:text@      types letter by letter
@OOPS:text@      types, then rubs out

DROP-INS
@SPIN@   a short spinner
@DOTS@   dots, one at a time
@NOISE@  a burst of modem line noise
@RULE@   a line to the end of the row
@BELL@   rings the bell, once a message

FILL-INS
@BOARD@        this board's name
@DATE@ @TIME@  the moment it is read

@@ prints one @. Anything that is not
a code prints as typed, so an email
address is fine.

Page 2 of 3
```

### 40 columns, page 3 of 3 (22 lines)

```
         1         2         3
123456789012345678901234567890123456789
THE FINE PRINT

Up to 8 codes work in a message, and
an effect holds up to 40 characters.
Past that, codes print as typed.
Codes work in lower case too.

A plain text terminal drops the colour
and keeps the words. Except on a
Commodore, ORANGE and BROWN look
alike, and so do GREY and LTGREY.

OOPS is a joke, not a delete: every
reader sees the words go by.

No code can clear a reader's screen,
pause it or slow it down. Those reach
into somebody else's screen and time,
so they are not on offer.

Old BBSes like PCBoard had @-codes too.
Page 3 of 3
```

## The 80-column version

One page, 21 lines, for ANSI terminals. The same words packed into a label
column, with three of the 40-column extras left off for room (see below).

### 80 columns, page 1 of 1 (21 lines)

```
         1         2         3         4         5         6         7
123456789012345678901234567890123456789012345678901234567890123456789012345678
CODES IN YOUR MESSAGES

Type a code between two @ signs in a forum post, in mail or in chat, and
everyone who reads it sees the effect. This line
    @YELLOW@Hello@N@ @OOPS:nerds@friends
shows "Hello" in yellow, types "nerds" and rubs it out, then "friends".

COLOUR   @RED@ @GREEN@ @BLUE@ @YELLOW@ @CYAN@ @PURPLE@ @ORANGE@ @BROWN@
         @WHITE@ @LTRED@ @LTGREEN@ @LTBLUE@ @GREY@ @LTGREY@ @DARKGREY@
         Colour lasts until @N@ or the end of the line.
EFFECTS  @BLINK:text@    flashes a few times  @TYPE:text@ a letter at a time
         @SCRAMBLE:text@ unscrambles in place @OOPS:text@ types, then rubs out
DROP-INS @SPIN@ a short spinner    @DOTS@ dots, one by one  @NOISE@ line noise
         @RULE@ a line to the edge @BELL@ rings the bell, once a message
FILL-INS @BOARD@ this board's name @DATE@ @TIME@ the moment it is read

@@ prints one @. Anything that is not a code prints as typed, so an email
address is fine. Codes past 8 in a message, and effects over 40 characters,
print as typed too. A plain text terminal drops the colour. OOPS is a joke,
not a delete: everyone saw it. No code can clear a reader's screen, pause it
or slow it down: those reach into somebody else's screen and time.
```

Only on the 40-column version, for room: the history line, "Codes work in
lower case too", and the note that ORANGE/BROWN and GREY/LTGREY look alike
except on a Commodore. Lower case matters least at 80 columns, where a PC
keyboard makes capitals free. The colour note matters more there than at 40,
because the 80-column reader is the one who will type `@GREY@` and see no
change. If a line can be found for it: "Except on a Commodore, ORANGE looks
like BROWN and GREY like LTGREY." (68 characters).

Not on either version: a line saying `CODES` and `/codes` show this screen.
Whoever is reading it got here that way, or from the pointer in the editor's
header, and page 3 needed the room for the limits.

## Wording checked against the renderer

A renderer appeared in the tree while this was being written, untracked and
not mine: `src/core/codes.h` and `src/core/codes.cpp`. These lines were
checked against it as it stood, and each one names what to recheck if it
changes.

- **"Codes work in lower case too."** `parse()` upper-cases the name
  (`codes.cpp:117`). If that goes, the line goes.
- **"Past that, codes print as typed."** A ninth code prints as typed
  (`codes.h`, the comment on `kPerMessage`, and `codes.cpp:395`), and an
  effect over 40 characters is not a code at all, so it prints whole
  (`codes.cpp:155`).
- **"flashes a few times"** (BLINK). It is `fx::blink`, three reverse-video
  flashes of 250 ms, then plain text (`codes.cpp:56-57`, `249`). If ANSI
  ever gets a real SGR 5 blink, say "blinks" on the 80-column page.
- **"Colour lasts until @N@ or the end of the line."** Means the line as the
  writer typed it: colour carries across the reader's word wrap and resets at
  an explicit line break (`codes.h`, `Painter` and `endParagraph`). If it
  ever resets per displayed row instead, say "the end of the row".
- **"@DATE@ @TIME@ the moment it is read."** The board's clock at reading
  time (`codes.cpp:183-184`). The spec's "the reader's date and time" was
  avoided on purpose: it reads like the reader's own time zone.
- **"rings the bell, once a message".** Once however many are written, and
  not at all for a reader who has the bell off (`codes.cpp:285-288`).
- **"OOPS is a joke, not a delete".** When the board is short of room it
  prints nothing for an OOPS rather than the words (`codes.cpp:265-270`).
  The line is still true: the words are in the message either way.

## Sources for the history line

"Old BBSes like PCBoard had @-codes too." Checked on 2026-09-22. It names
PCBoard alone on purpose.

- PCBoard let callers use them in messages: "There are primarily three places
  in PCBoard where you can use @ macros and @X codes. They are: Display files,
  Most PCBTEXT entries ..., Inside of messages left on the BBS."
  [PCBoard wiki, Display File Conventions](https://kuehlbox.wtf/wiki/customizing:display_file_conventions)
- Synchronet has @-codes, but not from callers: "@-codes contained in the
  text of messages sent as email or posted on sub-boards will not be expanded
  to the equivalent text unless the message was sent or posted locally by
  user #1 (the sysop)."
  [Synchronet wiki, custom:atcodes](https://wiki.synchro.net/custom:atcodes)
  So Synchronet is a precedent for the syntax, not for callers writing codes,
  which is what this screen is about. The line in CLAUDE.md, "PCBoard and
  Synchronet both used @-codes", is true as written.
