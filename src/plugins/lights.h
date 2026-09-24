/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/lights.h
 * Module:       Plugins / lights
 *
 * Purpose:      The lights plugin's word lists, in one place for the two
 *               things that read them: the plugin's parser, and CONFIG,
 *               which draws a pixel's Effect and Colour as cycle fields on
 *               a sub-page of its own (kComposites in bbs_sysop.cpp). Two
 *               copies of a list are two lists the day somebody adds a word
 *               to one of them, and the symptom would be a value CONFIG
 *               offers and the plugin quietly declines.
 *
 *               The order of each list is the order the form steps through
 *               it, and a word's position is what the plugin stores, so a
 *               word is only ever appended. Nothing on disk holds a number:
 *               system.cfg holds the words.
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     src/plugins/lights.cpp, COMMANDS.md
 *
 * Copyright 2026 - Robert Mech
 * License:      GNU General Public License v2 or later
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <https://www.gnu.org/licenses/>. The full
 * text is in the LICENSE file at the top of this repository.
 * ===========================================================================
 */
#pragma once
#include <cstdint>

namespace lights {

// The strip is ten pixels, one per caller line in nodes mode.
constexpr uint8_t kPixels = 10;

// Drive light styles. pc is the default.
constexpr char kDriveFx[] = "pc|1541|disk2|breathe|off";

// Strip effects. nodes is the default.
constexpr char kStripFx[] = "nodes|hayes|blinken|scanner|c64|boing|vu|rainbow|manual|off";

// Manual mode, one pixel: an effect and a colour. An unset pixel is solid
// and cycling, which is what a strip switched to manual on a fresh board
// shows: all ten pixels, gently out of step with each other.
constexpr char    kLedFx[]       = "solid|blink|breathe|flicker|sparkle|traffic|node|off";
constexpr char    kColours[]     = "red|orange|amber|yellow|green|cyan|blue|purple|pink|white|random|cycle";
constexpr uint8_t kLedFxDefault  = 0;     // solid
constexpr uint8_t kColourDefault = 11;    // cycle

} // namespace lights
