/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/bbs_shell.cpp
 * Module:       Core / caller shell
 *
 * Purpose:      Caller shell: the core command table (C3 registry), dispatch
 *                  and permission checks, HELP generated from the tables, the
 *                  paged lists (HELP WHO LAST DASH), the WHO n / DASH n refresh
 *                  rows, PAGE / DND, TIME, MEM, TERM, BAUD, the FX demo, and BYE
 *                  (which doubles as the staff elevation path).
 *                  Command reference: COMMANDS.md.
 *
 * Libraries:    none (libc)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     COMMANDS.md
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

#include "bbs.h"
#include "bbs_util.h"
#include "fx.h"
#include "clock.h"
#include "sysconfig.h"
#include "calllog.h"
#include "plugin.h"
#include "../platform/platform.h"

#include <climits>
#include <cstring>
#include <cstdio>
#include <cstdlib>

using namespace bbsu;

namespace {

// HELP: usage column width including the gap. The column has to clear the
// widest usage string there is ("ANNOUNCE TEST", 13) plus a space, or the
// longest command on the board is the one that gets truncated.
constexpr uint8_t kUsageCol = 15;
constexpr uint8_t kMainRank = 20;       // chat commands below this rank also sit on the main menu

// label: fixed-width demo label
void label(Term& t, Timeline& tl, const char* name) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%-11s", name);
    t.color(tl, Color::LightBlue);
    t.text(tl, buf);
    t.color(tl, Color::Grey);
}

// ---------------------------------------------------------------------------
// wrapTake: how many characters of text fit in width, breaking at a space
// ---------------------------------------------------------------------------
size_t wrapTake(const char* text, size_t width) {
    size_t len = strlen(text);
    if (len <= width) return len;
    for (size_t i = width; i > 0; --i) {
        if (text[i] == ' ') return i;
    }
    return width;                                    // one long word: hard break
}

// visibleLen: display columns of an ASCII string that may hold UTF-8 µ
size_t visibleLen(const char* s) {
    size_t n = 0;
    for (; *s; ++s) if ((static_cast<uint8_t>(*s) & 0xC0) != 0x80) ++n;
    return n;
}

// parseSeconds: "5" within [lo, hi], false otherwise
bool parseSeconds(const char* arg, uint8_t lo, uint8_t hi, uint8_t& out) {
    char* end = nullptr;
    long v = strtol(arg, &end, 10);
    if (end == arg || (end && *end) || v < lo || v > hi) return false;
    out = static_cast<uint8_t>(v);
    return true;
}

} // namespace

// ===========================================================================
// Command table (C3 registry, core entries)
// ===========================================================================

// ---------------------------------------------------------------------------
// coreCommands: the built-in commands. The table order is the HELP order, so
// it is kept sorted by how often a command gets used, most first, grouped by
// the menu it belongs to. Usage stays within 12 characters and marks the
// shortcut letter as [W]HO; descriptions wrap onto indented lines.
// ---------------------------------------------------------------------------
const Command* Bbs::coreCommands(uint8_t& count) {
    static const Command k[] = {
        // -- main: what nearly every caller types ---------------------------
        { "HELP", "H?", 0, CF_NONE, "[H]ELP [x]", "this menu; x picks another",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdHelp(s, a); },
          Menu::Main, 0 },
        { "WHO", "W", 0, CF_NONE, "[W]HO [n]", "who is on; n refreshes",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdWho(s, a); },
          Menu::Main, 1 },
        { "PAGE", "", 0, CF_NONE, "PAGE n msg", "message node n",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdPage(s, a); b.prompt(s); },
          Menu::Main, 2 },
        { "INFO", "I", 0, CF_NONE, "[I]NFO [h]", "a caller's profile",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdInfo(s, a); },
          Menu::Main, 3 },
        { "TIME", "", 0, CF_NONE, "TIME", "clock and time left",
          [](Bbs& b, Session& s, const char* a, uint32_t n) { b.cmdTime(s, a, n); },
          Menu::Main, 4 },
        { "LAST", "", 0, CF_NONE, "LAST", "the most recent calls",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.startList(s, ListKind::Last); },
          Menu::Main, 5 },
        { "CLS", "C", 0, CF_NONE, "[C]LS", "clear the screen",
          [](Bbs& b, Session& s, const char*, uint32_t) { s.term.cls(s.tl); b.prompt(s); },
          Menu::Main, 6 },
        { "ABOUT", "", 0, CF_NONE, "ABOUT", "this BBS, version, license",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdAbout(s); },
          Menu::Main, 8 },
        { "G", "", 0, CF_NONE, "[G]", "log off, asks first",
          [](Bbs&, Session& s, const char*, uint32_t) {
              s.st = SState::Confirm;
              s.term.color(s.tl, Color::Yellow);
              s.term.text(s.tl, kConfirmText);
              s.term.color(s.tl, Color::White);
          },
          Menu::Main, 9 },
        { "BYE", "", 0, CF_NONE, "BYE", "log off now",
          [](Bbs& b, Session& s, const char* a, uint32_t n) { b.cmdBye(s, a, n); },
          Menu::Main, 10 },

        // -- your account and your terminal ---------------------------------
        { "PROFILE", "", 0, CF_ACCOUNT, "PROFILE", "edit your profile",
          [](Bbs& b, Session& s, const char*, uint32_t n) { b.cmdProfile(s, n); },
          Menu::Account, 0 },
        { "PASSWORD", "", 0, CF_ACCOUNT, "PASSWORD", "change your password",
          [](Bbs& b, Session& s, const char*, uint32_t n) { b.cmdPassword(s, n); },
          Menu::Account, 1 },
        { "DND", "", 0, CF_NONE, "DND", "pages off / on",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdDnd(s); b.prompt(s); },
          Menu::Account, 2 },
        { "PRIVACY", "", 0, CF_NONE, "PRIVACY", "what this board knows about you",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdPrivacy(s); },
          Menu::Account, 7 },
        { "TERM", "T", 0, CF_NONE, "[T]ERM", "terminal type and size",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdTerm(s); b.prompt(s); },
          Menu::Account, 3 },
        { "BAUD", "", 0, CF_NONE, "BAUD n|OFF", "emulate 300-19200 bps",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdBaud(s, a); b.prompt(s); },
          Menu::Account, 4 },
        { "MEM", "M", 0, CF_NONE, "[M]EM", "memory use",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdMem(s); b.prompt(s); },
          Menu::Account, 5 },
        { "FX", "F", 0, CF_NONE, "[F]X", "effects demo",
          [](Bbs&, Session& s, const char*, uint32_t) {
              s.st = SState::Fx; s.fxStep = 0; s.savedCps = s.tl.cps();
          },
          Menu::Account, 6 },

        // -- spellings of BYE that never need listing ------------------------
        { "OFF", "", 0, CF_HIDDEN, "", "",
          [](Bbs& b, Session& s, const char*, uint32_t n) { b.goodbye(s, n); },
          Menu::Hidden, 99 },
        { "LOGOFF", "", 0, CF_HIDDEN, "", "",
          [](Bbs& b, Session& s, const char*, uint32_t n) { b.goodbye(s, n); },
          Menu::Hidden, 99 },
        { "QUIT", "", 0, CF_HIDDEN, "", "",
          [](Bbs& b, Session& s, const char*, uint32_t n) { b.goodbye(s, n); },
          Menu::Hidden, 99 },

        // -- staff (shown and dispatched only with the permission) ------------
        { "DASH", "", PERM_DASH, CF_NONE, "DASH [n]", "dashboard; n refreshes",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdDash(s, a); },
          Menu::Staff, 0 },
        { "USERS", "", PERM_USERS, CF_NONE, "USERS", "manage accounts",
          [](Bbs& b, Session& s, const char*, uint32_t n) { b.cmdUsers(s, n); },
          Menu::Staff, 1 },
        { "USER", "", PERM_USERS, CF_NONE, "USER ADD", "add an account",
          [](Bbs& b, Session& s, const char* a, uint32_t n) { b.cmdUser(s, a, n); },
          Menu::Staff, 2 },
        { "USER", "", PERM_USERS, CF_HELPONLY, "USER EDIT h", "edit account h", nullptr,
          Menu::Staff, 3 },
        { "USER", "", PERM_USERS, CF_HELPONLY, "USER DEL h", "delete account h", nullptr,
          Menu::Staff, 4 },
        { "KICK", "", PERM_KICK, CF_NONE, "KICK n [msg]", "disconnect node n",
          [](Bbs& b, Session& s, const char* a, uint32_t n) { b.cmdKick(s, a, n); b.prompt(s); },
          Menu::Staff, 5 },
        { "SNOOP", "", PERM_SNOOP, CF_NONE, "SNOOP n", "watch node n, Q stops",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdSnoop(s, a); },
          Menu::Staff, 6 },
        { "BROADCAST", "", PERM_BROADCAST, CF_NONE, "BROADCAST m", "message every node",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdBroadcast(s, a); b.prompt(s); },
          Menu::Staff, 7 },
        { "TIME", "", PERM_TIME, CF_HELPONLY, "TIME n +/-m", "add/remove node minutes", nullptr,
          Menu::Staff, 8 },
        { "HIDE", "", PERM_HIDE, CF_NONE, "HIDE", "hide me from WHO",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdShow(s, false); b.prompt(s); },
          Menu::Staff, 9 },
        { "SHOW", "", PERM_HIDE, CF_NONE, "SHOW", "list me in WHO",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdShow(s, true); b.prompt(s); },
          Menu::Staff, 10 },
        { "LURK", "", PERM_HIDE, CF_NONE, "LURK", "hide and refuse pages",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdLurk(s); b.prompt(s); },
          Menu::Staff, 11 },
        { "NODES", "", PERM_NODES, CF_NONE, "NODES", "every session with its IP",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.startList(s, ListKind::Nodes); },
          Menu::Staff, 12 },
        { "BANS", "", PERM_BANS, CF_NONE, "BANS", "banned IP addresses",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.startList(s, ListKind::Bans); },
          Menu::Staff, 13 },
        { "UNBAN", "", PERM_UNBAN, CF_NONE, "UNBAN ip", "lift a ban",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdUnban(s, a); b.prompt(s); },
          Menu::Staff, 14 },
        { "DROP", "", 0, CF_STAFF, "DROP", "give up staff access",
          [](Bbs& b, Session& s, const char*, uint32_t n) { b.cmdDrop(s, n); },
          Menu::Staff, 20 },

        // -- sysop ------------------------------------------------------------
        { "SYS", "", 0, CF_STAFF, "SYS", "radio, memory, storage, load",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.startList(s, ListKind::Sys); },
          Menu::Sysop, 0 },
        { "CALLS", "", 0, CF_STAFF, "CALLS", "when the board is busy",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdCalls(s); },
          Menu::Sysop, 1 },
        { "CONFIG", "", 0, CF_SYSOP, "CONFIG [p]", "board settings, page by page",
          [](Bbs& b, Session& s, const char* a, uint32_t n) { b.cmdConfig(s, a, n); },
          Menu::Sysop, 3 },
        { "PLUGINS", "", 0, CF_STAFF, "PLUGINS", "plugins and their state",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.startList(s, ListKind::Plugins); },
          Menu::Sysop, 2 },
    };
    count = static_cast<uint8_t>(sizeof(k) / sizeof(k[0]));
    return k;
}

