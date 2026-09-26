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

#include "backup.h"
#include "disk.h"              // fopen and opendir that tell the drive light (1.1.1)
#include "cardnames.h"
#include "sysconfig.h"
#include "guard.h"
#include "space.h"             // the kept free-space figures (1.1.2)
#include "runner.h"            // the unpack and the download's scan, off the loop (1.1.2)
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
#include <new>
#include <sys/stat.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace {

void nonBlocking(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

// The upload lands in staging on userdata (1.1.0), where it is unpacked too:
// see BBS_BACKUP_STAGING in config.h for why it moved off the screens.
void stagingDir(char* out, size_t n) {
    snprintf(out, n, "%s/%s", plat::userBase(), BBS_BACKUP_STAGING);
}

void uploadPath(char* out, size_t n) {
    snprintf(out, n, "%s/%s/upload.zip", plat::userBase(), BBS_BACKUP_STAGING);
}

// kbUp / kbDown: bytes in the board's KB of 1,024, rounded so a size just
// over a limit reads as over it: the size up and the limit down, or a zip
// one byte too big would be "257 KB, the limit is 257 KB".
unsigned kbUp(unsigned long bytes)   { return static_cast<unsigned>((bytes + 1023) / 1024); }
unsigned kbDown(unsigned long bytes) { return static_cast<unsigned>(bytes / 1024); }

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

// ---------------------------------------------------------------------------
// exporter / importer: make one half of the shared zip storage live.
//
// Switching constructs the new half in place over the old one. Neither class
// has a destructor to run, but the half being given up may still hold a FILE*,
// so it is closed first; neither close touches anything on disk, which keeps
// staging and the download snapshot exactly where the state machine left
// them. In practice both are already closed by then, because every request
// ends in dropClient() before the next one is routed.
// ---------------------------------------------------------------------------
ziparc::ZipExport& BackupService::exporter() {
    if (zipUse_ != ZipUse::Export) {
        zip_.imp.close();
        new (&zip_.exp) ziparc::ZipExport();
        zipUse_ = ZipUse::Export;
    }
    return zip_.exp;
}

ziparc::ZipImport& BackupService::importer() {
    if (zipUse_ != ZipUse::Import) {
        zip_.exp.abort();
        new (&zip_.imp) ziparc::ZipImport();
        zipUse_ = ZipUse::Import;
    }
    return zip_.imp;
}

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
    // Said where it is used rather than in a document: the zip carries the
    // Wi-Fi password. The port refuses outside addresses, but that is a
    // check on the source address, and a router that rewrites the source of
    // forwarded traffic gets past it. So the notice asks for the thing that
    // actually protects it, rather than promising a boundary it cannot see.
    note("*** It holds the Wi-Fi password. Never forward this port.");
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
// ===========================================================================
// Off the loop (1.1.2): the restore's unpack, and the download's scan.
//
// Both were a pass each on the loop that could be a second long. A restore
// unpacked one entry a pass, and an entry is up to 64 KB inflated with the
// ROM's tinfl, CRC'd and written to staging; the window's GET read every
// file of the backup whole for its CRC before the 200 (internal/audit-
// 1.1.2-2026-09-26.md, items 6 and 8). Now each is one job on the background
// runner, and the loop only watches for it to finish. The importer and the
// exporter are one union (zip_): the window's client and a card job never
// hold it at once (busy()), and while a job has it the loop does not touch
// it, only the job's phase.
//
// The unpack writes to staging in slices, with a breath between them, so a
// LittleFS erase lands in a different loop pass from the next one. A flash
// erase stops both cores whoever asks for it; on the loop the whole entry's
// erases were one pass.
// ===========================================================================
namespace {
struct ZipJob {
    runner::Job job;
    BackupService* svc = nullptr;
    char hostname[40] = {};          // the scan's MANIFEST
    bool ok = false;                 // the scan: made; the unpack: room to do it
    char err[96] = {};
};
ZipJob g_unpack, g_scan;
}   // namespace

void BackupService::unpackWork(runner::Job&) {
    BackupService& b = *g_unpack.svc;
    g_unpack.ok = b.importer().recheckRoom(g_unpack.err, sizeof(g_unpack.err));
    if (!g_unpack.ok) return;
    while (b.importer().step()) runner::breathe();
}

void BackupService::scanWork(runner::Job&) {
    BackupService& b = *g_scan.svc;
    g_scan.ok = b.exporter().scan(g_scan.hostname, g_scan.err, sizeof(g_scan.err));
}

// unpacked: start the unpack when none is out, and say when it is done. The
// loop's, every pass while one is wanted.
bool BackupService::unpacked() {
    if (runner::idle(g_unpack.job) && !unpackOut_) {
        g_unpack.svc = this;
        g_unpack.err[0] = '\0';
        g_unpack.job.work = unpackWork;
        g_unpack.job.name = "restore unpack";
        unpackOut_ = runner::post(g_unpack.job);
        if (!unpackOut_) {                         // no runner: say so as a refusal
            snprintf(g_unpack.err, sizeof(g_unpack.err), "the board could not start the unpack");
            g_unpack.ok = false;
            unpackFailed_ = true;
            return true;
        }
        return false;
    }
    if (!runner::done(g_unpack.job)) return false;
    runner::collect(g_unpack.job);
    unpackOut_    = false;
    unpackFailed_ = !g_unpack.ok;
    return true;
}

const char* BackupService::unpackWhy() const { return g_unpack.err; }

// zipJobOut: a job on the runner has the zip storage: queued or running,
// or done and not yet collected.
bool BackupService::zipJobOut() const {
    return !runner::idle(g_unpack.job) || !runner::idle(g_scan.job);
}

void BackupService::service(const fd_set& r, const fd_set& w, uint32_t now) {
    // A client that went while a job had the zip storage: tidied now it is
    // the loop's again (1.1.2).
    if (abandon_ && !zipJobOut()) {
        if (runner::done(g_unpack.job)) runner::collect(g_unpack.job);
        if (runner::done(g_scan.job))   runner::collect(g_scan.job);
        unpackOut_ = false;
        if (zipUse_ == ZipUse::Import) zip_.imp.discard();
        else { zip_.exp.abort(); zip_.exp.dropSnapshot(); }
        abandon_ = false;
    }
    if (lfd_ >= 0 && FD_ISSET(lfd_, &r)) acceptClient(now);
    if (cfd_ >= 0 && FD_ISSET(cfd_, &r)) readClient(now);
    if (cfd_ >= 0 && (FD_ISSET(cfd_, &w) || outPos_ < outLen_ || st_ == St::SendZip)) writeClient(now);

    if (st_ == St::Extract && unpacked()) {
        const ziparc::ImportReport& rep = importer().report();
        if (unpackFailed_) {
            // No room once measured on the runner: the zip's fault it is
            // not, and it says so as the check at receipt does.
            char body[128];
            snprintf(body, sizeof(body), "Board full: %.100s\n", unpackWhy());
            note("*** Upload from %s refused: no room on the board", clientIp_);
            importer().discard();
            reply(507, "Insufficient Storage", body);
        } else if (!rep.accepted) {
            char body[160];
            snprintf(body, sizeof(body), "Nothing to apply. %u rejected%s%s\n", rep.rejected,
                     rep.rejected ? ", first: " : "", rep.firstReject);
            note("*** Upload from %s refused: nothing usable", clientIp_);
            importer().discard();
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

    // The download's scan is done: the headers, then the zip (1.1.2).
    if (st_ == St::Scan && runner::done(g_scan.job)) {
        runner::collect(g_scan.job);
        if (!g_scan.ok) {
            reply(500, "Internal Server Error", g_scan.err);
        } else {
            int w = snprintf(reinterpret_cast<char*>(out_), sizeof(out_),
                             "HTTP/1.1 200 OK\r\n"
                             "Content-Type: application/zip\r\n"
                             "Content-Length: %u\r\n"
                             "Content-Disposition: attachment; filename=\"%s-backup.zip\"\r\n"
                             "Connection: close\r\n\r\n",
                             static_cast<unsigned>(exporter().totalBytes()), g_scan.hostname);
            outLen_ = static_cast<uint16_t>(w);
            outPos_ = 0;
            st_     = St::SendZip;
        }
    }

    // An accepted upload goes live a file a pass, in applyTick, which the
    // Bbs calls once it has closed any screen the upload replaces.

    if (cfd_ >= 0 && st_ != St::Approve && st_ != St::Extract && st_ != St::Apply && st_ != St::Hold) {
        if (now - lastIo_ > BBS_BACKUP_IDLE_MS)                  dropClient("idle timeout");
        else if (static_cast<int32_t>(now - deadline_) >= 0)     dropClient("too slow");
    }

    if (lfd_ >= 0 && static_cast<int32_t>(now - closesAt_) >= 0) {
        ::close(lfd_);
        lfd_ = -1;
        note("*** Backup window closed (time up)");
    }
}

// ---------------------------------------------------------------------------
// applyTick: an accepted upload goes live a file a pass. It carries on if
// curl goes away: stopping half way through would leave a board with part of
// one backup and part of what it had.
// ---------------------------------------------------------------------------
void BackupService::applyTick() {
    if (st_ == St::Apply && !importer().applyStep()) finishApply();
}

bool BackupService::applyingScreens(bool& card) const {
    card = false;
    if (zipUse_ != ZipUse::Import) return false;
    bool window = st_ == St::Apply;
    bool onCard = job_ == Job::Apply;
    if ((!window && !onCard) || !zip_.imp.report().hasScreens) return false;
    card = onCard && jobScreens_;
    return true;
}

// ===========================================================================
// Client
// ===========================================================================

// localAddr: the board's one rule for its own network (guard.h localNet,
// 1.1.1), which the shell asks too: RFC 1918, link local, 127.0.0.1, and
// 100.64/10 only when CONFIG network says so. It took all of 127/8 and
// 100.64/10 always until then.
static bool localAddr(uint32_t netOrder) {
    return localNet(netOrder, syscfg::get().cgnatLocal);
}

void BackupService::acceptClient(uint32_t now) {
    for (;;) {
        sockaddr_in a;
        socklen_t al = sizeof(a);
        int fd = accept(lfd_, reinterpret_cast<sockaddr*>(&a), &al);
        if (fd < 0) return;
        a.sin_addr.s_addr = peerAddr(a.sin_addr.s_addr);   // the host's 127.0.0.3
        if (!localAddr(a.sin_addr.s_addr)) {
            // A forwarded port, or one UPnP opened without anybody asking,
            // must not hand the accounts and the Wi-Fi password to the
            // internet. A VPN on a private address still works; Tailscale's
            // 100.64/10 only with CONFIG network's CGNAT row (1.1.1), and a
            // sysop refused from there is told which setting it is.
            static const char no[] = "HTTP/1.1 403 Forbidden\r\nContent-Length: 20\r\nConnection: close\r\n\r\nlocal network only.\n";
            static const char cg[] = "HTTP/1.1 403 Forbidden\r\nContent-Length: 47\r\nConnection: close\r\n\r\nlocal network only; see CONFIG network, CGNAT.\n";
            const bool cgnat = cgnatAddr(a.sin_addr.s_addr);
            if (cgnat) send(fd, cg, sizeof(cg) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
            else       send(fd, no, sizeof(no) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
            ::close(fd);
            char ip[16];
            ipToText(a.sin_addr.s_addr, ip, sizeof(ip));
            if (cgnat) note("*** Backup refused %s: 100.64/10, see CONFIG network CGNAT", ip);
            else       note("*** Backup refused %s: not a local address", ip);
            continue;
        }
        // One client at a time, and none while the zip storage is in use: a
        // restore still going live after its client left, or a card job.
        if (busy()) {
            static const char kBusy[] = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 5\r\nConnection: close\r\n\r\nbusy\n";
            send(fd, kBusy, sizeof(kBusy) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
            ::close(fd);
            continue;
        }
        nonBlocking(fd);
        cfd_    = fd;
        st_     = St::Headers;
        hdrLen_ = 0;
        outLen_ = outPos_ = 0;
        closeAfterOut_ = false;
        chunked_ = false;
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
        importer().discard();
        reply(507, "Insufficient Storage", "storage full while receiving the upload\n");
        return;
    }
    bodyLeft_ -= static_cast<uint32_t>(take);
    if (bodyLeft_ == 0) {
        fclose(upload_);
        upload_ = nullptr;
        char path[96];
        uploadPath(path, sizeof(path));
        openUpload(path);
    }
}

// ---------------------------------------------------------------------------
// openUpload: the whole upload is in staging; read its directory and start
// unpacking it, or say why not. No room on the board to unpack it is its
// own answer, because it is not the zip's fault.
// ---------------------------------------------------------------------------
void BackupService::openUpload(const char* path) {
    char err[96];
    if (importer().open(path, err, sizeof(err))) {
        st_ = St::Extract;
        return;
    }
    bool room = importer().failed() == ziparc::ZipImport::Fail::NoRoom;
    char body[128];
    if (room) snprintf(body, sizeof(body), "Board full: %u KB needed, %u KB free.\n",
                       static_cast<unsigned>(importer().needKB()), static_cast<unsigned>(importer().freeKB()));
    else      snprintf(body, sizeof(body), "rejected: %s\n", err);
    importer().discard();
    note("*** Upload from %s refused: %.60s", clientIp_, room ? "no room on the board" : err);
    if (room) reply(507, "Insufficient Storage", body);
    else      reply(400, "Bad Request", body);
}

void BackupService::writeClient(uint32_t now) {
    // One file opened a pass (1.1.2). The screens are small, so eight rounds
    // of a buffer could open a dozen of them in one pass, and an open on
    // LittleFS is a path walk; the rest go out on the passes after.
    const uint16_t opened0 = st_ == St::SendZip ? exporter().opened() : 0;
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
            if (exporter().opened() != opened0) return;    // the next pass goes on
            size_t n = exporter().produce(out_, sizeof(out_));
            if (n == 0) {
                note("*** Backup downloaded by %s: %u files, %u KB", clientIp_, exporter().entries(),
                     static_cast<unsigned>((exporter().totalBytes() + 1023) / 1024));
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
        // The scan (every file read whole for its CRC, the accounts
        // snapshotted) is the runner's (1.1.2); the 200 goes once it is
        // done, from service(). curl waits for the headers meanwhile.
        (void)exporter();                  // the union switched here, on the loop, not on the runner
        g_scan.svc = this;
        g_scan.err[0] = '\0';
        snprintf(g_scan.hostname, sizeof(g_scan.hostname), "%s", cfg.hostname);
        g_scan.job.work = scanWork;
        g_scan.job.name = "backup scan";
        if (!runner::idle(g_scan.job) || !runner::post(g_scan.job)) {
            reply(503, "Service Unavailable", "the board is busy; try again in a moment\n");
            return;
        }
        st_ = St::Scan;
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
            // The copy's words, the same as RESTORE SD says them (RS-toobig).
            char body[96];
            snprintf(body, sizeof(body), "Too big: %u KB. The limit is %u KB.\n", kbUp(len),
                     kbDown(BBS_ZIP_MAX_BYTES));
            reply(413, "Payload Too Large", body);
            return;
        }

        importer().discard();                              // any leftover staging goes

        // Room for the zip itself, before a byte of it is taken. Unpacking
        // it is measured again once it is here (ZipImport::open), but a zip
        // that will not even land is better refused than half received.
        // The kept figure (core/space.h, 1.1.2): this is the loop, and a
        // fresh one is a walk of the partition. ZipImport::open measures
        // again, fresh, once the zip is here, before a byte is unpacked.
        {
            const space::Fig fig = space::get(plat::PART_USER);
            const uint32_t total = static_cast<uint32_t>(fig.total), used = static_cast<uint32_t>(fig.used);
            if (fig.valid) {
                uint32_t free  = total > used ? total - used : 0;
                uint32_t avail = free > BBS_FS_RESERVE ? free - BBS_FS_RESERVE : 0;
                uint32_t need  = static_cast<uint32_t>((len + BBS_FS_BLOCK - 1) / BBS_FS_BLOCK) * BBS_FS_BLOCK;
                if (need > avail) {
                    char body[96];
                    snprintf(body, sizeof(body), "Board full: %u KB needed, %u KB free.\n",
                             kbUp(need), kbDown(avail));
                    note("*** Upload from %s refused: no room on the board", clientIp_);
                    reply(507, "Insufficient Storage", body);
                    return;
                }
            }
        }

        char dir[96], file[96];
        stagingDir(dir, sizeof(dir));
        mkdir(dir, 0755);
        uploadPath(file, sizeof(file));
        upload_ = disk::open(file, "wb");
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
            openUpload(file);
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
    // Half way through putting an upload live, the socket goes and the
    // restore does not: see service(). finishApply() tells the console. A
    // restore the sysop has said Y to and that is waiting for the board to
    // go quiet (Hold) carries on the same way: the Bbs decides its end.
    if (st_ == St::Apply || st_ == St::Hold) {
        plat::log("backup: client %s done (%s), the restore carries on", clientIp_, why);
        outLen_ = outPos_ = 0;
        closeAfterOut_ = false;
        chunked_ = false;
        return;
    }
    // A job on the runner still has the zip storage (1.1.2): it is tidied
    // when the job is done (service, abandon_), never from under it.
    if (zipJobOut()) {
        abandon_ = true;
        plat::log("backup: client %s done (%s)", clientIp_, why);
        st_ = St::Idle;
        outLen_ = outPos_ = 0;
        closeAfterOut_ = false;
        chunked_ = false;
        return;
    }
    // Tidy the half of the zip storage that is live and never the other
    // (backup.h). No accessor here: an accessor switches, and switching on
    // the way out would build a fresh half only to tidy nothing in it.
    if (zipUse_ == ZipUse::Import) {
        if (st_ == St::Body || st_ == St::Extract || st_ == St::Approve) zip_.imp.discard();
    } else {
        zip_.exp.abort();
        zip_.exp.dropSnapshot();                     // the users.txt copy taken for a download
    }
    plat::log("backup: client %s done (%s)", clientIp_, why);
    st_ = St::Idle;
    outLen_ = outPos_ = 0;
    closeAfterOut_ = false;
    chunked_ = false;
}

// ---------------------------------------------------------------------------
// decide: the sysop's Y/N on a staged upload
// ---------------------------------------------------------------------------
void BackupService::decide(bool accept, const char* why) {
    if (st_ != St::Approve) return;
    if (accept) {
        // Held until the Bbs releases it (holdRelease), which it does at once
        // on a quiet board. From there a file a pass (applyTick), and curl
        // hears once it is done. Putting sixty-odd files live in this one
        // call held every caller up for as long as it took, and since 1.1.0
        // each screen is a copy from the user partition rather than a rename.
        st_     = St::Hold;
        lastIo_ = plat::millis();
        return;
    }
    char msg[160];
    importer().discard();
    note("*** Upload discarded: %.48s", why);
    snprintf(msg, sizeof(msg), "Upload discarded: %s\n", why);
    reply(403, "Forbidden", msg);
}

// ---------------------------------------------------------------------------
// finishApply: an accepted upload is all in. Tell the console and curl.
// ---------------------------------------------------------------------------
void BackupService::finishApply() {
    char msg[192];
    importer().applyMessage(msg, sizeof(msg));
    bool ok = importer().applyOk();
    const ziparc::ImportReport& rep = importer().report();
    if (rep.hasCfg || rep.pages) restart_ = true;
    note("*** %.60s", msg);
    // The same news as the end of msg, which the note above cuts off: a
    // restore cannot bring a sysop password back, so a board on the
    // published default is still on it (1.0.2).
    if (rep.hasCfg && syscfg::get().sysopDefault)
        note("*** Sysop password is the published default: local only");
    // A co-sysop line naming the published password was left out, so that
    // level is off now (1.1.0). Said here and to curl, not only in the log,
    // because it changes who can get onto the board.
    char coLine[80] = "";
    uint8_t co = importer().applied().coOff;
    if (co) {
        snprintf(coLine, sizeof(coLine), "Co-sysop %s off: the published password is never set.",
                 co == 3 ? "1 and 2" : co == 1 ? "1" : "2");
        note("*** %s", coLine);
    }
    if (cfd_ < 0) {                                  // curl went away: nobody to tell
        st_ = St::Idle;
        return;
    }
    char body[384];
    snprintf(body, sizeof(body), "%.180s\n%.110s\n%s%s", msg, detail_, coLine, coLine[0] ? "\n" : "");
    // A restore that waited has told curl so already, as the first chunk
    // of a 200 (holdTell): the rest of the reply follows it. The status is
    // spent by then, so a restore with errors says so in the words.
    if (chunked_) { chunk(body, true); return; }
    reply(ok ? 200 : 500, ok ? "OK" : "Internal Server Error", body);
}

// ---------------------------------------------------------------------------
// chunk: HTTP/1.1 chunked transfer, one chunk onto out_. Anything of the
// last chunk still unsent is kept in front of it.
// ---------------------------------------------------------------------------
void BackupService::chunk(const char* text, bool last) {
    if (outPos_ && outPos_ < outLen_) {
        memmove(out_, out_ + outPos_, outLen_ - outPos_);
        outLen_ = static_cast<uint16_t>(outLen_ - outPos_);
        outPos_ = 0;
    } else if (outPos_ >= outLen_) {
        outLen_ = outPos_ = 0;
    }
    size_t room = sizeof(out_) - outLen_;
    size_t n    = strlen(text);
    if (n + 16 > room) n = room > 16 ? room - 16 : 0;       // size line, CRLFs, the end
    int w = n ? snprintf(reinterpret_cast<char*>(out_) + outLen_, room, "%x\r\n", static_cast<unsigned>(n)) : 0;
    if (w > 0) {
        outLen_ = static_cast<uint16_t>(outLen_ + w);
        memcpy(out_ + outLen_, text, n);
        outLen_ = static_cast<uint16_t>(outLen_ + n);
        memcpy(out_ + outLen_, "\r\n", 2);
        outLen_ = static_cast<uint16_t>(outLen_ + 2);
    }
    if (last) {
        // The clamp above keeps room for this, and the check says so rather
        // than trusting every message to stay short. Without room the reply
        // still ends: the connection closes after it (Connection: close).
        if (outLen_ + 5u <= sizeof(out_)) {
            memcpy(out_ + outLen_, "0\r\n\r\n", 5);
            outLen_ = static_cast<uint16_t>(outLen_ + 5);
        }
        closeAfterOut_ = true;
        deadline_ = plat::millis() + BBS_BACKUP_HEADER_MS;   // a reply that cannot be delivered is dropped
        st_ = St::Reply;
    }
}

// ---------------------------------------------------------------------------
// The hold (1.1.0): see holding() in backup.h.
// ---------------------------------------------------------------------------
void BackupService::holdTell(uint8_t callers) {
    if (st_ != St::Hold || cfd_ < 0 || chunked_) return;
    static const char kHead[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                                "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
    outLen_ = outPos_ = 0;
    memcpy(out_, kHead, sizeof(kHead) - 1);
    outLen_  = sizeof(kHead) - 1;
    chunked_ = true;
    char line[128];
    snprintf(line, sizeof(line), "Waiting for %u caller%s to leave before it is applied. "
             "The sysop's F applies it now.\n", callers, callers == 1 ? "" : "s");
    chunk(line, false);
    lastIo_ = plat::millis();
}

void BackupService::holdRelease() {
    if (st_ == St::Hold) {
        st_       = St::Apply;
        deadline_ = plat::millis() + BBS_BACKUP_TRANSFER_MS;
        lastIo_   = plat::millis();
    }
    if (job_ == Job::Hold) job_ = Job::Apply;
}

void BackupService::holdGiveUp(const char* why) {
    if (job_ == Job::Hold) {
        importer().discard();
        job_ = Job::None;
        return;
    }
    if (st_ != St::Hold) return;
    importer().discard();
    note("*** Upload discarded: %.48s", why);
    if (cfd_ < 0) { st_ = St::Idle; return; }
    char msg[160];
    snprintf(msg, sizeof(msg), "Upload discarded: %s\n", why);
    if (chunked_) chunk(msg, true);
    else          reply(403, "Forbidden", msg);
}

// ===========================================================================
// The SD card (1.1.0)
// ===========================================================================

BackupService::Start BackupService::cardBackup(const char* dir, const char* name, bool screensOnly,
                                               uint32_t& needKB, uint32_t& freeKB) {
    needKB = freeKB = 0;
    if (busy()) return Start::Busy;
    jobOk_      = false;
    jobBytes_   = 0;
    jobFiles_   = 0;
    jobScreens_ = screensOnly;

    uint32_t estimate = 0;
    exporter().beginFile(screensOnly, estimate);
    plat::SdInfo card = sdCardInfo();
    needKB = (estimate + 1023) / 1024;
    // The card's kept free space (core/space.h, 1.1.2). Not measured yet is
    // not "full": the writer stops and says so if the card fills, and the
    // .tmp it leaves is removed.
    const space::Fig cf = space::get(plat::PART_CARD);
    freeKB = cf.valid ? static_cast<uint32_t>((cf.total > cf.used ? cf.total - cf.used : 0) / 1024ull) : needKB;
    if (!card.mounted || needKB > freeKB) {
        exporter().abort();
        return Start::Full;
    }
    snprintf(jobPath_, sizeof(jobPath_), "%s/%s", dir, name);
    char tmp[124];
    snprintf(tmp, sizeof(tmp), "%.112s.tmp", jobPath_);
    jobOut_ = disk::open(tmp, "wb");
    if (!jobOut_) {
        exporter().abort();
        return Start::Failed;
    }
    job_ = Job::Write;
    return Start::Ok;
}

// finishWrite: the zip is complete, or will not be. Only a whole one keeps
// its name; anything else is removed, so "Nothing was saved" is true.
void BackupService::finishWrite(bool ok) {
    jobBytes_ = exporter().written();
    jobFiles_ = jobScreens_ ? exporter().screens() : exporter().files();
    exporter().abort();
    bool closed = fclose(jobOut_) == 0;              // FAT says "full" here as often as not
    jobOut_ = nullptr;
    char tmp[124];
    snprintf(tmp, sizeof(tmp), "%.112s.tmp", jobPath_);
    ok = ok && closed && rename(tmp, jobPath_) == 0;
    if (!ok) remove(tmp);
    jobOk_ = ok;
    job_   = Job::None;
}

bool BackupService::cardRestore(const char* zipPath, bool screensOnly, const char* screensDir,
                                char* err, size_t errLen) {
    if (busy()) { snprintf(err, errLen, "busy"); return false; }
    jobScreens_ = screensOnly;
    jobOk_      = false;
    // A screens restore unpacks on the card, next to where the screens go,
    // so putting them live is a rename there. A full one unpacks on userdata
    // like an upload does, with the files it replaces.
    char stage[96] = "";
    if (screensOnly) snprintf(stage, sizeof(stage), "%s/%s/%s", plat::sdBase(), cardbak::kDir, BBS_BACKUP_STAGING);
    ziparc::ZipImport::Mode mode = screensOnly ? ziparc::ZipImport::Mode::Screens
                                               : ziparc::ZipImport::Mode::Full;
    if (!importer().open(zipPath, err, errLen, mode, stage, screensDir)) {
        importer().discard();
        return false;
    }
    job_ = Job::Check;
    return true;
}

uint8_t BackupService::cardStep() {
    uint8_t dots = 0;
    switch (job_) {
        case Job::Write: {
            // Up to BBS_CARD_STEP_BYTES of zip a pass, and a dot for every
            // file that ends inside it.
            uint32_t start = exporter().written();
            for (;;) {
                uint32_t used = exporter().written() - start;
                if (used >= BBS_CARD_STEP_BYTES) break;
                ziparc::ZipExport::Wrote w =
                    exporter().writeFile(jobOut_, out_, sizeof(out_), BBS_CARD_STEP_BYTES - used);
                // One file a pass (1.1.2), the way the window's download
                // goes: the next entry's open waits for the next pass.
                if (w == ziparc::ZipExport::Wrote::Entry) { ++dots; break; }
                if (w == ziparc::ZipExport::Wrote::More) break;
                finishWrite(w == ziparc::ZipExport::Wrote::Done);
                break;
            }
            break;
        }
        case Job::Check: {
            // Unpacked and checked on the runner, the way an upload is
            // (1.1.2), a dot for every file it has finished so far.
            const uint8_t was = dotsSeen_;
            const bool fin = unpacked();
            const uint8_t now = importer().stepped();
            if (now > was) { dots = static_cast<uint8_t>(now - was); dotsSeen_ = now; }
            if (!fin) break;
            dotsSeen_ = 0;
            if (unpackFailed_ || !importer().report().accepted) {
                if (unpackFailed_) importer().noteRefusal(unpackWhy());
                importer().discard();
                job_ = Job::None;
            } else {
                job_ = Job::Ask;
            }
            break;
        }
        case Job::Apply:
            if (importer().applyStep()) { dots = 1; break; }
            if (!jobScreens_ && (importer().report().hasCfg || importer().report().pages)) restart_ = true;
            jobOk_ = importer().applyOk();
            job_   = Job::None;
            break;
        default:
            break;
    }
    return dots;
}

void BackupService::cardAnswer(bool yes) {
    if (job_ != Job::Ask) return;
    if (yes) { job_ = Job::Hold; return; }       // the Bbs releases it (holdRelease)
    importer().discard();
    job_ = Job::None;
}

void BackupService::cardDrop() {
    switch (job_) {
        case Job::Write:
            finishWrite(false);
            break;
        case Job::Check:
            if (zipJobOut()) { abandon_ = true; job_ = Job::None; break; }   // tidied after it (1.1.2)
            importer().discard();
            job_ = Job::None;
            break;
        case Job::Ask:
        case Job::Hold:
            importer().discard();
            job_ = Job::None;
            break;
        default:                          // Apply finishes; None has nothing to drop
            break;
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
