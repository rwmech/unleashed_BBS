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

FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

set(app_requires esp_wifi esp_netif esp_event nvs_flash esp_timer esp_hw_support heap lwip
                 esp_driver_gpio esp_driver_uart esp_rom
                 # esp_flash_get_size, for the directory's system badge
                 spi_flash
                 # HARDWARE and SYS (1.1.1): the package fuse, and the PSRAM
                 # chip's size on a board that has one
                 efuse esp_psram
                 # the BOOT-hold factory reset erases two partitions whole
                 esp_partition
                 # The optional SD card: SPI host, card protocol, FAT and the
                 # VFS glue. fatfs is the one that costs image space, so it is
                 # worth remembering it is here if the build ever gets tight.
                 esp_driver_spi esp_driver_sdspi sdmmc fatfs vfs
                 # The lights plugin's two WS2812B outputs: the RMT
                 # peripheral, hardware-timed, so nothing is bit-banged.
                 esp_driver_rmt
                 # The unleashed link (1.2.0): AES-CCM, P-256 ECDH and HKDF.
                 # Already linked for WPA3, so it costs next to no flash.
                 mbedtls)

# ---------------------------------------------------------------------------
# Plugins in their own repositories (1.2.0, LINK.md). BBS_EXT_PLUGINS is the
# environment's list, which tools/pio_plugins.py (a PlatformIO pre-script)
# puts in the environment after fetching each one into ext/<name> at the
# commit plugins.lock pins. Each plugin's bbs/ sources join this component,
# and ext_plugins.h, in the build directory, names their descriptors for
# plugins/registry.cpp. No list: an empty ext_plugins.h and nothing else.
# ---------------------------------------------------------------------------
if(NOT DEFINED BBS_EXT_PLUGINS)
    if(EXISTS "${CMAKE_BINARY_DIR}/ext_plugins.txt")
        # Written by tools/pio_plugins.py; a change to it reconfigures.
        file(READ "${CMAKE_BINARY_DIR}/ext_plugins.txt" BBS_EXT_PLUGINS)
        string(STRIP "${BBS_EXT_PLUGINS}" BBS_EXT_PLUGINS)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_BINARY_DIR}/ext_plugins.txt")
    elseif(DEFINED ENV{BBS_EXT_PLUGINS})
        set(BBS_EXT_PLUGINS "$ENV{BBS_EXT_PLUGINS}")
    endif()
endif()
set(ext_gen_dir "${CMAKE_BINARY_DIR}/ext_gen")
file(MAKE_DIRECTORY "${ext_gen_dir}")
set(ext_count 0)
set(ext_list "")
foreach(ext_name IN LISTS BBS_EXT_PLUGINS)
    set(ext_dir "${CMAKE_CURRENT_LIST_DIR}/../ext/${ext_name}")
    if(NOT EXISTS "${ext_dir}/unleashed-plugin.ini")
        message(FATAL_ERROR "plugin ${ext_name}: ext/${ext_name} is missing; run tools/plugins.py fetch")
    endif()
    file(STRINGS "${ext_dir}/unleashed-plugin.ini" ext_line REGEX "^descriptor[ \t]*=")
    string(REGEX REPLACE "^descriptor[ \t]*=[ \t]*([A-Za-z_][A-Za-z0-9_]*).*" "\\1" ext_sym "${ext_line}")
    if(NOT ext_sym MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR "plugin ${ext_name}: its manifest names no descriptor")
    endif()
    file(GLOB_RECURSE ext_srcs CONFIGURE_DEPENDS "${ext_dir}/bbs/*.cpp" "${ext_dir}/bbs/*.c")
    list(APPEND app_sources ${ext_srcs})
    math(EXPR ext_count "${ext_count} + 1")
    string(APPEND ext_list " X(${ext_sym})")
endforeach()
# Written only when it changes, so an unchanged list rebuilds nothing.
set(ext_text "// Generated by src/CMakeLists.txt from BBS_EXT_PLUGINS. Do not edit.\n#pragma once\n#define BBS_EXT_PLUGIN_COUNT ${ext_count}\n#define BBS_EXT_PLUGINS(X)${ext_list}\n")
file(WRITE "${ext_gen_dir}/ext_plugins.h.new" "${ext_text}")
configure_file("${ext_gen_dir}/ext_plugins.h.new" "${ext_gen_dir}/ext_plugins.h" COPYONLY)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../components/esp_littlefs")
    list(APPEND app_requires esp_littlefs)     # vendored fallback
endif()
# The S3 boards (1.1.0): the console on the chip's own USB, and the panel
# (esp_lcd, with the backlight on LEDC). Named for that target only, so the
# WROOM's image does not take on a component it never calls.
if(IDF_TARGET STREQUAL "esp32s3")
    list(APPEND app_requires esp_driver_usb_serial_jtag esp_lcd esp_driver_ledc)
endif()

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "." "../include"
                       REQUIRES ${app_requires})
target_include_directories(${COMPONENT_LIB} PRIVATE "${ext_gen_dir}")
# The camera boards' watermark re-encodes with the camera driver's own JPEG
# encoder (conversions/jpge), whose header the driver keeps private. The
# driver is in every ESP32 and S3 build (src/idf_component.yml) and linked
# only by a camera board's code.
if(TARGET __idf_espressif__esp32-camera)
    idf_component_get_property(bbs_cam_dir espressif__esp32-camera COMPONENT_DIR)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${bbs_cam_dir}/conversions/private_include")
endif()
