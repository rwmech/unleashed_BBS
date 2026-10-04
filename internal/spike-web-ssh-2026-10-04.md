<!--
 ===========================================================================
  µnleashed BBS
  Electronic freedom on a microcontroller.
 ===========================================================================

 File:         internal/spike-web-ssh-2026-10-04.md
 Module:       Working notes / phase 1 of the browser-SSH plan

 Purpose:      What the phase 1 spike proved, what it could not, and what
               phases 3 and 4 should do differently because of it.

 Applies to:   firmware 1.2.1 (main at 3e8adda), host build bbs_host_s3,
               wolfSSH 1.5.0 / wolfCrypt 5.9.4 as vendored.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v3 or later
 SPDX-License-Identifier: GPL-3.0-or-later
 ===========================================================================
-->

# Browser SSH, phase 1: the spike (2026-10-04)

Phase 1 of `internal/plan-web-ssh-2026-10-04.md`: the throwaway proof that a
browser can hold a session with the real firmware through a relay that only
ever carries ciphertext. Everything below was run on 127.0.0.1 against
`host/bbs_host_s3`, which links the same wolfSSH server the S3 boards run.
No board, no live site, no network. No production code was changed.

---

## 0. Bottom line

- **The path works, and nothing found in it argues against building it.**
  Twelve demonstrations against the real firmware pass, and nine more inside
  headless Chrome pass.
- **One thing could not be done on this machine at all: the client bundle.**
  `node` and `npm` are both refused by the machine guard (`guard: refused,
  'node' is not on the dev-stack list` and `guard: refused, installing
  software is Rob's to do`). So `@microsoft/dev-tunnels-ssh` could not be
  bundled, run or measured, and **the gzipped bundle size the plan asks for
  as phase 1's output is still unmeasured**. Everything underneath the
  JavaScript was proved instead, and the browser was made to prove the parts
  that need no library.
- **The plan's hard constraint is confirmed, and it is harder than the plan
  said.** Read off the wire: the board and dev-tunnels-ssh have **exactly one
  key exchange in common and exactly one host key algorithm in common**, with
  no second choice behind either. `ecdh-sha2-nistp256` and
  `ecdsa-sha2-nistp256` are not a preference, they are the only path. A
  future "drop P-256" on either side takes web SSH off every board, silently.
- **The fingerprint identity holds, character for character**, and it was
  checked three ways, one of them inside the browser with Web Crypto.
- **The detector resolves correctly over SSH, and the answer is UTF-8**, not
  CP437, because a terminal that decodes UTF-8 advances one column over the
  probe's test glyph. The art then leaves the board as UTF-8 and nothing in
  it is undecodable.

---

## 1. What was blocked, and what was done instead

The machine guard refused, in these words:

| Command | Refusal |
|---|---|
| `node --version` | `'node' is not on the dev-stack list; ask Rob to add it if it should be` |
| `npm --version` | `installing software is Rob's to do` |

There is no Node in WSL either, and nothing else on this machine vendors
xterm.js (the site's only third-party bundle is ESP Web Tools, which does
not contain it). A CDN copy is out: research goes through WebFetch, which
returns prose rather than bytes, and `curl` is limited to 127.0.0.1 and
GitHub, where neither library publishes a built bundle.

So the spike was split: everything below the JavaScript was proved with the
real firmware and real clients, and the browser was given the jobs it can do
without a library, which turn out to be most of the ones that were in doubt.

**To finish phase 1, one thing is needed from Rob:** `node` and `npm` on the
dev-stack list. With them, the remaining work is about two hours: an esbuild
bundle of `@microsoft/dev-tunnels-ssh` + `@xterm/xterm`, the measured
gzipped size, and the one end-to-end login in a browser. The relay, the
page scaffolding, the board and every measurement below are already there.

---

## 2. What was proved against the firmware

Twelve of twelve. Each line is a check that fails if the thing it names
stops being true.

