/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/bbs_backup.cpp
 * Module:       Core / backups on the SD card (1.1.0)
 *
 * Purpose:      BACKUP SD, RESTORE SD and the nightly backup, as the sysop
 *                  sees them. The zip work is BackupService's (backup.h),
 *                  which the backup window shares: this draws it.
 *
 *                    BACKUP SD [SCREENS]            the zip, onto the card
 *                    RESTORE SD [SCREENS]           the card's backups, newest first
 *                    RESTORE SD [SCREENS] n|file    check one, ask, put it back
 *
 *                  The work is a step a loop pass (serviceCard), with a dot
 *                  on the sysop's screen for each file, so writing a zip to
 *                  the card or reading one back never holds every caller up
 *                  for the length of it. While it runs the sysop's line is in
 *                  SState::CardJob, where keys are taken only as the answer
 *                  to the question.
 *
 *                  A restore is checked by the very code an upload through
 *                  the backup window is (ziparc::ZipImport), so a zip is good
 *                  or bad the same way from either door; that includes the
 *                  1.0.2 rule that the published password is never written.
 *
 *                  A number can only name a zip the list shows, and a name
 *                  only one that is in the card's backup folder, the file
 *                  manager's argument: nothing typed here can reach a path.
 *
 * Words:        internal/copy-1.1.0-2026-09-23.md section 2, the ids in
 *               the comments (BK-*, RS-*, NB-*).
 * Layout:       internal/tty-ux-1.1.0-2026-09-23.md section 4.
 *
 * Libraries:    none (libc stdio, dirent)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     BACKUP.md "Backups on the SD card", COMMANDS.md
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

#include "bbs.h"
#include "bbs_util.h"
#include "cardnames.h"
#include "clock.h"
#include "screens.h"
#include "sysconfig.h"
#include "users.h"
#include "../platform/platform.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

using namespace bbsu;

namespace {

constexpr uint8_t  kListMax     = 16;     // zips the list shows, newest first
constexpr uint8_t  kLabelCol    = 10;     // the question's label column
constexpr uint8_t  kRestoreDots = 30;     // "Restoring" and at most this many
constexpr uint32_t kNightlyLook = 30000;  // how often the nightly clock is read, ms
constexpr uint32_t kDayMs       = 86400000u;

// Why last night's backup did not happen, for staff at their next arrival.
enum : uint8_t { NB_OK, NB_FULL, NB_NOCARD, NB_NOCLOCK };

struct CardZip {
    char     name[cardbak::kNameMax + 1];
    uint32_t mtime;
    uint32_t size;
};

// line: one line at column 0 in one colour, ended
void line(Session& s, Color c, const char* text) {
    s.term.color(s.tl, c);
    s.term.text(s.tl, text);
    s.term.nl(s.tl);
}

// wrapped: a line that may be longer than the row, word wrapped at column 0
void wrapped(Session& s, Color c, const char* text, uint8_t width) {
    char row[136];
    s.term.color(s.tl, c);
    for (const char* p = wrap(text, row, sizeof(row), width); ; p = wrap(p, row, sizeof(row), width)) {
        s.term.text(s.tl, row);
        s.term.nl(s.tl);
        if (!p || !*p) break;
    }
}

// backupDir: <sd>/backup
void backupDir(char* out, size_t n) {
    snprintf(out, n, "%s/%s", plat::sdBase(), cardbak::kDir);
}

// word: the next space-separated word of *p into out, and *p past it
void word(const char*& p, char* out, size_t n) {
    while (*p == ' ') ++p;
    size_t w = 0;
    while (*p && *p != ' ') {
        if (w + 1 < n) out[w++] = *p;
        ++p;
    }
    out[w] = '\0';
    while (*p == ' ') ++p;
}

bool digits(const char* s) {
    if (!*s) return false;
    for (; *s; ++s) if (*s < '0' || *s > '9') return false;
    return true;
}

// newer: does a sort before b in the list? Newest first, then by name so two
// zips from the same second always come out in one order.
bool newer(const CardZip& a, const CardZip& b) {
    if (a.mtime != b.mtime) return a.mtime > b.mtime;
    return strcmp(a.name, b.name) > 0;
}

// cardList: the newest kListMax zips in the backup folder, sorted, and how
// many there are in all. The list and a number given to RESTORE SD are both
// this, so a number means what the list showed.
uint8_t cardList(CardZip* out, uint16_t& total) {
    total = 0;
    uint8_t n = 0;
    char dir[96];
    backupDir(dir, sizeof(dir));
    DIR* d = opendir(dir);
    if (!d) return 0;
    for (struct dirent* e = readdir(d); e; e = readdir(d)) {
        if (!cardbak::listable(e->d_name)) continue;
        char path[140];
        struct stat st;
        snprintf(path, sizeof(path), "%.90s/%.40s", dir, e->d_name);
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        ++total;
        CardZip z;
        snprintf(z.name, sizeof(z.name), "%.27s", e->d_name);       // listable: 27 at most
        z.mtime = static_cast<uint32_t>(st.st_mtime);
        z.size  = static_cast<uint32_t>(st.st_size);
        // Insert in order, dropping the oldest once the list is full.
        uint8_t at = n;
        while (at > 0 && newer(z, out[at - 1])) --at;
        if (at >= kListMax) continue;
        uint8_t last = n < kListMax ? n : static_cast<uint8_t>(kListMax - 1);
        for (uint8_t k = last; k > at; --k) out[k] = out[k - 1];
        out[at] = z;
        if (n < kListMax) ++n;
    }
    closedir(d);
    return n;
}

// sizeText: "31 KB", in the board's KB of 1,024, rounded up
void sizeText(uint32_t bytes, char* out, size_t n) {
    snprintf(out, n, "%u KB", static_cast<unsigned>((bytes + 1023) / 1024));
}

} // namespace

