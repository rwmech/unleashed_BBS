/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/bbs_sysop.cpp
 * Module:       Core / staff access
 *
 * Purpose:      Staff access.
 *                  Sysop: BYE <sysop_password> from any node or the busy line
 *                  moves the session to the hidden sysop node and frees its
 *                  caller node. Every permission, always.
 *                  Co-sysop 1/2: BYE <cosysopN_password> keeps the session on
 *                  its caller node and grants that level's column of the
 *                  [access] matrix in system.cfg.
 *                  Commands: NODES KICK BROADCAST SNOOP TIME n +/-m SHOW HIDE
 *                  LURK BANS UNBAN DROP (see COMMANDS.md). A command the
 *                  session does not hold answers as an unknown command.
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
#include "clock.h"
#include "plugin.h"
#include "sysconfig.h"
#include "../platform/platform.h"

#include <climits>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>

using namespace bbsu;

namespace {

void say(Term& t, Timeline& tl, Color c, const char* s) {
    t.color(tl, c);
    t.text(tl, s);
}

} // namespace

// ---------------------------------------------------------------------------
// cmdShow / cmdLurk: presence for staff (dispatch and permission checks
// come from the command table in bbs_shell.cpp)
// ---------------------------------------------------------------------------
void Bbs::cmdShow(Session& s, bool show) {
    if (show) {
        s.visible = true;
        s.lurk    = false;
        s.dnd     = false;
        say(s.term, s.tl, Color::Cyan, "You are listed in WHO.");
    } else {
        s.visible = false;
        say(s.term, s.tl, Color::Cyan, "You are hidden from WHO.");
    }
}

void Bbs::cmdLurk(Session& s) {
    s.lurk = !s.lurk;
    if (s.lurk) { s.visible = false; s.dnd = true; }
    else        { s.dnd = false; }
    say(s.term, s.tl, Color::Cyan, s.lurk ? "Lurking: hidden, pages off." : "Lurk off: still hidden, pages on.");
}

// ---------------------------------------------------------------------------
// markAccount: remember the rank on the account that just used a staff
// password. Guests and the busy line have no account to mark.
// ---------------------------------------------------------------------------
void Bbs::markAccount(Session& s, Access level) {
    s.rank = static_cast<uint8_t>(level) > s.rank ? static_cast<uint8_t>(level) : s.rank;
    if (s.guest || !s.user[0]) return;
    static UserRec u;
    if (!users::find(s.user, u)) return;
    if (u.level >= static_cast<uint8_t>(level)) return;
    u.level = static_cast<uint8_t>(level);
    if (users::update(u.handle, u) == users::Result::Ok) {
        plat::log("bbs: account '%s' marked %s", u.handle, syscfg::levelName(level));
    }
}

// ---------------------------------------------------------------------------
// elevate: move a caller (or busy-line guest) onto the sysop node
// ---------------------------------------------------------------------------
void Bbs::elevate(Session& s, uint32_t now) {
    if (sysop_.st != SState::Free) {
        plat::log("bbs: sysop node in use, node %c logs off instead", nodeChar(s));
        goodbye(s, now);
        return;
    }
    char from = nodeChar(s);
    bool fromNode = s.role == Role::Caller;

    moveSession(s, sysop_, 0, Role::Sysop);

    Session& d = sysop_;
    d.level      = Access::Sysop;
    d.perms      = syscfg::permsFor(Access::Sysop);
    d.visible    = false;
    d.lurk       = false;
    d.dnd        = false;
    d.busyLoginUntil = 0;
    d.timeWarned = 0;
    markAccount(d, Access::Sysop);
    if (!d.loggedIn) {                        // busy-line guest was never logged in
        d.loggedIn   = true;
        d.loginAt    = now;
        d.loginEpoch = clk::epoch();
    }

    Term& t = d.term;
    Timeline& tl = d.tl;
    char buf[48];
    t.color(tl, Color::LightGreen);
    t.text(tl, "Sysop node.");
    t.nl(tl);
    t.color(tl, Color::Grey);
    if (fromNode) {
        snprintf(buf, sizeof(buf), "Node %c is free for callers.", from);
        t.text(tl, buf);
        t.nl(tl);
    }
    t.text(tl, "Hidden from WHO. HELP for commands.");
    t.nl(tl);

    plat::log("bbs: node %c -> sysop node (%s, %s)", from, d.user, d.ip);
    prompt(d);
}

