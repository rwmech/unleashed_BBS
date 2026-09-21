<!--
 ===========================================================================
  µnleashed BBS
  Electronic freedom on a microcontroller.
 ===========================================================================

 File:         reports/ux-message-boards.md
 Module:       Design report / caller-facing UX for the message boards

 Purpose:      The flow, the keys and the screens for FORUMS: flat
               chronological messages in containers, with a per-caller
               last-read pointer. Design only. No code, no data model.

 Audience:     Rob, and whoever builds the forums plugin.

 Scope note:   PLAN-BULLETINS.md owns the data model, the file formats,
               the RAM budget and the phasing. This document owns what a
               caller sees and what the keys do. Where the two touch, this
               one says so and defers.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v2 or later
 SPDX-License-Identifier: GPL-2.0-or-later
 ===========================================================================
-->

# Forums: the caller-facing design

## The verdict

This is a good subsystem to build and the shape is already decided by three
things that are not up for discussion: the file manager set the house style
for a subsystem, the pointer is the feature, and a C64 has 40 columns and 25
rows. Follow those and the design writes itself.

The whole of it comes down to one sentence, and it is the thing to hold on
to when any detail is in doubt:

> **A caller should be able to read everything new on the board by pressing
> one key, repeatedly, and that key should be Enter.**

Not because one key is cute, but because this board's pager already treats
Enter as "carry on" (`bbs.cpp:2319`). Enter continues at `[More]`, Enter
continues at the end of a message, Enter carries you across a forum
boundary into the next forum. A caller who learns exactly one thing about
the forums can read the entire board with it, through page breaks and
container boundaries alike, and never once find out that those are
different mechanisms.

That single property is what made Citadel's `G` loop worth copying, and this
board can do it better than Citadel did, because the continue key is already
uniform.

---

## The trap, first, because it decides the key map

`N` is the obvious key for "next message". Citadel used `<N>ew messages`,
WWIV used `N`, PCBoard used `(N)ew`. It is wrong here, and this is not a
matter of taste.

`src/core/bbs.cpp:2318`:

```
case SState::More: {
    bool cont = k == 'y' || k == 'Y' || k == ' ' || k == KEY_ENTER;
    bool non  = k == 'c' || k == 'C';
    bool stop = k == 'n' || k == 'N' || k == 'q' || k == 'Q' || ...
```

At `[More]`, **`N` means stop**. A message longer than a screen pages, and
a caller who has been taught "press N for the next message" will press N at
the page break and lose the rest of the message they were reading. They will
not understand why, because from where they are sitting they pressed the
same key twice and got two different results.

So:

- **Enter and SPACE are the loop.** Both already continue at `[More]`.
- **`N` is not offered at all**, not even as an alias. An alias that is safe
  at one prompt and destructive at another is worse than no alias.
- `C` at `[More]` means continuous, which is exactly right for a long
  message on a fast link, and it is free.

This is the single most important finding in this document and it comes
from reading this board's code rather than from any BBS history.

---

## What the precedents did, and what is taken from each

### Citadel (1982), the one being borrowed from

Citadel's model was rooms, each a ring of messages, with a per-user mark of
where that user had got to in each room. The caller entered at the Lobby.

What made it work was not the room structure, it was that **the "go
somewhere useful" decision was a single key that the board answered, not the
caller**. `<G>oto` moved to the next room with unread messages in it, so the
caller never had to know which rooms had anything. Press G until there is
nothing left; that is the session. `<N>ew` read the new messages in the room
you were standing in, `<S>kip` marked the room read and moved on when you
did not care, and `<Z>ap` (forget room) took a room out of the rotation
permanently.

**Taken:** the board decides what is next, not the caller. `G` keeps its
letter and its meaning. `S` keeps its letter and its meaning. The
message-entry flow (blank line ends input, then a Save / Abort / Continue
choice) is taken more or less whole, because it is still the right answer
for a terminal with no cursor addressing.

**Left:** `Z`ap. It earned its keep on boards with hundreds of rooms; on a
board with five to eight forums, "I do not care about Swap Shop" is answered
by pressing `S` when you get there, which costs one key and no per-account
state. If a board ever has thirty forums this comes back, and the note is
here so nobody has to rediscover the argument.

**Left:** the Lobby as a place you land in and read from. This design lands
you on a list of forums with unread counts, which Citadel did not have and
which is strictly more orienting. Citadel made you press `K`nown rooms to
find out what existed.

### WWIV subs, PCBoard conferences, FidoNet echomail

All three are the same shape as Citadel with different nouns: a container, a
flat chronological list inside it, and a per-user "last message read"
pointer per container. PCBoard's pointer was literally called the Last
Message Read pointer and was per conference. WWIV had the new-scan across
subs. Offline readers (QWK, Blue Wave) took the same pointer and packaged
everything past it into a file.

**Taken:** the vocabulary of a **new scan** across every container at once,
and the fact that all of them made it a first-class thing rather than
something you assembled by visiting each container. Here it is the default
action at the forum list.

**Left:** the separate scan command. WWIV and PCBoard had both "read this
conference" and "scan everything", which is two commands for one intent. On
this board, Enter at the forum list *is* the scan, so a second verb would be
a second way to do the only thing the subsystem is for.

**Left:** the offline reader. It is the right idea and it is XMODEM plus a
packet format; it belongs after the online reader exists, not instead of it.

### Usenet: rn, trn, tin, nn

`rn` is where the "one key, forever" ergonomic actually comes from. At the
newsgroup prompt the default was yes, read it; at the article prompt the
default was the next article; and SPACE meant "whatever the obvious thing
is" everywhere. You could read a day of Usenet with a thumb.

`.newsrc` recorded **ranges** of read articles per group, not a single
high-water mark, which is why `rn` could handle "I read the new ones, then
someone posted three more". That is a genuinely better data model than a
single pointer and it is deliberately not being copied: see "Considered and
rejected".

