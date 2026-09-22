<!--
 ===========================================================================
  µnleashed BBS
  Electronic freedom on a microcontroller.
 ===========================================================================

 File:         reports/chat-commands-2026-09-22.md
 Module:       Research report / chat and the information index

 Purpose:      What DDial and GTalk actually did, checked against primary
               sources; a design for the /I0../I9 information index; and a
               ranked candidate list of room commands with their main-prompt
               equivalents and their cost in bytes.

 Audience:     Rob, before deciding what goes in the next build.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v2 or later
 SPDX-License-Identifier: GPL-2.0-or-later
 ===========================================================================
-->

# Chat commands and the information index

Research report, 2026-09-22. No code written, nothing in `src/` touched, no
socket bound.

---

## 0. The number that governs everything below

Measured from the ELF on disk this morning (`.pio/build/esp32dev/firmware.elf`,
0.21.1, built 08:24 today), using the method CLAUDE.md specifies rather than
PlatformIO's percentage:

```
xtensa-esp32-elf-nm firmware.elf | grep _bss_end
3ffdaef0 A _bss_end
```

| | bytes |
|---|---|
| static DRAM used (`_bss_end - 0x3FFB0000`) | 175,856 |
| ceiling (`LENGTH(dram0_0_seg)`) | 180,736 |
| **headroom** | **4,880** |

That is down from 34,360 in `reports/optimize-2026-09-20.md`, two days ago.
`BBS_COMPOSE_MAX` in `src/config.h` records why: forums and mail each kept
their own per-session body and the link failed with `dram0_0_seg overflowed
by 8,200 bytes`.

**4,880 bytes is 406 bytes per session across the 12 slots, and that is
spending every last byte.** Every proposal below is costed against it, and
the costing is what decides the ranking as much as the feature is. Two items
on this list (`/T` channels, `/N` notes) would be perfectly reasonable on a
board with room, and are refused here on arithmetic.

An aside found while checking shortcut letters, unrelated to this brief but
live: **`FX` and `FILES` both claim `F`.** `Bbs::findCommand`
(`src/core/bbs_shell.cpp:319`) walks the tables in order and returns the
first match, the core table registers before any plugin's, so `F` at the
shell runs the effects demo and `FILES`'s documented shortcut has never
worked. COMMANDS.md lists it as `FILES` / `F`.

---

## 1. Sources

### What is a real manual or a real source listing