// ===========================================================================
// Dispatch
// ===========================================================================

const Command* Bbs::commandAt(uint8_t index) const {
    for (uint8_t t = 0; t < tableCount_; ++t) {
        if (index < tables_[t].count) return &tables_[t].list[index];
        index = static_cast<uint8_t>(index - tables_[t].count);
    }
    return nullptr;
}

// pluginOf: which plugin owns the command at this index (0xFF = core)
uint8_t Bbs::pluginOf(uint8_t index) const {
    for (uint8_t t = 0; t < tableCount_; ++t) {
        if (index < tables_[t].count) return tables_[t].plugin;
        index = static_cast<uint8_t>(index - tables_[t].count);
    }
    return 0xFF;
}

// ---------------------------------------------------------------------------
// allowed: may this session run this command? Core commands go by the
// permission bits; a plugin's commands go by its read / write / admin
// levels, with anything untagged treated as write.
// ---------------------------------------------------------------------------
bool Bbs::allowed(const Session& s, const Command& c, uint8_t plugin) const {
    if (c.perm && !can(s, c.perm)) return false;
    if ((c.flags & CF_STAFF) && !s.perms) return false;
    if ((c.flags & CF_SYSOP) && s.level != Access::Sysop) return false;
    if ((c.flags & CF_ACCOUNT) && s.guest) return false;
    if (plugin == 0xFF) return true;
    if (!plugins::running(plugin)) return false;
    uint8_t which = (c.flags & CF_READ) ? 0 : ((c.flags & CF_ADMIN) ? 2 : 1);
    return plugins::mayUse(s, plugins::levelFor(plugin, which));
}

// ---------------------------------------------------------------------------
// findCommand: verb or one-letter key the session is allowed to run
// ---------------------------------------------------------------------------
const Command* Bbs::findCommand(const char* verb, const Session& s) const {
    char key = verb[1] == '\0' ? static_cast<char>(toupper(static_cast<unsigned char>(verb[0]))) : 0;
    for (uint8_t i = 0;; ++i) {
        const Command* c = commandAt(i);
        if (!c) return nullptr;
        if (!c->fn || (c->flags & CF_HELPONLY)) continue;
        bool match = ieq(verb, c->verb) || (key && strchr(c->keys, key));
        if (!match) continue;
        if (!allowed(s, *c, pluginOf(i))) continue;            // not granted: stays unknown
        return c;
    }
}

// ---------------------------------------------------------------------------
// runCommand: first word is the verb, the rest is the argument
// ---------------------------------------------------------------------------
void Bbs::runCommand(Session& s, const char* text, uint32_t now) {
    while (*text == ' ') ++text;
    char verb[16];
    size_t i = 0;
    while (text[i] && text[i] != ' ' && i < sizeof(verb) - 1) {
        verb[i] = text[i];
        ++i;
    }
    verb[i] = '\0';
    while (text[i] && text[i] != ' ') ++i;       // overlong verb: skip the rest

    char arg[BBS_LINE_MAX + 1];
    const char* a = text + i;
    while (*a == ' ') ++a;
    strncpy(arg, a, BBS_LINE_MAX);
    arg[BBS_LINE_MAX] = '\0';
    size_t n = strlen(arg);
    while (n && arg[n - 1] == ' ') arg[--n] = '\0';

    Term& t = s.term;
    Timeline& tl = s.tl;

    if (!verb[0]) { prompt(s); return; }

    // busy line: only BYE means anything
    if (s.role == Role::Busy) {
        t.nl(tl);
        if (ieq(verb, "BYE")) { cmdBye(s, arg, now); return; }
        t.color(tl, Color::LightRed);
        t.text(tl, "All nodes are in use. Try your call later.");
        t.nl(tl);
        goodbye(s, now);
        return;
    }

    const Command* c = findCommand(verb, s);
    if (c) {
        if (!ieq(c->verb, "BYE")) {                  // staff WHO/DASH show the verb, never arguments
            strncpy(s.doing, c->verb, BBS_DOING_MAX);
            s.doing[BBS_DOING_MAX] = '\0';
        }
        t.nl(tl);
        c->fn(*this, s, arg, now);
        return;
    }

    // unknown: rub out what was typed and flash the error in its place
    uint8_t used = s.role == Role::Sysop ? 11 : 10;  // "[S] Sysop: " / "[n] Main: "
    if (t.isPet()) inputError(s, used, "?SYNTAX  ERROR", "?SYNTAX  ERROR");
    else           inputError(s, used, "Unknown command. Type HELP.", "Unknown. Type HELP");
    armPrompt(s);
}

// ---------------------------------------------------------------------------
// listRow: one line of the active list
// ---------------------------------------------------------------------------
bool Bbs::listRow(Session& s) {
    switch (s.list) {
        case ListKind::Help:  return rowHelp(s);
        case ListKind::Who:   return rowWho(s);
        case ListKind::Last:  return rowLast(s);
        case ListKind::Nodes: return rowNodes(s);
        case ListKind::Bans:  return rowBans(s);
        case ListKind::Dash:  return rowDash(s);
        case ListKind::Users: return rowUsers(s);
        case ListKind::Plugins: return rowPlugins(s);
        case ListKind::Sys:   return rowSys(s);
        case ListKind::Calls: return rowCalls(s);
        case ListKind::PlugRows: {
            const Plugin* p = plugins::at(s.listPlugin);
            if (!p || !p->rows || !plugins::running(s.listPlugin)) return false;
            return p->rows(s);
        }
        default:              return false;
    }
}

// ===========================================================================
// Row output
// ===========================================================================

// rowWidth: lists and menus are laid out for 40 columns everywhere
uint8_t Bbs::rowWidth(const Session& s) const {
    // A zero here underflows to 255 and a title bar pads 255 reverse-video
    // spaces, which is the one mechanism that would genuinely paint a bar
    // past the right edge and onto the rows below. Unreachable today, since
    // setGeometry guards zero and detection only ever yields 40 or 80, but a
    // NAWS negotiation carrying zero does reach Telnet, and the guard is free.
    uint8_t cols = s.term.cols();
    if (!cols) cols = 80;
    return static_cast<uint8_t>((cols < 40 ? cols : 40) - 1);
}

// ---------------------------------------------------------------------------
// rowText: one line. On a refresh screen the line is padded to the full
// width so a redraw from the home position leaves no old characters.
// ---------------------------------------------------------------------------
void Bbs::rowText(Session& s, Color c, const char* text, bool newline) {
    Term& t = s.term;
    t.color(s.tl, c);
    if (s.watch != ListKind::None) {
        // A refresh screen redraws from home, so anything wider than the
        // frame wraps and leaves its tail on screen at every redraw. Cut it,
        // and let the terminal layer do the counting: it is the only thing
        // that knows which bytes are a character.
        uint8_t used = t.textCols(s.tl, text, static_cast<uint8_t>(rowWidth(s)));
        for (size_t i = used; i < rowWidth(s); ++i) t.ch(s.tl, ' ');
    } else {
        t.text(s.tl, text);
    }
    if (newline) t.nl(s.tl);
}

// ---------------------------------------------------------------------------
// rowSeg / rowEnd: a row built from coloured pieces. col carries the columns
// used so far, so a refresh screen can still pad the line to the full width
// and leave nothing of the previous frame behind.
// ---------------------------------------------------------------------------
void Bbs::rowSeg(Session& s, Color c, const char* text, uint8_t& col) {
    s.term.color(s.tl, c);
    s.term.text(s.tl, text);
    col = static_cast<uint8_t>(col + visibleLen(text));
}

void Bbs::rowEnd(Session& s, uint8_t col) {
    if (s.watch != ListKind::None)
        for (uint8_t i = col; i < rowWidth(s); ++i) s.term.ch(s.tl, ' ');
    s.term.nl(s.tl);
}