// ---------------------------------------------------------------------------
// coElevate: grant a co-sysop level in place. No special line: the session
// keeps its caller node and stays visible until it uses HIDE or LURK.
// ---------------------------------------------------------------------------
void Bbs::coElevate(Session& s, Access level, uint32_t now) {
    (void)now;
    s.level      = level;
    s.perms      = syscfg::permsFor(level);
    s.timeWarned = 0;
    markAccount(s, level);

    Term& t = s.term;
    Timeline& tl = s.tl;
    char buf[48];
    snprintf(buf, sizeof(buf), "%s access on node %u.", syscfg::levelName(level), s.id);
    say(t, tl, Color::LightGreen, buf);
    t.nl(tl);
    say(t, tl, Color::Grey, can(s, PERM_NOLIMITS) ? "No time limits. HELP for commands."
                                               : "HELP for commands.");
    plat::log("bbs: node %u -> %s (%s, %s) perms 0x%03x", s.id, syscfg::levelName(level),
              s.user, s.ip, s.perms);
    prompt(s);
}

// ---------------------------------------------------------------------------
// nodeByArg: "1".."6", "S" (sysop), "B" (busy line). *rest gets the text
// after the node token. nullptr if the token is not a node.
// ---------------------------------------------------------------------------
Session* Bbs::nodeByArg(const char* arg, const char** rest) {
    while (*arg == ' ') ++arg;
    Session* o = nullptr;
    const char* p = arg;
    if (*p == 's' || *p == 'S') {
        o = &sysop_;
        ++p;
    } else if (*p == 'b' || *p == 'B') {
        o = &busy_;
        ++p;
    } else if (isdigit(static_cast<unsigned char>(*p))) {
        char* end = nullptr;
        long n = strtol(p, &end, 10);
        p = end;
        if (n >= 1 && n <= BBS_MAX_NODES) o = &nodes_[n - 1];
    }
    if (*p && *p != ' ') o = nullptr;         // "5x", "Steve"
    while (*p == ' ') ++p;
    if (rest) *rest = p;
    return o;
}

// ===========================================================================
// Lists
// ===========================================================================

// ---------------------------------------------------------------------------
// rowNodes: every live session including the busy line and the sysop
// ---------------------------------------------------------------------------
bool Bbs::rowNodes(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    bool wide = t.cols() >= 60;
    const char* fmt = wide ? "%c%c%-20.20s %-15.15s %-10.10s %4s %5s" : "%c%c%-10.10s %-15.15s %4s %5s";
    char buf[96];
    char left[12];
    char idle[8];
    uint32_t now = plat::millis();

    for (;;) {
        uint8_t i = s.listIdx++;
        if (i == 0) {
            char count[16];
            snprintf(count, sizeof(count), "%u of %u", activeNodes(), BBS_MAX_NODES);
            rowTitle(s, "Nodes", count);
            return true;
        }
        if (i == 1) {
            if (wide) snprintf(buf, sizeof(buf), fmt, 'N', ' ', "Handle", "IP", "Terminal", "Left", "Idle");
            else      snprintf(buf, sizeof(buf), fmt, 'N', ' ', "Handle", "IP", "Left", "Idle");
            t.color(tl, Color::LightBlue);
            t.text(tl, buf);
            t.nl(tl);
            return true;
        }
        uint8_t k = static_cast<uint8_t>(i - 2);
        if (k == kSessions) { rowRule(s); return true; }
        if (k == kSessions + 1) {
            say(t, tl, Color::DarkGrey, kMarkKey);
            t.nl(tl);
            return true;
        }
        if (k > kSessions + 1) return false;
        const Session* o = all_[k];
        if (o->role != Role::Caller && o->st == SState::Free) continue;

        if (o->st == SState::Free) {
            snprintf(buf, sizeof(buf), "%c -", nodeChar(*o));
            t.color(tl, Color::DarkGrey);
            t.text(tl, buf);
            t.nl(tl);
            return true;
        }

        char h[24] = "(no handle)";
        if (o->user[0]) listHandle(h, sizeof(h), o->user, wide ? 20 : 10);
        bool hidden = o != &s && (!o->visible || o->lurk);
        if (o->role == Role::Caller && o->loggedIn && !can(*o, PERM_NOLIMITS)) {
            int32_t sec = secondsLeft(*o, now);
            if (sec == INT32_MAX) snprintf(left, sizeof(left), "--");
            else                  snprintf(left, sizeof(left), "%ld", static_cast<long>((sec + 59) / 60));
        } else {
            snprintf(left, sizeof(left), "--");
        }
        fmtIdle(idle, sizeof(idle), now - o->lastInput);
        if (wide) snprintf(buf, sizeof(buf), fmt, nodeChar(*o), markFor(*o), h, o->ip, o->term.name(), left, idle);
        else      snprintf(buf, sizeof(buf), fmt, nodeChar(*o), markFor(*o), h, o->ip, left, idle);
        t.color(tl, o == &s ? Color::White : (hidden ? Color::DarkGrey : Color::Grey));
        t.text(tl, buf);
        t.nl(tl);
        return true;
    }
}

