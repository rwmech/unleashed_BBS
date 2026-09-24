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

#include "ziparc.h"
#include "backup.h"     // sdCardInfo
#include "crc32.h"
#include "sysconfig.h"
#include "users.h"
#include "clock.h"
#include "../platform/platform.h"

#include <cerrno>
#include <cstring>
#include <strings.h>
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
constexpr char     kInfoDir[]  = "info";       // info/3.txt in the zip

void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
void put32(uint8_t* p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, (v >> 16) & 0xFFFF); }
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) { return get16(p) | (static_cast<uint32_t>(get16(p + 2)) << 16); }

bool isInfo(const char* name) { return validInfoName(name); }

// fnv: FNV-1a, to tell whether a setting changed across a reload without
// keeping a copy of it (bbsu::hash is the same function, behind a header
// that would bring the whole of bbs.h in with it).
uint32_t fnv(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; ++s) { h ^= static_cast<uint8_t>(*s); h *= 16777619u; }
    return h;
}

// livePath: where a file in the zip belongs on the board. The backup spans
// two partitions: the screens are on the one a filesystem upload can
// replace, while the config, the accounts and the information pages sit on
// the user partition that survives a reflash. Restoring has to put each
// back where it lives. An information page is info/3.txt in the zip and
// p/info/3.txt on the board, the info plugin's own folder.
void livePath(char* out, size_t n, const char* name) {
    if (isInfo(name)) {
        snprintf(out, n, "%s/%s/%s", plat::userBase(), BBS_PLUGIN_DIR, name);
        return;
    }
    bool owned = !strcmp(name, BBS_CONFIG_FILE) || !strcmp(name, BBS_USERS_FILE);
    snprintf(out, n, "%s/%s", owned ? plat::userBase() : plat::fsBase(), name);
}

void defaultStage(char* out, size_t n) {
    snprintf(out, n, "%s/%s", plat::userBase(), BBS_BACKUP_STAGING);
}

void ensureDir(const char* path) {
    mkdir(path, 0755);                          // EEXIST is fine
}

// removeDir: delete regular files in a directory, then the directory
void removeDir(const char* dir) {
    DIR* d = opendir(dir);
    if (d) {
        struct dirent* e;
        char path[160];
        while ((e = readdir(d)) != nullptr) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(path, sizeof(path), "%.90s/%.60s", dir, e->d_name);
            remove(path);
        }
        closedir(d);
    }
    rmdir(dir);
}

// removeStaging: a staging folder and the two folders inside it
void removeStaging(const char* stage) {
    char dir[112];
    snprintf(dir, sizeof(dir), "%.90s/%s", stage, BBS_SCREEN_DIR);
    removeDir(dir);
    snprintf(dir, sizeof(dir), "%.90s/%s", stage, kInfoDir);
    removeDir(dir);
    removeDir(stage);
}

// blocks: what a file of n bytes costs on LittleFS, whole 4 KB blocks. A few
// hundred bytes can live inside the directory's own metadata, but counting
// them as a block each errs toward "no room" rather than a restore that runs
// out half way.
uint32_t blocks(uint32_t n) {
    uint32_t b = (n + BBS_FS_BLOCK - 1) / BBS_FS_BLOCK;
    return b ? b : 1;
}

// copyFile: src to dst whole, or nothing: a failed copy removes its own
// half. The buffer is on the stack and small, because the backup path is
// already the deepest the task has (bbs.h, stackWatch).
bool copyFile(const char* src, const char* dst) {
    FILE* in = fopen(src, "rb");
    if (!in) return false;
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    uint8_t buf[256];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    if (ferror(in)) ok = false;
    fclose(in);
    if (fclose(out) != 0) ok = false;          // FAT says it is full here
    if (!ok) remove(dst);
    return ok;
}

