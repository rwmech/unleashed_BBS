<!--
µnleashed BBS: LINK.md

The µnleashed link: one protocol that lets a board talk to small devices
nearby (a camera satellite, a door box) over ESP-NOW or a serial line.
Also how a plugin kept in its own git repository is built into a board's
firmware.

Copyright 2026 - Robert Mech
License: GNU General Public License v3 or later
SPDX-License-Identifier: GPL-3.0-or-later

Documentation for µnleashed BBS, part of the same distribution as the
source. See the LICENSE file for terms.
-->

# The µnleashed link

Status: **1.2.0, in development.** The design below is settled (Rob,
2026-09-26); where a figure is an estimate it says so, and where a
behaviour of ESP-IDF was checked it names the header or page it was
checked against.

The link is how a board reaches hardware beside it without a cable and
without the router: a camera satellite in the garden, a box of doors on
the shelf. It is one protocol with message families on top, and it lives
in the core, so every board can use it, the bare ESP32-WROOM-32E included.

- [What it carries](#what-it-carries)
- [The radio: ESP-NOW on ESP-IDF 5.3.1](#the-radio-esp-now-on-esp-idf-531)
- [Channels](#channels)
- [Pairing and keys](#pairing-and-keys)
- [Frames](#frames)
- [Sessions, reliability and flow control](#sessions-reliability-and-flow-control)
- [Where the work runs](#where-the-work-runs)
- [Family 0: LINK (control)](#family-0-link-control)
- [Family 1: CAMERA](#family-1-camera)
- [Family 2: DOOR](#family-2-door)
- [The serial transport](#the-serial-transport)
- [Security rules](#security-rules)
- [Budgets](#budgets)
- [On the board: commands, SYS, HARDWARE](#on-the-board-commands-sys-hardware)
- [Plugins in their own repositories](#plugins-in-their-own-repositories)
- [Sources](#sources)

---

## What it carries

| Family | Id | Between | What |
|---|---|---|---|
| LINK | 0 | any | discovery, pairing, hello, heartbeat, acknowledgements |
| CAMERA | 1 | board and a camera satellite | snap requests, JPEG pictures, status, timelapse and motion events |
| DOOR | 2 | board and a door box | a caller handed to a door, bytes both ways, "finished", "time's up" |
| reserved | 3-127 | | future core families |
| plugin | 128-239 | | families registered by plugins, assigned in this file |
| experimental | 240-254 | | anybody's, never in a release |
| invalid | 255 | | never sent |

A family id is assigned by adding a row to this table, the same way a
PROTOCOL.md field is. Two plugins claiming one id is refused at start, by
name, in the console and in `PLUGINS`.

The satellite and the door box are **peers**. The board is always the
**host**. A peer never talks to another peer through the link.

The core side is `src/core/link.*` (the protocol engine, no ESP-IDF and no
BBS in it, so a satellite builds the same file), `src/core/linkcrypto.*`
(AES-CCM, ECDH and HKDF over mbedTLS), the `link` plugin (radio, pairing,
peers, CONFIG and the staff screens), the `doors` plugin (the DOOR family's
host side) and `src/core/photos.*` (filing a picture, shared by the
built-in camera and the camera satellite plugin).

---

## The radio: ESP-NOW on ESP-IDF 5.3.1

Checked against `components/esp_wifi/include/esp_now.h` and
`esp_wifi.h` in the framework this project pins (`framework-espidf@3.50301.0`,
`version.txt` = 5.3.1), and against Espressif's v5.3.1 ESP-NOW page.

- **Payload: 250 bytes a frame.** `ESP_NOW_MAX_DATA_LEN 250`. ESP-NOW v2.0
  (1,470 bytes, `ESP_NOW_MAX_DATA_LEN_V2`) arrived in ESP-IDF 5.4 and is
  not in 5.3.1; a v1 device truncates or drops a v2 frame over 250 bytes.
  The link uses 250 and never more, so a future board on a newer IDF still
  talks to a satellite built on this one.
- **Acknowledgement: MAC layer only.** The send callback reports
  `ESP_NOW_SEND_SUCCESS` "if the data is received successfully on the MAC
  layer", otherwise `ESP_NOW_SEND_FAIL`. That means the far radio got the
  frame, not that the far application took it. The link's own
  acknowledgements (below) cover the second half.
- **One frame in flight at the MAC.** The docs warn that "too short
  interval between sending two ESP-NOW data may lead to disorder of sending
  callback function" and recommend sending the next frame after the
  previous callback returned. The link's sender does exactly that.
- **Callbacks run in the Wi-Fi task**, high priority, on core 0: "do not
  do lengthy operations in the callback function". The link's receive
  callback copies the frame into a ring and returns. Nothing else.
- **Rate: 1 Mbps PHY by default.** Espressif's FAQ measures about 214 kbps
  of real throughput in open air and 555 kbps shielded. At 222 payload
  bytes a frame, a 30 KB VGA JPEG is 136 frames, roughly 1.2 s in open
  air. `esp_now_set_peer_rate_config()` (5.2 and later) can raise it per
  peer; the link leaves the default until a bench measurement says a
  faster rate still reaches the garden.
- **Peers: 20 in the ESP-NOW table.** The link caps itself at
  **8** (`BBS_LINK_PEERS`), and SYS and `LINK` say when that is full.
  **Peers are added unencrypted at the ESP-NOW layer**; the link does its
  own encryption (below). So the ESP-NOW encrypted-peer ceiling
  (`CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM`, default 7, 17 at most, and
  shared with SoftAP clients) does not apply.
- **Broadcast** is used only by discovery and pairing, and neither grants
  anything.
- **Long Range (LR).** `WIFI_PROTOCOL_LR` exists and can be enabled
  alongside 11b/g/n (`esp_wifi_set_protocol(ifx, 11B|11G|11N|LR)`), with
  raw rates of 1/2 and 1/4 Mbps (`WIFI_PHY_RATE_LORA_500K`,
  `WIFI_PHY_RATE_LORA_250K`), and only between Espressif chips. Whether a
  host that is a station on a non-Espressif router keeps its router link
  unchanged with LR added is **not documented and not yet measured**. So LR
  is **off** in 1.2.0 and a later, bench-measured option.

### Why the link encrypts for itself

`esp_now_recv_info_t` in 5.3.1 carries the source address, the destination
address and the radio's receive control. **It does not say whether a frame
was decrypted.** The docs say unencrypted frames are transmitted and
received, and nothing says an unencrypted frame from a MAC registered as an
encrypted peer is dropped. A MAC address is also trivially set by any
sender. With ESP-NOW's own encryption the board could not tell a sealed
frame from a forged one in the clear.

So the link seals every session frame itself with **AES-128-CCM**, the
header authenticated as associated data. One mechanism, checked by our own
code on every frame: a frame that does not open is dropped, whatever MAC it
came from. mbedTLS does the work, with the ESP32's AES hardware
(`CONFIG_MBEDTLS_HARDWARE_AES=y`).

---

## Channels

- **The host never changes channel.** It is a station on the router, and
  ESP-NOW "must be the same as that of the connected AP" (Espressif FAQ).
  Peers are added with `channel = 0`, "the current channel", so the host's
  sends follow the router without the link doing anything.
- **Peers follow the host.** A peer is not joined to any access point, so
  it may set its own channel with `esp_wifi_set_channel()`. It finds the
  host by scanning:
  1. try the last channel that worked, then 1 to 13 in order;
  2. on each, send a `DISCOVER` broadcast and listen 60 ms;
  3. the host answers with a unicast `BEACON` that carries its channel;
  4. stay there.
  A full sweep is under a second. Channel 14 (Japan, 11b only) is not
  scanned.
- **Every frame from the host carries the channel** in its `chan` byte
  (`esp_wifi_get_channel`). A peer that sees a different number from the
  one it is on moves at once.
- **When the router hops**, the host sees `WIFI_EVENT_HOME_CHANNEL_CHANGE`
  (present in 5.3.1, "doesn't occur when scanning"), logs it and counts it.
  It cannot warn its peers, because they are still on the old channel.
  They notice by silence: a peer sends `PING` every 5 s and rescans after
  three missed `PONG`s (15 s). A door caller sees a pause of up to that long
  while the box finds the host again; nothing is lost, because every
  unacknowledged frame is sent again.
- While the host is itself reconnecting to the router its channel wanders.
  Sends then fail with `ESP_ERR_ESPNOW_CHAN` or at the MAC, are counted,
  and are retried by the ordinary retry rules.

---

## Pairing and keys

A peer is paired by two physical acts, one at each end, inside a short
window. Nothing is paired from the air alone.

**At the host:** the sysop types `LINK PAIR` (or picks "Pair a device" on
CONFIG link). The window is open for 2 minutes, and one pairing at a time.

**At the peer:** a peer with a button holds it for 3 s. A peer without one
(the ESP32-CAM has only RESET) is in pairing mode for 5 minutes after any
boot **while it holds no pairing**; to pair it again it is told to forget
its pairing by a physical act its own firmware documents (for camsat: IO0
held to ground at power-up). Physical access to the peer is ownership of it.

**The exchange** (family 0; broadcast until the host answers, then unicast):

1. peer to host, `PAIR_HELLO`: its kind (`camsat`, `doorbox`), name,
   firmware version, link protocol version, a 16-byte nonce and an
   ephemeral ECDH public key (P-256, 65 bytes uncompressed).
2. The host, inside its window, shows the sysop:
   `Pair camsat "garden" 24:6f:28:aa:bb:cc, code 4821? (y/N)`.
3. On Y, host to peer, `PAIR_OFFER`: the host's ephemeral public key,
   its own nonce, the board's name and its channel.
4. Both sides compute the ECDH shared secret and derive with HKDF-SHA256
   (salt: host MAC, peer MAC, both nonces; info `"unleashed link 1"`):
   - `k_link`, 16 bytes: this pairing's long-term key;
   - the 4-digit code: two more bytes of the same output, mod 10000.
5. host to peer, `PAIR_DONE`, and peer to host, `PAIR_ACK`: each is a
   sealed empty message under `k_link` (packet number 0, each direction
   once), so each side proves it holds the same key. Then both store the
   pairing.

The code in step 2 is printed by the peer on its serial console too. A
sysop who has the console can compare them and so rule out anybody in the
middle; a sysop who has not is trusting that nobody else answered inside a
2-minute window they were standing beside, which is the same trust as
pressing WPS. `LINK` shows `checked` or `unchecked` beside each pairing,
set by the sysop's answer to `Codes match? (y/N)`.

All of the cryptography is mbedTLS code the WROOM image already links for
WPA3 (checked in the 1.1.1 ELF: `mbedtls_ecdh_compute_shared`,
`mbedtls_ecp_mul`, `mbedtls_md_hmac_*`, `mbedtls_ccm_*`, `mbedtls_sha256`).
`CONFIG_MBEDTLS_HKDF_C` is off in the build, so HKDF is written over the
HMAC that is there (RFC 5869, a dozen lines, checked against its test
vectors).

**Where pairings live:** `<userdata>/p/link/peers`, the link plugin's own
folder, one line per peer (MAC, kind, checked, paired-at, `k_link` in hex,
name), written through a temp file and a rename within the partition. It is **not** in the backup zip, the same as `wifi.last`: a
restored board pairs its devices again.

**Forgetting** (`LINK FORGET n`) deletes the ESP-NOW peer and the block. The
peer finds out when its `HELLO` is no longer answered, and goes back to
pairing mode only by its own physical act.

---

## Frames

One layout on every transport. Little-endian throughout.

```
 off  size  field    meaning
   0     1  ver      protocol version, 1
   1     1  family   0 LINK, 1 CAMERA, 2 DOOR, ... (table above)
   2     1  type     message type inside the family
   3     1  flags    0x01 REL  the sender wants this acknowledged
                     0x02 SEC  the payload is sealed and an 8-byte CCM tag follows it
   4     2  session  0 for the link itself; see Sessions
   6     2  seq      message number in this session and direction
   8     2  frag     fragment index, 0 .. nfrag-1
  10     2  nfrag    fragments in this message, 1 for a single frame
  12     1  len      payload bytes in this frame
  13     1  chan     sender's Wi-Fi channel (the host's home channel), 0 on serial
  14     2  crc      CRC-16/CCITT-FALSE over bytes 0-13, 16-19 and the plaintext payload
  16     4  pn       packet number: this frame's number in its direction, never repeated under a key
  20   len  payload  plaintext, or ciphertext when SEC
20+len   8  tag      when SEC: the AES-128-CCM tag
```

- **Sealing (SEC).** AES-128-CCM, 8-byte tag, 13-byte nonce:
  `dir (1) || pn (4, little-endian) || 8 zero bytes`, where `dir` is 0x48
  ('H') host to peer and 0x50 ('P') peer to host. The 20-byte header is
  the associated data, so the family, session, sequence, fragment and
  length are authenticated with the payload and cannot be moved from one
  frame to another. The key is the session key (below); a frame is sealed
  once and a retransmission is a new frame with a new `pn`, so a nonce is
  never reused under a key.
- **Payload at most 222 bytes** on the radio (250 - 20 - 8). On serial the
  limit is the same, so a frame can move between transports unchanged.
- **Which frames are sealed.** Everything once a session key exists. Six
  types come before one and go in the clear: `DISCOVER`, `BEACON`,
  `HELLO`, `HELLO_ACK`, `PAIR_HELLO` and `PAIR_OFFER`. They carry nonces,
  versions, a public key, the board's name and its channel, which the
  router's beacons already show. None of them grants anything, and a
  receiver ignores every other type that arrives unsealed from a radio
  peer.
- **The CRC-16** is on every frame. On serial it is the integrity check;
  on the radio it is redundant with the tag and cheap, and it catches our
  own bugs (a frame assembled from the wrong buffer) before the tag does.
- **A multi-fragment message** starts fragment 0's payload with an 8-byte
  preamble: `u32 total` (message length in bytes) and `u32 crc32` (IEEE,
  the one in `src/core/crc32.h`) over the whole message. The receiver
  learns the size before the first byte of data, so it can refuse a
  message too big for it at once rather than half way.
- **Checks, in order, before anything else looks at a frame:** length at
  least 20 (+8 with SEC), `ver` known, `len` consistent with the frame
  length, `frag < nfrag`, sealed if it must be, the tag, the `pn` replay
  window, then the CRC. The first failure drops the frame and counts it
  under its reason.

---

## Sessions, reliability and flow control

- **Link session.** A peer that comes up sends `HELLO` with a fresh 16-byte
  nonce. The host answers `HELLO_ACK` with its own. Both then use
  `k_sess = HKDF-SHA256(k_link, salt = nonce_peer || nonce_host,
  info = "sess")`, 16 bytes, and packet numbers start at 1 in each
  direction. A frame recorded before a reboot never opens after one.
- **A new key does not displace a working one until it is proved.** The
  host keeps the old session key alongside a pending new one, and switches
  only when a sealed frame opens under the new key. A `HELLO` anybody can
  send in the clear therefore cannot cut a working peer off.
- **Replay window, per peer and direction:** a `pn` is accepted if it is
  above the highest seen, or among the 64 below it and not yet seen.
- **Sessions** carry one conversation: one snap, one door caller. The id
  is 16 bits; the top bit says who opened it (0 host, 1 peer), so the two
  ends never pick the same one. Session 0 is the link itself (heartbeats,
  acknowledgements).
- **Message order and duplicates.** Each session and direction numbers
  its messages with `seq`. A message is delivered once, in order; a
  duplicate (its ack was lost) is acknowledged again and not delivered
  twice.
- **Acknowledgement.** A frame with REL is answered by family 0 `ACK`:
  session, the highest `seq` delivered in order, and for a multi-fragment
  message the highest fragment received in order plus a 32-bit bitmap of
  the fragments after it, and the receiver's `win`. Acks go out at most
  20 ms after the frame they answer.
- **Retries.** An unacknowledged frame is sent again (as a new frame, new
  `pn`) after 150 ms, doubling to 1.2 s, 8 tries in all. Then the session
  is reset with `RESET` and its owner told: a transfer is abandoned, a door
  caller taken back.
- **Window.** A sender may have at most `win` fragments (bulk) or messages
  (streams) unacknowledged. The receiver states `win` in `HELLO`,
  `HELLO_ACK` and every `ACK`, and states 0 when it has no room, which is
  how a slow caller's terminal throttles a door box. Default 16 fragments
  for bulk on a board without PSRAM (3.5 KB of buffer), 64 on one with it.
- **Order.** Fragments of a bulk message are written in order into the
  receiver's window and handed on as an in-order stream. With one frame in
  flight at the MAC, reordering only follows a loss and a retransmission,
  and the SACK bitmap resends only what is missing.

---

## Where the work runs

Rule no. 1 decides every line of this: nothing the link does may stall a
caller who is not using it.

| Where | What | Bound |
|---|---|---|
| Wi-Fi task, core 0 (receive callback) | length check, copy the frame into the receive ring, count a drop if full | one copy of 250 bytes |
| Wi-Fi task (send callback) | set "the last frame is done", with its result | an atomic store |
| BBS loop, core 1 (the link plugin's tick, every 20 ms) | every frame: open (CCM), replay window, CRC, ack; a single-frame message goes to its family (door bytes into the caller's timeline, control); a bulk fragment's plaintext goes into the bulk window; hand the radio its next frame | 8 frames a pass |
| background runner (1.1.2) | bulk messages: take the window's fragments in order, the message CRC-32, and the family's sink (a card file for a picture); the pairing arithmetic (P-256) | a runner job, `breathe()` between blocks |

Single-frame messages stay on the loop on purpose: a door keystroke is one
frame, and queueing it behind a SCREENS walk on the runner would put a
second of lag between a caller and their door. Reassembly, which is where
buffers and card writes live, is always on the runner and never on the
loop. The loop opens every frame, bulk ones included, so the keys, the
replay window and the sessions have one owner and no locks; the runner
only ever sees plaintext in the window, through two atomics (how far it has
taken, and which slots are full).

**The cost of opening a frame.** mbedTLS's own CCM runs the cipher a block
at a time, and on the ESP32 each block is one trip to the AES peripheral,
lock and all: the camsat bench measured about 320 us for one 222-byte frame
on an ESP32 and 1,000 us on an S3 (2026-09-26). linkcrypto builds the same
CCM from one CBC call for the MAC and one CTR call for the keystream, which
hold the peripheral once each; it is byte for byte mbedtls_ccm's (checked
against RFC 3610 packet vector 1 and against mbedtls_ccm for every payload
length a frame can have). Measured on the S3 in the BBS's own -Os build
with hardware AES, a 222-byte frame with its 20-byte header: **seal 78 us,
open 78 us**, against 963 us for mbedtls_ccm in the same image (camsat
bench, 2026-09-26). Under Rob's 100 us line, so the link stays on the loop.
The ESP32's figure and `Engine::poll`'s per frame (from `LINK`) are still to
be read on the bench.

**The loop's per-frame cost is measured on the bench and has a limit**
(Rob, 2026-09-26): if opening and dispatching one frame costs more than
about 100 µs, or the single-frame ring grows under load, the link moves
that work to a task of its own. `LINK` shows the average and worst
per-frame time and the ring's high-water mark, so the answer is read off
the board rather than argued.

The rings and windows are allocated when the link starts, from PSRAM on a
board that has it and the heap otherwise, and freed when it stops. Nothing
is allocated per frame, and nothing at all while the link is off.

The runner interface used is the one on rel-1.1.2a (`src/core/runner.h`):
a `runner::Job` per kind of work, `post`, `done`, `collect`, `breathe`.
The link owns one job, "link", posted when the bulk window has fragments
waiting or pairing wants its arithmetic, and the job is idle. The engine's
pairing state stays the loop's: `pairTake` copies what the arithmetic needs
into a job, `pairRun` does it anywhere, `pairGive` puts the answer back.
On a tree without the runner (the link branch before the 1.1.2 merge) the
plugin calls the same work from its tick; `__has_include("core/runner.h")`
picks, so the merge needs no edit here.

---

## Family 0: LINK (control)

| Type | Name | Dir | Sealed | Payload |
|---|---|---|---|---|
| 1 | DISCOVER | P→H broadcast | no | peer MAC; the host answers only its own peers, once a second each |
| 2 | BEACON | H→P | no | host channel, board name, link protocol version |
| 3 | HELLO | P→H | no | nonce, protocol version, kind, firmware version, families spoken, `win` |
| 4 | HELLO_ACK | H→P | no | nonce, `win` |
| 5 | PING | P→H | yes | uptime, free heap, RSSI of the host as the peer hears it |
| 6 | PONG | H→P | yes | unix time (0 when the host has no NTP), host channel |
| 7 | ACK | both | yes | session, seq delivered, frag delivered, 32-bit SACK, `win` |
| 8 | RESET | both | yes | session, reason code |
| 16 | PAIR_HELLO | P→H broadcast | no | kind, name, versions, nonce, ECDH public key |
| 17 | PAIR_OFFER | H→P | no | ECDH public key, nonce, board name, channel |
| 18 | PAIR_DONE | H→P | yes, `k_link` | empty |
| 19 | PAIR_ACK | P→H | yes, `k_link` | empty |

---

## Family 1: CAMERA

A camera satellite is a small ESP32 with a camera and no card. It takes a
picture when the host asks, on its own timer, or on motion, and the host
files it in Photos exactly like a snap from a built-in camera: the host
names the file, applies the Photos limits and keeps the record. The
satellite never chooses a path.

| Type | Name | Dir | Payload |
|---|---|---|---|
| 1 | SNAP | H→P | request id, frame size, quality (10-63), flash (off, on, auto), reason (caller, timelapse, test) |
| 2 | PICTURE | P→H, bulk | request id (0 when the satellite started it), reason, width, height, taken-at (unix, 0 if unknown), then the JPEG |
| 3 | SNAP_FAIL | P→H | request id, reason code (no sensor, no memory, busy, flash fault) |
| 4 | STATUS | P→H | sensor model, largest frame size, free heap and PSRAM, uptime, last error; sent after HELLO and every 60 s |
| 5 | EVENT | P→H | motion or timelapse, taken-at; always followed by its PICTURE |
| 6 | SETTINGS | H→P | timelapse interval (0 off), motion on/off, motion hold-off seconds, default frame size and quality, flash |
| 7 | SETTINGS_OK | P→H | what it is now using (it may clamp) |

- The host is the source of truth for the satellite's settings and sends
  SETTINGS after every HELLO, so a satellite that rebooted or was swapped
  picks them up without a UI of its own.
- A PICTURE bigger than the host's limit (512 KB, or less if the card is
  short of room) is refused at fragment 0 with RESET, before it is sent.
- On the host, the picture streams through the runner into a temp file on
  the card and is filed into Photos by `photos::` (the code the built-in
  camera uses too) only when the message CRC-32 checks. `FILES.BBS` has one
  writer, the files plugin, and filing goes through it. A board with no
  card mounted refuses PICTURE; the camera satellite plugin is `PF_SD` for
  that reason.
- The camera satellite's BBS side is **in every official image, off by
  default**, the base WROOM included (Rob): any board can add a camera.
- A caller's snap from a satellite shows the camera's usual spinner while
  it waits, and the same limits per handle apply.

---

## Family 2: DOOR

A door box is a device that runs doors: games and utilities a caller is
handed to and comes back from. The BBS stays in charge of the caller the
whole time. Several callers can be in one box at once, each in their own
session. The host side is the core `doors` plugin; door boxes live in
their own repositories.

| Type | Name | Dir | Payload |
|---|---|---|---|
| 1 | LIST | P→H | the doors it offers: id, name (24), players per door, total sessions it can hold |
| 2 | OPEN | H→P | the door id and the handoff line (below) |
| 3 | OPEN_OK | P→H | |
| 4 | REFUSED | P→H | reason: full, unknown door, not now, with a short text shown to the caller |
| 5 | DATA | both | bytes: the caller's keys one way, the door's output the other |
| 6 | RESIZE | H→P | cols, rows (the caller's terminal changed size) |
| 7 | WARN | H→P | minutes left (sent at 5 and 1) |
| 8 | TIMEUP | H→P | the caller's time is over |
| 9 | FINISHED | P→H | exit code, optional one-line text for the caller |
| 10 | CLOSE | H→P | the caller has gone: 1 hung up, 2 time's up, 3 taken back, 4 the board is closing the doors |
| 11 | LIST_ASK | H→P | send LIST on this session (the board asks whenever a box comes up) |

Byte layouts for both families are in `src/core/linkfam.h`, which the
board and a peer both build, with every multi-byte field little-endian:

- CAMERA SNAP: `u16 req, u8 size, u8 quality, u8 flash, u8 reason`.
- CAMERA PICTURE: a 16-byte header (`u16 req, u8 reason, u8 0, u16 width,
  u16 height, u32 takenAt, u32 0`), then the JPEG.
- CAMERA SNAP_FAIL `u16 req, u8 code`; STATUS `u8 sensor, u8 maxSize,
  u32 heap, u32 psram, u32 uptime, u8 lastErr, u8 0, char model[12]`;
  EVENT `u8 kind, u32 takenAt`; SETTINGS and SETTINGS_OK `u16 timelapseMin,
  u8 motion, u16 holdoffS, u8 size, u8 quality, u8 flash`.
- Frame sizes are the link's own numbers: 0 the satellite's default, 1
  QQVGA, 2 QVGA, 3 VGA, 4 SVGA, 5 XGA, 6 SXGA, 7 UXGA.
- DOOR LIST: `u8 sessions it holds, u8 count`, then per door `u8 id,
  u8 players, char name[24]` (8 doors at most in one frame).
- DOOR OPEN: `u8 door id`, then the handoff line. REFUSED and FINISHED:
  `u8 code`, then up to 60 characters for the caller.

### The handoff line

The first thing a door gets is one line of plain ASCII, the door's
equivalent of `DOOR.SYS`:

```
UNLEASHED-DOOR 1 node=3 session=32770 handle=Big+Dave rank=user cols=40 rows=25 term=pet40 minutes=42 board=The+Rusty+Antenna
```

- Words separated by single spaces, `key=value` after the first two.
  A space inside a value is `+`. Handles and board names may contain
  spaces but never `+` or `=` (the board replaces either with `-` in a
  board name), so this is lossless.
- `rank`: `guest`, `user`, `co2`, `co1`, `sysop`. It is information for
  the door (a high-score table might mark staff), never permission for
  anything on the board.
- `term`: `ascii`, `ansi`, `pet40`, `pet80`. `cols` and `rows` are what the
  board measured; a door does not probe again.
- `minutes` is the time left on this call when the door opened. The board
  enforces it, not the door.
- Unknown keys are ignored, so the line can grow.

A door author reads it like this:

```python
def handoff(line):
    words = line.strip().split(" ")
    if words[0] != "UNLEASHED-DOOR":
        raise ValueError("not a handoff line")
    info = {"version": int(words[1])}
    for word in words[2:]:
        key, _, value = word.partition("=")
        info[key] = value.replace("+", " ")
    for key in ("node", "session", "cols", "rows", "minutes"):
        info[key] = int(info[key])
    return info
```

### How a caller goes in and comes back

- Entering: the board owns the session (`Bbs::own`), turns raw input on
  (`Bbs::setRawInput`), sends OPEN, and waits up to 5 s for OPEN_OK
  (spinner showing). REFUSED or silence puts the caller back where they
  were with the reason.
- In the door: the caller's bytes go out as DATA; the door's DATA go
  straight into the caller's output buffer. When that buffer is short of
  room the board advertises `win = 0` and the box waits. Nothing the door
  sends is ever read by the board as input or as a command.
- Coming back, three ways:
  - the door sends FINISHED: the board takes the caller back at once;
  - time runs out: TIMEUP, then a 10 s grace for the door to save, then
    the board takes the caller back whether or not FINISHED came;
  - the caller hangs up: CLOSE, and the session ends.
- If the box goes silent (no ACK within the retry rules, or the peer
  drops), the board takes the caller back with "The door has gone away."
  and sends CLOSE when it next hears from the box.
- The session number the board uses to track the door is kept in
  `Session::ownerData`, which is the owning plugin's scratch word, so the
  door framework adds nothing to the `Session` struct.

---

## The serial transport

The same frames on a UART, so a door box can sit on a cable where there is
no radio, or be a PC or a Raspberry Pi with a USB serial adapter.

- Each frame is COBS-encoded and ends with a single 0x00. A reader resyncs
  on the next 0x00 after any error.
- Header, CRC-16, sessions, acknowledgements and windows as on the radio.
  `chan` is 0 and `pn` still counts, for duplicate detection.
- **Trusted by the wire: no pairing and no sealing** (Rob). A serial box
  is on a port the sysop configured, the same as the serial bridge:
  whoever can plug into the board's UART already has the board. SEC is
  never set on serial in 1.2.0.
- One serial peer per UART, and UART2 is shared with the serial bridge
  plugin: one owner at a time, refused by CONFIG like any other pin
  clash. Many callers still share that one box, because sessions are
  multiplexed. That removes the "one serial port is one caller" limit the
  doors entry in CLAUDE.md recorded.
- 115200 baud, 8N1 by default; set in CONFIG link.

---

## Security rules

The link moves data. It never moves authority.

- **Nothing a peer sends grants a caller anything.** No frame creates an
  account, changes a level, runs a command, elevates to staff, extends
  time, or chooses which caller it talks to. `rank` in the handoff line
  goes out, never comes back.
- **The board opens every door session.** A box can refuse one, finish
  one, or send bytes into one; it cannot start one or reach a caller it
  was not handed.
- **Time is the board's.** TIMEUP is followed by the board taking the
  caller back after the grace period, whatever the box does.
- **Door output is output.** It goes to the caller's terminal and is never
  parsed by the board as keys, commands or codes.
- **Pictures are data.** The board names the file, checks the size before
  accepting it, writes to a temp name and files it only when the CRC-32
  checks. A satellite cannot write anywhere but Photos, and only through
  the board.
- **On the radio, sealed or nothing.** Past the six clear types, a frame
  that is not sealed, or does not open, is dropped, whatever MAC it shows.
  PAIR_HELLO is only looked at while a sysop's window is open. Every drop
  is counted by reason in `LINK`.
- **Replays fail**: a packet-number window under a key made fresh at every
  HELLO, and a new key used only once it is proved.
- **Keys** are stored on `userdata`, never shown on any screen, never in a
  backup, and never logged.

---

## Budgets

The link is compiled into every image and costs nothing but flash until a
sysop turns it on.

Measured off the esp32dev build of the link branch (`1.2.0-link.2`,
2026-09-26), per object from the linker map:

| | WROOM (no PSRAM) | Boards with PSRAM |
|---|---|---|
| Static DRAM | **142 bytes**: the family table and pointers (link 65, doors 41, the radio 32, ESP-NOW 4). `_bss_end` 0x3ffd7750, 161,616 of 180,736: **19,120 free** | the same |
| While on (heap) | about 15 KB: the engine and its tables 7.7 KB, the bulk window 3.5 KB, the receive ring 2 KB, the doors' table 1.4 KB, the rest 0.5 KB (the host's 64-bit figures; the ESP32's pointers are half the size) | the same, from PSRAM |
| While off | 0 | 0 |
| Flash | **34,293 bytes**: the engine and the link plugin 21,113, doors 3,844, the CCM, HKDF and ECDH glue 1,492, the radio 1,240, ESP-NOW's library 6,604. The ECDH, CCM, HMAC and SHA-256 code under them was already in the image for WPA3 | about the same |
| Per session | 0: door state sits in `Session::ownerData` | 0 |

The WROOM image is 1,267,544 bytes, 80.6% of its slot, with the link and
doors in. The camera satellite plugin is measured when it exists
(unleashed_camsat).

---

## On the board: commands, SYS, HARDWARE

- **CONFIG link** (sysop): Enabled and the levels, the rows every plugin
  has. Off as shipped. Pairings are managed with the commands, not the form.
- **`LINK`** (staff): one row a pairing: number, name, kind, up/down, RSSI,
  and at 60 columns and wider when it was last heard and whether its code
  was checked; then frames in and out, retries, drops by reason, the time
  the loop spends on a frame (average and worst) and the receive ring's
  high-water mark.
- **`LINK PAIR`** (sysop): opens the 2-minute window and asks the pairing
  question on the sysop's own screen.
- **`LINK FORGET n`**, **`LINK NAME n name`** (sysop).
- **`DOORS`** (users): the doors the boxes on the air offer, numbered;
  **`DOORS n`** goes through one. Ctrl-] three times in a row always comes
  back.
- **SYS**, one line in the network section:
  `Link      on, ch 6, 2 of 3 up, 0 drops`, and `, peers full` when all
  8 pairings are taken.
- **HARDWARE**: `Radio link  ESP-NOW 1.0, 250 B frames, 8 peers`.
- The console logs pairing, a peer coming up or going down, a channel
  change and each reset session, never a key.

---

## Plugins in their own repositories

Rob: "each plugin will get its own repo". A plugin kept in a separate git
repository is built into a board's firmware from source, at a pinned
version, with no edit to the core. The first is **unleashed_camsat**: the
satellite's own firmware and the camera satellite plugin for the board,
in one repository.

### Layout of a plugin repository

```
unleashed_camsat/
  unleashed-plugin.ini     the manifest
  bbs/                     sources built into the BBS firmware
    camsat.cpp
    camsat.h
  firmware/                anything else (here: the satellite's own PlatformIO project)
  LICENSE
```

```ini
; unleashed-plugin.ini
name        = camsat                 ; a-z 0-9 _, also its [plugin:camsat] section
version     = 1.0.0
api         = 1.0                    ; the core plugin API it was written against
descriptor  = kCamsatPlugin          ; the const Plugin it defines
requires    = link                   ; core features it needs (link, sd, camera...)
boards      = *                      ; or a list: esp32dev ws_s3_lcd147
license     = GPL-3.0-or-later
```

### Choosing plugins for a build

A board's PlatformIO environment names its external plugins:

```ini
[env:ws_s3_lcd147]
custom_ext_plugins = camsat
```

and `plugins.lock` at the top of the core repository pins each one, a
line a plugin (`name  source  commit`):

```
camsat  https://github.com/rwmech/unleashed_camsat  3f2a9c1e...(the full 40-character commit)
```

`tools/plugins.py fetch` clones each locked plugin into `ext/<name>/`
(ignored by git) at exactly that commit, and refuses a checkout whose
commit does not match the lock. `tools/pio_plugins.py`, a PlatformIO
pre-script on every environment, runs it and passes the list on, so
`pio run -e ws_s3_lcd147` needs nothing typed beforehand. A plugin under
development can have a local path as its source, and `-` as its commit to
take that folder's working tree as it is; a release refuses both.

### How it is compiled and registered

- `src/CMakeLists.txt` reads the list (a file the pre-script writes in the
  build directory, listed as a configure dependency so a changed list
  reconfigures; or `BBS_EXT_PLUGINS` in the environment), adds
  `ext/<name>/bbs/*.cpp` to the core component's sources, and writes
  `ext_plugins.h` into the build directory. It defines
  `BBS_EXT_PLUGIN_COUNT` and `BBS_EXT_PLUGINS(X)`, which calls `X` once
  for each plugin's descriptor, as the manifest names it.
- `src/config.h` includes `ext_plugins.h` when there is one and counts the
  plugins into `BBS_MAX_PLUGINS`; `src/plugins/registry.cpp` expands
  `BBS_EXT_PLUGINS` twice, to declare the descriptors and to list them.
  So a new plugin changes no file in the core, and the `static_assert` on
  the table still catches an overflow.
- A plugin's sources include the core's headers from `src/`
  (`#include "core/plugin.h"`), which is on the include path for them.
- The host build does the same from `host/Makefile`:
  `make EXT="hello" bbs_host_ext` builds a host board with the named
  plugins beside the ordinary `bbs_host`.
- A plugin from its own repository runs like a shipped one, with one
  difference the core enforces: only a shipped plugin (`PF_CORE`) may keep
  files on the board's flash. One that wants storage declares `PF_SD` and
  keeps it on the card; one that wants flash storage without `PF_SD` is
  not started, and says why.

`tools/testplugin/` is the smallest such plugin ("hello", one command) and
the template for the next. `tools/test_ext_plugin.sh` proves the path end
to end on the host: it makes a git repository of the template, locks it,
fetches it, builds it in, sees it start, and checks the refusals (a local
path in a release, a commit the repository does not have, a plugin that
needs a newer core).

Why this and not the alternatives:

- **Not the ESP-IDF component manager.** The project has one
  `src/idf_component.yml` for every environment, because a per-board
  manifest made the component manager delete and refetch on every switch
  of environment and PlatformIO then built from a stale file list (1.1.0,
  esp32-camera). A plugin needs the core's headers and the core needs the
  plugin's descriptor, which is a component dependency cycle. And the host
  build has no component manager at all.
- **Not self-registration through a linker section or a constructor.**
  An object file nothing refers to is not pulled out of a static library,
  so it would need `WHOLE_ARCHIVE` or `-u symbol`, plus a linker fragment
  to keep a custom section on the ESP32, and something different again on
  the host. A generated list is explicit, ordered, identical on both
  builds, and checked by the compiler.

### Versions

- `src/core/plugin.h` defines `BBS_PLUGIN_API_MAJOR` and
  `BBS_PLUGIN_API_MINOR`. The minor number goes up whenever something is
  appended to `Plugin` or a `Bbs` method plugins use is added, which is
  the only kind of change the descriptor rules allow. The major number
  goes up only for a break, which the append-only rule exists to prevent.
- A plugin states what it needs in its source:
  `UNLEASHED_PLUGIN_API(1, 0);`, a `static_assert` that the major matches
  and the minor is at least that, so a plugin too new for the core fails
  to compile with a sentence saying why, not with a positional field
  error.
- `tools/plugins.py` checks the manifest's `api` against the core before
  anything is compiled. On the board, `PLUGINS` lists an external plugin
  with its version like any other.

### Releases

`tools/release.py` builds each release environment with its locked
plugins, fetched with `tools/plugins.py --release`, which:

- refuses a plugin whose checkout is not the locked commit, or whose lock
  entry is a local path or a working tree;
- refuses a manifest licence, or a source file's SPDX line, that cannot go
  into the GPL-3.0-or-later firmware, and a source file with no SPDX line;
- refuses a copyright or licence line naming Anthropic or Claude, the check
  the core gets;

and then writes each plugin's name, version and commit into the release's
`release.txt` (`plugin <family> <name> <version> <commit>`), so what went
into an image is on record. `version.txt` stays one line, the version, as
the directory's fetcher reads it.

---

## Testing

- `host/test_link.cpp` (`make test` runs it): both ends of the engine
  through a simulated radio that loses, duplicates and reorders frames and
  on which the host changes channel; pairing, a No, messages both ways in
  order and once each, a 30 KB bulk message through a lagging runner, a
  refused one, a receiver with no room, forgeries, replays, a forged HELLO,
  a peer that goes away and one that comes back new. The wire against a
  published CRC-16, CCM against RFC 3610, HKDF against RFC 5869. Clean
  under ASan and UBSan.
- `host/linkpeer`: a pretend door box (Echo and Clock) on the host board's
  UDP radio, built from the same engine. `tools/harness.sh` switches the
  link and doors on and gives the board and the box a port each;
  `--only=radio` runs `test_radio_link` (pairing through LINK PAIR, the
  codes, LINK, SYS) and `test_doors` (the handoff line, keys and output,
  FINISHED, Ctrl-], a caller hanging up in a door).
- `tools/test_ext_plugin.sh`: plugins in their own repositories, above.
- On the bench, still to do: the frame cost against the 100 us line, a
  picture from a real satellite, and whether 5.3.1 delivers an unsealed
  frame from a MAC it knows (the design does not depend on the answer).

---

## Sources

- ESP-IDF 5.3.1 headers in the pinned framework:
  `components/esp_wifi/include/esp_now.h` (frame length, peer limits,
  receive info, send and rate calls), `esp_wifi.h`
  (`esp_wifi_set_channel`, `esp_wifi_set_protocol`,
  `.espnow_max_encrypt_num`), `esp_wifi_types_generic.h`
  (`WIFI_PROTOCOL_LR`, `WIFI_PHY_RATE_LORA_*`,
  `WIFI_EVENT_HOME_CHANNEL_CHANGE`), `components/esp_wifi/Kconfig`
  (`ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM`), `examples/wifi/espnow`.
- [ESP-NOW, ESP-IDF v5.3.1](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-reference/network/esp_now.html)
- [Wi-Fi driver, Long Range, ESP-IDF v5.3.1](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-guides/wifi.html)
- [ESP-FAQ, ESP-NOW](https://docs.espressif.com/projects/esp-faq/en/latest/application-solution/esp-now.html)
  (default rate, measured throughput, the channel of the connected AP)
- [ESP-NOW, ESP-IDF latest](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html)
  (v2.0 and 1,470 bytes, and what a v1 device does with a v2 frame)
- [RFC 3610](https://www.rfc-editor.org/rfc/rfc3610) (CCM) and
  [RFC 5869](https://www.rfc-editor.org/rfc/rfc5869) (HKDF).
