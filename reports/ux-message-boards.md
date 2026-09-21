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
               one says so and defers. Sources for every historical claim
               are at the end.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v2 or later
 SPDX-License-Identifier: GPL-2.0-or-later
 ===========================================================================
-->

# Forums: the caller-facing design

## The verdict

This is a good subsystem to build and its shape is already decided by three
things that are not up for argument: the file manager set the house style
for a subsystem, the pointer is the feature, and a C64 has 40 columns and
25 rows. Follow those and most of the design writes itself.

The rest comes down to one sentence, and it is what to hold on to when any
detail is in doubt:

> **A caller should be able to read everything new on the board by pressing
> one key, repeatedly, and that key should be Enter.**

Not because one key is cute. Because this board's pager already treats
Enter as "carry on" (`src/core/bbs.cpp:2319`). Enter continues at `[More]`,
Enter shows the next message, Enter carries you across a forum boundary
into the next forum. A caller who learns exactly one thing about the forums
can read the whole board with it, through page breaks and container
boundaries alike, and never find out that those are different mechanisms.

rn stated the principle in 1984 and it is still the best sentence anybody
has written about terminal UI:

> "Typing space to any question means to do the normal thing. You will know
> what that is because every prompt has a list of several plausible commands
> enclosed in square brackets. The first command in the list is the one
> which will be done if you type a space."
> -- rn(1)

Two properties make that work, and both are copied here: the default is
context-dependent but always visible, and it is always the statistically
most likely action.

---

## The trap, first, because it decides the key map

`N` is the obvious key for "next message". Citadel used `<N>ew messages`,
WWIV's read prompt used it, the modern Citadel text client's post-message
prompt is literally `<B>ack <A>gain <R>eply reply<Q>uoted <N>ext <S>top `.
It is wrong here, and this is not taste.

`src/core/bbs.cpp:2318`:

```
case SState::More: {
    bool cont = k == 'y' || k == 'Y' || k == ' ' || k == KEY_ENTER;
    bool non  = k == 'c' || k == 'C';
    bool stop = k == 'n' || k == 'N' || k == 'q' || k == 'Q' || ...
```

At `[More]`, **`N` means stop**. Any message longer than a screen pages, and
a caller taught "press N for the next message" will press N at the page
break and lose the rest of what they were reading. They will not understand
why, because from where they sit they pressed the same key twice and got two
different results.

So:

- **Enter and SPACE are the loop.** Both already continue at `[More]`.
- **`N` is not offered at all**, not even as an alias. An alias that is safe
  at one prompt and destructive at another is worse than no alias.
- `C` at `[More]` means continuous, which is right for a long message on a
  fast link and costs nothing.

This is the single most important finding here and it comes from reading
this board's code, not from any BBS history.

---

## What the precedents did, and what is taken from each

### Citadel, and the two Citadels

Worth separating, because the popular account merges them and gets the
attributions wrong.

**Citadel 2.10 (CP/M, Cynbe ru Taren, public domain).** The whole command
set was twelve lines, and this is the file verbatim:

```
 <C>hat with Sysop
 <E>nter message
 <F>orward retrieval
 <G>oto
 <H>elp
 <K>nown rooms
 <L>ogin
 <N>ew messages
 <O>ld messages
 <R>everse retrieval
 <T>erminate
 <?> for menu (anywhere)
 "." to add options -- see ".Help EXTENDED"
```

`<S>kip`, `<Z>ap`, `<U>ngoto`, `<I>nformation` and `<M>eet user` are **not**
in it. They are Citadel-86 (Hue White, MS-DOS) additions. If somebody says
"1982 Citadel had Skip", they are a decade out.

The prompt was the room name and a `>` with one trailing space:

```
Lobby> 
```

and the `>` was treated as part of the room's name everywhere, including in
running prose: the message header printed `in Lobby>`. That is most of why
Citadel's output read as sentences rather than as fields.

**`<G>oto` is the thing being borrowed.** It moved to the next room with
messages posted since you were last in it, in room-slot order, which made
the order stable session after session. Citadel 2.10 had no message when
there was nothing left; you knew you were finished because you were back in
the Lobby. Citadel-86 added the explicit line, because the silence was
ambiguous.

**The single best detail, and it costs nothing: Citadel echoed the
expansion of the key you pressed.** Press G and the screen shows
`Lobby> goto`. One key in, one word out. The command vocabulary teaches
itself, and the scrollback afterwards reads as a transcript instead of as a
pile of output.

**The other detail worth having: flow control during output.** Citadel-86
took `<P>ause`, `<S>top`, `<N>ext` (abandon this message, start the next)
and `<J>ump` (skip to the next paragraph) as single keys *while the message
was still printing*. At 300 baud that is what made "read everything" safe:
you could bail out of anything boring without leaving the reading flow.

**Taken:** the board decides what is next, not the caller. `G` keeps its
letter and roughly its meaning. The command echo. The `>` as part of the
forum's name. The explicit end-of-loop message, which Citadel had to learn
the hard way.

**Left:** `<Z>ap`. It earned its keep on systems with dozens of rooms, and
Citadel's own help file shows why it is not free: un-forgetting required
typing the room's **full name** at `.Goto`, which is an asymmetric and
unforgiving undo. On a board with five to eight forums, "I do not care
about Swap Shop" is answered by pressing `S` when you reach it: one key, no
per-account state, and no undo to get wrong.

**Left:** the Lobby as the place you land in and read from. This design
lands on a list of forums with unread counts. Citadel made you press
`<K>nown rooms` to see what existed, and its own `listRooms()` sorted rooms
with unread messages first, which tells you the list was what people
actually wanted.

