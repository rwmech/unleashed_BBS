<!--
µnleashed BBS: README.md

What the project is, how to build, flash and test it, and where the rest of the documentation lives.

Copyright 2026 - Robert Mech
License: GNU General Public License v3 or later
SPDX-License-Identifier: GPL-3.0-or-later

Documentation for µnleashed BBS, part of the same distribution as the
source. See the LICENSE file for terms.
-->

# µnleashed BBS

Electronic freedom on a microcontroller. No web, no cloud, no browser.

µnleashed is a telnet BBS that runs on a bare ESP32-WROOM-32E and grows into an IoT terminal server through plugins: real hardware you reach from a 1982 home computer, a glass terminal, a phone or anything else that speaks telnet.

**The board is yours.** Not an account on somebody's platform, not a tenant on a server farm, not a feature that can be deprecated out from under you. It is a chip you own, on a port you chose, running software you can read all of in an afternoon and change when you disagree with it. Switch it off and it is off. Leave it in a drawer for a year, plug it back in, and it still works, because there is nothing at the other end that has to still exist.

**Before the web, this is how it worked.** Somebody put a spare machine in a spare room, hung a modem off it, and other people called it. No terms of service, no algorithm deciding what you saw, no third party keeping a copy for later. The sysop was a person you could ring up and argue with. [CBBS](https://en.wikipedia.org/wiki/CBBS) went online in Chicago on 16 February 1978, written by Ward Christensen with hardware by Randy Suess, the January blizzard having handed them the quiet weeks to finish it. Thousands of boards followed, each one somebody's own idea of what a community should look like, and at the [peak in the mid-1990s](https://en.wikipedia.org/wiki/Bulletin_board_system) an estimated 60,000 were running in the United States alone. Most of them on hardware weaker than the chip this runs on.

**And you can do it today.** Not as a re-enactment: as a live system with callers on it tonight. Flash the board, give it your Wi-Fi, forward one port on your router to it, and you are running a public BBS. That is the whole procedure. No hosting bill, no domain required, no provider to ask permission from, no account with anybody. A chip on a shelf and one line in your router, and your board is on the internet with a name you chose, run by you, for whoever you tell about it. [PUBLIC.md](PUBLIC.md) walks through it, including the parts that actually carry risk and the two settings that deal with them.

**Then we traded it for convenience.** Now the conversation lives on machines you cannot see, indexed, scraped to train something, monetised, ranked, and deleted at somebody else's discretion. You do not own the room, the member list, the history, or the right to keep any of it. You rent all of it, and the rent is paid in attention and data.

**This hands some of it back.** The user list is a text file. The settings are a text file. A message goes from one caller to another through a chip on your shelf and is gone the moment it is read. Nothing here is indexed, syndicated, profiled or sold, because there is no third party in the middle to do it. There is no account to create, nothing to subscribe to, and no vendor who can change the deal. It is GPL, so nobody can take it away from you later. That includes me.

**Honest about the limit.** Telnet is plain text, because a C64 cannot do TLS and pretending otherwise would be worse than saying so. This keeps your board off the public internet's record, not off the wire. If it has to survive somebody watching the link, put it behind a VPN or leave it on the LAN. Privacy you can explain in one sentence beats privacy you have to take on faith.

**Small on purpose.** One static binary, static allocation, no heap in the main loop, a fixed memory budget on a chip with 520 KB of RAM. No web stack, no scripting runtime, no package tree to audit at two in the morning, no telemetry, no update that arrives without you. What is not built cannot be exploited, and what fits in one head can be trusted by the person whose head it fits in.

**Quiet by default.** Your board will still be found: scanners sweep every address on the internet, and the ones that matter fingerprint the protocol rather than trusting the port number. But being found is not being read. Nothing crawls a BBS, logs in, sits in the room and scrapes the conversation, because there is no API to hand it over and nobody whose business it is to want it. Somebody learns a board answers there; they do not walk away with what was said on it. That is a different arrangement from everything else you use, and it is the arrangement worth keeping.

**Easy to put anywhere.** Flash it, power it, five minutes and it is answering calls. It costs less than lunch and draws less than a night light, which means you put ten of them around doing ten different jobs instead of building one enormous system that does everything and belongs to nobody. A board in the shack, a board in the garage, a board at the repeater site.

**Serial did not die.** [RS-232](https://en.wikipedia.org/wiki/RS-232) was standardised by the EIA in 1960 and still runs the console and management ports on network equipment, PLCs and test gear, and its asynchronous framing survives on nearly every microcontroller made since as a [TTL-level UART](https://en.wikipedia.org/wiki/Universal_asynchronous_receiver-transmitter). Sixty-five years on, the way a machine from 1982 talks is still the way you talk to the switch in the rack. That is why a [Commodore 64](https://en.wikipedia.org/wiki/Commodore_64) and a laptop bought this year can both call this board, and why the board can turn round and drive whatever is hanging off its own serial port.

**It brings the old iron back.** Not emulated, not behind glass in a museum: a C64, an Atari 800, a VT220 on a desk, dialled in tonight, doing something real. Hardware with no browser and no future on the modern web gets a live system to call, over the same port it always used.

**The name.** The micro sign is there because this runs on a microcontroller, and because microcomputers are what put computing in the hands of people who were never going to be given time on a mainframe. The [Altair 8800](https://en.wikipedia.org/wiki/Altair_8800) in 1975, then the Apple II, the PET and the TRS-80 in [1977](https://en.wikipedia.org/wiki/History_of_personal_computers#1977_and_the_emergence_of_the_%22Trinity%22), took the computer out of the raised-floor room that somebody else controlled and put it on a kitchen table. This is the same move, one more time, on a chip the size of a postage stamp. Where µ cannot be shown (PETSCII, hostnames, file names) it is written `unleashed`.

The core has one dial-in port, 10 caller nodes, a busy line and a hidden sysop node. It also has connect-time terminal detection, user accounts with fill-in forms, guest access, screens, a line editor with history, paged output, a message bus between nodes, a chat room, the TTY effects library and a shell.

Docs:

| File | For |
|---|---|
| [COMMANDS.md](COMMANDS.md) | every command, key, limit and `system.cfg` setting |
| [USERS.md](USERS.md) | signing up, logging in, guests, managing accounts |
| [PLUGINS.md](PLUGINS.md) | writing and running plugins |
| [BACKUP.md](BACKUP.md) | downloading and uploading config, accounts and screens as a `.zip`, and `BACKUP SD` / `RESTORE SD` to keep it on the SD card |
| [SCREENS.md](SCREENS.md) | screen formats, naming rules and upload limits |
| [CHAT.md](CHAT.md) | the chat room, room commands and messages |
| [CLIENTS.md](CLIENTS.md) | every machine that can call in, and what it needs |
| [PUBLIC.md](PUBLIC.md) | putting your board on the internet, and what that risks |
| [ANNOUNCE.md](ANNOUNCE.md) | listing your board in a directory, and the protocol for running one |
| [CHANGELOG.md](CHANGELOG.md) | what changed in every build |

## What it's for

A board the size of a stick of gum, 40 columns of text, and a port anyone can reach. Ordinary reasons first, stranger ones after.

- **Run a BBS again.** Nodes, handles, a user list, a chat room, file areas with XMODEM and YMODEM transfer, mail, a caller log, and a sysop you can ring for (`OPERATOR`), who answers in the chat room or finds what you wrote in their mail. All of it on hardware that costs less than lunch and draws less power than a night light. Leave it on a shelf for a year and forget it is there.
- **Retrocomputing with a point.** A C64, an Atari 800, a VT220 on a desk: machines with no browser and no future on the modern web get a live system to call, tonight, over the same serial port they always used. No emulator, no cloud account, no subscription. The board speaks their language, right down to PETSCII at 40 columns and an emulated 300 baud if you want to watch the text crawl.
- **Entertainment.** Games, trivia, message bases, and the whole business of dialling in to see what is new. `FORUMS` is topic boards on the SD card, with subjects, replies and unread counts. Text is a format, not a limitation: people played MUDs on less.
- **Somewhere to hang out.** The chat room is DDial and Gtalk in spirit: everybody in one room, one line at a time, handles and ranks in the margin, nothing threaded, nothing archived, nobody suggesting content. Small, fast, and with a personality that group chat lost somewhere around 2010.
- **A private board for a club, a family or a team.** Handles you hand out, a room, mail, and a file area with real XMODEM and YMODEM transfer once an SD card is fitted. Nobody signs up for a service, nobody agrees to terms, and the member list is a text file you can read.
- **Teach the whole stack.** One repository shows a TCP listener, a cooperative scheduler, terminal detection, a line editor, a permission model, a plugin API and a backup format, in a few thousand lines of C++ you can read on a rainy afternoon. Nothing is hidden behind a framework, and the whole thing fits in a microcontroller a student can hold.
- **Reach into the physical world.** The board has GPIO, and a plugin turns a pin into a command with its own permission level. That is a greenhouse you water from a Kaypro, a garage door, a ham shack antenna switch or rotator, a sprinkler zone, a 3D print farm, a brew rig, a generator that needs starting before you drive home, the block heater on the car on a cold morning, a model rocket launch controller, a lab bench you poke at from the far side of the building, or a deployment you kick off from a terminal on a boat. Anything that can be a relay closure or a sensor read can be a command somebody types. Interlocks and safety belong in the hardware, not in a text command: put the relay behind something that fails safe.
- **A front panel for a thing that has none.** Plenty of equipment has a serial port and nothing else. The serial bridge hands that port to a caller: one operator drives, everyone else watches. A PLC, a radio, a piece of test gear, a label printer, a headless server's console, a homelab status board. It is the cheapest remote console you will ever build, and it keeps working when the machine it is attached to does not.
- **Work where the web does not.** No browser, no TLS handshake, no 4 MB of JavaScript. Plain text over a socket survives a satellite hop, a packet radio link, a cellular modem on one bar, and an afternoon when somebody's DNS provider is having a bad day. A whole evening on the board costs less traffic than one modern web page, which matters when traffic is metered, slow, or being carried by a radio.
- **Conversations that never touch the web.** Nothing here is indexed, syndicated, scraped for training, analysed or sold. There is no third party in the middle: a message goes from one caller to another through a chip you own, on a port you chose, and it is gone when it is read. Worth being precise about what that does and does not buy you: telnet is plain text, so this keeps your conversation off the public internet's record, not off the wire. If it has to survive somebody watching the link, put the board behind a VPN or leave it on the LAN.
- **A low-profile system by construction.** The whole internet is crawled, catalogued and probed over HTTP; almost nobody is indexing what a telnet port on a chosen number has to say. Add to that no web stack, no scripting runtime, no package tree to audit at two in the morning, no telemetry and no cloud account, and you get a system that is quiet by default and small enough to reason about in one sitting. That is a security posture as much as an aesthetic: what is not built cannot be exploited, and what is not published cannot be harvested.

## What can call in

Anything with a telnet client, and, with a modem emulator or a serial bridge, very nearly anything with a serial port. The full list, with what each machine needs to get on the wire, is in **[CLIENTS.md](CLIENTS.md)**.

- **Commodore.** C64 and C128 ([CCGMS](https://github.com/mist64/ccgmsterm), [Novaterm](https://commodore.software/downloads/download/19-novaterm/653-novaterm-9-6c), [DesTerm](https://csdb.dk/release/?id=171068), [StrikeTerm](https://csdb.dk/release/?id=130807)), VIC-20, PET and CBM, Plus/4. On the wire: [TeensyROM](https://github.com/SensoriumEmbedded/TeensyROM), [WiModem232 Pro](https://www.cbmstuff.com/index.php?route=product/product&path=66&product_id=113), [Comet64](https://www.commodoreserver.com/ProductView.asp?PID=365065CF529B4C408F7D01C08BA34803), [Zimodem](https://github.com/bozimmerman/Zimodem), an RS-232 cartridge, or a UP9600 cable to a bridge.
- **Atari.** 8-bit ([BobTerm](https://archive.org/details/a8b_misc_bobtrmxp), [Ice-T](https://github.com/itaych/Ice-T), [AMODEM](https://archive.org/details/a8b_Amodem_v7.5_1987_Trent_Duley_BASIC)) through an [850 interface](https://en.wikipedia.org/wiki/Atari_8-bit_computer_peripherals#850), a [FujiNet](https://fujinet.online/atari-8-bit/) or a serial bridge. ST and Falcon ([UniTerm](https://www.atarimania.com/utility-atari-st-uniterm_33343.html), [Flash](https://www.atarimania.com/utility-atari-st-flash_21476.html), [CoNnect](https://www.atariuptodate.de/en/984/connect)).
- **Apple.** II and IIgs ([ProTERM](https://en.wikipedia.org/wiki/ProTERM), [ASCII Express](https://en.wikipedia.org/wiki/ASCII_Express), [Spectrum](https://speccie.uk/software/spectrum/)) with a Super Serial Card or an [Uthernet II](https://a2retrosystems.com/products.htm). Classic Mac ([ZTerm](https://www.dalverson.com/zterm/)), and anything since through the built-in telnet.
- **Tandy and friends.** TRS-80 Color Computer ([Greg-E-Term](https://colorcomputerarchive.com/repo/Disks/Applications/Greg-E-Term%20%28Greg%20Miller%29.zip)), [Model 100 and 102](https://en.wikipedia.org/wiki/TRS-80_Model_100) with their built-in TELCOM, MSX ([UNAPI TELNET](https://github.com/ducasp/MSX-Development/tree/master/UNAPI/TELNET)), ZX Spectrum ([VTX 5000](https://spectrumcomputing.co.uk/entry/11152/ZX-Spectrum/VTX_5000_User_To_User_Communications_Software), Spectranet), Amstrad CPC.
- **Amiga.** [NComm](https://aminet.net/package/comm/term/ncomm307), [term](https://aminet.net/package/comm/term/Term), [JR-Comm](https://archive.org/details/JR-Comm_v1.02_1991_Radigan_John), [A-Talk](https://archive.org/details/A-Talk_III_v1.0e_1986_Felsina_Software), or a TCP/IP stack and a telnet client.
- **CP/M and S-100.** Kaypro, Osborne, Altair, Northstar: anything running [Kermit](https://www.kermitproject.org/cpm.html) or [MEX](http://www.zimmers.net/anonftp/pub/cpm/comm/mex/index.html) through its serial port.
- **DOS and Windows.** [Telix](https://en.wikipedia.org/wiki/Telix), [Procomm Plus](https://en.wikipedia.org/wiki/Datastorm_Technologies), [Qmodem](https://en.wikipedia.org/wiki/Qmodem), [Terminate](https://en.wikipedia.org/wiki/Terminate_%28software%29), [Telemate](https://archive.org/details/msdos_festival_TM421-1), and today [SyncTERM](https://syncterm.bbsdev.net/), [NetRunner](https://www.mysticbbs.com/downloads.html), [mTelnet](https://mt32.bbses.info/) and [PuTTY](https://www.chiark.greenend.org.uk/~sgtatham/putty/). Windows Terminal is a host, not a telnet client: turn the optional Telnet Client feature on first.
- **Real glass terminals.** [VT100](https://en.wikipedia.org/wiki/VT100), [VT220](https://en.wikipedia.org/wiki/VT220), [VT320](https://en.wikipedia.org/wiki/VT320), [Wyse WY-60](https://terminals-wiki.org/wiki/index.php/Wyse_WY-60), [Televideo 925](https://terminals-wiki.org/wiki/index.php/TeleVideo_925), [ADM-3A](https://en.wikipedia.org/wiki/ADM-3A), [IBM 3151](https://terminals-wiki.org/wiki/index.php/IBM_3151), [Heathkit H19](https://terminals-wiki.org/wiki/index.php/Heathkit_H19). A [Lantronix device server](https://www.lantronix.com/lantronix-products/network-infrastructure/serial-to-ethernet-device-servers/), an [ESP-Link](https://github.com/jeelabs/esp-link) or a USB serial adapter puts them on the board, and the serial bridge plugin goes the other way.
- **Teleprinters.** An [ASR-33](https://en.wikipedia.org/wiki/Teletype_Model_33) through a current loop converter, at 110 baud, if that is the sort of thing you enjoy. `BAUD` slows the board down to match.
- **Unix and everything modern.** [telnet](https://www.gnu.org/software/inetutils/) and [nc](https://man.openbsd.org/nc.1) on Linux and BSD, [SyncTERM](https://syncterm.bbsdev.net/) anywhere, and `nc` or `brew install inetutils` on macOS, which has not shipped telnet since High Sierra.
- **The phone in your pocket.** [TERMinator](https://play.google.com/store/apps/details?id=com.terminator.android) on Android or [iOS](https://apps.apple.com/us/app/terminator-bbs-terminal/id6759012939) is built for BBSes and handles CP437 art; [MuffinTerm](https://apps.apple.com/us/app/muffinterm/id1583236494) on iOS and macOS speaks ANSI, PETSCII and ATASCII.

Terminal type, character set and width are detected at connect time: ANSI with CP437 or UTF-8, PETSCII at 40 or 80 columns, or plain ASCII. Nothing needs configuring at the caller's end.

## Hardware integration

The reference board is a bare ESP32-WROOM-32E: 520 KB of SRAM, 4 MB of flash, no PSRAM, Bluetooth switched off. Any ESP32 module with the same flash size will do. A dev board with a USB-serial chip needs nothing but the cable; a bare module needs 3V3, ground, EN pulled up, GPIO0 to ground for flashing, and a USB-serial adapter on the console pins.

Power: it runs from the USB port of the machine you flash it with, from a phone charger, or from 3V3 on a bench supply. Draw is roughly 100 mA, because the board keeps the radio awake (Wi-Fi power save is off, so a caller never waits for it to wake), with short peaks of about 250 mA when it transmits. Anything that can deliver 500 mA is comfortable.

A carrier PCB with the module, a level shifter and screw terminals is the obvious next step. Not today.

### Other boards

A board other than the reference is a profile, not a fork: a PlatformIO environment that defines the board, and its defaults in `src/board.h`. Whatever only that board has is compiled only into its image, so the WROOM's build carries none of it. One so far (1.1.0), described with its pins and its measured memory in [ESP32_BOARD_CHOICE.md](ESP32_BOARD_CHOICE.md):

- **Waveshare ESP32-S3-LCD-1.47** (`pio run -e ws_s3_lcd147`), the USB-A stick with an ESP32-S3R8, 16 MB of flash, 8 MB of PSRAM, a TF slot, one RGB pixel and a 1.47" display. The pixel is the drive light, the TF slot is the SD card, and the display is a status panel laid out like a phone's status bar: the board's name, where to dial and the uptime with the card's free space turning in the bar, status glyphs (a ring, the sysop's mail, uploads waiting, the listing, staff on), the Wi-Fi signal and the clock under it, who is on and the recent logins and logoffs, and the lights' strip as a row of square LEDs. `CONFIG panel` has its pins and geometry, and `PANEL` says what it is showing. The console, flashing and Improv all go through the chip's own USB, because the stick has no USB-serial bridge.

### Getting it on Wi-Fi

- The network lives in `system.cfg` on the `userdata` partition as `wifi_ssid` and `wifi_password`, so it survives a reflash. SSIDs are case sensitive. The values are taken as typed, so a `#` in a passphrase is part of it rather than a comment.
- The easy way to set it is Improv Wi-Fi Serial: with the board on USB, open a page that speaks Improv (ESP Web Tools after a flash, or the tester at <https://www.improv-wifi.com>) in Chrome or Edge on a desktop, pick the network, type the passphrase. The board tries it for 30 seconds and saves it only if it joins; otherwise it goes back to the network it had. Improv listens on the console port for as long as the board runs, so a board that has moved house is fixed with the same cable.
- Opening the port in ESP Web Tools resets the board, and the installer gives up on Improv 1.5 seconds after it first asks. The board answers from early in its boot (1.1.0), so an installer pointed at a board already running this firmware sees its name and version and offers Update. It answers "not on the network yet" at that point, because it is still joining; once it joins it says so unasked, and the dialog's Connect to Wi-Fi becomes Change Wi-Fi. The telnet link in that dialog needs the dialog opened again, because the installer drops a link it did not ask for.
- `CONFIG network` changes it from the board (`CONFIG wifi` still works), used from the next restart. Never live: changing the network under a telnet session would drop the sysop who changed it, and a typo would leave nobody on the board to fix it.
- A network that has never joined gets a minute (1.1.0). The board keeps the last network that did join in `userdata/wifi.last`, and when the one it dials at boot is a different one and has not joined within 60 seconds, it goes back to the one that worked and says so on the console. A typo in `CONFIG network` costs a minute rather than a trip with a cable. The new network is tried again at every boot until it is corrected, so fix it in `CONFIG network` once you are back on. The record is the board's own: not a setting, not in `system.cfg`, not in a backup. A board that has never joined anything keeps dialling what it has, as before, and Improv's own try-and-go-back is unchanged.
- A board with no network set says so on the console every 30 seconds and waits for Improv. `include/secrets.h` is now optional: a developer's build can still carry a network there as a fallback, used only when `system.cfg` has none, and a published binary carries nobody's.
- The board scans every channel and joins the strongest access point with that name, so a mesh or a pair of repeaters needs no extra configuration.
- `hostname` in `system.cfg` sets both the DHCP hostname and the mDNS name, so `unleashed.local` finds the board on a normal home network without hunting for its address.
- The clock comes from NTP at boot, and the time zone is a `system.cfg` setting. The board runs fine without either; only the log timestamps and time limits care.
- `SYS` shows the SSID, the channel, the signal in dBm with a plain word for what that means, and the address the board answers on. If callers are dropping, look there first: anything past about -75 dBm is a marginal link, and a board in a metal case is a board with a bad antenna.
- The dial-in port is 6400 by default, and `port` on the same `CONFIG network` page moves it, from the next restart (never to the backup window's port). mDNS, `SYS`, the console's dial-in line, Improv's telnet link and the directory listing follow it. Forwarding it from a router is what puts the board on the internet, which is a decision to make deliberately: the protocol is plain telnet and the passwords cross the wire in the clear. If the router forwards a different outside number, put that in announce's Outside so the directory lists what callers can actually dial.

### Serial

The second UART is wired to the serial bridge plugin, so a caller with permission can drive a device attached to the board and everyone else can watch. The console UART is never touched: flashing and the serial monitor keep working while somebody is using the bridge.

- Pins, baud rate and line format are `[plugin:serial]` settings, not compiled in, and `CONFIG serial` sets all four from the board (1.1.0). A pin the console, the SD card or anything else already holds is refused there. `SERIAL SET 9600 8N1` changes the line while the board is running.
- The plugin refuses the pins that would break the board: the flash pins (6 to 11), pins the chip does not have (20, 24 and 28 to 31 on the WROOM), the console pins, and transmit on the input-only pins (34 to 39).
- The ESP32 speaks 3V3 logic. A 5 V device needs a level shifter, and anything with a real RS-232 port needs a transceiver such as a MAX3232. Wiring a bare RS-232 line to a GPIO pin destroys the pin.
- This is what puts a glass terminal, a piece of test equipment, a radio, a PLC or a label printer on the board. It is also how the board itself can be reached from a terminal that has no network at all.

### GPIO and the physical world

- GPIO2 drives the activity LED by default, which is the LED already fitted to most dev boards. `activity_led_gpio` moves it.
- GPIO0, the BOOT button on a dev board, opens the backup window while the sysop is logged in. Hold it, and `system.cfg`, the accounts and the screens can be downloaded or uploaded over HTTP for a few minutes. For the first 10 seconds after the board starts it is the reset button instead: see [Resetting the board](#resetting-the-board).
- The remaining pins are free. The planned GPIO plugin exposes them to callers as commands with their own read, write and admin levels, so reading a sensor can be open to everyone while throwing a relay is staff only.
- Reserved by the hardware, not by this firmware: 6 to 11 are the flash, 20, 24 and 28 to 31 do not exist on the WROOM's chip (every pin setting refuses both, 1.1.0), 34 to 39 are input only and have no pull-ups, and the strapping pins (0, 2, 12, 15) decide how the chip boots and should be left alone unless you know what they do at reset.
- Anything switching mains, motors or an inductive load belongs behind a relay module or an opto-isolated driver with its own supply, not on a board that also has to keep six telnet sessions alive.

### Putting a retro machine on it

Old hardware reaches the board in one of three ways:

- A Wi-Fi modem emulator in the machine's own serial or cartridge port, which answers `ATDT` and opens a telnet session. TeensyROM, WiModem232, Comet64, Zimodem and FujiNet all do this.
- A serial-to-telnet bridge on the network, with a null modem cable to the machine's RS-232 port. An ESP-Link board, a Lantronix, or a small Linux box all work.
- The serial bridge plugin on this board, going the other way, for a terminal that has nothing but a serial port.

## Status

Commands come from a registry (`Command` tables); the menus, dispatch and permissions are generated from it, and plugins register into it. On hardware: PuTTY and a C64 through TeensyROM have both called in, every PETSCII glyph is verified on the C64, and NTP, mDNS and the backup window all come up on boot.

This section used to carry a per-version snapshot (host test counts, image size, heap free) and it went eleven versions without being touched, which is exactly the kind of number nobody should trust on sight. That detail belongs in [CHANGELOG.md](CHANGELOG.md), which is kept current with every build, and in the size reports under `internal/` for whoever wants the measured RAM and flash figures rather than a stale one.

### First boot: the sysop password

- A new board has one published default password, the sysop's: `unleashed`. It is used while `system.cfg` has no `sysop_password` line, or one that spells `unleashed` out, which since 1.1.0 reads as the same thing (a restore on 1.0.0 or 1.0.1 could write one, and such a board heals at its next boot). It works only from the board's own network (RFC 1918, link-local, loopback and `100.64/10`). From anywhere else it is a wrong password.
- Log in or sign up from a computer on the same network and the board asks for it straight away: "This board has not been set up yet." The right password makes you the sysop, shows a short setup screen, opens `CONFIG staff` to choose your own, then gives a paged tour of the rest of CONFIG. ESC (the left arrow on a Commodore) skips, and `BYE <password>` from the same network does it later.
- The board refuses the published default as anybody's chosen password, and it keeps itself out of the directory listing until the default is changed.
- Do not forward the port or switch on the directory listing until you have set your own. "Local only" is a guard, not a wall: a router that rewrites the source of forwarded traffic can make an outside caller look local.

### Resetting the board

The BOOT button can put a board right without a reflash. Press and let go of RESET, then press BOOT and hold it. What happens depends on how long you hold it, and happens when you let go:

- under 7 seconds, nothing;
- 7 to 15 seconds, the sysop password goes back to the published default, `unleashed`, which works only from your own network until you change it. Accounts, settings, mail and Wi-Fi are kept. While the password is the default, the board keeps itself off the directory;
- 15 to 20 seconds, a factory reset: the accounts, the settings, the Wi-Fi, the mail kept on the board and the caller log are erased. The screens, the firmware and the SD card are not touched;
- 20 seconds or more, nothing: the reset is abandoned.

The activity LED shows the stage while you hold: a slow blink under 7 seconds, fast flashing from 7, solid from 15, and off at 20. A board with no LED keeps the same timings, and the serial console says each stage as it arrives.

**A factory reset also costs the board its directory listing.** The listing is tied to a token the directory gave the board, and the token is kept in the settings, so after the reset the directory has no way to know it is the same board. Switched back on, it is listed as a new one: on unleashedbbs.com that means three hours before it appears, as the first time, while the old entry shows as offline and is dropped after a week of silence. Restoring a backup taken before the reset brings the token back with the settings. Back within four days, the listing carries on where it was; within a week it keeps its entry but waits the three hours again. A backup never holds the staff passwords, so set the sysop password again straight after restoring.

For developers, what the copy above leaves out:

- The press has to start within 10 seconds of the firmware starting, a little more than 10 seconds after RESET is let go, since the bootloader runs first. After that BOOT is the backup window's button again. Never hold BOOT while pressing or letting go of RESET: GPIO0 low at that moment is the chip's download mode, and the firmware never runs.
- Both resets restart the board once they are done. The reason goes into `reboots.log` as the reason for that boot, `password reset by BOOT` or `factory reset by BOOT`, or `factory reset FAILED` when the erase did not finish (1.1.0; it used to read as a plain software restart), and every staff login until the next restart, on the sysop node or a co-sysop's own line, is told `Last restart: <reason>.` and a second line on what it touched (`Sysop password only. Accounts are kept.`). `SYS` has a `Last restart` row for every boot.
- After a password reset, a caller on the board's own network is told `The sysop password was reset with the BOOT button.` rather than the new board's "not set up yet", then asked for the default. On that path the setup screen clears the screen, so the restart notice is given at the first prompt after the setup instead.
- The password reset removes the `sysop_password` line from `system.cfg` rather than writing the default into it: a board on the default is exactly one with no such line (1.0.2). The factory reset erases the `userdata` and `logs` partitions whole, not just their directories, so no old account hashes stay readable on the chip; the next boot formats them. The console's last line before the restart says what comes next: a release has no network left and waits for Improv, and a dev build with `include/secrets.h` falls back to that network (the choice is made at compile time, in `src/core/netfallback.h`, the header that also supplies the network).
- The timing is `recovery::BootHold` in `src/core/recovery.h`, tested at every boundary by `host/test_recovery.cpp`; `tools/testclient.py`'s `test_boot_hold` runs each band on a copy of a harness board, with `BBS_BOOT_HOLD_MS` playing the hold on the host build's simulated clock.

### When the board wedges

The task watchdog restarts a board whose BBS loop has stopped for 30 seconds, whether it is spinning or stuck waiting (1.1.0; before that the watchdog only printed a warning and a wedged board stayed wedged). `reboots.log` and `SYS` say `task watchdog`; staff logging in are told `Last restart: the board froze.`, `A watchdog restarted it after 30 s.` and how many unexpected restarts (crashes, watchdogs, brownouts) `reboots.log` holds, this one included. `pio run -e esp32dev_wdttest -t upload` is a bench build that stops its own loop a minute after starting, to see it happen.

## Releases

- Tag `v<BBS_VERSION>` and `.github/workflows/release.yml` builds the release on a fresh checkout with `tools/release.py` and publishes it as a GitHub Release. The directory server's `deploy/update.sh` fetches the newest one, checks every file against `SHA256SUMS`, and `/install` offers it.
- `python3 tools/release.py --allow-dirty` builds the same thing locally, to test before tagging. It writes `release/<version>/assets/` (the GitHub Release files) and `release/<version>/install/` (the directory server's `firmware/<version>/` layout, for copying across by hand).
- A release never carries anybody's network or passwords: the `esp32dev_release` environment ignores `include/secrets.h`, the screens image is built from `data/screens` only, and `release.py` searches every image for any password or network name the machine knows and refuses on a match.

## Build and flash (PlatformIO)

First time on a board:

```bash
cp data/system.cfg.example data/system.cfg       # timezone, passwords, limits
pio run -t flashall                              # firmware + partition table + data/
pio device monitor                               # then set Wi-Fi with Improv, see above
```

After that:

- `pio run -t upload` for new firmware. Config, screens and logs stay as they are.
- Config and screens change through the backup window ([BACKUP.md](BACKUP.md)), not by reflashing. With an SD card, `BACKUP SD` and `RESTORE SD` keep the same zip on the card from the sysop's prompt, `nightly = yes` on `CONFIG sd` makes one every night, and the sysop's `Backups` file area (`FILES 11`) downloads and uploads them over the line. A restore waits until nobody else is on. `SCREENS` lists every screen and where callers get it from.
- `flashall` / `uploadfs` rewrite the `storage` partition with `data/`, which is the screens. The accounts and the config are on `userdata` and stay put, so reflashing a board is no longer a reset.

Flash layout (4 MB): two 1.5 MB OTA app slots and three data partitions.

| Partition | Size | Holds | Rewritten by `uploadfs`? |
|---|---|---|---|
| `logs` | 32 KB | the caller log | no |
| `userdata` | 608 KB | `users.txt`, `system.cfg`, plugin files | no |
| `storage` | 256 KB | screens | **yes** |
| SD card | optional | file areas, message boards, your own screens, a long caller log | no |

`storage` is deliberately last, because PlatformIO's `uploadfs` writes the last
spiffs partition. That is what makes flashing safe: a filesystem upload can only
reach the screens. Accounts, configuration, chat mail and a directory listing
token all sit on `userdata` and survive a reflash.

| Command | Firmware | Screens | Keeps accounts and config |
|---|---|---|---|
| `pio run -t upload` | ✅ | no | ✅ |
| `pio run -t flashall` | ✅ | ✅ | ✅ |

On a blank board, `system.cfg` is seeded once from the copy shipped with the
screens, then belongs to the board. Changing `partitions.csv` itself moves the
filesystems and needs a full erase, so back up first.

The console prints `online <ip>  dial in: telnet <ip> <port>`, 6400 unless `port` says otherwise. On the LAN the board answers as `<hostname>.local` (default `unleashed.local`, also its DHCP name) and advertises `_telnet._tcp`.

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
src/core/bus.*            per-session message ring (PAGE, notices, broadcast, a ring)
src/core/ring.h           OPERATOR's rules: rate limits and the notes file format
src/core/bbs_ring.cpp     OPERATOR: ringing for the sysop, answering, missed rings to MAIL or notes
src/core/guard.*          IP ban list, per-handle login lockout
src/core/users.*          accounts in users.txt: field table, lookup, rewrite, password hashing
src/core/sha256.*         SHA-256 for password hashes
src/core/form.*           fill-in form widget (positional on ANSI/PETSCII, line prompts on ASCII)
src/core/calllog.*        caller log ring file on the logs partition (LAST)
src/core/clock.*          wall clock formatting (NTP)
src/core/sysconfig.*      system.cfg loader, validator, password redaction, access matrix
src/core/backup.*         backup window: button, HTTP in the BBS loop, Y/N approval; the card jobs
src/core/ziparc.*         backup zip export (stored) and import (stored/deflate, staged)
src/core/bbs_backup.cpp   BACKUP SD, RESTORE SD and the nightly backup, as the sysop sees them; a restore waiting for a quiet board
src/core/bbs_screens.cpp  SCREENS and SCREENS VIEW: every screen and where callers get it from
src/core/cardnames.h      the backups' names on the card, and which nightly one to prune
src/core/tzones.h         the timezones CONFIG board offers by name
src/core/crc32.h          CRC-32 for the zip
src/core/bbs.*            listener, sessions, flow, timers, paging
src/core/bbs_shell.cpp    caller commands
src/core/bbs_sysop.cpp    sysop node and commands
src/core/bbs_users.cpp    sign-up, PROFILE, PASSWORD, WHOIS, USERS manager, USER ADD/EDIT/DEL
src/core/plugin.*         plugin API: registry, config sections, levels, storage
src/plugins/              the plugins compiled into this firmware
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

## License

µnleashed BBS is free software under the GNU General Public License, version 3
or later. The full text is in [LICENSE](LICENSE), and every source file carries
the notice.

Copyright 2026 - Robert Mech

The firmware also contains Apache-2.0, MIT and BSD-licensed software from
Espressif and others. Anyone handing out a compiled binary has to pass those
notices along: they are collected in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## How it was built

Parts of the code and the site were developed with the help of AI tools,
including Claude and ChatGPT. The design, the decisions and the copyright
are Robert Mech's.

## Next

- An encrypted option alongside plaintext telnet, so a caller who can do better than a C64 can (SSH, not TLS: no certificate to renew on a board meant to sit on a shelf for a year).
- Doors on a second ESP32 over the serial port, rather than a scripting runtime in the core.
- OTA updates and browser flashing with ESP Web Tools.
