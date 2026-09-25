/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/camera.h
 * Module:       Plugins / camera (BBS_HAS_CAMERA boards)
 *
 * Purpose:      What the rest of the board may ask the camera. The file areas
 *               ask for the Photos area's levels (area 12), the sd plugin
 *               asks whether the card is in use before it lets the card go,
 *               and anything that takes pictures on its own (the timelapse
 *               now, a motion sensor on the GPIO plugin later) calls
 *               snapSystem.
 *
 * Interfaces:   running, busy, photosLevels, snapSystem
 *
 * Libraries:    none
 * Targets:      ESP32 and ESP32-S3 camera boards and the Linux host build
 * See also:     src/plugins/camera.cpp, internal/PLAN-freenove-cam.md
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
#include "../core/plugin.h"

#ifdef BBS_HAS_CAMERA
namespace camera {

// running: the camera plugin is on and has a card to keep photos on.
bool running();

// found: a sensor answered the latest bring-up this boot (the survey's look
// once a boot, or a snap). What announce's "camera" feature asks, with
// running(): a board whose camera will not start does not claim the badge.
bool found();

// busy: a picture is being taken or written, or old ones removed. The
// card is in use by the camera's worker until this is false.
bool busy();

// photosLevels: who may see and download photos (CONFIG camera, Photos),
// and who may remove them (the plugin's admin level). For the Photos area.
void photosLevels(PlugLevel& see, PlugLevel& remove);

// snapSystem: a picture the board takes by itself, into Photos/<folder>/ as
// <PREFIX>-YYYYMMDD-HHMMSS.JPG, kept for keepDays days (0 forever) and at
// most maxFiles of them (0 no limit), pruned on its own so a fast series
// can never take a caller's photos with it. The timelapse is
// snapSystem("timelapse", "TL", ...); a motion sensor would be
// snapSystem("motion", "MO", ...). Folder and prefix are short plain
// words. False, doing nothing, when the camera is busy, off or has no card:
// a system shot that finds the camera busy is simply skipped.
bool snapSystem(const char* folder, const char* prefix, uint16_t keepDays, uint32_t maxFiles);

} // namespace camera
#endif