**A deliberate inversion, and it needs saying out loud.** In Citadel,
`<G>oto` marked the room read on the way out and `<S>kip` was the version
that did not. Here it is the other way round: `G` moves on and leaves your
pointer alone, `S` marks the forum read and moves on. That is not
carelessness. Citadel's pointer was per *visit*, so leaving a room was the
event that marked it; this board's pointer advances per *message
displayed*, so leaving can never lose anything. Citadel's reason for two
keys evaporates, and what is left is two keys meaning "move on" and "give
up on this one", which is what the English words Goto and Skip mean. The
letters keep their plain meanings rather than Citadel's mechanics.

### WWIV subs

WWIV made the scan a first-class main-menu command rather than something
you assembled by visiting each sub: `N` scans all subs for new messages,
`Z` does the same non-stop. Subs are addressable by bare number from the
prompt, with `+`/`-`/`>`/`<` to step and `H` to hop by substring. Its read
prompt carried `W` for reply and `@`/`A` for an auto-reply that could
retarget or forward, so you never left the reading loop to respond.

**Taken:** reply from inside the reading loop, as one key. The numbered
container reachable directly.

**Left:** the separate scan verb. Here Enter at the forum list *is* the
scan, so a second command would be a second way to do the only thing the
subsystem exists for.

### PCBoard conferences

PCBoard's Last Message Read pointer was per conference, per user, and
**visible**: the read prompt showed the live message-number range you were
navigating, `(H)elp, (3523-5032), Message Read Command?`, and the caller
could move their own pointer with `R;SET`. `R;S` read everything unread in
the conference and `R;Y;S;ALL` read every unread message addressed to you
across every conference in one go.

**Taken:** the pointer is the caller's, and they can move it. That produced
the `U` key below, which nothing else in this design would have suggested.

**Left:** stacked subcommands (`R;Y;S;ALL`). Powerful and unlearnable, and
the board already decided that a subsystem is single keys rather than a
command language.

Worth noting: PCBoard's prompt text was not fixed at all. It lived in
PCBTEXT, nearly 700 sysop-editable records with separate novice and expert
variants, which is how PCBoard solved the same 300-baud verbosity problem
Citadel solved with single keys. That is the ancestor of the queued
`theme.txt` idea and is worth remembering when it comes up.

### FidoNet offline readers: QWK and Blue Wave

The important structural point is that in an offline reader **the new scan
is server-side**: the door walks every selected area using the BBS's own
per-user pointers and hands you a batch. The reader is presentation only.
Unread is a batch, not a cursor, so you cannot be surprised mid-session by a
new post, and the loop is download / churn / upload rather than "press G".

MultiMail, the surviving multi-format reader, documents Space as "a
combination PgDn/Enter key, allowing you to page through an area with one
key". Forty years on, that is still the answer.

**Taken:** nothing structural. The batch model is the wrong trade for an
online board with ten lines. **Noted for later:** it is XMODEM plus a packet
format, and this board now has YMODEM, so it is a real option once the
online reader exists.

### Usenet: rn, trn, tin, nn

**rn (Larry Wall, 1984)** is where the ergonomic comes from. Its own man
page opens by saying it "was written to be as efficient as possible,
particularly in human interaction", and measures cost in keypresses: the
KILL file exists because it "saves considerable wear and tear on your 'n'
key". The bracket-default contract is quoted at the top of this document.
The implementation is literal: `setdef()` pushes the first character of the
bracket string back into the input queue when you press space, and unless
`STRICTCR` is compiled in, **newline is treated identically to space**.

The default shifted with context, which is the part that turns a prompt into
a loop: `[npq]` mid-group means space is "next unread article", but at the
end of a group the string becomes `[qnp]` and the same space leaves the
group.

**tin** put a selector screen in front of all this: a full-screen list with
a cursor, unread counts and group descriptions, and it split rn's overloaded
space into two keys, `<CR>` = act on what the cursor is on and `<TAB>` = just
take me to the next unread thing. It also has a `d` key to toggle the
description column off, which is the same 40-column problem this board has.

**nn** inverted the loop: a menu of subject lines, pick with letter keys,
then space commits and reads only the picks; everything unpicked is marked
read. "No news is good news." Its menu had five selectable layouts, and
layouts 2 and 3 drop the author column entirely, which is exactly the
40-column trade discussed under the listing below.

**Taken from rn:** the context-dependent default, and the catch-up key `c`,
which rn confirmed with `Do you really want to mark everything as read?
[yn#h]` and nothing else confirmed.

**Taken from tin:** the forum list as the hub, with unread counts and
descriptions, and dropping the description at narrow widths rather than
truncating everything.

**Considered from nn:** its layouts are the precedent for what to drop at 40
columns. Its selection model is not taken: on a board where the right answer
is almost always "read all of it", making somebody pick first is work.

**Left from rn:** read-ranges in `.newsrc`, and the `m`/`M` mark-unread keys
that depend on them. Argued under "Considered and rejected".

**Left from trn and tin:** threading, the thread selector and the article
tree. Settled by the owner, and trn's tree is drawn in the upper right of an
80 column header, which is the first thing that does not exist at 40.

---

## The shape: two levels and one loop

Two places, and the caller always knows which one they are in because the
prompt says so.

```
Forums>            you are at the list of forums
[F2] C64>          you are inside forum 2, which is called C64
```

The bracketed tag appears only when you are inside something. That is the
device `files` uses (`[S1] Files>`), with one change: the forum's **name**
is in the prompt, not the subsystem's, because in a reading session that
crosses forums the name is what you need and the subsystem name never
changes. The `>` reads as part of the name, Citadel-style, which is what
makes `End of C64. Next: Swap Shop` read as a sentence a few lines later.

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
        U ............... from the one on screen, unread again
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

The one thing not reachable by Q is the `[More]` pager, where `q` and ESC
abort the output and land back at the forum prompt. Same key, same meaning,
one layer down. Subject to the core bug in "What this needs from the core".

### Every key echoes its word

Citadel's trick, and it costs four bytes:

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
[F2] C64> goto

 Swap Shop                       3 new 