| # | Demonstration | Evidence |
|---|---|---|
| 1 | A handshake held to **only** the algorithms dev-tunnels-ssh can reach here | `OPEN in 10 ms`, kex `ecdh-sha2-nistp256`, key `ecdsa-sha2-nistp256`, cipher `aes256-gcm@openssh.com` |
| 2 | What dev-tunnels-ssh itself would negotiate, computed from both offers off the wire | kex `ecdh-sha2-nistp256`, host key `ecdsa-sha2-nistp256`, cipher `aes256-gcm@openssh.com`, mac `hmac-sha2-512` |
| 3 | The host key fingerprint | identical three ways, see section 4 |
| 4 | SSH password auth, **through the WebSocket relay** | `bbs: node 1 SSH password accepted for 'Spike'`, and on screen `Signed in over SSH as Spike` |
| 5 | The detector resolves | cursor reports at columns 19 then 20 (delta 1), board logged `ANSI-UTF8 (telnet off)` |
| 6 | No PETSCII question is ever asked | `HIT DEL OR BACKSPACE` never appears |
| 7 | **The pty's size drives the layout** | a 100-column pty gives 99-column rows (`Bbs::rowWidth` is cols − 1), not the detector's 80 |
| 8 | CP437 art arrives re-encoded as UTF-8 | U+2500, U+2550, U+2580, U+2584, U+2588 (plus U+00B5 and U+2219); **zero** undecodable bytes |
| 9 | A resize reaches the board | window-change to 132x40, next command drawn at 131 columns |
| 10 | A CP437 terminal still resolves as CP437 | cursor reports 19 then 22 (delta 3), board logged `ANSI-CP437` |
| 11 | An SSH user name that is not an account reaches the handle prompt | `Enter your handle:` rather than a refusal |
| 12 | The relay refuses a key it does not hold | close frame 1008 `no such board`; the client never names a host or a port |

The 43-second session that did items 4 to 9 moved **11,394 bytes down and
1,415 up**, which is a useful starting figure for phase 3's per-session
byte cap: an ordinary look around costs about 11 KB.

---

## 3. The negotiation, read off the wire

Not taken from either end's opinion of itself: a tee in front of the board's
SSH port kept the cleartext prefix of both directions, and the two KEXINIT
packets were parsed by a separate program.

**The board offers** (`SSH-2.0-unleashedBBS`):

```
kex              curve25519-sha256, curve25519-sha256@libssh.org, ecdh-sha2-nistp256
server_host_key  ssh-ed25519, ecdsa-sha2-nistp256
encryption       aes256-gcm@openssh.com, aes128-gcm@openssh.com, aes256-ctr, aes128-ctr
mac              hmac-sha2-256, hmac-sha2-512
compression      none
```

**dev-tunnels-ssh 3.12.42 offers** (its `sshSessionConfiguration.ts`
defaults, in order, re-read today):

```
kex              ecdh-sha2-nistp384, ecdh-sha2-nistp256,
                 diffie-hellman-group16-sha512, diffie-hellman-group14-sha256
host key         rsa-sha2-512, rsa-sha2-256, ecdsa-sha2-nistp384, ecdsa-sha2-nistp256
encryption       aes256-gcm@openssh.com, aes256-ctr        (aes256-cbc commented out)
mac              hmac-sha2-512-etm, hmac-sha2-256-etm, hmac-sha2-512, hmac-sha2-256
```

**What is actually in common:**

| | In common | Count |
|---|---|---|
| key exchange | `ecdh-sha2-nistp256` | **1** |
| host key | `ecdsa-sha2-nistp256` | **1** |
| cipher | `aes256-gcm@openssh.com`, `aes256-ctr` | 2 |

The reason there is no slack is in `components/wolfssh/user_settings.h`:
`ECC_USER_CURVES` with `HAVE_ECC384` undefined (so no P-384 on either kex or
host key), `NO_DH` (so neither DH group), and `NO_RSA` (so no RSA host key).
The client has no curve25519 and no ed25519, which is the other half.

**So, plainly: web SSH depends on `ecdh-sha2-nistp256` and the board's
`host_ecdsa` key, and on nothing else.** Dropping P-256 from the firmware to
save space, or wolfSSH dropping it upstream, breaks browser callers on every
board at once while every native client carries on working, so nothing would
report it. That line belongs beside the key in `sshd.cpp`, not only in a
plan.

The handshake was then actually completed with a client held to exactly that
set (`wolfSSH_CTX_SetAlgoListKex/Key/Cipher/Mac`), so this is not an
inference from two lists: the board does it, in 10 ms on the host.

**Incidental, and already on the 1.2.1 queue:** the board's cipher list on
the wire is the corrected one, with no `aes192`. Good.

---

## 4. The fingerprint

`SHA256:` + unpadded base64 of SHA-256 over the public key's SSH wire blob.
Checked three ways on the same key, all identical:

```
SHA256:BRTXukynPjr6GufSC9QUtTo8Li8TfRbgg31T2dbP1Oo   the board's own log / SYS / HARDWARE
SHA256:BRTXukynPjr6GufSC9QUtTo8Li8TfRbgg31T2dbP1Oo   Python, from the blob the client was handed
SHA256:BRTXukynPjr6GufSC9QUtTo8Li8TfRbgg31T2dbP1Oo   Python, from the copy taken off the wire
```