// ===========================================================================
// BACKUP SD
// ===========================================================================

void Bbs::cmdBackup(Session& s, const char* arg, uint32_t now) {
    (void)now;
    const char* p = arg;
    char first[12], second[12], extra[8];
    word(p, first, sizeof(first));
    word(p, second, sizeof(second));
    word(p, extra, sizeof(extra));
    bool screens = ieq(second, "SCREENS");
    if (!ieq(first, "SD") || (second[0] && !screens) || extra[0]) {
        line(s, Color::Grey, "BACKUP SD saves a zip on the card.");       // BK-bare-1
        line(s, Color::Grey, "For the backup window, press BOOT.");       // BK-bare-2
        prompt(s);
        return;
    }
    if (!plat::sdBase()[0]) {
        line(s, Color::LightRed, "No card mounted. Try SD MOUNT first.");  // BK-nocard
        prompt(s);
        return;
    }
    // The name carries the date and time, and a board that has not heard
    // from NTP does not know them.
    if (!clk::valid()) {
        line(s, Color::LightRed, "No time yet: waiting for NTP. Try soon."); // BK-noclock
        prompt(s);
        return;
    }
    if (backup_.busy()) {
        line(s, Color::LightRed, "A backup or restore is already running."); // BK-busy
        prompt(s);
        return;
    }

    char stamp[20], name[32], dir[96], path[140];
    clk::fmt(stamp, sizeof(stamp), "%Y%m%d-%H%M");
    cardbak::makeName(screens ? cardbak::Kind::Screens : cardbak::Kind::Full, stamp, name, sizeof(name));
    backupDir(dir, sizeof(dir));
    mkdir(dir, 0755);                                         // there already is fine
    snprintf(path, sizeof(path), "%.96s/%s", dir, name);
    struct stat st;
    // Two in one minute would share a name. The first is kept, never
    // overwritten by the second.
    if (!name[0] || stat(path, &st) == 0) {
        line(s, Color::LightRed, "There is one from this minute already."); // BK-exists
        prompt(s);
        return;
    }

    uint32_t needKB = 0, freeKB = 0;
    BackupService::Start r = backup_.cardBackup(dir, name, screens, needKB, freeKB);
    if (r != BackupService::Start::Ok) {
        char buf[64];
        if (r == BackupService::Start::Busy)
            snprintf(buf, sizeof(buf), "A backup or restore is already running.");  // BK-busy
        else if (r == BackupService::Start::Full)
            snprintf(buf, sizeof(buf), "Card full: %u KB needed, %u KB free.",       // BK-full
                     static_cast<unsigned>(needKB), static_cast<unsigned>(freeKB));
        else
            snprintf(buf, sizeof(buf), "Card write failed. Nothing was saved.");     // BK-failed
        line(s, Color::LightRed, buf);
        prompt(s);
        return;
    }

    char buf[48];
    snprintf(buf, sizeof(buf), "Writing %s", name);                    // BK-writing
    line(s, Color::Grey, buf);
    // No "the board pauses" line (BK-pause): the zip goes out a step a pass
    // and nothing waits on it.
    cardDots_    = 0;
    cardNightly_ = false;
    cardScreens_ = screens;
    snprintf(cardName_, sizeof(cardName_), "%.27s", name);
    s.ed = LineEditor();
    s.st = SState::CardJob;
    plat::log("backup: %s writing %s to the card", s.user, name);
}

// ===========================================================================
// RESTORE SD
// ===========================================================================