410  Rob  18 Sep 20:44
Anyone want a spare 1571?
```

`G` prints `goto`, `L` prints `list`, `P` prints `post`, `S` prints `skip`,
`U` prints `unread`, a digit prints itself (which `files` already does).
It costs no rows, because the echo lands on the prompt line, and it makes
the scrollback a transcript.

**Enter and SPACE echo nothing.** The echo exists to disambiguate a single
letter; Enter has nothing to disambiguate and the next message appearing is
unmistakable. Printing `next` forty times in a session would be noise in
exactly the place where somebody is trying to read prose.

---

## The keys

Inside a forum:

| Key | Does | Where it comes from |
|---|---|---|
| `Enter` | the next message you have not read; at the end of a forum, into the next forum with unread | rn's bracket default; already the continue key at this board's `[More]` |
| `SPACE` | identical | rn; MultiMail's "combination PgDn/Enter key"; already continues at `[More]` |
| `B` | back up one message and show it again | the Citadel text client's `<B>ack`; rn's `-` |
| `L` | the numbered listing | `files` uses `L` for exactly this; rn's `=` listed unread subjects |
| a digit | read that message; digits accumulate so 1234 is reachable | `files`, where a digit picks a file; WWIV, where a digit picks a sub |
| `P` | post a new message here | WWIV |
| `R` | reply to the message on screen, subject prefilled `Re:` | WWIV's `W`/`A`, the Citadel client's `<R>eply` |
| `+` `-` | vote the message on screen up or down | new; no precedent worth citing |
| `U` | from the message on screen onward, unread again | PCBoard's user-editable Last Message Read pointer; rn's `m` at coarser grain |
| `G` | go to the next forum with unread | **Citadel `<G>oto`**, kept deliberately |
| `S` | skip: mark this forum read and move on | Citadel-86 `<S>kip`, with the marking sense inverted, argued above |
| `?` | the key list, on the screen it applies to | `files` |
| `Q` `ESC` | back to the forum list; again to leave | `files` |

At the forum list:

| Key | Does | Where it comes from |
|---|---|---|
| `Enter` `SPACE` | start the loop; on a pointing terminal, open whatever the bar is on, and the bar starts on the "read all new" row | tin's selector plus rn's default |
| a digit | open that forum at its first unread; `0` means ten | `files` |
| cursors | move the bar | `files` |
| `G` | jump to the next forum with unread | Citadel |
| `C` | catch up: mark every forum read. **Confirms.** | rn's `c`, tin's `C`, both of which confirmed |
| `?` | the key list | `files` |
| `Q` `ESC` | leave the forums | `files` |

Keys deliberately **not** used, and why:

- **`N`**, because at `[More]` it means stop. Covered above.
- **`E`** for enter-a-message, Citadel's key. `E` means erase in the file
  manager, and one letter should not mean "create" in one subsystem and
  "destroy" in another. `P` is unambiguous and is what the last thirty years
  has called it.
- **`Z`**, Citadel-86's forget-room. Argued above.
- **`A`**, which in the file manager means approve. Left free in case forum
  moderation ever wants a key, so it can mean the same thing in both.
- **`-` for "previous message"**, which is rn's. It collides with the
  downvote, and a vote is something a caller does far more often than
  stepping backwards. `B` takes the backwards job, with the Citadel text
  client as precedent.
- **Stacked subcommands** (`R;Y;S;ALL`), PCBoard's. A command language
  inside a subsystem that is otherwise single keys.

### Why `S` does not confirm and `C` does

Neither destroys a message. Both only move a pointer, and everything they
skip stays reachable by `L`, by number, and now by `U`. The difference is
magnitude and recovery: `S` gives up one forum you are standing in and
looking at, `C` gives up the whole board's unread from a screen that is one
keystroke from the shell. rn confirmed its catch-up for the same reason and
confirmed nothing else.

This is a judgement about scale rather than about destruction. Written down
as such so nobody "fixes" the inconsistency later.

### `U`, and why it earns a key

trn's source contains a small piece of interface manners worth stealing:
after the catch-up command, if you hit `u` (unsubscribe) when you meant `y`,
it prints `(If you meant to hit 'y' instead of 'u', press '-'.)`. The
principle is that a single-key action which cannot be undone should tell
you the way back.

There is no way back from `S` or `C` unless one exists, so one should exist,
and PCBoard already showed the shape: the pointer belongs to the caller and
they may move it. `U` sets the pointer to just before the message on screen.
Everything from there on becomes unread again.

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
[F2] C64> unread
412 onward is unread again.
[F2] C64> 
```

It costs one `uint16_t` write and no new state, because the pointer only
ever moves backwards here and there is no exception set to maintain. It
gives back three things at once: undo for `S`, undo for `C`, and "I want to
read this forum again from here". With no message on screen it says so
rather than guessing.

`S` says what it did and how to get back:

```
[F2] C64> skip
C64 marked read. L still lists it all.
```

---

## The screens

Every mock below was generated to the character grid and measured. The ruler
above each is real and no line exceeds `rowWidth`, which is `cols - 1`
(`src/core/bbs_shell.cpp:430`). Colour is described in the notes, not drawn.

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
  a dashed rule on plain ASCII, like every other list on the board.
- The "Read all 12 new messages" row is where the highlight bar sits when
  the screen is drawn. It is the only row without a number, which is what
  makes it visibly a different kind of thing from a forum.
- Unread counts right-aligned in a 3 wide column at the end of the name
  block. **Zero shows as nothing**, and that row is drawn in grey. A column
  of `0`s reads as a fault. Citadel did the same: its arrival banner printed
  the new count only when it was non-zero.
- 12 rows with five forums, one more per extra forum. With the eight the
  CONFIG page can hold it is 15 rows, leaving 10 for `screens/forums` on a
  25 row C64. Budget the door screen at 8 rows and it always fits.
