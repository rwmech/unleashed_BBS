// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/photos.cpp
// Module:       Core / filing a picture in Photos (1.2.0)
//
// Purpose:      See photos.h.
//
// Libraries:    libc stdio, POSIX mkdir/rename
// Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1) and the Linux host build
// See also:     src/core/photos.h
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3 of the License, or (at your
// option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>. The full
// text is in the LICENSE file at the top of this repository.
// ===========================================================================
#include "photos.h"

#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "clock.h"
#include "disk.h"
#include "runner.h"
#include "sysconfig.h"
#include "../plugins/camera_rules.h"
#include "../platform/platform.h"
#include "../plugins/files.h"        // photoDesc: FILES.BBS's one writer
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"           // the prune's tables in PSRAM on any board that has it
#endif

namespace photos {

namespace {
const Provider* g_prov[2] = {};

// The lower of two levels is the more open one: All < Users < ... < Nobody.
PlugLevel opener(PlugLevel a, PlugLevel b) { return static_cast<uint8_t>(a) < static_cast<uint8_t>(b) ? a : b; }
}  // namespace

bool provide(const Provider& p) {
    for (const Provider*& slot : g_prov) if (slot == &p) return true;
    for (const Provider*& slot : g_prov) {
        if (slot) continue;
        slot = &p;
        pruneSoon();                                 // what is on the card, counted and kept in bounds
        return true;
    }
    return false;
}

void withdraw(const Provider& p) {
    for (const Provider*& slot : g_prov) if (slot == &p) slot = nullptr;
}

bool present() {
    for (const Provider* p : g_prov) if (p && p->running && p->running()) return true;
    return false;
}

void levels(PlugLevel& see, PlugLevel& remove) {
    bool any = false;
    see = PlugLevel::Nobody;
    remove = PlugLevel::Nobody;
    for (const Provider* p : g_prov) {
        if (!p || !p->levels || (p->running && !p->running())) continue;
        PlugLevel s = PlugLevel::Nobody, r = PlugLevel::Nobody;
        p->levels(s, r);
        see = any ? opener(see, s) : s;
        remove = any ? opener(remove, r) : r;
        any = true;
    }
}

bool dir(char* out, size_t n) {
    const char* base = plat::sdBase();
    if (!base || !*base) return false;
    int w = snprintf(out, n, "%s/%s", base, camrules::kPhotosDir);
    return w > 0 && static_cast<size_t>(w) < n;
}

bool open(Writer& w, const char* tmpName) {
    w = Writer();
    char d[128];
    if (!dir(d, sizeof(d)) || !tmpName || tmpName[0] != '.') return false;
    mkdir(d, 0755);                                  // EEXIST is fine
    int k = snprintf(w.tmp, sizeof(w.tmp), "%s/%s", d, tmpName);
    if (k <= 0 || static_cast<size_t>(k) >= sizeof(w.tmp)) return false;
    w.f = disk::open(w.tmp, "wb");
    w.ok = w.f != nullptr;
    return w.ok;
}

bool write(Writer& w, const uint8_t* p, size_t n) {
    if (!w.ok || !w.f) return false;
    if (n && fwrite(p, 1, n, w.f) != n) w.ok = false;
    else w.bytes += static_cast<uint32_t>(n);
    return w.ok;
}

void abandon(Writer& w) {
    if (w.f) fclose(w.f);
    w.f = nullptr;
    if (w.tmp[0]) remove(w.tmp);
    w.ok = false;
}

namespace {
// Callers' snaps filed (callerSnaps). Written by whichever task files the
// picture (the camera's worker, the runner for a sat) and read by the loop,
// each under the runner's lock: a count and a handle copied.
uint16_t g_snaps = 0;
char     g_snapWho[BBS_USER_MAX + 1] = {};
char     g_snapShown[BBS_USER_MAX + 1] = {};   // the loop's copy

void snapFiled(const char* desc) {
    static const char kBy[] = "Taken by ";
    if (strncmp(desc, kBy, sizeof(kBy) - 1)) return;   // not a caller's line
    const char* h = desc + sizeof(kBy) - 1;
    if (*h == '*') ++h;                        // a guest: the handle the panel names
    char who[sizeof(g_snapWho)];
    snprintf(who, sizeof(who), "%.*s", BBS_USER_MAX, h);
    plat::runLock();                           // a copy and a count, nothing more
    memcpy(g_snapWho, who, sizeof(g_snapWho));
    g_snaps = static_cast<uint16_t>(g_snaps + 1);
    plat::runUnlock();
}
}  // namespace

uint16_t callerSnaps(const char*& who) {
    plat::runLock();
    const uint16_t n = g_snaps;
    memcpy(g_snapShown, g_snapWho, sizeof(g_snapShown));
    plat::runUnlock();
    who = g_snapShown;
    return n;
}

#ifdef BBS_HAS_LCD
namespace {
// The photo just filed (photos.h, Filed): written by whichever task files
// it (the runner, for every camera since 1.1.2), read by the loop, the
// record under the runner's lock and its serial an atomic, so the loop's
// every-tick look is one load and the copy is made only when it moved.
Filed                 g_filed;
std::atomic<uint16_t> g_filedSerial{ 0 };

// filedNote: the record for a picture just filed as name (under Photos).
// Its kind from what the filing said: a caller's line ("Taken by"), else
// its folder. Built outside the lock; only the copy is inside it.
void filedNote(const Writer& w, const char* name, const char* desc) {
    Filed f;
    static const char kBy[] = "Taken by ";
    const char* slash = strchr(name, '/');
    char sub[16] = "";
    if (slash) snprintf(sub, sizeof(sub), "%.*s", static_cast<int>(slash - name < 15 ? slash - name : 15), name);
    if (desc && !strncmp(desc, kBy, sizeof(kBy) - 1)) {
        f.kind = FILED_SNAP;
        const char* h = desc + sizeof(kBy) - 1;
        if (*h == '*') ++h;                    // a guest: the handle, as callerSnaps keeps it
        snprintf(f.who, sizeof(f.who), "%.*s", BBS_USER_MAX, h);
    } else if (slash && !strcasecmp(sub, camrules::kTlFolder)) {
        f.kind = FILED_TIMELAPSE;
    } else if (slash && !strcasecmp(sub, camrules::kMotionFolder)) {
        f.kind = FILED_MOTION;
    }
    const size_t tl = strlen(w.tmp), kl = strlen(camrules::kTmpName);
    f.builtIn = tl >= kl && !strcmp(w.tmp + tl - kl, camrules::kTmpName);
    snprintf(f.rel, sizeof(f.rel), "%s", name);
    if (w.camera) snprintf(f.camera, sizeof(f.camera), "%.16s", w.camera);
    plat::runLock();                           // a copy and a count, nothing more
    f.serial = static_cast<uint16_t>(g_filed.serial + 1u);
    if (!f.serial) f.serial = 1;               // 0 means none
    g_filed = f;
    g_filedSerial.store(f.serial);
    plat::runUnlock();
}
}  // namespace

uint16_t filedSerial() { return g_filedSerial.load(); }

void lastFiled(Filed& out) {
    plat::runLock();
    out = g_filed;
    plat::runUnlock();
}
#endif

namespace {
bool fileIn(Writer& w, char* rel, size_t cap, const char* desc, uint8_t later);
void justFiled();                         // a picture went in: a prune, a little after
}

bool file(Writer& w, const char* rel, const char* desc) {
    char r[160];
    if (!rel || snprintf(r, sizeof(r), "%s", rel) >= static_cast<int>(sizeof(r))) { abandon(w); return false; }
    return fileIn(w, r, sizeof(r), desc, 0);
}

bool fileAs(Writer& w, char* rel, size_t cap, const char* desc) {
    return fileIn(w, rel, cap, desc, kLater);
}

bool freeName(const char* rel, char* out, size_t cap) {
    char d[128], dst[300];   // the folder (127) and a name (159)
    if (!rel || !dir(d, sizeof(d))) return false;
    char name[160];
    if (snprintf(name, sizeof(name), "%s", rel) >= static_cast<int>(sizeof(name))) return false;
    struct stat st;
    for (uint8_t i = 0; i <= kLater; ++i) {
        if (i) {
            char next[160];
            if (!camrules::laterName(name, next, sizeof(next))) return false;
            memcpy(name, next, sizeof(name));
        }
        snprintf(dst, sizeof(dst), "%s/%s", d, name);
        if (stat(dst, &st) != 0) {
            const int w = snprintf(out, cap, "%s", name);
            return w > 0 && static_cast<size_t>(w) < cap;
        }
    }
    return false;
}

namespace {
// fileIn: file and fileAs, the name and up to later seconds on.
bool fileIn(Writer& w, char* rel, size_t cap, const char* desc, uint8_t later) {
    if (!w.f) { abandon(w); return false; }
    bool ok = w.ok && fflush(w.f) == 0;
    fsync(fileno(w.f));
    if (fclose(w.f) != 0) ok = false;
    w.f = nullptr;
    char d[128], dst[300];   // the folder (127) and a name (159)
    // A name from a satellite is data from the air: nothing that climbs out
    // of the folder, and none of FAT's other separators (a backslash, or a
    // colon naming a drive).
    if (!ok || !rel || !*rel || strstr(rel, "..") || rel[0] == '/' || strpbrk(rel, "\\:") ||
        !dir(d, sizeof(d))) {
        abandon(w);
        return false;
    }
    // The one folder a name may have (a handle's, a satellite's, a system
    // folder): a second slash is refused, as the Photos area lists one level.
    const char* slash = strchr(rel, '/');
    if (slash && (slash == rel || !slash[1] || strchr(slash + 1, '/'))) {
        abandon(w);
        return false;
    }
    if (slash) {
        char sub[200];
        snprintf(sub, sizeof(sub), "%s/%.*s", d, static_cast<int>(slash - rel), rel);
        mkdir(sub, 0755);
    }
    // The name, then the next second's, up to later on, when another camera
    // took it in the same second. FAT refuses to rename over a file, and a
    // picture never replaces one; the host's rename would replace it, so
    // the name is looked at first. Looked at again after a failed rename:
    // the other camera's filing runs on another task (the built-in camera's
    // worker, the runner) and can take the name between the look and the
    // rename, which is the case the later name is for.
    char name[160];
    if (snprintf(name, sizeof(name), "%s", rel) >= static_cast<int>(sizeof(name))) { abandon(w); return false; }
    bool filed = false;
    struct stat st;
    for (uint8_t i = 0; i <= later; ++i) {
        if (i) {
            char next[160];
            if (!camrules::laterName(name, next, sizeof(next)) || strlen(next) >= cap) break;
            memcpy(name, next, sizeof(name));
        }
        snprintf(dst, sizeof(dst), "%s/%s", d, name);
        if (stat(dst, &st) == 0) continue;
#ifdef BBS_HOST
        // The host's rename would replace a file the other task filed since
        // the look; link does not (EEXIST), so the host takes the same path
        // the card's FatFs does (f_rename refuses an existing name).
        if (link(w.tmp, dst) == 0) { unlink(w.tmp); filed = true; break; }
#else
        if (rename(w.tmp, dst) == 0) { filed = true; break; }
#endif
        if (stat(dst, &st) != 0) break;                // refused, and not for the name
    }
    if (!filed) {
        abandon(w);
        return false;
    }
    justFiled();                                     // one more picture: retention, for every camera
    if (desc && *desc) snapFiled(desc);              // a caller's: the panel's recent list
#ifdef BBS_HAS_LCD
    filedNote(w, name, desc);                        // the panel's new-photo show (1.2.1)
#endif
    if (strcmp(name, rel)) snprintf(rel, cap, "%s", name);
    w.tmp[0] = '\0';
    // Its FILES.BBS line is asked of the file areas, that file's one writer
    // (1.1.2), with the folder relative to Photos ("" for Photos itself). The
    // ask is queued: the line follows the picture, never holds it up.
    if (desc && *desc) {
        char sub[64] = "";
        const char* leaf = rel;
        if (slash) {
            snprintf(sub, sizeof(sub), "%.*s", static_cast<int>(slash - rel < 63 ? slash - rel : 63), rel);
            leaf = slash + 1;
        }
        files::photoDesc(sub, leaf, desc);
    }
    return true;
}
}  // namespace

// ===========================================================================
// Keeping Photos in bounds (photos.h). The loop's half is tick; the rest,
// from pruneWork down, runs on the background runner and touches only the
// prune's own job, the card and its own memory.
// ===========================================================================
namespace {
using camrules::Item;
using camrules::Policy;

// The system folders: timelapse and motion always, then whatever a camera
// names (camera::snapSystem), up to kSysMax in all. The loop's.
constexpr uint8_t kSysMax = 4;
struct SysFolder {
    char     folder[16] = {};
    char     prefix[8]  = {};
    Policy   pol;                         // a named one's own; the fixed two's are CONFIG photos'
};
SysFolder g_sys[kSysMax];
uint8_t   g_sysCount = 0;

// A callers' folder a prune took photos from, for its FILES.BBS tidy.
struct Touched { char name[64]; };
constexpr uint16_t kTouchedMax = 64;      // folders one prune may tidy

// The prune: its inputs copied by the loop before it is posted, its results
// written by the runner, read by the loop only once it is DONE.
struct Prune : runner::Job {
    // inputs
    Policy   pol[kSysMax + 1];            // 0 the callers', 1+ g_sys's order
    char     sysFolder[kSysMax][16] = {};
    char     sysPrefix[kSysMax][8]  = {};
    uint8_t  groups = 1;
    int32_t  floorMb = -1;
    // results
    Tally    out;
    Touched* touched = nullptr;           // heap; the loop takes it over
    uint16_t nTouched = 0;
};
Prune g_prune;

Tally    g_tally;                         // the last finished prune's
std::atomic<bool> g_want{ false };        // a prune is wanted (any task)
uint32_t g_lookAt  = 0;                   // millis of the last once-a-second look
uint32_t g_retryAt = 0;                   // a post the runner refused: not before this
uint32_t g_day     = 0;                   // the local day the last daily prune was for
bool     g_card    = false;               // a card was in at the last look
// The tidies a finished prune asked for, handed to the file areas' queue a
// few a pass as it has room (it holds four): the loop's.
Touched* g_tidy   = nullptr;
uint16_t g_tidyN  = 0, g_tidyAt = 0;
uint32_t g_tidySince = 0;                 // millis the list was taken over
uint32_t g_tidyTryAt = 0;                 // a refused ask: not again before this
// A picture filed (any task): the prune waits kSettleMs after the last one,
// so a burst of pictures (a motion sat, a timelapse at 10 s) is one prune
// after the burst, never one between two of its transfers.
std::atomic<uint32_t> g_filedAt{ 0 };
constexpr uint32_t kSettleMs = 2000;
// Under the floor the board's own shots are skipped, so nothing files and
// nothing recounts: while it lasts, a recount this often.
constexpr uint32_t kUnderMs  = 5u * 60u * 1000u;
uint32_t g_underAt = 0;
uint32_t g_last    = 0;                 // photos the last prune counted, to size the next
// The retention a prune last ran with: a change (CONFIG photos, a restore,
// the old camera lines) is a prune, for every camera.
struct Rules { uint16_t keep, tlKeep; uint32_t max, tlMax; int32_t floor; };
Rules    g_rules{};
bool     g_rulesSet = false;

bool ieqWord(const char* a, const char* b) { return !strcasecmp(a, b); }

void sysDefaults() {
    if (g_sysCount) return;
    snprintf(g_sys[0].folder, sizeof(g_sys[0].folder), "%s", camrules::kTlFolder);
    snprintf(g_sys[0].prefix, sizeof(g_sys[0].prefix), "%s", camrules::kTlPrefix);
    // motion/MO- (a sat's motion shots, 1.2.0): a group of its own, kept by
    // the timelapse's limits (the board's own shots) until CONFIG photos
    // has limits for it. Unlimited, a PIR with a short hold-off fills the
    // card in days and makes every prune a long one.
    snprintf(g_sys[1].folder, sizeof(g_sys[1].folder), "%s", camrules::kMotionFolder);
    snprintf(g_sys[1].prefix, sizeof(g_sys[1].prefix), "MO");
    g_sysCount = 2;
}

// pAlloc: PSRAM first on any board that has it (not only the camera boards:
// an S3 with a sat and no camera of its own), internal RAM otherwise. The
// prune's tables are the runner's for one job, and internal RAM is what
// the radio and the sockets live on.
void* pAlloc(size_t n) {
#ifdef ESP_PLATFORM
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
#else
    return malloc(n);
#endif
}
void pFree(void* p) { free(p); }          // heap_caps memory is free()d too

// maxFound: the most photos one walk remembers. 24 bytes each, in PSRAM
// where the board has it; a board with none keeps it to what internal RAM
// can lend for a moment (the shipped limits are about 600), and a walk past
// it is partial, which never prunes for the floor.
size_t maxFound() {
#ifdef ESP_PLATFORM
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) return 1024;
#endif
    return 16384;
}

uint32_t nameHash(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; ++s) { h ^= static_cast<uint8_t>(*s); h *= 16777619u; }
    return h;
}

// Where a photo is, beside its Item: its name and its folder, as hashes, so
// the second pass finds the very file the first counted. By the folder's
// name, not its place in the card's list: a folder made or removed between
// the passes cannot move a photo onto another folder's.
struct Seen {
    uint32_t nameHash;
    uint32_t subHash;
};
// An Item the second pass removed: its group becomes this, so the tally
// skips it and Seen stays two words.
constexpr uint8_t kGone = 0xFF;

// groupOf: which group a subfolder of Photos is, and the prefix its photos
// carry. A subfolder that is not a system folder is a caller's (by handle).
// Not case sensitive, as FAT is not.
uint8_t groupOf(const Prune& j, const char* sub, const char*& prefix) {
    for (uint8_t i = 0; i + 1 < j.groups; ++i)
        if (ieqWord(j.sysFolder[i], sub)) { prefix = j.sysPrefix[i]; return static_cast<uint8_t>(i + 1); }
    prefix = camrules::kSnapPrefix;
    return 0;
}

// walk: every photo a camera wrote, one level deep, calling
// fn(sub, name, key, bytes, group) for each; sub is "" for the Photos folder
// itself. plat::sdList reads each folder once, sizes and all: no entry is
// looked up by name, which on FAT is a search of the folder. False when it
// did not see every photo there is (a subfolder it could not read or had no
// room to remember): a partial walk is never taken for the whole.
struct Sub { char name[64]; };

template <typename Fn>
bool walk(const Prune& j, Fn fn) {
    struct Top {
        Fn*      fn;
        Sub*     subs;
        uint16_t nSubs, capSubs;
        bool     lost;
    };
    constexpr uint16_t kSubsMax = 256;
    Top top{ &fn, nullptr, 0, 0, false };
    plat::sdList(camrules::kPhotosDir, [](void* ctx, const char* name, bool dir, uint32_t size) {
        Top* t = static_cast<Top*>(ctx);
        if (dir) {
            // The folder list grows as it is needed: most cards have two or
            // three, and 16 KB for 256 of them is internal RAM on a board
            // with no PSRAM.
            if (t->nSubs == t->capSubs && t->capSubs < kSubsMax) {
                const uint16_t cap = t->capSubs ? static_cast<uint16_t>(t->capSubs * 2) : 8;
                Sub* g = static_cast<Sub*>(pAlloc(sizeof(Sub) * cap));
                if (g) {
                    if (t->subs) memcpy(g, t->subs, sizeof(Sub) * t->nSubs);
                    pFree(t->subs);
                    t->subs = g;
                    t->capSubs = cap;
                }
            }
            if (t->nSubs < t->capSubs) snprintf(t->subs[t->nSubs++].name, sizeof(Sub::name), "%.63s", name);
            else t->lost = true;                     // a folder not walked
            return true;
        }
        uint64_t key = 0;
        if (camrules::nameKey(name, camrules::kSnapPrefix, true, key)) (*t->fn)("", name, key, size, 0);
        return true;
    }, &top);
    bool whole = !top.lost;
    for (uint16_t i = 0; i < top.nSubs; ++i) {
        struct In { Fn* fn; const char* sub; const char* prefix; uint8_t g; };
        In in{ &fn, top.subs[i].name, nullptr, 0 };
        in.g = groupOf(j, top.subs[i].name, in.prefix);
        char rel[96];
        snprintf(rel, sizeof(rel), "%s/%.63s", camrules::kPhotosDir, top.subs[i].name);
        const bool read = plat::sdList(rel, [](void* ctx, const char* name, bool dir, uint32_t size) {
            In* x = static_cast<In*>(ctx);
            uint64_t key = 0;
            if (!dir && camrules::nameKey(name, x->prefix, false, key)) (*x->fn)(x->sub, name, key, size, x->g);
            return true;
        }, &in);
        if (!read) whole = false;
        runner::breathe();
    }
    pFree(top.subs);
    return whole;
}

// sortByKey: oldest first, the two arrays together. A heapsort: n log n
// whatever order the folders come off the card in (they interleave in
// time), in place, and a breath every 256 steps.
void sortByKey(Item* it, Seen* seen, size_t n) {
    auto swap2 = [&](size_t a, size_t b) {
        const Item ti = it[a]; it[a] = it[b]; it[b] = ti;
        const Seen ts = seen[a]; seen[a] = seen[b]; seen[b] = ts;
    };
    size_t steps = 0;
    auto sift = [&](size_t root, size_t end) {
        for (;;) {
            size_t c = 2 * root + 1;
            if (c >= end) return;
            if (c + 1 < end && it[c + 1].key > it[c].key) ++c;
            if (it[root].key >= it[c].key) return;
            swap2(root, c);
            root = c;
            if ((++steps & 255) == 0) runner::breathe();
        }
    };
    if (n < 2) return;
    for (size_t i = n / 2; i-- > 0;) sift(i, n);
    for (size_t end = n - 1; end > 0; --end) {
        swap2(0, end);
        sift(0, end);
    }
}

// pruneWork: count what is kept, measure the card, and remove by age, count
// and the floor (camera_rules.h, choose). Two passes over the folders, the
// first to decide and the second to remove, so no name is held in memory.
// The runner's.
void pruneWork(runner::Job& self) {
    Prune& j = static_cast<Prune&>(self);
    j.out = Tally();
    j.touched = nullptr;
    j.nTouched = 0;
    char photos[128];
    if (!dir(photos, sizeof(photos))) return;          // no card: nothing known
    const size_t kMaxFound = maxFound();
    // Sized from the last count, so the tables are not doubled on the way
    // (a doubling holds both copies at once).
    size_t cap = static_cast<size_t>(g_last) + 64, n = 0;
    if (cap > kMaxFound) cap = kMaxFound;
    bool   whole = true;
    Item* it   = static_cast<Item*>(pAlloc(cap * sizeof(Item)));
    Seen* seen = static_cast<Seen*>(pAlloc(cap * sizeof(Seen)));
    if (!it || !seen) { pFree(it); pFree(seen); it = nullptr; seen = nullptr; }
    const bool walked = walk(j, [&](const char* sub, const char* name, uint64_t key, uint32_t bytes, uint8_t g) {
        if (!it || n >= kMaxFound) { whole = false; return; }
        if (n == cap) {
            // Never past maxFound: on a board with no PSRAM that is the limit
            // on internal RAM, and a doubling holds both copies at once.
            const size_t grow = cap * 2 < kMaxFound ? cap * 2 : kMaxFound;
            if (grow <= cap) { whole = false; return; }
            Item* i2 = static_cast<Item*>(pAlloc(grow * sizeof(Item)));
            Seen* s2 = i2 ? static_cast<Seen*>(pAlloc(grow * sizeof(Seen))) : nullptr;
            if (!i2 || !s2) { pFree(i2); whole = false; return; }
            memcpy(i2, it, cap * sizeof(Item));
            memcpy(s2, seen, cap * sizeof(Seen));
            pFree(it);
            pFree(seen);
            it = i2;
            seen = s2;
            cap = grow;
        }
        Item x;
        x.key = key; x.bytes = bytes; x.group = g; x.del = false;
        it[n] = x;
        seen[n] = Seen{ nameHash(name), nameHash(sub) };
        ++n;
    });
    whole = whole && walked;

    // The card's space. When it cannot be read, no floor is applied: a
    // free space of "unknown" read as 0 would take every photo there is.
    uint64_t total = 0, freeB = 0;
    const bool space = plat::sdSpace(total, freeB);
    const uint64_t floor = space ? camrules::floorBytes(total, j.floorMb) : 0;

    bool met = true;
    if (n) {
        sortByKey(it, seen, n);
        uint64_t cut[kSysMax + 1];
        const time_t now = time(nullptr);
        for (uint8_t g = 0; g < j.groups; ++g) cut[g] = j.pol[g].days ? camrules::cutoffKey(now, j.pol[g].days) : 0;
        // Age and count hold on part of the photos (a photo's age is its
        // own, and a count over part is never over the whole's). The floor
        // does not: it takes the oldest of the system groups first, and a
        // walk that missed a system folder would take callers' photos while
        // the ones that should go first stood. So the floor only on a whole
        // walk; a partial one leaves it to the card's own figure below.
        met = camrules::choose(it, n, j.pol, cut, j.groups, freeB, whole ? floor : 0);
    } else if (space && freeB < floor) {
        met = false;                                   // nothing of the cameras' to take
    }

    // The second pass removes what was marked. Each folder of callers'
    // photos it removed from is noted for its FILES.BBS tidy, which the loop
    // hands the file areas (FILES.BBS's one writer, 1.1.2) once this is done.
    // Never a folder nothing was taken from, and never a system folder.
    uint32_t removed = 0;
    size_t   marked  = 0;
    for (size_t i = 0; i < n; ++i) marked += it[i].del ? 1 : 0;
    if (marked) {
        Touched* touched = static_cast<Touched*>(pAlloc(sizeof(Touched) * kTouchedMax));
        uint16_t nt = 0;
        walk(j, [&](const char* sub, const char* name, uint64_t key, uint32_t, uint8_t g) {
            // The keys are sorted: the first with this key, then along the
            // run of equal ones (a caller's folder and the timelapse can both
            // hold a photo of the same second).
            size_t lo = 0, hi = n;
            while (lo < hi) {
                const size_t mid = lo + (hi - lo) / 2;
                if (it[mid].key < key) lo = mid + 1; else hi = mid;
            }
            const uint32_t h = nameHash(name), sh = nameHash(sub);
            for (size_t i = lo; i < n && it[i].key == key; ++i) {
                if (!it[i].del || it[i].group == kGone || seen[i].nameHash != h || seen[i].subHash != sh) continue;
                char path[256];
                if (sub[0]) snprintf(path, sizeof(path), "%s/%.60s/%.60s", photos, sub, name);
                else        snprintf(path, sizeof(path), "%s/%.60s", photos, name);
                if (remove(path) == 0) {
                    ++removed;
                    it[i].group = kGone;
                    bool had = false;
                    for (uint16_t t = 0; t < nt && !had; ++t) had = !strcmp(touched[t].name, sub);
                    if (g == 0 && !had && touched && nt < kTouchedMax)
                        snprintf(touched[nt++].name, sizeof(touched[0].name), "%.63s", sub);
                }
                break;
            }
        });
        if (nt) { j.touched = touched; j.nTouched = nt; }
        else    pFree(touched);
        if (space) plat::sdSpace(total, freeB);
    }
    // After removals, or after a walk that could not see every photo, the
    // floor is what the card says it has now: freed clusters round up past
    // the files' sizes, and a partial walk cannot say that nothing would
    // make the room.
    if (space && (marked || !whole)) met = freeB >= floor;

    Tally& st = j.out;
    st.known     = true;
    st.whole     = whole;
    st.cardTotal = total;
    st.cardFree  = freeB;
    st.floor     = floor;
    st.floorMet  = met;
    st.removed   = removed;
    for (size_t i = 0; i < n; ++i) {
        if (it[i].group == kGone) continue;
        if (it[i].group == 0) { ++st.callers; st.callerBytes += it[i].bytes; }
        else                  { ++st.system;  st.systemBytes += it[i].bytes; }
        if (!st.oldest || it[i].key < st.oldest) st.oldest = it[i].key;
    }
    pFree(it);
    pFree(seen);
}

Rules rulesNow() {
    const SysConfig& c = syscfg::get();
    return Rules{ c.photosKeep, c.photosTlKeep, c.photosMax, c.photosTlMax, c.photosFloor };
}

bool sameRules(const Rules& a, const Rules& b) {
    return a.keep == b.keep && a.tlKeep == b.tlKeep && a.max == b.max && a.tlMax == b.tlMax && a.floor == b.floor;
}

// post: the prune's inputs from the live settings, then the job. The loop's.
bool post(uint32_t now) {
    sysDefaults();
    const Rules r = rulesNow();
    Prune& j = g_prune;
    j.pol[0].days  = r.keep;
    j.pol[0].count = r.max;
    j.groups = static_cast<uint8_t>(1 + g_sysCount);
    for (uint8_t i = 0; i < g_sysCount; ++i) {
        j.pol[i + 1] = g_sys[i].pol;
        if (i < 2) { j.pol[i + 1].days = r.tlKeep; j.pol[i + 1].count = r.tlMax; }   // CONFIG photos'
        snprintf(j.sysFolder[i], sizeof(j.sysFolder[i]), "%s", g_sys[i].folder);
        snprintf(j.sysPrefix[i], sizeof(j.sysPrefix[i]), "%s", g_sys[i].prefix);
    }
    j.floorMb = r.floor;
    j.work = pruneWork;
    j.name = "photos prune";
    if (!runner::post(j)) {
        g_retryAt = (now + 1000) | 1;                  // the queue is full: in a second
        return false;
    }
    g_rules = r;
    g_rulesSet = true;
    return true;
}

// collected: a finished prune's results, into what the loop shows, and its
// tidies taken over.
void collected() {
    Prune& j = g_prune;
    if (j.touched) {
        // Tidies left from the one before (the file areas not running, the
        // queue never free) are dropped: a tidy only removes lines of photos
        // that have gone, so the next that touches the folder does it.
        if (g_tidy) plat::log("photos: %u FILES.BBS tidies not made", static_cast<unsigned>(g_tidyN - g_tidyAt));
        pFree(g_tidy);
        g_tidy = j.touched;
        g_tidyN = j.nTouched;
        g_tidyAt = 0;
        g_tidySince = plat::millis();
        g_tidyTryAt = 0;
        j.touched = nullptr;
        j.nTouched = 0;
    }
    const Tally& t = j.out;
    if (!t.known) return;                              // no card by the time it ran
    g_last = t.callers + t.system;
    if (!t.floorMet && (!g_tally.known || g_tally.floorMet))
        plat::log("photos: the card is under its floor and Photos cannot free enough: photos refused");
    if (t.removed) plat::log("photos: %u old photos removed", static_cast<unsigned>(t.removed));
    if (!t.whole && (!g_tally.known || g_tally.whole))
        plat::log("photos: not every photo could be counted; the floor goes by the card's free space");
    g_tally = t;
}

// tidyTick: the next tidies into the file areas' queue while it has room.
void tidyTick(uint32_t now) {
    if (!g_tidy) return;
    // Room for a tidy leaves a slot for a photo's description (files.h), and
    // a refused ask is not asked again for a second: a refusal logs.
    if (!g_tidyTryAt || static_cast<int32_t>(now - g_tidyTryAt) >= 0) {
        g_tidyTryAt = 0;
        while (g_tidyAt < g_tidyN && files::photoEditRoom()) {
            if (!files::photoTidy(g_tidy[g_tidyAt].name)) { g_tidyTryAt = (now + 1000) | 1; break; }
            ++g_tidyAt;
        }
    }
    // Not handed over in a minute (the file areas off, or no card for them):
    // dropped, as above.
    // plat::since (1.2.1): collected() stamps g_tidySince from plat::millis() a
    // moment before this runs with the tick's earlier now, and unsigned that
    // was 4.29e9 ms: every tidy dropped the moment a prune came back.
    if (g_tidyAt < g_tidyN && plat::since(now, g_tidySince) >= 60000u) {
        plat::log("photos: %u FILES.BBS tidies not made", static_cast<unsigned>(g_tidyN - g_tidyAt));
        g_tidyAt = g_tidyN;
    }
    if (g_tidyAt >= g_tidyN) {
        pFree(g_tidy);
        g_tidy = nullptr;
        g_tidyN = g_tidyAt = 0;
    }
}
}  // namespace

