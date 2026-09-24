/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/composer.h
 * Module:       Core / the message editor's keys
 *
 * Purpose:      The half of the message editor that compose.h leaves out:
 *               what each key does on a session. compose.h is the buffer
 *               and has no board in it, so it can be tested alone; this is
 *               the prompt, the wrap while typing, backspace up a line, and
 *               /s and /a, on a real terminal.
 *
 * Design:       Forums and mail each wrote this out for themselves, the
 *               same fifty lines twice, and two copies drift. The
 *               information pages would have been a third. They use this
 *               one; moving forums and mail onto it is queued for the build
 *               after 0.22.0 rather than re-plumbing both in the middle of
 *               one.
 *
 * Interfaces:   composer::width, prompt, echo, key
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     compose.h
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

#pragma once
#include <cstdint>
#include "compose.h"

struct Session;

namespace composer {

enum class Res : uint8_t { Editing, Save, Abort };

// The " 1: " in front of every line. The rub-out counts the same four.
constexpr uint8_t kPromptCols = 4;

// width: the line length, from the terminal, one place, so the editor's
// capacity and the wrap trigger cannot come apart.
uint8_t width(const Session& s);

// prompt: " n: " for the next line, and the line editor armed behind it.
void prompt(Session& s, const compose::Body& b);

// redraw: the same " n: " again, with what was being typed on it, after a
// notice interrupted the line (1.1.0). Arms a fresh line if there is none.
void redraw(Session& s, const compose::Body& b);

// echo: show a line that is already in the body as if it had just been
// typed, so editing a page starts from what the page says.
void echo(Session& s, uint8_t row, const char* line);

// key: one key. Save and Abort are for the caller to act on; everything
// else, including the notes about running out of room, is handled here.
Res key(Session& s, compose::Body& b, int k);

} // namespace composer
