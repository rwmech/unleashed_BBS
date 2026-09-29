/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/skin_stock.cpp
 * Module:       Plugins / panel skins (BBS_HAS_LCD boards only)
 *
 * Purpose:      The stock skins the image carries, for skin_seed.h to put on
 *               the card at its first mount and keep current.
 *
 * Design:       Empty for now, deliberately. The five stock skins
 *               (skins/stock/, built by tools/mkskins_stock.py) come to
 *               about 200 KB, which the 1.1.1 layout's 1.5 MB app slots
 *               cannot spare on an S3; 1.1.2 moves every S3 to an 8 MB
 *               layout with 3 MB slots, and the set is embedded then, one
 *               StockFile per file, grouped by folder. Until then a board
 *               shows the built-in status skin and a sysop copies a skin
 *               folder to the card from the site's zip.
 *
 * Libraries:    none
 * Targets:      ESP32-S3 boards with BBS_HAS_LCD, and the Linux host build
 * See also:     SKINS.md, src/plugins/skin_seed.h
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
#include "../config.h"

#ifdef BBS_HAS_LCD
#include "skin.h"

const skin::StockFile* skin::stockFiles(size_t& n) {
    n = 0;
    return nullptr;
}
#endif  // BBS_HAS_LCD