// ---------------------------------------------------------------------------
// moveFile: a staged file into its live place, never losing the live one.
//
// A rename is the whole job when both are on one filesystem, and on
// LittleFS it replaces the old file atomically. It is refused (EXDEV) when
// they are not, and before 1.1.0 the restore answered that by removing the
// live file and trying the same rename again, which failed the same way: on
// the board, where the staging and the accounts were on different
// partitions, restoring a users.txt deleted every account. The host has one
// filesystem, so no host test ever saw it.
//
// Across partitions the file is copied to "<dst>.new" on the far side first,
// and only swapped in once the copy is whole. FAT will not rename over a
// file, so there, and only once the new copy exists, the old one goes first.
//
// Only "the file is there" (EEXIST, which is FAT's answer) ever lets the old
// file go first. Any other failure (no room to write the directory, a flash
// fault, a file somebody has open) leaves the live file exactly as it was
// and says the move failed: the old code removed it on every failure but
// EXDEV, which was the same lost accounts on a rarer path.
// ---------------------------------------------------------------------------
bool moveFile(const char* src, const char* dst) {
    if (rename(src, dst) == 0) return true;
    if (errno != EXDEV) {
        if (errno != EEXIST) return false;
        // Same filesystem, and it would not rename over the file: FAT. The
        // staged copy is whole already, so the old one can go.
        remove(dst);
        return rename(src, dst) == 0;
    }
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%.118s.new", dst);
    if (!copyFile(src, tmp)) return false;
    if (rename(tmp, dst) != 0) {
        if (errno != EEXIST) { remove(tmp); return false; }
        remove(dst);
        if (rename(tmp, dst) != 0) { remove(tmp); return false; }
    }
    remove(src);
    return true;
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

// ---------------------------------------------------------------------------
// Reading a staged system.cfg for the restore's question.
//
// Only the five keys the question is about, and only the top of the file
// where the parser reads them. All five take the rest of their line, '#'
// and all (sysconfig.cpp), so this does too. The file has already passed
// syscfg::check by the time this runs.
// ---------------------------------------------------------------------------
enum : uint8_t { K_SYSOP, K_CO1, K_CO2, K_SSID, K_PASS, K_COUNT };
constexpr const char* kCfgKeys[K_COUNT] = {
    "sysop_password", "cosysop1_password", "cosysop2_password", "wifi_ssid", "wifi_password",
};

// What each of the five says, as a kind and a hash of the value: the
// question only ever asks whether a value is the board's own, which is an
// equality, so no copy of a password is kept, on the stack or anywhere.
enum : uint8_t { V_TEXT, V_EMPTY, V_KEEP, V_DEFAULT };

struct CfgPeek {
    bool     present[K_COUNT] = {};
    uint8_t  kind[K_COUNT]    = {};
    uint32_t hash[K_COUNT]    = {};
};

void peekCfg(const char* path, CfgPeek& out) {
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[176];
    while (fgets(line, sizeof(line), f)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '[') break;                       // the keys live above the first section
        if (*p == '#' || *p == ';' || !*p || *p == '\n' || *p == '\r') continue;
        for (uint8_t k = 0; k < K_COUNT; ++k) {
            size_t kl = strlen(kCfgKeys[k]);
            if (strncasecmp(p, kCfgKeys[k], kl)) continue;
            const char* v = p + kl;
            while (*v == ' ' || *v == '\t') ++v;
            if (*v != '=') continue;
            ++v;
            while (*v == ' ' || *v == '\t') ++v;
            size_t w = 0;
            while (v[w] && v[w] != '\n' && v[w] != '\r') ++w;
            while (w && (v[w - 1] == ' ' || v[w - 1] == '\t')) --w;
            uint32_t h = 2166136261u;
            for (size_t i = 0; i < w; ++i) { h ^= static_cast<uint8_t>(v[i]); h *= 16777619u; }
            size_t dl = strlen(BBS_DEFAULT_SYSOP);
            out.kind[k] = !w                                   ? V_EMPTY
                        : (w == 3 && !strncmp(v, "***", 3))     ? V_KEEP
                        : (w == dl && !strncmp(v, BBS_DEFAULT_SYSOP, dl)) ? V_DEFAULT
                                                               : V_TEXT;
            out.hash[k]    = h;                     // fnv("") for an empty one
            out.present[k] = true;                  // a later line wins, as in the parser
        }
    }
    fclose(f);
}

// What a staff level will be after the restore, and what it is now. Worked
// out rather than assumed, because "passwords stay as they are" is only true
// of a zip whose staff lines all say ***: a line left out puts the sysop on
// the published default and a co-sysop off, an empty one switches a level
// off, and a typed one sets it. Two answers compare equal when they are.
enum class Staff : uint8_t { Off, Default, Set };

Staff staffNow(uint8_t k, const char*& value) {
    const SysConfig& c = syscfg::get();
    if (k == K_SYSOP) {
        value = c.sysopPass;
        if (c.sysopDefault) return Staff::Default;
        return c.sysopPass[0] ? Staff::Set : Staff::Off;
    }
    value = c.coPass[k - K_CO1];
    return value[0] ? Staff::Set : Staff::Off;
}

bool staffWouldChange(uint8_t k, const CfgPeek& z) {
    const char* live = nullptr;
    Staff now = staffNow(k, live);
    Staff absent = k == K_SYSOP ? Staff::Default : Staff::Off;
    if (!z.present[k]) return now != absent;
    switch (z.kind[k]) {
        case V_KEEP:    return false;                           // keeps what the board has
        case V_EMPTY:   return now != Staff::Off;
        // The published password is never written (1.0.2): the line is left out.
        case V_DEFAULT: return now != absent;
        default:        return now != Staff::Set || z.hash[k] != fnv(live);
    }
}

// sysopEmptied: would the zip's system.cfg leave "sysop_password =" with
// nothing after it? That is staff switched off: nobody can reach the sysop
// node again, and so nobody can open the backup window or type RESTORE SD
// to undo it, short of the cable. CONFIG refuses an empty sysop password
// for the same reason, so a restore does too. A *** that would resolve to
// an empty live password counts, for completeness: a board with staff off
// has no sysop to restore anything, so that one is never reached.
bool sysopEmptied(const CfgPeek& z) {
    if (!z.present[K_SYSOP]) return false;               // no line: the default
    if (z.kind[K_SYSOP] == V_EMPTY) return true;
    const SysConfig& c = syscfg::get();
    return z.kind[K_SYSOP] == V_KEEP && !c.sysopDefault && !c.sysopPass[0];
}

