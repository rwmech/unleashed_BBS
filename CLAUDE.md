# µnleashed BBS: project context

Read README.md for layout and build, COMMANDS.md for every command and setting, USERS.md for accounts, BACKUP.md for the backup window, SCREENS.md for screen rules. This file is the design history and current state. Keep it current with every build and decision.

## What this is

µnleashed BBS (ASCII: `unleashed`; repo github.com/rwmech/unleashed_BBS, private) is a telnet BBS framework hosted on an ESP32 that grows into an "IoT terminal server". The name is about electronic freedom: real hardware reachable without a web browser. Don't brand it to the ESP32, and remember that UCBBS/µCBBS clashes with the 1990 C64 program Ultra-Com BBS. A C64 dials in through TeensyROM (Swiftlink/Ethernet emulation at $DE00). PC callers use SyncTERM, PuTTY or telnet. The core is minimal; everything else bolts on as plugins.

Prior art check (done): no BBS software runs on an ESP32. ESP32 only shows up client-side (Zimodem, Meatloaf). No native Home Assistant client exists for the C64 (HomeTo64 needs an Ultimate 64 REST API). This is open ground.

## Settled decisions

- PlatformIO on ESP-IDF 5.3.1, pinned `espressif32@6.9.0`. ESPHome dropped. The Espressif VS Code extension is disabled for this workspace (it fights PlatformIO).
- Reference target: bare ESP32-WROOM-32E, Bluetooth off (520 KB SRAM, 4 MB flash, no PSRAM). The core is sized for it; plugins declare their own needs.
- C++ for core and hardware. Lua only for doors later. Static allocation, no heap in the BBS loop (exceptions: temporary inflate buffers during a backup upload).
- 6 caller nodes, a busy line session (7th caller: detection, busy screen, 10 s countdown), a hidden sysop node. Overflow callers get `BUSY` and a drop. Socket budget 16.
- One dial-in port, 6400. Connect-time terminal detection: settle 300 ms (a telnet client that speaks IAC first gets character mode negotiated and an immediate probe; this fixed PuTTY's line-mode "[3;20R" leak), CPR probe with UTF-8 test glyph, else PETSCII/ASCII key prompt, then 40/80 columns for PETSCII.
- Security posture: plaintext telnet accepted (the C64 side cannot encrypt). Staff passwords in `system.cfg`. `BYE <password>` elevates; a wrong one is a plain logoff and counts toward an IP ban (3 in 15 min = 15 min). No TOTP, no email codes.
- Staff: one sysop (hidden node, all permissions), co-sysop 1 and 2 (stay on their node, permissions from the `[access]` matrix in `system.cfg`, `NOLIMITS` is a matrix row). Rank: KICK/SNOOP only on lower levels.
- Everything that matters is in the storage partition and moves as one backup zip: `system.cfg`, `users.txt` and screens. Logs live on a separate `logs` partition, fixed-size rings only, never in the zip.
- Backup window: BOOT button (GPIO0) while the sysop is logged in opens HTTP on `backup_port` for `backup_window_minutes`. Download needs no confirmation and redacts passwords as `***`; upload is staged, validated, and applied only after the sysop's Y. HTTP runs inside the BBS select loop (no httpd task, saves 27 KB flash, host-testable).
- Commands: C3 is a command registry (verbs, shortcuts, permission, help, handler) that plugins register into. The navigation tree is deferred; if it returns it is a device namespace (`ls /gpio`), not a BBS menu tree.
- Plugin order after core: GPIO, chat (DDial/Gtalk style), serial bridge (sysop/LAN only, raw stream handoff), HA. Then mail, message bases, XMODEM, MQTT, Lua doors, federation.
- SD card plugin (planned): replaces the onboard filesystem for all data, takes over file management, and lets logging be redirected to the card. Onboard storage caps user accounts at about 100; more than that requires the SD card plugin. Core file access must stay behind `plat::fsBase()` / `plat::logsBase()` so the plugin can swap the mounts.
- User accounts (0.6.0): form and list screens (cursor-driven on ANSI and PETSCII, line prompts on plain ASCII) instead of typed commands, except `USER DEL`; self-registration on by default (`self_register`); password typed twice; salted SHA-256 x1000 (simple on purpose, not PBKDF2: the file has to be stolen first); 3 wrong per call hang up, 5 per handle in 15 min lock it (RAM); staff elevation stays on `BYE <password>`, separate from accounts, so a guessed account password never grants staff; flashy fx on login, sign-up and forms. `users.txt` is a `[handle]` block file, rewritten via temp file + rename; adding a field is one `UserRec` member plus one `kUserFields` row. USERS.md documents it.
- Guests (0.7.0): `GUEST` at the handle prompt, on by default (`guest`). Named `Guest<node>`, no account, nothing saved (`saveCallStats` skips them), `guest_minutes` (15) per call and no daily limit. The call still goes to the caller log (sysop's security record). `GUEST` and `Guest<digits>` are reserved handles. `CF_ACCOUNT` commands (PROFILE, PASSWORD) are hidden and unknown for guests. Guests may still `BYE <password>` (same exposure as self-registration; IP bans apply).
- Input effects (0.7.0): the handle, password and command editors use `F_STAY` (Enter does not move to a new line). A rejected handle or unknown command rubs out, flashes the reason in place (`inputError`, short text on 40 columns) and re-arms on the same line; known commands print their own newline first. Passwords: spinner, rub out the stars, `ACCESS GRANTED` in place; denied flashes and clears for a retry on the line. Masked input shows at most 24 stars so it never wraps on a C64. Pages and broadcasts: bell, flashing tag, rub out, message. Lists and MEM/TERM/TIME open with `rowTitle` (reverse bar on ANSI/PETSCII, dashed on ASCII) and lists close with a rule.
- Staff Doing column (0.7.0): `Session::doing` holds the verb of the last dispatched command (never arguments, never BYE, never unknown input, so a mistyped password can't show). WHO shows it instead of Terminal for staff with `NODES`; DASH always. Plugins can set it later for doors.
- Input backpressure: a session's socket is only read, and held keys only fed, while its timeline has `BBS_RX_ROOM` (1 KB) free. Form redraws (~1.2 KB on PETSCII) overflowed the old 2 KB timeline when keys were typed ahead; `BBS_TL_BYTES` is now 3 KB.
- Workflow: commit and push after every flashed build. COMMANDS.md, README.md and this file are updated in the same change. Code review at phase checkpoints; a robustness/pen test of the live board before any internet exposure (tabled for now).

## Phase plan

| Phase | Scope | State |
|---|---|---|
| C1 | listener, nodes, detection, screens, editor, effects, shell | done, verified on C64 and PuTTY |
| C2 | user accounts, auth, lockout, self-registration | done in 0.6.0 (host-tested, not yet on hardware): accounts, salted SHA-256, lockout, forms, user manager; plus staff passwords, co-sysop matrix, IP bans |
| C3 | command registry (tree deferred) | done: `Command` tables, generated HELP, `registerCommands()` for plugins |
| C4 | message bus, WHO, PAGE, notices, BROADCAST, DND | done |
| C5 | plugin API, requirements check, diagnostics | not started |
| C6 | network zones, bans, maintenance mode, OTA | partial: bans |

Also done: busy line, paging (`[More]`), abort keys, command history, time limits (per call, per day), caller log (`LAST`), NTP + TZ, mDNS, backup window, config reload without reboot.

## Current state (0.7.0, built, not flashed)

- Host build: 220/220 scripted checks (`tools/testclient.py --backup`), also under ASan/UBSan. Layouts checked through a C64 screen model (in-place errors and passwords stay on one 40-column line, title bars 39 wide).
- ESP32 0.7.0: image 922 KB (58.6% of the slot), static RAM 104 KB; session 5,688 bytes. No app warnings. Rob flashes it.
- 0.7.0 adds: guests, input effects in place, page/broadcast alerts, title bars and closing rules on lists, staff Doing column (WHO, DASH), Wi-Fi RSSI on DASH (`plat::wifiRssi`), `guest` and `guest_minutes` keys.
- Hardware checks for 0.7.0: everything listed for 0.6.0 below, plus on the C64: reverse title bars, rubout of errors and ACCESS GRANTED in place, the PAGE alert, and a real RSSI value on DASH.

## State at 0.6.0 (host-tested, shipped inside 0.7.0)

- Host build: 202/202 scripted checks. Every test login goes through an account (`login()` registers a new handle through the form).
- ESP32 0.6.0: image 919 KB (58.4% of the slot), static RAM 104 KB; session 5,672 bytes, pool 45 KB.
- 0.6.0 adds: accounts (`users.txt`, in the backup zip, validated on upload), sign-up form, `Password:` with fx, `LoginGuard` per-handle lockout (replaced the per-IP time bank; daily minutes now live in the account), `INFO`/`PROFILE`/`PASSWORD`, `USERS` manager and `USER ADD|EDIT|DEL` under the new `USERS` permission (CO1 yes, CO2 no by default), `self_register` and `max_users` keys, input backpressure.
- Hardware checks for 0.6.0 once flashed: sign-up form and user manager on the C64 (PETSCII cursor moves, reverse-video boxes, F1 save, left-arrow cancel), password hashing time on the ESP32 (1000 SHA-256 rounds should be well under 100 ms), heap with 6 callers.

## Previous state (0.5.0)

- Host build: 149/149 scripted checks. The old GCC 9 ASan in WSL sometimes dies at startup with `AddressSanitizer:DEADLYSIGNAL` (ASLR vs new kernel); rerun or use the plain build, it is not a BBS bug.
- ESP32: image ~900 KB (57% of the 1.5 MB slot), static RAM ~81 KB, heap ~186 KB free with the BBS listening (0.3.0 figure).
- 0.5.0 adds: TCP keepalive on caller sockets (60 s idle, 3 x 10 s; lwIP `LWIP_TCP_KEEPALIVE` is on), activity LED (`activity_led_gpio`, default 2), the C3 command registry with generated 40-column HELP, `WHO n` and `DASH [n]` refresh screens (home + padded rows, last row without newline so 24-row terminals never scroll, footer shows the idle clock). The stock help screen is gone.
- Verified on hardware: the keepalive drops the sysop line when the C64 is switched off; the blue LED on GPIO2 blinks with traffic.
- Flash layout: 2 x 1.5 MB OTA app, `logs` 128 KB, `storage` 768 KB.
- Hardware verified: PuTTY, C64 via TeensyROM, every PETSCII glyph (spinner 0xBE/0xBC/0xAC/0xBB, 0xC0, 0xDD, 0xA6, 0xA4), backup window download and upload.

## Next builds (approved)

0.5.0: done and bench-checked (keepalive, LED).

0.6.0: accounts, host-tested, never flashed on its own.

0.7.0: guests plus the queued notes (input fx, password in place, page alerts, list graphics, staff Doing column, DASH RSSI). Built, waiting for Rob to flash.

Queued for the next build: nothing yet.

- Tabled: robustness/pen test script (`tools/robustness.py`, untracked stub).
- Tagline: current screens are fine for now.

## Build gotchas already hit

- On Xtensa, `uint32_t` is `unsigned long`. Cast to `unsigned` for `%u`; `plat::log` has a printf format attribute.
- Partition labels `storage` and `logs`. `littlefs` as a name collides with an IDF 5.3 subtype name.
- PlatformIO `uploadfs` writes to the LAST data partition of subtype spiffs/fat/littlefs, so `storage` must stay last in `partitions.csv`. esp_littlefs mounts by label with any subtype.
- PlatformIO registers `src/` as an extra component, not `main`: IDF deps are listed in `src/CMakeLists.txt` (incl. `esp_driver_gpio`, `esp_rom` for ROM `tinfl_decompress`). LittleFS and mDNS come from `src/idf_component.yml`.
- A Component Manager newer than IDF 5.3 expects fails with `--interface_version` errors. Use the version PlatformIO bundles.
- `platformio.ini` must stay ASCII (µ only in C sources as `\xC2\xB5`).
- glibc defines `P_ALL` (waitid): permission bits are `PERM_*`.
- New source files need a clean build (`pio run -t clean`): the component uses `GLOB_RECURSE`.
- Git on this PC has `core.autocrlf`; `.gitattributes` keeps screens binary and text LF.
- Windows PowerShell 5.1: `curl` is Invoke-WebRequest, use `curl.exe`; no `Set-Content -NoNewline`.
- Wi-Fi: SSIDs are case-sensitive (`HOMENET`). The board scans all channels and joins the strongest AP. Auth timeouts (reason 2/15/39) turned out to be an unplugged AP, not firmware.
- A non-interactive shell cannot push to GitHub the first time: Git Credential Manager needs one interactive browser sign-in (done on this PC).

## Open questions

- Can TeensyROM+ run a generic CRT, Swiftlink and its REU ($DF00) at the same time? Needs a bench test or the TeensyROM Discord.
- Banked carts (EasyFlash, Magic Desk) use $DE00 for bank select and collide with Swiftlink, so banked cart means no network. A generic 8K/16K cart is fine.

## Reference links

- TeensyROM usage: https://github.com/SensoriumEmbedded/TeensyROM/blob/main/docs/General_Usage.md
- HomeTo64: https://github.com/JKnivesworthy/hometo64
- Image BBS source: https://github.com/Pinacolada64/ImageBBS
- Synchronet PETSCII: https://wiki.synchro.net/howto:petscii
- esp_littlefs: https://github.com/joltwallet/esp_littlefs
- ESP-IDF 5.3.1 mbedTLS RAM: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-reference/protocols/mbedtls.html
