/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/sshd.cpp
 * Module:       Core / the SSH server (1.1.2 preview, S3 only)
 *
 * Purpose:      The SSH task and the loop's side of an SSH link. See sshd.h
 *               for the shape and sshlink.h for the seam.
 *
 *               The task runs wolfSSH non-blocking over select() on its own
 *               sockets and one wake descriptor the loop posts to. A pass
 *               moves every link one step: allocate a new one, carry its
 *               handshake on, pump an open one's rings, close a finished
 *               one. A key exchange is a few hundred milliseconds of CPU in
 *               one wolfSSH_accept call, and it is the only long thing this
 *               task does: it holds up the other SSH links for that long,
 *               never the BBS loop, which is on the other core.
 *
 *               Authentication is the loop's to decide, never the task's:
 *               the account system, the lockout and the bans all live there
 *               (a password check here would also read the flash from a
 *               second task). The task hands the question over and wolfSSH
 *               waits on WOLFSSH_USERAUTH_WOULD_BLOCK, which keeps the packet
 *               and asks again on the next pass.
 *
 * Libraries:    wolfSSH 1.5.0, wolfCrypt 5.9.4 (components/wolfssh)
 * Targets:      ESP32-S3 (ESP-IDF 5.3.1) and the Linux host build
 * See also:     src/core/sshd.h, internal/ssh-research-2026-09-26.md
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

#include "sshd.h"

#if BBS_HAS_SSH
#include "disk.h"
#include "../platform/platform.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <cstdio>
#include <cstring>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

// wolfSSL's settings.h says, with an unconditional #warning on every Xtensa
// build, that it picks the small constant-time curve code there: which is
// the code the research chose (section 6.1). Said once here, not as a
// warning in every build.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
extern "C" {
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/ed25519.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <wolfssh/ssh.h>
#include <wolfssh/internal.h>
}
#pragma GCC diagnostic pop

#if defined(ESP_PLATFORM)
// Every wolfCrypt and wolfSSH allocation, PSRAM first (user_settings.h,
// XMALLOC_USER). Internal RAM only when PSRAM has none.
extern "C" void* XMALLOC(size_t n, void* heap, int type) {
    (void)heap; (void)type;
    return plat::extAlloc(n);
}
extern "C" void XFREE(void* p, void* heap, int type) {
    (void)heap; (void)type;
    plat::extFree(p);
}
extern "C" void* XREALLOC(void* p, size_t n, void* heap, int type) {
    (void)heap; (void)type;
    return plat::extRealloc(p, n);
}
#endif

// wolfSSH can load a PEM key, which needs wolfCrypt's PEM decoder, and
// that is compiled out with the certificate code (NO_CERTS). The host keys
// here are DER and nothing else is ever loaded, so the one reference gets
// an answer that says so.
extern "C" int wc_KeyPemToDer(const unsigned char*, int, unsigned char*, int, const char*) {
    return NOT_COMPILED_IN;
}

using ssh::Link;
using ssh::LState;

