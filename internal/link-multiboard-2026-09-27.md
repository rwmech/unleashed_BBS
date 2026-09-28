# One satellite, several boards: design (2026-09-27)

Rob: "pairing can be on multiple boards. Having one camera accessible by 5
boards would rock." Then, with the go: "make it happen and im sure we have
a CONFIG SATS in it", and a SATS command that shows callers what is
safe to show and staff everything.

This is the record of the decisions. LINK.md gets the protocol when it is
built.

## The answer in brief

- A satellite holds **up to 5 pairings** (`kHosts = 5`), each a normal
  pairing with its own ECDH, commit-then-reveal and `k_link`. A board
  needs no change to its own pairing table: a shared satellite is one peer
  of its 8.
- **All of a satellite's boards must be on one Wi-Fi channel**, which in
  practice means one router. The radio cannot be in two places. A board on
  another channel is refused at pairing with a sentence that says why, and
  a board whose router later moves is shown as not heard, with the reason,
  on every board that can see it.
- **The first board paired is the owner.** It owns the camera's settings,
  opens pairing for the next board, and can revoke any board. When the
  owner leaves, the oldest remaining board becomes the owner.
- A SNAPSHOT goes to the board that asked. Timelapse and motion pictures
  go to every board that asked for them (a per-board choice). One capture,
  sent as one unicast transfer per board.
- The satellite queues requests from several boards (2 a board, 8 in
  all) and answers "busy, n ahead of you" when it cannot take another.
  Callers' limits stay per board.

## The satellite's pairing table

- **Five slots**, each: the board's MAC, `k_link`, the board's name (from
  its OFFER or BEACON), the order it was paired in (for the owner), and
  what it asked to receive. About 48 bytes a slot in NVS, 240 bytes for
  five. For deep sleep, each slot's session state (session key, packet
  numbers, replay window, epoch) is about 40 bytes of RTC memory, 200 bytes
  for five, out of the ESP32's 8 KB.
- **The engine on a peer holds N hosts instead of 1.** Today the peer role
  keeps the HELLO and PING state in engine-wide fields and uses host slot
  0. It moves into each host slot (state, HELLO timer, PING timer, misses).
  The channel scan stays engine-wide: there is one radio. The satellite
  rescans only when every host is silent; with some hosts up, it stays on
  their channel.
- **Adding a board:** a satellite with no pairing is in pairing mode after
  boot, as today. A satellite with pairings pairs another board only when
  its owner opens a window (`LINK SHARE n`, or Share on CONFIG sats), for 2
  minutes: `PAIR_OPEN` (sealed, owner only). The new board then runs its
  own `LINK PAIR` and the sysop there checks the code as today. The
  satellite's physical act (IO0 at power-up) still forgets every pairing.
- **Removing a board:**
  - The board itself: `LINK FORGET n` sends `UNPAIR` (sealed) before it
    forgets its side, so the satellite drops that slot. A satellite out of
    reach at the time keeps the slot, and the board's side is gone anyway
    (it cannot open anything under a key it forgot).
  - The owner: `LINK REVOKE n board` sends `REVOKE mac` (sealed, owner
    only), for a board that is gone or not trusted.
  - Everything: the satellite's own physical reset.
- **What each end shows:** the board's LINK and CONFIG sats show the
  satellite and, from `PEERS`, the other boards by name, which one owns
  it, and which have been heard lately. The satellite's console lists its
  boards.

## The radio

- **One channel, and this rules things out.** An ESP-NOW frame goes out
  and is heard only on the channel the radio is on. A board is a station on
  its router and never leaves the router's channel. So a satellite can
  serve only boards that sit on one channel. Two boards on two routers
  that happen to share a channel work; if one router moves, that board
  drops out.
- **Enforced at pairing.** `PAIR_HELLO` carries the satellite's home
  channel (a new byte; 0 when it has no pairing). A board on another
  channel does not answer; it tells its sysop: "This satellite works on
  channel 1 for its other boards and this board is on 6. Boards sharing a
  satellite must be on one Wi-Fi channel, which usually means one router."
- **When a router moves later:**
  - Boards on the same router move together. Every host goes quiet, the
    satellite rescans and finds them all on the new channel. Nothing is
    lost.
  - One board's router moves alone. The satellite keeps serving the
    others and marks that board unheard. The others see it in `PEERS`
    and show "not heard since 14:05". The moved board shows the
    satellite as not answering, "shared with 2 boards; all must be on one
    Wi-Fi channel".
- **Following two channels was considered and left out.** The satellite
  could hop to each board's channel in turn, but a board's requests are
  heard only while the satellite sits on its channel, so every board
  would see its requests lost half the time. That is not something to
  ship. A later option is a slow probe: a satellite with an unheard board
  could look on other channels once a minute (about 260 ms away), to
  tell the others where that board went. Not in 1.2.0.

## Who gets what

- **SNAPSHOT:** to the board that asked, with that board's texts, as
  today.
- **Timelapse and motion:** each board chooses what it receives (Receives:
  timelapse, motion) on CONFIG sats. The satellite drives both by `EVENT`,
  asleep or awake. Each board that wants that kind answers with a SNAP, as
  a single board does today. The satellite collects SNAPs for 300 ms after
  the EVENT, captures once, and sends the same picture to each board that
  answered, as one transfer each. The watermark text comes from the
  owner's SNAP when the owner is among them, else from the first. Each
  board files and describes the picture its own way.
- **The cost:**
  - One XGA picture of about 80 KB is about 0.9 s on the link at link.7's
    measured 85-93 KB/s. Five boards: about 4.5 s of sending, one after
    another, after one capture and one lot of pixel work (about 3 s).
  - A deep-sleeping satellite serving five boards is awake about 7.5 s a
    picture, against about 3.9 s for one. That is the price, and it is
    per event, not per board.