// ---------------------------------------------------------------------------
// rowBans: active bans with minutes remaining
// ---------------------------------------------------------------------------
bool Bbs::rowBans(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    uint32_t now = plat::millis();
    char buf[48];
    char ip[16];
    BanList::Entry e;

    // rows: 0 title | 1 header | 2.. one per active ban (slot k at k + 2) |
    // BBS_BAN_SLOTS + 2 closing rule
    uint8_t i = s.listIdx++;
    if (i == 0) {
        rowTitle(s, "Bans");
        return true;
    }
    if (i == 1) {
        say(t, tl, Color::LightBlue, "Banned IP        Min left");
        t.nl(tl);
        return true;
    }
    if (i == 2) {
        bool any = false;
        for (uint8_t k = 0; k < BBS_BAN_SLOTS && !any; ++k) any = bans_.at(k, now, e);
        if (!any) {
            say(t, tl, Color::Grey, "No active bans.");
            t.nl(tl);
            s.listIdx = static_cast<uint8_t>(BBS_BAN_SLOTS + 2);
            return true;
        }
    }
    if (i >= BBS_BAN_SLOTS + 2) {
        if (i > BBS_BAN_SLOTS + 2) return false;
        rowRule(s);
        return true;
    }
    for (uint8_t k = static_cast<uint8_t>(i - 2); k < BBS_BAN_SLOTS; ++k) {
        if (!bans_.at(k, now, e)) continue;
        s.listIdx = static_cast<uint8_t>(k + 3);
        ipToText(e.ip, ip, sizeof(ip));
        snprintf(buf, sizeof(buf), "%-16s %u", ip, static_cast<unsigned>((e.until - now + 59999u) / 60000u));
        say(t, tl, Color::Grey, buf);
        t.nl(tl);
        return true;
    }
    s.listIdx = static_cast<uint8_t>(BBS_BAN_SLOTS + 3);
    rowRule(s);
    return true;
}

// ===========================================================================
// Commands
// ===========================================================================

// ---------------------------------------------------------------------------
// cmdKick: KICK n [message]
// ---------------------------------------------------------------------------
void Bbs::cmdKick(Session& s, const char* arg, uint32_t now) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    const char* msg = nullptr;
    Session* o = nodeByArg(arg, &msg);
    if (!o || o == &s || o->st == SState::Free || o->st == SState::Closing ||
        (!outranks(s, *o) && o->level != Access::None) ||
        (o->role == Role::Sysop && s.level != Access::Sysop)) {
        say(t, tl, Color::LightRed, "No caller on that node.");
        return;
    }
    char text[BBS_LINE_MAX + 32];
    if (msg && *msg) snprintf(text, sizeof(text), "Disconnected by sysop: %s", msg);
    else             snprintf(text, sizeof(text), "Disconnected by sysop.");
    char c = nodeChar(*o);
    plat::log("bbs: sysop kicked node %c (%s, %s)", c, o->user, o->ip);
    hangup(*o, text, now);

    char buf[32];
    snprintf(buf, sizeof(buf), "Node %c disconnected.", c);
    say(t, tl, Color::LightGreen, buf);
}

// ---------------------------------------------------------------------------
// cmdBroadcast: BROADCAST message, to every logged-in session
// ---------------------------------------------------------------------------
void Bbs::cmdBroadcast(Session& s, const char* arg) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    if (!*arg) {
        say(t, tl, Color::LightRed, "Usage: BROADCAST message");
        return;
    }
    unsigned sent = 0;
    for (Session* o : all_) {
        if (o == &s || !o->loggedIn || o->role == Role::Busy) continue;
        post(*o, BusKind::Broadcast, &s, arg);
        ++sent;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "Sent to %u node%s.", sent, sent == 1 ? "" : "s");
    say(t, tl, Color::LightGreen, buf);
}

// ---------------------------------------------------------------------------
// cmdSnoop: SNOOP n. Raw output mirror, so terminal types must match.
// ---------------------------------------------------------------------------
void Bbs::cmdSnoop(Session& s, const char* arg) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    Session* o = nodeByArg(arg, nullptr);
    if (!o || o == &s || o->st == SState::Free || o->st == SState::Closing ||
        (!outranks(s, *o) && o->level != Access::None)) {
        say(t, tl, Color::LightRed, "No caller on that node.");
        prompt(s);
        return;
    }
    if (o->snooper && o->snooper != &s) {
        say(t, tl, Color::LightRed, "That node is already being watched.");
        prompt(s);
        return;
    }
    if (o->term.type() != t.type() || o->term.charset() != t.charset() ||
        o->term.iacEscape() != t.iacEscape()) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Node %c is %s, you are %s.", nodeChar(*o), o->term.name(), t.name());
        say(t, tl, Color::LightRed, buf);
        prompt(s);
        return;
    }
    o->snooper = &s;
    s.ed = LineEditor();
    s.st = SState::Snoop;

    char buf[40];
    snprintf(buf, sizeof(buf), "Snooping node %c. Q stops.", nodeChar(*o));
    say(t, tl, Color::Yellow, buf);
    t.reset(tl);
    t.nl(tl);
    plat::log("bbs: sysop snooping node %c", nodeChar(*o));
}

