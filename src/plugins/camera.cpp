/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/camera.cpp
 * Module:       Plugins / camera (BBS_HAS_CAMERA boards only)
 *
 * Purpose:      Photos from the board's own camera, into the Photos area on
 *               the card (area 12). SNAPSHOT takes one for a caller, who is
 *               then offered the download; the board takes its own on a
 *               timer (the timelapse), and anything else can ask for one
 *               through camera::snapSystem. Built to
 *               internal/PLAN-freenove-cam.md and Rob's decisions after it.
 *
 * Design:       Rule no. 1 (Rob): the online experience without lag is
 *               paramount. So NOTHING the camera does that can take time
 *               runs on the BBS loop. The sensor's bring-up, the capture,
 *               the watermark's re-encode, the card write and the pruning
 *               all run on a worker task (plat::taskStart) on the loop's
 *               own core below the loop's priority: whenever the loop has
 *               anything to do, it runs and the worker waits. The loop only
 *               moves a job between phases, turns the flash on and off
 *               (never in silent mode: the photo is taken without it), and
 *               animates the caller's spinner. Card space is read by the
 *               worker too, and kept.
 *
 *               One job at a time, board-wide, and the job is the lock
 *               rather than a claim keyed by node: a caller who hangs up in
 *               the middle does not cancel the photo (it is saved and
 *               counted), so the job has to outlive the node it came from.
 *
 *               A job's phases, and who moves each:
 *                 Idle     -> Working   loop: a snap starts the worker
 *                 Working  -> Ready     worker: the sensor is up and settled
 *                 Ready    -> Go        loop: the flash is on and has led
 *                 Go       -> Exposed   worker: the frame is taken
 *                 Exposed  -> Writing   loop: the flash is off
 *                 Writing  -> Done      worker: saved (or Failed), pruned
 *                 Done     -> Idle      loop: the caller is told
 *               A survey job (at start, once a day) is Working -> Done.
 *
 *               The camera is brought up for each picture and down after,
 *               never left running: that gives the 32 KB of internal DMA
 *               memory it holds back to the board the rest of the time.
 *
 * Commands:     SNAPSHOT (SNAP)   take a photo (the snap level, staff as
 *                                 shipped), then "Download it now?"
 *               CAMERA (CAM)      staff: what is stored, the space, the
 *                                 last photo; CAMERA SET key value (admin)
 *
 * Libraries:    Espressif esp32-camera (Apache-2.0), through platform.h
 * Targets:      ESP32 and ESP32-S3 camera boards and the Linux host build
 * See also:     src/plugins/camera_rules.h, src/plugins/camera_mark.h,
 *               COMMANDS.md, PLUGINS.md
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

#include "../config.h"
#include "../core/disk.h"              // fopen and opendir that tell the drive light (1.1.1)

#ifdef BBS_HAS_CAMERA
#include "camera.h"
#include "camera_mark.h"
#include "camera_pic.h"
#include "camera_rules.h"
#include "files.h"
#include "lights.h"
#include "../core/bbs.h"
#include "../core/claims.h"
#include "../core/clock.h"
#include "../core/fx.h"
#include "../core/plugin.h"
#include "../core/runner.h"            // the worker is a job on the background runner (1.1.2)
#include "../core/bbs_util.h"          // wrap: the busy line at the width
#include "../core/silent.h"
#include "../core/sysconfig.h"
#include "../platform/platform.h"

#include <atomic>
#include <new>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

using camrules::Item;
using camrules::Policy;

namespace {

constexpr const char kName[] = "camera";
uint8_t g_index   = 0xFF;
bool    g_running = false;

// ---------------------------------------------------------------------------
// Settings (CONFIG camera). The main page holds twelve rows, the most a
// plugin page has room for; the flash, the timelapse and the picture each
// have a page of their own.
// ---------------------------------------------------------------------------
constexpr char kLevels[]  = "all|users|staff|co2|co1|sysop";
constexpr char kFlashes[] = "off|pixel|pin";
constexpr char kWbs[]     = "auto|sunny|cloudy|office|home";
constexpr char kEffects[] = "none|negative|grey|red|green|blue|sepia";

enum : uint8_t { FLASH_OFF = 0, FLASH_PIXEL = 1, FLASH_PIN = 2 };

struct Settings {
    PlugLevel snap     = PlugLevel::Staff;   // Rob: staff as shipped
    PlugLevel photos   = PlugLevel::All;     // Rob: everyone, guests included
    uint8_t   size     = BBS_CAM_SIZE;
    uint8_t   quality  = 10;                 // 1.1.1: 12 before; the frame buffer holds it at UXGA
    uint8_t   names    = camrules::NAME_DATE;
    bool      mark     = true;
    uint16_t  keepDays = 30;
    uint32_t  maxSnaps = 200;                // a file area lists 254 rows at most
    int32_t   floorMb  = -1;                 // -1: a tenth of the card, at most 512 MB
    uint8_t   flash    = BBS_CAM_FLASH;
    int8_t    flashPin = BBS_CAM_FLASH_PIN;
    uint16_t  lead     = 0;                  // ms the flash is on before the frame
    uint32_t  tlEvery  = 0;                  // tl_min * 60 + tl_sec, 0 off, else 10 or more
    uint16_t  tlMin    = 0;
    uint8_t   tlSec    = 0;
    uint16_t  tlKeep   = 7;
    uint32_t  tlMax    = 200;
    bool      flip = false, mirror = false;
    int8_t    bright = 0, contrast = 0, sat = 0, exposure = 0;
    uint8_t   wb = 0, effect = 0;
    bool      levels = true;                 // "Auto levels" (1.1.1): on as shipped
    uint8_t   gamma  = campic::kGammaNone;   // a word of campic::kGammas
};
Settings g_set;

// ---------------------------------------------------------------------------
// The sizes CONFIG offers follow the sensor (1.1.1): until a bring-up has
// found one, the profile's list (BBS_CAM_SIZES, what the board ships with);
// after it, every size that fits the sensor's largest frame. The worker
// sets the count, the loop rebuilds the words. A saved size is kept by its
// word, whatever the sensor: only the size a snap uses is brought down.
// ---------------------------------------------------------------------------
char                 g_sizeChoices[48] = BBS_CAM_SIZES;
std::atomic<uint8_t> g_sizeFound{ 0 };      // 0 until a bring-up says
uint8_t              g_sizeShown = 0;       // the count g_sizeChoices was built for

// sizeCount: how many sizes the camera offers now.
uint8_t sizeCount() {
    const uint8_t n = g_sizeFound.load();
    if (n) return n;
    uint8_t words = 1;
    for (const char* p = BBS_CAM_SIZES; *p; ++p) words += *p == '|';
    return words;
}

// sizeChoicesNow: the words brought up to date with the sensor, from the
// loop only.
void sizeChoicesNow() {
    const uint8_t n = g_sizeFound.load();
    if (!n || n == g_sizeShown) return;
    g_sizeShown = n;
    campic::sizeList(n, g_sizeChoices, sizeof(g_sizeChoices));
}

// wordAt: the n-th word of a bar-separated list, into out.
bool wordAt(const char* list, uint8_t n, char* out, size_t cap) {
    const char* p = list;
    for (uint8_t i = 0; i < n; ++i) {
        p = strchr(p, '|');
        if (!p) return false;
        ++p;
    }
    size_t len = strcspn(p, "|");
    snprintf(out, cap, "%.*s", static_cast<int>(len), p);
    return true;
}

// wordIndex: which word of a list v is, or -1.
int wordIndex(const char* list, const char* v) {
    char w[24];
    for (uint8_t i = 0; wordAt(list, i, w, sizeof(w)); ++i)
        if (!strcasecmp(w, v)) return i;
    return -1;
}

bool yes(const char* v) {
    return !strcasecmp(v, "yes") || !strcasecmp(v, "on") || !strcasecmp(v, "true") || !strcmp(v, "1");
}

// num: a whole number in lo..hi, or false.
bool num(const char* v, long lo, long hi, long& out) {
    char* end = nullptr;
    long n = strtol(v, &end, 10);
    if (!*v || !end || *end || n < lo || n > hi) return false;
    out = n;
    return true;
}

// apply: one key into a settings copy. False for a value this camera will
// not take, which the caller logs or says.
bool apply(Settings& g, const char* key, const char* v) {
    long n = 0;
    int w;
    if (!strcmp(key, "snap"))           { PlugLevel l; if (!plugins::levelFromText(v, l)) return false; g.snap = l; }
    else if (!strcmp(key, "photos"))    { PlugLevel l; if (!plugins::levelFromText(v, l)) return false; g.photos = l; }
    else if (!strcmp(key, "size"))      { if ((w = wordIndex(campic::kAllSizes, v)) < 0) return false; g.size = static_cast<uint8_t>(w); }
    else if (!strcmp(key, "quality"))   { if (!num(v, 4, 40, n)) return false; g.quality = static_cast<uint8_t>(n); }
    else if (!strcmp(key, "names"))     { if ((w = wordIndex(camrules::kSchemes, v)) < 0) return false; g.names = static_cast<uint8_t>(w); }
    else if (!strcmp(key, "watermark")) { g.mark = yes(v); }
    else if (!strcmp(key, "keep"))      { if (!num(v, 0, 3650, n)) return false; g.keepDays = static_cast<uint16_t>(n); }
    else if (!strcmp(key, "max"))       { if (!num(v, 0, 60000, n)) return false; g.maxSnaps = static_cast<uint32_t>(n); }
    else if (!strcmp(key, "floor")) {
        if (!*v) { g.floorMb = -1; return true; }
        if (!num(v, 0, 60000, n)) return false;
        g.floorMb = static_cast<int32_t>(n);
    }
    else if (!strcmp(key, "flash_mode")){ if ((w = wordIndex(kFlashes, v)) < 0) return false; g.flash = static_cast<uint8_t>(w); }
    else if (!strcmp(key, "flash_pin")) { if (!num(v, -1, BBS_GPIO_OUT_MAX, n) || syscfg::pinProblem(n)) return false;
                                          g.flashPin = static_cast<int8_t>(n); }
    else if (!strcmp(key, "flash_lead")){ if (!num(v, 0, 1000, n)) return false; g.lead = static_cast<uint16_t>(n); }
    else if (!strcmp(key, "tl_min"))    { if (!num(v, 0, 1440, n)) return false; g.tlMin = static_cast<uint16_t>(n); }
    else if (!strcmp(key, "tl_sec"))    { if (!num(v, 0, 59, n)) return false; g.tlSec = static_cast<uint8_t>(n); }
    else if (!strcmp(key, "tl_keep"))   { if (!num(v, 0, 3650, n)) return false; g.tlKeep = static_cast<uint16_t>(n); }
    else if (!strcmp(key, "tl_max"))    { if (!num(v, 0, 60000, n)) return false; g.tlMax = static_cast<uint32_t>(n); }
    else if (!strcmp(key, "pic_flip"))  { g.flip = yes(v); }
    else if (!strcmp(key, "pic_mirror")){ g.mirror = yes(v); }
    else if (!strcmp(key, "pic_bright")){ if (!num(v, -2, 2, n)) return false; g.bright = static_cast<int8_t>(n); }
    else if (!strcmp(key, "pic_contrast")) { if (!num(v, -2, 2, n)) return false; g.contrast = static_cast<int8_t>(n); }
    else if (!strcmp(key, "pic_sat"))   { if (!num(v, -2, 2, n)) return false; g.sat = static_cast<int8_t>(n); }
    else if (!strcmp(key, "pic_exposure")) { if (!num(v, -2, 2, n)) return false; g.exposure = static_cast<int8_t>(n); }
    else if (!strcmp(key, "pic_wb"))    { if ((w = wordIndex(kWbs, v)) < 0) return false; g.wb = static_cast<uint8_t>(w); }
    else if (!strcmp(key, "pic_effect")){ if ((w = wordIndex(kEffects, v)) < 0) return false; g.effect = static_cast<uint8_t>(w); }
    else if (!strcmp(key, "pic_levels")){ g.levels = yes(v); }
    else if (!strcmp(key, "pic_gamma")) { if ((w = wordIndex(campic::kGammas, v)) < 0) return false; g.gamma = static_cast<uint8_t>(w); }
    else return false;
    return true;
}

void readKey(void*, const char* key, const char* value) {
    if (!strcmp(key, "enabled") || !strcmp(key, "read") || !strcmp(key, "write") || !strcmp(key, "admin"))
        return;                                         // the core's
    if (!apply(g_set, key, value))
        plat::log("camera: %s = %s is not a value the camera takes, keeping its own", key, value);
}

// ---------------------------------------------------------------------------
// Per caller limits (camera_rules.h): a small table of windows in RAM, one a
// caller: an account's by its handle, carried to the new name when it is
// renamed (onRename), so a rename does not start the count again; a guest's
// by address and by handle both, so a guest cannot start again by
// reconnecting under another name or from another address. Keyed by handle
// rather than by the account's id so a snap reads nothing on the loop
// (users.txt is on the flash). A reboot forgets it (COMMANDS.md says so).
// Taken from the heap once, at the first start, and kept across restarts.
// ---------------------------------------------------------------------------
enum : uint8_t { WHO_FREE = 0, WHO_ACCOUNT, WHO_ADDR, WHO_GUESTNAME };
struct Who {
    uint8_t          kind = WHO_FREE;
    uint32_t         key  = 0;
    camrules::Window w;
};
constexpr uint8_t kWho = 24;
Who* g_who = nullptr;

// The photo each node was offered, so a timed shot or somebody else's
// finishing while a caller decides is never the one sent. Beside the limits,
// on the heap.
constexpr uint8_t kSlots = BBS_MAX_NODES + 2;
struct Offer { char rel[112]; };
Offer* g_offer = nullptr;

uint32_t fnv(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; ++s) { h ^= static_cast<uint8_t>(tolower(static_cast<unsigned char>(*s))); h *= 16777619u; }
    return h;
}

