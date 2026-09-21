/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/bbs.cpp
 * Module:       Core / sessions and scheduler
 *
 * Purpose:      BBS core loop: listener, 6 caller nodes, the busy line,
 *                  the hidden sysop node, connect-time detection, welcome and
 *                  bulletin screens, handle prompt, idle and time-limit timers,
 *                  message delivery, and paged output. Commands live in
 *                  bbs_shell.cpp and bbs_sysop.cpp.
 *
 * Libraries:    BSD sockets (lwIP on ESP32, libc on host)
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <climits>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

using namespace bbsu;

namespace {

constexpr uint32_t kEscIdleMs = 150;     // lone ESC resolves after this
const char kBusyMsg[] = "\r\nBUSY\r\n";  // overflow beyond the busy line

// ---------------------------------------------------------------------------
// DirectSink: immediate best-effort send for telnet negotiation replies
// ---------------------------------------------------------------------------
class DirectSink : public ByteSink {
public:
    explicit DirectSink(int fd) : fd_(fd) {}
    bool put(const uint8_t* d, size_t n) override {
        return send(fd_, d, n, MSG_DONTWAIT | MSG_NOSIGNAL) == static_cast<ssize_t>(n);
    }
private:
    int fd_;
};

// ---------------------------------------------------------------------------
// sessSend: Timeline send callback, mirrors to a snooping sysop
// ---------------------------------------------------------------------------
int sessSend(void* ctx, const uint8_t* d, size_t n) {
    Session* s = static_cast<Session*>(ctx);
    ssize_t r = send(s->fd, d, n, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) { s->wantWrite = true; return 0; }
        return -1;
    }
    Session* w = s->snooper;
    if (r > 0 && w && w->fd >= 0 && w->st == SState::Snoop) {
        send(w->fd, d, static_cast<size_t>(r), MSG_DONTWAIT | MSG_NOSIGNAL);   // best effort
    }
    if (static_cast<size_t>(r) < n) s->wantWrite = true;
    return static_cast<int>(r);
}

// ---------------------------------------------------------------------------
// keyTramp: Term::feed callback into Bbs::onKey
// ---------------------------------------------------------------------------
struct KeyCtx { Bbs* bbs; Session* s; uint32_t now; };

void keyTramp(void* c, int k) {
    KeyCtx* kc = static_cast<KeyCtx*>(c);
    kc->bbs->onKey(*kc->s, k, kc->now);
}

void setNonBlocking(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

} // namespace

// ===========================================================================
// Lifecycle
// ===========================================================================

Bbs& Bbs::instance() {
    static Bbs bbs;   // static storage, never on a task stack
    return bbs;
}

// ---------------------------------------------------------------------------
// begin: wire the session table, bind the single dial-in port
// ---------------------------------------------------------------------------
bool Bbs::begin(uint16_t port) {
    noteBoot();                       // why we are here, before anything else
    for (uint8_t i = 0; i < BBS_MAX_NODES; ++i) {
        nodes_[i].id   = static_cast<uint8_t>(i + 1);
        nodes_[i].role = Role::Caller;
        all_[i] = &nodes_[i];
    }
    busy_.id    = BBS_MAX_NODES + 1;
    busy_.role  = Role::Busy;
    sysop_.id   = 0;
    sysop_.role = Role::Sysop;
    all_[BBS_MAX_NODES]     = &busy_;
    all_[BBS_MAX_NODES + 1] = &sysop_;

    lfd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (lfd_ < 0) { plat::log("bbs: socket() failed errno %d", errno); return false; }

    int one = 1;
    setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(lfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        listen(lfd_, BBS_LISTEN_BACKLOG) < 0) {
        plat::log("bbs: bind/listen on %u failed errno %d", port, errno);
        close(lfd_);
        lfd_ = -1;
        return false;
    }
    setNonBlocking(lfd_);

    uint8_t coreCount = 0;
    const Command* core = coreCommands(coreCount);
    tables_[0]  = CommandTable{ core, coreCount, 0xFF };
    tableCount_ = 1;

    plat::HeapStats h = plat::heap();
    heapBaseline_ = h.freeBytes;
    plat::backupButtonBegin(syscfg::get().backupGpio);
    plat::activityLedBegin(syscfg::get().ledGpio);
    plat::log("bbs: %s %s listening on %u, %u nodes", BBS_NAME, BBS_VERSION, port, BBS_MAX_NODES);
    plat::log("bbs: session %u bytes, pool %u bytes (static), heap free %u",
              static_cast<unsigned>(sizeof(Session)),
              static_cast<unsigned>(sizeof(Session) * kSessions),
              static_cast<unsigned>(h.freeBytes));
    return true;
}

bool Bbs::registerCommands(const Command* list, uint8_t count, uint8_t plugin) {
    if (!list || !count || tableCount_ >= kCommandTables) return false;
    tables_[tableCount_++] = CommandTable{ list, count, plugin };
    return true;
}

// ---------------------------------------------------------------------------
// closeCardScreens: nobody may still be reading the card when it goes away.
//
// A ScreenPlayer holds an open file for as long as the screen is playing,
// and at a page break that is until the caller presses a key, which may be
// never. Unmounting under that leaves a descriptor pointing into a torn-down
// filesystem; on the board the VFS slot is reused by the next mount, so the
// stale handle can come back as somebody else's file rather than as an
// error. A screen that stops early is a far smaller thing.
//
// The caller is put back at the prompt rather than left staring at a half
// drawn screen with no way on.
// ---------------------------------------------------------------------------
void Bbs::closeCardScreens() {
    for (uint8_t i = 0; i < kSessions; ++i) {
        Session& s = *all_[i];
        if (s.st == SState::Free || !s.scr.onCard()) continue;
        s.scr.close();
        s.pendingPrompt = false;
        s.pendingForm   = FormKind::None;
        if (s.st == SState::AnyKey || s.st == SState::More || s.st == SState::Intro)
            s.st = SState::Shell;
        s.term.color(s.tl, Color::Grey);
        s.term.nl(s.tl);
        s.term.text(s.tl, "Screen ended: the card was removed.");
        if (s.loggedIn) prompt(s);
    }
}

// ---------------------------------------------------------------------------
// dropPluginCommands: forget every plugin's commands, keeping the core's.
//
// A config reload stops the plugins and starts them again, and each one
// registers its command table on the way up. Nothing was taking the old
// registrations back down, so the table grew by the number of running
// plugins on every reload until it was full, and from then on whichever
// plugins came last silently had no commands at all. Not a dangling pointer,
// because the tables are static, but a caller typing ANNOUNCE was told
// "Unknown command" on a board where the plugin was running fine.
//
// Table 0 is the core's and stays: it is registered in begin() and the core
// is not restarted by a reload.
// ---------------------------------------------------------------------------
void Bbs::dropPluginCommands() {
    for (uint8_t t = 1; t < tableCount_; ++t) tables_[t] = CommandTable{};
    tableCount_ = 1;
}

// ===========================================================================
// Plugin-facing helpers
// ===========================================================================

// ---------------------------------------------------------------------------
// own: hand the session's keys to a plugin until it releases them. The
// caller keeps their node and their call time; only the idle clock pauses.
// ---------------------------------------------------------------------------
bool Bbs::own(Session& s, uint8_t plugin) {
    if (s.st == SState::Free || s.role == Role::Busy || !s.loggedIn) return false;
    s.owner     = plugin;
    s.ownerData = 0;
    s.ed    = LineEditor();
    s.st    = SState::Plugin;
    return true;
}

void Bbs::release(Session& s) {
    if (s.owner == 0xFF) return;
    s.owner     = 0xFF;
    s.rawInput  = false;         // whatever was transferring is not any more
    s.ownerData = 0;
    if (s.st == SState::Plugin) {
        s.term.reset(s.tl);
        s.term.cursor(s.tl, true);
        prompt(s);
    }
}

bool Bbs::owns(const Session& s, uint8_t plugin) const {
    return s.owner == plugin && s.st == SState::Plugin;
}

// sayTo: a line on a caller's screen, then put back whatever they were at
void Bbs::sayTo(Session& s, Color c, const char* text) {
    if (s.st == SState::Free || s.fd < 0) return;
    Term& t = s.term;
    t.reset(s.tl);
    t.nl(s.tl);
    t.color(s.tl, c);
    t.text(s.tl, text);
    t.reset(s.tl);
    if (s.st != SState::Plugin) redrawInput(s);
}

void Bbs::eachSession(EachFn fn, void* ctx) {
    if (!fn) return;
    for (Session* s : all_) fn(ctx, *s);
}

void Bbs::setDoing(Session& s, const char* what) {
    strncpy(s.doing, what ? what : "", BBS_DOING_MAX);
    s.doing[BBS_DOING_MAX] = '\0';
}

