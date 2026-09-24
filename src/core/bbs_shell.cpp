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
#include "helptext.h"
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

// fxCode: the message code for the effect just shown, so the demo is also
// the reference (Rob, 0.22.2: "In the FX, you should put the codes for the
// FX on that page too"). On a wide terminal it follows the effect; at 40
// columns the effect has already used most of the row, so the code goes on
// the line under it, lined up past the label, rather than wrapping.
void fxCode(Term& t, Timeline& tl, const char* code) {
    t.color(tl, Color::DarkGrey);
    if (t.cols() >= 60) {
        t.text(tl, "   ");
    } else {
        t.nl(tl);
        t.text(tl, "           ");                       // the label's 11 columns
    }
    t.text(tl, code);
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
        { "WHO", "W", 0, CF_NONE, "[W]HO [n]", "who is on the board",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdWho(s, a); },
          Menu::Main, 0 },
        { "PAGE", "", 0, CF_NONE, "PAGE <n> <msg>", "send a caller a message",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdPage(s, a); b.prompt(s); },
          Menu::Main, 3 },
        // The sysop page (1.1.0). O is PCBoard's letter for it. The command
        // may leave the session asking why, ringing, or at the sysop's
        // question, so the prompt is drawn only when it is still at the
        // shell with the line finished.
        { "OPERATOR", "O", 0, CF_NONE, "[O]PERATOR", "ring for the sysop",
          [](Bbs& b, Session& s, const char* a, uint32_t) {
              b.cmdOperator(s, a);
              if (s.st == SState::Shell) b.prompt(s);
          },
          Menu::Main, 4 },

        // G and BYE are one line, not two. A caller does not need both
        // explained, only that it logs them off; G asks first and BYE does
        // not, which is what the usage line says without spending a row on
        // each. BYE is still its own entry so HELPONLY keeps it dispatchable
        // and out of the menu.
        { "G", "", 0, CF_NONE, "[G] | BYE", "log off (G asks first)",
          [](Bbs&, Session& s, const char*, uint32_t) {
              s.st = SState::Confirm;
              s.term.color(s.tl, Color::Yellow);
              s.term.text(s.tl, kConfirmText);
              s.term.color(s.tl, Color::White);
          },
          Menu::Main, 9 },
        // NOT CF_HELPONLY. That flag means "never dispatched", and BYE is
        // both how a caller logs off and how staff elevate, so flagging it
        // turned "bye <password>" into "Unknown command" and took staff
        // access off the board with it. Menu::Hidden is what keeps it off
        // the menu; the flag decides whether it runs at all.
        { "BYE", "", 0, CF_NONE, "BYE", "log off now",
          [](Bbs& b, Session& s, const char* a, uint32_t n) { b.cmdBye(s, a, n); },
          Menu::Hidden, 10 },

        // -- the board describing itself. Useful, not what anybody came for.
        { "HELP", "H?", 0, CF_NONE, "?|[H]ELP [x]", "these menus",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdHelp(s, a); },
          Menu::Account, 90 },
        { "WHOIS", "", 0, CF_NONE, "WHOIS [handle]", "a caller's profile",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdInfo(s, a); b.prompt(s); },
          Menu::Account, 91 },
        { "TIME", "", 0, CF_NONE, "TIME", "the clock and your time left",
          [](Bbs& b, Session& s, const char* a, uint32_t n) { b.cmdTime(s, a, n); },
          Menu::Account, 92 },
        { "LAST", "", 0, CF_NONE, "LAST", "the most recent calls",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.startList(s, ListKind::Last); },
          Menu::Account, 93 },
        { "CLS", "C", 0, CF_NONE, "[C]LS", "clear the screen",
          [](Bbs& b, Session& s, const char*, uint32_t) { s.term.cls(s.tl); b.prompt(s); },
          Menu::Account, 94 },
        { "ABOUT", "", 0, CF_NONE, "ABOUT", "this BBS, its version and licence",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdAbout(s); },
          Menu::Account, 95 },

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
        { "BELL", "", 0, CF_NONE, "BELL", "bells from others on / off",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdBell(s); b.prompt(s); },
          Menu::Account, 2 },
        { "CODES", "", 0, CF_NONE, "CODES", "colour and effects in messages",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdCodes(s); },
          Menu::Account, 8 },
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
        // No shortcut: "F" belongs to FILES, which is on the main menu.
        // Both tables declared it, the core registers first, and
        // findCommand returns the first match, so FILES's documented
        // shortcut never worked and COMMANDS.md said it did. Nothing
        // detects a duplicate shortcut, so it stayed that way.
        { "FX", "", 0, CF_NONE, "FX", "effects demo",
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
        { "NODES", "", PERM_NODES, CF_NONE, "NODES [n]", "every session with its IP",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdNodes(s, a); },
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
        // CALLS is public (Rob). It is a bar chart of calls per hour and
        // nothing else: no handles, no addresses, nobody's session. Knowing
        // when a board is busy is what tells a caller when to turn up, which
        // is the whole point of a small board. The same figures are already
        // published on the directory's own website for any board that opts
        // in to sharing activity, so there is no exposure here that a caller
        // could not already get from a browser.
        { "CALLS", "", 0, CF_NONE, "CALLS", "when the board is busy",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdCalls(s); },
          Menu::Account, 6 },
        { "SHUTDOWN", "", 0, CF_SYSOP, "SHUTDOWN [n]", "warn everyone, then close the board",
          [](Bbs& b, Session& s, const char* a, uint32_t n) { b.cmdShutdown(s, a, n); },
          Menu::Sysop, 40 },
        // The zip the backup window gives, onto the SD card and back (1.1.0).
        // Sysop only, like CONFIG: a restore replaces the settings and the
        // accounts, and a backup holds the Wi-Fi password.
        { "BACKUP", "", 0, CF_SYSOP, "BACKUP SD", "zip to card (or SCREENS)",
          [](Bbs& b, Session& s, const char* a, uint32_t n) { b.cmdBackup(s, a, n); },
          Menu::Sysop, 41 },
        { "RESTORE", "", 0, CF_SYSOP, "RESTORE SD n", "put a card zip back",
          [](Bbs& b, Session& s, const char* a, uint32_t n) { b.cmdRestore(s, a, n); },
          Menu::Sysop, 42 },
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

// rowWidth: a row is as wide as the terminal, not as wide as a C64
//
// This used to clamp at 40 for everybody, with the comment "lists and menus
// are laid out for 40 columns everywhere", and that one line is where most
// of the board's wasted screen came from: an 80 column caller got 40 columns
// of content and 40 of black, and HELP wrapped descriptions that had room to
// spare. It was always per-caller, since cols comes from the session; only
// the clamp made it narrow.
//
// Most of the board follows this for free, because it already derived from
// rowWidth: the HELP description column, the CALLS histogram, the refresh
// frame truncation, and every bar, rule and padded row. What does not follow
// are the few screens that build fixed columns of their own, which stay at
// their old width under a wider bar until each is given the same treatment.
//
// A zero here underflows to 255 and a title bar pads 255 reverse-video
// spaces, which is the one mechanism that would genuinely paint a bar past
// the right edge and onto the rows below. Unreachable today, since
// setGeometry guards zero and detection only ever yields 40 or 80, but a
// NAWS negotiation carrying zero does reach Telnet, and the guard is free.
//
// Capped at 132: a terminal can report a great deal more, and a 200 column
// reverse-video bar is not a design, it is a stripe.
uint8_t Bbs::rowWidth(const Session& s) const {
    uint8_t cols = s.term.cols();
    // An unknown width gets the narrow case, not the optimistic one. A
    // terminal that never said how wide it is has not promised 80, and a
    // row that wraps is worse on every terminal than a row that is short on
    // some. Plain ASCII callers do not land here: detection gives them 80x24
    // (detect.cpp), so their rows are 79 wide like ANSI's. The 39 columns in
    // SCREENS.md are for screen files, which a C64 has to be able to play.
    if (!cols) cols = 40;
    if (cols > 132) cols = 132;
    return static_cast<uint8_t>(cols - 1);
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
        // Erase the rest of the row rather than writing spaces across it.
        // See rowEnd: this padding was the bulk of a refresh frame. It is a
        // bandwidth saving and not the fix for the stall callers reported,
        // which was Wi-Fi power save.
        if (!t.eolClear(s.tl))
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
    // A refresh screen redraws from home, so every row has to cover the row
    // underneath it. That used to mean writing spaces out to the full width:
    // at 132 columns a DASH frame was 4485 bytes of which 2704 were padding.
    // Four bytes do the same job on any ANSI terminal, and the frame is now
    // 2096. PETSCII has no erase to end of line, so it keeps the spaces, and
    // its 40 column frames were never large anyway.
    //
    // **This is a bandwidth saving, not a bug fix, and the distinction was
    // paid for.** The first version of this comment said a frame larger than
    // BBS_TL_BYTES was dropped whole and that this was what stalled callers.
    // That is not what the code does: serviceWatch draws row by row while
    // the timeline has 512 bytes free and only begins a new frame once the
    // old one has drained, so a frame is built across as many passes as it
    // needs. Measured on the board, a 2957 byte WHO frame at 132 columns
    // arrived intact every time for eighty seconds, and keypress latency
    // during it was a flat 31 ms, so BBS_RX_ROOM was never starving input
    // either. The stall callers actually saw was Wi-Fi power save; see the
    // IP_EVENT_STA_GOT_IP handler in main.cpp.
    //
    // Worth keeping anyway: less to send is less to send, and it shortens
    // the quiet gap a refresh screen leaves between frames.
    if (s.watch != ListKind::None) {
        if (!s.term.eolClear(s.tl))
            for (uint8_t i = col; i < rowWidth(s); ++i) s.term.ch(s.tl, ' ');
    }
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
// rowBar: rowTitle with the colour chosen by the caller.
//
// Every title on the board is Cyan and that stays the default, but a
// subsystem drawing a caller-supplied string wants to say which colour it
// is, and the forums want a different one for the row a caller is standing
// on. Splitting it costs nothing: rowTitle is this with Cyan.
//
// **The title is truncated to fit, and that is the fix rather than the
// refactor.** Every caller until now passed a short literal, so `used`
// never exceeded `w`, the padding loop simply did nothing, and nobody
// noticed there was no clamp. A forum subject is typed by a caller and can
// be 49 characters against a 39 column bar: the row would run long, wrap,
// and leave the reverse attribute hanging down the next line. It has to be
// fixed here and not in each plugin, because the plugin cannot know what
// the right margin is doing.
void Bbs::rowBar(Session& s, Color c, const char* title, const char* right) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    uint8_t w = rowWidth(s);
    size_t rlen = right ? visibleLen(right) : 0;

    // What the title may take: the whole row, less the leading space, less
    // the right text and the space before it. Never less than one, because
    // a right text wider than the row would otherwise underflow this.
    size_t reserve = 1 + (rlen ? rlen + 1 : 0);
    uint8_t avail = (reserve < w) ? static_cast<uint8_t>(w - reserve) : 1;

    if (t.isAnsi() || t.isPet()) {
        t.color(tl, c);
        t.reverse(tl, true);
        t.color(tl, c);
        t.ch(tl, ' ');
        uint8_t drew = t.textCols(tl, title, avail);
        size_t used = 1 + drew + (rlen ? rlen + 1 : 0);
        for (size_t i = used; i < w; ++i) t.ch(tl, ' ');
        if (rlen) { t.text(tl, right); t.ch(tl, ' '); }
        t.reverse(tl, false);
    } else {
        uint8_t drew = t.textCols(tl, title, avail);
        t.ch(tl, ' ');
        size_t used = drew + 1 + (rlen ? rlen + 1 : 0);
        for (size_t i = used; i < w; ++i) t.glyph(tl, Glyph::HLine);
        if (rlen) { t.ch(tl, ' '); t.text(tl, right); }
    }
    t.nl(tl);
}