// ---------------------------------------------------------------------------
// statRow: "Heap free       136,424 bytes". The label stays quiet, the
// number is the thing being read, the note behind it is quieter still.
// ---------------------------------------------------------------------------
void Bbs::statRow(Session& s, const char* label, const char* value, Color c, const char* note) {
    char buf[40];
    uint8_t col = 0;
    snprintf(buf, sizeof(buf), "%-13.13s", label);
    rowSeg(s, Color::Grey, buf, col);
    // Pad to the column, but never cut. This was "%9.9s", which silently
    // truncated anything longer than nine characters, and an IPv4 address is
    // up to fifteen: SYS reported the board's address as "192.168.0" with the
    // host part missing. A value that overflows its column is untidy; a value
    // that is quietly wrong is a bug somebody acts on.
    snprintf(buf, sizeof(buf), "%9s", value);
    rowSeg(s, c, buf, col);
    if (note) {
        rowSeg(s, Color::DarkGrey, " ", col);
        rowSeg(s, Color::DarkGrey, note, col);
    }
    rowEnd(s, col);
}

void Bbs::statNum(Session& s, const char* label, uint32_t value, const char* note) {
    char num[16];
    fmtCommas(value, num, sizeof(num));
    statRow(s, label, num, Color::LightGreen, note);
}

// ---------------------------------------------------------------------------
// rowSection: a quiet divider inside a screen, "-- network ------------".
// Used by the system screen so a wall of numbers reads in groups.
// ---------------------------------------------------------------------------
void Bbs::rowSection(Session& s, const char* name) {
    Term& t = s.term;
    uint8_t col = 0;
    rowSeg(s, Color::DarkGrey, "-- ", col);
    rowSeg(s, Color::Cyan, name, col);
    rowSeg(s, Color::DarkGrey, " ", col);
    t.color(s.tl, Color::DarkGrey);
    for (uint8_t i = col; i < rowWidth(s); ++i) { t.ch(s.tl, '-'); ++col; }
    rowEnd(s, col);
}

void Bbs::rowRule(Session& s) {
    s.term.color(s.tl, Color::Cyan);
    fx::rule(s.term, s.tl, rowWidth(s));
    s.term.nl(s.tl);
}

// ---------------------------------------------------------------------------
// rowTitle: one-line title bar across the row width. ANSI and PETSCII get
// a reverse-video bar; plain ASCII gets the title inside a dashed rule.
// right: optional text flush right (clock, counts).
// ---------------------------------------------------------------------------
void Bbs::rowTitle(Session& s, const char* title, const char* right) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    uint8_t w = rowWidth(s);
    size_t tlen = visibleLen(title);
    size_t rlen = right ? visibleLen(right) : 0;

    if (t.isAnsi() || t.isPet()) {
        t.color(tl, Color::Cyan);
        t.reverse(tl, true);
        t.color(tl, Color::Cyan);
        t.ch(tl, ' ');
        t.text(tl, title);
        size_t used = 1 + tlen + (rlen ? rlen + 1 : 0);
        for (size_t i = used; i < w; ++i) t.ch(tl, ' ');
        if (rlen) { t.text(tl, right); t.ch(tl, ' '); }
        t.reverse(tl, false);
    } else {
        t.text(tl, title);
        t.ch(tl, ' ');
        size_t used = tlen + 1 + (rlen ? rlen + 1 : 0);
        for (size_t i = used; i < w; ++i) t.glyph(tl, Glyph::HLine);
        if (rlen) { t.ch(tl, ' '); t.text(tl, right); }
    }
    t.nl(tl);
}

// ---------------------------------------------------------------------------
// doingText: staff view of what a session is doing. Before login it is the
// login stage; after, the last command verb (plugins may set s.doing).
// ---------------------------------------------------------------------------
const char* Bbs::doingText(const Session& s) {
    if (s.loggedIn) return s.doing[0] ? s.doing : "-";
    switch (s.st) {
        case SState::Detect:
        case SState::Intro:       return "connect";
        case SState::Form:        return "sign-up";
        case SState::AskRegister: return "sign-up";
        case SState::AskKnowMore: return "sign-up";
        case SState::AnyKey:      return "reading";
        default:                  return "login";
    }
}

// ===========================================================================
// HELP
// ===========================================================================

namespace {

// menuName: the word a caller types after ? to reach a menu
const char* menuName(Menu m) {
    switch (m) {
        case Menu::Chat:    return "chat";
        case Menu::Account: return "account";
        case Menu::Staff:   return "staff";
        case Menu::Sysop:   return "sysop";
        default:            return "main";
    }
}

// menuTitle: the heading on the bar above a section
const char* menuTitle(Menu m) {
    switch (m) {
        case Menu::Chat:    return "Chat and messages";
        case Menu::Account: return "You and your terminal";
        case Menu::Staff:   return "Staff";
        case Menu::Sysop:   return "Sysop";
        default:            return "Commands";
    }
}

// menuNext: section order for "? all"
Menu menuNext(Menu m) {
    switch (m) {
        case Menu::Main:    return Menu::Chat;
        case Menu::Chat:    return Menu::Account;
        case Menu::Account: return Menu::Staff;
        default:            return Menu::Sysop;
    }
}

// menuFromText: the word after ? or HELP. Menu::Hidden means "all of them",
// which is safe as a marker because a Hidden command is never listed.
Menu menuFromText(const char* arg) {
    if (!arg || !*arg) return Menu::Main;
    if (ieq(arg, "all"))                               return Menu::Hidden;
    if (ieq(arg, "chat") || ieq(arg, "mail"))          return Menu::Chat;
    if (ieq(arg, "account") || ieq(arg, "me"))         return Menu::Account;
    if (ieq(arg, "staff") || ieq(arg, "co"))           return Menu::Staff;
    if (ieq(arg, "sysop"))                             return Menu::Sysop;
    return Menu::Main;
}

} // namespace

// ---------------------------------------------------------------------------
// cmdHelp: "?" alone is the main menu; "? staff", "? chat", "? account",
// "? sysop" and "? all" pick the others.
// ---------------------------------------------------------------------------
void Bbs::cmdHelp(Session& s, const char* arg) {
    Menu m = menuFromText(arg);
    s.helpAll  = m == Menu::Hidden;
    s.helpMenu = s.helpAll ? Menu::Main : m;
    startList(s, ListKind::Help);
}

// ---------------------------------------------------------------------------
// helpUsage: the usage column with the shortcut letter picked out, so
// "[W]HO [n]" prints as a bright W inside a quieter word. Anything in
// brackets that is not a single letter (the "[n]" argument) is left alone.
// ---------------------------------------------------------------------------
void Bbs::helpUsage(Session& s, const char* usage, bool dim) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    Color body = dim ? Color::Grey : Color::LightBlue;
    uint8_t col = 0;
    t.color(tl, body);
    for (const char* u = usage; *u && col + 1 < kUsageCol; ++u) {
        if (!col && *u == '[' && u[1] && u[2] == ']') {  // the shortcut letter
            t.color(tl, Color::Yellow);
            t.ch(tl, u[1]);
            t.color(tl, body);
            u += 2;
            ++col;
            continue;
        }
        t.ch(tl, *u);
        ++col;
    }
    for (; col < kUsageCol; ++col) t.ch(tl, ' ');
}

// ---------------------------------------------------------------------------
// helpRow: one command's line, or the next slice of a wrapped description.
// False means the command is not listed for this session.
// ---------------------------------------------------------------------------
bool Bbs::helpRow(Session& s, uint8_t index, uint8_t plugin) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    const Command* c = commandAt(index);
    if (!c || (c->flags & CF_HIDDEN) || c->menu == Menu::Hidden) return false;
    if (!c->usage[0]) return false;
    if (!allowed(s, *c, plugin)) return false;

    uint8_t descW = static_cast<uint8_t>(rowWidth(s) - kUsageCol);
    const char* d = c->help + s.listSub;
    while (*d == ' ') ++d;
    size_t take = wrapTake(d, descW);

    if (s.listSub) {
        t.color(tl, Color::LightBlue);
        for (uint8_t i = 0; i < kUsageCol; ++i) t.ch(tl, ' ');
    } else {
        helpUsage(s, c->usage, false);
    }
    t.color(tl, Color::Grey);
    t.textN(tl, d, take);
    t.nl(tl);

    const char* rest = d + take;
    while (*rest == ' ') ++rest;
    s.listSub = *rest ? static_cast<uint8_t>(rest - c->help) : 0;
    return true;
}

// ---------------------------------------------------------------------------
// helpWanted: does this command belong in the menu being drawn? The chat
// commands people use every call are also lifted onto the main menu, but
// only there, so "? all" never prints one twice.
// ---------------------------------------------------------------------------
bool Bbs::helpWanted(const Session& s, const Command& c) const {
    if (c.menu == s.helpMenu) return true;
    return !s.helpAll && s.helpMenu == Menu::Main &&
           c.menu == Menu::Chat && c.rank < kMainRank;
}

// helpEmpty: true when a section has nothing this session may run, so
// "? all" can skip its title bar instead of printing a bare heading.
bool Bbs::helpEmpty(const Session& s) const {
    for (uint8_t i = 0;; ++i) {
        const Command* c = commandAt(i);
        if (!c) return true;
        if ((c->flags & CF_HIDDEN) || c->menu == Menu::Hidden || !c->usage[0]) continue;
        if (helpWanted(s, *c) && allowed(s, *c, pluginOf(i))) return false;
    }
}

