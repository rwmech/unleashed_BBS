/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/backup.cpp
 * Module:       Core / backup window
 *
 * Purpose:      Backup window HTTP service (see backup.h). Minimal HTTP/1.1:
 *                  one request per connection, Connection: close, bodies need
 *                  Content-Length (no chunked uploads), Expect: 100-continue
 *                  honoured.
 *
 * Libraries:    BSD sockets (lwIP on ESP32, libc on host)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     BACKUP.md
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

#include "backup.h"
#include "sysconfig.h"
#include "guard.h"
#include "../platform/platform.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <sys/stat.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace {

void nonBlocking(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

void uploadPath(char* out, size_t n) {
    snprintf(out, n, "%s/%s/upload.zip", plat::fsBase(), BBS_BACKUP_STAGING);
}

// headerValue: case-insensitive "name:" lookup in the header block
const char* headerValue(const char* block, const char* name) {
    size_t nlen = strlen(name);
    const char* p = strstr(block, "\r\n");
    while (p && p[2] && !(p[2] == '\r' && p[3] == '\n')) {
        p += 2;
        bool match = true;
        for (size_t i = 0; i < nlen; ++i) {
            if (tolower(static_cast<unsigned char>(p[i])) != name[i]) { match = false; break; }
        }
        if (match && p[nlen] == ':') {
            const char* v = p + nlen + 1;
            while (*v == ' ' || *v == '\t') ++v;
            return v;
        }
        p = strstr(p, "\r\n");
    }
    return nullptr;
}

} // namespace

// ===========================================================================
// Window
// ===========================================================================

bool BackupService::open(uint32_t now, const char* localIp) {
    if (lfd_ >= 0) return true;
    const SysConfig& cfg = syscfg::get();

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) { note("*** Backup window failed: no socket"); return false; }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_port        = htons(cfg.backupPort);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) < 0 || listen(fd, 1) < 0) {
        ::close(fd);
        note("*** Backup window failed: port %u busy", cfg.backupPort);
        return false;
    }
    nonBlocking(fd);
    lfd_      = fd;
    closesAt_ = now + static_cast<uint32_t>(cfg.backupMinutes) * 60000u;
    note("*** Backup open %u min: http://%s:%u/backup.zip", cfg.backupMinutes, localIp, cfg.backupPort);
    return true;
}

void BackupService::close(const char* why) {
    if (cfd_ >= 0 && st_ != St::Reply) dropClient(why);     // a queued reply still gets sent
    if (lfd_ >= 0) {
        ::close(lfd_);
        lfd_ = -1;
        note("*** Backup window closed (%s)", why);
    }
}

void BackupService::addFds(fd_set& r, fd_set& w, int& maxfd) const {
    if (lfd_ >= 0) {
        FD_SET(lfd_, &r);
        if (lfd_ > maxfd) maxfd = lfd_;
    }
    if (cfd_ >= 0) {
        FD_SET(cfd_, &r);
        if (outPos_ < outLen_ || st_ == St::SendZip) FD_SET(cfd_, &w);
        if (cfd_ > maxfd) maxfd = cfd_;
    }
}

// ---------------------------------------------------------------------------
// service: accept, read, write, extract one entry, timers
// ---------------------------------------------------------------------------
void BackupService::service(const fd_set& r, const fd_set& w, uint32_t now) {
    if (lfd_ >= 0 && FD_ISSET(lfd_, &r)) acceptClient(now);
    if (cfd_ >= 0 && FD_ISSET(cfd_, &r)) readClient(now);
    if (cfd_ >= 0 && (FD_ISSET(cfd_, &w) || outPos_ < outLen_ || st_ == St::SendZip)) writeClient(now);

    if (st_ == St::Extract && !imp_.step()) {
        const ziparc::ImportReport& rep = imp_.report();
        if (!rep.accepted) {
            char body[160];
            snprintf(body, sizeof(body), "Nothing to apply. %u rejected%s%s\n", rep.rejected,
                     rep.rejected ? ", first: " : "", rep.firstReject);
            note("*** Upload from %s refused: nothing usable", clientIp_);
            imp_.discard();
            reply(422, "Unprocessable Entity", body);
        } else {
            snprintf(summary_, sizeof(summary_), "Upload from %s: %u file%s, %u KB%s%s%s",
                     clientIp_, rep.accepted, rep.accepted == 1 ? "" : "s",
                     static_cast<unsigned>((rep.bytes + 1023) / 1024),
                     rep.hasCfg ? ", system.cfg" : "", rep.hasUsers ? ", users" : "",
                     rep.hasScreens ? ", screens" : "");
            if (rep.rejected) snprintf(detail_, sizeof(detail_), "%u rejected (%.60s), %u screens removed",
                                       rep.rejected, rep.firstReject, rep.removed);
            else              snprintf(detail_, sizeof(detail_), "nothing rejected, %u screens removed", rep.removed);
            if (rep.note[0]) {
                size_t at = strlen(detail_);
                snprintf(detail_ + at, sizeof(detail_) - at, "; %.70s", rep.note);
            }
            st_        = St::Approve;
            approveBy_ = now + BBS_BACKUP_APPROVE_MS;
            plat::log("backup: %s; %s; waiting for sysop", summary_, detail_);
        }
    }

    if (st_ == St::Approve && static_cast<int32_t>(now - approveBy_) >= 0) {
        decide(false, "no answer from the sysop in time");
    }

    if (cfd_ >= 0 && st_ != St::Approve && st_ != St::Extract) {
        if (now - lastIo_ > BBS_BACKUP_IDLE_MS)                  dropClient("idle timeout");
        else if (static_cast<int32_t>(now - deadline_) >= 0)     dropClient("too slow");
    }

    if (lfd_ >= 0 && static_cast<int32_t>(now - closesAt_) >= 0) {
        ::close(lfd_);
        lfd_ = -1;
        note("*** Backup window closed (time up)");
    }
}