`tin` put a **selector screen** in front of it: a list of groups with unread
counts and descriptions, three levels (group, thread, article), menu-driven
rather than prompt-driven. Most people found tin easier than rn for exactly
one reason, which is that you could see the shape of what was waiting before
committing to it.

`nn` inverted the loop entirely: pick subjects off a menu first, then read
the selection. Interesting, and wrong for a small board where the right
answer is almost always "read all of it".

**Taken from rn:** the default action at every prompt is the one you wanted,
and SPACE does it. The catch-up key `c` (here `C` at the forum list), which
rn confirmed before acting.

**Taken from tin:** the forum list as the hub, with unread counts and
descriptions. That is the screen a caller lands on.

**Left from trn/tin:** threading and the thread selector, which is settled.

**Left from rn:** read-ranges in the pointer. One monotonic number per
forum, for reasons under "Considered and rejected".

---

## The shape: two levels and one loop

Two places, and the caller always knows which one they are in because the
prompt says so.

```
Forums>            you are at the list of forums
[F2] C64>          you are inside forum 2, which is called C64
```

The bracketed tag appears only when you are inside something. That is the
same device `files` uses (`[S1] Files>`), with one improvement: the forum's
**name** is in the prompt, not the subsystem's, because in a reading session
that crosses forums the name is the thing you need and the subsystem name
never changes.

---

## The flow

```
                              login
                                |
                  one line, only when there is something:
                  "12 new in 3 forums. FORUMS reads them."
                                |
        FORUMS typed   ......or...... account Start = Forums
                                |
                      screens/forums plays, if the board has one
                                |
                    first ever visit?  -->  THE FIRST-VISIT QUESTION
                                |                    |
                                |<-------------------+
                                v
      +---------------------------------------------------------+
      |  THE FORUM LIST                          prompt: Forums> |
      |  every forum the caller may read, with unread counts     |
      |  ANSI/PETSCII: a reverse bar, starting on "Read all new" |
      |  plain ASCII: the numbers, and the hint line says so     |
      +---------------------------------------------------------+
        Enter / SPACE ... start the loop (or open the barred row)
        1..9, 0 ......... open that forum, at its first unread
        cursors ......... move the bar          (ANSI/PETSCII only)
        G ............... jump to the next forum with unread
        C ............... catch up: mark everything read (confirms)
        ? ............... the key list, one page
        Q / ESC ......... leave the forums entirely
                                |
                                v
      +---------------------------------------------------------+
      |  INSIDE A FORUM                       prompt: [F2] C64>  |
      |  nothing is redrawn; messages scroll up like a log       |
      +---------------------------------------------------------+
        Enter / SPACE ... the next message you have not read
                          at the end of the forum: the boundary line,
                          then Enter again crosses into the next forum
                          with unread; when there is none, the loop ends
                          and you are put back on the forum list
        B ............... back up one and read it again
        1..9 ............ a number reads that message  (accumulates)
        L ............... the listing, paged           --> LISTING
        P ............... post here                    --> EDITOR
        R ............... reply to the one on screen   --> EDITOR
        + / - ........... vote the one on screen up or down
        G ............... next forum with unread, without finishing here
        S ............... skip: mark this forum read, move on
        ? ............... the key list, one page
        Q / ESC ......... back to the forum list
                                |
                +---------------+----------------+
                v                                v
      +-------------------+            +------------------------+
      |  LISTING          |            |  EDITOR                |
      |  clears first     |            |  clears first          |
      |  paged by [More]  |            |  Subject: then lines   |
      |  Y/SPACE/Enter on |            |  blank line ends input |
      |  n, q, ESC off    |            |  [S]ave [C]ontinue     |
      |  a number reads   |            |  [L]ist [E]dit [A]bort |
      +-------------------+            +------------------------+
                |                                |
                +--------> back to [F2] C64> <---+
```

**Every way out is Q or ESC, and it always goes back exactly one level.**
Forum to forum list, forum list to the shell. That is the `files` rule and
it should not be varied.

The one thing that is not reachable by Q is the `[More]` pager, where `q`
and ESC abort the output and land you back at the forum prompt. That is the
same key doing the same thing one layer down, which is fine, and it is
subject to the core bug in "What this needs from the core", below.

---

## The keys

Inside a forum:

| Key | Does | Where it comes from |
|---|---|---|
| `Enter` | the next message you have not read; at the end of a forum, into the next forum with unread | rn's default action; already the continue key at this board's `[More]` |
| `SPACE` | identical | rn; already the continue key at `[More]` |
| `B` | back up one message and show it again | Citadel `<O>ld messages` in reverse, rn's `-` |
| `L` | the numbered listing | `files` uses `L` for exactly this |
| a digit | read that message; digits accumulate so 1234 is reachable | `files`, where a digit picks a file |
| `P` | post a new message here | WWIV |
| `R` | reply to the message on screen: subject prefilled `Re:` | WWIV, PCBoard |
| `+` `-` | vote the message on screen up or down | new; no precedent worth citing |
| `G` | go to the next forum with unread | **Citadel `<G>oto`**, kept deliberately |
| `S` | skip: mark this forum read and move on | **Citadel `<S>kip`** |
| `?` | the key list, on the screen it applies to | `files` |
| `Q` `ESC` | back to the forum list; again to leave | `files` |

At the forum list:

| Key | Does | Where it comes from |
|---|---|---|
| `Enter` `SPACE` | start the loop; on a pointing terminal, open whatever the bar is on, and the bar starts on the "read all new" row | tin's selector, rn's default action |
| a digit | open that forum at its first unread; `0` means ten | `files` |
| cursors | move the bar | `files` |
| `G` | jump to the next forum with unread | Citadel |
| `C` | catch up: mark every forum read. **Confirms.** | rn's `c`, tin's `C`, both of which confirmed |
| `?` | the key list | `files` |
| `Q` `ESC` | leave the forums | `files` |

