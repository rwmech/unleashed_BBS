# Waveshare ESP32-S3-LCD-1.47: builder's reference

2026-09-24. Web research only. Nothing was connected to the board, a COM port
or the network. Written for the first S3 environment queued in CLAUDE.md
("First S3: Waveshare ESP32-S3-LCD-1.47").

How to read this:

- Every claim carries a source tag. The tags are links, listed at the end.
- UNCONFIRMED means no primary source established it. Nothing was filled in
  from memory.
- [SCH] is Waveshare's schematic PDF. It was read as text (net list) and as a
  rendered page, and it is the authority for every pin below.
- "Mirror" is a third-party GitHub copy of Waveshare's ESP-IDF demo
  ([MIRROR]). Waveshare's own demo zip ([DEMO]) is over the 10 MB fetch
  limit and was not read, so anything taken from the mirror is one step
  removed from Waveshare. The mirror's author says the LCD, LVGL and SD
  drivers are Waveshare's and that they changed the RGB and SD code.
- Buy link Rob gave: `https://link.amazon/B0bb1oJqt`. Noted, not fetched.

## Which board this is

Three Waveshare boards share the name, and two of them differ on pins.

- ESP32-S3-LCD-1.47 (SKU 28317): USB Type-A plug, TF slot, RGB LED
  [WIKI] [PROD]. This report is for this one.
- ESP32-S3-LCD-1.47B: USB-C, backlight on GPIO46 not 48, adds a QMI8658 IMU
  and a Li-ion charger [WIKI-B].
- ESP32-S3-Touch-LCD-1.47: a touch variant, not researched.

A USB-stick board with a USB-A plug is the first one. If Rob's has USB-C,
stop and re-check: the backlight pin is different.

## Pin table

Chip pin numbers are QFN56 pins from [DS] Table 2-4. "Pull" is a resistor on
the board, from [SCH].

| GPIO | Chip pin | Board use | Detail | Sources |
|---|---|---|---|---|
| 0 | 5 | BOOT button (Key2) | 10 kΩ pull-up (R2) to 3V3, button to GND. Strapping pin | [SCH] [DS] 3.1 |
| 1 | 6 | Header U2 pin 4 | free; ADC1_CH0, TOUCH1 | [SCH] [DS] T2-8 |
| 2 | 7 | Header U2 pin 5 | free; ADC1_CH1 | [SCH] |
| 3 | 8 | Header U2 pin 6 | free, but a strapping pin (JTAG source), floating at reset | [SCH] [DS] 3.4 |
| 4-10 | 9-15 | Header U2 pins 7-13 | free; ADC1_CH3..CH9 | [SCH] [DS] T2-8 |
| 11-13 | 16-18 | Header U2 pins 14-16 | free; ADC2 (not usable as ADC with Wi-Fi on) | [SCH] [DS] 4.2.2.1 |
| 14 | 19 | TF CLK | net SD_SCLK / SDIO_SCK, 10 kΩ pull-up R15 | [SCH] [WIKI] |
| 15 | 21 (XTAL_32K_P) | TF CMD | net SD_MOSI / SDIO_CMD, 10 kΩ R14 | [SCH] [WIKI] |
| 16 | 22 (XTAL_32K_N) | TF D0 | net SD_MISO / SDIO_D0, 10 kΩ R16 | [SCH] [WIKI] |
| 17 | 23 | TF D2 | via R11 0 Ω, 10 kΩ R12 | [SCH] [WIKI] |
| 18 | 24 | TF D1 | via R18 0 Ω, 10 kΩ R17 | [SCH] [WIKI] |
| 19 | 25 | USB D- | via R9 22 Ω to USB-A pin 2 | [SCH] [DS] T2-8 |
| 20 | 26 | USB D+ | via R10 22 Ω to USB-A pin 3 | [SCH] [DS] T2-8 |
| 21 | 27 | TF D3/CD | net SD_CS / SDIO_D3, 10 kΩ R13 | [SCH] [WIKI] |
| 22-25 | - | do not exist on the S3 | | [DS] T2-4 |
| 26-32 | 28, 30-35 | flash (W25Q128, CS0) and in-package PSRAM (CS1) bus | never use | [SCH] [DS] T2-14 |
| 33-37 | 38-42 | octal PSRAM DQ4-DQ7 and DQS | never use; drawn no-connect | [SCH] [DS] T2-14 |
| 38 | 43 | RGB LED data | WS2812B-0807 DI, 10 kΩ pull-up R19 to 3V3 | [SCH] [WIKI] |
| 39 | 44 (MTCK) | LCD RES | | [SCH] [WIKI] |
| 40 | 45 (MTDO) | LCD SCL (clock) | | [SCH] [WIKI] |
| 41 | 47 (MTDI) | LCD D/C | | [SCH] [WIKI] |
| 42 | 48 (MTMS) | LCD CS | | [SCH] [WIKI] |
| 43 | 49 (U0TXD) | Header U2 pin 18 "TXD" | through R7 499 Ω | [SCH] |
| 44 | 50 (U0RXD) | Header U2 pin 17 "RXD" | direct | [SCH] |
| 45 | 51 | LCD SDA (MOSI) | strapping pin (VDD_SPI voltage) | [SCH] [WIKI] [DS] 3.2 |
| 46 | 52 | net IO46 on the chip pin only | not broken out; strapping pin | [SCH] |
| 47 | 37 (SPICLK_P) | net IO47 on the chip pin only | not broken out | [SCH] |
| 48 | 36 (SPICLK_N) | LCD backlight | R6 1 kΩ to gate of Q1 (SI2302 N-MOSFET) | [SCH] [WIKI] |
| CHIP_PU | 4 | RESET button (Key1) | 10 kΩ pull-up R1, 1 µF C16 | [SCH] |

