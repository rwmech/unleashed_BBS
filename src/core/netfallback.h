/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/netfallback.h
 * Module:       Core / the compiled-in Wi-Fi fallback
 *
 * Purpose:      Whether this build carries a network of its own, and which.
 *
 *                  A developer's build may carry a network in
 *                  include/secrets.h as a fallback for when system.cfg names
 *                  none. A published binary never does: BBS_RELEASE (the
 *                  esp32dev_release environment) leaves the file out even
 *                  on a machine that has it, and the host build has no
 *                  include/ on its path at all.
 *
 *                  One header so every file that needs to know agrees.
 *                  main.cpp dials WIFI_SSID; core/recovery says after a
 *                  factory reset whether the board will come back on it
 *                  (kBuiltinWifi). The decision used to live in main.cpp
 *                  alone, where recovery could not see it, so the console
 *                  said "restarting with no Wi-Fi" on a build that rejoined
 *                  its compiled-in network seconds later.
 *
 *                  A secrets.h that defines an empty WIFI_SSID is not a
 *                  fallback: kBuiltinWifi is keyed on the name, not on the
 *                  file being there.
 *
 * Interfaces:   WIFI_SSID, WIFI_PASS, kBuiltinWifi
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     README.md, include/secrets.h.example
 *
 * Copyright 2026 - Robert Mech
 * License:      GNU General Public License v3 or later
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>. The full
 * text is in the LICENSE file at the top of this repository.
 * ===========================================================================
 */

#pragma once

#if !defined(BBS_RELEASE) && __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

// kBuiltinWifi: this build falls back to a network of its own when
// system.cfg names none. sizeof, so only the name's length is compiled into
// a file that asks, never the name itself.
constexpr bool kBuiltinWifi = sizeof(WIFI_SSID) > 1;
