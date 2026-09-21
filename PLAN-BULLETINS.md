<!--
 ===========================================================================
  µnleashed BBS
 ===========================================================================

 File:         PLAN-BULLETINS.md
 Purpose:      Two pieces of work that turned out to be one. A stable user
               identity, which the board needs and does not have, and the
               FORUMS subsystem, which is the first thing that would have
               been built wrong without it.

 Note:         Rename this file to PLAN-FORUMS.md if the name in part 2 is
               accepted. It is called PLAN-BULLETINS.md because that is what
               the work was called before it had a better name.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v2 or later
 SPDX-License-Identifier: GPL-2.0-or-later
 ===========================================================================
-->

# Identity, and the forums

## Read this first: what changed and why

The first version of this plan designed a message base keyed on caller
handles. Rob read it and found a hole that is bigger than message bases:

> We're storing by caller Handle, what if the handle changes? Seems we
> should have support for that but looks like we dont.

He is right, and it is not hypothetical. It is a live bug in shipped code,
and it costs somebody their mail:

- `USER EDIT` can rename a handle. `Bbs::formSave` writes the new name into
  `users.txt` and patches `o->user` on any live session, and stops there.
  Nothing else on the board is told.
- Chat mail stores `to` and `from` as handle strings in `MailRec`, and
  `mailSlotFor()` matches by handle. **Rename somebody and their waiting
  mail is orphaned**, addressed to a person who no longer exists.
- The chat room ban list is sixteen handle strings. **Renaming is an escape
  from a room ban**, and it works in both directions.
- `UPLOADS.BBS` records the uploader's handle, so a rename mis-attributes a
  pending upload to the staff deciding whether to approve it.
- **`USER DEL` frees the handle for re-registration.** The suite asserts it:
  "deleted handle is new again". So delete Alice, let Bob register the
  handle "Alice", and Bob is handed Alice's undelivered mail and inherits
  her room ban.

That last one is the serious one. It is the same shape as the mail bug that
was already found and fixed once: **the board losing somebody's data and
reporting success.** The fix there was "refuse, never destroy", and the same
instinct applies here one layer down.

So this plan is now in two parts.

- **Part 1 is identity**, and it is core work that has nothing to do with
  forums. A stable numeric user id, and accounts that are retired rather
  than removed.
- **Part 2 is the forums subsystem**, built on that id from the first line
  rather than retrofitted onto it later.

Part 1 has to land first. Not because forums need it most, but because every
day it does not land is another day somebody can lose their mail to a
rename, and because retrofitting an identity into a format people already
have on their cards is the expensive way to do this.

**The identity change also made the forums design simpler**, which is worth
saying up front because it is unusual. Three separate mechanisms in the
first version existed only to cope with handles being mutable and reusable:
a handle-sorted pointer file needing a binary search, a slot number that had
to be revalidated on every write in case somebody else registered, and a
sweep that deleted a votes file when a slot was reused. **All three are
gone.** An immutable id turns the pointer lookup into a single seek and a
single read, and makes a votes file impossible to inherit. The plan got
shorter.

**What is unchanged**, and is still the strongest part: a message number is
a slot in a fixed-width index and it never moves. Nothing renumbers, ever.
Part 2 opens with it.

**The caller-facing flow is provisional.** A separate agent is designing the
TTY experience against Citadel and the boards that got this right. The
command and key surface in part 2 is a starting point for that work and is
marked as such. The value here is the data model, the identity change, the
storage, the RAM and the phases.

---

# Part 1: identity

## What a user id is, and what it is not

**A user id is a number the board assigns to an account once, and never
gives to anybody else.** That is the whole definition and every property
below falls out of it.

It is not a login name. It is not shown to callers as a rule. It is not a
rank, a slot in a list, or a position in a file. It is the answer to "which
person is this", asked by a subsystem that has no business knowing or caring
what that person is currently called.

The handle stays exactly what it is today: what a caller types to log in,
what other callers see, and something they are allowed to change. Nobody
wants to keep the same name forever, and the board should not make them.

## The width: 32 bits

```c
uint32_t id = 0;     // 0 = never assigned (a record written before ids)
```

**Not 16 bits.** 65,535 sounds like plenty, and that is exactly the
reasoning that produced `max_users = 100` from a leftover partition size and
`CONFIG_LWIP_MAX_SOCKETS = 24` from a comment that knew the ceiling was 16.
A limit you have to think about is a limit somebody eventually hits, and
this one cannot be raised after the fact without renumbering, which is the
one thing an id must never do.

**Ids are consumed by every account ever created, not by live accounts.**
That is the point of not reusing them, and it is why the arithmetic has to
be done against the lifetime of a board rather than against `max_users`.

- 32 bits is 4,294,967,295. At one new account an hour for a century that is
  876,000. It cannot be exhausted by anything short of deliberate abuse, and
  `max_users` plus the registration path already bound that.
- **It does not wrap and it must not.** If the next id ever reached
  0xFFFFFFFF the board refuses to create accounts and says so, the same way
  it refuses when `max_users` is reached. A wrapped id is two people sharing
  an identity, which is worse than a board that cannot take a new member.
- `id = 0` is reserved to mean "not assigned", so the first real id is 1.

**Cost:** four bytes in `UserRec`. The optimize report measures 29
`UserRec` instances in the image at 456 bytes each, so that is **116 bytes
of static DRAM**, or 232 if alignment rounds the struct up by eight rather
than four. Measure it with `ptype /o struct UserRec` rather than guessing,
which is how `sizeof(Session)` was found to be 5,716 and not 6,000.

**On the card an id is cheaper than a handle.** Eight hex digits against
twenty characters. Every fixed-width record in part 2 got smaller, not
bigger, by keying on the id.

## Where it lives

In `UserRec`, and in `users.txt` as one more key:

```
[Daytona]
name = Rob
...
id = 42
level = user
created = 1758412800
```

The file format was built for this. Adding a field is one `UserRec` member,
one branch in `assign()` and one line in `writeRecord()`, which is what
`USERS.md` has always said. No block layout moves and no existing key
changes.

**One hazard worth naming, because nothing in the firmware can stop it.**
`assign()` warns about an unknown key and drops it on the next write, so an
**older build run against a `users.txt` that has ids would silently strip
every one of them**, and the migration below would then hand the same ids
out to different people. Running older firmware against newer accounts is
already discouraged; this makes it destructive. Put a version marker in the
file's comment header so at least the damage is visible afterwards, and say
it plainly in `USERS.md`.

## Assignment, and the rule that matters

The next id is **derived, not stored**: it is the highest id in `users.txt`
plus one. `users::add()` already reads the whole file to count accounts and
to check the handle is free, so finding the maximum in the same pass is
free.

Deriving rather than storing means there is no counter to keep in step with
the file, no counter to lose in a restore, and no counter that can disagree
with reality. It is correct by construction, **and it is correct only
because accounts are never removed.** Remove a block and its id becomes
available again the next time the maximum is computed.

So the two instructions reinforce each other: not removing users is what
makes a derived counter safe, and a derived counter is what makes not
removing users pay for itself.

**The invariant that must not be broken:**

> An id is never used anywhere until the account record carrying it has been
> written to `users.txt` successfully.

A board that hands out an id it did not persist will hand out the same id
again after a reboot, and by then it is on a card in a forum post. The
existing write path already gives this for free: `rewrite()` builds a temp
file, checks `ferror`, and only then renames, returning `IoError` on any
failure. Nothing may act on a new id until that returns `Ok`.

## Migration: what a board does on first boot with this firmware

- Read `users.txt` once. Note the highest `id` present, which on a board
  upgrading for the first time is zero.
- Walk the records **in file order** and give every record with `id = 0` the
  next number, starting at one.
- Write the file once, through the existing temp-file-and-rename path.
- Log it: `users: assigned ids to 37 accounts, next id 38`.

It is deterministic because file order is stable and the pass is single
threaded, and it happens once because afterwards no record has `id = 0`.
Run it again and it is a no-op that costs one file read.

**If the rewrite fails, nothing proceeds.** The forums plugin refuses to
start, and says why, rather than running with ids that exist only in RAM.
That is the posture `PF_SD` already takes about a missing card: refused with
a reason beats working in a way nobody asked for.

Where it runs: `users::begin()`, called from `main()` before the listener
opens, while the board is quiet. Not from a plugin, because identity belongs
to the core and because a plugin's `start()` also runs on every CONFIG save
with callers on the line. That is the scar the `sd` plugin already carries
from remounting the card on every config save.

**A restored backup carries ids with the accounts**, because they are in
`users.txt` and `users.txt` is in the zip. A board restored from a backup
older than some of its accounts recomputes the next id from what it has,
which is the file it is now running on. It cannot reissue a live id because
every live id is in that file.