uint8_t Bbs::activeNodes() const {
    uint8_t n = 0;
    for (const auto& s : nodes_) if (s.st != SState::Free) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// publicNodes / publicBusy: what the board looks like from outside.
//
// The sysop line is a seventh line that is not there most of the time and
// is hidden by default. When a sysop is on it and has made themselves
// visible, leaving them out means a board with somebody on it advertises
// itself as empty, so they count on both sides of the figure.
// ---------------------------------------------------------------------------
static bool sysopCounts(const Session& s) {
    return s.st != SState::Free && s.visible && !s.lurk;
}

const char* Bbs::preLoginName(const Session& s) {
    return (s.st == SState::Detect || s.st == SState::Intro) ? "(connecting)"
                                                             : "(logging in)";
}

uint8_t Bbs::publicNodes() const {
    return static_cast<uint8_t>(BBS_MAX_NODES + (sysopCounts(sysop_) ? 1 : 0));
}

uint8_t Bbs::publicBusy() const {
    return static_cast<uint8_t>(activeNodes() + (sysopCounts(sysop_) ? 1 : 0));
}

// ---------------------------------------------------------------------------
// tick: one pass of the cooperative scheduler
// ---------------------------------------------------------------------------
void Bbs::tick() {
    if (lfd_ < 0) return;

    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    int maxfd = lfd_;
    FD_SET(lfd_, &rfds);
    for (Session* s : all_) {
        if (s->fd < 0) continue;
        // backpressure: leave input in the socket while earlier input waits
        // or the output buffer is nearly full (a pasted burst must not
        // overflow the Timeline and lose output)
        if (!s->rxLen && s->tl.freeBytes() >= BBS_RX_ROOM) FD_SET(s->fd, &rfds);
        if (s->wantWrite) FD_SET(s->fd, &wfds);
        if (s->fd > maxfd) maxfd = s->fd;
    }
    backup_.addFds(rfds, wfds, maxfd);

    timeval tv;
    tv.tv_sec  = 0;
    tv.tv_usec = BBS_SELECT_MS * 1000;
    int r = select(maxfd + 1, &rfds, &wfds, nullptr, &tv);
    uint32_t now   = plat::millis();
    uint32_t work0 = plat::micros();        // time the work, not the wait
    if (r <= 0) { FD_ZERO(&rfds); FD_ZERO(&wfds); }

    if (FD_ISSET(lfd_, &rfds)) acceptAll(now);

    for (Session* s : all_) {
        if (s->fd >= 0 && s->rxLen)                    processInput(*s, now);   // held from before
        else if (s->fd >= 0 && FD_ISSET(s->fd, &rfds)) readSession(*s, now);
        if (s->st != SState::Free) serviceSession(*s, now);
    }

    backup_.service(rfds, wfds, now);
    serviceBackup(now);
    plugins::tick(now);
    plat::activityTick(now);

    uint32_t dt = plat::micros() - work0;
    if (dt > loopMaxUs_) loopMaxUs_ = dt;
    loopAvgUs_ = loopAvgUs_ ? (loopAvgUs_ * 7 + dt) / 8 : dt;    // gentle average
    ++loopPasses_;
}

// ===========================================================================
// Backup window
// ===========================================================================

// ---------------------------------------------------------------------------
// serviceBackup: button, notes to the sysop, the Y/N approval prompt
// ---------------------------------------------------------------------------
void Bbs::serviceBackup(uint32_t now) {
    bool sysopOn = sysop_.st != SState::Free && sysop_.loggedIn && sysop_.fd >= 0;

    if (plat::backupButtonPressed(now) && !backup_.isOpen()) {
        if (sysopOn) {
            char ip[16] = "0.0.0.0";
            sockaddr_in a;
            socklen_t al = sizeof(a);
            if (getsockname(sysop_.fd, reinterpret_cast<sockaddr*>(&a), &al) == 0) {
                ipToText(a.sin_addr.s_addr, ip, sizeof(ip));
            }
            backup_.open(now, ip);
        } else if (now - lastBtnLog_ > 10000u) {
            lastBtnLog_ = now;
            plat::log("backup: button pressed, but the sysop is not on the sysop node");
        }
    }

    // the window belongs to the sysop session that opened it
    if (!sysopOn && (backup_.isOpen() || backup_.awaitingApproval())) {
        backup_.decide(false, "sysop left");
        backup_.close("sysop left");
    }

    BackupService::Note n;
    while (backup_.poll(n)) {
        if (sysopOn) post(sysop_, BusKind::Notice, nullptr, n.text);
    }

    if (!backup_.awaitingApproval()) {
        if (sysop_.st == SState::Approve) {             // decided elsewhere (timeout)
            sysop_.term.nl(sysop_.tl);
            prompt(sysop_);
        }
        approvalShown_ = false;
        return;
    }
    if (!approvalShown_ && sysopOn && sysop_.st == SState::Shell && sysop_.ed.active() &&
        !sysop_.scr.active() && sysop_.tl.empty() && sysop_.mb.empty()) {
        showApproval(sysop_);
    }
}

void Bbs::showApproval(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    t.reset(tl);
    t.nl(tl);
    t.bell(tl);
    t.color(tl, Color::Yellow);
    t.text(tl, backup_.approvalSummary());
    t.nl(tl);
    t.color(tl, Color::Grey);
    t.text(tl, backup_.approvalDetail());
    t.nl(tl);
    t.color(tl, Color::Yellow);
    t.text(tl, "Accept upload (Y/N)? ");
    t.color(tl, Color::White);
    s.ed = LineEditor();
    s.st = SState::Approve;
    approvalShown_ = true;
}

// ===========================================================================
// Connections
// ===========================================================================

// ---------------------------------------------------------------------------
// acceptAll: banned IPs dropped, free node, else the busy line, else BUSY
// ---------------------------------------------------------------------------
void Bbs::acceptAll(uint32_t now) {
    for (;;) {
        sockaddr_in a;
        socklen_t   al = sizeof(a);
        int fd = accept(lfd_, reinterpret_cast<sockaddr*>(&a), &al);
        if (fd < 0) break;

        uint32_t ipAddr = a.sin_addr.s_addr;
        char ip[16];
        ipToText(ipAddr, ip, sizeof(ip));

        if (bans_.banned(ipAddr, now)) {
            close(fd);
            plat::log("bbs: BANNED %s dropped", ip);
            continue;
        }

        setNonBlocking(fd);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
        // a caller that vanishes without closing (C64 switched off) is
        // detected in about idle + interval * count seconds
        int ka[3] = { BBS_KEEPALIVE_IDLE_S, BBS_KEEPALIVE_INTVL_S, BBS_KEEPALIVE_CNT };
#if defined(TCP_KEEPIDLE) && defined(TCP_KEEPINTVL) && defined(TCP_KEEPCNT)
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE,  &ka[0], sizeof(int));
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &ka[1], sizeof(int));
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT,   &ka[2], sizeof(int));
#else
        (void)ka;
#endif
        plat::activityPulse(now);

        Session* slot = nullptr;
        for (auto& s : nodes_) {
            if (s.st == SState::Free) { slot = &s; break; }
        }
        if (slot) {
            openSession(*slot, fd, ip, ipAddr, Role::Caller, now);
            continue;
        }
        if (busy_.st == SState::Free) {
            openSession(busy_, fd, ip, ipAddr, Role::Busy, now);
            continue;
        }
        send(fd, kBusyMsg, sizeof(kBusyMsg) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
        close(fd);
        plat::log("bbs: BUSY  %s (overflow, dropped)", ip);
    }
}

// ---------------------------------------------------------------------------
// openSession: reset every per-call field and start detection
// ---------------------------------------------------------------------------
void Bbs::openSession(Session& s, int fd, const char* ip, uint32_t ipAddr, Role role, uint32_t now) {
    s.fd            = fd;
    s.st            = SState::Detect;
    s.role          = role;
    s.wantWrite     = false;
    s.negotiated    = false;
    s.rxLen         = 0;
    s.rxPos         = 0;
    s.rawInput      = false;     // a static pool: never inherit a transfer
    strncpy(s.ip, ip, sizeof(s.ip) - 1);
    s.ip[sizeof(s.ip) - 1] = '\0';
    s.ipAddr        = ipAddr;
    s.connectedAt   = now;
    s.lastInput     = now;
    s.lastRx        = now;
    s.closeAt       = 0;
    s.lingerAt      = 0;
    s.noLimits      = false;
    s.fxStep        = 0;
    s.savedCps      = 0;
    s.pendingPrompt = false;
    s.pendingForm   = FormKind::None;
    s.afterKey      = AfterKey::Prompt;
    s.pendingTail   = false;
    s.pendingKnowMore = false;
    s.newAccount      = false;
    s.user[0]       = '\0';

    s.loggedIn      = false;
    s.loginAt       = 0;
    s.loginEpoch    = 0;
    s.dayUsedMin    = 0;
    s.timeAdjMin    = 0;
    s.timeWarned    = 0;
    s.idleWarned    = false;
    s.busyLoginUntil = 0;
    s.guest         = false;
    s.rank          = 0;
    s.doing[0]      = '\0';

    s.list          = ListKind::None;
    s.listIdx       = 0;
    s.listSub       = 0;
    s.pageLines     = 0;
    s.nonstop       = false;
    s.moreFrom      = MoreFrom::List;
    s.watch         = ListKind::None;
    s.watchSecs     = 0;
    s.watchNext     = 0;
    s.countdown     = 0;
    s.nextTick      = 0;

    s.passTries     = 0;
    s.formKind      = FormKind::None;
    s.confirm       = ConfirmKind::Logoff;
    s.backToUsers   = false;
    s.ulSel = s.ulTop = s.ulCount = 0;
    memset(s.pwA, 0, sizeof(s.pwA));
    memset(s.pwB, 0, sizeof(s.pwB));
    memset(s.pwC, 0, sizeof(s.pwC));
    s.origHandle[0] = '\0';
    s.edit          = UserRec();
    s.level         = Access::None;
    s.perms         = 0;
    s.dnd           = false;
    s.visible       = true;
    s.lurk          = false;
    s.histPos       = -1;
    s.snooper       = nullptr;

    if (role == Role::Caller) {                  // what the board has handled since boot
        ++callsBoot_;
        uint8_t busy = static_cast<uint8_t>(activeNodes() + 1);
        if (busy > peakNodes_) peakNodes_ = busy;
    }

    s.tl.clear();
    s.tl.setCps(0);
    s.tn.reset();
    s.scr.close();
    s.term.setType(TermType::Unknown, Charset::Ascii, 80, 24);
    s.term.setIacEscape(false);
    s.ed   = LineEditor();
    s.hist = LineHistory();
    s.mb.clear();
    s.det.start(now);

    s.owner         = 0xFF;
    plat::HeapStats h = plat::heap();
    s.heapAtOpen = h.freeBytes;
    if (role == Role::Busy) {
        plat::log("bbs: busy line CONNECT %s (all nodes in use)", s.ip);
    } else {
        plat::log("bbs: node %u CONNECT %s  nodes %u/%u  heap free %u",
                  s.id, s.ip, activeNodes(), BBS_MAX_NODES, static_cast<unsigned>(h.freeBytes));
    }
}

// ---------------------------------------------------------------------------
// closeSession: log the call, bank the minutes, tell the others, release
// ---------------------------------------------------------------------------
void Bbs::closeSession(Session& s, const char* why, uint32_t now) {
    configRelease(s);                        // a dropped line must not lock CONFIG out

    // snoop links in both directions
    for (Session* o : all_) if (o->snooper == &s) o->snooper = nullptr;
    if (s.snooper) {
        Session* w = s.snooper;
        s.snooper = nullptr;
        if (w->st == SState::Snoop) stopSnoop(*w, "Node hung up.");
    }

    if (s.loggedIn) {
        for (uint8_t i = 0; i < plugins::count(); ++i) {          // tell the plugins first
            const Plugin* p = plugins::at(i);
            if (plugins::running(i) && p->onLogoff) p->onLogoff(s);
            if (plugins::running(i) && p->onPresence) p->onPresence(s);
        }
        s.owner = 0xFF;
        uint32_t secs = (now - s.loginAt) / 1000u;
        CallRec r;
        strncpy(r.user, s.user, BBS_USER_MAX);
        strncpy(r.ip, s.ip, sizeof(r.ip) - 1);
        r.node    = s.id;
        r.term    = static_cast<uint8_t>(s.term.type());
        r.charset = static_cast<uint8_t>(s.term.charset());
        uint8_t rank = s.rank > static_cast<uint8_t>(s.level) ? s.rank : static_cast<uint8_t>(s.level);
        r.flags   = static_cast<uint8_t>((s.role == Role::Sysop ? CallRec::F_SYSOP : 0) |
                                         (s.guest ? CallRec::F_GUEST : 0) |
                                         (!s.guest && rank >= static_cast<uint8_t>(Access::Sysop)
                                              ? CallRec::F_RANK_SYSOP
                                              : (!s.guest && rank ? CallRec::F_RANK_CO : 0)));
        r.start   = s.loginEpoch;
        r.secs    = secs;
        calllog::append(r);

        saveCallStats(s, now);
        if (s.role == Role::Caller) {
            if (s.visible && !s.lurk) {             // hidden co-sysops leave quietly
                char msg[BBS_USER_MAX + 32];
                snprintf(msg, sizeof(msg), "*** %s left node %u", s.user, s.id);
                noticeAll(s, msg);
            }
        }
        s.loggedIn = false;
    }

    memset(s.pwA, 0, sizeof(s.pwA));                 // typed passwords never outlive the call
    memset(s.pwB, 0, sizeof(s.pwB));
    memset(s.pwC, 0, sizeof(s.pwC));
    s.form.wipe();
    if (s.fd >= 0) close(s.fd);
    s.fd = -1;
    s.scr.close();
    s.tl.clear();
    s.mb.clear();
    s.list = ListKind::None;
    s.st   = SState::Free;

    plat::HeapStats h = plat::heap();
    plat::log("bbs: node %s HANGUP (%s)  nodes %u/%u  heap free %u",
              nodeName(s).t, why, activeNodes(), BBS_MAX_NODES, static_cast<unsigned>(h.freeBytes));
}

