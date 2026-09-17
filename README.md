# µnleashed BBS

Electronic freedom on a microcontroller. No web, no cloud, no browser.

µnleashed is a telnet BBS that runs on a bare ESP32-WROOM-32E and grows into an IoT terminal server through plugins: real hardware you reach from a C64, a PC terminal, or anything that speaks telnet. The C64 connects through TeensyROM.

The name is spelled with a micro sign. Where µ can't be shown (PETSCII, hostnames, file names) it's written `unleashed`.

The core has one dial-in port, 6 caller nodes, a busy line and a hidden sysop node. It also has connect-time terminal detection, user accounts with fill-in forms, guest access, screens, a line editor with history, paged output, a message bus between nodes, the TTY effects library and a shell.

Docs:

| File | For |
|---|---|
| [COMMANDS.md](COMMANDS.md) | every command, key, limit and `system.cfg` setting |
| [USERS.md](USERS.md) | signing up, logging in, guests, managing accounts |
| [BACKUP.md](BACKUP.md) | downloading and uploading config, accounts and screens as a `.zip` |
| [SCREENS.md](SCREENS.md) | screen formats, naming rules and upload limits |

## Status (0.7.0)

- Host build (Linux): the full scripted suite passes, also under AddressSanitizer and UBSan (`tools/testclient.py --backup`, plus `--slow` and `--ban`).
- ESP32 build: ESP-IDF 5.3.1 through PlatformIO (`espressif32@6.9.0`), with no warnings in app code.
  - Image: about 920 KB, 59% of the 1.5 MB OTA slot.
  - Static RAM: 104 KB, including the 8-session pool (5.7 KB per session) and the backup buffers.
- User accounts (0.6.0): self-registration through a form, salted SHA-256 passwords, per-handle lockout, PROFILE / PASSWORD / INFO, and a staff user manager. Up to 100 accounts on the board; more will need the SD card plugin.
- 0.7.0 (not yet on hardware): `GUEST` logins (15 minutes, nothing saved), input effects in place (errors and passwords resolve on the same line), page and broadcast alerts, title bars on lists, a staff Doing column in WHO and DASH, Wi-Fi signal on DASH.
- Commands come from a registry (`Command` tables); HELP, dispatch and permissions are generated from it, and plugins will register into it.
- On hardware:
  - PuTTY and a C64 through TeensyROM have both called in.
  - Every PETSCII glyph (spinner, lines, shade, underscore) is verified on the C64.
  - NTP and mDNS come up on boot; the backup window works on the board.

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

## Next

- C5: plugin API. The first plugins will be GPIO and chat.
- SD card plugin: more than 100 accounts, logs on the card.
