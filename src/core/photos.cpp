// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/photos.cpp
// Module:       Core / filing a picture in Photos (1.2.0)
//
// Purpose:      See photos.h.
//
// Libraries:    libc stdio, POSIX mkdir/rename
// Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1) and the Linux host build
// See also:     src/core/photos.h
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
#include "photos.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

#include "disk.h"
#include "../plugins/camera_rules.h"
#include "../platform/platform.h"
#include "../plugins/files.h"        // photoDesc: FILES.BBS's one writer

namespace photos {

namespace {
const Provider* g_prov[2] = {};

// The lower of two levels is the more open one: All < Users < ... < Nobody.
PlugLevel opener(PlugLevel a, PlugLevel b) { return static_cast<uint8_t>(a) < static_cast<uint8_t>(b) ? a : b; }
}  // namespace

bool provide(const Provider& p) {
    for (const Provider*& slot : g_prov) if (slot == &p) return true;
    for (const Provider*& slot : g_prov) if (!slot) { slot = &p; return true; }
    return false;
}

void withdraw(const Provider& p) {
    for (const Provider*& slot : g_prov) if (slot == &p) slot = nullptr;
}

bool present() {
    for (const Provider* p : g_prov) if (p && p->running && p->running()) return true;
    return false;
}

void levels(PlugLevel& see, PlugLevel& remove) {
    bool any = false;
    see = PlugLevel::Nobody;
    remove = PlugLevel::Nobody;
    for (const Provider* p : g_prov) {
        if (!p || !p->levels || (p->running && !p->running())) continue;
        PlugLevel s = PlugLevel::Nobody, r = PlugLevel::Nobody;
        p->levels(s, r);
        see = any ? opener(see, s) : s;
        remove = any ? opener(remove, r) : r;
        any = true;
    }
}

bool dir(char* out, size_t n) {
    const char* base = plat::sdBase();
    if (!base || !*base) return false;
    int w = snprintf(out, n, "%s/%s", base, camrules::kPhotosDir);
    return w > 0 && static_cast<size_t>(w) < n;
}

bool open(Writer& w, const char* tmpName) {
    w = Writer();
    char d[128];
    if (!dir(d, sizeof(d)) || !tmpName || tmpName[0] != '.') return false;
    mkdir(d, 0755);                                  // EEXIST is fine
    int k = snprintf(w.tmp, sizeof(w.tmp), "%s/%s", d, tmpName);
    if (k <= 0 || static_cast<size_t>(k) >= sizeof(w.tmp)) return false;
    w.f = disk::open(w.tmp, "wb");
    w.ok = w.f != nullptr;
    return w.ok;
}

bool write(Writer& w, const uint8_t* p, size_t n) {
    if (!w.ok || !w.f) return false;
    if (n && fwrite(p, 1, n, w.f) != n) w.ok = false;
    else w.bytes += static_cast<uint32_t>(n);
    return w.ok;
}

void abandon(Writer& w) {
    if (w.f) fclose(w.f);
    w.f = nullptr;
    if (w.tmp[0]) remove(w.tmp);
    w.ok = false;
}

namespace {
bool fileIn(Writer& w, char* rel, size_t cap, const char* desc, uint8_t later);
}

bool file(Writer& w, const char* rel, const char* desc) {
    char r[160];
    if (!rel || snprintf(r, sizeof(r), "%s", rel) >= static_cast<int>(sizeof(r))) { abandon(w); return false; }
    return fileIn(w, r, sizeof(r), desc, 0);
}

bool fileAs(Writer& w, char* rel, size_t cap, const char* desc) {
    return fileIn(w, rel, cap, desc, kLater);
}

bool freeName(const char* rel, char* out, size_t cap) {
    char d[128], dst[300];   // the folder (127) and a name (159)
    if (!rel || !dir(d, sizeof(d))) return false;
    char name[160];
    if (snprintf(name, sizeof(name), "%s", rel) >= static_cast<int>(sizeof(name))) return false;
    struct stat st;
    for (uint8_t i = 0; i <= kLater; ++i) {
        if (i) {
            char next[160];
            if (!camrules::laterName(name, next, sizeof(next))) return false;
            memcpy(name, next, sizeof(name));
        }
        snprintf(dst, sizeof(dst), "%s/%s", d, name);
        if (stat(dst, &st) != 0) {
            const int w = snprintf(out, cap, "%s", name);
            return w > 0 && static_cast<size_t>(w) < cap;
        }
    }
    return false;
}

namespace {
// fileIn: file and fileAs, the name and up to later seconds on.
bool fileIn(Writer& w, char* rel, size_t cap, const char* desc, uint8_t later) {
    if (!w.f) { abandon(w); return false; }
    bool ok = w.ok && fflush(w.f) == 0;
    fsync(fileno(w.f));
    if (fclose(w.f) != 0) ok = false;
    w.f = nullptr;
    char d[128], dst[300];   // the folder (127) and a name (159)
    // A name from a satellite is data from the air: nothing that climbs out
    // of the folder, and none of FAT's other separators (a backslash, or a
    // colon naming a drive).
    if (!ok || !rel || !*rel || strstr(rel, "..") || rel[0] == '/' || strpbrk(rel, "\\:") ||
        !dir(d, sizeof(d))) {
        abandon(w);
        return false;
    }
    // The one folder a name may have (a handle's, a satellite's, a system
    // folder): a second slash is refused, as the Photos area lists one level.
    const char* slash = strchr(rel, '/');
    if (slash && (slash == rel || !slash[1] || strchr(slash + 1, '/'))) {
        abandon(w);
        return false;
    }
    if (slash) {
        char sub[200];
        snprintf(sub, sizeof(sub), "%s/%.*s", d, static_cast<int>(slash - rel), rel);
        mkdir(sub, 0755);
    }
    // The name, then the next second's, up to later on, when another camera
    // took it in the same second. FAT refuses to rename over a file, and a
    // picture never replaces one; the host's rename would replace it, so
    // the name is looked at first. Looked at again after a failed rename:
    // the other camera's filing runs on another task (the built-in camera's
    // worker, the runner) and can take the name between the look and the
    // rename, which is the case the later name is for.
    char name[160];
    if (snprintf(name, sizeof(name), "%s", rel) >= static_cast<int>(sizeof(name))) { abandon(w); return false; }
    bool filed = false;
    struct stat st;
    for (uint8_t i = 0; i <= later; ++i) {
        if (i) {
            char next[160];
            if (!camrules::laterName(name, next, sizeof(next)) || strlen(next) >= cap) break;
            memcpy(name, next, sizeof(name));
        }
        snprintf(dst, sizeof(dst), "%s/%s", d, name);
        if (stat(dst, &st) == 0) continue;
#ifdef BBS_HOST
        // The host's rename would replace a file the other task filed since
        // the look; link does not (EEXIST), so the host takes the same path
        // the card's FatFs does (f_rename refuses an existing name).
        if (link(w.tmp, dst) == 0) { unlink(w.tmp); filed = true; break; }
#else
        if (rename(w.tmp, dst) == 0) { filed = true; break; }
#endif
        if (stat(dst, &st) != 0) break;                // refused, and not for the name
    }
    if (!filed) {
        abandon(w);
        return false;
    }
    if (strcmp(name, rel)) snprintf(rel, cap, "%s", name);
    w.tmp[0] = '\0';
    // Its FILES.BBS line is asked of the file areas, that file's one writer
    // (1.1.2), with the folder relative to Photos ("" for Photos itself). The
    // ask is queued: the line follows the picture, never holds it up.
    if (desc && *desc) {
        char sub[64] = "";
        const char* leaf = rel;
        if (slash) {
            snprintf(sub, sizeof(sub), "%.*s", static_cast<int>(slash - rel < 63 ? slash - rel : 63), rel);
            leaf = slash + 1;
        }
        files::photoDesc(sub, leaf, desc);
    }
    return true;
}
}  // namespace

}  // namespace photos