// ---------------------------------------------------------------------------
// moveSession: carry a live caller to another slot (sysop elevation, DROP).
// Member-wise copy keeps socket, terminal state and pending output; the
// source slot is released without closing anything.
// ---------------------------------------------------------------------------
void Bbs::moveSession(Session& from, Session& to, uint8_t newId, Role role) {
    to      = from;
    to.id   = newId;
    to.role = role;

    from.fd       = -1;
    from.st       = SState::Free;
    from.loggedIn = false;
    from.snooper  = nullptr;
    from.list     = ListKind::None;
    from.scr.detach();
    from.tl.clear();
    from.mb.clear();
}

// ---------------------------------------------------------------------------
// readSession: socket -> telnet filter -> detector or key events
// ---------------------------------------------------------------------------
void Bbs::readSession(Session& s, uint32_t now) {
    uint8_t raw[BBS_RX_CHUNK];
    uint8_t data[BBS_RX_CHUNK];

    ssize_t n = recv(s.fd, raw, sizeof(raw), MSG_DONTWAIT);
    if (n == 0) { closeSession(s, "remote", now); return; }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        closeSession(s, errno == ECONNRESET ? "remote" : "read error", now);
        return;
    }

    plat::activityPulse(now);
    DirectSink ds(s.fd);
    size_t m = s.tn.filter(raw, static_cast<size_t>(n), data, ds);
    s.lastRx = now;

    // a telnet client spoke first: character mode now, then probe at once
    if (s.st == SState::Detect && s.tn.clientSpoke() && !s.negotiated) {
        s.tn.negotiate(ds);
        s.negotiated = true;
        s.det.probeNow(now, s.tl);
    }

    if (s.term.isAnsi() && s.tn.hasSize()) s.term.setGeometry(s.tn.cols(), s.tn.rows());

    memcpy(s.rxBuf, data, m);
    s.rxLen = static_cast<uint8_t>(m);
    s.rxPos = 0;
    processInput(s, now);
}

// ---------------------------------------------------------------------------
// processInput: handle held input bytes while the Timeline has room; the
// rest waits for the next pass (the socket is not read meanwhile)
// ---------------------------------------------------------------------------
void Bbs::processInput(Session& s, uint32_t now) {
    KeyCtx kc{ this, &s, now };
    while (s.rxPos < s.rxLen) {
        if (s.st == SState::Free) { s.rxLen = s.rxPos = 0; return; }
        if (s.st != SState::Detect && s.tl.freeBytes() < BBS_RX_ROOM) return;
        // A plugin in raw mode gets what is left of the buffer in one go,
        // undecoded. One call rather than a byte at a time, because a
        // transfer wants blocks and calling a protocol engine 1,024 times
        // for a 1K block is work for nothing.
        if (s.rawInput && s.owner != 0xFF && plugins::running(s.owner)) {
            // This runs before the byte is read below, so everything from
            // rxPos to rxLen is still theirs. An earlier version decremented
            // rxPos here as though a byte had already been taken, which on
            // the first pass underflowed a uint8_t to 255 and read a long
            // way past the buffer.
            const Plugin* p = plugins::at(s.owner);
            size_t left = static_cast<size_t>(s.rxLen - s.rxPos);
            if (p && p->onBytes && left) p->onBytes(s, s.rxBuf + s.rxPos, left, now);
            s.lastRx = now;
            s.rxLen = s.rxPos = 0;
            return;
        }
        uint8_t b = s.rxBuf[s.rxPos++];
        s.lastRx = now;                              // keeps ESC [ B from splitting into ESC
        if (s.st == SState::Detect) {
            if (s.det.feed(b, now, s.tl) == Detector::Result::Done) onDetected(s, now);
            continue;
        }
        s.term.feed(b, keyTramp, &kc);
    }
    s.rxLen = s.rxPos = 0;
}

// ---------------------------------------------------------------------------
// flush: drain the Timeline to the socket
// ---------------------------------------------------------------------------
void Bbs::flush(Session& s, uint32_t now) {
    if (s.fd < 0 || s.tl.empty()) return;
    s.wantWrite = false;
    int sent = s.tl.pump(now, sessSend, &s);
    if (sent < 0) closeSession(s, "write error", now);
    else if (sent > 0) plat::activityPulse(now);
}

// ---------------------------------------------------------------------------
// serviceSession: timers, screens, lists, effects, mail, output
// ---------------------------------------------------------------------------
void Bbs::serviceSession(Session& s, uint32_t now) {
    ScreenPlayer::Vars vars{ s.user, s.id, BBS_MAX_NODES };
    Term& t = s.term;
    Timeline& tl = s.tl;

    switch (s.st) {
        case SState::Detect: {
            Detector::Result r = s.det.tick(now, tl);
            if (r == Detector::Result::Done)    onDetected(s, now);
            if (r == Detector::Result::Timeout) hangup(s, "NO RESPONSE", now);
            break;
        }
        case SState::Intro:
            if (s.scr.active()) s.scr.pump(t, tl, vars);
            if (!s.scr.active()) {
                loginHint(s);
                askName(s);
            }
            break;
        case SState::Shell:
            if (s.scr.active()) {
                bool more = s.scr.pump(t, tl, vars);
                if (s.scr.pageBreak()) {         // the screen asked for a page turn
                    pauseFor(s, AfterKey::ScreenNext);
                } else if (s.scr.paused()) {
                    showMore(s, MoreFrom::Screen);
                } else if (!more && s.pendingForm != FormKind::None) {
                    s.pendingForm = FormKind::None;  // a screen that leads into a form
                    pauseFor(s, AfterKey::SignupForm);   // let them read it first
                } else if (!more && s.pendingKnowMore) {
                    s.pendingKnowMore = false;   // rules read, now the warning
                    pauseFor(s, AfterKey::KnowMore);
                } else if (!more && s.pendingLand) {
                    // Before pendingPrompt, because playScreen set that too
                    // and landing does its own finishing.
                    s.pendingLand   = false;
                    s.pendingPrompt = false;
                    landAfterLogin(s);
                } else if (!more && s.pendingPrompt) {
                    s.pendingPrompt = false;
                    prompt(s);
                }
            }
            break;
        case SState::List:
            serviceList(s);
            break;
        case SState::Watch:
            serviceWatch(s, now);
            break;
        case SState::Fx:
            if (tl.empty()) fxNext(s);
            break;
        case SState::BusyWait:
            if (s.scr.active()) {
                s.scr.pump(t, tl, vars);
                break;
            }
            if (s.countdown == 0xFF) {                     // screen done: start counting
                t.reset(tl);
                t.nl(tl);
                t.color(tl, Color::Grey);
                t.text(tl, "Disconnecting in ");
                t.color(tl, Color::White);
                s.countdown = BBS_BUSY_COUNTDOWN;
                char num[4];
                snprintf(num, sizeof(num), "%u", s.countdown);
                t.text(tl, num);
                s.nextTick = now + 1000;
            } else if (static_cast<int32_t>(now - s.nextTick) >= 0) {
                char num[4];
                snprintf(num, sizeof(num), "%u", s.countdown);
                t.eraseBack(tl, static_cast<uint8_t>(strlen(num)));
                --s.countdown;
                snprintf(num, sizeof(num), "%u", s.countdown);
                t.text(tl, num);
                s.nextTick += 1000;
                if (s.countdown == 0) {
                    fx::hangup(t, tl);
                    s.st      = SState::Closing;
                    s.closeAt = now + 3000;
                }
            }
            break;
        case SState::Closing:
            if (s.scr.active()) {
                if (!s.scr.pump(t, tl, vars) && s.pendingTail) {
                    s.pendingTail = false;
                    fx::lineNoise(t, tl, 12, 300);
                    fx::hangup(t, tl);
                }
            }
            flush(s, now);
            if (s.st == SState::Closing) {
                bool drained = tl.empty() && !s.scr.active();
                // Start the clock the moment the last byte is away, not when
                // the send-off began: a slow terminal gets the same five
                // seconds to look at it as a fast one.
                if (drained && !s.lingerAt) s.lingerAt = now + BBS_EXIT_LINGER_MS;
                bool lingered = drained && static_cast<int32_t>(now - s.lingerAt) >= 0;
                if (lingered || static_cast<int32_t>(now - s.closeAt) >= 0)
                    closeSession(s, "hangup", now);
            }
            return;
        default:
            break;
    }

    if (s.st == SState::Free) return;

    if (static_cast<int32_t>(now - s.lastRx) >= static_cast<int32_t>(kEscIdleMs)) {
        KeyCtx kc{ this, &s, now };
        t.idle(keyTramp, &kc);
    }

    checkTimers(s, now);
    deliverMail(s);
    flush(s, now);
}

// ===========================================================================
// Flow
// ===========================================================================

// ---------------------------------------------------------------------------
// onDetected: lock the terminal, set telnet mode, start intro or busy
// ---------------------------------------------------------------------------
void Bbs::onDetected(Session& s, uint32_t now) {
    s.term.setType(s.det.type(), s.det.charset(), s.det.cols(), s.det.rows());

    bool telnet = false;
    if (s.term.isPet()) {
        telnet = s.tn.clientSpoke();          // raw C64 clients never see IAC
        s.tn.setEnabled(telnet);
    } else if (s.term.isAnsi() || s.tn.clientSpoke()) {
        if (!s.negotiated) {
            DirectSink ds(s.fd);
            s.tn.negotiate(ds);
            s.negotiated = true;
        }
        telnet = true;
    }
    s.term.setIacEscape(telnet);
    if (s.term.isAnsi() && s.tn.hasSize()) s.term.setGeometry(s.tn.cols(), s.tn.rows());

    plat::log("bbs: node %s %s (telnet %s)", nodeName(s).t, s.term.name(), telnet ? "on" : "off");
    for (uint8_t i = 0; i < plugins::count(); ++i) {
        const Plugin* p = plugins::at(i);
        if (plugins::running(i) && p->onConnect) p->onConnect(s);
    }
    if (s.role == Role::Busy) startBusy(s, now);
    else                      startIntro(s);
}

// ---------------------------------------------------------------------------
// startIntro: detected banner, then the welcome screen
// ---------------------------------------------------------------------------
void Bbs::startIntro(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    char line[48];

    t.init(tl);
    t.color(tl, Color::LightGreen);
    snprintf(line, sizeof(line), "%s DETECTED", t.name());
    fx::typewriter(t, tl, line, 25);
    t.nl(tl);
    t.color(tl, Color::Grey);
    snprintf(line, sizeof(line), "Opening node %u ", s.id);
    fx::working(t, tl, line, 700, "OK");
    t.reset(tl);
    fx::pause(tl, 300);

    s.st = SState::Intro;
    if (!s.scr.open("welcome", t)) {
        t.cls(tl);
        t.color(tl, Color::White);
        t.text(tl, BBS_NAME);
        t.nl(tl);
        t.color(tl, Color::Cyan);
        fx::rule(t, tl, static_cast<uint8_t>(t.cols() > 40 ? 40 : t.cols() - 2));
        t.nl(tl);
        t.color(tl, Color::Grey);
        t.text(tl, "No welcome screen found.\nUpload data/ with: pio run -t uploadfs\n");
        t.reset(tl);
    }
}