- When nothing is new: the title bar right text reads `nothing new`, the
  action row becomes a grey `Nothing new since your last call.`, the bar
  starts on forum 1, and the hint line becomes
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
a different answer from the one `files` gave. `files` packs its areas into a
grid because an area is nothing but a name. A forum has a description worth
reading and a last-post line worth scanning, so the width goes to content.
That is also what tin's group selector did: number, unread count, name,
description.

- **Description** appears at 64 columns and up, sized
  `max(20, min(34, whatever is left))`. It is the one place a sysop can say
  what a forum is for, which otherwise has nowhere to live. tin's `d` key
  toggled exactly this column, which is the precedent for dropping it at 40
  rather than truncating everything.
- **`New` and `Last post` get a header row** at 64 and up. A bare integer
  between a description and a handle is ambiguous without one, and plain
  ASCII has no colour to lean on.
- **Last post** is handle (9) and date (6), and comes from RAM: the plugin
  already holds the highest message number per forum to compute the unread
  counts, and the last poster and time beside it is about 24 bytes per
  forum. **Nothing on this screen touches the card.** Citadel made the same
  call for the same reason, keeping new-message detection entirely in the
  user record so that `<G>oto` needed no disk access at all and never
  stalled on a floppy-based CP/M box.

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

19 rows on a 25 row screen, and that is three whole messages. What makes it
fit:

- **The screen is never cleared between messages.** Entering the subsystem
  clears, the listing clears, the help clears, the editor clears. The
  reading loop does not, because it is a scroll and not a view: the previous
  message is the context for this one, and somebody who wants to glance back
  at what they just read should be able to. This is the one place where the
  "screen clears bro" rule from the file manager does **not** apply, and it
  needs saying out loud because somebody will try to make it consistent.
- **The prompt line is the separator.** One blank line *above* each message
  header and none below the body, so the overhead between two messages is
  two rows. Putting the blank above the header rather than below the body is
  what makes the grouping read correctly.
- **The message header is compact and left-aligned at every width.** Number,
  handle, date, and the vote tally when there is one. It does not stretch to
  the right margin: it is a label above a paragraph, not a table row, and a
  single line with a 50 column hole in it looks like a bug. Citadel's header
  was assembled the same way, as short left-aligned pieces
  (`"   %s " "from %s" " in %s>"`), and it reads as prose because of it.
- The forum title bar appears **only** on arriving in a forum or crossing
  into one, never per message.
- Handle shown to 14 characters at 40 and 20 at 64 and up. At 40 with a five
  digit message number and a 14 character handle the header is exactly 39
  columns, which is the worst case and it fits.

Colour, consistent with the rest of the board: header row grey, subject
white or light green (it is what the eye should land on), body light grey,
vote tally yellow when positive and light red when negative, prompt cyan
with the `[F2]` tag in light green, matching `files`.

**What this loses against Citadel, and it is worth knowing.** Citadel-86 let
you press `<N>ext` or `<J>ump` *during* output to abandon a message or a
paragraph. This board's pager offers `n`/`q` to stop, which drops you at the
forum prompt, from which Enter starts the next message. So Citadel's one key
is two keys here, with a visible intermediate state. That is an acceptable
trade and not worth changing the core pager for, but if somebody later asks
why abandoning a boring message feels clumsy, this is the answer.

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
C64, and a message typed at 35 columns on a C64 should not sit in a narrow
stripe down the left of an 80 column screen. Wrapping at read time is the
only thing that can be right for both, because the reader's width is not
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
  exists in a subsystem whose point is a pointer. tin used `+` for the same
  job in its group index and it is the first thing the eye picks up.
- **The message number column is as wide as the forum's largest number**,
  minimum 3. Freezing it at 5 wastes two columns of subject for the years
  before any board gets there, and this board has been bitten by frozen
  column widths more than once.
- Handle 8, subject the rest: 25 today and 23 once numbers reach five
  digits. That is the real cost of 40 columns and there is no clever way out
  of it. The alternative is nn's layout 3, which drops the author column
  entirely and gives the subject 34. On a board where you know everybody by
  name the handle is worth keeping, and it is one constant to change if Rob
  disagrees.
- **A deleted message keeps its row and never carries the unread mark.** It
  is skipped by the reading loop and shown here, because a conversation with
  a silent gap reads as the board being broken while a visible `(deleted)`
  reads as moderated.
- **`L` opens on the page containing the pointer**, not at the top and not
  at the end. In a forum with 400 messages the top is useless. A little
  context above the mark and the unread below it is what somebody wants.
  With nothing unread, the last page.
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

