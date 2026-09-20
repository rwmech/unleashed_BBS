<!--
µnleashed BBS: COMMANDS.md

Every command, key, limit and system.cfg setting the BBS understands.

Copyright 2026 - Robert Mech
License: GNU General Public License v2 or later
SPDX-License-Identifier: GPL-2.0-or-later

Documentation for µnleashed BBS, part of the same distribution as the
source. See the LICENSE file for terms.
-->

# µnleashed BBS: command reference

Version 0.10.0. This file tracks every command and key the BBS understands, and is updated with each build that changes them.

## Calling in

- Port 6400, telnet or raw TCP. `unleashed.local` resolves on the LAN through mDNS.
- 6 caller nodes. When all six are busy, the next caller gets the busy line (see below), and anyone beyond that gets `BUSY` and an immediate hangup.
- The terminal type is detected on connect:
  - Telnet clients (PuTTY, SyncTERM) are switched to character mode and detected straight away.
  - Other ANSI terminals are detected automatically within about 2 s.
  - If there is no answer, the BBS shows `HIT DEL OR BACKSPACE`. INST/DEL on a C64 selects PETSCII, and Backspace on a PC selects ASCII.
  - PETSCII callers then answer `40 OR 80 COLUMNS (4/8)?`.
- A line whose far end disappears without hanging up (a C64 switched off, a pulled cable) is dropped within about 90 seconds, staff lines included.

## Logging in

Full guide to accounts: [USERS.md](USERS.md).

- A hint above the first prompt says how to get in: `New? Type a handle to join or visit.` (it follows the `self_register` and `guest` settings).
- `Enter your handle:` is the same for everyone. It accepts letters, digits, space, `-`, `_` and `.`, up to 20 characters. A handle must start with a letter or digit, and `SYSOP` is reserved. Case doesn't matter.
- A handle that isn't accepted rubs out in place, the reason flashes on the same line and rubs out too, and you type again on that line. 40-column screens get a short reason (`Reserved handle`), wider ones the full sentence.
- A known handle asks `Password:`. After Enter the stars spin, rub out and turn into `ACCESS GRANTED` on the same line. A wrong password flashes `ACCESS DENIED` there and clears for another try. 3 wrong passwords hang up the call; 5 for one handle within 15 minutes lock that handle for 15 minutes. ESC goes back to the handle prompt.
- An unknown handle shows `<handle> is new here.` and asks `[R]egister, [G]uest or [N]ew handle?`, offering only what `self_register` and `guest` allow (no `[R]egister` once `max_users` is reached).
  - `R` opens the sign-up form: password twice, name, email, and optional address, phone and profile.
  - `G` logs in as a guest under that handle (see Guests below).
  - `N`, ESC or Ctrl-C go back to the handle prompt. Other keys are ignored.
- A handle a guest is using right now is refused in place (`That handle is online right now.`).
- With sign-ups and guests both off, an unknown handle is refused in place (`No account. The sysop creates accounts here.`).
- A locked account is refused before the password.
- If nothing is typed at the handle prompt, a warning appears at 30 s, followed by your partial input redrawn. The line hangs up at 60 s.
- After login you see your node, the date and time, your call count and your remaining time. If `screens/bulletin.*` exists, it plays next.

### Guests

- Type any handle that has no account, then `G`. On by default; `guest = no` turns it off.
- Guests keep the handle they typed. WHO, LAST, NODES and DASH mark them with `*` (`Visitor*`) and explain it with a `* guest` footnote under the list.
- Nothing is saved: no account, no profile, no call count. The call still appears in `LAST`, like every call, marked `*`.
- Nobody else can take a guest's handle while the guest is on. Once they leave, the handle is free again (and anyone may register it).
- 15 minutes per call (`guest_minutes`), with the usual warnings at 5 and 1 minute. No daily limit.
- `PROFILE` and `PASSWORD` don't exist for guests (not in HELP, answered as unknown). `INFO` without a handle says `Guests have no account.`
- Everything else works, including `PAGE`.
- Guests can't become staff. `BYE <anything>` from a guest is a plain logoff; the password isn't checked and doesn't count toward a ban.