// ===========================================================================
// Client
// ===========================================================================

void BackupService::acceptClient(uint32_t now) {
    for (;;) {
        sockaddr_in a;
        socklen_t al = sizeof(a);
        int fd = accept(lfd_, reinterpret_cast<sockaddr*>(&a), &al);
        if (fd < 0) return;
        if (cfd_ >= 0) {                                   // one client at a time
            static const char busy[] = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 5\r\nConnection: close\r\n\r\nbusy\n";
            send(fd, busy, sizeof(busy) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
            ::close(fd);
            continue;
        }
        nonBlocking(fd);
        cfd_    = fd;
        st_     = St::Headers;
        hdrLen_ = 0;
        outLen_ = outPos_ = 0;
        closeAfterOut_ = false;
        lastIo_   = now;
        deadline_ = now + BBS_BACKUP_HEADER_MS;
        ipToText(a.sin_addr.s_addr, clientIp_, sizeof(clientIp_));
        plat::log("backup: client %s", clientIp_);
    }
}

void BackupService::readClient(uint32_t now) {
    if (st_ == St::Headers) {
        size_t room = sizeof(hdr_) - 1 - hdrLen_;
        ssize_t n = recv(cfd_, hdr_ + hdrLen_, room, MSG_DONTWAIT);
        if (n == 0) { dropClient("client closed"); return; }
        if (n < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK) dropClient("read error"); return; }
        hdrLen_ = static_cast<uint16_t>(hdrLen_ + n);
        hdr_[hdrLen_] = '\0';
        lastIo_ = now;
        if (strstr(hdr_, "\r\n\r\n")) { route(now); return; }
        if (hdrLen_ >= sizeof(hdr_) - 1) reply(431, "Request Header Fields Too Large", "headers too large\n");
        return;
    }

    uint8_t buf[1024];
    ssize_t n = recv(cfd_, buf, sizeof(buf), MSG_DONTWAIT);
    if (n == 0) {
        if (st_ == St::Body || st_ == St::Extract || st_ == St::Approve) note("*** Upload from %s cancelled", clientIp_);
        dropClient("client closed");
        return;
    }
    if (n < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK) dropClient("read error"); return; }
    lastIo_ = now;
    if (st_ != St::Body) return;                           // not expecting data: ignore it

    size_t take = static_cast<size_t>(n) < bodyLeft_ ? static_cast<size_t>(n) : bodyLeft_;
    if (fwrite(buf, 1, take, upload_) != take) {
        fclose(upload_);
        upload_ = nullptr;
        imp_.discard();
        reply(507, "Insufficient Storage", "storage full while receiving the upload\n");
        return;
    }
    bodyLeft_ -= static_cast<uint32_t>(take);
    if (bodyLeft_ == 0) {
        fclose(upload_);
        upload_ = nullptr;
        char path[96], err[96];
        uploadPath(path, sizeof(path));
        if (!imp_.open(path, err, sizeof(err))) {
            imp_.discard();
            char body[128];
            snprintf(body, sizeof(body), "rejected: %s\n", err);
            note("*** Upload from %s refused: %s", clientIp_, err);
            reply(400, "Bad Request", body);
            return;
        }
        st_ = St::Extract;
    }
}

