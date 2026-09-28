# The link's redundancy: what exists, the gaps, a proposal each (2026-09-28)

Rob: "make sure the espnow connection has some sort of redundancy if you
haven't thought that out already". A design note from the link lane at
1.2.0-link.12. No code. Figures are from LINK.md and the camsat bench
(2026-09-26/27) unless marked as an estimate.

## The answer first

| Item | Cost | When |
|---|---|---|
| **Store-and-forward on the satellite** (pictures taken while no board hears it are kept in PSRAM and delivered in order, to every board that wants them, filed once) | sat about 5 KB flash, 0.4 KB RAM, a PSRAM budget (default 1 MB, about 12 XGA pictures); board about 1.5 KB flash, 0.5 KB heap, 0 static; one event kind, one reason, a picture id in reserved header bytes, one SETTINGS bit | **1.2.0**, PSRAM only: 2 to 3 days and a bench day |
| Board's Wi-Fi reconnect keeps the channel between tries (today it scans every channel, so the link is deaf while the router is down) | about 0.3 KB flash, no RAM | 1.2.x, after a router power-cycle on the bench |
| Faster rescan when several boards share a satellite | about 0.2 KB on the satellite | 1.2.x |
| Long range as the last rate step (24M, 1M, then LR 250K) | about 0.5 KB each end | after the LR bench test (1 to 2 days); 1.2.x if it passes |
| The serial transport, for door sats | board about 4 KB flash, 2 KB heap while on; a reference door sat | 1.3.0 |
| Flash spool on the satellite, so a sleeping satellite keeps its pictures | a new satellite partition table (one USB reflash), about 2 KB flash | 1.3.0 |
| Re-pair without the sysop after a board's flash is wiped | **not safe**; a LINK note instead, about 0.2 KB | the note in 1.2.x |

## 1. What exists

- **Every frame is acknowledged and resent.** A frame wanting an ACK is
  sent again after 150 ms, doubling to 1.2 s, 8 tries. A try counts only
  while the far end is heard, and only for a session's oldest message, so
  a slow peer or a channel hop is a pause, not a failure. Bulk messages
  (pictures) have a window (16 fragments, 64 with PSRAM) and a 32-bit
  selective-ACK bitmap; a lost fragment is resent alone. Duplicates are
  dropped by sequence and by the packet-number replay window.
- **A session ending cleanly survives a lost last ACK.** A closed session
  leaves a tombstone (8 kept, 30 s): a resend of something already taken
  is ACKed again, never filed twice (the link.5 bench bug).
- **Rate fallback, per board since link.9:** 24 Mbps 11g, falling to
  1 Mbps 11b after 3 MAC failures in a row, back after 30 s clean, and at
  once when a board's link comes back after a reset.
- **Channel following.** The board never changes channel; it is a station
  on the router. A satellite scans (last good channel, then 1 to 13, a
  DISCOVER and 20 ms a channel, about a quarter second a sweep) and moves
  to the channel the board's BEACON names.
- **"Not answering".** The satellite pings every 5 s; 3 missed is down. The
  board calls a satellite quiet after 20 s. With one board, 3 MAC failures
  in a row start a rescan at once (`fastRescan_`).
- **Recovery after a reset on either end.** Each end has a random boot
  epoch in HELLO and HELLO_ACK; a new one, once authenticated, ends that
  peer's old sessions at once (reason RESTART) instead of leaving them to
  time out. A new session key never displaces a working one until a sealed
  frame proves it, so a replayed HELLO cannot cut a working peer off. The
  camsat bench: board reset mid-picture, satellite reset mid-picture,
  satellite held in reset 10 minutes, CONFIG saves mid-picture, 20 SNAPs
  past the session tables: every one recovered, nothing filed twice.
- **Several boards share one satellite** (link.8): up to 5, each with its
  own key; one board being down does not hold up the others (it is
  skipped in a group, its queued SNAPs are dropped with a reset).

What there is **not**: anything that keeps a picture when no board can
take it. Today a motion or timelapse picture whose board is down is not
taken for that board (`evPeerState(down)` drops its queue, and an EVENT
does not wait for it); a picture whose session is reset part way (8
retries run out) is lost.

## 2. The gaps, and a proposal each

### 2.1 Store-and-forward on the satellite (1.2.0)

**What.** A picture the satellite takes for a board that cannot take it
now is kept and delivered when that board is back: timelapse and motion
pictures taken while a board is down, and any picture whose delivery
failed part way. A caller's SNAPSHOT is not stored: the caller is waiting
at a prompt and a picture arriving an hour later is nobody's answer; they
get "not answering" as now.

