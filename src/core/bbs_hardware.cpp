/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/bbs_hardware.cpp
 * Module:       Core / HARDWARE (1.1.1)
 *
 * Purpose:      What the board is and what it can do, on one screen. Rob:
 *                  callers should see it too, "so others can see how neat it
 *                  is". HARDWARE (HW) is public: the chip and its revision,
 *                  the cores and the clock they are running at, the flash,
 *                  the PSRAM, the board profile and its version, and what
 *                  this image has running (a card and its size, a camera and
 *                  its sensor, a display, lights). Staff also get the live
 *                  figures: PSRAM free, internal heap free and its lowest,
 *                  card free.
 *
 *                  SYS draws the same section through the same function,
 *                  hwRow, so the two cannot drift. Callers never get the
 *                  network (address, SSID, signal) or anything to do with
 *                  security: those stay in SYS, which is staff only.
 *
 *                  Every figure is a register or a counter read
 *                  (plat::chipInfo), and the card's figures are the ones the
 *                  sd plugin last kept (sdCardKept), never a trip to its FAT.
 *                  So a row costs formatting and nothing else, and the rows
 *                  are simply worked out again for each line.
 *
 *                  40 columns and 80 both: the rows are statRow's (label 13,
 *                  value 9 or its length, then a note), a note that does not
 *                  fit the width goes on a line of its own, and the list of
 *                  what the board has is word-wrapped at the width under its
 *                  label.
 *
 * Libraries:    none (libc stdio, string)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     COMMANDS.md, src/platform/platform.h (chipInfo)
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
#include "backup.h"            // sdCardKept, sdCardGB
#include "plugin.h"
#include "space.h"             // the card's kept free space (1.1.2)
#include "../platform/platform.h"
#include "../plugins/camera.h" // camera::running, camera::found (camera boards)
#include "../plugins/lights.h" // lights::wired

#include <cstdio>
#include <cstring>

using namespace bbsu;

namespace {

// Where statRow puts its value: the label is 13 columns.
constexpr uint8_t kValueCol = 13;

// fmtSize: "8 MB", "512 KB", or "-" for nothing known. Rounded to the
// nearest megabyte, because the heap's share of the PSRAM is a few KB short
// of the 4 MB mapped and must not read as 3.
void fmtSize(char* out, size_t n, uint32_t bytes) {
    constexpr uint32_t kMB = 1024u * 1024u;
    if (bytes >= kMB)  snprintf(out, n, "%u MB", static_cast<unsigned>((bytes + kMB / 2) / kMB));
    else if (bytes)    snprintf(out, n, "%u KB", static_cast<unsigned>(bytes / 1024u));
    else               snprintf(out, n, "-");
}

// cardMounted: a card is in and mounted now. sdBase is the platform's own
// state, a string read; the size comes from the sd plugin's kept figures.
bool cardMounted() {
    return plat::sdBase()[0] && sdCardKept().mounted;
}

// The snapshot of what is running (Session::hwCaps, 1.1.1). The line is
// worked out afresh for every row of the list, and a card or camera arriving
// between two rows (a caller at [More]) changed how many lines it wrapped
// to: one repeated or one skipped. Taken once when the list starts, the whole
// listing reads the same one. The card's size goes in as the power of two it
// is (sdCardGB rounds to 1, 2, 4 ... 1024), four bits.
enum : uint16_t {
    HC_TAKEN  = 0x8000,
    HC_CARD   = 0x0001,
    HC_CAMERA = 0x0002,
    HC_LCD    = 0x0004,
    HC_LIGHTS = 0x0008,
    HC_GBSHIFT = 4,                                // bits 4-7: log2 of the GB
};

uint16_t capsNow() {
    uint16_t c = HC_TAKEN;
    if (cardMounted()) {
        c |= HC_CARD;
        uint16_t gb = sdCardGB(sdCardKept());
        uint8_t  lg = 0;
        while (gb > 1 && lg < 15) { gb >>= 1; ++lg; }
        c |= static_cast<uint16_t>(lg << HC_GBSHIFT);
    }
#ifdef BBS_HAS_CAMERA
    if (camera::running() && camera::found()) c |= HC_CAMERA;
#endif
#ifdef BBS_HAS_LCD
    if (plugins::running(plugins::indexOf("panel"))) c |= HC_LCD;
#endif
    if (lights::wired()) c |= HC_LIGHTS;          // a pin, not just the plugin on
    return c;
}

// capabilities: what this image and board have, and have running, as a
// comma list: "32 GB SD card (SPI), camera (OV2640), LED lights". Only what
// works now, the rule announce's features follow: a camera whose sensor did
// not answer (or with no card in, which stops it), or lights with no pin
// wired, is not claimed. A card slot the board itself has is named empty
// when no card is in; the reference board's card is optional wiring and is
// named only when it is there.
void capabilities(char* out, size_t n, uint16_t caps) {
    size_t len = 0;
    out[0] = '\0';
    auto add = [&](const char* item) {
        if (len >= n) return;
        int w = snprintf(out + len, n - len, "%s%s", len ? ", " : "", item);
        if (w > 0) len += static_cast<size_t>(w);
        if (len >= n) len = n - 1;                  // cut, never past the end
    };
    char item[48];
#ifdef BBS_SD_SDMMC1
    const char* bus = "SDMMC";
#else
    const char* bus = "SPI";
#endif
    if (caps & HC_CARD) {
        snprintf(item, sizeof(item), "%u GB SD card (%s)",
                 1u << ((caps >> HC_GBSHIFT) & 0x0F), bus);
        add(item);
    } else {
#ifdef BBS_HAS_SD_SLOT
        snprintf(item, sizeof(item), "SD slot (%s), empty", bus);
        add(item);
#endif
    }
#ifdef BBS_HAS_CAMERA
    if (caps & HC_CAMERA) {
        const char* sensor = plat::camSensor();
        if (sensor && *sensor) snprintf(item, sizeof(item), "camera (%s)", sensor);
        else                   snprintf(item, sizeof(item), "camera");
        add(item);
    }
#endif
#ifdef BBS_HAS_LCD
    if (caps & HC_LCD) add("LCD panel");
#endif
    if (caps & HC_LIGHTS) add("LED lights");
    if (!len) snprintf(out, n, "none");
}

} // namespace