## Accounts are retired, not removed

Rob: "We should not remove users."

Taken as a design instruction, and it is the right one: every orphaning bug
above is caused by a handle outliving its owner and being handed to somebody
else.

**A retired account:**

- keeps its id, forever
- keeps its handle, **reserved**, so registration refuses it and nobody ever
  inherits anything
- cannot log in, and is told it is retired rather than that the password is
  wrong, because a locked-out person needs to know which problem they have
- still renders its name anywhere its id appears, so nothing is orphaned
- does not count toward `max_users`, because that cap is about live callers
- is marked in the user manager and left out of `WHO`, `LAST` and the
  ordinary lists

The flag is a `uint8_t flags` field on `UserRec` with `UF_RETIRED = 1`.
`UserRec` already ends with `land`, `locked` and `level`, three single-byte
members that almost certainly sit in a four-byte slot, so **the flag is very
likely free in existing padding**. Measure it; if it is not free it costs
another 116 bytes and is still worth it.

`users::count()` returning `uint8_t` becomes "live accounts", which is what
`add()`'s `max_users` check and the user manager both want, and a second
call reports total blocks for a sysop who wants to know what the partition
is holding.

### What the three commands should do

| Command | Level | What it does |
|---|---|---|
| `USER DEL <handle>` | `PERM_USERS` | **Retires.** The confirm says so in words. |
| `USER PURGE <handle>` | `CF_SYSOP` | Blanks what the board knows about a person, keeps the identity. |
| `USER FREE <handle>` | `CF_SYSOP` | Genuinely removes the block. Refused unless `calls == 0`. |

**`USER DEL` keeps the verb and changes the confirm**, because a sysop's
fingers already know it and because silently doing something different from
what a verb says is worse than either behaviour on its own:

```
Retire Newbie? They cannot log in, the handle stays reserved, and
nothing they left behind is orphaned. (y/N)
```

**`USER PURGE` is for a sysop who genuinely wants somebody gone.** It blanks
name, email, address, phone, profile and the password hash, and keeps the
id, the handle and the retired flag. The person is gone; the fact that user
42 existed is not. That is the honest answer, and it is the same argument
the mail history already settled: destroying a record to make a problem go
away is what caused the problem in the first place. It is logged, because a
moderation action nobody can audit is indistinguishable from a bug.

The handle stays reserved even after a purge. Releasing it is exactly the
inheritance bug, and "somebody else might want that name" is not worth
handing them somebody else's mail.

**`USER FREE` exists for one real case**: a typo registration or a mistaken
`USER ADD`, five minutes old, where nobody has done anything as that person.
`calls == 0` is a cheap, checkable proxy for exactly that, and anything else
is refused with a message pointing at `PURGE`. A board that offers "erase
all trace" as a button is a board that will erase somebody's trace by
accident.

For the genuinely nuclear case the sysop has the card, the backup zip and a
text editor, which is documented and is the right place for an operation
that cannot be undone.

### The test that has to change

`tools/testclient.py` asserts "deleted handle is new again". That test
encodes the old behaviour and must now assert the opposite: the handle is
refused, and the account is listed as retired. Same shape as `publicNodes()`
advertising eleven nodes, where the board was right and the test had written
down the old default.

## Rendering a name when you only stored an id

This is the part that decides whether the identity change is worth having or
is a new performance bug, and it is the same trap as `findDesc` reopening
`FILES.BBS` once per listing row, which is a suspect in the measured 126 ms
loop stall.

`users::lookup()` opens `users.txt` and scans blocks from the top until the
handle matches. A twenty row listing that resolved a name per row would be
twenty full-file scans with twenty `fopen` calls. That is the stall,
rebuilt, on the internal partition rather than on the card.

Three parts to the answer, and the first one does most of the work.

### The id is authoritative, the name beside it is a cached render

Wherever a subsystem stores an id, it stores **the handle as it was at that
moment, alongside it**, and treats that copy as a display cache that is
allowed to go stale.

This looks like the duplication this project normally refuses, so the
argument matters:

- The tie between a message and a person is the id, and it never breaks. The
  handle beside it is never used to identify anybody, only to draw them.
- **A listing shows the cached render and resolves nothing.** Zero file
  reads per row, by construction rather than by caching cleverness.
- A post from three years ago showing the name its author used then is
  arguably more correct than showing today's name. It is a record of
  something somebody said, and that is exactly the argument for leaving the
  caller log alone below.
- When a live name genuinely matters, which is opening a single message or
  looking at a profile, the board resolves that **one** id. One scan, once,
  for one row the caller asked about.

So the rule, stated once and applied everywhere: **the id is the key, the
stored handle is a picture of the name, and pictures are allowed to be old.**

### A small resolver cache for the live cases

Board wide, not per session: sixteen entries of id plus handle, 16 x (4 + 21)
= 400 bytes, call it **416** with a use counter each. A miss does one
`users::lookup` and fills the oldest slot. Twelve callers on the line plus a
few recently opened profiles fit comfortably.

Invalidation is a board-wide generation counter, one `uint32_t`, bumped by
any rename, retire or purge. One compare on the way in. Without it a renamed
caller keeps their old name on other people's screens until a reboot, which
is precisely the class of bug this whole exercise exists to remove.

### Use `users::range` when a whole run is needed

`users::range(start, n, fn, ctx)` already reads a contiguous run of accounts
in **one pass**, and the user manager already uses it for exactly this
reason. Anything that really does need live names for a run of accounts uses
that and not a lookup per row. It is the existing right answer and it should
be cited rather than reinvented.

## When an id has no account any more

Under "never remove" it should not happen, and it will anyway:

- a card with forum posts on it is moved to a different board
- a backup restore is older than the posts on the card
- somebody hand-edits `users.txt`
- a purge blanked the record, which is fine, because the id and handle stay

The rendering rule, in order:

- **If the record carries a cached handle, show it.** This covers almost
  everything, and it is why caching the render is not merely an optimisation:
  it is the resilience answer. A post keeps its author's name even on a board
  that has never heard of them.
- **Otherwise show `user 0042`.** Never blank, never the word "unknown".
  `user 0042` is something a sysop can go and search for; "unknown" is not.
- **A retired account renders its handle normally.** Do not hide it. The
  person said the thing, and a conversation attributed to nobody reads as
  the board being broken rather than as somebody having left.

## What else should move to ids, and in what order

This plan does not design these subsystems. It says which ones are affected
so the work can be sequenced, ordered by how bad the bug is today.

| | Subsystem | Today | Priority |
|---|---|---|---|
| 1 | **Chat mail**, `p/chat/mail.dat` | `MailRec.to` and `.from` are handles; `mailSlotFor()` matches by handle. A rename orphans mail, a re-registered handle inherits it. | **Live data loss. First.** |
| 2 | **Chat room bans**, `p/chat/bans.txt` | Sixteen handle strings. A rename escapes a ban; a re-registered handle inherits one. | **Live, and it cuts both ways. Cheapest fix on the list.** |
| 3 | **Uploads attribution**, `UPLOADS.BBS` | The uploader's handle. A rename mis-attributes a pending upload. | Not data loss, but staff approve or reject on the strength of that name. |
| 4 | **Forums** | Does not exist yet. | Built on ids from the first line, which is why this is happening now. |
| 5 | **Caller log**, `CallRec.user` | A handle. | **Leave it alone. It is not a bug.** |

**The caller log is correct as it stands and should not be "fixed".** It is a
historical security record and it should say what the handle was at the time
of the call, not what it is now. Rewriting history to match a rename is the
wrong behaviour, not the right one. Adding the id *beside* the handle would
be a genuine improvement, because it would let a sysop follow one person
across renames, but that is an enhancement to a working thing rather than a
repair to a broken one. Do it last or not at all, and if it is done, add
rather than replace.

**`MailRec` is the awkward one**, and deliberately so: it carries
`static_assert(sizeof(MailRec) == 564)` and
`static_assert(offsetof(MailRec, at) == 44)` precisely so that changing it
is a decision rather than an accident. Whoever does it gets to choose
between widening the record with a migration pass, or the trick that worked
last time, which was putting new fields in padding the record was already
wasting. Either way the asserts change in the same commit and the commit
says why.

Not affected: IP bans (keyed by address), the announce token, staff
passwords, screens, the file areas themselves.

## What identity costs, in bytes

| What | Bytes |
|---|---:|
| `UserRec::id`, 4 bytes x 29 measured instances | 116 |
| `UserRec::flags` for `UF_RETIRED`, if existing padding takes it | 0 |
| Resolver cache, 16 entries of id and handle | 416 |
| Generation counter | 4 |
| **Total** | **536** |

