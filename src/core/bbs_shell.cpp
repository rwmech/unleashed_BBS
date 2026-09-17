/*
 * File:        src/core/bbs_shell.cpp
 * Description: Caller shell: the core command table (C3 registry), dispatch
 *              and permission checks, HELP generated from the tables, the
 *              paged lists (HELP WHO LAST DASH), the WHO n / DASH n refresh
 *              rows, PAGE / DND, TIME, MEM, TERM, BAUD, the FX demo, and BYE
 *              (which doubles as the staff elevation path).
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

constexpr uint8_t kUsageCol = 13;       // HELP: usage column width incl. the gap

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
// coreCommands: the built-in commands in HELP order. Usage stays within 12
// characters; descriptions wrap onto indented lines.
// ---------------------------------------------------------------------------
const Command* Bbs::coreCommands(uint8_t& count) {
    static const Command k[] = {
        // -- everyone -------------------------------------------------------
        { "HELP", "H?", 0, CF_NONE, "[H]ELP", "this list, also ?",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdHelp(s); } },
        { "WHO", "W", 0, CF_NONE, "[W]HO [n]", "who is on; n refreshes",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdWho(s, a); } },
        { "LAST", "", 0, CF_NONE, "LAST", "the most recent calls",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.startList(s, ListKind::Last); } },
        { "INFO", "I", 0, CF_NONE, "[I]NFO [h]", "a caller's profile",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdInfo(s, a); } },
        { "PROFILE", "", 0, CF_NONE, "PROFILE", "edit your profile",
          [](Bbs& b, Session& s, const char*, uint32_t n) { b.cmdProfile(s, n); } },
        { "PASSWORD", "", 0, CF_NONE, "PASSWORD", "change your password",
          [](Bbs& b, Session& s, const char*, uint32_t n) { b.cmdPassword(s, n); } },
        { "PAGE", "", 0, CF_NONE, "PAGE n msg", "message node n",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdPage(s, a); b.prompt(s); } },
        { "DND", "", 0, CF_NONE, "DND", "pages off / on",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdDnd(s); b.prompt(s); } },
        { "TIME", "", 0, CF_NONE, "TIME", "clock and time left",
          [](Bbs& b, Session& s, const char* a, uint32_t n) { b.cmdTime(s, a, n); } },
        { "MEM", "M", 0, CF_NONE, "[M]EM", "memory use",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdMem(s); b.prompt(s); } },
        { "TERM", "T", 0, CF_NONE, "[T]ERM", "terminal type and size",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdTerm(s); b.prompt(s); } },
        { "BAUD", "", 0, CF_NONE, "BAUD n|OFF", "emulate 300-19200 bps",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdBaud(s, a); b.prompt(s); } },
        { "CLS", "C", 0, CF_NONE, "[C]LS", "clear the screen",
          [](Bbs& b, Session& s, const char*, uint32_t) { s.term.cls(s.tl); b.prompt(s); } },
        { "FX", "F", 0, CF_NONE, "[F]X", "effects demo",
          [](Bbs&, Session& s, const char*, uint32_t) {
              s.st = SState::Fx; s.fxStep = 0; s.savedCps = s.tl.cps();
          } },
        { "G", "", 0, CF_NONE, "[G]", "log off, asks first",
          [](Bbs&, Session& s, const char*, uint32_t) {
              s.st = SState::Confirm;
              s.term.color(s.tl, Color::Yellow);
              s.term.text(s.tl, kConfirmText);
              s.term.color(s.tl, Color::White);
          } },
        { "BYE", "", 0, CF_NONE, "BYE", "log off now",
          [](Bbs& b, Session& s, const char* a, uint32_t n) { b.cmdBye(s, a, n); } },
        { "OFF", "", 0, CF_HIDDEN, "", "",
          [](Bbs& b, Session& s, const char*, uint32_t n) { b.goodbye(s, n); } },
        { "LOGOFF", "", 0, CF_HIDDEN, "", "",
          [](Bbs& b, Session& s, const char*, uint32_t n) { b.goodbye(s, n); } },
        { "QUIT", "", 0, CF_HIDDEN, "", "",
          [](Bbs& b, Session& s, const char*, uint32_t n) { b.goodbye(s, n); } },

        // -- staff (shown and dispatched only with the permission) ------------
        { "DASH", "", PERM_DASH, CF_NONE, "DASH [n]", "dashboard; n refreshes",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdDash(s, a); } },
        { "USERS", "", PERM_USERS, CF_NONE, "USERS", "manage accounts",
          [](Bbs& b, Session& s, const char*, uint32_t n) { b.cmdUsers(s, n); } },
        { "USER", "", PERM_USERS, CF_NONE, "USER ADD", "add an account",
          [](Bbs& b, Session& s, const char* a, uint32_t n) { b.cmdUser(s, a, n); } },
        { "USER", "", PERM_USERS, CF_HELPONLY, "USER EDIT h", "edit account h", nullptr },
        { "USER", "", PERM_USERS, CF_HELPONLY, "USER DEL h", "delete account h", nullptr },
        { "NODES", "", PERM_NODES, CF_NONE, "NODES", "every session with its IP",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.startList(s, ListKind::Nodes); } },
        { "KICK", "", PERM_KICK, CF_NONE, "KICK n [msg]", "disconnect node n",
          [](Bbs& b, Session& s, const char* a, uint32_t n) { b.cmdKick(s, a, n); b.prompt(s); } },
        { "BROADCAST", "", PERM_BROADCAST, CF_NONE, "BROADCAST m", "message every node",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdBroadcast(s, a); b.prompt(s); } },
        { "SNOOP", "", PERM_SNOOP, CF_NONE, "SNOOP n", "watch node n, Q stops",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdSnoop(s, a); } },
        { "TIME", "", PERM_TIME, CF_HELPONLY, "TIME n +/-m", "add/remove node minutes", nullptr },
        { "SHOW", "", PERM_HIDE, CF_NONE, "SHOW", "list me in WHO",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdShow(s, true); b.prompt(s); } },
        { "HIDE", "", PERM_HIDE, CF_NONE, "HIDE", "hide me from WHO",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdShow(s, false); b.prompt(s); } },
        { "LURK", "", PERM_HIDE, CF_NONE, "LURK", "hide and refuse pages",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.cmdLurk(s); b.prompt(s); } },
        { "BANS", "", PERM_BANS, CF_NONE, "BANS", "banned IP addresses",
          [](Bbs& b, Session& s, const char*, uint32_t) { b.startList(s, ListKind::Bans); } },
        { "UNBAN", "", PERM_UNBAN, CF_NONE, "UNBAN ip", "lift a ban",
          [](Bbs& b, Session& s, const char* a, uint32_t) { b.cmdUnban(s, a); b.prompt(s); } },
        { "DROP", "", 0, CF_STAFF, "DROP", "give up staff access",
          [](Bbs& b, Session& s, const char*, uint32_t n) { b.cmdDrop(s, n); } },
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
        if (c->perm && !can(s, c->perm)) continue;             // not granted: stays unknown
        if ((c->flags & CF_STAFF) && !s.perms) continue;
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
        if (ieq(verb, "BYE")) { cmdBye(s, arg, now); return; }
        t.color(tl, Color::LightRed);
        t.text(tl, "All nodes are in use. Try your call later.");
        t.nl(tl);
        goodbye(s, now);
        return;
    }

    const Command* c = findCommand(verb, s);
    if (c) {
        c->fn(*this, s, arg, now);
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
        case ListKind::Dash:  return rowDash(s);
        case ListKind::Users: return rowUsers(s);
        default:              return false;
    }
}

// ===========================================================================
// Row output
// ===========================================================================

// rowWidth: lists and menus are laid out for 40 columns everywhere
uint8_t Bbs::rowWidth(const Session& s) const {
    uint8_t cols = s.term.cols();
    return static_cast<uint8_t>((cols < 40 ? cols : 40) - 1);
}

// ---------------------------------------------------------------------------
// rowText: one line. On a refresh screen the line is padded to the full
// width so a redraw from the home position leaves no old characters.
// ---------------------------------------------------------------------------
void Bbs::rowText(Session& s, Color c, const char* text, bool newline) {
    Term& t = s.term;
    t.color(s.tl, c);
    t.text(s.tl, text);
    if (s.watch != ListKind::None) {
        size_t used = visibleLen(text);
        for (size_t i = used; i < rowWidth(s); ++i) t.ch(s.tl, ' ');
    }
    if (newline) t.nl(s.tl);
}

void Bbs::rowRule(Session& s) {
    s.term.color(s.tl, Color::Cyan);
    fx::rule(s.term, s.tl, rowWidth(s));
    s.term.nl(s.tl);
}

// ===========================================================================
// HELP
// ===========================================================================

void Bbs::cmdHelp(Session& s) {
    startList(s, ListKind::Help);
}

// ---------------------------------------------------------------------------
// rowHelp: generated from the command tables. Sections: everyone, then the
// staff commands this session holds. Usage in a 13-column field, the
// description wraps under itself within 40 columns.
//   listIdx: 0 header | 1..n everyone | n+1 staff header | n+2..2n+1 staff | 2n+2 footer
//   listSub: offset into the current description (continuation lines)
// ---------------------------------------------------------------------------
bool Bbs::rowHelp(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;

    uint8_t total = 0;
    while (commandAt(total)) ++total;

    for (;;) {
        uint16_t i = s.listIdx;
        if (i == 0) {
            ++s.listIdx;
            rowText(s, Color::Yellow, "Commands");
            return true;
        }
        if (i == total + 1u) {
            ++s.listIdx;
            if (!s.perms) continue;
            rowText(s, Color::Yellow, syscfg::levelName(s.level));
            return true;
        }
        if (i == 2u * total + 2u) {
            ++s.listIdx;
            rowText(s, Color::DarkGrey, t.isPet() ? "SPACE or RUN/STOP stops output"
                                                  : "Space or Ctrl-C stops output");
            return true;
        }
        if (i > 2u * total + 2u) return false;

        bool staffPass = i > total + 1u;
        const Command* c = commandAt(static_cast<uint8_t>(staffPass ? i - total - 2u : i - 1u));
        bool isStaff = c->perm || (c->flags & CF_STAFF);
        bool granted = (!c->perm || can(s, c->perm)) && (!(c->flags & CF_STAFF) || s.perms);
        if ((c->flags & CF_HIDDEN) || isStaff != staffPass || !granted) {
            ++s.listIdx;
            s.listSub = 0;
            continue;
        }

        // one visual line of this command
        uint8_t descW = static_cast<uint8_t>(rowWidth(s) - kUsageCol);
        const char* d = c->help + s.listSub;
        while (*d == ' ') ++d;
        size_t take = wrapTake(d, descW);

        char usage[16];
        snprintf(usage, sizeof(usage), "%-*.*s", kUsageCol, kUsageCol - 1, s.listSub ? "" : c->usage);
        t.color(tl, Color::LightBlue);
        t.text(tl, usage);
        t.color(tl, Color::Grey);
        t.textN(tl, d, take);
        t.nl(tl);

        const char* rest = d + take;
        while (*rest == ' ') ++rest;
        if (*rest) {
            s.listSub = static_cast<uint8_t>(rest - c->help);
        } else {
            s.listSub = 0;
            ++s.listIdx;
        }
        return true;
    }
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
// rowWho: clock line, header, the 6 caller nodes, then the sysop if shown.
// The busy line is never listed. A refresh screen keeps a fixed height.
// ---------------------------------------------------------------------------
bool Bbs::rowWho(Session& s) {
    char buf[80];
    char on[8];
    char idle[8];
    const char* fmt = "%c %-12.12s %-10.10s %3s %5s";
    uint32_t now = plat::millis();
    bool refresh = s.watch != ListKind::None;

    for (;;) {
        uint8_t i = s.listIdx++;
        if (i == 0) {
            char when[24] = "";
            if (clk::valid()) clk::fmt(when, sizeof(when), "%a %d %b %H:%M:%S");
            snprintf(buf, sizeof(buf), "%-19s%20s", "Who's online", when);
            rowText(s, Color::Yellow, buf);
            return true;
        }
        if (i == 1) {
            snprintf(buf, sizeof(buf), fmt, 'N', "Handle", "Terminal", "Min", "Idle");
            rowText(s, Color::LightBlue, buf);
            return true;
        }
        uint8_t k = static_cast<uint8_t>(i - 2);
        const Session* n = nullptr;
        if (k < BBS_MAX_NODES) {
            n = &nodes_[k];
        } else if (k == BBS_MAX_NODES) {
            if (sysop_.st == SState::Free || !sysop_.visible || sysop_.lurk) {
                if (refresh) { rowText(s, Color::Grey, ""); return true; }   // keep the frame height
                continue;
            }
            n = &sysop_;
        } else {
            return false;
        }

        bool hidden = n != &s && (!n->visible || n->lurk);   // hidden co-sysop looks like a free line
        if (n->st == SState::Free || hidden) {
            snprintf(buf, sizeof(buf), "%c -- waiting for caller --", nodeChar(*n));
            rowText(s, Color::DarkGrey, buf);
            return true;
        }
        const char* h = n->user[0] ? n->user
                      : (n->st == SState::Detect || n->st == SState::Intro ? "(connecting)" : "(logging in)");
        snprintf(on, sizeof(on), "%u", static_cast<unsigned>((now - n->connectedAt) / 60000u));
        fmtIdle(idle, sizeof(idle), now - n->lastInput);
        snprintf(buf, sizeof(buf), fmt, nodeChar(*n), h, n->term.name(), on, idle);
        rowText(s, n == &s ? Color::White : Color::Grey, buf);
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
        const char* page = s.mb.empty() ? "" : "  page waiting";
        if (left < 0) snprintf(buf, sizeof(buf), "No idle limit%s", page);
        else          snprintf(buf, sizeof(buf), "Idle still counts: %ld:%02ld left%s",
                               static_cast<long>(left / 60), static_cast<long>(left % 60), page);
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
// rowDash: 22 rows, 40 columns. Clock, uptime, NTP, memory, every node,
// the day's calls, bans, busy line, backup window, the last 5 calls.
// ---------------------------------------------------------------------------
bool Bbs::rowDash(Session& s) {
    char buf[80];
    uint32_t now = plat::millis();
    uint8_t i = s.listIdx++;

    switch (i) {
        case 0: {
            char when[24] = "clock not set";
            if (clk::valid()) clk::fmt(when, sizeof(when), "%a %d %b %H:%M:%S");
            snprintf(buf, sizeof(buf), "%-19s%20s", "SYSOP DASHBOARD", when);
            rowText(s, Color::Yellow, buf);
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
            if (h.valid) {
                snprintf(buf, sizeof(buf), "NTP %s  heap %uK min %uK big %uK", clk::valid() ? "ok" : "--",
                         static_cast<unsigned>(h.freeBytes / 1024u), static_cast<unsigned>(h.minFree / 1024u),
                         static_cast<unsigned>(h.largestBlock / 1024u));
            } else {
                snprintf(buf, sizeof(buf), "NTP %s  heap n/a on host", clk::valid() ? "ok" : "--");
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
            snprintf(buf, sizeof(buf), "%c %-12.12s %-10.10s %5s %4s", 'N', "Handle", "Terminal", "Idle", "Left");
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
            if (backup_.awaitingApproval()) {
                snprintf(buf, sizeof(buf), "Backup: upload waiting for Y/N");
            } else if (backup_.isOpen()) {
                int32_t left = static_cast<int32_t>(backup_.closesAt() - now) / 1000;
                if (left < 0) left = 0;
                snprintf(buf, sizeof(buf), "Backup window open, %ld:%02ld left",
                         static_cast<long>(left / 60), static_cast<long>(left % 60));
            } else {
                snprintf(buf, sizeof(buf), "Backup window closed");
            }
            rowText(s, backup_.isOpen() ? Color::LightGreen : Color::Grey, buf);
            return true;
        }
        case 15:
            rowText(s, Color::Yellow, "Last calls");
            return true;
        default:
            break;
    }

    if (i >= 5 && i <= 11) {                                    // nodes 1..6, then S
        const Session& n = i <= 10 ? nodes_[i - 5] : sysop_;
        if (n.st == SState::Free) {
            snprintf(buf, sizeof(buf), "%c -", nodeChar(n));
            rowText(s, Color::DarkGrey, buf);
            return true;
        }
        char idle[8];
        char left[12] = "--";
        fmtIdle(idle, sizeof(idle), now - n.lastInput);
        if (n.role == Role::Caller && n.loggedIn && !can(n, PERM_NOLIMITS)) {
            int32_t sec = secondsLeft(n, now);
            if (sec != INT32_MAX) snprintf(left, sizeof(left), "%ld", static_cast<long>(sec > 0 ? (sec + 59) / 60 : 0));
        }
        const char* h = n.user[0] ? n.user : "(logging in)";
        snprintf(buf, sizeof(buf), "%c %-12.12s %-10.10s %5s %4s", nodeChar(n), h, n.term.name(), idle, left);
        rowText(s, &n == &s ? Color::White : Color::Grey, buf);
        return true;
    }

    if (i >= 16 && i <= 20) {                                   // last 5 calls
        CallRec r;
        if (calllog::get(static_cast<uint8_t>(i - 16), r)) {
            char when[16];
            clk::fmtEpoch(when, sizeof(when), "%m/%d %H:%M", r.start);
            char node = (r.flags & CallRec::F_SYSOP) ? 'S' : static_cast<char>('0' + (r.node % 10));
            snprintf(buf, sizeof(buf), "%-12.12s %c %-11s %4u min", r.user, node, when,
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

    uint8_t i = s.listIdx++;
    if (i == 0) { rowText(s, Color::Yellow, "Last callers"); return true; }
    if (i == 1) {
        if (wide && sysop) snprintf(buf, sizeof(buf), "%-20s N %-10s %-11s %4s %s", "Handle", "Terminal", "When", "Min", "IP");
        else if (wide)     snprintf(buf, sizeof(buf), "%-20s N %-10s %-11s %4s", "Handle", "Terminal", "When", "Min");
        else               snprintf(buf, sizeof(buf), "%-12s N %-11s %4s", "Handle", "When", "Min");
        rowText(s, Color::LightBlue, buf);
        return true;
    }

    uint8_t total = calllog::count();
    if (i == 2 && total == 0) { rowText(s, Color::Grey, "No calls logged yet."); return true; }

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
        rowText(s, Color::Grey, buf);
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
