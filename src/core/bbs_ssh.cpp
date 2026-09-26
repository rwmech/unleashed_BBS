/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/bbs_ssh.cpp
 * Module:       Core / an SSH caller's session (1.1.2 preview, S3 only)
 *
 * Purpose:      The loop's half of SSH, kept apart so the rest of the core
 *               reads as it did: the settle's sniff and the handoff to the
 *               SSH task, the link's input, the wait while the task runs the
 *               key exchange, the account check the task asks for, and the
 *               login an SSH password has already proved.
 *
 *               Who may log in, by the SSH user name:
 *                - an account's handle: its password, checked here against
 *                  users.txt with the same lockout as the prompt (five wrong
 *                  in fifteen minutes lock the handle), and a connection
 *                  that ends on wrong passwords counts once toward the
 *                  address's ban (closeSession). The login then skips the
 *                  handle and password prompts. Staff rights still come only
 *                  from the sysop and co-sysop passwords (BYE, the login
 *                  question), never from an account password alone.
 *                - any other name: in, with no password asked (SSH "none"
 *                  or any password), to the ordinary handle prompt, where
 *                  a new caller registers or visits as a guest.
 *                - a closed board with accounts: only a password for the
 *                  account it admits, and the same no for everything else,
 *                  so it says nothing about who has an account.
 *
 * Libraries:    none here (sshd.h has the SSH)
 * Targets:      ESP32-S3 (ESP-IDF 5.3.1) and the Linux host build
 * See also:     src/core/sshd.h, src/core/sshlink.h
 *
 * Copyright 2026 - Robert Mech
 * License:      GNU General Public License v3 or later
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>. The full
 * text is in the LICENSE file at the top of this repository.
 * ===========================================================================
 */

#include "bbs.h"

#if BBS_HAS_SSH
#include "sshd.h"
#include "fx.h"
#include "users.h"
#include "../platform/platform.h"

#include "sysconfig.h"
#include "guard.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstdio>
#include <cstring>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace {
// Session::sniff past the count of "SSH-2.0-" bytes matched.
constexpr uint8_t kSniffTelnet  = 0xFF;   // decided: telnet, as always
constexpr uint8_t kSniffRefused = 0xFE;   // an SSH client told no: its bytes are dropped
} // namespace

// ---------------------------------------------------------------------------
// sshSniff: the connect settle's look at a new caller's first bytes. True
// when it took them (a prefix of "SSH-2.0-" so far, a handoff, a refusal);
// false hands them to telnet exactly as before.
//
// Only while nothing has been sent: once the probe is out, an SSH client
// would have read it as the start of our identification line. The prefix
// held between two reads is not replayed to telnet when it turns out not
// to be SSH: it was letters, and the settle ignores keys.
// ---------------------------------------------------------------------------
bool Bbs::sshSniff(Session& s, const uint8_t* raw, size_t n, uint32_t now) {
    if (s.sniff == kSniffRefused) return true;
    if (s.st != SState::Detect || !s.det.settling()) { s.sniff = kSniffTelnet; return false; }

    size_t  i = 0;
    uint8_t m = s.sniff;
    while (i < n && m < sshd::kClientIdLen && raw[i] == static_cast<uint8_t>(sshd::kClientId[m])) {
        ++i;
        ++m;
    }
    if (m < sshd::kClientIdLen) {
        if (i == n) { s.sniff = m; return true; }   // a prefix so far: the next read says
        s.sniff = kSniffTelnet;
        return false;
    }
    s.sniff = kSniffTelnet;

    // An SSH client. What it sent after "SSH-2.0-" in this read goes to the
    // SSH task with the socket, ahead of anything still in the socket.
    uint8_t pre[ssh::kPreMax];
    size_t  pn = sshd::kClientIdLen;
    memcpy(pre, sshd::kClientId, pn);
    size_t rest = n - i;
    if (rest > sizeof(pre) - pn) rest = sizeof(pre) - pn;    // n is one BBS_RX_CHUNK read
    memcpy(pre + pn, raw + i, rest);
    pn += rest;
    return sshHandoff(s, pre, pn, now);
}