## Keys

| Key | Where | Effect |
|---|---|---|
| Up / Down arrow (C64: CRSR up/down) | command prompt | recall earlier commands (last 4) |
| Space, Ctrl-C, ESC (C64: SPACE, RUN/STOP, left-arrow) | lists, screens, FX demo | stop the output |
| any other key | animations, screens | skip delays |
| Y, Enter or Space | `[More] Y/n/c` | next page |
| N, Q, ESC or Ctrl-C | `[More] Y/n/c` | stop |
| C | `[More] Y/n/c` | continue without pausing |
| any key | `WHO n`, `DASH n` refresh | stop refreshing, back to the prompt |
| ESC or Ctrl-C | command prompt | clear the line |
| Enter on an unknown command | command prompt | the line rubs out, `Unknown command. Type HELP.` (C64: `?SYNTAX  ERROR`) flashes in its place, then you type again on the same line |
| Up / Down (C64: CRSR) | forms | previous / next field |
| Enter (C64: RETURN) | forms | next field; on `Save` / `Cancel` do that |
| Left / Right | forms | move between `Save` and `Cancel` |
| F1 | forms | save from any field |
| ESC or Ctrl-C (C64: left-arrow, RUN/STOP) | forms | cancel without saving |
| Up / Down, Enter, A, D, Q | `USERS` manager | move, edit, add, delete, quit |

Plain ASCII terminals get forms as one question per line, ending in `Save (Y/n)?`.

Paging pauses at the screen height minus 2 (23 lines on a C64, 22 on an 80x24 terminal). Art screens (`.ans`, `.seq`) are never paged.

## Caller commands

Commands are case-insensitive. The letter in brackets is a shortcut: `W` is the same as `WHO`. Lists (HELP, WHO, LAST, NODES, BANS, DASH) and MEM, TERM and TIME open with a title bar (reverse video on ANSI and PETSCII, a dashed rule on plain ASCII); lists close with a rule.