**Where it is kept.** PSRAM, as a bounded queue of finished JPEGs (after
the watermark, so delivery is only sending).
- The ESP32-CAM maps 4 MB of its 8 MB PSRAM (bench log: "4MB is
  mapped"). A picture already uses the camera's frame buffer, the 512 KB
  output buffer (`cam.cpp` `kOutCap`) and the codec's buffers.
- **Default budget: 1 MB and 12 pictures, whichever comes first.** At the
  bench's XGA sizes (63 to 83 KB) that is 12 pictures, an hour of a
  5-minute timelapse. At UXGA (estimate 150 to 250 KB) it is 4 to 6.
- **The 1 MB is an estimate to confirm on the bench**: read the
  satellite's PSRAM free at the peak of a UXGA picture (CAMERA n shows it,
  from STATUS) and keep the budget at least 256 KB under it. A setting in
  CONFIG camsat ("Pictures kept while away", 0 to switch it off), capped
  by what the satellite reports it can hold.
- **When it is full:** the oldest timelapse picture goes first, then the
  oldest motion picture, never the newest. A timelapse is periodic and a
  gap in it is visible for what it is; the motion picture is usually the
  one somebody wanted. Every dropped picture is counted, and the count
  rides in STATUS, so the board logs it and CAMERA n says "3 pictures
  dropped while this board was away".

**Deep sleep loses PSRAM.** A satellite set to sleep between pictures
powers PSRAM down, so a held picture would be lost at the next sleep.
Rule: **a satellite holding pictures stays awake** until they are
delivered, up to a limit (default 30 minutes, the same setting page),
then sleeps and counts them as dropped. Battery-powered satellites are
the sysop's trade: the default keeps pictures over battery. Keeping them
through sleep needs flash (2.6, 1.3.0).

**Delivery.** When a board comes back (its link goes up, or 60 s after a
failed delivery, whichever first), the satellite sends a new
`CAM_EVENT` kind, `CEV_STORED` (kind, takenAt, picture id: 9 bytes
instead of 5), oldest first. The board answers with a SNAP carrying a new
reason `CR_STORED`, and the satellite sends the stored JPEG instead of
taking one. One at a time per board, behind any live SNAP (a caller
waiting beats a backlog). The board's one-picture-at-a-time job and the
BUSY answer already pace it.

**Every board that wants it.** Each stored picture keeps a bitmask of the
boards that wanted it when it was taken (each board's receive settings
for timelapse and motion, `wants()` in satsched.h), and a bit is cleared
when that board files it. A board that is unpaired or revoked has its bit
cleared everywhere. The picture is freed when its mask is empty.

**Filed once, even after a lost final ACK.** Two layers:
- A **picture id**: a u32 in the picture header's reserved bytes 12 to 15
  (`kPictureHeader`, "reserved 0" today), counting up, kept in RTC memory
  across deep sleep and started from a random value at power-on. 0 means
  "no id" (an older satellite), which keeps today's behaviour.
- The board keeps the last 16 ids it filed per satellite (8 satellites x
  16 x 4 bytes = 512 bytes of the camsat plugin's heap, 0 static) and
  answers a repeat with the ACK and no filing. Across a board reboot that
  table is gone, so the file name is made deterministic from the
  satellite, takenAt and the id, and filing is "create if absent": the
  same picture sent again after the board restarted finds its file there
  and is acknowledged, not filed twice.

**Compatibility.** A 1.2.0 board says it takes stored pictures with one
bit in SETTINGS byte 22 (`SO_STORE`). A satellite stores only for boards
that said so; an older board gets today's behaviour.

**Cost.** Satellite: about 5 KB flash, 16 records of about 24 bytes in
internal RAM, the PSRAM budget. Board: about 1.5 KB flash in the camsat
plugin, 512 bytes of heap. Core (linkfam.h): one event kind, one reason,
one header field, one SETTINGS bit, no engine change. Tests: satsched's
host test gains the queue rules (full, drop order, mask), the host
linkpeer gains a stored picture, and `test_sats` checks a picture taken
with the board down arrives once after it comes back. **2 to 3 days and a
bench day** (board off for 20 minutes during a timelapse, board rebooted
mid-delivery).

### 2.2 Long range as the last fallback (after the LR bench test)

24 Mbps, then 1 Mbps, then Espressif's LR at 250 Kbps (1/4 Mbps raw,
"about 2 to 2.5 times the distance of 11B", 4 dB better sensitivity: IDF
5.3.1 Wi-Fi guide). ESP-NOW takes it per peer (`WIFI_PHY_MODE_LR`,
`WIFI_PHY_RATE_LORA_250K`), the call the fallback already makes.
- **The unknown is the board.** It must add LR to its station's protocol
  bitmap while joined to an ordinary router. The guide says a mixed-mode
  station stays compatible with a normal AP; whether it then receives LR
  ESP-NOW frames is not in the guide. The bench test in
  internal/sat-types-2026-09-27.md answers it in 1 to 2 days.
