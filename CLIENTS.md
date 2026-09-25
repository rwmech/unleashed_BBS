<!--
 ===========================================================================
  µnleashed BBS
  Electronic freedom on a microcontroller.
 ===========================================================================

 File:         CLIENTS.md
 Module:       Documentation / callers

 Purpose:      Every machine that can call the board, the terminal software
               it runs, and the hardware that puts it on the wire.

 Audience:     Anyone wondering whether the computer on their shelf can dial
               in. The short version of this list also lives in README.md;
               update both together.

 Copyright 2026 - Robert Mech
 License:      GNU General Public License v3 or later
 SPDX-License-Identifier: GPL-3.0-or-later
 ===========================================================================
-->

# µnleashed BBS: what can call in

The board wants one thing: a TCP connection to port 6400 carrying plain text. Everything else is detail. That means almost any computer built since 1977 can call it, as long as something bridges its serial port to a network.

Nothing needs configuring at the caller's end. Terminal type, character set and width are worked out at connect time: ANSI with CP437 or UTF-8, PETSCII at 40 or 80 columns, or plain ASCII. If the detection cannot decide, the board asks one question and remembers the answer for the call.

## Three ways onto the wire

A machine reaches the board through one of these:

1. **A Wi-Fi or Ethernet modem emulator** in the machine's own port. It answers `ATDT` and opens a telnet session, so the terminal software thinks it is dialling a phone number. [TeensyROM](https://github.com/SensoriumEmbedded/TeensyROM), [WiModem232 Pro](https://www.cbmstuff.com/index.php?route=product/product&path=66&product_id=113), [Comet64](https://www.commodoreserver.com/ProductView.asp?PID=365065CF529B4C408F7D01C08BA34803), [Zimodem](https://github.com/bozimmerman/Zimodem) and [FujiNet](https://fujinet.online/) all work this way.
2. **A serial-to-network bridge** on the network, with a null modem cable to the machine's RS-232 port. A [Lantronix device server](https://www.lantronix.com/lantronix-products/network-infrastructure/serial-to-ethernet-device-servers/), an [ESP-Link](https://github.com/jeelabs/esp-link) board, an [ESP32-Serial-Bridge](https://github.com/AlphaLima/ESP32-Serial-Bridge), or any small Linux box running a socket-to-serial daemon.
3. **A native TCP stack**, if the machine has one. Anything from an Amiga with a TCP/IP stack to a phone in your pocket.

There is also a fourth, going the other way: the board's own **serial bridge plugin** hands a caller the second UART, so a terminal with no network at all can be driven from the BBS. See [COMMANDS.md](COMMANDS.md).

## Commodore

