// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/plugins/doors.cpp
// Module:       Plugins / doors over the µnleashed link (1.2.0)
//
// Purpose:      The board's side of the DOOR family (LINK.md, "Family 2"):
//               a door box on the link offers doors, a caller picks one, the
//               board hands them over with a one-line handoff and stays in
//               charge of them the whole time.
//
//               Doors run somewhere else on purpose (CLAUDE.md, "Doors go
//               horizontal"): a door can be written in anything, changed
//               without reflashing the board, and cannot wedge it. The board
//               keeps what matters: who the caller is, their time, and the
//               right to take them back.
//
//               The rules, from LINK.md's "Security rules":
//                 - the board opens every door session; a box cannot reach a
//                   caller it was not handed;
//                 - nothing a box sends is read as a key, a command or a
//                   code: its bytes go to the caller's terminal and nowhere
//                   else;
//                 - time is the board's: TIMEUP with ten seconds' grace, then
//                   the caller comes back whatever the box does;
//                 - a box that goes quiet, or a session that fails, gives the
//                   caller back with a sentence rather than a hang.
//
//               Nothing is added to Session: the door session's id sits in
//               Session::ownerData, which is this plugin's while it owns the
//               caller. Everything else is allocated when the plugin starts.
//
//               The way out for a caller whatever the door does: Ctrl-]
//               three times in a row (0x1D, telnet's own escape character).
//
// Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1) and the Linux host build
// See also:     LINK.md, src/plugins/link.h, src/core/linkfam.h, COMMANDS.md
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
// with this program. If not, see <https://www.gnu.org/licenses/>. The full
// text is in the LICENSE file at the top of this repository.
// ===========================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "../core/bbs.h"
#include "../core/linkfam.h"
#include "../core/plugin.h"
#include "../platform/linkradio.h"   // linkAlloc: PSRAM where the board has it
#include "../platform/platform.h"
#include "link.h"

using namespace linkfam;

