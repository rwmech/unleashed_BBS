<!--
 ===========================================================================
  µnleashed BBS
  Electronic freedom on a microcontroller.
 ===========================================================================

 File:         internal/plan-web-ssh-2026-10-04.md
 Module:       Working notes / plan

 Purpose:      Browser SSH, turned from research into a build plan: the
               phases, what each costs, and the two decisions Rob settled.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v3 or later
 SPDX-License-Identifier: GPL-3.0-or-later
 ===========================================================================
-->

# Browser SSH: the build plan (2026-10-04)

Rob has decided to build it: "SSH Works, get it built", and "can't we add a
ssh from the browser to connect to these sites now?". So this supersedes
`internal/research-web-ssh-2026-09-28.md` as the thing to work from; that
document stays as the evidence behind it.

Read against: firmware `main` at 9209947 (BBS_VERSION 1.2.1), directory
`main` at 2.0.16, `unleashed_site` at 1.6.3. No code, config, test or doc
was changed by this report. Every library fact below was re-checked on the
web today rather than carried over; where a figure could not be measured
without running a build it says so.

One tooling note, because the rule says to report it: the guard refused
`curl` to `registry.npmjs.org` ("research goes through WebFetch"). Every
registry and API figure here came through WebFetch instead.

---

## 0. Bottom line

- **Build it, in six phases, 8.5 to 12.5 days.** Nothing in it needs Rob's
  hardware until the end of phase 2, and phases 3, 4 and 5 need none at all.
- **Rob's two answers make this materially cheaper than the research
  assumed**, and in one case they make a whole section of it unnecessary:
  - the relay does not have to be the directory, so **the directory changes
    not one line** and its "never makes outbound connections" promise stands
    exactly as written. The research wanted that sentence reworded. It does
    not now.
  - the allow-list replaces the PROXY protocol for phase 1, which takes 2
    days of S3 firmware work off the critical path.
- **The allow-list has one consequence that must be closed in the same
  change.** It is not the account passwords (those are protected per handle,
  address-independent, and the allow-list does not touch that). It is the
  **staff** password: a wrong `BYE <password>` or a wrong answer to the
  login's sysop question is rate-limited by **nothing but the address ban**
  (`Bbs::staffPassword`, bbs_shell.cpp:3546). Allow-list the relay and staff
  password guessing through it becomes unlimited. The fix is cheap and the
  project has already made it twice elsewhere: **an allow-listed address
  cannot elevate to staff at all**, the same rule as "no staff over RF".
- **One thing is missing that the directory already expects.** PROTOCOL.md
  says a board sends `ssh_port` "from firmware 1.2.2". **No branch sends it**
  (checked: all 88 refs, `src/plugins/announce.cpp`). The directory can
  receive it, store it, badge it and show it; nothing fills it in. So
  phase 0 is firmware, and until it ships no board can get a button at all.
- **Client: `@microsoft/dev-tunnels-ssh` stands up**, re-verified today.
  MIT, 3.12.42, published 2026-08-24, newest commit the same day.
- **The page belongs on unleashedbbs.com (`unleashed_site`), not on the
  directory**, and this is firmer than the research realised: the
  directory's selftest asserts that the only script on any page is one named
  inline string, with no `src`, that never fetches, stores or opens a
  WebSocket (selftest.py:3750-3769). A terminal page breaks that invariant
  by construction. The .com site already vendors a large third-party
  JavaScript bundle with SHA256SUMS (ESP Web Tools on `/install`), which is
  the exact precedent needed.

---

## 1. What was re-checked today, and what moved