- If it works: the satellite drops to LR after N failures at 1 Mbps and
  comes back the same way it comes back from 1 Mbps. A picture at LR is
  slower (estimate 12 to 20 KB/s, an XGA picture in 5 to 8 s), which
  store-and-forward and the spinner cover.
- Cost: about 0.5 KB each end, no RAM. 1.2.x if the test passes.

### 2.3 A second transport: the serial cable, for door sats (1.3.0)

LINK.md specifies it (COBS frames on a UART, the same header, sessions
and windows; trusted by the wire, so no pairing and no sealing) and the
engine has `cobsEncode`/`cobsDecode`, tested. Not built: the UART
transport on the board, UART2 shared with the serial bridge (one owner,
CONFIG refuses the clash), baud and pins in CONFIG link. A door sat on a
cable cannot lose its channel, which is the point. Cost: about 4 KB of
board flash, 2 KB of heap while on, 0 static. A camera satellite on a
cable is possible but not the goal (a picture at 115200 baud is about
11 KB/s). 1.3.0, with the Python reference door sat from the sat-types
note as its far end.

### 2.4 Board-side: re-pair without the sysop after a wipe? No

**Not safe, and the reason is the whole pairing design.** A pairing is a
key both ends made by ECDH while a person watched both ends (the sysop's
`LINK PAIR` and the satellite's physical act, the 4-digit codes). A
satellite that accepted a new pairing from "its board" after that board
lost its key would be accepting it from anything that claims to be its
board: a MAC is trivially spoofed, and a fresh key exchange has nothing to
check it against. Anyone in radio range could take the satellite, and its
camera.

What is safe, and what already holds:
- Pairings live on `userdata`, so a firmware update or a reflash keeps
  them. Only a factory reset (the BOOT hold's 15 s band) or a full erase
  loses them.
- Pairings are kept out of the backup by Rob's decision (a backup zip is
  one file over plain HTTP on the LAN; with the keys in it, whoever has
  the zip can be the board to its satellites). That stays his call.
- **Proposal (1.2.x, about 0.2 KB):** when the board hears a HELLO from a
  device it has no pairing for, repeatedly, LINK says so: "A satellite
  near this board thinks it is paired with it (24:6f:..:cc): pair it
  again with LINK PAIR and its button." Information only; it grants
  nothing. The re-pair itself stays two minutes and a button.

### 2.5 Other gaps found while looking

- **The board is deaf to the link while its router is down.** On a Wi-Fi
  disconnect the board calls `esp_wifi_connect()` at once and again after
  every failure (`src/main.cpp` ~141), and each attempt scans every
  channel. ESP-NOW goes where the radio goes, so a router reboot (a minute
  or two) is a minute or two in which no satellite can reach the board,
  and a satellite that sweeps channels looking for it finds it only by
  luck. Proposal: pace reconnects (every 10 s after the third) and between
  tries put the radio back on the last good channel
  (`esp_wifi_set_channel`, allowed while not connected). The link then
  works through a router reboot, and store-and-forward covers the rest.
  About 0.3 KB, core code: measure a router power-cycle on the bench
  first. 1.2.x.
- **A router's channel change with several boards.** The boards follow the
  router by themselves. A satellite with one board rescans after 3 MAC
  failures in a row; with several it waits for pings to fail (up to
  15 to 20 s), because one board being silent is not a channel change.
  Proposal: rescan at once when every board that was up shows the MAC
  failure streak. About 0.2 KB on the satellite. 1.2.x.
- **Both ends rebooting together (a power cut).** Already handled, and
  measured in parts: the board is back on Wi-Fi in about 7 s; the
  satellite sweeps until it hears a BEACON, and retries a board it has
  not heard every 30 s (`kHostRetryMs`); boot epochs end the old sessions
  on both sides. Worst case about 30 s to link up. What is lost today is
  any picture taken in that window, which store-and-forward keeps.
- **A delivery that fails part way** (8 retries run out on a fragment) is
  lost today. With store-and-forward the picture stays owed and is sent
  again 60 s later or when the link comes back. No extra cost.

## 3. Recommendation

- **1.2.0: store-and-forward, PSRAM only**, with the stay-awake rule, the
  drop order and count, the picture id and the create-if-absent filing.
  It is most of the value (nothing a satellite sees is lost to a board
  being briefly away) and none of it touches the engine.
- **1.2.x, each after its bench check:** the Wi-Fi reconnect that keeps
  the channel, the multi-board rescan, LR as the last rate step, the
  "thinks it is paired" note.
- **1.3.0:** the serial transport with a reference door sat, and a flash
  spool so a sleeping satellite keeps what it took.
- **Never:** re-pairing without a person at both ends.
