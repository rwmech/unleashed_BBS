/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/camera_rules.h
 * Module:       Plugins / camera (BBS_HAS_CAMERA boards), the rules
 *
 * Purpose:      Everything the camera decides that is not the camera: what
 *               a photo is called, which names are the camera's own and so
 *               may ever be removed, how many photos a caller may take and
 *               when the next is allowed, which photos retention takes and
 *               in what order, when a timed shot is due, whether a caller
 *               is offered the download, and the JPEG comment every photo
 *               carries. Pure and header-only, so host/test_camera.cpp
 *               drives every rule at its boundaries with no card, no sensor
 *               and no board.
 *
 *               Names (the plan's "Limits, notices and naming"):
 *
 *                 SNAP-20260924-171204.JPG              date (the default)
 *                 SNAP-20260924-171204-quantumrob.JPG   date + handle
 *                 quantumrob/SNAP-20260924-171204.JPG   by handle
 *                 timelapse/TL-20260924-171200.JPG      a system snap
 *
 *               Only a name of exactly that shape is ever counted or
 *               removed. A sysop's own garden.jpg in the folder, or a file
 *               called SNAP-holiday.JPG, is neither. The timestamp in the
 *               name, not the file's mtime, is what "oldest" means: FAT
 *               keeps a two-second mtime from a clock that may have been
 *               wrong, and a laptop copying the card rewrites it.
 *
 * Interfaces:   safeHandle, stampOf, callerName, systemName, nameKey,
 *               Window/check/record, Item/Policy/choose, tlDue, offerFor,
 *               jpegWhole, ComSink
 *
 * Libraries:    none
 * Targets:      ESP32 and ESP32-S3 camera boards (ESP-IDF 5.3.1) and the
 *               Linux host build
 * See also:     internal/PLAN-freenove-cam.md, src/plugins/camera.cpp
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

#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace camrules {

// ---------------------------------------------------------------------------
// Limits (Rob): ten an hour and twenty a day for each caller, rolling, not
// by the clock's hour or day, so "the next one is allowed at 17:42" is the
// oldest snap in the window plus the window.
// ---------------------------------------------------------------------------
constexpr uint8_t  kPerHour = 10;
constexpr uint8_t  kPerDay  = 20;
constexpr uint32_t kHour    = 3600;
constexpr uint32_t kDay     = 86400;

// The shortest timed interval a sysop may set: the camera comes up for every
// shot, and a bring-up and a capture take a second or two.
constexpr uint32_t kTlMin   = 10;
constexpr uint32_t kTlMax   = 86400;

// The folder of the Photos area on the card, and its system folders.
constexpr char kPhotosDir[]  = "photos";
constexpr char kTlFolder[]   = "timelapse";
constexpr char kTlPrefix[]   = "TL";
constexpr char kSnapPrefix[] = "SNAP";

// The temporary name a photo is written under: a leading dot keeps it out
// of every listing, and at start this exact name is removed.
constexpr char kTmpName[] = ".snap.tmp";

// "Name snaps" in CONFIG camera. A word's place is its number.
enum Scheme : uint8_t { NAME_DATE = 0, NAME_HANDLE = 1, NAME_FOLDER = 2 };
constexpr char kSchemes[] = "date|date+handle|by handle";

// isSystemFolder: a folder of Photos the board's own shots go in: the
// timelapse's, and a motion sensor's. Not case sensitive, as FAT is not.
constexpr char kMotionFolder[] = "motion";
inline bool isSystemFolder(const char* name) {
    auto ieq = [](const char* a, const char* b) {
        for (; *a && *b; ++a, ++b)
            if ((*a | 0x20) != (*b | 0x20)) return false;
        return *a == *b;
    };
    return ieq(name, kTlFolder) || ieq(name, kMotionFolder);
}

// ---------------------------------------------------------------------------
// safeHandle: a handle as a FAT name, for a file name or a folder. The
// characters FAT refuses and control characters are dropped, spaces and
// dots are trimmed from both ends (FAT drops a trailing dot or space on its
// own, and two handles differing only by one would share a folder), and the
// case is kept. A guest's gets "guest-" in front, so a guest can never be
// mistaken for the account that owns a folder. Nothing left is "caller".
// ---------------------------------------------------------------------------
// 19, so the longest name the camera writes, a guest's by date and handle,
// is 44 characters: inside the 48 the file areas carry a name in.
constexpr size_t kHandleMax = 19;

inline size_t safeHandle(const char* in, bool guest, char* out, size_t n) {
    if (!out || !n) return 0;
    char body[kHandleMax + 1];
    size_t k = 0;
    for (const char* p = in ? in : ""; *p && k < kHandleMax; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c < 0x20 || c == 0x7F || c >= 0x80) continue;
        if (strchr("\\/:*?\"<>|", static_cast<int>(c))) continue;
        body[k++] = static_cast<char>(c);
    }
    body[k] = '\0';
    size_t a = 0;
    while (body[a] == ' ' || body[a] == '.') ++a;
    while (k > a && (body[k - 1] == ' ' || body[k - 1] == '.')) body[--k] = '\0';
    const char* b = body[a] ? body + a : "caller";
    const bool sys = !guest && isSystemFolder(b);
    int w = guest ? snprintf(out, n, "guest-%.*s", static_cast<int>(kHandleMax - 6), b)
          : sys   ? snprintf(out, n, "user-%.*s", static_cast<int>(kHandleMax - 5), b)
                  : snprintf(out, n, "%s", b);
    return w < 0 ? 0 : (static_cast<size_t>(w) < n ? static_cast<size_t>(w) : n - 1);
}

