# Plan: the Freenove ESP32-WROVER CAM board profile

2026-09-24. A plan for Rob to approve, not a build. Nothing in `src/` was
changed and nothing was flashed. The board on COM13 was read with esptool
(read-only) and its boot log captured once. Every size below was measured
off a real build of this tree (HEAD `9a233d0`, 1.1.0-dev.9) in a scratch
copy outside the repo, which has since been left in the scratchpad and is
not part of the firmware.

Sources are tagged `[X]` and listed at the end. UNCONFIRMED means no primary
source settled it.

## Summary

- The board is Freenove's **ESP32 DEV CAM board (FNK0060 kit), pinout
  revision 3.0**: ESP32-WROVER-E (ESP32-D0WD-V3), 4 MB flash, 8 MB PSRAM,
  OV2640 on a 24-pin ribbon, micro SD in **SDMMC 1-bit mode on 14/15/2**,
  USB-C through a CH340, BOOT and RESET buttons, LEDs on IO2, TX, RX and ON
  [FN-PIN] [FN-SD] [FN-CAM] [FN-REPO] [FN-STORE].
- **Flash fits without any sacrifice.** Measured: the core plus PSRAM plus
  the camera driver (OV2640 only) plus an SDMMC mount is **1,304,768 of
  1,572,864 (83.0%)**. With the forms merge, the plugin and the Photos area
  estimated on top, about **1,327,000 (84.4%), 245 KB free**. Nothing needs
  to be dropped and the partition table stays the shared one.
- **Static DRAM fits**: 163,944 of 180,736 measured (16,792 free) before the
  forms merge and the plugin's ~0.3 KB.
- **Internal heap is the number to watch**, not flash: the ESP32 camera
  driver takes one **32 KB contiguous DMA block** plus a 4 KB task stack
  while it is running. Measured on the bench in phase 1 before phase 4 is
  started.
- **The sd plugin cannot reach this slot as it stands.** It is SDMMC 1-bit,
  whose pins are fixed on the ESP32 and are not the plugin's SPI defaults.
  A board-gated SDMMC path in the platform layer is needed (7.6 KB measured,
  compiled out of the other two boards).
- **Every one of the sd plugin's default SPI pins (5, 18, 19, 23) is a
  camera data line**, and the activity LED's GPIO 2 is the card's data line.

## 1. The board

What was measured on COM13 (esptool v4.11 through PlatformIO,
`flash_id`, read-only):

```
Chip is ESP32-D0WD-V3 (revision v3.1)
Features: WiFi, BT, Dual Core, 240MHz, VRef calibration in efuse
Crystal is 40MHz
Manufacturer: 46  Device: 4016
Detected flash size: 4MB
Flash voltage set by a strapping pin to 3.3V
Hard resetting via RTS pin...
```

- Auto-reset into download mode works: esptool connected with no button
  pressed and reset the board through RTS. UNCONFIRMED whether a card was in
  the slot at the time, which matters (section 2).
- PSRAM cannot be read by esptool; it shows in the IDF boot log of our own
  first build. The stock firmware's boot log is the Arduino bootloader and
  then unreadable output, so it said nothing about PSRAM.
- `espefuse summary` was not run: PlatformIO's copy lacks the `cryptography`
  module and installing it is outside the dev stack.

Identification:

- Freenove's store lists the FNK0060 kit as "ESP32 DEV CAM Board Kit 1GB
  Card ... 4 MB flash, 8 MB PSRAM ... memory card slot", with a 1 GB card
  and reader in the box [FN-STORE]. That is the "with Memory Card Slot and
  Card" kit, and the repository for it is Freenove_ESP32_WROVER_Board, which
  says "Apply to FNK0060" [FN-REPO].
- The repository carries two pinouts, 1.6 and 3.0 [FN-REPO]. The 3.0
  drawing shows a USB-C socket and an SD slot; version 1.x boards have no
  slot [RNT]. So this is the 3.0 board.
- USB-serial: the repository ships only CH340 drivers (`CH340/Windows`,
  `/MAC`, `/Linux`) [FN-REPO]. Which CH340 variant is UNCONFIRMED; the auto
  reset measured above is what matters.
- The module is an ESP32-WROVER-E: its datasheet puts an ESP32-D0WD-V3 and a
  QSPI PSRAM on the module [WROVER-E], which is what esptool read.

### Pins

From Freenove's own sketches and the 3.0 pinout drawing.

| GPIO | Board use | Source |
|---|---|---|
| 0 | BOOT button; strapping | [FN-PIN] |
| 1, 3 | UART0 to the CH340 (console, Improv, flashing) | [FN-PIN] |
| 2 | SD D0 **and** the blue LED "IO2"; strapping | [FN-PIN] [FN-SD] [FN-BLINK] |
| 4 | camera D0 (Y2) | [FN-CAM] |
| 5 | camera D1 (Y3); strapping (SDIO slave timing) | [FN-CAM] [FN-PIN] |
| 6-11 | module flash | [WROVER-E] |
| 12 | free header pin; strapping MTDI (flash voltage) | [FN-PIN] |
| 13 | free header pin | [FN-PIN] |
| 14 | SD CLK | [FN-SD] [FN-PIN] |
| 15 | SD CMD; strapping MTDO | [FN-SD] [FN-PIN] |
| 16, 17 | PSRAM CS and CLK, not broken out | [IDF-KPSRAM] [FN-PIN] |
| 18 | camera D2 (Y4) | [FN-CAM] |
| 19 | camera D3 (Y5) | [FN-CAM] |
| 21 | camera XCLK | [FN-CAM] |
| 22 | camera PCLK | [FN-CAM] |
| 23 | camera HREF | [FN-CAM] |
| 25 | camera VSYNC | [FN-CAM] |
| 26 | camera SIOD (SCCB data) | [FN-CAM] |
| 27 | camera SIOC (SCCB clock) | [FN-CAM] |
| 32, 33 | free header pins | [FN-PIN] |
| 34 | camera D6 (Y8), input only | [FN-CAM] |
| 35 | camera D7 (Y9), input only | [FN-CAM] |
| 36 | camera D4 (Y6), input only | [FN-CAM] |
| 39 | camera D5 (Y7), input only | [FN-CAM] |
| PWDN, RESET | not wired (-1) | [FN-CAM] |

