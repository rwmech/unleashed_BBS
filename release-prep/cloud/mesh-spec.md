# Cloud job: specify the self-healing mesh, before any of it is built

Paste this into a **new** claude.ai cloud session on the µnleashed BBS repo.
Not into an existing one: each of the idle sessions carries unrelated
context and you would pay for it on every call.

**Why cloud:** reading and writing only. No board, no firmware build (the
cloud environment cannot reach PlatformIO's registry), no network tests.
The deliverable is one document.

---

## What is settled, so none of it is re-litigated

- **Rob chose a full self-healing mesh on 2026-10-09**, over two cheaper
  options he was shown and declined: routes that CAN change but are planned
  by hand (~2 weeks), and the one-hop relay as already costed (1.5 weeks).
  He knows this one is 3 to 5 weeks. Do not re-propose the cheaper two.
- **Written as our own, over the link we already have.** He was offered
  Espressif's ESP-MESH-LITE and declined it: it is a Wi-Fi mesh with a root
  and a tree, a different topology from ESP-NOW peers, and the board is
  already joined to a router. **Do not propose adopting a mesh framework.**
- **Sats route; the board does not** (Rule no. 1). The board is a
  destination. This is also the fan-out answer, since the board then holds
  one pairing rather than thirty.
- **A repeater holds no key.** It forwards sealed frames it cannot open,
  with a small wrapper, below the family layer, so it never learns what
  CALLIN or a camera transfer is. A node that terminates a session and
  re-originates it is a different device (a "concentrator") and is out of
  scope here.
- **Rob wants caller sessions carried over it**, not only camera and sensor
  data (2026-10-05: "repeater to repetaer of the callin sat should apply").
  CALLIN is family 3 in LINK.md and peer kind 3 `gateway` is reserved.
- The accepted downside, stated to him before he chose: a self-healing mesh
  can reroute a session onto a longer path mid-call.

## Measured, so build on these rather than guessing

All read off this repo or the pinned IDF, not recalled:

- `ESP_NOW_MAX_TOTAL_PEER_NUM` is **20** and `ESP_NOW_MAX_DATA_LEN` is
  **250** in framework-espidf@3.50301.0's `esp_now.h`. 20 is what bounds a
  node's neighbour table; `kPeers = 8` is only the board's pairings.
- `ESP_NOW_MAX_ENCRYPT_PEER_NUM` is 6 and **does not bind us**: we seal
  frames ourselves and leave ESP-NOW peers unencrypted, a decision taken in
  1.2.0 because the 5.3.1 receive callback cannot say whether a frame was
  decrypted.
- Our AES-128-CCM costs **93.6 us to seal and 94.7 to open on a base
  ESP32**, 78/78 on an S3, with the 20-byte header as associated data.
- A whole control frame handled on the loop is **150-184 us**. Rob's line
  for work on the loop is 100 us, so control frames stay only because they
  are a few a second. **Bulk fragments never touch the loop**: the radio
  sorts them into a second ring and the runner opens them.
- Keystroke echo on the board today, five callers, measured on the bench:
  **p50 4 ms, p95 12 ms**. That is the bar a routed session is judged
  against.
- Forwarding from the receive callback is 2-6 ms a hop; forwarding on a
  tick is about 40 ms a hop. Two tick-driven hops is where a caller
  notices.
- 11g 24 Mbps per peer with a 1 Mbps fallback after three MAC failures,
  back after 30 s clean.

## The one question that must be answered before any code

**Route advertisements have to be authenticated, and nothing in the
existing design does that.** The payload stays sealed end to end, so a
repeater cannot read it, but routing metadata lives OUTSIDE that envelope
by construction, and in a self-healing mesh the network picks the path
where an operator used to. So anything in radio range can advertise itself
as a good next hop and pull traffic through itself. It cannot read what it
attracts; it can drop it, delay it, or learn who talks to whom.

Neither the one-hop relay nor a hand-planted chain had this problem,
because a human chose the path. Treat it as the spec's central problem, not
an appendix. Research how existing protocols solve it (Babel's and RPL's
security modes, batman-adv, Zigbee's network key and what it does and does
not protect) and say plainly which of those shapes can work when every node
is a cheap ESP32 with 180 KB of DRAM and the pairing is a four-digit code.

## Traps this project has already hit, each of which is live here

- **A clock that starts small hides `reached(now, 0)`.** Pairing armed with
  0 was false for half the millisecond clock's range, so a board up 24.8
  days never finished pairing. **Elapsed time is `plat::since(now, at)`**,
  never a raw subtraction and never a signed difference; a deadline
  compared as a signed difference needs a retiring clock guaranteed to run.
  Three separate bugs of this exact shape have shipped here.
- **State kept for "the peer" has to become state per peer.** The
  satellite's 1 Mbps fallback was one entry for the radio, so sending to
  two boards in turn reset it at every change of destination and the
  fallback never held. Anything singular in a mesh is a suspect.
- **A window that serves someone new must keep serving everyone else.** A
  pairing share hopped every channel for two minutes and killed every live
  session on every board. Found by review, not by a test, because no test
  held a session open across a window.
- **Every state needs an end.** A pairing that stopped part way wedged a
  satellite permanently.
- **`#if` cannot see an enumerator**, so a guard written as an enum
  silently compiles to nothing. Board profiles here are `#if` the whole way
  down.
- **A verifier needs its own proof.** A guard-removal runner once reported
  "every guard has a test" having tested none.

## What NOT to do, and why

- **Write no code.** This is a document. The link itself was specified in
  LINK.md before a line was written, and that is the pattern.
- **Invent no figures.** Nobody has measured hop latency, range, repair
  time or throughput on this hardware. Where a number is needed, say what
  would have to be measured and how. A spec that states a range in metres
  will be read as a promise.
- **Do not let "mesh" imply more capacity.** Every ESP-NOW peer sits on the
  board's router channel, so hops share one piece of air: reach goes up and
  capacity goes down, however good the routing. Zigbee spreads hops across
  channels and we cannot. Say so early and plainly.
- **Do not design the board as a router**, and do not propose a node that
  terminates and re-originates sessions.
- Do not touch `src/`, `tools/`, or any site repo.

## Read first

`LINK.md` (the whole thing, it is the protocol this extends),
`src/core/link.h` (peer kinds, family numbers, `kPeers`),
`src/core/linkfam.h`, `src/core/linkcrypto.*`,
`src/platform/linkradio*`, `internal/sat-types-2026-09-27.md` (the one-hop
relay as originally costed, including the 2-byte wrapper),
`internal/spec-callin-node-2026-10-05.md`,
`internal/link-multiboard-2026-09-27.md`, and the mesh and gateway entries
in `CLAUDE.md`.

## What the spec must answer

1. The frame wrapper: what a forwarded frame carries outside the sealed
   envelope, in bytes, against the 250-byte limit.
2. The neighbour table: what a node keeps per neighbour, how it is sized
   against 20 peers, and what it costs in static DRAM on a base ESP32.
3. How a route is advertised, **and how that advertisement is
   authenticated**. The central problem, above.
4. Loop control. The published page already found the failure by itself:
   three boxes each passing on everything they hear is a shouting match.
5. How a dead node is noticed, how long repair takes, and what the network
   does in the meantime.
6. What a reroute does to a session in flight, and whether a session can be
   pinned to a path once established.
7. The latency budget per hop, against p50 4 ms and p95 12 ms, and the hop
   count past which a terminal stops feeling like a terminal.
8. What is measured on hardware before any of it is trusted, as a list
   somebody can execute.

## The report

`internal/spec-mesh-<date>.md`, written the way LINK.md is written: decisions
with their reasoning, not options with a shrug. Commit it by file name on a
branch. **Do not push.**

Then, in at most 25 lines in the session itself: the decisions you took and
why, the figures you could not obtain and what would obtain them, anything
in the existing link design this mesh would force a change to, and **what
you found and did not fix**.

## House rules

- Run scripts bare: `python <path>`, no pipes, no redirection, no
  environment prefixes.
- Commit by file name. Never `git add -A` or `git commit -a`: other agents
  have uncommitted work in this checkout.
- No `Co-Authored-By`, no AI line. Copyright and SPDX name Robert Mech
  alone, never Anthropic or Claude.
- Do not push, do not tag.
- Never put a password in an environment variable: cloud environment
  variables are readable by anyone using the environment.