void Bbs::rowTitle(Session& s, const char* title, const char* right) {
    rowBar(s, Color::Cyan, title, right);
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
    // Not a menu. This used to return Menu::Main, so "HELP nonsense" printed
    // the main menu and never said it had not understood: identical output
    // to a bare "?", which reads as the board having decided your word meant
    // "main".
    return Menu::None;
}

} // namespace

// ---------------------------------------------------------------------------
// cmdHelp: "?" alone is the main menu; "? staff", "? chat", "? account",
// "? sysop" and "? all" pick the others.
// ---------------------------------------------------------------------------
void Bbs::cmdHelp(Session& s, const char* arg) {
    Menu m = menuFromText(arg);
    if (m == Menu::None) {
        // Not a menu, so a command, in full: HELP WHO, HELP W. A menu name
        // wins where the two collide (HELP CHAT is the chat menu, as it has
        // always been). Asked of findCommand, so a command this caller may
        // not use is as unknown here as it is at the prompt: HELP KICK does
        // not describe staff tools to somebody who cannot use them.
        char verb[16];
        size_t n = 0;
        while (arg[n] && arg[n] != ' ' && n < sizeof(verb) - 1) { verb[n] = arg[n]; ++n; }
        verb[n] = '\0';
        const Command* c = findCommand(verb, s);
        // A hidden alias with nothing written for it (OFF, QUIT, BULLETIN)
        // printed an empty box. It answers as unknown, the way the menus
        // treat it as not there.
        if (c && (c->flags & CF_HIDDEN || c->menu == Menu::Hidden) && !helptext::find(c->verb))
            c = nullptr;
        if (c) {
            longHelp(s, *c);
            prompt(s);
            return;
        }
        char buf[72];
        snprintf(buf, sizeof(buf), "No command or menu called %.20s. ? lists them.", arg);
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, buf);
        prompt(s);
        return;
    }

    s.helpAll  = m == Menu::Hidden;
    s.helpMenu = s.helpAll ? Menu::Main : m;

    // A menu with nothing on it for this caller drew a title bar, a rule and
    // no rows, which reads as a broken screen rather than as "not for you".
    // An ordinary caller reaches one by typing "? staff", because the
    // grammar line correctly offers them only the menus they have, while
    // menuFromText still accepts every name.
    if (!s.helpAll && helpEmpty(s)) {
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, "Nothing on that menu for you.");
        prompt(s);
        return;
    }
    startList(s, ListKind::Help);
}

// ---------------------------------------------------------------------------
// longHelp: one command in full. The usage line stands out and the rest is
// quieter, the same two voices the menus use. Printed as plain text: the
// entry for BELL mentions @BELL@, and running that through the @-code
// renderer would ring the reader's bell while telling them how to.
// ---------------------------------------------------------------------------
void Bbs::longHelp(Session& s, const Command& c) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    rowTitle(s, c.verb);
    const char* text = helptext::find(c.verb);
    if (!text) {                                   // a plugin nobody has written up
        t.color(tl, Color::LightBlue);
        t.text(tl, c.usage);
        t.nl(tl);
        t.color(tl, Color::Grey);
        t.text(tl, c.help);
        t.nl(tl);
        rowRule(s);
        return;
    }
    bool first = true;
    for (const char* p = text; *p;) {
        const char* e = strchr(p, '\n');
        size_t len = e ? static_cast<size_t>(e - p) : strlen(p);
        t.color(tl, first ? Color::LightBlue : Color::Grey);
        t.textN(tl, p, len);
        t.nl(tl);
        first = false;
        p += len;
        if (*p == '\n') ++p;
    }
    rowRule(s);
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
        // A grammar, not a sentence. Rob asked for something that reads like
        // a command reference, and it has to be one row: HELP paged every
        // single time, which trains people to hammer a key through it.
        char line[80];
        // Built from menuName so the grammar always lists exactly the words
        // menuFromText accepts. Hardcoding them here would be one rename
        // away from telling a caller to type something the parser refuses.
        // Upper case because that is how a command reference writes the
        // alternatives; the names themselves stay lower case elsewhere.
        auto upper = [](const char* w, char* out, size_t n) {
            size_t i = 0;
            for (; w[i] && i + 1 < n; ++i)
                out[i] = (w[i] >= 'a' && w[i] <= 'z') ? w[i] - 32 : w[i];
            out[i] = 0;
        };
        char a[12], b2[12], c[12], d[12];
        upper(menuName(Menu::Chat), a, sizeof(a));
        upper(menuName(Menu::Account), b2, sizeof(b2));
        if (s.perms) {
            upper(menuName(Menu::Staff), c, sizeof(c));
            upper(menuName(Menu::Sysop), d, sizeof(d));
            snprintf(line, sizeof(line), "?|H|HELP [%s|%s|%s|%s|ALL]", a, b2, c, d);
        } else {
            snprintf(line, sizeof(line), "?|H|HELP [%s|%s|ALL]", a, b2);
        }
        rowText(s, Color::LightGreen, line);
        return true;
    }
    // The main menu only, and one row: ? still fits one screen.
    if (s.listIdx == static_cast<uint8_t>(total + 3) && !s.helpAll &&
        s.helpMenu == Menu::Main) {
        ++s.listIdx;
        rowText(s, Color::DarkGrey, "HELP <command> explains one in full");
        return true;
    }
    return false;
}

// ===========================================================================
// WHO
// ===========================================================================

// cmdNodes: NODES alone lists once, NODES n redraws every n seconds. Same
// bounds as WHO, because they are the same question asked of the same clock
// and two different limits would only be two things to remember.
// ---------------------------------------------------------------------------
// cmdShutdown: tell everybody, count down, then take the board off the air.
//
// A macro over things that already exist: the bus carries the warnings, the
// tick drives the clock, and goodbye() takes each caller out with the
// send-off screen and the linger they would get from BYE. What it adds is
// that nobody is cut off without warning, and that the board says what
// happened to anyone who calls afterwards.
//
// Deliberately no confirmation prompt. The countdown IS the confirmation:
// there is a cancel, it is announced, and the shortest useful warning is
// longer than the time it takes to realise you typed the wrong thing.
// ---------------------------------------------------------------------------
void Bbs::cmdShutdown(Session& s, const char* arg, uint32_t now) {
    char buf[80];

    if (ieq(arg, "cancel") || ieq(arg, "off")) {
        if (!shutEnds_) {
            sayTo(s, Color::Grey, "No shutdown is running.");
            prompt(s);
            return;
        }
        shutEnds_ = 0;
        shutSaid_ = 0xFFFFFFFFu;
        plat::log("bbs: SHUTDOWN cancelled by %s", s.user);
        for (Session* o : all_) {
            if (o->st == SState::Free || o->role == Role::Busy) continue;
            post(*o, BusKind::Broadcast, nullptr, "*** The shutdown is cancelled. Carry on.");
        }
        sayTo(s, Color::LightGreen, "Shutdown cancelled.");
        prompt(s);
        return;
    }

    if (shutEnds_) {
        sayTo(s, Color::Yellow, "Already going down. SHUTDOWN CANCEL stops it.");
        prompt(s);
        return;
    }

    long secs = *arg ? strtol(arg, nullptr, 10) : 60;
    if (secs < 5 || secs > 3600) {
        sayTo(s, Color::LightRed, "SHUTDOWN n: n is 5 to 3600 seconds. SHUTDOWN CANCEL stops one.");
        prompt(s);
        return;
    }

    shutEnds_ = now + static_cast<uint32_t>(secs) * 1000u;
    if (!shutEnds_) shutEnds_ = 1;             // never the "no shutdown" value
    // Every threshold at or above where the countdown starts counts as said:
    // the line below says how long there is. It was 0xFFFFFFFF, and the first
    // passes of serviceShutdown then announced each threshold the countdown
    // had never reached, one a pass: SHUTDOWN 20 told everybody the board
    // went down in 120 seconds, then 60, then 30 (found in 1.1.0, when these
    // warnings started reaching callers outside the prompt too).
    shutSaid_ = static_cast<uint32_t>(secs);

    snprintf(buf, sizeof(buf), "*** %s is taking the board down in %ld seconds.",
             s.user, secs);
    for (Session* o : all_) {
        if (o == &s || o->st == SState::Free || o->role == Role::Busy) continue;
        post(*o, BusKind::Broadcast, nullptr, buf);
    }

    plat::log("bbs: SHUTDOWN in %ld s, by %s", secs, s.user);
    snprintf(buf, sizeof(buf), "Going down in %ld seconds. SHUTDOWN CANCEL stops it.", secs);
    sayTo(s, Color::LightRed, buf);
    // Said rather than enforced. Knowing which caller is mid-transfer needs
    // a hook into the plugin that owns the engine, and a sysop can see it in
    // NODES; being warned is most of the value and none of the coupling.
    sayTo(s, Color::Grey, "Any transfer in progress will be lost.");
    prompt(s);
}

void Bbs::cmdNodes(Session& s, const char* arg) {
    if (!*arg) { startList(s, ListKind::Nodes); return; }
    const SysConfig& cfg = syscfg::get();
    uint8_t secs = 0;
    if (!parseSeconds(arg, cfg.whoMin, cfg.whoMax, secs)) {
        char buf[48];
        snprintf(buf, sizeof(buf), "NODES n: n is %u to %u seconds.", cfg.whoMin, cfg.whoMax);
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, buf);
        prompt(s);
        return;
    }
    startWatch(s, ListKind::Nodes, secs);
}

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

// ===========================================================================
// One node row, for DASH, NODES and WHO (1.1.0)
// ===========================================================================

// nodeAt: see bbs.h
const Session* Bbs::nodeAt(uint8_t k) const {
    if (k < BBS_MAX_NODES)      return &nodes_[k];
    if (k == BBS_MAX_NODES)     return &sysop_;
    if (k == BBS_MAX_NODES + 1) return &busy_;
    return nullptr;
}