// whoFor: this caller's windows, one or two (a guest's address and name).
uint8_t whoKeys(const Session& s, uint8_t kind[2], uint32_t key[2]) {
    if (!s.guest) {
        kind[0] = WHO_ACCOUNT;
        key[0]  = fnv(s.user);
        return 1;
    }
    kind[0] = WHO_ADDR;      key[0] = s.ipAddr;
    kind[1] = WHO_GUESTNAME; key[1] = fnv(s.user);
    return 2;
}

Who* whoSlot(uint8_t kind, uint32_t key, uint32_t now, bool make) {
    if (!g_who) return nullptr;
    Who* freeOne = nullptr;
    Who* stalest = &g_who[0];
    for (uint8_t i = 0; i < kWho; ++i) {
        Who& w = g_who[i];
        camrules::age(w.w, now);
        if (w.kind == kind && w.key == key) return &w;
        if (w.kind == WHO_FREE || !w.w.n) { if (!freeOne) freeOne = &w; continue; }
        if (w.w.at[w.w.n - 1] < stalest->w.at[stalest->w.n ? stalest->w.n - 1 : 0]) stalest = &w;
    }
    if (!make) return nullptr;
    Who* w = freeOne ? freeOne : stalest;
    *w = Who();
    w->kind = kind;
    w->key  = key;
    return w;
}

// verdictFor: the tighter of the caller's windows.
camrules::Verdict verdictFor(const Session& s, uint32_t now) {
    uint8_t kind[2]; uint32_t key[2];
    uint8_t n = whoKeys(s, kind, key);
    camrules::Verdict v;
    for (uint8_t i = 0; i < n; ++i) {
        Who* w = whoSlot(kind[i], key[i], now, false);
        if (!w) continue;
        camrules::Verdict x = camrules::check(w->w, now);
        if (x.hour > v.hour) v.hour = x.hour;
        if (x.day > v.day)   v.day = x.day;
        if (!x.ok && (v.ok || x.nextAt > v.nextAt)) { v.nextAt = x.nextAt; v.byDay = x.byDay; }
        if (!x.ok) v.ok = false;
    }
    return v;
}

void recordFor(const Session& s, uint32_t now) {
    uint8_t kind[2]; uint32_t key[2];
    uint8_t n = whoKeys(s, kind, key);
    for (uint8_t i = 0; i < n; ++i)
        if (Who* w = whoSlot(kind[i], key[i], now, true)) camrules::record(w->w, now);
}

// ---------------------------------------------------------------------------
// System folders (snapSystem): each its own group for pruning. The
// timelapse is one; a motion sensor would be another.
// ---------------------------------------------------------------------------
constexpr uint8_t kSysMax = 4;
struct SysFolder {
    char     folder[16] = {};
    char     prefix[8]  = {};
    Policy   pol;
};
SysFolder g_sys[kSysMax];
uint8_t   g_sysCount = 0;

// sysFolder: the group for a folder, registering it the first time.
int sysGroup(const char* folder, const char* prefix, uint16_t keep, uint32_t max) {
    for (uint8_t i = 0; i < g_sysCount; ++i) {
        if (!strcmp(g_sys[i].folder, folder)) {
            g_sys[i].pol.days  = keep;
            g_sys[i].pol.count = max;
            return i + 1;
        }
    }
    if (g_sysCount >= kSysMax) return -1;
    SysFolder& f = g_sys[g_sysCount];
    snprintf(f.folder, sizeof(f.folder), "%s", folder);
    snprintf(f.prefix, sizeof(f.prefix), "%s", prefix);
    f.pol.days  = keep;
    f.pol.count = max;
    return ++g_sysCount;
}

// plainWord: a folder or prefix snapSystem may be given: letters and digits.
bool plainWord(const char* w, size_t max) {
    size_t n = strlen(w);
    if (!n || n > max) return false;
    for (size_t i = 0; i < n; ++i) if (!isalnum(static_cast<unsigned char>(w[i]))) return false;
    return true;
}

// ---------------------------------------------------------------------------
// The job
// ---------------------------------------------------------------------------
enum Ph : uint8_t { PH_IDLE, PH_WORKING, PH_READY, PH_GO, PH_EXPOSED, PH_WRITING, PH_DONE, PH_FAILED };
enum Kind : uint8_t { K_CALLER, K_SYSTEM, K_SURVEY };

struct Stats {
    uint32_t callers = 0, system = 0;
    uint64_t callerBytes = 0, systemBytes = 0;
    uint64_t oldest = 0;                   // nameKey of the oldest kept
    uint64_t cardTotal = 0, cardFree = 0;
    uint64_t floor = 0;
    bool     floorMet = true;
    bool     known = false;
};

struct Job {
    std::atomic<uint8_t> ph{ PH_IDLE };
    uint8_t   kind = K_SURVEY;
    bool      probe = false;               // a survey that also looks for the sensor
    uint8_t   node = 0xFF;                 // the caller waiting on it
    bool      waiting = false;             // and still there (loop only)
    uint8_t   group = 0;
    char      rel[112]    = {};            // under the Photos folder
    char      comment[200] = {};
    char      markText[96] = {};
    char      markBoard[48] = {};          // the watermark's pieces, in the font's
    char      markWhen[20]  = {};          // characters: fitted on the worker once
    char      markWho[32]   = {};          // the frame says how wide it really is
    bool      doMark = false;
    bool      raw = false;                 // RGB565 from the sensor, encoded in save
    uint8_t   jq = 80;                     // the re-encode's quality
    char      sizeWord[8] = {};
    plat::CamCfg cam;
    // the picture (camera_pic.h), copied when the job starts
    bool      levels = true;
    uint8_t   gammaT = 10;                 // tenths
    // pruning, copied when the job starts
    Policy    pol[kSysMax + 1];
    char      sysFolder[kSysMax][16] = {};
    char      sysPrefix[kSysMax][8]  = {};
    uint8_t   groups = 1;
    int32_t   floorMb = -1;
    // results
    char      err[72] = {};
    uint32_t  bytes = 0;
    uint16_t  w = 0, h = 0;
    bool      marked = false;
    uint32_t  msUp = 0, msShot = 0, msSave = 0, msAll = 0;
    uint32_t  msFix = 0;                   // the histogram and the tables
    uint8_t   settleFrames = 0;            // frames waited for the exposure
    uint16_t  meterA = 0, meterB = 0;      // the last meter reading (camMeter)
    uint8_t   meterKind = 0;               // plat::CamMeter
    uint32_t  srcBytes = 0;                // the sensor's own JPEG, before any re-encode
    uint8_t   qUsed = 0;                   // the sensor quality the frame was taken at
    bool      fixed = false;               // the tables changed the picture
    uint8_t   lo[3] = {}, hi[3] = {};      // the levels chosen
    uint32_t  stackFree = 0;
    uint32_t  freeUp = 0, dmaUp = 0;       // internal RAM with the camera up
    uint32_t  removed = 0;
    Stats     stats;
    // loop side
    uint32_t  startedAt = 0, phaseAt = 0, spinAt = 0;
    uint8_t   spin = 0;
    bool      flashOn = false, flashOwn = false;
    bool      flashDark = false;           // on, but not lit: silent mode (board::silent)
    uint8_t   flashMode = 0;               // the flash as the job started
    int8_t    flashPin  = -1;
    uint16_t  flashLead = 0;
    char      handle[BBS_USER_MAX + 2] = {};    // a guest's has a * in front
    char      desc[48] = {};               // its FILES.BBS line
};
Job g_job;

Stats    g_stats;                          // what the last job saw
char     g_last[112]  = {};                // the last photo, and who took it
char     g_lastBy[BBS_USER_MAX + 8] = {};
uint32_t g_lastAt = 0;
uint32_t g_surveyDay = 0;                  // the local day the last survey ran
bool     g_surveyWanted = false;
// Whether a sensor answered, this boot: the directory's camera badge
// (announce's "camera" feature) is claimed only on SENSOR_FOUND. Written by
// the worker at each bring-up (the latest one wins), read by the loop. Not
// reset by a CONFIG save's restart: the sensor is the same sensor. Unknown
// until the first survey after boot looks, once, or the first snap.
enum : uint8_t { SENSOR_UNKNOWN, SENSOR_FOUND, SENSOR_MISSING };
std::atomic<uint8_t> g_sensor{ SENSOR_UNKNOWN };
uint32_t g_tlSlot = 0;
bool     g_tlPrimed = false;

// photosDir: the Photos folder on the card, or false with no card.
bool photosDir(char* out, size_t n) {
    const char* base = plat::sdBase();
    if (!base[0]) return false;
    snprintf(out, n, "%s/%s", base, camrules::kPhotosDir);
    return true;
}

// ---------------------------------------------------------------------------
// The worker's side. Everything from here to worker() runs on the worker
// task: the card, the sensor, the codec. It touches the job and nothing the
// loop is using, and says it is finished by setting the phase last.
// ---------------------------------------------------------------------------
struct FileOut {
    FILE*  f;
    size_t written;
    bool   ok;
};

bool fileOut(void* ctx, const uint8_t* p, size_t n) {
    FileOut* o = static_cast<FileOut*>(ctx);
    while (o->ok && n) {                   // 4 KB at a time, with a breath between
        size_t k = n > 4096 ? 4096 : n;
        if (fwrite(p, 1, k, o->f) != k) { o->ok = false; break; }
        o->written += k;
        p += k;
        n -= k;
        if (n) plat::taskSleep(0);
    }
    return o->ok;
}

bool comOut(void* ctx, const uint8_t* p, size_t n) {
    return static_cast<camrules::ComSink*>(ctx)->put(p, n);
}

