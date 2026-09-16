# ESP32 IoT BBS

A telnet BBS on a bare ESP32-WROOM-32E that grows into an IoT terminal server through plugins. The C64 connects through TeensyROM.

The core has one dial-in port, 6 caller nodes, a busy line and a hidden sysop node. It also has connect-time terminal detection, screens, a line editor with history, paged output, a message bus between nodes, the TTY effects library and a shell.

Every command and key is documented in [COMMANDS.md](COMMANDS.md).

## Status (0.2.0)

- Host build (Linux): 90/90 scripted checks pass with `tools/testclient.py --slow`, and `--ban` passes too.
- ESP32 build: ESP-IDF 5.3.1 through PlatformIO (`espressif32@6.9.0`), with no warnings in app code.
  - Image: 851 KB, 54% of the 1.5 MB OTA slot.
  - Static RAM: 66 KB (.bss 50 KB, which includes the 8-session pool at 3,568 bytes each).
  - Heap: 243 KB free at boot, before Wi-Fi.
- On hardware:
  - 0.1.0 ran with two PuTTY callers.
  - 0.2.0 boots and loads `system.cfg`, but has not been tested over the network yet.

## Build and flash (PlatformIO)

```bash
cp include/secrets.h.example include/secrets.h   # Wi-Fi SSID (case-sensitive) and password
cp data/system.cfg.example data/system.cfg       # timezone, sysop password, time limits
pio run -t flashall                              # firmware + data/ (screens, system.cfg)
pio device monitor
```

- `pio run -t upload` flashes only the firmware and leaves LittleFS alone.
- `flashall` and `uploadfs` rewrite the whole filesystem, which erases `calls.log`.
- The console prints `online <ip>  dial in: telnet <ip> 6400`.
- On the LAN the board answers as `esp32bbs.local` and advertises `_telnet._tcp`.

LittleFS and mDNS come from the ESP-IDF Component Manager (`src/idf_component.yml`). If the LittleFS fetch fails, vendor it instead:

```bash
git clone --recursive https://github.com/joltwallet/esp_littlefs components/esp_littlefs
```

`src/CMakeLists.txt` detects the vendored copy automatically.

## Test on a PC first

```bash
cd host && make && ./bbs_host ../data 6400
python3 tools/testclient.py 127.0.0.1 6400           # add --slow for the 60 s hangup test
python3 tools/testclient.py 127.0.0.1 6400 --ban     # bans 127.0.0.1 for 15 min
```

The same script runs against a board: `python3 tools/testclient.py <ip> 6400`. Don't use `--ban` there, because it bans your own PC.

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
src/core/guard.*          IP ban list, daily time bank
src/core/calllog.*        caller log ring file (LAST)
src/core/clock.*          wall clock formatting (NTP)
src/core/sysconfig.*      data/system.cfg loader
src/core/bbs.*            listener, sessions, flow, timers, paging
src/core/bbs_shell.cpp    caller commands
src/core/bbs_sysop.cpp    sysop node and commands
data/screens/             welcome, help, busy, goodbye (.seq/.ans/.asc)
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

- C2: users, PBKDF2-SHA256 auth with lockout, sysop bootstrap.
- C5: plugin API. The first plugins will be chat and GPIO.