// stampOf: YYYYMMDD-HHMMSS, 15 characters, into room for 16.
inline void stampOf(const struct tm& t, char out[16]) {
    snprintf(out, 16, "%04u%02u%02u-%02u%02u%02u",
             static_cast<unsigned>(t.tm_year + 1900) % 10000u, static_cast<unsigned>(t.tm_mon + 1) % 100u,
             static_cast<unsigned>(t.tm_mday) % 100u, static_cast<unsigned>(t.tm_hour) % 100u,
             static_cast<unsigned>(t.tm_min) % 100u, static_cast<unsigned>(t.tm_sec) % 100u);
}

// callerName: the path of a caller's photo under the Photos folder.
inline bool callerName(uint8_t scheme, const struct tm& t, const char* handle, bool guest,
                       char* out, size_t n) {
    char st[16], h[kHandleMax + 8];
    stampOf(t, st);
    safeHandle(handle, guest, h, sizeof(h));
    int w;
    if (scheme == NAME_HANDLE)      w = snprintf(out, n, "%s-%s-%s.JPG", kSnapPrefix, st, h);
    else if (scheme == NAME_FOLDER) w = snprintf(out, n, "%s/%s-%s.JPG", h, kSnapPrefix, st);
    else                            w = snprintf(out, n, "%s-%s.JPG", kSnapPrefix, st);
    return w > 0 && static_cast<size_t>(w) < n;
}

// systemName: a system snap (the timelapse, a motion trigger), always
// <folder>/<PREFIX>-stamp.JPG whatever "Name snaps" says.
inline bool systemName(const char* folder, const char* prefix, const struct tm& t,
                       char* out, size_t n) {
    char st[16];
    stampOf(t, st);
    int w = snprintf(out, n, "%s/%s-%s.JPG", folder, prefix, st);
    return w > 0 && static_cast<size_t>(w) < n;
}

// keyOf: a time as the sortable number its name carries, YYYYMMDDHHMMSS.
inline uint64_t keyOf(const struct tm& t) {
    return (((((static_cast<uint64_t>(t.tm_year + 1900) * 100 + static_cast<uint64_t>(t.tm_mon + 1)) * 100 +
               static_cast<uint64_t>(t.tm_mday)) * 100 + static_cast<uint64_t>(t.tm_hour)) * 100 +
             static_cast<uint64_t>(t.tm_min)) * 100 + static_cast<uint64_t>(t.tm_sec));
}

