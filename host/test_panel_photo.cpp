/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         host/test_panel_photo.cpp
 * Module:       Host tests / the panel's new-photo show (1.2.1)
 *
 * Purpose:      The pure half of the new-photo show
 *               (src/plugins/panel_photo_fit.h), with no panel and no board:
 *                 - the frame on every glass a board has (172x320 and its
 *                   landscape turn, 240x320, 400x240, 480x320 and 320x480,
 *                   480x480): the picture never cropped and never enlarged,
 *                   one side reaching its box, every box on the glass, the
 *                   gallery spec's and the square spec's own figures;
 *                 - the decoder's scale for VGA, XGA, HD, UXGA and
 *                   2592x1944: the largest of 1/1 to 1/8 still at least the
 *                   drawn size;
 *                 - the caption's time from a photo's name, and its words cut
 *                   in the spec's order (the handle to 6, the handle, then
 *                   the name; the number and the time never);
 *                 - the scaler against the host's TJpgDec at every scale it
 *                   runs at: every picture row finished, each pixel the
 *                   average of exactly the decoded pixels that land on it,
 *                   and a called-off or stepped-aside decode stopping.
 *
 * Libraries:    host/tjpgd (ChaN's TJpgDec R0.03)
 * Targets:      Linux host (make test)
 * See also:     src/plugins/panel_photo_fit.h, host/photos/mkfixtures.py
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
#include "plugins/panel_photo_fit.h"
#include "tjpgd/tjpgd.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace photoshow;
using panelgfx::Rect;

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* name, bool ok) {
    std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", name);
    ok ? ++g_pass : ++g_fail;
}

bool inside(const Rect& r, int w, int h) { return r.x >= 0 && r.y >= 0 && r.x + r.w <= w && r.y + r.h <= h; }
bool overlap(const Rect& a, const Rect& b) {
    return !panelgfx::empty(a) && !panelgfx::empty(b) && a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}
bool sameRect(const Rect& r, int x, int y, int w, int h) { return r.x == x && r.y == y && r.w == w && r.h == h; }

struct Glass { uint16_t w, h; const char* name; };
const Glass kGlass[] = {
    { 172, 320, "the LCD-1.47 portrait" }, { 320, 172, "the LCD-1.47 landscape" },
    { 240, 320, "the Touch-LCD-2" },       { 400, 240, "the 4.3B as drawn" },
    { 480, 320, "the Makerfabs" },         { 320, 480, "the Makerfabs portrait" },
    { 480, 480, "the square 4848" },
};
struct Pic { uint16_t w, h; const char* name; };
const Pic kPics[] = {
    { 640, 480, "VGA" }, { 1024, 768, "XGA" }, { 1280, 720, "HD" }, { 1600, 1200, "UXGA" },
    { 2592, 1944, "2592x1944" }, { 480, 640, "an upright 3:4" }, { 600, 600, "a square" }, { 100, 80, "a small one" },
};

