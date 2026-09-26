# The µnleashed link: one page for Rob (2026-09-26)

The spec is LINK.md at the top of the repo (the repo keeps its docs at the
root; there is no docs/ folder, so I did not start one). Step 1 only:
nothing is built.

## What it is

- One ESP-NOW protocol in the core, off until the sysop turns it on.
  Families on top: 0 LINK (control), 1 CAMERA, 2 DOOR; 128-239 for plugins.
- The board never changes channel (it is a station on the router). Peers
  scan for it, 60 ms a channel, and every host frame carries the channel.
  A router hop costs a peer up to 15 s of silence, then it rescans; nothing
  is lost because unacked frames are resent.
- 16-byte header (version, family, type, flags, session, seq, fragment,
  count, length, channel, CRC-16), up to 226 bytes of payload, an 8-byte
  HMAC tag. Multi-fragment messages carry total length and a CRC-32 up
  front.
- Same frames over serial with COBS framing, so a door box can be a Pi on
  a USB cable. Sessions are multiplexed, which also ends "one serial port,
  one caller".

## Checked against IDF 5.3.1, not memory

- 250-byte payload. v2.0 (1,470) is IDF 5.4+, not ours.
- ACK is MAC layer only, and send the next frame after the callback.
- Encrypted peers: Kconfig default 7, up to 17 (SoftAP is compiled out, so
  no key slots go there). The header's "6" is stale. The link caps at 6.
- 1 Mbps PHY default, about 214 kbps real in open air: a 30 KB VGA JPEG is
  about 1.2 s.
- `WIFI_EVENT_HOME_CHANNEL_CHANGE` exists in 5.3.1.
- LR exists but nothing says a station on a normal router keeps its link
  with LR added: off in 1.2.0, a bench question later.
- The receive callback cannot say whether a frame was decrypted, and a MAC
  is trivially spoofed. So every radio frame carries its own tag and
  "came from a paired MAC" is never trusted.

## Pairing

`LINK PAIR` on the board (2-minute window) plus a physical act on the peer
(a button, or an unpaired satellite's first 5 minutes after boot). ECDH
P-256, HKDF, a 4-digit code the sysop may compare with the peer's console,
PMK and LMK for ESP-NOW encryption, a separate key for the tags. All of the
crypto is already linked in the WROOM image for WPA3 (checked in the 1.1.1
ELF), so it costs almost no flash.

## Rule no. 1

The Wi-Fi callback only copies into a ring. Single-frame messages (a door
keystroke, control) are handled on the loop, at most 8 a pass, about 60 µs
each, to be measured. Reassembly and anything touching the card run on the
1.1.2 runner, never on the loop.

## Budget (WROOM)

- Measured baseline, 1.1.1 ELF: 162,992 static DRAM, 17,744 free.
- Link: at most 1 KB static; about 10 KB heap only while it is on (PSRAM
  on boards that have it); 0 per Session (door state uses `ownerData`).
- Flash estimate 20-35 KB, to be measured per symbol in step 2.

## External plugins

A plugin repo has `unleashed-plugin.ini`, `bbs/` and anything else
(camsat's `firmware/`). An env names its plugins (`custom_ext_plugins`),
`plugins.lock` pins each to a commit, `tools/plugins.py` fetches into
`ext/` (gitignored) and refuses a mismatch. CMake and the host Makefile add
the sources and generate one include; registry.cpp includes it and is
never edited per plugin again. Plugin API version major.minor with a
`static_assert` in the plugin. release.py checks commits, SPDX and
copyright and records every plugin@commit in the release. Not the IDF
component manager (the refetch-per-env problem from 1.1.0, a dependency
cycle, and no host build), and not linker-section self-registration
(unreferenced objects are not linked).

## Decisions I need from you

1. Single frames on the loop and reassembly on the runner, or a small link
   task of its own (about 3.5 KB heap while on)?
2. ESP-NOW encryption plus our tag (the design), or our own AES-CCM only
   with no 7-peer ceiling?
3. Six peers enough?
4. Pairings left out of the backup zip, so a restored board pairs again?
5. The DOOR host side as a core plugin (`doors`), with door boxes in their
   own repos?
6. Photos on a board with no camera: move "file a JPEG into Photos" out of
   camera.cpp into shared code camsat also calls?
7. Which official images carry camsat: every image, off by default, or its
   own image set?
8. A serial door box trusted by the wire (no pairing), like the serial
   bridge?