struct MarkCtx {
    cammark::Box box;
    const char*  text;
    bool         laid;
    uint16_t     w, h;
};

void markRows(void* ctx, uint8_t* rgb, uint16_t width, uint16_t y0, uint16_t rows) {
    MarkCtx* m = static_cast<MarkCtx*>(ctx);
    if (!m->laid) { m->box = cammark::layout(width, m->h, m->text); m->laid = true; }
    cammark::drawRows(rgb, width, y0, rows, m->box, m->text);
}

// Each strip on its way to the encoder: the tables first (the correction),
// then the watermark on top, so the mark is never corrected.
struct PicCtx {
    const uint8_t (*lut)[256];            // null: nothing to correct
    MarkCtx*      mark;                   // null: no watermark
};

void picRows(void* ctx, uint8_t* rgb, uint16_t width, uint16_t y0, uint16_t rows) {
    PicCtx* p = static_cast<PicCtx*>(ctx);
    if (p->lut) campic::apply(p->lut, rgb, static_cast<size_t>(width) * rows);
    if (p->mark) markRows(p->mark, rgb, width, y0, rows);
}

void histPix(void* ctx, const uint8_t* rgb, size_t pixels) {
    campic::histRgb888(rgb, pixels, *static_cast<campic::Hist*>(ctx));
}

// tables: the correction for this picture, into lut (PSRAM, 768 bytes),
// from a histogram of the raw frame or of the JPEG at an eighth of its
// size. False when it would change nothing, or there was no room: the
// picture is then saved as it came, never refused for it.
bool tables(Job& j, const uint8_t* pic, size_t len, uint8_t (*lut)[256]) {
    const uint32_t t0 = plat::millis();
    campic::Hist* h = nullptr;
    if (j.levels) {
        h = static_cast<campic::Hist*>(plat::camAlloc(sizeof(campic::Hist)));
        if (h) {
            memset(h, 0, sizeof(*h));
            if (j.raw) campic::histRgb565(pic, j.w, j.h, 2, *h);
            else if (!plat::jpegHist(pic, len, histPix, h)) h->n = 0;
        }
    }
    campic::buildTables(h, j.levels, j.gammaT, lut, j.lo, j.hi);
    plat::camFree(h);
    j.msFix = plat::millis() - t0;
    j.fixed = !campic::identity(lut);
    return j.fixed;
}

// mkdirs: the folders of a path under Photos, made as needed.
void mkdirs(const char* dir, const char* rel) {
    mkdir(dir, 0755);
    char p[192];
    const char* slash = strrchr(rel, '/');
    if (!slash) return;
    snprintf(p, sizeof(p), "%s/%.*s", dir, static_cast<int>(slash - rel), rel);
    mkdir(p, 0755);
}

// A photo the survey found: where it is, so the second pass can find it.
struct Found {
    uint64_t key;
    uint32_t bytes;
    uint32_t nameHash;
    uint16_t dirIdx;                       // 0 the Photos folder, else a subfolder's number
    uint8_t  group;
    bool     del;
};

uint32_t nameHash(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; ++s) { h ^= static_cast<uint8_t>(*s); h *= 16777619u; }
    return h;
}

// groupOf: which group a subfolder of Photos is, and the prefix its photos
// carry. A subfolder that is not a system folder is a caller's (by handle).
// Not case sensitive, as FAT is not.
uint8_t groupOf(const Job& j, const char* sub, const char*& prefix) {
    for (uint8_t i = 0; i + 1 < j.groups; ++i)
        if (!strcasecmp(j.sysFolder[i], sub)) { prefix = j.sysPrefix[i]; return static_cast<uint8_t>(i + 1); }
    prefix = camrules::kSnapPrefix;
    return 0;
}

// walk: every photo the camera wrote, one level deep, calling
// fn(sub, name, key, bytes, dirIdx, group) for each; sub is "" for the
// Photos folder itself. plat::sdList reads each folder once, sizes and all:
// no entry is looked up by name, which on FAT is a search of the folder.
// dirIdx numbers the subfolders in the order the card holds them, the same
// on both passes because only the worker adds to the folder.
struct Sub { char name[64]; };

template <typename Fn>
void walk(const Job& j, Fn fn) {
    struct Top {
        const Job* j;
        Fn*        fn;
        Sub*       subs;
        uint16_t   nSubs, capSubs;
    };
    constexpr uint16_t kSubsMax = 256;
    Sub* subs = static_cast<Sub*>(plat::camAlloc(sizeof(Sub) * kSubsMax));
    Top top{ &j, &fn, subs, 0, subs ? kSubsMax : static_cast<uint16_t>(0) };
    plat::sdList(camrules::kPhotosDir, [](void* ctx, const char* name, bool dir, uint32_t size) {
        Top* t = static_cast<Top*>(ctx);
        if (dir) {
            if (t->nSubs < t->capSubs) snprintf(t->subs[t->nSubs++].name, sizeof(Sub::name), "%.63s", name);
            return true;
        }
        uint64_t key = 0;
        if (camrules::nameKey(name, camrules::kSnapPrefix, true, key)) (*t->fn)("", name, key, size, 0, 0);
        return true;
    }, &top);
    for (uint16_t i = 0; i < top.nSubs; ++i) {
        struct In { Fn* fn; const char* sub; const char* prefix; uint16_t idx; uint8_t g; };
        In in{ &fn, subs[i].name, nullptr, static_cast<uint16_t>(i + 1), 0 };
        in.g = groupOf(j, subs[i].name, in.prefix);
        char rel[96];
        snprintf(rel, sizeof(rel), "%s/%.63s", camrules::kPhotosDir, subs[i].name);
        plat::sdList(rel, [](void* ctx, const char* name, bool dir, uint32_t size) {
            In* x = static_cast<In*>(ctx);
            uint64_t key = 0;
            if (!dir && camrules::nameKey(name, x->prefix, false, key)) (*x->fn)(x->sub, name, key, size, x->idx, x->g);
            return true;
        }, &in);
        plat::taskSleep(0);
    }
    plat::camFree(subs);
}

// survey: count what is kept, measure the card, and prune by age, count
// and the floor (camera_rules.h, choose). Two passes over the folder, the
// first to decide and the second to remove, so no name is held in memory.
void survey(Job& j, const char* photos) {
    constexpr size_t kMaxFound = 16384;
    size_t cap = 256, n = 0;
    Found* f = static_cast<Found*>(plat::camAlloc(cap * sizeof(Found)));
    walk(j, [&](const char*, const char* name, uint64_t key, uint32_t bytes, uint16_t dir, uint8_t g) {
        if (!f || n >= kMaxFound) return;
        if (n == cap) {
            Found* g2 = static_cast<Found*>(plat::camAlloc(cap * 2 * sizeof(Found)));
            if (!g2) return;
            memcpy(g2, f, cap * sizeof(Found));
            plat::camFree(f);
            f = g2;
            cap *= 2;
        }
        f[n++] = Found{ key, bytes, nameHash(name), dir, g, false };
    });

    // The card's space. When it cannot be read, no floor is applied: a
    // free space of "unknown" read as 0 would take every photo there is.
    uint64_t total = 0, freeB = 0;
    const bool space = plat::sdSpace(total, freeB);
    uint64_t floor = space ? camrules::floorBytes(total, j.floorMb) : 0;

    Item* it = n ? static_cast<Item*>(plat::camAlloc(n * sizeof(Item))) : nullptr;
    bool met = true;
    if (it) {
        for (size_t i = 0; i < n; ++i) { it[i].key = f[i].key; it[i].bytes = f[i].bytes; it[i].group = f[i].group; it[i].del = false; }
        // sort the pair together: by key, carrying Found along
        for (size_t i = 1; i < n; ++i) {
            Item vi = it[i]; Found vf = f[i];
            size_t k = i;
            while (k && it[k - 1].key > vi.key) { it[k] = it[k - 1]; f[k] = f[k - 1]; --k; }
            it[k] = vi; f[k] = vf;
            if ((i & 255) == 0) plat::taskSleep(0);
        }
        uint64_t cut[kSysMax + 1];
        time_t now = time(nullptr);
        for (uint8_t g = 0; g < j.groups; ++g) cut[g] = j.pol[g].days ? camrules::cutoffKey(now, j.pol[g].days) : 0;
        met = camrules::choose(it, n, j.pol, cut, j.groups, freeB, floor);
        for (size_t i = 0; i < n; ++i) f[i].del = it[i].del;
        plat::camFree(it);
    } else if (space && freeB < floor) {
        met = false;                                   // nothing of the camera's to take
    }

    // The second pass removes what was marked. Then, in each caller's folder
    // it removed from: one rewrite of FILES.BBS without their lines, and the
    // folder itself when nothing else is left in it. Never a folder the
    // camera took nothing from, and never a system folder.
    uint32_t removed = 0;
    size_t   marked  = 0;
    for (size_t i = 0; i < n; ++i) marked += f[i].del ? 1 : 0;
    if (marked) {
        constexpr uint16_t kTouchedMax = 64;           // folders a pass may tidy
        struct Touched { char name[64]; uint16_t idx; };
        Touched* touched = static_cast<Touched*>(plat::camAlloc(sizeof(Touched) * kTouchedMax));
        uint16_t nt = 0;
        walk(j, [&](const char* sub, const char* name, uint64_t key, uint32_t, uint16_t dir, uint8_t g) {
            uint32_t h = nameHash(name);
            for (size_t i = 0; i < n; ++i) {
                if (!f[i].del || f[i].key != key || f[i].dirIdx != dir || f[i].nameHash != h) continue;
                char path[256];
                if (sub[0]) snprintf(path, sizeof(path), "%s/%.60s/%.60s", photos, sub, name);
                else        snprintf(path, sizeof(path), "%s/%.60s", photos, name);
                if (remove(path) == 0) {
                    ++removed;
                    f[i].del = false; f[i].bytes = 0; f[i].key = 0;
                    bool seen = false;
                    for (uint16_t t = 0; t < nt; ++t) seen |= touched[t].idx == dir;
                    if (g == 0 && !seen && touched && nt < kTouchedMax) {
                        snprintf(touched[nt].name, sizeof(touched[nt].name), "%.63s", sub);
                        touched[nt++].idx = dir;
                    }
                }
                break;
            }
        });
        // Each folder a photo went from is tidied by the file areas, which
        // are FILES.BBS's one writer (1.1.2): its lines for photos that have
        // gone, and a handle folder left empty removed. Asked, not done.
        for (uint16_t t = 0; t < nt; ++t) files::photoTidy(touched[t].name);
        plat::camFree(touched);
        if (space) plat::sdSpace(total, freeB);
    }

    Stats st;
    st.known = true;
    st.cardTotal = total;
    st.cardFree  = freeB;
    st.floor     = floor;
    st.floorMet  = met;
    for (size_t i = 0; i < n; ++i) {
        if (!f[i].key) continue;                       // removed
        if (f[i].group == 0) { ++st.callers; st.callerBytes += f[i].bytes; }
        else                 { ++st.system;  st.systemBytes += f[i].bytes; }
        if (!st.oldest || f[i].key < st.oldest) st.oldest = f[i].key;
    }
    j.stats   = st;
    j.removed = removed;
    plat::camFree(f);
}

void fail(Job& j, const char* why) {
    snprintf(j.err, sizeof(j.err), "%s", why);
    j.ph.store(PH_FAILED);
}

// sensorSizes: after a bring-up, which sizes the sensor gives.
void sensorSizes() {
    uint16_t w = 0, h = 0;
    if (plat::camMaxSize(w, h)) g_sizeFound.store(campic::sizeCountFor(w, h, 0));
}

