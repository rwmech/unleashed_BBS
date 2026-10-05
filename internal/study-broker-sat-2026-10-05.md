<!--
 ===========================================================================
  µnleashed BBS
  Electronic freedom on a microcontroller.
 ===========================================================================

 File:         internal/study-broker-sat-2026-10-05.md
 Module:       Working notes / design study and plan

 Purpose:      Reaching a board with no forwarded port: peer to peer where
               possible, through a broker where not; whether .onion is the
               better answer; and a .onion face for the directory itself.
               A study and a plan: no code, config, test or document was
               changed by it.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v3 or later
 SPDX-License-Identifier: GPL-3.0-or-later
 ===========================================================================
-->

# Reaching a board with no forwarded port, and a `.onion` directory (2026-10-05)

Rob, over the course of one day:

- "since we have the directory, what about instead of opening ports, the bbs
  could reach out and broker the conversation", carrying **SSH only,
  ciphertext**, so the middle can never read a session;
- "id prefer it to be even peer to peer. With a broker satellite that could
  be anywhere in the wild";
- "any way to do this with .onion or another privacy and freedom forward
  method?";
- "yeah, thinking these are more S3 features for all of this since SSH can't
  do the base ESP32 I dont really care the approach as long as its
  **distributed and freedom/privacy forward**";
- "it doesnt have to be .onion but I thought it made sense to use a framework
  that exists already. Id prefer it **something the microcontrollers can do
  without a website**. The directory server is fine to be a broker for sites
  listed in the directory. Id like directory though to be freedom forward
  should someone want a .onion directory. Maybe we can create a fully private
  .onion host of directory too? complete all the research and provide a plan."

Four things are therefore settled and are not argued below: **`.onion` is an
example, not a requirement**, and what matters is reusing a framework that
exists rather than designing one; **the directory may broker for boards
listed in it**; **the board side must need no website**, meaning no browser,
no TLS, no WebSocket; and **S3-only is accepted**, the way SSH and displays
already are. The ranking words are Rob's: **distributed**, and
**freedom/privacy forward**.