// ---------------------------------------------------------------------------
// stopSnoop: detach from every target and return to the prompt
// ---------------------------------------------------------------------------
void Bbs::stopSnoop(Session& s, const char* why) {
    for (Session* o : all_) if (o->snooper == &s) o->snooper = nullptr;
    Term& t = s.term;
    t.reset(s.tl);
    t.nl(s.tl);
    if (why) {
        say(t, s.tl, Color::LightRed, why);
        t.nl(s.tl);
    }
    say(t, s.tl, Color::Yellow, "Snoop ended.");
    prompt(s);
}

// ---------------------------------------------------------------------------
// cmdTimeAdjust: TIME n +m / -m / m
// ---------------------------------------------------------------------------
void Bbs::cmdTimeAdjust(Session& s, const char* arg) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    const char* rest = nullptr;
    Session* o = nodeByArg(arg, &rest);
    if (!o || !rest || !*rest) {
        say(t, tl, Color::LightRed, "Usage: TIME n +minutes");
        return;
    }
    if (o->role != Role::Caller || !o->loggedIn) {
        say(t, tl, Color::LightRed, "No logged-in caller on that node.");
        return;
    }
    char* end = nullptr;
    long v = strtol(rest, &end, 10);
    if (end == rest) {
        say(t, tl, Color::LightRed, "Usage: TIME n +minutes");
        return;
    }
    long adj = static_cast<long>(o->timeAdjMin) + v;
    if (adj > 1440)  adj = 1440;
    if (adj < -1440) adj = -1440;
    o->timeAdjMin = static_cast<int16_t>(adj);
    o->timeWarned = 0;

    char buf[48];
    int32_t sec = secondsLeft(*o, plat::millis());
    if (sec == INT32_MAX) snprintf(buf, sizeof(buf), "Node %c has no limit.", nodeChar(*o));
    else                  snprintf(buf, sizeof(buf), "Node %c: %ld min left.", nodeChar(*o),
                                   static_cast<long>(sec > 0 ? (sec + 59) / 60 : 0));
    say(t, tl, Color::LightGreen, buf);
}

void Bbs::cmdUnban(Session& s, const char* arg) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    uint32_t ip = 0;
    if (!ipFromText(arg, ip)) {
        say(t, tl, Color::LightRed, "Usage: UNBAN a.b.c.d");
        return;
    }
    if (bans_.clear(ip)) plat::log("bbs: sysop unbanned %s", arg);
    say(t, tl, Color::LightGreen, "Ban cleared.");
}

// ---------------------------------------------------------------------------
// cmdDrop: co-sysop gives up staff access in place; the sysop leaves the
// sysop node for a free caller node. Time limits apply again from now.
// ---------------------------------------------------------------------------
void Bbs::cmdDrop(Session& s, uint32_t now) {
    for (Session* o : all_) if (o->snooper == &s) o->snooper = nullptr;

    if (s.role != Role::Sysop) {
        plat::log("bbs: node %u drops %s (%s)", s.id, syscfg::levelName(s.level), s.user);
        s.level      = Access::None;
        s.perms      = 0;
        s.visible    = true;
        s.lurk       = false;
        s.dnd        = false;
        s.loginAt    = now;
        s.loginEpoch = clk::epoch();
        s.dayUsedMin = dayMinutesUsed(s.user, now);
        s.timeAdjMin = 0;
        s.timeWarned = 0;
        s.lastInput  = now;
        say(s.term, s.tl, Color::LightGreen, "Staff access dropped.");
        prompt(s);
        return;
    }

    Session* free = nullptr;
    for (auto& n : nodes_) {
        if (n.st == SState::Free) { free = &n; break; }
    }
    if (!free) {
        say(s.term, s.tl, Color::LightRed, "No free caller node.");
        prompt(s);
        return;
    }
    uint8_t id = free->id;
    moveSession(s, *free, id, Role::Caller);

    Session& d = *free;
    d.level      = Access::None;
    d.perms      = 0;
    d.visible    = true;
    d.lurk       = false;
    d.dnd        = false;
    d.loginAt    = now;
    d.loginEpoch = clk::epoch();
    d.dayUsedMin = dayMinutesUsed(d.user, now);
    d.timeAdjMin = 0;
    d.timeWarned = 0;
    d.lastInput  = now;

    char buf[32];
    snprintf(buf, sizeof(buf), "Back on node %u.", id);
    say(d.term, d.tl, Color::LightGreen, buf);
    plat::log("bbs: sysop node -> node %u (%s)", id, d.user);
    prompt(d);
}

