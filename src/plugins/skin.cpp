/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/skin.cpp
 * Module:       Plugins / panel skins (BBS_HAS_LCD boards only)
 *
 * Purpose:      A panel skin, from the card to the glass: found, read,
 *               checked, decoded and drawn, with the status skin standing
 *               in whenever anything about it is wrong. The interface is in
 *               skin.h; the pixels in skin_draw.h; the file's rules in
 *               skin_manifest.h and skin_jpeg.h.
 *
 * Design:       Everything slow is a job on the background runner
 *               (core/runner.h; "the worker" below) (Rule no. 1): reading the
 *               card, parsing, decoding the JPEG, working out the LEDs'
 *               weights, seeding the stock set and listing the skins
 *               folder. The worker builds the skin in a PSRAM block of its
 *               own, [Manifest | background | weights], and touches nothing
 *               the loop uses; the loop takes the block once the worker says
 *               done, so the panel keeps drawing the status skin while a
 *               skin loads, and a load called off costs nothing but itself.
 *               One job at a time. The loop reads the job's results only
 *               after the runner says DONE and writes its inputs only while
 *               it is IDLE; the worker the other way round.
 *
 *               On the loop: the background copied into the framebuffer 32
 *               rows a tick (a whole 480 x 320 copy at once is some 15 ms
 *               of PSRAM traffic, and a pass is meant to vanish), then the
 *               LEDs every 40 ms and the lines when the panel's figures
 *               change, each redrawn only where it changed.
 *
 *               A skin is kept loaded across the panel's stop and start (a
 *               CONFIG save restarts every plugin), and put back on the glass
 *               from its copy without touching the card again, unless the
 *               glass changed size, when it is let go and read again.
 *
 *               A skin that fails, for any reason, is not tried again until
 *               the setting changes or a card comes or goes: a board with a
 *               bad skin.txt does not read it every tick.
 *
 * Libraries:    none (libc, POSIX dirent)
 * Targets:      ESP32-S3 boards with BBS_HAS_LCD, and the Linux host build
 * See also:     SKINS.md, src/plugins/skin.h, src/plugins/panel.cpp
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

#ifdef BBS_HAS_LCD

#include "skin.h"
#include "skin_jpeg.h"
#include "lights.h"
#include "../core/bbs.h"
#include "../core/disk.h"      // opens that tell the drive light (and the host's costs)
#include "../core/runner.h"
#include "../platform/platform.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace skin {
char g_choices[kChoicesMax] = "status";
}

