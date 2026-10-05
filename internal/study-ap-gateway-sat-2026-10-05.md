<!--
µnleashed BBS: internal/study-ap-gateway-sat-2026-10-05.md

An AP gateway satellite: an open Wi-Fi access point that pops a captive
portal on a phone, lists the boards it can reach, and carries the caller
into one of them. Research and a plan. No code.

Copyright 2026 - Robert Mech
License: GNU General Public License v3 or later
SPDX-License-Identifier: GPL-3.0-or-later

Working notes for µnleashed BBS. See the LICENSE file for terms.
-->

# The AP gateway sat: a phone, an open AP, and a way in (2026-10-05)

Applies to versions: firmware 1.2.1 (the tree as it stands), looking at
1.2.3 (CALLIN and the caller-line core) and later. Nothing here is built.

Rob's words are the spec (2026-10-05):

> where are we at with repeater gateway esp32 networks. Id like to be able
> to have a satellite be an AP and provide a gateway to the board directly
> in fact even having a improv(probably wrong name) version that announces
> the BBS's when the AP is open, people connect and the "click here to
> login" then shows the BBS systems on the ESPNOW network. Then someone
> could say plant a few dozen around a fairgrounds and people could
> connect.

Plus three later additions the same day: repeating is part of CALLIN's
design rather than a later bolt-on; a base ESP32 must be able to be the
sat; and price every encryption layer separately and say which one, if
any, forces an ESP32-S3.