and a fourth, which is the one the plan actually cares about, **computed
inside headless Chrome** with `crypto.subtle.digest('SHA-256', blob)`,
`btoa`, `replace(/=+$/, '')`:

```
SHA256:75h4jH+uoESuuGajhqhU5KiCd5K9z6BMqmi1o62/28E   the page   (a later board instance)
SHA256:75h4jH+uoESuuGajhqhU5KiCd5K9z6BMqmi1o62/28E   the board  (same instance)
```

So trust on first use in a browser can be built on a string a sysop can read
off `SYS` and a visitor can read off the page, and they will match character
for character. **Do not invent a prettier format for the page.**

---

## 5. What the browser itself proved

Headless Chrome 154, nine of nine, against the relay on loopback:

- the fingerprint above, computed in the page;
- **every Web Crypto primitive the negotiated suite needs**: ECDH P-256,
  ECDSA P-256 verify, AES-256-GCM, HMAC-SHA-256 and HMAC-SHA-512. All
  available. (AES-CBC is available too, for what it is worth: the README's
  warning about it does not bite here because the board never offers it.)
- **a browser WebSocket reaches the relay and the board answers**: 2 binary
  frames, 390 bytes, `SSH-2.0-unleashedBBS`, then a KEXINIT (message 20)
  parsed out of the stream.

That last one is worth stating carefully, because it is the only part of the
browser path that was in real doubt and it now is not: a page in a browser,
with no library at all, reaches the firmware's key exchange through the
relay. What is left for the library is the key exchange itself, which runs on
the Web Crypto primitives just proved present.

---

## 6. The detector, and why the answer is UTF-8

`Bbs::sshWait` calls `det.ansiOnly()` and probes anyway, because a pty
request carries the size but not the character set. The probe is
`DETECTING TERMINAL`, `ESC[6n`, `U+2500` as three UTF-8 bytes, `ESC[6n`.
`Detector::finishAnsi` takes UTF-8 if the second column report is exactly one
more than the first.

Modelled as xterm.js behaves (decode UTF-8, advance one cell per character):
columns 19 then 20, delta 1, and the board logged **`ANSI-UTF8`**. Modelled
as a CP437 terminal (one cell per byte): 19 then 22, and the board logged
`ANSI-CP437`. Both correct, and the PETSCII question is never reached in
either.

So **the art reaches a browser as UTF-8**, through `Term::cp437`, which maps
each byte over 0x7F through `kCp437Hi`. Rendered back onto a character grid,
the stock welcome screen's wordmark comes out whole:

```
         ██  ██ ███ ██ ██     ██     ██  ██ ██     ██  ██ ██     ██  ██
         ██  ██ ███▄██ ██     █████  ██████  ▀███▄ ██████ █████  ██  ██
         ██  ██ ██ ███ ██     ██     ██  ██ ▄▄  ██ ██  ██ ██     ██  ██
         ██▄▄██ ██ ▀██ ██████ ██████ ██  ██ ▀████  ██  ██ ██████ ██▄▄█▀
                      E L E C T R O N I C   F R E E D O M
              no web  ∙  no cloud  ∙  no browser  ∙  real hardware
  ════════════════════════════════════════════════════════════════════════════
    ∙ Node 1 of 10      ∙ Terminal ANSI-UTF8      ∙ Sun 04 Oct 2026 23:39
```

U+2588, U+2580, U+2584, U+2550, U+2500, U+2219 and the µ at U+00B5. Zero
bytes in the whole session failed to decode as UTF-8.

**Still unverified, and cheap to check in phase 4:** that xterm.js's renderer
draws U+2580-U+259F itself, continuously across cell boundaries, rather than
leaving them to the font. The plan asserts it; GitHub's raw path for the file
that would settle it has moved, and it was not worth more guessing. Once the
bundle exists, write the captured session bytes into xterm.js and look at the
wordmark: if the blocks have seams, the WebGL or canvas addon is the fix, not
a font.

---

## 7. The relay

About 300 lines of stdlib asyncio, loopback only, throwaway. It does the
RFC 6455 handshake, unmasks client frames, refuses an unmasked frame and
anything over 1 MB before allocating, answers ping, and copies bytes. It
parses nothing above the frame layer and logs no payload.

Two things it did that the real one must keep:

- **the client never names a host or a port.** It names a key; the relay
  resolves it against its own table (standing in for the site's
  `boards-cache.json`) or closes with 1008 `no such board`. Demonstration 12.