Keys deliberately **not** used, and why:

- **`N`**, because at `[More]` it means stop. Covered above.
- **`E`** for enter-a-message, Citadel's key. `E` means erase in the file
  manager, and a caller who uses both subsystems should not have one letter
  meaning "create" in one place and "destroy" in another. `P` is
  unambiguous and is what the last thirty years of software has called it.
- **`Z`**, Citadel's forget-room. Argued above.
- **`A`**, which in the file manager means approve. Left free for forum
  moderation to use the same way, if moderation ever wants a key.
- **`-` for "previous message"**, which is rn's. It collides with the
  downvote, and a vote is a thing a caller will do far more often than
  stepping backwards. `B` takes the backwards job.

### Why `S` does not confirm and `C` does

Neither destroys a message. Both only move a pointer, and everything they
skip is still reachable by `L` and by number. The difference is magnitude
and recovery: `S` gives up one forum you are standing in and looking at,
`C` gives up the whole board's unread in a single keypress at a screen that
is one keystroke from the shell. rn confirmed its catch-up for the same
reason and did not confirm anything else.

This is a judgement about scale rather than about destruction, and it is
worth writing down as such so nobody "fixes" the inconsistency later.

---

## The screens

Every mock below was generated to the character grid and measured; the
ruler above each one is real and no line exceeds `rowWidth`, which is
`cols - 1` (`bbs_shell.cpp:428`). Colour is described in the notes, not
drawn.

### The forum list, at 40

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 Forums                         12 new 

     Read all 12 new messages

   1 General        5
   2 C64            4
   3 Swap Shop      3
   4 Off Topic       
   5 Sysop Notes     

 Enter reads them. ? help. Q leaves.
Forums> 
```

- Row 1 is `rowTitle`, so it is a reverse-video bar on ANSI and PETSCII and
  a dashed rule on plain ASCII, exactly like every other list on the board.
- The "Read all 12 new messages" row is where the highlight bar sits when
  the screen is drawn. It is the only row without a number, which is what
  makes it visibly different from a forum.
- Unread counts are right-aligned in a 3 wide column at the end of the name
  block. **Zero shows as nothing**, and that row is drawn in grey. A column
  of `0`s reads as a fault.
- 12 rows with five forums, one more per extra forum. With the eight the
  CONFIG page can hold it is 15 rows, which leaves 10 for `screens/forums`
  on a 25 row C64. Budget the door screen at 8 rows and it always fits.
- When nothing is new: the title bar right text reads `nothing new`, the
  action row is replaced by a grey `Nothing new since your last call.`, the
  bar starts on forum 1, and the hint line becomes
  ` A number opens a forum. ? help. Q leaves.`

### The forum list, at 80

```
         11111111112222222222333333333344444444445555555555666666666677777777778
12345678901234567890123456789012345678901234567890123456789012345678901234567890
 Forums                                                                 12 new 

     Read all 12 new messages

                                                    New  Last post
   1 General      Anything at all                     5  Daytona   19 Sep
   2 C64          The machine this board is for       4  Rob       19 Sep
   3 Swap Shop    Buying, selling, giving away        3  Mindcrime 18 Sep
   4 Off Topic    Everything else                        Daytona   12 Sep
   5 Sysop Notes  Board news, read only                  Rob       02 Sep

 Enter reads them.  G next with new.  ? help.  Q/ESC leaves.
Forums> 
```

**The extra width buys two columns, not two columns of forums.** This is the
answer to the standing "it looks jammed together at 80" complaint, and it is
a different answer from the one `files` gave. `files` packs its areas into
a grid because an area is nothing but a name. A forum has a description
worth reading and a last-post line worth scanning, so the width goes to
content:

- **Description** appears at 64 columns and up, sized
  `max(20, min(34, whatever is left))`. It is the one place a sysop can say
  what a forum is for, which otherwise has nowhere to live.
- **`New` and `Last post` get a header row** at 64 and up. A bare integer
  between a description and a handle is ambiguous without one, and plain
  ASCII has no colour to lean on.
- **Last post** is handle (9) and date (6). It comes from RAM: the plugin
  already has to hold the highest message number per forum to compute the
  unread counts, and holding the last poster and time beside it is about 24
  bytes per forum. **Nothing on this screen touches the card.**

At 132 the layout is identical to 80. Stretching a five row table across 131
columns is not using the screen, it is spreading it thin. The description
cap of 34 is what stops it.

### Reading, at 40: three messages, three keypresses

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 C64                             4 new 

412  Daytona  19 Sep 21:14  +3
1541 alignment disk
Anybody got a working 1541 alignment
disk? Mine died last week and the local
shop wants 40 quid for a service.
[F2] C64> 

413  Mindcrime_the  19 Sep 22:02
Re: 1541 alignment disk
I have one. Yours for the postage.
[F2] C64> 

414  Rob  20 Sep 08:30
Board is on 0.18.0 tonight
Short outage about nine. Forums are
new, have a poke at them.
[F2] C64> 
```

19 rows on a 25 row screen, and that is three whole messages. The things
that make it fit:

- **The screen is never cleared between messages.** Entering the subsystem
  clears, the listing clears, the help clears, the editor clears. The
  reading loop does not, because it is a scroll and not a view: the previous
  message is the context for this one, and a caller who wants to look back
  at what they just read should be able to. This is the one place where the
  "screen clears bro" rule from the file manager does **not** apply, and it
  is worth saying out loud because somebody will try to make it consistent.
- **The prompt line is the separator.** There is one blank line *above* each
  message header and none below the body, so the overhead between two
  messages is two rows: the prompt and a blank. Putting the blank above the
  header rather than below the body is what makes the grouping read
  correctly.