// ===========================================================================
// CONFIG: the settings, in the same forms the user manager uses
// ===========================================================================

namespace {

// Field kinds. The form widget only knows about text, masks and cycles;
// the kind is what CONFIG checks before anything reaches the file.
enum : uint8_t { CK_TEXT, CK_NUM, CK_YESNO, CK_LEVEL, CK_PASS };

struct CfgField {
    const char* key;      // key in system.cfg
    const char* label;    // 9 characters, the form column
    uint8_t     kind;
    uint16_t    lo;       // CK_NUM: the range the parser will accept
    uint16_t    hi;
    uint8_t     cap;      // characters, excluding the terminator
};

constexpr const char kYesNo[]  = "yes|no";
constexpr const char kLevels[] = "all|users|staff|co2|co1|sysop";
constexpr const char kMasked[] = "********";       // shown for a password already set

const CfgField kBoard[] = {
    { "hostname",          "Hostname", CK_TEXT, 0, 0, 31 },
    { "tz",                "Timezone", CK_TEXT, 0, 0, 40 },
    { "ntp_server",        "NTP",      CK_TEXT, 0, 0, 40 },
    { "idle_minutes",      "Idle min", CK_NUM,  1, 240, 4 },
    { "activity_led_gpio", "LED gpio", CK_NUM,  0, 39, 2 },
};

const CfgField kLimits[] = {
    { "call_minutes",    "Per call", CK_NUM, 1, 1440, 4 },
    { "day_minutes",     "Per day",  CK_NUM, 1, 1440, 4 },
    { "who_refresh_min", "WHO min",  CK_NUM, 1, 60, 3 },
    { "who_refresh_max", "WHO max",  CK_NUM, 1, 240, 3 },
    { "max_users",       "Accounts", CK_NUM, 1, 250, 3 },
};

const CfgField kAccounts[] = {
    { "self_register", "Sign-ups", CK_YESNO, 0, 0, 4 },
    { "guest",         "Guests",   CK_YESNO, 0, 0, 4 },
    { "guest_minutes", "Guest mn", CK_NUM,   1, 240, 4 },
};

const CfgField kBackup[] = {
    { "backup_port",           "Port",     CK_NUM, 1, 65535, 5 },
    { "backup_window_minutes", "Open for", CK_NUM, 1, 120, 4 },
    { "backup_button_gpio",    "Button",   CK_NUM, 0, 39, 2 },
};

const CfgField kStaff[] = {
    { "sysop_password",    "Sysop",    CK_PASS, 0, 0, 32 },
    { "cosysop1_password", "Co-sysop 1", CK_PASS, 0, 0, 32 },
    { "cosysop2_password", "Co-sysop 2", CK_PASS, 0, 0, 32 },
};

struct CfgPage {
    const char*     name;      // what a caller types after CONFIG
    const char*     title;     // the form's title bar
    const char*     what;      // one line in the page list
    const CfgField* fields;
    uint8_t         count;
};

const CfgPage kPages[] = {
    { "board",    "BOARD",           "name, clock, idle timeout, LED", kBoard, 5 },
    { "limits",   "TIME LIMITS",     "minutes per call and per day",   kLimits, 5 },
    { "accounts", "ACCOUNTS",        "sign-ups and guest calls",       kAccounts, 3 },
    { "backup",   "BACKUP WINDOW",   "port, how long it stays open",   kBackup, 3 },
    { "staff",    "STAFF PASSWORDS", "sysop and co-sysop passwords",   kStaff, 3 },
};
constexpr uint8_t kPageCount = sizeof(kPages) / sizeof(kPages[0]);

// One settings editor at a time. The sysop is a single caller, and two
// people writing the file at once is a good way to lose it.
const Session*  g_cfgOwner = nullptr;
const CfgPage*  g_cfgPage  = nullptr;
char            g_cfgSection[24] = "";                  // plugin section, empty for the board
char            g_cfgBuf[Form::kMaxFields][48] = {};
char            g_cfgWas[Form::kMaxFields][48] = {};     // to write only what changed
char            g_cfgKeys[Form::kMaxFields][24] = {};   // plugin pages build their keys here
CfgField        g_cfgPlugin[Form::kMaxFields] = {};     // and their field table
CfgPage         g_cfgPluginPage = {};

// cfgFileValue: what the file says this key is, empty when it says nothing
bool cfgFileValue(const char* section, const char* key, char* out, size_t n) {
    char path[160], line[192], want[40];
    out[0] = '\0';
    snprintf(path, sizeof(path), "%s/system.cfg", plat::fsBase());
    FILE* f = fopen(path, "r");
    if (!f) return false;
    bool inSection = section == nullptr || !*section;
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '[') {
            char name[40];
            size_t w = 0;
            ++p;
            while (*p && *p != ']' && w + 1 < sizeof(name)) name[w++] = *p++;
            name[w] = '\0';
            inSection = section && *section && !strcasecmp(name, section);
            continue;
        }
        if (!inSection || *p == '#' || *p == ';' || *p == '\n' || !*p) continue;
        size_t klen = 0;
        while (p[klen] && p[klen] != '=' && p[klen] != ' ' && p[klen] != '\t') ++klen;
        if (klen + 1 >= sizeof(want)) continue;
        memcpy(want, p, klen);
        want[klen] = '\0';
        if (strcasecmp(want, key)) continue;
        const char* v = p + klen;
        while (*v == ' ' || *v == '\t') ++v;
        if (*v != '=') continue;
        ++v;
        while (*v == ' ' || *v == '\t') ++v;
        size_t w = 0;
        while (*v && *v != '\n' && *v != '\r' && w + 1 < n) out[w++] = *v++;
        while (w && out[w - 1] == ' ') --w;
        out[w] = '\0';
        found = true;                                   // a later line wins, as the parser does
    }
    fclose(f);
    return found;
}

