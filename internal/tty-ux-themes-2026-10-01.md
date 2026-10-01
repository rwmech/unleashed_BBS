# Themes: one set of colour roles for the terminal and the display, 2026-10-01

For 1.2.2 (CLAUDE.md, "1.2.2 plan"). Rob: a CONFIG theme with about four
prebuilt themes plus "Hot Dog Stand" plus Custom, "where some global-ish
values are set with the color code", and "that same config theme could be
used for display and the TTY as well ... custom across both". Added by Rob
through the coordinator the same day: on a base ESP32 the display half of
CONFIG theme is shown greyed with a one-line upgrade note, and the same
pattern for the other S3-only settings.

Read for this: `src/core/term.*`, `src/core/screens.*`, `src/core/form.*`,
`src/core/bbs_shell.cpp` (rowBar, rowTitle, rowRule, helpUsage, statRow),
`src/core/bbs_sysop.cpp` (CONFIG), `src/plugins/chat.cpp`,
`src/plugins/panel.cpp`, `panel_gfx.h`, `skin_manifest.h`, `lights.cpp`,
`tools/mkscreens.py`, the screen style sheet
(`internal/screens-style-2026-10-01.md` on rel-1.2.1f), and the site's
`:root` palette (`unleashed_site/sitekit.py` line 1653).

How it was measured, and what was not:

- Wire bytes from the stock screens on disk (`data/screens/*.ans`, `*.seq`
  on main), parsed SGR by SGR, with the proposed resolver applied to the
  same colour changes.
- Static DRAM from the ELFs of 2026-09-29 (`.pio/build/esp32cam_aithinker`
  and `esp32dev`), symbol by symbol: where today's colour tables live, how
  big `SysConfig` is, how many copies there are.
- Contrast as WCAG ratios, ANSI on the VGA text palette (what SyncTERM
  draws), PETSCII on Pepto's PAL palette (VICE's reference), against black,
  the C64's stock blue (`#352879`) and each theme's own background.
- The host board was not run. Every testing plan needs Rob's OK, and the
  bytes the board UI sends are fixed by `Term::color` and readable from the
  source exactly, so nothing here depends on a capture.

## The verdict

