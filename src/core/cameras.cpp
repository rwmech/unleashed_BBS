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
#include "bbs_util.h"
#include "clock.h"
#include "satwords.h"
#include "sysconfig.h"
#include "../plugins/link.h"   // SATS: what the link knows of a satellite
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
// The numbers (1.2.0): each camera's, kept beside the list, worked out again
// whenever a camera comes or goes (renumber). The built-in camera is 1; a
// camera that asks for a number (2 to 9) and is the first to ask gets it;
// the rest take the lowest free, from 2 (from 1 on a board built with no
// camera), in the order they were registered. The list is kept in number
// order, so SNAPSHOT's choices and CAMERA's rows read 1, 2, 3.
// ---------------------------------------------------------------------------
uint8_t g_num[kCameras] = {};

#ifdef BBS_HAS_CAMERA
constexpr uint8_t kAutoFrom = 2;
#else
constexpr uint8_t kAutoFrom = 1;
#endif

void numberAll() {
    uint16_t taken = 0;                                   // bit n: number n is somebody's
    for (uint8_t i = 0; i < g_ncam; ++i) g_num[i] = 0;
    // The built-in camera first, then the ones that asked, then the rest,
    // each by its order (the pairing), never by where it sits in the list:
    // the list is sorted by number below, and walking it would let two that
    // ask for one number swap it on every renumber (code review).
    uint8_t byOrder[kCameras];
    for (uint8_t i = 0; i < g_ncam; ++i) byOrder[i] = i;
    for (uint8_t i = 1; i < g_ncam; ++i)
        for (uint8_t k = i; k && g_cam[byOrder[k - 1]]->order > g_cam[byOrder[k]]->order; --k) {
            const uint8_t t = byOrder[k]; byOrder[k] = byOrder[k - 1]; byOrder[k - 1] = t;
        }
    for (uint8_t j = 0; j < g_ncam; ++j) {
        const uint8_t i = byOrder[j];
        if (g_cam[i]->order == 0 && !(taken & 2)) { g_num[i] = 1; taken |= 2; }
    }
    for (uint8_t j = 0; j < g_ncam; ++j) {
        const uint8_t i = byOrder[j];
        const uint8_t want = g_cam[i]->number;
        if (g_num[i] || want < 2 || want > 9 || (taken & (1u << want))) continue;
        g_num[i] = want;
        taken = static_cast<uint16_t>(taken | (1u << want));
    }
    for (uint8_t j = 0; j < g_ncam; ++j) {
        const uint8_t i = byOrder[j];
        if (g_num[i]) continue;
        for (uint8_t n = kAutoFrom; n <= 9; ++n)
            if (!(taken & (1u << n))) { g_num[i] = n; taken = static_cast<uint16_t>(taken | (1u << n)); break; }
    }
    // Number order (an insertion sort: eight at most).
    for (uint8_t i = 1; i < g_ncam; ++i) {
        const Camera* c = g_cam[i];
        const uint8_t n = g_num[i];
        uint8_t at = i;
        while (at && g_num[at - 1] > n) { g_cam[at] = g_cam[at - 1]; g_num[at] = g_num[at - 1]; --at; }
        g_cam[at] = c;
        g_num[at] = n;
    }
}

// ---------------------------------------------------------------------------
// The verbs
// ---------------------------------------------------------------------------
void say(Session& s, Color c, const char* text) {
    s.term.color(s.tl, c);
    s.term.text(s.tl, text);
}

// pick: a camera by its number or by name. Null: none.
const Camera* pick(const char* arg, size_t len) {
    if (!len) return nullptr;
    bool digits = true;
    for (size_t i = 0; i < len; ++i) if (!isdigit(static_cast<unsigned char>(arg[i]))) digits = false;
    if (digits) {
        const long n = strtol(arg, nullptr, 10);
        for (uint8_t i = 0; i < g_ncam; ++i) if (g_num[i] == n) return g_cam[i];
        return nullptr;
    }
    for (uint8_t i = 0; i < g_ncam; ++i)
        if (strlen(g_cam[i]->name) == len && !strncasecmp(g_cam[i]->name, arg, len)) return g_cam[i];
    return nullptr;
}

bool isUp(const Camera* c) { return c && (!c->up || c->up(c->ctx)); }