- [0. Bottom line](#0-bottom-line)
- [1. Two names, and one of them is about to collide](#1-two-names-and-one-of-them-is-about-to-collide)
- [2. What was checked, and what I could not](#2-what-was-checked-and-what-i-could-not)
- [3. The ceilings, in order of how hard they are](#3-the-ceilings-in-order-of-how-hard-they-are)
- [4. Channels, and the fairground with no router](#4-channels-and-the-fairground-with-no-router)
- [5. The captive portal, mechanically](#5-the-captive-portal-mechanically)
- [6. The terminal on the phone](#6-the-terminal-on-the-phone)
- [7. Encryption: three hops by two board classes](#7-encryption-three-hops-by-two-board-classes)
- [8. Latency: what a caller feels](#8-latency-what-a-caller-feels)
- [9. Repeating, and the fan-out that answers the scale question](#9-repeating-and-the-fan-out-that-answers-the-scale-question)
- [10. Airtime at a fairground, and whether the LR test is urgent](#10-airtime-at-a-fairground-and-whether-the-lr-test-is-urgent)
- [11. What a sat offers and how a caller picks](#11-what-a-sat-offers-and-how-a-caller-picks)
- [12. The failure modes, which are the interesting part](#12-the-failure-modes-which-are-the-interesting-part)
- [13. Against what is already planned](#13-against-what-is-already-planned)
- [14. Where this meets the broker study](#14-where-this-meets-the-broker-study)
- [15. Phases](#15-phases)
- [16. What Rob must decide](#16-what-rob-must-decide)
- [17. Sources](#17-sources)

---

## 0. Bottom line

**Build it, and build the cheap half first, because the cheap half is a
complete product and needs no new protocol at all.**

Start with the ceiling, because it governs the fairground picture and Rob
should not meet it late. **A board serves ten callers at a time and a
gateway caller takes one of the same ten.** `BBS_MAX_NODES` is 10
(`src/config.h:97`) and the session pool is a static array,
`Session nodes_[BBS_MAX_NODES]` (`src/core/bbs.h:1316`), at about 6,980
bytes each. So a few dozen APs around a fairground buy **coverage, not
capacity**: people can connect from anywhere on the site and then queue
for ten lines. Two things change that, and one of them is Rob's own
instinct in the quote:

- **Several boards is the real answer, and the numbers fit what is already
  built.** A sat already holds up to five boards, each with its own key
  (`Engine::kHosts = 5`, `src/core/link.h:295`). Five boards at ten lines
  is **fifty simultaneous callers**, and "shows the BBS systems on the
  ESPNOW network" is exactly the right front page for that, because the
  portal can show a caller which boards have a line free and let them pick.
- **The ten is softer than it looks for gateway callers.** Ten is the
  honest number for telnet because of the socket budget
  (`CONFIG_LWIP_MAX_SOCKETS=16`), and **a
  CALLIN session uses no socket at all**: it arrives as ESP-NOW frames. So
  for a board fed only by gateways, the limit is static DRAM and one
  `static_assert`, not the socket cap. The next wall is at **14 caller
  lines**: `src/core/bbs.h:1315` asserts `BBS_MAX_NODES + 2 <= 16` because
  `takeTraffic` keeps a bit per session in a 16-bit word. Each line past 10
  costs about 6,980 bytes of static DRAM, so a WROOM (about 15 KB free)
  could afford two and an S3 (about 75 KB free) could afford all four to
  14. Past 14 needs a wider mask as well. **That is a real, cheap,
  figure-backed improvement and it is the first thing to reach for if ten
  is not enough.**

**And a finding that reaches well past this feature: the 16-socket wall is
a property of the pinned patch version, not of the ESP32.** CLAUDE.md
records it as "capped at 16 in IDF 5.3.1 on every chip", which is right for
5.3.1 and is no longer true of the 5.3 line. Verified twice, from two
frameworks on this machine:

```
framework-espidf@3.50301.0 (version.txt = 5.3.1), components/lwip/Kconfig:136
    config LWIP_MAX_SOCKETS
        int "Max number of open sockets"
        range 1 16
        default 10
        "The valid value is from 1 to 16."

framework-espidf@3.50503.0 (IDF 5.5.x), components/lwip/Kconfig:128
    config LWIP_MAX_SOCKETS
        range 1 253
        default 10
        "The practical maximum limit is determined by available heap memory
         at runtime... If using value above 61, update CMakeLists defining
         FD_SETSIZE..."
```

and the research traced the change to **v5.3.2**, a patch bump inside the
same 5.3 line. `LWIP_MAX_ACTIVE_TCP` and `LWIP_MAX_LISTENING_TCP` are
already `range 1 1024` with a default of 16 in 5.3.1, so those are defaults
rather than ceilings.

**This does not belong to this feature and should not be done inside it**,
because moving the pinned framework revalidates all 23 environments and
every board. But it means "ten caller lines is the ceiling and more RAM
does not raise it" is now a statement about a pin rather than about the
hardware, and CLAUDE.md's entry should say which. **Flagged for Rob as a
separate item**, and it is the most valuable thing this study found that
Rob did not ask about.

**The peer limit is not the wall I thought it was, and the reason is a
decision Rob already made.** Read out of the pinned framework
(`framework-espidf@3.50301.0`, `version.txt` = 5.3.1):
`ESP_NOW_MAX_TOTAL_PEER_NUM` is **20** and
`ESP_NOW_MAX_ENCRYPT_PEER_NUM` is **6** (`esp_now.h:49-50`); the Kconfig
`ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM` is `default 7`, `range 0 17`
(`esp_wifi/Kconfig:467-478`). The link registers its peers
**unencrypted** at the ESP-NOW layer and does its own AES-128-CCM (Rob,
2026-09-26), so the encrypted-peer ceiling does not apply — and that
sidestep is worth more here than anywhere else, because that same Kconfig
says in so many words: *"the espnow and SoftAP share the same hardware
keys... Maximum espnow encrypted peers number + maximum number of
connections of SoftAP = Max hardware keys number."* **Every encrypted
ESP-NOW peer would have cost the sat a phone.** Doing our own crypto is
why a gateway sat can hold its full allowance of phones. Our own
`kPeers = 8` (`src/core/link.h:294`) is Rob's figure, costs about **240
bytes a peer on the heap** (hand-summed from `Engine::Peer`, allocated
with `new Peer[npeers_]` at `link.cpp:432`, from PSRAM where the board has
it), so raising 8 to 20 costs about **2.9 KB of heap and no static DRAM at
all**, on a WROOM that already declares 20 KB for the link. 20 is then the
hard ESP-NOW wall.

**The channel rules.** A SoftAP picks its own channel; a station is forced
onto its router's; one radio means one channel for both. ESP-NOW peers are
registered per interface (`esp_now_peer_info_t.ifidx`) with `channel = 0`
meaning "whatever channel station or softap is on" (`esp_now.h:66-71`). So
an AP-only sat sets the channel and the board must be on it. **And that is
where the real blocker is, and it is on the board, not the sat:** the host
answers nothing while its station is not associated
(`if (!io_.associated()) return;`, `src/core/link.cpp:1104`), and worse,
`main.cpp` blocks in a wait loop and never reaches `Bbs::begin` without a
network at all (`main.cpp:1135-1150`), which is the trap that bit
Agentville, the 4.3B and the WS2 on 2026-10-01. **A board in a field with
no router is simply dark today.** The cheapest fix is not firmware: **let
the board join the gateway sat's AP as an ordinary station.** The sat
becomes the board's router, the channel is settled by construction, the
board gets DHCP and a working `Bbs::begin`, and ESP-NOW runs on the
station interface exactly as it does now.

**A base ESP32 can be the sat, Rob was right that I was measuring the
wrong device, and somebody has already shipped the exact thing on a 4 MB
part.** The arithmetic first: camsat's `firmware.bin` is **789,072 bytes**
against a `factory` partition of `0x3F0000` = **4,128,768 bytes** (one app,
no OTA, no data partition — `unleashed_camsat/firmware/partitions.csv`),
leaving **3,339,696 bytes free** on a 4 MB ESP32. Against that, the
measured payload:

| | Bytes |
|---|---|
| `@xterm/xterm` 6.0.0 `lib/xterm.js`, as shipped and already minified | **488,663** |
| the same file **gzipped**, measured | **120,632** |
| `@xterm/xterm` **5.5.0** gzipped | about **71,900** |
| `@xterm/addon-fit` gzipped | **592** |
| a whole page: terminal, addon, CSS and your own JS, gzipped | **about 86 KB (5.5.0) to 135 KB (6.0.0)** |

So **4% of a 4 MB sat's free flash**, and the 120,632-byte figure is not an
estimate: it is a `.gz` checked into a working ESP-IDF project next to its
488,936-byte original.

**Two current ESP-IDF projects already do all of this**, which is better
evidence than any size table: `ausil/esp32-web-terminal` serves xterm.js
from its **own app image** (`EMBED_FILES xterm.min.js.gz`, streamed in 4 KB
chunks with `Content-Encoding: gzip` set by hand), over **its own SoftAP**,
with a **WebSocket**, on an **ESP32-C3 Super Mini with 4 MB of flash** —
and `zvldz/ESP32-UART-Bridge` does the same on a 4 MB ESP32-S3-Zero, with
xterm.js **lazy-loaded** so an ordinary page view never pays for it.
Neither uses a CDN. Most hobby tutorials do, which is why they are useless
here. **The path is not speculative; it is in production on parts cheaper
than ours.**

**No S3 is needed to serve the terminal.** The sat also does not need
`@microsoft/dev-tunnels-ssh`, which is the larger half (74.5 KB gzipped
bundled, on top of xterm's 72 to 121): it needs a plain WebSocket, because
the sat-to-board hop is already sealed by the link.

**What a caller at a fairground is exposing.** The phone-to-sat hop is on
an **open access point and is in the clear**: anyone in radio range with a
laptop reads every keystroke and every screen, including a password. The
sat-to-board hop is sealed (AES-128-CCM, every frame, header as associated
data). So the weak hop is the first one, which is the opposite of the usual
telnet story where the weak part is the long haul.

**And on Rob's direct question — does securing it force an S3? No, and the
reason is better than a memory budget.** I expected to answer "yes, on
heap", and the research says otherwise: `ausil/esp32-web-terminal` runs
`esp_https_server` with an embedded certificate on a 4 MB ESP32-C3, capped
at four sessions. So TLS on a cheap sat is survivable at low concurrency
(one session's buffers are about 20.5 KB at IDF defaults:
`MBEDTLS_SSL_IN_CONTENT_LEN` 16384 plus `MBEDTLS_SSL_OUT_CONTENT_LEN`
4096, before context and handshake). **The reason not to do it is the
certificate, not the part.** A captive portal cannot intercept HTTPS at
all; a sat can only serve TLS under a name nobody has signed; so every
phone shows a full-page security warning before the caller reaches the
board. A front door that opens with "Attackers might be trying to steal
your information" is both a worse experience and a worse security message
than plain HTTP with an honest sentence. Section 7 has the full matrix.
The project already has the precedent and the voice for saying so: the
connection line.

**The LR bench test is now more urgent, but not for this.** A fairground
is planned topology, not a garden with a wall in it, so the answer is to
plant another box rather than to shout further; and LR costs callers'
latency on the host (29-55 ms gateway pings on the bench), which is Rule
no. 1 and much worse for a terminal than for a camera. **Do the LR test,
but as the relay decision it already was, and keep LR off on any board
carrying interactive callers.**

**Repeating composes for free, and this is the paragraph that answers the
scale question.** A relay forwards sealed frames it cannot open with a
two-byte wrapper (`internal/sat-types-2026-09-27.md` §1), which is below
the family layer, so **a repeater never learns what CALLIN is and CALLIN
never learns there is a repeater.** They are independent features that
compose, and neither needs to know about the other. The one thing that
does need stating, and it is a design rule rather than a cost: **a
repeater carrying a terminal must forward from its receive callback on its
own task, not on a poll tick.** On a 20 ms tick each hop adds about 40 ms
of round trip and the second hop is where a caller notices; event-driven, a
hop is a few milliseconds and three hops are still comfortable. Section 8
has the arithmetic.

### The plan, shortest proof first

| # | What | Repo | Days | Needs |
|---|---|---|---|---|
| **1** | **The local gateway.** One sat: open AP, DHCP, DNS hijack, captive portal, a page listing the boards, xterm.js over a WebSocket bridged to **telnet on the board's own IP**. The board joins the sat's AP as a station. No link, no CALLIN, no pairing, no new protocol. | new sat repo | **4 to 6** | one ESP32, one board, one phone |
| **2** | **Measure it.** Keystroke echo on a real phone, DTIM, how many phones hold a terminal, what the sat's heap does, the portal on iOS / Android / Windows. | — | **1 to 2** | the same, plus Rob's phone |
| **3** | **The caller-line core and CALLIN** (1.2.3 as planned): a session fed by a stream rather than a socket, `Session::link`, family 3. The serial sat and the ham dock reuse it unchanged. | core | **5 to 8** | host; one bench flash |
| **4** | **The distributed gateway.** The sat talks CALLIN over the link instead of telnet; several boards on the portal; the board list learnt from pairings; busy-line handling. | core + sat | **4 to 6** | two ESP32s, one board |
| **5** | **Raise the lines.** `kPeers` 8 to 20, `BBS_MAX_NODES` 10 to 14 on boards that can hold it, read the DRAM off the ELF. | core | **2 to 3** | host; bench |
| **6** | **The relay**, event-driven forwarding, one hop. | new or shared sat repo | **8 to 10** | three ESP32s, open ground |
| **7** | A fixed two-hop chain, if the bench says one hop is not enough. | same | **4 to 6** | four boxes |

Phase 1 is the smallest thing that proves the whole path end to end: one
sat, one board, one phone, in a room, with a caller logging in from a phone
with no app. **It is also shippable on its own**, which is why it is first.
Phases 1 and 2 are about a week and they settle every question that
research cannot.

---

## 1. Two names, and one of them is about to collide

**Rob means a captive portal, not Improv.** Improv Wi-Fi Serial is the
provisioning protocol the installer uses over USB to put an SSID into a
board; it has nothing to do with announcing anything to a phone over the
air. What he described — an open AP, a phone that pops "Sign in to
network", a page that lists what is behind it — is a **captive portal**.
That is the word, it is what the rest of this report uses, and that is the
last time it needs saying.

**Do not cite the 2026-09-22 "no captive portal" ruling against this.**
That entry in CLAUDE.md ("no need for captive because we can plug back in
and reconnect to reset or change wifi", and "do not propose either again")
was about provisioning **the board's own Wi-Fi credentials**, where the USB
cable already solved the problem and Improv listens for as long as the
board runs. It was a decision about a *provisioning* mechanism on the
*board*. This is a different feature in a different place for a different
person: a portal on a **sat**, as the front door for a **caller** who owns
nothing but a phone. The old ruling stands exactly as written and does not
reach this. Worth recording plainly, because the phrase "do not propose
either again" is the sort of thing a later reader will find by grep and
use to kill the wrong thing.

**And this feature satisfies Rob's strongest constraint of the same day
better than anything else on the roadmap.** From the entry committed hours
earlier (35f067c): *"The microcontroller must be able to do it with no
website"*, which "rules out any board-side design needing HTTP, TLS,
WebSocket or a browser". The AP gateway puts HTTP and a WebSocket on a
**sat** and none on the board; there is no website anywhere in it, no
droplet, no internet, no DNS that resolves off-site. A field, some boxes,
and people's phones. That is the constraint honoured rather than dodged,
and it is an argument for this feature over the broker work rather than
against it.

### The naming collision, flagged now rather than at the second meaning

Rob's words here say **"repeater"** for the ESP-NOW forwarding box. The
broker study running beside this one
(`internal/study-broker-sat-2026-10-05.md` §5.2) recommends **"repeater"**
for the internet-side rendezvous box, and does so carefully, having already
rejected "relay" on the grounds that *"'relay' is also taken: CLAUDE.md
lists relay as a future sat kind, 'one hop, not a mesh'"*.

So the two studies are about to ship one word for two things, which is
precisely the mistake CLAUDE.md records as costing several sittings over
"bulletin", with the rule attached: **rename at the second meaning, not the
fourth.** This is the second meaning. The recommendation:

- the ESP-NOW forwarding box keeps **relay**, or **relay sat**, as
  `sat-types-2026-09-27.md` already names it and as the CLAUDE.md sat-kinds
  list already has it. It is in radio range, it pairs over the air, it is a
  sat;
- **repeater** goes to the internet box, as the broker study proposes. It
  is not in radio range, it does not pair, a caller comes through it.

Either assignment works; both studies using the same word does not. This is
Rob's to settle in one line, and it costs nothing today and a rename later.

---

## 2. What was checked, and what I could not

| Claim | Verified how |
|---|---|
| ESP-NOW peer ceilings | **Pinned header**, `framework-espidf@3.50301.0/components/esp_wifi/include/esp_now.h:49-52`, with `version.txt` = 5.3.1 |
| Encrypted peers share hardware keys with SoftAP clients | **Pinned Kconfig**, `components/esp_wifi/Kconfig:467-478`, quoted verbatim in §3.3 |
| SoftAP client ceiling 15 on ESP32 / S2 / S3 | **Pinned header**, `components/esp_wifi/include/local/esp_wifi_types_native.h:24` (`ESP_WIFI_MAX_CONN_NUM (15)`; C3 is 10, C2 is 4) |
| SoftAP beacon and DTIM defaults | **Pinned header**, `esp_wifi_types_generic.h:342-344`: beacon 100 TU, `dtim_period` default 2, and a `csa_count` for channel switch announcements |
| IDF ships a captive portal example with a DNS redirector | **In the pinned tree**: `examples/protocols/http_server/captive_portal/`, with a `dns_server` component |
| iOS needs a body, not just a redirect | **The example's own source comment**: *"iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient."* |
| The example triggers the portal on Android, iOS and Windows, and will not redirect HTTPS | **The example's own README**, first paragraph |
| RFC 8910 (`Captive-Portal` DHCP option) is not in IDF 5.3.1 | **Grep of the pinned tree**: no `CAPTIVEPORTAL` or `captive` anywhere in `components/esp_netif/include` or `components/lwip/include`. So it would be hand-written DHCP option 114 |
| WebSocket server is in-tree, off by default | **Pinned Kconfig**, `components/esp_http_server/Kconfig:42-46`: `HTTPD_WS_SUPPORT`, `default n` |
| `esp_http_server` defaults | **Pinned header**, `esp_http_server.h:55-65`: `stack_size 4096`, `max_open_sockets 7`, `max_uri_handlers 8`; and `:187` says *"3 sockets are reserved for internal working of the HTTP server"* |
| One TLS session's buffers | **Pinned Kconfig**, `components/mbedtls/Kconfig:57-99`: `MBEDTLS_ASYMMETRIC_CONTENT_LEN` default y, `IN_CONTENT_LEN` 16384, `OUT_CONTENT_LEN` 4096, and the help text saying the asymmetric default *"saves 12KB of dynamic memory per TLS connection"* (16384+16384 → 16384+4096 is exactly 12,288) |
| The board answers nothing unassociated | **Source**, `src/core/link.cpp:1104`, and `src/plugins/link.cpp:157` wiring it to `esp_wifi_sta_get_ap_info` |
| A board with no network never starts the BBS | **Source**, `src/main.cpp:1135-1150` |
| 10 caller lines, pool is static, 14 is the next wall | **Source**, `src/config.h:97`, `src/core/bbs.h:1315-1316` |
| 16 sockets | **Source**, `sdkconfig.defaults:56` |
| 5 boards a sat, 8 peers a board, 16 sessions | **Source**, `src/core/link.h:294-297` |
| The link's crypto shape | **Source**, `src/core/linkcrypto.h`: AES-128-CCM, 16-byte key, 8-byte tag, 13-byte nonce, P-256 ECDH, HKDF |
| Link timing granularity | **Source**, `src/config.h:257` (`BBS_PLUGIN_FAST_MS 20`), `:271` (`BBS_SELECT_MS 10`), `src/core/link.h:306` (`kAckDelayMs 20`), `:304` (`kRtoMs 150`) |
| SSH is S3-only and its budget | **Source**, `src/config.h:411-437` (`BBS_HAS_SSH` default 0, `BBS_SSH_PSRAM_EACH` 48 KB, `BBS_SSH_PSRAM_KEEP` 128 KB, `BBS_SSH_STACK` 16384) and `src/board.h` (`BBS_HAS_SSH 1`, `BBS_SSH_MAX 8` in the S3 profiles only) |
| A sat firmware's real size and partition table | **Files on disk**: `unleashed_camsat/release/1.1.0/assets/firmware.bin` = 789,072 bytes; `unleashed_camsat/firmware/partitions.csv` |
| `LWIP_MAX_SOCKETS` is `range 1 16` at 5.3.1 and `range 1 253` later | **Both Kconfigs on this machine**: `framework-espidf@3.50301.0/components/lwip/Kconfig:136` and `@3.50503.0/.../Kconfig:128`. The research traced the change to v5.3.2 from the raw Kconfig at that tag |
| `ESP_NETIF_CAPTIVEPORTAL_URI` (DHCP option 114) is absent at 5.3.1 and present from 5.4 | My own grep of the pinned tree found nothing; the research read both `esp_netif_types.h` headers at the v5.3.1 and v5.4 tags and found the enum member added as `= 114` |
| What each OS probes, and the expected response | **Primary**, per OS: Apple's enterprise-network list names `captive.apple.com` on 80 and 443 for "Internet connectivity validation for networks that use captive portals"; AOSP's Mainline `NetworkStackUtils.java` carries the defaults verbatim; Microsoft's NCSI FAQ quotes the host, the payload and the 302 rule. §5 has them |
| Windows opens a browser deliberately | **Microsoft NCSI FAQ, verbatim**: *"This behavior is by design. Windows wants users to know when they connect to a network that requires captive portal authentication."* |
| NetworkManager ships **no** connectivity URI upstream | **Primary**, `NetworkManager.conf` man page: `uri` *"is unset by default"*, so "connectivity check may be disabled"; distros add it in a `conf.d` drop-in |
| GNOME does open a portal browser | **Primary**, gnome-shell's `js/portalHelper/main.js` (`org.gnome.Shell.PortalHelper`) |
| RFC 8908 requires HTTPS with a validated certificate | **The RFC itself**: the API *"MUST be accessed over HTTP using an `https` URI"*, with certificate validation against the provisioned DNS-ID, and clients *"MUST NOT proceed"* on failure |
| xterm.js sizes, real bytes | **Package file listings and a checked-in artefact**: jsDelivr's listing for `@xterm/xterm@6.0.0` gives `lib/xterm.js` = 488,663 and `css/xterm.css` = 7,112; `ausil/esp32-web-terminal` has `xterm.min.js` 488,936 beside **`xterm.min.js.gz` 120,632**, within 0.06% of the npm file, so the gzip figure is measured rather than modelled. 5.5.0's shipped UMD is 289,441 and bundles at 289,125/71,874, so **71.9 KB gzip** is a near-exact proxy |
| `esp_http_server` can serve pre-compressed gzip, and two projects do | **Source**: `ausil`'s `web_server.c` sets `Content-Encoding: gzip` by hand and chunks at 4 KB; `zvldz`'s `scripts/embed_html.py` gzips at build time into a `PROGMEM` array. IDF issue #18816 asked for an example of this and was closed **"Won't Do"** |
| Brotli is unavailable on plain HTTP | **Mozilla Hacks, verbatim**: Firefox supports Brotli *"over HTTPS, but not HTTP"*; every other browser does the same. So the ~20% brotli saving does not exist on a SoftAP portal |
| `dev-tunnels-ssh` has no curve25519, no ed25519 **and no AES-128** | **Source**, `src/ts/ssh/algorithms/sshAlgorithms.ts`: ciphers are `aes256-ctr` and `aes256-gcm@openssh.com` only. The first two were already recorded; the third is new and matters to any board narrowing its cipher list |
| The ban list keys on an IPv4 address | **Source**, `src/core/guard.h:85` (`Entry* slotFor(uint32_t ip)`) |
| xterm.js and dev-tunnels-ssh versions, licences, rough sizes | **Recorded and already re-verified on 2026-10-04** in `internal/plan-web-ssh-2026-10-04.md` §1 and §3, against the npm registry, the GitHub releases API and jsDelivr |

**What I could not verify, and must not be stated as fact:**

- **Whether ESP-NOW works between a SoftAP and a station associated to
  that same AP**, which is the one mechanism phase 1 leans on if the board
  also pairs. The IDF 5.3.1 ESP-NOW reference does say you may *"transmit
  via both Station and SoftAP interfaces"* and that channels must match,
  which they do by construction here, so it is highly likely; but nothing
  states the AP-to-own-client case and it is a bench question, not a
  reading question. **Phase 1 as specified does not need it**, because
  phase 1 uses telnet over the AP.
- **The gzip of `xterm.css`** at any version: nobody publishes it. The one
  hard adjacent figure is the 3,976 bytes a real project got by minifying
  it by hand, from 7,112.
- **The flash and RAM cost of `CONFIG_HTTPD_WS_SUPPORT`.** No IDF
  document, no issue and no project states it. Not guessed; `optimize`
  should read it off an ELF when the sat exists.
- **`esp_http_server` throughput over a SoftAP.** Espressif quotes
  *"up to 20 MBit/s TCP throughput"* for raw TCP, which is not the same
  thing. The only figure found for chunked HTTP (about 85 KB/s on a
  WROOM-32E, about 142 KB/s on an S3) is from a low-quality blog and
  should not be quoted to anybody. At 85 KB/s a 135 KB page is about 1.6 s
  on first load and nothing thereafter with a cache header, which is the
  shape rather than the number.
- **Whether Apple's or Android's detection rule is what everyone believes.**
  Apple declines to document it, on the record from an Apple engineer:
  *"does not support the default on-the-wire behaviour"*. Android's
  classification is readable in `NetworkMonitor.java` in outline but the
  method bodies could not be fetched whole. The behaviour is well attested
  and the mechanism is not formally specified by either vendor.
- **Whether Windows implements RFC 8910 at all.** No Microsoft document
  mentions option 114. Leaning firmly "no", not stated as fact.
- **Android's `CaptivePortalLogin` and Private DNS.** Widely assumed to
  bypass strict-mode Private DNS; the research looked for an AOSP statement
  and found none. **Do not assume the bypass exists.**
- **What a real phone's Wi-Fi power save does to keystroke echo.** The
  defaults say a sleeping station can buffer downlink traffic for about
  205 ms (beacon 100 TU, DTIM 2). Whether phones actually sleep with a
  WebSocket open and the screen on is not something to reason about; it is
  the single most important thing for phase 2 to measure.
- **Whether a captive portal still pops reliably on 2026 phones with
  Private DNS, DNS-over-HTTPS and HTTPS-first browsing on.** Espressif's
  own example README claims iOS, Android and Windows, but that README is
  not dated and the area has moved. Also a phase 2 measurement, on Rob's
  own phone, and the one that could most change the shape of the front
  door.
- **Per-hop relay forwarding latency.** `sat-types` calls it "a few
  milliseconds a hop, estimate, to measure" and nothing has measured it,
  because the relay does not exist.

---

## 3. The ceilings, in order of how hard they are

### 3.1 Caller lines: ten, then fourteen, then real work

`BBS_MAX_NODES` is 10 and `Session nodes_[BBS_MAX_NODES]` is a static
member array, so the lines are static DRAM at about **6,980 bytes a
session** (measured 2026-09-22; it may have grown, and re-measuring is a
`sizeof` on the host). The pool is `BBS_MAX_NODES + 2` for the sysop node
and the busy line.

Why ten is the honest number today: the socket budget.
`CONFIG_LWIP_MAX_SOCKETS` is 16 and IDF 5.3.1's Kconfig caps it at 16 on
every chip, so more RAM does not buy more telnet lines. With SSH on an S3
the arithmetic is already at the edge: two listeners, eleven sessions, two
backup clients, one announce, which CLAUDE.md records as 17 of 16 in the
worst case.

**A CALLIN line is not a socket.** It arrives as ESP-NOW frames and is
handed to a session whose bytes come from a stream (`Session::fd` is an
`int` that SSH already repurposes as an eventfd; CALLIN needs the
`Session::link` discriminator the 1.2.3 entry calls for). So for a board
fed by gateways the socket cap is simply not in the way, and the next wall
is in our own code:

```
src/core/bbs.h:1315
  static_assert(BBS_MAX_NODES + 2 <= 16, "takeTraffic keeps a bit per session in 16");
```

**So 14 caller lines is reachable with no structural change**, at about
6,980 bytes of static DRAM each. On a WROOM with about 15 KB free that is
two more lines; on an S3 with 75 to 80 KB free it is all four, with room
over. Past 14 the traffic mask widens to 32 bits and every `uint8_t` node
index wants an audit — the same mechanical-but-wide job as `max_users`.

One further step exists and should not be promised without measuring: on an
S3, `EXT_RAM_BSS_ATTR` on the session pool's own declaration moves it to
PSRAM, which CLAUDE.md already names as the upgrade path. The catch
CLAUDE.md also records is that PSRAM is slower and a session's output
buffer lives there, so every write pays; and the socket cap would then bind
telnet again. For a CALLIN-only board it is the one route past 14 that does
not touch the whole tree. **Unmeasured. Do not price it.**

### 3.2 Several boards: the answer Rob already reached for

The quote says "the BBS **systems**", plural, and that is the design.

- A sat holds up to **5 boards** (`kHosts = 5`), each with its own
  `k_link`, so one board never holds a key that opens another's traffic.
  That is built, specified and bench-tested as of 1.2.0.
- Five boards at ten lines is **fifty callers**, and at fourteen lines it
  is seventy.
- Every board sharing a sat must be on **one Wi-Fi channel**
  (`PAIR_HELLO` byte 94 carries it, and a board on another channel is
  refused with a sentence). At a fairground with no router that is free:
  the sats pick the channel and every board joins it.
- The portal page is the natural place to show it: a row a board, with its
  name, what it is, and whether it has a line free. A caller who finds
  board 1 busy taps board 2. That is a better experience than a busy
  signal and it is the thing a directory listing does on the web.

**So the honest headline for the fairground is not "a few dozen boxes let a
few dozen people on". It is "a few dozen boxes let anyone on site connect,
and the number who can be on at once is ten a board".** Plant four boards
and it is forty. That is worth saying in the marketing copy rather than
discovering at the event.

### 3.3 ESP-NOW peers: twenty is the wall, and eight is nearly free to raise

From the pinned header:

```
esp_now.h:49  #define ESP_NOW_MAX_TOTAL_PEER_NUM   20
esp_now.h:50  #define ESP_NOW_MAX_ENCRYPT_PEER_NUM 6
esp_now.h:52  #define ESP_NOW_MAX_DATA_LEN         250
```

and from the pinned Kconfig, which is the part that matters most here:

```
esp_wifi/Kconfig:467  config ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM
                 469      range 0 4  if IDF_TARGET_ESP32C2
                 470      range 0 17 if (!IDF_TARGET_ESP32C2)
                 472      default 7  if (!IDF_TARGET_ESP32C2)
                 475      "The number of hardware keys for encryption is fixed. And the espnow
                 476       and SoftAP share the same hardware keys. So this configuration will
                 477       affect the maximum connection number of SoftAP. Maximum espnow
                 478       encrypted peers number + maximum number of connections of SoftAP
                          = Max hardware keys number."
```

**That paragraph is the single most useful thing read for this report.**
Rob's decision of 2026-09-26 to do our own AES-128-CCM and register
ESP-NOW peers unencrypted was made for a different reason entirely — the
5.3.1 receive callback cannot say whether a frame was decrypted, and a MAC
is trivially forged — and it turns out to be *exactly* what a gateway sat
needs, because every encrypted ESP-NOW peer would have taken a hardware key
away from the SoftAP and so taken a phone off the AP. A sat that used
ESP-NOW's own encryption would hold fewer phones for every board it served.
Ours holds its full allowance. **The sidestep is not a technicality; it is
the reason this feature is possible on one radio.**

**The total is 17 keys, which makes the trade concrete.** IDF 5.3.1's
Wi-Fi driver guide says the SoftAP's `max_connection` defaults to 10, that
an ESP32 supports up to 15, and that *"ESP AP and ESP-NOW share the same
encryption hardware keys, so the `max_connection` parameter will be
affected by the `CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM`"*, with a total
capacity of 17. So with ESP-NOW's own encryption at its default of 7
encrypted peers, a sat's AP would be capped at **10 phones**; with
`kPeers` raised to 20 it could not have 20 encrypted peers and any phones
at all. **Doing our own crypto turns a hard trade into no trade**, and
`max_connection` can simply be set to 15.

One inconsistency to know about: the header constant
`ESP_NOW_MAX_ENCRYPT_PEER_NUM` says 6 while the Kconfig default is 7 and
its range reaches 17. The Kconfig is what gets built.

Our own cap is `Engine::kPeers = 8`. The cost of raising it:

| | Figure | How |
|---|---|---|
| `Engine::Peer` | about **240 bytes** | Hand-summed from the struct in `link.cpp` with Xtensa alignment (two keys and two nonces at 16 bytes each, two `uint64_t` replay masks on 8-byte alignment, a 36-byte `PeerStats`, a 17-byte name). **Hand-summed, not compiled.** A `sizeof` on the host would settle it in a minute |
| Where it lives | **the heap**, from PSRAM where there is any | `peers_ = new (std::nothrow) Peer[npeers_];` (`link.cpp:432`) |
| 8 → 20 | about **+2.9 KB of heap, +0 static DRAM** | 12 more peers at 240 |
| What else grows | the pairings file (one line a peer on `userdata`), and `LINK`'s list, which already pages | |
| What does not grow | `kSessions` is 16 **board-wide**, not per peer | `link.h:297` |

So raising `kPeers` to 20 is close to free on any board, and 20 is then the
ESP-NOW ceiling with no way past it on 5.3.1. **Twenty peers is still not
"a few dozen" if every AP pairs directly.** Which brings us to the part
that actually answers the question.

### 3.4 The fan-out, which is the valuable paragraph

Does a chain of sats behind one relay consume one of the board's eight
pairings, or several?

**As `sat-types` specifies the relay today: several.** Its §1 is explicit
that the far sat pairs with the board *through* the relay, end to end, with
its own `k_link`, and that the relay "can drop or delay frames and can do
nothing else". The relay also pairs, for its own status. So a relay plus
seven far sats fills a board at `kPeers = 8`. The relay buys **reach**, not
fan-out. That is the right design for a camera in a barn and it does not
help a fairground.

**But the link already has fan-out, and it is not the relay.** From
LINK.md's serial transport section: *"Many callers still share that one
box, because sessions are multiplexed."* A door sat holds several callers
in several sessions on one pairing. A camera sat queues two requests a
board on one pairing. **Sessions multiplex over a pairing; a pairing is a
device, not a caller.** So:

- **One paired gateway box can carry up to ten callers on one pairing**,
  because ten is all the board has. Eight pairings is not the wall for
  callers at all. Ten lines is.
- Therefore "a few dozen around a fairground" has two shapes, and they cost
  very differently:
  - **Every AP pairs with the board.** Needs `kPeers` raised (cheap, §3.3)
    and stops hard at 20 APs a board. Each AP is independent; the board
    sees twenty devices; no relay needed if they are all in range.
  - **A concentrator.** A few APs feed one paired box, which holds one
    pairing with the board and multiplexes every caller over it. The board
    sees one device whatever the field looks like. **This is what scales to
    a few dozen and beyond, and it is the real answer to Rob's scale
    question.**
- **The concentrator is not free, and the cost is trust, not RAM.** A relay
  forwards sealed frames it cannot open; a concentrator **terminates** the
  callers' sessions and therefore sees every keystroke in the clear. It
  would hold its own pairing with the board and originate every caller's
  CALLIN session itself. That is a real change of security posture and it
  needs Rob's eyes.
  - The precedent that covers it is already in LINK.md: the serial
    transport is **"trusted by the wire: no pairing and no sealing"**,
    because whoever can plug into the board's UART already has the board.
    At a fairground, the sysop plants every box, so a concentrator is the
    sysop's own hardware carrying the sysop's own callers. That is the same
    trust and it is honest.
  - What it must **never** be is a third party's box. A concentrator
    somebody else plants, carrying your callers, reads their passwords.
    So: a concentrator pairs like anything else, shows in `LINK` and `SATS`
    as what it is, and **a caller through one is never staff** (§7.3).
- **A middle ground that keeps the sealing and gets some fan-out**: let the
  board's wrapper carry a caller index as well as a sat index, so one
  *pairing* carries several *far sats'* sessions while each far sat still
  holds its own key to the board. The relay forwards; the sealing survives;
  one pairing covers a whole chain. That is more protocol work than
  `sat-types` priced (its wrapper is two bytes and names one far peer) and
  it is the thing to design if Rob dislikes the concentrator's trust story.
  **Not costed here; it is a design question for whoever builds phase 6.**

**Recommendation on fan-out:** do not solve it in phases 1 to 4. Ten lines
a board and five boards a sat is fifty callers, which is more than a
fairground BBS will see at once, and every AP pairing directly up to 20 is
a two-line change. Revisit the concentrator only when somebody really
plants more than twenty boxes for one board, and present the trust trade to
Rob when they do.

---

## 4. Channels, and the fairground with no router

### 4.1 Which interface, and what sets the channel

`esp_now_peer_info_t` carries an `ifidx` per peer, *"Wi-Fi interface that
peer uses to send/receive ESPNOW data"*, and a `channel` whose
documentation is the whole rule:

```
esp_now.h:68-70
  uint8_t channel;  /* Wi-Fi channel that peer uses to send/receive ESPNOW data.
                       If the value is 0, use the current channel which station
                       or softap is on. Otherwise, it must be set as the channel
                       that station or softap is on. */
```

One radio, so one channel for both interfaces. That gives three
arrangements:

| The sat is | Channel set by | ESP-NOW peers on | Notes |
|---|---|---|---|
| **AP only** | the sat, freely | `WIFI_IF_AP` | The fairground case. The sat is the channel authority and every board must follow it |
| **STA only** | the router it joined | `WIFI_IF_STA` | camsat today. No AP, so no phones |
| **AP + STA** | **the station**, i.e. the router | either | The AP is dragged onto the router's channel, **and the phones go with it**. A sat at home, serving phones on the house Wi-Fi's channel |

The AP+STA rule is spelt out in IDF 5.3.1's Wi-Fi driver guide and is worth
quoting, because it says what happens to the portal's clients: *"In
station/AP-coexistence mode, the home channel of AP and station must be the
same, and if they are different, **the station's home channel is always in
priority**... the AP needs to switch its channel from 6 to 9... While
switching channel, the ESP32 in AP mode will notify the connected stations
about the channel migration using a **Channel Switch Announcement
(CSA)**."* So an AP+STA sat whose router hops drags every phone on its
portal to the new channel, announced properly rather than silently. That is
good behaviour and it is already handled for us.

The board is always a station on a router, and ESP-NOW *"must be the same
as that of the connected AP"* (Espressif's own FAQ, cited in LINK.md). So:

- **At home**, the router sets the channel, the sat follows the board as
  peers already do, and an AP+STA sat's own AP lands on the router's
  channel too. Nothing new. A board joined to a router on channel 6 and a
  sat whose AP is forced to channel 6: consistent by construction.
- **At a fairground with no router**, nothing forces a channel and the sats
  may pick one. **Rob's guess in the brief is right and this is the easy
  case** — with one important condition on the board, next.
- **The mixed case is the bad one**: a board joined to a venue's router on
  channel 11 while the sats' AP is on channel 1. The board cannot leave its
  router's channel and the sats cannot serve two channels on one radio, so
  the sats must be told to use channel 11. The link already has the words
  for exactly this failure (`satwords.h`'s shared-satellite message about
  one channel for every board), and the gateway needs the same sentence
  from the sat's side: *"this board is on channel 11; set this sat's AP to
  channel 11"*. A sat whose AP channel is settable in CONFIG, and a board
  that says which channel it is on, is the whole fix.
- One pleasant find for later: `wifi_ap_config_t` has a **`csa_count`**,
  *"Channel Switch Announcement Count. Notify the station that the channel
  will switch after the csa_count beacon intervals"*, default 3. So a sat
  that must follow a board onto a new channel can tell its phones to come
  with it rather than silently dropping them. Worth knowing; not needed in
  phase 1.

### 4.2 The real blocker, and it is on the board

Two pieces of board code stand between this and a field:

```
src/core/link.cpp:1104   if (!io_.associated()) return;
```

in `onClear`, so a board whose station is not joined answers **no**
`DISCOVER`, **no** `HELLO` and **no** `PAIR_HELLO`. The comment says why
and the reason is sound: an unassociated board's channel is wherever its
scan has it, and a peer that believed a `BEACON` from there would camp on
the wrong channel. That was found on the bench on 2026-09-26.

```
src/main.cpp:1135-1150   while (!s_ssid[0]) { ... wait for Improv ... }
                         ...
                         while (!bbs.begin(s_port)) vTaskDelay(1000);
```

so a board with no network **never reaches `Bbs::begin`**, no plugin
starts, and on a display board the glass is simply off. That is the trap
recorded on 2026-10-01 that bit Agentville, the 4.3B and the WS2, and it is
a blocker for "a board in a field", not a nuisance.

**Three ways out, and the cheapest is not firmware at all:**

**(a) The board joins the gateway sat's AP as an ordinary station.
Recommended.** The sat runs an open AP with DHCP, as it must for the
phones. The board joins it with `wifi_ssid` in its own `system.cfg`, the
way any board joins any router. Then:

- the board is associated, so `link.cpp:1104` is satisfied and nothing
  changes;
- `Bbs::begin` runs, every plugin starts, the panel lights;
- the channel is the sat's AP channel and the board is on it by
  construction — the mixed case of §4.1 cannot arise;
- the board has a DHCP address, so **telnet works over the AP too**: a
  laptop or a SyncTERM on the same AP dials the board directly, and
  **phase 1 can be a WebSocket-to-telnet bridge with no link, no CALLIN and
  no pairing at all**;
- NTP will not resolve (no internet), so the clock is whatever the board
  has. See §4.3.

The one thing to think about is a chicken and egg: the board needs the sat
up before it can join. A board that boots first sits in the wait loop until
the sat appears, which is exactly what that loop is for, and the console
says so every 30 s. A sat that reboots takes its boards' Wi-Fi with it for
a few seconds; the recovery work of 1.2.1 (`wifi.last`, the redial backoff)
already covers reassociation.

**(b) Let the board run with no network at all.** Honest, and real work:
`main.cpp`'s wait loop becomes conditional, `netServicesStart` has to cope
with no address, mDNS and announce must stand down cleanly, the link needs
a channel policy for an unassociated host (set it explicitly with
`esp_wifi_set_channel` and make `BEACON` authoritative from a board that
chose rather than scanned), and `linkRadioAssociated()` grows a second
meaning. **Two to four days plus bench, and only worth it if Rob wants a
board with no AP in the field.** Option (a) makes it unnecessary.

**(c) Give the board its own SoftAP.** Rejected, and firmly: Rob's binding
constraint of 2026-10-05 is that the board must do its part with no
website, which "rules out any board-side design needing HTTP, TLS,
WebSocket or a browser"; the board's socket budget is already at 17 of 16
in the worst case with SSH; and an AP plus DNS plus HTTP on the loop is
exactly the sort of thing Rule no. 1 exists to refuse. **The portal lives
on a sat. Always.**

### 4.3 The clock, announce, and what a board in a field loses

- **The clock.** No internet means no NTP. The board keeps running on
  whatever it has, which after a cold boot is nothing. That matters more
  than it sounds: the caller log, forum post dates, photo filenames, the
  rotating information pages' "Until" date and the silent-hours window all
  want a clock, and CLAUDE.md already records the rule that with no NTP the
  rotator "keeps rotating rather than guess". For a fairground, the sysop
  should set the clock. **Worth checking whether anything exists to set it
  by hand**; if not, a sysop command to set the time is a small, obviously
  useful thing and belongs in this feature's phase 3 rather than being
  discovered on the day. The link's own `PONG` already carries
  *"u32 unix time (0: no NTP)"* from host to peer, so the plumbing to tell
  a sat the time exists; the reverse (a sat with a GPS or an RTC telling
  the board) does not and would be a new message.
- **Announce.** Pointless with no internet and it already fails gracefully
  (DNS on the runner, a kept last-good address, a refused post retried).
  It should simply be off in a field. Nothing to build.
- **mDNS** still works on the local AP, so `<hostname>.local` resolves for
  a laptop on the same AP. A nice bonus for phase 1 and it needs nothing.
- **The directory, the broker and `.onion` are all irrelevant here**, which
  is the point: this is the offline product.

---

## 5. The captive portal, mechanically

**Espressif ships the whole thing as an example in the pinned framework**,
at `examples/protocols/http_server/captive_portal/`, with a `dns_server`
component, and its README is a primary source for the behaviour:

> This example demonstrates a simple captive portal that will redirect all
> DNS IP questions to point to the softAP and redirect all HTTP requests to
> the captive portal root page. **Triggers captive portal (sign in) pop up
> on Android, iOS and Windows. Note that the example will not redirect
> HTTPS requests.**

The mechanism, from its own source:

- a SoftAP with DHCP (esp_netif's `dhcps`, which hands out the AP's own
  address as the DNS server);
- a tiny UDP server on port 53 that answers **every** A query with the
  AP's address (`192.168.4.1` by default);
- `esp_http_server` with a 404 error handler that answers
  `302 Temporary Redirect` with `Location: /`, and the comment that matters:

  ```
  // iOS requires content in the response to detect a captive portal,
  // simply redirecting is not sufficient.
  ```

  so the redirect carries a body as well as a header.

### What each OS probes

Better sourced than expected. Each row is from the vendor or from AOSP.

| OS | Probe | Expects | What makes the portal appear |
|---|---|---|---|
| **Apple** | `captive.apple.com/hotspot-detect.html`, on 80 and 443, listed by Apple as *"Internet connectivity validation for networks that use captive portals"* | a tiny page whose title **and** body are the word `Success` | Anything else. **Apple does not document the rule** and has said so on the record: an Apple engineer wrote that Apple *"does not support the default on-the-wire behaviour"*. The Captive Network Assistant sheet is still current: Apple's own platform-support guide has a "Network connects but welcome page doesn't appear" troubleshooting entry, which is proof the sheet is the expected behaviour |
| **Android** | `http://connectivitycheck.gstatic.com/generate_204` **and, in parallel,** `https://www.google.com/generate_204` — both verbatim from Mainline's `NetworkStackUtils.java` | `204 No Content` | `NetworkMonitor` classifies **204 → success; 200 → judged by content length, so a body means an intercept; 3xx → portal, and the `Location` header becomes the `redirectUrl` handed to the login UI**. The AOSP resource overlays are empty by design, so OEMs may substitute their own URLs |
| **Windows** | `www.msftconnecttest.com/connecttest.txt` (and `www.msftncsi.com/ncsi.txt` before build 14393) | HTTP 200 with the payload **`Microsoft Connect Test`** | NCSI FAQ, verbatim: *"The HTTP probe didn't get past a hotspot or captive portal. This is typically determined when an HTTP response 200 is received, but the response payload doesn't contain the text file connecttest.txt. Alternatively, a non-200 HTTP status code, such as **302**..."*. And on the browser: *"This behavior is by design. Windows wants users to know when they connect to a network that requires captive portal authentication."* Also: **from Windows 11, HTTP is always used**, and the DNS lookup only locates the probe |
| **Linux** | **NetworkManager ships no URI at all** — its man page says `uri` *"is unset by default"*, so connectivity checking "may be disabled"; distros add `network-test.debian.org`, `nmcheck.gnome.org` or `connectivity-check.ubuntu.com` in a drop-in | the header `X-NetworkManager-Status: online`, or the body `NetworkManager is online` | **GNOME does open a browser**: gnome-shell ships `org.gnome.Shell.PortalHelper`, launched when NM reports a portal. KDE not researched |
| **Firefox** | `detectportal.firefox.com/canonical.html`, its own probe | — | Shows an in-page notification. Usefully, Mozilla's TRR notes say the detection host *"is also added to the exclusion list internally, in order to be able to detect local captive portals"*, so **Firefox's DNS-over-HTTPS does not blind the portal** |

**Two consequences worth acting on:**

- **The Apple sheet is not full Safari**, and a page that wants a WebSocket
  terminal is exactly the sort of thing that may not run in it. **So design
  the portal as a landing page with a link, not as the terminal.** The
  terminal opens in the caller's real browser. That also fixes the Android
  Custom Tabs case and costs nothing.
- **Android's HTTPS probe is the one that decides "validated".** With no
  internet at all, both probes fail and Android knows it is captive, which
  is the fairground case and it works. But a sat ever given an uplink must
  **not** let `https://www.google.com/generate_204` through before login,
  or Android marks the network as having internet and **never offers "Sign
  in to network"** whatever the HTTP redirect says. Worth writing down
  before somebody helpfully adds an uplink.

### What breaks, and must be said on the page rather than discovered

- **HTTPS cannot be intercepted.** A caller who types an address, or whose
  browser is in HTTPS-first mode, gets a certificate error or a timeout,
  not the portal. The portal appears reliably **only** through the OS's own
  probe. So the page must say what to do if it did not pop: "open your
  browser and go to `http://10.0.0.1`", a bare IP over plain HTTP. For a
  box planted in a field that is a physical design decision: **put the SSID
  and the address on a sticker.**
- **Android Private DNS in strict mode defeats the hijack and there is no
  server-side fix.** With a hostname typed in, Android does encrypted DNS
  only; the DoT server is unreachable before login, so nothing resolves and
  the splash page cannot be fetched. Google's own announcement says Android
  marks such a network "No internet access". "Automatic" (opportunistic)
  mode falls back to the AP's plaintext answer and works. **The only
  mitigation is documentation**: tell the caller to set Private DNS to Off
  or Automatic, log in, then set it back. And see §2: the widely assumed
  "`CaptivePortalLogin` bypasses Private DNS" could not be found in any
  AOSP document, so do not rely on it.
- **ESP32 portals are known not to auto-pop on some recent Android and
  Samsung devices** — four or more years of reports against
  `arduino-esp32`. The folk workarounds are to move the SoftAP off
  `192.168.4.1` (`4.3.2.1` and `172.x.x.x` are the ones people report
  working) and to answer 200 with a body rather than 302. **Secondary and
  unexplained, and exactly the kind of thing to try on Rob's own phone in
  phase 2 rather than to design around now.** It is the single biggest risk
  to the front door and it is cheap to test.
- **RFC 8910 is the clean way and is not available to us.** The DHCP
  `Captive-Portal` option (114) is the modern answer and both Apple (from
  iOS 14) and Android (documented as RFC 8908 support) honour it. Two
  reasons it is out:
  - **`ESP_NETIF_CAPTIVEPORTAL_URI` does not exist in IDF 5.3.1.** My grep
    of the pinned tree found nothing, and the research confirmed the enum
    member appears first at **v5.4** as `= 114, /**< Captive Portal
    Identification */`. So on the pin it would be a hand-written DHCP
    option;
  - **and even then it needs HTTPS with a real certificate.** RFC 8908
    requires the API *"MUST be accessed over HTTP using an `https` URI"*,
    with certificate validation against the provisioned DNS-ID, and clients
    *"MUST NOT proceed"* if validation fails. **An isolated SoftAP cannot
    satisfy that honestly**, which is the same wall as §7.2 and it is the
    stronger objection of the two.
  - The RFC itself concedes the point: *"for the foreseeable future,
    captive portals will still need to implement interception techniques to
    serve legacy clients."* **So: the hijack is the mechanism, not a
    fallback.** Revisit option 114 only if the project ever moves to 5.4+
    *and* finds a way to a trusted certificate, which is to say probably
    never for a box in a field.

**What it costs on the sat.** Everything needed is in-tree:
`esp_wifi` SoftAP, `esp_netif`'s DHCP server, the example's ~200-line DNS
responder, `esp_http_server` with `CONFIG_HTTPD_WS_SUPPORT=y`. No
third-party library. The socket arithmetic is the thing to size, and the
example's own `sdkconfig.defaults` sets `CONFIG_LWIP_MAX_SOCKETS=16`, the
same cap the board has:

| Taker | Sockets |
|---|---|
| DNS responder (UDP) | 1 |
| `esp_http_server` internal | 3 (`esp_http_server.h:187`) |
| Each open HTTP or WebSocket client | 1 |
| **Left for clients at 16 total** | **about 12** |

and `max_open_sockets` defaults to 7, which with the three internal sockets
exactly fills `LWIP_MAX_SOCKETS`' own default of 10 — so **budget
`max_open_sockets + 3`**, and note that the IDF docs page glosses it the
other way round ("set 10, only 7 available"). The header's wording and the
matching defaults favour `+3`; assume three of overhead and measure. Two
further knobs matter: `lru_purge_enable` (default **false**) closes the
least-recently-used connection rather than refusing a new one, which a
public AP wants on; and `esp_http_server` **does not fragment WebSocket
messages for you** (its own header says so), so a long screen redraw is
your `final`/`fragmented` bookkeeping. For scale, the real project's
settings are `max_open_sockets = 4`, `max_uri_handlers = 20`,
`stack_size = 10240` (up from the default 4096, and it is heap), and
`lru_purge_enable = true`, with its README capping it at *"up to 4
concurrent sessions with a 1-hour idle timeout"*.

Against that:

- the SoftAP will hold **15** associated stations on an ESP32 or S3
  (`ESP_WIFI_MAX_CONN_NUM (15)`);
- the sat's sockets allow about **12** concurrent terminals;
- the board allows **10** callers.

Those three numbers sit in the same region, which is tidy: nothing is
wildly over-provisioned and the board is the binding constraint, which is
where a sysop would expect it to be.

---

## 6. The terminal on the phone

This is the part that decides whether the feature is any good, because a
phone has no telnet client and a portal that hands out an address and a
port is useless to the people it is for.

### 6.1 A base ESP32 can serve it, and here is the arithmetic

The brief's framing measured a browser terminal against the **BBS
firmware's** partition layout (1.5 MB app slots on 4 MB), and Rob's
correction is right: a sat has its own table and its own small firmware.
The measured comparison:

| | Bytes |
|---|---|
| camsat's `factory` app partition, 4 MB ESP32, no OTA | **4,128,768** (`0x3F0000`) |
| camsat's actual `firmware.bin` (ESP32-CAM, with the camera driver, PSRAM, the whole link, mbedTLS) | **789,072** |
| **Free** | **3,339,696** |

A gateway sat would drop the camera driver and add `esp_http_server`, a DNS
responder and the web assets. The payload, with real bytes:

| File | As shipped | Gzipped |
|---|---|---|
| `@xterm/xterm` **6.0.0** `lib/xterm.js` (already minified; there is no separate `.min.js`) | **488,663** | **120,632**, measured |
| `@xterm/xterm` 6.0.0 `lib/xterm.mjs` (ESM, new in v6) | 344,970 | about 88,100 |
| `@xterm/xterm` **5.5.0** `lib/xterm.js` | **289,441** | about **71,900** |
| `css/xterm.css` (6.0.0) | 7,112 | unpublished; hand-minified to 3,976 by one project |
| `@xterm/addon-fit` 0.11.0 | 1,521 | **592** |
| `@xterm/addon-attach` 0.12.0 | 1,701 | 679 — and its **entire source is 2,900 bytes of TypeScript**, about ten lines wiring `onData` to `ws.send`. Write it yourself |
| **A whole page, gzipped** | | **about 86 KB on 5.5.0, about 135 KB on 6.0.0** |

Everything is MIT and xterm has **zero runtime dependencies**. The 120,632
figure is the strongest in this report because it is not modelled: it is
`xterm.min.js.gz` checked into `ausil/esp32-web-terminal` beside its
488,936-byte original, within 0.06% of the npm file.

**So 86 to 135 KB against 3,339,696 bytes free: between 2.6% and 4%.**

Four things follow:

- **Consider xterm 5.5.0 rather than 6.0.0.** It is about **48 KB of gzip
  cheaper**, a 40% saving on the one asset that dominates, and v6's
  additions (ligatures, WebGL work) are not obviously needed to draw a BBS.
  Worth a look in phase 1, where both are a one-line change. Against it:
  6.0.0 is where the project is, and one real project reports that **6.0.1
  was never tagged despite 250-plus commits after 6.0.0**, including a
  "critical WriteBuffer dispose-timer fix", and pins a `6.1.0-beta` to get
  it. So the version question is "5.5.0 or a 6.1 beta", and the stable
  6.0.0 is arguably the worst of the three. **A real decision for phase 1,
  not a detail.**
- **The sat does not need `dev-tunnels-ssh`**, which bundles at **323,424
  minified / 74,503 gzipped** including its Node polyfills (`buffer`,
  `diffie-hellman`, `bn.js`, and ~30 KB of `vscode-jsonrpc` that an SSH
  client has no use for). That library exists to speak SSH from a browser
  across the internet through a relay; here the browser talks to the box it
  is standing next to, and the long hop is already sealed by the link. **A
  plain WebSocket costs 679 bytes by comparison.** Dropping the SSH client
  is the single biggest difference between this feature and browser SSH,
  and it is what makes this comfortable on a base ESP32.
- **Serve the files gzip-precompressed**, with `Content-Encoding: gzip` set
  by hand. Both real projects do exactly that, one embedding a `.gz` with
  `EMBED_FILES` and streaming it in 4 KB chunks, the other gzipping at
  build time (`gzip.compress(..., compresslevel=9)`) into a `PROGMEM`
  array with version hashes on the asset URLs. **There is no built-in
  support and there will not be**: IDF issue #18816 asked for a
  pre-compressed-gzip example, reporting 348 KB → 108 KB and about twice
  the page-load speed, and was closed **"Won't Do"**. **Brotli is not an
  option**: browsers advertise `br` only over HTTPS, so on a plain-HTTP
  SoftAP the extra ~20% does not exist.
- **Lazy-load the terminal.** The UART-bridge project loads xterm.js on
  demand, so an ordinary page view never pays for it. Here that means the
  **portal landing page is a few KB** and only a caller who taps "connect"
  fetches the terminal — which matters because the portal page is what
  every phone that joins will fetch, including the ones that wander off.

**Where to put the assets.** Both real projects chose the **app image**
over a filesystem, and on a 4 MB sat with no OTA that is clearly right:
one binary, one flash command, nothing to go out of step, and no partition
to corrupt. A data partition is the later option if anyone wants to theme
the page without rebuilding. A plausible table:

```
nvs,      data, nvs,     0x9000,   0x6000
phy_init, data, phy,     0xf000,   0x1000
factory,  app,  factory, 0x10000,  0x2F0000    # 3,080,192: firmware + embedded web
web,      data, spiffs,  0x300000, 0x100000    # 1,048,576: optional, if assets move out
```

For calibration, the two real projects' tables on 4 MB are 1.6 MB of app
plus a 393,216-byte SPIFFS, and 1,966,080 of app plus a **131,072**-byte
SPIFFS — the second being precisely why its 120 KB gz rides in the app.

**Verdict: the AP gateway sat is a base-ESP32 feature, and the strongest
evidence is not the arithmetic.** `ausil/esp32-web-terminal` ships exactly
this shape — SoftAP, captive-style web UI, `esp_http_server` with
WebSocket, xterm.js gzipped in the app image, four concurrent sessions,
PBKDF2 login and a per-address lockout — on an **ESP32-C3 Super Mini with
4 MB of flash**, a part smaller and cheaper than a WROOM. The project's own
rule applies and is satisfied: build for the smallest part and run on the
bigger ones. An S3 sat would buy PSRAM for buffers and 8 or 16 MB of flash
for a fatter page, neither of which phase 1 needs.

One caveat to measure rather than assert: **internal RAM on the sat, not
flash, is what will actually bind.** A SoftAP with 15 stations, lwIP with
16 sockets, `esp_http_server` with its own task (default 4096 bytes of
stack, raised to 10240 by the real project, from the heap) and a handful of
open connections, plus the link engine's roughly 15 to 19 KB of tables, is
a lot of heap for a WROOM's ~180 KB of usable DRAM. A base ESP32 **without**
PSRAM is what phase 2 must weigh; an ESP32-WROVER (PSRAM, same price class,
no S3 needed) is the obvious hedge if it is tight. **Do not promise a bare
WROOM gateway until phase 2 reads the heap** — the existence proof above is
on a C3, which has a different memory map and no second core, so it proves
the software path rather than the WROOM's budget.

And one Rule-no.-1 note for the sat, by analogy rather than by rule: the
httpd task is not the BBS loop, but a chunked 135 KB send reads flash on
every chunk, and the server is **single-threaded**, which is why the real
project chunks at 4 KB with the comment *"to avoid overwhelming the
single-threaded server"*. On a sat that is only its own problem. On a board
it would contend for the LittleFS partition mutex with screen playback,
which is one more reason the portal lives on a sat.

### 6.2 The alternatives, and why a full terminal is the right call

| Option | What it is | Verdict |
|---|---|---|
| **xterm.js over a WebSocket** | A real VT emulator: cursor positioning, full-screen ANSI, scrollback, a selectable renderer | **Recommended.** It is the only option that can draw the board as designed. The web-SSH plan has already thought about CP437 (xterm.js treats `write()` bytes as UTF-8 and the board re-encodes CP437 for a UTF-8 terminal) and about the detector (xterm.js answers the cursor position report, so it should detect as UTF-8 ANSI) — **both still unverified there and both verified for free by phase 1 here**, which is a bonus worth naming: this feature de-risks browser SSH |
| **A page that prints an address and a port** | "Telnet to 10.0.0.1 port 6400" | **Not enough on its own, and necessary anyway.** It is the fallback when the portal does not pop and the route for a caller with a real client on a laptop. Build it as part of the portal page, never as the whole answer |
| **A hand-written small terminal** | A few hundred lines of JS doing a cursor grid and the ANSI subset the board uses | **Rejected.** It would save flash the sat has in abundance, and cost weeks plus a permanent maintenance burden plus a guaranteed supply of "it renders wrong on this screen" bugs. The project's own history says this exactly: write a client beside the server and the two agree on something wrong |
| **`terminal.js`** (Gottox), **12 KB gzip**, MIT, zero dependencies | A real VT100 state machine and renderer, *"written from scratch"*, aiming at xterm compliance | **The only genuinely smaller full option, and still rejected.** 12 KB against 72 is a 60 KB saving on a sat with 3.3 MB spare, bought with the keyboard layer, focus and selection handling, resize glue, and unproven escape coverage against the exact sequences this board emits. Its last three commits are all dependabot bumps and it is still at 1.0.11. **Revisit only if flash ever becomes the binding constraint, which §6.1 says it is not** |
| **`hterm`** (Chromium libapps, via `hterm-umdjs`) | A proper terminal; what Chrome's Secure Shell used | **Rejected on size, which was the surprise.** 1,083,375 bytes unminified and about **142,613 gzipped — 18% larger than xterm 6.0.0 and 98% larger than 5.5.0.** It is also stale at 1.4.1 against an upstream in the 1.9x range, and the npm wrapper declares MIT while upstream libapps is Chromium BSD-3-Clause, which is a licence question nobody needs |
| **`jQuery Terminal`** | A line-oriented command interpreter that parses ANSI colour | **Rejected.** Its own author calls it a *"fake terminal emulator"*: no cursor addressing, no alternate screen. Plus eight dependencies including ~87 KB of jQuery, for about 89 KB gzipped in total — bigger than xterm 5.5.0 and far less capable |
| **`ghostty-web`** | A WASM VT parser with an xterm.js-compatible API, and genuinely better Unicode | **Rejected for now.** About 400 KB of WASM by its own README, so bigger, and its demo assumes a server. Worth remembering if a board ever wants Devanagari |
| **Plain HTTP forms** | A page per screen, submit to act | **Rejected, and worth saying why.** It is not a BBS. The whole character of the thing is a live line: the chat room, the input effects, the spinner, the paced screens, the `[More]` pager, DASH refreshing. A form-based BBS is a website with a retro skin, which is the one thing this project exists not to be. Rob's own framing ("real hardware reachable without a web browser") is about the board; a browser as a *terminal* keeps the line live, a browser as a *UI* destroys it |
| **`ansi_up`**, **2,968 bytes gzipped**, MIT | SGR colour and style in a string to HTML, plus linkification. **No cursor positioning, no CUP, no erase, no alternate screen, no keyboard** | **Rejected for the terminal.** It cannot draw a form, a reverse-video bar, a refresh screen or the wordmark. **Worth remembering for something else, though:** at 3 KB it is essentially free, and if the portal ever wants to show the board's own log or a coloured status snippet, this is the tool |

The size ladder, for the record: terminal.js 12 KB → xterm 5.5.0 ~72 KB →
xterm 6.0.0 ESM ~88 KB → xterm 6.0.0 UMD 120.6 KB → hterm ~142.6 KB, all
gzipped. **There is no smaller real terminal worth having**, so the only
live question is which xterm (§6.1).

`sshterm` is mentioned in the web-SSH plan at **5,132,663 bytes** with a
WASM blob, and `sshclient-wasm` (BSD-3-Clause, Go to WASM) publishes no
size at all but Go WASM output is habitually megabytes. Neither fits a
4 MB sat, and both are the wrong shape anyway: the sat needs no SSH.

### 6.3 What the page actually is

Three screens, and this is `tty-ux`'s and `explain`'s to specify properly
rather than this report's:

1. **The portal landing.** The sat's name, one line about what this is, and
   a row per board on the link: name, a word of description, and whether it
   has a line free. A "connect" button a row. Plus the plain-address
   fallback and the honest line about the open AP (§7.4).
2. **The terminal.** xterm.js at 80x24 on a phone in landscape, which
   should be the default hint; a portrait phone wants 40 columns and the
   board already has a complete 40-column design, so **a phone held upright
   gets the C64 layout**, which is a delightful accident and worth
   leaning into in the copy.
3. **The end.** Why the line closed, in the board's own words.

---

## 7. Encryption: three hops by two board classes

Rob: *"over espnow compute the cost for encryption OTA, if that moves us up
to a ESP32-S3 let me know. Again this could come down to a SSH vs UNSECURE
issue across base and S3 boards."* He is right that it is the same issue as
SSH. There are three distinct hops and they have three different answers,
so they are priced separately.

### 7.1 Hop 1, sat to board over ESP-NOW: already done, already cheap, does not force an S3

**Nothing to build and nothing to decide.** The link seals every session
frame with AES-128-CCM, 8-byte tag, the 20-byte header authenticated as
associated data so the family, session, sequence, fragment and length
cannot be moved between frames; the key is a session key derived with
HKDF-SHA256 from a per-pairing `k_link` at every `HELLO`, with a
packet-number replay window. `src/core/linkcrypto.h` confirms the shape
(16-byte key, 8-byte tag, 13-byte nonce, P-256 ECDH, HKDF) and it is in
every image including the WROOM's, because the ECDH, CCM, HMAC and SHA-256
code was already linked for WPA3.

The measured cost, from the camsat bench on 2026-09-26, recorded in
LINK.md:

| Per 222-byte frame with its header | seal | open | `mbedtls_ccm` in the same image |
|---|---|---|---|
| ESP32-S3 | **78 µs** | **78 µs** | 963 µs |
| ESP32 (classic) | **93.6 µs** | **94.7 µs** | 654 / 656 µs |

The whole of a control frame on the loop, open and dispatch together, is
**150 to 184 µs**. The per-block AES peripheral lock was the cost, not the
hardware, and `linkcrypto` builds the same CCM from one CBC call for the
MAC and one CTR call for the keystream.

**Per keystroke, both ways, on both classes of part: under 400 µs of
crypto.** That is a fifth of a millisecond each way on a classic ESP32,
against a board echo already measured at 4 ms p50. It is not visible, it is
not a Rule no. 1 risk, and **it does not move anybody to an S3.** A
repeater adds none of it, because a relay forwards sealed frames it cannot
open — it never holds a key for the traffic passing through. ESP-NOW's own
peer encryption is not used and should not be: besides the reasons Rob
already settled, §3.3 shows it would cost the sat a phone per peer.

**So the answer to Rob's question on this hop is: encryption over ESP-NOW
is already paid for, costs about 94 µs a frame on the cheapest part we
support, and is one of the few places in this project where the secure
option is also the cheap one.**

### 7.2 Hop 2, phone to sat: this is the one that forces the question, and the answer is "do not"

On an open access point this hop is **in the clear**. Anybody in radio
range with a laptop in monitor mode reads every keystroke and every screen,
including a password typed at the board's prompt. At a fairground that is
not a theoretical adversary; it is one bored person.

Securing it means TLS on the sat. Priced from the pinned framework:

| | Figure | Source |
|---|---|---|
| In buffer per TLS session | **16,384 bytes** | `mbedtls/Kconfig:82-85`, `MBEDTLS_SSL_IN_CONTENT_LEN` default 16384 |
| Out buffer per TLS session | **4,096 bytes** | `:91-94`, `MBEDTLS_SSL_OUT_CONTENT_LEN` default 4096 |
| **Buffers, per session** | **20,480 bytes** | the sum, and the Kconfig confirms it: the asymmetric default *"saves 12KB"* against 16384+16384 |
| Plus | the `mbedtls_ssl_context`, the session, and handshake state (certificate, key, ECDHE), which is tens of KB **during the handshake** | `:74-80`, `:117-137` |

The project's own earlier reading, "roughly 40 KB of heap a session", is
consistent with buffers plus handshake plus context, and it is the figure
that has killed every TLS idea here: the social-media webhook, the fTelnet
proxy, OTA straight from GitHub.

**Can the in buffer be cut?** `MBEDTLS_SSL_IN_CONTENT_LEN` has a range down
to 512, and the Kconfig says plainly that cutting it is *"safe if the other
end of the connection supports Maximum Fragment Length Negotiation
Extension (max_fragment_length, see RFC6066) or you know for certain that
it will never send a larger message"*. **A browser is neither.** Browsers
do not generally negotiate RFC 6066 max_fragment_length and may legitimately
send a 16 KB TLS record. So the honest figure stands at about 20.5 KB of
buffers plus overhead per browser TLS session, and the one real mitigation
in-tree is `MBEDTLS_DYNAMIC_BUFFER` (default n), which allocates TX and RX
only while a record is being sent or received and frees them after — real,
and it trades peak heap for fragmentation and a smaller margin.

| Part | TLS sessions a sat could hold | Verdict |
|---|---|---|
| Base ESP32 or C3, no PSRAM (~180 KB usable DRAM on a WROOM, already running a SoftAP, lwIP with 16 sockets, an HTTP server and the link engine's 15-19 KB) | **a few at low concurrency** | **Viable, and this corrects what I expected to write.** `ausil/esp32-web-terminal` runs `esp_https_server` with an embedded `server.crt`/`server.key` on a 4 MB ESP32-C3, capped at `max_open_sockets = 4` with a 10,240-byte httpd stack. So it is done, in the field, today |
| ESP32-WROVER or S3 with PSRAM | **more**, limited by whether mbedTLS's buffers sit in PSRAM and by the handshake's internal-RAM needs | Plausible, unmeasured, and still not worth buying, for the next reason |

**So, answering Rob's question directly: no, encryption on this hop does
not force an S3.** I expected it to and the evidence says otherwise. **The
reason not to do it is the certificate, and it is a better reason than
memory ever was.**

A captive portal cannot intercept HTTPS at all — Espressif's own example
README says it will not redirect HTTPS requests, and no portal can, because
there is no certificate for the name a caller typed. A sat can only serve
TLS under its **own** name, and it has no name anybody has signed, so it is
a self-signed certificate and **every phone will show a full-page security
warning** before the caller can proceed. A BBS front door that opens with
"Your connection is not private — Attackers might be trying to steal your
information" is a worse experience *and* a worse security message than
plain HTTP with an honest sentence, because it trains people to click
through warnings. It is also exactly the wall RFC 8908 runs into (§5): the
modern standard requires a certificate the client validates, and *"MUST NOT
proceed"* otherwise.

The one thing TLS would buy that plain HTTP cannot: **brotli**, which
browsers advertise only over HTTPS. About 20% off the terminal's 86 to
135 KB, or roughly 20 KB, on a sat with 3.3 MB spare. Not a reason.

**So: plain HTTP, and say so.** Which is what this project already does
everywhere else.

### 7.3 Hop 3, end to end, caller to board: SSH, and it is S3-only

`BBS_HAS_SSH` defaults to 0 (`src/config.h:411-412`) and is set to 1 only
in the ESP32-S3 profiles in `src/board.h`. Its budget is PSRAM:
`BBS_SSH_PSRAM_EACH` 48 KB a session and `BBS_SSH_PSRAM_KEEP` 128 KB held
back, over a 16 KB **internal** task stack (`src/config.h:429-437`). A
classic ESP32 has no PSRAM and no SSH, by design and permanently.

For this feature that means: **a gateway caller reaching a base board
cannot have end-to-end encryption at all, and does not need the complexity
of trying.** A gateway caller reaching an S3 board *could* in principle be
carried over SSH — the sat would run an SSH client, which is exactly the
heavy thing §6.1 is pleased to have dropped — but it buys nothing, because
the hop that is actually exposed is hop 2, which SSH does not cover. **SSH
over the link would encrypt the already-encrypted hop and leave the clear
one clear.** Reject it.

### 7.4 The matrix, and what is honest to offer

| Hop | Base ESP32 sat / base board | S3 sat / S3 board | Honest? |
|---|---|---|---|
| **Phone to sat** (open AP) | **clear**. Anyone in range reads it | **clear**, unless TLS with a self-signed certificate that warns on every phone | **Offer it clear and say so.** TLS here is not worth a warning page |
| **Sat to board** (ESP-NOW) | **sealed**, AES-128-CCM, ~94 µs a frame | **sealed**, ~78 µs a frame | **Already done.** Nothing to decide |
| **Through a relay** | **sealed end to end**; the relay cannot open a frame | same | **Already specified.** A relay sees traffic patterns, never content |
| **Caller to board end to end** | not available; no SSH on a classic part | possible over SSH, and pointless while hop 1 is clear | **Do not offer.** It would imply a protection the first hop does not give |

**Which combinations to offer:**

- **Offer:** a base-ESP32 sat with a base board. Clear on the AP, sealed on
  the air, and said plainly. That is the whole product and it runs on the
  cheapest hardware the project supports.
- **Offer:** any mix of parts. **The sat and the board need not be the same
  class**, and nothing in the three hops couples them: the sat's job is an
  AP, a portal and a WebSocket; the board's job is a caller line. A base
  sat feeding an S3 board and an S3 sat feeding a WROOM are both fine, and
  the per-hop answers do not change.
- **Do not offer:** TLS on the sat in phase 1 to 4. Revisit only if
  somebody finds a way to get a certificate a phone trusts onto a box in a
  field, which in practice means a real domain and a real CA and an
  internet connection, which is the opposite of this feature.
- **Do not offer:** staff elevation to a gateway caller. **This follows the
  project's own precedent exactly and for the same reason.** The browser-SSH
  decision of 2026-10-04 was "no elevation at all from the web", because
  the staff password's only rate limit is the address ban (three wrong in
  fifteen minutes) and a relay's address cannot be banned without locking
  out every browser caller. Here it is worse: the ban list keys on an IPv4
  address (`guard.h:85`, `Entry* slotFor(uint32_t ip)`) and **a gateway
  caller has no address at all**. Synthesise one per sat and a single bad
  guess bans every phone on that sat; synthesise one per phone and the
  phone changes its MAC. So `staffPassword` must return `Access::None` for
  a gateway caller before any comparison, the same shape as "no staff over
  RF" and "no staff from the relay". Rob elevates over telnet, SSH or the
  console. **This is not a nicety; without it the open AP is an unlimited
  guessing path at the one password that owns the board.**

### 7.5 The wording, since the project's precedent is to say rather than pretend

Copy is `explain`'s, but the shape follows the connection line that already
ships (`--> This connection is not securely encrypted`), and these are
offered as a starting point rather than a decision:

On the portal page, above the board list:

> This is an open Wi-Fi network with no password, so anything you type here
> can be read by anyone nearby with the right equipment. The hop from this
> box to the BBS is encrypted; the hop from your phone to this box is not.
> Say what you would say out loud, and use a password you use nowhere else.

On the board's own connection line, when the caller arrived by gateway, at
80 and at 40:

> `--> You came in over an open Wi-Fi gateway, not encrypted`
> `--> Open Wi-Fi gateway, not encrypted`

(39 and 34 columns; both fit a C64.) And if a caller tries `BYE
<password>`, the plain logoff a guest already gets, with nothing counted.

---

## 8. Latency: what a caller feels

A terminal is a harder requirement than a camera: a picture that takes four
seconds is fine, and a keystroke that takes two hundred milliseconds to
echo is not. The board's own echo over telnet was measured on the bench at
**p50 4 ms and p95 12 ms with five callers**, so that is the baseline to
compare against.

### 8.1 The chain, term by term

| Term | Per direction | Note |
|---|---|---|
| Phone to sat over Wi-Fi, phone **awake** | about 1 to 3 ms | ordinary 802.11 with a short queue |
| Phone to sat, phone in **power save** | **up to about 205 ms downlink** | beacon interval 100 TU (102.4 ms) and `dtim_period` 2, both IDF SoftAP defaults (`esp_wifi_types_generic.h:342-344`). `dtim_period = 1` halves it. **This is the biggest single term in the whole chain and the one to measure first** |
| Sat's HTTP/WS task to its link engine | ~0 if event-driven, 0 to 20 ms if it polls on a tick | a design decision, §8.3 |
| Seal the frame | 94 µs on an ESP32, 78 on an S3 | measured |
| Air, one 250-byte frame at 24 Mbps with its MAC ACK | about **0.2 ms** | 250 bytes is 83 µs of data; preamble, SIFS, ACK and DIFS make it about 200 µs |
| Board: into the control ring from the Wi-Fi task | immediate | the receive callback copies and returns |
| Board: the link plugin's tick drains it | **0 to 20 ms, mean 10** | `BBS_PLUGIN_FAST_MS 20` (`config.h:257`), `PF_FAST` |
| Board: open and dispatch the frame | **150 to 184 µs** | measured, whole control frame on the loop |
| Board: the echo into the session's timeline and out | **0 to 20 ms, mean 10** | the tick hands the radio one frame when idle; the timeline drains on the 10 ms loop (`BBS_SELECT_MS 10`) |

### 8.2 Round trips

With an awake phone and event-driven sats:

| Path | Added round trip | Total echo, p50 | Feel |
|---|---|---|---|
| Telnet, for reference | — | **4 ms** | the baseline |
| Gateway, direct to the board | about **+20 ms mean, +40 ms worst** | about **25 ms** | indistinguishable from direct |
| One relay hop, event-driven | **+2 to 6 ms** | about **30 ms** | fine |
| Two relay hops, event-driven | **+4 to 12 ms** | about **35 ms** | fine |
| Three relay hops, event-driven | **+6 to 18 ms** | about **40 ms** | fine |
| One relay hop, **polling on a 20 ms tick** | **+40 ms** | about **65 ms** | noticeable but usable |
| Two hops, polling | **+80 ms** | about **105 ms** | **a caller notices** |
| Three hops, polling | **+120 ms** | about **145 ms** | **clearly laggy** |
| Any of the above with a phone asleep at DTIM 2 | **+100 to 205 ms** | **125 to 350 ms** | **unusable** |

Two conclusions fall straight out, and they are the useful part of this
section:

- **Crypto is not the problem and never will be.** Under 400 µs of CCM
  round trip against a 20 to 40 ms chain. Nobody should spend a day
  optimising it.
- **Polling granularity is the problem**, and the sat's AP power-save
  behaviour may be a bigger one. Both are design or configuration
  decisions, not hardware ones, and both are free to fix.

### 8.3 The design rule this produces

**A repeater carrying a terminal forwards from its receive callback on its
own task, not on a poll tick.** That single decision is the difference
between three hops being comfortable and two hops being annoying. It is
also easy: the relay does no crypto, no reassembly and no session work on
the forwarded traffic, so its forwarding path is "copy out of the ring,
prepend two bytes, send", which is a task that sleeps on a notification and
is exactly the shape LINK.md already uses for the camsat bulk path.

The board's own 20 ms link tick is the other 40 ms, and it is worth a note
for whoever builds CALLIN: the loop runs every 10 ms and the link is polled
every 20. Two options, neither to be chosen from a desk:

- **drop `BBS_PLUGIN_FAST_MS`** to 10, which halves the added latency and
  doubles every `PF_FAST` plugin's tick rate board-wide. Blast radius, and
  Rule no. 1 says measure the loop before and after;
- **wake the loop on frame arrival.** The SSH work already added
  `plat::runWake` and the loop already selects on an eventfd for SSH, so a
  notification from the ESP-NOW receive callback into the loop's select is
  a known shape. Better, and more work.

Recommend: **build CALLIN on the existing 20 ms tick, measure it on a real
phone in phase 2, and only then decide.** 25 ms p50 may simply be fine, in
which case this is a paragraph nobody has to act on, and that is the right
outcome for a performance worry.

Also worth recording: **the `kAckDelayMs 20` and `kRtoMs 150` timers are
sized for bulk pictures, not keystrokes.** A keystroke is a single-frame
reliable message, so a lost frame costs 150 ms before the first retry,
which a caller sees as a visibly dropped character and then a stutter. On a
congested fairground channel that could happen often. **Whether a terminal
wants a shorter first retry is a real question and nobody has measured a
packet loss rate in a crowd.** Phase 2.

---

## 9. Repeating, and the fan-out that answers the scale question

### 9.1 Confirmed: repeating and CALLIN are independent and compose for free

The brief's reading is right, and plainly so. `sat-types-2026-09-27.md` §1
specifies the one-hop relay as forwarding **sealed frames it cannot open**,
with a two-byte wrapper (one byte "relayed", one byte the far sat's index
in the relay's table) and the frame unchanged beneath it. The wrapper sits
**below** the family layer; the relay never parses the header, never holds
a session key for the traffic, and the end-to-end ECDH, `k_link` and every
session key are the board's and the far sat's alone.

**So a repeater never learns what CALLIN is, and CALLIN never learns there
is a repeater.** They are independent features that compose, and neither
needs to know about the other. What that means for the design, concretely:

- **nothing in the CALLIN family specification changes for repeating**, and
  nothing in the relay changes for CALLIN. Designing them "together" means
  only not designing CALLIN in a way that breaks the property — and the one
  way it could is by putting a per-frame assumption about the sender's MAC
  into the family layer, which the engine already avoids by identifying a
  peer before dispatch;
- the one real protocol consequence `sat-types` already names:
  **`kPayloadMax` becomes per peer**, 220 bytes instead of 222 for a
  relayed peer, because the wrapper takes two of the 250. About 1% of
  throughput, and for a terminal it is nothing;
- **the latency rule in §8.3 is the only genuinely new thing**, and it is a
  relay implementation decision, not a protocol one;
- paths are learnt, not configured (the board keeps the next hop it last
  heard an authenticated frame through), so a fairground's topology needs no
  configuration and a sat that can sometimes reach the board directly just
  works, the duplicate dropped by the replay window.

So Rob's instruction to build repeating into CALLIN's design resolves to:
**write the CALLIN spec so that it never cares where a frame came from, and
say in LINK.md that CALLIN is relay-transparent.** That is a sentence, not
a work item.

### 9.2 One hop is costed; two is not, and the gap is large

`sat-types` prices the one-hop relay at **about 1.5 weeks with bench** and
the multi-hop mesh at **3 to 5 weeks, most of it bench**, with routing,
loop control and path repair, and says plainly that a mesh is **not
planned**. Those two figures are a long way apart and the 1.5 does not
stand in for the 5.

**Does a fairground need more than one hop?** Probably not, and the
reasoning is about the topology rather than the radio:

- a fairground is **flat, open ground with no buildings in the way**, which
  is the case 2.4 GHz is good at. `sat-types` is explicit that the case a
  relay is for is "a building in the way", because "2.4 GHz does not bend
  round a house";
- the topology is **planned by whoever plants the boxes**, not discovered.
  If a corner of the site cannot hear the board, the answer is to move a
  box or add a board, both of which a sysop with a bag of ESP32s can do on
  the day;
- and the cheap escape is **another board**, not another hop. A second
  board in the far corner with its own sats costs one more ESP32 and buys
  ten more lines as well as coverage.

**The middle ground, if Rob wants reach without a mesh.** A **fixed
two-hop chain with no routing**: a far sat is told, at pairing time, that it
reaches the board through relay R, and R is told it reaches the board
directly. No routing protocol, no path repair, no loop control, no
flooding, no TTL — because there is nothing to discover. That is much closer
to the 1.5-week figure than to the 5-week one, perhaps **1.5 weeks plus
half a week**, because the only addition over one hop is that the wrapper
can nest once and the relay can forward to another relay rather than only
to the board. **This is the honest answer to "a fairground may want two":
yes, and a planned chain is cheap where a mesh is not.** It is also exactly
the shape `sat-types` recommends for a mesh if one were ever built ("B
computes the routes... source routing. A tree rooted at the board, not a
general mesh").

I have not costed the nesting properly and should not pretend to. **It is
an estimate from the shape of the existing estimate, and it needs the
one-hop relay built before it can be priced honestly.**

### 9.3 What a repeater costs in pairings: see §3.4

Summarised here because it is the question: **as specified, each far sat
behind a relay still takes one of the board's eight pairings**, so a relay
buys reach and not fan-out. But **pairings are not the wall for callers**,
because sessions multiplex over a pairing and the board only has ten lines
anyway. Raising `kPeers` from 8 to 20 costs about 2.9 KB of heap and no
static DRAM, and 20 is the ESP-NOW ceiling. A **concentrator** (one paired
box carrying many callers, terminating their sessions) is what scales past
that, and its cost is a change of trust rather than memory. §3.4 has the
full working, including the middle option that keeps the sealing.

---

## 10. Airtime at a fairground, and whether the LR test is urgent

### 10.1 What a few dozen boxes on one channel actually do

Everything shares one channel, by §4.1, and that includes:

- each sat's SoftAP beacons: **one beacon per sat per 102.4 ms**, which at
  thirty sats is about **290 beacons a second** on the channel before
  anybody types anything. At 1 Mbps (beacons go at the lowest basic rate)
  each is on the order of a millisecond of airtime, so **thirty sats spend
  roughly 25 to 30% of the channel on beacons alone.** That is a real,
  first-order cost and it is the single most surprising number in this
  report;
- phones' own traffic: a phone associated to an open AP with no internet
  will keep trying — captive-portal probes, push connections retrying, DNS
  that goes nowhere. Multiply by every phone that ever joined and wandered
  off;
- the ESP-NOW traffic itself, which is tiny: a keystroke is one 250-byte
  frame at 24 Mbps, about 0.2 ms. A hundred keystrokes a second across ten
  callers is 2% of the channel. **The BBS traffic is not the problem;** the
  infrastructure is;
- the venue's own Wi-Fi, other people's hotspots, and a field full of
  phones.

**Mitigations, in order of value:**

- **raise the beacon interval** on the sats. The field allows 100 to 60000
  TU. At 500 TU (512 ms) the beacon cost falls fivefold, at the price of
  phones taking longer to find the network. Worth measuring: a phone
  scanning for an SSID wants to see a beacon, and half a second is probably
  fine for someone standing still reading a sign;
- **spread the boxes over channels 1, 6 and 11** — but only if the boards
  are spread with them, because every board sharing a sat must be on one
  channel. So this is really "three independent clusters, each with its own
  board or boards", which is also the right answer for capacity (§3.2).
  **That is a nice convergence: the thing that fixes airtime also fixes
  lines.**
- **keep `dtim_period` at 1** for latency (§8.1), which costs nothing in
  airtime since the beacon goes out either way;
- **do not add LR to anything carrying callers** (§10.2).

**None of this is measured and all of it is arithmetic from the defaults.**
A fairground's RF environment cannot be predicted from a desk. The thing to
do is phase 2 in a room, then a deliberate test with half a dozen boxes and
half a dozen phones in a car park before anyone takes thirty to an event.

### 10.2 The LR test: do it, but not for this

`sat-types` §1 says the LR bench test (1 to 2 days, no new protocol)
"decides whether a relay is needed at all", and LINK.md records that the
question it must answer is not in Espressif's guide: **whether a board
whose station is associated to a router still receives LR ESP-NOW frames.**
That is still unanswered and still the gate on the relay.

**Is this feature what makes it urgent? No, and here is why:**

- LR's measured cost on the host is **gateway pings of 29 to 55 ms** on the
  bench, which is callers' latency and a Rule no. 1 matter. Against a
  terminal budget of 20 to 40 ms, adding 30 to 55 ms to the board's own
  radio is **the worst trade in this report**. LR must be **off** on any
  board carrying interactive callers;
- LR at 250 kbps also throws away the headroom the rate table bought. A
  terminal does not need throughput, but a field full of LR frames at 1/4
  Mbps occupies far more airtime per byte than 24 Mbps does, which makes
  §10.1 worse rather than better;
- and the fairground does not need distance, it needs coverage, which is
  what planting more boxes is for.

**So: run the LR test as the relay decision it already was, at its existing
priority, and record the conclusion that LR and interactive callers do not
mix.** If anything, this feature makes the test *less* urgent, because it
supplies a better answer to "the far corner cannot hear the board": put a
board in the far corner.

---

## 11. What a sat offers and how a caller picks

Rob: the portal "then shows the BBS systems on the ESPNOW network".

**How the sat knows which boards are reachable: pairings, and yes, that is
the obvious answer and it is the right one.** A sat already holds up to five
boards, each with its own `k_link`, and already knows for each one whether
its link is up (`hostUp(slot)`), the board's name (carried in `PAIR_OFFER`
and `BEACON`), and when it was last heard. So the board list is the
pairings list, filtered to the ones whose session is up. Nothing new.

**What stops a sat advertising a board that has not agreed.** Pairing. A
board appears on a sat's portal only if its sysop ran `LINK PAIR` and
answered the four-digit code, which is two physical acts, one at each end,
inside a short window. There is no way for a sat to list a board it has not
paired with, because it has no key for it and would be answered by nothing.
**That is a complete answer and it needed no new mechanism**, which is
worth saying because "an open AP advertising other people's BBSes" sounds
alarming until you notice it is impossible.

The reverse consent is worth a thought too: **a board should be able to
refuse to be gatewayed.** A sysop who pairs a camera sat has not thereby
agreed to take anonymous callers off an open AP in a car park. So the
gateway wants a switch on the board's side — a `CONFIG` row, off by
default, in the spirit of "a board is closed until its sysop opens it".
Recommend: the gateway plugin ships **off**, and a board that has not
switched it on answers a sat's CALLIN attempt with a refusal the sat shows
as "this board is not taking gateway callers".

**How fresh the list is.** A peer sends `PING` every 5 s and a host marks a
peer down after 20 s of quiet (`kPingMs 5000`, `kHostQuietMs 20000`). From
the sat's side, three missed PINGs trigger a rescan. So "is this board up"
is accurate to about 5 to 15 seconds, which is plenty for a page a caller
looks at for a few seconds. **Do not poll on page load**; serve what the
link already knows, so the page costs nothing.

**Whether a board has a line free** is new information the link does not
carry today. Three options:

- **the board tells the sat**, in a small CALLIN message sent when its
  caller count changes — the same shape as announce's join/leave nudge.
  Cheap, accurate, and it is what to build;
- **the sat asks when the caller taps**, and shows "busy, try another" if
  refused. No new message at all, and the caller finds out one tap later;
- **show nothing** and let the board's own busy line answer. The board
  already has a complete busy experience (a busy screen, a ten-second
  countdown, a drop) and a gateway caller meeting it is not a failure.

Recommend **the second for phase 4 and the first for phase 5**: start with
no new message, add the nudge when it is clear people are hitting busy
lines. A page that says "2 of 10 lines free" is lovely and is not worth
blocking phase 4 on.

**What a caller sees when every line on every board is busy.** The portal
says so, with the number of boards and the wait, and offers the plain
address so they can try again from a real client. The board's own busy
screen is the fallback and already exists. Do **not** build a queue: a
queue on a ten-line board is a way of making somebody watch nothing happen,
which is this project's own recorded position on exactly that question.

---

## 12. The failure modes, which are the interesting part

A fairground is a hostile environment for this, and each of these is worth
a decision rather than a discovery.

**A phone roams between two sats mid-session.** Wi-Fi roaming is the
phone's decision and it will happen the moment somebody walks. The
WebSocket is to sat A; when the phone associates to sat B, that TCP
connection is gone. **The session should not survive, and should not try
to.** A caller's node holds their login, their minutes, their place in a
forum; reconnecting to sat B and resuming would mean the board matching a
new stream to an existing session, which is a session-hijack mechanism
however it is keyed, and a phone's MAC is not an identity. **Recommend: the
line drops, the board sees a clean hang-up, the caller taps again on sat B
and logs in again.** The portal page should say so in one line so walking
away from a sat is an understood thing rather than a bug. The board's
idle-hangup and the ten-second linger already handle the orphaned side.

A softer version is worth considering later: the sat detects the
disassociation and sends the board a clean `CLOSE` rather than letting the
session wait out its idle timer, so the line is freed in a second rather
than minutes. **That is worth building in phase 4**, because on a ten-line
board a walked-away caller holding a line for the idle timeout is the
difference between ten lines and three.

**A sat loses its board.** Three missed PINGs and a rescan; callers on it
see the door framework's existing behaviour, which is the right model:
`--> Lost the signal.` then `--> Back home.` For a gateway there is no
"home" to go back to, so the terminal should say the board went away and
offer the board list again. The sat keeps its AP and its portal up
throughout, which is important: **a sat whose board is gone must still
serve a page that explains that**, not a dead connection.

**Battery and power for a few dozen boxes.** Not a firmware problem and the
biggest practical one. An ESP32 running a SoftAP does not sleep, draws
roughly 100 to 180 mA average with the radio busy, and a 10,000 mAh USB
battery is therefore a day or so a box. Thirty boxes is thirty batteries,
thirty USB cables and somebody walking the site. Things that help: raising
the beacon interval (§10.1) saves transmit time but not much average
current; `esp_wifi_set_max_tx_power` down a few dB where coverage allows;
and accepting that this is an event deployment with a charging plan.
**Worth saying on the site when this ships, because somebody will plant
thirty boxes and be surprised at hour eighteen.**

**Somebody plants their own AP with the same SSID.** Trivially possible on
an open network and there is no defence at the Wi-Fi layer: an open SSID is
a name anybody may use, and a phone will join whichever it likes. What an
attacker gets is every caller's password typed into their page. **The only
real mitigations are social and physical:** an SSID that is specific rather
than generic, the address printed on the box, and the honest line on the
portal telling people not to use a password that matters anywhere else.
That last one is already the project's position on telnet generally and it
covers this case exactly. **Record it as a known and accepted risk, in
those words, rather than implying the sat can prevent it.**

**A caller misbehaves and cannot be banned.** §7.4. The ban list keys on an
IPv4 address, a gateway caller has none, and a phone can change its MAC. So
the guard's rate limits are toothless for gateway callers. Consequences,
all of which should be decided now:

- **no staff elevation**, as §7.4 requires, because that is the one that
  matters;
- the per-handle account lockout (five wrong in fifteen minutes) **still
  works**, because it keys on the handle, not the address. So account
  passwords are protected as well as they are over telnet;
- `KICK` works, because it acts on a node;
- a sysop who wants a troublemaker off the site has to switch the gateway
  off, or unpair the sat. **A `SATS`-level "stop taking gateway callers
  from this sat" would be a useful thing to have and is cheap**: it is a
  flag in the pairings file beside `recv` and `camno`.

**The caller log and WHO have nothing to put in the address column.**
`Session::ip[16]` is text and `ipAddr` is a `uint32_t`. A gateway caller
wants something there, and the useful thing is **the sat's name**, because
that is actionable information for a sysop ("three bad logins from the
gateway by the beer tent"). So gateway callers show as the sat, marked, the
same way a guest is marked with `*`. That is a display decision for
`tty-ux` and a one-field decision for whoever builds CALLIN.

**A phone that joins and never opens the portal.** Common: people join open
networks and wander off. Each one holds one of the sat's 15 association
slots indefinitely. Two separate reapers are needed and only one is free:

- **the HTTP side is covered**: `httpd_config_t.lru_purge_enable` (default
  **false**) closes the least-recently-used connection instead of refusing
  a new one. **Turn it on**, as the real project does, or the fifth phone
  to open a socket is simply told no;
- **the association side is not checked.** Whether IDF 5.3.1's SoftAP ages
  out an idle station, and whether there is a knob for it, was not
  established for this report. At a busy event a sat full of idle
  associations serves nobody, so **find out in phase 2**.

There is also no WebSocket heartbeat: `keep_alive_enable` defaults false
and PING/PONG reach a handler only with `handle_ws_control_frames` set, so
a phone that walks out of range holds its socket until TCP keepalive or
your own timer notices. **A sat-side idle timer is wanted**, which is also
what sends the board the clean `CLOSE` that frees the caller line (§12,
roaming).

---

## 13. Against what is already planned

The 1.2.3 entry in CLAUDE.md already specifies most of the core half of
this, and it is worth being precise about the division so nothing is built
twice.

**Already planned, and this feature consumes it rather than adding to it:**

- **the caller-line core**: "a session whose bytes come from a stream, not
  a socket, taking one of the 10 caller nodes while in use (zero extra RAM;
  telnet and SSH share the same 10)". That is exactly what a gateway caller
  needs;
- **`Session::link`**, the generalisation of line kinds (socket, SSH,
  serial, CALLIN, console) that the 1.2.3 entry names;
- **CALLIN over the link**, LINK.md's reserved family 3 and `KIND_GATEWAY`
  = 3, both already in the table with "reserved, 1.3.0" against them. The
  numbers exist; nothing sends or takes them;
- the 1.2.3 entry's own line: **"Up to 10 such sats can be the board's 10
  lines with no telnet at all"**, which is this feature's premise already
  written down;
- **the hardware button** on the terminal sat (press to hang up, press to
  connect), which maps onto a gateway sat as a physical "clear every
  session" for a sysop walking the site;
- the **serial sat** and the **1.3.0 ham radio dock**, both of which reuse
  CALLIN unchanged. **So building CALLIN for the gateway pays for both**,
  which is a strong argument for the ordering in §15.

**New, and sat work in a new repository:**

- the SoftAP, DHCP and the DNS redirector;
- the captive portal and its OS probe handling;
- the board list page and the picker;
- the browser terminal and the WebSocket bridge;
- the sat's own CONFIG (SSID, channel, which boards to offer);
- the relay's event-driven forwarding, if and when.

**New, and small core work beside CALLIN:**

- `staffPassword` returning `Access::None` for a gateway line (§7.4);
- the address column and the `*`-style marker for a gateway caller;
- a `CONFIG` switch so a board opts in to being gatewayed (§11);
- the connection line's gateway wording (§7.5);
- optionally, the free-lines nudge to the sats (§11).

**The naming question** (§1) touches `satwords.h` and nothing else.

---

## 14. Where this meets the broker study

The two studies answer the same shape of question for different distances
and they barely overlap, which is the right outcome.

- **The broker study** (`internal/study-broker-sat-2026-10-05.md`) is about
  a caller **on the internet** reaching a board with no forwarded port: a
  rendezvous, the directory as a broker, a box with reach, `.onion`. Its
  own §5.3 concludes an ESP32 cannot be that box.
- **This study** is about a caller **standing next to the board** with
  nothing but a phone. No internet, no droplet, no DNS that leaves the
  field, nothing that a microcontroller cannot do.

They meet in exactly three places:

1. **The word "repeater"**, which both want for different things (§1).
   That must be settled once, in one line, before either ships.
2. **The browser terminal.** The broker study's path and
   `plan-web-ssh-2026-10-04.md` both need xterm.js in a browser against the
   board's own detector and its CP437 art, and both record that as
   unverified. **This feature verifies it for free in phase 1**, on a
   simpler path with no relay and no SSH in the way. So doing phase 1 first
   de-risks browser SSH and the broker path at no extra cost, which is an
   argument for ordering that nobody would otherwise notice.
3. **"No staff from a path whose address cannot be banned."** The web-SSH
   decision of 2026-10-04 established the rule; this feature is the second
   instance and the stronger one, since a gateway caller has no address at
   all. **It is now a general rule rather than a web-SSH rule** and belongs
   in CLAUDE.md as one: *a caller whose line cannot be attributed to a
   bannable address is never staff.* That covers RF, the relay, the gateway
   and whatever comes next.

Otherwise they are independent and can be built in either order.

---

## 15. Phases

Each ships alone. Days are working days including code review, and exclude
Rob's own bench time.

### Phase 1: the local gateway. 4 to 6 days. New sat repo.

**The smallest thing that proves the whole path end to end**, and a
complete product: one sat, one board, one phone, in a room.

- a sat firmware: open SoftAP with DHCP, the DNS redirector (the IDF
  example's component, vendored, since it is example code rather than a
  registry component), the portal, a WebSocket, and xterm.js embedded in
  the app with `EMBED_FILES`, gzip-precompressed at build time, lazy-loaded
  so the landing page is a few KB;
- **decide 5.5.0 against a 6.1 beta here** (§6.1): a one-line change in
  phase 1 and a 48 KB difference;
- the bridge is **WebSocket to telnet on the board's own IP**, because the
  board joins the sat's AP as a station (§4.2a). **No link, no CALLIN, no
  pairing, no new protocol, and no core change at all;**
- the portal page lists the one board it is pointed at, by address;
- the honest line about the open AP, and the plain-address fallback.

What it proves that nothing else can: that a phone pops the portal, that
xterm.js satisfies the board's terminal detector, that CP437 art draws,
that the keystroke echo is bearable, and that a base ESP32 can hold it.
**It is also the phase that is allowed to kill the plan**, and it does so
for a week's work rather than a month's.

### Phase 2: measure it. 1 to 2 days. Needs Rob.

- keystroke echo p50 and p95 on a real phone, screen on and screen off;
- `dtim_period` 1 against 2;
- the portal on iOS, Android and Windows, with and without Private DNS,
  **and specifically whether a recent Android or Samsung phone pops it at
  all** (§5), trying `172.x.x.x` and a 200-with-body as well as
  `192.168.4.1` and a 302 if it does not;
- whether the Apple CNA sheet runs the WebSocket, or whether the terminal
  has to open in the real browser (§5);
- how many phones hold a terminal at once before the sat's heap or sockets
  give out, and the heap low on a **base ESP32 with no PSRAM** (§6.1);
- whether an idle association is ever reaped (§12);
- what the board's SYS says: loop worst, slow passes.

Every number in §8 and §10 of this report is arithmetic from defaults and
wants replacing with a measurement. **This phase needs Rob's explicit go,
like every test plan.**

### Phase 3: the caller-line core and CALLIN. 5 to 8 days. Core.

1.2.3 as already planned: a session fed by a stream, `Session::link`,
family 3, `KIND_GATEWAY`. Plus the small core items in §13: no staff on a
gateway line, the address column, the opt-in switch, the connection line's
wording. The serial sat and the 1.3.0 ham dock both ride on this.

### Phase 4: the distributed gateway. 4 to 6 days. Core + sat.

- the sat speaks CALLIN over the link instead of telnet;
- the portal lists every paired board whose link is up, with a picker;
- a busy board refuses and the sat says which others are free;
- the sat sends a clean `CLOSE` when a phone disassociates, so a walked-away
  caller frees the line in a second rather than minutes (§12).

After this, sats no longer need to be in the board's AP range and the
fairground picture works.

### Phase 5: raise the lines. 2 to 3 days. Core.

- `kPeers` 8 to 20 (about +2.9 KB of heap, no static DRAM);
- `BBS_MAX_NODES` 10 to 14 on the boards that can hold it, read off the ELF
  per profile, with the `takeTraffic` assert as the stated wall;
- the free-lines nudge from board to sat (§11).

### Phase 6: the relay, one hop, event-driven. 8 to 10 days. Needs open ground.

`sat-types` §1 as specified, with §8.3's rule: forward from the receive
callback on its own task, never a poll tick. Measure the per-hop latency,
which nobody has.

### Phase 7: a fixed two-hop chain, if phase 6's bench says one is not enough. 4 to 6 days.

No routing, no repair, no flooding: the chain is configured at pairing
because the topology is planned. **Estimated from the shape of the one-hop
figure and not independently costed** (§9.2).

### Ordered by value for effort

1. **Phase 1**, by a wide margin. A week, a complete product, and it
   answers every unanswerable question including two the browser-SSH plan is
   carrying.
2. **Phase 2.** Nothing else should be designed before these numbers exist.
3. **Phase 3**, which pays for the serial sat and the ham dock as well.
4. **Phase 5**, two days for 40% more lines.
5. **Phase 4.**
6. **Phase 6**, and only if a bench says reach is actually short.
7. **Phase 7**, only on evidence.

---

## 16. What Rob must decide

**Needed before phase 1:**

1. **"Repeater" or "relay" for the ESP-NOW forwarding box** (§1). One word,
   two studies, settle it now. My recommendation: **relay** here, as
   `sat-types` already has it, and **repeater** for the broker study's
   internet box.
2. **Phase 1's shape: a telnet bridge over the sat's own AP, with the board
   joined to it as a station** (§4.2a). It is the cheapest honest path and
   it means no core change in phase 1. The alternative is to do CALLIN
   first, which is three times the work before anything can be shown to a
   phone. **Recommend (a).**
3. **The test plan for phase 2** needs his explicit go, per the standing
   rule.

**Not this feature's, but found by it and his to schedule:**

4. **The 16-socket ceiling is a property of the pin, not the chip** (§0).
   `LWIP_MAX_SOCKETS` is `range 1 16` at the pinned IDF 5.3.1 and
   `range 1 253` from 5.3.2 on, verified from two frameworks on this
   machine. That does not change what ten caller lines costs in RAM, but it
   does change the sentence CLAUDE.md carries, and it means a patch bump
   inside the 5.3 line is the lever if telnet lines are ever wanted past
   ten. **Recommend: correct the CLAUDE.md wording now, and treat the
   framework bump as its own piece of work with its own 23-environment
   revalidation.** Nothing in this feature needs it.
5. **`dev-tunnels-ssh` offers no AES-128 at all**, only `aes256-ctr` and
   `aes256-gcm@openssh.com`, read from its own algorithm list (§2). The
   1.2.1 work narrowed wolfSSH's advertised cipher list; if that list ever
   loses AES-256, browser SSH breaks silently, the same trap already
   recorded for the P-256 host key. **One line beside the cipher list.**

**Needed before phase 3:**

4. **No staff elevation on a gateway line** (§7.4). I believe this is
   forced rather than optional, and the precedent is his own decision of
   2026-10-04, but it is his to confirm because it means he cannot elevate
   from his own phone at his own board.
5. **A board opts in to being gatewayed**, off by default (§11). Recommend
   yes, in the spirit of "a board is closed until its sysop opens it".
6. **The wording** for the portal's open-AP line and the board's gateway
   connection line (§7.5). Mine are a starting point; `explain` should own
   the final copy.

**Needed before phase 6, and not before:**

7. **The concentrator and its trust trade** (§3.4). One paired box carrying
   many callers scales past twenty APs and sees every caller's keystrokes.
   The precedent that covers it is the serial transport's "trusted by the
   wire", and it is only honest when the sysop owns every box. **Recommend
   deferring this decision until somebody actually plants more than twenty
   boxes for one board.**

**Answered, so he need not decide:**

- **TLS on the sat: no**, and **not for the reason I expected.** It is not
  a memory question: a real project runs HTTPS on a 4 MB ESP32-C3. It is
  the certificate — self-signed warns on every phone, a portal cannot
  intercept HTTPS at all, and RFC 8908 hits the same wall. §7.2.
- **Does encryption over ESP-NOW force an S3: no.** It is built, it costs
  about 94 µs a frame on the cheapest part, and it is one of the few places
  here where the secure option is also the cheap one. §7.1.
- **Does the browser terminal force an S3: no**, and it is now an existence
  proof rather than arithmetic: somebody ships SoftAP plus a gzipped
  xterm.js in the app image plus a WebSocket on a 4 MB ESP32-C3.
  86 to 135 KB gzipped against 3.3 MB free. **Internal RAM on a PSRAM-less
  WROOM is the one open question**, and a WROVER is the hedge if phase 2
  says it is tight. §6.1.
- **End-to-end SSH for a gateway caller: no.** It would encrypt the hop
  that is already encrypted and leave the clear one clear. §7.3.
- **Is the LR test now urgent: no**, and LR should be off on any board
  carrying interactive callers. §10.2.

---

## 17. Sources

**Primary, read out of the pinned framework**
(`~/.platformio/packages/framework-espidf@3.50301.0`, `version.txt` =
5.3.1):

- `components/esp_wifi/include/esp_now.h` — `ESP_NOW_MAX_TOTAL_PEER_NUM`
  20, `ESP_NOW_MAX_ENCRYPT_PEER_NUM` 6, `ESP_NOW_MAX_DATA_LEN` 250,
  `esp_now_peer_info_t.ifidx` and `.channel`, `esp_now_recv_info_t`,
  `esp_now_set_peer_rate_config`
- `components/esp_wifi/Kconfig` — `ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM`
  (default 7, range 0-17) and the hardware-key sharing paragraph
- `components/esp_wifi/include/local/esp_wifi_types_native.h` —
  `ESP_WIFI_MAX_CONN_NUM` 15 for ESP32 / S2 / S3
- `components/esp_wifi/include/esp_wifi_types_generic.h` —
  `wifi_ap_config_t.beacon_interval`, `.dtim_period`, `.csa_count`
- `components/esp_http_server/Kconfig` — `HTTPD_WS_SUPPORT`, default n
- `components/esp_http_server/include/esp_http_server.h` — the config
  defaults and the three reserved sockets
- `components/mbedtls/Kconfig` — `MBEDTLS_ASYMMETRIC_CONTENT_LEN`,
  `SSL_IN_CONTENT_LEN` 16384, `SSL_OUT_CONTENT_LEN` 4096,
  `MBEDTLS_DYNAMIC_BUFFER`
- `examples/protocols/http_server/captive_portal/` — the README's claim
  about iOS, Android and Windows and about HTTPS; `main/`'s 302 and the
  comment that iOS needs a body; the `dns_server` component
- `components/lwip/Kconfig` — `LWIP_MAX_SOCKETS` `range 1 16`,
  `LWIP_MAX_ACTIVE_TCP` and `LWIP_MAX_LISTENING_TCP` `range 1 1024`

**Primary, read out of the newer framework also on this machine**
(`framework-espidf@3.50503.0`, IDF 5.5.x), as the corroborating half of the
socket finding:

- `components/lwip/Kconfig` — `LWIP_MAX_SOCKETS` `range 1 253`, with the
  `FD_SETSIZE` caveat above 61

**Primary, read out of this repository:**

- `src/config.h`, `src/core/bbs.h`, `src/core/link.h`, `src/core/link.cpp`,
  `src/core/linkcrypto.h`, `src/core/guard.h`, `src/core/satwords.h`,
  `src/plugins/link.cpp`, `src/platform/linkradio_esp32.cpp`,
  `src/main.cpp`, `sdkconfig.defaults`, `src/board.h`
- `LINK.md` (the measured CCM figures, the rate table, the channel rules,
  pairing, sessions, the family table, the budgets)
- `internal/sat-types-2026-09-27.md` §1 (the one-hop relay, the two-byte
  wrapper, the mesh estimate, the LR question)
- `internal/plan-web-ssh-2026-10-04.md` §1, §3, §5 (xterm.js 6.0.0 and
  dev-tunnels-ssh 3.12.42 versions, licences and sizes, re-verified
  2026-10-04; the CP437 and detector questions; the no-SSH-auth fallback)
- `internal/study-broker-sat-2026-10-05.md` §5.1-5.3 (the "repeater" name)
- `CLAUDE.md` (Rule no. 1, the socket history, the 2026-09-22 captive
  portal ruling, the 2026-10-05 "no website on the board" constraint, the
  2026-10-04 no-staff-from-the-relay decision, the bench echo figures, the
  dark-board trap of 2026-10-01)
- `unleashed_camsat/firmware/partitions.csv` and
  `unleashed_camsat/release/1.1.0/assets/firmware.bin` (789,072 bytes)

**Published, fetched for this report (the research half):**

- [ESP-NOW, ESP-IDF v5.3.1](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-reference/network/esp_now.html)
  — transmit via both Station and SoftAP interfaces; `ESP_ERR_ESPNOW_IF`
  and `ESP_ERR_ESPNOW_CHAN`
- [Wi-Fi driver guide, ESP-IDF v5.3.1](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-guides/wifi.html)
  — the AP+STA channel priority and the Channel Switch Announcement; the
  SoftAP `max_connection` default of 10, 15 on an ESP32, and the 17
  hardware keys shared with ESP-NOW
- [esp_http_server, ESP-IDF v5.3.1](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-reference/protocols/esp_http_server.html)
  — WebSocket support behind `CONFIG_HTTPD_WS_SUPPORT`; no footprint
  figures published
- `esp_netif_types.h` at the [v5.3.1](https://github.com/espressif/esp-idf/blob/v5.3.1/components/esp_netif/include/esp_netif_types.h)
  and [v5.4](https://github.com/espressif/esp-idf/blob/v5.4/components/esp_netif/include/esp_netif_types.h)
  tags — `ESP_NETIF_CAPTIVEPORTAL_URI = 114` absent, then present
- `components/lwip/Kconfig` at [v5.3.1](https://raw.githubusercontent.com/espressif/esp-idf/v5.3.1/components/lwip/Kconfig)
  and [v5.3.2](https://raw.githubusercontent.com/espressif/esp-idf/v5.3.2/components/lwip/Kconfig)
  — where the socket range changed
- [ESP-IDF issue #18816](https://github.com/espressif/esp-idf/issues/18816)
  — a pre-compressed-gzip example, closed "Won't Do", with its
  348 KB → 108 KB figure
- [ESP-FAQ, ESP-NOW](https://docs.espressif.com/projects/esp-faq/en/latest/application-solution/esp-now.html)
  (cited by LINK.md; currently behind a ReadTheDocs login, so not
  re-fetched)

**Captive-portal behaviour, per vendor:**

- Apple: [enterprise network ports and hosts](https://support.apple.com/en-us/101555)
  (`captive.apple.com`), [joining a captive network](https://support.apple.com/en-us/102554),
  [Platform Support: captive network issues](https://support.apple.com/guide/platform-support/supc4380e13e/web),
  [option 114 from iOS 14](https://developer.apple.com/news/?id=q78sq5rv),
  and an Apple engineer on the undocumented heuristic
  ([forum thread](https://developer.apple.com/forums/thread/660827))
- Android: [`NetworkStackUtils.java`](https://android.googlesource.com/platform/packages/modules/NetworkStack/+/refs/heads/main/src/com/android/networkstack/util/NetworkStackUtils.java)
  (the probe URLs, verbatim),
  [`NetworkMonitor.java`](https://android.googlesource.com/platform/packages/modules/NetworkStack/+/refs/heads/main/src/com/android/server/connectivity/NetworkMonitor.java)
  (parallel probes and the 204/200/3xx rule),
  [Custom Tabs and RFC 8908](https://source.android.com/docs/core/connect/android-custom-tabs-captive-portal),
  [DNS over TLS in Android P](https://android-developers.googleblog.com/2018/04/dns-over-tls-support-in-android-p.html)
- Microsoft: [NCSI frequently asked questions](https://learn.microsoft.com/en-us/windows-server/networking/ncsi/ncsi-frequently-asked-questions)
- Linux: [`NetworkManager.conf`](https://networkmanager.dev/docs/api/latest/NetworkManager.conf.html),
  [gnome-shell `portalHelper/main.js`](https://gitlab.gnome.org/GNOME/gnome-shell/-/blob/main/js/portalHelper/main.js)
- Firefox: [Trusted Recursive Resolver](https://wiki.mozilla.org/Trusted_Recursive_Resolver)
  (the portal host is excluded from DoH)
- [RFC 8910](https://www.rfc-editor.org/rfc/rfc8910.html) (the DHCP and RA
  options, and the admission that interception is still needed) and
  [RFC 8908](https://www.rfc-editor.org/rfc/rfc8908.html) (the API, and its
  mandatory HTTPS with validated certificate)
- [arduino-esp32 issue #10330](https://github.com/espressif/arduino-esp32/issues/10330)
  — ESP32 portals not popping on recent Android, secondary

**The browser terminal, with measured bytes:**

- [`@xterm/xterm` on npm](https://registry.npmjs.org/@xterm/xterm/latest),
  [jsDelivr file listing for 6.0.0](https://data.jsdelivr.com/v1/packages/npm/@xterm/xterm@6.0.0?structure=flat)
  and [for 5.5.0](https://data.jsdelivr.com/v1/packages/npm/@xterm/xterm@5.5.0?structure=flat),
  [releases API](https://api.github.com/repos/xtermjs/xterm.js/releases)
- `@xterm/addon-fit` and `@xterm/addon-attach` listings and bundle sizes
- **`ausil/esp32-web-terminal`** — the existence proof:
  [repo](https://github.com/ausil/esp32-web-terminal),
  [`frontend/lib` listing](https://api.github.com/repos/ausil/esp32-web-terminal/contents/frontend/lib)
  (the 120,632-byte `xterm.min.js.gz`),
  [`web_server.c`](https://raw.githubusercontent.com/ausil/esp32-web-terminal/main/main/web_server.c),
  [`sdkconfig.defaults`](https://raw.githubusercontent.com/ausil/esp32-web-terminal/main/sdkconfig.defaults),
  [`partitions.csv`](https://raw.githubusercontent.com/ausil/esp32-web-terminal/main/partitions.csv)
- **`zvldz/ESP32-UART-Bridge`** — the lazy-loading and build-time gzip:
  [repo](https://github.com/zvldz/ESP32-UART-Bridge),
  [`versions.json`](https://raw.githubusercontent.com/zvldz/ESP32-UART-Bridge/main/src/webui_src/lib/versions.json),
  [`embed_html.py`](https://raw.githubusercontent.com/zvldz/ESP32-UART-Bridge/main/scripts/embed_html.py)
- Alternatives: [`Gottox/terminal.js`](https://github.com/Gottox/terminal.js/),
  [`hterm-umdjs`](https://registry.npmjs.org/hterm-umdjs/latest),
  [`jcubic/jquery.terminal`](https://github.com/jcubic/jquery.terminal),
  [`coder/ghostty-web`](https://github.com/coder/ghostty-web), `ansi_up`
- [`@microsoft/dev-tunnels-ssh` algorithm list](https://raw.githubusercontent.com/microsoft/dev-tunnels-ssh/main/src/ts/ssh/algorithms/sshAlgorithms.ts)
  — no curve25519, no ed25519, **and no AES-128**
- [MDN `Content-Encoding`](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Content-Encoding)
  and [Mozilla Hacks on Brotli](https://hacks.mozilla.org/2015/11/better-than-gzip-compression-with-brotli/)
  (HTTPS only)

**Not verified for this report, and flagged as such in §2:** whether
ESP-NOW works between a SoftAP and a station associated to it; the gzip of
`xterm.css`; the flash and RAM cost of `CONFIG_HTTPD_WS_SUPPORT`;
`esp_http_server` throughput over a SoftAP; Apple's and Android's exact
detection rules as opposed to their observed behaviour; whether Windows
implements RFC 8910 at all; whether Android's captive-portal login bypasses
strict Private DNS; real phone power-save behaviour with a WebSocket open;
per-hop relay forwarding latency; what IDF 5.3.1's SoftAP does with idle
associations; KDE's portal behaviour.
