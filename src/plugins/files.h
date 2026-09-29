/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/files.h
 * Module:       Plugins / file areas, what other plugins may ask of them
 *
 * Purpose:      A camera, built in (1.1.0) or a satellite on the link
 *               (1.2.0, any board), hands a caller straight to a download of
 *               the photo just taken, and labels each photo with who took it
 *               the way any file is labelled, in its folder's FILES.BBS.
 *               Both through the file areas' own code, so a photo is
 *               downloaded and described exactly as any file is.
 *
 * Interfaces:   sendPhoto, photoDesc, photoTidy
 *
 * Libraries:    none
 * Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1) and the Linux host build
 * See also:     src/plugins/files.cpp, src/plugins/camera.cpp
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
#include "../config.h"

class Bbs;
struct Session;

namespace files {

// sendPhoto: download rel (a name in the Photos area, or handle/name) to
// this caller, YMODEM or XMODEM, exactly as the file areas send any file,
// and back to the main prompt when it ends. False, with the reason said
// and the caller at the prompt, when it cannot start.
bool sendPhoto(Bbs& b, Session& s, const char* rel, bool xmodem, uint32_t now);

// photoDesc: set, or with an empty text remove, the FILES.BBS line for one
// photo: sub is its handle folder under Photos, "" for Photos itself.
// photoTidy: that folder's FILES.BBS without the lines of photos that have
// gone, and a handle folder that is empty now removed. Both are ASKS
// (1.1.2): they go into the file areas' queue, and the files plugin writes
// the file from one job on the background runner, so the Photos folders'
// FILES.BBS has one writer. False when the queue was full. The camera's
// worker asks; it never writes the file itself.
bool photoDesc(const char* sub, const char* name, const char* text);
bool photoTidy(const char* sub);
// photoEditRoom (1.2.0-link.15): the queue has room for one more tidy now,
// with one slot always left for a picture's description (a camera files
// from the runner and cannot wait). The photo system asks before each tidy
// it hands over from a prune, a few a pass, rather than losing the fifth to
// a full queue.
bool photoEditRoom();

} // namespace files