- **The message header is compact and left-aligned at every width.** Number,
  handle, date, and the vote tally when there is one. It does not stretch to
  the right margin: it is a label above a paragraph, not a table row, and a
  single line with a 50 column gap in the middle of it looks like a bug.
- The forum title bar appears **only** on arriving in a forum or crossing
  into one, never per message.
- Handle is shown to 14 characters at 40 and 20 at 64 and up. At 40 with a
  five digit message number and a 14 character handle the header is exactly
  39 columns, which is the worst case and it fits.

Colour, consistent with the rest of the board: the header row grey, the
subject white or light green (it is the thing your eye should land on), the
body light grey, the vote tally yellow when positive and light red when
negative, the prompt cyan with the `[F2]` tag in light green, matching
`files`.

### Reading, at 80

```
         11111111112222222222333333333344444444445555555555666666666677777777778
12345678901234567890123456789012345678901234567890123456789012345678901234567890
 C64                                                                     4 new 

412  Daytona  19 Sep 21:14  +3
1541 alignment disk
Anybody got a working 1541 alignment disk? Mine died last week and the local
shop wants 40 quid for a service.
[F2] C64> 

413  Mindcrime_the_Ripper  19 Sep 22:02
Re: 1541 alignment disk
I have one. Yours for the postage.
[F2] C64> 

414  Rob  20 Sep 08:30
Board is on 0.18.0 tonight
Short outage about nine. Forums are new, have a poke at them.
[F2] C64> 
```

The body is **word-wrapped on output at the reader's width**, not as it was
typed. A message typed at 72 columns on SyncTERM has to be readable on a
C64, and a message typed at 35 columns on a C64 should not sit in a 35
column stripe down the left of an 80 column screen. Wrapping at read time is
the only thing that can be right for both, because the reader's width is not
knowable when the message is written.

**This needs a word-wrap helper that does not exist yet**, and it is the
same one the queued "profile text should word wrap" item wants. Build it
once, in the core, and both get it. It is the only genuinely new drawing
primitive this subsystem needs.

### The listing, `L`, at 40

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 C64                  4 new  415 total 
 409 Rob      Board is on 0.18.0 tonigh
 410 Daytona  Anyone want a spare 1571?
 411 Mindcrim Re: Anyone want a spare 1
*412 Daytona  1541 alignment disk      
*413 Mindcrim Re: 1541 alignment disk  
 414          (deleted)                
*415 Daytona  JiffyDOS on a breadbin, w
---------------------------------------
 * is new. A number reads it.
[F2] C64> 
```

- **Column 1 is the unread mark.** That is the entire reason the listing
  exists in a subsystem whose point is a pointer.
- **The message number column is as wide as the forum's largest number**,
  minimum 3. Freezing it at 5 wastes two columns of subject for the years
  before any board gets there, and this board has been bitten by frozen
  column widths more than once.
- Handle 8, subject the rest, which is 25 today and 23 once numbers reach
  five digits. That is the real cost of 40 columns and there is no clever way
  out of it. The alternative, dropping the handle for 34 columns of subject,
  is worse on a board where you know everybody by name, and it is one
  constant to change if Rob disagrees.
- **A deleted message keeps its row and never carries the unread mark.** It
  is skipped by the reading loop but shown here, because a conversation with
  a silent gap reads as the board being broken and a visible `(deleted)`
  reads as moderated.
- **`L` opens on the page containing the pointer**, not at the top and not
  at the end. In a forum with 400 messages the top is useless. A little
  context above the mark and the unread below it is what somebody actually
  wants. With nothing unread, the last page.
- A number typed here reads that message and returns to the forum prompt,
  not to the listing. Jumping is a detour, not a mode.

### The listing at 80

```
         11111111112222222222333333333344444444445555555555666666666677777777778
12345678901234567890123456789012345678901234567890123456789012345678901234567890
 C64                                                          4 new  415 total 
 409 Rob            Board is on 0.18.0 tonight                     18 Sep 19:02
 410 Daytona        Anyone want a spare 1571?                      18 Sep 20:44
 411 Mindcrime      Re: Anyone want a spare 1571?                  19 Sep 09:12
*412 Daytona        1541 alignment disk                            19 Sep 21:14
*413 Mindcrime      Re: 1541 alignment disk                        19 Sep 22:02
 414                (deleted)                                      20 Sep 08:30
*415 Daytona        JiffyDOS on a breadbin, worth it?              20 Sep 18:55
-------------------------------------------------------------------------------
 * is new.  A number reads it.  Enter carries on.
[F2] C64> 
```

The width buys the handle its full 14 and a date column, and the subject
gets 44. No vote column: see the voting section.

### Help, `?`, at 40 and at 80

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 Forums: what the keys do
Enter   the next one you have not read
SPACE   the same thing
B       back up one and read it again
L       list this forum, numbered
1 2 3   a number reads that message
P       post a message here
R       reply to the one on screen
+  -    vote it up or down
G       next forum with new
S       mark this forum read, move on
?       this
Q  ESC  back one level, again to leave
---------------------------------------
```

```
         11111111112222222222333333333344444444445555555555666666666677777777778
12345678901234567890123456789012345678901234567890123456789012345678901234567890
 Forums: what the keys do
Enter    the next message you have not read
SPACE    the same thing
B        back up one, read it again
L        list this forum, numbered
1 2 3    a number reads that message
P        post a new message here
R        reply to the one on screen
+  -     vote the one on screen up or down
G        go to the next forum with new
S        skip: mark this forum read, move on
?        this
Q  ESC   back to the forum list, again to leave
-------------------------------------------------------------------------------
```

