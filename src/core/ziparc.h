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
 *                    info/<n>.txt        the information pages, 0 to 9 (1.1.0)
 *                    screens/<name>.<ext>
 *                    MANIFEST.txt        informational, ignored on upload
 *                  Logs live on their own partition and never enter the zip.
 *
 *                  ZipExport  streams a stored (uncompressed) zip without buffering it:
 *                  scan() sizes every entry and computes CRCs, produce() is
 *                  called until it returns 0. Or, for a zip written to the SD
 *                  card (BACKUP SD, 1.1.0), beginFile() then writeFile() a
 *                  step at a time, patching each entry's header as it ends.
 *                  ZipImport  takes an uploaded zip (stored or deflated), validates it
 *                  against the allow-list and limits, extracts accepted files
 *                  into staging one per step(), then applyStep() puts them
 *                  live one per call (or discard() throws them away).
 *
 *                  Upload rules (see SCREENS.md): system.cfg, users.txt,
 *                  info/<0-9>.txt and screens/<a-z0-9_-, 1..8> .asc .ans .seq
 *                  .p40 .p80 only; one file <= BBS_ZIP_FILE_MAX; at most
 *                  BBS_ZIP_MAX_FILES files and BBS_ZIP_TOTAL_MAX bytes unpacked,
 *                  and only as much as the board has room for. A single
 *                  top-level folder (from re-zipping an unpacked folder) is stripped.
 *                  When the upload carries screens, the live screens directory mirrors it.
 *
 *                  Mode::Screens (RESTORE SD SCREENS) takes the screens and
 *                  nothing else, and puts them in the card's screens folder,
 *                  the override layer, where they only ever add or replace.
 *                  What it puts there is marked as the sysop's own in the
 *                  seeded-screens manifest (sdSeededMark), 1.1.0.
 *
 *                  A zip that ends in 0x1A padding after its end record, as
 *                  one uploaded by XMODEM does, is read as the zip it is.
 *
 * Libraries:    none (libc stdio, dirent)
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

#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include "../config.h"

namespace ziparc {

// validScreenName: "welcome.ans" style base.ext check (lowercase expected)
bool validScreenName(const char* file);

// validInfoName: "info/3.txt", a page 0 to 9 (1.1.0)
bool validInfoName(const char* name);

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

    // -- written to a file as it goes (BACKUP SD, 1.1.0) ----------------------
    //
    // No copy of users.txt and no measuring pass first, which is what scan()
    // needs because an HTTP reply states its length before the first byte.
    // A file can be gone back over: each entry's CRC and size are worked out
    // as it is written and patched into its local header when it ends, and
    // the central directory at the end is written from the real figures.
    // That also means the zip is spread across as many loop passes as it
    // takes, rather than reading every file twice in one of them.
    //
    // beginFile: pick the entries. screensOnly for BACKUP SD SCREENS.
    // estimate gets roughly how big the zip will be, for "Card full".
    void beginFile(bool screensOnly, uint32_t& estimate);

    // writeFile: write up to budget bytes to out, through buf. Entry when an
    // entry was finished (the caller prints a dot), Done when the zip is
    // complete, Failed on a write that did not go.
    enum class Wrote : uint8_t { More, Entry, Done, Failed };
    Wrote writeFile(FILE* out, uint8_t* buf, size_t cap, size_t budget);

    // files: entries in the zip, not counting MANIFEST.txt; screens: of them,
    // how many are screens.
    uint8_t files()   const { return count_ ? static_cast<uint8_t>(count_ - (manifestIn_ ? 1 : 0)) : 0; }
    uint8_t screens() const { return screens_; }
    // written: bytes of the zip written so far; the whole zip once Done
    uint32_t written() const { return total_; }

private:
    enum class Src   : uint8_t { File, Config, Manifest, Snapshot };
    enum class Phase : uint8_t { Local, Data, Central, End, Done };

    struct Entry {
        char     name[32];     // path inside the zip
        Src      src;
        uint32_t size;
        uint32_t crc;
        uint32_t offset;       // local header offset
    };

    // collect: the entries both ways of making a zip share, so the window's
    // download and a card backup cannot come to hold different things.
    void   collect(bool screensOnly, bool snapshot);
    bool   addEntry(const char* name, Src src);
    bool   snapshotFile(const char* name);
public:
    // dropSnapshot: delete the users.txt and callstats.dat copies taken
    // for this download
    void   dropSnapshot();
private:
    bool   measure(Entry& e);
    size_t readData(Entry& e, uint8_t* buf, size_t cap);
    size_t cfgRead(uint8_t* buf, size_t cap);
    void   buildManifest(const char* hostname);
    void   fillHeader(const Entry& e, bool central);

    Entry    list_[BBS_ZIP_MAX_FILES + 2];
    uint8_t  count_ = 0;
    uint8_t  screens_ = 0;
    bool     manifestIn_ = false;
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
    uint8_t  replaced = 0;         // Mode::Screens: accepted screens already on the card
    uint8_t  screens  = 0;         // accepted screens
    uint8_t  pages    = 0;         // accepted information pages
    uint16_t accounts = 0;         // accounts in the zip's users.txt
    uint8_t  staffChanged = 0;     // staff levels the zip's system.cfg would change
    uint32_t bytes    = 0;         // unpacked bytes accepted
    bool     hasCfg     = false;
    bool     hasUsers   = false;
    bool     hasScreens = false;
    bool     wifiDiffers = false;  // the zip's network is not the board's
    char     firstReject[96] = {};
    char     note[72]        = {};   // accepted with a warning (unknown keys)
};

