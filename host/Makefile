# ===========================================================================
#  µnleashed BBS
#  Electronic freedom on a microcontroller.
# ===========================================================================
#
# File:         host/Makefile
# Module:       Build / Linux host test build
#
# Purpose:      Builds the BBS core as a Linux program for testing.
#               make && ./bbs_host ../data 6400
#               make SAN=1   AddressSanitizer + UBSan build for pen testing
#               BBS_BACKUP_TEST_OPEN=1 ./bbs_host ...  holds the backup button
#               BBS_BOOT_HOLD_MS=8000 ./bbs_host ...   plays a BOOT hold (1.1.0)
#               BBS_HOST_WIFI=never|up=<ms> ./bbs_host ...  plays the Wi-Fi fallback
#
# Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
# See also:     README.md
#
# Copyright 2026 - Robert Mech
# License:      GNU General Public License v3 or later
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>. The full
# text is in the LICENSE file at the top of this repository.
# ===========================================================================

.DEFAULT_GOAL := bbs_host
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -I../src
# BBS_HOST: the host build, for the few test hooks that must never reach a
# board (announce's room_test). Nothing may change behaviour on it otherwise.
CPPFLAGS += -DBBS_HOST
LDLIBS   += -lz -lpthread
CC       ?= gcc
ifeq ($(SAN),1)
CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS  += -fsanitize=address,undefined
WOLF_SAN  = -fsanitize=address,undefined -fno-omit-frame-pointer
endif

# wolfCrypt 5.9.4 and wolfSSH 1.5.0 (1.1.2), from the S3's own vendored copy
# (components/wolfssh), for the profiles that have SSH. Built as upstream
# ships it, warnings and all, with the same user_settings.h as the board
# (which keeps wolfSSH's client on the host, for ssh_call).
WOLF          = ../components/wolfssh
WOLF_CPPFLAGS = -DWOLFSSL_USER_SETTINGS -I$(WOLF) -I$(WOLF)/wolfssl -I$(WOLF)/wolfssh
WOLF_WC       = aes asn coding curve25519 ecc ed25519 error fe_low_mem ge_low_mem hash hmac kdf \
                logging memory random sha256 sha512 signature sp_c32 sp_int wc_encrypt wc_port wolfmath
WOLF_WS       = internal io log port ssh
WOLF_OBJS     = $(patsubst %,wolfobj/wc_%.o,$(WOLF_WC)) $(patsubst %,wolfobj/ws_%.o,$(WOLF_WS))
SRCS      = $(wildcard ../src/core/*.cpp) $(wildcard ../src/plugins/*.cpp) platform_host.cpp linkradio_host.cpp main_host.cpp \
            jpeg_host.cpp

# ---------------------------------------------------------------------------
# mbedTLS for the host (1.2.0). The link (src/core/linkcrypto.cpp) seals its
# frames with AES-CCM and pairs with P-256 ECDH over mbedTLS, which the board
# gets from ESP-IDF. The host has no mbedTLS of its own, so it compiles the
# very sources ESP-IDF 5.3.1 ships (mbedTLS 3.6.0) out of the PlatformIO
# package, with mbedTLS's own default configuration: the same code the board
# runs, in software rather than on the AES and SHA hardware. Built once into
# obj-mbedtls/ and libmbedtls_host.a, then only linked.
#
# MBEDTLS_DIR overrides where it is looked for:
#   make MBEDTLS_DIR=/path/to/mbedtls bbs_host
# ---------------------------------------------------------------------------
MBEDTLS_DIR ?= $(firstword $(wildcard /mnt/c/Users/*/.platformio/packages/framework-espidf@3.50301.0/components/mbedtls/mbedtls) \
                           $(wildcard $(HOME)/.platformio/packages/framework-espidf@3.50301.0/components/mbedtls/mbedtls))
