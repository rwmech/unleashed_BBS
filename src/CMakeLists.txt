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
                 esp_driver_rmt)
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
# The camera boards' watermark re-encodes with the camera driver's own JPEG
# encoder (conversions/jpge), whose header the driver keeps private. The
# driver is in every ESP32 and S3 build (src/idf_component.yml) and linked
# only by a camera board's code.
if(TARGET __idf_espressif__esp32-camera)
    idf_component_get_property(bbs_cam_dir espressif__esp32-camera COMPONENT_DIR)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${bbs_cam_dir}/conversions/private_include")
endif()
