/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/ziparc.cpp
 * Module:       Core / backup archive
 *
 * Purpose:      Backup archive export and import (see ziparc.h).
 *                  ZIP format: APPNOTE 6.3 subset. Stored entries out, stored
 *                  or deflated entries in, no ZIP64, no encryption.
 *
 * Libraries:    none (libc stdio, dirent, sys/stat)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     BACKUP.md
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

#include "ziparc.h"
#include "crc32.h"
#include "sysconfig.h"
#include "users.h"
#include "clock.h"
#include "../platform/platform.h"

#include <cstring>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ziparc {

namespace {

constexpr uint32_t kSigLocal   = 0x04034b50;
constexpr uint32_t kSigCentral = 0x02014b50;
constexpr uint32_t kSigEnd     = 0x06054b50;
constexpr uint16_t kMaxEntries = 256;          // central directory records we will read

void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
void put32(uint8_t* p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, (v >> 16) & 0xFFFF); }
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) { return get16(p) | (static_cast<uint32_t>(get16(p + 2)) << 16); }

// livePath: where a file in the zip belongs on the board. The backup spans
// two partitions now: the screens are on the one a filesystem upload can
// replace, while the config and the accounts sit on the user partition that
// survives a reflash. Restoring has to put each back where it lives.
void livePath(char* out, size_t n, const char* name) {
    bool owned = !strcmp(name, BBS_CONFIG_FILE) || !strcmp(name, BBS_USERS_FILE);
    snprintf(out, n, "%s/%s", owned ? plat::userBase() : plat::fsBase(), name);
}

void stagePath(char* out, size_t n, const char* name) {
    snprintf(out, n, "%s/%s/%s", plat::fsBase(), BBS_BACKUP_STAGING, name);
}

void ensureDir(const char* path) {
    mkdir(path, 0755);                          // EEXIST is fine
}

// removeFiles: delete regular files in a directory, then the directory
void removeDir(const char* dir) {
    DIR* d = opendir(dir);
    if (d) {
        struct dirent* e;
        char path[128];
        while ((e = readdir(d)) != nullptr) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(path, sizeof(path), "%.60s/%.60s", dir, e->d_name);
            remove(path);
        }
        closedir(d);
    }
    rmdir(dir);
}

void dosStamp(uint16_t& t, uint16_t& d) {
    t = 0;
    d = (0 << 9) | (1 << 5) | 1;                // 1980-01-01 when the clock is unset
    uint32_t e = clk::epoch();
    if (!e) return;
    time_t tt = static_cast<time_t>(e);
    struct tm lt;
    localtime_r(&tt, &lt);
    t = static_cast<uint16_t>((lt.tm_hour << 11) | (lt.tm_min << 5) | (lt.tm_sec / 2));
    d = static_cast<uint16_t>(((lt.tm_year - 80) << 9) | ((lt.tm_mon + 1) << 5) | lt.tm_mday);
}

bool skipName(const char* n) {
    size_t len = strlen(n);
    if (!len || n[len - 1] == '/') return true;                  // directory
    if (!strncmp(n, "__macosx/", 9)) return true;                // macOS resource forks
    const char* base = strrchr(n, '/');
    base = base ? base + 1 : n;
    return !strcmp(base, ".ds_store") || !strcmp(base, "manifest.txt") ||
           !strcmp(base, "thumbs.db") || !strcmp(base, "desktop.ini");
}

void normalize(const char* raw, char* out, size_t n) {
    size_t i = 0;
    for (; raw[i] && i + 1 < n; ++i) {
        char c = raw[i] == '\\' ? '/' : raw[i];
        out[i] = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    out[i] = '\0';
}

} // namespace

