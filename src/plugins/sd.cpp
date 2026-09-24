/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/sd.cpp
 * Module:       Plugin / SD card
 *
 * Purpose:      Mounts an SD card over SPI and tells the rest of the board
 *                  it is there. Optional throughout: a board with no card is
 *                  a complete board, and this plugin's whole job when there
 *                  is no card is to say so plainly and get out of the way.
 *
 *               What the card is for is the things that grow without limit
 *                  and can be lost: message bases, file areas, a sysop's own
 *                  screens, bigger logs. What stays on internal flash is
 *                  everything that must survive the card failing, which is
 *                  the accounts, the configuration and the caller log.
 *
 *               FAT32 rather than LittleFS, deliberately. Pulling the card
 *                  and reading your board's messages on any laptop is the
 *                  "you own your data" claim made physical, and a filesystem
 *                  nothing else can read would quietly take that away. The
 *                  cost is that FAT is not power-fail safe, which is exactly
 *                  why nothing that must survive lives here.
 *
 * Config:       [plugin:sd]
 *                  enabled      yes (PF_ON: wire a card, reboot, it works)
 *                  cs, mosi,
 *                  clk, miso    GPIO numbers, defaults matching the wiring
 *                               page: CS 5, MOSI 23, CLK 18, MISO 19
 *                  screens      yes: screens on the card override the stock
 *                               set. A missing file falls back to internal.
 *                  nightly      no: yes makes a full backup on the card at
 *                               03:00 every night, keeping the last seven
 *                               (1.1.0; the core does it, see sdNightly).
 *
 * Commands:     SD, SD MOUNT, SD UNMOUNT
 *
 * Depends on:   plat::sdMount / sdUnmount / sdBase / sdInfo
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     CLAUDE.md, NEXT.md
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

#include "../core/plugin.h"
#include "../core/bbs.h"
#include "../core/screens.h"   // declares sdScreensDir, so it is checked against the definition
#include "../core/backup.h"    // and sdNightly
#include "../core/bbs_util.h"
#include "../platform/platform.h"
#include "../config.h"
#include "panel_feed.h"       // sdcard::panel, on a board with a display

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <dirent.h>
#include <sys/stat.h>

using bbsu::ieq;

