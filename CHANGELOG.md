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
 License:      GNU General Public License v2 or later
 SPDX-License-Identifier: GPL-2.0-or-later
 ===========================================================================
-->

# Changelog

Every released build of µnleashed BBS, newest first. Versions are `MAJOR.MINOR.PATCH`; while the core is being built each minor number is one flashed build. Dates are the day the work landed in the repository.

A build is only marked **on hardware** once it has run on a real ESP32-WROOM-32E with a caller connected. Everything else is host-tested through `tools/testclient.py`.

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
- Not on hardware. Improv needs one manual check by Rob with a real browser.
  The local-address refusal cannot be exercised by the host suite, which
  only ever connects from 127.0.0.1.

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
`reports/ux-message-boards.md` is 4,345 lines, it was commissioned for
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
in `reports/model-ab-2026-09.md`.

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

**Six room commands, approved from `reports/chat-commands-2026-09-22.md`,
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
`reports/chat-commands-2026-09-22.md` (six approved, three rejected, plus
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
- A board can advertise a name of its own (`quantum.dnsfor.me`, say) instead of whatever address the directory saw, and can list itself in several directories at once with a comma-separated list. Both were always in the protocol; neither was reachable.
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
