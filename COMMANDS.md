# µnleashed BBS: command reference

Version 0.3.0. This file tracks every command and key the BBS understands, and is updated with each build that changes them.

## Calling in

- Port 6400, telnet or raw TCP. `unleashed.local` resolves on the LAN through mDNS.
- 6 caller nodes. When all six are busy, the next caller gets the busy line (see below), and anyone beyond that gets `BUSY` and an immediate hangup.
- The terminal type is detected on connect:
  - Telnet clients (PuTTY, SyncTERM) are switched to character mode and detected straight away.
  - Other ANSI terminals are detected automatically within about 2 s.
  - If there is no answer, the BBS shows `HIT DEL OR BACKSPACE`. INST/DEL on a C64 selects PETSCII, and Backspace on a PC selects ASCII.
  - PETSCII callers then answer `40 OR 80 COLUMNS (4/8)?`.

## Logging in

- `Enter your handle:` accepts letters, digits, space, `-`, `_` and `.`. A handle must start with a letter or digit, and `SYSOP` is reserved.
- There is no password until C2 adds user accounts.
- If nothing is typed, a warning appears at 30 s, followed by your partial input redrawn. The line hangs up at 60 s.
- After login you see your node, the date and time, and your remaining time. If `screens/bulletin.*` exists, it plays next.

## Keys

| Key | Where | Effect |
|---|---|---|
| Up / Down arrow (C64: CRSR up/down) | command prompt | recall earlier commands (last 4) |
| Space, Ctrl-C, ESC (C64: SPACE, RUN/STOP, left-arrow) | lists, screens, FX demo | stop the output |
| any other key | animations, screens | skip delays |
| Y, Enter or Space | `[More] Y/n/c` | next page |
| N, Q, ESC or Ctrl-C | `[More] Y/n/c` | stop |
| C | `[More] Y/n/c` | continue without pausing |
| ESC or Ctrl-C | command prompt | clear the line |

Paging pauses at the screen height minus 2 (23 lines on a C64, 22 on an 80x24 terminal). Art screens (`.ans`, `.seq`) are never paged.

## Caller commands

Commands are case-insensitive. The letter in brackets is a shortcut: `W` is the same as `WHO`.

| Command | Shortcut | What it does |
|---|---|---|
| `HELP` | `H`, `?` | Command list. Plays `screens/help.*` if present. |
| `WHO` | `W` | Who is on each node: handle, terminal, minutes on, idle time (mm:ss). A hidden sysop and the busy line are never listed. |
| `MEM` | `M` | Heap statistics and session sizing. |
| `TERM` | `T` | Terminal type, size, telnet mode, emulated line speed. |
| `CLS` | `C` | Clear the screen. |
| `FX` | `F` | TTY effects demo. |
| `TIME` | | Date and time, minutes online, minutes left. |
| `LAST` | | The last 50 calls, newest first. |
| `PAGE n message` | | Send a one-line message to node n. It arrives, with a bell, when that caller is back at the prompt. |
| `DND` | | Toggle do-not-disturb: pages to you are refused. |
| `BAUD n` | | Emulate 300, 1200, 2400, 9600 or 19200 bps. `BAUD OFF` for full speed. |
| `G` | | Log off after a `Log off (Y/N)?` confirm. |
| `BYE` | | Log off now. `OFF`, `LOGOFF` and `QUIT` do the same. |

Other nodes see `*** handle is on node n` and `*** handle left node n` when callers come and go.

## Timeouts and limits

| Limit | Default | Behavior |
|---|---|---|
| Handle prompt idle | 30 s / 60 s | warning, then hangup |
| Shell idle | 20 min (`idle_minutes`) | warning 1 minute before, then hangup |
| Per call | 60 min (`call_minutes`) | warnings at 5 and 1 minute left, then hangup |
| Per day | 480 min (`day_minutes`) | counted per handle + IP until C2; checked at login |

Staff holding `NOLIMITS` (the sysop always does) are exempt from idle and time limits.

## Busy line

When all 6 nodes are in use:

1. The caller is detected like anyone else and shown `screens/busy.*`.
2. `Disconnecting in 10` counts down, then the line hangs up.
3. Pressing a key during the countdown opens a handle prompt. The only command that works from there is `BYE <sysop password>`, which must come within 60 s. Anything else, co-sysop passwords included, logs off.

## Staff: sysop and co-sysops

### Levels

| Level | Password key | Where it lives | Permissions |
|---|---|---|---|
| Sysop | `sysop_password` | hidden sysop node `[S] Sysop:`, frees its caller node | always all |
| Co-sysop 1 | `cosysop1_password` | stays on its caller node | `CO1` column of `[access]` |
| Co-sysop 2 | `cosysop2_password` | stays on its caller node | `CO2` column of `[access]` |

There is one sysop node. A second sysop login while it is in use is a plain logoff. Any number of callers can hold co-sysop levels at once.

### Getting in

From any prompt, type `BYE <password>`.

