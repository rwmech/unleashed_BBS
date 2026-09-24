# tty-ux: the 1.1.0 screens

Spec for PLAN-1.1.0.md phases 1, 3 and 5. Quoted strings are copywriter
placeholders unless marked fixed. Mock-ups are 40 columns unless labelled.

## Verdict

Everything fits what exists: the 40-column form, the `rowTitle` and
`statRow` lists, the bus notice. Two gaps gate the screens: a notice never
reaches a plugin-owned session (`deliverMail`, bbs.cpp:2341, needs
`SState::Shell`; `Plugin` has no notice hook), and `CfgField` and
`PluginSetting` carry no `note` (only bbs_users.cpp:148 sets one).
Appended fields and a hook pair, not a redesign.

## Measured

- Form (form.h:137-141, form.cpp:109-211, gotoXY 1-based): label col 2,
  9 wide; box col 12, 27 wide, 26 shown; status line col 2, 38 wide; rule
  39. Same at 80 and 132: a 40-column card, left. A note over 38 is cut
  silently (form.cpp:208).
- Line-mode cycle (form.cpp:551): a letter picks the first match, so
  `co1` and `sysop` cannot be picked by letter.
- HELP usage over 14 characters is cut (bbs_shell.cpp:63). Shortcuts
  taken: W H ? C T M I F. No `/o` or `/y` in the room (chat.cpp:1906-2328).
- Shell notice (bbs.cpp:2339-2405): bell unless `bellOff`, flashing tag,
  rub out, line, blank, prompt. One-key questions are the house modal.

## 1. Sysop page

`OPERATOR`, shortcut `O`, usage `[O]PERATOR`, Main menu after PAGE. O is
PCBoard's; Y is free but says less. Room: `/o`.

Caller, every terminal:

```
0         1         2         3
0123456789012345678901234567890123456789
[3] Main: o
Why do you want the sysop?
can't upload to Drop Box_
Ringing the sysop /   (any key stops)
```

- `O <reason>` skips the question. Reason from column 0, 60 max.
- `fx::spin` in place, 45 s. Then one Yellow line, blank, prompt:
  - answered: "The sysop answered. You're in the room, talking to the
    sysop only. /p* ends it." Both land in chat with `g_sticky` set to
    each other, markers `[>S]` and `[>3]`. The room is the two-person
    mode; there is no RAM for another.
  - declined: "The sysop can't talk now. Your note was left."
  - away (on, visible, DND): "The sysop is away. Your note was left."
  - not on, hidden or lurking, one text so HIDE stays undetectable
    (bbs_shell.cpp:2047): "The sysop isn't on. Your note was left."
  - 45 s or any key: "No answer. Your note was left."
- Rate: one ring per caller per 3 minutes, three per call, one at a time
  board-wide. LightRed, in place of the question: "Already rung. Try again
  in 2 minutes." (37), "Somebody else is ringing. Try in a minute." The
  note is kept.

Sysop. New `BusKind::Ring`, tag ` RING ` through `deliverMail`, then a
one-key question the core owns:

```
0         1         2         3
0123456789012345678901234567890123456789
quantumrob (3) is ringing: can't upload
to Drop Box
[A]nswer [D]ecline [X] Away [Q] Later: _
```

- Line wraps at `rowWidth()`, column 0. Question 38 characters, fixed,
  same at 80.
- A answers. D declines. X sets DND (what `cmdPage` already calls away)
  and declines. Anything else: still ringing; the key is consumed.
- Sysop's bare `O`: re-asks while a ring waits, else "Nobody is ringing."
- Room, via `interrupt()` (chat.cpp:500): `--> quantumrob (3) is ringing:
  <reason>` then `--> /o answers, /o- declines.`
- Forums, files, mailbox: appended hooks `liftInput(Session&)` and
  `restoreInput(Session&)`; chat has both, the others newline and redraw
  their prompt. The core prints and takes the key between them. Without
  them the ring waits for the shell, as pages do now.
- Forms: status line, Yellow, as `warnNow`: "RING quantumrob (3). ESC,
  then O." (31).

Login and elevation note, after the arrival line, Yellow as
files.cpp:1636, up to 8, then cleared. Wrap at `rowWidth()`, column 0:

```
2 pages while you were off:
22 Sep 22:14 quantumrob (3): can't
upload to Drop Box
```

## 2. Timezone

Two rows replace `tz` (bbs_sysop.cpp:758):

```
0         1         2         3
0123456789012345678901234567890123456789
  Timezone  US Central (Chicago)
  TZ string CST6CDT,M3.2.0,M11.1.0.....
```

- `Timezone`: `FF_CYCLE`, table names plus `Custom`, names 24 max. After
  each key on a CONFIG form the owner hashes the field; on change it
  rewrites `TZ string` and redraws it. Needs a public `Form::redraw(i)`.
- `TZ string` is what is saved. Typing into it flips Timezone to Custom.
  A file value matching the table opens as that entry, else Custom.
