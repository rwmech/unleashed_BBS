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
#include "plugin.h"
#include "bbs.h"
#include "bbs_util.h"
#include "sysconfig.h"
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

// section: "[plugin:name]" for this plugin
void sectionName(const char* name, char* out, size_t n) {
    snprintf(out, n, "[plugin:%s]", name);
}

// levelFromText: the access ladder
bool levelFromText(const char* v, PlugLevel& out) {
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

// scan: walk system.cfg, hand every key of one plugin's section to fn.
// core = true also applies enabled/read/write/admin to the plugin's state.
void scan(uint8_t index, plugins::KeyFn fn, void* ctx, bool core) {
    const Plugin* p = plugins::at(index);
    if (!p) return;
    char path[96], want[40], line[176];
    snprintf(path, sizeof(path), "%s/%s", plat::fsBase(), BBS_CONFIG_FILE);
    sectionName(p->info.name, want, sizeof(want));
    FILE* f = fopen(path, "r");
    if (!f) return;

    bool mine = false;
    while (fgets(line, sizeof(line), f)) {
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
            if (ieq(key, "read"))  { if (levelFromText(val, lv)) st.level[0] = lv;
                                     else plat::log("cfg: [plugin:%s] bad read level '%s'", p->info.name, val);
                                     continue; }
            if (ieq(key, "write")) { if (levelFromText(val, lv)) st.level[1] = lv;
                                     else plat::log("cfg: [plugin:%s] bad write level '%s'", p->info.name, val);
                                     continue; }
            if (ieq(key, "admin")) { if (levelFromText(val, lv)) st.level[2] = lv;
                                     else plat::log("cfg: [plugin:%s] bad admin level '%s'", p->info.name, val);
                                     continue; }
        }
        if (fn) fn(ctx, key, val);
    }
    fclose(f);
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

uint32_t freeBytes() {
    uint32_t total = 0, used = 0;
    if (!plat::fsInfo(total, used) || used > total) return 0;
    return total - used;
}

// ---------------------------------------------------------------------------
// path: <fs>/p/<name>/<file>. Plugins that need the SD card get nothing
// here, and neither does anyone once free space is down to the reserve.
// ---------------------------------------------------------------------------
bool path(uint8_t index, const char* file, char* out, size_t n) {
    const Plugin* p = at(index);
    if (!p || !file || !*file) return false;
    if (p->info.flags & PF_SD) return false;                  // SD card not here yet
    if (!(p->info.flags & PF_CORE)) return false;             // only shipped plugins use flash
    if (freeBytes() <= reserveBytes()) return false;
    if (strchr(file, '/') || strstr(file, "..")) return false;

    char dir[96];
    snprintf(dir, sizeof(dir), "%s/%s", plat::fsBase(), BBS_PLUGIN_DIR);
    ensureDir(dir);
    snprintf(dir, sizeof(dir), "%s/%s/%s", plat::fsBase(), BBS_PLUGIN_DIR, p->info.name);
    ensureDir(dir);
    snprintf(out, n, "%s/%.16s", dir, file);
    return true;
}

// ---------------------------------------------------------------------------
// begin: every plugin that is switched on and fits gets started
// ---------------------------------------------------------------------------
void begin(Bbs& bbs) {
    for (uint8_t i = 0; i < count(); ++i) {
        const Plugin* p = kPlugins[i];
        State& st = g_state[i];
        st = State();
        st.enabled  = (p->info.flags & PF_ON) != 0;           // on unless told otherwise
        st.level[0] = p->info.read;                           // the plugin's own defaults
        st.level[1] = p->info.write;
        st.level[2] = p->info.admin;
        scan(i, nullptr, nullptr, true);                      // enabled + levels

        if (!st.enabled) { st.why = "off in system.cfg"; continue; }
        if ((p->info.flags & PF_SD) || !(p->info.flags & PF_CORE)) {
            st.why = "needs the SD card";
            plat::log("plugin: %s needs the SD card, not started", p->info.name);
            continue;
        }
        plat::HeapStats h = plat::heap();
        if (h.valid && p->info.heapBytes && h.freeBytes < p->info.heapBytes + BBS_HEAP_RESERVE) {
            st.why = "not enough memory";
            plat::log("plugin: %s wants %u bytes, only %u free: not started",
                      p->info.name, static_cast<unsigned>(p->info.heapBytes),
                      static_cast<unsigned>(h.freeBytes));
            continue;
        }
        uint32_t freeFs = freeBytes();
        if (p->info.storageBytes && freeFs && freeFs < p->info.storageBytes + reserveBytes()) {
            st.why = "not enough storage";
            plat::log("plugin: %s wants %u bytes of storage, %u free: not started",
                      p->info.name, static_cast<unsigned>(p->info.storageBytes),
                      static_cast<unsigned>(freeFs));
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

void stopAll() {
    for (uint8_t i = 0; i < count(); ++i) {
        if (!g_state[i].running) continue;
        if (kPlugins[i]->stop) kPlugins[i]->stop();
        g_state[i].running = false;
        g_state[i].why     = "stopped";
    }
}

void tick(uint32_t now) {
    if (static_cast<int32_t>(now - g_nextTick) < 0) return;
    g_nextTick = now + BBS_PLUGIN_TICK_MS;
    for (uint8_t i = 0; i < count(); ++i) {
        if (g_state[i].running && kPlugins[i]->tick) kPlugins[i]->tick(now);
    }
}

} // namespace plugins