| Command | Shortcut | What it does |
|---|---|---|
| `HELP` | `H`, `?` | The commands you use most, generated from the command table: only what you may run, 40 columns wide on every terminal, the shortcut letter picked out inside the word. The last line names the other menus. |
| `? chat` | | Chat and messages. |
| `? account` | | Your account, your terminal, your profile. |
| `? staff` | | Staff tools (staff only). |
| `? sysop` | | Sysop tools (sysop only). |
| `? all` | | Every menu in turn, each with its own heading. |
| `MAIL` | | Read the message waiting for you. `MAIL handle your message` leaves one. See [CHAT.md](CHAT.md). |
| `WHO` | `W` | Who is on each node: a marker, handle, terminal, minutes on, idle time (mm:ss). The busy line is never listed, and a hidden sysop or co-sysop looks like a free line to callers. Staff see hidden and lurking sessions, marked `hidden` or `lurking`, and with `NODES` get a Doing column (the last command each caller ran, verb only, never arguments) instead of the terminal. |
| `WHO n` | `W n` | The same list redrawn in place every n seconds (`who_refresh_min`..`who_refresh_max`, default 1..30) until you press a key. The footer shows the idle clock: refreshing is not input, so the idle hangup still counts down. |
| `MEM` | `M` | Heap statistics and session sizing. |
| `TERM` | `T` | Terminal type, size, telnet mode, emulated line speed. |
| `CLS` | `C` | Clear the screen. |
| `FX` | `F` | TTY effects demo. |
| `TIME` | | Date and time, minutes online, minutes left. |
| `LAST` | | The last 50 calls, newest first. |
| `ABOUT` | | What this BBS is, its version and its license. Plays `screens/about.*`, so a sysop can rewrite it. |
| `CHAT` | | Join the chat room (the `chat` plugin). Everything you type goes to everyone in the room, tagged DDial style: `#2:Daytona) hi`. The bracket is the rank: `)` a caller, `*` a guest, `>` a co-sysop, `]` the sysop. There is no prompt character: the cursor waits at the start of the line. While you are typing, nothing from the room lands on your screen; the lines wait and print in order when you press Enter. The room buffers 48 lines, and one caller may send 80 lines a minute (`rate =`), with 8 in a burst; going over tells that caller alone, and the room never sees it. `/s` lists who is there, `/?` lists every room command, `/q` or ESC leaves. Private lines, away notes, squelch, kicks, the vote to kick and messages are all in [CHAT.md](CHAT.md). |
| `SERIAL` | | Watch the serial device (the `serial` plugin). `T` takes the keyboard if you are allowed and it is free, ESC leaves. `SERIAL STATUS` prints the port, `SERIAL SET 9600 8N1` changes the line. |
| `INFO [handle]` | `I` | An account: name, member since, last call, calls, profile. Email, address and phone only on your own account (or with `USERS`). |
| `PRIVACY` | | What the board knows about you: that telnet is not encrypted, how your password is stored, what the sysop can see, and the one rule that matters. Plays `screens/privacy.*`, so a sysop can rewrite it. The same screen is offered during sign-up. |
| `PROFILE` | | Form to change your name, email, address, phone and profile. Not for guests. |
| `PASSWORD` | | Form: current password, then the new one twice. Not for guests. |
| `PAGE n message` | | Send a one-line message to node n. It arrives when that caller is back at the prompt: a bell, a flashing ` PAGE ` tag that rubs out, then the message. |
| `DND` | | Toggle do-not-disturb: pages to you are refused. |
| `BAUD n` | | Emulate 300, 1200, 2400, 9600 or 19200 bps. `BAUD OFF` for full speed. |
| `G` | | Log off after a `Log off (Y/N)?` confirm. |
| `BYE` | | Log off now. `OFF`, `LOGOFF` and `QUIT` do the same. |

A marker sits between the node number and the handle in WHO, NODES, LAST, DASH and the user manager, with a key line under the list:

```
1 NormalUser
2*GuestUser
3>CoSysop
4]Sysop
---------------------------------------
*GUEST  >CO-SYSOP  ]SYSOP
```

The marker follows the account, so staff are marked even before they type `BYE <password>` on this call. Guests are always `*`.

Other nodes see `*** handle is on node n` and `*** handle left node n` when callers come and go.

## Timeouts and limits

| Limit | Default | Behavior |
|---|---|---|
| Handle prompt idle | 30 s / 60 s | warning, then hangup |
| Sign-up form idle | 2 min / 3 min | warning on the form's status line, then hangup |
| Shell idle | 20 min (`idle_minutes`) | warning 1 minute before, then hangup |
| Per call | 60 min (`call_minutes`) | warnings at 5 and 1 minute left, then hangup |
| Per day | 480 min (`day_minutes`) | counted per account, saved at each logoff; checked at login |
| Guest call | 15 min (`guest_minutes`) | warnings at 5 and 1 minute left, then hangup; no daily limit |

Staff holding `NOLIMITS` (the sysop always does) are exempt from idle and time limits.

## Busy line

When all 6 nodes are in use:

1. The caller is detected like anyone else and shown `screens/busy.*`.
2. `Disconnecting in 10` counts down, then the line hangs up.
3. Pressing a key during the countdown opens a handle prompt (no account password, no sign-up). The only command that works from there is `BYE <sysop password>`, which must come within 60 s. Anything else, co-sysop passwords included, logs off.

## Staff: sysop and co-sysops

### Levels

| Level | Password key | Where it lives | Permissions |
|---|---|---|---|
| Sysop | `sysop_password` | hidden sysop node `[S] Sysop:`, frees its caller node | always all |
| Co-sysop 1 | `cosysop1_password` | stays on its caller node | `CO1` column of `[access]` |
| Co-sysop 2 | `cosysop2_password` | stays on its caller node | `CO2` column of `[access]` |

