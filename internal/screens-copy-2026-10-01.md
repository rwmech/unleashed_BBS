# Screen copy deck, 2026-10-01

Words for every stock screen, for screen-artist to draw from. Written by
`explain` on Rob's brief of 2026-10-01: "including the language updates to
better match the website". Words only: layout, colour and art are the
artist's, and nothing here is code.

Base: `tools/mkscreens.py` on `rel-1.2.1f` at 08df38f (1.2.1-screens.2),
which already has the 40-column rules, newuser and chatin and the
five-dollar fix. Where this deck and that branch disagree, this deck is the
newer copy.

## How to read this deck

- Each screen has an 80-column block and a 40-column block. Indentation in
  the blocks is real: the 80 blocks use the generator's indents (2 for body
  and headings, 8 for a rule's hung text, 6 and a bullet for list items),
  and the 40 blocks are measured at 38 columns, one inside the 39 that
  SCREENS.md allows, with every token at its widest.
- `----` is a rule. `<FF>` is the player's page break. A folio
  (`Page 1 of 3`) goes where the current screen puts it.
- `*` in a block is the bullet the artist already draws (CP437 0xF9 on
  ANSI, the rail on PETSCII). Titles are written flush left; centre them as
  now.
- Widths were checked with `@USER@` at 20 characters (BBS_USER_MAX),
  `@NODE@` and `@NODES@` at 2, `@VER@` at 20 (`1.2.1 (ESPCAM 1.0.5)`),
  `@BBS@` at 13, `@DATE@` at 15 (`Thu 01 Oct 2026`) and `@TIME@` at 5.
  `@BOARD@` can be 40 characters (`boardName[41]`), so it stands on a line
  of its own wherever the layout allows.
- "Same words" means what `pet40_check` means: the 40 copy is the 80 copy
  re-broken, titles and folios aside, with one key named for the machine
  (`_` for ESC on PETSCII) and, here, one CONFIG row named for the width
  (`Closed` at 40, `Stop taking calls` at 80). Where a 40 copy is shorter,
  the screen says so and lists what is left out.
- One new @-code, Rob's: `@SECURE:ssh text|telnet text@`, which 1.2.2's
  firmware adds. It is in rules, newuser and privacy, measured at its
  longer variant. Every other code used is in SCREENS.md.

## Phrases the test suite reads

Change any of these and `tools/testclient.py` fails. The deck keeps all of
them.

| Screen | Phrase |
|---|---|
| welcome | `Connecting you` (and the board's name after "to") |
| busy | `lines are busy` |
| goodbye | `Stay unleashed` |
| rules | `HOUSE RULES`, `NO HATE` |
| newuser | `YOU ARE ON THE BOARD`, `No hate` |
| chatin | `ENTERING CHAT` |
| privacy | `THE RISKS OF AN UNENCRYPTED BBS`, `TELNET IS NOT ENCRYPTED`, `use nowhere else`, `Page 1 of 4`, `Page 4 of 4` |
| setup | `YOU ARE THE SYSOP`, `straight over`, and never the word `Backspace` |
| closed (built in) | `Closed by the sysop` |
| copyright, 40 | `Robert Mech  GPLv3+` (two spaces) |

Three test phrases change with Rob's direction, so the tests move with
the copy (firmware lane, not this deck):

| Where | Test reads today | Becomes |
|---|---|---|
| welcome, PETSCII (testclient ~1088) | `No web. No cloud.` | `The next-generation BBS software` |
| connection line (~1051, 2861, 11473, 17924-17927, 18065, 18253) | `--> Connection via Telnet is not secure`, `Connection via SSH is Secure.`, `33;1mSecure.` | `--> This connection is not securely encrypted`, `--> This connection is securely encrypted` |
| sign-up warning (~12896, 17243) | `not encrypted` | `not securely encrypted` (`anywhere else` stays) |

## What changed in the language, and why

The site's voice is the target: plain, warm, about your own community, and
honest about telnet in the way the site and `/docs/privacy` are. Six
changes run through the whole deck.

- **Telnet, not "nothing here", and the screen knows which.** The old
  copy said "Nothing here is encrypted" in the rules and newuser. Since
  1.1.2 every ESP32-S3 board takes SSH, and over SSH that sentence is
  false. With Rob's `@SECURE` code (1.2.2) the screens say which one this
  caller is on: "This connection is securely encrypted" or "is not
  securely encrypted", the same words as the connection line and the
  sign-up warning (section "The connection line and the sign-up warning").
- **Transparency, not fear** (Rob, 2026-10-01). No screen says the sysop
  sees everything or that staff watch a node. Lines can be monitored, the
  sysop's included, and in practice rarely are: said once, with the help
  desk as the model, and not repeated on every screen.
- **Rob's framing for the risk**, the one `/docs/privacy` uses: open
  communication is radio; listening takes somebody on the path, with tools,
  on purpose; it is a conversation in a cafe, not a website that records
  everything; so say what you would say in public and use a password you use
  nowhere else. The privacy screen is rebuilt on that order.
- **No prices on any screen.** A screen sits on a board for years and
  prices move. "Five dollar" is gone already on rel-1.2.1f; this deck also
  drops "less memory than a floppy disk", which is false for an S3 with
  8 MB of PSRAM. The board is "about the size of a stick of gum", the
  site's own phrase.
- **No machine is the example.** "A C64 cannot do encryption" becomes
  "plenty of the machines that call here cannot encrypt at all".
- **The board's name, not the software's.** busy.ans and goodbye.seq said
  `@BBS@` where their siblings say `@BOARD@`. A caller dialled the board.

---

## welcome

**For:** the first screen a caller sees, after detection and the
connection line. Art, never paged, one screen. It sets the tone and then
hands over to the handle prompt.

**Shown:** every call, before login.

**Changes:**
- The tagline is the site's "The next-generation BBS software" (Rob's
  pick, 2026-10-01), the same at both widths.
