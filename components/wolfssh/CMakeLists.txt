# ===========================================================================
#  µnleashed BBS
#  Electronic freedom on a microcontroller.
# ===========================================================================
#
# File:         components/wolfssh/CMakeLists.txt
# Module:       Third party / wolfCrypt 5.9.4 and wolfSSH 1.5.0
#
# Purpose:      The SSH server's library (1.1.2), compiled for the ESP32-S3
#               only. Every other target registers an empty component, so
#               the WROOM's, the Freenove's and the ESP32-CAM's images carry
#               none of it. The sources are the subset of the two upstream
#               release tags (v5.9.4-stable and v1.5.0-stable) that
#               user_settings.h compiles, vendored unchanged; each keeps its
#               wolfSSL Inc. notice (GPLv3 or later in every file header).
#               THIRD_PARTY_NOTICES.md lists them.
#
# Targets:      ESP32-S3 (ESP-IDF 5.3.1)
# See also:     user_settings.h, host/Makefile (the host's copy)
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

if(IDF_TARGET STREQUAL "esp32s3")
    set(WC "${CMAKE_CURRENT_LIST_DIR}/wolfssl/wolfcrypt/src")
    set(WS "${CMAKE_CURRENT_LIST_DIR}/wolfssh/src")
    # misc.c of each is #included by the others (inline), never compiled alone.
    set(wolf_srcs
        ${WC}/aes.c ${WC}/asn.c ${WC}/coding.c ${WC}/curve25519.c ${WC}/ecc.c
        ${WC}/ed25519.c ${WC}/error.c ${WC}/fe_low_mem.c ${WC}/ge_low_mem.c
        ${WC}/hash.c ${WC}/hmac.c ${WC}/kdf.c ${WC}/logging.c ${WC}/memory.c
        ${WC}/random.c ${WC}/sha256.c ${WC}/sha512.c ${WC}/signature.c
        ${WC}/sp_c32.c ${WC}/sp_int.c ${WC}/wc_encrypt.c ${WC}/wc_port.c
        ${WC}/wolfmath.c
        ${WS}/internal.c ${WS}/io.c ${WS}/log.c ${WS}/port.c ${WS}/ssh.c)
    idf_component_register(SRCS ${wolf_srcs}
                           INCLUDE_DIRS "." "wolfssl" "wolfssh"
                           REQUIRES lwip esp_timer esp_hw_support freertos
                           PRIV_REQUIRES esp_rom soc hal)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC WOLFSSL_USER_SETTINGS)
    # Upstream code, built as it ships: its own warnings are not ours to fix,
    # and -Werror from the IDF would stop the build over them.
    target_compile_options(${COMPONENT_LIB} PRIVATE -w)
else()
    idf_component_register()
endif()