There is one sysop node. A second sysop login while it is in use is a plain logoff. Any number of callers can hold co-sysop levels at once.

Entering a staff password also marks that caller's account with the rank, which is what the `>` and `]` markers in the lists come from. The mark stays until staff change it in `USER EDIT`. Guests and the busy line have no account, so nothing is marked.

### Getting in

Log in with your account as usual, then type `BYE <password>` at the command prompt. Staff access is separate from accounts on purpose: guessing an account password never grants staff rights.

- Everything after `BYE ` is echoed as `*`, and the line is never stored in history or logs.
- The sysop password moves you to the sysop node. This also works from the busy line, so the sysop can get in when every node is full.
- A co-sysop password grants that level in place. The busy line has no node to keep, so a co-sysop password there is a plain logoff.
- Only a raise in level counts. Entering the same or a lower level's password is a plain logoff.
- A wrong password is an ordinary logoff. 3 wrong passwords from one IP within 15 minutes ban that IP for 15 minutes; banned connections are dropped silently.
- Guests can't elevate: from a guest, `BYE <password>` is a plain logoff whatever the password. Staff log in with their account first.
- An empty password in `system.cfg` disables that level.

### Staff commands

All caller commands still work. Node arguments are `1`-`6`, `S` (sysop node) or `B` (busy line). A command that isn't granted answers like an unknown command, and HELP lists only the commands you hold.

| Command | Permission | What it does |
|---|---|---|
| `DASH` | `DASH` | Dashboard on one 40-column screen: date and time, version, uptime, NTP state, heap (free, lowest, largest block), every node with what it is doing (last command, or connect / login / sign-up), idle and minutes left, calls today, active bans, busy line, Wi-Fi signal (dBm of the joined access point), backup window state, the last 5 calls. |
| `DASH n` | `DASH` | The dashboard redrawn every n seconds (same limits as `WHO n`) until a key. |
| `NODES` | `NODES` | Every session: handle, IP, minutes left, idle (plus terminal type on wide screens). |
| `KICK n [message]` | `KICK` | Disconnect node n. The caller sees `Disconnected by sysop: message`. |
| `BROADCAST message` | `BROADCAST` | Send `*** Sysop: message` to every logged-in node, announced like a page with a bell and a flashing ` SYSOP ` tag. |
| `SNOOP n` | `SNOOP` | Mirror node n's output to your screen. `Q`, ESC or Ctrl-C stops. Both terminals must be the same type, and only one watcher per node. |
| `TIME n +m` / `TIME n -m` | `TIME` | Add or remove minutes for node n. The caller's time warnings re-arm. |
| `SHOW` | `HIDE` | List yourself in WHO (pages on). |
| `HIDE` | `HIDE` | Remove yourself from WHO. The sysop starts hidden; co-sysops start visible. |
| `LURK` | `HIDE` | Toggle lurking: hidden from WHO and pages refused. |
| `BANS` | `BANS` | Active IP bans and minutes remaining. |
| `PLUGINS` | any staff | Every plugin compiled in: version, whether it is running, and why not. Also free disk space and the reserve. |
| `FILES` / `F` | all | **Goes into the file area**, the way `CHAT` goes into the room, rather than printing a list and returning. A screen plays on the way in if the board has `screens/files`, then the areas appear as a numbered menu laid out in as many columns as the terminal has room for. A digit opens an area, `Q` goes back one level and `Q` again leaves. `FILES n` enters and opens that area in one go. Only on a board with a card: the plugin does not start without one, so on a cardless board the command does not exist rather than offering an empty file area. |
| `DOWNLOAD file` / `D` | area's download level | Sends a file from the area you last opened, by XMODEM or XMODEM-1K. Start your terminal's receive when the board says to. One transfer at a time board-wide: a second caller is told to try again in a moment rather than queued. Named by the command because XMODEM carries no filename on the wire. |
| `UPLOAD file` / `U` | area's upload level | Receives a file into the area you last opened. **It is not in the area when it finishes.** It waits in a staging folder until staff approve it, so nothing a caller sends is visible to anyone else until somebody has looked at it. You name the file before the transfer starts, again because XMODEM has no filename on the wire. Limits: 4 MB per file and 20 waiting per area. |
| `UPLOADS` | area's delete level | Lists every upload waiting for approval, across all the areas you can approve in, with its size and which area it landed in. Staff are also told the count when they log in. |
| `APPROVE file` | area's delete level | Moves a waiting upload into the area you last opened, where everyone who may read it can see it. Describe it afterwards with `DESC`. |
| `REJECT file` | area's delete level | Throws a waiting upload away. |
| `ERASE file` | area's delete level | Removes a file from the area you last opened. Never `FILES.BBS`, which is the area's catalogue rather than one of its files. |
| `DESC file text` | staff | Set a file's description in the area you last opened. Empty text clears it. Typed at the command prompt, not inside the file area, because the file area takes keys rather than lines; the area you opened is remembered after you leave it. Written to `FILES.BBS` in that folder, which is plain text you can edit on a laptop with the card in hand. |
| `SD` | sysop | SD card status: type, mount point, free space, and where screens are coming from. With no card it says which pins it tried, because "no card found" without them sends you to re-seat a card that was never the problem. |
| `SD MOUNT` | sysop | Mount the card without rebooting. **Pauses the whole board** for a few hundred milliseconds while it negotiates over SPI, which is why it is typed rather than retried on a timer. |
| `SD UNMOUNT` | sysop | Flush and release, so the card can be pulled safely. Screens fall back to the stock set. |
| `UNBAN a.b.c.d` | `UNBAN` | Lift a ban. |
| `USERS` | `USERS` | User manager: cursor list of accounts with edit, add and delete (ANSI, PETSCII). A paged list on plain ASCII. |
| `USER ADD` | `USERS` | Add-account form: handle, password, fields, Level, Locked. |
| `USER EDIT handle` | `USERS` | Edit-account form. Empty `New pass` keeps the password. Renames follow callers who are online. |
| `USER DEL handle` | `USERS` | Delete after `Delete handle (y/N)?`. Not your own account. |

