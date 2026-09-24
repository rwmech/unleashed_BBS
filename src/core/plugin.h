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
    PF_SD    = 2,    // its files live on the SD card, and it needs one mounted
    PF_ON    = 4,    // on unless system.cfg says enabled = no
    // PF_EARLY: started before the plugins that are not marked with it.
    //
    // This exists for exactly one situation: a plugin that provides something
    // another plugin's requirements are checked against. The sd plugin mounts
    // the card, and whether a PF_SD plugin may start at all is decided by
    // whether a card is mounted, so the answer depends on the sd plugin having
    // already run. Leaving that to the order of the registry table would work
    // and would be invisible, which is the kind of dependency that survives
    // until somebody tidies the list alphabetically.
    PF_EARLY = 8,
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
// ---------------------------------------------------------------------------
// PluginSetting: one row on this plugin's page in CONFIG.
//
// Declaring a setting here is what makes it editable at all. CONFIG used to
// build a plugin's page out of whatever keys system.cfg already carried,
// which meant a setting nobody had written yet was invisible, and the only
// way to reach it was to know it existed and edit the file by hand. A
// declared setting shows up on a fresh board with its live value in it.
// ---------------------------------------------------------------------------
// PS_INFO: shown on the page, never editable, never written back.
//
// For a value the plugin does not own. The board's name belongs to the core
// and announce only publishes it, so announce shows it and a sysop who wants
// it changed is changing it in one place. The alternative, two editable
// fields both labelled "Board" on two pages, is a way of asking somebody to
// keep two copies in step by hand.
//
// PS_PIN: a GPIO number. A PS_NUM with lo and hi, and CONFIG also refuses
// what syscfg::pinProblem refuses (the flash pins), so a pin the plugin's
// own parser would decline is caught on the form rather than written, read,
// declined, and shown to the sysop as "Saved and live". Appended, so every
// existing value keeps its number.
enum : uint8_t { PS_TEXT, PS_NUM, PS_YESNO, PS_INFO, PS_PIN };

struct PluginSetting {
    const char* key;      // key inside the [plugin:<name>] section
    const char* label;    // 9 characters, the form's left column
    uint8_t     kind;     // PS_TEXT, PS_NUM, PS_YESNO, PS_INFO, PS_PIN
    uint16_t    lo;       // PS_NUM: the range the plugin will accept
    uint16_t    hi;
    uint8_t     cap;      // characters, excluding the terminator. May exceed
                          // the form's box: long values scroll while typed.
};

