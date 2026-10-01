<!--
µnleashed BBS: ADDING_A_BOARD.md

Porting µnleashed to a new ESP32 or ESP32-S3 board, step by step, for a
person working with Claude Code: identifying the board, its pins, the
profile, the builds, the tests, the bench proof and the pre-release.

Copyright 2026 - Robert Mech
License: GNU General Public License v3 or later
SPDX-License-Identifier: GPL-3.0-or-later

Documentation for µnleashed BBS, part of the same distribution as the
source. See the LICENSE file for terms.
-->

# Adding a board

**Applies to versions:** firmware 1.2.0 (the v1.2.0 tag and main since it).
The file names, tables and line shapes below are 1.2.0's; check them
against the tree you branch from.

This is how a board the firmware does not support yet becomes one it
does. Every step here was learned on a real board, mostly the hard way,
and each trap names the board that taught it. Read
[CONTRIBUTING.md](CONTRIBUTING.md) first for the flow, the sign-off and
the licence.

It is written for somebody working with Claude Code. The
[board-bringup agent](.claude/agents/board-bringup.md) follows this page as
a checklist and reports against it, and the project's other agents in
`.claude/agents/` (`code-review`, `bbs-qa`, `bbs-regression`, `optimize`,
`tty-ux`, `screen-artist`) are there to use. Claude Code is good at reading
a schematic, a vendor's library and a factory image; it is not a
substitute for you looking at the board. Where a step says "you", it
means your eyes and your hands.

## What gets accepted

