/*
 * File:        src/core/bbs_sysop.cpp
 * Description: Staff access.
 *              Sysop: BYE <sysop_password> from any node or the busy line
 *              moves the session to the hidden sysop node and frees its
 *              caller node. Every permission, always.
 *              Co-sysop 1/2: BYE <cosysopN_password> keeps the session on
 *              its caller node and grants that level's column of the
 *              [access] matrix in system.cfg.
 *              Commands: NODES KICK BROADCAST SNOOP TIME n +/-m SHOW HIDE
 *              LURK BANS UNBAN DROP (see COMMANDS.md). A command the
 *              session does not hold answers as an unknown command.
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc)
 */
#include "bbs.h"
#include "bbs_util.h"
#include "clock.h"
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
// runSysop: staff verbs gated by the session's permissions; false falls
// through to the caller shell (and "Unknown command" for ungranted verbs)
// ---------------------------------------------------------------------------
bool Bbs::runSysop(Session& s, const char* verb, const char* arg, uint32_t now) {
    Term& t = s.term;
    Timeline& tl = s.tl;

    if (ieq(verb, "DROP"))  { cmdDrop(s, now); return true; }
    if (ieq(verb, "NODES")     && can(s, PERM_NODES))     { startList(s, ListKind::Nodes); return true; }
    if (ieq(verb, "BANS")      && can(s, PERM_BANS))      { startList(s, ListKind::Bans); return true; }
    if (ieq(verb, "KICK")      && can(s, PERM_KICK))      { cmdKick(s, arg, now); prompt(s); return true; }
    if (ieq(verb, "BROADCAST") && can(s, PERM_BROADCAST)) { cmdBroadcast(s, arg); prompt(s); return true; }
    if (ieq(verb, "SNOOP")     && can(s, PERM_SNOOP))     { cmdSnoop(s, arg); return true; }
    if (ieq(verb, "UNBAN")     && can(s, PERM_UNBAN))     { cmdUnban(s, arg); prompt(s); return true; }
    if (ieq(verb, "TIME") && *arg && can(s, PERM_TIME))   { cmdTimeAdjust(s, arg); prompt(s); return true; }
    if (!can(s, PERM_HIDE)) return false;
    if (ieq(verb, "SHOW")) {
        s.visible = true;
        s.lurk    = false;
        s.dnd     = false;
        say(t, tl, Color::Cyan, "You are listed in WHO.");
        prompt(s);
        return true;
    }
    if (ieq(verb, "HIDE")) {
        s.visible = false;
        say(t, tl, Color::Cyan, "You are hidden from WHO.");
        prompt(s);
        return true;
    }
    if (ieq(verb, "LURK")) {
        s.lurk = !s.lurk;
        if (s.lurk) { s.visible = false; s.dnd = true; }
        else        { s.dnd = false; }
        say(t, tl, Color::Cyan, s.lurk ? "Lurking: hidden, pages off." : "Lurk off: still hidden, pages on.");
        prompt(s);
        return true;
    }
    return false;
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
    d.guestUntil = 0;
    d.timeWarned = 0;
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
    const char* fmt = wide ? "%c %-20.20s %-15.15s %-10.10s %4s %5s" : "%c %-10.10s %-15.15s %4s %5s";
    char buf[96];
    char left[12];
    char idle[8];
    uint32_t now = plat::millis();

    for (;;) {
        uint8_t i = s.listIdx++;
        if (i == 0) {
            if (wide) snprintf(buf, sizeof(buf), fmt, 'N', "Handle", "IP", "Terminal", "Left", "Idle");
            else      snprintf(buf, sizeof(buf), fmt, 'N', "Handle", "IP", "Left", "Idle");
            t.color(tl, Color::LightBlue);
            t.text(tl, buf);
            t.nl(tl);
            return true;
        }
        uint8_t k = static_cast<uint8_t>(i - 1);
        if (k >= kSessions) return false;
        const Session* o = all_[k];
        if (o->role != Role::Caller && o->st == SState::Free) continue;
        if (o != &s && outranks(*o, s) && (!o->visible || o->lurk)) {
            if (o->role != Role::Caller) continue;   // hidden sysop node: no row
            snprintf(buf, sizeof(buf), "%c -", nodeChar(*o));   // hidden senior co-sysop
            t.color(tl, Color::DarkGrey);
            t.text(tl, buf);
            t.nl(tl);
            return true;
        }

        if (o->st == SState::Free) {
            snprintf(buf, sizeof(buf), "%c -", nodeChar(*o));
            t.color(tl, Color::DarkGrey);
            t.text(tl, buf);
            t.nl(tl);
            return true;
        }

        const char* h = o->user[0] ? o->user : "(no handle)";
        if (o->role == Role::Caller && o->loggedIn && !can(*o, PERM_NOLIMITS)) {
            int32_t sec = secondsLeft(*o, now);
            if (sec == INT32_MAX) snprintf(left, sizeof(left), "--");
            else                  snprintf(left, sizeof(left), "%ld", static_cast<long>((sec + 59) / 60));
        } else {
            snprintf(left, sizeof(left), "--");
        }
        fmtIdle(idle, sizeof(idle), now - o->lastInput);
        if (wide) snprintf(buf, sizeof(buf), fmt, nodeChar(*o), h, o->ip, o->term.name(), left, idle);
        else      snprintf(buf, sizeof(buf), fmt, nodeChar(*o), h, o->ip, left, idle);
        t.color(tl, o == &s ? Color::White : Color::Grey);
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

    uint8_t i = s.listIdx++;
    if (i == 0) {
        say(t, tl, Color::LightBlue, "Banned IP        Min left");
        t.nl(tl);
        return true;
    }
    if (i == 1) {
        bool any = false;
        for (uint8_t k = 0; k < BBS_BAN_SLOTS && !any; ++k) any = bans_.at(k, now, e);
        if (!any) {
            say(t, tl, Color::Grey, "No active bans.");
            t.nl(tl);
            s.listIdx = static_cast<uint8_t>(BBS_BAN_SLOTS + 2);
            return true;
        }
    }
    for (uint8_t k = static_cast<uint8_t>(i - 1); k < BBS_BAN_SLOTS; ++k) {
        if (!bans_.at(k, now, e)) continue;
        s.listIdx = static_cast<uint8_t>(k + 2);
        ipToText(e.ip, ip, sizeof(ip));
        snprintf(buf, sizeof(buf), "%-16s %u", ip, static_cast<unsigned>((e.until - now + 59999u) / 60000u));
        say(t, tl, Color::Grey, buf);
        t.nl(tl);
        return true;
    }
    return false;
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
        s.dayUsedMin = bank_.used(s.user, s.ipAddr, clk::dayKey(now));
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
    d.dayUsedMin = bank_.used(d.user, d.ipAddr, clk::dayKey(now));
    d.timeAdjMin = 0;
    d.timeWarned = 0;
    d.lastInput  = now;

    char buf[32];
    snprintf(buf, sizeof(buf), "Back on node %u.", id);
    say(d.term, d.tl, Color::LightGreen, buf);
    plat::log("bbs: sysop node -> node %u (%s)", id, d.user);
    prompt(d);
}
