<!--
µnleashed BBS: README.md

What the project is, how to build, flash and test it, and where the rest of the documentation lives.

Copyright 2026 - Robert Mech
License: GNU General Public License v2 or later
SPDX-License-Identifier: GPL-2.0-or-later

Documentation for µnleashed BBS, part of the same distribution as the
source. See the LICENSE file for terms.
-->

# µnleashed BBS

Electronic freedom on a microcontroller. No web, no cloud, no browser.

µnleashed is a telnet BBS that runs on a bare ESP32-WROOM-32E and grows into an IoT terminal server through plugins: real hardware you reach from a 1982 home computer, a glass terminal, a phone or anything else that speaks telnet.

The name is spelled with a micro sign. Where µ can't be shown (PETSCII, hostnames, file names) it's written `unleashed`.

The core has one dial-in port, 6 caller nodes, a busy line and a hidden sysop node. It also has connect-time terminal detection, user accounts with fill-in forms, guest access, screens, a line editor with history, paged output, a message bus between nodes, the TTY effects library and a shell.

Docs:

| File | For |
|---|---|
| [COMMANDS.md](COMMANDS.md) | every command, key, limit and `system.cfg` setting |
| [USERS.md](USERS.md) | signing up, logging in, guests, managing accounts |
| [PLUGINS.md](PLUGINS.md) | writing and running plugins |
| [BACKUP.md](BACKUP.md) | downloading and uploading config, accounts and screens as a `.zip` |
| [SCREENS.md](SCREENS.md) | screen formats, naming rules and upload limits |
| [CHANGELOG.md](CHANGELOG.md) | what changed in every build |

## What it's for

A board the size of a stick of gum, 40 columns of text, and a port anyone can reach. That combination turns out to be useful well beyond nostalgia.

- **Run a BBS again.** Nodes, handles, a user list, a chat room, doors, a caller log, a sysop who can page you. All of it on hardware that costs less than lunch and draws less power than a night light. Leave it on a shelf for a year.
- **Give old hardware something to do.** A C64, an Atari 800, a VT220 on a desk: machines with no browser and no future on the modern web get a live system to call, today, with no cloud account and nothing to subscribe to.
- **Somewhere to hang out.** The chat room is DDial and Gtalk in spirit: everybody in one room, one line at a time, handles and ranks in the margin. Small, fast, and with a personality that group chat lost somewhere around 2010.
- **Teach the whole stack.** One repository shows a TCP listener, a cooperative scheduler, terminal detection, a line editor, a permission model and a plugin API, all in a few thousand lines of C++ you can read on a rainy afternoon. Nothing is hidden behind a framework.
- **Reach into the physical world.** The board has GPIO. A caller who types a command can read a sensor or throw a relay, from anywhere, with a client that fits in 8 KB. That is a greenhouse you can water from a Kaypro, a garage door, a ham shack antenna switch or rotator, a sprinkler zone, a 3D print farm, a brew rig, a generator that needs starting before you drive home, a model rocket launch controller, a lab bench you want to poke at from the other side of the building, a deployment you want to kick off from a terminal on a boat.
- **Work where the web does not.** No browser, no TLS handshake, no 4 MB of JavaScript. The protocol is plain text over a socket, so it survives a satellite hop, a packet radio link, a cellular modem on one bar, and an afternoon when the DNS provider is having a bad day. A whole board fits in the bandwidth of a single modern web page.
- **Keep the conversation off the web.** Nothing here is indexed, syndicated, analysed, or sold. There is no third party in the middle: messages go from one caller to another through a chip you own, on a port you chose, and they are gone when you clear them.
- **Stay small on purpose.** No web server, no scripting runtime, no package tree to audit at 2 a.m. The whole system is one static binary with a fixed memory budget, which is a security posture as much as an engineering one. What is not built cannot be exploited.

## What can call in

Anything with a telnet client, and, with a modem emulator or a serial bridge, very nearly anything with a serial port. A partial list, all of it terminal software people still run:

