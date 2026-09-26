# SSH on the ESP32-S3: research for 1.1.2 (preview)

Date: 2026-09-26
Tree: main at e656696 (1.1.2 in progress), measured in a throwaway detached
worktree, `release-prep/wt-ssh-research`. Nothing in the main checkout was
changed except this file. Nothing was committed, flashed or deployed. The
worktree is left dirty on purpose (the probe, the components, the S3 table)
so every figure below can be rebuilt; delete it with
`git worktree remove --force ../../release-prep/wt-ssh-research` when done.

Target: the Waveshare ESP32-S3-LCD-1.47 profile (`ws_s3_lcd147`), ESP-IDF
5.3.1 through `espressif32@6.9.0`, no Arduino, and (from Rob's revision
today) one S3 layout that fits an 8 MB part.

---

## 0. Bottom line

- **Recommended library: wolfSSH 1.5.0 on wolfCrypt 5.9.4**, built as a
  plain ESP-IDF component from source, software crypto, S3 only.
  GPLv3-or-later in every file header, so it combines with this project.
  Not the registry's `wolfssl/wolfssh` 1.4.20: it predates the fix for
  CVE-2025-14942 (fixed in 1.4.22) and does not build against the
  registry's own wolfSSL 5.8.2 with the stock settings.
- **Flash, measured: +115,232 bytes** on the S3 image (1,270,240 →
  1,385,472) for a working server shape: host keys made on the board,
  curve25519 and P-256 key exchange, Ed25519 and ECDSA host keys,
  AES-CTR/GCM, HMAC-SHA2-256, pty size captured. The glue a real
  integration adds is estimated at 5 to 10 KB more, so **about 125 KB**.
- **Static DRAM, measured: +312 bytes**, 304 of which are the probe's two
  host-key buffers (they can live on the heap). **IRAM: 0.**
- **RAM per session: about 14 KB steady, about 20 KB during key exchange**
  (estimate from struct sizes measured on the target, reasoning in §3).
  **All of it can be PSRAM**: the build routes every wolfSSL and wolfSSH
  allocation through `heap_caps_malloc_prefer(SPIRAM, INTERNAL)` and links.
  The internal RAM that cannot move is one task stack, 12 to 16 KB, and
  even that can go to PSRAM on the S3 under one rule (§3.3).
- **It fits the current 1.5 MB slot**: 187,392 bytes left after the
  library, about 177 KB after the glue. libssh does not fit sensibly:
  +278,032 bytes, 24,592 left.
- **The partition answer (Rob's clean 8 MB layout):** two 3 MB app slots,
  userdata 1,376 KB, storage 512 KB and last, logs unchanged at 32 KB.
  Built and checked: IDF's own table tool accepts it for 8 MB, PlatformIO
  puts the app at 0x20000, `buildfs` makes a 524,288-byte image (the
  storage partition), and the bootloader and app headers both say 8 MB.
  `tools/release.py` and the directory's `server.py` each hard-code the
  storage offset and must follow (§4). Proposed `partitions_s3.csv` is in §4.1.
- **Rule no. 1 holds if nothing in the BBS loop ever calls wolfSSH.** A key
  exchange is an estimated 0.2 to 0.6 s of CPU per connection on the S3
  (wolfSSL forces its slow, constant-time 25519 code on Xtensa). That is
  fine in its own task and a disaster in the loop. The seam in §5 keeps it
  out: a ring pair per SSH session and an `eventfd` the loop's `select()`
  already knows how to wait on.
- **Nothing in the "huge for small" category here.** This is a feature
  costing about 125 KB of flash and about 16 KB of internal RAM on a board
  that has room for both. The one cheap surprise: SSH can share port 6400
  with telnet by sniffing the client's `SSH-2.0-` line, which saves the
  listening socket and a second port forward (§5.4, needs a bench check).

### What changes for callers (say so in the release notes)

- A second way in: `ssh -p <port> <board>`, encrypted, on the S3 only. The
  ESP32 images are unchanged (the component compiles to nothing there).
- First connection asks the caller to trust the board's host key (trust on
  first use). A board that loses its userdata, or is reflashed with an
  erase, gets a new key and every returning SSH caller sees OpenSSH's
  "REMOTE HOST IDENTIFICATION HAS CHANGED" warning.
- Connecting over SSH takes an estimated 0.2 to 0.6 s longer than telnet
  before the first byte of the welcome.
- **The first 1.1.2 install on an S3 is a full erase.** Accounts, config and
  mail on the S3 are gone (Rob: only his desk boards exist, acceptable).
- The S3's directory badge will say "ESP32-S3 · 8 MB · PSRAM" instead of
  16 MB (§4.3), because the image header must say 8 MB to boot on an N8R8.

---

## Summary table

Ranked by fit for this board: what it gives for what it costs.

| Rank | Option | Flash on S3 | Static DRAM | Heap / session | Licence | Verdict |
|---|---|---:|---:|---|---|---|
| 1 | **wolfSSH 1.5.0 + wolfCrypt 5.9.4**, SW crypto, P-256 + 25519 | **+115,232 measured** | +312 measured | ~14 KB, ~20 KB peak (est.), PSRAM | GPLv3-or-later (file headers) | **Recommended** |
| 2 | same, with the S3's hardware SHA/AES | +121,136 measured (+5,904) | +352 | same | same | Not recommended: two independent locks on one peripheral (§6.2) |
| 3 | libssh 0.12.2 (`david-cermak/libssh`, real IDF component) | **+278,032 measured** | **+10,120 measured** | ~28 KB, ~32 KB peak (its own doc) | LGPL-2.1-only | Works on 5.3.1 with four Kconfig lines; too big for the 1.5 MB slot, fine in 3 MB |
| 4 | LibSSH-ESP32 0.11.5 (ewpa) | +272,064 measured | +10,168 measured | as libssh | LGPL | Declares `REQUIRES arduino`; built without it only after patching its config header |
| 5 | CycloneSSH 2.6.4 (Oryx) | not built | | | GPL-2.0-or-later | Compatible licence, but its headers include CycloneTCP's `core/net.h`; running it on lwIP is a port |
| 6 | Own minimal SSH on the mbedTLS already in the image | est. 30 to 60 KB + an Ed25519 | small | small | ours | Smallest, and a security protocol written from scratch; not for a preview |
| - | TinySSH | | | | CC0 | Rejected: chacha20-poly1305 only (no AES-CTR), so no SyncTERM at all; inetd/fork model |
| - | Dropbear | | | | MIT | Rejected: POSIX fork-per-connection, no ESP-IDF port found |
| - | libssh2 (`skuodi/libssh2_esp`) | | | | BSD-3 | Client only |