// ---------------------------------------------------------------------------
// startBusy: busy screen, then a countdown (BusyWait state). A key during
// the countdown opens a login that only a sysop password gets past.
// ---------------------------------------------------------------------------
void Bbs::startBusy(Session& s, uint32_t now) {
    (void)now;
    Term& t = s.term;
    Timeline& tl = s.tl;

    t.init(tl);
    s.st        = SState::BusyWait;
    s.countdown = 0xFF;                       // not started until the screen ends
    if (!s.scr.open("busy", t)) {
        t.color(tl, Color::White);
        t.text(tl, BBS_NAME);
        t.nl(tl);
        t.color(tl, Color::LightRed);
        t.text(tl, "Sorry, all lines are busy.");
        t.nl(tl);
        t.color(tl, Color::Grey);
        t.text(tl, "Please try your call later.");
        t.nl(tl);
    }
}

void Bbs::drawNamePrompt(Session& s) {
    Term& t = s.term;
    t.reset(s.tl);
    t.nl(s.tl);
    t.color(s.tl, Color::Cyan);
    t.text(s.tl, "Enter your handle: ");
    t.color(s.tl, Color::White);
}

void Bbs::askName(Session& s) {
    drawNamePrompt(s);
    armName(s);
}

// armName: take a handle on the current line (after an in-place error)
void Bbs::armName(Session& s) {
    s.ed.begin(BBS_USER_MAX, LineEditor::F_STAY);
    s.st         = SState::AskName;
    s.lastInput  = plat::millis();            // the 30/60 s clock starts at the prompt
    s.idleWarned = false;
}

// ---------------------------------------------------------------------------
// loginHint: one line above the first handle prompt saying how to get in
// ---------------------------------------------------------------------------
void Bbs::loginHint(Session& s) {
    const SysConfig& cfg = syscfg::get();
    const char* hint = nullptr;
    if (cfg.guestEnabled && cfg.selfRegister) hint = "New? Type a handle to join or visit.";
    else if (cfg.guestEnabled)                hint = "New? Type a handle to visit as a guest.";
    else if (cfg.selfRegister)                hint = "New? Type a handle to join.";
    if (!hint) return;
    s.term.reset(s.tl);
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, hint);
}

// ---------------------------------------------------------------------------
// inputError: rub out what was typed, flash the reason in its place, rub
// that out too. The line stays put, so the caller can simply retype.
// used = columns already on the line before the input (the prompt).
// ---------------------------------------------------------------------------
void Bbs::inputError(Session& s, uint8_t used, const char* longMsg, const char* shortMsg) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    uint8_t room = static_cast<uint8_t>(t.cols() > used + 1 ? t.cols() - used - 1 : 0);
    const char* msg = strlen(longMsg) <= room ? longMsg : shortMsg;
    if (strlen(msg) > room) msg = "";
    fx::rubout(t, tl, s.ed.shown(), 18);
    t.bell(tl);
    t.color(tl, Color::LightRed);
    fx::typeRubout(t, tl, msg, 12, 1100, 10);
    t.color(tl, Color::White);
}

// ---------------------------------------------------------------------------
// onHandle: one prompt for everyone. A known handle asks for its password;
// an unknown one offers [R]egister and/or [G]uest (per system.cfg) or a
// new handle. The editor leaves the cursor on the handle line: errors rub
// out in place, every other path starts its own line.
// ---------------------------------------------------------------------------
void Bbs::onHandle(Session& s, uint32_t now) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    constexpr uint8_t kPromptLen = 19;               // "Enter your handle: "

    const char* p = s.ed.text();
    while (*p == ' ') ++p;
    size_t n = strlen(p);
    while (n && p[n - 1] == ' ') --n;
    if (!n) {                                        // just Enter: keep waiting on this line
        fx::rubout(t, tl, s.ed.shown(), 0);
        armName(s);
        return;
    }
    if (n > BBS_USER_MAX) n = BBS_USER_MAX;

    char name[BBS_USER_MAX + 1];
    memcpy(name, p, n);
    name[n] = '\0';

    if (ieq(name, "SYSOP")) {
        inputError(s, kPromptLen, "That handle is reserved.", "Reserved handle");
        armName(s);
        return;
    }
    if (!users::validHandle(name)) {                 // also stops terminal junk like "[3;20R"
        inputError(s, kPromptLen, "Use letters, digits, space - _ .", "A-Z 0-9 space - _ .");
        armName(s);
        return;
    }

    if (s.role == Role::Busy) {                      // busy line: no node, no account
        memcpy(s.user, name, n + 1);
        t.nl(tl);
        t.color(tl, Color::Grey);
        t.text(tl, "All nodes are in use.");
        t.nl(tl);
        prompt(s);
        return;
    }

    memcpy(s.user, name, n + 1);
    users::Lookup found = users::lookup(name, s.edit);
    if (found == users::Lookup::Error) {                // accounts unreadable: refuse, never
        s.user[0] = '\0';                               // offer the handle as new
        plat::log("bbs: node %u users.txt unreadable at login", s.id);
        inputError(s, kPromptLen, "Accounts are unavailable. Try again shortly.", "Try again shortly");
        armName(s);
        return;
    }
    if (found == users::Lookup::Found) {
        strncpy(s.user, s.edit.handle, BBS_USER_MAX);   // the account's own spelling
        if (s.edit.locked) {
            plat::log("bbs: node %u locked account '%s'", s.id, s.user);
            hangup(s, "This account is locked. Ask the sysop.", now);
            return;
        }
        if (logins_.locked(s.user, now)) {
            hangup(s, "Too many wrong passwords. Try again later.", now);
            return;
        }
        askPassword(s);
        return;
    }

    // unknown handle
    const SysConfig& cfg = syscfg::get();
    if (handleOnline(s, name)) {                     // a guest is already using it
        s.user[0] = '\0';
        inputError(s, kPromptLen, "That handle is online right now.", "Handle in use");
        armName(s);
        return;
    }
    bool canRegister = cfg.selfRegister && users::count() < cfg.maxUsers;
    if (!canRegister && !cfg.guestEnabled) {
        s.user[0] = '\0';
        if (cfg.selfRegister) inputError(s, kPromptLen, "Sign-ups are closed: the BBS is full.", "BBS is full");
        else                  inputError(s, kPromptLen, "No account. The sysop creates accounts here.", "No such account");
        armName(s);
        return;
    }

    t.reset(tl);
    t.nl(tl);
    t.nl(tl);                                        // clear of what they typed
    t.color(tl, Color::White);
    t.text(tl, s.user);
    t.color(tl, Color::Yellow);
    fx::typewriter(t, tl, " is new here.", 12);
    t.nl(tl);                                        // a beat before the question
    t.nl(tl);
    t.color(tl, Color::Yellow);
    if (canRegister && cfg.guestEnabled) t.text(tl, "[R]egister, [G]uest or [N]ew handle? ");
    else if (canRegister)                t.text(tl, "[R]egister or [N]ew handle? ");
    else                                 t.text(tl, "[G]uest or [N]ew handle? ");
    t.color(tl, Color::White);
    s.st        = SState::AskRegister;
    s.lastInput = plat::millis();
}

// ---------------------------------------------------------------------------
// onNewHandle: the answer to [R]egister / [G]uest / [N]ew handle
// ---------------------------------------------------------------------------
void Bbs::onNewHandle(Session& s, int k, uint32_t now) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    const SysConfig& cfg = syscfg::get();
    bool canRegister = cfg.selfRegister && users::count() < cfg.maxUsers;
    bool reg   = (k == 'r' || k == 'R') && canRegister;
    bool guest = (k == 'g' || k == 'G') && cfg.guestEnabled;

    if ((reg || guest) && handleOnline(s, s.user)) {  // taken while this caller chose
        t.ch(tl, reg ? 'R' : 'G');
        t.nl(tl);
        t.color(tl, Color::LightRed);
        t.text(tl, "That handle just came online.");
        s.user[0] = '\0';
        askName(s);
        return;
    }
    if (reg) {
        t.ch(tl, 'R');
        t.nl(tl);
        showRules(s);
    } else if (guest) {
        t.ch(tl, 'G');
        loginGuest(s, now);
    } else if (k == 'n' || k == 'N' || k == KEY_ESC || k == KEY_BREAK) {
        t.ch(tl, 'N');
        s.user[0] = '\0';
        askName(s);
    }
}

// ---------------------------------------------------------------------------
// showRules: pressing R gets the house rules before anything else. They are
// the terms of the place, so they come before the sign-up form rather than
// after it, and before the encryption warning, which stays immediately in
// front of the password where it does the most good.
//
// A board with no rules screen loses nothing: the flow carries straight on.
// ---------------------------------------------------------------------------
void Bbs::showRules(Session& s) {
    s.term.cls(s.tl);
    if (playScreen(s, "rules")) {
        s.pendingPrompt   = false;       // not back to a prompt: on to the warning
        s.pendingKnowMore = true;
        return;
    }
    askKnowMore(s);
}

// ---------------------------------------------------------------------------
// askKnowMore: before anybody types a password, say plainly that the link
// is not encrypted, and offer the whole story to those who want it.
// ---------------------------------------------------------------------------
void Bbs::askKnowMore(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    t.color(tl, Color::Yellow);
    t.text(tl, t.cols() >= 64 ? "This connection is not encrypted. Use a password"
                              : "Not encrypted. Use a password");
    t.nl(tl);
    t.text(tl, t.cols() >= 64 ? "you do not use anywhere else." : "you use nowhere else.");
    t.nl(tl);
    t.color(tl, Color::LightGreen);
    t.text(tl, "Would you like to know more? ");
    t.color(tl, Color::White);
    t.text(tl, "[Y/N] ");
    t.cursor(tl, true);
    s.st = SState::AskKnowMore;
}

// ---------------------------------------------------------------------------
// onKnowMore: Y plays screens/privacy.*, and the sign-up form opens when it
// finishes. Anything else goes straight to the form.
// ---------------------------------------------------------------------------
void Bbs::onKnowMore(Session& s, int k, uint32_t now) {
    Term& t = s.term;
    if (k == 'y' || k == 'Y') {
        t.ch(s.tl, 'Y');
        t.nl(s.tl);
        showPrivacy(s, AfterKey::SignupForm);
        return;
    } else if (k == 'n' || k == 'N' || k == KEY_ENTER || k == KEY_ESC || k == KEY_BREAK) {
        t.ch(s.tl, 'N');
        t.nl(s.tl);
        t.nl(s.tl);                                  // room to breathe before the form
    } else {
        return;                                      // any other key: keep waiting
    }
    startForm(s, FormKind::Signup, now);
}