- **Commodore.** C64 and C128 (CCGMS, Novaterm, StrikeTerm, Desterm), VIC-20, PET and CBM, Plus/4. On the wire: TeensyROM, WiModem232, Comet64, Zimodem, an RS-232 cartridge, or a UP9600 cable to a bridge.
- **Atari.** 8-bit (BobTerm, Ice-T, Amodem) through an 850 interface, an FujiNet or a serial bridge. ST and Falcon (Uniterm, Flash, Connect).
- **Apple.** II and IIgs (ProTERM, Ascii Express, Spectrum) with a Super Serial Card or Uthernet. Classic Mac (ZTerm, MicroPhone, White Knight), and anything since through the built-in telnet.
- **Tandy and friends.** TRS-80 models I/III/4, Color Computer (Greg-E-Term), Model 100 and 102, MSX, Amstrad CPC, ZX Spectrum, Sinclair QL.
- **Amiga.** NComm, Term, JR-Comm, A-Talk, or AmiTelnet over a network stack.
- **CP/M and S-100.** Kaypro, Osborne, Altair, Northstar, anything running Kermit or MEX through its serial port.
- **DOS and Windows.** Telix, Procomm Plus, Qmodem, Terminate, Telemate, and today SyncTERM, NetRunner, mTelnet, PuTTY, Windows Terminal.
- **Real glass terminals.** DEC VT52, VT100, VT220, VT320 and VT420, Wyse 50 and 60, Televideo 925, ADM-3A, IBM 3151, Heathkit H19. A terminal server or a USB serial adapter puts them straight on the board, and the serial bridge plugin goes the other way.
- **Teleprinters.** An ASR-33 or a Teletype Model 43 through a current loop converter, at 110 baud, if that is the sort of thing you enjoy. The BAUD command will slow the board down to match.
- **Unix and everything modern.** telnet and nc on Linux, macOS and BSD, telnet apps on iOS and Android, a terminal in a browser tab if you must, and any serial console through the bridge.

Terminal type, character set and width are detected at connect time: ANSI with CP437 or UTF-8, PETSCII at 40 or 80 columns, or plain ASCII. Nothing needs configuring at the caller's end.

## Hardware integration

The reference board is a bare ESP32-WROOM-32E: 520 KB of SRAM, 4 MB of flash, no PSRAM, Bluetooth switched off. Any ESP32 module with the same flash size will do. A dev board with a USB-serial chip needs nothing but the cable; a bare module needs 3V3, ground, EN pulled up, GPIO0 to ground for flashing, and a USB-serial adapter on the console pins.

Power: it runs from the USB port of the machine you flash it with, from a phone charger, or from 3V3 on a bench supply. Draw is a few tens of milliamps idling with six callers on, with peaks when the radio transmits, so anything that can deliver 500 mA is comfortable.

A carrier PCB with the module, a level shifter and screw terminals is the obvious next step. Not today.

### Getting it on Wi-Fi

- Credentials live in `include/secrets.h`, which is never committed. Copy the example, put the SSID and the passphrase in, and build. SSIDs are case sensitive.
- The board scans every channel and joins the strongest access point with that name, so a mesh or a pair of repeaters needs no extra configuration.
- `hostname` in `system.cfg` sets both the DHCP hostname and the mDNS name, so `unleashed.local` finds the board on a normal home network without hunting for its address.
- The clock comes from NTP at boot, and the time zone is a `system.cfg` setting. The board runs fine without either; only the log timestamps and time limits care.
- `SYS` shows the SSID, the channel, the signal in dBm with a plain word for what that means, and the address the board answers on. If callers are dropping, look there first: anything past about -75 dBm is a marginal link, and a board in a metal case is a board with a bad antenna.
- The dial-in port is 6400 by default. Forwarding it from a router is what puts the board on the internet, which is a decision to make deliberately: the protocol is plain telnet and the passwords cross the wire in the clear.

### Serial

The second UART is wired to the serial bridge plugin, so a caller with permission can drive a device attached to the board and everyone else can watch. The console UART is never touched: flashing and the serial monitor keep working while somebody is using the bridge.

- Pins, baud rate and line format are `[plugin:serial]` settings, not compiled in. `SERIAL SET 9600 8N1` changes the line while the board is running.
- The plugin refuses the pins that would break the board: the flash pins (6 to 11), the console pins, and transmit on the input-only pins (34 to 39).
- The ESP32 speaks 3V3 logic. A 5 V device needs a level shifter, and anything with a real RS-232 port needs a transceiver such as a MAX3232. Wiring a bare RS-232 line to a GPIO pin destroys the pin.
- This is what puts a glass terminal, a piece of test equipment, a radio, a PLC or a label printer on the board. It is also how the board itself can be reached from a terminal that has no network at all.

### GPIO and the physical world

- GPIO2 drives the activity LED by default, which is the LED already fitted to most dev boards. `activity_led_gpio` moves it.
- GPIO0, the BOOT button on a dev board, opens the backup window while the sysop is logged in. Hold it, and `system.cfg`, the accounts and the screens can be downloaded or uploaded over HTTP for a few minutes.
- The remaining pins are free. The planned GPIO plugin exposes them to callers as commands with their own read, write and admin levels, so reading a sensor can be open to everyone while throwing a relay is staff only.
- Reserved by the hardware, not by this firmware: 6 to 11 are the flash, 34 to 39 are input only and have no pull-ups, and the strapping pins (0, 2, 12, 15) decide how the chip boots and should be left alone unless you know what they do at reset.
- Anything switching mains, motors or an inductive load belongs behind a relay module or an opto-isolated driver with its own supply, not on a board that also has to keep six telnet sessions alive.