// ---------------------------------------------------------------------------
// sshHandoff: the session's socket to the SSH task, with what was already
// read of it (nothing, on SSH's own port). Or, when there is no slot, the
// refusal in the clear and the node given straight back. Always true: the
// socket is dealt with either way.
// ---------------------------------------------------------------------------
bool Bbs::sshHandoff(Session& s, const uint8_t* pre, size_t pn, uint32_t now) {
    uint32_t local = 0;
    sockaddr_in a;
    socklen_t al = sizeof(a);
    if (getsockname(s.fd, reinterpret_cast<sockaddr*>(&a), &al) == 0) local = a.sin_addr.s_addr;

    ssh::Link* l = sshd::claim(s.fd, pre, pn, s.ipAddr, local, s.id);
    if (!l) {
        // Every SSH slot is taken (or SSH is off). Told so in the clear, with
        // no key exchange: the cheapest answer a client will show. The node
        // is given straight back: shut the sending side, drop what the client
        // sends, close when it does (or in three seconds).
        uint8_t buf[128];
        size_t k = sshd::refusal(buf, sizeof(buf), sshd::refused());
        if (k) send(s.fd, buf, k, MSG_DONTWAIT | MSG_NOSIGNAL);
        shutdown(s.fd, SHUT_WR);
        s.sniff    = kSniffRefused;
        s.st       = SState::Closing;
        s.closeAt  = now + 3000;
        s.lingerAt = 0;
        plat::log("bbs: node %u SSH from %s refused: %s%s%s", s.id, s.ip, sshd::refused(),
                  sshd::running() ? "" : ", ", sshd::offWhy());
        return true;
    }

    s.link = l;
    s.fd   = l->efd;                 // select() waits on the link from here on
    s.st   = SState::SshWait;
    s.tn.setEnabled(false);          // no telnet on an SSH link, ever
    s.lastInput = now;
    s.lastRx    = now;
    plat::log("bbs: node %u SSH from %s (%u of %u)", s.id, s.ip,
              static_cast<unsigned>(sshd::inUse()), static_cast<unsigned>(sshd::cap()));
    return true;
}

// ---------------------------------------------------------------------------
// sshListen: SSH's own port (Rob, 2026-09-26), where the board sends its
// identification the moment a client connects, with no telnet detection,
// so a client that waits to hear the server first (cryptlib: SyncTERM 1.9
// and older) connects. The shared port stays for the clients that speak
// first. Failing to bind is logged and costs nothing else: SSH goes on on
// the shared port.
// ---------------------------------------------------------------------------
void Bbs::sshListen() {
    const uint16_t p = syscfg::get().sshPort;
    if (!p || p == port_) return;
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) { plat::log("ssh: no socket for port %u (errno %d)", p, errno); return; }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_port        = htons(p);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) < 0 || listen(fd, BBS_LISTEN_BACKLOG) < 0) {
        plat::log("ssh: port %u will not bind (errno %d); SSH stays on port %u only", p, errno, port_);
        close(fd);
        return;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    sshLfd_  = fd;
    sshPort_ = p;
    plat::log("ssh: listening on %u too, speaking first", p);
}

// ---------------------------------------------------------------------------
// busyFits: the busy line only while the sessions after it and the
// listeners leave BBS_SOCK_RESERVE of lwIP's sockets for the backup window
// and announce. Counts what the board holds: listeners, sessions, and SSH
// sockets the task still holds for sessions already gone.
// ---------------------------------------------------------------------------
bool Bbs::busyFits() const {
#ifdef CONFIG_LWIP_MAX_SOCKETS
    constexpr uint8_t kSockets = CONFIG_LWIP_MAX_SOCKETS;
#else
    constexpr uint8_t kSockets = 16;
#endif
    uint8_t held = static_cast<uint8_t>((lfd_ >= 0) + (sshLfd_ >= 0) + sshd::lingering());
    for (const Session* o : all_) if (o->st != SState::Free) ++held;
    return held + 1u + BBS_SOCK_RESERVE <= kSockets;
}

