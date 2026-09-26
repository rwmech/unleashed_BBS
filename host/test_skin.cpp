/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         host/test_skin.cpp
 * Module:       Host tests / panel skins
 *
 * Purpose:      Panel skins (1.1.2) with no panel:
 *                 - skin.txt's grammar against every case in
 *                   host/skins/cases.txt (the ones tools/mkskin.py
 *                   --selftest reads too), then a fuzz: a hundred thousand
 *                   mangled manifests, none of which may crash the reader,
 *                   and every one it accepts must obey the geometry rules;
 *                 - skin::checkJpeg on files Pillow made (host/skins/jpeg),
 *                   the ROM decoder's refusals included, and the host
 *                   decoder's pixels against Pillow's own;
 *                 - the LEDs and the lines drawn over a picture: light only
 *                   ever brightens, black leaves the art alone, nothing
 *                   outside a box is touched, a shorter line leaves nothing
 *                   behind, and what is queued covers what changed;
 *                 - the stock set seeded onto a card and kept, the sysop's
 *                   own edits left alone;
 *                 - each skin in skins/stock drawn whole, lit, to
 *                   host/skins/out/<name>.ppm for a person to look at.
 *               Run from host/ (make test does).
 *
 * Libraries:    TJpgDec R0.03 (host/tjpgd)
 * Targets:      the Linux host tests
 * See also:     src/plugins/skin_*.h, SKINS.md
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
#include "plugins/skin_manifest.h"
#include "plugins/skin_jpeg.h"
#include "plugins/skin_draw.h"
#include "plugins/skin_seed.h"
#include "tjpgd/tjpgd.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace skin;

static int passes = 0, fails = 0;

static void sampleFigures(Figures& f);
static Live sampleLive();

static void check(const char* what, bool ok) {
    if (ok) ++passes;
    else { ++fails; printf("  FAIL  %s\n", what); }
}

static std::string slurp(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return std::string();
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

// ---------------------------------------------------------------------------
// The shared cases
// ---------------------------------------------------------------------------
struct Case {
    std::string name, expect, body;
    int line = 0;
    std::string text;
};

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::string unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[i + 1];
            if (n == 'r') { o += '\r'; ++i; continue; }
            if (n == 't') { o += '\t'; ++i; continue; }
            if (n == 'x' && i + 3 < s.size() && hexv(s[i + 2]) >= 0 && hexv(s[i + 3]) >= 0) {
                o += static_cast<char>(hexv(s[i + 2]) * 16 + hexv(s[i + 3]));
                i += 3;
                continue;
            }
        }
        o += s[i];
    }
    return o;
}

static std::vector<Case> loadCases(const std::string& path) {
    std::vector<Case> out;
    std::string all = slurp(path);
    size_t at = 0;
    Case* cur = nullptr;
    bool header = false;
    while (at < all.size()) {
        size_t e = all.find('\n', at);
        if (e == std::string::npos) e = all.size();
        std::string line = all.substr(at, e - at);
        at = e + 1;
        if (!line.compare(0, 4, "=== ")) {
            out.push_back(Case());
            cur = &out.back();
            cur->name = line.substr(4);
            header = true;
            continue;
        }
        if (!cur) continue;
        if (header) {
            header = false;
            if (!line.compare(0, 9, "expect ok")) { cur->expect = "ok"; continue; }
            if (!line.compare(0, 13, "expect error ")) {
                cur->expect = "error";
                char* end = nullptr;
                cur->line = static_cast<int>(strtol(line.c_str() + 13, &end, 10));
                cur->text = end && *end == ' ' ? end + 1 : "";
                continue;
            }
        }
        cur->body += unescape(line) + "\n";
    }
    // A case's trailing blank lines are the file's spacing, not the skin's.
    for (Case& c : out)
        while (c.body.size() >= 2 && c.body[c.body.size() - 1] == '\n' && c.body[c.body.size() - 2] == '\n')
            c.body.erase(c.body.size() - 1);
    for (Case& c : out) if (c.body == "\n") c.body.clear();
    return out;
}

static void runCases() {
    printf("skin.txt: the shared cases\n");
    std::vector<Case> cases = loadCases("skins/cases.txt");
    check("the cases file was read, and has cases", cases.size() > 40);
    for (const Case& c : cases) {
        Manifest m;
        char err[160];
        const bool ok = parse(c.body.data(), c.body.size(), m, err, sizeof(err));
        bool right;
        if (c.expect == "ok") {
            right = ok;
        } else {
            char want[16];
            if (c.line) snprintf(want, sizeof(want), "line %d: ", c.line);
            else        want[0] = '\0';
            right = !ok && (c.line ? !strncmp(err, want, strlen(want)) : strncmp(err, "line ", 5) != 0) &&
                    strstr(err, c.text.c_str());
        }
        char what[200];
        snprintf(what, sizeof(what), "case '%s'", c.name.c_str());
        check(what, right);
        if (!right) printf("        got %s: \"%s\"\n", ok ? "ok" : "error", ok ? "" : err);
    }
}

// ---------------------------------------------------------------------------
// The fuzz
// ---------------------------------------------------------------------------
static uint32_t g_rnd = 0x2545F491u;
static uint32_t rnd() {
    g_rnd ^= g_rnd << 13;
    g_rnd ^= g_rnd >> 17;
    g_rnd ^= g_rnd << 5;
    return g_rnd;
}