namespace {

const char* const kName = "sd";

uint8_t        g_index   = 0;
Bbs*           g_bbs     = nullptr;        // to close card screens before an unmount
plat::SdPins   g_pins;                     // defaults are the wiring page's pins
bool           g_screens = true;           // a card's screens override the stock set
bool           g_nightly = false;          // a backup on the card every night (1.1.0)
bool           g_running = false;          // plugin enabled, not just card present
char           g_why[72] = "not mounted";  // why there is no card, for SD and DASH

// A mount has been tried on g_pins since boot and found no card, or the
// sysop unmounted the card (1.1.0). start() runs at every CONFIG save, and it
// used to probe the bus again every time on a board with no card: a stall
// in the loop for nothing, since the answer cannot change until somebody
// puts a card in, and then they type SD MOUNT. SD MOUNT and new pins still
// try; a reboot starts over.
bool           g_tried   = false;

// sdInfo() reaches the filesystem, and status() is called from the DASH
// refresh, once per running plugin per row per redraw. Cached so a card that
// is slow to answer cannot turn a dashboard into a stall.
plat::SdInfo   g_info;
uint32_t       g_infoAt  = 0;
const uint32_t kInfoMs   = 3000;

const plat::SdInfo& cardInfo(bool force = false) {
    uint32_t now = plat::millis();
    if (force || !g_infoAt || now - g_infoAt >= kInfoMs) {
        g_info  = plat::sdInfo();
        g_infoAt = now ? now : 1;
        if (g_info.mounted) plat::diskPulse(plat::DISK_CARD);   // it read the FAT
    }
    return g_info;
}

// noMount: say on the drive light that a mount failed, when it matters.
//
// A sysop who typed SD MOUNT asked for a card, so any failure is an error.
// The mount at start is different: it runs on every board at boot and at
// every CONFIG save, and a board with no card is a complete board, so "no
// card found" there is not an error and must not blink red after every
// save. A card that is there and will not mount (not FAT, or answering and
// failing) is, on every path. "no card found" opens the platform's message
// for exactly the absent case, on the board and on the host.
void noMount(bool asked) {
    if (asked || strncmp(g_why, "no card found", 13)) plat::diskPulse(plat::DISK_ERROR);
}

// ---------------------------------------------------------------------------
// readKey: this plugin's own section. Pin numbers are checked against what
// the ESP32 can actually drive rather than taken on trust, because a typo
// here comes back as "no card found" and sends somebody to re-seat a card
// that was never the problem.
//
// On the ESP32, 34 to 39 are input-only, so none of the four can live there;
// 6 to 11 are the SPI flash the firmware is running from, and driving one of
// those does not produce an error message, it produces a board that stops.
// The chip's ranges are board.h's and its flash pins are pinProblem's, so an
// S3 (every pin to 48 an output, its flash at 26 to 37) is right too.
// ---------------------------------------------------------------------------
bool usablePin(long p, bool inputOnlyOk) {
    if (p < 0 || p > BBS_GPIO_MAX) return false;
    if (syscfg::pinProblem(p)) return false;                    // the flash the firmware runs from
    if (p > BBS_GPIO_OUT_MAX && !inputOnlyOk) return false;     // input only: no good for CS/MOSI/CLK
    return true;
}

void readKey(void* ctx, const char* key, const char* value) {
    (void)ctx;
    // MISO is the only one of the four the board reads rather than drives, so
    // it is the only one that may live on 34..39. Rejecting those for MISO
    // refused the legitimate case: GPIO36 is a perfectly good MISO pin and is
    // what those pins are for. The rejection went to the serial console,
    // which a sysop dialled in over telnet cannot see, so the symptom was a
    // card that would not mount while CONFIG showed the pin they had set.
    auto pin = [&](int8_t& out, bool inputOnlyOk) {
        char* end = nullptr;
        long  p   = strtol(value, &end, 10);
        bool  num = end && end != value && !*end;
        if (num && usablePin(p, inputOnlyOk)) { out = static_cast<int8_t>(p); return; }
        // strtol gives 0 for anything it cannot read, and 0 is a real pin
        // (the BOOT button), so a typo in a hand-edited system.cfg would
        // quietly become GPIO0 rather than being refused.
        snprintf(g_why, sizeof(g_why), "%.20s = %.20s is not a usable pin", key, value);
        plat::log("sd: %s = %s is not a pin this board can use, keeping %d",
                  key, value, static_cast<int>(out));
    };
    if      (!strcmp(key, "cs"))   pin(g_pins.cs,   false);
    else if (!strcmp(key, "mosi")) pin(g_pins.mosi, false);
    else if (!strcmp(key, "clk"))  pin(g_pins.clk,  false);
    else if (!strcmp(key, "miso")) pin(g_pins.miso, true);
    else if (!strcmp(key, "speed")) {
        long k = strtol(value, nullptr, 10);
        if (k >= 400 && k <= 40000) g_pins.speedKHz = static_cast<uint16_t>(k);
        else plat::log("sd: speed = %s is outside 400..40000 kHz, keeping %u",
                       value, static_cast<unsigned>(g_pins.speedKHz));
    }
    else if (!strcmp(key, "screens")) {
        g_screens = !strcasecmp(value, "yes") || !strcasecmp(value, "on") ||
                    !strcasecmp(value, "true") || !strcmp(value, "1");
    }
    else if (!strcmp(key, "nightly")) {
        g_nightly = !strcasecmp(value, "yes") || !strcasecmp(value, "on") ||
                    !strcasecmp(value, "true") || !strcmp(value, "1");
    }
}

// ---------------------------------------------------------------------------
// start: read the config, then mount if the card is not already up.
//
// start() is not only the boot path. Saving any CONFIG page stops and starts
// every plugin, so the first cut of this unmounted and remounted the card
// every time the sysop changed an unrelated setting, stalling every caller's
// line for the full SPI negotiation. The header above and the platform
// header both promise that cannot happen, and they were wrong.
//
// So: mounting is skipped when a card is already mounted on the same pins,
// and stop() leaves the card alone. The mount is board-level state rather
// than something this plugin owns for the length of a config generation. The
// only things that take a card down are SD UNMOUNT and a reboot, which is
// what a sysop expects of a mounted filesystem anyway.
//
// A failed mount is not a failed start. A board with no card carries on and
// says so; refusing to start would take the SD command away with it, and
// that is the one command somebody troubleshooting their wiring needs.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// copyOne: one file, flash to card, all or nothing.
//
// A partly written screen is worse than no screen at all, because the card
// wins at playback: a truncated welcome.ans would override the good flash
// copy and play as line noise to every caller until somebody noticed. So a
// copy that fails anywhere takes its own half-file with it and leaves the
// flash copy to be found on the next lookup.
// ---------------------------------------------------------------------------
bool copyOne(const char* from, const char* to) {
    FILE* in = fopen(from, "rb");
    if (!in) { plat::diskPulse(plat::DISK_ERROR); return false; }
    FILE* out = fopen(to, "wb");
    if (!out) { fclose(in); plat::diskPulse(plat::DISK_ERROR); return false; }
    plat::diskPulse(plat::DISK_FLASH);                 // the drive light: read here
    plat::diskPulse(plat::DISK_CARD);                  // and written there

    char   buf[256];
    size_t n;
    bool   ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    if (ferror(in)) ok = false;
    fclose(in);
    if (fclose(out) != 0) ok = false;      // FAT reports a full card here
    if (!ok) { remove(to); plat::diskPulse(plat::DISK_ERROR); }
    return ok;
}

// ---------------------------------------------------------------------------
// seedScreens: put the stock screens on the card the first time it is seen.
//
// The card's screens folder was only ever an override directory and nothing
// wrote to it, so on a fresh card it was empty, the Screens file area listed
// nothing, and a sysop had no way to discover that editing screens was even
// possible. Logs looked fine next to it only because the caller log is
// actively mirrored there.
//
// A file the sysop edited always wins; that is the whole point of the
// override. Until 0.22.0 this only ever filled gaps, which also meant a
// seeded copy nobody touched could never be updated: see seedScreens below
// for how the board now tells its own copy from the sysop's. Flash is never
// written, which is what keeps the card optional. Pull it and the board
// falls back to the set it shipped with, exactly as before.
//
// ~21 KB across 24 files, done once at mount while no caller is waiting.
// ---------------------------------------------------------------------------

// fileHash: FNV-1a over a file's bytes, 0 when it cannot be read. Only ever
// compared for equality, so the cost of a collision is one screen not
// refreshed, never one overwritten.
uint32_t fileHash(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t h = 2166136261u;
    uint8_t  buf[64];
    size_t   n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        for (size_t i = 0; i < n; ++i) { h ^= buf[i]; h *= 16777619u; }
    fclose(f);
    return h ? h : 1u;
}

// seededHash: what the manifest says this screen was when the board put it
// on the card, 0 when it has no entry. known says whether it has one at all,
// because an entry of 0 is a mark of its own (kMine).
uint32_t seededHash(const char* manifest, const char* name, bool& known) {
    known = false;
    FILE* f = fopen(manifest, "r");
    if (!f) return 0;
    char line[96];
    uint32_t found = 0;
    while (fgets(line, sizeof(line), f)) {
        char* sp = strchr(line, ' ');
        if (!sp) continue;
        *sp = '\0';
        if (strcmp(line, name)) continue;
        found = static_cast<uint32_t>(strtoul(sp + 1, nullptr, 16));
        known = true;
    }
    fclose(f);
    return found;
}

// kMine: the manifest's word for "the sysop's own", 0, which fileHash never
// gives. RESTORE SD SCREENS writes it for every screen it puts on the card
// (sdSeededMark, 1.1.0): an imported screen is the sysop's even where it
// matches a stock screen byte for byte, which is exactly the case the "no
// record, but the same as stock" rule below would otherwise take back.
constexpr uint32_t kMine = 0;

// kPastStock: stock screens as the board shipped them while it seeded cards
// and kept no record of doing so, 0.18.0 to 0.22.0, in every version that is
// not the stock screen now. A card copy with no record that is byte for byte
// one of these was put there by the board, not by the sysop, and is the
// board's to refresh. Without this such a copy is indistinguishable from a
// sysop's edit, and stays stale for ever: Unleashed HQ's welcome after its
// 1.0.0 update.
//
// A closed set: every board since 0.22.1 keeps the manifest, so nothing is
// ever added here. Worked from git with the FNV-1a fileHash() uses, over the
// blobs at 0.18.0 (a9eb971) and 0.21.9 (c1c225d), the only two screen sets
// the board seeded without recording it, less those still shipped as they
// were. A later stock change needs no entry: the manifest records it.
struct PastStock { const char* name; uint32_t hash; };
constexpr PastStock kPastStock[] = {
    { "privacy.ans", 0xfc9b7644u },   // 40 columns, reflowed in 1.1.0
    { "welcome.ans", 0x61fc8115u },   // before the 300 baud welcome, 0.21.9
    { "welcome.asc", 0x62b7208fu },
    { "welcome.seq", 0x2d7a6a99u },
};

bool pastStock(const char* name, uint32_t hash) {
    for (const PastStock& p : kPastStock)
        if (p.hash == hash && !strcmp(p.name, name)) return true;
    return false;
}

// replaceFile: tmp over path. Renamed over first, which on LittleFS and in
// POSIX replaces the old file in one step. FatFs refuses a name that is
// there (EEXIST), and only then, with the new file whole beside it, does the
// old one go first. Any other failure leaves the old file as it was: a live
// file is never removed to make room for a rename that may still fail.
bool replaceFile(const char* tmp, const char* path) {
    if (rename(tmp, path) == 0) return true;
    if (errno != EEXIST) { remove(tmp); return false; }
    remove(path);
    return rename(tmp, path) == 0;
}

// seedScreens: the stock screens on the card, and kept current.
//
// Seeding gave the Screens file area something in it, and the card is
// played before flash, so a card copy is what callers see. That had a cost
// nobody noticed until the welcome screen changed in 0.21.9: a stock screen
// improved in a new build never reached a board with a card, because the
// card still held the copy seeded the day it was first mounted.
//
// So the board remembers what it put there. .seeded in the card's screens
// folder holds a hash of each screen as seeded. At mount:
//   - not on the card            seed it, and record it
//   - on the card, unchanged     still the board's copy: refresh it if the
//     since it was seeded        stock one has changed, and record that
//   - on the card, changed       the sysop edited it. It is theirs now, and
//                                it is never touched or recorded again
//   - on the card, no record     the sysop's own, or seeded by a build older
//                                than the manifest. Left alone, because a
//                                sysop's work overwritten is far worse than
//                                a stale screen, which they can delete,
//                                unless it is byte for byte the stock screen
//                                or one the board shipped before it kept the
//                                manifest (kPastStock): then it is the
//                                board's own copy, recorded and followed.
//   - marked as the sysop's      imported by RESTORE SD SCREENS (kMine):
//                                never touched, and the mark is kept.
void seedScreens() {
    constexpr size_t kNameCap = 64;        // longest screen filename copied
    const char* base = plat::sdBase();
    if (!g_running || !g_screens || !base[0]) return;

    char dst[96], src[96];
    snprintf(dst, sizeof(dst), "%s/%s", base, BBS_SD_SCREEN_DIR);
    snprintf(src, sizeof(src), "%s/%s", plat::fsBase(), BBS_SCREEN_DIR);

    mkdir(dst, 0755);                      // already there is success

    DIR* d = opendir(src);
    if (!d) return;                        // nothing shipped to seed from

    char manifest[112], tmp[120];
    snprintf(manifest, sizeof(manifest), "%s/.seeded", dst);
    snprintf(tmp, sizeof(tmp), "%s.tmp", manifest);
    FILE* out = fopen(tmp, "w");

    uint8_t made = 0, fresh = 0, failed = 0;
    char    from[208], to[208];
    for (struct dirent* e = readdir(d); e; e = readdir(d)) {
        if (e->d_name[0] == '.') continue; // . .. and anything hidden

        // A name too long to hold is skipped rather than truncated. A cut
        // path does not fail, it names a different file, and the one thing
        // this function must never do is write a screen under a name
        // somebody did not choose. No real screen name is near this.
        size_t len = strlen(e->d_name);
        if (!len || len > kNameCap) continue;

        snprintf(to, sizeof(to), "%s/%.*s", dst, static_cast<int>(kNameCap), e->d_name);
        snprintf(from, sizeof(from), "%s/%.*s", src, static_cast<int>(kNameCap), e->d_name);
        uint32_t stock  = fileHash(from);
        uint32_t record = 0;
        bool     mine   = false;
        struct stat st;
        if (stat(to, &st) != 0) {
            if (copyOne(from, to)) { ++made; record = stock; } else ++failed;
        } else {
            bool     known = false;
            uint32_t was   = seededHash(manifest, e->d_name, known);
            if (known && was == kMine) {
                mine = true;                           // imported: the sysop's
            } else {
                uint32_t now = fileHash(to);
                // No record, but byte for byte the stock screen, or a stock
                // screen as the board shipped it before it kept a record:
                // provably the board's own copy (or one indistinguishable
                // from it), so it joins the record and follows the stock
                // one from here. This is how copies seeded before the
                // manifest existed get picked up wherever they were never
                // edited, including ones already out of date.
                if (!was && (now == stock || pastStock(e->d_name, now))) was = now;
                if (was && now == was) {               // still the board's copy
                    if (was == stock) {
                        record = was;
                    } else if (copyOne(from, to)) {    // the stock one moved on
                        ++fresh;
                        record = stock;
                    } else {
                        // copyOne removes a half-written copy, so a failed
                        // refresh falls back to the flash screen rather than
                        // a broken one.
                        ++failed;
                    }
                }
            }
        }
        if ((record || mine) && out)
            fprintf(out, "%.*s %08lx\n", static_cast<int>(kNameCap), e->d_name,
                    static_cast<unsigned long>(mine ? kMine : record));
    }
    closedir(d);
    if (out && fclose(out) == 0) replaceFile(tmp, manifest);
    else if (out)                remove(tmp);

    if (failed)
        plat::log("sd: seeded %u, refreshed %u screens, %u could not be written",
                  static_cast<unsigned>(made), static_cast<unsigned>(fresh),
                  static_cast<unsigned>(failed));
    else if (made || fresh)
        plat::log("sd: seeded %u, refreshed %u screens on the card",
                  static_cast<unsigned>(made), static_cast<unsigned>(fresh));
}

bool start(Bbs& bbs) {
    g_bbs   = &bbs;
    g_index = plugins::indexOf(kName);
    plat::SdPins before = g_pins;
    bool         had    = plat::sdBase()[0] != '\0';
    char         was[sizeof(g_why)];
    snprintf(was, sizeof(was), "%s", g_why);

    g_pins    = plat::SdPins();
    g_screens = true;
    g_nightly = false;
    if (!had) snprintf(g_why, sizeof(g_why), "%s", "not mounted");
    plugins::forEachKey(g_index, readKey, nullptr);
    g_running = true;

    bool moved = before.cs != g_pins.cs || before.mosi != g_pins.mosi ||
                 before.clk != g_pins.clk || before.miso != g_pins.miso ||
                 before.speedKHz != g_pins.speedKHz;
    if (had && !moved) return true;              // already up on these pins
    // No card on these pins last time, or the sysop took it out: asking the
    // bus again at a CONFIG save only stalls every caller. SD says why as it
    // did, unless a pin in the file has just been refused.
    if (!had && !moved && g_tried) {
        if (!strcmp(g_why, "not mounted")) snprintf(g_why, sizeof(g_why), "%s", was);
        return true;
    }
    if (had && moved) {
        plat::log("sd: pins changed, remounting");
        if (g_bbs) { g_bbs->closeCardScreens(); g_bbs->dropCardJob(); }
        plat::sdUnmount();
    }

    g_tried = true;
    if (plat::sdMount(g_pins, g_why, sizeof(g_why))) {
        plat::diskPulse(plat::DISK_CARD);
        snprintf(g_why, sizeof(g_why), "%s", "mounted");
        // Only on a mount we actually performed. The early return above
        // means a CONFIG save does not come through here, so saving an
        // unrelated setting never walks the screens folder.
        tidyCardBackups();
        seedScreens();
    } else {
        noMount(false);
        plat::log("sd: no card: %s", g_why);
    }
    cardInfo(true);
    return true;
}

// stop: the plugin goes away, the card does not. See start() for why. What
// does stop is the screen override, so a board whose sd plugin is switched
// off in system.cfg goes back to the stock screens on the next reload even
// though the card is still mounted underneath.
void stop() {
    g_running = false;
}

// ---------------------------------------------------------------------------
// screensDir: where the screen player should look first, or nullptr.
//
// The card overrides the stock screens rather than replacing them: a file
// the card does not have falls back to internal, so a sysop can put one
// custom screen on a card without having to supply all of them. Pull the
// card and the board runs on the stock set rather than losing its screens.
// ---------------------------------------------------------------------------
const char* screensDir() {
    static char dir[80];
    if (!g_running || !g_screens || !plat::sdBase()[0]) return nullptr;
    snprintf(dir, sizeof(dir), "%s/%s", plat::sdBase(), BBS_SD_SCREEN_DIR);
    return dir;
}

// ---------------------------------------------------------------------------
// status: the DASH row. Static storage, no work worth mentioning, because
// DASH redraws on a timer.
// ---------------------------------------------------------------------------
const char* status() {
    static char line[64];
    const plat::SdInfo& i = cardInfo();
    if (!i.mounted) {
        snprintf(line, sizeof(line), "SD: no card");
        return line;
    }
    // Megabytes, because a card is measured in gigabytes and nobody reads
    // seven digits of kilobytes on a dashboard.
    snprintf(line, sizeof(line), "SD: %.9s  %u MB free of %u",
             i.type, static_cast<unsigned>(i.freeKB / 1024u),
             static_cast<unsigned>(i.totalKB / 1024u));
    return line;
}

void showStatus(Bbs& b, Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    char buf[80];
    const plat::SdInfo& i = cardInfo(true);      // the sysop asked, so measure now

    t.color(tl, Color::Yellow);
    t.text(tl, "SD card");
    t.nl(tl);

    if (!i.mounted) {
        t.color(tl, Color::LightRed);
        snprintf(buf, sizeof(buf), "  %.70s", g_why);
        t.text(tl, buf);
        t.nl(tl);
        t.color(tl, Color::Grey);
        snprintf(buf, sizeof(buf), "  wired CS %d MOSI %d CLK %d MISO %d",
                 g_pins.cs, g_pins.mosi, g_pins.clk, g_pins.miso);
        t.text(tl, buf);
        t.nl(tl);
        snprintf(buf, sizeof(buf), "  bus %u kHz. CONFIG sd lowers it.",
                 static_cast<unsigned>(g_pins.speedKHz));
        t.text(tl, buf);
        t.nl(tl);
        t.text(tl, "  SD MOUNT tries again.");
        t.nl(tl);
        t.text(tl, "  The board runs fine without one.");
        t.nl(tl);
        b.prompt(s);
        return;
    }

    t.color(tl, Color::LightGreen);
    snprintf(buf, sizeof(buf), "  %.10s at %s, %u MHz", i.type, plat::sdBase(),
             static_cast<unsigned>(i.speedKHz / 1000u));
    t.text(tl, buf);
    t.nl(tl);
    t.color(tl, Color::Grey);
    snprintf(buf, sizeof(buf), "  %u MB free of %u MB",
             static_cast<unsigned>(i.freeKB / 1024u),
             static_cast<unsigned>(i.totalKB / 1024u));
    t.text(tl, buf);
    t.nl(tl);
    const char* sc = screensDir();
    snprintf(buf, sizeof(buf), "  screens: %s", sc ? sc : "stock set only");
    t.text(tl, buf);
    t.nl(tl);
    b.prompt(s);
}

// ---------------------------------------------------------------------------
// The commands. SD MOUNT and SD UNMOUNT both block for a moment, which is
// why they are the sysop's to type and nothing calls them on a timer.
// ---------------------------------------------------------------------------
const Command kCommands[] = {
    { "SD", "", 0, CF_ADMIN, "SD", "SD card status",
      [](Bbs& b, Session& s, const char* a, uint32_t) {
          Term& t = s.term;
          Timeline& tl = s.tl;
          char buf[80];
          if (ieq(a, "mount")) {
              // Every caller's line stops for this. Say so first, so the
              // pause is something the sysop was told about rather than
              // something that looked like a crash.
              t.color(tl, Color::Grey);
              t.text(tl, "Mounting, the board pauses.");
              t.nl(tl);
              bool fresh = !plat::sdBase()[0];     // not already up: a mount of our own
              g_tried = true;
              if (plat::sdMount(g_pins, g_why, sizeof(g_why))) {
                  plat::diskPulse(plat::DISK_CARD);
                  snprintf(g_why, sizeof(g_why), "%s", "mounted");
                  // Only on a card this has just mounted: on one already up,
                  // a backup may be writing its .tmp right now.
                  if (fresh) tidyCardBackups();
                  seedScreens();          // a fresh card gets the stock set
                  const plat::SdInfo& i = cardInfo(true);
                  t.color(tl, Color::LightGreen);
                  snprintf(buf, sizeof(buf), "Mounted: %.10s, %u MB free.", i.type,
                           static_cast<unsigned>(i.freeKB / 1024u));
                  t.text(tl, buf);
              } else {
                  noMount(true);          // the sysop asked, so no card is an error
                  t.color(tl, Color::LightRed);
                  snprintf(buf, sizeof(buf), "%.70s", g_why);
                  t.text(tl, buf);
              }
              b.prompt(s);
              return;
          }
          if (ieq(a, "unmount")) {
              if (!plat::sdBase()[0]) {
                  t.color(tl, Color::Grey);
                  t.text(tl, "No card is mounted.");
                  b.prompt(s);
                  return;
              }
              // Anyone mid-screen from the card has to be let go first, and
              // so does a backup being written to it (the nightly one).
              b.closeCardScreens();
              b.dropCardJob();
              plat::diskPulse(plat::DISK_CARD);  // the flush
              plat::sdUnmount();
              g_tried = true;                    // a CONFIG save must not put it back
              cardInfo(true);
              snprintf(g_why, sizeof(g_why), "%s", "unmounted by the sysop");
              t.color(tl, Color::LightGreen);
              t.text(tl, "Unmounted. The card is safe to pull.");
              b.prompt(s);
              return;
          }
          showStatus(b, s);
      },
      Menu::Sysop, 4 },
    { "SD", "", 0, CF_ADMIN | CF_HELPONLY, "SD MOUNT", "mount the card (pauses the board)", nullptr,
      Menu::Sysop, 5 },
    { "SD", "", 0, CF_ADMIN | CF_HELPONLY, "SD UNMOUNT", "flush and release the card", nullptr,
      Menu::Sysop, 6 },
};

// PS_PIN, so CONFIG refuses the flash pins on the form (syscfg::pinProblem)
// instead of writing one that readKey then quietly declines.
const PluginSetting kSettings[] = {
    { "cs",      "CS pin",   PS_PIN,   0, 33, 2 },
    { "mosi",    "MOSI pin", PS_PIN,   0, 33, 2 },
    { "clk",     "CLK pin",  PS_PIN,   0, 33, 2 },
    { "miso",    "MISO pin", PS_PIN,   0, 39, 2 },
    { "speed",   "Bus kHz",  PS_NUM,   400, 40000, 5 },
    { "screens", "Screens",  PS_YESNO, 0, 0,  4 },
    // A full backup at 03:00 into the card's backup folder, the last seven
    // kept, named nightly-YYYYMMDD.zip so a backup the sysop made by hand is
    // never counted among them (1.1.0). Off as shipped. The note avoids the
    // hour on purpose (the copy): COMMANDS.md and the setup guide give it.
    { "nightly", "Nightly",  PS_YESNO, 0, 0,  4, "A zip every night; the last 7 kept." },  // NB-note
};

// setting: the live value, for a key system.cfg does not carry yet
void setting(const char* key, char* out, size_t n) {
    if      (!strcmp(key, "cs"))      snprintf(out, n, "%d", g_pins.cs);
    else if (!strcmp(key, "mosi"))    snprintf(out, n, "%d", g_pins.mosi);
    else if (!strcmp(key, "clk"))     snprintf(out, n, "%d", g_pins.clk);
    else if (!strcmp(key, "miso"))    snprintf(out, n, "%d", g_pins.miso);
    else if (!strcmp(key, "speed"))   snprintf(out, n, "%u", static_cast<unsigned>(g_pins.speedKHz));
    else if (!strcmp(key, "screens")) snprintf(out, n, "%s", g_screens ? "yes" : "no");
    else if (!strcmp(key, "nightly")) snprintf(out, n, "%s", g_nightly ? "yes" : "no");
}

} // namespace