// ---------------------------------------------------------------------------
// acceptSsh: acceptAll for SSH's own port. The same checks in the same
// order (a ban, a shut-down board, a restore holding callers off, a free
// node, the busy line, the socket budget), each answered in SSH's terms,
// since nothing but an SSH client calls here: a refusal is the board's
// identification and one DISCONNECT in the clear, never text.
// ---------------------------------------------------------------------------
void Bbs::acceptSsh(uint32_t now) {
    for (;;) {
        sockaddr_in a;
        socklen_t   al = sizeof(a);
        int fd = accept(sshLfd_, reinterpret_cast<sockaddr*>(&a), &al);
        if (fd < 0) break;                           // none waiting, or no socket to take it

        uint32_t ipAddr = peerAddr(a.sin_addr.s_addr);
        char ip[16];
        ipToText(ipAddr, ip, sizeof(ip));
        if (bans_.banned(ipAddr, now)) {
            close(fd);
            plat::log("bbs: BANNED %s dropped (SSH port)", ip);
            continue;
        }
        // A refusal: the words in the clear, the sending side shut so they
        // go ahead of the FIN, and what the client already sent (its own
        // identification) read off first, since closing on unread input is
        // a reset, which some clients show instead of the words. Never
        // waits: whatever has not arrived yet is not waited for.
        auto refuse = [&](const char* why, const char* log) {
            uint8_t buf[128];
            size_t k = sshd::refusal(buf, sizeof(buf), why);
            if (k) send(fd, buf, k, MSG_DONTWAIT | MSG_NOSIGNAL);
            shutdown(fd, SHUT_WR);
            uint8_t sink[128];
            for (int drain = 0; drain < 8 && recv(fd, sink, sizeof(sink), MSG_DONTWAIT) > 0; ++drain) {}
            close(fd);
            plat::log("bbs: SSH port, %s refused: %s", ip, log);
        };
        if (shutDone_) { refuse("--> This board has been shut down by the sysop", "shut down"); continue; }

        // The same socket options acceptAll gives a caller: a line whose far
        // end vanishes (a laptop's Wi-Fi gone) is dropped in about 90 s, not
        // the stack's two hours, which would hold the sysop node that long.
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
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
        Role role = Role::Caller;
        if (!backup_.restoring()) {
            for (auto& s : nodes_) if (s.st == SState::Free) { slot = &s; break; }
        }
        const bool budget = !slot && busy_.st == SState::Free && !busyFits();
        if (!slot && busy_.st == SState::Free && !budget) { slot = &busy_; role = Role::Busy; }
        if (!slot) {
            refuse("--> All lines are busy", budget ? "all lines busy (the busy line held back: socket budget)"
                                                    : "all lines busy");
            continue;
        }
        openSession(*slot, fd, ip, ipAddr, role, now);
        sshHandoff(*slot, nullptr, 0, now);
    }
}

// ---------------------------------------------------------------------------
// sshRead: what the caller typed, out of the link's ring, into the same
// input path a socket's bytes take after the telnet filter. A resize first.
// ---------------------------------------------------------------------------
void Bbs::sshRead(Session& s, uint32_t now) {
    ssh::Link* l = s.link;
    if (l->resized.exchange(false, std::memory_order_acq_rel) && s.term.isAnsi()) {
        uint16_t c = l->cols.load(), r = l->rows.load();
        s.term.setGeometry(static_cast<uint8_t>(c > 255 ? 255 : c), static_cast<uint8_t>(r > 255 ? 255 : r));
    }
    uint8_t data[BBS_RX_CHUNK];
    size_t m = sshd::read(l, data, sizeof(data));
    if (!m) {
        if (sshd::gone(l)) closeSession(s, "remote", now);
        return;
    }
    plat::activityPulse(now);
    rxSeen_  = static_cast<uint16_t>(rxSeen_ | (1u << s.id));   // for the lights
    rxBytes_ += static_cast<uint32_t>(m);
    s.lastRx = now;
    memcpy(s.rxBuf, data, m);
    s.rxLen = static_cast<uint8_t>(m);
    s.rxPos = 0;
    processInput(s, now);
}

// ---------------------------------------------------------------------------
// sshWait: the SSH task is running the key exchange and the login. Answer
// its account question when it has one; go on to the terminal probe once the
// caller's shell is open. The probe still runs: the pty request gives the
// size but not the character set, and SyncTERM wants CP437 where OpenSSH and
// PuTTY want UTF-8. It is ANSI either way, so no reply means CP437, never
// the PETSCII question.
// ---------------------------------------------------------------------------
void Bbs::sshWait(Session& s, uint32_t now) {
    ssh::Link* l = s.link;
    uint8_t kind = 0;
    const char* user = nullptr;
    const char* pass = nullptr;
    if (sshd::asked(l, kind, user, pass)) sshd::answer(l, sshAuth(s, kind, user, pass, now));
    if (sshd::gone(l)) { closeSession(s, "SSH login ended", now); return; }
    // The SSH task ends a login that takes too long (BBS_SSH_LOGIN_MS); the
    // node is the loop's, so the loop does not wait on the task to give it
    // back. Ten seconds past the task's own limit is only a backstop.
    if (static_cast<int32_t>(now - s.connectedAt) > static_cast<int32_t>(BBS_SSH_LOGIN_MS + 10000)) {
        closeSession(s, "SSH login timed out", now);
        return;
    }
    if (!sshd::open(l)) return;

    l->resized.store(false, std::memory_order_relaxed);   // onDetected takes the size as it is now
    s.det.start(now);
    s.det.ansiOnly();
    s.det.probeNow(now, s.tl);
    s.st        = SState::Detect;
    s.lastRx    = now;
    s.lastInput = now;
    flush(s, now);
}