Header U2 on the schematic: pin 1 5V (VBUS), 2 GND, 3 3V3, 4-16 IO1-IO13,
17 RXD, 18 TXD [SCH]. The symbol is named "22 - 1.47_LCD Pin" but has 18
pins. The physical order on the board edge is UNCONFIRMED: check the
silkscreen. The header ships loose, not soldered [PROD].

## 1. Chip, flash and PSRAM

- It is not a module. The board carries a bare ESP32-S3R8 (U3, QFN56), an
  external Winbond W25Q128JVSI flash (U4), a 40 MHz crystal (Y1), an
  ME6217C33M5G LDO (U1) and a ceramic antenna [SCH] [PROD]. The LDO is 800 mA
  maximum [PROD].
- ESP32-S3R8: no in-package flash, 8 MB Octal SPI PSRAM in the package,
  VDD_SPI 3.3 V [DS] Table 1-1.
- Temperature, worth knowing for a board in a case behind a backlit LCD: the
  R8 is rated to 65 °C ambient. With PSRAM ECC on it goes to 85 °C and loses
  1/16 of the PSRAM [DS] Table 1-1 note 3.
- Flash: 16 MB, the W25Q128JVSI on VDD_SPI [SCH] [WIKI].
  - This is IDF's "F4R8" arrangement, quad flash plus octal PSRAM, which IDF
    5.3.1 supports; quad flash is STR only [IDF-FP].
  - The demo (mirror sdkconfig, IDF 5.1.2) runs it DIO at 80 MHz, 16MB
    [MIRROR-CFG].
  - QIO on this board: UNCONFIRMED. The Winbond datasheet was not fetched.
    Start with DIO, which is what the demo uses, and try QIO as a measured
    step.
- PSRAM mode is octal: `CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_SPIRAM_SPEED_80M=y`
  in the demo, CS on GPIO26 and clock on GPIO30 [MIRROR-CFG].
- GPIOs firmware must never hand out:
  - 26-32: SPICS1 (PSRAM CS) 26, SPIHD 27, SPIWP 28, SPICS0 (flash CS) 29,
    SPICLK 30, SPIQ 31, SPID 32. Flash and PSRAM share the data lines
    [DS] Table 2-14.
  - 33-37: DQ4-DQ7 and DQS in octal mode [DS] Table 2-14.
  - The datasheet says "Do not use the pins connected to in-package
    flash/PSRAM for any other purposes" [DS] 2.6.
  - GPIO47 and 48 are ordinary 3.3 V pins on the R8. Only the 1.8 V parts
    (R8V, R16V) run them at 1.8 V [DS] Table 2-1 note 4.
  - 19 and 20 are USB, and on this board they are the only way in (section
    5).
- For `syscfg::pinProblem` on this board:
  - Refuse outright: 19, 20, 26-37, and 22-25 (not a GPIO on the S3).
  - Board-owned, refuse unless the setting is the board's own use: 0 (BOOT),
    14-18 and 21 (TF), 38 (RGB), 39-42, 45 and 48 (LCD).
  - GPIO6-11, refused on the WROOM as flash pins, are free header pins here.
    This is the per-chip list the queue item already calls for.

## 2. LCD

- The panel is module LBS147TC-IF15 [LCD-DS]:
  - 172(H) x 320(V), 4-line SPI, 262K colours, "Normally black" IPS, RGB
    vertical stripe.
  - Backlight: two white LEDs in parallel, 40 mA typical, 60 mA maximum,
    2.8-3.2 V.
  - Serial write clock cycle is 16 ns minimum, which caps the write clock
    at 62.5 MHz.
- Controller: the datasheet's feature table says ST7789V3 [LCD-DS] p4.
  - Disagreement: its mechanical drawing says "STV7789V3/GC9307N"
    [LCD-DS] p5. That reads as a second-source IC, so a board could carry a
    GC9307N. UNCONFIRMED which one Rob's has.
  - Every piece of Waveshare and third-party code found drives it as an
    ST7789.
- FPC disagreement: the datasheet shows an 8-pin FPC [LCD-DS] p6. The board
  schematic shows a 12-pin connector: GND, LEDK, LEDA, VDD, GND, GND, D/C, CS,
  SCL, SDA, RES, GND [SCH]. The panel fitted may be a variant of the one
  linked. UNCONFIRMED.
- Interface: 4-wire SPI, write only (no MISO).
  - Pins: SDA/MOSI 45, SCLK 40, CS 42, DC 41, RST 39, backlight 48.
  - Four sources agree: [WIKI] [SCH] [ESPP] [TFTESPI].
