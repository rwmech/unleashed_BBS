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
#include "../src/plugins/camera_pic.h"

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
            // The wordmark's own corner (1.1.1), bottom left, is the other
            // place the watermark may touch; its pixels are neither light nor
            // dark, so they count towards neither.
            const bool mark = b.brand && x >= b.bx && x < b.bx + cammark::kBrandW * b.bs && y >= b.by &&
                              y < b.by + cammark::kBrandH * b.bs;
            if (mark) continue;
            if (!in) ++outside;
            if (p[0] == cammark::kLight[0]) ++light;
            else if (p[0] == cammark::kDark[0]) ++dark;
        }
    ::check("light text drawn", light > 20);
    ::check("with its dark outline", dark > light / 2);
    ::check("and not one pixel outside the corner touched", outside == 0);
    long covered = light + dark;
    ::check("covering well under 1% of the picture", covered * 100 < static_cast<long>(W) * H);

    printf("The wordmark\n");
    ::check("the site's wordmark: 62 x 12", cammark::kBrandW == 62 && cammark::kBrandH == 12);
    ::check("its micro sign has a descender (rows 10 and 11, column 0)",
            cammark::brandLit(0, 10) && cammark::brandLit(0, 11) && !cammark::brandLit(3, 11));
    {
        struct Sz { int w, h; } sizes[] = { {320, 240}, {640, 480}, {800, 600}, {1024, 768},
                                            {1280, 720}, {1280, 1024}, {1600, 1200}, {2048, 1536} };
        const char* text = "PixelBBS \xB7 2026-09-25 14:34 \xB7 CamTester";
        bool noOverlap = true, shownAbove = true, height = true;
        for (const Sz& z : sizes) {
            char fit[96];
            int max = cammark::maxGlyphs(z.w, z.h);
            cammark::fitText("PixelBBS", "2026-09-25 14:34", "CamTester", max, fit, sizeof(fit));
            (void)text;
            cammark::Box bx = cammark::layout(z.w, z.h, fit);
            if (bx.brand && bx.bx + cammark::kBrandW * bx.bs > bx.x - bx.scale) noOverlap = false;
            if (z.w >= 640 && !bx.brand) shownAbove = false;
            int bh = cammark::kBrandH * bx.bs, th = bx.h;
            if (bx.brand && (bh * 4 < th * 3 || bh * 3 > th * 4)) height = false;
        }
        ::check("never overlapping the text, at any size", noOverlap);
        ::check("shown at VGA and every size above", shownAbove);
        ::check("within a quarter of the text's height", height);
        cammark::Box q = cammark::layout(320, 240, "PixelBBS \xB7 2026-09-25 14:34 \xB7 CamTester");
        ::check("left out at QVGA when the text fills the width", !q.brand);
        cammark::Box q2 = cammark::layout(320, 240, "2026-09-25 14:34");
        ::check("kept at QVGA when there is room", q2.brand);

        // Blended, white at kBrandAlpha: a lit pixel is lifted towards white,
        // an unlit one untouched, and nothing outside the mark changes.
        const int VW = 640, VH = 480;
        cammark::Box v = cammark::layout(VW, VH, "2026-09-25 14:34");
        std::vector<uint8_t> px(static_cast<size_t>(VW) * VH * 3, 100);
        for (int y0 = 0; y0 < VH; y0 += 16)
            cammark::drawRows(&px[static_cast<size_t>(y0) * VW * 3], VW, y0, 16, v, "2026-09-25 14:34");
        long lifted = 0, other = 0;
        for (int y = 0; y < VH; ++y)
            for (int x = 0; x < VW / 2; ++x) {
                uint8_t c = px[(static_cast<size_t>(y) * VW + x) * 3];
                if (c == 100) continue;
                if (c == 100 + ((155 * cammark::kBrandAlpha) >> 8)) ++lifted; else ++other;
            }
        ::check("the wordmark is drawn at VGA, blended, and nothing else on its side", lifted > 100 && other == 0);
    }

    // ---------------------------------------------------------------------
    // The picture (1.1.1, camera_pic.h)
    // ---------------------------------------------------------------------
    printf("Sizes by sensor\n");
    ::check("a GC0308 (640x480) gives qvga and vga", campic::sizeCountFor(640, 480, 0) == 2);
    ::check("an OV2640 (1600x1200) gives qvga to uxga, seven", campic::sizeCountFor(1600, 1200, 0) == 7);
    ::check("an OV3660 (2048x1536) all eight", campic::sizeCountFor(2048, 1536, 0) == 8);
    ::check("no sensor yet: the fallback", campic::sizeCountFor(0, 0, 2) == 2);
    ::check("a sensor smaller than qvga still offers one size", campic::sizeCountFor(160, 120, 0) == 1);
    char sl[48];
    campic::sizeList(2, sl, sizeof(sl));
    ::check("the GC0308's list is qvga|vga", !strcmp(sl, "qvga|vga"));
    campic::sizeList(7, sl, sizeof(sl));
    ::check("the OV2640's list ends at uxga", !strcmp(sl, "qvga|vga|svga|xga|hd|sxga|uxga"));
    campic::sizeList(8, sl, sizeof(sl));
    ::check("the whole list fits CONFIG's buffer", !strcmp(sl, campic::kAllSizes));
    char tiny[8];
    campic::sizeList(7, tiny, sizeof(tiny));
    ::check("a short buffer stops at a whole word", !strcmp(tiny, "qvga"));
    ::check("a saved uxga on a GC0308 clamps to vga", campic::clampSize(6, 2) == 1);
    ::check("a saved vga on a GC0308 stays vga", campic::clampSize(1, 2) == 1);
    ::check("a saved uxga on an OV2640 stays uxga", campic::clampSize(6, 7) == 6);
    ::check("the words and the table agree",
            !strcmp(campic::kSizes[1].word, "vga") && campic::kSizes[6].w == 1600 && campic::kSizes[6].h == 1200);

    printf("GC0308 registers\n");
    campic::Reg rg[6];
    ::check("six writes, page 0 first", campic::gc0308Regs(0, 0, 0, 0, rg) == 6 && rg[0].reg == 0xFE && rg[0].val == 0);
    ::check("all at 0: the datasheet's defaults (0xB5 0, 0xB3 0x40, 0xD3 0x48)",
            rg[1].reg == 0xB5 && rg[1].val == 0x00 && rg[2].reg == 0xB3 && rg[2].val == 0x40 &&
            rg[3].val == 0x40 && rg[4].val == 0x40 && rg[5].reg == 0xD3 && rg[5].val == 0x48);
    campic::gc0308Regs(-2, 2, -2, -2, rg);
    ::check("brightness -2 is a signed -32 (0xE0)", rg[1].val == 0xE0);
    ::check("contrast 2 is 1.5 (0x60)", rg[2].val == 0x60);
    ::check("saturation -2 is 0x20 on Cb and Cr", rg[3].reg == 0xB1 && rg[3].val == 0x20 && rg[4].reg == 0xB2 && rg[4].val == 0x20);
    ::check("exposure -2 aims the AEC at 48", rg[5].val == 48);
    campic::gc0308Regs(2, -2, 2, 2, rg);
    ::check("brightness 2 is +32, exposure 2 aims at 96", rg[1].val == 0x20 && rg[5].val == 96 && rg[2].val == 0x20);
    campic::gc0308Regs(9, -9, 9, -9, rg);
    ::check("out of range is held at -2..2", rg[1].val == 0x20 && rg[2].val == 0x20 && rg[5].val == 48);

    printf("Exposure settling\n");
    {
        campic::Settle st;
        const uint8_t outdoor[] = { 250, 240, 200, 150, 110, 84, 76, 74, 73 };
        int at = -1;
        for (int i = 0; i < 9; ++i)
            if (campic::settled(st, outdoor[i], 72, 100u * i)) { at = i; break; }
        ::check("a white start is followed down, and settles near the target", at >= 6 && at <= 8);
        campic::Settle s2;
        bool early = false;
        for (int i = 0; i < 6; ++i) early |= campic::settled(s2, 250, 72, 100u * i);
        ::check("stuck white is not settled before the AEC has had time", !early);
        ::check("stuck for long enough is a limit, and settled", campic::settled(s2, 250, 72, 1300));
        campic::Settle s3;
        ::check("two readings are never enough", !campic::settled(s3, 72, 72, 0) && !campic::settled(s3, 72, 72, 5000));
        ::check("the third, still and on target, is", campic::settled(s3, 72, 72, 200));
    }

    printf("OV2640 steady\n");
    {
        campic::Steady sd;
        const uint16_t e[] = { 1200, 600, 300, 180, 150, 149, 150, 150 };
        const uint16_t g[] = { 30, 20, 12, 8, 6, 6, 6, 6 };
        int at = -1;
        for (int i = 0; i < 8; ++i) if (campic::steady(sd, e[i], g[i])) { at = i; break; }
        ::check("a falling exposure is followed until it holds still", at == 6);
        campic::Steady s2;
        bool early = false;
        for (int i = 0; i < 3; ++i) early |= campic::steady(s2, 500, 10);
        ::check("never before four frames, however still", !early && campic::steady(s2, 500, 10));
        campic::Steady s3;
        bool any = false;
        for (int i = 0; i < 10; ++i) any |= campic::steady(s3, static_cast<uint16_t>(i % 2 ? 400 : 440), 10);
        ::check("a hunting exposure (10% either way) is not steady", !any);
        campic::Steady s4;
        for (int i = 0; i < 4; ++i) campic::steady(s4, static_cast<uint16_t>(1000 + i * 20), 10);
        ::check("within 3% is steady", campic::steady(s4, 1080, 10));
    }
    ::check("the re-encode quality: 90 at the shipped 10, never under 60 or over 95",
            campic::reencodeQuality(10) == 90 && campic::reencodeQuality(40) == 60 &&
            campic::reencodeQuality(4) == 95 && campic::reencodeQuality(12) == 88);

    printf("Levels and gamma\n");
    {
        uint8_t lut[3][256], lo[3], hi[3];
        campic::buildTables(nullptr, false, 10, lut, lo, hi);
        ::check("off and gamma 1.0: the identity", campic::identity(lut));
        campic::buildTables(nullptr, true, 10, lut, lo, hi);
        ::check("levels with no histogram: the identity", campic::identity(lut));

        // A washed-out frame, as the bench's first outdoor photo: every
        // channel between 150 and 250.
        static campic::Hist h;
        memset(&h, 0, sizeof(h));
        for (int v = 150; v <= 250; ++v)
            for (int c = 0; c < 3; ++c) h.c[c][v] = 10;
        h.n = 101 * 10;
        campic::buildTables(&h, true, 10, lut, lo, hi);
        ::check("black and white points near the ends of the data", lo[0] >= 150 && lo[0] <= 152 && hi[0] >= 248 && hi[0] <= 250);
        ::check("stretched: 150 goes to black, 250 to white", lut[0][150] == 0 && lut[0][250] == 255 && lut[1][100] == 0);
        ::check("and the middle to the middle", lut[0][200] > 120 && lut[0][200] < 135);
        ::check("monotonic", [&] { for (int v = 1; v < 256; ++v) if (lut[0][v] < lut[0][v - 1]) return false; return true; }());
        ::check("and it changes the picture", !campic::identity(lut));

        // Gamma alone: 0.5 of the way up.
        campic::buildTables(nullptr, false, 8, lut, lo, hi);
        ::check("gamma 0.8 darkens the middle, keeps the ends", lut[1][128] < 128 && lut[1][0] == 0 && lut[1][255] == 255);
        campic::buildTables(nullptr, false, 14, lut, lo, hi);
        ::check("gamma 1.4 lightens it", lut[1][128] > 128);
        ::check("the words and the tenths agree", campic::gammaTenths(campic::kGammaNone) == 10 &&
                campic::gammaTenths(0) == 6 && campic::gammaTenths(8) == 16 && campic::gammaTenths(99) == 10);

        // A flat frame is not stretched to noise.
        memset(&h, 0, sizeof(h));
        for (int c = 0; c < 3; ++c) h.c[c][100] = 1000;
        h.n = 1000;
        campic::buildTables(&h, true, 10, lut, lo, hi);
        ::check("a flat frame keeps at least a 48 wide window", hi[0] - lo[0] >= 48);

        // A cast: blue sits 60 above red and green. The cast is taken out
        // only as far as kCastSpan allows.
        memset(&h, 0, sizeof(h));
        for (int v = 20; v <= 180; ++v) { h.c[0][v] = 5; h.c[1][v] = 5; }
        for (int v = 80; v <= 240; ++v) h.c[2][v] = 5;
        h.n = 161 * 5;
        campic::buildTables(&h, true, 10, lut, lo, hi);
        ::check("each channel's points held near the common ones",
                lo[2] <= lo[0] + 2 * campic::kCastSpan && hi[0] + 2 * campic::kCastSpan >= hi[2]);

        // apply over RGB888, in place.
        uint8_t px[6] = { 150, 200, 250, 0, 128, 255 };
        memset(&h, 0, sizeof(h));
        for (int v = 150; v <= 250; ++v) for (int c = 0; c < 3; ++c) h.c[c][v] = 10;
        h.n = 1010;
        campic::buildTables(&h, true, 10, lut, lo, hi);
        campic::apply(lut, px, 2);
        ::check("apply uses each channel's table", px[0] == 0 && px[2] == 255 && px[3] == 0 && px[5] == 255);
    }

    printf("Histograms\n");
    {
        // RGB565, high byte first: pure red, pure green, pure blue, white.
        const uint8_t f[] = { 0xF8, 0x00, 0x07, 0xE0, 0x00, 0x1F, 0xFF, 0xFF };
        campic::Hist h;
        campic::histRgb565(f, 4, 1, 1, h);
        ::check("four pixels counted", h.n == 4);
        ::check("red lands on 248, green's 252, blue's 248",
                h.c[0][248] == 2 && h.c[1][252] == 2 && h.c[2][248] == 2 && h.c[0][0] == 2);
        std::vector<uint8_t> big(64u * 32u * 2u, 0);
        campic::histRgb565(big.data(), 64, 32, 2, h);
        ::check("every second pixel of every second row", h.n == 32u * 16u);
        const uint8_t rgb[] = { 1, 2, 3, 1, 2, 4 };
        memset(&h, 0, sizeof(h));
        campic::histRgb888(rgb, 2, h);
        ::check("RGB888 counted by channel", h.n == 2 && h.c[0][1] == 2 && h.c[2][3] == 1 && h.c[2][4] == 1);
    }

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
