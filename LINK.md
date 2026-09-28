<!--
µnleashed BBS: LINK.md

The µnleashed link: one protocol that lets a board talk to small devices
nearby (a camera satellite, a door sat) over ESP-NOW or a serial line.
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
| DOOR | 2 | board and a door sat | a caller handed to a door, bytes both ways, "finished", "time's up" |
| reserved | 3-127 | | future core families |
| plugin | 128-239 | | families registered by plugins, assigned in this file |
| experimental | 240-254 | | anybody's, never in a release |
| invalid | 255 | | never sent |

A family id is assigned by adding a row to this table, the same way a
PROTOCOL.md field is. Two plugins claiming one id is refused at start, by
name, in the console and in `PLUGINS`.

The satellite and the door sat are **peers**. The board is always the
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
- **Four frames outstanding at the MAC.** The docs warn that "too short
  interval between sending two ESP-NOW data may lead to disorder of sending
  callback function". The link does not rely on the callbacks' order: it
  counts them, and sends while fewer than four are outstanding. On the
  bench four outstanding moved 53% more than one at 24 Mbps. The link's
  own window (below) is what keeps order and completeness, not the MAC.
- **Callbacks run in the Wi-Fi task**, high priority, on core 0: "do not
  do lengthy operations in the callback function". The receive callback
  copies the frame into one of two rings and returns; the send callback
  updates two counters. Nothing else.