- **Broadcast was considered and rejected.**
  - Every frame is sealed with the receiving board's own key. A
    broadcast would need a key all five share, and any one of them could
    then forge the satellite's frames to the others.
  - ESP-NOW broadcast also has no MAC-layer retry.
  - Five unicast transfers cost about 3.6 s more awake time and keep
    each board's keys its own.

## Concurrency

- **A queue on the satellite:** at most 2 requests a board and 8 in all,
  served round-robin by board, so one busy board cannot starve the rest.
- **Busy answer:** `SNAP_FAIL` with code `CE_BUSY` gains a trailing byte,
  the requests ahead in the queue (0xFF when full), shown to the caller as
  "The garden camera is busy, 2 ahead of you. Try again in a moment."
- **Limits stay per board:** `photos::budget`/`spend` count a caller on
  their own board, across that board's cameras. One board's callers never
  use up another board's allowance.

## Security

- **No board can pose as another.** Each board's frames are sealed under
  its own session key, from its own `k_link`, and the satellite looks the
  key up by source MAC. A board that knows its own keys cannot seal a
  frame another board's key would open. A spoofed MAC with the wrong key
  fails the tag, as today.
- **No board can read another's pictures.** A picture for board B is
  sealed under B's key; board A hears the frames and cannot open them.
- **Only the owner opens pairing and revokes.** A compromised non-owner
  board can remove only itself. A compromised owner can revoke the others
  and add boards. The satellite's physical reset is the answer to that,
  and the doc for sysops says so.
- **Revocation is immediate:** the slot's keys are wiped on the satellite
  when `UNPAIR` or `REVOKE` arrives, and every session with that board ends.
- **`PEERS` tells each board the others' names, MACs and flags.** A board
  paired to a satellite is its sysop's, and staff see this (SATS staff
  view). Callers never do.

## Frames and fields

New types, all in family 0 (LINK):

| Type | Name | Dir | Sealed | Payload |
|---|---|---|---|---|
| 22 | PAIR_OPEN | H→P | yes | `u16 seconds`; owner only; the satellite pairs one more board |
| 23 | UNPAIR | both | yes | empty; forget the other end. H→P: the board lets go. P→H: the satellite tells a board its owner revoked, then forgets it (added while building, so a revoked board stops retrying) |
| 24 | REVOKE | H→P | yes | `mac[6]`; owner only; forget that board |
| 25 | PEERS | P→H | yes | `u8 count`, then per board `u8 flags` (1 owner, 2 up now, 4 this is you), `mac[6]`, `char name[16]` |

Changes to existing messages:

- `PAIR_HELLO` grows to 95 bytes: byte 94 is the satellite's home channel
  (0 with no pairing). A board reads 94 or more.
- CAMERA `SETTINGS` byte 21, once padding, is **what this board wants**
  (bit 0 timelapse, bit 1 motion). A satellite applies bytes 0-20 only
  from its owner. `SETTINGS_OK` echoes what it runs. Its byte 22 bit 0 says
  "you are the owner".
- CAMERA `SNAP_FAIL` may carry a fourth byte, the queue position, with
  `CE_BUSY`.

## The board's side

- **Peers file v2:** a first line `#link-peers 2`. Each line is `mac kind
  checked pairedAt recv camno key name`: `recv` is a hex flag byte (1
  timelapse, 2 motion) and `camno` the camera number, 0 for automatic. A
  file with no header is v1 and reads as before, with the defaults.
  Pairings stay out of the backup.
- **LINK** keeps pairing and diagnostics. It adds `LINK SHARE n` (owner:
  open the satellite's pairing for another board), `LINK REVOKE n board`
  (owner), and the shared view: other boards, owner, heard.
- **SATS** (new, `CF_READ`): the satellites in use.
  - A caller's view: name, type, a plain status (awake, asleep, not
    answering), the last picture's time, and the camera number for
    SNAPSHOT.
  - The staff view (by the NODES/SYS rank rules): the channel, RSSI,
    rate and fallback, MAC, each pairing's key fingerprint (the first 8
    bytes of SHA-256 over `k_link`, never the key), the other boards,
    uptime, firmware, retries and drops, and the awake and sleep schedule.
  - A caller's view contains none of those, which its tests check by
    content.
- **CONFIG sats** (new): the paired satellites, each with a sub-page:
  name, type, status, pair and unpair, what this board receives, and its
  camera number. It links the CONFIG cameras and CONFIG camsat pages, so
  one place finds them all. The tty-ux agent specifies the screens at 40
  and 80 before they are written.

## Cost

- **Board:**
  - Static RAM: nothing per satellite. The peers file's two fields are
    held in the plugin's `Meta`, which is heap while the link is on (+2
    bytes a pairing).
  - Flash: a few KB for SATS and CONFIG sats. Measured at the build.
- **Satellite:**
  - Engine RAM: about 150 bytes a host slot on the ESP32, so about 600
    more for five. It has PSRAM.
  - NVS: 240 bytes. RTC: 200 bytes.
  - The queue: 8 small records.

## What ships when

- **1.2.0:**
  - The engine's N hosts and the new messages.
  - Share, unpair and revoke; the channel refusal and the unheard-board
    reporting.
  - Per-board receives, the queue with the busy answer, and the owner's
    settings.
  - SATS, CONFIG sats and LINK's shared view.
- **Later:** the slow probe for a board whose router moved, and handing
  ownership to another board on purpose.

## N

**5**, as Rob asked. The pairing table is the only thing sized by it on
the satellite (240 bytes of NVS), and the deep-sleep awake time grows about
0.9 s a board, which at five is still under 8 s a picture. Past five, a
motion picture would keep a battery satellite awake over 10 s. For more
boards, the sender is better on mains.
