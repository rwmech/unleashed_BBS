# The µnleashed link: one page for Rob (2026-09-26)

The spec is LINK.md at the top of the repo. Branch rel-1.2.0-link, off main
at 78a0ba4; not merged, pushed or tagged. This page is as of 1.2.0-link.5.

## What it is

- One ESP-NOW protocol in the core, off until the sysop turns it on.
  Families on top: 0 LINK (control), 1 CAMERA, 2 DOOR; 128-239 for plugins.
- The board never changes channel. Peers scan for it, 20 ms a channel,
  and trust only BEACON's channel. The board answers nothing while it is
  off its router. A router hop is a pause, not a lost session.
- 20-byte header, up to 222 bytes of payload, AES-128-CCM with the header as
  associated data and an 8-byte tag. Eight pre-key types go in the clear
  and grant nothing; HELLO and HELLO_ACK carry an HMAC tag and a boot epoch.
- Pairing is commit then reveal (P-256, HKDF bound to both public keys), so
  nobody in the middle can steer two exchanges to one code.
- Same frames over serial, COBS-framed (codec built and tested; the serial
  transport itself is for later).

## Your decisions, as built

1. Control frames on the loop (8 a pass). Picture fragments never touch the
   loop: the radio sorts them into their own ring and the runner takes them.
2. Our own AES-CCM only; ESP-NOW peers unencrypted.
3. 8 pairings, SYS/LINK say when full.
4. Pairings out of the backup.
5. `doors` is a core plugin.
6. `src/core/photos.*` is built; camera.cpp and files.cpp switch to it at
   the 1.1.2 merge (1.1.2a rewrote both).
7. camsat in every image, off by default: `plugins.lock` has its line
   commented until unleashed_camsat has a first commit.
8. Serial boxes trusted by the wire.

## The bench, adopted (camsat engineer, S3 on your router plus an ESP32-CAM)

- 11g 24 Mbps per peer, 1 Mbps after three MAC failures, back after 30 s
  clean. Four frames outstanding (+53%). LR off: it raised the board's own
  gateway pings to 29-55 ms.
- CCM as one CBC plus one CTR call: seal/open 78 us on the S3, 93.6/94.7 us
  on the ESP32 (mbedtls_ccm: 963 and 654 us).
- A whole control frame on the loop is 150-184 us; kept there because
  control frames come a few a second.
- First real pictures on link.4: 5 of 5 filed, XGA, but 17 KB/s. Two engine
  faults fixed in link.5: a session closed straight after a picture lost
  its last ACK (now held, with tombstones), and bulk ACKs waited for the
  loop's tick (now sent from the runner). Speed to re-measure on the bench.

## Budget (WROOM, measured off the build, link.5)

- Static DRAM: `_bss_end` 161,672, so 19,064 free.
- Flash: PlatformIO 1,275,160 (81.1%); the link and doors about 34 KB of it.
- Heap: about 19 KB, only while the link is on (declared 20 KB).

## Tested

- host/test_link: 118 checks through a simulated radio (loss, duplicates,
  reordering, a channel hop, a real runner thread), RFC 3610 and 5869
  vectors. ASan/UBSan clean; TSan cannot link in this WSL.
- Harness `--only=radio,plugins,shell,about`: 671 checks, all pass.
- tools/test_ext_plugin.sh: 13 checks.
- Not yet on a board: the link.5 engine. The camsat engineer re-measures.

## link.5 on the bench, and link.6

- link.5 with camsat (S3 plus ESP32-CAM): 54 KB/s steady, no retries, 39 of
  39 in a 6.5-minute timelapse soak. That is the pre-runner ceiling; the
  runner lifts it at the 1.1.2 merge. One or two slow passes a picture
  (worst 105 ms) are the card writes on the tick, also gone with the runner.
- link.6: the camera registry you approved. One SNAPSHOT for every camera
  (built-in first, then satellites by pairing), CONFIG cameras for the
  default, and one per-caller budget across all cameras. The engine now
  also tells the far end when it closes a session: the soak found a
  satellite's session table filling after 16 pictures.
- WROOM static DRAM 161,712 (19,024 free); Freenove 7,528 free.