| Claim | Status today | Source |
|---|---|---|
| dev-tunnels-ssh is MIT | **Yes**, `"license": "MIT"` in the registry and in the repo | [registry](https://registry.npmjs.org/@microsoft%2fdev-tunnels-ssh), [repo](https://github.com/microsoft/dev-tunnels-ssh) |
| latest version | **3.12.42**, published `2026-08-24T22:01:54.829Z`; registry `modified` the same. A web search claimed 3.12.36 "2 days ago"; the registry says otherwise, so the search was wrong | registry |
| still maintained | **Quietly, yes.** Newest commit 2026-08-24 ("Share signature verification across public-key and host-based auth paths", #161), so about six weeks quiet, not abandoned; 4 open issues, 10 open PRs, no archive notice | [commits API](https://api.github.com/repos/microsoft/dev-tunnels-ssh/commits) |
| browser support and crypto | **Confirmed in the README**: Node >= 14 or a browser; Web Crypto in the browser; "AES-CBC is not supported in browsers due to a limitation of the web crypto API. AES-CTR or AES-GCM works fine"; `diffie-hellman` needed because Web Crypto has no DH | repo README |
| size | registry `dist.unpackedSize` **1,154,335 bytes** for the whole tarball, which includes `.d.ts` and `.js.map`. Largest JS files: `sshSession.js` 55,827, `keyExchangeService.js` 30,948, `sshChannel.js` 23,201. The research's 530 KB raw / 87 KB gzip of shipped `.js` is consistent with that. **The minified, tree-shaken bundle size is still unmeasured** and is a phase 1 output, not a claim | registry, [jsDelivr](https://data.jsdelivr.com/v1/packages/npm/@microsoft/dev-tunnels-ssh@3.12.42) |
| runtime deps | `buffer ^5.2.1`, `debug ^4.1.1`, `diffie-hellman ^5.0.3`, `vscode-jsonrpc ^4.0.0`. All bundlable; `vscode-jsonrpc` should fall out of a tree-shaken browser build and that is worth confirming in phase 1 | registry |
| xterm.js | `@xterm/xterm`, **MIT**, latest **6.0.0**, released **2025-12-22**. The research implied a 2026-08-30 release; that was a commit on main, not a release. Ten months between releases on a mature terminal emulator is fine, but say the right thing | [releases API](https://api.github.com/repos/xtermjs/xterm.js/releases) |
| a relay is unavoidable | **Yes.** Direct Sockets (raw TCP from a page) shipped in Chrome 131 but only for **Isolated Web Apps**, in practice only on managed ChromeOS; Mozilla is negative on both raw sockets and IWAs, WebKit has no position | [Chrome IWA docs](https://developer.chrome.com/docs/iwa/direct-sockets), [blink-dev intent](https://groups.google.com/a/chromium.org/g/blink-dev/c/5R0P_aYBWQI) |
| a better client appeared | **No.** sshterm v0.9.0 (2026-09-07, 5,132,663-byte download) is still the most complete and still has no SSH password auth. SSHy (pure-JS, WebSocket) surfaced in searching and is **ruled out**: last push 2022-11-29, hand-rolled crypto on SJCL/JSBN rather than Web Crypto | [sshterm releases](https://api.github.com/repos/c2FmZQ/sshterm/releases), [SSHy](https://api.github.com/repos/stuicey/SSHy) |
| the directory has `ssh_port` | **Yes**, 2.0.16 today: column, `/announce` validation (1..65535 or null), `/api/boards.json` `ssh_port`, the `ssh` badge and filter, and the second address line with a closed padlock and an `ssh://` link | `server.py` 195, 1269-1271, 2554-2562, 2802, 2894-2928; `PROTOCOL.md` 115-135 |
| a board sends it | **No. Nothing does.** No branch of the firmware has `ssh_port` in `announce.cpp` | 88-ref scan |

---

## 2. Rob's two decisions

### 2.1 The relay must not be banned: allow-list, PROXY later, and one narrowing

**Recommendation: build Rob's allow-list now, with three properties rather
than one, and keep PROXY v2 as an optional later phase.**

The board's whole per-address defence is four call sites, which is why this
is cheap either way:

| Site | What it does |
|---|---|
| `bbs.cpp:773` | telnet accept: drop a banned address |
| `bbs_ssh.cpp:233` | SSH port accept: the same |
| `bbs_shell.cpp:3546` | a wrong **staff** password counts, and bans at 3 in 15 min |
| `bbs.cpp:1059` | an SSH connection that ended on wrong **account** passwords and no right one counts once |

**What the allow-list costs, honestly.** It removes the address ban for the
relay. Two different things then lose their rate limit, and they are not
equally protected elsewhere:

- **Account passwords: still protected, and not by the address.**
  `LoginGuard` locks a *handle* after `BBS_LOCK_FAILS` (5) wrong passwords
  in 15 minutes, for 15 minutes, keyed on the handle and nothing else
  (guard.cpp, `LoginGuard::fail`). It also refuses to evict a live counter
  when its 8 slots are full, precisely so an attacker cannot clear a
  victim's failures. The allow-list does not weaken any of that. On top of
  it a connection may ask the loop at most `kAsksMax` = 8 login questions
  (sshd.cpp:142), and one address may hold at most `kPerPeer` = 2 key
  exchanges at once (sshd.cpp:146), each up to half a second of the SSH
  task. So an allow-listed relay still cannot grind an account.
- **The staff password: not protected by anything else.**
  `Bbs::staffPassword` has exactly one limiter, `bans_.fail` on the address.
  A wrong `BYE <password>` is a plain logoff, so a guesser gets one try per
  connection and reconnects; at two concurrent handshakes that is a few
  tries a second, forever, with nothing counting. A long random sysop
  password survives that; a dictionary word does not. The published default
  is already safe, because `staffPassword` refuses it from any non-local
  address and the relay is never local (`localNet`, guard.cpp).

**So the allow-list is one flag with three properties, not one:**

1. **never banned** — one guesser cannot lock every web caller out of a
   board, which is the whole point;
2. **never local** — it is not on the board's network, so the published
   default, the setup offer and anything else `localNet` gates stay shut.
   This needs no code: the relay's address is a public one and already fails
   `localNet`. Write it down so nobody later "helpfully" adds the relay to
   the local rule;
3. **never staff** — `staffPassword` returns `Access::None` for a session
   whose address is allow-listed, before any comparison, and says so in
   words: "staff rights are not available over this connection; use an SSH
   client or the console". That is the same decision the project already
   took for RF ("No staff over RF", CLAUDE.md ham design) and for the
   published default from outside. It costs one `if`.

That turns the allow-list from "brute-force protection off" into "this
address carries many people, so it is treated as a public lobby": accounts
keep their per-handle lockout, and the one path with no other limiter is
closed rather than left open.

**What PROXY v2 would give that this does not**, if Rob wants it later:
real per-visitor bans, the visitor's own address in `WHO`, `NODES` and the
caller log, and staff over the web if he decides he wants it. What it costs:
a new trusted input on both listening paths, parsed before the SSH
identification on 6422 where the board speaks first, accepted only from a
named address, and the address it carries flowing into `openSession`'s
`ipAddr` and `sshd::claim`'s `peer`. Both of those already take the address
as a parameter, so the plumbing is small; the risk is that it is a parser
facing the internet, and the mitigation is that it is read only from one
configured address and the connection is dropped on anything unexpected.
About 2 to 2.5 days with tests, S3 only.

🔴 The counter-framing to my own recommendation: a sysop who reads
"never banned" in CONFIG will not read the three properties, and will put
their own monitoring host or their phone's address in there and quietly lose
the ability to elevate from it. The way through is the field's own words:
call the setting **"Relay addresses"** and not "never ban", with the note
"callers from here are never banned and can never become staff", so the
trade is in the row rather than in the documentation.

### 2.2 Where the relay runs

Rob: "the relay doesn't have to sit in the directory, .com or anywhere in
particular". That is the decision that saves the most work. Options, each
with its trust consequence in one line:

| Where | Trust consequence |
|---|---|
| **Its own systemd service on the droplet, behind the .com site's Caddy route (recommended)** | Rob sees connection metadata (who, which board, when, how long, how many bytes) and nothing else; the directory process is untouched and still never dials out. |
| A second small VPS of its own | The same metadata, but a second machine to patch and pay for, for no gain while Rob runs both. |
| Inside `server.py` on the directory | Rejected: it would break the directory's own promise in README.md:67 and server.py:19, and a long-lived connection in a `ThreadingHTTPServer` thread-per-request would put the listing and `/announce` behind the relay's worst day. |
| A relay each sysop runs beside their own board | Best trust (nobody but the sysop in the path) and worst reach (no button on a listing works until that sysop runs one). Worth documenting as the option for anybody who wants it, not as the default. |
| No relay | Impossible in a general browser, see Direct Sockets above. |

**Why the droplet and not elsewhere, concretely.** The pieces are already
there, and reusing them is what keeps the directory passive:

- Caddy's `setup.sh` already reverse-proxies `127.0.0.1:8080` (the
  directory) and `127.0.0.1:8081` (the .com site). The relay is a third
  block on `127.0.0.1:8082` under a `/ssh/` path on .com.
- The .com site **already fetches `/api/boards.json` from the directory over
  loopback and caches it** (`SITE_BOARDS_URL=http://127.0.0.1:8080/api/boards.json`
  in `deploy/unleashed-site.service`, cached in `boards-cache.json`). So the
  relay's "which board is this and may I dial it" lookup needs **no database
  access, no new outbound dependency and no directory change**: it reads the
  same cache the live list reads.
- The site repo already vendors a third-party JS bundle with its own
  LICENSE, THIRD_PARTY_LICENSES and SHA256SUMS, per version
  (`vendor/esp-web-tools/10.4.0/`), and `vendor/esp-web-tools/README.md`
  documents the pattern. One sentence in that README ("This directory is the
  only third-party code in the repository") stops being true and must change
  in the same commit.

**The board id problem, and the answer that needs no directory change.**
`/api/boards.json` carries `name, owner, description, host, address, port,
nodes, busy, state, ...` and `ssh_port`, but **no id** (`board_json`,
server.py:2780). Rather than add one, the page and the relay both **derive**
the key from the cache they share:

```
key = first 16 hex of sha256("unleashed-ssh|" + address + "|" + ssh_port)
```

The browser then names a key, never a host or a port, and the relay resolves
it by walking its own cache and hashing each row. A key that matches no
listed, online, unheld, SSH-announcing board resolves to nothing, so the
relay cannot be pointed anywhere: it is structurally incapable of being an
open proxy rather than merely configured not to be. And it dials
`address` (the heartbeat source, which a board has proved it holds by
sustaining heartbeats) and never `host` (free text a board sends), which is
the rule the research got right and is worth repeating here.

---

## 3. The browser client

**Use `@microsoft/dev-tunnels-ssh` 3.12.42 with `@xterm/xterm` 6.0.0.**
Both MIT. The reasoning, with today's figures:

- **It is a library, so the site draws its own page.** sshterm is a whole
  terminal application with its own command language; a "Connect" button
  wants a login box and a terminal, not a shell inside a shell.
- **No WASM blob.** Web Crypto, so the download is JavaScript measured in
  tens of KB gzipped rather than sshterm's 5,132,663 bytes.
- **It negotiates with the board as built.** The board offers kex
  curve25519 / curve25519@libssh.org / **ecdh-sha2-nistp256**, host keys
  ssh-ed25519 / **ecdsa-sha2-nistp256**, ciphers including **aes256-gcm** and
  aes256-ctr, MACs hmac-sha2-256/512, auth `password` and `none`. The client
  has no curve25519 and no ed25519, so it will always land on
  **ecdh-sha2-nistp256 with the board's `host_ecdsa` key and aes256-gcm**.
  That is a hard dependency and it must be written beside the key: a future
  "drop P-256 to save 29.5 KB" would silently take web SSH off every board.
  The board already keeps the ECDSA key for older SyncTERM, so nothing has
  to change for it.
- **It fails closed on the host key** if the page supplies no answer, which
  is the right default to build TOFU on top of.
- **AES-CBC is out in browsers by Web Crypto's own limits**, which costs
  nothing here: the board offers only GCM and CTR.

**Fallback if it stalls**, in order: (a) connect with no SSH auth at all and
let the caller log in at the board's own prompt — the board admits any user
name that is not an account with `none` and sends it to the handle prompt
(`bbs_ssh.cpp` header), so **an unmodified sshterm may work for guests and
for account holders who type their handle on the board** rather than in SSH
auth. Unverified, and a cheap phase 1 experiment worth running because it
would cost no fork at all. Its downside is real and must be said: a password
typed at the board's prompt leaks its length and keystroke rhythm to the
relay as packet timing, where an SSH password auth sends it in one packet.
(b) fork sshterm (MIT) to add `ssh.Password` and pin it to one endpoint, at
5 MB per visitor, cached after the first.

### 3.1 Host keys: trust on first use in a browser

- **Where the check happens.** dev-tunnels-ssh raises a host-key
  authentication callback on the client session; the page must answer it, and
  if it does not the session fails rather than proceeding. The page computes
  the fingerprint itself from the server's public key in SSH wire format:
  `crypto.subtle.digest('SHA-256', keyBlob)`, base64, padding stripped,
  prefixed `SHA256:`. That is byte-for-byte the string staff already see in
  `SYS` and `HARDWARE` (`g_fp[2][56]`, sshd.cpp), so a visitor can read one
  and a sysop can read the other and they will match character for
  character. That shared format is worth protecting: do not invent a prettier
  one for the page.
- **Where it is stored.** `localStorage` on the page's own origin, one entry
  per board key: `{fingerprint, firstSeen, lastSeen, boardName}`. Properties
  a visitor should be told, because every one of them will be met:
  it is per browser and per device (a phone and a laptop each trust
  separately); clearing site data forgets it; a private window never
  remembers it; and it never leaves the browser, so the relay and the
  directory never learn which boards somebody trusts.
- **What a returning visitor sees on a change.** The session stops **before
  the password is sent**. The page shows both fingerprints, old and new,
  side by side, says in plain words that a board's key changes when its
  `userdata` is erased or it is factory reset (the host keys are deliberately
  not in the backup zip), that the sysop can read the board's own
  fingerprint from `SYS` and confirm it, and that the other reason a key
  changes is that something is in the middle. "Forget this key and trust the
  new one" is then a second deliberate action, never a default and never the
  primary button.
- **A second opinion, and what it is actually worth.** The board could
  announce its ECDSA fingerprint and the page could say "this matches what
  the board told the directory". It is honest to price that: the page is
  served by the same droplet, so a compromised droplet could lie in both
  places. It protects only the network path between the relay and the board,
  which is admittedly the realistic attack on a relay. TOFU in the browser
  is what protects against the droplet. **Not in the plan**; a one-line
  announce field if Rob wants it later.
- **The limit no web page can fix, and the page must say so.** The
  JavaScript is served by the droplet. End-to-end encryption holds against a
  curious or subpoenaed relay, a leaked log, and anyone on the droplet's
  network. It does not hold against whoever controls the web root. The
  mitigations are Subresource Integrity on the bundle, a strict CSP
  (`connect-src 'self'`, no inline script on that page), the SHA256SUMS in
  the repo so the served bundle can be checked against it, and one sentence
  telling anyone who needs more to use a native client, with the exact
  command printed beside the terminal.

---

## 4. The relay

**Language: Python 3 standard library, asyncio, no dependencies**, matching
both server.py files, which are stdlib-only. Estimate 300 to 400 lines.

The RFC 6455 subset needed is small and bounded: the handshake
(`Sec-WebSocket-Accept` = base64(SHA-1(key + GUID))), unmasking,
7/16/64-bit lengths, continuation frames, ping/pong, close. Binary frames
only; no extensions, no permessage-deflate (the payload is ciphertext and
will not compress). The honest cost is that we own a framing parser facing
the internet, so it gets its own malformed-frame tests in the site's
selftest, not just a happy path.

The alternative is [`websockets`](https://pypi.org/project/websockets/)
(BSD-3-Clause), which brings `process_request`, `origins=` and `max_size`
for free but makes this the first pip dependency on the droplet and adds a
step to `update.sh`. Worth taking **only** if the framing tests turn out to
cost more than a day. websockify is out: a process per connection and no
rate limiting at all, so every limit below would still have to be written.

**How it refuses to be an open proxy.** It does not take a host or a port,
ever. It takes a derived key (section 2.2) and resolves it against the
site's `boards-cache.json`, dialling only a board that is:

- publicly listed, `state` online, not held and not banned;
- announcing an `ssh_port` in 1..65535;
- at a **public** `address` — private, loopback, link-local and CGNAT
  results are refused outright, so a board that somehow announces from
  inside the droplet's network cannot be dialled;
- reachable over IPv4. The board's ban list and `localNet` are IPv4 only
  (`uint32_t s_addr`), so the relay must present an IPv4 address to the
  board or the allow-list cannot name it. If the relay ever gets an IPv6
  address, pin its outbound socket to v4.

Carrier-grade NAT is the one edge: boards behind a shared public address
could name a neighbour's forwarded port. The port is one somebody forwarded
to the internet on purpose, so this is a nuisance rather than a hole, and it
is why the caps below also apply per address and not only per board.

**Limits, enforced in the relay** (Caddy has no rate limiting in its
standard build), with starting figures to tune:

| Scope | Limit | Why |
|---|---|---|
| Origin | exact match on the site's own origins, or 403 | stops other sites scripting a visitor's browser into the relay. Does not stop scripts, which forge Origin; the target rule and the caps do that |
| Per visitor (`X-Forwarded-For`, trusted from loopback only, as `/announce` already does) | 2 open, 6 opens a minute, 20 an hour | |
| Per board | at most its announced node count, and **at most 2 in the key exchange at once** | matches the board's own `kPerPeer` = 2 so the relay never provokes the board's own refusal |
| Per shared address | the same as per board | the CGNAT edge above |
| Per session | 60 s to finish the handshake, 15 min idle, 2 h hard, 10 MB each way | the byte cap is what stops the relay becoming a free bulk pipe through somebody's XMODEM |
| Global | 100 open sessions, and a file-descriptor ceiling | |

**Logging**, same retention as the site's other logs: time, visitor address,
board key and dialled address, bytes each way, duration, how it ended. No
payload, which is ciphertext anyway.

**Caddy.** WebSocket upgrades pass through `reverse_proxy` with no extra
configuration and the site's automatic HTTPS makes it `wss://`. One thing
must be set that is not set today: **`stream_close_delay`** on the relay's
route (2 h, the session cap), because Caddy closes every WebSocket on a
config reload by default, and without it every web caller drops whenever
anything reloads Caddy.

---

## 5. The site

**Home: `unleashed_site` (.com and .org), not the directory.** The
directory's selftest asserts that `/directory`, `/badges` and the filtered
list carry exactly one script, which must equal a named inline string in
`server.py`, with no `src`, and that this script contains none of
`fetch`, `XMLHttpRequest`, `WebSocket`, `Storage`, `eval`, `src=` or `http`
(selftest.py:3750-3769), and that every other listed page has no `<script>`
at all. That is a deliberate, tested property of the directory and a
terminal page cannot live inside it.

- **Where the button goes.** The .com live board list already shows a
  board's SSH address on a second line as of site 1.6.3. An SSH row gains
  **"Connect in your browser"** beside the `ssh://` link, linking to
  `/ssh/<key>`. **Zero directory change** in the plan; if Rob later wants the
  button on unleashedbbs.net too, it is one plain `<a href>` to .com, which
  is a link and not an outbound connection, and the one-script invariant
  survives it.
- **The page.** Board name and address at the top; a form (handle and
  password, or "visit as a guest", autocomplete off on the password); then
  xterm.js at 80x24, with the window size following the browser. On close,
  say how it ended in words: the board's own `-->` refusals arrive as SSH
  disconnect reasons ("All SSH ports are full", "All lines are busy", "This
  board has been shut down by the sysop"), so print them rather than
  "connection closed".
- **CP437 and the probe.** xterm.js treats `write()` bytes as UTF-8 and the
  board re-encodes CP437 art for a UTF-8 terminal; xterm.js answers the
  cursor position report the detector sends, so it should detect as UTF-8
  ANSI, and the WebGL renderer draws U+2500-U+259F itself, continuous across
  cells, which is what the frames and the wordmark need. **Unverified until
  phase 1**, and it is exactly the kind of thing that works in theory.
- **The honest line, in the page's own words** (copy is `explain`'s job, not
  this report's): the connection is encrypted from the browser to the board;
  the relay passes it along and can see that you connected, to which board,
  when and for how long, but not what you type or read; the first time,
  check the board's key with its sysop if it matters to you; staff rights are
  not available this way; and a native client is the choice for anyone who
  does not want to trust a website at all, with the command printed.
- **Vendoring.** `vendor/dev-tunnels-ssh/<version>/` and
  `vendor/xterm/<version>/`, each with LICENSE, SHA256SUMS and a README
  saying where it came from and how to move it, exactly as
  `vendor/esp-web-tools/` does. The bundle is built with esbuild on a
  developer's machine and committed as an artefact; nothing is built on the
  droplet and nothing is loaded from a CDN. `THIRD_PARTY_NOTICES.md` and the
  "only third-party code in the repository" sentence both move.

---

## 6. The firmware

**Two changes, and one of them is not optional for anybody.** Both S3-relevant
only, but `never_ban` lands in shared code.

### 6.1 `ssh_port` in announce (required before any button exists)

PROTOCOL.md already specifies it and the directory already stores it; the
firmware does not send it. Add it to `buildBody` beside `sd` and `closed`,
sent **only while SSH is actually listening** (`Bbs::sshPort()`, the bound
port, not `syscfg::get().sshPort`, which is what a restart would use), so a
board whose SSH is off loses the field and the badge with its next heartbeat.

**The byte budget bites, and this is the measured detail.** `announce.cpp`
reasons its worst case out in a comment: 1,343 bytes, 1,352 on a camera
board, against `kBodyMax` 1,368. `,"ssh_port":65535` is **18 bytes**, so a
camera board's worst case becomes **1,370, which is 2 over**. Every SSH
board is an S3, and three of them (WS2, ETH, MF35) are S3 camera boards, so
this is the live case, not a corner. `kBodyMax` goes to **1,392**: **+24
bytes of static DRAM** in `g_io`, on boards with 70 to 86 KB free. Update
the comment's arithmetic in the same edit, since that comment is the budget.

Also: ANNOUNCE.md, `data/system.cfg.example` if it lists the payload, and
`test_announce_badges`, which gives the host board the worst case on purpose
and must be given the new one.

### 6.2 The relay allow-list

A new `system.cfg` key and `SysConfig` field, on the CONFIG network page
beside `ssh_port`. The mechanics are the project's existing four-site
pattern: the field in `sysconfig.h`, the range row and the parse arm in
`sysconfig.cpp` (as `ssh_port` has at 222 and 565), the CONFIG row in
`bbs_sysop.cpp` (1040) and the `cfgFileValue` arm (1556).

- Key `relay_addrs`, a comma list of up to **4** dotted quads. Row label
  "Relay addresses" at 80 and "Relays" at 40; note: "Callers from here are
  never banned and can never become staff."
- Stored parsed, `uint32_t[4]` plus a count, and rendered back with
  `ipToText` for the form, so no text copy is kept.
- Read in **one** place, the way `localNet` is one place:
  `bool relayAddr(uint32_t)` beside it in `guard.h`. Then
  `BanList::banned` and `BanList::fail` return false for it (never banned,
  never counted), and `Bbs::staffPassword` returns `Access::None` for it
  before any comparison, with a log line and a word to the caller.
- **Size: about 20 bytes of static DRAM** (16 for the table, 1 for the
  count, padding) and an estimated 400 to 700 bytes of flash for the parse,
  the render and three checks. The flash figure is an estimate and the rule
  here is to measure rather than assert, so `optimize` reads the real delta
  off the ELF for `esp32dev` and `esp32cam_aithinker` before the commit. The
  ESP32-CAM is the board to watch: it had 2,720 bytes of static DRAM free at
  1.2.0, so 20 bytes is comfortable but it is the one that would complain.
- Open for Rob: whether to gate the whole thing on `BBS_HAS_SSH` and keep
  the three ESP32 images byte-identical. My view is **do not gate it**:
  `relay_addrs` is useful to any sysop who fronts their board with anything,
  20 bytes is affordable on the tightest board in the family, and a setting
  that exists on some boards and not others is a documentation problem
  forever.

### 6.3 Not in this plan, but queued in passing

The board advertises `aes192-gcm@openssh.com` and `aes192-ctr` while
AES-192 is compiled out (`NO_AES_192`), so a client that ranked aes192 first
would negotiate it and then fail. No known client does, dev-tunnels-ssh
included (it offers only aes256-gcm and aes256-ctr), so it bites nobody
today. One line, `wolfSSH_CTX_SetAlgoListCipher`. It is already on the 1.2.1
queue and should stay there rather than ride in on this.

---

## 7. Phases, estimates, and what needs hardware

Each phase is shippable and testable on its own. **Every test plan in here
needs Rob's explicit yes before it runs**; code review runs on all of it
without asking, before any testing.

| # | Phase | Days | Hardware? | Ships |
|---|---|---|---|---|
| **0** | **Firmware: `ssh_port` in announce**, `kBodyMax` 1,392, ANNOUNCE.md, the badge test's worst case | **0.5** | Host to build and test; **one bench S3 flash** to see a real heartbeat carry it and the directory badge it | firmware 1.2.2; the `ssh` badge starts working for real |
| **1** | **The proof of path.** esbuild bundle of dev-tunnels-ssh + xterm.js on a throwaway page, a 150-line throwaway relay on 127.0.0.1, against `bbs_host_s3` (which runs the real wolfSSH server). Prove: ecdh-nistp256 + `host_ecdsa` + aes256-gcm negotiated, password auth, pty, resize, **the detector resolving to ANSI/UTF-8**, CP437 art drawn, and the **measured** minified+gzipped bundle size. Also try the no-SSH-auth route (section 3) because it would make sshterm a free fallback | **1 to 1.5** | **None** | nothing but a measurement note in `internal/`. This is the phase that is allowed to kill the plan |
| **2** | **Firmware: the allow-list**, three properties, one `relayAddr` rule, CONFIG row, host tests, `optimize` reads the real size off the ELF | **1** | Host to build and test; **one bench S3 flash** at the end | firmware; a board can safely sit behind a relay |
| **3** | **The relay.** stdlib asyncio on 8082, framing, key resolution against `boards-cache.json`, every limit in section 4, logging, systemd unit, Caddy route with `stream_close_delay`; malformed-frame and limit tests in the site's selftest | **2.5 to 3** (+1 for the tests) | **None** | the relay, switched on with nothing pointing at it |
| **4** | **The page.** `/ssh/<key>` on .com, vendored bundles with SHA256SUMS and READMEs, the login form, TOFU and the key-changed screen, the disconnect wording, SRI and CSP, new selftest entries for the one page that has scripts, `web-qa` at phone and desktop widths | **2 to 3** | **None** | the page, reachable by hand-typed URL |
| **5** | **The buttons and the words.** "Connect in your browser" on the .com list's SSH rows, the honest copy (`explain`), the docs site, `vendor/.../README.md` and `THIRD_PARTY_NOTICES.md`, and the "only third-party code" sentence | **0.5 to 1** | **None**, then **Rob's own end-to-end call to a bench board from a browser** as the gate | it is live |
| **6** | *Optional, later:* **PROXY v2.** Real per-visitor bans, the visitor's address in `WHO` and the caller log, staff over the web if Rob wants it | **2 to 2.5** | **Bench S3** | firmware |
| | **Total, phases 0-5** | **8.5 to 12.5** | | |

Sequencing notes worth keeping:

- **Phase 1 before anything is committed to a live repo.** It is the cheap
  disproof: if xterm.js does not satisfy the board's detector, or the bundle
  is 400 KB gzipped, that is better known on day one than on day nine. The
  research left both unverified and so does this plan.
- **Phase 0 can run in parallel with phase 1**, different trees, different
  people. Nothing else can start before phase 1 reports.
- **Phases 3 and 4 can run in parallel** once phase 1 is green: they meet at
  one interface, `wss://<origin>/ssh/<key>` carrying binary frames and
  nothing else.
- **Phase 2 gates going live, not phase 3 or 4.** A relay and a page with no
  firmware allow-list are testable against a bench board all day; they must
  not be pointed at a listed board.

---

## 8. What it must never do

Written as the acceptance list, because each line is a thing a later change
could break quietly:

- **Never carry plaintext.** The relay copies bytes between a WebSocket and
  a TCP socket and parses nothing above the frame layer. No `-k`, no
  "debug mode" that logs payload, no feature flag for telnet. If the day
  ever comes that somebody wants browser telnet, it is a different product
  with a different promise and a different page.
- **Never reach anything but a listed board.** No host or port from the
  client, not even behind a signature. The key resolves against the cache or
  it resolves to nothing. Private, loopback, link-local and CGNAT targets
  refused. `address`, never `host`.
- **Never let a browser caller past a ban a telnet caller could not pass.**
  The allow-list exempts the *relay's* address from being banned; it must
  never be read as "callers from here skip the ban check" in any other
  sense. Concretely: the per-handle `LoginGuard` lockout still applies, the
  closed-board sign still applies, `kPerPeer` and `kAsksMax` still apply, a
  shut-down board still refuses, and the published default is still refused
  because the relay is not local. And the one thing the ban was the only
  guard for, the staff password, is closed outright rather than left
  unguarded.
- **Never let the directory dial out.** The relay is a different process
  with a different promise. README.md:67 and server.py:19 stay as written.
- **Never load the client from a CDN**, and never serve a bundle whose hash
  is not in the repo.

---

## 9. The honest risks

- **What the relay still sees**: the visitor's address, which board, when,
  for how long, how many bytes each way. Keystroke timing and packet sizes
  too, which is a known side channel against interactive SSH; it matters
  most for a password typed at the board's own prompt rather than in SSH
  auth, which is why the page should use SSH password auth when it can. It
  never sees the handle, the password or anything on screen.
- **What the visitor is actually trusting**: the droplet to serve honest
  JavaScript. End-to-end holds against a curious relay, a leaked log and the
  droplet's network; it does not hold against whoever controls the web root.
  Every in-browser crypto design has this property and the page must say so
  rather than imply otherwise. SRI, a strict CSP, and published hashes are
  the mitigation; a native client is the answer for anyone who wants more.
- **If the relay is down**, the button fails and nothing else does: the
  `ssh://` line, the telnet line and every native client keep working. That
  is worth designing for rather than discovering, and it is why the native
  command is printed on the page beside the terminal and why the button is
  an addition to the existing address lines rather than a replacement for
  them.
- **A new dependency for web callers on the droplet being up**, which the
  directory has so far carefully avoided for boards. It is a real change in
  the project's shape and it is Rob's to accept; the mitigation is that
  nothing about a board's own reachability changes.
- **We own a WebSocket framing parser facing the internet.** Mitigated by
  binary-frames-only, no extensions, size caps before allocation, and
  malformed-frame tests in the selftest. The escape hatch is `websockets`
  (BSD-3) if the parser proves to cost more than it saves.
- **Bandwidth is not the constraint, and the figure is an estimate.** A text
  session is on the order of under 1 KB/s average from this project's own
  measured redraws (1.2 KB for a PETSCII form, 2,476 bytes for a 16-row
  ANSI form at 80) plus SSH and WebSocket framing. The per-session byte cap
  is what bounds the one case that would matter, a file transfer.
- **Client maintenance.** dev-tunnels-ssh exists to serve Microsoft's dev
  tunnels, not us. Six weeks quiet today, MIT, and vendored, so a
  maintenance stop is survivable rather than urgent; but it is a library
  with one real consumer and that is worth knowing. The fallback is named
  (section 3) and it is a fork of something more actively released.
- **The hard dependency on the board's ECDSA host key** (section 3), which
  must be written beside the key or a future size saving breaks web SSH on
  every board at once.

---

## 10. What needs Rob rather than me

1. **The allow-list's third property: no staff elevation from a relay
   address.** My recommendation, and the only thing that stops the
   allow-list removing the one rate limit the staff password has. It means
   Rob cannot `BYE <password>` from a browser. He may want the opposite,
   in which case PROXY v2 (phase 6) moves from optional to required and the
   plan grows two days.
2. **Where the relay runs.** My recommendation: its own service on the
   droplet, behind the .com site. The alternatives and their trust
   consequences are in section 2.2.
3. **Whether the directory gets a plain `<a href>` button too**, or whether
   the button lives only on .com. The plan assumes .com only, so the
   directory changes nothing.
4. **The per-session byte cap.** 10 MB each way blocks a large download
   through the browser. Higher is a policy choice about what the droplet is
   for.
5. **PROXY v2: ever, or no?** It is the only way a web caller's own address
   reaches `WHO`, the caller log and the ban list.
6. **The test plans**, each one, before it runs: the phase 1 spike against
   `bbs_host_s3`, the phase 2 host runs and bench flash, the relay's
   selftest additions, `web-qa` on the page, and Rob's own end-to-end call
   as the phase 5 gate.
7. **Whether `relay_addrs` is gated on `BBS_HAS_SSH`**, which would keep the
   three ESP32 images byte-identical at the cost of a setting that exists on
   some boards and not others. My view is not to gate it.

---

## 11. Sources

Re-read on 2026-10-04 unless noted.

- [@microsoft/dev-tunnels-ssh on npm](https://www.npmjs.com/package/@microsoft/dev-tunnels-ssh),
  [registry JSON](https://registry.npmjs.org/@microsoft%2fdev-tunnels-ssh),
  [repo](https://github.com/microsoft/dev-tunnels-ssh),
  [commits API](https://api.github.com/repos/microsoft/dev-tunnels-ssh/commits),
  [jsDelivr file sizes](https://data.jsdelivr.com/v1/packages/npm/@microsoft/dev-tunnels-ssh@3.12.42)
- [xterm.js releases](https://api.github.com/repos/xtermjs/xterm.js/releases),
  [@xterm/xterm registry](https://registry.npmjs.org/@xterm%2fxterm)
- [c2FmZQ/sshterm releases](https://api.github.com/repos/c2FmZQ/sshterm/releases),
  [stuicey/SSHy](https://api.github.com/repos/stuicey/SSHy)
- [Direct Sockets for Isolated Web Apps](https://developer.chrome.com/docs/iwa/direct-sockets),
  [Intent to Ship: Direct Sockets API](https://groups.google.com/a/chromium.org/g/blink-dev/c/5R0P_aYBWQI)
- [`websockets` on PyPI](https://pypi.org/project/websockets/)
- In-tree, read directly: firmware `src/core/guard.{h,cpp}`,
  `src/core/bbs_ssh.cpp`, `src/core/sshd.cpp`, `src/core/bbs.cpp`,
  `src/core/bbs_shell.cpp`, `src/core/sysconfig.h`, `src/core/sysconfig.cpp`,
  `src/core/bbs_sysop.cpp`, `src/plugins/announce.cpp`, `src/config.h`,
  `src/board.h`; directory `server.py`, `selftest.py`, `PROTOCOL.md`,
  `README.md`, `CHANGELOG.md`; site `server.py`, `selftest.py`,
  `deploy/setup.sh`, `deploy/unleashed-site.service`,
  `vendor/esp-web-tools/README.md`, `CHANGELOG.md`
- The research this replaces:
  `internal/research-web-ssh-2026-09-28.md` on branch `research/web-ssh`
