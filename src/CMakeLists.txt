# ===========================================================================
#  µnleashed BBS
#  Electronic freedom on a microcontroller.
# ===========================================================================
#
# File:         src/CMakeLists.txt
# Module:       Build / application component
#
# Purpose:      ESP-IDF component for the application sources.
#               PlatformIO registers src/ as an extra component (not "main"),
#               so IDF dependencies are listed explicitly. LittleFS comes from
#               the Component Manager (src/idf_component.yml, requirement is
#               injected automatically) or from a vendored copy in components/.
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

FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

set(app_requires esp_wifi esp_netif esp_event nvs_flash esp_timer esp_hw_support heap lwip
                 esp_driver_gpio esp_driver_uart esp_rom)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../components/esp_littlefs")
    list(APPEND app_requires esp_littlefs)     # vendored fallback
endif()

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "." "../include"
                       REQUIRES ${app_requires})