Staff may only add, edit, lock, rename or delete accounts at their own rank or below, and the `Level` field only offers their own rank and below. So a co-sysop cannot touch the sysop's account, and nobody can promote themselves. `Space` steps the `Level` field through the choices, or press the first letter (`u`, `2`, `1`, `s`).
| `DROP` | any staff | Co-sysop: give up staff access. Sysop: leave the sysop node for a free caller node. Time limits apply again from now. |

Rank rules:
- `KICK` and `SNOOP` only work on callers and lower levels. Co-sysop 1 can act on co-sysop 2; nobody can act on the sysop.
- A hidden co-sysop shows as a free line in WHO, and hidden higher-level staff are masked in `NODES`.
- Any staff level sees sysop-node calls in `LAST`. `NODES` permission also shows caller IPs there on wide screens.

### Sysop screens

| Command | What it does |
|---|---|
| `SYS` | The whole board on one screen, in groups: network (SSID, signal in dBm with a word for what it means, channel, address, port), memory (heap free, the lowest it has been, the biggest block, session size), storage (used, free, what is held back for the board), load (uptime, clock, scheduler work per pass in microseconds, worst pass, passes since boot) and traffic (nodes busy and the peak, calls answered since boot, records in the caller log, plugins running, active IP bans). |
| `CALLS` | The caller log bucketed by hour of the day, as a bar chart, with the busiest hour named. Costs one pass over the log and no storage. |
| `ANNOUNCE` | Whether this board is listed in a directory, when each one last answered, and the public address the directory sees. `ANNOUNCE TEST` prints the exact payload and sends nothing; `ANNOUNCE NOW` sends a heartbeat immediately. Off until switched on: see [ANNOUNCE.md](ANNOUNCE.md). |
| `CONFIG` | The settings, page by page. On its own it lists the pages: `board`, `limits`, `accounts`, `backup`, `staff`, and one per plugin. `CONFIG limits` opens that page as the same kind of form the user manager uses: Up and Down move, F1 saves, ESC cancels. Only what you changed is written, the rest of `system.cfg` is left exactly as it was, comments included, and the board reloads the new settings straight away. Passwords show as `********` and are only written when you type a new one. One sysop edits at a time. |