// ---------------------------------------------------------------------------
// validScreenName: base 1..8 of a-z 0-9 _ -, one dot, known extension
// ---------------------------------------------------------------------------
bool validScreenName(const char* file) {
    const char* dot = strchr(file, '.');
    if (!dot || strchr(dot + 1, '.')) return false;
    size_t base = static_cast<size_t>(dot - file);
    if (base < 1 || base > BBS_SCREEN_NAME_MAX) return false;
    for (size_t i = 0; i < base; ++i) {
        char c = file[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    const char* ext = dot + 1;
    return !strcmp(ext, "asc") || !strcmp(ext, "ans") || !strcmp(ext, "seq") ||
           !strcmp(ext, "p40") || !strcmp(ext, "p80");
}

// ===========================================================================
// ZipExport
// ===========================================================================

bool ZipExport::addEntry(const char* name, Src src) {
    if (count_ >= sizeof(list_) / sizeof(list_[0])) return false;
    Entry& e = list_[count_++];
    strncpy(e.name, name, sizeof(e.name) - 1);
    e.name[sizeof(e.name) - 1] = '\0';
    e.src    = src;
    e.size   = 0;
    e.crc    = 0;
    e.offset = 0;
    return true;
}

// ---------------------------------------------------------------------------
// cfgRead: system.cfg with password values replaced by ***
// ---------------------------------------------------------------------------
size_t ZipExport::cfgRead(uint8_t* buf, size_t cap) {
    if (!f_) {
        char path[96];
        livePath(path, sizeof(path), BBS_CONFIG_FILE);
        f_ = fopen(path, "r");
        lineLen_ = linePos_ = 0;
        if (!f_) return 0;
    }
    size_t n = 0;
    while (n < cap) {
        if (linePos_ < lineLen_) {
            size_t take = lineLen_ - linePos_;
            if (take > cap - n) take = cap - n;
            memcpy(buf + n, line_ + linePos_, take);
            linePos_ = static_cast<uint8_t>(linePos_ + take);
            n += take;
            continue;
        }
        char raw[160];
        if (!fgets(raw, sizeof(raw), f_)) break;
        if (!syscfg::redactLine(raw, line_, sizeof(line_))) {
            strncpy(line_, raw, sizeof(line_) - 1);
            line_[sizeof(line_) - 1] = '\0';
        }
        lineLen_ = static_cast<uint8_t>(strlen(line_));
        linePos_ = 0;
    }
    return n;
}

size_t ZipExport::readData(Entry& e, uint8_t* buf, size_t cap) {
    switch (e.src) {
        case Src::Manifest: {
            size_t left = e.size - sent_;
            if (cap > left) cap = left;
            memcpy(buf, manifest_ + sent_, cap);
            return cap;
        }
        case Src::Config:
            return cfgRead(buf, cap);
        case Src::File:
        case Src::Snapshot:
        default:
            if (!f_) {
                char path[96];
                if (e.src == Src::Snapshot) livePath(path, sizeof(path), BBS_USERS_FILE ".export");
                else                        livePath(path, sizeof(path), e.name);
                f_ = fopen(path, "rb");
                if (!f_) return 0;
            }
            return fread(buf, 1, cap, f_);
    }
}

// measure: run the same read path once to get the exact size and CRC
bool ZipExport::measure(Entry& e) {
    uint8_t buf[256];
    sent_ = 0;
    size_t n;
    while ((n = readData(e, buf, sizeof(buf))) > 0) {
        e.crc   = crc32::update(e.crc, buf, n);
        e.size += static_cast<uint32_t>(n);
        sent_  += static_cast<uint32_t>(n);
        if (e.size > BBS_ZIP_FILE_MAX * 4u) break;          // runaway file: stop reading
    }
    abort();
    return e.src != Src::File || e.size > 0 || true;
}

void ZipExport::buildManifest(const char* hostname) {
    char when[32];
    clk::fmt(when, sizeof(when), "%Y-%m-%d %H:%M %Z");
    int w = snprintf(manifest_, sizeof(manifest_),
                     "%s backup\n"
                     "version  %s\n"
                     "host     %s\n"
                     "created  %s\n"
                     "files    %u\n\n"
                     "  bytes  file\n",
                     BBS_NAME, BBS_VERSION, hostname, clk::valid() ? when : "clock not set",
                     static_cast<unsigned>(count_));
    for (uint8_t i = 0; i < count_ && w > 0 && static_cast<size_t>(w) < sizeof(manifest_) - 64; ++i) {
        w += snprintf(manifest_ + w, sizeof(manifest_) - w, "%7u  %s\n",
                      static_cast<unsigned>(list_[i].size), list_[i].name);
    }
    if (w > 0 && static_cast<size_t>(w) < sizeof(manifest_) - 1) {
        w += snprintf(manifest_ + w, sizeof(manifest_) - w,
                      "\nUnzip, edit, zip the files again with any zip tool, and\n"
                      "upload during a backup window. See BACKUP.md.\n"
                      "Passwords show as ***; leaving *** keeps the current one.\n"
                      "This file is ignored on upload.\n");
    }
    if (w < 0) w = 0;
    if (static_cast<size_t>(w) >= sizeof(manifest_)) w = static_cast<int>(sizeof(manifest_) - 1);
    manifestLen_ = static_cast<uint16_t>(w);
}

// ---------------------------------------------------------------------------
// scan: pick entries, size them, lay out offsets
// ---------------------------------------------------------------------------
bool ZipExport::scan(const char* hostname, char* err, size_t errLen) {
    abort();
    count_ = 0;
    total_ = 0;
    dosStamp(dosTime_, dosDate_);

    char path[96];
    livePath(path, sizeof(path), BBS_CONFIG_FILE);
    struct stat st;
    if (stat(path, &st) == 0) addEntry(BBS_CONFIG_FILE, Src::Config);
    livePath(path, sizeof(path), BBS_USERS_FILE);
    if (stat(path, &st) == 0 && snapshotUsers()) addEntry(BBS_USERS_FILE, Src::Snapshot);

    uint8_t firstScreen = count_;
    livePath(path, sizeof(path), BBS_SCREEN_DIR);
    DIR* d = opendir(path);
    if (d) {
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            if (!validScreenName(e->d_name)) continue;
            if (count_ - firstScreen >= BBS_ZIP_MAX_FILES) break;
            char name[32];
            snprintf(name, sizeof(name), "%s/%.12s", BBS_SCREEN_DIR, e->d_name);
            addEntry(name, Src::File);
        }
        closedir(d);
    }
    // stable order: insertion sort of the screens by name
    for (uint8_t i = firstScreen + 1; i < count_; ++i) {
        Entry tmp = list_[i];
        uint8_t j = i;
        while (j > firstScreen && strcmp(list_[j - 1].name, tmp.name) > 0) { list_[j] = list_[j - 1]; --j; }
        list_[j] = tmp;
    }

    for (uint8_t i = 0; i < count_; ++i) measure(list_[i]);

    buildManifest(hostname);
    if (!addEntry("MANIFEST.txt", Src::Manifest)) {
        snprintf(err, errLen, "too many files");
        return false;
    }
    Entry& m = list_[count_ - 1];
    m.size = manifestLen_;
    m.crc  = crc32::update(0, reinterpret_cast<const uint8_t*>(manifest_), manifestLen_);

    uint32_t off = 0;
    for (uint8_t i = 0; i < count_; ++i) {
        list_[i].offset = off;
        off += 30u + static_cast<uint32_t>(strlen(list_[i].name)) + list_[i].size;
    }
    cdOffset_ = off;
    cdSize_   = 0;
    for (uint8_t i = 0; i < count_; ++i) cdSize_ += 46u + static_cast<uint32_t>(strlen(list_[i].name));
    total_ = cdOffset_ + cdSize_ + 22u;

    phase_  = Phase::Local;
    idx_    = 0;
    hdrLen_ = hdrPos_ = 0;
    sent_   = 0;
    (void)err;
    return true;
}

void ZipExport::fillHeader(const Entry& e, bool central) {
    uint16_t nlen = static_cast<uint16_t>(strlen(e.name));
    uint8_t* h = hdr_;
    memset(h, 0, sizeof(hdr_));
    if (!central) {
        put32(h, kSigLocal);
        put16(h + 4, 20);                        // version needed
        put16(h + 10, dosTime_);
        put16(h + 12, dosDate_);
        put32(h + 14, e.crc);
        put32(h + 18, e.size);
        put32(h + 22, e.size);
        put16(h + 26, nlen);
        memcpy(h + 30, e.name, nlen);
        hdrLen_ = static_cast<uint8_t>(30 + nlen);
    } else {
        put32(h, kSigCentral);
        put16(h + 4, 20);                        // version made by (MS-DOS, 2.0)
        put16(h + 6, 20);                        // version needed
        put16(h + 12, dosTime_);
        put16(h + 14, dosDate_);
        put32(h + 16, e.crc);
        put32(h + 20, e.size);
        put32(h + 24, e.size);
        put16(h + 28, nlen);
        put32(h + 42, e.offset);
        memcpy(h + 46, e.name, nlen);
        hdrLen_ = static_cast<uint8_t>(46 + nlen);
    }
    hdrPos_ = 0;
}

// ---------------------------------------------------------------------------
// produce: local header + data per entry, central directory, end record
// ---------------------------------------------------------------------------
size_t ZipExport::produce(uint8_t* buf, size_t cap) {
    size_t n = 0;
    while (n < cap && phase_ != Phase::Done) {
        if (hdrPos_ < hdrLen_) {
            size_t take = hdrLen_ - hdrPos_;
            if (take > cap - n) take = cap - n;
            memcpy(buf + n, hdr_ + hdrPos_, take);
            hdrPos_ = static_cast<uint8_t>(hdrPos_ + take);
            n += take;
            continue;
        }
        switch (phase_) {
            case Phase::Local:
                if (idx_ >= count_) { phase_ = Phase::Central; idx_ = 0; break; }
                fillHeader(list_[idx_], false);
                phase_ = Phase::Data;
                sent_  = 0;
                break;
            case Phase::Data: {
                Entry& e = list_[idx_];
                if (sent_ >= e.size) {
                    abort();
                    ++idx_;
                    phase_ = Phase::Local;
                    break;
                }
                size_t want = e.size - sent_;
                if (want > cap - n) want = cap - n;
                size_t got = readData(e, buf + n, want);
                if (got == 0) {                  // file shrank since scan: keep the layout
                    memset(buf + n, 0, want);
                    got = want;
                }
                sent_ += static_cast<uint32_t>(got);
                n += got;
                break;
            }
            case Phase::Central:
                if (idx_ >= count_) {
                    memset(hdr_, 0, 22);
                    put32(hdr_, kSigEnd);
                    put16(hdr_ + 8, count_);
                    put16(hdr_ + 10, count_);
                    put32(hdr_ + 12, cdSize_);
                    put32(hdr_ + 16, cdOffset_);
                    hdrLen_ = 22;
                    hdrPos_ = 0;
                    phase_  = Phase::End;
                    break;
                }
                fillHeader(list_[idx_], true);
                ++idx_;
                break;
            case Phase::End:
                phase_ = Phase::Done;
                break;
            default:
                phase_ = Phase::Done;
                break;
        }
    }
    return n;
}

void ZipExport::abort() {
    if (f_) { fclose(f_); f_ = nullptr; }
    lineLen_ = linePos_ = 0;
}

// ---------------------------------------------------------------------------
// snapshotUsers: copy users.txt aside for the download. Accounts keep
// changing while a client downloads (a logoff writes call stats), and the
// zip promises the size and CRC measured up front.
// ---------------------------------------------------------------------------
bool ZipExport::snapshotUsers() {
    char src[96], dst[96];
    livePath(src, sizeof(src), BBS_USERS_FILE);
    livePath(dst, sizeof(dst), BBS_USERS_FILE ".export");
    FILE* in = fopen(src, "rb");
    if (!in) return false;
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    uint8_t buf[256];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) ok = fwrite(buf, 1, n, out) == n && ok;
    ok = !ferror(in) && ok;
    fclose(in);
    ok = (fclose(out) == 0) && ok;
    if (!ok) remove(dst);
    return ok;
}

