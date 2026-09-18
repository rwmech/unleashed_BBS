/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/chat.cpp
 * Module:       Plugins / chat
 *
 * Purpose:      A DDial/Gtalk style chat room. One room, everyone who
 *               joins sees every line as it is typed, and the room keeps a
 *               few lines of history so somebody joining sees what was
 *               just said.
 *
 * Design:       The plugin owns each joined session, so keys come here
 *               instead of the command prompt. Lines are edited with the
 *               same line editor the shell uses, so backspace, abort and
 *               the terminal drivers all behave as usual. Incoming text is
 *               printed above the line being typed, which is then redrawn.
 *
 *                 [plugin:chat]
 *                 enabled = yes
 *                 read    = all      who may join and watch
 *                 write   = all      who may speak
 *                 admin   = sysop    who may clear the room
 *                 room    = Main
 *
 * Libraries:    none (libc)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     PLUGINS.md, COMMANDS.md
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
#include "../core/bbs.h"
#include "../core/bbs_util.h"
#include "../core/plugin.h"
#include "../platform/platform.h"

#include <cstdio>
#include <cstring>

using namespace bbsu;

namespace {

constexpr const char kName[]    = "chat";
constexpr uint8_t    kHistory   = 8;      // lines kept for a caller who joins
constexpr uint8_t    kLineMax   = 64;     // one chat line, fits 40 columns twice

char    g_room[20] = "Main";
char    g_hist[kHistory][kLineMax + 1] = {};
uint8_t g_histCount = 0;
uint8_t g_histNext  = 0;
uint8_t g_index     = 0xFF;

void readKey(void* ctx, const char* key, const char* value) {
    (void)ctx;
    if (!strcmp(key, "room")) snprintf(g_room, sizeof(g_room), "%.19s", value);
}

// remember: keep the line for whoever joins next
void remember(const char* line) {
    snprintf(g_hist[g_histNext], kLineMax + 1, "%.*s", kLineMax, line);
    g_histNext = static_cast<uint8_t>((g_histNext + 1) % kHistory);
    if (g_histCount < kHistory) ++g_histCount;
}

// who: "#2:Daytona)" - node, handle, and the bracket says the rank:
// ) a caller, * a guest, > a co-sysop, ] the sysop
char rankBracket(const Session& s) {
    char m = markFor(s);
    return m == ' ' ? ')' : m;
}

void tag(const Session& s, char* out, size_t n) {
    snprintf(out, n, "#%c:%.20s%c", nodeChar(s), s.user, rankBracket(s));
}

// chatPrompt: no prompt character in the room, just the cursor waiting at
// the start of the line, the way DDial and Gtalk did it
void chatPrompt(Session& s) {
    s.term.color(s.tl, Color::White);
    s.term.cursor(s.tl, true);
}

// armInput: take a line of chat from this caller. F_STAY keeps Enter on
// the same line so the line can be rewritten in its finished form.
void armInput(Session& s) {
    uint8_t room = static_cast<uint8_t>(s.term.cols() > 4 ? s.term.cols() - 2 : 32);
    s.ed.begin(room < kLineMax ? room : kLineMax, LineEditor::F_STAY);
    chatPrompt(s);
}

// wipeInput: take back whatever the caller has typed so far
void wipeInput(Session& s) {
    s.term.eraseBack(s.tl, s.ed.shown());
}

struct Fan { const Session* from; Color color; const char* text; };

// toEveryone: one line in the room. Each screen loses its input line, gets
// the chat line, then has its input line put back underneath, so the
// conversation scrolls with nothing blank between.
void toEveryone(const char* text, Color color, const Session* skip) {
    Fan f{ skip, color, text };
    Bbs::instance().eachSession([](void* ctx, Session& s) {
        Fan* f = static_cast<Fan*>(ctx);
        if (!Bbs::instance().owns(s, g_index)) return;
        if (&s == f->from) return;
        if (s.tl.freeBytes() < 256) return;                  // slow line: skip this one
        wipeInput(s);
        s.term.color(s.tl, f->color);
        s.term.text(s.tl, f->text);
        s.term.nl(s.tl);
        chatPrompt(s);
        s.ed.redraw(s.term, s.tl);                           // what they were typing
    }, &f);
}

uint8_t roomCount() {
    uint8_t n = 0;
    Bbs::instance().eachSession([](void* ctx, Session& s) {
        if (Bbs::instance().owns(s, g_index)) ++(*static_cast<uint8_t*>(ctx));
    }, &n);
    return n;
}

void leave(Session& s, const char* why) {
    Bbs& bbs = Bbs::instance();
    char line[80], me[32];
    tag(s, me, sizeof(me));
    snprintf(line, sizeof(line), "*** %.28s %s", me, why);
    wipeInput(s);                                            // drop the chat input line
    bbs.release(s);                                          // the prompt starts its own line
    toEveryone(line, Color::Grey, &s);
    remember(line);
}

void join(Bbs& bbs, Session& s) {
    char line[80];
    if (!bbs.own(s, g_index)) { bbs.prompt(s); return; }
    bbs.setDoing(s, "CHAT");

    Term& t = s.term;
    Timeline& tl = s.tl;
    t.reset(tl);
    t.color(tl, Color::Cyan);
    snprintf(line, sizeof(line), "%.19s: %u here. /q quits, /w lists.", g_room, roomCount());
    t.text(tl, line);
    t.nl(tl);
    for (uint8_t i = 0; i < g_histCount; ++i) {              // what they just missed
        uint8_t at = static_cast<uint8_t>((g_histNext + kHistory - g_histCount + i) % kHistory);
        t.color(tl, Color::DarkGrey);
        t.text(tl, g_hist[at]);
        t.nl(tl);
    }
    armInput(s);

    char me[32];
    tag(s, me, sizeof(me));
    snprintf(line, sizeof(line), "*** %.28s joined", me);
    toEveryone(line, Color::Grey, &s);
    remember(line);
}

// who: the room's roster on one caller's screen
void who(Session& s) {
    char line[80];
    Session* me = &s;
    wipeInput(s);
    Bbs::instance().eachSession([](void* ctx, Session& o) {
        Session* me = static_cast<Session*>(ctx);
        if (!Bbs::instance().owns(o, g_index)) return;
        char who[32], row[48];
        tag(o, who, sizeof(who));
        snprintf(row, sizeof(row), "  %-24.24s%s", who, &o == me ? " (you)" : "");
        me->term.color(me->tl, Color::Grey);
        me->term.text(me->tl, row);
        me->term.nl(me->tl);
    }, me);
    snprintf(line, sizeof(line), "%u in %.19s", roomCount(), g_room);
    s.term.color(s.tl, Color::Cyan);
    s.term.text(s.tl, line);
    s.term.nl(s.tl);
    armInput(s);
}

void say(Session& s, const char* text) {
    char line[96], me[32];
    wipeInput(s);
    if (!plugins::mayUse(s, plugins::levelFor(g_index, 1))) {   // read-only in the room
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "You can watch, but not talk here.");
        s.term.nl(s.tl);
        armInput(s);
        return;
    }
    tag(s, me, sizeof(me));
    snprintf(line, sizeof(line), "%.28s %.60s", me, text);
    s.term.color(s.tl, Color::LightGrey);                       // the speaker sees it too
    s.term.text(s.tl, line);
    s.term.nl(s.tl);
    armInput(s);
    toEveryone(line, Color::White, &s);
    remember(line);
}