// What a restore did, once applyStep has finished.
struct ApplyReport {
    uint8_t  screens  = 0;         // screens put live
    uint8_t  removed  = 0;         // live screens taken away (not in the zip)
    uint8_t  pages    = 0;         // information pages put live
    uint8_t  failures = 0;
    bool     users    = false;     // users.txt put live
    bool     stats    = false;     // callstats.dat put live (1.1.2)
    bool     cfgTried = false;     // the zip had a system.cfg
    bool     cfgLive  = false;     // and the board is running it
    bool     hostChanged = false;  // hostname differs: used from the next restart
    bool     wifiChanged = false;  // network differs: used from the next restart
    // Co-sysop levels the restore left off, because their line named the
    // published password, which is never written (1.0.2): bit 0 co-sysop 1,
    // bit 1 co-sysop 2 (1.1.0). The sysop's own such line needs no bit: the
    // board is then on the published default, which is said already.
    uint8_t  coOff    = 0;
    char     cfgErr[64] = {};      // why it is not running, when it is not
};

class ZipImport {
public:
    // Full: everything the zip may carry, onto the board. Screens: its
    // screens only, into destDir (the card's screens folder), never removing.
    enum class Mode : uint8_t { Full, Screens };

    // Why open() said no, beyond the words in err: the caller has its own
    // sentence for each. needKB and freeKB are set for NoRoom.
    enum class Fail : uint8_t { None, Unreadable, TooBig, NoRoom };

    // open: parse the central directory of zipPath and classify every entry.
    // stageDir: where to unpack before anything goes live, empty for
    // <userdata>/.staging. destDir: Mode::Screens only, the card's screens.
    bool open(const char* zipPath, char* err, size_t errLen, Mode mode = Mode::Full,
              const char* stageDir = nullptr, const char* destDir = nullptr);
    Fail     failed() const { return fail_; }
    uint32_t needKB() const { return needKB_; }
    uint32_t freeKB() const { return freeKB_; }
    bool     roomOnCard() const { return mode_ == Mode::Screens; }

    // step: extract the next accepted entry into staging. False when done.
    // The background runner's since 1.1.2 (backup.cpp's unpack job): an
    // entry is up to 64 KB inflated, CRC'd and written, which held a loop
    // pass for up to a second. recheckRoom is the unpack's first step, with
    // the free space measured there. stepped: entries done, which the loop
    // reads for the card job's dots (one byte, written by the runner).
    bool step();
    bool recheckRoom(char* err, size_t errLen);
    void noteRefusal(const char* why);
    uint8_t stepped() const { return stepped_; }

    const ImportReport& report() const { return rep_; }

    // applyStep: put one staged file live per call, removing a screen the
    // zip leaves out first, and reload system.cfg at the end. False once the
    // whole restore is done and staging is gone; applied() says how it went.
    // One a call, so a restore of sixty-odd files is sixty-odd loop passes
    // and never one long stall with every caller waiting on it.
    bool applyStep();
    const ApplyReport& applied() const { return applied_; }
    bool applyOk() const { return applied_.failures == 0; }

    // applyMessage: "Applied: ..." for curl and the console, as ever
    void applyMessage(char* msg, size_t msgLen) const;

    // discard: delete staging and the uploaded zip
    void discard();

    // close: let go of the uploaded zip and touch nothing on disk. For the
    // backup window, which hands this object's storage to a download
    // afterwards (backup.h) and must not leave a FILE* behind in it.
    void close() { if (zf_) { fclose(zf_); zf_ = nullptr; } }

private:
    struct Item {
        char     name[32];     // normalized: system.cfg or screens/x.ext
        uint8_t  method;       // 0 stored, 8 deflate
        uint32_t crc;
        uint32_t csize;
        uint32_t usize;
        uint32_t localOff;
        bool     ok;           // still accepted after extraction
        bool     live;         // put live by the apply (1.1.0)
    };

    void reject(const char* name, const char* why);
    bool extract(Item& it);
    void countRemovals();
    const char* inspectCfg(const char* staged);
    static const char* nthLive(void* ctx, uint8_t i);
    bool roomCheck(char* err, size_t errLen, bool fresh);
    void stagePath(char* out, size_t n, const char* name) const;
    bool applyItem(Item& it);
    bool removeOneStale();
    void finishApply();

    char         zipPath_[96] = {};
    char         stage_[80]   = {};
    char         dest_[80]    = {};
    FILE*        zf_       = nullptr;
    uint32_t     cdOffset_ = 0;
    Item         items_[BBS_ZIP_MAX_FILES];
    uint8_t      itemCount_ = 0;
    uint8_t      next_      = 0;
    Mode         mode_      = Mode::Full;
    Fail         fail_      = Fail::None;
    volatile uint8_t stepped_ = 0;   // step() calls since open (1.1.2)
    uint32_t     needKB_    = 0;
    uint32_t     freeKB_    = 0;
    uint8_t      applyPhase_ = 0;
    uint8_t      applyIdx_   = 0;
    ImportReport rep_;
    ApplyReport  applied_;
};

} // namespace ziparc

// sdSeededMark: mark screens on the card as the sysop's own in the sd
// plugin's seeded-screens manifest (<sd>/screens/.seeded), so the board
// never refreshes them from a newer stock screen. nth(ctx, i) gives the i-th
// file name ("about.asc") and nullptr past the last. Defined by the plugin,
// asked by the core, the way sdScreensDir is (1.1.0): RESTORE SD SCREENS
// calls it for what it put on the card.
void sdSeededMark(const char* (*nth)(void* ctx, uint8_t i), void* ctx);
