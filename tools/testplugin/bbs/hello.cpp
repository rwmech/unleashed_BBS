// ===========================================================================
//  unleashed_hello: the smallest plugin kept in a repository of its own
// ===========================================================================
//
// File:         bbs/hello.cpp
// Module:       An external plugin for µnleashed BBS (1.2.0)
//
// Purpose:      One command, HELLO, and a line in the console when it starts.
//               It exists to prove the path a plugin in its own repository
//               takes into a board's firmware (LINK.md, "Plugins in their own
//               repositories"), and as the template for the next one.
//
//               What an external plugin may do is what a plugin in the tree
//               may do, with one difference: it may not keep files on the
//               board's own flash (plugin.h: only PF_CORE plugins may), so
//               anything it stores goes on the card (PF_SD).
//
//               The includes are from the core's src/ folder, which is on the
//               include path for every plugin's sources.
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
// with this program. If not, see <https://www.gnu.org/licenses/>.
// ===========================================================================
#include "core/bbs.h"
#include "core/plugin.h"
#include "platform/platform.h"

// The plugin API this was written against: a core older than it will not
// compile it, and says why.
UNLEASHED_PLUGIN_API(1, 0);

namespace {

bool start(Bbs&) {
    plat::log("hello: an external plugin, started (api %d.%d)", BBS_PLUGIN_API_MAJOR, BBS_PLUGIN_API_MINOR);
    return true;
}

const Command kCommands[] = {
    { "HELLO", "", 0, CF_READ, "HELLO", "hello from an external plugin",
      [](Bbs& b, Session& s, const char*, uint32_t) {
          s.term.color(s.tl, Color::LightGreen);
          s.term.text(s.tl, "Hello from a plugin in its own repository.");
          b.prompt(s);
      },
      Menu::Hidden },
};

}  // namespace

extern const Plugin kHelloPlugin = {
    { "hello", "Hello, from its own repository", "1.0.0", 0, 0, PF_ON,
      PlugLevel::All, PlugLevel::Staff, PlugLevel::Sysop },
    start,
    nullptr,                 // stop
    nullptr,                 // tick
    nullptr,                 // onConnect
    nullptr,                 // onLogin
    nullptr,                 // onLogoff
    nullptr,                 // onKey
    nullptr,                 // status
    kCommands,
    sizeof(kCommands) / sizeof(kCommands[0]),
    nullptr,                 // settings
    0,
    nullptr,                 // setting
    nullptr,                 // rows
    nullptr,                 // onPresence
    nullptr,                 // onBytes
    nullptr,                 // onRename
    nullptr,                 // listDone
};
