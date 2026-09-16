/*
 * File:        src/core/bbs_shell.cpp
 * Description: Caller shell: command dispatch, shortcuts, paged lists
 *              (HELP WHO LAST), PAGE / DND, TIME, MEM, TERM, BAUD, the FX
 *              demo, and BYE (which doubles as the sysop elevation path).
 *              Command reference: COMMANDS.md.
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc)
 */
#include "bbs.h"
#include "bbs_util.h"
#include "fx.h"
#include "clock.h"
#include "sysconfig.h"
#include "calllog.h"
#include "../platform/platform.h"

#include <climits>
#include <cstring>
#include <cstdio>
#include <cstdlib>

using namespace bbsu;

namespace {

// label: fixed-width demo label
void label(Term& t, Timeline& tl, const char* name) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%-11s", name);
    t.color(tl, Color::LightBlue);
    t.text(tl, buf);
    t.color(tl, Color::Grey);
}

// Rows stay under 40 characters so a C64 never auto-wraps them.
const char* const kHelp[] = {
    "[H]ELP    this list, also ?",
    "[W]HO     who is online",
    "[M]EM     memory stats",
    "[T]ERM    terminal info",
    "[C]LS     clear screen",
    "[F]X      effects demo",
    "TIME      clock and time left",
    "LAST      recent callers",
    "PAGE n m  send message m to node n",
    "DND       pages off/on",
    "BAUD n    emulate n bps, BAUD OFF",
    "[G]       log off, asks first",
    "BYE       log off now",
};

// Staff rows are shown only when the session holds the permission
struct StaffRow { const char* text; uint16_t perm; };   // perm 0 = any staff
const StaffRow kStaffHelp[] = {
    { "NODES     all sessions with IP",   PERM_NODES },
    { "KICK n m  disconnect node n",      PERM_KICK },
    { "BROADCAST m  message every node",  PERM_BROADCAST },
    { "SNOOP n   watch node n, Q stops",  PERM_SNOOP },
    { "TIME n +m add or remove minutes",  PERM_TIME },
    { "SHOW      list me in WHO",         PERM_HIDE },
    { "HIDE      hide me from WHO",       PERM_HIDE },
    { "LURK      hidden, pages off",      PERM_HIDE },
    { "BANS      banned IPs",             PERM_BANS },
    { "UNBAN ip  lift a ban",             PERM_UNBAN },
    { "DROP      give up staff access",   0 },
};

constexpr int kHelpN  = sizeof(kHelp) / sizeof(kHelp[0]);
constexpr int kStaffN = sizeof(kStaffHelp) / sizeof(kStaffHelp[0]);

void line(Term& t, Timeline& tl, Color c, const char* s) {
    t.color(tl, c);
    t.text(tl, s);
    t.nl(tl);
}

} // namespace

// ===========================================================================
// Dispatch
// ===========================================================================

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
        if (ieq(verb, "BYE")) { cmdBye(s, arg, now); return; }
        t.color(tl, Color::LightRed);
        t.text(tl, "All nodes are in use. Try your call later.");
        t.nl(tl);
        goodbye(s, now);
        return;
    }

    if (s.perms && runSysop(s, verb, arg, now)) return;

    if (ieq(verb, "HELP") || ieq(verb, "H") || ieq(verb, "?")) { cmdHelp(s); return; }
    if (ieq(verb, "WHO")  || ieq(verb, "W")) { startList(s, ListKind::Who); return; }
    if (ieq(verb, "LAST"))                   { startList(s, ListKind::Last); return; }
    if (ieq(verb, "MEM")  || ieq(verb, "M")) { cmdMem(s); prompt(s); return; }
    if (ieq(verb, "TERM") || ieq(verb, "T")) { cmdTerm(s); prompt(s); return; }
    if (ieq(verb, "CLS")  || ieq(verb, "C")) { t.cls(tl); prompt(s); return; }
    if (ieq(verb, "TIME"))                   { cmdTime(s, now); prompt(s); return; }
    if (ieq(verb, "PAGE"))                   { cmdPage(s, arg); prompt(s); return; }
    if (ieq(verb, "DND"))                    { cmdDnd(s); prompt(s); return; }
    if (ieq(verb, "BAUD"))                   { cmdBaud(s, arg); prompt(s); return; }
    if (ieq(verb, "FX")   || ieq(verb, "F")) {
        s.st       = SState::Fx;
        s.fxStep   = 0;
        s.savedCps = tl.cps();
        return;
    }
    if (ieq(verb, "G")) {
        s.st = SState::Confirm;
        t.color(tl, Color::Yellow);
        t.text(tl, kConfirmText);
        t.color(tl, Color::White);
        return;
    }
    if (ieq(verb, "BYE")) { cmdBye(s, arg, now); return; }
    if (ieq(verb, "OFF") || ieq(verb, "LOGOFF") || ieq(verb, "QUIT")) {
        goodbye(s, now);
        return;
    }

    t.color(tl, Color::LightRed);
    t.text(tl, t.isPet() ? "?SYNTAX  ERROR" : "Unknown command. Type HELP.");
    prompt(s);
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
        default:              return false;
    }
}