14 rows, so it fits one page on a 24 row terminal with the prompt, which is
the rule the shell rework established: a help screen that pages every time
teaches people to hammer through it. Two texts per row, the short one under
60 columns, which is the split `files` already uses.

Rows a caller cannot use are not printed, the same way `filesHelp` hides the
staff rows. Somebody who may not post does not see `P` or `R`; somebody who
may not vote does not see `+` and `-`.

The forum list has its own, shorter, help:

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 Forums: what the keys do
Enter   read everything new
1 2 3   a number opens that forum
cursors move the bar, Enter opens
G       next forum with new
C       mark the lot read
?       this
Q  ESC  leave the forums
---------------------------------------
```

The `cursors` row is printed only on ANSI and PETSCII, which is exactly the
test `files` uses (`canPoint`).

---

## "New since I left", end to end

This is the headline feature, so here is the whole of it as a caller
experiences it, with the keystroke count.

**At login**, one line, and only when there is something:

```
Welcome back, Daytona.
12 new in 3 forums. FORUMS reads them.
```

Same shape as the file manager's "3 uploads awaiting approval" and chat's
"You have mail": one line, yellow, nothing when the number is zero. It must
cost no card reads worth mentioning. The per-forum high-water numbers are in
RAM already; the caller's pointers are one fixed-width record, one seek and
one read.

**`FORUMS`**, or nothing at all if their account's `Start` is `Forums`,
which is already wired up (`users.cpp:66`).

**The forum list** appears, with the bar sitting on `Read all 12 new
messages`.

**Enter.** From here the caller does not have to make another decision:

```
[F2] C64> 
```

Enter, message. Enter, message. At a page break, `[More] Y/n/c` and Enter
carries on. At the end of a forum:

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
[F2] C64> 

End of C64. Next: Swap Shop, 3 new.
[F2] C64> 
```

Enter again, and the next forum's title bar appears and the reading
continues. When there is nothing left:

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
[F4] Swap Shop> 

All caught up: 12 messages in 3 forums.

 Forums                          0 new 
```

and the caller is back on the forum list, which now shows zeroes.

**Keystroke count: `FORUMS`, Enter, and then Enter once per message.** For
a caller whose `Start` is Forums it is Enter, and then Enter per message.
There is no way to make that shorter and no reason to try.

### Why the boundary pauses instead of running straight on

One line and one extra keypress per forum, and it buys the thing that makes
the loop comprehensible: you always know which forum you are in. Without it,
a caller reads eleven messages and has no idea that four of them were
somewhere else. Citadel made the boundary an explicit `G`; this makes it an
explicit Enter, which is the same beat without a second key to remember.

`S` at the boundary skips the forum that was about to be offered. `Q` stops.

### Things that must be true for this to feel right

- **A message posted while you are reading appears in your loop.** "Next
  unread" is computed each time, not precomputed into a list at the start.
  On a ten line board where two people are reading and one is posting, this
  happens constantly and it should just work.
- **The pointer advances when a message is displayed**, not when the caller
  moves past it. If the line drops mid-message, that one message is marked
  read. That is the standard behaviour of every reader here and the cost of
  the alternative is a second piece of state.
- **The pointer is written to the card on leaving a forum, on leaving the
  subsystem, and at logoff. Never per message.** A card write per message
  would put synchronous card work in the middle of the reading loop, which
  is precisely the shape that produced the 126 ms stall already on the
  books.
- **Nothing on the forum list touches the card.** Counts come from RAM.
- **A listing page costs one read, not one read per row.** The index is
  fixed-width, so a page is a seek and a block. `rows()` is called once per
  line by the core, so the plugin has to have the page in hand before the
  first row. This is the hard rule from the file listing experience and it
  is the one performance requirement this design actually imposes.

---

## The first time somebody uses it

This is where these systems feel worst, because the honest state is "431
messages, all unread" and the honest behaviour is to show them all.

**Ask once, at the top, on the first ever visit to the subsystem, and apply
the answer to every forum.**

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 Forums

First visit. 431 messages in 5 forums
and you have read none of them.

  [R] read the lot, oldest first
  [L] last 10 in each      (Enter)
  [S] skip it all, start tonight

Which? 
```

```
         11111111112222222222333333333344444444445555555555666666666677777777778
12345678901234567890123456789012345678901234567890123456789012345678901234567890
 Forums

First visit.  431 messages in 5 forums, and you have read none of them.

  [R]  read the lot, oldest first
  [L]  the last 10 in each forum        (Enter)
  [S]  skip it all and start from tonight

Which? 
```

- **Once, not per forum.** Asking per forum means five questions before the
  caller has read a word, which is how you teach somebody that this
  subsystem is admin rather than reading.
- **`L` is the default**, and Enter takes it. A first-time caller wants to
  see what the place is like, not read a year of backlog. Ten in each across
  five forums is about fifty messages, which is a representative evening and
  is recoverable in both directions: `B` backs up into what was skipped and
  `L` lists all of it.
- `R` is there because some people genuinely want the lot, and refusing them
  would be rude.
- `S` is there because some people want to start clean.
- **ESC is `L`**, not an abort. There is no sensible "cancel" for a question
  the caller has to answer before anything can happen.
- **It never appears again.** Once there are pointers, there is no first
  visit.

### A forum added after the caller's first visit

The same problem, smaller, and it does not get a question:

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
[F2] C64> 

 Swap Shop                       3 new 

New to you. Showing the last 10 of 96.
B backs up, L lists the lot.
```

One line of explanation and two ways back into the backlog. A question here
would interrupt a reading loop that the caller is in the middle of, to ask
about a forum they have not seen yet and cannot have an opinion about.

### A caller who has been away long enough that messages rolled off

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
[F1] General> 

 C64                             4 new 

31 messages here expired before you
read them.
```

Said once, on arriving in that forum, in yellow, before the first message.
Silently clamping the pointer would mean the board quietly decided
something on the caller's behalf and did not mention it.