// kCoreRows: the rows CONFIG puts at the top of every plugin's page before
// any of the plugin's own (Enabled, Read, Write, Admin). A page holds
// Form::kMaxFields rows in all, so a plugin has kMaxFields - kCoreRows for
// its settings, and CONFIG drops anything past that without a word. A plugin
// with a long settings table asserts against this, and CONFIG builds its
// page from it, so the two cannot disagree the way a 4 typed in each place
// eventually would.
constexpr uint8_t kCoreRows = 4;

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

    // status: one short line for the sysop dashboard, or null for none.
    // Keep it under the row width, and remember DASH redraws on a timer:
    // return a pointer to storage that outlives the call, and do no work
    // worth mentioning.
    const char* (*status)();

    const Command* commands;
    uint8_t        commandCount;

    // ----------------------------------------------------------------------
    // Everything below is optional and was added after the first plugins
    // were written, so it sits at the end: a descriptor that leaves these
    // out still compiles and simply offers no settings of its own.
    // ----------------------------------------------------------------------

    // settings: the rows CONFIG offers, in the order they should appear.
    const PluginSetting* settings;
    uint8_t              settingCount;

    // setting: this plugin's live value for one key, used when system.cfg
    // does not carry it. Leave out empty for a key you do not recognise, so
    // a blank on the form never quietly means something else.
    void (*setting)(const char* key, char* out, size_t n);

    // rows: one line of a paged list, false when the list is finished.
    //
    // Called by the core after Bbs::startPluginList, once per line, with the
    // row number in Session::listIdx exactly as the core's own list builders
    // use it. The plugin gets the core's paging, [More] prompt, abort keys
    // and output backpressure rather than reimplementing them, which is the
    // whole reason this hook exists: a plugin printing a long list straight
    // into the timeline either truncates it or outruns a slow terminal.
    //
    // Appended, like everything below the line above, so the descriptors
    // written before it compile untouched and simply offer no list. The
    // struct is filled positionally, so a field inserted in the middle
    // silently shifts every existing one.
    bool (*rows)(Session& s);

    // onPresence: what the outside can see about who is on has changed.
    //
    // Not the same thing as onLogin and onLogoff, which mean a caller
    // arrived or left. A staff member typing SHOW, HIDE or LURK changes the
    // figure the board publishes without anybody arriving or leaving, because
    // Bbs::publicBusy counts a session only while it is visible.
    //
    // One hook rather than three calls bolted onto three commands, so that
    // the next thing that changes the public count, a sysop page or a door,
    // is not a fourth place somebody has to remember.
    void (*onPresence)(Session& s);

    // onBytes: input as it arrived, for a plugin in raw mode.
    //
    // onKey delivers decoded keys, which is right for everything a caller
    // types and wrong for a file: the terminal layer would read an 0x1B in
    // a data block as the start of an escape sequence and an 0x0D as Enter.
    // A plugin turns this on with Bbs::setRawInput while it owns the
    // session, and off again afterwards.
    //
    // Telnet has already been unescaped by the time these arrive, so IAC IAC
    // is one 0xFF here. Whether CR is left alone is Telnet::setBinary.
    void (*onBytes)(Session& s, const uint8_t* b, size_t n, uint32_t now);

    // onRename: a caller's handle has changed, old to new.
    //
    // A handle is a display name and a plugin that filed anything under one
    // has to be told when it changes, or that thing is orphaned. Chat's
    // mailbox and the room's ban list are both keyed by handle: before this
    // existed, renaming somebody hid their own unread mail from them, and
    // walked them straight out of a room ban.
    //
    // Called after users.txt has been written and only when the write
    // succeeded, so a plugin acting on it can trust that the new name is
    // the real one. Both names are valid for the length of the call only.
    void (*onRename)(const char* oldHandle, const char* newHandle);

    // listDone: a paged list this plugin started has finished.
    //
    // aborted is true when the caller stopped it at [More] rather than
    // reading to the end. A plugin that owns the session gets it back
    // either way and has to put something on the screen, because the core
    // deliberately does not print its prompt for it.
    //
    // Without this, stopping a file listing with Q left the caller looking
    // at "Stopped." and nothing else: the session was handed back to the
    // plugin with no way for it to know, and the file manager only drew its
    // prompt as the last row of a listing that had just been abandoned.
    void (*listDone)(Session& s, bool aborted);
};

namespace plugins {

// begin: check requirements, start every enabled plugin, register commands
void begin(Bbs& bbs);

// stopAll: called before a config reload
void stopAll();

// tick: call from the BBS loop
void tick(uint32_t now);

// renamed: a caller's handle changed. Call after users.txt is written.
void renamed(const char* oldHandle, const char* newHandle);

// listDone: tell the plugin that owns this session its list has ended.
void listDone(uint8_t index, Session& s, bool aborted);

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

// levelName: the word a config file uses for a level
const char* levelName(PlugLevel level);

// levelFromText: the reverse. A plugin with levels of its own, such as a
// file area that is staff only, parses them with the same words and the
// same ladder the core uses, so a sysop learns one vocabulary.
bool levelFromText(const char* v, PlugLevel& out);

// mayUse: does this session meet a level?
bool mayUse(const Session& s, PlugLevel level);

// path: <fs>/p/<name>/<file>, creating the folder. False if the plugin may
// not use onboard storage or the reserve is gone.
bool path(uint8_t index, const char* file, char* out, size_t n);

// readPath: the same name, for READING. No reserve check and no folders made.
// path() is a write guard: it measures the partition's free space, which on
// LittleFS walks the whole filesystem's metadata, and makes two directories.
// Paying that on every read put a full traversal behind every mailbox row and
// every information page lookup, on the BBS task. A read of a file that is
// not there simply fails to open.
bool readPath(uint8_t index, const char* file, char* out, size_t n);

// forEachKey: stream this plugin's own config keys (not enabled/read/write/
// admin, which the core handles). Called at start() and on reload.
using KeyFn = void (*)(void* ctx, const char* key, const char* value);
void forEachKey(uint8_t index, KeyFn fn, void* ctx);

// free space on the data filesystem, and the reserve the core keeps
uint32_t freeBytes();
uint32_t reserveBytes();

} // namespace plugins