#ifdef BBS_HAS_LCD
// sdcard::panel: the card as the board's display shows it (panel_feed.h).
// Mounted is the mount point being set, which is RAM. A card that would not
// mount is g_why holding a mount's refusal: anything but "no card found",
// which is an empty slot, and the three words this file writes when nothing
// has failed.
uint8_t sdcard::panel(uint32_t& freeKB, bool refresh) {
    freeKB = 0;
    if (plat::sdBase()[0]) {
        const plat::SdInfo& i = refresh ? cardInfo() : g_info;
        if (i.mounted) freeKB = i.freeKB;
        return 1;
    }
    if (g_running && strncmp(g_why, "no card found", 13) && strcmp(g_why, "not mounted") &&
        strcmp(g_why, "mounted") && strcmp(g_why, "unmounted by the sysop"))
        return 2;
    return 0;
}
#endif

// sdScreensDir: the screen player asks this, so it does not have to know a
// plugin exists. Null when there is no card or the override is switched off.
const char* sdScreensDir() {
    return screensDir();
}

// sdNightly: the core's nightly backup asks this (backup.h). Whether there is
// a card is the core's question, so that "no card" can be said rather than
// the night passing without a word.
bool sdNightly() {
    return g_running && g_nightly;
}

// sdCardInfo: the kept figures (cardInfo), for the core (backup.h).
const plat::SdInfo& sdCardInfo() {
    return cardInfo();
}