- Everything after `BYE ` is echoed as `*`, and the line is never stored in history or logs.
- The sysop password moves you to the sysop node. This also works from the busy line, so the sysop can get in when every node is full.
- A co-sysop password grants that level in place. The busy line has no node to keep, so a co-sysop password there is a plain logoff.
- Only a raise in level counts. Entering the same or a lower level's password is a plain logoff.
- A wrong password is an ordinary logoff. 3 wrong passwords from one IP within 15 minutes ban that IP for 15 minutes; banned connections are dropped silently.
- An empty password in `system.cfg` disables that level.

### Staff commands

All caller commands still work. Node arguments are `1`-`6`, `S` (sysop node) or `B` (busy line). A command that isn't granted answers like an unknown command, and HELP lists only the commands you hold.

| Command | Permission | What it does |
|---|---|---|
| `NODES` | `NODES` | Every session: handle, IP, minutes left, idle (plus terminal type on wide screens). |
| `KICK n [message]` | `KICK` | Disconnect node n. The caller sees `Disconnected by sysop: message`. |
| `BROADCAST message` | `BROADCAST` | Send `*** Sysop: message` to every logged-in node. |
| `SNOOP n` | `SNOOP` | Mirror node n's output to your screen. `Q`, ESC or Ctrl-C stops. Both terminals must be the same type, and only one watcher per node. |
| `TIME n +m` / `TIME n -m` | `TIME` | Add or remove minutes for node n. The caller's time warnings re-arm. |
| `SHOW` | `HIDE` | List yourself in WHO (pages on). |
| `HIDE` | `HIDE` | Remove yourself from WHO. The sysop starts hidden; co-sysops start visible. |
| `LURK` | `HIDE` | Toggle lurking: hidden from WHO and pages refused. |
| `BANS` | `BANS` | Active IP bans and minutes remaining. |
| `UNBAN a.b.c.d` | `UNBAN` | Lift a ban. |
| `DROP` | any staff | Co-sysop: give up staff access. Sysop: leave the sysop node for a free caller node. Time limits apply again from now. |

Rank rules:
- `KICK` and `SNOOP` only work on callers and lower levels. Co-sysop 1 can act on co-sysop 2; nobody can act on the sysop.
- A hidden co-sysop shows as a free line in WHO, and hidden higher-level staff are masked in `NODES`.
- Any staff level sees sysop-node calls in `LAST`. `NODES` permission also shows caller IPs there on wide screens.

## Screens

Files in `data/screens/`, chosen by terminal type:

| Name | When |
|---|---|
| `welcome` | after detection |
| `bulletin` | after login (optional, none ships) |
| `help` | `HELP` for callers |
| `busy` | busy line |
| `goodbye` | logoff |

@-codes: `@BBS@ @VER@ @NODE@ @NODES@ @USER@ @TERM@ @COLS@ @DATE@ @TIME@ @CLS@ @BELL@ @DELAY:ms@ @SPIN:ms@`

## system.cfg

`data/system.cfg` is git-ignored; copy it from `data/system.cfg.example`. Upload it with `pio run -t flashall`.

| Key | Default | Meaning |
|---|---|---|
| `tz` | `UTC0` | POSIX TZ string, e.g. `CST6CDT,M3.2.0,M11.1.0` |
| `ntp_server` | `pool.ntp.org` | clock source |
| `sysop_password` | empty | sysop level, empty = disabled |
| `cosysop1_password` | empty | co-sysop 1 level, empty = disabled |
| `cosysop2_password` | empty | co-sysop 2 level, empty = disabled |
| `idle_minutes` | `20` | shell idle hangup, 0 = never |
| `call_minutes` | `60` | per-call limit, 0 = unlimited |
| `day_minutes` | `480` | per-day limit, 0 = unlimited |

### [access] matrix

Keep this section last in the file: every line after `[access]` is a matrix row. `X` = allowed, `-` = denied. The SYSOP column is for reference only, since the sysop always has everything. Rows you leave out keep the defaults shown here.

```
[access]
# permission   SYSOP  CO1  CO2
NODES          X      X    X
KICK           X      X    -
BROADCAST      X      X    X
SNOOP          X      X    -
TIME           X      X    X
BANS           X      X    X
UNBAN          X      -    -
HIDE           X      X    -
NOLIMITS       X      X    X
```

| Permission | Grants |
|---|---|
| `NODES` | `NODES`, caller IPs in `LAST` |
| `KICK` | `KICK` |
| `BROADCAST` | `BROADCAST` |
| `SNOOP` | `SNOOP` |
| `TIME` | `TIME n +/-m` |
| `BANS` | `BANS` |
| `UNBAN` | `UNBAN` |
| `HIDE` | `SHOW`, `HIDE`, `LURK` |
| `NOLIMITS` | no idle hangup, no per-call or per-day limit |

The boot log prints each level's permission bits (`cfg: sysop on co1 off perms 0x1bf ...`) so you can confirm what loaded. Bad rows are logged and skipped.

`flashall` and `uploadfs` rewrite the whole filesystem, which also erases the caller log.