// ---------------------------------------------------------------------------
// pauseFor: stop and let somebody read what was just printed. Anything that
// clears the screen next, a form or another screen, would otherwise wipe it
// before it could be read.
// ---------------------------------------------------------------------------
void Bbs::pauseFor(Session& s, AfterKey then) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    t.nl(tl);
    t.color(tl, Color::Cyan);
    t.text(tl, t.isPet() ? "PRESS SPACE TO CONTINUE" : "Press SPACE to continue");
    t.cursor(tl, true);
    s.afterKey = then;
    s.st       = SState::AnyKey;
}

// onAnyKey: any key at all, then on with whatever we paused in front of
void Bbs::onAnyKey(Session& s, uint32_t now) {
    s.term.nl(s.tl);
    AfterKey then = s.afterKey;
    s.afterKey = AfterKey::Prompt;
    s.st       = SState::Shell;

    if (then == AfterKey::ScreenNext) {           // next page of a screen
        s.term.cls(s.tl);
        s.scr.resume();
        return;                                   // the player takes it from here
    }
    if (then == AfterKey::SignupForm) startForm(s, FormKind::Signup, now);
    else if (then == AfterKey::KnowMore) askKnowMore(s);   // rules read, now the warning
    else                              prompt(s);
}

// ---------------------------------------------------------------------------
// showPrivacy: play screens/privacy.*, which is an ordinary screen file so a
// sysop can say this in their own words. It uses form feeds as page breaks.
// The built-in text below is only the fallback for a board whose screens are
// missing: the real version lives on the filesystem, where text belongs.
// ---------------------------------------------------------------------------
void Bbs::showPrivacy(Session& s, AfterKey then) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    t.cls(tl);
    if (playScreen(s, "privacy")) {
        s.pendingPrompt = then != AfterKey::SignupForm;
        s.pendingForm   = then == AfterKey::SignupForm ? FormKind::Signup : FormKind::None;
        return;
    }

    t.color(tl, Color::Cyan);                     // no screen file: the short version
    t.text(tl, "TELNET IS NOT ENCRYPTED");
    t.nl(tl);
    t.nl(tl);
    t.color(tl, Color::Grey);
    t.text(tl, "Everything you type crosses the network");
    t.nl(tl);
    t.text(tl, "readable, including your password. On the");
    t.nl(tl);
    t.text(tl, "board it is salted and hashed, which");
    t.nl(tl);
    t.text(tl, "protects the file, not the wire.");
    t.nl(tl);
    t.nl(tl);
    t.color(tl, Color::LightGreen);
    t.text(tl, "Use a password you use nowhere else.");
    t.nl(tl);
    pauseFor(s, then);
}

// handleOnline: another logged-in session uses this handle
bool Bbs::handleOnline(const Session& s, const char* handle) const {
    for (const Session* o : all_) {
        if (o == &s || !o->user[0] || !ieq(o->user, handle)) continue;
        if (o->loggedIn) return true;
        if (o->st == SState::AskRegister) return true;            // choosing R or G
        if (o->st == SState::AskKnowMore) return true;            // reading the disclosure
        if (o->st == SState::Form && o->formKind == FormKind::Signup) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// loginGuest: the typed handle, no account, no password, nothing saved.
// guest_minutes per call, no daily limit. Lists mark guests with '*'.
// ---------------------------------------------------------------------------
void Bbs::loginGuest(Session& s, uint32_t now) {
    Term& t = s.term;
    Timeline& tl = s.tl;

    s.guest = true;
    s.edit  = UserRec();
    plat::log("bbs: node %u guest login '%s'", s.id, s.user);

    t.reset(tl);
    t.nl(tl);
    t.color(tl, Color::Grey);
    fx::working(t, tl, "Issuing guest pass ", 700, "");
    t.color(tl, Color::Yellow);
    fx::scramble(t, tl, "GUEST ACCESS", 10, 55);
    t.nl(tl);
    completeLogin(s, now);
}

void Bbs::askPassword(Session& s) {
    Term& t = s.term;
    t.reset(s.tl);
    t.nl(s.tl);
    t.color(s.tl, Color::Cyan);
    fx::typewriter(t, s.tl, "Password: ", 15);
    t.color(s.tl, Color::White);
    s.ed.begin(BBS_PASS_MAX, LineEditor::F_MASK | LineEditor::F_STAY);
    s.st        = SState::AskPass;
    s.lastInput = plat::millis();
}

// ---------------------------------------------------------------------------
// onPassword: verify in place. The stars spin, rub out and turn into
// ACCESS GRANTED, or ACCESS DENIED flashes and clears for another try on
// the same line. Three misses per call hang up, five per handle in
// 15 minutes lock it (RAM only).
// ---------------------------------------------------------------------------
void Bbs::onPassword(Session& s, uint32_t now) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    static constexpr char kDenied[] = "ACCESS DENIED";

    uint8_t stars = s.ed.shown();
    t.color(tl, Color::Grey);
    fx::spinner(t, tl, fx::Spin::Line, 650, 90);
    static UserRec fresh;                            // static: off the task stack
    users::Lookup found = users::lookup(s.user, fresh);
    bool ok = found == users::Lookup::Found && users::checkPassword(fresh, s.ed.text());
    s.ed = LineEditor();                             // wipe the typed password
    fx::rubout(t, tl, stars, 20);

    if (found == users::Lookup::Found && (fresh.locked || logins_.locked(s.user, now))) {
        s.edit = fresh;
        hangup(s, fresh.locked ? "This account is locked. Ask the sysop."
                               : "Too many wrong passwords. Try again later.", now);
        return;
    }
    if (found == users::Lookup::Missing) {           // deleted while typing
        hangup(s, "That account is gone.", now);
        return;
    }
    if (found == users::Lookup::Error) {
        hangup(s, "Accounts are unavailable. Try again shortly.", now);
        return;
    }
    s.edit = fresh;

    if (ok) {
        logins_.clear(s.user);
        t.color(tl, Color::LightGreen);
        fx::scramble(t, tl, "ACCESS GRANTED", 10, 55);
        t.nl(tl);
        t.nl(tl);            // the welcome crowded straight onto the password
        completeLogin(s, now);
        return;
    }

    ++s.passTries;
    bool lockedNow = logins_.fail(s.user, now);
    plat::log("bbs: node %u wrong password for '%s' (%u)", s.id, s.user, s.passTries);
    fx::lineNoise(t, tl, 14, 350);
    t.bell(tl);
    t.color(tl, Color::LightRed);
    fx::blink(t, tl, kDenied, 3, 140);
    if (lockedNow || s.passTries >= BBS_LOGIN_TRIES) {
        hangup(s, "Too many wrong passwords.", now);
        return;
    }
    fx::pause(tl, 500);
    fx::rubout(t, tl, sizeof(kDenied) - 1, 15);
    t.color(tl, Color::White);
    s.ed.begin(BBS_PASS_MAX, LineEditor::F_MASK | LineEditor::F_STAY);
    s.st        = SState::AskPass;
    s.lastInput = plat::millis();
}

// ---------------------------------------------------------------------------
// completeLogin: daily limit, greeting, arrival notice, bulletin, prompt.
// s.edit holds the account.
// ---------------------------------------------------------------------------
void Bbs::completeLogin(Session& s, uint32_t now) {
    Term& t = s.term;
    Timeline& tl = s.tl;

    const SysConfig& cfg = syscfg::get();
    s.dayUsedMin = (!s.guest && s.edit.dayKey == clk::dayKey(now)) ? s.edit.dayMinutes : 0;
    if (!s.guest && cfg.dayMinutes && s.dayUsedMin >= cfg.dayMinutes) {
        plat::log("bbs: node %u '%s' over daily limit", s.id, s.user);
        hangup(s, "Daily time limit reached. Call back tomorrow.", now);
        return;
    }

    s.loggedIn   = true;
    s.rank       = s.guest ? 0 : s.edit.level;
    s.loginAt    = now;
    s.loginEpoch = clk::epoch();
    s.timeWarned = 0;

    char buf[96];          // full sentences need more room than fragments did
    t.reset(tl);
    t.color(tl, Color::LightGreen);
    fx::typewriter(t, tl, s.edit.calls ? "Welcome back, " : "Welcome, ", 18);
    t.color(tl, Color::Yellow);
    t.text(tl, s.user);
    t.color(tl, Color::LightGreen);
    t.text(tl, "!");
    t.nl(tl);

    // Sentences rather than a column of fragments. A caller arriving should
    // be told where they are and how long they have, the way a person would
    // say it. The NTP line is a small brag and it also tells a sysop the
    // clock is real rather than whatever the chip powered up believing.
    t.color(tl, Color::Grey);
    bool wide = t.cols() >= 60;
    if (clk::valid()) {
        char when[40];
        clk::fmt(when, sizeof(when), "%a %d %b at %H:%M");
        snprintf(buf, sizeof(buf), wide ? "Connected to node %u of %u on %s."
                                        : "Node %u of %u, %s.",
                 s.id, BBS_MAX_NODES, when);
    } else {
        snprintf(buf, sizeof(buf), "Connected to node %u of %u.",
                 s.id, BBS_MAX_NODES);
    }
    t.text(tl, buf);
    t.nl(tl);
    if (clk::valid() && wide) {
        t.color(tl, Color::DarkGrey);
        t.text(tl, "Time brought to you by NTP.");
        t.nl(tl);
        t.color(tl, Color::Grey);
    }
    t.nl(tl);

    // How many callers today, and how long you have, in one breath.
    int32_t left = secondsLeft(s, now);
    char mins[32];
    if (left == INT32_MAX) snprintf(mins, sizeof(mins), "no time limit");
    else snprintf(mins, sizeof(mins), "%ld minutes", static_cast<long>((left + 59) / 60));

    // Counted off the caller log rather than kept as state: one pass over at
    // most BBS_CALLLOG_SIZE records, once per login, and it stays right
    // across a reboot and across midnight without anything to maintain.
    unsigned today = (clk::valid() ? calllog::countSince(clk::todayStart()) : 0u) + 1u;
    const char* ord = (today % 10 == 1 && today % 100 != 11) ? "st"
                    : (today % 10 == 2 && today % 100 != 12) ? "nd"
                    : (today % 10 == 3 && today % 100 != 13) ? "rd" : "th";
    snprintf(buf, sizeof(buf), "You're the %u%s caller today and have %s.",
             today, ord, mins);
    t.text(tl, buf);
    t.nl(tl);

    if (s.edit.calls) {
        char when[20];
        clk::fmtEpoch(when, sizeof(when), "%m/%d %H:%M", s.edit.lastCall);
        snprintf(buf, sizeof(buf), "This is call %u for you; the last was %s.",
                 s.edit.calls + 1u, when);
        t.text(tl, buf);
        t.nl(tl);
    }
    t.nl(tl);
    if (s.guest) {
        t.color(tl, Color::Yellow);
        t.text(tl, "Guest pass: nothing is saved.");
        t.nl(tl);
        t.color(tl, Color::Grey);
    }
    // The "[H]ELP for commands." line used to be printed here, to everybody.
    // It belongs to the main prompt and only to the main prompt: telling
    // somebody who is about to be put in the chat room to press H for
    // commands is advice for a place they are not going. landAfterLogin
    // prints it when that is in fact where they land.

    plat::log("bbs: node %u login '%s'", s.id, s.user);
    for (uint8_t i = 0; i < plugins::count(); ++i) {
        const Plugin* p = plugins::at(i);
        if (plugins::running(i) && p->onLogin) p->onLogin(s);
        if (plugins::running(i) && p->onPresence) p->onPresence(s);
    }

    snprintf(buf, sizeof(buf), "*** %s is on node %u", s.user, s.id);
    noticeAll(s, buf);

    // A caller who just registered gets the short rules; everybody else
    // gets whatever the board has to say today.
    const char* first = s.newAccount ? "newuser" : "bulletin";
    s.newAccount = false;
    if (!playScreen(s, first)) landAfterLogin(s);
    else s.pendingLand = true;       // the screen finishes, then they land
}

// ---------------------------------------------------------------------------
// landAfterLogin: put the caller where they asked to be put.
//
// The account decides; LAND_DEFAULT means it has not said, and the board's
// own setting fills in. Guests have no account, so they always get the
// board's setting.
//
// Anything the board cannot actually do falls back to the main prompt
// without comment. That is what lets Bulletin be offered as a choice before
// the bulletin plugin exists, and it is also the right answer for a board
// that has switched chat off: a caller should not be greeted with "Unknown
// command" because of a preference they set months ago.
// ---------------------------------------------------------------------------
void Bbs::landAfterLogin(Session& s) {
    uint8_t want = syscfg::get().landing;
    if (!s.guest) {
        static UserRec u;                       // static: off the task stack
        if (users::find(s.user, u) && u.land != LAND_DEFAULT) want = u.land;
    }

    const char* verb = users::landVerb(want);
    if (verb && findCommand(verb, s)) {
        s.landing = true;                       // "put here", not "typed it"
        runCommand(s, verb, plat::millis());
        s.landing = false;
        return;
    }

    Term& t = s.term;
    Timeline& tl = s.tl;
    t.color(tl, Color::Grey);
    t.text(tl, "[H]ELP for commands.");
    t.nl(tl);
    prompt(s);
}

// ---------------------------------------------------------------------------
// saveCallStats: at logoff, count the call and the day's minutes
// ---------------------------------------------------------------------------
void Bbs::saveCallStats(Session& s, uint32_t now) {
    static UserRec u;                                // static: keeps it off the task stack
    if (s.guest || s.role == Role::Busy || !users::find(s.user, u)) return;
    uint32_t mins = ((now - s.loginAt) / 1000u + 59u) / 60u;
    uint32_t day  = clk::dayKey(now);
    if (u.dayKey != day) { u.dayKey = day; u.dayMinutes = 0; }
    if (s.role == Role::Caller && !unlimited(s)) {
        uint32_t total = u.dayMinutes + mins;
        u.dayMinutes = static_cast<uint16_t>(total > 0xFFFF ? 0xFFFF : total);
    }
    if (u.calls < 0xFFFF) ++u.calls;
    if (s.loginEpoch) u.lastCall = s.loginEpoch;
    users::update(u.handle, u);
}

uint16_t Bbs::dayMinutesUsed(const char* handle, uint32_t now) {
    static UserRec u;
    if (!users::find(handle, u)) return 0;
    return u.dayKey == clk::dayKey(now) ? u.dayMinutes : 0;
}

// ---------------------------------------------------------------------------
// showScreen: a whole screen, now, without touching the session state.
// See bbs.h for why this is not playScreen.
// ---------------------------------------------------------------------------
bool Bbs::showScreen(Session& s, const char* name) {
    static ScreenPlayer one;                 // never the session's own player:
    if (!one.open(name, s.term)) return false;   // that one may be mid-screen
    one.setPaging(0);                        // no More: they asked to go, not to read
    ScreenPlayer::Vars vars{ s.user, s.id, BBS_MAX_NODES };
    // Bounded twice over: by the screen ending, and by the room left to say
    // it in. A transition screen that does not fit is a transition screen
    // that was too long.
    for (uint16_t guard = 0; guard < 2000; ++guard) {
        if (s.tl.freeBytes() < 256) break;
        if (!one.pump(s.term, s.tl, vars)) break;
    }
    one.close();
    return true;
}

// ---------------------------------------------------------------------------
// playScreen: stream a screen inside the shell (paged), prompt after
// ---------------------------------------------------------------------------
bool Bbs::playScreen(Session& s, const char* name) {
    if (!s.scr.open(name, s.term)) return false;
    s.ed = LineEditor();                          // editor idle while it plays
    s.scr.setPaging(pageRows(s));
    s.pendingPrompt = true;
    s.pageLines     = 0;
    s.st            = SState::Shell;
    return true;
}

// ---------------------------------------------------------------------------
// drawPrompt / prompt: "[n] Main: " ("[S] Sysop: " on the sysop node)
// ---------------------------------------------------------------------------
void Bbs::drawPrompt(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    char buf[8];

    t.reset(tl);
    t.nl(tl);
    t.color(tl, Color::LightBlue);
    snprintf(buf, sizeof(buf), "[%s] ", nodeName(s).t);
    t.text(tl, buf);
    t.color(tl, Color::Yellow);
    t.text(tl, s.role == Role::Sysop ? "Sysop" : "Main");
    t.color(tl, Color::Grey);
    t.text(tl, ": ");
    t.color(tl, Color::White);
}

void Bbs::prompt(Session& s) {
    drawPrompt(s);
    armPrompt(s);
}

// armPrompt: take a command on the current line (after an in-place error)
void Bbs::armPrompt(Session& s) {
    uint8_t cols = s.term.cols();
    uint8_t room = static_cast<uint8_t>(cols > 14 ? cols - 12 : 8);
    s.ed.begin(room < BBS_LINE_MAX ? room : BBS_LINE_MAX, LineEditor::F_BYEMASK | LineEditor::F_STAY);
    s.st        = SState::Shell;
    s.histPos   = -1;
    s.pageLines = 0;
    s.nonstop   = false;
    s.list      = ListKind::None;
}

void Bbs::hangup(Session& s, const char* msg, uint32_t now) {
    plat::log("bbs: node %s hangup: %s", nodeName(s).t, msg);
    s.scr.close();
    s.pendingTail = false;
    s.list = ListKind::None;
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::LightRed);
    s.term.text(s.tl, msg);

    // Somebody who got as far as being a caller gets the send-off, however
    // the call ended: idle, out of time, kicked or banned. Somebody who
    // never logged in does not, and that is deliberate. The busy line and a
    // refused password have nothing to say goodbye to, and a detection
    // timeout means the terminal type is still unknown, so an ANSI screen
    // would arrive as line noise.
    if (s.loginAt) { s.term.nl(s.tl); exitScreen(s, now); return; }

    fx::hangup(s.term, s.tl);
    s.st       = SState::Closing;
    s.lingerAt = 0;
    s.closeAt  = now + 5000;
}