void Bbs::cmdRestore(Session& s, const char* arg, uint32_t now) {
    (void)now;
    const char* p = arg;
    char first[12], second[32], target[32];
    word(p, first, sizeof(first));
    word(p, second, sizeof(second));
    bool screens = ieq(second, "SCREENS");
    if (screens) word(p, target, sizeof(target));
    else         snprintf(target, sizeof(target), "%s", second);
    if (!ieq(first, "SD") || *p) {
        line(s, Color::Grey, "RESTORE SD lists the card's backups.");
        line(s, Color::Grey, "For the backup window, press BOOT.");   // BK-bare-2
        prompt(s);
        return;
    }
    if (!plat::sdBase()[0]) {
        line(s, Color::LightRed, "No card mounted. Try SD MOUNT first.");  // BK-nocard
        prompt(s);
        return;
    }

    CardZip zips[kListMax];                   // 576 bytes, for the length of one command
    uint16_t total = 0;
    uint8_t shown = cardList(zips, total);

    if (!target[0]) {                         // the list
        if (!shown) {
            line(s, Color::Grey, "No backups on the card yet.");       // RS-list-empty
            line(s, Color::Grey, "BACKUP SD makes one.");              // RS-list-empty-2
            prompt(s);
            return;
        }
        char right[16], row[64], kb[12];
        if (total == 1) snprintf(right, sizeof(right), "1 file");                    // RS-list-count-1
        else            snprintf(right, sizeof(right), "%u files", static_cast<unsigned>(total));
        rowTitle(s, "Backups on the card", right);                                   // RS-list-title
        for (uint8_t i = 0; i < shown; ++i) {
            uint8_t col = 0;
            snprintf(row, sizeof(row), "%2u  ", static_cast<unsigned>(i + 1));
            rowSeg(s, Color::Yellow, row, col);
            snprintf(row, sizeof(row), "%-27.27s ", zips[i].name);
            rowSeg(s, Color::White, row, col);
            sizeText(zips[i].size, kb, sizeof(kb));
            snprintf(row, sizeof(row), "%6s", kb);
            rowSeg(s, Color::Grey, row, col);
            rowEnd(s, col);
        }
        rowRule(s);
        line(s, Color::Grey, screens ? "RESTORE SD SCREENS n restores one."   // RS-list-foot-s
                                     : "RESTORE SD n restores one.");         // RS-list-foot
        prompt(s);
        return;
    }

    // Which zip. A number is a row of the list above; a name has to be a zip
    // in the backup folder, matched as the card's own filesystem would
    // (FAT ignores case), and it is the folder's spelling that is used.
    const CardZip* pick = nullptr;
    if (digits(target)) {
        long n = strtol(target, nullptr, 10);
        if (n >= 1 && n <= shown) pick = &zips[n - 1];
        if (!pick) {
            line(s, Color::LightRed, "No backup with that number.");   // RS-bad-n
            prompt(s);
            return;
        }
    }
    CardZip byName;
    if (!pick) {
        char dir[96];
        backupDir(dir, sizeof(dir));
        DIR* d = cardbak::listable(target) ? opendir(dir) : nullptr;
        if (d) {
            for (struct dirent* e = readdir(d); e; e = readdir(d)) {
                if (strcasecmp(e->d_name, target) || !cardbak::listable(e->d_name)) continue;
                char path[140];
                struct stat st;
                snprintf(path, sizeof(path), "%.90s/%.40s", dir, e->d_name);
                if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
                snprintf(byName.name, sizeof(byName.name), "%.27s", e->d_name);
                byName.size  = static_cast<uint32_t>(st.st_size);
                byName.mtime = static_cast<uint32_t>(st.st_mtime);
                pick = &byName;
                break;
            }
            closedir(d);
        }
        if (!pick) {
            line(s, Color::LightRed, "No backup by that name on the card."); // RS-bad-file
            prompt(s);
            return;
        }
    }

    if (backup_.busy()) {
        line(s, Color::LightRed, "A backup or restore is already running."); // BK-busy
        prompt(s);
        return;
    }
    // Checked before the importer opens it, which would call a zip that has
    // simply grown past the limit "not a zip file". The size up and the
    // limit down, so a zip a byte over reads as over.
    if (pick->size > BBS_ZIP_MAX_BYTES) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Too big: %u KB. The limit is %u KB.",       // RS-toobig
                 static_cast<unsigned>((pick->size + 1023) / 1024),
                 static_cast<unsigned>(BBS_ZIP_MAX_BYTES / 1024));
        line(s, Color::LightRed, buf);
        prompt(s);
        return;
    }

    char path[140], dest[96], buf[64];
    char dir[96];
    backupDir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%.96s/%s", dir, pick->name);
    snprintf(dest, sizeof(dest), "%s/%s", plat::sdBase(), BBS_SD_SCREEN_DIR);
    snprintf(cardName_, sizeof(cardName_), "%s", pick->name);
    cardZipBytes_ = pick->size;
    cardScreens_  = screens;
    cardNightly_  = false;

    snprintf(buf, sizeof(buf), "Checking %s", cardName_);                   // RS-checking
    line(s, Color::Grey, buf);
    char err[96];
    if (!backup_.cardRestore(path, screens, dest, err, sizeof(err))) {
        char msg[128];
        switch (backup_.cardFail()) {
            case ziparc::ZipImport::Fail::TooBig:
                snprintf(msg, sizeof(msg), "Too big: %u KB. The limit is %u KB.",
                         static_cast<unsigned>((pick->size + 1023) / 1024),
                         static_cast<unsigned>(BBS_ZIP_MAX_BYTES / 1024));
                break;
            case ziparc::ZipImport::Fail::NoRoom: {
                // Unpacked where it is checked: the card for SCREENS, the
                // board's own flash for the rest. BK-full's words for the
                // card; the same shape for the board.
                snprintf(msg, sizeof(msg), "%s full: %u KB needed, %u KB free.",
                         screens ? "Card" : "Board", static_cast<unsigned>(backup_.cardNeedKB()),
                         static_cast<unsigned>(backup_.cardFreeKB()));
                break;
            }
            default:
                snprintf(msg, sizeof(msg), "Cannot read it: %.80s.", err);     // RS-unreadable
                break;
        }
        wrapped(s, Color::LightRed, msg, rowWidth(s));
        prompt(s);
        return;
    }
    cardDots_ = 0;
    s.ed = LineEditor();
    s.st = SState::CardJob;
    plat::log("backup: %s checking %s from the card%s", s.user, cardName_, screens ? " (screens)" : "");
}

// ---------------------------------------------------------------------------
// cardRow: "Replaces  settings, accounts, screens". The label is ten
// columns and a value too long for the rest of the row wraps under itself,
// not under the label.
// ---------------------------------------------------------------------------
void Bbs::cardRow(Session& s, const char* label, const char* value, Color c) {
    uint8_t width = rowWidth(s);
    uint8_t room  = width > kLabelCol + 8 ? static_cast<uint8_t>(width - kLabelCol) : 8;
    char lab[16], part[136];
    snprintf(lab, sizeof(lab), "%-10.10s", label);
    const char* p = wrap(value, part, sizeof(part), room);
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, lab);
    s.term.color(s.tl, c);
    s.term.text(s.tl, part);
    s.term.nl(s.tl);
    while (p && *p) {
        p = wrap(p, part, sizeof(part), room);
        if (!part[0]) break;
        for (uint8_t i = 0; i < kLabelCol; ++i) s.term.ch(s.tl, ' ');
        s.term.text(s.tl, part);
        s.term.nl(s.tl);
    }
}

