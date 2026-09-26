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
#include "../platform/platform.h"
#include "../plugins/camera_rules.h"
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

bool file(Writer& w, const char* rel, const char* desc) {
    if (!w.f) { abandon(w); return false; }
    bool ok = w.ok && fflush(w.f) == 0;
    fsync(fileno(w.f));
    if (fclose(w.f) != 0) ok = false;
    w.f = nullptr;
    char d[128], dst[256];
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
    snprintf(dst, sizeof(dst), "%s/%s", d, rel);
    struct stat st;
    // FAT refuses to rename over a file, and a picture never replaces one.
    if (stat(dst, &st) == 0 || rename(w.tmp, dst) != 0) {
        abandon(w);
        return false;
    }
    w.tmp[0] = '\0';
    // Its FILES.BBS line is asked of the file areas, that file's one writer
    // (1.1.2), with the folder relative to Photos ("" for Photos itself). The
    // ask is queued: the line follows the picture, never holds it up.
    if (desc && *desc) {
        char sub[64] = "";
        const char* name = rel;
        if (slash) {
            snprintf(sub, sizeof(sub), "%.*s", static_cast<int>(slash - rel < 63 ? slash - rel : 63), rel);
            name = slash + 1;
        }
        files::photoDesc(sub, name, desc);
    }
    return true;
}

}  // namespace photos