A board joins the project only when you have benched it yourself, with
proof (the full list is in [Bench proof](#bench-proof)). It ships first as
a board pre-release, one board's images labelled preview on the web
installer, and joins the next full release after that.

**Boards are chosen to maximise what the BBS can do, not to work around
vendor wiring.** A board whose design forces the firmware to give up a
feature to run on it is turned down. The KEYESTUDIO ESP32-S3 PRO (N16R8)
was, because its SD slot sits on GPIO 35 to 37, inside the octal PSRAM
bus, so PSRAM and the card cannot both work. If your board has a trap like
that, say so in your issue before you start.

Only two chip families are in scope: the **ESP32** and the **ESP32-S3**.
The loop is pinned to one core because Wi-Fi and lwIP own the other, so a
single-core part (C3, C6, S2, H2) is out, and the P4 has no radio of its
own. Displays need an S3: there is no panel code for the classic ESP32,
and a feature that is S3-only is left out of ESP32 images entirely.

## The steps

1. [Confirm the exact model](#1-confirm-the-exact-model)
2. [Back up the factory flash](#2-back-up-the-factory-flash)
3. [Collect the pins, from two sources each](#3-collect-the-pins-from-two-sources-each)
4. [Write the profile in src/board.h](#4-write-the-profile-in-srcboardh)
5. [The sdkconfig layer](#5-the-sdkconfig-layer)
6. [The PlatformIO environments](#6-the-platformio-environments)
7. [Build, and measure](#7-build-and-measure)
8. [Bring it up on the bench](#8-bring-it-up-on-the-bench)
9. [The host profile and its test](#9-the-host-profile-and-its-test)
10. [The release set](#10-the-release-set)
11. [Bench proof](#bench-proof)
12. [The pull request, and the pre-release](#12-the-pull-request-and-the-pre-release)

Choose a short **key** for your board first: lower case letters and
digits, like `ws2`, `wseth` or `mf35`. It names the sdkconfig layer, the
host profile and the harness's `--board`. The examples below use a
made-up `acme`, an "Acme ESP32-S3 Display 2.8", with profile define
`BBS_BOARD_ACME_S3LCD28`.

---

## 1. Confirm the exact model

From the board itself, never from a product listing alone. Listings reuse
names and photos across different designs, and vendors revise boards
without changing the name.

- **Photos of both sides**, sharp enough to read the silkscreen, the
  module's marking and any revision number. Keep them: they go in the PR.
- **esptool's readout** of the chip, revision, flash and PSRAM. With the
  board on USB (`pip install esptool` if you have no copy):

  ```
  esptool.py --port /dev/ttyACM0 chip_id
  esptool.py --port /dev/ttyACM0 flash_id
  ```

  On Windows the port is `COM5` and so on. Save the whole output, MAC
  included (you may blank the MAC's last three bytes in the PR).
- **The vendor's page that matches both**: the silkscreen and the chip
  readout. If the page and the board disagree, the board wins and you
  have the wrong page.

What this has cost before:

- A Makerfabs board was brought up from the SPI TFT's schematic while it
  was the Parallel TFT v1.0. The screen stayed dark until a photo of the
  back showed which board it was.
- The Makerfabs Parallel TFT is sold in two revisions under one name. v1.0
  has an N16R2 (quad PSRAM) and v2.0 an N16R8 (octal PSRAM) with the
  panel's pins moved, so an image for one does not boot on the other. The
  silkscreen says which. Two revisions are two profiles (`mf35`, and
  `mf35v2` in development).
- A 4" 480x480 board sold as AITRIP turned out to be a Guition
  ESP32-S3-4848S040, and a listing with the same wording and the same
  "86 box" size turned out to be a different design entirely (with a PMU,
  an IMU and an RTC). "86 box" is only the wall-box size. Only the board
  you benched is supported; a lookalike is "unverified" until somebody
  benches it.

## 2. Back up the factory flash

Before the first flash of anything, read the whole chip. It is your way
back to a working board, and it holds the vendor's own bring-up, which
you may need in step 8.

```
esptool.py --port /dev/ttyACM0 read_flash 0 ALL factory-<key>.bin
```

(`ALL` needs a recent esptool; otherwise give the size from `flash_id`,
for example `0x1000000` for 16 MB.) To put it back:

```
esptool.py --port /dev/ttyACM0 write_flash 0 factory-<key>.bin
```

Keep it out of git (it is the vendor's firmware, not ours), but keep it.

If the board has no USB-serial bridge (the S3's own USB, a
USB-Serial-JTAG port), esptool needs `--before usb_reset`, and a board
that will not answer at all goes into download mode by hand: hold BOOT,
press and release RESET, release BOOT.

## 3. Collect the pins, from two sources each

Write `pins.md` for the board: every pin it wires to anything, and for each
one **two independent sources** that agree. Good sources:

- the vendor's schematic (a PinOut table and the netlist are two);
- the vendor's demo firmware or library: what it actually drives;
- the factory image (step 2), when the library is not published;
- a well-kept third-party profile, such as ESPHome's device page for the
  board.

When two sources disagree, the code that runs on the board beats the
document, and you bench it to be sure.

Traps that are in the record:

- **A vendor schematic can carry another chip's pin names.** Makerfabs'
  Parallel v1.0 schematic draws an ESP32-S2 module symbol, so the nets
  "IO33/DB0" and "IO34/LCD_RD" are really the S3's IO47 and IO48. D0 on
  the wrong pin garbled every panel command and the glass stayed white.
  Cross-check against the firmware the vendor ships.
- **A pin can have a second job the wiki never mentions.** On the
  Waveshare ESP32-S3-ETH the camera's power is a P-FET on GPIO 8 (off at
  reset, on while GPIO 8 is low), found in the schematic and confirmed by
  a user's report; the wiki says nothing.
- **Strapping pins.** On the ESP32: 0, 2, 5, 12 and 15. On the S3: 0, 3,
  45 and 46. A strap the board pulls the wrong way blocks boot or download
  mode. The AI-Thinker ESP32-CAM's card holds GPIO 2 high at reset, so the
  board will not enter download mode with a card seated: flash it with the
  card out. GPIO 0 is that board's camera clock, so it has no BOOT button
  for the firmware to watch.
- **A pull-up can switch a thing on.** The same ESP32-CAM's flash LED on
  GPIO 4 lights from a pull-up alone, so the profile holds it low from a
  start-up constructor (`BBS_PINS_HOLD_LOW`).
- **Shared buses need every other chip-select high first.** On the
  Waveshare Touch-LCD-2 the panel and the TF slot share MOSI and SCLK. The
  panel's CS is GPIO 45, a strap pulled low at reset, and the card mounts
  before the panel starts, so the panel took the card's traffic as
  commands and drove its data line back: CRC errors on every mount.

`pins.md` is a table: pin, what it is wired to, direction, the two sources,
and a note (strap, shared, input only, free for a sysop). End it with the
list of pins a sysop may use for their own wiring, because CONFIG has to
refuse the rest.

## 4. Write the profile in src/board.h

A board is one block in `src/board.h`, selected by a define the build
passes in. Copy the block of the existing board most like yours (an S3
with a panel: `BBS_BOARD_WS_S3TOUCH2`; an S3 without one:
`BBS_BOARD_WS_S3ETH`; a classic ESP32 camera board:
`BBS_BOARD_AI_ESP32CAM`) and change it. The parts, in the order the
existing blocks use:

**The frame.** Two lines are read by tools with a regex, so their exact
shape matters:

```
#if defined(BBS_BOARD_ACME_S3LCD28)
...
#endif  // BBS_BOARD_ACME_S3LCD28
```

`#if defined(...)` alone on its line, and the closing comment naming the
define. `tools/release.py` and `tools/testclient.py` find the block that
way.

**The guards** at the top of the block:

- the target: an S3 board errors unless built for `esp32s3`
  (`#if defined(ESP_PLATFORM) && !CONFIG_IDF_TARGET_ESP32S3`), an ESP32
  board unless built for `esp32`;
- one profile at a time: the block checks the profiles before it, and the
  catch-all after the last block adds every `defined(BBS_BOARD_*)` and
  errors above one. **Add yours to that sum.**
- on an S3 board, `#ifndef BBS_CHIP_S3` / `#define BBS_CHIP_S3 1`: the
  host build has no `sdkconfig.h`, and this is how it gets the S3's pin
  rules;
- one `#error` for each setting your sdkconfig layer must have made, so a
  stale `sdkconfig.<env>` fails the build instead of shipping without it.
  The existing ones: `CONFIG_SPIRAM` for PSRAM, `CONFIG_SPIRAM_MODE_QUAD`
  for a quad part, `CONFIG_VFS_MAX_COUNT < 12`,
  `CONFIG_SPIRAM_FETCH_INSTRUCTIONS && CONFIG_SPIRAM_RODATA` for an RGB
  panel, a camera sensor's `CONFIG_*_SUPPORT`, and Ethernet's driver. Each
  says "delete sdkconfig.<env>" in its message.

**Identity:**

```
#define BBS_BOARD_NAME     "Acme ESP32-S3 Display 2.8"
#define BBS_BOARD_TAG      "ACME"
#define BBS_BOARD_VERSION  "1.0.0"
#define BBS_BOARD_PLUGINS  1
```

- `BBS_BOARD_TAG` and `BBS_BOARD_VERSION` show beside the core version,
  as `1.2.0 (ACME 1.0.0)`, everywhere a version shows. ASCII only. The
  board version starts at 1.0.0 and moves when board-only code changes;
  the core's `BBS_VERSION` in `src/config.h` is not yours to move.
- `BBS_BOARD_PLUGINS` counts the board-only plugins the profile compiles
  in: one for a panel (`BBS_HAS_LCD`), one for a camera
  (`BBS_HAS_CAMERA`). The Touch-LCD-2 has both, so 2. The registry
  `static_assert`s the total.

**Capabilities**, each a `BBS_HAS_*` the code tests (the glossary is at
the top of board.h): `BBS_HAS_PSRAM`, `BBS_HAS_SD_SLOT`, `BBS_HAS_LCD`
(with `BBS_LCD_RGB`, `BBS_LCD_I80` or the SPI default),
`BBS_HAS_TOUCH` (and `BBS_TOUCH_POLL` for a controller that is polled
rather than read on its interrupt line), `BBS_HAS_CAMERA`, `BBS_HAS_SSH`
with `BBS_SSH_MAX` (every S3 profile has SSH on), `BBS_HAS_ETH`,
`BBS_HAS_CHIP_TEMP`. And the bus choices: `BBS_SD_SDMMC1` for a card on
the ESP32's SDMMC slot, `BBS_SPI_SHARED` for a panel and card on one SPI
bus, `BBS_PSRAM_QUAD` for an S3 with quad PSRAM.

**Pins**: the card (`BBS_SD_CS`, `BBS_SD_MOSI`, `BBS_SD_CLK`,
`BBS_SD_MISO`, or `BBS_SDMMC_*`), the activity LED (`BBS_LED_GPIO`,
`BBS_LED_ACTIVE_LOW`; -1 for none), the BOOT and backup buttons
(`BBS_BOOT_GPIO`, `BBS_BACKUP_GPIO`; -1 where GPIO 0 is something else),
the serial bridge (`BBS_SERIAL_RX`, `BBS_SERIAL_TX`), the lights
(`BBS_LIGHTS_ON`, `BBS_LIGHTS_DRIVE_PIN`, `BBS_LIGHTS_DRIVE_ORDER`), and
the panel's, touch's, camera's and Ethernet's own pins. Anything you do
not define takes the reference board's default from the end of board.h,
so define every pin the board uses.

**The pins the board owns**, which CONFIG must refuse to a sysop, by
name, with a reason: `BBS_PINS_PSRAM`, `BBS_PINS_CONSOLE`,
`BBS_PINS_CARD`, `BBS_PINS_CAMERA`, `BBS_PINS_STRAP`, `BBS_PINS_LCD`,
`BBS_PINS_LCDBUS`, `BBS_PINS_WIRED`, `BBS_PINS_ONBOARD`, `BBS_PINS_ETH`.
`syscfg::pinProblem` (`src/core/sysconfig.cpp`) turns each list into a
refusal with its sentence. A new family of pins needs a row there too,
not only a define. The chip's own rules come first and need nothing from
you: on the S3, 26 to 37 (flash and octal PSRAM; 26 to 32 with
`BBS_PSRAM_QUAD`) and 19 and 20 (the USB port); on the ESP32, 6 to 11.

**The console.** The S3 layer puts the console on the chip's own USB
(`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`). A board whose USB socket goes to
a USB-serial chip on UART0 (the Makerfabs' CP2104, every classic ESP32
board) sets `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` in its own layer and lists
the UART0 pins in `BBS_PINS_CONSOLE` (43 and 44 on an S3, 1 and 3 on an
ESP32), so neither CONFIG nor the serial bridge plugin can take them.
Web flashing and Improv go through whichever port the console is on.

## 5. The sdkconfig layer

Settings the profile needs from ESP-IDF go in a layer of their own,
`sdkconfig.defaults.<key>` in the repository root, applied after the
shared `sdkconfig.defaults`. On an S3 target, ESP-IDF also reads
`sdkconfig.defaults.esp32s3` by itself (8 MB flash, octal PSRAM at 80 MHz,
Wi-Fi buffers 10/10, the USB-Serial-JTAG console). A classic ESP32 board
needs a named layer for anything it changes, because a
`sdkconfig.defaults.esp32` would reach the reference board too.

`.gitignore` ignores `sdkconfig.*` and lets each layer through by name.
**Add `!sdkconfig.defaults.<key>`**, or git never sees your layer.

What the existing layers set, and when you need the same:

- **The VFS table: `CONFIG_VFS_MAX_COUNT=12`.** The IDF has 8 VFS slots.
  The console(s), lwIP, three LittleFS partitions and SSH's eventfd fill
  them, and the SD card's FAT is the next. On the Makerfabs (two consoles)
  the card failed to mount with "not enough memory" and 1.7 MB of PSRAM
  free. Every S3 board with a card in 1.2.0 except the LCD-1.47 sets 12
  in its own layer, with a board.h `#error` below it. (Moving this into
  the shared S3 layer is queued; until it is on main, set it yourself.)
- **PSRAM: octal or quad.** The S3 layer is octal. A quad part (an N16R2,
  for example) sets `CONFIG_SPIRAM_MODE_QUAD=y` and the profile defines
  `BBS_PSRAM_QUAD`, which also frees GPIO 33 to 37. A classic ESP32 with
  PSRAM sets `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_USE_MALLOC=y`,
  `CONFIG_SPIRAM_IGNORE_NOTFOUND=y` and
  `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=40960` (the camera layers have
  them); the Freenove adds `CONFIG_ESP32_REV_MIN_3=y`.
- **An RGB panel needs XIP from PSRAM.** `CONFIG_SPIRAM_XIP_FROM_PSRAM=y`,
  `CONFIG_LCD_RGB_ISR_IRAM_SAFE=y`, `CONFIG_LCD_RGB_RESTART_IN_VSYNC=y`
  (the Touch-LCD-4.3B's layer). With code and read-only data in PSRAM, the
  IDF keeps the cache on through a flash write, so the panel's refill can
  still read PSRAM while LittleFS writes; without it, a PSRAM read with
  the cache off crashes. The 4.3B also runs its panel with no frame
  buffer, doubling a 400 x 240 PSRAM picture into four-line bounce
  buffers: no 768 KB buffer, 12.8 KB of internal RAM.
- **An S3 camera: `CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX=16384`.** The S3's
  camera driver takes at most that for raw frames, and the firmware asks
  internal RAM for 17 KB, not the ESP32's 33. Without it every snap is
  refused for memory. `src/platform/platform.h` `static_assert`s it on
  every S3 build, and the sensor's own `CONFIG_<SENSOR>_SUPPORT=y` with
  the others set to `n` belongs here too.
- **Anything the profile turns on only for itself**, like the ESP-ETH
  board's `CONFIG_ETH_SPI_ETHERNET_W5500=y` (and `src/CMakeLists.txt`
  links `esp_eth` only when that is set).

**A stale `sdkconfig.<env>` beats the defaults.** PlatformIO generates
`sdkconfig.<env>` once and does not regenerate it when a defaults file
changes, and a setting it records as "is not set" wins over your layer
without a word. After changing any layer, delete `sdkconfig.<env>` and
build again. The board.h `#error`s exist because this has shipped wrong
before (a board ran at 160 MHz instead of 240 from a stale file).

## 6. The PlatformIO environments

Two environments in `platformio.ini`: the board, and its release build.
The ESP32-S3-ETH's pair is the one to copy for an S3 written out in full:

```
[env:acme_s3lcd28]
platform              = espressif32@6.9.0
board                 = esp32-s3-devkitc-1
framework             = espidf
board_build.flash_mode  = dio
board_upload.flash_size = 8MB
board_build.partitions  = partitions_s3.csv
board_build.filesystem  = littlefs
upload_speed          = 921600
monitor_speed         = 115200
monitor_filters       = esp32_exception_decoder
extra_scripts         = pre:tools/pio_plugins.py
                        tools/pio_flashall.py
custom_ext_plugins    =
build_flags           = -DBBS_BOARD_ACME_S3LCD28
board_build.cmake_extra_args = -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.acme"

[env:acme_s3lcd28_release]
extends               = env:acme_s3lcd28
build_flags           = -DBBS_BOARD_ACME_S3LCD28 -DBBS_RELEASE
```

- The `_release` env **repeats the board define**: `build_flags` in an
  `extends` replaces the parent's, it does not add to it. A release env
  without the define builds the reference board under your board's name.
- `-DBBS_RELEASE` makes a build that ignores `include/secrets.h`, so no
  developer's Wi-Fi password is compiled into a public image.
- Every S3 board uses `partitions_s3.csv` and an 8 MB layout, even with
  16 MB of flash, so an 8 MB part fits too. Every classic ESP32 board
  `extends = env:esp32dev` and keeps the 4 MB `partitions.csv`. A new
  partition layout is a separate conversation.
- Several S3 envs `extends = env:ws_s3_lcd147` instead of writing it out;
  either works.

## 7. Build, and measure

```
pio run -e acme_s3lcd28
pio run -e acme_s3lcd28_release
```

Both must build with **no warnings** from our code. Then measure, from
the ELF, never from PlatformIO's summary:

**Static RAM.** PlatformIO's RAM percentage is measured against a figure
the linker never lets you reach. The ESP32 reference board once read
54.9% while it was over its limit. The real ceiling is the linker
script's DRAM segment:

| Chip | Measure | Ceiling |
|---|---|---|
| ESP32 | `_bss_end - 0x3FFB0000` | 180,736 bytes |
| ESP32-S3 | `_bss_end - 0x3FC88000` | 341,760 bytes (IRAM shares it) |

```
TC=~/.platformio/packages/toolchain-xtensa-esp-elf/bin
$TC/xtensa-esp32s3-elf-nm .pio/build/acme_s3lcd28/firmware.elf | grep -E ' _bss_end$'
```

(`xtensa-esp32-elf-nm` for a classic ESP32 build; the toolchain folder may
carry a version suffix, and on Windows the tools end in `.exe`.) The
largest statics, to see where it went:

```
$TC/xtensa-esp32s3-elf-nm -S --size-sort --radix=d -C .pio/build/acme_s3lcd28/firmware.elf | tail -30
```

Write the figure (used, free, and the flash image size) in the PR.

**The other boards must not move.** A profile is added beside the others,
never into them, so every other board's image should be what it was.
Build the reference board and at least one board of each family before
and after your change, and compare `_bss_end` and the section sizes
(`$TC/xtensa-esp32-elf-size -A firmware.elf`). Expect identical static RAM
on every other board. Two honest sources of small differences: code
that bakes in `__LINE__` (`ESP_ERROR_CHECK`) moves when lines move above
it, and the linker's call relaxation varies a few bytes between any two
links. Say which you saw. A difference in static RAM on another board is
a problem to find before the PR, not a note in it.

**Rule no. 1: nothing stalls the loop.** The BBS loop is cooperative: one
slow call in it is a stall for every caller on the board. A board's
drivers (a panel refill, a touch controller, a sensor, the camera) run on
their own task, on an interrupt, or in slices; they never wait on a bus in
the loop. Two shapes that already work: the Touch-LCD-2's panel tries the
shared SPI bus's mutex and skips a band to a later tick rather than wait
behind the card, and its touch is read from the interrupt line by an ISR,
never polled over I2C on the loop. On the bench, SYS's "Loop worst" and
"Slow passes" are the proof.

## 8. Bring it up on the bench

Flash the whole image, partitions and filesystem included. The first
install on an S3 erases the chip:

```
pio run -e acme_s3lcd28 -t erase
pio run -e acme_s3lcd28 -t upload
pio run -e acme_s3lcd28 -t uploadfs
pio device monitor -e acme_s3lcd28
```

(`pio run -e acme_s3lcd28 -t flashall` does upload and uploadfs in one
go.) Watch the console from power-up. Give it a network by Improv, from
the web installer's "Change Wi-Fi" or any Improv client, or for a
development build (never a release) put one in `include/secrets.h`, which
the board uses only while `system.cfg` has none. Then call it
by telnet on port 6400, register, go through setup, and look at SYS and
HARDWARE.

**If the panel stays dark, put the vendor's firmware back first.** Flash
your factory backup (step 2) and confirm the glass lights. If it does
not, the board is faulty and nothing else matters yet. If it does, port
the vendor's **whole** bring-up path, not just its init table: the reset
and power pins, the bus and its mode, the init list with its delays, the
panel timing, the order of the calls. The Guition 4848S040 matched the
vendor's ST7701 init table byte for byte and stayed black: the firmware
sent it through the S3's SPI peripheral and the chip never took it
(its status read back 0xFFFF), while the vendor's own configuration,
found in the factory image, bit-bangs the panel's 3-wire lines as plain
GPIOs. Done the same way, it lit. Read the vendor's actual bus
configuration out of their library, or out of the factory image if the
library is not published, not only the table.

Other things to check on the bench:

- **The card.** SD MOUNT, then MEM shows its free space. A mount that
  fails "not enough memory" with PSRAM free is the VFS table (step 5).
  The AI-Thinker ESP32-CAM's slot does not work in SDMMC one-bit mode
  under IDF 5.3.1 (the card wakes in SPI mode), so it runs over SPI.
- **PSRAM.** HARDWARE shows it, and the console's first lines say how much
  was found and mapped.
- **The camera**, if any: `SNAPSHOT` from an account, watching the
  console's heap figures. A snap refused for memory on an S3 is the
  `CAMERA_DMA_BUFFER_SIZE_MAX` line (step 5).
- **The lights and the activity LED**, if the board has a pixel or an
  LED: LIGHTS TEST.
- **Touch**, if any: a tap turns the panel's header.
- **SSH** on an S3: `ssh -p 6400 handle@board` and `-p 6422`.
- **The loop**, with a couple of callers on: SYS's "Loop worst" and
  "Slow passes". No slow pass from anything the board itself does.

## 9. The host profile and its test

The core also builds for Linux with your profile's define, so the test
suite can check your board's pin rules, its CONFIG pages and its
capabilities with no hardware. Every place a profile is named:

| File | What to add |
|---|---|
| `host/Makefile` | a `bbs_host_<key>` target (copy `bbs_host_wseth`; S3 profiles link `libwolfssh_host.a` and `$(WOLF_CPPFLAGS)` for SSH), and the name in `clean` |
| `tools/harness.sh` | a `--board <key>` case setting `BIN` and `BBS_HOST_BOARD`, the key in its error message and usage comment, and, for an S3, the three `case` lists of SSH profiles |
| `tools/parallel.py` | `PROFILE_BIN`, and the SSH tuple for an S3 |
| `tools/testclient.py` | `BOARD_DEFINES` (key to define); `SSH_BOARDS` for an S3; `PROFILE_TESTS` and, if it has a card, `PROFILE_CARD`; `ORDER_NAMES`; `CAM_BOARD` and `LIGHTS_BOARD` if it has a camera or lights; and `test_board_<key>()` |
| `tools/changed_groups.py` | `board_<key>` on the `src/board.h` row, a row for `sdkconfig.defaults.<key>`, and the camera or panel rows if it has one |

**`test_board_<key>`** checks what is specific to your board, from the
outside, the way a sysop would see it. It SKIPs unless
`HOST_BOARD == "<key>"`. The existing ones (`test_board_wseth`,
`test_board_ws2`) are the model:

- SYS, HARDWARE and PANEL say what the board is (its panel, its touch
  controller, its network);
- CONFIG refuses every pin the board owns, with the right reason
  ("that pin is the Ethernet chip's", "camera's", "flash and PSRAM"), and
  writes nothing to the file when it refuses;
- whatever the board does differently does it.

Run it, then the whole suite on your profile:

```
bash tools/harness.sh --board acme --only=board_acme
bash tools/harness.sh --board acme --card --only=board_acme
bash tools/harness.sh --jobs 8
```

The last one runs every profile, yours included (the shared SSH tests
run on every S3 profile). Everything must pass on your profile, and
nothing may change on the others.

## 10. The release set

`tools/release.py` builds the images the web installer serves. Add your
board in two places:

```
{"dir": "esp32s3-acme", "env": "acme_s3lcd28_release", "family": "ESP32-S3",
 "boot": 0x0, "board": "BBS_BOARD_ACME_S3LCD28", "table": "partitions_s3.csv",
 "tag_only": True},
```

in `BUILDS` (a classic ESP32 set is `"family": "ESP32"`, `"boot": 0x1000`,
`"table": "partitions.csv"`), and `"acme": "esp32s3-acme"` in
`BOARD_TAGS`. **`tag_only: True`** keeps a new board out of the plain
`vX.Y.Z` full release until it has been through a pre-release; it comes
off when the board joins a full release. Add the set's asset prefix
(`esp32s3-acme-`) to the release notes in `.github/workflows/release.yml`,
which name every set by hand.

You can run the whole release build for your board locally:

```
python3 tools/release.py --board esp32s3-acme
```

It refuses a dirty tree, checks the partition table, the formats and the
licence lines, builds the release env, checks that `1.2.0 (ACME 1.0.0)`
is in the image, and searches every file for leaked secrets.

## Bench proof

The PR carries all of this. Put it in a folder of the PR, `boards/<key>/`,
and the figures in the PR description:

- [ ] **Photos of both sides** of the board you benched, the silkscreen
      and module marking readable.
- [ ] **esptool's readout**: `chip_id` and `flash_id`, chip, revision,
      flash size, PSRAM.
- [ ] **`pins.md`**, every pin with two sources, and the pins left free.
- [ ] **SYS and HARDWARE captures** from the board running your build:
      the version line with your tag, PSRAM, the card, the panel or
      camera, the loop figures.
- [ ] **The host tests passing** on the board's profile: the output of
      `harness.sh --board <key>` with and without a card (or the `--jobs`
      run's summary).
- [ ] **Static RAM and image size** off the ELF, and the statement that
      the other boards' images did not move.
- [ ] **What you checked on the bench** (step 8): card, PSRAM, panel,
      touch, camera, lights, SSH, and the loop with callers on.
- [ ] **What does not work**, if anything, said plainly.

## 12. The pull request, and the pre-release

- One PR for the board, signed off ([CONTRIBUTING.md](CONTRIBUTING.md)),
  with the proof above.
- **Run the code-review agent on your diff first**
  (`.claude/agents/code-review.md`). It hunts the shapes that have
  shipped here: a guard that bounds the wrong quantity, state that leaks
  between callers, a path that only fails on hardware.
- **The documents in the same PR**: the board's entry in README.md's
  list of boards, ESP32_BOARD_CHOICE.md if the board changes the advice,
  and CHANGELOG.md. (CLIENTS.md is the machines that call in, not boards.)
  Any panel layout goes through the design agents first (`tty-ux` to
  specify it at the panel's size, `screen-artist` for art) and is built to
  the spec.
- **Review** is Rob's, plus a code-review pass and a targeted host run
  (`harness.sh --changed`). He may ask for a bench detail again; boards
  are where the surprises live.
- **The pre-release.** Once merged, Rob tags `v1.2.0-acme.1` (the core
  version, your key, a number). `tools/release.py` maps the key through
  `BOARD_TAGS` and builds only your set, the release workflow publishes it
  as a GitHub pre-release, and the web installer offers the board as a
  preview. Listing it on the site (its install family, its tested-boards
  row and buy link) is done in the site's own repository, by the project.
- **The full release.** When your board joins the next `vX.Y.0`, its
  `tag_only` comes off, and from then on every full release carries it.

Changes to the firmware that make this page wrong update it in the same
commit, and move its "Applies to versions" line.