`CONFIG` is the sysop's own command: co-sysops do not get it whatever the `[access]` matrix says, because it can change the staff passwords.

A setting whose value is several values packed with bars, as a file area is,
is not a text box on its page. It is a button showing the area's name, and
Enter (or space) on it opens **that area as a page of its own**: Path, Name,
Read and Write, with the two levels stepped through with space or picked with
their first letter rather than spelled out. Save or ESC comes back to the
page the button was on. The file still keeps the bar-separated form, so a
`system.cfg` edited by hand on a laptop reads and parses exactly as before.
On a plain ASCII terminal, which has no cursor to put a button under, the row
becomes `Area 1 [C64 Downloads] open (y/N)?` instead.

## Backup window (sysop)

The sysop can download and upload everything that matters (`system.cfg`, `users.txt` and the screens) as one `.zip`, without reflashing. Full steps: [BACKUP.md](BACKUP.md).

- Log in as sysop, then press BOOT on the board. The console shows `*** Backup open 5 min: http://<ip>:8080/backup.zip`.
- Download: `curl.exe -o backup.zip http://<ip>:8080/backup.zip`. No confirmation; staff passwords come out as `***`, account passwords only as salted hashes.
- Upload: `curl.exe -T backup.zip http://<ip>:8080/restore`. The sysop console shows what arrived and asks `Accept upload (Y/N)?`.
  - `Y` applies it at once, `N` discards it. No answer in 2 minutes counts as `N`.
  - While the question is on screen, only `Y`, `N`, ESC or Ctrl-C are accepted.
- The window closes after `backup_window_minutes` or when the sysop logs off.

## Screens

Files in `screens/`, chosen by terminal type. Names, formats and upload limits: [SCREENS.md](SCREENS.md).

| Name | When |
|---|---|
| `welcome` | after detection |
| `about` | the `ABOUT` command |
| `bulletin` | after login (optional, none ships) |
| `busy` | busy line |
| `goodbye` | logoff |

@-codes: `@BBS@ @VER@ @NODE@ @NODES@ @USER@ @TERM@ @COLS@ @DATE@ @TIME@ @CLS@ @BELL@ @DELAY:ms@ @SPIN:ms@`

## system.cfg

On a running board, edit `system.cfg` through the backup zip ([BACKUP.md](BACKUP.md)); it applies without a reboot. For a fresh board, `data/system.cfg` (git-ignored, copy from `data/system.cfg.example`) goes on with `pio run -t flashall`, and the board copies it to `userdata` the first time it boots. From then on that copy is the board's, and reflashing does not overwrite it.

