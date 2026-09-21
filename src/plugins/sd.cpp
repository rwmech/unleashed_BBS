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
 *
 * Commands:     SD, SD MOUNT, SD UNMOUNT
 *
 * Depends on:   plat::sdMount / sdUnmount / sdBase / sdInfo
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     CLAUDE.md, NEXT.md
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

#include "../core/plugin.h"
#include "../core/bbs.h"
#include "../core/screens.h"   // declares sdScreensDir, so it is checked against the definition
#include "../core/bbs_util.h"
#include "../platform/platform.h"
#include "../config.h"

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
bool           g_running = false;          // plugin enabled, not just card present
char           g_why[72] = "not mounted";  // why there is no card, for SD and DASH

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
    }
    return g_info;
}

// ---------------------------------------------------------------------------
// readKey: this plugin's own section. Pin numbers are checked against what
// the ESP32 can actually drive rather than taken on trust, because a typo
// here comes back as "no card found" and sends somebody to re-seat a card
// that was never the problem.
//
// 34 to 39 are input-only on this part, so none of the four can live there;
// 6 to 11 are the SPI flash the firmware is running from, and driving one of
// those does not produce an error message, it produces a board that stops.
// ---------------------------------------------------------------------------
bool usablePin(long p, bool inputOnlyOk) {
    if (p < 0 || p > 39) return false;
    if (p >= 6 && p <= 11) return false;     // the flash the firmware runs from
    if (p >= 34 && !inputOnlyOk) return false;   // input only: no good for CS/MOSI/CLK
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
    if (!in) return false;
    FILE* out = fopen(to, "wb");
    if (!out) { fclose(in); return false; }

    char   buf[256];
    size_t n;
    bool   ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    if (ferror(in)) ok = false;
    fclose(in);
    if (fclose(out) != 0) ok = false;      // FAT reports a full card here
    if (!ok) remove(to);
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
// Copy only what the card does not already have. A file on the card is
// somebody's edit and the whole point of the override is that it wins, so
// this never overwrites: it fills gaps. Flash is never written, which is
// what keeps the card optional. Pull it and the board falls back to the set
// it shipped with, exactly as before.
//
// ~21 KB across 24 files, done once at mount while no caller is waiting.
// ---------------------------------------------------------------------------
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

    uint8_t made = 0, failed = 0;
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
        struct stat st;
        if (stat(to, &st) == 0) continue;  // the card's own copy always wins
        snprintf(from, sizeof(from), "%s/%.*s", src, static_cast<int>(kNameCap), e->d_name);
        if (copyOne(from, to)) ++made; else ++failed;
    }
    closedir(d);

    if (failed)
        plat::log("sd: seeded %u screens to the card, %u could not be written",
                  static_cast<unsigned>(made), static_cast<unsigned>(failed));
    else if (made)
        plat::log("sd: seeded %u screens to the card",
                  static_cast<unsigned>(made));
}

bool start(Bbs& bbs) {
    g_bbs   = &bbs;
    g_index = plugins::indexOf(kName);
    plat::SdPins before = g_pins;
    bool         had    = plat::sdBase()[0] != '\0';

    g_pins    = plat::SdPins();
    g_screens = true;
    if (!had) snprintf(g_why, sizeof(g_why), "%s", "not mounted");
    plugins::forEachKey(g_index, readKey, nullptr);
    g_running = true;

    bool moved = before.cs != g_pins.cs || before.mosi != g_pins.mosi ||
                 before.clk != g_pins.clk || before.miso != g_pins.miso ||
                 before.speedKHz != g_pins.speedKHz;
    if (had && !moved) return true;              // already up on these pins
    if (had && moved) {
        plat::log("sd: pins changed, remounting");
        if (g_bbs) g_bbs->closeCardScreens();
        plat::sdUnmount();
    }

    if (plat::sdMount(g_pins, g_why, sizeof(g_why))) {
        snprintf(g_why, sizeof(g_why), "%s", "mounted");
        // Only on a mount we actually performed. The early return above
        // means a CONFIG save does not come through here, so saving an
        // unrelated setting never walks the screens folder.
        seedScreens();
    } else {
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
              if (plat::sdMount(g_pins, g_why, sizeof(g_why))) {
                  snprintf(g_why, sizeof(g_why), "%s", "mounted");
                  seedScreens();          // a fresh card gets the stock set
                  const plat::SdInfo& i = cardInfo(true);
                  t.color(tl, Color::LightGreen);
                  snprintf(buf, sizeof(buf), "Mounted: %.10s, %u MB free.", i.type,
                           static_cast<unsigned>(i.freeKB / 1024u));
                  t.text(tl, buf);
              } else {
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
              // Anyone mid-screen from the card has to be let go first.
              b.closeCardScreens();
              plat::sdUnmount();
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

const PluginSetting kSettings[] = {
    { "cs",      "CS pin",   PS_NUM,   0, 33, 2 },
    { "mosi",    "MOSI pin", PS_NUM,   0, 33, 2 },
    { "clk",     "CLK pin",  PS_NUM,   0, 33, 2 },
    { "miso",    "MISO pin", PS_NUM,   0, 39, 2 },
    { "speed",   "Bus kHz",  PS_NUM,   400, 40000, 5 },
    { "screens", "Screens",  PS_YESNO, 0, 0,  4 },
};

// setting: the live value, for a key system.cfg does not carry yet
void setting(const char* key, char* out, size_t n) {
    if      (!strcmp(key, "cs"))      snprintf(out, n, "%d", g_pins.cs);
    else if (!strcmp(key, "mosi"))    snprintf(out, n, "%d", g_pins.mosi);
    else if (!strcmp(key, "clk"))     snprintf(out, n, "%d", g_pins.clk);
    else if (!strcmp(key, "miso"))    snprintf(out, n, "%d", g_pins.miso);
    else if (!strcmp(key, "speed"))   snprintf(out, n, "%u", static_cast<unsigned>(g_pins.speedKHz));
    else if (!strcmp(key, "screens")) snprintf(out, n, "%s", g_screens ? "yes" : "no");
}

} // namespace

// sdScreensDir: the screen player asks this, so it does not have to know a
// plugin exists. Null when there is no card or the override is switched off.
const char* sdScreensDir() {
    return screensDir();
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