- **Rate: 802.11g 24 Mbps per peer** (`esp_now_set_peer_rate_config`,
  5.2 and later, `WIFI_PHY_MODE_11G`, `WIFI_PHY_RATE_24M`). A peer drops
  to 1 Mbps (11b) after three MAC failures in a row to it, and goes back to
  24 Mbps after 30 s without one. Not MCS7 and not 54 Mbps: 24 is where the
  bench stayed reliable across a house. The ESP-NOW default is 1 Mbps,
  which Espressif's FAQ measures at about 214 kbps of real throughput in
  open air; 24 Mbps moved 6 to 10 times that on the bench and took less
  airtime from the callers' Wi-Fi (gateway pings 1-4 ms against 7-21 ms at
  1 Mbps). `LINK` says how many devices are on 1 Mbps just now.
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
  `WIFI_PHY_RATE_LORA_250K`), and only between Espressif chips. **Off by
  default**: on the bench, adding LR to the host raised the BBS's own
  gateway pings to 29-55 ms on average, which is callers' latency (Rule
  no. 1). A later option for a board whose satellite is out of reach, with
  that cost stated.

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
  2. on each, send an empty `DISCOVER` broadcast and listen 20 ms (the
     bench's figure, 2026-09-26, down from 60);
  3. the host answers with a unicast `BEACON` that carries its channel;
  4. move to the channel the BEACON names, and stay there.
  A full sweep is about a quarter of a second. Channel 14 (Japan, 11b only)
  is not scanned.
- **BEACON is the only thing a peer trusts about the channel**, never a MAC
  acknowledgement: a MAC ACK says some radio heard the frame, not that the
  host is on that channel (on the bench a peer settled on a neighbouring
  channel that way).
- **The host answers DISCOVER, HELLO and PAIR_HELLO only while it is
  joined to its router** (`esp_wifi_sta_get_ap_info`). While it is
  reconnecting its channel is wherever the scan has it, and a peer that
  believed a BEACON from there would wait on the wrong channel.
- **Every frame from the host carries the channel** in its `chan` byte
  (`esp_wifi_get_channel`). A peer that sees a different number from the
  one it is on moves at once.
- **When the router hops**, the host sees `WIFI_EVENT_HOME_CHANNEL_CHANGE`
  (present in 5.3.1, "doesn't occur when scanning"), logs it and counts it.
  It cannot warn its peers, because they are still on the old channel.
  They notice by silence: a peer sends `PING` every 5 s, and after a miss
  the next one goes after 1 s; after three misses it rescans (with more
  than one board: see "One satellite, several boards"). A door caller
  sees a pause of a few seconds while the box finds the host again; nothing
  is lost, because every unacknowledged frame is sent again.
- **A sleeping sender rescans at once.** A peer that wakes, sends and
  sleeps again (camsat on a timer) cannot wait out three PINGs. It rescans
  after three MAC failures in a row instead (`Engine::setFastRescan`).
  Other peers keep the PING rule, because a MAC failure proves nothing
  about the host.

---

## Pairing and keys

A peer is paired by two physical acts, one at each end, inside a short
window. Nothing is paired from the air alone.

**At the host:** the sysop types `LINK PAIR` (or picks "Pair a device" on
CONFIG link). The window is open for 2 minutes, and one pairing at a time.

**At the peer:** a peer with a button holds it for 3 s. A peer without one
(the ESP32-CAM has only RESET) is in pairing mode for 5 minutes after any
boot **while it holds no pairing**; to pair it again it is told to forget
its pairing by a physical act its own firmware documents (camsat: IO0 held
low while it powers up, as its README says). Physical access to the peer is
ownership of it.

**The exchange** (family 0, all in the clear; commit, then reveal):

1. peer to host, `PAIR_HELLO`, broadcast on each channel in turn, a
   quarter of a second each: its kind, name (16), firmware version (12)
   and an ephemeral ECDH public key `pubP` (P-256, 65 bytes uncompressed).
2. The host, inside its window and for one device at a time, makes its own
   key pair `pubH` and the ECDH secret (on the runner), picks a 16-byte
   nonce `nh`, and answers `PAIR_OFFER`: `pubH`, a commitment
   `SHA-256(pubH || nh)` cut to 16 bytes, the board's name and its channel.
3. The peer moves to that channel, makes the same secret, picks its own
   nonce `np` and sends `PAIR_NONCE`: `np`, again every quarter second
   until it hears the next step.
4. The host, holding `np`, sends `PAIR_REVEAL`: `nh`. The peer checks it
   against the commitment and drops the exchange if it does not match.
5. Both derive, with HKDF-SHA256 (salt `np || nh`, the ECDH secret as
   input, info `"unleashed link 1" || pubP || pubH`), 18 bytes:
   - `k_link`, the first 16: this pairing's long-term key;
   - the 4-digit code: the last two as a little-endian number, mod 10000.
6. The host shows the sysop
   `Pair camera "garden" 24:6f:28:aa:bb:cc, code 4821? (y/N)`, and the
   peer shows the same code (its console, an LED pattern).
7. On Y, host to peer `PAIR_DONE`, and peer to host `PAIR_ACK`: each a
   sealed empty message under `k_link` (packet number 0, each direction
   once), so each side proves it holds the same key. Then both store the
   pairing. The peer stays on the host's channel for 5 s after, sending
   DISCOVER every 100 ms, so its first HELLO finds the host at once.

**Why commit and reveal.** The host commits to `nh` before it sees `np`,
and the peer sends `np` only after it has the commitment, so neither end
can choose its nonce after seeing the other's. A man in the middle running
two exchanges therefore cannot steer them to the same code: two codes match
by chance once in 10,000. A sysop who compares the codes rules the middle
out; a sysop who does not is trusting that nobody else answered inside a
2-minute window they were standing beside, which is the same trust as
pressing WPS. `LINK` shows `checked` or `unchecked` beside each pairing,
set by the sysop's answer to `Does the device show 4821 too? (y/N)`.

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

**Forgetting** (`LINK FORGET n`) tells the satellite first when it is up
(a sealed `UNPAIR`), so it frees this board's slot, then deletes the
ESP-NOW peer and the block. A satellite out of reach finds out when its
`HELLO` is no longer answered, and keeps counting this board until its
owner revokes it or it is reset. It goes back to pairing mode only by its
own physical act.

**The pairings file**, version 2 (1.2.0; a first line `#link-peers 2` says
so): `mac kind checked pairedAt recv camno chan key name`. `recv` is what
this board takes from a shared camera (1 timelapse, 2 motion), `camno` its
camera number here (0 auto), `chan` the channel it was last up on. A file
without the header is version 1 (`mac kind checked pairedAt key name`) and
still reads, as recv 3, camno 0, chan unknown.

### One satellite, several boards (1.2.0)

A satellite holds up to **5 boards**, each its own pairing with its own
`k_link`, so one board never holds a key that opens another's traffic.
The design record is `internal/link-multiboard-2026-09-27.md`.

- **The owner** is the board it paired with first (the lowest order in its
  table; a board that unpairs hands ownership to the next). Only the owner
  opens the satellite to another board, revokes one, or has its camera
  settings used; the others choose only what they receive.
- **Sharing** (`LINK SHARE n` on the owner, a sealed `PAIR_OPEN`): the
  satellite takes one more board for up to 2 minutes (it caps a request at
  10). The new board's sysop runs `LINK PAIR` and the exchange above runs
  as usual. The satellite stays on its boards' channel the whole window and
  goes on serving them; every 2 s it sends one `PAIR_HELLO` on another
  channel for 25 ms, so a board elsewhere can say why it will not pair. The
  window ends at its time whatever state it reached (a No, a window that
  closed, a stray OFFER), with 30 s more to finish once the codes are being
  compared.
- **One channel for every board.** A satellite has one radio. `PAIR_HELLO`
  byte 94 carries the channel its boards are on (0 with none), and a board
  on another channel does not answer; its sysop is told: "This satellite
  works on channel 1 for its other boards and this board is on 6. Boards
  sharing a satellite must be on one Wi-Fi channel, which usually means one
  router." A board whose router moves on its own is marked not heard to
  the others (`PEERS`), retried every 30 s, and the satellite stays with
  the boards it can hear; if every board moves it scans and follows, as
  with one board.
- **Leaving**: `LINK FORGET n` on any board sends `UNPAIR`. **Revoking**:
  `LINK REVOKE n board` on the owner sends `REVOKE mac`; the satellite
  tells the revoked board (`UNPAIR`, the other way) and forgets it. The
  physical reset forgets everything.
- **Who else shares it**: the satellite sends each board `PEERS` when that
  changes and when the board comes up, and LINK shows the other boards
  under it by name, the owner marked, and which are not heard.

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
- **Which frames are sealed.** Everything once a session key exists. Eight
  types come before one and go in the clear: `DISCOVER`, `BEACON`,
  `HELLO`, `HELLO_ACK` and the four pairing steps `PAIR_HELLO`,
  `PAIR_OFFER`, `PAIR_NONCE` and `PAIR_REVEAL`. They carry nonces,
  versions, public keys, the board's name and its channel, which the
  router's beacons already show. None of them grants anything, and a
  receiver ignores every other type that arrives unsealed from a radio
  peer. `HELLO` and `HELLO_ACK` still carry a tag of their own (below).
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
- **HELLO and HELLO_ACK are tagged.** Each ends in 8 bytes of
  `HMAC-SHA256(k_link, dir || type || payload)`, so only a paired device
  gets an answer and only the host's answer is believed. A peer takes one
  HELLO_ACK per HELLO, the one that echoes its nonce, so a replayed ACK
  cannot put its packet numbers back to 1 under an old key.
- **Boot epochs.** Each end picks a random 32-bit epoch at boot and sends
  it in HELLO and HELLO_ACK. A new epoch, believed only when it is
  authenticated (the peer: a HELLO_ACK that answers its HELLO; the host: a
  HELLO whose key a sealed frame has proved), means the far end restarted:
  the
  sessions this end had with it are gone on its side, so they are ended
  here too (their owners told, reason `RESTART`) rather than left waiting
  for retries.
- **A new key does not displace a working one until it is proved.** The
  host keeps the old session key alongside a pending new one, and switches
  only when a sealed frame opens under the new key. A replayed `HELLO`
  therefore cannot cut a working peer off.
- **Rekey.** Long before a packet number could wrap, the peer sends a new
  HELLO; the host asks for one with `RESET` on session 0, reason `REKEY`.
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
  `pn`) after 150 ms, doubling to 1.2 s, 8 tries in all. A try counts only
  for the oldest message on a session, and only if the far end has been
  heard since the last send: a peer that is merely slow, or a window held
  at 0, waits rather than fails. Then the session is reset with `RESET`
  and its owner told: a transfer is abandoned, a door caller taken back.
