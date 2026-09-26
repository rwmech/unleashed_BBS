// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/cameras.cpp
// Module:       Core / the board's cameras: one SNAPSHOT for all (1.2.0)
//
// Purpose:      The camera registry in photos.h: the built-in camera and any
//               camera satellites on the link register here, and the core
//               owns the verbs, SNAPSHOT and CAMERA, so a caller has one way
//               to take a picture however many cameras the board has (the
//               camsat engineer's proposal, approved 2026-09-26).
//
//               Also the callers' limits, which were the built-in camera's:
//               ONE budget across every camera (Rob), so ten an hour is ten
//               on the board. A small table of windows, from the heap once,
//               at the first camera, and kept across restarts (a CONFIG save
//               does not hand anybody a fresh allowance). An account's
//               window is keyed by its handle and follows a rename; a
//               guest's by address and by name both, so reconnecting under
//               another name, or from another address, starts nothing
//               again. A reboot forgets it (COMMANDS.md says so).
//
// Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1) and the Linux host build
// See also:     src/core/photos.h, src/plugins/camera.cpp, LINK.md
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

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <new>
#include <strings.h>

#include "bbs.h"
#include "sysconfig.h"
#include "../plugins/camera_rules.h"

namespace photos {

namespace {

constexpr uint8_t kCameras = 8;
const Camera* g_cam[kCameras] = {};
uint8_t       g_ncam = 0;

// ---------------------------------------------------------------------------
// The callers' windows
// ---------------------------------------------------------------------------
enum : uint8_t { WHO_FREE = 0, WHO_ACCOUNT, WHO_ADDR, WHO_GUESTNAME };
struct Who {
    uint8_t          kind = WHO_FREE;
    uint32_t         key  = 0;
    camrules::Window w;
};
constexpr uint8_t kWho = 24;
Who* g_who = nullptr;

uint32_t fnv(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; ++s) { h ^= static_cast<uint8_t>(tolower(static_cast<unsigned char>(*s))); h *= 16777619u; }
    return h;
}

// whoKeys: this caller's windows, one or two (a guest's address and name).
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

// ---------------------------------------------------------------------------
// The verbs
// ---------------------------------------------------------------------------
void say(Session& s, Color c, const char* text) {
    s.term.color(s.tl, c);
    s.term.text(s.tl, text);
}

// pick: a camera by number (as CAMERA lists them) or by name. Null: none.
const Camera* pick(const char* arg, size_t len) {
    if (!len) return nullptr;
    bool digits = true;
    for (size_t i = 0; i < len; ++i) if (!isdigit(static_cast<unsigned char>(arg[i]))) digits = false;
    if (digits) {
        const long n = strtol(arg, nullptr, 10);
        return n >= 1 && n <= g_ncam ? g_cam[n - 1] : nullptr;
    }
    for (uint8_t i = 0; i < g_ncam; ++i)
        if (strlen(g_cam[i]->name) == len && !strncasecmp(g_cam[i]->name, arg, len)) return g_cam[i];
    return nullptr;
}

bool isUp(const Camera* c) { return c && (!c->up || c->up(c->ctx)); }

// byDefault: CONFIG cameras' choice when it is up, else the built-in camera,
// else the first that is up, else the first there is (which says why not).
const Camera* byDefault() {
    const char* want = syscfg::get().camera;
    if (*want) {
        const Camera* c = pick(want, strlen(want));
        if (isUp(c)) return c;
    }
    for (uint8_t i = 0; i < g_ncam; ++i) if (g_cam[i]->order == 0 && isUp(g_cam[i])) return g_cam[i];
    for (uint8_t i = 0; i < g_ncam; ++i) if (isUp(g_cam[i])) return g_cam[i];
    return g_ncam ? g_cam[0] : nullptr;
}

size_t word(const char*& arg) {
    while (*arg == ' ') ++arg;
    size_t n = 0;
    while (arg[n] && arg[n] != ' ') ++n;
    return n;
}

void cmdSnapshot(Bbs& b, Session& s, const char* arg, uint32_t now) {
    const size_t n = word(arg);
    const Camera* c = n ? pick(arg, n) : byDefault();
    if (!c) {
        if (!g_ncam) {
            say(s, Color::LightRed, "This board has no camera just now.");
        } else if (s.perms) {
            say(s, Color::LightRed, "No camera by that name or number. CAMERA lists them.");
        } else {
            // CAMERA is staff's (camsat bench), so a caller is told the choices here.
            char buf[112];
            int w = snprintf(buf, sizeof(buf), "No such camera. Try:");
            for (uint8_t i = 0; i < g_ncam && w > 0 && static_cast<size_t>(w) < sizeof(buf); ++i)
                w += snprintf(buf + w, sizeof(buf) - static_cast<size_t>(w), " %u %s%s", static_cast<unsigned>(i + 1),
                              g_cam[i]->name, i + 1 < g_ncam ? "," : ".");
            say(s, Color::LightRed, buf);
        }
        b.prompt(s);
        return;
    }
    // A camera that is down still gets the call when it is the only one: its
    // own refusal says why (no card, no sensor) better than this could.
    if (!isUp(c) && g_ncam > 1) {
        char buf[80];
        snprintf(buf, sizeof(buf), "The %s camera is not taking pictures just now.", c->name);
        say(s, Color::LightRed, buf);
        b.prompt(s);
        return;
    }
    c->snap(c->ctx, b, s, now);
}

void cmdCamera(Bbs& b, Session& s, const char* arg, uint32_t now) {
    const char* rest = arg;
    const size_t n = word(rest);
    // One camera: CAMERA is that camera's own command, arguments and all,
    // as it was before there could be more than one (CAMERA SET ...).
    if (g_ncam == 1) {
        const Camera* c = g_cam[0];
        const Camera* named = n ? pick(rest, n) : nullptr;
        if (named == c) { rest += n; while (*rest == ' ') ++rest; }
        else rest = arg;
        if (c->command) { c->command(c->ctx, b, s, rest, now); return; }
    } else if (n) {
        const Camera* c = pick(rest, n);
        if (c) {
            rest += n;
            while (*rest == ' ') ++rest;
            if (c->command) { c->command(c->ctx, b, s, rest, now); return; }
        } else {
            say(s, Color::LightRed, "No camera by that name or number.");
            b.prompt(s);
            return;
        }
    }
    b.rowTitle(s, "CAMERAS");
    if (!g_ncam) b.rowText(s, Color::Grey, "This board has no camera just now.");
    const Camera* def = byDefault();
    const bool wide = b.rowWidth(s) >= 60;
    for (uint8_t i = 0; i < g_ncam; ++i) {
        const Camera* c = g_cam[i];
        char st[48] = "", line[112];
        if (c->line) c->line(c->ctx, st, sizeof(st));
        const char* state = !isUp(c) ? "down" : (c->busy && c->busy(c->ctx)) ? "busy" : "up";
        snprintf(line, sizeof(line), wide ? " %u%c %-16.16s %-5s %s" : " %u%c %-10.10s %-4s %.20s",
                 static_cast<unsigned>(i + 1), c == def ? '*' : ' ', c->name, state, st);
        b.rowText(s, isUp(c) ? Color::White : Color::Grey, line);
    }
    if (g_ncam > 1)
        b.rowText(s, Color::Grey, wide ? "* SNAPSHOT's default. SNAPSHOT n takes one; CAMERA n shows it."
                                       : "* the default. SNAPSHOT n, CAMERA n.");
    b.rowRule(s);
    b.prompt(s);
}

const Command kVerbs[] = {
    { "SNAPSHOT", "", 0, 0, "SNAPSHOT [n]", "take a photo with the board's camera", cmdSnapshot,
      Menu::Account, 45 },
    { "SNAP", "", 0, CF_HIDDEN, "", "", cmdSnapshot, Menu::Hidden, 99 },
    { "CAMERA", "", 0, CF_STAFF, "CAMERA [n]", "the cameras: photos kept, space, last", cmdCamera,
      Menu::Staff, 60 },
    { "CAM", "", 0, CF_STAFF | CF_HIDDEN, "", "", cmdCamera, Menu::Hidden, 99 },
};

}  // namespace