- Notes: "POSIX TZ. Custom takes a typed string." (37); TZ string "Where
  to find yours: <placeholder>".
- Line mode: a cycle prompt lists its choices numbered, a number picks.
  Every cycle field, see Measured.
- About 16 zones, UTC and the UK through the US to New Zealand. Longest
  string, `AEST-10AEDT,M10.1.0,M4.1.0/3`, is 28 and scrolls in the box.

## 3. CONFIG forums

- Under 12 set: four level rows, one `[ name ]` per set topic, one
  `[ new topic ]`. 16 fields: last row 19, buttons 21, status 23, prompt
  24. The existing budget, nothing spare.
- 12 set: four level rows and `[ 12 topics ]`, opening TOPICS: 16 buttons,
  no level rows, ESC back. Each button is today's sub-page.
- Line mode as file areas (form.cpp:488): `Topic 3 [C64 talk] open
  (y/N)?`, `Topic 5 [new topic] open (y/N)?`.
- Unset rows are not drawn; "not set" never shows here.

## 4. SD backup and restore

Core, `CF_SYSOP`: `BACKUP SD`, `BACKUP SD SCREENS`, `RESTORE SD [n|file]`,
`RESTORE SD SCREENS [n|file]`. Usage under 14: `BACKUP SD`, "SCREENS:
screens only" in the description; `RESTORE SD n`. Names
`unleashed-YYYYMMDD-HHMM.zip` (27), `screens-YYYYMMDD-HHMM.zip` (25).
Bare `RESTORE SD` lists, newest first. Not FILES: a number can only name
a file this list showed, the file manager's argument.

```
0         1         2         3
0123456789012345678901234567890123456789
 Backups on the card            3 files
 1  unleashed-20260923-2210.zip   31 KB
 2  screens-20260922-0900.zip      9 KB
 3  unleashed-20260921-0300.zip   30 KB
---------------------------------------
RESTORE SD n restores one.
```

`%2u  %-27s %6s` is 38. At 80 the bar and rule widen, rows stay.

Backup: "Writing unleashed-20260923-2210.zip", one `.` per entry, then
"Saved: 14 files, 31 KB." Failures: "No card. SD MOUNT first.", "Card
full: 31 KB needed, 12 KB free."

Restore: validate as HTTP does (backup.cpp:158-183), then, at 80:

```
 RESTORE unleashed-20260923-2210.zip
In zip    14 files, 31 KB
Replaces  system.cfg, 23 accounts, 12 screens
Removes   2 screens not in the zip
Rejected  1: screens/foo.txt bad name
Wi-Fi     network and password from the zip
Restore now? (y/N) _
```

Label 10 columns; at 40 a value wraps at `rowWidth()-10` under itself.
SCREENS: In zip, Replaces ("n screens on the card"), Rejected; never
Removes. Then "Restoring" with dots, the `imp_.apply` message as the
result, logged as HTTP is. 60 s on the question, then "Not restored."

## 5. Lights page

```
0         1         2         3
0123456789012345678901234567890123456789
  Enabled   yes
  Read      ...
  Drive pin 13
  Strip pin -1
  Strip     nodes
  Bright    32
```

- Pins `PS_PIN`, but the validator (bbs_sysop.cpp:1527-1546) refuses `-1`
  on a plugin pin. Allow -1 as off, range -1..33, or the default cannot be
  typed.
- Strip: new `PS_CYCLE`, `choices` appended to `PluginSetting`,
  `nodes|scanner|rainbow|off`. Distinct first letters: line mode works.
- Bright `PS_NUM` 1..255, default 32. Note, also on Strip pin (37): "10
  pixels white = 600 mA. Own 5V feed." Resistor and capacitor go in the
  docs, not on 38 columns.
- Pins equal, or a flash pin: `fail()` on the second, "Same as the drive
  pin".

## 6. Network page

- `kPages` row `network   Wi-Fi and port, next restart` fits the 29
  columns left at 40. `wifi` stays an alias in `pageByName`.
- Title `NETWORK`; rows `Network`, `Password`, `Port` (`CK_NUM`, cap 5).
  Port note "Callers dial this. Used at restart." (35). Equal to the
  backup port: `fail()`, "Same as the backup port".
- Announce: "Outside port" is 12 characters; the label cuts at 9 and would
  read `Outside p`. Label `Outside`, note (37): "The router's port.
  Blank: the board's."

## Stays

- The form stays a 40-column card at 80 and 132: values are 26 characters,
  and widening moves every CONFIG screenshot on the site for nothing.
- One ring question at every width. Sysop chat on the room, not a mode.

## Order

- Network page and the Outside label.
- Forums growing page: composite machinery exists.
- Lights page: table, `PS_CYCLE`, -1 on pins.
- Backup list and restore confirmation: list helpers exist.
- Timezone: `Form::redraw`, numbered line-mode cycle.
- Sysop page last: hooks, modal key, bus kind.