void pruneSoon() { g_want.store(true); }

namespace {
void justFiled() {
    g_filedAt.store(plat::millis() | 1);
    g_want.store(true);
}

// cameraBusy: some camera is taking or bringing in a picture. A camera
// sat's is busy from its ask to the last fragment filed, so a prune never
// holds up a transfer, and the core asks the cameras rather than the radio.
bool cameraBusy() {
    for (uint8_t i = 0; i < cameras(); ++i) {
        const Camera* c = camera(i);
        if (c && c->busy && c->busy(c->ctx)) return true;
    }
    return false;
}
}  // namespace

bool systemFolder(const char* folder, const char* prefix, uint16_t keepDays, uint32_t maxFiles) {
    sysDefaults();
    if (!folder || !prefix) return false;
    for (uint8_t i = 0; i < g_sysCount; ++i) {
        if (!ieqWord(g_sys[i].folder, folder)) continue;
        if (i >= 2) { g_sys[i].pol.days = keepDays; g_sys[i].pol.count = maxFiles; }   // the fixed two keep theirs
        return true;
    }
    if (g_sysCount >= kSysMax) return false;
    SysFolder& f = g_sys[g_sysCount];
    snprintf(f.folder, sizeof(f.folder), "%s", folder);
    snprintf(f.prefix, sizeof(f.prefix), "%s", prefix);
    f.pol.days  = keepDays;
    f.pol.count = maxFiles;
    ++g_sysCount;
    return true;
}