// cfgLiveValue: what the board is actually running with, for a key the
// file does not mention. Keeps the form honest about defaults.
void cfgLiveValue(const char* key, char* out, size_t n) {
    const SysConfig& c = syscfg::get();
    if      (!strcmp(key, "hostname"))              snprintf(out, n, "%.*s", static_cast<int>(n) - 1, c.hostname);
    else if (!strcmp(key, "tz"))                    snprintf(out, n, "%.*s", static_cast<int>(n) - 1, c.tz);
    else if (!strcmp(key, "ntp_server"))            snprintf(out, n, "%.*s", static_cast<int>(n) - 1, c.ntpServer);
    else if (!strcmp(key, "idle_minutes"))          snprintf(out, n, "%u", c.idleMinutes);
    else if (!strcmp(key, "activity_led_gpio"))     snprintf(out, n, "%d", c.ledGpio);
    else if (!strcmp(key, "call_minutes"))          snprintf(out, n, "%u", c.callMinutes);
    else if (!strcmp(key, "day_minutes"))           snprintf(out, n, "%u", c.dayMinutes);
    else if (!strcmp(key, "who_refresh_min"))       snprintf(out, n, "%u", c.whoMin);
    else if (!strcmp(key, "who_refresh_max"))       snprintf(out, n, "%u", c.whoMax);
    else if (!strcmp(key, "max_users"))             snprintf(out, n, "%u", c.maxUsers);
    else if (!strcmp(key, "self_register"))         snprintf(out, n, "%s", c.selfRegister ? "yes" : "no");
    else if (!strcmp(key, "guest"))                 snprintf(out, n, "%s", c.guestEnabled ? "yes" : "no");
    else if (!strcmp(key, "guest_minutes"))         snprintf(out, n, "%u", c.guestMinutes);
    else if (!strcmp(key, "backup_port"))           snprintf(out, n, "%u", c.backupPort);
    else if (!strcmp(key, "backup_window_minutes")) snprintf(out, n, "%u", c.backupMinutes);
    else if (!strcmp(key, "backup_button_gpio"))    snprintf(out, n, "%d", c.backupGpio);
    else if (!strcmp(key, "sysop_password"))        snprintf(out, n, "%s", c.sysopPass[0] ? kMasked : "");
    else if (!strcmp(key, "cosysop1_password"))     snprintf(out, n, "%s", c.coPass[0][0] ? kMasked : "");
    else if (!strcmp(key, "cosysop2_password"))     snprintf(out, n, "%s", c.coPass[1][0] ? kMasked : "");
    else out[0] = '\0';
}

// cfgPluginValue: what a plugin is running with, for the keys the file
// does not mention. Its own settings have no name here, so they come back
// empty and the sysop sees a blank rather than a wrong value.
void cfgPluginValue(const char* name, const char* key, char* out, size_t n) {
    out[0] = '\0';
    uint8_t i = plugins::indexOf(name);
    if (i == 0xFF) return;
    if      (!strcmp(key, "enabled")) snprintf(out, n, "%s", plugins::enabled(i) ? "yes" : "no");
    else if (!strcmp(key, "read"))    snprintf(out, n, "%s", plugins::levelName(plugins::levelFor(i, 0)));
    else if (!strcmp(key, "write"))   snprintf(out, n, "%s", plugins::levelName(plugins::levelFor(i, 1)));
    else if (!strcmp(key, "admin"))   snprintf(out, n, "%s", plugins::levelName(plugins::levelFor(i, 2)));
}