// ---------------------------------------------------------------------------
// The frame
// ---------------------------------------------------------------------------
void runFit() {
    std::printf("The frame, on every glass\n");
    bool rule = true, scale = true, boxes = true, reach = true;
    char what[160] = "";
    for (const Glass& g : kGlass)
        for (const Pic& p : kPics) {
            Frame f;
            if (!fit(g.w, g.h, p.w, p.h, f)) { rule = false; snprintf(what, sizeof(what), "%s on %s: no frame", p.name, g.name); continue; }
            int bw = 0, bh = 0;
            box(g.w, g.h, bw, bh);
            // Never cropped, never enlarged, the shape kept to a pixel.
            const bool ok = f.pw <= bw && f.ph <= bh && f.pw <= p.w && f.ph <= p.h &&
                            (abs(static_cast<int>(f.pw) - static_cast<int>(static_cast<uint32_t>(p.w) * f.ph / p.h)) <= 1 ||
                             abs(static_cast<int>(f.ph) - static_cast<int>(static_cast<uint32_t>(p.h) * f.pw / p.w)) <= 1);
            if (!ok) { rule = false; snprintf(what, sizeof(what), "%s on %s: %ux%u in %dx%d", p.name, g.name, f.pw, f.ph, bw, bh); }
            // As large as it can be: a side reaches its box, or it is drawn whole.
            if (!(f.pw == bw || f.ph == bh || (f.pw == p.w && f.ph == p.h))) {
                reach = false; snprintf(what, sizeof(what), "%s on %s: %ux%u reaches no side", p.name, g.name, f.pw, f.ph);
            }
            // The scale: at least the drawn size, and the next one down is not.
            const uint8_t s = f.scale;
            const bool big = (p.w >> s) >= f.pw && (p.h >> s) >= f.ph;
            const bool most = s == 3 || (p.w >> (s + 1)) < f.pw || (p.h >> (s + 1)) < f.ph;
            if (!big || !most) { scale = false; snprintf(what, sizeof(what), "%s on %s: 1/%u", p.name, g.name, 1u << s); }
            // Every box on the glass, the picture inside its keyline, nothing
            // over the picture.
            const bool in = inside(f.title, g.w, g.h) && inside(f.strip, g.w, g.h) && inside(f.key, g.w, g.h) &&
                            inside(f.cap1, g.w, g.h) && (panelgfx::empty(f.cap2) || inside(f.cap2, g.w, g.h)) &&
                            f.pic.x == f.key.x + 1 && f.pic.y == f.key.y + 1 && f.pic.w == f.pw && f.pic.h == f.ph &&
                            f.key.w == f.pw + 2 && f.key.h == f.ph + 2 && f.key.y >= 24 &&
                            !overlap(f.key, f.cap1) && !overlap(f.key, f.cap2) && !overlap(f.cap1, f.cap2) &&
                            !overlap(f.key, f.strip);
            if (!in) { boxes = false; snprintf(what, sizeof(what), "%s on %s: a box off the glass or over another", p.name, g.name); }
        }
    check("never cropped, never enlarged, the shape kept, on every glass", rule);
    check("one side reaches its box, or the photo is drawn whole", reach);
    check("decoded at the largest scale still at least the drawn size", scale);
    check("every box on the glass, the picture in its keyline, nothing over it", boxes);
    if (what[0]) std::printf("        first wrong: %s\n", what);

    // The specs' own figures (the gallery spec, and the square glass's).
    Frame f;
    fit(172, 320, 1024, 768, f);
    check("1.47 portrait, XGA: 162x121, keyline at 4,90, two caption rows",
          f.pw == 162 && f.ph == 121 && sameRect(f.key, 4, 90, 164, 123) && f.cap1.y + 2 == 219 && f.cap2.y + 2 == 239);
    check("and decoded at 1/4", f.scale == 2);
    fit(320, 172, 1024, 768, f);
    check("1.47 landscape: 157x118, one caption row", f.pw == 157 && f.ph == 118 && panelgfx::empty(f.cap2));
    fit(240, 320, 1600, 1200, f);
    check("Touch-LCD-2, UXGA: 230x172 at 1/4", f.pw == 230 && f.ph == 172 && f.scale == 2 && f.key.x == 4);
    fit(400, 240, 1600, 1200, f);
    check("4.3B, UXGA: 248x186, keyline at 75,28, at 1/4",
          f.pw == 248 && f.ph == 186 && sameRect(f.key, 75, 28, 250, 188) && f.scale == 2);
    fit(480, 320, 1024, 768, f);
    check("Makerfabs, XGA: 354x266, keyline 62,28 356x268, at 1/2",
          f.pw == 354 && f.ph == 266 && sameRect(f.key, 62, 28, 356, 268) && f.scale == 1);
    check("caption at the foot, text at h - 18", f.cap1.y + 2 == 320 - 18);
    fit(480, 480, 1024, 768, f);
    check("square, 4:3: 456x342, keyline 11,64 458x344, caption at y 416",
          f.pw == 456 && f.ph == 342 && sameRect(f.key, 11, 64, 458, 344) && f.cap1.y == 416);
    fit(480, 480, 1280, 720, f);
    check("square, 16:9: 456x256, keyline 11,107, caption at y 373",
          f.pw == 456 && f.ph == 256 && sameRect(f.key, 11, 107, 458, 258) && f.cap1.y == 373);
    fit(480, 480, 600, 600, f);
    check("square, 1:1: 414x414, keyline 32,28", f.pw == 414 && f.ph == 414 && sameRect(f.key, 32, 28, 416, 416));
    fit(480, 480, 480, 640, f);
    check("square, 3:4: 310x414, keyline 84,28", f.pw == 310 && f.ph == 414 && sameRect(f.key, 84, 28, 312, 416));
    fit(480, 320, 2592, 1944, f);
    check("Makerfabs, 2592x1944: at 1/4 (648x486 for 354x266)", f.scale == 2);
    fit(240, 320, 2592, 1944, f);
    check("Touch-LCD-2, 2592x1944: at 1/8 (324x243 for 230x172)", f.scale == 3);
    fit(480, 320, 100, 80, f);
    check("a photo smaller than the box is drawn at its own size, centred",
          f.pw == 100 && f.ph == 80 && f.scale == 0 && f.key.x == (480 - 102) / 2);
    check("no frame for no picture", !fit(480, 320, 0, 480, f));
    check("and none on a glass too small to frame", !fit(30, 40, 640, 480, f));
}