bool addCamera(const Camera& c) {
    bool have = false;
    for (uint8_t i = 0; i < g_ncam; ++i) if (g_cam[i] == &c) have = true;
    if (!have) {
        if (g_ncam >= kCameras) return false;
        // In order: the built-in camera, then satellites by pairing number.
        uint8_t at = g_ncam;
        while (at && g_cam[at - 1]->order > c.order) { g_cam[at] = g_cam[at - 1]; --at; }
        g_cam[at] = &c;
        ++g_ncam;
    }
    if (!g_who) {                                      // once: a CONFIG save keeps the counts
        g_who = static_cast<Who*>(malloc(sizeof(Who) * kWho));
        if (g_who) for (uint8_t i = 0; i < kWho; ++i) new (&g_who[i]) Who();
    }
    Bbs& b = Bbs::instance();
    if (!b.hasCommands(kVerbs)) b.registerCommands(kVerbs, sizeof(kVerbs) / sizeof(kVerbs[0]), 0xFF);
    return true;
}

void removeCamera(const Camera& c) {
    uint8_t k = 0;
    for (uint8_t i = 0; i < g_ncam; ++i) if (g_cam[i] != &c) g_cam[k++] = g_cam[i];
    for (uint8_t i = k; i < g_ncam; ++i) g_cam[i] = nullptr;
    g_ncam = k;
    if (!g_ncam) Bbs::instance().dropCommands(kVerbs);   // SNAPSHOT goes with the last camera
}

uint8_t cameras() { return g_ncam; }
const Camera* camera(uint8_t i) { return i < g_ncam ? g_cam[i] : nullptr; }

Budget budget(const Session& s, uint32_t now) {
    uint8_t kind[2]; uint32_t key[2];
    const uint8_t n = whoKeys(s, kind, key);
    Budget v;
    for (uint8_t i = 0; i < n; ++i) {
        Who* w = whoSlot(kind[i], key[i], now, false);
        if (!w) continue;
        const camrules::Verdict x = camrules::check(w->w, now);
        if (x.hour > v.hour) v.hour = x.hour;
        if (x.day > v.day)   v.day = x.day;
        if (!x.ok && (v.ok || x.nextAt > v.nextAt)) { v.nextAt = x.nextAt; v.byDay = x.byDay; }
        if (!x.ok) v.ok = false;
    }
    return v;
}

void spend(const Session& s, uint32_t now) {
    uint8_t kind[2]; uint32_t key[2];
    const uint8_t n = whoKeys(s, kind, key);
    for (uint8_t i = 0; i < n; ++i)
        if (Who* w = whoSlot(kind[i], key[i], now, true)) camrules::record(w->w, now);
}

void renamed(const char* oldHandle, const char* newHandle) {
    if (!g_who || !oldHandle || !newHandle) return;
    const uint32_t from = fnv(oldHandle), to = fnv(newHandle);
    for (uint8_t i = 0; i < kWho; ++i)
        if (g_who[i].kind == WHO_ACCOUNT && g_who[i].key == from) g_who[i].key = to;
}

}  // namespace photos