// ---------------------------------------------------------------------------
// sshAuth: the loop's answer to the SSH task's login question (see the file
// header for the rules). Every check the handle and password prompts make,
// in the same order, from the same records.
// ---------------------------------------------------------------------------
bool Bbs::sshAuth(Session& s, uint8_t kind, const char* user, const char* pass, uint32_t now) {
    // The busy line has no accounts: it shows its sign to anyone.
    if (s.role == Role::Busy) { s.sshAuthed = false; return true; }

    UserRec& u = s.edit;                              // the session's own record, off the stack
    users::Lookup found = users::validHandle(user) ? users::lookup(user, u) : users::Lookup::Missing;
    const bool closedNow = closedTo(s) && users::count() > 0;

    if (found == users::Lookup::Error) {              // accounts unreadable: nobody, never "new"
        plat::log("bbs: node %u users.txt unreadable at an SSH login", s.id);
        return false;
    }
    if (found == users::Lookup::Missing) {
        // Nobody by that name: the ordinary prompt, where a new caller
        // registers or visits. A closed board takes nobody new.
        if (closedNow) return false;
        s.sshAuthed = false;
        s.edit = UserRec();
        return true;
    }
    if (kind != ssh::AUTH_PASSWORD) return false;     // an account: its password

    // Before the lock checks, which would say the handle exists.
    if (closedNow && !closedAdmits(u.id)) return false;
    if (u.locked || logins_.locked(u.handle, now)) {
        plat::log("bbs: node %u SSH login for locked '%s'", s.id, u.handle);
        return false;
    }
    if (!users::checkPassword(u, pass)) {
        bool lockedNow = logins_.fail(u.handle, now);
        plat::log("bbs: node %u wrong SSH password for '%s'%s", s.id, u.handle,
                  lockedNow ? ", now locked" : "");
        return false;
    }
    logins_.clear(u.handle);
    snprintf(s.user, sizeof(s.user), "%s", u.handle);
    s.sshAuthed = true;
    s.sshPwOk   = true;
    plat::log("bbs: node %u SSH password accepted for '%s'", s.id, s.user);
    return true;
}

// ---------------------------------------------------------------------------
// sshLogin: the welcome has played; the SSH password proved the account, so
// the login is made here instead of at the prompts. The account is read
// again first, as onPassword does: it may have been locked, retired or
// closed out while the welcome played.
// ---------------------------------------------------------------------------
void Bbs::sshLogin(Session& s, uint32_t now) {
    s.sshAuthed = false;
    Term& t = s.term;
    Timeline& tl = s.tl;
    users::Lookup found = users::lookup(s.user, s.edit);
    if (found == users::Lookup::Missing) { hangup(s, "That account is gone.", now); return; }
    if (found == users::Lookup::Error)   { hangup(s, "Accounts are unavailable. Try again shortly.", now); return; }
    if (closedTo(s) && !closedAdmits(s.edit.id)) { closedRefuse(s, now); return; }
    if (s.edit.locked || logins_.locked(s.edit.handle, now)) {
        hangup(s, s.edit.locked ? "This account is locked. Ask the sysop."
                                : "Too many wrong passwords. Try again later.", now);
        return;
    }
    snprintf(s.user, sizeof(s.user), "%s", s.edit.handle);
    t.reset(tl);
    t.nl(tl);
    t.color(tl, Color::Grey);
    t.text(tl, "Signed in over SSH as ");
    t.color(tl, Color::White);
    t.text(tl, s.user);
    t.nl(tl);
    t.color(tl, Color::LightGreen);
    fx::scramble(t, tl, "ACCESS GRANTED", 10, 55);
    t.nl(tl);
    t.nl(tl);
    completeLogin(s, now);
}

#endif  // BBS_HAS_SSH