// staffKey: which of the three staff passwords a line of system.cfg sets,
// or -1. The parser's own reading of a key: leading blanks, the name, blanks,
// then '='.
int8_t staffKey(const char* line) {
    const char* p = line;
    while (*p == ' ' || *p == '\t') ++p;
    for (uint8_t k = K_SYSOP; k <= K_CO2; ++k) {
        size_t kl = strlen(kCfgKeys[k]);
        if (strncmp(p, kCfgKeys[k], kl)) continue;
        const char* v = p + kl;
        while (*v == ' ' || *v == '\t') ++v;
        if (*v == '=') return static_cast<int8_t>(k);
    }
    return -1;
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

// validInfoName: exactly info/<digit>.txt, the file the info plugin keeps a
// page in. Nothing looser: a name is also where the file goes on the board.
bool validInfoName(const char* name) {
    return name && !strncmp(name, "info/", 5) && name[5] >= '0' && name[5] <= '9' &&
           !strcmp(name + 6, ".txt");
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
// collect: the files a backup holds, in the order it holds them. Settings,
// accounts and the information pages first (none of them for a screens-only
// zip), then the screens sorted by name.
//
// At most BBS_ZIP_MAX_FILES of them, which is the number a restore accepts:
// a board whose own backup it would refuse to take back is a board without
// a backup. The list has room for the manifest on top.
// ---------------------------------------------------------------------------
void ZipExport::collect(bool screensOnly, bool snapshot) {
    abort();
    count_      = 0;
    screens_    = 0;
    manifestIn_ = false;
    total_      = 0;
    dosStamp(dosTime_, dosDate_);

    char path[96];
    struct stat st;
    if (!screensOnly) {
        livePath(path, sizeof(path), BBS_CONFIG_FILE);
        if (stat(path, &st) == 0) addEntry(BBS_CONFIG_FILE, Src::Config);
        livePath(path, sizeof(path), BBS_USERS_FILE);
        if (stat(path, &st) == 0) {
            // The window's download promises its length up front, so it
            // snapshots the accounts; a file written as it goes does not.
            if (!snapshot)            addEntry(BBS_USERS_FILE, Src::File);
            else if (snapshotUsers()) addEntry(BBS_USERS_FILE, Src::Snapshot);
        }
        // The information pages (1.1.0). A page with no text has no file,
        // or an empty one, and nothing to keep.
        for (char n = '0'; n <= '9'; ++n) {
            char name[16];
            snprintf(name, sizeof(name), "%s/%c.txt", kInfoDir, n);
            livePath(path, sizeof(path), name);
            if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) addEntry(name, Src::File);
        }
    }

    uint8_t firstScreen = count_;
    livePath(path, sizeof(path), BBS_SCREEN_DIR);
    DIR* d = opendir(path);
    if (d) {
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            if (!validScreenName(e->d_name)) continue;
            if (count_ >= BBS_ZIP_MAX_FILES) break;
            char name[32];
            snprintf(name, sizeof(name), "%s/%.12s", BBS_SCREEN_DIR, e->d_name);
            if (addEntry(name, Src::File)) ++screens_;
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
}

// ---------------------------------------------------------------------------
// scan: pick entries, size them, lay out offsets
// ---------------------------------------------------------------------------
bool ZipExport::scan(const char* hostname, char* err, size_t errLen) {
    collect(false, true);

    for (uint8_t i = 0; i < count_; ++i) measure(list_[i]);

    buildManifest(hostname);
    if (!addEntry("MANIFEST.txt", Src::Manifest)) {
        snprintf(err, errLen, "too many files");
        return false;
    }
    manifestIn_ = true;
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

// ---------------------------------------------------------------------------
// beginFile / writeFile: a zip written to a file as it goes (BACKUP SD)
// ---------------------------------------------------------------------------
void ZipExport::beginFile(bool screensOnly, uint32_t& estimate) {
    collect(screensOnly, false);
    estimate = 0;
    for (uint8_t i = 0; i < count_; ++i) {
        char path[96];
        struct stat st;
        livePath(path, sizeof(path), list_[i].name);
        uint32_t sz = (stat(path, &st) == 0) ? static_cast<uint32_t>(st.st_size) : 0;
        estimate += sz + 76u + 2u * static_cast<uint32_t>(strlen(list_[i].name));
    }
    estimate += sizeof(manifest_) + 100u + 22u;     // the manifest and the end record

    phase_    = Phase::Local;
    idx_      = 0;
    hdrLen_   = hdrPos_ = 0;
    sent_     = 0;
    total_    = 0;
    cdOffset_ = 0;
    cdSize_   = 0;
}

ZipExport::Wrote ZipExport::writeFile(FILE* out, uint8_t* buf, size_t cap, size_t budget) {
    size_t done = 0;
    while (done < budget) {
        switch (phase_) {
            case Phase::Local: {
                if (idx_ >= count_) {
                    // Every file is in, so the manifest can say how big each
                    // one really was: it goes last, as the download's does.
                    if (!manifestIn_) {
                        buildManifest(syscfg::get().hostname);
                        if (addEntry("MANIFEST.txt", Src::Manifest)) {
                            Entry& m = list_[count_ - 1];
                            m.size = manifestLen_;
                            m.crc  = crc32::update(0, reinterpret_cast<const uint8_t*>(manifest_), manifestLen_);
                            manifestIn_ = true;
                            continue;
                        }
                        manifestIn_ = true;             // no room for it: go without
                    }
                    phase_    = Phase::Central;
                    idx_      = 0;
                    cdOffset_ = total_;
                    cdSize_   = 0;
                    continue;
                }
                Entry& e = list_[idx_];
                e.offset = total_;
                if (e.src != Src::Manifest) { e.size = 0; e.crc = 0; }
                fillHeader(e, false);                   // CRC and sizes patched at the end
                if (fwrite(hdr_, 1, hdrLen_, out) != hdrLen_) return Wrote::Failed;
                total_ += hdrLen_;
                done   += hdrLen_;
                sent_   = 0;
                phase_  = Phase::Data;
                continue;
            }
            case Phase::Data: {
                Entry& e = list_[idx_];
                size_t n = readData(e, buf, cap);
                if (n) {
                    if (fwrite(buf, 1, n, out) != n) { abort(); return Wrote::Failed; }
                    if (e.src != Src::Manifest) {
                        e.crc   = crc32::update(e.crc, buf, n);
                        e.size += static_cast<uint32_t>(n);
                    }
                    sent_  += static_cast<uint32_t>(n);
                    total_ += static_cast<uint32_t>(n);
                    done   += n;
                    continue;
                }
                abort();
                // The entry has ended: its CRC and sizes go back into the
                // local header, 14 bytes in, and the file carries on at its
                // end. The central directory is written from the same figures.
                uint8_t fix[12];
                put32(fix, e.crc);
                put32(fix + 4, e.size);
                put32(fix + 8, e.size);
                if (fseek(out, static_cast<long>(e.offset) + 14, SEEK_SET) != 0 ||
                    fwrite(fix, 1, sizeof(fix), out) != sizeof(fix) ||
                    fseek(out, static_cast<long>(total_), SEEK_SET) != 0) return Wrote::Failed;
                ++idx_;
                phase_ = Phase::Local;
                return Wrote::Entry;
            }
            case Phase::Central: {
                if (idx_ >= count_) {
                    memset(hdr_, 0, 22);
                    put32(hdr_, kSigEnd);
                    put16(hdr_ + 8, count_);
                    put16(hdr_ + 10, count_);
                    put32(hdr_ + 12, cdSize_);
                    put32(hdr_ + 16, cdOffset_);
                    if (fwrite(hdr_, 1, 22, out) != 22) return Wrote::Failed;
                    total_ += 22;
                    phase_  = Phase::Done;
                    return Wrote::Done;
                }
                fillHeader(list_[idx_], true);
                if (fwrite(hdr_, 1, hdrLen_, out) != hdrLen_) return Wrote::Failed;
                cdSize_ += hdrLen_;
                total_  += hdrLen_;
                done    += hdrLen_;
                ++idx_;
                continue;
            }
            case Phase::End:
            case Phase::Done:
            default:
                return Wrote::Done;
        }
    }
    return Wrote::More;
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

void ZipImport::stagePath(char* out, size_t n, const char* name) const {
    snprintf(out, n, "%.80s/%s", stage_, name);
}

void ZipImport::reject(const char* name, const char* why) {
    if (!rep_.rejected) snprintf(rep_.firstReject, sizeof(rep_.firstReject), "%.40s: %s", name, why);
    if (rep_.rejected < 255) ++rep_.rejected;
    plat::log("backup: rejected %.40s (%s)", name, why);
}

// ---------------------------------------------------------------------------
// roomCheck: will what is about to be unpacked fit where it is unpacked?
//
// Asked before a byte is extracted, because the other way to find out is a
// restore that runs out part way, which is what the old limits made likely:
// they allowed 400,000 bytes of zip and 360,000 unpacked on a 256 KB
// partition. On the board it is counted in LittleFS's whole blocks, and the
// accounts' reserve is kept back, as a plugin has to (plugins::path).
//
// The figure is taken fresh (fsInfoStale), which on the board walks the
// user partition once: the kept one can be a minute old and would not yet
// show an upload that has just landed in staging.
// ---------------------------------------------------------------------------
bool ZipImport::roomCheck(char* err, size_t errLen) {
    uint32_t needBlocks = 3 * 2;                   // staging and its two folders
    uint32_t bytes = 0;
    for (uint8_t i = 0; i < itemCount_; ++i) {
        needBlocks += blocks(items_[i].usize);
        bytes      += items_[i].usize;
    }
    uint32_t needKB = 0, freeKB = 0;
    if (mode_ == Mode::Screens) {
        // The card: FAT, measured in kilobytes by the platform, and a margin
        // for its clusters rather than a block count. Through the sd
        // plugin's kept figure, as everything that asks about the card does.
        plat::SdInfo i = sdCardInfo();
        needKB = (bytes + 1023) / 1024 + 64;
        freeKB = i.freeKB;
    } else {
        uint32_t total = 0, used = 0;
        plat::fsInfoStale();
        if (!plat::userInfo(total, used)) return true;     // cannot tell: go on, as ever
        uint32_t free  = total > used ? total - used : 0;
        uint32_t avail = free > BBS_FS_RESERVE ? free - BBS_FS_RESERVE : 0;
        needKB = (needBlocks * BBS_FS_BLOCK + 1023) / 1024;
        freeKB = avail / 1024;
    }
    if (needKB <= freeKB) return true;
    needKB_ = needKB;
    freeKB_ = freeKB;
    snprintf(err, errLen, "not enough room: %u KB needed, %u KB free",
             static_cast<unsigned>(needKB), static_cast<unsigned>(freeKB));
    return false;
}

// ---------------------------------------------------------------------------
// open: find the end record, read the central directory twice (common
// folder prefix, then classification)
// ---------------------------------------------------------------------------
bool ZipImport::open(const char* zipPath, char* err, size_t errLen, Mode mode,
                     const char* stageDir, const char* destDir) {
    if (zf_) { fclose(zf_); zf_ = nullptr; }         // the upload itself lives in staging: keep it
    strncpy(zipPath_, zipPath, sizeof(zipPath_) - 1);
    zipPath_[sizeof(zipPath_) - 1] = '\0';
    if (stageDir && *stageDir) snprintf(stage_, sizeof(stage_), "%s", stageDir);
    else                       defaultStage(stage_, sizeof(stage_));
    snprintf(dest_, sizeof(dest_), "%s", destDir ? destDir : "");
    mode_       = mode;
    fail_       = Fail::Unreadable;
    needKB_     = freeKB_ = 0;
    rep_        = ImportReport();
    applied_    = ApplyReport();
    itemCount_  = 0;
    next_       = 0;
    applyPhase_ = applyIdx_ = 0;

    zf_ = fopen(zipPath, "rb");
    if (!zf_) { snprintf(err, errLen, "upload not found"); return false; }
    fseek(zf_, 0, SEEK_END);
    long size = ftell(zf_);
    if (size < 22) { snprintf(err, errLen, "not a zip file (size)"); return false; }
    if (size > BBS_ZIP_MAX_BYTES) {
        fail_ = Fail::TooBig;
        snprintf(err, errLen, "too big: %lu KB, the limit is %u KB",
                 static_cast<unsigned long>((size + 1023) / 1024),
                 static_cast<unsigned>(BBS_ZIP_MAX_BYTES / 1024));
        return false;
    }

    static uint8_t tail[22 + 1024];
    size_t tailLen = static_cast<size_t>(size) < sizeof(tail) ? static_cast<size_t>(size) : sizeof(tail);
    fseek(zf_, size - static_cast<long>(tailLen), SEEK_SET);
    if (fread(tail, 1, tailLen, zf_) != tailLen) { snprintf(err, errLen, "read error"); return false; }

    // The end record, with its comment, ends the file. Or ends it but for
    // 0x1A padding: a zip uploaded by XMODEM, which has no length field and
    // fills its last block with SUB (up to 1,023 of them for XMODEM-1K),
    // arrives that much longer, and the Backups file area takes uploads by
    // XMODEM as well as YMODEM (1.1.0). Only SUB counts as padding, so a
    // file with anything else after its end record is still not a zip.
    long eocd = -1;
    for (size_t p = tailLen - 22 + 1; p-- > 0;) {
        if (get32(tail + p) != kSigEnd) continue;
        size_t end = p + 22 + get16(tail + p + 20);
        if (end > tailLen) continue;
        bool padOnly = true;
        for (size_t q = end; q < tailLen && padOnly; ++q) padOnly = tail[q] == 0x1A;
        if (!padOnly) continue;
        eocd = size - static_cast<long>(tailLen) + static_cast<long>(p);
        break;
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
        bool known = (seg == 7 && !strncmp(norm, "screens", 7)) || (seg == 4 && !strncmp(norm, kInfoDir, 4));
        if (!slash || seg >= sizeof(prefix) - 1 || known) { prefixOk = false; continue; }
        if (!anyName) { memcpy(prefix, norm, seg + 1); prefix[seg + 1] = '\0'; anyName = true; }
        else if (strncmp(norm, prefix, seg + 1) || strlen(prefix) != seg + 1) prefixOk = false;
    }
    size_t strip = (prefixOk && anyName) ? strlen(prefix) : 0;

    // The screens partition, in whole blocks, less its root and the screens
    // folder (a metadata pair each). Only for a restore onto the board: the
    // card has room.
    //
    // What has to fit is the peak of the apply, not the set it ends with.
    // The screens the zip leaves out are removed first; then each screen is
    // copied in beside the one it replaces (moveFile: "<name>.new", then a
    // rename over it), so while one is being swapped the partition holds
    // every screen already put in, that screen's old copy and its new one,
    // and the old copies of the screens still to come. That is at most the
    // larger of old and new for each screen, plus one more copy of the
    // biggest new one. Counting the new set alone, with two blocks spare,
    // was one file short of that (1.1.0).
    uint32_t fsTotal = 0, fsUsed = 0;
    uint32_t screenRoom = 0xFFFFFFFFu;
    if (mode_ == Mode::Full && plat::fsInfo(fsTotal, fsUsed) && fsTotal) {
        uint32_t b = fsTotal / BBS_FS_BLOCK;
        screenRoom = b > 4 ? b - 4 : 0;
    }
    uint32_t screenBlocks = 0;          // the larger of old and new, each screen
    uint32_t biggest      = 0;          // blocks of the largest new screen

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

        bool isCfg    = !strcmp(name, BBS_CONFIG_FILE) || !strcmp(name, BBS_USERS_FILE);
        bool isPage   = validInfoName(name);
        bool isScreen = !strncmp(name, BBS_SCREEN_DIR "/", sizeof(BBS_SCREEN_DIR)) &&
                        validScreenName(name + sizeof(BBS_SCREEN_DIR));
        // A screens restore takes screens and says so of anything else,
        // rather than quietly holding back the settings in a full backup.
        if (mode_ == Mode::Screens && !isScreen)      { reject(name, "not a screen"); continue; }
        if (!isCfg && !isScreen && !isPage)           { reject(name, "not allowed (see SCREENS.md)"); continue; }
        uint32_t cap = !strcmp(name, BBS_USERS_FILE) ? BBS_ZIP_USERS_MAX
                     : isPage                        ? BBS_ZIP_INFO_MAX
                                                     : BBS_ZIP_FILE_MAX;
        if (usize > cap)                              { reject(name, "file too big"); continue; }
        if (method == 0 && csize != usize)            { reject(name, "corrupt entry"); continue; }
        if (static_cast<uint64_t>(loff) + 30u + csize > cdOffset_) { reject(name, "corrupt entry"); continue; }

        bool dup = false;
        for (uint8_t k = 0; k < itemCount_; ++k) if (!strcmp(items_[k].name, name)) dup = true;
        if (dup)                                      { reject(name, "duplicate"); continue; }
        if (itemCount_ >= BBS_ZIP_MAX_FILES)          { reject(name, "over 64 files"); continue; }
        if (rep_.bytes + usize > BBS_ZIP_TOTAL_MAX)   { reject(name, "total size limit"); continue; }
        if (isScreen && screenRoom != 0xFFFFFFFFu) {
            uint32_t now = blocks(usize), was = 0;
            char live[96];
            struct stat lst;
            livePath(live, sizeof(live), name);
            if (stat(live, &lst) == 0 && S_ISREG(lst.st_mode)) was = blocks(static_cast<uint32_t>(lst.st_size));
            uint32_t each = now > was ? now : was;
            uint32_t big  = now > biggest ? now : biggest;
            if (screenBlocks + each + big > screenRoom) {
                reject(name, "no room for it on the board");
                continue;
            }
            screenBlocks += each;
            biggest       = big;
        }

        Item& it = items_[itemCount_++];
        strncpy(it.name, name, sizeof(it.name) - 1);
        it.name[sizeof(it.name) - 1] = '\0';
        it.method   = static_cast<uint8_t>(method);
        it.crc      = crc;
        it.csize    = csize;
        it.usize    = usize;
        it.localOff = loff;
        it.ok       = false;
        it.live     = false;
        rep_.bytes += usize;
    }

    for (uint8_t i = 1; i < itemCount_; ++i) {         // system.cfg first: users.txt
        if (strcmp(items_[i].name, BBS_CONFIG_FILE)) continue;   // is checked against it
        Item tmp = items_[0];
        items_[0] = items_[i];
        items_[i] = tmp;
        break;
    }

    if (itemCount_ && !roomCheck(err, errLen)) { fail_ = Fail::NoRoom; return false; }

    char dir[112];
    ensureDir(stage_);
    stagePath(dir, sizeof(dir), BBS_SCREEN_DIR);
    ensureDir(dir);
    stagePath(dir, sizeof(dir), kInfoDir);
    ensureDir(dir);
    fail_ = Fail::None;
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

// inspectCfg: what the staged system.cfg would change that the question
// has to say out loud: the network, and the staff levels. Returns why the
// file is refused, or nullptr: a sysop password left empty (1.1.0).
const char* ZipImport::inspectCfg(const char* staged) {
    CfgPeek z;                         // 45 bytes: kinds and hashes, never a value
    peekCfg(staged, z);
    const SysConfig& c = syscfg::get();
    uint32_t ssid = z.present[K_SSID] ? z.hash[K_SSID] : fnv("");
    uint32_t pass = z.present[K_PASS] ? z.hash[K_PASS] : fnv("");
    rep_.wifiDiffers = ssid != fnv(c.wifiSsid) || pass != fnv(c.wifiPass);
    rep_.staffChanged = 0;
    for (uint8_t k = K_SYSOP; k <= K_CO2; ++k)
        if (staffWouldChange(k, z)) ++rep_.staffChanged;
    return sysopEmptied(z) ? "sysop password empty: staff would be off" : nullptr;
}

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
    if (fclose(out) != 0) p.writeFailed = true;

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
        else why = inspectCfg(path);
        // A refused system.cfg changes nothing, so the question must not
        // say it would.
        if (why) { rep_.staffChanged = 0; rep_.wifiDiffers = false; }
    }
    if (!why && !strcmp(it.name, BBS_USERS_FILE)) {
        static char usersErr[96];
        static char usersWarn[64];
        users::Issues iss;
        iss.err = usersErr;   iss.errLen = sizeof(usersErr);
        iss.warn = usersWarn; iss.warnLen = sizeof(usersWarn);
        // Limits from the uploaded config, when the zip carries one and it
        // parses cleanly; otherwise validateFile falls back to the live one.
        char cfgPath[112];
        stagePath(cfgPath, sizeof(cfgPath), BBS_CONFIG_FILE);
        struct stat cst;
        char ignore[8];
        uint8_t upMax = 0;
        if (stat(cfgPath, &cst) == 0 && !syscfg::check(cfgPath, ignore, 0, &upMax)) {
            iss.maxUsers = upMax;
        }
        if (users::validateFile(path, iss)) {
            why = usersErr;
        } else {
            rep_.accounts = iss.accounts;
            if (iss.warnings) {                      // accepted, but say what is dropped
                snprintf(rep_.note, sizeof(rep_.note), "users.txt %.40s%s", usersWarn,
                         iss.warnings > 1 ? " (and more)" : "");
                plat::log("backup: users.txt %s (%d warning%s)", usersWarn, iss.warnings,
                          iss.warnings == 1 ? "" : "s");
            }
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
        else if (validInfoName(it.name))           ++rep_.pages;
        else                                       { rep_.hasScreens = true; ++rep_.screens; }
    } else {
        rep_.bytes -= it.usize;
    }
    return true;
}

// countRemovals: live screens that an upload with screens would delete, or
// for a screens restore, how many of its screens the card already has.
void ZipImport::countRemovals() {
    rep_.removed  = 0;
    rep_.replaced = 0;
    if (!rep_.hasScreens) return;
    if (mode_ == Mode::Screens) {
        for (uint8_t k = 0; k < itemCount_; ++k) {
            if (!items_[k].ok) continue;
            char path[128];
            struct stat st;
            snprintf(path, sizeof(path), "%.80s/%s", dest_, items_[k].name + sizeof(BBS_SCREEN_DIR));
            if (stat(path, &st) == 0 && rep_.replaced < 255) ++rep_.replaced;
        }
        return;
    }
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
// removeOneStale: one live screen the zip leaves out, removed. False when
// there are none left. applyIdx_ counts the ones that would not go, so a
// file that cannot be removed is stepped past rather than tried for ever.
// Before the new ones are copied in, because the screens partition has to
// hold the new set, not the old and the new together.
// ---------------------------------------------------------------------------
bool ZipImport::removeOneStale() {
    char dir[96];
    livePath(dir, sizeof(dir), BBS_SCREEN_DIR);
    DIR* d = opendir(dir);
    if (!d) return false;
    struct dirent* e;
    char name[40];
    char victim[16] = "";
    uint8_t skip = applyIdx_;
    while ((e = readdir(d)) != nullptr) {
        if (!validScreenName(e->d_name)) continue;
        snprintf(name, sizeof(name), "%s/%.12s", BBS_SCREEN_DIR, e->d_name);
        bool kept = false;
        for (uint8_t k = 0; k < itemCount_; ++k) if (items_[k].ok && !strcmp(items_[k].name, name)) kept = true;
        if (kept) continue;
        if (skip) { --skip; continue; }
        snprintf(victim, sizeof(victim), "%.15s", e->d_name);
        break;
    }
    closedir(d);                                      // delete after the walk
    if (!victim[0]) return false;
    char path[128];
    snprintf(path, sizeof(path), "%.96s/%s", dir, victim);
    if (remove(path) == 0) {
        ++applied_.removed;
    } else {
        ++applied_.failures;
        ++applyIdx_;
    }
    return true;
}

// ---------------------------------------------------------------------------
// applyItem: one staged file into its live place
// ---------------------------------------------------------------------------
bool ZipImport::applyItem(Item& it) {
    char src[112], dst[128];
    stagePath(src, sizeof(src), it.name);

    if (!strcmp(it.name, BBS_CONFIG_FILE)) {
        livePath(dst, sizeof(dst), it.name);
        char tmp[140];
        snprintf(tmp, sizeof(tmp), "%.126s.new", dst);
        FILE* in  = fopen(src, "r");
        FILE* out = fopen(tmp, "w");
        bool ok = in && out;
        char line[176], merged[176];
        unsigned dropped = 0;
        while (ok && fgets(line, sizeof(line), in)) {
            const char* text = line;
            if (syscfg::unredactLine(line, merged, sizeof(merged))) {
                if (!merged[0]) {                      // the published password: left out
                    ++dropped;
                    // A co-sysop line left out is that level off. Said in
                    // the result, not only in the log (1.1.0): finishApply
                    // keeps the bit only if the level really is off once
                    // the file is read back, since a later line may set it.
                    int8_t k = staffKey(line);
                    if (k == K_CO1 || k == K_CO2)
                        applied_.coOff = static_cast<uint8_t>(applied_.coOff | (1u << (k - K_CO1)));
                    continue;
                }
                text = merged;
            }
            ok = fputs(text, out) >= 0;
        }
        if (in)  fclose(in);
        if (out && fclose(out) != 0) ok = false;
        if (dropped)                                        // never the value, only that it happened
            plat::log("backup: %u staff password line%s left out: the published password is never written",
                      dropped, dropped == 1 ? "" : "s");
        // tmp and dst are both on userdata, so this replaces it in one step.
        if (ok && rename(tmp, dst) == 0) { remove(src); return true; }
        remove(tmp);
        ++applied_.failures;
        return false;
    }

    if (mode_ == Mode::Screens) {
        snprintf(dst, sizeof(dst), "%.80s/%s", dest_, it.name + sizeof(BBS_SCREEN_DIR));
    } else {
        livePath(dst, sizeof(dst), it.name);
        if (validInfoName(it.name)) {                 // the info plugin's folder, made if new
            char dir[96];
            snprintf(dir, sizeof(dir), "%s/%s", plat::userBase(), BBS_PLUGIN_DIR);
            ensureDir(dir);
            snprintf(dir, sizeof(dir), "%s/%s/%s", plat::userBase(), BBS_PLUGIN_DIR, kInfoDir);
            ensureDir(dir);
        }
    }
    if (!moveFile(src, dst)) { ++applied_.failures; return false; }
    it.live = true;
    if (!strcmp(it.name, BBS_USERS_FILE)) applied_.users = true;
    else if (validInfoName(it.name))      ++applied_.pages;
    else                                  ++applied_.screens;
    return true;
}

// nthLive: the file name of the i-th screen a screens restore put on the
// card ("about.asc"), or nullptr past the last. For sdSeededMark.
const char* ZipImport::nthLive(void* ctx, uint8_t i) {
    const ZipImport* z = static_cast<const ZipImport*>(ctx);
    for (uint8_t k = 0; k < z->itemCount_; ++k) {
        if (!z->items_[k].live) continue;
        if (!i--) return z->items_[k].name + sizeof(BBS_SCREEN_DIR);
    }
    return nullptr;
}

// finishApply: reload what was put live, and throw staging away
void ZipImport::finishApply() {
    if (rep_.hasCfg) {
        const SysConfig& c = syscfg::get();
        uint32_t host = fnv(c.hostname);
        uint32_t ssid = fnv(c.wifiSsid);
        uint32_t pass = fnv(c.wifiPass);
        applied_.cfgTried = true;
        char err[96];
        if (syscfg::reload(err, sizeof(err))) {
            applied_.cfgLive     = true;
            const SysConfig& n   = syscfg::get();
            applied_.hostChanged = fnv(n.hostname) != host;
            applied_.wifiChanged = fnv(n.wifiSsid) != ssid || fnv(n.wifiPass) != pass;
            // A co-sysop whose line was left out is only off if nothing
            // later in the file set it after all.
            for (uint8_t i = 0; i < 2; ++i)
                if (n.coPass[i][0]) applied_.coOff = static_cast<uint8_t>(applied_.coOff & ~(1u << i));
        } else {
            snprintf(applied_.cfgErr, sizeof(applied_.cfgErr), "%.60s", err);
            ++applied_.failures;
            applied_.coOff = 0;           // the old settings are still running
        }
    } else {
        applied_.coOff = 0;
    }
    // A screen a screens restore put on the card is the sysop's own from now
    // on: marked so in the seeded-screens manifest, so a stock update never
    // takes it back, even where it happens to match a stock screen byte for
    // byte (sd.cpp, seedScreens).
    if (mode_ == Mode::Screens && applied_.screens) sdSeededMark(nthLive, this);
    discard();
    plat::fsInfoStale();                  // screens and accounts were rewritten wholesale
    char msg[192];
    applyMessage(msg, sizeof(msg));
    plat::log("backup: %s", msg);
}

// ---------------------------------------------------------------------------
// applyStep: config with *** restored, screens in, stale screens out, a
// file a call. See ziparc.h.
// ---------------------------------------------------------------------------
bool ZipImport::applyStep() {
    switch (applyPhase_) {
        case 0: {
            if (mode_ == Mode::Full && rep_.hasScreens && removeOneStale()) return true;
            char dir[96];
            if (mode_ == Mode::Screens) ensureDir(dest_);
            else { livePath(dir, sizeof(dir), BBS_SCREEN_DIR); ensureDir(dir); }
            applyPhase_ = 1;
            applyIdx_   = 0;
            return true;
        }
        case 1:
            while (applyIdx_ < itemCount_ && !items_[applyIdx_].ok) ++applyIdx_;
            if (applyIdx_ < itemCount_) {
                applyItem(items_[applyIdx_++]);
                return true;
            }
            applyPhase_ = 2;
            return true;
        case 2:
            finishApply();
            applyPhase_ = 3;
            return false;
        default:
            return false;
    }
}

void ZipImport::applyMessage(char* msg, size_t msgLen) const {
    const ApplyReport& a = applied_;
    if (mode_ == Mode::Screens) {
        snprintf(msg, msgLen, "Applied: %u screen%s on the card%s", a.screens,
                 a.screens == 1 ? "" : "s", a.failures ? ", with errors" : "");
        return;
    }
    char cfgMsg[112] = "";
    if (a.cfgTried) {
        if (a.cfgLive) snprintf(cfgMsg, sizeof(cfgMsg), "system.cfg reloaded, ");
        else           snprintf(cfgMsg, sizeof(cfgMsg), "system.cfg NOT reloaded (%.60s), ", a.cfgErr);
    }
    char pages[24] = "";
    if (a.pages) snprintf(pages, sizeof(pages), "%u info page%s, ", a.pages, a.pages == 1 ? "" : "s");
    // A restore cannot bring a sysop password back (a download carries ***),
    // so a board that was on the published default is still on it. Say so,
    // or the sysop learns it from the listing that never appears.
    snprintf(msg, msgLen, "Applied: %s%s%s%u screen%s, %u removed%s%s", cfgMsg,
             a.users ? "users.txt, " : "", pages,
             a.screens, a.screens == 1 ? "" : "s", a.removed, a.failures ? ", with errors" : "",
             a.cfgLive && syscfg::get().sysopDefault
                 ? "; sysop password is the published default, local network only" : "");
}

void ZipImport::discard() {
    if (zf_) { fclose(zf_); zf_ = nullptr; }
    char dflt[96];
    defaultStage(dflt, sizeof(dflt));
    if (stage_[0] && strcmp(stage_, dflt)) removeStaging(stage_);
    removeStaging(dflt);
    // Where staging was before 1.1.0, on the screens partition. A board
    // updated in the middle of a restore would otherwise keep it for ever.
    char legacy[96];
    snprintf(legacy, sizeof(legacy), "%s/%s", plat::fsBase(), BBS_BACKUP_STAGING);
    removeStaging(legacy);
}

} // namespace ziparc