Worst case, if the struct rounds up by eight rather than four and the flags
byte forces another word, **768**. Measure rather than assume; that is the
project's own rule and the difference here is one `ptype /o`.

Flash: a few hundred bytes for the migration pass, the resolver and the two
new commands. Not a constraint.

---
---

# Part 2: FORUMS

## The name

`BULLETIN` was never the name, it was what the work kept being called. Rob:
"the menu option and name of the message boards doesnt have to be bulletin i
just keep referencing it that way. Use something intuitive to the rest of
the world."

**The verb is `FORUMS`.**

- It is the word somebody under fifty already knows, and this board has to
  explain itself to people who have never used one.
- It does not collide with "board", which on this system means the BBS
  itself, and every period alternative does collide with something here:
  "conferences" and "subs" need explaining, "areas" is taken by the file
  areas, "rooms" is taken by chat, and "messages" blurs straight into MAIL.
- **It dissolves the screen collision** that was the first version's leading
  concern. `Bbs::completeLogin` plays `screens/bulletin.*` at login as
  whatever the board has to say today, which is documented in `SCREENS.md`
  and `COMMANDS.md`. That screen keeps its name and its meaning, and nothing
  else is called bulletin any more.

**No single-letter shortcut.** `F` is already claimed twice, by `FILES` and
by `FX`, which is a small pre-existing bug worth fixing in passing since
only one of them can win. `CHAT` and `MAIL` have no shortcut either, so
`FORUMS` having none is consistent with the two commands beside it on the
main menu. `BULLETIN` is kept as a `CF_HIDDEN` alias so that fingers, old
notes and this file's own name all still work.

### The rename migration

Four places say "bulletin" today and all four are cheap:

- `users.cpp`: `kLandKeys[3]` becomes `"forums"`, `kLandVerbs[3]` becomes
  `"FORUMS"`, and `LAND_BULLETIN` is renamed `LAND_FORUMS`. **The enum value
  stays 3**, so no account file changes meaning.
- `bbs_users.cpp`: `kLandNames[]` and `kLandPick` get "Forums" for the
  `Start` field's cycle picker.
- `sysconfig.cpp`: the `landing` key's rejection message stops saying
  "bulletin".
- **`landFromKey()` accepts `"bulletin"` as a read-only alias for forums.**

That last one is the one that matters. Rob is running a build where an
account may already have `land = bulletin` written into `users.txt`, and
`landFromKey` falls back to `LAND_DEFAULT` on anything it does not
recognise. So without the alias, his preference is silently reset to the
board default and nothing tells him. It is three lines, it is never written
back, and the next time that account is saved the file says `forums`.

A soft failure that silently discards somebody's setting is still a failure.
This project has already written down that a landing the board cannot do
should fall back quietly; a landing the board *can* do, spelled the old way,
should not.

## The invariant: a message number is a slot, and it never moves

`INDEX.TXT` is a fixed-width file of 128 byte records. **Message number N
lives at byte offset N x 128, forever.** Record 0 is the header.

That one decision answers the question that is the most likely way to get
this wrong:

- **Deleting a message does not renumber anything.** The record stays and
  its flag says `X`. The bytes in the body file stay until the segment they
  are in rolls off.
- **Rolling old messages off does not renumber anything.** Bodies live in
  segment files that are deleted whole. The index records stay as history;
  the header says which segment is the oldest surviving one, and a record
  pointing below it is a message whose text is gone.
- **The index is never packed, compacted or rewritten.** There is no
  operation anywhere in this design that shifts a record.

So a pointer that says "I have read up to 412" means the same thing next
week and next year. A pointer is only worth having if that is true, and the
usual way it stops being true is a well-meaning compaction pass.

The cost is that the index grows by 128 bytes per post and never shrinks. At
twenty posts a day that is 0.9 MB a year per topic on a card with gigabytes
on it. That is the right thing to spend, and it is the whole reason the card
is required.

**The same idea, one layer up, is what part 1 is.** A user id that never
moves is to a person what a slot that never moves is to a message. Having
built the second and not the first is what made the hole visible.

## What lives where

Everything is on the card. This is `PF_SD`: no card, no plugin, and `FORUMS`
is not a command at all. Same posture as `files`, and it closes a loop that
was already built: `users::landVerb` names the command and
`Bbs::landAfterLogin` falls back to the main prompt when it does not exist,
so a board with the card pulled puts a `Start = Forums` caller at the main
prompt without a word of complaint. The fallback written for "before the
plugin exists" turns out to cover "the card came out" for free.

```
<sd>/p/forums/
    PTRS.TXT              read pointers, one fixed record per user id
    v/0000002A.TXT        one account's votes, by id
    .tmp/0000002A.TXT     one account's message being typed right now
    general/
        INDEX.TXT         the header, then one 128 byte record per message
        M0001.TXT         body segment 1, up to 64 KB
        M0002.TXT         body segment 2
        MODLOG.TXT        who deleted what, append only
    c64/
        ...
```

Plain text throughout, tab separated, fixed width where the board needs to
seek. A sysop can pull the card, open `general/M0004.TXT` in Notepad and
read the conversation. Same argument `FILES.BBS` and the caller log mirror
already make, and worth a little parse cost: a listing parses at most a page
of short records out of one buffer, which is microseconds.

Every board-generated filename is 8.3 safe on purpose. `0000002A.TXT` is
eight and three, `M0001.TXT` is five and three. `CONFIG_FATFS_LFN_HEAP` is
on now, but the board has already lost an evening to a nine character folder
name being an *invalid name* under the FATFS default, and there is no reason
for the board's own filenames to depend on that setting being right. Topic
keys are validated to eight lowercase letters and digits for the same
reason.

**An id in a filename is another thing the identity change bought.** A
handle can contain spaces and dots, which FAT tolerates awkwardly, and can
change, which would strand the file. `0000002A.TXT` cannot do either.

## The data model

### The topic index: `INDEX.TXT`

Record 0 is the header, exactly 128 bytes, tab separated `key=value`, padded
with spaces and ending CR LF. Nothing depends on where a field sits in the
header, only on the whole line being 128 bytes, so a key can be added later
without moving a single message.

```
#UBF1<TAB>topic=general<TAB>count=000218<TAB>seg=0004<TAB>first=0002<TAB>firstmsg=000121<TAB>kb=000512<TAB><spaces to 126><CR><LF>
```

- `#UBF1` is the format version, in every file from the first release. When
  a record format changes the plugin migrates or refuses with a reason.
  Retrofitting this once people have real boards is expensive; doing it now
  is free.
- `count` is the highest message number ever issued in this topic. It is
  also the file's own length check: `filesize == (count + 1) * 128`.
- `seg` is the segment being appended to, `first` is the oldest surviving
  segment, `firstmsg` is the lowest message number whose body still exists.

Then one 128 byte record per message, in message order, no gaps:

| Field | Bytes | Notes |
|---|---:|---|
| message number | 6 | decimal, redundant with the slot, kept for humans and as a self-check |
| TAB | 1 | |
| flags | 2 | first: `.` live, `X` deleted. second: `.` normal, `!` pinned |
| TAB | 1 | |
| **author id** | **8** | **hex. the identity. never changes, never reused** |
| TAB | 1 | |
| author handle | 20 | space padded. **a cached render, allowed to be stale** |
| TAB | 1 | |
| epoch | 10 | when it was posted |
| TAB | 1 | |
| segment | 4 | which `M####.TXT` the body is in |
| TAB | 1 | |
| offset | 6 | byte offset of the body text within that segment |
| TAB | 1 | |
| length | 4 | body bytes |
| TAB | 1 | |
| up | 3 | vote tally, saturates at 999 |
| TAB | 1 | |
| down | 3 | vote tally, saturates at 999 |
| TAB | 1 | |
| subject | 50 | space padded |
| CR LF | 2 | |
| **total** | **128** | four records to a 512 byte sector |

The id costs nine bytes of the record and the subject paid for it, down from
59 characters to 50. That is the right trade: 50 characters is a longer
subject line than most boards of the era offered, and the alternative is a
160 byte record, which gives up the four-to-a-sector alignment and grows the
index by 25%. The handle stays at the full 20 because truncating an author's
name in the one place a listing reads it is the `%9.9s` mistake that printed
this board's own address as `192.168.0`.

#### Worked example: message 47

At byte offset 47 x 128 = 6,016, with tabs shown as `\t`:

```
000047\t..\t0000002A\tDaytona             \t1774101234\t0004\t012838\t0187\t003\t000\tAnybody got a working 1571?                       \r\n
```

Read it: message 47, live and not pinned, by user id 0x2A (42), who was
called Daytona when they posted it, at epoch 1774101234; the body is 187
bytes at offset 12,838 in `M0004.TXT`; three up votes and none down; subject
as shown, padded to 50.