namespace {

constexpr const char kName[]    = "doors";
constexpr uint8_t    kMaxDoors  = 16;            // across every box
constexpr uint8_t    kSlots     = BBS_MAX_NODES + 2;
constexpr uint8_t    kKeyBuf    = 64;            // keys held while the box's window is full
constexpr uint32_t   kOpenMs    = 5000;
constexpr uint32_t   kGraceMs   = 10000;
constexpr int32_t    kTimeUpSec = 15;             // TIMEUP this long before the core hangs up

struct Door {
    bool    used;
    uint8_t peer;
    uint8_t id;
    uint8_t players;
    char    name[kDoorName + 1];
};

enum : uint8_t { ST_FREE, ST_OPENING, ST_IN, ST_TIMEUP };

// One caller in a door. Found by node, never by Session*: the pool reuses
// sessions, and a slot outliving its caller would hand the next one a door.
struct Slot {
    uint8_t  st;
    uint8_t  node;
    uint8_t  peer;
    uint16_t sess;
    uint8_t  door;
    uint32_t at;              // opening: when OPEN went; timeup: when TIMEUP went
    uint8_t  warned;          // 0, 5 or 1: the last WARN sent
    uint8_t  cols, rows;
    uint8_t  escapes;         // Ctrl-] in a row
    uint8_t  held;            // keys waiting in keys[]
    uint8_t  keys[kKeyBuf];
};

struct Ctx {
    Door doors[kMaxDoors];
    Slot slots[kSlots];
};

Ctx*    g_ctx   = nullptr;
uint8_t g_index = 0xFF;

Bbs& bbs() { return Bbs::instance(); }

Session* sessionOf(uint8_t node) {
    struct Find { uint8_t node; Session* s; } fd{ node, nullptr };
    bbs().eachSession([](void* ctx, Session& s) {
        Find* f = static_cast<Find*>(ctx);
        if (s.st != SState::Free && s.id == f->node) f->s = &s;
    }, &fd);
    if (fd.s && !bbs().owns(*fd.s, g_index)) return nullptr;
    return fd.s;
}

Slot* slotOfSession(uint8_t peer, uint16_t sess) {
    if (!g_ctx) return nullptr;
    for (Slot& sl : g_ctx->slots) if (sl.st != ST_FREE && sl.peer == peer && sl.sess == sess) return &sl;
    return nullptr;
}

Slot* slotOfNode(uint8_t node) {
    if (!g_ctx) return nullptr;
    for (Slot& sl : g_ctx->slots) if (sl.st != ST_FREE && sl.node == node) return &sl;
    return nullptr;
}

// giveBack: the caller comes back to the prompt with a line saying why. The
// session on the link is closed here, and CLOSE told to the box if asked.
void giveBack(Slot& sl, Color col, const char* why, uint8_t closeReason) {
    ulink::Engine* e = linkp::engine();
    if (e && closeReason) {
        // CLOSE is the last thing on the session; the engine forgets it once
        // CLOSE is acknowledged or has run out of tries.
        e->send(sl.peer, sl.sess, ulink::FAM_DOOR, DOOR_CLOSE, &closeReason, 1);
        e->closeAfter(sl.peer, sl.sess);
    } else if (e) {
        e->closeSession(sl.peer, sl.sess);
    }
    Session* s = sessionOf(sl.node);
    sl.st = ST_FREE;
    if (!s) return;
    bbs().setRawInput(*s, false);
    s->term.reset(s->tl);
    s->term.nl(s->tl);
    s->term.color(s->tl, col);
    s->term.text(s->tl, why);
    bbs().release(*s);
}

// ---------------------------------------------------------------------------
// The family
// ---------------------------------------------------------------------------
void listFrom(uint8_t peer, const uint8_t* p, size_t n) {
    if (!g_ctx || n < 2) return;
    for (Door& d : g_ctx->doors) if (d.used && d.peer == peer) d.used = false;
    const uint8_t count = p[1];
    size_t off = 2;
    for (uint8_t i = 0; i < count && off + 2 + kDoorName <= n; ++i, off += 2 + kDoorName) {
        for (Door& d : g_ctx->doors) {
            if (d.used) continue;
            d.used = true;
            d.peer = peer;
            d.id = p[off];
            d.players = p[off + 1];
            size_t k = 0;
            for (; k < kDoorName && p[off + 2 + k]; ++k) {
                const uint8_t ch = p[off + 2 + k];
                d.name[k] = (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : '?';
            }
            d.name[k] = '\0';
            break;
        }
    }
}

bool onMessage(uint8_t peer, uint16_t sess, uint8_t type, const uint8_t* p, size_t n) {
    if (type == DOOR_LIST) {
        listFrom(peer, p, n);
        if (ulink::Engine* e = linkp::engine()) {
            // A list on a session the board opened to ask for it: done with
            // it once this message's acknowledgement has gone.
            if (!(sess & 0x8000)) e->closeAfter(peer, sess);
        }
        return true;
    }
    Slot* sl = slotOfSession(peer, sess);
    if (!sl) return true;                          // a caller already gone: drop it
    Session* s = sessionOf(sl->node);
    if (!s) { giveBack(*sl, Color::Grey, "", DC_HUNGUP); return true; }

    switch (type) {
        case DOOR_OPEN_OK:
            if (sl->st == ST_OPENING) {
                sl->st = ST_IN;
                s->term.nl(s->tl);
                bbs().setRawInput(*s, true);
            }
            return true;
        case DOOR_REFUSED: {
            char why[80] = "The door said no.";
            if (n > 1) {
                size_t k = n - 1 > 60 ? 60 : n - 1;
                for (size_t i = 0; i < k; ++i) why[i] = (p[1 + i] >= 0x20 && p[1 + i] < 0x7F) ? static_cast<char>(p[1 + i]) : '?';
                why[k] = '\0';
            } else if (n == 1 && p[0] == DR_FULL) {
                snprintf(why, sizeof(why), "That door is full. Try again in a while.");
            }
            giveBack(*sl, Color::Yellow, why, 0);
            return true;
        }
        case DOOR_DATA: {
            if (sl->st != ST_IN && sl->st != ST_TIMEUP) return true;
            // Room for the worst case: every byte an 0xFF, doubled for telnet.
            if (s->tl.freeBytes() < 2 * n + 64 || s->tl.freeFrames() < 4) return false;
            s->term.raw(s->tl, p, n);
            return true;
        }
        case DOOR_FINISHED: {
            char why[80] = "Back from the door.";
            if (n > 1) {
                size_t k = n - 1 > 60 ? 60 : n - 1;
                for (size_t i = 0; i < k; ++i) why[i] = (p[1 + i] >= 0x20 && p[1 + i] < 0x7F) ? static_cast<char>(p[1 + i]) : '?';
                why[k] = '\0';
            }
            giveBack(*sl, Color::LightGreen, why, 0);
            return true;
        }
        default:
            return true;
    }
}

void onReset(uint8_t peer, uint16_t sess, uint8_t reason) {
    (void)reason;
    Slot* sl = slotOfSession(peer, sess);
    if (sl) giveBack(*sl, Color::Yellow, "The door has gone away.", 0);
}

void onPeerState(uint8_t peer, bool up) {
    ulink::Engine* e = linkp::engine();
    if (!g_ctx || !e) return;
    if (up && e->peerKind(peer) == ulink::KIND_DOORBOX) {
        // Ask what it offers.
        uint16_t sess = e->openSession(peer, ulink::FAM_DOOR);
        if (sess) e->send(peer, sess, ulink::FAM_DOOR, DOOR_LIST_ASK, nullptr, 0);
        return;
    }
    if (!up) {
        for (Door& d : g_ctx->doors) if (d.used && d.peer == peer) d.used = false;
        for (Slot& sl : g_ctx->slots)
            if (sl.st != ST_FREE && sl.peer == peer) giveBack(sl, Color::Yellow, "The door box went quiet.", 0);
    }
}

linkp::Family g_family = [] {
    linkp::Family f;
    f.id = ulink::FAM_DOOR;
    f.name = "door";
    f.message = onMessage;
    f.reset = onReset;
    f.peerState = onPeerState;
    return f;
}();

// ---------------------------------------------------------------------------
// The handoff line (LINK.md, "The handoff line")
// ---------------------------------------------------------------------------
void plusSpaces(const char* in, char* out, size_t n) {
    size_t k = 0;
    for (; *in && k + 1 < n; ++in) {
        char c = *in;
        if (c == ' ') c = '+';
        else if (c == '+' || c == '=' || c < 0x20 || c > 0x7E) c = '-';
        out[k++] = c;
    }
    out[k] = '\0';
}

const char* rankWord(const Session& s) {
    if (s.guest) return "guest";
    uint8_t r = s.rank;
    if (static_cast<uint8_t>(s.level) > r) r = static_cast<uint8_t>(s.level);
    switch (static_cast<Access>(r)) {
        case Access::Sysop:    return "sysop";
        case Access::CoSysop1: return "co1";
        case Access::CoSysop2: return "co2";
        default:               return "user";
    }
}

const char* termWord(const Session& s) {
    switch (s.term.type()) {
        case TermType::Ansi:  return "ansi";
        case TermType::Pet40: return "pet40";
        case TermType::Pet80: return "pet80";
        default:              return "ascii";
    }
}

size_t handoff(const Session& s, uint16_t sess, uint32_t now, char* out, size_t n) {
    char handle[BBS_USER_MAX * 2], board[48];
    plusSpaces(s.user, handle, sizeof(handle));
    plusSpaces(syscfg::get().boardName[0] ? syscfg::get().boardName : "unleashed", board, sizeof(board));
    int32_t mins = bbs().minutesLeft(s, now);
    int w = snprintf(out, n, "UNLEASHED-DOOR 1 node=%u session=%u handle=%s rank=%s cols=%u rows=%u term=%s minutes=%ld board=%s",
                     s.id, sess, handle, rankWord(s), s.term.cols(), s.term.rows(), termWord(s),
                     static_cast<long>(mins < 0 ? 9999 : mins), board);
    return w < 0 ? 0 : (static_cast<size_t>(w) < n ? static_cast<size_t>(w) : n - 1);
}

// ---------------------------------------------------------------------------
// Going in
// ---------------------------------------------------------------------------
void cmdDoors(Bbs& b, Session& s, const char* arg, uint32_t now) {
    ulink::Engine* e = linkp::engine();
    Ctx* c = g_ctx;
    s.term.color(s.tl, Color::Cyan);
    if (!e || !c) {
        s.term.text(s.tl, "No door boxes: the link is off.");
        b.prompt(s);
        return;
    }
    while (*arg == ' ') ++arg;
    if (!*arg) {
        b.rowTitle(s, "Doors");
        uint8_t shown = 0;
        for (uint8_t i = 0; i < kMaxDoors; ++i) {
            const Door& d = c->doors[i];
            if (!d.used || !e->peerUp(d.peer)) continue;
            char line[64];
            snprintf(line, sizeof(line), " %2u  %s", static_cast<unsigned>(i + 1), d.name);
            b.rowText(s, Color::White, line);
            ++shown;
        }
        if (!shown) b.rowText(s, Color::Grey, "No door box is on the air.");
        else        b.rowText(s, Color::Grey, "DOORS n opens one. Ctrl-] three times leaves.");
        b.rowRule(s);
        b.prompt(s);
        return;
    }
    long n = strtol(arg, nullptr, 10);
    if (n < 1 || n > kMaxDoors || !c->doors[n - 1].used || !e->peerUp(c->doors[n - 1].peer)) {
        s.term.text(s.tl, "No door by that number. DOORS lists them.");
        b.prompt(s);
        return;
    }
    const Door& d = c->doors[n - 1];
    Slot* sl = nullptr;
    for (Slot& x : c->slots) if (x.st == ST_FREE) { sl = &x; break; }
    const uint16_t sess = sl ? e->openSession(d.peer, ulink::FAM_DOOR) : 0;
    if (!sess) {
        s.term.text(s.tl, "The link is busy. Try again in a moment.");
        b.prompt(s);
        return;
    }
    uint8_t msg[ulink::kPayloadMax];
    msg[0] = d.id;
    size_t len = 1 + handoff(s, sess, now, reinterpret_cast<char*>(msg + 1), sizeof(msg) - 1);
    if (e->send(d.peer, sess, ulink::FAM_DOOR, DOOR_OPEN, msg, len) != 1 || !b.own(s, g_index)) {
        e->closeSession(d.peer, sess);
        s.term.text(s.tl, "The link is busy. Try again in a moment.");
        b.prompt(s);
        return;
    }
    memset(sl, 0, sizeof(*sl));
    sl->st = ST_OPENING;
    sl->node = s.id;
    sl->peer = d.peer;
    sl->sess = sess;
    sl->door = static_cast<uint8_t>(n - 1);
    sl->at = now;
    sl->cols = s.term.cols();
    sl->rows = s.term.rows();
    s.ownerData = sess;
    char doing[BBS_DOING_MAX + 1];
    snprintf(doing, sizeof(doing), "DOOR");
    b.setDoing(s, doing);
    char line[64];
    snprintf(line, sizeof(line), "Opening %s...", d.name);
    s.term.text(s.tl, line);
}

// Keys from a caller in a door: as bytes (raw input), straight to the box.
void flush(Slot& sl) {
    ulink::Engine* e = linkp::engine();
    if (!e || !sl.held) return;
    if (e->send(sl.peer, sl.sess, ulink::FAM_DOOR, DOOR_DATA, sl.keys, sl.held) == 1) sl.held = 0;
}

void onBytes(Session& s, const uint8_t* b, size_t n, uint32_t now) {
    (void)now;
    Slot* sl = slotOfNode(s.id);
    if (!sl) { bbs().setRawInput(s, false); bbs().release(s); return; }
    for (size_t i = 0; i < n; ++i) {
        if (b[i] == 0x1D) {
            if (++sl->escapes >= 3) { giveBack(*sl, Color::Grey, "You left the door.", DC_TAKENBACK); return; }
        } else {
            sl->escapes = 0;
        }
        if (sl->st != ST_IN) continue;
        if (sl->held < kKeyBuf) sl->keys[sl->held++] = b[i];
        // A full buffer drops keys rather than blocking the board: a caller
        // typing 64 keys ahead of a box that has stopped reading.
    }
    flush(*sl);
}

void onKey(Session& s, int k, uint32_t now) {
    (void)k;
    (void)now;
    // Keys arrive as bytes once the door has opened; before that, only a way out.
    Slot* sl = slotOfNode(s.id);
    if (!sl) { bbs().release(s); return; }
    if (k == KEY_ESC || k == KEY_BREAK) giveBack(*sl, Color::Grey, "Stopped.", DC_TAKENBACK);
}

void onLogoff(Session& s) {
    Slot* sl = slotOfNode(s.id);
    if (!sl) return;
    ulink::Engine* e = linkp::engine();
    const uint8_t r = DC_HUNGUP;
    if (e) {
        e->send(sl->peer, sl->sess, ulink::FAM_DOOR, DOOR_CLOSE, &r, 1);
        e->closeAfter(sl->peer, sl->sess);
    }
    sl->st = ST_FREE;
}

void tick(uint32_t now) {
    Ctx* c = g_ctx;
    ulink::Engine* e = linkp::engine();
    if (!c) return;
    for (Slot& sl : c->slots) {
        if (sl.st == ST_FREE) continue;
        Session* s = sessionOf(sl.node);
        if (!s || !e) { giveBack(sl, Color::Grey, "The link stopped.", 0); continue; }
        if (sl.st == ST_OPENING) {
            if (now - sl.at > kOpenMs) giveBack(sl, Color::Yellow, "The door did not answer.", DC_TAKENBACK);
            continue;
        }
        flush(sl);
        // The terminal changed size (NAWS).
        if (s->term.cols() != sl.cols || s->term.rows() != sl.rows) {
            uint8_t g[2] = { s->term.cols(), s->term.rows() };
            if (e->send(sl.peer, sl.sess, ulink::FAM_DOOR, DOOR_RESIZE, g, 2) == 1) { sl.cols = g[0]; sl.rows = g[1]; }
        }
        // Time is the board's.
        const int32_t secs = bbs().callSecondsLeft(*s, now);
        if (secs < 0) continue;
        if (sl.st == ST_TIMEUP) {
            if (now - sl.at > kGraceMs) giveBack(sl, Color::Yellow, "Time's up.", DC_TIMEUP);
            continue;
        }
        if (secs <= kTimeUpSec) {
            if (e->send(sl.peer, sl.sess, ulink::FAM_DOOR, DOOR_TIMEUP, nullptr, 0) == 1) {
                sl.st = ST_TIMEUP;
                sl.at = now;
            }
            continue;
        }
        const uint8_t mins = secs <= 60 ? 1 : (secs <= 300 ? 5 : 0);
        if (mins && sl.warned != mins) {
            if (e->send(sl.peer, sl.sess, ulink::FAM_DOOR, DOOR_WARN, &mins, 1) == 1) sl.warned = mins;
        }
    }
}

bool start(Bbs&) {
    g_index = plugins::indexOf(kName);
    void* mem = plat::linkAlloc(sizeof(Ctx));
    if (!mem) return false;
    g_ctx = new (mem) Ctx();
    memset(g_ctx, 0, sizeof(Ctx));
    linkp::registerFamily(g_family);
    // A box already up when the doors start (a CONFIG save) is asked now.
    if (ulink::Engine* e = linkp::engine())
        for (uint8_t i = 0; i < ulink::Engine::kPeers; ++i)
            if (e->peerUp(i)) onPeerState(i, true);
    return true;
}

void stop() {
    linkp::unregisterFamily(ulink::FAM_DOOR);
    if (!g_ctx) return;
    for (Slot& sl : g_ctx->slots) if (sl.st != ST_FREE) giveBack(sl, Color::Grey, "The doors are closing.", DC_CLOSING);
    plat::linkFree(g_ctx);
    g_ctx = nullptr;
}

const Command kCommands[] = {
    { "DOORS", "", 0, CF_READ, "DOORS [n]", "games and more, on a door box", cmdDoors, Menu::Main, 40 },
};

}  // namespace

extern const Plugin kDoorsPlugin = {
    { kName, "Doors on the unleashed link", "1.0", sizeof(Ctx) + 256, 0, PF_CORE,
      PlugLevel::Users, PlugLevel::Users, PlugLevel::Sysop },
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
    nullptr,                 // settings
    0,
    nullptr,                 // setting
    nullptr,                 // rows
    nullptr,                 // onPresence
    onBytes,
    nullptr,                 // onRename
    nullptr,                 // listDone
};
