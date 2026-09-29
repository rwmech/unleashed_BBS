# FidoNet (FTN) and ZMODEM: build or not, and what each costs

2026-09-29, against main at 734d246 (core 1.2.0). Research, measurement and
reading only; no source, config, test or doc was changed. Both firmware
images were rebuilt to read their sizes (`pio run -e esp32cam_aithinker`,
`pio run -e esp32dev`), which left the gitignored generated
`sdkconfig.esp32cam_aithinker` in the tree.

## Bottom line

- **FidoNet: build it, but not in 1.3.0, and not in the shape sketched.**
  It is the only thing on the roadmap that fills a new board's empty
  forums with real conversation from exactly this audience, and it is
  cheap in RAM (about 250 bytes static). But the hidden cost is not the
  mailer, it is the forums plugin: the reader cannot show a message longer
  than 1,728 bytes, draws a message in one pass into a 3 KB buffer, has one
  writer rule that a runner-side tosser would break, and opens every body
  segment on every post. Those are fixed first (phase 0, useful with or
  without FTN), then fsxNet as a poll-only leaf with raw .PKT (phase 1).
  Target 1.4.0 for phase 1; phase 0 can go into any 1.3.x.
- **The frozen forum format survives.** The 128-byte index needs no change.
  A foreign author, their FTN address, the MSGID and the REPLY all fit in
  the body file and a reserved author id. The only hard format limit met is
  the 4-digit length field: a body tops out at 9,999 bytes.
- **Traffic is small and honest.** fsxNet's general echo runs about 5 to 8
  messages a day and its BBS echo about 3; the FidoNet echoes sampled run
  under one a day. That is a lively forum on a board with a handful of
  callers, and nowhere near enough to stress the card.
- **ZMODEM: fits 1.3.0 as planned.** About 30 to 60 bytes of static DRAM
  with the XMODEM engine in a union (estimate), 8 to 11 KB of flash
  (estimate). The claim checks out: a file-backed sender answers ZRPOS by
  seeking, no retransmit window. One core change is load-bearing and not
  in anyone's plan yet: the socket read gate (`BBS_RX_ROOM`) would stop a
  streaming sender ever hearing the receiver's ZRPOS.

Caller-visible: FTN puts other people's messages in the forums and sends
callers' posts to about 300 boards, where they cannot be recalled. ZMODEM
is a new choice in FILES' transfer prompt and is about 2.8x XMODEM-1K on a
long link, not more, because of the 2,880-byte TCP send buffer.

## Summary

| Item | Flash | Static DRAM | Heap | Gives up | Risk | Version |
|---|---:|---:|---:|---|---|---|
| FTN phase 0: forum reader, writer queue, segment cache, foreign-author body prefix | 3-5 KB est. | 20-50 est. | 0 (reuses `s.compose`) | a post confirms after the runner writes it (a spinner, milliseconds) | medium: touches the frozen-format code paths, not the format | any 1.3.x |
| FTN phase 1: fsxNet leaf, binkp poll-only, raw PKT, echomail in and out | 20-26 KB est. | ~250 est. | ~20 KB during a poll, 0 otherwise | callers' posts leave the board for good; sysop's real name in the nodelist | medium: interop is the risk, test against binkd | 1.4.0 |
| FTN phase 2: netmail to a sysop forum, ARCmail, retention, long-message split | 6-9 KB est. | ~30 est. | +43 KB while unzipping (PSRAM boards, or heap-gated) | none new | low | 1.4.x |
| ZMODEM send and receive, engine in a union with XMODEM | 8-11 KB est. | 30-60 est. | 0 | upload resume (declined on purpose) | medium: the RX gate change is core | 1.3.0 |

Nothing here saves memory; this report is a cost sheet. There is no
"huge impact for almost nothing" entry, and none is promoted to fill one.

### Since the last memory report (memory-2026-09-25-1.1.1.md, link.16 figures)

Measured off the ELFs today, `_bss_end - 0x3FFB0000` against the
`dram0_0_seg` length of `0x2c200` (180,736), read from each env's generated
`memory.ld`:

| Board | `_bss_end` | Static DRAM | Free | link.16 | App image | Slot free (1.5 MB) |
|---|---|---:|---:|---:|---:|---:|
| ESP32-CAM (esp32cam_aithinker) | 0x3ffdb770 | 178,032 | **2,704** | 2,720 (-16) | 1,398,224 (+144 on v1.2.0) | 174,640 |
| WROOM (esp32dev) | 0x3ffd8710 | 165,648 | **15,088** | ~15,080 (+8) | 1,267,024 | 305,840 |

- Still not acted on, and still the one that matters for anything new on
  the camera boards: item 4 of the 1.1.1 report, zero-filled statics into
  PSRAM (about 121 KB back on the ESP32-CAM). Neither feature here needs
  it; both would feel it.
- `Bbs::instance()::bbs` is 98,308 bytes (0x18004) on the ESP32-CAM, as
  before.

---

## Part 1: FidoNet

### 1.1 Is it used enough to be worth it?