### A guest

Guests get a pointer that lives in RAM for the length of the call, so within
one call everything works exactly as it does for an account. On the way in,
once:

```
Guest pass: the board will not remember where you got to.
```

Guests do not get the first-visit question, because there is nothing to
write down. They land on the forum list with everything unread and the
"last 10 in each" behaviour applied silently, which is the sensible default
and costs them nothing.

---

## Posting and replying

### The editor is a numbered line editor, and it has to be

There is no full-screen editor on this board and there should not be one:
plain ASCII has no cursor addressing, `LineEditor` is one line
(`editor.h:47`), and `FF_TEXTAREA` is four fixed rows inside a `Form`, which
is a different thing for a different job. A visual editor would also redraw
constantly, which is the one thing this board's output budget cannot pay
for.

A numbered line editor is what every BBS used, it works on a teletype, and
it is honest about what it is.

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 Post to C64

Subject: 1541 alignment disk

 1> Anybody got a working 1541
 2> alignment disk? Mine died last week
 3> and the local shop wants 40 quid
 4> for a service. Happy to pay postage
 5> both ways.
 6> 

[S]ave [C]ont [L]ist [E]dit [A]bort: 
```

```
         11111111112222222222333333333344444444445555555555666666666677777777778
12345678901234567890123456789012345678901234567890123456789012345678901234567890
 Post to C64

Subject: 1541 alignment disk

 1> Anybody got a working 1541 alignment disk? Mine died last week and the
 2> local shop wants 40 quid for a service. Happy to pay postage both ways.
 3> 

[S]ave  [C]ontinue  [L]ist  [E]dit a line  [A]bort: 
```

- **The gutter is 4 columns** (`NN> `), so the typed width is
  `min(BBS_LINE_MAX, rowWidth - 4)`: 35 on a C64 and 72 on anything from 76
  columns up. A C64 caller types short lines and a PC caller types long
  ones, and the output wrapper makes both readable to everybody.
- **A blank line ends input** and opens the decision line. That is Citadel's
  flow and it is still right. To get a blank line *inside* a message, type a
  single space; document that one trick in COMMANDS.md and nowhere else,
  because it is a thing people either need or never think about.
- **`C` continues** from where it left off, which is what makes the blank
  line safe: a caller who hit Enter one time too many is one key from
  carrying on.
- **`E` edits a line by number and prefills it**, using
  `LineEditor::replace()`, which already exists for history recall. Retyping
  the line from scratch would be the obvious cheap implementation and it is
  the wrong one: the usual reason to edit line 3 is a typo in one word.
- **`L` relists** with the numbers, because on a 25 row screen a long
  message has scrolled off by the time you finish it.
- **`A` aborts, and confirms**, because a message somebody has just spent
  five minutes typing is the most expensive thing in this subsystem to lose.
  Nothing else in this design confirms; this does.
- `/s`, `/a`, `/l` also work at the start of a line, for WWIV muscle memory.
  See the coordination note about `.` versus `/`.

### The subject is required

The subject *is* the listing. An empty subject makes a row that nobody can
use. Empty at the prompt re-asks once, with `A subject is how people find
it.`, and ESC at the subject prompt aborts the whole post before anything is
typed, which is the cheapest place to let somebody change their mind.

Subject is 59 characters (the index field), shown to 25 at 40 columns and 44
at 80.

### Reply

`R` is a new message with the subject prefilled as `Re: <original>`, cut to
fit 59. Nothing is quoted.

**No automatic quoting**, and this is a real decision rather than laziness.
A quote block at 40 columns with a `> ` prefix costs 2 of 35 columns, it has
to be re-wrapped to the replier's width and then re-wrapped again for every
reader, and the usual result is a message that is 80 percent quote. On a
board where the message you are replying to is three lines up the screen,
quoting buys nothing.

If it is ever wanted, the shape is a `Q` key **inside** the editor that
pulls the original in as `> ` prefixed lines that the caller can then cut
down with `E`, which is opt-in and puts the editing burden on the person who
chose it.

`R` with nothing on screen says `Read something first. P posts a new one.`

### What happens after a save

```
Posted as 416 in C64.
[F2] C64> 
```

One line, and straight back to the prompt with the loop intact. Posting is
not a reason to be thrown anywhere.

If the caller is over a cap, the refusal says which one and what to do about
it, in the file manager's style rather than "denied":

- `Twenty posts a day is the limit here. Tomorrow.`
- `Thirty seconds between posts. Try again in a moment.`
- `This forum is full and the card is nearly full. Tell the sysop.`

---

## Voting

### Where it sits

**Voting is a post-read action and nothing else.** `+` and `-` at the forum
prompt act on the message currently on screen.

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
[F2] C64> +
412 is now +4.
[F2] C64> 
```

- One keypress, no target to name, no number to get wrong. The thing you are
  voting on is the thing you just read, which is the only moment at which a
  vote means anything.
- **Pressing the same key again removes your vote; the opposite key changes
  it.** A vote you cannot take back is a vote people are reluctant to cast.
  It costs one record either way.
- The feedback is one short line with the new tally, so the caller sees the
  number move. That is the entire reward loop and it has to be immediate.
- A refusal says the actual reason:

```
[F2] C64> +
Voting needs an account and 5 calls.
```

  Not "you may not". The caller can act on the first one.

- Guests never vote, and the `+` and `-` rows are not printed in their help.
- **You cannot vote on your own message.** `That one is yours.` One line.
  Without it, the first thing anybody discovers is that they can upvote
  themselves, and then the number means nothing.

### Score visibility: the recommendation

**Show the tally on the message, never in a listing, and never as anything
that can be sorted on.**

Specifically:

- **In the message header, when it is not zero**, as a signed net figure:
  `412  Daytona  19 Sep 21:14  +3`. Zero prints nothing at all, not `0`.
- **Net only, not `+4 -1`.** On a board with ten lines a downvote count is
  small enough to be personal, and "one person downvoted this" on a board
  where four people are awake is an accusation with a very short list of
  suspects. Net hides the denominator, which is the kindest thing a small
  board can do with a score.
- **No column in the listing.** Three reasons, in order of weight:
  1. It is a number you can see and cannot act on. Reading order is
     chronological by design, so a score column in a listing is an
     invitation to ask for sorting, and sorting is the thing this design
     rejects. A UI element whose only possible next step is a feature you
     have ruled out should not be drawn.
  2. At 40 columns it costs 4 of 25 subject columns. The subject is what
     decides whether somebody reads a message; the score is not.
  3. A visible ranking in the index is what turns a conversation into a
     leaderboard. That is the dynamic threading plus vote-sorting produces,
     and it is what this board is deliberately not building.

**Give the tally one place where it is genuinely useful instead**, and make
that place explicitly not the reading loop: a `T` key at the forum list
showing the highest scoring messages of the last fortnight, drawn with the
same paged list, from which a number reads one. That answers "what is voting
for" without letting the score touch the pointer, and it is one screen.

Mark this as **phase 4 or later**. Voting can ship with no view at all and
be fine; it cannot ship with a sorted index and then have it taken away.

**This is the one item in this document that is genuinely Rob's call**, and
the alternative is coherent: show the score in the listing, accept that it
is decoration, and take the subject columns off the C64. I do not recommend
it, and if it goes in, it should go in at 64 columns and up only.

---

## How it degrades

### PETSCII at 40 by 25

Designed first, and everything above is measured at it. What changes from
80:

- The forum list loses the description and last-post columns and keeps the
  count. 12 rows with five forums.
- The listing loses the date column; subject drops from 44 to 25.
- Every hint line has a short form, at the `cols >= 60` test `files`
  already uses.
- The editor types 35 columns instead of 72.
- The help gets its short texts and still fits one page.

Everything else is identical, including every key. **No key and no screen
exists only at 80.**

PETSCII has reverse video (0x12/0x92) and cursor movement, so it gets the
highlight bar on the forum list exactly as ANSI does. It does **not** get
`eolClear`, so the bar redraw pads with spaces, which is why the forum list
must redraw only its own rows and not the whole screen. `files` already
solved this with its `Draw::Again` mechanism (`files.cpp:924`) and the same
approach applies here unchanged: move the bar, redraw the menu rows in
place, leave the door screen above alone.

### Plain ASCII

No cursor addressing, no reverse video, no colour. `cls` prints two blank
lines (`term.cpp:327`), `rowTitle` falls back to a dashed rule, and there is
no bar.

Nothing in the design depends on any of that. The forum list is numbered and
the hint line says so; Enter still starts the loop, because Enter is a key
and not a cursor gesture; `?` still works. The only thing a plain ASCII
caller loses is the highlight, and the numbers are right there. That is the
same split the forms and the file areas already make, and it is why the
single-key-plus-numbers approach was the right foundation.

The one thing to get right: on plain ASCII the "Read all new" row has no bar
on it, so the hint line has to say what Enter does in words. It does:
` Enter reads them. ? help. Q leaves.`

### A slow link

- **Nothing in this subsystem redraws on a timer.** There is no refresh
  screen anywhere in it, by design. DASH's frame-too-big-for-the-buffer
  failure is the reason, and there is no feature here that wants that shape.
- **One message per keypress, and nothing else on the wire.** A caller at an
  effective 2400 baud spends every byte on content. The hint line is printed
  on arrival and on `?`, never above every prompt, which is the one place
  this deliberately diverges from `files`: `files` reprints its hint line
  with every prompt, which is fine for a place you visit twice and wrong for
  a place you sit in for forty messages.
- **The body must go through `startPluginList`**, one output line per
  `rows()` call. A 24 line message wrapped to 40 columns is about 44 rows
  and roughly 1.8 KB with colour, against a 3,072 byte timeline that also
  has to keep 1,700 free before the board will read the caller's keys
  again. Written straight into the timeline it would stall the session; put
  through the list machinery it pages, backpressures and can be aborted.
  This is not an optimisation, it is the difference between working and
  hanging.
- `C` at `[More]` turns paging off for the rest of that message, for
  somebody on a fast link who does not want the interruptions.

---

## What this needs from the core

Three things, none of them large, all of them found by designing against
the existing code rather than by reading the plan.

### 1. A plugin gets no chance to redraw its prompt when a list is aborted

`abortOutput` (`bbs.cpp:2115`) prints "Stopped." and calls `listEnded`,
which for a plugin-owned session sets `SState::Plugin` and returns
(`bbs.cpp:2081`). There is no hook, so the plugin never hears about it and
never draws a prompt. `files` papers over the normal end of a list by
emitting `filesPrompt` as the list's last row (`files.cpp:878`), and that
row is never reached on an abort.

So today, pressing `q` at `[More]` in a file listing leaves the caller
holding a session with no prompt on the screen, in a subsystem whose keys
they cannot see. **This is a live bug in `files`**, and forums would hit it
constantly, because aborting a long message is a normal thing to do rather
than an edge case.

The fix is core-side and small: either an optional `listDone(Session&, bool
aborted)` hook on `Plugin`, or `listEnded` calling the owning plugin's
`onKey` with a synthetic redraw key. The first is cleaner. Handing this back
rather than specifying the code, per the consultant rule.

### 2. There is no output word-wrap helper

Message bodies have to be wrapped at the reader's width, and nothing in
`Term` or `Bbs` does that. It is the same helper the queued "profile text
should word wrap" item needs. Wrap at `rowWidth(s)`, break on spaces, break
a word longer than the width rather than overflowing it, and emit one row
per call so it can drive `rows()` directly.