// dropSnapshot: remove the download copy once the transfer ends
void ZipExport::dropSnapshot() {
    char dst[96];
    livePath(dst, sizeof(dst), BBS_USERS_FILE ".export");
    remove(dst);
}

// ===========================================================================
// ZipImport
// ===========================================================================

void ZipImport::reject(const char* name, const char* why) {
    if (!rep_.rejected) snprintf(rep_.firstReject, sizeof(rep_.firstReject), "%.40s: %s", name, why);
    if (rep_.rejected < 255) ++rep_.rejected;
    plat::log("backup: rejected %.40s (%s)", name, why);
}

// ---------------------------------------------------------------------------
// open: find the end record, read the central directory twice (common
// folder prefix, then classification)
// ---------------------------------------------------------------------------
bool ZipImport::open(const char* zipPath, char* err, size_t errLen) {
    if (zf_) { fclose(zf_); zf_ = nullptr; }         // the upload itself lives in staging: keep it
    strncpy(zipPath_, zipPath, sizeof(zipPath_) - 1);
    rep_       = ImportReport();
    itemCount_ = 0;
    next_      = 0;

    zf_ = fopen(zipPath, "rb");
    if (!zf_) { snprintf(err, errLen, "upload not found"); return false; }
    fseek(zf_, 0, SEEK_END);
    long size = ftell(zf_);
    if (size < 22 || size > BBS_ZIP_MAX_BYTES) { snprintf(err, errLen, "not a zip file (size)"); return false; }

    static uint8_t tail[22 + 1024];
    size_t tailLen = static_cast<size_t>(size) < sizeof(tail) ? static_cast<size_t>(size) : sizeof(tail);
    fseek(zf_, size - static_cast<long>(tailLen), SEEK_SET);
    if (fread(tail, 1, tailLen, zf_) != tailLen) { snprintf(err, errLen, "read error"); return false; }

    long eocd = -1;
    for (size_t p = tailLen - 22 + 1; p-- > 0;) {
        if (get32(tail + p) == kSigEnd && p + 22 + get16(tail + p + 20) == tailLen) {
            eocd = size - static_cast<long>(tailLen) + static_cast<long>(p);
            break;
        }
    }
    if (eocd < 0) { snprintf(err, errLen, "not a zip file (no end record)"); return false; }

    const uint8_t* e = tail + (eocd - (size - static_cast<long>(tailLen)));
    uint16_t total = get16(e + 10);
    uint32_t cdSize = get32(e + 12);
    cdOffset_ = get32(e + 16);
    if (get16(e + 4) || get16(e + 6) || get16(e + 8) != total) { snprintf(err, errLen, "multi-part zips are not supported"); return false; }
    if (total > kMaxEntries) { snprintf(err, errLen, "too many entries (%u)", total); return false; }
    if (static_cast<long>(cdOffset_) + static_cast<long>(cdSize) > eocd) { snprintf(err, errLen, "corrupt central directory"); return false; }

    static char raw[256];
    static char norm[256];
    uint8_t h[46];

    // pass 1: one shared top-level folder gets stripped
    char prefix[64] = {};
    bool prefixOk = true;
    bool anyName  = false;
    fseek(zf_, static_cast<long>(cdOffset_), SEEK_SET);
    for (uint16_t i = 0; i < total; ++i) {
        if (fread(h, 1, 46, zf_) != 46 || get32(h) != kSigCentral) { snprintf(err, errLen, "corrupt central directory"); return false; }
        uint16_t nlen = get16(h + 28);
        if (fread(raw, 1, nlen, zf_) != nlen) { snprintf(err, errLen, "corrupt central directory"); return false; }
        raw[nlen] = '\0';
        fseek(zf_, get16(h + 30) + get16(h + 32), SEEK_CUR);
        normalize(raw, norm, sizeof(norm));
        if (skipName(norm)) continue;
        const char* slash = strchr(norm, '/');
        size_t seg = slash ? static_cast<size_t>(slash - norm) : 0;
        if (!slash || seg >= sizeof(prefix) - 1 || (seg == 7 && !strncmp(norm, "screens", 7))) { prefixOk = false; continue; }
        if (!anyName) { memcpy(prefix, norm, seg + 1); prefix[seg + 1] = '\0'; anyName = true; }
        else if (strncmp(norm, prefix, seg + 1) || strlen(prefix) != seg + 1) prefixOk = false;
    }
    size_t strip = (prefixOk && anyName) ? strlen(prefix) : 0;

    // pass 2: classify
    fseek(zf_, static_cast<long>(cdOffset_), SEEK_SET);
    for (uint16_t i = 0; i < total; ++i) {
        if (fread(h, 1, 46, zf_) != 46 || get32(h) != kSigCentral) { snprintf(err, errLen, "corrupt central directory"); return false; }
        uint16_t flags  = get16(h + 8);
        uint16_t method = get16(h + 10);
        uint32_t crc    = get32(h + 16);
        uint32_t csize  = get32(h + 20);
        uint32_t usize  = get32(h + 24);
        uint16_t nlen   = get16(h + 28);
        uint32_t loff   = get32(h + 42);
        if (fread(raw, 1, nlen, zf_) != nlen) { snprintf(err, errLen, "corrupt central directory"); return false; }
        raw[nlen] = '\0';
        fseek(zf_, get16(h + 30) + get16(h + 32), SEEK_CUR);

        normalize(raw, norm, sizeof(norm));
        if (skipName(norm)) continue;
        const char* name = norm + ((strip && !strncmp(norm, prefix, strip)) ? strip : 0);

        if (strstr(name, "..") || name[0] == '/' || strchr(name, ':')) { reject(raw, "unsafe path"); continue; }
        if (flags & 1)                                { reject(name, "encrypted"); continue; }
        if (method != 0 && method != 8)               { reject(name, "unsupported compression"); continue; }
        if (csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu) { reject(name, "zip64 not supported"); continue; }

        bool isCfg = !strcmp(name, BBS_CONFIG_FILE) || !strcmp(name, BBS_USERS_FILE);
        bool isScreen = !strncmp(name, BBS_SCREEN_DIR "/", sizeof(BBS_SCREEN_DIR)) &&
                        validScreenName(name + sizeof(BBS_SCREEN_DIR));
        if (!isCfg && !isScreen)                      { reject(name, "not allowed (see SCREENS.md)"); continue; }
        uint32_t cap = !strcmp(name, BBS_USERS_FILE) ? BBS_ZIP_USERS_MAX : BBS_ZIP_FILE_MAX;
        if (usize > cap)                              { reject(name, "file too big"); continue; }
        if (method == 0 && csize != usize)            { reject(name, "corrupt entry"); continue; }
        if (static_cast<uint64_t>(loff) + 30u + csize > cdOffset_) { reject(name, "corrupt entry"); continue; }

        bool dup = false;
        for (uint8_t k = 0; k < itemCount_; ++k) if (!strcmp(items_[k].name, name)) dup = true;
        if (dup)                                      { reject(name, "duplicate"); continue; }
        if (itemCount_ >= BBS_ZIP_MAX_FILES)          { reject(name, "over 64 files"); continue; }
        if (rep_.bytes + usize > BBS_ZIP_TOTAL_MAX)   { reject(name, "total size limit"); continue; }

        Item& it = items_[itemCount_++];
        strncpy(it.name, name, sizeof(it.name) - 1);
        it.name[sizeof(it.name) - 1] = '\0';
        it.method   = static_cast<uint8_t>(method);
        it.crc      = crc;
        it.csize    = csize;
        it.usize    = usize;
        it.localOff = loff;
        it.ok       = false;
        rep_.bytes += usize;
    }

    for (uint8_t i = 1; i < itemCount_; ++i) {         // system.cfg first: users.txt
        if (strcmp(items_[i].name, BBS_CONFIG_FILE)) continue;   // is checked against it
        Item tmp = items_[0];
        items_[0] = items_[i];
        items_[i] = tmp;
        break;
    }

    char dir[96];
    stagePath(dir, sizeof(dir), BBS_SCREEN_DIR);
    ensureDir(dir);
    return true;
}

