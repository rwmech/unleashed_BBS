/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/skin_seed.h
 * Module:       Plugins / panel skins (BBS_HAS_LCD boards, and the host tests)
 *
 * Purpose:      The stock skins put on the card and kept current, by the
 *               rule the stock screens follow (sd.cpp, seedScreens), with a
 *               skin's folder as the unit: a sysop who has touched any file
 *               of a stock skin owns the whole of it from then on, so a new
 *               stock background never lands under an old hand-edited
 *               skin.txt that placed its LEDs for the old one.
 *
 *               <card>/skins/.seeded holds one line a stock file as the board
 *               wrote it, "c64/background.jpg 1a2b3c4d" (FNV-1a over its
 *               bytes). At each run, for each stock skin:
 *                 - no folder on the card       write it, record it
 *                 - every file as recorded      still the board's: rewrite
 *                                               what the new stock changed
 *                 - a file differs or is gone   the sysop's: never touched,
 *                   from its record             and no longer recorded
 *                 - no record at all            the sysop's, unless every
 *                                               file is byte for byte stock
 *               Each file goes down under a temporary name and is renamed
 *               in, so a pulled card leaves the old file or the new one.
 *
 *               Stdio only, no heap, so it runs on the board's worker and
 *               in host/test_skin.cpp against a folder.
 *
 * Libraries:    none (libc, POSIX mkdir)
 * Targets:      ESP32-S3 boards with BBS_HAS_LCD, and the Linux host tests
 * See also:     SKINS.md, src/plugins/skin.cpp, src/plugins/skin_stock.cpp
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
#include <cerrno>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace skin {

// One file of the stock set: "c64/skin.txt" and its bytes, in the image.
struct StockFile {
    const char*    path;
    const uint8_t* data;
    uint32_t       len;
};

constexpr size_t kSeedPathMax = 48;      // "folder/file" under skins/

struct SeedCount {
    uint16_t made = 0, refreshed = 0, kept = 0, failed = 0;
};

namespace sdetail {

inline uint32_t fnv(const uint8_t* p, size_t n, uint32_t h = 2166136261u) {
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

inline uint32_t fin(uint32_t h) { return h ? h : 1u; }

// fileHash: a file's FNV-1a, 0 when it cannot be read.
inline uint32_t fileHash(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t h = 2166136261u;
    uint8_t  buf[256];
    size_t   n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) h = fnv(buf, n, h);
    fclose(f);
    return fin(h);
}

// recorded: what .seeded says of path, 0 when it has no line.
inline uint32_t recorded(const char* manifest, const char* path) {
    FILE* f = fopen(manifest, "r");
    if (!f) return 0;
    char line[kSeedPathMax + 16];
    uint32_t found = 0;
    while (fgets(line, sizeof(line), f)) {
        char* sp = strchr(line, ' ');
        if (!sp) continue;
        *sp = '\0';
        if (!strcmp(line, path)) found = static_cast<uint32_t>(strtoul(sp + 1, nullptr, 16));
    }
    fclose(f);
    return found;
}

// folderOf: the stock file's folder, "c64" of "c64/skin.txt".
inline size_t folderLen(const char* path) {
    const char* s = strchr(path, '/');
    return s ? static_cast<size_t>(s - path) : 0;
}

inline bool sameFolder(const char* a, const char* b) {
    const size_t n = folderLen(a);
    return n && n == folderLen(b) && !strncmp(a, b, n);
}

// writeFile: data to path through path.tmp and a rename. FatFs will not
// rename over a file that is there, so only then, with the new one whole
// beside it, does the old one go first; and if that second rename fails the
// whole new copy stays as path.tmp rather than being thrown away with the
// old one already gone.
inline bool writeFile(const char* path, const uint8_t* data, uint32_t len) {
    char tmp[160];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= static_cast<int>(sizeof(tmp))) return false;
    FILE* f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(tmp); return false; }
    if (rename(tmp, path) == 0) return true;
    if (errno != EEXIST && errno != EACCES) { remove(tmp); return false; }
    remove(path);
    return rename(tmp, path) == 0;
}

} // namespace sdetail

// ---------------------------------------------------------------------------
// seed: the stock set onto <card>/skins, and .seeded rewritten. Counts what
// it did. Nothing at all when files is empty.
// ---------------------------------------------------------------------------
inline SeedCount seed(const char* card, const StockFile* files, size_t n) {
    using namespace sdetail;
    SeedCount c;
    if (!files || !n || !card || !*card) return c;
    char root[112], manifest[128], tmp[136];
    snprintf(root, sizeof(root), "%s/skins", card);
    snprintf(manifest, sizeof(manifest), "%s/.seeded", root);
    snprintf(tmp, sizeof(tmp), "%s.tmp", manifest);
    mkdir(root, 0755);

    // No record can be written: write nothing either, or the files would move
    // on while the record kept the old hashes, and the next run would take
    // every one of them for a sysop's edit.
    FILE* out = fopen(tmp, "w");
    if (!out) {
        c.failed = static_cast<uint16_t>(n);
        return c;
    }
    for (size_t i = 0; i < n;) {
        // One stock skin: files[i..k).
        size_t k = i + 1;
        while (k < n && sameFolder(files[i].path, files[k].path)) ++k;
        const size_t fl = folderLen(files[i].path);
        char dir[160], path[160];
        snprintf(dir, sizeof(dir), "%s/%.*s", root, static_cast<int>(fl), files[i].path);
        bool valid = fl > 0;
        for (size_t j = i; j < k; ++j)
            if (strlen(files[j].path) > kSeedPathMax) valid = false;
        if (!valid) { c.failed = static_cast<uint16_t>(c.failed + (k - i)); i = k; continue; }

        struct stat st;
        const bool there = stat(dir, &st) == 0;
        // The board's own copy: every recorded file as recorded, and every
        // file without a record (one a later build added to the skin) not on
        // the card or byte for byte stock; or, with no records at all, every
        // file byte for byte stock.
        bool ours = true, anyRecord = false, allStock = true;
        if (there) {
            for (size_t j = i; j < k; ++j) {
                snprintf(path, sizeof(path), "%s/%s", root, files[j].path);
                const uint32_t now = fileHash(path);
                const uint32_t was = recorded(manifest, files[j].path);
                const bool stock = now == fin(fnv(files[j].data, files[j].len));
                if (was) anyRecord = true;
                if (was ? now != was : (now && !stock)) ours = false;
                if (!stock) allStock = false;
            }
            if (!anyRecord) ours = allStock;
        } else {
            mkdir(dir, 0755);
        }
        if (there && !ours) {
            ++c.kept;
            i = k;
            continue;                                   // the sysop's: not recorded again
        }
        for (size_t j = i; j < k; ++j) {
            snprintf(path, sizeof(path), "%s/%s", root, files[j].path);
            const uint32_t stock = fin(fnv(files[j].data, files[j].len));
            bool write = !there || fileHash(path) != stock;
            bool ok = true;
            if (write) {
                ok = writeFile(path, files[j].data, files[j].len);
                if (!ok) ++c.failed;
                else if (there) ++c.refreshed;
            }
            if (ok) fprintf(out, "%s %08lx\n", files[j].path, static_cast<unsigned long>(stock));
        }
        if (!there) ++c.made;
        i = k;
    }
    if (fclose(out) == 0) {
        if (rename(tmp, manifest) != 0) {
            remove(manifest);
            rename(tmp, manifest);
        }
    } else {
        remove(tmp);
    }
    return c;
}

} // namespace skin
