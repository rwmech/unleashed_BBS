/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/serialbridge.cpp
 * Module:       Plugins / serial bridge
 *
 * Purpose:      Shares a serial device with the callers. One operator types
 *               at the device, any number of watchers see the same stream,
 *               so one window can drive a modem or a board while others
 *               watch from anywhere on the BBS.
 *
 * Design:       The device is on the second UART, never the console, so
 *               flashing and the serial monitor keep working. The plugin
 *               owns each joined session: the operator's keys go straight
 *               out the port, a watcher's keys only leave or ask for the
 *               seat. Device output goes to everyone through the terminal
 *               layer, so a C64 sees readable PETSCII. A watcher who
 *               cannot keep up is told how much it skipped rather than
 *               holding up the device or the other viewers. A small
 *               scrollback replays what just happened to whoever joins.
 *
 *                 [plugin:serial]
 *                 enabled = yes
 *                 read    = all        who may watch
 *                 write   = staff      who may hold the operator seat
 *                 admin   = sysop      who may change pins
 *                 rx = 16              UART2 defaults; any free pin works
 *                 tx = 17
 *                 baud = 115200
 *                 format = 8N1
 *
 * Libraries:    none (libc)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     PLUGINS.md, COMMANDS.md
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
#include "../core/bbs_util.h"
#include "../core/plugin.h"
#include "../platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace bbsu;

namespace {

constexpr const char kName[]      = "serial";
constexpr uint16_t   kScrollback  = 1024;     // bytes replayed to a joiner
constexpr uint16_t   kReadChunk   = 128;      // bytes taken from the port per tick
constexpr uint16_t   kRoomNeeded  = 512;      // output room a watcher must have

int      g_rx = 16, g_tx = 17;                // UART2 defaults on a WROOM-32E
uint32_t g_baud   = 115200;
uint8_t  g_bits   = 8, g_stop = 1;
char     g_parity = 'N';
uint8_t  g_index  = 0xFF;
Session* g_operator = nullptr;

char     g_back[kScrollback];
uint16_t g_backLen = 0;
uint32_t g_rxBytes = 0, g_txBytes = 0;

// ---------------------------------------------------------------------------
// config
// ---------------------------------------------------------------------------
void parseFormat(const char* v) {                 // "8N1"
    if (strlen(v) < 3) return;
    if (v[0] >= '5' && v[0] <= '8') g_bits = static_cast<uint8_t>(v[0] - '0');
    char p = static_cast<char>(toupper(static_cast<unsigned char>(v[1])));
    if (p == 'N' || p == 'E' || p == 'O') g_parity = p;
    if (v[2] == '1' || v[2] == '2') g_stop = static_cast<uint8_t>(v[2] - '0');
}

void readKey(void* ctx, const char* key, const char* value) {
    (void)ctx;
    if (!strcmp(key, "rx"))          g_rx = static_cast<int>(strtol(value, nullptr, 10));
    else if (!strcmp(key, "tx"))     g_tx = static_cast<int>(strtol(value, nullptr, 10));
    else if (!strcmp(key, "baud"))   g_baud = static_cast<uint32_t>(strtoul(value, nullptr, 10));
    else if (!strcmp(key, "format")) parseFormat(value);
}

// badPin: pins that are not ours to use
bool badPin(int pin, bool output) {
    if (pin < 0 || pin > 39) return true;
    if (pin >= 6 && pin <= 11) return true;                  // internal flash
    if (output && pin >= 34) return true;                    // input only
    if (pin == 1 || pin == 3) return true;                   // the console UART
    return false;
}

// ---------------------------------------------------------------------------
// scrollback
// ---------------------------------------------------------------------------
void keep(const uint8_t* data, size_t n) {
    if (n >= kScrollback) { data += n - kScrollback; n = kScrollback; }
    if (g_backLen + n > kScrollback) {
        uint16_t drop = static_cast<uint16_t>(g_backLen + n - kScrollback);
        memmove(g_back, g_back + drop, g_backLen - drop);
        g_backLen = static_cast<uint16_t>(g_backLen - drop);
    }
    memcpy(g_back + g_backLen, data, n);
    g_backLen = static_cast<uint16_t>(g_backLen + n);
}

struct Fan { const uint8_t* data; size_t n; };

// toWatchers: device output to everyone in the session, printable only
void toWatchers(const uint8_t* data, size_t n) {
    Fan f{ data, n };
    Bbs::instance().eachSession([](void* ctx, Session& s) {
        Fan* f = static_cast<Fan*>(ctx);
        if (!Bbs::instance().owns(s, g_index)) return;
        if (s.tl.freeBytes() < kRoomNeeded) {                // slow terminal: say what it missed
            char note[40];
            snprintf(note, sizeof(note), "[skipped %u bytes]", static_cast<unsigned>(f->n));
            if (s.tl.freeBytes() > 64) {
                s.term.color(s.tl, Color::DarkGrey);
                s.term.text(s.tl, note);
                s.term.nl(s.tl);
            }
            return;
        }
        s.term.color(s.tl, Color::LightGreen);
        for (size_t i = 0; i < f->n; ++i) {
            char c = static_cast<char>(f->data[i]);
            if (c == '\n')                   s.term.nl(s.tl);
            else if (c == '\r')              continue;
            else if (c >= 0x20 && c <= 0x7E) s.term.ch(s.tl, c);
            else if (c == '\t')              s.term.text(s.tl, "  ");
            else                             s.term.ch(s.tl, '.');
        }
    }, &f);
}

void status(Session& s, const char* extra) {
    char buf[80];
    snprintf(buf, sizeof(buf), "%s %u %u%c%u  rx %u tx %u%s%s",
             plat::serialIsOpen() ? "Port open" : "Port closed",
             static_cast<unsigned>(g_baud), g_bits, g_parity, g_stop,
             static_cast<unsigned>(g_rxBytes), static_cast<unsigned>(g_txBytes),
             extra && *extra ? "  " : "", extra ? extra : "");
    s.term.color(s.tl, Color::Cyan);
    s.term.text(s.tl, buf);
    s.term.nl(s.tl);
}

bool mayDrive(const Session& s) {
    return plugins::mayUse(s, plugins::levelFor(g_index, 1));
}

void hint(Session& s) {
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, g_operator == &s ? "You have the keyboard. ESC leaves, F1 takes settings."
                                       : "Watching. T takes the keyboard, ESC leaves.");
    s.term.nl(s.tl);
}