- "@BOARD@ running @BBS@" is split into two lines. With no board name,
  `@BOARD@` falls back to the software's name, and the old line read
  "µnleashed BBS running µnleashed BBS v1.1.1" (CLAUDE.md, 1.1.2 list).

80 columns (under the wordmark):

```80 welcome
  E L E C T R O N I C   F R E E D O M
  The next-generation BBS software
  ----
    * Node @NODE@ of @NODES@      * Terminal @TERM@      * @DATE@ @TIME@
    * @BOARD@
      @BBS@ v@VER@, a BBS on a microcontroller
  ----
  (C) 2026 Robert Mech. Free software under the GPL, v3 or later.

Connecting you to @BOARD@
```

40 columns (under the 3x5 logo):

```40 welcome
 E L E C T R O N I C   F R E E D O M
 The next-generation BBS software
----
 Node @NODE@ of @NODES@   @TERM@
 @DATE@ @TIME@
 @BOARD@
 @BBS@ v@VER@,
 a BBS on a microcontroller
 (C) 2026 Robert Mech  GPLv3+
----

Connecting you to @BOARD@
```

**40 is shorter:** the label "Terminal" is left out, and the copyright is
the short form the test expects.

**Words:** 53 at 80, 45 at 40, about what the screen carried before. Cut for slow lines:
nothing more to cut. The 300 baud line "Connecting you to @BOARD@" is
Rob's and stays, and it is the only paced line.

**For the artist:** a board name over 21 characters makes "Connecting you
to @BOARD@" wrap at 40. Rob keeps names up to 40 characters and accepts
the wrap (2026-10-01).

---

## busy

**For:** the caller past the last line. They get this, a 10 second
countdown, and the drop. A key during the countdown lets the sysop's
account log in, which the firmware handles.

**Shown:** when every caller line is in use.

**Change:** the box's title is `@BOARD@`, not `@BBS@`. The words are
otherwise right and short: keep them.

```80 busy
  @BOARD@
  ----
  Sorry, all @NODES@ lines are busy.
  Every node is unleashed right now.
  Please try your call again soon.
```

```40 busy
 @BOARD@
----
 Sorry, all @NODES@ lines are busy.
 Every node is unleashed right now.
 Please try your call again soon.
```

**Words:** 19 (unchanged).

**For the artist, two existing faults in busy.ans:**
- The right border is padded for "Sorry, all 6 lines are busy.", but
  `@NODES@` now prints 10, so the border is one column out.
- A board name of any length cannot sit inside a fixed box. Put `@BOARD@`
  above the box, or drop the right border on that row.

---

## closed

**For:** the sign a caller gets while the board is closed (CONFIG board,
"Stop taking calls"). It is the busy line's machinery with its own words.

**Shown:** to every caller of a closed board. A fresh board is closed
until its sysop opens it.

**Today no stock file ships.** The firmware prints its own four lines
(bbs.cpp, `startBusy`), and a sysop may draw `screens/closed.*`. If a
closed screen is shipped, it replaces the firmware's lines completely,
the last one included, so it has to carry them all. My recommendation is
**not to ship one**: the built-in words are already right. If Rob wants
one anyway, these are the words, the same at both widths:

```80 closed
  @BOARD@
  Closed by the sysop for now.
  Please try your call later.
  The sysop's account: any key logs in.
```

```40 closed
 @BOARD@
 Closed by the sysop for now.
 Please try your call later.
 The sysop's account: any key logs in.
```

**Words:** 19.

**Hand-back (code, not copy):** the built-in sign prints the software's
name (`BBS_NAME`), not the board's. It should print the board's name,
as `@BOARD@` does.

---

## goodbye

**For:** the send-off, however a call ends: BYE, idle, out of time,
kicked or banned. It has 20 seconds from its first byte, and the line is
held 5 seconds after it.

**Shown:** at every logoff by a logged-in caller.

**Change:** goodbye.seq says `@BBS@` where the others say `@BOARD@`. At 40
the board's name goes on a line of its own so a long one does not wrap.

```80 goodbye
----
Stay unleashed, @USER@.
@BOARD@ node @NODE@ is free again.
@DATE@ @TIME@
(C) 2026 Robert Mech. Free software under the GPL, v3 or later.
```

```40 goodbye
----
Stay unleashed, @USER@.
@BOARD@
node @NODE@ is free again.
@DATE@ @TIME@
(C) 2026 Robert Mech  GPLv3+
```

**Words:** 23 at 80, 16 at 40 (16 before). The only difference is the copyright,
which is the short form at 40 as everywhere. Nothing to cut: it is already
the shortest screen a caller sees.

---

## about

**For:** the `ABOUT` command. What this software is, who wrote it, its
licence, where the source is, and the pointer to HARDWARE. Plain text
on purpose, so a sysop can rewrite it.

**Shown:** on `ABOUT`.

**Changes:**
- "A telnet BBS on a bare ESP32" was true of 1.0. It now runs on ESP32 and
  ESP32-S3 boards, the S3s take SSH too, and the project's core value is
  the microcontroller. It now says "a BBS on a microcontroller".
- One line in the site's voice says what it is for.
- "see the repo" names the repo, which is public:
  github.com/rwmech/unleashed_BBS. I checked that it is reachable on
  2026-10-01.
- "Build your own" points at the site.
- The credits list ("ESP-IDF, FreeRTOS, lwIP and littlefs") has grown
  since: S3 images carry wolfSSH, the camera boards carry esp32-camera,
  and everything carries mbedTLS. It now says "ESP-IDF and other free
  software", which stays true whatever a board links. ESP-IDF includes
  FreeRTOS and lwIP.