- **Window.** A sender may have at most `win` fragments (bulk) or messages
  (streams, 4) unacknowledged. The receiver states `win` in `HELLO`,
  `HELLO_ACK` and every `ACK`, and states 0 when it has no room, which is
  how a slow caller's terminal throttles a door sat. 16 fragments for bulk
  on a board without PSRAM (3.5 KB of window), 64 on one with it (14 KB, in
  PSRAM; link.7, after the camsat bench found the S3 paced by a 16-fragment
  window at 72-88 KB/s with no retries). The SACK bitmap covers 32 past the
  first gap, so at 64 a loss can cost a few needless resends, never data.
- **Closing.** `closeAfter` ends a session once what is queued on it has
  been taken and the ACK this end owes has gone; the session then leaves a
  tombstone (8 kept, 30 s each). A resend of something already taken on a
  closed session (its ACK was lost) is ACKed again from the tombstone;
  anything new on it gets RESET, and is never taken as the first message of
  a new session. Without this the camsat bench saw a picture filed on the
  board and reported failed on the satellite (2026-09-26). Two seconds
  after it closes, the far end is sent RESET(CLOSED) for it, so its side
  goes too rather than idling in its table for two minutes: a satellite
  that never closed its own side filled all 16 after 16 pictures (camsat
  soak, link.5). The two seconds let a lost last ACK be resent and
  answered first.