// ---------------------------------------------------------------------------
// cardAsk: the zip is checked; say what it would do and ask.
//
// A full restore always shows Replaces, Accounts, Removes and Staff (the
// copy's hand-back): Removes is what tells it apart from RESTORE SD SCREENS
// when both are offered the same file, Accounts is the number that matters
// most, and Staff is what a sysop who has just reset the board needs to know
// before answering, not after.
// ---------------------------------------------------------------------------
void Bbs::cardAsk(Session& s, uint32_t now) {
    const ziparc::ImportReport& r = backup_.cardReport();
    char title[48], v[96], kb[12];
    snprintf(title, sizeof(title), "RESTORE %s", cardName_);                // RS-title / RS-s-title
    rowTitle(s, title);
    sizeText(cardZipBytes_, kb, sizeof(kb));

    if (cardScreens_) {
        snprintf(v, sizeof(v), "%u screen%s, %s", r.screens, r.screens == 1 ? "" : "s", kb); // RS-s-v-inzip
        cardRow(s, "In zip", v, Color::White);
        snprintf(v, sizeof(v), "%u screen%s on the card", r.replaced, r.replaced == 1 ? "" : "s"); // RS-s-v-replaces
        cardRow(s, "Replaces", v, Color::White);
        unsigned adds = r.screens > r.replaced ? r.screens - r.replaced : 0;
        if (adds) {
            snprintf(v, sizeof(v), "%u screen%s to the card", adds, adds == 1 ? "" : "s");  // RS-s-v-adds
            cardRow(s, "Adds", v, Color::White);
        }
        if (r.rejected) {
            snprintf(v, sizeof(v), "%u: %.88s", r.rejected, r.firstReject);                    // RS-v-rejected
            cardRow(s, "Rejected", v, Color::LightRed);
        }
        line(s, Color::Grey, "Stock screens in flash are not touched.");                    // RS-s-flash
        if (!sdScreensDir()) line(s, Color::Yellow, "Card screens are off in CONFIG sd.");  // RS-s-off
    } else {
        unsigned files = static_cast<unsigned>(r.accepted) + r.rejected;
        snprintf(v, sizeof(v), "%u file%s, %s", files, files == 1 ? "" : "s", kb);          // RS-v-inzip
        cardRow(s, "In zip", v, Color::White);
        // Only the parts the zip has, joined with commas.
        size_t at = 0;
        v[0] = '\0';
        auto part = [&](bool has, const char* what) {
            if (!has) return;
            int w = snprintf(v + at, sizeof(v) - at, "%s%s", at ? ", " : "", what);
            if (w > 0 && at + static_cast<size_t>(w) < sizeof(v)) at += static_cast<size_t>(w);
        };
        part(r.hasCfg, "settings");
        part(r.hasUsers, "accounts");
        part(r.hasScreens, "screens");
        part(r.pages > 0, "info pages");
        cardRow(s, "Replaces", v, Color::White);                                              // RS-v-replaces
        unsigned here = users::count();
        if (r.hasUsers) snprintf(v, sizeof(v), "%u, replacing all %u here", r.accounts, here); // RS-v-accounts
        else            snprintf(v, sizeof(v), "none in it; the %u here stay", here);
        cardRow(s, "Accounts", v, Color::White);
        if (r.hasScreens) snprintf(v, sizeof(v), "%u screen%s not in the zip", r.removed,     // RS-v-removes
                                   r.removed == 1 ? "" : "s");
        else              snprintf(v, sizeof(v), "none: no screens in it");
        cardRow(s, "Removes", v, Color::White);
        if (r.rejected) {
            snprintf(v, sizeof(v), "%u: %.88s", r.rejected, r.firstReject);                        // RS-v-rejected
            cardRow(s, "Rejected", v, Color::LightRed);
        }
        if (r.hasCfg && r.wifiDiffers) cardRow(s, "Wi-Fi", "the zip's, from next restart", Color::White); // RS-v-wifi
        if (r.staffChanged) snprintf(v, sizeof(v), "%u changed by the zip", r.staffChanged);
        else                snprintf(v, sizeof(v), "passwords stay as they are");                // RS-v-staff
        cardRow(s, "Staff", v, r.staffChanged ? Color::Yellow : Color::White);
        line(s, Color::Yellow, "Changes made since then will be lost.");                        // RS-lost
    }
    s.term.color(s.tl, Color::Yellow);
    s.term.text(s.tl, "Restore now? (y/N) ");                                                   // RS-ask
    s.term.color(s.tl, Color::White);
    cardAskBy_ = now + BBS_RESTORE_ASK_MS;
    if (!cardAskBy_) cardAskBy_ = 1;
}