Freenove's camera sketch selects `CAMERA_MODEL_WROVER_KIT` and those are
its pins [FN-CAM-CFG]. Freenove's SD sketch: `SD_MMC_CMD 15`, `SD_MMC_CLK
14`, `SD_MMC_D0 2`, "Please do not modify it", and `SD_MMC.begin(...,
true /* 1-bit */ ...)` [FN-SD].

What is free for a sysop to wire: **13, 32 and 33**, and 12 with a caveat.
Everything else is the camera's, the card's, the flash's, the PSRAM's or
the console's.

### Clashes with today's defaults

| Default | Value | On this board |
|---|---|---|
| sd CS / MOSI / CLK / MISO | 5 / 23 / 18 / 19 | all four are camera lines |
| activity LED | GPIO 2 | the card's D0. Blinking it would drive the data line |
| serial bridge RX / TX | 16 / 17 | the PSRAM's CS and CLK |
| lights pins | -1 | fine; must not be offered 2, 12, 14, 15 or a camera pin |
| BOOT | GPIO 0 | fine: the BOOT button, the BOOT-hold reset works as on the WROOM |

Strapping pins [ESP32-DS] [IDF-SDPU]:

- **GPIO 12** selects the flash voltage at reset. esptool reports "Flash
  voltage set by a strapping pin to 3.3V", so it must be low at reset. That
  is why the slot is 1-bit: SD D2 is GPIO 12 and would need a pull-up, and
  Espressif's own note says a pulled-up GPIO 12 is "incompatible with the SD
  card operation" on a 3.3 V flash board [IDF-SDPU]. (That page's claim
  that WROVER modules use 1.8 V flash is about the original WROVER; this
  one measured 3.3 V.) The profile refuses 12 for anything with a pull-up or
  a device on it at reset: lights and serial.
- **GPIO 2** must be low or floating to enter download mode. With a card in
  the slot, D0's pull-up can hold it high, and "you cannot enter Download
  mode" [IDF-SDPU]. The bench check is simple: flash once with the card in.
  If it fails, the installer page says "take the card out to flash".
- **GPIO 15** (MTDO, SD CMD) only silences the ROM boot log when low; its
  pull-up leaves the log on. Harmless.
- **GPIO 5** (camera D1) sets SDIO slave timing, unused here, and the
  sensor does not drive it until XCLK runs.
- **GPIO 0** is the BOOT button. Unchanged.

## 2. SD on this board

**Answer: SDMMC 1-bit. The sd plugin cannot reach this slot as it
stands.**

- The plugin speaks SPI only (`esp_vfs_fat_sdspi_mount`,
  `src/platform/platform_esp32.cpp:328`). SPI needs the card's D3 as chip
  select. On this board D3 is not on any documented GPIO: Freenove labels
  only CLK, CMD and D0 as "SD MMC Pin in 1Bit mode" [FN-PIN] and its sketch
  uses only those three [FN-SD].
- If D3 happened to reach GPIO 13, SPI on CS 13 / MOSI 15 / CLK 14 / MISO 2
  would work with no code at all. A one-minute continuity check (card
  socket pin 2 to the header's IO13) settles it, and I would not plan on it:
  Freenove wrote "Please do not modify it" beside 1-bit SDMMC.
- On the ESP32, SDMMC slot 1 is on the IO MUX and its pins are fixed:
  CLK 14, CMD 15, D0 2, D1 4, D2 12, D3 13 [IDF-SDMMC]. In 1-bit mode only
  CLK, CMD and D0 are driven, which leaves the camera's GPIO 4 and the flash
  strap on 12 alone.

What the change costs the shared plugin:

- `platform_esp32.cpp`: a second mount path under a board capability
  (`BBS_SD_SDMMC1`): `SDMMC_HOST_DEFAULT()`, `SDMMC_SLOT_CONFIG_DEFAULT()`
  with `width = 1`, `esp_vfs_fat_sdmmc_mount`. Unmount, `sdInfo` and
  everything above the mount are unchanged, because both paths end in the
  same FAT VFS at `/sd`. About 40 lines.
- `src/CMakeLists.txt`: `esp_driver_sdmmc` added to the requirements only
  when the profile asks for it (the file already branches per target).
- **Measured: `esp_driver_sdmmc` is 7,643 bytes of flash** and 132 bytes of
  static DRAM (64 data, 68 bss). The SPI driver (`esp_driver_sdspi`, 4,589
  bytes) could be compiled out of this board; not worth doing unless flash
  gets tight.
- `sd.cpp`: on an SDMMC board the four pin rows mean nothing (the pins are
  fixed), so under the capability the settings table shows one `PS_INFO`
  row, "Card slot: SDMMC 1-bit, CLK 14 CMD 15 D0 2", and keeps `speed`
  (20,000 default; 40,000 is SDMMC high speed). Old `cs`/`mosi` lines in a
  restored system.cfg are ignored with a console note, not refused.
- **The WROOM and S3 images do not change**: all of it is behind the
  capability, the way the panel is. The S3 could adopt it later (its slot is
  wired 4-bit), as its own measured step.
- Host build: unaffected. The host card is a directory.

Why not the alternative of SPI on free pins: there is no way to move the
slot's wiring, and an external SPI breakout on 13/32/33 would mean a sysop
wiring a second card slot to a board that has one.

## 3. PSRAM on the classic ESP32, IDF 5.3.1

The first classic-ESP32 profile with PSRAM. The sdkconfig layer:

```
CONFIG_SPIRAM=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MEMTEST=n
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
CONFIG_SPIRAM_IGNORE_NOTFOUND=y
CONFIG_ESP32_REV_MIN_3=y
```

- **`ESP32_REV_MIN_3` is worth 33 KB of flash.** Below revision 3 the IDF
  compiles the whole image with `-mfix-esp32-psram-cache-issue`
  (`SPIRAM_CACHE_WORKAROUND`, "depends on ... ESP32_REV_MIN_FULL < 300")
  [IDF-EXTRAM]. Measured: PSRAM with the workaround 1,295,792 bytes and
  163,504 static; with `REV_MIN_3` 1,262,608 and 162,176. The chip is
  v3.1. The price: the image refuses to boot on a pre-v3 ESP32, which no
  WROVER-E is.
- **PSRAM itself costs 7,072 bytes of flash and 216 of static DRAM**
  (measured, 1,255,536 to 1,262,608).
- Speed: 40 MHz, the default. 80 MHz needs the flash at 80 MHz too
  [IDF-KPSRAM]; not worth a risk nobody needs.
- **Only 4 MB of the 8 MB is usable by malloc.** "ESP32 has the ability to
  use up to 4 MB of virtual addresses for external PSRAM" [IDF-EXTRAM]; the
  upper 4 MB needs the himem bank-switching API. 4 MB is far more than this
  firmware can use.
- `MEMTEST=n` for the same reason as the S3: the Improv deadline after the
  installer's reset (sdkconfig.defaults.esp32s3).
- `IGNORE_NOTFOUND=y` (my addition): if somebody puts this image on a board
  without PSRAM, it boots as a plain BBS rather than panicking in a loop,
  and the camera plugin refuses to start with "no PSRAM". It is only
  available while `SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` is off
  (Kconfig.spiram.common).
- What moves into PSRAM: Wi-Fi's and lwIP's buffers (`TRY_ALLOCATE_WIFI_LWIP`,
  as on the S3), the backup inflater and chat's room buffer (both above
  `SPIRAM_MALLOC_ALWAYSINTERNAL`, 16 KB), and the camera's frame buffer
  (`CAMERA_FB_IN_PSRAM`). Not static data: `ALLOW_BSS_SEG_EXTERNAL_MEMORY`
  would move lwIP's and the Wi-Fi libraries' zeroed statics (about 6.7 KB
  in this build's map) but only with the attribute on our own, and it rules
  out `IGNORE_NOTFOUND`. Not proposed.
- Same restrictions as the S3: PSRAM is unreachable while the flash cache
  is off, which is every LittleFS write [IDF-EXTRAM].
- `LWIP_MAX_SOCKETS` stays 16, so ten caller lines, as everywhere.

**How the per-board sdkconfig layer works, verified.** An
`sdkconfig.defaults.esp32` would apply to every ESP32 build, the WROOM
included, so it cannot be used. IDF 5.3.1's `project.cmake` takes a
`SDKCONFIG_DEFAULTS` CMake variable and it overrides the default list
(tools/cmake/project.cmake:638), and PlatformIO 6.9.0 passes
`board_build.cmake_extra_args` straight to CMake
(builder/frameworks/espidf.py:1516). The scratch builds used

```
board_build.cmake_extra_args = -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.fncam"
```

and the generated `sdkconfig.<env>` carried `CONFIG_SPIRAM=y` and
`CONFIG_ESP32_REV_MIN_3=y`. This settles the UNCONFIRMED line in the S3
report. Two traps: PlatformIO only watches `sdkconfig.defaults` for
changes, so delete `sdkconfig.freenove_wrover_cam*` after editing the
board's file; and editing `platformio.ini` makes PlatformIO wipe every
env's build directory, which cost one measurement here.

## 4. The camera driver: espressif/esp32-camera

- **Version 2.1.7** (2026-06-05), IDF ">=5.1", depends on
  `espressif/esp_jpeg ^1.3.1` [CAM-REG]. Supports the ESP32, S2 and S3
  [CAM-README]. It built cleanly against IDF 5.3.1 here.
- **Licence: Apache-2.0** [CAM-REG] (the LICENSE file in the fetched
  component). Apache-2.0 is compatible with GPLv3 and "The FSF has never
  considered the Apache License to be compatible with GPL version 2"
  [ASF-GPL]. **The firmware already links Apache-2.0 code**: ESP-IDF, the
  Wi-Fi libraries and espressif/mdns, all in THIRD_PARTY_NOTICES.md. So
  esp32-camera adds a row to that file, not a new kind of question. It
  does sit awkwardly beside the 0.22.1 decision to write our own Improv
  rather than take the Apache-2.0 SDK "against GPLv2", which is the one
  place the project has treated Apache-2.0 as a reason not to use
  something. Rob's call; section 9.
- **SCCB on IDF 5.3 uses the legacy I2C driver automatically**: the
  component's CMake picks `sccb-ng.c` only on IDF 5.4 or later
  (CMakeLists.txt:90). We use no I2C, so no clash today. A future plugin
  on the new `i2c_master` driver would clash with it at boot on 5.3.
- **Flash, measured**: every sensor driver is compiled in by default (17 of
  them), and the camera costs **82,800 bytes** that way (1,262,608 to
  1,345,408). With every sensor but the OV2640 switched off in Kconfig, the
  camera component is **16,767 bytes** by the map, and camera plus the
  LEDC driver (XCLK, 5,578), the legacy I2C driver (7,513) and the SDMMC
  driver (7,643) is **42,160 bytes** (1,262,608 to 1,304,768). No, it
  pulls in nothing big.
- **Static DRAM, measured, and this is the one to get right**: with all
  sensors, **9,800 bytes** of static DRAM (their register tables are
  initialised data), leaving 8,760 free. With the OV2640 alone, **1,768
  bytes** including the SDMMC driver. The Kconfig pruning is not optional.
- **OV3660 as insurance**: Freenove's repository carries OV3660 datasheets
  beside the OV2640's [FN-REPO], and some ESP32 camera kits ship one. Its
  driver is 6,601 bytes by the map (plus some merged strings) and 991 bytes
  of static DRAM. Proposed: OV2640 only, and add OV3660 if the phase 4
  bench log shows a different sensor ID.
- **Heap while the camera is running** (read from the component source,
  measured on the bench in phase 4):
  - JPEG DMA on the ESP32 is fixed at 8 half-buffers of 4 KB, **one
    32,768-byte `MALLOC_CAP_DMA` allocation** (`target/esp32/ll_cam.c:482`,
    `cam_hal.c:522`). It must be internal and contiguous, and the
    `CAMERA_DMA_BUFFER_SIZE_MAX` Kconfig does not apply to the ESP32's JPEG
    path.
  - `cam_task`, 4,096-byte stack, priority `configMAX_PRIORITIES - 2`,
    pinned to core 0 by default (`cam_hal.c:620`, Kconfig).
  - The frame buffer in PSRAM: width x height x 2 / 5 plus one half
    buffer. SVGA about 196 KB, UXGA about 772 KB. Four MB holds that.
  - So about 37 KB of internal heap while initialised, returned on
    `esp_camera_deinit`.
- The sensor gives JPEG itself; no encoder is linked.
- One interaction worth knowing: a LittleFS write turns the flash cache
  off on both cores, which stalls `cam_task` while the DMA keeps filling.
  A frame caught in that can come out truncated. The plugin checks each
  JPEG for its FF D8 start and FF D9 end and takes one more frame if
  either is missing.

## 5. Flash and RAM budget

All flash figures are `firmware.bin` against the 1,572,864-byte slot;
static DRAM is `_bss_end - 0x3FFB0000` against 180,736, read off each ELF.

| Build | Flash | Slot free | Static DRAM | DRAM free | How |
|---|---|---|---|---|---|
| ESP32 today (HEAD, dev.9) | 1,255,536 | 317,328 | 161,960 | 18,776 | measured |
| + PSRAM, rev 3 minimum | 1,262,608 | 310,256 | 162,176 | 18,560 | measured |
| + PSRAM, rev 1 workaround | 1,295,792 | 277,072 | 163,504 | 17,232 | measured, rejected |
| + camera, all 17 sensors | 1,345,408 | 227,456 | 171,976 | 8,760 | measured, rejected |
| + camera OV2640 only + SDMMC | 1,304,768 | 268,096 | 163,944 | 16,792 | measured |
| + forms merge | +12,500 | | ? | | coordinator's figure; DRAM to measure |
| + camera plugin | +8,000 | | +300 | | estimate: lights is 9.5 KB, announce 10.4 KB |
| + Photos area, pin rules, sd glue, profile | +2,000 | | +120 | | estimate |
| **Profile at phase 5** | **~1,327,000 (84.4%)** | **~245,000** | **~164,400** | **~16,300** | |
| + OV3660 driver, if needed | +7,500 | | +991 | | map, strings estimated |
| + text-art preview (stretch) | +1,500 decoder, ~3,000 renderer | | 0 | | decoder measured |

- The margin is about 245 KB of flash and 16 KB of static DRAM. **Nothing
  needs Rob to choose between options**, so this plan does not stop here.
- Baseline check: the brief's 1,255,204 for the ESP32 image was the dev.10
  working copy; HEAD built here at 1,255,536.
- The preview decoder is measured at 1,477 bytes (`esp_jpeg`, which uses the
  ROM's TJpgDec). **Its convenience wrapper is not**: `jpg2rgb565` in the
  component's `to_bmp.c` carries a 3,100-byte static work buffer, which
  the preview build showed as +3,104 static DRAM. The plugin calls
  `esp_jpeg_decode` directly with a work buffer from PSRAM, so the preview
  costs no static DRAM. (A static 9.6 KB RGB buffer in the first preview
  probe overflowed DRAM by 3,936 bytes: buffers go on the heap, in PSRAM.)
- Internal heap is the real constraint and the one number not yet
  measured. Phase 1 reads MEM and SYS on the bench (free, lowest, largest
  block). Phase 4 needs a **32 KB largest free DMA block** at the moment of
  a snap, with the plugin's reserve on top.

### If flash ever does get tight: the options, measured where possible

Not needed for this plan. Here so the choice is ready if the core grows.

| Option | Frees | Costs |
|---|---|---|
| `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT` | **53,696** flash, 1,440 DRAM (measured) | a failed assert aborts with no file, line or expression: the backtrace still works, the message does not |
| `CONFIG_ESP_ERR_TO_NAME_LOOKUP=n` | **7,504** (measured) | every `esp_err_to_name` in our logs prints a number instead of `ESP_ERR_NO_MEM`; the sd mount errors are read by sysops |
| Drop plugins from this board's build | example ~1.8 KB, serial bridge ~3.1 KB, lights ~9.5 KB + RMT driver 9.6 KB, announce ~10.4 KB (map, strings not counted) | a profile that differs from the others in what it offers; the example plugin is the only free one |
| Compile out the SPI card driver on this board | 4,589 | nothing, while the board is SDMMC only |
| `-O2` instead of `-Os` | nothing | `-Os` is already on (`COMPILER_OPTIMIZATION_SIZE`); `-O2` would grow the image |
| Partition: both app slots +128 KB, from `userdata` (608 to 352 KB) | 131,072 per slot | one erase for this board only (free while it is new); userdata holds about 780 accounts instead of 1,380; a second partition table to keep in release.py's `EXPECT` and the BOOT-hold erase |
| Partition: one app slot, no OTA pair | about 1.5 MB | the 1.1.0 network update is impossible (it writes the other slot while this one runs), and so is rollback: a bad image means USB. Updates stay USB plus /install |

The first row is the big lever for the whole project, not just this board,
and deserves its own decision later.

## 6. The camera plugin (proposal)

Board-gated: `BBS_HAS_CAMERA` in the profile, registered in
`registry.cpp` under it the way the panel is, so no other image carries a
byte of it. **PF_SD**: no card, no CAMERA command (Rob). Not `PF_ON`: off
until the sysop enables it. Declares `heapBytes` of 40 KB so a board that
cannot afford it refuses at start with the reason.

### Commands

- `CAMERA` (alias `CAM`): status. Enabled, resolution, photos stored and the
  limit, the last snap and who took it, and your own wait before the next.
- `CAMERA SNAP` (shortcut `SNAP`): take a photo into Photos. Answers with
  the file name and its number in the Photos area, so the caller can go and
  download it.
- `CAMERA SET <key> <value>` for the admin level: the same settings as
  CONFIG camera, for a sysop testing framing from a terminal without
  walking the form each time.
- Command flag `CF_READ`, and the snap checks the snap level itself, the
  shape `files.cpp` settled for UPLOAD: the flag answers "may you use the
  camera at all", the setting answers "may you take a photo". Tagging SNAP
  with a level flag would have the same trap UPLOAD had.

### Who may snap

- **Snap level: its own setting**, the ladder all | users | staff | co2 |
  co1 | sysop, down to guests if the sysop says so (Rob).
- **Proposed default: `staff`.** Enabling a camera and deciding who may
  point it at the sysop's room are two decisions, and the second should be
  made on purpose rather than inherited from the first. A sysop who wants
  callers snapping the fish tank lowers it to `users` or `all` in one
  keypress on the same page. Rob's call (section 9).
- **Photos level**: its own setting, the same ladder, default `users`. It
  feeds the Photos area's read and download levels.
- The plugin's own admin level (default `sysop`) deletes photos and edits
  settings.

### CONFIG camera

Four core rows (enabled and the three plugin levels), then:

| Key | Label (80 / 40) | Kind | Range | Default |
|---|---|---|---|---|
| snap | Who may take a photo / Snap | PS_CYCLE | the ladder | staff |
| photos | Who may see photos / Photos | PS_CYCLE | the ladder | users |
| max_photos | Photos kept / Keep | PS_NUM | 1-500 | 50 |
| size | Resolution / Size | PS_CYCLE | qvga, vga, svga, xga, sxga, uxga | svga |
| quality | JPEG quality (lower is better) / Quality | PS_NUM | 4-40 | 12 |
| flip | Upside down / Flip | PS_YESNO | | no |
| mirror | Mirror / Mirror | PS_YESNO | | no |
| gap | Seconds between a caller's photos / Gap | PS_NUM | 10-3600 | 60 |
| per_hour | Photos an hour, board-wide / Per hour | PS_NUM | 1-120 | 20 |
| picture | Picture settings / Picture | PS_PAGE | | |

The Picture page (PS_PAGE, which lights already uses for led1-led10):
brightness, contrast and saturation (-2 to 2), exposure level (-2 to 2),
white balance (auto, sunny, cloudy, office, home) and effect (none,
negative, greyscale, red, green, blue, sepia). Every one is a real
`sensor_t` call on the OV2640 in esp32-camera (`set_brightness`,
`set_contrast`, `set_saturation`, `set_ae_level`, `set_wb_mode`,
`set_special_effect`); the scratch build linked the first three and the
flips. Settings the OV2640 has but a sysop would never want (gain ceiling,
lens correction, raw gamma, the colour bar test pattern) are left out.

That is fourteen fields on the main page, inside the form's sixteen. The
labels follow the 40/80 rule: nine characters at 40 columns, the longer
wording at 80.

JPEG quality in esp32-camera is 0 to 63 with lower meaning better; the
range is cut to 4-40 because below 4 the OV2640 overflows its own buffer
at high resolutions and above 40 is mush. UNCONFIRMED where exactly the
low end fails; the bench picks the floor.

### A snap, step by step

The camera blocks: sensor init over SCCB, then frames at 5 to 30 a second.
The BBS loop is cooperative, so none of that runs on it.

- The command checks, in order: enabled and a card mounted; the caller's
  level; the per-caller gap; the board-wide hour; the clock is set (the
  name is the date, and "no valid clock fails closed" as staff memory
  does); the card has the frame plus 64 KB free; nobody else is snapping
  (a `claims::Res::Camera` lock, which `openSession` releases like every
  other claim); and a 32 KB DMA block is free. Each refusal says which.
- A short-lived worker task (4 KB stack, internal, core 0 like the
  driver's own) runs `esp_camera_init`, applies the settings, throws away
  the first frames while exposure settles (three by default, UNCONFIRMED
  how many the OV2640 needs), takes one, validates the JPEG, and hands the
  frame buffer to the loop.
- The caller sees an `fx::` spinner and "Smile...". Their line is otherwise
  unaffected, and so is everybody else's.
- The loop writes the frame to the card in slices from `tick()`, a few KB a
  pass, the way a transfer is pumped, so no single pass stalls on a 100 KB
  write. Then it returns the buffer and calls `esp_camera_deinit`, which
  gives the 37 KB of internal heap back.
- The camera is initialised per snap and never left running. It costs a
  second or so of latency and buys 37 KB of internal heap the other 99.9%
  of the time, on a board where internal heap is the tight resource.

### Names, the write, and removing old photos

- Name: `YYYYMMDD-HHMMSS.jpg` in local time, 19 characters (long names are
  on: `CONFIG_FATFS_LFN_HEAP`). Sorting the names sorts the photos.
- Write under `.snap.tmp` in the Photos folder. The leading dot keeps it
  out of the listing, which already skips dot entries. `fwrite`, `fflush`,
  `fsync`, `fclose`, then `rename` to the final name.
- FatFs `f_rename` refuses an existing destination, and we never remove
  anything to make room for a rename (CLAUDE.md, 1.1.0-dev.6). Two snaps in
  one second are impossible with the lock and the gap; a name that exists
  anyway is refused, never overwritten.
- Then prune: list the folder, keep only names that match
  `^[0-9]{8}-[0-9]{6}\.jpg$` exactly, and remove the oldest by name until
  at most `max_photos` remain.
  - **By name, not mtime.** The names are ours and exact; a FAT mtime has
    2 s resolution, comes from a clock that may have been wrong, and is
    rewritten by whoever copies the card on a laptop.
  - **Never outside Photos, never anything else in it.** Every path is the
    Photos folder plus a name read from that folder that matched the
    pattern, which cannot contain `/` or `..`. A sysop's own `garden.jpg`
    in the folder is neither counted nor removed.
  - A photo's line in FILES.BBS goes with it, through the file-area helper
    that already rewrites FILES.BBS by temp file and rename.
- Power cut: before the rename, a `.snap.tmp` is left and the photo is
  lost; at plugin start the exact name `.snap.tmp` is removed. After the
  rename and before the prune, the folder is one over its limit until the
  next snap or start, which prunes. At no point is a finished photo at risk
  from an unfinished one.
- Accountability: each photo gets a FILES.BBS description, "Taken by
  <handle>" (guests marked `*` as everywhere), so the listing says who took
  it. The console and the card's log get a line per snap.

### Rate limit

- **Per caller: `gap` seconds between photos** (default 60), kept per node
  in a static table of twelve `uint32_t` (48 bytes, not in the Session,
  where it would cost twelve times as much again). Staff holding the
  `NOLIMITS` row of the access matrix are exempt, as they are from the
  other limits.
- **Board-wide: `per_hour`** (default 20), a rolling count. This is the
  real bound: a guest can hang up and dial again for a fresh node, and the
  board cap is what stops that churning the card.
- Worst case at the defaults: 20 photos an hour at SVGA, about 50 KB each,
  1 MB an hour of card writes. Negligible wear, and `max_photos` keeps the
  folder to 50.

### Privacy defaults

- Off until enabled; snap at `staff`; photos seen by `users` (accounts, not
  guests).
- **Settled by Rob's photo (2026-09-24): the Freenove has NO NeoPixel.** Its LEDs are IO2 (the card's D0), RX, TX and ON. The pixel Rob meant is on an ESP32-S3 DevKitC-style board (N16R8, an "RGB" LED, two USB-C) that has no profile yet. The paragraph below is superseded.
- ~~Correction from Rob (2026-09-24): the board has an embedded NeoPixel~~
  (WS2812-class), which this plan missed. It is the default drive light
  (lights plugin on, drive pin = its GPIO, sysop may change it) and the
  snap indicator (white for the length of a snap). Its GPIO is not yet
  known: phase 1 finds it from Freenove's FNK0060 schematic or tutorial
  code, cited, and if neither documents it, a bench test of the free
  candidates (13, 32, 33) with Rob watching. Everything below about "no
  indicator" is superseded once the pin is known.
- ~~No indicator LED is possible on the board as built~~: the only user LED
  is on GPIO 2, which is the card's data line. The OV2640 module has no LED
  and PWDN is not wired, so the sensor cannot be powered off by the
  firmware either; `esp_camera_deinit` stops XCLK and the sensor idles.
  Offered instead:
  - if the lights plugin has a drive light wired (GPIO 13, 32 or 33), it
    flashes white for the length of a snap;
  - every staff member online gets a notice line, "Node 3 took a photo";
  - the photo is labelled with who took it (above).
- The login banner gains one line on a board with the camera enabled and
  snap below staff: "This board has a camera callers can use." Wording is
  `explain`'s.
- A physical note for the site's board page: a lens cap is the only real
  guarantee, and it costs nothing.

## 7. The Photos area

- Built in by `files.cpp` the way Screens (9), Logs (10) and Backups (11)
  are, at **12**, fixed, reached by `#12` Enter at the area menu (the `#`
  entry dev.9 added for areas past 10). Only when `BBS_HAS_CAMERA` is
  defined and the camera plugin is enabled; `kMaxAreas` grows by one only
  in that build.