- The lifetime-member credits block (CLAUDE.md, supporters) is not here:
  there is nobody to credit yet.

```80 about
  ----
  @BBS@ v@VER@, a BBS on a microcontroller
  ----
  Node @NODE@ of @NODES@, you are @USER@.

  A BBS for your own community, on a board that fits in your hand.
  No ads, no feed, nobody in the middle.

  (C) 2026 Robert Mech
  Free software, GNU GPL v3 or later.
  Code: github.com/rwmech/unleashed_BBS
  Build your own: unleashedbbs.com

  Built on ESP-IDF and other free software. Their notices ship with it.

  HARDWARE shows what this board is running on.
  ----
```

```40 about
----
 @BBS@ v@VER@,
 a BBS on a microcontroller
----
 Node @NODE@ of @NODES@, you are
 @USER@.

 A BBS for your own community, on a
 board that fits in your hand. No ads,
 no feed, nobody in the middle.

 (C) 2026 Robert Mech
 Free software, GNU GPL v3 or later.
 Code: github.com/rwmech/unleashed_BBS
 Build your own: unleashedbbs.com

 Built on ESP-IDF and other free
 software. Their notices ship with it.

 HARDWARE shows what this board is
 running on.
----
```

**Words:** 73 (was 54). The new words are the purpose line, the repo
address and the site. The 40 copy is 22 rows, one screen.

---

## motd

**For:** the sysop's own message after login: news, the meeting night,
whatever is on.

**Shown:** after every login, if the board has one. A new account gets
`newuser` instead.

**No stock copy, deliberately.** CLAUDE.md: "No board ships a motd", and
the reason is good: a stock motd is the same paragraph on every board,
and every caller would pay for it at every login on a 1200 baud line. The
information pages (`INFO`, and the rotating /i pages) are where a board's
own news goes. If the docs want an example to copy, this one is short
and uses only real codes:

```80 motd
@BOARD@, @DATE@
Club night is Tuesday at 19:00. New files are in the library.
```

```40 motd
@BOARD@, @DATE@
Club night is Tuesday at 19:00. New
files are in the library.
```

**Words:** 14. Example only, not for `data/screens/`.

---

## rules

**For:** the terms of the place, before anybody types a password. Four
rules now, in Rob's order: respect, privacy, the hardware, and chaotic
neutral. The connection warning and the offer of the privacy screen follow
it, immediately in front of the password.

**Shown:** when a caller presses R to register. Two pages at 80, three
at 40.

**Rob's direction (2026-10-01), and how the copy follows it:**
- **Rule 1 is respect.** Argue all you like; harassment, bullying and
  being a jerk get you banned; keep it constructive. The heading keeps
  "NO HATE" because the test reads it and it still says the same thing.
  **I chose "jerk", not the word Rob used.** These are stock rules on every
  board, and the site has a /kids page and a /teachers page that send
  classrooms to build one. A school board should not have to edit its house
  rules before the first lesson, and a sysop who wants the stronger word can
  type it in.
- **"The sysop sees everything" is gone.** Rules 2, 3 and the old 4 are one
  rule, "UNDERSTAND THE PRIVACY". It says what is true without the big
  brother tone: SSH gets you to the board securely and telnet does not, a
  line can be monitored, the sysop's included, and in practice it rarely
  is. The support model is there by analogy ("the way a help desk can"),
  not spelled out. "Use a password you use nowhere else" stays inside it,
  still the one that matters.
