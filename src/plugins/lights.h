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
#include <cstdint>
#include "../platform/platform.h"

namespace lights {

// How many pixels the strip has is a setting (1.1.0, Rob: "could be 8 could
// be 10, could be 1, so variable would be better"). Ten as shipped, one a
// caller line in nodes mode; at most what an output can carry, which is
// plat::kPixelMax (sixteen, and platform.h says why).
constexpr uint8_t kPixelsMax     = plat::kPixelMax;
constexpr uint8_t kPixelsDefault = 10;

// The orders a pixel's bytes go out in, as plat::PixOrder numbers them. GRB
// is the WS2812B's own; the rest are for the strips that are not, and for
// the Waveshare S3's onboard pixel, which appears to want RGB.
constexpr char kOrders[] = "GRB|RGB|BRG|RBG|GBR|BGR";

// Drive light styles. pc is the default.
constexpr char kDriveFx[] = "pc|1541|disk2|breathe|off";

// Strip effects. nodes is the default. wifi was appended in 1.1.0, after
// off: a word's place is the number the plugin works with.
constexpr char kStripFx[] = "nodes|hayes|blinken|scanner|c64|boing|vu|rainbow|manual|off|wifi";

// Manual mode, one pixel: an effect and a colour. An unset pixel is solid
// and cycling, which is what a strip switched to manual on a fresh board
// shows: all ten pixels, gently out of step with each other.
constexpr char    kLedFx[]       = "solid|blink|breathe|flicker|sparkle|traffic|node|off";
constexpr char    kColours[]     = "red|orange|amber|yellow|green|cyan|blue|purple|pink|white|random|cycle";
constexpr uint8_t kLedFxDefault  = 0;     // solid
constexpr uint8_t kColourDefault = 11;    // cycle

#ifdef BBS_HAS_LCD
// ---------------------------------------------------------------------------
// The strip on a board's panel (BBS_HAS_LCD only). The panel plugin draws
// the strip's frame beside its own figures, so a board with a display shows
// the strip whether or not one is wired.
//
// wantPanel:  the panel is up (true) or gone (false). While it is up the
//             strip's effect runs every frame even with no strip pin, the
//             same effect at the same brightness a wired strip would show.
// panelFrame: the strip's last frame as RGB, into room for cap pixels, and
//             how many it has (the strip's count; 0 while the lights plugin
//             is not running). pct is the strip's brightness, 1 to 30,
//             which the panel needs to show a dimmed strip at a readable
//             level rather than as the near-black a 10% frame is on glass.
// ---------------------------------------------------------------------------
void    wantPanel(bool on);
uint8_t panelFrame(uint8_t* rgb, uint8_t cap, uint8_t& pct);
#endif

} // namespace lights