void leave(Session& s, const char* why) {
    Bbs& bbs = Bbs::instance();
    if (g_operator == &s) g_operator = nullptr;
    bbs.release(s);
    if (why && *why) {
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, why);
    }
}

void join(Bbs& bbs, Session& s) {
    if (!plat::serialIsOpen()) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "The serial port is not open.");
        bbs.prompt(s);
        return;
    }
    if (!bbs.own(s, g_index)) { bbs.prompt(s); return; }
    bbs.setDoing(s, "SERIAL");
    Term& t = s.term;
    t.reset(s.tl);
    t.nl(s.tl);
    status(s, "");
    if (g_backLen) {                                   // what the device said just now
        t.color(s.tl, Color::DarkGrey);
        t.text(s.tl, "--- recent ---");
        t.nl(s.tl);
        toWatchers(reinterpret_cast<const uint8_t*>(g_back), g_backLen);
        t.nl(s.tl);
    }
    if (!g_operator && mayDrive(s)) g_operator = &s;    // free seat, and allowed to drive
    hint(s);
}

// ---------------------------------------------------------------------------
// onKey: the operator types at the device; a watcher can only leave or
// ask for the seat.
// ---------------------------------------------------------------------------
void onKey(Session& s, int k, uint32_t now) {
    (void)now;
    if (k == KEY_ESC || k == KEY_BREAK) { leave(s, "Left the serial session."); return; }

    if (g_operator != &s) {
        if (k == 't' || k == 'T') {
            if (!mayDrive(s)) {
                s.term.color(s.tl, Color::LightRed);
                s.term.text(s.tl, "You may watch, not drive.");
                s.term.nl(s.tl);
                return;
            }
            if (g_operator) {
                s.term.color(s.tl, Color::LightRed);
                s.term.text(s.tl, "Someone else has the keyboard.");
                s.term.nl(s.tl);
                return;
            }
            g_operator = &s;
            hint(s);
        }
        return;
    }

    uint8_t out[3];
    uint8_t n = 0;
    if (k == KEY_ENTER)          { out[n++] = '\r'; }
    else if (k == KEY_BACKSPACE) { out[n++] = 0x08; }
    else if (k >= 0x20 && k <= 0x7E) { out[n++] = static_cast<uint8_t>(k); }
    else return;
    g_txBytes += plat::serialWrite(out, n);
}