// ---------------------------------------------------------------------------
// nodeHead: the heading for a plan, from the same widths nodeCells uses, so a
// column and its heading cannot come apart the way WHO's did at 80 columns
// (the header widened with the terminal and the rows stayed at 40). Leaves
// the row open: the 132 column dashboard puts more on the same line.
// ---------------------------------------------------------------------------
uint8_t Bbs::nodeHead(Session& v, NodePlan plan) {
    char buf[96];
    const char* what = can(v, PERM_NODES) ? "Doing" : "Terminal";
    switch (plan) {
        case NodePlan::Wide:
            snprintf(buf, sizeof(buf), "%s%c%-20.20s %-10.10s %5s %4s %-15.15s %-10.10s",
                     " N", ' ', "Handle", "Doing", "Idle", "Left", "Address", "Terminal");
            break;
        case NodePlan::Wide132:
            snprintf(buf, sizeof(buf), "%s%c%-20.20s %-10.10s %5s %4s %4s %-15.15s %-10.10s",
                     " N", ' ', "Handle", "Doing", "Idle", "Left", "On", "Address", "Terminal");
            break;
        case NodePlan::Narrow:
            snprintf(buf, sizeof(buf), "%s%c%-12.12s %-9.9s %5s %4s",
                     " N", ' ', "Handle", "Doing", "Idle", "Left");
            break;
        case NodePlan::From:
            snprintf(buf, sizeof(buf), "%s%c%-12.12s %-15.15s %-5.5s",
                     " N", ' ', "Handle", "Address", "Term");
            break;
        case NodePlan::Who:
            snprintf(buf, sizeof(buf), "%s%c%-20.20s %-12.12s %3s %5s",
                     " N", ' ', "Handle", what, "Min", "Idle");
            break;
        case NodePlan::WhoNarrow:
        default:
            snprintf(buf, sizeof(buf), "%s%c%-12.12s %-10.10s %3s %5s",
                     " N", ' ', "Handle", what, "Min", "Idle");
            break;
    }
    uint8_t col = 0;
    rowSeg(v, Color::LightBlue, buf, col);
    return col;
}

// ---------------------------------------------------------------------------
// nodeCells: one session's row in a plan, for viewer v, left open like
// nodeHead. The colours are the same thing in the same colour on all three
// screens: node LightBlue, rank marker by markColor, handle LightGreen (White
// for your own line, DarkGrey when hidden), doing Cyan, idle Grey going
// DarkGrey past five minutes, minutes left Grey going LightRed under five,
// address Grey, terminal DarkGrey, a free line DarkGrey.
//
// WHO keeps its own rules, which are not the staff screens': a caller sees
// terminals rather than what people are doing, and a hidden staff line looks
// free to anybody who is not staff.
// ---------------------------------------------------------------------------
uint8_t Bbs::nodeCells(Session& v, const Session& o, NodePlan plan) {
    uint32_t now = plat::millis();
    uint8_t col = 0;
    char buf[40];
    NodeStr ns = nodeLabel(o);
    bool who    = plan == NodePlan::Who || plan == NodePlan::WhoNarrow;
    bool seeAll = v.perms != 0;                              // staff see hidden lines
    bool hidden = &o != &v && (!o.visible || o.lurk);

    if (o.st == SState::Free || (who && hidden && !seeAll)) {
        rowSeg(v, Color::DarkGrey, ns.t, col);
        rowSeg(v, Color::DarkGrey, who ? " -- waiting for caller --" : " -", col);
        return col;
    }

    // cell: a column of fixed width with the space that follows it; the last
    // column of a row leaves the space off.
    auto cell = [&](Color c, const char* text, int w, bool last = false) {
        snprintf(buf, sizeof(buf), last ? "%-*.*s" : "%-*.*s ", w, w, text);
        rowSeg(v, c, buf, col);
    };
    // rcell: a figure, right aligned. Padded but never cut: a number too wide
    // for its column pushes the row out by a character, where a cut one
    // would be a different number (SYS's "%9.9s" lesson).
    auto rcell = [&](Color c, const char* text, int w, bool last = false) {
        snprintf(buf, sizeof(buf), last ? "%*s" : "%*s ", w, text);
        rowSeg(v, c, buf, col);
    };

    const char* handle = o.user[0] ? o.user : preLoginName(o);
    const char* doing  = hidden ? (o.lurk ? "lurking" : "hidden") : doingText(o);
    Color handleC = &o == &v ? Color::White : (hidden ? Color::DarkGrey : Color::LightGreen);
    Color doingC  = hidden ? Color::DarkGrey : Color::Cyan;

    uint32_t idleMs = now - o.lastInput;
    char idle[8];
    fmtIdle(idle, sizeof(idle), idleMs);
    Color idleC = idleMs > 300000u ? Color::DarkGrey : Color::Grey;

    char left[12] = "--";
    Color leftC = Color::Grey;
    if (o.role == Role::Caller && o.loggedIn && !unlimited(o)) {
        int32_t sec = secondsLeft(o, now);
        if (sec != INT32_MAX) {
            snprintf(left, sizeof(left), "%ld", static_cast<long>(sec > 0 ? (sec + 59) / 60 : 0));
            if (sec < 300) leftC = Color::LightRed;
        }
    }
    char on[12];
    snprintf(on, sizeof(on), "%u", static_cast<unsigned>((now - o.connectedAt) / 60000u));

    char mark[2] = { markFor(o), '\0' };
    rowSeg(v, Color::LightBlue, ns.t, col);
    rowSeg(v, markColor(mark[0]), mark, col);

    switch (plan) {
        case NodePlan::Wide:
        case NodePlan::Wide132:
            cell(handleC, handle, 20);
            cell(doingC, doing, 10);
            rcell(idleC, idle, 5);
            rcell(leftC, left, 4);
            if (plan == NodePlan::Wide132) rcell(Color::Grey, on, 4);
            cell(Color::Grey, o.ip, 15);
            cell(Color::DarkGrey, o.term.name(), 10, true);
            break;
        case NodePlan::Narrow:
            cell(handleC, handle, 12);
            cell(doingC, doing, 9);
            rcell(idleC, idle, 5);
            rcell(leftC, left, 4, true);
            break;
        case NodePlan::From:
            cell(handleC, handle, 12);
            cell(Color::Grey, o.ip, 15);
            cell(Color::DarkGrey, o.term.shortName(), 5, true);
            break;
        case NodePlan::Who:
        case NodePlan::WhoNarrow:
        default: {
            bool staff = can(v, PERM_NODES);
            const char* what = staff ? doing : o.term.name();
            bool wide = plan == NodePlan::Who;
            cell(handleC, handle, wide ? 20 : 12);
            cell(hidden ? Color::DarkGrey : (staff ? Color::Cyan : Color::Grey), what, wide ? 12 : 10);
            rcell(Color::Grey, on, 3);
            rcell(idleC, idle, 5, true);
            break;
        }
    }
    return col;
}

// ---------------------------------------------------------------------------
// rowClose: see bbs.h. A highlighted row was drawn with reverse video on; it
// is padded out in reverse so the bar is the width of the row, then turned
// off before the newline, which a C64 would otherwise do for it and an ANSI
// terminal would not.
// ---------------------------------------------------------------------------
void Bbs::rowClose(Session& s, uint8_t col, bool highlighted) {
    if (highlighted) {
        for (uint8_t w = rowWidth(s); col < w; ++col) s.term.ch(s.tl, ' ');
        s.term.reverse(s.tl, false);
    }
    rowEnd(s, col);
}

// ---------------------------------------------------------------------------
// rowWho: title bar with the clock, header, the caller nodes, the sysop if
// shown, a closing rule and the marker key. The busy line is never listed. A
// refresh screen keeps a fixed height. Staff with NODES see what each caller
// last ran (Doing) instead of the terminal type.
//
// The handle column follows the terminal: 20 at 60 columns and up, where the
// whole of BBS_USER_MAX fits and PAGE wants the whole handle, 12 below.
// ---------------------------------------------------------------------------
bool Bbs::rowWho(Session& s) {
    bool refresh = s.watch != ListKind::None;
    bool seeAll  = s.perms != 0;                     // staff also see hidden and lurking
    NodePlan plan = rowWidth(s) >= 60 ? NodePlan::Who : NodePlan::WhoNarrow;

    for (;;) {
        uint8_t i = s.listIdx++;
        if (i == 0) {
            char when[24] = "";
            if (clk::valid()) clk::fmt(when, sizeof(when), "%a %d %b %H:%M:%S");
            rowTitle(s, "Who's online", when[0] ? when : nullptr);
            return true;
        }
        if (i == 1) {
            rowClose(s, nodeHead(s, plan));
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
        rowClose(s, nodeCells(s, *n, plan));
        return true;
    }
}

// ---------------------------------------------------------------------------
// rowWatchFooter: two lines under a refresh screen, the last without a
// newline so a 24-row terminal never scrolls. listSub counts 1, 2, done.
// ---------------------------------------------------------------------------
bool Bbs::rowWatchFooter(Session& s) {
    char buf[96];
    if (s.listSub == 1) {
        if (s.watch == ListKind::Dash) dashFooter(s, buf, sizeof(buf));
        else snprintf(buf, sizeof(buf), "Refresh %us, any key stops", s.watchSecs);
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
        // Never wrap a refresh frame: a row wider than the terminal scrolls
        // it, and a refresh screen that scrolls corrupts itself on every
        // redraw. Clamped to the buffer as well as to the width, because
        // rowWidth is the terminal's now rather than a fixed 40, and an 80
        // column caller was writing the terminator past the end of a 64
        // byte buffer. That is a stack smash, and it crashed the board.
        uint8_t cut = rowWidth(s);
        if (cut > sizeof(buf) - 1) cut = sizeof(buf) - 1;
        buf[cut] = '\0';
        rowText(s, left >= 0 && left < 60 ? Color::LightRed : Color::DarkGrey, buf, false);
        s.listSub = 3;
        return true;
    }
    return false;
}

// ===========================================================================
// DASH (1.1.0, internal/tty-ux-dash-2026-09-24.md)
//
// Rebuilt the way NODES was already built: a row per session in node order,
// widths from rowWidth, the address beside each caller. One page at 80
// columns plus a second for the plugins, the bans and the calls; three at 40,
// where the where-from columns get a page of their own; one at 132.
//
// Every page at a given width is a fixed height, blank rows included, and no
// taller than the terminal, so a refresh that homes and redraws never
// scrolls. The old frame was 24 fixed rows plus one per plugin, and so
// scrolled on every 80x24 terminal a stock board had.
//
// A frame costs formatting and nothing else: snapFill at its first row takes
// every figure that is not on a Session, and nothing after that opens a
// file, walks the heap or a filesystem, or calls into the Wi-Fi task.
// ===========================================================================

namespace {

// DashRow: what a page function did with the row it was asked for. Skip is
// a row that is only there to keep a refreshing frame's height, left out of
// a list that is read once (DASH, DASH ALL).
enum DashRow : uint8_t { DR_DONE, DR_DREW, DR_SKIP };

// Shared with SYS, further down.
void fmtUptime(char* out, size_t n, uint32_t ms);
const char* signalWord(int8_t rssi, Color& c);

// Widths at which the dashboard changes layout. The 80 column page needs its
// 74 column vitals rows, the 132 column one its right-hand block to column
// 130; anything narrower than 74 gets the 40 column pages.
constexpr uint8_t kDash80  = 74;
constexpr uint8_t kDash132 = 131;
constexpr uint8_t kDashAll = 0x80;          // Session::dashPage: DASH ALL

// callNode: a call's node as a list shows it, right aligned in two
NodeStr callNode(const CallRec& r) {
    NodeStr n = nodeNum(r.node);
    if (r.flags & CallRec::F_SYSOP) { n.t[0] = 'S'; n.t[1] = '\0'; }
    if (n.t[1] == '\0') { n.t[1] = n.t[0]; n.t[0] = ' '; }
    return n;
}

// statusOf: a running plugin's dashboard line, or nullptr
const char* statusOf(const char* name) {
    uint8_t k = plugins::indexOf(name);
    if (k == 0xFF || !plugins::running(k)) return nullptr;
    const Plugin* p = plugins::at(k);
    if (!p || !p->status) return nullptr;
    const char* line = p->status();
    return line && *line ? line : nullptr;
}

// splitStatus: "Directory: online  12 sent" as "online" and "12 sent", for
// the 132 column block, where a status line is a label, a value and a note.
void splitStatus(const char* line, char* word, size_t wn, const char** rest) {
    const char* p = strchr(line, ':');
    p = p ? p + 1 : line;
    while (*p == ' ') ++p;
    size_t n = 0;
    while (p[n] && p[n] != ' ' && p[n] != ',' && n + 1 < wn) { word[n] = p[n]; ++n; }
    word[n] = '\0';
    p += n;
    while (*p == ',' || *p == ' ') ++p;
    *rest = p;
}

} // namespace

bool Bbs::dashWide132(const Session& s) const { return rowWidth(s) >= kDash132; }

bool Bbs::dashCells(const Session& s) const { return s.term.isAnsi() || s.term.isPet(); }

uint8_t Bbs::dashPages(const Session& s) const {
    if (rowWidth(s) >= kDash132) return 1;
    return rowWidth(s) >= kDash80 ? 2 : 3;
}

void Bbs::cmdDash(Session& s, const char* arg) {
    // DASH: page 1, once, as a list that fits one screen. DASH ALL: every
    // page in turn through the ordinary pager, which is also how a plain
    // terminal sees all of it at once. DASH n keeps meaning seconds, because
    // WHO n and NODES n do.
    if (!*arg || ieq(arg, "ALL")) {
        startList(s, ListKind::Dash);
        s.dashPage  = *arg ? kDashAll : 0;
        s.dashSel   = 0xFF;
        s.watchSecs = 0;                     // not refreshing: the last DASH n's is not ours
        s.nonstop   = !*arg;
        return;
    }
    const SysConfig& cfg = syscfg::get();
    uint8_t secs = 0;
    if (!parseSeconds(arg, cfg.whoMin, cfg.whoMax, secs)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "DASH n: n is %u to %u seconds. DASH ALL: every page.",
                 cfg.whoMin, cfg.whoMax);
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, buf);
        prompt(s);
        return;
    }
    startWatch(s, ListKind::Dash, secs);
}