// ---------------------------------------------------------------------------
// extract: one entry into staging, CRC and size checked
// ---------------------------------------------------------------------------
namespace {

struct Pipe {
    FILE*    in;
    uint32_t left;           // compressed bytes still to read
    FILE*    out;
    uint32_t produced;
    uint32_t cap;
    uint32_t crc;
    bool     writeFailed;
};

size_t pipeIn(void* ctx, uint8_t* buf, size_t cap) {
    Pipe* p = static_cast<Pipe*>(ctx);
    if (!p->left) return 0;
    if (cap > p->left) cap = p->left;
    size_t n = fread(buf, 1, cap, p->in);
    p->left -= static_cast<uint32_t>(n);
    return n;
}

bool pipeOut(void* ctx, const uint8_t* data, size_t n) {
    Pipe* p = static_cast<Pipe*>(ctx);
    if (p->produced + n > p->cap) return false;              // lies about its size: stop
    if (fwrite(data, 1, n, p->out) != n) { p->writeFailed = true; return false; }
    p->crc = crc32::update(p->crc, data, n);
    p->produced += static_cast<uint32_t>(n);
    return true;
}

} // namespace

bool ZipImport::extract(Item& it) {
    uint8_t h[30];
    fseek(zf_, static_cast<long>(it.localOff), SEEK_SET);
    if (fread(h, 1, 30, zf_) != 30 || get32(h) != kSigLocal) { reject(it.name, "corrupt local header"); return false; }
    uint32_t dataOff = it.localOff + 30u + get16(h + 26) + get16(h + 28);
    if (static_cast<uint64_t>(dataOff) + it.csize > cdOffset_) { reject(it.name, "corrupt entry"); return false; }
    fseek(zf_, static_cast<long>(dataOff), SEEK_SET);

    char path[112];
    stagePath(path, sizeof(path), it.name);
    FILE* out = fopen(path, "wb");
    if (!out) { reject(it.name, "cannot write staging"); return false; }

    Pipe p{ zf_, it.csize, out, 0, it.usize, 0, false };
    bool ok;
    if (it.method == 0) {
        uint8_t buf[512];
        ok = true;
        size_t n;
        while ((n = pipeIn(&p, buf, sizeof(buf))) > 0) {
            if (!pipeOut(&p, buf, n)) { ok = false; break; }
        }
    } else {
        ok = plat::inflateRaw(pipeIn, pipeOut, &p);
    }
    fclose(out);

    const char* why = nullptr;
    if (p.writeFailed)                 why = "storage full";
    else if (!ok)                      why = "corrupt data";
    else if (p.produced != it.usize)   why = "size mismatch";
    else if (p.crc != it.crc)          why = "CRC mismatch";

    if (!why && !strcmp(it.name, BBS_CONFIG_FILE)) {
        // Parsed into syscfg's own scratch rather than a SysConfig of ours:
        // two static probes here were 820 bytes of DRAM held for the length
        // of one call each (syscfg::check).
        static char cfgErr[96];
        if (syscfg::check(path, cfgErr, sizeof(cfgErr))) why = cfgErr;
    }
    if (!why && !strcmp(it.name, BBS_USERS_FILE)) {
        static char usersErr[96];
        static char usersWarn[64];
        users::Issues iss;
        iss.err = usersErr;   iss.errLen = sizeof(usersErr);
        iss.warn = usersWarn; iss.warnLen = sizeof(usersWarn);
        // Limits from the uploaded config, when the zip carries one and it
        // parses cleanly; otherwise validateFile falls back to the live one.
        char cfgPath[96];
        stagePath(cfgPath, sizeof(cfgPath), BBS_CONFIG_FILE);
        struct stat cst;
        char ignore[8];
        uint8_t upMax = 0;
        if (stat(cfgPath, &cst) == 0 && !syscfg::check(cfgPath, ignore, 0, &upMax)) {
            iss.maxUsers = upMax;
        }
        if (users::validateFile(path, iss)) {
            why = usersErr;
        } else if (iss.warnings) {                   // accepted, but say what is dropped
            snprintf(rep_.note, sizeof(rep_.note), "users.txt %.40s%s", usersWarn,
                     iss.warnings > 1 ? " (and more)" : "");
            plat::log("backup: users.txt %s (%d warning%s)", usersWarn, iss.warnings,
                      iss.warnings == 1 ? "" : "s");
        }
    }
    if (why) {
        remove(path);
        reject(it.name, why);
        return false;
    }
    return true;
}

