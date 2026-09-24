/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/panel_feed.h
 * Module:       Plugins / panel (BBS_HAS_LCD boards only)
 *
 * Purpose:      What the board's own display reads from the other plugins,
 *               declared once so each definition and its one reader cannot
 *               disagree about a signature. Each is a figure the plugin
 *               already keeps in RAM, answered as it stands: the panel asks
 *               twice a second, so nothing here opens a file or walks
 *               anything bigger than a plugin's own small table. The panel
 *               checks plugins::running first; each answers "nothing" when
 *               its plugin has never started.
 *
 *               Compiled only on a board profile with BBS_HAS_LCD. On any
 *               other board none of these exists.
 *
 * Libraries:    none
 * Targets:      ESP32-S3 boards with BBS_HAS_LCD, and the Linux host build of
 *               one (bbs_host_s3)
 * See also:     src/plugins/panel.cpp
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
#include "../config.h"

#ifdef BBS_HAS_LCD
#include <cstdint>

// chat.cpp: messages waiting for this handle and not yet read, from the mail
// index chat keeps in RAM so "you have mail" costs no read. 0 for no handle.
namespace chat {
uint8_t unreadFor(const char* handle);
}

// files.cpp: uploads waiting for approval, board-wide (g_pending, kept as
// uploads come and go).
namespace files {
uint16_t pendingCount();
}

// announce.cpp: the listing as a desk would want it, in panelgfx::Status's
// Listing values: 0 nothing to show (no directory set, or none has answered
// and nothing has failed yet), 1 listed and online, 2 waiting (held while
// the sysop password is the default, or not public yet: pending, queued,
// held by the directory), 3 anything else (offline, refused, or failing to
// reach a directory at all).
namespace announce {
uint8_t listing();
}

// sd.cpp: the card, in panelgfx::Status's Card values: 0 no card, 1 mounted,
// 2 a card that is there and would not mount (or pins that cannot drive
// one): what the sd plugin blinks the drive light red for. freeKB is the
// plugin's cached figure when mounted, 0 otherwise; refresh asks the plugin
// to renew its cache when that is older than its own three seconds, which
// reads the card's FAT, so the panel asks with it once a minute and no more.
namespace sdcard {
uint8_t panel(uint32_t& freeKB, bool refresh);
}

#endif  // BBS_HAS_LCD