namespace {

// Our identification line. No version in it: RFC 4253 forbids a minus sign
// in the software version, and the board's version has one.
const char kServerId[] = "SSH-2.0-unleashedBBS\r\n";

// What a client is told when every SSH slot is taken.
const char kFullWhy[] = "--> All SSH ports are full";

constexpr uint8_t kWakes = BBS_SSH_MAX + 1;   // one per link, one for the task

Link         g_link[BBS_SSH_MAX];
WOLFSSH*     g_ssh[BBS_SSH_MAX]   = {};     // the task's
uint32_t     g_since[BBS_SSH_MAX] = {};     // the task's: when the handshake began
// The task's: the last pump stopped because the socket or the client's
// window would take no more, so waiting on the socket is the only way on;
// and when an open link was first seen hung up by the loop.
bool         g_blocked[BBS_SSH_MAX] = {};
uint32_t     g_hungAt[BBS_SSH_MAX]  = {};
// Login questions one connection may ask the loop, "none" included: each
// is a users.txt lookup on the loop, so a client may not ask for ever.
constexpr uint8_t kAsksMax = 8;
// Handshakes at once from one address: a key exchange is up to half a
// second of this task's CPU, and one address need not hold more.
constexpr uint8_t kPerPeer = 2;
char         g_refused[48] = {};            // why the last claim said no
WOLFSSH_CTX* g_ctx   = nullptr;
int          g_wake  = -1;                  // loop -> task
std::atomic<bool> g_up{false};
char         g_fp[2][56] = {};              // "SHA256:" + 43 base64
char         g_off[48]   = "not started";
uint32_t     g_stackLow  = 0;               // the task's: least stack free seen

// The host keys, DER, read or made by the BBS task at begin().
uint8_t  g_edDer[128];
uint32_t g_edLen = 0;
uint8_t  g_ecDer[160];
uint32_t g_ecLen = 0;

inline LState stateOf(const Link& l) {
    return static_cast<LState>(l.state.load(std::memory_order_acquire));
}
inline void setState(Link& l, LState s) {
    l.state.store(static_cast<uint8_t>(s), std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Fingerprints, the way OpenSSH prints them: SHA-256 of the public key's
// wire blob, base64 without the padding.
// ---------------------------------------------------------------------------
size_t putString(uint8_t* out, const uint8_t* d, uint32_t n) {
    out[0] = static_cast<uint8_t>(n >> 24);
    out[1] = static_cast<uint8_t>(n >> 16);
    out[2] = static_cast<uint8_t>(n >> 8);
    out[3] = static_cast<uint8_t>(n);
    memcpy(out + 4, d, n);
    return 4u + n;
}

void fingerprintOf(const uint8_t* blob, size_t n, char* out, size_t cap) {
    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint8_t h[32];
    if (wc_Sha256Hash(blob, static_cast<word32>(n), h) != 0) { out[0] = 0; return; }
    size_t o = static_cast<size_t>(snprintf(out, cap, "SHA256:"));
    for (size_t i = 0; i < 32 && o + 4 < cap; i += 3) {
        uint32_t v = static_cast<uint32_t>(h[i]) << 16;
        if (i + 1 < 32) v |= static_cast<uint32_t>(h[i + 1]) << 8;
        if (i + 2 < 32) v |= h[i + 2];
        out[o++] = kB64[(v >> 18) & 63];
        out[o++] = kB64[(v >> 12) & 63];
        if (i + 1 < 32) out[o++] = kB64[(v >> 6) & 63];
        if (i + 2 < 32) out[o++] = kB64[v & 63];
    }
    out[o < cap ? o : cap - 1] = '\0';
}

// ---------------------------------------------------------------------------
// Host keys. <userdata>/ssh/host_ed25519 and host_ecdsa, DER, written once
// through a temp file and a rename within the partition. Made on the board
// the first time, with the radio up (the RNG's entropy). Read and written
// by the BBS task only: the SSH task never touches the flash.
// ---------------------------------------------------------------------------
void keyPath(char* out, size_t n, const char* name) {
    snprintf(out, n, "%s/ssh/%s", plat::userBase(), name);
}

// keyAbsent: there is no key file at all, the one case a key is made in.
// A file that is there and will not read or decode leaves SSH off instead:
// making a new key over it would change the board's identity on a passing
// flash error, and every caller's client would say the key had changed.
bool keyAbsent(const char* name) {
    char path[96];
    keyPath(path, sizeof(path), name);
    struct stat st;
    return stat(path, &st) != 0 && errno == ENOENT;
}

bool keyRead(const char* name, uint8_t* buf, size_t cap, uint32_t& len) {
    char path[96];
    keyPath(path, sizeof(path), name);
    FILE* f = disk::open(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, cap, f);
    bool more = fgetc(f) != EOF;
    fclose(f);
    if (!n || more) return false;
    len = static_cast<uint32_t>(n);
    return true;
}

bool keyWrite(const char* name, const uint8_t* buf, uint32_t len) {
    char dir[80], path[96], tmp[100];
    snprintf(dir, sizeof(dir), "%s/ssh", plat::userBase());
    mkdir(dir, 0755);
    keyPath(path, sizeof(path), name);
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    FILE* f = disk::open(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(buf, 1, len, f) == len;
    ok = fflush(f) == 0 && ok;
    fclose(f);
    // A rename within one LittleFS partition replaces the old file whole;
    // nothing is removed first (CLAUDE.md, 1.1.0: never remove a live file
    // to make room for a rename). There is no old file the first time.
    if (!ok || rename(tmp, path) != 0) {
        remove(tmp);
        return false;
    }
    return true;
}

// edKey: load the Ed25519 key, or make and keep one. The fingerprint from
// its public half.
bool edKey(WC_RNG& rng) {
    ed25519_key k;
    if (wc_ed25519_init(&k) != 0) return false;
    bool ok = false;
    word32 idx = 0;
    if (keyRead("host_ed25519", g_edDer, sizeof(g_edDer), g_edLen) &&
        wc_Ed25519PrivateKeyDecode(g_edDer, &idx, &k, g_edLen) == 0) {
        ok = true;
    } else if (!keyAbsent("host_ed25519")) {
        plat::log("ssh: host_ed25519 is there but will not read; not making another");
    } else {
        wc_ed25519_free(&k);
        wc_ed25519_init(&k);
        uint32_t t0 = plat::millis();
        if (wc_ed25519_make_key(&rng, ED25519_KEY_SIZE, &k) == 0) {
            int n = wc_Ed25519KeyToDer(&k, g_edDer, sizeof(g_edDer));
            if (n > 0) {
                g_edLen = static_cast<uint32_t>(n);
                ok = keyWrite("host_ed25519", g_edDer, g_edLen);
                plat::log("ssh: made an Ed25519 host key in %u ms%s",
                          static_cast<unsigned>(plat::millis() - t0), ok ? "" : ", NOT saved");
            }
        }
    }
    if (ok) {
        uint8_t pub[ED25519_PUB_KEY_SIZE];
        word32 pn = sizeof(pub);
        uint8_t blob[4 + 11 + 4 + ED25519_PUB_KEY_SIZE];
        if (wc_ed25519_export_public(&k, pub, &pn) == 0) {
            size_t b = putString(blob, reinterpret_cast<const uint8_t*>("ssh-ed25519"), 11);
            b += putString(blob + b, pub, pn);
            fingerprintOf(blob, b, g_fp[0], sizeof(g_fp[0]));
        }
    }
    wc_ed25519_free(&k);
    return ok;
}

// ecKey: the same for ECDSA P-256.
bool ecKey(WC_RNG& rng) {
    ecc_key k;
    if (wc_ecc_init(&k) != 0) return false;
    bool ok = false;
    word32 idx = 0;
    if (keyRead("host_ecdsa", g_ecDer, sizeof(g_ecDer), g_ecLen) &&
        wc_EccPrivateKeyDecode(g_ecDer, &idx, &k, g_ecLen) == 0) {
        ok = true;
    } else if (!keyAbsent("host_ecdsa")) {
        plat::log("ssh: host_ecdsa is there but will not read; not making another");
    } else {
        wc_ecc_free(&k);
        wc_ecc_init(&k);
        uint32_t t0 = plat::millis();
        if (wc_ecc_make_key_ex(&rng, 32, &k, ECC_SECP256R1) == 0) {
            int n = wc_EccKeyToDer(&k, g_ecDer, sizeof(g_ecDer));
            if (n > 0) {
                g_ecLen = static_cast<uint32_t>(n);
                ok = keyWrite("host_ecdsa", g_ecDer, g_ecLen);
                plat::log("ssh: made an ECDSA P-256 host key in %u ms%s",
                          static_cast<unsigned>(plat::millis() - t0), ok ? "" : ", NOT saved");
            }
        }
    }
    if (ok) {
        uint8_t q[65];
        word32 qn = sizeof(q);
        uint8_t blob[4 + 19 + 4 + 8 + 4 + 65];
        if (wc_ecc_export_x963(&k, q, &qn) == 0) {
            size_t b = putString(blob, reinterpret_cast<const uint8_t*>("ecdsa-sha2-nistp256"), 19);
            b += putString(blob + b, reinterpret_cast<const uint8_t*>("nistp256"), 8);
            b += putString(blob + b, q, qn);
            fingerprintOf(blob, b, g_fp[1], sizeof(g_fp[1]));
        }
    }
    wc_ecc_free(&k);
    return ok;
}

// ---------------------------------------------------------------------------
// wolfSSH's I/O, on the link's socket. The bytes the loop read before it
// knew this was SSH come first.
// ---------------------------------------------------------------------------
int ioRecv(WOLFSSH*, void* buf, word32 sz, void* ctx) {
    Link* l = static_cast<Link*>(ctx);
    if (l->preAt < l->preLen) {
        word32 n = static_cast<word32>(l->preLen - l->preAt);
        if (n > sz) n = sz;
        memcpy(buf, l->pre + l->preAt, n);
        l->preAt = static_cast<uint8_t>(l->preAt + n);
        return static_cast<int>(n);
    }
    ssize_t r = recv(l->sock, buf, sz, MSG_DONTWAIT);
    if (r > 0) return static_cast<int>(r);
    if (r == 0) return WS_CBIO_ERR_CONN_CLOSE;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return WS_CBIO_ERR_WANT_READ;
    if (errno == EINTR) return WS_CBIO_ERR_ISR;
    if (errno == ECONNRESET) return WS_CBIO_ERR_CONN_RST;
    return WS_CBIO_ERR_GENERAL;
}

int ioSend(WOLFSSH*, void* buf, word32 sz, void* ctx) {
    Link* l = static_cast<Link*>(ctx);
    ssize_t r = send(l->sock, buf, sz, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (r >= 0) return static_cast<int>(r);
    if (errno == EAGAIN || errno == EWOULDBLOCK) return WS_CBIO_ERR_WANT_WRITE;
    if (errno == EINTR) return WS_CBIO_ERR_ISR;
    if (errno == EPIPE || errno == ECONNRESET) return WS_CBIO_ERR_CONN_CLOSE;
    return WS_CBIO_ERR_GENERAL;
}

// ---------------------------------------------------------------------------
// userAuth: password (and "none") go to the loop, which answers. Anything
// else is not offered (authTypes) and is refused without asking.
// ---------------------------------------------------------------------------
int authTypes(WOLFSSH*, void*) {
    return WOLFSSH_USERAUTH_PASSWORD;
}

int userAuth(byte type, WS_UserAuthData* d, void* ctx) {
    Link* l = static_cast<Link*>(ctx);
    if (!l || !d) return WOLFSSH_USERAUTH_FAILURE;
    if (type != WOLFSSH_USERAUTH_NONE && type != WOLFSSH_USERAUTH_PASSWORD)
        return WOLFSSH_USERAUTH_FAILURE;

    const uint8_t a = l->auth.load(std::memory_order_acquire);
    if (a == ssh::AUTH_ASKED) return WOLFSSH_USERAUTH_WOULD_BLOCK;
    if (a == ssh::AUTH_YES || a == ssh::AUTH_NO) {
        l->auth.store(ssh::AUTH_IDLE, std::memory_order_relaxed);
        if (a == ssh::AUTH_YES) {
            l->authed.store(true, std::memory_order_release);
            return WOLFSSH_USERAUTH_SUCCESS;
        }
        if (type == WOLFSSH_USERAUTH_PASSWORD && l->pwFails.fetch_add(1) + 1 >= BBS_LOGIN_TRIES)
            return WOLFSSH_USERAUTH_REJECTED;       // the connection goes
        return WOLFSSH_USERAUTH_FAILURE;
    }

    // A fresh question, within the connection's budget.
    if (++l->asks > kAsksMax) return WOLFSSH_USERAUTH_REJECTED;
    // The name and password are the client's bytes: kept only if they fit,
    // never cut (a cut password could match a shorter one).
    if (d->usernameSz == 0 || d->usernameSz >= sizeof(l->user)) return WOLFSSH_USERAUTH_FAILURE;
    memcpy(l->user, d->username, d->usernameSz);
    l->user[d->usernameSz] = '\0';
    l->pass[0] = '\0';
    if (type == WOLFSSH_USERAUTH_PASSWORD) {
        const WS_UserAuthData_Password& pw = d->sf.password;
        if (pw.hasNewPassword || pw.passwordSz >= sizeof(l->pass)) {
            if (l->pwFails.fetch_add(1) + 1 >= BBS_LOGIN_TRIES) return WOLFSSH_USERAUTH_REJECTED;
            return WOLFSSH_USERAUTH_FAILURE;
        }
        memcpy(l->pass, pw.password, pw.passwordSz);
        l->pass[pw.passwordSz] = '\0';
        l->authKind = ssh::AUTH_PASSWORD;
    } else {
        l->authKind = ssh::AUTH_NONE;
    }
    l->auth.store(ssh::AUTH_ASKED, std::memory_order_release);
    plat::wakePost(l->efd);
    return WOLFSSH_USERAUTH_WOULD_BLOCK;
}

int onResize(WOLFSSH*, word32 cols, word32 rows, word32, word32, void* ctx) {
    Link* l = static_cast<Link*>(ctx);
    if (!l) return WS_SUCCESS;
    if (cols < 20)  cols = 20;
    if (cols > 255) cols = 255;
    if (rows < 5)   rows = 5;
    if (rows > 255) rows = 255;
    l->cols.store(static_cast<uint16_t>(cols), std::memory_order_relaxed);
    l->rows.store(static_cast<uint16_t>(rows), std::memory_order_relaxed);
    l->resized.store(true, std::memory_order_release);
    plat::wakePost(l->efd);
    return WS_SUCCESS;
}

// ---------------------------------------------------------------------------
// finish: the task is done with a link. Say goodbye to wolfSSH, close the
// socket, and hand the slot back once the loop has let go too.
// ---------------------------------------------------------------------------
void finish(uint8_t i, const char* why) {
    Link& l = g_link[i];
    if (!l.why[0]) snprintf(l.why, sizeof(l.why), "%s", why);
    if (g_ssh[i]) {
        if (stateOf(l) == LState::Open) wolfSSH_shutdown(g_ssh[i]);   // best effort
        wolfSSH_free(g_ssh[i]);
        g_ssh[i] = nullptr;
    }
    if (l.sock >= 0) {
        close(l.sock);
        l.sock = -1;
    }
    memset(l.pass, 0, sizeof(l.pass));
    l.taskDone.store(true, std::memory_order_release);
    setState(l, LState::Done);
    plat::wakePost(l.efd);            // the loop sees it gone
}

// reclaim: both sides done: the rings back, the slot back.
void reclaim(uint8_t i) {
    Link& l = g_link[i];
    plat::extFree(l.in.buf);
    plat::extFree(l.out.buf);
    l.in.reset(nullptr, 0);
    l.out.reset(nullptr, 0);
    g_blocked[i] = false;
    g_hungAt[i]  = 0;
    setState(l, LState::Free);
}

// start: a claimed link gets its rings and its wolfSSH.
bool start(uint8_t i) {
    Link& l = g_link[i];
    uint8_t* rin  = static_cast<uint8_t*>(plat::extAlloc(BBS_SSH_RING));
    uint8_t* rout = static_cast<uint8_t*>(plat::extAlloc(BBS_SSH_RING));
    WOLFSSH* s    = (rin && rout) ? wolfSSH_new(g_ctx) : nullptr;
    if (!s) {
        plat::extFree(rin);
        plat::extFree(rout);
        return false;
    }
    l.in.reset(rin, BBS_SSH_RING);
    l.out.reset(rout, BBS_SSH_RING);
    wolfSSH_SetIOReadCtx(s, &l);
    wolfSSH_SetIOWriteCtx(s, &l);
    wolfSSH_SetUserAuthCtx(s, &l);
    wolfSSH_SetTerminalResizeCb(s, onResize);
    wolfSSH_SetTerminalResizeCtx(s, &l);
    g_ssh[i]   = s;
    g_since[i] = plat::millis();
    setState(l, LState::Handshake);
    return true;
}

// handshake: one step of wolfSSH_accept. The whole key exchange happens in
// the one call that has the client's KEXINIT and key share to hand.
void handshake(uint8_t i) {
    Link& l = g_link[i];
    if (l.loopDone.load(std::memory_order_acquire)) { finish(i, "caller gone"); return; }
    if (plat::millis() - g_since[i] > BBS_SSH_LOGIN_MS) { finish(i, "login took too long"); return; }
    WOLFSSH* s = g_ssh[i];
    uint32_t t0 = plat::millis();
    int rc = wolfSSH_accept(s);
    uint32_t took = plat::millis() - t0;
    if (took >= 100)
        plat::log("ssh: node %u key exchange step %u ms (in the SSH task)", l.node, static_cast<unsigned>(took));
    if (rc == WS_SUCCESS) {
        // The SSH task's stack at its deepest, said when it is a new low:
        // the key exchange is the deep path, and 16 KB was an estimate
        // (research section 3.3) for the bench to confirm.
        uint32_t freeNow = plat::sideStackFree();
        if (freeNow && (!g_stackLow || freeNow < g_stackLow)) {
            g_stackLow = freeNow;
            plat::log("ssh: task stack least free %u of %u", static_cast<unsigned>(freeNow),
                      static_cast<unsigned>(BBS_SSH_STACK));
        }
        // pty-req came before the shell request, so the size is known now
        // (onResize has put it in the link).
        setState(l, LState::Open);
        plat::wakePost(l.efd);
        return;
    }
    int e = wolfSSH_get_error(s);
    if (e == WS_WANT_READ || e == WS_WANT_WRITE || e == WS_AUTH_PENDING) return;
    char why[40];
    if (e == WS_USER_AUTH_E) snprintf(why, sizeof(why), "wrong password");
    else snprintf(why, sizeof(why), "handshake: %s", wolfSSH_ErrorToName(e));
    finish(i, why);
}

// pump: an open link. The board's output out first (it is what the caller
// is waiting on), then whatever the client sent, into the inbound ring.
void pump(uint8_t i) {
    Link& l = g_link[i];
    WOLFSSH* s = g_ssh[i];
    if (!s->channelList) { finish(i, "channel closed"); return; }
    WOLFSSH_CHANNEL* ch = s->channelList;

    // Out: what the loop left in the ring, as channel data. A send the
    // client's window or the socket cannot take now stays in the ring, and
    // the link is marked blocked: the task then waits on the socket (a
    // window adjust, or room to write) rather than trying again at once,
    // which on the board would spin and starve core 0's idle task until the
    // watchdog restarted the board (a client that never opens its window
    // could do that on purpose).
    g_blocked[i] = false;
    for (int k = 0; k < 8 && l.out.used(); ++k) {
        const uint8_t* p = nullptr;
        size_t n = l.out.peek(p);
        int w = wolfSSH_stream_send(s, const_cast<byte*>(p), static_cast<word32>(n));
        if (w > 0) { l.out.drop(static_cast<size_t>(w)); continue; }
        int e = wolfSSH_get_error(s);
        if (w == 0 || w == WS_WINDOW_FULL || w == WS_WANT_WRITE || w == WS_REKEYING ||
            e == WS_WINDOW_FULL || e == WS_WANT_WRITE || e == WS_REKEYING) {
            g_blocked[i] = true;
            break;
        }
        finish(i, "send failed");
        return;
    }

    // In: wolfSSH reads the socket (and window adjusts, a rekey, a close);
    // channel data waits in the channel's own buffer until the ring has room,
    // which is what holds a fast paste back: the client's window stops
    // opening.
    for (int k = 0; k < 8; ++k) {
        WOLFSSH_BUFFER& ib = ch->inputBuffer;
        if (ib.length > ib.idx && l.in.room()) {
            uint8_t tmp[256];
            word32 want = static_cast<word32>(l.in.room() < sizeof(tmp) ? l.in.room() : sizeof(tmp));
            int n = wolfSSH_ChannelIdRead(s, ch->channel, tmp, want);
            if (n > 0) {
                l.in.push(tmp, static_cast<size_t>(n));
                plat::wakePost(l.efd);
                continue;
            }
        }
        int rc = wolfSSH_worker(s, nullptr);
        int e  = wolfSSH_get_error(s);
        if (rc == WS_CHAN_RXD || e == WS_CHAN_RXD) continue;
        if (rc == WS_SUCCESS) continue;
        if (e == WS_WANT_READ || e == WS_WANT_WRITE || e == WS_WINDOW_FULL ||
            e == WS_REKEYING || rc == WS_REKEYING) break;
        if (e == WS_CHANNEL_CLOSED || e == WS_EOF || rc == WS_CHANNEL_CLOSED) {
            finish(i, "caller closed");
            return;
        }
        finish(i, e == WS_SOCKET_ERROR_E ? "connection lost" : wolfSSH_ErrorToName(e));
        return;
    }
    if (!s->channelList) { finish(i, "caller closed"); return; }

    // The session is over and everything it said has been sent; or ten
    // seconds have gone trying to send the rest to a client that stopped
    // taking it, and the slot is wanted more than the goodbye.
    if (l.loopDone.load(std::memory_order_acquire)) {
        if (!g_hungAt[i]) g_hungAt[i] = plat::millis() | 1u;
        if (!l.out.used() && s->outputBuffer.length == 0) finish(i, "hung up");
        else if (plat::millis() - g_hungAt[i] > 10000u) finish(i, "hung up, the rest undelivered");
    }
}

// inPending: wolfSSH holds bytes it has not handed on: unprocessed packets,
// or channel data waiting for room in the inbound ring.
bool inPending(uint8_t i) {
    WOLFSSH* s = g_ssh[i];
    if (!s) return false;
    if (s->inputBuffer.length > s->inputBuffer.idx) return true;
    WOLFSSH_CHANNEL* ch = s->channelList;
    return ch && ch->inputBuffer.length > ch->inputBuffer.idx && g_link[i].in.room();
}

// wantsWrite: wolfSSH is holding bytes the socket would not take.
bool wantsWrite(uint8_t i) {
    return g_ssh[i] && g_ssh[i]->outputBuffer.length > g_ssh[i]->outputBuffer.idx;
}

void taskMain(void*) {
    uint32_t streak = 0, lastPassMs = 0;
    for (;;) {
        fd_set rfds, wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        int maxfd = g_wake;
        FD_SET(g_wake, &rfds);
        bool busy = false;                     // something to do without waiting
        const uint32_t now = plat::millis();
        for (uint8_t i = 0; i < BBS_SSH_MAX; ++i) {
            Link& l = g_link[i];
            LState st = stateOf(l);
            if (st == LState::Start) { busy = true; continue; }
            if (st == LState::Done) { if (l.loopDone.load(std::memory_order_acquire)) busy = true; continue; }
            if (st != LState::Handshake && st != LState::Open) continue;
            if (l.sock < 0) continue;
            // Reading stops while the inbound ring is full: the channel
            // window does the holding back.
            if (st == LState::Handshake || l.in.room()) FD_SET(l.sock, &rfds);
            if (wantsWrite(i)) FD_SET(l.sock, &wfds);
            if (l.sock > maxfd) maxfd = l.sock;
            if (l.preAt < l.preLen) busy = true;
            // Only what can move now counts: output a blocked link cannot
            // send waits for its socket to say so.
            if (st == LState::Open && !g_blocked[i] &&
                (l.out.used() || l.loopDone.load(std::memory_order_acquire)))
                busy = true;
            if (st == LState::Open && inPending(i)) busy = true;
            if (st == LState::Handshake && l.auth.load(std::memory_order_acquire) >= ssh::AUTH_YES)
                busy = true;
            if (st == LState::Open && g_hungAt[i] && now - g_hungAt[i] > 10000u) busy = true;
        }
        // Never a pass without a wait in a long run of them, and a pause
        // after a pass that held the CPU (a key exchange): this task is above
        // core 0's idle task, and an idle task that never runs is a restart
        // by the watchdog.
        timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = busy ? 0 : 250 * 1000;      // a quarter second: login timeouts
        if (busy && ++streak >= 16) { tv.tv_usec = 2000; streak = 0; }
        if (!busy) streak = 0;
        if (lastPassMs >= 50) tv.tv_usec = 10 * 1000;
        select(maxfd + 1, &rfds, &wfds, nullptr, &tv);
        plat::wakeTake(g_wake);
        const uint32_t passStart = plat::millis();

        for (uint8_t i = 0; i < BBS_SSH_MAX; ++i) {
            Link& l = g_link[i];
            switch (stateOf(l)) {
                case LState::Start:
                    if (!start(i)) { finish(i, "no memory"); break; }
                    handshake(i);
                    break;
                case LState::Handshake:
                    handshake(i);
                    if (stateOf(l) == LState::Open) pump(i);
                    break;
                case LState::Open:
                    pump(i);
                    break;
                case LState::Done:
                    if (l.loopDone.load(std::memory_order_acquire)) reclaim(i);
                    break;
                default:
                    break;
            }
        }
        lastPassMs = plat::millis() - passStart;
    }
}

} // namespace

namespace sshd {

bool running() { return g_up.load(std::memory_order_acquire); }
uint8_t boardCap() { return BBS_SSH_MAX; }
const char* fingerprint(uint8_t which) { return which < 2 ? g_fp[which] : ""; }
const char* offWhy() { return running() ? "" : g_off; }

uint8_t inUse() {
    uint8_t n = 0;
    for (const Link& l : g_link) if (stateOf(l) != LState::Free) ++n;
    return n;
}

// cap: the board's figure, or fewer when PSRAM cannot hold that many more
// sessions now, whichever is lower.
uint8_t cap() {
    const uint8_t used = inUse();
    const uint32_t freeB = plat::extFreeBytes();
    uint32_t room = freeB > BBS_SSH_PSRAM_KEEP ? (freeB - BBS_SSH_PSRAM_KEEP) / BBS_SSH_PSRAM_EACH : 0;
    uint32_t c = used + room;
    return static_cast<uint8_t>(c < BBS_SSH_MAX ? c : BBS_SSH_MAX);
}

bool begin() {
    if (running()) return true;
    uint32_t t0 = plat::millis();
    // A board with no PSRAM (an S3 without it, which the 8 MB image will
    // boot on) says SSH is off rather than every client hearing "full".
    if (plat::extFreeBytes() == 0) {
        snprintf(g_off, sizeof(g_off), "no PSRAM on this board");
        plat::log("ssh: off: no PSRAM");
        return false;
    }
    if (wolfSSH_Init() != WS_SUCCESS) { snprintf(g_off, sizeof(g_off), "the library did not start"); return false; }

    WC_RNG rng;
    if (wc_InitRng(&rng) != 0) { snprintf(g_off, sizeof(g_off), "no random numbers"); return false; }
    bool keys = edKey(rng) && ecKey(rng);
    wc_FreeRng(&rng);
    if (!keys) {
        snprintf(g_off, sizeof(g_off), "no host keys");
        plat::log("ssh: off: the host keys could not be read or made");
        return false;
    }

    g_ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_SERVER, nullptr);
    if (!g_ctx ||
        wolfSSH_CTX_UsePrivateKey_buffer(g_ctx, g_edDer, g_edLen, WOLFSSH_FORMAT_ASN1) != WS_SUCCESS ||
        wolfSSH_CTX_UsePrivateKey_buffer(g_ctx, g_ecDer, g_ecLen, WOLFSSH_FORMAT_ASN1) != WS_SUCCESS) {
        snprintf(g_off, sizeof(g_off), "the host keys were refused");
        plat::log("ssh: off: wolfSSH refused the host keys");
        return false;
    }
    wolfSSH_CTX_SetSshProtoIdStr(g_ctx, kServerId);
    wolfSSH_CTX_SetWindowPacketSize(g_ctx, BBS_SSH_WINDOW, BBS_SSH_PACKET);
    wolfSSH_SetUserAuth(g_ctx, userAuth);
    wolfSSH_SetUserAuthTypes(g_ctx, authTypes);
    wolfSSH_SetIORecv(g_ctx, ioRecv);
    wolfSSH_SetIOSend(g_ctx, ioSend);

    g_wake = plat::wakeOpen(kWakes);
    for (Link& l : g_link) {
        l.efd = plat::wakeOpen(kWakes);
        if (l.efd < 0 || g_wake < 0) {
            snprintf(g_off, sizeof(g_off), "no wake descriptors");
            plat::log("ssh: off: no eventfd");
            return false;
        }
    }
    if (!plat::sideTask(taskMain, nullptr, BBS_SSH_STACK, "ssh")) {
        snprintf(g_off, sizeof(g_off), "the task did not start");
        plat::log("ssh: off: the SSH task did not start");
        return false;
    }
    g_off[0] = '\0';
    g_up.store(true, std::memory_order_release);
    plat::log("ssh: on the telnet port, up to %u at once, ready in %u ms",
              static_cast<unsigned>(BBS_SSH_MAX), static_cast<unsigned>(plat::millis() - t0));
    plat::log("ssh: host key ed25519 %s", g_fp[0]);
    plat::log("ssh: host key ecdsa   %s", g_fp[1]);
    return true;
}

ssh::Link* claim(int sock, const uint8_t* pre, size_t n, uint32_t peer, uint32_t local, uint8_t node) {
    if (!running() || n > ssh::kPreMax) { snprintf(g_refused, sizeof(g_refused), "--> SSH is off on this board"); return nullptr; }
    if (inUse() >= cap()) { snprintf(g_refused, sizeof(g_refused), "%s", kFullWhy); return nullptr; }
    uint8_t fromPeer = 0;
    for (const Link& o : g_link) {
        LState st = stateOf(o);
        if ((st == LState::Start || st == LState::Handshake) && o.peer == peer) ++fromPeer;
    }
    if (fromPeer >= kPerPeer) {
        snprintf(g_refused, sizeof(g_refused), "--> Too many SSH logins at once from you");
        return nullptr;
    }
    for (Link& l : g_link) {
        if (stateOf(l) != LState::Free) continue;
        plat::wakeTake(l.efd);                // nothing left over from the last caller
        l.loopDone.store(false, std::memory_order_relaxed);
        l.taskDone.store(false, std::memory_order_relaxed);
        l.sock    = sock;
        l.node    = node;
        l.peer    = peer;
        l.local   = local;
        if (n) memcpy(l.pre, pre, n);            // SSH's own port hands over nothing read
        l.preLen  = static_cast<uint8_t>(n);
        l.preAt   = 0;
        l.asks    = 0;
        l.cols.store(80, std::memory_order_relaxed);
        l.rows.store(24, std::memory_order_relaxed);
        l.resized.store(false, std::memory_order_relaxed);
        l.auth.store(ssh::AUTH_IDLE, std::memory_order_relaxed);
        l.authKind = ssh::AUTH_NONE;
        memset(l.user, 0, sizeof(l.user));
        memset(l.pass, 0, sizeof(l.pass));
        l.pwFails.store(0, std::memory_order_relaxed);
        l.authed.store(false, std::memory_order_relaxed);
        l.why[0]  = '\0';
        setState(l, LState::Start);           // release: everything above first
        plat::wakePost(g_wake);
        return &l;
    }
    return nullptr;
}

size_t refusal(uint8_t* out, size_t cap, const char* why) {
    if (!why) why = kFullWhy;
    const size_t idLen   = sizeof(kServerId) - 1;
    const size_t whyLen  = strlen(why);
    const size_t payload = 1 + 4 + 4 + whyLen + 4;           // msg, reason, why, language
    size_t pad = 8 - ((4 + 1 + payload) % 8);
    if (pad < 4) pad += 8;
    const size_t total = idLen + 4 + 1 + payload + pad;
    if (cap < total) return 0;
    uint8_t* p = out;
    memcpy(p, kServerId, idLen);
    p += idLen;
    const uint32_t plen = static_cast<uint32_t>(1 + payload + pad);
    *p++ = static_cast<uint8_t>(plen >> 24);
    *p++ = static_cast<uint8_t>(plen >> 16);
    *p++ = static_cast<uint8_t>(plen >> 8);
    *p++ = static_cast<uint8_t>(plen);
    *p++ = static_cast<uint8_t>(pad);
    *p++ = 1;                                                  // SSH_MSG_DISCONNECT
    *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 12;                   // too many connections
    p += putString(p, reinterpret_cast<const uint8_t*>(why), static_cast<uint32_t>(whyLen));
    *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 0;                    // language: none
    memset(p, 0, pad);                                         // unencrypted: any padding
    p += pad;
    return static_cast<size_t>(p - out);
}

size_t read(ssh::Link* l, uint8_t* d, size_t n) {
    if (!l) return 0;
    plat::wakeTake(l->efd);
    size_t got = l->in.pop(d, n);
    // More waiting than this read took, or the link gone behind the last of
    // it: stay readable, so the next pass reads on or sees the end.
    if (l->in.used() || gone(l)) plat::wakePost(l->efd);
    // Room made: the task stops reading the socket while the ring is full.
    if (got) plat::wakePost(g_wake);
    return got;
}

int write(ssh::Link* l, const uint8_t* d, size_t n) {
    if (!l || gone(l)) return -1;
    // Posted on every push that added bytes, not only on the first into an
    // empty ring: the task may drain the ring and go to sleep between this
    // side looking and publishing, and the eventfd's counter merges posts.
    size_t put = l->out.push(d, n);
    if (put) plat::wakePost(g_wake);
    return static_cast<int>(put);
}

uint8_t lingering() {
    uint8_t n = 0;
    for (const Link& l : g_link) {
        LState st = stateOf(l);
        if (st != LState::Free && st != LState::Done && l.loopDone.load(std::memory_order_acquire)) ++n;
    }
    return n;
}

bool gone(const ssh::Link* l) {
    return !l || l->taskDone.load(std::memory_order_acquire);
}

bool open(const ssh::Link* l) {
    return l && stateOf(*l) == LState::Open;
}

void release(ssh::Link* l) {
    if (!l) return;
    // A question the loop never answered: no, and wiped.
    if (l->auth.load(std::memory_order_acquire) == ssh::AUTH_ASKED) {
        memset(l->pass, 0, sizeof(l->pass));
        l->auth.store(ssh::AUTH_NO, std::memory_order_release);
    }
    l->loopDone.store(true, std::memory_order_release);
    plat::wakePost(g_wake);
}

bool asked(ssh::Link* l, uint8_t& kind, const char*& user, const char*& pass) {
    if (!l || l->auth.load(std::memory_order_acquire) != ssh::AUTH_ASKED) return false;
    kind = l->authKind;
    user = l->user;
    pass = l->pass;
    return true;
}

void answer(ssh::Link* l, bool yes) {
    if (!l) return;
    memset(l->pass, 0, sizeof(l->pass));
    l->auth.store(yes ? ssh::AUTH_YES : ssh::AUTH_NO, std::memory_order_release);
    plat::wakePost(g_wake);
}

const char* refused() { return g_refused; }

const char* why(const ssh::Link* l) {
    return gone(l) && l->why[0] ? l->why : "";
}

bool wrongPasswords(const ssh::Link* l) {
    return l && l->pwFails.load(std::memory_order_acquire);
}

} // namespace sshd
#endif  // BBS_HAS_SSH