- **Bulk ACKs from the runner.** The runner sends the acknowledgements that
  are due as soon as it has taken fragments, and again when it has freed
  half a window, instead of leaving them to the loop's next tick: a sender
  with 16 fragments in its window otherwise waits up to 20 ms a window,
  which the first real pictures (17 KB/s, 2026-09-26) showed was the limit.
  With it (link.5, same bench, before the runner is in the tree): 54 KB/s
  steady, no retries and no drops, XGA pictures of 87-93 KB in 1.6-1.8 s,
  39 of 39 in a 6.5-minute timelapse soak. That is near the loop fallback's
  ceiling (8 fragments a 20 ms tick); the runner lifts it at the 1.1.2
  merge. The same soak saw one or two slow passes a picture, worst 105 ms:
  the card writes, running from the tick until the runner is there.
- **Measured at link.7, with the runner** (camsat bench, 2026-09-27: the
  Waveshare S3 on 1.1.2-dev.5 plus link.7, the ESP32-CAM satellite waking
  from deep sleep for a timelapse about every 67 s, XGA JPEGs of 77-83 KB):
  - **A two-hour soak: 106 of 106 pictures filed**, none failed, none lost.
    37 to 80 KB/s, median 75: most pictures in 1.0-1.1 s, the slowest
    2.2 s. One retry a picture (the first fragment after a wake), no
    drops. The board counted **3 slow passes in two hours**, all in the
    first two and a half minutes (worst 155 ms, the first pictures onto
    the card); after that the loop's worst in any 10 s was 2-5 ms, average
    0.6-1.0 ms. Internal heap steady at 35,963, 26,563 at its lowest
    during a picture. A second soak (9 of 9, 61-70 KB/s) agreed.
  - **Robustness runs, all filed or refused cleanly, no crash on either
    end:** 20 SNAPSHOTs in a row, more than the 16-entry session tables
    (20 of 20, 68-82 KB/s, up to 16 retries on a picture and no drops);
    a CONFIG camsat save mid-picture (5 of 5, no panic once the part-file
    had one owner, the picture reported not filed and the next one filed);
    the satellite reset mid-picture (the next one filed, 72-74 KB/s) and
    held in reset for 10 minutes (it came back and filed, 82 KB/s).
  - **The board reset mid-picture**: the satellite fell back to 1 Mbps
    and held it for 30 s after the board came back, so the first pictures
    after a board reset went at 46-47 KB/s. Since link.9 the satellite
    goes back to 24 Mbps as soon as the board's link is up again (a probe:
    a path that really is marginal falls back within about a second).
- **Order.** Fragments of a bulk message are written in order into the
  receiver's window and handed on as an in-order stream. With four frames
  outstanding at the MAC a loss can put fragments out of order; the window
  holds them, and the SACK bitmap resends only what is missing.

---

## Where the work runs

Rule no. 1 decides every line of this: nothing the link does may stall a
caller who is not using it.

| Where | What | Bound |
|---|---|---|
| Wi-Fi task, core 0 (receive callback) | length check; a fragment of a bulk message (`nfrag` above 1) into the bulk ring, anything else into the control ring; count a drop if full | one copy of 250 bytes |
| Wi-Fi task (send callback) | one fewer outstanding; the peer's run of failures for its rate | two atomics |
| BBS loop, core 1 (the link plugin's tick, every 20 ms) | control frames: open (CCM), replay window, CRC, ack; a single-frame message goes to its family (door bytes into the caller's timeline, control); hand the radio its next frames | 8 frames a pass |
| background runner (1.1.2) | bulk: take the bulk ring's fragments, open them, into the window (`pumpRx`); the window in order, the message CRC-32, and the family's sink, a card file for a picture (`pumpBulk`); the pairing arithmetic (P-256) | one job, which stays 50 ms after the last fragment and gives the runner back after 500 ms |

**Bulk never touches the loop** (Rob, from the bench, 2026-09-26). A loop
taking eight frames a pass lost half a picture at 24 Mbps: the fragments
arrive faster than a 20 ms tick drains them. The radio sorts them by the
header (unauthenticated, so only a routing decision; the engine checks the
frame whichever ring it arrives on) and the runner takes them all. On the
task path the bench saw no slow passes and a worst loop pass of 1.4 to
3.2 ms during a picture.

Single-frame messages stay on the loop on purpose: a door keystroke is one
frame, and queueing it behind a SCREENS walk on the runner would put a
second of lag between a caller and their door. The engine has one lock (a
recursive mutex, `linkLock`), which every public call and `pumpRx` take,
so the keys, the replay window and the sessions have one owner at a time;
`pumpBulk` works on plaintext in the window outside the lock, through two
atomics (how far it has taken, and which slots are full).