// holds: an accepted manifest obeys every rule the reader promises.
static bool holds(const Manifest& m) {
    if (!m.w || !m.h || m.w > kPanelMax || m.h > kPanelMax) return false;
    std::vector<Box> boxes;
    uint32_t leds = 0;
    auto led = [&](const Led& l) {
        if (l.d < kLedMin || l.d > kLedMax || l.halo > kHaloMax) return false;
        const Box b = ledBox(l);
        boxes.push_back(b);
        leds += static_cast<uint32_t>(b.w) * b.h;
        return true;
    };
    if (m.drive.on && (!led(m.drive) || m.driveStyle >= DS_COUNT)) return false;
    if (m.activity.on && !led(m.activity)) return false;
    if (m.strip > kStripMax) return false;
    for (uint8_t i = 0; i < m.strip; ++i) if (!m.led[i].on || !led(m.led[i])) return false;
    if (m.nLamps > kLampsMax) return false;
    for (uint8_t i = 0; i < m.nLamps; ++i) if (!led(m.lamp[i].led) || m.lamp[i].state >= ST_COUNT) return false;
    if (leds > kLedPixelsMax) return false;
    if (m.nFields > kFieldsMax || m.nDigits > kDigitsMax || m.nNodes > kListsMax || m.nEvents > kListsMax ||
        m.nMeters > kMetersMax || m.nGraphs > kGraphsMax) return false;
    for (uint8_t i = 0; i < m.nFields; ++i) {
        const Field& f = m.field[i];
        if (f.value >= LW_COUNT || f.value == LW_WHO || f.value == LW_BLANK || f.style.face >= FACE_COUNT) return false;
        if (f.box.h != kGlyphH[f.style.face] || f.box.w < kGlyphW[f.style.face]) return false;
        if (strlen(f.label) > kLabelMax) return false;
        boxes.push_back(f.box);
    }
    for (uint8_t i = 0; i < m.nDigits; ++i) {
        const Digits& d = m.digits[i];
        if (d.n < 1 || d.n > kDigitsNMax || !numericWord(d.value) || d.box.h < kDigitsHMin || d.box.h > kDigitsHMax)
            return false;
        boxes.push_back(d.box);
    }
    for (uint8_t i = 0; i < m.nNodes + m.nEvents; ++i) {
        const List& l = i < m.nNodes ? m.nodes[i] : m.events[i - m.nNodes];
        if (l.style.face >= FACE_COUNT || l.box.w < 8 * kGlyphW[l.style.face] || l.box.h < kGlyphH[l.style.face])
            return false;
        boxes.push_back(l.box);
    }
    for (uint8_t i = 0; i < m.nMeters + m.nGraphs; ++i) {
        const Meter& g = i < m.nMeters ? m.meter[i] : m.graph[i - m.nMeters];
        if (g.src >= SRC_COUNT || (i >= m.nMeters && !graphSource(g.src)) || g.box.w < 2 || g.box.h < 2) return false;
        boxes.push_back(g.box);
    }
    if (m.hasText != (m.nLines > 0)) return false;
    if (m.hasText) {
        boxes.push_back(m.text);
        if (m.nLines * kGlyphH[m.textStyle.face] > m.text.h || m.text.w < kGlyphW[m.textStyle.face]) return false;
        for (uint8_t i = 0; i < m.nLines; ++i) {
            if (m.lines[i] >= LW_COUNT) return false;
            if (m.lines[i] == LW_WHO && i != m.nLines - 1) return false;
        }
    }
    if (m.hasClock) boxes.push_back(m.clock);
    for (size_t i = 0; i < boxes.size(); ++i) {
        const Box& b = boxes[i];
        if (b.x < 0 || b.y < 0 || b.x + b.w > m.w || b.y + b.h > m.h) return false;
        for (size_t k = i + 1; k < boxes.size(); ++k) if (overlaps(b, boxes[k])) return false;
    }
    return strlen(m.name) <= kNameMax;
}

static void runFuzz() {
    printf("skin.txt: the fuzz\n");
    std::vector<Case> cases = loadCases("skins/cases.txt");
    std::vector<std::string> seeds;
    for (const Case& c : cases) seeds.push_back(c.body);
    static const char* const kWords[] = { "skin", "panel", "name", "drive", "activity", "strip", "led", "text",
                                          "lines", "clock", "halo=", "colour=#", "size=big", "align=right", "field", "digits", "nodes",
                                          "events", "meter", "graph", "lamp", "node3", "rx", "traffic", "size=tiny",
                                          "free=yes", "order=oldest", "dir=up", "label=A_B", "dim=none", "online",
                                          "background=none", "shadow=#000000", "who", "1541", "pc", "0", "1",
                                          "480", "320", "65535", "99999", "#", ";", "\t", "\r", " ", "=", "\n" };
    const int kIters = 100000;
    int accepted = 0, broken = 0, badErr = 0;
    for (int it = 0; it < kIters; ++it) {
        std::string s = seeds[rnd() % seeds.size()];
        const int muts = 1 + rnd() % 6;
        for (int k = 0; k < muts; ++k) {
            const size_t pos = s.empty() ? 0 : rnd() % (s.size() + 1);
            switch (rnd() % 7) {
                case 0: if (!s.empty() && pos < s.size()) s[pos] = static_cast<char>(rnd()); break;
                case 1: if (!s.empty() && pos < s.size()) s.erase(pos, 1 + rnd() % 8); break;
                case 2: s.insert(pos, kWords[rnd() % (sizeof(kWords) / sizeof(kWords[0]))]); break;
                case 3: { std::string d = std::to_string(rnd() % 2000); s.insert(pos, d); break; }
                case 4: if (!s.empty()) s = s.substr(0, pos); break;
                case 5: s.insert(pos, std::string(1 + rnd() % 200, static_cast<char>('a' + rnd() % 26))); break;
                default: s += seeds[rnd() % seeds.size()]; break;
            }
        }
        if (s.size() > 6000) s.resize(6000);
        Manifest m;
        char err[160];
        memset(err, 'X', sizeof(err));
        const bool ok = parse(s.data(), s.size(), m, err, sizeof(err));
        if (ok) { ++accepted; if (!holds(m)) ++broken; }
        else if (!memchr(err, '\0', sizeof(err)) || !err[0]) ++badErr;
    }
    printf("        %d of %d accepted\n", accepted, kIters);
    check("no mangled manifest crashed the reader (under make SAN=1, none read out of bounds)", true);
    check("every one it accepted obeys the geometry rules", broken == 0);
    check("every one it refused says why, in a terminated string", badErr == 0);

    // A short error buffer: cut, never overrun.
    Manifest m;
    char tiny[8];
    memset(tiny, 'X', sizeof(tiny));
    const char bad[] = "skin 1\npanel 480 320\nbezel\n";
    check("a short error buffer is cut and terminated", !parse(bad, sizeof(bad) - 1, m, tiny, sizeof(tiny)) &&
                                                     memchr(tiny, '\0', sizeof(tiny)));
    check("no error buffer at all is allowed", !parse(bad, sizeof(bad) - 1, m, nullptr, 0));
    std::string big(kFileMax + 1, '#');
    char err[160];
    check("a file past 4096 bytes is refused whole", !parse(big.data(), big.size(), m, err, sizeof(err)) &&
                                                   strstr(err, "4097 bytes"));
    std::string edge(kFileMax, '\n');
    const char head[] = "skin 1\npanel 480 320\n";
    memcpy(&edge[0], head, sizeof(head) - 1);
    check("and one of exactly 4096 is read", parse(edge.data(), edge.size(), m, err, sizeof(err)));
    const char nul[] = "skin 1\npanel 480 320\n\0name x\n";
    check("a NUL in the file is refused, not read as its end",
          !parse(nul, sizeof(nul) - 1, m, err, sizeof(err)) && strstr(err, "line 3"));
}