Reading the message is a seek to 6,016, one 128 byte read, then a seek to
12,838 in `M0004.TXT` and one 187 byte read. Two opens, two seeks, two
reads, nothing scanned.

**Drawing it in a listing costs nothing extra**, because the name is right
there in the record. If user 42 has since renamed to `RobM`, the listing
still says Daytona, and that is a deliberate choice rather than staleness
the board failed to notice: it is what they were called when they said it.
Opening the message resolves the id once and can show the current name if
that is what the UX work decides it wants.

### The bodies: `M####.TXT`

Append only, capped at 64 KB, then a new segment. Each message is a human
header line and then the text:

```
--- 000047 0000002A Daytona 1774101234 Anybody got a working 1571?
I've got two dead ones and a pile of belts, and neither will step.
Before I start swapping ICs, has anybody got a known-good one to
compare against?

```

The index's `offset` points at the first byte after that header line, and
`length` covers only the text, so the board never parses the body file at
all. The header line is for the person reading the card on a laptop and for
`FORUMS REBUILD`, which can reconstruct a lost index from the segments. It
carries the id as well as the handle so a rebuild restores the identity and
not just the name.

Rolling off is `remove("M0002.TXT")` and one header write to bump `first`.
Two operations, no copying, no renumbering, and the records that pointed
into it are recognised as rolled by arithmetic with no card reads at all.

### The read pointers: `PTRS.TXT`

**This is where the identity change pays off most visibly.**

The first version keyed this file by handle, which meant keeping it sorted,
binary searching it, rewriting it to insert, and revalidating a cached slot
on every write in case somebody registered in the meantime. All of that
existed because handles are mutable and arrive in no useful order.

An id is immutable and is assigned in order, so the file becomes a **direct
index**: the record for user id N is at byte offset `(N + 1) * 112`. Record
0 is a header of the same width.

**A lookup is one seek and one 112 byte read.** No search, no sort, no
insertion, no revalidation, no rewrite, ever.

| Field | Bytes |
|---|---:|
| user id, hex | 8 |
| TAB | 1 |
| handle, space padded, a cached render so a human can read the file | 20 |
| TAB | 1 |
| eight pointers, 6 digits each with a TAB after | 56 |
| day key (`clk::dayKey`, YYYYDDD) | 7 |
| TAB | 1 |
| posts made on that day | 3 |
| TAB | 1 |
| spare | 12 |
| CR LF | 2 |
| **total** | **112** |

#### Worked example: user 42's record

At byte offset 43 x 112 = 4,816:

```
0000002A\tDaytona             \t000412\t000087\t000000\t000000\t000000\t000000\t000000\t000000\t2026264\t003\t            \r\n
```

Read up to message 412 in topic 1 and 87 in topic 2, nothing in the rest,
three posts made on day 2026-264.

#### The self-check, and why holes are harmless

**A record counts only if its id field equals its own slot.** The id is
redundant with the position, exactly as the message number is redundant with
its slot in the index, and for the same reason.

That matters because FAT has no sparse files and the gap between ids is real
bytes. Seeking past the end of a file and writing extends it, and FatFs does
not promise what is in the gap. So a record in a never-written gap may be
garbage, and the self-check turns that from a correctness problem into
nothing at all: the id will not match, so the record reads as "no pointer",
so that caller sees everything as new. Which is the truth.

#### What it costs in space, said plainly

The file is `(highest id + 1) * 112` bytes. On a board with `max_users` at
250 and nothing retired, that is 28 KB. A board that retires and replaces
heavily for a decade might reach a few thousand ids and a 400 KB file. A
board that somehow issued 100,000 ids would have an 11 MB file on a card
that holds gigabytes.

That is the cheapest possible lookup bought with card space, which is the
one resource this subsystem has plenty of. It is worth saying out loud
rather than discovering: the file is as large as the highest id, not as
large as the number of accounts.

**The one stall, and how it is avoided.** Extending the file to reach a high
id allocates clusters, and doing that on a caller's path would be a hitch
for everybody. So the plugin **pre-extends `PTRS.TXT` to cover the current
highest id at `start()`**, when no caller exists, and logs it if it had to
grow. A new account registering while the board is up extends by one record
on first use, which is one cluster at most.

### The votes: `v/<id>.TXT`

**Keyed by voter, not by message**, and named by id: `v/0000002A.TXT`.
Fixed 24 byte records:

```
1\t000047\t+\t1774101299 \r\n
```

Topic 1, message 47, an up vote, when. A `.` in the sign column is a
withdrawn vote.

Capped at `vote_max` (200) records per account, so the file is at most 4,824
bytes and a "have I already voted on this" check is a bounded scan of **your
own** file. That is the property that matters: the cost is a function of how
much *you* have voted, which a cap controls, and not of how busy the board
is, which nothing controls.

Changing a vote is a seek and a 24 byte overwrite of your own record.
Casting a new one is an append. Both then write the tally into the index
record.

**The order of those two writes is fixed: the record of who voted first,
then the tally.** A power cut between them leaves the tally one low and the
caller still unable to vote twice. The other order lets them vote twice,
which is the one thing the record exists to prevent.

**A votes file can no longer be inherited by anybody**, which in the first
version needed a whole mechanism: slots were reused when an account was
deleted, so the plugin had to `remove()` the old votes file before claiming
a slot. Ids are never reused, so that code does not exist. When an account
is retired its votes file simply stays, inert, attached to an id nobody else
will ever have.

Votes by a retired or purged account stay in the tallies. The vote was cast
and the tally is history. `FORUMS REBUILD` recomputes tallies from the vote
files for a sysop who wants them swept.

## Permissions, and what a voting member is

Four levels per topic, following the four-levels-per-area shape `files`
arrived at, and for the same reason: one `write` level would have to mean
both "may post" and "may vote", which are not the same trust.

```
topic1 = general | General | all | users | users | co2
                             ^     ^       ^       ^
                             read  post    vote    mod
```

Each falls back on its own rather than all landing on read:

- `read` falls back to the plugin's read. Default `all`, so guests can read.
- `post` falls back to the plugin's write. Default `users`, so posting needs
  an account.
- `vote` falls back to **this topic's post level**. If you may say something
  here you may vote here, and a sysop who wants stricter sets it.
- `mod` falls back to the plugin's **admin**, never to post. Deleting is the
  destructive one, so it fails shut: a topic that says nothing about
  moderation does not inherit permission to delete from permission to post.

### The ladder has no rung for "trusted regular"

The ladder is `all`, `users`, `staff`, `co2`, `co1`, `sysop`. "Voting
member" most naturally means "has an account", which is `users`. The next
rung is staff, which is far too restrictive: a board where only the
operators may vote does not have voting.

So `vote = users` is the default and the gap is filled with a cheaper gate
that needs no new rung:

```
vote_calls = 5        ; calls before an account may vote
```

`UserRec::calls` already exists and is already loaded at login. One
comparison, no storage, and it implements "member" as "somebody who has been
here more than once", which is the sybil defence a small board actually
needs.

**And it is now enforceable in a way it was not before.** Under the old
model, somebody who burned their five calls could be deleted and
re-registered under the same handle with the counter back at zero. Retiring
rather than removing closes that, because the handle never comes back.

**Rob should confirm this is what he meant by voting being a privilege.**
The alternative reading is a per-account "may vote" flag, which is a
`UserRec` field, a form row and a `users.txt` key. That is now cheap,
because part 1 is already adding a `flags` byte and already touching every
one of those places, so if it is wanted it should be wanted **in phase 0**
rather than retrofitted.

## Guests

- **Within one call the pointer works properly.** It lives in the session's
  RAM and is real until they hang up, so a guest can read, get paged, come
  back and carry on.
- **Between calls it means nothing**, and the board says so once on the way
  in: `Guest pass: the board will not remember where you got to.` Saying it
  once beats silently forgetting.
- **Guests have no id**, so there is nothing to key a record to. That is now
  the clean statement of the rule rather than a special case: the pointer
  file is indexed by id, and a guest does not have one.
- **Guests never vote.** No id, no record, and a vote cast under a name
  anybody can type is a vote anybody can cast again.
- **Keying a guest pointer to their IP address is rejected.** An IP is not a
  person, it changes under them, and shared ones would quietly hand one
  guest another guest's place. That failure is invisible and reads as the
  board being broken.

A **new account** has no record until its first visit, and its slot in
`PTRS.TXT` reads as a hole, which the self-check turns into "no pointer".
Everything is new, which is correct for a first-time reader, and a topic
with more than a page of unread offers the choice rather than dumping it.

## Caps and rate limits, decided now