namespace {

using namespace skin;

constexpr uint32_t kLedMs      = 40;       // the LEDs, as the status skin's strip
constexpr uint16_t kCopyRows   = 32;       // background rows put back a tick
constexpr size_t   kWhyMax     = 96;       // a reason: two names of 24 and the words

// ---------------------------------------------------------------------------
// A loaded skin: one PSRAM block, [Manifest | background | LED weights].
// ---------------------------------------------------------------------------
struct Loaded {
    void*          block = nullptr;
    const Manifest* m    = nullptr;
    uint16_t*      bg    = nullptr;
    const uint8_t* map[kSceneLeds] = {};
    char           name[kNameMax + 1] = "";
    uint16_t       w = 0, h = 0;
};

void release(Loaded& l) {
    if (l.block) plat::psramFree(l.block);
    l = Loaded();
}

// ---------------------------------------------------------------------------
// The job: a job on the background runner (core/runner.h, 1.1.2), its
// inputs and outputs. The runner's stack (BBS_RUNNER_STACK, 8,192) holds a
// 512-byte segment, some paths and the manifest's geometry items; the load
// logs what it left spare.
// ---------------------------------------------------------------------------
struct Job {
    runner::Job          rj;             // the runner's state: IDLE, QUEUED, RUNNING, DONE
    std::atomic<bool>    cancel{ false };
    // In: set by the loop while idle.
    bool      load = false;              // read a skin (and not only list them)
    bool      seed = false;              // put the stock set on the card first
    char      name[kNameMax + 1] = "";
    uint16_t  w = 0, h = 0;
    char      base[64] = "";             // the card's mount point, "" with none
    // Out: set by the runner before DONE.
    Loaded    result;
    bool      ok = false;
    char      err[kWhyMax] = "";
    char      found[kChoicesMax] = "";   // the skins for this size, bar separated
    SeedCount seeded;
    uint32_t  ms = 0;                    // how long the job took
    uint32_t  stackFree = 0;             // the least free stack it had, bytes
};
Job g_job;

// ---------------------------------------------------------------------------
// The loop's state.
// ---------------------------------------------------------------------------
enum Phase : uint8_t { PH_STATUS, PH_COPY, PH_ACTIVE };

Phase    g_phase = PH_STATUS;
Loaded   g_cur;
Scene    g_scene;
uint16_t g_copyRow = 0;
bool     g_up      = false;              // the panel is running
char     g_want[kNameMax + 1] = "status";
uint16_t g_w = 0, g_h = 0;
char     g_why[kWhyMax] = "";
// The last try that failed: not tried again for the same name, size and card.
char     g_failName[kNameMax + 1] = "";
uint16_t g_failW = 0, g_failH = 0;
uint32_t g_failGen = 0;
// The skin on the glass has a newer copy on the card (a re-upload through
// the Skins area): read it again, and keep this one up until it is ready.
bool     g_curStale = false;
// The card: its comings and goings, and whether this one has been seeded.
bool     g_cardWas  = false;
uint32_t g_cardGen  = 1;
uint32_t g_seedGen  = 0;
bool     g_needScan = true;
Figures  g_fig;
bool     g_textOwed = false;             // every widget to draw on the next tick
// What the skin samples itself (1.2.0): traffic and callers twice a second
// for the meters and graphs, keystrokes and disk every LED frame for the
// lamps. Counters only.
constexpr uint32_t kSampleMs = 500;
uint16_t g_trafficHist[kHistory] = {};
uint8_t  g_callersHist[kHistory] = {};
Live     g_live;
uint32_t g_sampleAt = 0, g_sampleBytes = 0;
uint32_t g_rxSeen = 0, g_txSeen = 0;
plat::DiskSeen g_diskSeen;
uint32_t g_diskAt = 0;                   // millis the last storage access was seen, 0 none
uint32_t g_errAt  = 0;                   // and the last storage error
uint32_t g_ledAt = 0;
uint32_t g_traffic = 0;

bool builtIn(const char* s) {
    return !s || !*s || detail::ieq(s, kBuiltIn);
}

void setWhy(const char* fmt, const char* a, const char* b) {
    snprintf(g_why, sizeof(g_why), fmt, a, b);
}

// ---------------------------------------------------------------------------
// The worker
// ---------------------------------------------------------------------------
// The decode's one context: the file it reads and the background it fills.
struct FileRead {
    FILE*     f;
    uint16_t* bg = nullptr;
    uint16_t  w = 0, h = 0;
};

size_t fileRead(void* ctx, uint8_t* buf, size_t n) {
    FILE* f = static_cast<FileRead*>(ctx)->f;
    if (!buf) {
        const long at = ftell(f);
        if (fseek(f, static_cast<long>(n), SEEK_CUR) != 0) return 0;
        // fseek past the end succeeds; the reader must hear a short skip.
        fseek(f, 0, SEEK_END);
        const long end = ftell(f);
        const long to  = at + static_cast<long>(n) < end ? at + static_cast<long>(n) : end;
        fseek(f, to, SEEK_SET);
        return static_cast<size_t>(to - at);
    }
    plat::diskPulse(plat::DISK_CARD);
    return fread(buf, 1, n, f);
}

bool putRows(void* ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* rgb) {
    FileRead& d = *static_cast<FileRead*>(ctx);
    if (g_job.cancel.load(std::memory_order_relaxed)) return false;
    if (x + w > d.w || y + h > d.h) return false;
    for (uint16_t r = 0; r < h; ++r) {
        uint16_t* row = d.bg + static_cast<size_t>(y + r) * d.w + x;
        for (uint16_t c = 0; c < w; ++c, rgb += 3) row[c] = panelgfx::rgb(rgb[0], rgb[1], rgb[2]);
    }
    if (x + w == d.w) plat::taskSleep(1);          // a row of blocks done: the idle task's turn
    return true;
}

const char* decodeWords(int rc) {
    switch (rc) {
        case 1:  return "stopped";
        case 2:  return "the file ends early";
        case 3:
        case 4:  return "out of memory decoding it";
        default: return "damaged or a kind the decoder cannot do";
    }
}

bool jobFail(Job& j, const char* fmt, const char* a = "", const char* b = "") {
    snprintf(j.err, sizeof(j.err), fmt, a, b);
    return false;
}

// skinPath: where one of a skin's two files is. Its folder first
// (skins/<name>/skin.txt and background.jpg, the shape SKINS.md and the zip
// give), then the pair beside the folders (skins/<name>.txt and <name>.jpg,
// the shape an upload into the Skins file area takes, since a transfer
// names one file and no folder). False when neither is there.
bool skinPath(const char* base, const char* name, bool txt, char* out, size_t n) {
    struct stat st;
    snprintf(out, n, "%s/skins/%.24s/%s", base, name, txt ? "skin.txt" : "background.jpg");
    if (stat(out, &st) == 0) return true;
    snprintf(out, n, "%s/skins/%.24s.%s", base, name, txt ? "txt" : "jpg");
    return stat(out, &st) == 0;
}

// readManifest: skin.txt whole into buf (kFileMax + 1 bytes, so an
// oversized file is seen as one), n its length. False on a read error: a
// short read would otherwise parse as a shorter, different skin.
bool readManifest(const char* path, char* buf, size_t& n) {
    FILE* f = disk::open(path, "rb");
    if (!f) return false;
    n = fread(buf, 1, kFileMax + 1, f);
    const bool bad = ferror(f) != 0;
    fclose(f);
    if (bad) plat::diskPulse(plat::DISK_ERROR);
    return !bad;
}

// loadSkin: the named skin, whole, into j.result.
bool loadSkin(Job& j) {
    if (!j.base[0]) return jobFail(j, "no SD card");
    char path[160];

    // skin.txt, read whole (4 KB at most) and parsed.
    if (!skinPath(j.base, j.name, true, path, sizeof(path)))
        return jobFail(j, "no skins/%.24s/ or skins/%.24s.txt on the card", j.name, j.name);
    char* txt = static_cast<char*>(malloc(kFileMax + 1));
    if (!txt) return jobFail(j, "out of memory");
    size_t n = 0;
    if (!readManifest(path, txt, n)) { free(txt); return jobFail(j, "skin.txt could not be read"); }
    Manifest* m = static_cast<Manifest*>(malloc(sizeof(Manifest)));
    char why[kWhyMax];
    const bool parsed = m && parse(txt, n, *m, why, sizeof(why));
    free(txt);
    if (!parsed) {
        free(m);
        return m ? jobFail(j, "skin.txt %.70s", why) : jobFail(j, "out of memory");
    }
    if (m->w != j.w || m->h != j.h) {
        char was[16], is[16];
        snprintf(was, sizeof(was), "%ux%u", static_cast<unsigned>(m->w), static_cast<unsigned>(m->h));
        snprintf(is, sizeof(is), "%ux%u", static_cast<unsigned>(j.w), static_cast<unsigned>(j.h));
        free(m);
        return jobFail(j, "drawn for a %s panel; this one is %s", was, is);
    }

    // The block: the manifest, the background, then each LED's weights.
    const size_t head = (sizeof(Manifest) + 7u) & ~static_cast<size_t>(7u);
    const size_t bgBytes = static_cast<size_t>(j.w) * j.h * 2u;
    size_t mapBytes = 0;
    if (m->drive.on)    mapBytes += ledMapBytes(m->drive);
    if (m->activity.on) mapBytes += ledMapBytes(m->activity);
    for (uint8_t i = 0; i < m->strip; ++i) mapBytes += ledMapBytes(m->led[i]);
    for (uint8_t i = 0; i < m->nLamps; ++i) mapBytes += ledMapBytes(m->lamp[i].led);
    uint8_t* block = static_cast<uint8_t*>(plat::psramAlloc(head + bgBytes + mapBytes));
    if (!block) { free(m); return jobFail(j, "no PSRAM for the skin"); }
    memcpy(block, m, sizeof(Manifest));
    free(m);
    Loaded& r = j.result;
    r.block = block;
    r.m     = reinterpret_cast<const Manifest*>(block);
    r.bg    = reinterpret_cast<uint16_t*>(block + head);
    r.w     = j.w;
    r.h     = j.h;
    snprintf(r.name, sizeof(r.name), "%.24s", j.name);
    const Manifest& mm = *r.m;
    const Led* leds[kSceneLeds] = {};
    leds[0] = mm.drive.on ? &mm.drive : nullptr;
    leds[1] = mm.activity.on ? &mm.activity : nullptr;
    for (uint8_t i = 0; i < mm.strip; ++i) leds[2 + i] = &mm.led[i];
    for (uint8_t i = 0; i < mm.nLamps; ++i) leds[kLampBase + i] = &mm.lamp[i].led;

    // background.jpg: checked for the ROM decoder, then decoded.
    FILE* f = skinPath(j.base, j.name, false, path, sizeof(path)) ? disk::open(path, "rb") : nullptr;
    if (!f) { release(r); return jobFail(j, "no background.jpg (or %s.jpg) beside its skin.txt", j.name); }
    FileRead fr;
    fr.f = f;
    JpegInfo info;
    if (!checkJpeg(fileRead, &fr, info, why, sizeof(why))) {
        fclose(f);
        release(r);
        return jobFail(j, "background.jpg: %.62s", why);
    }
    if (info.w != j.w || info.h != j.h) {
        char was[16], is[16];
        snprintf(was, sizeof(was), "%ux%u", static_cast<unsigned>(info.w), static_cast<unsigned>(info.h));
        snprintf(is, sizeof(is), "%ux%u", static_cast<unsigned>(j.w), static_cast<unsigned>(j.h));
        fclose(f);
        release(r);
        return jobFail(j, "background.jpg is %s; the panel is %s", was, is);
    }
    rewind(f);
    fr.bg = r.bg;
    fr.w  = j.w;
    fr.h  = j.h;
    uint16_t dw = 0, dh = 0;
    const int rc = plat::jpegDecode(fileRead, putRows, &fr, dw, dh);
    fclose(f);
    if (rc != 0) {
        release(r);
        if (j.cancel.load()) return jobFail(j, "called off");
        return jobFail(j, "background.jpg: %s", decodeWords(rc));
    }
    // The decoder's own reading of the size, held to the check's: a picture
    // it reads as smaller would leave part of the block never written.
    if (dw != j.w || dh != j.h) {
        release(r);
        return jobFail(j, "background.jpg: the decoder reads another size than its header");
    }

    uint8_t* at = block + head + bgBytes;
    for (uint8_t i = 0; i < kSceneLeds; ++i) {
        if (!leds[i]) continue;
        ledWeights(*leds[i], at);
        r.map[i] = at;
        at += ledMapBytes(*leds[i]);
        if (j.cancel.load()) { release(r); return jobFail(j, "called off"); }
        plat::taskSleep(0);
    }
    return true;
}

// scanSkins: the folders under skins/ that hold a skin.txt drawn for this
// panel's size, in name order, into j.found.
void scanSkins(Job& j) {
    j.found[0] = '\0';
    if (!j.base[0]) return;
    char dir[96];
    snprintf(dir, sizeof(dir), "%s/skins", j.base);
    DIR* d = disk::dir(dir);
    if (!d) return;
    char names[kSkinsMax][kNameMax + 1];
    uint8_t n = 0;
    char* txt = static_cast<char*>(malloc(kFileMax + 1));
    Manifest* m = static_cast<Manifest*>(malloc(sizeof(Manifest)));
    for (struct dirent* e = readdir(d); e && n < kSkinsMax && txt && m; e = readdir(d)) {
        // A folder, or the .txt of a pair; the name either way.
        char name[kNameMax + 8];
        snprintf(name, sizeof(name), "%.31s", e->d_name);
        const size_t nl = strlen(name);
        if (nl > 4 && detail::ieq(name + nl - 4, ".txt")) name[nl - 4] = '\0';
        else if (strchr(name, '.')) continue;          // a pair's .jpg, or anything else
        if (!validName(name)) continue;
        bool dup = false;
        for (uint8_t i = 0; i < n; ++i) if (!strcmp(names[i], name)) dup = true;
        if (dup) continue;
        char path[160];
        size_t len = 0;
        if (!skinPath(j.base, name, true, path, sizeof(path)) || !readManifest(path, txt, len)) continue;
        char pic[160];
        if (!skinPath(j.base, name, false, pic, sizeof(pic))) continue;
        char why[kWhyMax];
        if (!parse(txt, len, *m, why, sizeof(why)) || m->w != j.w || m->h != j.h) continue;
        // In name order, as CONFIG steps through them.
        uint8_t at = n;
        while (at > 0 && strcmp(names[at - 1], name) > 0) {
            memcpy(names[at], names[at - 1], sizeof(names[0]));
            --at;
        }
        snprintf(names[at], sizeof(names[at]), "%.24s", name);
        ++n;
        plat::taskSleep(0);
    }
    closedir(d);
    free(txt);
    free(m);
    size_t len = 0;
    for (uint8_t i = 0; i < n; ++i) {
        int w = snprintf(j.found + len, sizeof(j.found) - len, "%s%s", len ? "|" : "", names[i]);
        if (w < 0 || len + static_cast<size_t>(w) >= sizeof(j.found)) break;
        len += static_cast<size_t>(w);
    }
}

void jobMain(runner::Job&) {
    Job& j = g_job;
    const uint32_t t0 = plat::millis();
    j.ok = false;
    j.err[0] = '\0';
    j.found[0] = '\0';
    j.seeded = SeedCount();
    j.result = Loaded();
    if (j.seed && j.base[0]) {
        size_t n = 0;
        const StockFile* files = stockFiles(n);
        j.seeded = seed(j.base, files, n);
    }
    scanSkins(j);
    if (j.load) j.ok = loadSkin(j);
    j.ms = plat::millis() - t0;
    j.stackFree = plat::taskStackFree();        // logged by the loop, as SYS does its own
}                                               // the runner sets DONE after this returns

// ---------------------------------------------------------------------------
// The loop's side of the job
// ---------------------------------------------------------------------------
bool needLoad() {
    if (!g_up || builtIn(g_want)) return false;
    if (g_cur.block && !g_curStale && !strcmp(g_cur.name, g_want) && g_cur.w == g_w && g_cur.h == g_h)
        return false;
    if (!strcmp(g_failName, g_want) && g_failW == g_w && g_failH == g_h && g_failGen == g_cardGen) return false;
    return true;
}

void startJob() {
    Job& j = g_job;
    j.load = needLoad();
    j.seed = g_cardWas && g_seedGen != g_cardGen;
    snprintf(j.name, sizeof(j.name), "%.24s", g_want);
    j.w = g_w;
    j.h = g_h;
    snprintf(j.base, sizeof(j.base), "%.63s", plat::sdBase());
    j.cancel.store(false);
    // A skin on the glass stays there while another loads, and is let go
    // only when the new one is ready (pollJob) or has failed. Dropping it at
    // once put the status layout on the glass for the length of the load,
    // and drawing that layout whole cost the loop some 24 ms on the MF35's
    // big glass, on the pass after every skin change.
    g_needScan = false;
    j.rj.work = jobMain;
    j.rj.name = "skin";
    if (!runner::post(j.rj)) {
        setWhy("%s%s", "the background runner would not take it", "");
        plat::log("panel: the runner would not take a skin's load; showing the status skin");
        if (j.load) {
            release(g_cur);
            g_phase = PH_STATUS;
        }
        // Recorded as a failure, so it is not tried again every pass: the
        // next want() (a CONFIG save) or a card coming or going tries again.
        snprintf(g_failName, sizeof(g_failName), "%.24s", g_want);
        g_failW = g_w;
        g_failH = g_h;
        g_failGen = g_cardGen;
        g_needScan = false;
    }
}

// reconcile: start whatever job the state wants, or call off one that is
// loading a skin nobody wants any more.
void reconcile() {
    Job& j = g_job;
    if (runner::pending(j.rj)) {
        if (j.load && (!g_up || strcmp(j.name, g_want) || j.w != g_w || j.h != g_h)) j.cancel.store(true);
        return;
    }
    if (!runner::idle(j.rj) || !g_up) return;
    if (needLoad() || g_needScan) startJob();
}

void takeChoices(const char* found) {
    size_t len = static_cast<size_t>(snprintf(g_choices, sizeof(g_choices), "%s", kBuiltIn));
    bool listed = builtIn(g_want);
    const char* p = found;
    while (*p) {
        const char* e = strchr(p, '|');
        const size_t n = e ? static_cast<size_t>(e - p) : strlen(p);
        if (n == strlen(g_want) && !strncmp(p, g_want, n)) listed = true;
        p = e ? e + 1 : p + n;
    }
    if (*found) {
        int w = snprintf(g_choices + len, sizeof(g_choices) - len, "|%s", found);
        if (w > 0 && len + static_cast<size_t>(w) < sizeof(g_choices)) len += static_cast<size_t>(w);
        else g_choices[len] = '\0';
    }
    if (!listed) {
        int w = snprintf(g_choices + len, sizeof(g_choices) - len, "|%s", g_want);
        if (w < 0 || len + static_cast<size_t>(w) >= sizeof(g_choices)) g_choices[len] = '\0';
    }
}

// pollJob: a finished job's results, taken on the loop.
void pollJob() {
    Job& j = g_job;
    if (!runner::done(j.rj)) return;
    if (j.seed) {
        g_seedGen = g_cardGen;
        const SeedCount& c = j.seeded;
        if (c.made || c.refreshed || c.failed)
            plat::log("panel: stock skins: %u put on the card, %u files refreshed, %u could not be written",
                      static_cast<unsigned>(c.made), static_cast<unsigned>(c.refreshed),
                      static_cast<unsigned>(c.failed));
    }
    takeChoices(j.found);
    if (j.load) {
        const bool current = g_up && !strcmp(j.name, g_want) && j.w == g_w && j.h == g_h && !j.cancel.load();
        if (current) g_curStale = false;          // this load answered it, either way
        if (j.ok && current) {
            release(g_cur);
            g_cur = j.result;
            g_scene.reset(g_cur.m, g_cur.bg);
            for (uint8_t i = 0; i < kSceneLeds; ++i) g_scene.map[i] = g_cur.map[i];
            g_phase   = PH_COPY;
            g_copyRow = 0;
            g_why[0]  = '\0';
            g_failName[0] = '\0';
            // The worker's least free stack, where the platform can say (the
            // host cannot): an overflow there would reboot the board.
            plat::log("panel: skin %s loaded in %u ms, worker stack %u bytes spare", g_cur.name,
                      static_cast<unsigned>(j.ms), static_cast<unsigned>(j.stackFree));
        } else {
            if (j.ok) release(j.result);             // loaded, and nobody wants it now
            if (current) {
                // The one that was on the glass meanwhile goes too: the setting
                // names another, and what it cannot show, the status layout
                // shows. A load that failed only for want of PSRAM while the old
                // skin still held its block is tried once more, without it.
                const bool held = g_cur.block != nullptr;
                release(g_cur);
                g_phase = PH_STATUS;
                if (held && !strncmp(j.err, "no PSRAM", 8)) {
                    j.result = Loaded();
                    runner::collect(j.rj);
                    return;                          // reconcile() starts it again
                }
                snprintf(g_failName, sizeof(g_failName), "%.24s", j.name);
                g_failW = j.w;
                g_failH = j.h;
                g_failGen = g_cardGen;
                snprintf(g_why, sizeof(g_why), "%.95s", j.err);    // PANEL names the skin
                plat::log("panel: skin %s: %s; showing the status skin (%u ms, stack %u spare)", j.name, j.err,
                          static_cast<unsigned>(j.ms), static_cast<unsigned>(j.stackFree));
            }
        }
    }
    j.result = Loaded();
    runner::collect(j.rj);
}

// pollCard: a card that came or went is a new chance for a skin that failed,
// and a new list for CONFIG.
void pollCard() {
    const bool card = plat::sdBase()[0] != '\0';
    if (card == g_cardWas) return;
    g_cardWas = card;
    ++g_cardGen;
    g_needScan = true;
}

// sample: the traffic rate and the callers on, into the graphs' history,
// twice a second. True when a sample was taken.
bool sample(uint32_t now) {
    if (g_sampleAt && now - g_sampleAt < kSampleMs) return false;
    Bbs& b = Bbs::instance();
    const uint32_t bytes = b.bytesIn() + b.bytesOut();
    const uint32_t ms = g_sampleAt ? now - g_sampleAt : kSampleMs;
    // 64 bits: after a gap (silent mode, the status layout) the difference
    // covers all of it, and a few MB times 1000 would wrap 32.
    const uint64_t r64 = g_sampleAt && ms ? static_cast<uint64_t>(bytes - g_sampleBytes) * 1000u / ms : 0;
    const uint32_t rate = r64 > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(r64);
    g_sampleAt = now ? now : 1;
    g_sampleBytes = bytes;
    g_live.head = static_cast<uint16_t>((g_live.head + 1) % kHistory);
    g_trafficHist[g_live.head] = static_cast<uint16_t>(rate > 65535u ? 65535u : rate);
    g_callersHist[g_live.head] = static_cast<uint8_t>(g_fig.nOnline < 0 ? 0 : g_fig.nOnline > 255 ? 255 : g_fig.nOnline);
    g_live.rate = rate;
    ++g_live.samples;
    g_live.traffic = g_trafficHist;
    g_live.callers = g_callersHist;
    return true;
}

// lineOn: somebody the panel may name is on this session (panel.cpp's
// shown(): hidden and lurking staff are not, any more than in WHO).
bool lineOn(const Session& s) {
    return s.st != SState::Free && s.loggedIn && s.visible && !s.lurk && s.user[0];
}

LedState ledState(uint32_t now) {
    LedState st;
    const Manifest& m = *g_cur.m;
    if (m.drive.on) lights::panelDrive(m.driveStyle, now, st.drive, st.drivePct);
    Bbs& b = Bbs::instance();
    const uint32_t in = b.bytesIn(), out = b.bytesOut();
    st.activity = in + out != g_traffic;
    g_traffic = in + out;
    if (m.strip) st.n = lights::panelFrame(st.strip, kStripMax, st.stripPct);
    if (m.nLamps) {
        st.rx = in != g_rxSeen;
        st.tx = out != g_txSeen;
        g_rxSeen = in;
        g_txSeen = out;
        // Storage: any access in the last 80 ms lights disk; an error blinks
        // error for ten seconds, half a second on, half off.
        const plat::DiskSeen dsk = plat::diskSeen();
        if (dsk.count[plat::DISK_CARD] != g_diskSeen.count[plat::DISK_CARD] ||
            dsk.count[plat::DISK_FLASH] != g_diskSeen.count[plat::DISK_FLASH]) g_diskAt = now ? now : 1;
        if (dsk.count[plat::DISK_ERROR] != g_diskSeen.count[plat::DISK_ERROR]) g_errAt = now ? now : 1;
        g_diskSeen = dsk;
        st.disk  = g_diskAt && now - g_diskAt < 80u;
        st.error = g_errAt && now - g_errAt < 10000u && ((now - g_errAt) / 500u) % 2u == 0;
        // Each line: on, and a key in the last 150 ms.
        struct Ctx { uint32_t on, hot; uint32_t now; } cx{ 0, 0, now };
        b.eachSession([](void* p, Session& s) {
            Ctx& x = *static_cast<Ctx*>(p);
            if (!lineOn(s)) return;
            uint32_t bit;
            if (s.role == Role::Sysop)                  bit = kSysopBit;
            else if (s.id >= 1 && s.id <= kNodeLines)  bit = 1u << (s.id - 1);
            else                                       return;
            x.on |= bit;
            if (s.lastInput && plat::since(x.now, s.lastInput) < 150u) x.hot |= bit;   // may be ahead (1.2.1)
        }, &cx);
        st.blink = (now / 250u) % 2u == 0;           // 2 Hz, the status layout's bell
        st.nodeOn  = cx.on;
        st.nodeHot = cx.hot;
        st.ring = g_fig.fRing;  st.mail = g_fig.fMail;     st.staff = g_fig.fStaff;
        st.listed = g_fig.fListed; st.closed = g_fig.fClosed; st.card = g_fig.fCard;
    }
    return st;
}

} // namespace

