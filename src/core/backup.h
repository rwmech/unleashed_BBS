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
 *               The SD card (1.1.0). BACKUP SD writes the same zip to the
 *                  card, RESTORE SD takes one back through the same checks,
 *                  and the nightly backup is the first of those on a timer.
 *                  They share this class's zip storage with the window, so
 *                  one of them runs at a time (busy()), and each is done a
 *                  step per loop pass (cardStep) so a zip going to or coming
 *                  off the card never holds every caller up. The Bbs draws
 *                  them; this only does the work.
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
#include "../platform/platform.h"
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
    // decide: Y holds the upload until the Bbs releases it (holding, below),
    // then it goes live a file a pass (applyTick) and the reply goes to curl
    // once it is all in; N throws it away.
    void        decide(bool accept, const char* why);

    // takeRestart: a restore put system.cfg or an information page live, so
    // the plugins should start again on it. True once per such restore.
    bool takeRestart() { bool r = restart_; restart_ = false; return r; }

    // applyTick: one more file of an upload the sysop accepted through the
    // window put live. Out of service() since 1.1.0, so that the Bbs can
    // close the screens it replaces first, in the same pass (serviceCard).
    void applyTick();

    // applyingScreens: a restore carrying screens is being put live, from
    // the window or a card job. card: into the card's screens folder
    // (RESTORE SD SCREENS) rather than the board's own. Callers still
    // reading one of those screens have to let go of it first: esp_littlefs
    // refuses to replace or remove a file somebody has open (EBUSY).
    bool applyingScreens(bool& card) const;

    // -- the SD card (1.1.0) ---------------------------------------------------
    //
    // Job: what the card is doing. Write is BACKUP SD or the nightly one,
    // Check is RESTORE SD unpacking and checking, Ask is waiting for the
    // sysop's Y, Hold is a Y waiting for the board to go quiet (1.1.0),
    // Apply is putting it live.
    enum class Job : uint8_t { None, Write, Check, Ask, Apply, Hold };
    Job  job() const { return job_; }

    // busy: the zip storage is somebody's: a window client, an upload the
    // window is still holding or putting live, or a card job.
    bool busy() const {
        return job_ != Job::None || cfd_ >= 0 || st_ == St::Apply || st_ == St::Hold;
    }

    // -- a restore waits for the board to go quiet (1.1.0) -------------------
    //
    // The sysop's Y, at either door, does not put a restore live while
    // anybody else is on: it holds, and the Bbs releases it once they have
    // gone, or when the sysop presses F, or gives up after a while. Rob
    // watched a live board "hang hard" through one; a file a pass took the
    // single stall away, but callers were still on a board changing under
    // them, accounts and all.
    //
    // holding: a restore the sysop said Y to is waiting. restoring: one is
    // waiting or being put live, which is when a new caller gets the busy
    // line rather than a login to a board about to change.
    bool holding()   const { return st_ == St::Hold || job_ == Job::Hold; }
    bool restoring() const { return holding() || st_ == St::Apply || job_ == Job::Apply; }
    // holdTell: the window's client is told it waits, how many for, and
    // that the sysop can apply it now. The reply goes out in chunks from
    // here, so what it finally says follows on the same connection.
    void holdTell(uint8_t callers);
    // holdRelease: put it live now. holdGiveUp: throw it away, as a Y that
    // never came would, and tell the client why.
    void holdRelease();
    void holdGiveUp(const char* why);

    // cardBackup: start writing dir/name, through name.tmp so a card pulled
    // half way leaves nothing a list would offer. needKB and freeKB are set
    // when it would not fit.
    enum class Start : uint8_t { Ok, Busy, Full, Failed };
    Start cardBackup(const char* dir, const char* name, bool screensOnly,
                     uint32_t& needKB, uint32_t& freeKB);

    // cardRestore: open a zip on the card and start checking it, exactly as
    // an upload is checked. screensDir: SCREENS only, where they go. False
    // with the importer's reason in err, and cardFail() saying which kind.
    bool cardRestore(const char* zipPath, bool screensOnly, const char* screensDir,
                     char* err, size_t errLen);
    ziparc::ZipImport::Fail cardFail() { return importer().failed(); }
    uint32_t cardNeedKB() { return importer().needKB(); }    // Fail::NoRoom
    uint32_t cardFreeKB() { return importer().freeKB(); }

    // cardStep: one step of whatever job is running. Returns how many files
    // it finished, for the progress dots.
    uint8_t cardStep();

    // cardAnswer: the sysop's answer while job() is Ask. Y is Hold, until the
    // Bbs releases it.
    void cardAnswer(bool yes);

    // cardDrop: give up the job (the sysop left). A write takes its half
    // file with it and a check its staging; a restore being applied is not
    // stopped half way, and finishes on its own.
    void cardDrop();

    // What the last job did. cardOk: a write that finished whole.
    bool     cardOk()      const { return jobOk_; }
    uint32_t cardBytes()   const { return jobBytes_; }
    uint8_t  cardFiles()   const { return jobFiles_; }
    const ziparc::ImportReport& cardReport() { return importer().report(); }
    const ziparc::ApplyReport&  cardApplied() { return importer().applied(); }

private:
    enum class St : uint8_t { Idle, Headers, Body, Extract, Approve, Apply, SendZip, Reply, Linger, Hold };
    void openUpload(const char* path);
    void finishApply();
    void finishWrite(bool ok);
    // chunk: one chunk of a chunked reply onto out_, after whatever of the
    // last one has not gone yet; last ends the reply and closes after it.
    void chunk(const char* text, bool last);

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
    bool     chunked_   = false;     // the reply went out as chunks (a held restore)

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

    bool     restart_   = false;     // see takeRestart

    // The card job. The zip goes through out_ above, which is free whenever
    // a job can run: a job and a window client are never live together.
    Job      job_      = Job::None;
    FILE*    jobOut_   = nullptr;    // Write: <dir>/<name>.tmp
    char     jobPath_[112] = {};     // Write: the finished name, <dir>/<name>
    bool     jobScreens_ = false;
    bool     jobOk_    = false;
    uint32_t jobBytes_ = 0;
    uint8_t  jobFiles_ = 0;
};

// sdNightly: whether the sd plugin is running with its "nightly" setting on
// (1.1.0). Defined by the plugin, asked by the core, the way sdScreensDir is:
// the core runs the backup without knowing plugins exist. True with no card
// in, deliberately: a nightly backup that could not happen for want of one
// is something the sysop is told about.
bool sdNightly();

// sdCardInfo: the card's figures as the sd plugin keeps them, refreshed at
// most every few seconds (1.1.0). Asking the platform directly is a trip to
// the card's FAT each time on a board whose cache has run out; everything
// that wants to know how full the card is asks here instead. Also defined by
// the plugin, and answers "not mounted" with no card in.
const plat::SdInfo& sdCardInfo();

// tidyCardBackups: remove what a BACKUP SD or nightly zip left half written
// in the card's backup folder (cardbak::partial) when the power went or the
// card was pulled (1.1.0). The sd plugin calls it on a mount it has just
// made, when no backup can be writing to that card.
void tidyCardBackups();