// nameKey: whether a file name (no folder) is one the camera wrote with this
// prefix, and if so its time as keyOf gives it. Exactly PREFIX-YYYYMMDD-
// HHMMSS.JPG, or with -<handle> before .JPG when handles is set; the date
// and time must be a real date and time. Anything else is somebody's own
// file and false.
inline bool nameKey(const char* name, const char* prefix, bool handles, uint64_t& key) {
    size_t pl = strlen(prefix);
    if (strncmp(name, prefix, pl) || name[pl] != '-') return false;
    const char* d = name + pl + 1;
    for (int i = 0; i < 15; ++i) {
        if (i == 8) { if (d[i] != '-') return false; continue; }
        if (d[i] < '0' || d[i] > '9') return false;
    }
    auto num = [&](int at, int len) {
        int v = 0;
        for (int i = 0; i < len; ++i) v = v * 10 + (d[at + i] - '0');
        return v;
    };
    int y = num(0, 4), mo = num(4, 2), dd = num(6, 2), h = num(9, 2), mi = num(11, 2), s = num(13, 2);
    if (y < 1970 || mo < 1 || mo > 12 || dd < 1 || dd > 31 || h > 23 || mi > 59 || s > 59) return false;
    const char* rest = d + 15;
    if (*rest == '-') {
        if (!handles) return false;
        const char* dot = strrchr(rest, '.');
        if (!dot || dot == rest + 1) return false;
        for (const char* p = rest + 1; p < dot; ++p)
            if (*p == '/' || *p == '\\') return false;
        rest = dot;
    }
    if (strcmp(rest, ".JPG") && strcmp(rest, ".jpg")) return false;
    key = static_cast<uint64_t>(y) * 10000000000ull + static_cast<uint64_t>(mo) * 100000000ull +
          static_cast<uint64_t>(dd) * 1000000ull + static_cast<uint64_t>(h) * 10000ull +
          static_cast<uint64_t>(mi) * 100ull + static_cast<uint64_t>(s);
    return true;
}

// ---------------------------------------------------------------------------
// Window: one caller's snaps in the last day, oldest first, as epoch
// seconds. Twenty is the day's limit, so twenty is all a window holds.
// ---------------------------------------------------------------------------
struct Window {
    uint32_t at[kPerDay] = {};
    uint8_t  n = 0;
};

// age: drop what is older than a day.
inline void age(Window& w, uint32_t now) {
    uint8_t keep = 0;
    for (uint8_t i = 0; i < w.n; ++i)
        if (now - w.at[i] < kDay) w.at[keep++] = w.at[i];
    w.n = keep;
}

struct Verdict {
    bool     ok     = true;
    uint8_t  hour   = 0;       // snaps in the last hour, before this one
    uint8_t  day    = 0;       // and in the last day
    uint32_t nextAt = 0;       // when refused: the first moment one is allowed
    bool     byDay  = false;   // refused by the day's limit (else the hour's)
};

// check: may one more be taken now? When not, nextAt is when: the snap that
// has to leave the window for the count to drop below the limit, plus the
// window. The day's limit wins when both are hit, since it is the later.
inline Verdict check(const Window& w0, uint32_t now) {
    Window w = w0;
    age(w, now);
    Verdict v;
    uint8_t firstHour = w.n;
    for (uint8_t i = 0; i < w.n; ++i) {
        if (now - w.at[i] < kHour) { if (firstHour == w.n) firstHour = i; ++v.hour; }
    }
    v.day = w.n;
    if (v.day >= kPerDay) {
        v.ok = false;
        v.byDay = true;
        v.nextAt = w.at[w.n - kPerDay] + kDay;
    }
    if (v.hour >= kPerHour) {
        uint32_t at = w.at[firstHour + (v.hour - kPerHour)] + kHour;
        if (v.ok || at > v.nextAt) { v.nextAt = at; v.byDay = false; }
        v.ok = false;
    }
    return v;
}