// ===========================================================================
// Lists
// ===========================================================================

// ---------------------------------------------------------------------------
// cmdHelp: help screen file for callers, built-in list otherwise.
// Staff always get the built-in list with the commands they hold.
// ---------------------------------------------------------------------------
void Bbs::cmdHelp(Session& s) {
    if (!s.perms && playScreen(s, "help")) return;
    startList(s, ListKind::Help);
}

bool Bbs::rowHelp(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;

    for (;;) {
        int i = s.listIdx++;
        if (i == 0) { line(t, tl, Color::Yellow, "Commands"); return true; }
        i -= 1;
        if (i < kHelpN) { line(t, tl, Color::Cyan, kHelp[i]); return true; }
        i -= kHelpN;
        if (s.perms) {
            if (i == 0) { line(t, tl, Color::Yellow, syscfg::levelName(s.level)); return true; }
            i -= 1;
            if (i < kStaffN) {
                const StaffRow& r = kStaffHelp[i];
                if (r.perm && !can(s, r.perm)) continue;     // not granted: skip the row
                line(t, tl, Color::Cyan, r.text);
                return true;
            }
            i -= kStaffN;
        }
        if (i == 0) {
            line(t, tl, Color::Grey, t.isPet() ? "SPACE or RUN/STOP stops output"
                                               : "Space or Ctrl-C stops output");
            return true;
        }
        return false;
    }
}

// ---------------------------------------------------------------------------
// rowWho: clock line, header, the 6 caller nodes, then the sysop if shown.
// The busy line is never listed.
// ---------------------------------------------------------------------------
bool Bbs::rowWho(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    bool wide = t.cols() >= 60;
    const char* fmt = wide ? "%c %-20.20s %-10.10s %4s %5s" : "%c %-12.12s %-10.10s %3s %5s";
    char buf[80];
    char on[8];
    char idle[8];
    uint32_t now = plat::millis();

    for (;;) {
        uint8_t i = s.listIdx++;
        if (i == 0) {
            char when[32];
            t.color(tl, Color::Yellow);
            t.text(tl, "Who's online");
            if (clk::valid()) {
                clk::fmt(when, sizeof(when), "   %a %d %b %H:%M");
                t.color(tl, Color::Grey);
                t.text(tl, when);
            }
            t.nl(tl);
            return true;
        }
        if (i == 1) {
            snprintf(buf, sizeof(buf), fmt, 'N', "Handle", "Terminal", "Min", "Idle");
            line(t, tl, Color::LightBlue, buf);
            return true;
        }
        uint8_t k = static_cast<uint8_t>(i - 2);
        const Session* n = nullptr;
        if (k < BBS_MAX_NODES) {
            n = &nodes_[k];
        } else if (k == BBS_MAX_NODES) {
            if (sysop_.st == SState::Free || !sysop_.visible || sysop_.lurk) continue;
            n = &sysop_;
        } else {
            return false;
        }

        bool hidden = n != &s && (!n->visible || n->lurk);   // hidden co-sysop looks like a free line
        if (n->st == SState::Free || hidden) {
            snprintf(buf, sizeof(buf), "%c -- waiting for caller --", nodeChar(*n));
            line(t, tl, Color::DarkGrey, buf);
            return true;
        }
        const char* h = n->user[0] ? n->user
                      : (n->st == SState::Detect || n->st == SState::Intro ? "(connecting)" : "(logging in)");
        snprintf(on, sizeof(on), "%u", static_cast<unsigned>((now - n->connectedAt) / 60000u));
        fmtIdle(idle, sizeof(idle), now - n->lastInput);
        snprintf(buf, sizeof(buf), fmt, nodeChar(*n), h, n->term.name(), on, idle);
        line(t, tl, n == &s ? Color::White : Color::Grey, buf);
        return true;
    }
}