// ---------------------------------------------------------------------------
// snapFill: see bbs.h. full is SYS, which also wants the biggest free block
// and so walks the heap, once per listing; the dashboard never does.
// ---------------------------------------------------------------------------
void Bbs::snapFill(bool full) {
    DashSnap& d = snap_;
    uint32_t now = plat::millis();
    d.net   = plat::netInfo();                               // the one Wi-Fi call
    d.power = plat::powerSave();
    if (!d.power) d.power = "";
    if (full) {
        plat::HeapStats h = plat::heap();
        d.heapFull  = h.valid;
        d.heapFree  = h.valid ? h.freeBytes : 0;
        d.heapLow   = h.valid ? h.minFree : 0;
        d.heapBig   = h.largestBlock;
        d.heapTotal = h.totalBytes;
    } else {
        // Counters, not a walk: heapFree reads the allocator's own total,
        // and heapLow_ is the floor heapWatch has kept since boot.
        d.heapFree = plat::heapFree();
        d.heapLow  = heapLow_ != 0xFFFFFFFFu && heapLow_ < d.heapFree ? heapLow_ : d.heapFree;
    }
    d.stackLeast = stackLow_;
    d.dataFree   = plugins::freeBytes();                     // kept a minute on the board
    d.card       = sdDashCard(d.cardFreeKB, d.cardTotalKB);  // kept a minute by the sd plugin
    d.bans       = 0;
    BanList::Entry e;
    for (uint8_t k = 0; k < BBS_BAN_SLOTS; ++k) if (bans_.at(k, now, e)) ++d.bans;
    d.today      = calllog::today();                         // kept by the caller log
    d.dir[0]     = '\0';
    if (const char* line = statusOf("announce")) {
        const char* rest = nullptr;
        splitStatus(line, d.dir, sizeof(d.dir), &rest);
        if (!d.dir[0]) snprintf(d.dir, sizeof(d.dir), "on");
    }
}

// dashTitle: the bar, with how many lines are in use and the clock
void Bbs::dashTitle(Session& s) {
    char when[32] = "";
    char right[48];
    bool wide = rowWidth(s) >= 59;
    if (clk::valid()) clk::fmt(when, sizeof(when), wide ? "%a %d %b %H:%M:%S" : "%H:%M:%S");
    snprintf(right, sizeof(right), wide ? "%u of %u on%s%s" : "%u of %u%s%s",
             static_cast<unsigned>(activeNodes()), static_cast<unsigned>(BBS_MAX_NODES),
             when[0] ? "  " : "", when);
    rowTitle(s, "DASHBOARD", right);
}

// ---------------------------------------------------------------------------
// dashWaiting: what is waiting on the staff member looking. Ring notes for
// the sysop, then whatever each plugin's waiting hook says (uploads to
// approve, unread mail), then a backup upload waiting for its Y. A live ring
// never shows here: it stops the refresh and asks at the prompt.
// ---------------------------------------------------------------------------
void Bbs::dashWaiting(Session& s) {
    bool wide = rowWidth(s) >= 59;
    char buf[160];
    size_t n = 0;
    auto add = [&](const char* piece) {
        if (n >= sizeof(buf)) return;
        int w = snprintf(buf + n, sizeof(buf) - n, "%s%s", n ? "  " : "", piece);
        if (w > 0) n += static_cast<size_t>(w);
    };
    char piece[48];
    if (s.level == Access::Sysop && ringNotesWaiting_) {
        snprintf(piece, sizeof(piece), wide ? "%u ring note%s" : "%u note%s",
                 static_cast<unsigned>(ringNotesWaiting_), ringNotesWaiting_ == 1 ? "" : "s");
        add(piece);
    }
    for (uint8_t k = 0; k < plugins::count(); ++k) {
        const Plugin* p = plugins::at(k);
        if (!p || !p->waiting || !plugins::running(k)) continue;
        piece[0] = '\0';
        if (p->waiting(s, piece, sizeof(piece)) && piece[0]) add(piece);
    }
    if (backup_.awaitingApproval()) add("backup Y/N");

    uint8_t col = 0;
    Term& t = s.term;
    if (!n) {
        t.color(s.tl, Color::DarkGrey);
        col = t.textCols(s.tl, "Nothing waiting on you", rowWidth(s));
    } else {
        const char* head = wide ? "Waiting on you: " : "Waiting: ";
        t.color(s.tl, Color::Yellow);
        col = t.textCols(s.tl, head, rowWidth(s));
        col = static_cast<uint8_t>(col + t.textCols(s.tl, buf, static_cast<uint8_t>(rowWidth(s) - col)));
    }
    rowEnd(s, col);
}

// ---------------------------------------------------------------------------
// dashSeg: one "label figure" pair on a vitals row, gap spaces after what is
// already there. Labels Grey, figures White, a figure in LightRed when it is
// an alarm. A pair that would run past the row is left off rather than
// wrapped: a refreshing frame that wraps scrolls.
// ---------------------------------------------------------------------------
void Bbs::dashSeg(Session& s, uint8_t& col, uint8_t gap, const char* label, const char* value,
                  bool alarm) {
    size_t need = (col ? gap : 0) + (label[0] ? strlen(label) + 1 : 0) + strlen(value);
    if (col + need > rowWidth(s)) return;
    if (col) {
        char sp[4] = "   ";
        sp[gap < 3 ? gap : 3] = '\0';
        rowSeg(s, Color::Grey, sp, col);
    }
    if (label[0]) {
        rowSeg(s, Color::Grey, label, col);
        rowSeg(s, Color::Grey, " ", col);
    }
    rowSeg(s, alarm ? Color::LightRed : Color::White, value, col);
}

// ---------------------------------------------------------------------------
// dashVitals: the board's own figures, two rows at 80 columns and four at 40,
// every one of them from the snapshot or a counter the loop keeps.
// ---------------------------------------------------------------------------
void Bbs::dashVitals(Session& s, uint8_t which) {
    const DashSnap& d = snap_;
    uint32_t now = plat::millis();
    bool wide = rowWidth(s) >= kDash80;
    uint8_t col = 0;
    char v[32];

    // Each figure, formatted when a row asks for it.
    auto up = [&]() { char u[24]; fmtUptime(u, sizeof(u), now); dashSeg(s, col, 2, "Up", u, false); };
    auto heap = [&]() {
        if (!d.heapFree) { dashSeg(s, col, 2, "Heap", "-", false); return; }
        snprintf(v, sizeof(v), "%uK", static_cast<unsigned>(d.heapFree / 1024u));
        dashSeg(s, col, 2, "Heap", v, d.heapFree < BBS_HEAP_RESERVE);
        snprintf(v, sizeof(v), "%uK", static_cast<unsigned>(d.heapLow / 1024u));
        dashSeg(s, col, 1, "low", v, d.heapLow < BBS_HEAP_RESERVE);
    };
    auto stack = [&]() {
        if (d.stackLeast) fmtCommas(d.stackLeast, v, sizeof(v));
        else              snprintf(v, sizeof(v), "-");
        dashSeg(s, col, 2, "Stack", v, d.stackLeast && d.stackLeast < 1024u);
    };
    auto loop = [&]() {
        snprintf(v, sizeof(v), "%luus", static_cast<unsigned long>(loopAvgUs_));
        dashSeg(s, col, 2, "Loop", v, false);
        snprintf(v, sizeof(v), "%lums", static_cast<unsigned long>((loopMaxUs_ + 500u) / 1000u));
        dashSeg(s, col, 1, "worst", v, false);
    };
    auto slow = [&]() {
        // Red while a slow pass is recent: within the refresh the viewer
        // chose, so the frame after one says so, and the one after that
        // does not unless there was another.
        uint32_t window = (s.watchSecs ? s.watchSecs : 5u) * 1000u + 1000u;
        snprintf(v, sizeof(v), "%lu", static_cast<unsigned long>(slowCount_));
        dashSeg(s, col, 2, "Slow", v, slowAt_ && now - slowAt_ < window);
    };
    // The radio's mode, known only when the board could read it: "" on the
    // host and "?" when the driver would not say are not a radio asleep.
    bool known = d.power[0] && d.power[0] != '?';
    bool awake = !known || d.power[0] == 'n';                 // "none"
    auto wifi = [&](bool full) {
        if (d.net.valid && d.net.rssi) snprintf(v, sizeof(v), "%d dBm", static_cast<int>(d.net.rssi));
        else                           snprintf(v, sizeof(v), "-");
        dashSeg(s, col, 2, "WiFi", v, !awake);
        if (!full) return;
        // The channel is the first thing given up for room: a radio asleep
        // says SLEEPING, which is longer and matters more.
        if (d.net.channel && d.net.rssi && awake) {           // no signal, no channel worth saying
            snprintf(v, sizeof(v), "%u", static_cast<unsigned>(d.net.channel));
            dashSeg(s, col, 1, "ch", v, false);
        }
        if (known) dashSeg(s, col, 1, "", awake ? "awake" : "SLEEPING", !awake);
    };
    auto data = [&]() {
        snprintf(v, sizeof(v), "%uK", static_cast<unsigned>(d.dataFree / 1024u));
        dashSeg(s, col, 2, "Data", v, false);
    };
    auto card = [&]() {
        if (!d.card)                          snprintf(v, sizeof(v), "none");
        else if (d.cardFreeKB >= 1048576u)    snprintf(v, sizeof(v), "%u GB",
                                                       static_cast<unsigned>((d.cardFreeKB + 524288u) / 1048576u));
        else                                  snprintf(v, sizeof(v), "%u MB",
                                                       static_cast<unsigned>(d.cardFreeKB / 1024u));
        dashSeg(s, col, 2, "Card", v, false);
    };
    auto dir = [&]() {
        dashSeg(s, col, 2, "Dir", d.dir[0] ? d.dir : "off", !strcmp(d.dir, "held"));
    };
    auto backup = [&]() {
        if (backup_.awaitingApproval()) {
            snprintf(v, sizeof(v), "Y/N");
        } else if (backup_.isOpen()) {
            int32_t left = static_cast<int32_t>(backup_.closesAt() - now) / 1000;
            if (left < 0) left = 0;
            snprintf(v, sizeof(v), "open %ld:%02ld", static_cast<long>(left / 60),
                     static_cast<long>(left % 60));
        } else {
            snprintf(v, sizeof(v), "closed");
        }
        dashSeg(s, col, 2, "Backup", v, false);
    };
    auto bans = [&]() {
        snprintf(v, sizeof(v), "%u", static_cast<unsigned>(d.bans));
        dashSeg(s, col, 2, "Bans", v, false);
    };
    auto busy = [&]() {
        dashSeg(s, col, 2, "Busy", busy_.st == SState::Free ? "free" : "in use", false);
    };

    // which: 0 and 1 are the two rows at 80 columns, 0 to 3 the four at 40,
    // and 4 the summary over the bans on the 40 column calls page.
    // A pair that does not fit is left off from the end of its row, so each
    // row ends on the figure that can best be spared: Dir after Backup at
    // 80, because the directory's state is also on page 2 and an open
    // backup window is on no other screen.
    if (wide) {
        if (which == 0) { up(); heap(); stack(); loop(); slow(); }
        else            { wifi(true); data(); card(); backup(); dir(); }
    } else {
        switch (which) {
            case 0:  up(); heap(); break;
            case 1:  stack(); loop(); break;
            case 2:  wifi(false); slow(); dir(); break;
            case 3:  data(); card(); backup(); break;
            default: bans(); busy(); backup(); break;
        }
    }
    rowEnd(s, col);
}