// ---------------------------------------------------------------------------
// The caption
// ---------------------------------------------------------------------------
void runCaption() {
    std::printf("The caption\n");
    char w[16];
    whenText("SNAP-20261001-143205.JPG", 20261001, w, sizeof(w));
    check("a snap today: today 14:32", !strcmp(w, "today 14:32"));
    whenText("SNAP-20260927-180259.JPG", 20261001, w, sizeof(w));
    check("another day: 27 Sep 18:02", !strcmp(w, "27 Sep 18:02"));
    whenText("quantumrob/SNAP-20261001-090000.JPG", 20261001, w, sizeof(w));
    check("in a handle's folder", !strcmp(w, "today 09:00"));
    whenText("SNAP-20261001-235900-daytona.JPG", 0, w, sizeof(w));
    check("a handle in the name, no clock: the date", !strcmp(w, "1 Oct 23:59"));
    whenText("timelapse/TL-20260101-000000.JPG", 20260101, w, sizeof(w));
    check("a timelapse frame at midnight", !strcmp(w, "today 00:00"));
    whenText("2026-10/garden.jpg", 20261001, w, sizeof(w));
    check("no stamp, no time", !w[0]);
    whenText("SNAP-20261341-990000.JPG", 20261001, w, sizeof(w));
    check("a stamp that is no date, no time", !w[0]);

    auto make = [](const char* num, const char* name, const char* who, const char* when) {
        Caption c;
        snprintf(c.num, sizeof(c.num), "%s", num);
        snprintf(c.name, sizeof(c.name), "%s", name);
        snprintf(c.who, sizeof(c.who), "%s", who);
        snprintf(c.when, sizeof(c.when), "%s", when);
        return c;
    };
    auto cost = [](const Caption& c, bool two) {
        int n = 0;
        if (c.num[0]) n += static_cast<int>(strlen(c.num)) + 1;
        n += static_cast<int>(strlen(c.name));
        if (c.who[0]) n += 2 + static_cast<int>(strlen(c.who));
        if (!two && c.when[0]) n += 2 + static_cast<int>(strlen(c.when));
        return n;
    };
    // The fits each glass gives (the caption's width less the icon, 8 px a glyph).
    const struct { int fit; bool two; const char* name; } kFits[] = {
        { 18, true, "the 1.47 portrait (18, two rows)" }, { 26, true, "the Touch-LCD-2 (26, two rows)" },
        { 37, false, "the 1.47 landscape (37)" }, { 46, false, "the 4.3B (46)" },
        { 54, false, "the square (54)" }, { 56, false, "the Makerfabs (56)" },
    };
    for (const auto& k : kFits) {
        Caption c = make("2", "shed_camera_16ch", "daytona_on_the_c12", "27 Sep 18:02");
        shrink(c, k.fit, k.two);
        char name[96];
        snprintf(name, sizeof(name), "%s: fits, the number and the time whole", k.name);
        check(name, cost(c, k.two) <= k.fit && !strcmp(c.num, "2") && !strcmp(c.when, "27 Sep 18:02"));
    }
    Caption c = make("2", "shed_camera_16ch", "daytona_on_the_c12", "27 Sep 18:02");
    shrink(c, 56, false);
    check("with room, nothing is cut", !strcmp(c.name, "shed_camera_16ch") && !strcmp(c.who, "daytona_on_the_c12"));
    c = make("2", "shed", "daytona_on_the_c12", "27 Sep 18:02");
    shrink(c, 30, false);
    check("the handle is cut to 6 first", !strcmp(c.who, "dayton") && !strcmp(c.name, "shed"));
    c = make("2", "shed_camera_16ch", "daytona_on_the_c12", "27 Sep 18:02");
    shrink(c, 32, false);
    check("then dropped, before the name is touched", !c.who[0] && !strcmp(c.name, "shed_camera_16ch"));
    c = make("2", "shed_camera_16ch", "daytona", "today 14:32");
    shrink(c, 18, false);
    check("then the name is cut", !c.who[0] && strlen(c.name) == 18u - 2 - 2 - 11 && !strcmp(c.when, "today 14:32"));
    c = make("", "snap", "", "today 14:32");
    shrink(c, 18, true);
    check("the kind's word with no camera, untouched", !strcmp(c.name, "snap"));
}