// ---------------------------------------------------------------------------
// rowLast: newest calls first; sysop calls only shown to the sysop
// ---------------------------------------------------------------------------
bool Bbs::rowLast(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    bool wide  = t.cols() >= 60;
    bool staff = s.perms != 0;             // sees sysop-node calls
    bool sysop = can(s, PERM_NODES);          // sees IPs
    char buf[96];

    uint8_t i = s.listIdx++;
    if (i == 0) { line(t, tl, Color::Yellow, "Last callers"); return true; }
    if (i == 1) {
        if (wide && sysop) snprintf(buf, sizeof(buf), "%-20s N %-10s %-11s %4s %s", "Handle", "Terminal", "When", "Min", "IP");
        else if (wide)     snprintf(buf, sizeof(buf), "%-20s N %-10s %-11s %4s", "Handle", "Terminal", "When", "Min");
        else               snprintf(buf, sizeof(buf), "%-12s N %-11s %4s", "Handle", "When", "Min");
        line(t, tl, Color::LightBlue, buf);
        return true;
    }

    uint8_t total = calllog::count();
    if (i == 2 && total == 0) { line(t, tl, Color::Grey, "No calls logged yet."); return true; }

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
        if (wide && sysop) snprintf(buf, sizeof(buf), "%-20.20s %c %-10.10s %-11s %4u %s", r.user, node, term, when, mins, r.ip);
        else if (wide)     snprintf(buf, sizeof(buf), "%-20.20s %c %-10.10s %-11s %4u", r.user, node, term, when, mins);
        else               snprintf(buf, sizeof(buf), "%-12.12s %c %-11s %4u", r.user, node, when, mins);
        line(t, tl, Color::Grey, buf);
        return true;
    }
    return false;
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

    t.color(tl, Color::Cyan);
    if (h.valid) {
        snprintf(buf, sizeof(buf), "Heap free     %7u", static_cast<unsigned>(h.freeBytes));    t.text(tl, buf); t.nl(tl);
        snprintf(buf, sizeof(buf), "Heap min      %7u", static_cast<unsigned>(h.minFree));      t.text(tl, buf); t.nl(tl);
        snprintf(buf, sizeof(buf), "Largest block %7u", static_cast<unsigned>(h.largestBlock)); t.text(tl, buf); t.nl(tl);
        long used = static_cast<long>(heapBaseline_) - static_cast<long>(h.freeBytes);
        snprintf(buf, sizeof(buf), "Heap vs boot  %7ld", used);          t.text(tl, buf); t.nl(tl);
    } else {
        t.text(tl, "Heap stats not available on host.");
        t.nl(tl);
    }
    snprintf(buf, sizeof(buf), "Session       %7u x %u",
             static_cast<unsigned>(sizeof(Session)), static_cast<unsigned>(kSessions));
    t.text(tl, buf);
    t.nl(tl);
    snprintf(buf, sizeof(buf), "Nodes active  %7u", activeNodes());
    t.text(tl, buf);
}

void Bbs::cmdTerm(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    char buf[48];
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
// cmdTime: wall clock, time on, time left
// ---------------------------------------------------------------------------
void Bbs::cmdTime(Session& s, uint32_t now) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    char buf[64];
    char when[40];

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

    int32_t left = (s.role == Role::Caller && s.loggedIn && !can(s, PERM_NOLIMITS)) ? secondsLeft(s, now) : INT32_MAX;
    if (left == INT32_MAX) snprintf(buf, sizeof(buf), "Left     no limit");
    else                   snprintf(buf, sizeof(buf), "Left     %ld min", static_cast<long>((left + 59) / 60));
    t.text(tl, buf);
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
        snprintf(buf, sizeof(buf), "Node %c is not taking pages.", nodeChar(*to));
        t.color(tl, Color::LightRed);
        t.text(tl, buf);
        return;
    }
    post(*to, BusKind::Page, &s, msg);
    char buf[32];
    snprintf(buf, sizeof(buf), "Page sent to node %c.", nodeChar(*to));
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
// password is an ordinary logoff that counts toward an IP ban.
// ---------------------------------------------------------------------------
void Bbs::cmdBye(Session& s, const char* arg, uint32_t now) {
    if (*arg && s.role != Role::Sysop && syscfg::anyPassword()) {
        Access lv = syscfg::passwordLevel(arg);
        if (lv != Access::None) {
            bans_.clear(s.ipAddr);
            if (lv == Access::Sysop) { elevate(s, now); return; }
            if (lv > s.level && s.role == Role::Caller) { coElevate(s, lv, now); return; }
            goodbye(s, now);                     // busy line has no node to keep
            return;
        }
        plat::log("bbs: node %c staff password failed from %s", nodeChar(s), s.ip);
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
