# µnleashed BBS: project context

Read README.md for layout, build and screen formats. This file is the design history and current state.

## What this is

µnleashed BBS (ASCII: `unleashed`; repo github.com/rwmech/unleashed_BBS, private) is a telnet BBS framework hosted on an ESP32 that grows into an "IoT terminal server". The name is about electronic freedom: real hardware reachable without a web browser. Don't brand it to the ESP32, and remember that UCBBS/µCBBS clashes with the 1990 C64 program Ultra-Com BBS. A C64 dials in through TeensyROM (Swiftlink/Ethernet emulation at $DE00). PC callers use SyncTERM or telnet. The core is minimal; everything else bolts on as plugins.

Prior art check (done): no BBS software runs on an ESP32. ESP32 only shows up client-side (Zimodem, Meatloaf). No native Home Assistant client exists for the C64 (HomeTo64 needs an Ultimate 64 REST API). This is open ground.

## Settled decisions

- ESPHome dropped. PlatformIO on ESP-IDF. Plain REST + WebSocket covers HA later, including push via `subscribe_events`.
- Reference target: bare ESP32-WROOM-32E, Bluetooth off. That's 520 KB SRAM, 4 MB flash, no PSRAM, and roughly 200 to 260 KB free after Wi-Fi.
  - The core is sized for this module.
  - Each plugin declares its own RAM, flash, PSRAM and SD needs, and the build warns or refuses when the board can't meet them.
- C++ for core and hardware. Lua (an ESP-IDF component) only for doors: no sysop toolchain, crash isolation, coroutines, per-script memory caps. Compute-heavy work (XMODEM, pathfinding) stays in C++.
- 6 caller nodes; the 7th connection gets the busy line (DDial style). The socket budget of 16 (the lwIP ceiling) covers the listener, 6 nodes, the busy line and spares for plugins, OTA and federation.
- One dial-in port: 6400. No port-per-terminal-type. Multiple callers share it.
- Connect-time terminal detection:
  - The server sends `DETECTING TERMINAL`, then `ESC[6n`, then UTF-8 U+2500, then `ESC[6n`, with dots every 500 ms.
  - Two cursor-position replies: a column delta of 1 means UTF-8, 3 means CP437.
  - No reply in 2 s: send a PETSCII clear, then `HIT DEL OR BACKSPACE`. $14 means PETSCII; $08 or $7F means ASCII.
  - PETSCII callers then answer `40 OR 80 COLUMNS (4/8)?`.
  - Telnet negotiation goes only to ANSI callers or clients that spoke IAC first. IAC bytes are junk on a C64, and a C64's pi key is $FF.
- The TTY effects library is core and reusable (`fx::`). Effects stay on one line because PETSCII has no bare CR. Size them with `fitSteps()`.
- HA and MQTT are plugins. HA may need a bigger ESP32 because of HTTPS (about 45 KB) and Lua. Rob's HA login `rwmech` gives a long-lived token; BBS access levels are the real gate.
- Security posture:
  - Network zones: LAN callers get IoT paths; internet callers get BBS features only unless granted.
  - Rate limiting and IP bans from day one.

## Phase plan

- C1, DONE: listener, telnet filter, node pool and busy line, 4 terminal drivers, screen player, line editor, effects library, C1 shell (HELP WHO FX MEM TERM BAUD CLS BYE).
- C2: users, PBKDF2-SHA256 auth with lockout, sysop bootstrap over serial.
- C3: navigation tree (flash-resident node table, per-node access levels, rendered as a menu or a path) plus a command registry.
- C4: message bus, WHO, paging between callers.
- C5: plugin API (init, tick, on_connect, on_disconnect, on_command, on_node, raw stream handoff), requirements check, diagnostics.
- C6: network zones, bans, maintenance mode, OTA.
- Plugins after core, in order: GPIO (first), chat, HA. Then mail, message bases, XMODEM, MQTT, Lua doors, federation.

## C1 verified state

- Host build: 31/31 checks in `tools/testclient.py`. Output rendered through pyte (ANSI) and a C64 screen model, including mid-animation frames, with no residue.
- ESP-IDF 5.3.1 build with idf.py and a vendored LittleFS: zero app warnings.
  - Image 757 KB, 51% of the 1.5 MB OTA slot free.
  - Static DRAM 49.5 KB.
- Session is 2,736 bytes; 6 nodes = 16,416 bytes, static. No heap use in the loop.
- NOT yet verified:
  - The PlatformIO build path (the registry was blocked on the build box).
  - Anything on real hardware.

## Build gotchas already hit

- On Xtensa, `uint32_t` is `unsigned long`. Cast to `unsigned` for `%u`. `plat::log` has a printf format attribute, so the compiler catches these.
- The partition label is `storage`. `littlefs` as a name collides with an IDF 5.3 subtype name.
- PlatformIO registers `src/` as an extra component, not `main`, so IDF deps are listed explicitly in `src/CMakeLists.txt`.
  - LittleFS comes in via `src/idf_component.yml`; the Component Manager injects the requirement.
  - Fallback: vendor `joltwallet/esp_littlefs` into `components/esp_littlefs`. CMake detects it by path.
- A Component Manager newer than IDF 5.3 expects fails with `--interface_version` errors. Use the version PlatformIO bundles.

## Verify on a real C64

PETSCII codes in the mixed-case charset, marked `VERIFY` in source:

- 0xBE 0xBC 0xAC 0xBB: quadrant spinner. There's no backslash in lowercase mode.
- 0xC0: horizontal line.
- 0xDD: vertical line.
- 0xA6: shade.
- 0xA4: underscore substitute.

## Open questions

- Can TeensyROM+ run a generic CRT, Swiftlink and its REU ($DF00) at the same time? Needs a bench test or the TeensyROM Discord.
- Banked carts (EasyFlash, Magic Desk) use $DE00 for bank select and collide with Swiftlink, so banked cart means no network. A generic 8K/16K cart is fine.

## Immediate next steps

- Rob's side:
  - First PlatformIO build, then `pio run -t upload` and `pio run -t uploadfs`.
  - Monitor for the IP.
- Then:
  - Run `python3 tools/testclient.py <board-ip> 6400` against the board.
  - Check `MEM` for real heap numbers with 6 callers.
  - Test from the C64 via TeensyROM and check the VERIFY glyphs.
- Then C2.

## Reference links

- TeensyROM usage: https://github.com/SensoriumEmbedded/TeensyROM/blob/main/docs/General_Usage.md
- HomeTo64: https://github.com/JKnivesworthy/hometo64
- Image BBS source: https://github.com/Pinacolada64/ImageBBS
- Synchronet PETSCII: https://wiki.synchro.net/howto:petscii
- esp_littlefs: https://github.com/joltwallet/esp_littlefs
