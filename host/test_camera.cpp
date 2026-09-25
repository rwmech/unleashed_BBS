/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         host/test_camera.cpp
 * Module:       Host tests / the camera's rules and watermark (1.1.0)
 *
 * Purpose:      Every rule the camera decides, at its boundaries, with no
 *               sensor and no card: the names it writes and the names it
 *               will ever count or remove, the per caller limits and when
 *               the next photo is allowed, retention by age, by count and
 *               by the card's floor, the timelapse's schedule, whether the
 *               download is offered, the JPEG comment, and the watermark
 *               drawn into a strip of rows.
 *
 * Targets:      Linux host build (make test)
 * See also:     src/plugins/camera_rules.h, src/plugins/camera_mark.h
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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../src/plugins/camera_rules.h"
#include "../src/plugins/camera_mark.h"

using namespace camrules;

static int fails = 0, passes = 0;

static void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passes; else ++fails;
}

static struct tm at(int y, int mo, int d, int h, int mi, int s) {
    struct tm t = {};
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = h; t.tm_min = mi; t.tm_sec = s;
    return t;
}

static bool outCollect(void* ctx, const uint8_t* p, size_t n) {
    static_cast<std::vector<uint8_t>*>(ctx)->insert(static_cast<std::vector<uint8_t>*>(ctx)->end(), p, p + n);
    return true;
}

