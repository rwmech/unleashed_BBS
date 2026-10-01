# A Bluetooth keyboard and the panel as a local terminal: feasibility

Research for Rob's question of 2026-10-01 ("Another cool option but I think
less doable, research this ... bluetooth keyboard pairing on the s3 so you
can use the display as a terminal (larger terminals only)"), with the
coordinator's addition the same day: the console uses the hidden sysop node,
and the 1.2.3 serial caller line should share its machinery if it can.

Everything below is for ESP-IDF 5.3.1 (`espressif32@6.9.0`), checked against
the tree at `~/.platformio/packages/framework-espidf@3.50301.0`. Figures
marked **measured** come from builds made for this report in a scratch
folder (no source, configuration or board touched, nothing flashed).
Figures marked **estimate** show their working. Nothing here was run on a
board, so every run-time heap and latency figure is an estimate and is
listed under "Bench checks owed" at the end.

## Bottom line

**Feasible, and not where the cost is expected.** The terminal on the panel
is the cheap, reusable part (about 15 to 25 KB of flash, a few KB of RAM
that can live in PSRAM). The Bluetooth half is the expensive part, and the
expense is internal RAM and the radio, not flash:

- **Flash is not the problem.** NimBLE plus the IDF's HID host adds
  **178,112 bytes** to an S3 image (measured). The S3 slot is 3 MB and the
  images are about 1.4 MB.