- Folder `photos` on the card, made at start like every other area.
- Levels: read and download from the camera's `photos` setting; upload
  sysop (nothing belongs there but photos, and a sysop may want to put one
  in); delete at the camera's admin level. `files.cpp` asks the camera
  plugin for them through a small header, as it asks `cardbak` for the
  backup folder.
- The listing shows the whole 19-character name, as Backups does, instead
  of the 12 columns other areas get (`files.cpp:998`). At 40 columns the
  name plus size fits; the description column takes what is left.
- Downloads by YMODEM or XMODEM like any file. A JPEG means nothing on a
  C64, so the area's banner line says "JPEG photos, for a PC terminal".
- Deleting by `E` in the area works as it does everywhere, and the
  camera's count follows it (recounted on the next snap).

## 8. Board profile

- Environments: `freenove_wrover_cam` and `freenove_wrover_cam_release`,
  `board = esp32dev` (there is no Freenove board definition in
  espressif32 6.9.0), `board_upload.flash_size = 4MB`, the shared
  `partitions.csv`, and the sdkconfig layer by `cmake_extra_args` (section
  3). Plus a bench-only `freenove_wrover_cam_backuptest` if wanted.
- `src/board.h`, `BBS_BOARD_FN_WROVER_CAM`:
  - `BBS_BOARD_NAME "Freenove ESP32-WROVER CAM"`
  - `BBS_BOARD_TAG "FNCAM"`, `BBS_BOARD_VERSION "1.0.0"`, shown as
    `1.1.0 (FNCAM 1.0.0)`
  - `BBS_HAS_CAMERA`, `BBS_HAS_PSRAM`, `BBS_SD_SDMMC1`,
    `BBS_BOARD_PLUGINS 1`
  - `BBS_LED_GPIO -1` (GPIO 2 is the card)
  - `BBS_SD_*` pins: unused under SDMMC; left at -1 so nothing drives the
    camera's lines
  - `BBS_SERIAL_RX 33`, `BBS_SERIAL_TX 32` (off until enabled)
  - lights: on, drive pin = the onboard NeoPixel's GPIO (Rob: the default
    drive light, "although someone will likely change it"); pin to be
    confirmed in phase 1
  - `BBS_HEAP_RESERVE`: set from the phase 1 measurement, not guessed.
    The S3 went from 40 KB to 16 KB after its first flash refused plugins
    with 35 KB free.
