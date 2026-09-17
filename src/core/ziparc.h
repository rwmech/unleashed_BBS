/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/ziparc.h
 * Module:       Core / backup archive
 *
 * Purpose:      The backup archive. Everything that matters lives in the
 *                  storage filesystem and travels as one .zip:
 *                    system.cfg          passwords written as *** on download
 *                    users.txt           accounts (password hashes, not passwords)
 *                    screens/<name>.<ext>
 *                    MANIFEST.txt        informational, ignored on upload
 *                  Logs live on their own partition and never enter the zip.
 *
 *                  ZipExport  streams a stored (uncompressed) zip without buffering it:
 *                  scan() sizes every entry and computes CRCs, produce() is
 *                  called until it returns 0.
 *                  ZipImport  takes an uploaded zip (stored or deflated), validates it
 *                  against the allow-list and limits, extracts accepted files
 *                  into <fs>/.staging one per step(), then apply() swaps them
 *                  in (or discard() throws them away).
 *
 *                  Upload rules (see SCREENS.md): system.cfg and screens/<a-z0-9_-, 1..8>
 *                  .asc .ans .seq .p40 .p80 only; one file <= BBS_ZIP_FILE_MAX; at most
 *                  BBS_ZIP_MAX_FILES files and BBS_ZIP_TOTAL_MAX bytes unpacked. A single
 *                  top-level folder (from re-zipping an unpacked folder) is stripped.
 *                  When the upload carries screens, the live screens directory mirrors it.
 *
 * Libraries:    none (libc stdio, dirent)
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

#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include "../config.h"

namespace ziparc {

// validScreenName: "welcome.ans" style base.ext check (lowercase expected)
bool validScreenName(const char* file);

// ===========================================================================
// ZipExport
// ===========================================================================
class ZipExport {
public:
    // scan: collect entries and sizes. False with err when nothing can be sent.
    bool scan(const char* hostname, char* err, size_t errLen);

    // totalBytes: exact size of the zip scan() described
    uint32_t totalBytes() const { return total_; }
    uint8_t  entries()    const { return count_; }

    // produce: next bytes of the zip, 0 when finished
    size_t produce(uint8_t* buf, size_t cap);

    // abort: close any open file (client went away)
    void abort();

private:
    enum class Src   : uint8_t { File, Config, Manifest };
    enum class Phase : uint8_t { Local, Data, Central, End, Done };

    struct Entry {
        char     name[32];     // path inside the zip
        Src      src;
        uint32_t size;
        uint32_t crc;
        uint32_t offset;       // local header offset
    };

    bool   addEntry(const char* name, Src src);
    bool   measure(Entry& e);
    size_t readData(Entry& e, uint8_t* buf, size_t cap);
    size_t cfgRead(uint8_t* buf, size_t cap);
    void   buildManifest(const char* hostname);
    void   fillHeader(const Entry& e, bool central);

    Entry    list_[BBS_ZIP_MAX_FILES + 2];
    uint8_t  count_ = 0;
    uint32_t total_ = 0;
    uint32_t cdOffset_ = 0;
    uint32_t cdSize_   = 0;
    uint16_t dosTime_  = 0;
    uint16_t dosDate_  = 0;

    // manifest text lives in RAM
    char     manifest_[1536] = {};
    uint16_t manifestLen_    = 0;

    // streaming state
    Phase    phase_  = Phase::Done;
    uint8_t  idx_    = 0;
    uint8_t  hdr_[96] = {};
    uint8_t  hdrLen_ = 0;
    uint8_t  hdrPos_ = 0;
    FILE*    f_      = nullptr;
    uint32_t sent_   = 0;          // bytes of the current entry's data sent
    char     line_[176] = {};      // redacted config line in progress
    uint8_t  lineLen_ = 0;
    uint8_t  linePos_ = 0;
};

// ===========================================================================
// ZipImport
// ===========================================================================
struct ImportReport {
    uint8_t  accepted = 0;
    uint8_t  rejected = 0;
    uint8_t  removed  = 0;         // live screens the upload deletes
    uint32_t bytes    = 0;         // unpacked bytes accepted
    bool     hasCfg     = false;
    bool     hasUsers   = false;
    bool     hasScreens = false;
    char     firstReject[96] = {};
};

class ZipImport {
public:
    // open: parse the central directory of zipPath and classify every entry
    bool open(const char* zipPath, char* err, size_t errLen);

    // step: extract the next accepted entry into staging. False when done.
    bool step();

    const ImportReport& report() const { return rep_; }

    // apply: move staged files live, mirror screens, reload system.cfg
    bool apply(char* msg, size_t msgLen);

    // discard: delete staging and the uploaded zip
    void discard();

private:
    struct Item {
        char     name[32];     // normalized: system.cfg or screens/x.ext
        uint8_t  method;       // 0 stored, 8 deflate
        uint32_t crc;
        uint32_t csize;
        uint32_t usize;
        uint32_t localOff;
        bool     ok;           // still accepted after extraction
    };

    void reject(const char* name, const char* why);
    bool extract(Item& it);
    void countRemovals();

    char         zipPath_[96] = {};
    FILE*        zf_       = nullptr;
    uint32_t     cdOffset_ = 0;
    Item         items_[BBS_ZIP_MAX_FILES];
    uint8_t      itemCount_ = 0;
    uint8_t      next_      = 0;
    ImportReport rep_;
};

} // namespace ziparc
