/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/skin.h
 * Module:       Plugins / panel skins (BBS_HAS_LCD boards only)
 *
 * Purpose:      What the panel plugin calls to show a skin: a picture from
 *               the card, <card>/skins/<name>/background.jpg, with the
 *               board's LEDs lit over it and its status lines in a
 *               rectangle, in place of the panel's own drawn layout (the
 *               built-in skin, "status"). SKINS.md is the whole story.
 *
 * Interface:    The panel keeps its framebuffer, its queue of rectangles to
 *               send and the band-at-a-time flush; this module only draws
 *               into the one and adds to the other, and only from the loop.
 *
 *                 want(name, w, h)   at the panel's start: the skin setting
 *                                    and the glass's size. Loads on a worker
 *                                    (never the loop) when it is not the
 *                                    one already loaded.
 *                 stop()             at the panel's stop. A load in flight
 *                                    is called off; a loaded skin is kept
 *                                    for the start that follows a CONFIG
 *                                    save.
 *                 tick(c, d, now, fresh) every panel tick while it draws
 *                                    (not silent). True when the skin owns
 *                                    the glass this pass, and the panel
 *                                    draws none of its own figures; false
 *                                    when the panel draws the status skin,
 *                                    and must draw it whole on the first
 *                                    false after a true. fresh: the panel
 *                                    has just filled figures().
 *                 figures()          the words of the status lines, filled
 *                                    in place by the panel.
 *                 redraw()           the whole skin again on the next tick:
 *                                    after silent mode, or anything else
 *                                    that wrote the framebuffer.
 *                 choices()          CONFIG's list: "status|c64|pc".
 *                 running(), why()   for PANEL: the skin on the glass, and
 *                                    why the one asked for is not.
 *
 * Libraries:    none
 * Targets:      ESP32-S3 boards with BBS_HAS_LCD, and the Linux host build
 * See also:     SKINS.md, src/plugins/skin.cpp, src/plugins/panel.cpp
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
#include "../config.h"

#ifdef BBS_HAS_LCD
#include "panel_gfx.h"
#include "skin_draw.h"
#include "skin_seed.h"

namespace skin {

constexpr char     kBuiltIn[]   = "status";      // the panel's own layout
constexpr uint8_t  kSkinsMax    = 16;            // folders CONFIG offers
constexpr size_t   kChoicesMax  = sizeof(kBuiltIn) + (kSkinsMax + 1) * (kNameMax + 1);

// CONFIG's choices for the skin row, bar separated: the built-in first, the
// card's skins for this panel's size in name order, and the one set in
// system.cfg even while it is not on the card (so a page saved without the
// card does not refuse its own value). A fixed buffer, so the panel's
// settings table can point at it; written only on the loop.
extern char g_choices[kChoicesMax];

void        want(const char* name, uint16_t w, uint16_t h);
void        stop();
bool        tick(panelgfx::Canvas& c, panelgfx::Dirty& d, uint32_t now, bool fresh);
Figures&    figures();    // the status lines' words, which the panel fills
void        redraw();
bool        live();       // a skin is on the glass, or being put there
bool        copying();    // its background is going back into the framebuffer
const char* choices();
const char* running();
const char* title();       // the running skin's name line, "" for none
// uploaded: a file landed in the card's skins/ (the Skins file area): the
// list is read again, and a skin of that name reloaded from the card.
void        uploaded(const char* file);
const char* why();

// validName: a folder name a skin may have: 1 to 24 of A-Z a-z 0-9 _ -, and
// not the built-in's.
bool        validName(const char* s);

// The stock set, in the image (skin_stock.cpp): grouped by folder. Empty
// until the S3 layout with room for it (1.1.2).
const StockFile* stockFiles(size_t& n);

} // namespace skin

#endif  // BBS_HAS_LCD