// sdSeededStock: the manifest has this card screen as the board's copy and
// the file still is it (screens.h). Read when asked, for SCREENS: nothing is
// kept.
bool sdSeededStock(const char* file) {
    const char* base = plat::sdBase();
    if (!file || !base[0]) return false;
    char manifest[112], path[160];
    snprintf(manifest, sizeof(manifest), "%s/%s/.seeded", base, BBS_SD_SCREEN_DIR);
    bool     known = false;
    uint32_t was   = seededHash(manifest, file, known);
    if (!known || was == kMine) return false;
    snprintf(path, sizeof(path), "%s/%s/%.64s", base, BBS_SD_SCREEN_DIR, file);
    return fileHash(path) == was;
}

// ---------------------------------------------------------------------------
// sdSeededMark: RESTORE SD SCREENS has put these screens on the card, and
// they are the sysop's own from now on (ziparc.h). The manifest is written
// again with each of them marked kMine and every other line as it was, in
// one pass, through a temp file.
// ---------------------------------------------------------------------------
void sdSeededMark(const char* (*nth)(void* ctx, uint8_t i), void* ctx) {
    const char* base = plat::sdBase();
    if (!nth || !base[0]) return;
    char manifest[112], tmp[120];
    snprintf(manifest, sizeof(manifest), "%s/%s/.seeded", base, BBS_SD_SCREEN_DIR);
    snprintf(tmp, sizeof(tmp), "%s.tmp", manifest);
    FILE* out = fopen(tmp, "w");
    if (!out) return;
    auto imported = [&](const char* name) {
        for (uint8_t i = 0; ; ++i) {
            const char* n = nth(ctx, i);
            if (!n) return false;
            if (!strcmp(n, name)) return true;
        }
    };
    bool ok = true;
    FILE* in = fopen(manifest, "r");
    if (in) {
        char line[96];
        while (ok && fgets(line, sizeof(line), in)) {
            char name[72];
            const char* sp = strchr(line, ' ');
            size_t n = sp ? static_cast<size_t>(sp - line) : 0;
            if (!n || n >= sizeof(name)) continue;       // not a line of ours: dropped
            memcpy(name, line, n);
            name[n] = '\0';
            if (imported(name)) continue;                // written below, marked
            ok = fputs(line, out) >= 0;
        }
        fclose(in);
    }
    uint8_t marked = 0;
    for (uint8_t i = 0; ok; ++i) {
        const char* n = nth(ctx, i);
        if (!n) break;
        ok = fprintf(out, "%.64s %08lx\n", n, static_cast<unsigned long>(kMine)) > 0;
        ++marked;
    }
    if (fclose(out) != 0) ok = false;
    if (ok && replaceFile(tmp, manifest)) {
        plat::diskPulse(plat::DISK_CARD);
        plat::log("sd: %u imported screen%s marked as the sysop's", static_cast<unsigned>(marked),
                  marked == 1 ? "" : "s");
    } else {
        remove(tmp);
        plat::diskPulse(plat::DISK_ERROR);
        plat::log("sd: could not mark the imported screens in %s", manifest);
    }
}

extern const Plugin kSdPlugin = {
    // PF_EARLY because a PF_SD plugin may only start once this one has had
    // its go at mounting. PF_ON because the point of the wiring page is that
    // you solder four jumpers, reboot, and it works.
    { kName, "SD card", "1.0", 0, 0, PF_CORE | PF_ON | PF_EARLY,
      PlugLevel::Sysop, PlugLevel::Sysop, PlugLevel::Sysop },
    start,
    stop,
    nullptr,                 // tick: nothing to poll, and polling a bus would block
    nullptr,                 // onConnect
    nullptr,                 // onLogin
    nullptr,                 // onLogoff
    nullptr,                 // onKey
    status,
    kCommands,
    sizeof(kCommands) / sizeof(kCommands[0]),
    kSettings,
    sizeof(kSettings) / sizeof(kSettings[0]),
    setting,
    nullptr,                 // rows: no paged list of its own
    nullptr,                 // onPresence
    nullptr,                 // onBytes
    nullptr,                 // onRename
    nullptr,                 // listDone
};