// ---------------------------------------------------------------------------
// cardKey: a key while a card job has the sysop's line. Only the question
// takes one; anything typed while a zip is being written or checked is let
// go, as it is while a screen is being drawn.
// ---------------------------------------------------------------------------
void Bbs::cardKey(Session& s, int k) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    if (!tl.empty()) tl.skipDelays();
    // A restore waiting for the board to go quiet, from either door: F puts
    // it live now, with a warning to whoever is still on; N gives it up.
    if (backup_.holding()) {
        if (k == 'f' || k == 'F') {
            t.ch(tl, 'F');
            t.nl(tl);
            holdGo(s, true);
        } else if (k == 'n' || k == 'N' || k == KEY_ESC || k == KEY_BREAK) {
            t.ch(tl, 'N');
            t.nl(tl);
            backup_.holdGiveUp("the sysop stopped waiting");
            holdUntil_ = 0;
            line(s, Color::Grey, "Not restored.");                              // RS-no
            prompt(s);
        }
        return;
    }
    if (backup_.job() != BackupService::Job::Ask) return;
    if (k == 'y' || k == 'Y') {
        t.ch(tl, 'Y');
        t.nl(tl);
        backup_.cardAnswer(true);                     // held: holdBegin decides when
        holdBegin(s, plat::millis());
        return;
    }
    if (k == 'n' || k == 'N' || k == KEY_ENTER || k == KEY_ESC || k == KEY_BREAK) {
        t.ch(tl, 'N');
        t.nl(tl);
        backup_.cardAnswer(false);
        line(s, Color::Grey, "Not restored.");                                // RS-no
        prompt(s);
    }
}

// ===========================================================================
// A restore waits for the board to go quiet (1.1.0)
//
// Rob, watching a live board through a restore: "it hangs hard ... it
// should be restored only when the site is not busy". A file a pass took
// the single stall away; this takes away callers being on a board whose
// accounts, settings and screens are changing under them. After the sysop's
// Y, at either door, nothing is put live while anybody else is on. It waits
// for them to go, says how many it is waiting for, and gives up after the
// backup window's own length (backup_window_minutes) rather than for ever.
// F puts it live at once, and whoever is still on is warned first, the way
// SHUTDOWN warns them. While it waits and while it goes in, a new caller
// gets the busy line (acceptAll).
// ===========================================================================

namespace {

// holdLimit: how long a restore waits for callers to go. The backup
// window's length, which is the time the sysop already chose for a restore
// to be pending. A test cannot wait five minutes, so on the host it may be
// named in milliseconds. Never on a board.
uint32_t holdLimit() {
#ifdef BBS_HOST
    if (const char* ms = getenv("BBS_RESTORE_HOLD_MS")) return static_cast<uint32_t>(atol(ms));
#endif
    return static_cast<uint32_t>(syscfg::get().backupMinutes) * 60000u;
}

// holdLine: what the sysop is told while it waits
void holdLine(Session& s, uint8_t n, uint8_t width) {
    char buf[96];
    snprintf(buf, sizeof(buf), "Waiting for %u caller%s to leave. F applies it now, N gives up.",  // RS-hold
             static_cast<unsigned>(n), n == 1 ? "" : "s");
    wrapped(s, Color::Yellow, buf, width);
}

} // namespace

uint8_t Bbs::othersOn() const {
    uint8_t n = 0;
    for (const Session& o : nodes_)
        if (o.st != SState::Free && o.st != SState::Closing) ++n;
    // The busy line is on its way out unless somebody logged in on it.
    if (busy_.st != SState::Free && busy_.st != SState::Closing && busy_.loggedIn) ++n;
    return n;
}

// windowAccepted: the sysop's Y to an upload through the backup window
void Bbs::windowAccepted(Session& s, uint32_t now) {
    approvalShown_ = false;
    s.st = SState::Shell;                    // before decide: notes queue for the prompt
    backup_.decide(true, "");
    holdBegin(s, now);
}

// holdBegin: after a Y, at either door. A quiet board goes straight on.
void Bbs::holdBegin(Session& s, uint32_t now) {
    uint8_t n = othersOn();
    if (!n) { holdGo(s, false); return; }
    holdUntil_ = now + holdLimit();
    if (!holdUntil_) holdUntil_ = 1;
    holdSaid_ = n;
    backup_.holdTell(n);                     // the window's client hears the same
    s.ed = LineEditor();
    s.st = SState::CardJob;                  // F and N are the only keys now
    holdLine(s, n, rowWidth(s));
    plat::log("backup: restore waiting for %u caller%s to leave", static_cast<unsigned>(n),
              n == 1 ? "" : "s");
}

// holdGo: put it live. Forced (F) with callers on: they are warned first,
// through the bus the way SHUTDOWN's warnings go, so it reaches them
// whatever they are doing.
void Bbs::holdGo(Session& s, bool forced) {
    bool card = backup_.job() == BackupService::Job::Hold;
    if (forced) {
        uint8_t told = 0;
        for (Session* o : all_) {
            if (o == &sysop_ || o->st == SState::Free) continue;
            if (o->role == Role::Busy && !o->loggedIn) continue;
            post(*o, BusKind::Broadcast, nullptr, "*** The sysop is restoring a backup now.");  // RS-now
            ++told;
        }
        plat::log("backup: restore put live by the sysop with %u caller%s on", static_cast<unsigned>(told),
                  told == 1 ? "" : "s");
    }
    holdUntil_ = 0;
    backup_.holdRelease();
    if (card) {
        // Anybody part way through a screen from the card lets go of it
        // before it is replaced, the way SD UNMOUNT does it.
        if (cardScreens_) closeCardScreens("Screen ended: the sysop is changing screens.");
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, "Restoring");                                    // RS-restoring
        cardDots_ = 0;
        s.ed = LineEditor();
        s.st = SState::CardJob;
        plat::log("backup: %s restoring %s from the card", s.user, cardName_);
    } else {
        // The window's: it goes in from here (applyTick) and the notes say
        // how it went. The sysop has their prompt back meanwhile.
        prompt(s);
    }
}