// ---------------------------------------------------------------------------
// goodbye: logoff screen, line noise, NO CARRIER
// ---------------------------------------------------------------------------
void Bbs::goodbye(Session& s, uint32_t now) {
    plat::log("bbs: node %s logoff", nodeName(s).t);
    s.term.reset(s.tl);
    s.term.nl(s.tl);
    exitScreen(s, now);
}

// ---------------------------------------------------------------------------
// exitScreen: the send-off, and the one place that decides how a call ends.
// Plays screens/goodbye if the board has one, otherwise says thank you in
// plain text, and either way finishes with line noise and NO CARRIER.
//
// The line is then held open for BBS_EXIT_LINGER_MS after everything has
// drained, because a screen that is sent and immediately followed by a
// closed socket is a screen most terminals never draw.
// ---------------------------------------------------------------------------
void Bbs::exitScreen(Session& s, uint32_t now) {
    Term& t = s.term;
    s.st       = SState::Closing;
    s.lingerAt = 0;
    s.closeAt  = now + 20000;          // cap, for a far end that stopped reading
    if (s.scr.open("goodbye", t)) {
        s.pendingTail = true;
        return;
    }
    t.color(s.tl, Color::LightGreen);
    t.text(s.tl, "Thanks for calling, ");
    t.text(s.tl, s.user[0] ? s.user : "caller");
    t.text(s.tl, "!");
    t.nl(s.tl);
    fx::lineNoise(t, s.tl, 12, 300);
    fx::hangup(t, s.tl);
}

// ===========================================================================
// Timers and notices
// ===========================================================================

// ---------------------------------------------------------------------------
// secondsLeft: the tighter of the per-call and per-day limits. Guests have
// guest_minutes per call and no daily limit.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// noteBoot: why this boot happened, into the log on the logs partition and
// into a flag for the next staff login.
//
// The file is plain text and capped, because the point of it is to be read
// by a person who is wondering what happened last night, not to be complete
// for ever.
// ---------------------------------------------------------------------------
void Bbs::noteBoot() {
    snprintf(bootReason_, sizeof(bootReason_), "%s", plat::resetReason());
    bootCrash_ = plat::resetWasCrash();
    plat::log("boot: %s", bootReason_);

    char path[96];
    snprintf(path, sizeof(path), "%s/%s", plat::logsBase(), BBS_REBOOT_FILE);

    // Count what is already there, and start again if it has grown past the
    // cap. Losing the oldest lines matters less than the file growing
    // without limit on a board that is genuinely stuck in a reboot loop.
    uint16_t lines = 0;
    long size = 0;
    if (FILE* r = fopen(path, "r")) {
        char line[96];
        while (fgets(line, sizeof(line), r)) {
            ++lines;
            if (strstr(line, "crash") || strstr(line, "watchdog") || strstr(line, "brownout"))
                ++bootCrashes_;
        }
        fseek(r, 0, SEEK_END);
        size = ftell(r);
        fclose(r);
    }

    FILE* f = fopen(path, size > BBS_REBOOT_MAX ? "w" : "a");
    if (!f) return;
    if (size > BBS_REBOOT_MAX) bootCrashes_ = bootCrash_ ? 1 : 0;

    char when[32];
    if (clk::valid()) clk::fmt(when, sizeof(when), "%Y-%m-%d %H:%M");
    else              snprintf(when, sizeof(when), "clock not set yet");
    fprintf(f, "%s  %s\n", when, bootReason_);
    fclose(f);
}

// ---------------------------------------------------------------------------
// unlimited: whether the clock applies to this call at all. Two ways to be
// off it: the caller's rank says so for every call, or a sysop typed
// TIME n -1 for this one.
// ---------------------------------------------------------------------------
bool Bbs::unlimited(const Session& s) const {
    return can(s, PERM_NOLIMITS) || s.noLimits;
}

int32_t Bbs::secondsLeft(const Session& s, uint32_t now) const {
    if (s.noLimits) return INT32_MAX;          // taken off the clock for tonight
    const SysConfig& c = syscfg::get();
    int32_t on   = static_cast<int32_t>((now - s.loginAt) / 1000u);
    int32_t adj  = static_cast<int32_t>(s.timeAdjMin) * 60;
    int32_t left = INT32_MAX;
    if (s.guest) {
        return c.guestMinutes ? static_cast<int32_t>(c.guestMinutes) * 60 + adj - on : INT32_MAX;
    }
    if (c.callMinutes) {
        int32_t v = static_cast<int32_t>(c.callMinutes) * 60 + adj - on;
        if (v < left) left = v;
    }
    if (c.dayMinutes) {
        int32_t v = (static_cast<int32_t>(c.dayMinutes) - s.dayUsedMin) * 60 + adj - on;
        if (v < left) left = v;
    }
    return left;
}