// ---------------------------------------------------------------------------
// hwRow: line k of the hardware section, false past its end. Worked out
// afresh for every line: the reads are counters, and holding the figures
// anywhere would be a Session field (twelve times over) for nothing.
//
// here() counts lines as they are passed, so a row that takes two lines at
// 40 columns and one at 80 needs no numbering of its own.
// ---------------------------------------------------------------------------
bool Bbs::hwRow(Session& s, uint8_t k, bool inSys) {
    plat::ChipInfo ci;
    plat::chipInfo(ci);
    const bool    staff = s.perms != 0;             // co-sysop 2 and up
    const uint8_t w     = rowWidth(s);
    char val[40], note[48];
    uint8_t n = 0;
    auto here = [&]() { return n++ == k; };

    // A line under a row: its text from the value column, quietly, or as far
    // left as it has to start to end inside the row. "Waveshare
    // ESP32-S3-LCD-1.47" is 27 characters: from column 13 it would be 40
    // wide at 40 columns, and wrap.
    auto under = [&](Color c, const char* text) {
        uint8_t col = 0;
        const size_t len = strlen(text);                // ASCII names and lists
        size_t indent = kValueCol;
        if (indent + len > w) indent = len < w ? w - len : 0;
        char pad[kValueCol + 1];
        memset(pad, ' ', indent);
        pad[indent] = '\0';
        rowSeg(s, Color::Grey, pad, col);
        rowSeg(s, c, text, col);
        rowEnd(s, col);
    };
    // Whether a statRow note fits the row: label, value, a space, the note.
    auto fits = [&](const char* value, const char* text) {
        size_t v = strlen(value) < 9 ? 9 : strlen(value);
        return kValueCol + v + 1 + strlen(text) <= w;          // ASCII names
    };

    if (here()) {
        if (ci.rev != plat::ChipInfo::kNoRev) {
            snprintf(note, sizeof(note), "rev %u.%u", static_cast<unsigned>(ci.rev / 100u),
                     static_cast<unsigned>(ci.rev % 100u));
            statRow(s, "Chip", ci.model, Color::White, note);
        } else {
            statRow(s, "Chip", ci.model, Color::White);
        }
        return true;
    }
    if (here()) {
        if (ci.cpuMHz) snprintf(val, sizeof(val), "%u MHz", static_cast<unsigned>(ci.cpuMHz));
        else           snprintf(val, sizeof(val), "-");
        snprintf(note, sizeof(note), "%u core%s", static_cast<unsigned>(ci.cores),
                 ci.cores == 1 ? "" : "s");
        statRow(s, "CPU", val, ci.cpuMHz ? Color::LightGreen : Color::DarkGrey, note);
        return true;
    }
    if (here()) {
        fmtSize(val, sizeof(val), ci.flash);
        statRow(s, "Flash", val, ci.flash ? Color::LightGreen : Color::DarkGrey);
        return true;
    }
    if (here()) {
        // The chip's size, and how much of it this board can use: an ESP32
        // maps 4 MB of an 8 MB chip. Just the size when the two agree.
        if (ci.psram || ci.psramHeap) {
            fmtSize(val, sizeof(val), ci.psram ? ci.psram : ci.psramHeap);
            char used[16];
            fmtSize(used, sizeof(used), ci.psramHeap);
            snprintf(note, sizeof(note), "%s mapped", used);
            const bool same = !ci.psram || !ci.psramHeap || !strcmp(val, used);
            statRow(s, "PSRAM", val, Color::LightGreen, same ? nullptr : note);
        } else {
            statRow(s, "PSRAM", "none", Color::Grey);
        }
        return true;
    }

    // The board: the profile and its version, and its name beside it when
    // the row has room, under it when it does not (40 columns, mostly).
    {
#ifdef BBS_BOARD_TAG
        snprintf(val, sizeof(val), "%s %s", BBS_BOARD_TAG, BBS_BOARD_VERSION);
        const char* name = BBS_BOARD_NAME;
#else
        snprintf(val, sizeof(val), "ESP32");
        const char* name = "(reference)";
#endif
        const bool beside = fits(val, name);
        if (here()) { statRow(s, "Board", val, Color::White, beside ? name : nullptr); return true; }
        if (!beside && here()) { under(Color::DarkGrey, name); return true; }
    }

    // What it has, word-wrapped under its label at the width.
    {
        char caps[160], line[96];
        capabilities(caps, sizeof(caps), (s.hwCaps & HC_TAKEN) ? s.hwCaps : capsNow());
        const uint8_t room = w > kValueCol + 10 ? static_cast<uint8_t>(w - kValueCol) : 10;
        const char* p = caps;
        bool first = true;
        while (const char* next = wrap(p, line, sizeof(line), room)) {
            if (here()) {
                if (first) {
                    uint8_t col = 0;
                    rowSeg(s, Color::Grey, "Capabilities ", col);
                    rowSeg(s, Color::White, line, col);
                    rowEnd(s, col);
                } else {
                    under(Color::White, line);
                }
                return true;
            }
            first = false;
            p = next;
        }
    }

    // Staff: the live figures. Counters, all of them.
    if (!staff) return false;
    if (ci.psramHeap && here()) { statNum(s, "PSRAM free", ci.psramFree, "bytes"); return true; }
    if (!inSys) {                                   // SYS has these under "memory"
        if (here()) {
            if (ci.heapFree) statNum(s, "Heap free", ci.heapFree, "bytes, internal");
            else             statRow(s, "Heap free", "-", Color::DarkGrey, "host build");
            return true;
        }
        if (here()) {
            if (ci.heapFree) statNum(s, "Heap low", ci.heapLow, "since boot");
            else             statRow(s, "Heap low", "-", Color::DarkGrey);
            return true;
        }
    }
    if (cardMounted() && here()) {
        // The kept figure (core/space.h, 1.1.2), marked as one: measured on
        // the runner, since asking the card's FAT here could stop the board.
        // The total with its comma and its unit (1.1.2, from the bench:
        // "29,537 MB of 29539" read as two different kinds of number).
        const space::Fig c = space::get(plat::PART_CARD);
        char tot[16];
        fmtCommas(static_cast<uint32_t>(c.total / (1024ull * 1024ull)), tot, sizeof(tot));
        snprintf(note, sizeof(note), "MB of %s MB", tot);
        statKept(s, "Card free", c.valid,
                 static_cast<uint32_t>((c.total > c.used ? c.total - c.used : 0) / (1024ull * 1024ull)),
                 c.valid ? note : "MB");
        return true;
    }
    // What the "." means, under it: SYS says it once under its storage.
    if (cardMounted() && !inSys && here()) {
        keptNote(s, "MEM FORCE");
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// rowHardware: the HARDWARE list. A title, the section, a rule. The title
// carries the version the board shows everywhere else.
// ---------------------------------------------------------------------------
void Bbs::hwSnap(Session& s) {
    s.hwCaps = capsNow();
}

bool Bbs::rowHardware(Session& s) {
    constexpr uint8_t kDone = 200;                  // past the rule
    uint8_t i = s.listIdx++;
    if (i == 0) {
        hwSnap(s);                                  // one snapshot for the listing
        rowTitle(s, "Hardware", BBS_VERSION_SHOWN);
        return true;
    }
    if (i >= kDone) return false;
    if (hwRow(s, static_cast<uint8_t>(i - 1), false)) return true;
    rowRule(s);
    s.listIdx = kDone;
    return true;
}
