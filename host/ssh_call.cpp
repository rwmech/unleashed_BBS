/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         host/ssh_call.cpp
 * Module:       Tests / an SSH caller for the host board (1.1.2)
 *
 * Purpose:      Dials the host board over SSH with wolfSSH's own client and
 *               joins it to stdin and stdout, the way `ssh -tt` would:
 *               tools/testclient.py's SshCaller runs it on a pipe. The
 *               server's host key fingerprint goes to stderr as
 *               "ssh_call: hostkey <type> SHA256:...", the end as
 *               "ssh_call: closed ...".
 *
 *                 ssh_call [-p port] [-u user] [-w password] [-b from]
 *                          [-R cols:rows@ms] [-k ed25519|ecdsa] host
 *
 *               -R sends a window-change that long after the shell opens.
 *               -k offers only that host key type, to prove each one.
 *
 *               OpenSSH would be the better witness, being somebody else's
 *               implementation, but this machine does not allow an ssh
 *               client to run. wolfSSH's client and server are separate
 *               code paths; the refusal and the disconnect packet are
 *               checked by testclient.py's own parser, which shares nothing.
 *
 * Libraries:    wolfSSH 1.5.0, wolfCrypt 5.9.4 (host/Makefile, libwolfssh_host.a)
 * Targets:      the Linux host build
 * See also:     src/core/sshd.h, tools/testclient.py
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

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <wolfssh/ssh.h>
#include <wolfssh/error.h>
}

// As in src/core/sshd.cpp: NO_CERTS leaves wolfSSH one reference to the PEM
// decoder, which nothing here uses.
extern "C" int wc_KeyPemToDer(const unsigned char*, int, unsigned char*, int, const char*) {
    return NOT_COMPILED_IN;
}