// shoot: bring the sensor up, wait for the flash, take the frame, keep a
// copy, and put the sensor down again. The copy is what is written: the
// sensor's 32 KB of internal DMA memory is back before the card is touched.
// On any failure the phase goes straight to Failed, never through Exposed,
// so the loop has one phase to see and nothing to overwrite.
uint8_t* shoot(Job& j, size_t& len) {
    uint32_t t0 = plat::millis();
    char why[72] = "";
    if (!plat::camOpen(j.cam, why, sizeof(why))) {
        g_sensor.store(SENSOR_MISSING);
        plat::camClose();
        fail(j, why[0] ? why : "the camera would not start");
        return nullptr;
    }
    g_sensor.store(SENSOR_FOUND);
    sensorSizes();
    j.freeUp = plat::camInternalFree();                // what the camera left while it runs
    j.dmaUp  = plat::camDmaLargest();
    const uint8_t* buf = nullptr;
    uint16_t w = 0, h = 0;
    // The exposure and white balance settle over the first frames, and the
    // camera comes up cold for every photo, so frames are thrown away until
    // the sensor says it has settled (camera_pic.h): the GC0308 by its own
    // average against its target (Settle, at most kSettleMaxMs), the OV2640
    // by its exposure and gain holding still (Steady, at most
    // kSteadyMaxMs). A sensor with no meter gets the three frames it
    // always had.
    campic::Settle st;
    campic::Steady sd;
    const uint32_t settleFrom = plat::millis();
    for (int i = 0; i < 60; ++i) {
        if (plat::camGrab(buf, len, w, h)) plat::camRelease();
        ++j.settleFrames;
        uint16_t a = 0, b = 0;
        const plat::CamMeter kind = plat::camMeter(a, b);
        j.meterKind = kind;
        j.meterA = a;
        j.meterB = b;
        const uint32_t took = plat::millis() - settleFrom;
        if (kind == plat::CAM_METER_LUMA) {
            if (campic::settled(st, static_cast<uint8_t>(a), static_cast<uint8_t>(b), took) ||
                took >= campic::kSettleMaxMs) break;
        } else if (kind == plat::CAM_METER_EXPOSURE) {
            if (campic::steady(sd, a, b) || took >= campic::kSteadyMaxMs) break;
        } else if (i >= 2) {
            break;
        }
    }
    j.msUp = plat::millis() - t0;
    j.ph.store(PH_READY);
    uint32_t waitFrom = plat::millis();
    while (j.ph.load() == PH_READY && plat::millis() - waitFrom < 5000) plat::taskSleep(5);

    uint32_t t1 = plat::millis();
    uint8_t* copy = nullptr;
    j.qUsed = j.cam.quality;
    for (int tries = 0; tries < 4 && !copy; ++tries) {
        // A JPEG that did not come whole twice running at this quality may
        // be too big for the frame buffer (the driver sizes it by the frame,
        // a fifth of the pixels): one step down, said once.
        if (tries == 2 && !plat::camRaw() && j.qUsed < 40 && plat::camQuality(j.qUsed + 2)) {
            plat::log("camera: no whole frame at quality %u; trying %u", static_cast<unsigned>(j.qUsed),
                      static_cast<unsigned>(j.qUsed + 2));
            j.qUsed = static_cast<uint8_t>(j.qUsed + 2);
        }
        // The frame being filled when the flash came on began before it, so
        // it goes; the one after is the picture.
        if (plat::camGrab(buf, len, w, h)) plat::camRelease();
        if (!plat::camGrab(buf, len, w, h)) continue;
        size_t n = len;
        const bool raw = plat::camRaw();
        if (raw ? (w && h && n == static_cast<size_t>(w) * h * 2u) : camrules::jpegWhole(buf, n)) {
            j.raw = raw;
            copy = static_cast<uint8_t*>(plat::camAlloc(n));
            if (copy) { memcpy(copy, buf, n); len = n; j.w = w; j.h = h; j.srcBytes = static_cast<uint32_t>(n); }
        }
        plat::camRelease();
    }
    plat::camClose();
    j.msShot = plat::millis() - t1;
    if (!copy) { fail(j, "the camera gave no whole picture"); return nullptr; }
    j.ph.store(PH_EXPOSED);
    return copy;
}

// save: write the picture under the temporary name and rename it into
// place. Never over a photo that is there already (FatFs refuses a rename
// onto a name, and the camera never removes one to make room).
void save(Job& j, const char* photos, const uint8_t* jpg, size_t len) {
    uint32_t t0 = plat::millis();
    mkdirs(photos, j.rel);
    char tmp[192], dst[256];
    snprintf(tmp, sizeof(tmp), "%s/%s", photos, camrules::kTmpName);
    snprintf(dst, sizeof(dst), "%s/%s", photos, j.rel);
    struct stat st;
    if (stat(dst, &st) == 0) { fail(j, "a photo with that name is already there"); return; }

    FILE* fp = disk::open(tmp, "wb");
    if (!fp) { fail(j, "the card would not take the photo"); return; }
    FileOut fo{ fp, 0, true };
    bool ok = false;
    if (j.doMark) {
        int max = cammark::maxGlyphs(j.w, j.h);
        if (max > static_cast<int>(sizeof(j.markText)) - 1) max = static_cast<int>(sizeof(j.markText)) - 1;
        cammark::fitText(j.markBoard, j.markWhen, j.markWho, max, j.markText, sizeof(j.markText));
    }
    // The correction's tables, in PSRAM beside the frame: 768 bytes, never a
    // second copy of the picture.
    uint8_t (*lut)[256] = static_cast<uint8_t (*)[256]>(plat::camAlloc(3 * 256));
    const bool fix = lut && tables(j, jpg, len, lut);
    MarkCtx mc{ {}, j.markText, false, j.w, j.h };
    PicCtx pc{ fix ? lut : nullptr, j.doMark ? &mc : nullptr };
    if (j.raw) {
        // No JPEG from the sensor: this is the encode, corrected, watermark
        // or not.
        camrules::ComSink sink(fileOut, &fo, j.comment);
        ok = plat::jpegRaw(jpg, j.w, j.h, j.jq, (fix || j.doMark) ? picRows : nullptr, &pc, comOut, &sink) &&
             fo.ok;
        j.marked = ok && j.doMark;
    } else if (j.doMark || fix) {
        // The sensor's own JPEG, decoded a strip at a time, corrected and
        // marked, and encoded again (the OV2640).
        camrules::ComSink sink(fileOut, &fo, j.comment);
        uint16_t w = 0, h = 0;
        ok = plat::jpegMark(jpg, len, j.jq, picRows, &pc, comOut, &sink, w, h) && fo.ok;
        j.marked = ok && j.doMark;
        if (!ok) {                                     // as it came rather than nothing
            static bool said = false;                  // once: on the host there is no codec at all
            if (!said) plat::log("camera: the photo could not be re-encoded; saving it as the sensor gave it");
            said = true;
            fclose(fp);
            fp = disk::open(tmp, "wb");
            fo = FileOut{ fp, 0, fp != nullptr };
        }
    }
    plat::camFree(lut);
    if (!ok && fp && !j.raw) {
        camrules::ComSink sink(fileOut, &fo, j.comment);
        ok = sink.put(jpg, len) && fo.ok;
    }
    if (fp) {
        if (fflush(fp) != 0) ok = false;
        fsync(fileno(fp));
        if (fclose(fp) != 0) ok = false;
    } else {
        ok = false;
    }
    if (!ok || rename(tmp, dst) != 0) {
        remove(tmp);
        fail(j, "the card would not take the photo");
        return;
    }
    j.bytes  = static_cast<uint32_t>(fo.written);
    // Who took a caller's photo, in its folder's FILES.BBS, as any file is
    // described. Not the board's own: their names say whose they are, and a
    // thousand-line FILES.BBS rewritten for every timed shot is card wear
    // and seconds of the card's time for nothing.
    if (j.kind == K_CALLER) {
        // Asked of the file areas, FILES.BBS's one writer (1.1.2): the
        // handle folder under Photos, or Photos itself.
        char rel[112];
        snprintf(rel, sizeof(rel), "%s", j.rel);
        char* slash = strrchr(rel, '/');
        if (slash) {
            *slash = '\0';
            files::photoDesc(rel, slash + 1, j.desc);
        } else {
            files::photoDesc("", rel, j.desc);
        }
    }
    j.msSave = plat::millis() - t0;
}

void worker(void*) {
    Job& j = g_job;
    uint32_t t0 = plat::millis();
    char photos[96];
    if (!photosDir(photos, sizeof(photos))) { fail(j, "no card in the slot"); return; }
    if (j.kind == K_SURVEY) {
        mkdir(photos, 0755);
        char tmp[160];
        snprintf(tmp, sizeof(tmp), "%s/%s", photos, camrules::kTmpName);
        remove(tmp);                                   // a photo a power cut interrupted
        if (j.probe) {
            // Once a boot, before anybody snaps: is there a sensor at all?
            // Brought up and straight down again, no frame, no flash. What
            // it finds is the camera badge on the directory (announce).
            char why[72] = "";
            const bool up = plat::camOpen(j.cam, why, sizeof(why));
            if (up) sensorSizes();
            plat::camClose();
            g_sensor.store(up ? SENSOR_FOUND : SENSOR_MISSING);
            if (up) plat::log("camera: sensor %s found at start", plat::camSensor());
            else    plat::log("camera: no sensor at start: %s", why[0] ? why : "the camera would not start");
        }
        survey(j, photos);
        j.msAll = plat::millis() - t0;
        j.stackFree = plat::taskStackFree();
        j.ph.store(PH_DONE);
        return;
    }
    size_t len = 0;
    uint8_t* jpg = shoot(j, len);
    if (!jpg) return;
    // The loop turns the flash off on Exposed and moves the job to Writing.
    // Waited for, so the flash is off before the card is written and the
    // caller is told "Developing..." however quick the write turns out to
    // be; not for ever, since the plugin may have been switched off.
    uint32_t waitFrom = plat::millis();
    while (j.ph.load() == PH_EXPOSED && plat::millis() - waitFrom < 2000) plat::taskSleep(5);
    save(j, photos, jpg, len);
    plat::camFree(jpg);
    if (j.ph.load() == PH_FAILED) return;
    survey(j, photos);
    j.msAll = plat::millis() - t0;
    j.stackFree = plat::taskStackFree();
    j.ph.store(PH_DONE);
}

// ---------------------------------------------------------------------------
// The loop's side
// ---------------------------------------------------------------------------
// The worker is a job on the background runner since 1.1.2 (core/runner.h),
// whose task's stack is BBS_RUNNER_STACK: 8 KB, because encoding a raw frame
// (a GC0308's RGB565) and the survey after it left 2,008 of 6,144 free on the
// bench, and an overflow reboots the board.
constexpr uint32_t kWorkerStack = BBS_RUNNER_STACK;

// The camera's job on the runner. The camera's own phases (Job::ph) still
// carry the snap; this is only how the worker gets a task to run on, one
// job in the runner's queue like any other, and the runner's lock rather
// than the old one-global trampoline.
runner::Job g_run;

void runWork(runner::Job&) { worker(nullptr); }
// What a snap takes from internal RAM, the worker's own stack and its task
// block included: the loop's check before the worker is started, since the
// stack comes out of the same memory the camera's DMA block has to.
constexpr uint32_t kSnapInternal = plat::kCamInternal + kWorkerStack + 512;

// roomToSnap: whether internal RAM can take a snap now. The largest DMA
// block and the internal total both: the camera's 32 KB has to be one
// piece, and the worker's stack and the driver's task come out of the same
// memory around it. A refusal is logged with the figures; the board's own
// shots (the timelapse) only the first of a run, so a board short of RAM
// does not log every interval.
bool roomToSnap(bool system) {
    static bool shortBefore = false;
    const uint32_t dma = plat::camDmaLargest(), internal = plat::camInternalFree();
    const bool ok = dma >= plat::kCamDmaBlock && internal >= kSnapInternal;
    if (!ok && (!system || !shortBefore))
        plat::log("camera: %s refused for memory: internal free %u of %u, largest DMA %u of %u",
                  system ? "a timed shot" : "a snap", static_cast<unsigned>(internal),
                  static_cast<unsigned>(kSnapInternal), static_cast<unsigned>(dma),
                  static_cast<unsigned>(plat::kCamDmaBlock));
    shortBefore = !ok;
    return ok;
}

