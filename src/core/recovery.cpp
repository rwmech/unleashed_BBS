/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/recovery.cpp
 * Module:       Core / recovery without a reflash (1.1.0)
 *
 * Purpose:      The BOOT-hold reset and the last network that worked, as
 *                  the board runs them: the button, the LED and the console
 *                  lines for the first, the file in userdata for the second.
 *                  The timing itself is in recovery.h, where a test can reach
 *                  it without a board.
 *
 * Libraries:    none (libc stdio)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     recovery.h
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

#include "recovery.h"
#include "disk.h"              // fopen and opendir that tell the drive light (1.1.1)
#include "netfallback.h"    // kBuiltinWifi: whether a factory reset leaves a network to rejoin
#include "sysconfig.h"
#include "../config.h"
#include "../platform/platform.h"
#include <cstdio>
#include <cstring>

namespace recovery {

// ===========================================================================
// BOOT-hold reset
// ===========================================================================

namespace {

BootHold g_hold;
int8_t   g_led = -1;           // what the watch last told the LED; -1 handed back

// ---------------------------------------------------------------------------
// act: what a release means. The console lines are the copy's (RB-*), in the
// order it gives them. Both actions restart the board: a board that comes up
// again from its files is in exactly the state the reset describes, with no
// plugin or session holding on to what was true a moment ago, and the reason
// goes into reboots.log as the reason for that boot, which is what the log
// is a record of.
// ---------------------------------------------------------------------------
void act(Stage stage, uint32_t heldMs) {
    switch (stage) {
    case Stage::Password: {
        // The line is removed rather than rewritten. A board on the published
        // default is exactly one whose system.cfg has no sysop_password line,
        // and that absence is what keeps the default local-only, the listing
        // held and setup on offer (1.0.2). Writing the default out would
        // make it a real password that works from anywhere.
        //
        // The closed state is written out as it stands (1.1.0). With no
        // closed line a board on the default starts closed, which is right
        // for a fresh board and wrong for a running one whose sysop only lost
        // the password: its callers would find it shut after the restart.
        const syscfg::KeyVal drop[] = { { "sysop_password", nullptr },
                                        { "closed", syscfg::get().closed ? "yes" : "no" } };
        char err[80] = "";
        if (!syscfg::write(drop, 2, nullptr, err, sizeof(err))) {
            plat::log("reset: could not write system.cfg (%s). Nothing changed.", err);
            return;
        }
        plat::log("reset: sysop password is back to the published default.");
        plat::log("reset: it works from this network only, until it is changed.");
        plat::log("reset: accounts, settings, mail and Wi-Fi are all kept.");
        plat::log("reset: the directory listing waits until the password changes.");
        plat::log("reset: restarting. Log in from this network to choose a new password.");
        plat::restart(NOTE_PASSWORD);
        return;
    }
    case Stage::Factory: {
        plat::log("reset: FACTORY RESET. Erasing userdata and logs.");
        plat::ledOverride(1);                       // solid while it works
        char err[24] = "";
        if (!plat::factoryErase(err, sizeof(err))) {
            plat::log("reset: erasing %s FAILED. Reinstall with Erase everything first.", err);
            // Restarted all the same. A half-erased partition is a state
            // nothing else expects, and the mount at boot formats one it
            // cannot read, which is the nearest thing to finishing the job.
            // With a note of its own, so the next boot says what happened
            // rather than "software restart", which says nothing.
            plat::restart(NOTE_FACTORY_FAILED);
            return;
        }
        plat::log("reset: done. Screens, firmware and SD card were not touched.");
        // "Wi-Fi erased" is true of every build: the network lives in
        // system.cfg and wifi.last, both on userdata, and the radio keeps
        // its own copy in RAM only. Whether the board then has somewhere to
        // go is decided at compile time, by the same header main.cpp dials.
        if (kBuiltinWifi)
            plat::log("reset: restarting, Wi-Fi erased. This build falls back to secrets.h.");
        else
            plat::log("reset: restarting, Wi-Fi erased. The web installer sets it again.");
        plat::restart(NOTE_FACTORY);
        return;
    }
    case Stage::Abort:
        plat::log("reset: let go after 20 s. Nothing changed.");
        return;
    default:
        plat::log("reset: let go at %u s. Nothing changed.", static_cast<unsigned>(heldMs / 1000u));
        return;
    }
}

} // namespace

bool bootPoll(uint32_t now) {
    if (!g_hold.watching()) return false;
    BootHold::Event e = g_hold.feed(now, plat::bootButtonDown(now));

    // The LED belongs to the watch from the press until the release, and
    // is handed back the moment it is over, before anything is acted on.
    int8_t want = -1;
    if (g_hold.watching() && g_hold.stage() != Stage::Waiting) want = g_hold.ledOn(now) ? 1 : 0;
    if (want != g_led) {
        plat::ledOverride(want);
        g_led = want;
    }

    switch (e.ev) {
    case BootHold::Ev::Held:
        plat::log("reset: BOOT held. Let go before 7 s and nothing happens.");
        break;
    case BootHold::Ev::Stage:
        if (e.stage == Stage::Password)
            plat::log("reset: 7 s. Let go now to put the sysop password back to the default.");
        else if (e.stage == Stage::Factory)
            plat::log("reset: 15 s. Let go now for a FACTORY RESET of accounts and settings.");
        else if (e.stage == Stage::Abort)
            plat::log("reset: 20 s. Cancelled. Let go; nothing will change.");
        break;
    case BootHold::Ev::Released:
        act(e.stage, e.heldMs);
        break;
    default:
        break;
    }
    return g_hold.watching();
}

bool bootWatching() {
    return g_hold.watching();
}

Led bootLed() {
    return g_hold.led();
}

// ===========================================================================
// Wi-Fi: the last network that worked
// ===========================================================================

namespace {

WifiFallback g_fb;

void lastPath(char* out, size_t n) {
    snprintf(out, n, "%s/%s", plat::userBase(), BBS_WIFI_LAST_FILE);
}

// readLine: one line without its line ending. False at the end of the file
// or when the line is too long for out, which is a file nobody should trust.
bool readLine(FILE* f, char* out, size_t n) {
    char line[80];
    if (!fgets(line, sizeof(line), f)) return false;
    size_t len = strlen(line);
    while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
    if (len >= n) return false;
    memcpy(out, line, len + 1);
    return true;
}

} // namespace

// The file is two lines, the network's name and then its password, exactly
// as the radio was given them: an empty second line is an open network.
// No comment line, because a network's name may start with anything.
bool lastGood(char (&ssid)[33], char (&pass)[65]) {
    ssid[0] = pass[0] = '\0';
    char path[96];
    lastPath(path, sizeof(path));
    FILE* f = disk::open(path, "r");
    if (!f) return false;
    bool ok = readLine(f, ssid, sizeof(ssid)) && ssid[0] && readLine(f, pass, sizeof(pass));
    fclose(f);
    if (!ok) ssid[0] = pass[0] = '\0';
    return ok;
}

// The trial's length, as the console says it: from the timer, so the words
// cannot drift from what the board actually waits.
constexpr unsigned kTrialS = static_cast<unsigned>(kWifiFallbackMs / 1000u);

void wifiBegin(uint32_t now, const char* ssid, const char* pass) {
    char os[33], op[65];
    const bool differs = lastGood(os, op) && ssid && ssid[0] &&
                         (strcmp(os, ssid) != 0 || strcmp(op, pass ? pass : "") != 0);
    g_fb.begin(now, differs);
    if (!differs) return;
    // The same name with a new password is a different network to try, and
    // naming it twice ("back to HomeNet" from HomeNet) says nothing.
    if (!strcmp(os, ssid))
        plat::log("wifi: new password for \"%s\": %u s to join, or back to the old one", ssid, kTrialS);
    else
        plat::log("wifi: %u s to join \"%s\", or back to \"%s\", which worked", kTrialS, ssid, os);
}

void wifiJoined(const char* ssid, const char* pass) {
    if (!ssid || !ssid[0]) return;
    if (!pass) pass = "";
    char os[33], op[65];
    if (lastGood(os, op) && !strcmp(os, ssid) && !strcmp(op, pass)) return;   // already kept

    char path[96], tmp[100];
    lastPath(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = disk::open(tmp, "w");
    bool ok = f != nullptr;
    if (f) {
        ok = fprintf(f, "%s\n%s\n", ssid, pass) > 0;
        ok = fflush(f) == 0 && ok;
        ok = fclose(f) == 0 && ok;
    }
    // Renamed over the old record, not removed first: LittleFS and POSIX both
    // replace in one step, so a power cut leaves the old network or the new
    // one to go back to, never neither.
    if (!ok || rename(tmp, path) != 0) {
        remove(tmp);
        plat::log("wifi: joined \"%s\" but could not keep it as the one to go back to", ssid);
        return;
    }
    plat::log("wifi: joined \"%s\"; kept as the network to go back to", ssid);
}

bool wifiDue(uint32_t now, bool up, bool busy, const char* from,
             char (&ssid)[33], char (&pass)[65]) {
    if (!g_fb.due(now, up, busy)) return false;
    if (!lastGood(ssid, pass)) return false;        // gone since boot: keep dialling as before
    if (!from) from = "";
    if (!strcmp(from, ssid))
        plat::log("wifi: could not join \"%s\" on the new password; back to the old one", ssid);
    else
        plat::log("wifi: could not join \"%s\" in %u s; going back to \"%s\"", from, kTrialS, ssid);
    // Only until the next restart: system.cfg still names the new one, so
    // every boot spends the trial on it first. Said, or nothing does.
    plat::log("wifi: each restart tries it for %u s first; change it in CONFIG network", kTrialS);
    return true;
}

} // namespace recovery
