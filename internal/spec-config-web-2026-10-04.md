<!--
 ===========================================================================
  µnleashed BBS
  Electronic freedom on a microcontroller.
 ===========================================================================

 File:         internal/spec-config-web-2026-10-04.md
 Module:       Working notes / design

 Purpose:      How a board and the web relay keep browser callers from
               being an abuse path: which side owns which limit, the
               escalating waits and their decay, the lock, and CONFIG web.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v3 or later
 SPDX-License-Identifier: GPL-3.0-or-later
 ===========================================================================
-->

# Browser callers, abuse, and CONFIG web (2026-10-04)

Rob's direction, verbatim: "You can lock the web and rate limit number of
calls for config web items use tier escalation of time between attempts and
fall back when stopped. Source ip connect rate limits as well. Implement and
use config web, typical options and time configuration. Bbs can implement
this or web."

Design only. No code, config, test or doc was changed. Read against firmware
`main` at d807e2b (BBS_VERSION 1.2.1), `internal/plan-web-ssh-2026-10-04.md`,
and in-tree `src/core/guard.{h,cpp}`, `src/core/bbs.cpp`,
`src/core/bbs_ssh.cpp`, `src/core/sshd.cpp`, `src/core/bbs_shell.cpp`,
`src/core/bbs_sysop.cpp`, `src/core/sysconfig.h`, `src/main.cpp`, and the
site's `deploy/unleashed-site.service`.

---

## 0. Bottom line

- **The answer to "BBS or web" is both, and the split is forced by what each
  side can see.** The relay sees addresses and connections and cannot see a
  failed login. The board sees every failed login and, until the PROXY
  protocol exists, sees only the relay. Neither side can enforce the other's
  limit, and neither needs to tell the other anything.
- **One design rule carries most of this: for a shared address, the answer is
  delay, not refusal, and never a ban.** A ban costs an innocent visitor
  everything and costs an attacker a reconnect from somewhere else. A wait
  that doubles costs an innocent visitor seconds and costs an attacker their
  whole attempt rate. That is why no part of this adds the relay to the ban
  list and why `BanList` is left exactly as it is.
- **Two independent escalators, one ladder.** The relay waits per visitor
  address (2 s doubling to 60 s); the board refuses per relay address after
  failed logins (15 s doubling to 5 min). Both decay by one step per calm
  period. The relay's bites the attacker first, because an attacker is one
  address; the board's is the backstop for a distributed attack, and it
  degrades the web for everyone rather than locking anybody out, which is
  the honest trade and the reason the lock exists.
- **The board's escalator has an exact input, not a heuristic.** `bbs.cpp:1059`
  already computes "this SSH connection tried passwords and got none right"
  (`sshd::wrongPasswords(s.link) && !s.sshPwOk`). Today that calls
  `bans_.fail`. For a relay address it calls the web escalator instead. No
  new signal, no traffic analysis, no new plumbing.
- **Nothing in it runs on the loop.** Every tier is evaluated lazily at
  accept from `plat::since`, in the refusal path `acceptSsh` already has. No
  timer, no sleep, no new work in `tick()`. Rule no. 1 is satisfied by
  construction rather than by measurement.
- **Cost: about 200 bytes of static DRAM on an S3, about 132 on a board with
  no SSH**, because every web-specific field sits inside `#if BBS_HAS_SSH`
  and the only piece the floor board pays for is the per-source connect
  table, which helps a board that has no web at all. The arithmetic is
  section 7; flash is an estimate until `optimize` reads the ELF.
- **This does not reopen staff from the web.** Section 8.

---

## 1. What each side can see, checked rather than assumed