| Machine | Software | Onto the wire |
|---|---|---|
| C64, C128 | [CCGMS](https://github.com/mist64/ccgmsterm), [Novaterm](https://commodore.software/downloads/download/19-novaterm/653-novaterm-9-6c), [DesTerm 128](https://csdb.dk/release/?id=171068), [StrikeTerm](https://csdb.dk/release/?id=130807) | [TeensyROM](https://github.com/SensoriumEmbedded/TeensyROM), [WiModem232 Pro](https://www.cbmstuff.com/index.php?route=product/product&path=66&product_id=113), [Comet64](https://www.commodoreserver.com/ProductView.asp?PID=365065CF529B4C408F7D01C08BA34803), [Zimodem](https://github.com/bozimmerman/Zimodem), an RS-232 cartridge, or a UP9600 cable to a bridge |
| VIC-20, PET, CBM, Plus/4 | period terminal software for the machine | a user-port RS-232 interface to a bridge |

The original WiModem232 is discontinued (the Pro is the current one), and the Comet64 is between production runs at the time of writing. TeensyROM and Zimodem are the easiest to get hold of today.

PETSCII is a first-class citizen here, not a fallback: the board detects it, switches to the C64 colour palette and glyph set, and lays every screen out for 40 columns. The C64 side is what the screens were designed against.

## Atari

| Machine | Software | Onto the wire |
|---|---|---|
| Atari 8-bit (400/800/XL/XE) | [BobTerm](https://archive.org/details/a8b_misc_bobtrmxp), [Ice-T](https://github.com/itaych/Ice-T), [AMODEM](https://archive.org/details/a8b_Amodem_v7.5_1987_Trent_Duley_BASIC) | [FujiNet](https://fujinet.online/atari-8-bit/), an [850 interface](https://en.wikipedia.org/wiki/Atari_8-bit_computer_peripherals#850) to a bridge, or an SIO2PC-style adapter |
| Atari ST, Falcon | [UniTerm](https://www.atarimania.com/utility-atari-st-uniterm_33343.html), [Flash](https://www.atarimania.com/utility-atari-st-flash_21476.html), [CoNnect](https://www.atariuptodate.de/en/984/connect) | the built-in serial port to a bridge |

## Apple

| Machine | Software | Onto the wire |
|---|---|---|
| Apple II, IIgs | [ProTERM](https://en.wikipedia.org/wiki/ProTERM) ([disk images](https://www.applearchives.com/software/intrec-proterm-apple-ii/)), [ASCII Express](https://en.wikipedia.org/wiki/ASCII_Express), [Spectrum](https://speccie.uk/software/spectrum/) | [Uthernet II](https://a2retrosystems.com/products.htm), or a [Super Serial Card](https://en.wikipedia.org/wiki/Apple_II_serial_cards) to a bridge |
| Classic Mac | [ZTerm](https://www.dalverson.com/zterm/), [MicroPhone II](https://www.macintoshrepository.org/33227-microphone-ii), [White Knight](https://www.macintoshrepository.org/33223-white-knight) | the modem or printer port to a bridge |
| Modern Mac | [MuffinTerm](https://apps.apple.com/us/app/muffinterm/id1583236494), [SyncTERM](https://syncterm.bbsdev.net/), or `nc host 6400` | it is already on the network |

## Amiga

| Software | Onto the wire |
|---|---|
| [NComm](https://aminet.net/package/comm/term/ncomm307), [term](https://aminet.net/package/comm/term/Term) by Olaf Barthel, [JR-Comm](https://archive.org/details/JR-Comm_v1.02_1991_Radigan_John), [A-Talk](https://archive.org/details/A-Talk_III_v1.0e_1986_Felsina_Software) | the serial port to a bridge, or a TCP/IP stack and a telnet client |

## Tandy, MSX, Sinclair and friends

| Machine | Software | Onto the wire |
|---|---|---|
| TRS-80 Color Computer | [Greg-E-Term](https://colorcomputerarchive.com/repo/Disks/Applications/Greg-E-Term%20%28Greg%20Miller%29.zip) | the bit-banger serial port to a bridge |
| TRS-80 Model 100, 102 | the built-in TELCOM ([Model 100](https://en.wikipedia.org/wiki/TRS-80_Model_100)) | the RS-232 port to a bridge |
| MSX | [TELNET for the UNAPI stack](https://github.com/ducasp/MSX-Development/tree/master/UNAPI/TELNET) | an ethernet or Wi-Fi UNAPI cartridge |
| ZX Spectrum | [VTX 5000 software](https://spectrumcomputing.co.uk/entry/11152/ZX-Spectrum/VTX_5000_User_To_User_Communications_Software), [Spectranet](https://spectrum.alioth.net/doc/index.php/Spectranet) clients | a [Prism VTX 5000](https://worldofspectrum.org/hardware/feat24.html), an RS-232 interface, or a [Spectranet](https://github.com/spectrumero/spectranet) card |
| Amstrad CPC | [EwenTerm](https://ewen.mcneill.gen.nz/programs/cpc/ewenterm/) and other period terminal software | a serial interface to a bridge |

## CP/M and S-100

| Software | Onto the wire |
|---|---|
| [Kermit](https://www.kermitproject.org/cpm.html), [MEX](http://www.zimmers.net/anonftp/pub/cpm/comm/mex/index.html) | the machine's serial port to a bridge |

[Kaypro](https://en.wikipedia.org/wiki/Kaypro), [Osborne](https://en.wikipedia.org/wiki/Osborne_1), Altair, Northstar: if it has a serial port and a terminal program, it can call. The board's plain ASCII mode exists for exactly these machines, and `BAUD` will slow the output down to match a 300 bps link if you want the authentic crawl.

## DOS and Windows

| Era | Software |
|---|---|
| DOS | [Telix](https://en.wikipedia.org/wiki/Telix), [Procomm Plus](https://en.wikipedia.org/wiki/Datastorm_Technologies), [Qmodem](https://en.wikipedia.org/wiki/Qmodem), [Terminate](https://en.wikipedia.org/wiki/Terminate_%28software%29), [Telemate](https://archive.org/details/msdos_festival_TM421-1) |
| Today | [SyncTERM](https://syncterm.bbsdev.net/), [NetRunner](https://www.mysticbbs.com/downloads.html), [mTelnet](https://mt32.bbses.info/), [PuTTY](https://www.chiark.greenend.org.uk/~sgtatham/putty/) |

SyncTERM and NetRunner are the ones to reach for if you want the full ANSI experience with CP437 line drawing. PuTTY works and is what most of the development testing runs against.

Windows Terminal is a terminal host, not a telnet client: it has no telnet of its own. Turn the optional Windows feature on first (`dism /online /Enable-Feature /FeatureName:TelnetClient`), then `telnet host 6400` inside it.

## Real terminals

| Terminal | Onto the wire |
|---|---|
| [DEC VT100](https://en.wikipedia.org/wiki/VT100), [VT220](https://en.wikipedia.org/wiki/VT220), [VT320](https://en.wikipedia.org/wiki/VT320) | a terminal server, a USB serial adapter, or the board's own serial bridge |
| [Wyse WY-60](https://terminals-wiki.org/wiki/index.php/Wyse_WY-60), [Televideo 925](https://terminals-wiki.org/wiki/index.php/TeleVideo_925), [ADM-3A](https://en.wikipedia.org/wiki/ADM-3A), [IBM 3151](https://terminals-wiki.org/wiki/index.php/IBM_3151), [Heathkit H19](https://terminals-wiki.org/wiki/index.php/Heathkit_H19) | the same |
| [Teletype Model 33 ASR](https://en.wikipedia.org/wiki/Teletype_Model_33) | a current loop converter, at 110 baud, if that is the sort of thing you enjoy |

A glass terminal gets the board's ANSI mode. The ones that predate ANSI get plain ASCII, which is why that mode has no cursor addressing in it at all: everything scrolls, forms become one question per line, and nothing assumes the terminal can move the cursor.

## Unix and modern

| Client | Notes |
|---|---|
| [GNU inetutils telnet](https://www.gnu.org/software/inetutils/) | `telnet unleashed.local 6400`. On Debian and Ubuntu: `sudo apt -y install inetutils-telnet` |
| [netcat](https://man.openbsd.org/nc.1) | `nc unleashed.local 6400`. No telnet negotiation, fine for a quick look |
| [SyncTERM](https://syncterm.bbsdev.net/) | the one to use on Linux and macOS for proper ANSI art |

macOS has not shipped `telnet` since 10.13 High Sierra. Use `nc`, install one with `brew install inetutils`, or run SyncTERM or MuffinTerm.

## Phones and tablets

The phone in your pocket is a perfectly good terminal, and these all speak telnet rather than SSH only.

| Platform | App | Notes |
|---|---|---|
| Android | [TERMinator](https://play.google.com/store/apps/details?id=com.terminator.android) | built for BBSes: CP437 and ANSI art, IBM VGA and Topaz fonts, 80x25 through 132x50, ZMODEM. The one to install |
| Android | [ConnectBot](https://play.google.com/store/apps/details?id=org.connectbot) | open source, telnet is in there despite the store copy saying SSH |
| Android | [Termius](https://play.google.com/store/apps/details?id=com.server.auditor.ssh.client) | telnet in the free tier, modern, but a plain xterm renderer rather than CP437 |
| iOS | [TERMinator](https://apps.apple.com/us/app/terminator-bbs-terminal/id6759012939) | the same BBS-minded client as the Android build |
| iOS, macOS | [MuffinTerm](https://apps.apple.com/us/app/muffinterm/id1583236494) | ANSI, **PETSCII** and ATASCII, XMODEM through ZMODEM, a dialling directory. The PETSCII support makes it the pick if you care about the C64 side |

Termux plus `pkg install inetutils` works too, but it is UTF-8 only, so CP437 art will come out wrong.

## What has actually been tested

Honesty matters more than a long list:

- **Verified on hardware:** PuTTY over the network, and a C64 through TeensyROM, including every PETSCII glyph the screens use.
- **Verified on the host build:** ANSI with UTF-8 and CP437, PETSCII at 40 and 80 columns, plain ASCII, and telnet clients that negotiate first.
- **Expected to work, not yet tried by us:** everything else on this page. The protocol is plain telnet and the detection falls back to asking, so the risk is low, but nobody has sat a Kaypro in front of it yet.

If you get a machine onto the board that is not on this list, or one on this list turns out to need a trick, say so and it goes in.

## See also

- [README.md](README.md): what the board is, and the short version of this list.
- [COMMANDS.md](COMMANDS.md): every command, key and setting.
- [CHAT.md](CHAT.md): the chat room and messages.