// ---------------------------------------------------------------------------
// dashCall: one of the last calls, newest first, from the caller log's copy
// of its newest five in RAM. Laid out on the node rows' grid: the handle,
// the address and the terminal sit under their own headings.
// ---------------------------------------------------------------------------
void Bbs::dashCall(Session& s, uint8_t back, NodePlan plan) {
    CallRec r;
    uint8_t col = 0;
    if (!calllog::get(back, r)) {
        if (back == 0) rowSeg(s, Color::DarkGrey, "No calls logged yet.", col);
        rowEnd(s, col);
        return;
    }
    char buf[88];
    char when[16];
    clk::fmtEpoch(when, sizeof(when), plan == NodePlan::From ? "%H:%M" : "%m/%d %H:%M", r.start);
    unsigned mins = static_cast<unsigned>((r.secs + 59u) / 60u);
    char mark[2] = { markForFlags(r.flags), '\0' };
    rowSeg(s, Color::LightBlue, callNode(r).t, col);
    rowSeg(s, markColor(mark[0]), mark, col);
    const char* term = Term::nameOf(static_cast<TermType>(r.term), static_cast<Charset>(r.charset));
    switch (plan) {
        case NodePlan::Wide:
        case NodePlan::Wide132:
            // The address starts where the node rows' address does: column
            // 46 at 80, 51 at 132, where the On column sits between. The
            // handle starts at 3 and the minutes end at 40.
            snprintf(buf, sizeof(buf), "%-20.20s %-11s %4u%*s%-15.15s ", r.user, when, mins,
                     plan == NodePlan::Wide ? 6 : 11, "", r.ip);
            rowSeg(s, Color::Grey, buf, col);
            snprintf(buf, sizeof(buf), "%-10.10s", term);
            rowSeg(s, Color::DarkGrey, buf, col);
            break;
        case NodePlan::From:
            snprintf(buf, sizeof(buf), "%-12.12s %5s ", r.user, when);
            rowSeg(s, Color::Grey, buf, col);
            snprintf(buf, sizeof(buf), "%-15.15s", r.ip);
            rowSeg(s, Color::Grey, buf, col);
            break;
        default:                                            // Narrow
            snprintf(buf, sizeof(buf), "%-12.12s %-11s %4u", r.user, when, mins);
            rowSeg(s, Color::Grey, buf, col);
            break;
    }
    rowEnd(s, col);
}

// dashPluginRow: the which-th running plugin's own dashboard line, in Cyan
// and cut to the row. False when there are not that many.
bool Bbs::dashPluginRow(Session& s, uint8_t which) {
    uint8_t seen = 0;
    for (uint8_t k = 0; k < plugins::count(); ++k) {
        const Plugin* p = plugins::at(k);
        if (!p || !plugins::running(k) || !p->status) continue;
        const char* line = p->status();
        if (!line || !*line) continue;
        if (seen++ != which) continue;
        s.term.color(s.tl, Color::Cyan);
        rowEnd(s, s.term.textCols(s.tl, line, rowWidth(s)));
        return true;
    }
    return false;
}

