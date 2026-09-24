<!--
 ===========================================================================
  µnleashed BBS
 ===========================================================================

 File:         ESP32_BOARD_CHOICE.md
 Purpose:      Which ESP32 to build a board on, and roughly how many caller
               nodes each one would carry.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v3 or later
 SPDX-License-Identifier: GPL-3.0-or-later
 ===========================================================================
-->

# Which ESP32 to use

## The requirement: two cores and Wi-Fi

**Both, or it is not a candidate.** This is architectural, not a preference.

The BBS runs its cooperative loop pinned to core 1 (`BBS_TASK_CORE`) because
Wi-Fi and lwIP own core 0. That split is what lets one loop serve every
caller without the radio's work showing up as latency on somebody's line.
On a single core FreeRTOS schedules both on the same one, and a loop that
yields politely is exactly the thing that suffers when the radio gets busy.
It would run. It would not run well, and fixing it properly means
restructuring the core rather than changing a setting.

Wi-Fi has to be on the chip for the same kind of reason: a board whose whole
premise is telnet over Wi-Fi should not need a second chip and a host
protocol to answer the phone.

That leaves two families:

| Supported | |
|---|---|
| **ESP32** | WROOM-32E, WROVER and relatives |
| **ESP32-S3** | with or without PSRAM |

| Ruled out | Why |
|---|---|
| ESP32-S2 | single core, and less memory than the part already in use |
| ESP32-C3 | single core |
| ESP32-C6 | single core, despite Wi-Fi 6 and plenty of SRAM |
| ESP32-H2 | single core, and no Wi-Fi at all |
| ESP32-P4 | **no integrated wireless**, despite having the most SRAM of the family |

## Read this first

**Only the ESP32-WROOM-32E has been tested.** Everything else on this page is
arithmetic from one measured board, and arithmetic is not a measurement.

The figures for other parts are derived from a single known quantity, what a
caller session costs, applied to memory sizes taken from Espressif's
datasheets. What none of them account for is how much DRAM each part actually
leaves for statically allocated data once its ROM and its radio have taken
theirs, and that number is not on any datasheet. On the WROOM it turned out
to be roughly 180 KB of a part advertised as having 520 KB of SRAM.

So treat the node counts below as the right order of magnitude and nothing
finer. When somebody builds one of these, this page gets updated with what it
actually did.

## What is actually known

Measured on the reference board, an ESP32-WROOM-32E at 0.21.8
(2026-09-22). This table used to carry the 0.17.1 figures, which
showed 34 KB of headroom: forums, the message editor and the
per-session buffers spent most of it, which is why these get
re-measured at every milestone.

| | |
|---|---|
| One caller session | **6,980 bytes** (from DWARF, not estimated) |
| Sessions allocated | `BBS_MAX_NODES + 2`, for the busy line and the hidden sysop node |
| Ten nodes | 12 sessions, **83,760 bytes** |
| Total static RAM | **175,440 bytes** |
| Usable DRAM ceiling | **180,736 bytes** |
| Headroom left | **5,296 bytes** |
| Sockets | **16**, which is lwIP's hard maximum in IDF 5.3.1 |

That ceiling is in the linker script, not on a datasheet:
`esp-idf/esp_system/ld/memory.ld` sets `dram0_0_seg` to `org = 0x3FFB0000,
len = 0x2c200`, and `sections.ld` asserts `_bss_end` stays inside it. So the
figure to measure is `_bss_end - 0x3FFB0000`.

**PlatformIO's RAM percentage is measured against 327,680 and is therefore
wrong by a factor of 1.81.** It reported 54.9% for a build that would not
link. Multiply its number by 1.81 for the truth: 55% on its scale is the
wall. At 0.21.8 the board is at 97% of what it actually has, which
PlatformIO reports as 53.5%.

**And the node count is bounded by sockets before it is bounded by RAM.**
IDF 5.3.1 caps `CONFIG_LWIP_MAX_SOCKETS` at 16, the listener, mDNS and SNTP
take three, and announce and the backup window each want one while active.
That, not memory, is what makes ten caller lines the honest number on this
part. A value outside the Kconfig range is discarded rather than clamped, so
check the generated `sdkconfig.esp32dev` rather than what the defaults file
asks for: this project asked for 24 and silently ran on 10 for two builds.

Everything else on the board, the buffers, the tables, the stacks, the
plugins, comes to about 90 KB. That part does not grow with the node count,
so the arithmetic for any part is:

```
nodes = (usable DRAM - 90 KB of fixed cost - headroom you want) / 6,980 - 2
```

subject to the socket ceiling, which on any ESP32 running IDF 5.3.1 is
`16 - 3 infrastructure - 2 transient`, so about eleven sessions whatever the
memory says.

## The parts

| Part | SRAM | Cores | Wi-Fi | Nodes | Confidence |
|---|---|---|---|---|---|
| **ESP32-WROOM-32E** | 520 KB | 2 | yes | **10** | **measured** |
| ESP32-WROVER (PSRAM) | 520 KB + 4-8 MB | 2 | yes | 24-30 | estimate |
| ESP32-S3, no PSRAM | 512 KB | 2 | yes | 12-20 | estimate |
| ESP32-S3 + PSRAM | 512 KB + up to 8 MB | 2 | yes | 24-30 | estimate |