// ---------------------------------------------------------------------------
// JPEGs
// ---------------------------------------------------------------------------
struct Mem {
    const std::string* s;
    size_t at = 0;
};

static size_t memRead(void* ctx, uint8_t* buf, size_t n) {
    Mem& m = *static_cast<Mem*>(ctx);
    const size_t left = m.s->size() - m.at;
    if (n > left) n = left;
    if (buf) memcpy(buf, m.s->data() + m.at, n);
    m.at += n;
    return n;
}

struct Decoded {
    std::vector<uint8_t> rgb;
    uint16_t w = 0, h = 0;
};

// decode: the host decoder, as jpeg_host.cpp drives it, into d.
struct DecCtx {
    Mem      mem;
    Decoded* out;
};

static size_t dIn(JDEC* jd, uint8_t* buf, size_t n) { return memRead(&static_cast<DecCtx*>(jd->device)->mem, buf, n); }

static int dOut(JDEC* jd, void* bitmap, JRECT* r) {
    DecCtx& c = *static_cast<DecCtx*>(jd->device);
    const uint8_t* p = static_cast<const uint8_t*>(bitmap);
    for (int y = r->top; y <= r->bottom; ++y)
        for (int x = r->left; x <= r->right; ++x, p += 3)
            memcpy(&c.out->rgb[(static_cast<size_t>(y) * c.out->w + x) * 3], p, 3);
    return 1;
}

static bool decode(const std::string& jpg, Decoded& d) {
    std::vector<uint8_t> pool(8192);
    JDEC jd;
    DecCtx c;
    c.mem.s = &jpg;
    c.out = &d;
    if (jd_prepare(&jd, dIn, pool.data(), pool.size(), &c) != JDR_OK) return false;
    d.w = static_cast<uint16_t>(jd.width);
    d.h = static_cast<uint16_t>(jd.height);
    d.rgb.assign(static_cast<size_t>(d.w) * d.h * 3, 0);
    return jd_decomp(&jd, dOut, 0) == JDR_OK;
}

