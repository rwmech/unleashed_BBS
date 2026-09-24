// Checks for the panel's drawing (1.1.0, BBS_HAS_LCD boards), with no panel
// attached: src/plugins/panel_gfx.h and the Spleen faces in panel_font.h.
//
// Every rule here is one a reading of the code would pass and the glass
// would show: a glyph drawn a column out, a figure that leaves the tail of a
// longer one behind, a lamp that bleeds into its neighbour, a layout whose
// boxes overlap or run off the glass, a queue that sends a rectangle twice
// or loses one, or a band that is bigger than the DMA buffer it goes out of.
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v2 or later
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../src/plugins/panel_gfx.h"

using namespace panelgfx;

static int fails = 0, passes = 0;

static void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passes; else ++fails;
}

// A canvas over a vector, filled with a colour that nothing draws.
struct Glass {
    std::vector<uint16_t> px;
    Canvas c;
    Glass(uint16_t w, uint16_t h, uint16_t fill = 0x1234) : px(static_cast<size_t>(w) * h, fill) {
        c = { px.data(), w, h };
    }
    uint16_t at(int x, int y) const { return px[static_cast<size_t>(y) * c.w + x]; }
};

static bool overlap(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static bool inside(const Rect& r, uint16_t w, uint16_t h) {
    return r.x >= 0 && r.y >= 0 && r.x + r.w <= w && r.y + r.h <= h;
}

int main() {
    printf("Panel drawing\n");

    // --- colours -----------------------------------------------------------
    check("RGB565 white is 0xFFFF and black 0", rgb(255, 255, 255) == 0xFFFF && rgb(0, 0, 0) == 0);
    check("red is the top five bits, green the middle six, blue the low five",
          rgb(255, 0, 0) == 0xF800 && rgb(0, 255, 0) == 0x07E0 && rgb(0, 0, 255) == 0x001F);
    check("isqrt at the edges", isqrt(0) == 0 && isqrt(1) == 1 && isqrt(15) == 3 && isqrt(16) == 4 &&
                                isqrt(65535) == 255 && isqrt(0xFFFFFFFFu) == 65535);
    check("a strip pixel at the lights' 30% ceiling is full on glass", ledLevel(76) == 255);
    check("at the shipped 10%, a readable 146", ledLevel(25) == 146);
    check("at 1%, still there", ledLevel(2) == 41);
    check("dark stays dark", ledLevel(0) == 0);
    bool mono = true;
    for (int v = 1; v < 256; ++v) if (ledLevel(static_cast<uint8_t>(v)) < ledLevel(static_cast<uint8_t>(v - 1))) mono = false;
    check("brighter in is never dimmer out", mono);

    // --- glyphs -------------------------------------------------------------
    {
        const char* s = "A";
        check("printable ASCII is itself", glyph(s) == 'A' - 0x20 && *s == 0);
        s = "\xC2\xB5nleashed";
        check("the micro sign in UTF-8 is its own glyph, one of them",
              glyph(s) == panelfont::kMicro && *s == 'n');
        s = "\xE2\x82\xAC!";                    // the euro sign, three bytes
        check("anything else is one '?', however many bytes it took",
              glyph(s) == '?' - 0x20 && *s == '!');
        s = "\x01";
        check("a control byte is a '?'", glyph(s) == '?' - 0x20);
        check("the board's name counts nine glyphs, not ten bytes' worth",
              glyphs("\xC2\xB5nleashed") == 9 && textWidth("\xC2\xB5nleashed", false) == 72);
        check("the big face is twice as wide", textWidth("3 of 10", true) == 7 * 16);
    }

    // --- text ---------------------------------------------------------------
    {
        Glass g(40, 20);
        int w = text(g.c, 0, 0, "A", 0xFFFF, 0x0000, false, 40);
        check("one small glyph is 8 wide", w == 8);
        // Spleen's 8x16 'A': row 2 is 0x7C, so columns 1 to 5 set.
        bool rowOk = g.at(0, 2) == 0 && g.at(1, 2) == 0xFFFF && g.at(5, 2) == 0xFFFF && g.at(6, 2) == 0;
        check("drawn from the font's bits, left pixel first", rowOk);
        check("the cell's blank rows painted in the background", g.at(3, 0) == 0 && g.at(3, 15) == 0);
        check("nothing drawn past the cell", g.at(8, 2) == 0x1234 && g.at(3, 16) == 0x1234);
        Glass h(40, 20);
        w = text(h.c, 0, 0, "ABCDEFGH", 0xFFFF, 0, false, 20);
        check("a string is cut at its box, a whole glyph at a time", w == 16 && h.at(16, 2) == 0x1234);
        // Past the canvas's corner: clipped, which SAN=1 checks for writes
        // outside the vector; here, the corner pixel is the glyph's.
        Glass e(12, 10);
        text(e.c, 6, 4, "W", 0xFFFF, 0, false, 40);
        check("a glyph at the canvas's corner is drawn up to it and no further",
              (e.at(11, 9) == 0xFFFF || e.at(11, 9) == 0) && e.at(5, 9) == 0x1234);
    }

    // --- a figure in its box ------------------------------------------------
    {
        Glass g(100, 20);
        Rect r = R(0, 0, 100, 20);
        field(g.c, r, "12345678", 0xFFFF, 0x0001, false, LEFT);
        field(g.c, r, "12", 0xFFFF, 0x0001, false, LEFT);
        bool clean = true;
        for (int y = 0; y < 20; ++y) for (int x = 16; x < 100; ++x) if (g.at(x, y) != 0x0001) clean = false;
        check("a shorter figure leaves nothing of the longer one behind", clean);
        Glass rgt(100, 20);
        field(rgt.c, r, "12", 0xFFFF, 0x0001, false, RIGHT);
        bool right = false;
        for (int y = 0; y < 20; ++y) if (rgt.at(99, y) == 0xFFFF || rgt.at(98, y) == 0xFFFF) right = true;
        check("right aligned ends at the box's edge", right);
        Glass cen(100, 40);
        field(cen.c, R(0, 0, 100, 40), "88", 0xFFFF, 0x0001, true, CENTRE);
        bool leftEmpty = true;
        for (int y = 0; y < 40; ++y) for (int x = 0; x < 34; ++x) if (cen.at(x, y) != 0x0001) leftEmpty = false;
        check("centred: 32 pixels of text in 100 start at 34", leftEmpty);

        // Two lines in a box tall enough, broken at a space: 21 glyphs in a
        // 12-glyph box goes "12:34 logoff" over "Somebody".
        Glass two(96, 32);
        field(two.c, R(0, 0, 96, 32), "12:34 logoff Somebody", 0xFFFF, 0x0001, false, LEFT);
        bool top = false, bottom = false;
        for (int x = 0; x < 96; ++x) {
            for (int y = 0; y < 16; ++y) if (two.at(x, y) == 0xFFFF) top = true;
            for (int y = 16; y < 32; ++y) if (two.at(x, y) == 0xFFFF) bottom = true;
        }
        check("too wide for one line in a two-line box: two lines", top && bottom);
        Glass one(96, 32);
        field(one.c, R(0, 0, 96, 32), "short", 0xFFFF, 0x0001, false, LEFT);
        bool second = false;
        for (int x = 0; x < 96; ++x) for (int y = 26; y < 32; ++y) if (one.at(x, y) == 0xFFFF) second = true;
        check("a string that fits stays one line, centred in the box", !second);
    }

    // --- the strip's lamps --------------------------------------------------
    {
        Rect strip = R(0, 122, 320, 50);
        for (uint8_t n : { 1, 2, 8, 10, 16 }) {
            bool ok = true;
            for (uint8_t i = 0; i < n; ++i) {
                Rect c = stripCell(strip, i, n);
                if (!inside(c, 320, 172) || c.w <= 0) ok = false;
                if (i && overlap(c, stripCell(strip, static_cast<uint8_t>(i - 1), n))) ok = false;
            }
            char what[64];
            snprintf(what, sizeof(what), "%u lamp%s fit the strip without touching", n, n > 1 ? "s" : "");
            check(what, ok);
        }
        // Portrait: the strip wraps into a grid of bigger lamps.
        Rect port = R(0, 180, 172, 136);
        uint8_t cols, rows;
        int sz;
        stripGrid(port, 10, cols, rows, sz);
        check("portrait, ten lamps: two rows of five", cols == 5 && rows == 2 && sz >= 30);
        stripGrid(port, 16, cols, rows, sz);
        check("portrait, sixteen: four rows of four", cols == 4 && rows == 4);
        stripGrid(strip, 10, cols, rows, sz);
        check("landscape, ten: one row", cols == 10 && rows == 1);
        stripGrid(port, 7, cols, rows, sz);
        Rect a = stripCell(port, 0, 7), b = stripCell(port, 4, 7), l = stripCell(port, 6, 7);
        check("seven: a short last row, centred under the full one",
              rows == 2 && cols == 4 && b.y > a.y &&
              abs((b.x - port.x) - ((port.x + port.w) - (l.x + l.w))) <= 1);
        bool okGrid = true;
        for (uint8_t n : { 1, 2, 7, 8, 10, 16 })
            for (uint8_t i = 0; i < n; ++i) {
                Rect cell = stripCell(port, i, n);
                if (!contains(port, cell) || cell.w <= 0) okGrid = false;
                for (uint8_t k = 0; k < i; ++k) if (overlap(cell, stripCell(port, k, n))) okGrid = false;
            }
        check("every portrait grid inside the strip, no two cells touching", okGrid);

        Glass g(320, 172, 0);
        Rect c = stripCell(strip, 3, 10);
        lamp(g.c, c, 0xF800, 0x3333, 0x0000);
        int cx = c.x + c.w / 2, cy = c.y + c.h / 2;
        check("a lamp is lit at its centre", g.at(cx, cy) == 0xF800);
        bool contained = true;
        for (int y = 0; y < 172; ++y)
            for (int x = 0; x < 320; ++x)
                if (g.at(x, y) && !(x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h)) contained = false;
        check("and draws nothing outside its own cell", contained);
        check("its rim is a different colour from its light",
              g.at(cx, cy - (c.w < c.h ? c.w : c.h) / 2 + 2) == 0x3333);
    }

    // --- the layout ---------------------------------------------------------
    for (int turn = 0; turn < 2; ++turn) {
        const uint16_t w = turn ? 172 : 320, h = turn ? 320 : 172;
        Layout L = layout(w, h);
        bool in = inside(L.bar, w, h) && inside(L.strip, w, h);
        bool apart = true;
        for (uint8_t i = 0; i < F_COUNT; ++i) {
            if (!inside(L.field[i], w, h)) in = false;
            if (empty(L.field[i])) continue;
            for (uint8_t k = static_cast<uint8_t>(i + 1); k < F_COUNT; ++k)
                if (!empty(L.field[k]) && overlap(L.field[i], L.field[k])) apart = false;
            if (!empty(L.strip) && overlap(L.field[i], L.strip)) apart = false;
        }
        char what[80];
        snprintf(what, sizeof(what), "%ux%u: every box on the glass", w, h);
        check(what, in);
        snprintf(what, sizeof(what), "%ux%u: no two figures overlap, nor the strip", w, h);
        check(what, apart);
        snprintf(what, sizeof(what), "%ux%u: a strip at least 24 pixels tall", w, h);
        check(what, L.strip.h >= 24);
        snprintf(what, sizeof(what), "%ux%u: the callers' figure in the big face", w, h);
        check(what, L.big[F_CALLERS] && L.field[F_CALLERS].h >= kBigH);
    }
    {
        Layout L = layout(320, 172);
        check("320x172: \"callers on 10 of 11\" fits its line in the big face",
              textWidth("callers on 10 of 11", true) <= L.field[F_CALLERS].w);
        check("and a full address and uptime share a line",
              textWidth("192.168.100.200:6400", false) <= L.field[F_ADDR].w &&
              textWidth("up 99d 23:59", false) <= L.field[F_UPTIME].w);
        check("landscape has no separate label line", empty(L.field[F_LABEL]));
        Layout P = layout(172, 320);
        check("172x320: the label on a line of its own", !empty(P.field[F_LABEL]));
        check("and \"10 of 11\" fits", textWidth("10 of 11", true) <= P.field[F_CALLERS].w);
    }

    // --- the queue of what to send -------------------------------------------
    {
        Dirty d;
        d.add(R(0, 0, 10, 10));
        d.add(R(2, 2, 3, 3));
        check("a rectangle inside one already queued is not queued again", d.n == 1);
        d.add(R(0, 0, 50, 50));
        check("one that covers the queue replaces what it covers", d.n == 1 && d.q[0].w == 50);
        d.add(R(100, 100, 5, 5));
        check("one elsewhere joins it", d.n == 2);
        d.add(Rect());
        check("an empty rectangle is nothing", d.n == 2);

        Dirty f;
        for (int i = 0; i < Dirty::kMax + 5; ++i) f.add(R(i * 10, 0, 5, 5));
        check("a queue that fills becomes one rectangle around it, losing nothing",
              f.n <= Dirty::kMax && contains(f.q[f.n - 1], R((Dirty::kMax + 4) * 10, 0, 5, 5)) &&
              contains(f.q[0], R(0, 0, 5, 5)));

        // Banding: every pixel of a 320 x 50 rectangle sent exactly once, in
        // bands no bigger than the platform's buffer.
        Dirty b;
        b.add(R(0, 122, 320, 50));
        std::vector<int> seen(320 * 172, 0);
        Rect r;
        bool small = true;
        int bands = 0;
        while (b.next(320 * 16, r)) {
            ++bands;
            if (static_cast<uint32_t>(r.w) * r.h > 320u * 16u) small = false;
            for (int y = r.y; y < r.y + r.h; ++y) for (int x = r.x; x < r.x + r.w; ++x) ++seen[y * 320 + x];
        }
        bool once = true;
        for (int y = 0; y < 172; ++y)
            for (int x = 0; x < 320; ++x)
                if (seen[y * 320 + x] != (y >= 122 ? 1 : 0)) once = false;
        check("a rectangle goes out in whole-row bands, each pixel once", once && bands == 4);
        check("no band bigger than the buffer it goes out of", small);
        check("and then the queue is idle", b.idle());

        // A rectangle wider than a band: a row at a time, in pieces.
        Dirty wide;
        wide.add(R(0, 0, 50, 3));
        std::vector<int> hit(50 * 3, 0);
        bool fits = true;
        while (wide.next(20, r)) {
            if (static_cast<uint32_t>(r.w) * r.h > 20u) fits = false;
            for (int y = r.y; y < r.y + r.h; ++y) for (int x = r.x; x < r.x + r.w; ++x) ++hit[y * 50 + x];
        }
        bool each = true;
        for (int v : hit) if (v != 1) each = false;
        check("wider than a band: sent in pieces, each pixel once", each && fits);

        // Sending part of a rectangle and then changing it: the change is
        // queued again rather than lost to the part already sent.
        Dirty mid;
        mid.add(R(0, 0, 320, 64));
        mid.next(320 * 16, r);                              // the first 16 rows are gone
        mid.add(R(0, 0, 320, 8));                           // and then they change
        check("a change to rows already sent is sent again", mid.n == 1 && mid.q[0].h == 8);
    }

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