Nothing else in the family meets the two-core, on-chip-Wi-Fi requirement.
The single core parts are listed above with the reason; the P4 has the most
memory of any of them and no radio at all.

### ESP32-WROOM-32E, the reference

Ten caller nodes, a busy line and a hidden sysop node. This is the part the
core is sized for and the only one anybody has run.

Sixteen nodes does not fit. That is not a guess: it is where the build
stopped linking.

### ESP32-WROVER, and any ESP32-S3 with PSRAM

The upgrade that actually changes the arithmetic, because of one setting:

```
CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y
```

That puts statically allocated data in PSRAM, which means the session pool,
by far the largest thing this firmware owns, stops competing for internal
DRAM at all. The node count then stops being a memory question.

The cost is that PSRAM is slower than internal SRAM, so a session's output
buffer living there costs a little on every write. For a board pretending to
be a modem this is unlikely to be noticeable, but it has not been measured.

### ESP32-S3 without PSRAM

Similar internal SRAM to the original, a more flexible memory layout, and
the same dual-core arrangement the BBS already assumes. Probably somewhat
more usable DRAM than the WROOM, hence the wider range: nobody has looked.

## A board this firmware has a profile for: Waveshare ESP32-S3-LCD-1.47

The first part other than the WROOM that the firmware has run on (1.1.0).
It is a board profile, not a fork: `-DBBS_BOARD_WS_S3LCD147` in its own
PlatformIO environment picks its defaults out of `src/board.h` and compiles
in what only it has. The WROOM's image carries none of it.

```
pio run -e ws_s3_lcd147 -t upload --upload-port COMn
pio run -e ws_s3_lcd147 -t uploadfs --upload-port COMn     (the screens)
```

The pins below are Waveshare's own schematic, read for this port and
recorded with their sources in
`internal/board-waveshare-s3-lcd147-2026-09-24.md`.

- **The part.** SKU 28317, the USB-A stick: a bare ESP32-S3R8 (8 MB of octal
  PSRAM in the package), a 16 MB quad flash, a TF slot, one WS2812B and a
  1.47" 172x320 ST7789 panel. The USB-C version (1.47B) moves the backlight
  and is not this profile.
- **USB is the chip's own.** There is no USB-serial bridge: the plug goes to
  GPIO 19 and 20, which are the console, the flashing port and Improv's
  port at once (USB-Serial-JTAG). On the one PC it has been flashed from,
  esptool's automatic reset did not reach the chip, so the first flash wants
  BOOT held while RESET is tapped, and a press of RESET afterwards.
- **What it ships with** (`src/board.h`), every one a CONFIG setting:
  - no activity LED; the lights plugin is on as shipped, with its drive
    light on the onboard WS2812B (GPIO 38, RGB order);
  - the SD card on the TF slot in SPI mode: CS 21, MOSI 15, CLK 14, MISO 16;
  - the serial bridge on header IO2 (RX) and IO1 (TX): 16 and 17 are the TF
    slot, and the header's RXD and TXD (44 and 43) carry the chip's ROM boot
    banner at every reset;
  - the panel plugin (`CONFIG panel`, `PANEL`), portrait with the plug at
    the top, laid out like a phone's status bar: the name, the address and
    the uptime with the card's free space turning in the bar; status
    glyphs, the Wi-Fi antenna and the clock under it; who is on and the
    recent logins, logoffs and pages; free heap and calls today; and the
    lights' strip as a row of square LEDs.
- **Pins it refuses.** 26 to 37 are the flash and the octal PSRAM, and 19
  and 20 are the USB; 22 to 25 do not exist on the S3. 6 to 11, the WROOM's
  flash, are ordinary pins here. `syscfg::pinProblem` applies whichever
  chip's rule the build is for.