The width buys the handle its full 14 and a date column; subject gets 44.
No vote column: see the voting section.

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
U       unread again from this one
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
U        unread again, from this one on
?        this
Q  ESC   back to the forum list, again to leave
-------------------------------------------------------------------------------
```

15 rows, so it fits one page on a 24 row terminal with the prompt, which is
the rule the shell rework established: a help screen that pages every time
teaches people to hammer through it. Two texts per row, the short one under
60 columns, which is the split `files` already uses.

Rows a caller cannot use are not printed, the way `filesHelp` hides the
staff rows. Somebody who may not post does not see `P` or `R`; somebody who
may not vote does not see `+` and `-`.

Citadel's `<?>` worked "anywhere", at every level, and so does this one. The
forum list has its own, shorter:

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

The `cursors` row prints only on ANSI and PETSCII, which is the test `files`
already uses (`canPoint`).

**What this deliberately does not do:** reprint the option list after every
message. The modern Citadel text client does, verbatim
`<B>ack <A>gain <R>eply reply<Q>uoted <N>ext <S>top `, and rn's bracket list
is the same instinct. At 40 columns that line is most of a row, on every
message, against a 3 KB output budget. The trade taken here is that the hint
line is printed on arrival and `?` is always one key away, and the cost is
that a caller has to press `?` once rather than reading it off every prompt.
The one place the default *is* spelled out is the forum boundary, where it
changes, which is exactly where rn spelled it out by flipping `[npq]` to
`[qnp]`.

---

## "New since I left", end to end

The headline feature, so here is the whole of it, with the keystroke count.

**At login**, one line, only when there is something:

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
which is already wired up (`src/core/users.cpp:66`).

**The forum list** appears, bar sitting on `Read all 12 new messages`.

**Enter.** From here the caller makes no further decisions:

```
[F2] C64> 
```

Enter, message. Enter, message. At a page break, `[More] Y/n/c`, and Enter
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

and the caller is back on the forum list, showing zeroes.

**Keystroke count: `FORUMS`, Enter, then Enter once per message.** For a
caller whose `Start` is Forums it is Enter, then Enter per message. There is
no way to make that shorter and no reason to try.

### Why the boundary pauses instead of running straight on

One line and one extra keypress per forum, and it buys the thing that makes
the loop comprehensible: you always know which forum you are in. Without it
somebody reads eleven messages and has no idea that four were somewhere
else. Citadel made the boundary an explicit `<G>oto`; this makes it an
explicit Enter, which is the same beat without a second key to remember.
It is also precisely rn's trick of changing what the default means at the
end of a group, said in words instead of in a bracket list.

`S` at the boundary skips the forum about to be offered. `Q` stops.

### The end must say so

Citadel 2.10 ended the loop by silently leaving you in the Lobby, and
Citadel-86 added `There are no more rooms with unread messages.` because the
silence was ambiguous. Take the fixed version, not the original: say `All
caught up`, and put the caller somewhere, not nowhere.

### Things that must be true for this to feel right

- **A message posted while you are reading appears in your loop.** "Next
  unread" is computed each time, not precomputed into a list at the start.
  On a ten line board where two people read while one posts, this happens
  constantly and should just work.
- **The pointer advances when a message is displayed**, not when the caller
  moves past it. A dropped line costs one message. Every reader here behaves
  that way and the alternative is a second piece of state.
- **The pointer is written to the card on leaving a forum, on leaving the
  subsystem, and at logoff. Never per message.** A card write per message
  puts synchronous card work in the middle of the reading loop, which is
  exactly the shape that produced the 126 ms stall already on the books.
- **Nothing on the forum list touches the card.** Counts come from RAM.
  Citadel's whole per-user read state was designed around answering "is
  there anything new" with no disk access at all, and that is the right
  instinct on a microcontroller too.
- **A listing page costs one read, not one read per row.** The index is
  fixed-width, so a page is a seek and a block. `rows()` is called once per
  line by the core, so the plugin must have the page in hand before the
  first row. This is the hard rule from the file listing experience and it
  is the only performance requirement this design actually imposes.

---

## The first time somebody uses it

This is where these systems feel worst, because the honest state is "431
messages, all unread" and the honest behaviour is to show them all.

**Ask once, at the top, on the first ever visit, and apply the answer to
every forum.**

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
  caller has read a word, which teaches them that this subsystem is admin
  rather than reading.
- **`L` is the default** and Enter takes it. A first-time caller wants to
  see what the place is like, not read a year of backlog. Ten in each across
  five forums is about fifty messages, a representative evening, and it is
  recoverable in both directions: `B` backs into what was skipped, `L` lists
  all of it, `U` re-arms anything.
- `R` is there because some people genuinely want the lot. `S` is there
  because some want to start clean. rn's catch-up offered a numeric third
  option, "mark all but the last N as read", which is the same idea as `L`
  with the number exposed; a fixed 10 is the right simplification for a
  board whose forums hold tens of messages rather than thousands.
- **ESC is `L`**, not an abort. There is no sensible cancel for a question
  that must be answered before anything can happen.
- **It never appears again.** Once there are pointers, there is no first
  visit.

### A forum added after the caller's first visit

Same problem, smaller, and it gets no question:

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
[F2] C64> 

 Swap Shop                       3 new 

New to you. Showing the last 10 of 96.
B backs up, L lists the lot.
```

One line of explanation and two ways into the backlog. A question here would
interrupt a reading loop to ask about a forum the caller has not seen and
cannot have an opinion about.

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
Silently clamping the pointer would mean the board quietly decided something
on the caller's behalf and did not mention it.

### A guest

A guest's pointer lives in RAM for the length of the call, so within one call
everything works exactly as it does for an account. On the way in, once:

```
Guest pass: the board will not remember where you got to.
```

Guests get no first-visit question, because there is nothing to write down.
They land on the forum list with the "last 10 in each" behaviour applied
silently, which is the sensible default and costs them nothing.

---

## Posting and replying

### The editor is a numbered line editor, and it has to be

There is no full-screen editor on this board and there should not be one:
plain ASCII has no cursor addressing, `LineEditor` is one line
(`src/core/editor.h:47`), and `FF_TEXTAREA` is four fixed rows inside a
`Form`, which is a different thing for a different job. A visual editor
would also redraw constantly, which is the one thing this board's output
budget cannot pay for.