- Backlight circuit [SCH]:
  - LEDA is tied to 3V3.
  - LEDK goes through R3 10 Ω to Q1, an SI2302 N-MOSFET switching to ground.
  - GPIO48 drives the gate through R6 1 kΩ. The gate has 100 kΩ (R5) to 3V3
    and 10 kΩ (R8) to GND, so a floating GPIO48 leaves it at about 0.3 V and
    the backlight off.
  - Active high, and PWM-able. The demo runs LEDC at 5 kHz, 13-bit, low-speed
    mode, timer 0, channel 0, with a 0-100 brightness call [MIRROR-H]
    [MIRROR-C].
- SPI clock and host, three sources and a disagreement:
  - Demo: 12 MHz (`EXAMPLE_LCD_PIXEL_CLOCK_HZ (12 * 1000 * 1000)`) on
    `SPI3_HOST`. The comment above the define says "Using SPI2"; the code
    says SPI3 [MIRROR-H].
  - espp: 80 MHz on `SPI2_HOST` [ESPP].
  - TFT_eSPI working setup: 8 MHz with `USE_HSPI_PORT` [TFTESPI].
  - 80 MHz is above the panel's 16 ns write cycle (62.5 MHz) [LCD-DS] p8.
    Start at the demo's 12 MHz. The real ceiling on this board is
    UNCONFIRMED.
- Resolution and offsets:
  - Demo: portrait, H_RES 172, V_RES 320, `Offset_X 34`, `Offset_Y 0`. The
    offsets are added to the area in the LVGL flush callback before
    `esp_lcd_panel_draw_bitmap` [MIRROR-H] [MIRROR-LVGL].
  - espp: landscape 320 x 172, `lcd_offset_x = 0`, `lcd_offset_y = 34`
    [ESPP].
  - They agree: 34 on the 172-pixel axis, whichever way round. Arithmetic,
    not a source: (240 - 172) / 2 = 34.
- Colour order:
  - Demo `rgb_endian = LCD_RGB_ENDIAN_BGR`, which sets the MADCTL BGR bit
    [MIRROR-C] [MIRROR-VERNON].
  - TFT_eSPI `TFT_RGB_ORDER TFT_BGR` [TFTESPI].
  - espp `swap_color_order = false` [ESPP], but what its driver treats as
    unswapped is UNCONFIRMED.
  - Two sources that name it say BGR.
- Inversion: the demo's init table ends with 0x21 (INVON) then 0x29
  [MIRROR-VERNON]. espp has `invert_colors = false` [ESPP], with the same
  caveat about its driver's baseline. For a normally-black IPS panel, INVON
  is what the demo does.
- Demo init table [MIRROR-VERNON]:
  - 0x11 (SLPOUT); 0x36 (MADCTL) = 0x00; 0x3A = 0x55 (RGB565).
  - 0xB0 = {0x00, 0xE8}; 0xB2 porch = {0x0C, 0x0C, 0x00, 0x33, 0x33}.
  - B7, BB, C0, C2, C3, C4, C6, D0; gamma E0 and E1, 14 bytes each.
  - 0x21, 0x29.
  - Then `esp_lcd_panel_mirror(panel, true, false)` [MIRROR-C].
  - Rotation is handled in LVGL's driver-update callback with swap_xy and
    mirror [MIRROR-LVGL].
- Drivers:
  - The demo uses LVGL 8.3 (the wiki lists LVGL v8.3.10 [WIKI]; the mirror
    carries 8.3.11 [MIRROR]).
  - It uses its own copy of an esp_lcd panel driver ("Vernon_ST7789T",
    `esp_lcd_panel_dev_st7789t_config_t`), not IDF's [MIRROR-C].
  - IDF 5.3.1 has `esp_lcd_new_panel_st7789` with `rgb_ele_order` (the
    newer name for `rgb_endian`) and `bits_per_pixel` [IDF-LCD].
  - A status panel needs a plain esp_lcd path, not LVGL. LVGL's flash and
    RAM cost was not measured.

## 3. RGB LED

- It is one addressable pixel, WS2812B-0807 (LED1), not three plain GPIOs
  [SCH]:
  - DI on GPIO38 with a 10 kΩ pull-up to 3V3.
  - VDD from 3V3, not 5 V.
  - DO not connected.
  - The wiki says "RGB light bead ... GPIO38" [WIKI]; espp calls it a
    Neopixel [ESPP].
- Colour order is UNCONFIRMED, and the sources disagree:
  - The part is named WS2812B [SCH]. WS2812B parts are conventionally GRB,
    but the Worldsemi datasheet was not fetched.
  - The mirror's author reports Waveshare's stock code showed wrong colours.
    They "fixed" it with `led_strip_set_pixel(strip, 0, green_val, red_val,
    blue_val)` [MIRROR-RGB].
  - Their led_strip 2.5.5 writes the wire bytes as [green arg, red arg,
    blue arg] [MIRROR-LEDSTRIP]. So the call that works puts R, G, B on the
    wire in that order. That is RGB order, whatever the comment beside it
    says.
  - Bench test: send one pure-red frame as R,G,B bytes and see what lights.
  - Either way, the lights plugin needs a colour-order setting, per output or
    per board profile.