| Fact | Who sees it | Why |
|---|---|---|
| A TCP connection opened, from which address | **relay** (and the board, as the relay's address) | the relay is the TCP client to the board and the WebSocket server to the browser; the visitor's address arrives as `X-Forwarded-For` from Caddy over loopback, which the site already trusts from loopback only |
| How long a connection lasted, how many bytes each way | **relay** | it copies the bytes |
| That a login failed | **board only** | password authentication happens inside the encrypted transport, after key exchange. The relay holds ciphertext. There is no SSH message before the kex that carries an auth outcome |
| Which handle was tried | **board only** | same reason |
| The visitor's own address | **relay only**, until PROXY v2 | the board's socket peer is the relay |
| How many lines the board has free | **board**; the relay only what the board announced | `/api/boards.json`'s `nodes` and `busy`, up to a minute stale |

**Can the relay learn an outcome without seeing plaintext?** Three routes
were considered and all three are rejected, which is why the board owns
failure counting:

- **Traffic analysis.** A successful login is followed by a channel and a
  burst of kilobytes (the welcome screen and the motd); a failed one closes
  after a few hundred bytes. So "fewer than about 2 KB from the board" is a
  usable proxy for "never got in". It is **not** an outcome: a board that is
  full, shut down or closed produces the same shape, and so does a visitor
  who closed the tab. Used as a strike it would punish honest people.
  **Used the other way round it is sound and costs nothing**, and that is
  where it survives in this design: a session that looks like a real call
  (bytes and duration, section 3.2) is what takes the relay's own tier down
  a step. Proving good faith by a heuristic is safe; accusing by one is not.
  It must never be logged or shown as "failed login".
- **The board telling the relay.** New outbound network from the board to a
  host it does not control, a new credential, a new thing to spoof, and a
  Rule no. 1 problem on a WROOM-shaped budget. Rejected; it would also break
  the property that a board needs nothing but its own settings.
- **The relay decrypting.** That is the product this is not.

---

## 2. The split: who owns which limit

| Limit | Lives | Counts | Default | Range |
|---|---|---|---|---|
| Concurrent connections per visitor address | **relay** | open WebSocket sessions | 2 | 1-4 |
| Opens per visitor address per minute | **relay** | WebSocket opens | 6 | 1-60 |
| Opens per visitor address per hour | **relay** | WebSocket opens | 20 | 1-500 |
| Escalating wait per visitor address | **relay** | opens over the allowance | 2 s doubling, cap 60 s | section 3.2 |
| Concurrent sessions per board | **relay** | its own sessions to that board | the board's announced `nodes`, and 2 in the handshake at once | matches `kPerPeer` = 2 (sshd.cpp:146) |
| Concurrent sessions in total | **relay** | all sessions | 100 | plus an fd ceiling |
| Handshake, idle, hard and byte caps per session | **relay** | 60 s / 15 min / 2 h / 10 MB each way | | from the plan, section 4 |
| **Web lines: web callers a board will hold at once** | **board** | sessions whose address is a relay | 2 | 1..`BBS_SSH_MAX` |
| **Escalating refusal after failed web logins** | **board** | SSH connections from a relay that tried passwords and got none right | 3 failures, then 15 s doubling, cap 5 min | section 3.1 |
| **The lock, and web hours** | **board** (enforced) and the listing (advertised) | | section 4 | |
| **Connects per minute per source address** | **board** | accepts, telnet and SSH, relay addresses skipped | 20 | 0 (off) to 120 |
| Login questions per SSH connection | **board, exists** | `kAsksMax` = 8 (sshd.cpp:143) | | not a setting, see below |
| Password tries per connection | **board, exists** | 3 | | not a setting |
| Wrong passwords per handle | **board, exists** | `LoginGuard`, 5 in 15 min locks the handle 15 min | | `BBS_LOCK_*` |

**The rule behind that table, worth stating because a later change will test
it: every limit a board cares about is enforced on the board. The relay's
limits exist to be kind to boards, not to protect them.** A compromised or
lying relay can under-report `X-Forwarded-For` and ignore every figure in its
own configuration; it cannot exceed `web_lines`, pass the lock, call outside
the web hours, or escape the board's escalator, because those are counted
from what the board's own accept path sees.

**Two things deliberately not added**, because a setting that exists is a
setting that gets set wrong:

- **A global connect-rate limit on the board.** The node count and the busy
  line already bound total concurrency, and overflow callers get `BUSY` and a
  drop. A rate limit over the top would refuse honest callers during a rush
  (a post on the directory, a release day) and buy nothing: an accept is
  cheap and the loop's worst pass has never been one.
- **`kAsksMax` and the three password tries as settings.** Nobody needs to
  tune them, they are already tight, and loosening either is the only change
  a sysop could make that would help an attacker.

---

## 3. The tiers, concretely

Both ladders are the same arithmetic and the same vocabulary as the ETH
board's Wi-Fi redial (`src/main.cpp` 126-178, 1.2.1-eth.2/eth.3), which is
the project's existing backoff: a `gap` that is the next wait, an `at` that
is when to act again with 0 meaning "nothing waiting", `if (!at) at = 1;`
because 0 is the sentinel, a doubling clamped to a maximum, and a success
resetting it. Elapsed time is **always** `plat::since(now, at)`
(platform.h:72), never a plain or a signed difference: `windowFrom` in
guard.cpp carries the comment explaining why, and the ETH redial was fixed
for the same reason.

**Where it diverges from the redial, and why.** The redial resets to zero on
a join. Neither ladder here resets to zero on one success: a relay address is
shared, so one attacker holding one valid account could log in once a cycle
and keep the tier at zero for everybody. A success steps the tier **down
one**, and time steps it down one per calm period. `BanList::clear` on a
correct staff password (bbs_shell.cpp:3537) is the existing
clear-on-success precedent and it is right there, because that entry is one
person's address. This one is not.

### 3.1 The board: failed web logins

- **Counted:** an SSH connection whose address is a relay (`relayAddr`) that
  ended having tried at least one password with none right. That is exactly
  the test `bbs.cpp:1059` already makes; for a relay address it steps this
  counter instead of calling `bans_.fail`.
- **Not counted:** a connection that tried no password; a refusal (full, shut
  down, closed, locked); a wrong answer to the login's sysop question, which
  a web caller is **never asked** (section 8). Counting a question that can
  never grant anything would hand anybody a way to slow the web by typing
  rubbish.
- **Window:** `BBS_WEB_WINDOW_MS` = 900,000 (15 minutes), the same window as
  the ban and the handle lockout, so a sysop learns one number.
- **Trigger:** every `web_fails` (default 3) counted failures inside the
  window steps the tier up one.
- **The ladder:** tier 1 = `web_slow` seconds (default 15), doubling,
  clamped to `web_slow_max` (default 5 minutes): 15, 30, 60, 120, 240, 300,
  300, ... The clamp is written the redial's way,
  `gap * 2 > max ? max : gap * 2`.
- **What the wait does:** while it is running, `acceptSsh` refuses a new
  connection from a relay address in the existing `refuse()` path, with the
  seconds in the words. Nothing sleeps and nothing is queued; the refusal is
  one DISCONNECT in the clear and a close, which that path already does for
  "all lines are busy".
- **Fall-back when it stops:** one step down for every `BBS_WEB_CALM_MS` =
  300,000 (5 minutes) with nothing counted, computed lazily at the next
  accept: `steps = plat::since(now, lastFail) / BBS_WEB_CALM_MS`, tier
  reduced by `steps`, and `lastFail` advanced by `steps * BBS_WEB_CALM_MS` so
  the remainder is not thrown away. At tier 0 the entry is forgotten. A web
  caller who logs in successfully also steps it down one.
- **It is never a ban.** The cap is `web_slow_max` and there is no path from
  this counter to `BanList`. A sysop stops it outright with the lock
  (section 4) or clears it with `UNBAN <relay address>`, which already exists
  and already means "forget what you have against this address"; it should
  clear this counter in the same call.
- **Worst case for a visitor who did nothing wrong:** up to
  `web_slow_max` of "not now", with their account untouched, their address
  unbanned, and telnet and native SSH unaffected.

### 3.2 The relay: opens per visitor

- **Key:** the visitor's address, IPv4 /32 and IPv6 **/64**, which is the
  dedupe unit the directory already uses for listings, so the two agree.
- **Allowance:** 2 concurrent, 6 opens a minute, 20 an hour.
- **Over the allowance the next open waits, it is not refused.** The browser
  shows "connecting", a person waits a moment, and a script's attempt rate
  collapses. **Only one waiting open per visitor**; further opens while one
  waits are refused at once with the retry-after, so waiting cannot be turned
  into a way to hold sockets.
- **The ladder:** 2 s, doubling, cap 60 s: 2, 4, 8, 16, 32, 60.
- **Fall-back:** one step down per 2 minutes with no new open. A session that
  **looked like a real call** also steps it down one: at least 2 KB from the
  board and at least 30 s open. Those two figures are starting points to tune
  from the relay's own log, and the heuristic is only ever used to reduce a
  tier, never to raise one (section 1).
- **Per board:** at most the board's announced `nodes`, at most 2 in the
  handshake at once so the relay never provokes the board's own `kPerPeer`
  refusal, and at most the board's `web_lines` if the board announces it.
- **Logged** as the plan says: time, visitor address, board key, dialled
  address, bytes each way, duration, how it ended. No payload, and no
  guess about whether a login worked.

### 3.3 The board: connects per source address

Rob's "source ip connect rate limits as well", and this is the one piece that
is not about the web at all: it helps every board, the WROOM included.

- **Counted** in both accept paths (`bbs.cpp` acceptAll, `bbs_ssh.cpp`
  acceptSsh), per address, after the ban check.
- **Relay addresses are skipped**, because they carry many people and the
  relay's own per-visitor ladder is what rations them. Skipping them here is
  the same decision as not banning them, for the same reason.
- **Allowance:** `connects_min` new connections a minute (default 20, 0 turns
  it off).
- **Over it:** the connection is refused with the seconds in the words, and
  the tier steps up. **The same ladder as 3.1** (15 s doubling to 5 minutes)
  and the same calm period, so the board has one ladder and two counters
  rather than two ladders. A connection that logs in successfully steps it
  down one; an address at tier 0 is forgotten.
- **Table:** `BBS_RATE_SLOTS` = 8, matching `BBS_BAN_SLOTS`, with
  `BanList::slotFor`'s eviction rule copied: reuse an empty slot, else the
  stalest entry that is not currently waiting, and **never evict a live
  waiting entry**, which is the hole `LoginGuard::fail` already closes by
  refusing to evict a live counter.
- **Honest limit:** eight slots means a flood from more than eight addresses
  at once evicts. That is the same eight-slot honesty `BanList` and
  `LoginGuard` already have, and the fix if it ever matters is the slot count,
  not the design.

---

## 4. The lock

**What "lock the web" means: the board refuses connections whose source is a
relay address, and says so.** Not a ban (the relay must stay reachable for
other boards), not a listener change (SSH is still there for native clients),
and nothing a visitor can mistake for the board being down.

- **One switch, and a schedule beside it.** `web` is yes/no. `web_from` and
  `web_until` are web hours, local, blank for none, in exactly silent hours'
  shape (`SysConfig::silentFrom/silentUntil`, `int16_t` minutes since local
  midnight, -1 for none, `board::fmtTime` for the form, both ends or
  neither). Like silent hours, the hours need a clock: with no NTP time they
  do nothing and only the switch applies. Reusing that shape means one
  pattern for "a thing that is only true at certain times" rather than two.
- **Blank `web_relays` is also off**, and that is the useful default
  behaviour: a board that has never been told the relay's address cannot be
  reached through it in any case, so the switch's default matters only to a
  sysop who has pasted an address in.
- **What a locked-out visitor is told.** The board's own words, as one SSH
  DISCONNECT in the clear through `sshd::refusal`, which is how every other
  refusal on that port already arrives and which the plan's page already
  prints verbatim instead of "connection closed":
  - locked: `--> This board does not take calls from the web`
  - outside the hours, with a clock: `--> This board takes web calls from
    09:00 to 23:00`
  - web lines full: `--> All web lines are busy; a native SSH client can
    still call`
  - slowed: `--> Web calls are waiting 30 s after failed logins`

  Those are drafts of the information, not the copy. `explain` owns the
  wording and `tty-ux` owns the widths (section 11).
- **Advertised as well as enforced, and the two halves are different jobs.**
  Enforcement must be on the board, because it is the board's policy. But a
  button that appears and then refuses is a bad page, so the board should
  also say "no" in its announce (`web`, true only while `web` is yes, the
  relays are set and SSH is actually listening, exactly as `ssh_port` is only
  sent while SSH is bound), the directory should carry it and the site should
  omit the button. That is a payload field on a budget that has just gone to
  1,392 bytes for `ssh_port`, so it is Rob's call (section 10).
- **A live staff command, not only a form.** `WEB ON` / `WEB OFF`, sysop,
  live, writing the setting the way the CGNAT row already saves live. A sysop
  under attack should not have to walk into a CONFIG form; and `WEB` with no
  argument is the natural place for the status (section 11).

---

## 5. CONFIG web's rows

The project's own shape: `{ key, label, kind, lo, hi, cap, note, wide,
wideNote }`, label 9 characters at 40 columns, `wide` 20 at 80, `note` 38 on
the status line at 40, `wideNote` 78 at 80. Number ranges stay 0,0 on a core
page and belong to the parser (`syscfg::trial`), because a second copy of a
range beside the table is exactly what drifted on `backup_window_minutes` and
`who_refresh_max`.

Eight rows, against `Form::kMaxFields` = 16, so there is room for the
schedule to grow without a sub-page.

| # | Key | 9-col label | 20-col label | Kind | Default | Range | Note (38) / wide note (78) |
|---|---|---|---|---|---|---|---|
| 1 | `web` | `Web calls` | `Web browser calls` | YESNO | Rob's call (section 10) | yes/no | "Yes: callers may come from a browser." / "Yes: callers may reach this board from a browser through the relay." |
| 2 | `web_relays` | `Relays` | `Relay addresses` | TEXT, cap 63 | blank | up to 4 dotted quads, comma separated | "Up to 4 addresses, comma separated." / "Up to 4, comma separated. Never banned, never staff. Blank: no web calls." |
| 3 | `web_lines` | `Web lines` | `Web callers at once` | NUM, cap 2 | 2 | 1..`BBS_SSH_MAX` | "Web callers at once. 1 to 8." / "How many SSH lines the web may hold at once. The rest stay for clients." |
| 4 | `web_from` | `Web at` | `Web calls from` | TEXT, cap 5 | blank | HH:MM | "HH:MM, local. Blank: any time." / "Web calls are taken from this time. Blank: any time. Needs the NTP clock." |
| 5 | `web_until` | `Web to` | `Web calls until` | TEXT, cap 5 | blank | HH:MM | "HH:MM, local. Blank: any time." / "And until this time. Both or neither, like the silent hours on CONFIG board." |
| 6 | `web_fails` | `Failures` | `Slow after failures` | NUM, cap 2 | 3 | 0 (never) to 20 | "Failed web logins before slowing." / "Failed logins through the relay before new web calls are made to wait." |
| 7 | `web_slow` | `Slow for` | `First wait` | NUM, cap 3 | 15 | 5 to 300 seconds | "Seconds the first wait lasts." / "Seconds the first wait lasts. It doubles with each further run of failures." |
| 8 | `web_slow_max` | `Slow max` | `Longest wait` | NUM, cap 2 | 5 | 1 to 60 minutes | "Longest wait, in minutes." / "The cap the doubling stops at. The web is never banned, only made to wait." |

Notes on the table, each of which is a decision rather than a detail:

- **`web_relays` lives here, not on CONFIG network.** The plan put it there as
  `relay_addrs` beside `ssh_port`. One page should own the web, and the row's
  three properties (never banned, never local, never staff) read as a web
  policy rather than a network setting. Nothing has shipped, so the rename is
  free. The 🔴 in the plan still stands and is answered by the row's own
  words: a sysop who puts their own monitoring host in here loses the ability
  to elevate from it, and the note says so where they are typing.
- **Row 1 is live; rows 2 to 8 are live too.** Nothing on this page needs a
  restart: none of it binds a socket. That is unlike the rest of CONFIG
  network and the page should say so in its one-line description in the page
  list, the way the CGNAT row's verdict already does.
- **The time pair is validated as a pair** (both or neither), the way
  `silent_from`/`silent_until` already are in `configSave` (the
  `silent_`-prefix pair check, bbs_sysop.cpp 2900 and 2942). Copy that rule
  rather than writing a second one.
- **The page list entry:** `CFG_PAGE("web", "WEB CALLS", "browser callers,
  relays, hours", kWeb)`, after `photos` so no row on any page above it moves
  and nothing a guide or a test counts down to changes.
- **One row goes on CONFIG network, not here**, because it is not about the
  web: `connects_min` / `Conn/min` / `New calls a minute` / NUM, default 20,
  0 to 120, note "New calls a minute, one address." / "New connections one
  address may start a minute. Over it they wait. 0: off." It goes last on
  that page, after `wifi_with_ethernet`, for the same no-row-moves reason.

### 5.1 A board with no SSH

The relay dials SSH, so a board with no SSH can have no web callers. Of the
nine rows:

- **All eight CONFIG web rows are S3-only**, and the whole page is compiled
  out behind `#if BBS_HAS_SSH`, as the `ssh_port` row on CONFIG network
  already is. An S3-only feature is left out of the base build entirely, not
  switched off, because a switched-off plugin still costs flash and its
  statics.
- **But the sysop still sees that it exists**, per Rob's rule of 2026-10-01:
  CONFIG's page list shows `web` dimmed with one line, "Web browser calls
  need an ESP32-S3 board (SSH)". Strings only, a few bytes of flash, no RAM,
  a sysop screen and never a caller's. **The page list has no pattern for a
  dimmed entry today** (the greying so far is per row), so this is one thing
  `tty-ux` is asked for in section 11.
- **`connects_min` is on every board**, the WROOM included. It is the only
  limit here that helps a board that will never see a web caller, it costs
  the one table, and a setting that exists on some boards and not others is a
  documentation problem forever.

---

## 6. The relay's own configuration, and knowing the two agree

The relay reads its settings from its unit's environment, the way both
`server.py` files do, so the code needs no deployment-specific edit
(`deploy/unleashed-site.service` is the pattern to copy, including the
hardening block and `SITE_TRUSTED_PROXIES=127.0.0.1,::1`):

```
RELAY_HOST=127.0.0.1
RELAY_PORT=8082
RELAY_ORIGINS=https://unleashedbbs.com,https://unleashedbbs.org
RELAY_TRUSTED_PROXIES=127.0.0.1,::1
RELAY_BOARDS=/var/lib/unleashed-site/boards.json
RELAY_VISITOR_OPEN=2
RELAY_VISITOR_MIN=6
RELAY_VISITOR_HOUR=20
RELAY_WAIT_FIRST=2
RELAY_WAIT_MAX=60
RELAY_CALM=120
RELAY_GOOD_BYTES=2048
RELAY_GOOD_SECONDS=30
RELAY_BOARD_HANDSHAKES=2
RELAY_GLOBAL=100
RELAY_HANDSHAKE=60
RELAY_IDLE=900
RELAY_HARD=7200
RELAY_BYTES=10485760
```

`RELAY_BOARDS` is the site's existing cache, read-only: no database access,
no new outbound dependency, no directory change.

**How a sysop knows the two agree.** There is no protocol between them and
there should not be, so agreement is established by the board reporting what
it has seen and the site printing what to paste:

- **The board says what it sees.** `WEB` with no argument, staff: the
  configured relay addresses; for each, whether a call has ever arrived from
  it since boot and when the last one was; web callers on now against
  `web_lines`; the lock and the hours, with whether the clock is valid; and
  the current slow tier with its remaining wait. `web = yes` with relays
  configured and nothing ever received **is** the disagreement signal, and it
  is the only one that needs no coordination. The same block belongs under
  `BANS`, which is where a sysop already goes to ask "what is the board
  holding against whom".
- **The site prints the exact value to paste**, on the `/ssh` page and in the
  guide: "put this in CONFIG web, Relays: a.b.c.d", with the guide's "Applies
  to versions" line moving whenever it changes.
- **When the relay moves address**, every web call is refused as an ordinary
  SSH caller, and because it is then not exempt it can be banned. The symptom
  a sysop sees is "the button stopped working"; the thing that names the new
  address is the board's own refusal log line, which already prints the
  address, and `BANS`.
- **What stops a guessed address becoming a privilege:** nothing in
  `web_relays` grants anything. It removes the ban, which is a shared-address
  concession, and adds two restrictions (never local, never staff). An
  attacker who got their own address into a sysop's `web_relays` would gain
  exemption from the ban and lose the ability to become staff, which is a bad
  trade for them and the right shape for us.

---

## 7. The cost on the board

Static DRAM, field by field, because the ESP32-CAM had 2,720 bytes free at
1.2.0 and the reference WROOM is the floor. Nothing here touches `Session`:
every check is computed from `s.ipAddr`, so the "every byte of a Session
costs twelve" rule is not engaged at all. That is deliberate and it is the
first thing to preserve if any of this is redesigned.

**`SysConfig`, one static instance.** Web fields inside `#if BBS_HAS_SSH`:

| Field | Bytes |
|---|---|
| `uint32_t relay[4]` (the parsed relay addresses; no text copy is kept, `ipToText` renders the form) | 16 |
| `int16_t webFrom`, `int16_t webUntil` (minutes since local midnight, -1 none, silent hours' type) | 4 |
| `uint16_t webSlow` (seconds) | 2 |
| `uint8_t relays`, `webLines`, `webFails`, `webSlowMax`, `bool web` | 5 |
| padding, with the array first | 1 |
| **web subtotal, S3 only** | **28** |
| `uint8_t connectsMin` + padding, **every board** | 4 |

**Board state, beside `bans_`.** One escalator shared by all the relay
addresses, inside `#if BBS_HAS_SSH`:

| Field | Bytes |
|---|---|
| `uint8_t fails`, `uint8_t tier`, padding | 4 |
| `uint32_t lastFail`, `uint32_t until`, `uint32_t lastCall` | 12 |
| `uint32_t seenAt[4]` (when each relay address last called, for the agreement check in section 6) | 16 |
| **subtotal** | **32** |

One escalator and not one per relay address, which saves 48 bytes and costs
a documented consequence: with two relays configured, an attack through one
slows calls through both. In practice there is one relay. If that ever stops
being true the fix is 4 x 12 bytes, not a redesign.

**The per-source connect table**, every board:

| Field | Bytes |
|---|---|
| `uint32_t ip`, `firstAt`, `until`; `uint8_t count`, `tier`; padding | 16 an entry |
| `BBS_RATE_SLOTS` = 8, matching `BBS_BAN_SLOTS` | **128** |

**Totals**

| Build | Bytes |
|---|---|
| An S3 with SSH | 28 + 4 + 32 + 128 = **192**, call it **200** with alignment |
| A board with no SSH (WROOM, Freenove, ESP32-CAM) | 4 + 128 = **132** |
| Per `Session` | **0** |

**Why the connect table is not folded into `BanList`**, which was the obvious
saving. Two reasons, and the second is a bug rather than a preference:
`BanList::clear` is called on a correct staff password (bbs_shell.cpp:3537)
and means "forget everything against this address", which would hand anybody
with the staff password a way to zero their own flood counter; and its eight
slots are already contended by bans, so a flood would evict live bans. The
two tables share the *shape* (`slotFor`'s empty-then-stalest rule, and the
refusal to evict a live entry that `LoginGuard::fail` already makes) and
nothing else.

**Flash: 1.5 to 2.5 KB, estimated**, for the parse and render of
`web_relays`, the eight CONFIG rows and their strings, the two ladders, the
`WEB` status block and the refusal words. The project's rule here is to
measure rather than assert, so `optimize` reads the real delta off the ELF
for `esp32dev` and one S3 release env before the commit. The ESP32-CAM is
the board to watch and it pays only the 132 bytes and the `connects_min`
row, because every web string sits behind `#if BBS_HAS_SSH` and the dimmed
page-list line is one literal.

**Loop cost: none.** Both ladders are evaluated at accept, in the pass that
already does the ban check: a walk of at most eight 16-byte entries and two
`plat::since` divisions. No timer, no `tick()` work, no sleep anywhere, and
the refusal is the `refuse()` lambda `acceptSsh` already has.

---

## 8. What this does not change

**No caller arriving through the relay ever becomes staff.** Rob settled it
on 2026-10-04: "No elevation at all from the web". Better rate limiting is
not a reason to revisit it, and this section exists so that nobody reads it
as one.

- `Bbs::staffPassword` returns `Access::None` for a session whose address is
  a relay, **before any comparison**, and says so in words. Same shape as "no
  staff over RF" and as the published default from outside.
- **The login's sysop question is not asked of a web caller at all.** It can
  never grant anything, so asking it is a bug: it would also burn the relay
  address's one `BanList::aheadTake` slot per window on every web login of
  the sysop's account.
- **The rule does not relax if PROXY v2 is built** and a web caller's own
  address becomes known. It is not "no staff from an un-bannable address", it
  is "no staff from the web".
- **Everything else a telnet caller meets, a web caller still meets:** the
  per-handle `LoginGuard` lockout, the closed-board sign, `kAsksMax` and
  `kPerPeer`, a shut-down board, the busy line, the socket budget, and the
  published default refused because the relay is not local.
- **One protection a web caller genuinely loses, and it should be written
  down:** `bans_.fail` at `bbs.cpp:1059` gives an SSH connection that ended
  on wrong account passwords one strike against its address. A relay address
  cannot take that strike, so for web callers that layer is replaced by the
  section 3.1 escalator, which slows rather than bans. A telnet caller never
  had that strike at all (`bans_.fail` has exactly two call sites, and the
  telnet account-password path is not one of them), so a web caller is left
  with precisely the protection a telnet caller has always had, plus a
  slowdown telnet does not get.

---

## 9. The failure modes, honestly

- **One determined attacker, one address.** The relay's ladder puts them at
  about one attempt a minute within five minutes. Against a single account
  `LoginGuard` caps them at 5 tries per 15 minutes whatever they do. Against
  **many** accounts nothing caps them per address, so spraying handles runs at
  roughly the relay's floor, on the order of a thousand tries a day across
  every account on the board. That is the real residual. It is also exactly
  the rate a native SSH client from a fresh address gets today, so the web is
  not the weak point; the per-handle lockout and a decent password are what
  carry it, as they already do.
- **A distributed attack.** Per-visitor limits are worth nothing against a
  botnet. The board's relay-wide escalator is the only backstop and it works
  by degrading the web for everyone, up to `web_slow_max`. That is a
  deliberate choice of "the web gets slow" over "the board gets ground", and
  the escape hatch is `WEB OFF`, which is why the lock has to be one word at
  a prompt and not four keystrokes inside a form.
- **A shared address: a university, a carrier NAT, a big workplace.** One bad
  actor there costs every innocent visitor from the same /32 or /64 a wait of
  up to 60 s, and if the board is also slowed, up to `web_slow_max` of
  refusals on top. **Worst case for a wholly innocent visitor is therefore
  about six minutes of "not now" at both ladders' caps, with their account
  untouched and their address unbanned.** Six minutes of annoyance is the
  price of not having a ban, and it is the single best argument for this
  design: a ban would have cost that visitor the board entirely, for fifteen
  minutes, with no way to tell they were collateral.
- **The relay is down.** The button fails and nothing else does: the `ssh://`
  line, the telnet line and every native client keep working, which is why
  the native command is printed beside the terminal. The board notices
  nothing; `WEB` simply shows no recent web call.
- **The relay is lying** (compromised, or just misconfigured). It can forge
  `X-Forwarded-For` and ignore every figure in section 6, so all of its
  limits evaporate. What remains is everything in section 2 marked **board**:
  `web_lines`, the lock, the hours, the escalator, and the fact that it can
  never become staff. Its worst case is then exactly what any host on the
  internet can already do to the SSH port, minus the ban, plus the slowdown.
  It can also serve bad JavaScript and see plaintext, which no board-side
  limit addresses and which the plan's section 9 already states as the thing
  a visitor is actually trusting.
- **Eight-slot tables.** `BanList`, `LoginGuard` and the new rate table all
  hold eight. A flood from more addresses than that evicts entries, so the
  rate limiter leaks under exactly the attack it is least needed for (a
  distributed one, where the per-relay escalator is the real defence). Named
  rather than fixed, because the fix is a number and the honesty is the point.
- **A clock-less board.** The hours do nothing without NTP and only the
  switch applies, as with silent hours. The ladders use `plat::millis` and are
  unaffected.
- **Wrap.** Everything is `plat::since`. The specific bug this avoids is the
  one `windowFrom` carries a comment about and the one that stopped pairing on
  a board up 24.8 days: a stamp of 0, and a plain difference read as 49 days.
  Any new counter here starts at `plat::millis()` and never at 0, and 0 keeps
  meaning "nothing waiting" with the `if (!at) at = 1;` guard.

---

## 10. What needs Rob

1. **Does `web` default on or off** for a board that has never seen a web
   caller? Off is the safe answer and means the button fails on most listings
   until each sysop opts in, which reads as a broken feature. On is the
   useful answer and means a sysop who never thought about the web gets web
   callers. My recommendation: **on**, with the board telling the sysop at
   their next login the first time a web caller arrives ("a caller reached
   this board through the web relay; CONFIG web turns it off"), so consent is
   informed rather than assumed. This is the one the task says is his.
2. **Does a successful login clear the tier or step it down one?** I
   recommend step it down (section 3), diverging from `BanList::clear`'s
   precedent, because a relay address is shared and clearing is buyable with
   one account.
3. **`web_lines` default.** 2 is cautious on a board with 8 SSH lines. Half
   the SSH lines rounded down is the alternative.
4. **`WEB ON` / `WEB OFF` as a live staff command**, beside the form. I think
   it is needed, because the moment a sysop wants it is the moment they do not
   want to walk a form.
5. **Does `web_relays` move from CONFIG network (the plan) to CONFIG web
   (this report)?** My recommendation: CONFIG web, one page owns the web.
6. **Is the `web` announce field worth the payload bytes** so no button
   appears on a board that says no? About 12 bytes against a budget that just
   rose to 1,392 for `ssh_port`, plus a directory and a site change. The
   board enforces the lock either way; this only buys a page that does not
   offer what will be refused.
7. **Does `connects_min` ship on every board including the WROOM?** My
   recommendation: yes. 132 bytes, and it is the only part of this a board
   with no web benefits from.
8. **Every test plan, before it runs**: the host tests for both ladders
   (which want the fast-timer mode and will need `board_secs`-style scaling or
   a `REALTIME` mark), a bench S3 flash, and the relay's own limit and
   malformed-frame tests in the site's selftest. Code review runs on all of it
   first, without asking.

---

## 11. What this needs from tty-ux and explain

Not designed here on purpose: `tty-ux` specifies screens and `explain` writes
what a person reads.

- **CONFIG web at 40 and 80 columns**, eight rows, one of them a time pair in
  silent hours' shape, and whether the five policy rows (3, 6, 7, 8 and the
  hours) should sit behind a sub-page button so the top of the page is just
  the switch, the relays and the lines.
- **A dimmed entry in CONFIG's page list** for a board with no SSH. The page
  list has no pattern for one today; the per-row greying does.
- **Where the web's status lives**: a `WEB` command of its own, extra lines
  under `BANS`, a section in `SYS`, or all three. It carries the relay
  addresses and whether each has ever called, web callers on against
  `web_lines`, the lock and the hours with the clock's validity, and the
  current tier with its remaining wait.
- **The refusal words**, within what `sshd::refusal` carries and readable at
  39 columns, for: locked, outside the hours, web lines full, and slowed. The
  drafts in section 4 are the information, not the copy.
- **Whether a web caller is marked** in `WHO`, `NODES`, `LAST` and the caller
  log, and if so how. The board knows "this address is a relay" and nothing
  more until PROXY v2, so the honest marker says the connection came through
  the web and not who. The caller log's address column showing one relay
  address for every web call is the thing a sysop will ask about first.

---

## 12. Files read

Firmware, in-tree at d807e2b: `src/core/guard.h` (BanList's 16-byte entry,
`LoginGuard`, `localNet`, `peerAddr`), `src/core/guard.cpp` (`slotFor`'s
eviction, `windowFrom` and why it uses `plat::since`, the refusal to evict a
live counter), `src/core/bbs.cpp` (773 the ban at accept, 1059 the SSH
wrong-password strike, 2533 `aheadTake`), `src/core/bbs_ssh.cpp` (`busyFits`,
`acceptSsh` and its `refuse()` lambda, the per-handle checks at 381-413),
`src/core/sshd.cpp` (`kAsksMax` 8, `kPerPeer` 2, `claim`, `refusal`),
`src/core/bbs_shell.cpp` (3530-3552 `staffPassword`, its single
`bans_.fail` and `bans_.clear`), `src/core/bbs_sysop.cpp` (`CfgField` and its
width rules, `kNetwork`, `kBoard`'s silent hours, `CfgPage`/`CFG_PAGE`,
`UNBAN`), `src/core/sysconfig.h` (`port`, `sshPort`, `cgnatLocal`,
`silentFrom`/`silentUntil`), `src/core/form.h` (`kMaxFields` 16),
`src/core/plugin.h` (`kSettingMax` 120), `src/config.h` (`BBS_BAN_*`,
`BBS_LOCK_*`, `BBS_SSH_*`), `src/main.cpp` (126-260, the redial backoff),
`src/platform/platform.h` (`plat::since`).

Plan and history: `internal/plan-web-ssh-2026-10-04.md`, `CLAUDE.md`
1743-1760 (browser SSH being built, and the tabling kept as history).

Site: `deploy/unleashed-site.service`, `server.py` environment conventions.