- `syscfg::pinProblem` under the profile, on top of 6-11:
  - refuse, hard: 16 and 17 ("the PSRAM"), 1 and 3 ("the console and
    Improv");
  - refuse, board-owned: 2, 14, 15 ("the card slot"); 4, 5, 18, 19, 21, 22,
    23, 25, 26, 27, 34, 35, 36, 39 ("the camera");
  - refuse 12 ("sets the flash voltage at reset").
  - That leaves 13, 32 and 33, which is the truth about this board.
- Partition table: the shared 4 MB one, unchanged. The measured image fits
  with 245 KB spare.
- Release: `tools/release.py` gains a third `BUILDS` row,
  `{"dir": "esp32-fncam", "env": "freenove_wrover_cam_release", "family":
  "ESP32", "boot": 0x1000, "board": "BBS_BOARD_FN_WROVER_CAM"}`, with its
  own `manifest.json` and version.txt, as the S3 has.
  - **Two image sets now share the chipFamily "ESP32"**, so ESP Web Tools
    cannot choose between them by reading the chip. The site's picker must
    ask which board, and the directory's `FLASH_FAMILIES` gains
    `esp32-fncam`. A web agent job, after the firmware exists.
  - Wrong image either way is survivable: the WROOM image on this board
    blinks GPIO 2 into the card and finds no card on its SPI pins; this
    image on a WROOM boots without PSRAM (`IGNORE_NOTFOUND`) and the camera
    refuses to start.
- The picker picture: the site's own line art of the board, in the style of
  the others, not Freenove's photograph.
- Tested-boards entry and a "Board features" section on /hardware (camera,
  card, the three free pins, the lens-cap note), once a build has run on it
  with a flashable image for the current version.
- Docs: ESP32_BOARD_CHOICE.md (a classic ESP32 with PSRAM meets the
  two-cores-and-radio rule), README and CLIENTS unchanged, COMMANDS.md for
  CAMERA, a CAMERA section in PLUGINS.md, CHANGELOG, THIRD_PARTY_NOTICES.md
  (esp32-camera and esp_jpeg), CLAUDE.md.
- Flashing: Rob acknowledges each flash, as with the S3 on COM12. COM13 is
  not yet on the "may flash" list.

## 9. Phases

Each phase ends with the targeted regression groups for what it touched,
with and without a card, and a static DRAM figure read off the ELF.

### Phase 1: the profile boots with the core

- board.h profile, the two envs, `sdkconfig.defaults.fncam`, pin rules,
  version tag. No camera, no SD code change.
- Budget: 1,262,608 flash measured (+7,072 over the ESP32 image), 162,176
  static DRAM.
- Host: the profile's pin rules (a host build of the profile, as the S3
  has), and the version string.