### Putting a retro machine on it

Old hardware reaches the board in one of three ways:

- A Wi-Fi modem emulator in the machine's own serial or cartridge port, which answers `ATDT` and opens a telnet session. TeensyROM, WiModem232, Comet64, Zimodem and FujiNet all do this.
- A serial-to-telnet bridge on the network, with a null modem cable to the machine's RS-232 port. An ESP-Link board, a Lantronix, or a small Linux box all work.
- The serial bridge plugin on this board, going the other way, for a terminal that has nothing but a serial port.

## Status (0.11.0)

- Host build: the scripted suite (`tools/testclient.py --backup`) passes, also under ASan/UBSan.
- 0.11.0 adds: HELP as menus sorted by use, with headings, colour and the shortcut letter picked out; a colour pass over WHO and MEM; `SYS` and `CALLS` for staff; the chat room's command set (`/?`, `/p`, `/me`, `/a`, `/sq`, `/t`, `/clear`), moderation (`/k`, `/b`, `/unb`, `/bans`) and a vote to kick when no staff are in the room; messages (`MAIL`, `/email`, `/e`) with expiry and a fixed number of slots; configurable chat colours and history depth; `CONFIG`, the sysop's settings manager, as the same forms the user manager uses.
- 0.10.0 (on hardware): chat room and serial bridge plugins.
- 0.9.0: the plugin API, an example plugin, the ABOUT screen.
- 0.8.0 (on hardware): staff ranks on accounts, markers in every list, staff see hidden callers.
- 0.7.0 (on hardware): guest logins, input effects in place, page alerts, title bars on lists, a staff Doing column, Wi-Fi signal on DASH.
- 0.6.0: user accounts with fill-in forms, salted SHA-256, lockout, the user manager.
- Commands come from a registry (`Command` tables); the menus, dispatch and permissions are generated from it, and plugins register into it.
- On hardware:
  - PuTTY and a C64 through TeensyROM have both called in.
  - Every PETSCII glyph (spinner, lines, shade, underscore) is verified on the C64.
  - NTP and mDNS come up on boot; the backup window works on the board.
  - Measured with six callers on: heap free 136,424, lowest 119,764, largest block 110,592, session 5,600 bytes each.

Full history: [CHANGELOG.md](CHANGELOG.md).

## Build and flash (PlatformIO)

First time on a board:

```bash
cp include/secrets.h.example include/secrets.h   # Wi-Fi SSID (case-sensitive) and password
cp data/system.cfg.example data/system.cfg       # timezone, passwords, limits
pio run -t flashall                              # firmware + partition table + data/
pio device monitor
```

After that:

- `pio run -t upload` for new firmware. Config, screens and logs stay as they are.
- Config and screens change through the backup window ([BACKUP.md](BACKUP.md)), not by reflashing.
- `flashall` / `uploadfs` rewrite the `storage` partition with `data/` (fresh board, or a deliberate reset). That wipes the accounts too: download a backup first.

Flash layout (4 MB): two 1.5 MB OTA app slots, `logs` (128 KB, caller log), `storage` (768 KB, `system.cfg`, `users.txt` and screens). Changing `partitions.csv` wipes the filesystems, so back up first.

The console prints `online <ip>  dial in: telnet <ip> 6400`. On the LAN the board answers as `<hostname>.local` (default `unleashed.local`, also its DHCP name) and advertises `_telnet._tcp`.

LittleFS and mDNS come from the ESP-IDF Component Manager (`src/idf_component.yml`). If the LittleFS fetch fails, vendor it instead:

```bash
git clone --recursive https://github.com/joltwallet/esp_littlefs components/esp_littlefs
```

`src/CMakeLists.txt` detects the vendored copy automatically.

## Test on a PC first

```bash
cd host && make && BBS_BACKUP_TEST_OPEN=1 ./bbs_host ../data 6400
python3 tools/testclient.py 127.0.0.1 6400 --backup  # add --slow for the 60 s hangup test
python3 tools/testclient.py 127.0.0.1 6400 --ban     # bans 127.0.0.1 for 15 min
```

- `make SAN=1` builds with AddressSanitizer and UBSan.
- `BBS_BACKUP_TEST_OPEN=1` holds the backup button down, so the window opens as soon as the sysop logs in.
- The same script runs against a board: `python3 tools/testclient.py <ip> 6400`. `--backup` there needs the bench build (`pio run -e esp32dev_backuptest -t upload`). Don't use `--ban` against a board, because it bans your own PC.