- **the close reason is useful.** `up 1415 down 11394 in 43.0s (the browser
  closed it)` is the whole log line, and it is the right shape for phase 3:
  who, which board, how long, how much, how it ended, no payload.

The spike's relay also serves the page and takes a POST of the page's own
report. **The real relay must do neither**: those belong to the site. It is
marked in the file so nobody copies it across.

---

## 8. What is still unproven

| | Why | What it needs |
|---|---|---|
| **The measured gzipped bundle size** | node and npm refused | the two tools, then one esbuild run. The raw figure for scale: the package's `.js` files total **611,638 bytes** unminified, of which about 45 KB is the `algorithms/node/` tree a browser build drops. A minified, gzipped browser bundle is very unlikely to be the 400 KB that would kill the plan, but this is an estimate and the plan was right to want a measurement |
| **An end-to-end browser login** | the same | the bundle. Everything it would talk to is proved |
| **xterm.js drawing the block glyphs** | the same | section 6 |
| **`none` auth from a client that sends it** | wolfSSH's client has no way to send a `none` request (`WOLFSSH_ALLOW_USERAUTH_NONE` is the server side only), and this machine allows no other SSH client | nothing, probably: `Bbs::sshAuth` returns true for a handle it cannot find **before** it looks at the auth kind (`bbs_ssh.cpp`, the `Lookup::Missing` arm precedes the `kind != AUTH_PASSWORD` check), so `none` and a password take the same path for a non-account name. Demonstration 11 proves that path with a password. Worth one real check with dev-tunnels-ssh in phase 4 rather than another workaround |

**On the "free fallback" idea in the plan's section 3.** It stands: a client
with no SSH password auth can still get in, because the board sends an
unknown user name to its own handle prompt. The cost the plan names is real
and worth repeating in the page's copy: a password typed at the board's own
prompt goes to the relay as a keystroke-at-a-time timing pattern, where SSH
password auth sends it in one packet. **So the page should use SSH password
auth whenever it can**, and the fallback is for guests.

---

## 9. What phases 3 and 4 should do differently

1. **Write the P-256 dependency into `sshd.cpp`, in phase 2, not phase 4.**
   One sentence beside the algorithm lists saying that `ecdh-sha2-nistp256`
   and the ECDSA host key are the only path a browser has. This is the
   finding with the longest fuse: it breaks nothing today and everything
   later, and nothing would report it.
2. **The relay's per-session byte cap can start much lower than 10 MB.** An
   ordinary 43-second look around is 11 KB. 10 MB is a file transfer's
   allowance; if Rob wants browser callers to download, say so deliberately,
   and if not, 1 MB is already thirty times a long chat session.
3. **The page must treat the socket as a byte stream, not as frames.** The
   first cut of the spike's own page looked for the KEXINIT inside each
   WebSocket frame and missed it when it arrived in the second one. That is
   this project's "a partial thing treated as whole" shape, in a new place,
   and the SSH library will get it right but the page's own TOFU and error
   handling must too.
4. **The phase 4 page cannot be tested with `--virtual-time-budget`.** It
   makes Chrome fire the page's timers in milliseconds of wall clock, which
   closed the spike's socket before the board's KEXINIT arrived and produced
   a confident, wrong FAIL. Render screenshots from a page that carries its
   data, and run the live page on a real clock.
5. **Phase 4's checks should assert the detector's answer, not just that the
   screen looks right.** `ANSI-UTF8` in the board's log is one grep and it is
   the difference between the art arriving and the art arriving as mojibake.
6. **Keep a grid read-back in the phase 4 test.** Rendering the captured
   bytes onto a character grid with a ruler is what showed the wordmark whole
   and the 99-column rows; a hex dump shows neither.

Two traps found on the way that are not about browser SSH at all and will
bite somebody else:

- **The firmware's host build has 80 to 96 byte path buffers**
  (`sshd.cpp` `keyPath`, and the config path), so a board run out of a long
  directory silently loses its `system.cfg` and its host keys. The only
  symptom was `cfg: defaults (no system.cfg)` and `ssh: host_ed25519 is there
  but will not read` for a file that did not exist. Run host boards out of
  short paths; `/tmp/bbs-<tag>` as the harness already does.
- **wolfSSH's client sends a pty-req of 80x24 whatever the terminal is**,
  because `GetTerminalInfo` is behind `HAVE_SYS_IOCTL_H` and the host build
  does not define it, so the `#else` "sane defaults" branch runs. That makes
  `host/ssh_call` unable to test anything about pty geometry, which is why
  the spike built a copy linked against one wolfSSH object recompiled with
  that define. Worth knowing before somebody writes a geometry test against
  `ssh_call` and finds it always says 80.

