<!--
µnleashed BBS: internal/offgrid-research-2026-09-26.md

Research report: ways to reach a board with no internet (Wi-Fi mesh,
LoRa meshes, Reticulum, serial radios, ham packet), ranked, with a phased
plan. Working notes, not a spec. No code was changed.

Copyright 2026 - Robert Mech
License: GNU General Public License v3 or later
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Off-grid research: reaching a board with no internet

2026-09-26. Research only: nothing built, nothing flashed, no traffic to any
board. Sources are linked inline and listed at the end; anything not
verified against a primary source says so.

## Summary (for Discord)

- No action needed now. This is a direction, not a build.
- Best path 1, the neighbourhood network: OpenWrt routers meshing with
  802.11s + batman-adv (or LibreMesh), the board joining as an ordinary
  station, a Pi on it for local services. Every phone and laptop can call,
  and it needs zero BBS code. ESP32 repeaters work for a street, not a
  neighbourhood: single radio, about 8 clients each, PCB antennas.
- Best path 2, the LoRa meshes (Meshtastic, MeshCore): a gateway box on the
  link, and the board answering by DM with a terse "message terminal". Not
  telnet: about 200 bytes a message at about 1 kbps, shared by everyone.
- Best path 3, Reticulum/LXMF: the right transport for board-to-board
  linking and store-and-forward mail between towns. Later, and it depends on
  microReticulum and a C++ LXMF maturing.
- Two cheap core items that serve all of them and the legacy serial crowd:
  a UART caller line (a serial radio, a modem, a VT220 cable becomes a node)
  and a CALLER/MSG pair of link families, so gateways plug in horizontally.
- Licences: Meshtastic is GPLv3 (fits), MeshCore MIT, microReticulum
  Apache-2.0 (both fit). The Python Reticulum and LXMF carry the Reticulum
  License, with use restrictions that GPLv3 cannot absorb: keep them in a
  separate program, never linked into the firmware.
- Law: nothing on ham bands may be encrypted and callers must be licensed;
  EU 868 MHz LoRa is 1% or 10% duty cycle depending on sub-band.

---

## Contents