- [0. Bottom line, and the plan in one table](#0-bottom-line-and-the-plan-in-one-table)
- [1. What was checked, and what I could not](#1-what-was-checked-and-what-i-could-not)
- [2. Is peer to peer reachable on this hardware?](#2-is-peer-to-peer-reachable-on-this-hardware)
- [3. The ladder](#3-the-ladder)
- [4. "Without a website": the rendezvous is an extension of announce](#4-without-a-website-the-rendezvous-is-an-extension-of-announce)
- [5. The repeater: shape, name, and the directory as one](#5-the-repeater-shape-name-and-the-directory-as-one)
- [6. The `.onion` path for a board](#6-the-onion-path-for-a-board)
- [7. A `.onion` directory](#7-a-onion-directory)
- [8. The other overlays, graded by what they provide](#8-the-other-overlays-graded-by-what-they-provide)
- [9. What each option hides, and from whom](#9-what-each-option-hides-and-from-whom)
- [10. The trust story](#10-the-trust-story)
- [11. What it costs the board](#11-what-it-costs-the-board)
- [12. What it costs whoever runs the broker](#12-what-it-costs-whoever-runs-the-broker)
- [13. Against the values](#13-against-the-values)
- [14. IPv6 and UPnP, compared fairly](#14-ipv6-and-upnp-compared-fairly)
- [15. The base ESP32](#15-the-base-esp32)
- [16. The plan](#16-the-plan)
- [17. What needs Rob](#17-what-needs-rob)
- [Sources](#sources)

---

## 0. Bottom line, and the plan in one table

**Peer to peer is not reachable here, and the reason is not NAT
statistics.** Hole punching needs code at *both* ends, synchronised by a
signalling channel, and the thing at the other end is SyncTERM, PuTTY,
OpenSSH or a C64 through TeensyROM. None of them can punch a hole, none can
be made to, and the board cannot do it on their behalf, because the mapping
has to be created by the machine behind the far NAT. Even if the far end
could, the best current measurement gives **70% ± 7.1% for the punch itself,
conditional on earlier steps succeeding, with about 29% failing before the
punch**, which multiplies out to roughly half of attempts ending direct; and
TCP, which is what SSH is, carries a complexity that the people who do this
professionally describe as possibly wanting kernel changes. So: **mostly
relayed, direct only where the board already has a public path.** Build the
ladder; build no hole punching, now or later.

**On `.onion`: the board cannot run Tor, so a board's onion address has to
be fronted by a box, and that box is best the sysop's own.** Fronted that
way it is the strongest option on both of Rob's words, because the sysop runs
it themselves and it hides the board's address as well as its traffic. The
cost lands on every caller: Tor running locally plus a SOCKS5-capable client,
and a measured median circuit round-trip of **260 to 720 ms** against the
4 ms keystroke echo the bench measures on telnet. So it is a **mode a sysop
chooses**, not the answer for everyone.

**On a `.onion` directory: it is the cheapest, highest-value item in this
whole study, and it is a weekend.** The directory is a Python 3 stdlib server
behind Caddy; Tor fronts it with three lines of `torrc`. The three-face
`Host` header split is **already gone** (`role_for()` returns `"list"`
unconditionally, server.py:238-241), so that worry does not apply. What does
need work is smaller than the Tor configuration and I would not have
predicted it: **absolute URLs are frozen at import time** (`AVATAR_URL`,
`OG_CARD_URL`, `canonical()`, the feed's `site`, the `HOME_URL` 301s), so one
process cannot serve both faces correctly, and **the per-address anti-spam
and rate limiting collapse to a single group when every request arrives from
127.0.0.1 through Tor**. Both have clean answers (§7), and the second one is
mostly moot because of the next point.

**And the correction I was asked to confirm or deny: yes, boards announce
over clearnet and people read over `.onion`.** A board cannot POST to a
`.onion` because it cannot run a Tor client (§6.1), so a `.onion`-only
directory could never receive a heartbeat. The hidden service is a **read
face for humans**, and that is worth having on its own merits: browsing a BBS
directory reveals what you are into, to your ISP and to the directory, and
the onion face removes both. It does **not** hide any board: a listing is
published by its sysop's choice, on both faces alike.

### The plan, ordered by value for effort

| # | Phase | Repo | Days | What it buys |
|---|---|---|---|---|
| **A** | **A `.onion` read face for the directory** | directory, site | **2-3** | Private browsing of the list. Freedom-forward, no firmware, no board changes, nothing to decide |
| **B** | **The onion recipe for a board, proved end to end** | firmware (one label), site | **1-2** | CGNAT-proof reachability with **no firmware at all**; measures the latency that decides §6 |
| **C** | **Port mapping: NAT-PMP, PCP, then UPnP** | firmware | **3-5** | Removes the router menu for the majority. No middle at all. The purest option on both of Rob's words |
| **D** | **IPv6 on the S3** | firmware, directory | **2-3** | Solves CGNAT outright where the ISP provides it, still direct, no middle |
| **E** | **The host-key fingerprint in the listing** | firmware, directory | **3-5** | Closes the one real impersonation hole, for the broker **and** for browser SSH |
| **F** | **CALLIN broker: board side + the directory's broker** | firmware, new repo | **15-20** | Reach for the caller who will not run Tor and the sysop who cannot forward |
| **G** | **A private `.onion` directory instance** | directory | **3-5** | Unlisted boards, visible only to people who know the address. Needs F or B first to be useful |
| — | Hole punching, WireGuard, Tailscale, I2P, Yggdrasil, cjdns, Tor in the firmware | — | — | **Not built.** Reasons in §2 and §8 |

**A and B together are under a week and need almost no code.** F is the only
item measured in weeks, and A through E are all worth shipping whether or not
F ever happens.

**The single best architectural finding:** `LINK.md` already reserves
**family 3, CALLIN**, for "inbound caller sessions, a station bringing a
caller in", and peer kind **3, `gateway`**, for "a station that brings callers
in over family 3". The broker is that, with a TCP transport instead of
ESP-NOW, and the pairing, key schedule and AES-128-CCM sealing are
`linkcrypto.*` unchanged. The protocol has a slot for this already.

**Second best, and it answers "without a website" exactly:** the rendezvous
is **the announce reply**. The board already POSTs about 1.4 KB of plain HTTP
every few minutes, non-blocking, from `tick()`, with DNS on the runner
(`src/plugins/announce.cpp`, `kBodyMax` 1368). A response header naming a
broker host, port and short-lived ticket is the whole of the discovery
protocol, and the **existing announce token is already the board's
credential** to the directory, so brokering through the directory needs no
new pairing at all.

---

## 1. What was checked, and what I could not

Read against firmware `main` at cb42511 (`BBS_VERSION` 1.2.1), directory
`main` 2.0.16. Nothing was changed. Every figure is either read out of these
trees or fetched today and cited.

**Tooling note, because the rule says to report it:** the guard refused
`curl` to `metrics.torproject.org` ("research goes through WebFetch; tests
stay on 127.0.0.1"). The Tor latency figures came through WebFetch instead.

**What I could not establish, and nobody should act as if I had:**

- **No current measured figure for what fraction of home routers have UPnP
  or NAT-PMP enabled.** The honest state of the art is Tailscale's "you
  can't rely on these protocols being present". I found no survey. This
  decides how much phase C is worth, and phase C can answer it by
  instrumenting the attempt.
- **No current CGNAT prevalence figure.** The best measurements are 2016 and
  2017 (17-18% of eyeball ASes; 10% of ~60 ISPs tested; >90% of cellular
  ASes). A 2025 vendor market report claims 68% of Tier-1 ISPs run some
  large-scale NAT; that is not a measurement and I am not leaning on it.
- **Whether SyncTERM can be pointed through a SOCKS5 proxy.** Not documented
  either way that I could find. Load-bearing for §6.3 and one download from
  settled.
- **Minitor's real requirements.** Its project page says "at least 70KB of
  memory and a filesystem with at least 500MB of storage"; the storage figure
  is not credible for a consensus cache and reads like a typo. Quoted, not
  relied on.
- **Whether `esp_wireguard` builds on ESP-IDF 5.3.1 or on an S3.** Its
  component metadata declares neither (§8.1).
- **Brokered keystroke latency.** Two internet legs instead of one, but I
  have no measurement and will not invent one. Phase F measures it.
- **Whether two processes sharing the directory's SQLite file are
  comfortable in practice.** `db()` sets `journal_mode=WAL` with a 10 s
  busy timeout (server.py:610-614), which is the right configuration for it,
  and `settle()` is called on reads as well as writes (server.py:3212, 3400,
  3462, 4549). So it should be fine and it is cheap to measure. §7.4 gives
  the alternative if it is not.

---

## 2. Is peer to peer reachable on this hardware?

### 2.1 The argument that ends it, before any NAT behaviour

A direct TCP path between two unforwarded home networks needs, at minimum:

1. a signalling channel both ends already trust, to swap candidate addresses
   and agree a moment;
2. both ends to emit packets at that moment, each creating an outbound
   mapping on its own NAT that the other's inbound packet can ride;
3. for TCP, a simultaneous-open or a listen-and-connect race, and a stack
   that tolerates what comes back.

**Step 2 needs software at the caller's end that intends to do it.** PuTTY
does not. OpenSSH does not. SyncTERM does not. A C64 through TeensyROM could
not if it wanted to. And there is no way for the board to do steps 2 and 3 on
a client's behalf, because the mapping must be made by the machine behind the
far NAT.

So peer to peer here is not "unreliable", it is **unavailable** for every
caller using software the project did not write. That finding is not improved
by better NAT handling on the board.

The one exception, worth naming because it is already on the roadmap: the
**browser-SSH page** is software the project does write, and a browser can do
UDP hole punching through WebRTC. A WebRTC data channel carrying SSH is a
genuine peer-to-peer path. It would want an ICE agent, DTLS and SCTP on the
board, which is far larger than anything in this study, and it would still
need STUN and a TURN fallback, which is a middle again. Noted and set aside.

### 2.2 And if the far end could, the numbers still say relay

The best current measurement is a campaign over DCUtR in IPFS: **6.25 million
hole-punch attempts, 4.43 million attributable to a single client network,
across 859 client networks and 86,769 remote peer networks, clients in 39
countries and peers in 167.**

- **70% ± 7.1%** success for the hole-punch stage, *conditional on relay
  reservation and public address discovery having succeeded*.
- About **29%** of attempts fail at those prerequisite stages.
- **TCP and QUIC are statistically indistinguishable, both near 70%**, which
  overturns the usual assumption that UDP is materially better.
- **97.6%** of successes landed on the first attempt.
- The authors explicitly could **not** classify remote NAT types, so they
  cannot attribute failures to symmetric NAT or CGNAT. Anyone quoting a
  per-NAT-type breakdown from this paper is overreading it.

My arithmetic on their numbers, flagged as mine: 0.71 × 0.70 ≈ **50%
end-to-end**. Half of attempts end relayed in a system purpose-built for
this, with matching software at both ends and a signalling network already in
place.

The older figure people still quote, 88% TCP success, came from 93 home NATs
in 2005: a tenth of the statistical power and a fifth of the age.

### 2.3 What defeats it

- **Symmetric NAT** (address-and-port-dependent mapping, in RFC 4787's
  terms): the mapping the far end would aim at is not the one it can learn,
  because a new destination gets a new port. Tailscale's treatment of two
  "hard" NATs is the honest one: with the birthday-paradox trick, 50% after
  about 54,000 packets, which is nine minutes, and 99.9% at about 28 minutes.
  Not a BBS dialling experience.
- **CGNAT**: there is no mapping the sysop can create at all, because the
  public address is shared and the CPE's WAN side is itself private. This is
  the case that makes port forwarding impossible rather than merely awkward,
  and it is the case this whole study exists for.
- **TCP specifically**: Tailscale, who do this for a living, write "You *can*
  do NAT traversal with TCP, but it adds another layer of complexity to an
  already quite complex problem, and may even require kernel
  customizations." On ESP-IDF 5.3.1 the kernel in question is lwIP, and
  simultaneous-open behaviour there is not a thing to discover during a
  release.

**Verdict: do not build hole punching.** Spend the effort on the ladder.

---

## 3. The ladder

What a board tries, in order. The structural point: **this resolves at the
board, not during a call.** A caller sees a listing that already says how to
dial, so there is no "what the caller sees while it resolves" — the UX is the
sysop's.

| Rung | What | Who is in the middle | Works under CGNAT |
|---|---|---|---|
| 0 | A public address: the board is on a public IP, or in a DMZ | nobody | n/a |
| 1 | A forwarded port the sysop set by hand | nobody | **no** |
| 2 | An automatic mapping: NAT-PMP, PCP, then UPnP IGD | nobody | **no** |
| 3 | IPv6, global address, CPE passing inbound | nobody | **yes** (CGNAT is IPv4-only) |
| 4a | **An onion front run by the sysop** | nobody at all, in the simple case | **yes** |
| 4b | **A CALLIN broker**, SSH ciphertext only | one broker operator, chosen | **yes** |
| — | A direct hole punch | — | **not built** (§2) |

Rungs 2 and 3 are in that order deliberately: **NAT-PMP and PCP before
UPnP**, because they are a two-packet UDP exchange with the default gateway
rather than SSDP multicast plus SOAP, and because PCP was designed with
carrier NAT in mind even if ISPs essentially never expose it. UPnP is the
fallback because it is the one most likely to be present.

### 3.1 How the board knows: the one problem nobody has solved

**Today nothing tells a sysop whether their forwarded port actually works.**
The board learns the address it was seen from, because announce reads
`X-Seen-Address` out of the reply (server.py:4483), but that proves outbound
worked, not inbound.

**And now that the directory may be the broker, the directory still must not
be the prober.** Its README is explicit: "a directory that connects to
whatever host and port a stranger posts to it is a port scanner with a public
API". That promise survives brokering intact — **a broker only ever accepts
connections, never initiates them** — and it would not survive probing. So
the two roles must stay apart, which is a reason to keep the broker a
separate program on the droplet rather than a route inside `server.py`
(§5.4). Three honest candidates for the prober:

- **the sysop**, from a phone on mobile data. What everybody does today, and
  the site should document it better regardless;
- **a third-party broker** a board paired with deliberately, probing a port
  that board named. A consented connection to a known peer, not an open
  proxy;
- **a separate tiny check service**: a fourth thing to run and trust. Not
  recommended.

Until one exists, the board says what it *believes* and says that it is a
belief, which is the same honesty pattern as the "as of" free-space figures
in `src/core/space.*`.

### 3.2 What the sysop sees

One page, showing the live rung and why each lower one is not it:

```
Reachable  Through broker "unleashedbbs.net"
Port 6422  not reachable from outside (checked 14:02)
Mapping    router refused (no NAT-PMP, no UPnP)
IPv6       no global address
Onion      off
```

At 40 columns the same rows with nine-character labels, per the settled 40/80
rule. S3-only, so not compiled into a base build at all; if any of it becomes
a shared sysop setting the greyed-row-plus-upgrade-note convention applies,
strings only.

---

## 4. "Without a website": the rendezvous is an extension of announce

Rob's constraint is binding, and taken strictly it rules out any board-side
design needing HTTP**S**, TLS, a WebSocket or a browser. It does **not** rule
out plain HTTP, because the board already does that: announce is a plain
POST, about 1.4 KB of body (`kBodyMax` 1368), written with `snprintf` into a
static buffer, driven non-blocking from `tick()`, with DNS resolved on the
runner and the last good address cached. That machinery is five releases old
and rock solid by deliberate effort (1.1.2's reliability suite).

### 4.1 Discovery: one response header, no new stack

The board already reads headers out of the announce reply: `X-Seen-Address`,
`X-Listing-Token`, `X-Listing-State`, `X-Listing-Public-In`. Adding

```
X-Broker: broker.unleashedbbs.net:6490
X-Broker-Ticket: <32 bytes, base64, valid 10 minutes>
X-Broker-Port: 7314
```

gives the board everything it needs: where to dial, how to prove it is
itself, and the port its callers will be told to use. **No new protocol, no
new stack, no discovery mechanism, no second DNS name to resolve if the
broker shares the directory's host.** The board refreshes the ticket on every
heartbeat it already sends.

For brokering through the directory, **the announce token is already the
credential**, so there is no pairing step at all: the sysop switches
brokering on in `CONFIG announce` and the next heartbeat carries it. For a
third-party broker, the sysop pairs it the way the link pairs a sat (§5.5).

This should go in `PROTOCOL.md` as optional headers, so any directory can
offer brokering and a board that does not understand them is unaffected. That
keeps the protocol's existing promise: a directory ignores fields it does not
know, and so does a board.

### 4.2 The transport: plain TCP, and the ciphertext rule makes it cheap

The board dials one TCP connection to the broker and holds it, seals a HELLO
carrying the ticket, and from then on the broker tells it when a caller has
arrived. Each caller is a second outbound TCP connection on which the board
forwards its SSH socket's bytes verbatim.

**Because the payload is already SSH ciphertext, the transport needs no TLS
and no encryption of its own beyond authenticating the control
connection.** On a WROOM a TLS session is about 40 KB of heap by this
project's own figure, which is why announce is plain HTTP in the first place.
Here it is zero. **The ciphertext rule is not only the safety property, it is
what makes the whole thing affordable.**

### 4.3 Two consumers, and only one of them is this

| Consumer | Wants | Where it belongs |
|---|---|---|
| A real SSH client (OpenSSH, PuTTY, SyncTERM, IcyTERM) | a **TCP** rendezvous: a host and port it can connect to | **This design.** The board needs no website |
| A browser | a **WebSocket** to a relay that speaks SSH onward | **`internal/plan-web-ssh-2026-10-04.md`**, already planned, already decided to live on `unleashed_site` rather than the directory. **Not this** |

They are separated here on purpose, because conflating them is how the board
side would acquire a WebSocket stack it must not have. The pleasant
consequence is that the browser relay and the TCP broker are the same program
shape, a stdlib asyncio byte mover, so they could be **one program with two
front doors** on the droplet, with the board side never knowing the browser
door exists.

### 4.4 How a caller reaches a brokered board, and the one wart

SSH has no clear-text host indication before the handshake, nothing like
TLS's SNI, so **one broker port cannot tell which board a caller wants.**
The options are:

- **a port per board on the broker.** The listing shows
  `ssh -p 7314 broker.unleashedbbs.net`. Works with every client unchanged;
- a preamble the caller sends first: needs a custom client, so it fails the
  "works with PuTTY" test;
- the broker speaking SSH and proxying: breaks the ciphertext rule outright.

**So: a port per board.** Ports are free (there are 64k of them) and the
board's port must be **stable**, stored in the broker's table, or a listing
would churn on every reconnect. The wart is cosmetic: the listing shows a
port that is not 6422. The directory already shows `host` and `port`
separately and already renders an `ssh://` line with a padlock, so this costs
no layout work.

---

## 5. The repeater: shape, name, and the directory as one

### 5.1 It is not a sat, and should not be called one

`satwords.h` is specific: a **sat** is a box on the µnleashed link, reached
over ESP-NOW or a serial line; an **orbiter** feeds the board data; a **door
sat** is one a caller goes into, reached with **UPLINK**. This is none of
those. It is not in radio range, it does not pair over the air, it feeds the
board nothing, and a caller does not go into it: a caller comes *through* it.
Calling it a sat would overload a word that already means something precise,
which is exactly the mistake "bulletin" cost several sittings.

"relay" is also taken: `CLAUDE.md` lists relay as a future sat kind, "one
hop, not a mesh". `BEACON` is a link frame name. `tower` is the S3 panel's
announce glyph.

### 5.2 Recommended name: **repeater**

A repeater sits somewhere with reach, you transmit to it, and it carries you
to people who cannot hear you directly. It is a ham term, which Rob reads
natively, it is ASCII so a C64 can print it, and it carries the right
expectations with no explanation:

- **repeaters are plural and nobody owns "the" repeater.** Clubs run them,
  there are thousands, you pick one, and picking one is normal rather than a
  compromise. Exactly the shape the values need (§13);
- **a repeater operator hears everything you transmit**, and every ham knows
  it. That makes the trust story intuitive instead of a disclaimer;
- **nothing in the tree uses the word.** Checked: no hit for `repeater` in
  `src/`, `tools/` or `host/`.

Falling out of it: `REPEATER` (staff, shows which is carrying callers),
`CONFIG repeater`, and in the board's voice `--> Callers are reaching you
through "unleashedbbs.net".` One word covers both mechanisms: an **onion
repeater** and a **broker repeater** are one role with two mechanisms, which
keeps the ladder one concept. Words go in `satwords.h`, or a sibling
`reachwords.h` if Rob would rather keep sat vocabulary clean.

### 5.3 What it runs on, and why it cannot be an ESP32

**An ESP32 cannot be a repeater for other people**, and this is worth saying
because the project's instinct is to reach for one. A repeater needs a public
address, which is the one thing a board behind NAT does not have; it must
hold one control connection plus up to ten data connections per board; and it
is pointless unless it is up more than the boards it serves.

**This does not contradict the core value.** BBS logic stays on the
microcontroller; a repeater moves bytes and makes no decision about accounts,
screens, policy or who may log in. It cannot: it only ever sees ciphertext.
That is the same line the settled "space station" pattern draws, a computer
for one big job and never for BBS logic.

| | Broker repeater | Onion repeater |
|---|---|---|
| Where | the directory's droplet, or any machine with a reachable address | **the sysop's own LAN** |
| Public address | required | **not required** |
| Software | a small stdlib asyncio program | `tor`, three lines of `torrc` |
| Hardware | a VPS, or the droplet that is already there | a Pi, a NAS, OpenWrt, or the PC that is on anyway |
| Cost | $4-6 a month, or nothing extra on the droplet | nothing |
| Who can run one | anybody | **the sysop** |

### 5.4 The directory as the broker

Rob is content with this, so it is not argued, only specified.

**What makes it fit:**

- **the promise survives.** A broker only accepts connections; it never
  initiates one. The README's "it never makes outbound connections" stays
  literally true, provided the prober role stays elsewhere (§3.1);
- **the credential already exists.** The announce token identifies a board
  to the directory and nothing new is needed;
- **the eligibility rule is already enforced.** "Boards listed in the
  directory" is a state the database already keeps, so a queued, held or
  expired board simply gets no broker headers;
- **the listing is where a caller looks anyway**, so the brokered dial line
  appears exactly where the direct one does.

**What must stay apart, and this is the one firm recommendation in this
section: the broker is a separate program and a separate process from
`server.py`.** Three reasons, in order of weight:

1. **`selftest.py` asserts invariants that a byte-moving service would
    strain.** The page invariant (selftest.py:3745-3769) pins the only script
    on any page to one inline string that never fetches, stores or opens a
    WebSocket. A broker is not a page, so it does not break that directly,
    but putting a long-lived socket pump inside the same single-threaded
    `ThreadingHTTPServer` process would couple the list's availability to the
    broker's load. The browser-SSH plan reached the same conclusion for the
    same reason and put the terminal on `unleashed_site`;
2. **the prober must not be the directory** (§3.1), and keeping the programs
    apart is what lets a third-party repeater probe while the directory never
    does;
3. **replaceability.** A separate program with a published protocol is one
    anybody can run; a route inside `server.py` is one only a directory
    operator can.

So: a new repository, GPL, stdlib asyncio, reading the directory's SQLite
read-only to answer "is this token a listed board", or better, told by
`server.py` through a tiny local handoff so it needs no database access at
all. The second is cleaner and should be the design.

### 5.5 How a board is paired to a third-party repeater

Reuse the link's pairing wholesale. `LINK.md` specifies commit-then-reveal
ECDH over P-256, HKDF-SHA256 to a 16-byte `k_link` plus a 4-digit
confirmation code, and AES-128-CCM with the header as associated data. The
pieces are mbedTLS code the WROOM image already links for WPA3, and
`linkcrypto` seals and opens in **78 µs** on an S3 in the real build.

Over TCP the exchange is easier, not harder: no broadcast, no channel
hunting, no 250-byte frame limit. Pairings live beside the link's in
`<userdata>/p/link/peers` with kind 3 (`gateway`), temp file and rename,
**not in the backup zip**, exactly as `wifi.last` and the link's pairings
already are. Eight pairings is the link's cap and is far more than anyone
needs; two or three repeaters is a sensible board.

---

## 6. The `.onion` path for a board

Rob has said `.onion` is an example rather than a requirement, so this section
is no longer "is Tor the answer" but "what does reusing this existing
framework actually cost and buy".

### 6.1 Tor on the chip itself: no, and here is the number

A Tor client is not small. To build a three-hop circuit it needs:

- the **directory consensus**, signed by hardcoded authorities, plus the
  microdescriptors it references. Proposal 158's entire purpose was to reduce
  bootstrap "from at least 500 kilobytes to 100 kilobytes", so **100 KB is
  the floor** for what must be fetched, verified and cached;
- **TLS to every relay it talks to.** That is the wall: this project's own
  figure for a TLS session on a WROOM is **about 40 KB of heap**;
- ed25519 including Tor's expanded keys, curve25519, AES-CTR, SHA-1,
  SHA-256, and the ntor handshake;
- a filesystem for the cache and **a correct clock**, because consensus
  validity is time-bounded.

**Implementations exist, and neither is a dependency to ship:**

- **Minitor** (jpbland1), "an embedded version of Tor for the esp32", C,
  **GPL-2.0, 21 stars, 1 open issue, last push 2023-09-01**, not archived.
  It both hosts and connects to onion services. It needs ESP-IDF, a
  filesystem (its example uses an SD card), NTP, and **a fork of wolfSSL**,
  because "wolfSSL doesn't support expanded ed25519 keys that Tor requires".
  Its documentation says the consensus fetch "usually takes around 300
  seconds" and that "starting the Onion Service may take several minutes on
  the esp32".
- **toresp32** (briand-hub), a C++17 Tor proxy for the ESP32. State not
  verified; not counted.

**Verdict, per board, with the number:**

- **Base ESP32-WROOM:** out of the question and not by a little. Free static
  DRAM at 1.2.0-link.12 is **15,632 bytes** on the WROOM and **2,824 bytes**
  on the ESP32-CAM; one TLS session is ~40 KB of heap before any cache. Moot
  anyway: no SSH there.
- **ESP32-S3 with PSRAM:** arithmetically possible, and Minitor proves it has
  been done. The wall there is **not RAM, it is maintenance**: a 21-star,
  three-years-stale project with a forked TLS library, carrying the anonymity
  property that is the entire point. Tor getting subtly wrong does not fail
  loudly, it quietly stops being anonymous.

**So: no Tor client in the firmware.** Not a size decision, a responsibility
one. Which means an onion address for a board must be fronted by a box, and
this is the structural reason the onion path and the broker path end up the
same shape: **both are a box in front of the board**, differing in who runs
it and what it hides.

### 6.2 An onion service fronted by a box: it composes exactly as described

```
caller's machine                 the sysop's own LAN
+-------------+                  +-----------------+     +---------+
| ssh client  |                  | tor daemon      |     | board   |
|   + tor     |== Tor circuit ==>| onion service   |---->| ssh     |
|   (SOCKS5)  |   (3+3 hops)     | HiddenServicePort     | :6422   |
+-------------+                  +-----------------+     +---------+
      |                                   |                   |
      |<--------------- SSH, end to end, through all of it --->|
```

- `HiddenServiceDir /var/lib/tor/bbs/` and
  `HiddenServicePort 22 192.168.0.42:6422` is the whole configuration. An
  onion service carries an **arbitrary TCP stream**, SSH is an ordinary use,
  and the Tor Project documents onion services for exactly this kind of
  self-hosting. There is **no exit node**: traffic never leaves the Tor
  network and the rendezvous point sees only encrypted cells.
- **The layering stacks the right way.** Tor hides *where* the board is, from
  the caller included. SSH hides *what is said*, from the Tor network and
  from the daemon's operator. Whoever runs the daemon terminates the onion
  layer and therefore sees the SSH ciphertext and the board's LAN address;
  they cannot read a session, cannot log in, and cannot impersonate the board
  if its fingerprint is published (§10.3).
- **In the simple case there is no operator at all**, because the sysop runs
  the daemon on their own network. Nothing else on this list achieves that.
- **The board needs no firmware change.** It listens on its LAN as it
  already does. The only touch is the listing: `CONFIG announce`'s `host` row
  is `PS_TEXT` at 95 characters (announce.cpp:1498) and the directory stores
  `host` with `tidy(..., 80)` and **no hostname validation at all**
  (server.py:1226). A v3 onion address is 62 characters. **It already fits
  both sides.** The row's label says "DNS name", which an onion address is
  not; a one-word fix.
- **Private and unlisted is available.** v3 client authorization makes the
  service refuse anyone without a key: an `authorized_clients/` directory
  under the service directory holding `descriptor:x25519:<base32 pubkey>` per
  client, and `ClientOnionAuthDir` on the caller's side.
  `HiddenServiceAuthorizeClient` is **v2 only** and must not be used. That
  makes "a board for five friends, unlisted, unreachable by anyone else" a
  real configuration, and it is what phase G would list.

### 6.3 What it asks of a caller

**Getting Tor running:**

| OS | What | Steps |
|---|---|---|
| Windows | Tor Browser (SOCKS5 on 127.0.0.1:9150 while open), or the Tor Expert Bundle as a service on 9050 | 1 install, 1 "leave it open" |
| macOS | `brew install tor` then `tor`, or Tor Browser | 2 |
| Linux | `sudo apt install -y tor`; runs as a service on 9050 | 1 |

**Pointing a client through it:**

| Client | Can it? | How |
|---|---|---|
| **OpenSSH** | **yes** | `ssh -o ProxyCommand='nc -X 5 -x 127.0.0.1:9050 %h %p' bbs@xxxx.onion`, or one `Host` block in `~/.ssh/config`, or `torsocks ssh` |
| **PuTTY** | **yes, out of the box** | Proxy pane, SOCKS 5, 127.0.0.1:9050. Its "Do DNS name lookup at proxy end" defaults to Auto, and Auto passes hostnames straight to a SOCKS5 proxy, which is what makes `.onion` resolve |
| **IcyTERM** | **yes, and it is built for this** | Per dialing-directory entry: **Tor (127.0.0.1:9050)**, I2P, or custom SOCKS5. Covers Telnet, Raw, RLogin and SSH; "the target hostname is resolved by the proxy (remote DNS), which is what makes `.onion` and `.i2p` addresses reachable". MIT/Apache-2.0, Windows 10+, macOS 10.14+, Linux |
| **SyncTERM** | **unknown** | Not documented either way that I could find. Matters, because SyncTERM is what the site recommends |
| **A C64 via TeensyROM** | **no** | No SOCKS, no Tor, no SSH. Already excluded from any encrypted path, so not a new exclusion |

**Is it harder than forwarding a port?** Two answers, pointing opposite ways,
and that split is the real finding:

- **for the sysop it is much easier**: two lines in a config file, no router
  menu at all, and it works where forwarding cannot;
- **for the caller it is harder**, and it is harder for *every* caller.
  Forwarding is one step one person takes once; reaching an onion is one step
  everybody takes.

So the test "if it is harder than forwarding a port it solves nothing for the
people who cannot forward a port" comes out **split**: it emphatically solves
the sysop's problem, including the CGNAT case nothing else solves without a
middle, and it adds a barrier for callers. Hence a **mode**, not the answer:
the sysop who cannot be reached any other way, or who wants their address
hidden, turns it on and accepts that callers install Tor.

### 6.4 The latency, which nobody should discover after shipping

Tor OnionPerf's published circuit round-trip latencies for onion measurements,
September 2026, by vantage point: medians around **260-340 ms** (Germany),
**420-500 ms** (US) and **650-720 ms** (Hong Kong), quartiles ±100-150 ms.

Against that, the bench measured keystroke echo on telnet at **p50 4 ms, p95
12 ms** with five callers on. The board echoes in character mode, so every
keystroke round-trips.

An onion caller therefore types with a third to three quarters of a second of
echo lag. That does **not** violate Rule no. 1, which is about the board's
loop and not the link, and the board still satisfies it. But it is a real
experience cost, and the honest framing is that it feels like a long-distance
call in 1988: some of this audience will find that authentic and others
unusable. Phase B measures it on a real board, which is the point of phase B.

---

## 7. A `.onion` directory

This is the cheapest high-value item in the study, and the surprises are not
where I expected them.

### 7.1 Confirmed: boards announce over clearnet, people read over `.onion`

Rob's reading is **correct**. A board cannot POST to a `.onion` because it
cannot run a Tor client (§6.1), and nothing else could do it for the board
without becoming another box in front of it. So:

- **a `.onion`-only directory could never receive a heartbeat.** The hidden
  service is a **read face for humans browsing**, not a transport for boards;
- **what a sysop or a reader gains:** browsing a BBS directory reveals what
  you are into, to your ISP and to whoever runs the directory. The onion face
  removes both. That is a genuine and non-trivial privacy gain, and it is the
  leak that actually matters here;
- **what nobody gains:** a board's own address is still published, by its
  sysop's choice, on both faces alike. An onion face hides *readers*, not
  *boards*. The site copy must say exactly that, because the natural
  assumption is the opposite.

There is one tidy exception worth knowing: a board whose listed `host` is
itself an onion address (§6.2) is a board whose *location* is not published
on either face. So the two features compose into something real — "an
unlisted-location board, listed in a directory you reach privately" — without
either being designed for it.

### 7.2 What the Tor side costs: three lines

```
HiddenServiceDir /var/lib/tor/unleashed-directory/
HiddenServiceVersion 3
HiddenServicePort 80 127.0.0.1:8081
```

The directory already listens on loopback (`DIRECTORY_HOST=127.0.0.1`,
`DIRECTORY_PORT=8080`) with Caddy in front, so Tor fronting a second
loopback port is exactly the same shape. No TLS: an onion address is already
authenticated and encrypted by its own key, so plain HTTP inside the circuit
is correct, not a compromise. **`server.py` needs no change for this part.**

### 7.3 What actually needs work, and it is not the Tor config

**Confirmed good news first.** The thing my brief most expected to bite does
not: the three-domain `Host` header split **is already gone**. `role_for()`
returns `"list"` unconditionally with a comment saying so (server.py:238-241,
"One, since the split: the directory"). So there is no face selection to
confuse.

**Problem 1: absolute URLs are frozen at import time.** These are computed
once, when the module loads, from `DIRECTORY_LIST_DOMAIN` or
`DIRECTORY_URL`:

- `AVATAR_URL` (server.py:1429) and `OG_CARD_URL` (server.py:1433), both
  substituted into the `PAGE` template at import;
- `canonical()` (server.py:4466-4473), which fills `@CANONICAL@` and the
  `og:url` on every page;
- the feed's `site` (server.py:3398);
- `site_url("home", ...)` and the `HOME_URL` 301s (server.py:258-259, 4616-4624),
  which send `/install`, `/static/`, `/pix/` and unknown paths to
  `https://unleashedbbs.com`.

**Consequence: one process cannot serve both faces correctly.** A reader on
the onion face would be handed a `rel=canonical` and an `og:url` pointing at
the clearnet domain, and a 301 to the clearnet `.com` for `/install`. The
canonical link is not fetched by a browser, so this is a correctness and
tidiness problem rather than an active leak, but **the 301 is an active one**:
click the wrong link on the onion face and Tor Browser goes to the clearnet
site. For a feature whose whole purpose is privacy that is not acceptable.

**The fix is a second process, and it is cheap**, because every setting is
already an environment variable and the unit file says so in as many words
("Settings live here, so the code needs no deployment-specific edits"):

```
Environment=DIRECTORY_PORT=8081
Environment=DIRECTORY_URL=http://<56 chars>.onion
Environment=DIRECTORY_LIST_DOMAIN=
Environment=DIRECTORY_HOME_URL=
```

`DIRECTORY_HOME_URL` empty is already a documented configuration ("Empty it
for a directory of your own"), and with it empty those paths are simply not
found rather than redirected off-network. So the onion face is **the same
code, a second unit, a different environment**: `unleashed-directory-onion.service`
beside the existing one. No code change at all for correctness; one small
change for politeness, which is to make the 301s relative or absent when
`HOME_URL` is empty, which they already are.

**Problem 2: every request through Tor arrives from 127.0.0.1, which
collapses the per-address rules.** `group_of()` returns the address itself
for IPv4 (server.py:738-748), and the anti-spam and rate limiting key on it:
`DIRECTORY_PER_ADDRESS=1`, `PER_ADDRESS + SPARE_ROWS` (4) listings held per
address, `DIRECTORY_ADDRESS_PER_MINUTE=20` accepted announces per address per
minute, and `DIRECTORY_MIN_SECONDS=30` per board. Also `is_trusted(peer)`
governs whether `X-Forwarded-For` is believed, and loopback is the whole
trusted list.

Two things save this and one of them is decisive:

- **decisive: `/announce` is a POST, and the onion face does not need to
  serve it at all.** Since boards cannot announce over Tor anyway (§7.1), the
  onion instance should **refuse `/announce` outright**. Then no per-address
  rule is exercised on that face, nothing collapses, and the hidden service
  carries only GETs. That turns the hardest-looking problem into a four-line
  guard, and it is the right design regardless, because an `/announce` route
  on an onion face could only ever be abuse;
- for the GET side, the only address-keyed behaviour left is the
  `X-Seen-Address` header on every reply (server.py:4483), which on the onion
  face would say `127.0.0.1` to every reader. Harmless, but it should be
  omitted on that face rather than say something false.

**Recommendation:** a `DIRECTORY_READ_ONLY=1` environment flag that (a)
refuses `/announce` with 404, (b) omits `X-Seen-Address`, and optionally (c)
skips `settle()` on reads. That is a small, well-contained change, it is
useful to anyone running a read mirror for any reason, and it is the one code
change phase A needs.

### 7.4 One database or two

`db()` opens SQLite with `journal_mode=WAL` and a 10 s busy timeout
(server.py:610-614). WAL is exactly the right configuration for several
processes on one file: readers do not block the writer and the writer does not
block readers. The wrinkle is that `settle()` **writes** and is called on
reads (server.py:3212, 3400, 3462), so a read-face process is not actually
read-only unless told to skip it — which is item (c) above and is why it is
worth having.

So: **one database, two processes, with the onion face skipping `settle()`.**
The clearnet face is announced-to every few minutes and settles the list
constantly, so the onion face loses nothing by not settling. If measurement
ever shows contention, the fallback is a periodic read-only copy, which is a
`cron` line and loses nothing but freshness.

### 7.5 "A fully private `.onion` host of directory too": the two readings

Rob's sentence can mean two quite different products, and he should pick
rather than be guessed at.

**Reading 1: a private face on the same list.** A hidden service beside the
public site, same database, same boards. What it buys is reader privacy and
nothing else. This is phase A, it is 2-3 days, and it needs no decision.

**Reading 2: a separate instance with its own database, listing boards that
are not on the public list at all.** This is the genuinely interesting one
and a different product: a board announces only to the private directory, so
its existence is known only to people who have the onion address. Combined
with a board whose own dial line is an onion with v3 client authorization,
that is a BBS that is invisible to everyone except an invited circle. Nothing
else in this project offers that, and it is squarely "freedom forward".

What reading 2 costs:

| | Cost |
|---|---|
| Directory | **almost nothing.** A second instance, its own `DIRECTORY_DB`, its own port, its own `torrc` block. `servers` in `CONFIG announce` is already a comma list, so a board can announce to both |
| Firmware | **the one real cost.** A board cannot POST to a `.onion`, so a private directory still needs a **clearnet address to receive heartbeats**, even if its only read face is the onion. So "fully private" is not achievable for the announce path on this hardware |
| The honest limit | the private directory's clearnet announce endpoint is a public fact; anyone who learns that hostname learns the directory exists, though not what is in it, because the list is only served over the onion face |

**So reading 2 is buildable and worth building, with one caveat stated
plainly: the heartbeat path cannot be hidden, only the list can.** A sysop
running one would put the announce endpoint on an unremarkable hostname and
serve the list only over Tor. That is meaningfully private and it is not
anonymous, and the documentation must not claim otherwise.

**Recommendation:** build reading 1 now, as phase A. Document reading 2 as a
configuration of the same code in the same guide, and build phase G only once
somebody wants it, because it needs no new code at all beyond phase A's
read-only flag.

### 7.6 "Freedom forward should someone want a `.onion` directory"

This is a documentation deliverable, and the repository is already in the
right shape for it: GPL, one file, every setting an environment variable, and
a README that already says a list nobody can replace would be the wrong shape.
What somebody needs, and what should go in `INSTALL.md` with an "Applies to
versions" line:

- the three `torrc` lines (§7.2);
- the second systemd unit and the four environment variables that make the
  onion face correct (§7.3);
- `DIRECTORY_READ_ONLY=1` and what it does;
- **the honest limit, up front: boards announce over clearnet.** A Tor-only
  directory receives nothing;
- the warning that `DIRECTORY_HOME_URL` must be empty on the onion face, with
  the reason, because leaving it set is the one mistake that sends a reader
  off-network;
- that the onion address goes in `DIRECTORY_URL` so canonical links and the
  feed are right.

**Changes to make in the code because they make this harder than it should
be**, each small and each worth doing anyway:

1. `DIRECTORY_READ_ONLY` as above;
2. `X-Seen-Address` omitted when the peer is the trusted loopback and no
   forwarding header is present, since it then says nothing true;
3. a line in `INSTALL.md` and the unit file's comments saying which variables
   a second face needs. The unit file is already the best-commented thing in
   the repository, so this is in keeping.

---

## 8. The other overlays, graded by what they provide

Three properties, which the report must not blur:

- **reachability**: can somebody open a connection at all;
- **confidentiality**: can a middle read the session;
- **anonymity**: is *where the board is* hidden.

**A broker carrying SSH gives reachability and confidentiality and no
anonymity at all**, because it sees every address on both sides. That belongs
in those words wherever a broker is described, the site included.

Ranked by Rob's test, "whether a microcontroller can do it":

| Option | Reach | Conf. | Anon. | On an ESP32? | Verdict |
|---|---|---|---|---|---|
| **Port mapping (NAT-PMP/PCP/UPnP)** | yes, where present | n/a (direct) | no | **yes, easily**: a UDP exchange, or <50 KB for UPnP | **Build (phase C).** The purest on both of Rob's words: there is no component at all |
| **IPv6** | yes, where the ISP gives it | n/a (direct) | no | **yes**: `CONFIG_LWIP_IPV6=y`, ~1,888 bytes | **Build (phase D)** |
| **CALLIN broker** | yes | yes (SSH end to end) | no | **yes**: plain TCP, no new crypto | **Build (phase F)** |
| **Tor onion, fronted** | yes | yes | **yes** | **no on the chip**; yes with a box in front | **Build the recipe (phase B)** |
| **WireGuard** | yes | **to the tunnel endpoint only** | no | `esp_wireguard`, with caveats | **Do not build**, §8.1 |
| **Tailscale** | yes | to the tailnet | no | a C3 port exists at ~200-300 KB | **No**: more than a WROOM has, and a coordination server is a middle |
| **I2P** | yes | yes | yes | **no implementation found** | **No** |
| **Yggdrasil** | yes | yes | no | **no port found** (Go; embeddable as a library) | **No** |
| **cjdns** | yes | yes | no | no port found; effectively dormant | **No** |
| **Nostr** | **no** | n/a | n/a | signing is cheap; it carries no TCP | **Not this problem**, §8.2 |

### 8.1 WireGuard, the one that looks right and is not

It deserves the attention: curve25519 plus ChaCha20-Poly1305 is small, there
is an ESP32 implementation, and a dial-out tunnel needs no inbound port.

**What exists:** `esp_wireguard` (trombik), based on the lwIP WireGuard
implementation, **BSD-3-Clause**, latest **0.9.0, published about a year
ago**. Its component metadata declares **ESP-IDF master, v4.2.x, v4.3.x,
v4.4.x and ESP8266 RTOS SDK v3.4**, and targets **esp32, esp32s2, esp32c3,
esp8266**. Note what is absent from both lists: **ESP-IDF 5.3 and the
esp32s3.** Not fatal, but porting to the IDF we pin and the chip we need is a
cost, and I found no published flash or RAM figure for it.

**Why it is still wrong here, and this is decisive.** A WireGuard tunnel is
*terminated* at the far endpoint, so a VPS running WireGuard sees the
plaintext of whatever rides inside it. If telnet rides inside, **the operator
reads every session and every password**, which is strictly worse than Rob's
ciphertext rule. If SSH rides inside, the operator sees ciphertext, which is
exactly what a plain TCP broker already gives, at the price of a second
crypto stack, a second key system, a second pairing story and an unported
library. **WireGuard buys nothing the ciphertext rule does not already buy,
and it offers a tempting way to break it.**

**The one case where it is right:** a sysop who rents their own VPS and wants
the board directly addressable at that VPS's address, as their own
infrastructure. Then the operator is the sysop, the trust question vanishes,
and WireGuard gives a clean public address with no protocol work. A recipe
for the site, not firmware for the project.

### 8.2 Nostr, placed correctly

Nostr relays carry small signed events, so a board could publish its current
dial line, onion address or broker ticket as a signed event readable from any
relay. That is a **distributed alternative to the directory's address line**,
and on "distributed" it scores very well: no single relay matters and anyone
can run one.

But it carries no TCP, so it solves **discovery, not reachability**. It
belongs in a later conversation about whether the directory should have a
gossip alternative, next to the routing-middleware and board-linking items
already queued. Not in this ladder.

---

## 9. What each option hides, and from whom

Rob's words are "privacy and freedom forward", which may be ethos as much as
threat model, so here is the grid rather than a slogan.

| Who | Forwarded port today | Broker (SSH only) | Onion front, sysop's own | Onion directory face |
|---|---|---|---|---|
| **the middle's operator** | n/a | both addresses, timing, byte counts, durations; **not one byte of the session** | n/a (the sysop is the operator) | n/a |
| **the caller's ISP** | the board's address, and that SSH is in use | the broker's address, not the board's | that Tor is in use, nothing about the board | **that Tor is in use, not what was browsed** |
| **the board's ISP** | inbound SSH from each caller | one long outbound connection | Tor traffic | n/a |
| **a passive observer** | addresses and timing | addresses and timing, in two legs | neither end learns the other | nothing |
| **the directory operator** | the board's address (published) | the same, plus who is brokered when | the onion address only | **not who read what** |
| **anyone reading the list** | **the board's address, published** | the broker's address and port | **the onion address only** | the same list either way |

### 9.1 What anonymity can even mean for a listed board

**A board listed in a public directory with its address on it has already
published where it is.** That is the function of a listing and no overlay can
un-publish it. So:

- a board on a forwarded port has **no** location privacy, by design, and the
  directory is where it was given up;
- a board behind a broker has **its address** private and **its existence**
  public, with the broker's operator holding the link between them;
- a board on an onion address has **its address private and its existence
  public**, with nobody holding the link. The only configuration here that
  offers it;
- a board on an onion address with **v3 client authorization and no public
  listing** — optionally listed on a private onion directory (§7.5) — has its
  existence private too, reachable only by people the sysop handed a key to.
  That is a genuinely different product: a board for a circle rather than for
  the public.

**Is onion-only-and-unlisted a mode the firmware should support?** Yes, and it
costs almost nothing: `announce` off, or pointed at a private directory, plus
an onion address nobody publishes. The board does not need to know. What it
*should* get is one line in `CONFIG` or the setup flow telling the sysop this
is possible, because nobody will discover it alone. Copy, not code.

---

## 10. The trust story

### 10.1 What SSH-only buys, demonstrated rather than asserted

"The middle cannot read a session" is true if and only if:

1. **the middle carries nothing but the SSH byte stream.** So the board must
   **refuse** to broker its telnet port, not merely default against it. One
   brokered port, enforced on the board where a broker cannot argue with it.
   A `CONFIG` row letting a sysop broker telnet would destroy the property on
   the boards that most need it;
2. **the caller verifies the board's host key**, or the middle can
   impersonate it (§10.3);
3. **the middle cannot make the board speak plaintext.** It cannot: the
   board's SSH is wolfSSH's and the broker is just a socket to it, exactly as
   an internet TCP connection is today.

Point 1 has a pleasant consequence already noted: because the payload is
ciphertext, **the broker protocol needs no TLS**. On a WROOM that would have
been ~40 KB of heap; here it is zero.

### 10.2 What a malicious broker operator can do

| Attack | Can they? | What stops it |
|---|---|---|
| Read a session | **no** | SSH end to end. The whole point |
| See who calls, when, and how much | **yes** | nothing. Say so plainly on the site |
| Refuse to carry a caller | **yes** | several brokers per board; the board notices and says so |
| Delay or throttle | **yes** | the board times its own keepalives and can report "slow" |
| Drop the board off quietly | **yes** | the board treats it as the ladder failing and falls back |
| **Impersonate the board to a caller who has never seen its host key** | **yes** | §10.3 |
| Inject into a live session | **no** | SSH integrity |
| Log in as a caller | **no** | it never sees a password |
| Use the board as a proxy | **no** | the board brokers only its own SSH port |

### 10.3 Host-key trust, the one real hole, and what the directory is then trusted for

A caller who knows the fingerprint is safe: SSH refuses a changed key. A
**first-time** caller has nothing to compare, and a broker is exactly the
position from which to exploit that: present its own host key, take the
password, relay onward.

**So the fingerprint must come from somewhere the broker does not control.**
The board already computes it: `SYS` and `HARDWARE` show OpenSSH-style
fingerprints of the Ed25519 and ECDSA host keys to staff. Three routes:

- **the listing.** A new optional `PROTOCOL.md` field (`host_key_sha256`,
  base64, 43 characters) fits the existing pattern exactly. Then **the
  directory is trusted for one thing and one thing only: not to lie about
  the fingerprint.** That is a far smaller trust than "runs the relay", and
  it is the right place to put it. It is not nothing: a hostile directory
  could swap the fingerprint and collude with a hostile broker. **Two parties
  colluding is a materially better position than one party in control**, and
  the site copy should say so in those terms. Note that **Rob brokering on
  the directory makes both parties the same party**, which is an argument for
  a sysop who cares to use a third-party broker, or to publish the
  fingerprint out of band, and an argument for the honest sentence over the
  flattering one;
- **out of band**, the sysop posting it where their callers are. Always
  available, always better, and worth encouraging;
- **the onion address itself**, the elegant case: a v3 onion address *is* the
  service's public key, so the rendezvous cannot be impersonated by whoever
  runs the daemon. An onion dial line needs **no fingerprint distribution for
  the location layer**, though the SSH host key remains trust-on-first-use
  for the SSH layer.

**Recommendation: phase E, and it is worth doing even if phase F never
ships**, because browser SSH has exactly the same gap.

### 10.4 One rule carried over from the browser-SSH plan

That plan found that allow-listing a relay's address removes the **staff**
password's only rate limit, because `Bbs::staffPassword` is guarded by the
address ban and nothing else, and it settled on "an allow-listed address
cannot elevate to staff at all", the same rule as "no staff over RF". **A
broker is in the same position**: every brokered caller arrives from the
broker's address, so one caller's bans would fall on all of them, and
allow-listing the broker would uncap staff guessing. Same rule, settled here
before it is built: **no staff over a broker.** A sysop reaches their own
board directly, from their own network, which is where they are.

---

## 11. What it costs the board

S3 only, so measured against the S3's budget.

### 11.1 Sockets, the tight one

`CONFIG_LWIP_MAX_SOCKETS` is **16** and IDF 5.3.1 caps it at 16 on every
chip. The board already oversubscribes: 2 listeners + 10 caller nodes + the
sysop node + the busy line + 2 for the backup window + 1 for announce =
**17**, managed by `Bbs::busyFits` holding the busy line back behind
`BBS_SOCK_RESERVE` (3).

A broker adds:

- **one permanently held control connection**, and
- **one data connection per brokered caller**, which costs what a direct
  caller's socket costs, so brokered callers are free against the budget.
  They take one of the ten lines, as they should: a caller is a caller.

So the honest figure is **+1 socket, permanently**, into a budget with no
spare. Three ways out, in order of preference:

1. **drop the control connection while the board is full.** A full board has
   nothing to gain from a brokered caller, and this is exactly the `busyFits`
   pattern already in the tree: count the broker's socket and let it go when
   the board would rather have the busy line. No new concept;
2. take it from `BBS_SOCK_RESERVE`, 3 to 2. Cheap, and it narrows the margin
   announce and the backup window rely on;
3. raise the lwIP cap. **Not available on IDF 5.3.1.** Whether a later IDF
   raises it is unverified and must not be assumed.

**Recommendation: (1).**

### 11.2 Keepalive, and what it costs

The mapping must survive the NAT. RFC 5382 requires an established-connection
idle timeout of **no less than 2 h 4 min**, but real equipment is far shorter
— OpenWrt's own tracker records `nf_conntrack_tcp_timeout_established` as too
low to comply — and CGNAT boxes are tighter still because they ration ports.

The board already sets `TCP_KEEPIDLE` **60 s**, `TCP_KEEPINTVL` **10 s**,
`TCP_KEEPCNT` **3** on caller sockets (`src/config.h:102-104`). Reuse those:

- **traffic:** one ~40-byte segment each way per minute, about **115 KB a
  month** per direction. Nothing;
- **power: zero extra.** The board forces `WIFI_PS_NONE` and re-asserts it on
  every reconnect (0.18.0, re-fixed 0.21.1), and `SYS` reports
  `esp_wifi_get_ps()` so it can be checked. The radio never sleeps, so a held
  connection costs no power that was not already being spent. One of the few
  places the no-power-save decision pays a dividend;
- **latency: none.** A keepalive is a bare ACK and never touches the loop's
  work.

Sixty seconds is comfortably inside any CGNAT timeout I am aware of. It
should be a setting with 60 as the default, because the one thing that varies
is somebody's ISP.

### 11.3 Heap, flash, static RAM

Estimates, flagged as estimates, by comparison with the link, which is the
same shape and is measured:

| | Estimate | Basis |
|---|---|---|
| Static DRAM | **under 200 bytes** | the whole link, engine and two plugins, is 142 bytes; this is one connection's state plus a pairing table that exists |
| Flash | **4 to 8 KB** | the link's engine plus plugin is 21 KB and carries pairing, discovery, channels, bulk windows and three families. This reuses `linkcrypto` and the pairing and adds a TCP transport and one family's host side; doors, a whole family, is 3,844 bytes |
| Heap while on | **2 to 4 KB** | one connection's buffers; ciphertext is forwarded, not framed or re-encrypted |
| Crypto | **nothing new** | `linkcrypto` is linked; mbedTLS's ECDH, HKDF, CCM and SHA-256 are in the image for WPA3; sealing is 78 µs on an S3 |
| Per session | **0** | a brokered caller is an ordinary `Session` on an ordinary socket |

The S3 has **85,560 bytes** of static RAM free at the 1.2.0 merge, so none of
this is near a wall.

**Rule no. 1:** nothing slow on the loop. Pairing's ECDH goes on the runner,
as the link's does. Forwarding is `recv` into the timeline and `send` out of
it, the same work a direct caller's socket does. The thing to watch is dial-out
and reconnect, because DNS can take seconds: that goes on the runner with
announce's resolver and its last-good-address cache, machinery 1.1.2 built.

---

## 12. What it costs whoever runs the broker

Rob is content for this to be the directory's droplet, so these figures are
his.

### 12.1 Bandwidth

A BBS session is tiny. A screen is about 2.4 KB (the timeline's own limit); a
whole ordinary call is a few tens of KB. File transfers are the only
non-rounding-error, and they double at the broker because every byte is
relayed in and out.

| Load | Transit through the broker |
|---|---|
| one ordinary call, say 100 KB | 200 KB |
| a busy board, 50 calls a day | ~10 MB a day |
| 100 such boards | ~1 GB a day, ~30 GB a month |
| one 5 MB file download | 10 MB |

The smallest VPS anyone sells comes with a terabyte of transfer. **A single
droplet carries a hundred busy boards' conversation with room to spare**, and
file transfer is what would eventually move the number. That is a reason to
let a sysop cap it, not a reason not to build it.

### 12.2 Connections and CPU

One control connection plus up to ten data connections per board, so a
hundred boards is 1,100 sockets and 1,100 pairs of `recv`/`send` at BBS data
rates. A stdlib asyncio process handles that without effort. Same program
shape as the browser-SSH relay, which is the argument for one program with
two front doors (§4.3).

### 12.3 What a hostile user could make it carry

**The ciphertext rule cuts both ways: a broker that cannot inspect cannot
police.** So:

- **a listed board could tunnel arbitrary TCP** through its data connections
  and use the broker as free transit. The broker cannot tell that from a BBS
  session;
- **a caller could flood** by opening data connections;
- **volume**: somebody serving a Linux ISO through "FILES".

What limits it, all on the broker's side where it belongs:

- **eligibility is a listing**, and a listing already takes three hours of
  sustained heartbeats and is capped per address. That is a real cost to an
  abuser and it is machinery that already exists;
- **per-board caps**: bytes a day, connections at once, connection rate. The
  board is told when it hits one and says so to the sysop;
- **it is not an open proxy.** The broker accepts from boards that dialled it
  and from callers, and connects onward only through a connection a board
  already opened. It never initiates a connection to an address a stranger
  named. That is the directory's own argument, unchanged, and it is the
  sentence that belongs in the broker's README;
- **a kill switch per board**, since the directory can already hold a
  listing.

---

## 13. Against the values

### 13.1 The question, asked straight

The site's ten freedoms include **"No platforms — nobody in the middle"**,
**"No cloud — nobody else's server"** and **"No hosting fees — it runs at
your place"** (`sitekit.py`, `FREEDOMS`). A broker on Rob's droplet is a
server somebody else runs, in the middle, that someone pays for monthly.

Rob has accepted that, so the job is not to argue but to keep it from
becoming the thing the project exists in opposition to. The directory's own
README states the test: *"A list nobody can replace would be the wrong shape
for a project about not depending on anybody."*

### 13.2 Applying that test to a broker

What has to be true, and all of it is cheap decided now rather than
retrofitted:

- **the protocol is published** in `PROTOCOL.md` (the headers) and `LINK.md`
  (family 3), so anybody can write a broker in an afternoon, as the announce
  protocol already promises for directories;
- **the broker is its own small repository**, GPL, runnable by anyone on any
  box, and **not a route inside `server.py`** (§5.4);
- **no broker is on by default.** The setting is off on a fresh board and
  blank in `data/system.cfg.example`. A board that does not use one behaves
  exactly as a board does today;
- **several brokers per board**, so no single one is load-bearing even for
  one board;
- **the ladder prefers every rung above it.** A board reachable directly is
  reached directly, and the sysop is told which rung is carrying them;
- **the site says what it means.** For this feature "No platforms, nobody in
  the middle" becomes **"nobody in the middle you did not choose"**, with the
  three things an operator can see named in the same breath. That is still a
  true and distinctive claim, and the project has form for preferring the
  honest sentence.

With those six, a broker is a tool a sysop may choose, like the directory and
like a satellite. Without them it is a platform with a nicer name.

### 13.3 Where the two non-broker answers win, on Rob's own words

- **Port mapping and IPv6 (phases C and D)** are the purest on **distributed**:
  there is no component at all, nothing to replace, nothing to run, nobody to
  trust. They should be built first for that reason as much as for cost.
- **The onion front (phase B)** is the strongest on **freedom/privacy
  forward**: there is nothing shared to depend on, every sysop runs their own,
  no default can be captured, and it is the only board-side option that hides
  location. It passes the directory's test not by being replaceable but by
  having nothing to replace.
- **The onion directory face (phase A)** is the same argument applied to
  readers, and it is the only item in the study that improves privacy for
  people who are not sysops at all.

The broker ranks below all three on Rob's two words, and above all three on
the one word he did not say but which decides whether any of it matters:
**whether an ordinary caller can reach the board with the client they already
have.** That is why the plan builds A through E first and F after.

---

## 14. IPv6 and UPnP, compared fairly

These two are the cheapest work on the list and they keep the board
**directly reachable**, which no broker can.

### 14.1 IPv6

**Adoption:** Google's measurement crossed **50% for the first time on
2026-03-28, at 50.10%**, from 46.33% a year earlier. Other vantage points
read lower, which is normal and worth stating rather than hiding: Cloudflare
Radar puts IPv6 at **40.1%** of HTTP requests, and APNIC Labs finds **43.13%**
of the networks it can see IPv6-capable. By country, France 73%, India 72%
and Saudi Arabia 65% down to Italy 17% and Spain 10%.

**What it buys:** the end of the hard case. CGNAT is an IPv4 scarcity
measure; a board with a global IPv6 address has an address of its own and no
mapping to create.

**What it does not buy, and this is the part usually skipped: a global
address is not inbound reachability.** RFC 7084, the basic requirements for
IPv6 customer edge routers, points at RFC 6092 for "simple security", and
RFC 6092's model is **default-deny stateful inbound filtering** on residential
CPE. So the sysop still opens a hole: a firewall rule instead of a port
forward. **The same router menu, minus the NAT.** For somebody who finds
router menus hard, IPv6 is not the answer. For somebody behind CGNAT with a
working router and an IPv6-capable ISP, it is the best answer in this entire
study, because the board stays directly reachable with nobody in the middle.

**What it costs us:** `CONFIG_LWIP_IPV6=n` today, turned off at 1.1.1 for
about **1,888 bytes of static DRAM**, with the reasoning recorded in
`sdkconfig.defaults:194-198` ("no board has ever had an IPv6 address"). On a
WROOM with 15,632 bytes free that is a real fraction; on an S3 with 85,560
free it is **noise**. Since this is S3-only work, turn it on there.

### 14.2 UPnP IGD, NAT-PMP and PCP

**What it buys:** the board asks the router to forward the port and the sysop
never opens a menu. Where it works the result is **identical to a
hand-forwarded port**: direct, no middle, nothing to trust. On Rob's two
words this is the purest option available, because there is no component at
all.

**What it costs:** `miniupnpc` is "less than 50KB code size… pure ANSI C";
`TinyUPnP` and `UPnP_Generic` are smaller and already target the
ESP32/ESP8266, though both are Arduino-flavoured and would want reworking for
this tree's style and its no-heap-in-the-loop rule. NAT-PMP and PCP are
smaller again: a two-packet UDP exchange with the default gateway, a few
hundred lines written from the RFCs rather than a library.

**Why it is first and still not sufficient:**

- **it does nothing under CGNAT.** The CPE maps to its own private WAN
  address, so the mapping succeeds and the board is still unreachable. PCP
  was designed to address exactly this at the carrier NAT, and ISPs
  essentially never expose it;
- **presence is unknown and unmeasured.** I found no survey. Tailscale's
  position after years of this is "you can't rely on these protocols being
  present", and Wikipedia's NAT traversal article notes that among consumer
  routers only AVM and the open-source firmwares are known to support the
  IGDv2/PCP/NAT-PMP alternatives;
- **a mapping that succeeds proves nothing** without the outside probe of
  §3.1, and a board claiming "forwarded" when it is not is worse than one
  saying "I do not know";
- it has a security reputation and some sysops will have turned it off on
  purpose. A setting, default off or ask-once, never silent.

**Instrument it.** The instrumentation is as valuable as the feature: after a
few hundred boards the project would hold the measured availability figure
nobody currently has, which decides how much the rest of the ladder is worth.

### 14.3 The four compared, one line each

- **Port mapping:** days, no middle, no monthly cost, helps the majority,
  **helps nobody behind CGNAT**.
- **IPv6:** days, no middle, no monthly cost, **solves CGNAT outright**,
  still one router rule, only where the ISP provides it.
- **Onion front:** almost no firmware, no middle, no monthly cost, solves
  CGNAT, hides the address, **asks every caller to install Tor**.
- **Broker:** weeks, one chosen middle, somebody's $5 a month (or Rob's
  droplet), solves CGNAT, **asks the caller for nothing at all**.

That last line is why all four belong in the ladder and none is the single
answer.

---

## 15. The base ESP32

Settled by Rob: this is S3 work. SSH is S3-only, the ciphertext rule depends
on SSH, so none of the brokered path reaches the base board, and that is
accepted rather than a defect.

A base ESP32 keeps what it has: telnet on a forwarded port, as now, fully
supported, nothing taken away. Per the standing pattern an S3-only feature is
**left out of the base build entirely** rather than compiled in and switched
off, so the broker, the onion plumbing and the IPv6 listener sit behind the
board gates and cost the WROOM image nothing — which matters, since it is at
about 81% of its slot with 15,632 bytes of static DRAM free. Where any of it
would surface as a `CONFIG` row a base board would otherwise list, the
greyed-row-with-an-upgrade-note convention applies, strings only.

Three notes, none of them an argument against the decision:

- **phase C, port mapping, is the one piece that would help the base board**,
  because it has nothing to do with SSH: it would get a WROOM behind a
  cooperative router onto the internet with no router menu, for its telnet
  port. It is also the cheapest piece. If anything here reaches the base
  build it is that, and it deserves its own decision rather than being swept
  along with the S3 gate.
- **phase A, the onion directory face, helps every sysop and every reader
  regardless of board**, because it is a directory feature with no firmware
  in it at all.
- **an onion front for a base board would carry telnet in plaintext to the
  daemon's operator.** In the simple case the operator is the sysop, so that
  is fine and is arguably the one legitimate telnet-over-onion configuration.
  With a third party it breaks the ciphertext rule and must be refused. The
  rule to settle now rather than discover: **a third-party-operated broker or
  onion front may carry SSH only; a sysop's own may carry what the sysop
  likes, on their own network, because they can already see it.**

---

## 16. The plan

Each phase ships alone and each is worth having if the next never happens.
Days are a working estimate for one developer on this tree, excluding Rob's
bench time and excluding the test plans that need his OK.

### Phase A — a `.onion` read face for the directory

**Repos:** `unleashed_directory`, `unleashed_site` (one guide). **2-3 days.**

- Three `torrc` lines; a second systemd unit,
  `unleashed-directory-onion.service`, with `DIRECTORY_PORT=8081`,
  `DIRECTORY_URL=http://<onion>`, `DIRECTORY_LIST_DOMAIN=` and
  **`DIRECTORY_HOME_URL=`** empty, which is the one setting that must not be
  wrong (§7.3).
- The one code change: **`DIRECTORY_READ_ONLY=1`**, which refuses `/announce`
  with a 404, omits `X-Seen-Address`, and skips `settle()` on reads. Four
  small edits, plus selftest coverage for each.
- One `INSTALL.md` section with an "Applies to versions" line, written so
  somebody else can run their own (§7.6), stating up front that **boards
  announce over clearnet**.
- `web-regression` and the site's own checks before the push, then it is live
  under the standing blanket approval.
- **Why first:** highest value for effort in the study, no firmware, nothing
  for Rob to decide, and it is the only item that improves privacy for people
  who are not sysops.

### Phase B — the onion front for a board, proved end to end

**Repos:** `esp32-bbs` (one label), `unleashed_site` (a guide). **1-2 days.**

- `tor` on a bench box with `HiddenServicePort 22 <board>:6422`; the onion
  address typed into `CONFIG announce`'s `host` row, which already takes 95
  characters and which the directory already stores unvalidated.
- Confirm the listing renders it; check **two interactions**: whether the
  `ssh://` line reads sensibly for an onion, and that the browser-SSH
  "Connect (SSH)" button does **not** offer itself for a board its relay
  cannot reach.
- Dial from OpenSSH, PuTTY and IcyTERM. **Measure the keystroke echo**, which
  is the number §6.4 is missing. Settle whether SyncTERM does SOCKS5.
- Deliverables: a guide with "Applies to versions", a `torrc` recipe, caller
  instructions per client, the measured latency, and the one label fix ("DNS
  name" is not what an onion address is).
- **Why second:** it either validates the privacy-forward board answer or
  kills it on latency or client support, before a line of broker code exists.

### Phase C — port mapping: NAT-PMP, PCP, then UPnP

**Repo:** `esp32-bbs`. **3-5 days.**

- NAT-PMP and PCP against the default gateway, then UPnP IGD. A setting, not
  silent. On the runner, never on the loop.
- `CONFIG network` says what was asked for and what was granted, with an
  "as of".
- The outcome in an optional announce field, so the project gets the measured
  availability figure nobody has.
- The board claims **mapped**, not **reachable**, until something can probe.

### Phase D — IPv6 on the S3

**Repos:** `esp32-bbs`, `unleashed_directory`. **2-3 days.**

- `CONFIG_LWIP_IPV6=y` in the S3 layer only; the WROOM keeps its 1,888 bytes.
- Dual-stack listeners, a v6 `host` in announce, the directory showing it,
  `CONFIG network` saying whether a global address exists and reminding the
  sysop that a CPE firewall rule is still needed.

### Phase E — the host-key fingerprint in the listing

**Repos:** `esp32-bbs`, `unleashed_directory`, `unleashed_site`. **3-5 days.**

- `host_key_sha256` in `PROTOCOL.md`, sent by announce, shown on the listing.
- One sentence on the site about what the directory is therefore trusted for,
  including the honest note that a directory brokering for a board is both
  parties at once (§10.3).
- **Worth doing even if phase F never ships**, because browser SSH has the
  same gap.

### Phase F — the CALLIN broker

**Repos:** `esp32-bbs`, plus a new GPL repository for the broker.
**15-20 days.**

- `LINK.md` family **3 (CALLIN)** and kind **3 (gateway)** specified before
  any code, as the link's were.
- `PROTOCOL.md`: the three optional response headers (§4.1).
- Board side: a TCP transport beside the ESP-NOW one; pairing, HKDF and
  AES-128-CCM from `linkcrypto` unchanged; **SSH port only**, enforced on the
  board; the control connection counted in `busyFits` and dropped when the
  board would rather have the busy line; **no staff over a broker**; the
  dial-out and DNS on the runner.
- Broker side: a separate program and process, stdlib asyncio, **a stable
  port per board** (§4.4), per-board caps, never an outbound connection to an
  address a stranger named, a README saying it is not an open proxy, and a
  local handoff from `server.py` so it needs no database access.
- The directory: the broker headers on an eligible listing, the brokered dial
  line on the listing, and a per-board off switch.
- Board UX: `REPEATER`, `CONFIG repeater`, the ladder status page (§3.2).
- Phase C's probe closes here, from a third-party broker only, never from the
  directory (§3.1).

### Phase G — a private `.onion` directory instance

**Repo:** `unleashed_directory` (configuration and docs only). **3-5 days.**

- A second instance, own `DIRECTORY_DB`, own port, own `torrc`. No new code
  beyond phase A's read-only flag.
- Documented with the limit stated plainly: **the list is private, the
  announce endpoint is not**, because a board cannot POST to an onion
  (§7.5).
- `servers` in `CONFIG announce` is already a comma list, so a board can
  announce to a private directory as well as, or instead of, the public one.
  Nothing in the firmware changes.
- **Build only when somebody wants it**, and expect that somebody to be a
  sysop running a board for a circle rather than for the public.

### Not built, and why

Hole punching (§2), WireGuard and Tailscale (§8.1), I2P, Yggdrasil and cjdns
(no microcontroller implementation and no audience), a Tor client in the
firmware (§6.1), and a broker route inside `server.py` (§5.4).

---

## 17. What needs Rob

**Recommendations, which need only a yes:**

1. **Phase A first**, and it needs no decision at all beyond "go".
2. **The name `repeater`**, with "onion repeater" and "broker repeater" as the
   two mechanisms, and whether the words live in `satwords.h` or a sibling
   `reachwords.h` (§5.2).
3. **Brokered callers take one of the ten lines.** A caller is a caller; a
   broker raises reachability, not capacity.
4. **IPv6 on the S3**: 1,888 bytes against 85,560 free. Yes.
5. **No broker on by default**, and the broker a separate program and
   repository rather than a route in `server.py` (§5.4, §13.2).
6. **No staff over a broker** (§10.4), settled before it is built.
7. **A third-party broker or onion front carries SSH only; a sysop's own
   carries what they like** (§15).

**Decisions that are genuinely his:**

8. **Which "fully private `.onion` directory" he meant** (§7.5). Reading 1 is
   phase A and is free; reading 2 is a different product and is phase G. My
   recommendation is reading 1 now, reading 2 documented and built on demand.
9. **Whether a board may be onion-only and unlisted**, with v3 client
   authorization, as a supported and documented configuration. I recommend
   yes: it costs almost nothing and it is a genuinely new kind of board.
10. **Whether to accept the onion latency**, 260-720 ms of echo, and whether
    the listing should say so. I recommend yes and yes, after phase B
    measures it on a real board.
11. **Whether the port-mapping rung reaches the base ESP32** (§15). It is the
    one piece that is not SSH-dependent, and it is a separate decision from
    the S3 gate.
12. **The site sentence.** For this feature, "No platforms, nobody in the
    middle" needs to become "nobody in the middle you did not choose", with
    the three things an operator can see named. And the onion directory page
    must say plainly that it hides **readers, not boards** (§7.1), because
    the natural assumption is the opposite. A `marketing` and `explain`
    decision as much as an engineering one, and better made before the
    feature exists.
13. **Whether SyncTERM's SOCKS support, once established in phase B, changes
    who the site points newcomers at.** IcyTERM does onion dialling properly,
    from its dialing directory, which is a better first experience if
    SyncTERM cannot.

---

## Sources

NAT traversal and hole punching:

- [Large-Scale Measurement of NAT Traversal for the Decentralized Web: A Case Study of DCUtR in IPFS](https://arxiv.org/html/2604.12484) — 70% ± 7.1% conditional hole-punch success, ~29% prerequisite failure, TCP ≈ QUIC, 6.25M attempts, 859 client / 86,769 peer networks, 97.6% first-attempt, and the authors' own statement that they cannot classify remote NAT types
- [Challenging Tribal Knowledge: Large Scale Measurement Campaign on Decentralized NAT Traversal](https://arxiv.org/pdf/2510.27500) — companion of the same campaign; surfaced in searching, not read in full
- [Tailscale: How NAT traversal works](https://tailscale.com/blog/how-nat-traversal-works) — "over 90% of the time" direct with the full stack; TCP "may even require kernel customizations"; two hard NATs at 54,000 packets for 50%; "you can't rely on these protocols being present"
- [Characterization and Measurement of TCP Traversal through NATs and Firewalls](https://www.researchgate.net/publication/221612047_Characterization_and_Measurement_of_TCP_Traversal_through_NATs_and_Firewalls) — the older 88%-over-93-NATs figure
- [RFC 5382, NAT Behavioral Requirements for TCP](https://www.rfc-editor.org/rfc/rfc5382.txt) — established-connection idle timeout MUST NOT be less than 2 h 4 min
- [OpenWrt #17098: nf_conntrack_tcp_timeout_established is too low; doesn't comply with RFC 5382](https://dev.archive.openwrt.org/ticket/17098.html) — real equipment is shorter than the RFC

CGNAT:

- [A Multi-perspective Analysis of Carrier-Grade NAT Deployment](https://www.icir.org/christian/publications/2016-imc-cgnat.pdf) (IMC 2016) — 17-18% of eyeball ASes, >90% of cellular ASes, ~40% of surveyed operators
- [Tracking the Big NAT across Europe and the U.S.](https://arxiv.org/pdf/1704.01296) — 10% of ~60 ISPs tested, from 5,121 vantage points

IPv6:

- [Google hits 50% IPv6](https://blog.apnic.net/2026/04/28/google-hits-50-ipv6/) — 50.10% on 2026-03-28 against 46.33% a year earlier; Cloudflare Radar 40.1%, APNIC Labs 43.13%
- [18 Years Later, IPv6 Reaches Majority](https://pulse.internetsociety.org/en/blog/2026/04/18-years-later-ipv6-reaches-majority/)
- [IPv6 Adoption in 2026](https://www.netmeister.org/blog/ipv6-adoption.html) — the country spread
- [RFC 6092, Recommended Simple Security Capabilities in CPE for Residential IPv6](https://www.rfc-editor.org/rfc/rfc6092.html) — the default-deny inbound model
- [RFC 7084, Basic Requirements for IPv6 Customer Edge Routers](https://www.rfc-editor.org/rfc/rfc7084) — points CPE at RFC 6092

Port mapping:

- [miniupnp / miniupnpc](https://github.com/miniupnp/miniupnp), [API docs](https://miniupnp.tuxfamily.org/doc/miniupnpc/2.3.0/) — "less than 50KB code size", pure ANSI C
- [TinyUPnP](https://github.com/ofekp/TinyUPnP), [UPnP_Generic](https://github.com/khoih-prog/UPnP_Generic) — ESP32/ESP8266 IGD clients
- [Wikipedia: NAT traversal](https://en.wikipedia.org/wiki/NAT_traversal) — IGDv2/PCP/NAT-PMP consumer support limited to AVM and the open-source firmwares
- [PCP working group](https://datatracker.ietf.org/wg/pcp/about/)

Tor:

- [Minitor (jpbland1)](https://github.com/jpbland1/Minitor), [its GitHub API record](https://api.github.com/repos/jpbland1/Minitor) — 21 stars, GPL-2.0, last push 2023-09-01, 1 open issue, not archived; forked wolfSSL for expanded ed25519; ~300 s consensus fetch; "several minutes" to start an onion service
- [Minitor project page](https://triplelayerdevelopment.com/minitor) — the "70KB of memory / 500MB of storage" claim, quoted and not relied on
- [toresp32 (briand-hub)](https://github.com/briand-hub/toresp32) — an ESP32 Tor proxy; state not verified
- [Tor proposal 158: microdescriptors](https://spec.torproject.org/proposals/158-microdescriptors.html) — bootstrap "from at least 500 kilobytes to 100 kilobytes"
- [Tor Project: Client Authorization](https://community.torproject.org/onion-services/advanced/client-auth/) — v3 `authorized_clients/`, `descriptor:x25519:<key>`, `ClientOnionAuthDir`; `HiddenServiceAuthorizeClient` is v2 only
- [Tor Metrics: OnionPerf latencies](https://metrics.torproject.org/onionperf-latencies.html) and its CSV for 2026-09-01 to 2026-10-04 — onion-server medians about 260-340 ms (op-de8a), 420-500 ms (op-us8a), 650-720 ms (op-hk8a)
- [SSH over an onion service](https://hackindex.io/blog/ssh-over-onion) — `ProxyCommand nc -X 5 -x 127.0.0.1:9050 %h %p`, torsocks

Clients:

- [PuTTY: name resolution when using a proxy](https://documentation.help/PuTTY/config-proxy-dns.html) — Auto passes hostnames straight to a SOCKS5 proxy, which is what makes `.onion` resolve
- [IcyTERM README](https://github.com/mkrueger/icy_tools/blob/master/crates/icy_term/README.md) — per-entry SOCKS5 for Telnet, Raw, RLogin and SSH; remote DNS "which is what makes `.onion` and `.i2p` addresses reachable"; Tor and I2P presets; MIT/Apache-2.0

Overlays:

- [esp_wireguard (trombik)](https://github.com/trombik/esp_wireguard), [component registry entry](https://components.espressif.com/components/trombik/esp_wireguard) — v0.9.0, BSD-3-Clause, declares ESP-IDF master/4.2/4.3/4.4 and targets esp32/s2/c3/esp8266: **not 5.3, not the S3**
- [Memory optimization for a Tailscale-on-ESP32-C3 port](https://deepwiki.com/alfs/tailscale-iot/5-memory-optimization-for-esp32-c3) — ~200 KB idle to ~300 KB peak; third-party page, not primary
- [Yggdrasil as an embedded Go library](https://dev.to/asciimoth/yggdrasil-network-as-an-embedded-go-library-9h) — Go only; no microcontroller port found
- [awesome-tunneling-list](https://github.com/tov-a/awesome-tunneling-list), [frp](https://github.com/fatedier/frp) — the existing self-hosted TCP relay landscape, as prior art for the broker

In these trees:

- `LINK.md` — family 3 CALLIN and kind 3 gateway reserved; the pairing exchange; the measured budgets; the 78 µs seal
- `src/core/satwords.h` — the settled vocabulary the repeater must not collide with
- `internal/plan-web-ssh-2026-10-04.md` — the browser relay, which is the other consumer, and the staff-password rule carried over here
- `src/config.h` — `BBS_SOCK_RESERVE` 3, `BBS_MAX_NODES` 10, `BBS_SSH_MAX`, `BBS_KEEPALIVE_*` 60/10/3, `BBS_SSH_PORT` 6422
- `sdkconfig.defaults` — `CONFIG_LWIP_MAX_SOCKETS=16`; `CONFIG_LWIP_IPV6=n` with its reasoning at lines 194-198
- `src/core/bbs_ssh.cpp` — `busyFits`, the socket accounting, the keepalive on the SSH port
- `src/plugins/announce.cpp` — `kBodyMax` 1368, `kUrlMax` 96, the `host` row at 1498 (`PS_TEXT`, 95 characters)
- `unleashed_directory/server.py` — `role_for()` 238-241 (one face); `group_of()` 738-748; `db()` 610-614 (WAL, 10 s); `"host": tidy(..., 80)` 1226; `settle()` on reads 3212/3400/3462; `canonical()` 4466-4473; `X-Seen-Address` 4483; the `HOME_URL` 301s 4616-4624; `AVATAR_URL` 1429, `OG_CARD_URL` 1433
- `unleashed_directory/deploy/unleashed-directory.service` — every setting as an environment variable, `DIRECTORY_PER_ADDRESS`, `DIRECTORY_MIN_SECONDS`, `DIRECTORY_ADDRESS_PER_MINUTE`, and "Empty it for a directory of your own"
- `unleashed_directory/selftest.py` 3745-3769 — the one-inline-script page invariant
- `unleashed_directory/README.md` — "never makes outbound connections"; "a list nobody can replace would be the wrong shape"
- `unleashed_directory/sitekit.py` — `FREEDOMS`: "No platforms / nobody in the middle", "No cloud / nobody else's server"
