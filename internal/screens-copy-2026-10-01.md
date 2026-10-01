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
- No new @-codes. Every code used is in SCREENS.md.

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

## What changed in the language, and why

The site's voice is the target: plain, warm, about your own community, and
honest about telnet in the way the site and `/docs/privacy` are. Five
changes run through the whole deck.

- **Telnet, not "nothing here".** The old copy said "Nothing here is
  encrypted" in the rules and newuser. Since 1.1.2 every ESP32-S3 board
  takes SSH, and over SSH that sentence is false. The screens are the same
  on every board and cannot know how a caller came in, so they now say what
  is true everywhere: telnet is plain text, SSH is encrypted where a board
  has it, and the line the board prints before the welcome
  (`--> Connection via Telnet is not secure`, or `via SSH is Secure.`)
  says which one this caller is on.
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
- The tagline takes the site's promise (no ads, no feed, nobody in the
  middle). Rob decides: see "For Rob" at the end.
- "@BOARD@ running @BBS@" is split into two lines. With no board name,
  `@BOARD@` falls back to the software's name, and the old line read
  "µnleashed BBS running µnleashed BBS v1.1.1" (CLAUDE.md, 1.1.2 list).

80 columns (under the wordmark):

```80 welcome
  E L E C T R O N I C   F R E E D O M
  no ads  *  no feed  *  nobody in the middle  *  real hardware
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
 No ads, no feed, nobody in the middle.
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

**40 is shorter:** "real hardware" and the label "Terminal" are left out,
and the copyright is the short form the test expects. The tagline is 38
characters, so centred it ends in column 39, the most SCREENS.md allows.

**Words:** 59 at 80, 49 at 40, about what the screen carried before. Cut for slow lines:
nothing more to cut. The 300 baud line "Connecting you to @BOARD@" is
Rob's and stays, and it is the only paced line.

**For the artist:** a board name over 21 characters makes "Connecting you
to @BOARD@" wrap at 40. It can't move: the test reads "Connecting you
to" followed by the name.

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

**For:** the terms of the place, before anybody types a password. Six
rules, the honest ones first. The encryption warning and the offer of the
privacy screen follow it, immediately in front of the password, which is
where they do the most good.

**Shown:** when a caller presses R to register. Two pages at 80, three
at 40.

**Changes:**
- Rule 2 was "NOTHING HERE IS ENCRYPTED", which an SSH caller on an S3
  board reads as false. It is now "TELNET IS AN OPEN LINE", with the radio
  framing and one sentence about SSH.
- Rule 3 said a reused password was handed "to everyone between you and
  this board". That overstates it, and Rob's framing is that being able
  to listen is not listening. It now says "to whoever heard it".
- Rule 4 gains the cafe test from the site. The heading is now "THE SYSOP
  CAN SEE IT ALL", which is accurate rather than ominous: the caller log
  and SNOOP are real, and nobody reads everything.
- Rule 5 drops the price and the floppy comparison and uses the site's
  "stick of gum". The heading is "IT IS A SMALL BOARD" (rel-1.2.1f had
  "IT IS A SMALL, CHEAP BOARD").

```80 rules
  THE HOUSE RULES
  ----
  You are about to make an account. This is the whole deal, and it is
  shorter than the thing you clicked through this morning.

  1.  NO HATE.
        Argue with anybody about anything. Come after a person for who they
        are and you are off the board. No warning, no appeal, no long
        conversation about it.

  2.  TELNET IS AN OPEN LINE.
        Over telnet, every word you type crosses the network as plain text,
        your password included. It is like radio: somebody on the path,
        listening on purpose, could hear it. SSH, where the board has it, is
        encrypted.

  3.  USE A PASSWORD YOU USE NOWHERE ELSE.
        This is the one that matters. If what you type here is also the
        password on your mail, you have handed your mail to whoever heard
        it. Make one up. It does not have to be clever. It has to be new.
<FF>
  THE HOUSE RULES
  ----
  4.  THE SYSOP CAN SEE IT ALL.
        Calls are logged, and staff can watch a node. Chat and mail are not
        private. Say what you would say at a table in a cafe.

  5.  IT IS A SMALL BOARD.
        The whole BBS is one microcontroller about the size of a stick of
        gum, on a shelf somewhere. Be patient with it. If it drops you, call
        back.

  6.  CHAOTIC NEUTRAL.
        Past all that, do as you like. Get along.

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

1.  NO HATE.
    Argue with anybody about
    anything. Come after a person for
    who they are and you are off the
    board. No warning, no appeal, no
    long conversation about it.

2.  TELNET IS AN OPEN LINE.
    Over telnet, every word you type
    crosses the network as plain
    text, your password included. It
    is like radio: somebody on the
    path, listening on purpose, could
    hear it. SSH, where the board has
    it, is encrypted.
