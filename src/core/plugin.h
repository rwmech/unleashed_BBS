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
    // PF_FAST: tick every BBS_PLUGIN_FAST_MS (20 ms) instead of every
    // BBS_PLUGIN_TICK_MS (250 ms). For a plugin that draws something that
    // moves, which at four frames a second is a slide show: the lights
    // plugin's pixels. A flag rather than a new hook, because a hook is a
    // field appended to every descriptor in the tree, and this is the same
    // hook called more often. The rule for tick() does not change: never
    // block, and at this rate do even less.
    PF_FAST  = 16,
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
//
// PS_OPTNUM: a PS_NUM that may also be left empty, which the plugin reads as
// "work it out" (1.1.0). An empty box is written as an empty value rather
// than refused as "Numbers only", and the plugin's setting() hook returns
// empty while the file does not set it, so the page shows what is true.
// Kept apart from PS_NUM because an empty value parses as 0, and on a PS_NUM
// whose range starts at 0 that would quietly mean something.
//
// A PS_PIN whose lo is -1 takes -1 as "no pin, this output is off" (1.1.0).
// The range says so rather than the kind, because the plugins differ: the
// lights are off until given a pin and -1 is exactly that, while an SD card
// has no "off" for its chip select, and a -1 CONFIG took would be a -1 the
// sd plugin's parser declines, the "Saved and live" trap PS_PIN exists to
// close.
//
// PS_CYCLE: one of a fixed list, stepped with Space or picked by its first
// letter, as the level fields are. The list is `choices`, bar separated.
//
// PS_PAGE: a button to a page of its own. It holds every setting in this
// table whose key is this one's followed by a number or an underscore
// ("led" holds led1 to led10, chat's "color" holds color_node and the rest),
// and those rows are left off the plugin's main page. For a plugin with more
// rows than one form can hold: kMaxFields less the core's four is twelve,
// and the lights want ten pixels on top of their own six. The button's text
// is the plugin's setting() for this key. Escape on the page, or saving it,
// comes back to the main page.
//
// PS_GROW: a PS_PAGE whose rows are slots, used or not (1.1.0). While they
// fit on the main page they are shown there instead of the button: the rows
// that have a value, and the first that has none, so the page grows a row at
// a time as they are filled in. The forums' sixteen topics work this way, up
// to twelve in place and then the button to all of them. Kept apart from
// PS_PAGE because an empty row means "unused" only for slots: an empty colour
// in chat means "the default", and showing only the first empty one would
// hide the rest.
enum : uint8_t { PS_TEXT, PS_NUM, PS_YESNO, PS_INFO, PS_PIN, PS_OPTNUM, PS_CYCLE,
                 PS_PAGE, PS_GROW };

struct PluginSetting {
    const char* key;      // key inside the [plugin:<name>] section
    const char* label;    // 9 characters, the form's left column at 40
    uint8_t     kind;     // PS_TEXT, PS_NUM, PS_YESNO, PS_INFO, PS_PIN, PS_OPTNUM,
                          // PS_CYCLE, PS_PAGE, PS_GROW
    int16_t     lo;       // PS_NUM, PS_PIN: the range the plugin will accept.
                          // Signed since 1.1.0, for a pin's -1.
    uint16_t    hi;
    uint8_t     cap;      // characters, excluding the terminator. May exceed
                          // the form's box: long values scroll while typed.
                          // CONFIG holds kSettingMax at most.
    // Appended (1.1.0), with a default, so every table written before it
    // still compiles, warns about nothing and reads nullptr here. Shown on
    // the form's status line while the row has the focus: 38 characters,
    // and anything longer is cut.
    const char* note = nullptr;
    // PS_CYCLE: what it steps through, bar separated ("nodes|hayes|off").
    // Appended after note (1.1.0) and defaulted the same way. CONFIG looks
    // it up from here when it draws the page rather than copying it into
    // every row, which would be sixteen pointers of static RAM for the one
    // page that has any.
    const char* choices = nullptr;
    // At 80 columns (1.1.0): the label, 20 characters, and the note, 78.
    // Null means the short one, padded, which reads fine for "Hostname" or
    // "Pixel 3" and is the right fallback for a table that has none. Plain
    // ASCII is 80 columns, so its prompts use these too. Appended after
    // choices, so a row that sets one spells the members before it:
    //   { "cs", "CS pin", PS_PIN, 0, 33, 2, nullptr, nullptr, "Chip select GPIO" }
    const char* wide     = nullptr;
    const char* wideNote = nullptr;
    // A PS_NUM that CONFIG takes up to hi but asks about past warnAbove
    // (1.1.0): saving a value above it puts "Drive % over 30: <warn> Save
    // anyway? (y/N)" to the sysop first, one question for the page naming
    // every such row, and anything but Y leaves the page open with nothing
    // saved. warn is what goes after the colon, one short sentence; null, as
    // shipped, means the row never asks. Only a row being changed asks, so
    // a page saved again later does not ask about a value already set.
    int16_t     warnAbove = 0;
    const char* warn      = nullptr;
};

// kSettingMax: the longest value CONFIG can hold for one setting. A cap
// above it is cut to it on the form. 120 because that is the longest the
// directory takes for a description (ANNOUNCE.md), and CONFIG held 95 while
// the plugin and the file took 120, so editing that row cut the tail off
// (1.1.0). Every byte here is one of sixteen in CONFIG's value buffers.
constexpr uint8_t kSettingMax = 120;

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

    // tick: every BBS_PLUGIN_TICK_MS, from the BBS loop, or every
    // BBS_PLUGIN_FAST_MS for a PF_FAST plugin. Never block.
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

    // ----------------------------------------------------------------------
    // Notices inside a plugin (1.1.0). Appended, and defaulted to null here,
    // so every descriptor written before them compiles, warns about nothing
    // and keeps the old behaviour: a notice waits until the caller is back
    // at the main prompt.
    //
    // Pages, broadcasts, SHUTDOWN's countdown, "you have mail" and a ring
    // for the sysop used to reach a caller only at the main prompt, so
    // somebody in the chat room, the forums, the file areas or their mailbox
    // got none of them, and a caller in the room through a SHUTDOWN was hung
    // up with no warning at all. A plugin that owns a session offers these
    // two so the core can print into it:
    //
    //   liftInput      take the input line out of the way and leave the
    //                  cursor at column 0 of an empty line. Return false for
    //                  "not now" (a transfer, a screen half drawn): the core
    //                  keeps the notice and asks again on a later pass.
    //   restoreInput   the notice is out: put the prompt back, with what the
    //                  caller had typed. Called once for each liftInput that
    //                  returned true, possibly much later: a ring for the
    //                  sysop is a question, and the core takes its one-key
    //                  answer between the two. If s.ed is not active when it
    //                  arrives, the core has used the line editor in between
    //                  (a caller ringing from the chat room), so start a fresh
    //                  line rather than redrawing the old one.
    //
    // The core only calls them while the session is SState::Plugin, owned by
    // this plugin, not in raw mode, and with nothing waiting to be sent.
    // ----------------------------------------------------------------------
    bool (*liftInput)(Session& s) = nullptr;
    void (*restoreInput)(Session& s) = nullptr;
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