// byDefault: CONFIG photos' choice when it is up, else the built-in camera,
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
                w += snprintf(buf + w, sizeof(buf) - static_cast<size_t>(w), " %u %s%s", static_cast<unsigned>(g_num[i]),
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
                 static_cast<unsigned>(g_num[i]), c == def ? '*' : ' ', c->name, state, st);
        b.rowText(s, isUp(c) ? Color::White : Color::Grey, line);
    }
    if (g_ncam > 1)
        b.rowText(s, Color::Grey, wide ? "* SNAPSHOT's default. SNAPSHOT n takes one; CAMERA n shows it."
                                       : "* the default. SNAPSHOT n, CAMERA n.");
    b.rowRule(s);
    b.prompt(s);
}

// ---------------------------------------------------------------------------
// SATS (1.2.0, tty-ux-sats section 2): the satellites. A caller sees each
// one they may see photos from or take one with: its number for SNAPSHOT,
// name, type, a plain status and its last picture, and nothing of the radio,
// the keys, the other boards or the firmware. Staff see the radio too, and
// SATS n one in full. The rows that name hardware (the MAC, the key's
// fingerprint, the other boards) also need the NODES permission, the one
// that shows callers' addresses.
// ---------------------------------------------------------------------------
bool isSat(const Camera* c) { return c && c->pairing >= 0; }

bool mayList(const Session& s, const Camera* c) {
    if (s.perms) return true;
    if (!c->levels) return true;
    PlugLevel see = PlugLevel::All, snap = PlugLevel::All;
    c->levels(c->ctx, see, snap);
    return plugins::mayUse(s, see) || plugins::mayUse(s, snap);
}

CamFacts factsOf(const Camera* c) {
    CamFacts f;
    if (!c->facts || !c->facts(c->ctx, f)) f.state = isUp(c) ? CST_AWAKE : CST_NOANSWER;
    if (!isUp(c) && f.state == CST_AWAKE) f.state = CST_NOANSWER;
    return f;
}

const char* stateWord(uint8_t st, bool wide) {
    switch (st) {
        case CST_AWAKE:  return "awake";
        case CST_ASLEEP: return "asleep";
        case CST_BUSY:   return "busy";
        default:         return wide ? "not answering" : "no answer";
    }
}

// lastText: a picture's time. 40: "14:32" today, "25 Sep" before; 80:
// "today 14:32", "25 Sep 18:02". "-" for none, or with no clock to say.
void lastText(uint32_t at, bool wide, char* out, size_t n) {
    if (!at || !clk::valid()) { snprintf(out, n, "-"); return; }
    const uint32_t now = clk::epoch();
    char day[12], today[12];
    clk::fmtEpoch(day, sizeof(day), "%Y%m%d", at);
    clk::fmtEpoch(today, sizeof(today), "%Y%m%d", now);
    const bool same = !strcmp(day, today);
    if (wide) {
        char t[8], d[8];
        clk::fmtEpoch(t, sizeof(t), "%H:%M", at);
        clk::fmtEpoch(d, sizeof(d), "%d %b", at);
        snprintf(out, n, same ? "today %s" : "%s %s", same ? t : d, t);
    } else {
        clk::fmtEpoch(out, n, same ? "%H:%M" : "%d %b", at);
    }
}

// upText: "42m", "3h12m", "12d"; "-" unknown.
void upText(uint32_t secs, char* out, size_t n) {
    if (!secs) snprintf(out, n, "-");
    else if (secs < 3600) snprintf(out, n, "%um", static_cast<unsigned>(secs / 60));
    else if (secs < 86400) snprintf(out, n, "%uh%02um", static_cast<unsigned>(secs / 3600), static_cast<unsigned>(secs / 60 % 60));
    else snprintf(out, n, "%ud", static_cast<unsigned>(secs / 86400));
}

// satsDetail: staff, SATS n: one satellite in full, one column at every
// width (the paged list takes a long one).
// rowCut: a row cut at the screen's width (rowText does not cut a list's).
void rowCut(Bbs& b, Session& s, Color c, char* line) {
    const uint8_t w = b.rowWidth(s);
    if (strlen(line) > w) line[w] = '\0';
    b.rowText(s, c, line);
}