// serviceHold: a pass of the wait. Once nobody else is on, it goes; after
// the limit, it is given up, as a Y that never came would be.
void Bbs::serviceHold(uint32_t now) {
    if (!backup_.holding()) return;
    Session& s = sysop_;
    bool sysopOn = s.st != SState::Free && s.loggedIn && s.fd >= 0;
    if (!sysopOn) {
        backup_.holdGiveUp("the sysop left");
        holdUntil_ = 0;
        plat::log("backup: held restore given up: the sysop left");
        return;
    }
    uint8_t n = othersOn();
    if (!n) { holdGo(s, false); return; }
    if (holdUntil_ && static_cast<int32_t>(now - holdUntil_) >= 0) {
        backup_.holdGiveUp("callers stayed on");
        holdUntil_ = 0;
        if (s.st == SState::CardJob) {
            line(s, Color::LightRed, "Not restored: callers stayed on.");    // RS-hold-gone
            prompt(s);
        }
        plat::log("backup: held restore given up: %u caller%s stayed on", static_cast<unsigned>(n),
                  n == 1 ? "" : "s");
        return;
    }
    if (n != holdSaid_ && s.st == SState::CardJob && s.tl.freeBytes() > 256) {
        holdSaid_ = n;
        holdLine(s, n, rowWidth(s));
    }
}

// ---------------------------------------------------------------------------
// cardDotsOut: the progress, a dot a file. Writing and checking put them on
// a line of their own, capped at the row, since "Writing" and a 27
// character name leave no room; restoring puts them after the word, capped
// at kRestoreDots so the line stays inside 40 columns (the copy).
// ---------------------------------------------------------------------------
void Bbs::cardDotsOut(Session& s, uint8_t n, uint8_t cap) {
    s.term.color(s.tl, Color::Grey);
    for (uint8_t i = 0; i < n && cardDots_ < cap; ++i, ++cardDots_) s.term.ch(s.tl, '.');
}

// ---------------------------------------------------------------------------
// cardDone: the job moved on. What the sysop is told, or the log for a
// nightly one with nobody watching.
// ---------------------------------------------------------------------------
void Bbs::cardDone(Session& s, BackupService::Job was, bool watching, uint32_t now) {
    char buf[80];
    if (was == BackupService::Job::Write) {
        if (cardNightly_) { nightlyDone(); return; }
        if (!watching) return;
        s.term.nl(s.tl);
        if (backup_.cardOk()) {
            char kb[12];
            sizeText(backup_.cardBytes(), kb, sizeof(kb));
            unsigned n = backup_.cardFiles();
            if (cardScreens_) snprintf(buf, sizeof(buf), "Saved: %u screen%s, %s.", n, n == 1 ? "" : "s", kb); // BK-saved-s
            else              snprintf(buf, sizeof(buf), "Saved: %u file%s, %s.", n, n == 1 ? "" : "s", kb);   // BK-saved
            line(s, Color::LightGreen, buf);
            // A full backup carries system.cfg and with it the network's
            // passphrase as typed (0.22.1); a screens one has neither.
            if (!cardScreens_) line(s, Color::Yellow, "It holds your Wi-Fi password as typed."); // BK-wifi
            plat::log("backup: %s saved %s, %u bytes", s.user, cardName_,
                      static_cast<unsigned>(backup_.cardBytes()));
        } else {
            // The half-written file went with it (BackupService::finishWrite).
            line(s, Color::LightRed, "Card write failed. Nothing was saved."); // BK-failed
            plat::log("backup: %s could not write %s", s.user, cardName_);
        }
        prompt(s);
        return;
    }

    if (was == BackupService::Job::Check) {
        if (!watching) return;
        s.term.nl(s.tl);
        if (backup_.job() == BackupService::Job::Ask) { cardAsk(s, now); return; }
        const ziparc::ImportReport& r = backup_.cardReport();
        line(s, Color::LightRed, "Nothing in it can be used.");              // RS-nothing
        snprintf(buf, sizeof(buf), "First problem: %.60s", r.firstReject);    // RS-nothing-why
        wrapped(s, Color::Grey, buf, rowWidth(s));
        prompt(s);
        return;
    }

    if (was == BackupService::Job::Apply) {
        if (!watching) return;
        s.term.nl(s.tl);
        const ziparc::ApplyReport& a = backup_.cardApplied();
        if (cardScreens_) {
            snprintf(buf, sizeof(buf), "Restored: %u screen%s on the card.", a.screens,   // RS-s-done
                     a.screens == 1 ? "" : "s");
            line(s, a.failures ? Color::Yellow : Color::LightGreen, buf);
            if (a.failures) line(s, Color::Yellow, "Restored, but not every file went back."); // RS-partial
            line(s, Color::Grey, "Deleting them from the card undoes it.");              // RS-s-undo
        } else {
            if (a.cfgTried && !a.cfgLive) {
                snprintf(buf, sizeof(buf), "Settings did not load: %.50s.", a.cfgErr);   // RS-cfg-bad
                wrapped(s, Color::LightRed, buf, rowWidth(s));
                line(s, Color::Yellow, "The old ones are still running.");               // RS-cfg-old
            } else if (a.failures) {
                line(s, Color::Yellow, "Restored, but not every file went back.");       // RS-partial
            } else {
                line(s, Color::LightGreen, "Restored and live.");                        // RS-done
            }
            if (a.wifiChanged) line(s, Color::Grey, "Wi-Fi is used from the next restart.");     // RS-done-wifi
            if (a.hostChanged) line(s, Color::Grey, "Hostname is used from the next restart.");  // RS-done-host
            if (syscfg::get().sysopDefault)
                line(s, Color::Yellow, "Now set a sysop password: CONFIG staff.");               // RS-done-sysop
            // A co-sysop line naming the published password was left out,
            // so that level is off (1.1.0). Both lines 39 columns or less.
            if (a.coOff) {
                line(s, Color::Yellow, a.coOff == 3 ? "Co-sysops 1, 2 off: published password."   // RS-done-co2
                                     : a.coOff == 1 ? "Co-sysop 1 off: the published password."   // RS-done-co
                                                    : "Co-sysop 2 off: the published password.");
                line(s, Color::Grey, "Set theirs in CONFIG staff.");                               // RS-done-co-2
            }
        }
        prompt(s);
    }
}