// ---------------------------------------------------------------------------
// rowHelp: the menu, one row per call. Commands come out in table order,
// which is kept sorted by how often they are used. listIdx 0 is the title
// bar, 1..total the commands, then the closing rows. "? all" walks every
// section in turn, each with its own title bar.
// ---------------------------------------------------------------------------
bool Bbs::rowHelp(Session& s) {
    uint8_t total = 0;
    while (commandAt(total)) ++total;

    for (;;) {
        if (s.listIdx == 0) {
            if (s.helpAll && helpEmpty(s)) {             // nothing here for them
                if (s.helpMenu == Menu::Sysop) { s.listIdx = static_cast<uint8_t>(total + 1); continue; }
                s.helpMenu = menuNext(s.helpMenu);
                continue;
            }
            ++s.listIdx;
            rowTitle(s, menuTitle(s.helpMenu), nullptr);
            return true;
        }

        while (s.listIdx <= total) {                      // the commands themselves
            uint8_t at = static_cast<uint8_t>(s.listIdx - 1);
            const Command* c = commandAt(at);
            if (!c) break;
            if (helpWanted(s, *c) && helpRow(s, at, pluginOf(at))) {
                if (!s.listSub) ++s.listIdx;              // a wrapped line continues here
                return true;
            }
            ++s.listIdx;
            s.listSub = 0;
        }

        if (s.helpAll && s.helpMenu != Menu::Sysop) {      // on to the next section
            s.helpMenu = menuNext(s.helpMenu);
            s.listIdx  = 0;
            continue;
        }
        break;
    }

    if (s.listIdx == static_cast<uint8_t>(total + 1)) {
        ++s.listIdx;
        rowRule(s);
        return true;
    }
    if (s.listIdx == static_cast<uint8_t>(total + 2)) {
        ++s.listIdx;
        if (s.helpAll) {
            rowText(s, Color::DarkGrey, "? on its own for the short menu");
            return true;
        }
        if (s.helpMenu != Menu::Main) {
            rowText(s, Color::DarkGrey, "? = this menu   ? all = everything");
            return true;
        }
        char line[48];
        snprintf(line, sizeof(line), "More: ? %s  ? %s  ? %s", menuName(Menu::Chat),
                 menuName(Menu::Account), s.perms ? menuName(Menu::Staff) : "all");
        rowText(s, Color::LightGreen, line);
        return true;
    }
    if (s.listIdx == static_cast<uint8_t>(total + 3)) {
        ++s.listIdx;
        if (s.helpAll || s.helpMenu != Menu::Main || !s.perms) return false;
        rowText(s, Color::LightGreen, "      ? sysop  ? all");
        return true;
    }
    return false;
}

// ===========================================================================
// WHO
// ===========================================================================

void Bbs::cmdWho(Session& s, const char* arg) {
    if (!*arg) { startList(s, ListKind::Who); return; }
    const SysConfig& cfg = syscfg::get();
    uint8_t secs = 0;
    if (!parseSeconds(arg, cfg.whoMin, cfg.whoMax, secs)) {
        char buf[48];
        snprintf(buf, sizeof(buf), "WHO n: n is %u to %u seconds.", cfg.whoMin, cfg.whoMax);
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, buf);
        prompt(s);
        return;
    }
    startWatch(s, ListKind::Who, secs);
}

// ---------------------------------------------------------------------------
// rowWho: title bar with the clock, header, the 6 caller nodes, the sysop
// if shown, a closing rule. The busy line is never listed. A refresh
// screen keeps a fixed height. Staff with NODES see what each caller last
// ran (Doing) instead of the terminal type.
// ---------------------------------------------------------------------------
bool Bbs::rowWho(Session& s) {
    char buf[80];
    char on[8];
    char idle[8];
    const char* fmt = "%s%c%-12.12s %-9.9s %3s %5s";
    uint32_t now = plat::millis();
    bool refresh = s.watch != ListKind::None;
    bool staff   = can(s, PERM_NODES);
    bool seeAll  = s.perms != 0;                     // staff also see hidden and lurking

    for (;;) {
        uint8_t i = s.listIdx++;
        if (i == 0) {
            char when[24] = "";
            if (clk::valid()) clk::fmt(when, sizeof(when), "%a %d %b %H:%M:%S");
            rowTitle(s, "Who's online", when[0] ? when : nullptr);
            return true;
        }
        if (i == 1) {
            snprintf(buf, sizeof(buf), fmt, " N", ' ', "Handle", staff ? "Doing" : "Terminal", "Min", "Idle");
            rowText(s, Color::LightBlue, buf);
            return true;
        }
        uint8_t k = static_cast<uint8_t>(i - 2);
        const Session* n = nullptr;
        if (k < BBS_MAX_NODES) {
            n = &nodes_[k];
        } else if (k == BBS_MAX_NODES) {
            if (sysop_.st == SState::Free || (!seeAll && (!sysop_.visible || sysop_.lurk))) {
                if (refresh) { rowText(s, Color::Grey, ""); return true; }   // keep the frame height
                continue;
            }
            n = &sysop_;
        } else if (k == BBS_MAX_NODES + 1) {
            rowRule(s);
            return true;
        } else if (k == BBS_MAX_NODES + 2) {
            uint8_t col = 0;                             // the key, each mark in its own colour
            rowSeg(s, markColor('*'), "*", col);
            rowSeg(s, Color::Grey, "GUEST  ", col);
            rowSeg(s, markColor('>'), ">", col);
            rowSeg(s, Color::Grey, "CO-SYSOP  ", col);
            rowSeg(s, markColor(']'), "]", col);
            rowSeg(s, Color::Grey, "SYSOP", col);
            rowEnd(s, col);
            return true;
        } else {
            return false;
        }

        bool hidden = n != &s && (!n->visible || n->lurk);   // hidden co-sysop looks like a free line
        uint8_t col = 0;
        NodeStr nodeStr = nodeLabel(*n);
        if (n->st == SState::Free || (hidden && !seeAll)) {
            rowSeg(s, Color::DarkGrey, nodeStr.t, col);
            rowSeg(s, Color::DarkGrey, " -- waiting for caller --", col);
            rowEnd(s, col);
            return true;
        }
        char h[16];
        if (n->user[0]) listHandle(h, sizeof(h), n->user, 12);
        else snprintf(h, sizeof(h), "%s", n->st == SState::Detect || n->st == SState::Intro ? "(connecting)" : "(logging in)");
        const char* what = staff ? (hidden ? (n->lurk ? "lurking" : "hidden") : doingText(*n)) : n->term.name();
        snprintf(on, sizeof(on), "%u", static_cast<unsigned>((now - n->connectedAt) / 60000u));
        fmtIdle(idle, sizeof(idle), now - n->lastInput);

        char mark[2] = { markFor(*n), '\0' };
        rowSeg(s, Color::LightBlue, nodeStr.t, col);            // node
        rowSeg(s, markColor(mark[0]), mark, col);               // rank marker
        snprintf(buf, sizeof(buf), "%-12.12s ", h);
        rowSeg(s, n == &s ? Color::White : (hidden ? Color::DarkGrey : Color::LightGreen), buf, col);
        snprintf(buf, sizeof(buf), "%-10.10s ", what);
        rowSeg(s, hidden ? Color::DarkGrey : (staff ? Color::Cyan : Color::Grey), buf, col);
        snprintf(buf, sizeof(buf), "%3s ", on);
        rowSeg(s, Color::Grey, buf, col);
        snprintf(buf, sizeof(buf), "%5s", idle);
        rowSeg(s, (now - n->lastInput) > 300000u ? Color::DarkGrey : Color::Grey, buf, col);
        rowEnd(s, col);
        return true;
    }
}

// ---------------------------------------------------------------------------
// rowWatchFooter: two lines under a refresh screen, the last without a
// newline so a 24-row terminal never scrolls. listSub counts 1, 2, done.
// ---------------------------------------------------------------------------
bool Bbs::rowWatchFooter(Session& s) {
    char buf[64];
    if (s.listSub == 1) {
        snprintf(buf, sizeof(buf), "Refresh %us, any key stops", s.watchSecs);
        rowText(s, Color::Cyan, buf);
        s.listSub = 2;
        return true;
    }
    if (s.listSub == 2) {
        int32_t left = idleSecondsLeft(s, plat::millis());
        const char* page = s.mb.empty() ? "" : "  page!";
        if (left < 0) snprintf(buf, sizeof(buf), "No idle limit%s", page);
        else          snprintf(buf, sizeof(buf), "Idle counts: %ld:%02ld left%s",
                               static_cast<long>(left / 60), static_cast<long>(left % 60), page);
        buf[rowWidth(s)] = '\0';                      // never wrap a refresh frame
        rowText(s, left >= 0 && left < 60 ? Color::LightRed : Color::DarkGrey, buf, false);
        s.listSub = 3;
        return true;
    }
    return false;
}

// ===========================================================================
// DASH
// ===========================================================================

void Bbs::cmdDash(Session& s, const char* arg) {
    if (!*arg) {                                     // one screen: no [More]
        startList(s, ListKind::Dash);
        s.nonstop = true;
        return;
    }
    const SysConfig& cfg = syscfg::get();
    uint8_t secs = 0;
    if (!parseSeconds(arg, cfg.whoMin, cfg.whoMax, secs)) {
        char buf[48];
        snprintf(buf, sizeof(buf), "DASH n: n is %u to %u seconds.", cfg.whoMin, cfg.whoMax);
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, buf);
        prompt(s);
        return;
    }
    startWatch(s, ListKind::Dash, secs);
}

// ---------------------------------------------------------------------------
// dashNode: the session shown in DASH node row `slot`, or nullptr for none.
//
// Busy lines first in node order, then the sysop if it is up, then free lines
// to fill whatever rows are left. A quiet board therefore looks the way it
// always did, a row per waiting line, and a busy one spends its six rows on
// the callers instead of on "waiting for caller" repeated sixteen times.
// ---------------------------------------------------------------------------
const Session* Bbs::dashNode(uint8_t slot) const {
    uint8_t seen = 0;
    for (uint8_t pass = 0; pass < 2; ++pass) {           // 0 = in use, 1 = free
        for (uint8_t k = 0; k <= BBS_MAX_NODES; ++k) {
            const Session& n = k < BBS_MAX_NODES ? nodes_[k] : sysop_;
            bool inUse = n.st != SState::Free;
            if (inUse != (pass == 0)) continue;
            if (&n == &sysop_ && !inUse) continue;       // an idle sysop line is not worth a row
            if (seen++ == slot) return &n;
        }
    }
    return nullptr;
}