- RMT sizing: the lights code assumes the ESP32's 64-symbol blocks and needs
  a change for this part.
  - The S3's RMT has four TX channels, and all eight channels share a 384 x
    32-bit RAM, which is 48 symbols a block [DS] 4.2.1.11. IDF's S3 docs say
    a non-DMA channel's `mem_block_symbols` "should be at least 48" [IDF-RMT].
  - `platform_esp32.cpp:938` rounds to multiples of 64. IDF 5.3.1 then
    rounds up again to whole 48-symbol blocks, which must be contiguous and
    free [IDF-RMT-SRC].
  - The one-pixel drive light (26 symbols) therefore takes 2 blocks. The
    ten-pixel strip (242 symbols, rounded to 256) takes 6. That is all 8
    blocks the S3 has.
  - Whether that allocation succeeds is UNCONFIRMED. Two ways out: round to
    `SOC_RMT_MEM_WORDS_PER_CHANNEL`, which gives 1 + 6 blocks, or put the
    strip on DMA. The S3 has TX DMA on channel 3 [DS] 4.2.1.11.
  - Keep the whole-frame-in-channel-memory property the plugin was designed
    around.
- Do not point `activity_led_gpio` at 38: that pin is a WS2812 data line,
  not a lamp. The default, GPIO2, is a bare header pin on this board with
  nothing on it.

## 4. TF slot

- The socket is TF1 (H2.8 MUP M617-2). Pins [SCH]:

  | Socket pin | Signal | Net |
  |---|---|---|
  | 1 | D2 | SDIO_D2 |
  | 2 | CD/D3 | SDIO_D3 |
  | 3 | CMD | SDIO_CMD |
  | 4 | VDD | 3V3 |
  | 5 | CLK | SDIO_SCK |
  | 6 | VSS | GND |
  | 7 | D0 | SDIO_D0 |
  | 8 | D1 | SDIO_D1 |
  | 9 | CD1 | not connected |
  | 10 | CD2 | not connected |
  | 11 | shell | GND |

- Wiring: a full 4-bit SD bus. CLK 14, CMD 15, D0 16, D1 18, D2 17, D3 21.
  All six lines have 10 kΩ pull-ups to 3V3 (R12-R17) [SCH]. The wiki pin
  table agrees [WIKI], and so does espp [ESPP].
- SPI mode works, and the schematic's own net names are SPI names: SD_SCLK =
  IO14, SD_MOSI = IO15, SD_MISO = IO16, SD_CS = IO21 [SCH].
  - That is exactly CS = D3 (21), MOSI = CMD (15), SCK = CLK (14),
    MISO = D0 (16).
  - D1 and D2 stay pulled up by the board.
  - For the `sd` plugin this is a config change, on an SPI host the LCD is
    not using.
- Card detect: none. The socket's CD1 and CD2 contacts are drawn
  unconnected [SCH]. There is still no insertion event on this board.
- No TF line is shared with the LCD. The LCD uses 39-42, 45 and 48 [SCH].
- SDMMC is also open on the S3, and the reason the WROOM build chose SPI does
  not apply here:
  - On the S3, SDMMC signals go through the GPIO matrix, so "any GPIO may be
    used for each of the SD card signals".
  - It has 1- and 4-line modes at 20 or 40 MHz, and needs external pull-ups
    [IDF-SDMMC], which this board has.
  - SDMMC 4-bit would leave both general SPI hosts (SPI2, SPI3 [DS] 4.2.1.5)
    free for the LCD and anything else.
- Demo: the wiki names the pins SDMMC-style [WIKI]. The mirror says "SD card
  support via SDMMC interface" [MIRROR]. Its modified `SD_MMC.c` uses 1-bit
  with internal pull-ups [MIRROR-SD]. Waveshare's original bus width is
  UNCONFIRMED.
- espp's README says the USB-A plug "doubles as a micro-SD card reader"
  [ESPP-REG]. The schematic has no reader chip: USB goes only to GPIO19/20
  [SCH]. So that can only be a firmware function (USB-OTG mass storage). It
  is not hardware, and it would compete with USB-Serial-JTAG for the one
  internal PHY [DS] 4.2.1.7.

## 5. USB and console

- The USB-A plug J1 goes: VBUS to the 5 V rail and the LDO; D- and D+
  through 22 Ω to GPIO19 and GPIO20 [SCH]. There is no USB-UART bridge
  anywhere on the schematic. The wiki says "USB Type-A" [WIKI].
- So USB is the S3's native PHY. By default GPIO19/20 belong to the USB
  Serial/JTAG controller [DS] 2.3.4.
  - It is a fixed CDC-ACM plus JTAG device that "supports host controllable
    chip reset and entry into download mode" [DS] 4.2.1.8.
  - USB-OTG can use the same PHY instead, time-shared [DS] 4.2.1.7.
- UART0 (GPIO43/44) goes only to header pins 17 and 18, not to the USB plug
  [SCH]. This matters for the port:
  - Today's Improv glue reads and writes `UART_NUM_0` (`src/main.cpp:361`,
    `:362`, `:577`, `:730`).
  - On this board UART0 talks to two header pins, and the browser never
    hears it.
  - Improv has to move to USB-Serial-JTAG for this build.
- Demo console (mirror sdkconfig, IDF 5.1.2) [MIRROR-CFG]:
  - `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` (UART0).
  - `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`: logs also go out over
    USB.
  - The wiki's FAQ says the same thing for Arduino, needing "USB CDC On
    Boot" for `Serial` [WIKI].