---

## 10. How to rebuild and re-run it

The spike lives in the session scratchpad and is deliberately not in any
repository. It is five small programs; this is enough to put it back.

**Build, from `host/`:**

```
make -j16 bbs_host_s3 ssh_call
```

and two throwaway clients beside it:

```
# devtun_call: an SSH client held to exactly dev-tunnels-ssh's reachable set
g++ -std=c++17 -O1 -I../src -DBBS_HOST -DWOLFSSL_USER_SETTINGS \
    -I../components/wolfssh -I../components/wolfssh/wolfssl \
    -I../components/wolfssh/wolfssh \
    -o devtun_call devtun_call.cpp libwolfssh_host.a -lpthread

# pty_call: host/ssh_call.cpp verbatim, linked against one wolfSSH object
# recompiled so its pty-req carries the real terminal size (section 9)
cc -c -O1 -w -DWOLFSSL_USER_SETTINGS -DHAVE_SYS_IOCTL_H \
   -I../components/wolfssh -I../components/wolfssh/wolfssl \
   -I../components/wolfssh/wolfssh \
   -o ws_internal_ioctl.o ../components/wolfssh/wolfssh/src/internal.c
g++ -std=c++17 -O1 -I../src -DBBS_HOST -DWOLFSSL_USER_SETTINGS \
    -I../components/wolfssh -I../components/wolfssh/wolfssl \
    -I../components/wolfssh/wolfssh \
    -o pty_call ssh_call.cpp ws_internal_ioctl.o libwolfssh_host.a -lpthread
```

`devtun_call.cpp` is a 170-line client: `wolfSSH_CTX_SetAlgoListKex`
`ecdh-sha2-nistp256`, `SetAlgoListKey` `ecdsa-sha2-nistp256`,
`SetAlgoListCipher` `aes256-gcm@openssh.com`, `SetAlgoListMac`
`hmac-sha2-256`, a public-key-check callback that writes the server's key
blob to a file, password auth, a shell, and it prints what it offered and
whether it opened.

**The three Python programs**, stdlib only:

- `relay.py` — WebSocket (RFC 6455 subset: handshake, unmask, 7/16/64-bit
  lengths, ping, close; binary frames only) to TCP, resolving a key against a
  `key host port` table and closing 1008 for anything else. Serves the spike
  page and takes a POST of its report, which the real relay must not.
- `wsbridge.py` — a local TCP listener that carries each connection to the
  relay over a WebSocket, so an ordinary SSH client exercises the whole relay
  path. Stands in for the browser's socket layer.
- `kexsniff.py` — a TCP tee in front of the board's SSH port keeping the
  first 16 KB each way, plus a parser for the identification lines, both
  KEXINIT name-lists and the host key blob out of KEX_ECDH_REPLY, and the
  RFC 4253 7.1 choice (first client algorithm the server also has).

**The driver**, `spike.py`: writes a data directory with `ssh_port` set and a
`users.txt` whose hash it computes the way `users.cpp` does
(`SHA256(salt||pw)` then 999 x `SHA256(h||salt)`, stored `hex(salt)$hex(h)`);
starts the board; runs the negotiation and fingerprint checks; then runs
sessions through the relay with the client on a **pty whose winsize it sets**
(`TIOCSWINSZ`) and a terminal model that tracks the column as xterm.js would
and answers `ESC[6n`. Ports are literal: board 7390 telnet, 7391 ssh, 7392
backup, tee 7500, relay 8082, bridge 7400. **Run it out of a short directory**
(`/tmp/bbs-webssh`), see section 9.

**The browser half**: `browser.py` leaves the board and relay up and writes
`page/facts.json` (the host key blob, base64, and the fingerprint the board
prints); `page/index.html` computes the fingerprint with Web Crypto, probes
the Web Crypto primitives, opens the WebSocket and reads the board's
identification and KEXINIT, then POSTs its report. Chrome:

```
chrome.exe --headless=new --disable-gpu --no-first-run \
  --user-data-dir=<scratch>\chrome-profile --window-size=1200,900 \
  http://127.0.0.1:8082/
```

with **no** `--virtual-time-budget` for the live run, and a separate
`--screenshot` run against a page that carries its data rather than fetching
it. On this machine `--screenshot` through PowerShell's call operator returns
before Chrome writes the file; `Start-Process -Wait -NoNewWindow` does not.

`grid.py` renders a captured session onto a character grid with a column
ruler, which is how section 6's wordmark and the 99-column rows were read.