<FF>
THE HOUSE RULES            Page 2 of 3
----
3.  USE A PASSWORD YOU USE
    NOWHERE ELSE.
    This is the one that matters. If
    what you type here is also the
    password on your mail, you have
    handed your mail to whoever heard
    it. Make one up. It does not have
    to be clever. It has to be new.

4.  THE SYSOP CAN SEE IT ALL.
    Calls are logged, and staff can
    watch a node. Chat and mail are
    not private. Say what you would
    say at a table in a cafe.
<FF>
THE HOUSE RULES            Page 3 of 3
----
5.  IT IS A SMALL BOARD.
    The whole BBS is one
    microcontroller about the size of
    a stick of gum, on a shelf
    somewhere. Be patient with it. If
    it drops you, call back.

6.  CHAOTIC NEUTRAL.
    Past all that, do as you like.
    Get along.

----
Still here? Good. Pick a handle and a
password nobody else has ever seen,
and welcome aboard.
```

**Same words** at both widths. Pages at 40: 22, 16 and 17 rows, against
the 22-row budget.

**Words:** 244 (was 252), titles aside. Cut for slow lines: nothing more. Each rule is
a sentence or two. The opening joke and "Chaotic neutral" are the board's
personality, and Rob's.

---

## newuser

**For:** the short version of the rules once somebody is actually in,
and the three things worth typing first.

**Shown:** once, in place of the motd, right after a registration
succeeds. Followed by "[H]ELP for commands." and the prompt.

**Changes:**
- "Nothing here is encrypted" becomes "Telnet is plain text", for the
  same reason as rule 2.
- "The sysop reads the logs" becomes "Staff can watch a node". There is a
  caller log, but nobody keeps a chat log, so the old line implied a record
  that does not exist. SNOOP is what makes chat not private.

```80 newuser
  YOU ARE ON THE BOARD
  ----
  Welcome aboard, @USER@. Node @NODE@ of @NODES@ is yours.

  The short version, now that you have joined:

      * No hate. That is the one that gets you removed.
      * Telnet is plain text. Never reuse a password.
      * Chat and mail are not private. Staff can watch a node.
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
* No hate. That is the one that gets
  you removed.
* Telnet is plain text. Never reuse a
  password.
* Chat and mail are not private.
  Staff can watch a node.
* Be patient. It is a microcontroller,
  not a data centre.
----
?        the command list
PRIVACY  the long version of rule two,
         any time you like
CHAT     find out whether anyone else
         is awake
```

**Same words** at both widths. 40 is 21 rows, its budget.

**Words:** 79 (was 79).

---

## chatin

**For:** the step into the chat room: what the room can see, and the five
commands a newcomer needs. The room's own lines (who is here, what was
just said) follow it, so it stays short enough that a quiet room still
shows all of it.

**Shown:** on joining the room, and again on `/welcome`.

**Change, a factual one:** the old copy said `/p` "crosses the wire in
the clear and the sysop has the log". No chat log exists: the room keeps
its last lines in a ring in memory (`history`, in CONFIG chat), and
nothing of the room is written to disk. Staff can SNOOP a node, and that
is the real reason `/p` is not private.

```80 chatin
  ENTERING CHAT
  ----
  Everyone in the room sees what you type. /p sends a line to one person,
  which is quieter but not private: staff can watch any node. The room
  remembers its last lines in memory, and nothing more.

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
which is quieter but not private:
staff can watch any node. The room
remembers its last lines in memory,
and nothing more.