// ---------------------------------------------------------------------------
// checkTimers: idle warn/hangup, busy-line login window, time limits
// ---------------------------------------------------------------------------
void Bbs::checkTimers(Session& s, uint32_t now) {
    if (s.st == SState::Detect || s.st == SState::Closing || s.st == SState::Free) return;
    char msg[64];

    if (s.role == Role::Busy) {
        if (s.busyLoginUntil && static_cast<int32_t>(now - s.busyLoginUntil) >= 0) {
            hangup(s, "All nodes are in use. Try your call later.", now);
        }
        if (s.st == SState::BusyWait || s.busyLoginUntil) return;   // countdown or login window rules
    }

    if (s.st == SState::Plugin) {                          // a plugin owns the screen:
        s.lastInput = now;                                 // watching is not idling
    }
    bool inForm      = s.st == SState::Form || s.st == SState::UserList;
    bool signingUp   = inForm && !s.loggedIn;               // sign-up form: still a login
    bool login       = s.st == SState::AskName || s.st == SState::AskPass ||
                       s.st == SState::AskRegister || s.st == SState::AskKnowMore ||
                       s.st == SState::AnyKey || signingUp;
    uint32_t idleMin = syscfg::get().idleMinutes;           // 0 = shell never idles out
    if (!unlimited(s) && (login || idleMin)) {
        uint32_t limit = signingUp ? BBS_FORM_TIMEOUT_MS
                       : login     ? BBS_NAME_TIMEOUT_MS : idleMin * 60000u;
        uint32_t warn  = signingUp ? BBS_FORM_WARN_MS
                       : login     ? BBS_NAME_WARN_MS
                       : (limit > 2u * BBS_IDLE_WARN_BEFORE_MS ? limit - BBS_IDLE_WARN_BEFORE_MS : limit / 2u);
        // lastInput can be stamped a few ms after this tick's "now"
        int32_t  since = static_cast<int32_t>(now - s.lastInput);
        uint32_t idle  = since > 0 ? static_cast<uint32_t>(since) : 0;
        if (idle >= limit) {
            hangup(s, "IDLE TIMEOUT", now);
            return;
        }
        if (idle >= warn && !s.idleWarned && canNotify(s)) {
            s.idleWarned = true;
            unsigned left = static_cast<unsigned>((limit - idle + 999u) / 1000u);
            if (login) snprintf(msg, sizeof(msg), "Still there? Disconnecting in %u seconds.", left);
            else       snprintf(msg, sizeof(msg), "Idle: disconnecting in %u seconds.", left);
            warnNow(s, msg);
        }
    }

    if (s.loggedIn && s.role == Role::Caller && !unlimited(s)) {
        int32_t left = secondsLeft(s, now);
        if (left <= 0) {
            hangup(s, "TIME LIMIT REACHED", now);
            return;
        }
        uint8_t level = left <= BBS_TIME_WARN2_S ? 2 : (left <= BBS_TIME_WARN1_S ? 1 : 0);
        if (level > s.timeWarned && canNotify(s)) {
            s.timeWarned = level;
            long mins = (left + 59) / 60;
            snprintf(msg, sizeof(msg), "%ld minute%s left.", mins, mins == 1 ? "" : "s");
            warnNow(s, msg);
        }
    }
}

// ---------------------------------------------------------------------------
// canNotify: the session is sitting at a prompt with room to print
// ---------------------------------------------------------------------------
bool Bbs::canNotify(const Session& s) const {
    bool atPrompt = s.st == SState::AskName || s.st == SState::AskPass || s.st == SState::More ||
                    s.st == SState::Confirm || s.st == SState::Form || s.st == SState::UserList ||
                    (s.st == SState::Shell && s.ed.active() && !s.scr.active());
    return atPrompt && s.tl.freeBytes() > 512 && s.tl.freeFrames() > 16;
}

// ---------------------------------------------------------------------------
// warnNow: a timer warning where the caller is looking. A form has its own
// status line and the user manager its message row; anywhere else the
// warning goes above the prompt.
// ---------------------------------------------------------------------------
void Bbs::warnNow(Session& s, const char* msg) {
    if (s.st == SState::Form)     { s.form.status(msg, Color::Yellow, s.term, s.tl); return; }
    if (s.st == SState::UserList) { ulStatus(s, Color::Yellow, msg); return; }
    notify(s, Color::Yellow, msg);
}

// ---------------------------------------------------------------------------
// notify: print a line above the current input and redraw that input
// ---------------------------------------------------------------------------
void Bbs::notify(Session& s, Color c, const char* msg) {
    Term& t = s.term;
    t.reset(s.tl);
    t.nl(s.tl);
    t.color(s.tl, c);
    t.text(s.tl, msg);
    t.reset(s.tl);
    redrawInput(s);
}

void Bbs::redrawInput(Session& s) {
    Term& t = s.term;
    switch (s.st) {
        case SState::AskName:
            drawNamePrompt(s);
            s.ed.redraw(t, s.tl);
            break;
        case SState::AskPass:
            t.nl(s.tl);
            t.color(s.tl, Color::Cyan);
            t.text(s.tl, "Password: ");
            t.color(s.tl, Color::White);
            s.ed.redraw(t, s.tl);
            break;
        case SState::Shell:
            drawPrompt(s);
            s.ed.redraw(t, s.tl);
            break;
        case SState::More:
            t.nl(s.tl);
            t.color(s.tl, Color::LightBlue);
            t.text(s.tl, kMoreText);
            t.color(s.tl, Color::White);
            break;
        case SState::Confirm:
            t.nl(s.tl);
            t.color(s.tl, Color::Yellow);
            t.text(s.tl, s.confirm == ConfirmKind::DeleteUser ? "Delete that account (y/N)? " : kConfirmText);
            t.color(s.tl, Color::White);
            break;
        default:
            t.nl(s.tl);
            break;
    }
}

// ---------------------------------------------------------------------------
// post: queue a bus message for one session
// ---------------------------------------------------------------------------
void Bbs::post(Session& to, BusKind kind, const Session* from, const char* text) {
    BusMsg m;
    m.kind     = kind;
    m.fromNode = (from && from->role == Role::Caller) ? from->id : 0;
    if (from) strncpy(m.from, from->user, BBS_USER_MAX);
    strncpy(m.text, text, BBS_LINE_MAX);
    if (!to.mb.push(m)) plat::log("bbs: node %s mailbox full, oldest dropped", nodeName(to).t);
}

// ---------------------------------------------------------------------------
// noticeAll: arrival/departure line to every other logged-in session
// ---------------------------------------------------------------------------
void Bbs::noticeAll(const Session& about, const char* text) {
    for (Session* o : all_) {
        if (o == &about || !o->loggedIn || o->role == Role::Busy) continue;
        post(*o, BusKind::Notice, &about, text);
    }
}

// ---------------------------------------------------------------------------
// deliverMail: print queued messages once the session is idle at its prompt
// ---------------------------------------------------------------------------
void Bbs::deliverMail(Session& s) {
    if (s.mb.empty() || s.role == Role::Busy) return;
    if (!(s.st == SState::Shell && s.ed.active() && !s.scr.active() && s.tl.empty())) return;

    Term& t = s.term;
    Timeline& tl = s.tl;
    BusMsg m;
    char line[BBS_USER_MAX + BBS_LINE_MAX + 32];
    bool any = false;

    while (tl.freeBytes() > 768 && s.mb.pop(m)) {
        Color c = Color::Cyan;
        const char* alert = nullptr;                 // pages and broadcasts flash first
        switch (m.kind) {
            case BusKind::Page:
                if (m.fromNode) snprintf(line, sizeof(line), "Page from %s (%u): %s", m.from, m.fromNode, m.text);
                else            snprintf(line, sizeof(line), "Page from Sysop: %s", m.text);
                c = Color::LightGreen;
                alert = " PAGE ";
                break;
            case BusKind::Broadcast:
                snprintf(line, sizeof(line), "*** Sysop: %s", m.text);
                c = Color::Yellow;
                alert = " SYSOP ";
                break;
            case BusKind::Notice:
            default:
                snprintf(line, sizeof(line), "%s", m.text);
                break;
        }
        t.reset(tl);
        t.nl(tl);
        if (alert) {                                 // bell, flashing tag, rub it out, message
            t.bell(tl);
            t.color(tl, Color::LightRed);
            fx::blink(t, tl, alert, 3, 150);
            fx::pause(tl, 250);
            fx::rubout(t, tl, static_cast<uint8_t>(strlen(alert)), 25);
        }
        t.color(tl, c);
        t.text(tl, line);
        any = true;
    }
    if (any) {
        drawPrompt(s);
        s.ed.redraw(t, tl);
    }
}

// ===========================================================================
// Paged output
// ===========================================================================

uint8_t Bbs::pageRows(const Session& s) const {
    uint8_t rows = s.term.rows();
    return static_cast<uint8_t>(rows > 8 ? rows - 2 : 6);
}

// ---------------------------------------------------------------------------
// presenceChanged: somebody became visible or invisible.
//
// Called from the visibility commands as well as from login and logoff,
// because publicBusy counts a session only while it is visible: a sysop
// typing HIDE changes what the directory publishes without anybody hanging
// up, and before this nothing told the directory that.
// ---------------------------------------------------------------------------
void Bbs::presenceChanged(Session& s) {
    for (uint8_t i = 0; i < plugins::count(); ++i) {
        const Plugin* p = plugins::at(i);
        if (plugins::running(i) && p->onPresence) p->onPresence(s);
    }
}

// startPluginList: a plugin's own paged list. The plugin is remembered on
// the session because listRow is called back later, on a different pass of
// the loop, and by then the only thing that knows whose list this is is the
// session itself.
void Bbs::setRawInput(Session& s, bool on) {
    if (s.owner == 0xFF) return;                 // nobody to give the bytes to
    s.rawInput = on;
}

void Bbs::startPluginList(Session& s, uint8_t plugin) {
    s.listPlugin = plugin;
    startList(s, ListKind::PlugRows);
}

void Bbs::startList(Session& s, ListKind kind) {
    s.list      = kind;
    s.listIdx   = 0;
    s.listSub   = 0;
    s.pageLines = 0;
    s.nonstop   = false;
    s.ed        = LineEditor();
    s.st        = SState::List;
}

// ---------------------------------------------------------------------------
// serviceList: one row at a time while the Timeline has room
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// listEnded: where a caller goes when a list finishes or is stopped.
//
// The shell prompt, normally. But a list started from inside a plugin that
// owns the session belongs to a caller who is somewhere else: in the file
// area, or a message base later. Prompting them would drop them out of it
// without saying so, which is the sort of thing that reads as the board
// losing track of where you are.
//
// The plugin draws its own prompt as the last thing its rows() emits, so
// there is nothing to call back into: the session simply goes back to being
// the plugin's.
// ---------------------------------------------------------------------------
void Bbs::listEnded(Session& s) {
    if (s.owner != 0xFF && plugins::running(s.owner)) {
        s.st = SState::Plugin;
        return;
    }
    prompt(s);
}

void Bbs::serviceList(Session& s) {
    while (s.st == SState::List && s.tl.freeBytes() > 512 && s.tl.freeFrames() > 16) {
        if (!s.nonstop && s.pageLines >= pageRows(s)) {
            showMore(s, MoreFrom::List);
            return;
        }
        if (!listRow(s)) {
            s.list = ListKind::None;
            listEnded(s);
            return;
        }
        ++s.pageLines;
    }
}

