/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/files.h
 * Module:       Plugins / file areas, what other plugins may ask of them
 *
 * Purpose:      The camera (1.1.0, BBS_HAS_CAMERA boards) hands a caller
 *               straight to a download of the photo just taken, and labels
 *               each photo with who took it the way any file is labelled,
 *               in its folder's FILES.BBS. Both through the file areas' own
 *               code, so a photo is downloaded and described exactly as any
 *               file is.
 *
 * Interfaces:   sendPhoto, photoDesc, photoDescDrop
 *
 * Libraries:    none
 * Targets:      ESP32 and ESP32-S3 camera boards and the Linux host build
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

#ifdef BBS_HAS_CAMERA
class Bbs;
struct Session;

namespace files {

// sendPhoto: download rel (a name in the Photos area, or handle/name) to
// this caller, YMODEM or XMODEM, exactly as the file areas send any file,
// and back to the main prompt when it ends. False, with the reason said
// and the caller at the prompt, when it cannot start.
bool sendPhoto(Bbs& b, Session& s, const char* rel, bool xmodem, uint32_t now);

// photoDesc: set, or with an empty text remove, the FILES.BBS line for one
// file in dir (a folder on the card), through the file areas' temp file and
// rename. Blocking card I/O: the camera's worker calls it, never the loop.
bool photoDesc(const char* dir, const char* name, const char* text);

// photoDescDrop: one rewrite of dir's FILES.BBS without the lines for which
// gone(ctx, name) says the file has gone. The camera's worker, after pruning.
bool photoDescDrop(const char* dir, bool (*gone)(void* ctx, const char* name), void* ctx);

} // namespace files
#endif