/s        who else is here
/p n      a line to one person
/welcome  read this again
/help     the rest of the commands
/q        leave, or press _
----
```

**Same words** at both widths. On PETSCII, `_` is shown as the
left arrow, the Commodore's ESC, as on rel-1.2.1f. 40 is 15 rows, against a
budget of 16.

**Words:** 64 (was 70).

---

## privacy

**For:** the honest explanation of telnet, read by somebody who is about
to choose a password. It is the screen this whole project is most careful
about. Rebuilt on Rob's order, the same one `/docs/privacy` uses: what it
is, what it would take, the comparison both ways, what to do.

**Shown:** at sign-up, when the caller answers Y to "Would you like to know
more?", and any time on `PRIVACY`. Four pages, numbered.

**Changes:**
- Page 1 drops "a C64 cannot do encryption" and points at the
  connection line, which already tells every caller whether they are on
  telnet or SSH.
- Page 2 opens with radio, and ends with "being able to listen is not
  listening".
- Page 3 is the cafe, then the website for contrast, then what this board
  knows. "Low risk, not no risk" is now its heading, so the "not zero" half
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

  Some boards also take SSH, which is encrypted. The line before the
  welcome said which way you came in.

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
  The sysop sees your handle, where you called from, when, for how long,
  and your last command. Staff can watch a node. Mail waits in a file until
  it is read. That is true of any machine you use. Here you know whose it
  is.

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

Some boards also take SSH, which is
encrypted. The line before the welcome
said which way you came in.

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
The sysop sees your handle, where you
called from, when, for how long, and
your last command. Staff can watch a
node. Mail waits in a file until it
is read. That is true of any machine
you use. Here you know whose it is.

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

**Words:** 376 (was about 350): longer by the radio and the cafe, which
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

## Facts used, and where they came from

| Fact | Source | Checked |
|---|---|---|
| CONFIG pages: board, limits, accounts, backup, staff, network, photos; fields per page | `kPages` and the `k*` field tables, `src/core/bbs_sysop.cpp`, rel-1.2.1f | 2026-10-01 |
| The 40-column label of the closed row is "Closed", 80 is "Stop taking calls"; it is the last row of CONFIG board | same file, `kBoard` | 2026-10-01 |
| Room commands /s, /p n, /welcome, /help (= /?), /q | `src/plugins/chat.cpp`, the room's help and alias table | 2026-10-01 |
| The room writes no log; its history is a RAM ring | chat.cpp: `history` setting, `calloc` at start, no file but `mail.dat` and the ban list | 2026-10-01 |
| Every caller sees "--> Connection via Telnet is not secure" or "via SSH is Secure." before the welcome | `Bbs::linkLine` in `startIntro`, bbs.cpp | 2026-10-01 |
| SSH on every ESP32-S3 board | CLAUDE.md (1.1.2, "Every ESP32-S3 board runs SSH"), `/docs/privacy` | 2026-10-01 |
| Ten caller lines | `BBS_MAX_NODES 10`, config.h | 2026-10-01 |
| Repo public at github.com/rwmech/unleashed_BBS | fetched | 2026-10-01 |
| Setup guide at unleashedbbs.net/docs/setup | fetched, "Set up your BBS" | 2026-10-01 |
| Privacy framing (radio, on the path on purpose, the cafe, say it in public, a password you use nowhere else) | `unleashed_documentation/pages/privacy.md`, `/different` | 2026-10-01 |
| "No ads, no feed, no platform in the middle"; "fits in your hand"; "stick of gum" | unleashed_site `server.py` front page, `pages/different.md` | 2026-10-01 |
| Board name up to 40 characters | `boardName[41]`, `src/core/sysconfig.h` | 2026-10-01 |

## For Rob

- **The welcome tagline.** "No web. No cloud. No browser." is yours and
  has been on every board since the start. The site's promise is "no
  ads, no feed, no platform in the middle", and that is what this deck
  proposes ("No ads, no feed, nobody in the middle."). Keep yours, take
  the site's, or keep yours at 40 and the site's at 80.
- **Rule 2's heading:** "TELNET IS AN OPEN LINE" replaces "NOTHING HERE IS
  ENCRYPTED", which is false for an SSH caller. The test reads only "HOUSE
  RULES" and "NO HATE", so any heading works.
- **Rule 5's heading:** "IT IS A SMALL BOARD" here, against rel-1.2.1f's
  "IT IS A SMALL, CHEAP BOARD". Either works. The floppy comparison is gone
  from both because it is false on the S3s.
- **Prices:** none on any screen, in either version. The brief gives
  display boards "from about $60". The site has a range: the
  Waveshare 1.47, whose screen is a status strip, about $20; the Makerfabs
  3.5 inch about $30 ("a full display starts from about $30" since site
  1.5.9); the big Waveshare touch boards from about $60. So "from about $60"
  is true only of the big touch boards. If a price ever goes on a screen,
  the site's "from about $15" is the one that holds.
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

- **The sign-up warning is wrong over SSH.** `Bbs::askKnowMore`
  (bbs.cpp, 1.2.1f around line 1855) always prints "This connection is not
  encrypted. Use a password you do not use anywhere else." A caller on SSH
  on an S3 board is told something false immediately before typing a
  password. It should follow `linkLine`'s answer. Over SSH, perhaps:
  "This connection is encrypted. Still, use a password you do not use
  anywhere else."
- **The built-in closed sign names the software**, not the board
  (`BBS_NAME` in `startBusy`).
- **busy.ans's right border** is padded for a one-digit `@NODES@`, so it
  is one column out at 10 lines (screen-artist).
- **A long board name wraps at 40** wherever `@BOARD@` shares a line. The
  deck keeps it alone wherever a test does not fix the line. Capping
  `board_name` at 38 would close it for good, and that is Rob's call.