This is buildable small, and most of it already exists in disguise. The
board's UI uses nine of the sixteen colours, and it uses them as roles
already: Grey is body text, DarkGrey is a note, Yellow is a key, LightRed is
a refusal, in 1,238 `Color::` references that agree with each other. A theme is
a remap of those nine plus five new roles, resolved in `Term::color` from a
table in flash, so every list, form, help page and prompt is themed in one
change without touching the call sites. Screens get role tokens
(`@KEY@`) that the player resolves per terminal, and the measured result is
fewer bytes on the wire than today's literal SGRs, not more: 1,934 against
2,671 across the stock ANSI set. The panel's seventeen colour constants
become reads of the same table. The cost is 16 bytes of static DRAM, no
bytes per session, and about 5 to 6 KB of flash on every board. Building it
also fixes a real defect: on a C64 that keeps its stock blue screen, the
board's dim text, its read-only form rows and chat's private lines are
invisible today (contrast 1.25 and 1.55 against the C64's own 2.26).

## Defects found on the way, worst first

These exist today, before any theme. Each one the theme work fixes or has
to step around.

- **PETSCII dim text vanishes on a stock C64 blue screen.** The UI sends
  DarkGrey (0x97) for every note, date, help footer, idle figure, guest
  marker and read-only form row: 90 references. On blue that is 1.25:1,
  where the C64's own light blue text is 2.26:1. Chat's private lines are
  Purple, 1.55:1. Body text is Grey (0x98), 2.31:1, which is only just the
  C64's own figure. Seven of the sixteen colours vanish on that blue:
  blue 1.00, brown 1.05, red 1.25, dark grey 1.25, purple 1.55, orange 1.63,
  black 1.73. Every theme's PETSCII column below avoids all seven for
  anything that must be read. Not every caller keeps the blue (many
  terminal programs and the C128's 80 column screen are black), so each
  PETSCII pick is also checked on black.
- **The handle is green in the lists and amber on the screens.** WHO,
  NODES, LAST and DASH draw a caller's handle in LightGreen
  (`bbs_shell.cpp` ~1352, "handle LightGreen"), and chat's default handle is
  ltgreen. The style sheet and the site say green means "up" and nothing
  else, and the screens put `@USER@` in amber. Under themes the handle is
  its own role, amber in µnleashed.
- **A title bar costs 33 bytes of SGR on ANSI and means something
  different on different terminals.** `rowBar` (`bbs_shell.cpp` 813) sends
  `color(c)` (9 bytes, `ESC[0;36;1m`), `reverse(on)` (4, `ESC[7m`),
  `color(c)` again with reverse (11, `ESC[0;36;1;7m`), and on the way out
  `color(cur_)` (9). Bold combined with reverse has no single rendering
  across terminals. The theme's title role sends one explicit bar,
  `ESC[0;30;46m` (10 bytes), and the next role closes it (7): 17 bytes, one
  meaning everywhere.
- **The `Color` enum reaches 14 of ANSI's 16 colours.** `kAnsiColor` maps
  Grey and LightGrey both to `37`, Orange and Brown both to `33`, and has no
  way to say dark cyan (`36`) or bright magenta (`1;35`). The style sheet's
  wordmark uses `1;35`, which only a literal byte in a file can send. A
  theme therefore stores the ANSI colour directly, not as a C64 name.
- **The style sheet's blue rules are near invisible.** `0;34` on black is
  1.58:1. Rules in µnleashed are dark cyan `36` (7.33:1), which the theme
  can now send.
- **CONFIG's page list is already at 24 rows on a WROOM.** Title, seven
  core pages, sats, eleven plugins, rule, footer, the prompt's blank line
  and the prompt: 24, with the typed command one row above. A `theme` page
  scrolls the title off. With the camsat plugin built in, or on any S3
  board (panel, camera), it is off already. The fix is in section 4.
- **Read-only form rows rely on colour alone.** `Form::drawField` greys a
  read-only row's label and value in DarkGrey with no other mark. That is
  invisible on a C64's blue and indistinguishable on plain ASCII, where
  `linePrompt` prints it as ordinary text. Section 9 gives it a mark that is
  not a colour.
- Two small ones. Form titles are scrambled in Yellow, the key colour, not
  the house title bar (`form.cpp` 108). The PLUGINS column header is
  LightBlue, the command colour, where column names are headings
  (`bbs_shell.cpp` 3144). Both become roles.

## 1. The roles

Fourteen roles, every one used on the terminal and on the glass. That is
the floor: each is a meaning a caller already relies on, and merging any
two loses a distinction the site or the board makes today. Fourteen also
fits the Custom page with one row to spare (a form holds 16).

| role | used for | panel use (today's token) | why it is separate |
|---|---|---|---|
| `title` | title bars over lists, forms and screens; the panel's header bar | `kBar`, and `kBand`/`kTrack` derived | a bar is a background with ink on it, not a text colour |
| `head` | headings, column names, the room banner, the board's `-->` marker | `kStruct` | structure, which must read above body text |
| `rule` | rules, frames, the dashed ASCII title fill | `kRule` | lines that should be quieter than the headings they sit under |
| `body` | most text: descriptions, help text, chat banners' words | `kInk` | the colour read for an hour |
| `dim` | notes, dates, footers, history, guests, idle times, read-only rows | `kDim`, and `kFaint` derived | the site's "faint": present, not asking to be read |
| `value` | the figure being read (SYS, MEM), what the caller types, said text | `kWhite` | the one thing on a row the eye should land on |
| `key` | a key to press: the `[W]` in HELP, `[Q]` on a screen, `Main` in the prompt | `kYellow` (the clock when synced) | the style sheet's rule: yellow is keys only |
| `action` | command words, node numbers, things to type; free lamps | `kDial`, and the free switchboard lamp | the site's "dial": things you can act on |
| `handle` | a caller's handle, `@USER@` | caller names on the glass | the site's "warm": the human |
| `ident` | the wordmark, `@BOARD@`, the board's own name | the board name on the big glass | the site's "name": identity |
| `up` | up, free, done, saved, OK | `kLive` | the site's "live": up and nothing else |
| `warn` | waiting, held, closed, slow, low heap | `kWarm` | caution that is not a failure |
| `risk` | errors, refusals, wrong passwords, shutting down | `kRisk` | the site's one alarm colour |
| `busy` | somebody wanting you: pages, rings, joins and leaves | `kBusy` | activity, which the panel already tells apart from caution |

Rank markers are not roles. They are aliases, fixed in the code, so a
theme author keeps four meanings apart rather than eight: a caller is
`body`, a guest `*` is `dim`, a co-sysop `>` is `key`, the sysop `]` is
`risk`. That is today's `markColor` and the panel's `rankColour`, unchanged
in meaning, and the switchboard's caller lamps follow it.

Three things each theme carries that are not roles, because only the glass
or one kind of terminal has them:

- **panel background** and **panel off-LED** (`kBg`, `kSurface`), RGB565;
- **panel bar ink**, the text on the header bar, RGB565;
- **ANSI screen background**, none or one of `40` to `47`. Two themes use
  it (section 3).

### How the board's own UI reaches the roles

`Term::color(Color)` becomes the theme's door. The nine colours the UI uses
are remapped through the active theme; the rest pass through as they are.

| UI colour today | becomes | call sites |
|---|---|---:|
| Grey, LightGrey | `body` | 346 |
| DarkGrey | `dim` | 90 |
| White | `value` | 135 |
| Yellow | `key` | 180 |
| LightBlue | `action` | 24 |
| LightGreen | `up` | 121 |
| LightRed | `risk` | 248 |
| Cyan | `head` | 81 |
| Purple | `ident` | 3 |
| Black, Red, Green, Blue, Orange, Brown | themselves | literal |

Counts are `grep -o "Color::X"` over `src/` on main, 2026-10-01, `term.cpp`'s own tables included. Then the sites whose meaning is
not the colour's get an explicit role, a short list rather than 1,238 edits:

- `rowBar`/`rowTitle`: `title`. `rowRule` and the form's rule: `rule`. The
  form title: `head`.
- Handles in WHO, NODES, LAST, DASH, USERS and chat: `handle`.
- Chat's notices, page and ring alerts: `busy`. The closed sign, slow and
  held words: `warn`.
- `statNum`'s figures are LightGreen today, so they would come out in `up`.
  Harmless in every theme below; worth moving to `value` in the same pass.

Two literal paths stay literal, and that is a rule, not a gap:

- **Caller codes** (`codes.cpp`): a caller who writes `@RED@` means red, in
  every theme. `@N@`, "normal", is `body`. Needs a `colorLiteral` beside
  `color`.
- **Chat's colour settings** when a sysop has picked a colour (section 8).

## 2. How screens carry roles

### The format

A role is an `@`-code naming it, inside the screen grammar the player
already scans: `@TITLE@ @HEAD@ @RULE@ @BODY@ @DIM@ @VALUE@ @KEY@ @ACTION@
@HANDLE@ @IDENT@ @UP@ @WARN@ @RISK@ @BUSY@`.

- Any case, in all three flavours, the way `@BOARD@` works today. In a
  `.seq` they are PETSCII letters, which `tokChar` already folds.
- The longest is 6 characters against the 15 `tok_` holds.
- None collides with a value token (`BBS BOARD VER NODE NODES USER TERM COLS
  DATE TIME`) or an action token (`CLS BELL DELAY BAUD SPIN`). `@@` stays
  the literal `@`.
- `@TITLE@` starts a bar and the next role ends it. A bar's text is written
  between them as today.
- In a `.asc` file a role is resolved for whatever terminal plays it: an
  ANSI or PETSCII caller who falls back to the `.asc` gets the theme's
  colours, a plain ASCII caller gets nothing.

### What goes on the wire

PETSCII: one byte, exactly as today. A role is its colour byte. `@TITLE@`
is `0x12` and the colour (two bytes, as a hand-drawn bar's opening is now);
the role after a bar sends `0x92` first.

Plain ASCII: nothing.

ANSI: the compact form below, which beats today's files because it knows
the terminal's state and a file does not. The player keeps two bytes of
state: the last role it sent and whether the terminal's attributes are
known. They are unknown at `open()`, after `resume()` (a page break or
`[More]`, where the board has printed its own prompt in its own colours),
and after any raw `ESC` byte from the file.

| situation | sends | bytes |
|---|---|---:|
| state unknown | `ESC[0;3f(;1)m`, self-contained | 7 or 9 |
| same intensity as the last role | `ESC[3fm` | 5 |
| normal to bright | `ESC[1;3fm` | 7 |
| bright to normal, or leaving a bar | `ESC[0;3fm` (`;1` if bright) | 7 or 9 |
| entering a bar | `ESC[0;3f;4bm` | 10 |
| a theme with a painted background | add `;4b` to any of the above | +3 |

After `resume()` the player re-asserts the last role before the next byte,
so a page that relies on the colour from the page before still gets it.

Measured on the stock ANSI set at main, every SGR replaced by the role that
draws the same colour:

| screen | colour bytes today | as roles | | screen | today | roles |
|---|---:|---:|---|---|---:|---:|
| about | 116 | 93 | | newsysop | 494 | 438 |
| busy | 113 | 65 | | newuser | 116 | 80 |
| chatin | 123 | 81 | | privacy | 340 | 131 |
| codes | 550 | 443 | | rules | 242 | 129 |
| files | 85 | 69 | | setup | 214 | 158 |
| goodbye | 60 | 55 | | welcome | 218 | 192 |
| **total** | **2,671** | **1,934** | | | | |

27.6% fewer colour bytes. The saving is the 7-byte `ESC[0;37m` the style
sheet puts at every row start (its bleed protection, about 15% of an ANSI
page). A row start resets nothing in ANSI, so with the state known it is
not needed; the page-break case it really protected is covered by the
re-assert. The PETSCII set is unchanged: 348 colour bytes and 171 reverse
bytes, one byte a change either way.

A one-row example from a busy screen, ANSI:

```
today    ESC[0;37m  ESC[1;33mQESC[0;37m  leave           21 colour bytes
roles    @BODY@  @KEY@Q@BODY@  leave
on wire  ESC[0;37m  ESC[1;33mQESC[0;37m  leave           21 (state unknown at open)
next row @BODY@ ...                                       0: already body
```

Flash, not wire: a `.seq` grows by 4 to 6 bytes a colour change (one byte
becomes `@KEY@`), about 1.7 KB across the stock set on a 256 KB partition.
An `.ans` shrinks. `tools/mkscreens.py` emits roles, and its byte budget
check counts wire bytes with this resolver's rules, not file bytes.

### A sysop's own screens

- Literal colour bytes play exactly as written, in every theme. A
  hand-drawn screen keeps the colours its author chose; that is correct.
- A sysop may mix: literal art, with `@BODY@` and `@KEY@` where they want
  the board's theme. A literal `ESC` makes the next role send its full form,
  so the two never fight.
- Cost of the convenience in an ANSI editor (PabloDraw, Moebius): a token
  takes columns in the editor and none on screen, the same as `@BOARD@`
  today. SCREENS.md says so (docs).

## 3. The themes

Every role's ANSI colour (SGR, on black unless the theme paints), its
PETSCII colour (C64 name; `rev` means a reverse bar), its panel RGB, and the
contrast of each. Thresholds used:

- ANSI: body, keys, headings and anything that must be read at 4.5:1 or
  better on the theme's background; dim at 2.5 or better. Three stated
  exceptions below.
- PETSCII: at least the C64's own text, 2.26:1, on the stock blue, and 3.9
  or better on black. Nine colours pass on blue: white, light green,
  yellow, cyan, light grey, green, light red, grey, light blue. Fourteen
  roles share them, so some share on a C64 in every theme. That is the
  hardware, not a choice.
- Panel: on the theme's own panel background.

### µnleashed (the default)

The site's palette, from `sitekit.py` `:root`, and today's panel tokens
unchanged. On the terminal it is today's board, with the style sheet's
decisions: handles amber, rules dark cyan, the name in the site's purple.

| role | ANSI | cr | PETSCII | blue | black | panel | cr |
|---|---|---:|---|---:|---:|---|---:|
| title | bar `46`, ink `30` | 7.33 | rev cyan | 4.41 | 7.64 | `#182C78`, ink `#C8C8C8` | 7.51 |
| head | `1;36` | 17.13 | cyan | 4.41 | 7.64 | `#4CE0E0` | 13.05 |
| rule | `36` | 7.33 | ltblue | 2.26 | 3.91 | `#2C2C38` | 1.52 |
| body | `37` | 9.04 | ltgrey | 4.05 | 7.01 | `#C8C8C8` | 12.55 |
| dim | `1;30` | 2.82 | grey | 2.31 | 4.00 | `#8A8A8A` | 6.08 |
| value | `1;37` | 21.00 | white | 12.13 | 21.00 | `#FFFFFF` | 21.00 |
| key | `1;33` | 19.69 | yellow | 6.61 | 11.44 | `#FFD35C` | 14.72 |
| action | `1;34` | 4.13 | ltblue | 2.26 | 3.91 | `#7FD4FF` | 12.76 |
| handle | `33` | 4.01 | yellow | 6.61 | 11.44 | `#E0A94E` | 9.95 |
| ident | `1;35` | 8.00 | ltblue | 2.26 | 3.91 | `#B48EF0` | 8.07 |
| up | `1;32` | 15.82 | ltgreen | 6.89 | 11.93 | `#5DDC7A` | 11.98 |
| warn | `33` | 4.01 | yellow | 6.61 | 11.44 | `#E0A94E` | 9.95 |
| risk | `1;31` | 6.68 | ltred | 2.58 | 4.46 | `#E06C6C` | 6.53 |
| busy | `33` | 4.01 | yellow | 6.61 | 11.44 | `#EF8B5A` | 8.51 |

Panel background `#000000`, off LED `#14141B`. The panel rule at 1.52 is
deliberate, a hairline. `action` at 4.13 and the three ambers at 4.01 are
the closest the 16 colours have: VGA's `33` is the only amber, so the
site's warm, busy and the handle are one colour on ANSI and three on the
glass. Changes from today's look a caller will notice: handles amber,
chat's node numbers light blue (the `action` of every other list), notices
amber, rules dark cyan, PETSCII body light grey.

### Amber terminal

A monochrome amber CRT. On ANSI, VGA's `33` (`#AA5500`) is the amber and
`1;33` the bright. Risk keeps red: a wrong password or a board shutting
down must not look like a heading.

| role | ANSI | cr | PETSCII | blue | black | panel | cr |
|---|---|---:|---|---:|---:|---|---:|
| title | bar `43`, ink `30` | 4.01 | rev yellow | 6.61 | 11.44 | `#5A3C00`, ink `#FFB000` | 5.51 |
| head | `1;33` | 19.69 | white | 12.13 | 21.00 | `#FFC833` | 13.00 |
| rule | `33` | 4.01 | yellow | 6.61 | 11.44 | `#3A2800` | 1.42 |
| body | `33` | 4.01 | yellow | 6.61 | 11.44 | `#FFB000` | 10.99 |
| dim | `1;30` | 2.82 | grey | 2.31 | 4.00 | `#9A6A00` | 4.25 |
| value | `1;33` | 19.69 | white | 12.13 | 21.00 | `#FFD480` | 14.36 |
| key | `1;33` | 19.69 | white | 12.13 | 21.00 | `#FFE6A8` | 16.42 |
| action | `33` | 4.01 | yellow | 6.61 | 11.44 | `#FFB000` | 10.99 |
| handle | `1;33` | 19.69 | white | 12.13 | 21.00 | `#FFC040` | 12.33 |
| ident | `1;33` | 19.69 | white | 12.13 | 21.00 | `#FFD060` | 13.84 |
| up | `1;33` | 19.69 | white | 12.13 | 21.00 | `#FFD060` | 13.84 |
| warn | `33` | 4.01 | yellow | 6.61 | 11.44 | `#FF9A00` | 9.46 |
| risk | `1;31` | 6.68 | ltred | 2.58 | 4.46 | `#FF5A1A` | 6.45 |
| busy | `1;33` | 19.69 | white | 12.13 | 21.00 | `#FF8C00` | 8.63 |

Panel background `#0A0700`, off LED `#1C1400`. Exception, stated: ANSI
body is 4.01, under 4.5, because `33` is the only amber there is and a
monochrome theme in bright yellow is not amber. A C64 has no amber at all;
on PETSCII this theme is yellow and white, and says so in its preview.

### Green phosphor

P1 green. The C64 is better at this than at amber: green and light green
give a real two-level phosphor on its blue (3.07 and 6.89).

| role | ANSI | cr | PETSCII | blue | black | panel | cr |
|---|---|---:|---|---:|---:|---|---:|
| title | bar `42`, ink `30` | 6.75 | rev ltgreen | 6.89 | 11.93 | `#0F4A1A`, ink `#33DD55` | 5.76 |
| head | `1;32` | 15.82 | ltgreen | 6.89 | 11.93 | `#66FF88` | 16.22 |
| rule | `32` | 6.75 | green | 3.07 | 5.31 | `#0E3A16` | 1.64 |
| body | `32` | 6.75 | green | 3.07 | 5.31 | `#33DD55` | 11.61 |
| dim | `1;30` | 2.82 | grey | 2.31 | 4.00 | `#1E8A38` | 4.75 |
| value | `1;32` | 15.82 | ltgreen | 6.89 | 11.93 | `#AAFFBB` | 17.73 |
| key | `1;32` | 15.82 | white | 12.13 | 21.00 | `#DDFFDD` | 19.42 |
| action | `32` | 6.75 | green | 3.07 | 5.31 | `#33DD55` | 11.61 |
| handle | `1;32` | 15.82 | ltgreen | 6.89 | 11.93 | `#88FF99` | 16.81 |
| ident | `1;32` | 15.82 | ltgreen | 6.89 | 11.93 | `#AAFFBB` | 17.73 |
| up | `1;32` | 15.82 | ltgreen | 6.89 | 11.93 | `#66FF88` | 16.22 |
| warn | `1;32` | 15.82 | ltgreen | 6.89 | 11.93 | `#CCFF66` | 18.06 |
| risk | `1;31` | 6.68 | ltred | 2.58 | 4.46 | `#FF5040` | 6.47 |
| busy | `1;32` | 15.82 | ltgreen | 6.89 | 11.93 | `#CCFF99` | 18.33 |

Panel background `#000000`, off LED `#0A140C`. Keys on PETSCII are white:
the C64 has no third green, and a key has to stand out from a heading.

### C64 blue

Built for the stock blue screen: light blue text, the C64's own, and
nothing from the seven colours that vanish there. On ANSI it paints the
screen blue (`44`), so a PC terminal looks like a C64 too.

| role | ANSI (on `44`) | cr | PETSCII | blue | black | panel (on `#352879`) | cr |
|---|---|---:|---|---:|---:|---|---:|
| title | bar `46`, ink `34` | 4.64 | rev ltblue | 2.26 | 3.91 | `#6C5EB5`, ink `#FFFFFF` | 5.38 |
| head | `1;36` | 10.84 | cyan | 4.41 | 7.64 | `#88D8E8` | 7.53 |
| rule | `1;34` | 2.61 | ltblue | 2.26 | 3.91 | `#6C5EB5` | 2.26 |
| body | `1;34` | 2.61 | ltblue | 2.26 | 3.91 | `#A69CE6` | 4.92 |
| dim | `1;34` | 2.61 | grey | 2.31 | 4.00 | `#8A8A8A` | 3.51 |
| value | `1;37` | 13.29 | white | 12.13 | 21.00 | `#FFFFFF` | 12.13 |
| key | `1;33` | 12.46 | yellow | 6.61 | 11.44 | `#EDF171` | 10.06 |
| action | `1;36` | 10.84 | cyan | 4.41 | 7.64 | `#88D8E8` | 7.53 |
| handle | `37` | 5.72 | ltgrey | 4.05 | 7.01 | `#C8C8C8` | 7.25 |
| ident | `1;37` | 13.29 | white | 12.13 | 21.00 | `#FFFFFF` | 12.13 |
| up | `1;32` | 10.01 | ltgreen | 6.89 | 11.93 | `#A9FF9F` | 10.11 |
| warn | `1;33` | 12.46 | yellow | 6.61 | 11.44 | `#EDF171` | 10.06 |
| risk | `1;31` | 4.23 | ltred | 2.58 | 4.46 | `#E08C8C` | 4.79 |
| busy | `1;35` | 5.06 | yellow | 6.61 | 11.44 | `#F0A0F0` | 6.35 |

Panel background `#352879`, off LED `#2A2060`. Exception, stated: body is
2.61 on ANSI and 2.26 on PETSCII, because that is what a C64 looks like and
the theme exists to look like one. On blue nothing readable is quieter than
light blue, so `dim` equals `body` on ANSI and is grey (same strength, a
different hue) on PETSCII. The glass lifts body to `#A69CE6` (4.92):
small type on a small panel read from across a room is not a C64 screen.

### Hot Dog Stand

Windows 3.1's scheme: ketchup-red title bars with white text, mustard
yellow everywhere you read, hard black edges. On ANSI it paints the screen
red (`41`), because a hot dog stand on black is not one. ANSI has no yellow
background (`43` is brown without iCE colours), so the terminal is red with
yellow text and the glass is the real thing: yellow window, red bar.

| role | ANSI (on `41`) | cr | PETSCII | blue | black | panel (on `#FFFF00`) | cr |
|---|---|---:|---|---:|---:|---|---:|
| title | bar `40`, ink `1;37` | 21.00 | rev ltred | 2.58 | 4.46 | `#FF0000`, ink `#FFFFFF` | 4.00 |
| head | `1;37` | 7.75 | white | 12.13 | 21.00 | `#FF0000` | 3.72 |
| rule | `30` | 2.71 | yellow | 6.61 | 11.44 | `#000000` | 19.56 |
| body | `1;33` | 7.27 | yellow | 6.61 | 11.44 | `#000000` | 19.56 |
| dim | `37` | 3.34 | grey | 2.31 | 4.00 | `#808000` | 3.91 |
| value | `1;37` | 7.75 | white | 12.13 | 21.00 | `#800000` | 10.20 |
| key | `1;37` | 7.75 | white | 12.13 | 21.00 | `#0000FF` | 8.00 |
| action | `1;37` | 7.75 | white | 12.13 | 21.00 | `#0000FF` | 8.00 |
| handle | `1;37` | 7.75 | white | 12.13 | 21.00 | `#800000` | 10.20 |
| ident | `1;37` | 7.75 | white | 12.13 | 21.00 | `#FF0000` | 3.72 |
| up | `1;32` | 5.84 | ltgreen | 6.89 | 11.93 | `#008000` | 4.78 |
| warn | `1;36` | 6.32 | ltred | 2.58 | 4.46 | `#800000` | 10.20 |
| risk | bar `40`, ink `1;33` | 19.69 | ltred | 2.58 | 4.46 | `#FF0000` | 3.72 |
| busy | `1;36` | 6.32 | yellow | 6.61 | 11.44 | `#0000FF` | 8.00 |

Panel background `#FFFF00`, off LED `#808000`, all from the Windows 16.
Red on yellow is 3.72 and white on red is 4.00, which is the scheme as
shipped: faithfully garish, as asked. Red cannot carry risk on a red
screen, so risk on ANSI is a black box with yellow text, the loudest thing
the theme can do. PETSCII cannot paint a background at all; there it is
yellow, white and light red on the caller's own screen.

### Painting the ANSI background (C64 blue, Hot Dog Stand)

Recommended for these two and no others. What it costs, so the call is
informed:

- +3 bytes on every colour change: +906 bytes across the stock ANSI set,
  5.7% of its 15,864.
- `Term::cls` sends the background before `ESC[2J`, `Term::reset` sends
  `body` with the background instead of a bare `ESC[0m`, and `eolClear`
  paints in it.
- A terminal without background-colour erase shows lines scrolled in at
  the bottom in its own background, so the screen can be striped. SyncTERM
  and PuTTY erase in the current background.
- A sysop's hand-drawn screen whose literal SGRs start with `0` punches
  black holes in a painted screen. The theme's preview says so, and a board
  with its own art should pick another theme.

### Custom

One pick per role from the 16 C64 names CONFIG already uses for chat's
colours (`black|white|red|cyan|...|ltgrey`), so a sysop learns one list.

- PETSCII: the pick. ANSI: the pick through `kAnsiColor`, the same map a
  chat colour uses today. Panel: the pick through one 16-entry table, chosen
  so a pick looks like itself on the glass and lands on the µnleashed token
  where the meaning matches: black `#000000`, white `#FFFFFF`, red
  `#C83C3C`, cyan `#4CE0E0`, purple `#B48EF0`, green `#3CB050`, blue
  `#2850C8`, yellow `#FFD35C`, orange `#EF8B5A`, brown `#A0702C`, ltred
  `#E06C6C`, darkgrey `#5A5A62`, grey `#8A8A8A`, ltgreen `#5DDC7A`, ltblue
  `#7FD4FF`, ltgrey `#C8C8C8`.
- The title pick is the bar. On ANSI it becomes the nearest background
  (`40` to `47`) with `30` ink on a light bar and `1;37` on a dark one
  (`40`, `41`, `44`, `45`); every pairing is 4.0:1 or better. On the glass
  the bar ink is `body` if that reads at 4.5:1, else white or black,
  whichever is higher.
- One pick cannot be right on both terminals for every role. `dim` is the
  plain case: `darkgrey` is right on ANSI and vanishes on a C64's blue;
  `grey` reads on blue and is the same `37` as body on ANSI. The page says
  which rows have that problem (section 4). A sysop who wants both right
  picks a prebuilt theme.
- No painted background, panel background black. Exact panel colours come
  later (section 4).

## 4. CONFIG theme

A core page, `CONFIG theme`, listed second, after `board`. Board-wide
(section 5). Keys at the top of `system.cfg`:

```
theme = unleashed        ; unleashed, amber, green, c64, hotdog or custom
theme_title  = cyan      ; theme_<role>: read only when theme = custom
theme_body   = ltgrey
...
```

The cycle shows the names a person reads (µnleashed, Amber terminal, Green
phosphor, C64 blue, Hot Dog Stand, Custom) and writes the short words.
`unleashed` stays ASCII in the file, as identifiers do.

### The page at 40 columns (ANSI and PETSCII)

The form's own geometry: label column 2, 9 wide; box column 12, 27 wide.
Four rows, so the page is short and nothing moves when it grows.

```
             1         2         3         4
    1234567890123456789012345678901234567890
  1 THEME
  2 ---------------------------------------
  3
  4  Theme     µnleashed..................
  5  Colours   [ 14 colours, for Custom  ]
  6  Preview   [ see it before saving    ]
  7  Display   (ESP32-S3 with a screen)
  8
  9            [ Save ]  [ Cancel ]
 10
 11 Space picks a theme. F1 saves.
```

### The page at 80 columns (ANSI, PETSCII 80, and 132 draws the same)

Label 20 wide, box column 23, 56 wide.

```
             1         2         3         4         5         6         7         8
    12345678901234567890123456789012345678901234567890123456789012345678901234567890
  1 THEME
  2 -------------------------------------------------------------------------------
  3
  4  Theme                µnleashed...............................................
  5  Custom colours       [ Title, Heading, Body and 11 more, for Custom         ]
  6  Preview              [ Every colour on one screen, before you save          ]
  7  Display colours      (ESP32-S3 boards with a screen)
  8
  9                       [ Save ]  [ Cancel ]
 10
 11 Space steps through the themes. F1 saves, and every caller has it at once.
```

On a board with a display, row 7 reads `Follow the theme`, read-only and
without the parentheses: it is information, not something missing.

Plain ASCII asks a line at a time, as every form does there: the cycle's
numbered choices, then `Colours [14 colours] open (y/N)?`, then `Preview
open (y/N)?`, then the read-only `Display: (ESP32-S3 boards with a screen)`,
then `Save (Y/n)?`. Nothing here is coloured on ASCII, and nothing needs to
be: the theme has nothing to show that terminal.

### Custom colours, at 40

Opened from the button. Fifteen rows: where to start, then the fourteen
roles. Each value is drawn in its own colour, so the page is its own
swatch; on the title row `[ cyan ]` marks a bar drawn in the pick, eight
columns wide. Rows 4 to 18, buttons 20, status 22: inside 24 rows on ANSI
and 25 on a C64.

```
             1         2         3         4
    1234567890123456789012345678901234567890
  1 CUSTOM COLOURS
  2 ---------------------------------------
  3
  4  Start     from µnleashed.............
  5  Title     [ cyan ]...................
  6  Heading   cyan.......................
  7  Rule      cyan.......................
  8  Body      ltgrey.....................
  9  Dim       grey.......................
 10  Value     white......................
 11  Key       yellow.....................
 12  Action    ltblue.....................
 13  Handle    yellow.....................
 14  Name      ltblue.....................
 15  Up        ltgreen....................
 16  Warn      yellow.....................
 17  Risk      ltred......................
 18  Busy      yellow.....................
 19
 20            [ Save ]  [ Cancel ]
 21
 22 Most of the text on every screen.
```

- `Start` is a cycle of the five prebuilt themes. Picking one refills the
  fourteen rows: each role gets the C64 name whose ANSI colour is that
  theme's ANSI pick, the nearest where none is (`36` becomes cyan, `1;35`
  purple). Fill from the ANSI column: it is the one that uses all sixteen
  colours, so the fill loses least.
- The status line is the role's meaning (38 characters), and when the pick
  is one of the seven that vanish on a C64's blue, it says `Hard to read on
  a C64's blue screen.` instead. Not refused: the sysop may know their
  callers.
- Saving this page writes the fourteen `theme_<role>` keys and
  `theme = custom`.

### Custom colours, at 80

The box has room, so the warning sits on the row itself and the status
line keeps the meaning.

```
             1         2         3         4         5         6         7         8
    12345678901234567890123456789012345678901234567890123456789012345678901234567890
  1 CUSTOM COLOURS
  2 -------------------------------------------------------------------------------
  3
  4  Start from theme     µnleashed...............................................
  5  Title bars           [ cyan ]
  6  Headings, columns    cyan
  7  Rules and frames     cyan
  8  Body text            ltgrey
  9  Notes and dates      darkgrey  hard to read on a C64's blue screen
 10  Figures, typing      white
 11  Keys to press        yellow
 12  Commands, nodes      ltblue
 13  Handles              yellow
 14  Board name           purple    hard to read on a C64's blue screen
 15  Up, saved, done      ltgreen
 16  Waiting, closed      yellow
 17  Errors, refusals     ltred
 18  Pages, joins, rings  yellow
 19
 20                       [ Save ]  [ Cancel ]
 21
 22 Notes, dates, the help footer, guests: quieter than body text.
```

The warning is 10 columns after the pick (box column 33), in `dim`.

### Preview

The button clears the screen and draws one card with the theme chosen on
the form, saved or not, for this caller only. Nobody else sees an unsaved
theme. Any key returns to the form; `W` plays the welcome screen in it
first. Drawn with a theme passed down for this one draw, not by swapping the
board's, so it costs no static RAM.

At 40 (the first row is a 39-column bar in `title`):

```
             1         2         3         4
    1234567890123456789012345678901234567890
  1  AMBER TERMINAL                preview
  2
  3 Headings and column names
  4 Body text is most of what you read.
  5 Notes and dates are quieter: 14:02
  6 A figure, 1,024. What you type: hi
  7 [W]HO          who is on now
  8 Daytona        The Rusty Antenna
  9 up  waiting  refused  ringing
 10 #2:Daytona) anyone on tonight?
 11 *** Rusty joined
 12 ---------------------------------------
 13 Not saved. W plays the welcome in it,
 14 any other key goes back.
```

At 80 the role names sit in a `dim` column at 56, so a sysop tuning Custom
can see which row is which. Row 13 ends at column 78.

```
             1         2         3         4         5         6         7         8
    12345678901234567890123456789012345678901234567890123456789012345678901234567890
  1  AMBER TERMINAL                                                        preview
  2
  3 Headings and column names                             head
  4 Body text is most of what you read.                   body
  5 Notes and dates are quieter: 14:02                    dim
  6 A figure, 1,024. What you type: hello                 value
  7 [W]HO          who is on now                          key, action
  8 Daytona        The Rusty Antenna                      handle, name
  9 up    waiting    refused    ringing                   up warn risk busy
 10 #2:Daytona) anyone on tonight?                        chat: action dim handle
 11 *** Rusty joined                                      chat: busy
 12 -------------------------------------------------------------------------------
 13 Not saved. W plays the welcome in these colours; any other key goes back.
```

On plain ASCII the preview says `Themes colour ANSI and PETSCII terminals;
this one shows none.` and returns.

### Live, not at the next restart

Save makes it live at once: every colour byte any session sends after the
save uses the new table, the panel redraws whole within its normal band
schedule (the path silent mode's end already uses), and the lights take it
on their next 20 ms frame. Callers' screens are not redrawn for them. A
caller part way through a screen gets the rest of it in the new colours,
once; latching a theme per screen would cost a byte a session to prevent a
mixed screen nobody will see twice.

The panel and the lights notice a theme change by comparing a generation
counter each tick, because a core page save restarts no plugin (since
1.1.2 only plugins whose section moved restart).

### Exact panel colours, later

Yes, and it fits. A `Display colours` sub-page on boards with a display:
the fourteen roles plus the panel background as `#RRGGBB` text rows, 15 of
the form's 16, keys `panel_<role>`. 32 bytes of RAM in `SysConfig` behind
`BBS_HAS_LCD`, none on a base board. Not in 1.2.2: Custom from the 16 names
gives a usable glass, and the sub-page waits until somebody wants a colour
the 16 cannot give.

### The CONFIG page list, which this page does not fit

Two rules, both cheap:

- **Columns by width:** `cols / 40` columns, at most 3. One at 40, two at
  80, three at 132. Reading order does not matter in a list of pages, so
  it reflows: core pages and sats down the left, plugins down the right.
  Each cell is 40 columns: a space, the name in 10, the description cut at
  a word to 28. The WROOM's 24 rows become 16.
- **At 40, the closing rule goes and the footer stays,** which pays for the
  `theme` row. Where the list is still taller than the screen (camsat
  built in, an S3 with panel and camera), it pages through the list
  machinery's `[More]` like every other list.

```
             1         2         3         4         5         6         7         8
    12345678901234567890123456789012345678901234567890123456789012345678901234567890
  1  Settings                                                           CONFIG page
  2  board     name, clock, LED, silent      sd        plugin: SD card
  3  theme     colours, screens and display  files     plugin: file areas
  4  limits    minutes per call and per day  forums    plugin: forums
  5  accounts  sign-ups and guest calls      info      plugin: information pages
  6  backup    port, how long it stays open  example   plugin: example
  7  staff     sysop and co-sysop passwords  chat      plugin: chat room
  8  network   Wi-Fi and port, next restart  serial    plugin: serial bridge
  9  photos    SNAPSHOT's default camera     announce  plugin: directory listing
 10  sats      the sats; CONFIG sat <name>   lights    plugin: lights
 11                                          doors     plugin: doors
 12                                          link      plugin: the link
 13                                          panel     (ESP32-S3 with a screen)
 14  F1 saves a page, left arrow leaves it
 15
 16 [1] Main:
```

Plugin descriptions above are illustrative; the real ones are each
plugin's `title`. Row 13 is the base board's greyed `panel` (appendix).

## 5. Board-wide, not per caller

Recommendation: board-wide only.

- **What a theme is here.** It is the board's look, the sysop's choice, the
  way a 1992 board's colours were its sysop's. The screens, the panel on
  the desk and the lights are all one look, and the panel cannot follow
  ten callers at once.
- **What a per-caller pick would break.** A sysop's hand-drawn screens keep
  their literal colours. A caller in Amber would get amber lists beside the
  sysop's full-colour art, which is the incoherent screen this work exists
  to remove.
- **Readability is not the argument for it.** Each prebuilt theme's PETSCII
  column already reads on a C64's blue and on black, so no caller needs
  their own theme to read the board.
- **What per-caller would cost, if ever wanted:** one byte in each session's
  `Term` (12 bytes static), a `theme` field in `users.txt`, a PROFILE row,
  about 1 KB of flash, and a rule for what the caller's pick does to the
  sysop's literal screens (nothing, which is the problem above). Nothing in
  this design prevents adding it later: `Term::color` would read the
  session's index instead of the board's.

## 6. The cost

Static DRAM, from the 2026-09-29 ELFs:

- `SysConfig` is 488 bytes and there are two of them, `g_cfg` and
  `g_scratch` (both `.dram0.data`). The theme adds one byte for the theme
  and seven for fourteen 4-bit Custom picks: 8 bytes each, **16 bytes in
  all**. Stored as whole bytes it would be 32 (488 + 15 rounds to 504).
  Pack the nibbles.
- Prebuilt themes, the Custom glass table and the role names are `const`:
  `.flash.rodata`, like `kAnsiColor` (32 bytes at `0x3f430af0`) and
  `kColorNames` (200 bytes) today. No DRAM.
- `ScreenPlayer`'s two bytes of resolver state fit its existing tail
  padding: its members end at offset 146 and it is aligned to 4, so it is
  148 either way. Assert `sizeof(ScreenPlayer) == 148` when it is built.
  **Per session: 0 bytes.**
- Chat's eleven colour bytes (`g_cNode` and the rest, 1 byte each in
  `.dram0.data`) take a "follow the theme" value in the same byte: 0.
- The lights read the theme's RGB from flash each frame: 0.
- The panel's live colour table, with the derived band, track and faint
  computed once a change: 40 bytes, S3 builds only (85 KB free there).
- ESP32-CAM: 2,640 free today becomes 2,624. WROOM: 15,088 free on the
  2026-09-29 ELF becomes 15,072.

Flash, estimated from the parts (measured by `optimize` once built):

- Theme tables: five themes of 14 roles at 4 bytes (ANSI, PETSCII, RGB565)
  plus 8 bytes of per-theme fields: about 320 bytes. The Custom glass table
  32, the Custom bar table 16.
- Strings: fourteen role labels at 9 and 20, notes at 38 and 78, the theme
  names: about 2 KB.
- Code: the remap in `Term::color` and the literal path, the resolver in
  the player, the `theme` keys, the CONFIG page, Custom and Preview: about
  3 KB.
- Total about 5 to 6 KB on every board; about 1 KB more on display boards
  for the panel's table reads. Nothing in IRAM, which matters on the
  ESP32-CAM (6,764 bytes of IRAM left).

On the wire: 27.6% fewer ANSI colour bytes on the screens, 16 fewer bytes
per title bar on ANSI, one byte fewer per bar on PETSCII, unchanged
elsewhere. +5.7% on the two painted themes.

## 7. Interplay

**Skins** (S3 display boards):

- Skin art is never recoloured. A JPEG background is the skin author's
  picture.
- A widget with `colour=#RRGGBB` in its manifest keeps it: the author chose
  it to read on their art.
- A widget with no colour takes its role from the theme instead of today's
  hard defaults (`fg = {0xC8,0xC8,0xC8}`, the panel's ink, and
  `{0x5D,0xDC,0x7A}`, its live green, in `skin_manifest.h`). The built-in
  `status` layout is fully themed.
- A manifest may name a role instead of a hex value (`colour=up`): a few
  lines in `colour()`, S3 only, and it lets one skin follow every theme.

**Silent mode:** nothing to do. The lights and the backlight are off
whatever the theme; a theme saved during silent is stored and the glass
redraws in it when silent ends, on the path that already repaints
everything then.

**Lights:**

- The two effects that show callers take the theme: `switchboard` and
  `nodes`. A free line is `action` (µnleashed `#7FD4FF`, today's `kDialRgb`
  exactly), a caller's line is their rank's alias (`body`, `dim`, `key`,
  `risk`).
- On a base board with no display the strip uses the same RGB from the
  theme table, so a WROOM's switchboard and an S3's agree.
- The decorative effects keep their own colours: `c64` stripes, `rainbow`,
  `boing`, `scanner`, `hayes`, `blinken`, `vu`, `manual`. So does the drive
  light (amber card, cool white flash, red error): it is an instrument, and
  its colours mean the medium, not the theme.
- The panel's row of LEDs mirrors the strip's frame, as now, so it follows
  automatically.

## 8. Migration

**A board with none of it set** (every board on 1.2.1): no `theme` line
means µnleashed. No chat colour lines means chat follows the theme.

**Chat's colour settings** (`[plugin:chat]`, eleven keys):

- Each cycle gains a first choice, `theme`, which is the default and means
  "follow the role below".

| key | follows | today's default |
|---|---|---|
| `color_node` | `action` | cyan |
| `color_punct` | `dim` | darkgrey |
| `color_handle` | `handle` | ltgreen |
| `color_text` | `value` | white |
| `color_old` | `dim` | darkgrey |
| `color_notice` | `busy` | yellow |
| `color_room` | `head` | cyan |
| `color_marker` | `head` | cyan |
| `color_private` | `ident` | purple |
| `color_pmark` | `risk` | ltred |
| `color_action` | `handle` | yellow |

- **A line whose value is today's default is read as `theme`.** Boards
  whose `system.cfg` came from the example carry eight of these lines, all
  defaults; without this rule none of them would ever follow a theme. The
  cost: a sysop who deliberately chose the default colour loses it to the
  theme. That cannot be told apart from the file, and the CHANGELOG says so.
- Any other value is the sysop's choice and stays literal in every theme.
- `system.cfg.example` drops the eight lines.
- Older firmware reading a 1.2.2 file: `theme` and `theme_<role>` are
  unknown top-level keys, which `syscfg` logs and ignores
  (`sysconfig.cpp` 573); a chat value of `theme` is not a colour name, so
  `colorByName` keeps the default. A 1.2.2 backup restores on 1.2.1.

**Stock screens** are redrawn with roles in 1.2.2 anyway (the style sheet's
step 2), so they are drawn once, with roles from the start. Screens a sysop
put on the card or installed keep their literal colours.

## 9. Greyed rows for what a base board cannot do

Rob, through the coordinator: on a base ESP32 the display half of CONFIG
theme is shown, greyed, with a one-line upgrade note; sysop-only, never
nagging; the panel code stays out of the base image.

### What "greyed" is on each terminal

PETSCII does have a dim colour, dark grey, but on a C64's stock blue it is
1.25:1 and vanishes, and on blue the C64's grey (2.31) is the same strength
as the light blue an editable label is drawn in (2.26). So colour cannot be
the signal on PETSCII, and plain ASCII has none. The signal is
**parentheses**, on every terminal: a value in parentheses means "not on
this board". Colour adds to it where it can.

| terminal | label | value | marker |
|---|---|---|---|
| ANSI | `dim` (`1;30`, 2.82, against 4.13 for an editable label) | `dim` | `( )` |
| PETSCII | `dim` (grey, readable on blue and black) | `dim` | `( )`, which is what shows it |
| plain ASCII | the line-mode `Label: (value)` | | `( )` |

The row never takes the focus (`FF_READONLY` already skips it), so the
cursor never stops on something that cannot be changed. Its words are its
value, not a status-line note, because a read-only row is never focused
and so never shows a note. That also makes it one line and nothing more.

### At 40, ANSI and PETSCII

```
             1         2         3         4
    1234567890123456789012345678901234567890
  7  Display   (ESP32-S3 with a screen)
```

Label columns 2 to 10, value from column 12, 24 characters, inside the
27-column box.

### At 80

```
             1         2         3         4         5         6         7         8
    12345678901234567890123456789012345678901234567890123456789012345678901234567890
  7  Display colours      (ESP32-S3 boards with a screen)
```

### Plain ASCII

```
Display: (ESP32-S3 boards with a screen)
```

### The rules that keep it from nagging

- Only in CONFIG, which is sysop-only already. Never at login, never in
  HELP, never in a caller's screen, never on the status line.
- One row, last on its page, so no row a guide or test counts to moves.
- Words, not a pitch: it says what has the feature, not "upgrade". The
  site's `/hardware` page is where the argument is made; the board only
  names the part.
- A base board's preview card (section 4) adds one dim line under the rule:
  `An ESP32-S3 board with a screen shows these too.` That is the one place a
  sysop who just chose a theme reads it, once per preview.

### Cost

A new CONFIG row kind for a fixed read-only value, compiled only when the
feature is not: a table entry and its strings, about 60 bytes of flash a
row and no RAM. No panel code in the base image.

## What stays as it is

- **Sixteen colours.** No 256-colour or true-colour mode: half the
  terminals that call here cannot show it, and a theme that only works on
  some callers is not a theme.
- **The `Color` enum and `colorByName`.** They stay the vocabulary for
  literal colours: caller codes, chat picks, Custom's picks.
- **Literal colour in screen files.** Played as written, in every theme.
- **Caller codes are literal.** A caller's `@RED@` is red on every board in
  every theme; their words are theirs.
- **The PETSCII 40 form geometry.** Labels 9, box 27, status 38. The theme
  changes colours only.
- **Plain ASCII.** Unchanged: no colour, the dashed title, the line-mode
  forms. Nothing in this design adds a byte there.
- **The rank marker characters** `* > ]` and their four meanings.
- **The drive light's colours and the decorative strip effects**, for the
  reasons in section 7.
- **The panel's layout and the skins' art.** Colour only.

## Implementation order

Cheapest and most visible first. Each step ships alone.

- **The table and the remap.** Theme tables in flash, `theme` key, the
  nine-colour remap in `Term::color`, `colorLiteral` for caller codes and
  chat picks, CONFIG theme with the cycle (prebuilt only). Every list, form,
  help page and prompt is themed in one change, and the PETSCII dim text
  becomes readable on a C64's blue.
- **The explicit roles.** `rowBar` as one bar SGR (16 bytes saved a bar),
  `rule`, `head` for form titles, `handle` at the list and chat sites,
  `busy` and `warn` at theirs, read-only rows in `dim` with parentheses.
  Preview.
- **Chat's `theme` value** and the default-means-theme migration.
- **Role tokens in the player** with the compact resolver, and
  `mkscreens.py` writing roles, with its budget check counting by the same
  rules. Pair it with the screen redraw so every screen is drawn once.
- **The glass and the strip.** Panel tokens become reads of the table (S3),
  the switchboard and nodes effects, skin role names.
- **Custom colours.** The page, `Start from`, the warnings.
- **Greyed rows and the page list.** The base board's `Display` row, the
  network page's SSH row, the two-column CONFIG list and the dropped rule
  at 40.
- **Painted backgrounds** for C64 blue and Hot Dog Stand, last, because
  they are the part that depends on how each terminal erases.

Decisions that are Rob's, each with the recommendation already made above:

- µnleashed changes handles to amber and chat's node numbers to light blue
  (recommended: yes, so one thing is one colour everywhere).
- Painted ANSI backgrounds for C64 blue and Hot Dog Stand (recommended:
  yes, those two only).
- Amber's ANSI body at 4.01 and C64 blue's at 2.61, under the 4.5 the
  others meet (recommended: accept; it is what those screens looked like).

## Appendix: the same pattern for other S3-only settings

Where a base board's CONFIG can show an S3 feature as a greyed row, it
should, under the rules in section 9. Three places, judged:

- **SSH in CONFIG network: yes.** It is the S3's headline feature
  (encrypted calls) and the page has room. Last row, after CGNAT, where
  `ssh_port` sits on an S3, so nothing moves.

```
             1         2         3         4
    1234567890123456789012345678901234567890
  4  Network   HomeNet....................
  5  Password  ********...................
  6  Port      6400.......................
  7  CGNAT     no.........................
  8  SSH port  (ESP32-S3 boards)
```

```
             1         2         3         4         5         6         7         8
    12345678901234567890123456789012345678901234567890123456789012345678901234567890
  4  Wi-Fi network        HomeNet.................................................
  5  Wi-Fi password       ********................................................
  6  Telnet port          6400....................................................
  7  CGNAT/Tailscale LAN  no......................................................
  8  SSH port (SyncTERM)  (encrypted calls need an ESP32-S3 board)
```

- **The panel page in the CONFIG list: at 80 and 132, not at 40.** In the
  two-column list it fills an empty cell (row 13 of the mock-up in section
  4). At 40 the list has no row to spare, and a greyed row that scrolls the
  title off is the bad layout this work is fixing. `CONFIG panel` typed on
  a base board answers one line: `--> The display needs an ESP32-S3 board
  with a screen.`
- **Ethernet in CONFIG network: no.** It is one board's hardware (the
  ESP32-S3-ETH), not the S3's, and two greyed rows on one page start to
  read as a sales sheet.

The general rule for the next one: show it greyed when it is a capability
of the S3 family that a sysop would want on the page they are already on,
and when it costs no row a screen needs. Otherwise leave it out.

## Sources

- Windows 3.1 Hot Dog Stand, red and yellow, designer's account:
  [PC Gamer](https://www.pcgamer.com/software/windows/windows-3-1-included-a-red-and-yellow-hot-dog-stand-color-scheme-so-garish-it-was-long-assumed-to-be-a-joke-so-i-tracked-down-the-original-designer-to-get-the-true-story/),
  [OSnews](https://www.osnews.com/story/144009/windows-3-1s-infamous-hot-dog-stand-colour-scheme-was-not-a-joke/).
  The element mapping (red title bars with white text, yellow for reading,
  black edges) as recreated in
  [BAMF Network Monitor PR 114](https://github.com/rhc52980/BAMF_Network_Monitor/pull/114);
  no primary source for Windows' own `CONTROL.INI` values was found, so the
  panel uses the Windows 16-colour palette's red, yellow, black, white,
  maroon, olive, green and blue.
- Windows 16-colour palette names:
  [List of software palettes](https://en.wikipedia.org/wiki/List_of_software_palettes).
- Site palette: `unleashed_site/sitekit.py` 1653-1666, and
  `LOGO_COLOURS` at 3551.