- Bench (Rob): flash with the card out. The boot log's `esp_psram` lines
  report 8 MB found and about 4 MB added to the heap; Improv provisions from
  /install; telnet in; **SYS and MEM: internal free, lowest and largest
  block, with nobody on and with three callers**; BOOT-hold stages on the
  console (no LED to watch: say so on the page).

### Phase 2: SD over SDMMC 1-bit

- The platform's SDMMC path and the sd plugin's settings under
  `BBS_SD_SDMMC1`.
- Budget: +7,643 flash, +132 static DRAM (measured).
- Host: unchanged paths; the settings table on an SDMMC build (host build
  of the profile).
- Bench: `SD MOUNT` with Freenove's 1 GB card; SCREENS, FILES, a YMODEM
  download and upload; **flash once with the card in** to see whether
  GPIO 2 blocks download mode; remount after a pull.

### Phase 3: PSRAM put to work

- Nothing to write if phase 1 showed Wi-Fi and lwIP in PSRAM; this phase
  is measuring, and setting `BBS_HEAP_RESERVE` from it.
- Bench: MEM before and after ten callers and a backup restore (the
  inflater now in PSRAM). The internal largest block after a busy hour is
  the number phase 4 is waiting for.

### Phase 4: the camera plugin

- esp32-camera 2.1.7 pinned, OV2640 only, the plugin, claims entry, the
  worker task, CONFIG camera, the notices.
