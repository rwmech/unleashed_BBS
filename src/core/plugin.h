/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/plugin.h
 * Module:       Core / plugin API
 *
 * Purpose:      How a feature bolts onto the BBS. A plugin is a static
 *               descriptor: what it is, what it needs, its commands, and
 *               the hooks it wants. Plugins are compiled in and switched
 *               on in system.cfg; a new one arrives as new firmware.
 *
 * Design:       Everything is plain data, so a script host can later offer
 *               the same surface to Lua without changing the core.
 *
 *                 [plugin:name]
 *                 enabled = yes      (PF_ON plugins are on without this)
 *                 read    = all        who may look
 *                 write   = staff      who may change things
 *                 admin   = sysop      who may configure the plugin
 *                 <its own keys>
 *
 *               Levels, lowest to highest: all (guests too), users (an
 *               account), staff (any staff level), co2, co1, sysop.
 *               Commands carry CF_READ, CF_WRITE or CF_ADMIN; anything
 *               untagged counts as write, so a careless plugin fails shut.
 *
 *               A plugin may own a caller's session (serial bridge, doors):
 *               keys go to its onKey hook until it calls release(). The
 *               idle clock pauses while it owns the session; the per-call
 *               time limit does not.
 *
 *               Each plugin gets a folder under <fs>/p/<name>/ with a size
 *               cap, and the core keeps a free-space reserve so accounts
 *               can always be written. Only the plugins shipped in this
 *               repo may use onboard flash; anything else declares PF_SD.
 *
 * Interfaces:   Plugin, PluginInfo, plugins::begin, at, count, enabled,
 *               mayUse, path, forEachKey; Bbs::own, release, sayTo,
 *               eachSession, setDoing, prompt
 * Depends on:   sysconfig (levels, sections), bbs (sessions, commands),
 *               platform (free space, log)
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
#pragma once
#include <cstdint>
#include <cstddef>
#include "../config.h"

class Bbs;
struct Session;
struct Command;

// Access ladder for a plugin's read / write / admin lines
enum class PlugLevel : uint8_t { All, Users, Staff, Co2, Co1, Sysop, Nobody };

enum PluginFlag : uint8_t {
    PF_NONE  = 0,
    PF_CORE  = 1,    // shipped in this repo: may use the onboard filesystem
    PF_SD    = 2,    // needs the SD card for its files
    PF_ON    = 4,    // on unless system.cfg says enabled = no
};

struct PluginInfo {
    const char* name;          // config section and folder name, a-z 0-9 _
    const char* title;         // one line for ABOUT and the log
    const char* version;       // "1.0"
    uint32_t    heapBytes;     // heap it needs while running (0 = none)
    uint32_t    storageBytes;  // space it wants for its files (0 = none)
    uint8_t     flags;         // PluginFlag
    PlugLevel   read;          // levels used when system.cfg does not say
    PlugLevel   write;
    PlugLevel   admin;
};

// Hooks. Every one is optional; leave it null and the core skips it.
struct Plugin {
    PluginInfo info;

    // start: read config, open hardware, claim memory. False refuses the
    // plugin (logged, BBS carries on without it).
    bool (*start)(Bbs& bbs);

    // stop: called when the plugin is switched off or the config reloads
    void (*stop)();

    // tick: every BBS_PLUGIN_TICK_MS, from the BBS loop. Never block.
    void (*tick)(uint32_t now);

    // caller lifecycle
    void (*onConnect)(Session& s);
    void (*onLogin)(Session& s);
    void (*onLogoff)(Session& s);

    // onKey: only while this plugin owns the session (see plugins::own)
    void (*onKey)(Session& s, int key, uint32_t now);

    const Command* commands;
    uint8_t        commandCount;
};

namespace plugins {

// begin: check requirements, start every enabled plugin, register commands
void begin(Bbs& bbs);

// stopAll: called before a config reload
void stopAll();

// tick: call from the BBS loop
void tick(uint32_t now);

// count / at / info: the compiled-in table, enabled or not
uint8_t count();
const Plugin* at(uint8_t index);
bool enabled(uint8_t index);
bool running(uint8_t index);
const char* whyNot(uint8_t index);          // reason a plugin is not running

// indexOf: find a plugin by name, 0xFF when it is not compiled in
uint8_t indexOf(const char* name);

// levels for this plugin, as configured
PlugLevel levelFor(uint8_t index, uint8_t which);   // 0 read, 1 write, 2 admin

// mayUse: does this session meet a level?
bool mayUse(const Session& s, PlugLevel level);

// path: <fs>/p/<name>/<file>, creating the folder. False if the plugin may
// not use onboard storage or the reserve is gone.
bool path(uint8_t index, const char* file, char* out, size_t n);

// forEachKey: stream this plugin's own config keys (not enabled/read/write/
// admin, which the core handles). Called at start() and on reload.
using KeyFn = void (*)(void* ctx, const char* key, const char* value);
void forEachKey(uint8_t index, KeyFn fn, void* ctx);

// free space on the data filesystem, and the reserve the core keeps
uint32_t freeBytes();
uint32_t reserveBytes();

} // namespace plugins
