/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/composer.cpp
 * Module:       Core / the message editor's keys
 *
 * Purpose:      The message editor's key handling (see composer.h).
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     composer.h
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

#include "composer.h"
#include "bbs.h"
#include <cstdio>

namespace composer {
namespace {

// note: the editor saying something, on its own line, in the quiet colour.
void note(Session& s, const char* text) {
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, text);
    s.term.nl(s.tl);
}

} // namespace

uint8_t width(const Session& s) {
    return compose::lineWidth(s.term.cols(), kPromptCols, BBS_LINE_MAX);
}

void prompt(Session& s, const compose::Body& b) {
    char q[12];
    snprintf(q, sizeof(q), "%2u: ", static_cast<unsigned>(b.rows + 1));
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, q);
    s.term.color(s.tl, Color::White);
    s.ed.begin(width(s), 0);
}

void echo(Session& s, uint8_t row, const char* line) {
    char q[12];
    snprintf(q, sizeof(q), "%2u: ", static_cast<unsigned>(row));
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, q);
    s.term.color(s.tl, Color::White);
    s.term.text(s.tl, line);
    s.term.nl(s.tl);
}

Res key(Session& s, compose::Body& b, int k) {
    // Backspace on an empty line takes the one above back for editing, so a
    // committed line is never out of reach. The recalled text has to land
    // where it was, one row up, or it shows twice.
    if (k == KEY_BACKSPACE && s.ed.len() == 0 && b.rows > 0) {
        char back[BBS_LINE_MAX + 1];
        compose::popLine(b, back, sizeof(back));
        uint8_t w = Bbs::instance().rowWidth(s);
        s.term.eraseBack(s.tl, kPromptCols);
        s.term.up(s.tl, 1);
        s.term.left(s.tl, w);
        s.term.eraseEol(s.tl, w);
        prompt(s, b);
        for (const char* c = back; *c; ++c) s.ed.key(*c, s.term, s.tl);
        return Res::Editing;
    }

    // Wrap while they type rather than stopping the echo dead. The carried
    // word was echoed as it was typed, so it is rubbed off this line
    // through Term::eraseBack before it moves down; writing the backspaces
    // through the text path is what printed "cra? ?? ?? ?" once.
    if (s.ed.len() >= width(s) && k >= ' ' && k < 0x7F) {
        char full[BBS_LINE_MAX + 1];
        snprintf(full, sizeof(full), "%s", s.ed.text());
        char carry[BBS_LINE_MAX + 1] = {};
        uint8_t keep = compose::wrapPoint(full, s.ed.len(), carry, sizeof(carry));
        full[keep] = '\0';
        s.term.eraseBack(s.tl, static_cast<uint8_t>(s.ed.len() - keep));
        s.term.nl(s.tl);
        if (!compose::addLine(b, full)) {
            note(s, "That is as much as one holds. /s saves it.");
            prompt(s, b);
            return Res::Editing;
        }
        prompt(s, b);
        for (const char* c = carry; *c; ++c) s.ed.key(*c, s.term, s.tl);
    }

    LineEditor::Res r = s.ed.key(k, s.term, s.tl);
    if (r == LineEditor::Res::Editing) return Res::Editing;
    if (r == LineEditor::Res::Abort)   return Res::Abort;

    const char* line = s.ed.text();
    if (compose::isAbort(line)) return Res::Abort;
    if (compose::isSave(line)) {
        if (!b.len) {
            note(s, "Nothing written yet. /a throws it away.");
            prompt(s, b);
            return Res::Editing;
        }
        return Res::Save;
    }
    if (!compose::addLine(b, line)) {
        note(s, "That is as much as one holds. /s saves it.");
        prompt(s, b);
        return Res::Editing;
    }
    if (compose::full(b)) note(s, "That is the last line. /s saves it.");
    prompt(s, b);
    return Res::Editing;
}

} // namespace composer