| Source | What it is | What it gave |
|---|---|---|
| **[Diversi-DIAL Station Owner Instructions](https://www.ddial.com/archives.php)** | **Bill Basham's own manual**, inline on ddial.com's archives page under "The Original Installation Manual" (the page toggles it with `toggleReference('ddial_install_manual')`, so it is in the HTML rather than behind a link). ~73 KB of text, references dates in 1985/86. | **The primary source.** Auto ads and their interval, the `/M` message slots, the `/I` messages, signon/signoff behaviour, the password system. |
| **[Diversi-Dial with Diversi-Modifications and Extended Modifications](https://www.ddial.com/archives.php)** | The "VZ mods" / "Ff mods" documentation, © 1989-1990 Paradise Software Systems, same page, `toggleReference('ddial_command_list_mods')` | The **complete caller and console command list** with access classes, the `/S` output format worked through with examples, `/SP`, `/CS`, and the vote-to-kick minimums. |
| [DigitalDial command list](http://www.digitaldial.us/commands.htm) | The published command reference for the live Retro-Dial board Rob pasted from (`DigitalDial-Sta#1`) | The modern DDial-family command set, quoted in full below. Useful as the *current* shape; the two above are what it descends from. |
| [OpenGTalk source, `gtalk-src-20050415.tar.gz`](https://sourceforge.net/projects/opengtalk/files/) | David Jeske and Dan Marks' GTalk, the actual C source | `Client/comparse.c` holds the complete command table with one-line descriptions, compiled into the server. **This is the primary source for section 4's GTalk column.** |
| [Ginsu Talk 1.6.9 Installation and Setup Guide](https://opengtalk.sourceforge.net/Gtalk_Installation_Guide.pdf), Scott Bachmann, May 2003 | A real manual, 18 pages | The class privilege flags (`CMD_INFO`, `CMD_PAGE`, `CMD_FB`, `CMD_BBS`, `CMD_X` …), the per-class configurable rank brackets, the sysop command list, and the `/S` "location" field. |
| [ddial.com](https://www.ddial.com/index.php?mobile=0) | The DDial community's own site | Confirms `/?` and `/I` are "familiar BBS-style screens" on current boards. |
| [ddial.com GTalk history](https://www.ddial.com/gtalk.php) | Narrative history | Provenance: GinsuTalk July 1992, Jeske and Marks; UNIX rewrite summer 1995. No command list. |
| Rob's pasted session | A live `DigitalDial-Sta#1` transcript | The `/I0../I9` index layout with its slot titles, and the `/s` output format. |

Three things worth quoting verbatim because the rest of this report rests on
them.

**DigitalDial's full command list** (the DDial family, as shipped today):

```
/?    - Command List              /Nx   - Show Note x
/A    - Toggle Ads On/Off         /Nx=  - Write Text To Note x
/B    - Toggle Beeps              /Nx-  - Remove Note x
/CP   - Console Page              /Pn   - Send Private Msg To Line n
/CR   - Toggle Carat/Carr Return  /Pn*  - Auto /P To Line n (/P* To End)
/E    - Check Email               /PS   - Send Private Msg To Station
/Ennn - Send Email To User nnn    /Q    - Quit
/H    - Set Handle                /S    - Display Station Status
/I    - Show Information Index    /SH   - Show Station History
/In   - View Information (n=0-9)  /SM   - Display Member List
/Mx   - View /M Slot x            /Tn   - Tune To Channel n
/Mx=  - Write Text To /M Slot x   /Xn   - Squelch Line n
/Mx-  - Remove /M Slot x
```

**GTalk's command table**, the relevant rows from `Client/comparse.c`:

```
{ "?",    "Display Command List (type /? <command> for more help)" }
{ "S",    "Display System List" }      { "SL",   "Display Extended System List" }
{ "P",    "Send Private Message" }     { "PAGE", "Page a Node with a Reason" }
{ "H",    "Change Handle" }            { "T",    "Change Channel" }
{ "INFO", "Display User Info" }        { "LAST", "Display User Last Info" }
{ "M",    "Show a /M (Message)" }      { "ROT",  "Rotator Menu" }
{ "X",    "Squelch Private Messages" } { "N",    "Squelch Channel Messages" }
{ "RE",   "Toggle seeing your own text in channel" }
{ "FB",   "Send Feedback Local Mail" } { "BBS",  "Enter BBS subsystem" }
{ "W",    "Set the width and height of your screen" }
{ "COLOR","Sets default color (0-15)" } { "DS",  "Double spacing output method" }
{ "IGIDLE","Toggle viewing idle users in System List" }
{ "WALL", "Broadcast a message to users" }
{ "K",    "Kill Node" }  { "RL", "Relog Node" }  { "G", "Give Time" }
{ "CI",.."Channel Invite" } { "CK", "Channel Ban" } { "CL", "Channel Lock" }
{ "CG",   "Channel Give Moderator" }   { "CA", "Channel Annonymous" }
```

**GTalk's system voice is `-->`.** From `Client/command.c:1641`:

```c
printf_ansi("--> Paging #%02d:%c%s|*r1%c\n--> Reason: %s\n", ...);
```

The marker this board already uses is not a reconstruction. It is what GTalk
printed, and it appears twice in Rob's DDial paste as well.

### What is reconstruction, and say so plainly

- **The 1984 disk's own `DIALINST` file.** The disk image is on the Internet
  Archive as
  [RIAG Crate 005: Volume 106](https://archive.org/details/riag_005_Volume_106_-_Diversi-Dial)
  and carries `DIALINST` (31 sectors). I did not extract it: it is inside a
  DOS 3.3 image. It does not matter much any more, because the Station Owner
  Instructions on ddial.com are the same body of documentation in text form
  and are what the quotations below come from. Noted so nobody assumes it was
  checked.
- **The meanings of the `/I` slots.** `/I0 Non-Member Welcome Message`,
  `/I1 News Bulletin 1 (Member Welcome Message)`, `/I9 Station Locked
  Message` come from Rob's paste of one live board. Basham's manual confirms
  three of them independently and that is worth having: **`/I1` is the
  sysop-editable owner message** ("`/O` Enter owner message in `/I1`";
  "EDITING SIGNON AND `/I1` MESSAGES: You can modify the opening message and
  the `/I1` message"), **`/I3` "Shows channel capacities"**, and **`/I9`**
  is the one that changes under lockout (`/U-992`). The rest of the mapping
  is one sysop's configuration, not a spec.
- **The bracket in `#7[T1=COTOSNET-NYC!)`.** The GTalk manual documents
  brackets as a per-class setting ("1 - Starting bracket of channel
  statement … 3 - Starting bracket of action"), and the DDial mods
  documentation shows three different opening brackets in one `/S` example
  (`#1<T1:bob)`, `#3(T1:frank)`, `#4[T2:jack)`), so the idea that the
  bracket carries rank is attested in both. Neither gives the mapping. The
  `=` before the handle in Rob's paste most likely marks a linked station,
  given `/SP` and `/CH "Remove Handle from link /SP"`; that is still a guess.
- **Whether DDial's `/I` pages were editable from the line.** `/M` slots
  were (`/Mx=` writes, `/Mx-` removes, gated by a message password) and `/I1`
  was editable from the console. `/I0=` is not in either command list, so
  section 2's editing design is mine, built on DDial's own `=` / `-`
  convention rather than copied from it.
- **GTalk has no numbered information index.** `/I` in GTalk is a fixed
  about-screen: `cmd_show_gtalk_info` prints a copyright and two URLs
  (`Client/command.c:1531`). GTalk's numbered-slot text system is the
  **rotator** (`/M`, `/ROT`), which is a different and also interesting
  thing, covered in section 4.

**One thing I got wrong before finding the manual, corrected here.** I had
read the number in `/s` (`#1(T1:?) 002`) as idle minutes, by analogy with
GTalk's `/S`. It is not. The mods documentation works the format through:

```
Normal /S with line 5 logging on:
#1<T1:bob) 040/#052*
#3(T1:frank) 003
#4[T2:jack) 032/#337$
...
Note that user number is on second line, where time is on top line.
```

**The number is minutes on the line, and the `/#nnn` after it is the member
number.** So `#7[T1=COTOSNET-NYC!) 139` in Rob's paste is a station that has
been connected 139 minutes. Reasoning by analogy between two related programs
gave the wrong answer, which is the lesson this project already has written
down twice.

**One piece of unexpected corroboration.** DDial had a vote to kick, with a
configurable minimum, switched off while staff are present:

```
/Ux989  - Subs can /K    :Votes are inactive so long as a:
        + Subs can't /K  :Co is flagged up and online    :
        A 2 vote minimum :This includes flagged links.   :
        B 3 vote minimum
```

That is what `/vk` already does here, arrived at independently. Worth knowing
the shape is authentic.

---

## 2. The information index

### What Rob asked for, restated

> "I like the /i0 having information right inside. We need to link the /i0
> and the bulletin messages, we can set them up as 0-9 just like that and it
> shows as a bulletin or a /i, use the config engine to setup and the
> forum/email editor for creating them."

One store, two doors: the same ten pages reachable as `/I0`..`/I9` in the
room and as a command at the main prompt. Set up in CONFIG, written with
`compose.h`.

### Do not call it BULLETIN

CLAUDE.md already settled this once, when the message bases were named:

> On this system "board" already means the BBS itself and "messages" would
> blur into MAIL, so forums is the word … It also settles a real collision:
> `screens/bulletin.*` is the notice screen that plays at login and is
> something else entirely, so the word was about to do three jobs at once.

`screens/bulletin.*` keeps its name and keeps its job. **The command is
`NEWS`, shortcut `N`.** `N` is free (the shortcuts in use are `W I C T M F`
plus `H ?` and `G`), `NEWS` is what DDial itself called slot 1 ("News
Bulletin 1"), and it means something to somebody who has never used a BBS.

In the room the verb is **`/I`**, because that is what Rob pasted and what
the family uses. `/news` is an alias, the way `/w` and `/who` alias `/s`.

### Where the code lives: the core, not chat

Chat is the obvious host — it already has the `/` parser, the composer
plumbing and `PF_CORE | PF_ON`. But Rob wants this at the main prompt too,
and `enabled = no` on chat would then take the board's news with it. Forums
cannot host it either: forums is `PF_SD` and a cardless board must still be
able to post a notice.

So: **`src/core/news.cpp` / `news.h`**, three entry points, and both doors
call them.

```
bool news::index(Session& s);            // the menu, or false when empty
bool news::show(Session& s, uint8_t n);  // one page, paged
bool news::begin(Session& s, uint8_t n); // open the composer on slot n
```

`NEWS` is an ordinary row in the core command table. Chat's `/i` is four
lines that call the same three functions. That also means moving the
commands later costs nothing, because the store was never a plugin's.

### Where the text lives

`<userdata>/news/0.txt` … `9.txt`. Plain text, one file per slot, written
through a temp file and a rename, the way `users.txt`, `mail.dat` and
`FILES.BBS` already are.

Internal flash, not the card, and the reasoning is the same as for the
accounts: these are the board's own words, a cardless board must have them,
and they have to survive a card failing. Ten slots capped at
`BBS_COMPOSE_MAX` (1,152 bytes) is 11.5 KB worst case on a 608 KB partition.

**They belong in the backup zip.** `ziparc::livePath` needs a route for
`news/`, which is the one piece of this that touches code outside the new
file. A sysop who restores a backup and finds the board's notices gone would
reasonably call that data loss.

### How CONFIG sets one up

The composite machinery already does exactly this shape. One row in
`kComposites` (`src/core/bbs_sysop.cpp:877`):

```c
{ "core:news", "page", "INFO PAGE", "page", CFG_PARTS(kNewsParts) },
```

```c
const CfgPart kNewsParts[] = {
    { "Title",  CK_TEXT,  24 },   // 24 so /I0 + title fits 39 columns
    { "Read",   CK_LEVEL,  6 },   // all | users | staff | co2 | co1 | sysop
    { "Login",  CK_YESNO,  4 },   // play it after login (phase 2, see below)
};
```

`CONFIG news` then draws ten buttons, `Page 0 [Welcome, visitors]`, each
opening its three fields as a page of its own, with the plain-ASCII fallback
`Page 0 [Welcome, visitors] open (y/N)?` already handled. No new CONFIG
mechanics at all.

One wrinkle: every existing composite hangs off a `[plugin:*]` section and
`compositeFor()` matches on the section name. A core section (`[news]`, keys
`page0 = Title | read | login`) is a small extension to `configPluginPage`,
or the ten keys sit as top-level keys above the first section the way
`landing` does. Either works; the section is tidier because ten keys is a
lot to put in the unsectioned block.

**Do not put the body in CONFIG.** Rob said "use the config engine to setup
and the forum/email editor for creating them", and that split is right: a
1,152 byte body in a `Form` text box would be the `FF_TEXTAREA` mistake
again.

### How the composer writes one

DDial's own convention, and it costs nothing because the parser already
supports it. From the command list: `/Mx` reads, `/Mx=` writes, `/Mx-`
removes. So:

| in the room | what it does | who |
|---|---|---|
| `/i` | the index | that page's Read level |
| `/i3` | read page 3 | page 3's Read level |
| `/i3=` | open the composer on page 3 | sysop |
| `/i3-` | clear page 3 | sysop |

**The existing verb rule handles all four with no new parsing.** From
`roomCommand` (`src/plugins/chat.cpp:1308`): the verb ends at a space *or*
the first digit. `/i3=` splits to verb `/i`, argument `3=`. `/info handle`
splits to verb `/info`, argument `handle`, because `f` is a letter. The two
cannot collide, which matters for section 4's `/info` proposal.

At the main prompt: `NEWS`, `NEWS 3`, `NEWS 3 EDIT`, `NEWS 3 CLEAR`. Slashes
are the room's vocabulary; the shell's is words.

The composer itself is `compose.h` unchanged. `compose::begin(body,
s.compose, BBS_COMPOSE_MAX, BBS_COMPOSE_ROWS)`, `/s` sends, `/a` discards,
backspace on an empty line pops the previous one. A sysop who has written a
forum post has already learned it.

### An empty slot

Three rules, all of them ones the board already applies somewhere.

- **A slot with no title and no file is not a page.** Same rule as "a folder
  with no config entry is not an area". It does not appear in the index and
  it is not counted.
- **`/i5` on a slot that does not exist, and `/i5` on a slot the caller may
  not read, answer in exactly the same words.** `There is no page 5.` This
  is the file-areas rule verbatim, and it is what stops the index being
  probed to find out which numbers are hiding something.
- **When no slot at all is set up, the index says so rather than drawing an
  empty menu.** `--> This board has no information pages yet.` This is the
  queued "a digit in an empty file area opens a file-number prompt" bug, and
  building the same bug again two weeks after finding it would be careless.

### What it looks like at 40 columns

The index, 39 columns:

```
--> Information Index
/I0  Welcome, visitors
/I1  News and what changed
/I2  House rules
/I9  Why the board is down
--> 4 pages. /I<n> reads one.
```

`/In` plus two spaces is 5 columns, the title cap is 24, total 29. That is
where the 24 in `kNewsParts` comes from, and it leaves room for an
"updated" column at 80 columns without a second layout.

A page:

```
--> [1] News and what changed
---------------------------------------
Uploads now wait for approval. If you
sent something and it has not appeared,
it has not been lost.
---------------------------------------
--> Written 21 Sep. /I lists them.
```

The body is wrapped with `bbsu::wrap` at `Bbs::rowWidth(s)`, which is the
forums rule and the right one: a page typed at 72 columns on SyncTERM has to
read on a C64.

### Paging

1,152 characters wrapped at 39 columns is about 30 lines, so a page can
overrun a C64 screen. Do not invent a pager: add `ListKind::NewsIndex` and
`ListKind::NewsPage` to the core's list machinery and get `[More]`, the abort
keys and the output backpressure for free. `Session::listIdx` is already the
row counter for every list on the board.

For the body, the row function reopens the file and walks to row *n* each
call rather than holding a handle. That is exactly the rule `files` follows
for `FILES.BBS`, and for the reason CLAUDE.md records: **a handle held across
a page break is held until somebody presses a key, which may be never.**

### Linking it to login: do the cheap half first

Rob's "link the /i0 and the bulletin messages" wants a page to play after
login. The honest recommendation is to do it in two steps.

**Phase 1, this build: one line, no new state.** The login block already
says which node, the date, the caller count and the minutes. Add:

```
3 information pages are waiting. NEWS reads them.
```

Zero risk, works whether the caller lands at the main prompt, in chat or in
forums, and it is a sentence rather than a screen somebody pages through
every single call.

**Phase 2: the `Login` field actually plays the page.** The hazard is
specific and already documented. `Bbs::completeLogin` plays
`screens/bulletin.*` through `pendingLand`, and CLAUDE.md records a live bug
where "a caller who dropped the line during the bulletin left the flag set".
Chaining a second thing onto that path is real work with a known trap in it,
not a config field. Build it deliberately, with the drop-the-line case in the
test.

When it is built: **play every at-login page the caller may read, in order,
lowest first.** No cleverness. Set page 0 to `Read = all` and page 1 to
`Read = users`, both `Login = yes`, and a visitor gets 0 while a member gets
0 and 1, which reproduces DDial's own `/I0` / `/I1` layout out of rules that
are already there.

### What it costs

| | bytes of static DRAM |
|---|---|
| `char g_title[10][25]` | 250 |
| `uint8_t g_read[10]` | 10 |
| `uint8_t g_login[10]` | 10 |
| editor guard: `Session* g_owner`, `compose::Body`, `uint8_t g_slot` | ~24 |
| body storage | **0** — the composer writes into `s.compose`, which exists |
| the reader | **0** — streamed from the file, ~200 bytes of stack |
| **total** | **~294** |

6.0% of the 4,880 bytes left.

**The editor guard is one static, not twelve**, and that is the decision
that keeps this cheap. Only the sysop writes a news page and CONFIG already
establishes that exactly one sysop edits settings at a time
(`g_cfgOwner`, `src/core/bbs_sysop.cpp:890`). A per-session editor state
would have been 12 × 21 bytes for a thing at most one person is ever doing.

Flash: the two row functions, the index, the file I/O, the CONFIG parts table
and the command handlers. Estimate 3 to 4 KB against roughly 465 KB spare.

---

## 3. The room printing its own roster: what DDial actually did

Rob's queued note asks for the room to print `/s` by itself every so often,
"see what DDial did", and CLAUDE.md records that the behaviour could not be
found documented. **It is documented, and the answer is more specific than
"every N minutes".**

### The short answer

**A DDial station did not print the local caller list on a timer.** `/S` was
on demand only, in both Basham's manual and the 1989-90 mods documentation.

**Two other things were timed, and one of them is what Rob is remembering:**

1. **Rotating messages ("auto ads"), every 5 minutes by default,
   sysop-settable 1 to 99 minutes.** Basham's Station Owner Instructions:

   > One advertising message appears every 5 minutes (or set interval with
   > /A) on channel 1 and 2.

   > **Auto Ads (K):** To turn the automatic ads to channel 1 and 2 on and
   > off, enter /A. To change the display interval, enter /Ann where 'nn' is
   > the time in minutes between auto ads (nn=01..99).

   A message slot became rotatable by starting it with a semicolon
   (`/MA=;text`), so the sysop chose which of the 36 slots went into the
   rotation. The mods documentation adds `/IRx  NEXT message to rotate is
   'x'`. Console only in both.

2. **The network-wide caller list, `/SP`, on that same timer, but only on a
   linked station.** From the mods command list, verbatim:

   > `/SP    Displays a list of callers on the system.  This command
   > only works during links, and usually is timed like rotating messages.`

   And the per-caller opt-out, which is the tell that it was pushed at people
   rather than asked for:

   > `/CS    Don't display /SP's to your line`

**So Rob's memory is right and it is specific: the list that appeared by
itself was the list of everybody on the linked network, going out on the
sysop's rotating-message interval, with a per-caller mute.** A standalone
station with seven lines had no reason to print its own seven names at
people; a network of a dozen linked stations did.

Interval: **sysop-set, 1 to 99 minutes, default 5.** Not fixed, not
event-driven.

### GTalk

Same answer, checked in the source rather than the manual.
`cmd_system_list` (`Client/command.c:618`) is reachable only from the command
table entry for `/S`; nothing calls it on a timer. GTalk's one timed printer
is the rotator, and its interval is sysop-set in minutes:

```c
printf("Enter number of minutes between rotation: ");
rfl.rfh.rotator_time = get_input_number() * 60;
```

with a `max_lines_to_rotate` cap and a per-slot `should_rotate` flag
(`Client/rotui.c:57-63`, `include/rotator.h`). A non-moderator sees it
read-only as `Rotation every %d seconds`. Identical shape to DDial's `/A`,
which is unsurprising given GTalk was written by people who used DDial.

### What was event-driven, and it is attested

Join and leave were **printed one-line notices**, not a roster redraw, and
they were filterable three ways:

> There are no signon/signoff messages on channel 4, for complete privacy.
> Handles of non-PASSWORD holders are not displayed with the signoff on
> channels 2,3,4.

> `/Ux997  - Print nonsub logon to T1 only` / `+ Print nonsub logon to ALL
> channels`

> `/CL   Don't display logons (not a filter for logINs)`
> `/CM   Previously undocumented - ignore NONsub logon/off`

That is what this board already does with `*** #2:Daytona) joined`, so
nothing to change there.

### Printed block or full-screen redraw

**Printed block.** The `/S` example in the mods documentation is a run of
lines at the cursor, and the condensed `/S#` is a two-row column layout
printed the same way. At 300 baud on an Apple //e with seven callers there
was no cursor addressing to redraw with. Nothing in either document mentions
clearing or homing.

This matters here: the board already has a home-and-redraw refresh screen
(`WHO n`, `DASH n`) and a printed list, and **the faithful answer is the
printed one**, which is also the only one that can coexist with somebody
typing.

### What I would build from this

Not a roster timer. **The rotator**, with the roster as one of the things it
can rotate, which is item 8 in the next section:

- a `rotate` interval in minutes in `[plugin:chat]`, `0` off, **default 5**,
  because that is DDial's own default and a defensible number rather than a
  picked one;
- the rotation going through the existing held-line path, so it can never
  land in the middle of somebody's sentence;
- `/b`-style per-caller mute, which is DDial's `/CS`;
- **and the check DDial did not have: only print the roster when the room has
  actually changed since the last one.** DDial was printing a whole
  network's worth of names and the list was different every time. Ten lines
  that have not moved in five minutes is a list people stop reading, which is
  what the existing queued note already says.

---

## 4. The candidate commands

Each one: the original if there is one, what it does in the room, the
main-prompt equivalent, what it costs, and how it behaves at 40 columns.

### Tier 1: build these, they are nearly free

---

#### 1. `/p3*` — stick a private conversation to one node

**Original:** DDial, `/Pn* - Auto /P To Line n (/P* To End)`. No GTalk
equivalent.

**This is the best value on the whole list and it is not close.**

**In the room:** `/p3*` starts a locked private conversation with node 3.
Every plain line you type after that goes to node 3 only, marked the same way
`/p` already marks it, until `/p*` ends it. A room command still works
normally while locked, because it starts with `/`.

Today a two-person side conversation in a busy room costs `/p 3 ` retyped in
front of every single line. That is nine keystrokes per line on a C64
keyboard, and it is the reason DDial had this. It is also why this board's
`/p` is currently a notification mechanism rather than a conversation one.

**The three things that make it safe, and they are the design:**
- The lock is shown, permanently, not remembered silently. The input line is
  armed with a visible prefix: `[>3] ` in `g_cPmark` before the cursor.
  Without it a caller types something for the room, it goes to one person,
  and they find out later. That is the only real hazard here.
- The lock ends by itself when node 3 leaves the room or drops the line, with
  a `-->` line saying so. Sessions come from a static pool; a stale target is
  the same class of bug as the stale `Session*` the transfer engine already
  had.
- `/q` clears it, `ESC` does not (ESC clears the typed line, which is the
  rule the room already follows).

**Main prompt:** **none, deliberately.** `PAGE n msg` is a one-shot notice,
not a conversation, and putting the shell into a mode where typed words go to
a person rather than to the command parser would mean every mistyped command
gets broadcast to somebody. The room is where conversation happens; say so
rather than building a worse version of it at the prompt.

**RAM:** `uint8_t g_pmTo[BBS_MAX_NODES + 2]` = **12 bytes.** 0xFF means not
locked. Nothing else.

**40 columns:** the `[>3] ` prefix is 5 columns off a 38-column input line,
which `armInput` already sizes from `s.term.cols()`. The received line is
unchanged from today's `/p`.

---

#### 2. `/sh` — replay what the room has said

**Original:** DDial, `/SH - Show Station History`.

**In the room:** replays the room's ring dimmed, exactly as a joiner is shown
it, paged. A caller who was paged away, who used `/clear`, or whose terminal
ate a line has no way back to it today; the ring is sitting right there with
48 lines in it.

**Main prompt:** none. The room's history is the room's, and a caller who is
not in it has not missed anything addressed to them.

**RAM: zero.** `g_hist`, `g_histCount` and `g_histNext` exist;
`showLine(s, line, true)` exists; `Session::listIdx` is the row counter. The
whole thing is one row function over a ring that is already allocated.

**40 columns:** identical to the eight lines a joiner already sees, which are
already correct at 40.

**One rule:** it must honour `/sq`, the way `flush()` does, or a squelch
becomes a thing you can undo by asking for the history.

---

#### 3. `/? <command>` and `HELP <command>` — say more than eleven columns allows

**Original:** GTalk, verbatim from `comparse.c`:
`"Display Command List (type /? <command> for more help)"`.

**The gap is real and measurable.** `helpLine` formats `%-11.11s` and the
description gets what is left of 39 columns, which is 28. `/vk` gets
`"vote to kick, no staff here"`. The actual rule is two thirds of everyone
but the target, rounded up, in a sixty second window, only with no staff
present and three or more callers, and it can only remove somebody from the
room. There is nowhere on this board to say that.

The same gap at the main prompt is one of Rob's nine menu items: "the usage
lines get a proper grammar rather than prose … this reads like shit". A short
column and a long explanation are two different things and the board only has
the short one.

**In the room:** `/? vk` prints the long form for `/vk`.

**Main prompt:** `HELP KICK` / `? KICK`. `?` with no argument still lists.

**RAM: zero.** `struct Command` (`src/core/bbs.h:270`) already carries
`verb keys perm flags usage help fn` and then `menu` and `rank` **with
defaults**. Appending `const char* more = nullptr;` after `rank` leaves every
existing table entry compiling untouched. This is the one place the
positional-descriptor trap does not bite, because these two fields are
already defaulted.

**Flash:** whatever the long strings weigh. Do not write one for all forty
commands: write them for the ten that actually need explaining (`VK`, `SQ`,
`BYE`, `LURK`, `DND`, `BAUD`, `UPLOAD`, `SHUTDOWN`, `MAIL`, `NEWS`) and let
`more == nullptr` fall back to `help`. Call it 1 KB.

**40 columns:** wrapped with `bbsu::wrap` at `rowWidth`, paged if it runs
over. No new layout.

---

#### 4. `/info <handle>` — who is that

**Original:** GTalk, `{ "INFO", "Display User Info" }` and
`{ "LAST", "Display User Last Info" }`, both room commands.

**In the room:** the same thing `INFO handle` prints at the shell. "Who is
that" is the most common question in any chat room and today it costs `/q`,
`INFO`, `CHAT`, which drops you out of the conversation to find out who you
are having it with.

**Main prompt:** `INFO handle`, which exists.

**RAM: zero.** It calls `cmdInfo`'s renderer with the room's `wipeInput` /
`flush` / `armInput` bracket around it, which is what every other room
command that prints does.

**40 columns:** already correct, since `INFO` draws at 40 today.

**Parsing:** `/info` and `/i0` cannot collide under the existing verb rule
(`/info` breaks at the space, `/i0` breaks at the digit). But **`/i` must
not alias `/info`**, which is the one thing to get right when both land in
the same build.

---

### Tier 2: build with the queued bell and sysop-page work

---

#### 5. `/b` — toggle beeps

**Original:** DDial, `/B - Toggle Beeps`. GTalk has the same idea as a
`/TOGGLES` menu entry.

**This is a prerequisite, not an addition.** Two items are already queued:
"a bell when somebody logs in and when somebody joins the chat room", and a
sysop page. A bell that cannot be turned off is a bell people hang up on,
and on a C64 the bell is an audible one in a room somebody else may be
sleeping in. DDial shipped the toggle in the same list as the bell.

**In the room:** `/b` toggles, and says which way it went.

**Main prompt:** `BELL` toggles the same flag. It is not `DND`: DND refuses
pages entirely, and "let it through but do not make a noise" is a different
answer that a lot of people want.

**RAM: 2 bytes.** `uint16_t g_quiet`, one bit per node, cleared on logoff the
way `g_squelch` already is. It could equally be a bit in an existing
`Session` flags byte for zero, if there is a spare one.

**40 columns:** one line of output.

---

#### 6. `/cp` — page the sysop

**Original:** DDial, `/CP - Console Page`. GTalk,
`{ "PAGE", "Page a Node with a Reason" }`, which requires the reason:
`"--> A paging reason is required"`.

**This is Rob's already-queued "a sysop page" item**, and the useful finding
is that DDial had it as a first-class room command rather than a sysop
feature bolted on. So this is corroboration, not a new idea.

**In the room:** `/cp <why>`, and the reason is required, which is GTalk's
rule and a good one: a bell with no reason attached is a bell the sysop
learns to ignore.

**Main prompt:** `SYSOP <why>`. Not `PAGE S`: the sysop node is hidden, so
`PAGE S` from an ordinary caller would confirm the hidden node exists, which
is the same information leak the file areas already refuse to give.

**The three things Rob's queued note already names:** a way to be away, a way
to decline, and something that stops one caller ringing forever. The first
two are `/a` and `DND`, which exist. The third is the only new state.

**RAM: ~24 bytes.** `uint16_t g_cpSec[12]`, one 16-bit second stamp per node,
one page per node per N minutes. Cheaper than a 32-bit millisecond stamp and
precise enough for a rate limit measured in minutes.

**40 columns:** the alert is the existing page path, which already draws at
40 with the bell, the flashing tag and the rub-out.

---

#### 7. `/page n <why>` — get somebody's attention, as distinct from talking to them

**Original:** GTalk keeps `/P` and `/PAGE` as separate commands and the
distinction is deliberate: `/P` is a private line to somebody who is in the
room with you, `/PAGE` is getting the attention of somebody who is not.

**In the room:** `/page 4 are you there` reaches node 4 wherever they are on
the board, through the existing page path with the bell and the tag. Today
`/p 4` only works on somebody who is **in the room** (`inRoom(id)` returns
null otherwise) and the room says `/p n text, to somebody in the room.` So
there is currently no way to reach a friend who is sitting in FILES without
leaving the room to do it.

**Main prompt:** `PAGE n message`, which exists. This is genuinely just
bringing the shell command into the room.

**RAM: zero.** It calls the existing page path. The same `DND` and `/b`
rules apply to it.

**40 columns:** unchanged from `PAGE` today.

---

### Tier 3: worth having, but later and with a card

---

#### 8. `/M` slots and the rotator

**Original:** DDial, `/Mx` read, `/Mx=` write, `/Mx-` remove, 36 slots, a
slot starting with `;` joins the rotation, interval `/Ann` in minutes,
default 5. GTalk, `{ "M", "Show a /M (Message)" }` and
`{ "ROT", "Rotator Menu" }`, interval sysop-set in minutes, per-slot
`should_rotate`, `max_lines_to_rotate` cap.

**This is the queued "chat shows the room list by itself every so often" item,
and section 3 is the evidence that it is the right shape for it.** DDial's
own periodic printer was this, not the roster; the roster only went out by
itself on a linked network, and it went out on *this* timer.

**Build it as phase 2 of the information index, not as its own thing.** It is
the same store, the same reader, the same composer and the same CONFIG
composite, with one extra field (`Rotate: yes/no`) and one interval setting.
The `-->` marker and the held-line path already exist so it can never land in
the middle of somebody typing.

**Main prompt:** none. A rotating notice is a room feature; at the shell it
would interrupt a command line.

**RAM:** another ~294 for a second bank, plus 4 bytes for the timer. Only
worth it once the `/I` bank exists and has proved itself.

**Do not make the slots caller-writable**, which is what DDial's `/Mx=` was.
On a ten-line board a public text slot anybody can rewrite is a moderation
surface with no moderation attached, and the same paragraph in the feedback
plugin's queued note applies: a caller who can write is a caller who can fill
a card.

---

#### 9. `/w 80 24` — tell the board how wide you actually are

**Original:** GTalk, `{ "W", "Set the width and height of your screen" }`.

**In the room:** overrides the detected width for this call.

**Main prompt:** extend the existing `TERM` to `TERM 80 24`.

**Why it is worth anything at all:** `Bbs::rowWidth` gives an unknown width
40 columns, deliberately and correctly. A caller on a 132-column terminal
whose client never negotiated NAWS is stuck at 40 with no way to say so, and
"my screen is mostly black" is the exact complaint the wide-terminal rework
exists to fix.

**RAM: zero.** It writes fields on `s.term` that already exist.

**The guard that matters:** the zero case. CLAUDE.md records that a NAWS
negotiation carrying zero underflows to 255 and paints 255 reverse-video
spaces down the screen. A typed width is another way in to the same bug.

---

### Two more, noted rather than proposed

- **`/fb`, feedback.** GTalk, `{ "FB", "Send Feedback Local Mail" }`. Rob's
  feedback plugin is already queued and card-only. When it is built, `/fb`
  should be the room's door to it, designed at the same time rather than
  bolted on. Naming it here so the two are not designed separately.
- **`/color`.** GTalk, `{ "COLOR", "Sets default color (0-15)" }`. This
  overlaps the queued `CONFIG PALETTE` and the themes item from the shell
  rework. It is the caller's side of the same feature. Decide the sysop side
  first or it gets built twice.

---

### The ranked list, with the arithmetic

| # | Command | Room | Main prompt | DRAM | Tier |
|---|---|---|---|---|---|
| 1 | Sticky private | `/p3*`, `/p*` | none, on purpose | **12 B** | 1 |
| 2 | Room history | `/sh` | none | **0** | 1 |
| 3 | Long help | `/? vk` | `HELP KICK` | **0** | 1 |
| 4 | Who is that | `/info h` | `INFO h` | **0** | 1 |
| 5 | Bell toggle | `/b` | `BELL` | **2 B** | 2 |
| 6 | Page the sysop | `/cp why` | `SYSOP why` | **~24 B** | 2 |
| 7 | Page a node | `/page n why` | `PAGE n msg` | **0** | 2 |
| 8 | Rotator slots | `/m`, `/m3` | none | ~298 B | 3 |
| 9 | Width override | `/w 80 24` | `TERM 80 24` | **0** | 3 |

**Tier 1 is 12 bytes of DRAM for four commands.** Tier 1 and 2 together are
38. The information index is ~294. All of it together is ~332 bytes, 6.8% of
the 4,880 that is left, and about 6 to 8 KB of flash.

---

## 5. What I would not build, and why

**`/T` — channels.** The famous one. DDial had four, GTalk had unlimited
channels with a full moderation set (`/CI` invite, `/CK` ban, `/CL` lock,
`/CG` give moderator, `/CA` anonymous, `/ADDCHANNEL`). It is the single
biggest feature in both programs and it is wrong here.

DDial ran seven lines and gave them four channels, and that worked because a
DDial station was linked to other stations: `/PS - Send Private Msg To
Station` and the `=` in Rob's own paste (`#7[T1=COTOSNET-NYC!)`) are a
linked board. The channels had a network behind them. **This board has ten
lines and no link.** Three callers split across four channels is four empty
rooms, and the thing that makes a small board worth calling is that whoever
is on is together. CHAT.md already says it: "one room, the way DDial and
Gtalk did it".

Cost if built anyway: a channel byte per session, a per-channel history ring
(the current one is `history` × 65 bytes, 3 KB at the default, so four
channels is 12 KB against 4,880 bytes of headroom), and the moderation set,
which is six more commands and a per-channel moderator table. It does not
fit, and it would not be good if it did.

**The thing channels were mostly used for is a side conversation, and `/p3*`
gives that for 12 bytes.** That is the counter-offer.

Revisit it when board linking lands, because then the argument changes: a
channel that spans two boards has people in it.

**`/H` — change your handle.** DDial's handle was a label for one call.
Here it is an identity: `UserRec::id`, assigned once and never reused, with
`onRename` rewriting `mail.dat` and the room ban list behind it. Changing it
mid-call would either skip that hook, which is the bug 0.19.0 was built to
fix, or run it mid-conversation. And a room where somebody can become
`Daytona` while `Daytona` is standing there is a room with an impersonation
problem. `USER EDIT` renames, staff-gated, with the hook. That is the right
and only door.

**`/N` — personal notes.** Ten private scratch slots per caller. Cheap in
RAM if stored per account, but it is a file per user on a partition that
holds the accounts, and its whole job is "leave yourself a note", which mail
already does. On a ten-line board this is a feature nobody asks for twice.

**`/SM` — public member list.** DDial listed everybody with an account.
Handles are already public in WHO and LAST, so it leaks nothing new, but it
is a paged list of up to 250 rows whose only use is finding somebody to
`/email`, and `INFO handle` answers that when you already have a name. The
staff version is `USERS` and it exists.

**`/CR`, `/DS`, `/IGIDLE`, `/RE`.** Four toggles, all of them answers to
1980s terminal problems this board does not have.
`/CR` (carat versus carriage return) and `/DS` (double spacing) are for
hardcopy and for terminals that ran lines together; this board's room line
is coloured in four parts so the boundaries are visible without spacing.
`/IGIDLE` hides idle users from a list; the list here is at most ten rows.
`/RE` stops you seeing your own text; `say()` echoes your own line
deliberately so that `F_STAY` can rewrite it in its finished form, and
turning that off would leave a blank.

If two or three toggles ever do land, GTalk's answer is the right shape:
one `/toggles` menu rather than N one-letter commands. Note it, do not build
it for two flags.

**`/V` — validate a guest.** A sysop upgrading a guest to an account in
place. Real work (a form, a password, a handle collision check against a
handle somebody is currently using) for something `USER ADD` already does in
thirty seconds, and the guest has to type the password anyway.

**`/A` — toggle ads.** No ads. Listed only so the omission is deliberate
rather than an oversight; the nearest real equivalent is switching the
rotator off, which belongs to the rotator.

**`/Ennn` — mail by user number.** DDial addressed mail by member number and
this board now has `UserRec::id`, so it would work. But the board's whole
vocabulary is handles, and asking somebody their number so you can write to
them is worse than asking their name. Skip.

**`/PS` — message the linked station.** This is federation, and it is
already on the roadmap as exactly that. Not a chat command until there is a
link for it to go down.

---

## 6. If only three things get built

1. **The information index.** It is what was asked for, it costs 294 bytes,
   and it reuses the composite machinery, `compose.h`, `bbsu::wrap` and the
   list pager without extending any of them.
2. **`/p3*`, sticky private.** Twelve bytes for the feature that makes a
   chat room a chat room rather than a broadcast channel.
3. **`/sh` and `/info h`.** Zero bytes between them, both of them things a
   caller currently has to leave the room to get.

`/? <command>` is the fourth and it is also free, but it is really part of
the queued menu and help rework rather than part of this, and doing it there
means writing the long help once against the new menu structure instead of
twice.

And one thing to take off the list rather than add to it: **the queued
"chat shows the room list by itself every so often" item should not be built
as a roster timer.** Section 3 has the documentation. What DDial printed by
itself was a rotating sysop message on a 1-to-99 minute interval, default 5;
the roster only went out on a timer on a *linked network*, on that same
interval, with a per-caller mute. Build the rotator (item 8) with the roster
as one of the things it can rotate, and the queued item is satisfied by the
thing DDial actually had.