---

## 1. Candidates, from primary sources

### 1.1 wolfSSH + wolfCrypt (recommended)

- **Licence.** Every wolfSSH 1.5.0 and wolfSSL 5.9.4 source file read here
  says "either version 3 of the License, or (at your option) any later
  version" (`wolfcrypt/src/ecc.c`, `wolfssh/src/internal.c`, lines 8 to 10).
  The top-level `LICENSING` files say "GPLv3" with no "or later" (wolfSSL
  adds a GPLv2 exception for listed software). Either reading combines
  with a GPL-3.0-or-later project; the shipped binary is GPLv3. No
  `SPDX: GPL-2.0` line in the subset used, so `release.py`'s check passes.
  Needs a `THIRD_PARTY_NOTICES.md` entry.
- **ESP-IDF availability.** The registry has `wolfssl/wolfssl` 5.8.2~1
  (GPL-3.0-only on its page) and `wolfssl/wolfssh` 1.4.20 with an echo
  server example. Tried: the pair does not build here with stock settings
  (`wc_SSH_KDF` undeclared, because the stock `user_settings.h` only
  defines `WOLFSSL_WOLFSSH` under an example-specific macro), and 1.4.20
  is two releases behind a server-relevant security fix:
  wolfSSH's ChangeLog lists CVE-2025-14942 ("key exchange state machine can
  be manipulated ... This fix is also recommended for wolfSSH server
  applications"), fixed in 1.4.22 (January 2026). **Use 1.5.0 (April 2026)
  from GitHub**, pinned, with our own 20-line component (the one in the
  worktree, `components/wolfssh_min/`). Vendor the needed subset of source
  rather than the whole wolfSSL repository (about 60 files: 30 of wolfCrypt,
  8 of wolfSSH, their headers).