// Busy until the runner has handed the job back too: the worker sets DONE
// on the camera's job a moment before it returns to the runner.
bool jobBusy() { return g_job.ph.load() != PH_IDLE || !runner::idle(g_run); }

void snapshotCfg(Job& j) {
    j.cam = plat::CamCfg();
    // The saved size, or the largest the sensor gives when it is more: said
    // once for each saved size and sensor, not at every snap.
    const uint8_t count = sizeCount();
    const uint8_t use   = campic::clampSize(g_set.size, count);
    if (use != g_set.size) {
        static uint16_t said = 0xFFFF;
        const uint16_t now = static_cast<uint16_t>(g_set.size << 8 | count);
        if (said != now) {
            said = now;
            plat::log("camera: size %s is more than the %s gives; using %s",
                      campic::kSizes[g_set.size < campic::kSizeCount ? g_set.size : 0].word,
                      plat::camSensor()[0] ? plat::camSensor() : BBS_CAM_SENSOR, campic::kSizes[use].word);
        }
    }
    snprintf(j.sizeWord, sizeof(j.sizeWord), "%s", campic::kSizes[use].word);
    j.cam.size       = j.sizeWord;
    j.cam.quality    = g_set.quality;
    j.cam.flip       = g_set.flip;
    j.cam.mirror     = g_set.mirror;
    j.cam.bright     = g_set.bright;
    j.cam.contrast   = g_set.contrast;
    j.cam.saturation = g_set.sat;
    j.cam.exposure   = g_set.exposure;
    j.cam.wb         = g_set.wb;
    j.cam.effect     = g_set.effect;
    // The GC0308's settings its driver leaves out (camera_pic.h), written
    // by camOpen only when that is the sensor it finds.
    campic::Reg regs[6];
    const uint8_t nr = campic::gc0308Regs(g_set.bright, g_set.contrast, g_set.sat, g_set.exposure, regs);
    j.cam.regsPid = 0x9B;                              // GC0308_PID (esp32-camera sensor.h)
    j.cam.nRegs   = nr;
    for (uint8_t i = 0; i < nr; ++i) { j.cam.regs[i][0] = regs[i].reg; j.cam.regs[i][1] = regs[i].val; }
    j.levels = g_set.levels;
    j.gammaT = campic::gammaTenths(g_set.gamma);
    j.jq = campic::reencodeQuality(g_set.quality);      // 90 at the shipped 10
    j.pol[0].days  = g_set.keepDays;
    j.pol[0].count = g_set.maxSnaps;
    j.groups = static_cast<uint8_t>(1 + g_sysCount);
    for (uint8_t i = 0; i < g_sysCount; ++i) {
        j.pol[i + 1] = g_sys[i].pol;
        snprintf(j.sysFolder[i], sizeof(j.sysFolder[i]), "%s", g_sys[i].folder);
        snprintf(j.sysPrefix[i], sizeof(j.sysPrefix[i]), "%s", g_sys[i].prefix);
    }
    j.floorMb = g_set.floorMb;
}

bool startJob(uint8_t kind, uint32_t now) {
    Job& j = g_job;
    j.kind = kind;
    j.err[0] = '\0';
    j.bytes = 0; j.w = j.h = 0; j.marked = false; j.raw = false;
    j.msUp = j.msShot = j.msSave = j.msAll = 0;
    j.msFix = 0; j.settleFrames = 0; j.meterA = j.meterB = 0; j.meterKind = 0; j.fixed = false;
    j.srcBytes = 0; j.qUsed = 0;
    j.freeUp = j.dmaUp = 0;
    j.removed = 0;
    j.startedAt = j.phaseAt = j.spinAt = now;
    j.flashOn = j.flashOwn = j.flashDark = false;
    // roomToSnap walks the heap, so only while the answer is still unknown:
    // once a boot, in practice.
    j.probe = kind == K_SURVEY && g_sensor.load() == SENSOR_UNKNOWN && roomToSnap(true);
    j.flashMode = g_set.flash;
    j.flashPin  = g_set.flashPin;
    j.flashLead = g_set.lead;
    snapshotCfg(j);
    j.ph.store(PH_WORKING);
    g_run.work = runWork;
    g_run.name = "camera";
    if (!runner::post(g_run)) {
        j.ph.store(PH_IDLE);
        plat::log("camera: the worker would not start");
        return false;
    }
    return true;
}

// Local time and the photo's texts.
bool localNow(struct tm& t) {
    if (!clk::valid()) return false;
    time_t e = static_cast<time_t>(clk::epoch());
    localtime_r(&e, &t);
    return true;
}

void texts(Job& j, const struct tm& t, const char* who) {
    const SysConfig& c = syscfg::get();
    char when[20];
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &t);
    char full[24];
    strftime(full, sizeof(full), "%Y-%m-%d %H:%M:%S", &t);
    // UTF-8, micro sign and all (1.1.1): a JPEG comment is bytes, and every
    // viewer that shows one reads UTF-8.
    snprintf(j.comment, sizeof(j.comment), "%s: %s, %s %s. %s %s",
             c.boardName[0] ? c.boardName : BBS_NAME, full,
             j.kind == K_CALLER ? "snapped by" : "taken by the board,", who, BBS_NAME,
             BBS_VERSION_SHOWN);
    if (j.kind == K_CALLER) snprintf(j.desc, sizeof(j.desc), "Taken by %s", who);
    else                    snprintf(j.desc, sizeof(j.desc), "Taken by the board (%s)", who);
    j.doMark = g_set.mark;
    j.markText[0] = '\0';
    if (j.doMark) {
        // Fitted in save(), to the frame the sensor actually gave: a sensor
        // brings a size it cannot do down to its largest.
        cammark::toFont(c.boardName, j.markBoard, sizeof(j.markBoard));
        cammark::toFont(who, j.markWho, sizeof(j.markWho));
        snprintf(j.markWhen, sizeof(j.markWhen), "%s", when);
    }
}

// flashLight: the light itself, pin or pixel, from the loop (the pins and
// the pixels are the loop's to drive).
void flashLight(Job& j, bool on) {
    if (j.flashMode == FLASH_PIN) {
        plat::pinOut(j.flashPin, on);
    } else if (j.flashMode == FLASH_PIXEL) {
        if (on) {
            j.flashOwn = !lights::flash(true);
            if (j.flashOwn && j.flashPin >= 0 && plat::pixelsBegin(0, j.flashPin, 1)) {
                const uint8_t white[3] = { 255, 255, 255 };
                plat::pixelsShow(0, white, 1);
            } else {
                j.flashOwn = false;
            }
        } else {
            lights::flash(false);
            if (j.flashOwn) plat::pixelsEnd(0);
            j.flashOwn = false;
        }
    }
}

// flashSet: the flash on or off for the exposure. In silent mode it is "on"
// but dark (flashDark): the job runs exactly as it would, with no lead, and
// the photo is taken without it (Rob: silent means no lights anywhere).
void flashSet(Job& j, bool on) {
    if (on == j.flashOn) return;
    j.flashOn = on;
    if (on && board::silent()) { j.flashDark = true; return; }
    if (!on && j.flashDark)    { j.flashDark = false; return; }
    flashLight(j, on);
}

// flashQuiet: silent mode starting while the flash is lit puts it out at
// once rather than at the end of the exposure. Asked every tick it is on.
void flashQuiet(Job& j) {
    if (!j.flashOn || j.flashDark || !board::silent()) return;
    flashLight(j, false);
    j.flashDark = true;
}

Session* waiter() {
    Job& j = g_job;
    if (!j.waiting || j.node == 0xFF) return nullptr;
    Session* s = nullptr;
    struct Find { uint8_t id; Session** out; } f{ j.node, &s };
    Bbs::instance().eachSession([](void* ctx, Session& x) {
        Find* ff = static_cast<Find*>(ctx);
        if (x.id == ff->id && x.loggedIn) *ff->out = &x;
    }, &f);
    if (!s || !Bbs::instance().owns(*s, g_index)) return nullptr;
    return s;
}

// spinTo: one frame of the caller's spinner, onto an empty line out, so a
// slow terminal spins slower rather than queueing frames.
void spinTo(Session& s, uint32_t now) {
    Job& j = g_job;
    if (static_cast<int32_t>(now - j.spinAt) < 0 || !s.tl.empty()) return;
    s.term.left(s.tl, 1);
    s.term.color(s.tl, Color::Yellow);
    fx::spinFrame(s.term, s.tl, fx::Spin::Line, ++j.spin);
    j.spinAt = now + 150;
}

void say(Session& s, Color c, const char* text) {
    s.term.color(s.tl, c);
    s.term.text(s.tl, text);
}

void notifyStaff(uint8_t fromNode) {
    struct Ctx { uint8_t from; } c{ fromNode };
    Bbs::instance().eachSession([](void* ctx, Session& x) {
        const Ctx* cc = static_cast<const Ctx*>(ctx);
        if (!x.loggedIn || x.id == cc->from || !plugins::mayUse(x, PlugLevel::Staff)) return;
        char msg[48];
        snprintf(msg, sizeof(msg), "Node %u took a photo.", static_cast<unsigned>(cc->from));
        Bbs::instance().notify(x, msg);
    }, &c);
}