// pageByName: "board", or a plugin's name
const CfgPage* pageByName(const char* name) {
    for (uint8_t i = 0; i < kPageCount; ++i)
        if (!strcasecmp(kPages[i].name, name)) return &kPages[i];
    return nullptr;
}

// digitsOnly: what CK_NUM accepts before anything is written
bool digitsOnly(const char* v) {
    if (!*v) return false;
    for (const char* p = v; *p; ++p) if (*p < '0' || *p > '9') return false;
    return true;
}

// collectKey: plugin pages are built from the keys the file already has,
// which is every key that matters once system.cfg.example has been used.
struct KeyGrab { uint8_t n; };

void collectKey(void* ctx, const char* key, const char* value) {
    (void)value;
    KeyGrab* g = static_cast<KeyGrab*>(ctx);
    if (g->n >= Form::kMaxFields - 4) return;             // enabled/read/write/admin first
    for (uint8_t i = 0; i < g->n; ++i) if (!strcmp(g_cfgKeys[i], key)) return;
    snprintf(g_cfgKeys[g->n], sizeof(g_cfgKeys[0]), "%.23s", key);
    ++g->n;
}

} // namespace

// ---------------------------------------------------------------------------
// configRelease: give the settings editor back. Called when the form ends
// and whenever a session closes, so a dropped line cannot lock CONFIG out.
// ---------------------------------------------------------------------------
void Bbs::configRelease(const Session& s) {
    if (g_cfgOwner == &s) {
        g_cfgOwner = nullptr;
        g_cfgPage  = nullptr;
    }
}

// ---------------------------------------------------------------------------
// configPages: what CONFIG on its own shows
// ---------------------------------------------------------------------------
void Bbs::configPages(Session& s) {
    char buf[64];
    rowTitle(s, "Settings", "CONFIG page");
    for (uint8_t i = 0; i < kPageCount; ++i) {
        uint8_t col = 0;
        snprintf(buf, sizeof(buf), "%-10.10s", kPages[i].name);
        rowSeg(s, Color::Yellow, buf, col);
        rowSeg(s, Color::Grey, kPages[i].what, col);
        rowEnd(s, col);
    }
    for (uint8_t i = 0; i < plugins::count(); ++i) {
        uint8_t col = 0;
        snprintf(buf, sizeof(buf), "%-10.10s", plugins::at(i)->info.name);
        rowSeg(s, Color::Yellow, buf, col);
        snprintf(buf, sizeof(buf), "plugin: %.24s", plugins::at(i)->info.title);
        rowSeg(s, Color::Grey, buf, col);
        rowEnd(s, col);
    }
    rowRule(s);
    rowText(s, Color::DarkGrey, "F1 saves a page, left arrow leaves it");
    prompt(s);
}