void BackupService::writeClient(uint32_t now) {
    for (int rounds = 0; rounds < 8; ++rounds) {
        if (outPos_ < outLen_) {
            ssize_t n = send(cfd_, out_ + outPos_, outLen_ - outPos_, MSG_DONTWAIT | MSG_NOSIGNAL);
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) dropClient("write error");
                return;
            }
            outPos_ = static_cast<uint16_t>(outPos_ + n);
            lastIo_ = now;
            if (outPos_ < outLen_) return;                 // socket full
        }
        outPos_ = outLen_ = 0;
        if (st_ == St::SendZip) {
            size_t n = exp_.produce(out_, sizeof(out_));
            if (n == 0) {
                note("*** Backup downloaded by %s: %u files, %u KB", clientIp_, exp_.entries(),
                     static_cast<unsigned>((exp_.totalBytes() + 1023) / 1024));
                dropClient("download done");
                return;
            }
            outLen_ = static_cast<uint16_t>(n);
            continue;
        }
        if (closeAfterOut_) dropClient("reply sent");
        return;
    }
}

// ---------------------------------------------------------------------------
// route: request line and headers are in hdr_
// ---------------------------------------------------------------------------
void BackupService::route(uint32_t now) {
    deadline_ = now + BBS_BACKUP_TRANSFER_MS;          // download or upload body from here
    char* end = strstr(hdr_, "\r\n\r\n");
    size_t headLen = static_cast<size_t>(end - hdr_) + 4;
    size_t extra   = hdrLen_ - headLen;
    end[2] = '\0';                                         // block ends after the last header CRLF

    char method[8] = {};
    char path[64]  = {};
    if (sscanf(hdr_, "%7s %63s", method, path) != 2 || !strstr(hdr_, " HTTP/1.")) {
        reply(400, "Bad Request", "bad request line\n");
        return;
    }
    char* q = strchr(path, '?');
    if (q) *q = '\0';
    plat::log("backup: %s %s %s", clientIp_, method, path);

    const SysConfig& cfg = syscfg::get();
    bool get = !strcmp(method, "GET");
    bool put = !strcmp(method, "PUT") || !strcmp(method, "POST");

    if (get && (!strcmp(path, "/") || !strcmp(path, "/index.txt"))) {
        char body[400];
        uint32_t left = lfd_ >= 0 ? (closesAt_ - plat::millis()) / 1000u : 0;
        snprintf(body, sizeof(body),
                 "%s %s backup window, closes in %u s\n\n"
                 "download: curl -o backup.zip http://%s.local:%u/backup.zip\n"
                 "upload:   curl -T backup.zip http://%s.local:%u/restore\n\n"
                 "Uploads wait for the sysop to answer Y/N. See BACKUP.md.\n",
                 BBS_NAME, BBS_VERSION, static_cast<unsigned>(left),
                 cfg.hostname, cfg.backupPort, cfg.hostname, cfg.backupPort);
        reply(200, "OK", body);
        return;
    }

    if (get && !strcmp(path, "/backup.zip")) {
        char err[64];
        if (!exp_.scan(cfg.hostname, err, sizeof(err))) { reply(500, "Internal Server Error", err); return; }
        int w = snprintf(reinterpret_cast<char*>(out_), sizeof(out_),
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Type: application/zip\r\n"
                         "Content-Length: %u\r\n"
                         "Content-Disposition: attachment; filename=\"%s-backup.zip\"\r\n"
                         "Connection: close\r\n\r\n",
                         static_cast<unsigned>(exp_.totalBytes()), cfg.hostname);
        outLen_ = static_cast<uint16_t>(w);
        outPos_ = 0;
        st_     = St::SendZip;
        return;
    }

    if (put && (!strcmp(path, "/restore") || !strcmp(path, "/restore.zip") || !strcmp(path, "/backup.zip"))) {
        const char* te = headerValue(hdr_, "transfer-encoding");
        const char* cl = headerValue(hdr_, "content-length");
        if (te || !cl) { reply(411, "Length Required", "send the zip with a Content-Length (curl -T does)\n"); return; }
        char* clEnd = nullptr;
        unsigned long len = strtoul(cl, &clEnd, 10);
        if (clEnd == cl || len == 0) { reply(400, "Bad Request", "empty upload\n"); return; }
        if (len > BBS_ZIP_MAX_BYTES) {
            char body[96];
            snprintf(body, sizeof(body), "zip is %lu bytes, the limit is %u (see SCREENS.md)\n", len,
                     static_cast<unsigned>(BBS_ZIP_MAX_BYTES));
            reply(413, "Payload Too Large", body);
            return;
        }

        imp_.discard();                                    // any leftover staging goes
        char dir[96], file[96];
        snprintf(dir, sizeof(dir), "%s/%s", plat::fsBase(), BBS_BACKUP_STAGING);
        mkdir(dir, 0755);
        uploadPath(file, sizeof(file));
        upload_ = fopen(file, "wb");
        if (!upload_) { reply(507, "Insufficient Storage", "cannot open staging\n"); return; }

        bodyLeft_ = static_cast<uint32_t>(len);
        st_ = St::Body;
        note("*** Upload from %s: receiving %lu KB", clientIp_, (len + 1023) / 1024);

        if (extra) {                                       // body bytes that came with the headers
            size_t take = extra < bodyLeft_ ? extra : bodyLeft_;
            fwrite(hdr_ + headLen, 1, take, upload_);
            bodyLeft_ -= static_cast<uint32_t>(take);
        }
        const char* expect = headerValue(hdr_, "expect");
        if (expect && !extra && !strncmp(expect, "100", 3)) {
            static const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
            memcpy(out_, cont, sizeof(cont) - 1);
            outLen_ = sizeof(cont) - 1;
            outPos_ = 0;
        }
        if (bodyLeft_ == 0) {                              // whole zip arrived with the headers
            fclose(upload_);
            upload_ = nullptr;
            char err[96];
            if (!imp_.open(file, err, sizeof(err))) {
                imp_.discard();
                reply(400, "Bad Request", err);
                return;
            }
            st_ = St::Extract;
        }
        return;
    }

    if (put || get) reply(404, "Not Found", "try GET /backup.zip or PUT /restore\n");
    else            reply(405, "Method Not Allowed", "GET or PUT only\n");
}

