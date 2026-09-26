/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/plugin.cpp
 * Module:       Core / plugin API
 *
 * Purpose:      Plugin registry: reads each plugin's section of system.cfg,
 *               checks what it needs against free heap and free storage,
 *               starts the ones that are switched on, registers their
 *               commands, and runs their periodic hook.
 *
 * Libraries:    none (libc)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     PLUGINS.md
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
#include "plugin.h"
#include "disk.h"              // fopen and opendir that tell the drive light (1.1.1)
#include "bbs.h"
#include "bbs_util.h"
#include "sysconfig.h"
#include "space.h"             // the kept free-space figures (1.1.2)
#include "../plugins/registry.h"
#include "../platform/platform.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

using namespace bbsu;

namespace {

struct State {
    bool        enabled  = false;
    bool        running  = false;
    PlugLevel   level[3] = { PlugLevel::All, PlugLevel::Staff, PlugLevel::Sysop };
    const char* why      = "";          // why it is not running
};

State    g_state[BBS_MAX_PLUGINS];
uint32_t g_nextTick = 0;
uint32_t g_nextFast = 0;            // PF_FAST plugins, see tick()

// section: "[plugin:name]" for this plugin
void sectionName(const char* name, char* out, size_t n) {
    snprintf(out, n, "[plugin:%s]", name);
}

// levelParse: the access ladder. plugins::levelFromText below is the public
// face of this; it lives here because the config scan above needs it before
// the plugins namespace opens.
bool levelParse(const char* v, PlugLevel& out) {
    if (ieq(v, "all"))    { out = PlugLevel::All;    return true; }
    if (ieq(v, "users"))  { out = PlugLevel::Users;  return true; }
    if (ieq(v, "staff"))  { out = PlugLevel::Staff;  return true; }
    if (ieq(v, "co2"))    { out = PlugLevel::Co2;    return true; }
    if (ieq(v, "co1"))    { out = PlugLevel::Co1;    return true; }
    if (ieq(v, "sysop"))  { out = PlugLevel::Sysop;  return true; }
    if (ieq(v, "none") || ieq(v, "nobody")) { out = PlugLevel::Nobody; return true; }
    return false;
}

const char* levelText(PlugLevel l) {
    switch (l) {
        case PlugLevel::All:    return "all";
        case PlugLevel::Users:  return "users";
        case PlugLevel::Staff:  return "staff";
        case PlugLevel::Co2:    return "co2";
        case PlugLevel::Co1:    return "co1";
        case PlugLevel::Sysop:  return "sysop";
        default:                return "none";
    }
}

char* trim(char* s) {
    while (*s == ' ' || *s == '\t') ++s;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) --n;
    s[n] = '\0';
    return s;
}

// ---------------------------------------------------------------------------
// The file once, for a whole start (1.1.2).
//
// Every plugin read system.cfg through for its own section twice as it
// started: once here for enabled and its levels, and again for its own keys
// (forEachKey, from its start). A CONFIG save starts them all again, so that
// was about two dozen reads of an 11 KB file on the loop for one save
// (internal/audit-1.1.2-2026-09-26.md, item 11). While begin() runs, the
// file is held in memory, read once, and every scan reads from that. It is
// the heap's for the length of begin(): never on the loop's steady path.
// A board that cannot spare it reads the file as before.
// ---------------------------------------------------------------------------
char*  g_cfgText = nullptr;          // the file, NUL terminated, while begin() runs
size_t g_cfgLen  = 0;

void cfgHold() {
    if (g_cfgText) return;
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", plat::userBase(), BBS_CONFIG_FILE);
    FILE* f = disk::open(path, "r");
    if (!f) return;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n > 0 && n < 64 * 1024 && fseek(f, 0, SEEK_SET) == 0) {
            g_cfgText = static_cast<char*>(malloc(static_cast<size_t>(n) + 1));
            if (g_cfgText) {
                g_cfgLen = fread(g_cfgText, 1, static_cast<size_t>(n), f);
                g_cfgText[g_cfgLen] = '\0';
            }
        }
    }
    fclose(f);
}

void cfgRelease() {
    free(g_cfgText);
    g_cfgText = nullptr;
    g_cfgLen  = 0;
}

// Lines: the file's lines, from memory when it is held, else from the file.
struct Lines {
    FILE*       f   = nullptr;
    const char* at  = nullptr;
    bool open() {
        if (g_cfgText) { at = g_cfgText; return true; }
        char path[96];
        snprintf(path, sizeof(path), "%s/%s", plat::userBase(), BBS_CONFIG_FILE);
        f = disk::open(path, "r");
        return f != nullptr;
    }
    bool next(char* line, size_t n) {
        if (f) return fgets(line, static_cast<int>(n), f) != nullptr;
        if (!at || !*at) return false;
        size_t w = 0;
        while (*at && *at != '\n') { if (w + 1 < n) line[w++] = *at; ++at; }
        if (*at == '\n') { if (w + 1 < n) line[w++] = '\n'; ++at; }
        line[w] = '\0';
        return true;
    }
    ~Lines() { if (f) fclose(f); }
};