## Layout

```
src/config.h              compile-time sizing and timeouts
src/main.cpp              app_main: NVS, LittleFS, system.cfg, Wi-Fi, NTP, mDNS, BBS task
src/platform/             platform.h + ESP32 implementation (host version in host/)
src/core/term.*           PETSCII-40/80, ANSI (CP437/UTF-8), ASCII drivers + key parser
src/core/timeline.*       per-session timed output (non-blocking effects, baud emulation)
src/core/fx.*             TTY effects library
src/core/telnet.*         RFC 854 filter, NAWS, CR LF / CR NUL collapse
src/core/detect.*         connect-time terminal detection (settle, probe, key prompt)
src/core/editor.*         line editor, BYE password mask, command history
src/core/screens.*        streaming screen player with @-codes and paging
src/core/bus.*            per-session message ring (PAGE, notices, broadcast)
src/core/guard.*          IP ban list, per-handle login lockout
src/core/users.*          accounts in users.txt: field table, lookup, rewrite, password hashing
src/core/sha256.*         SHA-256 for password hashes
src/core/form.*           fill-in form widget (positional on ANSI/PETSCII, line prompts on ASCII)
src/core/calllog.*        caller log ring file on the logs partition (LAST)
src/core/clock.*          wall clock formatting (NTP)
src/core/sysconfig.*      system.cfg loader, validator, password redaction, access matrix
src/core/backup.*         backup window: button, HTTP in the BBS loop, Y/N approval
src/core/ziparc.*         backup zip export (stored) and import (stored/deflate, staged)
src/core/crc32.h          CRC-32 for the zip
src/core/bbs.*            listener, sessions, flow, timers, paging
src/core/bbs_shell.cpp    caller commands
src/core/bbs_sysop.cpp    sysop node and commands
src/core/bbs_users.cpp    sign-up, PROFILE, PASSWORD, INFO, USERS manager, USER ADD/EDIT/DEL
src/core/plugin.*         plugin API: registry, config sections, levels, storage
src/plugins/              the plugins compiled into this firmware
data/screens/             stock welcome, busy, goodbye (.seq/.ans/.asc) for a fresh board
data/system.cfg.example   run-time settings template
tools/mkscreens.py        regenerates the stock screens
tools/pio_flashall.py     adds the flashall target
tools/testclient.py       scripted callers
host/                     Linux build of the same core
```

## Screens

Files are looked up by terminal type, and the first match wins:

| Terminal | Order |
|---|---|
| PETSCII-40 | `.p40` `.seq` `.asc` |
| PETSCII-80 | `.p80` `.seq` `.asc` |
| ANSI | `.ans` `.asc` |
| ASCII | `.asc` |

- `.seq` files are raw PETSCII.
- `.ans` files are raw CP437. They are re-encoded for UTF-8 callers, and SAUCE metadata is skipped.
- `.asc` files are translated for every terminal type and paged at screen height.

The @-codes are listed in [COMMANDS.md](COMMANDS.md#screens).

## Effects API (`fx::`)

- Timing: `pause`, `baud`
- Text reveal: `typewriter`, `scramble`, `blink`, `marquee`
- Erase and rewrite: `rubout`, `typeRubout`, `rewrite`
- Activity: `dots`, `spinner` (Line / Dots / Arrow), `cursorBlink`, `working`, `progressBar`, `countdown`
- Modem: `lineNoise`, `hangup`, `bell`
- Decoration: `rule`

Rules for writing effects:

- Keep an effect on one line. PETSCII has no carriage return without a line feed.
- Size animations with `fitSteps()` so they never overflow the timeline.

## PETSCII glyphs (verified on a C64)

These are codes in the mixed-case charset, confirmed on a C64 through TeensyROM:

- `0xBE 0xBC 0xAC 0xBB`: quadrant spinner. PETSCII lowercase mode has no backslash, so the C64 line spinner uses these.
- `0xC0`: horizontal line.
- `0xDD`: vertical line.
- `0xA6`: checker/shade.
- `0xA4`: underscore substitute.

## License

µnleashed BBS is free software under the GNU General Public License, version 2
or later. The full text is in [LICENSE](LICENSE), and every source file carries
the notice.

Copyright 2026 - Robert Mech

The firmware also contains Apache-2.0, MIT and BSD-licensed software from
Espressif and others. Anyone handing out a compiled binary has to pass those
notices along: they are collected in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Next

- C5: plugin API. The first plugins will be GPIO and chat.
- SD card plugin: more than 100 accounts, logs on the card.