The numbered line editor with single-letter commands is the universal BBS
shape. Renegade's `MAIL1.PAS` line editor took `A C D F I L M O P Q R S T U
Z ?`; every system in this family had something like it.

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

- **The gutter is 4 columns** (`NN> `), so typed width is
  `min(BBS_LINE_MAX, rowWidth - 4)`: 35 on a C64 and 72 from 76 columns up.
  A C64 caller types short lines and a PC caller types long ones, and the
  output wrapper makes both readable to everybody.
- **A blank line ends input** and opens the decision line. To get a blank
  line *inside* a message, type a single space. Document that one trick in
  COMMANDS.md and nowhere else: people either need it or never think about
  it.
- **`C` continues** from where it left off, which is what makes the blank
  line safe: somebody who pressed Enter one time too many is one key from
  carrying on.
- **`E` edits a line by number and prefills it**, using
  `LineEditor::replace()`, which already exists for history recall. Retyping
  from scratch is the obvious cheap implementation and the wrong one: the
  usual reason to edit line 3 is one word.
- **`L` relists** with numbers, because on a 25 row screen a long message has
  scrolled off by the time you finish it.
- **`A` aborts, and confirms**, because a message somebody just spent five
  minutes typing is the most expensive thing in this subsystem to lose.
  Nothing else in this design confirms; this does.
- `/s`, `/a`, `/l` also work at the start of a line, for muscle memory. See
  the coordination note about `.` versus `/`.

Sourcing note: I could not verify from primary sources that Citadel's own
message entry used blank-line-then-Save/Abort/Continue, although it is
widely described that way. The flow above is recommended on its merits, not
on that attribution.

### The subject is required

The subject *is* the listing. An empty one makes a row nobody can use. Empty
at the prompt re-asks once, with `A subject is how people find it.`, and ESC
at the subject prompt aborts the whole post before anything is typed, which
is the cheapest place to let somebody change their mind.

Subject is 59 characters (the index field), shown to 25 at 40 columns and 44
at 80.

### Reply

`R` is a new message with the subject prefilled `Re: <original>`, cut to fit
59. Nothing is quoted.

**No automatic quoting**, and that is a decision rather than laziness. A
quote block at 40 columns with a `> ` prefix costs 2 of 35 columns, has to
be re-wrapped to the replier's width and then again for every reader, and
usually produces a message that is 80 percent quote. On a board where the
message you are replying to is three lines up the screen, quoting buys
nothing.

If it is ever wanted, the shape is a `Q` key **inside** the editor that
pulls the original in as `> ` prefixed lines the caller then cuts down with
`E`. Opt-in, and the editing burden lands on whoever chose it. The modern
Citadel client offers exactly that as a separate command (`reply<Q>uoted`)
rather than as the default reply, which is the same conclusion.

`R` with nothing on screen says `Read something first. P posts a new one.`

### After a save

```
Posted as 416 in C64.
[F2] C64> 
```

One line, straight back to the prompt with the loop intact. Posting is not a
reason to be thrown anywhere.

A caller over a cap is told which one and what to do, in the file manager's
style rather than "denied":

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
- **The same key again removes your vote; the opposite key changes it.** A
  vote you cannot take back is a vote people are reluctant to cast. One
  record either way.
- Feedback is one short line with the new tally, so the caller sees the
  number move. That is the whole reward loop and it has to be immediate.
- A refusal says the real reason:

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

- **In the message header when it is not zero**, as a signed net figure:
  `412  Daytona  19 Sep 21:14  +3`. Zero prints nothing, not `0`.
- **Net only, not `+4 -1`.** On a board with ten lines a downvote count is
  small enough to be personal. "One person downvoted this" on a board where
  four people are awake is an accusation with a very short list of suspects.
  Net hides the denominator, which is the kindest thing a small board can do
  with a score.
- **No column in the listing.** Three reasons, in order of weight:
  1. It is a number you can see and cannot act on. Reading order is
     chronological by design, so a score column is an invitation to ask for
     sorting, and sorting is what this design rejects. A UI element whose
     only possible next step is a ruled-out feature should not be drawn.
  2. At 40 columns it costs 4 of 25 subject columns. The subject decides
     whether somebody reads a message; the score does not.
  3. A visible ranking in the index turns a conversation into a
     leaderboard. That is the dynamic threading plus vote-sorting produces,
     and it is what this board is deliberately not building.

**Give the tally one place where it is useful instead**, explicitly not the
reading loop: a `T` key at the forum list showing the highest scoring
messages of the last fortnight, drawn with the same paged list, from which a
number reads one. That answers "what is voting for" without letting the
score touch the pointer, and it is one screen.

Mark it **phase 4 or later**. Voting can ship with no view at all and be
fine; it cannot ship with a sorted index and then have it taken away.

**This is the one item here that is genuinely Rob's call**, and the
alternative is coherent: show the score in the listing, accept that it is
decoration, and take the columns off the C64. I do not recommend it, and if
it goes in, it should go in at 64 columns and up only.

---

## How it degrades

### PETSCII at 40 by 25

Designed first, and everything above is measured at it. What changes from
80:

- The forum list loses the description and last-post columns and keeps the
  count. 12 rows with five forums.
- The listing loses the date column; subject drops from 44 to 25.
- Every hint line has a short form, at the `cols >= 60` test `files` already
  uses.
- The editor types 35 columns instead of 72.
- Help gets its short texts and still fits one page.

Everything else is identical, including every key. **No key and no screen
exists only at 80.**

PETSCII has reverse video (0x12/0x92) and cursor movement, so it gets the
highlight bar on the forum list exactly as ANSI does. It does **not** have
`eolClear`, so the bar redraw pads with spaces, which is why the forum list
must redraw only its own rows rather than the whole screen. `files` solved
this with `Draw::Again` (`src/plugins/files.cpp:924`) and the same approach
applies unchanged: move the bar, redraw the menu rows in place, leave the
door screen above alone.

### Plain ASCII

No cursor addressing, no reverse video, no colour. `cls` prints two blank
lines (`src/core/term.cpp:327`), `rowTitle` falls back to a dashed rule, and
there is no bar.

Nothing in the design depends on any of that. The forum list is numbered and
the hint line says so; Enter still starts the loop, because Enter is a key
and not a cursor gesture; `?` still works; the command echo still tells you
what you pressed. The only loss is the highlight, and the numbers are right
there. That is the same split the forms and the file areas already make and
it is why single-keys-plus-numbers was the right foundation.

The one thing to get right: on plain ASCII the "Read all new" row has no bar
on it, so the hint line must say what Enter does in words. It does:
` Enter reads them. ? help. Q leaves.`

### A slow link

- **Nothing in this subsystem redraws on a timer.** There is no refresh
  screen anywhere in it, by design. DASH's frame-too-big-for-the-buffer
  failure is the reason, and no feature here wants that shape.
- **One message per keypress and nothing else on the wire.** The hint line
  prints on arrival and on `?`, never above every prompt. This is the one
  place the design deliberately diverges from `files`, which reprints its
  hint with every prompt: fine for a place you visit twice, wrong for a
  place you sit in for forty messages.
- **The body must go through `startPluginList`**, one output line per
  `rows()` call. A 24 line message wrapped to 40 columns is about 44 rows
  and roughly 1.8 KB with colour, against a 3,072 byte timeline that must
  also keep 1,700 free before the board will read the caller's keys again.
  Written straight into the timeline it would stall the session; through the
  list machinery it pages, backpressures and can be aborted. Not an
  optimisation: the difference between working and hanging.
- `C` at `[More]` turns paging off for the rest of that message, for
  somebody on a fast link who does not want the interruptions.

---

## What this needs from the core

Three things, none large, all found by designing against the existing code
rather than by reading the plan.

### 1. A plugin gets no chance to redraw its prompt when a list is aborted

`abortOutput` (`src/core/bbs.cpp:2115`) prints "Stopped." and calls
`listEnded`, which for a plugin-owned session sets `SState::Plugin` and
returns (`src/core/bbs.cpp:2081`). There is no hook, so the plugin never
hears about it and never draws a prompt. `files` papers over the *normal*
end of a list by emitting `filesPrompt` as the list's last row
(`src/plugins/files.cpp:878`), and that row is never reached on an abort.

So today, pressing `q` at `[More]` in a file listing leaves the caller
holding a session with no prompt on screen, in a subsystem whose keys they
cannot see. **This is a live bug in `files`.** Forums would hit it
constantly, because aborting a long message is normal rather than an edge
case, and it is also the two-key substitute for Citadel's `<N>ext`.

The fix is core-side and small: either an optional
`listDone(Session&, bool aborted)` hook on `Plugin`, or `listEnded` calling
the owning plugin's `onKey` with a synthetic redraw key. The first is
cleaner. Handed back rather than specified, per the consultant rule.

### 2. There is no output word-wrap helper

Message bodies must be wrapped at the reader's width and nothing in `Term`
or `Bbs` does that. It is the same helper the queued "profile text should
word wrap" item needs. Wrap at `rowWidth(s)`, break on spaces, break a word
longer than the width rather than overflowing it, and emit one row per call
so it can drive `rows()` directly.

### 3. `screens/forums`

Add it to SCREENS.md's optional list beside `files`, with the same door
semantics: it plays on the way in, the forum list draws underneath, and
entering does not wipe it. **Budget 8 rows at 40 and 8 at 80.** The forum
list is 12 rows with five forums and 15 with eight, and a C64 has 25.

---

## Considered and rejected

- **Threading, in any form.** Settled by the owner, and the terminal
  arithmetic agrees independently: a two-level indent costs 4 of 39 columns
  of subject at 40 and a three-level tree costs 6, and a tree only pays when
  you can see enough of it at once to navigate, which needs twenty-odd rows
  of subjects. A C64 has 25 rows in total. trn drew its article tree in the
  upper right of an 80 column header, which is the first thing that does not
  exist here. The one thing threading genuinely gives, following a
  conversation, is mostly covered by the `Re:` subject convention for free.

- **Vote-sorted reading order.** Settled, and mechanically impossible
  anyway: a chronological pointer has no "next" in a score-ordered list.

- **A score column in the listing.** Argued above. My recommendation, not a
  settled point.

- **rn's read-ranges instead of one pointer.** `.newsrc` recorded
  `group: 1-78,80,85-90`, a sorted set of read article numbers rather than a
  high-water mark, and that is why rn could offer `m` (mark this one unread
  again) and let you jump about with `p`, `-`, numbers and `/`-search
  without losing track. It is a better data model. It is rejected here
  because a range list is variable length, and this design rests on a
  fixed-width pointer record that can be seek-and-overwritten, which is also
  what makes the per-day post counter free. The failure mode of a single
  pointer is that jumping ahead by number marks everything before it read,
  and on a board where the loop reads in order that almost never happens.
  `U` covers the case where it does.

- **A separate `NEWSCAN` command**, as WWIV and PCBoard had. Enter at the
  forum list is the scan. A second verb is a second way to do the only thing
  the subsystem is for, and the shell rework has just finished taking
  commands off the menu.

- **Per-forum subscribe and unsubscribe** (rn's `u`, Citadel-86's `<Z>ap`).
  Per-account per-forum state to save one keypress on a board with eight
  forums, plus an undo that Citadel itself got wrong (un-forgetting needed
  the room's full name typed at `.Goto`). Revisit at thirty forums.

- **A message-granularity "mark unread" key.** rn had `m` and `M`, and
  needed `.newsrc` ranges to support them. `U` gives the useful 90 percent
  at forum granularity for one `uint16_t`.

- **A "new since your last call" divider row in the listing.** It carries
  exactly the information the `*` marker carries, which is cheaper and works
  when the listing is scrolled.

- **Clearing the screen between messages.** It would match the file manager
  and it is wrong: the previous message is the context for this one. Views
  clear, scrolls do not.

- **Two-line listing rows at 40 columns** (subject on one row, handle and
  date indented under it). It would give the subject 35 columns instead of
  25 at the cost of halving the messages on a screen. The listing's job is
  finding a number to jump to, not reading, so more rows wins. nn's layout 3
  is the better answer if the subject ever has to win: drop the author
  column outright rather than adding a row.

- **The `-->` marker from the chat room.** Chat needs it because it has no
  prompt character, so a board line is otherwise indistinguishable from
  something a caller typed. The forums have a prompt on every line, so there
  is no ambiguity to remove and the four columns are better spent. Written
  down because somebody will otherwise "fix" the inconsistency.

- **Reprinting the option list after every message**, as the modern Citadel
  client does. At 40 columns it is most of a row per message against a 3 KB
  output budget.

- **A full-screen visual editor.** No cursor addressing on plain ASCII, and
  constant redraw on a board whose output budget is 3 KB per session.

- **An offline reader (QWK or Blue Wave).** The board now has YMODEM, so it
  is genuinely possible, and the batch model is the wrong trade for a ten
  line online board. Worth revisiting only after the online reader exists,
  as an addition rather than an alternative.

---

## Open, and genuinely the owner's call

- **Score in the listing, or not.** Recommended not. Argued above.
- **`T`, the vote view.** Recommended, phase 4 or later. Without some view,
  votes are a number that goes nowhere; with the wrong view they become a
  leaderboard.
- **Whether `P` or `E` is the post key.** Recommended `P`. `E` is Citadel's
  and is what a veteran's fingers reach for; it is also erase in the file
  manager. If Rob wants `E`, then `E` should mean nothing else anywhere,
  including in `files`.
- **8 characters of handle in the 40 column listing**, versus nn's answer of
  no handle and 34 columns of subject. One constant.

---

## Coordination with PLAN-BULLETINS.md

Three places where this document and that one touch. All three are naming or
surface, not data model.

- **"Forum" versus "topic" for the container.** `src/core/users.h:99` already
  settled the subsystem's name as Forums, with the reasoning: "board"
  already means the BBS itself and "messages" blurs into MAIL. The container
  should then be a **forum**, not a topic, in everything a caller sees. In
  every piece of software written in the last twenty years a "topic" is a
  thread, so calling a flat container a topic promises threading to every
  caller under fifty who reads it. The config key can stay whatever the plan
  wants; this is about the words on the screen.

- **Editor commands: `.` or `/`.** The plan uses `.S` `.A` `.L` `.?`, which
  is Citadel's dot convention, and Citadel's own help was explicit that a
  space, a comma or a slash could be substituted for the period. This board
  already has one convention for "this line is a command, not text", and it
  is `/`, from the chat room, and callers already know it. Recommend `/`,
  with a leading space as the escape for a line that genuinely starts with
  one. Either is defensible; two conventions for one idea is one too many.

- **The first-visit question.** The plan proposes it per topic
  (`General has 218 messages and you have read none.`). Recommend it once,
  at the subsystem, applied to every forum, for the reason above: five
  questions before the first message is read is how a subsystem teaches
  people that it is work.

---

## Implementation order, cheapest and most visible first

1. The forum list and the prompts, at both widths. Everything else hangs off
   knowing where you are.
2. The reading loop: Enter, the message header, the body through
   `startPluginList`, the command echo, the boundary line, the end of the
   loop. This is the product and it is worth having before posting exists.
3. The core word-wrap helper, which item 2 needs.
4. The listing, `L`, `B`, jumping by number, and `U`.
5. The first-visit question and the never-opened-forum banner.
6. Posting and the line editor, then `R`.
7. Voting, with no view.
8. `T`, if it is wanted.

The `listDone` hook should be fixed before item 2, because item 2 is where
aborting a long body becomes an every-session event.

---

## Sources

Historical claims above are from these. Where a source is code, the string
quoted is a literal from it.

**Citadel**

- Citadel 2.10, CP/M, Cynbe ru Taren, public domain:
  https://github.com/jboone/citadel-cpm
  (`MAINOPT.MNU`, `READOPT.MNU`, `GOTO.HLP`, `EXTENDED.HLP`, `210ROOMA.C`,
  `210ROOMB.C`, `210MSG.C`, `210CTDL.H`, `210LOG.C`)
- Citadel-86, Hue White: https://github.com/neckro/citadel-86
  (`dist/single.hlp`, `dist/skip.hlp`, `dist/forget.hlp`, `dist/novflow.hlp`,
  `dist/dot.hlp`, `port/ctdl.c`, `port/rooma.c`, `man/hack3.man`)
- Modern Citadel text client, for the post-message prompt and `msg #` line:
  https://github.com/mingodad/citadel `textclient/src/messages.c`
