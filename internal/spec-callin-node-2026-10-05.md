<!--
µnleashed BBS: internal/spec-callin-node-2026-10-05.md

One ESP-NOW CALLIN node whose roles are switches: an access point with a
captive portal, a terminal server on its serial port, and a repeater, in
one device. The roles, the legal combinations, every use case, the
settings, the naming and what phase 1 is. Specification. No code.

Copyright 2026 - Robert Mech
License: GNU General Public License v3 or later
SPDX-License-Identifier: GPL-3.0-or-later

Working notes for µnleashed BBS. See the LICENSE file for terms.
-->

# One CALLIN node, three roles (2026-10-05)

Applies to versions: firmware 1.2.1 (the tree as it stands), specifying
1.2.3's caller-line core and CALLIN, and the node firmware that is new.
Nothing here is built. Supersedes the two-box framing in
`internal/study-ap-gateway-sat-2026-10-05.md` §15 phase 1; every figure
in that study and in `internal/study-broker-sat-2026-10-05.md` still
stands and is cited rather than repeated.

**Rob corrected the design, 2026-10-05, and his words are the
specification:**

> I think youre confused, here are my thoughts. ESPNOW a callin node which
> acts as an AP, Repeater and terminal server. Option for AP on (Terminal
> server rx/tx serial still works), option for repeater. Part of the ESPNOW
> network, allowing the terminal server to connect to the BBS via the sat
> interface or the repeater (repeating another distance node/terminal sat)
> over the ESPNOW network. This allows the same device to have different
> purposes. Even for ham radio it could connect to this and then use the
> functions on the device(most likely repeater) in the use case. Make sure
> oyu have all the use cases spelled out.

And, the same day: **"repeater to repetaer of the callin sat should
apply"**, so multi-hop is in scope.

