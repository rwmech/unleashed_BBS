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
#include "claims.h"
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
    presenceChanged(s);          // the public count just moved
}

void Bbs::cmdLurk(Session& s) {
    s.lurk = !s.lurk;
    if (s.lurk) { s.visible = false; s.dnd = true; }
    else        { s.dnd = false; }
    say(s.term, s.tl, Color::Cyan, s.lurk ? "Lurking: hidden, pages off." : "Lurk off: still hidden, pages on.");
    presenceChanged(s);
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
// rememberStaff / restoreStaff: type the staff password once a week, not
// once a call.
//
// See UserRec for why this is bound to an address and why the sysop is
// excluded. The short version: account passwords cross this board in the
// clear on every login, so remembering staff access against the account
// alone would make a sniffed account password worth a week of staff rights.
// ---------------------------------------------------------------------------
namespace { constexpr uint32_t kStaffWindow = 7u * 24u * 60u * 60u; }   // one week

void Bbs::rememberStaff(const Session& s, Access level) {
    if (s.guest || !s.user[0]) return;
    if (level == Access::Sysop || level == Access::None) return;   // never the sysop
    if (!clk::valid()) return;                    // no clock, nothing to date it by
    static UserRec u;
    if (!users::find(s.user, u)) return;
    u.staffAt    = clk::epoch();
    u.staffLevel = static_cast<uint8_t>(level);
    snprintf(u.staffIp, sizeof(u.staffIp), "%s", s.ip);
    users::update(u.handle, u);
}

void Bbs::restoreStaff(Session& s) {
    if (s.guest || !s.user[0] || s.role != Role::Caller) return;
    if (s.level != Access::None) return;           // already elevated this call

    // No clock means no way to tell whether the week has elapsed, so it
    // fails closed and asks for the password. Guessing "probably still
    // valid" on a board whose clock has not synced is how a window becomes
    // permanent.
    if (!clk::valid()) return;

    static UserRec u;
    if (!users::find(s.user, u)) return;
    if (!u.staffLevel || !u.staffAt || !u.staffIp[0]) return;

    uint32_t nowEpoch = clk::epoch();
    if (nowEpoch < u.staffAt || nowEpoch - u.staffAt > kStaffWindow) return;  // expired
    if (strcmp(u.staffIp, s.ip) != 0) return;      // somewhere else: type it again

    Access lv = static_cast<Access>(u.staffLevel);
    if (lv == Access::Sysop || lv == Access::None) return;
    s.level = lv;
    s.perms = syscfg::permsFor(lv);
    char buf[72];
    snprintf(buf, sizeof(buf), "%s access, remembered from %s.", syscfg::levelName(lv), s.ip);
    say(s.term, s.tl, Color::Cyan, buf);
    s.term.nl(s.tl);
    plat::log("bbs: node %s %s access restored for %s from %s",
              nodeName(s).t, syscfg::levelName(lv), s.user, s.ip);
}

// ---------------------------------------------------------------------------
// localAddr: is this caller on the same network as the board?
//
// Text comparison on the dotted quad, because that is the form the session
// carries and parsing it into an integer to compare ranges would be more
// code for the same answer. IPv6 link-local and loopback are included for
// completeness; a board reached over IPv6 from the LAN normally arrives on
// a ULA or a global address, so this is deliberately conservative and will
// say "not local" rather than guess.
// ---------------------------------------------------------------------------
bool Bbs::localAddr(const char* ip) {
    if (!ip || !*ip) return false;
    if (!strcmp(ip, "127.0.0.1") || !strcmp(ip, "::1")) return true;
    if (!strncmp(ip, "10.", 3))       return true;
    if (!strncmp(ip, "192.168.", 8))  return true;
    if (!strncmp(ip, "169.254.", 8))  return true;      // link local
    if (!strncmp(ip, "fe80:", 5) || !strncmp(ip, "FE80:", 5)) return true;
    if (!strncmp(ip, "172.", 4)) {                      // 172.16 .. 172.31
        int n = atoi(ip + 4);
        if (n >= 16 && n <= 31) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// elevate: move a caller (or busy-line guest) onto the sysop node
//
// The sysop node holds one caller, and it has to: it is a single hidden
// Session. A second sysop used to be hung up on, which is safe and is also
// useless to the person running the board, who wants a dashboard open in one
// window and somewhere to work in another.
//
// So when the node is taken and the caller is on the local network, they get
// sysop rights in place, on their own caller line, exactly the way a
// co-sysop does. The line stays visible in WHO, which is right: the hidden
// node is a property of that one node, not of being the sysop, and a second
// sysop session that is invisible to everybody would be worse.
//
// Local only, on purpose. Somebody arriving from the internet with the
// password still gets the old behaviour, so the board never has two sysop
// sessions open to the outside world at once.
// ---------------------------------------------------------------------------
void Bbs::elevate(Session& s, uint32_t now) {
    if (sysop_.st != SState::Free) {
        if (s.role == Role::Caller && localAddr(s.ip)) {
            plat::log("bbs: sysop node in use, node %s takes sysop in place (%s)",
                      nodeName(s).t, s.ip);
            coElevate(s, Access::Sysop, now);
            return;
        }
        plat::log("bbs: sysop node in use, node %s logs off instead", nodeName(s).t);
        goodbye(s, now);
        return;
    }
    NodeStr from = nodeName(s);
    bool fromNode = s.role == Role::Caller;

    moveSession(s, sysop_, 0, Role::Sysop);

    Session& d = sysop_;
    d.level      = Access::Sysop;
    d.perms      = syscfg::permsFor(Access::Sysop);
    // Visible, not lurking. The common case is the sysop being around, so
    // being seen should be the default and disappearing should be the thing
    // you ask for. Starting hidden meant a board could have its operator on
    // it and look empty to every caller, which is the opposite of what a
    // sysop node is for.
    d.visible    = true;
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
    t.text(tl, "You've been switched to the SysOp node.");
    t.nl(tl);
    t.color(tl, Color::Grey);
    if (fromNode) {
        snprintf(buf, sizeof(buf), "Node %s is free for callers.", from.t);
        t.text(tl, buf);
        t.nl(tl);
    }
    t.text(tl, "Shown in WHO. LURK makes you invisible.");
    t.nl(tl);
    t.text(tl, "HELP for commands.");
    t.nl(tl);

    plat::log("bbs: node %s -> sysop node (%s, %s)", from.t, d.user, d.ip);
    // The sysop arrives visible now, so the public caller count just moved
    // and the directory should hear about it. Without this a board would
    // advertise one fewer caller than it has until the next login.
    presenceChanged(d);
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
    rememberStaff(s, level);        // so it need not be typed again this week

    Term& t = s.term;
    Timeline& tl = s.tl;
    char buf[48];
    snprintf(buf, sizeof(buf), "%s access on node %u.", syscfg::levelName(level), s.id);
    say(t, tl, Color::LightGreen, buf);
    t.nl(tl);
    if (bootWasCrash()) {
        // Said to staff only, and said plainly. A board that restarted on
        // its own has lost every caller who was on it.
        char why[64];
        snprintf(why, sizeof(why), "Last restart was not clean: %s.", bootReason());
        say(t, tl, Color::LightRed, why);
        say(t, tl, Color::Grey, "SYS has the detail. The log is reboots.log.");
        t.nl(tl);
    }
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
    const char* fmt = wide ? "%s%c%-20.20s %-15.15s %-9.9s %4s %5s" : "%s%c%-9.9s %-15.15s %4s %5s";
    char buf[96];
    char left[12];
    char idle[8];
    uint32_t now = plat::millis();
    // A refresh screen redraws from home, so the frame has to be the same
    // height every time or the tail of a taller frame is left on the screen.
    // The sysop and busy lines are skipped when free, which makes the height
    // vary, so while refreshing they become blank rows instead. This is the
    // same trap DASH hit when it grew a row per node.
    bool refresh = s.watch != ListKind::None;

    for (;;) {
        uint8_t i = s.listIdx++;
        if (i == 0) {
            char count[16];
            snprintf(count, sizeof(count), "%u of %u", activeNodes(), BBS_MAX_NODES);
            rowTitle(s, "Nodes", count);
            return true;
        }
        if (i == 1) {
            if (wide) snprintf(buf, sizeof(buf), fmt, " N", ' ', "Handle", "IP", "Terminal", "Left", "Idle");
            else      snprintf(buf, sizeof(buf), fmt, " N", ' ', "Handle", "IP", "Left", "Idle");
            rowText(s, Color::LightBlue, buf);
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
        if (o->role != Role::Caller && o->st == SState::Free) {
            if (refresh) { rowText(s, Color::DarkGrey, ""); return true; }  // keep the height
            continue;
        }

        if (o->st == SState::Free) {
            snprintf(buf, sizeof(buf), "%s -", nodeLabel(*o).t);
            rowText(s, Color::DarkGrey, buf);
            return true;
        }

        char h[24];
        if (o->user[0]) listHandle(h, sizeof(h), o->user, wide ? 20 : 9);
        else            snprintf(h, sizeof(h), "%s", preLoginName(*o));
        bool hidden = o != &s && (!o->visible || o->lurk);
        if (o->role == Role::Caller && o->loggedIn && !unlimited(*o)) {
            int32_t sec = secondsLeft(*o, now);
            if (sec == INT32_MAX) snprintf(left, sizeof(left), "--");
            else                  snprintf(left, sizeof(left), "%ld", static_cast<long>((sec + 59) / 60));
        } else {
            snprintf(left, sizeof(left), "--");
        }
        fmtIdle(idle, sizeof(idle), now - o->lastInput);
        if (wide) snprintf(buf, sizeof(buf), fmt, nodeLabel(*o).t, markFor(*o), h, o->ip, o->term.name(), left, idle);
        else      snprintf(buf, sizeof(buf), fmt, nodeLabel(*o).t, markFor(*o), h, o->ip, left, idle);
        rowText(s, o == &s ? Color::White : (hidden ? Color::DarkGrey : Color::Grey), buf);
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
    NodeStr c = nodeName(*o);
    plat::log("bbs: sysop kicked node %s (%s, %s)", c.t, o->user, o->ip);
    hangup(*o, text, now);

    char buf[32];
    snprintf(buf, sizeof(buf), "Node %s disconnected.", c.t);
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
        snprintf(buf, sizeof(buf), "Node %s is %s, you are %s.", nodeName(*o).t, o->term.name(), t.name());
        say(t, tl, Color::LightRed, buf);
        prompt(s);
        return;
    }
    o->snooper = &s;
    s.ed = LineEditor();
    s.st = SState::Snoop;

    char buf[40];
    snprintf(buf, sizeof(buf), "Snooping node %s. Q stops.", nodeName(*o).t);
    say(t, tl, Color::Yellow, buf);
    t.reset(tl);
    t.nl(tl);
    plat::log("bbs: sysop snooping node %s", nodeName(*o).t);
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
// offClock: the words that mean "stop counting". "-1" is the one Rob asked
// for, and it costs the ability to take a single minute off a caller, which
// nobody has ever wanted to do.
static bool offClock(const char* v) {
    while (*v == ' ') ++v;
    return !strcmp(v, "-1") || ieq(v, "off") || ieq(v, "none") ||
           ieq(v, "unlimited") || ieq(v, "nolimit");
}

void Bbs::cmdTimeAdjust(Session& s, const char* arg) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    char buf2[64];

    // The permission is checked HERE, not only by the caller. The shell's
    // TIME checked PERM_TIME before calling this; the room's /t called it
    // directly, and bbs.h said this function "does its own permission
    // check" when it did not. So from 0.21.4 any caller in the room could
    // type /t -1 and be off the clock, or /t 3 +600 and give a node ten
    // hours. A check that lives with the thing it guards cannot be skipped
    // by the next door somebody adds to it.
    if (!can(s, PERM_TIME)) {
        say(t, tl, Color::LightRed, "Only staff can change a caller's time.");
        return;
    }

    // TIME -1 with no node is about this line, which is how a sysop settling
    // in for a long evening will type it.
    if (offClock(arg)) {
        s.noLimits  = true;
        s.timeWarned = 0;
        snprintf(buf2, sizeof(buf2), "Node %s is off the clock: no limit, no idle hangup.",
                 nodeName(s).t);
        say(t, tl, Color::LightGreen, buf2);
        return;
    }

    const char* rest = nullptr;
    Session* o = nodeByArg(arg, &rest);
    if (!o || !rest || !*rest) {
        say(t, tl, Color::LightRed, "Usage: TIME n +minutes, or TIME n -1 for no limit");
        return;
    }
    if (o->role != Role::Caller || !o->loggedIn) {
        say(t, tl, Color::LightRed, "No logged-in caller on that node.");
        return;
    }
    // TIME n -1: that node stops being on the clock at all, until it hangs
    // up. It is per call on purpose, so nobody is quietly unlimited for ever
    // because of something typed one evening months ago.
    if (offClock(rest)) {
        o->noLimits   = true;
        o->timeWarned = 0;
        snprintf(buf2, sizeof(buf2), "Node %s is off the clock: no limit, no idle hangup.",
                 nodeName(*o).t);
        say(t, tl, Color::LightGreen, buf2);
        return;
    }

    char* end = nullptr;
    long v = strtol(rest, &end, 10);
    if (end == rest) {
        say(t, tl, Color::LightRed, "Usage: TIME n +minutes, or TIME n -1 for no limit");
        return;
    }
    o->noLimits = false;                      // back on the clock if it was off
    long adj = static_cast<long>(o->timeAdjMin) + v;
    if (adj > 1440)  adj = 1440;
    if (adj < -1440) adj = -1440;
    o->timeAdjMin = static_cast<int16_t>(adj);
    o->timeWarned = 0;

    char buf[48];
    int32_t sec = secondsLeft(*o, plat::millis());
    if (sec == INT32_MAX) snprintf(buf, sizeof(buf), "Node %s has no limit.", nodeName(*o).t);
    else                  snprintf(buf, sizeof(buf), "Node %s: %ld min left.", nodeName(*o).t,
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
// CK_SUB is the odd one: not a value at all, a button that opens a page.
enum : uint8_t { CK_TEXT, CK_NUM, CK_YESNO, CK_LEVEL, CK_PASS, CK_SUB, CK_INFO };

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
    { "board_name",        "Board",    CK_TEXT, 0, 0, 40 },
    { "hostname",          "Hostname", CK_TEXT, 0, 0, 31 },
    { "tz",                "Timezone", CK_TEXT, 0, 0, 40 },
    { "ntp_server",        "NTP",      CK_TEXT, 0, 0, 40 },
    { "idle_minutes",      "Idle min", CK_NUM,  1, 240, 4 },
    { "activity_led_gpio", "LED gpio", CK_NUM,  0, 39, 2 },
    // Where a caller goes when their own account has not said. An account
    // set to anything other than Default overrides this, so changing it
    // moves exactly the people who never expressed a preference.
    { "landing",           "Land on",  CK_TEXT, 0, 0, 8 },
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

// The network. Used from the next restart, never live: changing it under a
// telnet session drops the sysop who changed it, and a typo would take the
// board off the network with nobody left on it to put it back. The cable
// and Improv are the way to fix a board that cannot reach its network.
const CfgField kWifi[] = {
    { "wifi_ssid",     "Network",  CK_TEXT, 0, 0, 32 },
    { "wifi_password", "Password", CK_PASS, 0, 0, 64 },
};

struct CfgPage {
    const char*     name;      // what a caller types after CONFIG
    const char*     title;     // the form's title bar
    const char*     what;      // one line in the page list
    const CfgField* fields;
    uint8_t         count;
};

// CFG_PAGE: the field count comes from the array, never from a number
// typed next to it.
//
// This was five hand-written counts, and kBoard's said 5 while the table
// held 7. The last two rows simply did not exist as far as CONFIG was
// concerned: activity_led_gpio had been unreachable from the board since
// the day it was added, and the only symptom was a field nobody could
// find. Nothing warns, because a short count is a perfectly valid way to
// describe a shorter page.
#define CFG_PAGE(name, title, what, arr) \
    { name, title, what, arr, static_cast<uint8_t>(sizeof(arr) / sizeof((arr)[0])) }

const CfgPage kPages[] = {
    CFG_PAGE("board",    "BOARD",           "name, clock, idle, LED, landing", kBoard),
    CFG_PAGE("limits",   "TIME LIMITS",     "minutes per call and per day",    kLimits),
    CFG_PAGE("accounts", "ACCOUNTS",        "sign-ups and guest calls",        kAccounts),
    CFG_PAGE("backup",   "BACKUP WINDOW",   "port, how long it stays open",    kBackup),
    CFG_PAGE("staff",    "STAFF PASSWORDS", "sysop and co-sysop passwords",    kStaff),
    CFG_PAGE("wifi",     "WI-FI",           "network, from the next restart",  kWifi),
};
constexpr uint8_t kPageCount = sizeof(kPages) / sizeof(kPages[0]);

// ---------------------------------------------------------------------------
// Composite settings: one key whose value is several values packed with
// bars, the way a file area is "path | name | read | write".
//
// The packed form stays in the file on purpose: it is what a sysop editing
// system.cfg on a laptop reads and what the plugin's own parser expects. It
// is a terrible thing to put in a text box, though, because the format is
// invisible unless you already know it, and there is no way to set one part
// without retyping the rest. So CONFIG draws the row as a button showing a
// summary and opens the parts as a page of their own.
//
// Two conventions a composite follows, both worth stating because they are
// what keeps this table one line per plugin rather than a special case:
//   - part 0 is what the record points at and part 1 is its human name, so
//     the button's summary is the name, or the path when it has none.
//   - each CK_LEVEL part names what it falls back to when the record does
//     not set it, so a part the file does not carry shows the level the
//     record is actually running under rather than a blank or a guess.
//
// That second one used to be "the level parts, in order, are read, write
// and admin", which held for two parts and not for four. A forum's third
// level part is Reply, which was seeded with the plugin's ADMIN level, and
// its fourth got "nobody"; Save pinned both. Rob, after saving a forum:
// "oddly it says I cant post in there but I can post I just cant reply".
// Each plugin decides its own fallbacks, so the table has to say them.
// ---------------------------------------------------------------------------
enum : uint8_t {
    IN_READ  = 0,         // the plugin's read level
    IN_WRITE = 1,         // the plugin's write level
    IN_ADMIN = 2,         // the plugin's admin level
    IN_PART  = 0x10,      // IN_PART | k: whatever part k ends up as
};

struct CfgPart {
    const char* label;    // 9 characters, the form column
    uint8_t     kind;     // CK_TEXT or CK_LEVEL
    uint8_t     cap;
    uint8_t     inherit;  // CK_LEVEL: what an unset part runs under
};

// The order here is the order on the wire, and Upload sits above Download
// because it is where the old single "Write" level lived. Reordering them
// to read nicely would silently reinterpret every area already configured,
// turning its upload level into its download level.
constexpr CfgPart kAreaParts[] = {
    { "Path",     CK_TEXT,  48, 0 },   // files.cpp kPathMax
    { "Name",     CK_TEXT,  24, 0 },   // files.cpp kNameMax
    // The fallbacks are files.cpp's mayRead, mayUp, mayDown and mayDel.
    { "Read",     CK_LEVEL,  6, IN_READ },      // see the area and list it
    { "Upload",   CK_LEVEL,  6, IN_WRITE },     // put files in, and describe them
    { "Download", CK_LEVEL,  6, IN_PART | 2 },  // take files out; the area's Read
    { "Delete",   CK_LEVEL,  6, IN_ADMIN },     // remove files, approve or reject
};

struct CfgComposite {
    const char*    section;   // the config section these keys live in
    const char*    prefix;    // keys starting with this are records
    const char*    title;     // the sub-page's title bar
    const char*    what;      // one word for the status line
    const CfgPart* parts;
    uint8_t        count;
    // Which part the button shows. A file area and a forum put their name
    // second, after the path or key; an information page has no key and
    // its title comes first, so a fixed "part 1" would have labelled every
    // page's button with its read level.
    uint8_t        namePart;
};

// The count MUST come from the table, not be written out again beside it.
// It was 4 against a six-entry kAreaParts, and the consequence was not a
// cosmetic one: the sub-page showed Path, Name, Read and Upload, and
// configSubSave packs exactly comp->count parts over whatever was in the
// file, so saving any area silently dropped its Download and Delete levels.
// files::mayDown then falls back to the area's READ level, which is
// documented and sensible on its own terms and means a staff-only download
// area quietly became downloadable by everybody. The board said "Saved and
// live" both times.
//
// Same shape as the CONFIG Board page rendering 5 of 7 fields: a count
// written next to a table instead of derived from it. Deriving it is the fix
// in both places, because then adding a part cannot be half done.
#define CFG_PARTS(t) (t), (sizeof(t) / sizeof((t)[0]))

// A forum's seven parts. The order here IS the wire format, and it is the
// order src/plugins/forums.cpp readKey() parses, checked against that
// function rather than against anybody's memory of it. The file areas
// shipped with the documentation naming a different order than the parser
// used, which would have had a sysop set the download level where the upload
// level goes, so this is written down once and verified in one place.
//
//   topic1 = key | name | about | read | start | reply | mod
//
// `start` and `reply` are separate on purpose, and the split is what makes a
// read-only announcements forum work BETTER than read-only:
// read=all, start=co1, reply=users is "staff post the news, anybody may
// answer it", which boards wanted and could not say.
constexpr CfgPart kTopicParts[] = {
    { "Key",      CK_TEXT,  12, 0 },   // folder on the card, forums.cpp kKeyMax
    { "Name",     CK_TEXT,  24, 0 },   // forums.cpp kNameMax
    { "About",    CK_TEXT,  40, 0 },   // one line, shown at 64 columns and up
    // The fallbacks are forums.cpp's mayRead, mayStart, mayReply and mayMod.
    { "Read",     CK_LEVEL,  6, IN_READ },      // see the forum in the list
    { "Start",    CK_LEVEL,  6, IN_PART | 5 },  // open a NEW subject; this forum's Reply
    { "Reply",    CK_LEVEL,  6, IN_WRITE },     // add to an existing one
    { "Moderate", CK_LEVEL,  6, IN_ADMIN },     // delete, pin, move
};

// An information page: "page3 = House rules | all". The text is written
// with INFO 3 EDIT; only what it is called and who may read it live here.
constexpr CfgPart kInfoParts[] = {
    // The fallback is info.cpp's mayRead.
    { "Title", CK_TEXT,  24, 0 },               // info.cpp kTitleMax
    { "Read",  CK_LEVEL,  6, IN_READ },         // who sees it in the list
};

const CfgComposite kComposites[] = {
    { "plugin:files",  "area",  "FILE AREA", "area",  CFG_PARTS(kAreaParts),  1 },
    { "plugin:forums", "topic", "FORUM",     "forum", CFG_PARTS(kTopicParts), 1 },
    { "plugin:info",   "page",  "INFO PAGE", "page",  CFG_PARTS(kInfoParts),  0 },
};
constexpr uint8_t kCompositeCount = sizeof(kComposites) / sizeof(kComposites[0]);

// Room for the widest composite, not for the one that happened to be first.
// Forums want seven parts, so this is sized for them rather than raised
// again when they land. 8 x 64 is 512 bytes of static RAM, up from 256.
constexpr uint8_t kMaxParts       = 8;

// And it is checked, because a bound written beside a table is the single
// shape that has shipped here most often: the CONFIG Board page showing 5 of
// 7 fields, max_users at 100, and kAreaParts registered with a count of 4,
// which silently dropped Download and Delete off every file area a sysop
// saved. Three of those were live.
//
// The loops in configSub* all read `i < comp->count && i < kMaxParts`, so an
// eighth part would not overflow anything, it would be quietly discarded on
// save: exactly the file-area bug again, and just as invisible. These fail
// the build instead, at the moment the part is added rather than the moment
// somebody notices a permission went missing.
static_assert(sizeof(kAreaParts)  / sizeof(kAreaParts[0])  <= kMaxParts,
              "kAreaParts has more parts than kMaxParts holds: raise kMaxParts");
static_assert(sizeof(kTopicParts) / sizeof(kTopicParts[0]) <= kMaxParts,
              "kTopicParts has more parts than kMaxParts holds: raise kMaxParts");
static_assert(sizeof(kInfoParts) / sizeof(kInfoParts[0]) <= kMaxParts,
              "kInfoParts has more parts than kMaxParts holds: raise kMaxParts");

// IN_PART | k is an index written beside a table, the shape that has cost
// this file three bugs already. Pin each one to the part it means, so a
// reordered table fails here rather than at a sysop's Save.
constexpr bool sameLabel(const char* a, const char* b) {
    return *a == *b && (*a == '\0' || sameLabel(a + 1, b + 1));
}
static_assert(sameLabel(kAreaParts[2].label, "Read"),
              "an area's Download inherits IN_PART | 2, which must be Read");
static_assert(sameLabel(kTopicParts[5].label, "Reply"),
              "a forum's Start inherits IN_PART | 5, which must be Reply");

// One settings editor at a time. The sysop is a single caller, and two
// people writing the file at once is a good way to lose it.
//
// The truth is claims::Res::Config, keyed by node. This pointer survives
// only so the refusal can name who is in there, and it is never branched on:
// a Session* from a static pool is exactly the thing that goes stale when a
// caller drops, which is why the ownership itself is a node id now and is
// cleared from openSession as well as closeSession.
const Session*  g_cfgOwner = nullptr;
const CfgPage*  g_cfgPage  = nullptr;
char            g_cfgSection[24] = "";                  // plugin section, empty for the board
char            g_cfgBuf[Form::kMaxFields][96] = {};
// What each field held when the page opened, as a hash. The text itself was
// never read back: its only use is a strcmp deciding whether to write the
// field, which is an equality question.
//
// This also permanently removes a bug that was live once. The old table held
// a TRUNCATED copy (it was filled with "%.47s" into 96 bytes), so any value
// longer than 47 characters always compared unequal to itself and was
// rewritten on every save whether or not it had been touched. A hash is
// taken over the whole string, so there is no length left to get wrong.
uint32_t        g_cfgWas[Form::kMaxFields] = {};
char            g_cfgSum[Form::kMaxFields][28] = {};     // CK_SUB: the button's summary
char            g_cfgKeys[Form::kMaxFields][24] = {};   // plugin pages build their keys here
CfgField        g_cfgPlugin[Form::kMaxFields] = {};     // and their field table
CfgPage         g_cfgPluginPage = {};

// The one open sub-page. Null means "not nested", and configRelease clears
// it with the rest, so a caller who drops the line inside a sub-page leaves
// nothing behind: the guard, the page and the nesting all go together.
const CfgComposite* g_subComp  = nullptr;
uint8_t             g_subField = 0;                      // row on the parent page
char                g_subKey[24]  = "";                  // "area3"
char                g_subOrig[96] = "";                  // packed value as it opened
char                g_subBuf[kMaxParts][64] = {};        // the parts, edited in place

// cfgFileValue: what the file says this key is, empty when it says nothing
bool cfgFileValue(const char* section, const char* key, char* out, size_t n) {
    char path[160], line[192], want[40];
    out[0] = '\0';
    snprintf(path, sizeof(path), "%s/system.cfg", plat::userBase());
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
    if      (!strcmp(key, "board_name"))            snprintf(out, n, "%s", c.boardName);
    else if (!strcmp(key, "hostname"))              snprintf(out, n, "%.*s", static_cast<int>(n) - 1, c.hostname);
    else if (!strcmp(key, "tz"))                    snprintf(out, n, "%.*s", static_cast<int>(n) - 1, c.tz);
    else if (!strcmp(key, "ntp_server"))            snprintf(out, n, "%.*s", static_cast<int>(n) - 1, c.ntpServer);
    else if (!strcmp(key, "idle_minutes"))          snprintf(out, n, "%u", c.idleMinutes);
    else if (!strcmp(key, "landing"))               snprintf(out, n, "%s", users::landKey(c.landing));
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
    else if (!strcmp(key, "wifi_ssid"))             snprintf(out, n, "%s", c.wifiSsid);
    else if (!strcmp(key, "wifi_password"))         snprintf(out, n, "%s", c.wifiPass[0] ? kMasked : "");
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
    else {
        // Its own settings. Without this a declared setting the file has
        // never carried shows blank, and the sysop cannot tell "empty" from
        // "running on a default I have no way to see".
        const Plugin* pl = plugins::at(i);
        if (pl && pl->setting) pl->setting(key, out, n);
    }
}

// ---------------------------------------------------------------------------
// cfgDeclared: does this plugin already offer the key as a proper field?
// Used to keep a hand-added key in the file from appearing twice.
// ---------------------------------------------------------------------------
bool cfgDeclared(const Plugin* pl, const char* key) {
    if (!strcmp(key, "enabled") || !strcmp(key, "read") ||
        !strcmp(key, "write")   || !strcmp(key, "admin")) return true;
    for (uint8_t i = 0; pl && i < pl->settingCount; ++i)
        if (!strcmp(pl->settings[i].key, key)) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Composite helpers.
// ---------------------------------------------------------------------------

// compositeFor: is this section's key one of the packed ones?
const CfgComposite* compositeFor(const char* section, const char* key) {
    if (!section || !*section || !key) return nullptr;
    for (uint8_t i = 0; i < kCompositeCount; ++i) {
        const CfgComposite& c = kComposites[i];
        size_t plen = strlen(c.prefix);
        if (strcasecmp(section, c.section)) continue;
        if (strncasecmp(key, c.prefix, plen)) continue;
        if (!key[plen]) continue;                      // the prefix alone is not a record
        return &c;
    }
    return nullptr;
}

// cfgPart: one bar-separated part of a packed value, trimmed. Empty when
// the value does not go that far, which is how an optional part reads.
void cfgPart(const char* packed, uint8_t want, char* out, size_t n) {
    out[0] = '\0';
    const char* p = packed;
    for (uint8_t i = 0; p; ++i) {
        const char* bar = strchr(p, '|');
        size_t len = bar ? static_cast<size_t>(bar - p) : strlen(p);
        if (i == want) {
            while (len && (*p == ' ' || *p == '\t')) { ++p; --len; }
            while (len && (p[len - 1] == ' ' || p[len - 1] == '\t')) --len;
            if (len > n - 1) len = n - 1;
            memcpy(out, p, len);
            out[len] = '\0';
            return;
        }
        p = bar ? bar + 1 : nullptr;
    }
}

// cfgSectionPlugin: the plugin index behind "plugin:files", 0xFF for none
uint8_t cfgSectionPlugin(const char* section) {
    if (!section || strncasecmp(section, "plugin:", 7)) return 0xFF;
    return plugins::indexOf(section + 7);
}

// cfgSummary: what the button says. The name, or what it points at when it
// has no name, or plainly nothing so an unused row reads as unused rather
// than as a blank somebody forgot to fill in.
void cfgSummary(const char* packed, uint8_t namePart, char* out, size_t n) {
    char part[64];
    cfgPart(packed, namePart, part, sizeof(part));
    if (!part[0]) cfgPart(packed, 0, part, sizeof(part));
    snprintf(out, n, "%.*s", static_cast<int>(n) - 1, part[0] ? part : "not set");
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
    if (claims::holds(claims::Res::Config, s.id)) {
        claims::release(claims::Res::Config, s.id);
        g_cfgOwner = nullptr;
        g_cfgPage  = nullptr;
        // The nesting goes with it. A line dropped two levels in is still
        // one dropped line, and leaving g_subComp set would have the next
        // sysop's first Save write somebody else's half-edited area.
        g_subComp  = nullptr;
        g_subKey[0]  = '\0';
        g_subOrig[0] = '\0';
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

    if (claims::held(claims::Res::Config) && !claims::holds(claims::Res::Config, s.id)) {
        snprintf(buf, sizeof(buf), "%.20s is editing the settings.",
                 g_cfgOwner ? g_cfgOwner->user : "Somebody");
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

        const Plugin* pl = plugins::at(pi);
        uint8_t n = 0;
        g_cfgPlugin[n++] = { "enabled", "Enabled", CK_YESNO, 0, 0, 4 };
        g_cfgPlugin[n++] = { "read",    "Read",    CK_LEVEL, 0, 0, 6 };
        g_cfgPlugin[n++] = { "write",   "Write",   CK_LEVEL, 0, 0, 6 };
        g_cfgPlugin[n++] = { "admin",   "Admin",   CK_LEVEL, 0, 0, 6 };

        // What the plugin says it has, in its own order, whether or not the
        // file has ever carried it. This is the whole point: a setting a
        // sysop has never written is still a setting they can find.
        const uint8_t kValueMax = static_cast<uint8_t>(sizeof(g_cfgBuf[0]) - 1);
        for (uint8_t i = 0; pl && i < pl->settingCount && n < Form::kMaxFields; ++i) {
            const PluginSetting& ps = pl->settings[i];
            uint8_t kind = ps.kind == PS_NUM   ? CK_NUM
                         : ps.kind == PS_YESNO ? CK_YESNO
                         : ps.kind == PS_INFO  ? CK_INFO
                                               : CK_TEXT;
            uint8_t cap = ps.cap < kValueMax ? ps.cap : kValueMax;
            // A packed setting is a button, not a box: the parts are a page
            // of their own and nothing is ever typed into the row itself.
            if (compositeFor(g_cfgSection, ps.key)) kind = CK_SUB;
            g_cfgPlugin[n++] = { ps.key, ps.label, kind, ps.lo, ps.hi, cap };
        }

        // Anything else the file carries stays editable, so a key somebody
        // added by hand is never silently dropped on the next save.
        for (uint8_t i = 0; i < grab.n && n < Form::kMaxFields; ++i) {
            if (cfgDeclared(pl, g_cfgKeys[i])) continue;
            g_cfgPlugin[n++] = { g_cfgKeys[i], g_cfgKeys[i], CK_TEXT, 0, 0, 40 };
        }
        g_cfgPluginPage = { plugins::at(pi)->info.name, plugins::at(pi)->info.name,
                            "", g_cfgPlugin, n };
        page = &g_cfgPluginPage;
    }

    claims::take(claims::Res::Config, s.id);
    g_cfgOwner = &s;
    g_cfgPage  = page;
    g_subComp  = nullptr;                                  // a fresh page is never nested
    for (uint8_t i = 0; i < page->count && i < Form::kMaxFields; ++i) {
        const CfgField& f = page->fields[i];
        char* buf2 = g_cfgBuf[i];
        if (!cfgFileValue(g_cfgSection, f.key, buf2, sizeof(g_cfgBuf[0]))) {
            if (g_cfgSection[0]) cfgPluginValue(arg, f.key, buf2, sizeof(g_cfgBuf[0]));
            else                 cfgLiveValue(f.key, buf2, sizeof(g_cfgBuf[0]));
        }
        if (f.kind == CK_PASS && *buf2) snprintf(buf2, sizeof(g_cfgBuf[0]), "%s", kMasked);
        // What it was when the page opened, at full width. Truncating this
        // meant any value longer than the truncation always compared
        // different and so was rewritten on every save, touched or not.
        g_cfgWas[i] = bbsu::hash(buf2);
    }
    configOpenPage(s, 0, now);
}

// ---------------------------------------------------------------------------
// configOpenPage: draw the page that is already loaded into g_cfgBuf, with
// the focus where the caller left it.
//
// Split out of cmdConfig because a sub-page has to come back to the page it
// was opened from, and that page's edits are still in the buffers. Reading
// the file again would be simpler and would throw away anything typed on
// the parent row above before the button was pressed.
// ---------------------------------------------------------------------------
void Bbs::configOpenPage(Session& s, uint8_t focus, uint32_t now) {
    if (!g_cfgPage) return;
    uint8_t n = 0;
    for (uint8_t i = 0; i < g_cfgPage->count && i < Form::kMaxFields; ++i) {
        const CfgField& f = g_cfgPage->fields[i];
        uint8_t flags = FF_NONE;
        const char* choices = nullptr;
        char* buf2 = g_cfgBuf[i];
        if (f.kind == CK_YESNO) { flags |= FF_CYCLE; choices = kYesNo; }
        if (f.kind == CK_LEVEL) { flags |= FF_CYCLE; choices = kLevels; }
        if (f.kind == CK_PASS)  flags |= FF_MASK;
        // Shown so a sysop can see what this plugin is working with, and
        // read-only because something else owns it.
        if (f.kind == CK_INFO)  flags |= FF_READONLY;
        if (f.kind == CK_SUB) {
            flags |= FF_ACTION;
            const CfgComposite* comp = compositeFor(g_cfgSection, f.key);
            cfgSummary(buf2, comp ? comp->namePart : 1, g_cfgSum[i], sizeof(g_cfgSum[0]));
            buf2 = g_cfgSum[i];                 // the button shows the summary
        }
        addField(s, n, f.label, buf2, f.kind == CK_SUB ? 0 : f.cap, flags, choices);
    }
    s.ed        = LineEditor();
    s.formKind  = FormKind::Config;
    s.st        = SState::Form;
    s.lastInput = now;
    s.form.begin(g_cfgPage->title, s.fields, n, s.term, s.tl, focus);
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
        if (f.kind == CK_SUB) continue;                              // its own page writes it
        if (f.kind == CK_INFO) continue;                             // somebody else owns it
        if (f.kind == CK_PASS && !strcmp(v, kMasked)) continue;      // untouched
        if (bbsu::hash(v) == g_cfgWas[i]) continue;                  // nothing to write
        if (!*v && (f.kind == CK_YESNO || f.kind == CK_LEVEL)) continue;
        // WPA2's own rule. Caught here, where it can be retyped, rather than
        // by the parser after it is on disk and the reload has refused it.
        if (!strcmp(f.key, "wifi_password") && *v && strlen(v) < 8) {
            s.form.fail(i, "8 to 64 characters", s.term, s.tl);
            return false;
        }
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
    bool ok = configReloadAll(err, errLen);
    // The radio is not touched until a restart (see kWifi), so "live" would
    // be a promise the board is not keeping.
    if (ok && g_cfgPage->fields == kWifi && !strcmp(err, "Saved and live"))
        snprintf(err, errLen, "Saved, used from the next restart");
    return ok;
}

// ---------------------------------------------------------------------------
// configReloadAll: read the file again and restart the plugins, having
// first got every caller out of one. Shared by a page save and a sub-page
// save, so "Saved and live" means exactly the same thing either way.
// ---------------------------------------------------------------------------
bool Bbs::configReloadAll(char* err, size_t errLen) {
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
            return;
        }
        // A caller part way through a generated list is holding a flat index
        // across the command tables, and dropPluginCommands is about to
        // renumber everything past the core. Resuming would carry on at the
        // wrong command or stop early. Cheaper to end the list than to make
        // the index survive a reload.
        if (o.st == SState::List || o.st == SState::More || o.list != ListKind::None)
            Bbs::instance().abortOutput(o);
    }, nullptr);
    plugins::stopAll();
    dropPluginCommands();        // or every reload registers them again
    plugins::begin(*this);
    snprintf(err, errLen, "Saved and live");
    return true;
}

// ===========================================================================
// CONFIG sub-pages: one row of a page, opened as a page of its own
// ===========================================================================

// ---------------------------------------------------------------------------
// configSubOpen: a button was pressed. Unpack the row into its parts and
// open them as a form. The parent page's buffers are left exactly as they
// are, so anything typed on it before the button was pressed survives.
// ---------------------------------------------------------------------------
void Bbs::configSubOpen(Session& s, uint8_t field, uint32_t now) {
    if (!claims::holds(claims::Res::Config, s.id) || !g_cfgPage) return;
    if (field >= g_cfgPage->count || field >= Form::kMaxFields) return;
    const CfgField& f = g_cfgPage->fields[field];
    const CfgComposite* comp = compositeFor(g_cfgSection, f.key);
    if (f.kind != CK_SUB || !comp) return;

    g_subComp  = comp;
    g_subField = field;
    snprintf(g_subKey,  sizeof(g_subKey),  "%s", f.key);
    snprintf(g_subOrig, sizeof(g_subOrig), "%s", g_cfgBuf[field]);

    uint8_t pi = cfgSectionPlugin(g_cfgSection);
    uint8_t cnt = comp->count < kMaxParts ? comp->count : kMaxParts;

    // A level the record does not carry, or one the ladder does not know,
    // shows the level the record is actually running under. A blank cycle
    // field would step to "all" on the first space, which is the one wrong
    // answer that opens an area to everybody.
    //
    // Two passes, because a part can inherit from a part after it (a
    // forum's Start falls back to its Reply). The plugin's own levels first,
    // then the parts that follow another part, from what that part became.
    bool unset[kMaxParts] = {};
    for (uint8_t i = 0; i < cnt; ++i) {
        const CfgPart& p = comp->parts[i];
        cfgPart(g_subOrig, i, g_subBuf[i], sizeof(g_subBuf[0]));
        if (p.kind != CK_LEVEL) continue;
        PlugLevel lv = PlugLevel::Nobody;
        unset[i] = !plugins::levelFromText(g_subBuf[i], lv) || lv == PlugLevel::Nobody;
        if (unset[i] && !(p.inherit & IN_PART))
            snprintf(g_subBuf[i], sizeof(g_subBuf[0]), "%s",
                     plugins::levelName(plugins::levelFor(pi, p.inherit)));
    }
    for (uint8_t i = 0; i < cnt; ++i) {
        const CfgPart& p = comp->parts[i];
        if (p.kind != CK_LEVEL || !unset[i] || !(p.inherit & IN_PART)) continue;
        uint8_t from = static_cast<uint8_t>(p.inherit & 0x0F);
        if (from < cnt) snprintf(g_subBuf[i], sizeof(g_subBuf[0]), "%s", g_subBuf[from]);
    }

    uint8_t n = 0;
    for (uint8_t i = 0; i < cnt && n < Form::kMaxFields; ++i) {
        const CfgPart& p = comp->parts[i];
        uint8_t flags = FF_NONE;
        const char* choices = nullptr;
        if (p.kind == CK_LEVEL) {
            flags |= FF_CYCLE;
            choices = kLevels;
        }
        uint8_t cap = p.cap < sizeof(g_subBuf[0]) - 1 ? p.cap
                                                      : static_cast<uint8_t>(sizeof(g_subBuf[0]) - 1);
        addField(s, n, p.label, g_subBuf[i], cap, flags, choices);
    }

    char title[40];
    snprintf(title, sizeof(title), "%s %s", comp->title, g_subKey + strlen(comp->prefix));
    s.ed        = LineEditor();
    s.formKind  = FormKind::ConfigArea;
    s.st        = SState::Form;
    s.lastInput = now;
    s.form.begin(title, s.fields, n, s.term, s.tl);
}

// ---------------------------------------------------------------------------
// configSubSave: pack the parts back up and write the one key.
//
// The packed form is what goes in the file, because that is what the plugin
// parses and what a sysop reads with the card in a laptop. An empty first
// part writes an empty value, which is how a row is cleared: the plugin
// already ignores a record with nothing to point at.
// ---------------------------------------------------------------------------
bool Bbs::configSubSave(Session& s, char* err, size_t errLen) {
    if (!g_subComp || !g_cfgPage) { snprintf(err, errLen, "nothing to save"); return false; }
    const CfgComposite* comp = g_subComp;

    for (uint8_t i = 0; i < comp->count && i < kMaxParts; ++i) {
        // A bar in a part would pack into a value that unpacks as two, so
        // it is refused where the sysop can see it rather than silently
        // moving the name into the level column on the next reload.
        if (strchr(g_subBuf[i], '|')) {
            s.form.fail(i, "No | in a value", s.term, s.tl);
            return false;
        }
        if (comp->parts[i].kind == CK_TEXT && strstr(g_subBuf[i], "..")) {
            s.form.fail(i, "No .. in a path", s.term, s.tl);
            return false;
        }
    }

    char packed[160] = "";
    if (g_subBuf[0][0]) {
        size_t at = 0;
        for (uint8_t i = 0; i < comp->count && i < kMaxParts; ++i) {
            int w = snprintf(packed + at, sizeof(packed) - at, "%s%s",
                             i ? " | " : "", g_subBuf[i]);
            if (w <= 0 || at + static_cast<size_t>(w) >= sizeof(packed)) break;
            at += static_cast<size_t>(w);
        }
    }

    if (!strcmp(packed, g_subOrig)) {
        snprintf(err, errLen, "Nothing changed");
        return true;
    }

    syscfg::KeyVal pair = { g_subKey, packed };
    if (!syscfg::write(&pair, 1, g_cfgSection[0] ? g_cfgSection : nullptr, err, errLen))
        return false;
    plat::log("bbs: %s set %s %s = %s", s.user, g_cfgSection, g_subKey, packed);
    return configReloadAll(err, errLen);
}

// ---------------------------------------------------------------------------
// configSubBack: back to the page the button was on, with the row's summary
// caught up to whatever is in the file now.
//
// The editing guard is not touched. The sysop never left CONFIG, and
// handing it back here would let a second staff session open a page under
// the one still on screen. A dropped line is the other case and closeSession
// still releases everything, nesting included.
// ---------------------------------------------------------------------------
void Bbs::configSubBack(Session& s, Color c, const char* msg, uint32_t now) {
    uint8_t field = g_subField;
    g_subComp = nullptr;
    if (!g_cfgPage || !claims::holds(claims::Res::Config, s.id)) {  // page went away
        configRelease(s);
        formDone(s, c, msg);                      // which ends the form properly
        return;
    }
    if (field < g_cfgPage->count && field < Form::kMaxFields) {
        const CfgField& f = g_cfgPage->fields[field];
        char* buf = g_cfgBuf[field];
        if (!cfgFileValue(g_cfgSection, f.key, buf, sizeof(g_cfgBuf[0])) &&
            cfgSectionPlugin(g_cfgSection) != 0xFF)
            cfgPluginValue(g_cfgSection + 7, f.key, buf, sizeof(g_cfgBuf[0]));
        g_cfgWas[field] = bbsu::hash(buf);
    }
    // Plain ASCII has no cursor to put back on the button, so the page is
    // asked again from the row after it: re-asking the row just dealt with
    // would walk the sysop straight back into the page they came out of.
    bool positional = s.term.isAnsi() || s.term.isPet();
    configOpenPage(s, positional ? field : static_cast<uint8_t>(field + 1), now);
    s.form.status(msg, c, s.term, s.tl);
}