**The networks.**
- FidoNet's nodelist of 2026-09-28: 1,240 nodes, 1,188 active, 1,026 with
  binkp; zone 2 has 765 (61.7%), zone 1 has 392 (31.6%). Verified on
  [NodelistDB stats](https://nodelist.fidonet.cc/stats); the figures in the
  brief are right.
- fsxNet (zone 21), [founded late 2015](https://github.com/fsxnet/infopack/blob/master/history.txt):
  the current nodelist, [FSXNET.268](https://github.com/fsxnet/nodelist)
  (2026-09-25), has 314 ordinary nodes plus 14 Pvt, 5 Down, 1 Hold,
  5 hubs and 5 hosts; 316 entries carry IBN (binkp). Counted from the
  file. Among 2026's joiners is [The CP/M Den](https://github.com/fsxnet/infopack/blob/master/history.txt),
  which is this project's audience exactly.
- Every major BBS package speaks FTN: [Synchronet (BinkIT)](http://wiki.synchro.net/module:binkit),
  [Mystic](https://wiki.mysticbbs.com/doku.php?id=config_echomail_nodes),
  [ENiGMA½](https://nuskooler.github.io/enigma-bbs/messageareas/ftn.html),
  [WWIV](https://docs.wwivbbs.org/en/latest/network/ftn_nets/),
  [Maximus NG](https://maximusng-bbs.github.io/maximus/fidonet/).
  We found no FTN node running on a microcontroller; do not claim first.

**Traffic.** No network publishes a current per-echo count that could be
fetched (fsxNet's [FSX_STA](https://fsxnet.nz/fsxnet/echomail) was retired
on 2025-02-22 and its bot posts only reach readers of FSX_BOT; the
[ems-bbs traffic report](https://www.ems-bbs.com/public/fsxnet.wct) failed
TLS today). So it was measured from Vertrauen (rob.synchro.net, Synchronet's
home board), which carries both networks. Synchronet's thread id is the
message number of a thread's first message in that sub, so two thread ids
and their dates give messages per day in the echo as that board received
it. Method: estimate from message numbers, not a published count.

| Echo | From | To | Messages | Per day |
|---|---|---|---:|---:|
| [FSX_GEN](https://rob.synchro.net/?page=001-forum.ssjs&sub=fsx_gen) | #111859, 13 Feb 2026 | #113621, 27 Sep 2026 | 1,762 | 7.8 |
| FSX_GEN, recent | #113362, 11 Aug 2026 | #113621, 27 Sep 2026 | 259 | 5.5 |
| [FSX_BBS](https://rob.synchro.net/?page=001-forum.ssjs&sub=fsx_bbs) | #13995, 27 Aug 2026 | #14101, 29 Sep 2026 | 106 | 3.2 |
| [FidoNet FN_SYSOP](https://rob.synchro.net/?page=001-forum.ssjs&sub=fn_sysop) | #150982, 21 Apr 2026 | #151025, 9 Sep 2026 | 43 | 0.3 |
| [FidoNet CHAT](https://rob.synchro.net/?page=001-forum.ssjs&sub=chat) | #41628, 13 Jan 2026 | #41719, 20 Aug 2026 | 91 | 0.4 |
| [FidoNet CBM](https://rob.synchro.net/?page=001-forum.ssjs&sub=cbm) | #2223, 15 Aug 2026 | #2245, 27 Sep 2026 | 22 | 0.5 |

Reading: fsxNet's 13 current areas together are plausibly 15 to 30
messages a day (estimate: two measured areas plus eleven quieter ones);
FidoNet's echoes are individually very quiet. The size of a day's full
fsxNet feed is therefore tens of KB, which is the right size for this
board. The value is not volume: it is that a board with three callers gets
a forum that moves every day, written by the people who actually run and
call BBSes, and that its callers get an audience.

**Joining fsxNet** ([join page](https://fsxnet.nz/fsxnet/join),
[infopack fsxnet.txt](https://github.com/fsxnet/infopack/blob/master/fsxnet.txt)):
- Fill in the infopack's application form and email it to Avon (Paul
  Hayton, avon@bbs.nz) or netmail 3:770/100; while he was away in June 2026
  the history says to ask Deon (net 3) or Todd (net 2). It is a manual
  review by a person, so FTN is never an instant feature.
- Fields: real name, alias, city and country, BBS name and software, OS,
  telnet, SSH and binkp addresses, binkp port, CRASH or HOLD, website,
  email, and an AreaFix/binkp password of at most 8 upper-case A-Z 0-9.
- **Real-name policy:** "Members real names are used in the nodelist,
  members and users aliases or real names are welcome in all echoareas."
  The sysop's name is public; callers keep their handles.
- Rules: no country politics or religion; `[ANSI]` in the subject of ANSI
  posts; the same ad no more than once a week; poll the hub hourly, at
  least daily; de-listed after 45 days of mail held at the hub.
- **A poll-only node is accepted**: 14 Pvt nodes are in FSXNET.268
  (`-Unpublished-`, no IBN). Choose HOLD, and the hub keeps our mail until
  we poll. No listener, no port forward, no extra socket.
- **FidoNet proper is different**: [Policy 4](https://www.fidonet.org/policy4.txt)
  requires a node to take mail during Zone Mail Hour, which a poll-only
  board cannot. On FidoNet this board is a point (x:y/z.p) under a boss
  node, which is what type 2+ packets and 4D addresses are for. Same code;
  a different application.

### 1.2 The forum format: does foreign mail fit the frozen index?

Read from `src/plugins/forums.cpp:83-148` (layout, asserts) and `:400-605`
(records, bodies). The index record is 128 bytes, asserted by offsets and
total (`static_assert(kOffCrLf + 2 == kRec)` and three more), message N at
N x 128. Fields: number 6, flags 2, **author id 8 hex**, handle 20 (a
"cached render, allowed to be stale"), epoch 10, segment 4, body offset 6,
**body length 4 (decimal)**, subject hash 8, subject 49. Bodies are
appended to `M####.TXT` segments, rolled at 128 KB (`kSegMax`), and are
free text.

What FTN needs, and where it goes without touching the index:

- **Foreign author.** `authorId` is written (`:2080`, `callerId(s)`) and
  parsed (`:472`) and read nowhere else in the tree (grep). Reserve one
  value, `0xFFFFFFFF`, as "from the network": ids are derived as highest
  plus one and the account index is `uint8_t`, so it cannot collide, and
  `0` already means guest. The name goes in the handle field, cut to 20.
- **Full name, FTN address, full subject, MSGID, REPLY, origin.** In the
  body, as a prefix of `0x01`-led lines (the FTN kludge convention), which a
  caller's editor cannot produce. Old messages have no prefix, so nothing
  migrates. The reader shows `By: Paul Hayton @ 21:1/100` from the prefix
  and hides the rest.
- **Threading.** Grouping is by the stored subject hash, and a reply
  carries its parent's hash (`:2087`). An inbound message with REPLY takes
  its parent's hash by looking the parent's MSGID up in the dupe ring (1.3);
  without one, or with a parent we never saw, `foldHash` of the subject
  with leading `Re:` stripped. An outbound reply to a foreign message reads
  the parent's MSGID out of the parent body's prefix.
- **Our own MSGIDs, with no storage.** [FTS-0009](http://ftsc.org/docs/fts-0009.001)
  wants a serial unique per system for three years. Derive it from
  `(forum key, message number, epoch)`, all three in the index, so any
  message's MSGID can be recomputed at any time. Not from `(key, number)`
  alone: a forum recreated on a new card would reuse numbers, and every
  other board would drop our new posts as dupes. A side benefit: a
  re-export after a crash carries the same MSGID, so the network dupe-kills
  it instead of showing it twice.
- **The one hard limit: body length is 4 decimal digits, 9,999 bytes.**
  Cap a foreign body at 9,000 with a one-line note that it was cut, or
  (phase 2) store a long message as consecutive records under one subject
  hash, "part 2 of 3", which the format already expresses. FIDONEWS issues
  (tens of KB) and ANSI art in FSX_ADS are the areas this bites; do not
  link those in phase 1.

**The format holds. What does not hold is the reader and the write path**,
and this is the deciding cost the sketch did not have:

- `kBodyMax` is 1,728 (`:518`); `appendBody` cuts every body to it (`:555`),
  and `showMessage` reads into `char body[kBodyMax + 1]` on the BBS task's
  stack (`:1851`). Echomail with quoting is routinely longer.
- `showMessage` renders the whole body in one pass into the caller's
  3,072-byte timeline (`BBS_TL_BYTES`), guarded at 96 rows. A 6 KB message
  cannot be drawn that way on any terminal. It has to become a paged read
  through the core list machinery (`startPluginList` and the `rows()` hook)
  reading the body from the card in slices, with `s.compose` (1,536 bytes,
  per session, idle while reading) as the slice buffer: no new per-session
  state, so not twelve times anything.
- `appendBody` finds the segment to write by opening every segment from
  `M0000` upward until one is under 128 KB (`:533-545`). Every post opens
  every full segment. See 4.1.

Migration cost: none on disk. The work is phase 0, in code.

### 1.3 Size

**Calibration, measured.** Linked bytes per object from the ESP32-CAM
link map (`unleashed-bbs.map`, text plus rodata), against non-blank,
non-comment lines:

| Object | Code lines | Flash | Bytes a line |
|---|---:|---:|---:|
| xmodem.cpp (a protocol state machine) | 615 | 4,005 | 6.5 |
| forums.cpp | 1,821 | 21,192 | 11.6 |
| announce.cpp (an HTTP client, JSON, CONFIG) | 965 | 12,355 | 12.8 |

Protocol code lands near 6.5 bytes a line; plugin code with strings and
CONFIG tables near 12. The estimates below use the matching figure.

**Flash, phase 1, estimated from line counts of what each part has to do:**

| Part | Lines, est. | Flash, est. |
|---|---:|---:|
| binkp client: frames, M_NUL/ADR/PWD/FILE/GOT/EOB/ERR/BSY/SKIP, CRAM-MD5, data frames streamed to the card | 600 | 5-7 KB |
| PKT type 2+ codec ([FTS-0001](http://ftsc.org/docs/fts-0001.016), FSC-0048): packet header, packed message header, line splitter | 300 | 2-3 KB |
| Tosser: AREA to forum, dupe ring, kludge prefix, charset fold, ANSI and SEEN-BY/PATH strip, REPLY threading | 450 | 4-5 KB |
| Scanner: per-forum export mark, MSGID/REPLY, tear and origin lines, SEEN-BY/PATH, @-code strip, outbound PKT | 300 | 3 KB |
| Plugin: CONFIG fidonet, `FIDO`, `FIDO POLL`, status for SYS, schedule, DNS on the runner | 300 | 3-4 KB |
| Forums side: prefix read, `By:` with address, reserved id, cap parameter | 150 | 1.5-2 KB |
| **Phase 1** | ~2,100 | **20-26 KB** |

The sketch's 20-35 KB is right for phases 1 and 2 together.

- **CRAM-MD5 is nearly free: measured.** `mbedtls_md5` and the whole
  `mbedtls_md_hmac` family are already linked in both images (nm), and the
  ROM has `esp_rom_md5_*`. The glue is a hundred bytes or so. The mechanism
  is [FTS-1027](http://ftsc.org/docs/fts-1026.001) (the server's
  `OPT CRAM-MD5-<challenge>`, answered with the HMAC).
- **Reuse: reference, not code.** [binkd](https://github.com/pgul/binkd)'s
  `protocol.c` is GPL-2.0-or-later ("either version 2 ... or (at your
  option) any later version"), so it could legally be taken under
  GPL-3.0-or-later, but it is 3,473 lines tied to binkd's config, threads
  and OS layer and allocates two `MAX_BLKSIZE` (32 KB) buffers. Write our
  own engine the way `xmodem.cpp` is written (fed bytes, asked for bytes,
  owns no socket) and use binkd as the thing to test against.
  ENiGMA½'s binkp fixed a real interop bug this month by reading binkd:
  it waited for a zero-length frame that binkd never sends; files end on
  the declared size ([PR #761](https://github.com/NuSkooler/enigma-bbs/pull/761),
  2026-09-04). Same lesson as lrzsz here: test against somebody else's
  implementation.

**Static DRAM, estimate: about 250 bytes.** Two `runner::Job`s (poll and
toss), the plugin's state (own and hub addresses, schedule, last result
and counts for SYS), and one "linked" byte per forum (16). The hub's host
name, the password and the echo tags live in `system.cfg` and are read
onto the heap when a poll starts, which is why this is 250 and not 600:
echo tags in `Forum` would be 16 x ~24 = 384 bytes held for ever. The
export high-water mark goes in the forum's header record, which was built
for exactly this ("a key can be added later without moving anything",
`:326-330`); there is room, since today's header uses about 52 of its 126
bytes. The ESP32-CAM's 2,704 takes it; the WROOM's 15,088 does not notice.

**Heap during a poll, estimate: about 20 KB, zero otherwise.**
- binkp needs no 32 KB buffer: data frames go straight to the card in
  1 KB reads; command frames are short and a 256-byte buffer truncates an
  over-long `M_NUL`. About 1 KB in, 1 KB out.
- One message at a time for the toss: up to 9 KB body plus kludges, 10 KB.
- The dupe table's hash column, 8 KB (below).
- lwIP's own socket and window (2,880 each way here).
- The runner's 8 KB stack already exists only while jobs run.
- **On the camera boards this must be asked for in PSRAM explicitly**
  (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`, the `photoAlloc` pattern):
  `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` is 16,384 there, so every piece
  above would land in internal heap, which sits near 9 KB with the camera
  up. On the WROOM the 20 KB is internal; measure SYS's heap low during a
  poll with callers on before calling it fine.

**The dupe ring on the card.** `<sd>/p/fidonet/dupes.dat`, 8-byte entries:
MSGID hash 4, message number 3, forum 1. 2,048 entries is 16 KB on the
card and its hash column is 8 KB of heap during a toss; about two months of
the whole fsxNet feed at the estimated 30 a day. Our own exported MSGIDs go
in too, so a post echoed back is dropped. A false dupe per message is
2,048 / 2^32, about 5e-7; over a year's 11,000 messages about a 0.5% chance
of dropping one. 8,192 entries on PSRAM boards.

**Sockets.** Poll-only means one client socket for the few seconds of a
poll and no listener. The budget (`Bbs::busyFits`, `BBS_SOCK_RESERVE` 3 for
the backup window's two and announce's one) has no slot for it. On the
WROOM (no SSH listener) the worst case is listener 1 + sessions 12 +
backup 2 + one outbound client = 16 of 16. So **binkp and announce take
turns**: one outbound client at a time, as a claim, and the budget does not
move. A poll that finds the budget short waits for the next minute and says
so once in the log.

### 1.4 Rule no. 1

**The writer rule.** `runner.h` is explicit: a job must not "write a card
file the loop may be writing (CONFIG_FATFS_FS_LOCK=0, so two tasks on one
FAT file corrupt it)". Local posts, removals and header writes happen on
the loop today; a tosser on the runner writing the same `INDEX.TXT` and
`M####.TXT` is two writers, and the runner is three priorities under the
BBS task on the same core, so the loop can preempt it mid-write and write
the same file. The pattern that already exists is the photo FILES.BBS queue
(`files.cpp:3084-3255`): one queue, one runner job, the loop's own writes
for those areas go through it too, the queue on the heap only while edits
wait. Do the same for forums: **every forum write goes through one queue on
the runner**, local posts included, and the toss is a job on the same
runner, which runs one job at a time, so the toss and a caller's post
cannot overlap by construction.
- What it gives up: a post is confirmed after the runner has written it,
  not in the same pass: a spinner for milliseconds, and during a toss a
  wait for the slice in progress (toss in slices of, say, 20 messages, each
  slice a job, so a post never waits behind a whole bundle).
- What it gains, FTN or not: the post's card writes leave the loop.
- The alternative, keeping local writes on the loop and a per-forum
  "tossing" flag under `runLock` both sides check, is less code and easier
  to get wrong: one missed check is a corrupt index.
- Readers are fine: the index record for N+1 is written before `newest`
  moves, and the reader never looks past `newest`.

**What a big inbound bundle does.**
- To the loop: nothing directly. Every byte of it is card work on the
  runner. The exposure is the FatFs volume lock (`CONFIG_FATFS_TIMEOUT_MS`
  10,000): a caller's screen or forum read on the card waits for the
  runner's current `f_*` call. Keep every runner write at 2 KB or less and
  `runner::breathe()` between messages. An SD card's own garbage
  collection can still hold a single write for 100 ms or more, the same
  exposure the camera's saves already have (CLAUDE.md records loop card
  opens waiting behind them). Measure SYS's slow passes during a toss with
  callers reading card screens.
- To the card: a bundle lands under a temporary name, is renamed when its
  declared size has arrived, and only then is `M_GOT` sent, so the hub
  deletes nothing we have not kept. If free space is under a reserve, the
  file is refused with `M_SKIP` and stays at the hub. The PKT is deleted
  after its toss. Nothing touches LittleFS, so no flash erase stops both
  cores.
- The big bundle to fear is a rescan: an AreaFix `%RESCAN` can send
  thousands of old messages. Phase 1 never asks for one, and the toss's
  slices keep even that off the loop; it only makes it long.

### 1.5 Moderation and charset

**Outside content.** A linked forum shows words nobody on this board wrote.
- The 1.3.0 censorship list (banned words for "everything public") applies
  at the toss: a match is imported already removed (the `X` flag), so the
  sysop can see it and nobody else can, and its MSGID still goes in the
  ring.
- Removal is local only. Echomail cannot be recalled from 300 boards; say
  so in the sysop's docs, not only here.
- Do not link FSX_ADS (ANSI art) or FSX_DAT (inter-BBS data) or FSX_BOT.

**Callers' posts go out.**
- Default a linked forum's `start` and `reply` to accounts (no guests).
- A line on the post screen of a linked forum: this goes to fsxNet and
  cannot be taken back.
- A per-caller daily cap for linked forums. The sysop answers to the hub
  for what their callers post, and fsxNet's rules (no politics or
  religion, one ad a week) become the sysop's rules.
- The privacy screen and the site want a line about it (explain).
- Our @-codes are stripped to plain text on export: other software either
  shows them raw or, worse, reads `@` codes of its own.

**Charset.** `Term::ch` prints any byte above 0x7E as `?` (`term.cpp:248-258`),
and forum bodies go through that path; the only non-ASCII the board carries
is the UTF-8 micro sign. So a CP437 box character arrives as `?`, and a
UTF-8 `é` as `??`. The tosser folds to ASCII at import, by the CHRS kludge
(FTS-5003: CP437, LATIN-1, UTF-8, IBMPC, ASCII): a 128-entry CP437 table and
a Latin-1 range table, accents to their letters, box drawing to `+-|`,
curly quotes to straight. About 0.5 to 1 KB of flash. ANSI escape sequences
and FTN soft returns (0x8D) are removed, lines end in LF. Stored ASCII
reaches every terminal, PETSCII included through `asciiToPet`, and wraps
correctly because one byte is one column. What it gives up: a UTF-8 or
CP437 caller sees `e` where the author wrote `é`. Storing the original and
translating per terminal is the better end state and a reader rewrite for
width measurement; not phase 1. Outbound is ASCII, so `CHRS: ASCII 1` or
none.

### 1.6 FTN, the routing middleware and board linking

Three different jobs, and FTN should be the protocol for exactly one:

- **Messages between boards, asynchronously: FTN.** It is what the world
  already speaks, it is store-and-forward on purpose, and a µnleashed-only
  message network would be an FTN othernet for free. Inventing a message
  federation protocol of our own would be wrong: it would federate with
  nobody.
- **Chat that spans boards (the superchat, DDial-style linking): not FTN.**
  A hub polled hourly is hours of latency. That stays the terminal-mode
  and linking design already queued.
- **Events outward (photos, posts to social media, pages): not FTN.** The
  middleware's plain-HTTP event protocol is right for that. It could carry
  an FTN gateway on its far side one day; the board should not.
- The core value holds: the mailer and tosser run on the microcontroller.
  The usual shortcut (binkd and hpt on a Pi, the board as a client of it)
  is exactly what Rob ruled out on 2026-09-29.
- A µnleashed hub needs a binkp server and routing, which is a board with
  a listener: an S3, later, not phase 1.

### 1.7 The sketch, challenged

| Sketch | Verdict |
|---|---|
| Leaf, one uplink, no nodelist | Keep. Everything routes to the hub. |
| binkp outbound polls | Keep. HOLD at the hub; fsxNet accepts Pvt nodes; FidoNet only as a point. |
| CRAM-MD5 with mbedtls MD5 | Keep; measured, already linked. |
| Type 2+ PKT read and write | Keep; type 2+ is what makes point addresses work. |
| ARCmail ZIP bundles | **Move to phase 2.** Inflate costs about 43 KB of heap (`plat::inflateRaw`: the tinfl state, a 32 KB dictionary, 1 KB), too much on a WROOM mid-poll. Mystic hubs send raw PKTs when the node's Archive Type is blank ([Mystic wiki](https://wiki.mysticbbs.com/doku.php?id=config_echomail_nodes)); binkp carries a raw .PKT like any file. Ask the hub for no packer and a Max PKT size. Outbound bundles can be stored (method 0) ZIPs, which need no compressor. |
| Tosser and scanner, the kludges | Keep, plus the charset fold, @-code strip, tear line and origin. |
| MSGID dupe ring on the card | Keep, with forum and number in each entry so REPLY threading uses the same file. |
| **Netmail into mail** | **Change.** `MailRec` is frozen at 564 bytes with a 512-character body and a 20-character `from` (`chat.cpp:175-204`), 64 slots board-wide. Netmail does not fit. Put it in a sysop-only "Netmail" forum, which reuses the foreign-author storage, and a reply there becomes outbound netmail. AreaFix is netmail, so this is how a sysop links areas. Phase 2; phase 1 links areas through the application form. |
| CONFIG fidonet | Keep, one page, below. |
| PF_SD, runner, heap only during a poll | Keep, plus **the single-writer queue**, which the sketch lacks, and the camera boards' explicit PSRAM allocation. |
| 20-35 KB flash | Right for phases 1 and 2 together; phase 1 alone 20-26. |
| 300-600 bytes static | Lower, about 250, if the tags stay in `system.cfg`. |
| (missing) | The forum reader: long bodies, paging, the stack buffer. The deciding cost. |
| (missing) | Sockets: take turns with announce. |

### 1.8 The plan

**Phase 0, any 1.3.x, worth doing without FTN.** Est. 3-5 KB flash,
20-50 bytes static.
- A paged reader: bodies read from the card in slices into `s.compose`,
  drawn through `startPluginList` and `rows()`, so `[More]`, the abort keys
  and the output backpressure come free.
- A body cap parameter on `appendBody`; `kBodyMax` stays 1,728 for local
  posts unless Rob wants longer ones.
- The current segment in the forum header (`seg=`), so a post opens one
  segment.
- The forum write queue on the runner.
- The reserved author id and the `0x01` body prefix, read and shown.

**Phase 1, 1.4.0: fsxNet, one hub, echomail.** Est. 20-26 KB flash, ~250
bytes static, ~20 KB heap during a poll.
- Poll-only binkp with CRAM-MD5 and raw PKTs, HOLD at the hub, hourly by
  default (fsxNet's own minimum).
- Linked forums: a forum's CONFIG sub-page gets an "Echo tag" part, and
  that is the whole link. Start with one or two areas (FSX_GEN, FSX_BBS or
  FSX_RETRO).
- Tosser and scanner as above, the dupe ring, the ASCII fold.
- CONFIG fidonet: switch; own address (21:n/f or a point); hub address,
  host and port; password (masked, 8 upper-case); poll minutes; origin
  line. `FIDO` for status (last poll, result, in and out counts), `FIDO
  POLL` for staff.
- Testing, for Rob's go: a host binkd and hpt in WSL as the hub, run by the
  harness on 127.0.0.1, so the board's code is tested against somebody
  else's mailer and tosser. The cloud can run it.

**Phase 2, 1.4.x.** Est. 6-9 KB flash, ~30 bytes static.
- Netmail through a sysop forum, and an AreaFix helper.
- ARCmail in, heap-gated: only when the 43 KB is there, or on PSRAM boards.
- Retention: delete a whole old body segment once every message in it is
  past the keep date, flagging those records `X`. The index never
  compacts.
- Long messages split into parts.

**Needs Rob's decisions.**
- Whether FTN is core firmware (a PF_SD plugin, off by default, like the
  forums) or waits for the features sat. The 2026-09-29 rule says extras
  go on sats; but the sat interface (EVENT, PUBLISH, GRANT) cannot feed
  the board's forums, so FTN on a sat would mean a second forum store.
  My recommendation: core plugin.
- 1.4.0, or push phase 1 into 1.3.x after ZMODEM.
- Which network first (fsxNet recommended), and applying: Rob's real name
  goes in the nodelist.
- The post UX in linked forums: accounts only, the warning line, the cap.
- ASCII fold now, or wait for per-terminal charset in the reader.

---

## Part 2: ZMODEM

**The claim holds.** Forsberg's
[zmodem.txt](https://gallium.inria.fr/~doligez/zmodem/zmodem.txt), 9.1: "A
ZRPOS header resets the sender's file offset to the correct position. If
possible, the sender should purge its output buffers and/or networks of all
unprocessed output data." The sender seeks the file and sends a fresh
ZDATA at that offset. The receiver throws away everything until that
header. No retransmit window lives in memory; the file is the window.

**Static DRAM, measured and estimated.**
- Measured: `xmodem::Engine g_eng` (`files.cpp:1952`) is 1,188 bytes
  (0x4a4, nm), in .data because of its initialisers. Of that, `data_[1024]`
  and `name_[64]` are 1,088; the other state is 100.
- Estimated: a ZMODEM engine written the same way needs the same 1,024-byte
  subpacket buffer and 64-byte name, plus about 130 bytes of its own (a
  24-byte header staging buffer shared by hex and binary headers, the
  subpacket decoder's CRC and ZDLE state, file, receiver and acknowledged
  offsets, the receiver's buffer length, timers, counters, the CAN queue),
  about 1,220 all in.
- **In a union with the XMODEM engine: 30 to 60 bytes of static DRAM**, 1
  to 2% of the ESP32-CAM's 2,704. One transfer at a time board-wide is
  already the design, so the union costs nothing in behaviour; the two
  classes have constructors, so it is an explicit union with placement new
  at `beginSend`/`beginRecv`.
- As a separate static instead: about 1,220, 45% of the ESP32-CAM's free.
  Do not.
- CRC-16 is XMODEM's (`xmodem::crc16`) and CRC-32 is ZIP's (`crc32.h`):
  both reused, 0 bytes.

**Flash, estimate: 8 to 11 KB.** About 1,000 lines of engine (header codec
in hex, binary 16 and binary 32; ZDLE; subpackets; sender and receiver
state machines) at the XMODEM engine's measured 6.5 bytes a line, 6.5 to
9 KB, plus 1 to 2 KB of glue in files.cpp. Cross-check: zmtx/zmrx, the
code SyncTERM's and Synchronet's ZMODEM grew from, is about 50 KB of C
source in three files and has been [ported to CP/M-80](https://github.com/codesmythe/zmtx-zmrx),
a 64 KB machine. [mbzm](https://github.com/roscopeco/mbzm) (MIT) is an
embedded receiver only and says its error handling is incomplete: a
reference, not a base. SyncTERM's and Synchronet's own ZMODEM is GPL-2.0
([sexyz notes](https://www.synchro.net/docs/sexyz.txt)); read, do not copy.

**The parts that need care.**
- **The socket read gate, load-bearing.** A session's socket is read only
  while its timeline has `BBS_RX_ROOM` (2,600 of 3,072) free, and held
  input is processed under the same test (`bbs.cpp:485`, `:1170`). XMODEM
  never meets it because it stops and waits. A streaming sender keeps the
  timeline full through `xferPump`, so the gate stays shut and the
  receiver's ZRPOS or abort is never read: a line error costs the rest of
  the file. Raw-input sessions must be read whatever the output room,
  since in raw mode input produces no echo. Core change, small, and it
  wants a test that injects an error mid-stream.
- **ZDLE and IAC.** ZMODEM escapes ZDLE, DLE, XON, XOFF and their high-bit
  forms (and CR after `@`), never 0xFF, so `Term::raw`'s IAC doubling is
  still needed after ZDLE encoding: worst case 2 bytes a data byte, which
  `xferPump`'s halving already covers. The telnet binary option
  (RFC 856, from the 2026-09-20 upload bug) is required for the same
  reason XMODEM needed it: a CR padded with NUL is corrupt ZMODEM data.
- **ZCRCW and ZCRCQ.** ZCRCG streams, ZCRCQ asks for a ZACK and carries
  on, ZCRCW waits, ZCRCE ends the frame. If the receiver's ZRINIT gives a
  buffer size (ZP0/ZP1 non-zero), the sender must end a subpacket with
  ZCRCW at least every that many bytes and wait. As a receiver, advertise
  0 (non-stop) and write straight to the file: on a CRC failure, ZRPOS back
  to the last good offset and seek there, and the good data overwrites the
  bad. That also handles senders whose subpackets exceed 1 KB (some go to
  8 KB) without a bigger buffer.
- **Resume.** Download resume is the receiver's choice: it sends ZRPOS
  with its partial file's length and the sender seeks. Free. Upload resume
  would mean keeping half-written files in `.pending`, which is how uploads
  were once refused as duplicates of earlier failures, and a stale partial
  belongs to nobody. As receiver, always answer ZRPOS 0. Say so in the
  help.
- **The card writes stay on the loop, and there are more of them.** An
  upload writes each block from the loop today (CLAUDE.md lists XMODEM's
  card writes as left there knowingly). XMODEM is paced by the round trip;
  ZMODEM is paced by TCP, so the loop writes more often. Measure slow
  passes during a LAN upload with other callers on before shipping. If
  they show, the lever is to hold the socket read while a write is
  pending, which TCP turns into backpressure.
- **What it really buys here.** In flight is bounded by
  `CONFIG_LWIP_TCP_SND_BUF_DEFAULT` and `TCP_WND_DEFAULT`, both 2,880 in
  the generated sdkconfigs. XMODEM-1K has 1,029 bytes in flight a round
  trip; ZMODEM at most 2,880. So about 2.8x on a long link and about the
  same on a LAN. Raising the TCP buffers is the dial, and it is per-socket
  heap; not recommended on this evidence.
- **Auto-start.** Downloads send `rz\r` and ZRQINIT so SyncTERM, Qodem and
  other terminals start by themselves; uploads send ZRINIT and wait for
  the caller to start. Keep `kSenderWaitMs`' three minutes: the person
  finding the file is still the slow part.

**Verdict: in 1.3.0 as planned**, with the RX-gate change named in the
plan and the union measured off the ELF once it exists.

---

## Found on the way

- **Every forum post opens every full body segment**, `forums.cpp:533-545`.
  The loop starts at `M0000` and stops at the first segment under 128 KB,
  so with 20 full segments a post is 21 card opens on the loop, and CLAUDE.md's
  own rule is one open a pass. Invisible on the host, which charges only
  for opens and gives no card latency. It grows with every 128 KB a forum
  holds: slowly with local posts, quickly with imported mail. Fix: keep
  the current segment in the header record (`seg=`), a key the header
  format was built to take. Phase 0; worth doing now regardless.
- **The socket read gate and streaming output** (Part 2) is a property of
  any future raw-mode plugin that streams, not only ZMODEM: a door or a
  bridge that pushes output as fast as it can would starve its own input
  the same way.

## How this was measured

- Static DRAM: `_bss_end` from `xtensa-esp32-elf-nm` on each
  `firmware.elf`, minus 0x3FFB0000, against the `len = 0x2c200` of
  `dram0_0_seg` in each env's generated `memory.ld`. Not PlatformIO's
  percentage.
- Flash per object: summed from the GNU ld map by address range (text at
  0x400D0000 and up, rodata 0x3F400000 to 0x3F800000), a scratch script.
- Symbols (`g_eng`, the MD5 and HMAC functions): `nm -S -C`.
- sdkconfig values: the generated `sdkconfig.esp32dev` and
  `sdkconfig.esp32cam_aithinker`, and the ESP32-CAM's `config/sdkconfig.h`.
- Traffic: message numbers of Synchronet threads on rob.synchro.net, two
  per echo, divided by the days between them. An estimate of the echo's
  volume at one board, not a published count.
- Every other figure marked "est." is reasoned from line counts and the
  calibration above, not measured.

## Sources

- NodelistDB statistics: https://nodelist.fidonet.cc/stats
- fsxNet join: https://fsxnet.nz/fsxnet/join
- fsxNet infopack, fsxnet.txt: https://github.com/fsxnet/infopack/blob/master/fsxnet.txt
- fsxNet infopack, history.txt: https://github.com/fsxnet/infopack/blob/master/history.txt
- fsxNet nodelist (FSXNET.268): https://github.com/fsxnet/nodelist
- fsxNet echo areas: https://fsxnet.nz/fsxnet/echomail
- Vertrauen forum listings: https://rob.synchro.net/?page=001-forum.ssjs&sub=fsx_gen , https://rob.synchro.net/?page=001-forum.ssjs&sub=fsx_bbs , https://rob.synchro.net/?page=001-forum.ssjs&sub=fn_sysop , https://rob.synchro.net/?page=001-forum.ssjs&sub=chat , https://rob.synchro.net/?page=001-forum.ssjs&sub=cbm
- FidoNet Policy 4: https://www.fidonet.org/policy4.txt
- FTS-1026 binkp (and FTS-1027 CRAM): http://ftsc.org/docs/fts-1026.001 (refused a connection on 2026-09-29; content confirmed through the ENiGMA½ PR below and binkd's source)
- FTS-0001 packets: http://ftsc.org/docs/fts-0001.016
- FTS-0009 MSGID and REPLY: http://ftsc.org/docs/fts-0009.001
- binkd source and licence: https://github.com/pgul/binkd
- ENiGMA½ binkp fix, PR #761: https://github.com/NuSkooler/enigma-bbs/pull/761
- Mystic echomail node config: https://wiki.mysticbbs.com/doku.php?id=config_echomail_nodes
- Synchronet BinkIT: http://wiki.synchro.net/module:binkit
- ENiGMA½ FTN: https://nuskooler.github.io/enigma-bbs/messageareas/ftn.html
- WWIV FTN: https://docs.wwivbbs.org/en/latest/network/ftn_nets/
- Maximus NG FidoNet: https://maximusng-bbs.github.io/maximus/fidonet/
- ZMODEM spec, Forsberg: https://gallium.inria.fr/~doligez/zmodem/zmodem.txt
- zmtx-zmrx, CP/M port: https://github.com/codesmythe/zmtx-zmrx
- mbzm: https://github.com/roscopeco/mbzm
- SEXYZ: https://www.synchro.net/docs/sexyz.txt