// ---------------------------------------------------------------------------
// dropCardJob: the card is about to go (SD UNMOUNT, or the sd plugin moving
// to new pins). A FILE* left open across an unmount is a descriptor into a
// torn-down filesystem that the next mount can hand to somebody else's file,
// the ScreenPlayer lesson from Block B, so the job lets go first.
// ---------------------------------------------------------------------------
void Bbs::dropCardJob() {
    BackupService::Job j = backup_.job();
    if (j == BackupService::Job::None || j == BackupService::Job::Apply) return;
    backup_.cardDrop();
    if (cardNightly_) {
        cardNightly_ = false;
        nightlyFail_ = NB_NOCARD;
        plat::log("backup: nightly skipped: the card was unmounted");  // NB-log-skip
    } else {
        plat::log("backup: card job dropped, the card was unmounted");
    }
}

// ---------------------------------------------------------------------------
// serviceCard: a step of the card job a pass, and what it means on screen.
// ---------------------------------------------------------------------------
void Bbs::serviceCard(uint32_t now) {
    // A restore that put system.cfg or a page live, from either door: the
    // plugins start again on it, as a CONFIG save makes them.
    if (backup_.takeRestart()) restartPlugins();

    // A restore the sysop said Y to, waiting for the board to go quiet.
    serviceHold(now);

    // A restore putting screens live, from either door: anybody reading one
    // of the screens it replaces lets go of it first, every pass, so a
    // caller who opens a screen part way through (a new caller's welcome)
    // is let go of too. esp_littlefs will not replace or remove a file
    // somebody has open (EBUSY), and a screen the restore could not put in
    // place is a restore that ends "with errors". The window's next file
    // goes in straight after, in this same pass, with nothing open.
    bool toCard = false;
    if (backup_.applyingScreens(toCard))
        endScreens(toCard, toCard ? "Screen ended: the sysop is changing screens."
                                  : "Screen ended: the sysop is restoring a backup.");
    backup_.applyTick();

    BackupService::Job was = backup_.job();
    if (was == BackupService::Job::None) {
        nightlyTick(now);
        return;
    }
    Session& s = sysop_;
    bool sysopOn  = s.st == SState::CardJob && s.loggedIn && s.fd >= 0;
    bool watching = !cardNightly_ && sysopOn;

    // The sysop who asked has gone: a half-made backup and a half-checked
    // restore go with them. A restore being put live finishes, because
    // stopping it half way leaves a board with part of two backups.
    if (!cardNightly_ && !sysopOn && was != BackupService::Job::Apply) {
        backup_.cardDrop();
        plat::log("backup: card job dropped, the sysop left");
        return;
    }
    if (was == BackupService::Job::Ask) {
        if (static_cast<int32_t>(now - cardAskBy_) >= 0) {       // 60 s: the same as N
            backup_.cardAnswer(false);
            s.term.nl(s.tl);
            line(s, Color::Grey, "Not restored.");                // RS-no
            prompt(s);
        }
        return;
    }
    // Not faster than the sysop's terminal takes the dots.
    if (watching && s.tl.freeBytes() < 512) return;

    uint8_t dots = backup_.cardStep();
    if (watching && dots)
        cardDotsOut(s, dots, was == BackupService::Job::Apply ? kRestoreDots : rowWidth(s));
    if (backup_.job() != was) cardDone(s, was, watching, now);
}

// ===========================================================================
// The nightly backup
// ===========================================================================

