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
LDLIBS   += -lz
ifeq ($(SAN),1)
CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS  += -fsanitize=address,undefined
endif
SRCS      = $(wildcard ../src/core/*.cpp) platform_host.cpp main_host.cpp

bbs_host: $(SRCS) $(wildcard ../src/core/*.h) ../src/config.h ../src/platform/platform.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LDLIBS)

clean:
	rm -f bbs_host

.PHONY: clean