### 3. `screens/forums`

Add it to SCREENS.md's optional list beside `files`, with the same door
semantics: it plays on the way in, the forum list is drawn underneath, and
entering does not wipe it. **Budget 8 rows at 40 and 8 at 80.** The forum
list is 12 rows with five forums and 15 with eight, and a C64 has 25.

---

## Considered and rejected

- **Threading, in any form.** Settled by the owner, and the terminal
  arithmetic agrees independently: a two-level indent costs 4 of 39 columns
  of subject at 40 and a three-level tree costs 6, and a tree only pays for
  itself when you can see enough of it at once to navigate, which needs
  twenty-odd rows of subjects. A C64 has 25 rows total. The one thing
  threading genuinely gives you, following a conversation, is 80 percent
  covered by the `Re:` subject convention for free.

- **Vote-sorted reading order.** Settled, and mechanically impossible
  anyway: a chronological pointer has no "next" in a score-ordered list.

- **A score column in the listing.** Argued above. My recommendation, not a
  settled point.

- **rn's read-ranges instead of one pointer.** `.newsrc` recorded
  `group: 1-345,347,400-500`, which handles "I read the new ones and then
  three more arrived and then I read those" exactly right. It is a better
  data model. It is rejected here because a range list is variable length,
  and the whole design rests on a fixed-width pointer record that can be
  seek-and-overwritten, which is also what makes the per-day post counter
  free. The single-pointer failure mode is that reading out of order with a
  number marks everything before it read, and on a board where the loop
  reads in order that almost never happens.

- **A separate `NEWSCAN` command**, as WWIV and PCBoard had. Enter at the
  forum list is the scan. A second verb is a second way to do the only thing
  the subsystem is for, and the shell rework just finished taking commands
  off the menu.

- **Per-forum subscribe and unsubscribe** (rn's `u`, Citadel's `<Z>ap`).
  It is per-account per-forum state to save a caller one keypress on a board
  with eight forums. Revisit at thirty.

- **A "mark this one unread" or "keep for later" key**, by analogy with
  mail's `[S]ave`. With a single monotonic pointer, an unread message in the
  middle of a read range is a second data structure. `B` backs up and rereads
  for nothing.

- **A "new since your last call" divider row in the listing.** It carries
  exactly the same information as the `*` marker, which is cheaper and works
  when the listing is scrolled.

- **Clearing the screen between messages.** It would match the file manager
  and it is wrong: the previous message is the context for this one. Views
  clear, scrolls do not.

- **Two-line listing rows at 40 columns** (subject on one row, handle and
  date indented on the next). It would give the subject 35 columns instead
  of 25, at the cost of halving the messages on a screen. The listing's job
  is finding a number to jump to, not reading, so more rows wins.

- **The `-->` marker from the chat room.** Chat needs it because it has no
  prompt character, so a board line is otherwise indistinguishable from
  something a caller typed. The forums have a prompt on every line, so there
  is no ambiguity to remove and the four columns are better spent. Written
  down because somebody will otherwise "fix" the inconsistency.

- **A full-screen visual editor.** No cursor addressing on plain ASCII, and
  constant redraw on a board whose output budget is 3 KB per session.

---

## Open, and genuinely the owner's call

- **Score in the listing, or not.** Recommended not. Argued above.
- **`T`op, the vote view.** Recommended, phase 4 or later. Without some
  view, votes are a number that goes nowhere; with the wrong view, they
  become a leaderboard.
- **Whether `P` or `E` is the post key.** Recommended `P`. `E` is Citadel's
  and it is what a veteran's fingers will reach for; it is also erase in the
  file manager. If Rob wants `E`, then `E` should mean nothing else
  anywhere, including in `files`.
- **8 characters of handle in the 40 column listing.** The alternative is no
  handle and 34 columns of subject. One constant.

---

## Coordination with PLAN-BULLETINS.md

Three places where this document and that one touch. All three are naming
or surface, not data model.

- **"Forum" versus "topic" for the container.** `users.h:99` already settled
  the subsystem's name as Forums and gave the reasoning: "board" already
  means the BBS itself and "messages" blurs into MAIL. The container should
  then be a **forum**, not a topic, in everything a caller sees. In every
  piece of software written in the last twenty years a "topic" is a thread,
  so calling a flat container a topic promises threading to every caller
  under fifty who reads it. The config key can stay whatever the plan wants;
  this is about the words on the screen.

- **Editor commands: `.` or `/`.** The plan uses `.S` `.A` `.L` `.?`, which
  is Citadel's dot convention. This board already has exactly one convention
  for "this line is a command, not text", and it is `/`, from the chat room,
  and callers already know it. Recommend `/`, with a leading space as the
  escape for a line that genuinely starts with one. Two conventions for one
  idea is one too many; either is defensible, but it should be decided once.

- **The first-visit question.** The plan proposes it per topic
  (`General has 218 messages and you have read none.`). Recommend it once,
  at the subsystem, applied to every forum, for the reason given above: five
  questions before the first message read is how a subsystem teaches people
  that it is work.

---

## Implementation order, cheapest and most visible first

1. The forum list and the prompts, at both widths. Everything else hangs off
   knowing where you are.
2. The reading loop: Enter, the message header, the body through
   `startPluginList`, the boundary line, the end of the loop. This is the
   product. It is worth having before posting exists.
3. The core word-wrap helper, which item 2 needs.
4. The listing, `L`, `B`, and jumping by number.
5. The first-visit question and the never-opened-forum banner.
6. Posting and the line editor, then `R`.
7. Voting, with no view.
8. `T`, if it is wanted.

The `listDone` hook should be fixed before item 2, because item 2 is where
aborting a long body becomes an every-session event.
