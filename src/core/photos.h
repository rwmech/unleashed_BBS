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
//               Since 1.2.0-link.7 (on 1.1.2) the built-in camera files its
//               pictures through this and provides Photos, and the file
//               areas ask present() and levels(), so Photos and Timelapse
//               exist on every board, shown while something takes pictures.
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
// kLater: how many seconds on a picture's stamp may move when its own name
// is taken (Rob, the gallery spec: "up to five").
constexpr uint8_t kLater = 5;
// fileAs (1.2.0): file, and when rel's name is taken (another camera in the
// same second: the built-in one and a satellite, two motion satellites),
// under the next second's stamp, up to kLater on (camera_rules.h laterName).
// rel is rewritten to the name it was filed under (cap is rel's size), so the
// caller offers and logs the right one. file() is the plain name or nothing.
bool fileAs(Writer& w, char* rel, size_t cap, const char* desc);
// freeName: the first of rel and its kLater later seconds not on the card
// yet, into out; false when all are taken. A check before the work of a
// picture; fileAs decides again at the rename, which is what counts.
bool freeName(const char* rel, char* out, size_t cap);
// abandon: close and remove the temporary file.
void abandon(Writer& w);

// ---------------------------------------------------------------------------
// Keeping Photos in bounds (1.2.0-link.15, Rob: "pruning for every camera").
// CONFIG photos' retention (photos_keep, photos_max, photos_floor and the
// timelapse's photos_tl_keep and photos_tl_max) is applied here, to every
// picture in Photos whichever camera took it, built in or a sat. It was the
// built-in camera's own survey until link.14, so a board whose only camera
// was a sat never pruned at all.
//
// The rules are camera_rules.h's (choose), unchanged: callers' photos (the
// Photos folder and its handle folders) are one group; timelapse/ (TL-) and
// motion/ (MO-) are groups of their own, both kept by the timelapse's
// limits (motion has none of its own yet), and so is any folder a camera
// names through systemFolder. Only a name of exactly a camera's shape is
// ever counted or removed. The floor takes the system groups first.
//
// The prune is a job on the background runner, never the loop's (Rule no.
// 1): the walk of the card, the space figure and the removals are the
// runner's. The loop only posts it (tick), and reads what it found (tally)
// once it is done. It runs after every picture filed (fileAs, whichever
// camera), when a camera starts providing Photos, when CONFIG photos'
// retention changes, and once a day. FILES.BBS lines of removed photos go
// through the file areas' queue (files::photoTidy), as before.
// ---------------------------------------------------------------------------
struct Tally {
    uint32_t callers = 0, system = 0;     // photos kept, callers' and the board's own
    uint64_t callerBytes = 0, systemBytes = 0;
    uint64_t oldest = 0;                  // nameKey of the oldest kept (YYYYMMDDhhmmss)
    uint64_t cardTotal = 0, cardFree = 0;
    uint64_t floor = 0;
    bool     floorMet = true;             // false: a snap is refused, "too full"
    bool     known = false;               // a prune has run since the card was seen
    bool     whole = true;                // the walk saw every photo (a big card may not fit)
    uint32_t removed = 0;                 // by the last prune
};

// pruneSoon: a prune is wanted. Any task (fileAs calls it on the runner).
void pruneSoon();
// systemFolder: a folder of the board's own shots (camera::snapSystem) as a
// group of its own, keepDays and maxFiles its limits (0 none). timelapse and
// motion are always groups, both with CONFIG photos' timelapse limits (the
// board's own shots), and are not changed by this. False when the table is full. Loop only.
bool systemFolder(const char* folder, const char* prefix, uint16_t keepDays, uint32_t maxFiles);
// pruning: a prune is queued or running. Loop only.
bool pruning();
// tally: what the last finished prune found. Loop only.
const Tally& tally();
// tick: the loop's, from Bbs::tick. Collects a finished prune, posts one
// that is wanted. Never touches the card.
void tick(uint32_t now);