**The cost of opening a frame.** mbedTLS's own CCM runs the cipher a block
at a time, and on the ESP32 each block is one trip to the AES peripheral,
lock and all: the camsat bench measured about 320 us for one 222-byte frame
on an ESP32 and 1,000 us on an S3 (2026-09-26). linkcrypto builds the same
CCM from one CBC call for the MAC and one CTR call for the keystream, which
hold the peripheral once each; it is byte for byte mbedtls_ccm's (checked
against RFC 3610 packet vector 1 and against mbedtls_ccm for every payload
length a frame can have). Measured in the BBS's own -Os build with hardware
AES, a 222-byte frame with its 20-byte header (camsat bench, 2026-09-26):

| | seal | open | mbedtls_ccm, same image |
|---|---|---|---|
| ESP32-S3 | 78 us | 78 us | 963 us |
| ESP32 | 93.6 us | 94.7 us | 654 us seal, 656 us open |

**The loop's per-frame cost has a limit** (Rob, 2026-09-26): about 100 µs a
frame, or the control ring growing under load, moves that work to a task
of its own. The bench measured a whole control frame on the loop, open and
dispatch together, at 150 to 184 µs, and Rob kept control frames on the
loop anyway: they come a few a second (a keystroke, a PING, an ACK), where
bulk comes hundreds a second, and bulk is what moved. `LINK` shows the
average and worst per-frame time and both rings' high-water marks, so the
answer is read off the board rather than argued.

The rings and windows are allocated when the link starts, from PSRAM on a
board that has it and the heap otherwise, and freed when it stops. Nothing
is allocated per frame, and nothing at all while the link is off.

The runner interface used is the one on rel-1.1.2a (`src/core/runner.h`):
a `runner::Job` per kind of work, `post`, `done`, `idle`, `collect`,
`breathe`. The link owns one job, "link", posted when the bulk ring or the
window has fragments waiting or pairing wants its arithmetic, and the job
is idle. The engine's pairing state stays the loop's: `pairTake` copies
what the arithmetic needs into a job, `pairRun` does it anywhere,
`pairGive` puts the answer back. A link stopped while its job runs (a
CONFIG save) is kept until the runner hands the job back, and its engine
neither sends nor takes frames meanwhile. On a tree without the runner
(the link branch before the 1.1.2 merge) the plugin's tick does a bounded
slice of the same work, which is fine for the host tests and not for a
picture at 24 Mbps; `__has_include("core/runner.h")` picks, so the merge
needs no edit here.

---

## Family 0: LINK (control)

| Type | Name | Dir | Sealed | Payload (bytes) |
|---|---|---|---|---|
| 1 | DISCOVER | P→H broadcast | no | empty; the host answers only its own peers (by source MAC), once a second each, and only while joined to its router |
| 2 | BEACON | H→P | no | `u8 channel, u8 version, char board[16]` |
| 3 | HELLO | P→H | tagged | `nonce[16], u8 version, u8 kind, char fw[12], u32 families, u8 win, u32 epoch`, then the 8-byte tag |
| 4 | HELLO_ACK | H→P | tagged | `nonce_peer[16], nonce_host[16], u8 win, u32 epoch`, then the 8-byte tag |
| 5 | PING | P→H | yes | `u32 uptime s, u32 free heap, i8 RSSI of the host as the peer hears it` |
| 6 | PONG | H→P | yes | `u32 unix time (0: no NTP), u8 channel` |
| 7 | ACK | both | yes | `u16 session, u16 seq expected, u8 bulk, u16 bulk seq, u16 fragments in order, u32 SACK, u16 win` |
| 8 | RESET | both | yes | `u16 session, u8 reason` (`CLOSED`, `RETRIES`, `REFUSED`, `BUSY`, `BADCRC`, `UNKNOWN`, `ABORTED`, `RESTART`, `REKEY`) |
| 16 | PAIR_HELLO | P→H broadcast | no | `u8 kind, char name[16], char fw[12], pubP[65]`, and from 1.2.0 `u8 home`: the channel its boards are on, 0 with none |
| 17 | PAIR_OFFER | H→P | no | `pubH[65], commit[16], char board[16], u8 channel` |
| 18 | PAIR_DONE | H→P | yes, `k_link` | empty |
| 19 | PAIR_ACK | P→H | yes, `k_link` | empty |
| 20 | PAIR_NONCE | P→H | no | `np[16]` |
| 21 | PAIR_REVEAL | H→P | no | `nh[16]` |
| 22 | PAIR_OPEN | H→P | yes | `u16 seconds`; from the owner only: take one more board (1.2.0) |
| 23 | UNPAIR | both | yes | empty. H→P: this board lets the satellite go. P→H: the satellite lets this board go (its owner revoked it) |
| 24 | REVOKE | H→P | yes | `mac[6]`; from the owner only: forget that board |
| 25 | PEERS | P→H | yes | `u8 count`, then per board `u8 flags` (1 owner, 2 up now, 4 this is you), `mac[6]`, `char name[16]` |