Posting is the second thing on this board a caller can use to consume
unbounded card space. Uploads were the first and the lesson was the same:
decide the caps before building, not after somebody finds them.

| Cap | Default | Range | Where enforced |
|---|---|---|---|
| lines in a message | 24 | 4 to 40 | editor, `msg_lines` |
| characters per line | 72 | fixed | `BBS_LINE_MAX` |
| body bytes per topic before the oldest segment rolls | 512 KB | 64 to 4096 KB | `keep_kb` |
| posts per account per day | 20 | 1 to 200 | `post_day`, counted in the pointer record |
| seconds between posts from one node | 30 | fixed | one `uint32_t` per session |
| votes recorded per account | 200 | fixed | `v/<id>.TXT` size |
| votes shown per message | 999 | fixed | tally saturates, never wraps |
| free card space below which posting is refused | 1 MB | fixed | `plat::sdInfo().freeKB` |

The per-day counter lives in the pointer record, which is already being
written and already seek-and-overwrite, so it costs nothing extra.

**Every per-account cap here is only meaningful because ids are permanent.**
A daily post cap that could be reset by deleting and re-registering an
account is not a cap, it is a speed bump. That is a second place where part
1 turns out to be load bearing rather than tidy.

## Retention: roll, and say so

When a topic's live body bytes pass `keep_kb`, the oldest segment file is
deleted and the header's `first` moves up. The index is untouched and
nothing is renumbered.

**This is a different case from mail, and the difference is worth stating
because the mail decision was right.** Mail chose "refuse, never destroy"
after a version where a third party writing to you destroyed a message you
had not read. The wrong thing there was that somebody else's action silently
destroyed your private property and reported success. A forum topic is
public, everyone has had the same chance to read it, and rolling the oldest
off is what every board from Citadel to FidoNet did. Citadel's rooms were
literally ring buffers.

The hazard that survives is real and is handled explicitly:

- The pointer is clamped to `firstmsg - 1` on entry, never silently.
- They are told: `31 messages in General expired before you read them.`
- Roll-off happens at post time, one segment at a time, never as a bulk
  purge, and it is one `remove()` plus one header write, so it is not
  something a caller waits for.

**`keep_kb` is per topic, so a sysop who wants "refuse, never destroy" can
have it** by setting a topic large enough that the card fills first, at
which point the board refuses the post. One mechanism, a config value, no
second code path.

## Moderation

- **The author may delete their own message.** Flag goes to `X`, the subject
  is replaced with `(deleted)`, the length is zeroed so the board cannot
  reach the text. The bytes stay in the segment until it rolls, and the docs
  say so plainly: delete here means "off the board", not "erased from the
  card".
- **`mod` level deletes anybody's**, pins with `!`, and locks a topic.
- **A deleted message keeps its slot**, which is the point of the tombstone
  and the direct answer to whether a pointer survives a deletion.
- **It leaves a visible hole**, one row: `42 X (deleted)  09/19`. Skipped by
  "next unread", shown in the listing. Hiding it makes a conversation with a
  gap read as the board being broken; showing it reads as moderated.
- **`MODLOG.TXT`** per topic, append only:
  `epoch<TAB>who id<TAB>who handle<TAB>action<TAB>msgno`. One append per
  action, and it records the id as well as the name so the audit trail
  survives the moderator renaming themselves, which is exactly the kind of
  thing an audit trail has to survive.

## The command and key surface: PROVISIONAL

> **A separate piece of work owns this.** A UX agent is designing the
> caller-facing flow against Citadel and the other boards that got this
> right, and what follows is a starting point for that work rather than a
> specification. Nothing below is settled except the two things marked as
> constraints, which come from the code and not from taste. Do not polish
> this section; take the argument to that work instead.

### Two constraints that are not UX opinions

- **`Session::listIdx` is a `uint8_t`**, and it is the row counter in every
  list on this board. A paged list emitting more than 255 rows wraps. A
  topic with 218 messages listed in full is 220 rows, which is inside the
  limit today and outside it the moment somebody's topic gets busy. **So a
  topic listing must be windowed**, never complete: `list_rows` (default
  100, hard maximum 200) at a time. This is designed around, not discovered
  later on somebody's busy board.
- **The score column can only appear at 60 columns or wider.** At 40 the
  width is spent on the subject, which is what tells a caller whether to
  read something. Worth noticing what that means: on the narrowest terminal
  the board serves, votes are invisible in the listing entirely and the
  subsystem still works. A design that only made sense once you could see
  the scores would be a design that had quietly become vote-sorted.

### Sketch: at the shell

| Command | Level | What |
|---|---|---|
| `FORUMS` | `CF_READ` | enter the subsystem |
| `FORUMS n` | `CF_READ` | enter topic n directly, the way `FILES n` does |
| `FORUMS NEW` | `CF_READ` | read everything new everywhere, without stopping at menus |
| `BULLETIN` | hidden alias | so fingers and old notes still work |
| `FORUMS SCAN` | `CF_ADMIN` | each topic's header figures |
| `FORUMS REBUILD [n]` | `CF_ADMIN` | recompute tallies, validate the index against the segments |

`FORUMS` is a place, not a command, exactly as `FILES` is: it owns the
session, plays `screens/forums.*` if the board has one, draws its own
prompt, and `Q` goes back one level while `Q` again leaves.

**When the board put them there rather than them typing it**, which
`Session::landing` already distinguishes, it skips the topic menu and drops
them on the first unread message. Log in, and you are reading what is new.
That is the headline feature made literal and it costs one `if`.

### Sketch: inside a topic

| Key | What |
|---|---|
| `SPACE` or Enter | the next unread message. The one key that matters. |
| `N` / `P` | next / previous, read or not |
| `A` | draw this one again |
| `L` | the topic listing, windowed |
| `J` | jump to a message number (marks everything before it read) |
| `M` | mark the whole topic read |
| `R` | reply: a new message with `Re: ` on the subject |
| `E` | a new message |
| `+` / `-` | vote up / down; the same key again withdraws |
| `D` | delete (author, or `mod` level) |
| `?` | help for this screen |
| `Q`, ESC | back to the topic menu |

`+` and `-` are 0x2B and 0x2D in PETSCII as well as ASCII, so they land the
same on a C64, a VT220 and SyncTERM.

### Sketch: 80 columns and 40 columns

```
--------------------------------------------------------------------------
 #47  Daytona                       Sun 21 Sep 09:13       +3    47 of 218
 Anybody got a working 1571?
--------------------------------------------------------------------------
I've got two dead ones and a pile of belts, and neither will step. Before
I start swapping ICs, has anybody got a known-good one to compare against?

SPACE next, N/P move, L list, R reply, E new, +/- vote, ? help, Q back
[T1] 47/218>
```

```
---------------------------------------
 #47 Daytona                 +3
 Sun 21 Sep 09:13      47 of 218
 Anybody got a working 1571?
---------------------------------------
I've got two dead ones and a pile of
belts, and neither will step. Before
I start swapping ICs, has anybody got
a known-good one to compare against?

SPACE next, L list, R reply, +- vote
[T1] 47/218>
```

Widths come from `Bbs::rowWidth`, which since 0.17.10 is the caller's own
width capped at 132. Nothing here carries a frozen 40. Plain ASCII gets
numbers and no highlight bar, the same split `canPoint()` already makes.

### Composing, and one thing that is not provisional

The core's `LineEditor` is the editor, for every terminal. One editor means
one set of bugs, and the full-screen ANSI editor sketched in `NEXT.md` is a
second editor plus a screen buffer per composing session, which is
per-session RAM this design deliberately spends nothing on.

`.S` saves, `.A` aborts, `.L` lists, which are the commands already written
down in `NEXT.md`.

**Each accepted line is appended straight to `.tmp/<id>.TXT` on the card**,
so a message in progress costs no per-session RAM beyond a line counter, and
the subject is the first line of that file rather than a 60 byte buffer per
session. Every line is one open, one append, one close: a typed line is a
human-scale event seconds apart, not a per-row loop, so this is not the
shape `findDesc` got wrong. No handle is ever held open across a keypress.

Naming that file by **id** rather than by node is what makes a draft survive
a dropped line and belong to a person rather than to a line. It is also what
makes draft resume cheap later.

**Messages are wrapped on output but never joined.** A line too long for
this terminal is broken at a word boundary; a line that fits is printed as
it is. Re-flowing paragraphs would make a 72 column message look native at
132 and would mangle ASCII art and code, and this board's callers write
both. This needs one greedy word wrapper, `bbsu::wrap`, which also settles
the queued "profile text should word wrap" item that has been parked because
wrapping per terminal width was the hard part.

## The RAM budget

All figures in bytes. Per-session arrays are `[BBS_MAX_NODES + 2]`, which is
12 on this board, the same shape `files` uses.