- IDF 5.3.1, USB-Serial-JTAG console [IDF-USJ]:
  - Primary is `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`. The secondary console is
    output only: "if you also want to input or use REPL with the console,
    please select CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG".
  - Flashing enters download mode automatically.
  - Limitations, quoted:
    - If the application reconfigures the USB pins, "the device disappears
      from the system", and recovery is GPIO0 low plus reset.
    - When its small buffer fills with no terminal attached, the chip "does
      a one-time wait of 50 ms".
    - Deep sleep drops the device.
- esptool [ESPTOOL-TS]:
  - If download mode was entered by hand over USB-Serial-JTAG, esptool
    cannot leave it with the default reset. "The USB-Serial/JTAG peripheral
    can only trigger a core reset, which does not re-sample the state of the
    boot strapping pin."
  - Use `--after watchdog-reset`, or press RESET.
  - The BOOT-hold recovery design is unaffected: it is GPIO0 read by
    firmware after boot, same as the WROOM.
- ESP Web Tools on an S3 over native USB:
  - ESP32-S3 is on its supported list [EWT].
  - Release notes [EWT-REL]:
    - 9.4.2 and 9.4.3: "Add small delay when resetting because USB JTAG".
    - 10.1.1: "Migrate to native reset when available".
    - 10.3.0: manifest builds accept an optional `serialType` ("cdc" or
      "uart").
  - Open against it: issue #331, "Firmware programming using native USB
    port on ESP32-S3 does not work", with "The device has been lost" at the
    end of an install. No fix was visible when fetched [EWT-331].
  - Whichever version the site vendors is what counts.
- Improv over USB-Serial-JTAG:
  - The Improv serial page says only that the device connects "via a
    USB/UART serial port" [IMPROV].
  - ESPHome's Improv component has a USB-Serial-JTAG path that writes with
    `usb_serial_jtag_write_bytes` [ESPHOME-IMPROV]. So Improv over this
    transport ships in a real firmware.
  - Install-then-Improv through the site's installer on this board is
    UNCONFIRMED. It needs one bench run.
- Flash offsets change with the chip: the S3 bootloader sits at 0x0
  [IDF-BOOT]. The release pipeline and the installer manifest need a
  per-chip bootloader offset and their own `chipFamily` entry.
  `tools/release.py` currently builds for `esp32` only.

## 6. Buttons

- BOOT is GPIO0: Key2 to GND, 10 kΩ pull-up R2 [SCH].
- RESET is CHIP_PU: Key1 to GND, 10 kΩ pull-up R1, 1 µF C16 [SCH].
- Both are listed on the product page [PROD].
- There is no other button. espp uses GPIO0 as its user button, active low
  [ESPP].
- Download mode per the wiki: hold BOOT, press and release RESET, release
  BOOT [WIKI].
- Strapping hold time is 3 ms after CHIP_PU rises [DS] Table 3-2.

## 7. GPIOs broken out, and what is free

- Header U2 carries 5V (VBUS), GND, 3V3, IO1-IO13, RXD (GPIO44) and TXD
  (GPIO43, through 499 Ω) [SCH]. Nothing else is broken out: GPIO46 and 47
  go nowhere [SCH].
- Free for a user: IO1-IO13, and GPIO43/44 once the console is on
  USB-Serial-JTAG.
  - IO3 is a strapping pin. It only matters if the JTAG-select eFuse is
    burnt (section 8), but it is the last one to hand out.
  - IO11-13 are ADC2 and cannot be used as ADC with Wi-Fi on
    [DS] 4.2.2.1.
- UART2 for the serial bridge: any two of IO1-IO13. UART2 has no fixed pins
  and "can be assigned to any GPIO pins" [DS] 2.3.5.
- External NeoPixel strip: any of IO1-IO13 except IO3. The header's 5 V pin
  is USB VBUS, so a strip's current comes out of whatever USB source powers
  the stick. What that source can supply is UNCONFIRMED, and Rob's ten-pixel
  power note already covers it.
- Power-up glitches: GPIO1-14 show a 60 µs low glitch, GPIO19/20 a high
  one [DS] Table 2-2. A strip or serial device should not see that as
  meaningful.

## 8. Strapping pins

| Pin | Default ([DS] T3-1) | What it selects | On this board |
|---|---|---|---|
| GPIO0 | weak pull-up, 1 | boot mode, with GPIO46 ([DS] T3-3) | BOOT button to GND, 10 kΩ pull-up [SCH]. SPI boot unless held |
| GPIO3 | floating | JTAG source, only when `EFUSE_STRAP_JTAG_SEL` is burnt ([DS] T3-5) | header IO3, nothing attached [SCH]. With default eFuses it is ignored and JTAG comes from USB |
| GPIO45 | weak pull-down, 0 | VDD_SPI voltage: 0 = 3.3 V, 1 = 1.8 V ([DS] T3-4) | LCD SDA, an input on the panel at reset [SCH]. The internal pull-down wins, VDD_SPI is 3.3 V, which the R8's 3.3 V PSRAM ([DS] T1-1) and the flash need. Not broken out, so nothing a user wires can pull it high |
| GPIO46 | weak pull-down, 0 | boot mode (with GPIO0), ROM log printing | not connected [SCH] |

- The pad JTAG pins (GPIO39-42) are the LCD, so pad JTAG is unavailable.
  USB JTAG, the default source, is unaffected [DS] 3.4 [SCH].

## 9. PlatformIO

