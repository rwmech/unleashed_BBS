# Copy for 1.1.0

Written 2026-09-23 by `explain` against `internal/PLAN-1.1.0.md` and the
layouts in `internal/tty-ux-1.1.0-2026-09-23.md`. Words only: nothing in
`src/`, `tools/` or `data/` was changed. Where the UX spec left a quoted
placeholder, the string here replaces it; where I changed the UX's shape
(an extra row, a different word), the builder note says so.

## How to read this file

- Every string a caller or sysop sees is in a table: `Id`, `Max`, `Len`,
  `Text`. `Len` is measured by script with the worst case filled in for
  each placeholder, not counted by eye.
- `Max` is the budget:
  - `39`: one line at 40 columns, must not wrap.
  - `38`: the form's status line (`Form::status` cuts at 38), which is where
    CONFIG field notes and `fail()` messages go.
  - `29`: a value beside a 10-column label, or a row description in the
    CONFIG page list, at 40 columns (both measured in the UX spec).
  - `24`: a HELP description column at 40 columns (39 less `kUsageCol` 15),
    or a Timezone name (the UX spec's cap).
  - `14`: a HELP usage (the UX spec: longer is cut).
  - `9`: a form label.
  - `wrap`: goes through the word wrap at `rowWidth()`. The 40-column wrap
    is shown under the table so it can be checked.
  - `console`: serial console only. No hard limit; kept under 72.
- Placeholders and the worst case used to measure them: `<handle>` 20
  characters (`BBS_USER_MAX`), `<n>` a node number, `10`; `<file>`
  `unleashed-20260923-2210.zip` (27); `<sfile>` `screens-20260923-2210.zip`
  (25); `<kb>` and `<count>` three digits; `<date>` `22 Sep 22:14`.
  `<reason>`, `<why>` and `<err>` vary and only appear in `wrap` rows.
- House rules held throughout: plain ASCII, none of the characters a C64
  lacks (braces, bar, tilde, backslash, underscore, caret, backtick), no
  exclamation marks, no em dashes, sentences start at column 0.
- Colours follow the house vocabulary (ux-message-boards.md): Yellow for an
  event or a count, LightRed for a refusal and nothing else, Grey for
  furniture, LightGreen for a result that went well.

## 1. Sysop page: OPERATOR

The UX spec names it `OPERATOR`, shortcut `O`, `/o` in the room. "Ring" is
the word throughout, so it never collides with `PAGE`, which is caller to
caller. What the caller typed is "what you wrote" to them and "their note"
to the sysop.

### HELP

| Id | Max | Len | Text |
|---|---|---|---|
| OP-help-usage | 14 | 10 | `[O]PERATOR` |
| OP-help-desc | 24 | 18 | `ring for the sysop` |
| OP-room-desc | 24 | 18 | `ring for the sysop` |

Long help, in the format of `internal/long-help-2026-09-22.md`:

```text
123456789012345678901234567890123456789

== OPERATOR
usage: OPERATOR [reason]
Rings for the sysop. If they answer,
you talk in the chat room, the two of
you. If not, what you wrote is saved
for them. O also works; /o in the room.
Sysop: O answers a waiting ring.

== /o
usage: /o [reason], or /o-
Rings for the sysop, as OPERATOR does
at the main prompt. Sysop: /o answers a
ring and /o- declines it.
```

### The caller

| Id | Max | Len | Text |
|---|---|---|---|
| OP-ask | 39 | 31 | `What do you need the sysop for?` |
| OP-cancel | 39 | 13 | `Nothing sent.` |
| OP-ringing | 39 | 17 | `Ringing the sysop` |
| OP-ringing-hint | 39 | 15 | `(any key stops)` |
| OP-answered | wrap | 97 | `The sysop answered. You're in the chat room, and what you type goes to the sysop only. /q leaves.` |
| OP-declined | wrap | 65 | `The sysop can't talk right now. What you wrote is saved for them.` |
| OP-away | wrap | 52 | `The sysop is away. What you wrote is saved for them.` |
| OP-unavailable | wrap | 60 | `The sysop isn't available. What you wrote is saved for them.` |
| OP-noanswer | wrap | 49 | `No answer. What you wrote is saved for the sysop.` |
| OP-stopped | wrap | 59 | `You stopped ringing. What you wrote is saved for the sysop.` |
| OP-rate-2 | 39 | 37 | `Already rung. Try again in 2 minutes.` |
| OP-rate-1 | 39 | 36 | `Already rung. Try again in 1 minute.` |
| OP-rate-call | 39 | 37 | `Three rings is the most for one call.` |
| OP-rate-board | 39 | 36 | `One ring at a time. Try in a minute.` |

- `OP-ask` is the question when `O` has no reason. Enter on an empty line,
  or ESC, answers `OP-cancel` and sends nothing: a ring with no reason is
  a bell the sysop learns to ignore, which is GTalk's rule and the reason
  the chat-commands report kept it.
- `OP-ringing` and `OP-ringing-hint` sit either side of the spinner, as in
  the UX mock-up: `Ringing the sysop /   (any key stops)`.
- `OP-stopped` is for a caller who pressed a key to stop. The UX spec used
  `OP-noanswer` for both; "No answer" is not true when the caller hung up
  on the ring themselves, so this gives the builder the honest one.
- The outcome lines are Yellow and the rate lines LightRed, per the UX spec.

At 40 columns the outcome lines wrap like this (checked by script):

```text
123456789012345678901234567890123456789
The sysop answered. You're in the chat
room, and what you type goes to the
sysop only. /q leaves.

The sysop can't talk right now. What
you wrote is saved for them.

The sysop is away. What you wrote is
saved for them.

The sysop isn't available. What you
wrote is saved for them.

No answer. What you wrote is saved for
the sysop.

You stopped ringing. What you wrote is
saved for the sysop.
```

### The sysop

| Id | Max | Len | Text |
|---|---|---|---|
| OP-tag | 39 | 6 | ` RING ` |
| OP-notice | wrap | 98 | `<handle> (<n>) is ringing: <reason>` |
| OP-notice-guest | wrap | 105 | `<handle> (<n>, guest) is ringing: <reason>` |
| OP-question | 39 | 39 | `[A]nswer [D]ecline [X] Away [Q] Later: ` |
| OP-did-answer | wrap | 78 | `Answered. What you type goes to <handle> only. /q leaves the room.` |
| OP-did-decline | 39 | 30 | `Declined. They have been told.` |
| OP-did-away | wrap | 77 | `You're away: rings are saved as notes and pages are off. DND brings you back.` |
| OP-did-later | 39 | 39 | `Still ringing. O answers while it does.` |
| OP-ended | wrap | 63 | `<handle> (<n>) stopped ringing. Their note is saved.` |
| OP-hungup | wrap | 55 | `<handle> (<n>) hung up. Their note is saved.` |
| OP-none | 39 | 18 | `Nobody is ringing.` |
| OP-room-1 | wrap | 98 | `<handle> (<n>) is ringing: <reason>` |
| OP-room-2 | 39 | 25 | `/o answers, /o- declines.` |
| OP-form | 38 | 44 | `RING <handle> (<n>). ESC, then O.` |
| OP-form-short | 38 | 31 | `RING from node <n>. ESC, then O.` |

- `OP-tag` is `RING` with one space either side, six characters, the
  same shape as the existing ` PAGE ` tag. (A Markdown viewer drops the
  two spaces from inside the backticks; the raw file has them.)
- `OP-notice` is the line printed after the flashing tag. The reason is 60
  characters at most (UX spec), so with a 20-character handle the line is
  at most 98: three lines at 40 columns, two at 80. With a real handle,
  `quantumrob (3) is ringing: can't upload to Drop Box`, it is the UX
  mock-up exactly.
- `OP-notice-guest` says "guest" in words rather than with the `*` marker,
  because the sysop is deciding whether to answer, and "no account to
  reply to" is the fact that matters.
- `OP-question` is the UX spec's line with one space kept after the colon
  for the cursor, which makes it 39. If the builder wants the 38 of the
  spec, drop the trailing space; the words are unchanged.
- `OP-did-away`: the UX spec makes X the existing DND, so the way back is
  `DND`, which already answers "Pages are on."
- `OP-did-later`: Q dismisses the question and lets the ring run out.
- `OP-ended` covers a ring that ran out after Q, or while the question was
  still up. `OP-hungup` is the caller dropping the line mid-ring.
- `OP-room-1` and `OP-room-2` are the room's two lines. They are written
  without the `--> ` prefix because the room's `tell()` adds it to a
  joined caller's system lines (0.18.0); if this path does not go through
  `tell()`, prefix them by hand.
- **`OP-form` does not fit the status line with a long handle.** At 20
  characters it is 44 against 38. Use `OP-form` when it fits and
  `OP-form-short` (node only, at most 31) when it does not; never cut the
  handle, because a cut handle names somebody else.

### The login and elevation note

| Id | Max | Len | Text |
|---|---|---|---|
| OP-login-head | 39 | 29 | `<count> rings while you were off:` |
| OP-login-head-1 | 39 | 26 | `1 ring while you were off:` |
| OP-login-row | wrap | 100 | `<date> <handle> (<n>): <reason>` |
| OP-login-row-guest | wrap | 107 | `<date> <handle> (<n>, guest): <reason>` |
| OP-login-over | 39 | 25 | `Only the last 8 are kept.` |
| OP-login-gone | 39 | 33 | `Shown once. They are cleared now.` |

- "Rings", not the UX spec's "pages", so it cannot be read as `PAGE`.
- `OP-login-over` only when more than 8 rang. `OP-login-gone` is optional
  and worth having: a list that vanishes after one showing should say so.

### Builder notes: sysop page

- **The hidden sysop must look exactly like an absent one, in time as well
  as in words.** `OP-unavailable` is one sentence for not on, HIDE and
  LURK, as the UX spec asks, and it is true in all three: a hidden sysop
  has made themselves unavailable. If a hidden sysop is rung for 45
  seconds while an absent one is answered at once, the delay gives HIDE
  away however the sentence reads. Either both answer at once or both
  wait the same.
- The answered line says what you type "goes to" the sysop only, not that
  "only they see it": staff with SNOOP can watch a node, and the privacy
  screen says so.

## 2. SD backup and restore

Commands as in the UX spec: `BACKUP SD`, `BACKUP SD SCREENS`,
`RESTORE SD [n or file]`, `RESTORE SD SCREENS [n or file]`, sysop only.

### HELP

| Id | Max | Len | Text |
|---|---|---|---|
| BK-help-usage | 14 | 9 | `BACKUP SD` |
| BK-help-desc | 24 | 24 | `zip to card (or SCREENS)` |
| RS-help-usage | 14 | 12 | `RESTORE SD n` |
| RS-help-desc | 24 | 19 | `put a card zip back` |

```text
123456789012345678901234567890123456789

== BACKUP
usage: BACKUP SD [SCREENS]
Saves the zip the backup window gives:
settings, accounts and screens, in the
card's backup folder. SCREENS saves the
screens alone. The full zip holds the
Wi-Fi password as typed.

== RESTORE
usage: RESTORE SD [SCREENS] [n]
Lists the backups on the card. With n,
shows what backup n would replace and
asks first. SCREENS puts only screens
back, onto the card, where deleting
them undoes it. Staff passwords stay.
```

### BACKUP SD and BACKUP SD SCREENS

| Id | Max | Len | Text |
|---|---|---|---|
| BK-writing | 39 | 35 | `Writing <file>` |
| BK-writing-s | 39 | 33 | `Writing <sfile>` |
| BK-pause | 39 | 33 | `The board pauses while it writes.` |
| BK-saved | 39 | 25 | `Saved: <count> files, <kb> KB.` |
| BK-saved-s | 39 | 27 | `Saved: <count> screens, <kb> KB.` |
| BK-wifi | 39 | 38 | `It holds your Wi-Fi password as typed.` |
| BK-nocard | 39 | 36 | `No card mounted. Try SD MOUNT first.` |
| BK-full | 39 | 38 | `Card full: <kb> KB needed, <kb> KB free.` |
| BK-noclock | 39 | 39 | `No time yet: waiting for NTP. Try soon.` |
| BK-exists | 39 | 38 | `There is one from this minute already.` |
| BK-failed | 39 | 37 | `Card write failed. Nothing was saved.` |
| BK-busy | 39 | 39 | `A backup or restore is already running.` |
| BK-bare-1 | 39 | 34 | `BACKUP SD saves a zip on the card.` |
| BK-bare-2 | 39 | 34 | `For the backup window, press BOOT.` |

- `BK-wifi` follows `BK-saved` for a full backup and never for a screens
  one, which has no `system.cfg` in it. This is the one line the brief
  asked for; it states the fact and trusts the sysop with it.
- `BK-pause` only if the write really does hold the loop, the way
  `SD MOUNT` says "Mounting, the board pauses." Do not print it for a write
  that is spread across passes.
- **The dots do not fit on the `Writing` line.** `BK-writing` is 35, so
  one dot per entry wraps after four. Put the dots on the line below, or
  cap them at `39 - len`.
- `BK-noclock`: the file name needs the date, and a board that has not
  heard from NTP does not know it. The words match TIME's "not set
  (waiting for NTP)".
- `BK-exists`: two backups in the same minute would get the same name.
  Refuse the second rather than overwrite the first.
- `BK-failed` is only true if the half-written file is removed, the way
  the `sd` seeding removes a failed copy. If it is not removed, the line
  must say so instead.
- `BK-bare-1` and `BK-bare-2` answer `BACKUP` typed on its own.

### RESTORE SD: the list

| Id | Max | Len | Text |
|---|---|---|---|
| RS-list-title | 39 | 19 | `Backups on the card` |
| RS-list-count | 39 | 9 | `<count> files` |
| RS-list-count-1 | 39 | 6 | `1 file` |
| RS-list-foot | 39 | 26 | `RESTORE SD n restores one.` |
| RS-list-foot-s | 39 | 34 | `RESTORE SD SCREENS n restores one.` |
| RS-list-empty | 39 | 27 | `No backups on the card yet.` |
| RS-list-empty-2 | 39 | 20 | `BACKUP SD makes one.` |
| RS-bad-n | 39 | 27 | `No backup with that number.` |
| RS-bad-file | 39 | 35 | `No backup by that name on the card.` |

`RS-bad-n` matches the file manager's "No file with that number."

### RESTORE SD: checking and the question

| Id | Max | Len | Text |
|---|---|---|---|
| RS-checking | 39 | 36 | `Checking <file>` |
| RS-title | 39 | 36 | ` RESTORE <file>` |
| RS-l-inzip | 9 | 6 | `In zip` |
| RS-v-inzip | 29 | 17 | `<count> files, <kb> KB` |
| RS-l-replaces | 9 | 8 | `Replaces` |
| RS-v-replaces | 29 | 27 | `settings, accounts, screens` |
| RS-l-accounts | 9 | 8 | `Accounts` |
| RS-v-accounts | 29 | 27 | `<count>, replacing all <count> here` |
| RS-l-removes | 9 | 7 | `Removes` |
| RS-v-removes | 29 | 26 | `<count> screens not in the zip` |
| RS-l-rejected | 9 | 8 | `Rejected` |
| RS-v-rejected | wrap | 5+ | `<count>: <why>` |
| RS-l-wifi | 9 | 5 | `Wi-Fi` |
| RS-v-wifi | 29 | 28 | `the zip's, from next restart` |
| RS-l-staff | 9 | 5 | `Staff` |
| RS-v-staff | 29 | 26 | `passwords stay as they are` |
| RS-lost | 39 | 37 | `Changes made since then will be lost.` |
| RS-ask | 39 | 19 | `Restore now? (y/N) ` |

- `RS-v-replaces` names only the parts the zip has: `settings`,
  `accounts`, `screens`, joined with commas.
- **`RS-l-accounts` is a row the UX spec does not have, and it is the one
  that matters most.** A zip's `users.txt` replaces every account on the
  board, so anybody who signed up after the backup was taken is gone.
  "23, replacing all 25 here" puts both numbers in front of the sysop
  before the Y. Only when the zip has `users.txt`.
- **`RS-l-staff` is also new.** A backup never holds a staff password, so
  a restore keeps the ones the board has now. After a factory reset that
  is the published default, and the sysop needs to know that before
  answering, not after. Always shown for a full restore.
- `RS-l-wifi` only when the zip's Wi-Fi keys differ from the board's.
- `RS-lost` sits under the rows, Yellow, before the question, for a full
  restore. Settings and accounts both go back to the day of the backup.

At 40 columns, with the label column at 10:

```text
123456789012345678901234567890123456789
 RESTORE unleashed-20260923-2210.zip
In zip    14 files, 31 KB
Replaces  settings, accounts, screens
Accounts  23, replacing all 25 here
Removes   2 screens not in the zip
Wi-Fi     the zip's, from next restart
Staff     passwords stay as they are
Changes made since then will be lost.
Restore now? (y/N)
```

### RESTORE SD SCREENS: the question

| Id | Max | Len | Text |
|---|---|---|---|
| RS-s-title | 39 | 34 | ` RESTORE <sfile>` |
| RS-s-v-inzip | 29 | 19 | `<count> screens, <kb> KB` |
| RS-s-v-replaces | 29 | 23 | `<count> screens on the card` |
| RS-s-l-adds | 9 | 4 | `Adds` |
| RS-s-v-adds | 29 | 23 | `<count> screens to the card` |
| RS-s-flash | 39 | 39 | `Stock screens in flash are not touched.` |
| RS-s-off | 39 | 34 | `Card screens are off in CONFIG sd.` |

- Labels are the full restore's: `In zip`, `Replaces`, `Rejected`, never
  `Removes`, as the UX spec says. `Adds` is optional and new: without it a
  zip of 24 screens that replaces 9 reads as if 15 went missing.
- `RS-s-off` only when the `sd` plugin's Screens setting is `no`, because
  the restored screens will not play until it is on.
- The same `RS-ask` question follows.

### RESTORE: results

| Id | Max | Len | Text |
|---|---|---|---|
| RS-restoring | 39 | 9 | `Restoring` |
| RS-done | 39 | 18 | `Restored and live.` |
| RS-done-wifi | 39 | 36 | `Wi-Fi is used from the next restart.` |
| RS-done-host | 39 | 39 | `Hostname is used from the next restart.` |
| RS-done-sysop | 39 | 39 | `Now set a sysop password: CONFIG staff.` |
| RS-s-done | 39 | 34 | `Restored: <count> screens on the card.` |
| RS-s-undo | 39 | 38 | `Deleting them from the card undoes it.` |
| RS-partial | 39 | 39 | `Restored, but not every file went back.` |
| RS-cfg-bad | wrap | 24+ | `Settings did not load: <err>.` |
| RS-cfg-old | 39 | 31 | `The old ones are still running.` |
| RS-nothing | 39 | 26 | `Nothing in it can be used.` |
| RS-nothing-why | wrap | 15+ | `First problem: <why>` |
| RS-unreadable | wrap | 17+ | `Cannot read it: <why>.` |
| RS-toobig | 39 | 37 | `Too big: <kb> KB. The limit is <kb> KB.` |
| RS-no | 39 | 13 | `Not restored.` |

- `RS-restoring` takes one dot per file on the same line; nine plus the
  64-file limit would wrap, so cap the dots at 30.
- `RS-done-wifi` and `RS-done-host` only when those settings changed.
- `RS-done-sysop` whenever the board is on the published default password
  after the restore, which is every restore after a factory reset. See the
  bug in the builder notes: today it is worse than this line suggests.
- `RS-partial` when `imp_.apply` reports failures. `RS-cfg-bad` and
  `RS-cfg-old` when the settings did not reload; the importer's own
  reason goes in `<err>`, and `syscfg::reload` leaves the running settings
  untouched on failure, so `RS-cfg-old` is true.
- `RS-unreadable` takes the importer's reason as it stands, for example
  `Cannot read it: corrupt central directory.` The importer also says
  "not a zip file (size)" for a zip over the size limit (ziparc.cpp:493),
  which is untrue of a real backup that has grown too big; check the size
  before importing and answer with `RS-toobig` instead.
- `RS-no` is both the N answer and the 60 second timeout, as in the UX
  spec. The console log line can stay the HTTP path's `Applied: ...`.
- `RS-toobig`: work both numbers out from bytes, in the board's KB of
  1,024. `BBS_ZIP_MAX_BYTES` is 400,000 bytes, which is 390 KB, not the
  400 that SCREENS.md prints. Typed as "400 KB", a zip of 395 KB would be
  refused as "Too big: 395 KB. The limit is 400 KB.", which reads as the
  board being wrong. Round the size up and the limit down, so a zip one
  byte over says 391 against 390 rather than 391 against 391.

### Nightly backups

| Id | Max | Len | Text |
|---|---|---|---|
| NB-label | 9 | 7 | `Nightly` |
| NB-note | 38 | 35 | `A zip every night; the last 7 kept.` |
| NB-failed | 39 | 38 | `Last night's backup failed: card full.` |
| NB-failed-nocard | 39 | 36 | `Last night's backup failed: no card.` |
| NB-failed-clock | 39 | 37 | `Last night's backup failed: no clock.` |
| NB-log-ok | console | 68 | `backup: nightly <file>, <kb> KB, <count> on the card` |
| NB-log-skip | console | 25+ | `backup: nightly skipped: <why>` |
| NB-log-prune | console | 63 | `backup: removed the oldest nightly, <file>` |

- `NB-failed*` is a Yellow line to staff at their next login, the same
  shape as the file manager's "3 uploads awaiting approval".
- The plan does not say what time of night or which CONFIG page the
  setting is on. The note avoids a time on purpose; add one if it is
  fixed.

### Docs paragraph: BACKUP.md, a new section "Backups on the SD card"

> With an SD card fitted, the board can keep its own backups. `BACKUP SD`
> writes the same zip the backup window gives you, settings, accounts and
> screens, into a `backup` folder on the card, named for the date and time
> it was made. `BACKUP SD SCREENS` writes the screens alone. `RESTORE SD`
> lists what is on the card, newest first, and `RESTORE SD 2` shows what the
> second one would replace, including how many accounts are on the board
> now and how many are in the zip, then asks before it changes anything.
>
> A full backup holds the Wi-Fi password as typed, exactly as a downloaded
> one does, so whoever has the card has the password. It does not hold the
> staff passwords: a restore always keeps the ones the board has, so after
> a factory reset, set the sysop password again straight after restoring.
>
> `RESTORE SD SCREENS` is different. It never touches the screens built
> into the board: it copies the zip's screens onto the card, where they
> take the place of the built-in ones for as long as they are there.
> Deleting them from the card puts the built-in ones back.

(Written as prose rather than a `> ` box: this goes in BACKUP.md, which
GitHub renders. The quote markers above are only to set it apart here.)

### Builder notes: backup and restore

- **Security bug, current code, and the restore path above walks straight
  into it.** On a board still on the published default (fresh, or after a
  factory reset or, once they exist, the BOOT resets), the live sysop password is
  `unleashed` with `sysopDefault` true because `system.cfg` has no
  `sysop_password` line. Restoring any backup writes the zip's
  `sysop_password = ***` through `syscfg::unredactLine`, which substitutes
  `livePassword(0)`, so the restored file says `sysop_password =
  unleashed` explicitly. `parseFile` then sets `sawSysop`, never calls
  `useDefaultSysop`, and `sysopDefault` goes false. From then on the
  published password works from any address, not only the local network
  (bbs_shell.cpp:2145 only refuses it while `sysopDefault`), and `announce`
  stops holding the listing (announce.cpp:860), so a listed board comes
  back on the directory with a password printed on the install page.
  This is reachable today through the HTTP backup window, not only through
  `RESTORE SD`. Not mine to fix; the shape of a fix is for `unredactLine`
  to drop the line, rather than write the default, when the live sysop
  password is the default.
- **Nightly pruning must not delete a sysop's own backups.** If nightly
  files share the `unleashed-YYYYMMDD-HHMM.zip` name, "keep the last 7"
  counts the manual ones too, and a sysop who made one before a risky
  change loses it a week later without being told. A distinct name for
  the nightly files (`nightly-YYYYMMDD.zip`, 20 characters, fits the
  list's 27-character name column) keeps the two apart.
- **`RESTORE SD` on a screens-only zip is not the same as `RESTORE SD
  SCREENS`.** The full path replaces the built-in screens set exactly,
  removing any screen not in the zip, which is the backup window's rule;
  the SCREENS path writes to the card. Both will be offered the same file
  from the same list. The confirmation's `Removes` row is what tells them
  apart, so it must never be left out of the full restore.

## 3. Timezone

### Field notes

| Id | Max | Len | Text |
|---|---|---|---|
| TZ-note-zone | 38 | 38 | `Pick a zone, or Custom and type below.` |
| TZ-note-string | 38 | 34 | `Find yours: unleashedbbs.com/setup` |
| TZ-note-single | 38 | 38 | `POSIX TZ. Help: unleashedbbs.com/setup` |
| TZ-custom | 24 | 6 | `Custom` |
| TZ-bad | 38 | 37 | `The board cannot read that TZ string.` |

- `TZ-note-zone` goes on the UX spec's `Timezone` cycle and
  `TZ-note-string` on its `TZ string` field. It is written to be true in
  both form modes: the UX spec gives line mode a numbered list, where
  "Space" would be wrong.
- `TZ-note-single` is for a single field, if the two-row design is not
  built.
- **The notes send people to `/setup`, so `/setup` has to carry the
  table below and the "not in the list" paragraph.** The plan already
  pairs a /setup update with 1.1.0; this is the content for it.
- `TZ-bad` only if the builder adds a check. Today the parser takes any
  string, and newlib's `tzset` gives up part way through a string it
  cannot read, so a typo shows up as a wrong clock rather than an error.

### The table

Ordered UTC first, because it is what every board ships with, then west to
east. Every name is 24 characters or fewer and every string 40 or fewer
(the CONFIG cap). Longest name 24, longest string 31.

| # | Name (shown) | POSIX TZ string (saved) | Stands for (tz database zones checked) |
|---|---|---|---|
| 1 | `UTC` | `UTC0` | Etc/UTC |
| 2 | `US Hawaii (Honolulu)` | `HST10` | Pacific/Honolulu |
| 3 | `US Alaska (Anchorage)` | `AKST9AKDT,M3.2.0,M11.1.0` | America/Anchorage |
| 4 | `US Pacific (Los Angeles)` | `PST8PDT,M3.2.0,M11.1.0` | America/Los_Angeles |
| 5 | `Arizona, BC (Phoenix)` | `MST7` | America/Phoenix, America/Vancouver (also America/Whitehorse, same footer) |
| 6 | `US Mountain (Denver)` | `MST7MDT,M3.2.0,M11.1.0` | America/Denver |
| 7 | `Mexico, Alberta, Sask.` | `CST6` | America/Mexico_City, America/Edmonton, America/Regina, America/Inuvik |
| 8 | `US Central (Chicago)` | `CST6CDT,M3.2.0,M11.1.0` | America/Chicago, America/Winnipeg |
| 9 | `US Eastern (New York)` | `EST5EDT,M3.2.0,M11.1.0` | America/New_York, America/Toronto |
| 10 | `Atlantic (Halifax)` | `AST4ADT,M3.2.0,M11.1.0` | America/Halifax |
| 11 | `Chile (Santiago)` | `<-04>4<-03>,M9.1.6/24,M4.1.6/24` | America/Santiago |
| 12 | `Newfoundland (St John's)` | `NST3:30NDT,M3.2.0,M11.1.0` | America/St_Johns |
| 13 | `Sao Paulo, Buenos Aires` | `<-03>3` | America/Sao_Paulo, America/Argentina/Buenos_Aires, America/Montevideo |
| 14 | `UK (London)` | `GMT0BST,M3.5.0/1,M10.5.0` | Europe/London |
| 15 | `Ireland (Dublin)` | `GMT0IST,M3.5.0/1,M10.5.0` | Europe/Dublin (see note) |
| 16 | `Portugal (Lisbon)` | `WET0WEST,M3.5.0/1,M10.5.0` | Europe/Lisbon, Atlantic/Canary |
| 17 | `West Africa (Lagos)` | `WAT-1` | Africa/Lagos |
| 18 | `Central Europe (Berlin)` | `CET-1CEST,M3.5.0,M10.5.0/3` | Europe/Berlin, Paris, Madrid, Rome, Warsaw, Stockholm, Amsterdam |
| 19 | `Eastern Europe (Athens)` | `EET-2EEST,M3.5.0/3,M10.5.0/4` | Europe/Athens, Helsinki, Kyiv, Bucharest |
| 20 | `Egypt (Cairo)` | `EET-2EEST,M4.5.5/0,M10.5.4/24` | Africa/Cairo |
| 21 | `South Africa (Pretoria)` | `SAST-2` | Africa/Johannesburg |
| 22 | `East Africa (Nairobi)` | `EAT-3` | Africa/Nairobi |
| 23 | `Turkey (Istanbul)` | `<+03>-3` | Europe/Istanbul |
| 24 | `Russia (Moscow)` | `MSK-3` | Europe/Moscow |
| 25 | `Gulf (Dubai)` | `<+04>-4` | Asia/Dubai |
| 26 | `India (New Delhi)` | `IST-5:30` | Asia/Kolkata |
| 27 | `China (Beijing)` | `CST-8` | Asia/Shanghai |
| 28 | `Singapore, Malaysia` | `<+08>-8` | Asia/Singapore, Asia/Kuala_Lumpur |
| 29 | `Australia WA (Perth)` | `AWST-8` | Australia/Perth |
| 30 | `Japan (Tokyo)` | `JST-9` | Asia/Tokyo |
| 31 | `Australia SA (Adelaide)` | `ACST-9:30ACDT,M10.1.0,M4.1.0/3` | Australia/Adelaide |
| 32 | `Australia QLD (Brisbane)` | `AEST-10` | Australia/Brisbane |
| 33 | `Australia East (Sydney)` | `AEST-10AEDT,M10.1.0,M4.1.0/3` | Australia/Sydney, Melbourne, Hobart |
| 34 | `New Zealand (Auckland)` | `NZST-12NZDT,M9.5.0,M4.1.0/3` | Pacific/Auckland |
| 35 | `Custom` | whatever is typed | |

Checked the same way, and left out only for length. Add any of them back
if the list can take it:

| Name | String | Stands for |
|---|---|---|
| `Colombia, Peru (Bogota)` | `<-05>5` | America/Bogota, America/Lima |
| `Indonesia (Jakarta)` | `WIB-7` | Asia/Jakarta |
| `Korea (Seoul)` | `KST-9` | Asia/Seoul |
| `Hong Kong` | `HKT-8` | Asia/Hong_Kong |
| `Philippines (Manila)` | `PST-8` | Asia/Manila |

If the UX spec's "about 16" wins over the brief's "about 30", keep 1, 2,
3, 4, 5, 6, 7, 8, 9, 13, 14, 18, 19, 26, 30, 33 and 34: that is 17, and
every one of them is a place with a lot of people or a lot of BBS history.

### Notes on the table

- **No two entries share a string**, so "a file value matching the table
  opens as that entry" always has one answer. Where places share rules,
  the name lists them: Arizona and British Columbia are both `MST7`;
  Mexico City, Alberta and Saskatchewan are all `CST6`; Sao Paulo, Buenos
  Aires and Montevideo are all `<-03>3`.
- **Canada changed in 2026, and the names say so on purpose.** British
  Columbia stopped changing its clocks after 8 March 2026 and stays at
  UTC-7; Alberta did the same at UTC-6 from 18 June; the Northwest
  Territories followed on 21 August (tz database 2026b, 2026c, 2026d). A
  sysop in Vancouver or Calgary who picks "US Pacific" or "US Mountain"
  gets a clock that goes wrong on 1 November 2026. That is why those two
  entries are named for the provinces and the US ones say "US".
- **Manitoba may be next.** The tz database's unreleased development tree
  already says "Manitoba moves to permanent -05 on 2026-11-01"; 2026d, the
  current release, does not. If that is released, Winnipeg leaves entry 8
  for UTC-5 all year, which no entry here is, and the Canada paragraph in
  the docs below loses its Manitoba clause. Check the tz announcements
  before the 1.1.0 tag.
- **Ireland's string is not the tz database's footer.** The database
  writes `IST-1GMT0,M10.5.0,M3.5.0/1`, which models winter as a negative
  daylight saving; I have not seen newlib handle that and chose not to find
  out on somebody's board. `GMT0IST,M3.5.0/1,M10.5.0` gives the identical
  UTC offset at every instant checked (below) and shows the abbreviations
  Irish callers expect.
- **The names with numbers in angle brackets are correct, and the board's
  C library reads them.** Where the tz database has no agreed abbreviation
  it uses the offset, `<-03>` and so on. The esp-4.3.0 branch of
  Espressif's newlib parses `<...>` in `tzset_r.c`; espressif32 6.9.0 pins
  the toolchain that carries it (13.2.0+20240530, newlib 4.3.0); and the
  firmware.elf built from this tree on 2026-09-23 contains that parser's
  format string (`%11[-+0-9A-Za-z]%n`) and not the old one
  (`%10[^0-9,+-]%n`). The ESP32 ROM has the old `tzset`, but it is listed
  only in `esp32.rom.newlib-time.ld`, which IDF 5.3.1 does not link into
  an app.
- `TIME` prints the abbreviation (`%Z`), so a caller in Sao Paulo sees
  `-03`, which is what their own computer would say.
- An entry whose string has a comma in it changes its clocks twice a
  year; one without does not. Egypt is the one most lists get wrong: it
  brought daylight saving back in 2023 (tz database 2023a).

### How the strings were checked

- Source: the tz database, release 2026d (2026-09-11), downloaded from
  data.iana.org on 2026-09-23.
- Each string is the POSIX TZ footer that the reference compiler `zic`
  writes into the compiled zone, read with `tail -n 1` (RFC 9636 defines
  that footer as a POSIX TZ string), except Ireland as noted.
- Then each string was compared with its compiled zone(s) under glibc:
  the UTC offset every 15 minutes from 2026-11-02 to 2032-01-01, about
  180,000 instants per zone, 60 zone and string pairs (54 for the main
  table, 6 for the extras). Zero differences.
  The start is 2 November 2026 because the tz database models the
  British Columbia and Alberta changes as happening on 1 November 2026,
  for software reasons its NEWS explains; the UTC offsets before that are
  the same anyway.
- **Not checked: running these strings on an ESP32.** The comparison ran
  under glibc on a PC. newlib's parser accepts every construct used here
  (angle brackets, `/24`, `/0`, `M.w.d` with week 5, southern-hemisphere
  rules) by its source, but nobody has set each one on a board and read
  `TIME`. Worth one pass on the bench for 11, 20, 31 and 34, the unusual
  ones.

### Docs paragraph: /setup (board page) and COMMANDS.md system.cfg table

> **Timezone** and **TZ string** (`tz`): Timezone picks a zone by name
> from a list, and TZ string shows the rule behind it, which is what the
> board keeps. Pick **Custom** to type your own. As shipped, `UTC`.
>
> A TZ string is the POSIX form the board's C library reads. It starts with
> the zone's short name and its offset from UTC in hours, counted **west**,
> so US zones are positive and zones east of London are negative. A zone
> with daylight saving adds the summer name and when the clocks change:
> `EST5EDT,M3.2.0,M11.1.0` is US Eastern, changing on the second Sunday of
> March and the first Sunday of November.
>
> If your place is not in the list, a Linux computer can tell you its
> string: `tail -n 1 /usr/share/zoneinfo/Europe/Paris`, with your own area
> and city, prints it. The answer is only as current as that computer's
> time zone data, and the rules do change: British Columbia, Alberta and
> the Northwest Territories all stopped changing their clocks in 2026, and
> lists of these strings made before then give the old rules. If your
> government changes the rules, type the new string as Custom; the board
> does not update its list by itself.

On the Canada point, for the same page:

> Ontario and Quebec use **US Eastern**, and Manitoba **US Central**: the
> rules are the same. British Columbia and Yukon are **Arizona, BC**, and
> Alberta, Saskatchewan and the Northwest Territories are **Mexico,
> Alberta, Sask.**, because none of them change their clocks any more.

(The Manitoba clause comes out if the tz database releases the change
described above.)

### Sources for the timezone facts

- tz database release 2026d, IANA: https://data.iana.org/time-zones/tzdb/
  (files `northamerica`, `southamerica`, `europe`, `africa`, `asia`,
  `australasia`, `NEWS`)
- tz database development tree, for the unreleased Manitoba entry:
  https://github.com/eggert/tz/blob/main/NEWS
- TZif footer as a POSIX TZ string: RFC 9636,
  https://datatracker.ietf.org/doc/rfc9636/ ; tzfile(5),
  https://man7.org/linux/man-pages/man5/tzfile.5.html
- newlib angle-bracket support, Espressif's branch:
  https://github.com/espressif/newlib-esp32/blob/esp-4.3.0/newlib/libc/time/tzset_r.c
- A published list of these strings that is out of date for British
  Columbia, Alberta and the Northwest Territories, and for Morocco (last
  changed 2025-09-29; checked 2026-09-23), which is why the docs above do
  not point at a list: https://github.com/nayarsystems/posix_tz_db

## 4. Lights

Fields as in the UX spec: `Drive pin`, `Strip pin`, `Strip`, `Bright`.

| Id | Max | Len | Text |
|---|---|---|---|
| LT-note-drive | 38 | 37 | `The disk light: one pixel. -1 is off.` |
| LT-note-strip | 38 | 36 | `10 pixels need their own 5 V supply.` |
| LT-note-mode | 38 | 38 | `nodes: one pixel for each caller line.` |
| LT-note-bright | 38 | 33 | `32 is about 75 mA; 255 is 600 mA.` |
| LT-same-pin | 38 | 36 | `That is the drive pin. Pick another.` |
| LT-flash-pin | 38 | 32 | `Pins 6 to 11 are the flash chip.` |
| LT-warn-1 | 39 | 37 | `Ten pixels at full white draw 600 mA.` |
| LT-warn-2 | 39 | 34 | `Give the strip its own 5 V supply.` |
| LT-what | 29 | 25 | `disk light and node strip` |

- `LT-warn-1` and `LT-warn-2` are the two-line power warning. Both are 38
  or under, so either can also stand alone on the status line.
- `LT-note-bright` puts the numbers where the setting is: at the shipped
  32, ten pixels at white draw about an eighth of 600 mA.
- `LT-flash-pin` is `syscfg::pinProblem`'s "pins 6-11 are the flash chip"
  as a sentence; the parser's own wording can stay in the log.
- `LT-what` is the CONFIG page list's one-line description (29 columns
  left at 40, as for the network row).

### Docs paragraph: COMMANDS.md, plugin `lights`

> **lights**: two NeoPixel (WS2812B) outputs, both off until you give them
> a pin.
>
> - **Drive pin**: one pixel that shows storage at work. Amber when the SD
>   card is read or written, cool white for the board's own flash, a slow
>   red blink after a storage error, and a dim glow in between. Every
>   flash is held long enough to see, so a read that takes two
>   milliseconds still shows. `-1` is off, as shipped.
> - **Strip pin**: a strip of ten pixels. In `nodes`, the shipped mode,
>   each pixel is one caller line: dark while the line is free, the
>   caller's rank colour while somebody is on (the colours WHO uses), and
>   a flicker when that line has traffic. `scanner` sweeps a light from
>   end to end, `rainbow` cycles the colours, and `off` is off. `-1` is
>   off, as shipped.
> - **Bright**: a brightness cap for both, 1 to 255. As shipped, 32.
> - Neither pin can be 6 to 11, which the flash chip uses, and the two
>   cannot be the same pin. A change applies when the plugin restarts,
>   which saving the page does.
>
> **Power, before you wire the strip.** One pixel draws at most about
> 60 mA at full white, which the board's own 5 V pin handles from USB.
> Ten draw about 600 mA, and the board itself needs up to about 400 mA
> when its radio transmits. Together that is more than a USB port has to
> supply (500 mA on USB 2, 900 mA on USB 3). The shipped cap of 32 brings
> the strip down to roughly 75 mA, but a cap is a setting and settings get
> changed, so wire it for full brightness:
>
> - Give the strip its own 5 V supply, rated 1 A or more, and join its
>   ground to the board's ground. Without the shared ground the data line
>   has nothing to be measured against, and the pixels show nonsense.
> - Put a 330 to 470 ohm resistor in the data line, close to the first
>   pixel.
> - Put a 100 nF capacitor across the strip's 5 V and ground, at the
>   strip. If the strip flickers when it changes colour, a larger
>   electrolytic across the same two points helps; Adafruit suggests 500
>   to 1000 microfarads.
> - Connect ground first and disconnect it last.

Sources: 60 mA per pixel at full white, and the 300 to 500 ohm resistor
and 500 to 1000 uF capacitor, are Adafruit's
(https://learn.adafruit.com/adafruit-neopixel-uberguide/powering-neopixels
and .../best-practices). The 400 mA radio peak is the figure already on
the site's build page. The 330 to 470 ohm and 100 nF values are Rob's,
from CLAUDE.md. Level shifting is deliberately not mentioned: settled by
Rob, and not to be raised again.

## 5. Network

| Id | Max | Len | Text |
|---|---|---|---|
| NW-what | 29 | 28 | `Wi-Fi and port, next restart` |
| NW-title | 39 | 7 | `NETWORK` |
| NW-l-port | 9 | 4 | `Port` |
| NW-note-port | 38 | 37 | `Callers use it from the next restart.` |
| NW-same | 38 | 38 | `Same as the backup port. Pick another.` |
| NW-saved | 38 | 33 | `Saved, used from the next restart` |
| AN-l-outside | 9 | 7 | `Outside` |
| AN-note-outside | 38 | 38 | `What callers dial through your router.` |
| AN-note-outside-alt | 38 | 35 | `Router's port. Blank: same as Port.` |

- `NW-saved` is the existing wording from the Wi-Fi page
  (bbs_sysop.cpp:1589), unchanged, so the two pages say the same thing.
- `AN-l-outside`: "Outside port" is 12 characters against a 9-character
  label column, which the UX spec measured cutting to `Outside p`. The
  note carries the word "port" instead.
- `AN-note-outside` is the brief's sentence and fits exactly.
  `AN-note-outside-alt` trades it for the blank default, which is the
  other thing worth knowing; pick one. The docs say both.

### Docs text: /setup and COMMANDS.md

> **Port** (`port`): The port callers dial. Used from the next restart.
> It cannot be the backup window's port. Takes 1 to 65535; as shipped,
> `6400`. If callers reach the board from the internet, the forward on
> your router has to point at the new number too.

For the announce page:

> **Outside**: The port callers dial from the internet, when your router
> forwards a different number to the board. Leave it empty if the router
> forwards the same number as **Port**, and the board sends that.

## 6. BOOT-hold reset

### Console lines

`reset:` in the style of the other console prefixes (`bbs:`, `sd:`,
`cfg:`). All `console`, no width limit; all under 72.

| Id | Max | Len | Text |
|---|---|---|---|
| RB-held | console | 56 | `reset: BOOT held. Let go before 7 s and nothing happens.` |
| RB-7 | console | 69 | `reset: 7 s. Let go now to put the sysop password back to the default.` |
| RB-15 | console | 69 | `reset: 15 s. Let go now for a FACTORY RESET of accounts and settings.` |
| RB-20 | console | 52 | `reset: 20 s. Cancelled. Let go; nothing will change.` |
| RB-early | console | 39 | `reset: let go at <n> s. Nothing changed.` |
| RB-pw-1 | console | 55 | `reset: sysop password is back to the published default.` |
| RB-pw-2 | console | 60 | `reset: it works from this network only, until it is changed.` |
| RB-pw-3 | console | 55 | `reset: accounts, settings, mail and Wi-Fi are all kept.` |
| RB-pw-4 | console | 62 | `reset: the directory listing waits until the password changes.` |
| RB-fr-1 | console | 48 | `reset: FACTORY RESET. Erasing userdata and logs.` |
| RB-fr-2 | console | 60 | `reset: done. Screens, firmware and SD card were not touched.` |
| RB-fr-3 | console | 62 | `reset: restarting with no Wi-Fi. The web installer sets it up.` |
| RB-late | console | 42 | `reset: let go after 20 s. Nothing changed.` |
| RB-pw-fail | console | 54+ | `reset: could not write system.cfg (<err>). Nothing changed.` |
| RB-fr-fail | console | 62+ | `reset: erasing <err> FAILED. Reinstall with Erase everything first.` |

- `RB-held` prints once, when the hold is first seen.
- `RB-pw-4` is true because `announce` sends nothing while the password
  is the default (announce.cpp:860), so a listed board goes quiet until
  the sysop sets a new one.
- `RB-pw-fail` is only true if a failed write leaves the old file. It
  should, going through syscfg's temp-file-and-rename.
- `RB-fr-fail` takes the partition's name in `<err>`. A half-erased
  userdata is a state nothing else in the firmware expects, so the line
  sends the sysop to the one repair that is certain.

### Docs paragraph: COMMANDS.md or README, "Resetting the board"

> The BOOT button can put a board right without a reflash. Press and let
> go of RESET, then press BOOT and hold it. What happens depends on how
> long you hold it, and happens when you let go:
>
> - under 7 seconds, nothing;
> - 7 to 15 seconds, the sysop password goes back to the published
>   default, `unleashed`, which works only from your own network until you
>   change it. Accounts, settings, mail and Wi-Fi are kept. While the
>   password is the default, the board keeps itself off the directory;
> - 15 to 20 seconds, a factory reset: the accounts, the settings, the
>   Wi-Fi, the mail kept on the board and the caller log are erased. The
>   screens, the firmware and the SD card are not touched;
> - 20 seconds or more, nothing: the reset is abandoned.
>
> The activity LED shows the stage while you hold: a slow blink under
> 7 seconds, fast flashing from 7, solid from 15, and off at 20. A board
> with no LED keeps the same timings, and the serial console says each
> stage as it arrives.
>
> **A factory reset also costs the board its directory listing.** The
> listing is tied to a token the directory gave the board, and the token
> is kept in the settings, so after the reset the directory has no way to
> know it is the same board. Switched back on, it is listed as a new one:
> on unleashedbbs.com that means three hours before it appears, as the
> first time, while the old entry shows as offline and is dropped after a
> week of silence. Restoring a backup taken before the reset brings the
> token back with the settings. Back within four days, the listing
> carries on where it was; within a week it keeps its entry but waits the
> three hours again. A backup never holds the staff passwords, so set the
> sysop password again straight after restoring.

### Replacement text: /install, "If something goes wrong, reset rather than reflash"

The section is gated `::: from 1.0.2` today; the plan gates it on 1.1.0.
Replace the "15 to 20 seconds" bullet, and put the warning box directly
above step 1 of the BOOT sequence, so it is read before anybody starts
counting:

> A factory reset also takes the board off this directory. The listing is
> tied to the board's settings, which the reset erases, so afterwards the
> directory sees a new board: three hours before it is listed again, like
> the first time. Restore a backup taken before the reset, within four
> days, and the listing carries on where it was.

```
- **15 to 20 seconds**, the LED stays on. Letting go is a factory reset:
  the accounts, the settings, the Wi-Fi, the mail and the logs are wiped,
  and the screens, the firmware and the SD card are kept. The board starts
  again like a fresh install, waiting for this page's Wi-Fi step, and it
  is no longer on the directory unless you restore a backup.
```

And one sentence for the end of the "7 to 15 seconds" bullet:

```
  Until you choose a new password, the board keeps itself off the
  directory.
```

(The five-line box above is a `> ` blockquote, which the site renders as
one warning box; it has no line that does not start with `> `, so it stays
one box.)

## 7. privacy.ans at 72 columns

`PRIVACY_PAGES` in `tools/mkscreens.py` stays exactly as it is for PETSCII
and plain ASCII. This is a second list for `make_privacy_ans()` only.

- Every word is the same and in the same order, the four pages are the
  same four, and each paragraph, heading, blank line and `Page n of 4`
  marker is where it was. Only the line breaks inside paragraphs moved.
  Checked by script against the file on disk, not by eye.
- Lines break at a sentence or a clause where one falls near the end of
  a line, rather than filling each line and leaving one word behind.
- Text is at most 71 characters; the builder's leading space makes 72.
- Page budget on a 24-row terminal: a page's lines, then the blank line
  and `Press SPACE to continue` that `pauseFor` prints, and the last page
  gets the same pause before the sign-up form. Pages use 21, 19, 15 and
  17 rows. The `.ans` file is never paged by the player, so the form
  feeds are the only breaks, and every page fits under them.
- "Four short pages" stays true: it is still four.

```python
# ANSI only: the same words as PRIVACY_PAGES, broken for 72 columns.
# make_privacy_ans() iterates this instead; PETSCII and ASCII keep
# PRIVACY_PAGES. Rows per page on 24 lines, with the pause: 21, 19, 15, 17.
PRIVACY_PAGES_ANSI = [
    [   ("h", "THE RISKS OF AN UNENCRYPTED BBS"),
        ("s", "and what it means for your privacy"),
        ("", ""),
        ("t", "Four short pages. They take a minute,"),
        ("t", "and they are the minute worth spending."),
        ("", ""),
        ("y", "TELNET IS NOT ENCRYPTED"),
        ("", ""),
        ("t", "Telnet has no encryption. It never has,"),
        ("t", "and on this board it never will."),
        ("", ""),
        ("t", "Everything you type crosses the network as readable text. What you say,"),
        ("t", "and the password you type to get in."),
        ("", ""),
        ("t", "That is not an oversight. It is the price of letting a 1982"),
        ("t", "computer call, and a C64 cannot do encryption."),
        ("t", "Better to tell you than quietly pretend."),
        ("", ""),
        ("d", "Page 1 of 4") ],

    [   ("y", "WHAT THAT ACTUALLY RISKS"),
        ("", ""),
        ("t", "Reading your password needs two things:"),
        ("t", "a sniffer, which is any program that records network traffic,"),
        ("t", "and a position on the path between you and this board."),
        ("", ""),
        ("t", "Who has that? Whoever runs the wifi you are on. Whoever runs the office"),
        ("t", "network. Your internet provider, and the board's. Somebody who has put"),
        ("t", "themselves in the middle on purpose."),
        ("", ""),
        ("t", "Not a stranger on the internet, then."),
        ("t", "It takes access, and most people simply do not have it."),
        ("", ""),
        ("t", "Low risk. Not no risk."),
        ("t", "Worth one unique password, not an afternoon of worry."),
        ("", ""),
        ("d", "Page 2 of 4") ],

    [   ("y", "YOUR PASSWORD ON THIS BOARD"),
        ("", ""),
        ("t", "It is never stored as you typed it. It is salted and hashed with"),
        ("t", "SHA-256, a thousand rounds, and only the result is written down."),
        ("t", "Nobody can read it back, including the sysop."),
        ("", ""),
        ("t", "That protects the file if the file is stolen."),
        ("t", "It does nothing for the wire, where you typed it in the clear."),
        ("", ""),
        ("t", "And a hash is not magic."),
        ("t", "A common password still falls to a lookup table."),
        ("", ""),
        ("d", "Page 3 of 4") ],

    [   ("y", "WHAT THIS BOARD KNOWS"),
        ("", ""),
        ("t", "The sysop sees your handle, the address you called from, when you"),
        ("t", "called and for how long, and the last command you ran. Staff can watch"),
        ("t", "a node. Messages you leave sit in a file until they are read."),
        ("", ""),
        ("t", "Assume whoever owns the machine can read what is on it."),
        ("t", "That is true everywhere. Here you at least know who they are."),
        ("", ""),
        ("t", "Honestly? It is a hobby board on a five dollar chip,"),
        ("t", "and it is conversation."),
        ("", ""),
        ("g", "Use a password you use nowhere else."),
        ("", ""),
        ("d", "Page 4 of 4") ],
]
```

For `screen-artist`: the colours per kind are `PRIV_ANSI` as today. The
one thing worth a look once it is drawn is page 1, the fullest at 21 rows:
on a terminal that reports 25 rows it has four to spare, on 24 it has
three.