void satsDetail(Bbs& b, Session& s, uint8_t idx) {
    const Camera* c = g_cam[idx];
    const CamFacts f = factsOf(c);
    const bool wide = b.rowWidth(s) >= 60;
    const bool hw = bbsu::can(s, PERM_NODES);
    char title[48], right[24], line[112], a[24], bb[24];
    snprintf(title, sizeof(title), "Satellite %u: %s", static_cast<unsigned>(g_num[idx]), c->name);
    snprintf(right, sizeof(right), "pairing %d", static_cast<int>(c->pairing) + 1);
    b.rowTitle(s, title, right);
    linkp::SatInfo li;
    const bool haveLink = linkp::satInfo(static_cast<uint8_t>(c->pairing), li);
    b.rowText(s, Color::Cyan, "-- radio");
    upText(f.uptime, a, sizeof(a));
    snprintf(line, sizeof(line), "Status    %s%s%s", stateWord(f.state, true), f.uptime ? ", up " : "", f.uptime ? a : "");
    rowCut(b, s, Color::White, line);
    if (haveLink) {
        snprintf(line, sizeof(line), "Signal    %d dBm here, %d there", li.rssi, li.farRssi);
        rowCut(b, s, Color::White, line);
        snprintf(line, sizeof(line), "Channel   %u%s", li.chan, li.up ? ", the router's" : " when last heard");
        rowCut(b, s, Color::White, line);
        snprintf(line, sizeof(line), "Rate      %s", li.slow ? "1 Mbps, fallen back" : "24 Mbps");
        rowCut(b, s, Color::White, line);
        if (hw) {
            snprintf(line, sizeof(line), "MAC       %02X:%02X:%02X:%02X:%02X:%02X", li.mac.b[0], li.mac.b[1],
                     li.mac.b[2], li.mac.b[3], li.mac.b[4], li.mac.b[5]);
            rowCut(b, s, Color::White, line);
        }
        bbsu::fmtCommas(li.rx, a, sizeof(a));
        bbsu::fmtCommas(li.tx, bb, sizeof(bb));
        snprintf(line, sizeof(line), "Frames    in %s, out %s", a, bb);
        rowCut(b, s, Color::White, line);
        snprintf(line, sizeof(line), "Lost      %u retried, %u dropped", static_cast<unsigned>(li.retries),
                 static_cast<unsigned>(li.drops));
        rowCut(b, s, Color::White, line);
    } else {
        b.rowText(s, Color::Grey, "Link      off: CONFIG link.");
    }
    b.rowText(s, Color::Cyan, "-- camera");
    snprintf(line, sizeof(line), "Sensor    %s", f.sensor[0] ? f.sensor : "not said yet");
    rowCut(b, s, Color::White, line);
    if (f.fw[0]) {
        snprintf(line, sizeof(line), "Firmware  %s", f.fw);
        rowCut(b, s, Color::White, line);
    }
    if (f.tlMin || f.tlSec) snprintf(a, sizeof(a), "%u:%02u", f.tlMin, f.tlSec);
    else snprintf(a, sizeof(a), "off");
    snprintf(line, sizeof(line), "Schedule  %s; timelapse %s; motion %s", f.sleeps ? "sleeps" : "awake", a,
             f.motion ? "on" : "off");
    rowCut(b, s, Color::White, line);
    lastText(f.lastAt, true, a, sizeof(a));
    snprintf(line, sizeof(line), "Pictures  %u since start, last %s", static_cast<unsigned>(f.pictures), a);
    rowCut(b, s, Color::White, line);
    if (haveLink) {
        snprintf(line, sizeof(line), "-- boards: %u of %u", li.boards ? li.boards : 1u, static_cast<unsigned>(ulink::Engine::kHosts));
        rowCut(b, s, Color::Cyan, line);
        if (hw) {
            snprintf(line, sizeof(line), "Key       %s", li.fp);
            rowCut(b, s, Color::White, line);
        }
        snprintf(line, sizeof(line), "Owner     %s", li.owned == 1 ? "this board" : li.owned == 0 ? (hw ? li.owner : "another board") : "this board, as far as it knows");
        rowCut(b, s, Color::White, line);
        uint8_t shown = 0;
        for (uint8_t k = 0; k < li.nothers; ++k) {
            const ulink::SharedBoard& o = li.others[k];
            snprintf(line, sizeof(line), "%s%-16.16s %s", shown ? "          " : "Also      ", hw ? o.name : "a board",
                     (o.flags & ulink::SB_HEARD) ? "heard" : "not heard");
            rowCut(b, s, (o.flags & ulink::SB_HEARD) ? Color::White : Color::Yellow, line);
            ++shown;
        }
        for (uint8_t k = 0; k < li.nothers; ++k) {
            if (li.others[k].flags & ulink::SB_HEARD) continue;
            snprintf(line, sizeof(line), wide ? "%s is not heard: all of %s's boards must be on one Wi-Fi channel."
                                              : "%s is not heard: all of", hw ? li.others[k].name : "A board", c->name);
            rowCut(b, s, Color::Yellow, line);
            if (!wide) {
                snprintf(line, sizeof(line), "%s's boards need one channel.", c->name);
                rowCut(b, s, Color::Yellow, line);
            }
            break;
        }
    }
    b.rowRule(s);
    b.prompt(s);
}