// ===========================================================================
// skin.h
// ===========================================================================
bool skin::validName(const char* s) {
    if (!s) return false;
    const size_t n = strlen(s);
    if (!n || n > kNameMax || builtIn(s)) return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

void skin::want(const char* name, uint16_t w, uint16_t h) {
    g_up = true;
    g_w  = w;
    g_h  = h;
    if (builtIn(name)) {
        snprintf(g_want, sizeof(g_want), "%s", kBuiltIn);
    } else if (!validName(name)) {
        plat::log("panel: skin = %.32s is not a skin's name (A-Z, 0-9, _ and -, 24 at most); showing the status skin",
                  name);
        snprintf(g_why, sizeof(g_why), "%.32s is not a skin's name", name);
        snprintf(g_want, sizeof(g_want), "%s", kBuiltIn);
    } else {
        snprintf(g_want, sizeof(g_want), "%.24s", name);
    }
    pollCard();
    g_needScan = true;                       // every start lists the card again
    g_failName[0] = '\0';                    // and tries again a skin that failed:
                                             // want() is a start, never a tick
    if (builtIn(g_want)) {
        release(g_cur);                      // the loop's alone: a job fills its own
        g_phase = PH_STATUS;
        if (validName(name) || builtIn(name)) g_why[0] = '\0';
    } else if (g_cur.block && g_cur.w == w && g_cur.h == h) {
        // The same skin: back from its copy. Another: the one loaded goes
        // back on the glass while the new one loads, and the new one takes
        // over when it is ready (startJob, pollJob).
        g_phase   = PH_COPY;
        g_copyRow = 0;
    } else {
        release(g_cur);                      // drawn for another size: no use now
        g_phase = PH_STATUS;
    }
    takeChoices("");
    reconcile();
}

void skin::stop() {
    g_up = false;
    g_phase = PH_STATUS;
    pollJob();                               // a finished result nobody wants now: let go
    reconcile();                             // calls off a load in flight
}

void skin::redraw() {
    if (g_phase == PH_ACTIVE || g_phase == PH_COPY) {
        g_phase   = PH_COPY;
        g_copyRow = 0;
    }
}

skin::Figures& skin::figures() { return g_fig; }

bool skin::tick(panelgfx::Canvas& c, panelgfx::Dirty& d, uint32_t now, bool fresh) {
    pollJob();
    pollCard();
    reconcile();
    if (g_phase == PH_STATUS || !g_cur.block) return false;
    if (c.w != g_cur.w || c.h != g_cur.h || !c.px) {         // the glass changed under it
        release(g_cur);
        g_phase = PH_STATUS;
        return false;
    }
    if (g_phase == PH_COPY) {
        // The status skin's queue is for pixels about to be overwritten.
        if (!g_copyRow) d.clear();
        const uint16_t rows = static_cast<uint16_t>(g_cur.h - g_copyRow < kCopyRows ? g_cur.h - g_copyRow : kCopyRows);
        memcpy(c.px + static_cast<size_t>(g_copyRow) * c.w, g_cur.bg + static_cast<size_t>(g_copyRow) * c.w,
               static_cast<size_t>(rows) * c.w * 2u);
        g_copyRow = static_cast<uint16_t>(g_copyRow + rows);
        if (g_copyRow < g_cur.h) return true;
        // All of it: the scene from nothing on the copy (full() without a
        // second memcpy) and the glass queued. The LEDs and the lines are
        // drawn from the next tick, each a pass of its own, so no one pass
        // carries the copy's end and the whole skin besides.
        g_scene.forget();
        g_scene.fills(c);
        d.clear();
        d.add(panelgfx::R(0, 0, c.w, c.h));
        g_textOwed = true;
        g_ledAt = now - kLedMs;
        g_phase = PH_ACTIVE;
        return true;
    }
    // The widgets: when the figures are fresh, a sample was taken, or the
    // last pass stopped at its budget. A pass that drew anything leaves the
    // LEDs to the next tick, so no one tick carries both.
    const bool sampled = sample(now);
    if (g_textOwed || fresh || sampled || g_scene.pending) {
        g_textOwed = false;
        if (g_scene.widgets(c, d, g_fig, g_live)) return true;
    }
    if (now - g_ledAt >= kLedMs) {
        g_ledAt = now;
        g_scene.leds(c, d, ledState(now));
    }
    return true;
}

bool skin::live() { return g_phase != PH_STATUS && g_cur.block; }

const char* skin::title() {
    return live() && g_cur.m ? g_cur.m->name : "";
}

void skin::uploaded(const char* file) {
    // The card's list is stale either way; and a new copy of the skin on the
    // glass, or of one that failed, is read again rather than trusted to
    // the copy in memory or to the failure on record.
    g_needScan = true;
    char name[kNameMax + 8];
    snprintf(name, sizeof(name), "%.31s", file ? file : "");
    char* dot = strrchr(name, '.');
    if (dot) *dot = '\0';
    if (!strcmp(name, g_failName)) g_failName[0] = '\0';
    // The one on the glass stays there until the new copy is read, as a
    // skin change does: letting it go at once put the status layout on the
    // glass whole, some 30 ms of the loop on the MF35, for the length of the
    // load. A load already running is called off and started again
    // (reconcile) so the new copy is the one read.
    if (g_cur.block && !strcmp(name, g_cur.name)) {
        g_curStale = true;
        if (runner::pending(g_job.rj) && g_job.load) g_job.cancel.store(true);
    }
    reconcile();
}

bool skin::copying() { return g_phase == PH_COPY && g_cur.block; }

const char* skin::choices() { return g_choices; }

const char* skin::running() {
    return g_phase == PH_STATUS || !g_cur.block ? kBuiltIn : g_cur.name;
}

const char* skin::why() {
    if (runner::pending(g_job.rj) && g_job.load) return "loading";
    return g_why;
}


#endif  // BBS_HAS_LCD
