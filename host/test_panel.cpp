// Checks for the panel's drawing (1.1.0, BBS_HAS_LCD boards), with no panel
// attached: src/plugins/panel_gfx.h and the Spleen faces in panel_font.h.
//
// Every rule here is one a reading of the code would pass and the glass
// would show: a glyph drawn a column out, a figure that leaves the tail of a
// longer one behind, a name cut mid-word, a fade that never reaches the
// bar, an antenna lit at the wrong height, two LEDs that touch, a layout
// whose boxes overlap or run off the glass, a list that loses a caller, a
// queue that sends a rectangle twice or loses one, or a band that is bigger
// than the DMA buffer it goes out of.
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../src/plugins/panel_gfx.h"

using namespace panelgfx;
using namespace panelgfx::tok;

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

static bool cut(const char* s, int fit, const char* want) {
    char out[64];
    cutWords(s, fit, out, sizeof(out));
    if (strcmp(out, want)) printf("        got \"%s\", want \"%s\"\n", out, want);
    return !strcmp(out, want);
}

template <typename F>
static bool fmtIs(F f, uint32_t v, const char* want) {
    char out[24];
    f(v, out, sizeof(out));
    if (strcmp(out, want)) printf("        got \"%s\", want \"%s\"\n", out, want);
    return !strcmp(out, want);
}