### Per session, 12 sessions

| What | Type | Bytes |
|---|---|---:|
| `g_where` where in the subsystem | `uint8_t` | 12 |
| `g_topic` which topic, 0xFF for the menu | `uint8_t` | 12 |
| `g_sel` menu highlight | `uint8_t` | 12 |
| `g_menuRows` rows the menu drew last time | `uint8_t` | 12 |
| `g_ask` which question is open | `uint8_t` | 12 |
| `g_lines` lines typed so far | `uint8_t` | 12 |
| `g_postsDay` posts made today | `uint8_t` | 12 |
| `g_ptrDirty` bitmask of topics whose pointer moved | `uint8_t` | 12 |
| `g_id` this caller's user id, read once at login | `uint32_t` | 48 |
| `g_cur` message being read | `uint32_t` | 48 |
| `g_winTop` top of the listing window | `uint32_t` | 48 |
| `g_postAt` last post, for the rate limit | `uint32_t` | 48 |
| `g_ptr[8]` read pointers, one per topic | `uint32_t` | 384 |
| **subtotal** | | **672** |

`g_id` replaces the first version's `g_slot`, and costs 24 bytes more
because an id is four bytes where a slot was two. It buys the removal of the
binary search, the insertion rewrite and the slot revalidation, so it is the
cheapest 24 bytes in this document.

### Board wide

| What | Bytes |
|---|---:|
| topic table, 8 x 60 (key, name, four levels, count, seg, first, keep) | 480 |
| index page cache, 8 records x 128, plus its key | 1,032 |
| one scratch index record | 128 |
| plugin index, counters, flags | 16 |
| **subtotal** | **1,656** |

### Totals

| | Bytes |
|---|---:|
| Part 1, identity (core, benefits everything) | 536 |
| Part 2, forums | 2,328 |
| **Total** | **2,864** |

Against the measured 29,176 of headroom, `_bss_end` at 0x3ffd5008 minus
0x3ffb0000 giving 151,560 against a `dram0_0_seg` of 180,736, that is 9.8%.
If the refresh-frame or output-buffer fix takes 18 KB first, leaving about
11,000, it is 26% of what is left. It fits either way, and it fits without
borrowing from the node count.

**Rob asked that the identity work not blow the 2,304 byte budget.** It
added 560: 536 for identity and 24 for the wider key. That is within a
rounding error of what the index page cache alone can give back, and the
identity half of it is core infrastructure that makes mail, uploads and
chat bans correct rather than being forum cost at all.

Nothing is allocated on the heap. There is no `calloc` at start, because
nothing here is sized by a config value the way chat's room buffer is.

### What it costs per phase

| Phase | Adds | Running |
|---|---:|---:|
| 0 identity and the rename | 536 | 536 |
| 1 storage and config | 624 | 1,160 |
| 2 reading and the pointer | 1,620 | 2,780 |
| 3 posting | 84 | 2,864 |
| 4 votes | 0 | 2,864 |
| 5 moderation | 0 | 2,864 |

Votes and moderation are free in static RAM because both are seek-and-write
against files that already exist.

### The one dial

The index page cache is 1,032 of the total and is the only thing here that
can be traded. Without it a 22 row listing page costs 22 opens, seeks, reads
and closes; with 8 records cached it costs 3; with 22 cached (2,816 bytes)
it costs 1. Eight is the recommendation. If RAM gets tight, halve it to 4
records (520 bytes) and accept six opens per page.

**Why a cache rather than holding the file open across the page.** The core
calls `rows()` per line and gives a plugin no "page finished" callback, so
there is nowhere to close a held handle, and a page break waits for a
keypress that may never come. Same rule `files` learned about directory
handles, against the same budget.

### Flash and heap, which are not the constraint

- Flash: 14 to 18 KB for the forums plugin, calibrated against chat at
  15.2 KB, plus a few hundred bytes for identity. Flash is at 68.7% with
  491 KB free in the OTA slot. To be measured, not assumed.
- Heap: none while running. But `BBS_SD_MAX_FILES` is `BBS_MAX_NODES + 4`
  = 14, and a `ScreenPlayer` holds a handle across a page break, so twelve
  callers each with a screen open plus the forums plugin's transient opens
  sits on the limit. **Raise it to `BBS_MAX_NODES + 8` when this lands**,
  costing about 3.1 KB of heap at mount at the measured 786 bytes per file.
  Heap, not static DRAM. This is the same class of bug as the 5-against-18
  that shipped once already: a failed open is indistinguishable from a file
  that is not there, so it fails silently.
- `storageBytes` is declared **0** and free space is checked at post time.
  Declaring 4.8 MB would refuse the plugin on a small card that could
  happily hold one small topic; `files` made this call for the same reason.

### Where `uint8_t` bites, said explicitly

- **`Session::listIdx`**, covered under the UX constraints above. It is the
  one place that shapes the design rather than being worked around.
- **`users::count()` returns `uint8_t` and caps at 255.** With retirement,
  the file holds more blocks than there are live accounts, so `count()` must
  mean live accounts and a second call must report total blocks. Otherwise
  `add()`'s `max_users` check starts refusing new members on a board that is
  mostly retired ones.
- **`max_users` is 250 because list indices are `uint8_t`**, and that is
  unchanged. A user id is deliberately **not** a list index, which is why it
  is 32 bits while `max_users` stays where it is. Widening `max_users` later
  is the same mechanical job it always was and this work does not make it
  harder.
- Topic count is 8 and no list of topics can overflow anything.
- Message numbers are `uint32_t` in RAM, six digits on the card: 999,999 per
  topic, which at twenty posts a day is 137 years. Past that the board
  refuses the post rather than wrapping.
- Vote tallies are three digits and **saturate at 999 rather than wrapping**.
  A wrapped tally would be a wrong answer somebody acts on.

## The invariant the whole design rests on

**The BBS loop is cooperative, single threaded and pinned to core 1, so one
caller's post, vote, pointer write or account rewrite runs start to finish
with nobody interleaved.** Every seek-and-overwrite in this plan is safe
because of that and only because of that. It stops being true the day any of
this moves to a second task, and if that happens every in-place write here
needs a lock. Worth writing at the top of the source, not only here.

The second rule, applied everywhere:

**Whatever makes a thing visible is written last.**

- **Identity:** the account record is written before its id is used
  anywhere. A crash leaves an id nobody has heard of, which is free. The
  other order issues the same id twice.
- **Posting:** the body goes into the segment, then the index record. A
  crash leaves orphan bytes nothing points at. The other order points the
  index at bytes that are not there.
- **Voting:** the voter's own record, then the tally. A crash leaves the
  tally one low and the double vote still refused. The other order lets them
  vote twice.

Recovery, stated so it can be tested: on start, each index is truncated to a
whole number of records, `filesize == (count + 1) * 128` is checked, and a
header that disagrees with the file is corrected from the file. A record
that will not parse is treated as deleted rather than discarded, because
discarding it would renumber everything after it.

---

# Phases

Riskiest and most foundational first. Each phase builds, tests and ships on
its own.

## Phase 0: identity, and the rename

**This is core work and it is not forums.** It ships on its own, fixes a
live bug, and is worth doing even if the forums are never built.

**Delivers**

- `UserRec::id` and `UserRec::flags`, the `id` key in `users.txt`, and the
  one-time migration in `users::begin()`.
- `users::byId()`, the resolver cache and the generation counter.
- Retirement: `USER DEL` retires and says so, `USER PURGE`, `USER FREE`.
  `users::count()` split into live and total.
- A reserved handle is refused at registration with a reason that does not
  leak whether the account is retired or merely taken, because those are the
  same answer to the person typing.
- The rename: `LAND_FORUMS`, `kLandVerbs`, the `Start` picker, the `landing`
  config message, and `landFromKey` accepting `bulletin` as a read-only
  alias.
- `USERS.md` and `COMMANDS.md` in the same change, as always.

**Tested**

- `tools/harness.sh --tag ident`.
- A `users.txt` with no ids gets them in file order, once, and a second boot
  changes nothing. Compare the file byte for byte across two boots.
- An id is never reissued: create, retire, create again, prove the new
  account got a higher number.
- Rename an account and prove `INFO`, `WHO` and the user manager all follow
  within one command, which is the resolver cache generation working.
- **The mail bug, before and after.** Leave mail for Alice, retire Alice,
  attempt to register "Alice", prove it is refused. Then run the same script
  against the pre-change tree and prove it succeeds, so the test documents
  the bug it fixes rather than merely passing.
- `USER FREE` refused on an account with calls, allowed on one without.
- **Fill `users.txt` to `max_users` with a mix of live and retired accounts**
  and prove a new registration is judged on live accounts only.