// ---------------------------------------------------------------------------
// The scaler, against the host's decoder
// ---------------------------------------------------------------------------
std::string slurp(const char* path) {
    std::string s;
    FILE* f = fopen(path, "rb");
    if (!f) return s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

struct Mem { const std::string* s; size_t at = 0; };

// Decoding to a whole picture at a scale: the reference the scaler is held to.
struct Whole { Mem mem; std::vector<uint8_t> rgb; uint16_t w = 0, h = 0; };
size_t wholeIn(JDEC* jd, uint8_t* buf, size_t n) {
    Mem& m = static_cast<Whole*>(jd->device)->mem;
    const size_t left = m.s->size() - m.at;
    if (n > left) n = left;
    if (buf) memcpy(buf, m.s->data() + m.at, n);
    m.at += n;
    return n;
}
int wholeOut(JDEC* jd, void* bitmap, JRECT* r) {
    Whole& c = *static_cast<Whole*>(jd->device);
    const uint8_t* p = static_cast<const uint8_t*>(bitmap);
    for (int y = r->top; y <= r->bottom; ++y)
        for (int x = r->left; x <= r->right; ++x, p += 3)
            if (x < c.w && y < c.h) memcpy(&c.rgb[(static_cast<size_t>(y) * c.w + x) * 3], p, 3);
    return 1;
}

bool decodeWhole(const std::string& jpg, uint8_t scale, Whole& out, uint16_t& iw, uint16_t& ih) {
    std::vector<uint8_t> pool(8192);
    JDEC jd;
    out.mem.s = &jpg;
    out.mem.at = 0;
    if (jd_prepare(&jd, wholeIn, pool.data(), pool.size(), &out) != JDR_OK) return false;
    iw = static_cast<uint16_t>(jd.width);
    ih = static_cast<uint16_t>(jd.height);
    out.w = static_cast<uint16_t>(iw >> scale);
    out.h = static_cast<uint16_t>(ih >> scale);
    out.rgb.assign(static_cast<size_t>(out.w) * out.h * 3, 0);
    return jd_decomp(&jd, wholeOut, scale) == JDR_OK;
}

// Through the scaler, as the board's job drives it (plat::jpegDecode's
// adapter, jpeg_host.cpp, inlined).
struct Through { Mem mem; Scaler* s; };
size_t throughIn(JDEC* jd, uint8_t* buf, size_t n) {
    Mem& m = static_cast<Through*>(jd->device)->mem;
    const size_t left = m.s->size() - m.at;
    if (n > left) n = left;
    if (buf) memcpy(buf, m.s->data() + m.at, n);
    m.at += n;
    return n;
}
int throughOut(JDEC* jd, void* bitmap, JRECT* r) {
    Through& t = *static_cast<Through*>(jd->device);
    return putBlock(t.s, r->left, r->top, static_cast<uint16_t>(r->right - r->left + 1),
                    static_cast<uint16_t>(r->bottom - r->top + 1), static_cast<const uint8_t*>(bitmap)) ? 1 : 0;
}

int g_waitAfter = -1;                    // the queue fills after this many rows of blocks
int g_rests = 0;
bool waitingSoon() { return g_waitAfter >= 0 && g_rests >= g_waitAfter; }
void restCount() { ++g_rests; }

int scaleThrough(const std::string& jpg, const Frame& f, std::vector<uint16_t>& pic, Scaler& s,
                 std::atomic<bool>& cancel, bool yield) {
    std::vector<uint16_t> acc(static_cast<size_t>(f.pw) * kAccRows * 4, 0);
    pic.assign(static_cast<size_t>(f.pw) * f.ph, 0xDEAD);
    std::vector<uint8_t> pool(8192);
    JDEC jd;
    Through t;
    t.mem.s = &jpg;
    s = Scaler{ nullptr, pic.data(), acc.data(), f.pw, f.ph, 0, 0, 0, &cancel, yield, false, waitingSoon, restCount };
    t.s = &s;
    if (jd_prepare(&jd, throughIn, pool.data(), pool.size(), &t) != JDR_OK) return -1;
    s.sw = static_cast<uint16_t>(jd.width >> f.scale);
    s.sh = static_cast<uint16_t>(jd.height >> f.scale);
    const int rc = jd_decomp(&jd, throughOut, f.scale);
    if (rc == JDR_OK) finish(s);
    return rc;
}

void runScaler() {
    std::printf("The scaler, against the host's decoder\n");
    const char* kFiles[] = { "photos/vga_422.jpg", "photos/xga_420.jpg", "photos/hd_420.jpg", "photos/uxga_422.jpg" };
    bool rows = true, exact = true, sizes = true, used[4] = {};
    char what[160] = "";
    for (const char* file : kFiles) {
        const std::string jpg = slurp(file);
        if (jpg.empty()) { check(file, false); continue; }
        for (const Glass& g : kGlass) {
            Whole probe;
            uint16_t iw = 0, ih = 0;
            if (!decodeWhole(jpg, 3, probe, iw, ih)) { sizes = false; snprintf(what, sizeof(what), "%s: no decode", file); continue; }
            Frame f;
            if (!fit(g.w, g.h, iw, ih, f)) continue;
            used[f.scale] = true;
            Whole ref;
            decodeWhole(jpg, f.scale, ref, iw, ih);
            if (ref.w != (iw >> f.scale) || ref.h != (ih >> f.scale)) sizes = false;
            std::vector<uint16_t> pic;
            Scaler s{};
            std::atomic<bool> cancel{ false };
            g_waitAfter = -1;
            const int rc = scaleThrough(jpg, f, pic, s, cancel, true);
            if (rc != JDR_OK || s.next != f.ph) {
                rows = false;
                snprintf(what, sizeof(what), "%s on %s: rc %d, %u of %u rows", file, g.name, rc, s.next, f.ph);
                continue;
            }
            // Each picture pixel: the integer average of the decoded pixels
            // whose (sx * pw / sw, sy * ph / sh) is it.
            std::vector<uint32_t> sum(static_cast<size_t>(f.pw) * f.ph * 4, 0);
            for (uint32_t y = 0; y < ref.h; ++y)
                for (uint32_t x = 0; x < ref.w; ++x) {
                    const size_t d = (static_cast<size_t>(y * f.ph / ref.h) * f.pw + x * f.pw / ref.w) * 4;
                    const uint8_t* p = &ref.rgb[(static_cast<size_t>(y) * ref.w + x) * 3];
                    sum[d] += p[0]; sum[d + 1] += p[1]; sum[d + 2] += p[2]; ++sum[d + 3];
                }
            for (size_t i = 0; i < pic.size() && exact; ++i) {
                const uint32_t n = sum[i * 4 + 3];
                if (!n) { exact = false; snprintf(what, sizeof(what), "%s on %s: pixel %zu has no sample", file, g.name, i); break; }
                const uint16_t want = panelgfx::rgb(static_cast<uint8_t>(sum[i * 4] / n), static_cast<uint8_t>(sum[i * 4 + 1] / n),
                                                    static_cast<uint8_t>(sum[i * 4 + 2] / n));
                if (pic[i] != want) { exact = false; snprintf(what, sizeof(what), "%s on %s: pixel %zu", file, g.name, i); }
            }
        }
    }
    check("every picture row finished, on every glass and every fixture", rows);
    check("each pixel the average of exactly the decoded pixels on it", exact);
    check("the decoder's output at 1/2^s is the file's size shifted", sizes);
    check("the fixtures reach 1/1, 1/2, 1/4 and 1/8", used[0] && used[1] && used[2] && used[3]);
    if (what[0]) std::printf("        first wrong: %s\n", what);

    // Called off, and stepping aside for other work.
    const std::string jpg = slurp("photos/uxga_422.jpg");
    Frame f;
    fit(480, 320, 1600, 1200, f);
    std::vector<uint16_t> pic;
    Scaler s{};
    std::atomic<bool> cancel{ true };
    g_waitAfter = -1;
    check("a called-off decode stops at its first block", scaleThrough(jpg, f, pic, s, cancel, true) == JDR_INTR && s.next == 0);
    cancel.store(false);
    g_rests = 0;
    g_waitAfter = 3;
    const int rc = scaleThrough(jpg, f, pic, s, cancel, true);
    check("other work queued: it steps aside at a row of blocks", rc == JDR_INTR && s.yielded && g_rests == 3);
    check("having finished only the rows those blocks closed", s.next > 0 && s.next < f.ph);
    g_rests = 0;
    const int rc2 = scaleThrough(jpg, f, pic, s, cancel, false);
    check("with its steps aside spent it runs to the end", rc2 == JDR_OK && !s.yielded && s.next == f.ph);
    g_waitAfter = -1;
}

}  // namespace

int main() {
    runFit();
    runCaption();
    runScaler();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