// finish: the job is over; tell whoever is waiting and go idle.
void finish(uint32_t now) {
    Job& j = g_job;
    const bool ok = j.ph.load() == PH_DONE;
    sizeChoicesNow();                                  // the sensor may have said its sizes
    flashSet(j, false);
    if (ok) {
        if (j.stats.known && !j.stats.floorMet && (!g_stats.known || g_stats.floorMet))
            plat::log("camera: the card is under its floor and Photos cannot free enough: photos refused");
        g_stats = j.stats;
        if (j.kind != K_SURVEY) {
            snprintf(g_last, sizeof(g_last), "%.111s", j.rel);
            snprintf(g_lastBy, sizeof(g_lastBy), "%.21s", j.kind == K_CALLER ? j.handle : "the board");
            g_lastAt = clk::epoch();
            // Two lines: plat::log keeps 160 characters, and one line cut
            // the memory figures off the end.
            plat::log("camera: %s %ux%u %u bytes%s, up %u ms, shot %u ms, saved %u ms, all %u ms, "
                      "%u removed",
                      j.rel, static_cast<unsigned>(j.w), static_cast<unsigned>(j.h),
                      static_cast<unsigned>(j.bytes), j.marked ? " marked" : "",
                      static_cast<unsigned>(j.msUp), static_cast<unsigned>(j.msShot),
                      static_cast<unsigned>(j.msSave), static_cast<unsigned>(j.msAll),
                      static_cast<unsigned>(j.removed));
            plat::log("camera: worker stack %u free; internal %u free and largest DMA %u with the camera "
                      "up, largest DMA %u now",
                      static_cast<unsigned>(j.stackFree), static_cast<unsigned>(j.freeUp),
                      static_cast<unsigned>(j.dmaUp), static_cast<unsigned>(plat::camDmaLargest()));
            char luma[48] = "";
            if (j.meterKind == plat::CAM_METER_LUMA)
                snprintf(luma, sizeof(luma), " (average %u, aiming at %u)",
                         static_cast<unsigned>(j.meterA), static_cast<unsigned>(j.meterB));
            else if (j.meterKind == plat::CAM_METER_EXPOSURE)
                snprintf(luma, sizeof(luma), " (exposure %u, gain %u)",
                         static_cast<unsigned>(j.meterA), static_cast<unsigned>(j.meterB));
            if (!j.raw && j.srcBytes)
                plat::log("camera: the sensor's JPEG %u bytes at quality %u; saved %u bytes, %s %u",
                          static_cast<unsigned>(j.srcBytes), static_cast<unsigned>(j.qUsed),
                          static_cast<unsigned>(j.bytes),
                          j.marked || j.fixed ? "encoded again at" : "as it came, not", static_cast<unsigned>(j.jq));
            plat::log("camera: settled after %u frames%s; levels %u-%u %u-%u %u-%u, gamma %u.%u, "
                      "%s, %u ms",
                      static_cast<unsigned>(j.settleFrames), luma,
                      static_cast<unsigned>(j.lo[0]), static_cast<unsigned>(j.hi[0]),
                      static_cast<unsigned>(j.lo[1]), static_cast<unsigned>(j.hi[1]),
                      static_cast<unsigned>(j.lo[2]), static_cast<unsigned>(j.hi[2]),
                      static_cast<unsigned>(j.gammaT / 10), static_cast<unsigned>(j.gammaT % 10),
                      j.fixed ? "corrected" : "as it came", static_cast<unsigned>(j.msFix));
        } else if (j.removed) {
            plat::log("camera: %u old photos removed", static_cast<unsigned>(j.removed));
        }
    } else {
        plat::log("camera: %s failed: %s", j.kind == K_SURVEY ? "survey" : j.rel, j.err);
    }

    Session* s = waiter();
    const uint8_t kind = j.kind, node = j.node;
    j.node = 0xFF;
    j.waiting = false;
    j.ph.store(PH_IDLE);
    if (kind == K_CALLER && ok) notifyStaff(node);
    if (!s) return;

    Bbs& b = Bbs::instance();
    s->term.left(s->tl, 1);
    s->term.text(s->tl, " ");
    s->term.cursor(s->tl, true);
    s->term.nl(s->tl);
    char buf[160];
    if (!ok) {
        snprintf(buf, sizeof(buf), "No photo: %.71s.", j.err);
        say(*s, Color::LightRed, buf);
        b.release(*s);
        return;
    }
    snprintf(buf, sizeof(buf), "Photo saved: %.111s (FILES, area 12)", j.rel);
    say(*s, Color::LightGreen, buf);
    s->term.nl(s->tl);
    const uint8_t fi = plugins::indexOf("files");
    const bool filesOn = fi != 0xFF && plugins::running(fi);
    camrules::Offer o = camrules::offerFor(true, true, filesOn && plugins::mayUse(*s, g_set.photos),
                                           claims::held(claims::Res::Transfer));
    if (o == camrules::Offer::Ask && g_offer && s->id < kSlots) {
        snprintf(g_offer[s->id].rel, sizeof(g_offer[0].rel), "%.111s", j.rel);
        say(*s, Color::Cyan, "Download it now?  [Y]es  [X]modem  [N]o ");
        s->term.color(s->tl, Color::White);
        s->ownerData = 1;                               // awaiting the answer
        return;
    }
    if (o == camrules::Offer::Busy)
        say(*s, Color::Grey, "Somebody is transferring right now: it is in FILES, area 12.");
    else
        say(*s, Color::Grey, "It is kept in the Photos area.");
    b.release(*s);
    (void)now;
}

// ---------------------------------------------------------------------------
// tick: every 20 ms (PF_FAST). Moves the job on, drives the flash and the
// spinner, and starts the timed shots and the daily survey. Never waits.
// ---------------------------------------------------------------------------
void tick(uint32_t now) {
    Job& j = g_job;
    if (runner::done(g_run)) runner::collect(g_run);   // the worker has returned
    const uint8_t ph = j.ph.load();
    if (ph == PH_IDLE) {
        struct tm t;
        bool clock = localNow(t);
        if (g_surveyWanted && plat::sdBase()[0]) {
            g_surveyWanted = false;
            startJob(K_SURVEY, now);
            return;
        }
        if (clock) {
            uint32_t day = static_cast<uint32_t>((t.tm_year + 1900) * 1000 + t.tm_yday);
            if (g_surveyDay && day != g_surveyDay && plat::sdBase()[0]) {
                g_surveyDay = day;
                startJob(K_SURVEY, now);
                return;
            }
            if (!g_surveyDay) g_surveyDay = day;
            uint32_t local = static_cast<uint32_t>(t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec) +
                             static_cast<uint32_t>(t.tm_yday) * camrules::kDay;
            if (camrules::tlDue(local, g_set.tlEvery, g_tlSlot, g_tlPrimed))
                camera::snapSystem(camrules::kTlFolder, camrules::kTlPrefix, g_set.tlKeep, g_set.tlMax);
        }
        return;
    }

    Session* s = waiter();
    flashQuiet(j);                                      // silent mode, from Ready through Go
    if (ph == PH_READY) {
        if (!j.flashOn) {
            flashSet(j, true);
            j.phaseAt = now;
            if (s && (s->term.isAnsi() || s->term.isPet())) {   // the shutter: one reverse cell
                s->term.left(s->tl, 1);
                s->term.reverse(s->tl, true);
                s->term.text(s->tl, " ");
                s->term.reverse(s->tl, false);
                j.spinAt = now + 150;
            }
        }
        // The lead, and for a pixel one lights frame (20 ms) to reach it.
        // None for a flash that is dark: there is nothing to lead with.
        uint32_t lead = j.flashMode == FLASH_OFF || j.flashDark
                        ? 0 : j.flashLead + (j.flashMode == FLASH_PIXEL ? 40u : 0u);
        // Compared and swapped: the worker never moves past Ready on its
        // own but for its timeout, and a blind store must not undo it.
        uint8_t want = PH_READY;
        if (now - j.phaseAt >= lead) j.ph.compare_exchange_strong(want, PH_GO);
    } else if (ph == PH_EXPOSED) {
        flashSet(j, false);
        uint8_t want = PH_EXPOSED;
        j.ph.compare_exchange_strong(want, PH_WRITING);
        if (s) {                                        // "* Developing... /"
            s->term.left(s->tl, 1);
            say(*s, Color::LightGreen, "*");
            say(*s, Color::Grey, " Developing... ");
            s->term.color(s->tl, Color::Yellow);
            fx::spinFrame(s->term, s->tl, fx::Spin::Line, j.spin);
            j.spinAt = now + 150;
        }
    } else if (ph == PH_DONE || ph == PH_FAILED) {
        finish(now);
        return;
    }
    if (s) spinTo(*s, now);
}

// ---------------------------------------------------------------------------
// SNAPSHOT
// ---------------------------------------------------------------------------
void refuse(Bbs& b, Session& s, const char* why) {
    say(s, Color::LightRed, why);
    b.prompt(s);
}

void cmdSnapshot(Bbs& b, Session& s, const char*, uint32_t now) {
    if (!g_running || !plat::sdBase()[0]) { refuse(b, s, "The camera needs the SD card in."); return; }
    if (!plugins::mayUse(s, g_set.snap)) { refuse(b, s, "Taking photos is not open to you here."); return; }
    if (!clk::valid()) { refuse(b, s, "The board's clock is not set yet, and a photo is named by it."); return; }
    const uint32_t epoch = clk::epoch();
    const bool sysop = plugins::mayUse(s, PlugLevel::Sysop);
    camrules::Verdict v;
    if (!sysop) {
        v = verdictFor(s, epoch);
        if (!v.ok) {
            char at[8], buf[96];
            clk::fmtEpoch(at, sizeof(at), "%H:%M", v.nextAt);
            snprintf(buf, sizeof(buf), "That is %u %s; the next one is allowed at %s.",
                     static_cast<unsigned>(v.byDay ? camrules::kPerDay : camrules::kPerHour),
                     v.byDay ? "today" : "this hour", at);
            refuse(b, s, buf);
            return;
        }
    }
    if (g_stats.known && !g_stats.floorMet) {
        g_surveyWanted = true;                         // count again: space may have been freed
        refuse(b, s, "The card is too full for another photo.");
        return;
    }
    if (jobBusy()) {
        // One snapshot at a time (Rob, 2026-09-26), and who has it, in the
        // room's voice: "--> Camera in use by node 3, try again in a minute".
        // The board's own shots (the timelapse, the daily count) have no
        // node. Wrapped at a word on a narrow terminal.
        char why[64], row[64];
        if (g_job.node != 0xFF && g_job.kind == K_CALLER)
            snprintf(why, sizeof(why), "Camera in use by node %u, try again in a minute",
                     static_cast<unsigned>(g_job.node));
        else
            snprintf(why, sizeof(why), "Camera in use by the board, try again in a minute");
        const uint8_t w = static_cast<uint8_t>(b.rowWidth(s) > 4 ? b.rowWidth(s) - 4 : 36);
        bool first = true;
        for (const char* q = bbsu::wrap(why, row, sizeof(row), w); ; q = bbsu::wrap(q, row, sizeof(row), w)) {
            say(s, Color::Cyan, first ? "--> " : "    ");
            say(s, Color::Yellow, row);
            s.term.nl(s.tl);
            first = false;
            if (!q || !*q) break;
        }
        b.prompt(s);
        return;
    }
    if (!roomToSnap(false)) {
        refuse(b, s, "The camera needs memory the board is using. Try in a minute.");
        return;
    }
    struct tm t;
    localNow(t);
    Job& j = g_job;
    j.kind = K_CALLER;
    if (!camrules::callerName(g_set.names, t, s.user, s.guest, j.rel, sizeof(j.rel))) {
        refuse(b, s, "That photo's name would be too long.");
        return;
    }
    snprintf(j.handle, sizeof(j.handle), "%s%s", s.guest ? "*" : "", s.user);
    texts(j, t, j.handle);
    if (!b.own(s, g_index)) { b.prompt(s); return; }
    b.setDoing(s, "SNAPSHOT");
    s.ownerData = 0;
    j.node    = s.id;
    j.waiting = true;
    if (!startJob(K_CALLER, now)) {
        j.waiting = false;
        say(s, Color::LightRed, "The camera would not start.");
        b.release(s);
        return;
    }
    if (!sysop) {
        recordFor(s, epoch);
        char buf[80];
        snprintf(buf, sizeof(buf), "Snapshot %u of %u this hour, %u of %u today.",
                 static_cast<unsigned>(v.hour + 1), static_cast<unsigned>(camrules::kPerHour),
                 static_cast<unsigned>(v.day + 1), static_cast<unsigned>(camrules::kPerDay));
        say(s, Color::Grey, buf);
        s.term.nl(s.tl);
    }
    // The spinner, then "Developing...", then the result: no countdown and
    // nothing to smile for (Rob: a caller is not in front of the camera).
    s.term.cursor(s.tl, false);
    s.term.color(s.tl, Color::Yellow);
    fx::spinFrame(s.term, s.tl, fx::Spin::Line, 0);
}

// onKey: the download question, and nothing else: the keys a caller types
// while the photo is being taken are ignored.
void onKey(Session& s, int k, uint32_t now) {
    Bbs& b = Bbs::instance();
    if (s.ownerData != 1) return;
    s.ownerData = 0;
    s.term.cursor(s.tl, true);
    s.term.nl(s.tl);
    if (k == 'y' || k == 'Y' || k == 'x' || k == 'X') {
        char rel[112] = "";
        if (g_offer && s.id < kSlots) snprintf(rel, sizeof(rel), "%s", g_offer[s.id].rel);
        b.release(s);
        if (!files::sendPhoto(b, s, rel, k == 'x' || k == 'X', now)) b.prompt(s);
        return;
    }
    say(s, Color::Grey, "It is kept in the Photos area.");
    b.release(s);
}