// dashBusyCount: lines in use, sysop included, for the summary row
uint8_t Bbs::dashBusyCount() const {
    uint8_t n = 0;
    for (uint8_t k = 0; k < BBS_MAX_NODES; ++k) if (nodes_[k].st != SState::Free) ++n;
    if (sysop_.st != SState::Free) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// rowDash: 22 rows, 40 columns. Clock, uptime, NTP, memory, the busiest
// nodes with what each is doing, the day's calls, bans, busy line, Wi-Fi
// signal, backup window, the last 5 calls.
//
// The row map is fixed because DASH is a refresh screen: it redraws from
// home, so a frame taller than the terminal scrolls and leaves its own tail
// behind on every pass. That is why the node block is kDashNodeRows rows
// plus one summary row rather than one row per node. At six nodes those were
// the same thing; at sixteen they are not, and the frame is what has to give.
// WHO is the paged list that still shows every line.
// ---------------------------------------------------------------------------
bool Bbs::rowDash(Session& s) {
    char buf[80];
    uint32_t now = plat::millis();
    uint8_t i = s.listIdx++;

    switch (i) {
        case 0: {
            char when[24] = "clock not set";
            if (clk::valid()) clk::fmt(when, sizeof(when), "%a %d %b %H:%M:%S");
            rowTitle(s, "SYSOP DASHBOARD", when);
            return true;
        }
        case 1: {
            uint32_t up = now / 1000u;
            snprintf(buf, sizeof(buf), "%s %s  up %ud %02u:%02u", BBS_NAME, BBS_VERSION,
                     static_cast<unsigned>(up / 86400u), static_cast<unsigned>(up / 3600u % 24u),
                     static_cast<unsigned>(up / 60u % 60u));
            rowText(s, Color::White, buf);
            return true;
        }
        case 2: {
            plat::HeapStats h = plat::heap();
            unsigned fs = static_cast<unsigned>(plugins::freeBytes() / 1024u);
            if (h.valid) {
                snprintf(buf, sizeof(buf), "NTP %s heap %uK min %uK  disk %uK", clk::valid() ? "ok" : "--",
                         static_cast<unsigned>(h.freeBytes / 1024u), static_cast<unsigned>(h.minFree / 1024u), fs);
            } else {
                snprintf(buf, sizeof(buf), "NTP %s  heap n/a on host  disk %uK", clk::valid() ? "ok" : "--", fs);
            }
            rowText(s, Color::Grey, buf);
            return true;
        }
        case 3:
        case 12:
        case 21:
            rowRule(s);
            return true;
        case 4:
            snprintf(buf, sizeof(buf), "%s%c%-12.12s %-9.9s %5s %4s", " N", ' ', "Handle", "Doing", "Idle", "Left");
            rowText(s, Color::LightBlue, buf);
            return true;
        case 13: {
            uint8_t bans = 0;
            BanList::Entry e;
            for (uint8_t k = 0; k < BBS_BAN_SLOTS; ++k) if (bans_.at(k, now, e)) ++bans;
            char today[8] = "--";
            uint32_t start = clk::todayStart();
            if (start) snprintf(today, sizeof(today), "%u", calllog::countSince(start));
            snprintf(buf, sizeof(buf), "Calls %s today  bans %u  busy %s", today, bans,
                     busy_.st == SState::Free ? "free" : "in use");
            rowText(s, Color::Grey, buf);
            return true;
        }
        case 14: {
            char wifi[16] = "WiFi --";
            int8_t rssi = plat::wifiRssi();
            if (rssi) snprintf(wifi, sizeof(wifi), "WiFi %d dBm", rssi);
            if (backup_.awaitingApproval()) {
                snprintf(buf, sizeof(buf), "%s  Backup: Y/N wait", wifi);
            } else if (backup_.isOpen()) {
                int32_t left = static_cast<int32_t>(backup_.closesAt() - now) / 1000;
                if (left < 0) left = 0;
                snprintf(buf, sizeof(buf), "%s  Backup open %ld:%02ld", wifi,
                         static_cast<long>(left / 60), static_cast<long>(left % 60));
            } else {
                snprintf(buf, sizeof(buf), "%s  Backup closed", wifi);
            }
            rowText(s, backup_.isOpen() ? Color::LightGreen : Color::Grey, buf);
            return true;
        }
        case 15:
            snprintf(buf, sizeof(buf), "%-14s%s", "Last calls", kMarkKey);
            rowText(s, Color::Yellow, buf);
            return true;
        default:
            break;
    }

    if (i >= 5 && i < 5 + kDashNodeRows) {                      // the busiest lines
        const Session* np = dashNode(static_cast<uint8_t>(i - 5));
        if (!np) { rowText(s, Color::Grey, ""); return true; }  // blank, to keep the frame height
        const Session& n = *np;
        if (n.st == SState::Free) {
            snprintf(buf, sizeof(buf), "%s -", nodeLabel(n).t);
            rowText(s, Color::DarkGrey, buf);
            return true;
        }
        char idle[8];
        char left[12] = "--";
        fmtIdle(idle, sizeof(idle), now - n.lastInput);
        if (n.role == Role::Caller && n.loggedIn && !unlimited(n)) {
            int32_t sec = secondsLeft(n, now);
            if (sec != INT32_MAX) snprintf(left, sizeof(left), "%ld", static_cast<long>(sec > 0 ? (sec + 59) / 60 : 0));
        }
        char h[16] = "(logging in)";
        if (n.user[0]) listHandle(h, sizeof(h), n.user, 12);
        bool hidden = &n != &s && (!n.visible || n.lurk);
        snprintf(buf, sizeof(buf), "%s%c%-12.12s %-9.9s %5s %4s", nodeLabel(n).t, markFor(n), h,
                 hidden ? (n.lurk ? "lurking" : "hidden") : doingText(n), idle, left);
        rowText(s, &n == &s ? Color::White : Color::Grey, buf);
        return true;
    }

    if (i == 5 + kDashNodeRows) {                               // what the rows could not hold
        uint8_t used = dashBusyCount();
        uint8_t free = static_cast<uint8_t>(BBS_MAX_NODES - (used > BBS_MAX_NODES ? BBS_MAX_NODES : used));
        if (used > kDashNodeRows) {
            snprintf(buf, sizeof(buf), "%u in use, %u not shown, %u free",
                     used, static_cast<unsigned>(used - kDashNodeRows), free);
        } else {
            snprintf(buf, sizeof(buf), "%u of %u lines free", free, BBS_MAX_NODES);
        }
        rowText(s, used > kDashNodeRows ? Color::Yellow : Color::DarkGrey, buf);
        return true;
    }

    if (i >= 22) {                                              // what the plugins say
        uint8_t wanted = static_cast<uint8_t>(i - 22);
        uint8_t seen = 0;
        for (uint8_t k = 0; k < plugins::count(); ++k) {
            const Plugin* p = plugins::at(k);
            if (!plugins::running(k) || !p->status) continue;
            const char* line = p->status();
            if (!line || !*line) continue;
            if (seen++ != wanted) continue;
            rowText(s, Color::Cyan, line);
            return true;
        }
        return false;
    }

    if (i >= 16 && i <= 20) {                                   // last 5 calls
        CallRec r;
        if (calllog::get(static_cast<uint8_t>(i - 16), r)) {
            char when[16];
            clk::fmtEpoch(when, sizeof(when), "%m/%d %H:%M", r.start);
            NodeStr node = nodeNum(r.node);
            if (r.flags & CallRec::F_SYSOP) { node.t[0] = 'S'; node.t[1] = '\0'; }
            char h[16];
            listHandle(h, sizeof(h), r.user, 12);
            snprintf(buf, sizeof(buf), "%c%-10.10s %-2s %-11s %4u min", markForFlags(r.flags), h, node.t, when,
                     static_cast<unsigned>((r.secs + 59u) / 60u));
            rowText(s, Color::Grey, buf);
        } else {
            rowText(s, Color::Grey, "");
        }
        return true;
    }
    return false;
}

// ===========================================================================
// LAST
// ===========================================================================

// ---------------------------------------------------------------------------
// rowLast: newest calls first; sysop calls only shown to staff
// ---------------------------------------------------------------------------
bool Bbs::rowLast(Session& s) {
    bool wide  = s.term.cols() >= 60;
    bool staff = s.perms != 0;             // sees sysop-node calls
    bool sysop = can(s, PERM_NODES);       // sees IPs
    char buf[96];

    if (s.listSub == 1) {                            // after the closing rule: the key
        s.listSub = 2;
        rowText(s, Color::DarkGrey, kMarkKey);
        return true;
    }
    if (s.listSub) return false;
    uint8_t i = s.listIdx++;
    if (i == 0) { rowTitle(s, "Last callers"); return true; }
    if (i == 1) {
        if (wide && sysop) snprintf(buf, sizeof(buf), " %-20s N %-10s %-11s %4s %s", "Handle", "Terminal", "When", "Min", "IP");
        else if (wide)     snprintf(buf, sizeof(buf), " %-20s N %-10s %-11s %4s", "Handle", "Terminal", "When", "Min");
        else               snprintf(buf, sizeof(buf), " %-11s N %-11s %4s", "Handle", "When", "Min");
        rowText(s, Color::LightBlue, buf);
        return true;
    }

    uint8_t total = calllog::count();
    if (i == 2 && total == 0) {
        rowText(s, Color::Grey, "No calls logged yet.");
        s.listIdx = static_cast<uint8_t>(total + 3);  // next row: the rule
        return true;
    }

    // row index i >= 2 maps to record back = i - 2; skipped sysop records
    // advance listIdx past themselves so each call still emits one line
    CallRec r;
    uint8_t back = static_cast<uint8_t>(i - 2);
    while (back < total) {
        bool ok = calllog::get(back, r);
        ++back;
        if (!ok || ((r.flags & CallRec::F_SYSOP) && !staff)) continue;
        s.listIdx = static_cast<uint8_t>(back + 2);

        char when[16];
        clk::fmtEpoch(when, sizeof(when), "%m/%d %H:%M", r.start);
        char node = (r.flags & CallRec::F_SYSOP) ? 'S' : static_cast<char>('0' + (r.node % 10));
        unsigned mins = static_cast<unsigned>((r.secs + 59u) / 60u);
        const char* term = Term::nameOf(static_cast<TermType>(r.term), static_cast<Charset>(r.charset));
        char h[24];
        char mk = markForFlags(r.flags);
        listHandle(h, sizeof(h), r.user, wide ? 20 : 11);
        if (wide && sysop) snprintf(buf, sizeof(buf), "%c%-20.20s %c %-10.10s %-11s %4u %s", mk, h, node, term, when, mins, r.ip);
        else if (wide)     snprintf(buf, sizeof(buf), "%c%-20.20s %c %-10.10s %-11s %4u", mk, h, node, term, when, mins);
        else               snprintf(buf, sizeof(buf), "%c%-11.11s %c %-11s %4u", mk, h, node, when, mins);
        rowText(s, Color::Grey, buf);
        return true;
    }
    s.listSub = 1;
    rowRule(s);
    return true;
}

// ===========================================================================
// Simple commands
// ===========================================================================

// ---------------------------------------------------------------------------
// cmdMem: heap and session sizing
// ---------------------------------------------------------------------------
void Bbs::cmdMem(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    char buf[48];
    plat::HeapStats h = plat::heap();

    rowTitle(s, "Memory", BBS_VERSION);
    if (h.valid) {
        statNum(s, "Heap free", h.freeBytes, "bytes");
        statNum(s, "Heap low", h.minFree, "since boot");
        statNum(s, "Biggest block", h.largestBlock, "bytes");
        long used = static_cast<long>(heapBaseline_) - static_cast<long>(h.freeBytes);
        char num[16];
        fmtCommas(static_cast<uint32_t>(used < 0 ? -used : used), num, sizeof(num));
        statRow(s, "Used by BBS", num, used > 0 ? Color::Yellow : Color::LightGreen, "since boot");
    } else {
        t.color(tl, Color::DarkGrey);
        t.text(tl, "Heap stats are an ESP32 thing.");
        t.nl(tl);
    }
    // Where the RAM actually went, not just what is left of it.
    //
    // The session pool is the largest single thing this firmware owns and it
    // was invisible: it is decided at compile time, so it never appears as
    // heap usage, and a sysop looking at a small heap had no way to see that
    // sixteen caller nodes are holding a hundred kilobytes. Showing the two
    // figures side by side is the difference between "something ate my RAM"
    // and "the node count costs this much".
    char buf2[48];
    char num[16];
    snprintf(buf2, sizeof(buf2), "x %u = %u KB", static_cast<unsigned>(kSessions),
             static_cast<unsigned>(sizeof(Session) * kSessions / 1024u));
    fmtCommas(static_cast<uint32_t>(sizeof(Session)), num, sizeof(num));
    statRow(s, "Session", num, Color::LightGreen, buf2);
    if (h.valid && h.totalBytes) {
        fmtCommas(h.totalBytes, num, sizeof(num));
        snprintf(buf2, sizeof(buf2), "%u%% free",
                 static_cast<unsigned>(h.freeBytes * 100u / h.totalBytes));
        statRow(s, "Heap total", num, Color::Grey, buf2);
    }
    snprintf(buf, sizeof(buf), "of %u", static_cast<unsigned>(BBS_MAX_NODES));
    fmtCommas(activeNodes(), num, sizeof(num));
    statRow(s, "Nodes busy", num, Color::LightGreen, buf);
    statNum(s, "Disk free", plugins::freeBytes(), "bytes");
    // The card, when there is one. In megabytes rather than bytes: a figure
    // with seven digits on it is not a figure anybody reads, and the point
    // of the row is whether there is room, not how many bytes of room.
    plat::SdInfo sd = plat::sdInfo();
    if (sd.mounted) {
        fmtCommas(sd.freeKB / 1024u, num, sizeof(num));
        snprintf(buf, sizeof(buf), "MB of %u", static_cast<unsigned>(sd.totalKB / 1024u));
        statRow(s, "Card free", num, Color::LightGreen, buf);
    }
    rowRule(s);
}

// ---------------------------------------------------------------------------
// fmtUptime: "3d 04:12" from milliseconds. millis wraps at 49 days, which
// is honest enough for a board that reboots for a flash every few days.
// ---------------------------------------------------------------------------
namespace {

void fmtUptime(char* out, size_t n, uint32_t ms) {
    uint32_t secs = ms / 1000u;
    uint32_t days = secs / 86400u;
    uint32_t hrs  = (secs % 86400u) / 3600u;
    uint32_t mins = (secs % 3600u) / 60u;
    if (days) snprintf(out, n, "%ud %02u:%02u", static_cast<unsigned>(days),
                       static_cast<unsigned>(hrs), static_cast<unsigned>(mins));
    else      snprintf(out, n, "%02u:%02u:%02u", static_cast<unsigned>(hrs),
                       static_cast<unsigned>(mins), static_cast<unsigned>(secs % 60u));
}

// signalWord: what an RSSI in dBm means to a human, and how alarmed to look
const char* signalWord(int8_t rssi, Color& c) {
    if (rssi == 0)    { c = Color::DarkGrey;   return "no radio"; }
    if (rssi >= -55)  { c = Color::LightGreen; return "excellent"; }
    if (rssi >= -67)  { c = Color::LightGreen; return "good"; }
    if (rssi >= -75)  { c = Color::Yellow;     return "fair"; }
    return              (c = Color::LightRed,         "weak");
}

} // namespace

// ---------------------------------------------------------------------------
// rowSys: the whole board on one screen for staff: radio, memory, storage,
// how hard the scheduler is working and how busy the lines have been. Every
// figure here is already kept, so the screen costs a few hundred bytes of
// formatting and nothing else.
// ---------------------------------------------------------------------------
bool Bbs::rowSys(Session& s) {
    char buf[48], num[16];
    uint8_t i = s.listIdx++;
    plat::NetInfo net = plat::netInfo();
    plat::HeapStats h = plat::heap();

    switch (i) {
        case 0:  rowTitle(s, "System", BBS_VERSION); return true;

        case 1:  rowSection(s, "network"); return true;
        case 2:  statRow(s, "Wi-Fi", net.ssid[0] ? net.ssid : "-", Color::White); return true;
        case 3: {
            int8_t rssi = net.valid ? net.rssi : plat::wifiRssi();
            Color c = Color::Grey;
            const char* word = signalWord(rssi, c);
            if (rssi) snprintf(num, sizeof(num), "%d dBm", static_cast<int>(rssi));
            else      snprintf(num, sizeof(num), "-");
            statRow(s, "Signal", num, c, word);
            return true;
        }
        case 4:
            if (net.channel) snprintf(num, sizeof(num), "%u", static_cast<unsigned>(net.channel));
            else             snprintf(num, sizeof(num), "-");
            statRow(s, "Channel", num, Color::LightGreen);
            return true;
        case 5:  statRow(s, "Address", net.ip[0] ? net.ip : "-", Color::White); return true;
        case 6:
            snprintf(num, sizeof(num), "%u", static_cast<unsigned>(BBS_PORT));
            statRow(s, "Port", num, Color::LightGreen, syscfg::get().hostname);
            return true;

        case 7:  rowSection(s, "memory"); return true;
        case 8:
            if (h.valid) statNum(s, "Heap free", h.freeBytes, "bytes");
            else         statRow(s, "Heap free", "-", Color::DarkGrey, "host build");
            return true;
        case 9:
            if (h.valid) statNum(s, "Heap low", h.minFree, "since boot");
            else         statRow(s, "Heap low", "-", Color::DarkGrey);
            return true;
        case 10:
            if (h.valid) statNum(s, "Biggest blk", h.largestBlock, "bytes");
            else         statRow(s, "Biggest blk", "-", Color::DarkGrey);
            return true;
        case 11:
            fmtCommas(static_cast<uint32_t>(sizeof(Session)), num, sizeof(num));
            snprintf(buf, sizeof(buf), "x %u sessions", static_cast<unsigned>(kSessions));
            statRow(s, "Session", num, Color::LightGreen, buf);
            return true;

        case 12: rowSection(s, "storage"); return true;
        case 13: {
            uint32_t total = 0, used = 0;
            if (plat::fsInfo(total, used)) {
                fmtCommas(used, num, sizeof(num));
                char tot[16];
                fmtCommas(total, tot, sizeof(tot));
                snprintf(buf, sizeof(buf), "of %s", tot);
                statRow(s, "Data used", num, used * 10u > total * 9u ? Color::LightRed : Color::LightGreen, buf);
            } else {
                statRow(s, "Data used", "-", Color::DarkGrey);
            }
            return true;
        }
        case 14: statNum(s, "Data free", plugins::freeBytes(), "bytes"); return true;
        case 15: statNum(s, "Held back", plugins::reserveBytes(), "for the board"); return true;

        case 16: rowSection(s, "load"); return true;
        case 17:
            fmtUptime(buf, sizeof(buf), plat::millis());
            // An uptime that keeps starting over is the only symptom of a
            // board that restarts on its own, so say why it started.
            statRow(s, "Uptime", buf, Color::White,
                    bootWasCrash() ? bootReason() : nullptr);
            return true;
        case 18: {
            char when[24] = "-";
            if (clk::valid()) clk::fmt(when, sizeof(when), "%H:%M:%S");
            statRow(s, "Clock", when, Color::White, clk::valid() ? nullptr : "not set");
            return true;
        }
        case 19: statNum(s, "Loop avg", loopAvgUs_, "us of work"); return true;
        case 20: statNum(s, "Loop worst", loopMaxUs_, "us"); return true;
        case 21: statNum(s, "Loop passes", loopPasses_, nullptr); return true;

        case 22: rowSection(s, "traffic"); return true;
        case 23:
            snprintf(num, sizeof(num), "%u", static_cast<unsigned>(activeNodes()));
            snprintf(buf, sizeof(buf), "of %u, peak %u", static_cast<unsigned>(BBS_MAX_NODES),
                     static_cast<unsigned>(peakNodes_));
            statRow(s, "Nodes busy", num, Color::LightGreen, buf);
            return true;
        case 24: statNum(s, "Calls", callsBoot_, "since boot"); return true;
        case 25:
            snprintf(num, sizeof(num), "%u", static_cast<unsigned>(calllog::count()));
            snprintf(buf, sizeof(buf), "of %u kept", static_cast<unsigned>(BBS_CALLLOG_SIZE));
            statRow(s, "Log", num, Color::LightGreen, buf);
            return true;
        case 26: {
            uint8_t run = 0;
            for (uint8_t k = 0; k < plugins::count(); ++k) if (plugins::running(k)) ++run;
            snprintf(num, sizeof(num), "%u", static_cast<unsigned>(run));
            snprintf(buf, sizeof(buf), "of %u compiled in", static_cast<unsigned>(plugins::count()));
            statRow(s, "Plugins", num, Color::LightGreen, buf);
            return true;
        }
        case 27: {
            uint8_t live = 0;                                  // only the bans still running
            BanList::Entry e;
            for (uint8_t k = 0; k < BBS_BAN_SLOTS; ++k) if (bans_.at(k, plat::millis(), e)) ++live;
            statNum(s, "IP bans", live, live ? "active" : nullptr);
            return true;
        }

        case 28: rowRule(s); return true;
        case 29: rowText(s, Color::DarkGrey, "CALLS shows the board hour by hour"); return true;
        default: return false;
    }
}

// ---------------------------------------------------------------------------
// cmdCalls: bucket the caller log by hour of the day before drawing, so the
// screen is one pass over the log and the rows are just arithmetic.
// ---------------------------------------------------------------------------
void Bbs::cmdCalls(Session& s) {
    memset(callHours_, 0, sizeof(callHours_));
    callsCounted_ = 0;
    uint8_t n = calllog::count();
    for (uint8_t i = 0; i < n; ++i) {
        CallRec r;
        if (!calllog::get(i, r) || !r.start) continue;
        char hh[4];
        clk::fmtEpoch(hh, sizeof(hh), "%H", r.start);
        if (hh[0] < '0' || hh[0] > '9') continue;              // clock was not set
        uint8_t hour = static_cast<uint8_t>((hh[0] - '0') * 10 + (hh[1] - '0'));
        if (hour > 23) continue;
        ++callHours_[hour];
        ++callsCounted_;
    }
    startList(s, ListKind::Calls);
}

// ---------------------------------------------------------------------------
// rowCalls: calls per hour as a bar chart. The busiest hours are picked out
// so a glance says when the board is worth being around for.
// ---------------------------------------------------------------------------
bool Bbs::rowCalls(Session& s) {
    char buf[32];
    uint8_t i = s.listIdx++;

    uint16_t peak = 0;
    uint8_t  peakAt = 0;
    for (uint8_t k = 0; k < 24; ++k) if (callHours_[k] > peak) { peak = callHours_[k]; peakAt = k; }

    if (i == 0) {
        snprintf(buf, sizeof(buf), "%u calls", static_cast<unsigned>(callsCounted_));
        rowTitle(s, "Calls by hour", buf);
        return true;
    }
    if (i == 1 && !callsCounted_) {
        s.listIdx = 25;                                    // no bars worth drawing
        rowText(s, Color::DarkGrey, "Nothing logged with a clock yet.");
        return true;
    }
    if (i >= 1 && i <= 24) {
        uint8_t hour = static_cast<uint8_t>(i - 1);
        uint16_t v = callHours_[hour];
        uint8_t room = static_cast<uint8_t>(rowWidth(s) - 10);         // hour + count columns
        uint8_t bar  = peak ? static_cast<uint8_t>((static_cast<uint32_t>(v) * room) / peak) : 0;
        if (v && !bar) bar = 1;                                        // one call still shows

        uint8_t col = 0;
        snprintf(buf, sizeof(buf), "%02u ", static_cast<unsigned>(hour));
        rowSeg(s, v ? Color::LightBlue : Color::DarkGrey, buf, col);
        s.term.color(s.tl, v == peak && peak ? Color::Yellow : Color::LightGreen);
        for (uint8_t k = 0; k < bar; ++k) { s.term.glyph(s.tl, Glyph::Block); ++col; }
        for (uint8_t k = bar; k < room; ++k) { s.term.ch(s.tl, ' '); ++col; }
        snprintf(buf, sizeof(buf), " %4u", static_cast<unsigned>(v));
        rowSeg(s, v ? Color::LightGrey : Color::DarkGrey, buf, col);
        rowEnd(s, col);
        return true;
    }
    if (i == 25) { rowRule(s); return true; }
    if (i == 26) {
        if (!callsCounted_) return false;
        snprintf(buf, sizeof(buf), "Busiest %02u:00 with %u", static_cast<unsigned>(peakAt),
                 static_cast<unsigned>(peak));
        rowText(s, Color::Cyan, buf);
        return true;
    }
    if (i == 27) {
        if (!callsCounted_) return false;
        snprintf(buf, sizeof(buf), "Last %u calls kept", static_cast<unsigned>(BBS_CALLLOG_SIZE));
        rowText(s, Color::DarkGrey, buf);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// cmdPrivacy: the same disclosure the sign-up offers, available any time.
// A caller who agreed to something months ago should be able to read it
// again without making a new account.
// ---------------------------------------------------------------------------
void Bbs::cmdPrivacy(Session& s) {
    showPrivacy(s, AfterKey::Prompt);
}

// ---------------------------------------------------------------------------
// cmdAbout: screens/about.* if it is there, a built-in card if it is not
// ---------------------------------------------------------------------------
void Bbs::cmdAbout(Session& s) {
    if (playScreen(s, "about")) return;
    Term& t = s.term;
    Timeline& tl = s.tl;
    char buf[64];
    rowTitle(s, "About", BBS_VERSION);
    t.color(tl, Color::White);
    t.text(tl, BBS_NAME);
    t.nl(tl);
    t.color(tl, Color::Grey);
    t.text(tl, "A telnet BBS on a bare ESP32.");
    t.nl(tl);
    snprintf(buf, sizeof(buf), "Copyright 2026 - Robert Mech");
    t.text(tl, buf);
    t.nl(tl);
    t.text(tl, "Free software, GPL v2 or later.");
    t.nl(tl);
    rowRule(s);
    prompt(s);
}

// ---------------------------------------------------------------------------
// rowPlugins: what is compiled in, what is running, and why not
// ---------------------------------------------------------------------------
bool Bbs::rowPlugins(Session& s) {
    char buf[80];
    uint8_t i = s.listIdx++;
    if (i == 0) {
        snprintf(buf, sizeof(buf), "%u of %u", plugins::count(), BBS_MAX_PLUGINS);
        rowTitle(s, "Plugins", buf);
        return true;
    }
    if (i == 1) {
        rowText(s, Color::LightBlue, " Name      Ver   State");
        return true;
    }
    uint8_t k = static_cast<uint8_t>(i - 2);
    if (k < plugins::count()) {
        const Plugin* p = plugins::at(k);
        snprintf(buf, sizeof(buf), " %-9.9s %-5.5s %-21.21s", p->info.name, p->info.version,
                 plugins::running(k) ? "running" : plugins::whyNot(k));
        rowText(s, plugins::running(k) ? Color::Grey : Color::DarkGrey, buf);
        return true;
    }
    if (k == plugins::count()) {
        snprintf(buf, sizeof(buf), "Disk free %uK, reserve %uK",
                 static_cast<unsigned>(plugins::freeBytes() / 1024u),
                 static_cast<unsigned>(plugins::reserveBytes() / 1024u));
        rowText(s, Color::Grey, buf);
        return true;
    }
    if (k == plugins::count() + 1) { rowRule(s); return true; }
    return false;
}

void Bbs::cmdTerm(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    char buf[48];
    rowTitle(s, "Terminal");
    t.color(tl, Color::Cyan);
    snprintf(buf, sizeof(buf), "Terminal  %s %ux%u", t.name(), t.cols(), t.rows());
    t.text(tl, buf);
    t.nl(tl);
    snprintf(buf, sizeof(buf), "Telnet    %s", s.tn.enabled() ? "on" : "off");
    t.text(tl, buf);
    t.nl(tl);
    if (tl.cps()) snprintf(buf, sizeof(buf), "Line      %u bps", tl.cps() * 10u);
    else          snprintf(buf, sizeof(buf), "Line      full speed");
    t.text(tl, buf);
}

// ---------------------------------------------------------------------------
// cmdTime: wall clock, time on, time left. Staff with TIME: TIME n +/-m.
// ---------------------------------------------------------------------------
void Bbs::cmdTime(Session& s, const char* arg, uint32_t now) {
    if (*arg && can(s, PERM_TIME)) {
        cmdTimeAdjust(s, arg);
        prompt(s);
        return;
    }
    Term& t = s.term;
    Timeline& tl = s.tl;
    char buf[64];
    char when[40];

    rowTitle(s, "Time", s.guest ? "guest pass" : nullptr);
    t.color(tl, Color::Cyan);
    if (clk::valid()) {
        clk::fmt(when, sizeof(when), "%a %d %b %Y %H:%M %Z");
        snprintf(buf, sizeof(buf), "Date     %s", when);
    } else {
        snprintf(buf, sizeof(buf), "Date     not set (waiting for NTP)");
    }
    t.text(tl, buf);
    t.nl(tl);

    uint32_t since = s.loginAt ? s.loginAt : s.connectedAt;
    snprintf(buf, sizeof(buf), "Online   %u min", static_cast<unsigned>((now - since) / 60000u));
    t.text(tl, buf);
    t.nl(tl);

    int32_t left = (s.role == Role::Caller && s.loggedIn && !unlimited(s)) ? secondsLeft(s, now) : INT32_MAX;
    if (left == INT32_MAX) snprintf(buf, sizeof(buf), "Left     no limit");
    else                   snprintf(buf, sizeof(buf), "Left     %ld min", static_cast<long>((left + 59) / 60));
    t.text(tl, buf);
    prompt(s);
}

// ---------------------------------------------------------------------------
// cmdBaud: emulate a modem line speed for this session
// ---------------------------------------------------------------------------
void Bbs::cmdBaud(Session& s, const char* arg) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    t.color(tl, Color::Cyan);
    if (!*arg) {
        t.text(tl, "BAUD 300|1200|2400|9600|19200|OFF");
        return;
    }
    long v = ieq(arg, "OFF") ? 0 : strtol(arg, nullptr, 10);
    switch (v) {
        case 0: case 300: case 1200: case 2400: case 9600: case 19200:
            fx::baud(tl, static_cast<uint32_t>(v));
            if (v) {
                char buf[32];
                snprintf(buf, sizeof(buf), "CONNECT %ld", v);
                t.text(tl, buf);
            } else {
                t.text(tl, "Line speed unlimited");
            }
            return;
        default:
            t.color(tl, Color::LightRed);
            t.text(tl, "Unsupported rate");
            return;
    }
}

// ---------------------------------------------------------------------------
// cmdPage: PAGE <node> <message>, delivered over the bus
// ---------------------------------------------------------------------------
void Bbs::cmdPage(Session& s, const char* arg) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    const char* msg = nullptr;
    Session* to = nodeByArg(arg, &msg);

    if (!to || !msg || !*msg) {
        t.color(tl, Color::LightRed);
        t.text(tl, "Usage: PAGE n message");
        return;
    }
    if (to == &s) {
        t.color(tl, Color::LightRed);
        t.text(tl, "That is you.");
        return;
    }
    bool hidden = !to->visible || to->lurk;
    if (!to->loggedIn || to->role == Role::Busy || to->dnd || hidden) {
        char buf[40];
        snprintf(buf, sizeof(buf), "Node %s is not taking pages.", nodeName(*to).t);
        t.color(tl, Color::LightRed);
        t.text(tl, buf);
        return;
    }
    post(*to, BusKind::Page, &s, msg);
    char buf[32];
    snprintf(buf, sizeof(buf), "Page sent to node %s.", nodeName(*to).t);
    t.color(tl, Color::LightGreen);
    t.text(tl, buf);
}

void Bbs::cmdDnd(Session& s) {
    s.dnd = !s.dnd;
    s.term.color(s.tl, Color::Cyan);
    s.term.text(s.tl, s.dnd ? "Pages are off." : "Pages are on.");
}

// ---------------------------------------------------------------------------
// cmdBye: BYE logs off. BYE <password> raises staff access instead:
//   sysop password     -> hidden sysop node, every permission
//   co-sysop password  -> stays on this node with that level's permissions
// Only a raise counts (same or lower level is a plain logoff). A wrong
// password is an ordinary logoff that counts toward an IP ban. Guests never
// rise to staff: for them the argument is not even checked.
// ---------------------------------------------------------------------------
void Bbs::cmdBye(Session& s, const char* arg, uint32_t now) {
    if (*arg && s.guest) {
        plat::log("bbs: node %s guest BYE with an argument: plain logoff", nodeName(s).t);
    } else if (*arg && s.role != Role::Sysop && syscfg::anyPassword()) {
        Access lv = syscfg::passwordLevel(arg);
        if (lv != Access::None) {
            bans_.clear(s.ipAddr);
            if (lv == Access::Sysop) { elevate(s, now); return; }
            if (lv > s.level && s.role == Role::Caller) { coElevate(s, lv, now); return; }
            goodbye(s, now);                     // busy line has no node to keep
            return;
        }
        plat::log("bbs: node %s staff password failed from %s", nodeName(s).t, s.ip);
        if (bans_.fail(s.ipAddr, now)) {
            plat::log("bbs: %s banned for %u min (sysop password)", s.ip,
                      static_cast<unsigned>(BBS_BAN_MS / 60000u));
        }
    }
    goodbye(s, now);
}

// ---------------------------------------------------------------------------
// fxNext: queue the next effects demo step when the previous one finished
// ---------------------------------------------------------------------------
void Bbs::fxNext(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    uint8_t w = static_cast<uint8_t>(t.cols() >= 80 ? 60 : t.cols() - 2);

    uint8_t step = s.fxStep;
    if (step != 0xFF) ++s.fxStep;

    switch (step) {
        case 0:
            t.cls(tl);
            t.color(tl, Color::Yellow);
            fx::scramble(t, tl, "TTY EFFECTS DEMO", 10, 60);
            t.nl(tl);
            t.color(tl, Color::Cyan);
            fx::rule(t, tl, w);
            t.nl(tl);
            t.color(tl, Color::Grey);
            t.text(tl, t.isPet() ? "Any key: skip  SPACE/<-: stop" : "Any key: skip  Space/ESC: stop");
            t.nl(tl);
            if (t.rows() > 25) t.nl(tl);     // 25-row screens have no spare line
            break;
        case 1:  label(t, tl, "Typewriter"); fx::typewriter(t, tl, "The quick brown fox jumps.", 45); t.nl(tl); break;
        case 2:  label(t, tl, "Dots"); t.text(tl, "Dialing"); fx::dots(t, tl, 6, 250);
                 t.text(tl, " CONNECT"); t.nl(tl); break;
        case 3:  label(t, tl, "Spin line");  fx::spinner(t, tl, fx::Spin::Line, 1800, 100);
                 t.text(tl, "done"); t.nl(tl); break;
        case 4:  label(t, tl, "Spin dots");  fx::spinner(t, tl, fx::Spin::Dots, 1600, 150);
                 t.text(tl, "done"); t.nl(tl); break;
        case 5:  label(t, tl, "Spin arrow"); fx::spinner(t, tl, fx::Spin::Arrow, 1600, 120);
                 t.text(tl, "done"); t.nl(tl); break;
        case 6:  label(t, tl, "Rubout"); fx::typeRubout(t, tl, "Erasing to start", 35, 600, 45);
                 t.text(tl, "gone"); t.nl(tl); break;
        case 7:  label(t, tl, "Rewrite"); t.text(tl, "Checking mail"); fx::dots(t, tl, 3, 300);
                 fx::rewrite(t, tl, 16, "No new mail"); t.nl(tl); break;
        case 8:  label(t, tl, "Working"); fx::working(t, tl, "Scanning ", 1500, "OK"); break;
        case 9:  label(t, tl, "Progress"); fx::progressBar(t, tl, 16, 2000); t.nl(tl); break;
        case 10: label(t, tl, "Countdown"); fx::countdown(t, tl, 5, 450, "GO!"); t.nl(tl); break;
        case 11: label(t, tl, "Scramble"); fx::scramble(t, tl, "ACCESS GRANTED", 14, 70); t.nl(tl); break;
        case 12: label(t, tl, "Line noise"); fx::lineNoise(t, tl, 18, 700);
                 t.text(tl, "clean"); t.nl(tl); break;
        case 13: label(t, tl, "Blink"); t.color(tl, Color::LightRed);
                 fx::blink(t, tl, "ALERT", 4, 250); t.color(tl, Color::Grey); t.nl(tl); break;
        case 14: label(t, tl, "Marquee"); fx::marquee(t, tl, "** UNLEASHED BBS **", 16, 80, 1);
                 t.text(tl, "done"); t.nl(tl); break;
        case 15: label(t, tl, "Cursor"); fx::cursorBlink(t, tl, 5, 250);
                 t.text(tl, "ready"); t.nl(tl); break;
        case 16: label(t, tl, "Bell"); fx::bell(t, tl); t.text(tl, "ding"); t.nl(tl); break;
        case 17: label(t, tl, "300 baud"); fx::baud(tl, 300);
                 t.text(tl, "Slow like 1984."); t.nl(tl); break;
        case 18:
            tl.setCps(s.savedCps);
            label(t, tl, "Colors");
            for (uint8_t c = 0; c < 16; ++c) {
                t.color(tl, static_cast<Color>(c));
                t.glyph(tl, Glyph::Block);
            }
            t.color(tl, Color::Grey);
            t.nl(tl);
            break;
        case 19:
            label(t, tl, "Glyphs");
            t.glyph(tl, Glyph::Block);  t.ch(tl, ' ');
            t.glyph(tl, Glyph::Shade);  t.ch(tl, ' ');
            t.glyph(tl, Glyph::HLine);  t.ch(tl, ' ');
            t.glyph(tl, Glyph::VLine);  t.ch(tl, ' ');
            t.glyph(tl, Glyph::Bullet);
            t.nl(tl);
            break;
        case 20:
            t.color(tl, Color::Cyan);
            fx::rule(t, tl, w);
            t.nl(tl);
            t.color(tl, Color::LightGreen);
            fx::typewriter(t, tl, "Demo complete.", 30);
            prompt(s);
            break;
        default:   // 0xFF: stopped by the caller
            tl.setCps(s.savedCps);
            t.reset(tl);
            t.cursor(tl, true);
            t.nl(tl);
            t.text(tl, "Demo stopped.");
            prompt(s);
            break;
    }
}