- [0. The model, confirmed and corrected](#0-the-model-confirmed-and-corrected)
- [1. The roles, as switches](#1-the-roles-as-switches)
- [2. The use-case matrix](#2-the-use-case-matrix)
- [3. The board list: what a caller picks from](#3-the-board-list-what-a-caller-picks-from)
- [4. Settings](#4-settings)
- [5. Naming, settled](#5-naming-settled)
- [6. New work against what is planned, and what phase 1 now is](#6-new-work-against-what-is-planned-and-what-phase-1-now-is)
- [7. Repeater to repeater](#7-repeater-to-repeater)
- [8. The honest limits](#8-the-honest-limits)
- [9. Out of scope: an ESP32 running a directory](#9-out-of-scope-an-esp32-running-a-directory)
- [10. What Rob has settled, and what is still open](#10-what-rob-has-settled-and-what-is-still-open)

---

## 0. The model, confirmed and corrected

**Rob's model as read back to him, and it is right:** one device, one
firmware, three independent switches that compose rather than exclude; the
access point being on does not stop the serial port working; the node's way
to a board is either its own ESP-NOW pairing or another node repeating for
it; and the ham dock is a client of the node rather than a fourth kind of
box.

**Three corrections, none of them to the model, all to my own earlier
framing of it:**

1. **There are three roles and a fourth thing, and the fourth is not a
   role.** How the node reaches a board is a separate choice from what the
   node offers the outside, and it belongs to each board in the node's list
   rather than to the node:
   - **over the link**, an ESP-NOW pairing, direct or through a repeater.
     This is Rob's "via the sat interface or the repeater" and it is the
     real product.
   - **over IP**, the node opening telnet to a board's address because both
     are on the same Wi-Fi: the house router, or the node's own access point
     with the board joined to it as a station.

   Keeping these out of the role list matters because a node can hold one
   board each way at once (five slots, `Engine::kHosts = 5`,
   `src/core/link.h:295`), and because the IP way needs no pairing, no
   CALLIN and no core change at all, which is what makes phase 1 a week
   (§6).

2. **The roles are runtime settings, never build flags.** Three build
   variants would be the two-box design Rob has just rejected, wearing one
   repository. Flash is what pays for that and it can: camsat's shipped
   `firmware.bin` is 789,072 bytes against a 4,128,768-byte `factory`
   partition, so there are **3,339,696 bytes free on a 4 MB part** and the
   whole web payload is 86 to 135 KB gzipped (study §6.1). Everything is
   compiled in on every node; a switch costs heap and tasks, not an image.

3. **"Node" is the one word in Rob's sentence that cannot be kept.** In this
   project a node is a caller line: `BBS_MAX_NODES`, `nodeName`,
   `nodeLabel`, the `NODES` command, "Connected to node 1 of 10". A box
   called a node beside a command that lists lines is the second meaning of
   a word, which is the mistake CLAUDE.md records "bulletin" costing several
   sittings. §5 settles the words; the device is a **gateway sat**, it
   speaks the **CALLIN** family, and "callin node" survives as the
   repository's name the way "camsat" does.

**Where I had it wrong before Rob's correction, recorded so the next reader
does not re-derive it:** the AP study wrote the portal box and the serial
sat as two devices with two names and two repositories, and priced the
relay as a third. They are one device. The measured consequences of that
are small and all in the same direction: one firmware, one settings shape,
one caller-stream abstraction with three sources, one repository, and the
terminal server effectively free once the access point exists, because a
UART is just a second source feeding the same uplink.

---

## 1. The roles, as switches

### 1.1 What each one is, and what it costs off

The node is always a link peer: the engine, the crypto and the pairing are
its reason to exist and are not switchable. On top of that:

| Role | Key | What it does | Off: compiled out? | Off: what comes back |
|---|---|---|---|---|
| **Access point** | `ap` | SoftAP, DHCP server, a DNS responder that answers every A query with the node's own address, `esp_http_server` with `CONFIG_HTTPD_WS_SUPPORT`, the portal page, the board list, and xterm.js over a WebSocket. Each browser terminal is one caller | **No.** Always in the image | the AP netif and DHCP server, the httpd task's stack (4,096 default, 10,240 in the one real project, from the heap), the DNS task, and every socket the server held: 3 internal plus one a client (`esp_http_server.h:187`). **Tens of KB of heap; measure in phase 2** |
| **Terminal server** | `termsrv` | A UART with configurable pins, baud and format; the bytes are one caller. Optional hardware button, optional carrier-detect or DTR hang-up | **No** | the UART driver's ring buffers (a few KB), its reader task, and the pins go back to being a sysop's |
| **Repeater** | `repeat` | Forwards sealed frames it cannot open between downstream peers and the board or the next hop upstream, from its receive callback on its own task. Carries any family, so it repeats a camera sat as readily as a caller | **No** | the forwarding task, the downstream MAC and path table (about 7 bytes a peer, `sat-types` §1, so roughly 60 bytes), and a branch in the receive path. **Effectively free either way** |

**Flash is paid once, by every node, whatever its switches.** That is the
price of one device and it is about 100 to 160 KB of a 4 MB part's 3.3 MB
free: the web assets, the HTTP and DNS servers, the UART driver and the
forwarding path. State it on the node's own status so nobody hunts for it:
3 to 5% of the free flash buys the fact that any node can become any of
these with a setting.

**Internal RAM on a PSRAM-less part is what will actually bind, not
flash.** A SoftAP with fifteen stations, lwIP with sixteen sockets, an HTTP
server with its own task, a handful of open WebSockets and the link
engine's 15 to 19 KB of tables is a great deal of heap for a WROOM's
~180 KB of usable DRAM. The existence proof in the study is on an
ESP32-C3, which has a different memory map and one core, so it proves the
software path and not the WROOM's budget. **Do not promise a bare WROOM
with all three roles on until phase 2 reads the heap**; an ESP32-WROVER,
same price class, is the hedge.

### 1.2 The radio is the constraint, and the rule is one sentence

One radio, one channel, for the access point and for ESP-NOW both. ESP-NOW
peers are registered per interface with `channel = 0` meaning "whatever
channel station or softap is on" (`esp_now.h:66-71`), so the question for
every combination is only ever **who sets the channel**.

| The node is | Channel set by | ESP-NOW on | Note |
|---|---|---|---|
| AP only | **the node**, freely | `WIFI_IF_AP` | The fairground case. The node is the channel authority for its phones, and must be set to its boards' channel |
| STA only | **the router it joined** | `WIFI_IF_STA` | camsat today. No phones |
| AP + STA | **the station**, i.e. the router | either | The AP is dragged onto the router's channel and the phones go with it, announced by a Channel Switch Announcement (`wifi_ap_config_t.csa_count`, default 3). Good behaviour, already handled |

**The design rule that falls out, and it holds in every legal
arrangement: the board's channel is the authority, and an access point must
be set to it.** The board is a station on whatever network it joined and
"must be the same as that of the connected AP" (LINK.md, Channels); its
`BEACON` is the only thing a peer trusts about the channel, and every frame
from it carries the channel in its `chan` byte. So:

- **board on a router**: the router sets the channel, the node's AP must be
  set to it. A node's AP channel that differs is the mixed case and is
  refused (§1.3).
- **board joined to a node's own AP**: the node sets the channel, the board
  is on it by construction, the board's `BEACON` then declares that same
  number, and every other node follows. Consistent, and the mixed case
  cannot arise. **This is the recommended shape wherever there is no
  router.**
- **board with no network at all**: broken today, and it is a board-side
  blocker rather than a node one. `src/core/link.cpp:1104` is
  `if (!io_.associated()) return;` so an unassociated board answers no
  `DISCOVER`, no `HELLO` and no `PAIR_HELLO`; and `src/main.cpp:1135-1150`
  blocks before `Bbs::begin`, so a board with no network never starts the
  BBS at all, which is the trap that bit Agentville, the 4.3B and the WS2 on
  2026-10-01. **Having the board join a node's AP is what avoids this
  entirely, and it is why that is the recommendation rather than teaching
  the board to run with no network** (study §4.2, which prices the
  alternative at two to four days plus bench).

### 1.3 Legal and illegal combinations

Eight combinations of three switches. Every one of them is legal as a
combination; what is refused is a **setting** inside one, and the refusals
are few and each has one reason.

| AP | Term | Rep | Legal | Notes |
|---|---|---|---|---|
| off | off | off | **Legal, and says so** | A node being configured. The console and the status say "no roles on" rather than looking broken. Do not refuse it: a node is in this state between flashing and setting up |
| **on** | off | off | Legal | The fairground AP. Rows 3, 8, 11 |
| off | **on** | off | Legal | The serial terminal sat 1.2.3 already planned. Rows 1, 2, 7 |
| off | off | **on** | Legal | A repeater on a pole. Row 5 |
| **on** | **on** | off | Legal, and Rob names it | "Option for AP on (Terminal server rx/tx serial still works)". Two callers, two board lines, one pairing. Row 4 |
| **on** | off | **on** | Legal | The Zigbee router shape with phones. Rows 6, 9, 10 |
| off | **on** | **on** | Legal | A far terminal that also repeats for one further out |
| **on** | **on** | **on** | Legal | Everything at once. The one to watch on a single-core part (§1.4) |

**What is refused, and why:**

1. **An access-point channel that differs from the boards' channel.**
   Refused outright, because one radio cannot serve two channels: this is
   the single hard impossibility in the design and it is physics plus one
   radio, not a software limit. Mechanically: `ap_chan` is blank by default
   and blank means "follow the board", taken from the `BEACON`. A number is
   accepted only while no board is paired over the link, and the refusal
   names the channel the boards are on, the way `satwords.h`'s shared-
   satellite message already does from the board's side.
2. **The repeater role with no link uplink.** A repeater forwards ESP-NOW
   frames to a board or to an upstream node over ESP-NOW; a node whose only
   boards are IP addresses has nowhere to forward to. Refused with
   "Repeating needs a board or an upstream node paired over the link."
3. **A repeater bridging to IP.** Not a refusal, an impossibility worth
   writing down because somebody will ask for it: a repeater has no key for
   what it carries (the ECDH secret and every session key are the far peer's
   and the board's alone, `sat-types` §1), so it **cannot** open a forwarded
   frame and re-originate it as telnet. Only a node that terminates a
   session can do that, and a node that terminates a session is carrying its
   own caller, not repeating. §7 makes the consequence explicit.
4. **The terminal server on the node's console pins.** Same shape as the
   board's own `BBS_CONSOLE_UART0` rule: refused by name, per node profile,
   so a sysop is told on the form rather than after a reload. On a node
   profile with no free UART pins at all (CLAUDE.md records the ESP32-CAM as
   a candidate) the rows are shown greyed with the reason, the
   `PS_INFO` plus `FF_READONLY` pattern the S3-only rows already use.
5. **Two nodes repeating for each other.** A loop, and with `sat-types`'
   two-byte wrapper there is nothing to stop it. §7 adds the hop count that
   makes a mis-planted chain fail safely instead of filling the channel.

**And one combination that is legal, pointless and should not be refused:**
a node that joins a router as a station *and* runs an access point. Both
land on the router's channel, the phones follow, everything works, and the
board could simply have joined the router itself. Legal; say nothing.

### 1.4 The one thing to measure about combinations

Rule no. 1 is the board's rule, and a node is not the board. But a node
with all three roles on is a forwarding task, an httpd task and a UART
reader task beside the link engine, and a caller judges a terminal on
keystroke echo. On a two-core ESP32 that is comfortable by inspection; on a
one-core part (the C3 the study's existence proof runs on) it is a question.
**Phase 2 measures echo with all three on, on the part Rob means to plant**,
and if it does not hold the answer is a note on the node's page rather than
a refusal, because the sysop planting the box knows what it is for.

---

## 2. The use-case matrix

This is the deliverable. Switch columns are the three roles; **Uplink** is
how the node reaches the board (§0.1); **Phase** is when it exists (§6).

| # | Deployment | AP | Term | Rep | Uplink | Caller's equipment | How it reaches a board | What the caller sees | Phase |
|---|---|---|---|---|---|---|---|---|---|
| **1** | **A terminal on the desk beside the board.** A VT220, or a PC's DB9 through a null modem | off | **on** | off | link, direct | the terminal itself, no software | one pairing, one hop, sealed | Power on, the board's welcome, a login. No picking: `ser_board` names its one board | 3 |
| **2** | **The same terminal in another room or another building.** The node is beside the terminal, out of the board's range | off | **on** | off | link, **through a repeater** | the terminal | its own pairing with the board, carried by a repeater that cannot read it | Identical to row 1. A caller cannot tell | 6 |
| **3** | **A fairground AP with phones.** A box on a post, open network, a sticker with the name and the address | **on** | off | off | link, direct | any phone, no app | one pairing; every phone multiplexes over it | Joins an open network, the portal pops, a board list, taps one, a terminal. The open-AP line on the page and the board's own connection line | 1 (IP) / 4 (link) |
| **4** | **One box, a phone and a terminal at once.** Rob's own example | **on** | **on** | off | link, direct | a phone, and a terminal on the wire | both callers over one pairing, two board lines | The phone caller sees row 3; the terminal caller sees row 1. Neither knows about the other | 3 |
| **5** | **A repeater on a pole**, carrying nothing of its own | off | off | **on** | link, direct (for its own status only) | none | it forwards other peers' sealed frames | Nothing. A caller never meets it; `LINK` shows "via mast-1" | 6 |
| **6** | **A repeater that also carries its own caller.** The Zigbee router shape Rob asked for | **on** | off | **on** | link, direct | a phone at the repeater | its own caller terminates here and is its own session; forwarded frames stay opaque | Row 3's experience, from a box that is also somebody else's path. The two paths cannot be confused because one has a key and the other does not | 6 |
| **7** | **The ham dock reaching a distant board.** The Pi's docking radio is a client of the node, and "most likely repeater" is Rob's own reading | off | **on** | **on** | link, direct or repeated | a radio caller, through Direwolf and linbpq on the Pi, into the docking radio's serial | the dock's serial is a terminal-server line; the node repeats for the dock if the dock cannot hear the board | RF mode: ASCII, no transfers, short pages, the callsign challenge. Unchanged by the node being in the path | 1.3.0 |
| **8** | **A board with no router at all.** The node is the board's access point as well as its front door | **on** | off or on | off | **IP**, telnet to the board on the node's own AP | phones, and a terminal if `termsrv` is on | no pairing, no CALLIN: a socket on the board | Row 3's experience. The board is a full board: `Bbs::begin` ran, plugins started, the panel lit | **1** |
| **9** | **A root node and leaves at a fairground with no router.** The root is the board's AP and repeats; each leaf is an AP in its own corner | **on** | off | **on** (root) | leaves: link through the root. Root: IP or link | phones anywhere on the site | leaves pair with the board end to end, forwarded by the root; the board is the root's station so the channel is settled by construction | Row 3 from any corner. A caller walking between leaves is dropped and logs in again (§8) | 6 |
| **10** | **A two-hop chain to a far corner**, Rob's repeater-to-repeater | **on** (far end) | off | **on** (both hops) | link, two hops | phones at the far end | far node → repeater → repeater → board, sealed throughout | Row 3, with about 4 to 12 ms more round trip if both hops forward from their receive callback, and about 80 ms more if either polls on a tick (§7) | 7 |
| **11** | **One node, several boards.** Rob's "the BBS **systems** on the ESPNOW network" | **on** | off or on | off | link, up to five pairings | phones, or a terminal | up to five pairings, each with its own key | A board list, name and a word of description a row, and whether a line is free. A busy board costs one tap, not a busy signal | 4 |
| **12** | **A node at home on the house router.** One board, no pairing, a sysop who wants a phone to reach their own board | **on** | off | off | **IP**, over the router | a phone on the house Wi-Fi | telnet to the board's address; the AP lands on the router's channel | The portal, one board, a terminal. Phones on the house network can use it too | **1** |
| **13** | **A repeater for a camera sat in a barn.** The case `sat-types` was written for, served by the same device | off | off | **on** | link, direct | none | forwards family 1 as readily as family 3, because it never parses the header | Nothing: a photo arrives in Photos. Worth naming because it means one firmware covers the camera case too | 6 |
| **14** | **A modem answering a phone line**, on the terminal server's UART | off | **on** | off | link, direct | whatever dialled in | the modem's bytes are one caller | The board, at whatever speed the modem negotiated. 1.2.3 already lists Hayes answering as a later option; nothing in the node design is in its way | later |
| **15** | **The sysop's own console**, a terminal on a node, landing on the hidden sysop node | off | **on** | off | link, direct | a terminal the sysop is standing at | the terminal-server line, with the board's own per-pairing `console` flag set | The sysop node, not a caller line. **The board decides this, never the node** (§4.3) | 3 |

### 2.1 Which of these Rob actually wants first

**Row 8, then row 3, then rows 1 and 4.** The reasoning:

- **Row 8 is the whole product in a week and needs no core change at all**
  (§6). It is also the row that proves the things research cannot settle:
  that a phone pops the portal, that xterm.js satisfies the board's own
  terminal detector, that CP437 art draws, and that keystroke echo is
  bearable. It is allowed to kill the plan for a week's work.
- **Row 3 is row 8 with the link under it instead of telnet**, which is
  phases 3 and 4, and it is what makes a node independent of the board's
  Wi-Fi range.
- **Rows 1 and 4 come nearly free once row 3 exists**, because a UART is a
  second source into the same uplink. That is the argument for building the
  terminal server in phase 1 rather than later (§6.2).
- **Rows 5, 6, 9, 10, 13 all wait on the repeater**, which wants three boxes
  and open ground and should not be designed from a desk.

Row 7 is 1.3.0's and is listed to prove the node does not need changing for
it: a docking radio on a node's serial port is row 1 with a Pi where the
VT220 was.

---

## 3. The board list: what a caller picks from

Rob: the portal "then shows the BBS systems on the ESPNOW network".

### 3.1 How a node learns which boards are reachable

**One uniform record a board, with its source as a field.** Five slots
(`kHosts = 5`), each:

| Field | From |
|---|---|
| name, 16 characters | `PAIR_OFFER` and `BEACON` for a paired board; typed in for an IP board |
| how reached | `link` (a pairing) or `ip` (an address) |
| up or down | the link's own state for a pairing; **unknown until a caller taps** for an IP board (§3.2) |
| last heard | the link's own figure |
| lines free, total | absent until the nudge of §3.3 exists |
| source | `paired`, `typed`. One field, and §9 is why it exists |

Nothing new is needed for a paired board: the node already knows whether
its link is up, the board's name and when it was last heard. **The board
list is the pairings list filtered to the ones whose session is up, plus
the sysop's typed-in addresses.**

### 3.2 How stale it can be

- **A paired board: accurate to 5 to 15 seconds.** A peer sends `PING`
  every 5 s (`kPingMs = 5000`), three misses trigger a rescan
  (`kPingMisses = 3`), and a host marks a peer down after 20 s of quiet
  (`kHostQuietMs = 20000`). That is plenty for a page somebody looks at for
  a few seconds.
- **Serve what the link already knows. Never poll on page load**, so the
  page costs nothing and a phone that reloads twice does not move any
  traffic on the air.
- **An IP board is not probed at all, and this is a decision rather than
  laziness.** A periodic TCP connect-and-close would appear in the board's
  caller log as a caller who never logged in, once every interval, for
  ever. The caller log is the sysop's security record and polluting it with
  the node's own heartbeat is worse than a board shown as available that
  turns out not to be. So an IP board is listed, and a caller finds out one
  tap later.

### 3.3 Whether a board has a line free

New information the link does not carry today. Three options, from the
study's §11, and the recommendation is unchanged:

- **Phase 4: ask when the caller taps.** No new message at all. A refused
  attempt makes the node say which other boards are free. The board's own
  busy experience already exists (a busy screen, a ten-second countdown, a
  drop) and a gateway caller meeting it is not a failure.
- **Phase 5: the board nudges.** A small CALLIN message carrying free lines
  and total, sent when the count changes, coalesced about 2 s the way
  announce's join and leave nudge is. Specify the field as optional from the
  start, so a node talking to an older board simply shows nothing rather
  than showing nonsense.
- **Never a queue.** A queue on a ten-line board is a way of making somebody
  watch nothing happen, which is this project's own recorded position on
  exactly that question.

**What a caller sees when every line on every board is busy:** the portal
says so, says how many boards it tried, and offers the plain address so
somebody with a real client on a laptop can try again. Not a spinner, not a
retry loop.

### 3.4 What stops a node advertising a board that never agreed

**Pairing is necessary and is not sufficient, and the second half is the
part worth building.**

- **Necessary, and it is a complete answer to the alarming-sounding
  version.** A node cannot list a board it has no key for: it would be
  answered by nothing. Pairing is two physical acts, one at each end,
  inside a short window, with a four-digit code the two sysops compare, and
  a relay that swapped public keys makes the two codes differ. So "an open
  AP advertising other people's BBSes" is not a risk to mitigate, it is
  impossible.
- **Not sufficient**, because a sysop who paired a camera sat has not
  thereby agreed to take anonymous callers off an open access point in a car
  park. Pairing consents to a device, not to a purpose.
- **So two switches on the board, and they are the sufficient part:**
  - the CALLIN plugin ships **off**, in the spirit of "a board is closed
    until its sysop opens it";
  - and **per pairing**, a "Take callers" flag, so one node may carry
    callers and another may not. It costs a flag in the pairings file beside
    the `recv` and `camno` that are already there, and it is also the
    sysop's lever against a troublemaker on one node (§8).
- A board that has not opted in refuses the node's CALLIN attempt, and the
  node shows that board as "not taking gateway callers" rather than as down,
  because those are different facts and a sysop debugging wants to know
  which.

---

## 4. Settings

In this project's own shape: `PluginSetting` rows, a 9-character `label`
for 40 columns and a longer `wide` for 80, a `note` of up to 38 characters
and a `wideNote` of up to 78, `warnAbove`/`warn`/`warnShort` where a value
can do harm, and `PS_INFO` plus `FF_READONLY` for a row a given board or
node cannot have, with the reason in the note. A page holds
`Form::kMaxFields` 16 rows of which `kCoreRows` 4 are CONFIG's own, so
**twelve rows a page**, which is why the node's settings are a `PS_PAGE`
per role rather than one long page.

### 4.1 Where node settings live, and why both ways

**On the node, in NVS, and editable from two places.** Not only from the
board, because of a chicken and egg that the camsat pattern never had: in
row 8 the node is the board's access point, so the node must be fully
configured before any board can reach it to configure it.

- **The node's own portal, behind a sysop password**, is how a node is set
  up from nothing. It is free: the access point and the HTTP server are the
  node's reason to exist, and the page is the same page machinery as the
  board list.
- **`CONFIG sat <name>` on the board** once paired, which is the settled
  pattern ("CONFIG sat `<name>`: one device, including a camera sat's own
  camera settings") and what a sysop will reach for.
- A node whose `ap` switch is off has no portal, so its settings are
  reachable only from a board or over USB. Say that on the AP row's note,
  because switching the access point off through the portal is a way to lock
  yourself out of your own box.

### 4.2 The node's pages

**Roles** (the node's main page):

| Key | Label (40) | Wide (80) | Kind | Range | Default | When |
|---|---|---|---|---|---|---|
| `ap` | `AP` | `Access point for phones` | PS_YESNO | — | **no** | next restart |
| `termsrv` | `Terminal` | `Terminal server on the serial port` | PS_YESNO | — | **no** | next restart |
| `repeat` | `Repeater` | `Forward other nodes' traffic` | PS_YESNO | — | **no** | live |
| `boards` | `Boards` | `The boards this node offers` | PS_GROW | 5 slots | — | live |
| `ap_` | `AP setup` | `Access point settings` | PS_PAGE | — | — | — |
| `ser_` | `Serial` | `Terminal server settings` | PS_PAGE | — | — | — |
| `rep_` | `Repeat` | `Repeater settings` | PS_PAGE | — | — | — |

`ap` and `termsrv` are next-restart because bringing a SoftAP or a UART up
reconfigures the radio or claims pins, and the node has no callers to
protect at boot. `repeat` is live because it is a task and a branch.

**AP page** (`ap_`):

| Key | Label | Wide | Kind | Range | Default | When |
|---|---|---|---|---|---|---|
| `ap_ssid` | `Network` | `The Wi-Fi name phones see` | PS_TEXT | 32 | the node's name | next restart |
| `ap_addr` | `Address` | `The address phones reach this node on` | PS_TEXT | 15 | `172.16.0.1` | next restart |
| `ap_chan` | `Channel` | `Wi-Fi channel; blank follows the board` | PS_OPTNUM | 1..13 | **blank** | next restart |
| `ap_max` | `Phones` | `Most phones on this network at once` | PS_NUM | 1..15 | 15 | next restart |
| `ap_beacon` | `Beacon` | `Beacon interval in TU; higher saves airtime` | PS_NUM | 100..1000 | 100 | next restart |
| `ap_dtim` | `DTIM` | `1 keeps a sleeping phone's keystrokes quick` | PS_NUM | 1..3 | 1 | next restart |
| `ap_sysop` | `Password` | `Password for this node's own setup page` | PS_TEXT | 32 | — | live |

- **`ap_addr` defaults to `172.16.0.1`, not `192.168.4.1`.** Four or more
  years of reports against `arduino-esp32` say ESP32 portals fail to pop on
  some recent Android and Samsung devices and that moving off the IDF's
  default address is one of the two things people report fixing it (study
  §5). Secondary and unexplained, free to choose, and phase 2 tests both.
- **`ap_chan` blank means "follow the board"** and a number is refused while
  a board is paired over the link (§1.3). The refusal names the boards'
  channel.
- **`ap_beacon` carries a `warnAbove` of 300** with `warn` "Phones take
  longer to find this network." and `warnShort` "Slower to find." Raising it
  is the first mitigation for a field full of boxes (§8) and it has a real
  cost to a caller standing in front of a sign.
- **There is no `ap_staff` row.** Staff elevation on an access-point line is
  refused in the core, not configured (§8), so offering the row would be the
  "Saved and live" trap `PS_PIN` exists to close.

**Serial page** (`ser_`):

| Key | Label | Wide | Kind | Range | Default | When |
|---|---|---|---|---|---|---|
| `ser_tx` | `TX pin` | `The node's TX, to the terminal's RX` | PS_PIN | -1..max | -1 | next restart |
| `ser_rx` | `RX pin` | `The node's RX, from the terminal's TX` | PS_PIN | -1..max | -1 | next restart |
| `ser_baud` | `Baud` | `Speed, matching the terminal's own setting` | PS_CYCLE | `300\|1200\|2400\|9600\|19200\|38400\|57600\|115200` | 9600 | next restart |
| `ser_fmt` | `Format` | `Data bits, parity and stop bits` | PS_CYCLE | `8N1\|7E1\|7N1` | 8N1 | next restart |
| `ser_flow` | `Flow` | `Hardware flow control on RTS and CTS` | PS_CYCLE | `none\|rtscts` | none | next restart |
| `ser_hang` | `Hang up` | `How this node knows the terminal has gone` | PS_CYCLE | `idle\|dcd\|dtr` | idle | next restart |
| `ser_idle` | `Idle min` | `Minutes of silence before the line is freed` | PS_NUM | 1..120 | 20 | live |
| `ser_board` | `Board` | `Which board this line calls; blank asks` | PS_OPTNUM | 1..5 | blank | live |
| `ser_btn` | `Button` | `A press hangs up; a press when idle calls` | PS_PIN | -1..max | -1 | next restart |

`-1` on a pin is off, the lights' convention. The pin rows are refused
against the node's own console pins and against the node profile's claimed
pins, by name (§1.3).

**Repeater page** (`rep_`):

| Key | Label | Wide | Kind | Range | Default | When |
|---|---|---|---|---|---|---|
| `rep_hops` | `Hops` | `Nodes a frame may be forwarded through` | PS_NUM | 1..2 | 1 | live |
| `rep_up` | `Upstream` | `The node this one forwards through; blank is the board` | PS_OPTNUM | 1..5 | blank | live |

Downstream paths are **learnt, not configured**: the board keeps the next
hop it last heard an authenticated frame through, and a node that can
sometimes reach the board directly simply works, the duplicate dropped by
the replay window (`sat-types` §1). **Upstream is configured**, because a
planted chain's topology is known to whoever planted it and discovering it
is the 3-to-5-week mesh (§7).

### 4.3 The board's pages

The CALLIN plugin's own page, under CONFIG's four core rows:

| Key | Label | Wide | Kind | Range | Default | When |
|---|---|---|---|---|---|---|
| (core) | `Enabled` | — | PS_YESNO | — | **no** | restarts the plugin |
| `max` | `Max lines` | `Caller lines link callers may take; blank is all` | PS_OPTNUM | 1..`BBS_MAX_NODES` | blank | live |
| `name_as` | `Shown as` | `What WHO and the caller log show for a link caller` | PS_CYCLE | `sat\|sat+kind` | `sat` | live |

And **per pairing**, on `CONFIG sat <name>`:

| Key | Label | Wide | Kind | Default | When |
|---|---|---|---|---|---|
| `callers` | `Callers` | `Take callers in from this sat` | PS_YESNO | **no** | live |
| `staff` | `Staff` | `Staff may elevate on this sat's wired line` | PS_YESNO | **no** | live |
| `console` | `Console` | `This sat's wired line is the sysop console` | PS_YESNO | **no** | next restart |

**`staff` is per pairing and not board-wide, which amends my own first
draft of this page.** The draft had one `staff_ser` row on the CALLIN
plugin's page. It is wrong, and §4.5 is why: what the board is trusting is
**custody of a particular box**, so the switch has to point at a box. A
sysop can then allow staff from the terminal in the hall and refuse it from
the one in a field, which a single board-wide switch cannot express.

**`console` lines still ask for the staff password**, once, with an idle
lock after, which is not a new decision: CLAUDE.md's keyboard-sat entry
already settled exactly this shape for a local terminal, "one sysop
password to unlock then an idle lock". So a console line is an ordinary
wired line that lands on the hidden sysop node instead of a caller node,
and it meets §4.5's test and §4.6's rate limit like any other. One rule,
not two.

**`console` is the board's flag and never the node's claim, and that is
forced rather than chosen.** LINK.md's security rules say "Nothing a peer
sends grants a caller anything. No frame creates an account, changes a
level, runs a command, elevates to staff". A node that could announce "my
serial line is your console" would be a frame granting authority. So the
board names the pairing whose wired line lands on the hidden sysop node,
which is also the 1.2.3 decision ("the console line should use the sysop
hidden line") implemented without breaking the rule.

**A per-pairing `staff` row exists and an `ap_staff` row does not, and the
asymmetry is the point.** An access-point caller is an anonymous phone on an open network
whose address cannot be banned, so no staff, ever, in the core (§8). A
wired terminal caller had to be standing at a node the sysop planted and
paired, which is the same trust LINK.md already grants the serial
transport ("trusted by the wire: no pairing and no sealing... whoever can
plug into the board's UART already has the board") one box further out. So
it is offered, off by default, per box, and gated by a test the board can
actually run (§4.5) and a limit it can actually count (§4.6).

### 4.4 The caller-visible lines

For `satwords.h`, each measured against 39 columns including the `--> `
that `Bbs::markedLine` adds, so the text is at most 35:

| Case | At 40 | At 80 |
|---|---|---|
| Arrived over an access point | `Open Wi-Fi gateway, not encrypted` (33) | `You came in over an open Wi-Fi gateway, not encrypted` (52) |
| Arrived over a wired terminal | `Wired line, then encrypted` (26) | `Wired to this box, then encrypted to the board` (46) |
| The node lost its board | `Lost the signal.` — already exists as `kWhySignal` | the same |

The wired line is worth its own wording because **the terminal-server role
is the only CALLIN path that is private end to end**: a wire the sysop ran,
then AES-128-CCM on every frame. Saying "not encrypted" there would be
wrong in the one place this project could honestly say otherwise. Final
copy is `explain`'s.

### 4.5 Staff on a wired line: the test

Rob, 2026-10-05: **"only allowed of secure on wired terminal. rate limits
apply"**. Read as three things, and the reading is stated so it can be
corrected:

1. **No staff over an access-point line.** Confirmed policy now, as well as
   forced by §8.
2. **Staff over a wired terminal line, but only while the path is secure end
   to end.** "Secure" is a property the board must **test**, not an
   adjective it assumes.
3. **Rate limits apply**, which is new work, in §4.6.

**`Bbs::callinStaffAllowed(const Session&)`, in `src/core/bbs_shell.cpp`
beside `staffPassword`, called by `staffPassword` before any comparison.**
It returns false unless **all** of these hold, and false on anything it
cannot determine, because this is authority and authority fails shut:

| # | The test | Why it is testable rather than asserted |
|---|---|---|
| 1 | The line is a CALLIN line (`Session::link`, 1.2.3). A socket, SSH or the board's own serial line is not this test's business | the discriminator 1.2.3 already specifies |
| 2 | **The link hop is sealed, or it is the board's own cable.** A radio pairing is AES-128-CCM on every frame with the 20-byte header as associated data, under a `k_link` from a P-256 ECDH that two sysops confirmed with a four-digit code. LINK.md's serial transport is **not** sealed (`SEC` is never set on serial) and does not need to be: it is "trusted by the wire", a cable into the board's own UART | both are checkable facts about the transport the frame arrived on, not claims inside it |
| 3 | **The sat reports no access point running.** Not "this line says it is wired": a per-line claim is a frame asking for authority, which LINK.md forbids. A sat with the access point role off has no anonymous callers to confuse a wired one with, and that is a property of the device the sysop can also verify by looking at the box | the role report is sealed under `k_link`, so it is as authentic as anything else from that sat; and `CONFIG sat <name>` shows the sat's roles on the very form where the sysop sets `staff`, so a sat with an access point on is visibly refused rather than silently trusted |
| 4 | The board's own per-pairing **`staff` flag is yes** (§4.3) | the board's setting, never the peer's claim |
| 5 | §4.6's counter has not tripped for this pairing | — |

**A repeater in the path does not disqualify, and that corrects the
coordinator's reading.** A relayed frame is exactly as secret as a direct
one: the repeater forwards sealed frames it cannot open, holds no key for
the traffic, and can drop or delay a frame and do nothing else. And a
repeater **cannot** terminate a session, which §7 shows is forced rather
than policy. So hops add nothing to the secrecy question, and the thing
that actually matters is custody of the far sat rather than how many boxes
the bytes crossed.

**What the test does not prove, said plainly because it is the real
limit: it proves the path's secrecy, not the node's custody.** A stolen
gateway sat still holds its `k_link`, so a thief's wired line passes every
one of the five tests. Nothing cryptographic can tell a sat in the sysop's
hallway from the same sat in a thief's van. That is the same shape as "a
concentrator somebody else plants reads your passwords" and as "whoever can
plug into the board's UART already has the board", and the answer is the
same: **custody is physical, and the lever is the per-pairing switch** plus
§4.6's limit, which is what caps what a thief can do with it to three
guesses a quarter of an hour.

### 4.6 The rate limit for a line with no address

The existing scheme is `BBS_BAN_TRIES` 3 wrong staff passwords inside
`BBS_BAN_WINDOW_MS` 15 minutes, banning that address for `BBS_BAN_MS` 15
minutes, in `BBS_BAN_SLOTS` 8 fixed slots keyed on a `uint32_t ip`
(`src/core/guard.h:85`, `src/config.h:191-194`). **A CALLIN caller has no
address.** So:

**It keys on the pairing, and the reason is better than "there is nothing
else".** An address is an **unauthenticated** identity: anybody may pick
one, which is precisely why the existing ban is a blunt fifteen minutes and
why a relay's address cannot be banned without locking out every caller
behind it. **A pairing is an authenticated one**: every frame under it is
sealed with a key from an exchange two sysops confirmed by comparing a
four-digit code, with a replay window over the packet numbers. So a pairing
is a *stronger* identity to rate-limit than an IP, not a weaker one.

**The attack each candidate resists or does not:**

| Key | Resists | Fails to |
|---|---|---|
| the individual line | nothing | a troublemaker hangs up and calls again on a new line. **Free. Rejected** |
| **the pairing** | the free reset, and every reset short of a physical act at the board: adding a pairing takes `LINK PAIR` at the board and a code compared at both ends, inside a short window. **Chosen** | distinguish a stolen sat from an honest one (§4.5), which no key can |
| the sat's MAC | nothing it should: a MAC is forged trivially, and LINK.md already records that a peer is identified before dispatch rather than by its MAC | — |
| board-wide | the same attacks the pairing key does | **it hands a troublemaker a way to lock the sysop out of his own board** with three wrong guesses. Rejected |

**Which failure I prefer, since the coordinator is right that both horns
exist.** The pairing key's failure is "a sysop who deliberately pairs a
second sat gets a second allowance". The board-wide key's failure is "any
stranger can refuse the sysop staff access". **Those are not comparable.**
The first requires the sysop's own cooperation at the board, which makes it
a choice rather than an attack; the second is a denial of service a
troublemaker performs on the person the mechanism exists to protect. So the
pairing wins, and the project's precedent agrees in both directions: fail
shut on **authority** (a tripped pairing gets no staff) and fail open on
**access** (its callers keep calling), which is exactly how a wrong
`BYE <password>` is a plain logoff rather than a disconnection until the
third one.

**What trips, and what it does.** Three wrong staff passwords on one
pairing's lines inside fifteen minutes, counted together across every line
of that sat (not per line: per line is the free reset above). On tripping,
**staff elevation is refused on that pairing for fifteen minutes. The
callers are not refused and the sat is not unpaired.**

- refusing the callers would let anybody at a terminal in a hallway take out
  that sat's whole gateway, the sysop's own console line included, by
  typing three wrong passwords. That is the lock-the-sysop-out failure
  scoped down to one box, and it is still the wrong failure;
- refusing the line is pointless, since a troublemaker simply takes
  another;
- and refusing staff is the minimum that stops the attack and costs nothing
  else. **The asymmetry with an address ban is deliberate and worth the
  sentence:** an address is one of billions and banning one costs the board
  nothing, so the address path bans the connection; a pairing is one of
  eight and each one cost a sysop a physical act, so banning it is
  expensive and a troublemaker would be the one choosing to spend it.

**Where it lives, and what it costs. A table of its own, `SatGuard` in
`src/core/guard.h`, indexed directly by the peer slot**, so the index *is*
the key and there is no key field and no lookup:

```
struct Entry { uint32_t firstFail; uint32_t until; uint8_t fails; uint8_t ahead; };
Entry slots_[Engine::kPeers];
```

10 bytes, 12 with Xtensa's alignment, so **96 bytes of static DRAM at
`kPeers` 8**, and 240 if phase 5 raises `kPeers` to 20. `fails`, the window
and `until` behave exactly as `BanList`'s do, and `plat::since` is used for
every elapsed comparison, per the 1.2.1 rule.

**The cheaper alternative was rejected, and finding out why found a
pre-existing bug.** The zero-byte option is to key a pairing into the
existing `BanList` as a pseudo-address, `ip = 1..kPeers`, since `0.0.0.0/8`
is never a valid source address and `peerAddr` cannot produce one; then
`slotFor`, `fail`, `banned`, `clear` and the window all work untouched.
**But `BanList` evicts**, and the eviction is not safe for authority:

```
src/core/guard.cpp:71-86   BanList::slotFor
    ... oldest = the oldest entry with until == 0  ...
    slot = empty ? empty : (oldest ? oldest : &slots_[0]);
    *slot = Entry();
```

With all eight slots carrying **active** bans, `empty` and `oldest` are both
null and the fallback is `&slots_[0]`, which is then reset: **an active ban
is silently cleared.** For a pairing entry that would silently restore staff
access, which fails open on authority and is the shape this project calls a
bug. A table of one entry a pairing cannot overflow by construction, which
is why 96 bytes is the right 96 bytes.

**And that eviction is a real pre-existing hole on the address path too,
independent of this feature**: nine distinct addresses banned inside one
fifteen-minute window silently un-ban the oldest-indexed one. `BBS_BAN_SLOTS`
is 8, three wrong passwords each, so it takes 27 wrong passwords from nine
addresses to reach it. Low severity and genuinely reachable. **Reported for
the code-review queue, not fixed here.**

**It does not survive a reboot, and that is the precedent rather than
laziness.** `BanList` is documented "RAM only, a reboot clears it" and
`LoginGuard` as "failures never write to flash", and the reason is worth
keeping: a guesser who could make the board write flash on demand would have
a flash-wear attack and a Rule no. 1 problem for free. A reboot clears it,
and a troublemaker who can power-cycle the board has physical access and has
already won — which is the argument the existing ban already rests on.

**A sysop clears it the way `UNBAN` clears an address, through `UNBAN`.**
`cmdUnban` tries `ipFromText` first and already fails cleanly on anything
that is not a dotted quad, so the fallback is natural: look the argument up
as a sat by name or by its `LINK` number. One verb, no new command, and
symmetric with `BANS` listing both. Usage becomes
`UNBAN a.b.c.d, or UNBAN <sat>` (36 columns, fits 40).

**What `BANS` shows for an entry with no address.** The list is 17 columns
of key and 8 of minutes (`Banned IP        Min left`, `rowBans` in
`bbs_sysop.cpp:538`). A pairing entry takes the same column, with the sat's
name prefixed by `@`:

```
Banned           Min left
192.168.0.37           12
@shed                   9
@ = a sat: no staff, callers OK
```

- the header's "IP" drops, the column width does not move, so nothing else
  on the row shifts;
- `@` because a sat name is `char name[16]` and `@` plus 15 characters is
  exactly the 16 the column holds, and because `@` exists in ASCII, PETSCII
  and CP437 alike, which is the same reason the inline codes use it;
- the footnote is the `* guest  > CO-SYSOP  ] SYSOP` pattern already under
  the lists, and it carries the one thing the row cannot: that a sat entry
  refuses staff while an address entry refuses the connection. 31 columns at
  40; at 80, `@ = a sat, not an address: staff is refused there, callers
  still get in` (70).

**Two things that need no new work at all**, and are worth recording so
nobody builds them twice:

- **the per-handle account lockout already covers CALLIN callers**, because
  `LoginGuard` keys on the handle: five wrong account passwords in fifteen
  minutes locks that handle whatever line it came in on. So account
  passwords on a gateway line are protected exactly as well as over telnet;
- **the SyncTERM typeahead allowance carries straight over.** `aheadTake`
  is one held answer per key per window, and the `ahead` byte is already in
  the `Entry` above, so a wired terminal that sends a burst gets the same
  single uncounted held answer a telnet caller does, keyed on its pairing.

---

## 5. Naming, settled

Checked against `src/`, `tools/`, `host/` and `satwords.h` before
proposing. Counts are case-insensitive whole-word hits.

| Candidate | Hits in src/tools/host | Verdict |
|---|---|---|
| `node` | everywhere (`BBS_MAX_NODES`, `nodeName`, `nodeLabel`, `NODES`) | **Taken, hard.** A node is a caller line |
| `repeater` | **0** | Free |
| `relay` | 7, all board-pin comments about relay modules | Taken in `CLAUDE.md`'s sat-kinds list as the forwarding sat; and a relay module is a different thing on a GPIO page |
| `gateway` | 2, both comments (`link.h:149`, `linkradio.h:32`) reserving kind 3 | **Free as a word, and already the reserved wire name** |
| `portal` | **0** | Free |
| `broker` | **0** | Free |
| `hub` | **0** | Free |
| `dock` | 0 in src, but `CLAUDE.md` has the ham radio **dock** and the **docking radio** | Taken |
| `booth` | 0 in src, but the **voting booth** is a planned feature | Taken |
| `station` | 47 | Taken: the Wi-Fi station |
| `bridge` | 65 | Taken: the serial bridge, RS485 |
| `beacon` | 10 | Taken: a link frame |
| `tower` | 13 | Taken: the panel's announce glyph |
| `switchboard` | 8 | Taken: the lights effect |
| `exchange` | several | Taken: key exchange |
| `outpost`, `kiosk`, `waypoint` | 0 | Free, and not needed |

**Settled by Rob, 2026-10-05, exactly as proposed below.** The reasoning he
accepted is that a word he uses unprompted for a thing is that thing's word,
which is what reverses the broker study. **The strings live in
`src/core/satwords.h`** for the sat vocabulary, and in a sibling
**`src/core/reachwords.h`** for the broker's, since a broker is not a sat
and `satwords.h` is kept for boxes on the link.

- **The device is a gateway sat.** Kind id **3**, already reserved in
  LINK.md and in `link.h:149` ("3 is reserved for 1.3.0's gateway kind
  (LINK.md): never reuse it"), shown as **`gateway`** in `LINK`'s kind
  column, which is the column that today reads camera, door, gpio, sensor,
  device. `satwords.h` gains `kKindGateway = "gateway"` and
  `kGatewaySat = "gateway sat"`, beside `kDoorSat` and `kOrbiter`.
  - It is the third class, and it completes the set cleanly: an **orbiter**
    feeds the board data, a **door sat** is one a caller goes into, and a
    **gateway sat** is one a caller comes in through. Three classes, three
    directions, nothing overloaded.
- **What it speaks is CALLIN**, family 3, unchanged.
- **Rob's "callin node" survives as the repository name**, `unleashed_callin`,
  exactly as camsat does: "camsat stays only as the firmware and repo name,
  never a type on screen". That keeps Rob's own word where he will look for
  it and keeps `node` out of the sysop's screens.
- **The roles keep Rob's own words**: **access point** (short **AP**),
  **terminal server** (short **Terminal** in a 9-character label), and
  **repeater**. All three are correct terms with the right history; a DEC
  terminal server did exactly this job, and "repeater" is the ham word Rob
  reads natively.
- **The page a phone lands on is the portal.** Free in the tree, and the
  right word for the page rather than for the device.

**Which reverses the broker study's §5.2, and it should be reversed.** That
study recommended `repeater` for the internet-side box, carefully, having
already ruled out `relay`, `beacon` and `tower`. But **Rob has now used
"repeater" twice, unprompted, for the ESP-NOW forwarding role**, and a word
Rob uses for a thing is that thing's word. So:

- **`repeater` is the ESP-NOW forwarding role**, here;
- **the internet-side box is the broker**, which is free in the tree and is
  already the role word that study uses throughout ("the directory as the
  broker", "a broker only accepts connections"). `BROKER` as the staff
  verb, `CONFIG broker`, and in the board's voice "Callers are reaching you
  through unleashedbbs.net." One word covers both of its mechanisms, an
  **onion broker** and a **port broker**, which keeps that ladder one
  concept exactly as `repeater` did.
- **And the broker is not a sat**, which that study already argued and which
  this naming makes plainer: a sat is a box on the µnleashed link, reached
  over ESP-NOW or a serial line. A broker is none of those. Its words belong
  in a sibling `reachwords.h`, not in `satwords.h`.

**Confirmed by Rob on the day it was proposed**, so it costs nothing at all.

---

## 6. New work against what is planned, and what phase 1 now is

### 6.1 The division, by repository

**Already planned, and this consumes it rather than adding to it.** The
1.2.3 entry in CLAUDE.md specifies the core half:

- **the caller-line core**: "a session whose bytes come from a stream, not a
  socket, taking one of the 10 caller nodes while in use (zero extra RAM;
  telnet and SSH share the same 10)";
- **`Session::link`**, generalising the line kinds (socket, SSH, serial,
  CALLIN, console);
- **CALLIN over the link**, family 3 and kind 3, both already in LINK.md's
  tables marked reserved. The numbers exist; nothing sends or takes them,
  and `link.h:150` still defines only `KIND_UNKNOWN`, `KIND_CAMSAT` and
  `KIND_DOORBOX`;
- **the hardware button** on a terminal sat, which maps onto `ser_btn`;
- **the serial sat** and **1.3.0's ham dock**, which both ride on CALLIN
  unchanged. So building CALLIN once pays for rows 1, 2, 7, 14 and 15 as
  well as the portal.

**New, in the new node repository (`unleashed_callin`):** the role
framework and the switches; the SoftAP, DHCP and the DNS redirector (the
IDF example's component, vendored, since it is example code rather than a
registry component); the portal and its OS probe handling; the board list
and the picker; the browser terminal, gzip-precompressed in the app image
and lazy-loaded; the WebSocket bridge; the UART source; the node's own
setup page; and the repeater's forwarding task.

**New, and small, in the core beside CALLIN:** `staffPassword` returning
`Access::None` for an access-point line before any comparison; the `*`-style
marker and the address column showing the sat's name; the plugin's opt-in
switch and the per-pairing `callers` and `console` flags; the connection
line's wording; and later the free-lines nudge.

**New, in the shared engine** (`src/core/link.*`, which a sat builds from
the same files): `KIND_GATEWAY = 3`; the relay wrapper with its hop count;
`kPayloadMax` becoming per peer (222 direct, 220 one hop, 219 two, §7).

### 6.2 What phase 1 is now, after Rob's correction

The study's phase 1 was a dedicated AP box with no roles and no switches.
**Most of it survives; what changes is the architecture, and that change is
the whole point of Rob's correction.**

**Phase 1, 6 to 8.5 days, new node repository, no core change at all.**
Rob approved starting it, and approved the terminal server inside it, on
2026-10-05; the figure is the study's 4-to-6-day access-point phase plus 1
to 1.5 days for the terminal server plus the framework.

- the node firmware with **the three-switch framework in place from the
  first commit**: one caller-stream abstraction, two sources implemented
  (WebSocket, UART), one uplink implemented (IP), and `repeat` present in
  the settings and refusing to turn on with "not in this build". Building
  the framework now is what stops this becoming the two-box design again;
- the **access point role**: open SoftAP, DHCP, the DNS redirector, the
  portal, the board list, xterm.js over a WebSocket;
- the **terminal server role** (**approved into phase 1 by Rob**), which is
  the cheapest possible proof that the roles really are switches over one
  core: a UART feeding the same abstraction the WebSocket does. **1 to 1.5
  days on top, and it buys rows 1 and 4 immediately and tests the one design
  claim phase 1 exists to test**;
- the **IP uplink**: a plain telnet connection to the board's own address,
  because the board joins the node's access point as a station (row 8) or
  both sit on a router (row 12). **No link, no CALLIN, no pairing, no new
  protocol, and no core change;**
- the node's own setup page behind `ap_sysop`;
- the open-access-point line on the portal, and the plain-address fallback
  for a phone whose portal did not pop.

**What it proves that nothing else can:** that a phone pops the portal, that
xterm.js satisfies the board's terminal detector, that CP437 art draws, that
keystroke echo is bearable, that a base ESP32 holds it, and that one device
with switches is the right shape. **It is also the phase that is allowed to
kill the plan**, for a week rather than a month. And it de-risks browser SSH
for free, because the detector and CP437 questions that
`internal/plan-web-ssh-2026-10-04.md` carries as unverified are answered
here on a simpler path.

**Phase 1 does not include** the repeater (three boxes and open ground),
CALLIN (phase 3), pairing, or more than one board in the list.

**Port mapping is independent of all of it.** It is the board's own
reachability from the internet and is being built in its own worktree; it
touches no node, no role and no CALLIN, and the two do not block each other
in either direction.

### 6.3 The phase table

| # | What | Repo | Days | Needs |
|---|---|---|---|---|
| **1** | **The local gateway** (§6.2): the three-switch framework, the **access point** role, the **terminal server** role, the **IP uplink**, the node's own setup page. No link, no CALLIN, no pairing, **no core change**. Rows 8, 12, and rows 1, 4 and 14 over an IP uplink | node | **6 to 8.5** | one ESP32, one board, one phone, one terminal. **Approved, starting** |
| **2** | **Measure phase 1.** Echo p50 and p95 on a real phone, screen on and off; DTIM 1 against 2; the portal on iOS, Android and Windows, with and without Private DNS, and specifically whether a recent Samsung pops it at all; whether the Apple sheet runs a WebSocket or the terminal must open in the real browser; how many phones hold a terminal; **the heap on a base ESP32 with no PSRAM, with all the roles phase 1 has on**; whether an idle association is ever reaped | — | **1 to 2** | Rob's phone. **Needs his explicit go** |
| **3** | **The caller-line core and CALLIN** (1.2.3 as planned) plus §6.1's small core items. Rows 1, 4, 15 | core | **5 to 8** | host, one bench flash |
| **4** | **The link uplink on the node**: CALLIN instead of telnet, several boards on the portal, busy handling, and a clean `CLOSE` when a phone disassociates so a walked-away caller frees a line in a second rather than in minutes. Rows 3, 11 | core + node | **4 to 6** | two ESP32s, one board |
| **5** | **Raise the lines.** `kPeers` 8 to 20 (about +2.9 KB of heap, no static DRAM); `BBS_MAX_NODES` 10 to 14 where a profile holds it, read off the ELF; the free-lines nudge | core | **2 to 3** | host, bench |
| **6** | **The repeater, one hop, event-driven.** Rows 2, 5, 6, 9, 13 | node | **8 to 10** | three ESP32s, open ground |
| **7** | **Two hops**, a planted chain with a hop count and no routing. Row 10 | node | **4 to 6** | four boxes |

---

## 7. Repeater to repeater

Rob: "repeater to repetaer of the callin sat should apply". **The
coordinator's recommended shape is right in all five parts and I would
strengthen two of them.**

**Confirmed as recommended:**

- **cap at two hops**;
- **plan the topology by hand** rather than discovering it;
- **forward from the receive callback, never from a tick**;
- **a repeater never terminates a session**;
- **each node can say where a frame died**.

**Where I would put it differently:**

1. **"A repeater never terminates a session" is forced, not a policy**, and
   saying so is better than a rule somebody could be argued out of. A
   repeater holds no key for what it carries: the ECDH secret, `k_link` and
   every session key are the far peer's and the board's alone, and every
   frame is AES-128-CCM with the 20-byte header as associated data, so a
   repeater **can drop or delay a frame and can do nothing else** — not read
   it, not alter it, not replay it (`sat-types` §1). The policy only bites
   for row 6, a node that repeats *and* carries its own caller, and there
   the two cannot be confused **because one has a key and the other does
   not**: its own caller is its own session under its own `k_link`;
   forwarded frames are opaque bytes with two bytes prepended. **One device
   doing both is safe precisely because the mechanism keeps them apart.**
   What must be explicit is the other half: a **concentrator**, one box that
   terminates many callers' sessions and re-originates them under its own
   pairing, reads every keystroke. That is a different device with a
   different trust story, it is the only thing that scales past twenty
   nodes a board, and it is not this (§8, and study §3.4).
2. **Cap the wire at three hops and the setting at two.** A hop count is
   free — three bits in the wrapper's flags byte — and putting the cap in a
   setting rather than in the frame format means a bench can try three
   without a protocol change or a reflash of every node. The binding reason
   for two is **not latency**: three event-driven hops is about +6 to 18 ms
   against a 20-to-40 ms chain and a caller would not notice. It is airtime
   and repair (§8). So: `rep_hops` 1..2, and the format reaches 3.

**The one protocol change multi-hop forces.** `sat-types`' wrapper is two
bytes, one saying "relayed" and one naming the far peer's index in the
relay's table, which names one hop and nothing more. For two it becomes a
flags-and-hop-count byte plus one index a hop:

| Path | Wrapper | `kPayloadMax` |
|---|---|---|
| direct | none | **222** (`link.h:73`) |
| one hop | 2 bytes | **220** |
| two hops | 3 bytes | **219** |

About 1.4% of throughput at two hops, which for a terminal is nothing.
`kPayloadMax` becomes per peer, which `sat-types` already called for at one
hop. **A hop count is also what makes a mis-planted chain fail safely**: two
nodes configured to forward through each other decrement to zero and drop,
instead of filling the channel until somebody notices.

**Where a frame died, with no console, cheaply.** Three pieces, none of them
a new family:

- each repeater keeps, per downstream peer, frames forwarded, frames
  dropped and the reason (hop count spent, no route, send failed), and when
  it last heard that peer. A handful of bytes a peer;
- it reports them in its own status over family 0, so `LINK` on the board
  shows a chain, "mast-1 via root, 14,203 frames, 7 dropped", and `SATS`
  shows "via root" as it already plans to;
- and **the portal shows a node's own uplink chain to anybody standing in
  front of it**, which is the part that makes a fairground fault
  debuggable with no laptop. It is free: the page machinery is already
  there for the board list.

---

## 8. The honest limits

Carried forward rather than rediscovered. Sources named; nothing here is
new research.

**Capacity:**

- **Ten caller lines a board**, and a gateway caller takes one of the same
  ten. `BBS_MAX_NODES` is 10 (`src/config.h:97`) and the pool is a static
  array at about 6,980 bytes a session. **The next wall is fourteen**, from
  `static_assert(BBS_MAX_NODES + 2 <= 16, "takeTraffic keeps a bit per
  session in 16")` (`src/core/bbs.h:1315`); past that the mask widens and
  every `uint8_t` node index wants an audit. A CALLIN line uses **no
  socket**, so the 16-socket cap is not in the way for a board fed by
  gateways, and the cost of each extra line is static DRAM: about two more
  on a WROOM, all four on an S3.
- **Five boards a node** (`kHosts = 5`, `link.h:295`), which is why fifty
  simultaneous callers, not ten, is the honest fairground headline: plant
  four boards and it is forty. **A few dozen boxes buy coverage, not
  capacity.** Worth saying in the copy rather than discovering at the event.
- **Twenty ESP-NOW peers**, the hard wall: `ESP_NOW_MAX_TOTAL_PEER_NUM` is
  20 in the pinned framework. Our own `kPeers` is 8 and raising it to 20
  costs about 2.9 KB of heap and no static DRAM.
- **Fifteen phones a node** (`ESP_WIFI_MAX_CONN_NUM`), and **about twelve
  concurrent terminals** on the node's sockets (`LWIP_MAX_SOCKETS` 16 less
  the DNS responder and the HTTP server's three internal). Those three
  numbers sit in the same region, which is tidy: the board is the binding
  constraint, where a sysop would expect it.
- **Sessions multiplex over a pairing**, so one node carries as many callers
  as the board has lines on one pairing. Pairings are devices, not callers.

**Latency:**

- **A phone in power save can buffer downlink traffic for about 205 ms**:
  beacon 100 TU and `dtim_period` 2, both IDF SoftAP defaults.
  `dtim_period = 1` halves it. **The biggest single term in the whole chain
  and the first thing phase 2 measures.** The board's own telnet echo is
  p50 4 ms, p95 12 ms with five callers.
- **Two tick-driven hops is where a caller notices**: about +40 ms a hop on
  a 20 ms poll against +2 to 6 ms a hop forwarding from the receive
  callback. Hence §7's rule.
- **Crypto is not the problem and never will be**: 93.6 µs to seal and
  94.7 µs to open a frame on a classic ESP32, 78 µs each on an S3. Under
  400 µs of CCM round trip against a 20-to-40 ms chain. Nobody should spend
  a day on it.
- **`kAckDelayMs 20` and `kRtoMs 150` are sized for bulk pictures, not
  keystrokes**, so a lost frame costs 150 ms before the first retry, which a
  caller sees as a dropped character and a stutter. Whether a terminal wants
  a shorter first retry is a real question and nobody has measured loss in a
  crowd.

**Hops consume capacity rather than adding it:**

- **every peer in the whole chain shares one channel**, so a three-hop chain
  puts three times the frames on the same air for one caller's keystrokes,
  and a relay cannot send while the node below it does. Throughput falls
  roughly with the hop count.
- **the infrastructure, not the BBS traffic, is what fills the channel.**
  Thirty nodes beaconing every 102.4 ms is about 290 beacons a second and
  roughly 25 to 30% of the channel before anybody types. A keystroke is one
  250-byte frame, about 0.2 ms. **Raise the beacon interval and spread the
  boxes across channels 1, 6 and 11 with their own boards** — which is also
  the answer for lines, a convergence worth noticing. All of it is
  arithmetic from defaults and none of it is measured.
- **a hand-planted chain has no redundancy and no repair.** One flat battery
  drops everything behind it, mid-session, and healing is the 3-to-5-week
  mesh rather than the 1.5-week relay. An ESP32 running a SoftAP does not
  sleep and draws roughly 100 to 180 mA, so a 10,000 mAh battery is about a
  day a box: thirty boxes is thirty batteries and somebody walking the site.
  **Say that on the site when this ships**, because somebody will plant
  thirty and be surprised at hour eighteen.
- **LR must be off on any board carrying interactive callers.** It cost 29
  to 55 ms gateway pings on the bench, which is callers' latency and Rule
  no. 1, and against a 20-to-40 ms budget it is the worst trade available.

**Secrecy:**

- **The phone-to-node hop is in the clear on an open access point.** Anyone
  in radio range with a laptop reads every keystroke and every screen,
  including a password. The node-to-board hop is sealed, so **the weak hop
  is the first one**, which is the opposite of the usual telnet story.
- **TLS on the node is refused on certificate grounds, not on memory.** A
  real project runs `esp_https_server` with an embedded certificate on a
  4 MB ESP32-C3, so the part can do it. But a captive portal cannot
  intercept HTTPS at all, a node can only serve TLS under a name nobody has
  signed, and every phone would show a full-page security warning before the
  caller reached the board. A front door that opens with "Attackers might be
  trying to steal your information" is a worse experience *and* a worse
  security message than plain HTTP with an honest sentence. RFC 8908 hits
  the same wall and concedes it.
- **The terminal-server role is the exception** and the only CALLIN path
  that is private end to end (§4.4).
- **Somebody can plant their own access point with the same name.** Trivial
  on an open network, with no defence at the Wi-Fi layer. The mitigations
  are social and physical: a specific rather than generic name, the address
  printed on the box, and the honest line telling people not to use a
  password that matters. **A known and accepted risk, in those words.**

**Authority:**

- **No staff elevation on an access-point line, and it is forced.** The ban
  list keys on an IPv4 address (`Entry* slotFor(uint32_t ip)`,
  `src/core/guard.h:85`) and **a gateway caller has no address at all**.
  Synthesise one a node and a single bad guess bans every phone on it;
  synthesise one a phone and the phone changes its MAC. Since the staff
  password's only rate limit is that ban, three wrong in fifteen minutes,
  without this rule an open access point is an unlimited guessing path at
  the one password that owns the board. So `staffPassword` returns
  `Access::None` before any comparison, the same shape as "no staff over RF"
  and "no staff from the relay". **Confirmed as policy by Rob, 2026-10-05**,
  as well as forced. An access-point caller who types `BYE <something>` gets
  the plain logoff a guest gets, with nothing counted anywhere, because
  nothing was compared.
- **The general rule this makes, which belongs in CLAUDE.md as one:** a
  caller whose line cannot be attributed to a bannable address is never
  staff **unless the board can test that the line is private end to end and
  limit it on an authenticated identity**. The second clause is new: the
  wired terminal line is the first path that can satisfy it (§4.5, §4.6),
  and it satisfies it because a pairing is a stronger identity than an
  address rather than a weaker one.
- **What still works** for a gateway caller: the per-handle account lockout,
  five wrong in fifteen minutes, because it keys on the handle; and `KICK`,
  because it acts on a node. **What does not**: anything keyed on an
  address, which is why §4.6 keys on the pairing. A sysop's levers against a
  troublemaker are the per-pairing `callers` and `staff` switches (§3.4,
  §4.3) and `UNBAN <sat>`.
- **The caller log and WHO show the node's name**, because that is the
  actionable fact for a sysop ("three bad logins from the gateway by the
  beer tent"), marked the way a guest is marked with `*`.

**Roaming:**

- **A phone that walks between two nodes loses its line, and should.**
  Resuming would mean the board matching a new stream to an existing
  session, which is a session-hijack mechanism however it is keyed, and a
  phone's MAC is not an identity. The line drops, the board sees a clean
  hang-up, the caller taps again and logs in again. **The portal says so in
  one line** so it is an understood thing rather than a bug. The node
  sending a clean `CLOSE` on disassociation is phase 4 and matters: on a
  ten-line board, walked-away callers holding lines for the idle timeout is
  the difference between ten lines and three.

---

## 9. Out of scope: an ESP32 running a directory

Rob, 2026-10-05, and this settles it: *"well Id like (different device,
still esp32) if we NEED it to help the user get outside the firewall
routing we could have an esp32 device acting like directory, or an esp32
version of directory running more obscure and hidden networks privacy
forward not in the directory. So this is about discovering networks and
bridging the gap but I think you're solving that otherwise and this is not
part of callin"*. **It is its own item. Nothing here designs it, costs it,
or gives the node a role for it.**

The two touch at exactly one point, and the next reader will ask: a node's
board list answers the same question a directory answers, at a smaller
scale and with no internet.

**Recommendation: design the board list so another source could feed it
later, and give the node no discovery of its own.** Concretely, that is the
one field already specified in §3.1 — **`source`, today `paired` or
`typed`** — and nothing more. A future ESP32 directory then becomes one more
value of that field and one more way to fill a slot, and no other part of
the node changes. The cost now is one enum field, which is the cheapest way
to keep a seam visible.

**What the node must not get is any protocol for learning boards it was not
told about.** The reason the current list is trustworthy is that a node
cannot list a board it has no key for (§3.4), and an unauthenticated list
arriving over the air is exactly how a node ends up advertising somebody
else's board to a field full of phones. So: a slot is filled by a pairing
(two physical acts, a four-digit code) or by a sysop typing an address, and
a third source, whenever it exists, has to bring its own answer to "who
agreed to this" before it fills one.

---

## 10. What Rob has settled, and what is still open

### Settled by Rob, 2026-10-05

1. **The words** (§5), exactly as proposed: **gateway sat**, kind 3, shown
   as `gateway`; the repository `unleashed_callin`; the roles **access
   point**, **terminal server**, **repeater**; the page the **portal**; and
   **the internet-side box is the broker**, reversing the broker study's
   §5.2. Strings in `satwords.h`, and the broker's in a sibling
   `reachwords.h`.
2. **The terminal server is in phase 1** (§6.2), so phase 1 is **6 to 8.5
   days**.
3. **Staff elevation**, from "only allowed of secure on wired terminal. rate
   limits apply": no staff on an access-point line; staff on a wired
   terminal line only while the board's five-part test passes (§4.5); and a
   rate limit keyed on the pairing (§4.6).
4. **A board opts in to being gatewayed, off by default and per pairing**
   (§3.4), because pairing consents to a device and not to a purpose, and
   the per-pairing flag is the only lever against a troublemaker when
   nothing keys on an address.
5. **`console` stays the board's per-pairing flag and never the node's
   claim** (§4.3).

### Still open

6. **Phase 2's test plan needs his explicit go**, per the standing rule.
7. **The wording** for the portal's open-access-point line and the board's
   two connection lines (§4.4). Mine are a starting point; `explain` owns
   the copy.
8. **Two hops as the setting, three as the format** (§7), before phase 7.
   Recommended, so a bench can try three without a protocol change. The cap
   is for airtime and repair, not latency.
9. **The pre-existing `BanList` eviction hole** found while costing §4.6:
   with all eight slots carrying active bans, `slotFor`'s fallback resets
   `slots_[0]` and silently clears an active ban, so nine addresses banned
   inside one window un-ban the first. Low severity, genuinely reachable,
   nothing to do with this feature. **For the code-review queue.**

**Not this spec's, and his to schedule:**

10. **The 16-socket ceiling is a property of the pinned framework, not of the
    chip.** `LWIP_MAX_SOCKETS` is `range 1 16` at IDF 5.3.1 and
    `range 1 253` from 5.3.2, verified from two frameworks on this machine
    (study §0). It changes nothing a CALLIN line needs, since a CALLIN line
    is not a socket, but it does change the sentence CLAUDE.md carries, and
    a framework bump is its own piece of work with its own 23-environment
    revalidation.
11. **An ESP32 running a directory** (§9), which he has already separated
    out.

**Answered, so he need not decide:** whether a base ESP32 can be the node
(yes, and there is an existence proof on a smaller part; internal RAM is
the open question, measured in phase 2); whether encryption over ESP-NOW
forces an S3 (no, it is built and costs about 94 µs a frame on the cheapest
part we support); whether TLS belongs on the node (no, on certificate
grounds rather than memory); whether end-to-end SSH helps a gateway caller
(no, it would encrypt the hop that is already encrypted and leave the clear
one clear); and whether the LR test is now urgent (no, and LR must stay off
on any board carrying callers).

---

## Sources

Everything measured is cited from one of these; nothing in this
specification is new research.

- `internal/study-ap-gateway-sat-2026-10-05.md` — the ceilings, the channel
  rules, the captive portal mechanics, the terminal's real byte sizes, the
  three-hop encryption matrix, the latency table, the airtime arithmetic and
  the failure modes. Its §2 lists what was verified and what could not be.
- `internal/study-broker-sat-2026-10-05.md` §5 — the internet-side box, and
  the naming argument this spec reverses.
- `internal/sat-types-2026-09-27.md` §1 — the one-hop relay: the wrapper,
  end-to-end keys, learnt paths, and the 1.5-week against 3-to-5-week
  figures.
- `LINK.md` — families and kinds (CALLIN 3, gateway 3, both reserved),
  channels and `BEACON`, pairing, the frame header, the serial transport's
  "trusted by the wire", and the security rules.
- `src/core/link.h` — `kPayloadMax` 222, `kPeers` 8, `kHosts` 5,
  `kSessions` 16, `kPingMs`, `kHostQuietMs`, `kAckDelayMs`, `kRtoMs`,
  and `KIND_*` with 3 reserved by comment.
- `src/core/guard.h`, `src/core/guard.cpp` and `src/config.h:191-194` — the
  ban list's address key, its eviction, `aheadTake`, and
  `BBS_BAN_SLOTS`/`TRIES`/`WINDOW_MS`/`MS`; `src/core/bbs_sysop.cpp:538`
  and `:775` — `rowBans`' columns and `cmdUnban`'s parsing.
- `src/core/satwords.h` — the settled sat vocabulary this spec extends.
- `src/config.h`, `src/core/bbs.h`, `src/core/guard.h`,
  `src/core/plugin.h`, `src/core/form.h` — `BBS_MAX_NODES`, the
  `takeTraffic` assert, the ban list's address key, `PluginSetting` and its
  label widths, `kMaxFields` and `kCoreRows`.
- `CLAUDE.md` — 1.2.3's caller-line core and serial sat, 1.3.0's ham radio
  design, and the 2026-10-05 decisions on the AP gateway, CALLIN repeating
  and a Zigbee-style mesh.