MBED_SRCS  = $(wildcard $(MBEDTLS_DIR)/library/*.c)
MBED_OBJS  = $(patsubst $(MBEDTLS_DIR)/library/%.c,obj-mbedtls/%.o,$(MBED_SRCS))
MBED_LIB   = libmbedtls_host.a
CPPFLAGS  += -I$(MBEDTLS_DIR)/include

mbedtls-check:
	@if [ ! -f "$(MBEDTLS_DIR)/include/mbedtls/ccm.h" ]; then \
	    echo "host: mbedTLS not found (MBEDTLS_DIR='$(MBEDTLS_DIR)')."; \
	    echo "host: it comes from PlatformIO's framework-espidf@3.50301.0 package: run 'pio run' once,"; \
	    echo "host: or point MBEDTLS_DIR at an mbedTLS 3.6 source tree (the folder holding include/ and library/)."; \
	    exit 1; fi

obj-mbedtls/%.o: $(MBEDTLS_DIR)/library/%.c | mbedtls-check
	@mkdir -p obj-mbedtls
	$(CC) -O2 -I$(MBEDTLS_DIR)/include -I$(MBEDTLS_DIR)/library -c $< -o $@

$(MBED_LIB): $(MBED_OBJS) | mbedtls-check
	@rm -f $@
	ar rcs $@ $(MBED_OBJS)

HDRS      = $(wildcard ../src/core/*.h) $(wildcard ../src/plugins/*.h) ../src/config.h ../src/board.h \
            ../src/platform/platform.h $(EXTGEN)/ext_plugins.h

# ---------------------------------------------------------------------------
# Plugins in their own repositories (1.2.0, LINK.md), the host's side of what
# src/CMakeLists.txt does for the board. tools/plugins.py fetches them into
# ../ext/<name>; EXT names the ones to build in:
#   make EXT="hello" bbs_host
# ext-gen/ext_plugins.h is written every run and replaced only when it
# changes, so the same EXT rebuilds nothing and a different one rebuilds.
# ---------------------------------------------------------------------------
# A build with plugins keeps its generated header in a folder of its own
# (1.2.0): with one folder, a harness run building bbs_host_ext beside runs
# building bbs_host rewrote the header under them, and a plain bbs_host was
# built counting the plugin, so the last shipped plugin (link) was taken
# for an external one and refused.
EXT      ?=
EXTGEN    = $(if $(strip $(EXT)),ext-gen-ext,ext-gen)
EXT_SRCS  = $(foreach n,$(EXT),$(wildcard ../ext/$(n)/bbs/*.cpp))
SRCS     += $(EXT_SRCS)
CPPFLAGS += -I$(EXTGEN)

$(EXTGEN)/ext_plugins.h: FORCE
	@mkdir -p $(EXTGEN)
	@for n in $(EXT); do [ -f ../ext/$$n/unleashed-plugin.ini ] || { echo "host: ../ext/$$n is missing: run tools/plugins.py fetch"; exit 1; }; done
	@{ echo '// Generated by host/Makefile from EXT. Do not edit.'; echo '#pragma once'; \
	   echo '#define BBS_EXT_PLUGIN_COUNT $(words $(EXT))'; \
	   printf '#define BBS_EXT_PLUGINS(X)'; \
	   for n in $(EXT); do \
	       sym=$$(sed -n 's/^descriptor[ \t]*=[ \t]*\([A-Za-z_][A-Za-z0-9_]*\).*/\1/p' ../ext/$$n/unleashed-plugin.ini); \
	       printf ' X(%s)' "$$sym"; \
	   done; echo; } > $(EXTGEN)/ext_plugins.h.new
	@cmp -s $(EXTGEN)/ext_plugins.h.new $(EXTGEN)/ext_plugins.h 2>/dev/null && rm -f $(EXTGEN)/ext_plugins.h.new || mv $(EXTGEN)/ext_plugins.h.new $(EXTGEN)/ext_plugins.h

FORCE:

# ChaN's TJpgDec (host/tjpgd, R0.03, set up as the ESP32-S3's ROM copy is):
# the panel's JPEG decoder on the host (1.2.0, panel skins). C, so compiled
# as C; linked into every host board, used only by those with a panel.
TJPGD     = tjpgd.o
ifeq ($(SAN),1)
# Its own name under the sanitisers, so a later plain build never links an
# instrumented object (undefined __asan_* at the link).
TJPGD     = tjpgd_san.o
TJSAN     = -fsanitize=address,undefined -fno-omit-frame-pointer
endif
$(TJPGD): tjpgd/tjpgd.c tjpgd/tjpgd.h tjpgd/tjpgdcnf.h
	$(CC) -std=c99 -O1 -g -Wall -Wextra $(TJSAN) -c -o $@ tjpgd/tjpgd.c

bbs_host: $(SRCS) $(HDRS) $(TJPGD) $(MBED_LIB)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(TJPGD) $(MBED_LIB) $(LDLIBS)

# The same host board built as the Waveshare ESP32-S3-LCD-1.47 profile
# (1.1.0, src/board.h): the S3's pin rules, that board's defaults, and the
# panel plugin with its glass in memory. tools/harness.sh --board s3 runs it;
# the ordinary suite is written for the reference board and runs bbs_host.
#
# It carries SSH (1.1.2), so it links wolfSSH: libwolfssh_host.a below.
bbs_host_s3: $(SRCS) $(HDRS) $(TJPGD) libwolfssh_host.a $(MBED_LIB)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DBBS_BOARD_WS_S3LCD147 $(WOLF_CPPFLAGS) $(LDFLAGS) -o $@ $(SRCS) \
	    $(TJPGD) libwolfssh_host.a $(MBED_LIB) $(LDLIBS)

# The Waveshare ESP32-S3-Touch-LCD-4.3B profile (1.1.2, WS43B): the RGB
# panel's picture at 400 x 240, touch as a file called "tap" in the data
# directory, and SSH as on the stick. tools/harness.sh --board ws43b runs it
# with --only=board_ws43b.
bbs_host_ws43b: $(SRCS) $(HDRS) $(TJPGD) libwolfssh_host.a $(MBED_LIB)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DBBS_BOARD_WS_S3TOUCH43B $(WOLF_CPPFLAGS) $(LDFLAGS) -o $@ $(SRCS) \
	    $(TJPGD) libwolfssh_host.a $(MBED_LIB) $(LDLIBS)

wolfobj/wc_%.o: $(WOLF)/wolfssl/wolfcrypt/src/%.c $(WOLF)/user_settings.h
	@mkdir -p wolfobj
	$(CC) -c -O1 -g -w $(WOLF_SAN) $(WOLF_CPPFLAGS) -o $@ $<

wolfobj/ws_%.o: $(WOLF)/wolfssh/src/%.c $(WOLF)/user_settings.h
	@mkdir -p wolfobj
	$(CC) -c -O1 -g -w $(WOLF_SAN) $(WOLF_CPPFLAGS) -o $@ $<

libwolfssh_host.a: $(WOLF_OBJS)
	rm -f $@
	ar rcs $@ $(WOLF_OBJS)

# ssh_call: a caller that dials the host board over SSH with wolfSSH's own
# client, for the tests (tools/testclient.py SshCaller). stdin goes to the
# board, the board comes out on stdout. OpenSSH would be the better witness,
# being somebody else's implementation, but this machine does not allow an
# ssh client to run; wolfSSH's client and server are separate code paths.
ssh_call: ssh_call.cpp libwolfssh_host.a
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(WOLF_CPPFLAGS) $(LDFLAGS) -o $@ ssh_call.cpp libwolfssh_host.a -lpthread

# The SSH link's ring, the refusal packet and the fingerprint's encoding
# (1.1.2): the loop and the SSH task meet in the ring, so a lost or doubled
# byte there is a corrupted file transfer.
test_sshlink: test_sshlink.cpp ../src/core/sshlink.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_sshlink.cpp -lpthread

# The Freenove ESP32-WROVER CAM profile (1.1.0, src/board.h): its defaults
# and the pins it owns, which syscfg::pinProblem refuses by name.
# tools/harness.sh --board fncam runs it with --only=board_fncam.
bbs_host_fncam: $(SRCS) $(HDRS) $(TJPGD) $(MBED_LIB)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DBBS_BOARD_FN_WROVER_CAM $(LDFLAGS) -o $@ $(SRCS) $(TJPGD) $(MBED_LIB) $(LDLIBS)

# The Makerfabs ESP32-S3 Parallel TFT 3.5" v1.0 profile (MF35 1.1.0,
# src/board.h): the S3's pin rules with quad PSRAM, that board's defaults and
# the panel on a 480 x 320 ILI9488. It carries SSH (MF35 1.1.0), so it links
# wolfSSH as bbs_host_s3 does.
# tools/harness.sh --board mf35 runs it with --only=board_mf35 (and =ssh).
bbs_host_mf35: $(SRCS) $(HDRS) $(TJPGD) libwolfssh_host.a $(MBED_LIB)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DBBS_BOARD_MF_S3PAR35 $(WOLF_CPPFLAGS) $(LDFLAGS) -o $@ $(SRCS) \
	    $(TJPGD) libwolfssh_host.a $(MBED_LIB) $(LDLIBS)

# The Guition ESP32-4848S040 profile (G4848 1.0.0, on 1.2.0): an RGB panel
# (ST7701S) drawn with the big glass's 480 x 320 layout, touch polled as on the
# 4.3B (a file called "tap" in the data directory), a TF slot, SSH.
# tools/harness.sh --board g4848 runs it with --only=board_g4848.
bbs_host_g4848: $(SRCS) $(HDRS) $(TJPGD) libwolfssh_host.a $(MBED_LIB)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DBBS_BOARD_GT_4848S040 $(WOLF_CPPFLAGS) $(LDFLAGS) -o $@ $(SRCS) \
	    $(TJPGD) libwolfssh_host.a $(MBED_LIB) $(LDLIBS)

# The AI-Thinker ESP32-CAM profile (1.1.1, src/board.h).
# tools/harness.sh --board espcam runs it with --only=board_espcam.
bbs_host_espcam: $(SRCS) $(HDRS) $(TJPGD) $(MBED_LIB)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DBBS_BOARD_AI_ESP32CAM $(LDFLAGS) -o $@ $(SRCS) $(TJPGD) $(MBED_LIB) $(LDLIBS)

# The Waveshare ESP32-S3-Touch-LCD-2 profile (WS2 1.0.0, src/board.h): the
# panel at 240 x 320 with taps (PANEL TAP), the camera, the card on the
# panel's bus, and SSH as on the LCD-1.47. tools/harness.sh --board ws2 runs
# it with --only=board_ws2.
bbs_host_ws2: $(SRCS) $(HDRS) $(TJPGD) libwolfssh_host.a $(MBED_LIB)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DBBS_BOARD_WS_S3TOUCH2 $(WOLF_CPPFLAGS) $(LDFLAGS) -o $@ $(SRCS) \
	    $(TJPGD) libwolfssh_host.a $(MBED_LIB) $(LDLIBS)

# The Waveshare ESP32-S3-ETH profile (ETH 1.0.0, 1.1.2, src/board.h): an S3
# with SSH, so it links wolfSSH as bbs_host_s3 does, and Ethernet, which the
# host plays as a wire with an address (platform_host.cpp, BBS_HOST_ETH).
# tools/harness.sh --board wseth runs it with --only=board_wseth.
bbs_host_wseth: $(SRCS) $(HDRS) $(TJPGD) libwolfssh_host.a $(MBED_LIB)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DBBS_BOARD_WS_S3ETH $(WOLF_CPPFLAGS) $(LDFLAGS) -o $@ $(SRCS) \
	    $(TJPGD) libwolfssh_host.a $(MBED_LIB) $(LDLIBS)

# Panel skins (1.2.0): the manifest's grammar thrown at hard (the shared
# cases in skins/cases.txt, then a fuzz), the JPEG check, the LEDs and the
# lines drawn over a real decoded picture, and the stock set's seeding, with
# no panel. A picture of each skin lands in skins/out/ for a person to look
# at. From the pure headers and the host decoder.
test_skin: test_skin.cpp ../src/plugins/skin_manifest.h ../src/plugins/skin_jpeg.h ../src/plugins/skin_draw.h \
           ../src/plugins/skin_seed.h ../src/plugins/panel_gfx.h ../src/plugins/panel_font.h $(TJPGD)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_skin.cpp $(TJPGD)

# The panel's drawing: glyphs, layout, lamps and the queue of what to send.
# Pure code with no platform, checked pixel by pixel with no display.
test_panel: test_panel.cpp ../src/plugins/panel_gfx.h ../src/plugins/panel_font.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_panel.cpp

# The XMODEM engine has no session, socket or card in it, so it is tested on
# its own: a sender wired to a receiver through a pipe that can corrupt and
# drop bytes. make test runs it.
test_xmodem: test_xmodem.cpp ../src/core/xmodem.cpp ../src/core/xmodem.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_xmodem.cpp ../src/core/xmodem.cpp

# bbsu::wrap has no board in it either: it is a pure function over a string
# and a column count, and every bug in it lives at a margin. Two were found
# the day it was written, both invisible to a reading of the code: a word
# ending exactly on the boundary was thrown onto the next line, and a line
# could end in a space, which on a reverse-video row is a visible notch.
test_wrap: test_wrap.cpp ../src/core/bbs_util.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_wrap.cpp

# The forums read pointer: pure arithmetic over a struct, and the piece of
# that subsystem most likely to be subtly wrong while looking right. Its
# failure is not a crash, it is a board quietly claiming three unread
# messages in a subject somebody just finished.
test_forums_ptr: test_forums_ptr.cpp ../src/plugins/forums_ptr.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_forums_ptr.cpp

# The shared message composer. Every subsystem that takes a body runs this
# code, so a bug here is a bug in all of them at once. That is the cost of
# sharing it and the reason it is tested on its own rather than only through
# whichever subsystem happened to be built first.
test_compose: test_compose.cpp ../src/core/compose.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_compose.cpp

# The inline @-codes. The wrap has to measure what is shown rather than what
# was typed, never split a code, and count the per-message limit exactly as
# the row does; each of those is invisible to a reading of the code and
# shows up only as a row that overflows somebody's C64.
test_codes: test_codes.cpp ../src/core/codes.cpp ../src/core/codes.h ../src/core/fx.cpp ../src/core/term.cpp ../src/core/timeline.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_codes.cpp ../src/core/codes.cpp ../src/core/fx.cpp ../src/core/term.cpp ../src/core/timeline.cpp

# Improv Wi-Fi Serial. The expected bytes are worked by hand in the test,
# not produced by the codec, because a board that only ever talks to itself
# would agree with any mistake it made.
test_improv: test_improv.cpp ../src/core/improv.cpp ../src/core/improv.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_improv.cpp ../src/core/improv.cpp

# The BOOT-hold reset's timing and the Wi-Fi fallback's (1.1.0). Every rule
# is a boundary, and a boundary that is off by one poll resets somebody's
# password when they meant nothing, so each is driven from both sides.
test_recovery: test_recovery.cpp ../src/core/recovery.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_recovery.cpp

# The sysop page's rules (1.1.0): the rate limits at every boundary, the
# notes file's lines read back or refused whole, and the bus queue keeping a
# ring through a burst of other messages.
test_ring: test_ring.cpp ../src/core/ring.h ../src/core/bus.cpp ../src/core/bus.h ../src/config.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_ring.cpp ../src/core/bus.cpp


# The timezone picker's table (1.1.0): one list that is both the rows CONFIG
# looks a TZ string up in and the choices its cycle steps through, so each
# rule that keeps the two in step is checked here.
test_tzones: test_tzones.cpp ../src/core/tzones.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_tzones.cpp

# The names of the backups on the SD card, and the rule that prunes the
# nightly ones (1.1.0): it must never take a backup the sysop made by hand.
test_cardnames: test_cardnames.cpp ../src/core/cardnames.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_cardnames.cpp

# The caller log's copy of its newest calls and today's count in RAM (1.1.0),
# which the dashboard reads every frame instead of the file. Checked against
# the file, then with the file taken away.
test_calllog: test_calllog.cpp ../src/core/calllog.cpp ../src/core/calllog.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_calllog.cpp ../src/core/calllog.cpp

# Silent mode's rules (1.1.0): the switch, the hours at every edge and across
# midnight, no clock, and the times as the file and CONFIG hold them. A rule a
# minute out is a strip lighting up in somebody's bedroom.
test_silent: test_silent.cpp ../src/core/silent.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_silent.cpp

# CONFIG photos' keys against the camera's old lines (1.2.0-link.15): a
# photos_ line wins over [plugin:camera]'s keep, max, floor, tl_keep and
# tl_max in either order in the file, and an old line stands in only where
# the file has no photos_ line. syscfg::parseFile alone, on a temporary file.
test_photos_cfg: test_photos_cfg.cpp ../src/core/sysconfig.cpp ../src/core/sysconfig.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_photos_cfg.cpp ../src/core/sysconfig.cpp

# The camera's rules and its watermark (1.1.0): names, limits, retention,
# the timelapse's schedule, the download offer, the JPEG comment, and the
# watermark drawn a strip at a time, all with no sensor and no card.
test_camera: test_camera.cpp ../src/plugins/camera_rules.h ../src/plugins/camera_mark.h ../src/plugins/panel_font.h \
             ../src/plugins/camera_pic.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_camera.cpp

# The µnleashed link (1.2.0): both ends of the protocol through a simulated
# radio that loses, duplicates and reorders frames and changes channel, the
# wire against a published CRC, and the cryptography against RFC 5869.
test_link: test_link.cpp ../src/core/link.cpp ../src/core/link.h ../src/core/linkcrypto.cpp ../src/core/linkcrypto.h $(MBED_LIB)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ test_link.cpp ../src/core/link.cpp ../src/core/linkcrypto.cpp $(MBED_LIB) -lpthread

# A pretend door box for the host board's link (1.2.0): the peer end of the
# same engine, over the host's UDP radio, so tools/testclient.py can pair a
# device and send a caller through a door with no radio anywhere.
linkpeer: linkpeer.cpp ../src/core/link.cpp ../src/core/link.h ../src/core/linkcrypto.cpp ../src/core/linkfam.h $(MBED_LIB)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ linkpeer.cpp ../src/core/link.cpp ../src/core/linkcrypto.cpp $(MBED_LIB)

# The host board with plugins from their own repositories built in, beside
# bbs_host rather than over it: make EXT="hello" bbs_host_ext
bbs_host_ext: $(SRCS) $(HDRS) $(TJPGD) $(MBED_LIB)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(TJPGD) $(MBED_LIB) $(LDLIBS)

# The project is GPL-3.0-or-later (1.1.0-dev.11). A tracked file whose SPDX
# line still says GPL-2.0 was added on an old header; release.py refuses it
# too, and this catches it before a release is ever attempted.
licence:
	@if git -C .. grep -nE '^[^[:alnum:]]*SPDX-License-Identifier:.*GPL-2\.0'; then \
	    echo "licence: GPL-2.0 SPDX line(s) above; the project is GPL-3.0-or-later"; exit 1; \
	else echo "licence: no GPL-2.0 SPDX lines"; fi

# Every board's printf is newlib nano (CONFIG_NEWLIB_NANO_FORMAT, 1.1.2): no
# %ll, %z, %hh, %j, %t or positional arguments, and floats are refused too.
# The host is glibc and prints all of them, so no host test can see one;
# this refuses them in src/. The self-test first, so a checker that stopped
# seeing %llu cannot pass.
formats:
	@python3 ../tools/check_formats.py --self-test
	@python3 ../tools/check_formats.py ../src

test: licence formats test_link test_xmodem test_wrap test_forums_ptr test_compose test_codes test_improv test_recovery test_ring test_tzones test_cardnames test_panel test_calllog test_silent test_camera test_photos_cfg test_sshlink test_skin
	./test_link
	./test_skin
	./test_xmodem
	./test_wrap
	./test_forums_ptr
	./test_compose
	./test_codes
	./test_improv
	./test_recovery
	./test_ring
	./test_tzones
	./test_cardnames
	./test_panel
	./test_calllog
	./test_silent
	./test_camera
	./test_photos_cfg
	./test_sshlink

clean:
	rm -rf wolfobj libwolfssh_host.a ssh_call test_sshlink
	rm -f tjpgd.o tjpgd_san.o test_skin
	rm -f bbs_host bbs_host_s3 bbs_host_ws43b bbs_host_fncam bbs_host_espcam bbs_host_ws2 bbs_host_wseth bbs_host_mf35 bbs_host_g4848 bbs_host_ext linkpeer test_link test_xmodem test_wrap test_forums_ptr test_compose test_codes test_improv test_recovery test_ring test_tzones test_cardnames test_panel test_calllog test_silent test_camera test_photos_cfg

.PHONY: clean test licence formats FORCE mbedtls-check