- The migration failing: make `userdata` unwritable, prove the board logs it
  and that nothing hands out an id.

**Caller visible:** almost nothing, which is the point. A renamed caller
keeps their mail, and a retired handle cannot be taken.

## Phase 1: the layout and the numbering, with no caller UI

The format is the risky part, because everything sits on it and changing it
after people have real cards means a migration nobody wants to write.

**Delivers**

- `<sd>/p/forums/<topic>/INDEX.TXT` and `M####.TXT` segments.
- Config: `topic1..8 = key | name | read | post | vote | mod`, the same
  six-field shape and the same `plugins::levelFromText` the file areas use.
  Topic keys validated to eight lowercase alphanumerics.
- `start()` creates folders, reads headers, repairs a torn index and
  pre-extends `PTRS.TXT`. Idempotent, because a CONFIG save calls it with
  callers on the line.
- `FORUMS SCAN`, sysop only.

**Tested**

- `tools/harness.sh --tag forum --card --only=forums`.
- A generated topic of 300 messages: prove the record at offset N x 128
  parses as message N for every N.
- **Torn write tests, which are the point of this phase.** Kill the host
  build after a segment append and before the index write; after a partial
  index record; after a partial header. Restart, prove the topic reads, the
  numbering has not moved, and the log says what was repaired.
- **`tools/forum_check.py`, which shares no code with the board.** It reads
  `INDEX.TXT` and the segments independently and asserts the numbering, and
  it can construct a topic by hand for the board to read back. This is the
  lrzsz lesson applied to a file format: a format tested only by the code
  that wrote it is a format that agrees with itself, and this project has
  paid for that twice.

**Caller visible: nothing.** Said plainly, because this is the phase people
skip.

## Phase 2: reading, and the pointer

The headline feature. Needs nothing from voting or posting, and would be
worth shipping alone.

**Delivers**

- `PTRS.TXT` as a direct index by id, with the id-equals-slot self-check.
- `FORUMS` as a place: topic menu with unread counts, cursor driven with
  numbers as the fallback, through one `visibleTopics()` helper so the
  highlight and the opener can never disagree.
- Entering a topic lands on the first unread.
- The reader, the windowed listing, `bbsu::wrap`.
- The login line: `Forums: 12 new in General, 3 in C64 & PET.`
- The landing: `Start = Forums` works and lands on the first unread.
- Guests: session-only pointer, told once.
- `FORUMS NEW`.

**Tested**

- Read three, log off, log back in, resume at the fourth. That one check is
  the feature.
- **Rename the caller between calls and prove the pointer follows them**,
  which is the whole reason part 1 exists and is the test that would have
  failed against the first version of this plan.
- Two callers on two nodes reading one topic: neither pointer moves the
  other.
- A guest reads, hangs up, calls back, gets everything as new, and was told
  it would be.
- A deliberately garbled record, and a record in a never-written gap, both
  read as "no pointer" rather than crashing or showing somebody else's
  place.
- 40 column PETSCII and 132 column ANSI both driven by `bbs-qa` reading the
  bytes, not by reading the code.
- **Measure the login cost with `plat::micros()`** around the pointer read
  and put the figure in the commit. `CLAUDE.md` asks for exactly this before
  message bases.

## Phase 3: posting

**Delivers**

- The line editor compose, subject, `.S` `.A` `.L`, the per-id temp file,
  per-line append and close.
- Body-then-index write order and the recovery that goes with it.
- `R` reply as a post with `Re: `.
- Every cap in the table, including the per-day counter in the pointer
  record and the card free-space check.
- Roll-off by segment.

**Tested**

- Post, read it back, check the bytes with `tools/forum_check.py`.
- Post past `keep_kb`, prove the oldest segment is gone, the index is
  untouched and message numbers did not move.
- A caller whose pointer fell below `firstmsg` is clamped and told how many
  they missed.
- The rate limit and the daily cap refuse and say why, and the daily cap
  resets across a day boundary.
- **Retire an account and prove its posts still render its name**, and that
  a purged account's posts do too, because the handle is in the record.
- A power cut between the segment append and the index write leaves orphan
  bytes and a readable topic.
- A CONFIG save while somebody is part way through typing: the plugin
  restarts, the caller goes back to the prompt, and the draft is still on
  the card. Same shape as the caller two pages deep in a CONFIG sub-page,
  which has already been got wrong once.
- **Measure a post with `plat::micros()`.** Four writes on a caller's path
  is the most synchronous card work anything here does, and it gets a number
  rather than an assumption.

## Phase 4: votes

**Delivers**

- `v/<id>.TXT`, `+` and `-`, the same key again withdrawing.
- The tally in the index, written after the voter record, saturating at 999.
- The `vote` level per topic and `vote_calls`.
- `FORUMS REBUILD`, sysop only.

**Tested**

- Vote, log off, log back on, prove the second vote is refused.
- Change a vote and withdraw one, and prove the tally follows both.
- **Retire an account and prove its votes stay counted and its votes file is
  never handed to anybody**, which under the first version needed a sweep
  and now needs nothing at all.
- **Burn `vote_calls`, retire the account, register the handle again, and
  prove it is refused**, which is the sybil hole retirement closes.
- A guest is not offered `+` or `-` and is refused if they press them.
- Fill a tally past 999 and prove it saturates.
- A power cut between the voter record and the tally leaves the tally low,
  the double vote still refused, and `REBUILD` fixes it.

## Phase 5: moderation and the sysop surface

**Delivers**

- Delete by author and by `mod`, the tombstone, the visible hole.
- Pin and lock.
- `MODLOG.TXT` recording the moderator's id as well as their name.
- `CONFIG forums`: four core keys, eight topic buttons and four tuning keys,
  with each button opening a topic's own page of Key, Name, Read, Post, Vote
  and Mod as `FF_CYCLE` pickers. This is the `FF_ACTION` and page-stack
  machinery the file areas already built, reused rather than rebuilt.

**Tested**

- A deleted message leaves a tombstone and every pointer still resolves.
- The mod log survives the moderator renaming themselves.
- A sub-page dropped mid-edit does not leave the CONFIG guard held.

## Later, and explicitly not needed to ship

- Draft resume after a dropped line. The temp file is already keyed by
  person rather than by node precisely so this is cheap.
- A full-screen editor for ANSI terminals.
- The caller log gaining an id **beside** its handle, so a sysop can follow
  one person across renames.
- Chat mail, chat bans and uploads moved onto ids. Sequenced in part 1; each
  is its own small piece of work.

---

# Challenges and concerns

Honest list. The first four want a decision before a line is written.

## 1. Phase 0 is a breaking change to `users.txt`, and downgrades are lossy

Adding `id` is additive and safe forwards. It is **not** safe backwards: an
older build drops unknown keys on the next write, so running one against a
migrated `users.txt` strips every id, and the next boot on the new firmware
reassigns them in file order to whoever is in the file by then. Mail,
pointers and votes would then belong to the wrong people, silently.

Nothing in firmware can prevent this. What can be done:

- Put a version marker in the file's comment header so the damage is at
  least visible afterwards.
- Say it in `USERS.md` and in the release note, in those words.
- **Take a backup before flashing**, which is already the standing advice
  for a layout change and is exactly as true here.

**Rob should decide whether this rides with a version bump loud enough that
nobody downgrades casually.** It is not a partition change and needs no
erase, so it is tempting to treat it as routine, and it is not.

## 2. Is `FORUMS` the name

The argument is above. The knock-on is four small edits plus a read-only
alias, and the alias is what stops Rob's own `land = bulletin` being
silently reset. If the answer is a different word, the same four places
change and the alias still has to exist.

## 3. Is "voting member" a level, or a flag

`vote = users` plus `vote_calls = 5` is the cheap answer and uses fields
that already exist. A per-account "may vote" flag is now **also** cheap,
because phase 0 is already adding a `flags` byte, a form row and a
`users.txt` key, and it would ride along for nearly nothing.

**That makes this a phase 0 decision rather than a phase 4 one.** Deciding
it late means touching `UserRec`, the form and the file format a second
time, for a board that by then has real accounts on it.

## 4. Roll or refuse, and do votes show in the listing

Both carried forward from the first version and both still open.

- Retention rolls, and the argument for why a public topic is not mail is
  above. `keep_kb` set high gives "refuse" with no second code path.
- **Vote visibility is the one place this design could quietly become the
  thing it rejects.** The mechanical argument is the strong one:
  vote-sorting and a chronological read pointer cannot share a view, because
  if the list is ordered by score then "the next unread" has no natural
  next, and the pointer is the headline feature. So the score can never
  drive reading order. Whether it appears in the listing at all is a taste
  question, and it now belongs to the UX work rather than here.

## 5. The 18 KB question