- **Memory, measured.** Static DRAM is `_bss_end - 0x3FC88000` against
  341,760, and on the S3 that figure includes the IRAM, which shares the
  same SRAM: 246,656 at 1.1.0-dev.7, of which 81,408 is IRAM. The first
  flash showed the real constraint is the internal heap, not static RAM:
  with PSRAM enabled the IDF preallocates Wi-Fi's TX buffers internally and
  keeps a 32 KB internal pool, and 35,707 bytes were free with nobody on,
  which refused the lights and the panel. Wi-Fi's and lwIP's buffers now go
  to PSRAM first (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`), and the plugins'
  internal reserve is 16 KB on this board rather than the WROOM's 40. The
  session pool is still in internal RAM: moving it to PSRAM is the separate,
  measured step described above, and it needs `EXT_RAM_BSS_ATTR` on the
  pool, not only the option.
- **Nodes.** Ten, the same as the WROOM. The socket ceiling (lwIP's 16) does
  not move with the chip.

### The ones that are out, and why it is worth saying

**ESP32-C3, C6, S2, H2: single core.** The C6 is the one worth regretting,
since it has 512 KB of SRAM and Wi-Fi 6, which would suit a board that sits
on a home network for years. It is still one core, and the core split is not
a detail this design can give up cheaply.

**ESP32-P4: 768 KB of SRAM and no radio.** The most memory in the family and
the wrong chip. Espressif's own answer is a second part from the C or S
series acting as a wireless companion over SPI, SDIO or UART. That is a
sensible design for what the P4 is aimed at, and for this it means two chips
and a host protocol to answer a telnet connection.

If any of these are ever revisited it should be because somebody decided to
restructure the core, not because a datasheet looked appealing.

## Off the family entirely: Teensy 4.1

Rob has several and asked what it would take. Worth recording because the
answer is "less than you would think, and the question is not technical".

| | |
|---|---|
| Processor | ARM Cortex-M7, 600 MHz, with FPU |
| RAM | **1024 KB** (512 KB ITCM, 512 KB DTCM), plus two QSPI sockets for up to 16 MB PSRAM |
| Flash | 8 MB |
| Network | **built-in 10/100 Ethernet PHY**, DP83825, needs the connector kit |
| SD card | **built-in socket, native 4-bit SDIO**, no wiring at all |
| Cores | one |

The two-core rule does not apply here, and that is not a loophole. The rule
exists because a Wi-Fi stack takes time unpredictably and the cooperative
loop is what suffers. Wired Ethernet on a 600 MHz M7 is a different problem:
the driver load is lower, it is deterministic, and there is roughly ten
times the compute to absorb it.

On memory it is not close. A megabyte against the WROOM's ~180 KB of usable
DRAM puts it past a hundred nodes on the arithmetic alone, at which point
sockets and loop fairness are the limit and memory is not in the
conversation.

**Effort to port, estimated:**

| Piece | Effort | Risk |
|---|---|---|
| `platform_teensy.cpp` | ~1 week | low |
| Networking | 1-2 weeks | **high** |
| Filesystem and storage model | 3-4 days | medium |
| Build profile and testing | 3-4 days | low |
| Total | **3-4 weeks** | |

The core ports nearly free. It already builds and runs on Linux, which is the
evidence that it is not ESP-specific, and a cooperative loop maps onto
Arduino's `loop()` without argument.

**Networking is the part that could run long and the part to prototype
first.** The BBS uses BSD sockets and `select()`; Teensy's Arduino-style
`EthernetServer` and `EthernetClient` are not that. QNEthernet is the route
worth trying because it is lwIP underneath, the same stack ESP-IDF uses, so
the sockets layer may adapt rather than be rewritten. Find that out before
committing to the rest.

Gone and needing replacements: NVS, `esp_littlefs` and the partition model,
mDNS. All replaceable, none free. The backup window uses sockets too, so it
rides on the same decision.

**The question that actually matters is not technical.** The pitch is a BBS
on a five dollar chip. A Teensy 4.1 with the Ethernet kit is around forty
five dollars, and it would be a better machine and a different product. The
constraint is what has produced the good decisions here: 40 column layouts,
static allocation, measured budgets, a 6,000 byte session that somebody
argued about. Take the constraint away and the design pressure goes with it.

So: the WROOM stays the reference and the identity. Teensy is the serious
board, and porting to a genuinely different architecture is the only real
proof that `plat::` is an abstraction rather than a hope. If it ports in
three weeks, that says something true about the design.

## The limit is probably not memory

Past about twenty simultaneous callers, RAM stops being the interesting
constraint and the radio starts. Twenty or thirty concurrent telnet sessions
over Wi-Fi brings in lwIP buffer sizing, Wi-Fi throughput, and whether one
cooperative loop shares itself fairly among that many sockets. None of that
has been tested either.

Ten is comfortable. Twenty to thirty on a PSRAM part looks reasonable.
Anything beyond that is a different engineering problem and should be
measured before it is promised.

Also worth remembering: a hobby board rarely sees more than one caller at a
time. The node count is mostly about what happens on the night somebody
posts a link somewhere busy.

## What changes when moving to a bigger part

Little, by design. Block A parameterised the lists and gave node numbers two
digits so the count stopped being baked into layouts.

- `BBS_MAX_NODES` in `src/config.h`. One line.
- `CONFIG_LWIP_MAX_SOCKETS` in `sdkconfig.defaults`: one per caller plus the
  listener, mDNS and the backup window.
- `CONFIG_SPIRAM_*` for a PSRAM part.
- A PlatformIO environment rather than a fork. This is already queued as
  "build profiles" and a bigger board is the same mechanism as a chat-only
  board or a logger-only one.

Two hard ceilings sit at 255 whatever the part, because `Session::id` and
`Session::listIdx` are both `uint8_t`. Nothing is near them.

## Building for the smallest part on purpose

The WROOM is the floor and that is deliberate. Sizing to it is what produced
the 40 column layouts, the static allocation, the 6,000 byte session and the
habit of measuring rather than assuming, and every one of those makes the
board better on a larger part rather than merely possible.

The reverse does not work. Build for an S3 and hope it squeezes onto a WROOM
and you find out at link time.
