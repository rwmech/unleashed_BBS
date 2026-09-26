# The µnleashed link: one page for Rob (2026-09-26)

The spec is LINK.md at the top of the repo. Branch rel-1.2.0-link, off main
at 78a0ba4; not merged, pushed or tagged.

## What it is

- One ESP-NOW protocol in the core, off until the sysop turns it on.
  Families on top: 0 LINK (control), 1 CAMERA, 2 DOOR; 128-239 for plugins.
- The board never changes channel. Peers scan for it, 60 ms a channel, and
  every board frame carries the channel. A router hop is a pause, not a
  lost session.
- 20-byte header, up to 222 bytes of payload, AES-128-CCM with the header as
  associated data and an 8-byte tag. Six pre-key types go in the clear and
  grant nothing.
- Same frames over serial, COBS-framed (codec built and tested; the serial
  transport itself is for later).

## Your decisions, as built

1. Single frames on the loop (8 a pass), reassembly on the runner. `LINK`
   shows the per-frame time and the ring's high-water mark.
2. Our own AES-CCM only; ESP-NOW peers unencrypted.
3. 8 pairings, SYS/LINK say when full.
4. Pairings out of the backup.
5. `doors` is a core plugin.
6. `src/core/photos.*` is built; camera.cpp and files.cpp switch to it at
   the 1.1.2 merge (1.1.2a rewrote both).
7. camsat in every image, off by default: `plugins.lock` has its line
   commented until unleashed_camsat has a first commit; then the release
   environments name it.
8. Serial boxes trusted by the wire.

## What moved after the spec

- mbedtls_ccm measured 320 us a frame on an ESP32 and 1,000 us on an S3
  (camsat bench). CCM is now one CBC + one CTR call, byte-identical to
  mbedtls_ccm: 78 us seal and 78 us open on the S3 in the real -Os build.
  Under your 100 us line, so no link task. ESP32 figure to come.

## Budget (WROOM, measured off the build)

- Static DRAM: 142 bytes. `_bss_end` 161,616, so 19,120 free.
- Flash: 34,293 bytes (ESP-NOW's library 6,604 of it). Image 80.6%.
- Heap: about 15 KB, only while the link is on.

## Tested

- host/test_link: 90 checks through a simulated radio (loss, duplicates,
  reordering, a channel hop), RFC 3610 and 5869 vectors. ASan/UBSan clean.
- `--only=radio`: 28 checks, pairing and doors end to end against a pretend
  door box.
- tools/test_ext_plugin.sh: 11 checks, a plugin from its own repository.
- Not yet on a board: everything. Bench list in LINK.md, "Testing".