// ---------------------------------------------------------------------------
// The board's cameras (1.2.0, approved 2026-09-26): one SNAPSHOT verb for
// every camera the board has, built in or on the link. The verbs are the
// core's (cameras.cpp) and exist while at least one camera is registered:
//   SNAPSHOT            the default camera: CONFIG photos "Default" when it
//                       names one that is up, else the built-in camera, else
//                       the first that is up
//   SNAPSHOT n|name     that one
//   CAMERA              staff: with one camera, its own view (as before);
//                       with more, every camera numbered, one line each
//   CAMERA n|name ...   that camera's own command (CAMERA 1 SET ...); with
//                       one camera, CAMERA SET ... still reaches it
// Numbered by a number of its own that does not move (1.2.0, tty-ux-sats):
// the built-in camera is 1; a satellite has the number CONFIG sats set for it
// (2 to 9), or "auto", the lowest free from 2 (from 1 on a board built with
// no camera), in order. Listed in number order. With one camera nothing
// changes for a caller.
//   SATS [n]            the satellites (callers: name, status, last picture,
//                       the number for SNAPSHOT; staff: the radio too), and
//                       SATS n one of them in full
//
// A caller's limits are ONE budget across every camera (Rob, 2026-09-26):
// 10 an hour means 10 on the board, not 10 per camera. A camera's snap asks
// budget() for anybody but the sysop and calls spend() once its picture is
// under way.
// ---------------------------------------------------------------------------
struct CamFacts;
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
    // --- 1.2.0, appended (a camera that sets none of these still works) ----
    // number: the camera number asked for, 2 to 9; 0 lets the board choose.
    uint8_t     number = 0;
    // pairing: a satellite's link pairing, -1 for a camera that is not one.
    // SATS lists the cameras with one.
    int8_t      pairing = -1;
    // levels: who may see its photos and who may take one, for SATS's
    // caller view. Null: everybody.
    void (*levels)(void* ctx, PlugLevel& see, PlugLevel& snap) = nullptr;
    // facts: what SATS shows of it beyond the radio. False: nothing known.
    bool (*facts)(void* ctx, CamFacts& f) = nullptr;
};

// What SATS says a camera is doing.
enum : uint8_t { CST_AWAKE = 0, CST_ASLEEP, CST_NOANSWER, CST_BUSY };

// CamFacts: a camera's own account of itself, for SATS.
struct CamFacts {
    uint8_t  state = CST_AWAKE;   // CST_*
    uint32_t lastAt = 0;          // its last picture, epoch seconds, 0 none
    uint32_t pictures = 0;        // since the board started
    uint32_t uptime = 0;          // seconds, 0 unknown
    char     sensor[12] = {};     // "OV2640"
    char     fw[13] = {};         // its firmware, "" unknown
    bool     sleeps = false;      // deep sleep between pictures
    uint16_t tlMin = 0;           // its timelapse, 0 0 none
    uint8_t  tlSec = 0;
    bool     motion = false;
    uint16_t hold = 0;            // seconds between motion pictures
};

// addCamera: from a camera's start(), or when a satellite comes up; kept by
// pointer, 8 at most. removeCamera: from its stop(), or when it goes.
bool addCamera(const Camera& c);
void removeCamera(const Camera& c);
// cameras: how many; camera(i): the i-th in number order, from 0.
uint8_t       cameras();
const Camera* camera(uint8_t i);
// numberOf: a camera's number (1 to 9), 0 for one that is not listed.
uint8_t       numberOf(const Camera* c);
// numberFree: n (2 to 9) is no other camera's to ask for. self may be null.
bool          numberFree(uint8_t n, const Camera* self);
// renumber: the numbers again, after a camera's number or pairing changed.
void          renumber();

// Budget: the tighter of a caller's windows (an account's by handle, a
// guest's by address and by name), each at most kPerHour an hour and
// kPerDay a day (camera_rules.h).
struct Budget {
    bool     ok     = true;
    uint8_t  hour   = 0;       // pictures in the last hour, before this one
    uint8_t  day    = 0;       // and in the last day
    uint32_t nextAt = 0;       // refused: the first moment one is allowed
    bool     byDay  = false;   // refused by the day's limit, else the hour's
    // The board's limits the verdict was against (CONFIG photos, 1.2.0), for
    // a camera to say ("That is 5 today"). Appended: API 1.1.
    uint8_t  perHour = 10;
    uint8_t  perDay  = 20;
};
Budget budget(const Session& s, uint32_t now);
void   spend(const Session& s, uint32_t now);
// renamed: an account's window follows its new handle (the core calls it).
void   renamed(const char* oldHandle, const char* newHandle);

}  // namespace photos