- **Internal RAM is.** The same build adds **24,600 bytes of static
  internal RAM** (measured), which on an S3 comes straight off the internal
  heap, plus an unmeasured run-time share (at least 11.5 KB of task stacks
  and the controller's own buffers). The 4.3B's internal heap low on the
  bench was 50.7 KB; this takes it to about 26 KB before the controller
  allocates anything, against a 16 KB `BBS_HEAP_RESERVE`. **On the panel
  boards, BLE needs the session pool moved to PSRAM first** (the 1.3.0
  camera-board move, about 84 KB of internal RAM back, estimate).
- **The radio is the Rule no. 1 question.** While a keyboard is connected,
  Espressif's coexistence gives Wi-Fi about half of each ~100 ms period,
  and Wi-Fi sleeps in the other half **even with `WIFI_PS_NONE` set**. That
  is the same family of fault as the 0.18.0 and 0.21.1 power-save stalls,
  smaller (tens of milliseconds, estimate, not a second), and it lands on
  every caller, not just the one at the keyboard. Compiling Bluetooth in at
  all also **forces `CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE` back on**
  (measured in the generated sdkconfig), undoing half of the 0.21.1 fix in
  every image that carries it.
- **Keyboards:** the S3 is Bluetooth LE only. Modern BLE keyboards work
  (Logitech MX Keys Mini, Pebble Keys 2 K380s, Keychron K2 HE, every ZMK
  board); Classic-only ones, the original Logitech K380 among them, never
  appear.

**Recommendation, one line:** build the panel terminal on the 1.2.3
stream-line core, feed it first from a keyboard on a **sat** (the 1.2.3
terminal sat, which costs the board no Bluetooth at all and takes Classic,
BLE, USB and PS/2 keyboards), and add direct BLE pairing only as an opt-in
per board after the session pool is in PSRAM, with the radio on only while
somebody is actually typing.

**Caller-visible:** direct BLE, as built here, would change how the board
behaves for callers who are not at the keyboard (network latency and
airtime while a keyboard is connected). The sat route does not.

## Summary table

| Approach | Board flash | Board static internal RAM | Board run-time internal heap | Radio cost to callers | Keyboards | Boards |
|---|---|---|---|---|---|---|
| Panel terminal (any input) | ~15-25 KB (estimate) | ~0.5 KB, grid in PSRAM (estimate) | 0 | none | n/a | 4.3B, G4848, MF35 v1/v2 |
| A. BLE direct, NimBLE | **+178,112** (measured) | **+24,600** (measured) | stacks 11.5 KB + controller, unmeasured | Wi-Fi ~50% airtime while connected; scan bursts | BLE HID only | as above, after the PSRAM session move |
| A'. BLE direct, Bluedroid | **+417,488** (measured) | **+19,416** (measured) | more than NimBLE (Espressif) | same | BLE HID only | not recommended |
| B. USB keyboard, OTG host | **+55,088** (measured) | **+1,712** (measured) | ~8 KB of stacks + DMA buffers (estimate) | none | any USB keyboard | Makerfabs only (its own native USB-C), VBUS to check |
| C. Keyboard on a sat (1.2.3 CALLIN) | ~0 beyond 1.2.3 | ~0 beyond 1.2.3 | 0 | small ESP-NOW frames, already in use | any the sat takes: USB, PS/2, BT Classic and BLE (an ESP32 classic sat) | every panel board on Wi-Fi |

## 1. Which keyboards pair

**The S3 has no Classic Bluetooth, in either stack.** Espressif's 5.3.1
guide: "ESP-Bluedroid for ESP32-S3 supports Bluetooth LE only. Classic
Bluetooth is not supported", and the same for NimBLE
([Bluetooth overview, 5.3.1, ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/bluetooth.html)).
So only keyboards that implement HID over GATT (HOGP) can pair.

There is **no fallback in the protocol sense**. A keyboard either implements
HOGP or it does not; a Classic-only keyboard is never seen by an LE scan.
"Bluetooth 5.x" on a box says what radio is inside, not which HID profile
its firmware runs.

Known, from reports with primary sources:

- **Work (BLE):**
  - Logitech MX Keys Mini: paired on an S3 and an ESP32 with the IDF's own
    example ([esp-idf #15379](https://github.com/espressif/esp-idf/issues/15379)).
  - Logitech Pebble Keys 2 (K380s): Logitech lists it as Bluetooth Low
    Energy ([comparison, LTT Labs](https://www.lttlabs.com/compare/logitech-k380-vs-logitech-mx-keys-mini),
    [RTINGS](https://www.rtings.com/keyboard/tools/compare/logitech-mx-keys-mini-logitech-k380/28677/1812)).
  - Keychron K2 HE: a host project pinned to IDF 5.3.1 (Bluedroid) names it
    tested, and found it registers 8+ notifications, so
    `CONFIG_BT_GATTC_NOTIF_REG_MAX` must be raised from 5 or keystrokes are
    silently dropped ([hid-proxy-for-ble-keyboard](https://github.com/anisehid/hid-proxy-for-ble-keyboard)).
  - Any ZMK keyboard: ZMK is BLE and needs LE Secure Connections (Bluetooth
    4.2+) ([ZMK docs](https://zmk.dev/docs/features/bluetooth)). The S3's
    stacks do Secure Connections (`CONFIG_BT_NIMBLE_SM_SC=y` in the probe).
- **Do not work (Classic only):** the original Logitech K380 (Bluetooth 3.0
  Classic; the same issue's reporter found it working only on the
  dual-mode ESP32: "the BT portion is working well, but not the BLE
  portion").
- **Unverified, do not list:** Apple's Magic Keyboard and the no-name
  multi-device keyboards. Test before naming them anywhere.

How common BLE keyboards are: the major brands' current lines are BLE
(Logitech's MX and Pebble ranges, Keychron's newer boards), and the older
and cheaper ones are often Classic. That is a judgement from the models
above, not a survey; the site should list tested models, not promise "any
Bluetooth keyboard".

## 2. The stack, measured

Three scratch builds for `esp32s3`, each against the same baseline: Wi-Fi
station started with `WIFI_PS_NONE`, ESP-NOW initialised, the S3 layer's
Wi-Fi buffers (10/10, BA window 10), octal PSRAM with
`SPIRAM_TRY_ALLOCATE_WIFI_LWIP`, `-Os`, nano printf, 240 MHz, IPv6 and
SoftAP off. The HID builds add the IDF's own `esp_hid_host` example
(`examples/bluetooth/esp_hid_host`) on top. Static internal RAM is read the
way CLAUDE.md measures the S3: `.dram0.heap_start - 0x3FC88000`, which
counts IRAM too because they share the SRAM.

| Build | Image | Static internal | IRAM text | DRAM data | DRAM bss |
|---|---|---|---|---|---|
| Baseline (Wi-Fi + ESP-NOW) | 610,368 | 101,168 | 82,335 | 20,068 | 14,024 |
| + NimBLE HID host (1 connection, central and observer only, bonds in NVS) | 788,480 (**+178,112**) | 125,768 (**+24,600**) | +15,788 | +2,100 | +6,632 |
| + Bluedroid HID host (BLE only, GATT client, dynamic env memory, SPIRAM first) | 1,027,856 (**+417,488**) | 120,584 (**+19,416**) | +15,484 | +2,068 | +1,736 |
| + USB HID host (`usb_host_hid` 1.2.1) | 665,456 (**+55,088**) | 102,880 (**+1,712**) | +1,440 | +52 | +128 |

Where the NimBLE growth goes, from the two map files: the controller
(`libbtdm_app.a`) 61.0 KB flash, **14.3 KB IRAM** and 1.1 KB DRAM; the
NimBLE host (`libbt.a`) 70.6 KB flash and 5.9 KB DRAM; `esp_hid` 12.0 KB;
coexistence 3.7 KB flash and 1.4 KB DRAM; the example's scan and print code
most of the rest, which a real implementation replaces with its own of
similar size. The controller's 14.3 KB of IRAM has no "run from flash"
option in 5.3.1 for the S3 (`components/bt/controller/esp32s3/Kconfig.in`).

**NimBLE, not Bluedroid.** Bluedroid costs 2.3x the flash for 5 KB less
static RAM, and that 5 KB is only because its dynamic-memory options move
host statics onto the heap, where they are paid at run time instead.
Espressif's own guidance is the same: "ESP-NimBLE requires less heap and
flash size" (guide above).

Two things about the IDF example that matter to anyone building on it:

- **The NimBLE path of `esp_hid_host` does not compile as shipped in
  5.3.1**: `esp_hid_host_main.c` uses `esp_bd_addr_t` and
  `esp_bt_dev_get_address`, which only Bluedroid defines. Three lines had
  to come out of the scratch copy. It is the less-travelled path.
- **The example cannot reconnect a bonded keyboard**, on either stack. It
  scans and keeps only results that advertise a HID appearance, and a
  bonded keyboard reconnecting advertises with appearance 0x0000 or
  directed at the host, so the scan finds nothing
  ([#15379](https://github.com/espressif/esp-idf/issues/15379), the
  reporter's own diagnosis). A real implementation connects straight to
  the bonded address (an accept-list initiator), the way desktop stacks do
  ([BlueZ #2594](https://github.com/bluez/bluez/issues/2594)).

Checked and **not** a problem in 5.3.1: the NimBLE HID host that never reads
the report map ([#19011](https://github.com/espressif/esp-idf/issues/19011))
is a v5.5+ regression; 5.3.1's `read_device_services` has the HID branch in
its `else` (`components/esp_hid/src/nimble_hidh.c:366-420`).

### What the board has, and what BLE takes from it

S3 static internal RAM free, from CLAUDE.md (link.16, 2026-09-29) and the
G4848's tty-ux report (2026-10-01), and what is left after NimBLE's
measured 24,600 bytes:

| Board | Static free now | After NimBLE | Internal heap low on the bench | After NimBLE, before its run-time share (estimate) |
|---|---|---|---|---|
| WS43B (4.3B) | 82,952 | 58,352 | 50.7 KB (2026-09-28) | ~26 KB |
| MF35 v1 | 86,048 | 61,448 | 56,943 (1.1.1) | ~32 KB |
| G4848 | 74,104 | 49,504 | not recorded | unknown |
| S3 stick | 85,560 | 60,960 | 49,439 (1.1.2, 5 callers) | ~25 KB (not a target) |

The run-time share on top, internal RAM:

- controller task stack 3.5 KB (`ESP_TASK_BT_CONTROLLER_STACK`), NimBLE
  host task 4 KB (`BT_NIMBLE_HOST_TASK_STACK_SIZE`), the `esp_hidh` event
  task 4 KB in the example: **11.5 KB of stacks**;
- the controller's own buffers, allocated at `esp_bt_controller_init` with
  `MALLOC_CAP_INTERNAL` (`controller/esp32c3/bt.c:688`, shared by the S3).
  **Nobody publishes an S3 figure**; Espressif's 40 KB "BLE single mode"
  figure is the classic ESP32's ([ESP-FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/bt/ble.html)). Bench item 1;
- the NimBLE host's pools, about 20 KB at the defaults (msys 12 x 256 and
  24 x 320, ACL 24 x 255, events 38 x 70, from the probe's sdkconfig).
  **These can live in PSRAM**: `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`
  exists in 5.3.1 and needs only `SPIRAM_USE_MALLOC`, which the S3 boards
  have (`components/bt/porting/mem/bt_osi_mem.c:15`). Bluedroid's
  equivalent is `CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST`.

So with the pools in PSRAM, the 4.3B lands at roughly 26 KB minus 11.5 KB
minus the controller's buffers: **at or under its 16 KB
`BBS_HEAP_RESERVE`**, where plugins are refused at start and an SSH login's
16 KB task would not fit. That is an estimate, and it is close enough to
the line that it decides the order of work: **move the session pool to
PSRAM first** (`EXT_RAM_BSS_ATTR` on the pool's own declaration,
`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`; twelve sessions at about
7 KB each is about 84 KB of internal RAM back, estimate), which is already
queued for the camera boards in 1.3.0 and has to be measured with callers
on against SYS's loop figures. After that the BLE stack fits with room.

## 3. Coexistence with Wi-Fi and ESP-NOW: the Rule no. 1 question

The S3 has one 2.4 GHz radio and shares it by time division. From
Espressif's 5.3.1 coexistence guide
([coexist.rst at v5.3.1](https://github.com/espressif/esp-idf/blob/v5.3.1/docs/en/api-guides/coexist.rst)):

- Every Wi-Fi station state works beside every BLE state ("Y: supported
  and performance is stable"). ESP-NOW RX is "S": stable in STA mode only,
  which is what the boards run. So it is supported, not exotic.
- "CONNECTED status: the coexistence period starts at the Target Beacon
  Transmission Time (TBTT) and is more than 100 ms." With Wi-Fi connected
  and BLE connected, "the time slices of Wi-Fi and BLE in a coexistence
  period each account for 50%."
- And from the Wi-Fi guide
  ([wifi.rst at v5.3.1](https://github.com/espressif/esp-idf/blob/v5.3.1/docs/en/api-guides/wifi.rst)):
  "in coexist mode, Wi-Fi will remain active only during Wi-Fi time slice,
  and sleep during non Wi-Fi time slice even if
  `esp_wifi_set_ps(WIFI_PS_NONE)` is called."

What that means for callers who are nowhere near the keyboard (estimate,
from the slice arithmetic, not measured):

- **Latency.** A frame for the board that arrives in the BLE half waits in
  the access point until the Wi-Fi half: up to about 50 ms, about 13 ms on
  average. The 1.1.2 bench measured keystroke echo at p50 4 ms and p95
  12 ms on the S3; with a keyboard connected, p95 plausibly moves to about
  50-60 ms. Not a stall, but a measurable step backwards on the one figure
  Rule no. 1 cares about most, and for every caller.
- **Airtime.** About half. Telnet does not notice. YMODEM and ZMODEM
  downloads, and the link's bulk pictures at 24 Mbps, do.
- **Scanning is worse than connected.** A scan takes long windows; a BLE
  scan at 99% duty on a C3 drove pings over 500 ms
  ([report](https://github.com/koosc/esp32-meshcore-bluetooth-websocket-bridge/issues/4)).
  Pairing scans should be short and only on request; reconnect scans low
  duty.
- **The loop is not touched.** The BLE tasks run on core 0 with Wi-Fi
  (`BT_CTRL_PINNED_TO_CORE_0`, `BT_NIMBLE_PINNED_TO_CORE_0`), at priorities
  `ESP_TASK_PRIO_MAX - 2` and `configMAX_PRIORITIES - 4`. Espressif suggests
  putting Bluetooth on the other core from Wi-Fi; here that core is the
  BBS loop's, and a task at priority 21 there would preempt it. A keyboard
  is a few reports a second, so core 0 is the right place and SYS's loop
  figures should not move.

**Compiled in is a cost even when never used.** `ESP_COEX_SW_COEXIST_ENABLE`
"select[s] ESP_WIFI_STA_DISCONNECTED_PM_ENABLE"
(`components/esp_coex/Kconfig:15`). The probe's defaults said `=n`, the
board's own value since 0.21.1, and the generated `sdkconfig.nimble` says
`=y`. Half of the 0.21.1 radio-asleep fix is gone in any image that
carries Bluetooth; the other half (re-asserting `WIFI_PS_NONE` on the right
events, main.cpp) still stands. Bluetooth must therefore be a board-profile
choice, never in a shared layer.

**Can BLE be quiet once paired?** Scanning, yes: nothing scans while
connected. The connection itself keeps the 50/50 scheme for as long as it
lasts, and a keyboard keeps its connection while awake. The coexistence
arbiter is switched on and off with the controller: `coex_enable()` in
`esp_bt_controller_enable` and `coex_disable()` in `esp_bt_controller_disable`
(`controller/esp32c3/bt.c:1552, 1639`). So the honest way to keep callers
whole is **the radio on only while somebody is at the keyboard**: a tap on
the glass (or the 1.2.2 hamburger's Keyboard entry) enables the controller
and connects to the bonded keyboard; N minutes with no key, the console
locks, disconnects and disables the controller. Two things to bench before
relying on it: that Wi-Fi returns to full time and to `WIFI_PS_NONE` after
the disable (re-assert it), and that disable and enable cycle cleanly
without a full deinit (deinit and reinit of NimBLE HID crashed on 5.5,
[#17493](https://github.com/espressif/esp-idf/issues/17493)).

## 4. Pairing on a board that has no keyboard yet

The glass is the way in, which is right on security grounds too: pairing
should need somebody standing at the board.

- **Start:** "Pair a keyboard" in the 1.2.2 hamburger menu (every target
  board has touch), or CONFIG console from a sysop session, which shows the
  same steps and the passkey on the panel.
- **Find:** a short scan (20-30 s, then off) listing devices that advertise
  the HID service (0x1812), the keyboard appearance (0x03C1) or just a
  name; the filter has to be that loose because real keyboards advertise
  only some of those (the Keychron project above). Tap one.
- **Pair with a passkey:** the board as `DISPLAY_ONLY` with MITM and
  Secure Connections on (`ble_hs_cfg.sm_io_cap`, `sm_mitm`, `sm_sc`). A
  keyboard is `KEYBOARD_ONLY`, so LE Passkey Entry applies: the panel shows
  six digits, the user types them on the keyboard and presses Enter. That
  proves the keyboard in front of the user is the one being bonded. A
  keyboard that only offers Just Works (ZMK's default) still pairs; the
  glass then asks for a tap to confirm, so the gate stays physical.
- **Bond:** NimBLE keeps keys in NVS with `CONFIG_BT_NIMBLE_NVS_PERSIST`
  (off by default; on in the probe), `MAX_BONDS` 2 is enough (one keyboard
  plus a spare). The S3 layout's `nvs` partition is 24 KB and holds this
  easily. **The BOOT-hold factory reset and CONFIG's "forget keyboard" must
  erase the bond namespace too**, or a board passed on keeps trusting the
  old owner's keyboard.
- **Reconnect at boot or after the keyboard sleeps:** an accept-list
  connect to the bonded address, low duty, and only while the console is
  wanted (section 3). Keyboards sleep and drop the link after idle; the
  first key after that is usually lost while it reconnects, which is
  ordinary BLE keyboard behaviour on any host.
- **Layout:** HID sends key positions, not characters. US layout first
  (about 1 KB of tables with modifiers, arrows as `ESC [ A`-`D`, F1-F4 as
  `ESC O P`-`S`, which the board's ANSI decoder already takes), others as
  tables later. Keyboards that send an NKRO bitmap instead of the 8-byte
  boot layout need the report map parsed; `esp_hidh` hands over the map.

## 5. The terminal on the panel

**The session side costs almost nothing, because the board already makes
ANSI.** A console session is an ordinary session whose Term is set, not
detected: `term.setType(TermType::Ansi, Charset::Cp437, 80, 24)` and
`setIacEscape(false)` (term.h:104, 107). Its output, instead of `send()`,
goes to a small ANSI interpreter that writes a cell grid (character plus
colour and attributes). No telnet, no probe, no backpressure worth the name:
the grid takes bytes as fast as the loop gives them, and pixels are drawn
from dirty cells on the panel's own tick, the way the panel already sends
one band a tick.

**The ANSI subset is small and known.** What the board emits (term.cpp and
the stock screens, counted for this report): CSI H, f, J, K, A, B, C, D,
`?25h`/`?25l`, and SGR 0, 1, 5, 7 and 30-37; the stock `.ans` screens use 13
distinct CP437 bytes (box lines, double lines, blocks). A sysop's own art
adds 40-47 backgrounds and `s`/`u`; take those too. That is an ANSI.SYS-level
parser, about 300-500 lines.

**Fonts.** Spleen's 8x16 has full CP437 since 2.0; the 6x12 has only Basic
Latin and Latin-1, no box drawing ([Spleen](https://github.com/fcambus/spleen)).
`tools/mkfont.py` extracts 96 glyphs today. Full CP437 at 8x16 is 4 KB; at
6x12 the box and block characters are best drawn by rule (each is four arms,
single or double, plus three shades and four half blocks), a few hundred
bytes of table.

**Per panel.** Character height uses each panel's pixel pitch, computed
from its diagonal.

| Board | Glass | Grid | Cell | Character height | How |
|---|---|---|---|---|---|
| WS43B (4.3B) | 800 x 480 | **80 x 24** (of 100 x 30) | 8 x 16 CP437 | 1.9 mm | a text-mode bounce fill, below |
| G4848 | 480 x 480 | **80 x 24**, 192 px left below for the status band | 6 x 12 + drawn box glyphs | 1.8 mm | into its PSRAM framebuffer |
| MF35 v1 and v2 | 480 x 320 | **80 x 24** (+2 status rows) | 6 x 12 + drawn box glyphs | 1.85 mm | into its PSRAM framebuffer |
| WS2 | 240 x 320 | 40 x 26 | 6 x 12 | 1.5 mm | **no**: 2", too small (Rob: larger only) |
| 1.47 stick | 172 x 320 | | | | **no**: no touch, too small |

1.9 mm is small: a 13" 1080p laptop's terminal text is about 2.4 mm. It is
readable at arm's length, and 60 x 30 at 8x16 on the square and 3.5"
glasses would be larger but is not a width the board's screens are drawn
for (40 or 80).

**The 4.3B, and something nobody has considered.** Its panel today draws a
400 x 240 picture doubled to the glass by an interrupt on core 1 that reads
1,600 bytes of PSRAM per 205 us bounce (platform_esp32_rgb.cpp). At that
picture size there is no 80 x 24: 8x16 gives 50 x 15. Rather than a 768 KB
native framebuffer (four times the PSRAM traffic, which that file was
designed to avoid), **swap the bounce fill while the console is up for a
text-mode fill**: compose each four-line bounce straight from the cell grid
and the font, at native 800 x 480, the way a CGA card did. It writes the
same 6,400 bytes per bounce as today's fill and reads less (a few hundred
bytes of cells and glyph rows instead of 1,600 bytes of PSRAM), so its
interrupt cost should be the same or lower (estimate; SYS's loop average
is how to check, idle was 881 us of work on the bench). The status picture
is untouched, so leaving the console is one pointer back.

**Drawing cost elsewhere** (estimate): a full 80 x 24 redraw at 6x12 is
138,240 pixels, about 276 KB of PSRAM writes, a few milliseconds of CPU.
Budgeted to about 2 ms a pass it is three or four passes, never one slow
pass, and only a CLS or a full screen ever needs all of it.

**Code size** (estimate): interpreter, grid and dirty tracking 6-10 KB;
fonts +5 KB; keymap 1 KB; the console mode and pairing screens on the panel
3-5 KB; the core's console glue 2-3 KB, most of it shared with 1.2.3.
**About 15-25 KB of flash. RAM: the grid is 3,840 bytes at 80 x 24 (6,000
for the 4.3B's full 100 x 30), and can sit in PSRAM.**

## 6. The console, the sysop node and logging in (the coordinator's addition)

**On the sysop node, as Rob said, and it works today with one change.**
The console is a session in `sysop_` (id 0, `Role::Sysop`), with `ip` set
to a label such as `console` and counted as local by `localAddr`, so the
first-boot setup works from it too.

**Trusted, or still elevate? Recommend: ask for the sysop password once,
then trust until the console locks.** The person at the glass is physical
access, and Rob has already ruled physical access to be full trust for
Shut down; anyone at the USB port can also erase and reflash the board.
But a BLE keyboard is not physical access to the board: it works through a
wall at about 10 m, and a keyboard is easy to walk off with. So:

- the console opens at a single `Sysop password:` (no handle), the same
  `staffPassword` check BYE uses;
- three wrong answers lock the console for 15 minutes and are logged, the
  console's equivalent of the address ban (it has no address to ban);
- right once, it stays the sysop until it locks: no key for N minutes
  (default 10), `BYE`, or a tap on the glass's lock;
- a CONFIG console switch "Trust the paired keyboard" (default **no**)
  skips the question, for a board in a locked room.

**Both at once: the sysop node holds one caller, and `elevate()` already
says what happens** (bbs_sysop.cpp:362-372). With the console on the node:

- a sysop on the LAN who types `BYE <password>` gets sysop rights in place
  on their own caller line (`coElevate`), visible in WHO. That works, at
  the cost of one caller line while they are on;
- a sysop **from outside** is logged off with no reason given. That is the
  case to fix: (1) a locked console gives the node up, so an idle console
  never blocks remote administration; (2) while it is in use, the remote
  sysop is told "The sysop node is in use at the board's console" instead
  of a bare goodbye. One message and one release, a few lines in
  `elevate()` and the console's lock.

**A dedicated console line is possible on these boards, and is a second
phase, not the first.** It is one more `Session` (about 7 KB), which on an
S3 can live in PSRAM with `EXT_RAM_BSS_ATTR` on its own declaration and
cost no internal RAM. The id fits the 16-bit traffic masks
(`static_assert(BBS_MAX_NODES + 2 <= 16)` would become `+ 3 <= 16`). The
cost is the code that walks sessions by id: WHO, NODES, DASH, the caller
log's node field, `ringLimits_`, claims, the panel's lists. Worth it only
if the console and a remote sysop session are wanted together routinely.

## 7. One machinery with the 1.2.3 serial line

They are the same thing: **a session whose bytes do not come from a
socket**. The SSH preview already proved the seam: `Session::link`
replaces the socket, and `sessSend`, `readSession` (`sshRead`),
`closeSession`, SNOOP's mirror, `moveSession` and `wantWrite` know it
(bbs.cpp:114-147, 1117-1120; bbs_ssh.cpp:293-316). 1.2.3's "caller-line
core" should generalise that seam once, as a line kind with three
operations, rather than adding a second `#if` beside SSH's:

| Line kind | Read (loop, every pass) | Write (loop) | Wakes the loop | Detect | Trust | Node |
|---|---|---|---|---|---|---|
| Socket (today) | `recv` | `send` | select on the socket | telnet + probe | by address | caller 1-10, sysop |
| SSH (1.1.2) | ring from the SSH task | ring to the SSH task | eventfd | probe, ANSI only | by address | caller 1-10, sysop |
| Serial line (1.2.3) | `uart_read_bytes(..., 0)` | `uart_write_bytes` within `uart_get_tx_buffer_free_size` | none needed: the loop's 10 ms select timeout (`BBS_SELECT_MS`) is the poll | probe, no telnet | CONFIG serial's (a modem on it is remote) | one caller node |
| CALLIN over the link (1.2.3, 1.3.0 ham) | the link family's DATA into a ring | DATA frames | the link plugin's tick | probe, or the sat says | the sat's pairing | one caller node |
| Console (this) | a ring the HID or sat input fills | the panel's ANSI interpreter, directly | none: polled each pass | none: set | sysop password once | sysop node |

The two things that make it one design: the telnet filter belongs to the
socket kind only (the others take bytes raw), and trust belongs to the line
kind, never to anything the far end says (LINK.md's rule for serial
already: "The trust belongs to the transport, not to the frame").

**The input side of the console does not care where keys come from.** A
BLE keyboard, a USB keyboard and a keyboard on a sat all end as bytes in the
console's ring. That is why the order of work in section 9 can start with
the sat and add BLE later without touching the terminal.

## 8. The alternatives

### B. A USB keyboard on the S3's OTG port

Measured: **+55,088 bytes of image and +1,712 static internal** with
`usb_host_hid` 1.2.1; run time adds the USB library task (4 KB) and the HID
driver task (4 KB) and small DMA buffers (estimate). No radio at all, and
every USB keyboard ever made works, boot protocol included.

The catch is the port. The S3's USB-OTG and USB-Serial-JTAG share one
internal PHY, so only one runs at a time
([Espressif, USB PHY](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_phy.html)):

- **4.3B, WS2, ETH, the stick:** the native USB is the only USB, and it is
  the console, Improv and the installer's Update. Host mode takes all three
  away while it runs. No.
- **G4848:** GPIO 19 and 20 are the touch controller's SDA and the panel's
  G1 (board.h, board-g4848). No.
- **MF35 v1 and v2:** a second USB-C wired to the chip's own USB (19, 20)
  beside the CP2104. **Yes, in principle**: the console stays on UART0 (the
  v2 profile currently puts it on native USB because of how the bench board
  is cabled, and v1 runs a second console there that would go). Unverified:
  whether that connector can supply 5 V to a keyboard. Read the schematic's
  VBUS net before promising it.

### C. A keyboard on a sat (the strongest option)

1.2.3 already plans CALLIN over the link and a terminal sat ("a console
could be hooked to a sat OR the board"). A **keyboard sat** is that sat
with a keyboard instead of a VT220, whose session says "render on the
board's panel". What it buys:

- **No Bluetooth on the board**: no 178 KB, no 24.6 KB of internal RAM, no
  coexistence, no forced `DISCONNECTED_PM`. ESP-NOW is already running and a
  keystroke is a tiny control frame (150-184 us each on the loop, a few a
  second; LINK.md's own bench).
- **More keyboards, not fewer.** A classic ESP32 sat runs Bluedroid in dual
  mode, so it pairs Classic keyboards (the K380) as well as BLE ones; an S3
  sat takes USB; anything takes PS/2 on two pins. And a coexistence hit on
  the sat hurts nobody.
- **It is the same work as 1.2.3**, not new work: the CALLIN family, the
  stream line, and a flag on the session for where its output goes.

Its costs: a second box and its power; the board must be on Wi-Fi for
ESP-NOW (the wired ETH board cannot pair sats until the 1.2.1 change, and it
has no panel anyway); pairing through CONFIG sats as for any sat.

## 9. Verdict and phases

**Build it, in this order:**

1. **With 1.2.3: the stream-line core** (section 7), with the console as one
   of its kinds on the sysop node, the sysop-password unlock and the
   console's lock and yield (section 6). Board cost about 2-3 KB of flash,
   negligible RAM.
2. **The panel terminal** on the G4848 and both Makerfabs (framebuffer
   boards, 80 x 24 at 6x12 with drawn box glyphs), fed by the **keyboard
   sat** over CALLIN. About 15-25 KB of flash per board, the grid in PSRAM.
   No new radio cost.
3. **The 4.3B's text-mode bounce fill** (80 x 24 at 8x16 CP437, the best
   glass of the lot). A few KB more, its interrupt to be compared with
   today's on SYS's loop figures.
4. **Only if Rob still wants direct pairing after 2 and 3: BLE, NimBLE,**
   behind a profile flag (`BBS_HAS_BLE_KBD` in the 4.3B, G4848 and MF35 v2
   blocks, or a `_kbd` env), never in a shared layer; **after the session
   pool is in PSRAM**; host pools in PSRAM
   (`BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`); controller on only while the
   console is awake. +178 KB flash, +24.6 KB static internal, plus
   11.5 KB of stacks and the controller's buffers while on.
5. **USB host on the Makerfabs**, optional, after the VBUS check: +55 KB
   flash, under 2 KB static.

**Per board, all phases:**

| Board | Phases | Flash | Internal RAM | Note |
|---|---|---|---|---|
| WS43B (4.3B) | 1, 3, (4) | ~20-30 KB; +178 KB with BLE | ~0; +24.6 KB static + run time with BLE | BLE only after the PSRAM session move: its heap low is the tightest |
| G4848 | 1, 2, (4) | ~20-30 KB; +178 KB with BLE | ~0; as above with BLE | heap figure not recorded yet; read it first |
| MF35 v2 | 1, 2, (4), (5) | as above; +55 KB with USB | as above | 8 MB octal PSRAM: the easiest home for BLE's pools |
| MF35 v1 | 1, 2, (5) | as above | as above | only 2 MB quad PSRAM; BLE not recommended |
| WS2, stick, ETH, every classic ESP32 | none | 0 | 0 | too small, no panel, or displays require an S3 |

## Bench checks owed (each needs Rob's OK as a test plan)

1. **The controller's internal heap on an S3.** The NimBLE probe built for
   this report logs `internal free after wifi` and `internal free after hid`;
   flashed to a spare S3 it answers the one figure nobody publishes. Read
   with the pools internal and again with `MEM_ALLOC_MODE_EXTERNAL` (the
   `nimext` env is in the same folder).
2. **Ping and echo latency with a keyboard connected**, against the
   controller disabled: p50 and p95 on the LAN, and SYS's slow passes.
   This is the number that decides whether direct BLE is acceptable at all.
3. **Controller disable and enable without deinit**, ten cycles, with
   `WIFI_PS_NONE` re-read from `esp_wifi_get_ps` after each.
4. **Real keyboards**: which of the ones on Rob's desk advertise HOGP.
5. **The Makerfabs native USB-C's VBUS** (schematic first).

## What was built, and where

Scratch only, not in the repository:
`%TEMP%\claude\...\scratchpad\blehid\` (envs `base`, `nimble`, `nimext`,
`bluedroid`: the IDF's `esp_hid_host` example on a Wi-Fi + ESP-NOW
baseline, three lines removed from its NimBLE path so it compiles) and
`...\scratchpad\usbprobe\` (the IDF's USB HID host example on the same
baseline). Built with PlatformIO `espressif32@6.9.0` for
`esp32-s3-devkitc-1`; sizes read with `xtensa-esp32s3-elf-size -A` and the
map files. Nothing was flashed.

## Sources

- [ESP-IDF 5.3.1, Bluetooth overview, ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/bluetooth.html)
- [ESP-IDF 5.3.1, RF coexistence (coexist.rst)](https://github.com/espressif/esp-idf/blob/v5.3.1/docs/en/api-guides/coexist.rst)
- [ESP-IDF 5.3.1, Wi-Fi driver (wifi.rst), modem sleep in coexistence](https://github.com/espressif/esp-idf/blob/v5.3.1/docs/en/api-guides/wifi.rst)
- [ESP-FAQ, Bluetooth LE memory](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/bt/ble.html)
- [ESP-FAQ, coexistence](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/coexistence.html)
- [Espressif, USB PHY on the ESP32-S3](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_phy.html)
- [esp-idf #15379, BLE HID reconnect fails with the example](https://github.com/espressif/esp-idf/issues/15379)
- [esp-idf #19011, NimBLE HID host report map (5.5+)](https://github.com/espressif/esp-idf/issues/19011)
- [esp-idf #17493, NimBLE HID deinit and reinit](https://github.com/espressif/esp-idf/issues/17493)
- [hid-proxy-for-ble-keyboard (IDF 5.3.1, Keychron K2 HE)](https://github.com/anisehid/hid-proxy-for-ble-keyboard)
- [BlueZ #2594, accept-list reconnect of bonded LE HID](https://github.com/bluez/bluez/issues/2594)
- [BLE scan duty and Wi-Fi starvation on a C3](https://github.com/koosc/esp32-meshcore-bluetooth-websocket-bridge/issues/4)
- [ZMK, Bluetooth](https://zmk.dev/docs/features/bluetooth)
- [Logitech K380 vs MX Keys Mini, LTT Labs](https://www.lttlabs.com/compare/logitech-k380-vs-logitech-mx-keys-mini) and [RTINGS](https://www.rtings.com/keyboard/tools/compare/logitech-mx-keys-mini-logitech-k380/28677/1812)
- [Spleen font coverage](https://github.com/fcambus/spleen)
- IDF tree on disk: `components/esp_coex/Kconfig:15`, `components/bt/controller/esp32c3/bt.c:688, 1552, 1639`, `components/bt/porting/mem/bt_osi_mem.c:15`, `components/esp_hid/src/nimble_hidh.c:366-420`, `components/esp_system/include/esp_task.h:32, 40`, `components/bt/host/nimble/nimble/porting/npl/freertos/src/nimble_port_freertos.c:43`