// record: one taken now.
inline void record(Window& w, uint32_t now) {
    age(w, now);
    if (w.n == kPerDay) {                       // cannot happen after a check; drop the oldest
        memmove(w.at, w.at + 1, sizeof(w.at[0]) * (kPerDay - 1));
        --w.n;
    }
    w.at[w.n++] = now;
}

// ---------------------------------------------------------------------------
// Retention. Every photo the camera wrote is an Item in a group: group 0 is
// the callers' snaps (Photos and its handle folders together), each system
// folder is a group of its own. A group's Policy removes what is older than
// its days and what is past its count, oldest first; 0 is no limit. Then the
// card's floor: while the free space would be under it, the oldest photos
// go, the system groups' before the callers', since a timed shot is the
// cheaper to lose. choose() marks; the caller removes. It returns whether
// the floor is met.
// ---------------------------------------------------------------------------
struct Item {
    uint64_t key   = 0;      // nameKey
    uint32_t bytes = 0;
    uint8_t  group = 0;      // 0 the callers', 1+ a system folder
    bool     del   = false;
};

struct Policy {
    uint16_t days  = 0;      // keep for, 0 = forever
    uint32_t count = 0;      // keep at most, 0 = no limit
};

// cutoffKey: the key a photo taken `days` before the local time t has.
// Anything with a smaller key is older than that.
inline uint64_t cutoffKey(time_t now, uint16_t days) {
    time_t then = now - static_cast<time_t>(days) * static_cast<time_t>(kDay);
    struct tm t;
    localtime_r(&then, &t);
    return keyOf(t);
}

inline void sortByKey(Item* it, size_t n) {       // insertion runs are short; n is bounded by the scan
    for (size_t i = 1; i < n; ++i) {
        Item v = it[i];
        size_t j = i;
        while (j && it[j - 1].key > v.key) { it[j] = it[j - 1]; --j; }
        it[j] = v;
    }
}

// choose: items sorted oldest first (sortByKey). pol[g] for each group g <
// groups; cutoff[g] is cutoffKey for its days, ignored when days is 0.
inline bool choose(Item* it, size_t n, const Policy* pol, const uint64_t* cutoff, uint8_t groups,
                   uint64_t freeBytes, uint64_t floorBytes) {
    for (uint8_t g = 0; g < groups; ++g) {
        uint32_t have = 0;
        for (size_t i = 0; i < n; ++i) if (it[i].group == g) ++have;
        for (size_t i = 0; i < n; ++i) {
            if (it[i].group != g || it[i].del) continue;
            bool old  = pol[g].days && it[i].key < cutoff[g];
            bool over = pol[g].count && have > pol[g].count;
            if (old || over) { it[i].del = true; --have; }
        }
    }
    uint64_t freed = 0, left = 0;
    for (size_t i = 0; i < n; ++i) (it[i].del ? freed : left) += it[i].bytes;
    if (freeBytes + freed >= floorBytes) return true;
    // When every photo there is would not make the room (the card filled by
    // the file areas or the backups, or a floor bigger than the card), none
    // is taken for it: emptying Photos would not help, and the snap is
    // refused instead.
    if (freeBytes + freed + left < floorBytes) return false;
    for (int pass = 0; pass < 2; ++pass) {           // the system groups first, then the callers'
        for (size_t i = 0; i < n; ++i) {
            if (it[i].del || (pass == 0) != (it[i].group != 0)) continue;
            it[i].del = true;
            freed += it[i].bytes;
            if (freeBytes + freed >= floorBytes) return true;
        }
    }
    return false;
}

// floorBytes: the card's floor. A figure in MB when the sysop set one, else
// a tenth of the card or 512 MB, whichever is smaller.
inline uint64_t floorBytes(uint64_t cardBytes, int32_t setMb) {
    if (setMb >= 0) return static_cast<uint64_t>(setMb) * 1024u * 1024u;
    uint64_t tenth = cardBytes / 10u;
    uint64_t cap   = 512ull * 1024u * 1024u;
    return tenth < cap ? tenth : cap;
}