namespace {

const char* g_pass = nullptr;

uint32_t nowMs() {
    timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint32_t>(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
}

// The key's fingerprint, the way OpenSSH prints it.
void fingerprint(const unsigned char* blob, unsigned n, char* out, size_t cap) {
    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char h[32];
    wc_Sha256Hash(blob, n, h);
    size_t o = static_cast<size_t>(snprintf(out, cap, "SHA256:"));
    for (int i = 0; i < 32 && o + 4 < cap; i += 3) {
        unsigned v = static_cast<unsigned>(h[i]) << 16;
        if (i + 1 < 32) v |= static_cast<unsigned>(h[i + 1]) << 8;
        if (i + 2 < 32) v |= h[i + 2];
        out[o++] = kB64[(v >> 18) & 63];
        out[o++] = kB64[(v >> 12) & 63];
        if (i + 1 < 32) out[o++] = kB64[(v >> 6) & 63];
        if (i + 2 < 32) out[o++] = kB64[v & 63];
    }
    out[o] = '\0';
}

int keyCheck(const unsigned char* pub, unsigned int pubSz, void*) {
    char fp[64];
    fingerprint(pub, pubSz, fp, sizeof(fp));
    // The blob starts with its type as an SSH string.
    unsigned tn = pubSz >= 4 ? (pub[0] << 24 | pub[1] << 16 | pub[2] << 8 | pub[3]) : 0;
    char type[40] = "?";
    if (tn && tn < sizeof(type) && 4 + tn <= pubSz) {
        memcpy(type, pub + 4, tn);
        type[tn] = '\0';
    }
    fprintf(stderr, "ssh_call: hostkey %s %s\n", type, fp);
    return 0;                                         // trusted: it is our own board
}

int userAuth(byte type, WS_UserAuthData* d, void*) {
    if (type == WOLFSSH_USERAUTH_PASSWORD && g_pass) {
        d->sf.password.password   = reinterpret_cast<const byte*>(g_pass);
        d->sf.password.passwordSz = static_cast<word32>(strlen(g_pass));
        return WOLFSSH_USERAUTH_SUCCESS;
    }
    return WOLFSSH_USERAUTH_FAILURE;
}

bool waitFd(int fd, bool wr, int ms) {
    fd_set s;
    FD_ZERO(&s);
    FD_SET(fd, &s);
    timeval tv{ ms / 1000, (ms % 1000) * 1000 };
    return select(fd + 1, wr ? nullptr : &s, wr ? &s : nullptr, nullptr, &tv) > 0;
}

} // namespace

int main(int argc, char** argv) {
    int port = 6400;
    const char* user = "guest";
    const char* key  = nullptr;
    const char* from = nullptr;
    unsigned rc = 0, rr = 0, rAt = 0;
    int opt;
    while ((opt = getopt(argc, argv, "p:u:w:R:k:b:")) != -1) {
        if (opt == 'p') port = atoi(optarg);
        else if (opt == 'u') user = optarg;
        else if (opt == 'w') g_pass = optarg;
        else if (opt == 'R') sscanf(optarg, "%u:%u@%u", &rc, &rr, &rAt);
        else if (opt == 'k') key = optarg;
        else if (opt == 'b') from = optarg;
        else { fprintf(stderr, "usage: ssh_call [-p port] [-u user] [-w pass] [-R c:r@ms] [-k type] host\n"); return 2; }
    }
    if (optind >= argc) { fprintf(stderr, "ssh_call: no host\n"); return 2; }
    const char* host = argv[optind];

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port   = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1) { fprintf(stderr, "ssh_call: bad address\n"); return 2; }
    if (from) {                                       // -b: call from this address
        sockaddr_in b{};
        b.sin_family = AF_INET;
        if (inet_pton(AF_INET, from, &b.sin_addr) != 1 ||
            bind(fd, reinterpret_cast<sockaddr*>(&b), sizeof(b)) != 0) {
            fprintf(stderr, "ssh_call: cannot call from %s\n", from);
            return 2;
        }
    }
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        fprintf(stderr, "ssh_call: connect: %s\n", strerror(errno));
        return 1;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    wolfSSH_Init();
    WOLFSSH_CTX* ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_CLIENT, nullptr);
    wolfSSH_CTX_SetPublicKeyCheck(ctx, keyCheck);
    wolfSSH_SetUserAuth(ctx, userAuth);
    if (key && !strcmp(key, "ed25519")) wolfSSH_CTX_SetAlgoListKey(ctx, "ssh-ed25519");
    if (key && !strcmp(key, "ecdsa"))   wolfSSH_CTX_SetAlgoListKey(ctx, "ecdsa-sha2-nistp256");
    WOLFSSH* ssh = wolfSSH_new(ctx);
    wolfSSH_set_fd(ssh, fd);
    wolfSSH_SetUsername(ssh, user);
    wolfSSH_SetChannelType(ssh, WOLFSSH_SESSION_TERMINAL, nullptr, 0);

    uint32_t t0 = nowMs();
    for (;;) {
        int r = wolfSSH_connect(ssh);
        if (r == WS_SUCCESS) break;
        int e = wolfSSH_get_error(ssh);
        if ((e == WS_WANT_READ || e == WS_WANT_WRITE) && nowMs() - t0 < 30000) {
            waitFd(fd, e == WS_WANT_WRITE, 100);
            continue;
        }
        fprintf(stderr, "ssh_call: closed in the handshake: %s (%d)\n", wolfSSH_ErrorToName(e), e);
        return 1;
    }
    fprintf(stderr, "ssh_call: open in %u ms\n", static_cast<unsigned>(nowMs() - t0));
    fflush(stderr);

    uint32_t opened = nowMs();
    bool stdinOpen = true, resized = false;
    unsigned char buf[4096];
    for (;;) {
        if (rc && !resized && nowMs() - opened >= rAt) {
            wolfSSH_ChangeTerminalSize(ssh, rc, rr, 0, 0);
            resized = true;
        }
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(fd, &rs);
        if (stdinOpen) FD_SET(0, &rs);
        timeval tv{ 0, 50 * 1000 };
        select(fd + 1, &rs, nullptr, nullptr, &tv);
        if (stdinOpen && FD_ISSET(0, &rs)) {
            ssize_t n = read(0, buf, sizeof(buf));
            if (n <= 0) stdinOpen = false;
            else {
                ssize_t off = 0;
                uint32_t ts = nowMs();
                while (off < n && nowMs() - ts < 10000) {
                    int w = wolfSSH_stream_send(ssh, buf + off, static_cast<word32>(n - off));
                    if (w > 0) { off += w; continue; }
                    wolfSSH_worker(ssh, nullptr);          // a window adjust, perhaps
                    waitFd(fd, false, 20);
                }
            }
        }
        // Everything the board has sent.
        for (;;) {
            int n = wolfSSH_stream_read(ssh, buf, sizeof(buf));
            if (n > 0) { fwrite(buf, 1, static_cast<size_t>(n), stdout); fflush(stdout); continue; }
            int e = wolfSSH_get_error(ssh);
            if (e == WS_WANT_READ || e == WS_WANT_WRITE || e == WS_REKEYING || e == WS_CHAN_RXD) break;
            fprintf(stderr, "ssh_call: closed: %s (%d)\n", wolfSSH_ErrorToName(e), e);
            fflush(stderr);
            wolfSSH_free(ssh);
            wolfSSH_CTX_free(ctx);
            close(fd);
            return 0;
        }
    }
}
