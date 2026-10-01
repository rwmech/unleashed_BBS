/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/panel_photo.h
 * Module:       Plugins / panel: a new photo on the glass (1.2.1, BBS_HAS_LCD)
 *
 * Purpose:      Rob, 2026-09-30: "Snap happens, show pic on screen for 1
 *               minute, if a new one comes in update, after 1 minute stop."
 *               A photo filed in Photos by any camera, built in or a sat
 *               (photos::lastFiled), takes the panel: the picture fitted
 *               inside the glass, never cropped and never enlarged, in a
 *               frame with a caption (the camera and when). A newer photo
 *               replaces it and restarts the minute; a minute after the last
 *               one the status panel (or the skin) comes back. A tap ends it
 *               early; a ring or a shutdown ends it and takes precedence;
 *               silent mode shows nothing. CONFIG photos says whether it
 *               shows and for which kinds (caller snaps, motion, timelapse).
 *
 *               The frame is the gallery spec's
 *               (internal/tty-ux-panel-gallery-2026-09-28.md, "The layout
 *               rule" and "The caption"), with the square glass's rule from
 *               internal/tty-ux-panel-g4848-2026-10-01.md, so a 480 x 480
 *               panel is framed by the same code. The swipe gallery, when
 *               it leaves the backlog, reuses the frame, the fit, the scaler
 *               and the decode job; only the stepping and the touch
 *               positions are its own.
 *
 * Design:       Rule no. 1. Everything slow is one job on the background
 *               runner (core/runner.h): opening the file on the card, the
 *               ROM JPEG decoder at the largest of 1/1, 1/2, 1/4 and 1/8
 *               still at least the picture's drawn size, and an area average
 *               down to that size, streamed a row of blocks at a time
 *               through a ring of 24 accumulator rows. The picture and the
 *               ring are PSRAM, the job's own until it is DONE; the ring is
 *               freed by the job, the picture by the loop once it is in the
 *               framebuffer, a second or so later. Nothing of the photo is
 *               in internal RAM but TJpgDec's 5 KB work area, for the call.
 *
 *               The loop draws the frame and copies the picture into the
 *               framebuffer 32 glass rows a tick (a pass is meant to
 *               vanish), queues nothing until the whole frame is in, then
 *               the whole glass once, sent a band a tick by the panel's own
 *               flush. While the photo is up the panel draws nothing of its
 *               own; the strip under the title shows the minute running out,
 *               one 4-row rectangle every 250 ms.
 *
 *               A newer photo while one shows: the shown one stays (and its
 *               minute is held) until the newer one is decoded, then the
 *               newer one is drawn. A newer photo while one decodes: that
 *               decode is called off and the newest is decoded (newest
 *               wins). Ownership of the job's results: the loop reads them
 *               only once the runner says DONE; a job the panel let go of
 *               while it ran (the panel stopped) frees its own picture.
 *
 *               Included by panel.cpp alone: the state below has internal
 *               linkage, one copy in that translation unit.
 *
 * Libraries:    none (libc)
 * Targets:      ESP32-S3 boards with BBS_HAS_LCD, and the Linux host build of
 *               one
 * See also:     src/plugins/panel.cpp, src/core/photos.h, src/plugins/skin.cpp
 *               (the same job pattern), src/plugins/skin_jpeg.h
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
#include "../config.h"

#ifdef BBS_HAS_LCD

#include "panel_gfx.h"
#include "panel_photo_fit.h"   // the frame, the caption and the scaler
#include "skin_jpeg.h"
#include "../core/clock.h"
#include "../core/disk.h"
#include "../core/photos.h"
#include "../core/runner.h"
#include "../core/sysconfig.h"
#include "../platform/platform.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace photoshow {

constexpr uint32_t kShowMs   = 60000;    // Rob: "show pic on screen for 1 minute"
constexpr uint32_t kStripMs  = 250;      // the minute's strip, four times a second
// A newer photo waiting holds the one on the glass past its minute, but not
// for ever: a camera that stays busy (camerasBusy) must not keep the status
// panel off the glass. Three minutes in all, then the status comes back and
// the newer one shows when it can.
constexpr uint32_t kHoldMaxMs = 3u * kShowMs;
constexpr uint16_t kCopyRows = 32;       // glass rows drawn a tick (the skin's copy)
// The accent: the site's --name violet (#b48ef0), the gallery spec's.
constexpr uint16_t kViolet   = panelgfx::rgb(0xB4, 0x8E, 0xF0);

// ===========================================================================
// The decode (the runner's)
// ===========================================================================
namespace {

// The hand-over of the job's picture when the panel lets go while the job
// runs (end()): whichever of the two comes second frees it.
enum : uint8_t { HAND_OPEN = 0, HAND_JOB_DONE, HAND_LET_GO };

struct Job {
    runner::Job       rj;
    std::atomic<bool> cancel{ false };    // a newer photo, a tap, a ring: stop
    std::atomic<uint8_t> hand{ HAND_OPEN };
    // In: set by the loop while the job is IDLE.
    char      path[176] = "";
    uint16_t  w = 0, h = 0;               // the glass
    bool      yield = true;               // step aside for other queued work (runner::waiting)
    // Out: set by the runner before DONE, read by the loop after.
    uint16_t* pic = nullptr;              // PSRAM, pw x ph native RGB565: the loop's once DONE
    Frame     f;
    uint16_t  iw = 0, ih = 0;             // the file's own size
    bool      ok = false;
    bool      yielded = false;            // stopped for another job: post it again
    char      err[48] = "";
    uint32_t  ms = 0;                     // the whole job
    uint32_t  psram = 0;                  // the most PSRAM it held at once
    uint32_t  stackFree = 0;
};
Job g_job;

// The file, read as TJpgDec asks: buf null skips (skin.cpp's reader). The
// context is the FILE* itself for the check, and the Scaler
// (panel_photo_fit.h), whose first member is the file, for the decode.
size_t readFile(void* ctx, uint8_t* buf, size_t n) {
    FILE* f = *static_cast<FILE**>(ctx);
    if (!buf) {
        const long at = ftell(f);
        if (fseek(f, 0, SEEK_END) != 0) return 0;
        const long end = ftell(f);
        const long to  = at + static_cast<long>(n) < end ? at + static_cast<long>(n) : end;
        fseek(f, to, SEEK_SET);
        return to > at ? static_cast<size_t>(to - at) : 0;
    }
    plat::diskPulse(plat::DISK_CARD);
    return fread(buf, 1, n, f);
}

const char* decodeWords(int rc) {
    switch (rc) {
        case 1:  return "called off";
        case 2:  return "the file ends early";
        case 3:
        case 4:  return "out of memory decoding it";
        default: return "damaged, or a kind it cannot draw";
    }
}

bool jobFail(Job& j, const char* why) {
    snprintf(j.err, sizeof(j.err), "%s", why);
    return false;
}

bool decode(Job& j) {
    FILE* fp = disk::open(j.path, "rb");
    if (!fp) return jobFail(j, "gone from the card");
    skin::JpegInfo info;
    char why[72] = "";
    if (!skin::checkJpeg(readFile, &fp, info, why, sizeof(why))) {
        fclose(fp);
        snprintf(j.err, sizeof(j.err), "%.47s", why);   // the checker's own words, for the bench
        return false;
    }
    j.iw = info.w;
    j.ih = info.h;
    if (!fit(j.w, j.h, info.w, info.h, j.f)) { fclose(fp); return jobFail(j, "no room on this glass"); }
    rewind(fp);
    const size_t picBytes = static_cast<size_t>(j.f.pw) * j.f.ph * 2u;
    const size_t accBytes = static_cast<size_t>(j.f.pw) * kAccRows * 4u * sizeof(uint16_t);
    uint16_t* pic = static_cast<uint16_t*>(plat::psramAlloc(picBytes));
    uint16_t* acc = pic ? static_cast<uint16_t*>(plat::psramAlloc(accBytes)) : nullptr;
    if (!pic || !acc) {
        fclose(fp);
        plat::psramFree(acc);
        plat::psramFree(pic);
        return jobFail(j, "no PSRAM for the picture");
    }
    j.psram = static_cast<uint32_t>(picBytes + accBytes);
    memset(acc, 0, accBytes);
    Scaler s{ fp, pic, acc, j.f.pw, j.f.ph, static_cast<uint16_t>(info.w >> j.f.scale),
              static_cast<uint16_t>(info.h >> j.f.scale), 0, &j.cancel, j.yield, false,
              runner::waiting, [] { plat::taskSleep(1); } };   // the idle task's turn, a row of blocks
    uint16_t dw = 0, dh = 0;
    const int rc = plat::jpegDecode(readFile, putBlock, &s, dw, dh, j.f.scale);
    fclose(fp);
    if (rc == 0) finish(s);                                // the rows the last blocks left open
    plat::psramFree(acc);
    if (rc != 0) {
        plat::psramFree(pic);
        j.yielded = s.yielded && !j.cancel.load();
        return jobFail(j, j.cancel.load() ? "called off" : j.yielded ? "stepped aside" : decodeWords(rc));
    }
    j.pic = pic;
    return true;
}

void jobMain(runner::Job&) {
    Job& j = g_job;
    const uint32_t t0 = plat::millis();
    j.ok = false;
    j.yielded = false;
    j.err[0] = '\0';
    j.pic = nullptr;
    j.psram = 0;
    j.ok = decode(j);
    j.ms = plat::since(plat::millis(), t0);
    j.stackFree = plat::taskStackFree();
    // Last: the results are final. If the panel let go while this ran
    // (end()), nobody will take the picture, so it is freed here; if it lets
    // go after this, end() frees it. Whichever comes second, never both.
    if (j.hand.exchange(HAND_JOB_DONE) == HAND_LET_GO && j.pic) {
        plat::psramFree(j.pic);
        j.pic = nullptr;
        j.ok = false;
    }
}                                                  // the runner sets DONE after this returns

// ===========================================================================
// The loop's side
// ===========================================================================
enum Phase : uint8_t { PH_OFF, PH_COPY, PH_SHOW };

using HiddenFn = bool (*)(const char* handle);

struct Want {
    char    path[sizeof(Job::path)] = "";
    Caption cap;
};

Phase     g_phase   = PH_OFF;
uint16_t  g_w = 0, g_h = 0;               // the glass
HiddenFn  g_hidden  = nullptr;
bool      g_up      = false;             // the panel is running
uint16_t  g_seen    = 0;                 // photos::filedSerial as last looked at
bool      g_seenInit = false;
Want      g_want;                        // the newest photo, not yet posted
bool      g_haveWant = false;
Caption   g_load;                        // the photo the job is decoding
Caption   g_cur;                         // the photo on the glass
Frame     g_frame;
uint16_t* g_pic     = nullptr;           // taken from the job, going into the framebuffer
uint16_t  g_row     = 0;                 // the next glass row of the copy
uint32_t  g_shownAt = 0;                 // the minute's start: the photo whole on the glass
uint32_t  g_stripAt = 0;
int16_t   g_stripW  = -1;
bool      g_started = false;             // this tick took the glass from the status panel
uint32_t  g_shows   = 0, g_fails = 0;    // since boot, for PANEL
char      g_last[64] = "";               // the last decode, or why it failed, for PANEL
bool      g_postSaid = false;            // the runner refused a post: said once
bool      g_requeue = false;             // a decode called off for a camera's work: post it again
// Steps aside a photo's decode may take for other runner work before it
// runs to the end regardless: a busy board must not starve the show.
constexpr uint8_t kYieldsMax = 4;
uint8_t   g_yields  = 0;

void freePic() {
    if (g_pic) plat::psramFree(g_pic);
    g_pic = nullptr;
}

// dropJob: a finished job collected and its picture freed: nobody wants it now.
void dropJob() {
    if (!runner::done(g_job.rj)) return;
    if (g_job.pic) plat::psramFree(g_job.pic);
    g_job.pic = nullptr;
    runner::collect(g_job.rj);
}

// stopAll: off the glass, nothing waiting, a decode in flight called off.
void stopAll() {
    g_phase = PH_OFF;
    g_haveWant = false;
    g_requeue = false;
    freePic();
    if (runner::pending(g_job.rj)) g_job.cancel.store(true);
    dropJob();
}

// camerasBusy: some camera is taking a picture or bringing one in. The
// runner is the camera's worker and a camera sat's sink (its fragments are
// opened and filed in the runner's slices), and a decode in the way of
// either holds a picture up or lets the radio's ring overflow. So a decode
// is not posted while one is busy, and one running is called off and
// posted again after (photos.cpp holds its prune back the same way).
bool camerasBusy() {
    for (uint8_t i = 0; i < photos::cameras(); ++i) {
        const photos::Camera* c = photos::camera(i);
        if (c && c->busy && c->busy(c->ctx)) return true;
    }
    return false;
}

// The sysop's switch is `photos_show` (SysConfig::photosShow, CONFIG photos'
// "New photos on panel"; Rob, 2026-10-01: showing on the panel is something
// the sysop can switch off). It is read here, in wanted() and in tick(), at
// every photo and every pass, never cached. The 1.2.2 hamburger menu's Show
// snaps / Hide snaps toggle flips this same key (written to system.cfg like a
// CONFIG save), not a key of its own, so the menu and CONFIG cannot disagree.
//
// kindOn: a caption's kind still one CONFIG photos shows (asked again when
// its decode is done, so a kind switched off meanwhile is not shown).
bool kindOn(uint8_t kind) {
    const SysConfig& c = syscfg::get();
    return c.photosShow && (kind == KIND_TIMELAPSE ? c.photosShowTl
                          : kind == KIND_MOTION    ? c.photosShowMotion : c.photosShowSnaps);
}

bool wanted(uint8_t kind) {
    const SysConfig& c = syscfg::get();
    if (!c.photosShow) return false;
    switch (kind) {
        case photos::FILED_SNAP:      return c.photosShowSnaps;
        case photos::FILED_MOTION:    return c.photosShowMotion;
        case photos::FILED_TIMELAPSE: return c.photosShowTl;
        default:                      return false;
    }
}

// cameraOf: the camera that filed it, from the registry (the loop's): by the
// name the filing gave, else the built-in camera for its temporary name,
// else a sat: the one busy now (from its ask to its last fragment), else the
// only one. Null when it cannot be told.
const photos::Camera* cameraOf(const photos::Filed& f) {
    const photos::Camera* only = nullptr;
    uint8_t sats = 0;
    for (uint8_t i = 0; i < photos::cameras(); ++i) {
        const photos::Camera* c = photos::camera(i);
        if (!c) continue;
        if (f.camera[0]) {
            if (c->name && !strcmp(c->name, f.camera)) return c;
            continue;
        }
        if (f.builtIn) {
            if (c->order == 0) return c;
            continue;
        }
        if (c->pairing < 0) continue;
        if (c->busy && c->busy(c->ctx)) return c;
        only = c;
        ++sats;
    }
    return sats == 1 ? only : nullptr;
}

void makeWant(const photos::Filed& f) {
    Want& w = g_want;
    char d[128];
    if (!photos::dir(d, sizeof(d))) return;            // the card went
    char path[sizeof(w.path)];
    const int n = snprintf(path, sizeof(path), "%s/%s", d, f.rel);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(path)) return;   // never a truncated path waiting
    memcpy(w.path, path, sizeof(w.path));
    g_yields = 0;                                       // a new photo has its own steps aside
    Caption& c = w.cap;
    c = Caption();
    c.kind = f.kind == photos::FILED_TIMELAPSE ? KIND_TIMELAPSE : f.kind == photos::FILED_MOTION ? KIND_MOTION
                                                                                                  : KIND_SNAP;
    const photos::Camera* cam = cameraOf(f);
    const uint8_t no = cam ? photos::numberOf(cam) : 0;
    if (no) snprintf(c.num, sizeof(c.num), "%u", static_cast<unsigned>(no));
    const char* name = cam && cam->name ? cam->name : f.camera;
    if (name && *name) snprintf(c.name, sizeof(c.name), "%.16s", name);
    else snprintf(c.name, sizeof(c.name), "%s",
                  c.kind == KIND_TIMELAPSE ? "timelapse" : c.kind == KIND_MOTION ? "motion" : "snap");
    // The snapper, except a caller hiding or lurking right now: the glass
    // would be announcing that they are on (the gallery spec's one exception).
    if (c.kind == KIND_SNAP && f.who[0] && !(g_hidden && g_hidden(f.who)))
        snprintf(c.who, sizeof(c.who), "%s", f.who);
    uint32_t today = 0;
    if (clk::valid()) {
        char ymd[12];
        clk::fmt(ymd, sizeof(ymd), "%Y%m%d");
        today = static_cast<uint32_t>(strtoul(ymd, nullptr, 10));
    }
    whenText(f.rel, today, c.when, sizeof(c.when));
    g_haveWant = true;
}

void post() {
    Job& j = g_job;
    static_assert(sizeof(j.path) == sizeof(g_want.path), "one path size");
    memcpy(j.path, g_want.path, sizeof(j.path));        // same size, always terminated
    j.w = g_w;
    j.h = g_h;
    j.cancel.store(false);
    j.hand.store(HAND_OPEN);
    j.yield = g_yields < kYieldsMax;
    j.pic = nullptr;
    j.rj.work = jobMain;
    j.rj.name = "photo show";
    g_load = g_want.cap;
    g_haveWant = false;
    if (!runner::post(j.rj)) {
        ++g_fails;
        snprintf(g_last, sizeof(g_last), "the background runner would not take it");
        if (!g_postSaid) plat::log("panel: the runner would not take a photo's decode; not shown");
        g_postSaid = true;
    }
}

// take: a finished decode. Into the copy when it is still wanted; freed when
// it is not (called off, the glass changed, the setting turned off).
void take() {
    Job& j = g_job;
    if (!runner::done(j.rj)) return;
    uint16_t* pic = j.pic;
    j.pic = nullptr;
    const bool keep = pic && j.ok && !j.cancel.load() && j.w == g_w && j.h == g_h && g_up &&
                      kindOn(g_load.kind);
    if (j.ok) {
        snprintf(g_last, sizeof(g_last), "%ux%u at 1/%u, %u ms", static_cast<unsigned>(j.iw),
                 static_cast<unsigned>(j.ih), 1u << j.f.scale, static_cast<unsigned>(j.ms));
        plat::log("panel: photo %ux%u decoded at 1/%u into %ux%u in %u ms, %u bytes of PSRAM, stack %u spare",
                  static_cast<unsigned>(j.iw), static_cast<unsigned>(j.ih), 1u << j.f.scale,
                  static_cast<unsigned>(j.f.pw), static_cast<unsigned>(j.f.ph), static_cast<unsigned>(j.ms),
                  static_cast<unsigned>(j.psram), static_cast<unsigned>(j.stackFree));
    } else if (!j.cancel.load() && !j.yielded) {
        ++g_fails;
        snprintf(g_last, sizeof(g_last), "not shown: %.36s", j.err);
        plat::log("panel: a new photo was not shown: %s", j.err);
    }
    // Called off only to let a camera's work by: the same photo again, once
    // the cameras are quiet, unless a newer one has come meanwhile.
    const bool again = (g_requeue && j.cancel.load()) || (j.yielded && !j.cancel.load());
    if (j.yielded) ++g_yields;
    if (again && !g_haveWant && g_up) {
        memcpy(g_want.path, j.path, sizeof(g_want.path));
        g_want.cap = g_load;
        g_haveWant = true;
    }
    g_requeue = false;
    runner::collect(j.rj);
    if (!keep) {
        if (pic) plat::psramFree(pic);
        return;
    }
    freePic();                                     // never two: a copy is not running when a job is posted
    g_started = g_phase == PH_OFF;
    g_pic   = pic;
    g_frame = j.f;
    g_cur   = g_load;
    g_phase = PH_COPY;
    g_row   = 0;
    ++g_shows;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void drawTitle(panelgfx::Canvas& c) {
    using namespace panelgfx;
    const Rect& t = g_frame.title;
    fill(c, t, tok::kRule);
    const char* set = g_cur.kind == KIND_TIMELAPSE ? "TIMELAPSE" : "PHOTOS";
    const int x = 4 + text(c, 4, t.y + 2, set, kViolet, tok::kRule, false, t.w - 8);
    text(c, x, t.y + 2, "  NEW", tok::kInk, tok::kRule, false, t.w - 4 - x);
}

void drawStrip(panelgfx::Canvas& c, int fillW) {
    using namespace panelgfx;
    const Rect& s = g_frame.strip;
    fill(c, s, tok::kBg);
    fill(c, R(s.x, s.y, fillW, s.h), kViolet);
}

void drawCaption(panelgfx::Canvas& c) {
    using namespace panelgfx;
    const Frame& f = g_frame;
    const bool two = !empty(f.cap2);
    Caption t = g_cur;
    shrink(t, (f.cap1.w - 20) / kSmallW, two);
    const Rect& r = f.cap1;
    fill(c, r, tok::kRule);
    const int ty = r.y + (r.h - kSmallH) / 2;
    const uint16_t ic = t.kind == KIND_MOTION ? tok::kWarm : t.kind == KIND_TIMELAPSE ? tok::kInk : tok::kDial;
    icon(c, r.x, r.y + (r.h - 16) / 2, t.kind == KIND_TIMELAPSE ? kIconClock : kIconCamera, ic);
    const int end = r.x + r.w;
    int x = r.x + 20;
    if (t.num[0]) x += text(c, x, ty, t.num, tok::kDial, tok::kRule, false, end - x) + kSmallW;
    x += text(c, x, ty, t.name, tok::kInk, tok::kRule, false, end - x);
    if (t.who[0]) x += 2 * kSmallW + text(c, x + 2 * kSmallW, ty, t.who, tok::kInk, tok::kRule, false,
                                          end - x - 2 * kSmallW);
    if (!t.when[0]) return;
    if (two) {
        fill(c, f.cap2, tok::kRule);
        text(c, f.cap2.x, f.cap2.y + (f.cap2.h - kSmallH) / 2, t.when, tok::kYellow, tok::kRule, false, f.cap2.w);
    } else {
        const int ww = textWidth(t.when, false);
        text(c, end - ww, ty, t.when, tok::kYellow, tok::kRule, false, ww);
    }
}

// copyTick: kCopyRows glass rows of the frame: the mat, the keyline and the
// picture's rows. Nothing is queued until the last band: the glass keeps
// whatever it showed until the whole frame is there to send.
void copyTick(panelgfx::Canvas& c, panelgfx::Dirty& d, uint32_t now) {
    using namespace panelgfx;
    const Frame& f = g_frame;
    if (!g_row) d.clear();                          // what was queued is about to be drawn over
    const uint16_t rows = static_cast<uint16_t>(c.h - g_row < kCopyRows ? c.h - g_row : kCopyRows);
    const Rect band = R(0, g_row, c.w, rows);
    fill(c, band, tok::kRule);
    const Rect k = clip(f.key, c.w, c.h);
    for (int y = band.y; y < band.y + band.h; ++y) {
        if (y < k.y || y >= k.y + k.h) continue;
        uint16_t* line = c.px + static_cast<size_t>(y) * c.w;
        const int py = y - f.pic.y;
        if (py < 0 || py >= f.ph) {                 // the keyline's top or bottom
            for (int x = k.x; x < k.x + k.w; ++x) line[x] = tok::kBg;
            continue;
        }
        line[k.x] = tok::kBg;
        line[k.x + k.w - 1] = tok::kBg;
        memcpy(line + f.pic.x, g_pic + static_cast<size_t>(py) * f.pw, static_cast<size_t>(f.pw) * 2u);
    }
    g_row = static_cast<uint16_t>(g_row + rows);
    if (g_row < c.h) return;
    // All of it: the words on top, the picture let go, the glass queued.
    drawTitle(c);
    drawStrip(c, c.w);
    drawCaption(c);
    freePic();
    d.clear();
    d.add(R(0, 0, c.w, c.h));
    g_phase   = PH_SHOW;
    g_shownAt = now ? now : 1;
    g_stripAt = now;
    g_stripW  = static_cast<int16_t>(c.w);
}

// stripTick: the minute running out, as the violet fill shrinking to the
// left, redrawn when its width moves.
void stripTick(panelgfx::Canvas& c, panelgfx::Dirty& d, uint32_t now) {
    if (plat::since(now, g_stripAt) < kStripMs) return;
    g_stripAt = now;
    const uint32_t gone = plat::since(now, g_shownAt);
    const uint32_t left = gone >= kShowMs ? 0 : kShowMs - gone;
    const int16_t w = static_cast<int16_t>(static_cast<uint32_t>(c.w) * left / kShowMs);
    if (w == g_stripW) return;
    g_stripW = w;
    drawStrip(c, w);
    d.add(g_frame.strip);
}

// ===========================================================================
// What the panel calls (still in the unnamed namespace: one translation unit)
// ===========================================================================
// begin: at the panel's start, the glass's size; hidden says whether a
// handle is a caller hiding or lurking now (panel.cpp's hiding). A photo
// filed before the first start of the boot is not shown.
inline void begin(uint16_t w, uint16_t h, HiddenFn hidden) {
    g_w = w;
    g_h = h;
    g_hidden = hidden;
    g_up = true;
    g_phase = PH_OFF;
    freePic();
    dropJob();
    if (!g_seenInit) {
        g_seenInit = true;
        g_seen = photos::filedSerial();
    }
}

// end: at the panel's stop (a CONFIG save is a stop and a start): off the
// glass, and every PSRAM block let go. A decode still running frees its own
// picture when it ends (the hand-over), so nothing waits for a tick that,
// with the panel switched off, may never come; one that finished its work
// but is not yet DONE has its picture freed here.
inline void end() {
    g_up = false;
    stopAll();
    // Unconditional: a job that finished its work between stopAll's look
    // and this one, and is DONE already, is caught too (take() and dropJob
    // null the picture they free, so a stale JOB_DONE frees nothing).
    if (g_job.hand.exchange(HAND_LET_GO) == HAND_JOB_DONE) {
        if (g_job.pic) plat::psramFree(g_job.pic);
        g_job.pic = nullptr;
    }
    dropJob();
}

// quiet: silent mode, every tick of it: nothing shows, and a photo filed
// meanwhile is not saved up for when it ends.
inline void quiet() {
    stopAll();
    g_seen = photos::filedSerial();
    g_seenInit = true;
}

// dismiss: a tap on the glass ends the show.
inline void dismiss() { stopAll(); }

inline bool showing() { return g_phase != PH_OFF; }
inline bool copying() { return g_phase == PH_COPY; }
inline bool started() { return g_started; }

// tick: every panel tick while it draws (not silent). blocked: a ring or a
// shutdown, which take the glass. True when the photo has the glass this
// pass and the panel draws nothing of its own; false when the status panel
// (or the skin) draws, and must be drawn whole on the first false after a
// true.
inline bool tick(panelgfx::Canvas& c, panelgfx::Dirty& d, uint32_t now, bool blocked) {
    g_started = false;
    take();
    const uint16_t n = photos::filedSerial();
    if (n != g_seen) {
        photos::Filed f;
        photos::lastFiled(f);
        g_seen = f.serial;
        if (!blocked && wanted(f.kind)) {
            makeWant(f);
            if (g_haveWant && runner::pending(g_job.rj)) g_job.cancel.store(true);   // newest wins
        }
    }
    // photos_show off (CONFIG photos, or the 1.2.2 hamburger's Hide snaps,
    // which flips the same key) ends a show at once.
    if (blocked || !syscfg::get().photosShow || !c.px || c.w != g_w || c.h != g_h) {
        stopAll();
        return false;
    }
    // The next decode: once the last is collected, never while a copy is
    // running (the job's picture and the copy's are never both held), and
    // never in a camera's way (camerasBusy); one in its way is called off
    // and posted again.
    const bool cams = camerasBusy();
    if (cams && runner::pending(g_job.rj) && !g_job.cancel.load()) {
        g_job.cancel.store(true);
        g_requeue = true;
    }
    if (g_haveWant && !cams && runner::idle(g_job.rj) && g_phase != PH_COPY) post();
    if (g_phase == PH_OFF) return false;
    if (g_phase == PH_COPY) {
        copyTick(c, d, now);
        return true;
    }
    // A newer photo decoding holds the minute: the panel never goes back to
    // the status for the moment between two photos.
    const bool newer = g_haveWant || runner::pending(g_job.rj);
    const uint32_t up = plat::since(now, g_shownAt);
    if ((!newer && up >= kShowMs) || up >= kHoldMaxMs) {
        g_phase = PH_OFF;
        return false;
    }
    stripTick(c, d, now);
    return true;
}

// status: PANEL's line about it: whether new photos show and of which
// kinds, and what the show is doing ("Photos on, snaps motion: showing, 41
// s left"). 56 columns at most.
inline void status(char* out, size_t n) {
    const SysConfig& c = syscfg::get();
    char kinds[24] = "";
    size_t k = 0;
    auto add = [&](bool on, const char* w) {
        if (!on) return;
        const int m = snprintf(kinds + k, sizeof(kinds) - k, " %s", w);
        if (m > 0 && k + static_cast<size_t>(m) < sizeof(kinds)) k += static_cast<size_t>(m);
    };
    add(c.photosShowSnaps, "snaps");
    add(c.photosShowMotion, "motion");
    add(c.photosShowTl, "timelapse");
    if (!k) snprintf(kinds, sizeof(kinds), " none");
    if (!c.photosShow) {
        snprintf(out, n, "Photos off");
    } else if (g_phase == PH_SHOW) {
        const uint32_t gone = plat::since(plat::millis(), g_shownAt);
        const uint32_t left = gone >= kShowMs ? 0 : (kShowMs - gone + 999u) / 1000u;
        snprintf(out, n, "Photos on,%s: showing, %u s left", kinds, static_cast<unsigned>(left));
    } else if (g_phase == PH_COPY || runner::pending(g_job.rj)) {
        snprintf(out, n, "Photos on,%s: loading one", kinds);
    } else {
        snprintf(out, n, "Photos on,%s: %u shown", kinds, static_cast<unsigned>(g_shows));
    }
}

// last: the last decode ("1024x768 at 1/2, 412 ms"), or why a photo was not
// shown, "" before the first. PANEL's second line.
inline const char* last() { return g_last; }

}  // namespace

}  // namespace photoshow

#endif  // BBS_HAS_LCD