- **The rule knows the connection.** Its first line is
  `THIS CONNECTION @SECURE:IS|IS NOT@ SECURELY ENCRYPTED.`, using the new
  screen code 1.2.2 adds. The form is `@SECURE:ssh text|telnet text@`.
  Only the two words that differ sit inside the code, so the code stays
  short (see the hand-backs: the player's token buffer is 16 bytes today),
  and the line breaks at 40 outside it. The two variants a caller sees are
  below the blocks.
- **Rule 3 is "IT RUNS ON A MICROCONTROLLER":** an ESP32 or ESP32-S3 that
  fits in your hand, performance first, the odd hiccup, and where to report
  one.
- **The issues address:** `https://github.com/rwmech/unleashed_BBS/issues`
  is 46 characters and does not fit 40 columns. Without the scheme,
  `github.com/rwmech/unleashed_BBS/issues` is 38: it fits a 39-column line
  only flush left, not inside the rule's four-column hang (42). So at 40
  it stands on its own line at column 1, and at 80 it sits in the hang.
  Terminal users type an address rather than click it, and GitHub takes
  it without the scheme. The bare host would also fit, but it lands on the
  code page, not the form, and a caller with a bug wants the form.
- **"Chaotic neutral" stays** as rule 4, now "Past all that, do as you
  like." "Get along" moved into rule 1's "play nice".

```80 rules
  THE HOUSE RULES
  ----
  You are about to make an account. This is the whole deal, and it is
  shorter than the thing you clicked through this morning.

  1.  RESPECT. NO HATE.
        Argue with anybody about anything, as hard as you like. Harassment,
        bullying and being a jerk will get you banned. Keep it constructive,
        and play nice with others.

  2.  UNDERSTAND THE PRIVACY.
        THIS CONNECTION @SECURE:IS|IS NOT@ SECURELY ENCRYPTED.
        SSH carries what you type to the board securely. Telnet does not:
        it goes as plain text, like radio, and someone on the path could
        listen in if they set out to. A sysop can look in on a line as well,
        the way a help desk can, but in practice it rarely happens.

        Use a password you use nowhere else. That is still the one that
        matters. Make one up. It does not have to be clever. It has to be
        new.
<FF>
  THE HOUSE RULES
  ----
  3.  IT RUNS ON A MICROCONTROLLER.
        The whole BBS is one ESP32 or ESP32-S3, on a board that fits in
        your hand. Performance is our number one goal, but now and then it
        may hiccup. If it drops you, call back. If it keeps happening, file
        a bug report at:
        github.com/rwmech/unleashed_BBS/issues

  4.  CHAOTIC NEUTRAL.
        Past all that, do as you like.

  ----
  Still here? Good. Pick a handle and a password nobody else has ever
  seen, and welcome aboard.
```

```40 rules
THE HOUSE RULES            Page 1 of 3
----
You are about to make an account.
This is the whole deal, and it is
shorter than the thing you clicked
through this morning.

1.  RESPECT. NO HATE.
    Argue with anybody about
    anything, as hard as you like.
    Harassment, bullying and being a
    jerk will get you banned. Keep it
    constructive, and play nice with
    others.
<FF>
THE HOUSE RULES            Page 2 of 3
----
2.  UNDERSTAND THE PRIVACY.
    THIS CONNECTION @SECURE:IS|IS NOT@
    SECURELY ENCRYPTED.
    SSH carries what you type to the
    board securely. Telnet does not:
    it goes as plain text, like radio,
    and someone on the path could
    listen in if they set out to. A
    sysop can look in on a line as
    well, the way a help desk can, but
    in practice it rarely happens.

    Use a password you use nowhere
    else. That is still the one that
    matters. Make one up. It does not
    have to be clever. It has to be
    new.
<FF>
THE HOUSE RULES            Page 3 of 3
----
3.  IT RUNS ON A MICROCONTROLLER.
    The whole BBS is one ESP32 or
    ESP32-S3, on a board that fits in
    your hand. Performance is our
    number one goal, but now and then
    it may hiccup. If it drops you,
    call back. If it keeps happening,
    file a bug report at:
github.com/rwmech/unleashed_BBS/issues

4.  CHAOTIC NEUTRAL.
    Past all that, do as you like.

----
Still here? Good. Pick a handle and a
password nobody else has ever seen,
and welcome aboard.
```

What each caller reads on rule 2's first line:

| Connection | Line |
|---|---|
| telnet | `THIS CONNECTION IS NOT SECURELY ENCRYPTED.` |
| SSH | `THIS CONNECTION IS SECURELY ENCRYPTED.` |

The rest of the rule is the same for both. An SSH caller still reads why
telnet is different, which matters because their next call may come in
over telnet.

**Same words** at both widths. Pages at 40: 14, 19 and 19 rows; at 80,
20 and 15. The budget is 22.

**Words:** 227, titles aside (was 252 for six rules on rel-1.2.1f). Cut
for slow lines: the old rule 4 is gone, rules 2 and 3 are one, and
"Chaotic neutral" is one sentence.

---

## newuser

**For:** the short version of the rules once somebody is actually in,
and the three things worth typing first.

**Shown:** once, in place of the motd, right after a registration
succeeds. Followed by "[H]ELP for commands." and the prompt.

**Changes, to match the revised rules:**
- The first bullet is rule 1: "No hate. Respect everyone and play nice."
  ("No hate" is a test anchor and stays.)
- The second is rule 2's line, connection-aware with `@SECURE`.
- The third is the password, on its own: it is still the one that matters.
- "Chat and mail are not private. Staff can watch a node." is gone, for
  the same reason the old rule 4 went. PRIVACY, a line below, is there for
  anybody who wants the whole story.

```80 newuser
  YOU ARE ON THE BOARD
  ----
  Welcome aboard, @USER@. Node @NODE@ of @NODES@ is yours.

  The short version, now that you have joined:

      * No hate. Respect everyone and play nice.
      * This connection @SECURE:is|is not@ securely encrypted.
      * Use a password you use nowhere else.
      * Be patient. It is a microcontroller, not a data centre.

  ----
  ?         the command list
  PRIVACY   the long version of rule two, any time you like
  CHAT      find out whether anyone else is awake
```

```40 newuser
YOU ARE ON THE BOARD
----
Welcome aboard, @USER@.
Node @NODE@ of @NODES@ is yours.

The short version, now that you
have joined:
* No hate. Respect everyone and play
  nice.
* This connection @SECURE:is|is not@
  securely encrypted.
* Use a password you use nowhere else.
* Be patient. It is a microcontroller,
  not a data centre.
----
?        the command list
PRIVACY  the long version of rule two,
         any time you like
CHAT     find out whether anyone else
         is awake
```

The `@SECURE` text is lower case here, so the code must keep its
argument's case (hand-backs).

**Same words** at both widths.

**Words:** 70 (was 79). 40 is 20 rows, against a budget of 21.

---

## chatin

**For:** the step into the chat room: what the room can see, and the five
commands a newcomer needs. The room's own lines (who is here, what was
just said) follow it, so it stays short enough that a quiet room still
shows all of it.

**Shown:** on joining the room, and again on `/welcome`.

**Changes:**
- The old copy said `/p` "crosses the wire in the clear and the sysop has
  the log". No chat log exists: the room keeps its last lines in a ring in
  memory (`history`, in CONFIG chat), and nothing of the room is written to
  disk.
- The first revision said "staff can watch any node", which is the tone
  Rob ruled out on 2026-10-01. A `/p` line is "quieter, though not a
  sealed letter": honest that it is not private, without anyone watching.

```80 chatin
  ENTERING CHAT
  ----
  Everyone in the room sees what you type. /p sends a line to one person,
  which is quieter, though not a sealed letter. The room remembers its last
  lines in memory, and nothing more.

      * /s        who else is here
      * /p n      a line to one person
      * /welcome  read this again
      * /help     the rest of the commands
      * /q        leave, or press ESC

  ----
```