// scan: walk system.cfg, hand every key of one plugin's section to fn.
// core = true also applies enabled/read/write/admin to the plugin's state.
void scan(uint8_t index, plugins::KeyFn fn, void* ctx, bool core) {
    const Plugin* p = plugins::at(index);
    if (!p) return;
    char want[40], line[176];
    sectionName(p->info.name, want, sizeof(want));
    Lines src;
    if (!src.open()) return;

    bool mine = false;
    while (src.next(line, sizeof(line))) {
        char* l = trim(line);
        if (!*l || *l == '#' || *l == ';') continue;
        if (*l == '[') {
            mine = ieq(l, want);
            continue;
        }
        if (!mine) continue;
        char* eq = strchr(l, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = trim(l);
        char* val = trim(eq + 1);
        char* hash = strchr(val, ';');               // trailing comment
        if (hash) { *hash = '\0'; val = trim(val); }

        if (core) {
            State& st = g_state[index];
            PlugLevel lv;
            if (ieq(key, "enabled")) {
                st.enabled = ieq(val, "yes") || ieq(val, "on") || !strcmp(val, "1");
                continue;
            }
            if (ieq(key, "read"))  { if (levelParse(val, lv)) st.level[0] = lv;
                                     else plat::log("cfg: [plugin:%s] bad read level '%s'", p->info.name, val);
                                     continue; }
            if (ieq(key, "write")) { if (levelParse(val, lv)) st.level[1] = lv;
                                     else plat::log("cfg: [plugin:%s] bad write level '%s'", p->info.name, val);
                                     continue; }
            if (ieq(key, "admin")) { if (levelParse(val, lv)) st.level[2] = lv;
                                     else plat::log("cfg: [plugin:%s] bad admin level '%s'", p->info.name, val);
                                     continue; }
        }
        if (fn) fn(ctx, key, val);
    }
}

// ensureDir: make a directory, ignoring "already there"
void ensureDir(const char* path) {
#if defined(_WIN32)
    mkdir(path);
#else
    mkdir(path, 0775);
#endif
}

} // namespace