| Key | Default | Meaning |
|---|---|---|
| `hostname` | `unleashed` | DHCP and mDNS name (`unleashed.local`), `a-z 0-9 -`, applies at reboot |
| `tz` | `UTC0` | POSIX TZ string, e.g. `CST6CDT,M3.2.0,M11.1.0` |
| `ntp_server` | `pool.ntp.org` | clock source |
| `sysop_password` | empty | sysop level, empty = disabled |
| `cosysop1_password` | empty | co-sysop 1 level, empty = disabled |
| `cosysop2_password` | empty | co-sysop 2 level, empty = disabled |
| `idle_minutes` | `20` | shell idle hangup, 0 = never |
| `call_minutes` | `60` | per-call limit, 0 = unlimited |
| `day_minutes` | `480` | per-day limit, 0 = unlimited |
| `backup_port` | `8080` | HTTP port while the backup window is open (not 6400) |
| `backup_window_minutes` | `5` | how long one button press keeps the window open (1..60) |
| `backup_button_gpio` | `0` | button pin, active low (BOOT on dev boards), -1 = no window |
| `who_refresh_min` | `1` | lowest `WHO n` / `DASH n` refresh, seconds |
| `who_refresh_max` | `30` | highest `WHO n` / `DASH n` refresh, seconds |
| `activity_led_gpio` | `2` | LED that blinks on network traffic (the blue LED on DOIT-style boards), -1 = none |
| `self_register` | `yes` | `no`: unknown handles can't sign up, staff add accounts |
| `max_users` | `250` | account limit, 1..250. Not a space limit: `userdata` holds roughly 1,380 accounts. The cap is that the list indices are `uint8_t`, which reaches into every list on the board, so raising it is its own piece of work. The SD card does not help and is not meant to: accounts stay on internal flash so they survive the card failing. |
| `guest` | `yes` | `no`: unknown handles are not offered `[G]uest` |
| `guest_minutes` | `15` | per guest call, 0 = unlimited; guests have no daily limit |

Keys must appear above the first `[section]` line. Sections are `[access]` for the staff matrix and `[plugin:name]` for each plugin (see [PLUGINS.md](PLUGINS.md)).

### Plugins

Four plugins ship with the firmware:

| Plugin | What it does | Defaults |
|---|---|---|
| `chat` | one chat room, DDial style, with a few lines of history for whoever joins | `read = all`, `write = all` |
| `serial` | shares a serial device: one operator types, any number watch | `read = all`, `write = staff` |
| `announce` | posts a small heartbeat to a directory so the board can be found | `sysop` throughout |
| `sd` | mounts an optional SD card and lets its screens override the stock ones | `sysop` throughout |
| `files` | publishes folders on the card as file areas callers can browse | `read = all`, `write = staff` |

Chat and `sd` are on by default, even with no section in `system.cfg`; `enabled = no` turns either off. `sd` on a board with no card costs one failed mount at boot and then nothing. The serial bridge waits to be switched on, since it needs wiring, and so does `announce`, since it is the one thing that talks out. Turning either off costs nothing: no commands, no hooks, no memory. The `example` plugin is the template for writing your own ([PLUGINS.md](PLUGINS.md)).

The SD card is optional and the board is complete without one. What goes on
it is the things that grow without limit and can be lost: message bases, file
areas, a sysop's own screens. What stays on internal flash is everything that
has to survive the card failing, which is the accounts, the configuration and
the caller log. FAT32 rather than LittleFS so the card can be pulled and read
on any laptop, and the price of that is that FAT is not safe against losing
power mid-write, which is why nothing that matters lives there.

```
[plugin:sd]
enabled = yes
cs      = 5         ; GPIO. Move to 4 if the board will not boot with a card
mosi    = 23
clk     = 18
miso    = 19
screens = yes       ; screens on the card override the stock set, per file
```

Wiring: `3V3` (**not VIN**), `GND`, `CS` to D5, `MOSI` to D23, `CLK` to D18,
`MISO` to D19. GPIO5 is a strapping pin, so if the board will not start with
the card attached, move `CS` to D4 and set `cs = 4`.

A file area is a folder on the card that the sysop mounts under a name. The
path is never shown to callers, so an area can point at a folder you already
have, and a folder with no entry here is not an area at all, which is what
lets you keep your own files on the same card.

```
[plugin:files]
enabled = yes
read    = all        ; who may browse
write   = staff      ; who may write descriptions
area1   = pub/c64 | C64 Downloads
area2   = pub/text | Text Files
area3   = screens | Screens | staff | sysop
```