// dashBanRow: the which-th active ban, or "none" for the first when there
// are none. False when there is nothing for this row.
bool Bbs::dashBanRow(Session& s, uint8_t which) {
    uint32_t now = plat::millis();
    uint8_t seen = 0;
    BanList::Entry e;
    for (uint8_t k = 0; k < BBS_BAN_SLOTS; ++k) {
        if (!bans_.at(k, now, e)) continue;
        if (seen++ != which) continue;
        char ip[16], buf[48];
        ipToText(e.ip, ip, sizeof(ip));
        snprintf(buf, sizeof(buf), "%-16s %u min left", ip,
                 static_cast<unsigned>((e.until - now + 59999u) / 60000u));
        uint8_t col = 0;
        rowSeg(s, Color::Grey, buf, col);
        rowEnd(s, col);
        return true;
    }
    if (which == 0) {
        uint8_t col = 0;
        rowSeg(s, Color::DarkGrey, "none", col);
        rowEnd(s, col);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// dashFooter: the first footer row, per width and page. Only what works on
// this page and this terminal is offered: plain ASCII has no cursor keys and
// picks pages by number, and a page with no node rows has nothing to pick.
// ---------------------------------------------------------------------------
void Bbs::dashFooter(Session& s, char* out, size_t n) {
    unsigned pages = dashPages(s);
    unsigned page  = (s.dashPage & 0x7F) + 1u;
    unsigned secs  = s.watchSecs;
    if (!dashCells(s)) {
        if (pages > 1) snprintf(out, n, "Page %u/%u  %s pick a page  Q quits  refresh %us",
                                page, pages, pages == 3 ? "1 2 3" : "1 2", secs);
        else           snprintf(out, n, "Q quits  refresh %us", secs);
        return;
    }
    if (pages == 1) {
        snprintf(out, n, "Up Dn pick  K kick  S snoop  Q quits  refresh %us", secs);
    } else if (pages == 2) {
        if (page == 1) snprintf(out, n, "Page 1/2  < > page  Up Dn pick  K kick  S snoop  Q quits  refresh %us", secs);
        else           snprintf(out, n, "Page %u/2  < > page  Q quits  refresh %us", page, secs);
    } else {
        if (page < 3)  snprintf(out, n, "Pg %u/3  < > page  Up Dn K S  Q quit %us", page, secs);
        else           snprintf(out, n, "Pg %u/3  < > page  Q quit  %us", page, secs);
    }
}


// ---------------------------------------------------------------------------
// The pages. Each takes the row number within its page, draws that row and
// says DR_DREW, or DR_DONE when the page is finished. i == 0 is the top of a
// frame, which is where the snapshot is taken. A row that is there only to
// keep a refreshing frame's height, an empty plugin or ban slot, is DR_SKIP
// when nobody is refreshing, so DASH and DASH ALL do not print blanks.
//
// The height rule: a refreshing page is exactly the terminal's rows less the
// two footer rows, blank rows included, so each redraw lands on the last
// and nothing scrolls. Page 1's last calls take whatever rows are left, one
// to five.
// ---------------------------------------------------------------------------
namespace {

// callsFit: how many last calls page 1 has room for, one to five
uint8_t callsFit(uint8_t rows, uint8_t fixed) {
    int c = static_cast<int>(rows) - static_cast<int>(fixed);
    return static_cast<uint8_t>(c < 1 ? 1 : (c > 5 ? 5 : c));
}

} // namespace

// dashCallsHead: "-- last calls, 12 today ------", the day's count kept by
// the caller log rather than counted here
void Bbs::dashCallsHead(Session& s) {
    char name[32];
    if (clk::valid()) snprintf(name, sizeof(name), "last calls, %u today", static_cast<unsigned>(snap_.today));
    else              snprintf(name, sizeof(name), "last calls");
    rowSection(s, name);
}

// dashNode: node row k of a page, highlighted when it is the pick and the
// frame is a refreshing one on a terminal that can show reverse video
void Bbs::dashNode(Session& s, uint8_t k, NodePlan plan) {
    bool sel = s.watch != ListKind::None && dashCells(s) && s.dashSel == k;
    if (sel) s.term.reverse(s.tl, true);
    rowClose(s, nodeCells(s, *nodeAt(k), plan), sel);
}

// 80 columns: page 1 the lines, what is waiting and the vitals; page 2 the
// plugins, the bans and the last five calls.
uint8_t Bbs::dashPage80(Session& s, uint8_t page, uint8_t i) {
    constexpr uint8_t N = kNodeRows;
    bool refresh = s.watch != ListKind::None;
    uint8_t rows = s.term.rows();
    uint8_t body = rows > 2 ? static_cast<uint8_t>(rows - 2) : 0;

    if (i == 0) { snapFill(false); dashTitle(s); return DR_DREW; }
    if (page == 0) {
        if (i == 1)     { rowClose(s, nodeHead(s, NodePlan::Wide)); return DR_DREW; }
        if (i < 2 + N)  { dashNode(s, static_cast<uint8_t>(i - 2), NodePlan::Wide); return DR_DREW; }
        uint8_t k = static_cast<uint8_t>(i - 2 - N);
        switch (k) {
            case 0: rowRule(s);         return DR_DREW;
            case 1: dashWaiting(s);     return DR_DREW;
            case 2: dashVitals(s, 0);   return DR_DREW;
            case 3: dashVitals(s, 1);   return DR_DREW;
            case 4: dashCallsHead(s);   return DR_DREW;
            default: break;
        }
        uint8_t c = static_cast<uint8_t>(k - 5);
        if (c < callsFit(rows, N + 9)) { dashCall(s, c, NodePlan::Wide); return DR_DREW; }
    } else {
        if (i == 1)  { rowSection(s, "plugins"); return DR_DREW; }
        if (i < 7) {
            if (dashPluginRow(s, static_cast<uint8_t>(i - 2))) return DR_DREW;
            if (!refresh) return DR_SKIP;
            rowText(s, Color::Grey, "");
            return DR_DREW;
        }
        if (i == 7)  { rowSection(s, "bans"); return DR_DREW; }
        if (i < 8 + BBS_BAN_SLOTS) {
            if (dashBanRow(s, static_cast<uint8_t>(i - 8))) return DR_DREW;
            if (!refresh) return DR_SKIP;
            rowText(s, Color::Grey, "");
            return DR_DREW;
        }
        uint8_t k = static_cast<uint8_t>(i - 8 - BBS_BAN_SLOTS);
        if (k == 0) { dashCallsHead(s); return DR_DREW; }
        if (k < 6)  { dashCall(s, static_cast<uint8_t>(k - 1), NodePlan::Wide); return DR_DREW; }
    }
    if (refresh && i < body) { rowText(s, Color::Grey, ""); return DR_DREW; }
    return DR_DONE;
}

// 40 columns: page 1 the lines and the vitals, page 2 where each line is
// calling from and the plugins, page 3 the last calls with their addresses
// and the bans. The handle is at column 3 on every page, so the grid does
// not move under the eye when the page turns.
uint8_t Bbs::dashPage40(Session& s, uint8_t page, uint8_t i) {
    constexpr uint8_t N = kNodeRows;
    bool refresh = s.watch != ListKind::None;
    uint8_t rows = s.term.rows();
    uint8_t body = rows > 2 ? static_cast<uint8_t>(rows - 2) : 0;

    if (i == 0) { snapFill(false); dashTitle(s); return DR_DREW; }
    if (page == 0) {
        if (i == 1)     { rowClose(s, nodeHead(s, NodePlan::Narrow)); return DR_DREW; }
        if (i < 2 + N)  { dashNode(s, static_cast<uint8_t>(i - 2), NodePlan::Narrow); return DR_DREW; }
        uint8_t k = static_cast<uint8_t>(i - 2 - N);
        if (k == 0) { rowRule(s); return DR_DREW; }
        if (k == 1) { dashWaiting(s); return DR_DREW; }
        if (k < 6)  { dashVitals(s, static_cast<uint8_t>(k - 2)); return DR_DREW; }
        if (k == 6) { dashCallsHead(s); return DR_DREW; }
        uint8_t c = static_cast<uint8_t>(k - 7);
        if (c < callsFit(rows, N + 11)) { dashCall(s, c, NodePlan::Narrow); return DR_DREW; }
    } else if (page == 1) {
        if (i == 1)     { rowClose(s, nodeHead(s, NodePlan::From)); return DR_DREW; }
        if (i < 2 + N)  { dashNode(s, static_cast<uint8_t>(i - 2), NodePlan::From); return DR_DREW; }
        uint8_t k = static_cast<uint8_t>(i - 2 - N);
        if (k == 0) { rowRule(s); return DR_DREW; }
        if (k < 6) {
            if (dashPluginRow(s, static_cast<uint8_t>(k - 1))) return DR_DREW;
            if (!refresh) return DR_SKIP;
            rowText(s, Color::Grey, "");
            return DR_DREW;
        }
    } else {
        if (i == 1) {
            char buf[48];
            snprintf(buf, sizeof(buf), "%s%c%-12.12s %5s %-15.15s", " N", ' ', "Handle", "Time", "Address");
            uint8_t col = 0;
            rowSeg(s, Color::LightBlue, buf, col);
            rowEnd(s, col);
            return DR_DREW;
        }
        if (i < 7)  { dashCall(s, static_cast<uint8_t>(i - 2), NodePlan::From); return DR_DREW; }
        if (i == 7) { rowRule(s); return DR_DREW; }
        if (i == 8) { dashVitals(s, 4); return DR_DREW; }
        if (i < 9 + BBS_BAN_SLOTS) {
            if (dashBanRow(s, static_cast<uint8_t>(i - 9))) return DR_DREW;
            if (!refresh) return DR_SKIP;
            rowText(s, Color::Grey, "");
            return DR_DREW;
        }
    }
    if (refresh && i < body) { rowText(s, Color::Grey, ""); return DR_DREW; }
    return DR_DONE;
}

// 132 columns: one page. The 80 column node rows with the minutes each call
// has been on, and beside them the board's figures in SYS's own grammar, so
// the same figure reads the same on both screens.
uint8_t Bbs::dashPage132(Session& s, uint8_t i) {
    constexpr uint8_t N = kNodeRows;
    constexpr uint8_t kLeft = 77;                        // the Wide132 row
    bool refresh = s.watch != ListKind::None;
    uint8_t rows = s.term.rows();
    uint8_t body = rows > 2 ? static_cast<uint8_t>(rows - 2) : 0;

    if (i == 0) { snapFill(false); dashTitle(s); return DR_DREW; }
    if (i == 1) {
        uint8_t col = nodeHead(s, NodePlan::Wide132);
        dashRight(s, col, 0xFF);
        rowEnd(s, col);
        return DR_DREW;
    }
    if (i < 2 + N) {
        uint8_t k = static_cast<uint8_t>(i - 2);
        bool sel = refresh && dashCells(s) && s.dashSel == k;
        if (sel) s.term.reverse(s.tl, true);
        uint8_t col = nodeCells(s, *nodeAt(k), NodePlan::Wide132);
        if (sel) {
            while (col < kLeft) { s.term.ch(s.tl, ' '); ++col; }
            s.term.reverse(s.tl, false);
        }
        dashRight(s, col, k);
        rowEnd(s, col);
        return DR_DREW;
    }
    uint8_t k = static_cast<uint8_t>(i - 2 - N);
    if (k == 0) { rowRule(s); return DR_DREW; }
    if (k == 1) { dashWaiting(s); return DR_DREW; }
    if (k == 2) { dashCallsHead(s); return DR_DREW; }
    if (k < 8)  { dashCall(s, static_cast<uint8_t>(k - 3), NodePlan::Wide132); return DR_DREW; }
    if (refresh && i < body) { rowText(s, Color::Grey, ""); return DR_DREW; }
    return DR_DONE;
}

// ---------------------------------------------------------------------------
// dashRight: the 132 column page's right-hand block, from column 80, on the
// row already begun: which 0xFF is its "-- board ---" heading, 0 to 11 its
// rows. Label 13, value 9, then a quieter note: statRow's grammar, cut at
// the edge of the row rather than wrapped.
// ---------------------------------------------------------------------------
void Bbs::dashRight(Session& s, uint8_t& col, uint8_t which) {
    constexpr uint8_t kAt = 80;
    const DashSnap& d = snap_;
    Term& t = s.term;
    uint8_t w = rowWidth(s);
    uint32_t now = plat::millis();
    while (col < kAt && col < w) { t.ch(s.tl, ' '); ++col; }
    auto put = [&](Color c, const char* text) {
        if (col >= w) return;
        t.color(s.tl, c);
        col = static_cast<uint8_t>(col + t.textCols(s.tl, text, static_cast<uint8_t>(w - col)));
    };
    if (which == 0xFF) {
        put(Color::DarkGrey, "-- ");
        put(Color::Cyan, "board");
        put(Color::DarkGrey, " ");
        t.color(s.tl, Color::DarkGrey);
        while (col < w) { t.ch(s.tl, '-'); ++col; }
        return;
    }

    const char* label = "";
    char value[24] = "";
    char note[48]  = "";
    char a[16], b[16];
    Color vc = Color::LightGreen;
    switch (which) {
        case 0:
            label = "Uptime";
            fmtUptime(value, sizeof(value), now);
            vc = Color::White;
            break;
        case 1:
            label = "Heap free";
            if (d.heapFree) {
                fmtCommas(d.heapFree, value, sizeof(value));
                fmtCommas(d.heapLow, a, sizeof(a));
                snprintf(note, sizeof(note), "low %s", a);
                if (d.heapFree < BBS_HEAP_RESERVE) vc = Color::LightRed;
            } else {
                snprintf(value, sizeof(value), "-");
                vc = Color::DarkGrey;
            }
            break;
        case 2:
            label = "Stack free";
            if (d.stackLeast) {
                fmtCommas(d.stackLeast, value, sizeof(value));
                fmtCommas(plat::stackSize(), a, sizeof(a));
                snprintf(note, sizeof(note), "least of %s", a);
                if (d.stackLeast < 1024u) vc = Color::LightRed;
            } else {
                snprintf(value, sizeof(value), "-");
                vc = Color::DarkGrey;
            }
            break;
        case 3:
            label = "Loop avg";
            snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(loopAvgUs_));
            fmtCommas(loopMaxUs_, a, sizeof(a));
            if (worstPhase_) snprintf(note, sizeof(note), "us, worst %s in %s", a, worstPhase_);
            else             snprintf(note, sizeof(note), "us, worst %s", a);
            break;
        case 4: {
            label = "Slow passes";
            uint32_t window = (s.watchSecs ? s.watchSecs : 5u) * 1000u + 1000u;
            snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(slowCount_));
            snprintf(note, sizeof(note), "over 50ms");
            if (slowAt_ && now - slowAt_ < window) vc = Color::LightRed;
            break;
        }
        case 5: {
            label = "Signal";
            int8_t rssi = d.net.valid ? d.net.rssi : 0;
            const char* word = signalWord(rssi, vc);
            if (rssi) snprintf(value, sizeof(value), "%d dBm", static_cast<int>(rssi));
            else      snprintf(value, sizeof(value), "-");
            bool known = d.power[0] && d.power[0] != '?';   // as dashVitals
            bool awake = !known || d.power[0] == 'n';
            int used = snprintf(note, sizeof(note), "%s", word);
            if (d.net.channel && rssi && used > 0)
                used += snprintf(note + used, sizeof(note) - static_cast<size_t>(used), ", ch %u",
                                 static_cast<unsigned>(d.net.channel));
            if (known && used > 0 && static_cast<size_t>(used) < sizeof(note))
                snprintf(note + used, sizeof(note) - static_cast<size_t>(used), ", %s",
                         awake ? "awake" : "SLEEPING");
            if (!awake) vc = Color::LightRed;
            break;
        }
        case 6:
            label = "Data free";
            fmtCommas(d.dataFree, value, sizeof(value));
            snprintf(note, sizeof(note), "bytes");
            break;
        case 7:
            label = "Card";
            if (!d.card) {
                snprintf(value, sizeof(value), "none");
                vc = Color::DarkGrey;
            } else if (d.cardFreeKB >= 1048576u) {
                snprintf(value, sizeof(value), "%u GB",
                         static_cast<unsigned>((d.cardFreeKB + 524288u) / 1048576u));
                snprintf(note, sizeof(note), "free of %u",
                         static_cast<unsigned>((d.cardTotalKB + 524288u) / 1048576u));
            } else {
                snprintf(value, sizeof(value), "%u MB", static_cast<unsigned>(d.cardFreeKB / 1024u));
                snprintf(note, sizeof(note), "free of %u", static_cast<unsigned>(d.cardTotalKB / 1024u));
            }
            break;
        case 8:
        case 11: {
            label = which == 8 ? "Directory" : "Lights";
            const char* line = statusOf(which == 8 ? "announce" : "lights");
            if (!line) {
                snprintf(value, sizeof(value), "off");
                vc = Color::DarkGrey;
                break;
            }
            const char* rest = "";
            splitStatus(line, value, sizeof(value), &rest);
            snprintf(note, sizeof(note), "%s", rest);
            if (which == 8 && !strcmp(value, "held")) vc = Color::LightRed;
            break;
        }
        case 9:
            label = "Backup";
            if (backup_.awaitingApproval()) {
                snprintf(value, sizeof(value), "Y/N");
                snprintf(note, sizeof(note), "an upload waits for you");
                vc = Color::Yellow;
            } else if (backup_.isOpen()) {
                int32_t left = static_cast<int32_t>(backup_.closesAt() - now) / 1000;
                if (left < 0) left = 0;
                snprintf(value, sizeof(value), "open %ld:%02ld", static_cast<long>(left / 60),
                         static_cast<long>(left % 60));
            } else {
                snprintf(value, sizeof(value), "closed");
                vc = Color::Grey;
            }
            break;
        case 10: {
            label = "Bans";
            snprintf(value, sizeof(value), "%u", static_cast<unsigned>(d.bans));
            BanList::Entry e;
            for (uint8_t k = 0; k < BBS_BAN_SLOTS; ++k) {
                if (!bans_.at(k, now, e)) continue;
                ipToText(e.ip, b, sizeof(b));
                snprintf(note, sizeof(note), "%s, %u min", b,
                         static_cast<unsigned>((e.until - now + 59999u) / 60000u));
                break;
            }
            break;
        }
        default:
            return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%-13.13s", label);
    put(Color::Grey, buf);
    snprintf(buf, sizeof(buf), "%9s", value);
    put(vc, buf);
    if (note[0]) {
        put(Color::DarkGrey, " ");
        put(Color::DarkGrey, note);
    }
}

// ---------------------------------------------------------------------------
// rowDash: one row of whichever page this caller is on, at whichever width.
// DASH ALL walks the pages in turn as one list.
// ---------------------------------------------------------------------------
bool Bbs::rowDash(Session& s) {
    for (;;) {
        uint8_t pages = dashPages(s);
        uint8_t page  = static_cast<uint8_t>(s.dashPage & 0x7F);
        if (page >= pages) {                         // the terminal changed width under it
            page = 0;
            s.dashPage = static_cast<uint8_t>(s.dashPage & kDashAll);
        }
        uint8_t i = s.listIdx++;
        // A refreshing frame stops at the terminal's height less its two
        // footer rows, whatever the page has left. The pages are laid out for
        // 24 and 25 rows; a window resized shorter would otherwise get the
        // whole page redrawn from home and scroll on every frame, which is
        // the thing a fixed height is for. What is cut is the bottom of the
        // page: the last calls, the bans, and on a very short window the
        // lines themselves.
        if (s.watch != ListKind::None) {
            uint8_t rows = s.term.rows();
            if (i >= (rows > 2 ? rows - 2 : 0)) return false;
        }
        uint8_t r = rowWidth(s) >= kDash132 ? dashPage132(s, i)
                  : rowWidth(s) >= kDash80  ? dashPage80(s, page, i)
                  :                           dashPage40(s, page, i);
        if (r == DR_DREW) return true;
        if (r == DR_SKIP) continue;
        if ((s.dashPage & kDashAll) && page + 1 < pages) {
            s.dashPage = static_cast<uint8_t>(kDashAll | (page + 1));
            s.listIdx  = 0;
            continue;
        }
        return false;
    }
}

// ---------------------------------------------------------------------------
// dashKey: a key while DASH n is up.
//
//   Left Right < > - +   the page before or after, wrapping
//   1 2 3                that page
//   Enter                redraw now
//   Q ESC Ctrl-C Space   back to the prompt
//   Up Down              pick a node row (ANSI and PETSCII, pages with nodes)
//   K S                  KICK or SNOOP the picked node, at the prompt
//
// Anything else is ignored, so a stray key no longer throws the dashboard
// away. K and S go through the command table exactly as if typed there, so
// the permission and rank rules that decide who may be kicked or watched
// are KICK's and SNOOP's own; a command this caller may not run is not
// offered by the key either.
// ---------------------------------------------------------------------------
void Bbs::dashKey(Session& s, int k, uint32_t now) {
    uint8_t pages = dashPages(s);
    uint8_t page  = static_cast<uint8_t>(s.dashPage & 0x7F);
    if (page >= pages) page = 0;

    // restart: the frame again from its first row, cleared first for a new
    // page (a page of a different shape would leave its tail) and homed for
    // the same page. A frame half drawn is simply not finished: every row
    // is written whole, so nothing is left open mid-attribute.
    auto restart = [&](bool clear) {
        if (clear) s.term.cls(s.tl);
        else {
            s.term.home(s.tl);
            if (!dashCells(s)) s.term.nl(s.tl);      // plain ASCII redraws below
        }
        s.listIdx   = 0;
        s.listSub   = 0;
        s.watchNext = 0;
    };

    if (k == 'q' || k == 'Q' || k == KEY_ESC || k == KEY_BREAK || k == ' ') {
        stopWatch(s);
        return;
    }
    int to = -1;
    if (k == KEY_LEFT || k == '<' || k == '-')       to = (page + pages - 1) % pages;
    else if (k == KEY_RIGHT || k == '>' || k == '+') to = (page + 1) % pages;
    else if (k >= '1' && k <= '9') {
        if (k - '1' >= pages) return;                // past the last page: nothing
        to = k - '1';
    }
    if (to >= 0) {
        if (to != page) {
            s.dashPage = static_cast<uint8_t>(to);
            restart(true);
        }
        return;
    }
    if (k == KEY_ENTER) { restart(false); return; }

    // The pick: only where node rows are on screen, and only on a terminal
    // that can show which row it is on.
    if (!dashCells(s) || !(page == 0 || (pages == 3 && page == 1))) return;
    if (k == KEY_UP || k == KEY_DOWN) {
        if (s.dashSel >= kNodeRows)                        s.dashSel = k == KEY_DOWN ? 0 : kNodeRows - 1;
        else if (k == KEY_DOWN && s.dashSel + 1 < kNodeRows) ++s.dashSel;
        else if (k == KEY_UP && s.dashSel > 0)             --s.dashSel;
        restart(false);
        return;
    }
    bool kick  = k == 'k' || k == 'K';
    bool snoop = k == 's' || k == 'S';
    if ((!kick && !snoop) || s.dashSel >= kNodeRows) return;
    const Session* o = nodeAt(s.dashSel);
    const char* verb = kick ? "KICK" : "SNOOP";
    if (!o || !findCommand(verb, s)) return;
    char line[16];
    snprintf(line, sizeof(line), "%s %s", verb, nodeName(*o).t);
    stopWatch(s);                                    // back at the prompt
    s.term.text(s.tl, line);                         // as though typed there
    runCommand(s, line, now);
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
//
// The radio and the heap come from the dashboard's snapshot, taken once at
// the title (1.1.0). They were read at the top of this function, which runs
// once per ROW: thirty calls into the Wi-Fi task and thirty heap walks under
// a critical section for one listing, all but two of them thrown away.
// ---------------------------------------------------------------------------
bool Bbs::rowSys(Session& s) {
    char buf[48], num[16];
    uint8_t i = s.listIdx++;
    if (i == 0) snapFill(true);                  // with the heap walk, for the biggest block
    const plat::NetInfo& net = snap_.net;
    const DashSnap& h = snap_;

    switch (i) {
        case 0:  rowTitle(s, "System", BBS_VERSION); return true;

        case 1:  rowSection(s, "network"); return true;
        case 2:  statRow(s, "Wi-Fi", net.ssid[0] ? net.ssid : "-", Color::White); return true;
        case 3: {
            int8_t rssi = net.valid ? net.rssi : 0;
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
        case 7: {
            // What the radio is ACTUALLY doing, read back rather than
            // assumed. A station in a power-save mode sleeps between DTIM
            // beacons and the access point buffers for it, so a packet
            // arriving in a quiet moment waits about a second. That has now
            // been chased twice from the outside, and once it was diagnosed
            // wrongly with confident arithmetic. A board that says which
            // mode it is in turns the next one into a reading.
            const char* ps = snap_.power;
            // "?" is the driver not saying, not a radio asleep
            if (!ps || !*ps || *ps == '?') { statRow(s, "Radio", "-", Color::Grey); return true; }
            bool awake = ps[0] == 'n';          // "none"
            statRow(s, "Radio", awake ? "awake" : ps,
                    awake ? Color::LightGreen : Color::LightRed,
                    awake ? "power save off" : "SLEEPING, expect ~1s lag");
            return true;
        }
        case 5:  statRow(s, "Address", net.ip[0] ? net.ip : "-", Color::White); return true;
        case 6:
            // The port it is answering on, not the configured one: a port
            // changed in CONFIG waits for a restart, and until then this is
            // still where callers get through.
            snprintf(num, sizeof(num), "%u", static_cast<unsigned>(port()));
            statRow(s, "Port", num, Color::LightGreen, syscfg::get().hostname);
            return true;

        case 8:  rowSection(s, "memory"); return true;
        case 9:
            if (h.heapFree) statNum(s, "Heap free", h.heapFree, "bytes");
            else            statRow(s, "Heap free", "-", Color::DarkGrey, "host build");
            return true;
        case 10:
            if (h.heapFree) statNum(s, "Heap low", h.heapLow, "since boot");
            else            statRow(s, "Heap low", "-", Color::DarkGrey);
            return true;
        case 11:
            if (h.heapFull) statNum(s, "Biggest blk", h.heapBig, "bytes");
            else            statRow(s, "Biggest blk", "-", Color::DarkGrey);
            return true;
        case 12:
            fmtCommas(static_cast<uint32_t>(sizeof(Session)), num, sizeof(num));
            snprintf(buf, sizeof(buf), "x %u sessions", static_cast<unsigned>(kSessions));
            statRow(s, "Session", num, Color::LightGreen, buf);
            return true;

        case 13: rowSection(s, "storage"); return true;
        case 14: {
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
        case 15: statNum(s, "Data free", h.dataFree, "bytes"); return true;
        case 16: statNum(s, "Held back", plugins::reserveBytes(), "for the board"); return true;

        case 17: rowSection(s, "load"); return true;
        case 18:
            fmtUptime(buf, sizeof(buf), plat::millis());
            statRow(s, "Uptime", buf, Color::White);
            return true;
        case 19:
            // An uptime that keeps starting over is the only symptom of a
            // board that restarts on its own, so say why it started, every
            // boot, power on included: that is the question a sysop brings
            // here. A row of its own (1.1.0, copy SYS-l-restart) rather than
            // Uptime's note, which at 40 columns has 16 characters and
            // wrapped four of the words. statRow pads and never cuts, so the
            // longest, "brownout (power dipped)", is a 36 column row.
            statRow(s, "Last restart", bootReason(),
                    bootWasCrash() ? Color::LightRed
                                   : (bootNoted_ ? Color::Yellow : Color::White));
            return true;
        case 20: {
            char when[24] = "-";
            if (clk::valid()) clk::fmt(when, sizeof(when), "%H:%M:%S");
            statRow(s, "Clock", when, Color::White, clk::valid() ? nullptr : "not set");
            return true;
        }
        case 21: statNum(s, "Loop avg", loopAvgUs_, "us of work"); return true;
        case 22: {
            // The worst pass says which phase owned it. Without that a stall
            // is a bare number and the investigation starts with a guess,
            // which is exactly how the last one was got wrong.
            char note[64] = "us";
            if (worstPhase_) {
                if (worstNode_ && worstDoing_[0])
                    snprintf(note, sizeof(note), "us in %s, node %u, %s",
                             worstPhase_, static_cast<unsigned>(worstNode_), worstDoing_);
                else if (worstNode_)
                    snprintf(note, sizeof(note), "us in %s, node %u",
                             worstPhase_, static_cast<unsigned>(worstNode_));
                else
                    snprintf(note, sizeof(note), "us in %s", worstPhase_);
            }
            statNum(s, "Loop worst", loopMaxUs_, note);
            return true;
        }
        case 23: statNum(s, "Loop passes", loopPasses_, nullptr); return true;
        case 24:
            // How many, not just how bad. One stall at boot and a stall every
            // minute look identical on a high-water mark.
            statNum(s, "Slow passes", slowCount_, "over 50ms");
            // Stack headroom: the least this task's stack has ever had free.
            // Not a decoration. It is what said the UserRec scratch buffers
            // could become ordinary locals (about 10 KB of static DRAM, in
            // 1.1.0), and it is what says whether that is still true. The
            // console names the call behind each new low (stackWatch).
            {
                uint32_t sf = plat::stackFree();
                if (sf) {
                    // Out of what, because the answer moved: 8,192 until
                    // 1.1.0 and 12,288 since, and "1,440 free" means
                    // something different against each.
                    // The note starts at column 23 (label 13, value 9, a
                    // space); the host's far larger stack would push it past
                    // a 40 column row, so it falls back rather than wrap.
                    char of[16], note[28];
                    fmtCommas(plat::stackSize(), of, sizeof(of));
                    snprintf(note, sizeof(note), "least of %s", of);
                    if (23 + strlen(note) > rowWidth(s)) snprintf(note, sizeof(note), "least ever");
                    statNum(s, "Stack free", sf, note);
                } else {
                    statRow(s, "Stack free", "n/a", Color::Grey, "not measured");
                }
            }
            return true;

        case 25: rowSection(s, "traffic"); return true;
        case 26:
            snprintf(num, sizeof(num), "%u", static_cast<unsigned>(activeNodes()));
            snprintf(buf, sizeof(buf), "of %u, peak %u", static_cast<unsigned>(BBS_MAX_NODES),
                     static_cast<unsigned>(peakNodes_));
            statRow(s, "Nodes busy", num, Color::LightGreen, buf);
            return true;
        case 27: statNum(s, "Calls", callsBoot_, "since boot"); return true;
        case 28:
            snprintf(num, sizeof(num), "%u", static_cast<unsigned>(calllog::count()));
            snprintf(buf, sizeof(buf), "of %u kept", static_cast<unsigned>(BBS_CALLLOG_SIZE));
            statRow(s, "Log", num, Color::LightGreen, buf);
            return true;
        case 29: {
            uint8_t run = 0;
            for (uint8_t k = 0; k < plugins::count(); ++k) if (plugins::running(k)) ++run;
            snprintf(num, sizeof(num), "%u", static_cast<unsigned>(run));
            snprintf(buf, sizeof(buf), "of %u compiled in", static_cast<unsigned>(plugins::count()));
            statRow(s, "Plugins", num, Color::LightGreen, buf);
            return true;
        }
        case 30: {
            uint8_t live = 0;                                  // only the bans still running
            BanList::Entry e;
            for (uint8_t k = 0; k < BBS_BAN_SLOTS; ++k) if (bans_.at(k, plat::millis(), e)) ++live;
            statNum(s, "IP bans", live, live ? "active" : nullptr);
            return true;
        }

        case 31: rowRule(s); return true;
        case 32: rowText(s, Color::DarkGrey, "CALLS shows the board hour by hour"); return true;
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
// minutesLeft: the plugin-facing form of secondsLeft.
//
// Rounded up, because somebody asking is deciding whether to start
// something and "0 minutes" with thirty seconds to go is the wrong answer.
// -1 for a call that is not on the clock at all.
// ---------------------------------------------------------------------------
int32_t Bbs::minutesLeft(const Session& s, uint32_t now) const {
    if (s.role != Role::Caller || !s.loggedIn || unlimited(s)) return -1;
    int32_t secs = secondsLeft(s, now);
    if (secs < 0) secs = 0;
    return (secs + 59) / 60;
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
    // An empty node is not the same as a busy one, and saying so costs
    // nothing. "Node 4 is not taking pages" directly contradicts the WHO the
    // caller just read, which said node 4 was free, and leaves them thinking
    // the board is broken or that somebody is ignoring them.
    //
    // Hidden and DND deliberately stay lumped together with each other: a
    // caller must not be able to tell a hidden staff member from one who has
    // switched pages off, because that would make HIDE detectable by
    // probing. Nothing is given away by admitting a line is empty, since WHO
    // already shows exactly that.
    if (!to->loggedIn || to->role == Role::Busy) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Nobody is on node %s.", nodeName(*to).t);
        t.color(tl, Color::LightRed);
        t.text(tl, buf);
        return;
    }
    bool hidden = !to->visible || to->lurk;
    if (to->dnd || hidden) {
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
// cmdBell: BELL, the same setting as the room's /b. Bells other callers
// cause: pages, broadcasts, arrivals, the room, @BELL@ in a message. A
// caller's own mistakes still beep, because that bell is an answer to
// something they did, not somebody else reaching into their speaker.
// ---------------------------------------------------------------------------
void Bbs::cmdBell(Session& s) {
    s.bellOff = !s.bellOff;
    s.term.color(s.tl, Color::Cyan);
    s.term.text(s.tl, s.bellOff ? "Bell off." : "Bell on.");
}

// ---------------------------------------------------------------------------
// cmdCodes: screens/codes if the board has one, the short list if not.
// The same shape as ABOUT: a screen a sysop can redraw, and words that are
// always there when nobody has.
// ---------------------------------------------------------------------------
void Bbs::cmdCodes(Session& s) {
    if (playScreen(s, "codes")) return;
    codesSummary(s);
    prompt(s);
}

void Bbs::codesSummary(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    // Every line inside 39 columns, and printed with Term::text, which
    // knows nothing of @-codes: the list shows the codes, it does not act
    // on them, or @BELL@ here would ring.
    static const char* const kLines[] = {
        "Colour  @RED@ @YELLOW@ @LTBLUE@ ...",
        "        @N@ goes back to normal",
        "Effects @BLINK:hi@ @SCRAMBLE:hi@",
        "        @TYPE:hi@ @OOPS:hi@",
        "Extras  @SPIN@ @DOTS@ @NOISE@ @RULE@",
        "        @BELL@ rings once a message",
        "Fill-in @BOARD@ @DATE@ @TIME@",
        "@@ is a plain @. 8 codes a message.",
        "Works in forums, mail and the room.",
    };
    rowTitle(s, "Codes in messages");
    for (const char* l : kLines) {
        t.color(tl, Color::Grey);
        t.text(tl, l);
        t.nl(tl);
    }
    rowRule(s);
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
        // The published default is honoured from the board's own network
        // only. From anywhere else it is a wrong password, ban count and
        // all, because anybody can read it on the install page.
        if (lv == Access::Sysop && syscfg::get().sysopDefault && !localAddr(s.ip)) {
            plat::log("bbs: node %s default sysop password refused from %s (not local)",
                      nodeName(s).t, s.ip);
            lv = Access::None;
        }
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
        case 1:  label(t, tl, "Typewriter"); fx::typewriter(t, tl, "The quick brown fox jumps.", 45);
                 fxCode(t, tl, "@TYPE:text@"); t.nl(tl); break;
        case 2:  label(t, tl, "Dots"); t.text(tl, "Dialing"); fx::dots(t, tl, 6, 250);
                 t.text(tl, " CONNECT"); fxCode(t, tl, "@DOTS@"); t.nl(tl); break;
        case 3:  label(t, tl, "Spin line");  fx::spinner(t, tl, fx::Spin::Line, 1800, 100);
                 t.text(tl, "done"); fxCode(t, tl, "@SPIN@"); t.nl(tl); break;
        case 4:  label(t, tl, "Spin dots");  fx::spinner(t, tl, fx::Spin::Dots, 1600, 150);
                 t.text(tl, "done"); t.nl(tl); break;
        case 5:  label(t, tl, "Spin arrow"); fx::spinner(t, tl, fx::Spin::Arrow, 1600, 120);
                 t.text(tl, "done"); t.nl(tl); break;
        case 6:  label(t, tl, "Rubout"); fx::typeRubout(t, tl, "Erasing to start", 35, 600, 45);
                 t.text(tl, "gone"); fxCode(t, tl, "@OOPS:text@"); t.nl(tl); break;
        case 7:  label(t, tl, "Rewrite"); t.text(tl, "Checking mail"); fx::dots(t, tl, 3, 300);
                 fx::rewrite(t, tl, 16, "No new mail"); t.nl(tl); break;
        case 8:  label(t, tl, "Working"); fx::working(t, tl, "Scanning ", 1500, "OK"); break;
        case 9:  label(t, tl, "Progress"); fx::progressBar(t, tl, 16, 2000); t.nl(tl); break;
        case 10: label(t, tl, "Countdown"); fx::countdown(t, tl, 5, 450, "GO!"); t.nl(tl); break;
        case 11: label(t, tl, "Scramble"); fx::scramble(t, tl, "ACCESS GRANTED", 14, 70);
                 fxCode(t, tl, "@SCRAMBLE:text@"); t.nl(tl); break;
        case 12: label(t, tl, "Line noise"); fx::lineNoise(t, tl, 18, 700);
                 t.text(tl, "clean"); fxCode(t, tl, "@NOISE@"); t.nl(tl); break;
        case 13: label(t, tl, "Blink"); t.color(tl, Color::LightRed);
                 fx::blink(t, tl, "ALERT", 4, 250); t.color(tl, Color::Grey);
                 fxCode(t, tl, "@BLINK:text@"); t.nl(tl); break;
        case 14: label(t, tl, "Marquee"); fx::marquee(t, tl, "** UNLEASHED BBS **", 16, 80, 1);
                 t.text(tl, "done"); t.nl(tl); break;
        case 15: label(t, tl, "Cursor"); fx::cursorBlink(t, tl, 5, 250);
                 t.text(tl, "ready"); t.nl(tl); break;
        case 16: label(t, tl, "Bell"); fx::bell(t, tl); t.text(tl, "ding"); fxCode(t, tl, "@BELL@"); t.nl(tl); break;
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
            fxCode(t, tl, "@RED@ @CYAN@ ... @N@");
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
            // Back the other way too: CODES points here, this points there.
            t.nl(tl);
            t.color(tl, Color::Grey);
            t.text(tl, "Grey codes work in messages. See CODES.");
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
