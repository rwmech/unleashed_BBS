// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/photos.h
// Module:       Core / filing a picture in Photos (1.2.0)
//
// Purpose:      The one way a picture goes into the Photos area on the card,
//               whoever took it: the built-in camera (plugins/camera.cpp) or
//               a camera satellite over the link (unleashed_camsat's plugin).
//               Rob, 2026-09-26: "File a JPEG into Photos moves into shared
//               code that the built-in camera and camsat both call".
//
//               Two halves:
//
//               Who provides Photos. The Photos area exists while something
//               that takes pictures is running. A provider says so, and says
//               the levels that see and remove its pictures; the file areas
//               ask present() and levels() instead of asking the camera, so a
//               board with no camera of its own (the WROOM) still has Photos
//               when a satellite is paired.
//
//               How a picture is written. Under a temporary name in the
//               Photos folder, a piece at a time (a satellite's picture
//               arrives in fragments), flushed and synced, then renamed to
//               its real name in one step, so a picture is on the card whole
//               or not at all. Its FILES.BBS line is asked of the file areas,
//               which are that file's one writer (1.1.2).
//
//               Everything in the Writer half is card I/O: the background
//               runner's, never the loop's (Rule no. 1).
//
//               MERGE NOTE (written against rel-1.1.2a, 2026-09-26): at the
//               1.1.2 merge, plugins/camera.cpp files its pictures through
//               this, and plugins/files.cpp asks present()/levels() where it
//               now asks BBS_HAS_CAMERA and camera::photosLevels. Neither is
//               done on this branch because 1.1.2a rewrote both.
//
// Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1) and the Linux host build
// See also:     LINK.md (Family 1: CAMERA), src/plugins/camera_rules.h (names)
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3 of the License, or (at your
// option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>. The full
// text is in the LICENSE file at the top of this repository.
// ===========================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "plugin.h"

class Bbs;
struct Session;

namespace photos {

// ---------------------------------------------------------------------------
// Who provides Photos
// ---------------------------------------------------------------------------
struct Provider {
    const char* name;                                  // "camera", "camsat"
    bool (*running)();                                 // taking pictures now
    void (*levels)(PlugLevel& see, PlugLevel& remove); // who sees them, who removes them
};

// provide: from the provider's start(). withdraw: from its stop(). Two at most.
bool provide(const Provider& p);
void withdraw(const Provider& p);

// present: some provider is running, so the Photos area is there.
bool present();

// levels: the most open of the running providers' levels, so a picture
// anybody may see from one camera is not hidden by the other's settings.
void levels(PlugLevel& see, PlugLevel& remove);

// ---------------------------------------------------------------------------
// Writing a picture (the runner)
// ---------------------------------------------------------------------------
// dir: the Photos folder on the card, "<card>/photos". False with no card.
bool dir(char* out, size_t n);

struct Writer {
    FILE*    f = nullptr;
    bool     ok = false;
    uint32_t bytes = 0;
    char     tmp[160] = {};
};

// open: a temporary file in Photos. tmpName is a dot name, one per writer
// that can be open at once (".sat0.tmp"): a leading dot keeps it out of
// every listing.
bool open(Writer& w, const char* tmpName);
// write: the next bytes. False once anything has failed; the writer then
// only waits to be abandoned.
bool write(Writer& w, const uint8_t* p, size_t n);
// file: flush, sync and close, make rel's folders, and rename it to rel
// (under Photos). False when rel is taken or the card refused; the temporary
// file is gone either way. desc, if not null, becomes its FILES.BBS line.
bool file(Writer& w, const char* rel, const char* desc);
// abandon: close and remove the temporary file.
void abandon(Writer& w);

// ---------------------------------------------------------------------------
// The board's cameras (1.2.0, approved 2026-09-26): one SNAPSHOT verb for
// every camera the board has, built in or on the link. The verbs are the
// core's (cameras.cpp) and exist while at least one camera is registered:
//   SNAPSHOT            the default camera: CONFIG cameras "Default" when it
//                       names one that is up, else the built-in camera, else
//                       the first that is up
//   SNAPSHOT n|name     that one
//   CAMERA              staff: with one camera, its own view (as before);
//                       with more, every camera numbered, one line each
//   CAMERA n|name ...   that camera's own command (CAMERA 1 SET ...); with
//                       one camera, CAMERA SET ... still reaches it
// Numbered by order: the built-in camera (order 0) first, then satellites
// by pairing number (order 1 + n). With one camera nothing changes for a
// caller.
//
// A caller's limits are ONE budget across every camera (Rob, 2026-09-26):
// 10 an hour means 10 on the board, not 10 per camera. A camera's snap asks
// budget() for anybody but the sysop and calls spend() once its picture is
// under way.
// ---------------------------------------------------------------------------
struct Camera {
    const char* name;      // shown and typed after SNAPSHOT: "camera", "garden"
    uint8_t     order;     // 0 the built-in camera, 1 + pairing number a satellite
    void*       ctx;       // handed back to every call
    bool (*up)(void* ctx);                                   // can take one now
    bool (*busy)(void* ctx);                                 // taking one now
    // snap: take one for this caller, with its own levels and refusals; it
    // prompts, or owns the session, exactly as a command handler does.
    void (*snap)(void* ctx, Bbs& b, Session& s, uint32_t now);
    // line: a short status for CAMERA's list (sensor, what is kept).
    void (*line)(void* ctx, char* out, size_t n);
    // command: CAMERA <this camera> <arg>. Null: CAMERA shows its line.
    void (*command)(void* ctx, Bbs& b, Session& s, const char* arg, uint32_t now);
};

// addCamera: from a camera's start(), or when a satellite comes up; kept by
// pointer, 8 at most. removeCamera: from its stop(), or when it goes.
bool addCamera(const Camera& c);
void removeCamera(const Camera& c);
// cameras: how many; camera(i): the i-th by order, from 0.
uint8_t       cameras();
const Camera* camera(uint8_t i);

// Budget: the tighter of a caller's windows (an account's by handle, a
// guest's by address and by name), each at most kPerHour an hour and
// kPerDay a day (camera_rules.h).
struct Budget {
    bool     ok     = true;
    uint8_t  hour   = 0;       // pictures in the last hour, before this one
    uint8_t  day    = 0;       // and in the last day
    uint32_t nextAt = 0;       // refused: the first moment one is allowed
    bool     byDay  = false;   // refused by the day's limit, else the hour's
};
Budget budget(const Session& s, uint32_t now);
void   spend(const Session& s, uint32_t now);
// renamed: an account's window follows its new handle (the core calls it).
void   renamed(const char* oldHandle, const char* newHandle);

}  // namespace photos