- Budget: camera driver, LEDC and legacy I2C about 30 KB (from the 42,160
  measured with SDMMC), plugin about 8 KB; static DRAM about +1.9 KB.
- Host: a stub camera in the host build that "takes" a fixed small JPEG,
  so the command checks, the rate limit, the naming, the temp-and-rename,
  and the prune are all tested with no board. Including: a folder with a
  sysop's own file in it (never removed), a leftover `.snap.tmp` at start
  (removed), a name that already exists (refused), the prune at exactly
  the limit, and the clock not set.
- Bench: first `CAMERA SNAP` with Rob watching; the sensor ID in the log;
  the internal heap before, during and after; a snap with a caller
  downloading at the same time; a snap during a CONFIG save (a LittleFS
  write); each resolution, to find the quality floor.

### Phase 5: the Photos area

- `files.cpp` area 12, name width, levels from the camera.
- Budget: under 1 KB flash, one `Area` of static DRAM.
- Host: levels, download by YMODEM, deletion by staff, a guest refused at
  the default.
- Bench: take three, list, download one to SyncTERM and open it.

### Phase 6: stretches, each its own go

- **Text-art preview, later.** `CAMERA VIEW [n]` draws the latest photo,
  or photo n, in the caller's terminal. The decode is `esp_jpeg` at 1/8
  scale into PSRAM (SVGA becomes 100 x 75), then:
  - ANSI at 80 columns: upper half blocks (CP437 0xDF) with foreground and
    background, 80 x 48 pixels on 24 rows, nearest of the 16 colours;
  - PETSCII at 40 columns: the quarter-block characters in the C64's 16
    colours, 80 x 50 pixels on 25 rows;
  - plain ASCII: a ten-step shade ramp, 80 x 24.
  Drawn a row at a time as the timeline drains, the way DASH is, because a
  full ANSI frame is well past `BBS_TL_BYTES`. Budget: 1,477 decoder
  (measured) plus about 3 KB of renderer; no static DRAM. The 40/80 rule
  applies: two layouts, specified by `tty-ux` first.
- **Timed snapshot, later.** `every` minutes, 0 for off, board-wide,
  through the same worker, counted against `max_photos` and exempt from
  the caller limits. A second line in the login banner says so while it is
  on.
- **On the S3 panel: no.** The S3 has no camera, and carrying photos from
  one board to another is not a board feature. Worth revisiting only if a
  board with both a panel and a camera is ever a profile (Freenove make an
  ESP32-S3 camera board; not researched here).

## 10. Risks and questions for Rob

Risks:

- **Internal heap at snap time** is the only budget not yet measured. If
  phase 3 shows no 32 KB DMA block after a busy hour, the options are to
  refuse the snap politely when the block is not there, to keep the camera
  initialised from start (32 KB of internal heap held all the time, but
  never fragmented away), or to lower `BBS_HEAP_RESERVE` pressure
  elsewhere. Measure first.
- **GPIO 2 and download mode with a card in.** If it blocks flashing,
  "take the card out to flash" goes on /install for this board.
- Two ESP32 image sets with one chipFamily: the picker must ask. Wrong
  image either way still boots.
- The camera stalls behind LittleFS writes (cache off); handled by
  validating each JPEG, not measured yet.