namespace plugins {

uint8_t count() {
    return kPluginCount < BBS_MAX_PLUGINS ? kPluginCount : BBS_MAX_PLUGINS;
}

const Plugin* at(uint8_t index) {
    return index < count() ? kPlugins[index] : nullptr;
}

bool enabled(uint8_t index) { return index < count() && g_state[index].enabled; }
bool running(uint8_t index) { return index < count() && g_state[index].running; }
const char* whyNot(uint8_t index) { return index < count() ? g_state[index].why : ""; }

uint8_t indexOf(const char* name) {
    for (uint8_t i = 0; i < count(); ++i) {
        if (ieq(kPlugins[i]->info.name, name)) return i;
    }
    return 0xFF;
}

bool levelFromText(const char* v, PlugLevel& out) { return levelParse(v, out); }

const char* levelName(PlugLevel level) {
    switch (level) {
        case PlugLevel::All:    return "all";
        case PlugLevel::Users:  return "users";
        case PlugLevel::Staff:  return "staff";
        case PlugLevel::Co2:    return "co2";
        case PlugLevel::Co1:    return "co1";
        case PlugLevel::Sysop:  return "sysop";
        default:                return "nobody";
    }
}

PlugLevel levelFor(uint8_t index, uint8_t which) {
    if (index >= count() || which > 2) return PlugLevel::Nobody;
    return g_state[index].level[which];
}

// ---------------------------------------------------------------------------
// mayUse: does this caller meet a level? Staff levels compare by rank, so
// co1 also satisfies co2, and the sysop satisfies everything.
// ---------------------------------------------------------------------------
bool mayUse(const Session& s, PlugLevel level) {
    uint8_t rank = static_cast<uint8_t>(s.level);
    switch (level) {
        case PlugLevel::All:    return true;
        case PlugLevel::Users:  return s.loggedIn && !s.guest;
        case PlugLevel::Staff:  return rank > 0;
        case PlugLevel::Co2:    return rank >= static_cast<uint8_t>(Access::CoSysop2);
        case PlugLevel::Co1:    return rank >= static_cast<uint8_t>(Access::CoSysop1);
        case PlugLevel::Sysop:  return rank >= static_cast<uint8_t>(Access::Sysop);
        default:                return false;
    }
}

void forEachKey(uint8_t index, KeyFn fn, void* ctx) {
    scan(index, fn, ctx, false);
}

uint32_t reserveBytes() { return BBS_FS_RESERVE; }

// freeBytes: room left where plugins keep their files, which is the user
// partition, not the one the screens are on. The kept figure (core/space.h,
// 1.1.2): measured on the runner at boot, at each staff login and on MEM
// FORCE, never on the loop. It measured again on the loop once a minute
// before, about 85 ms for whichever plugin write came first. The 32 KB
// reserve is the margin that makes a kept figure safe here. Before the first
// measure after boot the answer is "plenty", not "none": refusing every
// plugin write for the second the runner takes would be the worse error.
uint32_t freeBytes() {
    const space::Fig f = space::get(plat::PART_USER);
    if (!f.valid) return 0xFFFFFFFFu;
    const uint64_t free = f.total > f.used ? f.total - f.used : 0;
    return free > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(free);
}

// ---------------------------------------------------------------------------
// path: where a plugin's files live.
//
// <userdata>/p/<name>/<file> normally, and <sd>/p/<name>/<file> for a PF_SD
// plugin. The split is by who owns the data, the same rule the partitions
// follow: a PF_SD plugin has said its files are the kind that can be large
// and can be lost, so they go on the card and nowhere else. No falling back
// to internal flash when the card is missing, because a plugin that silently
// writes somewhere other than where it was told is worse than one that is
// refused: the sysop pulls the card expecting the data to be on it.
//
// Nobody gets a path once free space is down to the reserve.
// ---------------------------------------------------------------------------
bool path(uint8_t index, const char* file, char* out, size_t n) {
    const Plugin* p = at(index);
    if (!p || !file || !*file) return false;
    if (!(p->info.flags & PF_CORE)) return false;             // only shipped plugins get storage
    if (strchr(file, '/') || strstr(file, "..")) return false;

    const bool sd = (p->info.flags & PF_SD) != 0;
    const char* base = sd ? plat::sdBase() : plat::userBase();
    if (sd) {
        if (!base[0]) return false;                           // no card, so no files
        // The kept figure (core/space.h, 1.1.2): this runs on every file a
        // plugin opens on the card, and asking the card each time is a trip
        // to its FAT.
        //
        // The reserve is a flash rule: LittleFS needs room to garbage collect
        // and the core has to be able to write users.txt whatever a plugin is
        // doing. A card has neither problem, so the only question is whether
        // there is any room left at all.
        const space::Fig c = space::get(plat::PART_CARD);
        if (c.valid && c.total && c.used >= c.total) return false;
    } else {
        if (freeBytes() <= reserveBytes()) return false;
    }

    char dir[96];
    snprintf(dir, sizeof(dir), "%s/%s", base, BBS_PLUGIN_DIR);
    ensureDir(dir);
    snprintf(dir, sizeof(dir), "%s/%s/%s", base, BBS_PLUGIN_DIR, p->info.name);
    ensureDir(dir);
    snprintf(out, n, "%s/%.16s", dir, file);
    return true;
}

bool readPath(uint8_t index, const char* file, char* out, size_t n) {
    const Plugin* p = at(index);
    if (!p || !file || !*file) return false;
    if (!(p->info.flags & PF_CORE)) return false;
    if (strchr(file, '/') || strstr(file, "..")) return false;
    const bool sd = (p->info.flags & PF_SD) != 0;
    const char* base = sd ? plat::sdBase() : plat::userBase();
    if (sd && !base[0]) return false;
    snprintf(out, n, "%s/%s/%s/%.16s", base, BBS_PLUGIN_DIR, p->info.name, file);
    return true;
}

// ---------------------------------------------------------------------------
// begin: every plugin that is switched on and fits gets started.
//
// Two passes. PF_EARLY plugins go first because what they do decides whether
// the others may start at all: the sd plugin mounts the card, and a PF_SD
// plugin's requirement is a mounted card. One pass in table order would give
// the same answer today and a different one the first time somebody reorders
// the registry, which is not a thing that should change behaviour.
// ---------------------------------------------------------------------------
void begin(Bbs& bbs, uint32_t mask) {
    cfgHold();                                            // the file once for all of them (1.1.2)
    for (uint8_t pass = 0; pass < 2; ++pass) {
    for (uint8_t i = 0; i < count(); ++i) {
        const Plugin* p = kPlugins[i];
        if (((p->info.flags & PF_EARLY) != 0) != (pass == 0)) continue;
        if (!(mask & (1u << i))) continue;                // not one this start is for
        State& st = g_state[i];
        st = State();
        st.enabled  = (p->info.flags & PF_ON) != 0;           // on unless told otherwise
        st.level[0] = p->info.read;                           // the plugin's own defaults
        st.level[1] = p->info.write;
        st.level[2] = p->info.admin;
        scan(i, nullptr, nullptr, true);                      // enabled + levels

        if (!st.enabled) { st.why = "off in system.cfg"; continue; }
        if (!(p->info.flags & PF_CORE)) {
            st.why = "not a shipped plugin";
            plat::log("plugin: %s is not a shipped plugin, not started", p->info.name);
            continue;
        }
        if ((p->info.flags & PF_SD) && !plat::sdBase()[0]) {
            // Not an error and not a misconfiguration: a board with no card
            // is a supported board. Say which it is so a sysop who *has*
            // wired a card knows to go and look at SD rather than at this.
            st.why = "no SD card";
            plat::log("plugin: %s keeps its files on the SD card and none is mounted, not started",
                      p->info.name);
            continue;
        }
        // The counter, not plat::heap(), which walks the whole heap under a
        // critical section for its largest block, once per plugin at every
        // CONFIG save (1.1.2). 0 is the host's "cannot tell".
        const uint32_t heapFree = plat::heapFree();
        if (heapFree && p->info.heapBytes && heapFree < p->info.heapBytes + BBS_HEAP_RESERVE) {
            st.why = "not enough memory";
            plat::log("plugin: %s wants %u bytes, only %u free: not started",
                      p->info.name, static_cast<unsigned>(p->info.heapBytes),
                      static_cast<unsigned>(heapFree));
            continue;
        }
        // Measure the partition the plugin will actually write to. A PF_SD
        // plugin asking for 2 MB was being weighed against a 608 KB flash
        // partition it is never going to touch, which would refuse it on a
        // card with gigabytes free.
        uint32_t freeFs  = 0;
        uint32_t reserve = 0;
        if (p->info.flags & PF_SD) {
            const uint64_t cf = space::freeBytes(plat::PART_CARD);   // 0 before the first measure: not checked
            freeFs = cf > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(cf);
        } else {
            freeFs  = freeBytes();
            reserve = reserveBytes();
        }
        if (p->info.storageBytes && freeFs && freeFs < p->info.storageBytes + reserve) {
            st.why = "not enough storage";
            plat::log("plugin: %s wants %u bytes of storage, %u free on %s: not started",
                      p->info.name, static_cast<unsigned>(p->info.storageBytes),
                      static_cast<unsigned>(freeFs), (p->info.flags & PF_SD) ? "the card" : "flash");
            continue;
        }
        if (p->start && !p->start(bbs)) {
            st.why = "refused to start";
            plat::log("plugin: %s refused to start", p->info.name);
            continue;
        }
        if (p->commands && p->commandCount && !bbs.registerCommands(p->commands, p->commandCount, i)) {
            st.why = "command table full";
            plat::log("plugin: %s could not register commands", p->info.name);
            if (p->stop) p->stop();
            continue;
        }
        st.running = true;
        plat::log("plugin: %s %s started (read %s, write %s, admin %s)", p->info.name, p->info.version,
                  levelText(st.level[0]), levelText(st.level[1]), levelText(st.level[2]));
    }
    }
    cfgRelease();
}

void stopAll(uint32_t mask) {
    for (uint8_t i = 0; i < count(); ++i) {
        if (!(mask & (1u << i))) continue;
        if (!g_state[i].running) continue;
        if (kPlugins[i]->stop) kPlugins[i]->stop();
        g_state[i].running = false;
        g_state[i].why     = "stopped";
    }
}

// renamed: tell every running plugin that a handle changed.
void renamed(const char* oldHandle, const char* newHandle) {
    if (!oldHandle || !newHandle || !*oldHandle || !*newHandle) return;
    for (uint8_t i = 0; i < count(); ++i) {
        const Plugin* p = at(i);
        if (running(i) && p->onRename) p->onRename(oldHandle, newHandle);
    }
}

void listDone(uint8_t index, Session& s, bool aborted) {
    if (index >= count() || !running(index)) return;
    const Plugin* p = at(index);
    if (p->listDone) p->listDone(s, aborted);
}

// tick: two cadences. Most plugins every BBS_PLUGIN_TICK_MS; a PF_FAST one
// every BBS_PLUGIN_FAST_MS, and only then, so it is not called twice on a
// pass where both come due.
void tick(uint32_t now) {
    const bool slow = static_cast<int32_t>(now - g_nextTick) >= 0;
    const bool fast = static_cast<int32_t>(now - g_nextFast) >= 0;
    if (!slow && !fast) return;
    if (slow) g_nextTick = now + BBS_PLUGIN_TICK_MS;
    if (fast) g_nextFast = now + BBS_PLUGIN_FAST_MS;
    for (uint8_t i = 0; i < count(); ++i) {
        if (!g_state[i].running || !kPlugins[i]->tick) continue;
        if ((kPlugins[i]->info.flags & PF_FAST) ? fast : slow) kPlugins[i]->tick(now);
    }
}

} // namespace plugins
