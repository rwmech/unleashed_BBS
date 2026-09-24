/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/info.cpp
 * Module:       Plugins / information pages
 *
 * Purpose:      Ten pages the sysop writes, 0 to 9: the rules, the news,
 *               what the board is for, why it was down. DDial had them as
 *               /I0 to /I9 and every board since has had something like
 *               them. INFO at the main prompt, /i in the room.
 *
 *                 INFO             the list
 *                 INFO 3           page 3, paged with [More]
 *                 INFO 3 EDIT      write page 3 (the plugin's write level)
 *                 INFO 3 CLEAR     empty it (the same level)
 *
 * Design:       Set up in CONFIG, written with the message editor, which is
 *               the split Rob asked for: "use the config engine to setup
 *               and the forum/email editor for creating them". A page's
 *               title and who may read it are a packed setting,
 *                 page3 = House rules | all
 *               so CONFIG opens it as a page of its own like a forum or a
 *               file area. The text is a file, <userdata>/p/info/3.txt,
 *               on internal flash rather than the card: these are the
 *               board's own words and a board without a card has to have
 *               them too.
 *
 *               A plugin rather than core code, because the settings
 *               editor already knows how to give a plugin's packed settings
 *               a page each, and the core does not. PF_ON, so a fresh board
 *               has it; enabled = no switches it off.
 *
 *               A slot with no title and no text is not a page, and a page
 *               a caller may not read answers in exactly the words a page
 *               that does not exist does, so the numbers cannot be probed
 *               for what they hide. Both rules are the file areas'.
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     info.h, CLAUDE.md "What things are called"
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

#include "info.h"
#include "../config.h"
#include "../core/bbs.h"
#include "../core/bbs_util.h"
#include "../core/claims.h"
#include "../core/codes.h"
#include "../core/compose.h"
#include "../core/composer.h"
#include "../core/plugin.h"
#include "../platform/platform.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace bbsu;

namespace {

constexpr char    kName[]    = "info";
constexpr uint8_t kPages     = 10;
constexpr uint8_t kTitleMax  = 24;      // "/i0  " plus 24 is 29, inside 39
constexpr uint8_t kSlots     = BBS_MAX_NODES + 2;

struct Page {
    char      title[kTitleMax + 1] = {};
    PlugLevel read = PlugLevel::Nobody;   // Nobody: the plugin's read level
};

Page    g_page[kPages];
uint8_t g_index = 0xFF;

// Reading one page as a paged list at the prompt: which page, how far in,
// and the colour the next row starts in. Per caller, because two people can
// be reading two pages at once; small, because a Painter is a dozen bytes.
uint16_t       g_off[kSlots]   = {};
uint8_t        g_phase[kSlots] = {};    // 0 title, 1 body, 2 rule, 3 done
uint8_t        g_which[kSlots] = {};
codes::Painter g_pt[kSlots];

// Writing a page. One at a time, board-wide, so one of these rather than
// twelve: claims::Res::Info says whose it is.
compose::Body  g_body;
uint8_t        g_editing = 0xFF;

uint8_t slotOf(const Session& s) { return s.id < kSlots ? s.id : 0; }

// pagePath for writing (checks the reserve, makes the folder), readPage
// for reading. See plugins::readPath for why they differ.
bool pagePath(uint8_t n, char* out, size_t len) {
    char f[8];
    snprintf(f, sizeof(f), "%u.txt", static_cast<unsigned>(n));
    return plugins::path(g_index, f, out, len);
}

bool readPage(uint8_t n, char* out, size_t len) {
    char f[8];
    snprintf(f, sizeof(f), "%u.txt", static_cast<unsigned>(n));
    return plugins::readPath(g_index, f, out, len);
}

// Which pages have text, one bit each. Found at start() and kept by the
// editor and CLEAR, so knowing costs no filesystem at all: every login asks
// how many pages there are, and asking the filesystem ten times per login
// for a feature a board may never use is the wrong price.
uint16_t g_textMask = 0;

bool fileHasText(uint8_t n) {
    char p[96];
    if (!readPage(n, p, sizeof(p))) return false;
    FILE* f = fopen(p, "rb");
    if (!f) return false;
    int c = fgetc(f);
    fclose(f);
    return c != EOF;
}

bool hasText(uint8_t n) { return n < kPages && (g_textMask & (1u << n)); }

bool exists(uint8_t n) { return n < kPages && (g_page[n].title[0] || hasText(n)); }

bool mayRead(const Session& s, uint8_t n) {
    PlugLevel lv = g_page[n].read;
    if (lv == PlugLevel::Nobody) lv = plugins::levelFor(g_index, 0);
    return plugins::mayUse(s, lv);
}

bool mayWrite(const Session& s) {
    return plugins::mayUse(s, plugins::levelFor(g_index, 1));
}

bool visible(const Session& s, uint8_t n) { return exists(n) && mayRead(s, n); }

uint8_t visibleCount(const Session& s) {
    uint8_t c = 0;
    for (uint8_t n = 0; n < kPages; ++n) if (visible(s, n)) ++c;
    return c;
}

const char* titleOf(uint8_t n, char* buf, size_t len) {
    if (g_page[n].title[0]) return g_page[n].title;
    snprintf(buf, len, "Page %u", static_cast<unsigned>(n));
    return buf;
}

// load: a page's text into the caller's compose buffer, which is free while
// they read: nobody is writing a message and reading a page at once.
uint16_t load(Session& s, uint8_t n) {
    s.compose[0] = '\0';
    char p[96];
    if (!readPage(n, p, sizeof(p))) return 0;
    FILE* f = fopen(p, "rb");
    if (!f) return 0;
    size_t got = fread(s.compose, 1, BBS_COMPOSE_MAX, f);
    fclose(f);
    s.compose[got] = '\0';
    return static_cast<uint16_t>(got);
}

void line(Session& s, Color c, const char* text) {
    s.term.color(s.tl, c);
    s.term.text(s.tl, text);
    s.term.nl(s.tl);
}

// ---------------------------------------------------------------------------
// Settings: "page3 = House rules | all". A level the ladder does not know is
// logged and left to the fallback rather than guessed at, the forums' rule.
// ---------------------------------------------------------------------------
void readKey(void*, const char* key, const char* value) {
    if (strncmp(key, "page", 4) || key[4] < '0' || key[4] > '9' || key[5]) return;
    uint8_t n = static_cast<uint8_t>(key[4] - '0');
    Page pg;
    const char* bar = strchr(value, '|');
    size_t tlen = bar ? static_cast<size_t>(bar - value) : strlen(value);
    while (tlen && value[tlen - 1] == ' ') --tlen;
    const char* t = value;
    while (tlen && *t == ' ') { ++t; --tlen; }
    snprintf(pg.title, sizeof(pg.title), "%.*s",
             static_cast<int>(tlen > kTitleMax ? kTitleMax : tlen), t);
    if (bar) {
        char lv[12];
        const char* l = bar + 1;
        while (*l == ' ') ++l;
        snprintf(lv, sizeof(lv), "%s", l);
        for (size_t i = strlen(lv); i && lv[i - 1] == ' '; --i) lv[i - 1] = '\0';
        if (lv[0] && !plugins::levelFromText(lv, pg.read))
            plat::log("info: page%u read level '%s' is not a level, using the fallback",
                      static_cast<unsigned>(n), lv);
    }
    g_page[n] = pg;
}

void setting(const char* key, char* out, size_t n) {
    out[0] = '\0';
    if (strncmp(key, "page", 4) || key[4] < '0' || key[4] > '9' || key[5]) return;
    const Page& pg = g_page[key[4] - '0'];
    if (!pg.title[0] && pg.read == PlugLevel::Nobody) return;
    if (pg.read == PlugLevel::Nobody) snprintf(out, n, "%s", pg.title);
    else snprintf(out, n, "%s | %s", pg.title, plugins::levelName(pg.read));
}

const PluginSetting kSettings[] = {
    { "page0", "Page 0", PS_TEXT, 0, 0, 63 },
    { "page1", "Page 1", PS_TEXT, 0, 0, 63 },
    { "page2", "Page 2", PS_TEXT, 0, 0, 63 },
    { "page3", "Page 3", PS_TEXT, 0, 0, 63 },
    { "page4", "Page 4", PS_TEXT, 0, 0, 63 },
    { "page5", "Page 5", PS_TEXT, 0, 0, 63 },
    { "page6", "Page 6", PS_TEXT, 0, 0, 63 },
    { "page7", "Page 7", PS_TEXT, 0, 0, 63 },
    { "page8", "Page 8", PS_TEXT, 0, 0, 63 },
    { "page9", "Page 9", PS_TEXT, 0, 0, 63 },
};

// ---------------------------------------------------------------------------
// The list and one page, at the prompt.
// ---------------------------------------------------------------------------
void index(Bbs& b, Session& s) {
    uint8_t count = visibleCount(s);
    if (!count) {
        line(s, Color::Cyan, "--> This board has no information pages yet.");
        if (mayWrite(s)) line(s, Color::Grey, "INFO n EDIT writes one.");
        return;
    }
    char right[16];
    snprintf(right, sizeof(right), "%u page%s", static_cast<unsigned>(count), count == 1 ? "" : "s");
    b.rowTitle(s, "Information", right);
    char buf[16];
    for (uint8_t n = 0; n < kPages; ++n) {
        if (!visible(s, n)) continue;
        char num[4];
        snprintf(num, sizeof(num), "%u", static_cast<unsigned>(n));
        s.term.color(s.tl, Color::Yellow);
        s.term.text(s.tl, num);
        s.term.text(s.tl, "  ");
        s.term.color(s.tl, Color::White);
        s.term.text(s.tl, titleOf(n, buf, sizeof(buf)));
        s.term.nl(s.tl);
    }
    b.rowRule(s);
    line(s, Color::Grey, mayWrite(s) ? "INFO n reads one. INFO n EDIT writes."
                                     : "INFO n reads one.");
}

void show(Bbs& b, Session& s, uint8_t n) {
    load(s, n);
    uint8_t sl = slotOf(s);
    g_which[sl] = n;
    g_off[sl]   = 0;
    g_phase[sl] = 0;
    g_pt[sl].begin(Color::Grey, !s.bellOff);
    b.startPluginList(s, g_index);
}

// rows: one row of a page per call. The core pages it, so a page longer
// than the screen stops at [More] like every other list on the board.
bool rows(Session& s) {
    Bbs& b = Bbs::instance();
    uint8_t sl = slotOf(s);
    uint8_t n  = g_which[sl];
    if (g_phase[sl] == 0) {
        char buf[16], right[12];
        snprintf(right, sizeof(right), "INFO %u", static_cast<unsigned>(n));
        b.rowTitle(s, titleOf(n, buf, sizeof(buf)), right);
        g_phase[sl] = 1;
        ++s.listIdx;
        return true;
    }
    if (g_phase[sl] == 1) {
        uint8_t w = b.rowWidth(s);
        if (!w) w = 1;
        char row[160];
        const char* p = s.compose + g_off[sl];
        const char* next = codes::wrap(p, row, sizeof(row), w, g_pt[sl]);
        if (!next) {
            if (!g_off[sl]) line(s, Color::Grey, "(this page is empty)");
            g_phase[sl] = 2;
            if (!g_off[sl]) { ++s.listIdx; return true; }
        } else {
            g_pt[sl].reserve = strlen(next) + 64;
            codes::row(s.term, s.tl, row, w, g_pt[sl]);
            s.term.nl(s.tl);
            if (next > s.compose && next[-1] == '\n') codes::endParagraph(g_pt[sl]);
            g_off[sl] = static_cast<uint16_t>(next - s.compose);
            if (!*next) g_phase[sl] = 2;
            ++s.listIdx;
            return true;
        }
    }
    if (g_phase[sl] == 2) {
        b.rowRule(s);
        g_phase[sl] = 3;
        ++s.listIdx;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Writing and clearing.
// ---------------------------------------------------------------------------
bool mayChange(Bbs& b, Session& s) {
    if (mayWrite(s)) return true;
    line(s, Color::LightRed, "Only the sysop can change the information pages.");
    (void)b;
    return false;
}

void clear(Session& s, uint8_t n) {
    char p[96], msg[48];
    if (!readPage(n, p, sizeof(p)) || !hasText(n)) {
        snprintf(msg, sizeof(msg), "Page %u is already empty.", static_cast<unsigned>(n));
        line(s, Color::Grey, msg);
        return;
    }
    remove(p);
    g_textMask = static_cast<uint16_t>(g_textMask & ~(1u << n));
    snprintf(msg, sizeof(msg), "Page %u cleared.", static_cast<unsigned>(n));
    line(s, Color::LightGreen, msg);
    plat::log("info: %s cleared page %u", s.user, static_cast<unsigned>(n));
}

void edit(Bbs& b, Session& s, uint8_t n) {
    if (!claims::take(claims::Res::Info, s.id)) {
        line(s, Color::LightRed, "Somebody is writing a page. Try again in a minute.");
        b.prompt(s);
        return;
    }
    if (!b.own(s, g_index)) {
        claims::release(claims::Res::Info, s.id);
        b.prompt(s);
        return;
    }
    g_editing = n;

    // Start from what the page says, not from nothing: re-add each line so
    // backspace can walk up into it. The lines go back into the same buffer
    // they are read from, and a write never gets ahead of the read, so no
    // second copy of the page is needed: that would be 1.5 KB of stack.
    // The body is set up by hand because compose::begin clears the first
    // byte, which here is the first byte of the page.
    uint16_t len = load(s, n);
    compose::Body& body = g_body;
    body.buf     = s.compose;
    body.cap     = BBS_COMPOSE_MAX;
    body.len     = 0;
    body.rows    = 0;
    body.maxRows = BBS_COMPOSE_ROWS;

    char buf[16], bar[48];
    s.term.cls(s.tl);
    snprintf(bar, sizeof(bar), "Page %u: %.24s", static_cast<unsigned>(n), titleOf(n, buf, sizeof(buf)));
    b.rowBar(s, Color::Yellow, bar);
    s.term.nl(s.tl);
    char how[64];
    snprintf(how, sizeof(how), "Up to %u lines. /s saves, /a leaves it.",
             static_cast<unsigned>(BBS_COMPOSE_ROWS));
    line(s, Color::Grey, how);
    b.rowRule(s);

    // Where the next line starts is worked out BEFORE the line is added
    // back: addLine terminates what it writes, and that terminator lands
    // exactly on the newline that ended this line in the original.
    const char* p   = s.compose;
    const char* end = s.compose + len;
    char one[BBS_LINE_MAX * 2 + 1];
    while (p < end) {
        const char* e = static_cast<const char*>(memchr(p, '\n', static_cast<size_t>(end - p)));
        const char* next = e ? e + 1 : end;
        size_t l = static_cast<size_t>((e ? e : end) - p);
        if (l > sizeof(one) - 1) l = sizeof(one) - 1;
        memcpy(one, p, l);
        one[l] = '\0';
        if (!compose::addLine(body, one)) break;
        composer::echo(s, body.rows, one);
        p = next;
    }
    composer::prompt(s, body);
}

void finish(Session& s, bool save) {
    uint8_t n = g_editing;
    char msg[64];
    if (save && n < kPages) {
        char path[96], tmp[112];
        bool ok = pagePath(n, path, sizeof(path));
        if (ok) {
            // Through a temp file and a rename, like every other file the
            // board rewrites: a power cut leaves the old page or the new
            // one, never half of each.
            snprintf(tmp, sizeof(tmp), "%s.tmp", path);
            FILE* f = fopen(tmp, "wb");
            ok = f && fwrite(s.compose, 1, g_body.len, f) == g_body.len;
            if (f && fclose(f) != 0) ok = false;
            // Renamed over the old page first (1.1.0): userdata is LittleFS,
            // where that replaces it in one step. Only a filesystem that
            // refuses a name that is there (FatFs, EEXIST) has the old page
            // go first, once the new one is whole. Removing it first on
            // every path lost the page whenever the rename then failed.
            if (ok && rename(tmp, path) != 0) {
                if (errno == EEXIST) { remove(path); ok = rename(tmp, path) == 0; }
                else ok = false;
            }
            if (!ok) remove(tmp);
        }
        snprintf(msg, sizeof(msg), ok ? "Page %u saved." : "Page %u did not save.",
                 static_cast<unsigned>(n));
        line(s, ok ? Color::LightGreen : Color::LightRed, msg);
        if (ok) {
            plat::log("info: %s wrote page %u", s.user, static_cast<unsigned>(n));
            if (g_body.len) g_textMask = static_cast<uint16_t>(g_textMask | (1u << n));
            else            g_textMask = static_cast<uint16_t>(g_textMask & ~(1u << n));
        }
    } else {
        snprintf(msg, sizeof(msg), "Page %u left as it was.", static_cast<unsigned>(n));
        line(s, Color::Grey, msg);
    }
    g_editing = 0xFF;
    claims::release(claims::Res::Info, s.id);
    Bbs::instance().release(s);                     // draws the prompt
}

// liftInput / restoreInput: a page, a broadcast or a ring printed into the
// page editor (1.1.0). The line is ended and " n: " drawn again underneath
// with what was being typed; the page itself is untouched.
bool hookLift(Session& s) {
    if (g_editing == 0xFF || !claims::holds(claims::Res::Info, s.id)) return false;
    s.term.reset(s.tl);
    s.term.nl(s.tl);
    return true;
}

void hookRestore(Session& s) {
    composer::redraw(s, g_body);
}

void onKey(Session& s, int k, uint32_t) {
    if (g_editing == 0xFF || !claims::holds(claims::Res::Info, s.id)) return;
    composer::Res r = composer::key(s, g_body, k);
    if (r == composer::Res::Editing) return;
    finish(s, r == composer::Res::Save);
}

// ---------------------------------------------------------------------------
// INFO
// ---------------------------------------------------------------------------
void cmdInfo(Bbs& b, Session& s, const char* a) {
    while (*a == ' ') ++a;
    if (!*a) { index(b, s); b.prompt(s); return; }
    if (*a < '0' || *a > '9' || (a[1] && a[1] != ' ')) {
        line(s, Color::LightRed, "INFO, or INFO and a page, 0 to 9.");
        b.prompt(s);
        return;
    }
    uint8_t n = static_cast<uint8_t>(*a - '0');
    const char* rest = a + 1;
    while (*rest == ' ') ++rest;
    if (ieq(rest, "edit")) {
        if (!mayChange(b, s)) { b.prompt(s); return; }
        edit(b, s, n);
        return;
    }
    if (ieq(rest, "clear")) {
        if (mayChange(b, s)) clear(s, n);
        b.prompt(s);
        return;
    }
    if (*rest || !visible(s, n)) {
        char msg[32];
        snprintf(msg, sizeof(msg), "There is no page %u.", static_cast<unsigned>(n));
        line(s, Color::LightRed, msg);
        b.prompt(s);
        return;
    }
    show(b, s, n);
}

const Command kCommands[] = {
    { "INFO", "I", 0, CF_READ, "[I]NFO [n]", "the board's information pages",
      [](Bbs& b, Session& s, const char* a, uint32_t) { cmdInfo(b, s, a); },
      Menu::Main, 5 },
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
bool start(Bbs& bbs) {
    (void)bbs;
    g_index = plugins::indexOf(kName);
    for (auto& pg : g_page) pg = Page{};
    plugins::forEachKey(g_index, readKey, nullptr);
    g_textMask = 0;
    for (uint8_t n = 0; n < kPages; ++n)
        if (fileHasText(n)) g_textMask = static_cast<uint16_t>(g_textMask | (1u << n));
    return true;
}

void stop() {}

// One line at login when there is something to read, the same way mail says
// it is waiting. Phase 1 of linking the pages to login: a sentence, not a
// screen somebody pages through every call.
void onLogin(Session& s) {
    uint8_t c = visibleCount(s);
    if (!c) return;
    char msg[64];
    snprintf(msg, sizeof(msg), "%u information page%s. INFO reads them.",
             static_cast<unsigned>(c), c == 1 ? "" : "s");
    line(s, Color::Cyan, msg);
}

void onLogoff(Session& s) {
    // claims::releaseAll frees the lock when the session closes; this frees
    // the page number that went with it.
    if (claims::holds(claims::Res::Info, s.id)) g_editing = 0xFF;
}

} // namespace

// ---------------------------------------------------------------------------
// The room's door
// ---------------------------------------------------------------------------
namespace info {

void roomIndex(Session& s) {
    uint8_t count = visibleCount(s);
    if (!count) {
        line(s, Color::Cyan, "--> This board has no information pages yet.");
        return;
    }
    line(s, Color::Cyan, "--> Information");
    char buf[16];
    for (uint8_t n = 0; n < kPages; ++n) {
        if (!visible(s, n)) continue;
        char num[8];
        snprintf(num, sizeof(num), "/i%u  ", static_cast<unsigned>(n));
        s.term.color(s.tl, Color::Yellow);
        s.term.text(s.tl, num);
        s.term.color(s.tl, Color::White);
        s.term.text(s.tl, titleOf(n, buf, sizeof(buf)));
        s.term.nl(s.tl);
    }
    line(s, Color::Grey, "/in reads one.");
}

void roomShow(Session& s, uint8_t n) {
    char msg[48];
    if (n >= kPages || !visible(s, n)) {
        snprintf(msg, sizeof(msg), "--> There is no page %u.", static_cast<unsigned>(n));
        line(s, Color::LightRed, msg);
        return;
    }
    char buf[16];
    snprintf(msg, sizeof(msg), "--> [%u] %.24s", static_cast<unsigned>(n), titleOf(n, buf, sizeof(buf)));
    line(s, Color::Cyan, msg);
    load(s, n);
    uint8_t w = Bbs::instance().rowWidth(s);
    if (!w) w = 1;
    codes::Painter pt;
    pt.begin(Color::Grey, !s.bellOff);
    char row[160];
    const char* p = s.compose;
    uint8_t guard = 0;
    while ((p = codes::wrap(p, row, sizeof(row), w, pt)) != nullptr && ++guard < 96) {
        pt.reserve = strlen(p) + 64;
        codes::row(s.term, s.tl, row, w, pt);
        s.term.nl(s.tl);
        if (p[-1] == '\n') codes::endParagraph(pt);
        if (!*p) break;
    }
    line(s, Color::Grey, "--> /i lists them.");
}

void roomClear(Session& s, uint8_t n) {
    if (!mayWrite(s)) {
        line(s, Color::LightRed, "--> Only the sysop can change the information pages.");
        return;
    }
    clear(s, n);
}

} // namespace info

extern const Plugin kInfoPlugin;
const Plugin kInfoPlugin = {
    { kName, "Information pages", "1.0",
      0,                        // heapBytes
      16u * 1024u,              // storageBytes: ten pages at most 1.5 KB each
      PF_CORE | PF_ON,          // a fresh board has it, with no card
      PlugLevel::All,           // read
      PlugLevel::Sysop,         // write: writing and clearing pages
      PlugLevel::Sysop },       // admin
    start,
    stop,
    nullptr,                    // tick
    nullptr,                    // onConnect
    onLogin,
    onLogoff,
    onKey,
    nullptr,                    // status
    kCommands,
    sizeof(kCommands) / sizeof(kCommands[0]),
    kSettings,
    sizeof(kSettings) / sizeof(kSettings[0]),
    setting,
    rows,
    nullptr,                    // onPresence
    nullptr,                    // onBytes
    nullptr,                    // onRename
    nullptr,                    // listDone
    hookLift,                   // liftInput: notices reach the page editor (1.1.0)
    hookRestore,                // restoreInput
};