// ---------------------------------------------------------------------------
// tick: drain the port to everyone watching
// ---------------------------------------------------------------------------
void tick(uint32_t now) {
    (void)now;
    if (!plat::serialIsOpen()) return;
    uint8_t buf[kReadChunk];
    size_t n = plat::serialRead(buf, sizeof(buf));
    if (!n) return;
    g_rxBytes += static_cast<uint32_t>(n);
    keep(buf, n);
    toWatchers(buf, n);
}

void onLogoff(Session& s) {
    if (g_operator == &s) g_operator = nullptr;
}

bool start(Bbs& bbs) {
    (void)bbs;
    g_index = plugins::indexOf(kName);
    g_operator = nullptr;
    g_rxBytes = g_txBytes = 0;
    g_backLen = 0;
    plugins::forEachKey(g_index, readKey, nullptr);
    if (badPin(g_rx, false) || badPin(g_tx, true)) {
        plat::log("serial: pins rx %d tx %d are not usable", g_rx, g_tx);
        return false;
    }
    if (!plat::serialOpen(g_rx, g_tx, g_baud, g_bits, g_parity, g_stop)) {
        plat::log("serial: could not open the port on rx %d tx %d", g_rx, g_tx);
        return false;
    }
    plat::log("serial: rx %d tx %d at %u %u%c%u", g_rx, g_tx, static_cast<unsigned>(g_baud),
              g_bits, g_parity, g_stop);
    return true;
}

void stop() {
    g_operator = nullptr;
    plat::serialClose();
}

// setLine: "SERIAL SET 9600 8N1"
void setLine(Bbs& b, Session& s, const char* arg) {
    char rate[12] = {};
    char fmt[8]   = {};
    unsigned baud = 0;
    if (sscanf(arg, "%11s %7s", rate, fmt) < 1 || !(baud = static_cast<unsigned>(strtoul(rate, nullptr, 10)))) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "SERIAL SET 9600 [8N1]");
        b.prompt(s);
        return;
    }
    if (baud < 300 || baud > 921600) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "Speed must be 300 to 921600.");
        b.prompt(s);
        return;
    }
    g_baud = baud;
    if (fmt[0]) parseFormat(fmt);
    plat::serialSetLine(g_baud, g_bits, g_parity, g_stop);
    plat::log("serial: line set to %u %u%c%u by %s", static_cast<unsigned>(g_baud), g_bits,
              g_parity, g_stop, s.user);
    status(s, "changed");
    if (Bbs::instance().owns(s, g_index)) return;            // still in the session
    b.prompt(s);
}

const Command kCommands[] = {
    { "SERIAL", "", 0, CF_READ, "SERIAL", "watch the serial device",
      [](Bbs& b, Session& s, const char* a, uint32_t) {
          if (!*a)                    { join(b, s); return; }
          if (ieq(a, "STATUS"))       { status(s, plat::serialIsOpen() ? "" : "closed"); b.prompt(s); return; }
          if (!strncmp(a, "SET", 3) || !strncmp(a, "set", 3)) {
              if (!mayDrive(s)) {
                  s.term.color(s.tl, Color::LightRed);
                  s.term.text(s.tl, "You may watch, not drive.");
                  b.prompt(s);
                  return;
              }
              setLine(b, s, a + 3);
              return;
          }
          s.term.color(s.tl, Color::LightRed);
          s.term.text(s.tl, "SERIAL | SERIAL STATUS | SERIAL SET 9600 8N1");
          b.prompt(s);
      } },
};

} // namespace

extern const Plugin kSerialPlugin = {
    { kName, "Serial bridge", "1.0", 0, 0, PF_CORE, PlugLevel::All, PlugLevel::Staff, PlugLevel::Sysop },
    start,
    stop,
    tick,
    nullptr,                 // onConnect
    nullptr,                 // onLogin
    onLogoff,
    onKey,
    nullptr,                 // status
    kCommands,
    sizeof(kCommands) / sizeof(kCommands[0]),
    nullptr,                 // settings: nothing of its own in CONFIG yet
    0,
    nullptr,                 // setting
    nullptr,                 // rows: no paged list of its own
    nullptr,                 // onPresence
    nullptr,                 // onBytes
    nullptr,                 // onRename
};