void Bbs::showMore(Session& s, MoreFrom from) {
    s.moreFrom = from;
    s.st       = SState::More;
    s.term.color(s.tl, Color::LightBlue);
    s.term.text(s.tl, kMoreText);
    s.term.color(s.tl, Color::White);
}

// ---------------------------------------------------------------------------
// abortOutput: drop pending output and the source, back to the prompt
// ---------------------------------------------------------------------------
void Bbs::abortOutput(Session& s) {
    s.scr.close();
    s.tl.clear();
    s.list          = ListKind::None;
    s.pendingPrompt = false;
    s.pendingForm   = FormKind::None;
    s.term.reset(s.tl);
    s.term.cursor(s.tl, true);
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, "Stopped.");
    listEnded(s);
}

// ===========================================================================
// Refresh screens (WHO n, DASH n)
// ===========================================================================

// ---------------------------------------------------------------------------
// idleSecondsLeft: seconds before the idle hangup, -1 when none applies
// ---------------------------------------------------------------------------
int32_t Bbs::idleSecondsLeft(const Session& s, uint32_t now) const {
    uint32_t mins = syscfg::get().idleMinutes;
    if (!mins || unlimited(s) || s.role == Role::Busy) return -1;
    int32_t since = static_cast<int32_t>(now - s.lastInput);
    if (since < 0) since = 0;
    int32_t left = static_cast<int32_t>(mins * 60u) - since / 1000;
    return left < 0 ? 0 : left;
}

void Bbs::startWatch(Session& s, ListKind kind, uint8_t secs) {
    s.watch     = kind;
    s.watchSecs = secs;
    s.watchNext = 0;                                 // draw the first frame now
    s.list      = kind;
    s.listIdx   = 0;
    s.listSub   = 0;
    s.ed        = LineEditor();
    s.st        = SState::Watch;
    s.term.cursor(s.tl, false);
    s.term.cls(s.tl);
}

// ---------------------------------------------------------------------------
// serviceWatch: draw a frame row by row as the Timeline has room, then wait
// watchSecs and redraw from the home position (no scrolling, no flicker)
// ---------------------------------------------------------------------------
void Bbs::serviceWatch(Session& s, uint32_t now) {
    Timeline& tl = s.tl;
    if (s.watchNext == 0) {
        while (s.st == SState::Watch && tl.freeBytes() > 512 && tl.freeFrames() > 16) {
            if (s.listSub == 0) {                    // content rows
                bool more = s.watch == ListKind::Dash ? rowDash(s) : rowWho(s);
                if (more) continue;
                s.listSub = 1;
            }
            if (!rowWatchFooter(s)) {                // frame complete
                s.watchNext = now + static_cast<uint32_t>(s.watchSecs) * 1000u;
                if (!s.watchNext) s.watchNext = 1;
                break;
            }
        }
        return;
    }
    if (static_cast<int32_t>(now - s.watchNext) >= 0 && tl.empty()) {
        s.term.home(tl);
        if (!s.term.isAnsi() && !s.term.isPet()) s.term.nl(tl);   // ASCII cannot home: new frame below
        s.listIdx   = 0;
        s.listSub   = 0;
        s.watchNext = 0;
    }
}

void Bbs::stopWatch(Session& s) {
    s.tl.clear();
    s.term.reset(s.tl);
    s.term.cursor(s.tl, true);
    s.watch = ListKind::None;
    s.list  = ListKind::None;
    if (s.term.isAnsi() || s.term.isPet()) {          // park below the frame before the prompt
        s.term.gotoXY(s.tl, 1, static_cast<uint8_t>(s.term.rows() > 2 ? s.term.rows() - 2 : 1));
    }
    prompt(s);
}

// ===========================================================================
// Input
// ===========================================================================

// ---------------------------------------------------------------------------
// onKey: route one key event by session state
// ---------------------------------------------------------------------------
void Bbs::onKey(Session& s, int k, uint32_t now) {
    s.lastInput  = now;
    s.idleWarned = false;
    Term& t = s.term;
    Timeline& tl = s.tl;

    switch (s.st) {
        case SState::Intro:
            tl.skipDelays();                 // any key fast-forwards
            return;

        case SState::AskName: {
            if (!tl.empty()) tl.skipDelays();
            LineEditor::Res r = s.ed.key(k, t, tl);
            if (r == LineEditor::Res::Abort) { askName(s); return; }
            if (r == LineEditor::Res::Done)  onHandle(s, now);
            return;
        }

        case SState::AskPass: {
            if (!tl.empty()) tl.skipDelays();
            LineEditor::Res r = s.ed.key(k, t, tl);
            if (r == LineEditor::Res::Abort) { askName(s); return; }   // ESC: another handle
            if (r == LineEditor::Res::Done)  onPassword(s, now);
            return;
        }

        case SState::AskRegister:
            if (!tl.empty()) tl.skipDelays();
            onNewHandle(s, k, now);
            return;

        case SState::AskKnowMore:
            if (!tl.empty()) tl.skipDelays();
            onKnowMore(s, k, now);
            return;

        case SState::AnyKey:
            if (!tl.empty()) { tl.skipDelays(); return; }   // still printing: let it finish
            onAnyKey(s, now);
            return;

        case SState::Form: {
            if (!tl.empty()) tl.skipDelays();
            Form::Res r = s.form.key(k, t, tl);
            if (r == Form::Res::Save)   formSave(s, now);
            if (r == Form::Res::Cancel) formCancel(s, now);
            if (r == Form::Res::Open)   formOpen(s, s.form.opened(), now);
            return;
        }

        case SState::UserList:
            if (!tl.empty()) tl.skipDelays();
            ulKey(s, k, now);
            return;

        case SState::Shell: {
            if (s.scr.active()) {            // a screen is playing
                if (isAbortKey(k)) abortOutput(s);
                else               tl.skipDelays();
                return;
            }
            if (!tl.empty()) tl.skipDelays();
            if (!s.ed.active()) return;

            // Ctrl-L clears the screen, the way it does in every shell since
            // roughly forever, and SHIFT+CLR/HOME does it on a C64. Whatever
            // was half-typed is kept and redrawn underneath, because
            // clearing the screen is not the same as abandoning the line.
            if (k == KEY_CLEAR) {
                t.cls(tl);
                drawPrompt(s);
                s.ed.redraw(t, tl);
                return;
            }

            if (k == KEY_UP) {
                if (s.histPos + 1 < s.hist.count()) {
                    ++s.histPos;
                    s.ed.replace(s.hist.get(static_cast<uint8_t>(s.histPos)), t, tl);
                }
                return;
            }
            if (k == KEY_DOWN) {
                if (s.histPos > 0) {
                    --s.histPos;
                    s.ed.replace(s.hist.get(static_cast<uint8_t>(s.histPos)), t, tl);
                } else if (s.histPos == 0) {
                    s.histPos = -1;
                    s.ed.replace("", t, tl);
                }
                return;
            }

            LineEditor::Res r = s.ed.key(k, t, tl);
            if (r == LineEditor::Res::Abort) { t.nl(tl); prompt(s); return; }
            if (r == LineEditor::Res::Done) {
                char line[BBS_LINE_MAX + 1];
                strncpy(line, s.ed.text(), BBS_LINE_MAX);
                line[BBS_LINE_MAX] = '\0';
                s.hist.add(line);
                runCommand(s, line, now);
            }
            return;
        }

        case SState::List:
            if (isAbortKey(k)) abortOutput(s);
            else               tl.skipDelays();
            return;

        case SState::More: {
            bool cont = k == 'y' || k == 'Y' || k == ' ' || k == KEY_ENTER;
            bool non  = k == 'c' || k == 'C';
            bool stop = k == 'n' || k == 'N' || k == 'q' || k == 'Q' || k == KEY_ESC || k == KEY_BREAK;
            if (!cont && !non && !stop) return;
            t.eraseBack(tl, kMoreLen);
            if (stop) { abortOutput(s); return; }
            if (s.moreFrom == MoreFrom::List) {
                if (non) s.nonstop = true;
                s.pageLines = 0;
                s.st = SState::List;
            } else {
                if (non) s.scr.setPaging(0);
                s.scr.resume();
                s.st = SState::Shell;
            }
            return;
        }

        case SState::Confirm:
            if (s.confirm == ConfirmKind::DeleteUser) {      // default is No
                bool yes = k == 'y' || k == 'Y';
                if (!yes && k != 'n' && k != 'N' && k != KEY_ENTER && k != KEY_ESC && k != KEY_BREAK) return;
                t.ch(tl, yes ? 'Y' : 'N');
                t.nl(tl);
                s.confirm = ConfirmKind::Logoff;
                if (yes) {
                    users::Result r = users::remove(s.origHandle);
                    t.color(tl, r == users::Result::Ok ? Color::LightGreen : Color::LightRed);
                    t.text(tl, r == users::Result::Ok ? "Account deleted." : "Could not delete that account.");
                    if (r == users::Result::Ok) plat::log("bbs: %s deleted account '%s'", s.user, s.origHandle);
                } else {
                    t.color(tl, Color::Grey);
                    t.text(tl, "Kept.");
                }
                if (s.backToUsers) { s.backToUsers = false; ulOpen(s); }
                else               prompt(s);
                return;
            }
            if (k == 'y' || k == 'Y') {
                t.ch(tl, 'Y');
                t.nl(tl);
                goodbye(s, now);
            } else if (k == 'n' || k == 'N' || k == KEY_ENTER || k == KEY_ESC || k == KEY_BREAK) {
                t.ch(tl, 'N');
                prompt(s);
            }
            return;

        case SState::Fx:
            if (isAbortKey(k)) s.fxStep = 0xFF;
            tl.skipDelays();
            return;

        case SState::Watch:
            stopWatch(s);                            // any key ends a refresh screen
            return;

        case SState::Plugin: {
            const Plugin* p = plugins::at(s.owner);
            if (!p || !plugins::running(s.owner)) { release(s); return; }
            if (!tl.empty()) tl.skipDelays();
            if (p->onKey) p->onKey(s, k, now);
            else          release(s);
            return;
        }

        case SState::BusyWait:
            if (s.scr.active()) { tl.skipDelays(); return; }
            t.nl(tl);
            s.busyLoginUntil = now + BBS_BUSY_LOGIN_MS;
            askName(s);
            return;

        case SState::Snoop:
            if (k == 'q' || k == 'Q' || k == KEY_ESC || k == KEY_BREAK) stopSnoop(s, nullptr);
            return;

        case SState::Approve:
            if (k == 'y' || k == 'Y') {
                t.ch(tl, 'Y');
                t.nl(tl);
                approvalShown_ = false;
                s.st = SState::Shell;                    // before decide: notes queue for the prompt
                backup_.decide(true, "");
                prompt(s);
            } else if (k == 'n' || k == 'N' || k == KEY_ESC || k == KEY_BREAK) {
                t.ch(tl, 'N');
                t.nl(tl);
                approvalShown_ = false;
                s.st = SState::Shell;
                backup_.decide(false, "rejected by the sysop");
                prompt(s);
            }
            return;

        default:
            return;
    }
}
