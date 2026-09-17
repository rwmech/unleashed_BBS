# ===========================================================================
#  µnleashed BBS
#  Electronic freedom on a microcontroller.
# ===========================================================================
#
# File:         CMakeLists.txt
# Module:       Build / ESP-IDF project root
#
# Purpose:      ESP-IDF project root (used by PlatformIO and idf.py).
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

cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(unleashed-bbs)