- CH340 variant and the exact camera module are UNCONFIRMED; the boot log
  names the sensor.

Questions:

- **Apache-2.0**: esp32-camera and esp_jpeg are Apache-2.0, like the IDF and
  mdns already in the image. The 0.22.1 Improv decision treated Apache-2.0
  as a reason to write our own. Is linking esp32-camera fine, or does the
  camera wait for that to be settled for the whole project (the combined
  image is effectively GPLv3 either way, through the IDF)?
- **Snap default**: `staff` as proposed, or `users`?
- **Photos default**: `users` as proposed, or `all` so guests see them?
- **OV2640 only**, adding OV3660 only if the bench shows one? Or both from
  the start for 7.5 KB?
- **Card in during the esptool read on COM13?** It decides whether GPIO 2
  already proved download mode works with a card.
- **May I flash COM13**, on your acknowledgement per flash, as with COM12?
- The continuity check (card socket pin 2 to IO13): worth one minute to
  know whether D3 is wired, or skip it and go straight to SDMMC?
- `ASSERTIONS_SILENT` (53.7 KB) is a whole-project lever, not this board's:
  queue it as its own decision?

## Sources

- [FN-REPO] Freenove_ESP32_WROVER_Board, "Apply to FNK0060"; file tree
  incl. CH340 drivers, OV2640 and OV3660 datasheets:
  https://github.com/Freenove/Freenove_ESP32_WROVER_Board
- [FN-PIN] Pinout V3.0 (USB-C, SD in 1-bit mode, LED IO2, strapping):
  https://github.com/Freenove/Freenove_ESP32_WROVER_Board/blob/main/ESP32_Pinout_V3.0.png
- [FN-SD] SDMMC sketch, pins and 1-bit begin():
  https://github.com/Freenove/Freenove_ESP32_WROVER_Board/blob/main/C/Sketches/Sketch_03.1_SDMMC_Test/Sketch_03.1_SDMMC_Test.ino
- [FN-CAM] Camera pins (CAMERA_MODEL_WROVER_KIT):
  https://github.com/Freenove/Freenove_ESP32_WROVER_Board/blob/main/C/Sketches/Sketch_06.1_CameraWebServer/camera_pins.h
- [FN-CAM-CFG] Model selected:
  https://github.com/Freenove/Freenove_ESP32_WROVER_Board/blob/main/C/Sketches/Sketch_06.1_CameraWebServer/board_config.h
- [FN-BLINK] LED_BUILTIN 2:
  https://github.com/Freenove/Freenove_ESP32_WROVER_Board/blob/main/C/Sketches/Sketch_01.1_Blink/Sketch_01.1_Blink.ino
- [FN-STORE] FNK0060 product page: https://store.freenove.com/products/fnk0060
- [RNT] Board versions 1.x without the slot, 3.x with (secondary, for the
  version history only):
  https://randomnerdtutorials.com/getting-started-freenove-esp32-wrover-cam/
- [WROVER-E] ESP32-WROVER-E datasheet:
  https://documentation.espressif.com/esp32-wrover-e_esp32-wrover-ie_datasheet_en.pdf
- [ESP32-DS] ESP32 datasheet, strapping pins:
  https://documentation.espressif.com/esp32_datasheet_en.pdf
- [IDF-SDPU] SD pull-up requirements, IDF 5.3.1:
  https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-reference/peripherals/sd_pullup_requirements.html
- [IDF-SDMMC] SDMMC host, IDF 5.3.1:
  https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-reference/peripherals/sdmmc_host.html
- [IDF-EXTRAM] External RAM, ESP32, IDF 5.3.1:
  https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-guides/external-ram.html
- [IDF-KPSRAM] components/esp_psram/esp32/Kconfig.spiram in IDF 5.3.1
  (D0WD_PSRAM_CLK_IO 17, CS_IO 16; 80 MHz needs 80 MHz flash):
  https://github.com/espressif/esp-idf/blob/v5.3.1/components/esp_psram/esp32/Kconfig.spiram
- [CAM-REG] esp32-camera on the component registry (versions, licence,
  dependencies): https://components.espressif.com/components/espressif/esp32-camera
- [CAM-README] https://github.com/espressif/esp32-camera/blob/master/README.md
- [CAM-SRC] Driver source read at 2.1.7 (ll_cam.c, cam_hal.c, Kconfig,
  CMakeLists.txt): https://github.com/espressif/esp32-camera
- [ASF-GPL] Apache on GPL compatibility:
  https://www.apache.org/licenses/GPL-compatibility.html
- Measurements: scratch builds of HEAD 9a233d0 with PlatformIO
  espressif32 6.9.0 / IDF 5.3.1, envs esp32dev, fncam_psram,
  fncam_psram_wa, fncam_cam, fncam_real, fncam_real_nopv, lev_assert,
  lev_err; sizes from firmware.bin and `_bss_end`, per-component figures
  from the linker map (flash rodata per object excludes merged strings,
  so those are floors; the build-to-build totals are exact).


## Second camera board on order (Rob, 2026-09-24)

An "ESP32-S3-CAM" (Aideepen listing): ESP32-S3 N16R8 (16 MB flash, 8 MB
PSRAM), an OV3660 3 MP camera, dual USB-C, an external antenna. Rob: "keep
libraries in mind for S3 and ESP32 base". So the camera work is built for
two chips from the start, not ported later:

- **The camera plugin knows no pins and no chip.** Every camera pin, the
  sensor model, the SD mode and pins, and the onboard LED come from the
  board profile in `src/board.h` (`BBS_CAM_*`), the way the S3 panel's
  pins do. The plugin compiles unchanged for either chip.
- **Sensor drivers per board, not all 17.** This board: OV2640. The
  S3-CAM: OV3660 (and OV2640 only if the bench shows its module varies).
  Selected per board in that board's sdkconfig defaults layer, so neither
  image carries the other's driver.
- **The SDMMC path is written for both chips.** On the ESP32 the SDMMC
  slot's pins are fixed (14/15/2); on the S3 they go through the GPIO
  matrix and come from the profile. One code path, pins from the profile.
- **esp32-camera** supports both chips in the same component version;
  pin that version once for both boards.
- **Frame sizes:** the OV3660 reaches QXGA (2048x1536); the CONFIG
  resolution choices come from the profile's sensor, so neither board
  offers a size its sensor lacks.
- **Flash is not the constraint on the S3** (16 MB), so the S3-CAM profile
  can take the same 4 MB layout first and grow later, as the Waveshare did.
- Its pin map, LED and USB arrangement are taken from its own schematic
  when it arrives, not from this listing or from memory; clone S3-CAM
  boards differ.


## Snapshot command, self-timer and timelapse (Rob, 2026-09-24)

Rob: "a timer might be good ... it has to be fun for the end users", and
CONFIG camera gets "Automatically take a picture every x seconds", up to a
day, "for creating all sorts of timelapses".

- **The command is `SNAPSHOT`**, alias `SNAP` (check for a clash with
  every existing shortcut first; nothing detects one). `CAMERA` stays the
  status and staff verb. Level: the plugin's snap level (staff by default).