void BackupService::reply(int code, const char* reason, const char* body) {
    size_t blen = strlen(body);
    int w = snprintf(reinterpret_cast<char*>(out_), sizeof(out_),
                     "HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
                     code, reason, static_cast<unsigned>(blen));
    if (w < 0) w = 0;
    size_t room = sizeof(out_) - static_cast<size_t>(w);
    if (blen > room) blen = room;
    memcpy(out_ + w, body, blen);
    outLen_ = static_cast<uint16_t>(static_cast<size_t>(w) + blen);
    outPos_ = 0;
    closeAfterOut_ = true;
    deadline_ = plat::millis() + BBS_BACKUP_HEADER_MS;   // a reply that cannot be delivered is dropped
    if (upload_) { fclose(upload_); upload_ = nullptr; }
    st_ = St::Reply;
}

void BackupService::dropClient(const char* why) {
    if (cfd_ < 0) return;
    ::close(cfd_);
    cfd_ = -1;
    if (upload_) { fclose(upload_); upload_ = nullptr; }
    if (st_ == St::Body || st_ == St::Extract || st_ == St::Approve) imp_.discard();
    exp_.abort();
    exp_.dropSnapshot();                             // the users.txt copy taken for a download
    plat::log("backup: client %s done (%s)", clientIp_, why);
    st_ = St::Idle;
    outLen_ = outPos_ = 0;
    closeAfterOut_ = false;
}

// ---------------------------------------------------------------------------
// decide: the sysop's Y/N on a staged upload
// ---------------------------------------------------------------------------
void BackupService::decide(bool accept, const char* why) {
    if (st_ != St::Approve) return;
    char msg[160];
    if (accept) {
        bool ok = imp_.apply(msg, sizeof(msg));
        note("*** %.60s", msg);
        char body[300];
        snprintf(body, sizeof(body), "%s\n%s\n", msg, detail_);
        reply(ok ? 200 : 500, ok ? "OK" : "Internal Server Error", body);
    } else {
        imp_.discard();
        note("*** Upload discarded: %.48s", why);
        snprintf(msg, sizeof(msg), "Upload discarded: %s\n", why);
        reply(403, "Forbidden", msg);
    }
}

bool BackupService::poll(Note& out) {
    if (!noteCount_) return false;
    out = notes_[noteHead_];
    noteHead_ = static_cast<uint8_t>((noteHead_ + 1) % (sizeof(notes_) / sizeof(notes_[0])));
    --noteCount_;
    return true;
}

void BackupService::note(const char* fmt, ...) {
    uint8_t cap = sizeof(notes_) / sizeof(notes_[0]);
    if (noteCount_ == cap) {                               // full: drop the oldest
        noteHead_ = static_cast<uint8_t>((noteHead_ + 1) % cap);
        --noteCount_;
    }
    Note& n = notes_[(noteHead_ + noteCount_) % cap];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(n.text, sizeof(n.text), fmt, ap);
    va_end(ap);
    ++noteCount_;
    plat::log("backup: %s", n.text);
}