- https://en.wikipedia.org/wiki/Citadel_(software)
- "On Designing a WebCit": https://zork.net/cit/citanews.html

**Usenet readers**

- rn(1): https://www.unix.com/man_page/bsd/1/rn/
- trn source, the closest surviving code to rn's:
  https://web.mit.edu/kolya/sipb/trn/sun4/ (`trn.c`, `art.c`, `ng.c`,
  `term.c`, `util.c`, `rt-select.c`, `trn.1`)
- newsrc(5), Eighth Edition Unix:
  https://www.tuhs.org/cgi-bin/utree.pl?file=V8/usr/man/man5/newsrc.5
- tin(1): https://manpages.debian.org/testing/tin/tin.1.en.html
- nn(1): https://manpages.ubuntu.com/manpages/resolute/man1/nn.1.html

**BBS packages**

- WWIV docs: http://docs.wwivbbs.org/en/wwiv500/main_menu/ and
  https://wwivbbs.readthedocs.io/en/latest/menus/commands/ ; source
  `bbs/msgscan.cpp`: https://github.com/wwivbbs/wwiv
- PCBoard manual, transcribed: https://kuehlbox.wtf/wiki/commands:user:start
  (the `R`, `J` and `U_LMR` pages in particular)
- Offline readers: https://en.wikipedia.org/wiki/QWK_(file_format) ;
  MultiMail manual: https://wmcbrine.com/MultiMail/MANUAL.html
- Renegade source, `SOURCE/MAIL1.PAS` and `SOURCE/MAIL2.PAS`:
  https://github.com/Renegade-Exodus/Renegade

**Deliberately not claimed.** There is no primary source for the folk phrase
"you just keep pressing G"; the behaviour is sourced, the quotation is not.
Citadel's message-entry save/abort/continue flow could not be verified from
primary sources. WWIV's Q-scan versus N-scan distinction could not be
sourced and is not relied on here.