// ---------------------------------------------------------------------------
// cmdConfig: CONFIG lists the pages, CONFIG <page> opens one as a form.
// Editing is one page at a time and one sysop at a time; saving writes only
// that page's keys and reloads the running configuration.
// ---------------------------------------------------------------------------
void Bbs::cmdConfig(Session& s, const char* arg, uint32_t now) {
    char buf[80];
    while (*arg == ' ') ++arg;
    if (!*arg) { configPages(s); return; }

    if (g_cfgOwner && g_cfgOwner != &s) {
        snprintf(buf, sizeof(buf), "%.20s is editing the settings.", g_cfgOwner->user);
        say(s.term, s.tl, Color::LightRed, buf);
        prompt(s);
        return;
    }

    const CfgPage* page = pageByName(arg);
    g_cfgSection[0] = '\0';
    if (!page) {                                          // a plugin's own section
        uint8_t pi = plugins::indexOf(arg);
        if (pi == 0xFF) {
            say(s.term, s.tl, Color::LightRed, "No such page. CONFIG lists them.");
            prompt(s);
            return;
        }
        snprintf(g_cfgSection, sizeof(g_cfgSection), "plugin:%.15s", plugins::at(pi)->info.name);
        KeyGrab grab{ 0 };
        memset(g_cfgKeys, 0, sizeof(g_cfgKeys));
        plugins::forEachKey(pi, collectKey, &grab);

        uint8_t n = 0;
        g_cfgPlugin[n++] = { "enabled", "Enabled", CK_YESNO, 0, 0, 4 };
        g_cfgPlugin[n++] = { "read",    "Read",    CK_LEVEL, 0, 0, 6 };
        g_cfgPlugin[n++] = { "write",   "Write",   CK_LEVEL, 0, 0, 6 };
        g_cfgPlugin[n++] = { "admin",   "Admin",   CK_LEVEL, 0, 0, 6 };
        for (uint8_t i = 0; i < grab.n && n < Form::kMaxFields; ++i)
            g_cfgPlugin[n++] = { g_cfgKeys[i], g_cfgKeys[i], CK_TEXT, 0, 0, 40 };
        g_cfgPluginPage = { plugins::at(pi)->info.name, plugins::at(pi)->info.name,
                            "", g_cfgPlugin, n };
        page = &g_cfgPluginPage;
    }

    g_cfgOwner = &s;
    g_cfgPage  = page;
    uint8_t n = 0;
    for (uint8_t i = 0; i < page->count && i < Form::kMaxFields; ++i) {
        const CfgField& f = page->fields[i];
        char* buf2 = g_cfgBuf[i];
        if (!cfgFileValue(g_cfgSection, f.key, buf2, sizeof(g_cfgBuf[0]))) {
            if (g_cfgSection[0]) cfgPluginValue(arg, f.key, buf2, sizeof(g_cfgBuf[0]));
            else                 cfgLiveValue(f.key, buf2, sizeof(g_cfgBuf[0]));
        }
        if (f.kind == CK_PASS && *buf2) snprintf(buf2, sizeof(g_cfgBuf[0]), "%s", kMasked);
        snprintf(g_cfgWas[i], sizeof(g_cfgWas[0]), "%.47s", buf2);      // what it was when it opened
        uint8_t flags = FF_NONE;
        const char* choices = nullptr;
        if (f.kind == CK_YESNO) { flags |= FF_CYCLE; choices = kYesNo; }
        if (f.kind == CK_LEVEL) { flags |= FF_CYCLE; choices = kLevels; }
        if (f.kind == CK_PASS)  flags |= FF_MASK;
        addField(s, n, f.label, buf2, f.cap, flags, choices);
    }
    s.formKind  = FormKind::Config;
    s.st        = SState::Form;
    s.lastInput = now;
    s.form.begin(page->title, s.fields, n, s.term, s.tl);   // the title outlives the form
}

// ---------------------------------------------------------------------------
// configSave: check every field, then write the page in one pass and reload.
// A password left showing its mask is left alone; an empty one clears it.
// ---------------------------------------------------------------------------
bool Bbs::configSave(Session& s, char* err, size_t errLen) {
    if (!g_cfgPage) { snprintf(err, errLen, "nothing to save"); return false; }
    syscfg::KeyVal pairs[Form::kMaxFields];
    uint8_t n = 0;

    for (uint8_t i = 0; i < g_cfgPage->count && i < Form::kMaxFields; ++i) {
        const CfgField& f = g_cfgPage->fields[i];
        const char* v = g_cfgBuf[i];
        if (f.kind == CK_PASS && !strcmp(v, kMasked)) continue;      // untouched
        if (!strcmp(v, g_cfgWas[i])) continue;                       // nothing to write
        if (!*v && (f.kind == CK_YESNO || f.kind == CK_LEVEL)) continue;
        if (f.kind == CK_NUM) {
            if (!digitsOnly(v)) {
                s.form.fail(i, "Numbers only", s.term, s.tl);
                return false;
            }
            long val = strtol(v, nullptr, 10);
            if (val < f.lo || val > f.hi) {
                char msg[48];
                snprintf(msg, sizeof(msg), "Between %u and %u", static_cast<unsigned>(f.lo),
                         static_cast<unsigned>(f.hi));
                s.form.fail(i, msg, s.term, s.tl);
                return false;
            }
        }
        pairs[n].key   = f.key;
        pairs[n].value = v;
        ++n;
    }
    if (!n) { snprintf(err, errLen, "Nothing changed"); return true; }
    if (!syscfg::write(pairs, n, g_cfgSection[0] ? g_cfgSection : nullptr, err, errLen)) return false;

    char rerr[80] = "";
    if (!syscfg::reload(rerr, sizeof(rerr))) {
        snprintf(err, errLen, "saved, but %.48s", rerr);
        return true;
    }
    // A plugin about to be stopped may have callers inside it. Hand them
    // back to the command prompt first: a session left owning a plugin
    // that has given its memory back is a session that never comes home.
    eachSession([](void*, Session& o) {
        if (o.st == SState::Plugin) {
            o.term.color(o.tl, Color::Yellow);
            o.term.nl(o.tl);
            o.term.text(o.tl, "The sysop changed the settings.");
            o.term.nl(o.tl);
            Bbs::instance().release(o);
        }
    }, nullptr);
    plugins::stopAll();
    plugins::begin(*this);
    snprintf(err, errLen, "Saved and live");
    return true;
}