static void runJpeg() {
    printf("background.jpg: the check and the decoder\n");
    struct Want { const char* file; bool ok; const char* why; };
    const Want kWant[] = {
        { "ok_444.jpg", true, "" }, { "ok_422.jpg", true, "" }, { "ok_420.jpg", true, "" },
        { "ok_opt.jpg", true, "" }, { "ok_q100.jpg", true, "" }, { "ok_exif.jpg", true, "" },
        { "bad_prog.jpg", false, "progressive" }, { "bad_grey.jpg", false, "greyscale" },
        { "bad_cmyk.jpg", false, "three colour components" }, { "bad_cut.jpg", false, "ends" },
        { "bad_png.jpg", false, "not a JPEG" }, { "bad_fill.jpg", false, "fill bytes" },
        { "bad_empty.jpg", false, "empty segment" },
    };
    for (const Want& w : kWant) {
        const std::string jpg = slurp(std::string("skins/jpeg/") + w.file);
        Mem m;
        m.s = &jpg;
        JpegInfo info;
        char err[96] = "";
        const bool ok = !jpg.empty() && checkJpeg(memRead, &m, info, err, sizeof(err));
        char what[160];
        snprintf(what, sizeof(what), "%s: %s", w.file, w.ok ? "taken, 48x32" : w.why);
        check(what, jpg.size() && ok == w.ok && (ok ? info.w == 48 && info.h == 32 : strstr(err, w.why) != nullptr));
        if (ok != w.ok || (!ok && !strstr(err, w.why))) printf("        got %s: %s\n", ok ? "ok" : "refused", err);
        if (!w.ok) continue;
        // The decoder's picture against Pillow's: the same to within what
        // two decoders differ by. Pillow's libjpeg smooths subsampled
        // chroma and TJpgDec repeats it, so at the fixture's hard colour
        // edge one pixel may be 50-odd levels out; the mean is a few.
        Decoded d;
        const std::string ref = slurp(std::string("skins/jpeg/") + std::string(w.file).substr(0, strlen(w.file) - 4) +
                                      ".rgb");
        const bool dec = decode(jpg, d);
        long worst = 0, sum = 0;
        if (dec && ref.size() == d.rgb.size())
            for (size_t i = 0; i < ref.size(); ++i) {
                const long e = labs(static_cast<long>(static_cast<uint8_t>(ref[i])) - d.rgb[i]);
                if (e > worst) worst = e;
                sum += e;
            }
        snprintf(what, sizeof(what), "%s decodes to Pillow's picture (mean error %.2f, worst %ld)", w.file,
                 ref.size() ? static_cast<double>(sum) / ref.size() : 0.0, worst);
        check(what, dec && ref.size() == d.rgb.size() && sum < static_cast<long>(ref.size()) * 4 && worst < 80);
    }
    // Past the check, the file ends: the check stops at the scan, so a file
    // cut in its picture passes it and the decoder says so.
    const std::string whole = slurp("skins/jpeg/ok_420.jpg");
    std::string cut = whole.substr(0, whole.size() - 200);
    Mem m;
    m.s = &cut;
    JpegInfo info;
    char err[96];
    check("a file cut in its picture passes the header check", checkJpeg(memRead, &m, info, err, sizeof(err)));
    Decoded d;
    check("and the decoder refuses it", !decode(cut, d));
    // Every prefix of a good file: checked without reading past its end.
    bool safe = true;
    for (size_t n = 0; n < 400 && n < whole.size(); ++n) {
        std::string p = whole.substr(0, n);
        Mem pm;
        pm.s = &p;
        if (checkJpeg(memRead, &pm, info, err, sizeof(err)) && n < 200) safe = false;   // the scan starts late
    }
    check("every cut of the header is refused, none read past its end", safe);
    // Bytes at random after a good SOI: never a crash, never a hang.
    int taken = 0;
    for (int it = 0; it < 20000; ++it) {
        std::string f = whole.substr(0, 2 + rnd() % 200);
        for (int k = 0; k < 4; ++k) if (f.size() > 2) f[2 + rnd() % (f.size() - 2)] = static_cast<char>(rnd());
        Mem fm;
        fm.s = &f;
        if (checkJpeg(memRead, &fm, info, err, sizeof(err))) ++taken;
    }
    check("twenty thousand mangled headers, no crash", true);
    (void)taken;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
struct Glass {
    std::vector<uint16_t> fb, bg;
    panelgfx::Canvas c;
    Glass(uint16_t w, uint16_t h) : fb(static_cast<size_t>(w) * h), bg(static_cast<size_t>(w) * h) {
        c = { fb.data(), w, h };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                bg[static_cast<size_t>(y) * w + x] = panelgfx::rgb(static_cast<uint8_t>(40 + x * 100 / w),
                                                                  static_cast<uint8_t>(30 + y * 80 / h), 60);
    }
    uint16_t at(int x, int y) const { return fb[static_cast<size_t>(y) * c.w + x]; }
    uint16_t art(int x, int y) const { return bg[static_cast<size_t>(y) * c.w + x]; }
};

struct Built {
    Manifest m;
    std::vector<uint8_t> maps;
    Scene s;
};

static bool build(const char* txt, Glass& g, Built& b) {
    char err[160];
    if (!parse(txt, strlen(txt), b.m, err, sizeof(err))) { printf("        %s\n", err); return false; }
    size_t total = 0;
    const Led* leds[kSceneLeds] = { b.m.drive.on ? &b.m.drive : nullptr, b.m.activity.on ? &b.m.activity : nullptr };
    for (uint8_t i = 0; i < b.m.strip; ++i) leds[2 + i] = &b.m.led[i];
    for (uint8_t i = 0; i < b.m.nLamps; ++i) leds[kLampBase + i] = &b.m.lamp[i].led;
    for (const Led* l : leds) if (l) total += ledMapBytes(*l);
    b.maps.assign(total, 0);
    size_t at = 0;
    b.s = Scene();
    b.s.m = &b.m;
    b.s.bg = g.bg.data();
    for (int i = 0; i < kSceneLeds; ++i) {
        if (!leds[i]) continue;
        ledWeights(*leds[i], &b.maps[at]);
        b.s.map[i] = &b.maps[at];
        at += ledMapBytes(*leds[i]);
    }
    return true;
}

static bool insideBox(const Box& b, int x, int y) { return x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h; }

static void runDraw() {
    printf("Drawing a skin\n");
    Glass g(480, 320);
    Built b;
    const char* txt =
        "skin 1\npanel 480 320\n"
        "drive 400 250 12 1541 halo=6\n"
        "activity 440 250 8 colour=#40FF40\n"
        "strip 3\nled 1 20 300 8\nled 2 40 300 8\nled 3 60 300 8\n"
        "text 20 20 200 64 shadow=#000000\nlines name address callers who\n"
        "clock 400 20 size=big background=#102030\n";
    check("the drawing test's own manifest is taken", build(txt, g, b));
    panelgfx::Dirty d;
    b.s.full(g.c, d);
    // The art as full() leaves it: the picture, and the clock's own background.
    std::vector<uint16_t> want = g.bg;
    panelgfx::Canvas wc = { want.data(), 480, 320 };
    b.s.fills(wc);
    check("full(): the framebuffer is the art, the clock box filled", g.fb == want);
    check("and the whole glass is queued", d.n == 1 && d.q[0].w == 480 && d.q[0].h == 320);

    // Every LED dark: nothing moves.
    LedState dark;
    dark.n = 3;
    b.s.leds(g.c, d, dark);
    check("dark LEDs leave the art exactly as painted", g.fb == want);

    // Lit.
    LedState lit = dark;
    lit.drive[0] = 255; lit.drive[1] = 120; lit.drive[2] = 0; lit.drivePct = 100;
    lit.activity = true;
    lit.strip[0] = 255; lit.strip[4] = 255; lit.strip[8] = 255; lit.stripPct = 100;   // red, green, blue
    d.clear();
    const int drew = b.s.leds(g.c, d, lit);
    check("five LEDs redrawn", drew == 5);
    bool brighter = true, outside = true;
    std::vector<Box> boxes = { ledBox(b.m.drive), ledBox(b.m.activity), ledBox(b.m.led[0]), ledBox(b.m.led[1]),
                               ledBox(b.m.led[2]) };
    for (int y = 0; y < 320; ++y)
        for (int x = 0; x < 480; ++x) {
            bool in = false;
            for (const Box& bx : boxes) if (insideBox(bx, x, y)) in = true;
            const panelgfx::Rgb8 a = panelgfx::unpack(g.art(x, y)), f = panelgfx::unpack(g.at(x, y));
            if (!in) { if (g.at(x, y) != want[static_cast<size_t>(y) * 480 + x]) outside = false; continue; }
            if (f.r + 8 < a.r || f.g + 8 < a.g || f.b + 8 < a.b) brighter = false;   // a step of the packing
        }
    check("light only ever brightens the art", brighter);
    check("and nothing outside an LED's box changed", outside);
    {
        const panelgfx::Rgb8 c = panelgfx::unpack(g.at(20, 300));                 // led 1's middle, red
        const panelgfx::Rgb8 e = panelgfx::unpack(g.at(20 - 7, 300));             // its halo's edge
        check("a red LED's middle is red, and hot", c.r >= 240 && c.g > panelgfx::unpack(g.art(20, 300)).g);
        check("and its halo fades out towards the edge of its box", e.r < c.r && e.r >= panelgfx::unpack(g.art(13, 300)).r);
        const panelgfx::Rgb8 gr = panelgfx::unpack(g.at(40, 300));
        check("the green one is green", gr.g >= 240 && gr.g > gr.r);
    }
    bool covered = true;
    for (const Box& bx : boxes) {
        bool in = false;
        for (uint8_t i = 0; i < d.n; ++i) {
            const panelgfx::Rect& r = d.q[i];
            if (bx.x >= r.x && bx.y >= r.y && bx.x + bx.w <= r.x + r.w && bx.y + bx.h <= r.y + r.h) in = true;
        }
        if (!in) covered = false;
    }
    check("every LED that changed is inside a queued rectangle", covered);
    check("the strip's three went as one rectangle (merged), the drive and activity as another", d.n <= 3);

    // The same state again: nothing to do.
    d.clear();
    check("the same state again redraws nothing", b.s.leds(g.c, d, lit) == 0 && d.n == 0);
    // Back to dark: the art again, exactly.
    b.s.leds(g.c, d, dark);
    check("dark again: the art again, pixel for pixel", g.fb == want);

    // The backlog: a queue already long waits for the glass.
    d.clear();
    for (int i = 0; i < 6; ++i) d.add(panelgfx::R(i * 70, 100, 10, 10));
    check("with the glass behind, a frame of LEDs waits", b.s.leds(g.c, d, lit) == 0);

    // The budget: sixteen 64 x 64 boxes cost 4,096 pixels each, so a frame
    // draws two, and the next frame carries on from the third.
    {
        Glass g2(1024, 1024);
        Built big;
        std::string t = "skin 1\npanel 1024 1024\nstrip 16\n";
        for (int i = 0; i < 16; ++i)
            t += "led " + std::to_string(i + 1) + " " + std::to_string(40 + (i % 8) * 120) + " " +
                 std::to_string(40 + (i / 8) * 120) + " 48 halo=8\n";
        // 16 x 4,096 = 65,536: exactly the cap.
        check("a skin at the LED cap is taken", build(t.c_str(), g2, big));
        panelgfx::Dirty q;
        big.s.full(g2.c, q);
        LedState on;
        on.n = 16;
        on.stripPct = 100;
        for (int i = 0; i < 16; ++i) on.strip[i * 3] = 255;
        int frames = 0, total = 0, most = 0;
        for (; frames < 20 && total < 16; ++frames) {
            q.clear();
            const int n = big.s.leds(g2.c, q, on);
            total += n;
            if (n > most) most = n;
        }
        check("the LED budget: two 64 x 64 LEDs a frame, never more", most == 2);
        check("and every LED is drawn in turn within eight frames", total == 16 && frames == 8);
        q.clear();
        check("then a frame with nothing changed draws nothing", big.s.leds(g2.c, q, on) == 0);
    }

    // Text.
    Figures f;
    snprintf(f.name, sizeof(f.name), "The Rusty Antenna");
    snprintf(f.address, sizeof(f.address), "192.168.0.40:6400");
    snprintf(f.callers, sizeof(f.callers), "Callers 2/11");
    f.who = 3;
    snprintf(f.whoLine[0], sizeof(f.whoLine[0]), " 1) bob 4m");
    snprintf(f.whoLine[1], sizeof(f.whoLine[1]), " 2) alice 1h");
    snprintf(f.whoLine[2], sizeof(f.whoLine[2]), " 3) nobody fits");
    snprintf(f.clock, sizeof(f.clock), "14:05");
    d.clear();
    b.s.text(g.c, d, f);
    check("four rows of the rectangle queued, and the clock", d.n >= 2);
    auto lit_in = [&](const Box& bx, uint16_t col) {
        int n = 0;
        for (int y = bx.y; y < bx.y + bx.h; ++y)
            for (int x = bx.x; x < bx.x + bx.w; ++x) if (g.at(x, y) == col) ++n;
        return n;
    };
    const Box row0 = { 20, 20, 200, 16 };
    check("the name is written in the ink, over the art", lit_in(row0, pack(b.m.textStyle.fg)) > 40);
    check("with its shadow", lit_in(row0, pack(Rgb())) > 20);
    check("the clock's box is its own background colour where no glyph is",
          lit_in(b.m.clock, pack(b.m.clockStyle.bg)) > 400);
    check("nothing outside the rectangle and the clock was written", [&] {
        for (int y = 0; y < 320; ++y)
            for (int x = 0; x < 480; ++x)
                if (!insideBox(b.m.text, x, y) && !insideBox(b.m.clock, x, y) && g.at(x, y) != g.art(x, y)) return false;
        return true;
    }());
    // Only the 4 lines plus who's remainder rows exist: 64 px / 16 = 4 rows,
    // name address callers + one who row.
    const Box row3 = { 20, 68, 200, 16 };
    check("who gets the rows the others leave: one here", lit_in(row3, pack(b.m.textStyle.fg)) > 20);
    // A shorter name leaves nothing of the longer one behind.
    snprintf(f.name, sizeof(f.name), "Ant");
    d.clear();
    b.s.text(g.c, d, f);
    bool clean = true;
    for (int y = row0.y; y < row0.y + row0.h; ++y)
        for (int x = row0.x + 3 * 8 + 1; x < row0.x + row0.w; ++x)
            if (g.at(x, y) != g.art(x, y)) clean = false;
    check("a shorter line leaves nothing of the longer one", clean);
    check("and only that row was queued", d.n == 1 && d.q[0].y == 20 && d.q[0].h == 16);
    d.clear();
    b.s.text(g.c, d, f);
    check("the same words again: nothing drawn", d.n == 0);

    // ---- the live widgets (1.2.0) ------------------------------------------
    {
        Glass w(480, 320);
        Built wb;
        const char* wt =
            "skin 1\npanel 480 320\n"
            "field 10 10 200 online label=ON_LINE: size=tiny colour=#FFFFFF\n"
            "field 10 30 200 lastcaller colour=#FFFF00\n"
            "digits 300 10 30 2 online colour=#FF2000 dim=#200400\n"
            "digits 300 50 20 4 clock colour=#FF2000\n"
            "nodes 10 60 240 72 size=tiny free=yes\n"
            "events 10 140 240 48 size=tiny order=oldest background=#000000\n"
            "meter 300 90 100 8 callers colour=#00FF00\n"
            "graph 300 110 100 40 traffic colour=#00FF00 background=#000000\n"
            "lamp 300 200 8 node1\nlamp 320 200 8 node2\nlamp 340 200 8 node3\nlamp 360 200 8 run\n";
        check("a skin of every widget is taken", build(wt, w, wb));
        panelgfx::Dirty q;
        wb.s.full(w.c, q);
        Figures f;
        sampleFigures(f);
        const Live lv = sampleLive();
        q.clear();
        const int drew = wb.s.widgets(w.c, q, f, lv, 0xFFFFFFFFu);
        check("every widget unit drawn the first time", drew == static_cast<int>(wb.s.unitCount()));
        q.clear();
        check("and none again when nothing changed", wb.s.widgets(w.c, q, f, lv) == 0 && q.n == 0);
        // Words.
        char row[96];
        nodeRow(f.row[3], 40, row, sizeof(row));
        check("a node row fitted to 40 columns: line, mark, handle, doing, time",
              strstr(row, " 3> bob") == row && strstr(row, "FILES") && strlen(row) == 40);
        nodeRow(f.row[2], 40, row, sizeof(row));
        check("a free line says so", !strcmp(row, " 2  waiting"));
        nodeRow(f.row[1], 10, row, sizeof(row));
        check("too narrow for the doing, the handle and the time only", strstr(row, "alice") && !strstr(row, "CHAT"));
        const List& ev = wb.m.events[0];
        check("events oldest-first: the newest at the foot, the four before it above",
              !strcmp(eventRow(f, ev, 3, 4), "21:47 login Wanderer") && !strcmp(eventRow(f, ev, 0, 4), "21:20 page bob"));
        Figures few = f;
        few.events = 2;
        check("and with fewer events than rows the top rows are blank",
              !strcmp(eventRow(few, ev, 0, 4), "") && !strcmp(eventRow(few, ev, 2, 4), "21:40 login alice"));
        List newest;
        check("and newest-first: the newest at the top", !strcmp(eventRow(f, newest, 0, 4), "21:47 login Wanderer"));
        List busy;
        check("a node list of callers only skips the free lines",
              listRow(f, busy, 1) && listRow(f, busy, 1)->line == 3 && !listRow(f, busy, 3));
        List all;
        all.free = true;
        check("and with free=yes lists every line, the sysop's only while on it",
              listRow(f, all, 0) && listRow(f, all, 0)->line == 1 && listRow(f, all, 9) && listRow(f, all, 9)->line == 10 && !listRow(f, all, 10));
        // Pixels: the seven segments of "3" lit in the display's red, the
        // unlit ones in its dim, one digit blank (leading).
        const Digits& dg = wb.m.digits[0];
        int lit = 0, dim = 0;
        for (int y = dg.box.y; y < dg.box.y + dg.box.h; ++y)
            for (int x = dg.box.x; x < dg.box.x + dg.box.w; ++x) {
                if (w.at(x, y) == pack(dg.fg)) ++lit;
                if (w.at(x, y) == pack(dg.dim)) ++dim;
            }
        check("the display lights its digit's segments and dims the rest", lit > 60 && dim > 60);
        const Meter& mt = wb.m.meter[0];
        int full = 0;
        for (int x = mt.box.x; x < mt.box.x + mt.box.w; ++x) if (w.at(x, mt.box.y + 4) == pack(mt.fg)) ++full;
        check("the callers meter filled 3 of 11 of its length", full == 3 * 100 / 11);
        const Meter& gr = wb.m.graph[0];
        int cols = 0;
        for (int x = gr.box.x; x < gr.box.x + gr.box.w; ++x)
            if (w.at(x, gr.box.y + gr.box.h - 1) != pack(gr.bg)) ++cols;
        check("the traffic graph draws a column a sample", cols > 80);
        // The budget: a pass stops at it and the next carries on.
        wb.s.forget();
        q.clear();
        const int first = wb.s.widgets(w.c, q, f, lv, 4000);
        check("a pass stops at its budget and says so", first > 0 && first < static_cast<int>(wb.s.unitCount()) &&
                                                         wb.s.pending);
        int passes = 1;
        while (wb.s.pending && passes < 50) { q.clear(); wb.s.widgets(w.c, q, f, lv, 4000); ++passes; }
        check("and the passes after it finish the rest", !wb.s.pending && passes > 1);
        // A change redraws that unit only.
        snprintf(f.lastcaller, sizeof(f.lastcaller), "carol");
        q.clear();
        check("a new last caller redraws that one field", wb.s.widgets(w.c, q, f, lv) == 1 && q.n == 1 &&
                                                          q.q[0].y == 30);
        // Lamps: line 1 on dim, line 3 hot, line 2 dark; run always lit.
        LedState ls;
        ls.nodeOn = 0x0005;
        ls.nodeHot = 0x0004;
        check("a node lamp glows while its line is on", lampLevel(ST_NODE + 0, ls) == 90);
        check("flares on a keystroke", lampLevel(ST_NODE + 2, ls) == 255);
        check("and is dark on a free line", lampLevel(ST_NODE + 1, ls) == 0);
        check("run is always lit", lampLevel(ST_RUN, ls) == 255 && lampLevel(ST_CLOSED, ls) == 0);
        q.clear();
        wb.s.leds(w.c, q, ls);
        const Led& l3 = wb.m.lamp[2].led, l2 = wb.m.lamp[1].led;
        check("the lamps drawn: line 3's lit over the art, line 2's the art",
              w.at(l3.x, l3.y) != w.art(l3.x, l3.y) && w.at(l2.x, l2.y) == w.art(l2.x, l2.y));
    }

    // mergeBoxes on its own.
    Box row[4] = { { 0, 0, 10, 10 }, { 12, 0, 10, 10 }, { 24, 0, 10, 10 }, { 400, 300, 10, 10 } };
    check("a row of neighbours merges; a far corner does not", mergeBoxes(row, 4) == 2);

    // The weights: a lens is solid in the middle, zero at the box's corners.
    Led l;
    l.d = 16;
    l.halo = 8;
    std::vector<uint8_t> w(ledMapBytes(l));
    ledWeights(l, w.data());
    const int side = 32;
    check("the lens is lit in the middle, hot there",
          w[(16 * side + 16) * 2] >= 250 && w[(16 * side + 16) * 2 + 1] > 150);
    check("the box's corners are dark", w[0] == 0 && w[1] == 0 && w[((side - 1) * side + side - 1) * 2] == 0);
    bool sym = true;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            if (w[(y * side + x) * 2] != w[(y * side + (side - 1 - x)) * 2] ||
                w[(y * side + x) * 2] != w[(x * side + y) * 2]) sym = false;
    check("and it is round: the same mirrored and turned", sym);
}

// ---------------------------------------------------------------------------
// Seeding the stock set
// ---------------------------------------------------------------------------
static std::string tmpCard() {
    char tmpl[] = "/tmp/skinseedXXXXXX";
    const char* d = mkdtemp(tmpl);
    return d ? d : "";
}

static void runSeed() {
    printf("The stock skins on the card\n");
    const std::string card = tmpCard();
    check("a card to seed", !card.empty());
    static const uint8_t a1[] = "skin 1\npanel 480 320\n", a2[] = "JPEGA", b1[] = "skin 1\npanel 480 320\nname B\n",
                         b2[] = "JPEGB";
    StockFile v1[] = { { "aaa/skin.txt", a1, sizeof(a1) - 1 }, { "aaa/background.jpg", a2, sizeof(a2) - 1 },
                       { "bbb/skin.txt", b1, sizeof(b1) - 1 }, { "bbb/background.jpg", b2, sizeof(b2) - 1 } };
    SeedCount c = seed(card.c_str(), v1, 4);
    check("a fresh card gets both skins", c.made == 2 && !c.failed);
    check("byte for byte", slurp(card + "/skins/aaa/background.jpg") == "JPEGA" &&
                           slurp(card + "/skins/bbb/skin.txt") == std::string(reinterpret_cast<const char*>(b1)));
    check("and a record of them", slurp(card + "/skins/.seeded").find("aaa/skin.txt ") != std::string::npos);
    c = seed(card.c_str(), v1, 4);
    check("the same set again: nothing written", !c.made && !c.refreshed && !c.kept);

    // The sysop edits aaa's skin.txt; the stock set moves on for both.
    FILE* f = fopen((card + "/skins/aaa/skin.txt").c_str(), "wb");
    fputs("skin 1\npanel 480 320\nname Mine\n", f);
    fclose(f);
    static const uint8_t a2n[] = "JPEGA2", b2n[] = "JPEGB2";
    StockFile v2[] = { { "aaa/skin.txt", a1, sizeof(a1) - 1 }, { "aaa/background.jpg", a2n, sizeof(a2n) - 1 },
                       { "bbb/skin.txt", b1, sizeof(b1) - 1 }, { "bbb/background.jpg", b2n, sizeof(b2n) - 1 } };
    c = seed(card.c_str(), v2, 4);
    check("a skin the sysop touched is kept whole", c.kept == 1 && slurp(card + "/skins/aaa/background.jpg") == "JPEGA" &&
                                                  slurp(card + "/skins/aaa/skin.txt").find("Mine") != std::string::npos);
    check("the untouched one follows the new stock", c.refreshed == 1 && slurp(card + "/skins/bbb/background.jpg") == "JPEGB2");
    check("and the touched one is no longer recorded", slurp(card + "/skins/.seeded").find("aaa/") == std::string::npos);
    c = seed(card.c_str(), v2, 4);
    check("nor ever taken back", c.kept == 1 && slurp(card + "/skins/aaa/skin.txt").find("Mine") != std::string::npos);

    // No record, but byte for byte stock: the board's.
    remove((card + "/skins/.seeded").c_str());
    c = seed(card.c_str(), v2, 4);
    check("with the record lost, a folder that is exactly stock is the board's again",
          slurp(card + "/skins/.seeded").find("bbb/skin.txt") != std::string::npos);
    check("a folder that is not stays the sysop's", c.kept == 1);

    // Nothing to seed: nothing happens.
    c = seed(card.c_str(), nullptr, 0);
    check("an empty stock set does nothing", !c.made && !c.refreshed && !c.kept && !c.failed);
    std::string rm = "rm -rf " + card;
    if (system(rm.c_str()) != 0) printf("        (could not remove %s)\n", card.c_str());
}

// ---------------------------------------------------------------------------
// The stock skins, drawn
// ---------------------------------------------------------------------------
static void writePpm(const std::string& path, const std::vector<uint16_t>& fb, int w, int h) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (uint16_t v : fb) {
        const panelgfx::Rgb8 c = panelgfx::unpack(v);
        const uint8_t px[3] = { c.r, c.g, c.b };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

// sampleFigures: a board mid-evening, for the stock renders and the widget
// tests: three callers on (a co-sysop among them), a guest, a few events.
static void sampleFigures(Figures& f) {
    snprintf(f.name, sizeof(f.name), "The Rusty Antenna");
    snprintf(f.address, sizeof(f.address), "192.168.0.40:6400");
    snprintf(f.uptime, sizeof(f.uptime), "up 3d 4h");
    snprintf(f.callers, sizeof(f.callers), "Callers 3/11");
    snprintf(f.today, sizeof(f.today), "14 calls today");
    snprintf(f.heap, sizeof(f.heap), "84K free");
    snprintf(f.card, sizeof(f.card), "card 29 GB free");
    snprintf(f.clock, sizeof(f.clock), "21:47");
    snprintf(f.date, sizeof(f.date), "Sat 26 Sep");
    snprintf(f.last, sizeof(f.last), "21:47 login Wanderer");
    f.who = 3;
    snprintf(f.whoLine[0], sizeof(f.whoLine[0]), " 1) alice 7m");
    snprintf(f.whoLine[1], sizeof(f.whoLine[1]), " 3> bob 1h");
    snprintf(f.whoLine[2], sizeof(f.whoLine[2]), " 4* Wanderer 2m");
    snprintf(f.online, sizeof(f.online), "3");
    snprintf(f.lines, sizeof(f.lines), "11");
    snprintf(f.lastcaller, sizeof(f.lastcaller), "Wanderer");
    snprintf(f.rssi, sizeof(f.rssi), "-58 dBm");
    snprintf(f.peak, sizeof(f.peak), "5");
    snprintf(f.version, sizeof(f.version), "1.2.0 (MF35 1.1.0)");
    f.nOnline = 3; f.nLines = 11; f.nToday = 14; f.nPeak = 5; f.nRssi = -58; f.nHeapK = 84; f.nClock = 2147;
    f.heapTopK = 160; f.cardFreeMB = 29000; f.cardTotalMB = 30436;
    f.fMail = true; f.fStaff = true; f.fListed = true; f.fCard = true;
    f.rows = 11;                                        // the sysop's and ten lines
    for (uint8_t i = 0; i < f.rows; ++i) { f.row[i] = NodeRow(); f.row[i].line = i; }
    auto on = [&](uint8_t line, char mark, const char* h, const char* d, const char* t) {
        NodeRow& r = f.row[line];
        r.on = true; r.mark = mark;
        snprintf(r.handle, sizeof(r.handle), "%s", h);
        snprintf(r.doing, sizeof(r.doing), "%s", d);
        snprintf(r.onFor, sizeof(r.onFor), "%s", t);
    };
    on(1, ')', "alice", "CHAT", "7m");
    on(3, '>', "bob", "FILES", "1h");
    on(4, '*', "Wanderer", "MAIL", "2m");
    static const char* const kEv[] = { "21:47 login Wanderer", "21:40 login alice", "21:33 logoff carol",
                                       "21:20 page bob", "21:02 login bob", "20:51 logoff dave",
                                       "20:30 login carol", "20:12 ring dave" };
    f.events = 8;
    for (int i = 0; i < 8; ++i) snprintf(f.event[i], sizeof(f.event[i]), "%s", kEv[i]);
}

// sampleLive: two minutes of history, traffic rising and falling and the
// callers stepping up to three.
static uint16_t g_trafficS[kHistory];
static uint8_t  g_callersS[kHistory];
static Live sampleLive() {
    Live lv;
    for (int i = 0; i < kHistory; ++i) {
        const int back = kHistory - 1 - i;              // i is oldest first; head is the newest
        g_trafficS[i] = static_cast<uint16_t>(900.0 * (1.2 + sin(back / 7.0) + 0.6 * sin(back / 2.3)) > 0
                                              ? 900.0 * (1.2 + sin(back / 7.0) + 0.6 * sin(back / 2.3)) : 0);
        g_callersS[i] = static_cast<uint8_t>(back < 40 ? 3 : back < 90 ? 2 : back < 160 ? 1 : 0);
    }
    lv.traffic = g_trafficS;
    lv.callers = g_callersS;
    lv.head = kHistory - 1;
    lv.samples = kHistory;
    lv.rate = 2400;
    return lv;
}

static void runStock() {
    printf("The stock skins, drawn\n");
    DIR* dir = opendir("../skins/stock");
    if (!dir) { printf("        (no ../skins/stock)\n"); return; }
    mkdir("skins/out", 0755);
    int drawn = 0;
    for (struct dirent* e = readdir(dir); e; e = readdir(dir)) {
        if (e->d_name[0] == '.') continue;
        const std::string base = std::string("../skins/stock/") + e->d_name;
        const std::string txt = slurp(base + "/skin.txt"), jpg = slurp(base + "/background.jpg");
        if (txt.empty()) continue;
        Manifest m;
        char err[160];
        char what[160];
        const bool ok = parse(txt.data(), txt.size(), m, err, sizeof(err));
        snprintf(what, sizeof(what), "%.100s: skin.txt is taken", e->d_name);
        check(what, ok);
        if (!ok) { printf("        %s\n", err); continue; }
        Mem mm;
        mm.s = &jpg;
        JpegInfo info;
        const bool jok = checkJpeg(memRead, &mm, info, err, sizeof(err));
        snprintf(what, sizeof(what), "%.60s: background.jpg is one the ROM decodes, at the panel's size", e->d_name);
        check(what, jok && info.w == m.w && info.h == m.h);
        if (!jok) { printf("        %s\n", err); continue; }
        Decoded d;
        snprintf(what, sizeof(what), "%.100s: and it decodes", e->d_name);
        check(what, decode(jpg, d));
        Glass g(m.w, m.h);
        for (size_t i = 0; i < g.bg.size(); ++i) g.bg[i] = panelgfx::rgb(d.rgb[i * 3], d.rgb[i * 3 + 1], d.rgb[i * 3 + 2]);
        Built b;
        build(txt.c_str(), g, b);
        panelgfx::Dirty q;
        b.s.full(g.c, q);
        LedState st;
        st.drive[0] = 255; st.drive[1] = 150; st.drive[2] = 20; st.drivePct = 100;   // amber: the card
        st.activity = true;
        st.n = b.m.strip;
        st.stripPct = 100;
        for (uint8_t i = 0; i < st.n; ++i) {
            const bool on = (i * 7 + 3) % 5 < 3;                                      // a front panel's pattern
            st.strip[i * 3] = on ? 255 : 0;
            st.strip[i * 3 + 1] = on ? 40 : 0;
            st.strip[i * 3 + 2] = on ? 20 : 0;
        }
        // Lines 1, 3 and 4 on, 3 at the keyboard; traffic both ways; the
        // sysop's mail waiting, staff on, listed, a card in.
        st.nodeOn = 0x000D;
        st.nodeHot = 0x0004;
        st.rx = true;
        st.mail = st.staff = st.listed = st.card = true;
        for (int f = 0; f < 16; ++f) { q.clear(); b.s.leds(g.c, q, st); }   // past the budget, in turn
        Figures f;
        sampleFigures(f);
        const Live lv = sampleLive();
        for (int k = 0; k < 64 && (k == 0 || b.s.pending); ++k) { q.clear(); b.s.widgets(g.c, q, f, lv); }
        snprintf(what, sizeof(what), "%.100s: every widget drawn within the budget, a pass at a time", e->d_name);
        check(what, !b.s.pending);
        writePpm(std::string("skins/out/") + e->d_name + ".ppm", g.fb, m.w, m.h);
        ++drawn;
    }
    closedir(dir);
    printf("        %d drawn to host/skins/out/\n", drawn);
}

int main() {
    printf("Panel skins\n");
    runCases();
    runFuzz();
    runJpeg();
    runDraw();
    runSeed();
    runStock();
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
