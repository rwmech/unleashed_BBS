# Browser SSH from the directory: research (2026-09-28)

Question (Rob): can the directory offer a "Connect (SSH)" button that opens an
SSH session to a listed board inside the visitor's browser, with the
directory's server never seeing plaintext?

Research only. No product code was written. Firmware read at `e41634e`
(main), directory at `fed4c84` (main). Web sources were read on 2026-09-28;
where the session's egress proxy blocked a primary site, the same text was
read from the project's own GitHub source, and anything not read first-hand
is marked **unverified**.

---

## 1. Recommendation

**Build it, but not as a directory-only change, and not before two firmware
changes land.** The in-browser half is solved: there is a maintained, MIT,
browser-native SSH library that negotiates with the board's wolfSSH exactly
as it is configured today, and the relay is a few hundred lines. What is not
solved is on the board and in the directory's own rules:

1. **Every web caller reaches the board from the droplet's one address.** The
   board's per-address defences then treat all web callers as one person:
   three connections that end on wrong passwords within 15 minutes ban the
   droplet from that board for 15 minutes, locking out every web caller
   ([config.h:191-194](../src/config.h), `closeSession` strike described at
   [bbs_ssh.cpp:17-21](../src/core/bbs_ssh.cpp), refusal at
   [bbs_ssh.cpp:233](../src/core/bbs_ssh.cpp)), and only two key exchanges
   may be in progress at once from one address
   ([sshd.cpp:146](../src/core/sshd.cpp), refusal at
   [sshd.cpp:777](../src/core/sshd.cpp)). The relay cannot see passwords, so
   it cannot filter the guessers from the honest callers. Without a firmware
   answer (section 5.3), one visitor guessing can take a board's web access
   down for everyone, repeatably.