void cmdSats(Bbs& b, Session& s, const char* arg, uint32_t now) {
    (void)now;
    const bool staff = s.perms != 0;
    const bool hw = bbsu::can(s, PERM_NODES);
    const char* rest = arg;
    const size_t n = word(rest);
    // SATS n: one satellite. Staff in full; a caller its one row.
    int only = -1;
    if (n) {
        const Camera* c = pick(rest, n);
        for (uint8_t i = 0; i < g_ncam; ++i) if (g_cam[i] == c) only = i;
        if (only < 0 || !isSat(c) || !mayList(s, c)) {
            say(s, Color::LightRed, "No satellite by that name or number. SATS lists them.");
            b.prompt(s);
            return;
        }
        if (staff) { satsDetail(b, s, static_cast<uint8_t>(only)); return; }
    }
    const uint8_t w = b.rowWidth(s);
    const bool wide = w >= 60;
    const bool widest = w >= 120;
    const Camera* def = byDefault();
    uint8_t listed = 0, up = 0, sats = 0;
    bool asleep = false;
    for (uint8_t i = 0; i < g_ncam; ++i) {
        if (!isSat(g_cam[i])) continue;
        ++sats;
        up += isUp(g_cam[i]);
    }
    char right[32], line[160], last[24], upt[12];
    if (staff) {
        snprintf(right, sizeof(right), wide ? "channel %u, %u of %u up" : "ch %u, %u of %u up",
                 static_cast<unsigned>(linkp::channel()), static_cast<unsigned>(up), static_cast<unsigned>(sats));
        b.rowTitle(s, satwords::kSatTitle, right);
        // On the wire with Wi-Fi not joined, the link cannot reach a sat:
        // say so before the list says they are not answering (1.2.0; from
        // 1.2.1 Wi-Fi joins beside the wire, so only when it could not).
        if (linkp::onWire())
            b.rowText(s, Color::Yellow, w > sizeof(satwords::kOnWire) ? satwords::kOnWire : satwords::kOnWireShort);
    } else {
        b.rowTitle(s, satwords::kSatTitle, wide ? "SNAPSHOT n takes one" : "SNAPSHOT n");
    }
    if (staff)
        b.rowText(s, Color::Cyan, widest ? " #   Name              Status          Last picture  RSSI  Mbps  Boards  Up      MAC                Key"
                                  : wide ? " #   Name              Status          Last picture  RSSI  Mbps  Boards  Up"
                                         : " #  Name        Status    RSSI Mb Up");
    else
        b.rowText(s, Color::Cyan, wide ? " #   Name              Type      Status          Last picture"
                                       : " #  Name             Status    Last pic");
    for (uint8_t i = 0; i < g_ncam; ++i) {
        const Camera* c = g_cam[i];
        if (!isSat(c) || !mayList(s, c) || (only >= 0 && only != i)) continue;
        const CamFacts f = factsOf(c);
        asleep = asleep || f.state == CST_ASLEEP;
        lastText(f.lastAt, wide, last, sizeof(last));
        const char mark = c == def && g_ncam > 1 ? '*' : ' ';
        const unsigned num = g_num[i];
        if (!staff) {
            if (wide)
                snprintf(line, sizeof(line), " %u%c  %-16.16s  %-8s  %-14s  %s", num, mark, c->name, "camera",
                         stateWord(f.state, true), last);
            else
                snprintf(line, sizeof(line), " %u%c %-16.16s %-9s %s", num, mark, c->name, stateWord(f.state, false), last);
        } else {
            linkp::SatInfo li;
            const bool haveLink = linkp::satInfo(static_cast<uint8_t>(c->pairing), li);
            char rssi[8] = "-", brd[12] = "1";
            if (haveLink && li.up) snprintf(rssi, sizeof(rssi), "%d", li.rssi);
            if (haveLink && li.boards > 1) snprintf(brd, sizeof(brd), li.owned == 1 ? "%u own" : "%u", li.boards);
            upText(f.uptime, upt, sizeof(upt));
            const unsigned mb = haveLink && li.slow ? 1u : 24u;
            if (widest && hw && haveLink)
                snprintf(line, sizeof(line),
                         " %u%c  %-16.16s  %-14s  %-12s  %4s  %4u  %-6s  %-6s  %02X:%02X:%02X:%02X:%02X:%02X  %s", num, mark,
                         c->name, stateWord(f.state, true), last, rssi, mb, brd, upt, li.mac.b[0], li.mac.b[1],
                         li.mac.b[2], li.mac.b[3], li.mac.b[4], li.mac.b[5], li.fp);
            else if (wide)
                snprintf(line, sizeof(line), " %u%c  %-16.16s  %-14s  %-12s  %4s  %4u  %-6s  %s", num, mark, c->name,
                         stateWord(f.state, true), last, rssi, mb, brd, upt);
            else
                snprintf(line, sizeof(line), " %u%c %-11.11s %-9s %4s %2u %s", num, mark, c->name,
                         stateWord(f.state, false), rssi, mb, upt);
        }
        line[w < sizeof(line) ? w : sizeof(line) - 1] = '\0';
        b.rowText(s, isUp(c) ? Color::White : Color::Grey, line);
        ++listed;
    }
    // None at all is said as none, not as "none open to you" (code review).
    if (!sats)        b.rowText(s, Color::Grey, "No satellite is paired with this board.");
    else if (!listed) b.rowText(s, Color::Grey, "No satellite here is open to you.");
    if (listed && g_ncam > 1 && only < 0) {
        if (staff) b.rowText(s, Color::Grey, wide ? "* is SNAPSHOT's default. SATS n shows one in full: radio, key, boards."
                                                  : "* SNAPSHOT's. SATS n shows one in full.");
        else       b.rowText(s, Color::Grey, wide ? "* is the one SNAPSHOT alone uses. SNAPSHOT n or SNAPSHOT name picks one."
                                                  : "* SNAPSHOT alone uses this one.");
    }
    if (asleep) b.rowText(s, Color::Grey, wide ? "Asleep: it takes its timed and motion pictures, and none on request."
                                               : "Asleep: timed and motion pictures only.");
    b.rowRule(s);
    b.prompt(s);
}