- **Server side:** yes, it is a server first ("wolfSSL's Embeddable SSH
  Server", its README). Non-blocking operation with custom I/O callbacks
  (`wolfSSH_SetIORecv/Send`), which is what the seam in §5 needs.
- **Algorithms in 1.5.0** (`src/internal.c`, `cannedKexAlgoNames` and
  friends): kex `curve25519-sha256`, `ecdh-sha2-nistp256/384/521`, DH
  groups, ML-KEM hybrids; host keys `ssh-ed25519`, `ecdsa-sha2-nistp256`,
  RSA; ciphers `aes*-gcm@openssh.com`, `aes*-ctr` (CBC soft-disabled);
  MACs `hmac-sha2-256/512`. **No chacha20-poly1305 and no `-etm` MACs**,
  which also means no exposure to the Terrapin prefix truncation
  (CVE-2023-48795), which needs one of those; wolfSSH does not implement
  OpenSSH's strict-kex extension, and does not need it with this set.
- **Hardware crypto:** its own Espressif port
  (`wolfcrypt/src/port/Espressif/esp32_sha.c`, `esp32_aes.c`,
  `esp32_mp.c`), talking to the peripherals directly with its own mutexes,
  **not** through mbedTLS. See §6.2 for why that is a reason to turn it off.
- **A build bug worth knowing:** with ECC off and Ed25519 on, 1.5.0 does not
  compile (`internal.c:12635: union has no member named 'ed'`). So a
  25519-only build needs a patch; the recommended build keeps P-256 anyway
  (§2).

### 1.2 libssh

- **`david-cermak/libssh` 0.12.2** (registry, LGPL-2.1-only, "Minimal
  ESP-IDF component wrapping upstream libssh to run an SSH server"), IDF
  `>=5.2`, depends on `espressif/sock_utils`. **It builds on 5.3.1 without
  Arduino**, measured here, once four Kconfig lines are set:
  `CONFIG_MBEDTLS_THREADING_C=y`, `CONFIG_MBEDTLS_THREADING_PTHREAD=y`,
  `CONFIG_MBEDTLS_THREADING_ALT=n` (5.3.1 defaults ALT to y when threading
  is on, and that fails `check_config.h`), plus `CONFIG_LWIP_NETIF_API=y`
  and `CONFIG_VFS_SUPPORT_TERMIOS=y` from its example. Crypto through the
  IDF's mbedTLS, so the S3's hardware SHA/AES are used under the IDF's own
  lock. Its `footprint.md` (S3, IDF 5.5): `liblibssh.a` 233 KB, heap ~28 KB
  a session, peak ~32 KB, stack ~4.7 KB, 10 KB of DRAM.
- **`ewpa/LibSSH-ESP32` 0.11.5** (LGPL): "an Arduino library"; its
  `CMakeLists.txt` says `REQUIRES arduino`, and it relies on Arduino's
  `-DESP32` define (its `poll.h` redefines `nfds_t` otherwise). Builds
  without Arduino only after adding `#include "esp_idf_version.h"` and
  `#define ESP32 1` to its `libssh_esp32_config.h`. Its README also
  recommends turning `CONFIG_MBEDTLS_HARDWARE_SHA` off "for improved
  stability under any concurrency", which is the same hazard as §6.2.
- **Both are big for one reason:** libssh carries its own Ed25519 with a
  109,696-byte precomputed base table (`ge25519_base_multiples_affine`,
  measured in the map), because mbedTLS 3.x has no EdDSA.
- Server side, curve25519 kex, Ed25519 and ECDSA host keys, strict kex:
  all yes. `ssh_message_channel_request_pty_term/width/height` hands over
  the pty request whole, which is nicer than wolfSSH 1.5.0 (§5.3).

### 1.3 The others

- **CycloneSSH** (`Oryx-Embedded/CycloneSSH`, `ssh/ssh_server.c`:
  `SPDX-License-Identifier: GPL-2.0-or-later`, so licence-compatible).
  `ssh/ssh.h` includes `core/net.h` from CycloneTCP; no lwIP shim was found
  in the repository. Oryx's own ESP32-S3 crypto benchmark is the best
  published timing source for §6 and is cited there.
- The registry also has `jimmyw/ssh_cli_server` (MIT, on
  `david-cermak/libssh`, "approximately 20KB+ RAM per active connection")
  and `valdanylchuk/breezy_ssh` (MIT, on the same libssh). Wrappers, not
  alternatives.

---

## 2. Which algorithms, and why P-256 has to stay

The legacy-caller point matters here: SyncTERM is the BBS client, and it
changes crypto library between the release people have and the one being
written.

| Client | Key exchange | Host key | Cipher | MAC | Source |
|---|---|---|---|---|---|
| SyncTERM up to 1.9 (cryptlib, Synchronet's patches) | **ecdh-sha2-nistp256** preferred, DH group14/GEX | **ecdsa-sha2-nistp256**, rsa-sha2-256, ssh-rsa | aes128/256-ctr, CBC | hmac-sha2-256, hmac-sha1 | cryptlib `session/ssh2_algo.c` tables; `misc/config.h` defines `USE_ECDH`/`USE_ECDSA` by default and 25519 only in custom profiles; its README: X25519/Ed25519 "disabled by default"; Synchronet's `3rdp/build/cl-prefer-ECC.patch` and `cl-use-ssh-ctr.patch` |
| SyncTERM 1.10 (in development: "Switch to DeuceSSH + OpenSSL \| Botan") | **curve25519-sha256**, mlkem768x25519, sntrup761x25519, DH-GEX | **ssh-ed25519**, rsa-sha2-256 | **aes256-ctr** | hmac-sha2-256 | DeuceSSH README; SyncTERM `CHANGES` |
| OpenSSH, PuTTY, Windows' OpenSSH | curve25519 first, P-256 too | Ed25519, ECDSA, RSA | CTR and GCM | SHA-2 | common knowledge, and their defaults overlap both rows above |

- **No single kex and host-key pair covers both SyncTERMs.** Old SyncTERM
  has no 25519; new SyncTERM has no ECDSA. The cheap common ground is
  **two host keys (Ed25519 and ECDSA P-256) and both key exchanges**, which
  is what was measured. RSA would also cover both with one key, at the
  cost of a first-boot RSA-2048 key generation measured in seconds and a
  bigger build; not proposed.
- **P-256 costs about 29.5 KB of the 115 KB** (map: `ecc.c` 12,866,
  `sp_int.c` 8,950, `sp_c32.c` 7,708; estimate, because the 25519-only
  build does not compile, §1.1). It buys every SyncTERM in circulation.
- **Unverified and worth a bench check:** that a released SyncTERM build
  actually offers `ecdh-sha2-nistp256` and `ecdsa-sha2-nistp256` (the
  cryptlib defaults say so; the shipped binary was not tested). Also PuTTY
  and SyncTERM against `none` authentication (§5.5).
- SHA-1 is compiled out (`NO_SHA`, saves 5,264 bytes, measured). Nothing
  in the offered set uses it.

---

## 3. RAM

### 3.1 Static

Measured off each ELF as `_bss_end - 0x3FC88000` against 341,760 (the S3's
`dram0_0_seg` from its generated `memory.ld`, IRAM sharing the segment;
method as in `internal/memory-2026-09-25-1.1.1.md` §1).

| Build | Static used | Free | Delta |
|---|---:|---:|---:|
| Baseline, fresh S3 defaults | 247,784 | 93,976 | |
| wolfSSH, SW crypto (recommended) | 248,096 | 93,664 | **+312** (304 are the probe's key buffers) |
| wolfSSH, HW crypto | 248,136 | 93,624 | +352 |
| libssh 0.12.2 | 257,904 | 83,856 | **+10,120** |
| LibSSH-ESP32 0.11.5 | 257,952 | 83,808 | +10,168 |

### 3.2 Per session (estimate, reasoning shown)

The host measurement planned for this (wolfSSH on WSL, an OpenSSH client
on 127.0.0.1, counting allocations) could not run: the machine's guard
refuses an `ssh` client invocation. So these are struct sizes measured on
the target (`sizeof`, read back with `nm -S` from an object built with the
exact settings) plus wolfSSH's own buffer rules from its source.

| Piece | Bytes | When |
|---|---:|---|
| `WOLFSSH` | 6,548 | whole session |
| `WOLFSSH_CHANNEL` | 84 | whole session |
| channel receive buffer = `DEFAULT_WINDOW_SZ` (`ChannelNew`, internal.c ~2939) | 2,048 recommended (8,192 as built; stock is 128 KB) | whole session |
| input buffer, grows to the largest packet received (client KEXINIT about 1 to 1.5 KB, data capped by our `DEFAULT_MAX_PACKET_SZ`) | ~2,200 | whole session |
| output buffer, grows to the largest packet sent (one board write) | ~3,200 if the board hands over up to 3 KB at a time | whole session |
| pty modes string | ~100 | whole session |
| `HandshakeInfo` | 2,880 | key exchange and rekey only |
| keys and wolfCrypt temporaries (`ecc_key` 320, `ed25519_key` 328, `curve25519_key` 84, `Hmac` 552, `Aes` 320, small-stack heap scratch) | ~3,000 to 5,000 | key exchange |
| **Steady** | **~14 KB** | |
| **Peak** | **~20 KB** | |

- **All of it can be PSRAM.** The build defines `XMALLOC_USER` and supplies
  `XMALLOC/XFREE/XREALLOC` as `heap_caps_*_prefer(SPIRAM, INTERNAL)`;
  wolfSSH's `WMALLOC` maps to `XMALLOC` (`wolfssh/port.h:74`). It links. A
  first attempt with `XMALLOC_OVERRIDE` was silently undone by the
  FreeRTOS block in `settings.h` (~1693), which redefines `XMALLOC` to
  `wc_pvPortMalloc`; `XMALLOC_USER` is the switch that holds.
- Ten SSH callers at 20 KB is 200 KB of 8 MB PSRAM. RAM is not what limits
  the SSH line count on the S3; sockets are (§3.4).
- lwIP's per-socket memory already prefers PSRAM on this profile
  (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`).
- An SSH caller uses an ordinary node from the static pool, so no new
  `Session` RAM at all. That is the "a way to connect, not lines" line in
  CLAUDE.md, and it is free.
- The window: 2 KB is plenty. Client-to-board traffic is keystrokes and
  XMODEM/YMODEM blocks of at most 1 KB; a 2 KB window over a LAN round trip
  of a few ms is hundreds of KB/s, far above anything the board takes in.
  Throughput board-to-client is governed by the client's window, not ours.

### 3.3 Internal RAM that cannot trivially move

- **One SSH task stack.** Estimate 8 to 12 KB peak with `WOLFSSL_SMALL_STACK`
  and `WOLFSSL_SP_NO_MALLOC` (SP math keeps its points on the stack);
  libssh's own measurement was 4.7 KB. Give it 12 KB to 16 KB and read
  `uxTaskGetStackHighWaterMark` after ten connections of each key type.
  The S3 had 77,739 internal heap free, 68,031 at its lowest (dev.8), so
  16 KB internal is affordable.
- **It can go to PSRAM instead**, with one hard rule.
  `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY` is already `y` on the S3
  (default y in `esp_psram/esp32s3/Kconfig.spiram:41`, "should only be used
  for tasks where the stack is never accessed while the cache is
  disabled"), and 5.3.1 has `xTaskCreatePinnedToCoreWithCaps`. The rule:
  **that task never touches flash** (no LittleFS read or write, no NVS),
  because any flash operation disables the cache. So the host keys are read
  and written by the BBS task and handed over in memory. Cost: crypto on a
  PSRAM stack runs somewhat slower, which only lengthens the connect.
- Recommendation: internal stack for the first preview (simpler, one less
  rule to break), PSRAM stack only if the internal heap is ever short.

### 3.4 Sockets: the real ceiling, already at it

`LWIP_MAX_SOCKETS` is 16 and cannot be raised in 5.3.1 (CLAUDE.md). Counted
from the code (`socket(` appears in `bbs.cpp`, `backup.cpp`,
`announce.cpp`; mDNS is not socket-based here, `CONFIG_MDNS_NETWORKING_SOCKET`
is not set):

| Holder | Sockets |
|---|---:|
| telnet listener | 1 |
| ten nodes + the busy line | 11 |
| the overflow caller, accepted to be told BUSY | 1, briefly |
| announce, while it posts | 1, briefly |
| backup window: listener + one client | 2, while open |
| **worst case today** | **16** |
| **an SSH listener** | **17** |

- Each SSH caller replaces a telnet caller on the same node pool, so only
  the listener is new. In the corner where every node is busy, somebody is
  being told BUSY, announce is posting and the backup window is open, one
  `socket()` or `accept()` fails. Rare, but it fails silently the way the
  old 24-socket setting did.
- CLAUDE.md says "the listener, mDNS and SNTP take three". In this
  configuration mDNS and SNTP use lwIP's raw API, not sockets, so that line
  looks stale; the board's own count (a `lwip_socket` high-water in SYS)
  would settle it rather than either document.
- Two ways through: accept the corner and log it, or **share port 6400**
  (§5.4), which needs no listener socket at all.

---

## 4. Does it fit, and the partition layout

### 4.1 Flash, measured

All builds `pio run -e ws_s3_lcd147` in the worktree, fresh generated
`sdkconfig`, same toolchain, the new S3 table below.

| Build | `firmware.bin` | vs baseline | Left in 1.5 MB | Left in 3 MB |
|---|---:|---:|---:|---:|
| Baseline, 1.1.2 main (e656696) | 1,270,240 | | 302,624 | 1,875,488 |
| wolfSSH, HW crypto | 1,396,528 | +126,288 | 176,336 | |
| wolfSSH, SW crypto | 1,390,624 | +120,384 | 182,240 | |
| **wolfSSH, SW, no SHA-1 (recommended)** | **1,385,472** | **+115,232** | **187,392** | **1,760,256** |
| LibSSH-ESP32 0.11.5 | 1,542,304 | +272,064 | 30,560 | |
| libssh 0.12.2 (IDF component) | 1,548,272 | +278,032 | 24,592 | 1,597,456 |

(The brief's 1,270,224 is the same tree on the old table; the 16 bytes are
the header and table.)

Where the wolfSSH bytes go (map, recommended build):

| Object | .text | .rodata |
|---|---:|---:|
| wolfSSH `internal.c` | 27,031 | 1,178 |
| `aes.c` (CTR, GCM, tables) | 10,512 | 8,488 |
| `ecc.c` | 12,354 | 512 |
| `sha512.c` (Ed25519 needs it) | 9,548 | 640 |
| `sp_int.c` + `sp_c32.c` (P-256 math) | 16,134 | 524 |
| `asn.c` (key DER in and out) | 7,045 | 480 |
| `random.c` | 3,359 | 384 |
| Ed25519 + curve25519 (low-mem) | 7,583 | 986 |
| the rest | 6,138 | 256 |
| **Total** | **99,704** | **13,448** |

The probe itself is 795 bytes of flash. It fits today's slot with room to
spare; the bigger slots below are for what comes after it.

### 4.2 Proposed `partitions_s3.csv` (selected by the S3 envs only)

```
# Name,    Type, SubType, Offset,   Size
nvs,       data, nvs,     0x9000,   0x6000
otadata,   data, ota,     0xf000,   0x2000
phy_init,  data, phy,     0x11000,  0x1000
ota_0,     app,  ota_0,   0x20000,  0x300000
ota_1,     app,  ota_1,   0x320000, 0x300000
logs,      data, spiffs,  0x620000, 0x8000
userdata,  data, spiffs,  0x628000, 0x158000
storage,   data, spiffs,  0x780000, 0x80000
```

Ends at exactly 0x800000 (8 MB). Reasons, row by row:

- **bootloader 0x0 (S3), table 0x8000, nvs, otadata, phy_init: unchanged.**
  The S3 bootloader is 20,928 bytes, well inside 0x8000. `phy_init` is not
  used (`CONFIG_ESP_PHY_INIT_DATA_IN_PARTITION` is off) but the space up to
  0x20000 is alignment padding for the first app anyway, so it costs
  nothing to keep the two tables alike.
- **ota_0 and ota_1, 3 MB each.** The app is what grows: the WROOM image
  grew 103 KB in 1.1.0 alone, SSH is +125 KB, and anything later that wants
  TLS on the S3 (the social-posting and OTA-from-GitHub ideas) is another
  100 KB class of code. 1.39 MB with SSH leaves 1.76 MB of headroom, about
  fifteen minor versions at the 1.1.0 rate. Two slots, because the
  self-update in the queue needs both. 2.5 MB slots would give 1 MB more
  data; data is the thing that has not grown (below).
- **logs 32 KB, unchanged.** It holds two fixed rings (`BBS_CALLLOG_SIZE` 50
  and `reboots.log`) whose size does not follow the partition.
- **userdata 1,376 KB (was 608 KB).** Accounts at about 450 bytes each,
  mail, information pages, rings, and the backup restore's `.staging`
  (up to a whole 256 KB zip). 608 KB was about 4% used; this is room for
  forums or files on a cardless S3 later without another move.
- **storage 512 KB (was 256 KB), last.** Screens are 18.5 KB stock;
  `SCREENS INSTALL` (1.1.1) copies a sysop's card screens into it, and a
  full ANSI art set is the thing that would fill it. **One coupling to
  move with it:** the backup zip carries the screens and is capped at
  `BBS_ZIP_TOTAL_MAX` 262,144 unpacked (config.h:369). A storage partition
  filled past 256 KB makes a backup that cannot be restored. Either raise
  the cap on the S3 profile (staging room is there now) or say that the
  extra 256 KB is for installed screens, not for backups.

### 4.3 The four checks

- **(a) `uploadfs` still hits `storage`.** PlatformIO's `fetch_fs_size`
  (`espressif32@6.9.0/builder/main.py:204`) takes the *last data row of
  subtype spiffs/fat/littlefs*; app rows never count. `buildfs` on this
  table produced a 524,288-byte `littlefs.bin`, the storage partition's
  size. **And a trap Rob's working proposal would have hit:** IDF 5.3.1's
  `gen_esp32part.py` (lines ~196 to 206) rejects any row whose offset is
  below the end of the previous row ("Partitions overlap"), so rows must
  be in ascending offset order. App slots above 4 MB would have had to be
  written after `storage`; that still works for `uploadfs` for the reason
  above, but the clean layout avoids the question.
- **(b) Flash size.** `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` in
  `sdkconfig.defaults.esp32s3` and `board_upload.flash_size = 8MB` in both
  S3 envs. Built: both the bootloader and app headers carry 8 MB (byte 3,
  high nibble 3). **This is required, not tidy:** 5.3.1's
  `esp_flash_spi_init.c:401-403` refuses to start ("Detected size ...
  smaller than the size in the binary image header ... Probe failed") when
  the header claims more than the chip has, so today's 16 MB header would
  not boot on an N8R8. The other way round is a warning and uses the header
  size (`:406-410`). Consequence: `plat::hardware()` reports
  `esp_flash_get_size()`, which is the header size by design (the comment
  at `platform_esp32.cpp:255-262` says so and keeps it on purpose), so the
  Waveshare's badge goes from 16 MB to 8 MB. If Rob wants the chip's real
  size shown, `esp_flash_get_physical_size()` exists in 5.3.1
  (`esp_flash.h:171`); that is his call against the comment's reasoning.
- **(c) How the app offset is passed.** Nobody assumes 0x10000. PlatformIO
  asks IDF's parttool for the default boot partition
  (`frameworks/espidf.py:1128`), measured as `application_offset: 0x20000`
  on this table, and flashes `ota_data_initial.bin` at 0xF000. `release.py`
  checks one global table (`EXPECT = {"otadata": 0xF000, "ota_0": 0x20000,
  "storage": 0x3C0000}`, `SLOT_MAX = 0x180000`, lines 130-131) and writes
  the manifest from `EXPECT`; the directory's `server.py` has its own
  global `FLASH_PARTS` (lines 313-317, storage at 0x3C0000). **With this
  layout only the storage offset changes for the S3** (0x3C0000 →
  0x780000) and `SLOT_MAX` for it (0x300000). Both scripts need the table
  per image set, not per program:
  - `release.py`: a `table` key per `BUILDS` entry, `check_partitions()`
    per table, and `EXPECT`/`SLOT_MAX` derived from that table rather than
    typed (a count written beside a table again, otherwise).
  - `server.py`: take the offsets from the `partitions.bin` inside each
    image set (32-byte entries, magic 0xAA50, label at offset 12). That is
    the only way an older S3 release still installs at its own offsets and
    a 1.1.2 one at the new ones, with nothing to keep in step. The site's
    self-test (`selftest.py` ~5580-5776) asserts the S3's offsets and moves
    with it.
- **(d) otadata on a board that has booted the other slot.** The installer
  writes `ota_data_initial.bin` (8 KB of 0xFF, built by PlatformIO) at
  0xF000 on every install and update; blank otadata makes the bootloader
  boot `ota_0` by subtype. If otadata were left behind (a hand flash of
  `firmware.bin` alone), the bootloader looks the chosen slot up in the
  *new* table, and on an invalid image falls through to the others.
  Rollback is off (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` not set), so no
  pending-verify state can strand it.

### 4.4 What the first 1.1.2 S3 install must be, and why not Update

The site's non-erasing Update writes the table, otadata, the app and
`storage.bin` and leaves the rest. Across this layout change it would
"work": `logs` and `userdata` now sit where nothing was ever written, the
mount fails, `main.cpp:261` formats them (`format_if_mount_failed = true`),
and the board comes up fresh. Two reasons not to allow it anyway:

- It is silent. A sysop pressing Update and getting a blank board is the
  worst version of an intended erase.
- **The old userdata blocks, password hashes included, are left readable**
  inside the new `ota_1` range (0x328000 onwards) until the first
  self-update overwrites them. The factory reset erases rather than formats
  for exactly this reason (CLAUDE.md, Phase 2).

So the site should route an S3 on a pre-1.1.2 version to Install with erase,
and say so. Nothing on the ESP32 side moves.

### 4.5 A stale config in the main checkout

`sdkconfig.ws_s3_lcd147` in the main checkout is dated 2026-09-25 10:01,
before `e936a65` turned IPv6, SoftAP and Enterprise off in
`sdkconfig.defaults`, and it still says `CONFIG_LWIP_IPV6=y` and
`CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y`. A value PlatformIO already holds wins
over the defaults, so an S3 built in the main checkout is not the image a
release (fresh checkout) builds, by roughly the 70 KB the 1.1.1 report
measured for those items. Delete it before the next S3 dev build, and check
the other generated `sdkconfig.*` the same way.

---

## 5. How it plugs into the board

### 5.1 Where the socket is used today

`Session::fd` is read and written directly in a handful of places in
`src/core/bbs.cpp`: `select()` set-up (~409-411), `readSession` `recv`
(~926), `sessSend` `send` (~110) and the SNOOP mirror (~117), `DirectSink`
for telnet replies (~97, ~937, ~1167), `getsockname` for the local-address
check (~551), `acceptAll`/`openSession` (~613-699), and the close path.
Output all funnels through `sessSend` (the Timeline's pump callback) and
input through `readSession`; that is the seam.

### 5.2 The shape

- **A `Link` in the Session**: TCP (today) or SSH. The SSH kind holds two
  single-producer single-consumer rings in PSRAM (board to caller, caller
  to board, 2 to 4 KB each, allocated by the SSH task per connection, never
  by the loop) and one **`eventfd`**. IDF 5.3.1 has `esp_vfs_eventfd`
  (`vfs/include/esp_vfs_eventfd.h`), VFS `select()` is on
  (`CONFIG_VFS_SUPPORT_SELECT=y`, `LWIP_USE_ONLY_LWIP_SELECT` off), so the
  loop's existing `select()` wakes on SSH input exactly as it does on a
  socket. An eventfd is a VFS descriptor, not an lwIP socket, so it does
  not count against the 16.
- **Five call sites learn the kind**: `readSession` pops the ring instead of
  `recv`; `sessSend` pushes instead of `send` (a full ring sets `wantWrite`
  and retries next pass); `DirectSink` does nothing for SSH (no telnet on
  that link); `closeSession` marks the ring closed and signals the SSH task
  instead of `close()`; the SNOOP mirror writes to whatever link the
  snooper has.
- **Telnet stays out of the SSH path**: `s.tn.setEnabled(false)` and the
  terminal's `iacEsc_` false, or every 0xFF in an XMODEM/YMODEM block is
  doubled by `Term::raw` (`term.cpp:276-281`) and the screen player
  (`screens.cpp:225`) and uploads/downloads over SSH corrupt. This is the
  one integration trap that a test with text alone would never see; the
  lrzsz check should run over SSH too.
- **The SSH task** owns the SSH listener (or takes sniffed sockets, §5.4),
  runs wolfSSH non-blocking over `select()` on its own sockets plus one
  eventfd for "the board wrote something", and pumps rings both ways,
  coalescing what the Timeline paces out (at a low BAUD setting the
  Timeline sends a byte a frame, and each SSH packet carries about 40 to 55
  bytes of framing and MAC, so the task should gather for a few ms rather
  than send a packet per byte).
- **Handoff to a node**: once the channel has a shell, the SSH task posts
  `{ring pair, eventfd, peer address, cols, rows}` to a queue and pokes the
  loop's eventfd. A new `acceptSsh()` beside `acceptAll()` runs the same
  checks in the same order: ban, shutdown, restore hold, free node, busy
  line, BUSY. The peer address is the real one, so bans, the caller log,
  WHO and the "default password only from a local address" rule
  (`getsockname`, `Bbs::localAddr`) all keep working.
- **Refuse before spending CPU**: the loop publishes a free-node count
  atomically; the SSH task checks it and the ban list decision (by asking
  the loop, not by reading its table) before running a key exchange, and
  runs at most one or two handshakes at a time. A kex flood then costs
  bounded CPU in a task that is not the loop.
- **BUSY over SSH** needs a thought: RFC 4253 §4.2 lets the server send
  lines before its version string, but OpenSSH does not show them. The
  visible way is a `SSH_MSG_USERAUTH_BANNER` or a `DISCONNECT` reason after
  the key exchange, which costs one kex of CPU on a board that is full.

### 5.3 pty-req: terminal type and size

- wolfSSH 1.5.0 parses the pty request (`internal.c:9544-9579`) and passes
  **columns and rows** to `wolfSSH_SetTerminalResizeCb`. That setter only
  exists when `NO_FILESYSTEM` is **not** defined (`ssh.c:1437`), which is
  why the recommended build leaves the filesystem layer compiled (unused
  file helpers are dropped by the linker). Live resize (`window-change`) is
  compiled only with both `WOLFSSH_TERM` and `WOLFSSH_SHELL`
  (`internal.c:9581`); `WOLFSSH_SHELL` gates nothing else in the library,
  so define it.
- **The TERM name is parsed into a local and only logged** in 1.5.0. So
  "SSH callers skip the detection probe" (CLAUDE.md's SSH entry) is not
  free with this library: without TERM the board cannot tell SyncTERM
  (wants CP437) from OpenSSH or PuTTY (want UTF-8). Keep the existing CPR
  probe with its UTF-8 test glyph on the SSH path, skip the PETSCII and
  ASCII fallbacks (an SSH client is always an ANSI terminal), and take the
  geometry from the pty request. wolfSSH's master branch adds
  `wolfSSH_CTX_SetChannelReqAnyCb`, which hands over the raw pty-req (TERM
  included); adopt it when it is in a release, rather than patching.

### 5.4 One port for both (the idea nobody has considered)

RFC 4253 §4.2: "When the connection has been established, both sides MUST
send an identification string." A conforming client sends `SSH-2.0-...`
immediately. The board already waits 300 ms for the connection to settle
before it probes. If the first bytes are `SSH-2.0-`, hand the socket and
those bytes to the SSH task (a custom wolfSSH receive callback replays them)
instead of starting telnet detection. What it buys:

- no listener socket (the 17th in §3.4 disappears);
- one port forward for both (Rob's point about routers that can only forward
  a port to the same port);
- nothing new to configure.

What it risks: a client that waits for the server's version line before
sending its own would sit through the telnet probe and fail. OpenSSH does
not wait; PuTTY and SyncTERM were not checked. Bench it with the three
before choosing it; a separate `ssh_port` is the safe default for the
preview either way.

### 5.5 Authentication

- Accept SSH `none` authentication (`WOLFSSH_ALLOW_USERAUTH_NONE`, a
  compile flag in 1.5.0, `internal.c:6854`) and let the BBS login run inside
  the encrypted channel exactly as over telnet: same handle prompt, same
  lockouts and bans, same staff elevation. SyncTERM supports `none` since
  1.2 (its CHANGES); OpenSSH tries `none` first.
- Checking the SSH password against `users.txt` in the SSH task is the
  wrong shape: it reads flash (forbidden on a PSRAM stack, §3.3), runs
  1,000 SHA-256 rounds off the loop's accounting, and duplicates the
  lockout logic. Not worth it for a preview.

### 5.6 Host keys

- Generated on first start (Ed25519 and P-256), stored in
  `<userdata>/ssh/` (about 150 bytes each), read and written by the BBS task
  and handed to the SSH task in memory. Shown in SYS with their
  fingerprints so a sysop can publish them.
- Rob's call: in the backup zip or not. In it, a board moved to new
  hardware keeps its identity and callers see no warning; out of it, like
  `wifi.last`, a downloaded backup cannot impersonate the board. The zip
  already carries the Wi-Fi password over plain HTTP on the LAN.

---

## 6. Rule no. 1: what can block the loop

### 6.1 Key exchange CPU

- Published, ESP32-S3 at 240 MHz, CycloneCRYPTO with `-O3` (Oryx's S3
  benchmark, the best primary figures found): X25519 14 to 15 ms per
  operation, Ed25519 26 and 24 ms, ECDH P-256 about 63 ms per step, ECDSA
  P-256 73/66 ms software, 67/60 ms with the hardware.
- wolfCrypt here will be slower than that: `settings.h:3264-3274` forces
  `CURVE25519_SMALL` and `ED25519_SMALL` on every Xtensa build ("Compilers
  for Xtensa have been seen to compile C code into non-constant time
  assembly code"), and the build is `-Os`. The map confirms the low-memory
  implementations (`fe_low_mem.c`, `ge_low_mem.c`) are what links.
- **Estimate: 0.2 to 0.6 s of CPU per connection** (two scalar
  multiplications and one signature on either curve). Not measured; the
  bench test below measures it.
- Where it runs decides whether it matters. **In the BBS loop it would be a
  guaranteed slow pass of several hundred ms for every caller on every SSH
  connect. In its own task it is invisible to the loop.** Recommended: a
  task on core 0 at a priority just above idle (2): Wi-Fi (23), lwIP (18)
  and the timer task preempt it, so the radio does not notice, and core 1
  stays the loop's and the camera worker's. A 0.6 s burst holds off IDLE0
  well inside the 30 s watchdog.
- Per-byte cost afterwards is small: software AES and SHA-256 run at about
  2 MB/s on this core (Oryx), so a 3 KB screen redraw is about 3 ms of CPU,
  in the SSH task.

### 6.2 Hardware crypto: turn it off in wolfCrypt

wolfSSL's Espressif port drives the SHA and AES peripherals under its own
mutexes (`esp32_sha.c` `esp_CryptHwMutexLock(&sha_mutex ...)`, `esp32_aes.c`
`&aes_mutex`). The IDF's mbedTLS drives the same peripherals under
`esp_crypto_sha_aes_lock_acquire()` (`mbedtls/port/sha/dma/sha.c:62`,
`port/aes/dma/esp_aes.c:39`), and the Wi-Fi supplicant uses that mbedTLS
with hardware SHA/AES on this board (`CONFIG_ESP_WIFI_MBEDTLS_CRYPTO=y`,
`CONFIG_MBEDTLS_HARDWARE_SHA/AES=y`). Two locks on one peripheral means a
Wi-Fi rekey or reconnect during an SSH handshake can interleave register
writes. Rare, and the kind of fault that shows up as one failed login a
week. Software crypto costs nothing measurable at BBS rates and saves
5,904 bytes. LibSSH-ESP32's README gives the same advice for its own case.

### 6.3 Bench tests before the preview ships

- Time `wolfSSH_accept()` on the S3 for each key type (log the
  microseconds), ten connections back to back: SYS's slow-pass count and
  loop worst must not move.
- `uxTaskGetStackHighWaterMark` of the SSH task after those ten.
- Internal heap free and lowest (SYS) with five SSH callers on.
- Ping p90 to the board during the ten connects (radio unaffected).
- Clients: OpenSSH, PuTTY, Windows OpenSSH, SyncTERM 1.8/1.9 (P-256 path)
  and a SyncTERM 1.10 nightly if one exists (25519 path); `none` auth on
  each; the port-sharing sniff (§5.4) on each.
- XMODEM and YMODEM up and down over SSH with `tools/lrzsz_check.py`
  adapted to the SSH link (the 0xFF doubling trap, §5.2).

---

## 7. Risks, what it gives up, and how to know

| Item | Gives up | Risk | How you would know |
|---|---|---|---|
| wolfSSH in the S3 image | ~125 KB of flash of a 3 MB slot | Low. Mature, GPL, maintained (1.5.0 April 2026), but it has had critical CVEs in 2025; pin and follow its ChangeLog | release notes of wolfSSH; a CVE there is an S3 patch release here |
| Both P-256 and 25519 | ~29.5 KB | none | old SyncTERM connects |
| Software crypto | ~2 MB/s instead of the peripheral's | none at BBS rates | |
| PSRAM for all SSH heap | slower crypto than internal | Low | connect time in the bench log |
| SSH task stack internal, 16 KB | 16 KB of ~68 KB lowest internal heap | Low | SYS heap lowest; stack high-water |
| Extra listener socket | the 16th/17th socket corner | Medium in that corner only | an `accept` failure in the log with the backup window open on a full board; or share the port |
| New S3 table | one erase on every S3; badge says 8 MB | none for boards in the field (none exist) | first boot formats; badge |
| Storage 512 KB | backups of a storage past 256 KB do not round-trip until the zip cap moves | Medium if forgotten | a restore refused on size |

---

## 8. Against the previous report (`internal/memory-2026-09-25-1.1.1.md`)

- **S3 image 1,328,592 (1.1.1-dev.1) → 1,270,240 now, -58 KB**, and static
  249,712 → 247,784 (-1,928): the IPv6, SoftAP and Enterprise items from
  that report were taken in 1.1.1-dev.4 (`sdkconfig.defaults:198-205`).
  In the main checkout the stale generated config hides this (§4.5).
- **Not taken, and still true on the S3:** newlib nano formatting
  (`# CONFIG_NEWLIB_NANO_FORMAT is not set`, about 69 KB of flash, and
  about 640 bytes off every formatted-output stack chain, which the SSH
  task would also benefit from), silent assertions (about 60 KB), and item
  7, the S3's Wi-Fi static RX/TX at 16/16 with a 16 block-ack window
  (`CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=16`, `STATIC_TX_BUFFER_NUM=16`,
  about 19 KB of internal RAM). That report said to take item 7 "before SSH
  starts asking for heap". With SSH's heap routed to PSRAM it no longer has
  to come first; it is still 19 KB of internal RAM for nothing.
- That report's §4.5 (`.bss` in PSRAM on the S3, 98 KB) is also not needed
  for SSH: nothing SSH adds is static.

---

## 9. Reproducing the figures

Worktree `release-prep/wt-ssh-research` (detached at e656696), changes:
`partitions_s3.csv` and the S3 env pointing at it with `8MB`,
`CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y`, `components/wolfssh_min/`
(CMakeLists and `user_settings.h`, sources from wolfSSL `v5.9.4-stable` and
wolfSSH `v1.5.0-stable` clones in the session scratchpad),
`src/ssh_probe.cpp` (the server shape), one call in `main.cpp`, and
`-DSSHR_PROBE` in the S3 env. Measurement: `firmware.bin` size;
`_bss_end - 0x3FC88000` from `xtensa-esp32s3-elf-nm`; per-object sizes from
`unleashed-bbs.map` by address range. The libssh builds used the same probe
shape on each library's API, swapped in one at a time (two components with
a `curve25519.c` each collide in PlatformIO's object directory).

Two things the machine's guard refused, reported rather than worked round:
`curl` to gitlab.synchro.net (read through WebFetch and the GitHub mirror
instead), and an `ssh` client in WSL for the host heap measurement (§3.2 is
an estimate because of it).

## Sources

- wolfSSH: [repository](https://github.com/wolfSSL/wolfssh), `LICENSING`, `ChangeLog.md` (1.5.0, 1.4.22 CVE-2025-14942), `src/internal.c`, `src/ssh.c`, `wolfssh/port.h`; registry [wolfssl/wolfssh](https://components.espressif.com/components/wolfssl/wolfssh)
- wolfSSL: [repository](https://github.com/wolfSSL/wolfssl) at `v5.9.4-stable`, `LICENSING`, `wolfssl/wolfcrypt/settings.h`, `wolfcrypt/src/port/Espressif/`; registry [wolfssl/wolfssl](https://components.espressif.com/components/wolfssl/wolfssl); [wolfSSL Espressif support](https://www.wolfssl.com/docs/espressif/)
- libssh: registry [david-cermak/libssh](https://components.espressif.com/components/david-cermak/libssh) and its `footprint.md`; [ewpa/LibSSH-ESP32](https://github.com/ewpa/LibSSH-ESP32); [jimmyw/ssh_cli_server](https://components.espressif.com/components/jimmyw/ssh_cli_server); [valdanylchuk/breezy_ssh](https://components.espressif.com/components/valdanylchuk/breezy_ssh)
- CycloneSSH: [repository](https://github.com/Oryx-Embedded/CycloneSSH); [Oryx ESP32-S3 crypto benchmark](https://www.oryx-embedded.com/benchmark/espressif/crypto-esp32-s3.html)
- cryptlib: [repository](https://github.com/cryptlib/cryptlib) (`session/ssh2_algo.c`, `misc/config.h`, README); Synchronet [GitHub mirror](https://github.com/SynchronetBBS/sbbs) `3rdp/build/cl-*.patch`, `src/syncterm/CHANGES`
- DeuceSSH: [repository](https://github.com/andy5995/DeuceSSH)
- [RFC 4253 §4.2](https://www.rfc-editor.org/rfc/rfc4253#section-4.2)
- ESP-IDF 5.3.1 on disk: `components/partition_table/gen_esp32part.py`, `components/spi_flash/esp_flash_spi_init.c`, `components/spi_flash/include/esp_flash.h`, `components/esp_psram/esp32s3/Kconfig.spiram`, `components/vfs/include/esp_vfs_eventfd.h`, `components/mbedtls/port/`, `components/soc/esp32s3/include/soc/ext_mem_defs.h`
- PlatformIO `espressif32@6.9.0`: `builder/main.py` (`fetch_fs_size`), `builder/frameworks/espidf.py` (`get_app_partition_offset`, otadata)
