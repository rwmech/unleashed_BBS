<!--
 ===========================================================================
  µnleashed BBS
  Electronic freedom on a microcontroller.
 ===========================================================================

 File:         reports/ux-message-boards.md
 Module:       Design report / caller-facing UX for the message boards

 Purpose:      The flow, the keys, the colours and the screens for FORUMS:
               three levels (forum, subject, message), a per-caller read
               pointer, and a fast path that means a regular caller never
               navigates. Specified in all three terminal flavours.
               Design only. No code.

 Audience:     Rob, and whoever builds the forums plugin.

 Scope note:   PLAN-BULLETINS.md owns the data model, the file formats, the
               RAM budget and the phasing. This document owns what a caller
               sees and what the keys do. Where the two touch, this one says
               so and defers, EXCEPT where the UX forces a storage decision,
               which is called out under "Where this contradicts the plan".
               Sources for every historical claim are at the end.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v2 or later
 SPDX-License-Identifier: GPL-2.0-or-later
 ===========================================================================
-->

# Forums: the caller-facing design

## What changed in this revision, 2026-09-21

The first version of this document was right about layout and silent about
presentation. Every mock-up in it was monochrome text, which on this board is
one of four terminals, not the design. It also predated two decisions.

Read this section and the next two and you have the delta.

- **Everything is specified in three flavours now.** Every screen below is
  drawn as a character grid with a ruler, then annotated with the colour of
  each region and the exact `Term::` call that produces it. ANSI/CP437,
  PETSCII, plain ASCII. A reader can build a frame from the spec without
  guessing.
- **Plain ASCII is designed, not degraded.** It gets `-->`, `***`, `===` and
  `---` doing the structural work that reverse video and colour do elsewhere.
  It reads as a 1987 screen that happens to be monochrome. If the ANSI
  version looks like the real one and ASCII looks like what is left over, the
  brief has been failed.
- **Three levels, not two.** Forum, subject, message. The subject list is a
  new screen with no equivalent in the old document. Grouped subjects are
  mandatory (Rob), and they change the reading screen too: the subject is
  drawn once as the screen's title instead of once per message.
- **Votes are gone.** Not deferred in the mock-ups, absent. There is no vote
  column, no `+`/`-` key, no score in any header. `PLAN-BULLETINS.md` says
  they are dropped from the first version; the old document drew them
  everywhere and argued about where to put them. That argument is preserved
  under "Considered and rejected" so it does not have to happen again.
- **A theme key set.** Ten `color_*` keys in the plugin's own config section,
  the shape chat already uses and proved.
- **The byte cost of the richest frame is measured, with the arithmetic
  shown.** 3,326 bytes for a full sixteen-forum list at 132 columns, against a
  `BBS_TL_BYTES` of 3,072, so it does not fit in one timeline and the list
  screens must go through `startPluginList`. A full 24-line message body is 2,223 bytes
  at 40 columns, which is why it must go through the list machinery and not
  the timeline.
- **`CONFIG <plugin> [FEATURE]`.** Rob wants `CONFIG FORUMS TOPICS` to reach
  the forum editor by typed name, not only by pressing a button. Specified
  below as a core change to `CONFIG` with a beneficiary beyond forums.
- **Seven things in the source contradict the plan or are live bugs**,
  including one in `CONFIG files` that silently widens a file area's download
  permission. They are in their own section.

### Revised again the same day, after Rob read it

Three rulings and one correction, all folded in.

- **The unread bookkeeping is redesigned and the first version of it was
  wrong.** Rob rejected the RAM framing: forum definitions are config, that
  was never the cost, and a permanent 2-byte slot per subject per caller
  charged a board with three subjects for capacity it will never use. The
  design is now **a high-water mark plus a 16-byte window of what was read
  above it**: 20 bytes per forum per caller, fixed, independent of the number
  of subjects, of messages and of the size of the board. Thirteen times
  smaller than what it replaces and it scales with nothing. Failure mode
  stated. Item 1 under "Where this contradicts the plan".
- **Duplicate subject names are handled, and it costs zero columns**, because
  the number on a subject row is now the subject's own permanent number
  rather than its position in the list. `11 Help` and `3 Help`. The date
  prefix is rejected with its reason: it fails on the case it exists for, two
  "Help" subjects started the same evening. Under S2.
- **`Re:` is dropped**, confirmed by Rob.
- **The forum cap is sixteen**, decided on what a sysop concludes from the
  number rather than on RAM, which was the wrong axis.
- **A seventh source finding:** `CLAUDE.md` documented the file-area wire
  format in a different field order than `readKey` parses it, so a sysop
  following the docs set the download level where the upload level goes.
  Already corrected. The forums' own wire format is specified here in parser
  order, and the reason is written down.

### Then Rob closed the last two format questions

- **The read window is 16 bytes, 128 messages.** Fixed-width, load-bearing,
  and it cannot be raised later without converting every card.
- **The subject hash takes the two vote fields.** The record stays 128 bytes
  and four to a sector, and **a vote tally in the index is now permanently
  closed off** rather than merely unbuilt. Taken knowingly.
- **The verb is FORUMS**, confirmed against `users.cpp` rather than the plan.
  The "unsettled" note is gone and the whole document says FORUMS.
- **`CONFIG <plugin> [FEATURE]` is approved** as a core change.

Everything this report needed decided is decided. The **phase 1 build order**
at the end is what phase 1 starts from.

Kept unchanged because they are still right: the `N`-means-stop trap, the
precedent research, the "Enter is the only key you need" principle, the
no-threading argument, the editor shape, the sources, and every measured
mock-up.

---

## What is settled, and the one thing that is not

**Everything this report needed a decision on is decided.** Phase 1 can start
from these numbers.

| Settled | What |
|---|---|
| The verb | **FORUMS** |
| The read window | **16 bytes, 128 messages.** Fixed-width, cannot be raised later |
| The subject hash | **4 bytes, taking the two vote fields.** Votes permanently closed off in the index |
| `CONFIG <plugin> [FEATURE]` | approved, built as a core change |
| The forum cap | **16** |
| Grouped subjects | mandatory |
| Duplicate subject names | disambiguated by the subject's permanent number, no date prefix |
| `Re:` | dropped |
| Votes | not built, and now not buildable into the index |
| The `CONFIG files` permission bug | rides with phase 1 |

Still open, and it does not block anything here:

| Open | Owner | Where |
|---|---|---|
| Theme delivery: CONFIG page or `theme.txt` (shell rework item 8) | Rob | "The theme keys" |

The theme keys are designed so that either answer works without a screen
changing, which is why it can wait.

---

## The verb is FORUMS

Settled, and already shipped in part. Confirmed against the source rather
than against the plan:

- `src/core/users.h:150` carries `LAND_FORUMS = 3`.
- `src/core/users.cpp:65` has `kLandKeys[] = { "default", "main", "chat",
  "forums" }`.
- `src/core/users.cpp:66` has `kLandVerbs[] = { nullptr, nullptr, "CHAT",
  "FORUMS" }`.
- `landFromKey` accepts `"bulletin"` as a **read-only legacy alias**
  (`users.cpp:73-82`), on purpose: accounts written before the rename say
  that, and without the alias a caller's landing preference would be silently
  reset to the board default. The next save rewrites it as `forums`.

`screens/bulletin.*` keeps its name. It is the notice screen that plays at
login, and that collision is what forced the rename in the first place, so
nothing else on the board is called bulletin any more.

Five things have to agree on the word and now do: the command verb, the
plugin's name (which is what `CONFIG <name>` takes), the `[plugin:forums]`
section in `system.cfg`, the folder `<sd>/p/forums/`, and every title bar and
hint line in this document.

---

## The verdict

This is a good subsystem to build. Its shape is decided by four things that
are not up for argument: the file manager set the house style for a
subsystem, the pointer is the feature, subjects are mandatory, and a C64 has
40 columns and 25 rows.

The design risk is not drawing. It is that **three levels of drill-down means
three screens of navigation before anybody reads a word**, and on a C64 that
is slower than the flat design it replaces. If browsing becomes the only way
in, this is worse than what it replaced and people stop calling.

So there are two paths and only one of them is the default:

> **Reading is one key. Browsing is three screens. The highlight bar is
> already sitting on the reading path when the screen opens.**

And the sentence to hold on to when any detail is in doubt is unchanged:

> **A caller should be able to read everything new on the board by pressing
> one key, repeatedly, and that key should be Enter.**

Not because one key is cute. Because this board's pager already treats Enter
as "carry on" (`src/core/bbs.cpp:2319`). Enter continues at `[More]`, Enter
shows the next message, Enter carries you across a subject boundary and a
forum boundary alike. A caller who learns exactly one thing about the forums
can read the whole board with it and never find out that those are three
different mechanisms.

rn stated the principle in 1984 and it is still the best sentence anybody has
written about terminal UI:

> "Typing space to any question means to do the normal thing. You will know
> what that is because every prompt has a list of several plausible commands
> enclosed in square brackets. The first command in the list is the one which
> will be done if you type a space."
> -- rn(1)

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
a caller taught "press N for the next message" will press N at the page break
and lose the rest of what they were reading. They will not understand why,
because from where they sit they pressed the same key twice and got two
different results.

So:

- **Enter and SPACE are the loop.** Both already continue at `[More]`.
- **`N` is not offered at all**, not even as an alias. An alias that is safe
  at one prompt and destructive at another is worse than no alias.
- `C` at `[More]` means continuous, which is right for a long message on a
  fast link and costs nothing.

This is the single most important finding here and it comes from reading this
board's code, not from any BBS history.

---

## What the precedents did, and what is taken from each

### Citadel, and the two Citadels

Worth separating, because the popular account merges them and gets the
attributions wrong.

**Citadel 2.10 (CP/M, Cynbe ru Taren, public domain).** The whole command set
was twelve lines, and this is the file verbatim:

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
messages posted since you were last in it, in room-slot order, which made the
order stable session after session. Citadel 2.10 had no message when there
was nothing left; you knew you were finished because you were back in the
Lobby. Citadel-86 added the explicit line, because the silence was ambiguous.

**The single best detail, and it costs nothing: Citadel echoed the expansion
of the key you pressed.** Press G and the screen shows `Lobby> goto`. One key
in, one word out. The command vocabulary teaches itself, and the scrollback
afterwards reads as a transcript instead of as a pile of output.

**The other detail worth having: flow control during output.** Citadel-86
took `<P>ause`, `<S>top`, `<N>ext` (abandon this message, start the next) and
`<J>ump` (skip to the next paragraph) as single keys *while the message was
still printing*. At 300 baud that is what made "read everything" safe.

**Taken:** the board decides what is next, not the caller. `G` keeps its
letter and roughly its meaning. The command echo. The `>` as part of the
forum's name. The explicit end-of-loop message, which Citadel had to learn
the hard way.

**Left:** `<Z>ap`. It earned its keep on systems with dozens of rooms, and
Citadel's own help file shows why it is not free: un-forgetting required
typing the room's **full name** at `.Goto`, which is an asymmetric and
unforgiving undo. On a board capped at sixteen forums, "I do not care about
Swap Shop" is answered by pressing `S` when you reach it: one key, no
per-account state, and no undo to get wrong.

**Left:** the Lobby as the place you land in and read from. This design lands
on a list of forums with unread counts. Citadel made you press `<K>nown
rooms` to see what existed, and its own `listRooms()` sorted rooms with
unread messages first, which tells you the list was what people actually
wanted.

**A deliberate inversion, and it needs saying out loud.** In Citadel,
`<G>oto` marked the room read on the way out and `<S>kip` was the version
that did not. Here it is the other way round: `G` moves on and leaves your
pointer alone, `S` marks what you are standing in as read and moves on. That
is not carelessness. Citadel's pointer was per *visit*, so leaving a room was
the event that marked it; this board's pointer advances per *message
displayed*, so leaving can never lose anything. Citadel's reason for two keys
evaporates, and what is left is two keys meaning "move on" and "give up on
this one", which is what the English words Goto and Skip mean.

### WWIV subs

WWIV made the scan a first-class main-menu command rather than something you
assembled by visiting each sub: `N` scans all subs for new messages, `Z` does
the same non-stop. Subs are addressable by bare number from the prompt, with
`+`/`-`/`>`/`<` to step and `H` to hop by substring. Its read prompt carried
`W` for reply and `@`/`A` for an auto-reply that could retarget or forward,
so you never left the reading loop to respond.

**Taken:** reply from inside the reading loop, as one key. The numbered
container reachable directly.

**Left:** the separate scan verb. Here Enter at the forum list *is* the scan,
so a second command would be a second way to do the only thing the subsystem
exists for.

### PCBoard conferences

PCBoard's Last Message Read pointer was per conference, per user, and
**visible**: the read prompt showed the live message-number range you were
navigating, `(H)elp, (3523-5032), Message Read Command?`, and the caller
could move their own pointer with `R;SET`.

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

### Usenet: rn, trn, tin, nn

**rn (Larry Wall, 1984)** is where the ergonomic comes from. Its own man page
opens by saying it "was written to be as efficient as possible, particularly
in human interaction", and measures cost in keypresses. The implementation is
literal: `setdef()` pushes the first character of the bracket string back
into the input queue when you press space, and unless `STRICTCR` is compiled
in, **newline is treated identically to space**.

The default shifted with context, which is the part that turns a prompt into
a loop: `[npq]` mid-group means space is "next unread article", but at the end
of a group the string becomes `[qnp]` and the same space leaves the group.

**tin** put a selector screen in front of all this: a full-screen list with a
cursor, unread counts and group descriptions, and it split rn's overloaded
space into two keys. It also has a `d` key to toggle the description column
off, which is the same 40-column problem this board has.

**tin and rn also did subject grouping**, which is the thing Rob asked for and
is worth naming precisely: `rn` and `trn` called it "threading by subject",
and it is a filter over a flat list rather than a tree. That is exactly the
model here.

**nn** inverted the loop: a menu of subject lines, pick with letter keys,
then space commits and reads only the picks. Its menu had five selectable
layouts, and layouts 2 and 3 drop the author column entirely, which is the
precedent for what to drop at 40 columns.

**Taken from rn:** the context-dependent default, and the catch-up key `c`,
which rn confirmed with `Do you really want to mark everything as read?` and
nothing else confirmed.

**Taken from tin:** the list as the hub, with unread counts and descriptions,
and dropping the description at narrow widths rather than truncating
everything. And subject grouping as a filter, not a tree.

**Left from trn and tin:** threading, the thread selector and the article
tree. Settled by the owner, and trn's tree is drawn in the upper right of an
80 column header, which is the first thing that does not exist at 40.

### DDial and Gtalk