```40 chatin
ENTERING CHAT
----
Everyone in the room sees what you
type. /p sends a line to one person,
which is quieter, though not a sealed
letter. The room remembers its last
lines in memory, and nothing more.

/s        who else is here
/p n      a line to one person
/welcome  read this again
/help     the rest of the commands
/q        leave, or press _
----
```

**Same words** at both widths. On PETSCII, `_` is shown as the left
arrow, the Commodore's ESC, as on rel-1.2.1f.

**Words:** 61 (was 70). 40 is 14 rows, against a budget of 16.

---

## privacy

**For:** the honest explanation of telnet, read by somebody who is about
to choose a password. It is the screen this whole project is most careful
about. Rebuilt on Rob's order, the same one `/docs/privacy` uses: what it
is, what it would take, the comparison both ways, what to do.

**Shown:** at sign-up, when the caller answers Y to "Would you like to know
more?", and any time on `PRIVACY`. Four pages, numbered.

**Changes:**
- Page 1 drops "a C64 cannot do encryption", and its last line is the
  same connection-aware `@SECURE` line as rule 2, so an SSH caller reading
  the telnet explainer is told their own line is encrypted.
- Page 2 opens with radio, and ends with "being able to listen is not
  listening".
- Page 3 is the cafe, then the website for contrast, then what this board
  knows, in the tone Rob set for the rules: the board logs calls, a sysop
  can look in on a line the way a help desk can, and in practice it rarely
  happens. "Mail waits in a file" and "your last command" are cut. "Low risk, not no risk" is now its heading, so the "not zero" half
  cannot be skimmed past.
- Page 4 is the password, what to do, and the long version on the web. "A
  hash is not magic" is folded into one clause ("unless the password is a
  common one") so the page fits.
