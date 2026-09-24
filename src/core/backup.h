/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/backup.h
 * Module:       Core / backup window
 *
 * Purpose:      The backup window. A button press (while the sysop is on
 *                  the sysop node) opens a plain HTTP listener for
 *                  backup_window_minutes. It runs inside the BBS select loop:
 *                  no extra task, one client at a time, no heap except the
 *                  temporary inflate buffers during an upload.
 *
 *                  GET  /              short text help
 *                  GET  /backup.zip    the archive (see ziparc.h), no confirmation
 *                  PUT  /restore       upload a zip (POST works too). It is staged and
 *                           checked, the sysop answers Y/N on the console, and
 *                           the reply tells curl what happened.
 *
 *                  Everything the sysop should see is queued as a Note; the BBS delivers
 *                  notes and runs the Y/N prompt.
 *
 * Libraries:    BSD sockets (lwIP on ESP32)
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

#pragma once
#include <cstdint>
#include <cstddef>
#include <sys/select.h>
#include "../config.h"
#include "ziparc.h"

class BackupService {
public:
    struct Note { char text[BBS_LINE_MAX + 1]; };

    bool     isOpen() const { return lfd_ >= 0; }
    uint32_t closesAt() const { return closesAt_; }

    // open: start listening for minutes. localIp is only used in the note.
    bool open(uint32_t now, const char* localIp);

    // close: stop listening. An upload waiting for approval is discarded.
    void close(const char* why);

    void addFds(fd_set& r, fd_set& w, int& maxfd) const;
    void service(const fd_set& r, const fd_set& w, uint32_t now);

    // poll: next note for the sysop console
    bool poll(Note& out);

    // approval: an upload is staged and waiting for the sysop
    bool        awaitingApproval() const { return st_ == St::Approve; }
    const char* approvalSummary() const { return summary_; }
    const char* approvalDetail() const  { return detail_; }
    void        decide(bool accept, const char* why);

private:
    enum class St : uint8_t { Idle, Headers, Body, Extract, Approve, SendZip, Reply, Linger };

    void acceptClient(uint32_t now);
    void readClient(uint32_t now);
    void writeClient(uint32_t now);
    void route(uint32_t now);
    void reply(int code, const char* reason, const char* body);
    void dropClient(const char* why);
    void note(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    int      lfd_       = -1;
    int      cfd_       = -1;
    St       st_        = St::Idle;
    uint32_t closesAt_  = 0;
    uint32_t lastIo_    = 0;
    uint32_t deadline_  = 0;     // hard stop for the current phase (slowloris guard)
    uint32_t approveBy_ = 0;
    char     clientIp_[16] = {};

    char     hdr_[1536] = {};
    uint16_t hdrLen_    = 0;
    uint32_t bodyLeft_  = 0;
    FILE*    upload_    = nullptr;

    uint8_t  out_[1024] = {};
    uint16_t outLen_    = 0;
    uint16_t outPos_    = 0;
    bool     closeAfterOut_ = false;

    // The zip going out and the zip coming in share one block of storage,
    // which is 3,872 bytes of static DRAM back. They are never live
    // together, and the state machine is what guarantees it rather than a
    // convention: one client at a time, a request is only routed from
    // St::Headers, the download lives from route() through SendZip, and the
    // upload from Body through Extract and Approve to decide(). Each of
    // those ends in dropClient() before the next request can be routed.
    //
    // exporter() and importer() make their half the live one and are the
    // only way in, except in dropClient(), which must never switch: it
    // tidies whichever half is live and nothing else, because calling the
    // other half's abort() would run it over bytes that are the wrong type.
    enum class ZipUse : uint8_t { Export, Import };
    union ZipArea {
        ZipArea() : exp() {}
        ~ZipArea() {}
        ziparc::ZipExport exp;
        ziparc::ZipImport imp;
    };
    ziparc::ZipExport& exporter();
    ziparc::ZipImport& importer();

    ZipArea  zip_;
    ZipUse   zipUse_ = ZipUse::Export;
    char     summary_[128] = {};
    char     detail_[112] = {};

    Note     notes_[6];
    uint8_t  noteHead_  = 0;
    uint8_t  noteCount_ = 0;
};