// onRename: an account's window follows its new handle.
void onRename(const char* oldHandle, const char* newHandle) {
    if (!g_who) return;
    const uint32_t from = fnv(oldHandle), to = fnv(newHandle);
    for (uint8_t i = 0; i < kWho; ++i)
        if (g_who[i].kind == WHO_ACCOUNT && g_who[i].key == from) g_who[i].key = to;
}

void onLogoff(Session& s) {
    if (g_job.node == s.id) g_job.waiting = false;     // the photo is still saved and counted
}

void onLogin(Session& s) {
    if (!g_running) return;
    if (g_set.snap >= PlugLevel::Staff) return;
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, "This board has a camera callers can use.");
    s.term.nl(s.tl);
    if (g_set.tlEvery) {
        s.term.text(s.tl, "It also takes a photo of its own on a timer.");
        s.term.nl(s.tl);
    }
    s.term.reset(s.tl);
}

// ---------------------------------------------------------------------------
// CAMERA: what is stored, for staff; CAMERA SET for the admin level
// ---------------------------------------------------------------------------
void mb(char* out, size_t n, uint64_t bytes) {
    if (bytes >= 10ull * 1024 * 1024 * 1024) snprintf(out, n, "%u GB", static_cast<unsigned>(bytes >> 30));
    else snprintf(out, n, "%u MB", static_cast<unsigned>(bytes >> 20));
}

void cmdCamera(Bbs& b, Session& s, const char* arg, uint32_t) {
    while (*arg == ' ') ++arg;
    char buf[112];
    if (!strncasecmp(arg, "SET", 3) && (arg[3] == ' ' || !arg[3])) {
        if (!plugins::mayUse(s, plugins::levelFor(g_index, 2))) { refuse(b, s, "CAMERA SET is for the sysop."); return; }
        const char* p = arg + 3;
        while (*p == ' ') ++p;
        char key[24] = {};
        size_t k = 0;
        while (*p && *p != ' ' && k + 1 < sizeof(key)) key[k++] = *p++;
        while (*p == ' ') ++p;
        Settings trial = g_set;
        if (!key[0] || !apply(trial, key, p)) { refuse(b, s, "CAMERA SET <key> <value>: not a value the camera takes."); return; }
        syscfg::KeyVal kv{ key, p };
        char err[64] = "";
        if (!syscfg::write(&kv, 1, "plugin:camera", err, sizeof(err))) { refuse(b, s, err[0] ? err : "Not written."); return; }
        trial.tlEvery = static_cast<uint32_t>(trial.tlMin) * 60u + trial.tlSec;
        if (trial.tlEvery > camrules::kTlMax) trial.tlEvery = camrules::kTlMax;
        if (trial.tlEvery && trial.tlEvery < camrules::kTlMin) trial.tlEvery = camrules::kTlMin;
        g_set = trial;
        snprintf(buf, sizeof(buf), "Set %s = %s, live now.", key, p);
        say(s, Color::LightGreen, buf);
        b.prompt(s);
        return;
    }
    b.rowTitle(s, "CAMERA");
    {
        // The sensor by name once a bring-up has found one: "not found"
        // and "not looked for yet" are different evenings.
        const uint8_t count = sizeCount();
        const uint8_t use   = campic::clampSize(g_set.size, count);
        const uint8_t sen   = g_sensor.load();
        if (sen == SENSOR_FOUND)
            snprintf(buf, sizeof(buf), "Sensor %s, up to %s", plat::camSensor(),
                     campic::kSizes[count ? count - 1 : 0].word);
        else if (sen == SENSOR_MISSING)
            snprintf(buf, sizeof(buf), "No sensor found (%s as shipped)", BBS_CAM_SENSOR);
        else
            snprintf(buf, sizeof(buf), "Sensor not looked for yet (%s as shipped)", BBS_CAM_SENSOR);
        b.rowText(s, sen == SENSOR_MISSING ? Color::LightRed : Color::White, buf);
        char saved[16] = "";
        if (use != g_set.size)
            snprintf(saved, sizeof(saved), " (saved %s)",
                     campic::kSizes[g_set.size < campic::kSizeCount ? g_set.size : 0].word);
        snprintf(buf, sizeof(buf), "Size %s%s, quality %u%s", campic::kSizes[use].word, saved,
                 static_cast<unsigned>(g_set.quality), g_set.mark ? ", watermarked" : "");
        b.rowText(s, Color::White, buf);
        char gw[8] = "1.0";
        wordAt(campic::kGammas, g_set.gamma, gw, sizeof(gw));
        snprintf(buf, sizeof(buf), "Levels %s, gamma %s", g_set.levels ? "auto" : "off", gw);
    }
    b.rowText(s, Color::White, buf);
    if (!g_stats.known) {
        b.rowText(s, Color::Grey, jobBusy() ? "Counting the photos on the card..." : "No count yet.");
    } else {
        char cb[16], sb[16], fb[16], fl[16];
        mb(cb, sizeof(cb), g_stats.callerBytes);
        mb(sb, sizeof(sb), g_stats.systemBytes);
        mb(fb, sizeof(fb), g_stats.cardFree);
        mb(fl, sizeof(fl), g_stats.floor);
        snprintf(buf, sizeof(buf), "Photos %u (%s), timed %u (%s)", static_cast<unsigned>(g_stats.callers), cb,
                 static_cast<unsigned>(g_stats.system), sb);
        b.rowText(s, Color::White, buf);
        if (g_stats.oldest) {
            unsigned long long o = static_cast<unsigned long long>(g_stats.oldest / 1000000ull);
            snprintf(buf, sizeof(buf), "Oldest kept %04llu-%02llu-%02llu", o / 10000, (o / 100) % 100, o % 100);
            b.rowText(s, Color::White, buf);
        }
        snprintf(buf, sizeof(buf), "Card free %s, floor %s%s", fb, fl, g_stats.floorMet ? "" : " (UNDER)");
        b.rowText(s, g_stats.floorMet ? Color::White : Color::LightRed, buf);
    }
    if (g_last[0]) {
        char at[20];
        clk::fmtEpoch(at, sizeof(at), "%Y-%m-%d %H:%M", g_lastAt);
        snprintf(buf, sizeof(buf), "Last %s by %s", at, g_lastBy);
        b.rowText(s, Color::White, buf);
        b.rowText(s, Color::Grey, g_last);
    }
    if (g_set.tlEvery) {
        snprintf(buf, sizeof(buf), "Timelapse every %u s, kept %u days", static_cast<unsigned>(g_set.tlEvery),
                 static_cast<unsigned>(g_set.tlKeep));
        b.rowText(s, Color::White, buf);
    }
    if (!plugins::mayUse(s, PlugLevel::Sysop)) {
        camrules::Verdict v = verdictFor(s, clk::epoch());
        snprintf(buf, sizeof(buf), "You: %u this hour, %u today", static_cast<unsigned>(v.hour),
                 static_cast<unsigned>(v.day));
        b.rowText(s, Color::Grey, buf);
    }
    b.rowRule(s);
    b.prompt(s);
}

const Command kCommands[] = {
    { "SNAPSHOT", "", 0, CF_READ, "SNAPSHOT", "take a photo with the board's camera", cmdSnapshot,
      Menu::Account, 45 },
    { "SNAP", "", 0, CF_READ | CF_HIDDEN, "", "", cmdSnapshot, Menu::Hidden, 99 },
    { "CAMERA", "", 0, CF_WRITE, "CAMERA [SET]", "the camera: photos kept, space, last", cmdCamera,
      Menu::Staff, 60 },
    { "CAM", "", 0, CF_WRITE | CF_HIDDEN, "", "", cmdCamera, Menu::Hidden, 99 },
};

// ---------------------------------------------------------------------------
// CONFIG camera. Labels are 9 characters at 40 columns; the wide ones for
// 80 are the plan's wording.
// ---------------------------------------------------------------------------
const PluginSetting kSettings[] = {
    { "snap",      "Snap",      PS_CYCLE, 0, 0, 6, "Who may take a photo.", kLevels,
      "Who may take a photo" },
    { "photos",    "Photos",    PS_CYCLE, 0, 0, 6, "Who may see and download photos.", kLevels,
      "Who may see photos" },
    // The choices follow the sensor the last bring-up found (g_sizeChoices).
    { "size",      "Size",      PS_CYCLE, 0, 0, 5, "What this sensor gives; up to its largest.",
      g_sizeChoices, "Resolution" },
    // One direction on every sensor: the OV2640's own JPEG takes it as is,
    // and every re-encode (the GC0308's, the watermark's) is 100 minus it.
    { "quality",   "Quality",   PS_NUM,   4, 40, 2, "Lower: sharper, bigger. 10-12 is good.", nullptr,
      "JPEG quality", "4 to 40, lower is sharper and bigger; 10-12 suggested. Same on every sensor." },
    { "names",     "Names",     PS_CYCLE, 0, 0, 12, "SNAP-date, with the handle, or by handle.",
      camrules::kSchemes, "Name snaps" },
    { "watermark", "Watermark", PS_YESNO, 0, 0, 3, "Board, date and who, in a corner.", nullptr,
      "Watermark" },
    { "keep",      "Keep days", PS_NUM,   0, 3650, 4, "Callers' photos; 0 keeps them.", nullptr,
      "Keep snaps (days)", "Callers' photos older than this are removed; 0 keeps them." },
    { "max",       "Max snaps", PS_NUM,   0, 60000, 5, "Callers' photos kept; 0 no limit.", nullptr,
      "Max caller snaps" },
    { "floor",     "Floor MB",  PS_OPTNUM, 0, 60000, 5, "Card space kept free; empty: auto.", nullptr,
      "Card floor (MB)", "Space the camera leaves free on the card. Empty: a tenth, 512 MB at most." },
    { "flash",     "Flash",     PS_PAGE,  0, 0, 12, "The light for a photo.", nullptr, "Flash" },
    { "tl",        "Timelapse", PS_PAGE,  0, 0, 16, "A photo every so often.", nullptr, "Timelapse" },
    { "pic",       "Picture",   PS_PAGE,  0, 0, 12, "Levels, brightness, colour, turn.", nullptr, "Picture settings" },

    // The flash's page
    { "flash_mode", "Mode",     PS_CYCLE, 0, 0, 5, "off, a pixel, or a pin driven high.", kFlashes,
      "Flash", "pixel: a WS2812 goes white. pin: the pin goes high (an LED, a relay, a flash)." },
    { "flash_pin",  "Pin",      PS_PIN,  -1, BBS_GPIO_OUT_MAX, 2, "-1 for none.", nullptr, "Flash pin" },
    { "flash_lead", "Lead ms",  PS_NUM,   0, 1000, 4, "On this long before the shot.", nullptr,
      "Flash lead (ms)" },

    // The timelapse's page
    // Minutes and seconds rather than one figure: a form's number stops at
    // 65,535 and a day is 86,400 seconds. Both 0 is off; under 10 s is 10.
    { "tl_min",    "Every min", PS_NUM,   0, 1440, 4, "Minutes; 1440 is a day. 0 0 is off.", nullptr,
      "Every (minutes)", "Minutes between the board's own photos, with the seconds below. Both 0 is off." },
    { "tl_sec",    "and sec",   PS_NUM,   0, 59, 2, "Seconds; the least is 10 in all.", nullptr,
      "and seconds", "Seconds on top of the minutes. The shortest interval is 10 seconds." },
    { "tl_keep",   "Keep days", PS_NUM,   0, 3650, 4, "Timed photos; 0 keeps them.", nullptr,
      "Keep shots (days)", "Timed photos older than this are removed; 0 keeps them." },
    { "tl_max",    "Max shots", PS_NUM,   0, 60000, 5, "Timed photos kept; 0 no limit.", nullptr,
      "Max timelapse shots" },

    // The picture's page
    { "pic_flip",     "Flip",     PS_YESNO, 0, 0, 3, nullptr, nullptr, "Upside down" },
    { "pic_mirror",   "Mirror",   PS_YESNO, 0, 0, 3, nullptr, nullptr, "Mirror" },
    { "pic_bright",   "Bright",   PS_NUM,  -2, 2, 2, "-2 to 2.", nullptr, "Brightness" },
    { "pic_contrast", "Contrast", PS_NUM,  -2, 2, 2, "-2 to 2.", nullptr, "Contrast" },
    { "pic_sat",      "Colour",   PS_NUM,  -2, 2, 2, "-2 to 2.", nullptr, "Saturation" },
    { "pic_exposure", "Exposure", PS_NUM,  -2, 2, 2, "-2 to 2.", nullptr, "Exposure level" },
    { "pic_wb",       "White",    PS_CYCLE, 0, 0, 7, nullptr, kWbs, "White balance" },
    { "pic_effect",   "Effect",   PS_CYCLE, 0, 0, 8, nullptr, kEffects, "Effect" },
    // The board's own correction, on the worker before the encode, for any
    // sensor (camera_pic.h).
    { "pic_levels",   "Levels",   PS_YESNO, 0, 0, 3, "Stretch to full black and white.", nullptr,
      "Auto levels", "Stretches each photo to full black and white, and takes a colour cast out." },
    { "pic_gamma",    "Gamma",    PS_CYCLE, 0, 0, 3, "1.0 none; lower darkens the middle.",
      campic::kGammas, "Gamma", "1.0 changes nothing. Lower darkens the middle tones (a washed-out sky)." },
};