2. **The directory promises it never connects out.** "It never makes outbound
   connections. A directory that connects to whatever host and port a
   stranger posts to it is a port scanner with a public API"
   ([directory README.md:66](https://github.com/rwmech/unleashed_directory/blob/main/README.md),
   repeated in server.py's header). A relay is an outbound connection by
   definition. It can be made consistent with the reasoning behind that rule
   (section 4.2), but the sentence itself has to change, and that is Rob's
   call. The site's selftest also fails any page JavaScript that mentions
   `WebSocket` ([selftest.py:3615](https://github.com/rwmech/unleashed_directory/blob/main/selftest.py)),
   so the connect page needs a deliberate, scoped exception.

If both are accepted, the recommended stack is **xterm.js +
`@microsoft/dev-tunnels-ssh` in the page, and a stdlib asyncio relay run as
its own service beside server.py**, with the relay looking up the target
itself from a board id. Estimate: **9 to 13 working days** (section 7).

**Nobody in the BBS world does this yet.** fTelnet, Synchronet and ENiGMA½
all relay telnet or rlogin over WebSocket; none terminates SSH in the browser
(section 6). That is a reason it would be distinctive, and also a reason to
expect to find the rough edges first.

---

## 2. What the board offers (wolfSSH 1.5.0, as built)

Read from the vendored source with the project's
[user_settings.h](../components/wolfssh/user_settings.h) applied to wolfSSH's
own lists (`cannedKexAlgoNames` and the rest,
[internal.c:863-1012](../components/wolfssh/wolfssh/src/internal.c), gated by
[internal.h](../components/wolfssh/wolfssh/wolfssh/internal.h)).
`src/core/sshd.cpp` does not override the lists (no `SetAlgoList` call), so
these are what go on the wire:

| Kind | Offered, in wolfSSH's order |
|---|---|
| Key exchange | curve25519-sha256, curve25519-sha256@libssh.org, ecdh-sha2-nistp256 |
| Host key | ssh-ed25519, ecdsa-sha2-nistp256 |
| Cipher | aes256-gcm@openssh.com, **aes192-gcm@openssh.com**, aes128-gcm@openssh.com, aes256-ctr, **aes192-ctr**, aes128-ctr |
| MAC | hmac-sha2-256, hmac-sha2-512 (no `-etm`) |
| Auth | password, and `none` for a name that is not an account (sshd.cpp:370-414; COMMANDS.md "SSH, on the S3 boards") |

**A latent firmware bug found on the way:** the two aes192 ciphers are
advertised but AES-192 is compiled out (`#define NO_AES_192`,
user_settings.h:112), so `wc_AesSetKey` refuses a 24-byte key
(wolfcrypt `aes.c:4645-4650`: only 16 and 32 are accepted without
`WOLFSSL_AES_192`). A client that ranked aes192 above the other AES modes
would negotiate it and then fail. SSH picks the client's first mutual choice
(RFC 4253 section 7.1), and none of the clients below, nor OpenSSH, ranks
aes192 first, so it bites nobody known today. It is a one-line fix
(`wolfSSH_CTX_SetAlgoListCipher`, or keep AES-192 in), queued separately, not
done here.

**Terrapin (CVE-2023-48795) does not reach this board** whatever the client:
it needs chacha20-poly1305 or an encrypt-then-MAC mode
([terrapin-attack.com](https://terrapin-attack.com/)), and the board offers
neither. This agrees with the earlier SSH research
(internal/ssh-research-2026-09-26.md, section 1.1).

Key exchange runs on the SSH task, not the BBS loop (ssh-research section
6.1), so which curve a web client picks does not touch Rule no. 1.

---

## 3. The in-browser SSH clients

Every row was checked in the project's source (cloned) or its registry entry;
URLs are in each cell's notes below the table.

| Client | Licence | Latest | Negotiates with the board? | Password auth | Host-key check | Transport | Size |
|---|---|---|---|---|---|---|---|
| **@microsoft/dev-tunnels-ssh** (TypeScript, Web Crypto) | MIT | npm 3.12.42, 2026-08-24 | **Yes**: ecdh-nistp256, ECDSA-P256 host key, aes256-gcm | Yes | Embedder's callback; fails closed | Plain binary WebSocket | 530 KB raw / 87 KB gzip of .js in the tarball (not a minified bundle) |
| **golang.org/x/crypto/ssh**, compiled to WASM | BSD-3-Clause | v0.56.0, 2026-09-02 | **Yes**: curve25519, ECDSA-P256, aes128-gcm | Yes (`ssh.Password`) | Embedder's callback | Whatever the wrapper does | about 21 MB WASM in sshterm (below) |
| c2FmZQ/sshterm (x/crypto in WASM + xterm.js, an app) | MIT | v0.9.0, 2026-09-07 | Yes | **No**: public key and keyboard-interactive only | TOFU prompt, IndexedDB | Plain binary WebSocket | 5.1 MB download, `ssh.wasm` 21.2 MB unpacked |
| hullarb/ssheasy (x/crypto in WASM) | MIT | last commit 2026-02-23, no tags | Yes | Yes | MD5 fingerprint each time, never remembered, bypassable | **Own protocol: the client names host and port** | not measured |
| VerdigrisTech/sshclient-wasm | BSD-3-Clause | npm 0.1.5, 2026-02-26 | Yes | Yes | **None** (`InsecureIgnoreHostKey`) | Binary WebSocket | 7.3 MB unpacked |
| libapps Secure Shell / nassh (OpenSSH 10.5 in WASM) | BSD | 0.81, 2026-09-16 | Yes | Yes | OpenSSH known_hosts | Google's relay protocols; a Chrome extension in practice | not measured |
| mscdex/ssh2 | MIT | 1.17.0 | would, but | | | **Node only** (Node crypto, `net`) | |

Sources:
- dev-tunnels-ssh: [repo](https://github.com/microsoft/dev-tunnels-ssh),
  [npm](https://www.npmjs.com/package/@microsoft/dev-tunnels-ssh),
  [registry JSON](https://registry.npmjs.org/@microsoft%2fdev-tunnels-ssh).
  Browser use and Web Crypto:
  [README](https://github.com/microsoft/dev-tunnels-ssh/blob/main/README.md).
  Algorithm lists: `src/ts/ssh/sshSessionConfiguration.ts` L86-101 and
  `src/ts/ssh/algorithms/sshAlgorithms.ts` L87-121 in the repo: kex
  ecdh-nistp384, **ecdh-nistp256**, dh16, dh14 (**no curve25519**); host key
  rsa-sha2-512/256, ecdsa-nistp384, **ecdsa-nistp256** (**no ed25519**);
  ciphers **aes256-gcm**, aes256-ctr (no aes128, no aes192); MACs etm first,
  then **hmac-sha2-512/256**. Host-key callback: `sshClientSession.ts`
  L81-127 and `sshSession.ts` L942-961; skipped if the page calls only
  `authenticateClient()` (L54-69). Transport: `streams.ts` L257-290.
  Terrapin issue [#93](https://github.com/microsoft/dev-tunnels-ssh/issues/93),
  closed without strict kex (irrelevant here, section 2).
- x/crypto/ssh: default client order in `ssh/common.go` L90-200 of
  [golang.org/x/crypto](https://pkg.go.dev/golang.org/x/crypto/ssh); it
  prefers **ecdsa-sha2-nistp256 over ssh-ed25519** unless
  `HostKeyAlgorithms` is set. Advisories
  [GO-2026-6354](https://pkg.go.dev/vuln/GO-2026-6354) and
  [GO-2026-6355](https://pkg.go.dev/vuln/GO-2026-6355) (deadlock from a
  hostile peer, fixed in v0.56.0), [GO-2023-2402](https://pkg.go.dev/vuln/GO-2023-2402)
  (Terrapin, fixed in v0.17.0).
- sshterm: [repo](https://github.com/c2FmZQ/sshterm),
  [v0.9.0 asset](https://github.com/c2FmZQ/sshterm/releases/download/v0.9.0/sshterm-docroot-v0.9.0.tar.gz);
  auth methods at `go/internal/app/ssh.go` L283-310; TOFU at L402-440.
- ssheasy: [repo](https://github.com/hullarb/ssheasy); x/crypto v0.26.0 in
  `web/go.mod`, older than both 2026 fixes; target named by the client in
  `proxy/main.go` L110-150; `password=` accepted in the URL (README).
- sshclient-wasm: [repo](https://github.com/VerdigrisTech/sshclient-wasm),
  `pkg/sshclient/client.go` L82.
- nassh: [libapps mirror](https://github.com/libapps/libapps-mirror),
  `nassh/docs/FAQ.md` ("Everyone should install & use the extension
  variant"), `nassh/docs/relay-protocol.md`, `wassh/docs/sockets.md`.
- ssh2: [repo](https://github.com/mscdex/ssh2), Node-only per the maintainer
  in [#171](https://github.com/mscdex/ssh2/issues/171) and
  [#331](https://github.com/mscdex/ssh2/issues/331).
- Ruled out quickly: [o16s/browser-ssh](https://github.com/o16s/browser-ssh)
  (first commit 2026-09-22, no licence file, key auth only);
  russh has no browser build ([issue #224](https://github.com/Eugeny/russh/issues/224),
  open since 2024); [Tailscale tsconnect](https://github.com/tailscale/tailscale/blob/main/cmd/tsconnect/README.md)
  reaches hosts over a tailnet only. No maintained standalone WASM libssh or
  Dropbear build was found.

### 3.1 The pick: dev-tunnels-ssh

- **It is a library, not an app**, so the directory draws its own page:
  a login box, then xterm.js, which is what a button on a listing needs.
  sshterm is a whole terminal app with its own command language.
- **It uses the browser's own crypto** (Web Crypto), so there is no WASM
  blob; 87 KB gzipped against sshterm's 5 MB download.
- **It fails closed on the host key** if the page supplies no answer, which
  is the right default. Trust on first use is ours to write (section 5.2).
- **Its one dependency on the board: the ECDSA host key.** It has neither
  curve25519 nor Ed25519, so it always lands on ecdh-sha2-nistp256 with
  `host_ecdsa`. The board already keeps that key for old SyncTERM
  (ssh-research section 2), so nothing changes, but a future "drop P-256 to
  save 29.5 KB" would break web SSH. Write that down beside the key.
- Its npm runtime dependencies include Node polyfills (`buffer`,
  `diffie-hellman`, `debug`), so it needs one bundler run (esbuild) to make a
  single browser file. The bundle is a build artefact, vendored like
  `static/fonts`, with its licence notice in THIRD_PARTY_NOTICES.md.
- **Unverified:** the minified bundle size, and a real session through a
  byte-pipe relay. Both are first-day checks, against the host build
  `bbs_host_s3` (which runs the real wolfSSH server) before any board.

Fallback if dev-tunnels-ssh stalls: fork sshterm (MIT) to add `ssh.Password`
and lock it to one endpoint. It is the most complete implementation
(curve25519, strict kex, current advisories fixed) at a 5 MB cost per
visitor, cached after the first.

### 3.2 The terminal: xterm.js

- [xterm.js](https://github.com/xtermjs/xterm.js), MIT, 6.0.0 on main
  (last commit 2026-08-30).
- **CP437 is not the browser's problem.** `write()` treats raw bytes as UTF-8
  (`typings/xterm.d.ts` L1375), and the board already re-encodes CP437 art to
  UTF-8 for a UTF-8 terminal; its SSH callers get the charset from the probe
  (COMMANDS.md, SSH notes: "SyncTERM gets CP437 and OpenSSH or PuTTY
  UTF-8"). xterm.js answers the cursor position report the probe sends
  (`src/common/InputHandler.ts` L2722, "Report Cursor Position"), so it
  should detect as UTF-8 ANSI. **To confirm on the host build**, because the
  probe's UTF-8 test glyph is the kind of thing that works in theory.
- Block and box drawing: the WebGL renderer draws U+2500-U+259F itself,
  continuous across cells, on by default (`customGlyphs`,
  `addons/addon-webgl/typings/addon-webgl.d.ts` L60-75). That is the ANSI
  art's wordmark and frames.
- 80x24 at open; the pty request carries the size, and wolfSSH is built
  with `WOLFSSH_TERM` + `WOLFSSH_SHELL` so a resize reaches the board
  (user_settings.h).

---

## 4. The relay

### 4.1 Shape

```
browser (https page, xterm.js + SSH)
   | wss://unleashedbbs.net/ssh/<board id>        ciphertext in WS frames
Caddy (TLS, reverse_proxy, WebSocket upgrade automatic)
   | ws://127.0.0.1:8081/ssh/<board id>
relay (asyncio, its own systemd service)
   | TCP to the board's heartbeat address : announced ssh_port
board (wolfSSH on 6422)
```

- **The browser never names a host or port.** It names a board id. The relay
  reads the target from the directory's SQLite (read-only connection) at
  connect time, and refuses unless the board is listed publicly, online (a
  heartbeat within its interval), not held or banned, and announces an SSH
  port. This is fTelnetProxy's model (it relays only to its own target, a
  relay file, or entries in the Telnet BBS Guide's directory:
  `fTelnetProxy/WebSocketClientThread.cs` and `TelnetBbsGuide.cs` in
  [rickparrish/fTelnetProxy](https://github.com/rickparrish/fTelnetProxy)),
  and the opposite of nassh's v4 relay and ssheasy, where the client names
  the target. websockify's own maintainer called client-chosen targets "a
  HUGE security risk" ([websockify #3](https://github.com/novnc/websockify/issues/3)).
- **Dial the address the heartbeats come from, never the posted `host`.**
  `host` is free text the board sends (`tidy(payload.get("host"), 80)`,
  server.py `announce()`), so dialling it would let anybody who can announce
  point the droplet at any machine on the internet. The heartbeat's source
  address (`address`) is one the board has proved it holds by sustaining
  heartbeats from it for hours. The worst a listed board can then do is make
  the droplet connect to its own address, which is the whole point.
  Refuse private, loopback and link-local results as well, in case a board
  ever announces from inside the droplet's own network.
  **One edge:** boards behind carrier-grade NAT share a public address
  (`group_key` exists for this), so a board could name another subscriber's
  forwarded port on the same address. The port is still one somebody
  forwarded to the internet on purpose, so this is a nuisance, not a hole,
  but it is why the per-board caps below also apply per address.
- **The announce protocol needs one new field**, `ssh_port`: the port
  callers reach SSH on from outside, sent only while SSH runs, which today
  it is not (`features()` and the payload in
  [announce.cpp](../src/plugins/announce.cpp) carry no SSH). It is the SSH
  twin of the existing public port. PROTOCOL.md documents it, the directory
  stores it, and a board without it gets no button.

### 4.2 Stdlib, `websockets` or websockify

Python's standard library has no WebSocket server (checked on Python
3.11.15: no stdlib module contains the word). Three options:

| Option | Licence | For | Against |
|---|---|---|---|
| **Own asyncio relay, stdlib only** (recommended) | ours, GPL-3.0+ | Matches the directory's one rule about dependencies; the limits live in one small file; binary frames only, no extensions, so the RFC 6455 subset is small: the handshake (`Sec-WebSocket-Accept` = base64(SHA-1(key + GUID))), unmasking, 7/16/64-bit lengths, continuation, ping/pong, close ([MDN's server guide](https://github.com/mdn/content/blob/main/files/en-us/web/api/websockets_api/writing_websocket_servers/index.md)). Estimate 250 to 400 lines with lookup, limits and logging. | We own a framing parser facing the internet. It needs its own fuzz-ish tests in selftest. |
| [`websockets`](https://pypi.org/project/websockets/) 17.1 | BSD-3-Clause | Mature; `origins=`, `process_request` (refuse with 403/429 in the handshake), `max_size`, pings, all in [`server.py`](https://github.com/python-websockets/websockets/blob/main/src/websockets/asyncio/server.py) | The directory's first third-party dependency, and a pip step in `update.sh` |
| [websockify](https://github.com/novnc/websockify) 0.13.0 (2025-02-12) | LGPL-3.0 | `JSONTokenApi` token plugin could ask the directory for the target; `ExpectOrigin` auth plugin | A process per connection, **no rate limiting** at all, so the limits would still need writing somewhere |

Sources for websockify: [releases](https://github.com/novnc/websockify/releases),
[`token_plugins.py`](https://github.com/novnc/websockify/blob/master/websockify/token_plugins.py),
[`auth_plugins.py`](https://github.com/novnc/websockify/blob/master/websockify/auth_plugins.py),
[`websockifyserver.py`](https://github.com/novnc/websockify/blob/master/websockify/websockifyserver.py).
Its GitHub advisories are all in products that embed it, not in the package
([advisory search](https://github.com/advisories?query=websockify)).

**Why a separate service, not a thread in server.py:** server.py is a
`ThreadingHTTPServer`, a thread per request, and each relayed call is a
connection that lives for an hour. A crash or a slow leak in the relay should
not take the listing and `/announce` down with it, and the relay can be
switched off with `systemctl` without touching the directory.

### 4.3 Limits and logging (in the relay, not in Caddy)

Rate limiting is not in Caddy's standard build: `mholt/caddy-ratelimit` is
unofficial, needs a custom `xcaddy` binary, and counts requests, not
concurrent sessions or bytes
([README](https://github.com/mholt/caddy-ratelimit/blob/master/README.md)).
So the relay enforces, with starting figures to tune:

- **Origin**: exact match on the directory's own origins, or 403. This stops
  other websites scripting visitors' browsers into the relay (cross-site
  WebSocket hijacking). It does not stop scripts, which can forge Origin; the
  target rule and the caps do that.
- **Per visitor** (Caddy's `X-Forwarded-For`, trusted from loopback only, as
  server.py already does for `/announce`): 2 open sessions, 6 opens a
  minute, 20 an hour.
- **Per board**: at most its announced node count, and **at most 2 connections
  still in the key exchange at once**, matching the board's own `kPerPeer`
  (sshd.cpp:146) so the relay never provokes the board's refusal. Per
  shared address (`group_key`) the same.
- **Per session**: 60 s to finish the handshake, 15 min idle, 2 h maximum,
  and a byte cap (say 10 MB each way). The byte cap is what stops the relay
  becoming a free bulk pipe through a board's XMODEM, the one thing that
  would make bandwidth matter.
- **Global**: a ceiling on open sessions (say 100) and on file descriptors.
- **Log** per session: time, visitor address, board id, target address,
  bytes each way, duration, how it ended. No payload, which is ciphertext
  anyway. The same retention as the directory's other logs.

### 4.4 Caddy

- WebSocket upgrades pass through `reverse_proxy` with no extra config, and
  the site's automatic HTTPS makes it wss://
  ([Caddy reverse_proxy docs](https://github.com/caddyserver/website/blob/master/src/docs/markdown/caddyfile/directives/reverse_proxy.md)).
- **Caddy closes every WebSocket on a config reload by default.** Set
  `stream_close_delay` (for example `2h`, the session cap) on the relay's
  route, or every web caller drops whenever Caddy reloads. The directory's
  `deploy/update.sh` only checks that Caddy is running (L234) and does not
  reload it; `setup.sh` writes the live Caddyfile, and the private site
  repo's update script was not checked. Worth setting regardless.
- The page and the relay share an origin, so `connect-src 'self'` covers the
  WebSocket under CSP3 ([CSP source](https://github.com/w3c/webappsec-csp/blob/main/index.bs)),
  and an https page could not use ws:// anyway: non-media mixed content is
  blocked ([mixed content spec](https://github.com/w3c/webappsec-mixed-content/blob/main/index.bs),
  [WebSockets standard](https://github.com/whatwg/websockets/blob/main/index.bs)).

---

## 5. The browser page

### 5.1 Flow

1. "Connect (SSH)" on a listing whose board announces `ssh_port` and is
   online. It opens `/ssh/<id>` on the same domain.
2. A form: handle and password, or "Visit as a guest" (sends SSH `none`
   with the name `guest`, which the board sends to its ordinary prompt,
   COMMANDS.md). Autocomplete off for the password.
3. Connect, key exchange, **host key check** (5.2), password auth, pty
   80x24, shell. xterm.js takes the channel's bytes; keys go back the same
   way; the window size follows the browser window.
4. On close, say how it ended in words (the board's `-->` refusals arrive as
   SSH disconnect reasons: "All SSH ports are full", "All lines are busy").

### 5.2 Host keys: trust on first use, with the directory as a second opinion

- First connection: show the board's fingerprint (SHA256, OpenSSH style,
  the same text staff see in `SYS` and `HARDWARE`) and ask. On yes, store it
  in `localStorage` keyed by board id.
- Later connections: a match is silent; a mismatch stops before the password
  is sent and says plainly that the key changed, that a factory reset or an
  erase does this (COMMANDS.md: the keys are not in the backup), and that
  the sysop can confirm it.
- **A second opinion that does not trust the droplet blindly:** the board
  could announce its ECDSA fingerprint in the heartbeat, and the page could
  show "matches what this board announced". It is honest to say what that
  is worth: the page comes from the droplet, so a compromised droplet could
  lie in both places. It protects against a compromised network path between
  the droplet and the board, which is the realistic attack on a relay. TOFU
  in the browser is still what protects against the droplet.
- **The limit nobody can fix in a web page:** the page's JavaScript is served
  by the droplet. A compromised droplet could serve a page that sends the
  password somewhere else. End-to-end holds against a curious or subpoenaed
  relay, a leaked log, and anyone on the droplet's network; it does not hold
  against whoever controls the droplet's web root. Every in-browser crypto
  design has this property. The mitigations are Subresource Integrity on the
  bundle, a strict CSP (`connect-src 'self'`, no inline script on that
  page), and publishing the bundle's hash in the repo so it can be checked.
  A caller who needs more than that should use a native client, and the page
  should say so.

### 5.3 The shared-address problem, and the firmware fix

As section 1 says, the board sees every web caller as the droplet. Options:

- **A. Nothing.** The relay's per-board handshake cap keeps it inside
  `kPerPeer`, but one visitor with a wrong password three times still bans
  the droplet from that board for 15 minutes. Not acceptable as shipped.
- **B. The relay prepends a PROXY protocol v2 header** with the visitor's
  address, and the board honours it **only from an address the sysop names**
  (for example `relay_addr` on CONFIG announce, or derived from the
  directory the board announces to, resolved at start like announce's DNS).
  The board then bans, counts handshakes and logs the real visitor, and
  staff see the real address in `WHO` and the caller log. Synchronet already
  accepts HAProxy headers for the same reason (`websocketservice.js` in the
  [Synchronet mirror](https://github.com/SynchronetBBS/sbbs/blob/master/exec/websocketservice.js)).
  The header goes before the `SSH-2.0-` line on the SSH port, outside the
  encryption, so the relay can write it without seeing anything. On 6422 the
  board speaks first, so it must wait for the header from the named address
  before sending its identification. **Recommended.** About 2 days with
  tests, S3 firmware only.
- **C. The board exempts the relay address from bans.** Simple, and removes
  brute-force protection for every web caller. No.

The "local address" guard needs no change: the droplet is never local, so
the published default password never works through the relay
(CLAUDE.md, 1.0.0 first-boot design).

---

## 6. How other BBS software does it

- **fTelnet** (AGPL-3.0): telnet, rlogin and raw WebSocket connection
  classes only; **no SSH** ([source/connections](https://github.com/rickparrish/fTelnet/tree/master/source/connections)).
- **fTelnetProxy**: WebSocket to TCP, targets limited to its own, a relay
  file, or the Telnet BBS Guide's directory; logs IP, Origin and Referer
  per connection ([repo](https://github.com/rickparrish/fTelnetProxy)).
  The precedent for "only to listed boards".
- **Synchronet** `websocketservice.js`: telnet or rlogin only; any other port
  "would be a gaping security hole" ([source](https://github.com/SynchronetBBS/sbbs/blob/master/exec/websocketservice.js)).
  SSH is its terminal server's own port.
- **ENiGMA½**: a WebSocket login server that is telnet over WebSocket
  (`WebSocketClient extends TelnetClient`); SSH is a separate server
  ([websocket.js](https://github.com/NuSkooler/enigma-bbs/blob/master/core/servers/login/websocket.js)).
- **Mystic**: browser access via fTelnet or HtmlTerm plus a redirector
  (search results only, **unverified**).
- **In-browser SSH end to end exists outside the BBS world** with exactly
  this dumb-relay shape: sshterm, browser-ssh and
  [localssh](https://github.com/bradsec/localssh) ("the relay only ever sees
  ciphertext"). nassh documents a websockify mode that "just wraps SSH
  traffic in WebSocket frames" (`nassh/docs/relay-protocol.md`).
- **Answer: no BBS web client found does SSH in the browser.** Every one
  terminates at telnet or rlogin, which is plaintext at the proxy.

---

## 7. Effort

| Piece | Days | Notes |
|---|---|---|
| Firmware: `ssh_port` in announce, and the ECDSA fingerprint | 0.5 | S3 only; ESP32 images unchanged |
| Firmware: PROXY v2 from a named address (5.3 B) | 2 | Both SSH ports; tests on the host with the relay as the sender |
| Firmware: stop advertising aes192 | 0.1 | Independent of this feature; worth doing anyway |
| Directory: store `ssh_port`, the button, the `/ssh/<id>` page, PROTOCOL.md | 1 | |
| Relay: stdlib asyncio, framing, lookup, limits, logging, systemd unit, Caddy route | 2.5 | +1 day of selftest coverage for the framing and the limits |
| Page: esbuild bundle of dev-tunnels-ssh, xterm.js wiring, login form, TOFU, errors, SRI and CSP | 2 to 3 | The selftest's `WebSocket` ban needs a scoped exception for this one page |
| Code review, `web-qa` and `bbs-qa` passes, end-to-end against `bbs_host_s3` | 1 to 2 | Any test plan needs Rob's OK first |
| Docs and the caller-facing words (explain agent) | 0.5 | |
| **Total** | **9 to 13** | First day should be the risk check: dev-tunnels-ssh through a relay to `bbs_host_s3`, detected as UTF-8 |

---

## 8. Honest risks

- **What the relay still sees**: the visitor's IP address, the board they
  called, when, for how long, and how many bytes each way. Keystroke timing
  and packet sizes are visible too, which is a known side channel against
  interactive SSH (a password typed at the board's own prompt, rather than
  as SSH auth, leaks its length and rhythm; SSH password auth sends it in one
  packet). It does not see the handle, the password or anything on screen.
- **Trust in the page** (5.2): end-to-end against the relay, not against
  whoever controls the site. Say so on the page.
- **Abuse**: the relay hides a visitor's address from the board unless 5.3 B
  is built; with it, bans work as they do for any caller. Password guessing
  through the relay is capped by the relay and the board both. No specific
  published incident of a WebSocket-to-TCP relay being abused was found; the
  risk class is the open proxy, which the target rule removes.
- **Cost**: DigitalOcean's basic droplets include 500 GiB outbound a month,
  pooled across the account, $0.01 per GiB over, inbound free (from search
  snippets of [DO's pricing page](https://www.digitalocean.com/pricing/droplets)
  and [bandwidth billing docs](https://docs.digitalocean.com/platform/billing/bandwidth/);
  the pages themselves were blocked here, **unverified** first-hand). A text
  session is an **estimate** of under 1 KB/s average and a few KB/s at peak,
  from this project's measured redraws (1.2 KB PETSCII form, 2,476 bytes for
  a 16-row ANSI form at 80) plus SSH and WebSocket framing. That is on the
  order of 140,000 caller-hours in 500 GiB. Bandwidth only matters for file
  transfers, which the per-session byte cap bounds.
- **Reliability**: every web caller now depends on the droplet being up,
  which the directory has so far avoided for boards (CLAUDE.md, the fTelnet
  entry). Native clients keep working without it, and the page should say
  "or use any SSH client: `ssh -p <port> <handle>@<host>`" beside the
  terminal.
- **The CLAUDE.md fTelnet entry parked browser access over trust**, because
  every earlier option had the droplet reading passwords. In-browser SSH is
  the design that removes that objection for the relay, and leaves only the
  page-serving trust above, which is the same trust a visitor already places
  in any HTTPS site's JavaScript.

What a caller must be told, in the page's own words (the explain agent's
job): the connection is encrypted from their browser to the board; the
directory passes it along and can see that they connected, to which board,
when and for how long, but not what they type or read; the first time, check
the board's key with its sysop if it matters to them; and a native SSH client
is the choice for anyone who does not want to trust the website at all.