int main() {
    printf("Panel drawing\n");

    // --- colours -----------------------------------------------------------
    check("RGB565 white is 0xFFFF and black 0", rgb(255, 255, 255) == 0xFFFF && rgb(0, 0, 0) == 0);
    check("red is the top five bits, green the middle six, blue the low five",
          rgb(255, 0, 0) == 0xF800 && rgb(0, 255, 0) == 0x07E0 && rgb(0, 0, 255) == 0x001F);
    check("the site's tokens pack as the report says: bar, band, track, rule, surface",
          kBar == 0x196F && kBand == 0x08C9 && kTrack == 0x4BF3 && kRule == 0x2967 && kSurface == 0x10A3);
    check("and the rest: ink, dim, faint, live, warm, dial, busy, risk, yellow",
          kInk == 0xCE59 && kDim == 0x8C51 && kFaint == 0x6B4E && kLive == 0x5EEF && kWarm == 0xE549 &&
          kDial == 0x7EBF && kBusy == 0xEC4B && kRisk == 0xE36D && kYellow == 0xFE8B);
    {
        Rgb8 w = unpack(0xFFFF), k = unpack(0);
        check("unpacked, white is 255s and black 0s", w.r == 255 && w.g == 255 && w.b == 255 &&
                                                      k.r == 0 && k.g == 0 && k.b == 0);
        check("and a token comes back as itself", rgb(unpack(kDial).r, unpack(kDial).g, unpack(kDial).b) == kDial);
    }
    // The header's fade: k = 0 is the text, 8 the bar, and every step between
    // moves each channel toward the bar and never past it.
    check("the fade at step 0 is the text", mix(kInk, kBar, 0, 8) == kInk);
    check("and at step 8 the bar: the text is gone into it", mix(kInk, kBar, 8, 8) == kBar);
    {
        bool toward = true;
        Rgb8 prev = unpack(kInk);
        for (int k = 1; k <= 8; ++k) {
            Rgb8 c = unpack(mix(kInk, kBar, k, 8));
            if (c.r > prev.r || c.g > prev.g || c.b > prev.b + 8) toward = false;   // ink to a darker blue
            prev = c;
        }
        check("each step on the way from ink to the bar is darker, never lighter", toward);
        Rgb8 mid = unpack(mix(kLive, kBar, 4, 8)), a = unpack(kLive), b = unpack(kBar);
        check("half way is half way, to a step of the packing",
              abs(mid.g - (a.g + b.g) / 2) <= 4 && abs(mid.r - (a.r + b.r) / 2) <= 8);
    }
    check("a third of a colour, for an LED's glow", unpack(scale(0xFFFF, 1, 3)).g == 85 &&
                                                      scale(0, 1, 3) == 0);
    check("isqrt at the edges", isqrt(0) == 0 && isqrt(1) == 1 && isqrt(15) == 3 && isqrt(16) == 4 &&
                                isqrt(65535) == 255 && isqrt(0xFFFFFFFFu) == 65535);
    // A strip pixel on glass: the lights' white is 255 x pct / 100 per channel.
    check("white at 30% is full on glass, near enough", glassLevel(76, 30) >= 253);
    check("and at 100%", glassLevel(255, 100) == 255);
    check("at the shipped 10%, a readable 144", glassLevel(25, 10) == 144);
    check("at 1%, still there", glassLevel(2, 1) == 36);
    check("dark stays dark", glassLevel(0, 10) == 0 && glassLevel(25, 0) == 0);
    check("orange at 100% stays orange",
          glassLevel(255, 100) == 255 && glassLevel(128, 100) == 128 && glassLevel(0, 100) == 0);
    bool mono = true;
    for (int p = 1; p <= 100; ++p)
        for (int v = 1; v < 256; ++v)
            if (glassLevel(static_cast<uint8_t>(v), static_cast<uint8_t>(p)) <
                glassLevel(static_cast<uint8_t>(v - 1), static_cast<uint8_t>(p))) mono = false;
    check("brighter in is never dimmer out, at any brightness", mono);

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
        check("the board's name counts nine glyphs, not ten bytes' worth",
              glyphs("\xC2\xB5nleashed") == 9 && textWidth("\xC2\xB5nleashed", false) == 72);
    }

    // --- text ---------------------------------------------------------------
    {
        Glass g(40, 20);
        int w = text(g.c, 0, 0, "A", 0xFFFF, 0x0000, false, 40);
        check("one small glyph is 8 wide", w == 8);
        // Spleen's 8x16 'A': row 2 is 0x7C, so columns 1 to 5 set.
        bool rowOk = g.at(0, 2) == 0 && g.at(1, 2) == 0xFFFF && g.at(5, 2) == 0xFFFF && g.at(6, 2) == 0;
        check("drawn from the font's bits, left pixel first", rowOk);
        check("nothing drawn past the cell", g.at(8, 2) == 0x1234 && g.at(3, 16) == 0x1234);
        Glass h(40, 20);
        w = text(h.c, 0, 0, "ABCDEFGH", 0xFFFF, 0, false, 20);
        check("a string is cut at its box, a whole glyph at a time", w == 16 && h.at(16, 2) == 0x1234);
        Glass e(12, 10);
        text(e.c, 6, 4, "W", 0xFFFF, 0, false, 40);
        check("a glyph at the canvas's corner is drawn up to it and no further",
              (e.at(11, 9) == 0xFFFF || e.at(11, 9) == 0) && e.at(5, 9) == 0x1234);
    }
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
        check("right aligned ends at the box's edge (the clock)", right);
    }

    // --- the header's words ------------------------------------------------
    {
        Layout P = layout(172, 320), L = layout(320, 172);
        check("portrait: the slot holds 21 glyphs, the longest IPv4 address and port",
              P.slot.w / kSmallW == 21 && textWidth("255.255.255.255:65535", false) <= P.slot.w);
        check("landscape: 39", L.slot.w / kSmallW == 39);
        check("a name that fits is left whole", cut("The Rusty Antenna BBS", 21, "The Rusty Antenna BBS"));
        check("a long one is cut at a word, not in one", cut("The Rusty Antenna", 11, "The Rusty"));
        check("a space right after the cut is a word boundary already",
              cut("Hello World Again", 11, "Hello World"));
        check("no space past half: cut at the glyph, not to a stub", cut("ab cdefghijklmnop", 11, "ab cdefghij"));
        check("one long word: cut at the glyph", cut("Supercalifragilistic board", 11, "Supercalifr"));
        check("the micro sign is one glyph when counting", cut("\xC2\xB5nleashed BBS HQ", 12, "\xC2\xB5nleashed"));
        check("the cut never keeps a trailing space", cut("abcdef   ghijklmnopq", 10, "abcdef"));
        check("uptime: minutes under an hour", fmtIs(fmtUptime, 14 * 60, "14m"));
        check("hours and minutes under a day", fmtIs(fmtUptime, 3 * 3600 + 14 * 60, "3h 14m"));
        check("days and hours under a hundred days", fmtIs(fmtUptime, 12 * 86400 + 3 * 3600 + 59, "12d 3h"));
        check("days alone after", fmtIs(fmtUptime, 123u * 86400u, "123d"));
        check("the card: 7.5 GB, 29 GB, 512 MB",
              fmtIs(fmtFree, 7680u * 1024u, "7.5 GB") && fmtIs(fmtFree, 29u * 1024u * 1024u, "29 GB") &&
              fmtIs(fmtFree, 512u * 1024u, "512 MB"));
        check("page C at its longest fits the portrait slot",
              textWidth("up 99d 23h  1023 MB", false) <= P.slot.w);
        check("time on: 4m, 51m, 2h, 1d", fmtIs(fmtOnFor, 4 * 60000, "4m") &&
              fmtIs(fmtOnFor, 51 * 60000, "51m") && fmtIs(fmtOnFor, 150 * 60000, "2h") &&
              fmtIs(fmtOnFor, 30u * 3600000u, "1d"));
        check("and never more than three glyphs", fmtIs(fmtOnFor, 4000000000u, "46d") &&
              fmtIs(fmtOnFor, 59 * 60000 + 59000, "59m") && fmtIs(fmtOnFor, 1439 * 60000, "23h"));
    }

    // --- the status row ------------------------------------------------------
    {
        const Glyph* all[] = { &kGlyphSd, &kGlyphSdNone, &kGlyphBell, &kGlyphMail, &kGlyphUpload,
                               &kGlyphLock, &kGlyphTower, &kGlyphStaff, &kGlyphWarn, &kGlyphSlow };
        bool tidy = true;
        for (const Glyph* g : all) {
            if (g->w > 16 || g->h > 12 || g->h > 16) tidy = false;
            for (int r = 0; r < g->h; ++r) if (g->rows[r] & (0xFFFFu >> g->w)) tidy = false;
        }
        check("every glyph's bits lie inside its own width, and it fits the 16 px row", tidy);

        Status s;
        Packed p = pack(s, 4);
        check("a quiet board shows one glyph: the card, hollow and dim with none",
              p.n == 1 && p.which[0] == G_SD && p.glyph[0] == &kGlyphSdNone && p.colour[0] == kDim);
        s.card = Status::CARD_IN;
        p = pack(s, 4);
        check("with a card it is filled, in dial", p.glyph[0] == &kGlyphSd && p.colour[0] == kDial);
        s.card = Status::CARD_ERROR;
        p = pack(s, 4);
        check("and red on a card error", p.glyph[0] == &kGlyphSd && p.colour[0] == kRisk);
        s.ring = s.mail = s.upload = s.backup = s.warn = s.slow = true;
        s.listing = Status::LIST_ONLINE;
        s.staff = Status::STAFF_SYSOP;
        p = pack(s, 4);
        bool order = p.n == G_COUNT;
        for (uint8_t i = 0; i < p.n && order; ++i) if (p.which[i] != i) order = false;
        check("all nine, in the report's order", order);
        check("all nine end short of the antenna: under 114, in 104",
              p.end < 114 && p.end - 4 == 104 && p.end <= layout(172, 320).glyphs.x + layout(172, 320).glyphs.w);
        bool apart = true;
        for (uint8_t i = 1; i < p.n; ++i)
            if (p.x[i] != p.x[i - 1] + p.glyph[i - 1]->w + kGlyphGap) apart = false;
        check("each 3 px after the last", apart);
        bool centred = true;
        for (uint8_t i = 0; i < p.n; ++i) {
            Rect b = glyphBox(p, i, 24);
            if (b.y < 24 || b.y + b.h > 40 || abs((b.y - 24) - (40 - b.y - b.h)) > 1) centred = false;
        }
        check("each centred in the band's 16 px row", centred);
        check("the person is the sysop's red, or a co-sysop's yellow",
              p.colour[G_STAFF] == kRisk &&
              pack(Status{ Status::CARD_NONE, false, false, false, false, 0, Status::STAFF_CO, false, false }, 4)
                  .colour[1] == kYellow);
        Status t;
        t.listing = Status::LIST_WAITING;
        Status u;
        u.listing = Status::LIST_TROUBLE;
        check("the tower: live online, warm waiting, risk otherwise",
              p.colour[G_TOWER] == kLive && pack(t, 4).colour[1] == kWarm && pack(u, 4).colour[1] == kRisk);

        Glass g(172, 64, kBand);
        for (uint8_t i = 0; i < p.n; ++i) drawGlyphAt(g.c, p, i, 24);
        bool within = true;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 172; ++x)
                if (g.at(x, y) != kBand && (x < 4 || x >= 114 || y < 24 || y >= 40)) within = false;
        check("drawn, the row stays inside its strip", within);
        Glass h(20, 20, kBand);
        drawGlyphAt(h.c, pack(Status{}, 2), 0, 2);
        check("the hollow card is an outline: its middle is the band", h.at(6, 10) == kBand && h.at(2, 10) == kDim);
    }

    // --- the antenna (revision 2) -----------------------------------------------
    {
        const int dbs[]  = { -90, -89, -87, -75, -67, -62, -60, -50, -49, -100 };
        const int want[] = {   0,   0,   1,   6,   9,  11,  12,  16,  16,    0 };
        bool heights = true;
        for (size_t i = 0; i < sizeof(dbs) / sizeof(dbs[0]); ++i)
            if (signalFill(dbs[i]) != want[i]) {
                printf("        %d dBm: %d, want %d\n", dbs[i], signalFill(dbs[i]), want[i]);
                heights = false;
            }
        check("the fill at -90 -89 -87 -75 -67 -62 -60 -50 -49: 0 0 1 6 9 11 12 16 16", heights);
        check("not joined (0) fills nothing", signalFill(0) == 0);
        check("its colour: live from -67, warm to -75, risk below",
              signalColour(-50) == kLive && signalColour(-67) == kLive && signalColour(-68) == kWarm &&
              signalColour(-75) == kWarm && signalColour(-76) == kRisk);
        // Drawn: the silhouette is 48 pixels, the ball 22 and the mast 22 of them... counted here.
        auto draw = [](int h, uint16_t col, bool joined, int& lit, int& ghost, int& other, bool& clean) {
            Glass g(10, 20, 0x1234);
            drawSignal(g.c, 2, 2, h, col, joined, kBand);
            lit = ghost = other = 0;
            clean = true;
            for (int y = 0; y < 20; ++y)
                for (int x = 0; x < 10; ++x) {
                    uint16_t v = g.at(x, y);
                    bool in = x >= 2 && x < 8 && y >= 2 && y < 18;
                    if (!in) { if (v != 0x1234) clean = false; continue; }
                    const bool on = (kAntenna[y - 2] >> (5 - (x - 2))) & 1u;
                    if (!on) { if (v != kBand) clean = false; continue; }
                    if (v == col) ++lit; else if (v == kFaint) ++ghost; else ++other;
                    // lit rows are the bottom h, ghost the rest
                    if (joined && ((y - 2) >= 16 - h) != (v == col)) clean = false;
                }
        };
        int lit, ghost, other;
        bool clean;
        draw(0, kRisk, true, lit, ghost, other, clean);
        check("h = 0: the whole antenna is the faint ghost", lit == 0 && ghost == 48 && !other && clean);
        draw(8, kWarm, true, lit, ghost, other, clean);
        check("h = 8: the mast's bottom eight rows lit, the rest ghost", lit == 16 && ghost == 32 && !other && clean);
        draw(16, kLive, true, lit, ghost, other, clean);
        check("h = 16: all of it lit, the ball green", lit == 48 && ghost == 0 && !other && clean);
        draw(0, kRisk, false, lit, ghost, other, clean);
        check("not joined: the whole silhouette in risk, no ghost", lit == 48 && ghost == 0 && !other && clean);
        check("and nothing drawn outside its 6 x 16 cell, the band inside it", clean);
    }

    // --- the LEDs ------------------------------------------------------------
    {
        const Rect boxes[] = { R(4, 298, 164, 16), R(10, 154, 300, 16) };
        bool ok = true;
        for (const Rect& box : boxes)
            for (uint8_t n = 1; n <= 16; ++n)
                for (uint8_t i = 0; i < n; ++i) {
                    Led l = ledAt(box, i, n);
                    if (!contains(box, l.cell) || !contains(l.cell, l.led) || l.led.w >= l.cell.w ||
                        l.led.w != l.led.h || l.led.w < 6) ok = false;
                    for (uint8_t k = 0; k < i; ++k)
                        if (overlap(l.cell, ledAt(box, k, n).cell) || overlap(l.led, ledAt(box, k, n).led)) ok = false;
                }
        check("1 to 16 LEDs in both boxes: inside, square, never touching", ok);
        auto cellSide = [](const Rect& box, uint8_t n, int& cell, int& side, int& width) {
            Led a = ledAt(box, 0, n), b = ledAt(box, static_cast<uint8_t>(n - 1), n);
            cell = a.cell.w;
            side = a.led.w;
            width = b.cell.x + b.cell.w - a.cell.x;
        };
        int cell, side, width;
        cellSide(boxes[0], 10, cell, side, width);
        check("portrait, ten: cells of 16, LEDs of 12, 160 wide", cell == 16 && side == 12 && width == 160);
        cellSide(boxes[0], 11, cell, side, width);
        check("eleven: 14 and 10, 154", cell == 14 && side == 10 && width == 154);
        cellSide(boxes[0], 16, cell, side, width);
        check("sixteen: 10 and 6, 160", cell == 10 && side == 6 && width == 160);
        cellSide(boxes[1], 16, cell, side, width);
        check("landscape, sixteen: still 16", cell == 16 && side == 12);
        Led c = ledAt(boxes[0], 0, 10);
        check("the row centred in its box", c.cell.x - boxes[0].x == (boxes[0].w - 160) / 2);

        Glass g(172, 320, 0x1234);
        drawLed(g.c, c, kLive);
        const int cx = c.led.x + c.led.w / 2, cy = c.led.y + c.led.h / 2;
        check("a lit LED: its core in the colour", g.at(cx, cy) == kLive && g.at(c.led.x + 1, c.led.y + 1) == kLive);
        check("its edge the glow, a third of it", g.at(c.led.x, c.led.y) == scale(kLive, 1, 3));
        check("the rest of its cell black", g.at(c.cell.x, c.cell.y) == kBg);
        drawLed(g.c, c, 0);
        check("off: a ring in the rule's grey round a near-black fill, inset one more",
              g.at(c.led.x, c.led.y) == kBg && g.at(c.led.x + 1, c.led.y + 1) == kRule &&
              g.at(cx, cy) == kSurface);
        bool inCell = true;
        for (int y = 0; y < 320; ++y)
            for (int x = 0; x < 172; ++x)
                if (g.at(x, y) != 0x1234 && !(x >= c.cell.x && x < c.cell.x + c.cell.w &&
                                              y >= c.cell.y && y < c.cell.y + c.cell.h)) inCell = false;
        check("and nothing drawn outside its own cell", inCell);
    }

    // --- the layout ------------------------------------------------------------
    for (int turn = 0; turn < 2; ++turn) {
        const uint16_t w = turn ? 320 : 172, h = turn ? 172 : 320;
        Layout L = layout(w, h);
        char what[96];
        bool in = true;
        const Rect fixed[] = { L.bar, L.band, L.track, L.slot, L.glyphs, L.ant, L.clock, L.headIcon, L.head,
                               L.rule1, L.rule2, L.sys, L.leds };
        for (const Rect& r : fixed) if (empty(r) || !inside(r, w, h)) in = false;
        for (uint8_t k = 0; k < L.slots; ++k) if (!inside(listBox(L, k), w, h)) in = false;
        snprintf(what, sizeof(what), "%ux%u: every box on the glass", w, h);
        check(what, in);
        bool apart = true;
        for (uint8_t a = 0; a < F_COUNT; ++a) {
            Rect ra = fieldBox(L, a);
            if (empty(ra)) continue;
            for (uint8_t b = static_cast<uint8_t>(a + 1); b < F_COUNT; ++b)
                if (!empty(fieldBox(L, b)) && overlap(ra, fieldBox(L, b))) {
                    printf("        fields %u and %u overlap\n", a, b);
                    apart = false;
                }
            for (const Rect& r : { L.track, L.rule1, L.rule2, L.leds, L.colRule })
                if (!empty(r) && overlap(ra, r)) { printf("        field %u over a rule\n", a); apart = false; }
        }
        snprintf(what, sizeof(what), "%ux%u: no two fields overlap, nor the rules, the rail or the LEDs", w, h);
        check(what, apart);
        snprintf(what, sizeof(what), "%ux%u: the header is two rows and a rail, 43 px", w, h);
        check(what, L.bar.h == 22 && L.band.y == 22 && L.band.h == 20 && L.track.y == 42 && L.track.h == 1);
        snprintf(what, sizeof(what), "%ux%u: the antenna 10 left of the clock, the clock 4 from the edge", w, h);
        check(what, L.ant.x == (turn ? 266 : 118) && L.ant.y == 24 && L.ant.w == 6 && L.ant.h == 16 &&
                    L.clock.x == (turn ? 276 : 128) && L.clock.x + L.clock.w == w - 4);
        snprintf(what, sizeof(what), "%ux%u: the glyph strip ends clear of the antenna", w, h);
        check(what, L.glyphs.x == 4 && L.glyphs.x + L.glyphs.w <= L.ant.x - 4 && L.glyphs.w == 110);
    }
    {
        Layout P = layout(172, 320);
        check("portrait: ten slots, all of them open to callers", P.slots == 10 && P.callerSlots == 10);
        check("from 68 on a pitch of 20, the last ending above the rule at 266",
              P.list[0].y == 68 && P.list[9].y == 248 && P.list[9].y + 16 <= P.rule1.y && P.rule1.y == 266);
        check("the system row at 268, the rule at 292, the LEDs at 298",
              P.sys.y == 268 && P.rule2.y == 292 && P.leds.y == 298 && P.leds.x == 4 && P.leds.w == 164);
        check("each slot owns the air above it, where the rule under the callers goes",
              P.gaps && listBox(P, 0).y == 64 && listBox(P, 0).h == 20);
        Layout L = layout(320, 172);
        check("landscape: three caller rows at 68, 88 and 108",
              L.callerSlots == 3 && L.list[0].y == 68 && L.list[2].y == 108 && L.list[0].w == 152);
        check("four recent rows at 48 to 108 in the right column",
              L.slots == 7 && L.list[3].y == 48 && L.list[6].y == 108 && L.list[3].x == 166 && L.list[3].w == 150);
        check("the column rule at 160, the system row at 130 with room for peak, the LEDs at 154",
              L.colRule.x == 160 && L.colRule.y == 48 && L.colRule.h == 76 && L.sys.y == 130 && L.sysFigs == 3 &&
              L.leds.x == 10 && L.leds.y == 154 && L.leds.w == 300);
        check("a caller row's handle has 12 glyphs in portrait, 11 in landscape",
              (P.list[0].w - 32 - 24 - 8) / 8 == 12 && (L.list[0].w - 32 - 24 - 8) / 8 == 11);
    }

    // --- the two lists (revision 2) -------------------------------------------
    {
        Layout P = layout(172, 320);
        Alloc a = allocate(P, 0);
        check("portrait, nobody on: no callers, ten recent, no rule",
              a.callers == 0 && a.names == 0 && !a.more && a.recent0 == 0 && a.recentN == 10 && !a.rule);
        a = allocate(P, 1);
        check("one on: one name, nine recent, the rule between", a.callers == 1 && a.names == 1 &&
              a.recent0 == 1 && a.recentN == 9 && a.rule);
        a = allocate(P, 4);
        check("four on: four names, six recent, the rule", a.callers == 4 && a.names == 4 && !a.more &&
              a.recent0 == 4 && a.recentN == 6 && a.rule);
        a = allocate(P, 10);
        check("ten on: ten names, no recent rows, no rule", a.callers == 10 && a.names == 10 && !a.more &&
              a.recentN == 0 && !a.rule);
        a = allocate(P, 11);
        check("eleven on: nine names and \"+2 more\" in the tenth, no recent rows",
              a.callers == 10 && a.names == 9 && a.more == 2 && a.recentN == 0 && !a.rule);
        bool sum = true;
        for (uint8_t on = 0; on <= 12; ++on) {
            Alloc b = allocate(P, on);
            if (b.callers + b.recentN != P.slots || b.names + b.more != on) sum = false;
        }
        check("at any count every slot is used once, and every caller is named or counted", sum);
        Layout L = layout(320, 172);
        a = allocate(L, 0);
        check("landscape, nobody on: the recent column keeps its four", a.callers == 0 && a.recent0 == 3 &&
              a.recentN == 4 && !a.rule);
        a = allocate(L, 3);
        check("three on: three names", a.callers == 3 && a.names == 3 && !a.more && a.recentN == 4);
        a = allocate(L, 4);
        check("four on: two names and \"+2 more\"", a.callers == 3 && a.names == 2 && a.more == 2 && a.recentN == 4);
        a = allocate(L, 11);
        check("eleven on: two names and \"+9 more\"", a.names == 2 && a.more == 9);
    }

    // --- orientation: where the USB plug is ------------------------------------
    {
        uint8_t o = ORIENT_UP;
        check("the four words, in any case",
              orientFromWord("up", o) && o == ORIENT_UP && orientFromWord("LEFT", o) && o == ORIENT_LEFT &&
              orientFromWord("Right", o) && o == ORIENT_RIGHT && orientFromWord("down", o) && o == ORIENT_DOWN);
        o = ORIENT_LEFT;
        check("anything else is refused and the value kept",
              !orientFromWord("sideways", o) && !orientFromWord("", o) && !orientFromWord("90", o) &&
              !orientFromWord("upp", o) && !orientFromWord("u", o) && o == ORIENT_LEFT);
        check("the words are CONFIG's choices, in the same order",
              !strcmp(kOrientWords, "up|left|right|down") && !strcmp(orientWord(ORIENT_RIGHT), "right") &&
              !strcmp(orientWord(99), "up"));

        // This board's glass: 172 x 320 at 34, 0 with the plug up, wired
        // mirrored. The demo's portrait and espp's landscape, and the demo's
        // four MADCTL settings (LVGL_Driver.c).
        struct Want { uint8_t o; uint16_t rot; bool mv, mx, my; uint16_t w, h, xg, yg; bool land; };
        const Want want[] = {
            { ORIENT_UP,    0,   false, true,  false, 172, 320, 34, 0,  false },
            { ORIENT_LEFT,  270, true,  false, false, 320, 172, 0,  34, true  },
            { ORIENT_RIGHT, 90,  true,  true,  true,  320, 172, 0,  34, true  },
            { ORIENT_DOWN,  180, false, false, true,  172, 320, 34, 0,  false },
        };
        for (const Want& w : want) {
            Scan s = scanFor(w.o, true, 172, 320, 34, 0);
            char what[96];
            snprintf(what, sizeof(what), "USB %s: rotation %u, MV %d MX %d MY %d, %ux%u at %u,%u",
                     orientWord(w.o), w.rot, w.mv, w.mx, w.my, w.w, w.h, w.xg, w.yg);
            check(what, s.rotation == w.rot && s.mv == w.mv && s.mx == w.mx && s.my == w.my &&
                        s.w == w.w && s.h == w.h && s.xgap == w.xg && s.ygap == w.yg);
            Layout L = layout(s.w, s.h);
            snprintf(what, sizeof(what), "USB %s: the %s layout", orientWord(w.o), w.land ? "landscape" : "portrait");
            check(what, L.land == w.land && L.slots == (w.land ? 7 : 10));
            snprintf(what, sizeof(what), "USB %s: the window inside the controller's RAM", orientWord(w.o));
            check(what, (s.mv ? s.xgap + s.w <= kRamRows && s.ygap + s.h <= kRamCols
                              : s.xgap + s.w <= kRamCols && s.ygap + s.h <= kRamRows));
        }
        // A glass wired the other way: left and right change places, since a
        // mirror turns the picture the other way round.
        check("unmirrored, left is rotation 90 and right 270",
              scanFor(ORIENT_LEFT, false, 172, 320, 34, 0).rotation == 90 &&
              scanFor(ORIENT_RIGHT, false, 172, 320, 34, 0).rotation == 270);
        // A glass off centre: the gap moves to the other end of an axis run
        // backwards, so the window lands on the same memory columns.
        Scan up = scanFor(ORIENT_UP, true, 170, 320, 30, 0), dn = scanFor(ORIENT_DOWN, true, 170, 320, 30, 0);
        check("off centre: the plug up keeps the settings' own gaps", up.xgap == 30 && up.ygap == 0);
        check("upside down, the column gap moves to the other end: the same columns",
              dn.xgap == 40 && dn.xgap + 170 + up.xgap == kRamCols);
        Scan lf = scanFor(ORIENT_LEFT, true, 170, 320, 30, 0), rt = scanFor(ORIENT_RIGHT, true, 170, 320, 30, 0);
        check("and on its side the column gap is the y gap, from one end or the other",
              lf.ygap == 40 && rt.ygap == 30 && lf.xgap == 0 && rt.xgap == 0);
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

        Dirty mid;
        mid.add(R(0, 0, 320, 64));
        mid.next(320 * 16, r);
        mid.add(R(0, 0, 320, 8));
        check("a change to rows already sent is sent again", mid.n == 1 && mid.q[0].h == 8);

        // The busiest steady field, the header's slot, is one band a step.
        Layout P = layout(172, 320);
        check("a fade step of the slot is one band", static_cast<uint32_t>(P.slot.w) * P.slot.h <= 320u * 16u);
        check("so is a portrait list slot with its air, and the LED row",
              static_cast<uint32_t>(listBox(P, 0).w) * listBox(P, 0).h <= 320u * 16u &&
              static_cast<uint32_t>(P.leds.w) * P.leds.h <= 320u * 16u);
    }

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
