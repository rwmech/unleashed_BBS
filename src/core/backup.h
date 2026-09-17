/*
 * File:        src/core/backup.h
 * Description: The backup window. A button press (while the sysop is on
 *              the sysop node) opens a plain HTTP listener for
 *              backup_window_minutes. It runs inside the BBS select loop:
 *              no extra task, one client at a time, no heap except the
 *              temporary inflate buffers during an upload.
 *
 *   GET  /              short text help
 *   GET  /backup.zip    the archive (see ziparc.h), no confirmation
 *   PUT  /restore       upload a zip (POST works too). It is staged and
 *                       checked, the sysop answers Y/N on the console, and
 *                       the reply tells curl what happened.
 *
 *   Everything the sysop should see is queued as a Note; the BBS delivers
 *   notes and runs the Y/N prompt.
 * Listing:     COMPLETE FILE
 * Libraries:   BSD sockets (lwIP on ESP32)
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

    ziparc::ZipExport exp_;
    ziparc::ZipImport imp_;
    char     summary_[128] = {};
    char     detail_[112] = {};

    Note     notes_[6];
    uint8_t  noteHead_  = 0;
    uint8_t  noteCount_ = 0;
};