- "Hobby board on a five dollar chip" (and rel-1.2.1f's "cheap hobby
  board") is gone. Page 3 makes the same point by comparison, without a
  price.

```80 privacy
  THE RISKS OF AN UNENCRYPTED BBS
  and what it means for your privacy
  ----
  Four short pages, and the minute they take is worth spending.

  TELNET IS NOT ENCRYPTED
  Telnet has no encryption, and never will. Everything you type over it
  crosses the network as plain text: what you say, and your password.

  That is the price of letting a 1982 computer call in. Plenty of machines
  that call here cannot encrypt at all.

  Boards on an ESP32-S3 also take SSH, which is encrypted.
  This connection @SECURE:is|is not@ securely encrypted.

                                                              Page 1 of 4
<FF>
  WHAT IT WOULD TAKE
  ----
  Think of radio. You transmit, and anyone tuned to the channel hears you.

  Tuning in to telnet takes two things: a sniffer, which is a program that
  records network traffic, and a place on the path between you and the
  board.

  Who has that? Whoever runs the Wi-Fi you are on, or the office network.
  Your internet provider, and the board's. Somebody who has put themselves
  in the middle on purpose.

  Being able to listen is not listening. It takes a person, with tools,
  choosing to.

                                                              Page 2 of 4
<FF>
  LOW RISK, NOT NO RISK
  ----
  Think of a cafe. The next table could hear you if they tried. Mostly
  nobody does, and you still would not read out your bank details.

  A website records what you do and passes it on, by design. This board has
  no ads and no trackers, and what you write stays on it.

  WHAT THIS BOARD KNOWS
  The board logs each call: your handle, where you called from, when, and
  for how long. A sysop can look in on a line, the way a help desk can,
  though in practice it rarely happens. Here you know whose machine it is.

                                                              Page 3 of 4
<FF>
  YOUR PASSWORD ON THIS BOARD
  ----
  It is never stored as you typed it. It is salted and hashed with SHA-256,
  a thousand rounds, and only the result is written down. Nobody can read it
  back, the sysop included.

  That protects the file if it is stolen, unless the password is a common
  one. It does nothing for the wire, where you typed it.

  WHAT TO DO
  Say what you would say in public.
  Use a password you use nowhere else.

  The long version, with the details:
  unleashedbbs.net/docs/privacy

                                                              Page 4 of 4
```

```40 privacy
THE RISKS OF AN UNENCRYPTED BBS
and what it means for your privacy

Four short pages, and the minute they
take is worth spending.

TELNET IS NOT ENCRYPTED
Telnet has no encryption, and never
will. Everything you type over it
crosses the network as plain text:
what you say, and your password.

That is the price of letting a 1982
computer call in. Plenty of machines
that call here cannot encrypt at all.

Boards on an ESP32-S3 also take SSH,
which is encrypted. This connection
@SECURE:is|is not@ securely encrypted.

Page 1 of 4
<FF>
WHAT IT WOULD TAKE

Think of radio. You transmit, and
anyone tuned to the channel hears you.

Tuning in to telnet takes two things:
a sniffer, which is a program that
records network traffic, and a place
on the path between you and the board.

Who has that? Whoever runs the Wi-Fi
you are on, or the office network.
Your internet provider, and the
board's. Somebody who has put
themselves in the middle on purpose.

Being able to listen is not
listening. It takes a person, with
tools, choosing to.

Page 2 of 4
<FF>
LOW RISK, NOT NO RISK

Think of a cafe. The next table could
hear you if they tried. Mostly nobody
does, and you still would not read
out your bank details.

A website records what you do and
passes it on, by design. This board
has no ads and no trackers, and what
you write stays on it.

WHAT THIS BOARD KNOWS
The board logs each call: your
handle, where you called from, when,
and for how long. A sysop can look in
on a line, the way a help desk can,
though in practice it rarely happens.
Here you know whose machine it is.

Page 3 of 4
<FF>
YOUR PASSWORD ON THIS BOARD

It is never stored as you typed it.
It is salted and hashed with SHA-256,
a thousand rounds, and only the result
is written down. Nobody can read it
back, the sysop included.

That protects the file if it is
stolen, unless the password is a
common one. It does nothing for the
wire, where you typed it.

WHAT TO DO
Say what you would say in public.
Use a password you use nowhere else.

The long version, with the details:
unleashedbbs.net/docs/privacy

Page 4 of 4
```

**Same words** at both widths, paragraph for paragraph, which is what
`privacy_check_ansi` holds the two lists to. Every page at 40 is 21 rows,
the same as the longest page now. The three colours as now: yellow headings, green on
the line the screen exists for ("Use a password you use nowhere else.").
"Say what you would say in public." may share the green. That is the
artist's call.

**Words:** 371 (was about 350): longer by the radio and the cafe, which
is what Rob asked this screen to say.

**Checked against the source:** salted SHA-256, 1,000 rounds (CLAUDE.md,
users; unchanged). The connection line is printed to every caller before
the welcome (`Bbs::linkLine`, bbs.cpp). "No ads and no trackers, and what
you write stays on it" is the site's own claim on /different. Announce
never sends who is on. `unleashedbbs.net/docs/privacy` is the page the
site links for "what that means in practice".

---

## files

**For:** the door into the file library: a lintel over the area menu,
which is drawn underneath it. Budget eight rows at 80 and seven at 40.

**Shown:** on entering FILES.

**Words:** the mark "FILES", the line beside it, and the date.

```80 files
FILES   the file library
        @DATE@  @TIME@
```

```40 files
FILES   file library
        @DATE@
```

**40 is shorter**, as now: "the" and the time do not fit beside the mark
at 40. No change to the words. "File library" is the site's own phrase
("a shared file library").

---

## forums (new, if Rob wants it)

**For:** the same kind of door, into the forums. The forums plugin already
plays `screens/forums.*` if a board has one (forums.cpp), and no stock one
ships, so a caller gets the bare forum list. A lintel to match FILES would
make the two subsystems read as one family.

```80 forums
FORUMS   the message boards
         @DATE@  @TIME@
```

```40 forums
FORUMS   message boards
         @DATE@
```

**For the artist:** neither face in mkscreens.py has O, R, U or M yet. The
6x10 caps have U but not O, R or M. The 3x5 face has none of the four.

---

## codes

**For:** the reference for the colour and effect codes a caller can put
in a forum post, mail or a chat line.

**Shown:** on `CODES` at the prompt and `/codes` in the room.

**Change, one line at each width.** The colour note said "Except on a
Commodore, ORANGE and BROWN look alike", which makes one machine the
reference. The fact underneath is about ANSI: kAnsiColor gives ORANGE
and BROWN the same SGR (0;33), and GREY and LTGREY the same (0;37). It
now says that directly. Everything else on this screen is
`internal/codes-screen-copy-2026-09-22.md`, measured there, and stays.

80 columns, the one line that changes (it replaces "Except on a Commodore,
ORANGE looks like BROWN and GREY like LTGREY."):

```80 codes-line
         On ANSI terminals, ORANGE looks like BROWN and GREY like LTGREY.
```

40 columns, page 3, the paragraph that changes (same four rows):

```40 codes-line
A plain text terminal drops the colour
and keeps the words. On ANSI, ORANGE
and BROWN look alike, and so do GREY
and LTGREY.
```

**80 is shorter, by design** (unchanged): it is the 40 copy condensed onto
one page, which is how the codes copy was written.

---

## setup

**For:** the first-boot screen, read by whoever just became the sysop:
the passwords form is next, why it matters, and that the board stays
closed until they open it.

**Shown:** once, on a board still on the published default password, to
a local caller who gave it. Before CONFIG staff opens.

**Changes:** the words are already right and say what the form actually
does. Two small changes only: the 80 opening is two sentences instead of
two fragments, and the closed row is named the way each width shows it.

```80 setup
  YOU ARE THE SYSOP
  ----
  Welcome, @USER@. This board is yours to run: a real BBS on a
  chip, where this used to take a whole PC and a bank of modems.

  FIRST, THE PASSWORDS
  You got in with the default password, and anybody can read that one on
  the install page. So the first job is your own: the next screen is the
  staff passwords form.

      * Up and Down move between fields. F1 saves. ESC leaves without saving.
      * The Sysop row's stars are the published default: change it now. Type
        your own straight over them, no need to delete them first.
      * Co-sysop 1 and 2: leave them blank unless you want helpers. A blank
        level is one nobody can use.

  CLOSED UNTIL YOU OPEN IT
  Callers get a closed sign until you open the board: CONFIG board, then
  Stop taking calls, set to no. Do that after the passwords and settings.

  WHAT HAPPENS NEXT
  The passwords form, then a short tour of the rest of the settings.
```

```40 setup
YOU ARE THE SYSOP
----
Welcome, @USER@.
This board is yours to run: a real BBS
on a chip, no PC, no bank of modems.

FIRST, THE PASSWORDS
You got in with the default password,
and anybody can read that one on the
install page. So the first job is
yours: the staff passwords form, next.
 CRSR up and down move between fields.
 F1 saves, _ leaves without saving.
 Sysop's stars: the published default.
 Type your own straight over them.
 Co-sysop 1 and 2: blank unless you
 want helpers. Blank means nobody can
 use that level.

Callers see a closed sign until you
open it: CONFIG board, Closed, no.
Next: the form, then a short tour.
```

Plain ASCII (`.asc`) swaps the six indented form lines for its own, as
now:

```40 setup-asc-form
 One question a line: type the new
 password and press Enter. The sysop
 one is the published default now:
 change it. Answer Y at the end.
 Co-sysop 1 and 2: blank unless you
 want helpers. Blank means nobody can
 use that level.
```

**40 is shorter, by design, and the form lines differ per terminal.** A
Commodore has cursor keys, F1 and the left arrow, and a plain terminal is
asked one question a line, so each gets its own instructions. Setup is one
page because the form is next. At 40 that page holds 22 rows, so the 40
copy drops the headings "CLOSED UNTIL YOU OPEN IT" and "WHAT HAPPENS NEXT"
and says each in one line.

**Words:** 163 at 80 (was 162), 111 at 40 (was 112).

---

## newsysop

**For:** the short tour of CONFIG after the passwords form. It names every
page and the few staff commands worth knowing, and says what to do last.

**Shown:** once, in the first-boot flow, after CONFIG staff is saved.

**Changes:** the page list was out of date. Checked against `kPages` on
rel-1.2.1f:
- `wifi` is `network` now ("wifi" still opens it). The page also has the
  port callers dial, and SSH's port on the S3s.
- `board` gained silent hours and the sysop handle.
- `photos` exists.
- The plugin list grew: lights, the link and doors, and the panel and
  camera on boards that have them. It is now "such as", so it doesn't go
  stale again.
- A short SATS paragraph is added, in the site's word.
- The guide link moves to where the guide is: `unleashedbbs.net/docs/setup`,
  checked live on 2026-10-01. The old copy said "unleashedbbs.com".

```80 newsysop
  SETTING UP YOUR BOARD
  ----
  CONFIG on its own lists the pages. CONFIG and a page name opens one, and
  CONFIG board is the first. F1 saves a page; most changes apply at once.

  THE PAGES
  board     its name, hostname, timezone, idle minutes, LED, where callers
            land, silent hours, and whether it takes calls
  limits    minutes per call and per day, the WHO refresh, the account cap
  accounts  sign-ups, and whether guests may call and for how long
  backup    the backup window: its port, how long it stays open, the button
  staff     the passwords you just set
  network   Wi-Fi and the port callers dial, used from the next restart
  photos    snapshot limits and how long photos are kept

  And a page for each plugin, such as chat, forums, files, info and sd,
  each saying if it is on and who may use it.

                                                              Page 1 of 2
<FF>
  THE CARD
  File areas and the forums live on an SD card, so neither exists without
  one. A board meant to stay up should have a card. CONFIG sd sets it up.

  SATS
  A sat is a small board paired with this one over the air, such as a
  camera sat. SATS lists them, CONFIG sats pairs them.

  OPENING UP
  Callers get a closed sign until CONFIG board sets Stop taking calls to
  no. CONFIG announce lists the board in the public directory, once you
  switch it on. Port forwarding comes last.

  STAFF COMMANDS
  SYS         the board's health
  DASH        the live dashboard
  USERS       the accounts
  HELP STAFF  the staff commands
  HELP SYSOP  the sysop's own
  The guide: unleashedbbs.net/docs/setup
  Welcome aboard.
                                                              Page 2 of 2
```

```40 newsysop
SETTING UP YOUR BOARD
----
CONFIG on its own lists the pages.
CONFIG and a page name opens one, and
CONFIG board is the first. F1 saves a
page; most changes apply at once.

THE PAGES
board
  its name, hostname, timezone, idle
  minutes, LED, where callers land,
  silent hours, and whether it takes
  calls
limits
  minutes per call and per day, the
  WHO refresh, the account cap
accounts
  sign-ups, and whether guests may
  call and for how long
Page 1 of 3
<FF>
backup
  the backup window: its port, how
  long it stays open, the button
staff
  the passwords you just set
network
  Wi-Fi and the port callers dial,
  used from the next restart
photos
  snapshot limits and how long photos
  are kept

And a page for each plugin, such as
chat, forums, files, info and sd, each
saying if it is on and who may use it.

THE CARD
File areas and the forums live on an
SD card, so neither exists without
one. A board meant to stay up should
have a card. CONFIG sd sets it up.
Page 2 of 3
<FF>
SATS
A sat is a small board paired with
this one over the air, such as a
camera sat. SATS lists them, CONFIG
sats pairs them.

OPENING UP
Callers get a closed sign until
CONFIG board sets Closed to no.
CONFIG announce lists the board in the
public directory, once you switch it
on. Port forwarding comes last.

STAFF COMMANDS
SYS         the board's health
DASH        the live dashboard
USERS       the accounts
HELP STAFF  the staff commands
HELP SYSOP  the sysop's own
The guide: unleashedbbs.net/docs/setup
Welcome aboard.
Page 3 of 3
```

**Same words** at both widths. The one exception is the closed row,
named as each width shows it: `Stop taking calls` at 80, `Closed` at 40.
Pages at 40: 20, 22 and 22 rows; at 80, 19 and 22, the second page
without its title, as at 40. As now, commands and page names go in the
key colour.

**Words:** 258 (was 259), with SATS and the photos page now in.

---

## The connection line and the sign-up warning (firmware strings)

Not screen files: these are printed by the firmware, and Rob's wording for
them is fixed. CLAUDE.md puts both in 1.2.1 (lane rel-1.2.1e), ahead of the
screens. The copy:

**The connection line** (`Bbs::linkLine`), after detection and before the
welcome, on every call, the busy line and the closed sign included:

| | 80 columns | 40 columns |
|---|---|---|
| telnet | `--> This connection is not securely encrypted` | `--> This connection is not securely`<br>`    encrypted` |
| SSH | `--> This connection is securely encrypted` | `--> This connection is securely`<br>`    encrypted` |

Rob's words are 45 and 41 columns, and the old line was held to 39 so a
40-column screen never wrapped it. At 40 it breaks before "encrypted",
with the continuation under the text, not under the arrow. No words are
cut. Colour as now: the telnet line's "not securely encrypted" in light
red, the SSH line's "securely encrypted" in bold yellow, where "Secure."
was.

**The sign-up warning** (`Bbs::askKnowMore`), after the rules and
immediately before the password. It must follow the connection, as the
line above does:

```80 signup
--> This connection is not securely encrypted
Use a password you do not use anywhere else.
Would you like to know more? [Y/N]
```

```40 signup
--> This connection is not securely
    encrypted
Use a password you use nowhere else.
Would you like to know more? [Y/N]
```

Over SSH the first line is `--> This connection is securely encrypted`
(40: `--> This connection is securely` / `    encrypted`), and the other two
lines stay. The password advice holds on any connection, and the privacy
screen still answers "know more" honestly for an SSH caller.

The 40 password line keeps today's shorter wording, which the firmware
already uses at under 64 columns.

---

## Facts used, and where they came from

| Fact | Source | Checked |
|---|---|---|
| CONFIG pages: board, limits, accounts, backup, staff, network, photos; fields per page | `kPages` and the `k*` field tables, `src/core/bbs_sysop.cpp`, rel-1.2.1f | 2026-10-01 |
| The 40-column label of the closed row is "Closed", 80 is "Stop taking calls"; it is the last row of CONFIG board | same file, `kBoard` | 2026-10-01 |
| Room commands /s, /p n, /welcome, /help (= /?), /q | `src/plugins/chat.cpp`, the room's help and alias table | 2026-10-01 |
| The room writes no log; its history is a RAM ring | chat.cpp: `history` setting, `calloc` at start, no file but `mail.dat` and the ban list | 2026-10-01 |
| Every caller sees a connection line before the welcome (today "--> Connection via Telnet is not secure" or "via SSH is Secure.") | `Bbs::linkLine` in `startIntro`, bbs.cpp | 2026-10-01 |
| The screen player's token buffer is 16 bytes and folded to upper case | `tok_[16]`, `src/core/screens.h` | 2026-10-01 |
| The issues address is 46 characters with `https://`, 38 without | counted | 2026-10-01 |
| The repo has Issues and Discussions on, and the labels `bug`, `enhancement`, `question` | GitHub API, repos/rwmech/unleashed_BBS and its labels | 2026-10-01 |
| SSH on every ESP32-S3 board | CLAUDE.md (1.1.2, "Every ESP32-S3 board runs SSH"), `/docs/privacy` | 2026-10-01 |
| Ten caller lines | `BBS_MAX_NODES 10`, config.h | 2026-10-01 |
| Repo public at github.com/rwmech/unleashed_BBS | fetched | 2026-10-01 |
| Setup guide at unleashedbbs.net/docs/setup | fetched, "Set up your BBS" | 2026-10-01 |
| Privacy framing (radio, on the path on purpose, the cafe, say it in public, a password you use nowhere else) | `unleashed_documentation/pages/privacy.md`, `/different` | 2026-10-01 |
| "No ads, no feed, no platform in the middle"; "fits in your hand"; "stick of gum" | unleashed_site `server.py` front page, `pages/different.md` | 2026-10-01 |
| Board name up to 40 characters | `boardName[41]`, `src/core/sysconfig.h` | 2026-10-01 |

## For Rob

Settled on 2026-10-01 and followed here: the tagline ("The next-generation
BBS software"), board names up to 40 characters with the wrap accepted, the
four house rules, the connection line's wording and the sign-up warning
following the connection. Still open:

- **"Jerk", not the word you used, in rule 1.** I chose the milder word
  because the stock rules ship to every board, classrooms included (the
  site's /kids and /teachers send them here). Say so if you want it
  stronger. A sysop can always rewrite their own.
- **The issues address** is `github.com/rwmech/unleashed_BBS/issues`
  without `https://`: 38 characters, flush left on its own line at 40.
  With the scheme it is 46 and does not fit.
- **Prices:** none on any screen, in either version. The brief gave
  display boards "from about $60"; the site has the Waveshare 1.47 at about
  $20, the Makerfabs 3.5 inch at about $30, and the big Waveshare touch
  boards from about $60.
- **A stock closed screen:** I recommend none, because the built-in words
  are right. A stock file would replace the sysop's "any key logs in"
  line unless it carries it.
- **A stock forums screen:** words are above if you want the forums to
  have a door like FILES. It needs new letters drawn.
- **The board stays "sysop".** The site says "host" for newcomers and
  bridges to "sysop" with its glossary. On the board, sysop is the word
  every command uses (`HELP SYSOP`, `BYE`, `OPERATOR`), so the screens keep
  it.

## Hand-backs (code, not copy)

- **`@SECURE:ssh text|telnet text@` for 1.2.2.** Three things the screen
  player does today that this code cannot live with:
  - `tok_` is 16 bytes, and `SECURE:is|is not` is 16 characters before
    its terminator;
  - tokens are folded to upper case, and newuser and privacy use the code
    in lower case;
  - the argument has a space and a `|`.
  So the code wants its own path: case kept, a space and one `|` allowed,
  and a limit that covers about 24 characters of argument. The deck keeps
  every use to the words that differ, `IS|IS NOT` or `is|is not`, to keep
  it small. An unknown code prints as typed, so a 1.2.1 board playing a
  1.2.2 screen would show the code itself. Ship the screens with the
  firmware that knows the code.
- **`Bbs::askKnowMore` follows the connection** with Rob's wording (the
  section above). CLAUDE.md has it in 1.2.1, lane rel-1.2.1e, with the
  connection line and the closed sign's board name.
- **The tests move with the copy** (table under "Phrases the test suite
  reads"): the welcome tagline, the connection line and the sign-up
  warning.
- **busy.ans's right border** is padded for a one-digit `@NODES@`, so it
  is one column out at 10 lines (screen-artist).