const Command kVerbs[] = {
    { "SNAPSHOT", "", 0, 0, "SNAPSHOT [n]", "take a photo; n picks the camera", cmdSnapshot,
      Menu::Account, 45 },
    { "SNAP", "", 0, CF_HIDDEN, "", "", cmdSnapshot, Menu::Hidden, 99 },
    { "CAMERA", "", 0, CF_STAFF, "CAMERA [n]", "the cameras: photos kept, space, last", cmdCamera,
      Menu::Staff, 60 },
    { "CAM", "", 0, CF_STAFF | CF_HIDDEN, "", "", cmdCamera, Menu::Hidden, 99 },
    // No shortcut (tty-ux-sats): S is free and is SATS's one letter, but SATS
    // is read once to learn the numbers and SNAPSHOT is typed every time.
    { satwords::kVerbSats, "", 0, 0, "SATS [n]", "the camera satellites", cmdSats, Menu::Account, 46 },
};

}  // namespace

bool addCamera(const Camera& c) {
    bool have = false;
    for (uint8_t i = 0; i < g_ncam; ++i) if (g_cam[i] == &c) have = true;
    if (!have) {
        if (g_ncam >= kCameras) return false;
        g_cam[g_ncam++] = &c;
    }
    numberAll();
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
    numberAll();
    if (!g_ncam) Bbs::instance().dropCommands(kVerbs);   // SNAPSHOT goes with the last camera
}

uint8_t cameras() { return g_ncam; }
const Camera* camera(uint8_t i) { return i < g_ncam ? g_cam[i] : nullptr; }

uint8_t numberOf(const Camera* c) {
    for (uint8_t i = 0; i < g_ncam; ++i) if (g_cam[i] == c) return g_num[i];
    return 0;
}

bool numberFree(uint8_t n, const Camera* self) {
    if (n < 2 || n > 9) return false;
    for (uint8_t i = 0; i < g_ncam; ++i)
        if (g_cam[i] != self && (g_cam[i]->number == n || (g_cam[i]->order == 0 && n == 1))) return false;
    return true;
}

void renumber() { numberAll(); }

Budget budget(const Session& s, uint32_t now) {
    uint8_t kind[2]; uint32_t key[2];
    const uint8_t n = whoKeys(s, kind, key);
    Budget v;
    v.perHour = syscfg::get().photosPerHour;
    v.perDay  = syscfg::get().photosPerDay;
    for (uint8_t i = 0; i < n; ++i) {
        Who* w = whoSlot(kind[i], key[i], now, false);
        if (!w) continue;
        const camrules::Verdict x = camrules::check(w->w, now, v.perHour, v.perDay);
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