bool ZipImport::step() {
    if (next_ >= itemCount_) {
        if (zf_) {                                   // first time past the end: finish up
            fclose(zf_);
            zf_ = nullptr;
            countRemovals();
        }
        return false;
    }
    Item& it = items_[next_++];
    it.ok = extract(it);
    if (it.ok) {
        ++rep_.accepted;
        if (!strcmp(it.name, BBS_CONFIG_FILE))     rep_.hasCfg = true;
        else if (!strcmp(it.name, BBS_USERS_FILE)) rep_.hasUsers = true;
        else                                       rep_.hasScreens = true;
    } else {
        rep_.bytes -= it.usize;
    }
    return true;
}

// countRemovals: live screens that an upload with screens would delete
void ZipImport::countRemovals() {
    rep_.removed = 0;
    if (!rep_.hasScreens) return;
    char dir[96];
    livePath(dir, sizeof(dir), BBS_SCREEN_DIR);
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* e;
    char name[40];
    while ((e = readdir(d)) != nullptr) {
        if (!validScreenName(e->d_name)) continue;
        snprintf(name, sizeof(name), "%s/%.12s", BBS_SCREEN_DIR, e->d_name);
        bool kept = false;
        for (uint8_t k = 0; k < itemCount_; ++k) if (items_[k].ok && !strcmp(items_[k].name, name)) kept = true;
        if (!kept && rep_.removed < 255) ++rep_.removed;
    }
    closedir(d);
}