void setting(const char* key, char* out, size_t n) {
    char w[24];
    if      (!strcmp(key, "snap"))      snprintf(out, n, "%s", plugins::levelName(g_set.snap));
    else if (!strcmp(key, "photos"))    snprintf(out, n, "%s", plugins::levelName(g_set.photos));
    else if (!strcmp(key, "size"))      snprintf(out, n, "%s", campic::kSizes[campic::clampSize(g_set.size, sizeCount())].word);
    else if (!strcmp(key, "quality"))   snprintf(out, n, "%u", static_cast<unsigned>(g_set.quality));
    else if (!strcmp(key, "names"))     { wordAt(camrules::kSchemes, g_set.names, w, sizeof(w)); snprintf(out, n, "%s", w); }
    else if (!strcmp(key, "watermark")) snprintf(out, n, "%s", g_set.mark ? "yes" : "no");
    else if (!strcmp(key, "keep"))      snprintf(out, n, "%u", static_cast<unsigned>(g_set.keepDays));
    else if (!strcmp(key, "max"))       snprintf(out, n, "%u", static_cast<unsigned>(g_set.maxSnaps));
    else if (!strcmp(key, "floor"))     { if (g_set.floorMb >= 0) snprintf(out, n, "%d", static_cast<int>(g_set.floorMb)); }
    else if (!strcmp(key, "flash")) {
        if (g_set.flash == FLASH_PIN) snprintf(out, n, "pin %d", static_cast<int>(g_set.flashPin));
        else { wordAt(kFlashes, g_set.flash, w, sizeof(w)); snprintf(out, n, "%s", w); }
    }
    else if (!strcmp(key, "tl")) {
        if (g_set.tlEvery) snprintf(out, n, "every %u s", static_cast<unsigned>(g_set.tlEvery));
        else               snprintf(out, n, "off");
    }
    else if (!strcmp(key, "pic"))       snprintf(out, n, "%s", (g_set.flip || g_set.mirror || g_set.bright || g_set.contrast ||
                                                               g_set.sat || g_set.exposure || g_set.wb || g_set.effect ||
                                                               !g_set.levels || g_set.gamma != campic::kGammaNone)
                                                               ? "adjusted" : "as it comes");
    else if (!strcmp(key, "flash_mode")){ wordAt(kFlashes, g_set.flash, w, sizeof(w)); snprintf(out, n, "%s", w); }
    else if (!strcmp(key, "flash_pin")) snprintf(out, n, "%d", static_cast<int>(g_set.flashPin));
    else if (!strcmp(key, "flash_lead"))snprintf(out, n, "%u", static_cast<unsigned>(g_set.lead));
    else if (!strcmp(key, "tl_min"))    snprintf(out, n, "%u", static_cast<unsigned>(g_set.tlMin));
    else if (!strcmp(key, "tl_sec"))    snprintf(out, n, "%u", static_cast<unsigned>(g_set.tlSec));
    else if (!strcmp(key, "tl_keep"))   snprintf(out, n, "%u", static_cast<unsigned>(g_set.tlKeep));
    else if (!strcmp(key, "tl_max"))    snprintf(out, n, "%u", static_cast<unsigned>(g_set.tlMax));
    else if (!strcmp(key, "pic_flip"))  snprintf(out, n, "%s", g_set.flip ? "yes" : "no");
    else if (!strcmp(key, "pic_mirror"))snprintf(out, n, "%s", g_set.mirror ? "yes" : "no");
    else if (!strcmp(key, "pic_bright"))snprintf(out, n, "%d", g_set.bright);
    else if (!strcmp(key, "pic_contrast")) snprintf(out, n, "%d", g_set.contrast);
    else if (!strcmp(key, "pic_sat"))   snprintf(out, n, "%d", g_set.sat);
    else if (!strcmp(key, "pic_exposure")) snprintf(out, n, "%d", g_set.exposure);
    else if (!strcmp(key, "pic_wb"))    { wordAt(kWbs, g_set.wb, w, sizeof(w)); snprintf(out, n, "%s", w); }
    else if (!strcmp(key, "pic_effect")){ wordAt(kEffects, g_set.effect, w, sizeof(w)); snprintf(out, n, "%s", w); }
    else if (!strcmp(key, "pic_levels"))snprintf(out, n, "%s", g_set.levels ? "yes" : "no");
    else if (!strcmp(key, "pic_gamma")) { wordAt(campic::kGammas, g_set.gamma, w, sizeof(w)); snprintf(out, n, "%s", w); }
}

// pinShares: the flash pin may share its GPIO with the lights' drive light,
// and only in pixel mode, when the flash is that light going white (plugin.h).
// With anything else, and in pin mode with the drive light too, it is held.
bool pinShares(const char* key, const char* otherPlugin, const char* otherKey,
               const char* (*onPage)(const char* key)) {
    if (strcmp(key, "flash_pin") || strcmp(otherPlugin, "lights") || strcmp(otherKey, "drive_pin")) return false;
    const char* mode = onPage ? onPage("flash_mode") : nullptr;
    if (mode) return wordIndex(kFlashes, mode) == FLASH_PIXEL;
    return g_set.flash == FLASH_PIXEL;
}

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------
bool start(Bbs& bbs) {
    (void)bbs;
    g_index = plugins::indexOf(kName);
    g_set = Settings();
    plugins::forEachKey(g_index, readKey, nullptr);
    g_set.tlEvery = static_cast<uint32_t>(g_set.tlMin) * 60u + g_set.tlSec;
    if (g_set.tlEvery > camrules::kTlMax) g_set.tlEvery = camrules::kTlMax;
    if (g_set.tlEvery && g_set.tlEvery < camrules::kTlMin) g_set.tlEvery = camrules::kTlMin;
    if (!g_who) {                                        // once: a CONFIG save keeps the counts
        g_who = static_cast<Who*>(plat::camAlloc(sizeof(Who) * kWho));
        if (g_who) for (uint8_t i = 0; i < kWho; ++i) new (&g_who[i]) Who();
        g_offer = static_cast<Offer*>(plat::camAlloc(sizeof(Offer) * kSlots));
        if (g_offer) memset(g_offer, 0, sizeof(Offer) * kSlots);
    }
    sysGroup(camrules::kTlFolder, camrules::kTlPrefix, g_set.tlKeep, g_set.tlMax);
    // The flash pin idles low from the start, so a relay never clicks at boot.
    if (g_set.flash == FLASH_PIN && g_set.flashPin >= 0) plat::pinOut(g_set.flashPin, false);
    g_tlPrimed = false;
    g_running  = true;
    // What a snap will find, for the bench: the sensor needs one 32 KB block
    // of internal DMA memory. One walk of the heap, at start only.
    plat::log("camera: on, %s, largest internal DMA block %u", g_set.mark ? "watermarked" : "unmarked",
              static_cast<unsigned>(plat::camDmaLargest()));
    // A job still running from before a CONFIG save carries on; a finished
    // one is told on the next tick. The survey counts what is on the card
    // and clears a photo a power cut left half written.
    const uint8_t p = g_job.ph.load();
    if (p == PH_DONE || p == PH_FAILED) { g_job.waiting = false; finish(plat::millis()); }
    if (!jobBusy()) g_surveyWanted = true;
    return true;
}

void stop() {
    g_running = false;
    flashSet(g_job, false);                            // never left on by a plugin that stopped
}

const char* status() {
    static char line[48];
    if (jobBusy()) return "taking a photo";
    const char* sen = g_sensor.load() == SENSOR_FOUND ? plat::camSensor()
                    : g_sensor.load() == SENSOR_MISSING ? "no sensor" : "sensor not seen yet";
    if (!g_stats.known) { snprintf(line, sizeof(line), "%s, no count yet", sen); return line; }
    snprintf(line, sizeof(line), "%s, %u photos, %u timed", sen, static_cast<unsigned>(g_stats.callers),
             static_cast<unsigned>(g_stats.system));
    return line;
}

}   // namespace

// ---------------------------------------------------------------------------
// camera.h
// ---------------------------------------------------------------------------
bool camera::running() { return g_running && plat::sdBase()[0]; }
bool camera::found() { return g_sensor.load() == SENSOR_FOUND; }
bool camera::busy() {
    const uint8_t p = g_job.ph.load();
    return p != PH_IDLE && p != PH_DONE && p != PH_FAILED;
}

void camera::photosLevels(PlugLevel& see, PlugLevel& removeLevel) {
    see = g_set.photos;
    removeLevel = g_index != 0xFF ? plugins::levelFor(g_index, 2) : PlugLevel::Sysop;
}

bool camera::snapSystem(const char* folder, const char* prefix, uint16_t keepDays, uint32_t maxFiles) {
    if (!g_running || !plat::sdBase()[0] || jobBusy()) return false;
    if (!plainWord(folder, 15) || !plainWord(prefix, 7)) return false;
    if (!roomToSnap(true)) return false;
    if (g_stats.known && !g_stats.floorMet) return false;
    int g = sysGroup(folder, prefix, keepDays, maxFiles);
    struct tm t;
    if (g < 0 || !localNow(t)) return false;
    Job& j = g_job;
    j.kind = K_SYSTEM;
    j.group = static_cast<uint8_t>(g);
    if (!camrules::systemName(folder, prefix, t, j.rel, sizeof(j.rel))) return false;
    snprintf(j.handle, sizeof(j.handle), "%s", folder);
    texts(j, t, folder);
    j.node = 0xFF;
    j.waiting = false;
    return startJob(K_SYSTEM, plat::millis());
}

extern const Plugin kCameraPlugin = {
    { kName, "Camera", "1.0", 0, 0, PF_CORE | PF_SD | PF_FAST,
      PlugLevel::All, PlugLevel::Staff, PlugLevel::Sysop },
    start,
    stop,
    tick,
    nullptr,                 // onConnect
    onLogin,
    onLogoff,
    onKey,
    status,
    kCommands,
    sizeof(kCommands) / sizeof(kCommands[0]),
    kSettings,
    sizeof(kSettings) / sizeof(kSettings[0]),
    setting,
    nullptr,                 // rows
    nullptr,                 // onPresence
    nullptr,                 // onBytes
    onRename,
    nullptr,                 // listDone
    nullptr,                 // liftInput
    nullptr,                 // restoreInput
    nullptr,                 // waiting
    pinShares,
};

#endif  // BBS_HAS_CAMERA