- [The shape of the problem](#the-shape-of-the-problem)
- [1. Reticulum and LXMF](#1-reticulum-and-lxmf)
- [2. Meshtastic (and MeshCore)](#2-meshtastic-and-meshcore)
- [3. Wi-Fi mesh and repeaters from ESP32s](#3-wi-fi-mesh-and-repeaters-from-esp32s)
- [4. OpenWrt, Raspberry Pi and community mesh](#4-openwrt-raspberry-pi-and-community-mesh)
- [5. Other internet-free transports](#5-other-internet-free-transports)
- [6. Comparison table](#6-comparison-table)
- [7. Ranked recommendation and phased plan](#7-ranked-recommendation-and-phased-plan)
- [What could not be verified](#what-could-not-be-verified)
- [Sources](#sources)

---

## The shape of the problem

Two different things hide under "reach the board without the internet", and
every option below is one or the other:

- **Stream transports** carry a terminal session: bytes both ways, low
  latency, enough bandwidth for a screen. Wi-Fi of any kind, HaLow, a
  900 MHz serial radio, a modem, a cable, BLE's UART service, AX.25
  connected mode at 1200 baud. On these the board is the board: detection,
  screens, forms, chat.
- **Message transports** carry short, slow, shared-airtime messages:
  Meshtastic (about 200 bytes of text a packet), MeshCore, LXMF over LoRa,
  APRS (67 characters). A full-screen BBS over these is unusable for the
  caller and antisocial to everyone else on the channel. The board has to
  answer as a message service: commands in, short answers out, mail and
  forum posts bridged.

The second class needs one new thing in the core, a **message terminal**
(below, in section 7), and after that every message transport is a
translation job done in a peripheral. That split is the main architectural
finding of this report.

Where the work sits, against the settled philosophy ("add hardware
horizontally", Rule no. 1):

- **On the network**: the board is a station on someone's Wi-Fi. No board
  code at all. Wi-Fi meshes, OpenWrt, HaLow backhaul, a Pi.
- **On the link** (LINK.md): a peripheral that speaks a radio the board
  does not, and hands the board callers or messages over ESP-NOW or the
  link's serial transport. The board never runs a LoRa stack, a protobuf
  decoder or Reticulum's crypto. Families today: 0 LINK, 1 CAMERA, 2 DOOR;
  3-127 are reserved for core families.
- **On a UART**: a device that already speaks a byte stream (serial radio,
  modem, terminal) becomes a caller line.
- **In the core**: only what every transport shares.

---

## 1. Reticulum and LXMF

### What it is

- [Reticulum](https://github.com/markqvist/Reticulum) (RNS) is a
  cryptographic networking stack: addresses are hashes of public keys,
  everything is encrypted end to end, routing is multi-hop over any mix of
  media. It claims to work over "practically any medium that can support at
  least a half-duplex channel with greater throughput than 5 bits per
  second, and an MTU of 500 bytes".
- Interfaces ([manual](https://reticulum.network/manual/interfaces.html)):
  LoRa through an RNode, TCP and UDP, serial, KISS TNCs, AX.25 over KISS
  (with callsign ID for ham use), I2P, pipes to external programs.
- [LXMF](https://github.com/markqvist/LXMF) is mail on top: signed
  messages, 111 bytes of overhead (16 destination, 16 source, 64 Ed25519
  signature, the rest msgpack), and **propagation nodes** that store and
  forward for recipients who are not reachable. That is FidoNet's idea done
  with modern crypto, and it is exactly what a BBS's mail wants.
- Clients: Sideband (Android, desktop), NomadNet (a terminal UI that also
  serves "pages" in its own micron markup, the closest thing on Reticulum to
  a BBS), MeshChat.

### Licences, and the catch

- The **Python reference implementation of Reticulum and LXMF** is under the
  **Reticulum License**: MIT plus two use restrictions, "shall not be used in
  any kind of system which includes amongst its functions the ability to
  purposefully do harm to human beings" and not used "directly or
  indirectly, in the creation of an artificial intelligence, machine
  learning or language model training dataset"
  ([LICENSE](https://raw.githubusercontent.com/markqvist/Reticulum/master/LICENSE),
  [LXMF LICENSE](https://raw.githubusercontent.com/markqvist/LXMF/master/LICENSE)).
  The protocol itself "was dedicated to the Public Domain in 2016"
  (README). GPLv3 forbids further restrictions, so a GPLv3 program that
  imports the Python RNS is on shaky ground. Not legal advice; Rob's call.
  The practical rule: **never link or import the Python RNS into anything
  GPLv3**. Run it as a separate program (rnsd) and talk to it over a
  socket, or use a clean-room implementation.
- [microReticulum](https://github.com/attermann/microReticulum) (C++,
  Chad Attermann) is **Apache-2.0** (GitHub API; last push 2026-07-20, 410
  stars). Apache-2.0 combines with GPLv3. Reticulum's README lists it as a
  recognised, wire-compatible implementation targeting 32-bit MCUs.
  - Implemented: identities, destinations, packets, AES/HKDF/HMAC/Fernet,
    transport with path finding, links, announces, proofs.
  - **In progress: Resources and Channels.** Resources are how anything
    bigger than one packet moves (a NomadNet page, a large LXMF message), so
    until they land a microReticulum node can do short LXMF messages and
    not much browsing.
  - ESP32 and nRF52 supported; configurable HEAP or PSRAM allocators. No
    RAM or flash figure published that I found.
  - Note: RTNode-2400's README calls microReticulum GPL-3.0; the repository
    says Apache-2.0. The repository wins; flagged anyway.
- [microReticulum_Firmware](https://github.com/attermann/microReticulum_Firmware)
  embeds it into RNode firmware as a standalone transport node; community
  forks such as [RTNode-2400](https://github.com/5ugAv/RTNode-2400) (GPLv3,
  Heltec V3/V4, XIAO S3, T3S3, LoRa to TCP bridging, 24-entry path table,
  all releases "Beta") show the pattern works on a $20 board.
- **C++ LXMF**: [reticulous/lxmf](https://github.com/reticulous/lxmf) is a
  from-scratch C++ LXMF pinned to LXMF 0.9.8, with ESP-IDF directories,
  claiming wire compatibility with Sideband, NomadNet and MeshChat. 0 stars,
  128 commits, licence not stated on the page I read. Early. Watch, do not
  depend.
- [RNode firmware](https://github.com/markqvist/RNode_Firmware) is
  GPLv3 (dual GPL + commercial); supports SX1276/78, SX1262/68 and SX1280
  boards (T-Beam, LoRa32, Heltec). An RNode is also a KISS modem.

### How a board would appear on Reticulum

- **LXMF mail bridge.** The board gets an LXMF identity; a caller's board
  mail to `name@lxmf` goes out as LXMF, LXMF to the board's address lands in
  a mailbox. Propagation nodes mean the recipient does not have to be on
  the air at the time. This is the most useful single thing.
- **NomadNet pages.** The board serves its information pages, forum
  indexes and the WHO list as micron pages. A NomadNet user browses the
  board in a terminal UI. Needs Resources (above).
- **Board linking.** Two boards anywhere Reticulum reaches (LoRa between
  towns, TCP over the internet, both) exchange chat lines and mail over a
  Reticulum link: encrypted, multi-hop, identity-addressed. This is the
  "federation" and DDial superchat item carried over something that already
  exists, and it matches the routing-middleware note's "one protocol rather
  than three": Reticulum could be the carrier for board-to-board traffic
  whether or not the middleware exists.
- **Not** a terminal session. Reticulum links can carry a stream and
  someone could build a telnet-over-RNS bridge, but over LoRa it would be
  the same airtime problem as Meshtastic.

### Where it would run

- **Peripheral, one chip:** a Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262,
  about $20) running `unleashed_rns`: microReticulum + a C++ LXMF + the
  link engine (`src/core/link.*` builds on a peer by design). It speaks
  LoRa (and optionally TCP to an rnsd on the LAN) on one side and the link
  on the other. All crypto, path tables and store-and-forward live there.
  Rule no. 1: the board sees link frames (78 us to open one on an S3,
  LINK.md) and message-sized events.
- **Peripheral, Pi:** rnsd + NomadNet + a small bridge program that talks
  to the board (over the link's serial transport, or telnet on the LAN).
  Mature today, because it uses the reference Python code, but it is a Pi
  and a separate licence (the bridge cannot sensibly be GPLv3 if it imports
  RNS; Rob holds the copyright and could license that one repo MIT, which
  is his call).
- **In the core: no.** Path tables, announces and ECDH per peer on a WROOM
  with 19 KB of static DRAM left is the wrong place, and it is the kind of
  work Rule no. 1 exists to keep off the loop.

Maturity verdict: the transport is excellent and the community is
enthusiastic, but the embedded side is beta. Phase 3.

---

## 2. Meshtastic (and MeshCore)

### Protocol facts

- **Managed flooding**, hop limit default 3, maximum 7 ("Really, 3 is
  fine"). Nodes listen before rebroadcasting; farther nodes (lower SNR) get
  shorter contention windows. Since 2.6, DMs learn a next-hop route after a
  first flood ([mesh algorithm](https://meshtastic.org/docs/overview/mesh-algo/),
  [LoRa config](https://meshtastic.org/docs/configuration/radio/lora/)).
- **Payload about 237 bytes** before protobuf overhead; roughly 200
  characters of text in practice.
- **Presets** from SHORT_TURBO to VERY_LONG_SLOW; **LONG_FAST is the
  default**. My arithmetic with Semtech's time-on-air formula, assuming
  LONG_FAST is SF11 / 250 kHz / CR 4/5 with a 16-symbol preamble (the
  preset table was not re-read): about 1.07 kbps raw, about **2.2 s on air
  for a full 255-byte packet**, about 1 s for 100 bytes. Every relay hop
  spends that again.
- **Encryption:** channels are AES256-CTR with a PSK; the default primary
  channel's key ("AQ==") is "a simple known key", so the default channel is
  effectively public. **DMs are public-key encrypted and signed from
  firmware 2.5.0** ([encryption](https://meshtastic.org/docs/overview/encryption/)).
- **MQTT:** "OK to MQTT" is off by default and is "not a cryptographic
  solution but a polite request": any internet-connected node on the mesh
  can uplink what it hears. Worth saying to sysops: a board on a public
  channel is heard on the internet whether they like it or not.
- **Ham mode** (`is_licensed`): callsign as the long name, PSK cleared,
  encryption off, because US amateur rules forbid it
  ([FAQ](https://meshtastic.org/docs/faq/)).

### Device API

- [Client API](https://meshtastic.org/docs/development/device/client-api/):
  serial/USB, BLE, or **TCP port 4403**. Stream framing is `0x94 0xC3`, a
  16-bit big-endian length, then a protobuf (`ToRadio` / `FromRadio`);
  length over 512 means corruption. A client sends `want_config`, reads
  `FromRadio` until `config_complete`, then exchanges packets.
- [Serial module](https://meshtastic.org/docs/configuration/module/serial/)
  modes include **TEXTMSG** ("any string sent to the serial port is
  broadcast as a text message on the default channel", received as
  `<ShortName>: <text>`) and **PROTO** (the client API on a UART, 38400
  default). TEXTMSG is trivially easy and **wrong for a BBS**: it can only
  broadcast on the default channel, so every reply would go to the whole
  mesh. PROTO is the one to use.
- Licences: [firmware](https://github.com/meshtastic/firmware) and
  [protobufs](https://github.com/meshtastic/protobufs) are **GPL-3.0**
  (GitHub API), which fits the firmware's GPL-3.0-or-later. Nothing to
  flag.

### Existing Meshtastic BBSes

- [TC²-BBS](https://github.com/TheCommsChannel/TC2-BBS-mesh) (GPLv3,
  Python, usually on a Pi): mail, bulletin boards, a channel directory,
  node stats, a fortune, and **sync between several BBS nodes** listed by
  node ID. Connects over serial or TCP. Users DM the node and pick menu
  letters. This is the proof that the message-terminal model is what people
  actually use.
- [meshing-around](https://github.com/SpudGunMan/meshing-around) (GPLv3,
  Python, 537 stars): a bot with BBS and mail, store-and-forward, games
  (DopeWars, Lemonade Stand, BlackJack, poker), weather and EAS alerts,
  WSJT-X/JS8Call forwarding. Serial, TCP or BLE.
- Both are Python on a separate computer. None I found runs on the radio's
  own MCU or on an ESP32 beside it.

### MeshCore, the alternative worth knowing

[MeshCore](https://github.com/meshcore-dev/MeshCore) is **MIT**, a C++
**library** for LoRa multi-hop routing (3,341 commits, 3.7k stars). Two
things make it interesting here:

- Its **room server** is described as "a simple BBS server for shared
  Posts". The LoRa community already expects a BBS-shaped thing on a
  MeshCore mesh.
- It is a library, not a monolithic firmware, and MIT. A single $20 LoRa
  board could run MeshCore **and** the link engine, where Meshtastic would
  need either a fork or two chips.
- Its companion radio API runs over BLE, USB or Wi-Fi. Repeaters relay;
  "companion" nodes do not, which avoids some of Meshtastic's flooding cost.
- Not verified: whether the official MeshCore phone apps are open source.

### How a board would appear on a Meshtastic mesh

- **A node you DM.** `?` returns a one-packet menu. `W` who is on the board
  (telnet and mesh callers alike), `M` mail list, `R 3` read message 3 in
  up-to-200-byte chunks, `S name text` send board mail, `F` forums with new
  counts, `P 2 text` post. Replies are DMs, never channel broadcasts.
- **Mail bridge.** A mesh user has a board mailbox keyed by their node ID
  (and later a claimed handle); board users can mail them and it arrives as
  a DM when they are next heard. Store-and-forward on the board, which is
  what TC²-BBS does.
- **Chat bridge, carefully.** Mirroring the whole room onto a channel would
  flatten a busy mesh. A private channel for the board's own community,
  rate-limited, off by default, is defensible; the public LongFast channel
  never.
- **Guest by default.** A node ID is not an account. Mesh users are guests
  with a node ID until they link an account (a one-time code shown to a
  logged-in telnet user and sent by DM), and nothing a mesh message says
  grants staff. The link's rule applies: "The link moves data. It never
  moves authority."

### Airtime and duty cycle

- **EU 868:** ERC Recommendation 70-03 Annex 1 gives 868.0-868.6 MHz at
  25 mW ERP and 1% duty cycle, and 869.4-869.65 MHz at 500 mW and 10%, both
  with LBT+AFA as the alternative access method
  ([CEPT ERC/REC 70-03](https://docdb.cept.org/download/4635)). Meshtastic
  enforces 10% on EU_433 and EU_868, rolling over an hour. 10% is 360 s an
  hour: about 160 full packets an hour from one node, and a relayed packet
  costs every relaying node its own share.
- **US 902-928 MHz:** [47 CFR 15.247](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/subject-group-ECFR2f2e5828339709e/section-15.247)
  sets hopping rules (0.4 s occupancy in 10 or 20 s) or a 500 kHz minimum
  6 dB bandwidth for digital modulation, 1 W conducted. No duty-cycle cap as
  such. Meshtastic's US presets use one fixed channel; how that maps onto
  15.247 (or another section) I did not establish, so no claim here.
- **The real limit is social, not legal.** A community mesh at LONG_FAST
  carries a few kilobits a second for everybody within range. A BBS answer
  of 1.5 KB is eight packets and about 17 s of air per hop. The message
  terminal has to be terse and rate-limited by design, not by courtesy.

### Where it would run

Three shapes, cheapest first:

- **Stock Meshtastic node on the board's UART, PROTO mode.** A Heltec V3
  or RAK4631 (about $20-45) on a cable. The board speaks the client API
  itself: nanopb plus the Meshtastic protobufs, as a plugin in its own
  repository built in through the external-plugin mechanism (LINK.md). Flash
  cost not measured; my estimate is tens of KB, which the S3's new 8 MB
  layout can take and the WROOM at 80.6% may not. Takes one UART, shared
  with the serial bridge and the link's serial transport (one owner).
- **Stock node + a $5 ESP32 gateway on the link.** The gateway speaks the
  client API to the node and a new MSG link family to the board. The board
  never sees a protobuf. Two cheap boxes, zero radio code in the core, and
  the same MSG family later serves MeshCore, LXMF and APRS. This is the
  horizontal answer.
- **One LoRa board running MeshCore + the link** (MeshCore only; the MIT
  library makes it possible). One box, $20.

Rule no. 1: all three keep the radio off the loop. The board handles one
message event at a time, and the message terminal must build its answer in
bounded slices (it already has paging and the runner for walks).

---

## 3. Wi-Fi mesh and repeaters from ESP32s

The pitch: ten $6 ESP32s on windowsills and phones anywhere nearby reach the
board. The board itself needs nothing: it joins the nearest repeater as a
station, the same as a router. What varies is the repeaters.

### ESP-WIFI-MESH (IDF built-in, ESP-MDF)

- A tree: one root, parents and children, each node STA + AP at once. Up to
  25 layers in a tree, about 1,000 nodes claimed and under 512 recommended,
  1,456-byte packets ([ESP-FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/application-solution/wifi-mesh-development-framework.html),
  [IDF 5.3.1 guide](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-guides/esp-wifi-mesh.html)).
- **Phones cannot join it.** Espressif: "In the scenario without a router
  Mesh, the softAP initiated by Mesh devices does not support the
  connection of devices other than Mesh devices ... it is recommended to use
  ESP-Mesh-Lite." It is a sensor mesh, and TCP/IP runs only on the root.
  Ruled out for callers.

### ESP-Mesh-Lite

- [esp-mesh-lite](https://github.com/espressif/esp-mesh-lite): SoftAP +
  station per node, every node gets an IP from its parent and runs lwIP, so
  nodes (and, per Espressif's recommendation, phones) are ordinary IP
  devices. 1-15 layers, 1-10 downstream connections per node, 8-12 ms per
  hop, root can run without a router
  ([user guide](https://github.com/espressif/esp-mesh-lite/blob/master/components/mesh_lite/User_Guide.md)).
  IDF 5.0-5.4, ESP32 and S3 among others.
- No throughput figures published. Licence not detected by the GitHub API
  and not found on the pages read: **unverified**. Last push 2025-09-30.
- Espressif recommends it over ESP-WIFI-MESH for new designs. It is the
  right base if the repeaters run our own firmware.

### esp32_nat_router (martin-ger)

- [esp32_nat_router](https://github.com/martin-ger/esp32_nat_router):
  an ESP32 as a NAT router, upstream Wi-Fi (or Ethernet or WireGuard), a
  SoftAP downstream, web config. "5 - 15 mbps under reasonable conditions",
  **up to 8 clients** (5 on a C3), about 5 KB RAM each. Active (pushed
  2026-09-11). Not a mesh: repeaters chain, each one NATing the next.
- **No licence** detected by the GitHub API or stated for the main project
  (only the WireGuard part is BSD-3). Running it on your own boxes is fine;
  shipping its binary on our site is not something to do without asking the
  author.

### painlessMesh

- [painlessMesh](https://github.com/gmag11/painlessMesh) (GPL-3.0,
  Arduino): not IP, JSON messages addressed by chip ID. Phones cannot use
  it. Ruled out for callers.

### ESP-NOW multi-hop

- ESP-NOW has no routing. Multi-hop means our own relaying, which the link
  deliberately does not do ("A peer never talks to another peer"). Phones
  cannot speak ESP-NOW. It is a backhaul between ESP32s, not an access
  network.
- **Long Range mode** reaches "one-kilometer line of sight", 2 to 2.5 times
  11b, at 512 or 256 kbps, ESP-to-ESP only, and can coexist with 11b/g/n on
  one interface ([IDF 5.3.1 Wi-Fi guide](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-guides/wifi.html)).
  LINK.md keeps LR off until measured, for good reason.

### Can ten ESP32 repeaters serve a neighbourhood?

For callers, the arithmetic is kind: a telnet session at a fast pace is a
few kbps, so even a chain of ten hops each at 1 Mbps effective is plenty.
What limits it:

- **One radio.** An ESP32 is a station and an AP on the same channel
  ("the home channel of AP and station must be the same"), so every hop in
  a chain shares one channel and each hop roughly halves what is left.
  Fine for telnet, poor for anything else a neighbour will try.
- **Clients per node:** 8 on esp32_nat_router, up to 15 Wi-Fi connections
  on the chip in total (shared with ESP-NOW encrypted peers,
  `ESP_WIFI_MAX_CONN_NUM`), 10 downstream in Mesh-Lite.
- **Range:** a PCB antenna indoors reaches a room or a house; outdoors,
  tens of metres. Not measured here. A `-32U` module with an external
  antenna in a window is the minimum for "across the street".
- **Phones need a terminal app.** Neither Android nor iOS ships telnet. A
  local web terminal (section 4) fixes that and matters more than any
  repeater.
- **Resilience:** a NAT chain has no healing; one repeater unplugged cuts
  everything behind it. Mesh-Lite heals (repair under 50 s per Espressif).

🔴 Ten ESP32s make a demo street, not a neighbourhood network: one radio,
8 clients and a PCB antenna each, and a chain that breaks at the first
unplugged node. A $25-40 dual-band OpenWrt router does the same job with a
mesh radio and an AP radio, and Freifunk runs 41,000 of them. Way through:
recommend OpenWrt for the network, and keep ESP32 repeaters as the cheap
gap-filler and the "it's all ESP32s" showpiece, in their own repo.

What the repeaters run: esp32_nat_router today (with the licence caveat),
or an `unleashed_repeater` on Mesh-Lite if we want our own, with the
board's name in the SSID and a local landing page. No change to the board.

---

## 4. OpenWrt, Raspberry Pi and community mesh

### The network

- **802.11s** on OpenWrt: "most wireless drivers used by OpenWrt support
  802.11s mesh", except Broadcom and most USB dongles; check with
  `iw list | grep "mesh point"`
  ([OpenWrt 802.11s](https://openwrt.org/docs/guide-user/network/wifi/mesh/80211s)).
  On a dual-band router, one band meshes and the other serves phones.
- **batman-adv** is a Linux kernel module that meshes at layer 2 and
  "emulates a virtual network switch of all nodes participating"; clients
  are bridged in through `bat0` ([kernel docs](https://docs.kernel.org/networking/batman-adv.html)).
  The whole neighbourhood becomes one LAN, so the board's mDNS name
  resolves everywhere on it.
- **Freifunk** runs it at scale: about 400 communities and over 41,000
  access points, with the [Gluon](https://github.com/freifunk-gluon/gluon)
  firmware framework (batman-adv IV fully, V partially).
- **LibreMesh** ([libremesh.org](https://libremesh.org/)): OpenWrt-based
  firmware framework, batman-adv plus BMX/Babel, AGPL, backed by Freifunk,
  Guifi and others. The closest thing to "flash these routers and you have a
  community network".
- **Althea** is a pay-per-forward ISP model with blockchain micropayments
  between routers ([how it works](https://docs.althea.net/pages/how-althea-works.html)).
  It is about selling internet access, not about local services. Not a fit.

### The Pi

A Raspberry Pi is a weak access point (one radio, modest client count) and
an excellent **local server**. Put it behind the OpenWrt mesh, not in front
of it. What it can host, none of which touches the board's firmware:

- **A local directory.** `unleashed_directory` is Python stdlib + SQLite in
  one file, so it runs on a Pi as is. Announce already takes a
  comma-separated server list over plain HTTP, so boards on the mesh add
  `http://directory.local/announce` and a neighbourhood gets its own board
  list with no internet. Better still, boards already advertise
  `_telnet._tcp` over mDNS (`main.cpp`), so a local directory can list
  boards it discovers without any announce at all. A small directory
  feature: "LAN mode", discovery by mDNS.
- **A web terminal.** The fTelnet problem in CLAUDE.md was mixed content:
  an `https://` page cannot open `ws://`. **On a local network with no
  internet the page is served over plain `http://` from the Pi, so `ws://`
  is allowed.** An xterm.js or fTelnet page plus a WebSocket-to-telnet
  proxy on the Pi gives every phone and laptop a terminal with no app. This
  is the single most useful thing for neighbours who have never heard of
  telnet.
- **A "local services" landing page.** Rob declined a captive portal for
  provisioning, and that decision stands. For a community AP it is a
  different question: a portal that says "this network has no internet;
  here is the board, the directory and the terminal". Phones open captive
  pages in a restricted mini-browser; whether a WebSocket terminal works
  inside iOS's captive network assistant I could not verify, so the portal
  should link out to the terminal in the full browser rather than host it.
  Rob's call whether a portal is wanted at all.
- **Gateways for the other transports:** rnsd and NomadNet (section 1), a
  Meshtastic bridge (section 2), Direwolf and LinBPQ (section 5).

### "An internet AP that bridges to the board"

Rob's idea is ordinary routing: a gateway with internet on one side and the
neighbourhood mesh on the other. Neighbours reach the board over the mesh;
the rest of the world reaches it through the gateway's port forward as
today. The board does not know or care which. The one thing to design:
the directory should show a board's LAN address to LAN visitors and its
public address to everyone else, which the "LAN mode" directory does
naturally.

### HaLow as backhaul

Covered in section 5; in short, a HaLowLink 1 at each end of a long gap
turns two OpenWrt islands into one network. No phone speaks HaLow, so it is
backhaul only.

---

## 5. Other internet-free transports

### AX.25 packet radio and APRS (ham only)

- Classic packet BBSes (FBB, LinBPQ) still run, at 1200 baud AFSK on VHF
  or 9600 on UHF. Direwolf is a software TNC with a KISS interface; LinBPQ
  hosts a node, a chat room and a BBS on Linux
  ([example config](https://github.com/N1OF/LinBPQ-Config)).
- **Law (US):** 97.113(a)(4) prohibits "messages encoded for the purpose of
  obscuring their meaning" ([eCFR](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-D/part-97/subpart-B/section-97.113)),
  so no encryption, and passwords cross the air in the clear (as on telnet,
  but now a licensed operator is responsible). 97.119 requires the station
  callsign at the end of each communication and at least every 10 minutes
  ([eCFR](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-D/part-97/subpart-B/section-97.119)).
  97.115 governs third-party traffic
  ([eCFR](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-D/part-97/subpart-B/section-97.115)).
  **Every caller must hold a licence**, and nothing commercial. Other
  countries differ in detail, not in spirit.
- For the board: a KISS TNC on a UART plus an AX.25 connected-mode layer
  would make a packet caller a node, and 1200 baud is a perfectly usable
  terminal speed for the retro crowd. Cheaper first step: LinBPQ on a Pi
  and a bridge into the board's telnet port. Whether LinBPQ can hand a
  connected packet user to an outside telnet host out of the box I did not
  verify.
- APRS messaging is 67 characters; a board could answer APRS messages
  through the same message terminal. Novelty.
- Verdict: loved by exactly the audience that calls this board, reaches
  only licensed hams. Low priority, high charm.

### Wi-Fi HaLow (802.11ah)

- Sub-GHz Wi-Fi (902-928 MHz in the US), up to 32.5 Mbps at 8 MHz.
  [HaLowLink 1](https://www.cnx-software.com/2025/02/17/halowlink-1-wi-fi-halow-gateway-turns-legacy-devices-into-wi-fi-halow-clients-via-ethernet-usb-or-2-4-ghz-wi-fi/)
  is an OpenWrt 23.05 router (MT7621, MM6108, 2.4 GHz AP for ordinary
  devices), $99 at Mouser. Marketing says 32 Mbps over 16 km (10 miles);
  treat that as a best case at 8 MHz line of sight, not a promise.
- ESP32 hosts: Morse Micro's ESP32 support moved to the
  `morsemicro/halow` component, which **requires ESP-IDF 5.4.2 or later
  and below 6.0** ([Beyondlogic](https://www.beyondlogic.org/evaluating-802-11ah-halow-using-the-esp32-s3-fgh100m-h/));
  the old [mm-iot-esp32](https://github.com/MorseMicro/mm-iot-esp32)
  (Apache-2.0 SDK, radio firmware as binary blobs) was archived July 2026.
  This project is pinned to 5.3.1, so **HaLow on the board itself waits for
  an IDF move** and would be a binary blob besides. Station mode is what
  the examples show; AP on an ESP32 not established.
- EU HaLow sits in 863-868 MHz SRD bands with their duty cycles; not
  researched further.
- Verdict: the best long backhaul between neighbourhood islands, as
  external OpenWrt boxes. Not a board feature.

### LoRa point-to-point

- Raw LoRa without a mesh protocol: fine for a fixed link between two
  sites (board-to-board linking between towns), too slow and too shared for
  a caller session. Reticulum over RNodes is the better way to do the same
  thing, with routing and crypto already solved. Same duty-cycle rules as
  section 2.

### 900 MHz serial radios (a "wireless serial cable")

- SiK telemetry radios: FHSS + TDM on 915 MHz, air rates up to 250 kbps,
  a transparent full-duplex serial link (57600 baud typical), better than
  300 m out of the box and several km with better antennas
  ([ArduPilot](https://ardupilot.org/copter/docs/common-sik-telemetry-radio.html)).
  Open firmware. Caveat from 3DR: kits "do not possess an FCC ID".
  Buy a certified variant (SparkFun, Holybro sell ones).
- This is the most "legacy serial community" option on the list: a VT220,
  a Model 100 or an 8086 box at a neighbour's house, a SiK radio on its
  serial port, the other radio on the board's UART. No protocol to write,
  no crypto, no app.
- What it needs from the board: a **UART caller line** (section 7). Today
  UART2 belongs to the serial bridge, which is the opposite direction
  (sysop reaches a device).

### Bluetooth

- Off on the board by decision (NimBLE is on the order of 100 KB of flash,
  CLAUDE.md). The S3 has only BLE; the classic ESP32 also has Classic SPP.
- BLE's de facto serial profile is the Nordic UART Service; ESP-IDF
  components exist ([esp-nimble-nordic-uart](https://github.com/masuidrive/esp-nimble-nordic-uart)).
  A phone with a NUS terminal app would be a caller at 10-50 m.
- Better as a peripheral: a $5 ESP32 running NUS on one side and a CALLER
  session on the link on the other. Keeps BLE's flash and RAM off the
  board. Low priority: the phone next to the board is usually on the same
  Wi-Fi anyway.

### Dial-up and modems

- Already on the roadmap as terminal mode. For answering: a modem on a
  UART in auto-answer is the same UART caller line as the serial radio,
  with `RING`/`CONNECT` handling. For off-grid it needs a phone line, and
  copper POTS is disappearing; a line simulator between two modems works
  for a club or a museum. Worth it for authenticity, not for reach.

### Others considered and dropped

- **GMRS/FRS data**: US-specific, tight limits on data; not pursued.
- **HF digital (JS8Call, VARA, Winlink)**: ham, and already served by
  existing software; meshing-around bridges JS8Call. Not the board's job.
- **Satellite**: out of scope by request.

---

## 6. Comparison table

Costs are rough 2026 retail in USD, not verified per item. "Effort" is ours.

| Option | Who can call, with what | Where it runs | Cost per node | Effort | Rule no. 1 | Horizontal fit |
|---|---|---|---|---|---|---|
| OpenWrt 802.11s + batman-adv / LibreMesh | anyone in range: phone, laptop, retro box with Wi-Fi modem | external routers; board is a station | $25-90 a router | docs only | none on the board | excellent |
| ESP32 repeaters (nat_router / Mesh-Lite) | phones, laptops, a street's worth | external ESP32s | $6-12 | docs, or a repeater repo | none on the board | good, limited scale |
| Pi local services (directory, web terminal) | anyone on the local net, no app | external Pi | $45-80 | small: directory LAN mode, a proxy | none | excellent |
| HaLow backhaul | joins two Wi-Fi islands | external HaLowLink boxes | $99 each end | docs | none | excellent |
| Meshtastic gateway | anyone with a Meshtastic node or app | stock node + ESP32 on the link, or node on UART | $20-50 | medium: MSG family, message terminal, gateway repo | peripheral does radio | good |
| MeshCore gateway | MeshCore users | one LoRa board on the link | $20 | medium | peripheral | very good |
| Reticulum / LXMF | Sideband, NomadNet, MeshChat users; other boards | LoRa board on the link, or Pi | $20-80 | large, depends on upstream | peripheral | very good |
| 900 MHz serial radio | any serial terminal, km range | radio on a UART | $40-110 a pair | small: UART caller line | UART ring, non-blocking | excellent |
| Modem, answer | anyone with a modem and a line | modem on a UART | $20-60 used | small after the UART line | same | good |
| AX.25 packet | licensed hams with a TNC | TNC on a UART, or LinBPQ on a Pi | $100-250 | medium to large | same | good |
| BLE NUS | phones within 10-50 m | ESP32 peripheral on the link | $5 | small to medium | peripheral | fine |
| Board SoftAP hotspot | phones within one board's range | on the board (S3 only) | $0 | small, but RAM, 44 KB flash, sockets | Wi-Fi task on core 0 | poor: it is the one thing a separate AP does better |

---

## 7. Ranked recommendation and phased plan

### The three core pieces everything shares

These are small, and each serves several transports. They are the only
things that need to be in the core.

- **UART caller line.** A UART configured as a caller port gets a Session
  like a socket does: detection (or a fixed terminal type from CONFIG),
  login, the lot, with the telnet layer off. It counts as a node but uses
  no lwIP socket, so the 16-socket ceiling does not bind it (the session
  pool of twelve still does). Serves serial radios, a direct VT220 cable,
  an answering modem (with a small `RING`/`ATA` state machine), and later a
  KISS TNC. Estimated small: the session already writes through
  `ByteSink` and the serial bridge already owns a UART; not measured.
  The legacy serial community is who this board is for, and today it
  cannot take a serial caller.
- **Link family 3, CALLER.** A peer brings a caller in: `OPEN` (transport
  name, remote identity string, terminal hint, columns), `DATA` both ways,
  `CLOSE`. The mirror of DOOR, on the same session machinery. Serves an
  ESP32 BLE gateway, a Wi-Fi gateway on the far side of an ESP-NOW hop, a
  LoRa board relaying a Reticulum link. Same security rule as the link:
  the peer vouches for nothing; the caller logs in like anyone.
- **Link family 4, MSG, and the message terminal.** A peer delivers a
  message from a remote identity (`meshtastic:!a1b2c3d4`,
  `lxmf:<hash>`, `meshcore:<key>`) with its maximum reply size; the board
  answers with one or more reply messages. Behind it, a **message
  terminal**: a terminal type with no screens, no menus, no colour, one-line
  commands, answers cut to the transport's size and paged by the caller
  (`N` for more), a per-identity rate limit, and a board-wide reply budget
  per hour so the board is never the loudest node on a mesh. It reuses
  mail, forums, WHO and INFO, not new stores. Estimated medium. Built once,
  it makes every message transport a gateway problem.

Numbering 3 and 4 is a proposal: LINK.md says a family is assigned by
adding a row to its table.

### Ranking

1. **The neighbourhood network (OpenWrt/LibreMesh + a Pi).** Largest
   reach, every device, no firmware, and it is what the question "no
   internet infrastructure" actually needs. Our work is a guide, a
   directory LAN mode and a web terminal proxy.
2. **Serial callers and the LoRa meshes.** The UART caller line first
   (smallest effort, serves our core audience), then the MSG family, the
   message terminal and a Meshtastic/MeshCore gateway. Meshtastic has the
   crowd (8,346 stars on the firmware); MeshCore has the easier licence and
   the library shape. Do Meshtastic first for reach, with the gateway
   written so MeshCore is a second front end, not a second design.
3. **Reticulum/LXMF for mail and board linking.** The right carrier for
   federation and the superchat between towns, and LXMF mail with
   propagation nodes is exactly board mail done properly. Start with a
   Pi-hosted bridge (mature, separate licence) and move to a one-chip
   `unleashed_rns` peripheral when microReticulum's Resources land and a C++
   LXMF has a clear licence.

Behind those: HaLow as documented backhaul, AX.25 as a UART-caller-line
extension or a LinBPQ bridge, BLE as a CALLER peripheral, a board SoftAP
on the S3 only if Rob wants it after the OpenWrt route exists.

### Phases

- **Off-grid 1 (no radio code on the board).**
  - A site page, "A board with no internet": OpenWrt or LibreMesh routers,
    the board as a station, the Pi's role, with ESP32 repeaters as the
    small-scale option and their limits stated plainly. `explain` writes it.
  - Directory "LAN mode": run on a Pi, discover boards by mDNS
    `_telnet._tcp`, accept local announces. Directory repo.
  - `unleashed_gateway` (Pi): a web terminal (xterm.js or fTelnet) and a
    WebSocket-to-telnet proxy, served over plain HTTP on the LAN, plus a
    one-command install of the local directory.
  - Core: the UART caller line, with CONFIG for port, pins, baud and a fixed
    or detected terminal type. Test on the host with a pty, the way
    `lrzsz_check.py` does; bench with a SiK pair.
  - Needs nothing new in the link.
- **Off-grid 2 (the LoRa meshes).**
  - Core: link families CALLER and MSG, the message terminal, mesh
    identities as guests with account linking by one-time code.
  - `unleashed_mesh`: the gateway firmware (ESP32 + stock Meshtastic node
    over PROTO, or MeshCore on one LoRa board) and its BBS-side plugin, in
    one repository, the same layout as `unleashed_camsat`.
  - A reply budget measured against EU 10% and against a busy US mesh
    before it ships.
- **Off-grid 3 (Reticulum and linking).**
  - `unleashed_rns`: a Pi bridge first; then a Heltec V3 peripheral with
    microReticulum + LXMF + the link engine.
  - Board-to-board linking over it, designed together with the routing
    middleware so it is one protocol.
- **Optional, any time:** HaLow backhaul docs; AX.25 over the UART line;
  a BLE CALLER peripheral; an `unleashed_repeater` on Mesh-Lite.

### Core versus peripheral repositories

| Piece | Home |
|---|---|
| UART caller line, CALLER and MSG families, message terminal, account linking for mesh identities | core |
| Meshtastic and MeshCore gateway firmware + BBS plugin | `unleashed_mesh` |
| Reticulum/LXMF bridge and peripheral | `unleashed_rns` (not `unleashed_lora`: LoRa is the radio, and three protocols run on it) |
| Web terminal, proxy, local-directory install for a Pi | `unleashed_gateway` |
| Directory LAN mode, mDNS discovery | `unleashed_directory` |
| ESP32 repeater firmware, if we make one | `unleashed_repeater` |

### Budget notes

- Flash: the WROOM image is 80.6% of its slot with the link in. The core
  pieces above are small; any on-board protobuf or Reticulum code is not,
  which is a further reason the radios belong on peripherals.
- Static DRAM: the core pieces should cost one CONFIG row set and a UART
  ring allocated at start (heap, like the serial bridge since 1.1.0), not
  per-session fields. Every byte in a Session still costs twelve.
- Sockets: UART and link callers use none, so off-grid callers do not
  compete with telnet for the 16.

---

## What could not be verified

- microReticulum's RAM and flash on an ESP32; the licence of
  reticulous/lxmf; RTNode's claim that microReticulum is GPL-3.0 (the
  repository says Apache-2.0).
- esp-mesh-lite's licence and throughput; esp32_nat_router's licence
  (none detected).
- Range of an ESP32 PCB antenna in a real neighbourhood: not measured.
- How Meshtastic's single-channel US presets sit under Part 15; the exact
  LONG_FAST parameters I used for airtime (SF11/250 kHz/CR 4/5, 16-symbol
  preamble) were recalled, not re-read.
- Whether MeshCore's phone apps are open source.
- Flash cost of nanopb + the Meshtastic protobufs on the ESP32.
- Whether a WebSocket terminal works inside iOS or Android captive-portal
  browsers.
- Whether LinBPQ can hand a packet caller to an external telnet host
  without extra software.
- HaLow's EU rules; AP mode on an ESP32 host; the HaLowLink range claim.
- All prices.
- The Reticulum License's compatibility with GPLv3 is my reading, not
  legal advice.

---

## Sources

Reticulum and LXMF
- https://github.com/markqvist/Reticulum
- https://raw.githubusercontent.com/markqvist/Reticulum/master/LICENSE
- https://reticulum.network/manual/interfaces.html
- https://github.com/markqvist/LXMF
- https://raw.githubusercontent.com/markqvist/LXMF/master/LICENSE
- https://github.com/attermann/microReticulum (and GitHub API for its licence)
- https://github.com/attermann/microReticulum_Firmware
- https://github.com/5ugAv/RTNode-2400
- https://github.com/reticulous/lxmf
- https://github.com/markqvist/RNode_Firmware

Meshtastic and MeshCore
- https://meshtastic.org/docs/overview/mesh-algo/
- https://meshtastic.org/docs/configuration/radio/lora/
- https://meshtastic.org/docs/development/device/client-api/
- https://meshtastic.org/docs/configuration/module/serial/
- https://meshtastic.org/docs/overview/encryption/
- https://meshtastic.org/docs/faq/
- https://github.com/meshtastic/firmware (GitHub API: GPL-3.0)
- https://github.com/meshtastic/protobufs
- https://github.com/TheCommsChannel/TC2-BBS-mesh
- https://github.com/SpudGunMan/meshing-around
- https://github.com/meshcore-dev/MeshCore

Wi-Fi mesh and ESP32
- https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-guides/esp-wifi-mesh.html
- https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-guides/wifi.html
- https://docs.espressif.com/projects/esp-faq/en/latest/application-solution/wifi-mesh-development-framework.html
- https://github.com/espressif/esp-mesh-lite
- https://github.com/espressif/esp-mesh-lite/blob/master/components/mesh_lite/User_Guide.md
- https://github.com/martin-ger/esp32_nat_router
- https://github.com/gmag11/painlessMesh
- https://github.com/masuidrive/esp-nimble-nordic-uart

OpenWrt and community mesh
- https://openwrt.org/docs/guide-user/network/wifi/mesh/80211s
- https://docs.kernel.org/networking/batman-adv.html
- https://github.com/freifunk-gluon/gluon
- https://en.wikipedia.org/wiki/Freifunk
- https://libremesh.org/
- https://docs.althea.net/pages/how-althea-works.html

Other transports and law
- https://www.ecfr.gov/current/title-47/chapter-I/subchapter-D/part-97/subpart-B/section-97.113
- https://www.ecfr.gov/current/title-47/chapter-I/subchapter-D/part-97/subpart-B/section-97.119
- https://www.ecfr.gov/current/title-47/chapter-I/subchapter-D/part-97/subpart-B/section-97.115
- https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/subject-group-ECFR2f2e5828339709e/section-15.247
- https://docdb.cept.org/download/4635 (ERC Recommendation 70-03)
- https://github.com/N1OF/LinBPQ-Config
- https://www.cnx-software.com/2025/02/17/halowlink-1-wi-fi-halow-gateway-turns-legacy-devices-into-wi-fi-halow-clients-via-ethernet-usb-or-2-4-ghz-wi-fi/
- https://www.beyondlogic.org/evaluating-802-11ah-halow-using-the-esp32-s3-fgh100m-h/
- https://github.com/MorseMicro/mm-iot-esp32
- https://ardupilot.org/copter/docs/common-sik-telemetry-radio.html

Project
- LINK.md (release-prep/wt-link), CLAUDE.md (doors, board linking, routing
  middleware, 1.2.0 link entries), sdkconfig.defaults (SoftAP off, about
  44 KB), src/main.cpp (mDNS `_telnet._tcp`).
