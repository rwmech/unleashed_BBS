/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/silent.h
 * Module:       Core / silent mode (1.1.0)
 *
 * Purpose:      Silent mode: every light the firmware drives off, and kept
 *               off (Rob: "Silent as in no lights anywhere"). One switch in
 *               CONFIG board, and optional silent hours beside it, a start
 *               and an end time, local by the board's timezone.
 *
 *               It is an override, not a setting anything else has to
 *               remember. The activity LED, the lights plugin's two outputs,
 *               the panel's backlight, and the camera plugin's flash (a snap
 *               in silence is taken without it), each keep their own
 *               settings untouched, ask silent() before they light
 *               anything, and carry on exactly as they were when it ends.
 *
 *               What it cannot do: a dev board's power LED is wired straight
 *               to 3V3 on every board this firmware knows (the WROOM dev
 *               board, the Waveshare S3, the Freenove). No pin drives it, so
 *               no firmware can turn it off. Tape, or lifting the LED.
 *
 * Design:       Rule no. 1: silent() is one byte read, from anywhere, any
 *               number of times a frame. The switch and the hours are looked
 *               at by silentTick, from the BBS loop, at most once a second,
 *               and straight away when the settings are read again (a
 *               CONFIG save). No file is read for it: the times are in the
 *               live SysConfig, parsed once with the rest of system.cfg.
 *
 *               The hours want a real clock. Until NTP has set it they do
 *               nothing, and only the switch applies: a board that does not
 *               know what time it is must not guess it is night.
 *
 *               The rules are pure and header-only, so host/test_silent.cpp
 *               drives them at every boundary with no board.
 *
 * Interfaces:   silent, silentWhy, silentTick, inHours, rule, parseTime,
 *               fmtTime
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E, ESP32-S3 (ESP-IDF 5.3.1) and the Linux host
 * See also:     COMMANDS.md (CONFIG board, SYS), src/plugins/lights.cpp,
 *               src/plugins/panel.cpp
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
#include <cstddef>

namespace board {

// Why the board is silent, if it is. The switch wins over the hours: with
// both, the lights stay off after the hours end, so the switch is the
// honest answer to "why".
enum class Quiet : uint8_t { Off, Switch, Hours };

// A time of day not set: blank in system.cfg.
constexpr int16_t kNoTime  = -1;
// parseTime's answer for a value that is not a time at all.
constexpr int16_t kBadTime = -2;

// ---------------------------------------------------------------------------
// inHours: is minute (0 to 1439) inside the hours from..until? The start is
// in and the end is out, so 22:00 to 07:00 is silent at 22:00 and lit again
// at 07:00. from after until crosses midnight. Either end unset, the two the
// same, or no clock (minute -1): never.
// ---------------------------------------------------------------------------
constexpr bool inHours(int16_t from, int16_t until, int16_t minute) {
    return from >= 0 && until >= 0 && minute >= 0 && from != until &&
           (from < until ? (minute >= from && minute < until)
                         : (minute >= from || minute < until));
}

// rule: the whole decision. sw is the switch; minute is -1 with no clock.
constexpr Quiet rule(bool sw, int16_t from, int16_t until, int16_t minute) {
    return sw ? Quiet::Switch : inHours(from, until, minute) ? Quiet::Hours : Quiet::Off;
}

// ---------------------------------------------------------------------------
// parseTime: "H:MM" or "HH:MM", 00:00 to 23:59, as minutes since midnight.
// Empty (or all spaces) is kNoTime; anything else is kBadTime. No seconds,
// no "7pm": the form's box is five characters and the file is read by
// people as well as by this.
// ---------------------------------------------------------------------------
inline int16_t parseTime(const char* v) {
    if (!v) return kNoTime;
    while (*v == ' ') ++v;
    if (!*v) return kNoTime;
    int h = 0, digits = 0;
    while (*v >= '0' && *v <= '9' && digits < 3) { h = h * 10 + (*v - '0'); ++v; ++digits; }
    if (digits < 1 || digits > 2 || *v != ':') return kBadTime;
    ++v;
    if (v[0] < '0' || v[0] > '9' || v[1] < '0' || v[1] > '9') return kBadTime;
    int m = (v[0] - '0') * 10 + (v[1] - '0');
    v += 2;
    while (*v == ' ') ++v;
    if (*v || h > 23 || m > 59) return kBadTime;
    return static_cast<int16_t>(h * 60 + m);
}

// fmtTime: minutes as "HH:MM" into out (6 bytes at least), "" for kNoTime.
inline void fmtTime(int16_t minutes, char* out, size_t n) {
    if (!out || n < 6) { if (out && n) out[0] = '\0'; return; }
    if (minutes < 0 || minutes >= 1440) { out[0] = '\0'; return; }
    out[0] = static_cast<char>('0' + minutes / 600);
    out[1] = static_cast<char>('0' + (minutes / 60) % 10);
    out[2] = ':';
    out[3] = static_cast<char>('0' + (minutes % 60) / 10);
    out[4] = static_cast<char>('0' + minutes % 10);
    out[5] = '\0';
}

// ---------------------------------------------------------------------------
// The board's state (core/silent.cpp).
//
// silent:     every light off, now. One byte read: ask it every frame.
// silentWhy:  and why: Off, Switch or Hours.
// silentTick: look at the switch and the hours again, at most once a second
//             unless forced. The BBS loop calls it every pass; syscfg's load
//             and reload force it, so a CONFIG save or a restore takes effect
//             before the plugins start again. A change is logged, and the
//             activity LED is told (plat::ledSilent).
// ---------------------------------------------------------------------------
bool  silent();
Quiet silentWhy();
void  silentTick(uint32_t now, bool force = false);

} // namespace board