Not a message system, but the source of the board's own visual grammar and
the reason Rob asked for `-->` here. The system's voice is marked so it
cannot be mistaken for somebody talking. That mark is already in this
codebase: `src/plugins/chat.cpp:218`, `const char kMark[] = "--> ";`,
coloured by `color_marker`. This design uses the same characters and the same
colour key, because a caller should not have to learn two vocabularies for
the same idea on one board.

---

## The shape: three levels, two paths, one loop

Three places, and the caller always knows which one they are in because the
prompt says so.

```
Forums>             the list of forums.     Sysop-configured.
[F2] C64>           inside forum 2.         The subjects in it.
[F2.12] C64>        inside subject 12.      The messages.
```

The bracketed tag appears only when you are inside something. That is the
device `files` uses (`[S1] Files>`), with two changes: the **forum's** name
is in the prompt rather than the subsystem's, because in a reading session
that crosses forums the name is what you need; and the tag carries the path,
so `[F2.12]` says exactly where you are without spending a word on it. The
subject is on the title bar, where it has room to be read.

**Both numbers in the tag are permanent.** `2` is the forum's config slot and
`12` is the subject's own number, not its row on any screen. So `[F2.12]` is
a stable address a caller can write down, and it is the same pair of numbers
they would type to get back there. A tag built from row positions would name
a different conversation next week.

The `>` reads as part of the name, Citadel-style, which is what makes
`*** End of C64. Next: Swap Shop, 3 new.` read as a sentence.

### The two paths, and why the fast one is not a separate mode

```
                            login
                              |
         one line, only when there is something:
         "12 new in 3 forums. FORUMS reads them."
                              |
     FORUMS typed  ....or....  account Start = Forums
                              |
              screens/forums plays, if the board has one
                              |
   +----------------------------------------------------------+
   |  THE FORUM LIST                          prompt: Forums>  |
   |  the bar OPENS sitting on the action row, not on forum 1   |
   +----------------------------------------------------------+
       |                                              |
   Enter                                        1..9 / cursors
       |                                              |
       v                                              v
  +--------------------+                  +---------------------------+
  |  THE READING LOOP  |                  |  THE SUBJECT LIST         |
  |  no navigation     |                  |  prompt: [F2] C64>        |
  |  ever again        |<---- Enter ------|  the bar opens on ITS      |
  |                    |                  |  action row too            |
  +--------------------+                  +---------------------------+
       |                                              |
       |                                        1..9 / cursors
       |                                              |
       +--------------> prompt: [F2.12] C64> <---------+
                        THE MESSAGES
```

**The fast path is not a mode. It is auto-navigation.** Pressing Enter on the
action row does not open a parallel reader: it walks you into the forum and
the subject where the next unread message actually lives, and puts you there
with a real prompt, a real title bar and a real `Q` that goes back one level.
When the loop crosses into a different subject it says so and the title bar
changes. When it crosses into a different forum it says so more loudly.

That is one mechanism, not two, and it is the reason a caller who only ever
presses Enter still ends up somewhere they can navigate from if they want to.
The alternative, a separate "read everything" reader with its own screen,
would mean two code paths drawing the same things and disagreeing about them,
which is the bug this project keeps finding.

**Every way out is Q or ESC, and it always goes back exactly one level.**
Messages to subject list, subject list to forum list, forum list to the shell.
That is the `files` rule and it should not be varied.

### Why three levels does not cost a regular caller anything

Count the keystrokes for somebody who calls every day:

| | Keys |
|---|---|
| `Start = Forums`, read everything | Enter, then Enter per message |
| `FORUMS` typed, read everything | `FORUMS`, Enter, then Enter per message |
| Browse to the 20m antennas subject | `FORUMS`, `2`, `4`, Enter |

The three-level tree costs the browsing caller two keypresses more than a
flat list would. It costs the reading caller nothing at all. That is the
whole case for it, and it only holds if the action row is genuinely the
default, which is why it is at the top of both list screens and why the bar
starts on it.

---

## The presentation system

This is the part the old document did not have. Everything below is drawn
from what `src/core/term.cpp` can actually emit, not from what a terminal
could theoretically do.

### The matrix is smaller than it looks

| Terminal | Widths it can be | Rows |
|---|---|---|
| ANSI / CP437 or UTF-8 | anything; NAWS or the CPR probe reports it; `rowWidth` caps at 132 | usually 24 |
| PETSCII | **40 or 80 only** (`TermType::Pet40`, `Pet80`) | 25 |
| Plain ASCII | usually **unknown, which `rowWidth` turns into 40** (`src/core/bbs_shell.cpp:440`); 80 only if the terminal actually reported it | 24 |

So the work is: ANSI at 40, 80 and 132; PETSCII at 40 with a note for 80;
plain ASCII at 40 with a note for 80. Nine cells, not twenty-seven.

**PETSCII at 40 is the hard case and is drawn first.** Every layout below is
designed at 39 columns and widened, never the other way round. That is the
project's own standing rule and it is the reason the widths work out.

### What each terminal actually has

Measured against `term.cpp`, not assumed.

| Capability | ANSI | PETSCII | ASCII |
|---|---|---|---|
| `color()` | `ESC[0;NN[;1]m`, 16 entries | one byte, `kPetColor[]` | nothing at all |
| `reverse()` | `ESC[7m` on, **re-assert the colour** off | `0x12` / `0x92` | nothing |
| `cls()` | `ESC[2J ESC[H` | `0x93` | `\r\n\r\n` |
| `eolClear()` | `ESC[K`, **refused while reverse is on** | false, pad with spaces | false, pad with spaces |
| `gotoXY`, `up`, `down` | yes | yes | **no** |
| `Glyph::HLine` | CP437 `0xC4` | `0xC0` | `-` |
| `Glyph::VLine` | CP437 `0xB3` | `0xDD` | `\|` |
| `Glyph::Block` | CP437 `0xDB` | reverse space, 3 bytes | `#` |
| `Glyph::Shade` | CP437 `0xB1` | `0xA6` checker | `:` |
| `cp437(b)` for arbitrary art | yes, re-encoded to UTF-8 when needed | **`?`** | **`?`** |

Two of those are traps and both have bitten this project:

- **Do not rely on SGR 27.** ANSI.SYS never implemented it and SyncTERM
  ignores it. `Term::reverse(o,false)` re-asserts the colour instead. Nothing
  in this design may assume an attributes-off code exists.
- **`Term::cp437()` prints `?` on PETSCII and ASCII.** So box drawing with
  raw CP437 bytes is an ANSI-only device and must have a `Glyph` fallback or
  it renders as a row of question marks. This is exactly the `??nleashed`
  bug, one layer down.

### Two colours that are secretly the same on ANSI

From `kAnsiColor[]` in `term.cpp:63`:

- `Color::Grey` `{0,37}` and `Color::LightGrey` `{0,37}` are **byte for byte
  identical on ANSI**. They are different on a C64 (`0x98` and `0x9B`).
- `Color::Orange` `{0,33}` and `Color::Brown` `{0,33}` are likewise
  identical on ANSI.

So a design that uses Grey and LightGrey to mean two different things works
on a C64 and is invisible on SyncTERM. **Nothing below depends on that
distinction.** Worth writing down because it is not visible from the enum.

### What is unreadable on a C64, and therefore banned

A stock C64 sits on a dark blue background. Against it:

- **Blue (6) is invisible.** Never.
- **Black (0) and Brown (9) are close to it.** Never.
- **Purple (4) and Red (2) are dark and muddy.** Red survives only because it
  means an error and an error is worth squinting at. Nothing else uses them.
- **DarkGrey (11) is legible and tiring.** Chat uses it for punctuation and
  gets away with it. Nothing in the forums uses it for content: only for the
  `---` rules, where missing it costs nothing.

Good on blue, and what this design uses: White (1), Cyan (3), Yellow (7),
LightGreen (13), Grey (12), LightRed (10).

### The colour vocabulary: six colours, six jobs

The rule the brief asks for, stated once: **the same thing is the same colour
on every screen.**

| Colour | Means | Where |
|---|---|---|
| **Cyan** | structure | title bars, rules, the `-->` marker, the `>` in a prompt |
| **LightGreen** | the thing to act on, and who acted | the action row, the unread `*`, a handle, the `[F2.12]` tag |
| **White** | content you read | subjects in a list, the subject you are in, message bodies |
| **Yellow** | a number, and an event | unread counts, `N new`, the `***` notice lines |
| **Grey** | furniture and labels | row numbers, dates, forum descriptions, hint lines |
| **LightRed** | a refusal | and nothing else, ever |

Six is the ceiling on purpose. Chat runs on nine and is a wall of colour when
a room is busy; a reading screen should be quieter than a chat room.

### Reverse video means exactly one thing

**Reverse video means "the bar is here".** One meaning, every screen, every
terminal that has it.

The one exception is `rowTitle`, which is a reverse cyan bar at the top of a
screen. That is established across the whole board and it is unambiguous
because a title bar is at a fixed place and never moves. Nothing else may use
reverse.

This matters immediately: the action row is a *different kind of thing* from
a forum row, and the obvious way to say so on ANSI is to make it a reverse
bar. If it were always reverse, the caller could not tell where the cursor
was. So:

- **The action row is reverse only when the bar is on it**, which it is when
  the screen opens.
- **Its other signals are permanent**: it is LightGreen where a forum row is
  Grey and White, and it carries `-->`.

### The `-->` marker is on every terminal, not just ASCII

Rob's words were "ascii gets the old skewl `--` and `-->` stuff like ddial".
The right reading of that is not "ASCII gets an arrow and ANSI gets colour".
It is that **the arrow carries the structure where there is no colour, and
carries it anyway where there is**, because chat already does exactly that:
`tell()` prints `kMark` on every terminal and merely colours it differently.

A caller who dials in from SyncTERM one night and a C64 the next should see
the same words. So:

- `-->` is the board's own voice. Same three characters everywhere. Cyan on
  ANSI and PETSCII, plain on ASCII.
- `***` is an event, not an answer. Yellow on ANSI and PETSCII, plain on
  ASCII. Chat already uses it for joins and leaves.
- What ASCII gets *extra* is the rules: `===` where the others have a reverse
  bar, `---` where the others have a coloured divider.

### The plain ASCII grammar, in full

This is the "old skewl" set. Five devices, each with exactly one job.

| Device | Job | Where ANSI/PETSCII does it with |
|---|---|---|
| `Name ------------------ right` | a title bar | reverse cyan bar (`rowTitle` already does both) |
| `=======================` above and below a row | this row is the action | reverse LightGreen bar |
| `-->` | the board is talking | `-->` in cyan |
| `***` | something happened | `***` in yellow |
| `-----------------------` | end of a list | `rowRule`, cyan |
| `*` in column 1 | unread | `*` in LightGreen |
| `-- ` before a message header | this line is the board, the next is the message | Grey/LightGreen colouring |
| `[3]` | a number you can type | a highlight bar you can move onto |

