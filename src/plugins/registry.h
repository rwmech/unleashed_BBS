/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/registry.h
 * Module:       Plugins / registry
 *
 * Purpose:      The list of plugins compiled into this firmware. Adding a
 *               plugin means one entry here and one source folder; a board
 *               then switches it on in system.cfg.
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     PLUGINS.md
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

extern const Plugin* const kPlugins[];
extern const uint8_t       kPluginCount;