- Start from `esp32-s3-devkitc-1` in espressif32 6.9.0 [PIO-BOARD]:
  - "ESP32-S3-DevKitC-1-N8 (8 MB QD, No PSRAM)", mcu esp32s3, flash mode
    qio, 80 MHz, 8MB.
  - `"maximum_ram_size": 327680`. PlatformIO's RAM percentage on the S3 is
    as meaningless as on the WROOM. Section 10 gives the real number.
  - No Waveshare definition turned up in the part of the 6.9.0 boards
    listing that could be read. UNCONFIRMED.
- Overrides in the env (the values are this report's recommendation):

  ```ini
  [env:ws_s3_lcd147]
  platform                = espressif32@6.9.0
  board                   = esp32-s3-devkitc-1
  framework               = espidf
  board_build.flash_mode  = dio        ; the demo's; qio after a bench test
  board_upload.flash_size = 16MB
  board_upload.maximum_size = 16777216
  board_build.partitions  = partitions.csv
  board_build.filesystem  = littlefs
  ```

  - PlatformIO warns, and does not stop, when `board_upload.flash_size`
    differs from `CONFIG_ESPTOOLPY_FLASHSIZE` [PIO-ESPIDF]. The shared
    `sdkconfig.defaults` says `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y`, so the S3
    layer must override it.
- sdkconfig for this board:
  - From the demo [MIRROR-CFG]: `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`,
    `CONFIG_ESPTOOLPY_FLASHFREQ_80M=y`, `CONFIG_ESPTOOLPY_FLASHMODE_DIO=y`,
    `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_MODE_OCT=y`,
    `CONFIG_SPIRAM_SPEED_80M=y`.
  - From IDF, not the demo: `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, the
    primary console, needed for Improv input [IDF-USJ].
  - Later, as its own measured step: `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`,
    plus `EXT_RAM_BSS_ATTR` on what should move (section 10).
- Layering one project over two chips [IDF-BUILD]:
  - Per chip: IDF loads `sdkconfig.defaults.<IDF_TARGET>` right after
    `sdkconfig.defaults`, "if and only if an `sdkconfig.defaults` file
    exists". A `sdkconfig.defaults.esp32s3` therefore layers over the shared
    file for every S3 env, with no PlatformIO setting. Later files override
    earlier ones, so `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` there beats the
    shared 4MB.
  - Per board (for a second S3 board): IDF takes `SDKCONFIG_DEFAULTS` as a
    semicolon list from the environment variable or from `set()` in the
    top-level `CMakeLists.txt`.
    - PlatformIO passes `board_build.cmake_extra_args` through to CMake
      [PIO-ESPIDF].
    - Whether `-DSDKCONFIG_DEFAULTS=...` there is honoured is UNCONFIRMED:
      IDF documents only the environment variable and `set()`.
- PlatformIO writes the config to `sdkconfig.<env>` [PIO-ESPIDF].
  - An existing value in it wins over the defaults, which apply only "when
    any new config value hasn't yet been set in the sdkconfig file"
    [IDF-BUILD].
  - Delete `sdkconfig.ws_s3_lcd147` after changing a default and check the
    generated file, as the project already does for the WROOM.
- The socket ceiling does not move with the chip. `LWIP_MAX_SOCKETS` is
  `range 1 16` in IDF 5.3.1 with no target condition [IDF-LWIP]. More RAM
  alone does not raise the ten-caller figure the WROOM notes call
  socket-bound.

## 10. Static DRAM on the S3 under IDF 5.3.1

- From `esp32s3/memory.ld.in` [IDF-MEM]:
  - `SRAM_DRAM_START 0x3FC88000`, `SRAM_DIRAM_I_START 0x40378000`.
  - `SRAM_IRAM_END 0x403CB700`, commented as the "2nd stage bootloader
    iram_loader_seg start address".
  - `I_D_SRAM_OFFSET` = 0x006F0000. `SRAM_DRAM_END` = 0x3FCDB700.
  - `I_D_SRAM_SIZE` = 0x53700 = 341,760 bytes.
  - `dram0_0_seg`: org 0x3FC88000, len 0x53700, unless
    `CONFIG_ESP32S3_USE_FIXED_STATIC_RAM_SIZE`.
  - `iram0_0_seg`: org 0x40370000 plus the I-cache size, which defaults to
    16 KB [IDF-KCACHE], so 0x40374000; len 0x57700.
- The catch: on the S3, IRAM and DRAM are the same SRAM.
  - `sections.ld.in` starts DRAM at `ORIGIN(dram0_0_seg) + MAX(_iram_end -
    _diram_i_start, 0)`, with `_diram_i_start = SRAM_DIRAM_I_START`
    [IDF-SECT] [IDF-MEM].
  - Every IRAM byte past 0x40378000 comes out of the 341,760. The 16 KB of
    IRAM from 0x40374000 to 0x40377FFF is free.
- Link check: `ASSERT(((_bss_end - ORIGIN(dram0_0_seg)) <=
  LENGTH(dram0_0_seg)), "DRAM segment data does not fit.")` [IDF-SECT].
- Measure it the WROOM way: `_bss_end - 0x3FC88000` against 341,760, read
  off the ELF.
  - That figure already includes the IRAM spill.
  - Read `_iram_end` alongside it to see how much of the budget IRAM is
    taking.
  - How much IRAM this firmware places on the S3 is UNCONFIRMED until a
    build exists. Do not quote "1.9 times the WROOM's 180,736": the S3's
    number is shared with IRAM code, so the two figures do not compare
    like for like until both are measured off an ELF.
- Heap: memory.ld.in notes that "startup code uses the IRAM from 0x403B9000
  to 0x403E0000, which is not available for static memory, but can only be
  used after app starts" [IDF-MEM]. Some internal heap appears only after
  boot, and the 8 MB PSRAM comes on top through malloc.
- PSRAM for static data, a correction to the CLAUDE.md wording
  [IDF-EXTRAM]:
  - `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` moves the BSS of "the
    lwIP, net80211, libpp, wpa_supplicant and bluedroid ESP-IDF libraries"
    automatically.
  - Application statics move only when declared with `EXT_RAM_BSS_ATTR`.
    The session pool does not go to PSRAM by the option alone; it needs the
    attribute on its declaration.
  - Restrictions: DMA descriptors cannot live in PSRAM; PSRAM is
    unreachable while the flash cache is disabled (every LittleFS write);
    ISRs and callbacks that can run then must stay in internal RAM.

## Port checklist

What this board needs that the WROOM build does not, from the sections
above:

- A per-chip GPIO refuse list: 19, 20, 26-37 hard; board-owned pins
  optional (section 1).
- Improv moved from UART0 to USB-Serial-JTAG, and USB-Serial-JTAG as the
  primary console (section 5).
- RMT block sizing per chip, and a colour-order option for WS2812 outputs
  (section 3).
- `activity_led_gpio` not left at 2 (bare pin) and never 38 (section 3).
- SD in SPI mode as CS 21, MOSI 15, SCK 14, MISO 16, on the SPI host the LCD
  is not using. Or SDMMC 4-bit, which the S3 allows on these pins
  (section 4).
- Installer: S3 bootloader at 0x0, its own `chipFamily`, one bench run of
  install-then-Improv (section 5).
- Measure static DRAM as `_bss_end - 0x3FC88000` against 341,760, IRAM spill
  included (section 10).

## Sources

- [WIKI]: https://www.waveshare.com/wiki/ESP32-S3-LCD-1.47
- [PROD]: https://www.waveshare.com/esp32-s3-lcd-1.47.htm
- [WIKI-B]: https://www.waveshare.com/wiki/ESP32-S3-LCD-1.47B
- [SCH]: https://files.waveshare.com/wiki/ESP32-S3-LCD-1.47/ESP32-S3-LCD-1.47_schematic_diagram.pdf
- [DEMO] (not read, over the fetch limit): https://files.waveshare.com/wiki/ESP32-S3-LCD-1.47/ESP32-S3-LCD-1.47-Demo.zip
- [LCD-DS]: https://files.waveshare.com/wiki/ESP32-S3-LCD-1.47/1.47inch_LCD_Datasheet.pdf
- [DS] ESP32-S3 Series Datasheet v2.2 (2026-03-05): https://documentation.espressif.com/esp32-s3_datasheet_en.pdf
- [MIRROR]: https://github.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47
- [MIRROR-H]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/main/LCD_Driver/ST7789.h
- [MIRROR-C]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/main/LCD_Driver/ST7789.c
- [MIRROR-VERNON]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/main/LCD_Driver/Vernon_ST7789T/Vernon_ST7789T.c
- [MIRROR-LVGL]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/main/LVGL_Driver/LVGL_Driver.c
- [MIRROR-RGB]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/main/RGB/RGB.c
- [MIRROR-LEDSTRIP]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/components/espressif__led_strip/src/led_strip_rmt_dev.c
- [MIRROR-SD]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/main/SD_Card/SD_MMC.c
- [MIRROR-CFG]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/sdkconfig
- [ESPP]: https://raw.githubusercontent.com/esp-cpp/espp/main/components/ws-s3-lcd-1-47/include/ws-s3-lcd-1-47.hpp
- [ESPP-REG]: https://components.espressif.com/components/espp/ws-s3-lcd-1-47
- [TFTESPI]: https://github.com/Bodmer/TFT_eSPI/discussions/3527
- [IDF-MEM]: https://raw.githubusercontent.com/espressif/esp-idf/v5.3.1/components/esp_system/ld/esp32s3/memory.ld.in
- [IDF-SECT]: https://raw.githubusercontent.com/espressif/esp-idf/v5.3.1/components/esp_system/ld/esp32s3/sections.ld.in
- [IDF-KCACHE]: https://raw.githubusercontent.com/espressif/esp-idf/v5.3.1/components/esp_system/port/soc/esp32s3/Kconfig.cache
- [IDF-EXTRAM]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/external-ram.html
- [IDF-FP]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/flash_psram_config.html
- [IDF-USJ]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/usb-serial-jtag-console.html
- [IDF-SDMMC]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/peripherals/sdmmc_host.html
- [IDF-RMT]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/peripherals/rmt.html
- [IDF-RMT-SRC]: https://raw.githubusercontent.com/espressif/esp-idf/v5.3.1/components/esp_driver_rmt/src/rmt_tx.c
- [IDF-LCD]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/peripherals/lcd/spi_lcd.html
- [IDF-BUILD]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/build-system.html
- [IDF-BOOT]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/bootloader.html
- [IDF-LWIP]: https://raw.githubusercontent.com/espressif/esp-idf/v5.3.1/components/lwip/Kconfig
- [PIO-BOARD]: https://raw.githubusercontent.com/platformio/platform-espressif32/v6.9.0/boards/esp32-s3-devkitc-1.json
- [PIO-ESPIDF]: https://raw.githubusercontent.com/platformio/platform-espressif32/v6.9.0/builder/frameworks/espidf.py
- [EWT]: https://esphome.github.io/esp-web-tools/
- [EWT-REL]: https://github.com/esphome/esp-web-tools/releases
- [EWT-331]: https://github.com/esphome/esp-web-tools/issues/331
- [ESPHOME-IMPROV]: https://raw.githubusercontent.com/esphome/esphome/dev/esphome/components/improv_serial/improv_serial_component.cpp
- [IMPROV]: https://www.improv-wifi.com/serial/
- [ESPTOOL-TS]: https://docs.espressif.com/projects/esptool/en/latest/esp32s3/troubleshooting.html

[WIKI]: https://www.waveshare.com/wiki/ESP32-S3-LCD-1.47
[PROD]: https://www.waveshare.com/esp32-s3-lcd-1.47.htm
[WIKI-B]: https://www.waveshare.com/wiki/ESP32-S3-LCD-1.47B
[SCH]: https://files.waveshare.com/wiki/ESP32-S3-LCD-1.47/ESP32-S3-LCD-1.47_schematic_diagram.pdf
[DEMO]: https://files.waveshare.com/wiki/ESP32-S3-LCD-1.47/ESP32-S3-LCD-1.47-Demo.zip
[LCD-DS]: https://files.waveshare.com/wiki/ESP32-S3-LCD-1.47/1.47inch_LCD_Datasheet.pdf
[DS]: https://documentation.espressif.com/esp32-s3_datasheet_en.pdf
[MIRROR]: https://github.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47
[MIRROR-H]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/main/LCD_Driver/ST7789.h
[MIRROR-C]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/main/LCD_Driver/ST7789.c
[MIRROR-VERNON]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/main/LCD_Driver/Vernon_ST7789T/Vernon_ST7789T.c
[MIRROR-LVGL]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/main/LVGL_Driver/LVGL_Driver.c
[MIRROR-RGB]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/main/RGB/RGB.c
[MIRROR-LEDSTRIP]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/components/espressif__led_strip/src/led_strip_rmt_dev.c
[MIRROR-SD]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/main/SD_Card/SD_MMC.c
[MIRROR-CFG]: https://raw.githubusercontent.com/mylesdebastion/waveshare-esp32-s3-lcd-1.47/master/ESP32-S3-LCD-1.47-Demo/ESP-IDF/ESP32-S3-LCD-1.47-Test/sdkconfig
[ESPP]: https://raw.githubusercontent.com/esp-cpp/espp/main/components/ws-s3-lcd-1-47/include/ws-s3-lcd-1-47.hpp
[ESPP-REG]: https://components.espressif.com/components/espp/ws-s3-lcd-1-47
[TFTESPI]: https://github.com/Bodmer/TFT_eSPI/discussions/3527
[IDF-MEM]: https://raw.githubusercontent.com/espressif/esp-idf/v5.3.1/components/esp_system/ld/esp32s3/memory.ld.in
[IDF-SECT]: https://raw.githubusercontent.com/espressif/esp-idf/v5.3.1/components/esp_system/ld/esp32s3/sections.ld.in
[IDF-KCACHE]: https://raw.githubusercontent.com/espressif/esp-idf/v5.3.1/components/esp_system/port/soc/esp32s3/Kconfig.cache
[IDF-EXTRAM]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/external-ram.html
[IDF-FP]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/flash_psram_config.html
[IDF-USJ]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/usb-serial-jtag-console.html
[IDF-SDMMC]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/peripherals/sdmmc_host.html
[IDF-RMT]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/peripherals/rmt.html
[IDF-RMT-SRC]: https://raw.githubusercontent.com/espressif/esp-idf/v5.3.1/components/esp_driver_rmt/src/rmt_tx.c
[IDF-LCD]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/peripherals/lcd/spi_lcd.html
[IDF-BUILD]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/build-system.html
[IDF-BOOT]: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/bootloader.html
[IDF-LWIP]: https://raw.githubusercontent.com/espressif/esp-idf/v5.3.1/components/lwip/Kconfig
[PIO-BOARD]: https://raw.githubusercontent.com/platformio/platform-espressif32/v6.9.0/boards/esp32-s3-devkitc-1.json
[PIO-ESPIDF]: https://raw.githubusercontent.com/platformio/platform-espressif32/v6.9.0/builder/frameworks/espidf.py
[EWT]: https://esphome.github.io/esp-web-tools/
[EWT-REL]: https://github.com/esphome/esp-web-tools/releases
[EWT-331]: https://github.com/esphome/esp-web-tools/issues/331
[ESPHOME-IMPROV]: https://raw.githubusercontent.com/esphome/esphome/dev/esphome/components/improv_serial/improv_serial_component.cpp
[IMPROV]: https://www.improv-wifi.com/serial/
[ESPTOOL-TS]: https://docs.espressif.com/projects/esptool/en/latest/esp32s3/troubleshooting.html
