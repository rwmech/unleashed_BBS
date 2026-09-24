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
# License:      GNU General Public License v2 or later
# SPDX-License-Identifier: GPL-2.0-or-later
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <https://www.gnu.org/licenses/>. The full
# text is in the LICENSE file at the top of this repository.
# ===========================================================================

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -I../src
# BBS_HOST: the host build, for the few test hooks that must never reach a
# board (announce's room_test). Nothing may change behaviour on it otherwise.
CPPFLAGS += -DBBS_HOST
LDLIBS   += -lz -lpthread
ifeq ($(SAN),1)
CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS  += -fsanitize=address,undefined
endif
SRCS      = $(wildcard ../src/core/*.cpp) $(wildcard ../src/plugins/*.cpp) platform_host.cpp main_host.cpp

bbs_host: $(SRCS) $(wildcard ../src/core/*.h) $(wildcard ../src/plugins/*.h) ../src/config.h ../src/platform/platform.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LDLIBS)

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

test: test_xmodem test_wrap test_forums_ptr test_compose test_codes test_improv test_recovery
	./test_xmodem
	./test_wrap
	./test_forums_ptr
	./test_compose
	./test_codes
	./test_improv
	./test_recovery

clean:
	rm -f bbs_host test_xmodem test_wrap test_forums_ptr test_compose test_codes test_improv test_recovery

.PHONY: clean test
