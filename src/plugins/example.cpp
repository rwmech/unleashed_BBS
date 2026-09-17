/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/example.cpp
 * Module:       Plugins / example
 *
 * Purpose:      The template plugin, and what the scripted tests drive. It
 *               uses every part of the API: a config key, a stored file,
 *               the periodic hook, caller hooks, read / write / admin
 *               commands, and owning a session until the caller quits.
 *               Copy this file to start a new plugin.
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
#include "../core/bbs.h"
#include "../core/plugin.h"
#include "../platform/platform.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr const char kName[] = "example";

char     g_greeting[32] = "hello";
uint32_t g_ticks   = 0;
uint32_t g_pokes   = 0;
uint8_t  g_index   = 0xFF;                       // our slot in the registry

// readKey: one key from our section of system.cfg
void readKey(void* ctx, const char* key, const char* value) {
    (void)ctx;
    if (!strcmp(key, "greeting")) snprintf(g_greeting, sizeof(g_greeting), "%.31s", value);
}

// load / save: a counter in our own folder, <fs>/p/example/count
void load() {
    char path[96];
    if (!plugins::path(g_index, "count", path, sizeof(path))) return;
    FILE* f = fopen(path, "r");
    if (!f) return;
    unsigned v = 0;
    if (fscanf(f, "%u", &v) == 1) g_pokes = v;
    fclose(f);
}

void save() {
    char path[96];
    if (!plugins::path(g_index, "count", path, sizeof(path))) return;
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%u\n", static_cast<unsigned>(g_pokes));
    fclose(f);
}

bool start(Bbs& bbs) {
    (void)bbs;
    g_index = plugins::indexOf(kName);
    g_ticks = 0;
    plugins::forEachKey(g_index, readKey, nullptr);
    load();
    return true;
}

void stop() {
    save();
}

void tick(uint32_t now) {
    (void)now;
    ++g_ticks;
}

// onKey: we own the session while ECHO runs. Q or the abort keys leave.
void onKey(Session& s, int k, uint32_t now) {
    (void)now;
    Bbs& bbs = Bbs::instance();
    if (k == 'q' || k == 'Q' || k == KEY_ESC || k == KEY_BREAK) {
        s.term.nl(s.tl);
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, "Echo off.");
        bbs.release(s);
        return;
    }
    if (k == KEY_ENTER) { s.term.nl(s.tl); return; }
    if (k < 0x20 || k > 0x7E) return;
    s.term.ch(s.tl, static_cast<char>(k));    // color was set when echo started
}

const Command kCommands[] = {
    { "PING", "", 0, CF_READ, "PING", "example: say hello",
      [](Bbs& b, Session& s, const char*, uint32_t) {
          char buf[96];
          snprintf(buf, sizeof(buf), "%.31s, %.20s. %u ticks, %u pokes.", g_greeting, s.user,
                   static_cast<unsigned>(g_ticks), static_cast<unsigned>(g_pokes));
          s.term.color(s.tl, Color::Cyan);
          s.term.text(s.tl, buf);
          b.prompt(s);
      } },
    { "POKE", "", 0, CF_WRITE, "POKE", "example: count a poke",
      [](Bbs& b, Session& s, const char*, uint32_t) {
          char buf[48];
          ++g_pokes;
          save();
          snprintf(buf, sizeof(buf), "Poked. Count is %u.", static_cast<unsigned>(g_pokes));
          s.term.color(s.tl, Color::LightGreen);
          s.term.text(s.tl, buf);
          b.prompt(s);
      } },
    { "ECHO", "", 0, CF_READ, "ECHO", "example: echo keys, Q quits",
      [](Bbs& b, Session& s, const char*, uint32_t) {
          if (!b.own(s, g_index)) { b.prompt(s); return; }
          b.setDoing(s, "ECHO");
          s.term.color(s.tl, Color::Grey);
          s.term.text(s.tl, "Echo on. Q quits.");
          s.term.nl(s.tl);
          s.term.color(s.tl, Color::LightGreen);    // everything typed comes back green
      } },
    { "EXAMPLE", "", 0, CF_ADMIN, "EXAMPLE", "example: settings (admin)",
      [](Bbs& b, Session& s, const char*, uint32_t) {
          char buf[80];
          char path[96] = "(none)";
          plugins::path(g_index, "count", path, sizeof(path));
          snprintf(buf, sizeof(buf), "greeting=%.20s  file=%.40s", g_greeting, path);
          s.term.color(s.tl, Color::Yellow);
          s.term.text(s.tl, buf);
          b.prompt(s);
      } },
};

} // namespace

extern const Plugin kExamplePlugin = {
    { kName, "Example plugin", "1.0", 0, 512, PF_CORE },
    start,
    stop,
    tick,
    nullptr,                 // onConnect
    nullptr,                 // onLogin
    nullptr,                 // onLogoff
    onKey,
    kCommands,
    sizeof(kCommands) / sizeof(kCommands[0]),
};