// ---------------------------------------------------------------------------
// onKey: the caller is in the room. Lines go to everyone; /q leaves.
// ---------------------------------------------------------------------------
void onKey(Session& s, int k, uint32_t now) {
    (void)now;
    LineEditor::Res r = s.ed.key(k, s.term, s.tl);
    if (r == LineEditor::Res::Abort) { leave(s, "left the room"); return; }
    if (r != LineEditor::Res::Done) return;

    char line[BBS_LINE_MAX + 1];
    strncpy(line, s.ed.text(), BBS_LINE_MAX);
    line[BBS_LINE_MAX] = '\0';
    const char* p = line;
    while (*p == ' ') ++p;

    if (!*p)                              { wipeInput(s); armInput(s); return; }
    if (ieq(p, "/q") || ieq(p, "/quit"))  { leave(s, "left the room"); return; }
    if (ieq(p, "/w") || ieq(p, "/who"))   { who(s); return; }
    say(s, p);
}

void onLogoff(Session& s) {
    if (!Bbs::instance().owns(s, g_index)) return;
    char line[80], me[32];
    tag(s, me, sizeof(me));
    snprintf(line, sizeof(line), "*** %.28s logged off", me);
    toEveryone(line, Color::Grey, &s);
    remember(line);
}

bool start(Bbs& bbs) {
    (void)bbs;
    g_index = plugins::indexOf(kName);
    g_histCount = g_histNext = 0;
    plugins::forEachKey(g_index, readKey, nullptr);
    return true;
}

const Command kCommands[] = {
    { "CHAT", "", 0, CF_READ, "CHAT", "join the chat room",
      [](Bbs& b, Session& s, const char*, uint32_t) { join(b, s); } },
    { "CHATCLEAR", "", 0, CF_ADMIN, "CHATCLEAR", "clear the chat history",
      [](Bbs& b, Session& s, const char*, uint32_t) {
          g_histCount = g_histNext = 0;
          s.term.color(s.tl, Color::Yellow);
          s.term.text(s.tl, "Chat history cleared.");
          b.prompt(s);
      } },
};

} // namespace

extern const Plugin kChatPlugin = {
    // on unless switched off, and everyone may talk unless system.cfg says otherwise
    { kName, "Chat room", "1.0", 0, 0, PF_CORE | PF_ON, PlugLevel::All, PlugLevel::All, PlugLevel::Sysop },
    start,
    nullptr,                 // stop
    nullptr,                 // tick
    nullptr,                 // onConnect
    nullptr,                 // onLogin
    onLogoff,
    onKey,
    kCommands,
    sizeof(kCommands) / sizeof(kCommands[0]),
};