// ---------------------------------------------------------------------------
// nightlyTick: once a day at BBS_NIGHTLY_HOUR local, when the sd plugin's
// nightly setting is on. The hour is the whole window: a board that was off
// or busy for all of it skips that night rather than making one at noon, in
// the middle of whoever is calling. A board restarted inside the hour does
// not make a second one, because the file is already there.
// ---------------------------------------------------------------------------
void Bbs::nightlyTick(uint32_t now) {
    if (!sdNightly()) { nightlyNoClock_ = 0; return; }
    if (!clk::valid()) {
        // No clock, no night. Said once for every day of uptime without one.
        if (!nightlyNoClock_) nightlyNoClock_ = now ? now : 1;
        else if (now - nightlyNoClock_ >= kDayMs) {
            nightlyNoClock_ = now ? now : 1;
            nightlyFail_    = NB_NOCLOCK;
            plat::log("backup: nightly skipped: no clock");      // NB-log-skip
        }
        return;
    }
    nightlyNoClock_ = 0;
    if (nightlyLookAt_ && now - nightlyLookAt_ < kNightlyLook) return;
    nightlyLookAt_ = now ? now : 1;

    int wanted = BBS_NIGHTLY_HOUR;
#ifdef BBS_HOST
    // A test cannot wait for three in the morning, so on the host it may
    // name the hour. Never on a board.
    if (const char* h = getenv("BBS_NIGHTLY_HOUR")) wanted = atoi(h);
#endif
    char hour[4];
    clk::fmt(hour, sizeof(hour), "%H");
    uint32_t today = clk::dayKey(now);
    if (atoi(hour) != wanted || nightlyDay_ == today) return;
    if (backup_.busy()) return;                   // the window or the sysop: try again in 30 s
    nightlyDay_ = today;

    if (!plat::sdBase()[0]) {
        nightlyFail_ = NB_NOCARD;
        plat::log("backup: nightly skipped: no card");          // NB-log-skip
        return;
    }
    char stamp[20], name[32], dir[96], path[140];
    clk::fmt(stamp, sizeof(stamp), "%Y%m%d-%H%M");
    if (!cardbak::makeName(cardbak::Kind::Nightly, stamp, name, sizeof(name))) return;
    backupDir(dir, sizeof(dir));
    mkdir(dir, 0755);
    snprintf(path, sizeof(path), "%.96s/%s", dir, name);
    struct stat st;
    if (stat(path, &st) == 0) return;             // made already tonight, before a restart

    uint32_t needKB = 0, freeKB = 0;
    BackupService::Start r = backup_.cardBackup(dir, name, false, needKB, freeKB);
    if (r == BackupService::Start::Ok) {
        cardNightly_ = true;
        cardScreens_ = false;
        snprintf(cardName_, sizeof(cardName_), "%.27s", name);
        return;
    }
    nightlyFail_ = NB_FULL;
    plat::log("backup: nightly skipped: %s", r == BackupService::Start::Full ? "card full"
                                                                          : "the card would not take it");
}

// nightlyDone: the nightly zip is written, or is not. The oldest go until
// seven are left, and only ever nightly-*: a backup the sysop made by hand
// is theirs, however old (cardnames.h).
void Bbs::nightlyDone() {
    cardNightly_ = false;
    if (!backup_.cardOk()) {
        nightlyFail_ = NB_FULL;
        plat::log("backup: nightly skipped: the card write failed, card full?");   // NB-log-skip
        return;
    }
    nightlyFail_ = NB_OK;
    char dir[96];
    backupDir(dir, sizeof(dir));
    uint16_t left = 0;
    for (uint8_t guard = 0; guard < 32; ++guard) {
        cardbak::NightlyScan scan;
        DIR* d = opendir(dir);
        if (!d) break;
        for (struct dirent* e = readdir(d); e; e = readdir(d)) scan.feed(e->d_name);
        closedir(d);
        left = scan.count;
        const char* victim = scan.prune();
        if (!victim) break;
        char path[140];
        snprintf(path, sizeof(path), "%.96s/%s", dir, victim);
        if (remove(path) != 0) break;
        plat::log("backup: removed the oldest nightly, %s", victim);   // NB-log-prune
    }
    plat::log("backup: nightly %s, %u KB, %u on the card", cardName_,     // NB-log-ok
              static_cast<unsigned>((backup_.cardBytes() + 1023) / 1024), static_cast<unsigned>(left));
}

// ---------------------------------------------------------------------------
// tidyCardBackups: a zip is written as <name>.tmp and renamed once it is
// whole (BackupService::finishWrite), so the power going or the card coming
// out half way leaves the .tmp behind, where nothing ever removed it. It
// cannot be restored and takes card space, and since 1.1.0 it would sit in
// the Backups file area as well. Called by the sd plugin on a mount it has
// just made, so no backup can be writing to this card. Only names that are
// provably ours (cardbak::partial) go: anything else is the sysop's.
// ---------------------------------------------------------------------------
void tidyCardBackups() {
    if (!plat::sdBase()[0]) return;
    char dir[96];
    backupDir(dir, sizeof(dir));
    for (uint8_t guard = 0; guard < 32; ++guard) {    // one at a time: never remove mid-walk
        char victim[cardbak::kNameMax + 8] = "";
        DIR* d = opendir(dir);
        if (!d) return;
        for (struct dirent* e = readdir(d); e; e = readdir(d)) {
            if (!cardbak::partial(e->d_name)) continue;
            snprintf(victim, sizeof(victim), "%.34s", e->d_name);    // partial: 31 at most
            break;
        }
        closedir(d);
        if (!victim[0]) return;
        char path[140];
        snprintf(path, sizeof(path), "%.96s/%s", dir, victim);
        if (remove(path) != 0) return;
        plat::log("backup: removed %s, a backup the card lost half way", victim);
    }
}

// nightlyNotice: to staff as they arrive, while the nightly backup is on
// and last night's did not happen. The same shape as the file manager's
// "uploads awaiting approval".
void Bbs::nightlyNotice(Session& s) {
    if (!nightlyFail_ || !sdNightly()) return;
    const char* why = nightlyFail_ == NB_NOCARD  ? "Last night's backup failed: no card."     // NB-failed-nocard
                    : nightlyFail_ == NB_NOCLOCK ? "Last night's backup failed: no clock."    // NB-failed-clock
                                                 : "Last night's backup failed: card full.";  // NB-failed
    line(s, Color::Yellow, why);
}