Up to eight areas, and an area may set its own read and write levels after
the name. Leave them off and it uses the plugin's; set one through
`CONFIG files` and both are written down, because the form shows you the
level the area is running under and saving it is you agreeing to it. An area a caller may not
read is not listed for them, and opening it by number is refused in the same
words as a number that is not an area at all, so the command cannot be used
to find out which numbers are hiding something. The number is the config
slot, the same for everybody, rather than a position in whatever list you
happen to see.

The path is relative to the card, and the board creates the folder when it
starts, so setting an area up does not mean pulling the card and finding a
PC.

`screens` is worth knowing about: it is the folder the board already reads
its screen overrides from, the one `SD` prints. Mounting it as a staff area
gives you a view of your own screens from the board. Note that is `screens`,
not `admin/screens`; any other path is just an ordinary folder that no
screen comes from.

Each area carries four permission levels of its own, and they are separate
because they are different kinds of trust:

```
area1 = pub/c64 | C64 Downloads | read | upload | download | delete
```

- **read** sees the area in the menu and lists what is in it
- **upload** puts files in, and writes descriptions
- **download** takes files out
- **delete** removes files, and approves or rejects uploads

All four are optional and each falls back on its own: read to the plugin's
`read`, upload to the plugin's `write`, **download to that area's own read**,
and delete to the plugin's `admin`. Delete never inherits from upload, so an
area that says nothing about deletion does not get it from permission to
upload.

Upload sits before download in that line even though it reads oddly. It is
where the old single `write` level used to be, and moving it would silently
have turned every already-configured area's upload level into its download
level.

An area an ordinary caller may not read is not listed for them at all, and
opening it by number is refused in the same words as a number that is not an
area, so the menu cannot be used to find out which numbers are hiding
something.

Descriptions live in `FILES.BBS` inside each folder, one line per file,
`name description`, the way every BBS did it; the board rewrites that file
through a temp file and a rename, because FAT is not safe against losing
power mid-write.

There is no file transfer yet, so an area is for browsing and for files you
put there yourself. Uploads and moving files between areas wait for XMODEM.

Each plugin reads its own section:

```
[plugin:example]
enabled = yes
read    = all        ; all | users | staff | co2 | co1 | sysop
write   = staff
admin   = sysop
greeting = howdy     ; the plugin's own keys
```

- `read` covers looking, `write` changing something, `admin` configuring the plugin itself.
- Commands you may not run are hidden from HELP and answer as unknown.
- A plugin that is off contributes nothing: no commands, no hooks, no memory.
- `PLUGINS` shows what is compiled in and what is running.

The serial bridge adds its own keys:

```
[plugin:serial]
enabled = yes
read  = all         ; who may watch
write = staff       ; who may hold the keyboard and change the line
admin = sysop
rx = 16             ; UART2 defaults on a WROOM-32E; any free pin works
tx = 17
baud = 115200
format = 8N1
```

- It uses the second UART, never the console, so flashing and `pio device monitor` keep working.
- Pins 6 to 11 (flash), 1 and 3 (console) are refused, and a transmit pin must not be 34 to 39, which are input only.
- One operator at a time. Watchers see the same stream, and a terminal that cannot keep up is told how much it skipped instead of holding up the device.
- While you are in the serial session or the chat room, the idle timeout pauses; your call time limit still counts.

A value out of range is logged and the default is kept. An upload with a bad value is rejected, so it never replaces a working config.

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
DASH           X      X    X
USERS          X      X    -
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
| `DASH` | `DASH`, `DASH n` |
| `USERS` | `USERS`, `USER ADD/EDIT/DEL`, private fields in `INFO` |

The boot log prints each level's permission bits (`cfg: sysop on co1 off perms 0x1bf ...`) so you can confirm what loaded. Bad rows are logged and skipped.

`flashall` and `uploadfs` rewrite the storage partition, which holds the screens. The live config and the accounts are on `userdata` and the caller log is on `logs`, so neither is touched. A backup is still worth having before a big change, and it is the only way back if you move a partition, because that needs a full erase.