// ---------------------------------------------------------------------------
// apply: config with *** restored, screens renamed in, stale screens removed
// ---------------------------------------------------------------------------
bool ZipImport::apply(char* msg, size_t msgLen) {
    char src[112], dst[112];
    int failures = 0;
    uint8_t screens = 0;

    livePath(dst, sizeof(dst), BBS_SCREEN_DIR);
    ensureDir(dst);

    for (uint8_t k = 0; k < itemCount_; ++k) {
        Item& it = items_[k];
        if (!it.ok) continue;
        stagePath(src, sizeof(src), it.name);
        livePath(dst, sizeof(dst), it.name);

        if (!strcmp(it.name, BBS_CONFIG_FILE)) {
            char tmp[124];
            snprintf(tmp, sizeof(tmp), "%.110s.new", dst);
            FILE* in  = fopen(src, "r");
            FILE* out = fopen(tmp, "w");
            bool ok = in && out;
            char line[176], merged[176];
            unsigned dropped = 0;
            while (ok && fgets(line, sizeof(line), in)) {
                const char* text = line;
                if (syscfg::unredactLine(line, merged, sizeof(merged))) {
                    if (!merged[0]) { ++dropped; continue; }   // the published password: left out
                    text = merged;
                }
                ok = fputs(text, out) >= 0;
            }
            if (in)  fclose(in);
            if (out) fclose(out);
            if (dropped)                                        // never the value, only that it happened
                plat::log("backup: %u staff password line%s left out: the published password is never written",
                          dropped, dropped == 1 ? "" : "s");
            if (ok && rename(tmp, dst) == 0) { remove(src); }
            else { remove(tmp); ++failures; }
            continue;
        }
        bool moved = rename(src, dst) == 0;
        if (!moved) {
            remove(dst);                             // some filesystems will not rename over a file
            moved = rename(src, dst) == 0;
        }
        if (!moved)                                   ++failures;
        else if (strcmp(it.name, BBS_USERS_FILE) != 0) ++screens;
    }

    uint8_t removed = 0;
    if (rep_.hasScreens) {
        livePath(src, sizeof(src), BBS_SCREEN_DIR);
        DIR* d = opendir(src);
        if (d) {
            struct dirent* e;
            char name[40];
            char stale[BBS_ZIP_MAX_FILES][16];
            uint8_t staleCount = 0;
            while ((e = readdir(d)) != nullptr) {
                if (!validScreenName(e->d_name)) continue;
                snprintf(name, sizeof(name), "%s/%.12s", BBS_SCREEN_DIR, e->d_name);
                bool kept = false;
                for (uint8_t k = 0; k < itemCount_; ++k) if (items_[k].ok && !strcmp(items_[k].name, name)) kept = true;
                if (!kept && staleCount < BBS_ZIP_MAX_FILES) {
                    strncpy(stale[staleCount], e->d_name, sizeof(stale[0]) - 1);
                    stale[staleCount][sizeof(stale[0]) - 1] = '\0';
                    ++staleCount;
                }
            }
            closedir(d);
            for (uint8_t k = 0; k < staleCount; ++k) {         // delete after the walk
                snprintf(name, sizeof(name), "%s/%.15s", BBS_SCREEN_DIR, stale[k]);
                livePath(dst, sizeof(dst), name);
                if (remove(dst) == 0) ++removed; else ++failures;
            }
        }
    }

    char cfgMsg[112] = "";
    bool cfgLive = false;
    if (rep_.hasCfg) {
        char err[96];
        if (syscfg::reload(err, sizeof(err))) { snprintf(cfgMsg, sizeof(cfgMsg), "system.cfg reloaded, "); cfgLive = true; }
        else { snprintf(cfgMsg, sizeof(cfgMsg), "system.cfg NOT reloaded (%.60s), ", err); ++failures; }
    }
    discard();

    // A restore cannot bring a sysop password back (a download carries ***),
    // so a board that was on the published default is still on it. Say so,
    // or the sysop learns it from the listing that never appears.
    snprintf(msg, msgLen, "Applied: %s%s%u screen%s, %u removed%s%s", cfgMsg,
             rep_.hasUsers ? "users.txt, " : "",
             screens, screens == 1 ? "" : "s", removed, failures ? ", with errors" : "",
             cfgLive && syscfg::get().sysopDefault
                 ? "; sysop password is the published default, local network only" : "");
    plat::log("backup: %s", msg);
    return failures == 0;
}

void ZipImport::discard() {
    if (zf_) { fclose(zf_); zf_ = nullptr; }
    char dir[96];
    stagePath(dir, sizeof(dir), BBS_SCREEN_DIR);
    removeDir(dir);
    snprintf(dir, sizeof(dir), "%s/%s", plat::fsBase(), BBS_BACKUP_STAGING);
    removeDir(dir);
}

} // namespace ziparc