The known output-buffer or refresh-frame fix may take most of the 29,176.
This plan is 2,864 and fits either way, but if the buffer fix lands first
these numbers should be re-measured against what is actually left. The index
page cache is the dial.

Worth noting that `optimize`'s finding B1 alone, the resident
`BackupService` at 12,192 bytes for a feature switched off almost all the
time, would more than pay for both.

## 6. `users::count()` changing meaning is a quiet blast radius

Making it mean "live accounts" is right, and it is called from `add()`, the
user manager, `MEM` and the backup validator. Each one has to be looked at
and asked which number it wanted. A validator that keeps counting blocks and
an `add()` that counts live accounts is correct; getting it the other way
round means a board refusing new members because of people who left.

## 7. `CallRec` is the tempting wrong fix

Somebody will notice the caller log stores handles and "fix" it. It is not
broken; it is a historical record and should say what somebody was called at
the time. This is written down here so that argument only has to happen
once.

## 8. The CONFIG page is exactly full

Four core keys plus eight topic buttons plus four tuning keys is 16, and
`Form::kMaxFields` is 16. There is no room for a fifth tuning key, and an
undeclared key found in `system.cfg` is also listed on the page, so a sysop
who hand-adds one could push it over. Either accept eight topics as the hard
limit and say so, or spend one topic to buy two settings.

## 9. `F` is claimed twice today

`FILES` and `FX` both declare `"F"` as their shortcut, so only one of them
can win. Found while checking which letters were free for `FORUMS`. Small,
pre-existing, and worth fixing in whichever build touches the command table
next.

## 10. Card work on the caller's path, measured rather than assumed

A post writes four things. A login reads one pointer record. Both get
`plat::micros()` around them in the phase that builds them, with the figures
in the commit message. The board already collects `loopMaxUs_` and the
126 ms stall was found from it, so this is instrumentation rather than
guesswork. This design deliberately does not walk the card the way the file
areas do, and the measurement is how that claim gets checked rather than
believed.

## 11. The cooperative-loop assumption is load bearing

Every seek-and-overwrite here, and the derived next id, are correct only
because one caller's work runs to completion with nobody interleaved. True
today, and the reason the loop is pinned to core 1. It goes at the top of
the source, because whoever eventually moves something to a second task will
read the source and not this file.

---

# Considered and rejected

## Identity

- **A 16 bit id.** 65,535 is the kind of ceiling that looks generous and is
  not, and it cannot be widened later without renumbering, which is the one
  thing an id must never do. Four bytes across 29 `UserRec` instances is 116
  bytes to never have to think about it again.
- **Storing the next id in `system.cfg`.** A counter that can disagree with
  the file it is counting, and one more thing a restore can put out of step.
  Deriving it from the highest id in `users.txt` cannot be wrong, and is
  safe precisely because accounts are never removed.
- **Reusing the id of a removed account.** The entire bug.
- **Keying anything off an account's position in `users.txt`.** It shifts
  the moment a block is added or removed, which is the message-renumbering
  bug wearing a different hat. Every argument for a permanent message slot
  is an argument against a positional user key.
- **Hashing the handle into a fixed-width id.** Deterministic and needs no
  migration, and it fails at the first rename, which is the thing being
  fixed. It also collides: 32 bits over 250 accounts is roughly a 0.3%
  chance of two people being the same person to the board.
- **Resolving every name live from `users.txt`.** One full-file scan and one
  `fopen` per listing row. This is `findDesc` reopening `FILES.BBS` per row,
  which is a suspect in the measured 126 ms stall, rebuilt on the internal
  partition where it would be worse.
- **A full id-to-handle table in RAM.** 250 entries at 25 bytes is 6.25 KB
  of the 29 KB headroom for a cache that a 416 byte one and a stored render
  make unnecessary.
- **Rewriting mail, uploads and the caller log during the phase 0
  migration.** Tempting, and it would make one boot fix everything. It also
  means the riskiest single boot in the board's history touching four file
  formats at once, and a failure part way through leaving a board whose
  accounts and whose mail disagree. Each subsystem moves on its own, in its
  own build, with its own test.
- **Letting `USER DEL` keep deleting, with a warning.** A warning that is
  read once and dismissed forever is not a fix, and the data loss is silent
  and delayed.

## Forums

- **Threading, or a `reply_to` field kept "just in case".** Rob's decision,
  and concretely: a field nothing reads is a field that grows a tree view
  the first time somebody has a free afternoon. The `Re:` convention gives
  most of the follow-the-conversation benefit for no storage and no code.
- **The pointer on `UserRec`, as `NEXT.md` sketched.** It puts card-shaped
  data in the one file deliberately kept on LittleFS because it must survive
  the card, and every pointer change would rewrite all 250 accounts through
  a temp file. The thing that must never be at risk would be rewritten every
  time somebody finished reading a topic.
- **`.newsrc` style read ranges.** No fixed width, so no seek-and-overwrite,
  and one caller's record grows without bound. A high-water mark is the
  degenerate case and satisfies "pick up exactly where they left off"
  exactly. The cost is real and should be said: jump to message 60 and
  20 to 59 are marked read too, which is why `J` says so.
- **A handle-sorted pointer file with a binary search.** What the first
  version had, and it is gone: eight seeks and an occasional 28 KB insertion
  rewrite became one seek and one read the moment the key stopped being
  mutable.
- **An in-RAM index per topic**, which `NEXT.md` also sketched. 200 messages
  times anything times eight topics does not fit in 29 KB, and the point of
  a fixed-width index on the card is that seeking replaces holding.
- **A per-message voter bitmap keyed by account index.** O(1) and 32 bytes
  per message, and it breaks the moment an account is added or removed.
  Keyed by **id** it would be a 4 GB bitmap, which is its own answer.
- **A board-wide vote log scanned on each vote.** The scan is proportional
  to how busy the board is, which nothing bounds, on a caller's keypress.
  Per-voter files are proportional to how much that voter has voted, which a
  cap bounds exactly.
- **A per-message voter block of fixed size.** O(1) and clean, and it caps
  voters per message, and "this message has all the votes it can take" is
  not a sentence a board should have to say.
- **Repacking the index to reclaim space.** Any front truncation either
  renumbers or costs a full copy of the topic on a caller's path. Deleting a
  whole segment costs one `remove()`, and an index growing at 0.9 MB a year
  costs nothing on a card.
- **A full-screen ANSI editor in the first version.** A second editor is a
  second set of bugs and needs a screen buffer per composing session, which
  is per-session RAM this design spends nothing on.
- **Re-flowing paragraphs to the reader's width.** It mangles art and code,
  and this board's callers write both.
- **Guest pointers keyed by IP address.** An IP is not a person. With ids
  this stops being a judgement call and becomes a statement of fact: the
  pointer file is indexed by id and a guest has not got one.
- **A `FORUMS` command that prints a list.** The file areas already learned
  this: a place has a prompt, its own keys and a `Q` that goes back one
  level, and a command has none of those.
- **Archiving rolled messages to a second file on the card.** Unbounded
  growth is what the cap exists to prevent, and the segments are plain text
  specifically so a sysop can copy them off before they roll.
- **Declaring the plugin's real `storageBytes`.** Eight topics at 512 KB is
  4.8 MB, and declaring it would refuse the plugin on a card that could
  happily hold one small topic.

---

# What this unlocks

**Identity was always going to be needed and it was always going to be
cheapest now.** Every subsystem on the roadmap stores something about a
person: mail worth the name, the feedback plugin, poker chips, a GPIO point
table with an owner, board linking where two systems have to agree who
somebody is. Every one of those would have been built on handles, and every
one of them would have had the same bug.

The forums are the first subsystem that would have had it, which is the only
reason it surfaced here rather than in the mail phase, where it would have
been found by somebody losing mail on a real board.

The queued mail phase wants "read and reply from the main prompt" and "mail
that is not rationed when a card is present", with the standing instruction
to make it one plugin whose storage moves rather than two plugins with two
formats. Everything in part 2 is that shape already: fixed-width records on
the card, seek-and-overwrite, a direct index by id, an append-only body
store, and a version tag in every header. **A mailbox is a topic with one
reader and a recipient id.** Whoever builds the mail phase should read this
file first, and if the two end up sharing a storage layer rather than merely
a style, that is the right outcome rather than a coincidence.

And board linking, which is the furthest thing on the list, is the one that
makes the id non-negotiable. Two boards exchanging messages have to agree on
who said what, and a handle is not an identity even on one board. It is
worth knowing now that the id will eventually need a board part as well as a
user part, and that this design leaves room for it: eight hex digits is the
field width, and a linked board's messages would carry a qualified id rather
than a bare one. Not built, not designed, just not painted into a corner.