bool pruning() {
    return runner::pending(g_prune) || (g_want.load() && plat::sdBase()[0] && present());
}

const Tally& tally() { return g_tally; }

void tick(uint32_t now) {
    if (runner::done(g_prune)) {
        collected();
        runner::collect(g_prune);
    }
    tidyTick(now);
    if (!runner::idle(g_prune)) return;
    // Once a second: the card coming or going, the day turning, and the
    // retention changing.
    if (!g_lookAt || now - g_lookAt >= 1000) {
        g_lookAt = now ? now : 1;
        const bool card = plat::sdBase()[0] != '\0';
        if (card != g_card) {
            g_card = card;
            g_tally = Tally();                         // another card, or none: count again
            if (card) g_want.store(true);
        }
        if (g_rulesSet && !sameRules(g_rules, rulesNow())) g_want.store(true);
        if (g_tally.known && !g_tally.floorMet) {
            if (!g_underAt) g_underAt = now | 1;
            else if (now - g_underAt >= kUnderMs) { g_underAt = now | 1; g_want.store(true); }
        } else {
            g_underAt = 0;
        }
        if (clk::valid()) {
            const uint32_t day = clk::todayStart();
            if (g_day && day != g_day) g_want.store(true);
            g_day = day;
        }
    }
    if (!g_want.load()) return;
    // Only with a card, and while something takes pictures: a board with no
    // camera does not walk the card every day. Still wanted until then.
    if (!plat::sdBase()[0] || !present()) return;
    // Only with the runner free, no camera taking or bringing in a picture,
    // and kSettleMs after the last one filed: a sat's picture arrives in
    // slices of the runner, and a prune (which cannot be stopped part way)
    // between two slices, or between two pictures of a burst, would hold
    // the transfer up while the radio's ring fills. Wants that come while one
    // waits or runs are one more prune after it, not one each.
    const uint32_t filedAt = g_filedAt.load();
    // Signed: a filing on the runner after now was read is "just now".
    if (filedAt && static_cast<int32_t>(now - filedAt) < static_cast<int32_t>(kSettleMs)) return;
    if (runner::busy() || cameraBusy()) return;
    if (g_retryAt && static_cast<int32_t>(now - g_retryAt) < 0) return;
    g_retryAt = 0;
    g_want.store(false);
    if (!post(now)) g_want.store(true);
}

}  // namespace photos