int main() {
    setenv("TZ", "UTC0", 1);
    tzset();

    printf("Names\n");
    char out[128];
    struct tm t = at(2026, 9, 24, 17, 12, 4);
    callerName(NAME_DATE, t, "quantumrob", false, out, sizeof(out));
    check("date: SNAP-20260924-171204.JPG", !strcmp(out, "SNAP-20260924-171204.JPG"));
    callerName(NAME_HANDLE, t, "quantumrob", false, out, sizeof(out));
    check("date + handle: SNAP-20260924-171204-quantumrob.JPG", !strcmp(out, "SNAP-20260924-171204-quantumrob.JPG"));
    callerName(NAME_FOLDER, t, "quantumrob", false, out, sizeof(out));
    check("by handle: quantumrob/SNAP-20260924-171204.JPG", !strcmp(out, "quantumrob/SNAP-20260924-171204.JPG"));
    callerName(NAME_FOLDER, t, "Visitor", true, out, sizeof(out));
    check("a guest's folder is guest-Visitor, never an account's", !strcmp(out, "guest-Visitor/SNAP-20260924-171204.JPG"));
    systemName("timelapse", "TL", at(2026, 9, 24, 17, 12, 0), out, sizeof(out));
    check("a system snap: timelapse/TL-20260924-171200.JPG", !strcmp(out, "timelapse/TL-20260924-171200.JPG"));
    safeHandle("a/b\\c:d*e?f\"g<h>i|j", false, out, sizeof(out));
    check("FAT's refused characters dropped", !strcmp(out, "abcdefghij"));
    safeHandle(" ..Dot Man.. ", false, out, sizeof(out));
    check("spaces and dots trimmed from both ends, the case kept", !strcmp(out, "Dot Man"));
    safeHandle("...", false, out, sizeof(out));
    check("nothing left is 'caller'", !strcmp(out, "caller"));
    safeHandle("ABCDEFGHIJKLMNOPQRSTUVWXYZ", true, out, sizeof(out));
    check("a long guest handle is cut to 19 with its prefix", strlen(out) == kHandleMax && !strncmp(out, "guest-", 6));
    callerName(NAME_HANDLE, t, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", true, out, sizeof(out));
    check("the longest name the camera writes is 44 characters", strlen(out) == 44);
    callerName(NAME_FOLDER, t, "TimeLapse", false, out, sizeof(out));
    check("a caller called TimeLapse never shares the board's folder", !strncmp(out, "user-TimeLapse/", 15));

    printf("Only the camera's own names are counted or removed\n");
    uint64_t k = 0;
    check("SNAP-20260924-171204.JPG is", nameKey("SNAP-20260924-171204.JPG", "SNAP", true, k) && k == 20260924171204ull);
    check("with a handle, where handles are allowed", nameKey("SNAP-20260924-171204-bob.JPG", "SNAP", true, k));
    check("but not in a handle folder, where they never are", !nameKey("SNAP-20260924-171204-bob.JPG", "SNAP", false, k));
    check("a sysop's garden.jpg is not", !nameKey("garden.jpg", "SNAP", true, k));
    check("nor SNAP-holiday.JPG", !nameKey("SNAP-holiday.JPG", "SNAP", true, k));
    check("nor a 13th month", !nameKey("SNAP-20261324-171204.JPG", "SNAP", true, k));
    check("nor 25 o'clock", !nameKey("SNAP-20260924-251204.JPG", "SNAP", true, k));
    check("nor a .png", !nameKey("SNAP-20260924-171204.png", "SNAP", true, k));
    check("nor the temporary name", !nameKey(kTmpName, "SNAP", true, k));
    check("a TL name under TL", nameKey("TL-20260924-171200.JPG", "TL", false, k));
    check("but not under SNAP", !nameKey("TL-20260924-171200.JPG", "SNAP", true, k));
    check("lower-case .jpg is taken (a card copied on a laptop)", nameKey("SNAP-20260924-171204.jpg", "SNAP", true, k));

    printf("Limits: 10 an hour, 20 a day, rolling\n");
    Window w;
    const uint32_t T0 = 1790000000u;
    for (int i = 0; i < 10; ++i) {
        Verdict v = check(w, T0 + i * 60);
        if (!v.ok || v.hour != i) { ::check("the first ten in an hour are allowed", false); break; }
        record(w, T0 + i * 60);
    }
    Verdict v = check(w, T0 + 600);
    ::check("the eleventh in the hour is refused", !v.ok && !v.byDay && v.hour == 10);
    ::check("and is allowed an hour after the first", v.nextAt == T0 + 3600);
    v = check(w, T0 + 3600);
    ::check("which it is, to the second", v.ok && v.hour == 9);
    for (int i = 0; i < 10; ++i) record(w, T0 + 7200 + i * 60);
    v = check(w, T0 + 7200 + 600);
    ::check("twenty in the day: refused by the day", !v.ok && v.byDay && v.day == 20);
    ::check("until a day after the day's first", v.nextAt == T0 + 86400);
    v = check(w, T0 + 86400 + 1);
    ::check("then the day's window has room again", v.ok && v.day == 19);
    Window w2;
    for (int i = 0; i < 10; ++i) record(w2, T0 + i);
    v = check(w2, T0 + 30);
    ::check("\"the next one is allowed at\" is the oldest in the window plus the hour",
            !v.ok && v.nextAt == T0 + 3600);

    printf("Retention\n");
    auto items = [](std::vector<Item> v) { sortByKey(v.data(), v.size()); return v; };
    Policy pol[2] = { { 30, 0 }, { 7, 0 } };
    time_t now = mktime(new struct tm(at(2026, 9, 24, 12, 0, 0)));
    uint64_t cut[2] = { cutoffKey(now, 30), cutoffKey(now, 7) };
    std::vector<Item> it = items({
        { 20260801120000ull, 100, 0, false },       // caller, 54 days
        { 20260920120000ull, 100, 0, false },       // caller, 4 days
        { 20260910120000ull, 100, 1, false },       // timelapse, 14 days
        { 20260923120000ull, 100, 1, false },       // timelapse, 1 day
    });
    bool met = choose(it.data(), it.size(), pol, cut, 2, 1ull << 30, 1ull << 20);
    ::check("by age: each group its own days", met && it[0].del && !it[3].del &&
            it[1].del /* the 14 day timelapse */ && !it[2].del);
    Policy pol2[2] = { { 0, 2 }, { 0, 1 } };
    it = items({ { 1, 1, 0 }, { 2, 1, 0 }, { 3, 1, 0 }, { 4, 1, 1 }, { 5, 1, 1 }, { 6, 1, 1 } });
    uint64_t nocut[2] = { 0, 0 };
    choose(it.data(), it.size(), pol2, nocut, 2, 1ull << 30, 0);
    int callersLeft = 0, tlLeft = 0;
    for (auto& x : it) if (!x.del) (x.group ? tlLeft : callersLeft)++;
    ::check("by count: callers keep 2 and the timelapse 1, the oldest going", callersLeft == 2 && tlLeft == 1 &&
            it[0].del && !it[1].del && !it[2].del && it[3].del && it[4].del && !it[5].del);
    ::check("a fast timelapse never takes a caller's photo", !it[1].del && !it[2].del);
    Policy none[2] = { { 0, 0 }, { 0, 0 } };
    it = items({ { 1, 400, 0 }, { 2, 300, 1 }, { 3, 300, 1 }, { 4, 200, 0 } });
    met = choose(it.data(), it.size(), none, nocut, 2, 100, 700);
    ::check("under the floor: the timelapse goes first, oldest first", met && it[1].del && it[2].del && !it[0].del && !it[3].del);
    it = items({ { 1, 400, 0 }, { 2, 300, 1 }, { 3, 300, 1 }, { 4, 200, 0 } });
    met = choose(it.data(), it.size(), none, nocut, 2, 100, 1100);
    ::check("then the callers', oldest first", met && it[0].del && it[1].del && it[2].del && !it[3].del);
    it = items({ { 1, 10, 0 } });
    met = choose(it.data(), it.size(), none, nocut, 2, 100, 100000);
    ::check("and when Photos cannot make the room, it says so", !met);
    ::check("and takes nothing for it", !it[0].del);
    ::check("the floor: a tenth of an 8 GB card, 512 MB at most", floorBytes(8ull << 30, -1) == 512ull << 20);
    ::check("a tenth of a 1 GB card", floorBytes(1ull << 30, -1) == (1ull << 30) / 10);
    ::check("or the sysop's own figure", floorBytes(8ull << 30, 100) == 100ull << 20);

    printf("Timelapse\n");
    uint32_t slot = 0;
    bool primed = false;
    ::check("the first look notes the slot and takes nothing", !tlDue(3600 * 5 + 10, 60, slot, primed));
    ::check("nothing inside the same minute", !tlDue(3600 * 5 + 59, 60, slot, primed));
    ::check("the next minute is due", tlDue(3600 * 5 + 60, 60, slot, primed));
    ::check("once", !tlDue(3600 * 5 + 61, 60, slot, primed));
    ::check("every 86,400 s falls at midnight", !tlDue(86399, 86400, slot, primed = false) && tlDue(86400, 86400, slot, primed));
    ::check("0 is off", !tlDue(99999, 0, slot, primed));

    printf("The download offer\n");
    ::check("asked of a caller still there, allowed, engine free", offerFor(true, true, true, false) == Offer::Ask);
    ::check("not asked of one allowed to snap but not to download", offerFor(true, true, false, false) == Offer::NotAllowed);
    ::check("nor of one who hung up (the photo is still saved)", offerFor(true, false, true, false) == Offer::Gone);
    ::check("nor while somebody else is transferring", offerFor(true, true, true, true) == Offer::Busy);
    ::check("nor when nothing was saved", offerFor(false, true, true, false) == Offer::Failed);

    printf("JPEG\n");
    uint8_t good[] = { 0xFF, 0xD8, 1, 2, 3, 0xFF, 0xD9, 0, 0, 0 };
    size_t n = sizeof(good);
    ::check("a whole frame, trimmed to its end marker", jpegWhole(good, n) && n == 7);
    uint8_t cut2[] = { 0xFF, 0xD8, 1, 2, 3, 4 };
    n = sizeof(cut2);
    ::check("a frame cut short is not whole", !jpegWhole(cut2, n));
    uint8_t nosoi[] = { 0, 0xD8, 0xFF, 0xD9 };
    n = sizeof(nosoi);
    ::check("nor one with no start", !jpegWhole(nosoi, n));
    std::vector<uint8_t> got;
    ComSink sink(outCollect, &got, "Board: 2026-09-24 17:12:04, snapped by rob");
    sink.put(good, 1);                               // the SOI arrives a byte at a time
    sink.put(good + 1, 6);
    const size_t text = strlen("Board: 2026-09-24 17:12:04, snapped by rob");
    ::check("the comment goes straight after SOI", got.size() == 7 + 4 + text && got[0] == 0xFF && got[1] == 0xD8 &&
            got[2] == 0xFF && got[3] == 0xFE && got[4] == 0 && got[5] == text + 2 &&
            !memcmp(&got[6], "Board:", 6) && got[6 + text] == 1);
    ::check("and the picture follows it untouched", got.back() == 0xD9 && sink.total() == got.size());

    printf("The watermark\n");
    char line[96];
    cammark::fitText("The Rusty Antenna", "2026-09-24 17:12", "quantumrob", 60, line, sizeof(line));
    ::check("board, date and handle, with the dots", !strcmp(line, "The Rusty Antenna \xB7 2026-09-24 17:12 \xB7 quantumrob"));
    cammark::fitText("The Rusty Antenna", "2026-09-24 17:12", "quantumrob", 38, line, sizeof(line));
    ::check("short of room the board's name gives way first", strlen(line) <= 38 &&
            strstr(line, "2026-09-24 17:12 \xB7 quantumrob") && !strncmp(line, "The Ru", 6));
    cammark::fitText("The Rusty Antenna", "2026-09-24 17:12", "quantumrob", 20, line, sizeof(line));
    ::check("then the handle", strlen(line) <= 20 && !strncmp(line, "2026-09-24 17:12", 16));
    ::check("about 2.5% of the height: scale 2 at UXGA, 1 at SVGA", cammark::scaleFor(1200) == 2 && cammark::scaleFor(600) == 1);
    const int W = 800, H = 600;
    cammark::Box b = cammark::layout(W, H, "AB");
    ::check("in the bottom right corner, inside the picture", b.x > W / 2 && b.y > H / 2 &&
            b.x + b.w + b.scale <= W && b.y + b.h + b.scale <= H);
    std::vector<uint8_t> rgb(static_cast<size_t>(W) * H * 3, 128);
    std::vector<uint8_t> before = rgb;
    for (int y0 = 0; y0 < H; y0 += 8)                // a strip at a time, as the encoder feeds it
        cammark::drawRows(&rgb[static_cast<size_t>(y0) * W * 3], W, y0, 8, b, "AB");
    long light = 0, dark = 0, outside = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const uint8_t* p = &rgb[(static_cast<size_t>(y) * W + x) * 3];
            if (p[0] == 128) continue;
            bool in = x >= b.x - b.scale && x < b.x + b.w + b.scale && y >= b.y - b.scale && y < b.y + b.h + b.scale;
            if (!in) ++outside;
            if (p[0] == cammark::kLight[0]) ++light;
            else if (p[0] == cammark::kDark[0]) ++dark;
        }
    ::check("light text drawn", light > 20);
    ::check("with its dark outline", dark > light / 2);
    ::check("and not one pixel outside the corner touched", outside == 0);
    long covered = light + dark;
    ::check("covering well under 1% of the picture", covered * 100 < static_cast<long>(W) * H);

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