---

## Family 1: CAMERA

A camera satellite is a small ESP32 with a camera and no card. **Every
picture is a host SNAP**: the host names the file, applies the Photos
limits, dates and describes it and keeps the record, exactly as for a snap
from a built-in camera. The satellite never chooses a path or a name and
needs no clock. The pixel work, the watermark and the picture correction
(levels, gamma), runs on the satellite, which has the PSRAM for it; the
host sends the text to stamp. (The WROOM could not: the JPEG encoder's
tables are 8 KB of static DRAM.)

| Type | Name | Dir | Payload |
|---|---|---|---|
| 1 | SNAP | H→P | request id, frame size, quality, flash (off, on, auto), reason (caller, timelapse, test, motion); then optionally the watermark switch, board name, date, who, and the JPEG comment |
| 2 | PICTURE | P→H, bulk | the request id, reason, width, height, taken-at (0: no clock), then the JPEG |
| 3 | SNAP_FAIL | P→H | request id, reason code (no sensor, no memory, busy, flash fault, capture); busy adds the place in the queue (1.2.0: 1 next, 255 full) |
| 4 | STATUS | P→H | sensor model, largest frame size, free heap and PSRAM, uptime, last error; sent after HELLO and every 60 s |
| 5 | EVENT | P→H | motion, or its timer while it deep-sleeps: a request for a SNAP |
| 6 | SETTINGS | H→P | the camera page: timelapse, motion and hold-off, size, quality, flash, deep sleep, the picture settings, the motion pin; byte 21 what this board receives (1 timelapse, 2 motion; bit 7 set says it says, as bytes 21 to 23 were padding before 1.2.0) |
| 7 | SETTINGS_OK | P→H | the same, as it now runs them (it may clamp); byte 22 bit 0 set when the board it answers owns it, bit 7 when it keeps its own timelapse clock (the board's own clock then leaves it alone) |

- **Motion and deep sleep.** The satellite sends EVENT; the host answers
  with a SNAP (reason motion or timelapse, the texts filled) under the
  Photos limits and the system folders, as the built-in camera's system
  snaps are, or drops the EVENT by not answering (hold-off, card full). An
  EVENT to picture round trip is 3 to 10 ms on the bench, so motion loses
  nothing. A satellite that stays awake gets its timelapse SNAPs from the
  host's own clock.
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
  it waits, and the same limits per handle apply: one count across every
  camera on the board, not one per camera (Rob).
- **Several boards** (1.2.0). SETTINGS bytes 0 to 20 are taken only from
  the owner; every board sends byte 21, what it wants delivered, and the
  satellite echoes the owner's settings to all. A SNAP goes to the board
  that asked. A timelapse or motion picture is one capture, sent to each
  board that wants it, one after another. Requests queue two a board and
  eight in all, served in turn; a full queue answers `SNAP_FAIL` busy with
  its place, so the caller is told how many are ahead.
- **One SNAPSHOT.** A satellite does not bring verbs of its own: the camsat
  plugin adds a `photos::Camera` for each satellite that is up (order 1 +
  its pairing number, so the built-in camera stays camera 1), and the
  core's `SNAPSHOT [n|name]` and `CAMERA [n|name]` reach it. CONFIG cameras
  picks the default (PLUGINS.md, "A camera").

---

## Family 2: DOOR

A door sat is a device that runs doors: games and utilities a caller is
handed to and comes back from. The BBS stays in charge of the caller the
whole time. Several callers can be in one box at once, each in their own
session. The host side is the core `doors` plugin; door sats live in
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

- CAMERA SNAP, 6 bytes then an optional text block, 217 at most:
  `u16 req, u8 size, u8 quality, u8 flash, u8 reason` (0-5), `u8 mark`
  (6), `char board[20]` (7), `char when[16]` (27, "2026-09-26 14:05", the
  host's local time), `char who[24]` (43, "*guest", a handle, "timelapse"
  or "motion"), then the JPEG comment, up to 150 bytes and no NUL (67). A
  satellite that reads only the first 6 still works.
- CAMERA PICTURE: a 16-byte header (`u16 req, u8 reason, u8 0, u16 width,
  u16 height, u32 takenAt, u32 0`), then the JPEG.
- CAMERA SNAP_FAIL `u16 req, u8 code`; STATUS `u8 sensor, u8 maxSize,
  u32 heap, u32 psram, u32 uptime, u8 lastErr, u8 0, char model[12]`;
  EVENT `u8 kind, u32 takenAt`.
- CAMERA SETTINGS and SETTINGS_OK, 24 bytes: `u16 timelapseMin,
  u8 timelapseSec, u8 motion, u16 holdoffS, u8 size, u8 quality, u8 flash,
  u8 sleep` (0 awake, 1 deep sleep between shots), `u8 flip, u8 mirror,
  i8 bright, i8 contrast, i8 saturation, i8 exposure, u8 wb, u8 effect,
  u8 levels, u8 gammaIdx, u8 motionPin` (0xFF none), `u8 0[3]`.
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
- CLOSE is the last message on its session. When the session's window is
  full (a box that has stopped reading) the caller is back at the prompt
  at once and the board keeps trying CLOSE for 5 s from its tick, then
  forgets the session. When the doors stop (a CONFIG save, a shutdown) a
  CLOSE that cannot go becomes a RESET, which needs no window, and the
  link sends what is queued before its radio goes.
- If the box goes silent (retries run out, or the peer drops), the board
  takes the caller back with "--> Lost the signal." and "--> Back home."
- The words: `UPLINK` (or `DOORS n`) goes in with `--> Uplinking to
  <sat>...` and `--> Home is Ctrl-C three times.` (RUN/STOP on PETSCII);
  every way back ends in `--> Back home.`, after the reason in the board's
  voice or the door's own FINISHED or REFUSED words as they came. All of
  them are in `src/core/satwords.h`, and each fits 39 columns with a
  16-character sat name.
- The session number the board uses to track the door is kept in
  `Session::ownerData`, which is the owning plugin's scratch word, so the
  door framework adds nothing to the `Session` struct.

---

## The serial transport

The same frames on a UART, so a door sat can sit on a cable where there is
no radio, or be a PC or a Raspberry Pi with a USB serial adapter.

- Each frame is COBS-encoded and ends with a single 0x00. A reader resyncs
  on the next 0x00 after any error.
- Header, CRC-16, sessions, acknowledgements and windows as on the radio.
  `chan` is 0 and `pn` still counts, for duplicate detection.
- **Trusted by the wire: no pairing and no sealing** (Rob). A serial box
  is on a port the sysop configured, the same as the serial bridge:
  whoever can plug into the board's UART already has the board. SEC is
  never set on serial in 1.2.0. The trust belongs to the transport, not to
  the frame: a frame from the radio is never treated as serial, whatever
  it says.
- **Not built in 1.2.0-link.** The frames, COBS and the rules are settled
  and `cobsEncode`/`cobsDecode` are in the engine and tested; the UART
  transport itself follows the radio.
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
- **For door authors: the leave key reaches you.** The caller's way out is
  0x03 three times within 1.5 s, and every 0x03 is also passed to the door.
  A door run on a terminal device in cooked mode turns the first one into
  an interrupt signal: read raw, or treat a single Ctrl-C as nothing, so a
  caller's habit does not end a game unsaved.
- **A shared satellite gives no board another's keys.** Each board's
  pairing has its own `k_link`. Sharing and revoking come only from the
  owner, sealed; `PEERS` names the other boards and says nothing of their
  traffic. A board's name reaches the others only from its pairing OFFER,
  never from a clear BEACON.
- **Pictures are data.** The board names the file, checks the size before
  accepting it, writes to a temp name and files it only when the CRC-32
  checks. A satellite cannot write anywhere but Photos, and only through
  the board.
- **On the radio, sealed or nothing.** Past the eight clear types, a frame
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

At `1.2.0-link.5` (the bench's radio: two rings, four outstanding, the rate
table): `_bss_end` 0x3ffd7788, 161,672, so **19,064 free** (+56 bytes), and
PlatformIO's flash figure 1,275,160 (81.1%). The heap while on grows by the
second ring and the rate table: control ring 8 x 258, bulk ring 16 x 258
and 12 rate entries, about 6.4 KB together in place of the 2 KB ring, so
about 19 KB in all; the plugin declares 20 KB and starts only with that
plus the core's reserve free.

At `1.2.0-link.6` (the camera registry in the core): esp32dev `_bss_end`
0x3ffd77b0, 161,712, so **19,024 free** (+40: the camera table and the
`camera` setting), PlatformIO flash 1,275,740 (81.1%). The Freenove camera
board: `_bss_end` 0x3ffda498, 173,208, 7,528 free, flash 85.9%. The S3
builds at 83.6% flash. The callers' snap windows (about 2 KB) are taken
from the heap at the first camera, as the built-in camera took them before.

At `1.2.0-link.7`, rebased on main 1.1.2-dev.3 (the runner, SSH on the S3),
off the ELFs:

| Image | Static RAM (`_bss_end`) | Free | Against main at 1.1.2-dev.1 | Flash (PlatformIO) |
|---|---|---|---|---|
| esp32dev | 164,368 | 16,368 of 180,736 | +568 | 1,301,028 (82.7%) |
| Freenove WROVER CAM | 175,720 | 5,016 | +400 | 1,374,456 (87.4%) |
| AI-Thinker ESP32-CAM | 177,192 | **3,544** | +416 | 1,429,460 (90.9%) |
| Waveshare S3 (8 MB layout) | 254,672 of 341,760 (`_bss_end - 0x3FC88000`) | 87,088 | +4,176, SSH's included | 1,476,040 of a 3 MB slot (46.9%) |

At `1.2.0-link.8` (one satellite, several boards): esp32dev 164,648
(16,088 free, +280: the pairings' sharing state, the doors' leave window),
Freenove 175,960 (4,776 free), ESP32-CAM 177,432 (**3,304** free), S3
254,952. The satellite's side of sharing is in unleashed_camsat.

At `1.2.0-link.9` (SATS, CONFIG sats, fixed camera numbers): esp32dev
164,688 (16,048 free, +40), Freenove 176,016 (4,720 free, +56), ESP32-CAM
177,488 (**3,248** free, +56), S3 255,008 (+56). Images (firmware.bin):
1,320,688, 1,399,984, 1,455,616 and 1,495,744 bytes. SATS and CONFIG sats
are code, not statics: the page's state is a handful of bytes beside
CONFIG's own.

At `1.2.0-link.12`, on 1.1.2 (the small printf gave about 70 KB of flash
back): esp32dev 165,104 (15,632 free), Freenove 176,440 (4,296 free),
ESP32-CAM 177,912 (**2,824** free), S3 255,432 of 341,760. Images
1,255,824, 1,335,104, 1,390,288 and 1,430,928 bytes.

The ESP32-CAM is the one to watch: under 3 KB of static RAM at link.12
(3.5 KB at link.7). The camera boards' PSRAM move (1.3.0) is what buys it room.

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
- **`LINK FORGET n`**, **`LINK NAME n name`** (sysop). Pairings are numbered
  from 1. A name is one word, unique among the board's cameras, because
  SNAPSHOT takes one by name.
- **`LINK SHARE n`** (sysop, owner): open satellite n to one more board for
  2 minutes. **`LINK REVOKE n board`** (sysop, owner): board is its number
  under n in LINK, or its name.
- **LINK and a shared satellite**: a Shared column (`3, this board` at 80,
  `3 own` at 40), a row for each other board under it (`heard` or `not
  heard`) while the list fits the screen, and a footnote naming a board
  that is not heard, or saying this board's router moved.
- **`SATS [n]`** (callers as the cameras' levels allow, 1.2.0): the camera
  satellites, a row each: its number for SNAPSHOT, name, awake, asleep or
  not answering, its last picture. Never the radio, the keys, the other
  boards or the firmware. Staff also see channel, signal, rate, boards and
  uptime, and `SATS n` one in full (MAC, fingerprint and other boards need
  NODES). LINK is the radio and pairing; SATS is the satellites at work.
- **CONFIG sats** (sysop): a satellite's name, camera number and what this
  board receives from it (timelapse, motion), stored in the pairings file
  (`recv`, `camno`) and sent to the satellite at once.
- **`DOORS`** (users): the doors the boxes on the air offer, numbered;
  **`DOORS n`** goes through one. The break key three times within 1.5 s
  (0x03: Ctrl-C, RUN/STOP on PETSCII) always comes back; the key still
  reaches the door. Not 0x1D, which is cursor-right on PETSCII. A client
  that sends Ctrl-C or Break as a telnet command (IAC IP, IAC BRK) has it
  turned into 0x03 by the board's telnet layer.
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
- `host/linkpeer`: a pretend door sat (Echo and Clock) on the host board's
  UDP radio, built from the same engine. `tools/harness.sh` switches the
  link and doors on and gives the board and the box a port each;
  `--only=radio` runs `test_radio_link` (pairing through LINK PAIR, the
  codes, LINK, SYS) and `test_doors` (the handoff line, keys and output,
  FINISHED, the break key, a caller hanging up in a door), and
  `test_doors_petscii` (cursor-right stays in a door, RUN/STOP gets out).
- `tools/test_ext_plugin.sh`: plugins in their own repositories, above.
  The board's side of the same path was proved by building `esp32dev`
  with `custom_ext_plugins = hello` and `hello  tools/testplugin  -` in the
  lock: the pre-script fetched it, CMake wrote `X(kHelloPlugin)` into the
  build directory's `ext_plugins.h`, and `kHelloPlugin` is in the ELF.
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