Three of those (`rowTitle`'s dashed form, `rowRule`, the `*`) already exist
and behave this way. The rest is convention, not code.

**The `===` device is the only new drawing primitive**, and it needs a
`Glyph` entry rather than a literal `=`, because `Term::ch()` maps ASCII
through `asciiToPet()` and a bare `=` is fine on PETSCII but a *double* rule
is not available there. See the core asks at the end: one enum value,
`Glyph::HLine2`, giving CP437 `0xCD`, PETSCII `0xC0` (the same single line,
because the C64 has no double) and ASCII `=`. Appending to `Glyph` is safe;
it is an enum class in a switch, not a positional descriptor.

### The theme keys

Ten keys, in the plugin's own `[plugin:forums]` section, parsed by
`colorByName()` exactly as chat's nine are (`src/plugins/chat.cpp:255`).

```
color_title    = cyan      title bars and rules
color_action   = ltgreen   the read-everything row
color_marker   = cyan      the --> the board speaks with
color_notice   = yellow    *** something happened
color_subject  = white     a subject in a list, and the one you are in
color_body     = ltgrey    message text
color_who      = ltgreen   a handle
color_when     = grey      a date, a time, a description
color_count    = yellow    an unread count
color_deny     = ltred     a refusal
```

**Which shape this assumes, and why.** Shell-rework item 8 is still undecided
between a CONFIG colour page and a `theme.txt` on the card. This key set
assumes **neither**, and that is deliberate: they are ordinary config keys
with defaults in code, read by `readKey()`, and **not declared as
`PluginSetting`s**. That is chat's shape and it has three properties worth
having:

- **They cost no CONFIG rows.** `Form::kMaxFields` is 16 and the forums page
  needs every row it has. Ten declared colour keys would not fit.
- **They are still discoverable.** `cmdConfig` lists any key the file already
  carries that the plugin did not declare (`bbs_sysop.cpp:1152`), so the
  moment a sysop writes `color_body = white` into `system.cfg` by hand, it
  appears on the CONFIG page as an editable field. Free until used, editable
  once used.
- **A `theme.txt` can seed exactly these keys later without one screen
  changing.** If item 8 lands on the card-file answer, the file sets the same
  names and nothing above this layer knows. If it lands on a CONFIG page, the
  page declares them and they stop being free. Either way the key names and
  the meanings do not move, which is what makes this decidable later.

`color_marker` and `color_notice` deliberately share names with chat's. Same
idea, same word, and a sysop who has themed one has themed the other's
vocabulary.

### The byte budget, with the arithmetic

`BBS_TL_BYTES` is 3,072 per session and a `Timeline::put()` is all or nothing
(`src/core/timeline.cpp:59`). `Term::text()` puts one byte at a time and
ignores the return, so a full timeline does not error, it **silently drops
characters mid-row**. That is the failure to design against.

ANSI escape costs, from `Term::color()` at `term.cpp:344`:

| Call | Bytes |
|---|---|
| `color()`, non-bold entry (Grey, LightGrey, Orange, Brown, Red, Green, Blue, Purple, Black) | 7 |
| `color()`, bold entry (White, Cyan, Yellow, LightRed, DarkGrey, LightGreen, LightBlue) | 9 |
| `color()` while reverse is on | +2 |
| `reverse(on)` | 4 |
| `reverse(off)` | a `color()`, so 7 or 9 |
| `nl()` | 2 |
| `eolClear()` | 3 |

PETSCII: every colour is 1 byte, reverse is 1 byte, newline is 1 byte.
ASCII: colour and reverse are 0, newline is 2.

**The richest frame is a full forum list at 132 columns, in full colour:**

```
 title bar     colour 9 + reverse-on 4 + colour-with-reverse 11
               + 131 text + reverse-off 9 + nl 2                  =   166
 blank                                                            =     2
 action bar    same shape as the title bar                        =   166
 blank                                                            =     2
 forum row     6 colour changes (9+7+9+9+7+7 = 48)
               + 131 columns of text + nl 2                       =   181
 blank                                                            =     2
 hint line     colour 7 + 66 text + nl 2                          =    75
 prompt        colour 9 + 8 text                                  =    17
```

| Forums on the board | Frame | Against 3,072 |
|---|---:|---|
| 5 | 1,335 | 43% |
| 8 | 1,878 | 61% |
| **16, the cap** | **3,326** | **108%, does not fit** |

**At the cap it does not fit in one timeline, and that settles an open
question rather than raising one.** The list screens were already specified
to go through `startPluginList`; this makes it a requirement instead of good
practice. Paced by `rows()`, `serviceList` only draws while 512 bytes are
free, so the frame is built across as many passes as it needs and cannot
overflow by construction. Drawn directly, as `files` draws its area menu, a
sixteen-forum board at 132 columns would **silently lose characters out of
the middle of the last few rows**, because `Term::text` puts one byte at a
time and discards the return.

Worth naming the shape: this is the same failure as the sixteen-node build
that overflowed `dram0_0_seg` by 104 bytes. A number that was comfortable at
the size the thing was designed for, and is not at the size it grew to, with
nothing warning in between.

The same frame at 40 columns on PETSCII is **510 bytes** with five forums,
and on plain ASCII **585**. Colour and width are what cost, and the C64 is
the cheap case throughout.

**The expensive thing is a message body, and the narrow terminal is the
expensive one.** A body at the cap is 24 lines of 72 characters, 1,728
characters. Wrapped for output, the row overhead is one colour plus a newline:

| Width | Rows | Overhead per row | Total |
|---|---|---|---|
| 39 (PETSCII) | 45 | 2 | 1,818 |
| 39 (ANSI) | 45 | 11 | **2,223** |
| 79 (ANSI) | 22 | 11 | 1,970 |
| 131 (ANSI) | 14 | 11 | 1,882 |

2,223 bytes is 72% of the timeline for one message, which is why **the body
must go through `startPluginList` and `rows()`**, one output row per call.
`serviceList` only draws while the timeline has 512 bytes free
(`bbs.cpp:2220`), so paced that way it cannot overflow by construction, and
it gets `[More]`, the abort keys and backpressure for nothing. Written
straight into the timeline it would silently truncate somebody's message.

This is not an optimisation. It is the difference between working and losing
characters, and it is counter-intuitive enough to be worth the table: **a
40-column terminal costs more bytes per message than a 132-column one**,
because wrapping multiplies the per-row overhead.

### One place this arithmetic changes an existing design

`files` redraws **every** menu row when the bar moves (`files.cpp:951`,
`Draw::Again`). At 40 columns with five areas that is about 200 bytes per
keypress and nobody noticed. The forum list at 132 columns with sixteen
forums is **2,896 bytes per keypress**, and cursor keys auto-repeat.

**Specify: a bar move redraws exactly the two rows that changed.** Move to
the row being left, redraw it plain, move to the row being entered, redraw it
reversed, return. About 362 bytes on ANSI, and it works on PETSCII too
because PETSCII has cursor movement. Plain ASCII has no bar and never reaches
this path.

---

## The screens

Every mock-up below was generated to the character grid and measured. The
ruler above each is real and no line exceeds `rowWidth`, which is `cols - 1`
capped at 132 (`src/core/bbs_shell.cpp:433`).

The ANSI and PETSCII grids are **identical** for a given width: they differ
only in the bytes that carry the colour and the reverse video. So each screen
is drawn once with a colour legend, and the PETSCII differences are stated
where they exist. The plain ASCII grid is drawn separately, because it is
genuinely different.

### The column grammar, once, for both list screens

The forum list and the subject list are the same layout one level apart.
Same drawing routine, different content. That is a design statement: moving
down a level should feel like the same place, one step in.

```
[mark 1][num 2][sp 1][name nameW][gap][count 9][gap][extra ...][gap][last lastW]
```

- `gap` is **1 column below 60, 2 columns at 60 and above.**
- `count` is 9 columns, right aligned, and reads `5 new` when there is unread
  or `218 msgs` when there is not. Never `0`. A column of zeroes reads as a
  fault; a column that switches between two units reads as a column that
  always has something to say. Citadel did the same, printing the new count
  only when it was non-zero.
- `mark` is `*` when the row has unread, space otherwise. It costs no columns
  because the gutter exists anyway, and it is what plain ASCII leans on.

| Width | `nameW` | `extra` | `lastW` |
|---|---|---|---|
| 39 | 25 | none | none |
| 79, forum list | 16 | description, 28 | 16 |
| 79, subject list | 46 | none | 16 |
| 131, forum list | 20 | description, 42 | 50 |
| 131, subject list | 50 | none | 64 |

`last` is `handle(9) date(6)` at 79 and the **whole last message as a
preview** at 131. That is the wide-terminal payoff and it is content, not
padding: at 132 columns a caller can see what the newest conversation in each
forum is actually about without opening anything. Stretching a five-row table
across 131 columns is not using the screen, it is spreading it thin.

---

### S1. The forum list

The top level. Sysop-configured, exactly like file areas.

#### ANSI and PETSCII, 40 columns

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 Forums                         12 new 

 --> Read everything new        12 new 

* 1 General                       5 new
* 2 C64                           4 new
* 3 Swap Shop                     3 new
  4 Off Topic                   12 msgs
  5 Sysop Notes                  7 msgs

 Enter reads. A number opens. ? help.
Forums> 
```

Colour legend, region by region, with the call that produces it:

| Region | Colour | Call |
|---|---|---|
| row 1, the whole bar | reverse Cyan | `b.rowTitle(s, "Forums", "12 new")` |
| row 3, the whole bar, **when the cursor is on it** | reverse LightGreen | `rowBar(s, Color::LightGreen, "--> Read everything new", "12 new")` |
| row 3 when the cursor has moved away | plain LightGreen, no reverse | same, `reverse` not set |
| `*` in column 1 | LightGreen | `rowSeg(s, Color::LightGreen, "*", col)` |
| ` 1`, the number | Grey | `rowSeg(s, Color::Grey, " 1", col)` |
| `General`, the name | White | `rowSeg(s, Color::White, buf, col)` |
| `5 new` | Yellow | `rowSeg(s, Color::Yellow, buf, col)` |
| `12 msgs` | Grey | `rowSeg(s, Color::Grey, buf, col)` |
| the hint line | Grey | `rowText(s, Color::Grey, ...)` |
| `Forums>` | Cyan | `t.color(tl, Color::Cyan)` then `t.text` |

PETSCII differences: none in the grid. The colour bytes are `0x9F` cyan,
`0x99` light green, `0x05` white, `0x9E` yellow, `0x98` grey; reverse is
`0x12` on and `0x92` off. `rowTitle` already emits all of this correctly.
A C64 has 25 rows so this screen leaves 13 for `screens/forums`.

Note the count column earning its keep: `12 msgs` in Grey on a forum with
nothing new is quieter than `5 new` in Yellow on one that has. The eye goes
to the right rows without reading a word.

#### Plain ASCII, 40 columns

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
Forums ------------------------- 12 new
=======================================
--> Read everything new          12 new
=======================================
* 1 General                       5 new
* 2 C64                           4 new
* 3 Swap Shop                     3 new
  4 Off Topic                   12 msgs
  5 Sysop Notes                  7 msgs
---------------------------------------
 Enter reads. A number opens. ? help.
Forums> 
```

Three things to notice, because they are the whole argument for this being a
design rather than a fallback:

- **`rowTitle` already draws row 1 like that.** `src/core/bbs_shell.cpp:582`:
  title, a space, `Glyph::HLine` to the width, a space, the right text.
  **There is no leading space on ASCII**, unlike the ANSI and PETSCII forms,
  which start with one inside the reverse bar. The old document's mock-ups
  showed the leading space on all three and were wrong about it.
- **The `===` pair is what reverse video was doing.** It costs two rows. The
  forum list is 14 rows with five forums and 17 with eight, against 24, so
  the rows are there to spend.
- **The `*` and the `--->` do the work colour was doing.** Nothing on this
  screen is ambiguous without colour, which is the test.

The hint line says `Enter reads` in words because there is no bar sitting on
anything to make it obvious.

13 rows on ASCII against 12 on ANSI. One extra row, for a terminal that has
one fewer than a C64. It fits.

#### ANSI, 80 columns

```
         11111111112222222222333333333344444444445555555555666666666677777777778
12345678901234567890123456789012345678901234567890123456789012345678901234567890
 Forums                                                                 12 new 

 --> Read everything new, oldest first                                  12 new 

* 1 General               5 new  Anything at all               Daytona   19 Sep
* 2 C64                   4 new  The machine this board is fo  Daytona   19 Sep
* 3 Swap Shop             3 new  Buying, selling, giving away  Mindcrime 18 Sep
  4 Off Topic           12 msgs  Everything else               Daytona   12 Sep
  5 Sysop Notes          7 msgs  Board news, staff post        Rob       02 Sep

 Enter reads them all.  A number opens one.  ? help.  Q/ESC leaves.
Forums> 
```

The extra width buys two **columns**, not two columns of forums. `files`
packs its areas into a grid because an area is nothing but a name. A forum
has a description worth reading and a last-post line worth scanning, so the
width goes to content. That is also what tin's group selector did.

- **Description** appears at 60 columns and up. It is the one place a sysop
  can say what a forum is for, which otherwise has nowhere to live. Grey, so
  it recedes behind the name.
- **Last post** is handle (9) and date (6). Grey, and it comes from RAM: the
  plugin already holds the highest message number per forum to compute the
  unread counts, and the last poster and time beside it is about 24 bytes per
  forum. **Nothing on this screen touches the card.** Citadel made the same
  call for the same reason, keeping new-message detection entirely in the
  user record so `<G>oto` needed no disk access on a floppy-based CP/M box.
- **No header row.** `5 new` and `Daytona 19 Sep` label themselves. A header
  row is one more thing to keep aligned and it buys nothing here. The old
  document specified one; drop it.

The hint line grows its long form at 60 and up, the same `cols >= 60` test
`files` already uses (`files.cpp:801`).

#### ANSI, 132 columns

```
         111111111122222222223333333333444444444455555555556666666666777777777788888888889999999999000000000011111111112222222222333
123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012
 Forums                                                                                                                     12 new 

 --> Read everything new, oldest first                                                                                      12 new 

* 1 General                   5 new  Anything at all                             Daytona, 19 Sep: Board is on 0.18.0 tonight       
* 2 C64                       4 new  The machine this board is for               Daytona, 19 Sep: 1541 alignment disk              
* 3 Swap Shop                 3 new  Buying, selling, giving away                Mindcrime, 18 Sep: Anyone want a spare 1571?      
  4 Off Topic               12 msgs  Everything else                             Daytona, 12 Sep: What are you all drinking        
  5 Sysop Notes              7 msgs  Board news, staff post                      Rob, 02 Sep: House rules, please read             

 Enter reads them all.  A number opens one.  ? help.  Q/ESC leaves.
Forums> 
```

The last column stops being `handle date` and becomes **who said what, and
when**. The subject text in it is Grey like the rest of the column, because
it is context and not the thing you are choosing between.

This is the answer to "it looks jammed together at 80 and black at 132". The
screen is not stretched, it is given something more to say.

---

### S2. The subject list

**New screen. The old document had no equivalent.** This is the level Rob's
example lives at: a `HAM RADIO` forum carrying both "20m tips and tricks" and
"20m antennas", where following one must not mean reading the other.

#### ANSI and PETSCII, 40 columns

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 C64                             4 new 

 --> Read the 4 new here         4 new 

*12 1541 alignment disk           3 new
*14 JiffyDOS on a breadbin        1 new
  9 Anyone want a spare 1571?    5 msgs
 11 Help                         2 msgs
  3 Help                         4 msgs

 Enter reads. A number opens. P posts.
[F2] C64> 
```

Identical grammar to the forum list, one level down. Identical colours. The
only differences are the content of the name column and the action row's
wording, which now names the forum you are in rather than the board.

#### The number is the subject's own, permanently, and that is what handles duplicates

Look at the last two rows. Two subjects called `Help`, and nothing has been
added to the screen to tell them apart, because the thing that tells them
apart is already on every row.

**The number in the left column is the subject's permanent number, not its
position in the list.** It is assigned when the subject is created, it never
moves, and it is never reused, which is the same invariant a message number
already has and the same one a file area number already has. `files` does
exactly this: its numbers are area numbers, not row positions, which is why
`Screens` and `Logs` can sit at 9 and 10 above the eight configured ones.

That single decision answers the duplicate-subject requirement completely
and costs **zero columns**:

- Two subjects called `Help` are `11 Help` and `3 Help`. Different rows,
  different numbers, permanently.
- The number a caller learns stays true. "Help is 11" is true next week.
- It is typeable. A date prefix is not.
- **It cannot itself collide**, by construction.

**A date prefix was the original proposal and it does not actually solve the
problem.** Duplicates happen when two people hit the same thing on the same
evening, which is precisely when a date prefix gives both subjects the same
prefix and leaves them identical. It also costs 8 of 25 columns at 40, and
it is unstable if it is the date of the *last* message rather than the
first, because then the row's text changes under a caller who is looking at
it. Recommend the number; recommend against the date.

Conditional disambiguation, showing an identifier only when a clash exists,
is also rejected: it means a row's appearance changes the day a stranger
creates a clashing name, which is the same instability one layer along.

**The identity used for grouping is the 4-byte hash and never the display
string.** Renaming a subject, or any display-time disambiguation, cannot
split or merge a conversation, because nothing matches on text. See item 2
under "Where this contradicts the plan".

#### The rest of the rules

- **The number column is as wide as the forum's highest subject number, with
  a minimum of 2.** Same rule as the message-number column in the listing and
  for the same reason. A board with three subjects gets a 2-column number and
  the full 25 columns of subject; a forum with 400 subjects over the years
  gets 3 and gives one back. **The cost tracks what is actually on the
  board.**
- **Subjects with unread float to the top**, then everything else, each block
  newest first. Citadel's own `listRooms()` sorted rooms with unread messages
  first, which tells you what people actually wanted. It also puts the rows
  the action row is about to read directly underneath it. Two walks of a
  small fixed-width file, no sort and no RAM.
- **Subjects are cut at 25 columns with no ellipsis.** There is no clever way
  out of 40 columns; the full subject is on the title bar the moment you open
  it, which is the place it can be read.
- **The subject list is windowed**, `list_rows` at a time, for the same
  reason the message listing is: `Session::listIdx` is a `uint8_t` and it is
  the row counter in every list on this board. A windowed list of permanent
  numbers is non-contiguous on screen, which is exactly what the file areas
  already look like and has never confused anybody.

#### Plain ASCII, 40 columns

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
C64 ----------------------------- 4 new
=======================================
--> Read the 4 new here           4 new
=======================================
*12 1541 alignment disk           3 new
*14 JiffyDOS on a breadbin        1 new
  9 Anyone want a spare 1571?    5 msgs
 11 Help                         2 msgs
  3 Help                         4 msgs
---------------------------------------
 Enter reads. A number opens. P posts.
[F2] C64> 
```

#### ANSI, 80 columns

```
         11111111112222222222333333333344444444445555555555666666666677777777778
12345678901234567890123456789012345678901234567890123456789012345678901234567890
 C64  (The machine this board is for)                                    4 new 

 --> Read the 4 new in C64, oldest first                                 4 new 

*12 1541 alignment disk                                 3 new  Daytona   19 Sep
*14 JiffyDOS on a breadbin                              1 new  Daytona   20 Sep
  9 Anyone want a spare 1571?                          5 msgs  Mindcrime 18 Sep
 11 Help                                               2 msgs  Rob       18 Sep
  3 Help                                               4 msgs  Daytona   02 Sep

 Enter reads them.  A number opens one.  P posts a new subject.  ? help.
[F2] C64> 
```

The forum's description moves into the title bar in parentheses at 60 and
up, which is the only place it fits without a column of its own. At 40 it is
dropped: the forum name alone is what the caller just chose.

The width goes to the subject, 46 columns of it. That is the right spend on
this screen, because on a subject list the subject is the entire content.
There is deliberately no "started by" column: it is the same handle on most
rows and the last-post column already says who is talking now.

#### ANSI, 132 columns

```
         111111111122222222223333333333444444444455555555556666666666777777777788888888889999999999000000000011111111112222222222333
123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012
 C64  (The machine this board is for)                                                                                        4 new 

 --> Read the 4 new in C64, oldest first                                                                                     4 new 

*12 1541 alignment disk                                     3 new  Mindcrime, 19 Sep: I have one. Yours for the postage.           
*14 JiffyDOS on a breadbin                                  1 new  Daytona, 20 Sep: worth it? I keep going back and forth          
  9 Anyone want a spare 1571?                              5 msgs  Mindcrime, 18 Sep: yes please, postage paid                     
 11 Help                                                   2 msgs  Rob, 18 Sep: try the reset line first                           
  3 Help                                                   4 msgs  Daytona, 02 Sep: never mind, sorted it                          

 Enter reads them.  A number opens one.  P posts a new subject.  ? help.
[F2] C64> 
```

At 132 this screen becomes genuinely useful for browsing: subject on the
left, the latest thing said in it on the right. That is what a wide terminal
is for.

### The unread arithmetic must add up, and here is how it does

A forum's count is the sum of its subjects' counts. On the screens above:
C64 shows `4 new` and its subjects show `3 new` and `1 new`. If those ever
disagree the board looks broken, and a caller will check.

**The only way to guarantee it is to compute both from one pass**, which is
what the design under "Where this contradicts the plan" does: one backward
scan of the unread region tallies by subject, and the forum's number is that
same tally summed. They are not two numbers kept in step, they are one number
printed twice.

That also settles the harder half, which is a subject's count going to zero
the moment it has been read. A forum-wide high-water mark on its own cannot
do it, and getting it wrong is visible on this screen rather than buried.
Item 1 over there has the mechanism and what it costs.

---

### S3. Reading

The reading loop is a **scroll, not a view**. The screen is never cleared
between messages: the previous message is the context for this one. This is
the one place where the "screen clears bro" rule from the file manager does
not apply, and it needs saying out loud because somebody will try to make it
consistent. Entering the subsystem clears, the listing clears, the help
clears, the editor clears. Reading does not.

#### ANSI and PETSCII, 40 columns

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 1541 alignment disk            1 of 3 

412  Daytona  19 Sep 21:14
Anybody got a working 1541 alignment
disk? Mine died last week and the local
shop wants 40 quid for a service. Happy
to pay postage both ways.
[F2.12] C64> 
```

| Region | Colour |
|---|---|
| the title bar | reverse Cyan (`rowTitle`) |
| `412` | Yellow |
| `Daytona` | LightGreen |
| `19 Sep 21:14` | Grey |
| the body | LightGrey |
| `[F2.12]` | LightGreen |
| `C64>` | White |

**The subject is drawn once, as the screen's title, not once per message.**
That is the direct gain from grouping and it is worth a row per message on a
25-row screen. It also means the stored `Re:` prefix has no job left; see the
third item under "Where this contradicts the plan".

- **The header is compact and left-aligned at every width.** It does not
  stretch to the right margin: it is a label above a paragraph, not a table
  row, and a single line with a 90-column hole in it looks like a bug.
  Citadel's header was assembled the same way, as short left-aligned pieces
  (`"   %s " "from %s" " in %s>"`), and it reads as prose because of it.
- **The prompt line is the separator.** One blank line above each header and
  none below the body, so the overhead between two messages is two rows.
  Putting the blank above the header rather than below the body is what makes
  the grouping read correctly.
- `1 of 3` on the title bar is the position within the subject, which is what
  a caller in a grouped reader actually wants to know. Yellow.
- **The body is word-wrapped at the reader's width, never re-flowed into
  paragraphs.** A line too long for this terminal is broken at a word
  boundary; a line that already fits is printed as it is. Re-flowing would
  mangle ASCII art and code, and this board's callers write both. That needs
  one greedy word wrapper in the core; see the core asks.

#### Plain ASCII, 40 columns

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
1541 alignment disk ------------ 1 of 3
-- 412  Daytona  19 Sep 21:14
Anybody got a working 1541 alignment
disk? Mine died last week and the local
shop wants 40 quid for a service. Happy
to pay postage both ways.
[F2.12] C64> 
```

The `-- ` prefix is the whole difference, and it does exactly one job: it
says "this line is the board, the next line is the person". On a colour
terminal that job is done by the header being Yellow and LightGreen against a
LightGrey body. On a monochrome one there is nothing else to do it with, and
without it a message that happens to start with a date reads as part of the
header.

There is no blank line above the header on ASCII, because the `-- ` already
separates and 24 rows is one fewer than a C64 has.

Note this is the same `-- ` that opens every message in the body segment
files on the card (`PLAN-BULLETINS.md`, the `M####.TXT` format). A sysop
reading the card on a laptop and a caller on a VT220 see the same shape.
That is a coincidence worth keeping rather than fixing.

#### ANSI, 80 columns

```
         11111111112222222222333333333344444444445555555555666666666677777777778
12345678901234567890123456789012345678901234567890123456789012345678901234567890
 1541 alignment disk                                                    1 of 3 

412  Daytona  19 Sep 21:14
Anybody got a working 1541 alignment disk? Mine died last week and the local
shop wants 40 quid for a service. Happy to pay postage both ways.
[F2.12] C64> 

413  Mindcrime_the_Ripper  19 Sep 22:02
I have one. Yours for the postage.
[F2.12] C64> 
```

Handle is shown to 14 characters at 40 and its full 20 at 60 and up. At 40
with a five-digit message number and a 14-character handle the header is
exactly 33 columns, which is the worst case and it fits.

At 132 the layout is identical to 80. A message header is a label, not a
table, so there is nothing to widen. The body simply wraps later.

#### Crossing a boundary, and how the hierarchy stays legible in a scroll

The title bar always means **the subject you are in**. One meaning, one
place. What changes at a boundary is what gets announced above it.

Same forum, next subject:

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
[F2.12] C64> 

*** End of "1541 alignment disk".
 JiffyDOS on a breadbin         1 of 9 

420  Rob  20 Sep 08:30
Worth it? I keep going back and forth
on this one.
[F2.14] C64> 
```

Different forum:

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
[F2.14] C64> 

*** End of C64.
--> Next: Swap Shop, 3 new. Enter goes.
[F2.14] C64> 
```

and Enter again:

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
*** Now in Swap Shop.
 Anyone want a spare 1571?      1 of 5 

410  Daytona  18 Sep 20:44
I have a spare and no use for it.
[F3.6] Swap Shop> 
```

- `***` lines are Yellow on ANSI and PETSCII, plain on ASCII. They are
  events. Chat already uses `***` for joins and leaves, so this is the
  board's existing vocabulary.
- `-->` lines are Cyan. They are the board offering something.
- **A subject boundary does not pause.** Enter carries straight through, one
  `***` line and the new title bar. **A forum boundary does pause**, with
  one line and one keypress, and it buys the thing that makes the loop
  comprehensible: you always know which forum you are in. Without it somebody
  reads eleven messages and has no idea four of them were somewhere else.
  This is precisely rn's trick of changing what the default means at the end
  of a group, said in words instead of in a bracket list.
- `S` at the boundary skips the forum about to be offered. `Q` stops.

The end of everything:

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
[F4.6] Swap Shop> 

*** All caught up: 12 messages, 7
    subjects, 3 forums.

 Forums                          0 new 
```

Citadel 2.10 ended the loop by silently leaving you in the Lobby, and
Citadel-86 added `There are no more rooms with unread messages.` because the
silence was ambiguous. Take the fixed version, not the original: say `All
caught up`, and put the caller somewhere, not nowhere.

#### Every key echoes its word

Citadel's trick, and it costs four bytes:

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
[F2.12] C64> goto

*** Now in Swap Shop.
```

`G` prints `goto`, `L` prints `list`, `P` prints `post`, `R` prints `reply`,
`S` prints `skip`, `U` prints `unread`, a digit prints itself (which `files`
already does). It costs no rows, because the echo lands on the prompt line,
and it makes the scrollback a transcript.

**Enter and SPACE echo nothing.** The echo exists to disambiguate a single
letter; Enter has nothing to disambiguate and the next message appearing is
unmistakable. Printing `next` forty times in a session would be noise in
exactly the place where somebody is trying to read prose.

---

### S4. The listing, `L`

`L` lists the messages **in the subject you are standing in**. It is not a
list of everything in the forum: that is the subject list, one level up, and
it is reached with `Q`.

#### ANSI and PETSCII, 40 columns

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 1541 alignment disk            3 msgs 
 412 Daytona               19 Sep 21:14
*413 Mindcrime_the_Ripper  19 Sep 22:02
*414 Daytona               20 Sep 08:30
---------------------------------------
 * is new. A number reads it.
[F2.12] C64> 
```

Columns: `mark 1`, `num numW`, `sp 1`, `handle handleW`, `sp 2`, `date 12`.
`numW` is the digit count of the forum's highest message number with a
minimum of 3, and `handleW` is `rowWidth - numW - 16`. Freezing `numW` at 5
would waste two columns of handle for the years before any board gets there,
and this board has been bitten by frozen column widths more than once.

- **Column 1 is the unread mark**, LightGreen. That is the entire reason the
  listing exists in a subsystem whose point is a pointer. tin used `+` for
  the same job in its group index and it is the first thing the eye picks up.
- **A deleted message keeps its row and never carries the mark.** It is
  skipped by the reading loop and shown here, because a conversation with a
  silent gap reads as the board being broken while a visible `(deleted)`
  reads as moderated.
- **`L` opens on the page containing the pointer**, not at the top and not at
  the end. With nothing unread, the last page.
- A number typed here reads that message and returns to the message prompt,
  not to the listing. Jumping is a detour, not a mode.
- **No vote column.** See "Considered and rejected".

#### Plain ASCII, 40 columns

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
1541 alignment disk ------------ 3 msgs
 412 Daytona               19 Sep 21:14
*413 Mindcrime_the_Ripper  19 Sep 22:02
*414 Daytona               20 Sep 08:30
---------------------------------------
 * is new. A number reads it.
[F2.12] C64> 
```

The only difference is the title bar's dashed form, which `rowTitle` already
produces, and the closing rule, which `rowRule` already produces. This screen
needs nothing else from the ASCII grammar because the `*` is already carrying
the one distinction that matters. **That is the test of whether a screen was
designed for monochrome: if it needs no extra furniture, the colour was
decoration rather than information.**

#### ANSI, 80 columns

```
         11111111112222222222333333333344444444445555555555666666666677777777778
12345678901234567890123456789012345678901234567890123456789012345678901234567890
 1541 alignment disk                                                    3 msgs 
 412 Daytona               19 Sep 21:14  Anybody got a working 1541 alignment d
*413 Mindcrime_the_Ripper  19 Sep 22:02  I have one. Yours for the postage.    
*414 Daytona               20 Sep 08:30  Brilliant, sending a PM now. Do you wa
-------------------------------------------------------------------------------
 * is new.  A number reads it.  Enter carries on.
[F2.12] C64> 
```

The width buys a **preview of the message itself**, 38 columns of it, in
Grey. On a grouped listing every row has the same subject, so a subject
column would be 38 columns of identical text. The first words of the message
are the only thing on this screen that differs per row and is worth reading.

At 132 the preview grows to 90 columns and nothing else changes.

---

### S5. Help, `?`

Three help screens, one per level, each on the screen it applies to.
Citadel's `<?>` worked "anywhere" and so does this one.

Every one of them **must fit one page**. A help screen that pages every time
teaches people to hammer a key through it, which is the lesson the shell
rework already paid for.

Rows a caller cannot use are not printed, the way `filesHelp` hides the staff
rows. Somebody who may not start a subject does not see `P`; somebody who may
not reply does not see `R`.

#### Reading, 40 columns

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 Forums: reading
Enter   the next one you have not read
SPACE   the same thing
B       back up one and read it again
L       list this subject, numbered
1 2 3   a number reads that message
R       reply in this subject
P       start a new subject here
G       next forum with new
S       done with this subject, move on
U       unread again, from this one
?       this
Q  ESC  back to the subject list
---------------------------------------
```

14 rows. On plain ASCII the title row is `Forums: reading ----------------`
and the rest is identical, because a help screen is a table of two text
columns and a table needs neither colour nor reverse video. On ANSI and
PETSCII the key column is White and the description column is Grey.

#### Reading, 80 columns

```
         11111111112222222222333333333344444444445555555555666666666677777777778
12345678901234567890123456789012345678901234567890123456789012345678901234567890
 Forums: reading
Enter    the next message you have not read, wherever it is
SPACE    the same thing
B        back up one, read it again
L        list this subject, numbered
1 2 3    a number reads that message
R        reply in this subject
P        start a new subject in this forum
G        go to the next forum with something new
S        done with this subject, move on to the next
U        unread again, from this message on
?        this
Q  ESC   back to the subject list, again for the forum list
-------------------------------------------------------------------------------
```

Two texts per row, the short one under 60 columns, which is the split `files`
already uses.

#### The subject list, 40 columns

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 Forums: C64
Enter   read the new ones in this forum
1 2 3   a number opens that subject
cursors move the bar, Enter opens
P       start a new subject here
S       mark this forum read, move on
C       mark this forum read, stay
?       this
Q  ESC  back to the forum list
---------------------------------------
```

#### The forum list, 40 columns

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 Forums: what the keys do
Enter   read everything new
1 2 3   a number opens that forum
cursors move the bar, Enter opens
G       next forum with new
C       mark the whole board read
?       this
Q  ESC  leave the forums
---------------------------------------
```

The `cursors` row prints only on ANSI and PETSCII, which is the test `files`
already uses (`canPoint`, `files.cpp:679`).

**What this deliberately does not do:** reprint the option list after every
message. The modern Citadel text client does, verbatim `<B>ack <A>gain
<R>eply reply<Q>uoted <N>ext <S>top `, and rn's bracket list is the same
instinct. At 40 columns that line is most of a row, on every message, against
a 3 KB output budget. The trade taken here is that the hint line is printed
on arrival and `?` is always one key away. The one place the default *is*
spelled out is the forum boundary, where it changes, which is exactly where
rn spelled it out by flipping `[npq]` to `[qnp]`.

---

### S6. The editor

The numbered line editor with single-letter commands is the universal BBS
shape. Renegade's `MAIL1.PAS` line editor took `A C D F I L M O P Q R S T U Z
?`; every system in this family had something like it.

There is no full-screen editor on this board and there should not be one:
plain ASCII has no cursor addressing, `LineEditor` is one line
(`src/core/editor.h:47`), and a visual editor would redraw constantly, which
is the one thing this board's output budget cannot pay for.

**The editor clears the screen first**, because it is a view and not a
scroll.

#### Starting a new subject, 40 columns

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 New subject in C64

Subject: 1541 alignment disk

 1> Anybody got a working 1541
 2> alignment disk? Mine died last week
 3> and the local shop wants 40 quid
 4> for a service. Happy to pay postage
 5> both ways.
 6> 

[S]ave [C]ont [L]ist [E]dit [A]bort: 
```

#### Replying, 40 columns

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 Reply in 1541 alignment disk

 1> I have one. Yours for the postage.
 2> 

[S]ave [C]ont [L]ist [E]dit [A]bort: 
```

**A reply has no subject prompt at all.** The subject is the container, so
there is nothing to ask and nothing to type. That is a real gain from
grouping rather than a tidy-up: the old flat design had to ask, then prefill
`Re:`, then cut it to fit, then explain that editing it silently started a
different conversation. All of that is gone.

#### 80 columns

```
         11111111112222222222333333333344444444445555555555666666666677777777778
12345678901234567890123456789012345678901234567890123456789012345678901234567890
 New subject in C64

Subject: 1541 alignment disk

 1> Anybody got a working 1541 alignment disk? Mine died last week and the
 2> local shop wants 40 quid for a service. Happy to pay postage both ways.
 3> 

[S]ave  [C]ontinue  [L]ist  [E]dit a line  [A]bort: 
```

#### Plain ASCII, 40 columns

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
New subject in C64 --------------------

Subject: 1541 alignment disk

 1> Anybody got a working 1541
 2> alignment disk? Mine died last week
 3> and the local shop wants 40 quid
 4> for a service. Happy to pay postage
 5> both ways.
 6> 

[S]ave [C]ont [L]ist [E]dit [A]bort: 
```

The bracketed letters are already the plain-ASCII device. Nothing else is
needed here and nothing else should be added: this is the screen where a
caller is typing, and furniture around a text area is a distraction.

Rules, unchanged from the first version because they are right:

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
- **The subject is required** when starting one. The subject *is* the list.
  Empty at the prompt re-asks once, with `A subject is how people find it.`,
  and ESC at the subject prompt aborts the whole post before anything is
  typed, which is the cheapest place to let somebody change their mind.

Sourcing note: I could not verify from primary sources that Citadel's own
message entry used blank-line-then-Save/Abort/Continue, although it is widely
described that way. The flow above is recommended on its merits, not on that
attribution.

After a save:

```
--> Posted as 416 in "1541 alignment disk".
[F2.12] C64> 
```

One line, straight back to the prompt with the loop intact. Posting is not a
reason to be thrown anywhere.

---

### S7. Empty states and refusals

These are the screens that decide whether a board feels finished. Every one
of them is drawn here because "the empty case" is where the file manager
already collected a queued bug.

#### Nothing new anywhere

The action row stops being reverse, stops being LightGreen and stops being
the default. The bar opens on forum 1 instead.

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 Forums                     nothing new

 --> Nothing new since your last call.

  1 General                    218 msgs
  2 C64                         96 msgs
  3 Swap Shop                   41 msgs
  4 Off Topic                   12 msgs
  5 Sysop Notes                  7 msgs

 A number opens a forum. ? help.
Forums> 
```

The `-->` line is Grey rather than Cyan, because it is an answer and not an
offer. On plain ASCII the `===` rules are **not** drawn around it, which is
how ASCII says the same thing: no action row, no action rules.

#### An empty forum

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
 Off Topic                   0 subjects

--> Nothing posted in here yet.
--> P starts the first subject.

[F4] Off Topic> 
```

**And pressing a digit here answers from the emptiness**, rather than opening
a question whose answer is already known:

```
[F4] Off Topic> 4
--> There is nothing in here yet.
```

not `Subject number:` followed by `No subject with that number.` That is the
exact shape of the queued file-areas bug (`CLAUDE.md`, "A digit in an empty
file area opens a file-number prompt") and it should not be rebuilt here.

#### Refusals

Always LightRed, always one line, always saying the thing the caller can act
on:

```
[F5] Sysop Notes> p
--> Staff start the subjects here. R replies.

[F2.12] C64> r
--> An account is needed to post. R registers one at the main prompt.

[F2] C64> p
--> Twenty posts a day is the limit here. Tomorrow.

[F2] C64> p
--> Thirty seconds between posts. Try again in a moment.

[F2] C64> p
--> The card is nearly full. Tell the sysop.
```

Not "denied", not "permission error". The file manager already set this tone
and it should not drift.

#### A forum added since the caller's first visit

Same problem as a first visit, smaller, and it gets no question:

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
*** New to you: Swap Shop, 96 messages.
--> Showing the last 10. L lists them.
```

One line of explanation and two ways into the backlog. A question here would
interrupt a reading loop to ask about a forum the caller has not seen and
cannot have an opinion about.

#### A caller who has been away long enough that messages rolled off

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
*** 31 messages in C64 expired before
    you read them.
```

Said once, on arriving in that forum, in Yellow, before the first message.
Silently clamping the pointer would mean the board quietly decided something
on the caller's behalf and did not mention it.

#### A guest

```
--> Guest pass: the board will not remember where you got to.
```

Once, on the way in, Grey. Guests get no first-visit question, because there
is nothing to write down. They land on the forum list with the "last 10 in
each" behaviour applied silently, which is the sensible default and costs
them nothing.

---

## The first time somebody uses it

This is where these systems feel worst, because the honest state is "431
messages, all unread" and the honest behaviour is to show them all.

**Ask once, at the top, on the first ever visit, and apply the answer to
every forum.** Not per forum, and now especially not per subject: with three
levels, asking per container would be dozens of questions before the caller
has read a word.

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

On plain ASCII the title row is dashed and the rest is identical: the `[R]`
brackets are already the ASCII device and there is nothing to translate.

- **`L` is the default** and Enter takes it. A first-time caller wants to see
  what the place is like, not read a year of backlog. Ten in each across five
  forums is about fifty messages, a representative evening, and it is
  recoverable in both directions: `B` backs into what was skipped, `L` lists
  all of it, `U` re-arms anything.
- `R` is there because some people genuinely want the lot. `S` is there
  because some want to start clean. rn's catch-up offered a numeric third
  option, "mark all but the last N as read", which is the same idea as `L`
  with the number exposed; a fixed 10 is the right simplification for a board
  whose forums hold tens of messages rather than thousands.
- **ESC is `L`**, not an abort. There is no sensible cancel for a question
  that must be answered before anything can happen.
- **It never appears again.** Once there are pointers, there is no first
  visit.

---

## The keys

### Inside a subject, reading

| Key | Does | Where it comes from |
|---|---|---|
| `Enter` | the next message you have not read, wherever it is: this subject, then the next subject, then the next forum | rn's bracket default; already the continue key at this board's `[More]` |
| `SPACE` | identical | rn; MultiMail's "combination PgDn/Enter key" |
| `B` | back up one message and show it again | the Citadel text client's `<B>ack`; rn's `-` |
| `L` | the numbered listing of **this subject** | `files` uses `L` for exactly this |
| a digit | read that message; digits accumulate so 1234 is reachable | `files`; WWIV, where a digit picks a sub |
| `R` | reply in this subject. No subject prompt. | WWIV's `W`/`A`, the Citadel client's `<R>eply` |
| `P` | start a new subject in this forum | WWIV |
| `U` | from the message on screen onward, unread again, **in this subject only** | PCBoard's user-editable Last Message Read pointer |
| `G` | go to the next forum with unread | **Citadel `<G>oto`**, kept deliberately |
| `S` | done with this subject, move on | Citadel-86 `<S>kip`, marking sense inverted |
| `?` | the key list | `files` |
| `Q` `ESC` | back to the subject list | `files` |

### At the subject list

| Key | Does |
|---|---|
| `Enter` `SPACE` | read the new ones in this forum, oldest first; on a pointing terminal, open whatever the bar is on |
| a digit | open that subject at its first unread; `0` means ten |
| cursors | move the bar |
| `P` | start a new subject here |
| `S` | mark this forum read and move on to the next forum |
| `C` | mark this forum read and stay here |
| `?` | the key list |
| `Q` `ESC` | back to the forum list |

### At the forum list

| Key | Does |
|---|---|
| `Enter` `SPACE` | start the loop; on a pointing terminal, open whatever the bar is on, and the bar starts on the action row |
| a digit | open that forum's subject list; `0` means ten |
| cursors | move the bar |
| `G` | jump to the next forum with unread |
| `C` | catch up: mark the whole board read. **Confirms.** |
| `?` | the key list |
| `Q` `ESC` | leave the forums |

### The rule that makes three levels learnable

**`S` gives up on whatever you are standing in, and moves on. `C` marks
whatever you are standing in as read, and stays. `Q` goes back one level.**

One sentence, three keys, three levels. Nothing has a different meaning
depending on where you are, which is the whole difficulty with a tree.

### Keys deliberately not used, and why

- **`N`**, because at `[More]` it means stop. Covered above.
- **`E`** for enter-a-message, Citadel's key. `E` means erase in the file
  manager, and one letter should not mean "create" in one subsystem and
  "destroy" in another. `P` is unambiguous and is what the last thirty years
  has called it.
- **`Z`**, Citadel-86's forget-room. Argued above.
- **`A`**, which in the file manager means approve. Left free in case forum
  moderation ever wants a key, so it can mean the same thing in both.
- **`+` and `-`.** Votes are not being built. Left free so that if they ever
  are, they land where the plan already put them, and so that nothing else
  claims them in the meantime.
- **Stacked subcommands** (`R;Y;S;ALL`), PCBoard's. A command language inside
  a subsystem that is otherwise single keys.

### Why `S` does not confirm and `C` at the forum list does

Neither destroys a message. Both only move a pointer, and everything they
skip stays reachable by `L`, by number, and by `U`. The difference is
magnitude and recovery: `S` gives up one subject or one forum you are
standing in and looking at, `C` at the forum list gives up the whole board's
unread from a screen that is one keystroke from the shell. rn confirmed its
catch-up for the same reason and confirmed nothing else.

`C` at the *subject* list gives up one forum and does not confirm, which is
the same scale as `S`. This is a judgement about scale rather than about
destruction, written down as such so nobody "fixes" the inconsistency later.

### `U`, and why it earns a key

trn's source contains a small piece of interface manners worth stealing:
after the catch-up command, if you hit `u` when you meant `y`, it prints
`(If you meant to hit 'y' instead of 'u', press '-'.)`. The principle is that
a single-key action which cannot be undone should tell you the way back.

`U` sets the pointer to just before the message on screen, in that subject.
Everything from there on becomes unread again.

```
         1111111111222222222233333333334
1234567890123456789012345678901234567890
[F2.12] C64> unread
--> 412 onward is unread again.
[F2.12] C64> 
```

`S` says what it did and how to get back:

```
[F2.12] C64> skip
--> Done with that one. L still lists it.
```

With per-subject read state, `U` is finer-grained than it was in the flat
design and therefore more useful: re-arming one conversation no longer
re-arms everything posted after it.

---

## "New since I left", end to end

The headline feature, with the keystroke count.

**At login**, one line, only when there is something:

```
Welcome back, Daytona.
12 new in 3 forums. FORUMS reads them.
```

Same shape as the file manager's "3 uploads awaiting approval" and chat's
"You have mail": one line, Yellow, nothing when the number is zero. It must
cost no card reads worth mentioning.

**`FORUMS`**, or nothing at all if their account's `Start` is `Forums`, which
is already wired up (`src/core/users.cpp:66`).

**The forum list** appears, bar sitting on `--> Read everything new`.

**Enter.** From here the caller makes no further decisions: Enter, message,
Enter, message, through subject boundaries and forum boundaries alike.

**Keystroke count: `FORUMS`, Enter, then Enter once per message.** For a
caller whose `Start` is Forums it is Enter, then Enter per message. There is
no way to make that shorter and no reason to try.

### Things that must be true for this to feel right

- **A message posted while you are reading appears in your loop.** "Next
  unread" is computed each time, not precomputed into a list at the start. On
  a ten line board where two people read while one posts, this happens
  constantly and should just work.
- **The pointer advances when a message is displayed**, not when the caller
  moves past it. A dropped line costs one message. Every reader here behaves
  that way and the alternative is a second piece of state.
- **The pointer is written to the card on leaving a subject, on leaving the
  subsystem, and at logoff. Never per message.** A card write per message
  puts synchronous card work in the middle of the reading loop, which is
  exactly the shape that produced the 126 ms stall already on the books.
- **Nothing on the forum list touches the card.** Counts come from RAM.
  Citadel's whole per-user read state was designed around answering "is there
  anything new" with no disk access at all, and that is the right instinct on
  a microcontroller too.
- **A listing page costs one read, not one read per row.** The index is
  fixed-width, so a page is a seek and a block. `rows()` is called once per
  line by the core, so the plugin must have the page in hand before the first
  row. This is the hard rule from the file listing experience and it is the
  only performance requirement this design actually imposes.

---

## The sysop's side: CONFIG

The forums are sysop-configured at the top level. Callers create subjects;
they never create forums. **A forum with no config entry is not a forum**,
the same rule file areas already have.

### `CONFIG <plugin> [FEATURE]`: APPROVED, and it is a core change

Today `cmdConfig` takes exactly one argument, which names a page or a plugin
(`src/core/bbs_sysop.cpp:1100`). A plugin's per-record sub-pages are reachable
only by moving the cursor onto an `FF_ACTION` button and pressing Enter.

Rob, 2026-09-21: "yes yes 100% on the config stuff." Approved as a core
change, built with the forums.

```
CONFIG                     lists the pages
CONFIG FORUMS              the plugin page: enabled, levels, tuning
CONFIG FORUMS TOPICS       the forum editor
CONFIG FORUMS TOPICS 3     forum 3's own page, straight there
```

**This is a generalisation of `CONFIG` itself, not a forums affordance, and
it has a beneficiary beyond forums.** `CONFIG FILES AREAS` would work the
same way and would be a straight improvement on what file areas have now,
because a sysop who knows what they want could stop navigating to it. The
seam is shared; whoever builds it should know that. This document does not
design the file-areas change.

Four things to pin down, three of which are UX and one of which is a trap
this project has already sprung once:

- **Both routes must reach the same page.** A button and a typed name that
  produce different editors is exactly the bug this project keeps finding,
  where two code paths each work the column count out for themselves and
  disagree. One function builds the page; the button and the parser both call
  it.
- **The typed name is the better route on a dumb terminal, and that is worth
  saying out loud.** Plain ASCII has no cursor and no button to sit under, so
  `CONFIG files` already falls back to asking `Area 1 [C64 Downloads] open
  (y/N)?` one row at a time. `CONFIG FORUMS TOPICS 3` replaces four yes/no
  questions with one typed line. **This is the first CONFIG affordance that
  is easier on a VT220 than on SyncTERM**, and that is a good sign rather
  than an anomaly: naming a thing is a terminal-independent gesture and
  pointing at it is not.
- **An unknown feature must not fall through.** `CONFIG FORUMS NONSENSE` says
  so and stays at the prompt:

```
--> No NONSENSE page in forums. Try: TOPICS
```

  Silently opening the plugin page as though nothing was typed is the worst
  of the three options, because the sysop then edits the wrong page believing
  they are on the right one.
- **The editing guard.** `CONFIG` allows one session at a time and releases
  on save, cancel and `closeSession` (`bbs_sysop.cpp:1061`). The rule, now
  with a third level: **the guard is taken on the first `CONFIG` and released
  only when the caller leaves CONFIG entirely.** Cancel on a sub-page pops one
  level and does not release, because the sysop never left CONFIG. A dropped
  line at any depth releases everything, guard and nesting together. That is
  already written down in `CLAUDE.md` from the file-areas work; a third level
  makes it worse, not different.

### The pages

**`CONFIG FORUMS`**, ten fields against a `Form::kMaxFields` of 16:

| Row | Field | Kind |
|---|---|---|
| 1 | Enabled | `FF_CYCLE` yes/no |
| 2 | Read | `FF_CYCLE` level |
| 3 | Write | `FF_CYCLE` level |
| 4 | Admin | `FF_CYCLE` level |
| 5 | **Forums…** | `FF_ACTION`, opens the same page `CONFIG FORUMS TOPICS` does |
| 6 | Lines in a message | number, 4..40 |
| 7 | Keep per forum, KB | number |
| 8 | Posts per day | number |
| 9 | Subjects per forum | number |
| 10 | Rows per listing page | number |

Six rows spare, which is deliberate: an undeclared key a sysop hand-adds to
`system.cfg` is also listed on this page (`bbs_sysop.cpp:1152`), and the ten
`color_*` keys are exactly that. A sysop who themes three of them fills three
of the six. A sysop who themes all ten overflows, and **the page silently
stops at 16 fields** with no warning. Worth knowing; not worth designing
around until somebody does it.

**`CONFIG FORUMS TOPICS`**, eight `FF_ACTION` buttons showing each forum's
name or `not set`, exactly the shape `CONFIG files` already has for areas.

**Each forum's own page**, seven fields:

| Field | Kind | Means |
|---|---|---|
| Key | text, 8 lowercase alphanumerics | the folder on the card |
| Name | text, 24 | what callers see |
| About | text, 44 | the description column at 60 and up |
| Read | `FF_CYCLE` level | the forum appears, and can be read |
| Start | `FF_CYCLE` level | may open a new subject |
| Reply | `FF_CYCLE` level | may add to an existing subject |
| Mod | `FF_CYCLE` level | delete anybody's, pin, lock |

### Four levels, and what the forum equivalents are

File areas landed on `read / up / down / del` after starting with two, and
the argument was that one `write` level would have to mean both "can pull
files out" and "can push files in", which are not the same trust. The same
argument applies here and gives a different split:

- **`read`** falls back to the plugin's read. Default `all`, so guests read.
- **`reply`** falls back to the plugin's write. Default `users`.
- **`start`** falls back to **this forum's reply**. If you may add to a
  conversation here you may begin one, and a sysop who wants an
  announcements forum sets `start` higher.
- **`mod`** falls back to the plugin's **admin**, never to `start`. Deleting
  is the destructive one, so it fails shut: a forum that says nothing about
  moderation does not inherit permission to delete from permission to post.

**`start` and `reply` being separable is what makes "Sysop Notes, read only"
work properly**, and it is better than read-only: `read = all, start = co1,
reply = users` is a forum where staff post the announcements and anybody may
answer them. That is a real thing boards wanted and two levels could not
express.

### The wire format, and a documentation bug that just bit the file areas

Six parts after the key, **written here in the order the parser will read
them**:

```
topic1 = general | General | Anything at all | all | users | users | co2
                   ^         ^                 ^     ^       ^       ^
                   name      about             read  start   reply   mod
```

There is no legacy to preserve here, unlike file areas where `up` had to stay
where `write` used to be, so the order can read the way it is used.

**Document it in parser order and check the document against the parser, not
against the design.** This exact class of bug has just been found in the file
areas: `CLAUDE.md` documented the wire format as
`path | name | read | down | up | del` while `readKey` parses
`read | up | down | del`, so **a sysop following the documentation would have
put the download level where the upload level goes**, opening uploads to
whoever was meant to be allowed to download. Already corrected in
`CLAUDE.md`. It is the `docs` agent's own first rule, which is to check every
claim against the source rather than against other prose, and it is worth
naming here because the forums are about to write the same kind of line into
the same kind of file.

Two forums' worth of defence, both cheap: the sub-page under
`CONFIG FORUMS TOPICS` shows the parts as **labelled fields**, so the order
in the file stops being something a sysop has to know; and the levels are
`FF_CYCLE` pickers, so a level can never end up in the wrong column by being
typed one field early.

### How many forums: sixteen, and it is not a RAM question

The previous revision argued eight and used static RAM to do it. **That was
the wrong axis and Rob said so.** Forum definitions are config read at start,
a couple of KB at sixteen forums, and presenting that as a decision point
made it look like a cost when it is not one. The real per-caller cost is the
unread bookkeeping, it is now fixed at 20 bytes per forum, and it is settled
under item 1.

So decide it on the only thing left, which is **what a sysop concludes when
they read the number**:

- **Sixteen, because it is where `Form::kMaxFields` actually puts the wall.**
  A `TOPICS` page is one button per forum and a form holds 16 fields. That is
  an honest ceiling rather than a chosen one, and an honest ceiling is easier
  to explain and harder to argue with.
- Eight reads as a limit somebody picked. Sixteen reads as the shape of the
  machine. The board has already been through this once with `max_users`,
  where a hundred accounts came from a leftover partition size and nothing
  real.
- What actually grows is subjects, and they are unbounded. **The docs should
  say "up to 16 forums, with as many subjects in each as the card holds"**,
  because a sysop who reads a bare "8" or "16" and thinks it means that many
  conversations will conclude the board is tiny.

The knock-ons are a `uint8_t` becoming a `uint16_t` (`g_ptrDirty`, a bitmask
of forums with a moved pointer) and the `PTRS.TXT` record going from 320 to
640 bytes of read state. Noted as consequences, not as arguments.

---

## Where this contradicts the plan, and the first one is expensive

Four things. The first two are storage decisions that the UX forces, and
`PLAN-BULLETINS.md` is explicit that `INDEX.TXT` and `PTRS.TXT` are the
decisions that are expensive to change later. **These want settling before
phase 1, not after.**

### 1. Unread bookkeeping: the mark plus a window, and nothing per subject

**This section was wrong in the previous revision and has been reworked.**
Rob's objection is the correct one and it is worth stating in his words
before the design, because it is the constraint:

> "Why do forums need RAM beyond the ability to read the setup from a config
> file. Size doesn't matter if a kid has 3 topics on some board, it is what
> it is."

He is right twice. **Forum definitions are config**, read at start: name,
description, four levels. That was never the cost and presenting it as a
decision point made it look like it was. And the previous revision's answer
to the unread problem, a permanent 2-byte slot per subject per caller,
**charged a board with three subjects for a capacity it would never use**.
That is exactly the shape he is rejecting.

**The constraint, stated as a test the design has to pass:** the bookkeeping
cost scales with what is actually on the board and with what a caller has
actually missed, never with a maximum.

#### The problem is still real, and here it is again

The plan has one high-water pointer per forum: everything at or below it is
read. That works perfectly for a flat forum and it breaks the moment there is
a subject list, because a high-water mark is a statement about *message
numbers* and subjects interleave in message-number order.

Using Rob's own example:

- `HAM RADIO` has "20m tips and tricks" holding messages 100, 104 and 109,
  and "20m antennas" holding 101, 102, 103, 105, 106, 107, 108.
- The caller opens the tips subject and reads all three. The newest is 109.
- A forum-wide mark of 109 now says messages 101 to 108 are read too. **The
  antennas subject shows `0 new` although they have read none of it.**
- Refuse to move the mark instead, and the opposite happens: the tips subject
  still shows `3 new` immediately after they read all three.

Either way the number on the subject list is a lie, and **it is the first
thing a caller checks after reading something**.

**The requirement, unchanged and still the right test:** the count beside a
subject goes to zero when that subject has been read, and a forum's count is
the sum of its subjects' counts. Both, always.

#### The design: a mark, and a window of what was read above it

**Per caller, per forum: the high-water mark, plus a fixed bitmap of messages
above it that have already been shown.**

```
ptr      uint32   everything at or below this is read
window   16 bytes bit i = message (ptr + 1 + i) has been read
```

Bit 0 is `ptr + 1`. Sixteen bytes is a 128-message window.

Three operations, all of them a few instructions:

- **Show message N where N == ptr + 1:** advance `ptr`, then keep advancing
  while bit 0 is set, shifting the window right as you go.
- **Show message N where ptr + 1 < N <= ptr + 128:** set bit `N - ptr - 1`.
- **Show message N beyond the window:** record nothing. See the failure mode.

Walk Rob's example through it. `ptr = 99`, the caller reads the tips subject:

| Action | Result |
|---|---|
| show 100 | `100 == ptr+1`, so `ptr = 100`, window shifts |
| show 104 | set bit 3 |
| show 109 | set bit 8 |

Now: tips unread = its messages above 100 that are not marked = {104 marked,
109 marked} = **0**. Antennas unread = {101,102,103,105,106,107,108} = **7**.
Forum unread = messages above 100 not marked = **7**. Sum of subjects = 0 + 7
= 7. **Consistent, and both screens tell the truth.**

Then they read antennas in order. 101, 102, 103 each advance the mark; on
reaching 103 the mark sees bit for 104 already set and **advances through it
by itself** to 104. 105 to 108 advance it to 108, and 109's bit carries it to
109. The window is empty, the mark is at 109, everything is read, and no
bookkeeping is left behind. **The window self-drains**, which is what stops
it filling up over a long session.

#### What it costs

| | Bytes |
|---|---:|
| `ptr` + `window`, per forum, per caller | 20 |
| 16 forums | **320** per caller, on the card |
| In RAM, per session: 16 marks | 64 |
| In RAM, per session: the current forum's window only | 16 |
| In RAM, 12 sessions | **960** |

**Fixed. It does not move with the number of subjects, the number of
messages, or the size of the board.** Rob's kid with three subjects pays 320
bytes on the card and so would a board with twelve million messages. That is
the test passed.

Against the previous revision's slot array at 4 KB per caller on the card,
this is **thirteen times smaller and does not scale with anything**. That
revision was wrong and this one is the correction, costed.

In `PTRS.TXT` the window is written as 32 hex characters so the file stays
readable on a laptop, which is a property the plan deliberately bought. Per
forum that is `6 digits + TAB + 32 hex + TAB` = 40 bytes, so 16 forums is 640
bytes of a record around 700. At 250 ids that is 175 KB on a card with
gigabytes, and the plan already accepted the `(highest id + 1) x record`
shape.

#### Where the unread counts come from, with no per-subject storage

One backward scan of `INDEX.TXT`, from the newest message down to `ptr`,
tallying by subject hash and skipping anything the window has marked. The
forum's total is the same tally summed, which is why the two screens cannot
disagree: **they are one number computed once.**

**The scan covers exactly the messages the caller has not read.** Somebody up
to date scans nothing. Somebody back after a week scans a week. Somebody back
after a year scans what the roll-off left. That is the same principle one
more time: you pay for what you actually missed.

Costs, so it is a measurement and not a claim:

- A record is 128 bytes, so 100 unread messages is a 12.8 KB sequential read.
  It happens once on entering a forum, not once per row, which is the
  distinction that matters and is what `findDesc` got wrong.
- **This goes in the phase 2 commit with `plat::micros()` around it**, which
  is what `CLAUDE.md` asks for before message bases and what the 126 ms stall
  taught.
- The tally needs one slot per subject **that has unread**, capped at
  `list_rows`. That is 8 bytes a row, 160 bytes per session at 20 rows.

`SUBJ.TXT`, one fixed-width record per subject that exists, carries the hash,
the display text, the message count and the newest message number. **That is
board data, not per-caller data**: three subjects is a 240-byte file. It is
what makes a subject list row one seek and one read instead of a scan, and it
is what holds the permanent subject number.

#### The failure mode, stated because every design has one

**A message read more than 128 numbers above the caller's mark cannot be
recorded, so it stays unread and is shown again.**

The direction of that failure is the point: **it never marks an unread
message read, it only forgets that one was read.** Showing somebody a message
twice is a mild annoyance they can explain to themselves. Hiding one they
have not seen is the bug, and it is silent. Of the two ways to be wrong, this
design can only be wrong in the harmless direction.

How reachable is it? The window only fills when reading **out of order**, and
only spans from the mark to the newest message read. A caller three subjects
behind, each ten messages deep, uses about thirty of the 128. A caller five
hundred messages behind who opens only the newest subject exceeds it and
re-reads that subject next call.

#### SETTLED: sixteen bytes, 128 messages

Rob's ruling, 2026-09-21. The number and the reasoning, recorded together
because in six months only the number will be obvious:

- **128 messages is comfortably past realistic out-of-order reading.** The
  window only fills when a caller reads out of order, and it only has to span
  from their mark to the newest message they read. Three subjects behind at
  ten messages each uses about **30 of the 128**. Doubling it would buy
  headroom against a case nobody has described.
- **The failure direction is what makes 16 safe rather than merely small.**
  Overrun the window and the board forgets that a message was read, so it
  shows it again. It can never do the opposite. Showing somebody a message
  twice is a mild annoyance they can explain to themselves; hiding one they
  have not seen is a silent bug. A design that can only be wrong in the
  harmless direction does not need much margin.
- Thirty-two bytes would have bought a 256-message window for 320 more bytes
  per caller on the card and nothing in RAM. Cheap, and not needed.

**This number is load-bearing and it is fixed-width.** It is not a runtime
setting, it is not a CONFIG value, and **raising it later means converting
every `PTRS.TXT` on every card**, which is exactly the class of migration
`PLAN-BULLETINS.md` exists to avoid. Sixteen is the number; write it into the
format version so a future build can at least tell what it is reading.

#### What must not happen

The subject list shipping with counts computed from a bare forum-wide mark.
That is the version where the board looks broken, and it looks broken in the
one screen Rob asked for.

### 2. The subject hash: SETTLED, and the vote fields pay for it

Rob's ruling, 2026-09-21. **A 4-byte subject hash in the fixed record, with a
reply carrying its parent's hash rather than matching on the subject string.**
Every screen above assumes it.

**Where the nine bytes come from.** The record stays exactly 128 bytes, four
to a sector, and that alignment is worth keeping. Eight hex digits plus a tab
is nine bytes. The `up` and `down` vote fields plus their tabs are eight.
Take those and one character off the subject:

| Field | Was | Now |
|---|---|---|
| `up` + TAB | 4 | gone |
| `down` + TAB | 4 | gone |
| subject hash + TAB | 0 | 9 |
| subject text | 50 | 49 |
| **total** | **128** | **128** |

#### Say this in the right words: votes are closed off, not merely unbuilt

`PLAN-BULLETINS.md` says votes are "dropped from the first version" and can
be added later "as an additive file without touching the message format".
**That sentence is now only half true and the half that changed is the
important one.**

- A votes **file**, recording who voted on what, is still additive and still
  possible. Nothing here touches it.
- A vote **tally in the index**, which is what a score column in a listing
  would need, is **gone**. The bytes it lived in are the subject hash now. Any
  future build that wants a score in a listing is converting every
  `INDEX.TXT` on every card, which is the one migration this whole format was
  designed to never need.

**Rob took that decision knowing it.** It is recorded here in those terms so
that nobody later reads "votes were skipped" and assumes the door is still
open. It is shut, deliberately, and the thing that shut it is a feature that
was actually wanted.

This is consistent with the recommendation this report already makes on its
own grounds: **no score column in any listing**, because reading order is
chronological by design, a score you can see and cannot sort on is an
invitation to ask for sorting, and sorting is what the design rejects. The
storage and the UX now agree rather than one of them merely tolerating the
other.

**The subject text is a cached render, exactly like the author handle.** The
hash is the key and the text is a picture of the name, allowed to be old.
That is the same rule the identity work already established for handles, and
it means the subject list shows the **root** message's text, which is what
the person who started the conversation called it.

**Duplicate names need no display rule at all, because the number already
disambiguates them.** Two people posting "Help" a month apart are two
subjects with two hashes, correctly, and they render as `11 Help` and
`3 Help` because the row number is the subject's permanent number rather than
its position. Argued in full under S2; the short version is that a date
prefix costs 8 of 25 columns at 40 and **fails on the case it exists for**,
since two "Help" subjects started the same evening get the same date.

**The identity is the hash and never the display string.** That is what makes
the disambiguation safe: nothing about how a subject is drawn, truncated,
numbered or renamed can split or merge a conversation, because no code path
compares subject text. This is the same rule the identity work already
established for handles, one level along.

A 32-bit hash over a few hundred subjects per forum gives a birthday
collision probability around 0.003%, and the failure mode if it ever happens
is two conversations merging, which is visible and not destructive. Scope the
hash per forum.

### 3. `Re:` is dropped. SETTLED by Rob, 2026-09-21

> "I don't care how you do the grouping, doesn't need to be Re:, I just was
> listing an example."

The plan says a reply "carries it forward as `Re: ...`". With grouping and a
hash, `Re:` is doing nothing:

- The subject is drawn once as the screen's title, so `Re:` would appear on
  a title bar that is already the subject.
- The reply never asks for a subject, so there is nothing to prefix.
- Matching is by hash, so nothing depends on the string.
- On a 40-column subject list, `Re: ` is 4 of 25 columns spent saying "this
  is a reply" in a design where every message after the first one is.

Drop it. This is a one-line change to the plan and it costs nothing.

### 4. `Session::listIdx` still shapes both list screens

Not a contradiction, a constraint the plan already names and that now applies
in two places rather than one. `listIdx` is a `uint8_t` and is the row
counter in every list on this board, so **the subject list must be windowed
as well as the message listing**, `list_rows` at a time. A forum with 300
subjects listed in full would wrap at 255 rows.

---

## How it degrades

### PETSCII at 40 by 25

Designed first, and everything above is measured at it. What changes from 80:

- The forum list loses the description and last-post columns and keeps the
  count. 12 rows with five forums, 15 with eight.
- The subject list loses the last-post column; the subject keeps 25 columns.
- The message listing loses the preview column.
- Every hint line has a short form, at the `cols >= 60` test `files` already
  uses.
- The editor types 35 columns instead of 72.
- Help gets its short texts and still fits one page.

Everything else is identical, including every key. **No key and no screen
exists only at 80.**

PETSCII has reverse video (`0x12`/`0x92`) and cursor movement, so it gets the
highlight bar exactly as ANSI does. It does **not** have `eolClear`, so the
bar redraw pads with spaces, which is why the list screens must redraw only
their own rows rather than the whole screen. `files` solved this with
`Draw::Again` (`src/plugins/files.cpp:924`) and the same approach applies,
with the two-rows-only refinement specified under the byte budget.

PETSCII at 80 (`TermType::Pet80`) gets the ANSI 80 layouts unchanged. The
only thing to watch is that `Glyph::HLine` is `0xC0` there, which draws a
line through the middle of the cell rather than CP437's `0xC4`; both read as
a rule and neither needs special-casing.

### Plain ASCII

No cursor addressing, no reverse video, no colour. `cls` prints two blank
lines (`src/core/term.cpp:327`), `rowTitle` falls back to a dashed rule, and
there is no bar.

**Nothing in the design depends on any of that**, and that is the test it was
built to pass. The lists are numbered and the hint line says so; Enter still
starts the loop, because Enter is a key and not a cursor gesture; `?` still
works; the command echo still tells you what you pressed. The `*`, the `-->`,
the `***` and the `===` carry everything the colour was carrying.

Three things to get right, and they are the only ASCII-specific work:

- The action row needs its `===` pair, and **must not have it when there is
  nothing new**, because then it is not an action row.
- The hint line must say what Enter does in words, since there is no bar
  sitting on anything to make it obvious.
- `-- ` goes in front of a message header, and nowhere else. It means "board,
  not person", and using it for anything else spends the distinction.

### A slow link

- **Nothing in this subsystem redraws on a timer.** There is no refresh
  screen anywhere in it, by design. DASH's frame-too-big-for-the-buffer
  argument is the reason, and no feature here wants that shape.
- **One message per keypress and nothing else on the wire.** The hint line
  prints on arrival and on `?`, never above every prompt. This is the one
  place the design deliberately diverges from `files`, which reprints its
  hint with every prompt: fine for a place you visit twice, wrong for a place
  you sit in for forty messages.
- **The body goes through `startPluginList`**, for the reasons and with the
  arithmetic under the byte budget.
- `C` at `[More]` turns paging off for the rest of that message, for somebody
  on a fast link who does not want the interruptions.

---

## What this needs from the core

Six things, none large. Items 1 to 3 are new since the first version; items 4
and 5 were in it; item 6 is a bug rather than a feature.

### 1. `rowTitle` must truncate its title

`src/core/bbs_shell.cpp:565` calls `t.text(tl, title)` with no length limit
and then pads to the width. Every existing caller passes a short literal, so
nothing has ever overflowed.

**The forums pass a caller-supplied subject of up to 49 characters into a bar
that is 39 columns wide on a C64.** It will wrap on the first day, break the
reverse bar across two rows, and leave the tail sitting under it.

Fix it in `rowTitle` rather than in the plugin: cut the title to
`w - rlen - 3` with `Term::textCols`, which already counts columns rather
than bytes and is the thing that stops a multi-byte character being split.
Doing it in the plugin means the next plugin with a variable title rebuilds
the same guard and gets it wrong.

### 2. `rowBar(s, Color, title, right)`

`rowTitle` hardcodes Cyan. The action row needs the same bar in LightGreen,
and a theme needs `color_title` to be settable at all.

Pure refactor, no behaviour change: `rowBar` takes the colour, `rowTitle`
calls it with Cyan. Every existing call site is untouched.

### 3. `Glyph::HLine2`, the double rule

One enum value appended to `Glyph`, three cases: CP437 `0xCD` on ANSI,
`0xC0` on PETSCII (the C64 has no double rule, and a single one reads as a
rule), `=` on ASCII. Appending to `Glyph` is safe because it is an enum class
in a switch, not one of this project's positional descriptor tables.

Without it the `===` device has to be a literal `=`, which is fine on ASCII
and wrong on ANSI, where a CP437 double rule is the thing a 1992 board would
have drawn.

### 4. A word-wrap helper

Message bodies must be wrapped at the reader's width and nothing in `Term` or
`Bbs` does that. It is the same helper the queued "profile text should word
wrap" item needs. Wrap at `rowWidth(s)`, break on spaces, break a word longer
than the width rather than overflowing it, and emit one row per call so it
can drive `rows()` directly. `PLAN-BULLETINS.md` already names it `bbsu::wrap`.

### 5. `screens/forums`

Add it to SCREENS.md's optional list beside `files`, with the same door
semantics: it plays on the way in, the forum list draws underneath, and
entering does not wipe it. **Budget 8 rows at 40 and 8 at 80.** The forum list
is 12 rows with five forums and 15 with eight, and a C64 has 25.

### 6. The `listDone` hook: already done, keep it that way

The first version of this document reported that a plugin gets no chance to
redraw its prompt when a list is aborted, and called it a live bug in `files`.
**It is fixed.** `src/core/plugin.h:246` declares
`void (*listDone)(Session& s, bool aborted)` and `src/core/bbs.cpp:2213`
calls it. `files.cpp:651` uses it.

Noted here because the forums will hit that path constantly: aborting a long
message body is normal rather than an edge case, and it is the two-key
substitute for Citadel's `<N>ext`.

---

## Findings in the source, worst first

Found while designing against the code rather than against the plan. The
first one is live and affects permissions.

### A. `CONFIG files` silently drops an area's Download and Delete levels

**Live in 0.19.0, widens a permission, independently confirmed, and
scheduled: it rides with the forums build as part of phase 1, not before
it**, because the forums need the same fix for their seven parts and doing it
once is doing it once.

**The regression test that has to come with it:** save a six-field area
through `CONFIG files` without changing anything, and assert all six fields
survive byte for byte in `system.cfg`. A test that only checks the save
succeeded would pass against the broken code, which is this project's oldest
lesson about tests that agree with the implementation.

`src/core/bbs_sysop.cpp:821` declares `kAreaParts` with six entries: Path,
Name, Read, Upload, Download, Delete. `kComposites` at line 839 registers it
with a count of **4**, and `kMaxParts` at line 843 is **4**.

So:

- The sub-page shows only Path, Name, Read and Upload. A sysop cannot see or
  edit Download or Delete from the board at all.
- `configSubSave` (line 1364) packs `comp->count` parts, so **saving writes
  four parts back over a six-part line**, discarding whatever Download and
  Delete were set to.
- `files.cpp:2235` then falls back: `down` becomes the area's **read** level,
  `del` becomes the plugin's admin.

Concretely: an area configured `pub/c64 | C64 | all | users | co1 | co1`
means "everybody sees it, users may upload, only co-sysop 1 may download or
delete". Open it in CONFIG, change the name, press Save, and it becomes
`pub/c64 | C64 | all | users`, which means **everybody may download it**.
The sysop is told "Saved and live" and nothing says a permission moved.

Verified against `files.cpp:259`: `mayDown` falls back to the area's own
`read` before falling back to the plugin's, so **download fails open**, and
`mayDel` falls back to the plugin's admin, so **delete fails shut**. Two
permissions move in opposite directions, neither is mentioned, and the sysop
is told "Saved and live".

The fix is `kMaxParts = 8` and a count of 6, and it is the same change the
forums need for their seven parts, so whoever does one does both.

This is the review agent's own third pattern wearing a different hat: a guard
sized for the old shape, left behind when the shape grew.

### B. `Term::text` ignores a failed `put` and drops characters silently

`Timeline::put` returns false when the data does not fit
(`src/core/timeline.cpp:59`), and `ByteSink::putc`, `Term::ch` and
`Term::text` all discard the return. A full timeline therefore does not
error, it loses bytes out of the middle of a row.

Nothing currently draws a frame large enough to reach it outside the list
machinery, which is guarded at 512 bytes free. **The forum list at 132
columns with sixteen forums is 3,326 bytes, which is larger than the whole
timeline**, and a naive port of the way `files` draws its area menu would
draw it directly on entering the subsystem. That is the first frame on this
board big enough to matter, and it would fail by losing characters rather
than by erroring.

The design answer is in this document: draw both list screens through
`startPluginList`. The core answer, if anybody wants one, is that a dropped
`put` should at least be counted somewhere a sysop can see, because a screen
that is silently missing a character is indistinguishable from line noise.

### C. `Color::Grey` and `Color::LightGrey` are the same colour on ANSI

`kAnsiColor[]` at `term.cpp:63` maps index 12 and index 15 both to `{0, 37}`.
`Orange` and `Brown` are likewise both `{0, 33}`. They are genuinely
different on PETSCII.

Not a bug, but it means a colour scheme that distinguishes them works on a
C64 and is invisible on SyncTERM, and nobody would learn that from the enum.
Nothing in this design depends on it.

### D. `cmdConfig` takes one argument and has no room for a second

`src/core/bbs_sysop.cpp:1100` takes `arg` whole and passes it to `pageByName`
and then `plugins::indexOf`. Rob's `CONFIG <plugin> <feature>` needs the
argument split at the first space, and the second token routed to a
per-plugin feature table. Small, and it is a core change rather than a plugin
one, which is worth stating before somebody tries to build it inside the
forums plugin.

### E. The composite sub-page is one level deep, not a stack

`g_subComp` is a single pointer (`bbs_sysop.cpp:860`), so one nesting level
is all there is. `CONFIG FORUMS` -> `Forums…` button -> a forum's own page is
two levels.

The cheap answer, and the one this document assumes, is that **the topics page
is a peer rather than a child**: reached by name or by button, it replaces the
plugin page rather than nesting under it, and one byte remembers where to
return. That keeps `g_subComp` a single pointer and keeps the guard rule
simple. Turning it into a stack is the other answer and is more code for a
depth nobody has asked for beyond three.

### F. `visibleAreas` computes its column count from `s.term.cols()`, not `rowWidth`

`files.cpp:703` uses `s.term.cols()` directly with its own zero guard. That
is correct today and it bypasses the 132 cap, so on a terminal reporting 200
columns the area menu would lay out for 200 while every bar and rule around
it stops at 132.

The forums do not have this problem because neither list screen is a grid.
Noted so the same arithmetic is not copied across.

### G. The file-area wire format was documented in the wrong field order

Found while the `CONFIG files` finding above was being verified, and
**already corrected in `CLAUDE.md`**. The documentation gave the format as
`path | name | read | down | up | del`; `readKey` in `files.cpp` parses
`read | up | down | del`.

So a sysop editing `system.cfg` by hand from the documentation put the
**download** level where the **upload** level goes, and vice versa. On an
area meant to be "anybody may download, members may upload" that hands
uploads to everybody. Silent, and it fails open.

Recorded here for two reasons. It is the second permission bug in the same
six fields in one afternoon, one in the editor and one in the prose, which
says that a packed bar-separated value is a format that invites this and that
the labelled sub-page is worth having for more than convenience. And the
forums are about to write the same kind of line into the same kind of file,
so their wire format is specified in this report **in the order the parser
will read it**, with the reason written next to it.

---

## What stays exactly as it is

A consultant who finds everything wrong is not reading carefully.

- **`rowTitle`'s three-way fallback is right** and is the model for
  everything in this document. Reverse bar, reverse bar, dashed rule, chosen
  by terminal type, with the padding done in columns rather than bytes. Every
  screen here is built on it.
- **`rowWidth` since 0.17.10 is right**, including the unknown-width-gets-40
  rule and the 132 cap. Nothing here needs to work around either.
- **`Term::reverse(o,false)` re-asserting the colour is right** and is the
  reason a reverse bar can be used freely in this design. The SGR 27 history
  is in `CLAUDE.md` and does not need relitigating.
- **`files`' `canPoint()` split is right** and is reused unchanged: cursor
  and reverse video on ANSI and PETSCII, numbers everywhere.
- **Chat's colour keys are the right precedent** for a theme and are copied
  rather than improved on. Nine keys, `colorByName`, defaults in code, not
  declared as settings.
- **The `[S1] Files>` prompt shape is right** and is extended rather than
  replaced. `[F2.12] C64>` is the same idea with one more level in it.
- **`startPluginList` and `rows()` are the right machinery** and this design
  uses them for everything that could be long, including the two list
  screens, which `files` draws directly and gets away with only because its
  menu is small.

---

## Considered and rejected

- **Threading, in any form.** Settled by the owner, and the terminal
  arithmetic agrees independently: a two-level indent costs 4 of 25 columns
  of subject at 40 and a three-level tree costs 6, and a tree only pays when
  you can see enough of it at once to navigate, which needs twenty-odd rows
  of subjects. A C64 has 25 rows in total. trn drew its article tree in the
  upper right of an 80 column header, which is the first thing that does not
  exist here. **Subject grouping gives the thing people actually wanted**,
  which is following one conversation, and it is what `rn` and `tin` called
  threading anyway.

- **Per-subject prompts, title bars and questions.** Three levels is already
  two more places to be than `files` has. Adding a per-subject first-visit
  question, or a per-subject hint line, would turn the subsystem into
  administration. One question at the top, applied everywhere.

- **A separate "read everything" reader.** It would be a clean mode and it is
  the wrong shape: two code paths drawing the same message header, and a
  caller who presses Q at the end of it landing nowhere in particular. The
  fast path is auto-navigation into the real tree.

- **Vote-sorted reading order.** Settled, and mechanically impossible anyway:
  a chronological pointer has no "next" in a score-ordered list.

- **A score column in the listing.** Votes are not being built, so this is
  moot for now. The argument is kept because it will come back: a score in a
  listing is a number you can see and cannot act on, reading order is
  chronological by design, so a score column is an invitation to ask for
  sorting and sorting is what this design rejects. A UI element whose only
  possible next step is a ruled-out feature should not be drawn. At 40
  columns it also costs 4 of 25 subject columns, and the subject is what
  decides whether somebody reads a message.

- **rn's read-ranges instead of a count per subject.** `.newsrc` recorded
  `group: 1-78,80,85-90`, a sorted set rather than a high-water mark, and
  that is why rn could offer mark-unread and let you jump about without
  losing track. It is a better data model. It is rejected because a range
  list is variable length, and this design rests on fixed-width records that
  can be seek-and-overwritten. **The per-subject count recommended above is
  the middle ground**: it is finer-grained than one forum-wide mark and still
  fixed width.

- **A separate `NEWSCAN` command**, as WWIV and PCBoard had. Enter at the
  forum list is the scan. A second verb is a second way to do the only thing
  the subsystem is for, and the shell rework has just finished taking
  commands off the menu.

- **Per-forum subscribe and unsubscribe** (rn's `u`, Citadel-86's `<Z>ap`).
  Per-account per-forum state to save one keypress on a board capped at
  sixteen forums, plus an undo that Citadel itself got wrong. `S` gives the
  useful part for free.

- **A per-subject read pointer.** The obvious answer to the subject-count
  problem and the one this report recommended in its first revision. It
  charges every caller for every subject that has ever existed, which on a
  board with three subjects is charging for capacity nobody will use, and it
  needs a windowed read on the caller's path to stay inside the RAM budget.
  A mark plus a fixed window is thirteen times smaller, scales with nothing,
  and fails only in the harmless direction.

- **A "new since your last call" divider row in the listing.** It carries
  exactly the information the `*` marker carries, which is cheaper and works
  when the listing is scrolled.

- **Clearing the screen between messages.** It would match the file manager
  and it is wrong: the previous message is the context for this one. Views
  clear, scrolls do not.

- **Two-line listing rows at 40 columns.** It would give the subject 35
  columns instead of 25 at the cost of halving the messages on a screen. The
  listing's job is finding a number to jump to, not reading, so more rows
  wins.

- **Header rows on the list screens.** The old version specified `New` and
  `Last post` headers at 64 and up. `5 new` and `Daytona 19 Sep` label
  themselves, and a header row is one more thing to keep aligned under a
  title bar that is already saying what the screen is.

- **A grid of forums at 80 or 132**, the way `files` packs its areas. An area
  is nothing but a name; a forum has a description and a last message worth
  reading. Width goes to content, not to more columns of the same thing.

- **Stretching the 132-column layout to fill the width with padding.** The
  last column becomes a preview of the newest message instead. Adding content
  is using a wide screen; padding a table is spreading it thin.

- **Reprinting the option list after every message**, as the modern Citadel
  client does. At 40 columns it is most of a row per message against a 3 KB
  output budget.

- **A full-screen visual editor.** No cursor addressing on plain ASCII, and
  constant redraw on a board whose output budget is 3 KB per session.

- **Re-flowing paragraphs to the reader's width.** It mangles ASCII art and
  code, and this board's callers write both. Wrap, never join.

- **An offline reader (QWK or Blue Wave).** The board now has YMODEM, so it
  is genuinely possible, and the batch model is the wrong trade for a ten
  line online board. Worth revisiting only after the online reader exists, as
  an addition rather than an alternative.

- **Using `-->` only on plain ASCII.** Tempting, because it is where it does
  the most work. Rejected because chat already prints `kMark` on every
  terminal and merely colours it, and a caller who dials in from SyncTERM one
  night and a C64 the next should see the same words. The arrow is the
  board's voice; the colour is the terminal's luxury.

---

## Implementation order, cheapest and most visible first

1. `rowBar` and the `rowTitle` truncation. Two small core changes that
   everything else draws with.
2. The forum list and the prompts, at all three widths and in all three
   flavours, through `startPluginList`. Everything else hangs off knowing
   where you are.
3. The subject list, which is the same drawing routine with different
   content. Building it second proves the shared grammar rather than
   assuming it.
4. The reading loop: Enter, the header, the body through `rows()`, the
   command echo, the two kinds of boundary, the end of the loop. This is the
   product and it is worth having before posting exists.
5. The core word-wrap helper, which item 4 needs.
6. The message listing `L`, `B`, jumping by number, and `U`.
7. The first-visit question and the never-opened-forum banner.
8. Posting and the line editor, then `R`.
9. `CONFIG FORUMS TOPICS`, and with it the `kMaxParts` fix that also repairs
   the live file-areas bug. **The fix and its regression test belong in
   phase 1**, not at step 9; it is listed here because the forums' own
   seven-part sub-page is what exercises it.
10. The theme keys, which are ten lines in `readKey` and touch nothing else.

The two-rows-only bar redraw belongs with item 2, not as an optimisation
afterwards, because the direct port of `files`' full-menu redraw is 2,896
bytes per cursor keypress at 132 columns.

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

**This board**

Everything in "The presentation system", "The byte budget" and "Findings in
the source" was read out of the tree at 0.19.0, not recalled:
`src/core/term.h`, `src/core/term.cpp`, `src/core/bbs_shell.cpp`,
`src/core/bbs.cpp`, `src/core/bbs_sysop.cpp`, `src/core/form.h`,
`src/core/timeline.cpp`, `src/core/plugin.h`, `src/plugins/chat.cpp`,
`src/plugins/files.cpp`, `src/config.h`.

**Deliberately not claimed.** There is no primary source for the folk phrase
"you just keep pressing G"; the behaviour is sourced, the quotation is not.
Citadel's message-entry save/abort/continue flow could not be verified from
primary sources. WWIV's Q-scan versus N-scan distinction could not be sourced
and is not relied on here.

---

## Phase 1 build order

A checklist, in the order somebody would write it. Every number in it is
settled. Each step has a done-when that can be checked rather than judged.

### 1. The `kMaxParts` fix, and its test, first

Nothing else touches it and it is a live permission bug.

- `kMaxParts` 4 to 8. `kComposites`' entry for `plugin:files` from count 4
  to 6.
- **Test:** open a six-field area in `CONFIG files`, Save without changing
  anything, assert all six fields survive byte for byte in `system.cfg`.
- Done when: that test fails against the current tree and passes after.

Do it first because it is independent, because the forums' own seven-part
sub-page needs it anyway, and because it is shipping wrong right now.

### 2. The four core drawing changes

Small, independent, testable on the host without a card.

- `rowBar(s, Color, title, right)`; `rowTitle` calls it with Cyan.
- `rowTitle` truncates its title with `Term::textCols` to `w - rlen - 3`.
- `Glyph::HLine2`: CP437 `0xCD`, PETSCII `0xC0`, ASCII `=`.
- `bbsu::wrap`: greedy word wrap at `rowWidth(s)`, one row per call, breaks
  an over-long word rather than overflowing.
- Done when: a 49-character title at 40 columns draws a single unbroken bar
  on ANSI, PETSCII and ASCII.

### 3. Freeze the three formats, and write the outside checker

This is the risky part and nothing after it should move a byte.

- `INDEX.TXT`, 128 bytes, four to a sector: the vote fields gone, subject
  hash 8 hex + TAB, subject text 49.
- `SUBJ.TXT`, fixed-width, one record per subject: permanent number, hash,
  display text, message count, newest message number. Board data.
- `PTRS.TXT`: per forum, `6 digits + TAB + 32 hex + TAB` = 40 bytes. Sixteen
  forums. The 32 hex characters are the **16-byte** read window.
- `#UBF1` format version in every header.
- **`tools/forum_check.py`, sharing no code with the board.** It reads the
  files independently and asserts the numbering, and it can build a topic by
  hand for the board to read back. A format tested only by the code that
  wrote it is a format that agrees with itself, and this project has paid for
  that twice.
- Done when: a generated topic of 300 messages parses as message N at offset
  N x 128 for every N, under `forum_check.py` and not under the plugin.

### 4. The plugin skeleton

- `PF_SD`, `PF_EARLY` not needed. No card, no plugin, no `FORUMS` command.
- Config parsed **in parser order**:
  `key | name | about | read | start | reply | mod`. Levels fall back read to
  plugin read, reply to plugin write, start to this forum's reply, mod to
  plugin admin and never to start.
- Sixteen forums. `g_ptrDirty` is a `uint16_t`.
- `start()` creates folders, reads headers, repairs a torn index, pre-extends
  `PTRS.TXT`. **Idempotent**, because a CONFIG save calls it with callers on
  the line. That is the scar `sd` already carries.
- Done when: a CONFIG save with a caller mid-session does not remount, stall
  or restart anything visible.

### 5. `CONFIG FORUMS` and `CONFIG FORUMS TOPICS`

- Split `cmdConfig`'s argument at the first space; route the second token
  through a per-plugin feature table. **One function builds the page**, and
  the button and the typed name both call it.
- Unknown feature says `--> No NONSENSE page in forums. Try: TOPICS` and
  stays at the prompt. Never falls through to the plugin page.
- Guard: taken on the first `CONFIG`, released only on leaving CONFIG
  entirely. Cancel on a sub-page pops one level. A dropped line at any depth
  releases guard and nesting together.
- The topics page is a **peer** of the plugin page, not a child, so
  `g_subComp` stays a single pointer.
- Done when: a caller drops the line two pages deep and a second sysop can
  open CONFIG immediately.

### 6. The forum list: the first screen testable end to end

Counts are zero at this stage and that is fine. This step proves config
reaches a screen.

- Drawn through `startPluginList`, **not** directly. 3,326 bytes at sixteen
  forums and 132 columns is larger than the whole timeline.
- The bar move redraws **two rows**, not the menu.
- The action row is reverse only while the bar is on it.
- Done when `bbs-qa` reads the bytes off a host session and confirms, at 40,
  80 and 132, on ANSI, PETSCII and plain ASCII: no row exceeds `rowWidth`,
  one reverse run per frame, every colour clears attributes before it, and
  the ASCII frame carries `===`, `-->` and `*` where the others carry colour.

### 7. Close phase 1

- `FORUMS SCAN`, sysop only, printing each forum's header figures.
- Torn-write tests: kill the host build after a segment append, after a
  partial index record, after a partial header. Restart, prove the numbering
  has not moved and the log says what was repaired.
- `optimize` report into `reports/`, per the milestone rule.
- COMMANDS.md, README.md, CHANGELOG.md and CLAUDE.md in the same change.
  **The wire format goes into the docs in parser order**, and somebody checks
  it against `readKey` rather than against this report.

**Caller visible after phase 1: a forum list and nothing else.** Said plainly
because this is the phase people skip, and because the formats frozen in
step 3 are what every later phase is standing on.