// ---------------------------------------------------------------------------
// tlDue: whether a timed shot is due. The day is cut into slots of `every`
// seconds of local time, so a shot every hour falls on the hour and a shot
// every 86,400 at midnight, and a shot is due when the slot changes. The
// first look only notes the slot: a board that has just started, or a sysop
// who has just set the interval, does not get a shot that instant. every 0
// is off.
// ---------------------------------------------------------------------------
inline bool tlDue(uint32_t localSecs, uint32_t every, uint32_t& lastSlot, bool& primed) {
    if (!every) { primed = false; return false; }
    uint32_t slot = localSecs / every;
    if (!primed) { primed = true; lastSlot = slot; return false; }
    if (slot == lastSlot) return false;
    lastSlot = slot;
    return true;
}

// ---------------------------------------------------------------------------
// offerFor: after a caller's snap, "Download it now? (y/N)" or why not.
// Offered only to a caller still waiting at the snap, allowed to download
// from Photos (the Photos area's own level, not only the snap level), with
// the transfer engine free.
// ---------------------------------------------------------------------------
enum class Offer : uint8_t { Ask, NotAllowed, Gone, Busy, Failed };

inline Offer offerFor(bool saved, bool stillThere, bool mayDownload, bool transferBusy) {
    if (!stillThere)   return Offer::Gone;
    if (!saved)        return Offer::Failed;
    if (!mayDownload)  return Offer::NotAllowed;
    if (transferBusy)  return Offer::Busy;
    return Offer::Ask;
}

// ---------------------------------------------------------------------------
// jpegWhole: a sensor's JPEG starts FF D8 and ends FF D9. A frame caught
// while a flash write had the cache off can come out cut short, so a frame
// that fails this is taken again rather than saved. The OV2640 pads its
// buffer after the end marker, so the marker is looked for near the end and
// the length trimmed to it.
// ---------------------------------------------------------------------------
inline bool jpegWhole(const uint8_t* p, size_t& n) {
    if (!p || n < 4 || p[0] != 0xFF || p[1] != 0xD8) return false;
    size_t stop = n > 1024 ? n - 1024 : 1;
    for (size_t i = n - 1; i >= stop && i > 1; --i) {
        if (p[i - 1] == 0xFF && p[i] == 0xD9) { n = i + 1; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// ComSink: every photo carries who and when in a JPEG COM segment, written
// straight after the SOI marker as the bytes go by, so no pixel is touched
// and nothing is copied. A viewer's "comment" or exiftool's Comment shows
// it. Text is cut at kComMax. put() returns false when the output does.
// ---------------------------------------------------------------------------
constexpr size_t kComMax = 250;

class ComSink {
public:
    using Out = bool (*)(void* ctx, const uint8_t* p, size_t n);
    ComSink(Out out, void* ctx, const char* text) : out_(out), ctx_(ctx) {
        size_t len = text ? strlen(text) : 0;
        if (len > kComMax) len = kComMax;
        com_[0] = 0xFF;
        com_[1] = 0xFE;
        com_[2] = static_cast<uint8_t>((len + 2) >> 8);
        com_[3] = static_cast<uint8_t>((len + 2) & 0xFF);
        if (len) memcpy(com_ + 4, text, len);
        comLen_ = len + 4;
    }
    bool put(const uint8_t* p, size_t n) {
        while (n && seen_ < 2) {                   // the SOI, then the comment
            if (!out_(ctx_, p, 1)) return false;
            ++p; --n; ++seen_; ++total_;
            if (seen_ == 2) {
                if (!out_(ctx_, com_, comLen_)) return false;
                total_ += comLen_;
            }
        }
        if (!n) return true;
        total_ += n;
        return out_(ctx_, p, n);
    }
    size_t total() const { return total_; }
private:
    Out     out_;
    void*   ctx_;
    uint8_t com_[kComMax + 4] = {};
    size_t  comLen_ = 0;
    uint8_t seen_ = 0;
    size_t  total_ = 0;
};

} // namespace camrules