- **No countdown, no self-timer** (Rob: "a BBS user cant be in front of
  it"). The caller is somewhere else; the camera looks at the board's room.
  `SNAPSHOT` takes the picture at once, with a short fx beat while the
  sensor comes up (the spinner, since bring-up takes a moment anyway) and
  a one-frame reverse-video shutter flash where the terminal has reverse.
- **Then straight to the photo:** "Photo 42 saved: PHOTO-20260924-1712.JPG
  (FILES, area 12)", and **"Download it now? (y/N)"**. Y starts the same
  YMODEM/XMODEM download the file areas use, with the same protocol choice,
  on that file, so a caller can have the picture in seconds without going
  into FILES. N returns to the prompt. Gated by the Photos area's download
  level, not only the snap level: a caller allowed to snap but not to
  download is told where it went and not offered the download.
  - Staff online get a notice line, "Node 3 took a photo".
  - **"Use flash LED?"** in CONFIG camera (Rob): yes/no. Yes lights the
    drive-light NeoPixel white for the length of the exposure, for callers'
    snapshots and timelapse shots alike; no leaves the drive light doing
    its usual effect. Default: yes, since it doubles as the only visible
    sign in the room that a picture is being taken. Does nothing when the
    lights plugin has no drive pin.
  - Works at 40 and 80 columns, and in plain ASCII (no reverse flash there,
    just the words).
  - If the text-art preview stretch is approved, the caller sees the photo
    drawn in their terminal before the download question, which is the
    payoff.
- **Timelapse, in CONFIG camera:** "Auto photo every" in seconds, 0 = off,
  up to 86,400 (a day).
  - Taken by the board itself from the plugin's tick, and labelled as
    such in the name (`TL-20260924-171200.JPG`) so a sysop can tell them
    from callers' photos.
  - Timelapse shots go to their own folder inside Photos
    (`Photos/timelapse/`) with their own max-files setting, so a fast
    timelapse cannot prune away callers' snapshots and vice versa. Oldest
    goes first, by the date-stamped name, same safe write, rename, then
    prune.
  - A floor on the interval, to be measured on the bench: the camera comes
    up per shot (about 32 KB of internal DMA memory while it is on), so a
    very short interval either keeps the camera up between shots (memory
    held) or is refused. Propose the floor from the measured bring-up time;
    somewhere around 10 s is the likely answer.
  - Card wear: a 10 s interval is 8,640 files a day. The max-files cap
    bounds the space; the plan should say what it does to a card's write
    endurance at the shortest interval.
  - Skipped silently while a caller's snapshot is being taken, and while
    the card is out.
- **Stretch, with its own go:** a caller can download a timelapse as one
  file, frames zipped (the backup zip writer exists), or an MJPEG.


## Limits, notices and naming (Rob, 2026-09-24)

Supersedes the earlier "60 s per caller, 20 an hour board-wide" proposal.

- **Per caller: at most 10 snapshots an hour and 20 a day.** Counted per
  account (by the permanent user id, so a rename does not reset it); for a
  guest, per address as well as per handle, so a guest cannot reset the
  count by reconnecting under a new name. Held in RAM: a small table, not
  per session, and a reboot clears it (accepted; say so in COMMANDS.md).
  Staff are not exempt unless the sysop is. Whether sysop is exempt: yes.
- **The caller is told on every snap** where they stand: "Snapshot 3 of 10
  this hour, 7 of 20 today." At the limit: "That is 10 this hour; the next
  one is allowed at 17:42." (the time from the oldest snap in the window),
  and the same for the day.
- **Naming, a CONFIG camera setting "Name snaps":** one consistent scheme,
  with the handle placed by the sysop's choice:
  - `date` (default): `SNAP-20260924-171204.JPG`
  - `date + handle`: `SNAP-20260924-171204-quantumrob.JPG` (Rob's "append
    username")
  - `by handle`: `quantumrob/SNAP-20260924-171204.JPG`, a folder per caller
    inside Photos (Rob's "store snaps by username")
  Handles are made FAT-safe for a filename (the characters FAT refuses are
  dropped, and the case is kept); a guest's handle gets a `guest-` prefix
  so it is never mistaken for an account's folder.
- **System snaps (the timelapse) have their own naming and place:**
  `timelapse/TL-20260924-171200.JPG`, whatever "Name snaps" says.
- **Pruning with folders:** max files counts every caller photo across
  Photos and its handle folders, oldest (by the timestamp in the name)
  first, and an emptied handle folder is removed. The timelapse folder
  keeps its own max files and is pruned separately.
- In the FILES listing, `by handle` folders show as the caller's name
  under the Photos area; a caller can find their own pictures.


## Retention: age, count and card space (Rob, 2026-09-24)

Rob: max files "should consider drive space remaining and number of snaps
... by users and by the system", with "remove user snaps after x days",
because an 8 GB card holds a great many images. Three rules, each its own
CONFIG camera setting, applied oldest first after every write and once a
day from the plugin's tick:

- **Keep caller snaps for** N days (0 = no age limit; default 30). A
  separate **Keep timelapse for** N days (default 7, because a timelapse
  piles up far faster).
- **Max caller snaps** and **Max timelapse shots** (0 = no count limit),
  each counted on its own, as before.
- **Card space floor:** the camera never lets the card's free space drop
  below a CONFIG figure (default 10% of the card, or 512 MB, whichever is
  smaller). Below it, the oldest photos go first (timelapse before callers'
  snaps, since they are the cheaper to lose) until there is room; if the
  floor still cannot be met from Photos alone, the snap is refused with a
  plain message rather than eating space that file areas, forums and the
  nightly backups also use. Free space comes from the sd plugin's cached
  figure, never a fresh FAT scan per shot.
- For scale, in the plan's numbers to be measured on the bench: a UXGA
  JPEG at the default quality is roughly 100 to 250 KB, so 8 GB is tens of
  thousands of photos. The age rule is what keeps it tidy; the space floor
  is what keeps it safe.
- CAMERA (status, staff) shows: photos stored (callers / timelapse), their
  space, the oldest date kept, and the free space against the floor.


## Latest photo on the directory listing (Rob, 2026-09-24; after the camera works)

Rob: with announce running, show "a small picture" of the last photo the
board took on its directory row ("plenty of space to the right"),
"a background send it along thing", updating at most about 4 times an hour.

- **Opt-in, off by default**: CONFIG camera "Show latest photo on the
  directory". It publishes a picture of somebody's room; the sysop chooses.
- **Small**: a thumbnail about 160x120, about 5-10 KB of JPEG. Take it as a
  second, small capture at snap time (the sensor can switch frame size)
  rather than decoding and scaling the big one; measure both.
- **Background, rate-limited**: at most 4 an hour, whatever the snap rate;
  sent by announce's existing non-blocking socket from its tick, as its
  own POST to a new endpoint beside /announce, never inside the heartbeat.
  Rule no. 1 applies: no loop stalls, measured.
- **Directory side** (a site version of its own, and PROTOCOL.md so any
  directory can implement it): accepts only from a listed board with its
  token; JPEG magic and a size cap checked; metadata stripped; one image
  per board, replaced each time; shown to the right of the row; covered by
  the report/moderation design already queued; dropped when the board is
  delisted or stops sending.
