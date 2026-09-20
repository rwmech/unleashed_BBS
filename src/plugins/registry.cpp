/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/registry.cpp
 * Module:       Plugins / registry
 *
 * Purpose:      The plugins compiled into this firmware, in the order they
 *               start. Each one is still off until system.cfg says
 *               otherwise, except where its own section sets enabled = yes.
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     PLUGINS.md
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
#include "registry.h"

extern const Plugin kExamplePlugin;
extern const Plugin kChatPlugin;
extern const Plugin kSerialPlugin;
extern const Plugin kAnnouncePlugin;
extern const Plugin kSdPlugin;
extern const Plugin kFilesPlugin;

// Order here is display order, not start order: the sd plugin is PF_EARLY
// and plugins::begin() runs those first whatever position they hold, so this
// list can be reordered without changing what happens.
const Plugin* const kPlugins[] = {
    &kSdPlugin,
    &kFilesPlugin,
    &kExamplePlugin,
    &kChatPlugin,
    &kSerialPlugin,
    &kAnnouncePlugin,
};

const uint8_t kPluginCount = sizeof(kPlugins) / sizeof(kPlugins[0]);
